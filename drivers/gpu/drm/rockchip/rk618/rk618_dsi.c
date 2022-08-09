// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/mfd/rk618.h>

#include <drm/drm_drv.h>
#include <drm/drm_of.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include <video/of_display_timing.h>
#include <video/mipi_display.h>
#include <video/videomode.h>
#include <asm/unaligned.h>

#include "../rockchip_drm_drv.h"

#define HOSTREG(x)		((x) + 0x1000)
#define DSI_VERSION		HOSTREG(0x0000)
#define DSI_PWR_UP		HOSTREG(0x0004)
#define SHUTDOWNZ		BIT(0)
#define POWER_UP		BIT(0)
#define RESET			0
#define DSI_CLKMGR_CFG		HOSTREG(0x0008)
#define TO_CLK_DIVIDSION(x)	UPDATE(x, 15, 8)
#define TX_ESC_CLK_DIVIDSION(x)	UPDATE(x, 7, 0)
#define DSI_DPI_CFG		HOSTREG(0x000c)
#define EN18_LOOSELY		BIT(10)
#define COLORM_ACTIVE_LOW	BIT(9)
#define SHUTD_ACTIVE_LOW	BIT(8)
#define HSYNC_ACTIVE_LOW	BIT(7)
#define VSYNC_ACTIVE_LOW	BIT(6)
#define DATAEN_ACTIVE_LOW	BIT(5)
#define DPI_COLOR_CODING(x)	UPDATE(x, 4, 2)
#define DPI_VID(x)		UPDATE(x, 1, 0)
#define DSI_PCKHDL_CFG		HOSTREG(0x0018)
#define GEN_VID_RX(x)		UPDATE(x, 6, 5)
#define EN_CRC_RX		BIT(4)
#define EN_ECC_RX		BIT(3)
#define EN_BTA			BIT(2)
#define EN_EOTP_RX		BIT(1)
#define EN_EOTP_TX		BIT(0)
#define DSI_VID_MODE_CFG	HOSTREG(0x001c)
#define LPCMDEN			BIT(12)
#define FRAME_BTA_ACK		BIT(11)
#define EN_NULL_PKT		BIT(10)
#define EN_MULTI_PKT		BIT(9)
#define EN_LP_HFP		BIT(8)
#define EN_LP_HBP		BIT(7)
#define EN_LP_VACT		BIT(6)
#define EN_LP_VFP		BIT(5)
#define EN_LP_VBP		BIT(4)
#define EN_LP_VSA		BIT(3)
#define VID_MODE_TYPE(x)	UPDATE(x, 2, 1)
#define EN_VIDEO_MODE		BIT(0)
#define DSI_VID_PKT_CFG		HOSTREG(0x0020)
#define NULL_PKT_SIZE(x)	UPDATE(x, 30, 21)
#define NUM_CHUNKS(x)		UPDATE(x, 20, 11)
#define VID_PKT_SIZE(x)		UPDATE(x, 10, 0)
#define DSI_CMD_MODE_CFG	HOSTREG(0x0024)
#define TEAR_FX_EN		BIT(14)
#define ACK_RQST_EN		BIT(13)
#define DCS_LW_TX		BIT(12)
#define GEN_LW_TX		BIT(11)
#define MAX_RD_PKT_SIZE		BIT(10)
#define DCS_SR_0P_TX		BIT(9)
#define DCS_SW_1P_TX		BIT(8)
#define DCS_SW_0P_TX		BIT(7)
#define GEN_SR_2P_TX		BIT(6)
#define GEN_SR_1P_TX		BIT(5)
#define GEN_SR_0P_TX		BIT(4)
#define GEN_SW_2P_TX		BIT(3)
#define GEN_SW_1P_TX		BIT(2)
#define GEN_SW_0P_TX		BIT(1)
#define EN_CMD_MODE		BIT(0)
#define DSI_TMR_LINE_CFG	HOSTREG(0x0028)
#define HLINE_TIME(x)		UPDATE(x, 31, 18)
#define HBP_TIME(x)		UPDATE(x, 17, 9)
#define HSA_TIME(x)		UPDATE(x, 8, 0)
#define DSI_VTIMING_CFG		HOSTREG(0x002c)
#define V_ACTIVE_LINES(x)	UPDATE(x, 26, 16)
#define VFP_LINES(x)		UPDATE(x, 15, 10)
#define VBP_LINES(x)		UPDATE(x, 9, 4)
#define VSA_LINES(x)		UPDATE(x, 3, 0)
#define DSI_PHY_TMR_CFG		HOSTREG(0x0030)
#define PHY_HS2LP_TIME(x)	UPDATE(x, 31, 24)
#define PHY_LP2HS_TIME(x)	UPDATE(x, 23, 16)
#define MAX_RD_TIME(x)		UPDATE(x, 14, 0)
#define DSI_GEN_HDR		HOSTREG(0x0034)
#define DSI_GEN_PLD_DATA	HOSTREG(0x0038)
#define DSI_GEN_PKT_STATUS	HOSTREG(0x003c)
#define GEN_RD_CMD_BUSY		BIT(6)
#define GEN_PLD_R_FULL		BIT(5)
#define GEN_PLD_R_EMPTY		BIT(4)
#define GEN_PLD_W_FULL		BIT(3)
#define GEN_PLD_W_EMPTY		BIT(2)
#define GEN_CMD_FULL		BIT(1)
#define GEN_CMD_EMPTY		BIT(0)
#define DSI_TO_CNT_CFG		HOSTREG(0x0040)
#define LPRX_TO_CNT(x)		UPDATE(x, 31, 16)
#define HSTX_TO_CNT(x)		UPDATE(x, 15, 0)
#define DSI_INT_ST0		HOSTREG(0x0044)
#define DSI_INT_ST1		HOSTREG(0x0048)
#define DSI_INT_MSK0		HOSTREG(0x004c)
#define DSI_INT_MSK1		HOSTREG(0x0050)
#define DSI_PHY_RSTZ		HOSTREG(0x0054)
#define PHY_ENABLECLK		BIT(2)
#define DSI_PHY_IF_CFG		HOSTREG(0x0058)
#define PHY_STOP_WAIT_TIME(x)	UPDATE(x, 9, 2)
#define N_LANES(x)		UPDATE(x, 1, 0)
#define DSI_PHY_IF_CTRL		HOSTREG(0x005c)
#define PHY_TX_TRIGGERS(x)	UPDATE(x, 8, 5)
#define PHY_TXEXITULPSLAN	BIT(4)
#define PHY_TXREQULPSLAN	BIT(3)
#define PHY_TXEXITULPSCLK	BIT(2)
#define PHY_RXREQULPSCLK	BIT(1)
#define PHY_TXREQUESCLKHS	BIT(0)
#define DSI_PHY_STATUS		HOSTREG(0x0060)
#define ULPSACTIVENOT3LANE	BIT(12)
#define PHYSTOPSTATE3LANE	BIT(11)
#define ULPSACTIVENOT2LANE	BIT(10)
#define PHYSTOPSTATE2LANE	BIT(9)
#define ULPSACTIVENOT1LANE	BIT(8)
#define PHYSTOPSTATE1LANE	BIT(7)
#define RXULPSESC0LANE		BIT(6)
#define ULPSACTIVENOT0LANE	BIT(5)
#define PHYSTOPSTATE0LANE	BIT(4)
#define PHYULPSACTIVENOTCLK	BIT(3)
#define PHYSTOPSTATECLKLANE	BIT(2)
#define PHYSTOPSTATELANE	(PHYSTOPSTATE0LANE | PHYSTOPSTATECLKLANE)
#define PHYDIRECTION		BIT(1)
#define PHYLOCK			BIT(0)
#define DSI_LP_CMD_TIM		HOSTREG(0x0070)
#define OUTVACT_LPCMD_TIME(x)	UPDATE(x, 15, 8)
#define INVACT_LPCMD_TIME(x)	UPDATE(x, 7, 0)
#define DSI_MAX_REGISTER	DSI_LP_CMD_TIM

