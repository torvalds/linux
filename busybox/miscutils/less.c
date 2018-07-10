/* vi: set sw=4 ts=4: */
/*
 * Mini less implementation for busybox
 *
 * Copyright (C) 2005 by Rob Sullivan <cogito.ergo.cogito@gmail.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/*
 * TODO:
 * - Add more regular expression support - search modifiers, certain matches, etc.
 * - Add more complex bracket searching - currently, nested brackets are
 *   not considered.
 * - Add support for "F" as an input. This causes less to act in
 *   a similar way to tail -f.
 * - Allow horizontal scrolling.
 *
 * Notes:
 * - the inp file pointer is used so that keyboard input works after
 *   redirected input has been read from stdin
 */
//config:config LESS
//config:	bool "less (15 kb)"
//config:	default y
//config:	help
//config:	'less' is a pager, meaning that it displays text files. It possesses
//config:	a wide array of features, and is an improvement over 'more'.
//config:
//config:config FEATURE_LESS_MAXLINES
//config:	int "Max number of input lines less will try to eat"
//config:	default 9999999
//config:	depends on LESS
//config:
//config:config FEATURE_LESS_BRACKETS
//config:	bool "Enable bracket searching"
//config:	default y
//config:	depends on LESS
//config:	help
//config:	This option adds the capability to search for matching left and right
//config:	brackets, facilitating programming.
//config:
//config:config FEATURE_LESS_FLAGS
//config:	bool "Enable -m/-M"
//config:	default y
//config:	depends on LESS
//config:	help
//config:	The -M/-m flag enables a more sophisticated status line.
//config:
//config:config FEATURE_LESS_TRUNCATE
//config:	bool "Enable -S"
//config:	default y
//config:	depends on LESS
//config:	help
//config:	The -S flag causes long lines to be truncated rather than
//config:	wrapped.
//config:
//config:config FEATURE_LESS_MARKS
//config:	bool "Enable marks"
//config:	default y
//config:	depends on LESS
//config:	help
//config:	Marks enable positions in a file to be stored for easy reference.
//config:
//config:config FEATURE_LESS_REGEXP
//config:	bool "Enable regular expressions"
//config:	default y
//config:	depends on LESS
//config:	help
//config:	Enable regular expressions, allowing complex file searches.
//config:
//config:config FEATURE_LESS_WINCH
//config:	bool "Enable automatic resizing on window size changes"
//config:	default y
//config:	depends on LESS
//config:	help
//config:	Makes less track window size changes.
//config:
//config:config FEATURE_LESS_ASK_TERMINAL
//config:	bool "Use 'tell me cursor position' ESC sequence to measure window"
//config:	default y
//config:	depends on FEATURE_LESS_WINCH
//config:	help
//config:	Makes less track window size changes.
//config:	If terminal size can't be retrieved and $LINES/$COLUMNS are not set,
//config:	this option makes less perform a last-ditch effort to find it:
//config:	position cursor to 999,999 and ask terminal to report real
//config:	cursor position using "ESC [ 6 n" escape sequence, then read stdin.
//config:	This is not clean but helps a lot on serial lines and such.
//config:
//config:config FEATURE_LESS_DASHCMD
//config:	bool "Enable flag changes ('-' command)"
//config:	default y
//config:	depends on LESS
//config:	help
//config:	This enables the ability to change command-line flags within
//config:	less itself ('-' keyboard command).
//config:
//config:config FEATURE_LESS_LINENUMS
//config:	bool "Enable -N (dynamic switching of line numbers)"
//config:	default y
//config:	depends on FEATURE_LESS_DASHCMD
//config:
//config:config FEATURE_LESS_RAW
//config:	bool "Enable -R ('raw control characters')"
//config:	default y
//config:	depends on FEATURE_LESS_DASHCMD
//config:	help
//config:	This is essential for less applet to work with tools that use colors
//config:	and paging, such as git, systemd tools or nmcli.
//config:
//config:config FEATURE_LESS_ENV
//config:	bool "Take options from $LESS environment variable"
//config:	default y
//config:	depends on FEATURE_LESS_DASHCMD
//config:	help
//config:	This is essential for less applet to work with tools that use colors
//config:	and paging, such as git, systemd tools or nmcli.

//applet:IF_LESS(APPLET(less, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_LESS) += less.o

//usage:#define less_trivial_usage
//usage:       "[-EF" IF_FEATURE_LESS_REGEXP("I")IF_FEATURE_LESS_FLAGS("Mm")
//usage:       "N" IF_FEATURE_LESS_TRUNCATE("S") IF_FEATURE_LESS_RAW("R") "h~] [FILE]..."
//usage:#define less_full_usage "\n\n"
//usage:       "View FILE (or stdin) one screenful at a time\n"
//usage:     "\n	-E	Quit once the end of a file is reached"
//usage:     "\n	-F	Quit if entire file fits on first screen"
//usage:	IF_FEATURE_LESS_REGEXP(
//usage:     "\n	-I	Ignore case in all searches"
//usage:	)
//usage:	IF_FEATURE_LESS_FLAGS(
//usage:     "\n	-M,-m	Display status line with line numbers"
//usage:     "\n		and percentage through the file"
//usage:	)
//usage:     "\n	-N	Prefix line number to each line"
//usage:	IF_FEATURE_LESS_TRUNCATE(
//usage:     "\n	-S	Truncate long lines"
//usage:	)
//usage:	IF_FEATURE_LESS_RAW(
//usage:     "\n	-R	Remove color escape codes in input"
//usage:	)
//usage:     "\n	-~	Suppress ~s displayed past EOF"

#include <sched.h>  /* sched_yield() */

#include "libbb.h"
#include "common_bufsiz.h"
#if ENABLE_FEATURE_LESS_REGEXP
#include "xregex.h"
#endif


#define ESC "\033"
/* The escape codes for highlighted and normal text */
#define HIGHLIGHT   ESC"[7m"
#define NORMAL      ESC"[m"
/* The escape code to home and clear to the end of screen */
#define CLEAR       ESC"[H"ESC"[J"
/* The escape code to clear to the end of line */
#define CLEAR_2_EOL ESC"[K"

enum {
/* Absolute max of lines eaten */
	MAXLINES = CONFIG_FEATURE_LESS_MAXLINES,
/* This many "after the end" lines we will show (at max) */
	TILDES = 1,
};

/* Command line options */
enum {
	FLAG_E = 1 << 0,
	FLAG_M = 1 << 1,
	FLAG_m = 1 << 2,
	FLAG_N = 1 << 3,
	FLAG_TILDE = 1 << 4,
	FLAG_I = 1 << 5,
	FLAG_F = 1 << 6,
	FLAG_S = (1 << 7) * ENABLE_FEATURE_LESS_TRUNCATE,
	FLAG_R = (1 << 8) * ENABLE_FEATURE_LESS_RAW,
/* hijack command line options variable for internal state vars */
	LESS_STATE_MATCH_BACKWARDS = 1 << 15,
};

#if !ENABLE_FEATURE_LESS_REGEXP
enum { pattern_valid = 0 };
#endif

