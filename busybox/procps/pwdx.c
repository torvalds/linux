/* vi: set sw=4 ts=4: */
/*
 * pwdx implementation for busybox
 *
 * Copyright (c) 2004 Nicholas Miell
 * ported from procps by Pere Orga <gotrunks@gmail.com> 2011
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config PWDX
//config:	bool "pwdx (3.5 kb)"
//config:	default y
//config:	help
//config:	Report current working directory of a process

//applet:IF_PWDX(APPLET_NOFORK(pwdx, pwdx, BB_DIR_USR_BIN, BB_SUID_DROP, pwdx))

//kbuild:lib-$(CONFIG_PWDX) += pwdx.o

//usage:#define pwdx_trivial_usage
//usage:       "PID..."
//usage:#define pwdx_full_usage "\n\n"
//usage:       "Show current directory for PIDs"

#include "libbb.h"

int pwdx_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int pwdx_main(int argc UNUSED_PARAM, char **argv)
{
	getopt32(argv, "^" "" "\0" "-1");
	argv += optind;

	do {
		char buf[sizeof("/proc/%u/cwd") + sizeof(int)*3];
		unsigned pid;
		char *s;
		char *arg = *argv;

		// Allowed on the command line:
		// /proc/NUM
		// NUM
		if (is_prefixed_with(arg, "/proc/"))
			arg += 6;

		pid = bb_strtou(arg, NULL, 10);
		if (errno)
			bb_error_msg_and_die("invalid process id: '%s'", arg);

		sprintf(buf, "/proc/%u/cwd", pid);

		/* NOFORK: only one alloc is allowed; must free */
		s = xmalloc_readlink(buf);
		// "pwdx /proc/1" says "/proc/1: DIR", not "1: DIR"
		printf("%s: %s\n", *argv, s ? s : strerror(errno == ENOENT ? ESRCH : errno));
		free(s);
	} while (*++argv);

	return EXIT_SUCCESS;
}
