// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MPC85xx PM operators
 *
 * Copyright 2015 Freescale Semiconductor Inc.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/fsl/guts.h>

#include <asm/io.h>
#include <asm/fsl_pm.h>

static struct ccsr_guts __iomem *guts;

static void mpc85xx_irq_mask(int cpu)
{

}

static void mpc85xx_irq_unmask(int cpu)
{

}

static void mpc85xx_cpu_die(int cpu)
{
	u32 tmp;

	tmp = (mfspr(SPRN_HID0) & ~(HID0_DOZE|HID0_SLEEP)) | HID0_NAP;
	mtspr(SPRN_HID0, tmp);

	/* Enter NAP mode. */
	tmp = mfmsr();
	tmp |= MSR_WE;
	asm volatile(
		"msync\n"
		"mtmsr %0\n"
		"isync\n"
		:
		: "r" (tmp));
}

static void mpc85xx_cpu_up_prepare(int cpu)
{

}

static void mpc85xx_freeze_time_base(bool freeze)
{
	uint32_t mask;

	mask = CCSR_GUTS_DEVDISR_TB0 | CCSR_GUTS_DEVDISR_TB1;
	if (freeze)
		setbits32(&guts->devdisr, mask);
	else
		clrbits32(&guts->devdisr, mask);

	in_be32(&guts->devdisr);
}

static const struct of_device_id mpc85xx_smp_guts_ids[] = {
	{ .compatible = "fsl,mpc8572-guts", },
	{ .compatible = "fsl,p1020-guts", },
	{ .compatible = "fsl,p1021-guts", },
	{ .compatible = "fsl,p1022-guts", },
	{ .compatible = "fsl,p1023-guts", },
	{ .compatible = "fsl,p2020-guts", },
	{ .compatible = "fsl,bsc9132-guts", },
	{},
};

static const struct fsl_pm_ops mpc85xx_pm_ops = {
	.freeze_time_base = mpc85xx_freeze_time_base,
	.irq_mask = mpc85xx_irq_mask,
	.irq_unmask = mpc85xx_irq_unmask,
	.cpu_die = mpc85xx_cpu_die,
	.cpu_up_prepare = mpc85xx_cpu_up_prepare,
};

int __init mpc85xx_setup_pmc(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, mpc85xx_smp_guts_ids);
	if (np) {
		guts = of_iomap(np, 0);
		of_node_put(np);
		if (!guts) {
			pr_err("Could not map guts node address\n");
			return -ENOMEM;
		}
		qoriq_pm_ops = &mpc85xx_pm_ops;
	}

	return 0;
}
