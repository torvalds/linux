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
#include <linux/phy/phy.h>
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
#include <uapi/linux/videodev2.h>
#include <video/mipi_display.h>
#include <asm/unaligned.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define DRIVER_NAME    "dw-mipi-dsi"

#define IS_DSI0(dsi)	((dsi)->id == 0)
#define IS_DSI1(dsi)	((dsi)->id == 1)

#define UPDATE(v, h, l)	(((v) << (l)) & GENMASK((h), (l)))

#define DSI_VERSION			0x00
#define DSI_PWR_UP			0x04
#define RESET				0
#define POWERUP				BIT(0)

#define DSI_CLKMGR_CFG			0x08
#define TO_CLK_DIVIDSION(div)		(((div) & 0xff) << 8)
#define TX_ESC_CLK_DIVIDSION(div)	(((div) & 0xff) << 0)

#define DSI_DPI_VCID			0x0c
#define DPI_VID(vid)			(((vid) & 0x3) << 0)

#define DSI_DPI_COLOR_CODING		0x10
#define EN18_LOOSELY			BIT(8)
#define DPI_COLOR_CODING_16BIT_1	0x0
#define DPI_COLOR_CODING_16BIT_2	0x1
#define DPI_COLOR_CODING_16BIT_3	0x2
#define DPI_COLOR_CODING_18BIT_1	0x3
#define DPI_COLOR_CODING_18BIT_2	0x4
#define DPI_COLOR_CODING_24BIT		0x5

#define DSI_DPI_CFG_POL			0x14
#define COLORM_ACTIVE_LOW		BIT(4)
#define SHUTD_ACTIVE_LOW		BIT(3)
#define HSYNC_ACTIVE_LOW		BIT(2)
#define VSYNC_ACTIVE_LOW		BIT(1)
#define DATAEN_ACTIVE_LOW		BIT(0)

#define DSI_DPI_LP_CMD_TIM		0x18
#define OUTVACT_LPCMD_TIME(p)		(((p) & 0xff) << 16)
#define INVACT_LPCMD_TIME(p)		((p) & 0xff)

#define DSI_DBI_VCID			0x1c
#define DBI_VCID(x)			UPDATE(x, 1, 0)
#define DSI_DBI_CFG			0x20
#define DSI_DBI_CMDSIZE			0x28
#define DSI_PCKHDL_CFG			0x2c
#define CRC_RX_EN			BIT(4)
#define ECC_RX_EN			BIT(3)
#define BTA_EN				BIT(2)
#define EOTP_RX_EN			BIT(1)
#define EOTP_TX_EN			BIT(0)

#define DSI_MODE_CFG			0x34
#define CMD_VIDEO_MODE			BIT(0)
#define COMMAND_MODE			BIT(0)
#define VIDEO_MODE			0
#define DSI_VID_MODE_CFG		0x38
#define VPG_EN				BIT(16)
#define LP_CMD_EN			BIT(15)
#define FRAME_BTA_ACK			BIT(14)
#define LP_HFP_EN			BIT(13)
#define LP_HBP_EN			BIT(12)
#define LP_VACT_EN			BIT(11)
#define LP_VFP_EN			BIT(10)
#define LP_VBP_EN			BIT(9)
#define LP_VSA_EN			BIT(8)
#define VID_MODE_TYPE_BURST_SYNC_PULSES	0x0
#define VID_MODE_TYPE_BURST_SYNC_EVENTS	0x1
#define VID_MODE_TYPE_BURST		0x2

#define DSI_VID_PKT_SIZE		0x3c
#define VID_PKT_SIZE(p)			(((p) & 0x3fff) << 0)
#define VID_PKT_MAX_SIZE		0x3fff

#define DSI_VID_NUM_CHUMKS		0x40
#define DSI_VID_NULL_PKT_SIZE		0x44
#define DSI_VID_HSA_TIME		0x48
#define DSI_VID_HBP_TIME		0x4c
#define DSI_VID_HLINE_TIME		0x50
#define DSI_VID_VSA_LINES		0x54
#define DSI_VID_VBP_LINES		0x58
#define DSI_VID_VFP_LINES		0x5c
#define DSI_VID_VACTIVE_LINES		0x60
#define DSI_EDPI_CMD_SIZE		0x64
#define DSI_CMD_MODE_CFG		0x68
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
#define DSI_GEN_HDR			0x6c
#define GEN_HDATA(data)			(((data) & 0xffff) << 8)
#define GEN_HDATA_MASK			(0xffff << 8)
#define GEN_HTYPE(type)			(((type) & 0xff) << 0)
#define GEN_HTYPE_MASK			0xff

#define DSI_GEN_PLD_DATA		0x70

#define DSI_CMD_PKT_STATUS		0x74
#define GEN_CMD_EMPTY			BIT(0)
#define GEN_CMD_FULL			BIT(1)
#define GEN_PLD_W_EMPTY			BIT(2)
#define GEN_PLD_W_FULL			BIT(3)
#define GEN_PLD_R_EMPTY			BIT(4)
#define GEN_PLD_R_FULL			BIT(5)
#define GEN_RD_CMD_BUSY			BIT(6)

#define DSI_TO_CNT_CFG			0x78
#define HSTX_TO_CNT(p)			(((p) & 0xffff) << 16)
#define LPRX_TO_CNT(p)			((p) & 0xffff)

#define DSI_BTA_TO_CNT			0x8c
#define DSI_LPCLK_CTRL			0x94
#define AUTO_CLKLANE_CTRL		BIT(1)
#define PHY_TXREQUESTCLKHS		BIT(0)

#define DSI_PHY_TMR_LPCLK_CFG		0x98
#define PHY_CLKHS2LP_TIME(lbcc)		(((lbcc) & 0x3ff) << 16)
#define PHY_CLKLP2HS_TIME(lbcc)		((lbcc) & 0x3ff)

#define DSI_PHY_TMR_CFG			0x9c
#define PHY_HS2LP_TIME(lbcc)		(((lbcc) & 0xff) << 24)
#define PHY_LP2HS_TIME(lbcc)		(((lbcc) & 0xff) << 16)
#define MAX_RD_TIME(lbcc)		((lbcc) & 0x7fff)

#define DSI_PHY_RSTZ			0xa0
#define PHY_ENFORCEPLL			BIT(3)
#define PHY_ENABLECLK			BIT(2)
#define PHY_RSTZ			BIT(1)
#define PHY_SHUTDOWNZ			BIT(0)

#define DSI_PHY_IF_CFG			0xa4
#define N_LANES(n)			((((n) - 1) & 0x3) << 0)
#define PHY_STOP_WAIT_TIME(cycle)	(((cycle) & 0xff) << 8)

#define DSI_PHY_STATUS			0xb0
#define PHY_ULPSACTIVENOT3LANE		BIT(12)
#define PHY_STOPSTATE3LANE		BIT(11)
#define PHY_ULPSACTIVENOT2LANE		BIT(10)
#define PHY_STOPSTATE2LANE		BIT(9)
#define PHY_ULPSACTIVENOT1LANE		BIT(8)
#define PHY_STOPSTATE1LANE		BIT(7)
#define PHY_RXULPSESC0LANE		BIT(6)
#define PHY_ULPSACTIVENOT0LANE		BIT(5)
#define PHY_STOPSTATE0LANE		BIT(4)
#define PHY_ULPSACTIVENOTCLK		BIT(3)
#define PHY_STOPSTATECLKLANE		BIT(2)
#define PHY_DIRECTION			BIT(1)
#define PHY_LOCK			BIT(0)
#define PHY_STOPSTATELANE		(PHY_STOPSTATE0LANE | \
					 PHY_STOPSTATECLKLANE)

#define DSI_PHY_TST_CTRL0		0xb4
#define PHY_TESTCLK			BIT(1)
#define PHY_TESTCLR			BIT(0)
#define DSI_PHY_TST_CTRL1		0xb8
#define PHY_TESTEN			BIT(16)
#define PHY_TESTDOUT_SHIFT		8
#define PHY_TESTDIN_MASK		GENMASK(7, 0)
#define PHY_TESTDIN(v)			UPDATE(v, 7, 0)
#define DSI_INT_ST0			0xbc
#define DSI_INT_ST1			0xc0
#define DPI_PLD_WR_ERR			BIT(7)
#define DSI_INT_MSK0			0xc4
#define DSI_INT_MSK1			0xc8
#define DSI_MAX_REGISGER		DSI_INT_MSK1

#define PHY_STATUS_TIMEOUT_US		10000
#define CMD_PKT_STATUS_TIMEOUT_US	20000

/* Test Code: 0x44 (HS RX Control of Lane 0) */
#define HSFREQRANGE(x)			UPDATE(x, 6, 1)
/* Test Code: 0x17 (PLL Input Divider Ratio) */
#define INPUT_DIV(x)			UPDATE(x, 6, 0)
/* Test Code: 0x18 (PLL Loop Divider Ratio) */
#define FEEDBACK_DIV_LO(x)		UPDATE(x, 4, 0)
#define FEEDBACK_DIV_HI(x)		(BIT(7) | UPDATE(x, 3, 0))
/* Test Code: 0x19 (PLL Input and Loop Divider Ratios Control) */
#define FEEDBACK_DIV_DEF_VAL_BYPASS	BIT(5)
#define INPUT_DIV_DEF_VAL_BYPASS	BIT(4)

enum soc_type {
	PX30,
	RK1808,
	RK3126,
	RK3288,
	RK3366,
	RK3368,
	RK3399,
};