struct globals {
	int cur_fline; /* signed */
	int kbd_fd;  /* fd to get input from */
	int kbd_fd_orig_flags;
	int less_gets_pos;
/* last position in last line, taking into account tabs */
	size_t last_line_pos;
	unsigned max_fline;
	unsigned max_lineno; /* this one tracks linewrap */
	unsigned max_displayed_line;
	unsigned width;
#if ENABLE_FEATURE_LESS_WINCH
	unsigned winch_counter;
#endif
	ssize_t eof_error; /* eof if 0, error if < 0 */
	ssize_t readpos;
	ssize_t readeof; /* must be signed */
	const char **buffer;
	const char **flines;
	const char *empty_line_marker;
	unsigned num_files;
	unsigned current_file;
	char *filename;
	char **files;
#if ENABLE_FEATURE_LESS_FLAGS
	int num_lines; /* a flag if < 0, line count if >= 0 */
# define REOPEN_AND_COUNT (-1)
# define REOPEN_STDIN     (-2)
# define NOT_REGULAR_FILE (-3)
#endif
#if ENABLE_FEATURE_LESS_MARKS
	unsigned num_marks;
	unsigned mark_lines[15][2];
#endif
#if ENABLE_FEATURE_LESS_REGEXP
	unsigned *match_lines;
	int match_pos; /* signed! */
	int wanted_match; /* signed! */
	int num_matches;
	regex_t pattern;
	smallint pattern_valid;
#endif
#if ENABLE_FEATURE_LESS_RAW
	smallint in_escape;
#endif
#if ENABLE_FEATURE_LESS_ASK_TERMINAL
	smallint winsize_err;
#endif
	smallint terminated;
	struct termios term_orig, term_less;
	char kbd_input[KEYCODE_BUFFER_SIZE];
};
#define G (*ptr_to_globals)
#define cur_fline           (G.cur_fline         )
#define kbd_fd              (G.kbd_fd            )
#define less_gets_pos       (G.less_gets_pos     )
#define last_line_pos       (G.last_line_pos     )
#define max_fline           (G.max_fline         )
#define max_lineno          (G.max_lineno        )
#define max_displayed_line  (G.max_displayed_line)
#define width               (G.width             )
#define winch_counter       (G.winch_counter     )
/* This one is 100% not cached by compiler on read access */
#define WINCH_COUNTER (*(volatile unsigned *)&winch_counter)
#define eof_error           (G.eof_error         )
#define readpos             (G.readpos           )
#define readeof             (G.readeof           )
#define buffer              (G.buffer            )
#define flines              (G.flines            )
#define empty_line_marker   (G.empty_line_marker )
#define num_files           (G.num_files         )
#define current_file        (G.current_file      )
#define filename            (G.filename          )
#define files               (G.files             )
#define num_lines           (G.num_lines         )
#define num_marks           (G.num_marks         )
#define mark_lines          (G.mark_lines        )
#if ENABLE_FEATURE_LESS_REGEXP
#define match_lines         (G.match_lines       )
#define match_pos           (G.match_pos         )
#define num_matches         (G.num_matches       )
#define wanted_match        (G.wanted_match      )
#define pattern             (G.pattern           )
#define pattern_valid       (G.pattern_valid     )
#endif
#define terminated          (G.terminated        )
#define term_orig           (G.term_orig         )
#define term_less           (G.term_less         )
#define kbd_input           (G.kbd_input         )
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
	less_gets_pos = -1; \
	empty_line_marker = "~"; \
	current_file = 1; \
	eof_error = 1; \
	terminated = 1; \
	IF_FEATURE_LESS_REGEXP(wanted_match = -1;) \
} while (0)

/* flines[] are lines read from stdin, each in malloc'ed buffer.
 * Line numbers are stored as uint32_t prepended to each line.
 * Pointer is adjusted so that flines[i] points directly past
 * line number. Accessor: */
#define MEMPTR(p) ((char*)(p) - 4)
#define LINENO(p) (*(uint32_t*)((p) - 4))


/* Reset terminal input to normal */
static void set_tty_cooked(void)
{
	fflush_all();
	tcsetattr(kbd_fd, TCSANOW, &term_orig);
}

/* Move the cursor to a position (x,y), where (0,0) is the
   top-left corner of the console */
static void move_cursor(int line, int col)
{
	printf(ESC"[%u;%uH", line, col);
}

static void clear_line(void)
{
	printf(ESC"[%u;0H" CLEAR_2_EOL, max_displayed_line + 2);
}

static void print_hilite(const char *str)
{
	printf(HIGHLIGHT"%s"NORMAL, str);
}

static void print_statusline(const char *str)
{
	clear_line();
	printf(HIGHLIGHT"%.*s"NORMAL, width - 1, str);
}

/* Exit the program gracefully */
static void less_exit(int code)
{
	set_tty_cooked();
	if (!(G.kbd_fd_orig_flags & O_NONBLOCK))
		ndelay_off(kbd_fd);
	clear_line();
	if (code < 0)
		kill_myself_with_sig(- code); /* does not return */
	exit(code);
}

#if (ENABLE_FEATURE_LESS_DASHCMD && ENABLE_FEATURE_LESS_LINENUMS) \
 || ENABLE_FEATURE_LESS_WINCH
static void re_wrap(void)
{
	int w = width;
	int new_line_pos;
	int src_idx;
	int dst_idx;
	int new_cur_fline = 0;
	uint32_t lineno;
	char linebuf[w + 1];
	const char **old_flines = flines;
	const char *s;
	char **new_flines = NULL;
	char *d;

	if (option_mask32 & FLAG_N)
		w -= 8;

	src_idx = 0;
	dst_idx = 0;
	s = old_flines[0];
	lineno = LINENO(s);
	d = linebuf;
	new_line_pos = 0;
	while (1) {
		*d = *s;
		if (*d != '\0') {
			new_line_pos++;
			if (*d == '\t') { /* tab */
				new_line_pos += 7;
				new_line_pos &= (~7);
			}
			s++;
			d++;
			if (new_line_pos >= w) {
				int sz;
				/* new line is full, create next one */
				*d = '\0';
 next_new:
				sz = (d - linebuf) + 1; /* + 1: NUL */
				d = ((char*)xmalloc(sz + 4)) + 4;
				LINENO(d) = lineno;
				memcpy(d, linebuf, sz);
				new_flines = xrealloc_vector(new_flines, 8, dst_idx);
				new_flines[dst_idx] = d;
				dst_idx++;
				if (new_line_pos < w) {
					/* if we came here thru "goto next_new" */
					if (src_idx > max_fline)
						break;
					lineno = LINENO(s);
				}
				d = linebuf;
				new_line_pos = 0;
			}
			continue;
		}
		/* *d == NUL: old line ended, go to next old one */
		free(MEMPTR(old_flines[src_idx]));
		/* btw, convert cur_fline... */
		if (cur_fline == src_idx)
			new_cur_fline = dst_idx;
		src_idx++;
		/* no more lines? finish last new line (and exit the loop) */
		if (src_idx > max_fline)
			goto next_new;
		s = old_flines[src_idx];
		if (lineno != LINENO(s)) {
			/* this is not a continuation line!
			 * create next _new_ line too */
			goto next_new;
		}
	}

	free(old_flines);
	flines = (const char **)new_flines;

	max_fline = dst_idx - 1;
	last_line_pos = new_line_pos;
	cur_fline = new_cur_fline;
	/* max_lineno is screen-size independent */
#if ENABLE_FEATURE_LESS_REGEXP
	pattern_valid = 0;
#endif
}
#endif

#if ENABLE_FEATURE_LESS_REGEXP
static void fill_match_lines(unsigned pos);
#else
#define fill_match_lines(pos) ((void)0)
#endif

static int at_end(void)
{
	return (option_mask32 & FLAG_S)
		? !(cur_fline <= max_fline &&
			max_lineno > LINENO(flines[cur_fline]) + max_displayed_line)
		: !(max_fline > cur_fline + max_displayed_line);
}

/* Devilishly complex routine.
 *
 * Has to deal with EOF and EPIPE on input,
 * with line wrapping, with last line not ending in '\n'
 * (possibly not ending YET!), with backspace and tabs.
 * It reads input again if last time we got an EOF (thus supporting
 * growing files) or EPIPE (watching output of slow process like make).
 *
 * Variables used:
 * flines[] - array of lines already read. Linewrap may cause
 *      one source file line to occupy several flines[n].
 * flines[max_fline] - last line, possibly incomplete.
 * terminated - 1 if flines[max_fline] is 'terminated'
 *      (if there was '\n' [which isn't stored itself, we just remember
 *      that it was seen])
 * max_lineno - last line's number, this one doesn't increment
 *      on line wrap, only on "real" new lines.
 * readbuf[0..readeof-1] - small preliminary buffer.
 * readbuf[readpos] - next character to add to current line.
 * last_line_pos - screen line position of next char to be read
 *      (takes into account tabs and backspaces)
 * eof_error - < 0 error, == 0 EOF, > 0 not EOF/error
 *
 * "git log -p | less -m" on the kernel git tree is a good test for EAGAINs,
 * "/search on very long input" and "reaching max line count" corner cases.
 */
