// SPDX-License-Identifier: GPL-2.0
/*
 * Power-button driver for Basin Cove PMIC
 *
 * Copyright (c) 2019, Intel Corporation.
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/mfd/intel_soc_pmic_mrfld.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/slab.h>

#define BCOVE_PBSTATUS		0x27
#define BCOVE_PBSTATUS_PBLVL	BIT(4)	/* 1 - release, 0 - press */

static irqreturn_t mrfld_pwrbtn_interrupt(int irq, void *dev_id)
{
	struct input_dev *input = dev_id;
	struct device *dev = input->dev.parent;
	struct regmap *regmap = dev_get_drvdata(dev);
	unsigned int state;
	int ret;

	ret = regmap_read(regmap, BCOVE_PBSTATUS, &state);
	if (ret)
		return IRQ_NONE;

	dev_dbg(dev, "PBSTATUS=0x%x\n", state);
	input_report_key(input, KEY_POWER, !(state & BCOVE_PBSTATUS_PBLVL));
	input_sync(input);

	regmap_update_bits(regmap, BCOVE_MIRQLVL1, BCOVE_LVL1_PWRBTN, 0);
	return IRQ_HANDLED;
}

static int mrfld_pwrbtn_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev->parent);
	struct input_dev *input;
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;
	input->name = pdev->name;
	input->phys = "power-button/input0";
	input->id.bustype = BUS_HOST;
	input->dev.parent = dev;
	input_set_capability(input, EV_KEY, KEY_POWER);
	ret = input_register_device(input);
	if (ret)
		return ret;

	dev_set_drvdata(dev, pmic->regmap);

	ret = devm_request_threaded_irq(dev, irq, NULL, mrfld_pwrbtn_interrupt,
					IRQF_ONESHOT | IRQF_SHARED, pdev->name,
					input);
	if (ret)
		return ret;

	regmap_update_bits(pmic->regmap, BCOVE_MIRQLVL1, BCOVE_LVL1_PWRBTN, 0);
	regmap_update_bits(pmic->regmap, BCOVE_MPBIRQ, BCOVE_PBIRQ_PBTN, 0);

	device_init_wakeup(dev, true);
	dev_pm_set_wake_irq(dev, irq);
	return 0;
}

static void mrfld_pwrbtn_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	dev_pm_clear_wake_irq(dev);
	device_init_wakeup(dev, false);
}

static const struct platform_device_id mrfld_pwrbtn_id_table[] = {
	{ .name = "mrfld_bcove_pwrbtn" },
	{}
};
MODULE_DEVICE_TABLE(platform, mrfld_pwrbtn_id_table);

static struct platform_driver mrfld_pwrbtn_driver = {
	.driver = {
		.name	= "mrfld_bcove_pwrbtn",
	},
	.probe		= mrfld_pwrbtn_probe,
	.remove_new	= mrfld_pwrbtn_remove,
	.id_table	= mrfld_pwrbtn_id_table,
};
module_platform_driver(mrfld_pwrbtn_driver);

MODULE_DESCRIPTION("Power-button driver for Basin Cove PMIC");
MODULE_LICENSE("GPL v2");
