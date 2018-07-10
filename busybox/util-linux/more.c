/* vi: set sw=4 ts=4: */
/*
 * Mini more implementation for busybox
 *
 * Copyright (C) 1995, 1996 by Bruce Perens <bruce@pixar.com>.
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Latest version blended together by Erik Andersen <andersen@codepoet.org>,
 * based on the original more implementation by Bruce, and code from the
 * Debian boot-floppies team.
 *
 * Termios corrects by Vladimir Oleynik <dzo@simtreas.ru>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config MORE
//config:	bool "more (6.7 kb)"
//config:	default y
//config:	help
//config:	more is a simple utility which allows you to read text one screen
//config:	sized page at a time. If you want to read text that is larger than
//config:	the screen, and you are using anything faster than a 300 baud modem,
//config:	you will probably find this utility very helpful. If you don't have
//config:	any need to reading text files, you can leave this disabled.

//applet:IF_MORE(APPLET(more, BB_DIR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_MORE) += more.o

//usage:#define more_trivial_usage
//usage:       "[FILE]..."
//usage:#define more_full_usage "\n\n"
//usage:       "View FILE (or stdin) one screenful at a time"
//usage:
//usage:#define more_example_usage
//usage:       "$ dmesg | more\n"

#include "libbb.h"
#include "common_bufsiz.h"

struct globals {
	int tty_fileno;
	unsigned terminal_width;
	unsigned terminal_height;
	struct termios initial_settings;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { setup_common_bufsiz(); } while (0)

static void get_wh(void)
{
	/* never returns w, h <= 1 */
	get_terminal_width_height(G.tty_fileno, &G.terminal_width, &G.terminal_height);
	G.terminal_height -= 1;
}

static void tcsetattr_tty_TCSANOW(struct termios *settings)
{
	tcsetattr(G.tty_fileno, TCSANOW, settings);
}

static void gotsig(int sig UNUSED_PARAM)
{
	/* bb_putchar_stderr doesn't use stdio buffering,
	 * therefore it is safe in signal handler */
	bb_putchar_stderr('\n');
	tcsetattr_tty_TCSANOW(&G.initial_settings);
	_exit(EXIT_FAILURE);
}

#define CONVERTED_TAB_SIZE 8

int more_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int more_main(int argc UNUSED_PARAM, char **argv)
{
	int c = c; /* for compiler */
	int input = 0;
	int spaces = 0;
	int please_display_more_prompt;
	FILE *tty;

	INIT_G();

	/* Parse options */
	/* Accepted but ignored: */
	/* -d	Display help instead of ringing bell */
	/* -f	Count logical lines (IOW: long lines are not folded) */
	/* -l	Do not pause after any line containing a ^L (form feed) */
	/* -s	Squeeze blank lines into one */
	/* -u	Suppress underlining */
	getopt32(argv, "dflsu");
	argv += optind;

	/* Another popular pager, most, detects when stdout
	 * is not a tty and turns into cat. This makes sense. */
	if (!isatty(STDOUT_FILENO))
		return bb_cat(argv);
	tty = fopen_for_read(CURRENT_TTY);
	if (!tty)
		return bb_cat(argv);

	G.tty_fileno = fileno(tty);

	/* Turn on unbuffered input; turn off echoing */
	set_termios_to_raw(G.tty_fileno, &G.initial_settings, 0);
	bb_signals(BB_FATAL_SIGS, gotsig);

	do {
		struct stat st;
		FILE *file;
		int len;
		int lines;

		file = stdin;
		if (*argv) {
			file = fopen_or_warn(*argv, "r");
			if (!file)
				continue;
		}
		st.st_size = 0;
		fstat(fileno(file), &st);

		get_wh();

		please_display_more_prompt = 0;
		len = 0;
		lines = 0;
		for (;;) {
			int wrap;

			if (spaces)
				spaces--;
			else {
				c = getc(file);
				if (c == EOF) break;
			}
 loop_top:
			if (input != 'r' && please_display_more_prompt) {
				len = printf("--More-- ");
				if (st.st_size != 0) {
					uoff_t d = (uoff_t)st.st_size / 100;
					if (d == 0)
						d = 1;
					len += printf("(%u%% of %"OFF_FMT"u bytes)",
						(int) ((uoff_t)ftello(file) / d),
						st.st_size);
				}

				/*
				 * We've just displayed the "--More--" prompt, so now we need
				 * to get input from the user.
				 */
				for (;;) {
					fflush_all();
					input = getc(tty);
					input = tolower(input);
					/* Erase the last message */
					printf("\r%*s\r", len, "");

					if (input == 'q')
						goto end;
					/* Due to various multibyte escape
					 * sequences, it's not ok to accept
					 * any input as a command to scroll
					 * the screen. We only allow known
					 * commands, else we show help msg. */
					if (input == ' ' || input == '\n' || input == 'r')
						break;
					len = printf("(Enter:next line Space:next page Q:quit R:show the rest)");
				}
				len = 0;
				lines = 0;
				please_display_more_prompt = 0;

				/* The user may have resized the terminal.
				 * Re-read the dimensions. */
				get_wh();
			}

			/* Crudely convert tabs into spaces, which are
			 * a bajillion times easier to deal with. */
			if (c == '\t') {
				spaces = ((unsigned)~len) % CONVERTED_TAB_SIZE;
				c = ' ';
			}

			/*
			 * There are two input streams to worry about here:
			 *
			 * c    : the character we are reading from the file being "mored"
			 * input: a character received from the keyboard
			 *
			 * If we hit a newline in the _file_ stream, we want to test and
			 * see if any characters have been hit in the _input_ stream. This
			 * allows the user to quit while in the middle of a file.
			 */
			wrap = (++len > G.terminal_width);
			if (c == '\n' || wrap) {
				/* Then outputting this character
				 * will move us to a new line. */
				if (++lines >= G.terminal_height || input == '\n')
					please_display_more_prompt = 1;
				len = 0;
			}
			if (c != '\n' && wrap) {
				/* Then outputting this will also put a character on
				 * the beginning of that new line. Thus we first want to
				 * display the prompt (if any), so we skip the putchar()
				 * and go back to the top of the loop, without reading
				 * a new character. */
				goto loop_top;
			}
			/* My small mind cannot fathom backspaces and UTF-8 */
			putchar(c);
			die_if_ferror_stdout(); /* if tty was destroyed (closed xterm, etc) */
		}
		fclose(file);
		fflush_all();
	} while (*argv && *++argv);
 end:
	tcsetattr_tty_TCSANOW(&G.initial_settings);
	return 0;
}
