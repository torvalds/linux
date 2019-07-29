// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/plat-omap/sram.c
 *
 * OMAP SRAM detection and management
 *
 * Copyright (C) 2005 Nokia Corporation
 * Written by Tony Lindgren <tony@atomide.com>
 *
 * Copyright (C) 2009-2012 Texas Instruments
 * Added OMAP4/5 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 */
#undef DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/fncpy.h>
#include <asm/tlb.h>
#include <asm/cacheflush.h>
#include <asm/set_memory.h>

#include <asm/mach/map.h>

#include <plat/sram.h>

#define ROUND_DOWN(value,boundary)	((value) & (~((boundary)-1)))

static void __iomem *omap_sram_base;
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

	return (void *)omap_sram_ceil;
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

	set_memory_ro(base, pages);
	set_memory_x(base, pages);

	return dst;
}

/*
 * The SRAM context is lost during off-idle and stack
 * needs to be reset.
 */
void omap_sram_reset(void)
{
	omap_sram_ceil = omap_sram_base + omap_sram_size;
}

/*
 * Note that we cannot use ioremap for SRAM, as clock init needs SRAM early.
 */
void __init omap_map_sram(unsigned long start, unsigned long size,
				 unsigned long skip, int cached)
{
	unsigned long base;
	int pages;

	if (size == 0)
		return;

	start = ROUND_DOWN(start, PAGE_SIZE);
	omap_sram_size = size;
	omap_sram_skip = skip;
	omap_sram_base = __arm_ioremap_exec(start, size, cached);
	if (!omap_sram_base) {
		pr_err("SRAM: Could not map\n");
		return;
	}

	omap_sram_reset();

	/*
	 * Looks like we need to preserve some bootloader code at the
	 * beginning of SRAM for jumping to flash for reboot to work...
	 */
	memset_io(omap_sram_base + omap_sram_skip, 0,
		  omap_sram_size - omap_sram_skip);

	base = (unsigned long)omap_sram_base;
	pages = PAGE_ALIGN(omap_sram_size) / PAGE_SIZE;

	set_memory_ro(base, pages);
	set_memory_x(base, pages);
}