#define PHYREG(x)		((x) + 0x0c00)
#define MIPI_PHY_REG0		PHYREG(0x0000)
#define LANE_EN_MASK		GENMASK(6, 2)
#define LANE_EN_CK		BIT(6)
#define MIPI_PHY_REG1		PHYREG(0x0004)
#define REG_DA_PPFC		BIT(4)
#define REG_DA_SYNCRST		BIT(2)
#define REG_DA_LDOPD		BIT(1)
#define REG_DA_PLLPD		BIT(0)
#define MIPI_PHY_REG3		PHYREG(0x000c)
#define REG_FBDIV_HI_MASK	GENMASK(5, 5)
#define REG_FBDIV_HI(x)		UPDATE(x, 5, 5)
#define REG_PREDIV_MASK		GENMASK(4, 0)
#define REG_PREDIV(x)		UPDATE(x, 4, 0)
#define MIPI_PHY_REG4		PHYREG(0x0010)
#define REG_FBDIV_LO_MASK	GENMASK(7, 0)
#define REG_FBDIV_LO(x)		UPDATE(x, 7, 0)
#define MIPI_PHY_REG5		PHYREG(0x0014)
#define MIPI_PHY_REG6		PHYREG(0x0018)
#define MIPI_PHY_REG7		PHYREG(0x001c)
#define MIPI_PHY_REG9		PHYREG(0x0024)
#define MIPI_PHY_REG20		PHYREG(0x0080)
#define REG_DIG_RSTN		BIT(0)
#define MIPI_PHY_MAX_REGISTER	PHYREG(0x0348)

#define THS_SETTLE_OFFSET	0x00
#define THS_SETTLE_MASK		GENMASK(3, 0)
#define THS_SETTLE(x)		UPDATE(x, 3, 0)
#define TLPX_OFFSET		0x14
#define TLPX_MASK		GENMASK(5, 0)
#define TLPX(x)			UPDATE(x, 5, 0)
#define THS_PREPARE_OFFSET	0x18
#define THS_PREPARE_MASK	GENMASK(6, 0)
#define THS_PREPARE(x)		UPDATE(x, 6, 0)
#define THS_ZERO_OFFSET		0x1c
#define THS_ZERO_MASK		GENMASK(5, 0)
#define THS_ZERO(x)		UPDATE(x, 5, 0)
#define THS_TRAIL_OFFSET	0x20
#define THS_TRAIL_MASK		GENMASK(6, 0)
#define THS_TRAIL(x)		UPDATE(x, 6, 0)
#define THS_EXIT_OFFSET		0x24
#define THS_EXIT_MASK		GENMASK(4, 0)
#define THS_EXIT(x)		UPDATE(x, 4, 0)
#define TCLK_POST_OFFSET	0x28
#define TCLK_POST_MASK		GENMASK(3, 0)
#define TCLK_POST(x)		UPDATE(x, 3, 0)
#define TWAKUP_HI_OFFSET	0x30
#define TWAKUP_HI_MASK		GENMASK(1, 0)
#define TWAKUP_HI(x)		UPDATE(x, 1, 0)
#define TWAKUP_LO_OFFSET	0x34
#define TWAKUP_LO_MASK		GENMASK(7, 0)
#define TWAKUP_LO(x)		UPDATE(x, 7, 0)
#define TCLK_PRE_OFFSET		0x38
#define TCLK_PRE_MASK		GENMASK(3, 0)
#define TCLK_PRE(x)		UPDATE(x, 3, 0)
#define TTA_GO_OFFSET		0x40
#define TTA_GO_MASK		GENMASK(5, 0)
#define TTA_GO(x)		UPDATE(x, 5, 0)
#define TTA_SURE_OFFSET		0x44
#define TTA_SURE_MASK		GENMASK(5, 0)
#define TTA_SURE(x)		UPDATE(x, 5, 0)
#define TTA_WAIT_OFFSET		0x48
#define TTA_WAIT_MASK		GENMASK(5, 0)
#define TTA_WAIT(x)		UPDATE(x, 5, 0)

