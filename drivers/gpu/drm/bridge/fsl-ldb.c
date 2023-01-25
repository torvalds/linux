// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022 Marek Vasut <marex@denx.de>
 */

#include <linux/clk.h>
#include <linux/media-bus-format.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>

#define LDB_CTRL_CH0_ENABLE			BIT(0)
#define LDB_CTRL_CH0_DI_SELECT			BIT(1)
#define LDB_CTRL_CH1_ENABLE			BIT(2)
#define LDB_CTRL_CH1_DI_SELECT			BIT(3)
#define LDB_CTRL_SPLIT_MODE			BIT(4)
#define LDB_CTRL_CH0_DATA_WIDTH			BIT(5)
#define LDB_CTRL_CH0_BIT_MAPPING		BIT(6)
#define LDB_CTRL_CH1_DATA_WIDTH			BIT(7)
#define LDB_CTRL_CH1_BIT_MAPPING		BIT(8)
#define LDB_CTRL_DI0_VSYNC_POLARITY		BIT(9)
#define LDB_CTRL_DI1_VSYNC_POLARITY		BIT(10)
#define LDB_CTRL_REG_CH0_FIFO_RESET		BIT(11)
#define LDB_CTRL_REG_CH1_FIFO_RESET		BIT(12)
#define LDB_CTRL_ASYNC_FIFO_ENABLE		BIT(24)
#define LDB_CTRL_ASYNC_FIFO_THRESHOLD_MASK	GENMASK(27, 25)

#define LVDS_CTRL_CH0_EN			BIT(0)
#define LVDS_CTRL_CH1_EN			BIT(1)
/*
 * LVDS_CTRL_LVDS_EN bit is poorly named in i.MX93 reference manual.
 * Clear it to enable LVDS and set it to disable LVDS.
 */
#define LVDS_CTRL_LVDS_EN			BIT(1)
#define LVDS_CTRL_VBG_EN			BIT(2)
#define LVDS_CTRL_HS_EN				BIT(3)
#define LVDS_CTRL_PRE_EMPH_EN			BIT(4)
#define LVDS_CTRL_PRE_EMPH_ADJ(n)		(((n) & 0x7) << 5)
#define LVDS_CTRL_PRE_EMPH_ADJ_MASK		GENMASK(7, 5)
#define LVDS_CTRL_CM_ADJ(n)			(((n) & 0x7) << 8)
#define LVDS_CTRL_CM_ADJ_MASK			GENMASK(10, 8)
#define LVDS_CTRL_CC_ADJ(n)			(((n) & 0x7) << 11)
#define LVDS_CTRL_CC_ADJ_MASK			GENMASK(13, 11)
#define LVDS_CTRL_SLEW_ADJ(n)			(((n) & 0x7) << 14)
#define LVDS_CTRL_SLEW_ADJ_MASK			GENMASK(16, 14)
#define LVDS_CTRL_VBG_ADJ(n)			(((n) & 0x7) << 17)
#define LVDS_CTRL_VBG_ADJ_MASK			GENMASK(19, 17)

enum fsl_ldb_devtype {
	IMX8MP_LDB,
	IMX93_LDB,
};

struct fsl_ldb_devdata {
	u32 ldb_ctrl;
	u32 lvds_ctrl;
	bool lvds_en_bit;
};

static const struct fsl_ldb_devdata fsl_ldb_devdata[] = {
	[IMX8MP_LDB] = {
		.ldb_ctrl = 0x5c,
		.lvds_ctrl = 0x128,
	},
	[IMX93_LDB] = {
		.ldb_ctrl = 0x20,
		.lvds_ctrl = 0x24,
		.lvds_en_bit = true,
	},
};

struct fsl_ldb {
	struct device *dev;
	struct drm_bridge bridge;
	struct drm_bridge *panel_bridge;
	struct clk *clk;
	struct regmap *regmap;
	bool lvds_dual_link;
	const struct fsl_ldb_devdata *devdata;
};

static inline struct fsl_ldb *to_fsl_ldb(struct drm_bridge *bridge)
{
	return container_of(bridge, struct fsl_ldb, bridge);
}

static unsigned long fsl_ldb_link_frequency(struct fsl_ldb *fsl_ldb, int clock)
{
	if (fsl_ldb->lvds_dual_link)
		return clock * 3500;
	else
		return clock * 7000;
}

static int fsl_ldb_attach(struct drm_bridge *bridge,
			  enum drm_bridge_attach_flags flags)
{
	struct fsl_ldb *fsl_ldb = to_fsl_ldb(bridge);

	return drm_bridge_attach(bridge->encoder, fsl_ldb->panel_bridge,
				 bridge, flags);
}

