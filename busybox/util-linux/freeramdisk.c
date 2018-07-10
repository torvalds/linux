/* vi: set sw=4 ts=4: */
/*
 * freeramdisk and fdflush implementations for busybox
 *
 * Copyright (C) 2000 and written by Emanuele Caratti <wiz@iol.it>
 * Adjusted a bit by Erik Andersen <andersen@codepoet.org>
 * Unified with fdflush by Tito Ragusa <farmatito@tiscali.it>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config FDFLUSH
//config:	bool "fdflush (1.4 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	fdflush is only needed when changing media on slightly-broken
//config:	removable media drives. It is used to make Linux believe that a
//config:	hardware disk-change switch has been actuated, which causes Linux to
//config:	forget anything it has cached from the previous media. If you have
//config:	such a slightly-broken drive, you will need to run fdflush every time
//config:	you change a disk. Most people have working hardware and can safely
//config:	leave this disabled.
//config:
//config:config FREERAMDISK
//config:	bool "freeramdisk (1.4 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Linux allows you to create ramdisks. This utility allows you to
//config:	delete them and completely free all memory that was used for the
//config:	ramdisk. For example, if you boot Linux into a ramdisk and later
//config:	pivot_root, you may want to free the memory that is allocated to the
//config:	ramdisk. If you have no use for freeing memory from a ramdisk, leave
//config:	this disabled.

//                     APPLET_ODDNAME:name         main         location     suid_type     help
//applet:IF_FDFLUSH(   APPLET_ODDNAME(fdflush,     freeramdisk, BB_DIR_BIN,  BB_SUID_DROP, fdflush    ))
//applet:IF_FREERAMDISK(APPLET_NOEXEC(freeramdisk, freeramdisk, BB_DIR_SBIN, BB_SUID_DROP, freeramdisk))

//kbuild:lib-$(CONFIG_FDFLUSH) += freeramdisk.o
//kbuild:lib-$(CONFIG_FREERAMDISK) += freeramdisk.o

//usage:#define freeramdisk_trivial_usage
//usage:       "DEVICE"
//usage:#define freeramdisk_full_usage "\n\n"
//usage:       "Free all memory used by the specified ramdisk"
//usage:
//usage:#define freeramdisk_example_usage
//usage:       "$ freeramdisk /dev/ram2\n"
//usage:
//usage:#define fdflush_trivial_usage
//usage:       "DEVICE"
//usage:#define fdflush_full_usage "\n\n"
//usage:       "Force floppy disk drive to detect disk change"

#include <sys/mount.h>
#include "libbb.h"

/* From <linux/fd.h> */
#define FDFLUSH  _IO(2,0x4b)

int freeramdisk_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int freeramdisk_main(int argc UNUSED_PARAM, char **argv)
{
	int fd;

	fd = xopen(single_argv(argv), O_RDWR);

	// Act like freeramdisk, fdflush, or both depending on configuration.
	ioctl_or_perror_and_die(fd,
		((ENABLE_FREERAMDISK && applet_name[1] == 'r') || !ENABLE_FDFLUSH)
				? BLKFLSBUF
				: FDFLUSH,
		NULL, "%s", argv[1]
	);

	if (ENABLE_FEATURE_CLEAN_UP) close(fd);

	return EXIT_SUCCESS;
}
