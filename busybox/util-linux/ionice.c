/* vi: set sw=4 ts=4: */
/*
 * ionice implementation for busybox based on linux-utils-ng 2.14
 *
 * Copyright (C) 2008 by  <u173034@informatik.uni-oldenburg.de>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config IONICE
//config:	bool "ionice (3.6 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Set/set program io scheduling class and priority
//config:	Requires kernel >= 2.6.13

//applet:IF_IONICE(APPLET_NOEXEC(ionice, ionice, BB_DIR_BIN, BB_SUID_DROP, ionice))

//kbuild:lib-$(CONFIG_IONICE) += ionice.o

//usage:#define ionice_trivial_usage
//usage:	"[-c 1-3] [-n 0-7] [-p PID] [PROG]"
//usage:#define ionice_full_usage "\n\n"
//usage:       "Change I/O priority and class\n"
//usage:     "\n	-c	Class. 1:realtime 2:best-effort 3:idle"
//usage:     "\n	-n	Priority"

#include <sys/syscall.h>
#include <asm/unistd.h>
#include "libbb.h"

static int ioprio_set(int which, int who, int ioprio)
{
	return syscall(SYS_ioprio_set, which, who, ioprio);
}

static int ioprio_get(int which, int who)
{
	return syscall(SYS_ioprio_get, which, who);
}

enum {
	IOPRIO_WHO_PROCESS = 1,
	IOPRIO_WHO_PGRP,
	IOPRIO_WHO_USER
};

enum {
	IOPRIO_CLASS_NONE,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE
};

static const char to_prio[] ALIGN1 = "none\0realtime\0best-effort\0idle";

#define IOPRIO_CLASS_SHIFT      13

int ionice_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ionice_main(int argc UNUSED_PARAM, char **argv)
{
	/* Defaults */
	int ioclass = 0;
	int pri = 0;
	int pid = 0; /* affect own porcess */
	int opt;
	enum {
		OPT_n = 1,
		OPT_c = 2,
		OPT_p = 4,
	};

	/* Numeric params */
	/* '+': stop at first non-option */
	opt = getopt32(argv, "+n:+c:+p:+", &pri, &ioclass, &pid);
	argv += optind;

	if (opt & OPT_c) {
		if (ioclass > 3)
			bb_error_msg_and_die("bad class %d", ioclass);
// Do we need this (compat?)?
//		if (ioclass == IOPRIO_CLASS_NONE)
//			ioclass = IOPRIO_CLASS_BE;
//		if (ioclass == IOPRIO_CLASS_IDLE) {
//			//if (opt & OPT_n)
//			//	bb_error_msg("ignoring priority for idle class");
//			pri = 7;
//		}
	}

	if (!(opt & (OPT_n|OPT_c))) {
		if (!(opt & OPT_p) && *argv)
			pid = xatoi_positive(*argv);

		pri = ioprio_get(IOPRIO_WHO_PROCESS, pid);
		if (pri == -1)
			bb_perror_msg_and_die("ioprio_%cet", 'g');

		ioclass = (pri >> IOPRIO_CLASS_SHIFT) & 0x3;
		pri &= 0xff;
		printf((ioclass == IOPRIO_CLASS_IDLE) ? "%s\n" : "%s: prio %d\n",
				nth_string(to_prio, ioclass), pri);
	} else {
//printf("pri=%d class=%d val=%x\n",
//pri, ioclass, pri | (ioclass << IOPRIO_CLASS_SHIFT));
		pri |= (ioclass << IOPRIO_CLASS_SHIFT);
		if (ioprio_set(IOPRIO_WHO_PROCESS, pid, pri) == -1)
			bb_perror_msg_and_die("ioprio_%cet", 's');
		if (argv[0]) {
			BB_EXECVP_or_die(argv);
		}
	}

	return EXIT_SUCCESS;
}
