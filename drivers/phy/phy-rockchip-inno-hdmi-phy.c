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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/phy/phy.h>

#define INNO_HDMI_PHY_TIMEOUT_LOOP_COUNT	1000

#define UPDATE(x, h, l)		(((x) << (l)) & GENMASK((h), (l)))

/* REG: 0x00 */
#define PRE_PLL_REFCLK_SEL_MASK			BIT(0)
#define PRE_PLL_REFCLK_SEL_PCLK			BIT(0)
#define PRE_PLL_REFCLK_SEL_OSCCLK		0
/* REG: 0x01 */
#define BYPASS_RXSENSE_EN_MASK			BIT(2)
#define BYPASS_RXSENSE_EN			BIT(2)
#define BYPASS_PWRON_EN_MASK			BIT(1)
#define BYPASS_PWRON_EN				BIT(1)
#define BYPASS_PLLPD_EN_MASK			BIT(0)
#define BYPASS_PLLPD_EN				BIT(0)
/* REG: 0x02 */
#define BYPASS_PDATA_EN_MASK			BIT(4)
#define BYPASS_PDATA_EN				BIT(4)
#define PDATAEN_MASK				BIT(0)
#define PDATAEN_DISABLE				BIT(0)
#define PDATAEN_ENABLE				0
/* REG: 0x03 */
#define BYPASS_AUTO_TERM_RES_CAL		BIT(7)
#define AUDO_TERM_RES_CAL_SPEED_14_8(x)		UPDATE(x, 6, 0)
/* REG: 0x04 */
#define AUDO_TERM_RES_CAL_SPEED_7_0(x)		UPDATE(x, 7, 0)
/* REG: 0xaa */
#define POST_PLL_CTRL_MASK			BIT(0)
#define POST_PLL_CTRL_MANUAL			BIT(0)
/* REG: 0xe0 */
#define POST_PLL_POWER_MASK			BIT(5)
#define POST_PLL_POWER_DOWN			BIT(5)
#define POST_PLL_POWER_UP			0
#define PRE_PLL_POWER_MASK			BIT(4)
#define PRE_PLL_POWER_DOWN			BIT(4)
#define PRE_PLL_POWER_UP			0
#define RXSENSE_CLK_CH_MASK			BIT(3)
#define RXSENSE_CLK_CH_ENABLE			BIT(3)
#define RXSENSE_DATA_CH2_MASK			BIT(2)
#define RXSENSE_DATA_CH2_ENABLE			BIT(2)
#define RXSENSE_DATA_CH1_MASK			BIT(1)
#define RXSENSE_DATA_CH1_ENABLE			BIT(1)
#define RXSENSE_DATA_CH0_MASK			BIT(0)
#define RXSENSE_DATA_CH0_ENABLE			BIT(0)
/* REG: 0xe1 */
#define BANDGAP_MASK				BIT(4)
#define BANDGAP_ENABLE				BIT(4)
#define BANDGAP_DISABLE				0
#define TMDS_DRIVER_MASK			GENMASK(3, 0)
#define TMDS_DRIVER_ENABLE			UPDATE(0xf, 3, 0)
#define TMDS_DRIVER_DISABLE			0
/* REG: 0xe2 */
#define PRE_PLL_FB_DIV_8_MASK			BIT(7)
#define PRE_PLL_FB_DIV_8_SHIFT			7
#define PRE_PLL_FB_DIV_8(x)			UPDATE(x, 7, 7)
#define PCLK_VCO_DIV_5_MASK			BIT(5)
#define PCLK_VCO_DIV_5_SHIFT			5
#define PCLK_VCO_DIV_5(x)			UPDATE(x, 5, 5)
#define PRE_PLL_PRE_DIV_MASK			GENMASK(4, 0)
#define PRE_PLL_PRE_DIV(x)			UPDATE(x, 4, 0)
/* REG: 0xe3 */
#define PRE_PLL_FB_DIV_7_0(x)			UPDATE(x, 7, 0)
/* REG: 0xe4 */
#define PRE_PLL_PCLK_DIV_B_MASK			GENMASK(6, 5)
#define PRE_PLL_PCLK_DIV_B_SHIFT		5
#define PRE_PLL_PCLK_DIV_B(x)			UPDATE(x, 6, 5)
#define PRE_PLL_PCLK_DIV_A_MASK			GENMASK(4, 0)
#define PRE_PLL_PCLK_DIV_A_SHIFT		0
#define PRE_PLL_PCLK_DIV_A(x)			UPDATE(x, 4, 0)
/* REG: 0xe5 */
#define PRE_PLL_PCLK_DIV_C_MASK			GENMASK(6, 5)
#define PRE_PLL_PCLK_DIV_C_SHIFT		5
#define PRE_PLL_PCLK_DIV_C(x)			UPDATE(x, 6, 5)
#define PRE_PLL_PCLK_DIV_D_MASK			GENMASK(4, 0)
#define PRE_PLL_PCLK_DIV_D_SHIFT		0
#define PRE_PLL_PCLK_DIV_D(x)			UPDATE(x, 4, 0)
/* REG: 0xe6 */
#define PRE_PLL_TMDSCLK_DIV_C_MASK		GENMASK(5, 4)
#define PRE_PLL_TMDSCLK_DIV_C(x)		UPDATE(x, 5, 4)
#define PRE_PLL_TMDSCLK_DIV_A_MASK		GENMASK(3, 2)
#define PRE_PLL_TMDSCLK_DIV_A(x)		UPDATE(x, 3, 2)
#define PRE_PLL_TMDSCLK_DIV_B_MASK		GENMASK(1, 0)
#define PRE_PLL_TMDSCLK_DIV_B(x)		UPDATE(x, 1, 0)
/* REG: 0xe8 */
#define PRE_PLL_LOCK_STATUS			BIT(0)
/* REG: 0xe9 */
#define POST_PLL_POST_DIV_EN_MASK		GENMASK(7, 6)
#define POST_PLL_POST_DIV_ENABLE		UPDATE(3, 7, 6)
#define POST_PLL_POST_DIV_DISABLE		0
#define POST_PLL_PRE_DIV_MASK			GENMASK(4, 0)
#define POST_PLL_PRE_DIV(x)			UPDATE(x, 4, 0)
/* REG: 0xea */
#define POST_PLL_FB_DIV_7_0(x)			UPDATE(x, 7, 0)
/* REG: 0xeb */
#define POST_PLL_FB_DIV_8_MASK			BIT(7)
#define POST_PLL_FB_DIV_8(x)			UPDATE(x, 7, 7)
#define POST_PLL_POST_DIV_MASK			GENMASK(5, 4)
#define POST_PLL_POST_DIV(x)			UPDATE(x, 5, 4)
#define POST_PLL_LOCK_STATUS			BIT(0)
/* REG: 0xee */
#define TMDS_CH_TA_MASK				GENMASK(7, 4)
#define TMDS_CH_TA_ENABLE			UPDATE(0xf, 7, 4)
#define TMDS_CH_TA_DISABLE			0
/* REG: 0xef */
#define TMDS_CLK_CH_TA(x)			UPDATE(x, 7, 6)
#define TMDS_DATA_CH2_TA(x)			UPDATE(x, 5, 4)
#define TMDS_DATA_CH1_TA(x)			UPDATE(x, 3, 2)
#define TMDS_DATA_CH0_TA(x)			UPDATE(x, 1, 0)
/* REG: 0xf0 */
#define TMDS_DATA_CH2_PRE_EMPHASIS_MASK		GENMASK(5, 4)
#define TMDS_DATA_CH2_PRE_EMPHASIS(x)		UPDATE(x, 5, 4)
#define TMDS_DATA_CH1_PRE_EMPHASIS_MASK		GENMASK(3, 2)
#define TMDS_DATA_CH1_PRE_EMPHASIS(x)		UPDATE(x, 3, 2)
#define TMDS_DATA_CH0_PRE_EMPHASIS_MASK		GENMASK(1, 0)
#define TMDS_DATA_CH0_PRE_EMPHASIS(x)		UPDATE(x, 1, 0)
/* REG: 0xf1 */
#define TMDS_CLK_CH_OUTPUT_SWING(x)		UPDATE(x, 7, 4)
#define TMDS_DATA_CH2_OUTPUT_SWING(x)		UPDATE(x, 3, 0)
/* REG: 0xf2 */
#define TMDS_DATA_CH1_OUTPUT_SWING(x)		UPDATE(x, 7, 4)
#define TMDS_DATA_CH0_OUTPUT_SWING(x)		UPDATE(x, 3, 0)

