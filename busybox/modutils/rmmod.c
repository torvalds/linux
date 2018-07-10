/* vi: set sw=4 ts=4: */
/*
 * Mini rmmod implementation for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 * Copyright (C) 2008 Timo Teras <timo.teras@iki.fi>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config RMMOD
//config:	bool "rmmod (3.6 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	rmmod is used to unload specified modules from the kernel.

//applet:IF_RMMOD(IF_NOT_MODPROBE_SMALL(APPLET_NOEXEC(rmmod, rmmod, BB_DIR_SBIN, BB_SUID_DROP, rmmod)))

//kbuild:ifneq ($(CONFIG_MODPROBE_SMALL),y)
//kbuild:lib-$(CONFIG_RMMOD) += rmmod.o modutils.o
//kbuild:endif

//usage:#if !ENABLE_MODPROBE_SMALL
//usage:#define rmmod_trivial_usage
//usage:       "[-wfa] [MODULE]..."
//usage:#define rmmod_full_usage "\n\n"
//usage:       "Unload kernel modules\n"
//usage:     "\n	-w	Wait until the module is no longer used"
//usage:     "\n	-f	Force unload"
//usage:     "\n	-a	Remove all unused modules (recursively)"
//usage:#define rmmod_example_usage
//usage:       "$ rmmod tulip\n"
//usage:#endif

#include "libbb.h"
#include "modutils.h"

int rmmod_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int rmmod_main(int argc UNUSED_PARAM, char **argv)
{
	int n, err;
	unsigned flags = O_NONBLOCK | O_EXCL;

	/* Parse command line. */
	n = getopt32(argv, "wfas"); // -s ignored
	argv += optind;
	if (n & 1)  // --wait
		flags &= ~O_NONBLOCK;
	if (n & 2)  // --force
		flags |= O_TRUNC;
	if (n & 4) {
		/* Unload _all_ unused modules via NULL delete_module() call */
		err = bb_delete_module(NULL, flags);
		if (err && err != EFAULT)
			bb_perror_msg_and_die("rmmod");
		return EXIT_SUCCESS;
	}

	if (!*argv)
		bb_show_usage();

	n = ENABLE_FEATURE_2_4_MODULES && get_linux_version_code() < KERNEL_VERSION(2,6,0);
	while (*argv) {
		char modname[MODULE_NAME_LEN];
		const char *bname;

		bname = bb_basename(*argv++);
		if (n)
			safe_strncpy(modname, bname, MODULE_NAME_LEN);
		else
			filename2modname(bname, modname);
		err = bb_delete_module(modname, flags);
		if (err)
			bb_perror_msg_and_die("can't unload module '%s'",
					modname);
	}

	return EXIT_SUCCESS;
}
