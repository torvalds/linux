/*
 * i.MX drm driver - LVDS display bridge
 *
 * Copyright (C) 2012 Sascha Hauer, Pengutronix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <video/of_display_timing.h>
#include <video/of_videomode.h>
#include <linux/regmap.h>
#include <linux/videodev2.h>

#include "imx-drm.h"

#define DRIVER_NAME "imx-ldb"

#define LDB_CH0_MODE_EN_TO_DI0		(1 << 0)
#define LDB_CH0_MODE_EN_TO_DI1		(3 << 0)
#define LDB_CH0_MODE_EN_MASK		(3 << 0)
#define LDB_CH1_MODE_EN_TO_DI0		(1 << 2)
#define LDB_CH1_MODE_EN_TO_DI1		(3 << 2)
#define LDB_CH1_MODE_EN_MASK		(3 << 2)
#define LDB_SPLIT_MODE_EN		(1 << 4)
#define LDB_DATA_WIDTH_CH0_24		(1 << 5)
#define LDB_BIT_MAP_CH0_JEIDA		(1 << 6)
#define LDB_DATA_WIDTH_CH1_24		(1 << 7)
#define LDB_BIT_MAP_CH1_JEIDA		(1 << 8)
#define LDB_DI0_VS_POL_ACT_LOW		(1 << 9)
#define LDB_DI1_VS_POL_ACT_LOW		(1 << 10)
#define LDB_BGREF_RMODE_INT		(1 << 15)

struct imx_ldb;

struct imx_ldb_channel {
	struct imx_ldb *ldb;
	struct drm_connector connector;
	struct drm_encoder encoder;

	/* Defines what is connected to the ldb, only one at a time */
	struct drm_panel *panel;
	struct drm_bridge *bridge;

	struct device_node *child;
	struct i2c_adapter *ddc;
	int chno;
	void *edid;
	int edid_len;
	struct drm_display_mode mode;
	int mode_valid;
	u32 bus_format;
	u32 bus_flags;
};

static inline struct imx_ldb_channel *con_to_imx_ldb_ch(struct drm_connector *c)
{
	return container_of(c, struct imx_ldb_channel, connector);
}

static inline struct imx_ldb_channel *enc_to_imx_ldb_ch(struct drm_encoder *e)
{
	return container_of(e, struct imx_ldb_channel, encoder);
}

struct bus_mux {
	int reg;
	int shift;
	int mask;
};

struct imx_ldb {
	struct regmap *regmap;
	struct device *dev;
	struct imx_ldb_channel channel[2];
	struct clk *clk[2]; /* our own clock */
	struct clk *clk_sel[4]; /* parent of display clock */
	struct clk *clk_parent[4]; /* original parent of clk_sel */
	struct clk *clk_pll[2]; /* upstream clock we can adjust */
	u32 ldb_ctrl;
	const struct bus_mux *lvds_mux;
};

static void imx_ldb_ch_set_bus_format(struct imx_ldb_channel *imx_ldb_ch,
				      u32 bus_format)
{
	struct imx_ldb *ldb = imx_ldb_ch->ldb;
	int dual = ldb->ldb_ctrl & LDB_SPLIT_MODE_EN;

	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
		if (imx_ldb_ch->chno == 0 || dual)
			ldb->ldb_ctrl |= LDB_DATA_WIDTH_CH0_24;
		if (imx_ldb_ch->chno == 1 || dual)
			ldb->ldb_ctrl |= LDB_DATA_WIDTH_CH1_24;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
		if (imx_ldb_ch->chno == 0 || dual)
			ldb->ldb_ctrl |= LDB_DATA_WIDTH_CH0_24 |
					 LDB_BIT_MAP_CH0_JEIDA;
		if (imx_ldb_ch->chno == 1 || dual)
			ldb->ldb_ctrl |= LDB_DATA_WIDTH_CH1_24 |
					 LDB_BIT_MAP_CH1_JEIDA;
		break;
	}
}

