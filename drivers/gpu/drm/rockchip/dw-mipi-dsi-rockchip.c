// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:
 *      Chris Zhong <zyw@rock-chips.com>
 *      Nickey Yang <nickey.yang@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/math64.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <video/mipi_display.h>
#include <uapi/linux/videodev2.h>
#include <drm/bridge/dw_mipi_dsi.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_simple_kms_helper.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define DSI_PHY_RSTZ			0xa0
#define PHY_DISFORCEPLL			0
#define PHY_ENFORCEPLL			BIT(3)
#define PHY_DISABLECLK			0
#define PHY_ENABLECLK			BIT(2)
#define PHY_RSTZ			0
#define PHY_UNRSTZ			BIT(1)
#define PHY_SHUTDOWNZ			0
#define PHY_UNSHUTDOWNZ			BIT(0)

#define DSI_PHY_IF_CFG			0xa4
#define N_LANES(n)			((((n) - 1) & 0x3) << 0)
#define PHY_STOP_WAIT_TIME(cycle)	(((cycle) & 0xff) << 8)

#define DSI_PHY_STATUS			0xb0
#define LOCK				BIT(0)
#define STOP_STATE_CLK_LANE		BIT(2)

#define DSI_PHY_TST_CTRL0		0xb4
#define PHY_TESTCLK			BIT(1)
#define PHY_UNTESTCLK			0
#define PHY_TESTCLR			BIT(0)
#define PHY_UNTESTCLR			0

#define DSI_PHY_TST_CTRL1		0xb8
#define PHY_TESTEN			BIT(16)
#define PHY_UNTESTEN			0
#define PHY_TESTDOUT(n)			(((n) & 0xff) << 8)
#define PHY_TESTDIN(n)			(((n) & 0xff) << 0)

#define DSI_INT_ST0			0xbc
#define DSI_INT_ST1			0xc0
#define DSI_INT_MSK0			0xc4
#define DSI_INT_MSK1			0xc8

#define PHY_STATUS_TIMEOUT_US		10000
#define CMD_PKT_STATUS_TIMEOUT_US	20000

#define BYPASS_VCO_RANGE	BIT(7)
#define VCO_RANGE_CON_SEL(val)	(((val) & 0x7) << 3)
#define VCO_IN_CAP_CON_DEFAULT	(0x0 << 1)
#define VCO_IN_CAP_CON_LOW	(0x1 << 1)
#define VCO_IN_CAP_CON_HIGH	(0x2 << 1)
#define REF_BIAS_CUR_SEL	BIT(0)

#define CP_CURRENT_3UA	0x1
#define CP_CURRENT_4_5UA	0x2
#define CP_CURRENT_7_5UA	0x6
#define CP_CURRENT_6UA	0x9
#define CP_CURRENT_12UA	0xb
#define CP_CURRENT_SEL(val)	((val) & 0xf)
#define CP_PROGRAM_EN		BIT(7)

#define LPF_RESISTORS_15_5KOHM	0x1
#define LPF_RESISTORS_13KOHM	0x2
#define LPF_RESISTORS_11_5KOHM	0x4
#define LPF_RESISTORS_10_5KOHM	0x8
#define LPF_RESISTORS_8KOHM	0x10
#define LPF_PROGRAM_EN		BIT(6)
#define LPF_RESISTORS_SEL(val)	((val) & 0x3f)

#define HSFREQRANGE_SEL(val)	(((val) & 0x3f) << 1)

#define INPUT_DIVIDER(val)	(((val) - 1) & 0x7f)
#define LOW_PROGRAM_EN		0
#define HIGH_PROGRAM_EN		BIT(7)
#define LOOP_DIV_LOW_SEL(val)	(((val) - 1) & 0x1f)
#define LOOP_DIV_HIGH_SEL(val)	((((val) - 1) >> 5) & 0xf)
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

#define PLL_BIAS_CUR_SEL_CAP_VCO_CONTROL		0x10
#define PLL_CP_CONTROL_PLL_LOCK_BYPASS			0x11
#define PLL_LPF_AND_CP_CONTROL				0x12
#define PLL_INPUT_DIVIDER_RATIO				0x17
#define PLL_LOOP_DIVIDER_RATIO				0x18
#define PLL_INPUT_AND_LOOP_DIVIDER_RATIOS_CONTROL	0x19
#define BANDGAP_AND_BIAS_CONTROL			0x20
#define TERMINATION_RESISTER_CONTROL			0x21
#define AFE_BIAS_BANDGAP_ANALOG_PROGRAMMABILITY		0x22
#define HS_RX_CONTROL_OF_LANE_0				0x44
#define HS_TX_CLOCK_LANE_REQUEST_STATE_TIME_CONTROL	0x60
#define HS_TX_CLOCK_LANE_PREPARE_STATE_TIME_CONTROL	0x61
#define HS_TX_CLOCK_LANE_HS_ZERO_STATE_TIME_CONTROL	0x62
#define HS_TX_CLOCK_LANE_TRAIL_STATE_TIME_CONTROL	0x63
#define HS_TX_CLOCK_LANE_EXIT_STATE_TIME_CONTROL	0x64
#define HS_TX_CLOCK_LANE_POST_TIME_CONTROL		0x65
#define HS_TX_DATA_LANE_REQUEST_STATE_TIME_CONTROL	0x70
#define HS_TX_DATA_LANE_PREPARE_STATE_TIME_CONTROL	0x71
#define HS_TX_DATA_LANE_HS_ZERO_STATE_TIME_CONTROL	0x72
#define HS_TX_DATA_LANE_TRAIL_STATE_TIME_CONTROL	0x73
#define HS_TX_DATA_LANE_EXIT_STATE_TIME_CONTROL		0x74

#define DW_MIPI_NEEDS_PHY_CFG_CLK	BIT(0)
#define DW_MIPI_NEEDS_GRF_CLK		BIT(1)
#define DW_MIPI_NEEDS_HCLK		BIT(2)

#define PX30_GRF_PD_VO_CON1		0x0438
#define PX30_DSI_FORCETXSTOPMODE	(0xf << 7)
#define PX30_DSI_FORCERXMODE		BIT(6)
#define PX30_DSI_TURNDISABLE		BIT(5)
#define PX30_DSI_LCDC_SEL		BIT(0)

#define RK3288_GRF_SOC_CON6		0x025c
#define RK3288_DSI0_LCDC_SEL		BIT(6)
#define RK3288_DSI1_LCDC_SEL		BIT(9)

#define RK3399_GRF_SOC_CON20		0x6250
#define RK3399_DSI0_LCDC_SEL		BIT(0)
#define RK3399_DSI1_LCDC_SEL		BIT(4)

#define RK3399_GRF_SOC_CON22		0x6258
#define RK3399_DSI0_TURNREQUEST		(0xf << 12)
#define RK3399_DSI0_TURNDISABLE		(0xf << 8)
#define RK3399_DSI0_FORCETXSTOPMODE	(0xf << 4)
#define RK3399_DSI0_FORCERXMODE		(0xf << 0)

