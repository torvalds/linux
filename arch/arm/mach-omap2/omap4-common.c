/*
 * OMAP4 specific common source file.
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Author:
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 *
 * This program is free software,you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#include <asm/hardware/gic.h>
#include <asm/hardware/cache-l2x0.h>

#include <mach/hardware.h>
#include <mach/omap4-common.h>

#ifdef CONFIG_CACHE_L2X0
void __iomem *l2cache_base;
#endif

void __iomem *gic_cpu_base_addr;
void __iomem *gic_dist_base_addr;


void __init gic_init_irq(void)
{
	/* Static mapping, never released */
	gic_dist_base_addr = ioremap(OMAP44XX_GIC_DIST_BASE, SZ_4K);
	BUG_ON(!gic_dist_base_addr);
	gic_dist_init(0, gic_dist_base_addr, 29);

	/* Static mapping, never released */
	gic_cpu_base_addr = ioremap(OMAP44XX_GIC_CPU_BASE, SZ_512);
	BUG_ON(!gic_cpu_base_addr);
	gic_cpu_init(0, gic_cpu_base_addr);
}

#ifdef CONFIG_CACHE_L2X0
static int __init omap_l2_cache_init(void)
{
	/*
	 * To avoid code running on other OMAPs in
	 * multi-omap builds
	 */
	if (!cpu_is_omap44xx())
		return -ENODEV;

	/* Static mapping, never released */
	l2cache_base = ioremap(OMAP44XX_L2CACHE_BASE, SZ_4K);
	BUG_ON(!l2cache_base);

	/* Enable PL310 L2 Cache controller */
	omap_smc1(0x102, 0x1);

	/*
	 * 32KB way size, 16-way associativity,
	 * parity disabled
	 */
	l2x0_init(l2cache_base, 0x0e050000, 0xc0000fff);

	return 0;
}
early_initcall(omap_l2_cache_init);
#endif
