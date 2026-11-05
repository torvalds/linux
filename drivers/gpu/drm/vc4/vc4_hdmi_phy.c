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

#define VC6_HDMI_TX_PHY_HDMI_POWERUP_CTL_BG_PWRUP	BIT(8)
#define VC6_HDMI_TX_PHY_HDMI_POWERUP_CTL_LDO_PWRUP	BIT(7)
#define VC6_HDMI_TX_PHY_HDMI_POWERUP_CTL_BIAS_PWRUP	BIT(6)
#define VC6_HDMI_TX_PHY_HDMI_POWERUP_CTL_RNDGEN_PWRUP	BIT(4)
#define VC6_HDMI_TX_PHY_HDMI_POWERUP_CTL_TX_CK_PWRUP	BIT(3)
#define VC6_HDMI_TX_PHY_HDMI_POWERUP_CTL_TX_2_PWRUP	BIT(2)
#define VC6_HDMI_TX_PHY_HDMI_POWERUP_CTL_TX_1_PWRUP	BIT(1)
#define VC6_HDMI_TX_PHY_HDMI_POWERUP_CTL_TX_0_PWRUP	BIT(0)

#define VC6_HDMI_TX_PHY_PLL_REFCLK_REFCLK_SEL_CMOS	BIT(13)
#define VC6_HDMI_TX_PHY_PLL_REFCLK_REFFRQ_MASK		VC4_MASK(9, 0)

#define VC6_HDMI_TX_PHY_PLL_POST_KDIV_CLK0_SEL_MASK	VC4_MASK(3, 2)
#define VC6_HDMI_TX_PHY_PLL_POST_KDIV_KDIV_MASK		VC4_MASK(1, 0)

#define VC6_HDMI_TX_PHY_PLL_VCOCLK_DIV_VCODIV_EN	BIT(10)
#define VC6_HDMI_TX_PHY_PLL_VCOCLK_DIV_VCODIV_MASK	VC4_MASK(9, 0)

#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EXT_CURRENT_CTL_MASK	VC4_MASK(31, 28)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_FFE_ENABLE_MASK		VC4_MASK(27, 27)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_SLEW_RATE_CTL_MASK	VC4_MASK(26, 26)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_FFE_POST_TAP_EN_MASK	VC4_MASK(25, 25)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_LDMOS_BIAS_CTL_MASK	VC4_MASK(24, 23)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_COM_MODE_LDMOS_EN_MASK	VC4_MASK(22, 22)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EDGE_SEL_MASK		VC4_MASK(21, 21)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EXT_CURRENT_SRC_HS_EN_MASK	VC4_MASK(20, 20)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_TERM_CTL_MASK		VC4_MASK(19, 18)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EXT_CURRENT_SRC_EN_MASK	VC4_MASK(17, 17)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_INT_CURRENT_SRC_EN_MASK	VC4_MASK(16, 16)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_INT_CURRENT_CTL_MASK	VC4_MASK(15, 12)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_INT_CURRENT_SRC_HS_EN_MASK	VC4_MASK(11, 11)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_MAIN_TAP_CURRENT_SELECT_MASK	VC4_MASK(10, 8)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_POST_TAP_CURRENT_SELECT_MASK	VC4_MASK(7, 5)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_SLEW_CTL_SLOW_LOADING_MASK	VC4_MASK(4, 3)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_SLEW_CTL_SLOW_DRIVING_MASK	VC4_MASK(2, 1)
#define VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_FFE_PRE_TAP_EN_MASK	VC4_MASK(0, 0)

#define VC6_HDMI_TX_PHY_PLL_RESET_CTL_PLL_PLLPOST_RESETB	BIT(1)
#define VC6_HDMI_TX_PHY_PLL_RESET_CTL_PLL_RESETB	BIT(0)

#define VC6_HDMI_TX_PHY_PLL_POWERUP_CTL_PLL_PWRUP	BIT(0)

#define OSCILLATOR_FREQUENCY	54000000