#define RK3399_GRF_SOC_CON23		0x625c
#define RK3399_DSI1_TURNDISABLE		(0xf << 12)
#define RK3399_DSI1_FORCETXSTOPMODE	(0xf << 8)
#define RK3399_DSI1_FORCERXMODE		(0xf << 4)
#define RK3399_DSI1_ENABLE		(0xf << 0)

#define RK3399_GRF_SOC_CON24		0x6260
#define RK3399_TXRX_MASTERSLAVEZ	BIT(7)
#define RK3399_TXRX_ENABLECLK		BIT(6)
#define RK3399_TXRX_BASEDIR		BIT(5)

#define RK3568_GRF_VO_CON2		0x0368
#define RK3568_GRF_VO_CON3		0x036c
#define RK3568_DSI_FORCETXSTOPMODE	(0xf << 4)
#define RK3568_DSI_TURNDISABLE		(0x1 << 2)
#define RK3568_DSI_FORCERXMODE		(0x1 << 0)

#define HIWORD_UPDATE(val, mask)	(val | (mask) << 16)

#define to_dsi(nm)	container_of(nm, struct dw_mipi_dsi_rockchip, nm)

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

enum soc_type {
	PX30,
	RK3288,
	RK3399,
	RK3568,
};

struct rockchip_dw_dsi_chip_data {
	u32 reg;

	u32 lcdsel_grf_reg;
	u32 lcdsel_big;
	u32 lcdsel_lit;

	u32 enable_grf_reg;
	u32 enable;

	u32 lanecfg1_grf_reg;
	u32 lanecfg1;
	u32 lanecfg2_grf_reg;
	u32 lanecfg2;

	enum soc_type soc_type;
	unsigned int flags;
	unsigned int max_data_lanes;
	unsigned long max_bit_rate_per_lane;
};

struct dw_mipi_dsi_rockchip {
	struct device *dev;
	struct drm_encoder encoder;
	void __iomem *base;
	int id;

	struct regmap *grf_regmap;
	struct clk *pllref_clk;
	struct clk *pclk;
	struct clk *grf_clk;
	struct clk *phy_cfg_clk;
	struct clk *hclk;

	/* dual-channel */
	bool is_slave;
	struct dw_mipi_dsi_rockchip *slave;

	/* optional external dphy */
	bool phy_enabled;
	struct phy *phy;
	union phy_configure_opts phy_opts;

	unsigned int lane_mbps; /* per lane */
	u16 input_div;
	u16 feedback_div;
	u32 format;

	struct dw_mipi_dsi *dmd;
	const struct rockchip_dw_dsi_chip_data *cdata;
	struct dw_mipi_dsi_plat_data pdata;
	int devcnt;
	struct rockchip_drm_sub_dev sub_dev;
	struct drm_panel *panel;
};

struct dphy_pll_parameter_map {
	unsigned int max_mbps;
	u8 hsfreqrange;
	u8 icpctrl;
	u8 lpfctrl;
};

/* The table is based on 27MHz DPHY pll reference clock. */
static const struct dphy_pll_parameter_map dppa_map[] = {
	{  89, 0x00, CP_CURRENT_3UA, LPF_RESISTORS_13KOHM },
	{  99, 0x10, CP_CURRENT_3UA, LPF_RESISTORS_13KOHM },
	{ 109, 0x20, CP_CURRENT_3UA, LPF_RESISTORS_13KOHM },
	{ 129, 0x01, CP_CURRENT_3UA, LPF_RESISTORS_15_5KOHM },
	{ 139, 0x11, CP_CURRENT_3UA, LPF_RESISTORS_15_5KOHM },
	{ 149, 0x21, CP_CURRENT_3UA, LPF_RESISTORS_15_5KOHM },
	{ 169, 0x02, CP_CURRENT_6UA, LPF_RESISTORS_13KOHM },
	{ 179, 0x12, CP_CURRENT_6UA, LPF_RESISTORS_13KOHM },
	{ 199, 0x22, CP_CURRENT_6UA, LPF_RESISTORS_13KOHM },
	{ 219, 0x03, CP_CURRENT_4_5UA, LPF_RESISTORS_13KOHM },
	{ 239, 0x13, CP_CURRENT_4_5UA, LPF_RESISTORS_13KOHM },
	{ 249, 0x23, CP_CURRENT_4_5UA, LPF_RESISTORS_13KOHM },
	{ 269, 0x04, CP_CURRENT_6UA, LPF_RESISTORS_11_5KOHM },
	{ 299, 0x14, CP_CURRENT_6UA, LPF_RESISTORS_11_5KOHM },
	{ 329, 0x05, CP_CURRENT_3UA, LPF_RESISTORS_15_5KOHM },
	{ 359, 0x15, CP_CURRENT_3UA, LPF_RESISTORS_15_5KOHM },
	{ 399, 0x25, CP_CURRENT_3UA, LPF_RESISTORS_15_5KOHM },
	{ 449, 0x06, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 499, 0x16, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 549, 0x07, CP_CURRENT_7_5UA, LPF_RESISTORS_10_5KOHM },
	{ 599, 0x17, CP_CURRENT_7_5UA, LPF_RESISTORS_10_5KOHM },
	{ 649, 0x08, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 699, 0x18, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 749, 0x09, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 799, 0x19, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 849, 0x29, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 899, 0x39, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 949, 0x0a, CP_CURRENT_12UA, LPF_RESISTORS_8KOHM },
	{ 999, 0x1a, CP_CURRENT_12UA, LPF_RESISTORS_8KOHM },
	{1049, 0x2a, CP_CURRENT_12UA, LPF_RESISTORS_8KOHM },
	{1099, 0x3a, CP_CURRENT_12UA, LPF_RESISTORS_8KOHM },
	{1149, 0x0b, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM },
	{1199, 0x1b, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM },
	{1249, 0x2b, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM },
	{1299, 0x3b, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM },
	{1349, 0x0c, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM },
	{1399, 0x1c, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM },
	{1449, 0x2c, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM },
	{1500, 0x3c, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM }
};

static int max_mbps_to_parameter(unsigned int max_mbps)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dppa_map); i++)
		if (dppa_map[i].max_mbps >= max_mbps)
			return i;

	return -EINVAL;
}

static inline void dsi_write(struct dw_mipi_dsi_rockchip *dsi, u32 reg, u32 val)
{
	writel(val, dsi->base + reg);
}

static inline u32 dsi_read(struct dw_mipi_dsi_rockchip *dsi, u32 reg)
{
	return readl(dsi->base + reg);
}

static inline void dsi_set(struct dw_mipi_dsi_rockchip *dsi, u32 reg, u32 mask)
{
	dsi_write(dsi, reg, dsi_read(dsi, reg) | mask);
}

