// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Broadcom
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include "vc4_hdmi.h"
#include "vc4_regs.h"
#include "vc4_hdmi_regs.h"

#define VC4_HDMI_TX_PHY_RESET_CTL_PLL_RESETB	BIT(5)
#define VC4_HDMI_TX_PHY_RESET_CTL_PLLDIV_RESETB	BIT(4)
#define VC4_HDMI_TX_PHY_RESET_CTL_TX_CK_RESET	BIT(3)
#define VC4_HDMI_TX_PHY_RESET_CTL_TX_2_RESET	BIT(2)
#define VC4_HDMI_TX_PHY_RESET_CTL_TX_1_RESET	BIT(1)
#define VC4_HDMI_TX_PHY_RESET_CTL_TX_0_RESET	BIT(0)

#define VC4_HDMI_TX_PHY_POWERDOWN_CTL_RNDGEN_PWRDN	BIT(4)

#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_2_PREEMP_SHIFT	29
#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_2_PREEMP_MASK	VC4_MASK(31, 29)
#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_2_MAINDRV_SHIFT	24
#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_2_MAINDRV_MASK	VC4_MASK(28, 24)
#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_1_PREEMP_SHIFT	21
#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_1_PREEMP_MASK	VC4_MASK(23, 21)
#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_1_MAINDRV_SHIFT	16
#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_1_MAINDRV_MASK	VC4_MASK(20, 16)
#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_0_PREEMP_SHIFT	13
#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_0_PREEMP_MASK	VC4_MASK(15, 13)
#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_0_MAINDRV_SHIFT	8
#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_0_MAINDRV_MASK	VC4_MASK(12, 8)
#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_CK_PREEMP_SHIFT	5
#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_CK_PREEMP_MASK	VC4_MASK(7, 5)
#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_CK_MAINDRV_SHIFT	0
#define VC4_HDMI_TX_PHY_CTL_0_PREEMP_CK_MAINDRV_MASK	VC4_MASK(4, 0)

#define VC4_HDMI_TX_PHY_CTL_1_RES_SEL_DATA2_SHIFT	15
#define VC4_HDMI_TX_PHY_CTL_1_RES_SEL_DATA2_MASK	VC4_MASK(19, 15)
#define VC4_HDMI_TX_PHY_CTL_1_RES_SEL_DATA1_SHIFT	10
#define VC4_HDMI_TX_PHY_CTL_1_RES_SEL_DATA1_MASK	VC4_MASK(14, 10)
#define VC4_HDMI_TX_PHY_CTL_1_RES_SEL_DATA0_SHIFT	5
#define VC4_HDMI_TX_PHY_CTL_1_RES_SEL_DATA0_MASK	VC4_MASK(9, 5)
#define VC4_HDMI_TX_PHY_CTL_1_RES_SEL_CK_SHIFT		0
#define VC4_HDMI_TX_PHY_CTL_1_RES_SEL_CK_MASK		VC4_MASK(4, 0)

#define VC4_HDMI_TX_PHY_CTL_2_VCO_GAIN_SHIFT		16
#define VC4_HDMI_TX_PHY_CTL_2_VCO_GAIN_MASK		VC4_MASK(19, 16)
#define VC4_HDMI_TX_PHY_CTL_2_TERM_RES_SELDATA2_SHIFT	12
#define VC4_HDMI_TX_PHY_CTL_2_TERM_RES_SELDATA2_MASK	VC4_MASK(15, 12)
#define VC4_HDMI_TX_PHY_CTL_2_TERM_RES_SELDATA1_SHIFT	8
#define VC4_HDMI_TX_PHY_CTL_2_TERM_RES_SELDATA1_MASK	VC4_MASK(11, 8)
#define VC4_HDMI_TX_PHY_CTL_2_TERM_RES_SELDATA0_SHIFT	4
#define VC4_HDMI_TX_PHY_CTL_2_TERM_RES_SELDATA0_MASK	VC4_MASK(7, 4)
#define VC4_HDMI_TX_PHY_CTL_2_TERM_RES_SELCK_SHIFT	0
#define VC4_HDMI_TX_PHY_CTL_2_TERM_RES_SELCK_MASK	VC4_MASK(3, 0)