static void fsl_ldb_atomic_enable(struct drm_bridge *bridge,
				  struct drm_bridge_state *old_bridge_state)
{
	struct fsl_ldb *fsl_ldb = to_fsl_ldb(bridge);
	struct drm_atomic_state *state = old_bridge_state->base.state;
	const struct drm_bridge_state *bridge_state;
	const struct drm_crtc_state *crtc_state;
	const struct drm_display_mode *mode;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	unsigned long configured_link_freq;
	unsigned long requested_link_freq;
	bool lvds_format_24bpp;
	bool lvds_format_jeida;
	u32 reg;

	/* Get the LVDS format from the bridge state. */
	bridge_state = drm_atomic_get_new_bridge_state(state, bridge);

	switch (bridge_state->output_bus_cfg.format) {
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
		lvds_format_24bpp = false;
		lvds_format_jeida = true;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
		lvds_format_24bpp = true;
		lvds_format_jeida = true;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
		lvds_format_24bpp = true;
		lvds_format_jeida = false;
		break;
	default:
		/*
		 * Some bridges still don't set the correct LVDS bus pixel
		 * format, use SPWG24 default format until those are fixed.
		 */
		lvds_format_24bpp = true;
		lvds_format_jeida = false;
		dev_warn(fsl_ldb->dev,
			 "Unsupported LVDS bus format 0x%04x, please check output bridge driver. Falling back to SPWG24.\n",
			 bridge_state->output_bus_cfg.format);
		break;
	}

	/*
	 * Retrieve the CRTC adjusted mode. This requires a little dance to go
	 * from the bridge to the encoder, to the connector and to the CRTC.
	 */
	connector = drm_atomic_get_new_connector_for_encoder(state,
							     bridge->encoder);
	crtc = drm_atomic_get_new_connector_state(state, connector)->crtc;
	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	mode = &crtc_state->adjusted_mode;

	requested_link_freq = fsl_ldb_link_frequency(fsl_ldb, mode->clock);
	clk_set_rate(fsl_ldb->clk, requested_link_freq);

	configured_link_freq = clk_get_rate(fsl_ldb->clk);
	if (configured_link_freq != requested_link_freq)
		dev_warn(fsl_ldb->dev, "Configured LDB clock (%lu Hz) does not match requested LVDS clock: %lu Hz",
			 configured_link_freq,
			 requested_link_freq);

	clk_prepare_enable(fsl_ldb->clk);

	/* Program LDB_CTRL */
	reg = LDB_CTRL_CH0_ENABLE;

	if (fsl_ldb->lvds_dual_link)
		reg |= LDB_CTRL_CH1_ENABLE | LDB_CTRL_SPLIT_MODE;

	if (lvds_format_24bpp) {
		reg |= LDB_CTRL_CH0_DATA_WIDTH;
		if (fsl_ldb->lvds_dual_link)
			reg |= LDB_CTRL_CH1_DATA_WIDTH;
	}

	if (lvds_format_jeida) {
		reg |= LDB_CTRL_CH0_BIT_MAPPING;
		if (fsl_ldb->lvds_dual_link)
			reg |= LDB_CTRL_CH1_BIT_MAPPING;
	}

	if (mode->flags & DRM_MODE_FLAG_PVSYNC) {
		reg |= LDB_CTRL_DI0_VSYNC_POLARITY;
		if (fsl_ldb->lvds_dual_link)
			reg |= LDB_CTRL_DI1_VSYNC_POLARITY;
	}

	regmap_write(fsl_ldb->regmap, fsl_ldb->devdata->ldb_ctrl, reg);

	/* Program LVDS_CTRL */
	reg = LVDS_CTRL_CC_ADJ(2) | LVDS_CTRL_PRE_EMPH_EN |
	      LVDS_CTRL_PRE_EMPH_ADJ(3) | LVDS_CTRL_VBG_EN;
	regmap_write(fsl_ldb->regmap, fsl_ldb->devdata->lvds_ctrl, reg);

	/* Wait for VBG to stabilize. */
	usleep_range(15, 20);

	reg |= LVDS_CTRL_CH0_EN;
	if (fsl_ldb->lvds_dual_link)
		reg |= LVDS_CTRL_CH1_EN;

	regmap_write(fsl_ldb->regmap, fsl_ldb->devdata->lvds_ctrl, reg);
}

static void fsl_ldb_atomic_disable(struct drm_bridge *bridge,
				   struct drm_bridge_state *old_bridge_state)
{
	struct fsl_ldb *fsl_ldb = to_fsl_ldb(bridge);

	/* Stop channel(s). */
	if (fsl_ldb->devdata->lvds_en_bit)
		/* Set LVDS_CTRL_LVDS_EN bit to disable. */
		regmap_write(fsl_ldb->regmap, fsl_ldb->devdata->lvds_ctrl,
			     LVDS_CTRL_LVDS_EN);
	else
		regmap_write(fsl_ldb->regmap, fsl_ldb->devdata->lvds_ctrl, 0);
	regmap_write(fsl_ldb->regmap, fsl_ldb->devdata->ldb_ctrl, 0);

