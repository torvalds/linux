/*
 * SMP support for Allwinner SoCs
 *
 * Copyright (C) 2013 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * Based on code
 *  Copyright (C) 2012-2013 Allwinner Ltd.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/memory.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/smp.h>

#define CPUCFG_CPU_PWR_CLAMP_STATUS_REG(cpu)	((cpu) * 0x40 + 0x64)
#define CPUCFG_CPU_RST_CTRL_REG(cpu)		(((cpu) + 1) * 0x40)
#define CPUCFG_CPU_CTRL_REG(cpu)		(((cpu) + 1) * 0x40 + 0x04)
#define CPUCFG_CPU_STATUS_REG(cpu)		(((cpu) + 1) * 0x40 + 0x08)
#define CPUCFG_GEN_CTRL_REG			0x184
#define CPUCFG_PRIVATE0_REG			0x1a4
#define CPUCFG_PRIVATE1_REG			0x1a8
#define CPUCFG_DBG_CTL0_REG			0x1e0
#define CPUCFG_DBG_CTL1_REG			0x1e4

#define PRCM_CPU_PWROFF_REG			0x100
#define PRCM_CPU_PWR_CLAMP_REG(cpu)		(((cpu) * 4) + 0x140)

static void __iomem *cpucfg_membase;
static void __iomem *prcm_membase;

static DEFINE_SPINLOCK(cpu_lock);

static void __init sun6i_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "allwinner,sun6i-a31-prcm");
	if (!node) {
		pr_err("Missing A31 PRCM node in the device tree\n");
		return;
	}

	prcm_membase = of_iomap(node, 0);
	if (!prcm_membase) {
		pr_err("Couldn't map A31 PRCM registers\n");
		return;
	}

	node = of_find_compatible_node(NULL, NULL,
				       "allwinner,sun6i-a31-cpuconfig");
	if (!node) {
		pr_err("Missing A31 CPU config node in the device tree\n");
		return;
	}

	cpucfg_membase = of_iomap(node, 0);
	if (!cpucfg_membase)
		pr_err("Couldn't map A31 CPU config registers\n");

}

static int sun6i_smp_boot_secondary(unsigned int cpu,
				    struct task_struct *idle)
{
	u32 reg;
	int i;

	if (!(prcm_membase && cpucfg_membase))
		return -EFAULT;

	spin_lock(&cpu_lock);

	/* Set CPU boot address */
	writel(virt_to_phys(secondary_startup),
	       cpucfg_membase + CPUCFG_PRIVATE0_REG);

	/* Assert the CPU core in reset */
	writel(0, cpucfg_membase + CPUCFG_CPU_RST_CTRL_REG(cpu));

	/* Assert the L1 cache in reset */
	reg = readl(cpucfg_membase + CPUCFG_GEN_CTRL_REG);
	writel(reg & ~BIT(cpu), cpucfg_membase + CPUCFG_GEN_CTRL_REG);

	/* Disable external debug access */
	reg = readl(cpucfg_membase + CPUCFG_DBG_CTL1_REG);
	writel(reg & ~BIT(cpu), cpucfg_membase + CPUCFG_DBG_CTL1_REG);

	/* Power up the CPU */
	for (i = 0; i <= 8; i++)
		writel(0xff >> i, prcm_membase + PRCM_CPU_PWR_CLAMP_REG(cpu));
	mdelay(10);

	/* Clear CPU power-off gating */
	reg = readl(prcm_membase + PRCM_CPU_PWROFF_REG);
	writel(reg & ~BIT(cpu), prcm_membase + PRCM_CPU_PWROFF_REG);
	mdelay(1);

	/* Deassert the CPU core reset */
	writel(3, cpucfg_membase + CPUCFG_CPU_RST_CTRL_REG(cpu));

	/* Enable back the external debug accesses */
	reg = readl(cpucfg_membase + CPUCFG_DBG_CTL1_REG);
	writel(reg | BIT(cpu), cpucfg_membase + CPUCFG_DBG_CTL1_REG);

	spin_unlock(&cpu_lock);

	return 0;
}

static struct smp_operations sun6i_smp_ops __initdata = {
	.smp_prepare_cpus	= sun6i_smp_prepare_cpus,
	.smp_boot_secondary	= sun6i_smp_boot_secondary,
};
CPU_METHOD_OF_DECLARE(sun6i_smp, "allwinner,sun6i-a31", &sun6i_smp_ops);
