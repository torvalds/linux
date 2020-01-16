// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * Author: Zheng Yang <zhengyang@rock-chips.com>
 *         Heiko Stuebner <heiko@sntech.de>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/phy/phy.h>
#include <linux/slab.h>

#define UPDATE(x, h, l)		(((x) << (l)) & GENMASK((h), (l)))

/* REG: 0x00 */
#define RK3228_PRE_PLL_REFCLK_SEL_PCLK			BIT(0)
/* REG: 0x01 */
#define RK3228_BYPASS_RXSENSE_EN			BIT(2)
#define RK3228_BYPASS_PWRON_EN				BIT(1)
#define RK3228_BYPASS_PLLPD_EN				BIT(0)
/* REG: 0x02 */
#define RK3228_BYPASS_PDATA_EN				BIT(4)
#define RK3228_PDATAEN_DISABLE				BIT(0)
/* REG: 0x03 */
#define RK3228_BYPASS_AUTO_TERM_RES_CAL			BIT(7)
#define RK3228_AUTO_TERM_RES_CAL_SPEED_14_8(x)		UPDATE(x, 6, 0)
/* REG: 0x04 */
#define RK3228_AUTO_TERM_RES_CAL_SPEED_7_0(x)		UPDATE(x, 7, 0)
/* REG: 0xaa */
#define RK3228_POST_PLL_CTRL_MANUAL			BIT(0)
/* REG: 0xe0 */
#define RK3228_POST_PLL_POWER_DOWN			BIT(5)
#define RK3228_PRE_PLL_POWER_DOWN			BIT(4)
#define RK3228_RXSENSE_CLK_CH_ENABLE			BIT(3)
#define RK3228_RXSENSE_DATA_CH2_ENABLE			BIT(2)
#define RK3228_RXSENSE_DATA_CH1_ENABLE			BIT(1)
#define RK3228_RXSENSE_DATA_CH0_ENABLE			BIT(0)
/* REG: 0xe1 */
#define RK3228_BANDGAP_ENABLE				BIT(4)
#define RK3228_TMDS_DRIVER_ENABLE			GENMASK(3, 0)
/* REG: 0xe2 */
#define RK3228_PRE_PLL_FB_DIV_8_MASK			BIT(7)
#define RK3228_PRE_PLL_FB_DIV_8(x)			UPDATE((x) >> 8, 7, 7)
#define RK3228_PCLK_VCO_DIV_5_MASK			BIT(5)
#define RK3228_PCLK_VCO_DIV_5(x)			UPDATE(x, 5, 5)
#define RK3228_PRE_PLL_PRE_DIV_MASK			GENMASK(4, 0)
#define RK3228_PRE_PLL_PRE_DIV(x)			UPDATE(x, 4, 0)
/* REG: 0xe3 */
#define RK3228_PRE_PLL_FB_DIV_7_0(x)			UPDATE(x, 7, 0)
/* REG: 0xe4 */
#define RK3228_PRE_PLL_PCLK_DIV_B_MASK			GENMASK(6, 5)
#define RK3228_PRE_PLL_PCLK_DIV_B_SHIFT			5
#define RK3228_PRE_PLL_PCLK_DIV_B(x)			UPDATE(x, 6, 5)
#define RK3228_PRE_PLL_PCLK_DIV_A_MASK			GENMASK(4, 0)
#define RK3228_PRE_PLL_PCLK_DIV_A(x)			UPDATE(x, 4, 0)
/* REG: 0xe5 */
#define RK3228_PRE_PLL_PCLK_DIV_C_MASK			GENMASK(6, 5)
#define RK3228_PRE_PLL_PCLK_DIV_C(x)			UPDATE(x, 6, 5)
#define RK3228_PRE_PLL_PCLK_DIV_D_MASK			GENMASK(4, 0)
#define RK3228_PRE_PLL_PCLK_DIV_D(x)			UPDATE(x, 4, 0)
/* REG: 0xe6 */
#define RK3228_PRE_PLL_TMDSCLK_DIV_C_MASK		GENMASK(5, 4)
#define RK3228_PRE_PLL_TMDSCLK_DIV_C(x)			UPDATE(x, 5, 4)
#define RK3228_PRE_PLL_TMDSCLK_DIV_A_MASK		GENMASK(3, 2)
#define RK3228_PRE_PLL_TMDSCLK_DIV_A(x)			UPDATE(x, 3, 2)
#define RK3228_PRE_PLL_TMDSCLK_DIV_B_MASK		GENMASK(1, 0)
#define RK3228_PRE_PLL_TMDSCLK_DIV_B(x)			UPDATE(x, 1, 0)
/* REG: 0xe8 */
#define RK3228_PRE_PLL_LOCK_STATUS			BIT(0)
/* REG: 0xe9 */
#define RK3228_POST_PLL_POST_DIV_ENABLE			UPDATE(3, 7, 6)
#define RK3228_POST_PLL_PRE_DIV_MASK			GENMASK(4, 0)
#define RK3228_POST_PLL_PRE_DIV(x)			UPDATE(x, 4, 0)
/* REG: 0xea */
#define RK3228_POST_PLL_FB_DIV_7_0(x)			UPDATE(x, 7, 0)
/* REG: 0xeb */
#define RK3228_POST_PLL_FB_DIV_8_MASK			BIT(7)
#define RK3228_POST_PLL_FB_DIV_8(x)			UPDATE((x) >> 8, 7, 7)
#define RK3228_POST_PLL_POST_DIV_MASK			GENMASK(5, 4)
#define RK3228_POST_PLL_POST_DIV(x)			UPDATE(x, 5, 4)
#define RK3228_POST_PLL_LOCK_STATUS			BIT(0)
/* REG: 0xee */
#define RK3228_TMDS_CH_TA_ENABLE			GENMASK(7, 4)
/* REG: 0xef */
#define RK3228_TMDS_CLK_CH_TA(x)			UPDATE(x, 7, 6)
#define RK3228_TMDS_DATA_CH2_TA(x)			UPDATE(x, 5, 4)
#define RK3228_TMDS_DATA_CH1_TA(x)			UPDATE(x, 3, 2)
#define RK3228_TMDS_DATA_CH0_TA(x)			UPDATE(x, 1, 0)
/* REG: 0xf0 */
#define RK3228_TMDS_DATA_CH2_PRE_EMPHASIS_MASK		GENMASK(5, 4)
#define RK3228_TMDS_DATA_CH2_PRE_EMPHASIS(x)		UPDATE(x, 5, 4)
#define RK3228_TMDS_DATA_CH1_PRE_EMPHASIS_MASK		GENMASK(3, 2)
#define RK3228_TMDS_DATA_CH1_PRE_EMPHASIS(x)		UPDATE(x, 3, 2)
#define RK3228_TMDS_DATA_CH0_PRE_EMPHASIS_MASK		GENMASK(1, 0)
#define RK3228_TMDS_DATA_CH0_PRE_EMPHASIS(x)		UPDATE(x, 1, 0)
/* REG: 0xf1 */
#define RK3228_TMDS_CLK_CH_OUTPUT_SWING(x)		UPDATE(x, 7, 4)
#define RK3228_TMDS_DATA_CH2_OUTPUT_SWING(x)		UPDATE(x, 3, 0)
/* REG: 0xf2 */
#define RK3228_TMDS_DATA_CH1_OUTPUT_SWING(x)		UPDATE(x, 7, 4)
#define RK3228_TMDS_DATA_CH0_OUTPUT_SWING(x)		UPDATE(x, 3, 0)