static int imx_ldb_connector_get_modes(struct drm_connector *connector)
{
	struct imx_ldb_channel *imx_ldb_ch = con_to_imx_ldb_ch(connector);
	int num_modes = 0;

	if (imx_ldb_ch->panel && imx_ldb_ch->panel->funcs &&
	    imx_ldb_ch->panel->funcs->get_modes) {
		num_modes = imx_ldb_ch->panel->funcs->get_modes(imx_ldb_ch->panel);
		if (num_modes > 0)
			return num_modes;
	}

	if (!imx_ldb_ch->edid && imx_ldb_ch->ddc)
		imx_ldb_ch->edid = drm_get_edid(connector, imx_ldb_ch->ddc);

	if (imx_ldb_ch->edid) {
		drm_mode_connector_update_edid_property(connector,
							imx_ldb_ch->edid);
		num_modes = drm_add_edid_modes(connector, imx_ldb_ch->edid);
	}

	if (imx_ldb_ch->mode_valid) {
		struct drm_display_mode *mode;

		mode = drm_mode_create(connector->dev);
		if (!mode)
			return -EINVAL;
		drm_mode_copy(mode, &imx_ldb_ch->mode);
		mode->type |= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode);
		num_modes++;
	}

	return num_modes;
}

static struct drm_encoder *imx_ldb_connector_best_encoder(
		struct drm_connector *connector)
{
	struct imx_ldb_channel *imx_ldb_ch = con_to_imx_ldb_ch(connector);

	return &imx_ldb_ch->encoder;
}

static void imx_ldb_set_clock(struct imx_ldb *ldb, int mux, int chno,
		unsigned long serial_clk, unsigned long di_clk)
{
	int ret;

	dev_dbg(ldb->dev, "%s: now: %ld want: %ld\n", __func__,
			clk_get_rate(ldb->clk_pll[chno]), serial_clk);
	clk_set_rate(ldb->clk_pll[chno], serial_clk);

	dev_dbg(ldb->dev, "%s after: %ld\n", __func__,
			clk_get_rate(ldb->clk_pll[chno]));

	dev_dbg(ldb->dev, "%s: now: %ld want: %ld\n", __func__,
			clk_get_rate(ldb->clk[chno]),
			(long int)di_clk);
	clk_set_rate(ldb->clk[chno], di_clk);

	dev_dbg(ldb->dev, "%s after: %ld\n", __func__,
			clk_get_rate(ldb->clk[chno]));

	/* set display clock mux to LDB input clock */
	ret = clk_set_parent(ldb->clk_sel[mux], ldb->clk[chno]);
	if (ret)
		dev_err(ldb->dev,
			"unable to set di%d parent clock to ldb_di%d\n", mux,
			chno);
}

