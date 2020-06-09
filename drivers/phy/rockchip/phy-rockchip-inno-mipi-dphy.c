/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <linux/pm_runtime.h>
#include <linux/mfd/syscon.h>

#define UPDATE(x, h, l)	(((x) << (l)) & GENMASK((h), (l)))

/*
 * The offset address[7:0] is distributed two parts, one from the bit7 to bit5
 * is the first address, the other from the bit4 to bit0 is the second address.
 * when you configure the registers, you must set both of them. The Clock Lane
 * and Data Lane use the same registers with the same second address, but the
 * first address is different.
 */
#define FIRST_ADDRESS(x)		(((x) & 0x7) << 5)
#define SECOND_ADDRESS(x)		(((x) & 0x1f) << 0)
#define INNO_PHY_REG(first, second)	(FIRST_ADDRESS(first) | \
					 SECOND_ADDRESS(second))

/* Analog Register Part: reg00 */
#define BANDGAP_POWER_MASK		BIT(7)
#define BANDGAP_POWER_DOWN		BIT(7)
#define BANDGAP_POWER_ON		0
#define LANE_EN_MASK			GENMASK(6, 2)
#define LANE_EN_CK			BIT(6)
#define LANE_EN_3			BIT(5)
#define LANE_EN_2			BIT(4)
#define LANE_EN_1			BIT(3)
#define LANE_EN_0			BIT(2)
#define POWER_WORK_MASK			GENMASK(1, 0)
#define POWER_WORK_ENABLE		UPDATE(1, 1, 0)
#define POWER_WORK_DISABLE		UPDATE(2, 1, 0)
/* Analog Register Part: reg01 */
#define REG_SYNCRST_MASK		BIT(2)
#define REG_SYNCRST_RESET		BIT(2)
#define REG_SYNCRST_NORMAL		0
#define REG_LDOPD_MASK			BIT(1)
#define REG_LDOPD_POWER_DOWN		BIT(1)
#define REG_LDOPD_POWER_ON		0
#define REG_PLLPD_MASK			BIT(0)
#define REG_PLLPD_POWER_DOWN		BIT(0)
#define REG_PLLPD_POWER_ON		0
/* Analog Register Part: reg03 */
#define REG_FBDIV_HI_MASK		BIT(5)
#define REG_FBDIV_HI(x)			UPDATE(x, 5, 5)
#define REG_PREDIV_MASK			GENMASK(4, 0)
#define REG_PREDIV(x)			UPDATE(x, 4, 0)
/* Analog Register Part: reg04 */
#define REG_FBDIV_LO_MASK		GENMASK(7, 0)
#define REG_FBDIV_LO(x)			UPDATE(x, 7, 0)
/* Analog Register Part: reg05 */
#define CLK_LANE_SKEW_PHASE_SET_MASK	GENMASK(2, 0)
#define CLK_LANE_SKEW_PHASE_SET(x)	UPDATE(x, 2, 0)
/* Analog Register Part: reg06 */
#define LDO_OUTPUT_SET_HI_MASK		BIT(7)
#define LDO_OUTPUT_SET_HI(x)		UPDATE(x, 7, 7)
#define LANE_3_SKEW_PHASE_SET_MASK	GENMASK(6, 4)
#define LANE_3_SKEW_PHASE_SET(x)	UPDATE(x, 6, 4)
#define LDO_OUTPUT_SET_LO_MASK		BIT(3)
#define LDO_OUTPUT_SET_LO(x)		UPDATE(x, 3, 3)
#define LANE_2_SKEW_PHASE_SET_MASK	GENMASK(2, 0)
#define LANE_2_SKEW_PHASE_SET(x)	UPDATE(x, 2, 0)
/* Analog Register Part: reg07 */
#define PRE_EMPHASIS_RANGE_SET_HI_MASK	BIT(7)
#define PRE_EMPHASIS_RANGE_SET_HI(x)	UPDATE(x, 7, 7)
#define LANE_1_SKEW_PHASE_SET_MASK	GENMASK(6, 4)
#define LANE_1_SKEW_PHASE_SET(x)	UPDATE(x, 6, 4)
#define PRE_EMPHASIS_RANGE_SET_LO_MASK	BIT(3)
#define PRE_EMPHASIS_RANGE_SET_LO(x)	UPDATE(x, 3, 3)
#define LANE_0_SKEW_PHASE_SET_MASK	GENMASK(2, 0)
#define LANE_0_SKEW_PHASE_SET(x)	UPDATE(x, 2, 0)
/* Analog Register Part: reg08 */
#define PRE_EMPHASIS_ENABLE_MASK	BIT(7)
#define PRE_EMPHASIS_ENABLE		BIT(7)
#define PRE_EMPHASIS_DISABLE		0
#define PLL_POST_DIV_ENABLE_MASK	BIT(5)
#define PLL_POST_DIV_ENABLE		BIT(5)
#define PLL_POST_DIV_DISABLE		0
#define DATA_LANE_VOD_RANGE_SET_MASK	GENMASK(3, 0)
#define DATA_LANE_VOD_RANGE_SET(x)	UPDATE(x, 3, 0)
/* Analog Register Part: reg0b */
#define CLOCK_LANE_VOD_RANGE_SET_MASK	GENMASK(3, 0)
#define CLOCK_LANE_VOD_RANGE_SET(x)	UPDATE(x, 3, 0)
#define VOD_MIN_RANGE			0x1
#define VOD_MID_RANGE			0x3
#define VOD_BIG_RANGE			0x7
#define VOD_MAX_RANGE			0xf
/* Analog Register Part: reg11 */
#define DATA_SAMPLE_PHASE_SET_MASK	GENMASK(7, 6)
#define DATA_SAMPLE_PHASE_SET(x)	UPDATE(x, 7, 6)
/* Digital Register Part: reg00 */
#define REG_DIG_RSTN_MASK		BIT(0)
#define REG_DIG_RSTN_NORMAL		BIT(0)
#define REG_DIG_RSTN_RESET		0
/* Digital Register Part: reg01 */
#define INV_PIN_TXCLKESC_0_ENABLE_MASK	BIT(1)
#define INV_PIN_TXCLKESC_0_ENABLE	BIT(1)
#define INV_PIN_TXCLKESC_0_DISABLE	0
#define INV_PIN_TXBYTECLKHS_ENABLE_MASK	BIT(0)
#define INV_PIN_TXBYTECLKHS_ENABLE	BIT(0)
#define INV_PIN_TXBYTECLKHS_DISABLE	0
/* Clock/Data0/Data1/Data2/Data3 Lane Register Part: reg00 */
#define DIFF_SIGNAL_SWAP_ENABLE_MASK	BIT(4)
#define DIFF_SIGNAL_SWAP_ENABLE		BIT(4)
#define DIFF_SIGNAL_SWAP_DISABLE	0
/* Clock/Data0/Data1/Data2/Data3 Lane Register Part: reg05 */
#define T_LPX_CNT_MASK			GENMASK(5, 0)
#define T_LPX_CNT(x)			UPDATE(x, 5, 0)
/* Clock/Data0/Data1/Data2/Data3 Lane Register Part: reg06 */
#define T_HS_ZERO_CNT_HI_MASK		BIT(7)
#define T_HS_ZERO_CNT_HI(x)		UPDATE(x, 7, 7)
#define T_HS_PREPARE_CNT_MASK		GENMASK(6, 0)
#define T_HS_PREPARE_CNT(x)		UPDATE(x, 6, 0)
/* Clock/Data0/Data1/Data2/Data3 Lane Register Part: reg07 */
#define T_HS_ZERO_CNT_LO_MASK		GENMASK(5, 0)
#define T_HS_ZERO_CNT_LO(x)		UPDATE(x, 5, 0)
/* Clock/Data0/Data1/Data2/Data3 Lane Register Part: reg08 */
#define T_HS_TRAIL_CNT_MASK		GENMASK(6, 0)
#define T_HS_TRAIL_CNT(x)		UPDATE(x, 6, 0)
/* Clock/Data0/Data1/Data2/Data3 Lane Register Part: reg09 */
#define T_HS_EXIT_CNT_LO_MASK		GENMASK(4, 0)
#define T_HS_EXIT_CNT_LO(x)		UPDATE(x, 4, 0)
/* Clock/Data0/Data1/Data2/Data3 Lane Register Part: reg0a */
#define T_CLK_POST_CNT_LO_MASK		GENMASK(3, 0)
#define T_CLK_POST_CNT_LO(x)		UPDATE(x, 3, 0)
/* Clock/Data0/Data1/Data2/Data3 Lane Register Part: reg0c */
#define LPDT_TX_PPI_SYNC_ENABLE_MASK	BIT(2)
#define LPDT_TX_PPI_SYNC_ENABLE		BIT(2)
#define LPDT_TX_PPI_SYNC_DISABLE	0
#define T_WAKEUP_CNT_HI_MASK		GENMASK(1, 0)
#define T_WAKEUP_CNT_HI(x)		UPDATE(x, 1, 0)
/* Clock/Data0/Data1/Data2/Data3 Lane Register Part: reg0d */
#define T_WAKEUP_CNT_LO_MASK		GENMASK(7, 0)
#define T_WAKEUP_CNT_LO(x)		UPDATE(x, 7, 0)
/* Clock/Data0/Data1/Data2/Data3 Lane Register Part: reg0e */
#define T_CLK_PRE_CNT_MASK		GENMASK(3, 0)
#define T_CLK_PRE_CNT(x)		UPDATE(x, 3, 0)
/* Clock/Data0/Data1/Data2/Data3 Lane Register Part: reg10 */
#define T_CLK_POST_HI_MASK		GENMASK(7, 6)
#define T_CLK_POST_HI(x)		UPDATE(x, 7, 6)
#define T_TA_GO_CNT_MASK		GENMASK(5, 0)
#define T_TA_GO_CNT(x)			UPDATE(x, 5, 0)
/* Clock/Data0/Data1/Data2/Data3 Lane Register Part: reg11 */
#define T_HS_EXIT_CNT_HI_MASK		BIT(6)
#define T_HS_EXIT_CNT_HI(x)		UPDATE(x, 6, 6)
#define T_TA_SURE_CNT_MASK		GENMASK(5, 0)
#define T_TA_SURE_CNT(x)		UPDATE(x, 5, 0)
/* Clock/Data0/Data1/Data2/Data3 Lane Register Part: reg12 */
#define T_TA_WAIT_CNT_MASK		GENMASK(5, 0)
#define T_TA_WAIT_CNT(x)		UPDATE(x, 5, 0)