#define VC4_HDMI_TX_PHY_CTL_3_RP_SHIFT			17
#define VC4_HDMI_TX_PHY_CTL_3_RP_MASK			VC4_MASK(19, 17)
#define VC4_HDMI_TX_PHY_CTL_3_RZ_SHIFT			12
#define VC4_HDMI_TX_PHY_CTL_3_RZ_MASK			VC4_MASK(16, 12)
#define VC4_HDMI_TX_PHY_CTL_3_CP1_SHIFT			10
#define VC4_HDMI_TX_PHY_CTL_3_CP1_MASK			VC4_MASK(11, 10)
#define VC4_HDMI_TX_PHY_CTL_3_CP_SHIFT			8
#define VC4_HDMI_TX_PHY_CTL_3_CP_MASK			VC4_MASK(9, 8)
#define VC4_HDMI_TX_PHY_CTL_3_CZ_SHIFT			6
#define VC4_HDMI_TX_PHY_CTL_3_CZ_MASK			VC4_MASK(7, 6)
#define VC4_HDMI_TX_PHY_CTL_3_ICP_SHIFT			0
#define VC4_HDMI_TX_PHY_CTL_3_ICP_MASK			VC4_MASK(5, 0)

#define VC4_HDMI_TX_PHY_PLL_CTL_0_MASH11_MODE		BIT(13)
#define VC4_HDMI_TX_PHY_PLL_CTL_0_VC_RANGE_EN		BIT(12)
#define VC4_HDMI_TX_PHY_PLL_CTL_0_EMULATE_VC_LOW	BIT(11)
#define VC4_HDMI_TX_PHY_PLL_CTL_0_EMULATE_VC_HIGH	BIT(10)
#define VC4_HDMI_TX_PHY_PLL_CTL_0_VCO_SEL_SHIFT		9
#define VC4_HDMI_TX_PHY_PLL_CTL_0_VCO_SEL_MASK		VC4_MASK(9, 9)
#define VC4_HDMI_TX_PHY_PLL_CTL_0_VCO_FB_DIV2		BIT(8)
#define VC4_HDMI_TX_PHY_PLL_CTL_0_VCO_POST_DIV2		BIT(7)
#define VC4_HDMI_TX_PHY_PLL_CTL_0_VCO_CONT_EN		BIT(6)
#define VC4_HDMI_TX_PHY_PLL_CTL_0_ENA_VCO_CLK		BIT(5)

#define VC4_HDMI_TX_PHY_PLL_CTL_1_CPP_SHIFT			16
#define VC4_HDMI_TX_PHY_PLL_CTL_1_CPP_MASK			VC4_MASK(27, 16)
#define VC4_HDMI_TX_PHY_PLL_CTL_1_FREQ_DOUBLER_DELAY_SHIFT	14
#define VC4_HDMI_TX_PHY_PLL_CTL_1_FREQ_DOUBLER_DELAY_MASK	VC4_MASK(15, 14)
#define VC4_HDMI_TX_PHY_PLL_CTL_1_FREQ_DOUBLER_ENABLE		BIT(13)
#define VC4_HDMI_TX_PHY_PLL_CTL_1_POST_RST_SEL_SHIFT		11
#define VC4_HDMI_TX_PHY_PLL_CTL_1_POST_RST_SEL_MASK		VC4_MASK(12, 11)

#define VC4_HDMI_TX_PHY_CLK_DIV_VCO_SHIFT		8
#define VC4_HDMI_TX_PHY_CLK_DIV_VCO_MASK		VC4_MASK(15, 8)

