// SPDX-License-Identifier: GPL-2.0
/*
 * Lontium LT9211 bridge driver
 *
 * LT9211 is capable of converting:
 *   2xDSI/2xLVDS/1xDPI -> 2xDSI/2xLVDS/1xDPI
 * Currently supported is:
 *   1xDSI -> 1xLVDS
 *
 * Copyright (C) 2022 Marek Vasut <marex@denx.de>
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#define REG_PAGE_CONTROL			0xff
#define REG_CHIPID0				0x8100
#define REG_CHIPID0_VALUE			0x18
#define REG_CHIPID1				0x8101
#define REG_CHIPID1_VALUE			0x01
#define REG_CHIPID2				0x8102
#define REG_CHIPID2_VALUE			0xe3

#define REG_DSI_LANE				0xd000
/* DSI lane count - 0 means 4 lanes ; 1, 2, 3 means 1, 2, 3 lanes. */
#define REG_DSI_LANE_COUNT(n)			((n) & 3)

struct lt9211 {
	struct drm_bridge		bridge;
	struct device			*dev;
	struct regmap			*regmap;
	struct mipi_dsi_device		*dsi;
	struct drm_bridge		*panel_bridge;
	struct gpio_desc		*reset_gpio;
	struct regulator		*vccio;
	bool				lvds_dual_link;
	bool				lvds_dual_link_even_odd_swap;
};

static const struct regmap_range lt9211_rw_ranges[] = {
	regmap_reg_range(0xff, 0xff),
	regmap_reg_range(0x8100, 0x816b),
	regmap_reg_range(0x8200, 0x82aa),
	regmap_reg_range(0x8500, 0x85ff),
	regmap_reg_range(0x8600, 0x86a0),
	regmap_reg_range(0x8700, 0x8746),
	regmap_reg_range(0xd000, 0xd0a7),
	regmap_reg_range(0xd400, 0xd42c),
	regmap_reg_range(0xd800, 0xd838),
	regmap_reg_range(0xd9c0, 0xd9d5),
};

static const struct regmap_access_table lt9211_rw_table = {
	.yes_ranges = lt9211_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(lt9211_rw_ranges),
};

static const struct regmap_range_cfg lt9211_range = {
	.name = "lt9211",
	.range_min = 0x0000,
	.range_max = 0xda00,
	.selector_reg = REG_PAGE_CONTROL,
	.selector_mask = 0xff,
	.selector_shift = 0,
	.window_start = 0,
	.window_len = 0x100,
};

static const struct regmap_config lt9211_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.rd_table = &lt9211_rw_table,
	.wr_table = &lt9211_rw_table,
	.volatile_table	= &lt9211_rw_table,
	.ranges = &lt9211_range,
	.num_ranges = 1,
	.cache_type = REGCACHE_MAPLE,
	.max_register = 0xda00,
};

static struct lt9211 *bridge_to_lt9211(struct drm_bridge *bridge)
{
	return container_of(bridge, struct lt9211, bridge);
}

static int lt9211_attach(struct drm_bridge *bridge,
			 enum drm_bridge_attach_flags flags)
{
	struct lt9211 *ctx = bridge_to_lt9211(bridge);

	return drm_bridge_attach(bridge->encoder, ctx->panel_bridge,
				 &ctx->bridge, flags);
}

static int lt9211_read_chipid(struct lt9211 *ctx)
{
	u8 chipid[3];
	int ret;

	/* Read Chip ID registers and verify the chip can communicate. */
	ret = regmap_bulk_read(ctx->regmap, REG_CHIPID0, chipid, 3);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to read Chip ID: %d\n", ret);
		return ret;
	}

	/* Test for known Chip ID. */
	if (chipid[0] != REG_CHIPID0_VALUE || chipid[1] != REG_CHIPID1_VALUE ||
	    chipid[2] != REG_CHIPID2_VALUE) {
		dev_err(ctx->dev, "Unknown Chip ID: 0x%02x 0x%02x 0x%02x\n",
			chipid[0], chipid[1], chipid[2]);
		return -EINVAL;
	}

	return 0;
}