#define GRF_REG_FIELD(reg, lsb, msb)	((reg << 16) | (lsb << 8) | (msb))

enum grf_reg_fields {
	DPIUPDATECFG,
	DPISHUTDN,
	DPICOLORM,
	VOPSEL,
	TURNREQUEST,
	TURNDISABLE,
	FORCETXSTOPMODE,
	FORCERXMODE,
	ENABLE_N,
	MASTERSLAVEZ,
	ENABLECLK,
	BASEDIR,
	MAX_FIELDS,
};

struct dw_mipi_dsi_plat_data {
	const u32 *dsi0_grf_reg_fields;
	const u32 *dsi1_grf_reg_fields;
	unsigned long max_bit_rate_per_lane;
	enum soc_type soc_type;
};

struct mipi_dphy {
	/* SNPS PHY */
	struct regmap *regmap;
	struct clk *cfg_clk;
	struct clk *ref_clk;
	u16 input_div;
	u16 feedback_div;

	/* Non-SNPS PHY */
	struct phy *phy;
	struct clk *hs_clk;
};

struct dw_mipi_dsi {
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct drm_bridge *bridge;
	struct device_node *client;
	struct mipi_dsi_host dsi_host;
	struct mipi_dphy dphy;
	struct drm_panel *panel;
	struct device *dev;
	struct regmap *grf;
	struct reset_control *rst;
	struct regmap *regmap;
	struct clk *pclk;
	struct clk *h2p_clk;
	int irq;

	/* dual-channel */
	struct dw_mipi_dsi *master;
	struct dw_mipi_dsi *slave;
	struct device_node *panel_node;
	int id;

	unsigned long mode_flags;
	unsigned int lane_mbps; /* per lane */
	u32 channel;
	u32 lanes;
	u32 format;
	struct drm_display_mode mode;

	const struct dw_mipi_dsi_plat_data *pdata;
};

static void grf_field_write(struct dw_mipi_dsi *dsi, enum grf_reg_fields index,
			    unsigned int val)
{
	const u32 field = dsi->id ?
			  dsi->pdata->dsi1_grf_reg_fields[index] :
			  dsi->pdata->dsi0_grf_reg_fields[index];
	u16 reg;
	u8 msb, lsb;

	if (!field)
		return;

	reg = (field >> 16) & 0xffff;
	lsb = (field >>  8) & 0xff;
	msb = (field >>  0) & 0xff;

	regmap_write(dsi->grf, reg, (val << lsb) | (GENMASK(msb, lsb) << 16));
}

static inline struct dw_mipi_dsi *host_to_dsi(struct mipi_dsi_host *host)
{
	return container_of(host, struct dw_mipi_dsi, dsi_host);
}

static inline struct dw_mipi_dsi *con_to_dsi(struct drm_connector *con)
{
	return container_of(con, struct dw_mipi_dsi, connector);
}

static inline struct dw_mipi_dsi *encoder_to_dsi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct dw_mipi_dsi, encoder);
}

static int genif_wait_w_pld_fifo_not_full(struct dw_mipi_dsi *dsi)
{
	u32 sts;
	int ret;

	ret = regmap_read_poll_timeout(dsi->regmap, DSI_CMD_PKT_STATUS,
				       sts, !(sts & GEN_PLD_W_FULL),
				       10, CMD_PKT_STATUS_TIMEOUT_US);
	if (ret < 0) {
		dev_err(dsi->dev, "generic write payload fifo is full\n");
		return ret;
	}

	return 0;
}

static int genif_wait_cmd_fifo_not_full(struct dw_mipi_dsi *dsi)
{
	u32 sts;
	int ret;

	ret = regmap_read_poll_timeout(dsi->regmap, DSI_CMD_PKT_STATUS,
				       sts, !(sts & GEN_CMD_FULL),
				       10, CMD_PKT_STATUS_TIMEOUT_US);
	if (ret < 0) {
		dev_err(dsi->dev, "generic write cmd fifo is full\n");
		return ret;
	}

	return 0;
}

static int genif_wait_write_fifo_empty(struct dw_mipi_dsi *dsi)
{
	u32 sts;
	u32 mask;
	int ret;

	mask = GEN_CMD_EMPTY | GEN_PLD_W_EMPTY;
	ret = regmap_read_poll_timeout(dsi->regmap, DSI_CMD_PKT_STATUS,
				       sts, (sts & mask) == mask,
				       10, CMD_PKT_STATUS_TIMEOUT_US);
	if (ret < 0) {
		dev_err(dsi->dev, "generic write fifo is full\n");
		return ret;
	}

	return 0;
}

static inline void mipi_dphy_enableclk_assert(struct dw_mipi_dsi *dsi)
{
	regmap_update_bits(dsi->regmap, DSI_PHY_RSTZ,
			   PHY_ENABLECLK, PHY_ENABLECLK);
	udelay(1);
}

static inline void mipi_dphy_enableclk_deassert(struct dw_mipi_dsi *dsi)
{
	regmap_update_bits(dsi->regmap, DSI_PHY_RSTZ, PHY_ENABLECLK, 0);
	udelay(1);
}

static inline void mipi_dphy_shutdownz_assert(struct dw_mipi_dsi *dsi)
{
	regmap_update_bits(dsi->regmap, DSI_PHY_RSTZ, PHY_SHUTDOWNZ, 0);
	udelay(1);
}

static inline void mipi_dphy_shutdownz_deassert(struct dw_mipi_dsi *dsi)
{
	regmap_update_bits(dsi->regmap, DSI_PHY_RSTZ,
			   PHY_SHUTDOWNZ, PHY_SHUTDOWNZ);
	udelay(1);
}

static inline void mipi_dphy_rstz_assert(struct dw_mipi_dsi *dsi)
{
	regmap_update_bits(dsi->regmap, DSI_PHY_RSTZ, PHY_RSTZ, 0);
	udelay(1);
}

static inline void mipi_dphy_rstz_deassert(struct dw_mipi_dsi *dsi)
{
	regmap_update_bits(dsi->regmap, DSI_PHY_RSTZ, PHY_RSTZ, PHY_RSTZ);
	udelay(1);
}

static inline void dpishutdn_assert(struct dw_mipi_dsi *dsi)
{
	grf_field_write(dsi, DPISHUTDN, 1);
}

static inline void dpishutdn_deassert(struct dw_mipi_dsi *dsi)
{
	grf_field_write(dsi, DPISHUTDN, 0);
}

static inline void testif_testclk_assert(struct dw_mipi_dsi *dsi)
{
	regmap_update_bits(dsi->regmap, DSI_PHY_TST_CTRL0,
			   PHY_TESTCLK, PHY_TESTCLK);
	udelay(1);
}

static inline void testif_testclk_deassert(struct dw_mipi_dsi *dsi)
{
	regmap_update_bits(dsi->regmap, DSI_PHY_TST_CTRL0, PHY_TESTCLK, 0);
	udelay(1);
}

static inline void testif_testclr_assert(struct dw_mipi_dsi *dsi)
{
	regmap_update_bits(dsi->regmap, DSI_PHY_TST_CTRL0,
			   PHY_TESTCLR, PHY_TESTCLR);
	udelay(1);
}

static inline void testif_testclr_deassert(struct dw_mipi_dsi *dsi)
{
	regmap_update_bits(dsi->regmap, DSI_PHY_TST_CTRL0, PHY_TESTCLR, 0);
	udelay(1);
}

static inline void testif_testen_assert(struct dw_mipi_dsi *dsi)
{
	regmap_update_bits(dsi->regmap, DSI_PHY_TST_CTRL1,
			   PHY_TESTEN, PHY_TESTEN);
	udelay(1);
}

static inline void testif_testen_deassert(struct dw_mipi_dsi *dsi)
{
	regmap_update_bits(dsi->regmap, DSI_PHY_TST_CTRL1, PHY_TESTEN, 0);
	udelay(1);
}

static inline void testif_set_data(struct dw_mipi_dsi *dsi, u8 data)
{
	regmap_update_bits(dsi->regmap, DSI_PHY_TST_CTRL1,
			   PHY_TESTDIN_MASK, PHY_TESTDIN(data));
	udelay(1);
}

static inline u8 testif_get_data(struct dw_mipi_dsi *dsi)
{
	u32 data = 0;

	regmap_read(dsi->regmap, DSI_PHY_TST_CTRL1, &data);

	return data >> PHY_TESTDOUT_SHIFT;
}

static void testif_test_code_write(struct dw_mipi_dsi *dsi, u8 test_code)
{
	testif_testclk_assert(dsi);
	testif_set_data(dsi, test_code);
	testif_testen_assert(dsi);
	testif_testclk_deassert(dsi);
	testif_testen_deassert(dsi);
}

static void testif_test_data_write(struct dw_mipi_dsi *dsi, u8 test_data)
{
	testif_testclk_deassert(dsi);
	testif_set_data(dsi, test_data);
	testif_testclk_assert(dsi);
}

static int testif_write(void *context, unsigned int reg, unsigned int value)
{
	struct dw_mipi_dsi *dsi = context;

	testif_test_code_write(dsi, reg);
	testif_test_data_write(dsi, value);

	dev_dbg(dsi->dev,
		"test_code=0x%02x, test_data=0x%02x, monitor_data=0x%02x\n",
		reg, value, testif_get_data(dsi));

	return 0;
}

static int testif_read(void *context, unsigned int reg, unsigned int *value)
{
	struct dw_mipi_dsi *dsi = context;

	testif_test_code_write(dsi, reg);
	*value = testif_get_data(dsi);
	testif_test_data_write(dsi, *value);

	return 0;
}