#define VC4_HDMI_TX_PHY_PLL_CFG_PDIV_SHIFT		0
#define VC4_HDMI_TX_PHY_PLL_CFG_PDIV_MASK		VC4_MASK(3, 0)

#define VC4_HDMI_TX_PHY_CHANNEL_SWAP_TXCK_OUT_SEL_MASK	VC4_MASK(13, 12)
#define VC4_HDMI_TX_PHY_CHANNEL_SWAP_TXCK_OUT_SEL_SHIFT	12
#define VC4_HDMI_TX_PHY_CHANNEL_SWAP_TX2_OUT_SEL_MASK	VC4_MASK(9, 8)
#define VC4_HDMI_TX_PHY_CHANNEL_SWAP_TX2_OUT_SEL_SHIFT	8
#define VC4_HDMI_TX_PHY_CHANNEL_SWAP_TX1_OUT_SEL_MASK	VC4_MASK(5, 4)
#define VC4_HDMI_TX_PHY_CHANNEL_SWAP_TX1_OUT_SEL_SHIFT	4
#define VC4_HDMI_TX_PHY_CHANNEL_SWAP_TX0_OUT_SEL_MASK	VC4_MASK(1, 0)
#define VC4_HDMI_TX_PHY_CHANNEL_SWAP_TX0_OUT_SEL_SHIFT	0

#define VC4_HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_1_MIN_LIMIT_MASK		VC4_MASK(27, 0)
#define VC4_HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_1_MIN_LIMIT_SHIFT	0

#define VC4_HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_2_MAX_LIMIT_MASK		VC4_MASK(27, 0)
#define VC4_HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_2_MAX_LIMIT_SHIFT	0

#define VC4_HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_4_STABLE_THRESHOLD_MASK	VC4_MASK(31, 16)
#define VC4_HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_4_STABLE_THRESHOLD_SHIFT	16
#define VC4_HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_4_HOLD_THRESHOLD_MASK	VC4_MASK(15, 0)
#define VC4_HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_4_HOLD_THRESHOLD_SHIFT	0

#define VC4_HDMI_RM_CONTROL_EN_FREEZE_COUNTERS		BIT(19)
#define VC4_HDMI_RM_CONTROL_EN_LOAD_INTEGRATOR		BIT(17)
#define VC4_HDMI_RM_CONTROL_FREE_RUN			BIT(4)

#define VC4_HDMI_RM_OFFSET_ONLY				BIT(31)
#define VC4_HDMI_RM_OFFSET_OFFSET_SHIFT			0
#define VC4_HDMI_RM_OFFSET_OFFSET_MASK			VC4_MASK(30, 0)

#define VC4_HDMI_RM_FORMAT_SHIFT_SHIFT			24
#define VC4_HDMI_RM_FORMAT_SHIFT_MASK			VC4_MASK(25, 24)

#define OSCILLATOR_FREQUENCY	54000000

void vc4_hdmi_phy_init(struct vc4_hdmi *vc4_hdmi,
		       struct vc4_hdmi_connector_state *conn_state)
{
	unsigned long flags;

	/* PHY should be in reset, like
	 * vc4_hdmi_encoder_disable() does.
	 */

	spin_lock_irqsave(&vc4_hdmi->hw_lock, flags);

	HDMI_WRITE(HDMI_TX_PHY_RESET_CTL, 0xf << 16);
	HDMI_WRITE(HDMI_TX_PHY_RESET_CTL, 0);

	spin_unlock_irqrestore(&vc4_hdmi->hw_lock, flags);
}

void vc4_hdmi_phy_disable(struct vc4_hdmi *vc4_hdmi)
{
	unsigned long flags;

	spin_lock_irqsave(&vc4_hdmi->hw_lock, flags);
	HDMI_WRITE(HDMI_TX_PHY_RESET_CTL, 0xf << 16);
	spin_unlock_irqrestore(&vc4_hdmi->hw_lock, flags);
}