#define PSEC_PER_NSEC	1000L
#define PSECS_PER_SEC	1000000000000LL

enum inno_video_phy_functions {
	INNO_PHY_PADCTL_FUNC_MIPI,
	INNO_PHY_PADCTL_FUNC_LVDS,
	INNO_PHY_PADCTL_FUNC_TTL,
	INNO_PHY_PADCTL_FUNC_IDLE,
};

struct mipi_dphy_timing {
	unsigned int clkmiss;
	unsigned int clkpost;
	unsigned int clkpre;
	unsigned int clkprepare;
	unsigned int clksettle;
	unsigned int clktermen;
	unsigned int clktrail;
	unsigned int clkzero;
	unsigned int dtermen;
	unsigned int eot;
	unsigned int hsexit;
	unsigned int hsprepare;
	unsigned int hszero;
	unsigned int hssettle;
	unsigned int hsskip;
	unsigned int hstrail;
	unsigned int init;
	unsigned int lpx;
	unsigned int taget;
	unsigned int tago;
	unsigned int tasure;
	unsigned int wakeup;
};

struct inno_mipi_dphy_timing {
	unsigned int max_lane_mbps;
	u8 lpx;
	u8 hs_prepare;
	u8 clk_lane_hs_zero;
	u8 data_lane_hs_zero;
	u8 hs_trail;
};

