/* vi: set sw=4 ts=4: */
/*
 * setsid.c -- execute a command in a new session
 * Rick Sladkey <jrs@world.std.com>
 * In the public domain.
 *
 * 1999-02-22 Arkadiusz Mickiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 * 2001-01-18 John Fremlin <vii@penguinpowered.com>
 * - fork in case we are process group leader
 *
 * 2004-11-12 Paul Fox
 * - busyboxed
 */
//config:config SETSID
//config:	bool "setsid (3.9 kb)"
//config:	default y
//config:	help
//config:	setsid runs a program in a new session

//applet:IF_SETSID(APPLET(setsid, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_SETSID) += setsid.o

//usage:#define setsid_trivial_usage
//usage:       "[-c] PROG ARGS"
//usage:#define setsid_full_usage "\n\n"
//usage:       "Run PROG in a new session. PROG will have no controlling terminal\n"
//usage:       "and will not be affected by keyboard signals (^C etc).\n"
//usage:     "\n	-c	Set controlling terminal to stdin"

#include "libbb.h"

int setsid_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int setsid_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned opt;

	/* +: stop on first non-opt */
	opt = getopt32(argv, "^+" "c" "\0" "-1"/* at least one arg */);
	argv += optind;

	/* setsid() is allowed only when we are not a process group leader.
	 * Otherwise our PID serves as PGID of some existing process group
	 * and cannot be used as PGID of a new process group.
	 *
	 * Example: setsid() below fails when run alone in interactive shell:
	 *  $ setsid PROG
	 * because shell's child (setsid) is put in a new process group.
	 * But doesn't fail if shell is not interactive
	 * (and therefore doesn't create process groups for pipes),
	 * or if setsid is not the first process in the process group:
	 *  $ true | setsid PROG
	 * or if setsid is executed in backquotes (`setsid PROG`)...
	 */
	if (setsid() < 0) {
		pid_t pid = fork_or_rexec(argv);
		if (pid != 0) {
			/* parent */
			/* TODO:
			 * we can waitpid(pid, &status, 0) and then even
			 * emulate exitcode, making the behavior consistent
			 * in both forked and non forked cases.
			 * However, the code is larger and upstream
			 * does not do such trick.
			 */
			return EXIT_SUCCESS;
		}

		/* child */
		/* now there should be no error: */
		setsid();
	}

	if (opt) {
		/* -c: set (with stealing) controlling tty */
		ioctl(0, TIOCSCTTY, 1);
	}

	BB_EXECVP_or_die(argv);
}