#define PSEC_PER_NSEC	1000L
#define PSEC_PER_SEC	1000000000000LL

struct mipi_dphy {
	struct regmap *regmap;
	u8 prediv;
	u16 fbdiv;
	unsigned int rate;
};

struct rk618_dsi {
	struct drm_bridge base;
	struct drm_connector connector;
	struct drm_display_mode mode;
	struct drm_panel *panel;
	struct mipi_dsi_host host;
	struct mipi_dphy phy;
	unsigned int channel;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	unsigned long mode_flags;

	struct device *dev;
	struct rk618 *parent;
	struct regmap *regmap;
	struct clk *clock;
	struct rockchip_drm_sub_dev sub_dev;
};

enum {
	NON_BURST_MODE_SYNC_PULSE,
	NON_BURST_MODE_SYNC_EVENT,
	BURST_MODE,
};

enum {
	PIXEL_COLOR_CODING_16BIT_1,
	PIXEL_COLOR_CODING_16BIT_2,
	PIXEL_COLOR_CODING_16BIT_3,
	PIXEL_COLOR_CODING_18BIT_1,
	PIXEL_COLOR_CODING_18BIT_2,
	PIXEL_COLOR_CODING_24BIT,
};

static inline struct rk618_dsi *bridge_to_dsi(struct drm_bridge *b)
{
	return container_of(b, struct rk618_dsi, base);
}

static inline struct rk618_dsi *connector_to_dsi(struct drm_connector *c)
{
	return container_of(c, struct rk618_dsi, connector);
}

static inline struct rk618_dsi *host_to_dsi(struct mipi_dsi_host *h)
{
	return container_of(h, struct rk618_dsi, host);
}

static inline bool is_clk_lane(u32 offset)
{
	if (offset == 0x100)
		return true;

	return false;
}

static void rk618_dsi_set_hs_clk(struct rk618_dsi *dsi)
{
	const struct drm_display_mode *mode = &dsi->mode;
	struct mipi_dphy *phy = &dsi->phy;
	struct device *dev = dsi->dev;
	u32 fout, fref, prediv, fbdiv;
	u32 min_delta = UINT_MAX;
	unsigned int value;

	if (!of_property_read_u32(dev->of_node, "rockchip,lane-rate", &value)) {
		fout = value * USEC_PER_SEC;
	} else {
		int bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
		unsigned int lanes = dsi->lanes;
		u64 bandwidth;

		bandwidth = (u64)mode->clock * 1000 * bpp;
		do_div(bandwidth, lanes);
		bandwidth = div_u64(bandwidth * 10, 9);
		bandwidth = div_u64(bandwidth, USEC_PER_SEC);
		bandwidth = bandwidth * USEC_PER_SEC;
		fout = bandwidth;
	}

	if (fout > 1000000000UL)
		fout = 1000000000UL;

	fref = clk_get_rate(dsi->parent->clkin);

	for (prediv = 1; prediv <= 12; prediv++) {
		u64 tmp;
		u32 delta;

		if (fref % prediv)
			continue;

		tmp = (u64)fout * prediv;
		do_div(tmp, fref);
		fbdiv = tmp;

		if (fbdiv < 12 || fbdiv > 511)
			continue;

		if (fbdiv == 15)
			continue;

		tmp = (u64)fbdiv * fref;
		do_div(tmp, prediv);

		delta = abs(fout - tmp);
		if (!delta) {
			phy->rate = tmp;
			phy->prediv = prediv;
			phy->fbdiv = fbdiv;
			break;
		} else if (delta < min_delta) {
			phy->rate = tmp;
			phy->prediv = prediv;
			phy->fbdiv = fbdiv;
			min_delta = delta;
		}
	}
}

static void rk618_dsi_phy_power_off(struct rk618_dsi *dsi)
{
	struct mipi_dphy *phy = &dsi->phy;

	regmap_update_bits(phy->regmap, MIPI_PHY_REG0, LANE_EN_MASK, 0);
	regmap_update_bits(phy->regmap, MIPI_PHY_REG1,
			   REG_DA_LDOPD | REG_DA_PLLPD,
			   REG_DA_LDOPD | REG_DA_PLLPD);
}