static void read_lines(void)
{
	char *current_line, *p;
	int w = width;
	char last_terminated = terminated;
	time_t last_time = 0;
	int retry_EAGAIN = 2;
#if ENABLE_FEATURE_LESS_REGEXP
	unsigned old_max_fline = max_fline;
#endif

#define readbuf bb_common_bufsiz1
	setup_common_bufsiz();

	/* (careful: max_fline can be -1) */
	if (max_fline + 1 > MAXLINES)
		return;

	if (option_mask32 & FLAG_N)
		w -= 8;

	p = current_line = ((char*)xmalloc(w + 5)) + 4;
	if (!last_terminated) {
		const char *cp = flines[max_fline];
		p = stpcpy(p, cp);
		free(MEMPTR(cp));
		/* last_line_pos is still valid from previous read_lines() */
	} else {
		max_fline++;
		last_line_pos = 0;
	}

	while (1) { /* read lines until we reach cur_fline or wanted_match */
		*p = '\0';
		terminated = 0;
		while (1) { /* read chars until we have a line */
			char c;
			/* if no unprocessed chars left, eat more */
			if (readpos >= readeof) {
				int flags = ndelay_on(0);

				while (1) {
					time_t t;

					errno = 0;
					eof_error = safe_read(STDIN_FILENO, readbuf, COMMON_BUFSIZE);
					if (errno != EAGAIN)
						break;
					t = time(NULL);
					if (t != last_time) {
						last_time = t;
						if (--retry_EAGAIN < 0)
							break;
					}
					sched_yield();
				}
				fcntl(0, F_SETFL, flags); /* ndelay_off(0) */
				readpos = 0;
				readeof = eof_error;
				if (eof_error <= 0)
					goto reached_eof;
				retry_EAGAIN = 1;
			}
			c = readbuf[readpos];
			/* backspace? [needed for manpages] */
			/* <tab><bs> is (a) insane and */
			/* (b) harder to do correctly, so we refuse to do it */
			if (c == '\x8' && last_line_pos && p[-1] != '\t') {
				readpos++; /* eat it */
				last_line_pos--;
			/* was buggy (p could end up <= current_line)... */
				*--p = '\0';
				continue;
			}
#if ENABLE_FEATURE_LESS_RAW
			if (option_mask32 & FLAG_R) {
				if (c == '\033')
					goto discard;
				if (G.in_escape) {
					if (isdigit(c)
					 || c == '['
					 || c == ';'
					 || c == 'm'
					) {
 discard:
						G.in_escape = (c != 'm');
						readpos++;
						continue;
					}
					/* Hmm, unexpected end of "ESC [ N ; N m" sequence */
					G.in_escape = 0;
				}
			}
#endif
			{
				size_t new_last_line_pos = last_line_pos + 1;
				if (c == '\t') {
					new_last_line_pos += 7;
					new_last_line_pos &= (~7);
				}
				if ((int)new_last_line_pos > w)
					break;
				last_line_pos = new_last_line_pos;
			}
			/* ok, we will eat this char */
			readpos++;
			if (c == '\n') {
				terminated = 1;
				last_line_pos = 0;
				break;
			}
			/* NUL is substituted by '\n'! */
			if (c == '\0') c = '\n';
			*p++ = c;
			*p = '\0';
		} /* end of "read chars until we have a line" loop */
#if 0
//BUG: also triggers on this:
// { printf "\nfoo\n"; sleep 1; printf "\nbar\n"; } | less
// (resulting in lost empty line between "foo" and "bar" lines)
// the "terminated" logic needs fixing (or explaining)
		/* Corner case: linewrap with only "" wrapping to next line */
		/* Looks ugly on screen, so we do not store this empty line */
		if (!last_terminated && !current_line[0]) {
			last_terminated = 1;
			max_lineno++;
			continue;
		}
#endif
 reached_eof:
		last_terminated = terminated;
		flines = xrealloc_vector(flines, 8, max_fline);

		flines[max_fline] = (char*)xrealloc(MEMPTR(current_line), strlen(current_line) + 1 + 4) + 4;
		LINENO(flines[max_fline]) = max_lineno;
		if (terminated)
			max_lineno++;

		if (max_fline >= MAXLINES) {
			eof_error = 0; /* Pretend we saw EOF */
			break;
		}
		if (!at_end()) {
#if !ENABLE_FEATURE_LESS_REGEXP
			break;
#else
			if (wanted_match >= num_matches) { /* goto_match called us */
				fill_match_lines(old_max_fline);
				old_max_fline = max_fline;
			}
			if (wanted_match < num_matches)
				break;
#endif
		}
		if (eof_error <= 0) {
			break;
		}
		max_fline++;
		current_line = ((char*)xmalloc(w + 5)) + 4;
		p = current_line;
		last_line_pos = 0;
	} /* end of "read lines until we reach cur_fline" loop */

	if (eof_error < 0) {
		if (errno == EAGAIN) {
			eof_error = 1;
		} else {
			print_statusline(bb_msg_read_error);
		}
	}
#if ENABLE_FEATURE_LESS_FLAGS
	else if (eof_error == 0)
		num_lines = max_lineno;
#endif

	fill_match_lines(old_max_fline);
#if ENABLE_FEATURE_LESS_REGEXP
	/* prevent us from being stuck in search for a match */
	wanted_match = -1;
#endif
#undef readbuf
}

#if ENABLE_FEATURE_LESS_FLAGS
static int safe_lineno(int fline)
{
	if (fline >= max_fline)
		fline = max_fline - 1;

	/* also catches empty file (max_fline == 0) */
	if (fline < 0)
		return 0;

	return LINENO(flines[fline]) + 1;
}

/* count number of lines in file */
static void update_num_lines(void)
{
	int count, fd;
	struct stat stbuf;
	ssize_t len, i;
	char buf[4096];

	/* only do this for regular files */
	if (num_lines == REOPEN_AND_COUNT || num_lines == REOPEN_STDIN) {
		count = 0;
		fd = open("/proc/self/fd/0", O_RDONLY);
		if (fd < 0 && num_lines == REOPEN_AND_COUNT) {
			/* "filename" is valid only if REOPEN_AND_COUNT */
			fd = open(filename, O_RDONLY);
		}
		if (fd < 0) {
			/* somebody stole my file! */
			num_lines = NOT_REGULAR_FILE;
			return;
		}
		if (fstat(fd, &stbuf) != 0 || !S_ISREG(stbuf.st_mode)) {
			num_lines = NOT_REGULAR_FILE;
			goto do_close;
		}
		while ((len = safe_read(fd, buf, sizeof(buf))) > 0) {
			for (i = 0; i < len; ++i) {
				if (buf[i] == '\n' && ++count == MAXLINES)
					goto done;
			}
		}
 done:
		num_lines = count;
 do_close:
		close(fd);
	}
}

/* Print a status line if -M was specified */
static void m_status_print(void)
{
	int first, last;
	unsigned percent;

	if (less_gets_pos >= 0) /* don't touch statusline while input is done! */
		return;

	clear_line();
	printf(HIGHLIGHT"%s", filename);
	if (num_files > 1)
		printf(" (file %i of %i)", current_file, num_files);

	first = safe_lineno(cur_fline);
	last = (option_mask32 & FLAG_S)
			? MIN(first + max_displayed_line, max_lineno)
			: safe_lineno(cur_fline + max_displayed_line);
	printf(" lines %i-%i", first, last);

	update_num_lines();
	if (num_lines >= 0)
		printf("/%i", num_lines);

	if (at_end()) {
		printf(" (END)");
		if (num_files > 1 && current_file != num_files)
			printf(" - next: %s", files[current_file]);
	} else if (num_lines > 0) {
		percent = (100 * last + num_lines/2) / num_lines;
		printf(" %i%%", percent <= 100 ? percent : 100);
	}
	printf(NORMAL);
}
#endif

/* Print the status line */
static void status_print(void)
{
	const char *p;

	if (less_gets_pos >= 0) /* don't touch statusline while input is done! */
		return;

	/* Change the status if flags have been set */
#if ENABLE_FEATURE_LESS_FLAGS
	if (option_mask32 & (FLAG_M|FLAG_m)) {
		m_status_print();
		return;
	}
	/* No flags set */
#endif

	clear_line();
	if (cur_fline && !at_end()) {
		bb_putchar(':');
		return;
	}
	p = "(END)";
	if (!cur_fline)
		p = filename;
	if (num_files > 1) {
		printf(HIGHLIGHT"%s (file %i of %i)"NORMAL,
				p, current_file, num_files);
		return;
	}
	print_hilite(p);
}