void vc4_hdmi_phy_rng_enable(struct vc4_hdmi *vc4_hdmi)
{
	unsigned long flags;

	spin_lock_irqsave(&vc4_hdmi->hw_lock, flags);
	HDMI_WRITE(HDMI_TX_PHY_CTL_0,
		   HDMI_READ(HDMI_TX_PHY_CTL_0) &
		   ~VC4_HDMI_TX_PHY_RNG_PWRDN);
	spin_unlock_irqrestore(&vc4_hdmi->hw_lock, flags);
}

void vc4_hdmi_phy_rng_disable(struct vc4_hdmi *vc4_hdmi)
{
	unsigned long flags;

	spin_lock_irqsave(&vc4_hdmi->hw_lock, flags);
	HDMI_WRITE(HDMI_TX_PHY_CTL_0,
		   HDMI_READ(HDMI_TX_PHY_CTL_0) |
		   VC4_HDMI_TX_PHY_RNG_PWRDN);
	spin_unlock_irqrestore(&vc4_hdmi->hw_lock, flags);
}

static unsigned long long
phy_get_vco_freq(unsigned long long clock, u8 *vco_sel, u8 *vco_div)
{
	unsigned long long vco_freq = clock;
	unsigned int _vco_div = 0;
	unsigned int _vco_sel = 0;

	while (vco_freq < 3000000000ULL) {
		_vco_div++;
		vco_freq = clock * _vco_div * 10;
	}

	if (vco_freq > 4500000000ULL)
		_vco_sel = 1;

	*vco_sel = _vco_sel;
	*vco_div = _vco_div;

	return vco_freq;
}

static u8 phy_get_cp_current(unsigned long vco_freq)
{
	if (vco_freq < 3700000000ULL)
		return 0x1c;

	return 0x18;
}

static u32 phy_get_rm_offset(unsigned long long vco_freq)
{
	unsigned long long fref = OSCILLATOR_FREQUENCY;
	u64 offset = 0;

	/* RM offset is stored as 9.22 format */
	offset = vco_freq * 2;
	offset = offset << 22;
	do_div(offset, fref);
	offset >>= 2;

	return offset;
}

static u8 phy_get_vco_gain(unsigned long long vco_freq)
{
	if (vco_freq < 3350000000ULL)
		return 0xf;

	if (vco_freq < 3700000000ULL)
		return 0xc;

	if (vco_freq < 4050000000ULL)
		return 0x6;

	if (vco_freq < 4800000000ULL)
		return 0x5;

	if (vco_freq < 5200000000ULL)
		return 0x7;

	return 0x2;
}

struct phy_lane_settings {
	struct {
		u8 preemphasis;
		u8 main_driver;
	} amplitude;

	u8 res_sel_data;
	u8 term_res_sel_data;
};

struct phy_settings {
	unsigned long long min_rate;
	unsigned long long max_rate;
	struct phy_lane_settings channel[3];
	struct phy_lane_settings clock;
};

