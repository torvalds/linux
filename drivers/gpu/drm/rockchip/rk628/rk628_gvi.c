// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Sandy Huang <hjc@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/mfd/rk628.h>

#include <drm/drm_of.h>
#include <drm/drm_atomic.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include "rk628_combtxphy.h"

#define HOSTREG(x)					((x) + 0x80000)
#define GVI_SYS_CTRL0					HOSTREG(0x0000)
#define GVI_SYS_CTRL1					HOSTREG(0x0004)
#define GVI_SYS_CTRL2					HOSTREG(0x0008)
#define GVI_SYS_CTRL3					HOSTREG(0x000c)
#define GVI_VERSION					HOSTREG(0x0010)
#define GVI_SYS_RST					HOSTREG(0x0014)
#define GVI_LINE_FLAG					HOSTREG(0x0018)
#define GVI_STATUS					HOSTREG(0x001c)
#define GVI_PLL_LOCK_TIMEOUT				HOSTREG(0x0030)
#define GVI_HTPDN_TIMEOUT				HOSTREG(0x0034)
#define GVI_LOCKN_TIMEOUT				HOSTREG(0x0038)
#define GVI_WAIT_LOCKN					HOSTREG(0x003C)
#define GVI_WAIT_HTPDN					HOSTREG(0x0040)
#define GVI_INTR_EN					HOSTREG(0x0050)
#define GVI_INTR_CLR					HOSTREG(0x0054)
#define GVI_INTR_RAW_STATUS				HOSTREG(0x0058)
#define GVI_INTR_STATUS					HOSTREG(0x005c)
#define GVI_COLOR_BAR_CTRL				HOSTREG(0x0060)
#define GVI_COLOR_BAR_HTIMING0				HOSTREG(0x0070)
#define GVI_COLOR_BAR_HTIMING1				HOSTREG(0x0074)
#define GVI_COLOR_BAR_VTIMING0				HOSTREG(0x0078)
#define GVI_COLOR_BAR_VTIMING1				HOSTREG(0x007c)

/* SYS_CTRL0 */
#define SYS_CTRL0_GVI_EN				BIT(0)
#define SYS_CTRL0_AUTO_GATING				BIT(1)
#define SYS_CTRL0_FRM_RST_EN				BIT(2)
#define SYS_CTRL0_FRM_RST_MODE				BIT(3)
#define SYS_CTRL0_LANE_NUM_MASK				GENMASK(7, 4)
#define SYS_CTRL0_LANE_NUM(x)				UPDATE(x, 7, 4)
#define SYS_CTRL0_BYTE_MODE_MASK			GENMASK(9, 8)
#define SYS_CTRL0_BYTE_MODE(x)				UPDATE(x, 9, 8)
#define SYS_CTRL0_SECTION_NUM_MASK			GENMASK(11, 10)
#define SYS_CTRL0_SECTION_NUM(x)			UPDATE(x, 11, 10)
#define SYS_CTRL0_CDR_ENDIAN_SWAP			BIT(12)
#define SYS_CTRL0_PACK_BYTE_SWAP			BIT(13)
#define SYS_CTRL0_PACK_ENDIAN_SWAP			BIT(14)
#define SYS_CTRL0_ENC8B10B_ENDIAN_SWAP			BIT(15)
#define SYS_CTRL0_CDR_EN				BIT(16)
#define SYS_CTRL0_ALN_EN				BIT(17)
#define SYS_CTRL0_NOR_EN				BIT(18)
#define SYS_CTRL0_ALN_NOR_MODE				BIT(19)
#define SYS_CTRL0_GVI_MASK				GENMASK(19, 16)
#define SYS_CTRL0_GVI_GN_EN(x)				UPDATE(x, 19, 16)

