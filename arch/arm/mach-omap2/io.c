/*
 * linux/arch/arm/mach-omap2/io.c
 *
 * OMAP2 I/O mapping code
 *
 * Copyright (C) 2005 Nokia Corporation
 * Author: Juha Yrjölä <juha.yrjola@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/tlb.h>
#include <asm/io.h>

#include <asm/mach/map.h>

#include <asm/arch/mux.h>
#include <asm/arch/omapfb.h>

extern void omap_sram_init(void);
extern int omap2_clk_init(void);
extern void omap2_check_revision(void);
extern void gpmc_init(void);
extern void omapfb_reserve_sdram(void);

/*
 * The machine specific code may provide the extra mapping besides the
 * default mapping provided here.
 */
static struct map_desc omap2_io_desc[] __initdata = {
	{
		.virtual	= L3_24XX_VIRT,
		.pfn		= __phys_to_pfn(L3_24XX_PHYS),
		.length		= L3_24XX_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= DSP_MEM_24XX_VIRT,
		.pfn		= __phys_to_pfn(DSP_MEM_24XX_PHYS),
		.length		= DSP_MEM_24XX_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= DSP_IPI_24XX_VIRT,
		.pfn		= __phys_to_pfn(DSP_IPI_24XX_PHYS),
		.length		= DSP_IPI_24XX_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= DSP_MMU_24XX_VIRT,
		.pfn		= __phys_to_pfn(DSP_MMU_24XX_PHYS),
		.length		= DSP_MMU_24XX_SIZE,
		.type		= MT_DEVICE
	}
};

void __init omap2_map_common_io(void)
{
	iotable_init(omap2_io_desc, ARRAY_SIZE(omap2_io_desc));

	/* Normally devicemaps_init() would flush caches and tlb after
	 * mdesc->map_io(), but we must also do it here because of the CPU
	 * revision check below.
	 */
	local_flush_tlb_all();
	flush_cache_all();

	omap2_check_revision();
	omap_sram_init();
	omapfb_reserve_sdram();
}

void __init omap2_init_common_hw(void)
{
	omap2_mux_init();
	omap2_clk_init();
	gpmc_init();
}
