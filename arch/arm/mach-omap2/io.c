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

#include <plat/sram.h>
#include <plat/sdrc.h>
#include <plat/gpmc.h>
#include <plat/serial.h>

#include "clock2xxx.h"
#include "clock3xxx.h"
#include "clock44xx.h"
#include "io.h"

#include <plat/omap-pm.h>
#include <plat/powerdomain.h>
#include "powerdomains.h"

#include <plat/clockdomain.h>
#include "clockdomains.h"

#include <plat/omap_hwmod.h>

/*
 * The machine specific code may provide the extra mapping besides the
 * default mapping provided here.
 */

#ifdef CONFIG_ARCH_OMAP2
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

#ifdef	CONFIG_ARCH_OMAP3
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
#if defined(CONFIG_DEBUG_LL) &&							\
	(defined(CONFIG_MACH_OMAP_ZOOM2) || defined(CONFIG_MACH_OMAP_ZOOM3))
	{
		.virtual	= ZOOM_UART_VIRT,
		.pfn		= __phys_to_pfn(ZOOM_UART_BASE),
		.length		= SZ_1M,
		.type		= MT_DEVICE
	},
#endif
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

static void __init _omap2_map_common_io(void)
{
	/* Normally devicemaps_init() would flush caches and tlb after
	 * mdesc->map_io(), but we must also do it here because of the CPU
	 * revision check below.
	 */
	local_flush_tlb_all();
	flush_cache_all();

	omap2_check_revision();
	omap_sram_init();
}

#ifdef CONFIG_ARCH_OMAP2420
void __init omap242x_map_common_io(void)
{
	iotable_init(omap24xx_io_desc, ARRAY_SIZE(omap24xx_io_desc));
	iotable_init(omap242x_io_desc, ARRAY_SIZE(omap242x_io_desc));
	_omap2_map_common_io();
}
#endif

#ifdef CONFIG_ARCH_OMAP2430
void __init omap243x_map_common_io(void)
{
	iotable_init(omap24xx_io_desc, ARRAY_SIZE(omap24xx_io_desc));
	iotable_init(omap243x_io_desc, ARRAY_SIZE(omap243x_io_desc));
	_omap2_map_common_io();
}
#endif

#ifdef CONFIG_ARCH_OMAP3
void __init omap34xx_map_common_io(void)
{
	iotable_init(omap34xx_io_desc, ARRAY_SIZE(omap34xx_io_desc));
	_omap2_map_common_io();
}
#endif

#ifdef CONFIG_ARCH_OMAP4
void __init omap44xx_map_common_io(void)
{
	iotable_init(omap44xx_io_desc, ARRAY_SIZE(omap44xx_io_desc));
	_omap2_map_common_io();
}
#endif

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
	u8 skip_setup_idle = 0;

	pwrdm_init(powerdomains_omap);
	clkdm_init(clockdomains_omap, clkdm_autodeps);
	if (cpu_is_omap242x())
		omap2420_hwmod_init();
	else if (cpu_is_omap243x())
		omap2430_hwmod_init();
	else if (cpu_is_omap34xx())
		omap3xxx_hwmod_init();
	else if (cpu_is_omap44xx())
		omap44xx_hwmod_init();

	/* The OPP tables have to be registered before a clk init */
	omap_pm_if_early_init(mpu_opps, dsp_opps, l3_opps);

	if (cpu_is_omap2420())
		omap2420_clk_init();
	else if (cpu_is_omap2430())
		omap2430_clk_init();
	else if (cpu_is_omap34xx())
		omap3xxx_clk_init();
	else if (cpu_is_omap44xx())
		omap4xxx_clk_init();
	else
		pr_err("Could not init clock framework - unknown CPU\n");

	omap_serial_early_init();

#ifndef CONFIG_PM_RUNTIME
	skip_setup_idle = 1;
#endif
	omap_hwmod_late_init(skip_setup_idle);
	if (cpu_is_omap24xx() || cpu_is_omap34xx()) {
		omap2_sdrc_init(sdrc_cs0, sdrc_cs1);
		_omap2_init_reprogram_sdrc();
	}
	gpmc_init();
}