#define SYS_CTRL0_SCRAMBLER_EN				BIT(20)
#define SYS_CTRL0_ENCODE8B10B_EN			BIT(21)
#define SYS_CTRL0_INIT_RD_EN				BIT(22)
#define SYS_CTRL0_INIT_RD_VALUE				BIT(23)
#define SYS_CTRL0_FORCE_HTPDN_EN			BIT(24)
#define SYS_CTRL0_FORCE_HTPDN_VALUE			BIT(25)
#define SYS_CTRL0_FORCE_PLL_EN				BIT(26)
#define SYS_CTRL0_FORCE_PLL_VALUE			BIT(27)
#define SYS_CTRL0_FORCE_LOCKN_EN			BIT(28)
#define SYS_CTRL0_FORCE_LOCKN_VALUE			BIT(29)

/* SYS_CTRL1 */
#define SYS_CTRL1_COLOR_DEPTH_MASK			GENMASK(3, 0)
#define SYS_CTRL1_COLOR_DEPTH(x)			UPDATE(x, 3, 0)
#define SYS_CTRL1_DUAL_PIXEL_EN				BIT(4)
#define SYS_CTRL1_TIMING_ALIGN_EN			BIT(8)
#define SYS_CTRL1_LANE_ALIGN_EN				BIT(9)

#define SYS_CTRL1_DUAL_PIXEL_SWAP			BIT(12)
#define SYS_CTRL1_RB_SWAP				BIT(13)
#define SYS_CTRL1_YC_SWAP				BIT(14)
#define SYS_CTRL1_WHOLE_FRM_EN				BIT(16)
#define SYS_CTRL1_NOR_PROTECT				BIT(17)
#define SYS_CTRL1_RD_WCNT_UPDATE			BIT(31)

/* SYS_CTRL2 */
#define SYS_CTRL2_AFIFO_READ_THOLD_MASK			GENMASK(7, 0)
#define SYS_CTRL2_AFIFO_READ_THOLD(x)			UPDATE(x, 7, 0)
#define SYS_CTRL2_AFIFO_ALMOST_FULL_THOLD_MASK		GENMASK(23, 16)
#define SYS_CTRL2_AFIFO_ALMOST_FULL_THOLD(x)		UPDATE(x, 23, 16)
#define SYS_CTRL2_AFIFO_ALMOST_EMPTY_THOLD_MASK		GENMASK(31, 24)
#define SYS_CTRL2_AFIFO_ALMOST_EMPTY_THOLD(x)		UPDATE(x, 31, 24)