struct inno_mipi_dphy {
	struct device *dev;
	struct clk *ref_clk;
	struct clk *pclk;
	struct regmap *regmap;
	struct reset_control *rst;
	struct regmap *grf;

	unsigned int lanes;
	unsigned long lane_rate;

	struct {
		struct clk_hw hw;
		u8 prediv;
		u16 fbdiv;
	} pll;
};

enum {
	REGISTER_PART_ANALOG,
	REGISTER_PART_DIGITAL,
	REGISTER_PART_CLOCK_LANE,
	REGISTER_PART_DATA0_LANE,
	REGISTER_PART_DATA1_LANE,
	REGISTER_PART_DATA2_LANE,
	REGISTER_PART_DATA3_LANE,
};

static const
struct inno_mipi_dphy_timing inno_mipi_dphy_timing_table[] = {
	{ 110, 0x02, 0x7f, 0x16, 0x02, 0x02},
	{ 150, 0x02, 0x7f, 0x16, 0x03, 0x02},
	{ 200, 0x02, 0x7f, 0x17, 0x04, 0x02},
	{ 250, 0x02, 0x7f, 0x17, 0x05, 0x04},
	{ 300, 0x02, 0x7f, 0x18, 0x06, 0x04},
	{ 400, 0x03, 0x7e, 0x19, 0x07, 0x04},
	{ 500, 0x03, 0x7c, 0x1b, 0x07, 0x08},
	{ 600, 0x03, 0x70, 0x1d, 0x08, 0x10},
	{ 700, 0x05, 0x40, 0x1e, 0x08, 0x30},
	{ 800, 0x05, 0x02, 0x1f, 0x09, 0x30},
	{1000, 0x05, 0x08, 0x20, 0x09, 0x30},
	{1200, 0x06, 0x03, 0x32, 0x14, 0x0f},
	{1400, 0x09, 0x03, 0x32, 0x14, 0x0f},
	{1600, 0x0d, 0x42, 0x36, 0x0e, 0x0f},
	{1800, 0x0e, 0x47, 0x7a, 0x0e, 0x0f},
	{2000, 0x11, 0x64, 0x7a, 0x0e, 0x0b},
	{2200, 0x13, 0x64, 0x7e, 0x15, 0x0b},
	{2400, 0x13, 0x33, 0x7f, 0x15, 0x6a},
	{2500, 0x15, 0x54, 0x7f, 0x15, 0x6a},
};