static int mipi_dphy_power_on(struct dw_mipi_dsi *dsi)
{
	unsigned int val, mask;
	int ret;

	mipi_dphy_shutdownz_deassert(dsi);
	mipi_dphy_rstz_deassert(dsi);
	usleep_range(1500, 2000);

	if (dsi->dphy.phy) {
		ret = phy_set_mode(dsi->dphy.phy, PHY_MODE_VIDEO_MIPI);
		if (ret) {
			dev_err(dsi->dev, "failed to set phy mode: %d\n", ret);
			return ret;
		}

		phy_power_on(dsi->dphy.phy);
	}

	ret = regmap_read_poll_timeout(dsi->regmap, DSI_PHY_STATUS,
				       val, val & PHY_LOCK,
				       1000, PHY_STATUS_TIMEOUT_US);
	if (ret < 0) {
		dev_err(dsi->dev, "PHY is not locked\n");
		return ret;
	}

	usleep_range(100, 200);

	mask = PHY_STOPSTATELANE;
	ret = regmap_read_poll_timeout(dsi->regmap, DSI_PHY_STATUS,
				       val, (val & mask) == mask,
				       1000, PHY_STATUS_TIMEOUT_US);
	if (ret < 0) {
		dev_err(dsi->dev, "lane module is not in stop state\n");
		return ret;
	}

	udelay(10);

	return 0;
}

static void mipi_dphy_power_off(struct dw_mipi_dsi *dsi)
{
	regmap_write(dsi->regmap, DSI_PHY_RSTZ, 0);

	if (dsi->dphy.phy)
		phy_power_off(dsi->dphy.phy);
}

static int dw_mipi_dsi_turn_on_peripheral(struct dw_mipi_dsi *dsi)
{
	dpishutdn_assert(dsi);
	udelay(20);
	dpishutdn_deassert(dsi);

	return 0;
}

static int dw_mipi_dsi_shutdown_peripheral(struct dw_mipi_dsi *dsi)
{
	dpishutdn_deassert(dsi);
	udelay(20);
	dpishutdn_assert(dsi);

	return 0;
}

static void dw_mipi_dsi_host_power_on(struct dw_mipi_dsi *dsi)
{
	regmap_write(dsi->regmap, DSI_PWR_UP, POWERUP);
}

static void dw_mipi_dsi_host_power_off(struct dw_mipi_dsi *dsi)
{
	regmap_write(dsi->regmap, DSI_PWR_UP, RESET);
}

static void dw_mipi_dsi_phy_pll_init(struct dw_mipi_dsi *dsi)
{
	struct mipi_dphy *dphy = &dsi->dphy;
	u16 n, m;

	n = dphy->input_div - 1;
	m = dphy->feedback_div - 1;

	regmap_write(dphy->regmap, 0x19,
		     FEEDBACK_DIV_DEF_VAL_BYPASS | INPUT_DIV_DEF_VAL_BYPASS);
	regmap_write(dphy->regmap, 0x17, INPUT_DIV(n));
	regmap_write(dphy->regmap, 0x18, FEEDBACK_DIV_LO(m));
	regmap_write(dphy->regmap, 0x18, FEEDBACK_DIV_HI(m >> 5));
}

static void dw_mipi_dsi_phy_init(struct dw_mipi_dsi *dsi)
{
	struct mipi_dphy *dphy = &dsi->dphy;
	/* Table 5-1 Frequency Ranges */
	const struct {
		unsigned long max_lane_mbps;
		u8 hsfreqrange;
	} hsfreqrange_table[] = {
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
	u8 hsfreqrange, counter;
	unsigned int index, txbyteclkhs;

	for (index = 0; index < ARRAY_SIZE(hsfreqrange_table); index++)
		if (dsi->lane_mbps <= hsfreqrange_table[index].max_lane_mbps)
			break;

	if (index == ARRAY_SIZE(hsfreqrange_table))
		--index;

	hsfreqrange = hsfreqrange_table[index].hsfreqrange;
	regmap_write(dphy->regmap, 0x44, HSFREQRANGE(hsfreqrange));

	txbyteclkhs = dsi->lane_mbps >> 3;
	counter = txbyteclkhs * 60 / NSEC_PER_USEC;
	regmap_write(dphy->regmap, 0x60, 0x80 | counter);
	regmap_write(dphy->regmap, 0x70, 0x80 | counter);

	if (IS_DSI0(dsi))
		dw_mipi_dsi_phy_pll_init(dsi);
}

static unsigned long dw_mipi_dsi_get_lane_rate(struct dw_mipi_dsi *dsi)
{
	struct device *dev = dsi->dev;
	const struct drm_display_mode *mode = &dsi->mode;
	unsigned long max_lane_rate = dsi->pdata->max_bit_rate_per_lane;
	unsigned long lane_rate;
	unsigned int value;
	int bpp, lanes;
	u64 tmp;

	/* optional override of the desired bandwidth */
	if (!of_property_read_u32(dev->of_node, "rockchip,lane-rate", &value))
		return value * USEC_PER_SEC;

	bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
	if (bpp < 0)
		bpp = 24;

	lanes = dsi->slave ? dsi->lanes * 2 : dsi->lanes;
	tmp = (u64)mode->clock * 1000 * bpp;
	do_div(tmp, lanes);

	/* take 1 / 0.9, since mbps must big than bandwidth of RGB */
	tmp *= 10;
	do_div(tmp, 9);

	if (tmp > max_lane_rate)
		lane_rate = max_lane_rate;
	else
		lane_rate = tmp;

	return lane_rate;
}

static void dw_mipi_dsi_set_pll(struct dw_mipi_dsi *dsi, unsigned long rate)
{
	unsigned long fin, fout;
	unsigned long fvco_min, fvco_max, best_freq = 984000000;
	u8 min_prediv, max_prediv;
	u8 _prediv, best_prediv = 2;
	u16 _fbdiv, best_fbdiv = 82;
	u32 min_delta = UINT_MAX;

	fin = clk_get_rate(dsi->dphy.ref_clk);
	fout = rate;

	/* 5Mhz < Fref / N < 40MHz, 80MHz < Fvco < 1500Mhz */
	min_prediv = DIV_ROUND_UP(fin, 40000000);
	max_prediv = fin / 5000000;
	fvco_min = 80000000;
	fvco_max = 1500000000;

	for (_prediv = min_prediv; _prediv <= max_prediv; _prediv++) {
		u64 tmp, _fout;
		u32 delta;

		/* Fvco = Fref * M / N */
		tmp = (u64)fout * _prediv;
		do_div(tmp, fin);
		_fbdiv = tmp;

		/*
		 * Due to the use of a "by 2 pre-scaler," the range of the
		 * feedback multiplication value M is limited to even division
		 * numbers, and m must be greater than 12, less than 1000.
		 */
		if (_fbdiv <= 12 || _fbdiv >= 1000)
			continue;

		if (_fbdiv % 2)
			++_fbdiv;

		_fout = (u64)_fbdiv * fin;
		do_div(_fout, _prediv);

		if (_fout < fvco_min || _fout > fvco_max)
			continue;

		delta = abs(fout - _fout);
		if (!delta) {
			best_prediv = _prediv;
			best_fbdiv = _fbdiv;
			best_freq = _fout;
			break;
		} else if (delta < min_delta) {
			best_prediv = _prediv;
			best_fbdiv = _fbdiv;
			best_freq = _fout;
			min_delta = delta;
		}
	}

	dsi->lane_mbps = best_freq / USEC_PER_SEC;
	dsi->dphy.input_div = best_prediv;
	dsi->dphy.feedback_div = best_fbdiv;
	if (dsi->slave) {
		dsi->slave->lane_mbps = dsi->lane_mbps;
		dsi->slave->dphy.input_div = dsi->dphy.input_div;
		dsi->slave->dphy.feedback_div = dsi->dphy.feedback_div;
	}
}

static void dw_mipi_dsi_set_hs_clk(struct dw_mipi_dsi *dsi, unsigned long rate)
{
	rate = clk_round_rate(dsi->dphy.hs_clk, rate);
	clk_set_rate(dsi->dphy.hs_clk, rate);
	dsi->lane_mbps = rate / USEC_PER_SEC;
}

static int dw_mipi_dsi_host_attach(struct mipi_dsi_host *host,
				   struct mipi_dsi_device *device)
{
	struct dw_mipi_dsi *dsi = host_to_dsi(host);

	if (dsi->master)
		return 0;

	if (device->lanes < 1 || device->lanes > 8)
		return -EINVAL;

	dsi->client = device->dev.of_node;
	dsi->lanes = device->lanes;
	dsi->channel = device->channel;
	dsi->format = device->format;
	dsi->mode_flags = device->mode_flags;

	return 0;
}

static int dw_mipi_dsi_host_detach(struct mipi_dsi_host *host,
				   struct mipi_dsi_device *device)
{
	struct dw_mipi_dsi *dsi = host_to_dsi(host);

	if (dsi->panel)
		drm_panel_detach(dsi->panel);

	dsi->panel = NULL;
	return 0;
}

static int dw_mipi_dsi_read_from_fifo(struct dw_mipi_dsi *dsi,
				      const struct mipi_dsi_msg *msg)
{
	u8 *payload = msg->rx_buf;
	u16 length;
	u32 val;
	int ret;

	ret = regmap_read_poll_timeout(dsi->regmap, DSI_CMD_PKT_STATUS,
				       val, !(val & GEN_RD_CMD_BUSY), 50, 5000);
	if (ret) {
		dev_err(dsi->dev, "entire response isn't stored in the FIFO\n");
		return ret;
	}

	/* Receive payload */
	for (length = msg->rx_len; length; length -= 4) {
		ret = regmap_read_poll_timeout(dsi->regmap, DSI_CMD_PKT_STATUS,
					       val, !(val & GEN_PLD_R_EMPTY),
					       50, 5000);
		if (ret) {
			dev_err(dsi->dev, "Read payload FIFO is empty\n");
			return ret;
		}

		regmap_read(dsi->regmap, DSI_GEN_PLD_DATA, &val);

		switch (length) {
		case 3:
			payload[2] = (val >> 16) & 0xff;
			/* Fall through */
		case 2:
			payload[1] = (val >> 8) & 0xff;
			/* Fall through */
		case 1:
			payload[0] = val & 0xff;
			return 0;
		}

		payload[0] = (val >>  0) & 0xff;
		payload[1] = (val >>  8) & 0xff;
		payload[2] = (val >> 16) & 0xff;
		payload[3] = (val >> 24) & 0xff;
		payload += 4;
	}

	return 0;
}

static ssize_t dw_mipi_dsi_transfer(struct dw_mipi_dsi *dsi,
				    const struct mipi_dsi_msg *msg)
{
	struct mipi_dsi_packet packet;
	int ret;
	int val;
	int len = msg->tx_len;

	if (msg->flags & MIPI_DSI_MSG_USE_LPM) {
		regmap_update_bits(dsi->regmap, DSI_VID_MODE_CFG,
				   LP_CMD_EN, LP_CMD_EN);
		regmap_update_bits(dsi->regmap, DSI_LPCLK_CTRL,
				   PHY_TXREQUESTCLKHS, 0);
	} else {
		regmap_update_bits(dsi->regmap, DSI_VID_MODE_CFG, LP_CMD_EN, 0);
		regmap_update_bits(dsi->regmap, DSI_LPCLK_CTRL,
				   PHY_TXREQUESTCLKHS, PHY_TXREQUESTCLKHS);
	}

	switch (msg->type) {
	case MIPI_DSI_SHUTDOWN_PERIPHERAL:
		return dw_mipi_dsi_shutdown_peripheral(dsi);
	case MIPI_DSI_TURN_ON_PERIPHERAL:
		return dw_mipi_dsi_turn_on_peripheral(dsi);
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

	if (msg->flags & MIPI_DSI_MSG_REQ_ACK)
		regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG,
				   ACK_RQST_EN, ACK_RQST_EN);

	/* create a packet to the DSI protocol */
	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret) {
		dev_err(dsi->dev, "failed to create packet: %d\n", ret);
		return ret;
	}

	/* Send payload */
	while (DIV_ROUND_UP(packet.payload_length, 4)) {
		/*
		 * Alternatively, you can always keep the FIFO
		 * nearly full by monitoring the FIFO state until
		 * it is not full, and then writea single word of data.
		 * This solution is more resource consuming
		 * but it simultaneously avoids FIFO starvation,
		 * making it possible to use FIFO sizes smaller than
		 * the amount of data of the longest packet to be written.
		 */
		ret = genif_wait_w_pld_fifo_not_full(dsi);
		if (ret)
			return ret;

		if (packet.payload_length < 4) {
			/* send residu payload */
			val = 0;
			memcpy(&val, packet.payload, packet.payload_length);
			regmap_write(dsi->regmap, DSI_GEN_PLD_DATA, val);
			packet.payload_length = 0;
		} else {
			val = get_unaligned_le32(packet.payload);
			regmap_write(dsi->regmap, DSI_GEN_PLD_DATA, val);
			packet.payload += 4;
			packet.payload_length -= 4;
		}
	}

	ret = genif_wait_cmd_fifo_not_full(dsi);
	if (ret)
		return ret;

	/* Send packet header */
	val = get_unaligned_le32(packet.header);
	regmap_write(dsi->regmap, DSI_GEN_HDR, val);

	ret = genif_wait_write_fifo_empty(dsi);
	if (ret)
		return ret;

	if (msg->rx_len) {
		ret = dw_mipi_dsi_read_from_fifo(dsi, msg);
		if (ret < 0)
			return ret;
	}

	if (dsi->slave)
		dw_mipi_dsi_transfer(dsi->slave, msg);

	return len;
}

