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

struct bu18rl82 {
	struct drm_bridge base;
	struct drm_panel *panel;
	struct device *dev;
	struct regmap *regmap;
	struct serdes_init_seq *serdes_init_seq;
};

static const struct regmap_config bu18rl82_regmap_config = {
	.name = "bu18rl82",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x0700,
};

static struct bu18rl82 *bridge_to_bu18rl82(struct drm_bridge *bridge)
{
	return container_of(bridge, struct bu18rl82, base);
}

static int bu18rl82_parse_init_seq(struct device *dev, const u16 *data,
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

static int bu18rl82_get_init_seq(struct bu18rl82 *bu18rl82)
{
	struct device *dev = bu18rl82->dev;
	struct device_node *np = dev->of_node;
	const void *data;
	int len, err;

	data = of_get_property(np, "serdes-init-sequence", &len);
	if (!data) {
		dev_err(dev, "failed to get serdes-init-sequence\n");
		return -EINVAL;
	}

	bu18rl82->serdes_init_seq = devm_kzalloc(dev, sizeof(*bu18rl82->serdes_init_seq),
						 GFP_KERNEL);
	if (!bu18rl82->serdes_init_seq)
		return -ENOMEM;

	err = bu18rl82_parse_init_seq(dev, data, len, bu18rl82->serdes_init_seq);
	if (err) {
		dev_err(dev, "failed to parse serdes-init-sequence\n");
		return err;
	}

	return 0;
}

static int bu18rl82_bridge_get_modes(struct drm_bridge *bridge,
				     struct drm_connector *connector)
{
	struct bu18rl82 *bu18rl82 = bridge_to_bu18rl82(bridge);

	return drm_panel_get_modes(bu18rl82->panel, connector);
}

static int bu18rl82_bridge_attach(struct drm_bridge *bridge,
				  enum drm_bridge_attach_flags flags)
{
	struct bu18rl82 *bu18rl82 = bridge_to_bu18rl82(bridge);
	int ret;

	ret = drm_of_find_panel_or_bridge(bu18rl82->dev->of_node, 1, -1,
					  &bu18rl82->panel, NULL);
	if (ret)
		return ret;

	return 0;
}

static void bu18rl82_bridge_enable(struct drm_bridge *bridge)
{
	struct bu18rl82 *bu18rl82 = bridge_to_bu18rl82(bridge);
	struct serdes_init_seq *init_seq = bu18rl82->serdes_init_seq;
	int count = init_seq->reg_seq_cnt;

	regmap_multi_reg_write(bu18rl82->regmap, init_seq->reg_sequence, count);

	drm_panel_enable(bu18rl82->panel);
}

static void bu18rl82_bridge_disable(struct drm_bridge *bridge)
{
	struct bu18rl82 *bu18rl82 = bridge_to_bu18rl82(bridge);

	drm_panel_disable(bu18rl82->panel);

	regmap_write(bu18rl82->regmap, 0x91, 0x00);
}

static void bu18rl82_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct bu18rl82 *bu18rl82 = bridge_to_bu18rl82(bridge);

	drm_panel_prepare(bu18rl82->panel);
}

static void bu18rl82_bridge_post_disable(struct drm_bridge *bridge)
{
	struct bu18rl82 *bu18rl82 = bridge_to_bu18rl82(bridge);

	drm_panel_unprepare(bu18rl82->panel);
}

static const struct drm_bridge_funcs bu18rl82_bridge_funcs = {
	.attach = bu18rl82_bridge_attach,
	.enable = bu18rl82_bridge_enable,
	.disable = bu18rl82_bridge_disable,
	.pre_enable = bu18rl82_bridge_pre_enable,
	.post_disable = bu18rl82_bridge_post_disable,
	.get_modes = bu18rl82_bridge_get_modes,
};

static int bu18rl82_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct bu18rl82 *bu18rl82;
	int ret;

	bu18rl82 = devm_kzalloc(dev, sizeof(*bu18rl82), GFP_KERNEL);
	if (!bu18rl82)
		return -ENOMEM;

	bu18rl82->dev = dev;
	i2c_set_clientdata(client, bu18rl82);

	bu18rl82->regmap = devm_regmap_init_i2c(client, &bu18rl82_regmap_config);
	if (IS_ERR(bu18rl82->regmap))
		return dev_err_probe(dev, PTR_ERR(bu18rl82->regmap),
				     "failed to initialize regmap\n");

	ret = bu18rl82_get_init_seq(bu18rl82);
	if (ret)
		return ret;

	bu18rl82->base.funcs = &bu18rl82_bridge_funcs;
	bu18rl82->base.of_node = dev->of_node;
	bu18rl82->base.ops = DRM_BRIDGE_OP_MODES;

	drm_bridge_add(&bu18rl82->base);

	return 0;
}

static int bu18rl82_i2c_remove(struct i2c_client *client)
{
	struct bu18rl82 *bu18rl82 = i2c_get_clientdata(client);

	drm_bridge_remove(&bu18rl82->base);

	return 0;
}

static const struct i2c_device_id bu18rl82_i2c_table[] = {
	{ "bu18rl82", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, bu18rl82_i2c_table);

static const struct of_device_id bu18rl82_of_match[] = {
	{ .compatible = "rohm,bu18rl82" },
	{}
};
MODULE_DEVICE_TABLE(of, bu18rl82_of_match);

static struct i2c_driver bu18rl82_i2c_driver = {
	.driver = {
		.name = "bu18rl82",
		.of_match_table = bu18rl82_of_match,
	},
	.probe = bu18rl82_i2c_probe,
	.remove = bu18rl82_i2c_remove,
	.id_table = bu18rl82_i2c_table,
};
module_i2c_driver(bu18rl82_i2c_driver);

MODULE_AUTHOR("Guochun Huang <hero.huang@rock-chips.com>");
MODULE_DESCRIPTION("Rohm BU18RL82 Clockless Link-BD Deserializer with LVDS Interface");
MODULE_LICENSE("GPL");