/* SYS_CTRL3 */
#define SYS_CTRL3_LANE0_SEL_MASK			GENMASK(2, 0)
#define SYS_CTRL3_LANE0_SEL(x)				UPDATE(x, 2, 0)
#define SYS_CTRL3_LANE1_SEL_MASK			GENMASK(6, 4)
#define SYS_CTRL3_LANE1_SEL(x)				UPDATE(x, 6, 4)
#define SYS_CTRL3_LANE2_SEL_MASK			GENMASK(10, 8)
#define SYS_CTRL3_LANE2_SEL(x)				UPDATE(x, 10, 8)
#define SYS_CTRL3_LANE3_SEL_MASK			GENMASK(14, 12)
#define SYS_CTRL3_LANE3_SEL(x)				UPDATE(x, 14, 12)
#define SYS_CTRL3_LANE4_SEL_MASK			GENMASK(18, 16)
#define SYS_CTRL3_LANE4_SEL(x)				UPDATE(x, 18, 16)
#define SYS_CTRL3_LANE5_SEL_MASK			GENMASK(22, 20)
#define SYS_CTRL3_LANE5_SEL(x)				UPDATE(x, 22, 20)
#define SYS_CTRL3_LANE6_SEL_MASK			GENMASK(26, 24)
#define SYS_CTRL3_LANE6_SEL(x)				UPDATE(x, 26, 24)
#define SYS_CTRL3_LANE7_SEL_MASK			GENMASK(30, 28)
#define SYS_CTRL3_LANE7_SEL(x)				UPDATE(x, 30, 28)
/* VERSIION */
#define VERSION_VERSION(x)				UPDATE(x, 31, 0)
/* SYS_RESET*/
#define SYS_RST_SOFT_RST				BIT(0)
/* LINE_FLAG */
#define LINE_FLAG_LANE_FLAG0_MASK			GENMASK(15, 0)
#define LINE_FLAG_LANE_FLAG0(x)				UPDATE(x, 15, 0)
#define LINE_FLAG_LANE_FLAG1_MASK			GENMASK(31, 16)
#define LINE_FLAG_LANE_FLAG1(x)				UPDATE(x, 31, 16)
/* STATUS */
#define STATUS_HTDPN					BIT(4)
#define STATUS_LOCKN					BIT(5)
#define STATUS_PLL_LOCKN				BIT(6)
#define STATUS_AFIFO0_WCNT_MASK				GENMASK(23, 16)
#define STATUS_AFIFO0_WCNT(x)				UPDATE(x, 23, 16)
#define STATUS_AFIFO1_WCNT_MASK				GENMASK(31, 24)
#define STATUS_AFIFO1_WCNT(x)				UPDATE(x, 31, 24)
/* PLL_LTIMEOUT */
#define PLL_LOCK_TIMEOUT_PLL_LOCK_TIME_OUT_MASK		GENMASK(31, 0)
#define PLL_LOCK_TIMEOUT_PLL_LOCK_TIME_OUT(x)		UPDATE(x, 31, 0)
/* HTPDNEOUT */
#define HTPDN_TIMEOUT_HTPDN_TIME_OUT_MASK		GENMASK(31, 0)
#define HTPDN_TIMEOUT_HTPDN_TIME_OUT(x)			UPDATE(x, 31, 0)
/* LOCKNEOUT */
#define LOCKN_TIMEOUT_LOCKN_TIME_OUT_MASK		GENMASK(31, 0)
#define LOCKN_TIMEOUT_LOCKN_TIME_OUT(x)			UPDATE(x, 31, 0)
/* WAIT_LOCKN */
#define WAIT_LOCKN_WAIT_LOCKN_TIME_MASK			GENMASK(30, 0)
#define WAIT_LOCKN_WAIT_LOCKN_TIME(x)			UPDATE(x, 30, 0)
#define WAIT_LOCKN_WAIT_LOCKN_TIME_EN			BIT(31)
/* WAIT_HTPDN */
#define WAIT_HTPDN_WAIT_HTPDN_TIME_MASK			GENMASK(30, 0)
#define WAIT_HTPDN_WAIT_HTPDN_TIME(x)			UPDATE(x, 30, 0)
#define WAIT_HTPDN_WAIT_HTPDN_EN			BIT(31)
/* INTR_EN */
#define INTR_EN_INTR_FRM_ST_EN				BIT(0)
#define INTR_EN_INTR_PLL_LOCK_EN			BIT(1)
#define INTR_EN_INTR_HTPDN_EN				BIT(2)
#define INTR_EN_INTR_LOCKN_EN				BIT(3)
#define INTR_EN_INTR_PLL_TIMEOUT_EN			BIT(4)
#define INTR_EN_INTR_HTPDN_TIMEOUT_EN			BIT(5)
#define INTR_EN_INTR_LOCKN_TIMEOUT_EN			BIT(6)
#define INTR_EN_INTR_LINE_FLAG0_EN			BIT(8)
#define INTR_EN_INTR_LINE_FLAG1_EN			BIT(9)
#define INTR_EN_INTR_AFIFO_OVERFLOW_EN			BIT(10)
#define INTR_EN_INTR_AFIFO_UNDERFLOW_EN			BIT(11)
#define INTR_EN_INTR_PLL_ERR_EN				BIT(12)
#define INTR_EN_INTR_HTPDN_ERR_EN			BIT(13)
#define INTR_EN_INTR_LOCKN_ERR_EN			BIT(14)
/* INTR_CLR*/
#define INTR_CLR_INTR_FRM_ST_CLR			BIT(0)
#define INTR_CLR_INTR_PLL_LOCK_CLR			BIT(1)
#define INTR_CLR_INTR_HTPDN_CLR				BIT(2)
#define INTR_CLR_INTR_LOCKN_CLR				BIT(3)
#define INTR_CLR_INTR_PLL_TIMEOUT_CLR			BIT(4)
#define INTR_CLR_INTR_HTPDN_TIMEOUT_CLR			BIT(5)
#define INTR_CLR_INTR_LOCKN_TIMEOUT_CLR			BIT(6)
#define INTR_CLR_INTR_LINE_FLAG0_CLR			BIT(8)
#define INTR_CLR_INTR_LINE_FLAG1_CLR			BIT(9)
#define INTR_CLR_INTR_AFIFO_OVERFLOW_CLR		BIT(10)
#define INTR_CLR_INTR_AFIFO_UNDERFLOW_CLR		BIT(11)
#define INTR_CLR_INTR_PLL_ERR_CLR			BIT(12)
#define INTR_CLR_INTR_HTPDN_ERR_CLR			BIT(13)
#define INTR_CLR_INTR_LOCKN_ERR_CLR			BIT(14)
/* INTR_RAW_STATUS */
#define INTR_RAW_STATUS_RAW_INTR_FRM_ST			BIT(0)
#define INTR_RAW_STATUS_RAW_INTR_PLL_LOCK		BIT(1)
#define INTR_RAW_STATUS_RAW_INTR_HTPDN			BIT(2)
#define INTR_RAW_STATUS_RAW_INTR_LOCKN			BIT(3)
#define INTR_RAW_STATUS_RAW_INTR_PLL_TIMEOUT		BIT(4)
#define INTR_RAW_STATUS_RAW_INTR_HTPDN_TIMEOUT		BIT(5)
#define INTR_RAW_STATUS_RAW_INTR_LOCKN_TIMEOUT		BIT(6)
#define INTR_RAW_STATUS_RAW_INTR_LINE_FLAG0		BIT(8)
#define INTR_RAW_STATUS_RAW_INTR_LINE_FLAG1		BIT(9)
#define INTR_RAW_STATUS_RAW_INTR_AFIFO_OVERFLOW		BIT(10)
#define INTR_RAW_STATUS_RAW_INTR_AFIFO_UNDERFLOW	BIT(11)
#define INTR_RAW_STATUS_RAW_INTR_PLL_ERR		BIT(12)
#define INTR_RAW_STATUS_RAW_INTR_HTPDN_ERR		BIT(13)
#define INTR_RAW_STATUS_RAW_INTR_LOCKN_ERR		BIT(14)
/* INTR_STATUS */
#define INTR_STATUS_INTR_FRM_ST				BIT(0)
#define INTR_STATUS_INTR_PLL_LOCK			BIT(1)
#define INTR_STATUS_INTR_HTPDN				BIT(2)
#define INTR_STATUS_INTR_LOCKN				BIT(3)
#define INTR_STATUS_INTR_PLL_TIMEOUT			BIT(4)
#define INTR_STATUS_INTR_HTPDN_TIMEOUT			BIT(5)
#define INTR_STATUS_INTR_LOCKN_TIMEOUT			BIT(6)
#define INTR_STATUS_INTR_LINE_FLAG0			BIT(8)
#define INTR_STATUS_INTR_LINE_FLAG1			BIT(9)
#define INTR_STATUS_INTR_AFIFO_OVERFLOW			BIT(10)
#define INTR_STATUS_INTR_AFIFO_UNDERFLOW		BIT(11)
#define INTR_STATUS_INTR_PLL_ERR			BIT(12)
#define INTR_STATUS_INTR_HTPDN_ERR			BIT(13)
#define INTR_STATUS_INTR_LOCKN_ERR			BIT(14)

