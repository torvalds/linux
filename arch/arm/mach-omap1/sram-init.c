// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP SRAM detection and management
 *
 * Copyright (C) 2005 Nokia Corporation
 * Written by Tony Lindgren <tony@atomide.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/fncpy.h>
#include <asm/tlb.h>
#include <asm/cacheflush.h>

#include <asm/mach/map.h>

#include "soc.h"
#include "sram.h"

#define OMAP1_SRAM_PA		0x20000000
#define SRAM_BOOTLOADER_SZ	0x80

/*
 * The amount of SRAM depends on the core type.
 * Note that we cannot try to test for SRAM here because writes
 * to secure SRAM will hang the system. Also the SRAM is not
 * yet mapped at this point.
 */
static void __init omap_detect_and_map_sram(void)
{
	unsigned long omap_sram_skip = SRAM_BOOTLOADER_SZ;
	unsigned long omap_sram_start = OMAP1_SRAM_PA;
	unsigned long omap_sram_size;

	if (cpu_is_omap7xx())
		omap_sram_size = 0x32000;	/* 200K */
	else if (cpu_is_omap15xx())
		omap_sram_size = 0x30000;	/* 192K */
	else if (cpu_is_omap1610() || cpu_is_omap1611() ||
			cpu_is_omap1621() || cpu_is_omap1710())
		omap_sram_size = 0x4000;	/* 16K */
	else {
		pr_err("Could not detect SRAM size\n");
		omap_sram_size = 0x4000;
	}

	omap_map_sram(omap_sram_start, omap_sram_size,
		omap_sram_skip, 1);
}

static void (*_omap_sram_reprogram_clock)(u32 dpllctl, u32 ckctl);

void omap_sram_reprogram_clock(u32 dpllctl, u32 ckctl)
{
	BUG_ON(!_omap_sram_reprogram_clock);
	/* On 730, bit 13 must always be 1 */
	if (cpu_is_omap7xx())
		ckctl |= 0x2000;
	_omap_sram_reprogram_clock(dpllctl, ckctl);
}

int __init omap_sram_init(void)
{
	omap_detect_and_map_sram();
	_omap_sram_reprogram_clock =
			omap_sram_push(omap1_sram_reprogram_clock,
					omap1_sram_reprogram_clock_sz);

	return 0;
}
