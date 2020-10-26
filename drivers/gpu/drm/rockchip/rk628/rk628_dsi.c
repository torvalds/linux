// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/mfd/rk628.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>

#include <video/of_display_timing.h>
#include <video/mipi_display.h>
#include <video/videomode.h>
#include <asm/unaligned.h>

#define DSI_VERSION			0x0000
#define DSI_PWR_UP			0x0004
#define RESET				0
#define POWER_UP			BIT(0)
#define DSI_CLKMGR_CFG			0x0008
#define TO_CLK_DIVISION(x)		UPDATE(x, 15,  8)
#define TX_ESC_CLK_DIVISION(x)		UPDATE(x,  7,  0)
#define DSI_DPI_VCID			0x000c
#define DPI_VID(x)			UPDATE(x,  1,  0)
#define DSI_DPI_COLOR_CODING		0x0010
#define LOOSELY18_EN			BIT(8)
#define DPI_COLOR_CODING(x)		UPDATE(x,  3,  0)
#define DSI_DPI_CFG_POL			0x0014
#define COLORM_ACTIVE_LOW		BIT(4)
#define SHUTD_ACTIVE_LOW		BIT(3)
#define HSYNC_ACTIVE_LOW		BIT(2)
#define VSYNC_ACTIVE_LOW		BIT(1)
#define DATAEN_ACTIVE_LOW		BIT(0)
#define DSI_DPI_LP_CMD_TIM		0x0018
#define OUTVACT_LPCMD_TIME(x)		UPDATE(x, 23, 16)
#define INVACT_LPCMD_TIME(x)		UPDATE(x,  7,  0)
#define DSI_PCKHDL_CFG			0x002c
#define CRC_RX_EN			BIT(4)
#define ECC_RX_EN			BIT(3)
#define BTA_EN				BIT(2)
#define EOTP_RX_EN			BIT(1)
#define EOTP_TX_EN			BIT(0)
#define DSI_GEN_VCID			0x0030
#define DSI_MODE_CFG			0x0034
#define CMD_VIDEO_MODE(x)		UPDATE(x,  0,  0)
#define DSI_VID_MODE_CFG		0x0038
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
#define DSI_VID_PKT_SIZE		0x003c
#define VID_PKT_SIZE(x)			UPDATE(x, 13,  0)
#define DSI_VID_NUM_CHUNKS		0x0040
#define DSI_VID_NULL_SIZE		0x0044
#define DSI_VID_HSA_TIME		0x0048
#define VID_HSA_TIME(x)			UPDATE(x, 11,  0)
#define DSI_VID_HBP_TIME		0x004c
#define VID_HBP_TIME(x)			UPDATE(x, 11,  0)
#define DSI_VID_HLINE_TIME		0x0050
#define VID_HLINE_TIME(x)		UPDATE(x, 14,  0)
#define DSI_VID_VSA_LINES		0x0054
#define VSA_LINES(x)			UPDATE(x,  9,  0)
#define DSI_VID_VBP_LINES		0x0058
#define VBP_LINES(x)			UPDATE(x,  9,  0)
#define DSI_VID_VFP_LINES		0x005c
#define VFP_LINES(x)			UPDATE(x,  9,  0)
#define DSI_VID_VACTIVE_LINES		0x0060
#define V_ACTIVE_LINES(x)		UPDATE(x, 13,  0)
#define DSI_EDPI_CMD_SIZE		0x0064
#define EDPI_ALLOWED_CMD_SIZE(x)	UPDATE(x, 15,  0)
#define DSI_CMD_MODE_CFG		0x0068
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
#define DSI_GEN_HDR			0x006c
#define GEN_WC_MSBYTE(x)		UPDATE(x, 23, 16)
#define GEN_WC_LSBYTE(x)		UPDATE(x, 15,  8)
#define GEN_VC(x)			UPDATE(x,  7,  6)
#define GEN_DT(x)			UPDATE(x,  5,  0)
#define DSI_GEN_PLD_DATA		0x0070
#define DSI_CMD_PKT_STATUS		0x0074
#define GEN_RD_CMD_BUSY			BIT(6)
#define GEN_PLD_R_FULL			BIT(5)
#define GEN_PLD_R_EMPTY			BIT(4)
#define GEN_PLD_W_FULL			BIT(3)
#define GEN_PLD_W_EMPTY			BIT(2)
#define GEN_CMD_FULL			BIT(1)
#define GEN_CMD_EMPTY			BIT(0)
#define DSI_TO_CNT_CFG			0x0078
#define HSTX_TO_CNT(x)			UPDATE(x, 31, 16)
#define LPRX_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_HS_RD_TO_CNT		0x007c
#define HS_RD_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_LP_RD_TO_CNT		0x0080
#define LP_RD_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_HS_WR_TO_CNT		0x0084
#define HS_WR_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_LP_WR_TO_CNT		0x0088
#define LP_WR_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_BTA_TO_CNT			0x008c
#define BTA_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_SDF_3D			0x0090
#define DSI_LPCLK_CTRL			0x0094
#define AUTO_CLKLANE_CTRL		BIT(1)
#define PHY_TXREQUESTCLKHS		BIT(0)
#define DSI_PHY_TMR_LPCLK_CFG		0x0098
#define PHY_CLKHS2LP_TIME(x)		UPDATE(x, 25, 16)
#define PHY_CLKLP2HS_TIME(x)		UPDATE(x,  9,  0)
#define DSI_PHY_TMR_CFG			0x009c
#define PHY_HS2LP_TIME(x)		UPDATE(x, 31, 24)
#define PHY_LP2HS_TIME(x)		UPDATE(x, 23, 16)
#define MAX_RD_TIME(x)			UPDATE(x, 14,  0)
#define DSI_PHY_RSTZ			0x00a0
#define PHY_FORCEPLL			BIT(3)
#define PHY_ENABLECLK			BIT(2)
#define PHY_RSTZ			BIT(1)
#define PHY_SHUTDOWNZ			BIT(0)
#define DSI_PHY_IF_CFG			0x00a4
#define PHY_STOP_WAIT_TIME(x)		UPDATE(x, 15,  8)
#define N_LANES(x)			UPDATE(x,  1,  0)
#define DSI_PHY_STATUS			0x00b0
#define PHY_STOPSTATE3LANE		BIT(11)
#define PHY_STOPSTATE2LANE		BIT(9)
#define PHY_STOPSTATE1LANE		BIT(7)
#define PHY_STOPSTATE0LANE		BIT(4)
#define PHY_STOPSTATECLKLANE		BIT(2)
#define PHY_LOCK			BIT(0)
#define PHY_STOPSTATELANE		(PHY_STOPSTATE0LANE | \
					 PHY_STOPSTATECLKLANE)
