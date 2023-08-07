// SPDX-License-Identifier: GPL-2.0
//
// Driver for TPS65219 Push Button
//
// Copyright (C) 2022 BayLibre Incorporated - https://www.baylibre.com/

#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/tps65219.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

struct tps65219_pwrbutton {
	struct device *dev;
	struct input_dev *idev;
	char phys[32];
};

static irqreturn_t tps65219_pb_push_irq(int irq, void *_pwr)
{
	struct tps65219_pwrbutton *pwr = _pwr;

	input_report_key(pwr->idev, KEY_POWER, 1);
	pm_wakeup_event(pwr->dev, 0);
	input_sync(pwr->idev);

	return IRQ_HANDLED;
}

static irqreturn_t tps65219_pb_release_irq(int irq, void *_pwr)
{
	struct tps65219_pwrbutton *pwr = _pwr;

	input_report_key(pwr->idev, KEY_POWER, 0);
	input_sync(pwr->idev);

	return IRQ_HANDLED;
}

static int tps65219_pb_probe(struct platform_device *pdev)
{
	struct tps65219 *tps = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct tps65219_pwrbutton *pwr;
	struct input_dev *idev;
	int error;
	int push_irq;
	int release_irq;

	pwr = devm_kzalloc(dev, sizeof(*pwr), GFP_KERNEL);
	if (!pwr)
		return -ENOMEM;

	idev = devm_input_allocate_device(dev);
	if (!idev)
		return -ENOMEM;

	idev->name = pdev->name;
	snprintf(pwr->phys, sizeof(pwr->phys), "%s/input0",
		 pdev->name);
	idev->phys = pwr->phys;
	idev->id.bustype = BUS_I2C;

	input_set_capability(idev, EV_KEY, KEY_POWER);

	pwr->dev = dev;
	pwr->idev = idev;
	device_init_wakeup(dev, true);

	push_irq = platform_get_irq(pdev, 0);
	if (push_irq < 0)
		return -EINVAL;

	release_irq = platform_get_irq(pdev, 1);
	if (release_irq < 0)
		return -EINVAL;

	error = devm_request_threaded_irq(dev, push_irq, NULL,
					  tps65219_pb_push_irq,
					  IRQF_ONESHOT,
					  dev->init_name, pwr);
	if (error) {
		dev_err(dev, "failed to request push IRQ #%d: %d\n", push_irq,
			error);
		return error;
	}

	error = devm_request_threaded_irq(dev, release_irq, NULL,
					  tps65219_pb_release_irq,
					  IRQF_ONESHOT,
					  dev->init_name, pwr);
	if (error) {
		dev_err(dev, "failed to request release IRQ #%d: %d\n",
			release_irq, error);
		return error;
	}

	error = input_register_device(idev);
	if (error) {
		dev_err(dev, "Can't register power button: %d\n", error);
		return error;
	}

	/* Enable interrupts for the pushbutton */
	regmap_clear_bits(tps->regmap, TPS65219_REG_MASK_CONFIG,
			  TPS65219_REG_MASK_INT_FOR_PB_MASK);

	/* Set PB/EN/VSENSE pin to be a pushbutton */
	regmap_update_bits(tps->regmap, TPS65219_REG_MFP_2_CONFIG,
			   TPS65219_MFP_2_EN_PB_VSENSE_MASK, TPS65219_MFP_2_PB);

	return 0;
}

static void tps65219_pb_remove(struct platform_device *pdev)
{
	struct tps65219 *tps = dev_get_drvdata(pdev->dev.parent);
	int ret;

	/* Disable interrupt for the pushbutton */
	ret = regmap_set_bits(tps->regmap, TPS65219_REG_MASK_CONFIG,
			      TPS65219_REG_MASK_INT_FOR_PB_MASK);
	if (ret)
		dev_warn(&pdev->dev, "Failed to disable irq (%pe)\n", ERR_PTR(ret));
}

static const struct platform_device_id tps65219_pwrbtn_id_table[] = {
	{ "tps65219-pwrbutton", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, tps65219_pwrbtn_id_table);

static struct platform_driver tps65219_pb_driver = {
	.probe = tps65219_pb_probe,
	.remove_new = tps65219_pb_remove,
	.driver = {
		.name = "tps65219_pwrbutton",
	},
	.id_table = tps65219_pwrbtn_id_table,
};
module_platform_driver(tps65219_pb_driver);

MODULE_DESCRIPTION("TPS65219 Power Button");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Markus Schneider-Pargmann <msp@baylibre.com");