static void imx_ldb_encoder_enable(struct drm_encoder *encoder)
{
	struct imx_ldb_channel *imx_ldb_ch = enc_to_imx_ldb_ch(encoder);
	struct imx_ldb *ldb = imx_ldb_ch->ldb;
	int dual = ldb->ldb_ctrl & LDB_SPLIT_MODE_EN;
	int mux = drm_of_encoder_active_port_id(imx_ldb_ch->child, encoder);

	drm_panel_prepare(imx_ldb_ch->panel);

	if (dual) {
		clk_set_parent(ldb->clk_sel[mux], ldb->clk[0]);
		clk_set_parent(ldb->clk_sel[mux], ldb->clk[1]);

		clk_prepare_enable(ldb->clk[0]);
		clk_prepare_enable(ldb->clk[1]);
	} else {
		clk_set_parent(ldb->clk_sel[mux], ldb->clk[imx_ldb_ch->chno]);
	}

	if (imx_ldb_ch == &ldb->channel[0] || dual) {
		ldb->ldb_ctrl &= ~LDB_CH0_MODE_EN_MASK;
		if (mux == 0 || ldb->lvds_mux)
			ldb->ldb_ctrl |= LDB_CH0_MODE_EN_TO_DI0;
		else if (mux == 1)
			ldb->ldb_ctrl |= LDB_CH0_MODE_EN_TO_DI1;
	}
	if (imx_ldb_ch == &ldb->channel[1] || dual) {
		ldb->ldb_ctrl &= ~LDB_CH1_MODE_EN_MASK;
		if (mux == 1 || ldb->lvds_mux)
			ldb->ldb_ctrl |= LDB_CH1_MODE_EN_TO_DI1;
		else if (mux == 0)
			ldb->ldb_ctrl |= LDB_CH1_MODE_EN_TO_DI0;
	}

	if (ldb->lvds_mux) {
		const struct bus_mux *lvds_mux = NULL;

		if (imx_ldb_ch == &ldb->channel[0])
			lvds_mux = &ldb->lvds_mux[0];
		else if (imx_ldb_ch == &ldb->channel[1])
			lvds_mux = &ldb->lvds_mux[1];

		regmap_update_bits(ldb->regmap, lvds_mux->reg, lvds_mux->mask,
				   mux << lvds_mux->shift);
	}

	regmap_write(ldb->regmap, IOMUXC_GPR2, ldb->ldb_ctrl);

	drm_panel_enable(imx_ldb_ch->panel);
}

static void
imx_ldb_encoder_atomic_mode_set(struct drm_encoder *encoder,
				struct drm_crtc_state *crtc_state,
				struct drm_connector_state *connector_state)
{
	struct imx_ldb_channel *imx_ldb_ch = enc_to_imx_ldb_ch(encoder);
	struct drm_display_mode *mode = &crtc_state->adjusted_mode;
	struct imx_ldb *ldb = imx_ldb_ch->ldb;
	int dual = ldb->ldb_ctrl & LDB_SPLIT_MODE_EN;
	unsigned long serial_clk;
	unsigned long di_clk = mode->clock * 1000;
	int mux = drm_of_encoder_active_port_id(imx_ldb_ch->child, encoder);
	u32 bus_format = imx_ldb_ch->bus_format;

	if (mode->clock > 170000) {
		dev_warn(ldb->dev,
			 "%s: mode exceeds 170 MHz pixel clock\n", __func__);
	}
	if (mode->clock > 85000 && !dual) {
		dev_warn(ldb->dev,
			 "%s: mode exceeds 85 MHz pixel clock\n", __func__);
	}

	if (dual) {
		serial_clk = 3500UL * mode->clock;
		imx_ldb_set_clock(ldb, mux, 0, serial_clk, di_clk);
		imx_ldb_set_clock(ldb, mux, 1, serial_clk, di_clk);
	} else {
		serial_clk = 7000UL * mode->clock;
		imx_ldb_set_clock(ldb, mux, imx_ldb_ch->chno, serial_clk,
				  di_clk);
	}

	/* FIXME - assumes straight connections DI0 --> CH0, DI1 --> CH1 */
	if (imx_ldb_ch == &ldb->channel[0] || dual) {
		if (mode->flags & DRM_MODE_FLAG_NVSYNC)
			ldb->ldb_ctrl |= LDB_DI0_VS_POL_ACT_LOW;
		else if (mode->flags & DRM_MODE_FLAG_PVSYNC)
			ldb->ldb_ctrl &= ~LDB_DI0_VS_POL_ACT_LOW;
	}
	if (imx_ldb_ch == &ldb->channel[1] || dual) {
		if (mode->flags & DRM_MODE_FLAG_NVSYNC)
			ldb->ldb_ctrl |= LDB_DI1_VS_POL_ACT_LOW;
		else if (mode->flags & DRM_MODE_FLAG_PVSYNC)
			ldb->ldb_ctrl &= ~LDB_DI1_VS_POL_ACT_LOW;
	}

	if (!bus_format) {
		struct drm_connector *connector = connector_state->connector;
		struct drm_display_info *di = &connector->display_info;

		if (di->num_bus_formats)
			bus_format = di->bus_formats[0];
	}
	imx_ldb_ch_set_bus_format(imx_ldb_ch, bus_format);
}

