/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * COPYING NOTES
 *
 * timeout.c -- a timeout handler for shell commands
 *
 * Copyright (C) 2005-6, Roberto A. Foglietta <me@roberto.foglietta.name>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * REVISION NOTES:
 * released 17-11-2005 by Roberto A. Foglietta
 * talarm   04-12-2005 by Roberto A. Foglietta
 * modified 05-12-2005 by Roberto A. Foglietta
 * sizerdct 06-12-2005 by Roberto A. Foglietta
 * splitszf 12-05-2006 by Roberto A. Foglietta
 * rewrite  14-11-2008 vda
 */
//config:config TIMEOUT
//config:	bool "timeout (5.5 kb)"
//config:	default y
//config:	help
//config:	Runs a program and watches it. If it does not terminate in
//config:	specified number of seconds, it is sent a signal.

//applet:IF_TIMEOUT(APPLET(timeout, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_TIMEOUT) += timeout.o

//usage:#define timeout_trivial_usage
//usage:       "[-t SECS] [-s SIG] PROG ARGS"
//usage:#define timeout_full_usage "\n\n"
//usage:       "Runs PROG. Sends SIG to it if it is not gone in SECS seconds.\n"
//usage:       "Defaults: SECS: 10, SIG: TERM."

#include "libbb.h"

int timeout_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int timeout_main(int argc UNUSED_PARAM, char **argv)
{
	int signo;
	int status;
	int parent = 0;
	int timeout = 10;
	pid_t pid;
#if !BB_MMU
	char *sv1, *sv2;
#endif
	const char *opt_s = "TERM";

	/* -p option is not documented, it is needed to support NOMMU. */

	/* -t SECONDS; -p PARENT_PID */
	/* '+': stop at first non-option */
	getopt32(argv, "+s:t:+" USE_FOR_NOMMU("p:+"), &opt_s, &timeout, &parent);
	/*argv += optind; - no, wait for bb_daemonize_or_rexec! */
	signo = get_signum(opt_s);
	if (signo < 0)
		bb_error_msg_and_die("unknown signal '%s'", opt_s);

	/* We want to create a grandchild which will watch
	 * and kill the grandparent. Other methods:
	 * making parent watch child disrupts parent<->child link
	 * (example: "tcpsvd 0.0.0.0 1234 timeout service_prog" -
	 * it's better if service_prog is a child of tcpsvd!),
	 * making child watch parent results in programs having
	 * unexpected children. */

	if (parent) /* we were re-execed, already grandchild */
		goto grandchild;
	if (!argv[optind]) /* no PROG? */
		bb_show_usage();

#if !BB_MMU
	sv1 = argv[optind];
	sv2 = argv[optind + 1];
#endif
	pid = xvfork();
	if (pid == 0) {
		/* Child: spawn grandchild and exit */
		parent = getppid();
#if !BB_MMU
		argv[optind] = xasprintf("-p%u", parent);
		argv[optind + 1] = NULL;
#endif
		/* NB: exits with nonzero on error: */
		bb_daemonize_or_rexec(0, argv);
		/* Here we are grandchild. Sleep, then kill grandparent */
 grandchild:
		/* Just sleep(HUGE_NUM); kill(parent) may kill wrong process! */
		while (1) {
			sleep(1);
			if (--timeout <= 0)
				break;
			if (kill(parent, 0)) {
				/* process is gone */
				return EXIT_SUCCESS;
			}
		}
		kill(parent, signo);
		return EXIT_SUCCESS;
	}

	/* Parent */
	wait(&status); /* wait for child to die */
	/* Did intermediate [v]fork or exec fail? */
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return EXIT_FAILURE;
	/* Ok, exec a program as requested */
	argv += optind;
#if !BB_MMU
	argv[0] = sv1;
	argv[1] = sv2;
#endif
	BB_EXECVP_or_die(argv);
}
