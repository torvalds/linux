/* vi: set sw=4 ts=4: */
/*
 * Prints out the previous and the current runlevel.
 *
 * Version: @(#)runlevel  1.20  16-Apr-1997  MvS
 *
 * This file is part of the sysvinit suite,
 * Copyright 1991-1997 Miquel van Smoorenburg.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * initially busyboxified by Bernhard Reutner-Fischer
 */
//config:config RUNLEVEL
//config:	bool "runlevel (518 bytes)"
//config:	default y
//config:	depends on FEATURE_UTMP
//config:	help
//config:	find the current and previous system runlevel.
//config:
//config:	This applet uses utmp but does not rely on busybox supporing
//config:	utmp on purpose. It is used by e.g. emdebian via /etc/init.d/rc.

//applet:IF_RUNLEVEL(APPLET_NOEXEC(runlevel, runlevel, BB_DIR_SBIN, BB_SUID_DROP, runlevel))

//kbuild:lib-$(CONFIG_RUNLEVEL) += runlevel.o

//usage:#define runlevel_trivial_usage
//usage:       "[FILE]"
//usage:#define runlevel_full_usage "\n\n"
//usage:       "Find the current and previous system runlevel\n"
//usage:       "\n"
//usage:       "If no utmp FILE exists or if no runlevel record can be found,\n"
//usage:       "print \"unknown\""
//usage:
//usage:#define runlevel_example_usage
//usage:       "$ runlevel /var/run/utmp\n"
//usage:       "N 2"

#include "libbb.h"

int runlevel_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int runlevel_main(int argc UNUSED_PARAM, char **argv)
{
	struct utmpx *ut;
	char prev;

	if (argv[1]) utmpxname(argv[1]);

	setutxent();
	while ((ut = getutxent()) != NULL) {
		if (ut->ut_type == RUN_LVL) {
			prev = ut->ut_pid / 256;
			if (prev == 0) prev = 'N';
			printf("%c %c\n", prev, ut->ut_pid % 256);
			if (ENABLE_FEATURE_CLEAN_UP)
				endutxent();
			return 0;
		}
	}

	puts("unknown");

	if (ENABLE_FEATURE_CLEAN_UP)
		endutxent();
	return 1;
}
