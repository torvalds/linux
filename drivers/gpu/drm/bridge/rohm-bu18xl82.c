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

#define VPLL0_MVALSET	    BIT(7)
#define SWRST_ALL	    BIT(7)


struct serdes_init_seq {
	struct reg_sequence *reg_sequence;
	unsigned int reg_seq_cnt;
};

struct bu18xl82 {
	struct drm_bridge base;
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	struct drm_display_mode mode;
	struct drm_connector connector;
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct mipi_dsi_device *dsi;
	struct device_node *dsi_node;
	struct serdes_init_seq *serdes_init_seq;
	bool sel_mipi;
	struct regulator *supply;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
};

static const struct regmap_config bu18xl82_regmap_config = {
	.name = "bu18xl82",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x0700,
};

static struct bu18xl82 *bridge_to_bu18xl82(struct drm_bridge *bridge)
{
	return container_of(bridge, struct bu18xl82, base);
}

static inline struct bu18xl82 *connector_to_bu18xl82(struct drm_connector *c)
{
	return container_of(c, struct bu18xl82, connector);
}

static int bu18xl82_parse_init_seq(struct device *dev, const u16 *data,
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

static int bu18xl82_get_init_seq(struct bu18xl82 *bu18xl82)
{
	struct device *dev = bu18xl82->dev;
	struct device_node *np = dev->of_node;
	const void *data;
	int len, err;

	data = of_get_property(np, "serdes-init-sequence", &len);
	if (!data) {
		dev_err(dev, "failed to get serdes-init-sequence\n");
		return -EINVAL;
	}

	bu18xl82->serdes_init_seq = devm_kzalloc(dev, sizeof(*bu18xl82->serdes_init_seq),
						 GFP_KERNEL);
	if (!bu18xl82->serdes_init_seq)
		return -ENOMEM;

	err = bu18xl82_parse_init_seq(dev, data, len, bu18xl82->serdes_init_seq);
	if (err) {
		dev_err(dev, "failed to parse serdes-init-sequence\n");
		return err;
	}

	return 0;
}

static struct drm_encoder *
bu18xl82_connector_best_encoder(struct drm_connector *connector)
{
	struct bu18xl82 *bu18xl82 = connector_to_bu18xl82(connector);

	return bu18xl82->base.encoder;
}

static int bu18xl82_connector_get_modes(struct drm_connector *connector)
{
	struct bu18xl82 *bu18xl82 = connector_to_bu18xl82(connector);

	return drm_panel_get_modes(bu18xl82->panel, connector);
}

static const struct drm_connector_helper_funcs
bu18xl82_connector_helper_funcs = {
	.get_modes = bu18xl82_connector_get_modes,
	.best_encoder = bu18xl82_connector_best_encoder,
};

static void bu18xl82_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static enum drm_connector_status
bu18xl82_connector_detect(struct drm_connector *connector, bool force)
{
	struct bu18xl82 *bu18xl82 = connector_to_bu18xl82(connector);

	return drm_bridge_detect(&bu18xl82->base);
}

static const struct drm_connector_funcs bu18xl82_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = bu18xl82_connector_destroy,
	.detect = bu18xl82_connector_detect,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int
bu18xl82_connector_init(struct drm_bridge *bridge, struct bu18xl82 *bu18xl82)
{
	int ret;

	ret = drm_connector_init(bridge->dev, &bu18xl82->connector,
				 &bu18xl82_connector_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret) {
		DRM_ERROR("Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(&bu18xl82->connector,
				 &bu18xl82_connector_helper_funcs);
	drm_connector_attach_encoder(&bu18xl82->connector, bridge->encoder);

	return 0;
}

static struct mipi_dsi_device *bu18xl82_attach_dsi(struct bu18xl82 *bu18xl82,
						 struct device_node *dsi_node)
{
	const struct mipi_dsi_device_info info = { "bu18tl82", 0, NULL };
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	int ret;

	host = of_find_mipi_dsi_host_by_node(dsi_node);
	if (!host) {
		dev_err(bu18xl82->dev, "failed to find dsi host\n");
		return ERR_PTR(-EPROBE_DEFER);
	}

	dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dsi)) {
		dev_err(bu18xl82->dev, "failed to create dsi device\n");
		return dsi;
	}

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(bu18xl82->dev, "failed to attach dsi to host\n");
		mipi_dsi_device_unregister(dsi);
		return ERR_PTR(ret);
	}

	return dsi;
}

