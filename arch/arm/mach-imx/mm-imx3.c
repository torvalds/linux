// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 1999,2000 Arm Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *    - add MX31 specific definitions
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/pinctrl/machine.h>

#include <asm/system_misc.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/map.h>

#include "common.h"
#include "crmregs-imx3.h"
#include "hardware.h"

void __iomem *mx3_ccm_base;

static void imx3_idle(void)
{
	unsigned long reg = 0;

	__asm__ __volatile__(
		/* disable I and D cache */
		"mrc p15, 0, %0, c1, c0, 0\n"
		"bic %0, %0, #0x00001000\n"
		"bic %0, %0, #0x00000004\n"
		"mcr p15, 0, %0, c1, c0, 0\n"
		/* invalidate I cache */
		"mov %0, #0\n"
		"mcr p15, 0, %0, c7, c5, 0\n"
		/* clear and invalidate D cache */
		"mov %0, #0\n"
		"mcr p15, 0, %0, c7, c14, 0\n"
		/* WFI */
		"mov %0, #0\n"
		"mcr p15, 0, %0, c7, c0, 4\n"
		"nop\n" "nop\n" "nop\n" "nop\n"
		"nop\n" "nop\n" "nop\n"
		/* enable I and D cache */
		"mrc p15, 0, %0, c1, c0, 0\n"
		"orr %0, %0, #0x00001000\n"
		"orr %0, %0, #0x00000004\n"
		"mcr p15, 0, %0, c1, c0, 0\n"
		: "=r" (reg));
}

static void __iomem *imx3_ioremap_caller(phys_addr_t phys_addr, size_t size,
					 unsigned int mtype, void *caller)
{
	if (mtype == MT_DEVICE) {
		/*
		 * Access all peripherals below 0x80000000 as nonshared device
		 * on mx3, but leave l2cc alone.  Otherwise cache corruptions
		 * can occur.
		 */
		if (phys_addr < 0x80000000 &&
				!addr_in_module(phys_addr, MX3x_L2CC))
			mtype = MT_DEVICE_NONSHARED;
	}

	return __arm_ioremap_caller(phys_addr, size, mtype, caller);
}

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
	iotable_init(mx31_io_desc, ARRAY_SIZE(mx31_io_desc));
}

static void imx31_idle(void)
{
	int reg = imx_readl(mx3_ccm_base + MXC_CCM_CCMR);
	reg &= ~MXC_CCM_CCMR_LPM_MASK;
	imx_writel(reg, mx3_ccm_base + MXC_CCM_CCMR);

	imx3_idle();
}

void __init imx31_init_early(void)
{
	struct device_node *np;

	mxc_set_cpu_type(MXC_CPU_MX31);
	arch_ioremap_caller = imx3_ioremap_caller;
	arm_pm_idle = imx31_idle;
	np = of_find_compatible_node(NULL, NULL, "fsl,imx31-ccm");
	mx3_ccm_base = of_iomap(np, 0);
	BUG_ON(!mx3_ccm_base);
}

void __init mx31_init_irq(void)
{
	void __iomem *avic_base;
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx31-avic");
	avic_base = of_iomap(np, 0);
	BUG_ON(!avic_base);

	mxc_init_irq(avic_base);
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
	iotable_init(mx35_io_desc, ARRAY_SIZE(mx35_io_desc));
}

static void imx35_idle(void)
{
	int reg = imx_readl(mx3_ccm_base + MXC_CCM_CCMR);
	reg &= ~MXC_CCM_CCMR_LPM_MASK;
	reg |= MXC_CCM_CCMR_LPM_WAIT_MX35;
	imx_writel(reg, mx3_ccm_base + MXC_CCM_CCMR);

	imx3_idle();
}

void __init imx35_init_early(void)
{
	struct device_node *np;

	mxc_set_cpu_type(MXC_CPU_MX35);
	arm_pm_idle = imx35_idle;
	arch_ioremap_caller = imx3_ioremap_caller;
	np = of_find_compatible_node(NULL, NULL, "fsl,imx35-ccm");
	mx3_ccm_base = of_iomap(np, 0);
	BUG_ON(!mx3_ccm_base);
}

void __init mx35_init_irq(void)
{
	void __iomem *avic_base;
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx35-avic");
	avic_base = of_iomap(np, 0);
	BUG_ON(!avic_base);

	mxc_init_irq(avic_base);
}
#endif /* ifdef CONFIG_SOC_IMX35 */