struct inno_hdmi_phy {
	struct device *dev;
	struct regmap *regmap;

	/* clk provider */
	struct clk_hw hw;
	struct clk *pclk;
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
};

static const struct pre_pll_config pre_pll_cfg_table[] = {
	{ 27000000,  27000000, 1,  90, 3, 2, 2, 10, 3, 3, 4, 0},
	{ 27000000,  33750000, 1,  90, 1, 3, 3, 10, 3, 3, 4, 0},
	{ 40000000,  40000000, 1,  80, 2, 2, 2, 12, 2, 2, 2, 0},
	{ 59400000,  59400000, 1,  99, 3, 1, 1,  1, 3, 3, 4, 0},
	{ 59400000,  74250000, 1,  99, 1, 2, 2,  1, 3, 3, 4, 0},
	{ 74250000,  74250000, 1,  99, 1, 2, 2,  1, 2, 3, 4, 0},
	{ 74250000,  92812500, 4, 495, 1, 2, 2,  1, 3, 3, 4, 0},
	{148500000, 148500000, 1,  99, 1, 1, 1,  1, 2, 2, 2, 0},
	{148500000, 185625000, 4, 495, 0, 2, 2,  1, 3, 2, 2, 0},
	{297000000, 297000000, 1,  99, 0, 1, 1,  1, 0, 2, 2, 0},
	{297000000, 371250000, 4, 495, 1, 2, 0,  1, 3, 1, 1, 0},
	{594000000, 297000000, 1,  99, 0, 1, 1,  1, 0, 2, 1, 0},
	{594000000, 371250000, 4, 495, 1, 2, 0,  1, 3, 1, 1, 1},
	{594000000, 594000000, 1,  99, 0, 2, 0,  1, 0, 1, 1, 0},
	{     ~0UL,	    0, 0,   0, 0, 0, 0,  0, 0, 0, 0, 0}
};