static void imx_ldb_encoder_disable(struct drm_encoder *encoder)
{
	struct imx_ldb_channel *imx_ldb_ch = enc_to_imx_ldb_ch(encoder);
	struct imx_ldb *ldb = imx_ldb_ch->ldb;
	int mux, ret;

	drm_panel_disable(imx_ldb_ch->panel);

	if (imx_ldb_ch == &ldb->channel[0])
		ldb->ldb_ctrl &= ~LDB_CH0_MODE_EN_MASK;
	else if (imx_ldb_ch == &ldb->channel[1])
		ldb->ldb_ctrl &= ~LDB_CH1_MODE_EN_MASK;

	regmap_write(ldb->regmap, IOMUXC_GPR2, ldb->ldb_ctrl);

	if (ldb->ldb_ctrl & LDB_SPLIT_MODE_EN) {
		clk_disable_unprepare(ldb->clk[0]);
		clk_disable_unprepare(ldb->clk[1]);
	}

	if (ldb->lvds_mux) {
		const struct bus_mux *lvds_mux = NULL;

		if (imx_ldb_ch == &ldb->channel[0])
			lvds_mux = &ldb->lvds_mux[0];
		else if (imx_ldb_ch == &ldb->channel[1])
			lvds_mux = &ldb->lvds_mux[1];

		regmap_read(ldb->regmap, lvds_mux->reg, &mux);
		mux &= lvds_mux->mask;
		mux >>= lvds_mux->shift;
	} else {
		mux = (imx_ldb_ch == &ldb->channel[0]) ? 0 : 1;
	}

	/* set display clock mux back to original input clock */
	ret = clk_set_parent(ldb->clk_sel[mux], ldb->clk_parent[mux]);
	if (ret)
		dev_err(ldb->dev,
			"unable to set di%d parent clock to original parent\n",
			mux);

	drm_panel_unprepare(imx_ldb_ch->panel);
}

static int imx_ldb_encoder_atomic_check(struct drm_encoder *encoder,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	struct imx_crtc_state *imx_crtc_state = to_imx_crtc_state(crtc_state);
	struct imx_ldb_channel *imx_ldb_ch = enc_to_imx_ldb_ch(encoder);
	struct drm_display_info *di = &conn_state->connector->display_info;
	u32 bus_format = imx_ldb_ch->bus_format;

	/* Bus format description in DT overrides connector display info. */
	if (!bus_format && di->num_bus_formats) {
		bus_format = di->bus_formats[0];
		imx_crtc_state->bus_flags = di->bus_flags;
	} else {
		bus_format = imx_ldb_ch->bus_format;
		imx_crtc_state->bus_flags = imx_ldb_ch->bus_flags;
	}
	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
		imx_crtc_state->bus_format = MEDIA_BUS_FMT_RGB666_1X18;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
		imx_crtc_state->bus_format = MEDIA_BUS_FMT_RGB888_1X24;
		break;
	default:
		return -EINVAL;
	}

	imx_crtc_state->di_hsync_pin = 2;
	imx_crtc_state->di_vsync_pin = 3;

	return 0;
}


static const struct drm_connector_funcs imx_ldb_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = imx_drm_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs imx_ldb_connector_helper_funcs = {
	.get_modes = imx_ldb_connector_get_modes,
	.best_encoder = imx_ldb_connector_best_encoder,
};

static const struct drm_encoder_funcs imx_ldb_encoder_funcs = {
	.destroy = imx_drm_encoder_destroy,
};

static const struct drm_encoder_helper_funcs imx_ldb_encoder_helper_funcs = {
	.atomic_mode_set = imx_ldb_encoder_atomic_mode_set,
	.enable = imx_ldb_encoder_enable,
	.disable = imx_ldb_encoder_disable,
	.atomic_check = imx_ldb_encoder_atomic_check,
};

