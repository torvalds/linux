/* vi: set sw=4 ts=4: */
/*
 * readahead implementation for busybox
 *
 * Preloads the given files in RAM, to reduce access time.
 * Does this by calling the readahead(2) system call.
 *
 * Copyright (C) 2006  Michael Opdenacker <michael@free-electrons.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config READAHEAD
//config:	bool "readahead (2 kb)"
//config:	default y
//config:	depends on LFS
//config:	select PLATFORM_LINUX
//config:	help
//config:	Preload the files listed on the command line into RAM cache so that
//config:	subsequent reads on these files will not block on disk I/O.
//config:
//config:	This applet just calls the readahead(2) system call on each file.
//config:	It is mainly useful in system startup scripts to preload files
//config:	or executables before they are used. When used at the right time
//config:	(in particular when a CPU bound process is running) it can
//config:	significantly speed up system startup.
//config:
//config:	As readahead(2) blocks until each file has been read, it is best to
//config:	run this applet as a background job.

//applet:IF_READAHEAD(APPLET(readahead, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_READAHEAD) += readahead.o

//usage:#define readahead_trivial_usage
//usage:       "[FILE]..."
//usage:#define readahead_full_usage "\n\n"
//usage:       "Preload FILEs to RAM"

#include "libbb.h"

int readahead_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int readahead_main(int argc UNUSED_PARAM, char **argv)
{
	int retval = EXIT_SUCCESS;

	if (!argv[1]) {
		bb_show_usage();
	}

	while (*++argv) {
		int fd = open_or_warn(*argv, O_RDONLY);
		if (fd >= 0) {
			off_t len;
			int r;

			/* fdlength was reported to be unreliable - use seek */
			len = xlseek(fd, 0, SEEK_END);
			xlseek(fd, 0, SEEK_SET);
			r = readahead(fd, 0, len);
			close(fd);
			if (r >= 0)
				continue;
		}
		retval = EXIT_FAILURE;
	}

	return retval;
}