static inline void dsi_update_bits(struct dw_mipi_dsi_rockchip *dsi, u32 reg,
				   u32 mask, u32 val)
{
	dsi_write(dsi, reg, (dsi_read(dsi, reg) & ~mask) | val);
}

static void dw_mipi_dsi_phy_write(struct dw_mipi_dsi_rockchip *dsi,
				  u8 test_code,
				  u8 test_data)
{
	/*
	 * With the falling edge on TESTCLK, the TESTDIN[7:0] signal content
	 * is latched internally as the current test code. Test data is
	 * programmed internally by rising edge on TESTCLK.
	 */
	dsi_write(dsi, DSI_PHY_TST_CTRL0, PHY_TESTCLK | PHY_UNTESTCLR);

	dsi_write(dsi, DSI_PHY_TST_CTRL1, PHY_TESTEN | PHY_TESTDOUT(0) |
					  PHY_TESTDIN(test_code));

	dsi_write(dsi, DSI_PHY_TST_CTRL0, PHY_UNTESTCLK | PHY_UNTESTCLR);

	dsi_write(dsi, DSI_PHY_TST_CTRL1, PHY_UNTESTEN | PHY_TESTDOUT(0) |
					  PHY_TESTDIN(test_data));

	dsi_write(dsi, DSI_PHY_TST_CTRL0, PHY_TESTCLK | PHY_UNTESTCLR);
}

/**
 * ns2bc - Nanoseconds to byte clock cycles
 */
static inline unsigned int ns2bc(struct dw_mipi_dsi_rockchip *dsi, int ns)
{
	return DIV_ROUND_UP(ns * dsi->lane_mbps / 8, 1000);
}

/**
 * ns2ui - Nanoseconds to UI time periods
 */
static inline unsigned int ns2ui(struct dw_mipi_dsi_rockchip *dsi, int ns)
{
	return DIV_ROUND_UP(ns * dsi->lane_mbps, 1000);
}

static void dw_mipi_dsi_phy_tx_config(struct dw_mipi_dsi_rockchip *dsi)
{
	if (dsi->cdata->lanecfg1_grf_reg)
		regmap_write(dsi->grf_regmap, dsi->cdata->lanecfg1_grf_reg,
					      dsi->cdata->lanecfg1);

	if (dsi->cdata->lanecfg2_grf_reg)
		regmap_write(dsi->grf_regmap, dsi->cdata->lanecfg2_grf_reg,
					      dsi->cdata->lanecfg2);

	if (dsi->cdata->enable_grf_reg)
		regmap_write(dsi->grf_regmap, dsi->cdata->enable_grf_reg,
					      dsi->cdata->enable);
}

static int dw_mipi_dsi_phy_init(void *priv_data)
{
	struct dw_mipi_dsi_rockchip *dsi = priv_data;
	int i, vco;

	dw_mipi_dsi_phy_tx_config(dsi);

	if (dsi->phy)
		return 0;

	/*
	 * Get vco from frequency(lane_mbps)
	 * vco	frequency table
	 * 000 - between   80 and  200 MHz
	 * 001 - between  200 and  300 MHz
	 * 010 - between  300 and  500 MHz
	 * 011 - between  500 and  700 MHz
	 * 100 - between  700 and  900 MHz
	 * 101 - between  900 and 1100 MHz
	 * 110 - between 1100 and 1300 MHz
	 * 111 - between 1300 and 1500 MHz
	 */
	vco = (dsi->lane_mbps < 200) ? 0 : (dsi->lane_mbps + 100) / 200;

	i = max_mbps_to_parameter(dsi->lane_mbps);
	if (i < 0) {
		DRM_DEV_ERROR(dsi->dev,
			      "failed to get parameter for %dmbps clock\n",
			      dsi->lane_mbps);
		return i;
	}

	dw_mipi_dsi_phy_write(dsi, PLL_BIAS_CUR_SEL_CAP_VCO_CONTROL,
			      BYPASS_VCO_RANGE |
			      VCO_RANGE_CON_SEL(vco) |
			      VCO_IN_CAP_CON_LOW |
			      REF_BIAS_CUR_SEL);

	dw_mipi_dsi_phy_write(dsi, PLL_CP_CONTROL_PLL_LOCK_BYPASS,
			      CP_CURRENT_SEL(dppa_map[i].icpctrl));
	dw_mipi_dsi_phy_write(dsi, PLL_LPF_AND_CP_CONTROL,
			      CP_PROGRAM_EN | LPF_PROGRAM_EN |
			      LPF_RESISTORS_SEL(dppa_map[i].lpfctrl));

	dw_mipi_dsi_phy_write(dsi, HS_RX_CONTROL_OF_LANE_0,
			      HSFREQRANGE_SEL(dppa_map[i].hsfreqrange));

	dw_mipi_dsi_phy_write(dsi, PLL_INPUT_DIVIDER_RATIO,
			      INPUT_DIVIDER(dsi->input_div));
	dw_mipi_dsi_phy_write(dsi, PLL_LOOP_DIVIDER_RATIO,
			      LOOP_DIV_LOW_SEL(dsi->feedback_div) |
			      LOW_PROGRAM_EN);
	/*
	 * We need set PLL_INPUT_AND_LOOP_DIVIDER_RATIOS_CONTROL immediately
	 * to make the configured LSB effective according to IP simulation
	 * and lab test results.
	 * Only in this way can we get correct mipi phy pll frequency.
	 */
	dw_mipi_dsi_phy_write(dsi, PLL_INPUT_AND_LOOP_DIVIDER_RATIOS_CONTROL,
			      PLL_LOOP_DIV_EN | PLL_INPUT_DIV_EN);
	dw_mipi_dsi_phy_write(dsi, PLL_LOOP_DIVIDER_RATIO,
			      LOOP_DIV_HIGH_SEL(dsi->feedback_div) |
			      HIGH_PROGRAM_EN);
	dw_mipi_dsi_phy_write(dsi, PLL_INPUT_AND_LOOP_DIVIDER_RATIOS_CONTROL,
			      PLL_LOOP_DIV_EN | PLL_INPUT_DIV_EN);

	dw_mipi_dsi_phy_write(dsi, AFE_BIAS_BANDGAP_ANALOG_PROGRAMMABILITY,
			      LOW_PROGRAM_EN | BIASEXTR_SEL(BIASEXTR_127_7));
	dw_mipi_dsi_phy_write(dsi, AFE_BIAS_BANDGAP_ANALOG_PROGRAMMABILITY,
			      HIGH_PROGRAM_EN | BANDGAP_SEL(BANDGAP_96_10));

	dw_mipi_dsi_phy_write(dsi, BANDGAP_AND_BIAS_CONTROL,
			      POWER_CONTROL | INTERNAL_REG_CURRENT |
			      BIAS_BLOCK_ON | BANDGAP_ON);

	dw_mipi_dsi_phy_write(dsi, TERMINATION_RESISTER_CONTROL,
			      TER_RESISTOR_LOW | TER_CAL_DONE |
			      SETRD_MAX | TER_RESISTORS_ON);
	dw_mipi_dsi_phy_write(dsi, TERMINATION_RESISTER_CONTROL,
			      TER_RESISTOR_HIGH | LEVEL_SHIFTERS_ON |
			      SETRD_MAX | POWER_MANAGE |
			      TER_RESISTORS_ON);

	dw_mipi_dsi_phy_write(dsi, HS_TX_CLOCK_LANE_REQUEST_STATE_TIME_CONTROL,
			      TLP_PROGRAM_EN | ns2bc(dsi, 60));
	dw_mipi_dsi_phy_write(dsi, HS_TX_CLOCK_LANE_PREPARE_STATE_TIME_CONTROL,
			      THS_PRE_PROGRAM_EN | ns2ui(dsi, 40));
	dw_mipi_dsi_phy_write(dsi, HS_TX_CLOCK_LANE_HS_ZERO_STATE_TIME_CONTROL,
			      THS_ZERO_PROGRAM_EN | ns2bc(dsi, 300));
	dw_mipi_dsi_phy_write(dsi, HS_TX_CLOCK_LANE_TRAIL_STATE_TIME_CONTROL,
			      THS_PRE_PROGRAM_EN | ns2ui(dsi, 100));
	dw_mipi_dsi_phy_write(dsi, HS_TX_CLOCK_LANE_EXIT_STATE_TIME_CONTROL,
			      BIT(5) | ns2bc(dsi, 100));
	dw_mipi_dsi_phy_write(dsi, HS_TX_CLOCK_LANE_POST_TIME_CONTROL,
			      BIT(5) | (ns2bc(dsi, 60) + 7));

	dw_mipi_dsi_phy_write(dsi, HS_TX_DATA_LANE_REQUEST_STATE_TIME_CONTROL,
			      TLP_PROGRAM_EN | ns2bc(dsi, 60));
	dw_mipi_dsi_phy_write(dsi, HS_TX_DATA_LANE_PREPARE_STATE_TIME_CONTROL,
			      THS_PRE_PROGRAM_EN | (ns2ui(dsi, 50) + 20));
	dw_mipi_dsi_phy_write(dsi, HS_TX_DATA_LANE_HS_ZERO_STATE_TIME_CONTROL,
			      THS_ZERO_PROGRAM_EN | (ns2bc(dsi, 140) + 2));
	dw_mipi_dsi_phy_write(dsi, HS_TX_DATA_LANE_TRAIL_STATE_TIME_CONTROL,
			      THS_PRE_PROGRAM_EN | (ns2ui(dsi, 60) + 8));
	dw_mipi_dsi_phy_write(dsi, HS_TX_DATA_LANE_EXIT_STATE_TIME_CONTROL,
			      BIT(5) | ns2bc(dsi, 100));

	return 0;
}