static int imx_ldb_get_clk(struct imx_ldb *ldb, int chno)
{
	char clkname[16];

	snprintf(clkname, sizeof(clkname), "di%d", chno);
	ldb->clk[chno] = devm_clk_get(ldb->dev, clkname);
	if (IS_ERR(ldb->clk[chno]))
		return PTR_ERR(ldb->clk[chno]);

	snprintf(clkname, sizeof(clkname), "di%d_pll", chno);
	ldb->clk_pll[chno] = devm_clk_get(ldb->dev, clkname);

	return PTR_ERR_OR_ZERO(ldb->clk_pll[chno]);
}

static int imx_ldb_register(struct drm_device *drm,
	struct imx_ldb_channel *imx_ldb_ch)
{
	struct imx_ldb *ldb = imx_ldb_ch->ldb;
	struct drm_encoder *encoder = &imx_ldb_ch->encoder;
	int ret;

	ret = imx_drm_encoder_parse_of(drm, encoder, imx_ldb_ch->child);
	if (ret)
		return ret;

	ret = imx_ldb_get_clk(ldb, imx_ldb_ch->chno);
	if (ret)
		return ret;

	if (ldb->ldb_ctrl & LDB_SPLIT_MODE_EN) {
		ret = imx_ldb_get_clk(ldb, 1);
		if (ret)
			return ret;
	}

	drm_encoder_helper_add(encoder, &imx_ldb_encoder_helper_funcs);
	drm_encoder_init(drm, encoder, &imx_ldb_encoder_funcs,
			 DRM_MODE_ENCODER_LVDS, NULL);

	if (imx_ldb_ch->bridge) {
		imx_ldb_ch->bridge->encoder = encoder;

		imx_ldb_ch->encoder.bridge = imx_ldb_ch->bridge;
		ret = drm_bridge_attach(drm, imx_ldb_ch->bridge);
		if (ret) {
			DRM_ERROR("Failed to initialize bridge with drm\n");
			return ret;
		}
	} else {
		/*
		 * We want to add the connector whenever there is no bridge
		 * that brings its own, not only when there is a panel. For
		 * historical reasons, the ldb driver can also work without
		 * a panel.
		 */
		drm_connector_helper_add(&imx_ldb_ch->connector,
				&imx_ldb_connector_helper_funcs);
		drm_connector_init(drm, &imx_ldb_ch->connector,
				&imx_ldb_connector_funcs,
				DRM_MODE_CONNECTOR_LVDS);
		drm_mode_connector_attach_encoder(&imx_ldb_ch->connector,
				encoder);
	}

	if (imx_ldb_ch->panel) {
		ret = drm_panel_attach(imx_ldb_ch->panel,
				       &imx_ldb_ch->connector);
		if (ret)
			return ret;
	}

	return 0;
}

enum {
	LVDS_BIT_MAP_SPWG,
	LVDS_BIT_MAP_JEIDA
};

struct imx_ldb_bit_mapping {
	u32 bus_format;
	u32 datawidth;
	const char * const mapping;
};

static const struct imx_ldb_bit_mapping imx_ldb_bit_mappings[] = {
	{ MEDIA_BUS_FMT_RGB666_1X7X3_SPWG,  18, "spwg" },
	{ MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,  24, "spwg" },
	{ MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA, 24, "jeida" },
};

static u32 of_get_bus_format(struct device *dev, struct device_node *np)
{
	const char *bm;
	u32 datawidth = 0;
	int ret, i;

	ret = of_property_read_string(np, "fsl,data-mapping", &bm);
	if (ret < 0)
		return ret;

	of_property_read_u32(np, "fsl,data-width", &datawidth);

	for (i = 0; i < ARRAY_SIZE(imx_ldb_bit_mappings); i++) {
		if (!strcasecmp(bm, imx_ldb_bit_mappings[i].mapping) &&
		    datawidth == imx_ldb_bit_mappings[i].datawidth)
			return imx_ldb_bit_mappings[i].bus_format;
	}

	dev_err(dev, "invalid data mapping: %d-bit \"%s\"\n", datawidth, bm);

	return -ENOENT;
}