static inline struct inno_mipi_dphy *hw_to_inno(struct clk_hw *hw)
{
	return container_of(hw, struct inno_mipi_dphy, pll.hw);
}

static void inno_update_bits(struct inno_mipi_dphy *inno, u8 first, u8 second,
			     u8 mask, u8 val)
{
	u32 reg = INNO_PHY_REG(first, second) << 2;

	regmap_update_bits(inno->regmap, reg, mask, val);
}

static void inno_mipi_dphy_reset(struct inno_mipi_dphy *inno)
{
	/* Reset analog */
	inno_update_bits(inno, REGISTER_PART_ANALOG, 0x01,
			 REG_SYNCRST_MASK, REG_SYNCRST_RESET);
	udelay(1);
	inno_update_bits(inno, REGISTER_PART_ANALOG, 0x01,
			 REG_SYNCRST_MASK, REG_SYNCRST_NORMAL);
	/* Reset digital */
	inno_update_bits(inno, REGISTER_PART_DIGITAL, 0x00,
			 REG_DIG_RSTN_MASK, REG_DIG_RSTN_RESET);
	udelay(1);
	inno_update_bits(inno, REGISTER_PART_DIGITAL, 0x00,
			 REG_DIG_RSTN_MASK, REG_DIG_RSTN_NORMAL);
}

static void inno_mipi_dphy_power_work_enable(struct inno_mipi_dphy *inno)
{
	inno_update_bits(inno, REGISTER_PART_ANALOG, 0x00,
			 POWER_WORK_MASK, POWER_WORK_ENABLE);
}

static void inno_mipi_dphy_power_work_disable(struct inno_mipi_dphy *inno)
{
	inno_update_bits(inno, REGISTER_PART_ANALOG, 0x00,
			 POWER_WORK_MASK, POWER_WORK_DISABLE);
}

static void inno_mipi_dphy_bandgap_power_enable(struct inno_mipi_dphy *inno)
{
	inno_update_bits(inno, REGISTER_PART_ANALOG, 0x00,
			 BANDGAP_POWER_MASK, BANDGAP_POWER_ON);
}

static void inno_mipi_dphy_bandgap_power_disable(struct inno_mipi_dphy *inno)
{
	inno_update_bits(inno, REGISTER_PART_ANALOG, 0x00,
			 BANDGAP_POWER_MASK, BANDGAP_POWER_DOWN);
}

static void inno_mipi_dphy_lane_enable(struct inno_mipi_dphy *inno)
{
	u8 val = LANE_EN_CK;

	switch (inno->lanes) {
	case 1:
		val |= LANE_EN_0;
		break;
	case 2:
		val |= LANE_EN_1 | LANE_EN_0;
		break;
	case 3:
		val |= LANE_EN_2 | LANE_EN_1 | LANE_EN_0;
		break;
	case 4:
	default:
		val |= LANE_EN_3 | LANE_EN_2 | LANE_EN_1 | LANE_EN_0;
		break;
	}

	inno_update_bits(inno, REGISTER_PART_ANALOG, 0x00, LANE_EN_MASK, val);
}

static void inno_mipi_dphy_lane_disable(struct inno_mipi_dphy *inno)
{
	inno_update_bits(inno, REGISTER_PART_ANALOG, 0x00, LANE_EN_MASK, 0);
}

static void inno_mipi_dphy_pll_enable(struct inno_mipi_dphy *inno)
{
	inno_update_bits(inno, REGISTER_PART_ANALOG, 0x03,
			 REG_PREDIV_MASK, REG_PREDIV(inno->pll.prediv));
	inno_update_bits(inno, REGISTER_PART_ANALOG, 0x03,
			 REG_FBDIV_HI_MASK, REG_FBDIV_HI(inno->pll.fbdiv >> 8));
	inno_update_bits(inno, REGISTER_PART_ANALOG, 0x04,
			 REG_FBDIV_LO_MASK, REG_FBDIV_LO(inno->pll.fbdiv));
	inno_update_bits(inno, REGISTER_PART_ANALOG, 0x08,
			 PLL_POST_DIV_ENABLE_MASK, PLL_POST_DIV_ENABLE);
	inno_update_bits(inno, REGISTER_PART_ANALOG, 0x0b,
			 CLOCK_LANE_VOD_RANGE_SET_MASK,
			 CLOCK_LANE_VOD_RANGE_SET(VOD_MAX_RANGE));
	inno_update_bits(inno, REGISTER_PART_ANALOG, 0x01,
			 REG_LDOPD_MASK | REG_PLLPD_MASK,
			 REG_LDOPD_POWER_ON | REG_PLLPD_POWER_ON);
}