void vc4_hdmi_phy_init(struct vc4_hdmi *vc4_hdmi,
		       struct drm_connector_state *conn_state)
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
		       struct drm_connector_state *conn_state)
{
	const struct phy_lane_settings *chan0_settings, *chan1_settings, *chan2_settings, *clock_settings;
	const struct vc4_hdmi_variant *variant = vc4_hdmi->variant;
	unsigned long long pixel_freq = conn_state->hdmi.tmds_char_rate;
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

#define VC6_VCO_MIN_FREQ	(8ULL * 1000 * 1000 * 1000)
#define VC6_VCO_MAX_FREQ	(12ULL * 1000 * 1000 * 1000)

static unsigned long long
vc6_phy_get_vco_freq(unsigned long long tmds_rate, unsigned int *vco_div)
{
	unsigned int min_div;
	unsigned int max_div;
	unsigned int div;

	div = 0;
	while (tmds_rate * div * 10 < VC6_VCO_MIN_FREQ)
		div++;
	min_div = div;

	while (tmds_rate * (div + 1) * 10 < VC6_VCO_MAX_FREQ)
		div++;
	max_div = div;

	div = min_div + (max_div - min_div) / 2;

	*vco_div = div;
	return tmds_rate * div * 10;
}

struct vc6_phy_lane_settings {
	unsigned int ext_current_ctl:4;
	unsigned int ffe_enable:1;
	unsigned int slew_rate_ctl:1;
	unsigned int ffe_post_tap_en:1;
	unsigned int ldmos_bias_ctl:2;
	unsigned int com_mode_ldmos_en:1;
	unsigned int edge_sel:1;
	unsigned int ext_current_src_hs_en:1;
	unsigned int term_ctl:2;
	unsigned int ext_current_src_en:1;
	unsigned int int_current_src_en:1;
	unsigned int int_current_ctl:4;
	unsigned int int_current_src_hs_en:1;
	unsigned int main_tap_current_select:3;
	unsigned int post_tap_current_select:3;
	unsigned int slew_ctl_slow_loading:2;
	unsigned int slew_ctl_slow_driving:2;
	unsigned int ffe_pre_tap_en:1;
};

struct vc6_phy_settings {
	unsigned long long min_rate;
	unsigned long long max_rate;
	struct vc6_phy_lane_settings channel[3];
	struct vc6_phy_lane_settings clock;
};

static const struct vc6_phy_settings vc6_hdmi_phy_settings[] = {
	{
		0, 222000000,
		{
			{
				/* 200mA */
				.ext_current_ctl = 8,

				/* 0.85V */
				.ldmos_bias_ctl = 1,

				/* Enable External Current Source */
				.ext_current_src_en = 1,

				/* 200mA */
				.int_current_ctl = 8,

				/* 17.6 mA */
				.main_tap_current_select = 7,
			},
			{
				/* 200mA */
				.ext_current_ctl = 8,

				/* 0.85V */
				.ldmos_bias_ctl = 1,

				/* Enable External Current Source */
				.ext_current_src_en = 1,

				/* 200mA */
				.int_current_ctl = 8,

				/* 17.6 mA */
				.main_tap_current_select = 7,
			},
			{
				/* 200mA */
				.ext_current_ctl = 8,

				/* 0.85V */
				.ldmos_bias_ctl = 1,

				/* Enable External Current Source */
				.ext_current_src_en = 1,

				/* 200mA */
				.int_current_ctl = 8,

				/* 17.6 mA */
				.main_tap_current_select = 7,
			},
		},
		{
			/* 200mA */
			.ext_current_ctl = 8,

			/* 0.85V */
			.ldmos_bias_ctl = 1,

			/* Enable External Current Source */
			.ext_current_src_en = 1,

			/* 200mA */
			.int_current_ctl = 8,

			/* 17.6 mA */
			.main_tap_current_select = 7,
		},
	},
	{
		222000001, 297000000,
		{
			{
				/* 200mA and 180mA ?! */
				.ext_current_ctl = 12,

				/* 0.85V */
				.ldmos_bias_ctl = 1,

				/* 100 Ohm */
				.term_ctl = 1,

				/* Enable External Current Source */
				.ext_current_src_en = 1,

				/* Enable Internal Current Source */
				.int_current_src_en = 1,
			},
			{
				/* 200mA and 180mA ?! */
				.ext_current_ctl = 12,

				/* 0.85V */
				.ldmos_bias_ctl = 1,

				/* 100 Ohm */
				.term_ctl = 1,

				/* Enable External Current Source */
				.ext_current_src_en = 1,

				/* Enable Internal Current Source */
				.int_current_src_en = 1,
			},
			{
				/* 200mA and 180mA ?! */
				.ext_current_ctl = 12,

				/* 0.85V */
				.ldmos_bias_ctl = 1,

				/* 100 Ohm */
				.term_ctl = 1,

				/* Enable External Current Source */
				.ext_current_src_en = 1,

				/* Enable Internal Current Source */
				.int_current_src_en = 1,
			},
		},
		{
			/* 200mA and 180mA ?! */
			.ext_current_ctl = 12,

			/* 0.85V */
			.ldmos_bias_ctl = 1,

			/* 100 Ohm */
			.term_ctl = 1,

			/* Enable External Current Source */
			.ext_current_src_en = 1,

			/* Enable Internal Current Source */
			.int_current_src_en = 1,

			/* Internal Current Source Half Swing Enable*/
			.int_current_src_hs_en = 1,
		},
	},
	{
		297000001, 597000044,
		{
			{
				/* 200mA */
				.ext_current_ctl = 8,

				/* Normal Slew Rate Control */
				.slew_rate_ctl = 1,

				/* 0.85V */
				.ldmos_bias_ctl = 1,

				/* 50 Ohms */
				.term_ctl = 3,

				/* Enable External Current Source */
				.ext_current_src_en = 1,

				/* Enable Internal Current Source */
				.int_current_src_en = 1,

				/* 200mA */
				.int_current_ctl = 8,

				/* 17.6 mA */
				.main_tap_current_select = 7,
			},
			{
				/* 200mA */
				.ext_current_ctl = 8,

				/* Normal Slew Rate Control */
				.slew_rate_ctl = 1,

				/* 0.85V */
				.ldmos_bias_ctl = 1,

				/* 50 Ohms */
				.term_ctl = 3,

				/* Enable External Current Source */
				.ext_current_src_en = 1,

				/* Enable Internal Current Source */
				.int_current_src_en = 1,

				/* 200mA */
				.int_current_ctl = 8,

				/* 17.6 mA */
				.main_tap_current_select = 7,
			},
			{
				/* 200mA */
				.ext_current_ctl = 8,

				/* Normal Slew Rate Control */
				.slew_rate_ctl = 1,

				/* 0.85V */
				.ldmos_bias_ctl = 1,

				/* 50 Ohms */
				.term_ctl = 3,

				/* Enable External Current Source */
				.ext_current_src_en = 1,

				/* Enable Internal Current Source */
				.int_current_src_en = 1,

				/* 200mA */
				.int_current_ctl = 8,

				/* 17.6 mA */
				.main_tap_current_select = 7,
			},
		},
		{
			/* 200mA */
			.ext_current_ctl = 8,

			/* Normal Slew Rate Control */
			.slew_rate_ctl = 1,

			/* 0.85V */
			.ldmos_bias_ctl = 1,

			/* External Current Source Half Swing Enable*/
			.ext_current_src_hs_en = 1,

			/* 50 Ohms */
			.term_ctl = 3,

			/* Enable External Current Source */
			.ext_current_src_en = 1,

			/* Enable Internal Current Source */
			.int_current_src_en = 1,

			/* 200mA */
			.int_current_ctl = 8,

			/* Internal Current Source Half Swing Enable*/
			.int_current_src_hs_en = 1,

			/* 17.6 mA */
			.main_tap_current_select = 7,
		},
	},
};

static const struct vc6_phy_settings *
vc6_phy_get_settings(unsigned long long tmds_rate)
{
	unsigned int count = ARRAY_SIZE(vc6_hdmi_phy_settings);
	unsigned int i;

	for (i = 0; i < count; i++) {
		const struct vc6_phy_settings *s = &vc6_hdmi_phy_settings[i];

		if (tmds_rate >= s->min_rate && tmds_rate <= s->max_rate)
			return s;
	}

	/*
	 * If the pixel clock exceeds our max setting, try the max
	 * setting anyway.
	 */
	return &vc6_hdmi_phy_settings[count - 1];
}

static const struct vc6_phy_lane_settings *
vc6_phy_get_channel_settings(enum vc4_hdmi_phy_channel chan,
			     unsigned long long tmds_rate)
{
	const struct vc6_phy_settings *settings = vc6_phy_get_settings(tmds_rate);

	if (chan == PHY_LANE_CK)
		return &settings->clock;

	return &settings->channel[chan];
}

static void vc6_hdmi_reset_phy(struct vc4_hdmi *vc4_hdmi)
{
	lockdep_assert_held(&vc4_hdmi->hw_lock);

	HDMI_WRITE(HDMI_TX_PHY_RESET_CTL, 0);
	HDMI_WRITE(HDMI_TX_PHY_POWERUP_CTL, 0);
}

void vc6_hdmi_phy_init(struct vc4_hdmi *vc4_hdmi,
		       struct drm_connector_state *conn_state)
{
	const struct vc6_phy_lane_settings *chan0_settings;
	const struct vc6_phy_lane_settings *chan1_settings;
	const struct vc6_phy_lane_settings *chan2_settings;
	const struct vc6_phy_lane_settings *clock_settings;
	const struct vc4_hdmi_variant *variant = vc4_hdmi->variant;
	unsigned long long pixel_freq = conn_state->hdmi.tmds_char_rate;
	unsigned long long vco_freq;
	unsigned char word_sel;
	unsigned long flags;
	unsigned int vco_div;

