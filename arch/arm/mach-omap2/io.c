/*
 * linux/arch/arm/mach-omap2/io.c
 *
 * OMAP2 I/O mapping code
 *
 * Copyright (C) 2005 Nokia Corporation
 * Copyright (C) 2007-2009 Texas Instruments
 *
 * Author:
 *	Juha Yrjola <juha.yrjola@nokia.com>
 *	Syed Khasim <x0khasim@ti.com>
 *
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/omapfb.h>

#include <asm/tlb.h>

#include <asm/mach/map.h>

#include <plat/mux.h>
#include <plat/sram.h>
#include <plat/sdrc.h>
#include <plat/gpmc.h>
#include <plat/serial.h>
#include <plat/vram.h>

#include "clock.h"

#include <plat/omap-pm.h>
#include <plat/powerdomain.h>
#include "powerdomains.h"

#include <plat/clockdomain.h>
#include "clockdomains.h"
#include <plat/omap_hwmod.h>
#include "omap_hwmod_2420.h"
#include "omap_hwmod_2430.h"
#include "omap_hwmod_34xx.h"

/*
 * The machine specific code may provide the extra mapping besides the
 * default mapping provided here.
 */

#ifdef CONFIG_ARCH_OMAP24XX
static struct map_desc omap24xx_io_desc[] __initdata = {
	{
		.virtual	= L3_24XX_VIRT,
		.pfn		= __phys_to_pfn(L3_24XX_PHYS),
		.length		= L3_24XX_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= L4_24XX_VIRT,
		.pfn		= __phys_to_pfn(L4_24XX_PHYS),
		.length		= L4_24XX_SIZE,
		.type		= MT_DEVICE
	},
};

#ifdef CONFIG_ARCH_OMAP2420
static struct map_desc omap242x_io_desc[] __initdata = {
	{
		.virtual	= DSP_MEM_2420_VIRT,
		.pfn		= __phys_to_pfn(DSP_MEM_2420_PHYS),
		.length		= DSP_MEM_2420_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= DSP_IPI_2420_VIRT,
		.pfn		= __phys_to_pfn(DSP_IPI_2420_PHYS),
		.length		= DSP_IPI_2420_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= DSP_MMU_2420_VIRT,
		.pfn		= __phys_to_pfn(DSP_MMU_2420_PHYS),
		.length		= DSP_MMU_2420_SIZE,
		.type		= MT_DEVICE
	},
};

#endif

#ifdef CONFIG_ARCH_OMAP2430
static struct map_desc omap243x_io_desc[] __initdata = {
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
	{
		.virtual	= OMAP243X_SDRC_VIRT,
		.pfn		= __phys_to_pfn(OMAP243X_SDRC_PHYS),
		.length		= OMAP243X_SDRC_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= OMAP243X_SMS_VIRT,
		.pfn		= __phys_to_pfn(OMAP243X_SMS_PHYS),
		.length		= OMAP243X_SMS_SIZE,
		.type		= MT_DEVICE
	},
};
#endif
#endif

#ifdef	CONFIG_ARCH_OMAP34XX
static struct map_desc omap34xx_io_desc[] __initdata = {
	{
		.virtual	= L3_34XX_VIRT,
		.pfn		= __phys_to_pfn(L3_34XX_PHYS),
		.length		= L3_34XX_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= L4_34XX_VIRT,
		.pfn		= __phys_to_pfn(L4_34XX_PHYS),
		.length		= L4_34XX_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= L4_WK_34XX_VIRT,
		.pfn		= __phys_to_pfn(L4_WK_34XX_PHYS),
		.length		= L4_WK_34XX_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= OMAP34XX_GPMC_VIRT,
		.pfn		= __phys_to_pfn(OMAP34XX_GPMC_PHYS),
		.length		= OMAP34XX_GPMC_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= OMAP343X_SMS_VIRT,
		.pfn		= __phys_to_pfn(OMAP343X_SMS_PHYS),
		.length		= OMAP343X_SMS_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= OMAP343X_SDRC_VIRT,
		.pfn		= __phys_to_pfn(OMAP343X_SDRC_PHYS),
		.length		= OMAP343X_SDRC_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= L4_PER_34XX_VIRT,
		.pfn		= __phys_to_pfn(L4_PER_34XX_PHYS),
		.length		= L4_PER_34XX_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= L4_EMU_34XX_VIRT,
		.pfn		= __phys_to_pfn(L4_EMU_34XX_PHYS),
		.length		= L4_EMU_34XX_SIZE,
		.type		= MT_DEVICE
	},
};
#endif
#ifdef	CONFIG_ARCH_OMAP4
static struct map_desc omap44xx_io_desc[] __initdata = {
	{
		.virtual	= L3_44XX_VIRT,
		.pfn		= __phys_to_pfn(L3_44XX_PHYS),
		.length		= L3_44XX_SIZE,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= L4_44XX_VIRT,
		.pfn		= __phys_to_pfn(L4_44XX_PHYS),
		.length		= L4_44XX_SIZE,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= L4_WK_44XX_VIRT,
		.pfn		= __phys_to_pfn(L4_WK_44XX_PHYS),
		.length		= L4_WK_44XX_SIZE,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= OMAP44XX_GPMC_VIRT,
		.pfn		= __phys_to_pfn(OMAP44XX_GPMC_PHYS),
		.length		= OMAP44XX_GPMC_SIZE,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= OMAP44XX_EMIF1_VIRT,
		.pfn		= __phys_to_pfn(OMAP44XX_EMIF1_PHYS),
		.length		= OMAP44XX_EMIF1_SIZE,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= OMAP44XX_EMIF2_VIRT,
		.pfn		= __phys_to_pfn(OMAP44XX_EMIF2_PHYS),
		.length		= OMAP44XX_EMIF2_SIZE,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= OMAP44XX_DMM_VIRT,
		.pfn		= __phys_to_pfn(OMAP44XX_DMM_PHYS),
		.length		= OMAP44XX_DMM_SIZE,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= L4_PER_44XX_VIRT,
		.pfn		= __phys_to_pfn(L4_PER_44XX_PHYS),
		.length		= L4_PER_44XX_SIZE,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= L4_EMU_44XX_VIRT,
		.pfn		= __phys_to_pfn(L4_EMU_44XX_PHYS),
		.length		= L4_EMU_44XX_SIZE,
		.type		= MT_DEVICE,
	},
};
#endif