static void inno_mipi_dphy_pll_disable(struct inno_mipi_dphy *inno)
{
	inno_update_bits(inno, REGISTER_PART_ANALOG, 0x01,
			 REG_LDOPD_MASK | REG_PLLPD_MASK,
			 REG_LDOPD_POWER_DOWN | REG_PLLPD_POWER_DOWN);
}

static void mipi_dphy_timing_get_default(struct mipi_dphy_timing *timing,
					 unsigned long period)
{
	/* Global Operation Timing Parameters */
	timing->clkmiss = 0;
	/*
	 * The D-PHY spec define the clk post min time is 60ns + 52UI and
	 * no define max time, so we set 200 + 52UI leave move margin.
	 */
	timing->clkpost = 200 + 52 * period / PSEC_PER_NSEC;
	timing->clkpre = 8 * period / PSEC_PER_NSEC;
	timing->clkprepare = 65;
	timing->clksettle = 95;
	timing->clktermen = 0;
	timing->clktrail = 80;
	timing->clkzero = 260;
	timing->dtermen = 0;
	timing->eot = 0;
	timing->hsexit = 120;
	timing->hsprepare = 65 + 4 * period / PSEC_PER_NSEC;
	timing->hszero = 145 + 6 * period / PSEC_PER_NSEC;
	timing->hssettle = 85 + 6 * period / PSEC_PER_NSEC;
	timing->hsskip = 40;
	timing->hstrail = max(8 * period / PSEC_PER_NSEC,
			      60 + 4 * period / PSEC_PER_NSEC);
	timing->init = 100000;
	timing->lpx = 60;
	timing->taget = 5 * timing->lpx;
	timing->tago = 4 * timing->lpx;
	timing->tasure = 2 * timing->lpx;
	timing->wakeup = 1000000;
}

static const struct inno_mipi_dphy_timing *
inno_mipi_dphy_get_timing(struct inno_mipi_dphy *inno)
{
	const struct inno_mipi_dphy_timing *timings;
	unsigned int num_timings;
	unsigned int lane_mbps = inno->lane_rate / USEC_PER_SEC;
	unsigned int i;

	timings = inno_mipi_dphy_timing_table;
	num_timings = ARRAY_SIZE(inno_mipi_dphy_timing_table);

	for (i = 0; i < num_timings; i++)
		if (lane_mbps <= timings[i].max_lane_mbps)
			break;

	if (i == num_timings)
		--i;

	return &timings[i];
}