struct post_pll_config {
	unsigned long tmdsclock;
	u8 prediv;
	u16 fbdiv;
	u8 postdiv;
};

static const struct post_pll_config post_pll_cfg_table[] = {
	{ 33750000, 1, 40, 8},
	{ 74250000, 1, 40, 8},
	{148500000, 2, 40, 4},
	{297000000, 4, 40, 2},
	{371250000, 8, 40, 1},
	{     ~0UL, 0,  0, 0}
};

struct phy_config {
	unsigned long tmdsclock;
	u8 pre_emphasis;
	u8 clk_level;
	u8 data0_level;
	u8 data1_level;
	u8 data2_level;
};

static const struct phy_config phy_cfg_table[] = {
	{165000000, 0, 4,  4,  4,  4},
	{225000000, 0, 6,  6,  6,  6},
	{340000000, 1, 6, 10, 10, 10},
	{594000000, 1, 7, 10, 10, 10},
	{     ~0UL, 0, 0,  0,  0,  0}
};

static inline struct inno_hdmi_phy *to_inno_hdmi_phy(struct clk_hw *hw)
{
	return container_of(hw, struct inno_hdmi_phy, hw);
}

static inline void inno_write(struct inno_hdmi_phy *inno, u32 reg, u8 val)
{
	regmap_write(inno->regmap, reg * 4, val);
}

static inline u8 inno_read(struct inno_hdmi_phy *inno, u32 reg)
{
	u32 val;

	regmap_read(inno->regmap, reg * 4, &val);

	return val;
}