static const char controls[] ALIGN1 =
	/* NUL: never encountered; TAB: not converted */
	/**/"\x01\x02\x03\x04\x05\x06\x07\x08"  "\x0a\x0b\x0c\x0d\x0e\x0f"
	"\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f"
	"\x7f\x9b"; /* DEL and infamous Meta-ESC :( */
static const char ctrlconv[] ALIGN1 =
	/* why 40 instead of 4a below? - it is a replacement for '\n'.
	 * '\n' is a former NUL - we subst it with @, not J */
	"\x40\x41\x42\x43\x44\x45\x46\x47\x48\x49\x40\x4b\x4c\x4d\x4e\x4f"
	"\x50\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5a\x5b\x5c\x5d\x5e\x5f";

static void print_lineno(const char *line)
{
	const char *fmt = "        ";
	unsigned n = n; /* for compiler */

	if (line != empty_line_marker) {
		/* Width of 7 preserves tab spacing in the text */
		fmt = "%7u ";
		n = LINENO(line) + 1;
		if (n > 9999999 && MAXLINES > 9999999) {
			n %= 10000000;
			fmt = "%07u ";
		}
	}
	printf(fmt, n);
}


#if ENABLE_FEATURE_LESS_REGEXP
static void print_found(const char *line)
{
	int match_status;
	int eflags;
	char *growline;
	regmatch_t match_structs;

	char buf[width+1];
	const char *str = line;
	char *p = buf;
	size_t n;

	while (*str) {
		n = strcspn(str, controls);
		if (n) {
			if (!str[n]) break;
			memcpy(p, str, n);
			p += n;
			str += n;
		}
		n = strspn(str, controls);
		memset(p, '.', n);
		p += n;
		str += n;
	}
	strcpy(p, str);

	/* buf[] holds quarantined version of str */

	/* Each part of the line that matches has the HIGHLIGHT
	 * and NORMAL escape sequences placed around it.
	 * NB: we regex against line, but insert text
	 * from quarantined copy (buf[]) */
	str = buf;
	growline = NULL;
	eflags = 0;
	goto start;

	while (match_status == 0) {
		char *new = xasprintf("%s%.*s"HIGHLIGHT"%.*s"NORMAL,
				growline ? growline : "",
				(int)match_structs.rm_so, str,
				(int)(match_structs.rm_eo - match_structs.rm_so),
						str + match_structs.rm_so);
		free(growline);
		growline = new;
		str += match_structs.rm_eo;
		line += match_structs.rm_eo;
		eflags = REG_NOTBOL;
 start:
		/* Most of the time doesn't find the regex, optimize for that */
		match_status = regexec(&pattern, line, 1, &match_structs, eflags);
		/* if even "" matches, treat it as "not a match" */
		if (match_structs.rm_so >= match_structs.rm_eo)
			match_status = 1;
	}

	printf("%s%s\n", growline ? growline : "", str);
	free(growline);
}
#else
void print_found(const char *line);
#endif

static void print_ascii(const char *str)
{
	char buf[width+1];
	char *p;
	size_t n;

	while (*str) {
		n = strcspn(str, controls);
		if (n) {
			if (!str[n]) break;
			printf("%.*s", (int) n, str);
			str += n;
		}
		n = strspn(str, controls);
		p = buf;
		do {
			if (*str == 0x7f)
				*p++ = '?';
			else if (*str == (char)0x9b)
			/* VT100's CSI, aka Meta-ESC. Who's inventor? */
			/* I want to know who committed this sin */
				*p++ = '{';
			else
				*p++ = ctrlconv[(unsigned char)*str];
			str++;
		} while (--n);
		*p = '\0';
		print_hilite(buf);
	}
	puts(str);
}

/* Print the buffer */
static void buffer_print(void)
{
	unsigned i;

	move_cursor(0, 0);
	for (i = 0; i <= max_displayed_line; i++) {
		printf(CLEAR_2_EOL);
		if (option_mask32 & FLAG_N)
			print_lineno(buffer[i]);
		if (pattern_valid)
			print_found(buffer[i]);
		else
			print_ascii(buffer[i]);
	}
	if ((option_mask32 & (FLAG_E|FLAG_F))
	 && eof_error <= 0
	) {
		i = option_mask32 & FLAG_F ? 0 : cur_fline;
		if (max_fline - i <= max_displayed_line)
			less_exit(EXIT_SUCCESS);
	}
	status_print();
}

static void buffer_fill_and_print(void)
{
	unsigned i;
#if ENABLE_FEATURE_LESS_TRUNCATE
	int fpos = cur_fline;

	if (option_mask32 & FLAG_S) {
		/* Go back to the beginning of this line */
		while (fpos && LINENO(flines[fpos]) == LINENO(flines[fpos-1]))
			fpos--;
	}

	i = 0;
	while (i <= max_displayed_line && fpos <= max_fline) {
		int lineno = LINENO(flines[fpos]);
		buffer[i] = flines[fpos];
		i++;
		do {
			fpos++;
		} while ((fpos <= max_fline)
		      && (option_mask32 & FLAG_S)
		      && lineno == LINENO(flines[fpos])
		);
	}
#else
	for (i = 0; i <= max_displayed_line && cur_fline + i <= max_fline; i++) {
		buffer[i] = flines[cur_fline + i];
	}
#endif
	for (; i <= max_displayed_line; i++) {
		buffer[i] = empty_line_marker;
	}
	buffer_print();
}

/* move cur_fline to a given line number, reading lines if necessary */
static void goto_lineno(int target)
{
	if (target <= 0 ) {
		cur_fline = 0;
	}
	else if (target > LINENO(flines[cur_fline])) {
 retry:
		while (LINENO(flines[cur_fline]) != target && cur_fline < max_fline)
			++cur_fline;
		/* target not reached but more input is available */
		if (LINENO(flines[cur_fline]) != target && eof_error > 0) {
			read_lines();
			goto retry;
		}
	}
	else {
		/* search backwards through already-read lines */
		while (LINENO(flines[cur_fline]) != target && cur_fline > 0)
			--cur_fline;
	}
}

static void cap_cur_fline(void)
{
	if ((option_mask32 & FLAG_S)) {
		if (cur_fline > max_fline)
			cur_fline = max_fline;
		if (LINENO(flines[cur_fline]) + max_displayed_line > max_lineno + TILDES) {
			goto_lineno(max_lineno - max_displayed_line + TILDES);
			read_lines();
		}
	}
	else {
		if (cur_fline + max_displayed_line > max_fline + TILDES)
			cur_fline = max_fline - max_displayed_line + TILDES;
		if (cur_fline < 0)
			cur_fline = 0;
	}
}

/* Move the buffer up and down in the file in order to scroll */
static void buffer_down(int nlines)
{
	if ((option_mask32 & FLAG_S))
		goto_lineno(LINENO(flines[cur_fline]) + nlines);
	else
		cur_fline += nlines;
	read_lines();
	cap_cur_fline();
	buffer_fill_and_print();
}

static void buffer_up(int nlines)
{
	if ((option_mask32 & FLAG_S)) {
		goto_lineno(LINENO(flines[cur_fline]) - nlines);
	}
	else {
		cur_fline -= nlines;
		if (cur_fline < 0)
			cur_fline = 0;
	}
	read_lines();
	buffer_fill_and_print();
}

/* display a given line where the argument can be either an index into
 * the flines array or a line number */
static void buffer_to_line(int linenum, int is_lineno)
{
	if (linenum <= 0)
		cur_fline = 0;
	else if (is_lineno)
		goto_lineno(linenum);
	else
		cur_fline = linenum;
	read_lines();
	cap_cur_fline();
	buffer_fill_and_print();
}

static void buffer_line(int linenum)
{
	buffer_to_line(linenum, FALSE);
}

static void buffer_lineno(int lineno)
{
	buffer_to_line(lineno, TRUE);
}

static void open_file_and_read_lines(void)
{
	if (filename) {
		xmove_fd(xopen(filename, O_RDONLY), STDIN_FILENO);
#if ENABLE_FEATURE_LESS_FLAGS
		num_lines = REOPEN_AND_COUNT;
#endif
	} else {
		/* "less" with no arguments in argv[] */
		/* For status line only */
		filename = xstrdup(bb_msg_standard_input);
#if ENABLE_FEATURE_LESS_FLAGS
		num_lines = REOPEN_STDIN;
#endif
	}
	readpos = 0;
	readeof = 0;
	last_line_pos = 0;
	terminated = 1;
	read_lines();
}

