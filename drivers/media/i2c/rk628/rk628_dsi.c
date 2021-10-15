// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunqing Chen <csq@rock-chips.com>
 */

#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>

#include "rk628.h"
#include "rk628_cru.h"
#include "rk628_dsi.h"
#include "rk628_mipi_dphy.h"
#include "rk628_combtxphy.h"

enum {
	VID_MODE_TYPE_NON_BURST_SYNC_PULSES,
	VID_MODE_TYPE_NON_BURST_SYNC_EVENTS,
	VID_MODE_TYPE_BURST,
};

#define MIPI_DSI_MODE_VIDEO		BIT(0)
#define MIPI_DSI_MODE_VIDEO_BURST	BIT(1)
#define MIPI_DSI_MODE_VIDEO_SYNC_PULSE	BIT(2)
#define MIPI_DSI_MODE_VIDEO_HFP		BIT(5)
#define MIPI_DSI_MODE_VIDEO_HBP		BIT(6)
#define MIPI_DSI_MODE_EOT_PACKET	BIT(9)
#define MIPI_DSI_CLOCK_NON_CONTINUOUS	BIT(10)
#define MIPI_DSI_MODE_LPM		BIT(11)

static inline int dsi_write(struct rk628 *rk628, int id, u32 reg, u32 val)
{
	unsigned int dsi_base;

	dsi_base = id ? DSI1_BASE : DSI0_BASE;

	return rk628_i2c_write(rk628, dsi_base + reg, val);
}

static inline int dsi_read(struct rk628 *rk628, int id, u32 reg, u32 *val)
{
	unsigned int dsi_base;

	dsi_base = id ? DSI1_BASE : DSI0_BASE;

	return rk628_i2c_read(rk628, dsi_base + reg, val);
}

static inline int dsi_update_bits(struct rk628 *rk628, int id,
				  u32 reg, u32 mask, u32 val)
{
	unsigned int dsi_base;

	dsi_base = id ? DSI1_BASE : DSI0_BASE;

	return rk628_i2c_update_bits(rk628, dsi_base + reg, mask, val);
}

static void mipi_dphy_power_on_dsi(struct rk628_dsi *dsi)
{
	int dev_id;
	unsigned int dsi_base;
	unsigned int val, mask;
	int ret;
	struct rk628 *rk628 = dsi->rk628;

	dev_id = RK628_DEV_DSI0;
	dsi_base = DSI0_BASE;

	dsi_update_bits(rk628, 0, DSI_PHY_RSTZ, PHY_ENABLECLK, 0);
	dsi_update_bits(rk628, 0, DSI_PHY_RSTZ, PHY_SHUTDOWNZ, 0);
	dsi_update_bits(rk628, 0, DSI_PHY_RSTZ, PHY_RSTZ, 0);
	testif_testclr_assert(dsi->rk628);

	/* Set all REQUEST inputs to zero */
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
			      FORCERXMODE_MASK | FORCETXSTOPMODE_MASK,
			      FORCETXSTOPMODE(0) | FORCERXMODE(0));
	udelay(1);

	testif_testclr_deassert(dsi->rk628);

	mipi_dphy_init_hsfreqrange(dsi->rk628, dsi->lane_mbps);

	dsi_update_bits(rk628, 0, DSI_PHY_RSTZ,
			PHY_ENABLECLK, PHY_ENABLECLK);
	dsi_update_bits(rk628, 0, DSI_PHY_RSTZ,
			PHY_SHUTDOWNZ, PHY_SHUTDOWNZ);
	dsi_update_bits(rk628, 0, DSI_PHY_RSTZ, PHY_RSTZ, PHY_RSTZ);
	usleep_range(1500, 2000);

	rk628_txphy_power_on(rk628);

	ret = regmap_read_poll_timeout(rk628->regmap[dev_id],
				       dsi_base + DSI_PHY_STATUS,
				       val, val & PHY_LOCK, 0, 1000);
	if (ret < 0)
		dev_err(rk628->dev, "PHY is not locked\n");

	usleep_range(100, 200);

	mask = PHY_STOPSTATELANE;
	ret = regmap_read_poll_timeout(rk628->regmap[dev_id],
				       dsi_base + DSI_PHY_STATUS,
				       val, (val & mask) == mask,
				       0, 1000);
	if (ret < 0)
		dev_err(rk628->dev, "lane module is not in stop state\n");

	udelay(10);
}