	vco_freq = vc6_phy_get_vco_freq(pixel_freq, &vco_div);

	spin_lock_irqsave(&vc4_hdmi->hw_lock, flags);

	vc6_hdmi_reset_phy(vc4_hdmi);

	HDMI_WRITE(HDMI_TX_PHY_PLL_MISC_0, 0x810c6000);
	HDMI_WRITE(HDMI_TX_PHY_PLL_MISC_1, 0x00b8c451);
	HDMI_WRITE(HDMI_TX_PHY_PLL_MISC_2, 0x46402e31);
	HDMI_WRITE(HDMI_TX_PHY_PLL_MISC_3, 0x00b8c005);
	HDMI_WRITE(HDMI_TX_PHY_PLL_MISC_4, 0x42410261);
	HDMI_WRITE(HDMI_TX_PHY_PLL_MISC_5, 0xcc021001);
	HDMI_WRITE(HDMI_TX_PHY_PLL_MISC_6, 0xc8301c80);
	HDMI_WRITE(HDMI_TX_PHY_PLL_MISC_7, 0xb0804444);
	HDMI_WRITE(HDMI_TX_PHY_PLL_MISC_8, 0xf80f8000);

	HDMI_WRITE(HDMI_TX_PHY_PLL_REFCLK,
		   VC6_HDMI_TX_PHY_PLL_REFCLK_REFCLK_SEL_CMOS |
		   VC4_SET_FIELD(54, VC6_HDMI_TX_PHY_PLL_REFCLK_REFFRQ));

