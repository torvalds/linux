/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *              htt://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/cache.h>
#include <linux/syscore_ops.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <asm/cacheflush.h>

#include <plat/cpu.h>
#include <plat/cci.h>

#include <mach/map.h>
#include <mach/regs-cci.h>

static void __iomem *cci_base;
static void __iomem *core_misc_base;
static int cci_enabled __read_mostly;

int dev_cci_snoop_control(enum cci_device_name name,
			enum dev_cci_snoop_control cntl)
{
	void __iomem *control_reg;

	if (!cci_enabled)
		return -EPERM;

	switch (name) {
	case MDMA:
		control_reg = core_misc_base + MDMA_SHARED_CTRL;
		break;
	case SSS:
		control_reg = core_misc_base + SSS_SHARED_CTRL;
		break;
	case G2D:
		control_reg = core_misc_base + G2D_SHARED_CTRL;
		break;
	default:
		return -EINVAL;
	}

	switch (cntl) {
	case DISABLE_BY_SFR:
	case ENABLE_BY_SFR:
	case CONTROL_BY_SMMU:
		writel(cntl, control_reg);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

void enable_cci_snoops(unsigned int cluster_id)
{
	void __iomem *control_reg;

	if (!cci_enabled)
		return;

	if (samsung_rev() < EXYNOS5410_REV_1_0)
		cluster_id ^= 1;

	if (cluster_id)
		control_reg = CCI_A7_SL_IFACE(cci_base) + SNOOP_CTLR_REG;
	else
		control_reg = CCI_A15_SL_IFACE(cci_base) + SNOOP_CTLR_REG;

	if ((readl(control_reg) & 0x3) == 0x3)
		return;

	/* Turn on CCI snoops & DVM Messages */
	writel(0x3, control_reg);
	dsb();

	/* Wait for the dust to settle down */
	while (readl(cci_base + STATUS_REG) & 0x1);

	return;
}

void disable_cci_snoops(unsigned int cluster_id)
{
	void __iomem *control_reg;

	if (!cci_enabled)
		return;

	if (samsung_rev() < EXYNOS5410_REV_1_0)
		cluster_id ^= 1;

	if (cluster_id)
		control_reg = CCI_A7_SL_IFACE(cci_base) + SNOOP_CTLR_REG;
	else
		control_reg = CCI_A15_SL_IFACE(cci_base) + SNOOP_CTLR_REG;

	if (!(readl(control_reg) & 0x3))
		return;

	/* Turn off CCI snoops & DVM messages */
	writel(0, control_reg);
	dsb();

	/* Wait for the dust to settle down */
	while (readl(cci_base + STATUS_REG) & 0x1);

	return;
}

static int get_cci_snoop_status(unsigned int cluster_id)
{
	void __iomem *control_reg;

	if (!cci_enabled)
		return 0;

	if (samsung_rev() < EXYNOS5410_REV_1_0)
		cluster_id ^= 1;

	if (cluster_id)
		control_reg = CCI_A7_SL_IFACE(cci_base) + SNOOP_CTLR_REG;
	else
		control_reg = CCI_A15_SL_IFACE(cci_base) + SNOOP_CTLR_REG;

	if ((readl(control_reg) & 0x3))
		return 1;

	return 0;
}

/*
 * Use our own MPIDR accessors as the generic ones in asm/cputype.h have
 * __attribute_const__ and we don't want the compiler to assume any
 * constness here.
 */

static int read_mpidr(void)
{
	unsigned int id;
	asm volatile ("mrc\tp15, 0, %0, c0, c0, 5" : "=r" (id));
	return id;
}

#if defined(CONFIG_PM)
static int cci_status[2];

static int cci_suspend(void)
{
	int i;

	if (!cci_enabled)
		return 0;

	for (i = 0; i < 2; i++)
		cci_status[i] = get_cci_snoop_status(i);

	return 0;
}

static void cci_resume(void)
{
	int i;

	if (!cci_enabled) {
		writel(0x1, core_misc_base);
		return;
	}

	for (i = 0; i < 2; i++)
		if (cci_status[i])
			enable_cci_snoops(i);
}
#else
#define cci_suspend NULL
#define cci_resume NULL
#endif

static struct syscore_ops cci_syscore_ops = {
	.suspend	= cci_suspend,
	.resume		= cci_resume,
};

#ifndef CONFIG_EXYNOS5_CCI
/*
 * This function is used for checking CCI hw configuration
 * It CCI hw is not disabled, kernel panic is occurred by
 * this function.
 */
static void cci_check_hw(void)
{
	void __iomem *trp_a15_base;
	void __iomem *statfilter_a15_base;
	void __iomem *statprofiler_a15_base;
	unsigned int tmp;

	trp_a15_base = ioremap(EXYNOS5410_NOCP_TRP_EAGLE_BASE, 0x1000);
	if (!trp_a15_base)
		goto out_1;

	statfilter_a15_base = ioremap(0x10CA0000, 0x1000);
	if (!statfilter_a15_base)
		goto out_2;

	statprofiler_a15_base = ioremap(0x10CB2000, 0x1000);
	if (!statprofiler_a15_base)
		goto out_3;


	/* Configure Statfilter */
	__raw_writel(0x1, statfilter_a15_base + 0x8);
	__raw_writel(0x3, statfilter_a15_base + 0x20);
	__raw_writel(0x0, statfilter_a15_base + 0xC);
	__raw_writel(0x3f, statfilter_a15_base + 0x14);

	/* Configure StatProfile */
	__raw_writel(0x0, statprofiler_a15_base + 0xC);
	__raw_writel(0x0, statprofiler_a15_base + 0x2C);

	/* Configure Probe */
	__raw_writel(0x0, trp_a15_base + 0x24);
	__raw_writel(0x20, trp_a15_base + 0x138);
	__raw_writel(0x3, trp_a15_base + 0x13C);
	__raw_writel(0x0, trp_a15_base + 0x2C);
	__raw_writel(0x0, trp_a15_base + 0x30);
	__raw_writel(0x18, trp_a15_base + 0x8);
	__raw_writel(0x1, trp_a15_base + 0xC);

	__raw_writel(0x1, statprofiler_a15_base + 0x8);

	/* Make term before reading counter */
	flush_cache_all();

	tmp = __raw_readl(trp_a15_base + 0x140);

	/* Disable */
	__raw_writel(0x0, trp_a15_base + 0x8);
	__raw_writel(0x0, trp_a15_base + 0xC);


	if (!tmp) {
		pr_err("***** CCI is not disabled, Please check board type *****\n");
		panic("CCI is not disabled! Do not use this board!\n");
	}

	iounmap(statprofiler_a15_base);
out_3:
	iounmap(statfilter_a15_base);
out_2:
	iounmap(trp_a15_base);
out_1:
	return;
}
#endif

static int __init cci_init(void)
{
	unsigned int cluster_id = (read_mpidr() >> 8) & 0xf;
	int err;

#if defined(CONFIG_EXYNOS5_CCI)
	if (soc_is_exynos5410())
		cci_enabled = 1;
#endif

	if (!cci_enabled) {
		pr_info("CCI is not supported");

		core_misc_base = ioremap(EXYNOS5_PA_CORE_MISC, 0x1000);
		writel(0x1, core_misc_base);

#ifndef CONFIG_EXYNOS5_CCI
		cci_check_hw();
#endif
		goto disabled;
	}

	cci_base = ioremap(EXYNOS5_PA_CCI, 0x10000);
	if (!cci_base) {
		err = -ENOMEM;
		goto out;
	}

	core_misc_base = ioremap(EXYNOS5_PA_CORE_MISC, 0x1000);
	if (!core_misc_base) {
		err = -ENOMEM;
		goto out_unmap;
	}

	enable_cci_snoops(cluster_id);

disabled:
	register_syscore_ops(&cci_syscore_ops);

	return 0;

out_unmap:
	iounmap(cci_base);
out:
	return err;
}

arch_initcall(cci_init);
