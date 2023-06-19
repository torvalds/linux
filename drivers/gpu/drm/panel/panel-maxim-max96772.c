// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/of.h>

#include <video/videomode.h>
#include <video/of_display_timing.h>
#include <video/display_timing.h>
#include <uapi/linux/media-bus-format.h>

#include <drm/drm_device.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct max96772_panel;

struct panel_desc {
	const char *name;
	u32 width_mm;
	u32 height_mm;
	u32 link_rate;
	u32 lane_count;
	bool ssc;

	int (*prepare)(struct max96772_panel *p);
	int (*unprepare)(struct max96772_panel *p);
	int (*enable)(struct max96772_panel *p);
	int (*disable)(struct max96772_panel *p);
	int (*backlight_enable)(struct max96772_panel *p);
	int (*backlight_disable)(struct max96772_panel *p);
};

struct max96772_panel {
	struct drm_panel panel;
	struct device *dev;
	struct {
		struct regmap *serializer;
		struct regmap *deserializer;
	} regmap;
	struct backlight_device *backlight;
	struct drm_display_mode mode;
	const struct panel_desc *desc;
	u32 link_rate;
	u32 lane_count;
	bool ssc;
	bool panel_dual_link;
};

#define maxim_serializer_write(p, reg, val) do {			\
		int ret;						\
		ret = regmap_write(p->regmap.serializer, reg, val);	\
		if (ret)						\
			return ret;					\
	} while (0)

#define maxim_serializer_read(p, reg, val) do {				\
		int ret;						\
		ret = regmap_read(p->regmap.serializer, reg, val);	\
		if (ret)						\
			return ret;					\
	} while (0)

#define maxim_deserializer_write(p, reg, val) do {			\
		int ret;						\
		ret = regmap_write(p->regmap.deserializer, reg, val);	\
		if (ret)						\
			return ret;					\
	} while (0)

#define maxim_deserializer_read(p, reg, val) do {			\
		int ret;						\
		ret = regmap_read(p->regmap.deserializer, reg, val);	\
		if (ret)						\
			return ret;					\
	} while (0)

static const struct reg_sequence max96772_clk_ref[3][14] = {
	{
		{ 0xe7b2, 0x50 },
		{ 0xe7b3, 0x00 },
		{ 0xe7b4, 0xcc },
		{ 0xe7b5, 0x44 },
		{ 0xe7b6, 0x81 },
		{ 0xe7b7, 0x30 },
		{ 0xe7b8, 0x07 },
		{ 0xe7b9, 0x10 },
		{ 0xe7ba, 0x01 },
		{ 0xe7bb, 0x00 },
		{ 0xe7bc, 0x00 },
		{ 0xe7bd, 0x00 },
		{ 0xe7be, 0x52 },
		{ 0xe7bf, 0x00 },
	}, {
		{ 0xe7b2, 0x50 },
		{ 0xe7b3, 0x00 },
		{ 0xe7b4, 0x00 },
		{ 0xe7b5, 0x40 },
		{ 0xe7b6, 0x6c },
		{ 0xe7b7, 0x20 },
		{ 0xe7b8, 0x07 },
		{ 0xe7b9, 0x00 },
		{ 0xe7ba, 0x01 },
		{ 0xe7bb, 0x00 },
		{ 0xe7bc, 0x00 },
		{ 0xe7bd, 0x00 },
		{ 0xe7be, 0x52 },
		{ 0xe7bf, 0x00 },
	}, {
		{ 0xe7b2, 0x30 },
		{ 0xe7b3, 0x00 },
		{ 0xe7b4, 0x00 },
		{ 0xe7b5, 0x40 },
		{ 0xe7b6, 0x6c },
		{ 0xe7b7, 0x20 },
		{ 0xe7b8, 0x14 },
		{ 0xe7b9, 0x00 },
		{ 0xe7ba, 0x2e },
		{ 0xe7bb, 0x00 },
		{ 0xe7bc, 0x00 },
		{ 0xe7bd, 0x01 },
		{ 0xe7be, 0x32 },
		{ 0xe7bf, 0x00 },
	}
};

static int max96772_aux_dpcd_read(struct max96772_panel *p, u32 reg, u32 *value)
{
	maxim_deserializer_write(p, 0xe778, reg & 0xff);
	maxim_deserializer_write(p, 0xe779, (reg >> 8) & 0xff);
	maxim_deserializer_write(p, 0xe77c, (reg >> 16) & 0xff);
	maxim_deserializer_write(p, 0xe776, 0x10);
	maxim_deserializer_write(p, 0xe777, 0x80);
	/* FIXME */
	msleep(50);
	maxim_deserializer_read(p, 0xe77a, value);

	return 0;
}

