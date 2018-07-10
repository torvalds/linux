/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2017 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config PARTPROBE
//config:	bool "partprobe (3.6 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Ask kernel to rescan partition table.

//applet:IF_PARTPROBE(APPLET_NOEXEC(partprobe, partprobe, BB_DIR_USR_SBIN, BB_SUID_DROP, partprobe))

//kbuild:lib-$(CONFIG_PARTPROBE) += partprobe.o

#include <linux/fs.h>
#include "libbb.h"
#ifndef BLKRRPART
# define BLKRRPART _IO(0x12,95)
#endif

//usage:#define partprobe_trivial_usage
//usage:	"DEVICE..."
//usage:#define partprobe_full_usage "\n\n"
//usage:	"Ask kernel to rescan partition table"
//
// partprobe (GNU parted) 3.2:
// -d, --dry-run	Don't update the kernel
// -s, --summary	Show a summary of devices and their partitions

int partprobe_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int partprobe_main(int argc UNUSED_PARAM, char **argv)
{
	getopt32(argv, "");
	argv += optind;

	/* "partprobe" with no arguments just does nothing */

	while (*argv) {
		int fd = xopen(*argv, O_RDONLY);
		/*
		 * Newer versions of parted scan partition tables themselves and
		 * use BLKPG ioctl (BLKPG_DEL_PARTITION / BLKPG_ADD_PARTITION)
		 * since this way kernel does not need to know
		 * partition table formats.
		 * We use good old BLKRRPART:
		 */
		ioctl_or_perror_and_die(fd, BLKRRPART, NULL, "%s", *argv);
		close(fd);
		argv++;
	}

	return EXIT_SUCCESS;
}
