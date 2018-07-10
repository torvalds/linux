/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2017 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config FSFREEZE
//config:	bool "fsfreeze (3.6 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	select LONG_OPTS
//config:	help
//config:	Halt new accesses and flush writes on a mounted filesystem.

//applet:IF_FSFREEZE(APPLET_NOEXEC(fsfreeze, fsfreeze, BB_DIR_USR_SBIN, BB_SUID_DROP, fsfreeze))

//kbuild:lib-$(CONFIG_FSFREEZE) += fsfreeze.o

//usage:#define fsfreeze_trivial_usage
//usage:       "--[un]freeze MOUNTPOINT"
//usage:#define fsfreeze_full_usage "\n\n"
//usage:	"Flush and halt writes to MOUNTPOINT"

#include "libbb.h"
#include <linux/fs.h>

#ifndef FIFREEZE
# define FIFREEZE _IOWR('X', 119, int)
# define FITHAW   _IOWR('X', 120, int)
#endif

int fsfreeze_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int fsfreeze_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned opts;
	int fd;

	/* exactly one non-option arg: the mountpoint */
	/* one of opts is required */
	/* opts are mutually exclusive */
	opts = getopt32long(argv, "^"
		"" /* no opts */
		"\0" "=1:""\xff:\xfe:""\xff--\xfe:\xfe--\xff",
		"freeze\0"   No_argument "\xff"
		"unfreeze\0" No_argument "\xfe"
	);

	fd = xopen(argv[optind], O_RDONLY);
	/* Works with NULL arg on linux-4.8.0 */
	xioctl(fd, (opts & 1) ? FIFREEZE : FITHAW, NULL);

	return EXIT_SUCCESS;
}
