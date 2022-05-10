// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author:
 *      Guochun Huang <hero.huang@rock-chips.com>
 */

#include <asm/unaligned.h>
#include <drm/drm_bridge.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_panel.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>

struct serdes_init_seq {
	struct reg_sequence *reg_sequence;
	unsigned int reg_seq_cnt;
};

struct bu18tl82 {
	struct drm_bridge base;
	struct drm_bridge *bridge;
	struct device *dev;
	struct regmap *regmap;
	struct mipi_dsi_device *dsi;
	struct device_node *dsi_node;
	struct serdes_init_seq *serdes_init_seq;
	bool sel_mipi;
	struct regulator *supply;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
};

static const struct regmap_config bu18tl82_regmap_config = {
	.name = "bu18tl82",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x0700,
};

static struct bu18tl82 *bridge_to_bu18tl82(struct drm_bridge *bridge)
{
	return container_of(bridge, struct bu18tl82, base);
}

static int bu18tl82_parse_init_seq(struct device *dev, const u16 *data,
				   int length, struct serdes_init_seq *seq)
{
	struct reg_sequence *reg_sequence;
	u16 *buf, *d;
	unsigned int i, cnt;

	if (!seq)
		return -EINVAL;

	buf = devm_kmemdup(dev, data, length, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	d = buf;
	cnt = length / 4;
	seq->reg_seq_cnt = cnt;

	seq->reg_sequence = devm_kcalloc(dev, cnt, sizeof(struct reg_sequence), GFP_KERNEL);
	if (!seq->reg_sequence)
		return -ENOMEM;


	for (i = 0; i < cnt; i++) {
		reg_sequence = &seq->reg_sequence[i];
		reg_sequence->reg = get_unaligned_be16(&d[0]);
		reg_sequence->def = get_unaligned_be16(&d[1]);
		d += 2;
	}

	return 0;
}

static int bu18tl82_get_init_seq(struct bu18tl82 *bu18tl82)
{
	struct device *dev = bu18tl82->dev;
	struct device_node *np = dev->of_node;
	const void *data;
	int len, err;

	data = of_get_property(np, "serdes-init-sequence", &len);
	if (!data) {
		dev_err(dev, "failed to get serdes-init-sequence\n");
		return -EINVAL;
	}

	bu18tl82->serdes_init_seq = devm_kzalloc(dev, sizeof(*bu18tl82->serdes_init_seq),
						 GFP_KERNEL);
	if (!bu18tl82->serdes_init_seq)
		return -ENOMEM;

	err = bu18tl82_parse_init_seq(dev, data, len, bu18tl82->serdes_init_seq);
	if (err) {
		dev_err(dev, "failed to parse serdes-init-sequence\n");
		return err;
	}

	return 0;
}

static int bu18tl82_bridge_get_modes(struct drm_bridge *bridge,
				     struct drm_connector *connector)
{
	struct bu18tl82 *bu18tl82 = bridge_to_bu18tl82(bridge);

	return drm_bridge_get_modes(bu18tl82->bridge, connector);
}

static struct mipi_dsi_device *bu18tl82_attach_dsi(struct bu18tl82 *bu18tl82,
						 struct device_node *dsi_node)
{
	const struct mipi_dsi_device_info info = { "bu18tl82", 0, NULL };
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	int ret;

	host = of_find_mipi_dsi_host_by_node(dsi_node);
	if (!host) {
		dev_err(bu18tl82->dev, "failed to find dsi host\n");
		return ERR_PTR(-EPROBE_DEFER);
	}

	dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dsi)) {
		dev_err(bu18tl82->dev, "failed to create dsi device\n");
		return dsi;
	}

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(bu18tl82->dev, "failed to attach dsi to host\n");
		mipi_dsi_device_unregister(dsi);
		return ERR_PTR(ret);
	}

	return dsi;
}

static int bu18tl82_bridge_attach(struct drm_bridge *bridge,
				  enum drm_bridge_attach_flags flags)
{
	struct bu18tl82 *bu18tl82 = bridge_to_bu18tl82(bridge);
	int ret;

	ret = drm_of_find_panel_or_bridge(bu18tl82->dev->of_node, 1, -1,
					  NULL, &bu18tl82->bridge);
	if (ret)
		return ret;

	if (bu18tl82->sel_mipi) {
		/* Attach primary DSI */
		bu18tl82->dsi = bu18tl82_attach_dsi(bu18tl82, bu18tl82->dsi_node);
		if (IS_ERR(bu18tl82->dsi))
			return PTR_ERR(bu18tl82->dsi);
	}

	ret = drm_bridge_attach(bridge->encoder, bu18tl82->bridge,
				bridge, flags);
	if (ret) {
		if (bu18tl82->sel_mipi)
			mipi_dsi_device_unregister(bu18tl82->dsi);

		dev_err(bu18tl82->dev, "failed to attach bridge\n");
		return ret;
	}

	return 0;
}

static void bu18tl82_bridge_detach(struct drm_bridge *bridge)
{
	struct bu18tl82 *bu18tl82 = bridge_to_bu18tl82(bridge);

	if (bu18tl82->sel_mipi) {
		mipi_dsi_detach(bu18tl82->dsi);
		mipi_dsi_device_unregister(bu18tl82->dsi);
	}
}

static void bu18tl82_bridge_enable(struct drm_bridge *bridge)
{
	struct bu18tl82 *bu18tl82 = bridge_to_bu18tl82(bridge);
	struct serdes_init_seq *init_seq = bu18tl82->serdes_init_seq;
	int count = init_seq->reg_seq_cnt;

	regmap_multi_reg_write(bu18tl82->regmap, init_seq->reg_sequence, count);
	mdelay(1000);
}

