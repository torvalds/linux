/* vi: set sw=4 ts=4: */
/*
 * seq implementation for busybox
 *
 * Copyright (C) 2004, Glenn McGrath
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config SEQ
//config:	bool "seq (3.6 kb)"
//config:	default y
//config:	help
//config:	print a sequence of numbers

//applet:IF_SEQ(APPLET_NOEXEC(seq, seq, BB_DIR_USR_BIN, BB_SUID_DROP, seq))
/* was NOFORK, but then "seq 1 999999999" can't be ^C'ed if run by hush */

//kbuild:lib-$(CONFIG_SEQ) += seq.o

//usage:#define seq_trivial_usage
//usage:       "[-w] [-s SEP] [FIRST [INC]] LAST"
//usage:#define seq_full_usage "\n\n"
//usage:       "Print numbers from FIRST to LAST, in steps of INC.\n"
//usage:       "FIRST, INC default to 1.\n"
//usage:     "\n	-w	Pad to last with leading zeros"
//usage:     "\n	-s SEP	String separator"

#include "libbb.h"

/* This is a NOEXEC applet. Be very careful! */

int seq_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int seq_main(int argc, char **argv)
{
	enum {
		OPT_w = (1 << 0),
		OPT_s = (1 << 1),
	};
	double first, last, increment, v;
	unsigned n;
	unsigned width;
	unsigned frac_part;
	const char *sep, *opt_s = "\n";
	unsigned opt;

#if ENABLE_LOCALE_SUPPORT
	/* Undo busybox.c: on input, we want to use dot
	 * as fractional separator, regardless of current locale */
	setlocale(LC_NUMERIC, "C");
#endif

	opt = getopt32(argv, "+ws:", &opt_s);
	argc -= optind;
	argv += optind;
	first = increment = 1;
	errno = 0;
	switch (argc) {
			char *pp;
		case 3:
			increment = strtod(argv[1], &pp);
			errno |= *pp;
		case 2:
			first = strtod(argv[0], &pp);
			errno |= *pp;
		case 1:
			last = strtod(argv[argc-1], &pp);
			if (!errno && *pp == '\0')
				break;
		default:
			bb_show_usage();
	}

#if ENABLE_LOCALE_SUPPORT
	setlocale(LC_NUMERIC, "");
#endif

	/* Last checked to be compatible with: coreutils-6.10 */
	width = 0;
	frac_part = 0;
	while (1) {
		char *dot = strchrnul(*argv, '.');
		int w = (dot - *argv);
		int f = strlen(dot);
		if (width < w)
			width = w;
		argv++;
		if (!*argv)
			break;
		/* Why do the above _before_ frac check below?
		 * Try "seq 1 2.0" and "seq 1.0 2.0":
		 * coreutils never pay attention to the number
		 * of fractional digits in last arg. */
		if (frac_part < f)
			frac_part = f;
	}
	if (frac_part) {
		frac_part--;
		if (frac_part)
			width += frac_part + 1;
	}
	if (!(opt & OPT_w))
		width = 0;

	sep = "";
	v = first;
	n = 0;
	while (increment >= 0 ? v <= last : v >= last) {
		if (printf("%s%0*.*f", sep, width, frac_part, v) < 0)
			break; /* I/O error, bail out (yes, this really happens) */
		sep = opt_s;
		/* v += increment; - would accumulate floating point errors */
		n++;
		v = first + n * increment;
	}
	if (n) /* if while loop executed at least once */
		bb_putchar('\n');

	return fflush_all();
}