static const struct phy_settings vc5_hdmi_phy_settings[] = {
	{
		0, 50000000,
		{
			{{0x0, 0x0A}, 0x12, 0x0},
			{{0x0, 0x0A}, 0x12, 0x0},
			{{0x0, 0x0A}, 0x12, 0x0}
		},
		{{0x0, 0x0A}, 0x18, 0x0},
	},
	{
		50000001, 75000000,
		{
			{{0x0, 0x09}, 0x12, 0x0},
			{{0x0, 0x09}, 0x12, 0x0},
			{{0x0, 0x09}, 0x12, 0x0}
		},
		{{0x0, 0x0C}, 0x18, 0x3},
	},
	{
		75000001,   165000000,
		{
			{{0x0, 0x09}, 0x12, 0x0},
			{{0x0, 0x09}, 0x12, 0x0},
			{{0x0, 0x09}, 0x12, 0x0}
		},
		{{0x0, 0x0C}, 0x18, 0x3},
	},
	{
		165000001,  250000000,
		{
			{{0x0, 0x0F}, 0x12, 0x1},
			{{0x0, 0x0F}, 0x12, 0x1},
			{{0x0, 0x0F}, 0x12, 0x1}
		},
		{{0x0, 0x0C}, 0x18, 0x3},
	},
	{
		250000001,  340000000,
		{
			{{0x2, 0x0D}, 0x12, 0x1},
			{{0x2, 0x0D}, 0x12, 0x1},
			{{0x2, 0x0D}, 0x12, 0x1}
		},
		{{0x0, 0x0C}, 0x18, 0xF},
	},
	{
		340000001,  450000000,
		{
			{{0x0, 0x1B}, 0x12, 0xF},
			{{0x0, 0x1B}, 0x12, 0xF},
			{{0x0, 0x1B}, 0x12, 0xF}
		},
		{{0x0, 0x0A}, 0x12, 0xF},
	},
	{
		450000001,  600000000,
		{
			{{0x0, 0x1C}, 0x12, 0xF},
			{{0x0, 0x1C}, 0x12, 0xF},
			{{0x0, 0x1C}, 0x12, 0xF}
		},
		{{0x0, 0x0B}, 0x13, 0xF},
	},
};

static const struct phy_settings *phy_get_settings(unsigned long long tmds_rate)
{
	unsigned int count = ARRAY_SIZE(vc5_hdmi_phy_settings);
	unsigned int i;

	for (i = 0; i < count; i++) {
		const struct phy_settings *s = &vc5_hdmi_phy_settings[i];

		if (tmds_rate >= s->min_rate && tmds_rate <= s->max_rate)
			return s;
	}

	/*
	 * If the pixel clock exceeds our max setting, try the max
	 * setting anyway.
	 */
	return &vc5_hdmi_phy_settings[count - 1];
}

static const struct phy_lane_settings *
phy_get_channel_settings(enum vc4_hdmi_phy_channel chan,
			 unsigned long long tmds_rate)
{
	const struct phy_settings *settings = phy_get_settings(tmds_rate);

	if (chan == PHY_LANE_CK)
		return &settings->clock;

	return &settings->channel[chan];
}

static void vc5_hdmi_reset_phy(struct vc4_hdmi *vc4_hdmi)
{
	lockdep_assert_held(&vc4_hdmi->hw_lock);

	HDMI_WRITE(HDMI_TX_PHY_RESET_CTL, 0x0f);
	HDMI_WRITE(HDMI_TX_PHY_POWERDOWN_CTL, BIT(10));
}

void vc5_hdmi_phy_init(struct vc4_hdmi *vc4_hdmi,
		       struct vc4_hdmi_connector_state *conn_state)
{
	const struct phy_lane_settings *chan0_settings, *chan1_settings, *chan2_settings, *clock_settings;
	const struct vc4_hdmi_variant *variant = vc4_hdmi->variant;
	unsigned long long pixel_freq = conn_state->pixel_rate;
	unsigned long long vco_freq;
	unsigned char word_sel;
	unsigned long flags;
	u8 vco_sel, vco_div;

	vco_freq = phy_get_vco_freq(pixel_freq, &vco_sel, &vco_div);

	spin_lock_irqsave(&vc4_hdmi->hw_lock, flags);

	vc5_hdmi_reset_phy(vc4_hdmi);

	HDMI_WRITE(HDMI_TX_PHY_POWERDOWN_CTL,
		   VC4_HDMI_TX_PHY_POWERDOWN_CTL_RNDGEN_PWRDN);

	HDMI_WRITE(HDMI_TX_PHY_RESET_CTL,
		   HDMI_READ(HDMI_TX_PHY_RESET_CTL) &
		   ~VC4_HDMI_TX_PHY_RESET_CTL_TX_0_RESET &
		   ~VC4_HDMI_TX_PHY_RESET_CTL_TX_1_RESET &
		   ~VC4_HDMI_TX_PHY_RESET_CTL_TX_2_RESET &
		   ~VC4_HDMI_TX_PHY_RESET_CTL_TX_CK_RESET);

