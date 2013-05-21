/*
 * SH7264 Pinmux
 *
 *  Copyright (C) 2012  Renesas Electronics Europe Ltd
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/bug.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <cpu/pfc.h>

static struct resource sh7264_pfc_resources[] = {
	[0] = {
		.start	= 0xfffe3800,
		.end	= 0xfffe393f,
		.flags	= IORESOURCE_MEM,
	},
};

static int __init plat_pinmux_setup(void)
{
	return sh_pfc_register("pfc-sh7264", sh7264_pfc_resources,
			       ARRAY_SIZE(sh7264_pfc_resources));
}
arch_initcall(plat_pinmux_setup);
