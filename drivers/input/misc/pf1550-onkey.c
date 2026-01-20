// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the PF1550 ONKEY
 * Copyright (C) 2016 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * Portions Copyright (c) 2025 Savoir-faire Linux Inc.
 * Samuel Kayode <samuel.kayode@savoirfairelinux.com>
 */

#include <linux/err.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/pf1550.h>
#include <linux/platform_device.h>

#define PF1550_ONKEY_IRQ_NR	6

struct onkey_drv_data {
	struct device *dev;
	const struct pf1550_ddata *pf1550;
	bool wakeup;
	struct input_dev *input;
};

static irqreturn_t pf1550_onkey_irq_handler(int irq, void *data)
{
	struct onkey_drv_data *onkey = data;
	struct platform_device *pdev = to_platform_device(onkey->dev);
	int i, state, irq_type = -1;

	for (i = 0; i < PF1550_ONKEY_IRQ_NR; i++)
		if (irq == platform_get_irq(pdev, i))
			irq_type = i;

	switch (irq_type) {
	case PF1550_ONKEY_IRQ_PUSHI:
		state = 0;
		break;
	case PF1550_ONKEY_IRQ_1SI:
	case PF1550_ONKEY_IRQ_2SI:
	case PF1550_ONKEY_IRQ_3SI:
	case PF1550_ONKEY_IRQ_4SI:
	case PF1550_ONKEY_IRQ_8SI:
		state = 1;
		break;
	default:
		dev_err(onkey->dev, "onkey interrupt: irq %d occurred\n",
			irq_type);
		return IRQ_HANDLED;
	}

	input_event(onkey->input, EV_KEY, KEY_POWER, state);
	input_sync(onkey->input);

	return IRQ_HANDLED;
}

static int pf1550_onkey_probe(struct platform_device *pdev)
{
	struct onkey_drv_data *onkey;
	struct input_dev *input;
	bool key_power = false;
	int i, irq, error;

	onkey = devm_kzalloc(&pdev->dev, sizeof(*onkey), GFP_KERNEL);
	if (!onkey)
		return -ENOMEM;

	onkey->dev = &pdev->dev;

	onkey->pf1550 = dev_get_drvdata(pdev->dev.parent);
	if (!onkey->pf1550->regmap)
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "failed to get regmap\n");

	onkey->wakeup = device_property_read_bool(pdev->dev.parent,
						  "wakeup-source");

	if (device_property_read_bool(pdev->dev.parent,
				      "nxp,disable-key-power")) {
		error = regmap_clear_bits(onkey->pf1550->regmap,
					  PF1550_PMIC_REG_PWRCTRL1,
					  PF1550_ONKEY_RST_EN);
		if (error)
			return dev_err_probe(&pdev->dev, error,
					     "failed: disable turn system off");
	} else {
		key_power = true;
	}

	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return dev_err_probe(&pdev->dev, -ENOMEM,
				     "failed to allocate the input device\n");

	input->name = pdev->name;
	input->phys = "pf1550-onkey/input0";
	input->id.bustype = BUS_HOST;

	if (key_power)
		input_set_capability(input, EV_KEY, KEY_POWER);

	onkey->input = input;
	platform_set_drvdata(pdev, onkey);

	for (i = 0; i < PF1550_ONKEY_IRQ_NR; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0)
			return irq;

		error = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						  pf1550_onkey_irq_handler,
						  IRQF_NO_SUSPEND,
						  "pf1550-onkey", onkey);
		if (error)
			return dev_err_probe(&pdev->dev, error,
					     "failed: irq request (IRQ: %d)\n",
					     i);
	}

	error = input_register_device(input);
	if (error)
		return dev_err_probe(&pdev->dev, error,
				     "failed to register input device\n");

	device_init_wakeup(&pdev->dev, onkey->wakeup);

	return 0;
}

static int pf1550_onkey_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct onkey_drv_data *onkey = platform_get_drvdata(pdev);
	int i, irq;

	if (!device_may_wakeup(&pdev->dev))
		regmap_write(onkey->pf1550->regmap,
			     PF1550_PMIC_REG_ONKEY_INT_MASK0,
			     ONKEY_IRQ_PUSHI | ONKEY_IRQ_1SI | ONKEY_IRQ_2SI |
			     ONKEY_IRQ_3SI | ONKEY_IRQ_4SI | ONKEY_IRQ_8SI);
	else
		for (i = 0; i < PF1550_ONKEY_IRQ_NR; i++) {
			irq = platform_get_irq(pdev, i);
			if (irq > 0)
				enable_irq_wake(irq);
		}

	return 0;
}

static int pf1550_onkey_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct onkey_drv_data *onkey = platform_get_drvdata(pdev);
	int i, irq;

	if (!device_may_wakeup(&pdev->dev))
		regmap_write(onkey->pf1550->regmap,
			     PF1550_PMIC_REG_ONKEY_INT_MASK0,
			     ~((u8)(ONKEY_IRQ_PUSHI | ONKEY_IRQ_1SI |
			     ONKEY_IRQ_2SI | ONKEY_IRQ_3SI | ONKEY_IRQ_4SI |
			     ONKEY_IRQ_8SI)));
	else
		for (i = 0; i < PF1550_ONKEY_IRQ_NR; i++) {
			irq = platform_get_irq(pdev, i);
			if (irq > 0)
				disable_irq_wake(irq);
		}

	return 0;
}

static SIMPLE_DEV_PM_OPS(pf1550_onkey_pm_ops, pf1550_onkey_suspend,
			 pf1550_onkey_resume);

static const struct platform_device_id pf1550_onkey_id[] = {
	{ "pf1550-onkey", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, pf1550_onkey_id);

static struct platform_driver pf1550_onkey_driver = {
	.driver = {
		.name = "pf1550-onkey",
		.pm   = pm_sleep_ptr(&pf1550_onkey_pm_ops),
	},
	.probe = pf1550_onkey_probe,
	.id_table = pf1550_onkey_id,
};
module_platform_driver(pf1550_onkey_driver);

MODULE_AUTHOR("Freescale Semiconductor");
MODULE_DESCRIPTION("PF1550 onkey Driver");
MODULE_LICENSE("GPL");