/* REG: 0x01 */
#define RK3328_BYPASS_RXSENSE_EN			BIT(2)
#define RK3328_BYPASS_POWERON_EN			BIT(1)
#define RK3328_BYPASS_PLLPD_EN				BIT(0)
/* REG: 0x02 */
#define RK3328_INT_POL_HIGH				BIT(7)
#define RK3328_BYPASS_PDATA_EN				BIT(4)
#define RK3328_PDATA_EN					BIT(0)
/* REG:0x05 */
#define RK3328_INT_TMDS_CLK(x)				UPDATE(x, 7, 4)
#define RK3328_INT_TMDS_D2(x)				UPDATE(x, 3, 0)
/* REG:0x07 */
#define RK3328_INT_TMDS_D1(x)				UPDATE(x, 7, 4)
#define RK3328_INT_TMDS_D0(x)				UPDATE(x, 3, 0)
/* for all RK3328_INT_TMDS_*, ESD_DET as defined in 0xc8-0xcb */
#define RK3328_INT_AGND_LOW_PULSE_LOCKED		BIT(3)
#define RK3328_INT_RXSENSE_LOW_PULSE_LOCKED		BIT(2)
#define RK3328_INT_VSS_AGND_ESD_DET			BIT(1)
#define RK3328_INT_AGND_VSS_ESD_DET			BIT(0)
/* REG: 0xa0 */
#define RK3328_PCLK_VCO_DIV_5_MASK			BIT(1)
#define RK3328_PCLK_VCO_DIV_5(x)			UPDATE(x, 1, 1)
#define RK3328_PRE_PLL_POWER_DOWN			BIT(0)
/* REG: 0xa1 */
#define RK3328_PRE_PLL_PRE_DIV_MASK			GENMASK(5, 0)
#define RK3328_PRE_PLL_PRE_DIV(x)			UPDATE(x, 5, 0)
/* REG: 0xa2 */
/* unset means center spread */
#define RK3328_SPREAD_SPECTRUM_MOD_DOWN			BIT(7)
#define RK3328_SPREAD_SPECTRUM_MOD_DISABLE		BIT(6)
#define RK3328_PRE_PLL_FRAC_DIV_DISABLE			UPDATE(3, 5, 4)
#define RK3328_PRE_PLL_FB_DIV_11_8_MASK			GENMASK(3, 0)
#define RK3328_PRE_PLL_FB_DIV_11_8(x)			UPDATE((x) >> 8, 3, 0)
/* REG: 0xa3 */
#define RK3328_PRE_PLL_FB_DIV_7_0(x)			UPDATE(x, 7, 0)
/* REG: 0xa4*/
#define RK3328_PRE_PLL_TMDSCLK_DIV_C_MASK		GENMASK(1, 0)
#define RK3328_PRE_PLL_TMDSCLK_DIV_C(x)			UPDATE(x, 1, 0)
#define RK3328_PRE_PLL_TMDSCLK_DIV_B_MASK		GENMASK(3, 2)
#define RK3328_PRE_PLL_TMDSCLK_DIV_B(x)			UPDATE(x, 3, 2)
#define RK3328_PRE_PLL_TMDSCLK_DIV_A_MASK		GENMASK(5, 4)
#define RK3328_PRE_PLL_TMDSCLK_DIV_A(x)			UPDATE(x, 5, 4)
/* REG: 0xa5 */
#define RK3328_PRE_PLL_PCLK_DIV_B_SHIFT			5
#define RK3328_PRE_PLL_PCLK_DIV_B_MASK			GENMASK(6, 5)
#define RK3328_PRE_PLL_PCLK_DIV_B(x)			UPDATE(x, 6, 5)
#define RK3328_PRE_PLL_PCLK_DIV_A_MASK			GENMASK(4, 0)
#define RK3328_PRE_PLL_PCLK_DIV_A(x)			UPDATE(x, 4, 0)
/* REG: 0xa6 */
#define RK3328_PRE_PLL_PCLK_DIV_C_SHIFT			5
#define RK3328_PRE_PLL_PCLK_DIV_C_MASK			GENMASK(6, 5)
#define RK3328_PRE_PLL_PCLK_DIV_C(x)			UPDATE(x, 6, 5)
#define RK3328_PRE_PLL_PCLK_DIV_D_MASK			GENMASK(4, 0)
#define RK3328_PRE_PLL_PCLK_DIV_D(x)			UPDATE(x, 4, 0)
/* REG: 0xa9 */
#define RK3328_PRE_PLL_LOCK_STATUS			BIT(0)
/* REG: 0xaa */
#define RK3328_POST_PLL_POST_DIV_ENABLE			GENMASK(3, 2)
#define RK3328_POST_PLL_REFCLK_SEL_TMDS			BIT(1)
#define RK3328_POST_PLL_POWER_DOWN			BIT(0)
/* REG:0xab */
#define RK3328_POST_PLL_FB_DIV_8(x)			UPDATE((x) >> 8, 7, 7)
#define RK3328_POST_PLL_PRE_DIV(x)			UPDATE(x, 4, 0)
/* REG: 0xac */
#define RK3328_POST_PLL_FB_DIV_7_0(x)			UPDATE(x, 7, 0)
/* REG: 0xad */
#define RK3328_POST_PLL_POST_DIV_MASK			GENMASK(1, 0)
#define RK3328_POST_PLL_POST_DIV_2			0x0
#define RK3328_POST_PLL_POST_DIV_4			0x1
#define RK3328_POST_PLL_POST_DIV_8			0x3
/* REG: 0xaf */
#define RK3328_POST_PLL_LOCK_STATUS			BIT(0)
/* REG: 0xb0 */
#define RK3328_BANDGAP_ENABLE				BIT(2)
/* REG: 0xb2 */
#define RK3328_TMDS_CLK_DRIVER_EN			BIT(3)
#define RK3328_TMDS_D2_DRIVER_EN			BIT(2)
#define RK3328_TMDS_D1_DRIVER_EN			BIT(1)
#define RK3328_TMDS_D0_DRIVER_EN			BIT(0)
#define RK3328_TMDS_DRIVER_ENABLE		(RK3328_TMDS_CLK_DRIVER_EN | \
						RK3328_TMDS_D2_DRIVER_EN | \
						RK3328_TMDS_D1_DRIVER_EN | \
						RK3328_TMDS_D0_DRIVER_EN)
/* REG:0xc5 */
#define RK3328_BYPASS_TERM_RESISTOR_CALIB		BIT(7)
#define RK3328_TERM_RESISTOR_CALIB_SPEED_14_8(x)	UPDATE((x) >> 8, 6, 0)
/* REG:0xc6 */
#define RK3328_TERM_RESISTOR_CALIB_SPEED_7_0(x)		UPDATE(x, 7, 0)
/* REG:0xc7 */
#define RK3328_TERM_RESISTOR_50				UPDATE(0, 2, 1)
#define RK3328_TERM_RESISTOR_62_5			UPDATE(1, 2, 1)
#define RK3328_TERM_RESISTOR_75				UPDATE(2, 2, 1)
#define RK3328_TERM_RESISTOR_100			UPDATE(3, 2, 1)
/* REG 0xc8 - 0xcb */
#define RK3328_ESD_DETECT_MASK				GENMASK(7, 6)
#define RK3328_ESD_DETECT_340MV				(0x0 << 6)
#define RK3328_ESD_DETECT_280MV				(0x1 << 6)
#define RK3328_ESD_DETECT_260MV				(0x2 << 6)
#define RK3328_ESD_DETECT_240MV				(0x3 << 6)
/* resistors can be used in parallel */
#define RK3328_TMDS_TERM_RESIST_MASK			GENMASK(5, 0)
#define RK3328_TMDS_TERM_RESIST_75			BIT(5)
#define RK3328_TMDS_TERM_RESIST_150			BIT(4)
#define RK3328_TMDS_TERM_RESIST_300			BIT(3)
#define RK3328_TMDS_TERM_RESIST_600			BIT(2)
#define RK3328_TMDS_TERM_RESIST_1000			BIT(1)
#define RK3328_TMDS_TERM_RESIST_2000			BIT(0)
/* REG: 0xd1 */
#define RK3328_PRE_PLL_FRAC_DIV_23_16(x)		UPDATE((x) >> 16, 7, 0)
/* REG: 0xd2 */
#define RK3328_PRE_PLL_FRAC_DIV_15_8(x)			UPDATE((x) >> 8, 7, 0)
/* REG: 0xd3 */
#define RK3328_PRE_PLL_FRAC_DIV_7_0(x)			UPDATE(x, 7, 0)

