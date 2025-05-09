// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Freebox SAS
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>

struct tdp158 {
	struct drm_bridge bridge;
	struct drm_bridge *next;
	struct gpio_desc *enable; // Operation Enable - pin 36
	struct regulator *vcc; // 3.3V
	struct regulator *vdd; // 1.1V
	struct device *dev;
};

static void tdp158_enable(struct drm_bridge *bridge,
			  struct drm_atomic_state *state)
{
	int err;
	struct tdp158 *tdp158 = bridge->driver_private;

	err = regulator_enable(tdp158->vcc);
	if (err)
		dev_err(tdp158->dev, "failed to enable vcc: %d", err);

	err = regulator_enable(tdp158->vdd);
	if (err)
		dev_err(tdp158->dev, "failed to enable vdd: %d", err);

	gpiod_set_value_cansleep(tdp158->enable, 1);
}

static void tdp158_disable(struct drm_bridge *bridge,
			   struct drm_atomic_state *state)
{
	struct tdp158 *tdp158 = bridge->driver_private;

	gpiod_set_value_cansleep(tdp158->enable, 0);
	regulator_disable(tdp158->vdd);
	regulator_disable(tdp158->vcc);
}

static int tdp158_attach(struct drm_bridge *bridge,
			 struct drm_encoder *encoder,
			 enum drm_bridge_attach_flags flags)
{
	struct tdp158 *tdp158 = bridge->driver_private;

	return drm_bridge_attach(encoder, tdp158->next, bridge, flags);
}

static const struct drm_bridge_funcs tdp158_bridge_funcs = {
	.attach = tdp158_attach,
	.atomic_enable = tdp158_enable,
	.atomic_disable = tdp158_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

static int tdp158_probe(struct i2c_client *client)
{
	struct tdp158 *tdp158;
	struct device *dev = &client->dev;

	tdp158 = devm_drm_bridge_alloc(dev, struct tdp158, bridge,
				       &tdp158_bridge_funcs);
	if (IS_ERR(tdp158))
		return PTR_ERR(tdp158);

	tdp158->next = devm_drm_of_get_bridge(dev, dev->of_node, 1, 0);
	if (IS_ERR(tdp158->next))
		return dev_err_probe(dev, PTR_ERR(tdp158->next), "missing bridge");

	tdp158->vcc = devm_regulator_get(dev, "vcc");
	if (IS_ERR(tdp158->vcc))
		return dev_err_probe(dev, PTR_ERR(tdp158->vcc), "vcc");

	tdp158->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(tdp158->vdd))
		return dev_err_probe(dev, PTR_ERR(tdp158->vdd), "vdd");

	tdp158->enable = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(tdp158->enable))
		return dev_err_probe(dev, PTR_ERR(tdp158->enable), "enable");

	tdp158->bridge.of_node = dev->of_node;
	tdp158->bridge.driver_private = tdp158;
	tdp158->dev = dev;

	return devm_drm_bridge_add(dev, &tdp158->bridge);
}

static const struct of_device_id tdp158_match_table[] = {
	{ .compatible = "ti,tdp158" },
	{ }
};
MODULE_DEVICE_TABLE(of, tdp158_match_table);

static struct i2c_driver tdp158_driver = {
	.probe = tdp158_probe,
	.driver = {
		.name = "tdp158",
		.of_match_table = tdp158_match_table,
	},
};
module_i2c_driver(tdp158_driver);

MODULE_DESCRIPTION("TI TDP158 driver");
MODULE_LICENSE("GPL");
