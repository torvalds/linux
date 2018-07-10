/* vi: set sw=4 ts=4: */
/*
 * Mini kill/killall[5] implementation for busybox
 *
 * Copyright (C) 1995, 1996 by Bruce Perens <bruce@pixar.com>.
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config KILL
//config:	bool "kill (2.6 kb)"
//config:	default y
//config:	help
//config:	The command kill sends the specified signal to the specified
//config:	process or process group. If no signal is specified, the TERM
//config:	signal is sent.
//config:
//config:config KILLALL
//config:	bool "killall (5.6 kb)"
//config:	default y
//config:	help
//config:	killall sends a signal to all processes running any of the
//config:	specified commands. If no signal name is specified, SIGTERM is
//config:	sent.
//config:
//config:config KILLALL5
//config:	bool "killall5 (5.3 kb)"
//config:	default y
//config:	help
//config:	The SystemV killall command. killall5 sends a signal
//config:	to all processes except kernel threads and the processes
//config:	in its own session, so it won't kill the shell that is running
//config:	the script it was called from.

//applet:IF_KILL(    APPLET_NOFORK(kill,     kill, BB_DIR_BIN,      BB_SUID_DROP, kill))
//                   APPLET_NOFORK:name      main  location         suid_type     help
//applet:IF_KILLALL( APPLET_NOFORK(killall,  kill, BB_DIR_USR_BIN,  BB_SUID_DROP, killall))
//applet:IF_KILLALL5(APPLET_NOFORK(killall5, kill, BB_DIR_USR_SBIN, BB_SUID_DROP, killall5))

//kbuild:lib-$(CONFIG_KILL) += kill.o
//kbuild:lib-$(CONFIG_KILLALL) += kill.o
//kbuild:lib-$(CONFIG_KILLALL5) += kill.o

//usage:#define kill_trivial_usage
//usage:       "[-l] [-SIG] PID..."
//usage:#define kill_full_usage "\n\n"
//usage:       "Send a signal (default: TERM) to given PIDs\n"
//usage:     "\n	-l	List all signal names and numbers"
/* //usage:  "\n	-s SIG	Yet another way of specifying SIG" */
//usage:
//usage:#define kill_example_usage
//usage:       "$ ps | grep apache\n"
//usage:       "252 root     root     S [apache]\n"
//usage:       "263 www-data www-data S [apache]\n"
//usage:       "264 www-data www-data S [apache]\n"
//usage:       "265 www-data www-data S [apache]\n"
//usage:       "266 www-data www-data S [apache]\n"
//usage:       "267 www-data www-data S [apache]\n"
//usage:       "$ kill 252\n"
//usage:
//usage:#define killall_trivial_usage
//usage:       "[-l] [-q] [-SIG] PROCESS_NAME..."
//usage:#define killall_full_usage "\n\n"
//usage:       "Send a signal (default: TERM) to given processes\n"
//usage:     "\n	-l	List all signal names and numbers"
/* //usage:  "\n	-s SIG	Yet another way of specifying SIG" */
//usage:     "\n	-q	Don't complain if no processes were killed"
//usage:
//usage:#define killall_example_usage
//usage:       "$ killall apache\n"
//usage:
//usage:#define killall5_trivial_usage
//usage:       "[-l] [-SIG] [-o PID]..."
//usage:#define killall5_full_usage "\n\n"
//usage:       "Send a signal (default: TERM) to all processes outside current session\n"
//usage:     "\n	-l	List all signal names and numbers"
//usage:     "\n	-o PID	Don't signal this PID"
/* //usage:  "\n	-s SIG	Yet another way of specifying SIG" */

#include "libbb.h"

/* Note: kill_main is directly called from shell in order to implement
 * kill built-in. Shell substitutes job ids with process groups first.
 *
 * This brings some complications:
 *
 * + we can't use xfunc here
 * + we can't use applet_name
 * + we can't use bb_show_usage
 * (doesn't apply for killall[5], still should be careful b/c NOFORK)
 *
 * kill %n gets translated into kill ' -<process group>' by shell (note space!)
 * This is needed to avoid collision with kill -9 ... syntax
 */