/* Reinitialize everything for a new file - free the memory and start over */
static void reinitialize(void)
{
	unsigned i;

	if (flines) {
		for (i = 0; i <= max_fline; i++)
			free(MEMPTR(flines[i]));
		free(flines);
		flines = NULL;
	}

	max_fline = -1;
	cur_fline = 0;
	max_lineno = 0;
	open_file_and_read_lines();
#if ENABLE_FEATURE_LESS_ASK_TERMINAL
	if (G.winsize_err)
		printf(ESC"[999;999H" ESC"[6n");
#endif
	buffer_fill_and_print();
}

static int64_t getch_nowait(void)
{
	int rd;
	int64_t key64;
	struct pollfd pfd[2];

	pfd[0].fd = STDIN_FILENO;
	pfd[0].events = POLLIN;
	pfd[1].fd = kbd_fd;
	pfd[1].events = POLLIN;
 again:
	tcsetattr(kbd_fd, TCSANOW, &term_less);
	/* NB: select/poll returns whenever read will not block. Therefore:
	 * if eof is reached, select/poll will return immediately
	 * because read will immediately return 0 bytes.
	 * Even if select/poll says that input is available, read CAN block
	 * (switch fd into O_NONBLOCK'ed mode to avoid it)
	 */
	rd = 1;
	/* Are we interested in stdin? */
	if (at_end()) {
		if (eof_error > 0) /* did NOT reach eof yet */
			rd = 0; /* yes, we are interested in stdin */
	}
	/* Position cursor if line input is done */
	if (less_gets_pos >= 0)
		move_cursor(max_displayed_line + 2, less_gets_pos + 1);
	fflush_all();

	if (kbd_input[0] == 0) { /* if nothing is buffered */
#if ENABLE_FEATURE_LESS_WINCH
		while (1) {
			int r;
			/* NB: SIGWINCH interrupts poll() */
			r = poll(pfd + rd, 2 - rd, -1);
			if (/*r < 0 && errno == EINTR &&*/ winch_counter)
				return '\\'; /* anything which has no defined function */
			if (r) break;
		}
#else
		safe_poll(pfd + rd, 2 - rd, -1);
#endif
	}

	/* We have kbd_fd in O_NONBLOCK mode, read inside read_key()
	 * would not block even if there is no input available */
	key64 = read_key(kbd_fd, kbd_input, /*timeout off:*/ -2);
	if ((int)key64 == -1) {
		if (errno == EAGAIN) {
			/* No keyboard input available. Since poll() did return,
			 * we should have input on stdin */
			read_lines();
			buffer_fill_and_print();
			goto again;
		}
		/* EOF/error (ssh session got killed etc) */
		less_exit(EXIT_SUCCESS);
	}
	set_tty_cooked();
	return key64;
}

/* Grab a character from input without requiring the return key.
 * May return KEYCODE_xxx values.
 * Note that this function works best with raw input. */
static int64_t less_getch(int pos)
{
	int64_t key64;
	int key;

 again:
	less_gets_pos = pos;
	key = key64 = getch_nowait();
	less_gets_pos = -1;

	/* Discard Ctrl-something chars.
	 * (checking only lower 32 bits is a size optimization:
	 * upper 32 bits are used only by KEYCODE_CURSOR_POS)
	 */
	if (key >= 0 && key < ' ' && key != 0x0d && key != 8)
		goto again;

	return key64;
}

static char* less_gets(int sz)
{
	int c;
	unsigned i = 0;
	char *result = xzalloc(1);

	while (1) {
		c = '\0';
		less_gets_pos = sz + i;
		c = getch_nowait();
		if (c == 0x0d) {
			result[i] = '\0';
			less_gets_pos = -1;
			return result;
		}
		if (c == 0x7f)
			c = 8;
		if (c == 8 && i) {
			printf("\x8 \x8");
			i--;
		}
		if (c < ' ') /* filters out KEYCODE_xxx too (<0) */
			continue;
		if (i >= width - sz - 1)
			continue; /* len limit */
		bb_putchar(c);
		result[i++] = c;
		result = xrealloc(result, i+1);
	}
}

static void examine_file(void)
{
	char *new_fname;

	print_statusline("Examine: ");
	new_fname = less_gets(sizeof("Examine: ") - 1);
	if (!new_fname[0]) {
		status_print();
 err:
		free(new_fname);
		return;
	}
	if (access(new_fname, R_OK) != 0) {
		print_statusline("Cannot read this file");
		goto err;
	}
	free(filename);
	filename = new_fname;
	/* files start by = argv. why we assume that argv is infinitely long??
	files[num_files] = filename;
	current_file = num_files + 1;
	num_files++; */
	files[0] = filename;
	num_files = current_file = 1;
	reinitialize();
}

/* This function changes the file currently being paged. direction can be one of the following:
 * -1: go back one file
 *  0: go to the first file
 *  1: go forward one file */
static void change_file(int direction)
{
	if (current_file != ((direction > 0) ? num_files : 1)) {
		current_file = direction ? current_file + direction : 1;
		free(filename);
		filename = xstrdup(files[current_file - 1]);
		reinitialize();
	} else {
		print_statusline(direction > 0 ? "No next file" : "No previous file");
	}
}

static void remove_current_file(void)
{
	unsigned i;

	if (num_files < 2)
		return;

	if (current_file != 1) {
		change_file(-1);
		for (i = 3; i <= num_files; i++)
			files[i - 2] = files[i - 1];
		num_files--;
	} else {
		change_file(1);
		for (i = 2; i <= num_files; i++)
			files[i - 2] = files[i - 1];
		num_files--;
		current_file--;
	}
}

static void colon_process(void)
{
	int keypress;

	/* Clear the current line and print a prompt */
	print_statusline(" :");

	keypress = less_getch(2);
	switch (keypress) {
	case 'd':
		remove_current_file();
		break;
	case 'e':
		examine_file();
		break;
#if ENABLE_FEATURE_LESS_FLAGS
	case 'f':
		m_status_print();
		break;
#endif
	case 'n':
		change_file(1);
		break;
	case 'p':
		change_file(-1);
		break;
	case 'q':
		less_exit(EXIT_SUCCESS);
		break;
	case 'x':
		change_file(0);
		break;
	}
}

#if ENABLE_FEATURE_LESS_REGEXP
static void normalize_match_pos(int match)
{
	if (match >= num_matches)
		match = num_matches - 1;
	if (match < 0)
		match = 0;
	match_pos = match;
}

static void goto_match(int match)
{
	if (!pattern_valid)
		return;
	if (match < 0)
		match = 0;
	/* Try to find next match if eof isn't reached yet */
	if (match >= num_matches && eof_error > 0) {
		wanted_match = match; /* "I want to read until I see N'th match" */
		read_lines();
	}
	if (num_matches) {
		normalize_match_pos(match);
		buffer_line(match_lines[match_pos]);
	} else {
		print_statusline("No matches found");
	}
}

static void fill_match_lines(unsigned pos)
{
	if (!pattern_valid)
		return;
	/* Run the regex on each line of the current file */
	while (pos <= max_fline) {
		/* If this line matches */
		if (regexec(&pattern, flines[pos], 0, NULL, 0) == 0
		/* and we didn't match it last time */
		 && !(num_matches && match_lines[num_matches-1] == pos)
		) {
			match_lines = xrealloc_vector(match_lines, 4, num_matches);
			match_lines[num_matches++] = pos;
		}
		pos++;
	}
}

static void regex_process(void)
{
	char *uncomp_regex, *err;

	/* Reset variables */
	free(match_lines);
	match_lines = NULL;
	match_pos = 0;
	num_matches = 0;
	if (pattern_valid) {
		regfree(&pattern);
		pattern_valid = 0;
	}

	/* Get the uncompiled regular expression from the user */
	clear_line();
	bb_putchar((option_mask32 & LESS_STATE_MATCH_BACKWARDS) ? '?' : '/');
	uncomp_regex = less_gets(1);
	if (!uncomp_regex[0]) {
		free(uncomp_regex);
		buffer_print();
		return;
	}

	/* Compile the regex and check for errors */
	err = regcomp_or_errmsg(&pattern, uncomp_regex,
				(option_mask32 & FLAG_I) ? REG_ICASE : 0);
	free(uncomp_regex);
	if (err) {
		print_statusline(err);
		free(err);
		return;
	}

	pattern_valid = 1;
	match_pos = 0;
	fill_match_lines(0);
	while (match_pos < num_matches) {
		if ((int)match_lines[match_pos] > cur_fline)
			break;
		match_pos++;
	}
	if (option_mask32 & LESS_STATE_MATCH_BACKWARDS)
		match_pos--;

	/* It's possible that no matches are found yet.
	 * goto_match() will read input looking for match,
	 * if needed */
	goto_match(match_pos);
}
#endif

