/* vi: set sw=4 ts=4: */
/*
 * Rexec program for system have fork() as vfork() with foreground option
 *
 * Copyright (C) Vladimir N. Oleynik <dzo@simtreas.ru>
 * Copyright (C) 2003 Russ Dill <Russ.Dill@asu.edu>
 *
 * daemon() portion taken from uClibc:
 *
 * Copyright (c) 1991, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Modified for uClibc by Erik Andersen <andersee@debian.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "busybox.h" /* uses applet tables */
#include "NUM_APPLETS.h"

#define NOFORK_SUPPORT ((NUM_APPLETS > 1) && (ENABLE_FEATURE_PREFER_APPLETS || ENABLE_FEATURE_SH_NOFORK))
#define NOEXEC_SUPPORT ((NUM_APPLETS > 1) && (ENABLE_FEATURE_PREFER_APPLETS || ENABLE_FEATURE_SH_STANDALONE))

#if defined(__linux__) && (NUM_APPLETS > 1)
# include <sys/prctl.h>
# ifndef PR_SET_NAME
# define PR_SET_NAME 15
# endif
# ifndef PR_GET_NAME
# define PR_GET_NAME 16
# endif
void FAST_FUNC set_task_comm(const char *comm)
{
	/* okay if too long (truncates) */
	prctl(PR_SET_NAME, (long)comm, 0, 0, 0);
}
#endif

/*
 * NOFORK/NOEXEC support
 */
#if NOFORK_SUPPORT
static jmp_buf die_jmp;
static void jump(void)
{
	/* Special case. We arrive here if NOFORK applet
	 * calls xfunc, which then decides to die.
	 * We don't die, but instead jump back to caller.
	 * NOFORK applets still cannot carelessly call xfuncs:
	 * p = xmalloc(10);
	 * q = xmalloc(10); // BUG! if this dies, we leak p!
	 */
	/* | 0x100 allows to pass zero exitcode (longjmp can't pass 0).
	 * This works because exitcodes are bytes,
	 * run_nofork_applet() ensures that by "& 0xff"
	 */
	longjmp(die_jmp, xfunc_error_retval | 0x100);
}

struct nofork_save_area {
	jmp_buf die_jmp;
	void (*die_func)(void);
	const char *applet_name;
	uint32_t option_mask32;
	smallint logmode;
	uint8_t xfunc_error_retval;
};
static void save_nofork_data(struct nofork_save_area *save)
{
	memcpy(&save->die_jmp, &die_jmp, sizeof(die_jmp));
	save->die_func = die_func;
	save->applet_name = applet_name;
	save->option_mask32 = option_mask32;
	save->logmode = logmode;
	save->xfunc_error_retval = xfunc_error_retval;
}
static void restore_nofork_data(struct nofork_save_area *save)
{
	memcpy(&die_jmp, &save->die_jmp, sizeof(die_jmp));
	die_func = save->die_func;
	applet_name = save->applet_name;
	option_mask32 = save->option_mask32;
	logmode = save->logmode;
	xfunc_error_retval = save->xfunc_error_retval;
}

int FAST_FUNC run_nofork_applet(int applet_no, char **argv)
{
	int rc, argc;
	struct nofork_save_area old;

	save_nofork_data(&old);

	logmode = LOGMODE_STDIO;
	xfunc_error_retval = EXIT_FAILURE;
	/* In case getopt() was already called:
	 * reset the libc getopt() function, which keeps internal state.
	 * (getopt32() does it itself, but getopt() doesn't (and can't))
	 */
	GETOPT_RESET();

	argc = string_array_len(argv);

	/* If xfunc "dies" in NOFORK applet, die_func longjmp's here instead */
	die_func = jump;
	rc = setjmp(die_jmp);
	if (!rc) {
		/* Some callers (xargs)
		 * need argv untouched because they free argv[i]! */
		char *tmp_argv[argc+1];
		memcpy(tmp_argv, argv, (argc+1) * sizeof(tmp_argv[0]));
		applet_name = tmp_argv[0];
		/* Finally we can call NOFORK applet's main() */
		rc = applet_main[applet_no](argc, tmp_argv);
		/* Important for shells: `which CMD` was failing */
		fflush_all();
	} else {
		/* xfunc died in NOFORK applet */
	}

	/* Restoring some globals */
	restore_nofork_data(&old);
	/* Other globals can be simply reset to defaults */
	GETOPT_RESET();

	return rc & 0xff; /* don't confuse people with "exitcodes" >255 */
}
#endif

#if NOEXEC_SUPPORT
void FAST_FUNC run_noexec_applet_and_exit(int a, const char *name, char **argv)
{
	/* reset some state and run without execing */
	/* msg_eol = "\n"; - no caller needs this reinited yet */
	logmode = LOGMODE_STDIO;
	xfunc_error_retval = EXIT_FAILURE;
	die_func = NULL;
	GETOPT_RESET();

//TODO: think pidof, pgrep, pkill!
//set_task_comm() makes our pidof find NOEXECs (e.g. "yes >/dev/null"),
//but one from procps-ng-3.3.10 needs more!
//Rewrite /proc/PID/cmdline? (need to save argv0 and length at init for this to work!)
	set_task_comm(name);
	/* applet_name is set by this function: */
	run_applet_no_and_exit(a, name, argv);
}
#endif