void __init omap2_map_common_io(void)
{
#if defined(CONFIG_ARCH_OMAP2420)
	iotable_init(omap24xx_io_desc, ARRAY_SIZE(omap24xx_io_desc));
	iotable_init(omap242x_io_desc, ARRAY_SIZE(omap242x_io_desc));
#endif

#if defined(CONFIG_ARCH_OMAP2430)
	iotable_init(omap24xx_io_desc, ARRAY_SIZE(omap24xx_io_desc));
	iotable_init(omap243x_io_desc, ARRAY_SIZE(omap243x_io_desc));
#endif

#if defined(CONFIG_ARCH_OMAP34XX)
	iotable_init(omap34xx_io_desc, ARRAY_SIZE(omap34xx_io_desc));
#endif

#if defined(CONFIG_ARCH_OMAP4)
	iotable_init(omap44xx_io_desc, ARRAY_SIZE(omap44xx_io_desc));
#endif
	/* Normally devicemaps_init() would flush caches and tlb after
	 * mdesc->map_io(), but we must also do it here because of the CPU
	 * revision check below.
	 */
	local_flush_tlb_all();
	flush_cache_all();

	omap2_check_revision();
	omap_sram_init();
	omapfb_reserve_sdram();
	omap_vram_reserve_sdram();
}

/*
 * omap2_init_reprogram_sdrc - reprogram SDRC timing parameters
 *
 * Sets the CORE DPLL3 M2 divider to the same value that it's at
 * currently.  This has the effect of setting the SDRC SDRAM AC timing
 * registers to the values currently defined by the kernel.  Currently
 * only defined for OMAP3; will return 0 if called on OMAP2.  Returns
 * -EINVAL if the dpll3_m2_ck cannot be found, 0 if called on OMAP2,
 * or passes along the return value of clk_set_rate().
 */
static int __init _omap2_init_reprogram_sdrc(void)
{
	struct clk *dpll3_m2_ck;
	int v = -EINVAL;
	long rate;

	if (!cpu_is_omap34xx())
		return 0;

	dpll3_m2_ck = clk_get(NULL, "dpll3_m2_ck");
	if (!dpll3_m2_ck)
		return -EINVAL;

	rate = clk_get_rate(dpll3_m2_ck);
	pr_info("Reprogramming SDRC clock to %ld Hz\n", rate);
	v = clk_set_rate(dpll3_m2_ck, rate);
	if (v)
		pr_err("dpll3_m2_clk rate change failed: %d\n", v);

	clk_put(dpll3_m2_ck);

	return v;
}

void __init omap2_init_common_hw(struct omap_sdrc_params *sdrc_cs0,
				 struct omap_sdrc_params *sdrc_cs1)
{
	struct omap_hwmod **hwmods = NULL;

	if (cpu_is_omap2420())
		hwmods = omap2420_hwmods;
	else if (cpu_is_omap2430())
		hwmods = omap2430_hwmods;
	else if (cpu_is_omap34xx())
		hwmods = omap34xx_hwmods;

#ifndef CONFIG_ARCH_OMAP4 /* FIXME: Remove this once the clkdev is ready */
	/* The OPP tables have to be registered before a clk init */
	omap_hwmod_init(hwmods);
	omap2_mux_init();
	omap_pm_if_early_init(mpu_opps, dsp_opps, l3_opps);
	pwrdm_init(powerdomains_omap);
	clkdm_init(clockdomains_omap, clkdm_pwrdm_autodeps);
#endif
	omap2_clk_init();
	omap_serial_early_init();
#ifndef CONFIG_ARCH_OMAP4
	omap_hwmod_late_init();
	omap_pm_if_init();
	omap2_sdrc_init(sdrc_cs0, sdrc_cs1);
	_omap2_init_reprogram_sdrc();
#endif
	gpmc_init();
}