static ssize_t dw_mipi_dsi_host_transfer(struct mipi_dsi_host *host,
					 const struct mipi_dsi_msg *msg)
{
	struct dw_mipi_dsi *dsi = host_to_dsi(host);

	return dw_mipi_dsi_transfer(dsi, msg);
}

static const struct mipi_dsi_host_ops dw_mipi_dsi_host_ops = {
	.attach = dw_mipi_dsi_host_attach,
	.detach = dw_mipi_dsi_host_detach,
	.transfer = dw_mipi_dsi_host_transfer,
};

static void dw_mipi_dsi_video_mode_config(struct dw_mipi_dsi *dsi)
{
	u32 val = LP_VACT_EN | LP_VFP_EN | LP_VBP_EN | LP_VSA_EN |
		  LP_HFP_EN | LP_HBP_EN;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_HFP)
		val &= ~LP_HFP_EN;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_HBP)
		val &= ~LP_HBP_EN;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		val |= VID_MODE_TYPE_BURST;
	else if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		val |= VID_MODE_TYPE_BURST_SYNC_PULSES;
	else
		val |= VID_MODE_TYPE_BURST_SYNC_EVENTS;

	regmap_write(dsi->regmap, DSI_VID_MODE_CFG, val);

	if (dsi->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS)
		regmap_update_bits(dsi->regmap, DSI_LPCLK_CTRL,
				   AUTO_CLKLANE_CTRL, AUTO_CLKLANE_CTRL);
}

static void mipi_dphy_init(struct dw_mipi_dsi *dsi)
{
	u32 map[] = {0x1, 0x3, 0x7, 0xf};

	mipi_dphy_enableclk_deassert(dsi);
	mipi_dphy_shutdownz_assert(dsi);
	mipi_dphy_rstz_assert(dsi);
	testif_testclr_assert(dsi);

	/* Configures DPHY to work as a Master */
	grf_field_write(dsi, MASTERSLAVEZ, 1);

	/* Configures lane as TX */
	grf_field_write(dsi, BASEDIR, 0);

	/* Set all REQUEST inputs to zero */
	grf_field_write(dsi, TURNREQUEST, 0);
	grf_field_write(dsi, TURNDISABLE, 0);
	grf_field_write(dsi, FORCETXSTOPMODE, 0);
	grf_field_write(dsi, FORCERXMODE, 0);
	udelay(1);

	testif_testclr_deassert(dsi);

	if (!dsi->dphy.phy)
		dw_mipi_dsi_phy_init(dsi);

	/* Enable Data Lane Module */
	grf_field_write(dsi, ENABLE_N, map[dsi->lanes - 1]);

	/* Enable Clock Lane Module */
	grf_field_write(dsi, ENABLECLK, 1);

	mipi_dphy_enableclk_assert(dsi);
}

static void dw_mipi_dsi_init(struct dw_mipi_dsi *dsi)
{
	u32 esc_clk_div;

	regmap_write(dsi->regmap, DSI_PWR_UP, RESET);

	/* The maximum value of the escape clock frequency is 20MHz */
	esc_clk_div = DIV_ROUND_UP(dsi->lane_mbps >> 3, 20);
	regmap_write(dsi->regmap, DSI_CLKMGR_CFG, TO_CLK_DIVIDSION(10) |
		     TX_ESC_CLK_DIVIDSION(esc_clk_div));
}

static void dw_mipi_dsi_dpi_config(struct dw_mipi_dsi *dsi,
				   struct drm_display_mode *mode)
{
	u32 val = 0, color = 0;

	switch (dsi->format) {
	case MIPI_DSI_FMT_RGB888:
		color = DPI_COLOR_CODING_24BIT;
		break;
	case MIPI_DSI_FMT_RGB666:
		color = DPI_COLOR_CODING_18BIT_2 | EN18_LOOSELY;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		color = DPI_COLOR_CODING_18BIT_1;
		break;
	case MIPI_DSI_FMT_RGB565:
		color = DPI_COLOR_CODING_16BIT_1;
		break;
	}

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		val |= VSYNC_ACTIVE_LOW;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		val |= HSYNC_ACTIVE_LOW;

	regmap_write(dsi->regmap, DSI_DPI_VCID, DPI_VID(dsi->channel));
	regmap_write(dsi->regmap, DSI_DPI_COLOR_CODING, color);
	regmap_write(dsi->regmap, DSI_DPI_CFG_POL, val);
	regmap_write(dsi->regmap, DSI_DPI_LP_CMD_TIM,
		     OUTVACT_LPCMD_TIME(4) | INVACT_LPCMD_TIME(4));
}

static void dw_mipi_dsi_packet_handler_config(struct dw_mipi_dsi *dsi)
{
	u32 val = CRC_RX_EN | ECC_RX_EN | BTA_EN | EOTP_TX_EN;

	if (dsi->mode_flags & MIPI_DSI_MODE_EOT_PACKET)
		val &= ~EOTP_TX_EN;

	regmap_write(dsi->regmap, DSI_PCKHDL_CFG, val);
}

static void dw_mipi_dsi_video_packet_config(struct dw_mipi_dsi *dsi,
					    struct drm_display_mode *mode)
{
	int pkt_size;

