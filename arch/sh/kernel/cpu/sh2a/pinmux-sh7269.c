// SPDX-License-Identifier: GPL-2.0
/*
 * SH7269 Pinmux
 *
 * Copyright (C) 2012  Renesas Electronics Europe Ltd
 * Copyright (C) 2012  Phil Edworthy
 */

#include <linux/bug.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <cpu/pfc.h>

static struct resource sh7269_pfc_resources[] = {
	[0] = {
		.start	= 0xfffe3800,
		.end	= 0xfffe391f,
		.flags	= IORESOURCE_MEM,
	},
};

static int __init plat_pinmux_setup(void)
{
	return sh_pfc_register("pfc-sh7269", sh7269_pfc_resources,
			       ARRAY_SIZE(sh7269_pfc_resources));
}
arch_initcall(plat_pinmux_setup);