static struct bus_mux imx6q_lvds_mux[2] = {
	{
		.reg = IOMUXC_GPR3,
		.shift = 6,
		.mask = IMX6Q_GPR3_LVDS0_MUX_CTL_MASK,
	}, {
		.reg = IOMUXC_GPR3,
		.shift = 8,
		.mask = IMX6Q_GPR3_LVDS1_MUX_CTL_MASK,
	}
};

/*
 * For a device declaring compatible = "fsl,imx6q-ldb", "fsl,imx53-ldb",
 * of_match_device will walk through this list and take the first entry
 * matching any of its compatible values. Therefore, the more generic
 * entries (in this case fsl,imx53-ldb) need to be ordered last.
 */
static const struct of_device_id imx_ldb_dt_ids[] = {
	{ .compatible = "fsl,imx6q-ldb", .data = imx6q_lvds_mux, },
	{ .compatible = "fsl,imx53-ldb", .data = NULL, },
	{ }
};
MODULE_DEVICE_TABLE(of, imx_ldb_dt_ids);

static int imx_ldb_panel_ddc(struct device *dev,
		struct imx_ldb_channel *channel, struct device_node *child)
{
	struct device_node *ddc_node;
	const u8 *edidp;
	int ret;

	ddc_node = of_parse_phandle(child, "ddc-i2c-bus", 0);
	if (ddc_node) {
		channel->ddc = of_find_i2c_adapter_by_node(ddc_node);
		of_node_put(ddc_node);
		if (!channel->ddc) {
			dev_warn(dev, "failed to get ddc i2c adapter\n");
			return -EPROBE_DEFER;
		}
	}

	if (!channel->ddc) {
		/* if no DDC available, fallback to hardcoded EDID */
		dev_dbg(dev, "no ddc available\n");

		edidp = of_get_property(child, "edid",
					&channel->edid_len);
		if (edidp) {
			channel->edid = kmemdup(edidp,
						channel->edid_len,
						GFP_KERNEL);
		} else if (!channel->panel) {
			/* fallback to display-timings node */
			ret = of_get_drm_display_mode(child,
						      &channel->mode,
						      &channel->bus_flags,
						      OF_USE_NATIVE_MODE);
			if (!ret)
				channel->mode_valid = 1;
		}
	}
	return 0;
}

