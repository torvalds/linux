/* vi: set sw=4 ts=4: */
/*
 * yes implementation for busybox
 *
 * Copyright (C) 2003  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* Mar 16, 2003      Manuel Novoa III   (mjn3@codepoet.org)
 *
 * Size reductions and removed redundant applet name prefix from error messages.
 */
//config:config YES
//config:	bool "yes (956 bytes)"
//config:	default y
//config:	help
//config:	yes is used to repeatedly output a specific string, or
//config:	the default string 'y'.

//applet:IF_YES(APPLET_NOEXEC(yes, yes, BB_DIR_USR_BIN, BB_SUID_DROP, yes))
/* was NOFORK, but then yes can't be ^C'ed if run by hush */

//kbuild:lib-$(CONFIG_YES) += yes.o

/* BB_AUDIT SUSv3 N/A -- Matches GNU behavior. */

//usage:#define yes_trivial_usage
//usage:       "[STRING]"
//usage:#define yes_full_usage "\n\n"
//usage:       "Repeatedly output a line with STRING, or 'y'"

#include "libbb.h"

int yes_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int yes_main(int argc UNUSED_PARAM, char **argv)
{
	char **pp;

	argv[0] = (char*)"y";
	if (argv[1])
		++argv;

	do {
		pp = argv;
		while (1) {
			fputs(*pp, stdout);
			if (!*++pp)
				break;
			putchar(' ');
		}
	} while (putchar('\n') != EOF);

	bb_perror_nomsg_and_die();
}
