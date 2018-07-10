/* vi: set sw=4 ts=4: */
/*
 * shuf: Write a random permutation of the input lines to standard output.
 *
 * Copyright (C) 2014 by Bartosz Golaszewski <bartekgola@gmail.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config SHUF
//config:	bool "shuf (5.4 kb)"
//config:	default y
//config:	help
//config:	Generate random permutations

//applet:IF_SHUF(APPLET_NOEXEC(shuf, shuf, BB_DIR_USR_BIN, BB_SUID_DROP, shuf))

//kbuild:lib-$(CONFIG_SHUF) += shuf.o

//usage:#define shuf_trivial_usage
//usage:       "[-e|-i L-H] [-n NUM] [-o FILE] [-z] [FILE|ARG...]"
//usage:#define shuf_full_usage "\n\n"
//usage:       "Randomly permute lines\n"
//usage:     "\n	-e	Treat ARGs as lines"
//usage:     "\n	-i L-H	Treat numbers L-H as lines"
//usage:     "\n	-n NUM	Output at most NUM lines"
//usage:     "\n	-o FILE	Write to FILE, not standard output"
//usage:     "\n	-z	End lines with zero byte, not newline"

#include "libbb.h"

/* This is a NOEXEC applet. Be very careful! */

#define OPT_e		(1 << 0)
#define OPT_i		(1 << 1)
#define OPT_n		(1 << 2)
#define OPT_o		(1 << 3)
#define OPT_z		(1 << 4)
#define OPT_STR		"ei:n:o:z"

/*
 * Use the Fisher-Yates shuffle algorithm on an array of lines.
 */
static void shuffle_lines(char **lines, unsigned numlines)
{
	unsigned i;
	unsigned r;
	char *tmp;

	srand(monotonic_us());

	for (i = numlines-1; i > 0; i--) {
		r = rand();
		/* RAND_MAX can be as small as 32767 */
		if (i > RAND_MAX)
			r ^= rand() << 15;
		r %= i + 1;
		tmp = lines[i];
		lines[i] = lines[r];
		lines[r] = tmp;
	}
}

int shuf_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int shuf_main(int argc, char **argv)
{
	unsigned opts;
	char *opt_i_str, *opt_n_str, *opt_o_str;
	unsigned i;
	char **lines;
	unsigned numlines;
	char eol;

	opts = getopt32(argv, "^"
			OPT_STR
			"\0" "e--i:i--e"/* mutually exclusive */,
			&opt_i_str, &opt_n_str, &opt_o_str
	);

	argc -= optind;
	argv += optind;

	/* Prepare lines for shuffling - either: */
	if (opts & OPT_e) {
		/* make lines from command-line arguments */
		numlines = argc;
		lines = argv;
	} else
	if (opts & OPT_i) {
		/* create a range of numbers */
		char *dash;
		unsigned lo, hi;

		dash = strchr(opt_i_str, '-');
		if (!dash) {
			bb_error_msg_and_die("bad range '%s'", opt_i_str);
		}
		*dash = '\0';
		lo = xatou(opt_i_str);
		hi = xatou(dash + 1);
		*dash = '-';
		if (hi < lo) {
			bb_error_msg_and_die("bad range '%s'", opt_i_str);
		}

		numlines = (hi+1) - lo;
		lines = xmalloc(numlines * sizeof(lines[0]));
		for (i = 0; i < numlines; i++) {
			lines[i] = (char*)(uintptr_t)lo;
			lo++;
		}
	} else {
		/* default - read lines from stdin or the input file */
		FILE *fp;

		if (argc > 1)
			bb_show_usage();

		fp = xfopen_stdin(argv[0] ? argv[0] : "-");
		lines = NULL;
		numlines = 0;
		for (;;) {
			char *line = xmalloc_fgetline(fp);
			if (!line)
				break;
			lines = xrealloc_vector(lines, 6, numlines);
			lines[numlines++] = line;
		}
		fclose_if_not_stdin(fp);
	}

	if (numlines != 0)
		shuffle_lines(lines, numlines);

	if (opts & OPT_o)
		xmove_fd(xopen(opt_o_str, O_WRONLY|O_CREAT|O_TRUNC), STDOUT_FILENO);

	if (opts & OPT_n) {
		unsigned maxlines;
		maxlines = xatou(opt_n_str);
		if (numlines > maxlines)
			numlines = maxlines;
	}

	eol = '\n';
	if (opts & OPT_z)
		eol = '\0';

	for (i = 0; i < numlines; i++) {
		if (opts & OPT_i)
			printf("%u%c", (unsigned)(uintptr_t)lines[i], eol);
		else
			printf("%s%c", lines[i], eol);
	}

	fflush_stdout_and_exit(EXIT_SUCCESS);
}