static void dw_mipi_dsi_phy_power_on(void *priv_data)
{
	struct dw_mipi_dsi_rockchip *dsi = priv_data;

	if (dsi->phy_enabled)
		return;

	phy_power_on(dsi->phy);
	dsi->phy_enabled = true;
}

static void dw_mipi_dsi_phy_power_off(void *priv_data)
{
	struct dw_mipi_dsi_rockchip *dsi = priv_data;

	if (!dsi->phy_enabled)
		return;

	phy_power_off(dsi->phy);
	dsi->phy_enabled = false;
}

static int
dw_mipi_dsi_get_lane_mbps(void *priv_data, const struct drm_display_mode *mode,
			  unsigned long mode_flags, u32 lanes, u32 format,
			  unsigned int *lane_mbps)
{
	struct dw_mipi_dsi_rockchip *dsi = priv_data;
	struct device *dev = dsi->dev;
	int bpp;
	unsigned long mpclk, tmp;
	unsigned int target_mbps = 1000;
	unsigned int max_mbps;
	unsigned long best_freq = 0;
	unsigned long fvco_min, fvco_max, fin, fout;
	unsigned int min_prediv, max_prediv;
	unsigned int _prediv, best_prediv;
	unsigned long _fbdiv, best_fbdiv;
	unsigned long min_delta = ULONG_MAX;
	unsigned long target_pclk, hs_clk_rate;
	unsigned int value;
	int ret;

	max_mbps = dsi->cdata->max_bit_rate_per_lane / USEC_PER_SEC;
	dsi->format = format;
	bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
	if (bpp < 0) {
		DRM_DEV_ERROR(dsi->dev,
			      "failed to get bpp for pixel format %d\n",
			      dsi->format);
		return bpp;
	}

	/* optional override of the desired bandwidth */
	if (!of_property_read_u32(dev->of_node, "rockchip,lane-rate", &value)) {
		target_mbps = value;
	} else {
		mpclk = DIV_ROUND_UP(mode->clock, MSEC_PER_SEC);
		if (mpclk) {
			/* take 1 / 0.9, since mbps must big than bandwidth of RGB */
			tmp = mpclk * (bpp / lanes) * 10 / 9;
			if (tmp < max_mbps)
				target_mbps = tmp;
			else {
				DRM_DEV_ERROR(dsi->dev,
					      "DPHY clock frequency is out of range\n");
				target_mbps = max_mbps;
			}
		}
	}

	/* for external phy only a the mipi_dphy_config is necessary */
	if (dsi->phy) {
		target_pclk = DIV_ROUND_CLOSEST_ULL(target_mbps * lanes, bpp);
		phy_mipi_dphy_get_default_config(target_pclk * USEC_PER_SEC,
						 bpp, lanes,
						 &dsi->phy_opts.mipi_dphy);
		ret = phy_set_mode(dsi->phy, PHY_MODE_MIPI_DPHY);
		if (ret) {
			DRM_DEV_ERROR(dsi->dev,
				      "failed to set phy mode: %d\n", ret);
			return ret;
		}

		phy_configure(dsi->phy, &dsi->phy_opts);
		hs_clk_rate = dsi->phy_opts.mipi_dphy.hs_clk_rate;
		dsi->lane_mbps = DIV_ROUND_UP(hs_clk_rate, USEC_PER_SEC);
		*lane_mbps = dsi->lane_mbps;

		return 0;
	}

	fin = clk_get_rate(dsi->pllref_clk);
	fout = target_mbps * USEC_PER_SEC;

	/* constraint: 5Mhz <= Fref / N <= 40MHz */
	min_prediv = DIV_ROUND_UP(fin, 40 * USEC_PER_SEC);
	max_prediv = fin / (5 * USEC_PER_SEC);

	/* constraint: 80MHz <= Fvco <= 1500Mhz */
	fvco_min = 80 * USEC_PER_SEC;
	fvco_max = 1500 * USEC_PER_SEC;

	for (_prediv = min_prediv; _prediv <= max_prediv; _prediv++) {
		u64 tmp;
		u32 delta;
		/* Fvco = Fref * M / N */
		tmp = (u64)fout * _prediv;
		do_div(tmp, fin);
		_fbdiv = tmp;
		/*
		 * Due to the use of a "by 2 pre-scaler," the range of the
		 * feedback multiplication value M is limited to even division
		 * numbers, and m must be greater than 6, not bigger than 512.
		 */
		if (_fbdiv < 6 || _fbdiv > 512)
			continue;

		_fbdiv += _fbdiv % 2;

		tmp = (u64)_fbdiv * fin;
		do_div(tmp, _prediv);
		if (tmp < fvco_min || tmp > fvco_max)
			continue;

		delta = abs(fout - tmp);
		if (delta < min_delta) {
			best_prediv = _prediv;
			best_fbdiv = _fbdiv;
			min_delta = delta;
			best_freq = tmp;
		}
	}

	if (best_freq) {
		dsi->lane_mbps = DIV_ROUND_UP(best_freq, USEC_PER_SEC);
		*lane_mbps = dsi->lane_mbps;
		dsi->input_div = best_prediv;
		dsi->feedback_div = best_fbdiv;
	} else {
		DRM_DEV_ERROR(dsi->dev, "Can not find best_freq for DPHY\n");
		return -EINVAL;
	}

	return 0;
}

