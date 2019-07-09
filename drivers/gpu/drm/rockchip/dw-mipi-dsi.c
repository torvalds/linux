/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/iopoll.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/mfd/syscon.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drmP.h>
#include <video/mipi_display.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define RK3288_GRF_SOC_CON6		0x025c
#define RK3288_DSI0_SEL_VOP_LIT		BIT(6)
#define RK3288_DSI1_SEL_VOP_LIT		BIT(9)

#define RK3399_GRF_SOC_CON20		0x6250
#define RK3399_DSI0_SEL_VOP_LIT		BIT(0)
#define RK3399_DSI1_SEL_VOP_LIT		BIT(4)

/* disable turnrequest, turndisable, forcetxstopmode, forcerxmode */
#define RK3399_GRF_SOC_CON22		0x6258
#define RK3399_GRF_DSI_MODE		0xffff0000

#define UPDATE(v, h, l)			(((v) << (l)) & GENMASK((h), (l)))

/* DWC_mipi_dsi_host registers */
#define DSI_VERSION			0x000
#define DSI_PWR_UP			0x004
#define RESET				0
#define POWER_UP			BIT(0)
#define DSI_CLKMGR_CFG			0x008
#define TO_CLK_DIVISION(x)		UPDATE(x, 15,  8)
#define TX_ESC_CLK_DIVISION(x)		UPDATE(x,  7,  0)
#define DSI_DPI_VCID			0x00c
#define DPI_VID(x)			UPDATE(x,  1,  0)
#define DSI_DPI_COLOR_CODING		0x010
#define LOOSELY18_EN			BIT(8)
#define DPI_COLOR_CODING(x)		UPDATE(x,  3,  0)
#define DSI_DPI_CFG_POL			0x014
#define COLORM_ACTIVE_LOW		BIT(4)
#define SHUTD_ACTIVE_LOW		BIT(3)
#define HSYNC_ACTIVE_LOW		BIT(2)
#define VSYNC_ACTIVE_LOW		BIT(1)
#define DATAEN_ACTIVE_LOW		BIT(0)
#define DSI_DPI_LP_CMD_TIM		0x018
#define OUTVACT_LPCMD_TIME(x)		UPDATE(x, 23, 16)
#define INVACT_LPCMD_TIME(x)		UPDATE(x,  7,  0)
#define DSI_PCKHDL_CFG			0x02c
#define CRC_RX_EN			BIT(4)
#define ECC_RX_EN			BIT(3)
#define BTA_EN				BIT(2)
#define EOTP_RX_EN			BIT(1)
#define EOTP_TX_EN			BIT(0)
#define DSI_MODE_CFG			0x034
#define CMD_VIDEO_MODE(x)		UPDATE(x,  0,  0)
#define DSI_VID_MODE_CFG		0x038
#define VPG_EN				BIT(16)
#define LP_CMD_EN			BIT(15)
#define FRAME_BTA_ACK_EN		BIT(14)
#define LP_HFP_EN			BIT(13)
#define LP_HBP_EN			BIT(12)
#define LP_VACT_EN			BIT(11)
#define LP_VFP_EN			BIT(10)
#define LP_VBP_EN			BIT(9)
#define LP_VSA_EN			BIT(8)
#define VID_MODE_TYPE(x)		UPDATE(x,  1,  0)
#define DSI_VID_PKT_SIZE		0x03c
#define VID_PKT_SIZE(x)			UPDATE(x, 13,  0)
#define DSI_VID_HSA_TIME		0x048
#define VID_HSA_TIME(x)			UPDATE(x, 11,  0)
#define DSI_VID_HBP_TIME		0x04c
#define VID_HBP_TIME(x)			UPDATE(x, 11,  0)
#define DSI_VID_HLINE_TIME		0x050
#define VID_HLINE_TIME(x)		UPDATE(x, 14,  0)
#define DSI_VID_VSA_LINES		0x054
#define VSA_LINES(x)			UPDATE(x,  9,  0)
#define DSI_VID_VBP_LINES		0x058
#define VBP_LINES(x)			UPDATE(x,  9,  0)
#define DSI_VID_VFP_LINES		0x05c
#define VFP_LINES(x)			UPDATE(x,  9,  0)
#define DSI_VID_VACTIVE_LINES		0x060
#define V_ACTIVE_LINES(x)		UPDATE(x, 13,  0)
#define DSI_CMD_MODE_CFG		0x068
#define MAX_RD_PKT_SIZE			BIT(24)
#define DCS_LW_TX			BIT(19)
#define DCS_SR_0P_TX			BIT(18)
#define DCS_SW_1P_TX			BIT(17)
#define DCS_SW_0P_TX			BIT(16)
#define GEN_LW_TX			BIT(14)
#define GEN_SR_2P_TX			BIT(13)
#define GEN_SR_1P_TX			BIT(12)
#define GEN_SR_0P_TX			BIT(11)
#define GEN_SW_2P_TX			BIT(10)
#define GEN_SW_1P_TX			BIT(9)
#define GEN_SW_0P_TX			BIT(8)
#define ACK_RQST_EN			BIT(1)
#define TEAR_FX_EN			BIT(0)
#define DSI_GEN_HDR			0x06c
#define GEN_WC_MSBYTE(x)		UPDATE(x, 23, 16)
#define GEN_WC_LSBYTE(x)		UPDATE(x, 15,  8)
#define GEN_VC(x)			UPDATE(x,  7,  6)
#define GEN_DT(x)			UPDATE(x,  5,  0)
#define DSI_GEN_PLD_DATA		0x070
#define DSI_CMD_PKT_STATUS		0x074
#define GEN_RD_CMD_BUSY			BIT(6)
#define GEN_PLD_R_FULL			BIT(5)
#define GEN_PLD_R_EMPTY			BIT(4)
#define GEN_PLD_W_FULL			BIT(3)
#define GEN_PLD_W_EMPTY			BIT(2)
#define GEN_CMD_FULL			BIT(1)
#define GEN_CMD_EMPTY			BIT(0)
#define DSI_TO_CNT_CFG			0x078
#define HSTX_TO_CNT(x)			UPDATE(x, 31, 16)
#define LPRX_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_HS_RD_TO_CNT		0x07c
#define HS_RD_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_LP_RD_TO_CNT		0x080
#define LP_RD_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_HS_WR_TO_CNT		0x084
#define HS_WR_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_LP_WR_TO_CNT		0x088
#define LP_WR_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_BTA_TO_CNT			0x08c
#define BTA_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_LPCLK_CTRL			0x094
#define AUTO_CLKLANE_CTRL		BIT(1)
#define PHY_TXREQUESTCLKHS		BIT(0)
#define DSI_PHY_TMR_LPCLK_CFG		0x098
#define PHY_CLKHS2LP_TIME(x)		UPDATE(x, 25, 16)
#define PHY_CLKLP2HS_TIME(x)		UPDATE(x,  9,  0)
#define DSI_PHY_TMR_CFG			0x09c
#define PHY_HS2LP_TIME(x)		UPDATE(x, 31, 24)
#define PHY_LP2HS_TIME(x)		UPDATE(x, 23, 16)
#define MAX_RD_TIME(x)			UPDATE(x, 14,  0)
#define DSI_PHY_RSTZ			0x0a0
#define PHY_FORCEPLL			BIT(3)
#define PHY_ENABLECLK			BIT(2)
#define PHY_RSTZ			BIT(1)
#define PHY_SHUTDOWNZ			BIT(0)
#define DSI_PHY_IF_CFG			0x0a4
#define PHY_STOP_WAIT_TIME(x)		UPDATE(x, 15,  8)
#define N_LANES(x)			UPDATE(x,  1,  0)
#define DSI_PHY_STATUS			0x0b0
#define PHY_STOPSTATE3LANE		BIT(11)
#define PHY_STOPSTATE2LANE		BIT(9)
#define PHY_STOPSTATE1LANE		BIT(7)
#define PHY_STOPSTATE0LANE		BIT(4)
#define PHY_STOPSTATECLKLANE		BIT(2)
#define PHY_LOCK			BIT(0)
#define DSI_PHY_TST_CTRL0		0x0b4
#define PHY_TESTCLK			BIT(1)
#define PHY_TESTCLR			BIT(0)
#define DSI_PHY_TST_CTRL1		0x0b8
#define PHY_TESTEN			BIT(16)
#define PHY_TESTDOUT_SHIFT		8
#define PHY_TESTDIN_MASK		GENMASK(7, 0)
#define PHY_TESTDIN(x)			UPDATE(x, 7, 0)
#define DSI_INT_ST0			0x0bc
#define DSI_INT_ST1			0x0c0
#define DSI_INT_MSK0			0x0c4
#define DSI_INT_MSK1			0x0c8

