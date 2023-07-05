/*
 * CPCAP Power Button Input Driver
 *
 * Copyright (C) 2017 Sebastian Reichel <sre@kernel.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/mfd/motorola-cpcap.h>

#define CPCAP_IRQ_ON 23
#define CPCAP_IRQ_ON_BITMASK (1 << (CPCAP_IRQ_ON % 16))

struct cpcap_power_button {
	struct regmap *regmap;
	struct input_dev *idev;
	struct device *dev;
};

static irqreturn_t powerbutton_irq(int irq, void *_button)
{
	struct cpcap_power_button *button = _button;
	int val;

	val = cpcap_sense_virq(button->regmap, irq);
	if (val < 0) {
		dev_err(button->dev, "irq read failed: %d", val);
		return IRQ_HANDLED;
	}

	pm_wakeup_event(button->dev, 0);
	input_report_key(button->idev, KEY_POWER, val);
	input_sync(button->idev);

	return IRQ_HANDLED;
}

static int cpcap_power_button_probe(struct platform_device *pdev)
{
	struct cpcap_power_button *button;
	int irq;
	int err;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	button = devm_kmalloc(&pdev->dev, sizeof(*button), GFP_KERNEL);
	if (!button)
		return -ENOMEM;

	button->idev = devm_input_allocate_device(&pdev->dev);
	if (!button->idev)
		return -ENOMEM;

	button->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!button->regmap)
		return -ENODEV;

	button->dev = &pdev->dev;

	button->idev->name = "cpcap-pwrbutton";
	button->idev->phys = "cpcap-pwrbutton/input0";
	input_set_capability(button->idev, EV_KEY, KEY_POWER);

	err = devm_request_threaded_irq(&pdev->dev, irq, NULL,
		powerbutton_irq, IRQF_ONESHOT, "cpcap_pwrbutton", button);
	if (err < 0) {
		dev_err(&pdev->dev, "IRQ request failed: %d\n", err);
		return err;
	}

	err = input_register_device(button->idev);
	if (err) {
		dev_err(&pdev->dev, "Input register failed: %d\n", err);
		return err;
	}

	device_init_wakeup(&pdev->dev, true);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id cpcap_pwrbutton_dt_match_table[] = {
	{ .compatible = "motorola,cpcap-pwrbutton" },
	{},
};
MODULE_DEVICE_TABLE(of, cpcap_pwrbutton_dt_match_table);
#endif

static struct platform_driver cpcap_power_button_driver = {
	.probe		= cpcap_power_button_probe,
	.driver		= {
		.name	= "cpcap-pwrbutton",
		.of_match_table = of_match_ptr(cpcap_pwrbutton_dt_match_table),
	},
};
module_platform_driver(cpcap_power_button_driver);

MODULE_ALIAS("platform:cpcap-pwrbutton");
MODULE_DESCRIPTION("CPCAP Power Button");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sebastian Reichel <sre@kernel.org>");
