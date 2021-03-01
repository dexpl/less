int prc=0;
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include "lesstest.h"
#undef countof
#define countof(a) (sizeof(a)/sizeof(*a))

const char version[] = "lt_screen|v=1";

int usage() {
	fprintf(stderr, "usage: lt_screen\n");
	return 0;
}

// ------------------------------------------------------------------

#define MAX_PARAMS         3

typedef struct ScreenChar {
	char ch;
	unsigned char attr;
	unsigned char fg_color;
	unsigned char bg_color;
} ScreenChar;

typedef struct ScreenState {
	ScreenChar* chars;
	int w;
	int h;
	int cx;
	int cy;
	unsigned char curr_attr;
	unsigned char curr_fg_color;
	unsigned char curr_bg_color;
	int param_top;
	int params[MAX_PARAMS+1];
	int in_esc;
} ScreenState;

static ScreenState screen;
static int ttyin; // input text and control sequences
static int ttyout; // output for R command etc.
static int quiet = 0;
static int verbose = 0;

// ------------------------------------------------------------------

void screen_init() {
	screen.w = 80;
	screen.h = 24;
	screen.cx = 0;
	screen.cy = 0;
	screen.in_esc = 0;
	screen.curr_attr = 0;
	screen.curr_fg_color = screen.curr_bg_color = 0;
	screen.param_top = 0;
	screen.params[0] = 0; // start with a 0 on the stack
}

void param_push(int v) {
	if (screen.param_top >= countof(screen.params)-1)
		return;
	screen.params[++screen.param_top] = v;
}

int param_pop() {
	int v = screen.params[screen.param_top];
	if (screen.param_top > 0)
		--screen.param_top;
	return v;
}

int screen_x(int x) {
	if (x < 0) x = 0;
	if (x >= screen.w) x = screen.w-1;
	return x;
}

int screen_y(int y) {
	if (y < 0) y = 0;
	if (y >= screen.h) y = screen.h-1;
	return y;
}

ScreenChar* screen_char(int x, int y) {
	x = screen_x(x);
	y = screen_x(y);
	return &screen.chars[y * screen.w + x];
}

int screen_incr(int* px, int* py) {
	if (++(*px) >= screen.w) {
		*px = 0;
		if (++(*py) >= screen.h) {
			*py = 0;
			return 0;
		}
	}
	return 1;
}

void screen_char_set(ScreenChar* sc, char ch, unsigned char attr, unsigned char fg_color, unsigned char bg_color) {
	sc->ch = ch;
	sc->attr = attr;
	sc->fg_color = fg_color;
	sc->bg_color = bg_color;
}

int screen_clear(int x, int y, int count) {
	while (count-- > 0) {
		ScreenChar* sc = screen_char(x, y);
		screen_char_set(sc, '_', 0, 0, 0);
		screen_incr(&x, &y);
	}
	return 1;
}

int screen_busy() {
	write(ttyout, "*BUSY*\n", 7);
	return 1;
}
int screen_read(int x, int y, int count) {
	//write(ttyout, "$|", 2);
	int attr = 0;
	int fg_color = 0;
	int bg_color = 0;
	while (count-- > 0) {
		char buf[32];
		char* bufp = buf;
		ScreenChar* sc = screen_char(x, y);
		if (sc->attr != attr) {
			attr = sc->attr;
			*bufp++ = '@';
			*bufp++ = attr;
		}
		if (sc->fg_color != fg_color || sc->bg_color != bg_color) {
			fg_color = sc->fg_color;
			bg_color = sc->bg_color;
			*bufp++ = '$';
			*bufp++ = fg_color;
			*bufp++ = bg_color;
		}
		if (sc->ch == '@' || sc->ch == '$' || sc->ch == '\\' || sc->ch == '#')
			*bufp++ = '\\';
		*bufp++ = sc->ch;
		if (x == screen.cx && y == screen.cy)
			*bufp++ = '#';
		write(ttyout, buf, bufp-buf);
		screen_incr(&x, &y);
	}
	write(ttyout, "\n", 1);
	return 1;
}

int screen_move(int x, int y) {
	screen.cx = x;
	screen.cy = y;
	return 1;
}

int screen_cr() {
	screen.cx = 0;
	return 1;
}

int screen_bs() {
	if (screen.cx <= 0) return 0;
	--screen.cx;
	return 1;
}

int screen_scroll() {
	int len = screen.w * (screen.h-1);
	memmove(screen_char(0,0), screen_char(0,1), len * sizeof(ScreenChar));
	screen_clear(0, screen.h-1, screen.w);
	return 1;
}

int screen_rscroll() {
	int len = screen.w * (screen.h-1);
	memmove(screen_char(0,1), screen_char(0,0), len * sizeof(ScreenChar));
	screen_clear(0, 0, screen.w);
	return 1;
}

int screen_set_attr(int attr) {
	screen.curr_attr |= attr;
	return 0;
}

int screen_clear_attr(int attr) {
	screen.curr_attr &= ~attr;
	return 0;
}

// ------------------------------------------------------------------ 

void beep() {
	if (!quiet)
		fprintf(stderr, "\7");
}

