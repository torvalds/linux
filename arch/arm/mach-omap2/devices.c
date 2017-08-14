/*
 * linux/arch/arm/mach-omap2/devices.c
 *
 * OMAP2 platform device setup/initialization
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/pinctrl/machine.h>

#include <asm/mach-types.h>
#include <asm/mach/map.h>

#include <linux/omap-dma.h>

#include "iomap.h"
#include "omap_hwmod.h"
#include "omap_device.h"

#include "soc.h"
#include "common.h"
#include "control.h"
#include "display.h"

#define L3_MODULES_MAX_LEN 12
#define L3_MODULES 3

/*-------------------------------------------------------------------------*/

#if IS_ENABLED(CONFIG_VIDEO_OMAP2_VOUT)
#if IS_ENABLED(CONFIG_FB_OMAP2)
static struct resource omap_vout_resource[3 - CONFIG_FB_OMAP2_NUM_FBS] = {
};
#else
static struct resource omap_vout_resource[2] = {
};
#endif

static struct platform_device omap_vout_device = {
	.name		= "omap_vout",
	.num_resources	= ARRAY_SIZE(omap_vout_resource),
	.resource 	= &omap_vout_resource[0],
	.id		= -1,
};

int __init omap_init_vout(void)
{
	return platform_device_register(&omap_vout_device);
}
#else
int __init omap_init_vout(void) { return 0; }
#endif