	if (dsi->slave || dsi->master)
		pkt_size = VID_PKT_SIZE(mode->hdisplay / 2);
	else
		pkt_size = VID_PKT_SIZE(mode->hdisplay);

	regmap_write(dsi->regmap, DSI_VID_PKT_SIZE, pkt_size);
}

static void dw_mipi_dsi_command_mode_config(struct dw_mipi_dsi *dsi)
{
	regmap_write(dsi->regmap, DSI_TO_CNT_CFG,
		     HSTX_TO_CNT(1000) | LPRX_TO_CNT(1000));
	regmap_write(dsi->regmap, DSI_BTA_TO_CNT, 0xd00);
}

/* Get lane byte clock cycles. */
static u32 dw_mipi_dsi_get_hcomponent_lbcc(struct dw_mipi_dsi *dsi,
					   u32 hcomponent)
{
	u32 lbcc;

	lbcc = hcomponent * dsi->lane_mbps * MSEC_PER_SEC / 8;

	if (dsi->mode.clock == 0) {
		dev_err(dsi->dev, "dsi mode clock is 0!\n");
		return 0;
	}

	return DIV_ROUND_CLOSEST_ULL(lbcc, dsi->mode.clock);
}

static void dw_mipi_dsi_line_timer_config(struct dw_mipi_dsi *dsi)
{
	u32 htotal, hsa, hbp, lbcc;
	struct drm_display_mode *mode = &dsi->mode;

	htotal = mode->htotal;
	hsa = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;

	lbcc = dw_mipi_dsi_get_hcomponent_lbcc(dsi, htotal);
	regmap_write(dsi->regmap, DSI_VID_HLINE_TIME, lbcc);

	lbcc = dw_mipi_dsi_get_hcomponent_lbcc(dsi, hsa);
	regmap_write(dsi->regmap, DSI_VID_HSA_TIME, lbcc);

	lbcc = dw_mipi_dsi_get_hcomponent_lbcc(dsi, hbp);
	regmap_write(dsi->regmap, DSI_VID_HBP_TIME, lbcc);
}

static void dw_mipi_dsi_vertical_timing_config(struct dw_mipi_dsi *dsi)
{
	u32 vactive, vsa, vfp, vbp;
	struct drm_display_mode *mode = &dsi->mode;

	vactive = mode->vdisplay;
	vsa = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;

	regmap_write(dsi->regmap, DSI_VID_VACTIVE_LINES, vactive);
	regmap_write(dsi->regmap, DSI_VID_VSA_LINES, vsa);
	regmap_write(dsi->regmap, DSI_VID_VFP_LINES, vfp);
	regmap_write(dsi->regmap, DSI_VID_VBP_LINES, vbp);
}

static void dw_mipi_dsi_dphy_timing_config(struct dw_mipi_dsi *dsi)
{
	regmap_write(dsi->regmap, DSI_PHY_TMR_CFG, PHY_HS2LP_TIME(0x14) |
		     PHY_LP2HS_TIME(0x10) | MAX_RD_TIME(10000));
	regmap_write(dsi->regmap, DSI_PHY_TMR_LPCLK_CFG,
		     PHY_CLKHS2LP_TIME(0x40) | PHY_CLKLP2HS_TIME(0x40));
}

static void dw_mipi_dsi_dphy_interface_config(struct dw_mipi_dsi *dsi)
{
	regmap_write(dsi->regmap, DSI_PHY_IF_CFG,
		     PHY_STOP_WAIT_TIME(0x20) | N_LANES(dsi->lanes));
}

static void dw_mipi_dsi_interrupt_enable(struct dw_mipi_dsi *dsi)
{
	regmap_write(dsi->regmap, DSI_INT_MSK0, 0x1fffff);
	regmap_write(dsi->regmap, DSI_INT_MSK1, 0x1f7f);
}

static void dw_mipi_dsi_interrupt_disable(struct dw_mipi_dsi *dsi)
{
	regmap_write(dsi->regmap, DSI_INT_MSK0, 0);
	regmap_write(dsi->regmap, DSI_INT_MSK1, 0);
}

static void dw_mipi_dsi_encoder_mode_set(struct drm_encoder *encoder,
					struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode)
{
	struct dw_mipi_dsi *dsi = encoder_to_dsi(encoder);

	drm_mode_copy(&dsi->mode, adjusted_mode);

	if (dsi->slave)
		drm_mode_copy(&dsi->slave->mode, adjusted_mode);
}

static void dw_mipi_dsi_disable(struct dw_mipi_dsi *dsi)
{
	regmap_update_bits(dsi->regmap, DSI_INT_MSK1, DPI_PLD_WR_ERR, 0);
	regmap_write(dsi->regmap, DSI_PWR_UP, RESET);
	regmap_write(dsi->regmap, DSI_LPCLK_CTRL, 0);
	regmap_write(dsi->regmap, DSI_EDPI_CMD_SIZE, 0);
	regmap_update_bits(dsi->regmap, DSI_MODE_CFG,
			   CMD_VIDEO_MODE, COMMAND_MODE);
	regmap_write(dsi->regmap, DSI_PWR_UP, POWERUP);

	if (dsi->slave)
		dw_mipi_dsi_disable(dsi->slave);
}

static void dw_mipi_dsi_post_disable(struct dw_mipi_dsi *dsi)
{
	dw_mipi_dsi_interrupt_disable(dsi);
	dw_mipi_dsi_host_power_off(dsi);
	mipi_dphy_power_off(dsi);

	pm_runtime_put(dsi->dev);

	if (dsi->slave)
		dw_mipi_dsi_post_disable(dsi->slave);
}

static void dw_mipi_dsi_encoder_disable(struct drm_encoder *encoder)
{
	struct dw_mipi_dsi *dsi = encoder_to_dsi(encoder);

	if (dsi->panel)
		drm_panel_disable(dsi->panel);

	dw_mipi_dsi_disable(dsi);

	if (dsi->panel)
		drm_panel_unprepare(dsi->panel);

	dw_mipi_dsi_post_disable(dsi);
}

static void dw_mipi_dsi_host_init(struct dw_mipi_dsi *dsi)
{
	dw_mipi_dsi_init(dsi);
	dw_mipi_dsi_dpi_config(dsi, &dsi->mode);
	dw_mipi_dsi_packet_handler_config(dsi);
	dw_mipi_dsi_video_mode_config(dsi);
	dw_mipi_dsi_video_packet_config(dsi, &dsi->mode);
	dw_mipi_dsi_command_mode_config(dsi);
	regmap_update_bits(dsi->regmap, DSI_MODE_CFG,
			   CMD_VIDEO_MODE, COMMAND_MODE);
	dw_mipi_dsi_line_timer_config(dsi);
	dw_mipi_dsi_vertical_timing_config(dsi);
	dw_mipi_dsi_dphy_timing_config(dsi);
	dw_mipi_dsi_dphy_interface_config(dsi);
	dw_mipi_dsi_interrupt_enable(dsi);
}

static void dw_mipi_dsi_pre_enable(struct dw_mipi_dsi *dsi)
{
	pm_runtime_get_sync(dsi->dev);

	/* MIPI DSI APB software reset request. */
	reset_control_assert(dsi->rst);
	udelay(10);
	reset_control_deassert(dsi->rst);
	udelay(10);

	dw_mipi_dsi_host_init(dsi);
	mipi_dphy_init(dsi);
	mipi_dphy_power_on(dsi);
	dw_mipi_dsi_host_power_on(dsi);

	if (dsi->slave)
		dw_mipi_dsi_pre_enable(dsi->slave);
}

static void dw_mipi_dsi_enable(struct dw_mipi_dsi *dsi)
{
	const struct drm_display_mode *mode = &dsi->mode;
	u32 int_st1;

	/*
	 * The high-speed clock is started before that the high-speed data is
	 * sent via the data lanes.
	 */
	regmap_update_bits(dsi->regmap, DSI_LPCLK_CTRL,
			   PHY_TXREQUESTCLKHS, PHY_TXREQUESTCLKHS);

	regmap_write(dsi->regmap, DSI_PWR_UP, RESET);

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO) {
		regmap_update_bits(dsi->regmap, DSI_MODE_CFG,
				   CMD_VIDEO_MODE, VIDEO_MODE);
	} else {
		regmap_write(dsi->regmap, DSI_DBI_VCID,
			     DBI_VCID(dsi->channel));
		regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG,
				   DCS_LW_TX, 0);
		regmap_write(dsi->regmap, DSI_EDPI_CMD_SIZE, mode->hdisplay);
		regmap_update_bits(dsi->regmap, DSI_MODE_CFG,
				   CMD_VIDEO_MODE, COMMAND_MODE);
	}

	regmap_write(dsi->regmap, DSI_PWR_UP, POWERUP);

	regmap_read(dsi->regmap, DSI_INT_ST1, &int_st1);
	regmap_update_bits(dsi->regmap, DSI_INT_MSK1,
			   DPI_PLD_WR_ERR, DPI_PLD_WR_ERR);

	if (dsi->slave)
		dw_mipi_dsi_enable(dsi->slave);
}

static void dw_mipi_dsi_vop_routing(struct dw_mipi_dsi *dsi)
{
	int pipe;

	pipe = drm_of_encoder_active_endpoint_id(dsi->dev->of_node,
						 &dsi->encoder);
	grf_field_write(dsi, VOPSEL, pipe);
	if (dsi->slave)
		grf_field_write(dsi->slave, VOPSEL, pipe);
}