//kbuild:lib-$(CONFIG_ASH_JOB_CONTROL) += kill.o
//kbuild:lib-$(CONFIG_HUSH_KILL) += kill.o

#define SH_KILL (ENABLE_ASH_JOB_CONTROL || ENABLE_HUSH_KILL)
/* If shells want to have "kill", for ifdefs it's like ENABLE_KILL=1 */
#if SH_KILL
# undef  ENABLE_KILL
# define ENABLE_KILL 1
#endif
#define KILL_APPLET_CNT (ENABLE_KILL + ENABLE_KILLALL + ENABLE_KILLALL5)

int kill_main(int argc UNUSED_PARAM, char **argv)
{
	char *arg;
	pid_t pid;
	int signo = SIGTERM, errors = 0;
#if ENABLE_KILL || ENABLE_KILLALL
	int quiet = 0;
#endif

#if KILL_APPLET_CNT == 1
# define is_killall  ENABLE_KILLALL
# define is_killall5 ENABLE_KILLALL5
#else
/* How to determine who we are? find 3rd char from the end:
 * kill, killall, killall5
 *  ^i       ^a        ^l  - it's unique
 * (checking from the start is complicated by /bin/kill... case) */
	const char char3 = argv[0][strlen(argv[0]) - 3];
# define is_killall  (ENABLE_KILLALL  && char3 == 'a')
# define is_killall5 (ENABLE_KILLALL5 && char3 == 'l')
#endif

	/* Parse any options */
	arg = *++argv;

	if (!arg || arg[0] != '-') {
		goto do_it_now;
	}

	/* The -l option, which prints out signal names.
	 * Intended usage in shell:
	 * echo "Died of SIG`kill -l $?`"
	 * We try to mimic what kill from coreutils-6.8 does */
	if (arg[1] == 'l' && arg[2] == '\0') {
		arg = *++argv;
		if (!arg) {
			/* Print the whole signal list */
			print_signames();
			return 0;
		}
		/* -l <sig list> */
		do {
			if (isdigit(arg[0])) {
				signo = bb_strtou(arg, NULL, 10);
				if (errno) {
					bb_error_msg("unknown signal '%s'", arg);
					return EXIT_FAILURE;
				}
				/* Exitcodes >= 0x80 are to be treated
				 * as "killed by signal (exitcode & 0x7f)" */
				puts(get_signame(signo & 0x7f));
				/* TODO: 'bad' signal# - coreutils says:
				 * kill: 127: invalid signal
				 * we just print "127" instead */
			} else {
				signo = get_signum(arg);
				if (signo < 0) {
					bb_error_msg("unknown signal '%s'", arg);
					return EXIT_FAILURE;
				}
				printf("%d\n", signo);
			}
			arg = *++argv;
		} while (arg);
		return EXIT_SUCCESS;
	}

	/* The -q quiet option */
	if (is_killall && arg[1] == 'q' && arg[2] == '\0') {
#if ENABLE_KILL || ENABLE_KILLALL
		quiet = 1;
#endif
		arg = *++argv;
		if (!arg)
			bb_show_usage();
		if (arg[0] != '-')
			goto do_it_now;
	}

	arg++; /* skip '-' */

	/* -o PID? (if present, it always is at the end of command line) */
	if (is_killall5 && arg[0] == 'o')
		goto do_it_now;

	/* "--" separates options from args. Testcase: "kill -- -123" */
	if (!is_killall5 && arg[0] == '-' && arg[1] == '\0')
		goto do_it_sooner;

	if (argv[1] && arg[0] == 's' && arg[1] == '\0') { /* -s SIG? */
		arg = *++argv;
	} /* else it must be -SIG */
	signo = get_signum(arg);
	if (signo < 0) {
		bb_error_msg("bad signal name '%s'", arg);
		return EXIT_FAILURE;
	}
 do_it_sooner:
	arg = *++argv;

 do_it_now:
	pid = getpid();

	if (is_killall5) {
		pid_t sid;
		procps_status_t* p = NULL;
		/* compat: exitcode 2 is "no one was signaled" */
		errors = 2;

		/* Find out our session id */
		sid = getsid(pid);
		/* Stop all processes */
		if (signo != SIGSTOP && signo != SIGCONT)
			kill(-1, SIGSTOP);
		/* Signal all processes except those in our session */
		while ((p = procps_scan(p, PSSCAN_PID|PSSCAN_SID)) != NULL) {
			char **args;

			if (p->sid == (unsigned)sid
			 || p->sid == 0 /* compat: kernel thread, don't signal it */
			 || p->pid == (unsigned)pid
			 || p->pid == 1
			) {
				continue;
			}

			/* All remaining args must be -o PID options.
			 * Check p->pid against them. */
			args = argv;
			while (*args) {
				pid_t omit;

				arg = *args++;
				if (arg[0] != '-' || arg[1] != 'o') {
					bb_error_msg("bad option '%s'", arg);
					errors = 1;
					goto resume;
				}
				arg += 2;
				if (!arg[0] && *args)
					arg = *args++;
				omit = bb_strtoi(arg, NULL, 10);
				if (errno) {
					bb_error_msg("invalid number '%s'", arg);
					errors = 1;
					goto resume;
				}
				if (p->pid == omit)
					goto dont_kill;
			}
			kill(p->pid, signo);
			errors = 0;
 dont_kill: ;
		}
 resume:
		/* And let them continue */
		if (signo != SIGSTOP && signo != SIGCONT)
			kill(-1, SIGCONT);
		return errors;
	}

#if ENABLE_KILL || ENABLE_KILLALL
	/* Pid or name is required for kill/killall */
	if (!arg) {
		bb_error_msg("you need to specify whom to kill");
		return EXIT_FAILURE;
	}

	if (!ENABLE_KILL || is_killall) {
		/* Looks like they want to do a killall.  Do that */
		do {
			pid_t* pidList;

			pidList = find_pid_by_name(arg);
			if (*pidList == 0) {
				errors++;
				if (!quiet)
					bb_error_msg("%s: no process killed", arg);
			} else {
				pid_t *pl;

				for (pl = pidList; *pl; pl++) {
					if (*pl == pid)
						continue;
					if (kill(*pl, signo) == 0)
						continue;
					errors++;
					if (!quiet)
						bb_perror_msg("can't kill pid %d", (int)*pl);
				}
			}
			free(pidList);
			arg = *++argv;
		} while (arg);
		return errors;
	}
#endif

#if ENABLE_KILL
	/* Looks like they want to do a kill. Do that */
	while (arg) {
# if SH_KILL
		/*
		 * We need to support shell's "hack formats" of
		 * " -PRGP_ID" (yes, with a leading space)
		 * and " PID1 PID2 PID3" (with degenerate case "")
		 */
		while (*arg != '\0') {
			char *end;
			if (*arg == ' ')
				arg++;
			pid = bb_strtoi(arg, &end, 10);
			if (errno && (errno != EINVAL || *end != ' ')) {
				bb_error_msg("invalid number '%s'", arg);
				errors++;
				break;
			}
			if (kill(pid, signo) != 0) {
				bb_perror_msg("can't kill pid %d", (int)pid);
				errors++;
			}
			arg = end; /* can only point to ' ' or '\0' now */
		}
# else /* ENABLE_KILL but !SH_KILL */
		pid = bb_strtoi(arg, NULL, 10);
		if (errno) {
			bb_error_msg("invalid number '%s'", arg);
			errors++;
		} else if (kill(pid, signo) != 0) {
			bb_perror_msg("can't kill pid %d", (int)pid);
			errors++;
		}
# endif
		arg = *++argv;
	}
	return errors;
#endif
}
