/*
 * Copyright (c) 2017 BayLibre, SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <linux/clk-provider.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include "gxbb-aoclk.h"

/*
 * The AO Domain embeds a dual/divider to generate a more precise
 * 32,768KHz clock for low-power suspend mode and CEC.
 *                      ______   ______
 *                     |      | |      |
 *         ______      | Div1 |-| Cnt1 |       ______
 *        |      |    /|______| |______|\     |      |
 * Xtal-->| Gate |---|  ______   ______  X-X--| Gate |-->
 *        |______| |  \|      | |      |/  |  |______|
 *                 |   | Div2 |-| Cnt2 |   |
 *                 |   |______| |______|   |
 *                 |_______________________|
 *
 * The dividing can be switched to single or dual, with a counter
 * for each divider to set when the switching is done.
 * The entire dividing mechanism can be also bypassed.
 */

#define CLK_CNTL0_N1_MASK	GENMASK(11, 0)
#define CLK_CNTL0_N2_MASK	GENMASK(23, 12)
#define CLK_CNTL0_DUALDIV_EN	BIT(28)
#define CLK_CNTL0_OUT_GATE_EN	BIT(30)
#define CLK_CNTL0_IN_GATE_EN	BIT(31)

#define CLK_CNTL1_M1_MASK	GENMASK(11, 0)
#define CLK_CNTL1_M2_MASK	GENMASK(23, 12)
#define CLK_CNTL1_BYPASS_EN	BIT(24)
#define CLK_CNTL1_SELECT_OSC	BIT(27)

#define PWR_CNTL_ALT_32K_SEL	GENMASK(13, 10)

struct cec_32k_freq_table {
	unsigned long parent_rate;
	unsigned long target_rate;
	bool dualdiv;
	unsigned int n1;
	unsigned int n2;
	unsigned int m1;
	unsigned int m2;
};

static const struct cec_32k_freq_table aoclk_cec_32k_table[] = {
	[0] = {
		.parent_rate = 24000000,
		.target_rate = 32768,
		.dualdiv = true,
		.n1 = 733,
		.n2 = 732,
		.m1 = 8,
		.m2 = 11,
	},
};

/*
 * If CLK_CNTL0_DUALDIV_EN == 0
 *  - will use N1 divider only
 * If CLK_CNTL0_DUALDIV_EN == 1
 *  - hold M1 cycles of N1 divider then changes to N2
 *  - hold M2 cycles of N2 divider then changes to N1
 * Then we can get more accurate division.
 */
static unsigned long aoclk_cec_32k_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct aoclk_cec_32k *cec_32k = to_aoclk_cec_32k(hw);
	unsigned long n1;
	u32 reg0, reg1;

	regmap_read(cec_32k->regmap, AO_RTC_ALT_CLK_CNTL0, &reg0);
	regmap_read(cec_32k->regmap, AO_RTC_ALT_CLK_CNTL1, &reg1);

	if (reg1 & CLK_CNTL1_BYPASS_EN)
		return parent_rate;

	if (reg0 & CLK_CNTL0_DUALDIV_EN) {
		unsigned long n2, m1, m2, f1, f2, p1, p2;

		n1 = FIELD_GET(CLK_CNTL0_N1_MASK, reg0) + 1;
		n2 = FIELD_GET(CLK_CNTL0_N2_MASK, reg0) + 1;

		m1 = FIELD_GET(CLK_CNTL1_M1_MASK, reg1) + 1;
		m2 = FIELD_GET(CLK_CNTL1_M2_MASK, reg1) + 1;

		f1 = DIV_ROUND_CLOSEST(parent_rate, n1);
		f2 = DIV_ROUND_CLOSEST(parent_rate, n2);

		p1 = DIV_ROUND_CLOSEST(100000000 * m1, f1 * (m1 + m2));
		p2 = DIV_ROUND_CLOSEST(100000000 * m2, f2 * (m1 + m2));

		return DIV_ROUND_UP(100000000, p1 + p2);
	}

	n1 = FIELD_GET(CLK_CNTL0_N1_MASK, reg0) + 1;

	return DIV_ROUND_CLOSEST(parent_rate, n1);
}

static const struct cec_32k_freq_table *find_cec_32k_freq(unsigned long rate,
							  unsigned long prate)
{
	int i;

	for (i = 0 ; i < ARRAY_SIZE(aoclk_cec_32k_table) ; ++i)
		if (aoclk_cec_32k_table[i].parent_rate == prate &&
		    aoclk_cec_32k_table[i].target_rate == rate)
			return &aoclk_cec_32k_table[i];

	return NULL;
}

static long aoclk_cec_32k_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *prate)
{
	const struct cec_32k_freq_table *freq = find_cec_32k_freq(rate,
								  *prate);

	/* If invalid return first one */
	if (!freq)
		return aoclk_cec_32k_table[0].target_rate;

	return freq->target_rate;
}

/*
 * From the Amlogic init procedure, the IN and OUT gates needs to be handled
 * in the init procedure to avoid any glitches.
 */

static int aoclk_cec_32k_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	const struct cec_32k_freq_table *freq = find_cec_32k_freq(rate,
								  parent_rate);
	struct aoclk_cec_32k *cec_32k = to_aoclk_cec_32k(hw);
	u32 reg = 0;

	if (!freq)
		return -EINVAL;

	/* Disable clock */
	regmap_update_bits(cec_32k->regmap, AO_RTC_ALT_CLK_CNTL0,
			   CLK_CNTL0_IN_GATE_EN | CLK_CNTL0_OUT_GATE_EN, 0);

	reg = FIELD_PREP(CLK_CNTL0_N1_MASK, freq->n1 - 1);
	if (freq->dualdiv)
		reg |= CLK_CNTL0_DUALDIV_EN |
		       FIELD_PREP(CLK_CNTL0_N2_MASK, freq->n2 - 1);

	regmap_write(cec_32k->regmap, AO_RTC_ALT_CLK_CNTL0, reg);

	reg = FIELD_PREP(CLK_CNTL1_M1_MASK, freq->m1 - 1);
	if (freq->dualdiv)
		reg |= FIELD_PREP(CLK_CNTL1_M2_MASK, freq->m2 - 1);

	regmap_write(cec_32k->regmap, AO_RTC_ALT_CLK_CNTL1, reg);

	/* Enable clock */
	regmap_update_bits(cec_32k->regmap, AO_RTC_ALT_CLK_CNTL0,
			   CLK_CNTL0_IN_GATE_EN, CLK_CNTL0_IN_GATE_EN);

	udelay(200);

	regmap_update_bits(cec_32k->regmap, AO_RTC_ALT_CLK_CNTL0,
			   CLK_CNTL0_OUT_GATE_EN, CLK_CNTL0_OUT_GATE_EN);

	regmap_update_bits(cec_32k->regmap, AO_CRT_CLK_CNTL1,
			   CLK_CNTL1_SELECT_OSC, CLK_CNTL1_SELECT_OSC);

	/* Select 32k from XTAL */
	regmap_update_bits(cec_32k->regmap,
			  AO_RTI_PWR_CNTL_REG0,
			  PWR_CNTL_ALT_32K_SEL,
			  FIELD_PREP(PWR_CNTL_ALT_32K_SEL, 4));

	return 0;
}

const struct clk_ops meson_aoclk_cec_32k_ops = {
	.recalc_rate = aoclk_cec_32k_recalc_rate,
	.round_rate = aoclk_cec_32k_round_rate,
	.set_rate = aoclk_cec_32k_set_rate,
};
