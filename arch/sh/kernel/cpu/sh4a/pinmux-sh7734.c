// SPDX-License-Identifier: GPL-2.0
/*
 * SH7734 processor support - PFC hardware block
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 * Copyright (C) 2012  Nobuhiro Iwamatsu <nobuhiro.iwamatsu.yj@renesas.com>
 */
#include <linux/bug.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <cpu/pfc.h>

static struct resource sh7734_pfc_resources[] = {
	[0] = { /* PFC */
		.start	= 0xFFFC0000,
		.end	= 0xFFFC011C,
		.flags	= IORESOURCE_MEM,
	},
	[1] = { /* GPIO */
		.start	= 0xFFC40000,
		.end	= 0xFFC4502B,
		.flags	= IORESOURCE_MEM,
	}
};

static int __init plat_pinmux_setup(void)
{
	return sh_pfc_register("pfc-sh7734", sh7734_pfc_resources,
			       ARRAY_SIZE(sh7734_pfc_resources));
}
arch_initcall(plat_pinmux_setup);
