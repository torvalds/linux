/* vi: set sw=4 ts=4: */
/*
 * pivot_root.c - Change root file system.  Based on util-linux 2.10s
 *
 * busyboxed by Evin Robertson
 * pivot_root syscall stubbed by Erik Andersen, so it will compile
 *     regardless of the kernel being used.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config PIVOT_ROOT
//config:	bool "pivot_root (898 bytes)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	The pivot_root utility swaps the mount points for the root filesystem
//config:	with some other mounted filesystem. This allows you to do all sorts
//config:	of wild and crazy things with your Linux system and is far more
//config:	powerful than 'chroot'.
//config:
//config:	Note: This is for initrd in linux 2.4. Under initramfs (introduced
//config:	in linux 2.6) use switch_root instead.

//applet:IF_PIVOT_ROOT(APPLET_NOFORK(pivot_root, pivot_root, BB_DIR_SBIN, BB_SUID_DROP, pivot_root))

//kbuild:lib-$(CONFIG_PIVOT_ROOT) += pivot_root.o

//usage:#define pivot_root_trivial_usage
//usage:       "NEW_ROOT PUT_OLD"
//usage:#define pivot_root_full_usage "\n\n"
//usage:       "Move the current root file system to PUT_OLD and make NEW_ROOT\n"
//usage:       "the new root file system"

#include "libbb.h"

extern int pivot_root(const char *new_root, const char *put_old);

int pivot_root_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int pivot_root_main(int argc, char **argv)
{
	if (argc != 3)
		bb_show_usage();

	/* NOFORK applet. Hardly matters wrt performance, but code is trivial */

	if (pivot_root(argv[1], argv[2]) < 0) {
		/* prints "pivot_root: <strerror text>" */
		bb_perror_nomsg_and_die();
	}

	return EXIT_SUCCESS;
}