static void inno_mipi_dphy_timing_init(struct inno_mipi_dphy *inno)
{
	struct mipi_dphy_timing gotp;
	const struct inno_mipi_dphy_timing *timing;
	unsigned long txbyteclk, txclkesc, ui, sys_clk;
	unsigned int esc_clk_div;
	u32 hs_exit, clk_post, clk_pre, wakeup, lpx, ta_go, ta_sure, ta_wait;
	u32 hs_prepare, hs_trail, hs_zero;
	unsigned int i;

	memset(&gotp, 0, sizeof(gotp));

	txbyteclk = inno->lane_rate / 8;
	sys_clk = clk_get_rate(inno->pclk);
	esc_clk_div = DIV_ROUND_UP(txbyteclk, 20000000);
	txclkesc = txbyteclk / esc_clk_div;
	ui = DIV_ROUND_CLOSEST_ULL(PSECS_PER_SEC, inno->lane_rate);

	dev_dbg(inno->dev, "txbyteclk=%ld, ui=%ld, sys_clk=%ld\n",
		txbyteclk, ui, sys_clk);

	mipi_dphy_timing_get_default(&gotp, ui);
	timing = inno_mipi_dphy_get_timing(inno);

	hs_exit = DIV_ROUND_UP(gotp.hsexit * txbyteclk, NSEC_PER_SEC);
	clk_post = DIV_ROUND_UP(gotp.clkpost * txbyteclk, NSEC_PER_SEC);
	clk_pre = DIV_ROUND_UP(gotp.clkpre * txbyteclk, NSEC_PER_SEC);

	wakeup = DIV_ROUND_UP(gotp.wakeup * sys_clk, NSEC_PER_SEC);
	if (wakeup > 0x3ff)
		wakeup = 0x3ff;

	ta_go = DIV_ROUND_UP(gotp.tago * txclkesc, NSEC_PER_SEC);
	ta_sure = DIV_ROUND_UP(gotp.tasure * txclkesc, NSEC_PER_SEC);
	ta_wait = DIV_ROUND_UP(gotp.taget * txclkesc, NSEC_PER_SEC);

	lpx = timing->lpx;
	hs_prepare = timing->hs_prepare;
	hs_trail = timing->hs_trail;

	for (i = REGISTER_PART_CLOCK_LANE; i <= REGISTER_PART_DATA3_LANE; i++) {
		if (i == REGISTER_PART_CLOCK_LANE)
			hs_zero = timing->clk_lane_hs_zero;
		else
			hs_zero = timing->data_lane_hs_zero;

		dev_dbg(inno->dev, "lpx=%x\n", lpx);
		dev_dbg(inno->dev,
			"hs_trail=%x, hs_exit=%x, hs_prepare=%x, hs_zero=%x\n",
			hs_trail, hs_exit, hs_prepare, hs_zero);
		dev_dbg(inno->dev, "clk_pre=%x, clk_post=%x\n",
			clk_pre, clk_post);
		dev_dbg(inno->dev, "ta_go=%x, ta_sure=%x, ta_wait=%x\n",
			ta_go, ta_sure, ta_wait);

		inno_update_bits(inno, i, 0x05, T_LPX_CNT_MASK,
				 T_LPX_CNT(lpx));
		inno_update_bits(inno, i, 0x06, T_HS_PREPARE_CNT_MASK,
				 T_HS_PREPARE_CNT(hs_prepare));
		inno_update_bits(inno, i, 0x06, T_HS_ZERO_CNT_HI_MASK,
				 T_HS_ZERO_CNT_HI(hs_zero >> 6));
		inno_update_bits(inno, i, 0x07, T_HS_ZERO_CNT_LO_MASK,
				 T_HS_ZERO_CNT_LO(hs_zero));
		inno_update_bits(inno, i, 0x08, T_HS_TRAIL_CNT_MASK,
				 T_HS_TRAIL_CNT(hs_trail));
		inno_update_bits(inno, i, 0x11, T_HS_EXIT_CNT_HI_MASK,
				 T_HS_EXIT_CNT_HI(hs_exit >> 5));
		inno_update_bits(inno, i, 0x09, T_HS_EXIT_CNT_LO_MASK,
				 T_HS_EXIT_CNT_LO(hs_exit));
		inno_update_bits(inno, i, 0x10, T_CLK_POST_HI_MASK,
				 T_CLK_POST_HI(clk_post >> 4));
		inno_update_bits(inno, i, 0x0a, T_CLK_POST_CNT_LO_MASK,
				 T_CLK_POST_CNT_LO(clk_post));
		inno_update_bits(inno, i, 0x0e, T_CLK_PRE_CNT_MASK,
				 T_CLK_PRE_CNT(clk_pre));
		inno_update_bits(inno, i, 0x0c, T_WAKEUP_CNT_HI_MASK,
				 T_WAKEUP_CNT_HI(wakeup >> 8));
		inno_update_bits(inno, i, 0x0d, T_WAKEUP_CNT_LO_MASK,
				 T_WAKEUP_CNT_LO(wakeup));
		inno_update_bits(inno, i, 0x10, T_TA_GO_CNT_MASK,
				 T_TA_GO_CNT(ta_go));
		inno_update_bits(inno, i, 0x11, T_TA_SURE_CNT_MASK,
				 T_TA_SURE_CNT(ta_sure));
		inno_update_bits(inno, i, 0x12, T_TA_WAIT_CNT_MASK,
				 T_TA_WAIT_CNT(ta_wait));
	}
}

