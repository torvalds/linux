/*
 * linux/arch/arm/plat-omap/sram.c
 *
 * OMAP SRAM detection and management
 *
 * Copyright (C) 2005 Nokia Corporation
 * Written by Tony Lindgren <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/mach/map.h>
#include <asm/io.h>
#include <asm/cacheflush.h>

#include "sram.h"

#define OMAP1_SRAM_BASE		0xd0000000
#define OMAP1_SRAM_START	0x20000000
#define SRAM_BOOTLOADER_SZ	0x80

static unsigned long omap_sram_base;
static unsigned long omap_sram_size;
static unsigned long omap_sram_ceil;

/*
 * The amount of SRAM depends on the core type:
 * 730 = 200K, 1510 = 512K, 5912 = 256K, 1610 = 16K, 1710 = 16K
 * Note that we cannot try to test for SRAM here because writes
 * to secure SRAM will hang the system. Also the SRAM is not
 * yet mapped at this point.
 */
void __init omap_detect_sram(void)
{
	omap_sram_base = OMAP1_SRAM_BASE;

	if (cpu_is_omap730())
		omap_sram_size = 0x32000;
	else if (cpu_is_omap1510())
		omap_sram_size = 0x80000;
	else if (cpu_is_omap1610() || cpu_is_omap1621() || cpu_is_omap1710())
		omap_sram_size = 0x4000;
	else if (cpu_is_omap1611())
		omap_sram_size = 0x3e800;
	else {
		printk(KERN_ERR "Could not detect SRAM size\n");
		omap_sram_size = 0x4000;
	}

	printk(KERN_INFO "SRAM size: 0x%lx\n", omap_sram_size);
	omap_sram_ceil = omap_sram_base + omap_sram_size;
}

static struct map_desc omap_sram_io_desc[] __initdata = {
	{	/* .length gets filled in at runtime */
		.virtual	= OMAP1_SRAM_BASE,
		.pfn		= __phys_to_pfn(OMAP1_SRAM_START),
		.type		= MT_DEVICE
	}
};

/*
 * In order to use last 2kB of SRAM on 1611b, we must round the size
 * up to multiple of PAGE_SIZE. We cannot use ioremap for SRAM, as
 * clock init needs SRAM early.
 */
void __init omap_map_sram(void)
{
	if (omap_sram_size == 0)
		return;

	omap_sram_io_desc[0].length = (omap_sram_size + PAGE_SIZE-1)/PAGE_SIZE;
	omap_sram_io_desc[0].length *= PAGE_SIZE;
	iotable_init(omap_sram_io_desc, ARRAY_SIZE(omap_sram_io_desc));

	/*
	 * Looks like we need to preserve some bootloader code at the
	 * beginning of SRAM for jumping to flash for reboot to work...
	 */
	memset((void *)omap_sram_base + SRAM_BOOTLOADER_SZ, 0,
	       omap_sram_size - SRAM_BOOTLOADER_SZ);
}

static void (*_omap_sram_reprogram_clock)(u32 dpllctl, u32 ckctl) = NULL;

void omap_sram_reprogram_clock(u32 dpllctl, u32 ckctl)
{
	if (_omap_sram_reprogram_clock == NULL)
		panic("Cannot use SRAM");

	return _omap_sram_reprogram_clock(dpllctl, ckctl);
}

void * omap_sram_push(void * start, unsigned long size)
{
	if (size > (omap_sram_ceil - (omap_sram_base + SRAM_BOOTLOADER_SZ))) {
		printk(KERN_ERR "Not enough space in SRAM\n");
		return NULL;
	}
	omap_sram_ceil -= size;
	omap_sram_ceil &= ~0x3;
	memcpy((void *)omap_sram_ceil, start, size);

	return (void *)omap_sram_ceil;
}

void __init omap_sram_init(void)
{
	omap_detect_sram();
	omap_map_sram();
	_omap_sram_reprogram_clock = omap_sram_push(sram_reprogram_clock,
						    sram_reprogram_clock_sz);
}