static void rk628_dsi_pre_enable(struct rk628_dsi *dsi)
{
	u32 val;
	struct rk628 *rk628 = dsi->rk628;
	u32 lane_mbps = dsi->lane_mbps;

	dsi_write(rk628, 0, DSI_PWR_UP, RESET);
	dsi_write(rk628, 0, DSI_MODE_CFG, CMD_VIDEO_MODE(COMMAND_MODE));

	val = DIV_ROUND_UP(lane_mbps >> 3, 20);
	dsi_write(rk628, 0, DSI_CLKMGR_CFG,
		  TO_CLK_DIVISION(10) | TX_ESC_CLK_DIVISION(val));

	val = CRC_RX_EN | ECC_RX_EN | BTA_EN | EOTP_TX_EN;
	if (dsi->mode_flags & MIPI_DSI_MODE_EOT_PACKET)
		val &= ~EOTP_TX_EN;

	dsi_write(rk628, 0, DSI_PCKHDL_CFG, val);

	dsi_write(rk628, 0, DSI_TO_CNT_CFG,
		  HSTX_TO_CNT(1000) | LPRX_TO_CNT(1000));
	dsi_write(rk628, 0, DSI_BTA_TO_CNT, 0xd00);
	dsi_write(rk628, 0, DSI_PHY_TMR_CFG,
		  PHY_HS2LP_TIME(0x14) | PHY_LP2HS_TIME(0x10) |
		  MAX_RD_TIME(10000));
	dsi_write(rk628, 0, DSI_PHY_TMR_LPCLK_CFG,
		  PHY_CLKHS2LP_TIME(0x40) | PHY_CLKLP2HS_TIME(0x40));
	dsi_write(rk628, 0, DSI_PHY_IF_CFG,
		  PHY_STOP_WAIT_TIME(0x20) | N_LANES(4 - 1));

	mipi_dphy_power_on_dsi(dsi);

	dsi_write(rk628, 0, DSI_PWR_UP, POWER_UP);
}

static void rk628_dsi_set_vid_mode(struct rk628_dsi *dsi)
{
	unsigned int lanebyteclk = (dsi->lane_mbps * 1000L) >> 3;
	u64 dpipclk;
	u32 hline, hs, hbp, hline_time, hs_time, hbp_time;
	u32 vactive, vs, vfp, vbp;
	u32 val;
	int pkt_size;
	struct v4l2_bt_timings *bt = &dsi->timings.bt;
	struct rk628 *rk628 = dsi->rk628;

	dpipclk = bt->pixelclock;
	do_div(dpipclk, 1000);
	val = LP_HFP_EN | LP_HBP_EN | LP_VACT_EN | LP_VFP_EN | LP_VBP_EN |
	      LP_VSA_EN;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_HFP)
		val &= ~LP_HFP_EN;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_HBP)
		val &= ~LP_HBP_EN;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		val |= VID_MODE_TYPE_BURST;
	else if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		val |= VID_MODE_TYPE_NON_BURST_SYNC_PULSES;
	else
		val |= VID_MODE_TYPE_NON_BURST_SYNC_EVENTS;

	dsi_write(rk628, 0, DSI_VID_MODE_CFG, val);

	if (dsi->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS)
		dsi_update_bits(rk628, 0, DSI_LPCLK_CTRL,
				AUTO_CLKLANE_CTRL, AUTO_CLKLANE_CTRL);


	pkt_size = VID_PKT_SIZE(bt->width);

	dsi_write(rk628, 0, DSI_VID_PKT_SIZE, pkt_size);

	vactive = bt->height;
	vs = bt->vsync;
	vfp = bt->vfrontporch;
	vbp = bt->vbackporch;
	hs = bt->hsync;
	hbp = bt->hbackporch;
	hline = bt->width;

	dev_info(dsi->rk628->dev, "h: %d %d %d %d, v:%d %d %d %d clock:%llu\n",
		 bt->width, bt->hfrontporch, bt->hsync, bt->hbackporch,
		 bt->height, bt->vfrontporch, bt->vsync, bt->vbackporch,
		 bt->pixelclock);

	//hline_time = hline * lanebyteclk / dpipclk;
	hline_time = DIV_ROUND_CLOSEST_ULL(hline * lanebyteclk, dpipclk);
	dsi_write(rk628, 0, DSI_VID_HLINE_TIME,
		  VID_HLINE_TIME(hline_time));
	//hs_time = hs * lanebyteclk / dpipclk;
	hs_time = DIV_ROUND_CLOSEST_ULL(hs * lanebyteclk, dpipclk);
	dsi_write(rk628, 0, DSI_VID_HSA_TIME, VID_HSA_TIME(hs_time));
	//hbp_time = hbp * lanebyteclk / dpipclk;
	hbp_time = DIV_ROUND_CLOSEST_ULL(hbp * lanebyteclk, dpipclk);
	dsi_write(rk628, 0, DSI_VID_HBP_TIME, VID_HBP_TIME(hbp_time));

	dsi_write(rk628, 0, DSI_VID_VACTIVE_LINES, vactive);
	dsi_write(rk628, 0, DSI_VID_VSA_LINES, vs);
	dsi_write(rk628, 0, DSI_VID_VFP_LINES, vfp);
	dsi_write(rk628, 0, DSI_VID_VBP_LINES, vbp);

	dsi_write(rk628, 0, DSI_MODE_CFG, CMD_VIDEO_MODE(VIDEO_MODE));
}

