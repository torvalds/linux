/* vi: set sw=4 ts=4: */
/*
 * Mini chgrp implementation for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config CHGRP
//config:	bool "chgrp (7.2 kb)"
//config:	default y
//config:	help
//config:	chgrp is used to change the group ownership of files.

//applet:IF_CHGRP(APPLET_NOEXEC(chgrp, chgrp, BB_DIR_BIN, BB_SUID_DROP, chgrp))

//kbuild:lib-$(CONFIG_CHGRP) += chgrp.o chown.o

/* BB_AUDIT SUSv3 defects - none? */
/* BB_AUDIT GNU defects - unsupported long options. */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/chgrp.html */

//usage:#define chgrp_trivial_usage
//usage:       "[-RhLHP"IF_DESKTOP("cvf")"]... GROUP FILE..."
//usage:#define chgrp_full_usage "\n\n"
//usage:       "Change the group membership of each FILE to GROUP\n"
//usage:     "\n	-R	Recurse"
//usage:     "\n	-h	Affect symlinks instead of symlink targets"
//usage:     "\n	-L	Traverse all symlinks to directories"
//usage:     "\n	-H	Traverse symlinks on command line only"
//usage:     "\n	-P	Don't traverse symlinks (default)"
//usage:	IF_DESKTOP(
//usage:     "\n	-c	List changed files"
//usage:     "\n	-v	Verbose"
//usage:     "\n	-f	Hide errors"
//usage:	)
//usage:
//usage:#define chgrp_example_usage
//usage:       "$ ls -l /tmp/foo\n"
//usage:       "-r--r--r--    1 andersen andersen        0 Apr 12 18:25 /tmp/foo\n"
//usage:       "$ chgrp root /tmp/foo\n"
//usage:       "$ ls -l /tmp/foo\n"
//usage:       "-r--r--r--    1 andersen root            0 Apr 12 18:25 /tmp/foo\n"

#include "libbb.h"

/* This is a NOEXEC applet. Be very careful! */


int chgrp_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int chgrp_main(int argc, char **argv)
{
	/* "chgrp [opts] abc file(s)" == "chown [opts] :abc file(s)" */
	char **p = argv;
	while (*++p) {
		if (p[0][0] != '-') {
			p[0] = xasprintf(":%s", p[0]);
			break;
		}
	}
	return chown_main(argc, argv);
}