#define DSI_INT_ST0			0x00bc
#define DSI_INT_ST1			0x00c0
#define DSI_INT_MSK0			0x00c4
#define DSI_INT_MSK1			0x00c8
#define DSI_INT_FORCE0			0x00d8
#define DSI_INT_FORCE1			0x00dc
#define DSI_MAX_REGISTER		DSI_INT_FORCE1

/* Test Code: 0x44 (HS RX Control of Lane 0) */
#define HSFREQRANGE(x)			UPDATE(x, 6, 1)

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

struct rk628_dsi_data {
	u32 reg_base;
	u8 id;
};

struct rk628_dsi {
	struct drm_bridge base;
	struct drm_connector connector;
	struct drm_display_mode mode;
	struct drm_panel *panel;

	struct device *dev;
	struct rk628 *parent;
	struct mipi_dsi_host host;
	struct phy *phy;
	struct clk *pclk;
	struct clk *cfgclk;
	struct reset_control *rst;
	struct regmap *grf;
	struct regmap *regmap;
	struct regmap *testif;
	struct regmap_config config;
	struct regmap_access_table rd_table;
	struct regmap_range range;
	int irq;
	u32 reg_base;
	u8 id;

	struct rk628_dsi *master;
	struct rk628_dsi *slave;
	unsigned int lane_mbps;
	u32 channel;
	u32 lanes;
	u32 format;
	unsigned long mode_flags;
};

static inline struct rk628_dsi *bridge_to_dsi(struct drm_bridge *b)
{
	return container_of(b, struct rk628_dsi, base);
}

static inline struct rk628_dsi *host_to_dsi(struct mipi_dsi_host *h)
{
	return container_of(h, struct rk628_dsi, host);
}

static inline struct rk628_dsi *connector_to_dsi(struct drm_connector *c)
{
	return container_of(c, struct rk628_dsi, connector);
}

static inline void dsi_write(struct rk628_dsi *dsi, u32 reg, u32 val)
{
	regmap_write(dsi->regmap, dsi->reg_base + reg, val);
}

static inline u32 dsi_read(struct rk628_dsi *dsi, u32 reg)
{
	u32 val;

	regmap_read(dsi->regmap, dsi->reg_base + reg, &val);

	return val;
}

static inline void dsi_update_bits(struct rk628_dsi *dsi, u32 reg, u32 mask,
				   u32 val)
{
	u32 orig, tmp;

	orig = dsi_read(dsi, reg);
	tmp = orig & ~mask;
	tmp |= val & mask;
	dsi_write(dsi, reg, tmp);
}

static inline void dpishutdn_assert(struct rk628_dsi *dsi)
{
	regmap_update_bits(dsi->grf, dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   DPISHUTDN, 1);
}

static inline void dpishutdn_deassert(struct rk628_dsi *dsi)
{
	regmap_update_bits(dsi->grf, dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   DPISHUTDN, 0);
}

static int genif_wait_w_pld_fifo_not_full(struct rk628_dsi *dsi)
{
	u32 sts;
	int ret;

	ret = regmap_read_poll_timeout(dsi->regmap,
				       dsi->reg_base + DSI_CMD_PKT_STATUS,
				       sts, !(sts & GEN_PLD_W_FULL),
				       0, 1000);
	if (ret < 0) {
		dev_err(dsi->dev, "generic write payload fifo is full\n");
		return ret;
	}

	return 0;
}

static int genif_wait_cmd_fifo_not_full(struct rk628_dsi *dsi)
{
	u32 sts;
	int ret;

	ret = regmap_read_poll_timeout(dsi->regmap,
				       dsi->reg_base + DSI_CMD_PKT_STATUS,
				       sts, !(sts & GEN_CMD_FULL),
				       0, 1000);
	if (ret < 0) {
		dev_err(dsi->dev, "generic write cmd fifo is full\n");
		return ret;
	}

	return 0;
}

static int genif_wait_write_fifo_empty(struct rk628_dsi *dsi)
{
	u32 sts;
	u32 mask;
	int ret;

	mask = GEN_CMD_EMPTY | GEN_PLD_W_EMPTY;
	ret = regmap_read_poll_timeout(dsi->regmap,
				       dsi->reg_base + DSI_CMD_PKT_STATUS,
				       sts, (sts & mask) == mask,
				       0, 1000);
	if (ret < 0) {
		dev_err(dsi->dev, "generic write fifo is full\n");
		return ret;
	}

	return 0;
}

static inline void testif_testclk_assert(struct rk628_dsi *dsi)
{
	regmap_update_bits(dsi->grf, dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   PHY_TESTCLK, PHY_TESTCLK);
	udelay(1);
}