static int max96772_prepare(struct max96772_panel *p)
{
	const struct drm_display_mode *mode = &p->mode;
	u32 hfp, hsa, hbp, hact;
	u32 vact, vsa, vfp, vbp;
	u64 hwords, mvid;
	bool hsync_pol, vsync_pol;

	if (p->panel_dual_link) {
		maxim_deserializer_write(p, 0x0010, 0x00);
	}

	maxim_deserializer_write(p, 0xe790, p->link_rate);
	maxim_deserializer_write(p, 0xe792, p->lane_count);

	if (p->ssc) {
		maxim_deserializer_write(p, 0xe7b0, 0x01);
		maxim_deserializer_write(p, 0xe7b1, 0x10);
	} else {
		maxim_deserializer_write(p, 0xe7b1, 0x00);
	}

	dev_info(p->dev, "link_rate=0x%02x, lane_count=0x%02x, ssc=%d\n",
		 p->link_rate, p->lane_count, p->ssc);

	switch (p->link_rate) {
	case DP_LINK_BW_5_4:
		regmap_multi_reg_write(p->regmap.deserializer, max96772_clk_ref[2],
				       ARRAY_SIZE(max96772_clk_ref[2]));
		break;
	case DP_LINK_BW_2_7:
		regmap_multi_reg_write(p->regmap.deserializer, max96772_clk_ref[1],
				       ARRAY_SIZE(max96772_clk_ref[1]));
		break;
	case DP_LINK_BW_1_62:
	default:
		regmap_multi_reg_write(p->regmap.deserializer, max96772_clk_ref[0],
				       ARRAY_SIZE(max96772_clk_ref[0]));
		break;
	}

	vact = mode->vdisplay;
	vsa = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;
	hact = mode->hdisplay;
	hsa = mode->hsync_end - mode->hsync_start;
	hfp = mode->hsync_start - mode->hdisplay;
	hbp = mode->htotal - mode->hsync_end;

	maxim_deserializer_write(p, 0xe794, hact & 0xff);
	maxim_deserializer_write(p, 0xe795, (hact >> 8) & 0xff);
	maxim_deserializer_write(p, 0xe796, hfp & 0xff);
	maxim_deserializer_write(p, 0xe797, (hfp >> 8) & 0xff);
	maxim_deserializer_write(p, 0xe798, hsa & 0xff);
	maxim_deserializer_write(p, 0xe799, (hsa >> 8) & 0xff);
	maxim_deserializer_write(p, 0xe79a, hbp & 0xff);
	maxim_deserializer_write(p, 0xe79b, (hbp >> 8) & 0xff);
	maxim_deserializer_write(p, 0xe79c, vact & 0xff);
	maxim_deserializer_write(p, 0xe79d, (vact >> 8) & 0xff);
	maxim_deserializer_write(p, 0xe79e, vfp & 0xff);
	maxim_deserializer_write(p, 0xe79f, (vfp >> 8) & 0xff);
	maxim_deserializer_write(p, 0xe7a0, vsa & 0xff);
	maxim_deserializer_write(p, 0xe7a1, (vsa >> 8) & 0xff);
	maxim_deserializer_write(p, 0xe7a2, vbp & 0xff);
	maxim_deserializer_write(p, 0xe7a3, (vbp >> 8) & 0xff);

	hsync_pol = !!(mode->flags & DRM_MODE_FLAG_NHSYNC);
	vsync_pol = !!(mode->flags & DRM_MODE_FLAG_NVSYNC);
	maxim_deserializer_write(p, 0xe7ac, hsync_pol | (vsync_pol << 1));

	/* NVID should always be set to 0x8000 */
	maxim_deserializer_write(p, 0xe7a8, 0);
	maxim_deserializer_write(p, 0xe7a9, 0x80);

	/* HWORDS = ((HRES x bits / pixel) / 16) - LANE_COUNT */
	hwords = DIV_ROUND_CLOSEST_ULL(hact * 24, 16) - p->lane_count;
	maxim_deserializer_write(p, 0xe7a4, hwords);
	maxim_deserializer_write(p, 0xe7a5, hwords >> 8);

	/* MVID = (PCLK x NVID) x 10 / Link Rate */
	mvid = DIV_ROUND_CLOSEST_ULL((u64)mode->clock * 32768,
				     drm_dp_bw_code_to_link_rate(p->link_rate));
	maxim_deserializer_write(p, 0xe7a6, mvid & 0xff);
	maxim_deserializer_write(p, 0xe7a7, (mvid >> 8) & 0xff);

	maxim_deserializer_write(p, 0xe7aa, 0x40);
	maxim_deserializer_write(p, 0xe7ab, 0x00);

	/* set AUD_TX_EN = 0 */
	maxim_deserializer_write(p, 0x02, 0xf3);
	/* set AUD_EN_RX = 0 */
	maxim_deserializer_write(p, 0x158, 0x20);
	/* set MFP2 GPIO_TX_EN */
	maxim_deserializer_write(p, 0x2b6, 0x03);

	return 0;
}