static int imx_ldb_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = data;
	struct device_node *np = dev->of_node;
	const struct of_device_id *of_id =
			of_match_device(imx_ldb_dt_ids, dev);
	struct device_node *child;
	struct imx_ldb *imx_ldb;
	int dual;
	int ret;
	int i;

	imx_ldb = devm_kzalloc(dev, sizeof(*imx_ldb), GFP_KERNEL);
	if (!imx_ldb)
		return -ENOMEM;

	imx_ldb->regmap = syscon_regmap_lookup_by_phandle(np, "gpr");
	if (IS_ERR(imx_ldb->regmap)) {
		dev_err(dev, "failed to get parent regmap\n");
		return PTR_ERR(imx_ldb->regmap);
	}

	imx_ldb->dev = dev;

	if (of_id)
		imx_ldb->lvds_mux = of_id->data;

	dual = of_property_read_bool(np, "fsl,dual-channel");
	if (dual)
		imx_ldb->ldb_ctrl |= LDB_SPLIT_MODE_EN;

	/*
	 * There are three different possible clock mux configurations:
	 * i.MX53:  ipu1_di0_sel, ipu1_di1_sel
	 * i.MX6q:  ipu1_di0_sel, ipu1_di1_sel, ipu2_di0_sel, ipu2_di1_sel
	 * i.MX6dl: ipu1_di0_sel, ipu1_di1_sel, lcdif_sel
	 * Map them all to di0_sel...di3_sel.
	 */
	for (i = 0; i < 4; i++) {
		char clkname[16];

		sprintf(clkname, "di%d_sel", i);
		imx_ldb->clk_sel[i] = devm_clk_get(imx_ldb->dev, clkname);
		if (IS_ERR(imx_ldb->clk_sel[i])) {
			ret = PTR_ERR(imx_ldb->clk_sel[i]);
			imx_ldb->clk_sel[i] = NULL;
			break;
		}

		imx_ldb->clk_parent[i] = clk_get_parent(imx_ldb->clk_sel[i]);
	}
	if (i == 0)
		return ret;

	for_each_child_of_node(np, child) {
		struct imx_ldb_channel *channel;
		struct device_node *ep;
		int bus_format;

		ret = of_property_read_u32(child, "reg", &i);
		if (ret || i < 0 || i > 1)
			return -EINVAL;

		if (dual && i > 0) {
			dev_warn(dev, "dual-channel mode, ignoring second output\n");
			continue;
		}

		if (!of_device_is_available(child))
			continue;

		channel = &imx_ldb->channel[i];
		channel->ldb = imx_ldb;
		channel->chno = i;
		channel->child = child;

		/*
		 * The output port is port@4 with an external 4-port mux or
		 * port@2 with the internal 2-port mux.
		 */
		ep = of_graph_get_endpoint_by_regs(child,
						   imx_ldb->lvds_mux ? 4 : 2,
						   -1);
		if (ep) {
			struct device_node *remote;

			remote = of_graph_get_remote_port_parent(ep);
			of_node_put(ep);
			if (remote) {
				channel->panel = of_drm_find_panel(remote);
				channel->bridge = of_drm_find_bridge(remote);
			} else
				return -EPROBE_DEFER;
			of_node_put(remote);

			if (!channel->panel && !channel->bridge) {
				dev_err(dev, "panel/bridge not found: %s\n",
					remote->full_name);
				return -EPROBE_DEFER;
			}
		}

		/* panel ddc only if there is no bridge */
		if (!channel->bridge) {
			ret = imx_ldb_panel_ddc(dev, channel, child);
			if (ret)
				return ret;
		}

		bus_format = of_get_bus_format(dev, child);
		if (bus_format == -EINVAL) {
			/*
			 * If no bus format was specified in the device tree,
			 * we can still get it from the connected panel later.
			 */
			if (channel->panel && channel->panel->funcs &&
			    channel->panel->funcs->get_modes)
				bus_format = 0;
		}
		if (bus_format < 0) {
			dev_err(dev, "could not determine data mapping: %d\n",
				bus_format);
			return bus_format;
		}
		channel->bus_format = bus_format;

		ret = imx_ldb_register(drm, channel);
		if (ret)
			return ret;
	}

	dev_set_drvdata(dev, imx_ldb);

	return 0;
}

static void imx_ldb_unbind(struct device *dev, struct device *master,
	void *data)
{
	struct imx_ldb *imx_ldb = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < 2; i++) {
		struct imx_ldb_channel *channel = &imx_ldb->channel[i];

		if (channel->bridge)
			drm_bridge_detach(channel->bridge);
		if (channel->panel)
			drm_panel_detach(channel->panel);

		kfree(channel->edid);
		i2c_put_adapter(channel->ddc);
	}
}

static const struct component_ops imx_ldb_ops = {
	.bind	= imx_ldb_bind,
	.unbind	= imx_ldb_unbind,
};

static int imx_ldb_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &imx_ldb_ops);
}

static int imx_ldb_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &imx_ldb_ops);
	return 0;
}

static struct platform_driver imx_ldb_driver = {
	.probe		= imx_ldb_probe,
	.remove		= imx_ldb_remove,
	.driver		= {
		.of_match_table = imx_ldb_dt_ids,
		.name	= DRIVER_NAME,
	},
};

module_platform_driver(imx_ldb_driver);

MODULE_DESCRIPTION("i.MX LVDS driver");
MODULE_AUTHOR("Sascha Hauer, Pengutronix");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