struct hstt {
	unsigned int maxfreq;
	struct dw_mipi_dsi_dphy_timing timing;
};

#define HSTT(_maxfreq, _c_lp2hs, _c_hs2lp, _d_lp2hs, _d_hs2lp)	\
{					\
	.maxfreq = _maxfreq,		\
	.timing = {			\
		.clk_lp2hs = _c_lp2hs,	\
		.clk_hs2lp = _c_hs2lp,	\
		.data_lp2hs = _d_lp2hs,	\
		.data_hs2lp = _d_hs2lp,	\
	}				\
}

/* Table A-3 High-Speed Transition Times */
struct hstt hstt_table[] = {
	HSTT(  90,  32, 20,  26, 13),
	HSTT( 100,  35, 23,  28, 14),
	HSTT( 110,  32, 22,  26, 13),
	HSTT( 130,  31, 20,  27, 13),
	HSTT( 140,  33, 22,  26, 14),
	HSTT( 150,  33, 21,  26, 14),
	HSTT( 170,  32, 20,  27, 13),
	HSTT( 180,  36, 23,  30, 15),
	HSTT( 200,  40, 22,  33, 15),
	HSTT( 220,  40, 22,  33, 15),
	HSTT( 240,  44, 24,  36, 16),
	HSTT( 250,  48, 24,  38, 17),
	HSTT( 270,  48, 24,  38, 17),
	HSTT( 300,  50, 27,  41, 18),
	HSTT( 330,  56, 28,  45, 18),
	HSTT( 360,  59, 28,  48, 19),
	HSTT( 400,  61, 30,  50, 20),
	HSTT( 450,  67, 31,  55, 21),
	HSTT( 500,  73, 31,  59, 22),
	HSTT( 550,  79, 36,  63, 24),
	HSTT( 600,  83, 37,  68, 25),
	HSTT( 650,  90, 38,  73, 27),
	HSTT( 700,  95, 40,  77, 28),
	HSTT( 750, 102, 40,  84, 28),
	HSTT( 800, 106, 42,  87, 30),
	HSTT( 850, 113, 44,  93, 31),
	HSTT( 900, 118, 47,  98, 32),
	HSTT( 950, 124, 47, 102, 34),
	HSTT(1000, 130, 49, 107, 35),
	HSTT(1050, 135, 51, 111, 37),
	HSTT(1100, 139, 51, 114, 38),
	HSTT(1150, 146, 54, 120, 40),
	HSTT(1200, 153, 57, 125, 41),
	HSTT(1250, 158, 58, 130, 42),
	HSTT(1300, 163, 58, 135, 44),
	HSTT(1350, 168, 60, 140, 45),
	HSTT(1400, 172, 64, 144, 47),
	HSTT(1450, 176, 65, 148, 48),
	HSTT(1500, 181, 66, 153, 50)
};

struct dw_mipi_dsi_dphy_timing ext_dphy_timing = {
	.clk_lp2hs = 0x40,
	.clk_hs2lp = 0x40,
	.data_lp2hs = 0x10,
	.data_hs2lp = 0x14,
};

static int
dw_mipi_dsi_phy_get_timing(void *priv_data, unsigned int lane_mbps,
			   struct dw_mipi_dsi_dphy_timing *timing)
{
	struct dw_mipi_dsi_rockchip *dsi = priv_data;
	int i;

	if (dsi->phy) {
		*timing = ext_dphy_timing;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(hstt_table); i++)
		if (lane_mbps < hstt_table[i].maxfreq)
			break;

	if (i == ARRAY_SIZE(hstt_table))
		i--;

	*timing = hstt_table[i].timing;

	return 0;
}

static const struct dw_mipi_dsi_phy_ops dw_mipi_dsi_rockchip_phy_ops = {
	.init = dw_mipi_dsi_phy_init,
	.power_on = dw_mipi_dsi_phy_power_on,
	.power_off = dw_mipi_dsi_phy_power_off,
	.get_lane_mbps = dw_mipi_dsi_get_lane_mbps,
	.get_timing = dw_mipi_dsi_phy_get_timing,
};

static void dw_mipi_dsi_rockchip_vop_routing(struct dw_mipi_dsi_rockchip *dsi)
{
	int mux;

	mux = drm_of_encoder_active_endpoint_id(dsi->dev->of_node,
						&dsi->encoder);
	if (mux < 0)
		return;

	if (dsi->cdata->lcdsel_grf_reg) {
		regmap_write(dsi->grf_regmap, dsi->cdata->lcdsel_grf_reg,
			mux ? dsi->cdata->lcdsel_lit : dsi->cdata->lcdsel_big);

		if (dsi->slave && dsi->slave->cdata->lcdsel_grf_reg)
			regmap_write(dsi->slave->grf_regmap,
				     dsi->slave->cdata->lcdsel_grf_reg,
				     mux ? dsi->slave->cdata->lcdsel_lit :
				     dsi->slave->cdata->lcdsel_big);
	}
}