static int max96776_enable(struct max96772_panel *p)
{
	u32 status[2];
	u32 val;
	int ret;

	/* Run link training */
	maxim_deserializer_write(p, 0xe776, 0x02);
	maxim_deserializer_write(p, 0xe777, 0x80);

	ret = regmap_read_poll_timeout(p->regmap.deserializer, 0x07f0, val,
				       val & 0x01, MSEC_PER_SEC,
				       500 * MSEC_PER_SEC);
	if (!ret)
		return 0;

	ret = max96772_aux_dpcd_read(p, DP_LANE0_1_STATUS, &status[0]);
	if (ret)
		return ret;

	ret = max96772_aux_dpcd_read(p, DP_LANE2_3_STATUS, &status[1]);
	if (ret)
		return ret;

	dev_err(p->dev, "Link Training failed: LANE0_1_STATUS=0x%02x, LANE2_3_STATUS=0x%02x\n",
		status[0], status[1]);

	return 0;
}

static inline struct max96772_panel *to_max96772_panel(struct drm_panel *panel)
{
	return container_of(panel, struct max96772_panel, panel);
}

static int max96772_panel_prepare(struct drm_panel *panel)
{
	struct max96772_panel *p = to_max96772_panel(panel);

	pinctrl_pm_select_default_state(p->dev);

	if (p->desc->prepare)
		p->desc->prepare(p);

	if (!p->desc->link_rate || !p->desc->lane_count) {
		u32 dpcd;
		int ret;

		ret = max96772_aux_dpcd_read(p, DP_MAX_LANE_COUNT, &dpcd);
		if (ret) {
			dev_err(p->dev, "failed to read max lane count\n");
			return ret;
		}

		p->lane_count = min_t(int, 4, dpcd & DP_MAX_LANE_COUNT_MASK);

		ret = max96772_aux_dpcd_read(p, DP_MAX_LINK_RATE, &dpcd);
		if (ret) {
			dev_err(p->dev, "failed to read max link rate\n");
			return ret;
		}

		p->link_rate = min_t(int, dpcd, DP_LINK_BW_5_4);

		ret = max96772_aux_dpcd_read(p, DP_MAX_DOWNSPREAD, &dpcd);
		if (ret) {
			dev_err(p->dev, "failed to read max downspread\n");
			return ret;
		}

		p->ssc = !!(dpcd & DP_MAX_DOWNSPREAD_0_5);
	} else {
		p->link_rate = p->desc->link_rate;
		p->lane_count = p->desc->lane_count;
		p->ssc = p->desc->ssc;
	}

	return max96772_prepare(p);
}

static int max96772_panel_unprepare(struct drm_panel *panel)
{
	struct max96772_panel *p = to_max96772_panel(panel);

	if (p->desc->unprepare)
		p->desc->unprepare(p);

	pinctrl_pm_select_sleep_state(p->dev);

	return 0;
}

static int max96772_panel_enable(struct drm_panel *panel)
{
	struct max96772_panel *p = to_max96772_panel(panel);

	max96776_enable(p);

	if (p->desc->enable)
		p->desc->enable(p);

	backlight_enable(p->backlight);

	if (p->desc->backlight_enable)
		p->desc->backlight_enable(p);

	return 0;
}

static int max96772_panel_disable(struct drm_panel *panel)
{
	struct max96772_panel *p = to_max96772_panel(panel);

	if (p->desc->backlight_disable)
		p->desc->backlight_disable(p);

	backlight_disable(p->backlight);

	if (p->desc->disable)
		p->desc->disable(p);

	return 0;
}