	HDMI_WRITE(HDMI_RM_CONTROL,
		   HDMI_READ(HDMI_RM_CONTROL) |
		   VC4_HDMI_RM_CONTROL_EN_FREEZE_COUNTERS |
		   VC4_HDMI_RM_CONTROL_EN_LOAD_INTEGRATOR |
		   VC4_HDMI_RM_CONTROL_FREE_RUN);

	HDMI_WRITE(HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_1,
		   (HDMI_READ(HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_1) &
		    ~VC4_HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_1_MIN_LIMIT_MASK) |
		   VC4_SET_FIELD(0, VC4_HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_1_MIN_LIMIT));

	HDMI_WRITE(HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_2,
		   (HDMI_READ(HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_2) &
		    ~VC4_HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_2_MAX_LIMIT_MASK) |
		   VC4_SET_FIELD(0, VC4_HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_2_MAX_LIMIT));

	HDMI_WRITE(HDMI_RM_OFFSET,
		   VC4_SET_FIELD(phy_get_rm_offset(vco_freq),
				 VC4_HDMI_RM_OFFSET_OFFSET) |
		   VC4_HDMI_RM_OFFSET_ONLY);

	HDMI_WRITE(HDMI_TX_PHY_CLK_DIV,
		   VC4_SET_FIELD(vco_div, VC4_HDMI_TX_PHY_CLK_DIV_VCO));

	HDMI_WRITE(HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_4,
		   VC4_SET_FIELD(0xe147, VC4_HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_4_HOLD_THRESHOLD) |
		   VC4_SET_FIELD(0xe14, VC4_HDMI_TX_PHY_PLL_CALIBRATION_CONFIG_4_STABLE_THRESHOLD));

	HDMI_WRITE(HDMI_TX_PHY_PLL_CTL_0,
		   VC4_HDMI_TX_PHY_PLL_CTL_0_ENA_VCO_CLK |
		   VC4_HDMI_TX_PHY_PLL_CTL_0_VCO_CONT_EN |
		   VC4_HDMI_TX_PHY_PLL_CTL_0_MASH11_MODE |
		   VC4_SET_FIELD(vco_sel, VC4_HDMI_TX_PHY_PLL_CTL_0_VCO_SEL));

	HDMI_WRITE(HDMI_TX_PHY_PLL_CTL_1,
		   HDMI_READ(HDMI_TX_PHY_PLL_CTL_1) |
		   VC4_HDMI_TX_PHY_PLL_CTL_1_FREQ_DOUBLER_ENABLE |
		   VC4_SET_FIELD(3, VC4_HDMI_TX_PHY_PLL_CTL_1_POST_RST_SEL) |
		   VC4_SET_FIELD(1, VC4_HDMI_TX_PHY_PLL_CTL_1_FREQ_DOUBLER_DELAY) |
		   VC4_SET_FIELD(0x8a, VC4_HDMI_TX_PHY_PLL_CTL_1_CPP));

	HDMI_WRITE(HDMI_RM_FORMAT,
		   HDMI_READ(HDMI_RM_FORMAT) |
		   VC4_SET_FIELD(2, VC4_HDMI_RM_FORMAT_SHIFT));

	HDMI_WRITE(HDMI_TX_PHY_PLL_CFG,
		   HDMI_READ(HDMI_TX_PHY_PLL_CFG) |
		   VC4_SET_FIELD(1, VC4_HDMI_TX_PHY_PLL_CFG_PDIV));

	if (pixel_freq >= 340000000)
		word_sel = 3;
	else
		word_sel = 0;
	HDMI_WRITE(HDMI_TX_PHY_TMDS_CLK_WORD_SEL, word_sel);

