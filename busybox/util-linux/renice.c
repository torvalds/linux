/* vi: set sw=4 ts=4: */
/*
 * renice implementation for busybox
 *
 * Copyright (C) 2005  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* Notes:
 *   Setting an absolute priority was obsoleted in SUSv2 and removed
 *   in SUSv3.  However, the common linux version of renice does
 *   absolute and not relative.  So we'll continue supporting absolute,
 *   although the stdout logging has been removed since both SUSv2 and
 *   SUSv3 specify that stdout isn't used.
 *
 *   This version is lenient in that it doesn't require any IDs.  The
 *   options -p, -g, and -u are treated as mode switches for the
 *   following IDs (if any).  Multiple switches are allowed.
 */
//config:config RENICE
//config:	bool "renice (3.8 kb)"
//config:	default y
//config:	help
//config:	Renice alters the scheduling priority of one or more running
//config:	processes.

//applet:IF_RENICE(APPLET_NOEXEC(renice, renice, BB_DIR_USR_BIN, BB_SUID_DROP, renice))

//kbuild:lib-$(CONFIG_RENICE) += renice.o

//usage:#define renice_trivial_usage
//usage:       "[-n] PRIORITY [[-p | -g | -u] ID...]..."
//usage:#define renice_full_usage "\n\n"
//usage:       "Change scheduling priority of a running process\n"
//usage:     "\n	-n	Add PRIORITY to current nice value"
//usage:     "\n		Without -n, nice value is set to PRIORITY"
//usage:     "\n	-p	Process ids (default)"
//usage:     "\n	-g	Process group ids"
//usage:     "\n	-u	Process user names"

#include "libbb.h"

int renice_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int renice_main(int argc UNUSED_PARAM, char **argv)
{
	static const char Xetpriority_msg[] ALIGN1 = "%cetpriority";

	int retval = EXIT_SUCCESS;
	int which = PRIO_PROCESS;  /* Default 'which' value. */
	int use_relative = 0;
	int adjustment, new_priority;
	unsigned who;
	char *arg;

	/* Yes, they are not #defines in glibc 2.4! #if won't work */
	BUILD_BUG_ON(PRIO_PROCESS < CHAR_MIN || PRIO_PROCESS > CHAR_MAX);
	BUILD_BUG_ON(PRIO_PGRP < CHAR_MIN || PRIO_PGRP > CHAR_MAX);
	BUILD_BUG_ON(PRIO_USER < CHAR_MIN || PRIO_USER > CHAR_MAX);

	arg = *++argv;

	/* Check if we are using a relative adjustment. */
	if (arg && arg[0] == '-' && arg[1] == 'n') {
		use_relative = 1;
		if (!arg[2])
			arg = *++argv;
		else
			arg += 2;
	}

	if (!arg) {  /* No args?  Then show usage. */
		bb_show_usage();
	}

	/* Get the priority adjustment (absolute or relative). */
	adjustment = xatoi_range(arg, INT_MIN/2, INT_MAX/2);

	while ((arg = *++argv) != NULL) {
		/* Check for a mode switch. */
		if (arg[0] == '-' && arg[1]) {
			static const char opts[] ALIGN1 = {
				'p', 'g', 'u', 0, PRIO_PROCESS, PRIO_PGRP, PRIO_USER
			};
			const char *p = strchr(opts, arg[1]);
			if (p) {
				which = p[4];
				if (!arg[2])
					continue;
				arg += 2;
			}
		}

		/* Process an ID arg. */
		if (which == PRIO_USER) {
			struct passwd *p;
			/* NB: use of getpwnam makes it risky to be NOFORK, switch to getpwnam_r? */
			p = getpwnam(arg);
			if (!p) {
				bb_error_msg("unknown user %s", arg);
				goto HAD_ERROR;
			}
			who = p->pw_uid;
		} else {
			who = bb_strtou(arg, NULL, 10);
			if (errno) {
				bb_error_msg("invalid number '%s'", arg);
				goto HAD_ERROR;
			}
		}

		/* Get priority to use, and set it. */
		if (use_relative) {
			int old_priority;

			errno = 0;  /* Needed for getpriority error detection. */
			old_priority = getpriority(which, who);
			if (errno) {
				bb_perror_msg(Xetpriority_msg, 'g');
				goto HAD_ERROR;
			}

			new_priority = old_priority + adjustment;
		} else {
			new_priority = adjustment;
		}

		if (setpriority(which, who, new_priority) == 0) {
			continue;
		}

		bb_perror_msg(Xetpriority_msg, 's');
 HAD_ERROR:
		retval = EXIT_FAILURE;
	}

	/* No need to check for errors outputting to stderr since, if it
	 * was used, the HAD_ERROR label was reached and retval was set. */

	return retval;
}
