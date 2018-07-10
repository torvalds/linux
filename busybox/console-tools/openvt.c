/* vi: set sw=4 ts=4: */
/*
 *  openvt.c - open a vt to run a command.
 *
 *  busyboxed by Quy Tonthat <quy@signal3.com>
 *  hacked by Tito <farmatito@tiscali.it>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config OPENVT
//config:	bool "openvt (7 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	This program is used to start a command on an unused
//config:	virtual terminal.

//applet:IF_OPENVT(APPLET(openvt, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_OPENVT) += openvt.o

//usage:#define openvt_trivial_usage
//usage:       "[-c N] [-sw] [PROG ARGS]"
//usage:#define openvt_full_usage "\n\n"
//usage:       "Start PROG on a new virtual terminal\n"
//usage:     "\n	-c N	Use specified VT"
//usage:     "\n	-s	Switch to the VT"
/* //usage:     "\n	-l	Run PROG as login shell (by prepending '-')" */
//usage:     "\n	-w	Wait for PROG to exit"
//usage:
//usage:#define openvt_example_usage
//usage:       "openvt 2 /bin/ash\n"

#include <linux/vt.h>
#include "libbb.h"

/* "Standard" openvt's man page (we do not support all of this):

openvt [-c NUM] [-fsulv] [--] [command [args]]

Find the first available VT, and run command on it. Stdio is directed
to that VT. If no command is specified then $SHELL is used.

-c NUM
    Use the given VT number, not the first free one.
-f
    Force opening a VT: don't try to check if VT is already in use.
-s
    Switch to the new VT when starting the command.
    The VT of the new command will be made the new current VT.
-u
    Figure out the owner of the current VT, and run login as that user.
    Suitable to be called by init. Shouldn't be used with -c or -l.
-l
    Make the command a login shell: a "-" is prepended to the argv[0]
    when command is executed.
-v
    Verbose.
-w
    Wait for command to complete. If -w and -s are used together,
    switch back to the controlling terminal when the command completes.

bbox:
-u: not implemented
-f: always in effect
-l: not implemented, ignored
-v: ignored
-ws: does NOT switch back
*/

/* Helper: does this fd understand VT_xxx? */
static int not_vt_fd(int fd)
{
	struct vt_stat vtstat;
	return ioctl(fd, VT_GETSTATE, &vtstat); /* !0: error, it's not VT fd */
}

/* Helper: get a fd suitable for VT_xxx */
static int get_vt_fd(void)
{
	int fd;

	/* Do we, by chance, already have it? */
	for (fd = 0; fd < 3; fd++)
		if (!not_vt_fd(fd))
			return fd;
	fd = open(DEV_CONSOLE, O_RDONLY | O_NONBLOCK);
	if (fd >= 0 && !not_vt_fd(fd))
		return fd;
	bb_error_msg_and_die("can't find open VT");
}

static int find_free_vtno(void)
{
	int vtno;
	int fd = get_vt_fd();

	errno = 0;
	/*xfunc_error_retval = 3; - do we need compat? */
	if (ioctl(fd, VT_OPENQRY, &vtno) != 0 || vtno <= 0)
		bb_perror_msg_and_die("can't find open VT");
// Not really needed, grep for DAEMON_CLOSE_EXTRA_FDS
//	if (fd > 2)
//		close(fd);
	return vtno;
}

/* vfork scares gcc, it generates bigger code.
 * Keep it away from main program.
 * TODO: move to libbb; or adapt existing libbb's spawn().
 */
static NOINLINE void vfork_child(char **argv)
{
	if (vfork() == 0) {
		/* CHILD */
		/* Try to make this VT our controlling tty */
		setsid(); /* lose old ctty */
		ioctl(STDIN_FILENO, TIOCSCTTY, 0 /* 0: don't forcibly steal */);
		//bb_error_msg("our sid %d", getsid(0));
		//bb_error_msg("our pgrp %d", getpgrp());
		//bb_error_msg("VT's sid %d", tcgetsid(0));
		//bb_error_msg("VT's pgrp %d", tcgetpgrp(0));
		BB_EXECVP_or_die(argv);
	}
}

int openvt_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int openvt_main(int argc UNUSED_PARAM, char **argv)
{
	char vtname[sizeof(VC_FORMAT) + sizeof(int)*3];
	struct vt_stat vtstat;
	char *str_c;
	int vtno;
	int flags;
	enum {
		OPT_c = (1 << 0),
		OPT_w = (1 << 1),
		OPT_s = (1 << 2),
		OPT_l = (1 << 3),
		OPT_f = (1 << 4),
		OPT_v = (1 << 5),
	};

	/* "+" - stop on first non-option */
	flags = getopt32(argv, "+c:wslfv", &str_c);
	argv += optind;

	if (flags & OPT_c) {
		/* Check for illegal vt number: < 1 or > 63 */
		vtno = xatou_range(str_c, 1, 63);
	} else {
		vtno = find_free_vtno();
	}

	/* Grab new VT */
	sprintf(vtname, VC_FORMAT, vtno);
	/* (Try to) clean up stray open fds above fd 2 */
	bb_daemon_helper(DAEMON_CLOSE_EXTRA_FDS);
	close(STDIN_FILENO);
	/*setsid(); - BAD IDEA: after we exit, child is SIGHUPed... */
	xopen(vtname, O_RDWR);
	xioctl(STDIN_FILENO, VT_GETSTATE, &vtstat);

	if (flags & OPT_s) {
		console_make_active(STDIN_FILENO, vtno);
	}

	if (!argv[0]) {
		argv--;
		argv[0] = (char *) get_shell_name();
		/*argv[1] = NULL; - already is */
	}

	xdup2(STDIN_FILENO, STDOUT_FILENO);
	xdup2(STDIN_FILENO, STDERR_FILENO);

#ifdef BLOAT
	{
	/* Handle -l (login shell) option */
	const char *prog = argv[0];
	if (flags & OPT_l)
		argv[0] = xasprintf("-%s", argv[0]);
	}
#endif

	vfork_child(argv);
	if (flags & OPT_w) {
		/* We have only one child, wait for it */
		safe_waitpid(-1, NULL, 0); /* loops on EINTR */
		if (flags & OPT_s) {
			console_make_active(STDIN_FILENO, vtstat.v_active);
			// Compat: even with -c N (try to) disallocate:
			// # /usr/app/kbd-1.12/bin/openvt -f -c 9 -ws sleep 5
			// openvt: could not deallocate console 9
			xioctl(STDIN_FILENO, VT_DISALLOCATE, (void*)(ptrdiff_t)vtno);
		}
	}
	return EXIT_SUCCESS;
}