struct inyes_hdmi_phy_drv_data;

struct inyes_hdmi_phy {
	struct device *dev;
	struct regmap *regmap;
	int irq;

	struct phy *phy;
	struct clk *sysclk;
	struct clk *refoclk;
	struct clk *refpclk;

	/* platform data */
	const struct inyes_hdmi_phy_drv_data *plat_data;
	int chip_version;

	/* clk provider */
	struct clk_hw hw;
	struct clk *phyclk;
	unsigned long pixclock;
};

struct pre_pll_config {
	unsigned long pixclock;
	unsigned long tmdsclock;
	u8 prediv;
	u16 fbdiv;
	u8 tmds_div_a;
	u8 tmds_div_b;
	u8 tmds_div_c;
	u8 pclk_div_a;
	u8 pclk_div_b;
	u8 pclk_div_c;
	u8 pclk_div_d;
	u8 vco_div_5_en;
	u32 fracdiv;
};

struct post_pll_config {
	unsigned long tmdsclock;
	u8 prediv;
	u16 fbdiv;
	u8 postdiv;
	u8 version;
};

struct phy_config {
	unsigned long	tmdsclock;
	u8		regs[14];
};

struct inyes_hdmi_phy_ops {
	int (*init)(struct inyes_hdmi_phy *inyes);
	int (*power_on)(struct inyes_hdmi_phy *inyes,
			const struct post_pll_config *cfg,
			const struct phy_config *phy_cfg);
	void (*power_off)(struct inyes_hdmi_phy *inyes);
};

struct inyes_hdmi_phy_drv_data {
	const struct inyes_hdmi_phy_ops	*ops;
	const struct clk_ops		*clk_ops;
	const struct phy_config		*phy_cfg_table;
};

static const struct pre_pll_config pre_pll_cfg_table[] = {
	{ 27000000,  27000000, 1,  90, 3, 2, 2, 10, 3, 3, 4, 0, 0},
	{ 27000000,  33750000, 1,  90, 1, 3, 3, 10, 3, 3, 4, 0, 0},
	{ 40000000,  40000000, 1,  80, 2, 2, 2, 12, 2, 2, 2, 0, 0},
	{ 59341000,  59341000, 1,  98, 3, 1, 2,  1, 3, 3, 4, 0, 0xE6AE6B},
	{ 59400000,  59400000, 1,  99, 3, 1, 1,  1, 3, 3, 4, 0, 0},
	{ 59341000,  74176250, 1,  98, 0, 3, 3,  1, 3, 3, 4, 0, 0xE6AE6B},
	{ 59400000,  74250000, 1,  99, 1, 2, 2,  1, 3, 3, 4, 0, 0},
	{ 74176000,  74176000, 1,  98, 1, 2, 2,  1, 2, 3, 4, 0, 0xE6AE6B},
	{ 74250000,  74250000, 1,  99, 1, 2, 2,  1, 2, 3, 4, 0, 0},
	{ 74176000,  92720000, 4, 494, 1, 2, 2,  1, 3, 3, 4, 0, 0x816817},
	{ 74250000,  92812500, 4, 495, 1, 2, 2,  1, 3, 3, 4, 0, 0},
	{148352000, 148352000, 1,  98, 1, 1, 1,  1, 2, 2, 2, 0, 0xE6AE6B},
	{148500000, 148500000, 1,  99, 1, 1, 1,  1, 2, 2, 2, 0, 0},
	{148352000, 185440000, 4, 494, 0, 2, 2,  1, 3, 2, 2, 0, 0x816817},
	{148500000, 185625000, 4, 495, 0, 2, 2,  1, 3, 2, 2, 0, 0},
	{296703000, 296703000, 1,  98, 0, 1, 1,  1, 0, 2, 2, 0, 0xE6AE6B},
	{297000000, 297000000, 1,  99, 0, 1, 1,  1, 0, 2, 2, 0, 0},
	{296703000, 370878750, 4, 494, 1, 2, 0,  1, 3, 1, 1, 0, 0x816817},
	{297000000, 371250000, 4, 495, 1, 2, 0,  1, 3, 1, 1, 0, 0},
	{593407000, 296703500, 1,  98, 0, 1, 1,  1, 0, 2, 1, 0, 0xE6AE6B},
	{594000000, 297000000, 1,  99, 0, 1, 1,  1, 0, 2, 1, 0, 0},
	{593407000, 370879375, 4, 494, 1, 2, 0,  1, 3, 1, 1, 1, 0x816817},
	{594000000, 371250000, 4, 495, 1, 2, 0,  1, 3, 1, 1, 1, 0},
	{593407000, 593407000, 1,  98, 0, 2, 0,  1, 0, 1, 1, 0, 0xE6AE6B},
	{594000000, 594000000, 1,  99, 0, 2, 0,  1, 0, 1, 1, 0, 0},
	{ /* sentinel */ }
};

static const struct post_pll_config post_pll_cfg_table[] = {
	{33750000,  1, 40, 8, 1},
	{33750000,  1, 80, 8, 2},
	{74250000,  1, 40, 8, 1},
	{74250000, 18, 80, 8, 2},
	{148500000, 2, 40, 4, 3},
	{297000000, 4, 40, 2, 3},
	{594000000, 8, 40, 1, 3},
	{ /* sentinel */ }
};

/* phy tuning values for an undocumented set of registers */
static const struct phy_config rk3228_phy_cfg[] = {
	{	165000000, {
			0xaa, 0x00, 0x44, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00,
		},
	}, {
		340000000, {
			0xaa, 0x15, 0x6a, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00,
		},
	}, {
		594000000, {
			0xaa, 0x15, 0x7a, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00,
		},
	}, { /* sentinel */ },
};

/* phy tuning values for an undocumented set of registers */
static const struct phy_config rk3328_phy_cfg[] = {
	{	165000000, {
			0x07, 0x0a, 0x0a, 0x0a, 0x00, 0x00, 0x08, 0x08, 0x08,
			0x00, 0xac, 0xcc, 0xcc, 0xcc,
		},
	}, {
		340000000, {
			0x0b, 0x0d, 0x0d, 0x0d, 0x07, 0x15, 0x08, 0x08, 0x08,
			0x3f, 0xac, 0xcc, 0xcd, 0xdd,
		},
	}, {
		594000000, {
			0x10, 0x1a, 0x1a, 0x1a, 0x07, 0x15, 0x08, 0x08, 0x08,
			0x00, 0xac, 0xcc, 0xcc, 0xcc,
		},
	}, { /* sentinel */ },
};

static inline struct inyes_hdmi_phy *to_inyes_hdmi_phy(struct clk_hw *hw)
{
	return container_of(hw, struct inyes_hdmi_phy, hw);
}

/*
 * The register description of the IP block does yest use any distinct names
 * but instead the databook simply numbers the registers in one-increments.
 * As the registers are obviously 32bit sized, the inyes_* functions
 * translate the databook register names to the actual registers addresses.
 */
static inline void inyes_write(struct inyes_hdmi_phy *inyes, u32 reg, u8 val)
{
	regmap_write(inyes->regmap, reg * 4, val);
}

