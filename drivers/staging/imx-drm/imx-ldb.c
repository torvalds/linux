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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
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

#define con_to_imx_ldb_ch(x) container_of(x, struct imx_ldb_channel, connector)
#define enc_to_imx_ldb_ch(x) container_of(x, struct imx_ldb_channel, encoder)

struct imx_ldb;

struct imx_ldb_channel {
	struct imx_ldb *ldb;
	struct drm_connector connector;
	struct imx_drm_connector *imx_drm_connector;
	struct drm_encoder encoder;
	struct imx_drm_encoder *imx_drm_encoder;
	int chno;
	void *edid;
	int edid_len;
	struct drm_display_mode mode;
	int mode_valid;
};

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
	struct clk *clk_pll[2]; /* upstream clock we can adjust */
	u32 ldb_ctrl;
	const struct bus_mux *lvds_mux;
};

static enum drm_connector_status imx_ldb_connector_detect(
		struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void imx_ldb_connector_destroy(struct drm_connector *connector)
{
	/* do not free here */
}

static int imx_ldb_connector_get_modes(struct drm_connector *connector)
{
	struct imx_ldb_channel *imx_ldb_ch = con_to_imx_ldb_ch(connector);
	int num_modes = 0;

	if (imx_ldb_ch->edid) {
		drm_mode_connector_update_edid_property(connector,
							imx_ldb_ch->edid);
		num_modes = drm_add_edid_modes(connector, imx_ldb_ch->edid);
	}

	if (imx_ldb_ch->mode_valid) {
		struct drm_display_mode *mode;

		mode = drm_mode_create(connector->dev);
		drm_mode_copy(mode, &imx_ldb_ch->mode);
		mode->type |= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode);
		num_modes++;
	}

	return num_modes;
}

static int imx_ldb_connector_mode_valid(struct drm_connector *connector,
			  struct drm_display_mode *mode)
{
	return 0;
}

static struct drm_encoder *imx_ldb_connector_best_encoder(
		struct drm_connector *connector)
{
	struct imx_ldb_channel *imx_ldb_ch = con_to_imx_ldb_ch(connector);

	return &imx_ldb_ch->encoder;
}

static void imx_ldb_encoder_dpms(struct drm_encoder *encoder, int mode)
{
}

static bool imx_ldb_encoder_mode_fixup(struct drm_encoder *encoder,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode)
{
	return true;
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
	if (ret) {
		dev_err(ldb->dev, "unable to set di%d parent clock to ldb_di%d\n", mux, chno);
	}
}

static void imx_ldb_encoder_prepare(struct drm_encoder *encoder)
{
	struct imx_ldb_channel *imx_ldb_ch = enc_to_imx_ldb_ch(encoder);
	struct imx_ldb *ldb = imx_ldb_ch->ldb;
	struct drm_display_mode *mode = &encoder->crtc->mode;
	u32 pixel_fmt;
	unsigned long serial_clk;
	unsigned long di_clk = mode->clock * 1000;
	int mux = imx_drm_encoder_get_mux_id(imx_ldb_ch->imx_drm_encoder,
					     encoder->crtc);

	if (ldb->ldb_ctrl & LDB_SPLIT_MODE_EN) {
		/* dual channel LVDS mode */
		serial_clk = 3500UL * mode->clock;
		imx_ldb_set_clock(ldb, mux, 0, serial_clk, di_clk);
		imx_ldb_set_clock(ldb, mux, 1, serial_clk, di_clk);
	} else {
		serial_clk = 7000UL * mode->clock;
		imx_ldb_set_clock(ldb, mux, imx_ldb_ch->chno, serial_clk, di_clk);
	}

	switch (imx_ldb_ch->chno) {
	case 0:
		pixel_fmt = (ldb->ldb_ctrl & LDB_DATA_WIDTH_CH0_24) ?
			V4L2_PIX_FMT_RGB24 : V4L2_PIX_FMT_BGR666;
		break;
	case 1:
		pixel_fmt = (ldb->ldb_ctrl & LDB_DATA_WIDTH_CH1_24) ?
			V4L2_PIX_FMT_RGB24 : V4L2_PIX_FMT_BGR666;
		break;
	default:
		dev_err(ldb->dev, "unable to config di%d panel format\n",
			imx_ldb_ch->chno);
		pixel_fmt = V4L2_PIX_FMT_RGB24;
	}

	imx_drm_crtc_panel_format(encoder->crtc, DRM_MODE_ENCODER_LVDS,
			pixel_fmt);
}