static void rk618_dsi_phy_power_on(struct rk618_dsi *dsi, u32 txclkesc)
{
	struct mipi_dphy *phy = &dsi->phy;
	u32 offset, value, index;
	const struct {
		unsigned int rate;
		u8 ths_settle;
		u8 ths_zero;
		u8 ths_trail;
	} timing_table[] = {
		{ 110000000, 0x00, 0x03, 0x0c},
		{ 150000000, 0x01, 0x04, 0x0d},
		{ 200000000, 0x02, 0x04, 0x11},
		{ 250000000, 0x03, 0x05, 0x14},
		{ 300000000, 0x04, 0x06, 0x18},
		{ 400000000, 0x05, 0x07, 0x1d},
		{ 500000000, 0x06, 0x08, 0x23},
		{ 600000000, 0x07, 0x0a, 0x29},
		{ 700000000, 0x08, 0x0b, 0x31},
		{ 800000000, 0x09, 0x0c, 0x34},
		{1000000000, 0x0a, 0x0f, 0x40},
	};
	u32 Ttxbyteclkhs, UI, Ttxddrclkhs, Ttxclkesc;
	u32 Tlpx, Ths_exit, Tclk_post, Tclk_pre, Ths_prepare;
	u32 Tta_go, Tta_sure, Tta_wait;

	Ttxbyteclkhs = div_u64(PSEC_PER_SEC, phy->rate / 8);
	UI = Ttxddrclkhs = div_u64(PSEC_PER_SEC, phy->rate);
	Ttxclkesc = div_u64(PSEC_PER_SEC, txclkesc);

	regmap_update_bits(phy->regmap, MIPI_PHY_REG3, REG_FBDIV_HI_MASK |
			   REG_PREDIV_MASK, REG_FBDIV_HI(phy->fbdiv >> 8) |
			   REG_PREDIV(phy->prediv));
	regmap_update_bits(phy->regmap, MIPI_PHY_REG4,
			   REG_FBDIV_LO_MASK, REG_FBDIV_LO(phy->fbdiv));
	regmap_update_bits(phy->regmap, MIPI_PHY_REG1,
			   REG_DA_LDOPD | REG_DA_PLLPD, 0);

	regmap_update_bits(phy->regmap, MIPI_PHY_REG0, LANE_EN_MASK,
			   LANE_EN_CK | GENMASK(dsi->lanes - 1 + 2, 2));

	regmap_update_bits(phy->regmap, MIPI_PHY_REG1,
			   REG_DA_SYNCRST, REG_DA_SYNCRST);
	udelay(1);
	regmap_update_bits(phy->regmap, MIPI_PHY_REG1, REG_DA_SYNCRST, 0);

	regmap_update_bits(phy->regmap, MIPI_PHY_REG20, REG_DIG_RSTN, 0);
	udelay(1);
	regmap_update_bits(phy->regmap, MIPI_PHY_REG20,
			   REG_DIG_RSTN, REG_DIG_RSTN);

	/* XXX */
	regmap_write(phy->regmap, MIPI_PHY_REG6, 0x11);
	regmap_write(phy->regmap, MIPI_PHY_REG7, 0x11);
	regmap_write(phy->regmap, MIPI_PHY_REG9, 0xcc);

	if (phy->rate < 800000000)
		regmap_update_bits(phy->regmap, MIPI_PHY_REG1,
				   REG_DA_PPFC, REG_DA_PPFC);
	else
		regmap_write(phy->regmap, MIPI_PHY_REG5, 0x30);

	for (index = 0; index < ARRAY_SIZE(timing_table); index++)
		if (phy->rate <= timing_table[index].rate)
			break;

	if (index == ARRAY_SIZE(timing_table))
		--index;

	for (offset = 0x100; offset <= 0x300; offset += 0x80) {
		regmap_update_bits(phy->regmap,
				   PHYREG(offset + THS_SETTLE_OFFSET),
				   THS_SETTLE_MASK,
				   THS_SETTLE(timing_table[index].ths_settle));

		/*
		 * The value of counter for HS Tlpx Time
		 * Tlpx = Tpin_txbyteclkhs * value
		 */
		Tlpx = 60 * PSEC_PER_NSEC;
		value = DIV_ROUND_UP(Tlpx, Ttxbyteclkhs);
		Tlpx = Ttxbyteclkhs * value;
		regmap_update_bits(phy->regmap,
				   PHYREG(offset + TLPX_OFFSET),
				   TLPX_MASK, TLPX(value));

		/*
		 * The value of counter for HS Ths-prepare
		 * For clock lane, Ths-prepare(38ns~95ns)
		 * For data lane, Ths-prepare(40ns+4UI~85ns+6UI)
		 * Ths-prepare = Ttxddrclkhs * value
		 */
		if (is_clk_lane(offset))
			Ths_prepare = 65 * PSEC_PER_NSEC;
		else
			Ths_prepare = 65 * PSEC_PER_NSEC + 4 * UI;

		value = DIV_ROUND_UP(Ths_prepare, Ttxddrclkhs);
		regmap_update_bits(phy->regmap,
				   PHYREG(offset + THS_PREPARE_OFFSET),
				   THS_PREPARE_MASK, THS_PREPARE(value));

		regmap_update_bits(phy->regmap,
				   PHYREG(offset + THS_ZERO_OFFSET),
				   THS_ZERO_MASK,
				   THS_ZERO(timing_table[index].ths_zero));

		regmap_update_bits(phy->regmap,
				   PHYREG(offset + THS_TRAIL_OFFSET),
				   THS_TRAIL_MASK,
				   THS_TRAIL(timing_table[index].ths_trail));

		/*
		 * The value of counter for HS Ths-exit
		 * Ths-exit = Tpin_txbyteclkhs * value
		 */
		Ths_exit = 120 * PSEC_PER_NSEC;
		value = DIV_ROUND_UP(Ths_exit, Ttxbyteclkhs);
		regmap_update_bits(phy->regmap,
				   PHYREG(offset + THS_EXIT_OFFSET),
				   THS_EXIT_MASK, THS_EXIT(value));

		/*
		 * The value of counter for HS Tclk-post
		 * Tclk-post = Ttxbyteclkhs * value
		 */
		Tclk_post = 70 * PSEC_PER_NSEC + 52 * UI;
		value = DIV_ROUND_UP(Tclk_post, Ttxbyteclkhs);
		regmap_update_bits(phy->regmap,
				   PHYREG(offset + TCLK_POST_OFFSET),
				   TCLK_POST_MASK, TCLK_POST(value));

		/*
		 * The value of counter for HS Twakup
		 * Twakup for ulpm,
		 * Twakup = Tpin_sys_clk * value
		 */
		regmap_update_bits(phy->regmap,
				   PHYREG(offset + TWAKUP_HI_OFFSET),
				   TWAKUP_HI_MASK, TWAKUP_HI(0x3));
		regmap_update_bits(phy->regmap,
				   PHYREG(offset + TWAKUP_LO_OFFSET),
				   TWAKUP_LO_MASK, TWAKUP_LO(0xff));

		/*
		 * The value of counter for HS Tclk-pre
		 * Tclk-pre for clock lane
		 * Tclk-pre = Tpin_txbyteclkhs * value
		 */
		Tclk_pre = 8 * UI;
		value = DIV_ROUND_UP(Tclk_pre, Ttxbyteclkhs);
		regmap_update_bits(phy->regmap,
				   PHYREG(offset + TCLK_PRE_OFFSET),
				   TCLK_PRE_MASK, TCLK_PRE(value));

		/*
		 * The value of counter for HS Tta-go
		 * Tta-go for turnaround
		 * Tta-go = Ttxclkesc * value
		 */
		Tta_go = 4 * Tlpx;
		value = DIV_ROUND_UP(Tta_go, Ttxclkesc);
		regmap_update_bits(phy->regmap,
				   PHYREG(offset + TTA_GO_OFFSET),
				   TTA_GO_MASK, TTA_GO(value));

		/*
		 * The value of counter for HS Tta-sure
		 * Tta-sure for turnaround
		 * Tta-sure = Ttxclkesc * value
		 */
		Tta_sure = 2 * Tlpx;
		value = DIV_ROUND_UP(Tta_sure, Ttxclkesc);
		regmap_update_bits(phy->regmap,
				   PHYREG(offset + TTA_SURE_OFFSET),
				   TTA_SURE_MASK, TTA_SURE(value));

		/*
		 * The value of counter for HS Tta-wait
		 * Tta-wait for turnaround
		 * Interval from receiving ppi turnaround request to
		 * sending esc request.
		 * Tta-wait = Ttxclkesc * value
		 */
		Tta_wait = 5 * Tlpx;
		value = DIV_ROUND_UP(Tta_wait, Ttxclkesc);
		regmap_update_bits(phy->regmap,
				   PHYREG(offset + TTA_WAIT_OFFSET),
				   TTA_WAIT_MASK, TTA_WAIT(value));
	}
}

