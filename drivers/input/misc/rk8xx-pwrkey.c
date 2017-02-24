/*
 * driver/input/misc/rk8xx-pwrkey.c
 * Power Key driver for RK8xx PMIC Power Button.
 *
 * Copyright (C) 2017, Rockchip Technology Co., Ltd.
 * Author: Chen Jianhong <chenjh@rock-chips.com>
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

#include <linux/errno.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/rk808.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

struct rk8xx_pwrkey {
	struct rk808 *rk8xx;
	struct input_dev *input_dev;
	int report_key;
};

static irqreturn_t rk8xx_pwrkey_irq_falling(int irq, void *data)
{
	struct rk8xx_pwrkey *pwr = data;

	input_report_key(pwr->input_dev, pwr->report_key, 1);
	input_sync(pwr->input_dev);

	return IRQ_HANDLED;
}

static irqreturn_t rk8xx_pwrkey_irq_rising(int irq, void *data)
{
	struct rk8xx_pwrkey *pwr = data;

	input_report_key(pwr->input_dev, pwr->report_key, 0);
	input_sync(pwr->input_dev);

	return IRQ_HANDLED;
}

static int rk8xx_pwrkey_probe(struct platform_device *pdev)
{
	struct rk808 *rk8xx = dev_get_drvdata(pdev->dev.parent);
	struct rk8xx_pwrkey *pwrkey;
	int fall_irq, rise_irq, err;
	struct device_node *np;

	np = of_get_child_by_name(pdev->dev.parent->of_node, "pwrkey");
	if (np) {
		if (!of_device_is_available(np)) {
			dev_info(&pdev->dev, "device is disabled\n");
			return -EINVAL;
		}
	}

	pwrkey = devm_kzalloc(&pdev->dev,
			      sizeof(struct rk8xx_pwrkey), GFP_KERNEL);
	if (!pwrkey)
		return -ENOMEM;

	pwrkey->input_dev = devm_input_allocate_device(&pdev->dev);
	if (!pwrkey->input_dev) {
		dev_err(&pdev->dev, "Can't allocate power button\n");
		return -ENOMEM;
	}

	/* init struct input_dev */
	pwrkey->rk8xx = rk8xx;
	pwrkey->report_key = KEY_POWER;
	pwrkey->input_dev->name = "rk8xx_pwrkey";
	pwrkey->input_dev->phys = "rk8xx_pwrkey/input0";
	pwrkey->input_dev->dev.parent = pdev->dev.parent;
	pwrkey->input_dev->evbit[0] = BIT_MASK(EV_KEY);
	pwrkey->input_dev->keybit[BIT_WORD(pwrkey->report_key)] =
					BIT_MASK(pwrkey->report_key);
	platform_set_drvdata(pdev, pwrkey);

	/* requeset rise and fall irqs */
	rise_irq = platform_get_irq(pdev, 0);
	if (rise_irq < 0) {
		dev_err(&pdev->dev, "no IRQ for rise: %d\n", rise_irq);
		return rise_irq;
	}

	fall_irq = platform_get_irq(pdev, 1);
	if (fall_irq < 0) {
		dev_err(&pdev->dev, "no IRQ for fall: %d\n", fall_irq);
		return fall_irq;
	}

	err = devm_request_threaded_irq(&pdev->dev, fall_irq,
					NULL, rk8xx_pwrkey_irq_falling,
					IRQF_TRIGGER_FALLING,
					"rk8xx_pwrkey_fall", pwrkey);
	if (err) {
		dev_err(&pdev->dev, "Can't get fall irq for pwrkey: %d\n", err);
		return err;
	}
	err = devm_request_threaded_irq(&pdev->dev, rise_irq,
					NULL, rk8xx_pwrkey_irq_rising,
					IRQF_TRIGGER_RISING,
					"rk8xx_pwrkey_rise", pwrkey);
	if (err) {
		dev_err(&pdev->dev, "Can't get rise irq for pwrkey: %d\n", err);
		return err;
	}

	/* register input device */
	err = input_register_device(pwrkey->input_dev);
	if (err) {
		dev_err(&pdev->dev, "Can't register power button: %d\n", err);
		return err;
	}

	return 0;
}

static struct platform_driver rk8xx_pwrkey_driver = {
	.probe = rk8xx_pwrkey_probe,
	.driver		= {
		.name	= "rk8xx-pwrkey",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(rk8xx_pwrkey_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("RK8xx Power Button");
MODULE_AUTHOR("Chen Jianhong <chenjh@rock-chips.com>");