static int
dw_mipi_dsi_encoder_atomic_check(struct drm_encoder *encoder,
				 struct drm_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct dw_mipi_dsi_rockchip *dsi = to_dsi(encoder);
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

	if (info->num_bus_formats)
		s->bus_format = info->bus_formats[0];
	else
		s->bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	/* rk356x series drive mipi pixdata on posedge */
	if (dsi->cdata->soc_type == RK3568) {
		s->bus_flags &= ~DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE;
		s->bus_flags |= DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE;
	}

	s->output_type = DRM_MODE_CONNECTOR_DSI;
	s->color_space = V4L2_COLORSPACE_DEFAULT;
	s->output_if = dsi->id ? VOP_OUTPUT_IF_MIPI1 : VOP_OUTPUT_IF_MIPI0;
	if (dsi->slave) {
		s->output_flags |= ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE;
		s->output_if |= VOP_OUTPUT_IF_MIPI1;
	}

	/* dual link dsi for rk3399 */
	if (dsi->id && dsi->cdata->soc_type == RK3399)
		s->output_flags |= ROCKCHIP_OUTPUT_DATA_SWAP;

	return 0;
}

static void dw_mipi_dsi_encoder_enable(struct drm_encoder *encoder)
{
	struct dw_mipi_dsi_rockchip *dsi = to_dsi(encoder);

	dw_mipi_dsi_rockchip_vop_routing(dsi);
}

static void dw_mipi_dsi_encoder_disable(struct drm_encoder *encoder)
{
}

static void dw_mipi_dsi_rockchip_loader_protect(struct dw_mipi_dsi_rockchip *dsi, bool on)
{
	if (on) {
		pm_runtime_get_sync(dsi->dev);
		phy_init(dsi->phy);
		dsi->phy_enabled = true;
		if (dsi->phy)
			dsi->phy->power_count++;
	} else {
		pm_runtime_put(dsi->dev);
		phy_exit(dsi->phy);
		dsi->phy_enabled = false;
		if (dsi->phy)
			dsi->phy->power_count--;
	}

	if (dsi->slave)
		dw_mipi_dsi_rockchip_loader_protect(dsi->slave, on);
}

static void dw_mipi_dsi_rockchip_encoder_loader_protect(struct drm_encoder *encoder,
					      bool on)
{
	struct dw_mipi_dsi_rockchip *dsi = to_dsi(encoder);

	if (dsi->panel)
		panel_simple_loader_protect(dsi->panel);

	dw_mipi_dsi_rockchip_loader_protect(dsi, on);
}

static const struct drm_encoder_helper_funcs
dw_mipi_dsi_encoder_helper_funcs = {
	.atomic_check = dw_mipi_dsi_encoder_atomic_check,
	.enable = dw_mipi_dsi_encoder_enable,
	.disable = dw_mipi_dsi_encoder_disable,
};

static int rockchip_dsi_drm_create_encoder(struct dw_mipi_dsi_rockchip *dsi,
					   struct drm_device *drm_dev)
{
	struct drm_encoder *encoder = &dsi->encoder;
	int ret;

	encoder->possible_crtcs = rockchip_drm_of_find_possible_crtcs(drm_dev,
								      dsi->dev->of_node);

	ret = drm_simple_encoder_init(drm_dev, encoder, DRM_MODE_ENCODER_DSI);
	if (ret) {
		DRM_ERROR("Failed to initialize encoder with drm\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &dw_mipi_dsi_encoder_helper_funcs);

	return 0;
}

static struct device
*dw_mipi_dsi_rockchip_find_second(struct dw_mipi_dsi_rockchip *dsi)
{
	struct device_node *node = NULL;
	struct platform_device *pdev;
	struct dw_mipi_dsi_rockchip *dsi2;

	node = of_parse_phandle(dsi->dev->of_node, "rockchip,dual-channel", 0);
	if (node) {
		pdev = of_find_device_by_node(node);
		if (!pdev)
			return ERR_PTR(-EPROBE_DEFER);

		dsi2 = platform_get_drvdata(pdev);
		if (!dsi2) {
			platform_device_put(pdev);
			return ERR_PTR(-EPROBE_DEFER);
		}

		return &pdev->dev;
	}

	return NULL;
}

static int dw_mipi_dsi_rockchip_bind(struct device *dev,
				     struct device *master,
				     void *data)
{
	struct dw_mipi_dsi_rockchip *dsi = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct device *second;
	int ret;

	second = dw_mipi_dsi_rockchip_find_second(dsi);
	if (IS_ERR(second))
		return PTR_ERR(second);

	if (second) {
		/* we are the slave in dual-DSI */
		dsi->slave = dev_get_drvdata(second);
		if (!dsi->slave) {
			DRM_DEV_ERROR(dev, "could not get slaves data\n");
			return -ENODEV;
		}

		dsi->slave->is_slave = true;
		dw_mipi_dsi_set_slave(dsi->dmd, dsi->slave->dmd);
		put_device(second);
	}

	if (dsi->is_slave)
		return 0;

	ret = clk_prepare_enable(dsi->pllref_clk);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to enable pllref_clk: %d\n", ret);
		return ret;
	}

	ret = rockchip_dsi_drm_create_encoder(dsi, drm_dev);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to create drm encoder\n");
		return ret;
	}

	ret = dw_mipi_dsi_bind(dsi->dmd, &dsi->encoder);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to bind: %d\n", ret);
		return ret;
	}

	ret = drm_of_find_panel_or_bridge(dsi->dev->of_node, 1, 0,
					  &dsi->panel, NULL);
	if (ret)
		dev_err(dsi->dev, "failed to find panel\n");

	dsi->sub_dev.connector = dw_mipi_dsi_get_connector(dsi->dmd);
	if (dsi->sub_dev.connector) {
		dsi->sub_dev.of_node = dev->of_node;
		dsi->sub_dev.loader_protect = dw_mipi_dsi_rockchip_encoder_loader_protect;
		rockchip_drm_register_sub_dev(&dsi->sub_dev);
	}

	return 0;
}

static void dw_mipi_dsi_rockchip_unbind(struct device *dev,
					struct device *master,
					void *data)
{
	struct dw_mipi_dsi_rockchip *dsi = dev_get_drvdata(dev);

	if (dsi->is_slave)
		return;

	if (dsi->sub_dev.connector)
		rockchip_drm_unregister_sub_dev(&dsi->sub_dev);

	dw_mipi_dsi_unbind(dsi->dmd);

	clk_disable_unprepare(dsi->pllref_clk);
}

static const struct component_ops dw_mipi_dsi_rockchip_ops = {
	.bind	= dw_mipi_dsi_rockchip_bind,
	.unbind	= dw_mipi_dsi_rockchip_unbind,
};

static int dw_mipi_dsi_rockchip_host_attach(void *priv_data,
					    struct mipi_dsi_device *device)
{
	struct dw_mipi_dsi_rockchip *dsi = priv_data;
	struct device *second;
	int ret;

	ret = component_add(dsi->dev, &dw_mipi_dsi_rockchip_ops);
	if (ret) {
		DRM_DEV_ERROR(dsi->dev, "Failed to register component: %d\n",
					ret);
		return ret;
	}

