/*
 * wm831x-isink.c  --  Current sink driver for the WM831x series
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/slab.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/regulator.h>
#include <linux/mfd/wm831x/pdata.h>

#define WM831X_ISINK_MAX_NAME 7

struct wm831x_isink {
	char name[WM831X_ISINK_MAX_NAME];
	struct regulator_desc desc;
	int reg;
	struct wm831x *wm831x;
	struct regulator_dev *regulator;
};

static int wm831x_isink_enable(struct regulator_dev *rdev)
{
	struct wm831x_isink *isink = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = isink->wm831x;
	int ret;

	/* We have a two stage enable: first start the ISINK... */
	ret = wm831x_set_bits(wm831x, isink->reg, WM831X_CS1_ENA,
			      WM831X_CS1_ENA);
	if (ret != 0)
		return ret;

	/* ...then enable drive */
	ret = wm831x_set_bits(wm831x, isink->reg, WM831X_CS1_DRIVE,
			      WM831X_CS1_DRIVE);
	if (ret != 0)
		wm831x_set_bits(wm831x, isink->reg, WM831X_CS1_ENA, 0);

	return ret;

}

static int wm831x_isink_disable(struct regulator_dev *rdev)
{
	struct wm831x_isink *isink = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = isink->wm831x;
	int ret;

	ret = wm831x_set_bits(wm831x, isink->reg, WM831X_CS1_DRIVE, 0);
	if (ret < 0)
		return ret;

	ret = wm831x_set_bits(wm831x, isink->reg, WM831X_CS1_ENA, 0);
	if (ret < 0)
		return ret;

	return ret;

}

static int wm831x_isink_is_enabled(struct regulator_dev *rdev)
{
	struct wm831x_isink *isink = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = isink->wm831x;
	int ret;

	ret = wm831x_reg_read(wm831x, isink->reg);
	if (ret < 0)
		return ret;

	if ((ret & (WM831X_CS1_ENA | WM831X_CS1_DRIVE)) ==
	    (WM831X_CS1_ENA | WM831X_CS1_DRIVE))
		return 1;
	else
		return 0;
}

static int wm831x_isink_set_current(struct regulator_dev *rdev,
				    int min_uA, int max_uA)
{
	struct wm831x_isink *isink = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = isink->wm831x;
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(wm831x_isinkv_values); i++) {
		int val = wm831x_isinkv_values[i];
		if (min_uA <= val && val <= max_uA) {
			ret = wm831x_set_bits(wm831x, isink->reg,
					      WM831X_CS1_ISEL_MASK, i);
			return ret;
		}
	}

	return -EINVAL;
}

static int wm831x_isink_get_current(struct regulator_dev *rdev)
{
	struct wm831x_isink *isink = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = isink->wm831x;
	int ret;

	ret = wm831x_reg_read(wm831x, isink->reg);
	if (ret < 0)
		return ret;

	ret &= WM831X_CS1_ISEL_MASK;
	if (ret > WM831X_ISINK_MAX_ISEL)
		ret = WM831X_ISINK_MAX_ISEL;

	return wm831x_isinkv_values[ret];
}

static const struct regulator_ops wm831x_isink_ops = {
	.is_enabled = wm831x_isink_is_enabled,
	.enable = wm831x_isink_enable,
	.disable = wm831x_isink_disable,
	.set_current_limit = wm831x_isink_set_current,
	.get_current_limit = wm831x_isink_get_current,
};

static irqreturn_t wm831x_isink_irq(int irq, void *data)
{
	struct wm831x_isink *isink = data;

	regulator_notifier_call_chain(isink->regulator,
				      REGULATOR_EVENT_OVER_CURRENT,
				      NULL);

	return IRQ_HANDLED;
}


static int wm831x_isink_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = dev_get_platdata(wm831x->dev);
	struct wm831x_isink *isink;
	int id = pdev->id % ARRAY_SIZE(pdata->isink);
	struct regulator_config config = { };
	struct resource *res;
	int ret, irq;

	dev_dbg(&pdev->dev, "Probing ISINK%d\n", id + 1);

	if (pdata == NULL || pdata->isink[id] == NULL)
		return -ENODEV;

	isink = devm_kzalloc(&pdev->dev, sizeof(struct wm831x_isink),
			     GFP_KERNEL);
	if (!isink)
		return -ENOMEM;

	isink->wm831x = wm831x;

	res = platform_get_resource(pdev, IORESOURCE_REG, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "No REG resource\n");
		ret = -EINVAL;
		goto err;
	}
	isink->reg = res->start;

	/* For current parts this is correct; probably need to revisit
	 * in future.
	 */
	snprintf(isink->name, sizeof(isink->name), "ISINK%d", id + 1);
	isink->desc.name = isink->name;
	isink->desc.id = id;
	isink->desc.ops = &wm831x_isink_ops;
	isink->desc.type = REGULATOR_CURRENT;
	isink->desc.owner = THIS_MODULE;

	config.dev = pdev->dev.parent;
	config.init_data = pdata->isink[id];
	config.driver_data = isink;

	isink->regulator = devm_regulator_register(&pdev->dev, &isink->desc,
						   &config);
	if (IS_ERR(isink->regulator)) {
		ret = PTR_ERR(isink->regulator);
		dev_err(wm831x->dev, "Failed to register ISINK%d: %d\n",
			id + 1, ret);
		goto err;
	}

	irq = wm831x_irq(wm831x, platform_get_irq(pdev, 0));
	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					wm831x_isink_irq,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					isink->name,
					isink);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request ISINK IRQ %d: %d\n",
			irq, ret);
		goto err;
	}

	platform_set_drvdata(pdev, isink);

	return 0;

err:
	return ret;
}

static struct platform_driver wm831x_isink_driver = {
	.probe = wm831x_isink_probe,
	.driver		= {
		.name	= "wm831x-isink",
	},
};

static int __init wm831x_isink_init(void)
{
	int ret;
	ret = platform_driver_register(&wm831x_isink_driver);
	if (ret != 0)
		pr_err("Failed to register WM831x ISINK driver: %d\n", ret);

	return ret;
}
subsys_initcall(wm831x_isink_init);

static void __exit wm831x_isink_exit(void)
{
	platform_driver_unregister(&wm831x_isink_driver);
}
module_exit(wm831x_isink_exit);

/* Module information */
MODULE_AUTHOR("Mark Brown");
MODULE_DESCRIPTION("WM831x current sink driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm831x-isink");