static int bu18xl82_bridge_attach(struct drm_bridge *bridge,
				  enum drm_bridge_attach_flags flags)
{
	struct bu18xl82 *bu18xl82 = bridge_to_bu18xl82(bridge);
	int ret;

	ret = drm_of_find_panel_or_bridge(bu18xl82->dev->of_node, 1, -1,
					  &bu18xl82->panel, &bu18xl82->bridge);
	if (ret)
		return ret;

	if (bu18xl82->bridge) {
		if (bu18xl82->sel_mipi) {
			dev_err(bu18xl82->dev, "failed to attach bridge\n");
			/* Attach primary DSI */
			bu18xl82->dsi = bu18xl82_attach_dsi(bu18xl82, bu18xl82->dsi_node);
			if (IS_ERR(bu18xl82->dsi))
				return PTR_ERR(bu18xl82->dsi);
		}

		ret = drm_bridge_attach(bridge->encoder, bu18xl82->bridge,
					bridge, flags);
		if (ret) {
			if (bu18xl82->sel_mipi)
				mipi_dsi_device_unregister(bu18xl82->dsi);

			dev_err(bu18xl82->dev, "failed to attach bridge\n");
			return ret;
		}
	}

	if (bu18xl82->panel) {
		ret = bu18xl82_connector_init(bridge, bu18xl82);
		if (ret)
			return ret;
	}

	return 0;
}

static void bu18xl82_bridge_detach(struct drm_bridge *bridge)
{
	struct bu18xl82 *bu18xl82 = bridge_to_bu18xl82(bridge);

	if (bu18xl82->sel_mipi) {
		mipi_dsi_detach(bu18xl82->dsi);
		mipi_dsi_device_unregister(bu18xl82->dsi);
	}
}

static void bu18xl82_bridge_enable(struct drm_bridge *bridge)
{
	struct bu18xl82 *bu18xl82 = bridge_to_bu18xl82(bridge);
	struct serdes_init_seq *init_seq = bu18xl82->serdes_init_seq;
	const struct device_node *of_node = bu18xl82->dev->of_node;
	int count = init_seq->reg_seq_cnt;

	if (of_device_is_compatible(of_node, "rohm,bu18tl82")) {
		regmap_multi_reg_write(bu18xl82->regmap, init_seq->reg_sequence, count);
		mdelay(1000);
	}


	if (of_device_is_compatible(of_node, "rohm,bu18rl82"))
		regmap_multi_reg_write(bu18xl82->regmap, init_seq->reg_sequence, count);

	if (bu18xl82->panel)
		drm_panel_enable(bu18xl82->panel);
}

static void bu18xl82_bridge_disable(struct drm_bridge *bridge)
{
	struct bu18xl82 *bu18xl82 = bridge_to_bu18xl82(bridge);

	if (bu18xl82->panel)
		drm_panel_disable(bu18xl82->panel);

}

static void bu18xl82_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct bu18xl82 *bu18xl82 = bridge_to_bu18xl82(bridge);
	int ret;

	if (bu18xl82->supply) {
		ret = regulator_enable(bu18xl82->supply);
		if (ret < 0)
			return;

		msleep(120);
	}

	if (bu18xl82->enable_gpio) {
		gpiod_direction_output(bu18xl82->enable_gpio, 1);
		msleep(120);
	}

	if (bu18xl82->reset_gpio) {
		gpiod_direction_output(bu18xl82->reset_gpio, 1);
		msleep(120);
		gpiod_direction_output(bu18xl82->reset_gpio, 0);
	}

	if (bu18xl82->panel)
		drm_panel_prepare(bu18xl82->panel);
}

static void bu18xl82_bridge_post_disable(struct drm_bridge *bridge)
{
	struct bu18xl82 *bu18xl82 = bridge_to_bu18xl82(bridge);

	if (bu18xl82->panel)
		drm_panel_unprepare(bu18xl82->panel);

	if (bu18xl82->reset_gpio)
		gpiod_direction_output(bu18xl82->reset_gpio, 1);

	if (bu18xl82->enable_gpio)
		gpiod_direction_output(bu18xl82->enable_gpio, 0);

	if (bu18xl82->supply)
		regulator_disable(bu18xl82->supply);
}