static inline void inno_update_bits(struct inno_hdmi_phy *inno, u8 reg,
				    u8 mask, u8 val)
{
	regmap_update_bits(inno->regmap, reg * 4, mask, val);
}

static void inno_hdmi_phy_post_pll_update(struct inno_hdmi_phy *inno,
					  const struct post_pll_config *cfg)
{
	u8 m, v;

	m = POST_PLL_PRE_DIV_MASK;
	v = POST_PLL_PRE_DIV(cfg->prediv);
	inno_update_bits(inno, 0xe9, m, v);

	m = POST_PLL_FB_DIV_8_MASK;
	v = POST_PLL_FB_DIV_8(cfg->fbdiv >> 8);
	inno_update_bits(inno, 0xeb, m, v);
	inno_write(inno, 0xea, POST_PLL_FB_DIV_7_0(cfg->fbdiv));

	if (cfg->postdiv == 1) {
		/* Disable Post-PLL post divider */
		m = POST_PLL_POST_DIV_EN_MASK;
		v = POST_PLL_POST_DIV_DISABLE;
		inno_update_bits(inno, 0xe9, m, v);
	} else {
		/* Enable Post-PLL post divider */
		m = POST_PLL_POST_DIV_EN_MASK;
		v = POST_PLL_POST_DIV_ENABLE;
		inno_update_bits(inno, 0xe9, m, v);

		m = POST_PLL_POST_DIV_MASK;
		v = POST_PLL_POST_DIV(cfg->postdiv / 2 - 1);
		inno_update_bits(inno, 0xeb, m, v);
	}
}

static int inno_hdmi_phy_power_on(struct phy *phy)
{
	struct inno_hdmi_phy *inno = phy_get_drvdata(phy);
	const struct post_pll_config *cfg = post_pll_cfg_table;
	const struct phy_config *phy_cfg = phy_cfg_table;
	int pll_tries;
	u32 m, v;

	for (; cfg->tmdsclock != ~0UL; cfg++)
		if (inno->pixclock <= cfg->tmdsclock)
			break;

	for (; phy_cfg->tmdsclock != ~0UL; phy_cfg++)
		if (inno->pixclock <= cfg->tmdsclock)
			break;

	if (cfg->tmdsclock == ~0UL || phy_cfg->tmdsclock == ~0UL)
		return -EINVAL;

	/* pdata_en disable */
	inno_update_bits(inno, 0x02, PDATAEN_MASK, PDATAEN_DISABLE);

	/* Power down Post-PLL */
	inno_update_bits(inno, 0xe0, POST_PLL_POWER_MASK, POST_PLL_POWER_DOWN);

	/* Pre-emphasis level control */
	m = TMDS_DATA_CH2_PRE_EMPHASIS_MASK | TMDS_DATA_CH1_PRE_EMPHASIS_MASK |
	    TMDS_DATA_CH0_PRE_EMPHASIS_MASK;
	v = TMDS_DATA_CH2_PRE_EMPHASIS(phy_cfg->pre_emphasis) |
	    TMDS_DATA_CH1_PRE_EMPHASIS(phy_cfg->pre_emphasis) |
	    TMDS_DATA_CH0_PRE_EMPHASIS(phy_cfg->pre_emphasis);
	inno_update_bits(inno, 0xf0, m, v);

	/* Output swing control */
	v = TMDS_CLK_CH_OUTPUT_SWING(phy_cfg->clk_level) |
	    TMDS_DATA_CH2_OUTPUT_SWING(phy_cfg->data2_level);
	inno_write(inno, 0xf1, v);
	v = TMDS_DATA_CH1_OUTPUT_SWING(phy_cfg->data1_level) |
	    TMDS_DATA_CH0_OUTPUT_SWING(phy_cfg->data0_level);
	inno_write(inno, 0xf2, v);

	/* Post-PLL update */
	inno_hdmi_phy_post_pll_update(inno, cfg);

	/* Power up Post-PLL */
	inno_update_bits(inno, 0xe0, POST_PLL_POWER_MASK, POST_PLL_POWER_UP);

	/* BandGap enable */
	inno_update_bits(inno, 0xe1, BANDGAP_MASK, BANDGAP_ENABLE);

	/* TMDS driver enable */
	inno_update_bits(inno, 0xe1, TMDS_DRIVER_MASK, TMDS_DRIVER_ENABLE);

	/* Wait for post PLL lock */
	pll_tries = 0;
	while (!(inno_read(inno, 0xeb) & POST_PLL_LOCK_STATUS)) {
		if (pll_tries == INNO_HDMI_PHY_TIMEOUT_LOOP_COUNT) {
			dev_err(inno->dev, "Post-PLL unlock\n");
			return -ETIMEDOUT;
		}

		pll_tries++;
		usleep_range(100, 110);
	}

	if (cfg->tmdsclock > 340000000)
		msleep(100);

	/* pdata_en enable */
	inno_update_bits(inno, 0x02, PDATAEN_MASK, PDATAEN_ENABLE);

	dev_dbg(inno->dev, "Inno HDMI PHY Power On\n");

	return 0;
}