static void imx_ldb_encoder_commit(struct drm_encoder *encoder)
{
	struct imx_ldb_channel *imx_ldb_ch = enc_to_imx_ldb_ch(encoder);
	struct imx_ldb *ldb = imx_ldb_ch->ldb;
	int dual = ldb->ldb_ctrl & LDB_SPLIT_MODE_EN;
	int mux = imx_drm_encoder_get_mux_id(imx_ldb_ch->imx_drm_encoder,
					     encoder->crtc);

	if (dual) {
		clk_prepare_enable(ldb->clk[0]);
		clk_prepare_enable(ldb->clk[1]);
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
}

static void imx_ldb_encoder_mode_set(struct drm_encoder *encoder,
			 struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode)
{
	struct imx_ldb_channel *imx_ldb_ch = enc_to_imx_ldb_ch(encoder);
	struct imx_ldb *ldb = imx_ldb_ch->ldb;
	int dual = ldb->ldb_ctrl & LDB_SPLIT_MODE_EN;

	if (mode->clock > 170000) {
		dev_warn(ldb->dev,
			 "%s: mode exceeds 170 MHz pixel clock\n", __func__);
	}
	if (mode->clock > 85000 && !dual) {
		dev_warn(ldb->dev,
			 "%s: mode exceeds 85 MHz pixel clock\n", __func__);
	}

	/* FIXME - assumes straight connections DI0 --> CH0, DI1 --> CH1 */
	if (imx_ldb_ch == &ldb->channel[0]) {
		if (mode->flags & DRM_MODE_FLAG_NVSYNC)
			ldb->ldb_ctrl |= LDB_DI0_VS_POL_ACT_LOW;
		else if (mode->flags & DRM_MODE_FLAG_PVSYNC)
			ldb->ldb_ctrl &= ~LDB_DI0_VS_POL_ACT_LOW;
	}
	if (imx_ldb_ch == &ldb->channel[1]) {
		if (mode->flags & DRM_MODE_FLAG_NVSYNC)
			ldb->ldb_ctrl |= LDB_DI1_VS_POL_ACT_LOW;
		else if (mode->flags & DRM_MODE_FLAG_PVSYNC)
			ldb->ldb_ctrl &= ~LDB_DI1_VS_POL_ACT_LOW;
	}
}

static void imx_ldb_encoder_disable(struct drm_encoder *encoder)
{
	struct imx_ldb_channel *imx_ldb_ch = enc_to_imx_ldb_ch(encoder);
	struct imx_ldb *ldb = imx_ldb_ch->ldb;

	/*
	 * imx_ldb_encoder_disable is called by
	 * drm_helper_disable_unused_functions without
	 * the encoder being enabled before.
	 */
	if (imx_ldb_ch == &ldb->channel[0] &&
	    (ldb->ldb_ctrl & LDB_CH0_MODE_EN_MASK) == 0)
		return;
	else if (imx_ldb_ch == &ldb->channel[1] &&
		 (ldb->ldb_ctrl & LDB_CH1_MODE_EN_MASK) == 0)
		return;

	if (imx_ldb_ch == &ldb->channel[0])
		ldb->ldb_ctrl &= ~LDB_CH0_MODE_EN_MASK;
	else if (imx_ldb_ch == &ldb->channel[1])
		ldb->ldb_ctrl &= ~LDB_CH1_MODE_EN_MASK;

	regmap_write(ldb->regmap, IOMUXC_GPR2, ldb->ldb_ctrl);

	if (ldb->ldb_ctrl & LDB_SPLIT_MODE_EN) {
		clk_disable_unprepare(ldb->clk[0]);
		clk_disable_unprepare(ldb->clk[1]);
	}
}

static void imx_ldb_encoder_destroy(struct drm_encoder *encoder)
{
	/* do not free here */
}

static struct drm_connector_funcs imx_ldb_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = imx_ldb_connector_detect,
	.destroy = imx_ldb_connector_destroy,
};