	HDMI_WRITE(HDMI_TX_PHY_CTL_3,
		   VC4_SET_FIELD(phy_get_cp_current(vco_freq),
				 VC4_HDMI_TX_PHY_CTL_3_ICP) |
		   VC4_SET_FIELD(1, VC4_HDMI_TX_PHY_CTL_3_CP) |
		   VC4_SET_FIELD(1, VC4_HDMI_TX_PHY_CTL_3_CP1) |
		   VC4_SET_FIELD(3, VC4_HDMI_TX_PHY_CTL_3_CZ) |
		   VC4_SET_FIELD(4, VC4_HDMI_TX_PHY_CTL_3_RP) |
		   VC4_SET_FIELD(6, VC4_HDMI_TX_PHY_CTL_3_RZ));

	chan0_settings =
		phy_get_channel_settings(variant->phy_lane_mapping[PHY_LANE_0],
					 pixel_freq);
	chan1_settings =
		phy_get_channel_settings(variant->phy_lane_mapping[PHY_LANE_1],
					 pixel_freq);
	chan2_settings =
		phy_get_channel_settings(variant->phy_lane_mapping[PHY_LANE_2],
					 pixel_freq);
	clock_settings =
		phy_get_channel_settings(variant->phy_lane_mapping[PHY_LANE_CK],
					 pixel_freq);

	HDMI_WRITE(HDMI_TX_PHY_CTL_0,
		   VC4_SET_FIELD(chan0_settings->amplitude.preemphasis,
				 VC4_HDMI_TX_PHY_CTL_0_PREEMP_0_PREEMP) |
		   VC4_SET_FIELD(chan0_settings->amplitude.main_driver,
				 VC4_HDMI_TX_PHY_CTL_0_PREEMP_0_MAINDRV) |
		   VC4_SET_FIELD(chan1_settings->amplitude.preemphasis,
				 VC4_HDMI_TX_PHY_CTL_0_PREEMP_1_PREEMP) |
		   VC4_SET_FIELD(chan1_settings->amplitude.main_driver,
				 VC4_HDMI_TX_PHY_CTL_0_PREEMP_1_MAINDRV) |
		   VC4_SET_FIELD(chan2_settings->amplitude.preemphasis,
				 VC4_HDMI_TX_PHY_CTL_0_PREEMP_2_PREEMP) |
		   VC4_SET_FIELD(chan2_settings->amplitude.main_driver,
				 VC4_HDMI_TX_PHY_CTL_0_PREEMP_2_MAINDRV) |
		   VC4_SET_FIELD(clock_settings->amplitude.preemphasis,
				 VC4_HDMI_TX_PHY_CTL_0_PREEMP_CK_PREEMP) |
		   VC4_SET_FIELD(clock_settings->amplitude.main_driver,
				 VC4_HDMI_TX_PHY_CTL_0_PREEMP_CK_MAINDRV));

	HDMI_WRITE(HDMI_TX_PHY_CTL_1,
		   HDMI_READ(HDMI_TX_PHY_CTL_1) |
		   VC4_SET_FIELD(chan0_settings->res_sel_data,
				 VC4_HDMI_TX_PHY_CTL_1_RES_SEL_DATA0) |
		   VC4_SET_FIELD(chan1_settings->res_sel_data,
				 VC4_HDMI_TX_PHY_CTL_1_RES_SEL_DATA1) |
		   VC4_SET_FIELD(chan2_settings->res_sel_data,
				 VC4_HDMI_TX_PHY_CTL_1_RES_SEL_DATA2) |
		   VC4_SET_FIELD(clock_settings->res_sel_data,
				 VC4_HDMI_TX_PHY_CTL_1_RES_SEL_CK));

