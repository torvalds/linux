/*
 * linux/arch/arm/mach-omap2/io.c
 *
 * OMAP2 I/O mapping code
 *
 * Copyright (C) 2005 Nokia Corporation
 * Copyright (C) 2007 Texas Instruments
 *
 * Author:
 *	Juha Yrjola <juha.yrjola@nokia.com>
 *	Syed Khasim <x0khasim@ti.com>
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

#include <mach/mux.h>
#include <mach/omapfb.h>
#include <mach/sram.h>

#include "memory.h"

#include "clock.h"

#include <mach/powerdomain.h>

#include "powerdomains.h"

#include <mach/clockdomain.h>
#include "clockdomains.h"

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
		.virtual        = L4_24XX_VIRT,
		.pfn            = __phys_to_pfn(L4_24XX_PHYS),
		.length         = L4_24XX_SIZE,
		.type           = MT_DEVICE
	},
#ifdef CONFIG_ARCH_OMAP2430
	{
		.virtual	= L4_WK_243X_VIRT,
		.pfn		= __phys_to_pfn(L4_WK_243X_PHYS),
		.length		= L4_WK_243X_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= OMAP243X_GPMC_VIRT,
		.pfn		= __phys_to_pfn(OMAP243X_GPMC_PHYS),
		.length		= OMAP243X_GPMC_SIZE,
		.type		= MT_DEVICE
	},
#endif
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
	pwrdm_init(powerdomains_omap);
	clkdm_init(clockdomains_omap, clkdm_pwrdm_autodeps);
	omap2_clk_init();
/*
 * Need to Fix this for 2430
 */
#ifndef CONFIG_ARCH_OMAP2430
	omap2_init_memory();
#endif
	gpmc_init();
}
