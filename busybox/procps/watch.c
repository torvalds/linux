/* vi: set sw=4 ts=4: */
/*
 * Mini watch implementation for busybox
 *
 * Copyright (C) 2001 by Michael Habermann <mhabermann@gmx.de>
 * Copyrigjt (C) Mar 16, 2003 Manuel Novoa III   (mjn3@codepoet.org)
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config WATCH
//config:	bool "watch (4.1 kb)"
//config:	default y
//config:	help
//config:	watch is used to execute a program periodically, showing
//config:	output to the screen.

//applet:IF_WATCH(APPLET(watch, BB_DIR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_WATCH) += watch.o

//usage:#define watch_trivial_usage
//usage:       "[-n SEC] [-t] PROG ARGS"
//usage:#define watch_full_usage "\n\n"
//usage:       "Run PROG periodically\n"
//usage:     "\n	-n	Loop period in seconds (default 2)"
//usage:     "\n	-t	Don't print header"
//usage:
//usage:#define watch_example_usage
//usage:       "$ watch date\n"
//usage:       "Mon Dec 17 10:31:40 GMT 2000\n"
//usage:       "Mon Dec 17 10:31:42 GMT 2000\n"
//usage:       "Mon Dec 17 10:31:44 GMT 2000"

/* BB_AUDIT SUSv3 N/A */
/* BB_AUDIT GNU defects -- only option -n is supported. */

#include "libbb.h"

#define ESC "\033"

// procps 2.0.18:
// watch [-d] [-n seconds]
//   [--differences[=cumulative]] [--interval=seconds] command
//
// procps-3.2.3:
// watch [-dt] [-n seconds]
//   [--differences[=cumulative]] [--interval=seconds] [--no-title] command
//
// (procps 3.x and procps 2.x are forks, not newer/older versions of the same)

int watch_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int watch_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned opt;
	unsigned period = 2;
	unsigned width, new_width;
	char *header;
	char *cmd;

#if 0 // maybe ENABLE_DESKTOP?
	// procps3 compat - "echo TEST | watch cat" doesn't show TEST:
	close(STDIN_FILENO);
	xopen("/dev/null", O_RDONLY);
#endif

	// "+": stop at first non-option (procps 3.x only); -n NUM
	// at least one param
	opt = getopt32(argv, "^+" "dtn:+" "\0" "-1", &period);
	argv += optind;

	// watch from both procps 2.x and 3.x does concatenation. Example:
	// watch ls -l "a /tmp" "2>&1" - ls won't see "a /tmp" as one param
	cmd = *argv;
	while (*++argv)
		cmd = xasprintf("%s %s", cmd, *argv); // leaks cmd

	width = (unsigned)-1; // make sure first time new_width != width
	header = NULL;
	while (1) {
		/* home; clear to the end of screen */
		printf(ESC"[H" ESC"[J");
		if (!(opt & 0x2)) { // no -t
			const unsigned time_len = sizeof("1234-67-90 23:56:89");

			// STDERR_FILENO is procps3 compat:
			// "watch ls 2>/dev/null" does not detect tty size
			new_width = get_terminal_width(STDERR_FILENO);
			if (new_width != width) {
				width = new_width;
				free(header);
				header = xasprintf("Every %us: %-*s", period, (int)width, cmd);
			}
			if (time_len < width) {
				strftime_YYYYMMDDHHMMSS(
					header + width - time_len,
					time_len,
					/*time_t*:*/ NULL
				);
			}

			// compat: empty line between header and cmd output
			printf("%s\n\n", header);
		}
		fflush_all();
		// TODO: 'real' watch pipes cmd's output to itself
		// and does not allow it to overflow the screen
		// (taking into account linewrap!)
		system(cmd);
		sleep(period);
	}
	return 0; // gcc thinks we can reach this :)
}
