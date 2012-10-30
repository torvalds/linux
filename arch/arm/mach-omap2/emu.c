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

#include "soc.h"
#include "iomap.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Shishkin");

/* Cortex CoreSight components within omap3xxx EMU */
#define ETM_BASE	(L4_EMU_34XX_PHYS + 0x10000)
#define DBG_BASE	(L4_EMU_34XX_PHYS + 0x11000)
#define ETB_BASE	(L4_EMU_34XX_PHYS + 0x1b000)
#define DAPCTL		(L4_EMU_34XX_PHYS + 0x1d000)

static AMBA_APB_DEVICE(omap3_etb, "etb", 0x000bb907, ETB_BASE, { }, NULL);
static AMBA_APB_DEVICE(omap3_etm, "etm", 0x102bb921, ETM_BASE, { }, NULL);

static int __init emu_init(void)
{
	if (!cpu_is_omap34xx())
		return -ENODEV;

	amba_device_register(&omap3_etb_device, &iomem_resource);
	amba_device_register(&omap3_etm_device, &iomem_resource);

	return 0;
}

subsys_initcall(emu_init);