static int rk618_dsi_pre_enable(struct rk618_dsi *dsi)
{
	struct drm_display_mode *mode = &dsi->mode;
	u32 esc_clk_div, txclkesc;
	u32 lanebyteclk, dpipclk;
	u32 hsw, hbp, vsw, vfp, vbp;
	u32 hsa_time, hbp_time, hline_time;
	u32 value;
	int ret;

	rk618_dsi_set_hs_clk(dsi);

	regmap_update_bits(dsi->regmap, DSI_PWR_UP, SHUTDOWNZ, RESET);

	/* Configuration of the internal clock dividers */
	esc_clk_div = DIV_ROUND_UP(dsi->phy.rate >> 3, 20000000);
	txclkesc = dsi->phy.rate >> 3 / esc_clk_div;
	value = TO_CLK_DIVIDSION(10) | TX_ESC_CLK_DIVIDSION(esc_clk_div);
	regmap_write(dsi->regmap, DSI_CLKMGR_CFG, value);

	/* The DPI interface configuration */
	value = DPI_VID(dsi->channel);

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		value |= VSYNC_ACTIVE_LOW;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		value |= HSYNC_ACTIVE_LOW;

	switch (dsi->format) {
	case MIPI_DSI_FMT_RGB666:
		value |= DPI_COLOR_CODING(PIXEL_COLOR_CODING_18BIT_2);
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		value |= DPI_COLOR_CODING(PIXEL_COLOR_CODING_18BIT_1);
		value |= EN18_LOOSELY;
		break;
	case MIPI_DSI_FMT_RGB565:
		value |= DPI_COLOR_CODING(PIXEL_COLOR_CODING_16BIT_1);
		break;
	case MIPI_DSI_FMT_RGB888:
	default:
		value |= DPI_COLOR_CODING(PIXEL_COLOR_CODING_24BIT);
		break;
	}

	regmap_write(dsi->regmap, DSI_DPI_CFG, value);

	/* Packet handler configuration */
	value = GEN_VID_RX(dsi->channel) | EN_CRC_RX | EN_ECC_RX | EN_BTA;

	if (!(dsi->mode_flags & MIPI_DSI_MODE_EOT_PACKET))
		value |= EN_EOTP_TX;

	regmap_write(dsi->regmap, DSI_PCKHDL_CFG, value);

	/* Video mode configuration */
	value = EN_LP_VACT | EN_LP_VBP | EN_LP_VFP | EN_LP_VSA;

	if (!(dsi->mode_flags & MIPI_DSI_MODE_VIDEO_HFP))
		value |= EN_LP_HFP;

	if (!(dsi->mode_flags & MIPI_DSI_MODE_VIDEO_HBP))
		value |= EN_LP_HBP;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		value |= VID_MODE_TYPE(BURST_MODE);
	else if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		value |= VID_MODE_TYPE(NON_BURST_MODE_SYNC_PULSE);
	else
		value |= VID_MODE_TYPE(NON_BURST_MODE_SYNC_EVENT);

	regmap_write(dsi->regmap, DSI_VID_MODE_CFG, value);

	/* Video packet configuration */
	regmap_write(dsi->regmap, DSI_VID_PKT_CFG,
		     VID_PKT_SIZE(mode->hdisplay));

	/* Timeout timers configuration */
	regmap_write(dsi->regmap, DSI_TO_CNT_CFG,
		     LPRX_TO_CNT(1000) | HSTX_TO_CNT(1000));

	hsw = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;
	vsw = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;

	/* Line timing configuration */
	lanebyteclk = (dsi->phy.rate >> 3) / USEC_PER_SEC;
	dpipclk = mode->clock / USEC_PER_MSEC;
	hline_time = DIV_ROUND_UP(mode->htotal * lanebyteclk, dpipclk);
	hbp_time = DIV_ROUND_UP(hbp * lanebyteclk, dpipclk);
	hsa_time = DIV_ROUND_UP(hsw * lanebyteclk, dpipclk);
	regmap_write(dsi->regmap, DSI_TMR_LINE_CFG, HLINE_TIME(hline_time) |
		     HBP_TIME(hbp_time) | HSA_TIME(hsa_time));

	/* Vertical timing configuration */
	regmap_write(dsi->regmap, DSI_VTIMING_CFG,
		     V_ACTIVE_LINES(mode->vdisplay) | VFP_LINES(vfp) |
		     VBP_LINES(vbp) | VSA_LINES(vsw));

	/* D-PHY interface configuration */
	value = N_LANES(dsi->lanes - 1) | PHY_STOP_WAIT_TIME(0x20);
	regmap_write(dsi->regmap, DSI_PHY_IF_CFG, value);

	/* D-PHY timing configuration */
	value = PHY_HS2LP_TIME(20) | PHY_LP2HS_TIME(16) | MAX_RD_TIME(10000);
	regmap_write(dsi->regmap, DSI_PHY_TMR_CFG, value);

	/* enables the D-PHY Clock Lane Module */
	regmap_update_bits(dsi->regmap, DSI_PHY_RSTZ,
			   PHY_ENABLECLK, PHY_ENABLECLK);

	regmap_write(dsi->regmap, DSI_INT_MSK0, 0);
	regmap_write(dsi->regmap, DSI_INT_MSK1, 0);

	regmap_update_bits(dsi->regmap, DSI_VID_MODE_CFG, EN_VIDEO_MODE, 0);
	regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG,
			   EN_CMD_MODE, EN_CMD_MODE);

	rk618_dsi_phy_power_on(dsi, txclkesc);

	/* wait for the PHY to acquire lock */
	ret = regmap_read_poll_timeout(dsi->regmap, DSI_PHY_STATUS,
				       value, value & PHYLOCK, 50, 1000);
	if (ret) {
		dev_err(dsi->dev, "PHY is not locked\n");
		return ret;
	}

	/* wait for the lane go to the stop state */
	ret = regmap_read_poll_timeout(dsi->regmap, DSI_PHY_STATUS,
				       value, value & PHYSTOPSTATELANE,
				       50, 1000);
	if (ret) {
		dev_err(dsi->dev, "lane module is not in stop state\n");
		return ret;
	}

	regmap_update_bits(dsi->regmap, DSI_PWR_UP, SHUTDOWNZ, POWER_UP);

	return 0;
}

