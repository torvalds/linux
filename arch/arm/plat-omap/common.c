/*
 * linux/arch/arm/plat-omap/common.c
 *
 * Code common to all OMAP machines.
 * The file is created by Tony Lindgren <tony@atomide.com>
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>

#include <plat/common.h>
#include <plat/vram.h>
#include <linux/platform_data/dsp-omap.h>
#include <plat/dma.h>

#include <plat/omap-secure.h>

void __init omap_reserve(void)
{
	omap_vram_reserve_sdram_memblock();
	omap_dsp_reserve_sdram_memblock();
	omap_secure_ram_reserve_memblock();
	omap_barrier_reserve_memblock();
}

void __init omap_init_consistent_dma_size(void)
{
#ifdef CONFIG_FB_OMAP_CONSISTENT_DMA_SIZE
	init_consistent_dma_size(CONFIG_FB_OMAP_CONSISTENT_DMA_SIZE << 20);
#endif
}

/*
 * Stub function for OMAP2 so that common files
 * continue to build when custom builds are used
 */
int __weak omap_secure_ram_reserve_memblock(void)
{
	return 0;
}