static void number_process(int first_digit)
{
	unsigned i;
	int num;
	int keypress;
	char num_input[sizeof(int)*4]; /* more than enough */

	num_input[0] = first_digit;

	/* Clear the current line, print a prompt, and then print the digit */
	clear_line();
	printf(":%c", first_digit);

	/* Receive input until a letter is given */
	i = 1;
	while (i < sizeof(num_input)-1) {
		keypress = less_getch(i + 1);
		if ((unsigned)keypress > 255 || !isdigit(keypress))
			break;
		num_input[i] = keypress;
		bb_putchar(keypress);
		i++;
	}

	num_input[i] = '\0';
	num = bb_strtou(num_input, NULL, 10);
	/* on format error, num == -1 */
	if (num < 1 || num > MAXLINES) {
		buffer_print();
		return;
	}

	/* We now know the number and the letter entered, so we process them */
	switch (keypress) {
	case KEYCODE_DOWN: case 'z': case 'd': case 'e': case ' ': case '\015':
		buffer_down(num);
		break;
	case KEYCODE_UP: case 'b': case 'w': case 'y': case 'u':
		buffer_up(num);
		break;
	case 'g': case '<': case 'G': case '>':
		buffer_lineno(num - 1);
		break;
	case 'p': case '%':
#if ENABLE_FEATURE_LESS_FLAGS
		update_num_lines();
		num = num * (num_lines > 0 ? num_lines : max_lineno) / 100;
#else
		num = num * max_lineno / 100;
#endif
		buffer_lineno(num);
		break;
#if ENABLE_FEATURE_LESS_REGEXP
	case 'n':
		goto_match(match_pos + num);
		break;
	case '/':
		option_mask32 &= ~LESS_STATE_MATCH_BACKWARDS;
		regex_process();
		break;
	case '?':
		option_mask32 |= LESS_STATE_MATCH_BACKWARDS;
		regex_process();
		break;
#endif
	}
}

#if ENABLE_FEATURE_LESS_DASHCMD
static void flag_change(void)
{
	int keypress;

	clear_line();
	bb_putchar('-');
	keypress = less_getch(1);

	switch (keypress) {
	case 'M':
		option_mask32 ^= FLAG_M;
		break;
	case 'm':
		option_mask32 ^= FLAG_m;
		break;
	case 'E':
		option_mask32 ^= FLAG_E;
		break;
	case '~':
		option_mask32 ^= FLAG_TILDE;
		break;
#if ENABLE_FEATURE_LESS_TRUNCATE
	case 'S':
		option_mask32 ^= FLAG_S;
		buffer_fill_and_print();
		break;
#endif
#if ENABLE_FEATURE_LESS_LINENUMS
	case 'N':
		option_mask32 ^= FLAG_N;
		re_wrap();
		buffer_fill_and_print();
		break;
#endif
	}
}

#ifdef BLOAT
static void show_flag_status(void)
{
	int keypress;
	int flag_val;

	clear_line();
	bb_putchar('_');
	keypress = less_getch(1);

	switch (keypress) {
	case 'M':
		flag_val = option_mask32 & FLAG_M;
		break;
	case 'm':
		flag_val = option_mask32 & FLAG_m;
		break;
	case '~':
		flag_val = option_mask32 & FLAG_TILDE;
		break;
	case 'N':
		flag_val = option_mask32 & FLAG_N;
		break;
	case 'E':
		flag_val = option_mask32 & FLAG_E;
		break;
	default:
		flag_val = 0;
		break;
	}

	clear_line();
	printf(HIGHLIGHT"The status of the flag is: %u"NORMAL, flag_val != 0);
}
#endif

#endif /* ENABLE_FEATURE_LESS_DASHCMD */

static void save_input_to_file(void)
{
	const char *msg = "";
	char *current_line;
	unsigned i;
	FILE *fp;

	print_statusline("Log file: ");
	current_line = less_gets(sizeof("Log file: ")-1);
	if (current_line[0]) {
		fp = fopen_for_write(current_line);
		if (!fp) {
			msg = "Error opening log file";
			goto ret;
		}
		for (i = 0; i <= max_fline; i++)
			fprintf(fp, "%s\n", flines[i]);
		fclose(fp);
		msg = "Done";
	}
 ret:
	print_statusline(msg);
	free(current_line);
}

#if ENABLE_FEATURE_LESS_MARKS
static void add_mark(void)
{
	int letter;

	print_statusline("Mark: ");
	letter = less_getch(sizeof("Mark: ") - 1);

	if (isalpha(letter)) {
		/* If we exceed 15 marks, start overwriting previous ones */
		if (num_marks == 14)
			num_marks = 0;

		mark_lines[num_marks][0] = letter;
		mark_lines[num_marks][1] = cur_fline;
		num_marks++;
	} else {
		print_statusline("Invalid mark letter");
	}
}

static void goto_mark(void)
{
	int letter;
	int i;

	print_statusline("Go to mark: ");
	letter = less_getch(sizeof("Go to mark: ") - 1);
	clear_line();

	if (isalpha(letter)) {
		for (i = 0; i <= num_marks; i++)
			if (letter == mark_lines[i][0]) {
				buffer_line(mark_lines[i][1]);
				break;
			}
		if (num_marks == 14 && letter != mark_lines[14][0])
			print_statusline("Mark not set");
	} else
		print_statusline("Invalid mark letter");
}
#endif

#if ENABLE_FEATURE_LESS_BRACKETS
static char opp_bracket(char bracket)
{
	switch (bracket) {
		case '{': case '[': /* '}' == '{' + 2. Same for '[' */
			bracket++;
		case '(':           /* ')' == '(' + 1 */
			bracket++;
			break;
		case '}': case ']':
			bracket--;
		case ')':
			bracket--;
			break;
	};
	return bracket;
}

static void match_right_bracket(char bracket)
{
	unsigned i = cur_fline;

	if (i >= max_fline
	 || strchr(flines[i], bracket) == NULL
	) {
		print_statusline("No bracket in top line");
		return;
	}

	bracket = opp_bracket(bracket);
	for (; i < max_fline; i++) {
		if (strchr(flines[i], bracket) != NULL) {
			/*
			 * Line with matched right bracket becomes
			 * last visible line
			 */
			buffer_line(i - max_displayed_line);
			return;
		}
	}
	print_statusline("No matching bracket found");
}

static void match_left_bracket(char bracket)
{
	int i = cur_fline + max_displayed_line;

	if (i >= max_fline
	 || strchr(flines[i], bracket) == NULL
	) {
		print_statusline("No bracket in bottom line");
		return;
	}

	bracket = opp_bracket(bracket);
	for (; i >= 0; i--) {
		if (strchr(flines[i], bracket) != NULL) {
			/*
			 * Line with matched left bracket becomes
			 * first visible line
			 */
			buffer_line(i);
			return;
		}
	}
	print_statusline("No matching bracket found");
}
#endif  /* FEATURE_LESS_BRACKETS */