static int inno_hdmi_phy_power_off(struct phy *phy)
{
	struct inno_hdmi_phy *inno = phy_get_drvdata(phy);

	/* TMDS driver Disable */
	inno_update_bits(inno, 0xe1, TMDS_DRIVER_MASK, TMDS_DRIVER_DISABLE);

	/* BandGap Disable */
	inno_update_bits(inno, 0xe1, BANDGAP_MASK, BANDGAP_DISABLE);

	/* Post-PLL power down */
	inno_update_bits(inno, 0xe0, POST_PLL_POWER_MASK, POST_PLL_POWER_DOWN);

	dev_dbg(inno->dev, "Inno HDMI PHY Power Off\n");

	return 0;
}

static const struct phy_ops inno_hdmi_phy_ops = {
	.owner	   = THIS_MODULE,
	.power_on  = inno_hdmi_phy_power_on,
	.power_off = inno_hdmi_phy_power_off,
};

static int inno_hdmi_phy_clk_is_prepared(struct clk_hw *hw)
{
	struct inno_hdmi_phy *inno = to_inno_hdmi_phy(hw);
	u8 status;

	status = inno_read(inno, 0xe0) & PRE_PLL_POWER_MASK;

	return status ? 0 : 1;
}

static int inno_hdmi_phy_clk_prepare(struct clk_hw *hw)
{
	struct inno_hdmi_phy *inno = to_inno_hdmi_phy(hw);

	inno_update_bits(inno, 0xe0, PRE_PLL_POWER_MASK, PRE_PLL_POWER_UP);

	return 0;
}

static void inno_hdmi_phy_clk_unprepare(struct clk_hw *hw)
{
	struct inno_hdmi_phy *inno = to_inno_hdmi_phy(hw);

	inno_update_bits(inno, 0xe0, PRE_PLL_POWER_MASK, PRE_PLL_POWER_DOWN);
}

static unsigned long inno_hdmi_phy_clk_recalc_rate(struct clk_hw *hw,
						   unsigned long parent_rate)
{
	struct inno_hdmi_phy *inno = to_inno_hdmi_phy(hw);

	return inno->pixclock;
}

static long inno_hdmi_phy_clk_round_rate(struct clk_hw *hw, unsigned long rate,
					 unsigned long *parent_rate)
{
	struct inno_hdmi_phy *inno = to_inno_hdmi_phy(hw);
	const struct pre_pll_config *cfg = pre_pll_cfg_table;

	for (; cfg->pixclock != ~0UL; cfg++)
		if (cfg->pixclock == rate)
			break;

	/* XXX: Limit pixel clock under 300MHz */
	if (cfg->pixclock > 300000000)
		return -EINVAL;

	dev_dbg(inno->dev, "%s: rate=%ld\n", __func__, cfg->pixclock);

	return cfg->pixclock;
}

