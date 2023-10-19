// SPDX-License-Identifier: GPL-2.0
#include <linux/bug.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <cpu/pfc.h>

static struct resource sh7722_pfc_resources[] = {
	[0] = {
		.start	= 0xa4050100,
		.end	= 0xa405018f,
		.flags	= IORESOURCE_MEM,
	},
};

static int __init plat_pinmux_setup(void)
{
	return sh_pfc_register("pfc-sh7722", sh7722_pfc_resources,
			       ARRAY_SIZE(sh7722_pfc_resources));
}
arch_initcall(plat_pinmux_setup);