static inline u8 inyes_read(struct inyes_hdmi_phy *inyes, u32 reg)
{
	u32 val;

	regmap_read(inyes->regmap, reg * 4, &val);

	return val;
}

static inline void inyes_update_bits(struct inyes_hdmi_phy *inyes, u8 reg,
				    u8 mask, u8 val)
{
	regmap_update_bits(inyes->regmap, reg * 4, mask, val);
}

#define inyes_poll(inyes, reg, val, cond, sleep_us, timeout_us) \
	regmap_read_poll_timeout((inyes)->regmap, (reg) * 4, val, cond, \
				 sleep_us, timeout_us)

static unsigned long inyes_hdmi_phy_get_tmdsclk(struct inyes_hdmi_phy *inyes,
					       unsigned long rate)
{
	int bus_width = phy_get_bus_width(inyes->phy);

	switch (bus_width) {
	case 4:
	case 5:
	case 6:
	case 10:
	case 12:
	case 16:
		return (u64)rate * bus_width / 8;
	default:
		return rate;
	}
}

static irqreturn_t inyes_hdmi_phy_rk3328_hardirq(int irq, void *dev_id)
{
	struct inyes_hdmi_phy *inyes = dev_id;
	int intr_stat1, intr_stat2, intr_stat3;

	intr_stat1 = inyes_read(inyes, 0x04);
	intr_stat2 = inyes_read(inyes, 0x06);
	intr_stat3 = inyes_read(inyes, 0x08);

	if (intr_stat1)
		inyes_write(inyes, 0x04, intr_stat1);
	if (intr_stat2)
		inyes_write(inyes, 0x06, intr_stat2);
	if (intr_stat3)
		inyes_write(inyes, 0x08, intr_stat3);

	if (intr_stat1 || intr_stat2 || intr_stat3)
		return IRQ_WAKE_THREAD;

	return IRQ_HANDLED;
}

static irqreturn_t inyes_hdmi_phy_rk3328_irq(int irq, void *dev_id)
{
	struct inyes_hdmi_phy *inyes = dev_id;

	inyes_update_bits(inyes, 0x02, RK3328_PDATA_EN, 0);
	usleep_range(10, 20);
	inyes_update_bits(inyes, 0x02, RK3328_PDATA_EN, RK3328_PDATA_EN);

	return IRQ_HANDLED;
}

static int inyes_hdmi_phy_power_on(struct phy *phy)
{
	struct inyes_hdmi_phy *inyes = phy_get_drvdata(phy);
	const struct post_pll_config *cfg = post_pll_cfg_table;
	const struct phy_config *phy_cfg = inyes->plat_data->phy_cfg_table;
	unsigned long tmdsclock = inyes_hdmi_phy_get_tmdsclk(inyes,
							    inyes->pixclock);
	int ret;

	if (!tmdsclock) {
		dev_err(inyes->dev, "TMDS clock is zero!\n");
		return -EINVAL;
	}

	if (!inyes->plat_data->ops->power_on)
		return -EINVAL;

	for (; cfg->tmdsclock != 0; cfg++)
		if (tmdsclock <= cfg->tmdsclock &&
		    cfg->version & inyes->chip_version)
			break;

	for (; phy_cfg->tmdsclock != 0; phy_cfg++)
		if (tmdsclock <= phy_cfg->tmdsclock)
			break;

	if (cfg->tmdsclock == 0 || phy_cfg->tmdsclock == 0)
		return -EINVAL;

	dev_dbg(inyes->dev, "Inyes HDMI PHY Power On\n");

	ret = clk_prepare_enable(inyes->phyclk);
	if (ret)
		return ret;

	ret = inyes->plat_data->ops->power_on(inyes, cfg, phy_cfg);
	if (ret) {
		clk_disable_unprepare(inyes->phyclk);
		return ret;
	}

	return 0;
}

static int inyes_hdmi_phy_power_off(struct phy *phy)
{
	struct inyes_hdmi_phy *inyes = phy_get_drvdata(phy);

	if (!inyes->plat_data->ops->power_off)
		return -EINVAL;

	inyes->plat_data->ops->power_off(inyes);

	clk_disable_unprepare(inyes->phyclk);

	dev_dbg(inyes->dev, "Inyes HDMI PHY Power Off\n");

	return 0;
}

static const struct phy_ops inyes_hdmi_phy_ops = {
	.owner = THIS_MODULE,
	.power_on = inyes_hdmi_phy_power_on,
	.power_off = inyes_hdmi_phy_power_off,
};

static const
struct pre_pll_config *inyes_hdmi_phy_get_pre_pll_cfg(struct inyes_hdmi_phy *inyes,
						     unsigned long rate)
{
	const struct pre_pll_config *cfg = pre_pll_cfg_table;
	unsigned long tmdsclock = inyes_hdmi_phy_get_tmdsclk(inyes, rate);

	for (; cfg->pixclock != 0; cfg++)
		if (cfg->pixclock == rate && cfg->tmdsclock == tmdsclock)
			break;

	if (cfg->pixclock == 0)
		return ERR_PTR(-EINVAL);

	return cfg;
}

static int inyes_hdmi_phy_rk3228_clk_is_prepared(struct clk_hw *hw)
{
	struct inyes_hdmi_phy *inyes = to_inyes_hdmi_phy(hw);
	u8 status;

	status = inyes_read(inyes, 0xe0) & RK3228_PRE_PLL_POWER_DOWN;
	return status ? 0 : 1;
}

static int inyes_hdmi_phy_rk3228_clk_prepare(struct clk_hw *hw)
{
	struct inyes_hdmi_phy *inyes = to_inyes_hdmi_phy(hw);

	inyes_update_bits(inyes, 0xe0, RK3228_PRE_PLL_POWER_DOWN, 0);
	return 0;
}

static void inyes_hdmi_phy_rk3228_clk_unprepare(struct clk_hw *hw)
{
	struct inyes_hdmi_phy *inyes = to_inyes_hdmi_phy(hw);

	inyes_update_bits(inyes, 0xe0, RK3228_PRE_PLL_POWER_DOWN,
			 RK3228_PRE_PLL_POWER_DOWN);
}

static
unsigned long inyes_hdmi_phy_rk3228_clk_recalc_rate(struct clk_hw *hw,
						   unsigned long parent_rate)
{
	struct inyes_hdmi_phy *inyes = to_inyes_hdmi_phy(hw);
	u8 nd, yes_a, yes_b, yes_d;
	u64 vco;
	u16 nf;

	nd = inyes_read(inyes, 0xe2) & RK3228_PRE_PLL_PRE_DIV_MASK;
	nf = (inyes_read(inyes, 0xe2) & RK3228_PRE_PLL_FB_DIV_8_MASK) << 1;
	nf |= inyes_read(inyes, 0xe3);
	vco = parent_rate * nf;

	if (inyes_read(inyes, 0xe2) & RK3228_PCLK_VCO_DIV_5_MASK) {
		do_div(vco, nd * 5);
	} else {
		yes_a = inyes_read(inyes, 0xe4) & RK3228_PRE_PLL_PCLK_DIV_A_MASK;
		if (!yes_a)
			yes_a = 1;
		yes_b = inyes_read(inyes, 0xe4) & RK3228_PRE_PLL_PCLK_DIV_B_MASK;
		yes_b >>= RK3228_PRE_PLL_PCLK_DIV_B_SHIFT;
		yes_b += 2;
		yes_d = inyes_read(inyes, 0xe5) & RK3228_PRE_PLL_PCLK_DIV_D_MASK;

		do_div(vco, (nd * (yes_a == 1 ? yes_b : yes_a) * yes_d * 2));
	}

	inyes->pixclock = vco;

	dev_dbg(inyes->dev, "%s rate %lu\n", __func__, inyes->pixclock);

	return vco;
}