static void inno_hdmi_phy_pre_pll_update(struct inno_hdmi_phy *inno,
					 const struct pre_pll_config *cfg)
{
	u32 m, v;

	m = PRE_PLL_FB_DIV_8_MASK | PCLK_VCO_DIV_5_MASK | PRE_PLL_PRE_DIV_MASK;
	v = PRE_PLL_FB_DIV_8(cfg->fbdiv >> 8) |
	    PCLK_VCO_DIV_5(cfg->vco_div_5_en) | PRE_PLL_PRE_DIV(cfg->prediv);
	inno_update_bits(inno, 0xe2, m, v);

	inno_write(inno, 0xe3, PRE_PLL_FB_DIV_7_0(cfg->fbdiv));

	m = PRE_PLL_PCLK_DIV_B_MASK | PRE_PLL_PCLK_DIV_A_MASK;
	v = PRE_PLL_PCLK_DIV_B(cfg->pclk_div_b) |
	    PRE_PLL_PCLK_DIV_A(cfg->pclk_div_a);
	inno_update_bits(inno, 0xe4, m, v);

	m = PRE_PLL_PCLK_DIV_C_MASK | PRE_PLL_PCLK_DIV_D_MASK;
	v = PRE_PLL_PCLK_DIV_C(cfg->pclk_div_c) |
	    PRE_PLL_PCLK_DIV_D(cfg->pclk_div_d);
	inno_update_bits(inno, 0xe5, m, v);

	m = PRE_PLL_TMDSCLK_DIV_C_MASK | PRE_PLL_TMDSCLK_DIV_A_MASK |
	    PRE_PLL_TMDSCLK_DIV_B_MASK;
	v = PRE_PLL_TMDSCLK_DIV_C(cfg->tmds_div_c) |
	    PRE_PLL_TMDSCLK_DIV_A(cfg->tmds_div_a) |
	    PRE_PLL_TMDSCLK_DIV_B(cfg->tmds_div_b);
	inno_update_bits(inno, 0xe6, m, v);
}

static int inno_hdmi_phy_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long parent_rate)
{
	struct inno_hdmi_phy *inno = to_inno_hdmi_phy(hw);
	const struct pre_pll_config *cfg = pre_pll_cfg_table;
	int pll_tries;

	for (; cfg->pixclock != ~0UL; cfg++)
		if (cfg->pixclock == rate && cfg->tmdsclock == rate)
			break;

	if (cfg->pixclock == ~0UL) {
		dev_err(inno->dev, "unsupported rate %lu\n", rate);
		return -EINVAL;
	}

	/* Power down PRE-PLL */
	inno_update_bits(inno, 0xe0, PRE_PLL_POWER_MASK, PRE_PLL_POWER_DOWN);

	inno_hdmi_phy_pre_pll_update(inno, cfg);

	/* Power up PRE-PLL */
	inno_update_bits(inno, 0xe0, PRE_PLL_POWER_MASK, PRE_PLL_POWER_UP);

	/* Wait for Pre-PLL lock */
	pll_tries = 0;
	while (!(inno_read(inno, 0xe8) & PRE_PLL_LOCK_STATUS)) {
		if (pll_tries == INNO_HDMI_PHY_TIMEOUT_LOOP_COUNT) {
			dev_err(inno->dev, "Pre-PLL unlock\n");
			return -ETIMEDOUT;
		}

		pll_tries++;
		usleep_range(100, 110);
	}

	inno->pixclock = rate;

	return 0;
}

static const struct clk_ops inno_hdmi_phy_clk_ops = {
	.prepare = inno_hdmi_phy_clk_prepare,
	.unprepare = inno_hdmi_phy_clk_unprepare,
	.is_prepared = inno_hdmi_phy_clk_is_prepared,
	.recalc_rate = inno_hdmi_phy_clk_recalc_rate,
	.round_rate = inno_hdmi_phy_clk_round_rate,
	.set_rate = inno_hdmi_phy_clk_set_rate,
};