static void rk618_dsi_enable(struct rk618_dsi *dsi)
{
	/* controls the D-PHY PPI txrequestclkhs signal */
	regmap_update_bits(dsi->regmap, DSI_PHY_IF_CTRL,
			   PHY_TXREQUESCLKHS, PHY_TXREQUESCLKHS);

	/* enables the DPI Video mode transmission */
	regmap_update_bits(dsi->regmap, DSI_PWR_UP, SHUTDOWNZ, RESET);
	regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG, EN_CMD_MODE, 0);
	regmap_update_bits(dsi->regmap, DSI_VID_MODE_CFG,
			   EN_VIDEO_MODE, EN_VIDEO_MODE);
	regmap_update_bits(dsi->regmap, DSI_PWR_UP, SHUTDOWNZ, POWER_UP);

	dev_info(dsi->dev, "final DSI-Link bandwidth: %lu x %d Mbps\n",
		 dsi->phy.rate / USEC_PER_SEC, dsi->lanes);
}

static void rk618_dsi_disable(struct rk618_dsi *dsi)
{
	/* enables the Command mode protocol for transmissions */
	regmap_update_bits(dsi->regmap, DSI_PWR_UP, SHUTDOWNZ, RESET);
	regmap_update_bits(dsi->regmap, DSI_PHY_IF_CTRL, PHY_TXREQUESCLKHS, 0);
	regmap_update_bits(dsi->regmap, DSI_VID_MODE_CFG, EN_VIDEO_MODE, 0);
	regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG,
			   EN_CMD_MODE, EN_CMD_MODE);
	regmap_update_bits(dsi->regmap, DSI_PWR_UP, SHUTDOWNZ, POWER_UP);
}

static void rk618_dsi_post_disable(struct rk618_dsi *dsi)
{
	regmap_update_bits(dsi->regmap, DSI_PWR_UP, SHUTDOWNZ, RESET);
	regmap_update_bits(dsi->regmap, DSI_PHY_RSTZ, PHY_ENABLECLK, 0);

	rk618_dsi_phy_power_off(dsi);
}

static struct drm_encoder *
rk618_dsi_connector_best_encoder(struct drm_connector *connector)
{
	struct rk618_dsi *dsi = connector_to_dsi(connector);

	return dsi->base.encoder;
}

static int rk618_dsi_connector_get_modes(struct drm_connector *connector)
{
	struct rk618_dsi *dsi = connector_to_dsi(connector);

	return drm_panel_get_modes(dsi->panel, connector);
}

static const struct drm_connector_helper_funcs
rk618_dsi_connector_helper_funcs = {
	.get_modes = rk618_dsi_connector_get_modes,
	.best_encoder = rk618_dsi_connector_best_encoder,
};