	clk_disable_unprepare(fsl_ldb->clk);
}

#define MAX_INPUT_SEL_FORMATS 1
static u32 *
fsl_ldb_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
				  struct drm_bridge_state *bridge_state,
				  struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *conn_state,
				  u32 output_fmt,
				  unsigned int *num_input_fmts)
{
	u32 *input_fmts;

	*num_input_fmts = 0;

	input_fmts = kcalloc(MAX_INPUT_SEL_FORMATS, sizeof(*input_fmts),
			     GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	input_fmts[0] = MEDIA_BUS_FMT_RGB888_1X24;
	*num_input_fmts = MAX_INPUT_SEL_FORMATS;

	return input_fmts;
}

static enum drm_mode_status
fsl_ldb_mode_valid(struct drm_bridge *bridge,
		   const struct drm_display_info *info,
		   const struct drm_display_mode *mode)
{
	struct fsl_ldb *fsl_ldb = to_fsl_ldb(bridge);

	if (mode->clock > (fsl_ldb->lvds_dual_link ? 160000 : 80000))
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static const struct drm_bridge_funcs funcs = {
	.attach = fsl_ldb_attach,
	.atomic_enable = fsl_ldb_atomic_enable,
	.atomic_disable = fsl_ldb_atomic_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_get_input_bus_fmts = fsl_ldb_atomic_get_input_bus_fmts,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.mode_valid = fsl_ldb_mode_valid,
};

static int fsl_ldb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *panel_node;
	struct device_node *port1, *port2;
	struct drm_panel *panel;
	struct fsl_ldb *fsl_ldb;
	int dual_link;

	fsl_ldb = devm_kzalloc(dev, sizeof(*fsl_ldb), GFP_KERNEL);
	if (!fsl_ldb)
		return -ENOMEM;

	fsl_ldb->devdata = of_device_get_match_data(dev);
	if (!fsl_ldb->devdata)
		return -EINVAL;

	fsl_ldb->dev = &pdev->dev;
	fsl_ldb->bridge.funcs = &funcs;
	fsl_ldb->bridge.of_node = dev->of_node;

	fsl_ldb->clk = devm_clk_get(dev, "ldb");
	if (IS_ERR(fsl_ldb->clk))
		return PTR_ERR(fsl_ldb->clk);

	fsl_ldb->regmap = syscon_node_to_regmap(dev->of_node->parent);
	if (IS_ERR(fsl_ldb->regmap))
		return PTR_ERR(fsl_ldb->regmap);

	/* Locate the panel DT node. */
	panel_node = of_graph_get_remote_node(dev->of_node, 1, 0);
	if (!panel_node)
		return -ENXIO;

	panel = of_drm_find_panel(panel_node);
	of_node_put(panel_node);
	if (IS_ERR(panel))
		return PTR_ERR(panel);

	fsl_ldb->panel_bridge = devm_drm_panel_bridge_add(dev, panel);
	if (IS_ERR(fsl_ldb->panel_bridge))
		return PTR_ERR(fsl_ldb->panel_bridge);

	/* Determine whether this is dual-link configuration */
	port1 = of_graph_get_port_by_id(dev->of_node, 1);
	port2 = of_graph_get_port_by_id(dev->of_node, 2);
	dual_link = drm_of_lvds_get_dual_link_pixel_order(port1, port2);
	of_node_put(port1);
	of_node_put(port2);

	if (dual_link == DRM_LVDS_DUAL_LINK_EVEN_ODD_PIXELS) {
		dev_err(dev, "LVDS channel pixel swap not supported.\n");
		return -EINVAL;
	}

	if (dual_link == DRM_LVDS_DUAL_LINK_ODD_EVEN_PIXELS)
		fsl_ldb->lvds_dual_link = true;

	platform_set_drvdata(pdev, fsl_ldb);

	drm_bridge_add(&fsl_ldb->bridge);

	return 0;
}

static int fsl_ldb_remove(struct platform_device *pdev)
{
	struct fsl_ldb *fsl_ldb = platform_get_drvdata(pdev);

	drm_bridge_remove(&fsl_ldb->bridge);

	return 0;
}

static const struct of_device_id fsl_ldb_match[] = {
	{ .compatible = "fsl,imx8mp-ldb",
	  .data = &fsl_ldb_devdata[IMX8MP_LDB], },
	{ .compatible = "fsl,imx93-ldb",
	  .data = &fsl_ldb_devdata[IMX93_LDB], },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, fsl_ldb_match);

static struct platform_driver fsl_ldb_driver = {
	.probe	= fsl_ldb_probe,
	.remove	= fsl_ldb_remove,
	.driver		= {
		.name		= "fsl-ldb",
		.of_match_table	= fsl_ldb_match,
	},
};
module_platform_driver(fsl_ldb_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Freescale i.MX8MP LDB");
MODULE_LICENSE("GPL");
