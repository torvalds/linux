/* vi: set sw=4 ts=4: */
/*
 * Support functions for mounting devices by label/uuid
 *
 * Copyright (C) 2006 by Jason Schoon <floydpink@gmail.com>
 * Some portions cribbed from e2fsprogs, util-linux, dosfstools
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config FINDFS
//config:	bool "findfs (11 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	select VOLUMEID
//config:	help
//config:	Prints the name of a filesystem with given label or UUID.

/* Benefits from suid root: better access to /dev/BLOCKDEVs: */
//applet:IF_FINDFS(APPLET(findfs, BB_DIR_SBIN, BB_SUID_MAYBE))

//kbuild:lib-$(CONFIG_FINDFS) += findfs.o

//usage:#define findfs_trivial_usage
//usage:       "LABEL=label or UUID=uuid"
//usage:#define findfs_full_usage "\n\n"
//usage:       "Find a filesystem device based on a label or UUID"
//usage:
//usage:#define findfs_example_usage
//usage:       "$ findfs LABEL=MyDevice"

#include "libbb.h"
#include "volume_id.h"

int findfs_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int findfs_main(int argc UNUSED_PARAM, char **argv)
{
	char *dev = *++argv;

	if (!dev)
		bb_show_usage();

	if (is_prefixed_with(dev, "/dev/")) {
		/* Just pass any /dev/xxx name right through.
		 * This might aid in some scripts being able
		 * to call this unconditionally */
		dev = NULL;
	} else {
		/* Otherwise, handle LABEL=xxx and UUID=xxx,
		 * fail on anything else */
		if (!resolve_mount_spec(argv))
			bb_show_usage();
	}

	if (*argv != dev) {
		puts(*argv);
		return 0;
	}
	return 1;
}