static enum drm_connector_status
rk618_dsi_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void rk618_dsi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs rk618_dsi_connector_funcs = {
	.detect = rk618_dsi_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rk618_dsi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void rk618_dsi_bridge_enable(struct drm_bridge *bridge)
{
	struct rk618_dsi *dsi = bridge_to_dsi(bridge);

	clk_prepare_enable(dsi->clock);

	rk618_dsi_pre_enable(dsi);
	drm_panel_prepare(dsi->panel);
	rk618_dsi_enable(dsi);
	drm_panel_enable(dsi->panel);
}

static void rk618_dsi_bridge_disable(struct drm_bridge *bridge)
{
	struct rk618_dsi *dsi = bridge_to_dsi(bridge);

	drm_panel_disable(dsi->panel);
	rk618_dsi_disable(dsi);
	drm_panel_unprepare(dsi->panel);
	rk618_dsi_post_disable(dsi);

	clk_disable_unprepare(dsi->clock);
}

static void rk618_dsi_bridge_mode_set(struct drm_bridge *bridge,
				      const struct drm_display_mode *mode,
				      const struct drm_display_mode *adj)
{
	struct rk618_dsi *dsi = bridge_to_dsi(bridge);

	if (bridge->driver_private)
		drm_mode_copy(&dsi->mode, bridge->driver_private);
	else
		drm_mode_copy(&dsi->mode, adj);
}

static int rk618_dsi_bridge_attach(struct drm_bridge *bridge,
				   enum drm_bridge_attach_flags flags)
{
	struct rk618_dsi *dsi = bridge_to_dsi(bridge);
	struct drm_connector *connector = &dsi->connector;
	struct drm_device *drm = bridge->dev;
	int ret;

	ret = drm_connector_init(drm, connector, &rk618_dsi_connector_funcs,
				 DRM_MODE_CONNECTOR_DSI);
	if (ret) {
		dev_err(dsi->dev, "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &rk618_dsi_connector_helper_funcs);
	drm_connector_attach_encoder(connector, bridge->encoder);

	dsi->sub_dev.connector = &dsi->connector;
	dsi->sub_dev.of_node = dsi->dev->of_node;
	rockchip_drm_register_sub_dev(&dsi->sub_dev);

	return 0;
}

static void rk618_dsi_bridge_detach(struct drm_bridge *bridge)
{
	struct rk618_dsi *dsi = bridge_to_dsi(bridge);

	rockchip_drm_unregister_sub_dev(&dsi->sub_dev);
}

static const struct drm_bridge_funcs rk618_dsi_bridge_funcs = {
	.attach = rk618_dsi_bridge_attach,
	.detach = rk618_dsi_bridge_detach,
	.mode_set = rk618_dsi_bridge_mode_set,
	.enable = rk618_dsi_bridge_enable,
	.disable = rk618_dsi_bridge_disable,
};

static ssize_t rk618_dsi_host_transfer(struct mipi_dsi_host *host,
				       const struct mipi_dsi_msg *msg)
{
	struct rk618_dsi *dsi = host_to_dsi(host);
	struct mipi_dsi_packet packet;
	u32 value, mask;
	int ret;

	if (msg->flags & MIPI_DSI_MSG_USE_LPM)
		regmap_update_bits(dsi->regmap, DSI_PHY_IF_CTRL,
				   PHY_TXREQUESCLKHS, 0);
	else
		regmap_update_bits(dsi->regmap, DSI_PHY_IF_CTRL,
				   PHY_TXREQUESCLKHS, PHY_TXREQUESCLKHS);

	switch (msg->type) {
	case MIPI_DSI_DCS_SHORT_WRITE:
		regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG, DCS_SW_0P_TX,
				   msg->flags & MIPI_DSI_MSG_USE_LPM ?
				   DCS_SW_0P_TX : 0);
		break;
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG, DCS_SW_1P_TX,
				   msg->flags & MIPI_DSI_MSG_USE_LPM ?
				   DCS_SW_1P_TX : 0);
		break;
	case MIPI_DSI_DCS_LONG_WRITE:
		regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG, DCS_LW_TX,
				   msg->flags & MIPI_DSI_MSG_USE_LPM ?
				   DCS_LW_TX : 0);
		break;
	case MIPI_DSI_DCS_READ:
		regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG, DCS_SR_0P_TX,
				   msg->flags & MIPI_DSI_MSG_USE_LPM ?
				   DCS_SR_0P_TX : 0);
		break;
	case MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE:
		regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG,
				   MAX_RD_PKT_SIZE,
				   msg->flags & MIPI_DSI_MSG_USE_LPM ?
				   MAX_RD_PKT_SIZE : 0);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
		regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG, GEN_SW_0P_TX,
				   msg->flags & MIPI_DSI_MSG_USE_LPM ?
				   GEN_SW_0P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
		regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG, GEN_SW_1P_TX,
				   msg->flags & MIPI_DSI_MSG_USE_LPM ?
				   GEN_SW_1P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG, GEN_SW_2P_TX,
				   msg->flags & MIPI_DSI_MSG_USE_LPM ?
				   GEN_SW_2P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_LONG_WRITE:
		regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG, GEN_LW_TX,
				   msg->flags & MIPI_DSI_MSG_USE_LPM ?
				   GEN_LW_TX : 0);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
		regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG, GEN_SR_0P_TX,
				   msg->flags & MIPI_DSI_MSG_USE_LPM ?
				   GEN_SR_0P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
		regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG, GEN_SR_1P_TX,
				   msg->flags & MIPI_DSI_MSG_USE_LPM ?
				   GEN_SR_1P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
		regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG, GEN_SR_2P_TX,
				   msg->flags & MIPI_DSI_MSG_USE_LPM ?
				   GEN_SR_2P_TX : 0);
		break;
	default:
		return -EINVAL;
	}

	/* create a packet to the DSI protocol */
	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret) {
		dev_err(dsi->dev, "failed to create packet: %d\n", ret);
		return ret;
	}

	/* Send payload */
	while (packet.payload_length >= 4) {
		mask = GEN_PLD_W_FULL;
		ret = regmap_read_poll_timeout(dsi->regmap, DSI_GEN_PKT_STATUS,
					       value, !(value & mask),
					       50, 1000);
		if (ret) {
			dev_err(dsi->dev, "Write payload FIFO is full\n");
			return ret;
		}

		value = get_unaligned_le32(packet.payload);
		regmap_write(dsi->regmap, DSI_GEN_PLD_DATA, value);
		packet.payload += 4;
		packet.payload_length -= 4;
	}

	value = 0;
	switch (packet.payload_length) {
	case 3:
		value |= packet.payload[2] << 16;
		/* Fall through */
	case 2:
		value |= packet.payload[1] << 8;
		/* Fall through */
	case 1:
		value |= packet.payload[0];
		regmap_write(dsi->regmap, DSI_GEN_PLD_DATA, value);
		break;
	}

	mask = GEN_CMD_FULL;
	ret = regmap_read_poll_timeout(dsi->regmap, DSI_GEN_PKT_STATUS,
				       value, !(value & mask), 50, 1000);
	if (ret) {
		dev_err(dsi->dev, "Command FIFO is full\n");
		return ret;
	}

	/* Send packet header */
	value = get_unaligned_le32(packet.header);
	regmap_write(dsi->regmap, DSI_GEN_HDR, value);

	mask = GEN_PLD_W_EMPTY | GEN_CMD_EMPTY;
	ret = regmap_read_poll_timeout(dsi->regmap, DSI_GEN_PKT_STATUS,
				       value, (value & mask) == mask, 50, 1000);
	if (ret) {
		dev_err(dsi->dev, "Write payload FIFO is not empty\n");
		return ret;
	}

	if (msg->rx_len) {
		u8 *payload = msg->rx_buf;
		u16 length;

		mask = GEN_RD_CMD_BUSY;
		ret = regmap_read_poll_timeout(dsi->regmap, DSI_GEN_PKT_STATUS,
					       value, !(value & mask),
					       50, 1000);
		if (ret) {
			dev_err(dsi->dev,
				"entire response is not stored in the FIFO\n");
			return ret;
		}

		/* Receive payload */
		for (length = msg->rx_len; length; length -= 4) {
			mask = GEN_PLD_R_EMPTY;
			ret = regmap_read_poll_timeout(dsi->regmap,
						       DSI_GEN_PKT_STATUS,
						       value, !(value & mask),
						       50, 1000);
			if (ret) {
				dev_err(dsi->dev,
					"Read payload FIFO is empty\n");
				return ret;
			}

			regmap_read(dsi->regmap, DSI_GEN_PLD_DATA, &value);

			switch (length) {
			case 3:
				payload[2] = (value >> 16) & 0xff;
				/* Fall through */
			case 2:
				payload[1] = (value >> 8) & 0xff;
				/* Fall through */
			case 1:
				payload[0] = value & 0xff;
				return length;
			}

			payload[0] = (value >>  0) & 0xff;
			payload[1] = (value >>  8) & 0xff;
			payload[2] = (value >> 16) & 0xff;
			payload[3] = (value >> 24) & 0xff;
			payload += 4;
		}
	}

	return packet.payload_length;
}

