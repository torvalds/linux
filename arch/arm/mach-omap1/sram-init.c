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
#include <linux/set_memory.h>

#include <asm/fncpy.h>
#include <asm/tlb.h>
#include <asm/cacheflush.h>

#include <asm/mach/map.h>

#include "soc.h"
#include "sram.h"

#define OMAP1_SRAM_PA		0x20000000
#define SRAM_BOOTLOADER_SZ	0x80
#define ROUND_DOWN(value, boundary)	((value) & (~((boundary) - 1)))

static void __iomem *omap_sram_base;
static unsigned long omap_sram_start;
static unsigned long omap_sram_skip;
static unsigned long omap_sram_size;
static void __iomem *omap_sram_ceil;

/*
 * Memory allocator for SRAM: calculates the new ceiling address
 * for pushing a function using the fncpy API.
 *
 * Note that fncpy requires the returned address to be aligned
 * to an 8-byte boundary.
 */
static void *omap_sram_push_address(unsigned long size)
{
	unsigned long available, new_ceil = (unsigned long)omap_sram_ceil;

	available = omap_sram_ceil - (omap_sram_base + omap_sram_skip);

	if (size > available) {
		pr_err("Not enough space in SRAM\n");
		return NULL;
	}

	new_ceil -= size;
	new_ceil = ROUND_DOWN(new_ceil, FNCPY_ALIGN);
	omap_sram_ceil = IOMEM(new_ceil);

	return (void __force *)omap_sram_ceil;
}

void *omap_sram_push(void *funcp, unsigned long size)
{
	void *sram;
	unsigned long base;
	int pages;
	void *dst = NULL;

	sram = omap_sram_push_address(size);
	if (!sram)
		return NULL;

	base = (unsigned long)sram & PAGE_MASK;
	pages = PAGE_ALIGN(size) / PAGE_SIZE;

	set_memory_rw(base, pages);

	dst = fncpy(sram, funcp, size);

	set_memory_rox(base, pages);

	return dst;
}

/*
 * The amount of SRAM depends on the core type.
 * Note that we cannot try to test for SRAM here because writes
 * to secure SRAM will hang the system. Also the SRAM is not
 * yet mapped at this point.
 * Note that we cannot use ioremap for SRAM, as clock init needs SRAM early.
 */
static void __init omap_detect_and_map_sram(void)
{
	unsigned long base;
	int pages;

	omap_sram_skip = SRAM_BOOTLOADER_SZ;
	omap_sram_start = OMAP1_SRAM_PA;

	if (cpu_is_omap15xx())
		omap_sram_size = 0x30000;	/* 192K */
	else if (cpu_is_omap1610() || cpu_is_omap1611() ||
			cpu_is_omap1621() || cpu_is_omap1710())
		omap_sram_size = 0x4000;	/* 16K */
	else {
		pr_err("Could not detect SRAM size\n");
		omap_sram_size = 0x4000;
	}

	omap_sram_start = ROUND_DOWN(omap_sram_start, PAGE_SIZE);
	omap_sram_base = __arm_ioremap_exec(omap_sram_start, omap_sram_size, 1);
	if (!omap_sram_base) {
		pr_err("SRAM: Could not map\n");
		return;
	}

	omap_sram_ceil = omap_sram_base + omap_sram_size;

	/*
	 * Looks like we need to preserve some bootloader code at the
	 * beginning of SRAM for jumping to flash for reboot to work...
	 */
	memset_io(omap_sram_base + omap_sram_skip, 0,
		  omap_sram_size - omap_sram_skip);

	base = (unsigned long)omap_sram_base;
	pages = PAGE_ALIGN(omap_sram_size) / PAGE_SIZE;

	set_memory_rox(base, pages);
}

static void (*_omap_sram_reprogram_clock)(u32 dpllctl, u32 ckctl);

void omap_sram_reprogram_clock(u32 dpllctl, u32 ckctl)
{
	BUG_ON(!_omap_sram_reprogram_clock);
	_omap_sram_reprogram_clock(dpllctl, ckctl);
}

int __init omap1_sram_init(void)
{
	omap_detect_and_map_sram();
	_omap_sram_reprogram_clock =
			omap_sram_push(omap1_sram_reprogram_clock,
					omap1_sram_reprogram_clock_sz);

	return 0;
}
