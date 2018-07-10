/* vi: set sw=4 ts=4: */
/*
 * raidautorun implementation for busybox
 *
 * Copyright (C) 2006 Bernhard Reutner-Fischer
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config RAIDAUTORUN
//config:	bool "raidautorun (1.4 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	raidautorun tells the kernel md driver to
//config:	search and start RAID arrays.

//applet:IF_RAIDAUTORUN(APPLET_NOEXEC(raidautorun, raidautorun, BB_DIR_SBIN, BB_SUID_DROP, raidautorun))

//kbuild:lib-$(CONFIG_RAIDAUTORUN) += raidautorun.o

//usage:#define raidautorun_trivial_usage
//usage:       "DEVICE"
//usage:#define raidautorun_full_usage "\n\n"
//usage:       "Tell the kernel to automatically search and start RAID arrays"
//usage:
//usage:#define raidautorun_example_usage
//usage:       "$ raidautorun /dev/md0"

#include "libbb.h"

#include <linux/major.h>
#include <linux/raid/md_u.h>

int raidautorun_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int raidautorun_main(int argc UNUSED_PARAM, char **argv)
{
	xioctl(xopen(single_argv(argv), O_RDONLY), RAID_AUTORUN, NULL);
	return EXIT_SUCCESS;
}
