/*
 *  Copyright (C) 1999,2000 Arm Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *    - add MX31 specific definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/err.h>

#include <asm/pgtable.h>
#include <asm/mach/map.h>
#include <asm/hardware/cache-l2x0.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/iomux-v3.h>

/*!
 * @file mm.c
 *
 * @brief This file creates static virtual to physical mappings, common to all MX3 boards.
 *
 * @ingroup Memory
 */

#ifdef CONFIG_SOC_IMX31
static struct map_desc mx31_io_desc[] __initdata = {
	imx_map_entry(MX31, X_MEMC, MT_DEVICE),
	imx_map_entry(MX31, AVIC, MT_DEVICE_NONSHARED),
	imx_map_entry(MX31, AIPS1, MT_DEVICE_NONSHARED),
	imx_map_entry(MX31, AIPS2, MT_DEVICE_NONSHARED),
	imx_map_entry(MX31, SPBA0, MT_DEVICE_NONSHARED),
};

/*
 * This function initializes the memory map. It is called during the
 * system startup to create static physical to virtual memory mappings
 * for the IO modules.
 */
void __init mx31_map_io(void)
{
	mxc_set_cpu_type(MXC_CPU_MX31);
	mxc_arch_reset_init(MX31_IO_ADDRESS(MX31_WDOG_BASE_ADDR));

	iotable_init(mx31_io_desc, ARRAY_SIZE(mx31_io_desc));
}

int imx31_register_gpios(void);
void __init mx31_init_irq(void)
{
	mxc_init_irq(MX31_IO_ADDRESS(MX31_AVIC_BASE_ADDR));
	imx31_register_gpios();
}
#endif /* ifdef CONFIG_SOC_IMX31 */

#ifdef CONFIG_SOC_IMX35
static struct map_desc mx35_io_desc[] __initdata = {
	imx_map_entry(MX35, X_MEMC, MT_DEVICE),
	imx_map_entry(MX35, AVIC, MT_DEVICE_NONSHARED),
	imx_map_entry(MX35, AIPS1, MT_DEVICE_NONSHARED),
	imx_map_entry(MX35, AIPS2, MT_DEVICE_NONSHARED),
	imx_map_entry(MX35, SPBA0, MT_DEVICE_NONSHARED),
};

void __init mx35_map_io(void)
{
	mxc_set_cpu_type(MXC_CPU_MX35);
	mxc_iomux_v3_init(MX35_IO_ADDRESS(MX35_IOMUXC_BASE_ADDR));
	mxc_arch_reset_init(MX35_IO_ADDRESS(MX35_WDOG_BASE_ADDR));

	iotable_init(mx35_io_desc, ARRAY_SIZE(mx35_io_desc));
}

int imx35_register_gpios(void);
void __init mx35_init_irq(void)
{
	mxc_init_irq(MX35_IO_ADDRESS(MX35_AVIC_BASE_ADDR));
	imx35_register_gpios();
}
#endif /* ifdef CONFIG_SOC_IMX35 */

#ifdef CONFIG_CACHE_L2X0
static int mxc_init_l2x0(void)
{
	void __iomem *l2x0_base;
	void __iomem *clkctl_base;
/*
 * First of all, we must repair broken chip settings. There are some
 * i.MX35 CPUs in the wild, comming with bogus L2 cache settings. These
 * misconfigured CPUs will run amok immediately when the L2 cache gets enabled.
 * Workaraound is to setup the correct register setting prior enabling the
 * L2 cache. This should not hurt already working CPUs, as they are using the
 * same value
 */
#define L2_MEM_VAL 0x10

	clkctl_base = ioremap(MX35_CLKCTL_BASE_ADDR, 4096);
	if (clkctl_base != NULL) {
		writel(0x00000515, clkctl_base + L2_MEM_VAL);
		iounmap(clkctl_base);
	} else {
		pr_err("L2 cache: Cannot fix timing. Trying to continue without\n");
	}

	l2x0_base = ioremap(MX3x_L2CC_BASE_ADDR, 4096);
	if (IS_ERR(l2x0_base)) {
		printk(KERN_ERR "remapping L2 cache area failed with %ld\n",
				PTR_ERR(l2x0_base));
		return 0;
	}

	l2x0_init(l2x0_base, 0x00030024, 0x00000000);

	return 0;
}

arch_initcall(mxc_init_l2x0);
#endif