/* COLOR_BAR_CTRL */
#define COLOR_BAR_EN					BIT(0)

#define COLOR_DEPTH_RGB_YUV444_18BIT			0
#define COLOR_DEPTH_RGB_YUV444_24BIT			1
#define COLOR_DEPTH_RGB_YUV444_30BIT			2
#define COLOR_DEPTH_YUV422_16BIT			8
#define COLOR_DEPTH_YUV422_20BIT			9

enum gvi_byte_mode {
	GVI_3BYTE_MODE = 0,
	GVI_4BYTE_MODE,
	GVI_5BYTE_MODE,
};

struct rk628_gvi {
	struct drm_bridge base;
	struct drm_connector connector;
	struct drm_panel *panel;
	struct drm_display_mode mode;
	struct device *dev;
	struct regmap *grf;
	struct regmap *regmap;
	struct clk *pclk;
	struct reset_control *rst;
	struct phy *phy;
	struct rk628 *parent;
	u32 lane_mbps;
	u32 bus_format;
	u32 lane_num;
	u8 color_depth;
	u8 byte_mode;
	bool division_mode;
};

static inline struct rk628_gvi *bridge_to_gvi(struct drm_bridge *b)
{
	return container_of(b, struct rk628_gvi, base);
}

static inline struct rk628_gvi *connector_to_gvi(struct drm_connector *c)
{
	return container_of(c, struct rk628_gvi, connector);
}