/*
 * Higher-level code, hiding optional NOFORK/NOEXEC trickery.
 */

/* This does a fork/exec in one call, using vfork().  Returns PID of new child,
 * -1 for failure.  Runs argv[0], searching path if that has no / in it. */
pid_t FAST_FUNC spawn(char **argv)
{
	/* Compiler should not optimize stores here */
	volatile int failed;
	pid_t pid;

	fflush_all();

	/* Be nice to nommu machines. */
	failed = 0;
	pid = vfork();
	if (pid < 0) /* error */
		return pid;
	if (!pid) { /* child */
		/* This macro is ok - it doesn't do NOEXEC/NOFORK tricks */
		BB_EXECVP(argv[0], argv);

		/* We are (maybe) sharing a stack with blocked parent,
		 * let parent know we failed and then exit to unblock parent
		 * (but don't run atexit() stuff, which would screw up parent.)
		 */
		failed = errno;
		/* mount, for example, does not want the message */
		/*bb_perror_msg("can't execute '%s'", argv[0]);*/
		_exit(111);
	}
	/* parent */
	/* Unfortunately, this is not reliable: according to standards
	 * vfork() can be equivalent to fork() and we won't see value
	 * of 'failed'.
	 * Interested party can wait on pid and learn exit code.
	 * If 111 - then it (most probably) failed to exec */
	if (failed) {
		safe_waitpid(pid, NULL, 0); /* prevent zombie */
		errno = failed;
		return -1;
	}
	return pid;
}

/* Die with an error message if we can't spawn a child process. */
pid_t FAST_FUNC xspawn(char **argv)
{
	pid_t pid = spawn(argv);
	if (pid < 0)
		bb_simple_perror_msg_and_die(*argv);
	return pid;
}

int FAST_FUNC spawn_and_wait(char **argv)
{
	int rc;
#if ENABLE_FEATURE_PREFER_APPLETS && (NUM_APPLETS > 1)
	int a = find_applet_by_name(argv[0]);

	if (a >= 0) {
		if (APPLET_IS_NOFORK(a))
			return run_nofork_applet(a, argv);
# if BB_MMU /* NOEXEC needs fork(), thus this is done only on MMU machines: */
		if (APPLET_IS_NOEXEC(a)) {
			fflush_all();
			rc = fork();
			if (rc) /* parent or error */
				return wait4pid(rc);

			/* child */
			run_noexec_applet_and_exit(a, argv[0], argv);
		}
# endif
	}
#endif
	rc = spawn(argv);
	return wait4pid(rc);
}

#if !BB_MMU
void FAST_FUNC re_exec(char **argv)
{
	/* high-order bit of first char in argv[0] is a hidden
	 * "we have (already) re-execed, don't do it again" flag */
	argv[0][0] |= 0x80;
	execv(bb_busybox_exec_path, argv);
	bb_perror_msg_and_die("can't execute '%s'", bb_busybox_exec_path);
}

pid_t FAST_FUNC fork_or_rexec(char **argv)
{
	pid_t pid;
	/* Maybe we are already re-execed and come here again? */
	if (re_execed)
		return 0;

	/* fflush_all(); ? - so far all callers had no buffered output to flush */

	pid = xvfork();
	if (pid) /* parent */
		return pid;
	/* child - re-exec ourself */
	re_exec(argv);
}
#endif

/* Due to a #define in libbb.h on MMU systems we actually have 1 argument -
 * char **argv "vanishes" */
void FAST_FUNC bb_daemonize_or_rexec(int flags, char **argv)
{
	int fd;

	if (flags & DAEMON_CHDIR_ROOT)
		xchdir("/");

	if (flags & DAEMON_DEVNULL_STDIO) {
		close(0);
		close(1);
		close(2);
	}

	fd = open(bb_dev_null, O_RDWR);
	if (fd < 0) {
		/* NB: we can be called as bb_sanitize_stdio() from init
		 * or mdev, and there /dev/null may legitimately not (yet) exist!
		 * Do not use xopen above, but obtain _ANY_ open descriptor,
		 * even bogus one as below. */
		fd = xopen("/", O_RDONLY); /* don't believe this can fail */
	}

	while ((unsigned)fd < 2)
		fd = dup(fd); /* have 0,1,2 open at least to /dev/null */

	if (!(flags & DAEMON_ONLY_SANITIZE)) {

		/* fflush_all(); - add it in fork_or_rexec() if necessary */

		if (fork_or_rexec(argv))
			_exit(EXIT_SUCCESS); /* parent */
		/* if daemonizing, detach from stdio & ctty */
		setsid();
		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		if (flags & DAEMON_DOUBLE_FORK) {
			/* On Linux, session leader can acquire ctty
			 * unknowingly, by opening a tty.
			 * Prevent this: stop being a session leader.
			 */
			if (fork_or_rexec(argv))
				_exit(EXIT_SUCCESS); /* parent */
		}
	}
	while (fd > 2) {
		close(fd--);
		if (!(flags & DAEMON_CLOSE_EXTRA_FDS))
			return;
		/* else close everything after fd#2 */
	}
}

void FAST_FUNC bb_sanitize_stdio(void)
{
	bb_daemonize_or_rexec(DAEMON_ONLY_SANITIZE, NULL);
}
