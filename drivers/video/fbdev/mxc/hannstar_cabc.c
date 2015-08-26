/*
 * Copyright (C) 2014-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>

#define DRIVER_NAME "hannstar-cabc"

static const struct of_device_id cabc_dt_ids[] = {
	{ .compatible = "hannstar,cabc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cabc_dt_ids);

static int cabc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node, *pp;
	int cabc_gpio, ret = 0;
	bool cabc_enable;
	unsigned long gpio_flag;
	enum of_gpio_flags flags;

	for_each_child_of_node(np, pp) {
		if (!of_find_property(pp, "gpios", NULL)) {
			dev_warn(&pdev->dev, "Found interface without "
				 "gpios\n");
			continue;
		}

		cabc_gpio = of_get_gpio_flags(pp, 0, &flags);
		if (!gpio_is_valid(cabc_gpio)) {
			ret = cabc_gpio;
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev,
					"Failed to get gpio flags, "
					"error: %d\n", ret);
			return ret;
		}

		cabc_enable = of_property_read_bool(pp, "cabc-enable");

		if (flags & OF_GPIO_ACTIVE_LOW)
			gpio_flag = cabc_enable ?
				GPIOF_OUT_INIT_LOW : GPIOF_OUT_INIT_HIGH;
		else
			gpio_flag = cabc_enable ?
				GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW;

		devm_gpio_request_one(&pdev->dev, cabc_gpio,
					gpio_flag, "hannstar-cabc");
	}

	return ret;
}

static struct platform_driver cabc_driver = {
	.probe	= cabc_probe,
	.driver = {
		.of_match_table = cabc_dt_ids,
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};
module_platform_driver(cabc_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("Hannstar CABC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