static void dw_mipi_dsi_encoder_enable(struct drm_encoder *encoder)
{
	struct dw_mipi_dsi *dsi = encoder_to_dsi(encoder);
	unsigned long lane_rate = dw_mipi_dsi_get_lane_rate(dsi);

	if (dsi->dphy.phy)
		dw_mipi_dsi_set_hs_clk(dsi, lane_rate);
	else
		dw_mipi_dsi_set_pll(dsi, lane_rate);

	dev_info(dsi->dev, "final DSI-Link bandwidth: %u x %d Mbps\n",
		 dsi->lane_mbps, dsi->slave ? dsi->lanes * 2 : dsi->lanes);

	dw_mipi_dsi_vop_routing(dsi);
	dw_mipi_dsi_pre_enable(dsi);

	if (dsi->panel)
		drm_panel_prepare(dsi->panel);

	dw_mipi_dsi_enable(dsi);

	if (dsi->panel)
		drm_panel_enable(dsi->panel);
}

static int
dw_mipi_dsi_encoder_atomic_check(struct drm_encoder *encoder,
				 struct drm_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct dw_mipi_dsi *dsi = encoder_to_dsi(encoder);
	struct drm_connector *connector = conn_state->connector;
	struct drm_display_info *info = &connector->display_info;

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
	if (info->num_bus_formats)
		s->bus_format = info->bus_formats[0];
	else
		s->bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	s->tv_state = &conn_state->tv;
	s->eotf = TRADITIONAL_GAMMA_SDR;
	s->color_space = V4L2_COLORSPACE_DEFAULT;

	if (dsi->slave)
		s->output_flags |= ROCKCHIP_OUTPUT_DSI_DUAL_CHANNEL;

	if (IS_DSI1(dsi))
		s->output_flags |= ROCKCHIP_OUTPUT_DSI_DUAL_LINK;

	return 0;
}

static int dw_mipi_dsi_loader_protect(struct dw_mipi_dsi *dsi, bool on)
{
	u32 int_st1;

	if (on) {
		pm_runtime_get_sync(dsi->dev);
		regmap_read(dsi->regmap, DSI_INT_ST1, &int_st1);
		regmap_update_bits(dsi->regmap, DSI_INT_MSK1,
				   DPI_PLD_WR_ERR, DPI_PLD_WR_ERR);
	} else {
		regmap_update_bits(dsi->regmap, DSI_INT_MSK1,
				   DPI_PLD_WR_ERR, 0);
		pm_runtime_put(dsi->dev);
	}

	if (dsi->slave)
		dw_mipi_dsi_loader_protect(dsi->slave, on);

	return 0;
}

static int dw_mipi_dsi_encoder_loader_protect(struct drm_encoder *encoder,
					      bool on)
{
	struct dw_mipi_dsi *dsi = encoder_to_dsi(encoder);

	if (dsi->panel)
		drm_panel_loader_protect(dsi->panel, on);

	return dw_mipi_dsi_loader_protect(dsi, on);
}

static const struct drm_encoder_helper_funcs
dw_mipi_dsi_encoder_helper_funcs = {
	.loader_protect = dw_mipi_dsi_encoder_loader_protect,
	.mode_set = dw_mipi_dsi_encoder_mode_set,
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

static struct drm_encoder *dw_mipi_dsi_connector_best_encoder(
					struct drm_connector *connector)
{
	struct dw_mipi_dsi *dsi = con_to_dsi(connector);

	return &dsi->encoder;
}

static const struct drm_connector_helper_funcs
dw_mipi_dsi_connector_helper_funcs = {
	.get_modes = dw_mipi_dsi_connector_get_modes,
	.best_encoder = dw_mipi_dsi_connector_best_encoder,
};

static enum drm_connector_status
dw_mipi_dsi_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void dw_mipi_dsi_drm_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs dw_mipi_dsi_atomic_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = dw_mipi_dsi_detect,
	.destroy = dw_mipi_dsi_drm_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int dw_mipi_dsi_dual_channel_probe(struct dw_mipi_dsi *dsi)
{
	struct device_node *np;
	struct platform_device *secondary;

	np = of_parse_phandle(dsi->dev->of_node, "rockchip,dual-channel", 0);
	if (np) {
		secondary = of_find_device_by_node(np);
		dsi->slave = platform_get_drvdata(secondary);
		of_node_put(np);

		if (!dsi->slave)
			return -EPROBE_DEFER;

		dsi->slave->master = dsi;
		dsi->lanes /= 2;

		dsi->slave->lanes = dsi->lanes;
		dsi->slave->channel = dsi->channel;
		dsi->slave->format = dsi->format;
		dsi->slave->mode_flags = dsi->mode_flags;
	}

	return 0;
}

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
		dev_err(dev, "Failed to initialize encoder with drm\n");
		return ret;
	}

	/* If there's a bridge, attach to it and let it create the connector. */
	if (dsi->bridge) {
		dsi->bridge->driver_private = &dsi->dsi_host;
		dsi->bridge->encoder = encoder;

		ret = drm_bridge_attach(drm, dsi->bridge);
		if (ret) {
			dev_err(dev, "Failed to attach bridge: %d\n", ret);
			goto encoder_cleanup;
		}

		encoder->bridge = dsi->bridge;
	/* Otherwise create our own connector and attach to a panel */
	} else {
		dsi->connector.port = dev->of_node;
		ret = drm_connector_init(drm, &dsi->connector,
					 &dw_mipi_dsi_atomic_connector_funcs,
					 DRM_MODE_CONNECTOR_DSI);
		if (ret) {
			dev_err(dev, "Failed to initialize connector\n");
			goto encoder_cleanup;
		}
		drm_connector_helper_add(connector,
					 &dw_mipi_dsi_connector_helper_funcs);
		drm_mode_connector_attach_encoder(connector, encoder);
		drm_panel_attach(dsi->panel, connector);
	}

	return 0;

encoder_cleanup:
	drm_encoder_cleanup(encoder);
	return ret;
}

static int dw_mipi_dsi_bind(struct device *dev, struct device *master,
			     void *data)
{
	struct drm_device *drm = data;
	struct dw_mipi_dsi *dsi = dev_get_drvdata(dev);
	int ret;

	ret = dw_mipi_dsi_dual_channel_probe(dsi);
	if (ret)
		return ret;

	if (dsi->master)
		return 0;

	dsi->panel = of_drm_find_panel(dsi->client);
	if (!dsi->panel) {
		dsi->bridge = of_drm_find_bridge(dsi->client);
		if (!dsi->bridge)
			return -EPROBE_DEFER;
	}

	ret = dw_mipi_dsi_register(drm, dsi);
	if (ret) {
		dev_err(dev, "Failed to register mipi_dsi: %d\n", ret);
		return ret;
	}

	dev_set_drvdata(dev, dsi);

	pm_runtime_enable(dev);
	if (dsi->slave)
		pm_runtime_enable(dsi->slave->dev);

	return ret;
}

static void dw_mipi_dsi_unbind(struct device *dev, struct device *master,
	void *data)
{
	struct dw_mipi_dsi *dsi = dev_get_drvdata(dev);

	pm_runtime_disable(dev);
	if (dsi->slave)
		pm_runtime_disable(dsi->slave->dev);
}

static const struct component_ops dw_mipi_dsi_ops = {
	.bind	= dw_mipi_dsi_bind,
	.unbind	= dw_mipi_dsi_unbind,
};

static const char * const dphy_error[] = {
	"ErrEsc escape entry error from Lane 0",
	"ErrSyncEsc low-power data transmission synchronization error from Lane 0",
	"the ErrControl error from Lane 0",
	"LP0 contention error ErrContentionLP0 from Lane 0",
	"LP1 contention error ErrContentionLP1 from Lane 0",
};

static const char * const ack_with_err[] = {
	"the SoT error from the Acknowledge error report",
	"the SoT Sync error from the Acknowledge error report",
	"the EoT Sync error from the Acknowledge error report",
	"the Escape Mode Entry Command error from the Acknowledge error report",
	"the LP Transmit Sync error from the Acknowledge error report",
	"the Peripheral Timeout error from the Acknowledge Error report",
	"the False Control error from the Acknowledge error report",
	"the reserved (specific to device) from the Acknowledge error report",
	"the ECC error, single-bit (detected and corrected) from the Acknowledge error report",
	"the ECC error, multi-bit (detected, not corrected) from the Acknowledge error report",
	"the checksum error (long packet only) from the Acknowledge error report",
	"the not recognized DSI data type from the Acknowledge error report",
	"the DSI VC ID Invalid from the Acknowledge error report",
	"the invalid transmission length from the Acknowledge error report",
	"the reserved (specific to device) from the Acknowledge error report",
	"the DSI protocol violation from the Acknowledge error report",
};

static const char * const error_report[] = {
	"Host reports that the configured timeout counter for the high-speed transmission has expired",
	"Host reports that the configured timeout counter for the low-power reception has expired",
	"Host reports that a received packet contains a single bit error",
	"Host reports that a received packet contains multiple ECC errors",
	"Host reports that a received long packet has a CRC error in its payload",
	"Host receives a transmission that does not end in the expected by boundaries",
	"Host receives a transmission that does not end with an End of Transmission packet",
	"An overflow occurs in the DPI pixel payload FIFO",
	"An overflow occurs in the Generic command FIFO",
	"An overflow occurs in the Generic write payload FIFO",
	"An underflow occurs in the Generic write payload FIFO",
	"An underflow occurs in the Generic read FIFO",
	"An overflow occurs in the Generic read FIFO",
};