static unsigned long inno_mipi_dphy_pll_round_rate(struct inno_mipi_dphy *inno,
						   unsigned long prate,
						   unsigned long rate,
						   u8 *prediv, u16 *fbdiv)
{
	const struct inno_mipi_dphy_timing *timings;
	unsigned int num_timings;
	unsigned long best_freq = 0;
	unsigned int fin, fout, max_fout;
	u8 min_prediv, max_prediv;
	u8 _prediv, best_prediv = 1;
	u16 _fbdiv, best_fbdiv = 1;
	u32 min_delta = UINT_MAX;

	timings = inno_mipi_dphy_timing_table;
	num_timings = ARRAY_SIZE(inno_mipi_dphy_timing_table);

	/*
	 * The PLL output frequency can be calculated using a simple formula:
	 * PLL_Output_Frequency = (FREF / PREDIV * FBDIV) / 2
	 * PLL_Output_Frequency: it is equal to DDR-Clock-Frequency * 2
	 */
	fin = prate / USEC_PER_SEC;
	fout = 2 * (rate / USEC_PER_SEC);
	max_fout = 2 * timings[num_timings - 1].max_lane_mbps;
	if (fout > max_fout)
		fout = max_fout;

	/* constraint: 5Mhz < Fref / prediv < 40MHz */
	min_prediv = DIV_ROUND_UP(fin, 40);
	max_prediv = fin / 5;

	for (_prediv = min_prediv; _prediv <= max_prediv; _prediv++) {
		u32 delta, tmp;

		_fbdiv = fout * _prediv / fin;
		/*
		 * The all possible settings of feedback divider are
		 * 12, 13, 14, 16, ~ 511
		 */
		if ((_fbdiv == 15) || (_fbdiv < 12) || (_fbdiv > 511))
			continue;

		tmp = _fbdiv * fin / _prediv;
		delta = abs(fout - tmp);
		if (delta < min_delta) {
			best_prediv = _prediv;
			best_fbdiv = _fbdiv;
			min_delta = delta;
			best_freq = tmp * USEC_PER_SEC;
		}
	}

	if (best_freq) {
		*prediv = best_prediv;
		*fbdiv = best_fbdiv;
	}

	return best_freq / 2;
}

static int inno_mipi_dphy_power_on(struct phy *phy)
{
	struct inno_mipi_dphy *inno = phy_get_drvdata(phy);

	clk_prepare_enable(inno->pclk);
	pm_runtime_get_sync(inno->dev);
	inno_mipi_dphy_bandgap_power_enable(inno);
	inno_mipi_dphy_power_work_enable(inno);
	inno_mipi_dphy_pll_enable(inno);
	inno_mipi_dphy_lane_enable(inno);
	inno_mipi_dphy_reset(inno);
	inno_mipi_dphy_timing_init(inno);
	udelay(1);

	return 0;
}

static int inno_mipi_dphy_power_off(struct phy *phy)
{
	struct inno_mipi_dphy *inno = phy_get_drvdata(phy);

	inno_mipi_dphy_lane_disable(inno);
	inno_mipi_dphy_pll_disable(inno);
	inno_mipi_dphy_power_work_disable(inno);
	inno_mipi_dphy_bandgap_power_disable(inno);
	pm_runtime_put(inno->dev);
	clk_disable_unprepare(inno->pclk);

	return 0;
}

static const struct phy_ops inno_mipi_dphy_ops = {
	.power_on  = inno_mipi_dphy_power_on,
	.power_off = inno_mipi_dphy_power_off,
	.owner	   = THIS_MODULE,
};

static long inno_mipi_dphy_pll_clk_round_rate(struct clk_hw *hw,
					      unsigned long rate,
					      unsigned long *prate)
{
	struct inno_mipi_dphy *inno = hw_to_inno(hw);
	unsigned long fin = *prate;
	unsigned long fout;
	u16 fbdiv = 1;
	u8 prediv = 1;

	fout = inno_mipi_dphy_pll_round_rate(inno, fin, rate,
					     &prediv, &fbdiv);

	dev_dbg(inno->dev, "%s: fin=%lu, req_rate=%lu\n",
		__func__, *prate, rate);
	dev_dbg(inno->dev, "%s: fout=%lu, prediv=%u, fbdiv=%u\n",
		__func__, fout, prediv, fbdiv);

	inno->pll.prediv = prediv;
	inno->pll.fbdiv = fbdiv;

	return fout;
}

static int inno_mipi_dphy_pll_clk_set_rate(struct clk_hw *hw,
					   unsigned long rate,
					   unsigned long parent_rate)
{
	struct inno_mipi_dphy *inno = hw_to_inno(hw);

	dev_dbg(inno->dev, "%s: rate: %lu Hz\n", __func__, rate);

	inno->lane_rate = rate;

	return 0;
}

static unsigned long
inno_mipi_dphy_pll_clk_recalc_rate(struct clk_hw *hw, unsigned long prate)
{
	struct inno_mipi_dphy *inno = hw_to_inno(hw);

	dev_dbg(inno->dev, "%s: rate: %lu Hz\n", __func__, inno->lane_rate);

	return inno->lane_rate;
}

static const struct clk_ops inno_mipi_dphy_pll_clk_ops = {
	.round_rate = inno_mipi_dphy_pll_clk_round_rate,
	.set_rate = inno_mipi_dphy_pll_clk_set_rate,
	.recalc_rate = inno_mipi_dphy_pll_clk_recalc_rate,
};