static struct drm_connector_helper_funcs imx_ldb_connector_helper_funcs = {
	.get_modes = imx_ldb_connector_get_modes,
	.best_encoder = imx_ldb_connector_best_encoder,
	.mode_valid = imx_ldb_connector_mode_valid,
};

static struct drm_encoder_funcs imx_ldb_encoder_funcs = {
	.destroy = imx_ldb_encoder_destroy,
};

static struct drm_encoder_helper_funcs imx_ldb_encoder_helper_funcs = {
	.dpms = imx_ldb_encoder_dpms,
	.mode_fixup = imx_ldb_encoder_mode_fixup,
	.prepare = imx_ldb_encoder_prepare,
	.commit = imx_ldb_encoder_commit,
	.mode_set = imx_ldb_encoder_mode_set,
	.disable = imx_ldb_encoder_disable,
};

static int imx_ldb_get_clk(struct imx_ldb *ldb, int chno)
{
	char clkname[16];

	sprintf(clkname, "di%d", chno);
	ldb->clk[chno] = devm_clk_get(ldb->dev, clkname);
	if (IS_ERR(ldb->clk[chno]))
		return PTR_ERR(ldb->clk[chno]);

	sprintf(clkname, "di%d_pll", chno);
	ldb->clk_pll[chno] = devm_clk_get(ldb->dev, clkname);
	if (IS_ERR(ldb->clk_pll[chno]))
		return PTR_ERR(ldb->clk_pll[chno]);

	return 0;
}

static int imx_ldb_register(struct imx_ldb_channel *imx_ldb_ch)
{
	int ret;
	struct imx_ldb *ldb = imx_ldb_ch->ldb;

	ret = imx_ldb_get_clk(ldb, imx_ldb_ch->chno);
	if (ret)
		return ret;
	if (ldb->ldb_ctrl & LDB_SPLIT_MODE_EN) {
		ret |= imx_ldb_get_clk(ldb, 1);
		if (ret)
			return ret;
	}

	imx_ldb_ch->connector.funcs = &imx_ldb_connector_funcs;
	imx_ldb_ch->encoder.funcs = &imx_ldb_encoder_funcs;

	imx_ldb_ch->encoder.encoder_type = DRM_MODE_ENCODER_LVDS;
	imx_ldb_ch->connector.connector_type = DRM_MODE_CONNECTOR_LVDS;

	drm_encoder_helper_add(&imx_ldb_ch->encoder,
			&imx_ldb_encoder_helper_funcs);
	ret = imx_drm_add_encoder(&imx_ldb_ch->encoder,
			&imx_ldb_ch->imx_drm_encoder, THIS_MODULE);
	if (ret) {
		dev_err(ldb->dev, "adding encoder failed with %d\n", ret);
		return ret;
	}

	drm_connector_helper_add(&imx_ldb_ch->connector,
			&imx_ldb_connector_helper_funcs);

	ret = imx_drm_add_connector(&imx_ldb_ch->connector,
			&imx_ldb_ch->imx_drm_connector, THIS_MODULE);
	if (ret) {
		imx_drm_remove_encoder(imx_ldb_ch->imx_drm_encoder);
		dev_err(ldb->dev, "adding connector failed with %d\n", ret);
		return ret;
	}

	drm_mode_connector_attach_encoder(&imx_ldb_ch->connector,
			&imx_ldb_ch->encoder);

	return 0;
}

enum {
	LVDS_BIT_MAP_SPWG,
	LVDS_BIT_MAP_JEIDA
};

static const char *imx_ldb_bit_mappings[] = {
	[LVDS_BIT_MAP_SPWG]  = "spwg",
	[LVDS_BIT_MAP_JEIDA] = "jeida",
};

const int of_get_data_mapping(struct device_node *np)
{
	const char *bm;
	int ret, i;

	ret = of_property_read_string(np, "fsl,data-mapping", &bm);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(imx_ldb_bit_mappings); i++)
		if (!strcasecmp(bm, imx_ldb_bit_mappings[i]))
			return i;

	return -EINVAL;
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

