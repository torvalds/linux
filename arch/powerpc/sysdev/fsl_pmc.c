/*
 * Suspend/resume support
 *
 * Copyright 2009  MontaVista Software, Inc.
 *
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/of_platform.h>

struct pmc_regs {
	__be32 devdisr;
	__be32 devdisr2;
	__be32 :32;
	__be32 :32;
	__be32 pmcsr;
#define PMCSR_SLP	(1 << 17)
};

static struct device *pmc_dev;
static struct pmc_regs __iomem *pmc_regs;

static int pmc_suspend_enter(suspend_state_t state)
{
	int ret;

	setbits32(&pmc_regs->pmcsr, PMCSR_SLP);
	/* At this point, the CPU is asleep. */

	/* Upon resume, wait for SLP bit to be clear. */
	ret = spin_event_timeout((in_be32(&pmc_regs->pmcsr) & PMCSR_SLP) == 0,
				 10000, 10) ? 0 : -ETIMEDOUT;
	if (ret)
		dev_err(pmc_dev, "tired waiting for SLP bit to clear\n");
	return ret;
}

static int pmc_suspend_valid(suspend_state_t state)
{
	if (state != PM_SUSPEND_STANDBY)
		return 0;
	return 1;
}

static const struct platform_suspend_ops pmc_suspend_ops = {
	.valid = pmc_suspend_valid,
	.enter = pmc_suspend_enter,
};

static int pmc_probe(struct platform_device *ofdev)
{
	pmc_regs = of_iomap(ofdev->dev.of_node, 0);
	if (!pmc_regs)
		return -ENOMEM;

	pmc_dev = &ofdev->dev;
	suspend_set_ops(&pmc_suspend_ops);
	return 0;
}

static const struct of_device_id pmc_ids[] = {
	{ .compatible = "fsl,mpc8548-pmc", },
	{ .compatible = "fsl,mpc8641d-pmc", },
	{ },
};

static struct platform_driver pmc_driver = {
	.driver = {
		.name = "fsl-pmc",
		.owner = THIS_MODULE,
		.of_match_table = pmc_ids,
	},
	.probe = pmc_probe,
};

static int __init pmc_init(void)
{
	return platform_driver_register(&pmc_driver);
}
device_initcall(pmc_init);