static long inyes_hdmi_phy_rk3228_clk_round_rate(struct clk_hw *hw,
						unsigned long rate,
						unsigned long *parent_rate)
{
	const struct pre_pll_config *cfg = pre_pll_cfg_table;

	rate = (rate / 1000) * 1000;

	for (; cfg->pixclock != 0; cfg++)
		if (cfg->pixclock == rate && !cfg->fracdiv)
			break;

	if (cfg->pixclock == 0)
		return -EINVAL;

	return cfg->pixclock;
}

static int inyes_hdmi_phy_rk3228_clk_set_rate(struct clk_hw *hw,
					     unsigned long rate,
					     unsigned long parent_rate)
{
	struct inyes_hdmi_phy *inyes = to_inyes_hdmi_phy(hw);
	const struct pre_pll_config *cfg = pre_pll_cfg_table;
	unsigned long tmdsclock = inyes_hdmi_phy_get_tmdsclk(inyes, rate);
	u32 v;
	int ret;

	dev_dbg(inyes->dev, "%s rate %lu tmdsclk %lu\n",
		__func__, rate, tmdsclock);

	cfg = inyes_hdmi_phy_get_pre_pll_cfg(inyes, rate);
	if (IS_ERR(cfg))
		return PTR_ERR(cfg);

	/* Power down PRE-PLL */
	inyes_update_bits(inyes, 0xe0, RK3228_PRE_PLL_POWER_DOWN,
			 RK3228_PRE_PLL_POWER_DOWN);

	inyes_update_bits(inyes, 0xe2, RK3228_PRE_PLL_FB_DIV_8_MASK |
			 RK3228_PCLK_VCO_DIV_5_MASK |
			 RK3228_PRE_PLL_PRE_DIV_MASK,
			 RK3228_PRE_PLL_FB_DIV_8(cfg->fbdiv) |
			 RK3228_PCLK_VCO_DIV_5(cfg->vco_div_5_en) |
			 RK3228_PRE_PLL_PRE_DIV(cfg->prediv));
	inyes_write(inyes, 0xe3, RK3228_PRE_PLL_FB_DIV_7_0(cfg->fbdiv));
	inyes_update_bits(inyes, 0xe4, RK3228_PRE_PLL_PCLK_DIV_B_MASK |
			 RK3228_PRE_PLL_PCLK_DIV_A_MASK,
			 RK3228_PRE_PLL_PCLK_DIV_B(cfg->pclk_div_b) |
			 RK3228_PRE_PLL_PCLK_DIV_A(cfg->pclk_div_a));
	inyes_update_bits(inyes, 0xe5, RK3228_PRE_PLL_PCLK_DIV_C_MASK |
			 RK3228_PRE_PLL_PCLK_DIV_D_MASK,
			 RK3228_PRE_PLL_PCLK_DIV_C(cfg->pclk_div_c) |
			 RK3228_PRE_PLL_PCLK_DIV_D(cfg->pclk_div_d));
	inyes_update_bits(inyes, 0xe6, RK3228_PRE_PLL_TMDSCLK_DIV_C_MASK |
			 RK3228_PRE_PLL_TMDSCLK_DIV_A_MASK |
			 RK3228_PRE_PLL_TMDSCLK_DIV_B_MASK,
			 RK3228_PRE_PLL_TMDSCLK_DIV_C(cfg->tmds_div_c) |
			 RK3228_PRE_PLL_TMDSCLK_DIV_A(cfg->tmds_div_a) |
			 RK3228_PRE_PLL_TMDSCLK_DIV_B(cfg->tmds_div_b));

	/* Power up PRE-PLL */
	inyes_update_bits(inyes, 0xe0, RK3228_PRE_PLL_POWER_DOWN, 0);

	/* Wait for Pre-PLL lock */
	ret = inyes_poll(inyes, 0xe8, v, v & RK3228_PRE_PLL_LOCK_STATUS,
			100, 100000);
	if (ret) {
		dev_err(inyes->dev, "Pre-PLL locking failed\n");
		return ret;
	}

	inyes->pixclock = rate;

	return 0;
}

static const struct clk_ops inyes_hdmi_phy_rk3228_clk_ops = {
	.prepare = inyes_hdmi_phy_rk3228_clk_prepare,
	.unprepare = inyes_hdmi_phy_rk3228_clk_unprepare,
	.is_prepared = inyes_hdmi_phy_rk3228_clk_is_prepared,
	.recalc_rate = inyes_hdmi_phy_rk3228_clk_recalc_rate,
	.round_rate = inyes_hdmi_phy_rk3228_clk_round_rate,
	.set_rate = inyes_hdmi_phy_rk3228_clk_set_rate,
};

static int inyes_hdmi_phy_rk3328_clk_is_prepared(struct clk_hw *hw)
{
	struct inyes_hdmi_phy *inyes = to_inyes_hdmi_phy(hw);
	u8 status;

	status = inyes_read(inyes, 0xa0) & RK3328_PRE_PLL_POWER_DOWN;
	return status ? 0 : 1;
}

static int inyes_hdmi_phy_rk3328_clk_prepare(struct clk_hw *hw)
{
	struct inyes_hdmi_phy *inyes = to_inyes_hdmi_phy(hw);

	inyes_update_bits(inyes, 0xa0, RK3328_PRE_PLL_POWER_DOWN, 0);
	return 0;
}

static void inyes_hdmi_phy_rk3328_clk_unprepare(struct clk_hw *hw)
{
	struct inyes_hdmi_phy *inyes = to_inyes_hdmi_phy(hw);

	inyes_update_bits(inyes, 0xa0, RK3328_PRE_PLL_POWER_DOWN,
			 RK3328_PRE_PLL_POWER_DOWN);
}

static
unsigned long inyes_hdmi_phy_rk3328_clk_recalc_rate(struct clk_hw *hw,
						   unsigned long parent_rate)
{
	struct inyes_hdmi_phy *inyes = to_inyes_hdmi_phy(hw);
	unsigned long frac;
	u8 nd, yes_a, yes_b, yes_c, yes_d;
	u64 vco;
	u16 nf;

	nd = inyes_read(inyes, 0xa1) & RK3328_PRE_PLL_PRE_DIV_MASK;
	nf = ((inyes_read(inyes, 0xa2) & RK3328_PRE_PLL_FB_DIV_11_8_MASK) << 8);
	nf |= inyes_read(inyes, 0xa3);
	vco = parent_rate * nf;

	if (!(inyes_read(inyes, 0xa2) & RK3328_PRE_PLL_FRAC_DIV_DISABLE)) {
		frac = inyes_read(inyes, 0xd3) |
		       (inyes_read(inyes, 0xd2) << 8) |
		       (inyes_read(inyes, 0xd1) << 16);
		vco += DIV_ROUND_CLOSEST(parent_rate * frac, (1 << 24));
	}

	if (inyes_read(inyes, 0xa0) & RK3328_PCLK_VCO_DIV_5_MASK) {
		do_div(vco, nd * 5);
	} else {
		yes_a = inyes_read(inyes, 0xa5) & RK3328_PRE_PLL_PCLK_DIV_A_MASK;
		yes_b = inyes_read(inyes, 0xa5) & RK3328_PRE_PLL_PCLK_DIV_B_MASK;
		yes_b >>= RK3328_PRE_PLL_PCLK_DIV_B_SHIFT;
		yes_b += 2;
		yes_c = inyes_read(inyes, 0xa6) & RK3328_PRE_PLL_PCLK_DIV_C_MASK;
		yes_c >>= RK3328_PRE_PLL_PCLK_DIV_C_SHIFT;
		yes_c = 1 << yes_c;
		yes_d = inyes_read(inyes, 0xa6) & RK3328_PRE_PLL_PCLK_DIV_D_MASK;

		do_div(vco, (nd * (yes_a == 1 ? yes_b : yes_a) * yes_d * 2));
	}

	inyes->pixclock = vco;
	dev_dbg(inyes->dev, "%s rate %lu\n", __func__, inyes->pixclock);

	return vco;
}