static void keypress_process(int keypress)
{
	switch (keypress) {
	case KEYCODE_DOWN: case 'e': case 'j': case 0x0d:
		buffer_down(1);
		break;
	case KEYCODE_UP: case 'y': case 'k':
		buffer_up(1);
		break;
	case KEYCODE_PAGEDOWN: case ' ': case 'z': case 'f':
		buffer_down(max_displayed_line + 1);
		break;
	case KEYCODE_PAGEUP: case 'w': case 'b':
		buffer_up(max_displayed_line + 1);
		break;
	case 'd':
		buffer_down((max_displayed_line + 1) / 2);
		break;
	case 'u':
		buffer_up((max_displayed_line + 1) / 2);
		break;
	case KEYCODE_HOME: case 'g': case 'p': case '<': case '%':
		buffer_line(0);
		break;
	case KEYCODE_END: case 'G': case '>':
		cur_fline = MAXLINES;
		read_lines();
		buffer_line(cur_fline);
		break;
	case 'q': case 'Q':
		less_exit(EXIT_SUCCESS);
		break;
#if ENABLE_FEATURE_LESS_MARKS
	case 'm':
		add_mark();
		buffer_print();
		break;
	case '\'':
		goto_mark();
		buffer_print();
		break;
#endif
	case 'r': case 'R':
		/* TODO: (1) also bind ^R, ^L to this?
		 * (2) re-measure window size?
		 */
		buffer_print();
		break;
	/*case 'R':
		full_repaint();
		break;*/
	case 's':
		save_input_to_file();
		break;
	case 'E':
		examine_file();
		break;
#if ENABLE_FEATURE_LESS_FLAGS
	case '=':
		m_status_print();
		break;
#endif
#if ENABLE_FEATURE_LESS_REGEXP
	case '/':
		option_mask32 &= ~LESS_STATE_MATCH_BACKWARDS;
		regex_process();
		break;
	case 'n':
		goto_match(match_pos + 1);
		break;
	case 'N':
		goto_match(match_pos - 1);
		break;
	case '?':
		option_mask32 |= LESS_STATE_MATCH_BACKWARDS;
		regex_process();
		break;
#endif
#if ENABLE_FEATURE_LESS_DASHCMD
	case '-':
		flag_change();
		buffer_print();
		break;
#ifdef BLOAT
	case '_':
		show_flag_status();
		break;
#endif
#endif
#if ENABLE_FEATURE_LESS_BRACKETS
	case '{': case '(': case '[':
		match_right_bracket(keypress);
		break;
	case '}': case ')': case ']':
		match_left_bracket(keypress);
		break;
#endif
	case ':':
		colon_process();
		break;
	}

	if (isdigit(keypress))
		number_process(keypress);
}

static void sig_catcher(int sig)
{
	less_exit(- sig);
}

#if ENABLE_FEATURE_LESS_WINCH
static void sigwinch_handler(int sig UNUSED_PARAM)
{
	winch_counter++;
}
#endif

int less_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int less_main(int argc, char **argv)
{
	char *tty_name;
	int tty_fd;

	INIT_G();

	/* TODO: -x: do not interpret backspace, -xx: tab also
	 * -xxx: newline also
	 * -w N: assume width N (-xxx -w 32: hex viewer of sorts)
	 * -s: condense many empty lines to one
	 *     (used by some setups for manpage display)
	 */
	getopt32(argv, "EMmN~IF"
		IF_FEATURE_LESS_TRUNCATE("S")
		IF_FEATURE_LESS_RAW("R")
		/*ignored:*/"s"
	);
	argv += optind;
	num_files = argc - optind;
	files = argv;

	/* Tools typically pass LESS="FRSXMK".
	 * The options we don't understand are ignored. */
	if (ENABLE_FEATURE_LESS_ENV) {
		char *c = getenv("LESS");
		if (c) while (*c) switch (*c++) {
		case 'F':
			option_mask32 |= FLAG_F;
			break;
		case 'M':
			option_mask32 |= FLAG_M;
			break;
		case 'R':
			option_mask32 |= FLAG_R;
			break;
		case 'S':
			option_mask32 |= FLAG_S;
			break;
		default:
			break;
		}
	}

	/* Another popular pager, most, detects when stdout
	 * is not a tty and turns into cat. This makes sense. */
	if (!isatty(STDOUT_FILENO))
		return bb_cat(argv);

	if (!num_files) {
		if (isatty(STDIN_FILENO)) {
			/* Just "less"? No args and no redirection? */
			bb_show_usage();
		}
	} else {
		filename = xstrdup(files[0]);
	}

	if (option_mask32 & FLAG_TILDE)
		empty_line_marker = "";

	/* Some versions of less can survive w/o controlling tty,
	 * try to do the same. This also allows to specify an alternative
	 * tty via "less 1<>TTY".
	 *
	 * We prefer not to use STDOUT_FILENO directly,
	 * since we want to set this fd to non-blocking mode,
	 * and not interfere with other processes which share stdout with us.
	 */
	tty_name = xmalloc_ttyname(STDOUT_FILENO);
	if (tty_name) {
		tty_fd = open(tty_name, O_RDONLY);
		free(tty_name);
		if (tty_fd < 0)
			goto try_ctty;
	} else {
		/* Try controlling tty */
 try_ctty:
		tty_fd = open(CURRENT_TTY, O_RDONLY);
		if (tty_fd < 0) {
			/* If all else fails, less 481 uses stdout. Mimic that */
			tty_fd = STDOUT_FILENO;
		}
	}
	G.kbd_fd_orig_flags = ndelay_on(tty_fd);
	kbd_fd = tty_fd; /* save in a global */

	get_termios_and_make_raw(tty_fd, &term_less, &term_orig, TERMIOS_RAW_CRNL_INPUT);

	IF_FEATURE_LESS_ASK_TERMINAL(G.winsize_err =) get_terminal_width_height(tty_fd, &width, &max_displayed_line);
	/* 20: two tabstops + 4 */
	if (width < 20 || max_displayed_line < 3)
		return bb_cat(argv);
	max_displayed_line -= 2;

	/* We want to restore term_orig on exit */
	bb_signals(BB_FATAL_SIGS, sig_catcher);
#if ENABLE_FEATURE_LESS_WINCH
	signal(SIGWINCH, sigwinch_handler);
#endif

	buffer = xmalloc((max_displayed_line+1) * sizeof(char *));
	reinitialize();
	while (1) {
		int64_t keypress;

#if ENABLE_FEATURE_LESS_WINCH
		while (WINCH_COUNTER) {
 again:
			winch_counter--;
			IF_FEATURE_LESS_ASK_TERMINAL(G.winsize_err =) get_terminal_width_height(kbd_fd, &width, &max_displayed_line);
 IF_FEATURE_LESS_ASK_TERMINAL(got_size:)
			/* 20: two tabstops + 4 */
			if (width < 20)
				width = 20;
			if (max_displayed_line < 3)
				max_displayed_line = 3;
			max_displayed_line -= 2;
			free(buffer);
			buffer = xmalloc((max_displayed_line+1) * sizeof(char *));
			/* Avoid re-wrap and/or redraw if we already know
			 * we need to do it again. These ops are expensive */
			if (WINCH_COUNTER)
				goto again;
			re_wrap();
			if (WINCH_COUNTER)
				goto again;
			buffer_fill_and_print();
			/* This took some time. Loop back and check,
			 * were there another SIGWINCH? */
		}
		keypress = less_getch(-1); /* -1: do not position cursor */
# if ENABLE_FEATURE_LESS_ASK_TERMINAL
		if ((int32_t)keypress == KEYCODE_CURSOR_POS) {
			uint32_t rc = (keypress >> 32);
			width = (rc & 0x7fff);
			max_displayed_line = ((rc >> 16) & 0x7fff);
			goto got_size;
		}
# endif
#else
		keypress = less_getch(-1); /* -1: do not position cursor */
#endif
		keypress_process(keypress);
	}
}