#define PHY_STATUS_TIMEOUT_US		10000
#define CMD_PKT_STATUS_TIMEOUT_US	20000

#define BYPASS_VCO_RANGE	BIT(7)
#define VCO_RANGE_CON_SEL(val)	(((val) & 0x7) << 3)
#define VCO_IN_CAP_CON_DEFAULT	(0x0 << 1)
#define VCO_IN_CAP_CON_LOW	(0x1 << 1)
#define VCO_IN_CAP_CON_HIGH	(0x2 << 1)
#define REF_BIAS_CUR_SEL	BIT(0)

#define CP_CURRENT_3MA		BIT(3)
#define CP_PROGRAM_EN		BIT(7)
#define LPF_PROGRAM_EN		BIT(6)
#define LPF_RESISTORS_20_KOHM	0

#define HSFREQRANGE_SEL(val)	(((val) & 0x3f) << 1)

#define INPUT_DIVIDER(val)	(((val) - 1) & 0x7f)
#define LOW_PROGRAM_EN		0
#define HIGH_PROGRAM_EN		BIT(7)
#define LOOP_DIV_LOW_SEL(val)	(((val) - 1) & 0x1f)
#define LOOP_DIV_HIGH_SEL(val)	((((val) - 1) >> 5) & 0x1f)
#define PLL_LOOP_DIV_EN		BIT(5)
#define PLL_INPUT_DIV_EN	BIT(4)

#define POWER_CONTROL		BIT(6)
#define INTERNAL_REG_CURRENT	BIT(3)
#define BIAS_BLOCK_ON		BIT(2)
#define BANDGAP_ON		BIT(0)

#define TER_RESISTOR_HIGH	BIT(7)
#define	TER_RESISTOR_LOW	0
#define LEVEL_SHIFTERS_ON	BIT(6)
#define TER_CAL_DONE		BIT(5)
#define SETRD_MAX		(0x7 << 2)
#define POWER_MANAGE		BIT(1)
#define TER_RESISTORS_ON	BIT(0)

#define BIASEXTR_SEL(val)	((val) & 0x7)
#define BANDGAP_SEL(val)	((val) & 0x7)
#define TLP_PROGRAM_EN		BIT(7)
#define THS_PRE_PROGRAM_EN	BIT(7)
#define THS_ZERO_PROGRAM_EN	BIT(6)

#define DW_MIPI_NEEDS_PHY_CFG_CLK	BIT(0)

enum {
	BANDGAP_97_07,
	BANDGAP_98_05,
	BANDGAP_99_02,
	BANDGAP_100_00,
	BANDGAP_93_17,
	BANDGAP_94_15,
	BANDGAP_95_12,
	BANDGAP_96_10,
};

enum {
	BIASEXTR_87_1,
	BIASEXTR_91_5,
	BIASEXTR_95_9,
	BIASEXTR_100,
	BIASEXTR_105_94,
	BIASEXTR_111_88,
	BIASEXTR_118_8,
	BIASEXTR_127_7,
};

enum dpi_color_coding {
	DPI_COLOR_CODING_16BIT_1,
	DPI_COLOR_CODING_16BIT_2,
	DPI_COLOR_CODING_16BIT_3,
	DPI_COLOR_CODING_18BIT_1,
	DPI_COLOR_CODING_18BIT_2,
	DPI_COLOR_CODING_24BIT,
};

enum vid_mode_type {
	VID_MODE_TYPE_NON_BURST_SYNC_PULSES,
	VID_MODE_TYPE_NON_BURST_SYNC_EVENTS,
	VID_MODE_TYPE_BURST,
};

enum operation_mode {
	VIDEO_MODE,
	COMMAND_MODE,
};

struct dw_mipi_dsi_plat_data {
	u32 dsi0_en_bit;
	u32 dsi1_en_bit;
	u32 grf_switch_reg;
	u32 grf_dsi0_mode;
	u32 grf_dsi0_mode_reg;
	unsigned int flags;
	unsigned int max_data_lanes;
};

struct mipi_dphy {
	struct clk *ref_clk;
	struct clk *cfg_clk;
	u16 input_div;
	u16 feedback_div;
};

struct dw_mipi_dsi {
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct mipi_dsi_host host;
	struct drm_panel *panel;
	struct device *dev;
	struct regmap *grf_regmap;
	void __iomem *base;
	struct clk *pclk;
	struct mipi_dphy dphy;

	int dpms_mode;
	unsigned int lane_mbps; /* per lane */
	u32 channel;
	u32 lanes;
	u32 format;
	unsigned long mode_flags;