static long inyes_hdmi_phy_rk3328_clk_round_rate(struct clk_hw *hw,
						unsigned long rate,
						unsigned long *parent_rate)
{
	const struct pre_pll_config *cfg = pre_pll_cfg_table;

	rate = (rate / 1000) * 1000;

	for (; cfg->pixclock != 0; cfg++)
		if (cfg->pixclock == rate)
			break;

	if (cfg->pixclock == 0)
		return -EINVAL;

	return cfg->pixclock;
}

static int inyes_hdmi_phy_rk3328_clk_set_rate(struct clk_hw *hw,
					     unsigned long rate,
					     unsigned long parent_rate)
{
	struct inyes_hdmi_phy *inyes = to_inyes_hdmi_phy(hw);
	const struct pre_pll_config *cfg = pre_pll_cfg_table;
	unsigned long tmdsclock = inyes_hdmi_phy_get_tmdsclk(inyes, rate);
	u32 val;
	int ret;

	dev_dbg(inyes->dev, "%s rate %lu tmdsclk %lu\n",
		__func__, rate, tmdsclock);

	cfg = inyes_hdmi_phy_get_pre_pll_cfg(inyes, rate);
	if (IS_ERR(cfg))
		return PTR_ERR(cfg);

	inyes_update_bits(inyes, 0xa0, RK3328_PRE_PLL_POWER_DOWN,
			 RK3328_PRE_PLL_POWER_DOWN);

	/* Configure pre-pll */
	inyes_update_bits(inyes, 0xa0, RK3228_PCLK_VCO_DIV_5_MASK,
			 RK3228_PCLK_VCO_DIV_5(cfg->vco_div_5_en));
	inyes_write(inyes, 0xa1, RK3328_PRE_PLL_PRE_DIV(cfg->prediv));

	val = RK3328_SPREAD_SPECTRUM_MOD_DISABLE;
	if (!cfg->fracdiv)
		val |= RK3328_PRE_PLL_FRAC_DIV_DISABLE;
	inyes_write(inyes, 0xa2, RK3328_PRE_PLL_FB_DIV_11_8(cfg->fbdiv) | val);
	inyes_write(inyes, 0xa3, RK3328_PRE_PLL_FB_DIV_7_0(cfg->fbdiv));
	inyes_write(inyes, 0xa5, RK3328_PRE_PLL_PCLK_DIV_A(cfg->pclk_div_a) |
		   RK3328_PRE_PLL_PCLK_DIV_B(cfg->pclk_div_b));
	inyes_write(inyes, 0xa6, RK3328_PRE_PLL_PCLK_DIV_C(cfg->pclk_div_c) |
		   RK3328_PRE_PLL_PCLK_DIV_D(cfg->pclk_div_d));
	inyes_write(inyes, 0xa4, RK3328_PRE_PLL_TMDSCLK_DIV_C(cfg->tmds_div_c) |
		   RK3328_PRE_PLL_TMDSCLK_DIV_A(cfg->tmds_div_a) |
		   RK3328_PRE_PLL_TMDSCLK_DIV_B(cfg->tmds_div_b));
	inyes_write(inyes, 0xd3, RK3328_PRE_PLL_FRAC_DIV_7_0(cfg->fracdiv));
	inyes_write(inyes, 0xd2, RK3328_PRE_PLL_FRAC_DIV_15_8(cfg->fracdiv));
	inyes_write(inyes, 0xd1, RK3328_PRE_PLL_FRAC_DIV_23_16(cfg->fracdiv));

	inyes_update_bits(inyes, 0xa0, RK3328_PRE_PLL_POWER_DOWN, 0);

	/* Wait for Pre-PLL lock */
	ret = inyes_poll(inyes, 0xa9, val, val & RK3328_PRE_PLL_LOCK_STATUS,
			1000, 10000);
	if (ret) {
		dev_err(inyes->dev, "Pre-PLL locking failed\n");
		return ret;
	}

	inyes->pixclock = rate;

	return 0;
}

static const struct clk_ops inyes_hdmi_phy_rk3328_clk_ops = {
	.prepare = inyes_hdmi_phy_rk3328_clk_prepare,
	.unprepare = inyes_hdmi_phy_rk3328_clk_unprepare,
	.is_prepared = inyes_hdmi_phy_rk3328_clk_is_prepared,
	.recalc_rate = inyes_hdmi_phy_rk3328_clk_recalc_rate,
	.round_rate = inyes_hdmi_phy_rk3328_clk_round_rate,
	.set_rate = inyes_hdmi_phy_rk3328_clk_set_rate,
};

static int inyes_hdmi_phy_clk_register(struct inyes_hdmi_phy *inyes)
{
	struct device *dev = inyes->dev;
	struct device_yesde *np = dev->of_yesde;
	struct clk_init_data init;
	const char *parent_name;
	int ret;

	parent_name = __clk_get_name(inyes->refoclk);

	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = 0;
	init.name = "pin_hd20_pclk";
	init.ops = inyes->plat_data->clk_ops;

	/* optional override of the clock name */
	of_property_read_string(np, "clock-output-names", &init.name);

	inyes->hw.init = &init;

	inyes->phyclk = devm_clk_register(dev, &inyes->hw);
	if (IS_ERR(inyes->phyclk)) {
		ret = PTR_ERR(inyes->phyclk);
		dev_err(dev, "failed to register clock: %d\n", ret);
		return ret;
	}

	ret = of_clk_add_provider(np, of_clk_src_simple_get, inyes->phyclk);
	if (ret) {
		dev_err(dev, "failed to register clock provider: %d\n", ret);
		return ret;
	}

	return 0;
}

static int inyes_hdmi_phy_rk3228_init(struct inyes_hdmi_phy *inyes)
{
	/*
	 * Use phy internal register control
	 * rxsense/poweron/pllpd/pdataen signal.
	 */
	inyes_write(inyes, 0x01, RK3228_BYPASS_RXSENSE_EN |
		   RK3228_BYPASS_PWRON_EN |
		   RK3228_BYPASS_PLLPD_EN);
	inyes_update_bits(inyes, 0x02, RK3228_BYPASS_PDATA_EN,
			 RK3228_BYPASS_PDATA_EN);

	/* manual power down post-PLL */
	inyes_update_bits(inyes, 0xaa, RK3228_POST_PLL_CTRL_MANUAL,
			 RK3228_POST_PLL_CTRL_MANUAL);

	inyes->chip_version = 1;

	return 0;
}