static struct drm_encoder *rk628_gvi_connector_best_encoder(struct drm_connector
							    *connector)
{
	struct rk628_gvi *gvi = connector_to_gvi(connector);

	return gvi->base.encoder;
}

static int rk628_gvi_connector_get_modes(struct drm_connector *connector)
{
	struct rk628_gvi *gvi = connector_to_gvi(connector);
	struct drm_display_info *info = &connector->display_info;
	int num_modes;

	num_modes = drm_panel_get_modes(gvi->panel, connector);

	if (info->num_bus_formats)
		gvi->bus_format = info->bus_formats[0];
	else
		gvi->bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	switch (gvi->bus_format) {
	case MEDIA_BUS_FMT_RGB666_1X18:
		gvi->byte_mode = 3;
		gvi->color_depth = COLOR_DEPTH_RGB_YUV444_18BIT;
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
		gvi->byte_mode = 4;
		gvi->color_depth = COLOR_DEPTH_RGB_YUV444_24BIT;
		break;
	case MEDIA_BUS_FMT_RGB101010_1X30:
		gvi->byte_mode = 4;
		gvi->color_depth = COLOR_DEPTH_RGB_YUV444_30BIT;
		break;
	case MEDIA_BUS_FMT_YUYV8_1X16:
		gvi->byte_mode = 3;
		gvi->color_depth = COLOR_DEPTH_YUV422_16BIT;
		break;
	case MEDIA_BUS_FMT_YUYV10_1X20:
		gvi->byte_mode = 3;
		gvi->color_depth = COLOR_DEPTH_YUV422_20BIT;
		break;
	default:
		gvi->byte_mode = 3;
		gvi->color_depth = COLOR_DEPTH_RGB_YUV444_24BIT;
		dev_info(gvi->dev, "unsupported bus_format: 0x%x\n",
			 gvi->bus_format);
		break;
	}

	info->edid_hdmi_dc_modes = 0;
	info->hdmi.y420_dc_modes = 0;
	info->color_formats = 0;
	info->max_tmds_clock = 300000;
	connector->ycbcr_420_allowed = true;

	num_modes += rk628_scaler_add_src_mode(gvi->parent, connector);

	return num_modes;
}

static const
struct drm_connector_helper_funcs rk628_gvi_connector_helper_funcs = {
	.get_modes = rk628_gvi_connector_get_modes,
	.best_encoder = rk628_gvi_connector_best_encoder,
};

