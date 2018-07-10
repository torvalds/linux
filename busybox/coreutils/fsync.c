/* vi: set sw=4 ts=4: */
/*
 * Mini fsync implementation for busybox
 *
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config FSYNC
//config:	bool "fsync (3.7 kb)"
//config:	default y
//config:	help
//config:	fsync is used to flush file-related cached blocks to disk.

//applet:IF_FSYNC(APPLET_NOFORK(fsync, fsync, BB_DIR_BIN, BB_SUID_DROP, fsync))

//kbuild:lib-$(CONFIG_FSYNC) += fsync.o

//usage:#define fsync_trivial_usage
//usage:       "[-d] FILE..."
//usage:#define fsync_full_usage "\n\n"
//usage:       "Write files' buffered blocks to disk\n"
//usage:     "\n	-d	Avoid syncing metadata"

#include "libbb.h"
#ifndef O_NOATIME
# define O_NOATIME 0
#endif

/* This is a NOFORK applet. Be very careful! */

int fsync_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int fsync_main(int argc UNUSED_PARAM, char **argv)
{
	int status;
	int opts;

	opts = getopt32(argv, "d"); /* fdatasync */
	argv += optind;
	if (!*argv) {
		bb_show_usage();
	}

	status = EXIT_SUCCESS;
	do {
		int fd = open_or_warn(*argv, O_NOATIME | O_NOCTTY | O_RDONLY);

		if (fd == -1) {
			status = EXIT_FAILURE;
			continue;
		}
		if ((opts ? fdatasync(fd) : fsync(fd))) {
			//status = EXIT_FAILURE; - do we want this?
			bb_simple_perror_msg(*argv);
		}
		close(fd);
	} while (*++argv);

	return status;
}