	HDMI_WRITE(HDMI_TX_PHY_RESET_CTL, 0x7f);

	HDMI_WRITE(HDMI_RM_OFFSET,
		   VC4_HDMI_RM_OFFSET_ONLY |
		   VC4_SET_FIELD(phy_get_rm_offset(vco_freq),
				 VC4_HDMI_RM_OFFSET_OFFSET));

	HDMI_WRITE(HDMI_TX_PHY_PLL_VCOCLK_DIV,
		   VC6_HDMI_TX_PHY_PLL_VCOCLK_DIV_VCODIV_EN |
		   VC4_SET_FIELD(vco_div,
				 VC6_HDMI_TX_PHY_PLL_VCOCLK_DIV_VCODIV));

	HDMI_WRITE(HDMI_TX_PHY_PLL_CFG,
		   VC4_SET_FIELD(0, VC4_HDMI_TX_PHY_PLL_CFG_PDIV));

	HDMI_WRITE(HDMI_TX_PHY_PLL_POST_KDIV,
		   VC4_SET_FIELD(2, VC6_HDMI_TX_PHY_PLL_POST_KDIV_CLK0_SEL) |
		   VC4_SET_FIELD(1, VC6_HDMI_TX_PHY_PLL_POST_KDIV_KDIV));

	chan0_settings =
		vc6_phy_get_channel_settings(variant->phy_lane_mapping[PHY_LANE_0],
					     pixel_freq);
	HDMI_WRITE(HDMI_TX_PHY_CTL_0,
		   VC4_SET_FIELD(chan0_settings->ext_current_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EXT_CURRENT_CTL) |
		   VC4_SET_FIELD(chan0_settings->ffe_enable,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_FFE_ENABLE) |
		   VC4_SET_FIELD(chan0_settings->slew_rate_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_SLEW_RATE_CTL) |
		   VC4_SET_FIELD(chan0_settings->ffe_post_tap_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_FFE_POST_TAP_EN) |
		   VC4_SET_FIELD(chan0_settings->ldmos_bias_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_LDMOS_BIAS_CTL) |
		   VC4_SET_FIELD(chan0_settings->com_mode_ldmos_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_COM_MODE_LDMOS_EN) |
		   VC4_SET_FIELD(chan0_settings->edge_sel,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EDGE_SEL) |
		   VC4_SET_FIELD(chan0_settings->ext_current_src_hs_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EXT_CURRENT_SRC_HS_EN) |
		   VC4_SET_FIELD(chan0_settings->term_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_TERM_CTL) |
		   VC4_SET_FIELD(chan0_settings->ext_current_src_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EXT_CURRENT_SRC_EN) |
		   VC4_SET_FIELD(chan0_settings->int_current_src_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_INT_CURRENT_SRC_EN) |
		   VC4_SET_FIELD(chan0_settings->int_current_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_INT_CURRENT_CTL) |
		   VC4_SET_FIELD(chan0_settings->int_current_src_hs_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_INT_CURRENT_SRC_HS_EN) |
		   VC4_SET_FIELD(chan0_settings->main_tap_current_select,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_MAIN_TAP_CURRENT_SELECT) |
		   VC4_SET_FIELD(chan0_settings->post_tap_current_select,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_POST_TAP_CURRENT_SELECT) |
		   VC4_SET_FIELD(chan0_settings->slew_ctl_slow_loading,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_SLEW_CTL_SLOW_LOADING) |
		   VC4_SET_FIELD(chan0_settings->slew_ctl_slow_driving,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_SLEW_CTL_SLOW_DRIVING) |
		   VC4_SET_FIELD(chan0_settings->ffe_pre_tap_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_FFE_PRE_TAP_EN));

