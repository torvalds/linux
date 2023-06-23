// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Richtek Technology Corp.
 * Author: ChiYuan Huang <cy_huang@richtek.com>
 */

#include <linux/bits.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define RT5120_REG_INTSTAT	0x1E
#define RT5120_PWRKEYSTAT_MASK	BIT(7)

struct rt5120_priv {
	struct regmap *regmap;
	struct input_dev *input;
};

static irqreturn_t rt5120_pwrkey_handler(int irq, void *devid)
{
	struct rt5120_priv *priv = devid;
	unsigned int stat;
	int error;

	error = regmap_read(priv->regmap, RT5120_REG_INTSTAT, &stat);
	if (error)
		return IRQ_NONE;

	input_report_key(priv->input, KEY_POWER,
			 !(stat & RT5120_PWRKEYSTAT_MASK));
	input_sync(priv->input);

	return IRQ_HANDLED;
}

static int rt5120_pwrkey_probe(struct platform_device *pdev)
{
	struct rt5120_priv *priv;
	struct device *dev = &pdev->dev;
	int press_irq, release_irq;
	int error;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = dev_get_regmap(dev->parent, NULL);
	if (!priv->regmap) {
		dev_err(dev, "Failed to init regmap\n");
		return -ENODEV;
	}

	press_irq = platform_get_irq_byname(pdev, "pwrkey-press");
	if (press_irq < 0)
		return press_irq;

	release_irq = platform_get_irq_byname(pdev, "pwrkey-release");
	if (release_irq < 0)
		return release_irq;

	/* Make input device be device resource managed */
	priv->input = devm_input_allocate_device(dev);
	if (!priv->input)
		return -ENOMEM;

	priv->input->name = "rt5120_pwrkey";
	priv->input->phys = "rt5120_pwrkey/input0";
	priv->input->id.bustype = BUS_I2C;
	input_set_capability(priv->input, EV_KEY, KEY_POWER);

	error = input_register_device(priv->input);
	if (error) {
		dev_err(dev, "Failed to register input device: %d\n", error);
		return error;
	}

	error = devm_request_threaded_irq(dev, press_irq,
					  NULL, rt5120_pwrkey_handler,
					  0, "pwrkey-press", priv);
	if (error) {
		dev_err(dev,
			"Failed to register pwrkey press irq: %d\n", error);
		return error;
	}

	error = devm_request_threaded_irq(dev, release_irq,
					  NULL, rt5120_pwrkey_handler,
					  0, "pwrkey-release", priv);
	if (error) {
		dev_err(dev,
			"Failed to register pwrkey release irq: %d\n", error);
		return error;
	}

	return 0;
}

static const struct of_device_id r5120_pwrkey_match_table[] = {
	{ .compatible = "richtek,rt5120-pwrkey" },
	{}
};
MODULE_DEVICE_TABLE(of, r5120_pwrkey_match_table);

static struct platform_driver rt5120_pwrkey_driver = {
	.driver = {
		.name = "rt5120-pwrkey",
		.of_match_table = r5120_pwrkey_match_table,
	},
	.probe = rt5120_pwrkey_probe,
};
module_platform_driver(rt5120_pwrkey_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("Richtek RT5120 power key driver");
MODULE_LICENSE("GPL");
