/* vi: set sw=4 ts=4: */
/*
 * usleep implementation for busybox
 *
 * Copyright (C) 2003  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config USLEEP
//config:	bool "usleep (1.1 kb)"
//config:	default y
//config:	help
//config:	usleep is used to pause for a specified number of microseconds.

//applet:IF_USLEEP(APPLET_NOFORK(usleep, usleep, BB_DIR_BIN, BB_SUID_DROP, usleep))

//kbuild:lib-$(CONFIG_USLEEP) += usleep.o

/* BB_AUDIT SUSv3 N/A -- Apparently a busybox extension. */

//usage:#define usleep_trivial_usage
//usage:       "N"
//usage:#define usleep_full_usage "\n\n"
//usage:       "Pause for N microseconds"
//usage:
//usage:#define usleep_example_usage
//usage:       "$ usleep 1000000\n"
//usage:       "[pauses for 1 second]\n"

#include "libbb.h"

/* This is a NOFORK applet. Be very careful! */

int usleep_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int usleep_main(int argc UNUSED_PARAM, char **argv)
{
	if (!argv[1]) {
		bb_show_usage();
	}

	/* Safe wrt NOFORK? (noforks are not allowed to run for
	 * a long time). Try "usleep 99999999" + ^C + "echo $?"
	 * in hush with FEATURE_SH_NOFORK=y.
	 * At least on uclibc, usleep() thanslates to nanosleep()
	 * which returns early on any signal (even caught one),
	 * and uclibc does not loop back on EINTR.
	 */
	usleep(xatou(argv[1]));

	return EXIT_SUCCESS;
}