static int
inyes_hdmi_phy_rk3228_power_on(struct inyes_hdmi_phy *inyes,
			      const struct post_pll_config *cfg,
			      const struct phy_config *phy_cfg)
{
	int ret;
	u32 v;

	inyes_update_bits(inyes, 0x02, RK3228_PDATAEN_DISABLE,
			 RK3228_PDATAEN_DISABLE);
	inyes_update_bits(inyes, 0xe0, RK3228_PRE_PLL_POWER_DOWN |
			 RK3228_POST_PLL_POWER_DOWN,
			 RK3228_PRE_PLL_POWER_DOWN |
			 RK3228_POST_PLL_POWER_DOWN);

	/* Post-PLL update */
	inyes_update_bits(inyes, 0xe9, RK3228_POST_PLL_PRE_DIV_MASK,
			 RK3228_POST_PLL_PRE_DIV(cfg->prediv));
	inyes_update_bits(inyes, 0xeb, RK3228_POST_PLL_FB_DIV_8_MASK,
			 RK3228_POST_PLL_FB_DIV_8(cfg->fbdiv));
	inyes_write(inyes, 0xea, RK3228_POST_PLL_FB_DIV_7_0(cfg->fbdiv));

	if (cfg->postdiv == 1) {
		inyes_update_bits(inyes, 0xe9, RK3228_POST_PLL_POST_DIV_ENABLE,
				 0);
	} else {
		int div = cfg->postdiv / 2 - 1;

		inyes_update_bits(inyes, 0xe9, RK3228_POST_PLL_POST_DIV_ENABLE,
				 RK3228_POST_PLL_POST_DIV_ENABLE);
		inyes_update_bits(inyes, 0xeb, RK3228_POST_PLL_POST_DIV_MASK,
				 RK3228_POST_PLL_POST_DIV(div));
	}

	for (v = 0; v < 4; v++)
		inyes_write(inyes, 0xef + v, phy_cfg->regs[v]);

	inyes_update_bits(inyes, 0xe0, RK3228_PRE_PLL_POWER_DOWN |
			 RK3228_POST_PLL_POWER_DOWN, 0);
	inyes_update_bits(inyes, 0xe1, RK3228_BANDGAP_ENABLE,
			 RK3228_BANDGAP_ENABLE);
	inyes_update_bits(inyes, 0xe1, RK3228_TMDS_DRIVER_ENABLE,
			 RK3228_TMDS_DRIVER_ENABLE);

	/* Wait for post PLL lock */
	ret = inyes_poll(inyes, 0xeb, v, v & RK3228_POST_PLL_LOCK_STATUS,
			100, 100000);
	if (ret) {
		dev_err(inyes->dev, "Post-PLL locking failed\n");
		return ret;
	}

	if (cfg->tmdsclock > 340000000)
		msleep(100);

	inyes_update_bits(inyes, 0x02, RK3228_PDATAEN_DISABLE, 0);
	return 0;
}

static void inyes_hdmi_phy_rk3228_power_off(struct inyes_hdmi_phy *inyes)
{
	inyes_update_bits(inyes, 0xe1, RK3228_TMDS_DRIVER_ENABLE, 0);
	inyes_update_bits(inyes, 0xe1, RK3228_BANDGAP_ENABLE, 0);
	inyes_update_bits(inyes, 0xe0, RK3228_POST_PLL_POWER_DOWN,
			 RK3228_POST_PLL_POWER_DOWN);
}

static const struct inyes_hdmi_phy_ops rk3228_hdmi_phy_ops = {
	.init = inyes_hdmi_phy_rk3228_init,
	.power_on = inyes_hdmi_phy_rk3228_power_on,
	.power_off = inyes_hdmi_phy_rk3228_power_off,
};

static int inyes_hdmi_phy_rk3328_init(struct inyes_hdmi_phy *inyes)
{
	struct nvmem_cell *cell;
	unsigned char *efuse_buf;
	size_t len;

	/*
	 * Use phy internal register control
	 * rxsense/poweron/pllpd/pdataen signal.
	 */
	inyes_write(inyes, 0x01, RK3328_BYPASS_RXSENSE_EN |
		   RK3328_BYPASS_POWERON_EN |
		   RK3328_BYPASS_PLLPD_EN);
	inyes_write(inyes, 0x02, RK3328_INT_POL_HIGH | RK3328_BYPASS_PDATA_EN |
		   RK3328_PDATA_EN);

	/* Disable phy irq */
	inyes_write(inyes, 0x05, 0);
	inyes_write(inyes, 0x07, 0);

	/* try to read the chip-version */
	inyes->chip_version = 1;
	cell = nvmem_cell_get(inyes->dev, "cpu-version");
	if (IS_ERR(cell)) {
		if (PTR_ERR(cell) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		return 0;
	}

	efuse_buf = nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);

	if (IS_ERR(efuse_buf))
		return 0;
	if (len == 1)
		inyes->chip_version = efuse_buf[0] + 1;
	kfree(efuse_buf);

	return 0;
}

static int
inyes_hdmi_phy_rk3328_power_on(struct inyes_hdmi_phy *inyes,
			      const struct post_pll_config *cfg,
			      const struct phy_config *phy_cfg)
{
	int ret;
	u32 v;

	inyes_update_bits(inyes, 0x02, RK3328_PDATA_EN, 0);
	inyes_update_bits(inyes, 0xaa, RK3328_POST_PLL_POWER_DOWN,
			 RK3328_POST_PLL_POWER_DOWN);

	inyes_write(inyes, 0xac, RK3328_POST_PLL_FB_DIV_7_0(cfg->fbdiv));
	if (cfg->postdiv == 1) {
		inyes_write(inyes, 0xaa, RK3328_POST_PLL_REFCLK_SEL_TMDS);
		inyes_write(inyes, 0xab, RK3328_POST_PLL_FB_DIV_8(cfg->fbdiv) |
			   RK3328_POST_PLL_PRE_DIV(cfg->prediv));
	} else {
		v = (cfg->postdiv / 2) - 1;
		v &= RK3328_POST_PLL_POST_DIV_MASK;
		inyes_write(inyes, 0xad, v);
		inyes_write(inyes, 0xab, RK3328_POST_PLL_FB_DIV_8(cfg->fbdiv) |
			   RK3328_POST_PLL_PRE_DIV(cfg->prediv));
		inyes_write(inyes, 0xaa, RK3328_POST_PLL_POST_DIV_ENABLE |
			   RK3328_POST_PLL_REFCLK_SEL_TMDS);
	}

	for (v = 0; v < 14; v++)
		inyes_write(inyes, 0xb5 + v, phy_cfg->regs[v]);

	/* set ESD detection threshold for TMDS CLK, D2, D1 and D0 */
	for (v = 0; v < 4; v++)
		inyes_update_bits(inyes, 0xc8 + v, RK3328_ESD_DETECT_MASK,
				 RK3328_ESD_DETECT_340MV);

	if (phy_cfg->tmdsclock > 340000000) {
		/* Set termination resistor to 100ohm */
		v = clk_get_rate(inyes->sysclk) / 100000;
		inyes_write(inyes, 0xc5, RK3328_TERM_RESISTOR_CALIB_SPEED_14_8(v)
			   | RK3328_BYPASS_TERM_RESISTOR_CALIB);
		inyes_write(inyes, 0xc6, RK3328_TERM_RESISTOR_CALIB_SPEED_7_0(v));
		inyes_write(inyes, 0xc7, RK3328_TERM_RESISTOR_100);
		inyes_update_bits(inyes, 0xc5,
				 RK3328_BYPASS_TERM_RESISTOR_CALIB, 0);
	} else {
		inyes_write(inyes, 0xc5, RK3328_BYPASS_TERM_RESISTOR_CALIB);

		/* clk termination resistor is 50ohm (parallel resistors) */
		if (phy_cfg->tmdsclock > 165000000)
			inyes_update_bits(inyes, 0xc8,
					 RK3328_TMDS_TERM_RESIST_MASK,
					 RK3328_TMDS_TERM_RESIST_75 |
					 RK3328_TMDS_TERM_RESIST_150);

		/* data termination resistor for D2, D1 and D0 is 150ohm */
		for (v = 0; v < 3; v++)
			inyes_update_bits(inyes, 0xc9 + v,
					 RK3328_TMDS_TERM_RESIST_MASK,
					 RK3328_TMDS_TERM_RESIST_150);
	}

	inyes_update_bits(inyes, 0xaa, RK3328_POST_PLL_POWER_DOWN, 0);
	inyes_update_bits(inyes, 0xb0, RK3328_BANDGAP_ENABLE,
			 RK3328_BANDGAP_ENABLE);
	inyes_update_bits(inyes, 0xb2, RK3328_TMDS_DRIVER_ENABLE,
			 RK3328_TMDS_DRIVER_ENABLE);

	/* Wait for post PLL lock */
	ret = inyes_poll(inyes, 0xaf, v, v & RK3328_POST_PLL_LOCK_STATUS,
			1000, 10000);
	if (ret) {
		dev_err(inyes->dev, "Post-PLL locking failed\n");
		return ret;
	}

	if (phy_cfg->tmdsclock > 340000000)
		msleep(100);

	inyes_update_bits(inyes, 0x02, RK3328_PDATA_EN, RK3328_PDATA_EN);

	/* Enable PHY IRQ */
	inyes_write(inyes, 0x05, RK3328_INT_TMDS_CLK(RK3328_INT_VSS_AGND_ESD_DET)
		   | RK3328_INT_TMDS_D2(RK3328_INT_VSS_AGND_ESD_DET));
	inyes_write(inyes, 0x07, RK3328_INT_TMDS_D1(RK3328_INT_VSS_AGND_ESD_DET)
		   | RK3328_INT_TMDS_D0(RK3328_INT_VSS_AGND_ESD_DET));
	return 0;
}

