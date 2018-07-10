/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2017 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config NL
//config:	bool "nl (4.3 kb)"
//config:	default y
//config:	help
//config:	nl is used to number lines of files.

//applet:IF_NL(APPLET(nl, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_NL) += nl.o

//usage:#define nl_trivial_usage
//usage:       "[OPTIONS] [FILE]..."
//usage:#define nl_full_usage "\n\n"
//usage:       "Write FILEs to standard output with line numbers added\n"
//usage:     "\n	-b STYLE	Which lines to number - a: all, t: nonempty, n: none"
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^TODO: support "pBRE": number only lines thatmatch regexp BRE"
////usage:     "\n	-f STYLE	footer lines"
////usage:     "\n	-h STYLE	header lines"
////usage:     "\n	-d CC		use CC for separating logical pages"
//usage:     "\n	-i N		Line number increment"
////usage:     "\n	-l NUMBER	group of NUMBER empty lines counted as one"
////usage:     "\n	-n FORMAT	lneft justified, no leading zeros; rn or rz"
////usage:     "\n	-p 		do not reset line numbers at logical pages (huh?)"
//usage:     "\n	-s STRING	Use STRING as line number separator"
//usage:     "\n	-v N		Start from N"
//usage:     "\n	-w N		Width of line numbers"

/* By default, selects -v1 -i1 -l1 -sTAB -w6 -nrn -hn -bt -fn */

#include "libbb.h"

int nl_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int nl_main(int argc UNUSED_PARAM, char **argv)
{
	struct number_state ns;
	const char *opt_b = "t";
	enum {
		OPT_p = (1 << 0),
	};
#if ENABLE_LONG_OPTS
	static const char nl_longopts[] ALIGN1 =
		"body-numbering\0"	Required_argument "b"
	//	"footer-numbering\0"	Required_argument "f" - not implemented yet
	//	"header-numbering\0"	Required_argument "h" - not implemented yet
	//	"section-delimiter\0"	Required_argument "d" - not implemented yet
		"line-increment\0"	Required_argument "i"
	//	"join-blank-lines\0"	Required_argument "l" - not implemented yet
	//	"number-format\0"	Required_argument "n" - not implemented yet
		"no-renumber\0"		No_argument       "p" // no-op so far
		"number-separator\0"	Required_argument "s"
		"starting-line-number\0"Required_argument "v"
		"number-width\0"	Required_argument "w"
	;
#endif
	ns.width = 6;
	ns.start = 1;
	ns.inc = 1;
	ns.sep = "\t";
	getopt32long(argv, "pw:+s:v:+i:+b:", nl_longopts,
			&ns.width, &ns.sep, &ns.start, &ns.inc, &opt_b);
	ns.all = (opt_b[0] == 'a');
	ns.nonempty = (opt_b[0] == 't');
	ns.empty_str = xasprintf("%*s\n", ns.width + (int)strlen(ns.sep), "");

	argv += optind;
	if (!*argv)
		*--argv = (char*)"-";

	do {
		print_numbered_lines(&ns, *argv);
	} while (*++argv);

	fflush_stdout_and_exit(EXIT_SUCCESS);
}