static void rk628_dsi_set_cmd_mode(struct rk628_dsi *dsi)
{
	struct rk628 *rk628 = dsi->rk628;

	dsi_update_bits(rk628, 0, DSI_CMD_MODE_CFG, DCS_LW_TX, 0);
	dsi_write(rk628, 0, DSI_EDPI_CMD_SIZE,
		  EDPI_ALLOWED_CMD_SIZE(dsi->timings.bt.width));
	dsi_write(rk628, 0, DSI_MODE_CFG, CMD_VIDEO_MODE(COMMAND_MODE));
}

static void rk628_dsi_enable(struct rk628_dsi *dsi)
{
	u32 val;
	struct rk628 *rk628 = dsi->rk628;

	dsi_write(rk628, 0, DSI_PWR_UP, RESET);

	val = DPI_COLOR_CODING(5);

	dsi_write(rk628, 0, DSI_DPI_COLOR_CODING, val);

	val = 0;

	/*
	 * if (mode->flags & DRM_MODE_FLAG_NVSYNC)
	 *	val |= VSYNC_ACTIVE_LOW;
	 * if (mode->flags & DRM_MODE_FLAG_NHSYNC)
	 *	val |= HSYNC_ACTIVE_LOW;
	 */

	dsi_write(rk628, 0, DSI_DPI_CFG_POL, val);

	dsi_write(rk628, 0, DSI_DPI_VCID, DPI_VID(0));
	dsi_write(rk628, 0, DSI_DPI_LP_CMD_TIM,
		  OUTVACT_LPCMD_TIME(4) | INVACT_LPCMD_TIME(4));
	dsi_update_bits(rk628, 0, DSI_LPCLK_CTRL,
			PHY_TXREQUESTCLKHS | AUTO_CLKLANE_CTRL,
			PHY_TXREQUESTCLKHS | AUTO_CLKLANE_CTRL);
	if (dsi->vid_mode == VIDEO_MODE)
		rk628_dsi_set_vid_mode(dsi);
	else
		rk628_dsi_set_cmd_mode(dsi);

	dsi_write(rk628, 0, DSI_PWR_UP, POWER_UP);
}

static u32 rk628_dsi_get_lane_rate(struct rk628_dsi *dsi)
{
	u32 lane_rate;
	u32 max_lane_rate = 1500;
	u8 bpp, lanes;
	u64 pixelclock = dsi->timings.bt.pixelclock;

	bpp = 24;
	lanes = 4;
	pixelclock = div_u64(pixelclock, 1000 * 1000);
	lane_rate = pixelclock  * bpp;
	lane_rate = div_u64(lane_rate, lanes);
	lane_rate = DIV_ROUND_UP(lane_rate * 5, 4);

	if (lane_rate > max_lane_rate)
		lane_rate = max_lane_rate;

	return lane_rate;
}

void rk628_mipi_dsi_power_on(struct rk628_dsi *dsi)
{
	struct rk628 *rk628 = dsi->rk628;
	u32 rate = rk628_dsi_get_lane_rate(dsi);
	int bus_width;

	rk628_i2c_update_bits(rk628, GRF_SYSTEM_CON0, SW_OUTPUT_MODE_MASK,
			      SW_OUTPUT_MODE(OUTPUT_MODE_DSI));
	rk628_i2c_update_bits(rk628, GRF_POST_PROC_CON, SW_SPLIT_EN, 0);

	bus_width =  rate << 8;
	bus_width |= COMBTXPHY_MODULEA_EN;
	rk628_txphy_set_bus_width(dsi->rk628, bus_width);
	rk628_txphy_set_mode(dsi->rk628, PHY_MODE_VIDEO_MIPI);
	dsi->lane_mbps = rk628_txphy_get_bus_width(dsi->rk628);
	dev_dbg(dsi->rk628->dev, "%s mipi bitrate:%llu mbps\n", __func__,
		dsi->lane_mbps);

	rk628_dsi_pre_enable(dsi);

	rk628_dsi_enable(dsi);
}
EXPORT_SYMBOL(rk628_mipi_dsi_power_on);