	chan1_settings =
		vc6_phy_get_channel_settings(variant->phy_lane_mapping[PHY_LANE_1],
					     pixel_freq);
	HDMI_WRITE(HDMI_TX_PHY_CTL_1,
		   VC4_SET_FIELD(chan1_settings->ext_current_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EXT_CURRENT_CTL) |
		   VC4_SET_FIELD(chan1_settings->ffe_enable,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_FFE_ENABLE) |
		   VC4_SET_FIELD(chan1_settings->slew_rate_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_SLEW_RATE_CTL) |
		   VC4_SET_FIELD(chan1_settings->ffe_post_tap_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_FFE_POST_TAP_EN) |
		   VC4_SET_FIELD(chan1_settings->ldmos_bias_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_LDMOS_BIAS_CTL) |
		   VC4_SET_FIELD(chan1_settings->com_mode_ldmos_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_COM_MODE_LDMOS_EN) |
		   VC4_SET_FIELD(chan1_settings->edge_sel,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EDGE_SEL) |
		   VC4_SET_FIELD(chan1_settings->ext_current_src_hs_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EXT_CURRENT_SRC_HS_EN) |
		   VC4_SET_FIELD(chan1_settings->term_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_TERM_CTL) |
		   VC4_SET_FIELD(chan1_settings->ext_current_src_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EXT_CURRENT_SRC_EN) |
		   VC4_SET_FIELD(chan1_settings->int_current_src_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_INT_CURRENT_SRC_EN) |
		   VC4_SET_FIELD(chan1_settings->int_current_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_INT_CURRENT_CTL) |
		   VC4_SET_FIELD(chan1_settings->int_current_src_hs_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_INT_CURRENT_SRC_HS_EN) |
		   VC4_SET_FIELD(chan1_settings->main_tap_current_select,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_MAIN_TAP_CURRENT_SELECT) |
		   VC4_SET_FIELD(chan1_settings->post_tap_current_select,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_POST_TAP_CURRENT_SELECT) |
		   VC4_SET_FIELD(chan1_settings->slew_ctl_slow_loading,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_SLEW_CTL_SLOW_LOADING) |
		   VC4_SET_FIELD(chan1_settings->slew_ctl_slow_driving,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_SLEW_CTL_SLOW_DRIVING) |
		   VC4_SET_FIELD(chan1_settings->ffe_pre_tap_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_FFE_PRE_TAP_EN));

