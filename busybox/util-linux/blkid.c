/* vi: set sw=4 ts=4: */
/*
 * Print UUIDs on all filesystems
 *
 * Copyright (C) 2008 Denys Vlasenko.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config BLKID
//config:	bool "blkid (11 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	select VOLUMEID
//config:	help
//config:	Lists labels and UUIDs of all filesystems.
//config:
//config:config FEATURE_BLKID_TYPE
//config:	bool "Print filesystem type"
//config:	default y
//config:	depends on BLKID
//config:	help
//config:	Show TYPE="filesystem type"

//applet:IF_BLKID(APPLET_NOEXEC(blkid, blkid, BB_DIR_SBIN, BB_SUID_DROP, blkid))

//kbuild:lib-$(CONFIG_BLKID) += blkid.o

//usage:#define blkid_trivial_usage
//usage:       "[BLOCKDEV]..."
//usage:#define blkid_full_usage "\n\n"
//usage:       "Print UUIDs of all filesystems"

#include "libbb.h"
#include "volume_id.h"

int blkid_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int blkid_main(int argc UNUSED_PARAM, char **argv)
{
	int scan_devices = 1;

	while (*++argv) {
		/* Note: bogus device names don't cause any error messages */
		add_to_uuid_cache(*argv);
		scan_devices = 0;
	}

	display_uuid_cache(scan_devices);
	return 0;
}
