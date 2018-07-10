/* vi: set sw=4 ts=4: */
/*
 * nice implementation for busybox
 *
 * Copyright (C) 2005  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config NICE
//config:	bool "nice (1.8 kb)"
//config:	default y
//config:	help
//config:	nice runs a program with modified scheduling priority.

//applet:IF_NICE(APPLET_NOEXEC(nice, nice, BB_DIR_BIN, BB_SUID_DROP, nice))

//kbuild:lib-$(CONFIG_NICE) += nice.o

//usage:#define nice_trivial_usage
//usage:       "[-n ADJUST] [PROG ARGS]"
//usage:#define nice_full_usage "\n\n"
//usage:       "Change scheduling priority, run PROG\n"
//usage:     "\n	-n ADJUST	Adjust priority by ADJUST"

#include "libbb.h"

int nice_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int nice_main(int argc UNUSED_PARAM, char **argv)
{
	int old_priority, adjustment;

	old_priority = getpriority(PRIO_PROCESS, 0);

	if (!*++argv) { /* No args, so (GNU) output current nice value. */
		printf("%d\n", old_priority);
		fflush_stdout_and_exit(EXIT_SUCCESS);
	}

	adjustment = 10;  /* Set default adjustment. */

	if (argv[0][0] == '-') {
		char *nnn = argv[0] + 1;
		if (nnn[0] == 'n') { /* -n */
			nnn += 1;
			if (!nnn[0]) { /* "-n NNN" */
				nnn = *++argv;
			}
			/* else: "-nNNN" (w/o space) */
		}
		/* else: "-NNN" (NNN may be negative) - same as "-n NNN" */

		if (!nnn || !argv[1]) {  /* Missing priority or PROG! */
			bb_show_usage();
		}
		adjustment = xatoi_range(nnn, INT_MIN/2, INT_MAX/2);
		argv++;
	}

	{  /* Set our priority. */
		int prio = old_priority + adjustment;

		if (setpriority(PRIO_PROCESS, 0, prio) < 0) {
			bb_perror_msg_and_die("setpriority(%d)", prio);
		}
	}

	BB_EXECVP_or_die(argv);
}