static int max96772_panel_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	struct max96772_panel *p = to_max96772_panel(panel);
	struct drm_display_mode *mode;
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	connector->display_info.width_mm = p->desc->width_mm;
	connector->display_info.height_mm = p->desc->height_mm;
	drm_display_info_set_bus_formats(&connector->display_info, &bus_format, 1);

	mode = drm_mode_duplicate(connector->dev, &p->mode);
	mode->width_mm = p->desc->width_mm;
	mode->height_mm = p->desc->height_mm;
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs max96772_panel_funcs = {
	.prepare = max96772_panel_prepare,
	.unprepare = max96772_panel_unprepare,
	.enable = max96772_panel_enable,
	.disable = max96772_panel_disable,
	.get_modes = max96772_panel_get_modes,
};

static int max96772_panel_parse_dt(struct max96772_panel *p)
{
	struct device *dev = p->dev;
	struct display_timing dt;
	struct videomode vm;
	int ret;

	ret = of_get_display_timing(dev->of_node, "panel-timing", &dt);
	if (ret < 0) {
		dev_err(dev, "%pOF: no panel-timing node found\n", dev->of_node);
		return ret;
	}

	videomode_from_timing(&dt, &vm);
	drm_display_mode_from_videomode(&vm, &p->mode);
	p->panel_dual_link = of_property_read_bool(dev->of_node, "panel_dual_link");

	return 0;
}

static const struct regmap_range max96772_readable_ranges[] = {
	regmap_reg_range(0x0000, 0x0800),
	regmap_reg_range(0x1700, 0x1700),
	regmap_reg_range(0x4100, 0x4100),
	regmap_reg_range(0x6230, 0x6230),
	regmap_reg_range(0xe75e, 0xe75e),
	regmap_reg_range(0xe776, 0xe7bf),
};

static const struct regmap_access_table max96772_readable_table = {
	.yes_ranges = max96772_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(max96772_readable_ranges),
};

static const struct regmap_config max96772_regmap_config = {
	.name = "max96772",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xffff,
	.rd_table = &max96772_readable_table,
};

static int max96772_panel_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct max96772_panel *p;
	struct i2c_client *parent;
	int ret;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->dev = dev;
	p->desc = of_device_get_match_data(dev);
	i2c_set_clientdata(client, p);

	ret = max96772_panel_parse_dt(p);
	if (ret)
		return dev_err_probe(dev, ret, "failed to parse DT\n");

	p->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(p->backlight))
		return dev_err_probe(dev, PTR_ERR(p->backlight),
				     "failed to get backlight\n");

	p->regmap.deserializer =
		devm_regmap_init_i2c(client, &max96772_regmap_config);
	if (IS_ERR(p->regmap.deserializer))
		return dev_err_probe(dev, PTR_ERR(p->regmap.deserializer),
				     "failed to initialize deserializer regmap\n");

	parent = of_find_i2c_device_by_node(dev->of_node->parent->parent);
	if (!parent)
		return dev_err_probe(dev, -ENODEV, "failed to find parent\n");

	p->regmap.serializer = dev_get_regmap(&parent->dev, NULL);
	if (!p->regmap.serializer)
		return dev_err_probe(dev, -ENODEV,
				     "failed to initialize serializer regmap\n");

	drm_panel_init(&p->panel, dev, &max96772_panel_funcs,
		       DRM_MODE_CONNECTOR_eDP);
	drm_panel_add(&p->panel);

	return 0;
}

static int max96772_panel_remove(struct i2c_client *client)
{
	struct max96772_panel *p = i2c_get_clientdata(client);

	drm_panel_remove(&p->panel);

	return 0;
}

static int boe_ae146m1t_l10_prepare(struct max96772_panel *p)
{
	return 0;
}

static int boe_ae146m1t_l10_unprepare(struct max96772_panel *p)
{
	return 0;
}


static const struct panel_desc boe_ae146m1t_l10 = {
	.name = "boe,ae146mit0-l10",
	.width_mm = 323,
	.height_mm = 182,
	.link_rate = DP_LINK_BW_2_7,
	.lane_count = 4,
	.ssc = 0,
	.prepare = boe_ae146m1t_l10_prepare,
	.unprepare = boe_ae146m1t_l10_unprepare,

};

static const struct of_device_id max96772_panel_of_match[] = {
	{ .compatible = "boe,ae146m1t-l10", &boe_ae146m1t_l10 },
	{ }
};
MODULE_DEVICE_TABLE(of, max96772_panel_of_match);

static struct i2c_driver max96772_panel_driver = {
	.driver = {
		.name = "max96772-panel",
		.of_match_table = max96772_panel_of_match,
	},
	.probe_new = max96772_panel_probe,
	.remove = max96772_panel_remove,
};

module_i2c_driver(max96772_panel_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Maxim MAX96772 based panel driver");
MODULE_LICENSE("GPL");