int exec_esc(char ch)
{
	int x, y, count;
	if (verbose) {
		fprintf(stderr, "exec ESC-%c ", ch);
		int i;
		for (i = 0; i <= screen.param_top; ++i)
			fprintf(stderr, "%d ", screen.params[i]);
		fprintf(stderr, "\n");
	}
	switch (ch) {
	case 'A': // clear all 
		return screen_clear(0, 0, screen.w * screen.h);
	case 'L': // clear to end of line 
		return screen_clear(screen.cx, screen.cy, screen.w - screen.cx);
	case 'S': // clear to end of screen 
		return screen_clear(screen.cx, screen.cy, 
			(screen.w - screen.cx) + (screen.h - screen.cy -1) * screen.w);
	case 'R': // read 
		count = param_pop();
		y = param_pop();
		x = param_pop();
		return screen_read(x, y, count);
	case 'j': // move 
		y = param_pop();
		x = param_pop();
		return screen_move(x, y);
	case 'g': // visual bell 
		return 0;
	case 'h': // home 
		return screen_move(0, 0);
	case 'l': // lower left 
		return screen_move(0, screen.h-1);
	case 'r':
		return screen_rscroll();
	case '<': // cursor left
		return screen_cr();
	case 's':
		return screen_set_attr(ATTR_STANDOUT);
	case 't':
		return screen_clear_attr(ATTR_STANDOUT);
	case 'u':
		return screen_set_attr(ATTR_UNDERLINE);
	case 'v':
		return screen_clear_attr(ATTR_UNDERLINE);
	case 'd':
		return screen_set_attr(ATTR_BOLD);
	case 'e':
		return screen_clear_attr(ATTR_BOLD);
	case 'b':
		return screen_set_attr(ATTR_BLINK);
	case 'c':
		return screen_clear_attr(ATTR_BLINK);
	case '?':
		write(ttyout, version, strlen(version));
		return 1;
	default:
		return 0;
	}
}

int add_char(char ch) {
	if (verbose) fprintf(stderr, "add %c at %d,%d\n", ch, screen.cx, screen.cy);
	ScreenChar* sc = screen_char(screen.cx, screen.cy);
	screen_char_set(sc, ch, screen.curr_attr, screen.curr_fg_color, screen.curr_bg_color);
	if (!screen_incr(&screen.cx, &screen.cy)) {
		screen.cx = 0;
		screen.cy = screen.h-1;
		return screen_scroll();
	}
	return 1;
}

int process_char(char ch)
{
	int ok = 1;
	if (screen.in_esc) {
		if (ch >= '0' && ch <= '9') {
			param_push(10 * param_pop() + ch - '0');
		} else if (ch == ';') {
			param_push(0);
		} else {
			screen.in_esc = 0;
			ok = exec_esc(ch);
		}
	} else if (ch == ESC) {
		screen.in_esc = 1;
	} else if (ch == '\r') {
		screen_cr();
	} else if (ch == '\b') {
		screen_bs();
	} else if (ch == '\n') {
		if (screen.cy < screen.h-1)
			++screen.cy;
		else 
			screen_scroll();
		screen.cx = 0; // auto CR
	} else if (ch == '\t') {
		ok = add_char(' ');
	} else if (ch >= '\40') {
		ok = add_char(ch);
	}
	return ok;
}

void screen_dump_handler(int signum) {
	// (signum == LTSIG_READ_SCREEN)
	if (verbose) fprintf(stderr, "screen: rcv dump signal\n");
	if (prc) {
fprintf(stderr, "******* BUSY???\n");
		screen_busy();
	} else {
		(void) screen_read(0, 0, screen.w * screen.h);
	}
}

// ------------------------------------------------------------------ 

int setup(int argc, char** argv)
{
	int ch;
	int ready_pid = 0;
	screen_init();
	while ((ch = getopt(argc, argv, "h:qr:vw:")) != -1) {
		switch (ch) {
		case 'h':
			screen.h = atoi(optarg);
			break;
		case 'q':
			quiet = 1;
			break;
		case 'r':
			ready_pid = atoi(optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'w':
			screen.w = atoi(optarg);
			break;
		default:
			return usage();
		}
	}
	int len = screen.w * screen.h;
	screen.chars = malloc(len * sizeof(ScreenChar));
	screen_clear(0, 0, len);
	if (optind >= argc) {
		ttyin = 0;
		ttyout = 1;
	} else {
		ttyin = ttyout = open(argv[optind], O_RDWR);
		if (ttyin < 0) {
			fprintf(stderr, "cannot open %s\n", argv[optind]);
			return 0;
		}
	}
fprintf(stderr, "set dump hdlr\n");
	signal(LTSIG_SCREEN_DUMP, screen_dump_handler);
fprintf(stderr, "signal ready %ld\n", (long)ready_pid);
	if (ready_pid != 0)
		kill(ready_pid, LTSIG_SCREEN_READY);
	return 1;
}

int main(int argc, char** argv)
{
	if (!setup(argc, argv))
		return 1;
	for (;;) {
		char ch;
		int n = read(ttyin, &ch, 1);
		if (n < 0)
			return 0;
		if (n == 0)
			break;
		if (verbose) fprintf(stderr, "screen read %c (%02x)\n", ch >= ' ' && ch < 0x7f ? ch : '.', ch);
prc=1;
		if (!process_char(ch))
			beep();
prc=0;
	}
	return 1;
}