static irqreturn_t dw_mipi_dsi_irq_handler(int irq, void *dev_id)
{
	struct dw_mipi_dsi *dsi = dev_id;
	u32 int_st0, int_st1;
	unsigned int i;

	regmap_read(dsi->regmap, DSI_INT_ST0, &int_st0);
	regmap_read(dsi->regmap, DSI_INT_ST1, &int_st1);

	for (i = 0; i < ARRAY_SIZE(ack_with_err); i++)
		if (int_st0 & BIT(i))
			dev_err(dsi->dev, "%s\n", ack_with_err[i]);

	for (i = 0; i < ARRAY_SIZE(dphy_error); i++)
		if (int_st0 & BIT(16 + i))
			dev_err(dsi->dev, "%s\n", dphy_error[i]);

	for (i = 0; i < ARRAY_SIZE(error_report); i++)
		if (int_st1 & BIT(i))
			dev_err(dsi->dev, "%s\n", error_report[i]);

	if (int_st1 & DPI_PLD_WR_ERR) {
		regmap_write(dsi->regmap, DSI_PWR_UP, RESET);
		regmap_write(dsi->regmap, DSI_PWR_UP, POWERUP);
	}

	return IRQ_HANDLED;
}

static const struct regmap_config testif_regmap_config = {
	.name = "phy",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x97,
	.fast_io = true,
	.reg_write = testif_write,
	.reg_read = testif_read,
};

static int mipi_dphy_attach(struct dw_mipi_dsi *dsi)
{
	struct device *dev = dsi->dev;
	struct mipi_dphy *dphy = &dsi->dphy;
	int ret;

	dphy->phy = devm_phy_optional_get(dev, "mipi_dphy");
	if (IS_ERR(dphy->phy)) {
		ret = PTR_ERR(dphy->phy);
		dev_err(dev, "failed to get mipi dphy: %d\n", ret);
		return ret;
	}

	if (dphy->phy) {
		dev_dbg(dev, "Use Non-SNPS PHY\n");

		dphy->hs_clk = devm_clk_get(dev, "hs_clk");
		if (IS_ERR(dphy->hs_clk)) {
			dev_err(dev, "failed to get PHY high-speed clock\n");
			return PTR_ERR(dphy->hs_clk);
		}
	} else {
		dev_dbg(dev, "Use SNPS PHY\n");

		dphy->ref_clk = devm_clk_get(dev, "ref");
		if (IS_ERR(dphy->ref_clk)) {
			dev_err(dev, "failed to get PHY reference clock\n");
			return PTR_ERR(dphy->ref_clk);
		}

		if (dsi->pdata->soc_type != RK3288) {
			dphy->cfg_clk = devm_clk_get(dev, "phy_cfg");
			if (IS_ERR(dphy->cfg_clk)) {
				dev_err(dev, "failed to get PHY config clk\n");
				return PTR_ERR(dphy->cfg_clk);
			}
		}

		dphy->regmap = devm_regmap_init(dev, NULL, dsi,
						&testif_regmap_config);
		if (IS_ERR(dphy->regmap)) {
			dev_err(dev, "failed to create mipi dphy regmap\n");
			return PTR_ERR(dphy->regmap);
		}
	}

	return 0;
}

static int dw_mipi_dsi_parse_dt(struct dw_mipi_dsi *dsi)
{
	struct device *dev = dsi->dev;
	struct device_node *np = dev->of_node;
	struct device_node *endpoint, *remote = NULL;

	endpoint = of_graph_get_endpoint_by_regs(np, 1, -1);
	if (endpoint) {
		remote = of_graph_get_remote_port_parent(endpoint);
		of_node_put(endpoint);
		if (!remote) {
			dev_err(dev, "no panel/bridge connected\n");
			return -ENODEV;
		}
		of_node_put(remote);
	}

	dsi->client = remote;

	return 0;
}

static const struct regmap_config dw_mipi_dsi_regmap_config = {
	.name = "host",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = DSI_MAX_REGISGER,
};

static int dw_mipi_dsi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_mipi_dsi *dsi;
	struct device_node *np = dev->of_node;
	struct resource *res;
	void __iomem *regs;
	int ret;
	int dsi_id;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi_id = of_alias_get_id(np, "dsi");
	if (dsi_id < 0)
		dsi_id = 0;

	dsi->id = dsi_id;
	dsi->dev = dev;
	dsi->pdata = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, dsi);

	ret = dw_mipi_dsi_parse_dt(dsi);
	if (ret) {
		dev_err(dev, "failed to parse DT\n");
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	dsi->irq = platform_get_irq(pdev, 0);
	if (dsi->irq < 0)
		return dsi->irq;

	dsi->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dsi->pclk)) {
		ret = PTR_ERR(dsi->pclk);
		dev_err(dev, "Unable to get pclk: %d\n", ret);
		return ret;
	}

	dsi->regmap = devm_regmap_init_mmio(dev, regs,
					    &dw_mipi_dsi_regmap_config);
	if (IS_ERR(dsi->regmap)) {
		dev_err(dev, "failed to init register map\n");
		return PTR_ERR(dsi->regmap);
	}

	if (dsi->pdata->soc_type == RK3126) {
		dsi->h2p_clk = devm_clk_get(dev, "h2p");
		if (IS_ERR(dsi->h2p_clk)) {
			dev_err(dev, "failed to get h2p bridge clock\n");
			return PTR_ERR(dsi->h2p_clk);
		}
	}

	dsi->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(dsi->grf)) {
		dev_err(dev, "Unable to get rockchip,grf\n");
		return PTR_ERR(dsi->grf);
	}

	dsi->rst = devm_reset_control_get(dev, "apb");
	if (IS_ERR(dsi->rst)) {
		dev_err(dev, "failed to get reset control\n");
		return PTR_ERR(dsi->rst);
	}

	ret = mipi_dphy_attach(dsi);
	if (ret)
		return ret;

	ret = devm_request_irq(dev, dsi->irq, dw_mipi_dsi_irq_handler,
			       IRQF_SHARED, dev_name(dev), dsi);
	if (ret) {
		dev_err(dev, "failed to request irq: %d\n", ret);
		return ret;
	}

	dsi->dsi_host.ops = &dw_mipi_dsi_host_ops;
	dsi->dsi_host.dev = dev;

	ret = mipi_dsi_host_register(&dsi->dsi_host);
	if (ret)
		return ret;

	ret = component_add(dev, &dw_mipi_dsi_ops);
	if (ret)
		mipi_dsi_host_unregister(&dsi->dsi_host);

	return ret;
}

static int dw_mipi_dsi_remove(struct platform_device *pdev)
{
	struct dw_mipi_dsi *dsi = dev_get_drvdata(&pdev->dev);

	if (dsi)
		mipi_dsi_host_unregister(&dsi->dsi_host);
	component_del(&pdev->dev, &dw_mipi_dsi_ops);
	return 0;
}

static int __maybe_unused dw_mipi_dsi_runtime_suspend(struct device *dev)
{
	struct dw_mipi_dsi *dsi = dev_get_drvdata(dev);

	clk_disable_unprepare(dsi->pclk);
	clk_disable_unprepare(dsi->h2p_clk);
	clk_disable_unprepare(dsi->dphy.hs_clk);
	clk_disable_unprepare(dsi->dphy.ref_clk);
	clk_disable_unprepare(dsi->dphy.cfg_clk);

	return 0;
}

static int __maybe_unused dw_mipi_dsi_runtime_resume(struct device *dev)
{
	struct dw_mipi_dsi *dsi = dev_get_drvdata(dev);

	clk_prepare_enable(dsi->dphy.cfg_clk);
	clk_prepare_enable(dsi->dphy.ref_clk);
	clk_prepare_enable(dsi->dphy.hs_clk);
	clk_prepare_enable(dsi->h2p_clk);
	clk_prepare_enable(dsi->pclk);

	return 0;
}

static const struct dev_pm_ops dw_mipi_dsi_pm_ops = {
	SET_RUNTIME_PM_OPS(dw_mipi_dsi_runtime_suspend,
			   dw_mipi_dsi_runtime_resume, NULL)
};

static const u32 px30_dsi_grf_reg_fields[MAX_FIELDS] = {
	[DPIUPDATECFG]		= GRF_REG_FIELD(0x0434,  7,  7),
	[DPICOLORM]		= GRF_REG_FIELD(0x0434,  3,  3),
	[DPISHUTDN]		= GRF_REG_FIELD(0x0434,  2,  2),
	[FORCETXSTOPMODE]	= GRF_REG_FIELD(0x0438,  7, 10),
	[FORCERXMODE]		= GRF_REG_FIELD(0x0438,  6,  6),
	[TURNDISABLE]		= GRF_REG_FIELD(0x0438,  5,  5),
	[VOPSEL]		= GRF_REG_FIELD(0x0438,  0,  0),
};

static const struct dw_mipi_dsi_plat_data px30_socdata = {
	.dsi0_grf_reg_fields = px30_dsi_grf_reg_fields,
	.max_bit_rate_per_lane = 1000000000UL,
	.soc_type = PX30,
};

static const u32 rk1808_dsi_grf_reg_fields[MAX_FIELDS] = {
	[MASTERSLAVEZ]          = GRF_REG_FIELD(0x0440,  8,  8),
	[DPIUPDATECFG]          = GRF_REG_FIELD(0x0440,  7,  7),
	[DPICOLORM]             = GRF_REG_FIELD(0x0440,  3,  3),
	[DPISHUTDN]             = GRF_REG_FIELD(0x0440,  2,  2),
	[FORCETXSTOPMODE]       = GRF_REG_FIELD(0x0444,  7, 10),
	[FORCERXMODE]           = GRF_REG_FIELD(0x0444,  6,  6),
	[TURNDISABLE]           = GRF_REG_FIELD(0x0444,  5,  5),
};