	const struct dw_mipi_dsi_plat_data *pdata;
};

struct dphy_pll_testdin_map {
	unsigned int max_mbps;
	u8 testdin;
};

/* The table is based on 27MHz DPHY pll reference clock. */
static const struct dphy_pll_testdin_map dptdin_map[] = {
	{  90, 0x00}, { 100, 0x10}, { 110, 0x20}, { 130, 0x01},
	{ 140, 0x11}, { 150, 0x21}, { 170, 0x02}, { 180, 0x12},
	{ 200, 0x22}, { 220, 0x03}, { 240, 0x13}, { 250, 0x23},
	{ 270, 0x04}, { 300, 0x14}, { 330, 0x05}, { 360, 0x15},
	{ 400, 0x25}, { 450, 0x06}, { 500, 0x16}, { 550, 0x07},
	{ 600, 0x17}, { 650, 0x08}, { 700, 0x18}, { 750, 0x09},
	{ 800, 0x19}, { 850, 0x29}, { 900, 0x39}, { 950, 0x0a},
	{1000, 0x1a}, {1050, 0x2a}, {1100, 0x3a}, {1150, 0x0b},
	{1200, 0x1b}, {1250, 0x2b}, {1300, 0x3b}, {1350, 0x0c},
	{1400, 0x1c}, {1450, 0x2c}, {1500, 0x3c}
};

static int max_mbps_to_testdin(unsigned int max_mbps)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dptdin_map); i++)
		if (dptdin_map[i].max_mbps > max_mbps)
			return dptdin_map[i].testdin;

	return -EINVAL;
}

/*
 * The controller should generate 2 frames before
 * preparing the peripheral.
 */
static void dw_mipi_dsi_wait_for_two_frames(struct drm_display_mode *mode)
{
	int refresh, two_frames;

	refresh = drm_mode_vrefresh(mode);
	two_frames = DIV_ROUND_UP(MSEC_PER_SEC, refresh) * 2;
	msleep(two_frames);
}

static inline struct dw_mipi_dsi *host_to_dsi(struct mipi_dsi_host *host)
{
	return container_of(host, struct dw_mipi_dsi, host);
}

static inline struct dw_mipi_dsi *con_to_dsi(struct drm_connector *con)
{
	return container_of(con, struct dw_mipi_dsi, connector);
}

static inline struct dw_mipi_dsi *encoder_to_dsi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct dw_mipi_dsi, encoder);
}

static inline void dsi_write(struct dw_mipi_dsi *dsi, u32 reg, u32 val)
{
	writel(val, dsi->base + reg);
}

static inline u32 dsi_read(struct dw_mipi_dsi *dsi, u32 reg)
{
	return readl(dsi->base + reg);
}

static void dw_mipi_dsi_phy_write(struct dw_mipi_dsi *dsi, u8 test_code,
				  u8 test_data)
{
	/*
	 * With the falling edge on TESTCLK, the TESTDIN[7:0] signal content
	 * is latched internally as the current test code. Test data is
	 * programmed internally by rising edge on TESTCLK.
	 */
	dsi_write(dsi, DSI_PHY_TST_CTRL0, PHY_TESTCLK);

	dsi_write(dsi, DSI_PHY_TST_CTRL1, PHY_TESTEN | PHY_TESTDIN(test_code));

	dsi_write(dsi, DSI_PHY_TST_CTRL0, 0);

	dsi_write(dsi, DSI_PHY_TST_CTRL1, PHY_TESTDIN(test_data));

	dsi_write(dsi, DSI_PHY_TST_CTRL0, PHY_TESTCLK);
}

/**
 * ns2bc - Nanoseconds to byte clock cycles
 */
static inline unsigned int ns2bc(struct dw_mipi_dsi *dsi, int ns)
{
	return DIV_ROUND_UP(ns * dsi->lane_mbps / 8, 1000);
}

/**
 * ns2ui - Nanoseconds to UI time periods
 */
static inline unsigned int ns2ui(struct dw_mipi_dsi *dsi, int ns)
{
	return DIV_ROUND_UP(ns * dsi->lane_mbps, 1000);
}

