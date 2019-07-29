// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2013 John Crispin <john@phrozen.org>
 */

#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>

#include <asm/mach-ralink/ralink_regs.h>

#define REG_ILL_ACC_ADDR	0x10
#define REG_ILL_ACC_TYPE	0x14

#define ILL_INT_STATUS		BIT(31)
#define ILL_ACC_WRITE		BIT(30)
#define ILL_ACC_LEN_M		0xff
#define ILL_ACC_OFF_M		0xf
#define ILL_ACC_OFF_S		16
#define ILL_ACC_ID_M		0x7
#define ILL_ACC_ID_S		8

#define	DRV_NAME		"ill_acc"

static const char * const ill_acc_ids[] = {
	"cpu", "dma", "ppe", "pdma rx", "pdma tx", "pci/e", "wmac", "usb",
};

static irqreturn_t ill_acc_irq_handler(int irq, void *_priv)
{
	struct device *dev = (struct device *) _priv;
	u32 addr = rt_memc_r32(REG_ILL_ACC_ADDR);
	u32 type = rt_memc_r32(REG_ILL_ACC_TYPE);

	dev_err(dev, "illegal %s access from %s - addr:0x%08x offset:%d len:%d\n",
		(type & ILL_ACC_WRITE) ? ("write") : ("read"),
		ill_acc_ids[(type >> ILL_ACC_ID_S) & ILL_ACC_ID_M],
		addr, (type >> ILL_ACC_OFF_S) & ILL_ACC_OFF_M,
		type & ILL_ACC_LEN_M);

	rt_memc_w32(ILL_INT_STATUS, REG_ILL_ACC_TYPE);

	return IRQ_HANDLED;
}

static int __init ill_acc_of_setup(void)
{
	struct platform_device *pdev;
	struct device_node *np;
	int irq;

	/* somehow this driver breaks on RT5350 */
	if (of_machine_is_compatible("ralink,rt5350-soc"))
		return -EINVAL;

	np = of_find_compatible_node(NULL, NULL, "ralink,rt3050-memc");
	if (!np)
		return -EINVAL;

	pdev = of_find_device_by_node(np);
	if (!pdev) {
		pr_err("%pOFn: failed to lookup pdev\n", np);
		return -EINVAL;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		dev_err(&pdev->dev, "failed to get irq\n");
		return -EINVAL;
	}

	if (request_irq(irq, ill_acc_irq_handler, 0, "ill_acc", &pdev->dev)) {
		dev_err(&pdev->dev, "failed to request irq\n");
		return -EINVAL;
	}

	rt_memc_w32(ILL_INT_STATUS, REG_ILL_ACC_TYPE);

	dev_info(&pdev->dev, "irq registered\n");

	return 0;
}

arch_initcall(ill_acc_of_setup);