static int imx_ldb_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *of_id =
			of_match_device(of_match_ptr(imx_ldb_dt_ids),
					&pdev->dev);
	struct device_node *child;
	const u8 *edidp;
	struct imx_ldb *imx_ldb;
	int datawidth;
	int mapping;
	int dual;
	int ret;
	int i;

	imx_ldb = devm_kzalloc(&pdev->dev, sizeof(*imx_ldb), GFP_KERNEL);
	if (!imx_ldb)
		return -ENOMEM;

	imx_ldb->regmap = syscon_regmap_lookup_by_phandle(np, "gpr");
	if (IS_ERR(imx_ldb->regmap)) {
		dev_err(&pdev->dev, "failed to get parent regmap\n");
		return PTR_ERR(imx_ldb->regmap);
	}

	imx_ldb->dev = &pdev->dev;

	if (of_id)
		imx_ldb->lvds_mux = of_id->data;

	dual = of_property_read_bool(np, "fsl,dual-channel");
	if (dual)
		imx_ldb->ldb_ctrl |= LDB_SPLIT_MODE_EN;

	/*
	 * There are three diferent possible clock mux configurations:
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
	}
	if (i == 0)
		return ret;

	for_each_child_of_node(np, child) {
		struct imx_ldb_channel *channel;

		ret = of_property_read_u32(child, "reg", &i);
		if (ret || i < 0 || i > 1)
			return -EINVAL;

		if (dual && i > 0) {
			dev_warn(&pdev->dev, "dual-channel mode, ignoring second output\n");
			continue;
		}

		if (!of_device_is_available(child))
			continue;

		channel = &imx_ldb->channel[i];
		channel->ldb = imx_ldb;
		channel->chno = i;

		edidp = of_get_property(child, "edid", &channel->edid_len);
		if (edidp) {
			channel->edid = kmemdup(edidp, channel->edid_len,
						GFP_KERNEL);
		} else {
			ret = of_get_drm_display_mode(child, &channel->mode, 0);
			if (!ret)
				channel->mode_valid = 1;
		}

		ret = of_property_read_u32(child, "fsl,data-width", &datawidth);
		if (ret)
			datawidth = 0;
		else if (datawidth != 18 && datawidth != 24)
			return -EINVAL;

		mapping = of_get_data_mapping(child);
		switch (mapping) {
		case LVDS_BIT_MAP_SPWG:
			if (datawidth == 24) {
				if (i == 0 || dual)
					imx_ldb->ldb_ctrl |= LDB_DATA_WIDTH_CH0_24;
				if (i == 1 || dual)
					imx_ldb->ldb_ctrl |= LDB_DATA_WIDTH_CH1_24;
			}
			break;
		case LVDS_BIT_MAP_JEIDA:
			if (datawidth == 18) {
				dev_err(&pdev->dev, "JEIDA standard only supported in 24 bit\n");
				return -EINVAL;
			}
			if (i == 0 || dual)
				imx_ldb->ldb_ctrl |= LDB_DATA_WIDTH_CH0_24 | LDB_BIT_MAP_CH0_JEIDA;
			if (i == 1 || dual)
				imx_ldb->ldb_ctrl |= LDB_DATA_WIDTH_CH1_24 | LDB_BIT_MAP_CH1_JEIDA;
			break;
		default:
			dev_err(&pdev->dev, "data mapping not specified or invalid\n");
			return -EINVAL;
		}

		ret = imx_ldb_register(channel);
		if (ret)
			return ret;

		imx_drm_encoder_add_possible_crtcs(channel->imx_drm_encoder, child);
	}

	platform_set_drvdata(pdev, imx_ldb);

	return 0;
}

static int imx_ldb_remove(struct platform_device *pdev)
{
	struct imx_ldb *imx_ldb = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < 2; i++) {
		struct imx_ldb_channel *channel = &imx_ldb->channel[i];
		struct drm_connector *connector = &channel->connector;
		struct drm_encoder *encoder = &channel->encoder;

		drm_mode_connector_detach_encoder(connector, encoder);

		imx_drm_remove_connector(channel->imx_drm_connector);
		imx_drm_remove_encoder(channel->imx_drm_encoder);
	}

	return 0;
}

static struct platform_driver imx_ldb_driver = {
	.probe		= imx_ldb_probe,
	.remove		= imx_ldb_remove,
	.driver		= {
		.of_match_table = imx_ldb_dt_ids,
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(imx_ldb_driver);

MODULE_DESCRIPTION("i.MX LVDS driver");
MODULE_AUTHOR("Sascha Hauer, Pengutronix");
MODULE_LICENSE("GPL");