static int dw_mipi_dsi_phy_init(struct dw_mipi_dsi *dsi)
{
	struct mipi_dphy *dphy = &dsi->dphy;
	int ret, testdin, vco, val;

	vco = (dsi->lane_mbps < 200) ? 0 : (dsi->lane_mbps + 100) / 200;

	testdin = max_mbps_to_testdin(dsi->lane_mbps);
	if (testdin < 0) {
		DRM_DEV_ERROR(dsi->dev,
			      "failed to get testdin for %dmbps lane clock\n",
			      dsi->lane_mbps);
		return testdin;
	}

	/* Start by clearing PHY state */
	dsi_write(dsi, DSI_PHY_TST_CTRL0, 0);
	dsi_write(dsi, DSI_PHY_TST_CTRL0, PHY_TESTCLR);
	dsi_write(dsi, DSI_PHY_TST_CTRL0, 0);

	dw_mipi_dsi_phy_write(dsi, 0x10, BYPASS_VCO_RANGE |
					 VCO_RANGE_CON_SEL(vco) |
					 VCO_IN_CAP_CON_LOW |
					 REF_BIAS_CUR_SEL);

	dw_mipi_dsi_phy_write(dsi, 0x11, CP_CURRENT_3MA);
	dw_mipi_dsi_phy_write(dsi, 0x12, CP_PROGRAM_EN | LPF_PROGRAM_EN |
					 LPF_RESISTORS_20_KOHM);

	dw_mipi_dsi_phy_write(dsi, 0x44, HSFREQRANGE_SEL(testdin));

	dw_mipi_dsi_phy_write(dsi, 0x17, INPUT_DIVIDER(dphy->input_div));
	dw_mipi_dsi_phy_write(dsi, 0x18, LOOP_DIV_LOW_SEL(dphy->feedback_div) |
					 LOW_PROGRAM_EN);
	dw_mipi_dsi_phy_write(dsi, 0x18, LOOP_DIV_HIGH_SEL(dphy->feedback_div) |
					 HIGH_PROGRAM_EN);
	dw_mipi_dsi_phy_write(dsi, 0x19, PLL_LOOP_DIV_EN | PLL_INPUT_DIV_EN);

	dw_mipi_dsi_phy_write(dsi, 0x22, LOW_PROGRAM_EN |
					 BIASEXTR_SEL(BIASEXTR_127_7));
	dw_mipi_dsi_phy_write(dsi, 0x22, HIGH_PROGRAM_EN |
					 BANDGAP_SEL(BANDGAP_96_10));

	dw_mipi_dsi_phy_write(dsi, 0x20, POWER_CONTROL | INTERNAL_REG_CURRENT |
					 BIAS_BLOCK_ON | BANDGAP_ON);

	dw_mipi_dsi_phy_write(dsi, 0x21, TER_RESISTOR_LOW | TER_CAL_DONE |
					 SETRD_MAX | TER_RESISTORS_ON);
	dw_mipi_dsi_phy_write(dsi, 0x21, TER_RESISTOR_HIGH | LEVEL_SHIFTERS_ON |
					 SETRD_MAX | POWER_MANAGE |
					 TER_RESISTORS_ON);

	dw_mipi_dsi_phy_write(dsi, 0x60, TLP_PROGRAM_EN | ns2bc(dsi, 500));
	dw_mipi_dsi_phy_write(dsi, 0x61, THS_PRE_PROGRAM_EN | ns2ui(dsi, 40));
	dw_mipi_dsi_phy_write(dsi, 0x62, THS_ZERO_PROGRAM_EN | ns2bc(dsi, 300));
	dw_mipi_dsi_phy_write(dsi, 0x63, THS_PRE_PROGRAM_EN | ns2ui(dsi, 100));
	dw_mipi_dsi_phy_write(dsi, 0x64, BIT(5) | ns2bc(dsi, 100));
	dw_mipi_dsi_phy_write(dsi, 0x65, BIT(5) | (ns2bc(dsi, 60) + 7));

	dw_mipi_dsi_phy_write(dsi, 0x70, TLP_PROGRAM_EN | ns2bc(dsi, 500));
	dw_mipi_dsi_phy_write(dsi, 0x71,
			      THS_PRE_PROGRAM_EN | (ns2ui(dsi, 50) + 5));
	dw_mipi_dsi_phy_write(dsi, 0x72,
			      THS_ZERO_PROGRAM_EN | (ns2bc(dsi, 140) + 2));
	dw_mipi_dsi_phy_write(dsi, 0x73,
			      THS_PRE_PROGRAM_EN | (ns2ui(dsi, 60) + 8));
	dw_mipi_dsi_phy_write(dsi, 0x74, BIT(5) | ns2bc(dsi, 100));

	dsi_write(dsi, DSI_PHY_RSTZ, PHY_FORCEPLL | PHY_ENABLECLK |
		  PHY_RSTZ | PHY_SHUTDOWNZ);

	ret = readl_poll_timeout(dsi->base + DSI_PHY_STATUS,
				 val, val & PHY_LOCK, 1000,
				 PHY_STATUS_TIMEOUT_US);
	if (ret < 0) {
		DRM_DEV_ERROR(dsi->dev, "failed to wait for phy lock state\n");
		return ret;
	}

	ret = readl_poll_timeout(dsi->base + DSI_PHY_STATUS,
				 val, val & PHY_STOPSTATECLKLANE, 1000,
				 PHY_STATUS_TIMEOUT_US);
	if (ret < 0)
		DRM_DEV_ERROR(dsi->dev,
			      "failed to wait for phy clk lane stop state\n");

	return ret;
}

static int mipi_dphy_power_on(struct dw_mipi_dsi *dsi)
{
	struct mipi_dphy *dphy = &dsi->dphy;

	clk_prepare_enable(dphy->ref_clk);
	clk_prepare_enable(dphy->cfg_clk);

	dw_mipi_dsi_phy_init(dsi);

	return 0;
}

static void mipi_dphy_power_off(struct dw_mipi_dsi *dsi)
{
	struct mipi_dphy *dphy = &dsi->dphy;

	clk_disable_unprepare(dphy->cfg_clk);
	clk_disable_unprepare(dphy->ref_clk);
}