	second = dw_mipi_dsi_rockchip_find_second(dsi);
	if (IS_ERR(second))
		return PTR_ERR(second);
	if (second) {
		ret = component_add(second, &dw_mipi_dsi_rockchip_ops);
		if (ret) {
			DRM_DEV_ERROR(second,
				      "Failed to register component: %d\n",
				      ret);
			return ret;
		}
	}

	return 0;
}

static int dw_mipi_dsi_rockchip_host_detach(void *priv_data,
					    struct mipi_dsi_device *device)
{
	struct dw_mipi_dsi_rockchip *dsi = priv_data;
	struct device *second;

	second = dw_mipi_dsi_rockchip_find_second(dsi);
	if (second && !IS_ERR(second))
		component_del(second, &dw_mipi_dsi_rockchip_ops);

	component_del(dsi->dev, &dw_mipi_dsi_rockchip_ops);

	return 0;
}

static const struct dw_mipi_dsi_host_ops dw_mipi_dsi_rockchip_host_ops = {
	.attach = dw_mipi_dsi_rockchip_host_attach,
	.detach = dw_mipi_dsi_rockchip_host_detach,
};

static int dw_mipi_dsi_rockchip_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct dw_mipi_dsi_rockchip *dsi;
	struct resource *res;
	const struct rockchip_dw_dsi_chip_data *cdata =
				of_device_get_match_data(dev);
	int ret, i;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dsi->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(dsi->base)) {
		DRM_DEV_ERROR(dev, "Unable to get dsi registers\n");
		return PTR_ERR(dsi->base);
	}

	i = 0;
	while (cdata[i].reg) {
		if (cdata[i].reg == res->start) {
			dsi->cdata = &cdata[i];
			dsi->id = i;
			break;
		}

		i++;
	}

	if (!dsi->cdata) {
		DRM_DEV_ERROR(dev, "no dsi-config for %s node\n", np->name);
		return -EINVAL;
	}

	/* try to get a possible external dphy */
	dsi->phy = devm_phy_optional_get(dev, "dphy");
	if (IS_ERR(dsi->phy)) {
		ret = PTR_ERR(dsi->phy);
		DRM_DEV_ERROR(dev, "failed to get mipi dphy: %d\n", ret);
		return ret;
	}

	dsi->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dsi->pclk)) {
		ret = PTR_ERR(dsi->pclk);
		dev_err(dev, "Unable to get pclk: %d\n", ret);
		return ret;
	}

	dsi->pllref_clk = devm_clk_get(dev, "ref");
	if (IS_ERR(dsi->pllref_clk)) {
		if (dsi->phy) {
			/*
			 * if external phy is present, pll will be
			 * generated there.
			 */
			dsi->pllref_clk = NULL;
		} else {
			ret = PTR_ERR(dsi->pllref_clk);
			DRM_DEV_ERROR(dev,
				      "Unable to get pll reference clock: %d\n",
				      ret);
			return ret;
		}
	}

	if (dsi->cdata->flags & DW_MIPI_NEEDS_PHY_CFG_CLK) {
		dsi->phy_cfg_clk = devm_clk_get(dev, "phy_cfg");
		if (IS_ERR(dsi->phy_cfg_clk)) {
			ret = PTR_ERR(dsi->phy_cfg_clk);
			DRM_DEV_ERROR(dev,
				      "Unable to get phy_cfg_clk: %d\n", ret);
			return ret;
		}
	}

	if (dsi->cdata->flags & DW_MIPI_NEEDS_GRF_CLK) {
		dsi->grf_clk = devm_clk_get(dev, "grf");
		if (IS_ERR(dsi->grf_clk)) {
			ret = PTR_ERR(dsi->grf_clk);
			DRM_DEV_ERROR(dev, "Unable to get grf_clk: %d\n", ret);
			return ret;
		}
	}

	if (dsi->cdata->flags & DW_MIPI_NEEDS_HCLK) {
		dsi->hclk = devm_clk_get(dev, "hclk");
		if (IS_ERR(dsi->hclk)) {
			ret = PTR_ERR(dsi->hclk);
			DRM_DEV_ERROR(dev, "Unable to get hclk: %d\n", ret);
			return ret;
		}
	}

	dsi->grf_regmap = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(dsi->grf_regmap)) {
		DRM_DEV_ERROR(dsi->dev, "Unable to get rockchip,grf\n");
		return PTR_ERR(dsi->grf_regmap);
	}

	dsi->dev = dev;
	dsi->pdata.base = dsi->base;
	dsi->pdata.max_data_lanes = dsi->cdata->max_data_lanes;
	dsi->pdata.phy_ops = &dw_mipi_dsi_rockchip_phy_ops;
	dsi->pdata.host_ops = &dw_mipi_dsi_rockchip_host_ops;
	dsi->pdata.priv_data = dsi;
	platform_set_drvdata(pdev, dsi);

	dsi->dmd = dw_mipi_dsi_probe(pdev, &dsi->pdata);
	if (IS_ERR(dsi->dmd)) {
		ret = PTR_ERR(dsi->dmd);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev,
				      "Failed to probe dw_mipi_dsi: %d\n", ret);
		goto err_clkdisable;
	}

	return 0;

err_clkdisable:
	clk_disable_unprepare(dsi->pllref_clk);
	return ret;
}

static int dw_mipi_dsi_rockchip_remove(struct platform_device *pdev)
{
	struct dw_mipi_dsi_rockchip *dsi = platform_get_drvdata(pdev);

	if (dsi->devcnt == 0)
		component_del(dsi->dev, &dw_mipi_dsi_rockchip_ops);

	dw_mipi_dsi_remove(dsi->dmd);

	return 0;
}

static __maybe_unused int dw_mipi_dsi_runtime_suspend(struct device *dev)
{
	struct dw_mipi_dsi_rockchip *dsi = dev_get_drvdata(dev);

	clk_disable_unprepare(dsi->grf_clk);
	clk_disable_unprepare(dsi->pclk);
	clk_disable_unprepare(dsi->hclk);
	clk_disable_unprepare(dsi->phy_cfg_clk);

	return 0;
}

static __maybe_unused int dw_mipi_dsi_runtime_resume(struct device *dev)
{
	struct dw_mipi_dsi_rockchip *dsi = dev_get_drvdata(dev);

	clk_prepare_enable(dsi->phy_cfg_clk);
	clk_prepare_enable(dsi->hclk);
	clk_prepare_enable(dsi->pclk);
	clk_prepare_enable(dsi->grf_clk);

	return 0;
}

static const struct dev_pm_ops dw_mipi_dsi_rockchip_pm_ops = {
	SET_RUNTIME_PM_OPS(dw_mipi_dsi_runtime_suspend,
			   dw_mipi_dsi_runtime_resume, NULL)
};

