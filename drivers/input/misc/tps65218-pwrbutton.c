/*
 * Texas Instruments' TPS65218 Power Button Input Driver
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Felipe Balbi <balbi@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/tps65218.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct tps65218_pwrbutton {
	struct device *dev;
	struct tps65218 *tps;
	struct input_dev *idev;
};

static irqreturn_t tps65218_pwr_irq(int irq, void *_pwr)
{
	struct tps65218_pwrbutton *pwr = _pwr;
	unsigned int reg;
	int error;

	error = tps65218_reg_read(pwr->tps, TPS65218_REG_STATUS, &reg);
	if (error) {
		dev_err(pwr->dev, "can't read register: %d\n", error);
		goto out;
	}

	if (reg & TPS65218_STATUS_PB_STATE) {
		input_report_key(pwr->idev, KEY_POWER, 1);
		pm_wakeup_event(pwr->dev, 0);
	} else {
		input_report_key(pwr->idev, KEY_POWER, 0);
	}

	input_sync(pwr->idev);

out:
	return IRQ_HANDLED;
}

static int tps65218_pwron_probe(struct platform_device *pdev)
{
	struct tps65218 *tps = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct tps65218_pwrbutton *pwr;
	struct input_dev *idev;
	int error;
	int irq;

	pwr = devm_kzalloc(dev, sizeof(*pwr), GFP_KERNEL);
	if (!pwr)
		return -ENOMEM;

	idev = devm_input_allocate_device(dev);
	if (!idev)
		return -ENOMEM;

	idev->name = "tps65218_pwrbutton";
	idev->phys = "tps65218_pwrbutton/input0";
	idev->dev.parent = dev;
	idev->id.bustype = BUS_I2C;

	input_set_capability(idev, EV_KEY, KEY_POWER);

	pwr->tps = tps;
	pwr->dev = dev;
	pwr->idev = idev;
	platform_set_drvdata(pdev, pwr);
	device_init_wakeup(dev, true);

	irq = platform_get_irq(pdev, 0);
	error = devm_request_threaded_irq(dev, irq, NULL, tps65218_pwr_irq,
					  IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
					  "tps65218-pwrbutton", pwr);
	if (error) {
		dev_err(dev, "failed to request IRQ #%d: %d\n",
			irq, error);
		return error;
	}

	error= input_register_device(idev);
	if (error) {
		dev_err(dev, "Can't register power button: %d\n", error);
		return error;
	}

	return 0;
}

static const struct of_device_id of_tps65218_pwr_match[] = {
	{ .compatible = "ti,tps65218-pwrbutton" },
	{ },
};
MODULE_DEVICE_TABLE(of, of_tps65218_pwr_match);

static struct platform_driver tps65218_pwron_driver = {
	.probe	= tps65218_pwron_probe,
	.driver	= {
		.name	= "tps65218_pwrbutton",
		.of_match_table = of_tps65218_pwr_match,
	},
};
module_platform_driver(tps65218_pwron_driver);

MODULE_DESCRIPTION("TPS65218 Power Button");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Felipe Balbi <balbi@ti.com>");
