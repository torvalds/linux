/*
 * Copyright (C) 2010 Timo Teras <timo.teras@iki.fi>
 *
 * This is free software, licensed under the GNU General Public License v2.
 */
//config:config FLOCK
//config:	bool "flock (6.1 kb)"
//config:	default y
//config:	help
//config:	Manage locks from shell scripts

//applet:IF_FLOCK(APPLET(flock, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_FLOCK) += flock.o

//usage:#define flock_trivial_usage
//usage:       "[-sxun] FD|{FILE [-c] PROG ARGS}"
//usage:#define flock_full_usage "\n\n"
//usage:       "[Un]lock file descriptor, or lock FILE, run PROG\n"
//usage:     "\n	-s	Shared lock"
//usage:     "\n	-x	Exclusive lock (default)"
//usage:     "\n	-u	Unlock FD"
//usage:     "\n	-n	Fail rather than wait"

#include <sys/file.h>
#include "libbb.h"

int flock_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int flock_main(int argc UNUSED_PARAM, char **argv)
{
	int mode, opt, fd;
	enum {
		OPT_s = (1 << 0),
		OPT_x = (1 << 1),
		OPT_n = (1 << 2),
		OPT_u = (1 << 3),
		OPT_c = (1 << 4),
	};

#if ENABLE_LONG_OPTS
	static const char flock_longopts[] ALIGN1 =
		"shared\0"      No_argument       "s"
		"exclusive\0"   No_argument       "x"
		"unlock\0"      No_argument       "u"
		"nonblock\0"    No_argument       "n"
		;
#endif

	opt = getopt32long(argv, "^+" "sxnu" "\0" "-1", flock_longopts);
	argv += optind;

	if (argv[1]) {
		fd = open(argv[0], O_RDONLY|O_NOCTTY|O_CREAT, 0666);
		if (fd < 0 && errno == EISDIR)
			fd = open(argv[0], O_RDONLY|O_NOCTTY);
		if (fd < 0)
			bb_perror_msg_and_die("can't open '%s'", argv[0]);
		//TODO? close_on_exec_on(fd);
	} else {
		fd = xatoi_positive(argv[0]);
	}
	argv++;

	/* If it is "flock FILE -c PROG", then -c isn't caught by getopt32:
	 * we use "+" in order to support "flock -opt FILE PROG -with-opts",
	 * we need to remove -c by hand.
	 */
	if (argv[0]
	 && argv[0][0] == '-'
	 && (  (argv[0][1] == 'c' && !argv[0][2])
	    || (ENABLE_LONG_OPTS && strcmp(argv[0] + 1, "-command") == 0)
	    )
	) {
		argv++;
		if (argv[1])
			bb_error_msg_and_die("-c takes only one argument");
		opt |= OPT_c;
	}

	if (OPT_s == LOCK_SH && OPT_x == LOCK_EX && OPT_n == LOCK_NB && OPT_u == LOCK_UN) {
		/* With suitably matched constants, mode setting is much simpler */
		mode = opt & (LOCK_SH + LOCK_EX + LOCK_NB + LOCK_UN);
		if (!(mode & ~LOCK_NB))
			mode |= LOCK_EX;
	} else {
		if (opt & OPT_u)
			mode = LOCK_UN;
		else if (opt & OPT_s)
			mode = LOCK_SH;
		else
			mode = LOCK_EX;
		if (opt & OPT_n)
			mode |= LOCK_NB;
	}

	if (flock(fd, mode) != 0) {
		if (errno == EWOULDBLOCK)
			return EXIT_FAILURE;
		bb_perror_nomsg_and_die();
	}

	if (argv[0]) {
		int rc;
		if (opt & OPT_c) {
			/* -c 'PROG ARGS' means "run sh -c 'PROG ARGS'" */
			argv -= 2;
			argv[0] = (char*)get_shell_name();
			argv[1] = (char*)"-c";
			/* argv[2] = "PROG ARGS"; */
			/* argv[3] = NULL; */
		}
		rc = spawn_and_wait(argv);
		if (rc < 0)
			bb_simple_perror_msg(argv[0]);
		return rc;
	}

	return EXIT_SUCCESS;
}