static int inno_hdmi_phy_clk_register(struct inno_hdmi_phy *inno)
{
	struct device *dev = inno->dev;
	struct device_node *np = dev->of_node;
	struct clk_init_data init;
	struct clk *refclk;
	const char *parent_name;
	int ret;

	refclk = devm_clk_get(dev, "refclk");
	if (IS_ERR(refclk)) {
		dev_err(dev, "failed to get ref clock\n");
		return PTR_ERR(refclk);
	}

	parent_name = __clk_get_name(refclk);

	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = 0;
	init.name = "pin_hd20_pclk";
	init.ops = &inno_hdmi_phy_clk_ops;

	/* optional override of the clock name */
	of_property_read_string(np, "clock-output-names", &init.name);

	inno->hw.init = &init;

	inno->pclk = devm_clk_register(dev, &inno->hw);
	if (IS_ERR(inno->pclk)) {
		ret = PTR_ERR(inno->pclk);
		dev_err(dev, "failed to register clock: %d\n", ret);
		return ret;
	}

	ret = of_clk_add_provider(np, of_clk_src_simple_get, inno->pclk);
	if (ret) {
		dev_err(dev, "failed to register OF clock provider: %d\n", ret);
		return ret;
	}

	return 0;
}

static void inno_hdmi_phy_init(struct inno_hdmi_phy *inno)
{
	u32 m, v;

	/* Use internal control rxsense/poweron/pllpd/pdataen signal. */
	m = BYPASS_RXSENSE_EN_MASK | BYPASS_PWRON_EN_MASK |
	    BYPASS_PLLPD_EN_MASK;
	v = BYPASS_RXSENSE_EN | BYPASS_PWRON_EN | BYPASS_PLLPD_EN;
	inno_update_bits(inno, 0x01, m, v);
	inno_update_bits(inno, 0x02, BYPASS_PDATA_EN_MASK, BYPASS_PDATA_EN);

	/* manual power down post-PLL */
	inno_update_bits(inno, 0xaa, POST_PLL_CTRL_MASK, POST_PLL_CTRL_MANUAL);
}

static const struct regmap_config inno_hdmi_phy_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x400,
};

static int inno_hdmi_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct inno_hdmi_phy *inno;
	struct phy *phy;
	struct phy_provider *phy_provider;
	struct resource *res;
	void __iomem *regs;
	int ret;

	inno = devm_kzalloc(dev, sizeof(*inno), GFP_KERNEL);
	if (!inno)
		return -ENOMEM;

	inno->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	inno->regmap = devm_regmap_init_mmio_clk(dev, "sysclk", regs,
						 &inno_hdmi_phy_regmap_config);
	if (IS_ERR(inno->regmap)) {
		ret = PTR_ERR(inno->regmap);
		dev_err(dev, "failed to init regmap: %d\n", ret);
		return ret;
	}

	phy = devm_phy_create(dev, NULL, &inno_hdmi_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create HDMI PHY\n");
		return PTR_ERR(phy);
	}

	phy_set_drvdata(phy, inno);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(dev, "failed to register PHY provider\n");
		return PTR_ERR(phy_provider);
	}

	inno_hdmi_phy_init(inno);

	ret = inno_hdmi_phy_clk_register(inno);
	if (ret)
		return ret;

	return 0;
}

static int inno_hdmi_phy_remove(struct platform_device *pdev)
{
	of_clk_del_provider(pdev->dev.of_node);

	return 0;
}

static const struct of_device_id inno_hdmi_phy_of_match[] = {
	{ .compatible = "rockchip,rk3228-hdmi-phy" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, inno_hdmi_phy_of_match);

static struct platform_driver inno_hdmi_phy_driver = {
	.probe  = inno_hdmi_phy_probe,
	.remove = inno_hdmi_phy_remove,
	.driver = {
		.name = "inno-hdmi-phy",
		.of_match_table = of_match_ptr(inno_hdmi_phy_of_match),
	},
};

module_platform_driver(inno_hdmi_phy_driver);

MODULE_DESCRIPTION("Innosilion HDMI 2.0 Transmitter PHY Driver");
MODULE_LICENSE("GPL v2");
