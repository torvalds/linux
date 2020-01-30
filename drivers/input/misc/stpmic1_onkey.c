// SPDX-License-Identifier: GPL-2.0
// Copyright (C) STMicroelectronics 2018
// Author: Pascal Paillet <p.paillet@st.com> for STMicroelectronics.

#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mfd/stpmic1.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

/**
 * struct stpmic1_onkey - OnKey data
 * @input_dev:		pointer to input device
 * @irq_falling:	irq that we are hooked on to
 * @irq_rising:		irq that we are hooked on to
 */
struct stpmic1_onkey {
	struct input_dev *input_dev;
	int irq_falling;
	int irq_rising;
};

static irqreturn_t onkey_falling_irq(int irq, void *ponkey)
{
	struct stpmic1_onkey *onkey = ponkey;
	struct input_dev *input_dev = onkey->input_dev;

	input_report_key(input_dev, KEY_POWER, 1);
	pm_wakeup_event(input_dev->dev.parent, 0);
	input_sync(input_dev);

	return IRQ_HANDLED;
}

static irqreturn_t onkey_rising_irq(int irq, void *ponkey)
{
	struct stpmic1_onkey *onkey = ponkey;
	struct input_dev *input_dev = onkey->input_dev;

	input_report_key(input_dev, KEY_POWER, 0);
	pm_wakeup_event(input_dev->dev.parent, 0);
	input_sync(input_dev);

	return IRQ_HANDLED;
}

static int stpmic1_onkey_probe(struct platform_device *pdev)
{
	struct stpmic1 *pmic = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct input_dev *input_dev;
	struct stpmic1_onkey *onkey;
	unsigned int val, reg = 0;
	int error;

	onkey = devm_kzalloc(dev, sizeof(*onkey), GFP_KERNEL);
	if (!onkey)
		return -ENOMEM;

	onkey->irq_falling = platform_get_irq_byname(pdev, "onkey-falling");
	if (onkey->irq_falling < 0)
		return onkey->irq_falling;

	onkey->irq_rising = platform_get_irq_byname(pdev, "onkey-rising");
	if (onkey->irq_rising < 0)
		return onkey->irq_rising;

	if (!device_property_read_u32(dev, "power-off-time-sec", &val)) {
		if (val > 0 && val <= 16) {
			dev_dbg(dev, "power-off-time=%d seconds\n", val);
			reg |= PONKEY_PWR_OFF;
			reg |= ((16 - val) & PONKEY_TURNOFF_TIMER_MASK);
		} else {
			dev_err(dev, "power-off-time-sec out of range\n");
			return -EINVAL;
		}
	}

	if (device_property_present(dev, "st,onkey-clear-cc-flag"))
		reg |= PONKEY_CC_FLAG_CLEAR;

	error = regmap_update_bits(pmic->regmap, PKEY_TURNOFF_CR,
				   PONKEY_TURNOFF_MASK, reg);
	if (error) {
		dev_err(dev, "PKEY_TURNOFF_CR write failed: %d\n", error);
		return error;
	}

	if (device_property_present(dev, "st,onkey-pu-inactive")) {
		error = regmap_update_bits(pmic->regmap, PADS_PULL_CR,
					   PONKEY_PU_INACTIVE,
					   PONKEY_PU_INACTIVE);
		if (error) {
			dev_err(dev, "ONKEY Pads configuration failed: %d\n",
				error);
			return error;
		}
	}

	input_dev = devm_input_allocate_device(dev);
	if (!input_dev) {
		dev_err(dev, "Can't allocate Pwr Onkey Input Device\n");
		return -ENOMEM;
	}

	input_dev->name = "pmic_onkey";
	input_dev->phys = "pmic_onkey/input0";

	input_set_capability(input_dev, EV_KEY, KEY_POWER);

	onkey->input_dev = input_dev;

	/* interrupt is nested in a thread */
	error = devm_request_threaded_irq(dev, onkey->irq_falling, NULL,
					  onkey_falling_irq, IRQF_ONESHOT,
					  dev_name(dev), onkey);
	if (error) {
		dev_err(dev, "Can't get IRQ Onkey Falling: %d\n", error);
		return error;
	}

	error = devm_request_threaded_irq(dev, onkey->irq_rising, NULL,
					  onkey_rising_irq, IRQF_ONESHOT,
					  dev_name(dev), onkey);
	if (error) {
		dev_err(dev, "Can't get IRQ Onkey Rising: %d\n", error);
		return error;
	}

	error = input_register_device(input_dev);
	if (error) {
		dev_err(dev, "Can't register power button: %d\n", error);
		return error;
	}

	platform_set_drvdata(pdev, onkey);
	device_init_wakeup(dev, true);

	return 0;
}

static int __maybe_unused stpmic1_onkey_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct stpmic1_onkey *onkey = platform_get_drvdata(pdev);

	if (device_may_wakeup(dev)) {
		enable_irq_wake(onkey->irq_falling);
		enable_irq_wake(onkey->irq_rising);
	}
	return 0;
}

static int __maybe_unused stpmic1_onkey_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct stpmic1_onkey *onkey = platform_get_drvdata(pdev);

	if (device_may_wakeup(dev)) {
		disable_irq_wake(onkey->irq_falling);
		disable_irq_wake(onkey->irq_rising);
	}
	return 0;
}

static SIMPLE_DEV_PM_OPS(stpmic1_onkey_pm,
			 stpmic1_onkey_suspend,
			 stpmic1_onkey_resume);

static const struct of_device_id of_stpmic1_onkey_match[] = {
	{ .compatible = "st,stpmic1-onkey" },
	{ },
};

MODULE_DEVICE_TABLE(of, of_stpmic1_onkey_match);

static struct platform_driver stpmic1_onkey_driver = {
	.probe	= stpmic1_onkey_probe,
	.driver	= {
		.name	= "stpmic1_onkey",
		.of_match_table = of_match_ptr(of_stpmic1_onkey_match),
		.pm	= &stpmic1_onkey_pm,
	},
};
module_platform_driver(stpmic1_onkey_driver);

MODULE_DESCRIPTION("Onkey driver for STPMIC1");
MODULE_AUTHOR("Pascal Paillet <p.paillet@st.com>");
MODULE_LICENSE("GPL v2");
