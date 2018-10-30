// SPDX-License-Identifier: GPL-2.0
/*
 * Platform dependent support for DIG64 platforms.
 *
 * Copyright (C) 1999 Intel Corp.
 * Copyright (C) 1999, 2001 Hewlett-Packard Co
 * Copyright (C) 1999, 2001, 2003 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999 Vijay Chander <vijay@engr.sgi.com>
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/string.h>
#include <linux/screen_info.h>
#include <linux/console.h>
#include <linux/timex.h>
#include <linux/sched.h>
#include <linux/root_dev.h>

#include <asm/io.h>
#include <asm/machvec.h>
#include <asm/setup.h>

void __init
dig_setup (char **cmdline_p)
{
	unsigned int orig_x, orig_y, num_cols, num_rows, font_height;

	/*
	 * Default to /dev/sda2.  This assumes that the EFI partition
	 * is physical disk 1 partition 1 and the Linux root disk is
	 * physical disk 1 partition 2.
	 */
	ROOT_DEV = Root_SDA2;		/* default to second partition on first drive */

#ifdef CONFIG_SMP
	init_smp_config();
#endif

	memset(&screen_info, 0, sizeof(screen_info));

	if (!ia64_boot_param->console_info.num_rows
	    || !ia64_boot_param->console_info.num_cols)
	{
		printk(KERN_WARNING "dig_setup: warning: invalid screen-info, guessing 80x25\n");
		orig_x = 0;
		orig_y = 0;
		num_cols = 80;
		num_rows = 25;
		font_height = 16;
	} else {
		orig_x = ia64_boot_param->console_info.orig_x;
		orig_y = ia64_boot_param->console_info.orig_y;
		num_cols = ia64_boot_param->console_info.num_cols;
		num_rows = ia64_boot_param->console_info.num_rows;
		font_height = 400 / num_rows;
	}

	screen_info.orig_x = orig_x;
	screen_info.orig_y = orig_y;
	screen_info.orig_video_cols  = num_cols;
	screen_info.orig_video_lines = num_rows;
	screen_info.orig_video_points = font_height;
	screen_info.orig_video_mode = 3;	/* XXX fake */
	screen_info.orig_video_isVGA = 1;	/* XXX fake */
	screen_info.orig_video_ega_bx = 3;	/* XXX fake */
}
