/*
 * Cortina Gemini Reset controller driver
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <dt-bindings/reset/cortina,gemini-reset.h>

/**
 * struct gemini_reset - gemini reset controller
 * @map: regmap to access the containing system controller
 * @rcdev: reset controller device
 */
struct gemini_reset {
	struct regmap *map;
	struct reset_controller_dev rcdev;
};

#define GEMINI_GLOBAL_SOFT_RESET 0x0c

#define to_gemini_reset(p) \
	container_of((p), struct gemini_reset, rcdev)

/*
 * This is a self-deasserting reset controller.
 */
static int gemini_reset(struct reset_controller_dev *rcdev,
			unsigned long id)
{
	struct gemini_reset *gr = to_gemini_reset(rcdev);

	/* Manual says to always set BIT 30 (CPU1) to 1 */
	return regmap_write(gr->map,
			    GEMINI_GLOBAL_SOFT_RESET,
			    BIT(GEMINI_RESET_CPU1) | BIT(id));
}

static int gemini_reset_status(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	struct gemini_reset *gr = to_gemini_reset(rcdev);
	u32 val;
	int ret;

	ret = regmap_read(gr->map, GEMINI_GLOBAL_SOFT_RESET, &val);
	if (ret)
		return ret;

	return !!(val & BIT(id));
}

static const struct reset_control_ops gemini_reset_ops = {
	.reset = gemini_reset,
	.status = gemini_reset_status,
};

static int gemini_reset_probe(struct platform_device *pdev)
{
	struct gemini_reset *gr;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret;

	gr = devm_kzalloc(dev, sizeof(*gr), GFP_KERNEL);
	if (!gr)
		return -ENOMEM;

	gr->map = syscon_node_to_regmap(np);
	if (IS_ERR(gr->map)) {
		ret = PTR_ERR(gr->map);
		dev_err(dev, "unable to get regmap (%d)", ret);
		return ret;
	}
	gr->rcdev.owner = THIS_MODULE;
	gr->rcdev.nr_resets = 32;
	gr->rcdev.ops = &gemini_reset_ops;
	gr->rcdev.of_node = pdev->dev.of_node;

	ret = devm_reset_controller_register(&pdev->dev, &gr->rcdev);
	if (ret)
		return ret;

	dev_info(dev, "registered Gemini reset controller\n");
	return 0;
}

static const struct of_device_id gemini_reset_dt_ids[] = {
	{ .compatible = "cortina,gemini-syscon", },
	{ /* sentinel */ },
};

static struct platform_driver gemini_reset_driver = {
	.probe	= gemini_reset_probe,
	.driver = {
		.name		= "gemini-reset",
		.of_match_table	= gemini_reset_dt_ids,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(gemini_reset_driver);
