// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:
 *      Chris Zhong <zyw@rock-chips.com>
 *      Nickey Yang <nickey.yang@rock-chips.com>
 */

#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/bridge/dw_mipi_dsi.h>
#include <drm/drm_of.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/math64.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <video/mipi_display.h>

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

	unsigned int flags;
	unsigned int max_data_lanes;
};

struct dw_mipi_dsi_rockchip {
	struct device *dev;
	struct drm_encoder encoder;
	void __iomem *base;

	struct regmap *grf_regmap;
	struct clk *pllref_clk;
	struct clk *grf_clk;
	struct clk *phy_cfg_clk;

	/* dual-channel */
	bool is_slave;
	struct dw_mipi_dsi_rockchip *slave;

	unsigned int lane_mbps; /* per lane */
	u16 input_div;
	u16 feedback_div;
	u32 format;

	struct dw_mipi_dsi *dmd;
	const struct rockchip_dw_dsi_chip_data *cdata;
	struct dw_mipi_dsi_plat_data pdata;
	int devcnt;
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

static int dw_mipi_dsi_phy_init(void *priv_data)
{
	struct dw_mipi_dsi_rockchip *dsi = priv_data;
	int ret, i, vco;

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

	ret = clk_prepare_enable(dsi->phy_cfg_clk);
	if (ret) {
		DRM_DEV_ERROR(dsi->dev, "Failed to enable phy_cfg_clk\n");
		return ret;
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
			      TLP_PROGRAM_EN | ns2bc(dsi, 500));
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
			      TLP_PROGRAM_EN | ns2bc(dsi, 500));
	dw_mipi_dsi_phy_write(dsi, HS_TX_DATA_LANE_PREPARE_STATE_TIME_CONTROL,
			      THS_PRE_PROGRAM_EN | (ns2ui(dsi, 50) + 20));
	dw_mipi_dsi_phy_write(dsi, HS_TX_DATA_LANE_HS_ZERO_STATE_TIME_CONTROL,
			      THS_ZERO_PROGRAM_EN | (ns2bc(dsi, 140) + 2));
	dw_mipi_dsi_phy_write(dsi, HS_TX_DATA_LANE_TRAIL_STATE_TIME_CONTROL,
			      THS_PRE_PROGRAM_EN | (ns2ui(dsi, 60) + 8));
	dw_mipi_dsi_phy_write(dsi, HS_TX_DATA_LANE_EXIT_STATE_TIME_CONTROL,
			      BIT(5) | ns2bc(dsi, 100));

	clk_disable_unprepare(dsi->phy_cfg_clk);

	return ret;
}

