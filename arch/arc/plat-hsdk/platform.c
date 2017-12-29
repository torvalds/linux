/*
 * ARC HSDK Platform support code
 *
 * Copyright (C) 2017 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <asm/arcregs.h>
#include <asm/io.h>
#include <asm/mach_desc.h>

#define ARC_CCM_UNUSED_ADDR	0x60000000

static void __init hsdk_init_per_cpu(unsigned int cpu)
{
	/*
	 * By default ICCM is mapped to 0x7z while this area is used for
	 * kernel virtual mappings, so move it to currently unused area.
	 */
	if (cpuinfo_arc700[cpu].iccm.sz)
		write_aux_reg(ARC_REG_AUX_ICCM, ARC_CCM_UNUSED_ADDR);

	/*
	 * By default DCCM is mapped to 0x8z while this area is used by kernel,
	 * so move it to currently unused area.
	 */
	if (cpuinfo_arc700[cpu].dccm.sz)
		write_aux_reg(ARC_REG_AUX_DCCM, ARC_CCM_UNUSED_ADDR);
}

#define ARC_PERIPHERAL_BASE	0xf0000000
#define CREG_BASE		(ARC_PERIPHERAL_BASE + 0x1000)
#define CREG_PAE		(CREG_BASE + 0x180)
#define CREG_PAE_UPDATE		(CREG_BASE + 0x194)

#define CREG_CORE_IF_CLK_DIV	(CREG_BASE + 0x4B8)
#define CREG_CORE_IF_CLK_DIV_2	0x1
#define CGU_BASE		ARC_PERIPHERAL_BASE
#define CGU_PLL_STATUS		(ARC_PERIPHERAL_BASE + 0x4)
#define CGU_PLL_CTRL		(ARC_PERIPHERAL_BASE + 0x0)
#define CGU_PLL_STATUS_LOCK	BIT(0)
#define CGU_PLL_STATUS_ERR	BIT(1)
#define CGU_PLL_CTRL_1GHZ	0x3A10
#define HSDK_PLL_LOCK_TIMEOUT	500

#define HSDK_PLL_LOCKED() \
	!!(ioread32((void __iomem *) CGU_PLL_STATUS) & CGU_PLL_STATUS_LOCK)

#define HSDK_PLL_ERR() \
	!!(ioread32((void __iomem *) CGU_PLL_STATUS) & CGU_PLL_STATUS_ERR)

static void __init hsdk_set_cpu_freq_1ghz(void)
{
	u32 timeout = HSDK_PLL_LOCK_TIMEOUT;

	/*
	 * As we set cpu clock which exceeds 500MHz, the divider for the interface
	 * clock must be programmed to div-by-2.
	 */
	iowrite32(CREG_CORE_IF_CLK_DIV_2, (void __iomem *) CREG_CORE_IF_CLK_DIV);

	/* Set cpu clock to 1GHz */
	iowrite32(CGU_PLL_CTRL_1GHZ, (void __iomem *) CGU_PLL_CTRL);

	while (!HSDK_PLL_LOCKED() && timeout--)
		cpu_relax();

	if (!HSDK_PLL_LOCKED() || HSDK_PLL_ERR())
		pr_err("Failed to setup CPU frequency to 1GHz!");
}

#define SDIO_BASE		(ARC_PERIPHERAL_BASE + 0xA000)
#define SDIO_UHS_REG_EXT	(SDIO_BASE + 0x108)
#define SDIO_UHS_REG_EXT_DIV_2	(2 << 30)

static void __init hsdk_init_early(void)
{
	/*
	 * PAE remapping for DMA clients does not work due to an RTL bug, so
	 * CREG_PAE register must be programmed to all zeroes, otherwise it
	 * will cause problems with DMA to/from peripherals even if PAE40 is
	 * not used.
	 */

	/* Default is 1, which means "PAE offset = 4GByte" */
	writel_relaxed(0, (void __iomem *) CREG_PAE);

	/* Really apply settings made above */
	writel(1, (void __iomem *) CREG_PAE_UPDATE);

	/*
	 * Switch SDIO external ciu clock divider from default div-by-8 to
	 * minimum possible div-by-2.
	 */
	iowrite32(SDIO_UHS_REG_EXT_DIV_2, (void __iomem *) SDIO_UHS_REG_EXT);

	/*
	 * Setup CPU frequency to 1GHz.
	 * TODO: remove it after smart hsdk pll driver will be introduced.
	 */
	hsdk_set_cpu_freq_1ghz();
}

static const char *hsdk_compat[] __initconst = {
	"snps,hsdk",
	NULL,
};

MACHINE_START(SIMULATION, "hsdk")
	.dt_compat	= hsdk_compat,
	.init_early     = hsdk_init_early,
	.init_per_cpu	= hsdk_init_per_cpu,
MACHINE_END