static void inyes_hdmi_phy_rk3328_power_off(struct inyes_hdmi_phy *inyes)
{
	inyes_update_bits(inyes, 0xb2, RK3328_TMDS_DRIVER_ENABLE, 0);
	inyes_update_bits(inyes, 0xb0, RK3328_BANDGAP_ENABLE, 0);
	inyes_update_bits(inyes, 0xaa, RK3328_POST_PLL_POWER_DOWN,
			 RK3328_POST_PLL_POWER_DOWN);

	/* Disable PHY IRQ */
	inyes_write(inyes, 0x05, 0);
	inyes_write(inyes, 0x07, 0);
}

static const struct inyes_hdmi_phy_ops rk3328_hdmi_phy_ops = {
	.init = inyes_hdmi_phy_rk3328_init,
	.power_on = inyes_hdmi_phy_rk3328_power_on,
	.power_off = inyes_hdmi_phy_rk3328_power_off,
};

static const struct inyes_hdmi_phy_drv_data rk3228_hdmi_phy_drv_data = {
	.ops = &rk3228_hdmi_phy_ops,
	.clk_ops = &inyes_hdmi_phy_rk3228_clk_ops,
	.phy_cfg_table = rk3228_phy_cfg,
};

static const struct inyes_hdmi_phy_drv_data rk3328_hdmi_phy_drv_data = {
	.ops = &rk3328_hdmi_phy_ops,
	.clk_ops = &inyes_hdmi_phy_rk3328_clk_ops,
	.phy_cfg_table = rk3328_phy_cfg,
};

static const struct regmap_config inyes_hdmi_phy_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x400,
};

static void inyes_hdmi_phy_action(void *data)
{
	struct inyes_hdmi_phy *inyes = data;

	clk_disable_unprepare(inyes->refpclk);
	clk_disable_unprepare(inyes->sysclk);
}

static int inyes_hdmi_phy_probe(struct platform_device *pdev)
{
	struct inyes_hdmi_phy *inyes;
	struct phy_provider *phy_provider;
	struct resource *res;
	void __iomem *regs;
	int ret;

	inyes = devm_kzalloc(&pdev->dev, sizeof(*inyes), GFP_KERNEL);
	if (!inyes)
		return -ENOMEM;

	inyes->dev = &pdev->dev;

	inyes->plat_data = of_device_get_match_data(inyes->dev);
	if (!inyes->plat_data || !inyes->plat_data->ops)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(inyes->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	inyes->sysclk = devm_clk_get(inyes->dev, "sysclk");
	if (IS_ERR(inyes->sysclk)) {
		ret = PTR_ERR(inyes->sysclk);
		dev_err(inyes->dev, "failed to get sysclk: %d\n", ret);
		return ret;
	}

	inyes->refpclk = devm_clk_get(inyes->dev, "refpclk");
	if (IS_ERR(inyes->refpclk)) {
		ret = PTR_ERR(inyes->refpclk);
		dev_err(inyes->dev, "failed to get ref clock: %d\n", ret);
		return ret;
	}

	inyes->refoclk = devm_clk_get(inyes->dev, "refoclk");
	if (IS_ERR(inyes->refoclk)) {
		ret = PTR_ERR(inyes->refoclk);
		dev_err(inyes->dev, "failed to get oscillator-ref clock: %d\n",
			ret);
		return ret;
	}

	ret = clk_prepare_enable(inyes->sysclk);
	if (ret) {
		dev_err(inyes->dev, "Canyest enable inyes phy sysclk: %d\n", ret);
		return ret;
	}

	/*
	 * Refpclk needs to be on, on at least the rk3328 for still
	 * unkyeswn reasons.
	 */
	ret = clk_prepare_enable(inyes->refpclk);
	if (ret) {
		dev_err(inyes->dev, "failed to enable refpclk\n");
		clk_disable_unprepare(inyes->sysclk);
		return ret;
	}

	ret = devm_add_action_or_reset(inyes->dev, inyes_hdmi_phy_action,
				       inyes);
	if (ret)
		return ret;

	inyes->regmap = devm_regmap_init_mmio(inyes->dev, regs,
					     &inyes_hdmi_phy_regmap_config);
	if (IS_ERR(inyes->regmap))
		return PTR_ERR(inyes->regmap);

	/* only the newer rk3328 hdmiphy has an interrupt */
	inyes->irq = platform_get_irq(pdev, 0);
	if (inyes->irq > 0) {
		ret = devm_request_threaded_irq(inyes->dev, inyes->irq,
						inyes_hdmi_phy_rk3328_hardirq,
						inyes_hdmi_phy_rk3328_irq,
						IRQF_SHARED,
						dev_name(inyes->dev), inyes);
		if (ret)
			return ret;
	}

	inyes->phy = devm_phy_create(inyes->dev, NULL, &inyes_hdmi_phy_ops);
	if (IS_ERR(inyes->phy)) {
		dev_err(inyes->dev, "failed to create HDMI PHY\n");
		return PTR_ERR(inyes->phy);
	}

	phy_set_drvdata(inyes->phy, inyes);
	phy_set_bus_width(inyes->phy, 8);

	if (inyes->plat_data->ops->init) {
		ret = inyes->plat_data->ops->init(inyes);
		if (ret)
			return ret;
	}

	ret = inyes_hdmi_phy_clk_register(inyes);
	if (ret)
		return ret;

	phy_provider = devm_of_phy_provider_register(inyes->dev,
						     of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
}

static int inyes_hdmi_phy_remove(struct platform_device *pdev)
{
	of_clk_del_provider(pdev->dev.of_yesde);

	return 0;
}

static const struct of_device_id inyes_hdmi_phy_of_match[] = {
	{
		.compatible = "rockchip,rk3228-hdmi-phy",
		.data = &rk3228_hdmi_phy_drv_data
	}, {
		.compatible = "rockchip,rk3328-hdmi-phy",
		.data = &rk3328_hdmi_phy_drv_data
	}, { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, inyes_hdmi_phy_of_match);

static struct platform_driver inyes_hdmi_phy_driver = {
	.probe  = inyes_hdmi_phy_probe,
	.remove = inyes_hdmi_phy_remove,
	.driver = {
		.name = "inyes-hdmi-phy",
		.of_match_table = inyes_hdmi_phy_of_match,
	},
};
module_platform_driver(inyes_hdmi_phy_driver);

MODULE_AUTHOR("Zheng Yang <zhengyang@rock-chips.com>");
MODULE_DESCRIPTION("Inyessilion HDMI 2.0 Transmitter PHY Driver");
MODULE_LICENSE("GPL v2");
