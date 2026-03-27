// SPDX-License-Identifier: GPL-2.0
/*
 * Ilitek ILI9806E core driver.
 *
 * Copyright (c) 2026 Amarula Solutions, Dario Binacchi <dario.binacchi@amarulasolutions.com>
 */

#include <drm/drm_panel.h>

#include <linux/delay.h>
#include <linux/export.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>

#include "panel-ilitek-ili9806e-core.h"

struct ili9806e {
	void *transport;
	struct drm_panel panel;

	unsigned int num_supplies;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
};

void *ili9806e_get_transport(struct drm_panel *panel)
{
	struct ili9806e *ctx = container_of(panel, struct ili9806e, panel);

	return ctx->transport;
}
EXPORT_SYMBOL_GPL(ili9806e_get_transport);

int ili9806e_power_on(struct device *dev)
{
	struct ili9806e *ctx = dev_get_drvdata(dev);
	int ret;

	gpiod_set_value(ctx->reset_gpio, 1);

	ret = regulator_bulk_enable(ctx->num_supplies, ctx->supplies);
	if (ret) {
		dev_err(dev, "regulator bulk enable failed: %d\n", ret);
		return ret;
	}

	usleep_range(10000, 20000);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10000, 20000);

	return 0;
}
EXPORT_SYMBOL_GPL(ili9806e_power_on);

int ili9806e_power_off(struct device *dev)
{
	struct ili9806e *ctx = dev_get_drvdata(dev);
	int ret;

	gpiod_set_value(ctx->reset_gpio, 1);

	ret = regulator_bulk_disable(ctx->num_supplies, ctx->supplies);
	if (ret)
		dev_err(dev, "regulator bulk disable failed: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(ili9806e_power_off);

int ili9806e_probe(struct device *dev, void *transport,
		  const struct drm_panel_funcs *funcs,
		  int connector_type)
{
	struct ili9806e *ctx;
	bool set_prepare_prev_first = false;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(struct ili9806e), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	dev_set_drvdata(dev, ctx);
	ctx->transport = transport;

	ctx->supplies[ctx->num_supplies++].supply = "vdd";
	if (of_device_is_compatible(dev->of_node,
				    "densitron,dmt028vghmcmi-1d") ||
	    of_device_is_compatible(dev->of_node,
				    "ortustech,com35h3p70ulc")) {
		ctx->supplies[ctx->num_supplies++].supply = "vccio";
		set_prepare_prev_first = true;
	}

	ret = devm_regulator_bulk_get(dev, ctx->num_supplies, ctx->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	drm_panel_init(&ctx->panel, dev, funcs, connector_type);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	if (set_prepare_prev_first)
		ctx->panel.prepare_prev_first = true;

	drm_panel_add(&ctx->panel);

	return 0;

}
EXPORT_SYMBOL_GPL(ili9806e_probe);

void ili9806e_remove(struct device *dev)
{
	struct ili9806e *ctx = dev_get_drvdata(dev);

	drm_panel_remove(&ctx->panel);
}
EXPORT_SYMBOL_GPL(ili9806e_remove);

MODULE_AUTHOR("Dario Binacchi <dario.binacchi@amarulasolutions.com>");
MODULE_AUTHOR("Gunnar Dibbern <gunnar.dibbern@lht.dlh.de>");
MODULE_AUTHOR("Michael Walle <mwalle@kernel.org>");
MODULE_DESCRIPTION("Ilitek ILI9806E Controller Driver");
MODULE_LICENSE("GPL");