static inline void testif_testclk_deassert(struct rk628_dsi *dsi)
{
	regmap_update_bits(dsi->grf, dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   PHY_TESTCLK, 0);
	udelay(1);
}

static inline void testif_testclr_assert(struct rk628_dsi *dsi)
{
	regmap_update_bits(dsi->grf, dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   PHY_TESTCLR, PHY_TESTCLR);
	udelay(1);
}

static inline void testif_testclr_deassert(struct rk628_dsi *dsi)
{
	regmap_update_bits(dsi->grf, dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   PHY_TESTCLR, 0);
	udelay(1);
}

static inline void testif_testen_assert(struct rk628_dsi *dsi)
{
	regmap_update_bits(dsi->grf, dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   PHY_TESTEN, PHY_TESTEN);
	udelay(1);
}

static inline void testif_testen_deassert(struct rk628_dsi *dsi)
{
	regmap_update_bits(dsi->grf,  dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   PHY_TESTEN, 0);
	udelay(1);
}

static inline void testif_set_data(struct rk628_dsi *dsi, u8 data)
{
	regmap_update_bits(dsi->grf, dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   PHY_TESTDIN_MASK, PHY_TESTDIN(data));
	udelay(1);
}

static inline u8 testif_get_data(struct rk628_dsi *dsi)
{
	u32 data = 0;

	regmap_read(dsi->grf, dsi->id ?
		    GRF_DPHY1_STATUS : GRF_DPHY0_STATUS, &data);

	return data >> PHY_TESTDOUT_SHIFT;
}

static void testif_test_code_write(struct rk628_dsi *dsi, u8 test_code)
{
	testif_testclk_assert(dsi);
	testif_set_data(dsi, test_code);
	testif_testen_assert(dsi);
	testif_testclk_deassert(dsi);
	testif_testen_deassert(dsi);
}

static void testif_test_data_write(struct rk628_dsi *dsi, u8 test_data)
{
	testif_testclk_deassert(dsi);
	testif_set_data(dsi, test_data);
	testif_testclk_assert(dsi);
}

static int testif_write(void *context, unsigned int reg, unsigned int value)
{
	struct rk628_dsi *dsi = context;
	u8 monitor_data;

	testif_test_code_write(dsi, reg);
	testif_test_data_write(dsi, value);
	monitor_data = testif_get_data(dsi);

	dev_dbg(dsi->dev,
		"test_code=0x%02x, test_data=0x%02x, monitor_data=0x%02x\n",
		reg, value, monitor_data);

	return 0;
}

static int testif_read(void *context, unsigned int reg, unsigned int *value)
{
	struct rk628_dsi *dsi = context;

	testif_test_code_write(dsi, reg);
	*value = testif_get_data(dsi);
	testif_test_data_write(dsi, *value);

	return 0;
}

static inline void mipi_dphy_enableclk_assert(struct rk628_dsi *dsi)
{
	dsi_update_bits(dsi, DSI_PHY_RSTZ, PHY_ENABLECLK, PHY_ENABLECLK);
	udelay(1);
}

static inline void mipi_dphy_enableclk_deassert(struct rk628_dsi *dsi)
{
	dsi_update_bits(dsi, DSI_PHY_RSTZ, PHY_ENABLECLK, 0);
	udelay(1);
}

static inline void mipi_dphy_shutdownz_assert(struct rk628_dsi *dsi)
{
	dsi_update_bits(dsi, DSI_PHY_RSTZ, PHY_SHUTDOWNZ, 0);
	udelay(1);
}

static inline void mipi_dphy_shutdownz_deassert(struct rk628_dsi *dsi)
{
	dsi_update_bits(dsi, DSI_PHY_RSTZ, PHY_SHUTDOWNZ, PHY_SHUTDOWNZ);
	udelay(1);
}

static inline void mipi_dphy_rstz_assert(struct rk628_dsi *dsi)
{
	dsi_update_bits(dsi, DSI_PHY_RSTZ, PHY_RSTZ, 0);
	udelay(1);
}

static inline void mipi_dphy_rstz_deassert(struct rk628_dsi *dsi)
{
	dsi_update_bits(dsi, DSI_PHY_RSTZ, PHY_RSTZ, PHY_RSTZ);
	udelay(1);
}

static void mipi_dphy_init(struct rk628_dsi *dsi)
{
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
	u8 hsfreqrange;
	unsigned int index;

	for (index = 0; index < ARRAY_SIZE(hsfreqrange_table); index++)
		if (dsi->lane_mbps <= hsfreqrange_table[index].max_lane_mbps)
			break;

	if (index == ARRAY_SIZE(hsfreqrange_table))
		--index;

	hsfreqrange = hsfreqrange_table[index].hsfreqrange;
	regmap_write(dsi->testif, 0x44, HSFREQRANGE(hsfreqrange));
}

static int mipi_dphy_power_on(struct rk628_dsi *dsi)
{
	unsigned int val, mask;
	int ret;

	mipi_dphy_enableclk_deassert(dsi);
	mipi_dphy_shutdownz_assert(dsi);
	mipi_dphy_rstz_assert(dsi);
	testif_testclr_assert(dsi);

	/* Set all REQUEST inputs to zero */
	regmap_write(dsi->grf, dsi->id ?
		     GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
		     FORCETXSTOPMODE(0) | FORCERXMODE(0));
	udelay(1);

	testif_testclr_deassert(dsi);
	mipi_dphy_init(dsi);

	mipi_dphy_enableclk_assert(dsi);
	mipi_dphy_shutdownz_deassert(dsi);
	mipi_dphy_rstz_deassert(dsi);
	usleep_range(1500, 2000);

	phy_power_on(dsi->phy);

	ret = regmap_read_poll_timeout(dsi->regmap, dsi->reg_base + DSI_PHY_STATUS,
				       val, val & PHY_LOCK, 0, 1000);
	if (ret < 0) {
		dev_err(dsi->dev, "PHY is not locked\n");
		return ret;
	}

	usleep_range(100, 200);

	mask = PHY_STOPSTATELANE;
	ret = regmap_read_poll_timeout(dsi->regmap, dsi->reg_base + DSI_PHY_STATUS,
				       val, (val & mask) == mask,
				       0, 1000);
	if (ret < 0) {
		dev_err(dsi->dev, "lane module is not in stop state\n");
		return ret;
	}

	udelay(10);

	return 0;
}