static const struct dw_mipi_dsi_plat_data rk1808_socdata = {
	.dsi0_grf_reg_fields = rk1808_dsi_grf_reg_fields,
	.max_bit_rate_per_lane = 2000000000UL,
	.soc_type = RK1808,
};

static const u32 rk3128_dsi_grf_reg_fields[MAX_FIELDS] = {
	[FORCETXSTOPMODE]	= GRF_REG_FIELD(0x0150, 10, 13),
	[FORCERXMODE]		= GRF_REG_FIELD(0x0150,  9,  9),
	[TURNDISABLE]		= GRF_REG_FIELD(0x0150,  8,  8),
	[DPICOLORM]		= GRF_REG_FIELD(0x0150,  5,  5),
	[DPISHUTDN]		= GRF_REG_FIELD(0x0150,  4,  4),
};

static const struct dw_mipi_dsi_plat_data rk3128_socdata = {
	.dsi0_grf_reg_fields = rk3128_dsi_grf_reg_fields,
	.max_bit_rate_per_lane = 1000000000UL,
	.soc_type = RK3126,
};

static const u32 rk3288_dsi0_grf_reg_fields[MAX_FIELDS] = {
	[DPICOLORM]		= GRF_REG_FIELD(0x025c,  8,  8),
	[DPISHUTDN]		= GRF_REG_FIELD(0x025c,  7,  7),
	[VOPSEL]		= GRF_REG_FIELD(0x025c,  6,  6),
	[FORCETXSTOPMODE]	= GRF_REG_FIELD(0x0264,  8, 11),
	[FORCERXMODE]		= GRF_REG_FIELD(0x0264,  4,  7),
	[TURNDISABLE]		= GRF_REG_FIELD(0x0264,  0,  3),
	[TURNREQUEST]		= GRF_REG_FIELD(0x03a4,  8, 10),
	[DPIUPDATECFG]		= GRF_REG_FIELD(0x03a8,  0,  0),
};

static const u32 rk3288_dsi1_grf_reg_fields[MAX_FIELDS] = {
	[DPICOLORM]		= GRF_REG_FIELD(0x025c, 11, 11),
	[DPISHUTDN]		= GRF_REG_FIELD(0x025c, 10, 10),
	[VOPSEL]		= GRF_REG_FIELD(0x025c,  9,  9),
	[ENABLE_N]		= GRF_REG_FIELD(0x0268, 12, 15),
	[FORCETXSTOPMODE]	= GRF_REG_FIELD(0x0268,  8, 11),
	[FORCERXMODE]		= GRF_REG_FIELD(0x0268,  4,  7),
	[TURNDISABLE]		= GRF_REG_FIELD(0x0268,  0,  3),
	[BASEDIR]		= GRF_REG_FIELD(0x027c, 15, 15),
	[MASTERSLAVEZ]		= GRF_REG_FIELD(0x027c, 14, 14),
	[ENABLECLK]		= GRF_REG_FIELD(0x027c, 12, 12),
	[TURNREQUEST]		= GRF_REG_FIELD(0x03a4,  4,  7),
	[DPIUPDATECFG]		= GRF_REG_FIELD(0x03a8,  1,  1),
};

static const struct dw_mipi_dsi_plat_data rk3288_socdata = {
	.dsi0_grf_reg_fields = rk3288_dsi0_grf_reg_fields,
	.dsi1_grf_reg_fields = rk3288_dsi1_grf_reg_fields,
	.max_bit_rate_per_lane = 1500000000UL,
	.soc_type = RK3288,
};

static const u32 rk3366_dsi_grf_reg_fields[MAX_FIELDS] = {
	[VOPSEL]		= GRF_REG_FIELD(0x0400,  2,  2),
	[DPIUPDATECFG]		= GRF_REG_FIELD(0x0410,  9,  9),
	[DPICOLORM]		= GRF_REG_FIELD(0x0410,  3,  3),
	[DPISHUTDN]		= GRF_REG_FIELD(0x0410,  2,  2),
	[FORCETXSTOPMODE]	= GRF_REG_FIELD(0x0414,  7, 10),
	[FORCERXMODE]		= GRF_REG_FIELD(0x0414,  6,  6),
	[TURNDISABLE]		= GRF_REG_FIELD(0x0414,  5,  5),
};

static const struct dw_mipi_dsi_plat_data rk3366_socdata = {
	.dsi0_grf_reg_fields = rk3366_dsi_grf_reg_fields,
	.max_bit_rate_per_lane = 1000000000UL,
	.soc_type = RK3366,
};

static const u32 rk3368_dsi_grf_reg_fields[MAX_FIELDS] = {
	[DPIUPDATECFG]		= GRF_REG_FIELD(0x0418,  7,  7),
	[DPICOLORM]		= GRF_REG_FIELD(0x0418,  3,  3),
	[DPISHUTDN]		= GRF_REG_FIELD(0x0418,  2,  2),
	[FORCETXSTOPMODE]	= GRF_REG_FIELD(0x041c,  7, 10),
	[FORCERXMODE]		= GRF_REG_FIELD(0x041c,  6,  6),
	[TURNDISABLE]		= GRF_REG_FIELD(0x041c,  5,  5),
};

static const struct dw_mipi_dsi_plat_data rk3368_socdata = {
	.dsi0_grf_reg_fields = rk3368_dsi_grf_reg_fields,
	.max_bit_rate_per_lane = 1000000000UL,
	.soc_type = RK3368,
};

static const u32 rk3399_dsi0_grf_reg_fields[MAX_FIELDS] = {
	[DPIUPDATECFG]		= GRF_REG_FIELD(0x6224, 15, 15),
	[DPISHUTDN]		= GRF_REG_FIELD(0x6224, 14, 14),
	[DPICOLORM]		= GRF_REG_FIELD(0x6224, 13, 13),
	[VOPSEL]		= GRF_REG_FIELD(0x6250,  0,  0),
	[TURNREQUEST]		= GRF_REG_FIELD(0x6258, 12, 15),
	[TURNDISABLE]		= GRF_REG_FIELD(0x6258,  8, 11),
	[FORCETXSTOPMODE]	= GRF_REG_FIELD(0x6258,  4,  7),
	[FORCERXMODE]		= GRF_REG_FIELD(0x6258,  0,  3),
};

static const u32 rk3399_dsi1_grf_reg_fields[MAX_FIELDS] = {
	[VOPSEL]		= GRF_REG_FIELD(0x6250,  4,  4),
	[DPIUPDATECFG]		= GRF_REG_FIELD(0x6250,  3,  3),
	[DPISHUTDN]		= GRF_REG_FIELD(0x6250,  2,  2),
	[DPICOLORM]		= GRF_REG_FIELD(0x6250,  1,  1),
	[TURNDISABLE]		= GRF_REG_FIELD(0x625c, 12, 15),
	[FORCETXSTOPMODE]	= GRF_REG_FIELD(0x625c,  8, 11),
	[FORCERXMODE]		= GRF_REG_FIELD(0x625c,  4,  7),
	[ENABLE_N]		= GRF_REG_FIELD(0x625c,  0,  3),
	[MASTERSLAVEZ]		= GRF_REG_FIELD(0x6260,  7,  7),
	[ENABLECLK]		= GRF_REG_FIELD(0x6260,  6,  6),
	[BASEDIR]		= GRF_REG_FIELD(0x6260,  5,  5),
	[TURNREQUEST]		= GRF_REG_FIELD(0x6260,  0,  3),
};

static const struct dw_mipi_dsi_plat_data rk3399_socdata = {
	.dsi0_grf_reg_fields = rk3399_dsi0_grf_reg_fields,
	.dsi1_grf_reg_fields = rk3399_dsi1_grf_reg_fields,
	.max_bit_rate_per_lane = 1500000000UL,
	.soc_type = RK3399,
};

static const struct of_device_id dw_mipi_dsi_dt_ids[] = {
	{ .compatible = "rockchip,px30-mipi-dsi", .data = &px30_socdata, },
	{ .compatible = "rockchip,rk1808-mipi-dsi", .data = &rk1808_socdata, },
	{ .compatible = "rockchip,rk3128-mipi-dsi", .data = &rk3128_socdata, },
	{ .compatible = "rockchip,rk3288-mipi-dsi", .data = &rk3288_socdata, },
	{ .compatible = "rockchip,rk3366-mipi-dsi", .data = &rk3366_socdata, },
	{ .compatible = "rockchip,rk3368-mipi-dsi", .data = &rk3368_socdata, },
	{ .compatible = "rockchip,rk3399-mipi-dsi", .data = &rk3399_socdata, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dw_mipi_dsi_dt_ids);

static struct platform_driver dw_mipi_dsi_driver = {
	.probe		= dw_mipi_dsi_probe,
	.remove		= dw_mipi_dsi_remove,
	.driver		= {
		.of_match_table = dw_mipi_dsi_dt_ids,
		.pm = &dw_mipi_dsi_pm_ops,
		.name	= DRIVER_NAME,
	},
};
module_platform_driver(dw_mipi_dsi_driver);

MODULE_DESCRIPTION("ROCKCHIP MIPI DSI host controller driver");
MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