	chan2_settings =
		vc6_phy_get_channel_settings(variant->phy_lane_mapping[PHY_LANE_2],
					     pixel_freq);
	HDMI_WRITE(HDMI_TX_PHY_CTL_2,
		   VC4_SET_FIELD(chan2_settings->ext_current_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EXT_CURRENT_CTL) |
		   VC4_SET_FIELD(chan2_settings->ffe_enable,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_FFE_ENABLE) |
		   VC4_SET_FIELD(chan2_settings->slew_rate_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_SLEW_RATE_CTL) |
		   VC4_SET_FIELD(chan2_settings->ffe_post_tap_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_FFE_POST_TAP_EN) |
		   VC4_SET_FIELD(chan2_settings->ldmos_bias_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_LDMOS_BIAS_CTL) |
		   VC4_SET_FIELD(chan2_settings->com_mode_ldmos_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_COM_MODE_LDMOS_EN) |
		   VC4_SET_FIELD(chan2_settings->edge_sel,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EDGE_SEL) |
		   VC4_SET_FIELD(chan2_settings->ext_current_src_hs_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EXT_CURRENT_SRC_HS_EN) |
		   VC4_SET_FIELD(chan2_settings->term_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_TERM_CTL) |
		   VC4_SET_FIELD(chan2_settings->ext_current_src_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EXT_CURRENT_SRC_EN) |
		   VC4_SET_FIELD(chan2_settings->int_current_src_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_INT_CURRENT_SRC_EN) |
		   VC4_SET_FIELD(chan2_settings->int_current_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_INT_CURRENT_CTL) |
		   VC4_SET_FIELD(chan2_settings->int_current_src_hs_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_INT_CURRENT_SRC_HS_EN) |
		   VC4_SET_FIELD(chan2_settings->main_tap_current_select,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_MAIN_TAP_CURRENT_SELECT) |
		   VC4_SET_FIELD(chan2_settings->post_tap_current_select,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_POST_TAP_CURRENT_SELECT) |
		   VC4_SET_FIELD(chan2_settings->slew_ctl_slow_loading,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_SLEW_CTL_SLOW_LOADING) |
		   VC4_SET_FIELD(chan2_settings->slew_ctl_slow_driving,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_SLEW_CTL_SLOW_DRIVING) |
		   VC4_SET_FIELD(chan2_settings->ffe_pre_tap_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_FFE_PRE_TAP_EN));