static int mipi_dphy_attach(struct dw_mipi_dsi *dsi)
{
	struct mipi_dphy *dphy = &dsi->dphy;
	struct device *dev = dsi->dev;
	int ret;

	dphy->ref_clk = devm_clk_get(dev, "ref");
	if (IS_ERR(dphy->ref_clk)) {
		ret = PTR_ERR(dphy->ref_clk);
		DRM_DEV_ERROR(dev,
			      "Unable to get pll reference clock: %d\n", ret);
		return ret;
	}

	if (dsi->pdata->flags & DW_MIPI_NEEDS_PHY_CFG_CLK) {
		dphy->cfg_clk = devm_clk_get(dev, "phy_cfg");
		if (IS_ERR(dphy->cfg_clk)) {
			ret = PTR_ERR(dphy->cfg_clk);
			DRM_DEV_ERROR(dev,
				      "Unable to get phy_cfg_clk: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int dw_mipi_dsi_get_lane_bps(struct dw_mipi_dsi *dsi,
				    struct drm_display_mode *mode)
{
	struct mipi_dphy *dphy = &dsi->dphy;
	unsigned int i, pre;
	unsigned long mpclk, pllref, tmp;
	unsigned int m = 1, n = 1, target_mbps = 1000;
	unsigned int max_mbps = dptdin_map[ARRAY_SIZE(dptdin_map) - 1].max_mbps;
	int bpp;

	bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
	if (bpp < 0) {
		DRM_DEV_ERROR(dsi->dev,
			      "failed to get bpp for pixel format %d\n",
			      dsi->format);
		return bpp;
	}

	mpclk = DIV_ROUND_UP(mode->clock, MSEC_PER_SEC);
	if (mpclk) {
		/* take 1 / 0.8, since mbps must big than bandwidth of RGB */
		tmp = mpclk * (bpp / dsi->lanes) * 10 / 8;
		if (tmp < max_mbps)
			target_mbps = tmp;
		else
			DRM_DEV_ERROR(dsi->dev,
				      "DPHY clock frequency is out of range\n");
	}

	pllref = DIV_ROUND_UP(clk_get_rate(dphy->ref_clk), USEC_PER_SEC);
	tmp = pllref;

	/*
	 * The limits on the PLL divisor are:
	 *
	 *	5MHz <= (pllref / n) <= 40MHz
	 *
	 * we walk over these values in descreasing order so that if we hit
	 * an exact match for target_mbps it is more likely that "m" will be
	 * even.
	 *
	 * TODO: ensure that "m" is even after this loop.
	 */
	for (i = pllref / 5; i > (pllref / 40); i--) {
		pre = pllref / i;
		if ((tmp > (target_mbps % pre)) && (target_mbps / pre < 512)) {
			tmp = target_mbps % pre;
			n = i;
			m = target_mbps / pre;
		}
		if (tmp == 0)
			break;
	}

	dsi->lane_mbps = pllref / n * m;
	dphy->input_div = n;
	dphy->feedback_div = m;

	return 0;
}

static int dw_mipi_dsi_host_attach(struct mipi_dsi_host *host,
				   struct mipi_dsi_device *device)
{
	struct dw_mipi_dsi *dsi = host_to_dsi(host);

	if (device->lanes > dsi->pdata->max_data_lanes) {
		DRM_DEV_ERROR(dsi->dev,
			      "the number of data lanes(%u) is too many\n",
			      device->lanes);
		return -EINVAL;
	}

	dsi->lanes = device->lanes;
	dsi->channel = device->channel;
	dsi->format = device->format;
	dsi->mode_flags = device->mode_flags;
	dsi->panel = of_drm_find_panel(device->dev.of_node);
	if (!IS_ERR(dsi->panel))
		return drm_panel_attach(dsi->panel, &dsi->connector);

	return -EINVAL;
}

static int dw_mipi_dsi_host_detach(struct mipi_dsi_host *host,
				   struct mipi_dsi_device *device)
{
	struct dw_mipi_dsi *dsi = host_to_dsi(host);

	drm_panel_detach(dsi->panel);

	return 0;
}

static void dw_mipi_message_config(struct dw_mipi_dsi *dsi,
				   const struct mipi_dsi_msg *msg)
{
	bool lpm = msg->flags & MIPI_DSI_MSG_USE_LPM;
	u32 val = 0;

	if (msg->flags & MIPI_DSI_MSG_REQ_ACK)
		val |= ACK_RQST_EN;

	if (lpm)
		val |= MAX_RD_PKT_SIZE | DCS_LW_TX | DCS_SR_0P_TX |
		       DCS_SW_1P_TX | DCS_SW_0P_TX | GEN_LW_TX |
		       GEN_SR_2P_TX | GEN_SR_1P_TX | GEN_SR_0P_TX |
		       GEN_SW_2P_TX | GEN_SW_1P_TX | GEN_SW_0P_TX;

	dsi_write(dsi, DSI_LPCLK_CTRL, lpm ? 0 : PHY_TXREQUESTCLKHS);
	dsi_write(dsi, DSI_CMD_MODE_CFG, val);
}

static int dw_mipi_dsi_gen_pkt_hdr_write(struct dw_mipi_dsi *dsi, u32 hdr_val)
{
	int ret;
	u32 val, mask;

	ret = readl_poll_timeout(dsi->base + DSI_CMD_PKT_STATUS,
				 val, !(val & GEN_CMD_FULL), 1000,
				 CMD_PKT_STATUS_TIMEOUT_US);
	if (ret < 0) {
		DRM_DEV_ERROR(dsi->dev,
			      "failed to get available command FIFO\n");
		return ret;
	}

	dsi_write(dsi, DSI_GEN_HDR, hdr_val);

	mask = GEN_CMD_EMPTY | GEN_PLD_W_EMPTY;
	ret = readl_poll_timeout(dsi->base + DSI_CMD_PKT_STATUS,
				 val, (val & mask) == mask,
				 1000, CMD_PKT_STATUS_TIMEOUT_US);
	if (ret < 0) {
		DRM_DEV_ERROR(dsi->dev, "failed to write command FIFO\n");
		return ret;
	}

	return 0;
}

static int dw_mipi_dsi_dcs_short_write(struct dw_mipi_dsi *dsi,
				       const struct mipi_dsi_msg *msg)
{
	const u8 *tx_buf = msg->tx_buf;
	u8 gen_wc_msbyte = 0, gen_wc_lsbyte = 0;
	u32 val;

	if (msg->tx_len > 0)
		gen_wc_lsbyte = tx_buf[0];
	if (msg->tx_len > 1)
		gen_wc_msbyte = tx_buf[1] << 8;

	if (msg->tx_len > 2) {
		DRM_DEV_ERROR(dsi->dev,
			      "too long tx buf length %zu for short write\n",
			      msg->tx_len);
		return -EINVAL;
	}

	val = GEN_WC_MSBYTE(gen_wc_msbyte) | GEN_WC_LSBYTE(gen_wc_lsbyte) |
	      GEN_VC(dsi->channel) | GEN_DT(msg->type);

	return dw_mipi_dsi_gen_pkt_hdr_write(dsi, val);
}

static int dw_mipi_dsi_dcs_long_write(struct dw_mipi_dsi *dsi,
				      const struct mipi_dsi_msg *msg)
{
	const u8 *tx_buf = msg->tx_buf;
	int len = msg->tx_len, pld_data_bytes = sizeof(u32), ret;
	u32 hdr_val;
	u32 remainder;
	u32 val;

	hdr_val = GEN_WC_MSBYTE((msg->tx_len >> 8) & 0xff) |
		  GEN_WC_LSBYTE(msg->tx_len & 0xff) | GEN_VC(dsi->channel) |
		  GEN_DT(msg->type);

	if (msg->tx_len < 3) {
		DRM_DEV_ERROR(dsi->dev,
			      "wrong tx buf length %zu for long write\n",
			      msg->tx_len);
		return -EINVAL;
	}

	while (DIV_ROUND_UP(len, pld_data_bytes)) {
		if (len < pld_data_bytes) {
			remainder = 0;
			memcpy(&remainder, tx_buf, len);
			dsi_write(dsi, DSI_GEN_PLD_DATA, remainder);
			len = 0;
		} else {
			memcpy(&remainder, tx_buf, pld_data_bytes);
			dsi_write(dsi, DSI_GEN_PLD_DATA, remainder);
			tx_buf += pld_data_bytes;
			len -= pld_data_bytes;
		}

		ret = readl_poll_timeout(dsi->base + DSI_CMD_PKT_STATUS,
					 val, !(val & GEN_PLD_W_FULL), 1000,
					 CMD_PKT_STATUS_TIMEOUT_US);
		if (ret < 0) {
			DRM_DEV_ERROR(dsi->dev,
				      "failed to get available write payload FIFO\n");
			return ret;
		}
	}

	return dw_mipi_dsi_gen_pkt_hdr_write(dsi, hdr_val);
}

static ssize_t dw_mipi_dsi_host_transfer(struct mipi_dsi_host *host,
					 const struct mipi_dsi_msg *msg)
{
	struct dw_mipi_dsi *dsi = host_to_dsi(host);
	int ret;

	dw_mipi_message_config(dsi, msg);

	switch (msg->type) {
	case MIPI_DSI_DCS_SHORT_WRITE:
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
	case MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE:
		ret = dw_mipi_dsi_dcs_short_write(dsi, msg);
		break;
	case MIPI_DSI_DCS_LONG_WRITE:
		ret = dw_mipi_dsi_dcs_long_write(dsi, msg);
		break;
	default:
		DRM_DEV_ERROR(dsi->dev, "unsupported message type 0x%02x\n",
			      msg->type);
		ret = -EINVAL;
	}

	return ret;
}

static const struct mipi_dsi_host_ops dw_mipi_dsi_host_ops = {
	.attach = dw_mipi_dsi_host_attach,
	.detach = dw_mipi_dsi_host_detach,
	.transfer = dw_mipi_dsi_host_transfer,
};

static void dw_mipi_dsi_video_mode_config(struct dw_mipi_dsi *dsi)
{
	u32 val;

	val = LP_HFP_EN | LP_HBP_EN | LP_VACT_EN | LP_VFP_EN | LP_VBP_EN |
	      LP_VSA_EN;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		val |= VID_MODE_TYPE_BURST;
	else if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		val |= VID_MODE_TYPE_NON_BURST_SYNC_PULSES;
	else
		val |= VID_MODE_TYPE_NON_BURST_SYNC_EVENTS;

	dsi_write(dsi, DSI_VID_MODE_CFG, val);
}

static void dw_mipi_dsi_set_vid_mode(struct dw_mipi_dsi *dsi)
{
	dsi_write(dsi, DSI_PWR_UP, RESET);
	dsi_write(dsi, DSI_MODE_CFG, CMD_VIDEO_MODE(VIDEO_MODE));
	dw_mipi_dsi_video_mode_config(dsi);
	dsi_write(dsi, DSI_LPCLK_CTRL, PHY_TXREQUESTCLKHS);
	dsi_write(dsi, DSI_PWR_UP, POWER_UP);
}

static void dw_mipi_dsi_set_cmd_mode(struct dw_mipi_dsi *dsi)
{
	dsi_write(dsi, DSI_PWR_UP, RESET);
	dsi_write(dsi, DSI_MODE_CFG, CMD_VIDEO_MODE(COMMAND_MODE));
	dsi_write(dsi, DSI_PWR_UP, POWER_UP);
}

static void dw_mipi_dsi_disable(struct dw_mipi_dsi *dsi)
{
	dsi_write(dsi, DSI_PWR_UP, RESET);
	dsi_write(dsi, DSI_PHY_RSTZ, 0);
}

static void dw_mipi_dsi_init(struct dw_mipi_dsi *dsi)
{
	/*
	 * The maximum permitted escape clock is 20MHz and it is derived from
	 * lanebyteclk, which is running at "lane_mbps / 8".  Thus we want:
	 *
	 *     (lane_mbps >> 3) / esc_clk_division < 20
	 * which is:
	 *     (lane_mbps >> 3) / 20 > esc_clk_division
	 */
	u32 esc_clk_division = (dsi->lane_mbps >> 3) / 20 + 1;

	dsi_write(dsi, DSI_PWR_UP, RESET);
	dsi_write(dsi, DSI_PHY_RSTZ, 0);
	dsi_write(dsi, DSI_CLKMGR_CFG, TO_CLK_DIVISION(10) |
		  TX_ESC_CLK_DIVISION(esc_clk_division));
}

static void dw_mipi_dsi_dpi_config(struct dw_mipi_dsi *dsi,
				   struct drm_display_mode *mode)
{
	u32 val = 0, color = 0;

	switch (dsi->format) {
	case MIPI_DSI_FMT_RGB666:
		color = DPI_COLOR_CODING(DPI_COLOR_CODING_18BIT_2) |
			LOOSELY18_EN;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		color = DPI_COLOR_CODING(DPI_COLOR_CODING_18BIT_1);
		break;
	case MIPI_DSI_FMT_RGB565:
		color = DPI_COLOR_CODING(DPI_COLOR_CODING_16BIT_1);
		break;
	case MIPI_DSI_FMT_RGB888:
	default:
		color = DPI_COLOR_CODING(DPI_COLOR_CODING_24BIT);
		break;
	}

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		val |= VSYNC_ACTIVE_LOW;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		val |= HSYNC_ACTIVE_LOW;

	dsi_write(dsi, DSI_DPI_VCID, DPI_VID(dsi->channel));
	dsi_write(dsi, DSI_DPI_COLOR_CODING, color);
	dsi_write(dsi, DSI_DPI_CFG_POL, val);
	dsi_write(dsi, DSI_DPI_LP_CMD_TIM, OUTVACT_LPCMD_TIME(4) |
		  INVACT_LPCMD_TIME(4));
}

static void dw_mipi_dsi_packet_handler_config(struct dw_mipi_dsi *dsi)
{
	dsi_write(dsi, DSI_PCKHDL_CFG, CRC_RX_EN | ECC_RX_EN | BTA_EN);
}

static void dw_mipi_dsi_video_packet_config(struct dw_mipi_dsi *dsi,
					    struct drm_display_mode *mode)
{
	dsi_write(dsi, DSI_VID_PKT_SIZE, VID_PKT_SIZE(mode->hdisplay));
}

static void dw_mipi_dsi_command_mode_config(struct dw_mipi_dsi *dsi)
{
	dsi_write(dsi, DSI_TO_CNT_CFG, HSTX_TO_CNT(1000) | LPRX_TO_CNT(1000));
	dsi_write(dsi, DSI_BTA_TO_CNT, 0xd00);
	dsi_write(dsi, DSI_MODE_CFG, CMD_VIDEO_MODE(COMMAND_MODE));
}

/* Get lane byte clock cycles. */
static u32 dw_mipi_dsi_get_hcomponent_lbcc(struct dw_mipi_dsi *dsi,
					   struct drm_display_mode *mode,
					   u32 hcomponent)
{
	u32 frac, lbcc;

	lbcc = hcomponent * dsi->lane_mbps * MSEC_PER_SEC / 8;

	frac = lbcc % mode->clock;
	lbcc = lbcc / mode->clock;
	if (frac)
		lbcc++;

	return lbcc;
}

static void dw_mipi_dsi_line_timer_config(struct dw_mipi_dsi *dsi,
					  struct drm_display_mode *mode)
{
	u32 htotal, hsa, hbp, lbcc;

	htotal = mode->htotal;
	hsa = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;

	lbcc = dw_mipi_dsi_get_hcomponent_lbcc(dsi, mode, htotal);
	dsi_write(dsi, DSI_VID_HLINE_TIME, lbcc);

	lbcc = dw_mipi_dsi_get_hcomponent_lbcc(dsi, mode, hsa);
	dsi_write(dsi, DSI_VID_HSA_TIME, lbcc);

	lbcc = dw_mipi_dsi_get_hcomponent_lbcc(dsi, mode, hbp);
	dsi_write(dsi, DSI_VID_HBP_TIME, lbcc);
}

static void dw_mipi_dsi_vertical_timing_config(struct dw_mipi_dsi *dsi,
					       struct drm_display_mode *mode)
{
	u32 vactive, vsa, vfp, vbp;

	vactive = mode->vdisplay;
	vsa = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;

	dsi_write(dsi, DSI_VID_VACTIVE_LINES, vactive);
	dsi_write(dsi, DSI_VID_VSA_LINES, vsa);
	dsi_write(dsi, DSI_VID_VFP_LINES, vfp);
	dsi_write(dsi, DSI_VID_VBP_LINES, vbp);
}

static void dw_mipi_dsi_dphy_timing_config(struct dw_mipi_dsi *dsi)
{
	dsi_write(dsi, DSI_PHY_TMR_CFG, PHY_HS2LP_TIME(0x40)
		  | PHY_LP2HS_TIME(0x40) | MAX_RD_TIME(10000));

	dsi_write(dsi, DSI_PHY_TMR_LPCLK_CFG, PHY_CLKHS2LP_TIME(0x40)
		  | PHY_CLKLP2HS_TIME(0x40));
}

static void dw_mipi_dsi_dphy_interface_config(struct dw_mipi_dsi *dsi)
{
	dsi_write(dsi, DSI_PHY_IF_CFG, PHY_STOP_WAIT_TIME(0x20) |
		  N_LANES(dsi->lanes - 1));
}

static void dw_mipi_dsi_clear_err(struct dw_mipi_dsi *dsi)
{
	dsi_read(dsi, DSI_INT_ST0);
	dsi_read(dsi, DSI_INT_ST1);
	dsi_write(dsi, DSI_INT_MSK0, 0);
	dsi_write(dsi, DSI_INT_MSK1, 0);
}

static void dw_mipi_dsi_encoder_disable(struct drm_encoder *encoder)
{
	struct dw_mipi_dsi *dsi = encoder_to_dsi(encoder);

	if (dsi->dpms_mode != DRM_MODE_DPMS_ON)
		return;

	if (clk_prepare_enable(dsi->pclk)) {
		DRM_DEV_ERROR(dsi->dev, "Failed to enable pclk\n");
		return;
	}

	drm_panel_disable(dsi->panel);

	dw_mipi_dsi_set_cmd_mode(dsi);
	drm_panel_unprepare(dsi->panel);

	dw_mipi_dsi_disable(dsi);
	mipi_dphy_power_off(dsi);
	pm_runtime_put(dsi->dev);
	clk_disable_unprepare(dsi->pclk);
	dsi->dpms_mode = DRM_MODE_DPMS_OFF;
}

static void dw_mipi_dsi_encoder_enable(struct drm_encoder *encoder)
{
	struct dw_mipi_dsi *dsi = encoder_to_dsi(encoder);
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
	const struct dw_mipi_dsi_plat_data *pdata = dsi->pdata;
	int mux = drm_of_encoder_active_endpoint_id(dsi->dev->of_node, encoder);
	u32 val;
	int ret;

	ret = dw_mipi_dsi_get_lane_bps(dsi, mode);
	if (ret < 0)
		return;

	if (dsi->dpms_mode == DRM_MODE_DPMS_ON)
		return;

	if (clk_prepare_enable(dsi->pclk)) {
		DRM_DEV_ERROR(dsi->dev, "Failed to enable pclk\n");
		return;
	}

	pm_runtime_get_sync(dsi->dev);
	dw_mipi_dsi_init(dsi);
	dw_mipi_dsi_dpi_config(dsi, mode);
	dw_mipi_dsi_packet_handler_config(dsi);
	dw_mipi_dsi_video_mode_config(dsi);
	dw_mipi_dsi_video_packet_config(dsi, mode);
	dw_mipi_dsi_command_mode_config(dsi);
	dw_mipi_dsi_line_timer_config(dsi, mode);
	dw_mipi_dsi_vertical_timing_config(dsi, mode);
	dw_mipi_dsi_dphy_timing_config(dsi);
	dw_mipi_dsi_dphy_interface_config(dsi);
	dw_mipi_dsi_clear_err(dsi);

	if (pdata->grf_dsi0_mode_reg)
		regmap_write(dsi->grf_regmap, pdata->grf_dsi0_mode_reg,
			     pdata->grf_dsi0_mode);

	mipi_dphy_power_on(dsi);
	dw_mipi_dsi_wait_for_two_frames(mode);

	dw_mipi_dsi_set_cmd_mode(dsi);
	if (drm_panel_prepare(dsi->panel))
		DRM_DEV_ERROR(dsi->dev, "failed to prepare panel\n");

	dw_mipi_dsi_set_vid_mode(dsi);
	drm_panel_enable(dsi->panel);

	clk_disable_unprepare(dsi->pclk);

	if (mux)
		val = pdata->dsi0_en_bit | (pdata->dsi0_en_bit << 16);
	else
		val = pdata->dsi0_en_bit << 16;

	regmap_write(dsi->grf_regmap, pdata->grf_switch_reg, val);
	DRM_DEV_DEBUG(dsi->dev,
		      "vop %s output to dsi0\n", (mux) ? "LIT" : "BIG");
	dsi->dpms_mode = DRM_MODE_DPMS_ON;
}

static int
dw_mipi_dsi_encoder_atomic_check(struct drm_encoder *encoder,
				 struct drm_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct dw_mipi_dsi *dsi = encoder_to_dsi(encoder);

	switch (dsi->format) {
	case MIPI_DSI_FMT_RGB888:
		s->output_mode = ROCKCHIP_OUT_MODE_P888;
		break;
	case MIPI_DSI_FMT_RGB666:
		s->output_mode = ROCKCHIP_OUT_MODE_P666;
		break;
	case MIPI_DSI_FMT_RGB565:
		s->output_mode = ROCKCHIP_OUT_MODE_P565;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	s->output_type = DRM_MODE_CONNECTOR_DSI;

	return 0;
}

static const struct drm_encoder_helper_funcs
dw_mipi_dsi_encoder_helper_funcs = {
	.enable = dw_mipi_dsi_encoder_enable,
	.disable = dw_mipi_dsi_encoder_disable,
	.atomic_check = dw_mipi_dsi_encoder_atomic_check,
};

static const struct drm_encoder_funcs dw_mipi_dsi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int dw_mipi_dsi_connector_get_modes(struct drm_connector *connector)
{
	struct dw_mipi_dsi *dsi = con_to_dsi(connector);

	return drm_panel_get_modes(dsi->panel);
}

static struct drm_connector_helper_funcs dw_mipi_dsi_connector_helper_funcs = {
	.get_modes = dw_mipi_dsi_connector_get_modes,
};

static void dw_mipi_dsi_drm_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs dw_mipi_dsi_atomic_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = dw_mipi_dsi_drm_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int dw_mipi_dsi_register(struct drm_device *drm,
				struct dw_mipi_dsi *dsi)
{
	struct drm_encoder *encoder = &dsi->encoder;
	struct drm_connector *connector = &dsi->connector;
	struct device *dev = dsi->dev;
	int ret;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm,
							     dev->of_node);
	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	drm_encoder_helper_add(&dsi->encoder,
			       &dw_mipi_dsi_encoder_helper_funcs);
	ret = drm_encoder_init(drm, &dsi->encoder, &dw_mipi_dsi_encoder_funcs,
			       DRM_MODE_ENCODER_DSI, NULL);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to initialize encoder with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector,
				 &dw_mipi_dsi_connector_helper_funcs);

	drm_connector_init(drm, &dsi->connector,
			   &dw_mipi_dsi_atomic_connector_funcs,
			   DRM_MODE_CONNECTOR_DSI);

	drm_connector_attach_encoder(connector, encoder);

	return 0;
}

static int dw_mipi_dsi_parse_dt(struct dw_mipi_dsi *dsi)
{
	struct device_node *np = dsi->dev->of_node;

	dsi->grf_regmap = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(dsi->grf_regmap)) {
		DRM_DEV_ERROR(dsi->dev, "Unable to get rockchip,grf\n");
		return PTR_ERR(dsi->grf_regmap);
	}

	return 0;
}

static int dw_mipi_dsi_bind(struct device *dev, struct device *master,
			    void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct reset_control *apb_rst;
	struct drm_device *drm = data;
	struct dw_mipi_dsi *dsi;
	struct resource *res;
	int ret;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->dev = dev;
	dsi->pdata = of_device_get_match_data(dev);
	dsi->dpms_mode = DRM_MODE_DPMS_OFF;

	ret = dw_mipi_dsi_parse_dt(dsi);
	if (ret)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dsi->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(dsi->base))
		return PTR_ERR(dsi->base);

	ret = mipi_dphy_attach(dsi);
	if (ret)
		return ret;

	dsi->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dsi->pclk)) {
		ret = PTR_ERR(dsi->pclk);
		DRM_DEV_ERROR(dev, "Unable to get pclk: %d\n", ret);
		return ret;
	}

	/*
	 * Note that the reset was not defined in the initial device tree, so
	 * we have to be prepared for it not being found.
	 */
	apb_rst = devm_reset_control_get(dev, "apb");
	if (IS_ERR(apb_rst)) {
		ret = PTR_ERR(apb_rst);
		if (ret == -ENOENT) {
			apb_rst = NULL;
		} else {
			DRM_DEV_ERROR(dev,
				      "Unable to get reset control: %d\n", ret);
			return ret;
		}
	}

	if (apb_rst) {
		ret = clk_prepare_enable(dsi->pclk);
		if (ret) {
			DRM_DEV_ERROR(dev, "Failed to enable pclk\n");
			return ret;
		}

		reset_control_assert(apb_rst);
		usleep_range(10, 20);
		reset_control_deassert(apb_rst);

		clk_disable_unprepare(dsi->pclk);
	}

	ret = dw_mipi_dsi_register(drm, dsi);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to register mipi_dsi: %d\n", ret);
		return ret;
	}

	dsi->host.ops = &dw_mipi_dsi_host_ops;
	dsi->host.dev = dev;
	ret = mipi_dsi_host_register(&dsi->host);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to register MIPI host: %d\n", ret);
		goto err_cleanup;
	}

	if (!dsi->panel) {
		ret = -EPROBE_DEFER;
		goto err_mipi_dsi_host;
	}

	dev_set_drvdata(dev, dsi);
	pm_runtime_enable(dev);

	return 0;

