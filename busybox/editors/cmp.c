/* vi: set sw=4 ts=4: */
/*
 * Mini cmp implementation for busybox
 *
 * Copyright (C) 2000,2001 by Matt Kraai <kraai@alumni.carnegiemellon.edu>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config CMP
//config:	bool "cmp (5.4 kb)"
//config:	default y
//config:	help
//config:	cmp is used to compare two files and returns the result
//config:	to standard output.

//applet:IF_CMP(APPLET(cmp, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_CMP) += cmp.o

//usage:#define cmp_trivial_usage
//usage:       "[-l] [-s] FILE1 [FILE2" IF_DESKTOP(" [SKIP1 [SKIP2]]") "]"
//usage:#define cmp_full_usage "\n\n"
//usage:       "Compare FILE1 with FILE2 (or stdin)\n"
//usage:     "\n	-l	Write the byte numbers (decimal) and values (octal)"
//usage:     "\n		for all differing bytes"
//usage:     "\n	-s	Quiet"

/* BB_AUDIT SUSv3 (virtually) compliant -- uses nicer GNU format for -l. */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/cmp.html */

#include "libbb.h"

static const char fmt_eof[] ALIGN1 = "cmp: EOF on %s\n";
static const char fmt_differ[] ALIGN1 = "%s %s differ: char %"OFF_FMT"u, line %u\n";
// This fmt_l_opt uses gnu-isms.  SUSv3 would be "%.0s%.0s%"OFF_FMT"u %o %o\n"
static const char fmt_l_opt[] ALIGN1 = "%.0s%.0s%"OFF_FMT"u %3o %3o\n";

#define OPT_STR "sl"
#define CMP_OPT_s (1<<0)
#define CMP_OPT_l (1<<1)

int cmp_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int cmp_main(int argc UNUSED_PARAM, char **argv)
{
	FILE *fp1, *fp2, *outfile = stdout;
	const char *filename1, *filename2 = "-";
	off_t skip1 = 0, skip2 = 0, char_pos = 0;
	int line_pos = 1; /* Hopefully won't overflow... */
	const char *fmt;
	int c1, c2;
	unsigned opt;
	int retval = 0;

	opt = getopt32(argv, "^"
			OPT_STR
			"\0" "-1"
			IF_DESKTOP(":?4")
			IF_NOT_DESKTOP(":?2")
			":l--s:s--l"
	);
	argv += optind;

	filename1 = *argv;
	if (*++argv) {
		filename2 = *argv;
		if (ENABLE_DESKTOP && *++argv) {
			skip1 = XATOOFF(*argv);
			if (*++argv) {
				skip2 = XATOOFF(*argv);
			}
		}
	}

	xfunc_error_retval = 2;  /* missing file results in exitcode 2 */
	if (opt & CMP_OPT_s)
		logmode = 0;  /* -s suppresses open error messages */
	fp1 = xfopen_stdin(filename1);
	fp2 = xfopen_stdin(filename2);
	if (fp1 == fp2) {		/* Paranoia check... stdin == stdin? */
		/* Note that we don't bother reading stdin.  Neither does gnu wc.
		 * But perhaps we should, so that other apps down the chain don't
		 * get the input.  Consider 'echo hello | (cmp - - && cat -)'.
		 */
		return 0;
	}
	logmode = LOGMODE_STDIO;

	if (opt & CMP_OPT_l)
		fmt = fmt_l_opt;
	else
		fmt = fmt_differ;

	if (ENABLE_DESKTOP) {
		while (skip1) { getc(fp1); skip1--; }
		while (skip2) { getc(fp2); skip2--; }
	}
	do {
		c1 = getc(fp1);
		c2 = getc(fp2);
		++char_pos;
		if (c1 != c2) {			/* Remember: a read error may have occurred. */
			retval = 1;		/* But assume the files are different for now. */
			if (c2 == EOF) {
				/* We know that fp1 isn't at EOF or in an error state.  But to
				 * save space below, things are setup to expect an EOF in fp1
				 * if an EOF occurred.  So, swap things around.
				 */
				fp1 = fp2;
				filename1 = filename2;
				c1 = c2;
			}
			if (c1 == EOF) {
				die_if_ferror(fp1, filename1);
				fmt = fmt_eof;	/* Well, no error, so it must really be EOF. */
				outfile = stderr;
				/* There may have been output to stdout (option -l), so
				 * make sure we fflush before writing to stderr. */
				fflush_all();
			}
			if (!(opt & CMP_OPT_s)) {
				if (opt & CMP_OPT_l) {
					line_pos = c1;	/* line_pos is unused in the -l case. */
				}
				fprintf(outfile, fmt, filename1, filename2, char_pos, line_pos, c2);
				if (opt) {	/* This must be -l since not -s. */
					/* If we encountered an EOF,
					 * the while check will catch it. */
					continue;
				}
			}
			break;
		}
		if (c1 == '\n') {
			++line_pos;
		}
	} while (c1 != EOF);

	die_if_ferror(fp1, filename1);
	die_if_ferror(fp2, filename2);

	fflush_stdout_and_exit(retval);
}