static void bu18xl82_bridge_mode_set(struct drm_bridge *bridge,
				   const struct drm_display_mode *mode,
				   const struct drm_display_mode *adj_mode)
{
	struct bu18xl82 *bu18xl82 = bridge_to_bu18xl82(bridge);

	drm_mode_copy(&bu18xl82->mode, adj_mode);
}

static enum
drm_connector_status bu18xl82_bridge_detect(struct drm_bridge *bridge)
{
	struct drm_bridge *prev_bridge = drm_bridge_get_prev_bridge(bridge);

	if (prev_bridge && prev_bridge->ops & DRM_BRIDGE_OP_DETECT)
		if (drm_bridge_detect(prev_bridge) != connector_status_connected)
			return connector_status_disconnected;

	return connector_status_connected;
}

static const struct drm_bridge_funcs bu18xl82_bridge_funcs = {
	.attach = bu18xl82_bridge_attach,
	.detect = bu18xl82_bridge_detect,
	.detach = bu18xl82_bridge_detach,
	.enable = bu18xl82_bridge_enable,
	.disable = bu18xl82_bridge_disable,
	.pre_enable = bu18xl82_bridge_pre_enable,
	.post_disable = bu18xl82_bridge_post_disable,
	.mode_set = bu18xl82_bridge_mode_set,
};

static int bu18xl82_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct bu18xl82 *bu18xl82;
	int ret;

	bu18xl82 = devm_kzalloc(dev, sizeof(*bu18xl82), GFP_KERNEL);
	if (!bu18xl82)
		return -ENOMEM;

	bu18xl82->dev = dev;
	bu18xl82->client = client;
	i2c_set_clientdata(client, bu18xl82);

	bu18xl82->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(bu18xl82->supply))
		return dev_err_probe(dev, PTR_ERR(bu18xl82->supply),
				     "failed to get power regulator\n");

	bu18xl82->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(bu18xl82->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(bu18xl82->reset_gpio),
				     "failed to acquire reset gpio\n");

	bu18xl82->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_ASIS);
	if (IS_ERR(bu18xl82->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(bu18xl82->enable_gpio),
				     "failed to acquire enable gpio\n");

	bu18xl82->regmap = devm_regmap_init_i2c(client, &bu18xl82_regmap_config);
	if (IS_ERR(bu18xl82->regmap))
		return dev_err_probe(dev, PTR_ERR(bu18xl82->regmap),
				     "failed to initialize regmap\n");

	bu18xl82->sel_mipi = of_property_read_bool(dev->of_node, "sel-mipi");

	if (bu18xl82->sel_mipi) {
		bu18xl82->dsi_node = of_graph_get_remote_node(dev->of_node, 0, -1);
		if (!bu18xl82->dsi_node)
			return dev_err_probe(dev, -ENODEV,
					     "failed to get remote node for dsi\n");
	}

	ret = bu18xl82_get_init_seq(bu18xl82);
	if (ret)
		return ret;

	bu18xl82->base.funcs = &bu18xl82_bridge_funcs;
	bu18xl82->base.of_node = dev->of_node;
	bu18xl82->base.ops = DRM_BRIDGE_OP_DETECT;

	drm_bridge_add(&bu18xl82->base);

	return 0;
}

static int bu18xl82_i2c_remove(struct i2c_client *client)
{
	struct bu18xl82 *bu18xl82 = i2c_get_clientdata(client);

	drm_bridge_remove(&bu18xl82->base);

	return 0;
}

static const struct i2c_device_id bu18xl82_i2c_table[] = {
	{ "bu18xl82", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, bu18xl82_i2c_table);

static const struct of_device_id bu18xl82_of_match[] = {
	{ .compatible = "rohm,bu18tl82" },
	{ .compatible = "rohm,bu18rl82" },
	{}
};
MODULE_DEVICE_TABLE(of, bu18xl82_of_match);

static struct i2c_driver bu18xl82_i2c_driver = {
	.driver = {
		.name = "bu18xl82",
		.of_match_table = bu18xl82_of_match,
	},
	.probe = bu18xl82_i2c_probe,
	.remove = bu18xl82_i2c_remove,
	.id_table = bu18xl82_i2c_table,
};
module_i2c_driver(bu18xl82_i2c_driver);

MODULE_AUTHOR("Guochun Huang <hero.huang@rock-chips.com>");
MODULE_DESCRIPTION("ROHM BU18TL82/BU18RL82 Clockless Link-BD Serializer bridge chip driver");
MODULE_LICENSE("GPL v2");