	HDMI_WRITE(HDMI_TX_PHY_CTL_2,
		   VC4_SET_FIELD(chan0_settings->term_res_sel_data,
				 VC4_HDMI_TX_PHY_CTL_2_TERM_RES_SELDATA0) |
		   VC4_SET_FIELD(chan1_settings->term_res_sel_data,
				 VC4_HDMI_TX_PHY_CTL_2_TERM_RES_SELDATA1) |
		   VC4_SET_FIELD(chan2_settings->term_res_sel_data,
				 VC4_HDMI_TX_PHY_CTL_2_TERM_RES_SELDATA2) |
		   VC4_SET_FIELD(clock_settings->term_res_sel_data,
				 VC4_HDMI_TX_PHY_CTL_2_TERM_RES_SELCK) |
		   VC4_SET_FIELD(phy_get_vco_gain(vco_freq),
				 VC4_HDMI_TX_PHY_CTL_2_VCO_GAIN));

	HDMI_WRITE(HDMI_TX_PHY_CHANNEL_SWAP,
		   VC4_SET_FIELD(variant->phy_lane_mapping[PHY_LANE_0],
				 VC4_HDMI_TX_PHY_CHANNEL_SWAP_TX0_OUT_SEL) |
		   VC4_SET_FIELD(variant->phy_lane_mapping[PHY_LANE_1],
				 VC4_HDMI_TX_PHY_CHANNEL_SWAP_TX1_OUT_SEL) |
		   VC4_SET_FIELD(variant->phy_lane_mapping[PHY_LANE_2],
				 VC4_HDMI_TX_PHY_CHANNEL_SWAP_TX2_OUT_SEL) |
		   VC4_SET_FIELD(variant->phy_lane_mapping[PHY_LANE_CK],
				 VC4_HDMI_TX_PHY_CHANNEL_SWAP_TXCK_OUT_SEL));

	HDMI_WRITE(HDMI_TX_PHY_RESET_CTL,
		   HDMI_READ(HDMI_TX_PHY_RESET_CTL) &
		   ~(VC4_HDMI_TX_PHY_RESET_CTL_PLL_RESETB |
		     VC4_HDMI_TX_PHY_RESET_CTL_PLLDIV_RESETB));

	HDMI_WRITE(HDMI_TX_PHY_RESET_CTL,
		   HDMI_READ(HDMI_TX_PHY_RESET_CTL) |
		   VC4_HDMI_TX_PHY_RESET_CTL_PLL_RESETB |
		   VC4_HDMI_TX_PHY_RESET_CTL_PLLDIV_RESETB);

	spin_unlock_irqrestore(&vc4_hdmi->hw_lock, flags);
}

void vc5_hdmi_phy_disable(struct vc4_hdmi *vc4_hdmi)
{
	unsigned long flags;

	spin_lock_irqsave(&vc4_hdmi->hw_lock, flags);
	vc5_hdmi_reset_phy(vc4_hdmi);
	spin_unlock_irqrestore(&vc4_hdmi->hw_lock, flags);
}

void vc5_hdmi_phy_rng_enable(struct vc4_hdmi *vc4_hdmi)
{
	unsigned long flags;

	spin_lock_irqsave(&vc4_hdmi->hw_lock, flags);
	HDMI_WRITE(HDMI_TX_PHY_POWERDOWN_CTL,
		   HDMI_READ(HDMI_TX_PHY_POWERDOWN_CTL) &
		   ~VC4_HDMI_TX_PHY_POWERDOWN_CTL_RNDGEN_PWRDN);
	spin_unlock_irqrestore(&vc4_hdmi->hw_lock, flags);
}

void vc5_hdmi_phy_rng_disable(struct vc4_hdmi *vc4_hdmi)
{
	unsigned long flags;

	spin_lock_irqsave(&vc4_hdmi->hw_lock, flags);
	HDMI_WRITE(HDMI_TX_PHY_POWERDOWN_CTL,
		   HDMI_READ(HDMI_TX_PHY_POWERDOWN_CTL) |
		   VC4_HDMI_TX_PHY_POWERDOWN_CTL_RNDGEN_PWRDN);
	spin_unlock_irqrestore(&vc4_hdmi->hw_lock, flags);
}
