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
#include <linux/string.h>
#include <linux/screen_info.h>
#include <linux/console.h>
#include <linux/timex.h>
#include <linux/sched.h>

#include <asm/io.h>
#include <asm/machvec.h>
#include <asm/setup.h>

void __init
dig_setup (char **cmdline_p)
{
#ifdef CONFIG_SMP
	init_smp_config();
#endif
}