static int inno_mipi_dphy_pll_register(struct inno_mipi_dphy *inno)
{
	struct device *dev = inno->dev;
	struct device_node *np = dev->of_node;
	struct clk *clk;
	const char *parent_name;
	struct clk_init_data init = {};
	int ret;

	parent_name = __clk_get_name(inno->ref_clk);

	ret = of_property_read_string(np, "clock-output-names", &init.name);
	if (ret < 0) {
		dev_err(dev, "Missing clock-output-names property: %d\n", ret);
		return ret;
	}

	init.ops = &inno_mipi_dphy_pll_clk_ops;
	init.parent_names = (const char * const *)&parent_name;
	init.num_parents = 1;
	init.flags = 0;

	inno->pll.hw.init = &init;
	clk = devm_clk_register(dev, &inno->pll.hw);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err(dev, "failed to register PLL: %d\n", ret);
		return ret;
	}

	return of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

static void inno_mipi_dphy_pll_unregister(struct inno_mipi_dphy *inno)
{
	of_clk_del_provider(inno->dev->of_node);
}

static int inno_mipi_dphy_parse_dt(struct inno_mipi_dphy *inno)
{
	struct device *dev = inno->dev;

	if (of_property_read_u32(dev->of_node, "inno,lanes", &inno->lanes))
		inno->lanes = 4;

	return 0;
}

static const struct regmap_config inno_mipi_dphy_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x3ac,
};

static int inno_mipi_dphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct inno_mipi_dphy *inno;
	struct phy_provider *phy_provider;
	struct phy *phy;
	struct resource *res;
	void __iomem *regs;
	int ret;

	inno = devm_kzalloc(dev, sizeof(*inno), GFP_KERNEL);
	if (!inno)
		return -ENOMEM;

	inno->dev = dev;
	platform_set_drvdata(pdev, inno);

	ret = inno_mipi_dphy_parse_dt(inno);
	if (ret) {
		dev_err(dev, "failed to parse DT\n");
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	inno->regmap = devm_regmap_init_mmio(dev, regs,
					     &inno_mipi_dphy_regmap_config);
	if (IS_ERR(inno->regmap)) {
		ret = PTR_ERR(inno->regmap);
		dev_err(dev, "failed to init regmap: %d\n", ret);
		return ret;
	}

	inno->ref_clk = devm_clk_get(dev, "ref");
	if (IS_ERR(inno->ref_clk)) {
		dev_err(dev, "failed to get reference clock\n");
		return PTR_ERR(inno->ref_clk);
	}

	inno->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(inno->pclk)) {
		dev_err(dev, "failed to get pclk\n");
		return PTR_ERR(inno->pclk);
	}

	inno->rst = devm_reset_control_get(dev, "apb");
	if (IS_ERR(inno->rst)) {
		dev_err(dev, "failed to get system reset control\n");
		return PTR_ERR(inno->rst);
	}

	inno->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
						    "rockchip,grf");
	if (IS_ERR(inno->grf)) {
		dev_err(dev, "failed to get grf regmap\n");
		return PTR_ERR(inno->grf);
	}

	phy = devm_phy_create(dev, NULL, &inno_mipi_dphy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create MIPI D-PHY\n");
		return PTR_ERR(phy);
	}

	phy_set_drvdata(phy, inno);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(dev, "failed to register phy provider\n");
		return PTR_ERR(phy_provider);
	}

	ret = inno_mipi_dphy_pll_register(inno);
	if (ret)
		return ret;

	pm_runtime_enable(dev);

	return 0;
}

static int inno_mipi_dphy_remove(struct platform_device *pdev)
{
	struct inno_mipi_dphy *inno = platform_get_drvdata(pdev);

	inno_mipi_dphy_pll_unregister(inno);
	pm_runtime_disable(inno->dev);

	return 0;
}

static const struct of_device_id inno_mipi_dphy_of_match[] = {
	{ .compatible = "rockchip,rk1808-mipi-dphy", },
	{ .compatible = "rockchip,rv1126-mipi-dphy", },
	{}
};
MODULE_DEVICE_TABLE(of, inno_mipi_dphy_of_match);

static struct platform_driver inno_mipi_dphy_driver = {
	.driver = {
		.name = "inno-mipi-dphy",
		.of_match_table	= of_match_ptr(inno_mipi_dphy_of_match),
	},
	.probe	= inno_mipi_dphy_probe,
	.remove = inno_mipi_dphy_remove,
};

#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT
static int __init inno_mipi_dphy_driver_init(void)
{
	return platform_driver_register(&inno_mipi_dphy_driver);
}
fs_initcall(inno_mipi_dphy_driver_init);

static void __exit inno_mipi_dphy_driver_exit(void)
{
	platform_driver_unregister(&inno_mipi_dphy_driver);
}
module_exit(inno_mipi_dphy_driver_exit);
#else
module_platform_driver(inno_mipi_dphy_driver);
#endif

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Innosilicon MIPI D-PHY Driver");
MODULE_LICENSE("GPL v2");