err_mipi_dsi_host:
	mipi_dsi_host_unregister(&dsi->host);
err_cleanup:
	dsi->connector.funcs->destroy(&dsi->connector);
	dsi->encoder.funcs->destroy(&dsi->encoder);
	return ret;
}

static void dw_mipi_dsi_unbind(struct device *dev, struct device *master,
			       void *data)
{
	struct dw_mipi_dsi *dsi = dev_get_drvdata(dev);

	mipi_dsi_host_unregister(&dsi->host);
	pm_runtime_disable(dev);

	dsi->connector.funcs->destroy(&dsi->connector);
	dsi->encoder.funcs->destroy(&dsi->encoder);
}

static const struct component_ops dw_mipi_dsi_ops = {
	.bind	= dw_mipi_dsi_bind,
	.unbind	= dw_mipi_dsi_unbind,
};

static int dw_mipi_dsi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &dw_mipi_dsi_ops);
}

static int dw_mipi_dsi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dw_mipi_dsi_ops);

	return 0;
}

static struct dw_mipi_dsi_plat_data rk3288_mipi_dsi_plat_data = {
	.dsi0_en_bit = RK3288_DSI0_SEL_VOP_LIT,
	.dsi1_en_bit = RK3288_DSI1_SEL_VOP_LIT,
	.grf_switch_reg = RK3288_GRF_SOC_CON6,
	.max_data_lanes = 4,
};