/*
Help text of less version 418 is below.
If you are implementing something, keeping
key and/or command line switch compatibility is a good idea:


                   SUMMARY OF LESS COMMANDS

      Commands marked with * may be preceded by a number, N.
      Notes in parentheses indicate the behavior if N is given.
  h  H                 Display this help.
  q  :q  Q  :Q  ZZ     Exit.
 ---------------------------------------------------------------------------
                           MOVING
  e  ^E  j  ^N  CR  *  Forward  one line   (or N lines).
  y  ^Y  k  ^K  ^P  *  Backward one line   (or N lines).
  f  ^F  ^V  SPACE  *  Forward  one window (or N lines).
  b  ^B  ESC-v      *  Backward one window (or N lines).
  z                 *  Forward  one window (and set window to N).
  w                 *  Backward one window (and set window to N).
  ESC-SPACE         *  Forward  one window, but don't stop at end-of-file.
  d  ^D             *  Forward  one half-window (and set half-window to N).
  u  ^U             *  Backward one half-window (and set half-window to N).
  ESC-)  RightArrow *  Left  one half screen width (or N positions).
  ESC-(  LeftArrow  *  Right one half screen width (or N positions).
  F                    Forward forever; like "tail -f".
  r  ^R  ^L            Repaint screen.
  R                    Repaint screen, discarding buffered input.
        ---------------------------------------------------
        Default "window" is the screen height.
        Default "half-window" is half of the screen height.
 ---------------------------------------------------------------------------
                          SEARCHING
  /pattern          *  Search forward for (N-th) matching line.
  ?pattern          *  Search backward for (N-th) matching line.
  n                 *  Repeat previous search (for N-th occurrence).
  N                 *  Repeat previous search in reverse direction.
  ESC-n             *  Repeat previous search, spanning files.
  ESC-N             *  Repeat previous search, reverse dir. & spanning files.
  ESC-u                Undo (toggle) search highlighting.
        ---------------------------------------------------
        Search patterns may be modified by one or more of:
        ^N or !  Search for NON-matching lines.
        ^E or *  Search multiple files (pass thru END OF FILE).
        ^F or @  Start search at FIRST file (for /) or last file (for ?).
        ^K       Highlight matches, but don't move (KEEP position).
        ^R       Don't use REGULAR EXPRESSIONS.
 ---------------------------------------------------------------------------
                           JUMPING
  g  <  ESC-<       *  Go to first line in file (or line N).
  G  >  ESC->       *  Go to last line in file (or line N).
  p  %              *  Go to beginning of file (or N percent into file).
  t                 *  Go to the (N-th) next tag.
  T                 *  Go to the (N-th) previous tag.
  {  (  [           *  Find close bracket } ) ].
  }  )  ]           *  Find open bracket { ( [.
  ESC-^F <c1> <c2>  *  Find close bracket <c2>.
  ESC-^B <c1> <c2>  *  Find open bracket <c1>
        ---------------------------------------------------
        Each "find close bracket" command goes forward to the close bracket
          matching the (N-th) open bracket in the top line.
        Each "find open bracket" command goes backward to the open bracket
          matching the (N-th) close bracket in the bottom line.
  m<letter>            Mark the current position with <letter>.
  '<letter>            Go to a previously marked position.
  ''                   Go to the previous position.
  ^X^X                 Same as '.
        ---------------------------------------------------
        A mark is any upper-case or lower-case letter.
        Certain marks are predefined:
             ^  means  beginning of the file
             $  means  end of the file
 ---------------------------------------------------------------------------
                        CHANGING FILES
  :e [file]            Examine a new file.
  ^X^V                 Same as :e.
  :n                *  Examine the (N-th) next file from the command line.
  :p                *  Examine the (N-th) previous file from the command line.
  :x                *  Examine the first (or N-th) file from the command line.
  :d                   Delete the current file from the command line list.
  =  ^G  :f            Print current file name.
 ---------------------------------------------------------------------------
                    MISCELLANEOUS COMMANDS
  -<flag>              Toggle a command line option [see OPTIONS below].
  --<name>             Toggle a command line option, by name.
  _<flag>              Display the setting of a command line option.
  __<name>             Display the setting of an option, by name.
  +cmd                 Execute the less cmd each time a new file is examined.
  !command             Execute the shell command with $SHELL.
  |Xcommand            Pipe file between current pos & mark X to shell command.
  v                    Edit the current file with $VISUAL or $EDITOR.
  V                    Print version number of "less".
 ---------------------------------------------------------------------------
                           OPTIONS
        Most options may be changed either on the command line,
        or from within less by using the - or -- command.
        Options may be given in one of two forms: either a single
        character preceded by a -, or a name preceded by --.
  -?  ........  --help
                  Display help (from command line).
  -a  ........  --search-skip-screen
                  Forward search skips current screen.
  -b [N]  ....  --buffers=[N]
                  Number of buffers.
  -B  ........  --auto-buffers
                  Don't automatically allocate buffers for pipes.
  -c  ........  --clear-screen
                  Repaint by clearing rather than scrolling.
  -d  ........  --dumb
                  Dumb terminal.
  -D [xn.n]  .  --color=xn.n
                  Set screen colors. (MS-DOS only)
  -e  -E  ....  --quit-at-eof  --QUIT-AT-EOF
                  Quit at end of file.
  -f  ........  --force
                  Force open non-regular files.
  -F  ........  --quit-if-one-screen
                  Quit if entire file fits on first screen.
  -g  ........  --hilite-search
                  Highlight only last match for searches.
  -G  ........  --HILITE-SEARCH
                  Don't highlight any matches for searches.
  -h [N]  ....  --max-back-scroll=[N]
                  Backward scroll limit.
  -i  ........  --ignore-case
                  Ignore case in searches that do not contain uppercase.
  -I  ........  --IGNORE-CASE
                  Ignore case in all searches.
  -j [N]  ....  --jump-target=[N]
                  Screen position of target lines.
  -J  ........  --status-column
                  Display a status column at left edge of screen.
  -k [file]  .  --lesskey-file=[file]
                  Use a lesskey file.
  -L  ........  --no-lessopen
                  Ignore the LESSOPEN environment variable.
  -m  -M  ....  --long-prompt  --LONG-PROMPT
                  Set prompt style.
  -n  -N  ....  --line-numbers  --LINE-NUMBERS
                  Don't use line numbers.
  -o [file]  .  --log-file=[file]
                  Copy to log file (standard input only).
  -O [file]  .  --LOG-FILE=[file]
                  Copy to log file (unconditionally overwrite).
  -p [pattern]  --pattern=[pattern]
                  Start at pattern (from command line).
  -P [prompt]   --prompt=[prompt]
                  Define new prompt.
  -q  -Q  ....  --quiet  --QUIET  --silent --SILENT
                  Quiet the terminal bell.
  -r  -R  ....  --raw-control-chars  --RAW-CONTROL-CHARS
                  Output "raw" control characters.
  -s  ........  --squeeze-blank-lines
                  Squeeze multiple blank lines.
  -S  ........  --chop-long-lines
                  Chop long lines.
  -t [tag]  ..  --tag=[tag]
                  Find a tag.
  -T [tagsfile] --tag-file=[tagsfile]
                  Use an alternate tags file.
  -u  -U  ....  --underline-special  --UNDERLINE-SPECIAL
                  Change handling of backspaces.
  -V  ........  --version
                  Display the version number of "less".
  -w  ........  --hilite-unread
                  Highlight first new line after forward-screen.
  -W  ........  --HILITE-UNREAD
                  Highlight first new line after any forward movement.
  -x [N[,...]]  --tabs=[N[,...]]
                  Set tab stops.
  -X  ........  --no-init
                  Don't use termcap init/deinit strings.
                --no-keypad
                  Don't use termcap keypad init/deinit strings.
  -y [N]  ....  --max-forw-scroll=[N]
                  Forward scroll limit.
  -z [N]  ....  --window=[N]
                  Set size of window.
  -" [c[c]]  .  --quotes=[c[c]]
                  Set shell quote characters.
  -~  ........  --tilde
                  Don't display tildes after end of file.
  -# [N]  ....  --shift=[N]
                  Horizontal scroll amount (0 = one half screen width)

 ---------------------------------------------------------------------------
                          LINE EDITING
        These keys can be used to edit text being entered
        on the "command line" at the bottom of the screen.
 RightArrow                       ESC-l     Move cursor right one character.
 LeftArrow                        ESC-h     Move cursor left one character.
 CNTL-RightArrow  ESC-RightArrow  ESC-w     Move cursor right one word.
 CNTL-LeftArrow   ESC-LeftArrow   ESC-b     Move cursor left one word.
 HOME                             ESC-0     Move cursor to start of line.
 END                              ESC-$     Move cursor to end of line.
 BACKSPACE                                  Delete char to left of cursor.
 DELETE                           ESC-x     Delete char under cursor.
 CNTL-BACKSPACE   ESC-BACKSPACE             Delete word to left of cursor.
 CNTL-DELETE      ESC-DELETE      ESC-X     Delete word under cursor.
 CNTL-U           ESC (MS-DOS only)         Delete entire line.
 UpArrow                          ESC-k     Retrieve previous command line.
 DownArrow                        ESC-j     Retrieve next command line.
 TAB                                        Complete filename & cycle.
 SHIFT-TAB                        ESC-TAB   Complete filename & reverse cycle.
 CNTL-L                                     Complete filename, list all.
*/