static void bu18tl82_bridge_disable(struct drm_bridge *bridge)
{
}

static void bu18tl82_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct bu18tl82 *bu18tl82 = bridge_to_bu18tl82(bridge);
	int ret;

	if (bu18tl82->supply) {
		ret = regulator_enable(bu18tl82->supply);
		if (ret < 0)
			return;

		msleep(120);
	}

	if (bu18tl82->enable_gpio) {
		gpiod_direction_output(bu18tl82->enable_gpio, 1);
		msleep(120);
	}

	if (bu18tl82->reset_gpio) {
		gpiod_direction_output(bu18tl82->reset_gpio, 1);
		msleep(120);
		gpiod_direction_output(bu18tl82->reset_gpio, 0);
	}
}

static void bu18tl82_bridge_post_disable(struct drm_bridge *bridge)
{
	struct bu18tl82 *bu18tl82 = bridge_to_bu18tl82(bridge);

	if (bu18tl82->reset_gpio)
		gpiod_direction_output(bu18tl82->reset_gpio, 1);

	if (bu18tl82->enable_gpio)
		gpiod_direction_output(bu18tl82->enable_gpio, 0);

	if (bu18tl82->supply)
		regulator_disable(bu18tl82->supply);
}

static enum
drm_connector_status bu18tl82_bridge_detect(struct drm_bridge *bridge)
{
	struct bu18tl82 *bu18tl82 = bridge_to_bu18tl82(bridge);

	if (bu18tl82->bridge) {
		struct drm_bridge *next_bridge = bu18tl82->bridge;

		if (next_bridge->ops & DRM_BRIDGE_OP_DETECT)
			return drm_bridge_detect(bu18tl82->bridge);
	}

	return connector_status_connected;
}

static const struct drm_bridge_funcs bu18tl82_bridge_funcs = {
	.attach = bu18tl82_bridge_attach,
	.detect = bu18tl82_bridge_detect,
	.detach = bu18tl82_bridge_detach,
	.enable = bu18tl82_bridge_enable,
	.disable = bu18tl82_bridge_disable,
	.pre_enable = bu18tl82_bridge_pre_enable,
	.post_disable = bu18tl82_bridge_post_disable,
	.get_modes = bu18tl82_bridge_get_modes,
};

static int bu18tl82_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct bu18tl82 *bu18tl82;
	int ret;

	bu18tl82 = devm_kzalloc(dev, sizeof(*bu18tl82), GFP_KERNEL);
	if (!bu18tl82)
		return -ENOMEM;

	bu18tl82->dev = dev;
	i2c_set_clientdata(client, bu18tl82);

	bu18tl82->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(bu18tl82->supply))
		return dev_err_probe(dev, PTR_ERR(bu18tl82->supply),
				     "failed to get power regulator\n");

	bu18tl82->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(bu18tl82->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(bu18tl82->reset_gpio),
				     "failed to acquire reset gpio\n");

	bu18tl82->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_ASIS);
	if (IS_ERR(bu18tl82->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(bu18tl82->enable_gpio),
				     "failed to acquire enable gpio\n");

	bu18tl82->regmap = devm_regmap_init_i2c(client, &bu18tl82_regmap_config);
	if (IS_ERR(bu18tl82->regmap))
		return dev_err_probe(dev, PTR_ERR(bu18tl82->regmap),
				     "failed to initialize regmap\n");

	bu18tl82->sel_mipi = of_property_read_bool(dev->of_node, "sel-mipi");

	if (bu18tl82->sel_mipi) {
		bu18tl82->dsi_node = of_graph_get_remote_node(dev->of_node, 0, -1);
		if (!bu18tl82->dsi_node)
			return dev_err_probe(dev, -ENODEV,
					     "failed to get remote node for dsi\n");
	}

	ret = bu18tl82_get_init_seq(bu18tl82);
	if (ret)
		return ret;

	bu18tl82->base.funcs = &bu18tl82_bridge_funcs;
	bu18tl82->base.of_node = dev->of_node;
	bu18tl82->base.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_MODES;

	drm_bridge_add(&bu18tl82->base);

	return 0;
}

static int bu18tl82_i2c_remove(struct i2c_client *client)
{
	struct bu18tl82 *bu18tl82 = i2c_get_clientdata(client);

	drm_bridge_remove(&bu18tl82->base);

	return 0;
}

static const struct i2c_device_id bu18tl82_i2c_table[] = {
	{ "bu18tl82", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, bu18tl82_i2c_table);

static const struct of_device_id bu18tl82_of_match[] = {
	{ .compatible = "rohm,bu18tl82" },
	{}
};
MODULE_DEVICE_TABLE(of, bu18tl82_of_match);

static struct i2c_driver bu18tl82_i2c_driver = {
	.driver = {
		.name = "bu18tl82",
		.of_match_table = bu18tl82_of_match,
	},
	.probe = bu18tl82_i2c_probe,
	.remove = bu18tl82_i2c_remove,
	.id_table = bu18tl82_i2c_table,
};
module_i2c_driver(bu18tl82_i2c_driver);

MODULE_AUTHOR("Guochun Huang <hero.huang@rock-chips.com>");
MODULE_DESCRIPTION("Rohm BU18TL82 Clockless Link-BD Serializer with MIPI and LVDS Interface");
MODULE_LICENSE("GPL");