static int
dw_mipi_dsi_get_lane_mbps(void *priv_data, struct drm_display_mode *mode,
			  unsigned long mode_flags, u32 lanes, u32 format,
			  unsigned int *lane_mbps)
{
	struct dw_mipi_dsi_rockchip *dsi = priv_data;
	int bpp;
	unsigned long mpclk, tmp;
	unsigned int target_mbps = 1000;
	unsigned int max_mbps = dppa_map[ARRAY_SIZE(dppa_map) - 1].max_mbps;
	unsigned long best_freq = 0;
	unsigned long fvco_min, fvco_max, fin, fout;
	unsigned int min_prediv, max_prediv;
	unsigned int _prediv, uninitialized_var(best_prediv);
	unsigned long _fbdiv, uninitialized_var(best_fbdiv);
	unsigned long min_delta = ULONG_MAX;

	dsi->format = format;
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
		tmp = mpclk * (bpp / lanes) * 10 / 8;
		if (tmp < max_mbps)
			target_mbps = tmp;
		else
			DRM_DEV_ERROR(dsi->dev,
				      "DPHY clock frequency is out of range\n");
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

static const struct dw_mipi_dsi_phy_ops dw_mipi_dsi_rockchip_phy_ops = {
	.init = dw_mipi_dsi_phy_init,
	.get_lane_mbps = dw_mipi_dsi_get_lane_mbps,
};

static void dw_mipi_dsi_rockchip_config(struct dw_mipi_dsi_rockchip *dsi,
					int mux)
{
	if (dsi->cdata->lcdsel_grf_reg)
		regmap_write(dsi->grf_regmap, dsi->cdata->lcdsel_grf_reg,
			mux ? dsi->cdata->lcdsel_lit : dsi->cdata->lcdsel_big);

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

static int
dw_mipi_dsi_encoder_atomic_check(struct drm_encoder *encoder,
				 struct drm_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct dw_mipi_dsi_rockchip *dsi = to_dsi(encoder);

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
	if (dsi->slave)
		s->output_flags = ROCKCHIP_OUTPUT_DSI_DUAL;

	return 0;
}

static void dw_mipi_dsi_encoder_enable(struct drm_encoder *encoder)
{
	struct dw_mipi_dsi_rockchip *dsi = to_dsi(encoder);
	int ret, mux;

	mux = drm_of_encoder_active_endpoint_id(dsi->dev->of_node,
						&dsi->encoder);
	if (mux < 0)
		return;

	pm_runtime_get_sync(dsi->dev);
	if (dsi->slave)
		pm_runtime_get_sync(dsi->slave->dev);

	/*
	 * For the RK3399, the clk of grf must be enabled before writing grf
	 * register. And for RK3288 or other soc, this grf_clk must be NULL,
	 * the clk_prepare_enable return true directly.
	 */
	ret = clk_prepare_enable(dsi->grf_clk);
	if (ret) {
		DRM_DEV_ERROR(dsi->dev, "Failed to enable grf_clk: %d\n", ret);
		return;
	}

	dw_mipi_dsi_rockchip_config(dsi, mux);
	if (dsi->slave)
		dw_mipi_dsi_rockchip_config(dsi->slave, mux);

	clk_disable_unprepare(dsi->grf_clk);
}

static void dw_mipi_dsi_encoder_disable(struct drm_encoder *encoder)
{
	struct dw_mipi_dsi_rockchip *dsi = to_dsi(encoder);

	if (dsi->slave)
		pm_runtime_put(dsi->slave->dev);
	pm_runtime_put(dsi->dev);
}

static const struct drm_encoder_helper_funcs
dw_mipi_dsi_encoder_helper_funcs = {
	.atomic_check = dw_mipi_dsi_encoder_atomic_check,
	.enable = dw_mipi_dsi_encoder_enable,
	.disable = dw_mipi_dsi_encoder_disable,
};

static const struct drm_encoder_funcs dw_mipi_dsi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int rockchip_dsi_drm_create_encoder(struct dw_mipi_dsi_rockchip *dsi,
					   struct drm_device *drm_dev)
{
	struct drm_encoder *encoder = &dsi->encoder;
	int ret;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev,
							     dsi->dev->of_node);

	ret = drm_encoder_init(drm_dev, encoder, &dw_mipi_dsi_encoder_funcs,
			       DRM_MODE_ENCODER_DSI, NULL);
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
	const struct of_device_id *match;
	struct device_node *node = NULL, *local;

	match = of_match_device(dsi->dev->driver->of_match_table, dsi->dev);

	local = of_graph_get_remote_node(dsi->dev->of_node, 1, 0);
	if (!local)
		return NULL;

	while ((node = of_find_compatible_node(node, NULL,
					       match->compatible))) {
		struct device_node *remote;

		/* found ourself */
		if (node == dsi->dev->of_node)
			continue;

		remote = of_graph_get_remote_node(node, 1, 0);
		if (!remote)
			continue;

		/* same display device in port1-ep0 for both */
		if (remote == local) {
			struct dw_mipi_dsi_rockchip *dsi2;
			struct platform_device *pdev;

			pdev = of_find_device_by_node(node);

			/*
			 * we have found the second, so will either return it
			 * or return with an error. In any case won't need the
			 * nodes anymore nor continue the loop.
			 */
			of_node_put(remote);
			of_node_put(node);
			of_node_put(local);

			if (!pdev)
				return ERR_PTR(-EPROBE_DEFER);

			dsi2 = platform_get_drvdata(pdev);
			if (!dsi2) {
				platform_device_put(pdev);
				return ERR_PTR(-EPROBE_DEFER);
			}

			return &pdev->dev;
		}

		of_node_put(remote);
	}

	of_node_put(local);

	return NULL;
}

static int dw_mipi_dsi_rockchip_bind(struct device *dev,
				     struct device *master,
				     void *data)
{
	struct dw_mipi_dsi_rockchip *dsi = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct device *second;
	bool master1, master2;
	int ret;

	second = dw_mipi_dsi_rockchip_find_second(dsi);
	if (IS_ERR(second))
		return PTR_ERR(second);

	if (second) {
		master1 = of_property_read_bool(dsi->dev->of_node,
						"clock-master");
		master2 = of_property_read_bool(second->of_node,
						"clock-master");

		if (master1 && master2) {
			DRM_DEV_ERROR(dsi->dev, "only one clock-master allowed\n");
			return -EINVAL;
		}

		if (!master1 && !master2) {
			DRM_DEV_ERROR(dsi->dev, "no clock-master defined\n");
			return -EINVAL;
		}

		/* we are the slave in dual-DSI */
		if (!master1) {
			dsi->is_slave = true;
			return 0;
		}

		dsi->slave = dev_get_drvdata(second);
		if (!dsi->slave) {
			DRM_DEV_ERROR(dev, "could not get slaves data\n");
			return -ENODEV;
		}

		dsi->slave->is_slave = true;
		dw_mipi_dsi_set_slave(dsi->dmd, dsi->slave->dmd);
		put_device(second);
	}

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

	return 0;
}

static void dw_mipi_dsi_rockchip_unbind(struct device *dev,
					struct device *master,
					void *data)
{
	struct dw_mipi_dsi_rockchip *dsi = dev_get_drvdata(dev);

	if (dsi->is_slave)
		return;

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
			break;
		}

		i++;
	}

	if (!dsi->cdata) {
		dev_err(dev, "no dsi-config for %s node\n", np->name);
		return -EINVAL;
	}

	dsi->pllref_clk = devm_clk_get(dev, "ref");
	if (IS_ERR(dsi->pllref_clk)) {
		ret = PTR_ERR(dsi->pllref_clk);
		DRM_DEV_ERROR(dev,
			      "Unable to get pll reference clock: %d\n", ret);
		return ret;
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

static const struct rockchip_dw_dsi_chip_data rk3288_chip_data[] = {
	{
		.reg = 0xff960000,
		.lcdsel_grf_reg = RK3288_GRF_SOC_CON6,
		.lcdsel_big = HIWORD_UPDATE(0, RK3288_DSI0_LCDC_SEL),
		.lcdsel_lit = HIWORD_UPDATE(RK3288_DSI0_LCDC_SEL, RK3288_DSI0_LCDC_SEL),

		.max_data_lanes = 4,
	},
	{
		.reg = 0xff964000,
		.lcdsel_grf_reg = RK3288_GRF_SOC_CON6,
		.lcdsel_big = HIWORD_UPDATE(0, RK3288_DSI1_LCDC_SEL),
		.lcdsel_lit = HIWORD_UPDATE(RK3288_DSI1_LCDC_SEL, RK3288_DSI1_LCDC_SEL),

		.max_data_lanes = 4,
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
	},
	{ /* sentinel */ }
};

static const struct of_device_id dw_mipi_dsi_rockchip_dt_ids[] = {
	{
	 .compatible = "rockchip,rk3288-mipi-dsi",
	 .data = &rk3288_chip_data,
	}, {
	 .compatible = "rockchip,rk3399-mipi-dsi",
	 .data = &rk3399_chip_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dw_mipi_dsi_rockchip_dt_ids);

struct platform_driver dw_mipi_dsi_rockchip_driver = {
	.probe		= dw_mipi_dsi_rockchip_probe,
	.remove		= dw_mipi_dsi_rockchip_remove,
	.driver		= {
		.of_match_table = dw_mipi_dsi_rockchip_dt_ids,
		.name	= "dw-mipi-dsi-rockchip",
	},
};