static int lt9211_system_init(struct lt9211 *ctx)
{
	const struct reg_sequence lt9211_system_init_seq[] = {
		{ 0x8201, 0x18 },
		{ 0x8606, 0x61 },
		{ 0x8607, 0xa8 },
		{ 0x8714, 0x08 },
		{ 0x8715, 0x00 },
		{ 0x8718, 0x0f },
		{ 0x8722, 0x08 },
		{ 0x8723, 0x00 },
		{ 0x8726, 0x0f },
		{ 0x810b, 0xfe },
	};

	return regmap_multi_reg_write(ctx->regmap, lt9211_system_init_seq,
				      ARRAY_SIZE(lt9211_system_init_seq));
}

static int lt9211_configure_rx(struct lt9211 *ctx)
{
	const struct reg_sequence lt9211_rx_phy_seq[] = {
		{ 0x8202, 0x44 },
		{ 0x8204, 0xa0 },
		{ 0x8205, 0x22 },
		{ 0x8207, 0x9f },
		{ 0x8208, 0xfc },
		/* ORR with 0xf8 here to enable DSI DN/DP swap. */
		{ 0x8209, 0x01 },
		{ 0x8217, 0x0c },
		{ 0x8633, 0x1b },
	};

	const struct reg_sequence lt9211_rx_cal_reset_seq[] = {
		{ 0x8120, 0x7f },
		{ 0x8120, 0xff },
	};

	const struct reg_sequence lt9211_rx_dig_seq[] = {
		{ 0x8630, 0x85 },
		/* 0x8588: BIT 6 set = MIPI-RX, BIT 4 unset = LVDS-TX */
		{ 0x8588, 0x40 },
		{ 0x85ff, 0xd0 },
		{ REG_DSI_LANE, REG_DSI_LANE_COUNT(ctx->dsi->lanes) },
		{ 0xd002, 0x05 },
	};

	const struct reg_sequence lt9211_rx_div_reset_seq[] = {
		{ 0x810a, 0xc0 },
		{ 0x8120, 0xbf },
	};

	const struct reg_sequence lt9211_rx_div_clear_seq[] = {
		{ 0x810a, 0xc1 },
		{ 0x8120, 0xff },
	};

	int ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211_rx_phy_seq,
				     ARRAY_SIZE(lt9211_rx_phy_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211_rx_cal_reset_seq,
				     ARRAY_SIZE(lt9211_rx_cal_reset_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211_rx_dig_seq,
				     ARRAY_SIZE(lt9211_rx_dig_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, lt9211_rx_div_reset_seq,
				     ARRAY_SIZE(lt9211_rx_div_reset_seq));
	if (ret)
		return ret;

	usleep_range(10000, 15000);

	return regmap_multi_reg_write(ctx->regmap, lt9211_rx_div_clear_seq,
				      ARRAY_SIZE(lt9211_rx_div_clear_seq));
}

static int lt9211_autodetect_rx(struct lt9211 *ctx,
				const struct drm_display_mode *mode)
{
	u16 width, height;
	u32 byteclk;
	u8 buf[5];
	u8 format;
	u8 bc[3];
	int ret;

	/* Measure ByteClock frequency. */
	ret = regmap_write(ctx->regmap, 0x8600, 0x01);
	if (ret)
		return ret;

	/* Give the chip time to lock onto RX stream. */
	msleep(100);

	/* Read the ByteClock frequency from the chip. */
	ret = regmap_bulk_read(ctx->regmap, 0x8608, bc, sizeof(bc));
	if (ret)
		return ret;

	/* RX ByteClock in kHz */
	byteclk = ((bc[0] & 0xf) << 16) | (bc[1] << 8) | bc[2];

	/* Width/Height/Format Auto-detection */
	ret = regmap_bulk_read(ctx->regmap, 0xd082, buf, sizeof(buf));
	if (ret)
		return ret;

	width = (buf[0] << 8) | buf[1];
	height = (buf[3] << 8) | buf[4];
	format = buf[2] & 0xf;

	if (format == 0x3) {		/* YUV422 16bit */
		width /= 2;
	} else if (format == 0xa) {	/* RGB888 24bit */
		width /= 3;
	} else {
		dev_err(ctx->dev, "Unsupported DSI pixel format 0x%01x\n",
			format);
		return -EINVAL;
	}

	if (width != mode->hdisplay) {
		dev_err(ctx->dev,
			"RX: Detected DSI width (%d) does not match mode hdisplay (%d)\n",
			width, mode->hdisplay);
		return -EINVAL;
	}

	if (height != mode->vdisplay) {
		dev_err(ctx->dev,
			"RX: Detected DSI height (%d) does not match mode vdisplay (%d)\n",
			height, mode->vdisplay);
		return -EINVAL;
	}

	dev_dbg(ctx->dev, "RX: %dx%d format=0x%01x byteclock=%d kHz\n",
		width, height, format, byteclk);

	return 0;
}

static int lt9211_configure_timing(struct lt9211 *ctx,
				   const struct drm_display_mode *mode)
{
	const struct reg_sequence lt9211_timing[] = {
		{ 0xd00d, (mode->vtotal >> 8) & 0xff },
		{ 0xd00e, mode->vtotal & 0xff },
		{ 0xd00f, (mode->vdisplay >> 8) & 0xff },
		{ 0xd010, mode->vdisplay & 0xff },
		{ 0xd011, (mode->htotal >> 8) & 0xff },
		{ 0xd012, mode->htotal & 0xff },
		{ 0xd013, (mode->hdisplay >> 8) & 0xff },
		{ 0xd014, mode->hdisplay & 0xff },
		{ 0xd015, (mode->vsync_end - mode->vsync_start) & 0xff },
		{ 0xd016, (mode->hsync_end - mode->hsync_start) & 0xff },
		{ 0xd017, ((mode->vsync_start - mode->vdisplay) >> 8) & 0xff },
		{ 0xd018, (mode->vsync_start - mode->vdisplay) & 0xff },
		{ 0xd019, ((mode->hsync_start - mode->hdisplay) >> 8) & 0xff },
		{ 0xd01a, (mode->hsync_start - mode->hdisplay) & 0xff },
	};

	return regmap_multi_reg_write(ctx->regmap, lt9211_timing,
				      ARRAY_SIZE(lt9211_timing));
}

static int lt9211_configure_plls(struct lt9211 *ctx,
				 const struct drm_display_mode *mode)
{
	const struct reg_sequence lt9211_pcr_seq[] = {
		{ 0xd026, 0x17 },
		{ 0xd027, 0xc3 },
		{ 0xd02d, 0x30 },
		{ 0xd031, 0x10 },
		{ 0xd023, 0x20 },
		{ 0xd038, 0x02 },
		{ 0xd039, 0x10 },
		{ 0xd03a, 0x20 },
		{ 0xd03b, 0x60 },
		{ 0xd03f, 0x04 },
		{ 0xd040, 0x08 },
		{ 0xd041, 0x10 },
		{ 0x810b, 0xee },
		{ 0x810b, 0xfe },
	};

	unsigned int pval;
	int ret;

	/* DeSSC PLL reference clock is 25 MHz XTal. */
	ret = regmap_write(ctx->regmap, 0x822d, 0x48);
	if (ret)
		return ret;

	if (mode->clock < 44000) {
		ret = regmap_write(ctx->regmap, 0x8235, 0x83);
	} else if (mode->clock < 88000) {
		ret = regmap_write(ctx->regmap, 0x8235, 0x82);
	} else if (mode->clock < 176000) {
		ret = regmap_write(ctx->regmap, 0x8235, 0x81);
	} else {
		dev_err(ctx->dev,
			"Unsupported mode clock (%d kHz) above 176 MHz.\n",
			mode->clock);
		return -EINVAL;
	}

	if (ret)
		return ret;

	/* Wait for the DeSSC PLL to stabilize. */
	msleep(100);

	ret = regmap_multi_reg_write(ctx->regmap, lt9211_pcr_seq,
				     ARRAY_SIZE(lt9211_pcr_seq));
	if (ret)
		return ret;

	/* PCR stability test takes seconds. */
	ret = regmap_read_poll_timeout(ctx->regmap, 0xd087, pval, pval & 0x8,
				       20000, 10000000);
	if (ret)
		dev_err(ctx->dev, "PCR unstable, ret=%i\n", ret);

	return ret;
}

static int lt9211_configure_tx(struct lt9211 *ctx, bool jeida,
			       bool bpp24, bool de)
{
	const struct reg_sequence system_lt9211_tx_phy_seq[] = {
		/* DPI output disable */
		{ 0x8262, 0x00 },
		/* BIT(7) is LVDS dual-port */
		{ 0x823b, 0x38 | (ctx->lvds_dual_link ? BIT(7) : 0) },
		{ 0x823e, 0x92 },
		{ 0x823f, 0x48 },
		{ 0x8240, 0x31 },
		{ 0x8243, 0x80 },
		{ 0x8244, 0x00 },
		{ 0x8245, 0x00 },
		{ 0x8249, 0x00 },
		{ 0x824a, 0x01 },
		{ 0x824e, 0x00 },
		{ 0x824f, 0x00 },
		{ 0x8250, 0x00 },
		{ 0x8253, 0x00 },
		{ 0x8254, 0x01 },
		/* LVDS channel order, Odd:Even 0x10..A:B, 0x40..B:A */
		{ 0x8646, ctx->lvds_dual_link_even_odd_swap ? 0x40 : 0x10 },
		{ 0x8120, 0x7b },
		{ 0x816b, 0xff },
	};

	const struct reg_sequence system_lt9211_tx_dig_seq[] = {
		{ 0x8559, 0x40 | (jeida ? BIT(7) : 0) |
			  (de ? BIT(5) : 0) | (bpp24 ? BIT(4) : 0) },
		{ 0x855a, 0xaa },
		{ 0x855b, 0xaa },
		{ 0x855c, ctx->lvds_dual_link ? BIT(0) : 0 },
		{ 0x85a1, 0x77 },
		{ 0x8640, 0x40 },
		{ 0x8641, 0x34 },
		{ 0x8642, 0x10 },
		{ 0x8643, 0x23 },
		{ 0x8644, 0x41 },
		{ 0x8645, 0x02 },
	};

	const struct reg_sequence system_lt9211_tx_pll_seq[] = {
		/* TX PLL power down */
		{ 0x8236, 0x01 },
		{ 0x8237, ctx->lvds_dual_link ? 0x2a : 0x29 },
		{ 0x8238, 0x06 },
		{ 0x8239, 0x30 },
		{ 0x823a, 0x8e },
		{ 0x8737, 0x14 },
		{ 0x8713, 0x00 },
		{ 0x8713, 0x80 },
	};

	unsigned int pval;
	int ret;

	ret = regmap_multi_reg_write(ctx->regmap, system_lt9211_tx_phy_seq,
				     ARRAY_SIZE(system_lt9211_tx_phy_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, system_lt9211_tx_dig_seq,
				     ARRAY_SIZE(system_lt9211_tx_dig_seq));
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(ctx->regmap, system_lt9211_tx_pll_seq,
				     ARRAY_SIZE(system_lt9211_tx_pll_seq));
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(ctx->regmap, 0x871f, pval, pval & 0x80,
				       10000, 1000000);
	if (ret) {
		dev_err(ctx->dev, "TX PLL unstable, ret=%i\n", ret);
		return ret;
	}

	ret = regmap_read_poll_timeout(ctx->regmap, 0x8720, pval, pval & 0x80,
				       10000, 1000000);
	if (ret) {
		dev_err(ctx->dev, "TX PLL unstable, ret=%i\n", ret);
		return ret;
	}

	return 0;
}

static void lt9211_atomic_enable(struct drm_bridge *bridge,
				 struct drm_atomic_state *state)
{
	struct lt9211 *ctx = bridge_to_lt9211(bridge);
	const struct drm_bridge_state *bridge_state;
	const struct drm_crtc_state *crtc_state;
	const struct drm_display_mode *mode;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	bool lvds_format_24bpp;
	bool lvds_format_jeida;
	u32 bus_flags;
	int ret;

	ret = regulator_enable(ctx->vccio);
	if (ret) {
		dev_err(ctx->dev, "Failed to enable vccio: %d\n", ret);
		return;
	}

	/* Deassert reset */
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(20000, 21000);	/* Very long post-reset delay. */

	/* Get the LVDS format from the bridge state. */
	bridge_state = drm_atomic_get_new_bridge_state(state, bridge);
	bus_flags = bridge_state->output_bus_cfg.flags;

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
		 * Some bridges still don't set the correct
		 * LVDS bus pixel format, use SPWG24 default
		 * format until those are fixed.
		 */
		lvds_format_24bpp = true;
		lvds_format_jeida = false;
		dev_warn(ctx->dev,
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

	ret = lt9211_read_chipid(ctx);
	if (ret)
		return;

	ret = lt9211_system_init(ctx);
	if (ret)
		return;

	ret = lt9211_configure_rx(ctx);
	if (ret)
		return;

	ret = lt9211_autodetect_rx(ctx, mode);
	if (ret)
		return;

	ret = lt9211_configure_timing(ctx, mode);
	if (ret)
		return;

	ret = lt9211_configure_plls(ctx, mode);
	if (ret)
		return;

	ret = lt9211_configure_tx(ctx, lvds_format_jeida, lvds_format_24bpp,
				  bus_flags & DRM_BUS_FLAG_DE_HIGH);
	if (ret)
		return;

	dev_dbg(ctx->dev, "LT9211 enabled.\n");
}

static void lt9211_atomic_disable(struct drm_bridge *bridge,
				  struct drm_atomic_state *state)
{
	struct lt9211 *ctx = bridge_to_lt9211(bridge);
	int ret;

	/*
	 * Put the chip in reset, pull nRST line low,
	 * and assure lengthy 10ms reset low timing.
	 */
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);	/* Very long reset duration. */

	ret = regulator_disable(ctx->vccio);
	if (ret)
		dev_err(ctx->dev, "Failed to disable vccio: %d\n", ret);

	regcache_mark_dirty(ctx->regmap);
}

static enum drm_mode_status
lt9211_mode_valid(struct drm_bridge *bridge,
		  const struct drm_display_info *info,
		  const struct drm_display_mode *mode)
{
	/* LVDS output clock range 25..176 MHz */
	if (mode->clock < 25000)
		return MODE_CLOCK_LOW;
	if (mode->clock > 176000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

#define MAX_INPUT_SEL_FORMATS	1

static u32 *
lt9211_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
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

	/* This is the DSI-end bus format */
	input_fmts[0] = MEDIA_BUS_FMT_RGB888_1X24;
	*num_input_fmts = 1;

	return input_fmts;
}

static const struct drm_bridge_funcs lt9211_funcs = {
	.attach			= lt9211_attach,
	.mode_valid		= lt9211_mode_valid,
	.atomic_enable		= lt9211_atomic_enable,
	.atomic_disable		= lt9211_atomic_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_bridge_destroy_state,
	.atomic_get_input_bus_fmts = lt9211_atomic_get_input_bus_fmts,
	.atomic_reset		= drm_atomic_helper_bridge_reset,
};

static int lt9211_parse_dt(struct lt9211 *ctx)
{
	struct device_node *port2, *port3;
	struct drm_bridge *panel_bridge;
	struct device *dev = ctx->dev;
	struct drm_panel *panel;
	int dual_link;
	int ret;

	ctx->vccio = devm_regulator_get(dev, "vccio");
	if (IS_ERR(ctx->vccio))
		return dev_err_probe(dev, PTR_ERR(ctx->vccio),
				     "Failed to get supply 'vccio'\n");

	ctx->lvds_dual_link = false;
	ctx->lvds_dual_link_even_odd_swap = false;

	port2 = of_graph_get_port_by_id(dev->of_node, 2);
	port3 = of_graph_get_port_by_id(dev->of_node, 3);
	dual_link = drm_of_lvds_get_dual_link_pixel_order(port2, port3);
	of_node_put(port2);
	of_node_put(port3);

	if (dual_link == DRM_LVDS_DUAL_LINK_ODD_EVEN_PIXELS) {
		ctx->lvds_dual_link = true;
		/* Odd pixels to LVDS Channel A, even pixels to B */
		ctx->lvds_dual_link_even_odd_swap = false;
	} else if (dual_link == DRM_LVDS_DUAL_LINK_EVEN_ODD_PIXELS) {
		ctx->lvds_dual_link = true;
		/* Even pixels to LVDS Channel A, odd pixels to B */
		ctx->lvds_dual_link_even_odd_swap = true;
	}

	ret = drm_of_find_panel_or_bridge(dev->of_node, 2, 0, &panel, &panel_bridge);
	if (ret < 0)
		return ret;
	if (panel) {
		panel_bridge = devm_drm_panel_bridge_add(dev, panel);
		if (IS_ERR(panel_bridge))
			return PTR_ERR(panel_bridge);
	}

	ctx->panel_bridge = panel_bridge;

	return 0;
}

static int lt9211_host_attach(struct lt9211 *ctx)
{
	const struct mipi_dsi_device_info info = {
		.type = "lt9211",
		.channel = 0,
		.node = NULL,
	};
	struct device *dev = ctx->dev;
	struct device_node *host_node;
	struct device_node *endpoint;
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	int dsi_lanes;
	int ret;

	endpoint = of_graph_get_endpoint_by_regs(dev->of_node, 0, -1);
	dsi_lanes = drm_of_get_data_lanes_count(endpoint, 1, 4);
	host_node = of_graph_get_remote_port_parent(endpoint);
	host = of_find_mipi_dsi_host_by_node(host_node);
	of_node_put(host_node);
	of_node_put(endpoint);

	if (!host)
		return -EPROBE_DEFER;

	if (dsi_lanes < 0)
		return dsi_lanes;

	dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	if (IS_ERR(dsi))
		return dev_err_probe(dev, PTR_ERR(dsi),
				     "failed to create dsi device\n");

	ctx->dsi = dsi;

	dsi->lanes = dsi_lanes;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO_NO_HSA |
			  MIPI_DSI_MODE_VIDEO_NO_HFP | MIPI_DSI_MODE_VIDEO_NO_HBP |
			  MIPI_DSI_MODE_NO_EOT_PACKET;

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach dsi to host: %d\n", ret);
		return ret;
	}

	return 0;
}

static int lt9211_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct lt9211 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;

	/*
	 * Put the chip in reset, pull nRST line low,
	 * and assure lengthy 10ms reset low timing.
	 */
	ctx->reset_gpio = devm_gpiod_get_optional(ctx->dev, "reset",
						  GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return PTR_ERR(ctx->reset_gpio);

	usleep_range(10000, 11000);	/* Very long reset duration. */

	ret = lt9211_parse_dt(ctx);
	if (ret)
		return ret;

	ctx->regmap = devm_regmap_init_i2c(client, &lt9211_regmap_config);
	if (IS_ERR(ctx->regmap))
		return PTR_ERR(ctx->regmap);

	dev_set_drvdata(dev, ctx);
	i2c_set_clientdata(client, ctx);

	ctx->bridge.funcs = &lt9211_funcs;
	ctx->bridge.of_node = dev->of_node;
	drm_bridge_add(&ctx->bridge);

	ret = lt9211_host_attach(ctx);
	if (ret)
		drm_bridge_remove(&ctx->bridge);

	return ret;
}

static void lt9211_remove(struct i2c_client *client)
{
	struct lt9211 *ctx = i2c_get_clientdata(client);

	drm_bridge_remove(&ctx->bridge);
}

static const struct i2c_device_id lt9211_id[] = {
	{ "lontium,lt9211" },
	{},
};
MODULE_DEVICE_TABLE(i2c, lt9211_id);

static const struct of_device_id lt9211_match_table[] = {
	{ .compatible = "lontium,lt9211" },
	{},
};
MODULE_DEVICE_TABLE(of, lt9211_match_table);

static struct i2c_driver lt9211_driver = {
	.probe = lt9211_probe,
	.remove = lt9211_remove,
	.id_table = lt9211_id,
	.driver = {
		.name = "lt9211",
		.of_match_table = lt9211_match_table,
	},
};
module_i2c_driver(lt9211_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Lontium LT9211 DSI/LVDS/DPI bridge driver");
MODULE_LICENSE("GPL");