static struct dw_mipi_dsi_plat_data rk3399_mipi_dsi_plat_data = {
	.dsi0_en_bit = RK3399_DSI0_SEL_VOP_LIT,
	.dsi1_en_bit = RK3399_DSI1_SEL_VOP_LIT,
	.grf_switch_reg = RK3399_GRF_SOC_CON20,
	.grf_dsi0_mode = RK3399_GRF_DSI_MODE,
	.grf_dsi0_mode_reg = RK3399_GRF_SOC_CON22,
	.flags = DW_MIPI_NEEDS_PHY_CFG_CLK,
	.max_data_lanes = 4,
};

static const struct of_device_id dw_mipi_dsi_dt_ids[] = {
	{
		.compatible = "rockchip,rk3288-mipi-dsi",
		.data = &rk3288_mipi_dsi_plat_data,
	}, {
		.compatible = "rockchip,rk3399-mipi-dsi",
		.data = &rk3399_mipi_dsi_plat_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, dw_mipi_dsi_dt_ids);

struct platform_driver dw_mipi_dsi_driver = {
	.probe	= dw_mipi_dsi_probe,
	.remove = dw_mipi_dsi_remove,
	.driver = {
		.of_match_table = dw_mipi_dsi_dt_ids,
		.name = "dw-mipi-dsi",
	},
};
