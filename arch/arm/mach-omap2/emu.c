/*
 * emu.c
 *
 * ETM and ETB CoreSight components' resources as found in OMAP3xxx.
 *
 * Copyright (C) 2009 Nokia Corporation.
 * Alexander Shishkin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Shishkin");

/* Cortex CoreSight components within omap3xxx EMU */
#define ETM_BASE	(L4_EMU_34XX_PHYS + 0x10000)
#define DBG_BASE	(L4_EMU_34XX_PHYS + 0x11000)
#define ETB_BASE	(L4_EMU_34XX_PHYS + 0x1b000)
#define DAPCTL		(L4_EMU_34XX_PHYS + 0x1d000)

static struct amba_device omap3_etb_device = {
	.dev		= {
		.init_name = "etb",
	},
	.res		= {
		.start	= ETB_BASE,
		.end	= ETB_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.periphid	= 0x000bb907,
};

static struct amba_device omap3_etm_device = {
	.dev		= {
		.init_name = "etm",
	},
	.res		= {
		.start	= ETM_BASE,
		.end	= ETM_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.periphid	= 0x102bb921,
};

static int __init emu_init(void)
{
	amba_device_register(&omap3_etb_device, &iomem_resource);
	amba_device_register(&omap3_etm_device, &iomem_resource);

	return 0;
}

subsys_initcall(emu_init);