static int rk618_dsi_host_attach(struct mipi_dsi_host *host,
				 struct mipi_dsi_device *device)
{
	struct rk618_dsi *dsi = host_to_dsi(host);

	if (device->lanes < 1 || device->lanes > 4)
		return -EINVAL;

	dsi->lanes = device->lanes;
	dsi->channel = device->channel;
	dsi->format = device->format;
	dsi->mode_flags = device->mode_flags;

	dsi->panel = of_drm_find_panel(device->dev.of_node);
	if (!dsi->panel)
		return -EPROBE_DEFER;

	return 0;
}

static int rk618_dsi_host_detach(struct mipi_dsi_host *host,
				 struct mipi_dsi_device *device)
{
	return 0;
}

static const struct mipi_dsi_host_ops rk618_dsi_host_ops = {
	.attach = rk618_dsi_host_attach,
	.detach = rk618_dsi_host_detach,
	.transfer = rk618_dsi_host_transfer,
};

static bool rk618_dsi_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DSI_VERSION ... DSI_MAX_REGISTER:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rk618_dsi_host_regmap_config = {
	.name = "dsi",
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = DSI_MAX_REGISTER,
	.reg_format_endian = REGMAP_ENDIAN_NATIVE,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.readable_reg = rk618_dsi_readable_reg,
};

static bool rk618_dsi_phy_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MIPI_PHY_REG0 ... MIPI_PHY_MAX_REGISTER:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rk618_dsi_phy_regmap_config = {
	.name = "dphy",
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = MIPI_PHY_MAX_REGISTER,
	.reg_format_endian = REGMAP_ENDIAN_NATIVE,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.readable_reg = rk618_dsi_phy_readable_reg,
};

static int rk618_dsi_probe(struct platform_device *pdev)
{
	struct rk618 *rk618 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk618_dsi *dsi;
	int ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->dev = dev;
	dsi->parent = rk618;
	platform_set_drvdata(pdev, dsi);

	dsi->clock = devm_clk_get(dev, "dsi");
	if (IS_ERR(dsi->clock)) {
		ret = PTR_ERR(dsi->clock);
		dev_err(dev, "failed to get dsi clock: %d\n", ret);
		return ret;
	}

	dsi->regmap = devm_regmap_init_i2c(rk618->client,
					   &rk618_dsi_host_regmap_config);
	if (IS_ERR(dsi->regmap)) {
		ret = PTR_ERR(dsi->regmap);
		dev_err(dev, "failed to allocate host register map: %d\n", ret);
		return ret;
	}

	dsi->phy.regmap = devm_regmap_init_i2c(rk618->client,
					       &rk618_dsi_phy_regmap_config);
	if (IS_ERR(dsi->phy.regmap)) {
		ret = PTR_ERR(dsi->phy.regmap);
		dev_err(dev, "failed to allocate phy register map: %d\n", ret);
		return ret;
	}

	dsi->base.funcs = &rk618_dsi_bridge_funcs;
	dsi->base.of_node = dev->of_node;
	drm_bridge_add(&dsi->base);

	dsi->host.dev = dev;
	dsi->host.ops = &rk618_dsi_host_ops;
	ret = mipi_dsi_host_register(&dsi->host);
	if (ret) {
		drm_bridge_remove(&dsi->base);
		dev_err(dev, "failed to register host: %d\n", ret);
		return ret;
	}

	return 0;
}

static int rk618_dsi_remove(struct platform_device *pdev)
{
	struct rk618_dsi *dsi = platform_get_drvdata(pdev);

	mipi_dsi_host_unregister(&dsi->host);
	drm_bridge_remove(&dsi->base);

	return 0;
}

static const struct of_device_id rk618_dsi_of_match[] = {
	{ .compatible = "rockchip,rk618-dsi", },
	{},
};
MODULE_DEVICE_TABLE(of, rk618_dsi_of_match);

static struct platform_driver rk618_dsi_driver = {
	.driver = {
		.name = "rk618-dsi",
		.of_match_table = of_match_ptr(rk618_dsi_of_match),
	},
	.probe = rk618_dsi_probe,
	.remove = rk618_dsi_remove,
};
module_platform_driver(rk618_dsi_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK618 MIPI-DSI driver");
MODULE_LICENSE("GPL v2");