	clock_settings =
		vc6_phy_get_channel_settings(variant->phy_lane_mapping[PHY_LANE_CK],
					     pixel_freq);
	HDMI_WRITE(HDMI_TX_PHY_CTL_CK,
		   VC4_SET_FIELD(clock_settings->ext_current_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EXT_CURRENT_CTL) |
		   VC4_SET_FIELD(clock_settings->ffe_enable,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_FFE_ENABLE) |
		   VC4_SET_FIELD(clock_settings->slew_rate_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_SLEW_RATE_CTL) |
		   VC4_SET_FIELD(clock_settings->ffe_post_tap_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_FFE_POST_TAP_EN) |
		   VC4_SET_FIELD(clock_settings->ldmos_bias_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_LDMOS_BIAS_CTL) |
		   VC4_SET_FIELD(clock_settings->com_mode_ldmos_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_COM_MODE_LDMOS_EN) |
		   VC4_SET_FIELD(clock_settings->edge_sel,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EDGE_SEL) |
		   VC4_SET_FIELD(clock_settings->ext_current_src_hs_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EXT_CURRENT_SRC_HS_EN) |
		   VC4_SET_FIELD(clock_settings->term_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_TERM_CTL) |
		   VC4_SET_FIELD(clock_settings->ext_current_src_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_EXT_CURRENT_SRC_EN) |
		   VC4_SET_FIELD(clock_settings->int_current_src_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_INT_CURRENT_SRC_EN) |
		   VC4_SET_FIELD(clock_settings->int_current_ctl,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_INT_CURRENT_CTL) |
		   VC4_SET_FIELD(clock_settings->int_current_src_hs_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_INT_CURRENT_SRC_HS_EN) |
		   VC4_SET_FIELD(clock_settings->main_tap_current_select,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_MAIN_TAP_CURRENT_SELECT) |
		   VC4_SET_FIELD(clock_settings->post_tap_current_select,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_POST_TAP_CURRENT_SELECT) |
		   VC4_SET_FIELD(clock_settings->slew_ctl_slow_loading,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_SLEW_CTL_SLOW_LOADING) |
		   VC4_SET_FIELD(clock_settings->slew_ctl_slow_driving,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_SLEW_CTL_SLOW_DRIVING) |
		   VC4_SET_FIELD(clock_settings->ffe_pre_tap_en,
				 VC6_HDMI_TX_PHY_HDMI_CTRL_CHX_FFE_PRE_TAP_EN));

	if (pixel_freq >= 340000000)
		word_sel = 3;
	else
		word_sel = 0;
	HDMI_WRITE(HDMI_TX_PHY_TMDS_CLK_WORD_SEL, word_sel);

	HDMI_WRITE(HDMI_TX_PHY_POWERUP_CTL,
		   VC6_HDMI_TX_PHY_HDMI_POWERUP_CTL_BG_PWRUP |
		   VC6_HDMI_TX_PHY_HDMI_POWERUP_CTL_LDO_PWRUP |
		   VC6_HDMI_TX_PHY_HDMI_POWERUP_CTL_BIAS_PWRUP |
		   VC6_HDMI_TX_PHY_HDMI_POWERUP_CTL_TX_CK_PWRUP |
		   VC6_HDMI_TX_PHY_HDMI_POWERUP_CTL_TX_2_PWRUP |
		   VC6_HDMI_TX_PHY_HDMI_POWERUP_CTL_TX_1_PWRUP |
		   VC6_HDMI_TX_PHY_HDMI_POWERUP_CTL_TX_0_PWRUP);

	HDMI_WRITE(HDMI_TX_PHY_PLL_POWERUP_CTL,
		   VC6_HDMI_TX_PHY_PLL_POWERUP_CTL_PLL_PWRUP);

	HDMI_WRITE(HDMI_TX_PHY_PLL_RESET_CTL,
		   HDMI_READ(HDMI_TX_PHY_PLL_RESET_CTL) &
		   ~VC6_HDMI_TX_PHY_PLL_RESET_CTL_PLL_RESETB);

	HDMI_WRITE(HDMI_TX_PHY_PLL_RESET_CTL,
		   HDMI_READ(HDMI_TX_PHY_PLL_RESET_CTL) |
		   VC6_HDMI_TX_PHY_PLL_RESET_CTL_PLL_RESETB);

	spin_unlock_irqrestore(&vc4_hdmi->hw_lock, flags);
}

void vc6_hdmi_phy_disable(struct vc4_hdmi *vc4_hdmi)
{
	unsigned long flags;

	spin_lock_irqsave(&vc4_hdmi->hw_lock, flags);
	vc6_hdmi_reset_phy(vc4_hdmi);
	spin_unlock_irqrestore(&vc4_hdmi->hw_lock, flags);
}
