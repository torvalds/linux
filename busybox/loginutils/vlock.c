/* vi: set sw=4 ts=4: */
/*
 * vlock implementation for busybox
 *
 * Copyright (C) 2000 by spoon <spoon@ix.netcom.com>
 * Written by spoon <spon@ix.netcom.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

/* Shoutz to Michael K. Johnson <johnsonm@redhat.com>, author of the
 * original vlock.  I snagged a bunch of his code to write this
 * minimalistic vlock.
 */
/* Fixed by Erik Andersen to do passwords the tinylogin way...
 * It now works with md5, sha1, etc passwords.
 */
//config:config VLOCK
//config:	bool "vlock (17 kb)"
//config:	default y
//config:	help
//config:	Build the "vlock" applet which allows you to lock (virtual) terminals.
//config:
//config:	Note that busybox binary must be setuid root for this applet to
//config:	work properly.

//applet:/* Needs to be run by root or be suid root - needs to change uid and gid: */
//applet:IF_VLOCK(APPLET(vlock, BB_DIR_USR_BIN, BB_SUID_REQUIRE))

//kbuild:lib-$(CONFIG_VLOCK) += vlock.o

//usage:#define vlock_trivial_usage
//usage:       "[-a]"
//usage:#define vlock_full_usage "\n\n"
//usage:       "Lock a virtual terminal. A password is required to unlock.\n"
//usage:     "\n	-a	Lock all VTs"

#include "libbb.h"

#ifdef __linux__
#include <sys/vt.h>

static void release_vt(int signo UNUSED_PARAM)
{
	/* If -a, param is 0, which means:
	 * "no, kernel, we don't allow console switch away from us!" */
	ioctl(STDIN_FILENO, VT_RELDISP, (unsigned long) !option_mask32);
}

static void acquire_vt(int signo UNUSED_PARAM)
{
	/* ACK to kernel that switch to console is successful */
	ioctl(STDIN_FILENO, VT_RELDISP, VT_ACKACQ);
}
#endif

int vlock_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int vlock_main(int argc UNUSED_PARAM, char **argv)
{
#ifdef __linux__
	struct vt_mode vtm;
	struct vt_mode ovtm;
#endif
	struct termios term;
	struct termios oterm;
	struct passwd *pw;

	pw = xgetpwuid(getuid());
	getopt32(argv, "^" "a" "\0" "=0"/* no args!*/);

	/* Ignore some signals so that we don't get killed by them */
	bb_signals(0
		+ (1 << SIGTSTP)
		+ (1 << SIGTTIN)
		+ (1 << SIGTTOU)
		+ (1 << SIGHUP )
		+ (1 << SIGCHLD) /* paranoia :) */
		+ (1 << SIGQUIT)
		+ (1 << SIGINT )
		, SIG_IGN);

#ifdef __linux__
	/* We will use SIGUSRx for console switch control: */
	/* 1: set handlers */
	signal_SA_RESTART_empty_mask(SIGUSR1, release_vt);
	signal_SA_RESTART_empty_mask(SIGUSR2, acquire_vt);
	/* 2: unmask them */
	sig_unblock(SIGUSR1);
	sig_unblock(SIGUSR2);
#endif

	/* Revert stdin/out to our controlling tty
	 * (or die if we have none) */
	xmove_fd(xopen(CURRENT_TTY, O_RDWR), STDIN_FILENO);
	xdup2(STDIN_FILENO, STDOUT_FILENO);

#ifdef __linux__
	xioctl(STDIN_FILENO, VT_GETMODE, &vtm);
	ovtm = vtm;
	/* "console switches are controlled by us, not kernel!" */
	vtm.mode = VT_PROCESS;
	vtm.relsig = SIGUSR1;
	vtm.acqsig = SIGUSR2;
	ioctl(STDIN_FILENO, VT_SETMODE, &vtm);
#endif

//TODO: use set_termios_to_raw()
	tcgetattr(STDIN_FILENO, &oterm);
	term = oterm;
	term.c_iflag |= IGNBRK; /* ignore serial break (why? VTs don't have breaks, right?) */
	term.c_iflag &= ~BRKINT; /* redundant? "dont translate break to SIGINT" */
	term.c_lflag &= ~(ISIG | ECHO | ECHOCTL); /* ignore ^C ^Z, echo off */
	tcsetattr_stdin_TCSANOW(&term);

	while (1) {
		printf("Virtual console%s locked by %s.\n",
				/* "s" if -a, else "": */ "s" + !option_mask32,
				pw->pw_name
		);
		if (ask_and_check_password(pw) > 0) {
			break;
		}
		bb_do_delay(LOGIN_FAIL_DELAY);
		puts("Incorrect password");
	}

#ifdef __linux__
	ioctl(STDIN_FILENO, VT_SETMODE, &ovtm);
#endif
	tcsetattr_stdin_TCSANOW(&oterm);
	fflush_stdout_and_exit(EXIT_SUCCESS);
}