static enum drm_connector_status
rk628_gvi_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void rk628_gvi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs rk628_gvi_connector_funcs = {
	.detect = rk628_gvi_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rk628_gvi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static unsigned int rk628_gvi_get_lane_rate(struct rk628_gvi *gvi)
{
	struct device *dev = gvi->dev;
	const struct drm_display_mode *mode = &gvi->mode;
	u32 lane_bit_rate, min_lane_rate = 500000, max_lane_rate = 4000000;
	u64 total_bw;

	/* optional override of the desired bandwidth */
	if (!of_property_read_u32
	    (dev->of_node, "rockchip,lane-rate", &lane_bit_rate))
		return lane_bit_rate;

	/**
	 * [ENCODER TOTAL BIT-RATE](bps) = [byte mode](byte) x 10 / [pixel clock](HZ)
	 *
	 * lane_bit_rate = [total bit-rate](bps) / [lane number]
	 *
	 * 500Mbps <= lane_bit_rate <= 4Gbps
	 */
	total_bw = (unsigned long long)gvi->byte_mode * 10 * mode->clock;	/* Kbps */
	do_div(total_bw, gvi->lane_num);
	lane_bit_rate = total_bw;

	if (lane_bit_rate < min_lane_rate)
		lane_bit_rate = min_lane_rate;
	if (lane_bit_rate > max_lane_rate)
		lane_bit_rate = max_lane_rate;

	return lane_bit_rate;
}

static void rk628_gvi_enable_color_bar(struct rk628_gvi *gvi)
{
	const struct drm_display_mode *mode = &gvi->mode;
	struct videomode vm;
	u16 hsync_len, hact_st, hact_end, htotal;
	u16 vsync_len, vact_st, vact_end, vtotal;

	drm_display_mode_to_videomode(mode, &vm);

	if (gvi->division_mode) {
		hsync_len = vm.hsync_len / 2;
		hact_st = (vm.hsync_len + vm.hback_porch) / 2;
		hact_end = (vm.hsync_len + vm.hback_porch + vm.hactive) / 2;
		htotal = mode->htotal / 2;
	} else {
		hsync_len = vm.hsync_len;
		hact_st = vm.hsync_len + vm.hback_porch;
		hact_end = vm.hsync_len + vm.hback_porch + vm.hactive;
		htotal = mode->htotal;
	}
	vsync_len = vm.vsync_len;
	vact_st = vsync_len + vm.vback_porch;
	vact_end = vact_st + vm.vactive;
	vtotal = mode->vtotal;

	regmap_write(gvi->regmap, GVI_COLOR_BAR_HTIMING0,
		     hact_st << 16 | hsync_len);
	regmap_write(gvi->regmap, GVI_COLOR_BAR_HTIMING1,
		     (htotal - 1) << 16 | hact_end);
	regmap_write(gvi->regmap, GVI_COLOR_BAR_VTIMING0,
		     vact_st << 16 | vsync_len);
	regmap_write(gvi->regmap, GVI_COLOR_BAR_VTIMING1,
		     (vtotal - 1) << 16 | vact_end);
	regmap_write_bits(gvi->regmap, GVI_COLOR_BAR_CTRL, COLOR_BAR_EN, 0);
}

static void rk628_gvi_pre_enable(struct rk628_gvi *gvi)
{
	clk_prepare_enable(gvi->pclk);

	/* gvi reset */
	regmap_write_bits(gvi->regmap, GVI_SYS_RST, SYS_RST_SOFT_RST,
			  SYS_RST_SOFT_RST);
	udelay(10);
	regmap_write_bits(gvi->regmap, GVI_SYS_RST, SYS_RST_SOFT_RST, 0);
	udelay(10);

	regmap_write_bits(gvi->regmap, GVI_SYS_CTRL0, SYS_CTRL0_LANE_NUM_MASK,
			  SYS_CTRL0_LANE_NUM(gvi->lane_num - 1));
	regmap_write_bits(gvi->regmap, GVI_SYS_CTRL0, SYS_CTRL0_BYTE_MODE_MASK,
			  SYS_CTRL0_BYTE_MODE(gvi->byte_mode ==
					      3 ? 0 : (gvi->byte_mode ==
						       4 ? 1 : 2)));
	regmap_write_bits(gvi->regmap, GVI_SYS_CTRL0,
			  SYS_CTRL0_SECTION_NUM_MASK,
			  SYS_CTRL0_SECTION_NUM(gvi->division_mode));
	regmap_update_bits(gvi->grf, GRF_POST_PROC_CON, SW_SPLIT_EN,
			   gvi->division_mode ? SW_SPLIT_EN : 0);
	regmap_write_bits(gvi->regmap, GVI_SYS_CTRL1, SYS_CTRL1_DUAL_PIXEL_EN,
			  gvi->division_mode ? SYS_CTRL1_DUAL_PIXEL_EN : 0);

	regmap_write_bits(gvi->regmap, GVI_SYS_CTRL0, SYS_CTRL0_FRM_RST_EN, 0);
	regmap_write_bits(gvi->regmap, GVI_SYS_CTRL1, SYS_CTRL1_LANE_ALIGN_EN, 0);
}

static void rk628_gvi_post_enable(struct rk628_gvi *gvi)
{
	u32 val;

	val = SYS_CTRL0_GVI_EN | SYS_CTRL0_AUTO_GATING;
	regmap_write_bits(gvi->regmap, GVI_SYS_CTRL0, val, 3);
}

static void rk628_gvi_bridge_enable(struct drm_bridge *bridge)
{
	struct rk628_gvi *gvi = bridge_to_gvi(bridge);
	unsigned int rate = rk628_gvi_get_lane_rate(gvi);
	int ret;

	regmap_update_bits(gvi->grf, GRF_SYSTEM_CON0, SW_OUTPUT_MODE_MASK,
			   SW_OUTPUT_MODE(OUTPUT_MODE_GVI));
	phy_set_bus_width(gvi->phy, rate);
	rk628_combtxphy_set_gvi_division_mode(gvi->phy, gvi->division_mode);
	ret = phy_set_mode(gvi->phy, 0);
	if (ret) {
		dev_err(gvi->dev, "failed to set phy mode: %d\n", ret);
		return;
	}
	phy_power_on(gvi->phy);
	gvi->lane_mbps = phy_get_bus_width(gvi->phy);
	rk628_gvi_pre_enable(gvi);
	drm_panel_prepare(gvi->panel);
	rk628_gvi_enable_color_bar(gvi);
	rk628_gvi_post_enable(gvi);
	drm_panel_enable(gvi->panel);

	dev_info(gvi->dev,
		 "GVI-Link bandwidth: %d x %d Mbps, Byte mode: %d, Color Depty: %d, %s division mode\n",
		 gvi->lane_mbps, gvi->lane_num, gvi->byte_mode,
		 gvi->color_depth, gvi->division_mode ? "two" : "one");
}

static void rk628_gvi_post_disable(struct drm_bridge *bridge)
{
	struct rk628_gvi *gvi = bridge_to_gvi(bridge);

	regmap_write_bits(gvi->regmap, GVI_SYS_CTRL0, SYS_CTRL0_GVI_EN, 0);
}

static void rk628_gvi_bridge_disable(struct drm_bridge *bridge)
{
	struct rk628_gvi *gvi = bridge_to_gvi(bridge);

	drm_panel_disable(gvi->panel);
	drm_panel_unprepare(gvi->panel);
	rk628_gvi_post_disable(bridge);
	clk_disable_unprepare(gvi->pclk);
	phy_power_off(gvi->phy);
}

static int rk628_gvi_bridge_attach(struct drm_bridge *bridge,
				   enum drm_bridge_attach_flags flags)
{
	struct rk628_gvi *gvi = bridge_to_gvi(bridge);
	struct drm_connector *connector = &gvi->connector;
	struct drm_device *drm = bridge->dev;
	int ret;

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		return 0;

	ret = drm_connector_init(drm, connector, &rk628_gvi_connector_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret) {
		dev_err(gvi->dev, "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &rk628_gvi_connector_helper_funcs);
	drm_connector_attach_encoder(connector, bridge->encoder);

	return 0;
}

static void rk628_gvi_bridge_mode_set(struct drm_bridge *bridge,
				      const struct drm_display_mode *mode,
				      const struct drm_display_mode *adj)
{
	struct rk628_gvi *gvi = bridge_to_gvi(bridge);

	rk628_mode_copy(gvi->parent, &gvi->mode, mode);

	dev_info(gvi->dev, "src mode: %dx%d, clk: %d, dst mode: %dx%d, clk: %d\n",
		 mode->hdisplay, mode->vdisplay, mode->clock,
		 gvi->mode.hdisplay, gvi->mode.vdisplay, gvi->mode.clock);
}

static const struct drm_bridge_funcs rk628_gvi_bridge_funcs = {
	.attach = rk628_gvi_bridge_attach,
	.enable = rk628_gvi_bridge_enable,
	.disable = rk628_gvi_bridge_disable,
	.mode_set = rk628_gvi_bridge_mode_set,
};

static const struct regmap_range rk628_gvi_readable_ranges[] = {
	regmap_reg_range(GVI_SYS_CTRL0, GVI_COLOR_BAR_VTIMING1),
};

static const struct regmap_access_table rk628_gvi_readable_table = {
	.yes_ranges = rk628_gvi_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(rk628_gvi_readable_ranges),
};

static const struct regmap_config rk628_gvi_regmap_cfg = {
	.name = "gvi",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = GVI_COLOR_BAR_VTIMING1,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.rd_table = &rk628_gvi_readable_table,
};

static int rk628_gvi_probe(struct platform_device *pdev)
{
	struct rk628 *rk628 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk628_gvi *gvi;
	int ret = 0;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	gvi = devm_kzalloc(dev, sizeof(*gvi), GFP_KERNEL);
	if (!gvi)
		return -ENOMEM;

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1,
					  &gvi->panel, NULL);
	if (ret)
		return ret;

	gvi->dev = dev;
	gvi->parent = rk628;
	gvi->division_mode = of_property_read_bool(dev->of_node,
						   "rockchip,division-mode");
	ret = of_property_read_u32(dev->of_node, "rockchip,lane-num",
				   &gvi->lane_num);
	if (ret) {
		dev_err(gvi->dev, "Failed to get lane num\n");
		gvi->lane_num = 4;
	}

	platform_set_drvdata(pdev, gvi);
	gvi->grf = rk628->grf;
	if (!gvi->grf)
		return -ENODEV;

	gvi->regmap = devm_regmap_init_i2c(rk628->client,
					   &rk628_gvi_regmap_cfg);
	if (IS_ERR(gvi->regmap)) {
		ret = PTR_ERR(gvi->regmap);
		dev_err(dev, "failed to allocate register map: %d\n", ret);
		return ret;
	}

	gvi->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(gvi->pclk)) {
		ret = PTR_ERR(gvi->pclk);
		dev_err(dev, "failed to get pclk: %d\n", ret);
		return ret;
	}

	gvi->rst = of_reset_control_get(dev->of_node, NULL);
	if (IS_ERR(gvi->rst)) {
		ret = PTR_ERR(gvi->rst);
		dev_err(dev, "failed to get reset control: %d\n", ret);
		return ret;
	}

	gvi->phy = devm_of_phy_get(dev, dev->of_node, NULL);
	if (IS_ERR(gvi->phy)) {
		ret = PTR_ERR(gvi->phy);
		dev_err(dev, "failed to get phy: %d\n", ret);
		return ret;
	}

	gvi->base.funcs = &rk628_gvi_bridge_funcs;
	gvi->base.of_node = dev->of_node;
	drm_bridge_add(&gvi->base);

	return 0;
}

static int rk628_gvi_remove(struct platform_device *pdev)
{
	struct rk628_gvi *gvi = platform_get_drvdata(pdev);

	drm_bridge_remove(&gvi->base);

	return 0;
}

static const struct of_device_id rk628_gvi_of_match[] = {
	{.compatible = "rockchip,rk628-gvi",},
	{},
};

MODULE_DEVICE_TABLE(of, rk628_gvi_of_match);

static struct platform_driver rk628_gvi_driver = {
	.driver = {
		.name = "rk628-gvi",
		.of_match_table = of_match_ptr(rk628_gvi_of_match),
	},
	.probe = rk628_gvi_probe,
	.remove = rk628_gvi_remove,
};

module_platform_driver(rk628_gvi_driver);

MODULE_AUTHOR("Sandy Huang <hjc@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK628 GVI driver");
MODULE_LICENSE("GPL v2");