static void mipi_dphy_power_off(struct rk628_dsi *dsi)
{
	dsi_write(dsi, DSI_PHY_RSTZ, 0);
	phy_power_off(dsi->phy);
}

static int rk628_dsi_turn_on_peripheral(struct rk628_dsi *dsi)
{
	dpishutdn_assert(dsi);
	udelay(20);
	dpishutdn_deassert(dsi);

	return 0;
}

static int rk628_dsi_shutdown_peripheral(struct rk628_dsi *dsi)
{
	dpishutdn_deassert(dsi);
	udelay(20);
	dpishutdn_assert(dsi);

	return 0;
}

static int rk628_dsi_host_attach(struct mipi_dsi_host *host,
				 struct mipi_dsi_device *device)
{
	struct rk628_dsi *dsi = host_to_dsi(host);

	if (device->lanes < 1 || device->lanes > 8)
		return -EINVAL;

	dsi->lanes = device->lanes;
	dsi->channel = device->channel;
	dsi->format = device->format;
	dsi->mode_flags = device->mode_flags;

	dsi->panel = of_drm_find_panel(device->dev.of_node);
	if (!dsi->panel)
		return -EPROBE_DEFER;

	if (dsi->lanes > 4) {
		struct device *d = bus_find_device_by_name(&platform_bus_type,
							   NULL, "rk628-dsi1");
		struct rk628_dsi *slave;

		if (!d)
			return -EPROBE_DEFER;

		slave = dev_get_drvdata(d);
		if (!slave)
			return -EPROBE_DEFER;

		dsi->slave = slave;
		dsi->lanes /= 2;
		slave->master = dsi;
		slave->lanes = dsi->lanes;
		slave->channel = dsi->channel;
		slave->format = dsi->format;
		slave->mode_flags = dsi->mode_flags;
	}

	return 0;
}

static int rk628_dsi_host_detach(struct mipi_dsi_host *host,
				 struct mipi_dsi_device *device)
{
	return 0;
}