static const struct rockchip_dw_dsi_chip_data px30_chip_data[] = {
	{
		.reg = 0xff450000,
		.lcdsel_grf_reg = PX30_GRF_PD_VO_CON1,
		.lcdsel_big = HIWORD_UPDATE(0, PX30_DSI_LCDC_SEL),
		.lcdsel_lit = HIWORD_UPDATE(PX30_DSI_LCDC_SEL,
					    PX30_DSI_LCDC_SEL),

		.lanecfg1_grf_reg = PX30_GRF_PD_VO_CON1,
		.lanecfg1 = HIWORD_UPDATE(0, PX30_DSI_TURNDISABLE |
					     PX30_DSI_FORCERXMODE |
					     PX30_DSI_FORCETXSTOPMODE),

		.max_data_lanes = 4,
		.max_bit_rate_per_lane = 1000000000UL,
		.soc_type = PX30,
	},
	{ /* sentinel */ }
};

static const struct rockchip_dw_dsi_chip_data rk3288_chip_data[] = {
	{
		.reg = 0xff960000,
		.lcdsel_grf_reg = RK3288_GRF_SOC_CON6,
		.lcdsel_big = HIWORD_UPDATE(0, RK3288_DSI0_LCDC_SEL),
		.lcdsel_lit = HIWORD_UPDATE(RK3288_DSI0_LCDC_SEL, RK3288_DSI0_LCDC_SEL),

		.max_data_lanes = 4,
		.max_bit_rate_per_lane = 1500000000UL,
		.soc_type = RK3288,
	},
	{
		.reg = 0xff964000,
		.lcdsel_grf_reg = RK3288_GRF_SOC_CON6,
		.lcdsel_big = HIWORD_UPDATE(0, RK3288_DSI1_LCDC_SEL),
		.lcdsel_lit = HIWORD_UPDATE(RK3288_DSI1_LCDC_SEL, RK3288_DSI1_LCDC_SEL),

		.max_data_lanes = 4,
		.max_bit_rate_per_lane = 1500000000UL,
		.soc_type = RK3288,
	},
	{ /* sentinel */ }
};

static const struct rockchip_dw_dsi_chip_data rk3399_chip_data[] = {
	{
		.reg = 0xff960000,
		.lcdsel_grf_reg = RK3399_GRF_SOC_CON20,
		.lcdsel_big = HIWORD_UPDATE(0, RK3399_DSI0_LCDC_SEL),
		.lcdsel_lit = HIWORD_UPDATE(RK3399_DSI0_LCDC_SEL,
					    RK3399_DSI0_LCDC_SEL),

		.lanecfg1_grf_reg = RK3399_GRF_SOC_CON22,
		.lanecfg1 = HIWORD_UPDATE(0, RK3399_DSI0_TURNREQUEST |
					     RK3399_DSI0_TURNDISABLE |
					     RK3399_DSI0_FORCETXSTOPMODE |
					     RK3399_DSI0_FORCERXMODE),

		.flags = DW_MIPI_NEEDS_PHY_CFG_CLK | DW_MIPI_NEEDS_GRF_CLK,
		.max_data_lanes = 4,
		.max_bit_rate_per_lane = 1500000000UL,
		.soc_type = RK3399,
	},
	{
		.reg = 0xff968000,
		.lcdsel_grf_reg = RK3399_GRF_SOC_CON20,
		.lcdsel_big = HIWORD_UPDATE(0, RK3399_DSI1_LCDC_SEL),
		.lcdsel_lit = HIWORD_UPDATE(RK3399_DSI1_LCDC_SEL,
					    RK3399_DSI1_LCDC_SEL),

		.lanecfg1_grf_reg = RK3399_GRF_SOC_CON23,
		.lanecfg1 = HIWORD_UPDATE(0, RK3399_DSI1_TURNDISABLE |
					     RK3399_DSI1_FORCETXSTOPMODE |
					     RK3399_DSI1_FORCERXMODE |
					     RK3399_DSI1_ENABLE),

		.lanecfg2_grf_reg = RK3399_GRF_SOC_CON24,
		.lanecfg2 = HIWORD_UPDATE(RK3399_TXRX_MASTERSLAVEZ |
					  RK3399_TXRX_ENABLECLK,
					  RK3399_TXRX_MASTERSLAVEZ |
					  RK3399_TXRX_ENABLECLK |
					  RK3399_TXRX_BASEDIR),

		.enable_grf_reg = RK3399_GRF_SOC_CON23,
		.enable = HIWORD_UPDATE(RK3399_DSI1_ENABLE, RK3399_DSI1_ENABLE),

		.flags = DW_MIPI_NEEDS_PHY_CFG_CLK | DW_MIPI_NEEDS_GRF_CLK,
		.max_data_lanes = 4,
		.max_bit_rate_per_lane = 1500000000UL,
		.soc_type = RK3399,
	},
	{ /* sentinel */ }
};

static const struct rockchip_dw_dsi_chip_data rk3568_chip_data[] = {
	{
		.reg = 0xfe060000,

		.lanecfg1_grf_reg = RK3568_GRF_VO_CON2,
		.lanecfg1 = HIWORD_UPDATE(0, RK3568_DSI_TURNDISABLE |
					     RK3568_DSI_FORCERXMODE |
					     RK3568_DSI_FORCETXSTOPMODE),

		.flags = DW_MIPI_NEEDS_HCLK,
		.max_data_lanes = 4,
		.max_bit_rate_per_lane = 1200000000UL,
		.soc_type = RK3568,
	},
	{
		.reg = 0xfe070000,

		.lanecfg1_grf_reg = RK3568_GRF_VO_CON3,
		.lanecfg1 = HIWORD_UPDATE(0, RK3568_DSI_TURNDISABLE |
					     RK3568_DSI_FORCERXMODE |
					     RK3568_DSI_FORCETXSTOPMODE),

		.flags = DW_MIPI_NEEDS_HCLK,
		.max_data_lanes = 4,
		.max_bit_rate_per_lane = 1200000000UL,
		.soc_type = RK3568,
	},
	{ /* sentinel */ }
};

static const struct of_device_id dw_mipi_dsi_rockchip_dt_ids[] = {
	{
	 .compatible = "rockchip,px30-mipi-dsi",
	 .data = &px30_chip_data,
	}, {
	 .compatible = "rockchip,rk3288-mipi-dsi",
	 .data = &rk3288_chip_data,
	}, {
	 .compatible = "rockchip,rk3399-mipi-dsi",
	 .data = &rk3399_chip_data,
	}, {
	 .compatible = "rockchip,rk3568-mipi-dsi",
	 .data = &rk3568_chip_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dw_mipi_dsi_rockchip_dt_ids);

struct platform_driver dw_mipi_dsi_rockchip_driver = {
	.probe		= dw_mipi_dsi_rockchip_probe,
	.remove		= dw_mipi_dsi_rockchip_remove,
	.driver		= {
		.of_match_table = dw_mipi_dsi_rockchip_dt_ids,
		.pm = &dw_mipi_dsi_rockchip_pm_ops,
		.name	= "dw-mipi-dsi-rockchip",
	},
};
