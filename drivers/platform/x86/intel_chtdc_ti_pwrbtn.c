/*
 * Power-button driver for Dollar Cove TI PMIC
 * Copyright (C) 2014 Intel Corp
 * Copyright (c) 2017 Takashi Iwai <tiwai@suse.de>
 */

#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/slab.h>

#define CHTDC_TI_SIRQ_REG	0x3
#define SIRQ_PWRBTN_REL		BIT(0)

static irqreturn_t chtdc_ti_pwrbtn_interrupt(int irq, void *dev_id)
{
	struct input_dev *input = dev_id;
	struct device *dev = input->dev.parent;
	struct regmap *regmap = dev_get_drvdata(dev);
	int state;

	if (!regmap_read(regmap, CHTDC_TI_SIRQ_REG, &state)) {
		dev_dbg(dev, "SIRQ_REG=0x%x\n", state);
		input_report_key(input, KEY_POWER, !(state & SIRQ_PWRBTN_REL));
		input_sync(input);
	}

	return IRQ_HANDLED;
}

static int chtdc_ti_pwrbtn_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev->parent);
	struct input_dev *input;
	int irq, err;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;
	input->name = pdev->name;
	input->phys = "power-button/input0";
	input->id.bustype = BUS_HOST;
	input_set_capability(input, EV_KEY, KEY_POWER);
	err = input_register_device(input);
	if (err)
		return err;

	dev_set_drvdata(dev, pmic->regmap);

	err = devm_request_threaded_irq(dev, irq, NULL,
					chtdc_ti_pwrbtn_interrupt,
					0, KBUILD_MODNAME, input);
	if (err)
		return err;

	device_init_wakeup(dev, true);
	dev_pm_set_wake_irq(dev, irq);
	return 0;
}

static int chtdc_ti_pwrbtn_remove(struct platform_device *pdev)
{
	dev_pm_clear_wake_irq(&pdev->dev);
	device_init_wakeup(&pdev->dev, false);
	return 0;
}

static const struct platform_device_id chtdc_ti_pwrbtn_id_table[] = {
	{ .name = "chtdc_ti_pwrbtn" },
	{},
};
MODULE_DEVICE_TABLE(platform, chtdc_ti_pwrbtn_id_table);

static struct platform_driver chtdc_ti_pwrbtn_driver = {
	.driver = {
		.name	= KBUILD_MODNAME,
	},
	.probe		= chtdc_ti_pwrbtn_probe,
	.remove		= chtdc_ti_pwrbtn_remove,
	.id_table	= chtdc_ti_pwrbtn_id_table,
};
module_platform_driver(chtdc_ti_pwrbtn_driver);

MODULE_DESCRIPTION("Power-button driver for Dollar Cove TI PMIC");
MODULE_LICENSE("GPL v2");
