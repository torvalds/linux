/*
 *  Atheros AR71XX/AR724X/AR913X GPIO API support
 *
 *  Copyright (C) 2010-2011 Jaiganesh Narayanan <jnarayanan@atheros.com>
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  Parts of this file are based on Atheros' 2.6.15/2.6.31 BSP
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/gpio/driver.h>
#include <linux/platform_data/gpio-ath79.h>
#include <linux/of_device.h>

#include <asm/mach-ath79/ar71xx_regs.h>

struct ath79_gpio_ctrl {
	struct gpio_chip gc;
	void __iomem *base;
	spinlock_t lock;
};

static const struct of_device_id ath79_gpio_of_match[] = {
	{ .compatible = "qca,ar7100-gpio" },
	{ .compatible = "qca,ar9340-gpio" },
	{},
};

static int ath79_gpio_probe(struct platform_device *pdev)
{
	struct ath79_gpio_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct device_node *np = pdev->dev.of_node;
	struct ath79_gpio_ctrl *ctrl;
	struct resource *res;
	u32 ath79_gpio_count;
	bool oe_inverted;
	int err;

	ctrl = devm_kzalloc(&pdev->dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	if (np) {
		err = of_property_read_u32(np, "ngpios", &ath79_gpio_count);
		if (err) {
			dev_err(&pdev->dev, "ngpios property is not valid\n");
			return err;
		}
		if (ath79_gpio_count >= 32) {
			dev_err(&pdev->dev, "ngpios must be less than 32\n");
			return -EINVAL;
		}
		oe_inverted = of_device_is_compatible(np, "qca,ar9340-gpio");
	} else if (pdata) {
		ath79_gpio_count = pdata->ngpios;
		oe_inverted = pdata->oe_inverted;
	} else {
		dev_err(&pdev->dev, "No DT node or platform data found\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ctrl->base = devm_ioremap_nocache(
		&pdev->dev, res->start, resource_size(res));
	if (!ctrl->base)
		return -ENOMEM;

	spin_lock_init(&ctrl->lock);
	err = bgpio_init(&ctrl->gc, &pdev->dev, 4,
			ctrl->base + AR71XX_GPIO_REG_IN,
			ctrl->base + AR71XX_GPIO_REG_SET,
			ctrl->base + AR71XX_GPIO_REG_CLEAR,
			oe_inverted ? NULL : ctrl->base + AR71XX_GPIO_REG_OE,
			oe_inverted ? ctrl->base + AR71XX_GPIO_REG_OE : NULL,
			0);
	if (err) {
		dev_err(&pdev->dev, "bgpio_init failed\n");
		return err;
	}
	/* Use base 0 to stay compatible with legacy platforms */
	ctrl->gc.base = 0;

	err = gpiochip_add_data(&ctrl->gc, ctrl);
	if (err) {
		dev_err(&pdev->dev,
			"cannot add AR71xx GPIO chip, error=%d", err);
		return err;
	}

	return 0;
}

static struct platform_driver ath79_gpio_driver = {
	.driver = {
		.name = "ath79-gpio",
		.of_match_table	= ath79_gpio_of_match,
	},
	.probe = ath79_gpio_probe,
};

module_platform_driver(ath79_gpio_driver);