static int rk628_dsi_read_from_fifo(struct rk628_dsi *dsi,
				    const struct mipi_dsi_msg *msg)
{
	u8 *payload = msg->rx_buf;
	unsigned int vrefresh = drm_mode_vrefresh(&dsi->mode);
	u16 length;
	u32 val;
	int ret;

	ret = regmap_read_poll_timeout(dsi->regmap,
				       dsi->reg_base + DSI_CMD_PKT_STATUS,
				       val, !(val & GEN_RD_CMD_BUSY),
				       0, DIV_ROUND_UP(1000000, vrefresh));
	if (ret) {
		dev_err(dsi->dev, "entire response isn't stored in the FIFO\n");
		return ret;
	}

	/* Receive payload */
	for (length = msg->rx_len; length; length -= 4) {
		ret = regmap_read_poll_timeout(dsi->regmap,
					       dsi->reg_base + DSI_CMD_PKT_STATUS,
					       val, !(val & GEN_PLD_R_EMPTY),
					       0, 1000);
		if (ret) {
			dev_err(dsi->dev, "Read payload FIFO is empty\n");
			return ret;
		}

		val = dsi_read(dsi, DSI_GEN_PLD_DATA);

		switch (length) {
		case 3:
			payload[2] = (val >> 16) & 0xff;
			/* fallthrough */
		case 2:
			payload[1] = (val >> 8) & 0xff;
			/* fallthrough */
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

static ssize_t rk628_dsi_transfer(struct rk628_dsi *dsi,
				  const struct mipi_dsi_msg *msg)
{
	struct mipi_dsi_packet packet;
	int ret;
	u32 val;

	if (msg->flags & MIPI_DSI_MSG_REQ_ACK)
		dsi_update_bits(dsi, DSI_CMD_MODE_CFG,
				ACK_RQST_EN, ACK_RQST_EN);

	if (msg->flags & MIPI_DSI_MSG_USE_LPM) {
		dsi_update_bits(dsi, DSI_VID_MODE_CFG, LP_CMD_EN, LP_CMD_EN);
	} else {
		dsi_update_bits(dsi, DSI_VID_MODE_CFG, LP_CMD_EN, 0);
		dsi_update_bits(dsi, DSI_LPCLK_CTRL,
				PHY_TXREQUESTCLKHS, PHY_TXREQUESTCLKHS);
	}

	switch (msg->type) {
	case MIPI_DSI_SHUTDOWN_PERIPHERAL:
		return rk628_dsi_shutdown_peripheral(dsi);
	case MIPI_DSI_TURN_ON_PERIPHERAL:
		return rk628_dsi_turn_on_peripheral(dsi);
	case MIPI_DSI_DCS_SHORT_WRITE:
		dsi_update_bits(dsi, DSI_CMD_MODE_CFG, DCS_SW_0P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				DCS_SW_0P_TX : 0);
		break;
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		dsi_update_bits(dsi, DSI_CMD_MODE_CFG, DCS_SW_1P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				DCS_SW_1P_TX : 0);
		break;
	case MIPI_DSI_DCS_LONG_WRITE:
		dsi_update_bits(dsi, DSI_CMD_MODE_CFG, DCS_LW_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				DCS_LW_TX : 0);
		break;
	case MIPI_DSI_DCS_READ:
		dsi_update_bits(dsi, DSI_CMD_MODE_CFG, DCS_SR_0P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				DCS_SR_0P_TX : 0);
		break;
	case MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE:
		dsi_update_bits(dsi, DSI_CMD_MODE_CFG, MAX_RD_PKT_SIZE,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				MAX_RD_PKT_SIZE : 0);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
		dsi_update_bits(dsi, DSI_CMD_MODE_CFG, GEN_SW_0P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				GEN_SW_0P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
		dsi_update_bits(dsi, DSI_CMD_MODE_CFG, GEN_SW_1P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				GEN_SW_1P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		dsi_update_bits(dsi, DSI_CMD_MODE_CFG, GEN_SW_2P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				GEN_SW_2P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_LONG_WRITE:
		dsi_update_bits(dsi, DSI_CMD_MODE_CFG, GEN_LW_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				GEN_LW_TX : 0);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
		dsi_update_bits(dsi, DSI_CMD_MODE_CFG, GEN_SR_0P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				GEN_SR_0P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
		dsi_update_bits(dsi, DSI_CMD_MODE_CFG, GEN_SR_1P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				GEN_SR_1P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
		dsi_update_bits(dsi, DSI_CMD_MODE_CFG, GEN_SR_2P_TX,
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

		val = get_unaligned_le32(packet.payload);
		dsi_write(dsi, DSI_GEN_PLD_DATA, val);

		packet.payload += 4;
		packet.payload_length -= 4;
	}

	val = 0;
	switch (packet.payload_length) {
	case 3:
		val |= packet.payload[2] << 16;
		/* fallthrough */
	case 2:
		val |= packet.payload[1] << 8;
		/* fallthrough */
	case 1:
		val |= packet.payload[0];
		dsi_write(dsi, DSI_GEN_PLD_DATA, val);
		break;
	}

	ret = genif_wait_cmd_fifo_not_full(dsi);
	if (ret)
		return ret;

	/* Send packet header */
	val = get_unaligned_le32(packet.header);
	dsi_write(dsi, DSI_GEN_HDR, val);

	ret = genif_wait_write_fifo_empty(dsi);
	if (ret)
		return ret;

	if (msg->rx_len) {
		ret = rk628_dsi_read_from_fifo(dsi, msg);
		if (ret < 0)
			return ret;
	}

	if (dsi->slave)
		rk628_dsi_transfer(dsi->slave, msg);

	return msg->tx_len;
}

static ssize_t rk628_dsi_host_transfer(struct mipi_dsi_host *host,
				       const struct mipi_dsi_msg *msg)
{
	struct rk628_dsi *dsi = host_to_dsi(host);

	return rk628_dsi_transfer(dsi, msg);
}

static const struct mipi_dsi_host_ops rk628_dsi_host_ops = {
	.attach = rk628_dsi_host_attach,
	.detach = rk628_dsi_host_detach,
	.transfer = rk628_dsi_host_transfer,
};

static struct drm_encoder *
rk628_dsi_connector_best_encoder(struct drm_connector *connector)
{
	struct rk628_dsi *dsi = connector_to_dsi(connector);

	return dsi->base.encoder;
}

static int rk628_dsi_connector_get_modes(struct drm_connector *connector)
{
	struct rk628_dsi *dsi = connector_to_dsi(connector);

	return drm_panel_get_modes(dsi->panel);
}

static struct drm_connector_helper_funcs rk628_dsi_connector_helper_funcs = {
	.get_modes = rk628_dsi_connector_get_modes,
	.best_encoder = rk628_dsi_connector_best_encoder,
};

static void rk628_dsi_drm_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs rk628_dsi_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rk628_dsi_drm_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void rk628_dsi_set_vid_mode(struct rk628_dsi *dsi)
{
	struct drm_display_mode *mode = &dsi->mode;
	unsigned int lanebyteclk = (dsi->lane_mbps * USEC_PER_MSEC) >> 3;
	unsigned int dpipclk = mode->clock;
	u32 hline, hsa, hbp, hline_time, hsa_time, hbp_time;
	u32 vactive, vsa, vfp, vbp;
	u32 val;

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

	dsi_write(dsi, DSI_VID_MODE_CFG, val);

	if (dsi->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS)
		dsi_update_bits(dsi, DSI_LPCLK_CTRL,
				AUTO_CLKLANE_CTRL, AUTO_CLKLANE_CTRL);

	dsi_write(dsi, DSI_VID_PKT_SIZE, VID_PKT_SIZE(mode->hdisplay));

	vactive = mode->vdisplay;
	vsa = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;
	hsa = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;
	hline = mode->htotal;

	hline_time = DIV_ROUND_CLOSEST_ULL(hline * lanebyteclk, dpipclk);
	dsi_write(dsi, DSI_VID_HLINE_TIME, VID_HLINE_TIME(hline_time));
	hsa_time = DIV_ROUND_CLOSEST_ULL(hsa * lanebyteclk, dpipclk);
	dsi_write(dsi, DSI_VID_HSA_TIME, VID_HSA_TIME(hsa_time));
	hbp_time = DIV_ROUND_CLOSEST_ULL(hbp * lanebyteclk, dpipclk);
	dsi_write(dsi, DSI_VID_HBP_TIME, VID_HBP_TIME(hbp_time));

	dsi_write(dsi, DSI_VID_VACTIVE_LINES, vactive);
	dsi_write(dsi, DSI_VID_VSA_LINES, vsa);
	dsi_write(dsi, DSI_VID_VFP_LINES, vfp);
	dsi_write(dsi, DSI_VID_VBP_LINES, vbp);

	dsi_write(dsi, DSI_MODE_CFG, CMD_VIDEO_MODE(VIDEO_MODE));
}

static void rk628_dsi_set_cmd_mode(struct rk628_dsi *dsi)
{
	struct drm_display_mode *mode = &dsi->mode;

	dsi_update_bits(dsi, DSI_CMD_MODE_CFG, DCS_LW_TX, 0);
	dsi_write(dsi, DSI_EDPI_CMD_SIZE,
		  EDPI_ALLOWED_CMD_SIZE(mode->hdisplay));
	dsi_write(dsi, DSI_MODE_CFG, CMD_VIDEO_MODE(COMMAND_MODE));
}

static void rk628_dsi_disable(struct rk628_dsi *dsi)
{
	dsi_write(dsi, DSI_PWR_UP, RESET);
	dsi_write(dsi, DSI_LPCLK_CTRL, 0);
	dsi_write(dsi, DSI_EDPI_CMD_SIZE, 0);
	dsi_write(dsi, DSI_MODE_CFG, CMD_VIDEO_MODE(COMMAND_MODE));
	dsi_write(dsi, DSI_PWR_UP, POWER_UP);

	if (dsi->slave)
		rk628_dsi_disable(dsi->slave);
}

static void rk628_dsi_post_disable(struct rk628_dsi *dsi)
{
	dsi_write(dsi, DSI_INT_MSK0, 0);
	dsi_write(dsi, DSI_INT_MSK1, 0);
	dsi_write(dsi, DSI_PWR_UP, RESET);
	mipi_dphy_power_off(dsi);

	clk_disable_unprepare(dsi->cfgclk);
	clk_disable_unprepare(dsi->pclk);

	if (dsi->slave)
		rk628_dsi_post_disable(dsi->slave);
}

static unsigned int rk628_dsi_get_lane_rate(struct rk628_dsi *dsi)
{
	struct device *dev = dsi->dev;
	const struct drm_display_mode *mode = &dsi->mode;
	unsigned int max_lane_rate = 1500;
	unsigned int lane_rate;
	unsigned int value;
	int bpp, lanes;

	/* optional override of the desired bandwidth */
	if (!of_property_read_u32(dev->of_node, "rockchip,lane-rate", &value))
		return value;

	bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
	if (bpp < 0)
		bpp = 24;

	lanes = dsi->slave ? dsi->lanes * 2 : dsi->lanes;
	lane_rate = mode->clock / 1000 * bpp / lanes;
	lane_rate = DIV_ROUND_UP(lane_rate * 5, 4);

	if (lane_rate > max_lane_rate)
		lane_rate = max_lane_rate;

	return lane_rate;
}

static void rk628_dsi_pre_enable(struct rk628_dsi *dsi)
{
	u32 val;

	clk_prepare_enable(dsi->pclk);
	clk_prepare_enable(dsi->cfgclk);
	reset_control_assert(dsi->rst);
	usleep_range(20, 40);
	reset_control_deassert(dsi->rst);
	usleep_range(20, 40);

	dsi_write(dsi, DSI_PWR_UP, RESET);
	dsi_write(dsi, DSI_MODE_CFG, CMD_VIDEO_MODE(COMMAND_MODE));

	val = DIV_ROUND_UP(dsi->lane_mbps >> 3, 20);
	dsi_write(dsi, DSI_CLKMGR_CFG,
		  TO_CLK_DIVISION(10) | TX_ESC_CLK_DIVISION(val));

	val = CRC_RX_EN | ECC_RX_EN | BTA_EN | EOTP_TX_EN;

	if (dsi->mode_flags & MIPI_DSI_MODE_EOT_PACKET)
		val &= ~EOTP_TX_EN;

	dsi_write(dsi, DSI_PCKHDL_CFG, val);

	dsi_write(dsi, DSI_TO_CNT_CFG, HSTX_TO_CNT(1000) | LPRX_TO_CNT(1000));
	dsi_write(dsi, DSI_BTA_TO_CNT, 0xd00);
	dsi_write(dsi, DSI_PHY_TMR_CFG,
		  PHY_HS2LP_TIME(0x14) | PHY_LP2HS_TIME(0x10) |
		  MAX_RD_TIME(10000));
	dsi_write(dsi, DSI_PHY_TMR_LPCLK_CFG,
		  PHY_CLKHS2LP_TIME(0x40) | PHY_CLKLP2HS_TIME(0x40));
	dsi_write(dsi, DSI_PHY_IF_CFG,
		  PHY_STOP_WAIT_TIME(0x20) | N_LANES(dsi->lanes - 1));

	mipi_dphy_power_on(dsi);

	dsi_write(dsi, DSI_PWR_UP, POWER_UP);

	dsi_write(dsi, DSI_INT_MSK0, 0x1fffff);
	dsi_write(dsi, DSI_INT_MSK1, 0x1f7f);

	if (dsi->slave)
		rk628_dsi_pre_enable(dsi->slave);
}

static void rk628_dsi_enable(struct rk628_dsi *dsi)
{
	struct drm_display_mode *mode = &dsi->mode;
	u32 val;

	dsi_write(dsi, DSI_PWR_UP, RESET);

	switch (dsi->format) {
	case MIPI_DSI_FMT_RGB666:
		val = DPI_COLOR_CODING(DPI_COLOR_CODING_18BIT_2) | LOOSELY18_EN;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		val = DPI_COLOR_CODING(DPI_COLOR_CODING_18BIT_1);
		break;
	case MIPI_DSI_FMT_RGB565:
		val = DPI_COLOR_CODING(DPI_COLOR_CODING_16BIT_1);
		break;
	case MIPI_DSI_FMT_RGB888:
	default:
		val = DPI_COLOR_CODING(DPI_COLOR_CODING_24BIT);
		break;
	}

	dsi_write(dsi, DSI_DPI_COLOR_CODING, val);

	val = 0;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		val |= VSYNC_ACTIVE_LOW;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		val |= HSYNC_ACTIVE_LOW;
	dsi_write(dsi, DSI_DPI_CFG_POL, val);

	dsi_write(dsi, DSI_DPI_VCID, DPI_VID(dsi->channel));
	dsi_write(dsi, DSI_DPI_LP_CMD_TIM,
		  OUTVACT_LPCMD_TIME(4) | INVACT_LPCMD_TIME(4));

	dsi_update_bits(dsi, DSI_LPCLK_CTRL,
			PHY_TXREQUESTCLKHS, PHY_TXREQUESTCLKHS);

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO)
		rk628_dsi_set_vid_mode(dsi);
	else
		rk628_dsi_set_cmd_mode(dsi);

	dsi_write(dsi, DSI_PWR_UP, POWER_UP);

	if (dsi->slave)
		rk628_dsi_enable(dsi->slave);
}

static void rk628_dsi_bridge_enable(struct drm_bridge *bridge)
{
	struct rk628_dsi *dsi = bridge_to_dsi(bridge);
	unsigned int rate = rk628_dsi_get_lane_rate(dsi);
	int bus_width;
	int ret;

	regmap_update_bits(dsi->grf, GRF_SYSTEM_CON0, SW_OUTPUT_MODE_MASK,
			   SW_OUTPUT_MODE(OUTPUT_MODE_DSI));
	regmap_update_bits(dsi->grf, GRF_POST_PROC_CON, SW_SPLIT_EN,
			   dsi->slave ? SW_SPLIT_EN : 0);

	bus_width = rate << 8;
	if (dsi->slave)
		bus_width |= COMBTXPHY_MODULEA_EN | COMBTXPHY_MODULEB_EN;
	else if (dsi->id)
		bus_width |= COMBTXPHY_MODULEB_EN;
	else
		bus_width |= COMBTXPHY_MODULEA_EN;
	phy_set_bus_width(dsi->phy, bus_width);

	ret = phy_set_mode(dsi->phy, PHY_MODE_VIDEO_MIPI);
	if (ret) {
		dev_err(dsi->dev, "failed to set phy mode: %d\n", ret);
		return;
	}
	dsi->lane_mbps = phy_get_bus_width(dsi->phy);
	if (dsi->slave)
		dsi->slave->lane_mbps = dsi->lane_mbps;

	rk628_dsi_pre_enable(dsi);
	drm_panel_prepare(dsi->panel);
	rk628_dsi_enable(dsi);
	drm_panel_enable(dsi->panel);

	dev_info(dsi->dev, "final DSI-Link bandwidth: %u x %d Mbps\n",
		 dsi->lane_mbps, dsi->slave ? dsi->lanes * 2 : dsi->lanes);
}

static void rk628_dsi_bridge_disable(struct drm_bridge *bridge)
{
	struct rk628_dsi *dsi = bridge_to_dsi(bridge);

	drm_panel_disable(dsi->panel);
	rk628_dsi_disable(dsi);
	drm_panel_unprepare(dsi->panel);
	rk628_dsi_post_disable(dsi);
}

static void rk628_dsi_bridge_mode_set(struct drm_bridge *bridge,
				      struct drm_display_mode *mode,
				      struct drm_display_mode *adj)
{
	struct rk628_dsi *dsi = bridge_to_dsi(bridge);

	drm_mode_copy(&dsi->mode, adj);
	if (dsi->slave) {
		dsi->mode.hdisplay /= 2;
		drm_mode_copy(&dsi->slave->mode, &dsi->mode);
	}
}

static int rk628_dsi_bridge_attach(struct drm_bridge *bridge)
{
	struct rk628_dsi *dsi = bridge_to_dsi(bridge);
	struct drm_connector *connector = &dsi->connector;
	struct drm_device *drm = bridge->dev;
	int ret;

	if (!dsi->panel)
		return -EPROBE_DEFER;

	ret = drm_connector_init(drm, connector, &rk628_dsi_connector_funcs,
				 DRM_MODE_CONNECTOR_DSI);
	if (ret) {
		dev_err(dsi->dev, "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &rk628_dsi_connector_helper_funcs);
	drm_connector_attach_encoder(connector, bridge->encoder);

	ret = drm_panel_attach(dsi->panel, connector);
	if (ret) {
		dev_err(dsi->dev, "Failed to attach panel\n");
		return ret;
	}

	return 0;
}

static const struct drm_bridge_funcs rk628_dsi_bridge_funcs = {
	.attach = rk628_dsi_bridge_attach,
	.mode_set = rk628_dsi_bridge_mode_set,
	.enable = rk628_dsi_bridge_enable,
	.disable = rk628_dsi_bridge_disable,
};

static irqreturn_t rk628_dsi_irq_handler(int irq, void *dev_id)
{
	struct rk628_dsi *dsi = dev_id;
	u32 int_st0, int_st1;

	int_st0 = dsi_read(dsi, DSI_INT_ST0);
	int_st1 = dsi_read(dsi, DSI_INT_ST1);

	if (!int_st0 && !int_st1)
		return IRQ_NONE;

	dev_info(dsi->dev, "int_st0=0x%08x, int_st1=0x%08x\n",
		 int_st0, int_st1);

	return IRQ_HANDLED;
}

static const struct regmap_config testif_regmap_config = {
	.name = "phy",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x97,
	.cache_type = REGCACHE_RBTREE,
	.reg_write = testif_write,
	.reg_read = testif_read,
};

static bool rk628_dsi_register_volatile(struct device *dev, unsigned int reg)
{
	reg &= 0xffff;

	switch (reg) {
	case DSI_GEN_HDR:
	case DSI_GEN_PLD_DATA:
	case DSI_CMD_PKT_STATUS:
	case DSI_PHY_STATUS:
	case DSI_INT_ST0:
	case DSI_INT_ST1:
	case DSI_INT_FORCE0:
	case DSI_INT_FORCE1:
		return true;
	default:
		return false;
	}
}

static int rk628_dsi_probe(struct platform_device *pdev)
{
	struct rk628 *rk628 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk628_dsi *dsi;
	const struct rk628_dsi_data *data = of_device_get_match_data(dev);
	char name[8];
	int ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->dev = dev;
	dsi->parent = rk628;
	dsi->grf = rk628->grf;
	dsi->reg_base = data->reg_base;
	dsi->id = data->id;
	platform_set_drvdata(pdev, dsi);

	dsi->irq = platform_get_irq(pdev, 0);
	if (dsi->irq < 0)
		return dsi->irq;

	dsi->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dsi->pclk)) {
		ret = PTR_ERR(dsi->pclk);
		dev_err(dev, "failed to get pclk: %d\n", ret);
		return ret;
	}

	dsi->cfgclk = devm_clk_get(dev, "cfg");
	if (IS_ERR(dsi->cfgclk)) {
		ret = PTR_ERR(dsi->cfgclk);
		dev_err(dev, "failed to get cfg clk: %d\n", ret);
		return ret;
	}

	dsi->rst = of_reset_control_get(dev->of_node, NULL);
	if (IS_ERR(dsi->rst)) {
		ret = PTR_ERR(dsi->rst);
		dev_err(dev, "failed to get reset control: %d\n", ret);
		return ret;
	}

	dsi->phy = devm_of_phy_get(dev, dev->of_node, NULL);
	if (IS_ERR(dsi->phy)) {
		ret = PTR_ERR(dsi->phy);
		dev_err(dev, "failed to get phy: %d\n", ret);
		return ret;
	}

	sprintf(name, "dsi%d", dsi->id);
	dsi->config.name = name;
	dsi->config.reg_bits = 32;
	dsi->config.val_bits = 32;
	dsi->config.reg_stride = 4;
	dsi->config.cache_type = REGCACHE_RBTREE;
	dsi->config.max_register = dsi->reg_base + DSI_MAX_REGISTER;
	dsi->config.reg_format_endian = REGMAP_ENDIAN_LITTLE;
	dsi->config.val_format_endian = REGMAP_ENDIAN_LITTLE;
	dsi->config.volatile_reg = rk628_dsi_register_volatile;
	dsi->range.range_min = dsi->reg_base + DSI_VERSION;
	dsi->range.range_max = dsi->reg_base + DSI_MAX_REGISTER;
	dsi->rd_table.yes_ranges = &dsi->range;
	dsi->rd_table.n_yes_ranges = 1;
	dsi->config.rd_table = &dsi->rd_table;

	dsi->regmap = devm_regmap_init_i2c(rk628->client, &dsi->config);
	if (IS_ERR(dsi->regmap)) {
		ret = PTR_ERR(dsi->regmap);
		dev_err(dev, "failed to allocate register map: %d\n", ret);
		return ret;
	}

	dsi->testif = devm_regmap_init(dev, NULL, dsi, &testif_regmap_config);
	if (IS_ERR(dsi->testif)) {
		ret = PTR_ERR(dsi->testif);
		dev_err(dev, "failed to create testif regmap: %d\n", ret);
		return ret;
	}

	ret = devm_request_threaded_irq(dev, dsi->irq, NULL,
					rk628_dsi_irq_handler, IRQF_ONESHOT,
					dev_name(dev), dsi);
	if (ret) {
		dev_err(dev, "failed to request irq: %d\n", ret);
		return ret;
	}

	dsi->base.funcs = &rk628_dsi_bridge_funcs;
	dsi->base.of_node = dev->of_node;
	drm_bridge_add(&dsi->base);

	dsi->host.ops = &rk628_dsi_host_ops;
	dsi->host.dev = dev;
	ret = mipi_dsi_host_register(&dsi->host);
	if (ret) {
		drm_bridge_remove(&dsi->base);
		dev_err(dev, "Failed to register MIPI host: %d\n", ret);
		return ret;
	}

	return 0;
}

static int rk628_dsi_remove(struct platform_device *pdev)
{
	struct rk628_dsi *dsi = platform_get_drvdata(pdev);

	mipi_dsi_host_unregister(&dsi->host);
	drm_bridge_remove(&dsi->base);

	return 0;
}

static const struct rk628_dsi_data rk628_dsi0_data = {
	.reg_base = 0x50000,
	.id = 0,
};

static const struct rk628_dsi_data rk628_dsi1_data = {
	.reg_base = 0x60000,
	.id = 1,
};

static const struct of_device_id rk628_dsi_of_match[] = {
	{ .compatible = "rockchip,rk628-dsi0", .data = &rk628_dsi0_data },
	{ .compatible = "rockchip,rk628-dsi1", .data = &rk628_dsi1_data },
	{}
};
MODULE_DEVICE_TABLE(of, rk628_dsi_of_match);

static struct platform_driver rk628_dsi_driver = {
	.driver = {
		.name = "rk628-dsi",
		.of_match_table = of_match_ptr(rk628_dsi_of_match),
	},
	.probe	= rk628_dsi_probe,
	.remove = rk628_dsi_remove,
};
module_platform_driver(rk628_dsi_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK628 MIPI-DSI driver");
MODULE_LICENSE("GPL v2");
