/* vi: set sw=4 ts=4: */
/*
 * Mini basename implementation for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* Mar 16, 2003      Manuel Novoa III   (mjn3@codepoet.org)
 *
 * Changes:
 * 1) Now checks for too many args.  Need at least one and at most two.
 * 2) Don't check for options, as per SUSv3.
 * 3) Save some space by using strcmp().  Calling strncmp() here was silly.
 */
//config:config BASENAME
//config:	bool "basename (371 bytes)"
//config:	default y
//config:	help
//config:	basename is used to strip the directory and suffix from filenames,
//config:	leaving just the filename itself. Enable this option if you wish
//config:	to enable the 'basename' utility.

//applet:IF_BASENAME(APPLET_NOFORK(basename, basename, BB_DIR_USR_BIN, BB_SUID_DROP, basename))

//kbuild:lib-$(CONFIG_BASENAME) += basename.o

/* BB_AUDIT SUSv3 compliant */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/basename.html */

//usage:#define basename_trivial_usage
//usage:       "FILE [SUFFIX]"
//usage:#define basename_full_usage "\n\n"
//usage:       "Strip directory path and .SUFFIX from FILE"
//usage:
//usage:#define basename_example_usage
//usage:       "$ basename /usr/local/bin/foo\n"
//usage:       "foo\n"
//usage:       "$ basename /usr/local/bin/\n"
//usage:       "bin\n"
//usage:       "$ basename /foo/bar.txt .txt\n"
//usage:       "bar"

#include "libbb.h"

/* This is a NOFORK applet. Be very careful! */

int basename_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int basename_main(int argc UNUSED_PARAM, char **argv)
{
	size_t m, n;
	char *s;

	if (argv[1] && strcmp(argv[1], "--") == 0) {
		argv++;
	}
	if (!argv[1])
		bb_show_usage();

	/* It should strip slash: /abc/def/ -> def */
	s = bb_get_last_path_component_strip(*++argv);

	m = strlen(s);
	if (*++argv) {
		if (argv[1])
			bb_show_usage();
		n = strlen(*argv);
		if ((m > n) && (strcmp(s+m-n, *argv) == 0)) {
			m -= n;
			/*s[m] = '\0'; - redundant */
		}
	}

	/* puts(s) will do, but we can do without stdio this way: */
	s[m++] = '\n';
	/* NB: != is correct here: */
	return full_write(STDOUT_FILENO, s, m) != (ssize_t)m;
}
