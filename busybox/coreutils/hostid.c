/* vi: set sw=4 ts=4: */
/*
 * Mini hostid implementation for busybox
 *
 * Copyright (C) 2000  Edward Betts <edward@debian.org>.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config HOSTID
//config:	bool "hostid (247 bytes)"
//config:	default y
//config:	help
//config:	hostid prints the numeric identifier (in hexadecimal) for
//config:	the current host.

//applet:IF_HOSTID(APPLET_NOFORK(hostid, hostid, BB_DIR_USR_BIN, BB_SUID_DROP, hostid))

//kbuild:lib-$(CONFIG_HOSTID) += hostid.o

/* BB_AUDIT SUSv3 N/A -- Matches GNU behavior. */

//usage:#define hostid_trivial_usage
//usage:       ""
//usage:#define hostid_full_usage "\n\n"
//usage:       "Print out a unique 32-bit identifier for the machine"

#include "libbb.h"

/* This is a NOFORK applet. Be very careful! */

int hostid_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int hostid_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	if (argv[1]) {
		bb_show_usage();
	}

	/* POSIX says gethostid returns a "32-bit identifier" */
	printf("%08x\n", (unsigned)(uint32_t)gethostid());

	return fflush_all();
}
