// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/sched/clock.h>

#include "clk-alpha-pll.h"
#include "clk-debug.h"
#include "common.h"

#define PLL_MODE(p)		((p)->offset + 0x0)
# define PLL_OUTCTRL		BIT(0)
# define PLL_BYPASSNL		BIT(1)
# define PLL_RESET_N		BIT(2)
# define PLL_OFFLINE_REQ	BIT(7)
# define PLL_LOCK_COUNT_SHIFT	8
# define PLL_LOCK_COUNT_MASK	0x3f
# define PLL_BIAS_COUNT_SHIFT	14
# define PLL_BIAS_COUNT_MASK	0x3f
#define PLL_LATCH_INTERFACE     BIT(11)
# define PLL_VOTE_FSM_ENA	BIT(20)
# define PLL_FSM_ENA		BIT(20)
# define PLL_VOTE_FSM_RESET	BIT(21)
# define PLL_UPDATE		BIT(22)
# define PLL_UPDATE_BYPASS	BIT(23)
# define PLL_FSM_LEGACY_MODE	BIT(24)
# define PLL_OFFLINE_ACK	BIT(28)
# define ALPHA_PLL_ACK_LATCH	BIT(29)
# define PLL_ACTIVE_FLAG	BIT(30)
# define PLL_LOCK_DET		BIT(31)

#define PLL_L_VAL(p)		((p)->offset + (p)->regs[PLL_OFF_L_VAL])
#define PLL_CAL_L_VAL(p)	((p)->offset + (p)->regs[PLL_OFF_CAL_L_VAL])
#define PLL_ALPHA_VAL(p)	((p)->offset + (p)->regs[PLL_OFF_ALPHA_VAL])
#define PLL_ALPHA_VAL_U(p)	((p)->offset + (p)->regs[PLL_OFF_ALPHA_VAL_U])

#define PLL_USER_CTL(p)		((p)->offset + (p)->regs[PLL_OFF_USER_CTL])
# define PLL_POST_DIV_SHIFT	8
# define PLL_POST_DIV_MASK(p)	GENMASK((p)->width - 1, 0)
# define PLL_ALPHA_EN		BIT(24)
# define PLL_ALPHA_MODE		BIT(25)
# define PLL_VCO_SHIFT		20
# define PLL_VCO_MASK		0x3

#define PLL_USER_CTL_U(p)	((p)->offset + (p)->regs[PLL_OFF_USER_CTL_U])
#define PLL_USER_CTL_U1(p)	((p)->offset + (p)->regs[PLL_OFF_USER_CTL_U1])

#define PLL_CONFIG_CTL(p)	((p)->offset + (p)->regs[PLL_OFF_CONFIG_CTL])
#define PLL_CONFIG_CTL_U(p)	((p)->offset + (p)->regs[PLL_OFF_CONFIG_CTL_U])
#define PLL_CONFIG_CTL_U1(p)	((p)->offset + (p)->regs[PLL_OFF_CONFIG_CTL_U1])
#define PLL_TEST_CTL(p)		((p)->offset + (p)->regs[PLL_OFF_TEST_CTL])
#define PLL_TEST_CTL_U(p)	((p)->offset + (p)->regs[PLL_OFF_TEST_CTL_U])
#define PLL_TEST_CTL_U1(p)     ((p)->offset + (p)->regs[PLL_OFF_TEST_CTL_U1])
#define PLL_TEST_CTL_U2(p)     ((p)->offset + (p)->regs[PLL_OFF_TEST_CTL_U2])
#define PLL_STATUS(p)		((p)->offset + (p)->regs[PLL_OFF_STATUS])
#define PLL_OPMODE(p)		((p)->offset + (p)->regs[PLL_OFF_OPMODE])
#define PLL_FRAC(p)		((p)->offset + (p)->regs[PLL_OFF_FRAC])

const u8 clk_alpha_pll_regs[][PLL_OFF_MAX_REGS] = {
	[CLK_ALPHA_PLL_TYPE_DEFAULT] =  {
		[PLL_OFF_L_VAL] = 0x04,
		[PLL_OFF_ALPHA_VAL] = 0x08,
		[PLL_OFF_ALPHA_VAL_U] = 0x0c,
		[PLL_OFF_USER_CTL] = 0x10,
		[PLL_OFF_USER_CTL_U] = 0x14,
		[PLL_OFF_CONFIG_CTL] = 0x18,
		[PLL_OFF_TEST_CTL] = 0x1c,
		[PLL_OFF_TEST_CTL_U] = 0x20,
		[PLL_OFF_STATUS] = 0x24,
	},
	[CLK_ALPHA_PLL_TYPE_HUAYRA] =  {
		[PLL_OFF_L_VAL] = 0x04,
		[PLL_OFF_ALPHA_VAL] = 0x08,
		[PLL_OFF_USER_CTL] = 0x10,
		[PLL_OFF_CONFIG_CTL] = 0x14,
		[PLL_OFF_CONFIG_CTL_U] = 0x18,
		[PLL_OFF_TEST_CTL] = 0x1c,
		[PLL_OFF_TEST_CTL_U] = 0x20,
		[PLL_OFF_STATUS] = 0x24,
	},
	[CLK_ALPHA_PLL_TYPE_BRAMMO] =  {
		[PLL_OFF_L_VAL] = 0x04,
		[PLL_OFF_ALPHA_VAL] = 0x08,
		[PLL_OFF_ALPHA_VAL_U] = 0x0c,
		[PLL_OFF_USER_CTL] = 0x10,
		[PLL_OFF_CONFIG_CTL] = 0x18,
		[PLL_OFF_TEST_CTL] = 0x1c,
		[PLL_OFF_STATUS] = 0x24,
	},
	[CLK_ALPHA_PLL_TYPE_FABIA] =  {
		[PLL_OFF_L_VAL] = 0x04,
		[PLL_OFF_USER_CTL] = 0x0c,
		[PLL_OFF_USER_CTL_U] = 0x10,
		[PLL_OFF_CONFIG_CTL] = 0x14,
		[PLL_OFF_CONFIG_CTL_U] = 0x18,
		[PLL_OFF_TEST_CTL] = 0x1c,
		[PLL_OFF_TEST_CTL_U] = 0x20,
		[PLL_OFF_STATUS] = 0x24,
		[PLL_OFF_OPMODE] = 0x2c,
		[PLL_OFF_FRAC] = 0x38,
	},
	[CLK_ALPHA_PLL_TYPE_TRION] = {
		[PLL_OFF_L_VAL] = 0x04,
		[PLL_OFF_CAL_L_VAL] = 0x08,
		[PLL_OFF_USER_CTL] = 0x0c,
		[PLL_OFF_USER_CTL_U] = 0x10,
		[PLL_OFF_USER_CTL_U1] = 0x14,
		[PLL_OFF_CONFIG_CTL] = 0x18,
		[PLL_OFF_CONFIG_CTL_U] = 0x1c,
		[PLL_OFF_CONFIG_CTL_U1] = 0x20,
		[PLL_OFF_TEST_CTL] = 0x24,
		[PLL_OFF_TEST_CTL_U] = 0x28,
		[PLL_OFF_TEST_CTL_U1] = 0x2c,
		[PLL_OFF_STATUS] = 0x30,
		[PLL_OFF_OPMODE] = 0x38,
		[PLL_OFF_ALPHA_VAL] = 0x40,
		[PLL_OFF_SSC_DELTA_ALPHA] = 0x48,
		[PLL_OFF_SSC_NUM_STEPS] = 0x4C,
		[PLL_OFF_SSC_UPDATE_RATE] = 0x50,
	},
	[CLK_ALPHA_PLL_TYPE_AGERA] =  {
		[PLL_OFF_L_VAL] = 0x04,
		[PLL_OFF_ALPHA_VAL] = 0x08,
		[PLL_OFF_USER_CTL] = 0x0c,
		[PLL_OFF_CONFIG_CTL] = 0x10,
		[PLL_OFF_CONFIG_CTL_U] = 0x14,
		[PLL_OFF_TEST_CTL] = 0x18,
		[PLL_OFF_TEST_CTL_U] = 0x1c,
		[PLL_OFF_STATUS] = 0x2c,
	},
	[CLK_ALPHA_PLL_TYPE_ZONDA] =  {
		[PLL_OFF_L_VAL] = 0x04,
		[PLL_OFF_ALPHA_VAL] = 0x08,
		[PLL_OFF_USER_CTL] = 0x0c,
		[PLL_OFF_CONFIG_CTL] = 0x10,
		[PLL_OFF_CONFIG_CTL_U] = 0x14,
		[PLL_OFF_CONFIG_CTL_U1] = 0x18,
		[PLL_OFF_TEST_CTL] = 0x1c,
		[PLL_OFF_TEST_CTL_U] = 0x20,
		[PLL_OFF_TEST_CTL_U1] = 0x24,
		[PLL_OFF_OPMODE] = 0x28,
		[PLL_OFF_STATUS] = 0x38,
	},
	[CLK_ALPHA_PLL_TYPE_ZONDA_5LPE] = {
		[PLL_OFF_L_VAL] = 0x04,
		[PLL_OFF_ALPHA_VAL] = 0x08,
		[PLL_OFF_USER_CTL] = 0x0c,
		[PLL_OFF_CONFIG_CTL] = 0x10,
		[PLL_OFF_CONFIG_CTL_U] = 0x14,
		[PLL_OFF_CONFIG_CTL_U1] = 0x18,
		[PLL_OFF_TEST_CTL] = 0x1c,
		[PLL_OFF_TEST_CTL_U] = 0x20,
		[PLL_OFF_TEST_CTL_U1] = 0x24,
		[PLL_OFF_OPMODE] = 0x28,
		[PLL_OFF_STATUS] = 0x38,
	},
	[CLK_ALPHA_PLL_TYPE_REGERA] =  {
		[PLL_OFF_L_VAL] = 0x04,
		[PLL_OFF_ALPHA_VAL] = 0x08,
		[PLL_OFF_USER_CTL] = 0x0c,
		[PLL_OFF_CONFIG_CTL] = 0x10,
		[PLL_OFF_CONFIG_CTL_U] = 0x14,
		[PLL_OFF_CONFIG_CTL_U1] = 0x18,
		[PLL_OFF_TEST_CTL] = 0x1c,
		[PLL_OFF_TEST_CTL_U] = 0x20,
		[PLL_OFF_TEST_CTL_U1] = 0x24,
		[PLL_OFF_OPMODE] = 0x28,
		[PLL_OFF_STATUS] = 0x38,
	},
	[CLK_ALPHA_PLL_TYPE_LUCID_EVO] = {
		[PLL_OFF_OPMODE] = 0x04,
		[PLL_OFF_STATUS] = 0x0c,
		[PLL_OFF_L_VAL] = 0x10,
		[PLL_OFF_ALPHA_VAL] = 0x14,
		[PLL_OFF_USER_CTL] = 0x18,
		[PLL_OFF_USER_CTL_U] = 0x1c,
		[PLL_OFF_CONFIG_CTL] = 0x20,
		[PLL_OFF_CONFIG_CTL_U] = 0x24,
		[PLL_OFF_CONFIG_CTL_U1] = 0x28,
		[PLL_OFF_TEST_CTL] = 0x2c,
		[PLL_OFF_TEST_CTL_U] = 0x30,
		[PLL_OFF_TEST_CTL_U1] = 0x34,
	},
	[CLK_ALPHA_PLL_TYPE_RIVIAN_EVO] = {
		[PLL_OFF_OPMODE] = 0x04,
		[PLL_OFF_STATUS] = 0x0c,
		[PLL_OFF_L_VAL] = 0x10,
		[PLL_OFF_USER_CTL] = 0x14,
		[PLL_OFF_USER_CTL_U] = 0x18,
		[PLL_OFF_CONFIG_CTL] = 0x1c,
		[PLL_OFF_CONFIG_CTL_U] = 0x20,
		[PLL_OFF_CONFIG_CTL_U1] = 0x24,
		[PLL_OFF_TEST_CTL] = 0x28,
		[PLL_OFF_TEST_CTL_U] = 0x2c,
	},
	[CLK_ALPHA_PLL_TYPE_LUCID_OLE] = {
		[PLL_OFF_OPMODE] = 0x04,
		[PLL_OFF_STATE] = 0x08,
		[PLL_OFF_STATUS] = 0x0c,
		[PLL_OFF_L_VAL] = 0x10,
		[PLL_OFF_ALPHA_VAL] = 0x14,
		[PLL_OFF_USER_CTL] = 0x18,
		[PLL_OFF_USER_CTL_U] = 0x1c,
		[PLL_OFF_CONFIG_CTL] = 0x20,
		[PLL_OFF_CONFIG_CTL_U] = 0x24,
		[PLL_OFF_CONFIG_CTL_U1] = 0x28,
		[PLL_OFF_TEST_CTL] = 0x2c,
		[PLL_OFF_TEST_CTL_U] = 0x30,
		[PLL_OFF_TEST_CTL_U1] = 0x34,
		[PLL_OFF_TEST_CTL_U2] = 0x38,
	},
	[CLK_ALPHA_PLL_TYPE_DEFAULT_EVO] =  {
		[PLL_OFF_L_VAL] = 0x04,
		[PLL_OFF_ALPHA_VAL] = 0x08,
		[PLL_OFF_ALPHA_VAL_U] = 0x0c,
		[PLL_OFF_TEST_CTL] = 0x10,
		[PLL_OFF_TEST_CTL_U] = 0x14,
		[PLL_OFF_USER_CTL] = 0x18,
		[PLL_OFF_USER_CTL_U] = 0x1c,
		[PLL_OFF_CONFIG_CTL] = 0x20,
		[PLL_OFF_STATUS] = 0x24,
	},
	[CLK_ALPHA_PLL_TYPE_BRAMMO_EVO] =  {
		[PLL_OFF_L_VAL] = 0x04,
		[PLL_OFF_ALPHA_VAL] = 0x08,
		[PLL_OFF_ALPHA_VAL_U] = 0x0c,
		[PLL_OFF_TEST_CTL] = 0x10,
		[PLL_OFF_TEST_CTL_U] = 0x14,
		[PLL_OFF_USER_CTL] = 0x18,
		[PLL_OFF_CONFIG_CTL] = 0x1C,
		[PLL_OFF_STATUS] = 0x20,
	},
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_regs);

/*
 * Even though 40 bits are present, use only 32 for ease of calculation.
 */
#define ALPHA_REG_BITWIDTH	40
#define ALPHA_REG_16BIT_WIDTH	16
#define ALPHA_BITWIDTH		32U
#define ALPHA_SHIFT(w)		min(w, ALPHA_BITWIDTH)

#define PLL_HUAYRA_M_WIDTH		8
#define PLL_HUAYRA_M_SHIFT		8
#define PLL_HUAYRA_M_MASK		0xff
#define PLL_HUAYRA_N_SHIFT		0
#define PLL_HUAYRA_N_MASK		0xff
#define PLL_HUAYRA_ALPHA_WIDTH		16

#define PLL_STANDBY		0x0
#define PLL_RUN			0x1
#define PLL_OUT_MASK		0x7
#define PLL_RATE_MARGIN		500

/* TRION PLL specific settings and offsets */
#define TRION_PLL_CAL_VAL	0x44
#define TRION_PCAL_DONE		BIT(26)

/* LUCID PLL specific settings and offsets */
#define LUCID_PCAL_DONE		BIT(27)

/* LUCID 5LPE PLL specific settings and offsets */
#define LUCID_5LPE_PCAL_DONE		BIT(11)
#define LUCID_5LPE_ALPHA_PLL_ACK_LATCH	BIT(13)
#define LUCID_5LPE_PLL_LATCH_INPUT	BIT(14)
#define LUCID_5LPE_ENABLE_VOTE_RUN	BIT(21)
#define LUCID_EVO_PCAL_NOT_DONE		BIT(8)
#define LUCID_EVO_ENABLE_VOTE_RUN	BIT(25)
#define LUCID_EVO_PLL_L_VAL_MASK	GENMASK(15, 0)
#define LUCID_EVO_PLL_CAL_L_VAL_MASK	GENMASK(31, 16)
#define LUCID_EVO_PLL_CAL_L_VAL_SHIFT	16
#define LUCID_OLE_PROCESS_CAL_L_VAL_MASK	GENMASK(23, 16)
#define LUCID_OLE_PROCESS_CAL_L_VAL_SHIFT	16
#define LUCID_OLE_RINGOSC_CAL_L_VAL_MASK	GENMASK(31, 24)
#define LUCID_OLE_RINGOSC_CAL_L_VAL_SHIFT	24

/* LUCID EVO PLL specific settings and offsets */
#define LUCID_EVO_PCAL_NOT_DONE		BIT(8)
#define LUCID_EVO_ENABLE_VOTE_RUN       BIT(25)
#define LUCID_EVO_PLL_L_VAL_MASK        GENMASK(15, 0)
#define LUCID_EVO_PLL_CAL_L_VAL_SHIFT	16

/* ZONDA PLL specific */
#define ZONDA_PLL_OUT_MASK	0xf
#define ZONDA_STAY_IN_CFA	BIT(16)
#define ZONDA_PLL_FREQ_LOCK_DET	BIT(29)
#define ZONDA_5LPE_ENABLE_VOTE_RUN	BIT(21)

#define pll_alpha_width(p)					\
		((PLL_ALPHA_VAL_U(p) - PLL_ALPHA_VAL(p) == 4) ?	\
				 ALPHA_REG_BITWIDTH : ALPHA_REG_16BIT_WIDTH)

#define pll_has_64bit_config(p)	((PLL_CONFIG_CTL_U(p) - PLL_CONFIG_CTL(p)) == 4)

#define to_clk_alpha_pll(_hw) container_of(to_clk_regmap(_hw), \
					   struct clk_alpha_pll, clkr)

#define to_clk_alpha_pll_postdiv(_hw) container_of(to_clk_regmap(_hw), \
					   struct clk_alpha_pll_postdiv, clkr)

static int wait_for_pll(struct clk_alpha_pll *pll, u32 mask, bool inverse,
			const char *action)
{
	u32 val;
	int count;
	int ret;
	u64 time;

	ret = regmap_read(pll->clkr.regmap, PLL_MODE(pll), &val);
	if (ret)
		return ret;

	time = sched_clock();

	for (count = 200; count > 0; count--) {
		ret = regmap_read(pll->clkr.regmap, PLL_MODE(pll), &val);
		if (ret)
			return ret;
		if (inverse && !(val & mask))
			return 0;
		else if ((val & mask) == mask)
			return 0;

		udelay(1);
	}
	time = sched_clock() - time;

	pr_err("PLL lock bit detection total wait time: %lld ns\n", time);

	WARN_CLK(&pll->clkr.hw, 1, "pll failed to %s!\n", action);
	return -ETIMEDOUT;
}

#define wait_for_pll_enable_active(pll) \
	wait_for_pll(pll, PLL_ACTIVE_FLAG, 0, "enable")

#define wait_for_pll_enable_lock(pll) \
	wait_for_pll(pll, PLL_LOCK_DET, 0, "enable")

#define wait_for_zonda_pll_freq_lock(pll) \
	wait_for_pll(pll, ZONDA_PLL_FREQ_LOCK_DET, 0, "freq enable")

#define wait_for_pll_disable(pll) \
	wait_for_pll(pll, PLL_ACTIVE_FLAG, 1, "disable")

#define wait_for_pll_offline(pll) \
	wait_for_pll(pll, PLL_OFFLINE_ACK, 0, "offline")

#define wait_for_pll_update(pll) \
	wait_for_pll(pll, PLL_UPDATE, 1, "update")

#define wait_for_pll_update_ack_set(pll) \
	wait_for_pll(pll, ALPHA_PLL_ACK_LATCH, 0, "update_ack_set")

#define wait_for_pll_update_ack_clear(pll) \
	wait_for_pll(pll, ALPHA_PLL_ACK_LATCH, 1, "update_ack_clear")

static void clk_alpha_pll_write_config(struct regmap *regmap, unsigned int reg,
					unsigned int val)
{
	if (val)
		regmap_write(regmap, reg, val);
}

void clk_alpha_pll_configure(struct clk_alpha_pll *pll, struct regmap *regmap,
			     const struct alpha_pll_config *config)
{
	u32 val, mask;

	if (config->l)
		regmap_write(regmap, PLL_L_VAL(pll), config->l);
	if (config->alpha)
		regmap_write(regmap, PLL_ALPHA_VAL(pll), config->alpha);
	if (config->config_ctl_val)
		regmap_write(regmap, PLL_CONFIG_CTL(pll),
				config->config_ctl_val);

	if (pll_has_64bit_config(pll))
		regmap_write(regmap, PLL_CONFIG_CTL_U(pll),
			     config->config_ctl_hi_val);

	if (pll_alpha_width(pll) > 32)
		regmap_write(regmap, PLL_ALPHA_VAL_U(pll), config->alpha_hi);

	if (config->main_output_mask || config->aux_output_mask ||
		config->aux2_output_mask || config->early_output_mask ||
		config->pre_div_val || config->vco_val ||
		config->alpha_en_mask) {
		val = config->main_output_mask;
		val |= config->aux_output_mask;
		val |= config->aux2_output_mask;
		val |= config->early_output_mask;
		val |= config->pre_div_val;
		val |= config->vco_val;
		val |= config->alpha_en_mask;

		mask = config->main_output_mask;
		mask |= config->aux_output_mask;
		mask |= config->aux2_output_mask;
		mask |= config->early_output_mask;
		mask |= config->pre_div_mask;
		mask |= config->vco_mask;
		mask |= config->alpha_en_mask;

		regmap_update_bits(regmap, PLL_USER_CTL(pll), mask, val);
	}

	if (config->post_div_mask) {
		mask = config->post_div_mask;
		val = config->post_div_val;
		regmap_update_bits(regmap, PLL_USER_CTL(pll), mask, val);
	}

	 /* Do not bypass the latch interface */
	if (pll->flags & SUPPORTS_SLEW)
		regmap_update_bits(regmap, PLL_USER_CTL_U(pll),
		PLL_LATCH_INTERFACE, (u32)~PLL_LATCH_INTERFACE);

	if (pll->flags & SUPPORTS_DYNAMIC_UPDATE) {
		regmap_update_bits(regmap, PLL_MODE(pll),
				PLL_UPDATE_BYPASS,
				PLL_UPDATE_BYPASS);
	}

	if (config->test_ctl_mask) {
		mask = config->test_ctl_mask;
		val = config->test_ctl_val;
		regmap_update_bits(regmap, PLL_TEST_CTL(pll), mask, val);
	}

	if (config->test_ctl_hi_mask) {
		mask = config->test_ctl_hi_mask;
		val = config->test_ctl_hi_val;
		regmap_update_bits(regmap, PLL_TEST_CTL_U(pll), mask, val);
	}
	if (pll->flags & SUPPORTS_DYNAMIC_UPDATE)
		regmap_update_bits(regmap, PLL_MODE(pll), PLL_UPDATE_BYPASS,
					PLL_UPDATE_BYPASS);

	if (pll->flags & SUPPORTS_FSM_MODE)
		qcom_pll_set_fsm_mode(regmap, PLL_MODE(pll), 6, 0);
}
EXPORT_SYMBOL_GPL(clk_alpha_pll_configure);

static int clk_alpha_pll_hwfsm_enable(struct clk_hw *hw)
{
	int ret;
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 val;

	ret = regmap_read(pll->clkr.regmap, PLL_MODE(pll), &val);
	if (ret)
		return ret;

	val |= PLL_FSM_ENA;

	if (pll->flags & SUPPORTS_OFFLINE_REQ)
		val &= ~PLL_OFFLINE_REQ;

	ret = regmap_write(pll->clkr.regmap, PLL_MODE(pll), val);
	if (ret)
		return ret;

	/* Make sure enable request goes through before waiting for update */
	mb();

	return wait_for_pll_enable_active(pll);
}

static void clk_alpha_pll_hwfsm_disable(struct clk_hw *hw)
{
	int ret;
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 val;

	ret = regmap_read(pll->clkr.regmap, PLL_MODE(pll), &val);
	if (ret)
		return;

	if (pll->flags & SUPPORTS_OFFLINE_REQ) {
		ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
					 PLL_OFFLINE_REQ, PLL_OFFLINE_REQ);
		if (ret)
			return;

		ret = wait_for_pll_offline(pll);
		if (ret)
			return;
	}

	/* Disable hwfsm */
	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
				 PLL_FSM_ENA, 0);
	if (ret)
		return;

	wait_for_pll_disable(pll);
}

static int pll_is_enabled(struct clk_hw *hw, u32 mask)
{
	int ret;
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 val;

	ret = regmap_read(pll->clkr.regmap, PLL_MODE(pll), &val);
	if (ret)
		return ret;

	return !!(val & mask);
}

static int clk_alpha_pll_hwfsm_is_enabled(struct clk_hw *hw)
{
	return pll_is_enabled(hw, PLL_ACTIVE_FLAG);
}

static int clk_alpha_pll_is_enabled(struct clk_hw *hw)
{
	return pll_is_enabled(hw, PLL_LOCK_DET);
}

static int clk_alpha_pll_enable(struct clk_hw *hw)
{
	int ret;
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 val, mask;

	mask = PLL_OUTCTRL | PLL_RESET_N | PLL_BYPASSNL;
	ret = regmap_read(pll->clkr.regmap, PLL_MODE(pll), &val);
	if (ret)
		return ret;

	/* If in FSM mode, just vote for it */
	if (val & PLL_VOTE_FSM_ENA) {
		ret = clk_enable_regmap(hw);
		if (ret)
			return ret;
		return wait_for_pll_enable_active(pll);
	}

	/* Skip if already enabled */
	if ((val & mask) == mask)
		return 0;

	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
				 PLL_BYPASSNL, PLL_BYPASSNL);
	if (ret)
		return ret;

	/*
	 * H/W requires a 5us delay between disabling the bypass and
	 * de-asserting the reset.
	 */
	mb();
	udelay(5);

	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
				 PLL_RESET_N, PLL_RESET_N);
	if (ret)
		return ret;

	ret = wait_for_pll_enable_lock(pll);
	if (ret)
		return ret;

	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
				 PLL_OUTCTRL, PLL_OUTCTRL);

	/* Ensure that the write above goes through before returning. */
	mb();
	return ret;
}

static void clk_alpha_pll_disable(struct clk_hw *hw)
{
	int ret;
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 val, mask;

	ret = regmap_read(pll->clkr.regmap, PLL_MODE(pll), &val);
	if (ret)
		return;

	/* If in FSM mode, just unvote it */
	if (val & PLL_VOTE_FSM_ENA) {
		clk_disable_regmap(hw);
		return;
	}

	mask = PLL_OUTCTRL;
	regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll), mask, 0);

	/* Delay of 2 output clock ticks required until output is disabled */
	mb();
	udelay(1);

	mask = PLL_RESET_N | PLL_BYPASSNL;
	regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll), mask, 0);
}

static unsigned long
alpha_pll_calc_rate(u64 prate, u32 l, u32 a, u32 alpha_width)
{
	unsigned long rate;

	rate = (prate * l) + ((prate * a) >> ALPHA_SHIFT(alpha_width));

	/*
	 * PLLs with narrow ALPHA (e.g. 16 bits) aren't able to hit all
	 * frequencies precisely and may be under by a few hundred Hz. Round to
	 * the nearest KHz to avoid reporting strange, slightly lower than
	 * requested frequencies. The small delta has no functional impact.
	 */
	return roundup(rate, 1000);
}

static unsigned long
alpha_pll_round_rate(unsigned long rate, unsigned long prate, u32 *l, u64 *a,
		     u32 alpha_width)
{
	u64 remainder;
	u64 quotient;

	quotient = rate;
	remainder = do_div(quotient, prate);
	*l = quotient;

	if (!remainder) {
		*a = 0;
		return rate;
	}

	/* Upper ALPHA_BITWIDTH bits of Alpha */
	quotient = remainder << ALPHA_SHIFT(alpha_width);

	do_div(quotient, prate);

	*a = quotient;
	return alpha_pll_calc_rate(prate, *l, *a, alpha_width);
}

static const struct pll_vco *
alpha_pll_find_vco(const struct clk_alpha_pll *pll, unsigned long rate)
{
	const struct pll_vco *v = pll->vco_table;
	const struct pll_vco *end = v + pll->num_vco;

	for (; v < end; v++)
		if (rate >= v->min_freq && rate <= v->max_freq)
			return v;

	return NULL;
}

static unsigned long
clk_alpha_pll_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	u32 l, low, high, ctl;
	u64 a = 0, prate = parent_rate;
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 alpha_width = pll_alpha_width(pll);

	regmap_read(pll->clkr.regmap, PLL_L_VAL(pll), &l);

	regmap_read(pll->clkr.regmap, PLL_USER_CTL(pll), &ctl);
	if (ctl & PLL_ALPHA_EN) {
		regmap_read(pll->clkr.regmap, PLL_ALPHA_VAL(pll), &low);
		if (alpha_width > 32) {
			regmap_read(pll->clkr.regmap, PLL_ALPHA_VAL_U(pll),
				    &high);
			a = (u64)high << 32 | low;
		} else {
			a = low & GENMASK(alpha_width - 1, 0);
		}

		if (alpha_width > ALPHA_BITWIDTH)
			a >>= alpha_width - ALPHA_BITWIDTH;
	}

	return alpha_pll_calc_rate(prate, l, a, alpha_width);
}

static int clk_alpha_pll_dynamic_update(struct clk_alpha_pll *pll)
{
	int ret;

	/* Latch the input to the PLL */
	regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
				PLL_UPDATE, PLL_UPDATE);

	/* Wait for 2 reference cycle before checking ACK bit */
	udelay(1);

	ret = wait_for_pll_update_ack_set(pll);
	if (ret)
		return ret;

	/* Return latch input to 0 */
	regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
				PLL_UPDATE, (u32)~PLL_UPDATE);

	ret = wait_for_pll_enable_lock(pll);
	if (ret)
		return ret;

	return 0;
}

static const struct pll_vco_data
	*find_vco_data(const struct pll_vco_data *data,
			unsigned long rate, size_t size)
{
	int i;

	if (!data)
		return NULL;

	for (i = 0; i < size; i++) {
		if (rate == data[i].freq)
			return &data[i];
	}

	return &data[i - 1];
}

static int __clk_alpha_pll_update_latch(struct clk_alpha_pll *pll)
{
	int ret;
	u32 mode;

	regmap_read(pll->clkr.regmap, PLL_MODE(pll), &mode);

	/* Latch the input to the PLL */
	regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll), PLL_UPDATE,
			   PLL_UPDATE);

	/* Wait for 2 reference cycle before checking ACK bit */
	udelay(1);

	/*
	 * PLL will latch the new L, Alpha and freq control word.
	 * PLL will respond by raising PLL_ACK_LATCH output when new programming
	 * has been latched in and PLL is being updated. When
	 * UPDATE_LOGIC_BYPASS bit is not set, PLL_UPDATE will be cleared
	 * automatically by hardware when PLL_ACK_LATCH is asserted by PLL.
	 */
	if (mode & PLL_UPDATE_BYPASS) {
		ret = wait_for_pll_update_ack_set(pll);
		if (ret)
			return ret;

		regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll), PLL_UPDATE, 0);
	} else {
		ret = wait_for_pll_update(pll);
		if (ret)
			return ret;
	}

	if (pll->flags & SUPPORTS_DYNAMIC_UPDATE)
		ret = wait_for_pll_enable_lock(pll);
	else
		ret = wait_for_pll_update_ack_clear(pll);
	if (ret)
		return ret;

	/* Wait for PLL output to stabilize */
	udelay(10);

	return 0;
}

static int clk_alpha_pll_update_latch(struct clk_alpha_pll *pll,
				      int (*is_enabled)(struct clk_hw *))
{
	if (!is_enabled(&pll->clkr.hw) ||
	    !(pll->flags & SUPPORTS_DYNAMIC_UPDATE))
		return 0;

	return __clk_alpha_pll_update_latch(pll);
}

static int __clk_alpha_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long prate,
				    int (*is_enabled)(struct clk_hw *))
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	struct clk_alpha_pll_postdiv *pll_postdiv =
		to_clk_alpha_pll_postdiv(hw);
	const struct pll_vco *vco;
	const struct pll_vco_data *data;
	u32 l, alpha_width = pll_alpha_width(pll);
	u64 a;
	unsigned long rrate;

	rrate = alpha_pll_round_rate(rate, prate, &l, &a, alpha_width);
	if (rrate != rate) {
		pr_err("alpha_pll: Call clk_set_rate with rounded rates!\n");
		return -EINVAL;
	}

	vco = alpha_pll_find_vco(pll, rate);
	if (pll->vco_table && !vco) {
		pr_err("%s: alpha pll not in a valid vco range\n",
		       clk_hw_get_name(hw));
		return -EINVAL;
	}

	/*
	 * For PLLs that do not support dynamic programming (dynamic_update
	 * is not set), ensure PLL is off before changing rate. For
	 * optimization reasons, assume no downstream clock is actively
	 * using it.
	 */
	if (is_enabled(&pll->clkr.hw) &&
	    !(pll->flags & SUPPORTS_DYNAMIC_UPDATE))
		hw->init->ops->disable(hw);

	regmap_write(pll->clkr.regmap, PLL_L_VAL(pll), l);

	if (alpha_width > ALPHA_BITWIDTH)
		a <<= alpha_width - ALPHA_BITWIDTH;

	if (alpha_width > 32)
		regmap_write(pll->clkr.regmap, PLL_ALPHA_VAL_U(pll), a >> 32);

	regmap_write(pll->clkr.regmap, PLL_ALPHA_VAL(pll), a);

	if (vco) {
		regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
				   PLL_VCO_MASK << PLL_VCO_SHIFT,
				   vco->val << PLL_VCO_SHIFT);
	}

	data = find_vco_data(pll->vco_data, rate, pll->num_vco_data);
	if (data) {
		if (data->freq == rate)
			regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
				PLL_POST_DIV_MASK(pll_postdiv)
				<< PLL_POST_DIV_SHIFT,
				data->post_div_val << PLL_POST_DIV_SHIFT);
		else
			regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
					PLL_POST_DIV_MASK(pll_postdiv)
					<< PLL_POST_DIV_SHIFT,
					0x0 << PLL_VCO_SHIFT);
	}

	regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
			   PLL_ALPHA_EN, PLL_ALPHA_EN);

	if (is_enabled(&pll->clkr.hw) &&
		(pll->flags & SUPPORTS_DYNAMIC_UPDATE))
		clk_alpha_pll_dynamic_update(pll);

	if (is_enabled(&pll->clkr.hw) &&
		!(pll->flags & SUPPORTS_DYNAMIC_UPDATE))
		hw->init->ops->enable(hw);

	return clk_alpha_pll_update_latch(pll, is_enabled);
}

static int clk_alpha_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long prate)
{
	return __clk_alpha_pll_set_rate(hw, rate, prate,
					clk_alpha_pll_is_enabled);
}

static int clk_alpha_pll_hwfsm_set_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long prate)
{
	return __clk_alpha_pll_set_rate(hw, rate, prate,
					clk_alpha_pll_hwfsm_is_enabled);
}

static long clk_alpha_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *prate)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 l, alpha_width = pll_alpha_width(pll);
	u64 a;
	unsigned long min_freq, max_freq;

	if (rate < pll->min_supported_freq)
		return pll->min_supported_freq;

	rate = alpha_pll_round_rate(rate, *prate, &l, &a, alpha_width);
	if (!pll->vco_table || alpha_pll_find_vco(pll, rate))
		return rate;

	min_freq = pll->vco_table[0].min_freq;
	max_freq = pll->vco_table[pll->num_vco - 1].max_freq;

	return clamp(rate, min_freq, max_freq);
}

static unsigned long
alpha_huayra_pll_calc_rate(u64 prate, u32 l, u32 a)
{
	/*
	 * a contains 16 bit alpha_val in two’s complement number in the range
	 * of [-0.5, 0.5).
	 */
	if (a >= BIT(PLL_HUAYRA_ALPHA_WIDTH - 1))
		l -= 1;

	return alpha_pll_calc_rate(prate, l, a, PLL_HUAYRA_ALPHA_WIDTH);
}

static unsigned long
alpha_huayra_pll_round_rate(unsigned long rate, unsigned long prate,
			    u32 *l, u32 *a)
{
	u64 remainder;
	u64 quotient;

	quotient = rate;
	remainder = do_div(quotient, prate);
	*l = quotient;

	if (!remainder) {
		*a = 0;
		return rate;
	}

	quotient = remainder << PLL_HUAYRA_ALPHA_WIDTH;
	do_div(quotient, prate);

	/*
	 * alpha_val should be in two’s complement number in the range
	 * of [-0.5, 0.5) so if quotient >= 0.5 then increment the l value
	 * since alpha value will be subtracted in this case.
	 */
	if (quotient >= BIT(PLL_HUAYRA_ALPHA_WIDTH - 1))
		*l += 1;

	*a = quotient;
	return alpha_huayra_pll_calc_rate(prate, *l, *a);
}

static unsigned long
alpha_pll_huayra_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	u64 rate = parent_rate, tmp;
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 l, alpha = 0, ctl, alpha_m, alpha_n;

	regmap_read(pll->clkr.regmap, PLL_L_VAL(pll), &l);
	regmap_read(pll->clkr.regmap, PLL_USER_CTL(pll), &ctl);

	if (ctl & PLL_ALPHA_EN) {
		regmap_read(pll->clkr.regmap, PLL_ALPHA_VAL(pll), &alpha);
		/*
		 * Depending upon alpha_mode, it can be treated as M/N value or
		 * as a two’s complement number. When alpha_mode=1,
		 * pll_alpha_val<15:8>=M and pll_apla_val<7:0>=N
		 *
		 *		Fout=FIN*(L+(M/N))
		 *
		 * M is a signed number (-128 to 127) and N is unsigned
		 * (0 to 255). M/N has to be within +/-0.5.
		 *
		 * When alpha_mode=0, it is a two’s complement number in the
		 * range [-0.5, 0.5).
		 *
		 *		Fout=FIN*(L+(alpha_val)/2^16)
		 *
		 * where alpha_val is two’s complement number.
		 */
		if (!(ctl & PLL_ALPHA_MODE))
			return alpha_huayra_pll_calc_rate(rate, l, alpha);

		alpha_m = alpha >> PLL_HUAYRA_M_SHIFT & PLL_HUAYRA_M_MASK;
		alpha_n = alpha >> PLL_HUAYRA_N_SHIFT & PLL_HUAYRA_N_MASK;

		rate *= l;
		tmp = parent_rate;
		if (alpha_m >= BIT(PLL_HUAYRA_M_WIDTH - 1)) {
			alpha_m = BIT(PLL_HUAYRA_M_WIDTH) - alpha_m;
			tmp *= alpha_m;
			do_div(tmp, alpha_n);
			rate -= tmp;
		} else {
			tmp *= alpha_m;
			do_div(tmp, alpha_n);
			rate += tmp;
		}

		return rate;
	}

	return alpha_huayra_pll_calc_rate(rate, l, alpha);
}

static int alpha_pll_huayra_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long prate)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 l, a, ctl, cur_alpha = 0;

	rate = alpha_huayra_pll_round_rate(rate, prate, &l, &a);

	regmap_read(pll->clkr.regmap, PLL_USER_CTL(pll), &ctl);

	if (ctl & PLL_ALPHA_EN)
		regmap_read(pll->clkr.regmap, PLL_ALPHA_VAL(pll), &cur_alpha);

	/*
	 * Huayra PLL supports PLL dynamic programming. User can change L_VAL,
	 * without having to go through the power on sequence.
	 */
	if (clk_alpha_pll_is_enabled(hw)) {
		if (cur_alpha != a) {
			pr_err("%s: clock needs to be gated\n",
			       clk_hw_get_name(hw));
			return -EBUSY;
		}

		regmap_write(pll->clkr.regmap, PLL_L_VAL(pll), l);
		/* Ensure that the write above goes to detect L val change. */
		mb();
		return wait_for_pll_enable_lock(pll);
	}

	regmap_write(pll->clkr.regmap, PLL_L_VAL(pll), l);
	regmap_write(pll->clkr.regmap, PLL_ALPHA_VAL(pll), a);

	if (a == 0)
		regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
				   PLL_ALPHA_EN, 0x0);
	else
		regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
				   PLL_ALPHA_EN | PLL_ALPHA_MODE, PLL_ALPHA_EN);

	return 0;
}

static long alpha_pll_huayra_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *prate)
{
	u32 l, a;

	return alpha_huayra_pll_round_rate(rate, *prate, &l, &a);
}

static int trion_pll_is_enabled(struct clk_alpha_pll *pll,
				struct regmap *regmap)
{
	u32 mode_val, opmode_val;
	int ret;

	ret = regmap_read(regmap, PLL_MODE(pll), &mode_val);
	ret |= regmap_read(regmap, PLL_OPMODE(pll), &opmode_val);
	if (ret)
		return 0;

	return ((opmode_val & PLL_RUN) && (mode_val & PLL_OUTCTRL));
}

static int clk_trion_pll_is_enabled(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);

	return trion_pll_is_enabled(pll, pll->clkr.regmap);
}

static int clk_trion_pll_enable(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	struct regmap *regmap = pll->clkr.regmap;
	u32 val;
	int ret;

	ret = regmap_read(regmap, PLL_MODE(pll), &val);
	if (ret)
		return ret;

	/* If in FSM mode, just vote for it */
	if (val & PLL_VOTE_FSM_ENA) {
		ret = clk_enable_regmap(hw);
		if (ret)
			return ret;
		return wait_for_pll_enable_active(pll);
	}

	/* Set operation mode to RUN */
	regmap_write(regmap, PLL_OPMODE(pll), PLL_RUN);

	ret = wait_for_pll_enable_lock(pll);
	if (ret)
		return ret;

	/* Enable the PLL outputs */
	ret = regmap_update_bits(regmap, PLL_USER_CTL(pll),
				 PLL_OUT_MASK, PLL_OUT_MASK);
	if (ret)
		return ret;

	/* Enable the global PLL outputs */
	return regmap_update_bits(regmap, PLL_MODE(pll),
				 PLL_OUTCTRL, PLL_OUTCTRL);
}

static void clk_trion_pll_disable(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	struct regmap *regmap = pll->clkr.regmap;
	u32 val;
	int ret;

	ret = regmap_read(regmap, PLL_MODE(pll), &val);
	if (ret)
		return;

	/* If in FSM mode, just unvote it */
	if (val & PLL_VOTE_FSM_ENA) {
		clk_disable_regmap(hw);
		return;
	}

	/* Disable the global PLL output */
	ret = regmap_update_bits(regmap, PLL_MODE(pll), PLL_OUTCTRL, 0);
	if (ret)
		return;

	/* Disable the PLL outputs */
	ret = regmap_update_bits(regmap, PLL_USER_CTL(pll),
				 PLL_OUT_MASK, 0);
	if (ret)
		return;

	/* Place the PLL mode in STANDBY */
	regmap_write(regmap, PLL_OPMODE(pll), PLL_STANDBY);
	regmap_update_bits(regmap, PLL_MODE(pll), PLL_RESET_N, PLL_RESET_N);
}

static unsigned long
clk_trion_pll_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 l, frac, alpha_width = pll_alpha_width(pll);

	regmap_read(pll->clkr.regmap, PLL_L_VAL(pll), &l);
	regmap_read(pll->clkr.regmap, PLL_ALPHA_VAL(pll), &frac);

	return alpha_pll_calc_rate(parent_rate, l, frac, alpha_width);
}

static void clk_alpha_pll_list_registers(struct seq_file *f, struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	int size, i, val;

	static struct clk_register_data data[] = {
		{"PLL_MODE", PLL_OFF_MODE},
		{"PLL_L_VAL", PLL_OFF_L_VAL},
		{"PLL_ALPHA_VAL", PLL_OFF_ALPHA_VAL},
		{"PLL_ALPHA_VAL_U", PLL_OFF_ALPHA_VAL_U},
		{"PLL_TEST_CTL", PLL_OFF_TEST_CTL},
		{"PLL_TEST_CTL_U", PLL_OFF_TEST_CTL_U},
		{"PLL_USER_CTL", PLL_OFF_USER_CTL},
		{"PLL_USER_CTL_U", PLL_OFF_USER_CTL_U},
		{"PLL_CONFIG_CTL", PLL_OFF_CONFIG_CTL},
		{"PLL_STATUS", PLL_OFF_STATUS},
	};

	static struct clk_register_data data1[] = {
		{"APSS_PLL_VOTE", 0x0},
	};

	size = ARRAY_SIZE(data);

	for (i = 0; i < size; i++) {
		regmap_read(pll->clkr.regmap, pll->offset +
				pll->regs[data[i].offset], &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data[i].name, val);
	}

	regmap_read(pll->clkr.regmap, pll->offset +
					pll->regs[data[0].offset], &val);

	if (val & PLL_FSM_ENA) {
		regmap_read(pll->clkr.regmap, pll->clkr.enable_reg +
					data1[0].offset, &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data1[0].name, val);
	}
}

static struct clk_regmap_ops clk_alpha_pll_regmap_ops = {
	.list_registers = clk_alpha_pll_list_registers,
};

static int clk_alpha_pll_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_alpha_pll_regmap_ops;

	return 0;
}

static int get_pll_type(struct clk_alpha_pll *pll,
				const u8 clk_alpha_pll_regs[][PLL_OFF_MAX_REGS])
{
	if (pll->regs)
		return (pll->regs - clk_alpha_pll_regs[0]) / (PLL_OFF_MAX_REGS);

	pr_debug("pll->regs not defined\n");
	return -EINVAL;
}

static void clk_pll_restore_context(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	int type = get_pll_type(pll, clk_alpha_pll_regs);

	if (!pll->config)
		return;

	switch (type) {
	case CLK_ALPHA_PLL_TYPE_DEFAULT:
	case CLK_ALPHA_PLL_TYPE_HUAYRA:
		clk_alpha_pll_configure(pll, pll->clkr.regmap,
					pll->config);
		break;
	case CLK_ALPHA_PLL_TYPE_FABIA:
		clk_fabia_pll_configure(pll, pll->clkr.regmap,
					pll->config);
		break;
	case CLK_ALPHA_PLL_TYPE_TRION:
		clk_trion_pll_configure(pll, pll->clkr.regmap,
					 pll->config);
		break;
	case CLK_ALPHA_PLL_TYPE_ZONDA:
		clk_zonda_pll_configure(pll, pll->clkr.regmap,
					pll->config);
		break;
	case CLK_ALPHA_PLL_TYPE_ZONDA_5LPE:
		clk_zonda_5lpe_pll_configure(pll, pll->clkr.regmap,
					pll->config);
		break;
	case CLK_ALPHA_PLL_TYPE_REGERA:
		clk_regera_pll_configure(pll, pll->clkr.regmap,
					pll->config);
		break;
	case CLK_ALPHA_PLL_TYPE_AGERA:
		clk_agera_pll_configure(pll, pll->clkr.regmap,
					pll->config);
		break;
	case CLK_ALPHA_PLL_TYPE_LUCID_EVO:
		clk_lucid_evo_pll_configure(pll, pll->clkr.regmap,
					pll->config);
		break;
	case CLK_ALPHA_PLL_TYPE_RIVIAN_EVO:
		clk_rivian_evo_pll_configure(pll, pll->clkr.regmap,
					pll->config);
		break;
	default:
		pr_err("Invalid pll type!\n");
	}
}

const struct clk_ops clk_alpha_pll_fixed_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = clk_alpha_pll_enable,
	.disable = clk_alpha_pll_disable,
	.is_enabled = clk_alpha_pll_is_enabled,
	.recalc_rate = clk_alpha_pll_recalc_rate,
	.init = clk_alpha_pll_init,
	.debug_init = clk_common_debug_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_fixed_ops);

const struct clk_ops clk_alpha_pll_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = clk_alpha_pll_enable,
	.disable = clk_alpha_pll_disable,
	.is_enabled = clk_alpha_pll_is_enabled,
	.recalc_rate = clk_alpha_pll_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.set_rate = clk_alpha_pll_set_rate,
	.init = clk_alpha_pll_init,
	.debug_init = clk_common_debug_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_ops);

static void clk_alpha_pll_huayra_list_registers(struct seq_file *f,
							struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	int size, i, val;

	static struct clk_register_data data[] = {
		{"PLL_MODE", PLL_OFF_MODE},
		{"PLL_L_VAL", PLL_OFF_L_VAL},
		{"PLL_ALPHA_VAL", PLL_OFF_ALPHA_VAL},
		{"PLL_USER_CTL", PLL_OFF_USER_CTL},
		{"PLL_CONFIG_CTL", PLL_OFF_CONFIG_CTL},
		{"PLL_CONFIG_CTL_U", PLL_OFF_CONFIG_CTL_U},
		{"PLL_CONFIG_CTL_U1", PLL_OFF_CONFIG_CTL_U1},
		{"PLL_TEST_CTL", PLL_OFF_TEST_CTL},
		{"PLL_TEST_CTL_U", PLL_OFF_TEST_CTL_U},
		{"PLL_TEST_CTL_U1", PLL_OFF_TEST_CTL_U1},
		{"PLL_OPMODE", PLL_OFF_OPMODE},
		{"PLL_STATUS", PLL_OFF_STATUS},
	};

	static struct clk_register_data data1[] = {
		{"APSS_PLL_VOTE", 0x0},
	};

	size = ARRAY_SIZE(data);

	for (i = 0; i < size; i++) {
		regmap_read(pll->clkr.regmap, pll->offset +
					pll->regs[data[i].offset], &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data[i].name, val);
	}

	regmap_read(pll->clkr.regmap, pll->offset + pll->regs[data[0].offset],
								&val);

	if (val & PLL_FSM_ENA) {
		regmap_read(pll->clkr.regmap, pll->clkr.enable_reg +
				data1[0].offset, &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data1[0].name, val);
	}
}

static struct clk_regmap_ops clk_alpha_pll_huayra_regmap_ops = {
	.list_registers = clk_alpha_pll_huayra_list_registers,
};

static int clk_alpha_pll_huayra_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_alpha_pll_huayra_regmap_ops;

	return 0;
}

const struct clk_ops clk_alpha_pll_huayra_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = clk_alpha_pll_enable,
	.disable = clk_alpha_pll_disable,
	.is_enabled = clk_alpha_pll_is_enabled,
	.recalc_rate = alpha_pll_huayra_recalc_rate,
	.round_rate = alpha_pll_huayra_round_rate,
	.set_rate = alpha_pll_huayra_set_rate,
	.init = clk_alpha_pll_huayra_init,
	.debug_init = clk_common_debug_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_huayra_ops);

const struct clk_ops clk_alpha_pll_hwfsm_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = clk_alpha_pll_hwfsm_enable,
	.disable = clk_alpha_pll_hwfsm_disable,
	.is_enabled = clk_alpha_pll_hwfsm_is_enabled,
	.recalc_rate = clk_alpha_pll_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.set_rate = clk_alpha_pll_hwfsm_set_rate,
	.init = clk_alpha_pll_init,
	.debug_init = clk_common_debug_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_hwfsm_ops);

static void clk_trion_pll_list_registers(struct seq_file *f, struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	int size, i, val;

	static struct clk_register_data data[] = {
		{"PLL_MODE", PLL_OFF_MODE},
		{"PLL_L_VAL", PLL_OFF_L_VAL},
		{"PLL_CAL_L_VAL", PLL_OFF_CAL_L_VAL},
		{"PLL_USER_CTL", PLL_OFF_USER_CTL},
		{"PLL_USER_CTL_U", PLL_OFF_USER_CTL_U},
		{"PLL_USER_CTL_U1", PLL_OFF_USER_CTL_U1},
		{"PLL_CONFIG_CTL", PLL_OFF_CONFIG_CTL},
		{"PLL_CONFIG_CTL_U", PLL_OFF_CONFIG_CTL_U},
		{"PLL_CONFIG_CTL_U1", PLL_OFF_CONFIG_CTL_U1},
		{"PLL_TEST_CTL", PLL_OFF_TEST_CTL},
		{"PLL_TEST_CTL_U", PLL_OFF_TEST_CTL_U},
		{"PLL_TEST_CTL_U1", PLL_OFF_TEST_CTL_U1},
		{"PLL_STATUS", PLL_OFF_STATUS},
		{"PLL_OPMODE", PLL_OFF_OPMODE},
		{"PLL_ALPHA_VAL", PLL_OFF_ALPHA_VAL},
	};

	static struct clk_register_data data1[] = {
		{"APSS_PLL_VOTE", 0x0},
	};

	size = ARRAY_SIZE(data);

	for (i = 0; i < size; i++) {
		regmap_read(pll->clkr.regmap, pll->offset +
					pll->regs[data[i].offset], &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data[i].name, val);
	}

	regmap_read(pll->clkr.regmap, pll->offset + pll->regs[data[0].offset],
								&val);

	if (val & PLL_FSM_ENA) {
		regmap_read(pll->clkr.regmap, pll->clkr.enable_reg +
				data1[0].offset, &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data1[0].name, val);
	}
}

static struct clk_regmap_ops clk_trion_pll_regmap_ops = {
	.list_registers = &clk_trion_pll_list_registers,
};

static int clk_trion_pll_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_trion_pll_regmap_ops;

	return 0;
}

const struct clk_ops clk_alpha_pll_fixed_trion_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = clk_trion_pll_enable,
	.disable = clk_trion_pll_disable,
	.is_enabled = clk_trion_pll_is_enabled,
	.recalc_rate = clk_trion_pll_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.debug_init = clk_common_debug_init,
	.init = clk_trion_pll_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_fixed_trion_ops);

static unsigned long
clk_alpha_pll_postdiv_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_alpha_pll_postdiv *pll = to_clk_alpha_pll_postdiv(hw);
	u32 ctl;

	regmap_read(pll->clkr.regmap, PLL_USER_CTL(pll), &ctl);

	ctl >>= PLL_POST_DIV_SHIFT;
	ctl &= PLL_POST_DIV_MASK(pll);

	return parent_rate >> fls(ctl);
}

static const struct clk_div_table clk_alpha_div_table[] = {
	{ 0x0, 1 },
	{ 0x1, 2 },
	{ 0x3, 4 },
	{ 0x7, 8 },
	{ 0xf, 16 },
	{ }
};

static const struct clk_div_table clk_alpha_2bit_div_table[] = {
	{ 0x0, 1 },
	{ 0x1, 2 },
	{ 0x3, 4 },
	{ }
};

static long
clk_alpha_pll_postdiv_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *prate)
{
	struct clk_alpha_pll_postdiv *pll = to_clk_alpha_pll_postdiv(hw);
	const struct clk_div_table *table;

	if (pll->width == 2)
		table = clk_alpha_2bit_div_table;
	else
		table = clk_alpha_div_table;

	return divider_round_rate(hw, rate, prate, table,
				  pll->width, CLK_DIVIDER_POWER_OF_TWO);
}

static long
clk_alpha_pll_postdiv_round_ro_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long *prate)
{
	struct clk_alpha_pll_postdiv *pll = to_clk_alpha_pll_postdiv(hw);
	struct clk_hw *parent_hw;
	u32 ctl, div;

	regmap_read(pll->clkr.regmap, PLL_USER_CTL(pll), &ctl);

	ctl >>= PLL_POST_DIV_SHIFT;
	ctl &= BIT(pll->width) - 1;
	div = 1 << fls(ctl);

	if (clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) {
		parent_hw = clk_hw_get_parent(hw);
		if (!parent_hw)
			return -EINVAL;

		*prate = clk_hw_round_rate(parent_hw, div * rate);
	}

	return DIV_ROUND_UP_ULL((u64)*prate, div);
}

static int clk_alpha_pll_postdiv_set_rate(struct clk_hw *hw, unsigned long rate,
					  unsigned long parent_rate)
{
	struct clk_alpha_pll_postdiv *pll = to_clk_alpha_pll_postdiv(hw);
	int div;

	/* 16 -> 0xf, 8 -> 0x7, 4 -> 0x3, 2 -> 0x1, 1 -> 0x0 */
	div = DIV_ROUND_UP_ULL(parent_rate, rate) - 1;

	return regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
				  PLL_POST_DIV_MASK(pll) << PLL_POST_DIV_SHIFT,
				  div << PLL_POST_DIV_SHIFT);
}

const struct clk_ops clk_alpha_pll_postdiv_ops = {
	.recalc_rate = clk_alpha_pll_postdiv_recalc_rate,
	.round_rate = clk_alpha_pll_postdiv_round_rate,
	.set_rate = clk_alpha_pll_postdiv_set_rate,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_postdiv_ops);

const struct clk_ops clk_alpha_pll_postdiv_ro_ops = {
	.round_rate = clk_alpha_pll_postdiv_round_ro_rate,
	.recalc_rate = clk_alpha_pll_postdiv_recalc_rate,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_postdiv_ro_ops);

void clk_fabia_pll_configure(struct clk_alpha_pll *pll, struct regmap *regmap,
			     const struct alpha_pll_config *config)
{
	u32 val, mask;

	clk_alpha_pll_write_config(regmap, PLL_L_VAL(pll), config->l);
	clk_alpha_pll_write_config(regmap, PLL_FRAC(pll), config->alpha);
	clk_alpha_pll_write_config(regmap, PLL_CONFIG_CTL(pll),
						config->config_ctl_val);
	clk_alpha_pll_write_config(regmap, PLL_CONFIG_CTL_U(pll),
						config->config_ctl_hi_val);
	clk_alpha_pll_write_config(regmap, PLL_USER_CTL(pll),
						config->user_ctl_val);
	clk_alpha_pll_write_config(regmap, PLL_USER_CTL_U(pll),
						config->user_ctl_hi_val);
	clk_alpha_pll_write_config(regmap, PLL_TEST_CTL(pll),
						config->test_ctl_val);
	clk_alpha_pll_write_config(regmap, PLL_TEST_CTL_U(pll),
						config->test_ctl_hi_val);

	if (config->post_div_mask) {
		mask = config->post_div_mask;
		val = config->post_div_val;
		regmap_update_bits(regmap, PLL_USER_CTL(pll), mask, val);
	}

	if (pll->flags & SUPPORTS_FSM_LEGACY_MODE)
		regmap_update_bits(regmap, PLL_MODE(pll), PLL_FSM_LEGACY_MODE,
							PLL_FSM_LEGACY_MODE);

	regmap_update_bits(regmap, PLL_MODE(pll), PLL_UPDATE_BYPASS,
							PLL_UPDATE_BYPASS);

	regmap_update_bits(regmap, PLL_MODE(pll), PLL_RESET_N, PLL_RESET_N);
}
EXPORT_SYMBOL_GPL(clk_fabia_pll_configure);

static int alpha_pll_fabia_enable(struct clk_hw *hw)
{
	int ret;
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 val, opmode_val;
	struct regmap *regmap = pll->clkr.regmap;

	ret = regmap_read(regmap, PLL_MODE(pll), &val);
	if (ret)
		return ret;

	/* If in FSM mode, just vote for it */
	if (val & PLL_VOTE_FSM_ENA) {
		ret = clk_enable_regmap(hw);
		if (ret)
			return ret;
		return wait_for_pll_enable_active(pll);
	}

	ret = regmap_read(regmap, PLL_OPMODE(pll), &opmode_val);
	if (ret)
		return ret;

	/* Skip If PLL is already running */
	if ((opmode_val & PLL_RUN) && (val & PLL_OUTCTRL))
		return 0;

	ret = regmap_update_bits(regmap, PLL_MODE(pll), PLL_OUTCTRL, 0);
	if (ret)
		return ret;

	ret = regmap_write(regmap, PLL_OPMODE(pll), PLL_STANDBY);
	if (ret)
		return ret;

	ret = regmap_update_bits(regmap, PLL_MODE(pll), PLL_RESET_N,
				 PLL_RESET_N);
	if (ret)
		return ret;

	ret = regmap_write(regmap, PLL_OPMODE(pll), PLL_RUN);
	if (ret)
		return ret;

	ret = wait_for_pll_enable_lock(pll);
	if (ret)
		return ret;

	ret = regmap_update_bits(regmap, PLL_USER_CTL(pll),
				 PLL_OUT_MASK, PLL_OUT_MASK);
	if (ret)
		return ret;

	return regmap_update_bits(regmap, PLL_MODE(pll), PLL_OUTCTRL,
				 PLL_OUTCTRL);
}

static void alpha_pll_fabia_disable(struct clk_hw *hw)
{
	int ret;
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 val;
	struct regmap *regmap = pll->clkr.regmap;

	ret = regmap_read(regmap, PLL_MODE(pll), &val);
	if (ret)
		return;

	/* If in FSM mode, just unvote it */
	if (val & PLL_FSM_ENA) {
		clk_disable_regmap(hw);
		return;
	}

	ret = regmap_update_bits(regmap, PLL_MODE(pll), PLL_OUTCTRL, 0);
	if (ret)
		return;

	/* Disable main outputs */
	ret = regmap_update_bits(regmap, PLL_USER_CTL(pll), PLL_OUT_MASK, 0);
	if (ret)
		return;

	/* Place the PLL in STANDBY */
	regmap_write(regmap, PLL_OPMODE(pll), PLL_STANDBY);
}

static unsigned long alpha_pll_fabia_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 l, frac, alpha_width = pll_alpha_width(pll);

	regmap_read(pll->clkr.regmap, PLL_L_VAL(pll), &l);
	regmap_read(pll->clkr.regmap, PLL_FRAC(pll), &frac);

	return alpha_pll_calc_rate(parent_rate, l, frac, alpha_width);
}

/*
 * Due to limited number of bits for fractional rate programming, the
 * rounded up rate could be marginally higher than the requested rate.
 */
static int alpha_pll_check_rate_margin(struct clk_hw *hw,
			unsigned long rrate, unsigned long rate)
{
	unsigned long rate_margin = rate + PLL_RATE_MARGIN;

	if (rrate > rate_margin || rrate < rate) {
		pr_err("%s: Rounded rate %lu not within range [%lu, %lu)\n",
		       clk_hw_get_name(hw), rrate, rate, rate_margin);
		return -EINVAL;
	}

	return 0;
}

static int alpha_pll_fabia_set_rate(struct clk_hw *hw, unsigned long rate,
						unsigned long prate)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 l, alpha_width = pll_alpha_width(pll);
	unsigned long rrate;
	int ret;
	u64 a;

	rrate = alpha_pll_round_rate(rate, prate, &l, &a, alpha_width);

	ret = alpha_pll_check_rate_margin(hw, rrate, rate);
	if (ret < 0)
		return ret;

	regmap_write(pll->clkr.regmap, PLL_L_VAL(pll), l);
	regmap_write(pll->clkr.regmap, PLL_FRAC(pll), a);

	return __clk_alpha_pll_update_latch(pll);
}

static int alpha_pll_fabia_prepare(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	const struct pll_vco *vco;
	struct clk_hw *parent_hw;
	unsigned long cal_freq, rrate;
	u32 cal_l, val, alpha_width = pll_alpha_width(pll);
	const char *name = clk_hw_get_name(hw);
	u64 a;
	int ret;

	ret = clk_prepare_regmap(hw);
	if (ret)
		return ret;

	/* Check if calibration needs to be done i.e. PLL is in reset */
	ret = regmap_read(pll->clkr.regmap, PLL_MODE(pll), &val);
	if (ret)
		return ret;

	/* Return early if calibration is not needed. */
	if (val & PLL_RESET_N)
		return 0;

	vco = alpha_pll_find_vco(pll, clk_hw_get_rate(hw));
	if (!vco) {
		pr_err("%s: alpha pll not in a valid vco range\n", name);
		return -EINVAL;
	}

	cal_freq = DIV_ROUND_CLOSEST((pll->vco_table[0].min_freq +
				pll->vco_table[0].max_freq) * 54, 100);

	parent_hw = clk_hw_get_parent(hw);
	if (!parent_hw)
		return -EINVAL;

	rrate = alpha_pll_round_rate(cal_freq, clk_hw_get_rate(parent_hw),
					&cal_l, &a, alpha_width);

	ret = alpha_pll_check_rate_margin(hw, rrate, cal_freq);
	if (ret < 0)
		return ret;

	/* Setup PLL for calibration frequency */
	regmap_write(pll->clkr.regmap, PLL_CAL_L_VAL(pll), cal_l);

	/* Bringup the PLL at calibration frequency */
	ret = clk_alpha_pll_enable(hw);
	if (ret) {
		pr_err("%s: alpha pll calibration failed\n", name);
		return ret;
	}

	clk_alpha_pll_disable(hw);

	return 0;
}

static void clk_fabia_pll_list_registers(struct seq_file *f, struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	int size, i, val;

	static struct clk_register_data data[] = {
		{"PLL_MODE", PLL_OFF_MODE},
		{"PLL_L_VAL", PLL_OFF_L_VAL},
		{"PLL_CAL_L_VAL", PLL_OFF_CAL_L_VAL},
		{"PLL_USER_CTL", PLL_OFF_USER_CTL},
		{"PLL_USER_CTL_U", PLL_OFF_USER_CTL_U},
		{"PLL_CONFIG_CTL", PLL_OFF_CONFIG_CTL},
		{"PLL_CONFIG_CTL_U", PLL_OFF_CONFIG_CTL_U},
		{"PLL_TEST_CTL", PLL_OFF_TEST_CTL},
		{"PLL_TEST_CTL_U", PLL_OFF_TEST_CTL_U},
		{"PLL_STATUS", PLL_OFF_STATUS},
		{"PLL_OPMODE", PLL_OFF_OPMODE},
		{"PLL_FRAC", PLL_OFF_FRAC},
	};

	static struct clk_register_data data1[] = {
		{"APSS_PLL_VOTE", 0x0},
	};

	size = ARRAY_SIZE(data);

	for (i = 0; i < size; i++) {
		regmap_read(pll->clkr.regmap, pll->offset +
					pll->regs[data[i].offset], &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data[i].name, val);
	}

	regmap_read(pll->clkr.regmap, pll->offset + pll->regs[data[0].offset],
								&val);

	if (val & PLL_FSM_ENA) {
		regmap_read(pll->clkr.regmap, pll->clkr.enable_reg +
				data1[0].offset, &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data1[0].name, val);
	}
}

static struct clk_regmap_ops clk_fabia_pll_regmap_ops = {
	.list_registers = &clk_fabia_pll_list_registers,
};

static int clk_fabia_pll_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_fabia_pll_regmap_ops;

	return 0;
}

const struct clk_ops clk_alpha_pll_fabia_ops = {
	.prepare = alpha_pll_fabia_prepare,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = alpha_pll_fabia_enable,
	.disable = alpha_pll_fabia_disable,
	.is_enabled = clk_alpha_pll_is_enabled,
	.set_rate = alpha_pll_fabia_set_rate,
	.recalc_rate = alpha_pll_fabia_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.debug_init = clk_common_debug_init,
	.init = clk_fabia_pll_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_fabia_ops);

const struct clk_ops clk_alpha_pll_fixed_fabia_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = alpha_pll_fabia_enable,
	.disable = alpha_pll_fabia_disable,
	.is_enabled = clk_alpha_pll_is_enabled,
	.recalc_rate = alpha_pll_fabia_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.debug_init = clk_common_debug_init,
	.init = clk_fabia_pll_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_fixed_fabia_ops);

static unsigned long clk_alpha_pll_postdiv_fabia_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct clk_alpha_pll_postdiv *pll = to_clk_alpha_pll_postdiv(hw);
	u32 i, div = 1, val;
	int ret;

	ret = regmap_read(pll->clkr.regmap, PLL_USER_CTL(pll), &val);
	if (ret)
		return ret;

	val >>= pll->post_div_shift;
	val &= BIT(pll->width) - 1;

	for (i = 0; i < pll->num_post_div; i++) {
		if (pll->post_div_table[i].val == val) {
			div = pll->post_div_table[i].div;
			break;
		}
	}

	return (parent_rate / div);
}

static unsigned long
clk_trion_pll_postdiv_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_alpha_pll_postdiv *pll = to_clk_alpha_pll_postdiv(hw);
	struct regmap *regmap = pll->clkr.regmap;
	u32 i, div = 1, val;

	if (!pll->post_div_table) {
		pr_err("Missing the post_div_table for the PLL\n");
		return -EINVAL;
	}

	regmap_read(regmap, PLL_USER_CTL(pll), &val);

	val >>= pll->post_div_shift;
	val &= PLL_POST_DIV_MASK(pll);

	for (i = 0; i < pll->num_post_div; i++) {
		if (pll->post_div_table[i].val == val) {
			div = pll->post_div_table[i].div;
			break;
		}
	}

	return (parent_rate / div);
}

static long
clk_trion_pll_postdiv_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *prate)
{
	struct clk_alpha_pll_postdiv *pll = to_clk_alpha_pll_postdiv(hw);

	return divider_round_rate(hw, rate, prate, pll->post_div_table,
				  pll->width, CLK_DIVIDER_ROUND_CLOSEST);
};

static int
clk_trion_pll_postdiv_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct clk_alpha_pll_postdiv *pll = to_clk_alpha_pll_postdiv(hw);
	struct regmap *regmap = pll->clkr.regmap;
	int i, val = 0, div;

	if (!pll->post_div_table) {
		pr_err("Missing the post_div_table for the PLL\n");
		return -EINVAL;
	}

	div = DIV_ROUND_UP_ULL(parent_rate, rate);
	for (i = 0; i < pll->num_post_div; i++) {
		if (pll->post_div_table[i].div == div) {
			val = pll->post_div_table[i].val;
			break;
		}
	}

	return regmap_update_bits(regmap, PLL_USER_CTL(pll),
				  PLL_POST_DIV_MASK(pll) << pll->post_div_shift,
				  val << pll->post_div_shift);
}

const struct clk_ops clk_alpha_pll_postdiv_trion_ops = {
	.recalc_rate = clk_trion_pll_postdiv_recalc_rate,
	.round_rate = clk_trion_pll_postdiv_round_rate,
	.set_rate = clk_trion_pll_postdiv_set_rate,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_postdiv_trion_ops);

static long clk_alpha_pll_postdiv_fabia_round_rate(struct clk_hw *hw,
				unsigned long rate, unsigned long *prate)
{
	struct clk_alpha_pll_postdiv *pll = to_clk_alpha_pll_postdiv(hw);

	return divider_round_rate(hw, rate, prate, pll->post_div_table,
				pll->width, CLK_DIVIDER_ROUND_CLOSEST);
}

static int clk_alpha_pll_postdiv_fabia_set_rate(struct clk_hw *hw,
				unsigned long rate, unsigned long parent_rate)
{
	struct clk_alpha_pll_postdiv *pll = to_clk_alpha_pll_postdiv(hw);
	int i, val = 0, div, ret;

	/*
	 * If the PLL is in FSM mode, then treat set_rate callback as a
	 * no-operation.
	 */
	ret = regmap_read(pll->clkr.regmap, PLL_MODE(pll), &val);
	if (ret)
		return ret;

	if (val & PLL_VOTE_FSM_ENA)
		return 0;

	div = DIV_ROUND_UP_ULL(parent_rate, rate);
	for (i = 0; i < pll->num_post_div; i++) {
		if (pll->post_div_table[i].div == div) {
			val = pll->post_div_table[i].val;
			break;
		}
	}

	return regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
				(BIT(pll->width) - 1) << pll->post_div_shift,
				val << pll->post_div_shift);
}

const struct clk_ops clk_alpha_pll_postdiv_fabia_ops = {
	.recalc_rate = clk_alpha_pll_postdiv_fabia_recalc_rate,
	.round_rate = clk_alpha_pll_postdiv_fabia_round_rate,
	.set_rate = clk_alpha_pll_postdiv_fabia_set_rate,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_postdiv_fabia_ops);

/**
 * clk_trion_pll_configure - configure the trion pll
 *
 * @pll: clk alpha pll
 * @regmap: register map
 * @config: configuration to apply for pll
 */
void clk_trion_pll_configure(struct clk_alpha_pll *pll, struct regmap *regmap,
			     const struct alpha_pll_config *config)
{
	/*
	 * If the bootloader left the PLL enabled it's likely that there are
	 * RCGs that will lock up if we disable the PLL below.
	 */
	if (trion_pll_is_enabled(pll, regmap)) {
		pr_debug("Trion PLL is already enabled, skipping configuration\n");
		return;
	}

	clk_alpha_pll_write_config(regmap, PLL_L_VAL(pll), config->l);

	if (config->cal_l)
		regmap_write(regmap, PLL_CAL_L_VAL(pll), config->cal_l);
	else
		regmap_write(regmap, PLL_CAL_L_VAL(pll), TRION_PLL_CAL_VAL);

	clk_alpha_pll_write_config(regmap, PLL_ALPHA_VAL(pll), config->alpha);
	clk_alpha_pll_write_config(regmap, PLL_CONFIG_CTL(pll),
				     config->config_ctl_val);
	clk_alpha_pll_write_config(regmap, PLL_CONFIG_CTL_U(pll),
				     config->config_ctl_hi_val);
	clk_alpha_pll_write_config(regmap, PLL_CONFIG_CTL_U1(pll),
				     config->config_ctl_hi1_val);
	clk_alpha_pll_write_config(regmap, PLL_USER_CTL(pll),
					config->user_ctl_val);
	clk_alpha_pll_write_config(regmap, PLL_USER_CTL_U(pll),
					config->user_ctl_hi_val);
	clk_alpha_pll_write_config(regmap, PLL_USER_CTL_U1(pll),
					config->user_ctl_hi1_val);
	clk_alpha_pll_write_config(regmap, PLL_TEST_CTL(pll),
					config->test_ctl_val);
	clk_alpha_pll_write_config(regmap, PLL_TEST_CTL_U(pll),
					config->test_ctl_hi_val);
	clk_alpha_pll_write_config(regmap, PLL_TEST_CTL_U1(pll),
					config->test_ctl_hi1_val);

	regmap_update_bits(regmap, PLL_MODE(pll), PLL_UPDATE_BYPASS,
			   PLL_UPDATE_BYPASS);

	if (pll->flags & SUPPORTS_FSM_LEGACY_MODE)
		regmap_update_bits(regmap, PLL_MODE(pll), PLL_FSM_LEGACY_MODE,
						PLL_FSM_LEGACY_MODE);

	/* Disable PLL output */
	regmap_update_bits(regmap, PLL_MODE(pll),  PLL_OUTCTRL, 0);

	/* Set operation mode to OFF */
	regmap_write(regmap, PLL_OPMODE(pll), PLL_STANDBY);

	/* Place the PLL in STANDBY mode */
	regmap_update_bits(regmap, PLL_MODE(pll), PLL_RESET_N, PLL_RESET_N);
}
EXPORT_SYMBOL_GPL(clk_trion_pll_configure);

/*
 * The TRION PLL requires a power-on self-calibration which happens when the
 * PLL comes out of reset. Calibrate in case it is not completed.
 */
static int __alpha_pll_trion_prepare(struct clk_hw *hw, u32 pcal_done)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 val;
	int ret;

	ret = clk_prepare_regmap(hw);
	if (ret)
		return ret;

	/* Return early if calibration is not needed. */
	regmap_read(pll->clkr.regmap, PLL_STATUS(pll), &val);
	if (val & pcal_done)
		return 0;

	/* On/off to calibrate */
	ret = clk_trion_pll_enable(hw);
	if (!ret)
		clk_trion_pll_disable(hw);

	return ret;
}

static int alpha_pll_trion_prepare(struct clk_hw *hw)
{
	return __alpha_pll_trion_prepare(hw, TRION_PCAL_DONE);
}

static int alpha_pll_lucid_prepare(struct clk_hw *hw)
{
	return __alpha_pll_trion_prepare(hw, LUCID_PCAL_DONE);
}

static int __alpha_pll_trion_set_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long prate, u32 latch_bit, u32 latch_ack)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	unsigned long rrate;
	u32 val, l, alpha_width = pll_alpha_width(pll);
	u64 a;
	int ret;

	rrate = alpha_pll_round_rate(rate, prate, &l, &a, alpha_width);

	ret = alpha_pll_check_rate_margin(hw, rrate, rate);
	if (ret < 0)
		return ret;

	regmap_write(pll->clkr.regmap, PLL_L_VAL(pll), l);
	regmap_write(pll->clkr.regmap, PLL_ALPHA_VAL(pll), a);

	/* Latch the PLL input */
	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll), latch_bit, latch_bit);
	if (ret)
		return ret;

	/* Wait for 2 reference cycles before checking the ACK bit. */
	udelay(1);
	regmap_read(pll->clkr.regmap, PLL_MODE(pll), &val);
	if (!(val & latch_ack)) {
		pr_err("Lucid PLL latch failed. Output may be unstable!\n");
		return -EINVAL;
	}

	/* Return the latch input to 0 */
	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll), latch_bit, 0);
	if (ret)
		return ret;

	if (clk_hw_is_enabled(hw)) {
		ret = wait_for_pll_enable_lock(pll);
		if (ret)
			return ret;
	}

	/* Wait for PLL output to stabilize */
	udelay(100);
	return 0;
}

static int alpha_pll_trion_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long prate)
{
	return __alpha_pll_trion_set_rate(hw, rate, prate, PLL_UPDATE, ALPHA_PLL_ACK_LATCH);
}

const struct clk_ops clk_alpha_pll_trion_ops = {
	.prepare = alpha_pll_trion_prepare,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = clk_trion_pll_enable,
	.disable = clk_trion_pll_disable,
	.is_enabled = clk_trion_pll_is_enabled,
	.recalc_rate = clk_trion_pll_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.set_rate = alpha_pll_trion_set_rate,
	.debug_init = clk_common_debug_init,
	.init = clk_trion_pll_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_trion_ops);

static void lucid_pll_list_registers(struct seq_file *f,
		struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	int size, i, val;

	static struct clk_register_data data[] = {
		{"PLL_MODE", PLL_OFF_MODE},
		{"PLL_L_VAL", PLL_OFF_L_VAL},
		{"PLL_CAL_L_VAL", PLL_OFF_CAL_L_VAL},
		{"PLL_USER_CTL", PLL_OFF_USER_CTL},
		{"PLL_USER_CTL_U", PLL_OFF_USER_CTL_U},
		{"PLL_USER_CTL_U1", PLL_OFF_USER_CTL_U1},
		{"PLL_CONFIG_CTL", PLL_OFF_CONFIG_CTL},
		{"PLL_CONFIG_CTL_U", PLL_OFF_CONFIG_CTL_U},
		{"PLL_CONFIG_CTL_U1", PLL_OFF_CONFIG_CTL_U1},
		{"PLL_TEST_CTL", PLL_OFF_TEST_CTL},
		{"PLL_TEST_CTL_U", PLL_OFF_TEST_CTL_U},
		{"PLL_TEST_CTL_U1", PLL_OFF_TEST_CTL_U1},
		{"PLL_STATUS", PLL_OFF_STATUS},
		{"PLL_OPMODE", PLL_OFF_OPMODE},
		{"PLL_ALPHA_VAL", PLL_OFF_ALPHA_VAL},
		{"PLL_SSC_DELTA_ALPHA", PLL_OFF_SSC_DELTA_ALPHA},
		{"PLL_SSC_NUM_STEPS", PLL_OFF_SSC_NUM_STEPS},
		{"PLL_SSC_UPDATE_RATE", PLL_OFF_SSC_UPDATE_RATE},
	};

	static struct clk_register_data data1[] = {
		{"APSS_PLL_VOTE", 0x0},
	};

	size = ARRAY_SIZE(data);

	for (i = 0; i < size; i++) {
		regmap_read(pll->clkr.regmap, pll->offset +
					pll->regs[data[i].offset], &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data[i].name, val);
	}

	regmap_read(pll->clkr.regmap, pll->offset +
					pll->regs[data[0].offset], &val);

	if (val & PLL_FSM_ENA) {
		regmap_read(pll->clkr.regmap, pll->clkr.enable_reg +
					data1[0].offset, &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data1[0].name, val);
	}
}

static struct clk_regmap_ops clk_lucid_pll_regmap_ops = {
	.list_registers = &lucid_pll_list_registers,
};

static int clk_lucid_pll_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_lucid_pll_regmap_ops;

	return 0;
}

const struct clk_ops clk_alpha_pll_lucid_ops = {
	.prepare = alpha_pll_lucid_prepare,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = clk_trion_pll_enable,
	.disable = clk_trion_pll_disable,
	.is_enabled = clk_trion_pll_is_enabled,
	.recalc_rate = clk_trion_pll_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.set_rate = alpha_pll_trion_set_rate,
	.debug_init = clk_common_debug_init,
	.init = clk_lucid_pll_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_lucid_ops);

const struct clk_ops clk_alpha_pll_postdiv_lucid_ops = {
	.recalc_rate = clk_alpha_pll_postdiv_fabia_recalc_rate,
	.round_rate = clk_alpha_pll_postdiv_fabia_round_rate,
	.set_rate = clk_alpha_pll_postdiv_fabia_set_rate,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_postdiv_lucid_ops);

void clk_agera_pll_configure(struct clk_alpha_pll *pll, struct regmap *regmap,
			const struct alpha_pll_config *config)
{
	clk_alpha_pll_write_config(regmap, PLL_L_VAL(pll), config->l);
	clk_alpha_pll_write_config(regmap, PLL_ALPHA_VAL(pll), config->alpha);
	clk_alpha_pll_write_config(regmap, PLL_USER_CTL(pll),
							config->user_ctl_val);
	clk_alpha_pll_write_config(regmap, PLL_CONFIG_CTL(pll),
						config->config_ctl_val);
	clk_alpha_pll_write_config(regmap, PLL_CONFIG_CTL_U(pll),
						config->config_ctl_hi_val);
	clk_alpha_pll_write_config(regmap, PLL_TEST_CTL(pll),
						config->test_ctl_val);
	clk_alpha_pll_write_config(regmap,  PLL_TEST_CTL_U(pll),
						config->test_ctl_hi_val);
}
EXPORT_SYMBOL_GPL(clk_agera_pll_configure);

static int clk_alpha_pll_agera_set_rate(struct clk_hw *hw, unsigned long rate,
							unsigned long prate)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 l, alpha_width = pll_alpha_width(pll);
	int ret;
	unsigned long rrate;
	u64 a;

	rrate = alpha_pll_round_rate(rate, prate, &l, &a, alpha_width);
	ret = alpha_pll_check_rate_margin(hw, rrate, rate);
	if (ret < 0)
		return ret;

	/* change L_VAL without having to go through the power on sequence */
	regmap_write(pll->clkr.regmap, PLL_L_VAL(pll), l);
	regmap_write(pll->clkr.regmap, PLL_ALPHA_VAL(pll), a);

	if (clk_hw_is_enabled(hw))
		return wait_for_pll_enable_lock(pll);

	return 0;
}

static void clk_agera_pll_list_registers(struct seq_file *f, struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	int size, i, val;

	static struct clk_register_data data[] = {
		{"PLL_MODE", PLL_OFF_MODE},
		{"PLL_L_VAL", PLL_OFF_L_VAL},
		{"PLL_ALPHA_VAL", PLL_OFF_ALPHA_VAL},
		{"PLL_USER_CTL", PLL_OFF_USER_CTL},
		{"PLL_CONFIG_CTL", PLL_OFF_CONFIG_CTL},
		{"PLL_CONFIG_CTL_U", PLL_OFF_CONFIG_CTL_U},
		{"PLL_TEST_CTL", PLL_OFF_TEST_CTL},
		{"PLL_TEST_CTL_U", PLL_OFF_TEST_CTL_U},
		{"PLL_STATUS", PLL_OFF_STATUS},
	};

	static struct clk_register_data data1[] = {
		{"APSS_PLL_VOTE", 0x0},
	};


	size = ARRAY_SIZE(data);

	for (i = 0; i < size; i++) {
		regmap_read(pll->clkr.regmap, pll->offset +
					pll->regs[data[i].offset], &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data[i].name, val);
	}

	regmap_read(pll->clkr.regmap, pll->offset + pll->regs[data[0].offset],
								&val);
	if (val & PLL_FSM_ENA) {
		regmap_read(pll->clkr.regmap, pll->clkr.enable_reg +
				data1[0].offset, &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data1[0].name, val);
	}
}

static struct clk_regmap_ops clk_agera_pll_regmap_ops = {
	.list_registers = clk_agera_pll_list_registers,
};

static int clk_agera_pll_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_agera_pll_regmap_ops;

	return 0;
}

const struct clk_ops clk_alpha_pll_agera_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = clk_alpha_pll_enable,
	.disable = clk_alpha_pll_disable,
	.is_enabled = clk_alpha_pll_is_enabled,
	.recalc_rate = alpha_pll_fabia_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.set_rate = clk_alpha_pll_agera_set_rate,
	.debug_init = clk_common_debug_init,
	.init = clk_agera_pll_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_agera_ops);

static int alpha_pll_lucid_5lpe_enable(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 val;
	int ret;

	ret = regmap_read(pll->clkr.regmap, PLL_USER_CTL(pll), &val);
	if (ret)
		return ret;

	/* If in FSM mode, just vote for it */
	if (val & LUCID_5LPE_ENABLE_VOTE_RUN) {
		ret = clk_enable_regmap(hw);
		if (ret)
			return ret;
		return wait_for_pll_enable_lock(pll);
	}

	/* Check if PLL is already enabled, return if enabled */
	ret = trion_pll_is_enabled(pll, pll->clkr.regmap);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll), PLL_RESET_N, PLL_RESET_N);
	if (ret)
		return ret;

	regmap_write(pll->clkr.regmap, PLL_OPMODE(pll), PLL_RUN);

	ret = wait_for_pll_enable_lock(pll);
	if (ret)
		return ret;

	/* Enable the PLL outputs */
	ret = regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll), PLL_OUT_MASK, PLL_OUT_MASK);
	if (ret)
		return ret;

	/* Enable the global PLL outputs */
	return regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll), PLL_OUTCTRL, PLL_OUTCTRL);
}

static void alpha_pll_lucid_5lpe_disable(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 val;
	int ret;

	ret = regmap_read(pll->clkr.regmap, PLL_USER_CTL(pll), &val);
	if (ret)
		return;

	/* If in FSM mode, just unvote it */
	if (val & LUCID_5LPE_ENABLE_VOTE_RUN) {
		clk_disable_regmap(hw);
		return;
	}

	/* Disable the global PLL output */
	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll), PLL_OUTCTRL, 0);
	if (ret)
		return;

	/* Disable the PLL outputs */
	ret = regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll), PLL_OUT_MASK, 0);
	if (ret)
		return;

	/* Place the PLL mode in STANDBY */
	regmap_write(pll->clkr.regmap, PLL_OPMODE(pll), PLL_STANDBY);
}

/*
 * The Lucid 5LPE PLL requires a power-on self-calibration which happens
 * when the PLL comes out of reset. Calibrate in case it is not completed.
 */
static int alpha_pll_lucid_5lpe_prepare(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	struct clk_hw *p;
	u32 val = 0;
	int ret;

	ret = clk_prepare_regmap(hw);
	if (ret)
		return ret;

	/* Return early if calibration is not needed. */
	regmap_read(pll->clkr.regmap, PLL_MODE(pll), &val);
	if (val & LUCID_5LPE_PCAL_DONE)
		return 0;

	p = clk_hw_get_parent(hw);
	if (!p)
		return -EINVAL;

	ret = alpha_pll_lucid_5lpe_enable(hw);
	if (ret)
		return ret;

	alpha_pll_lucid_5lpe_disable(hw);

	return 0;
}

static int alpha_pll_lucid_5lpe_set_rate(struct clk_hw *hw, unsigned long rate,
					 unsigned long prate)
{
	return __alpha_pll_trion_set_rate(hw, rate, prate,
					  LUCID_5LPE_PLL_LATCH_INPUT,
					  LUCID_5LPE_ALPHA_PLL_ACK_LATCH);
}

static int __clk_lucid_pll_postdiv_set_rate(struct clk_hw *hw, unsigned long rate,
					    unsigned long parent_rate,
					    unsigned long enable_vote_run)
{
	struct clk_alpha_pll_postdiv *pll = to_clk_alpha_pll_postdiv(hw);
	struct regmap *regmap = pll->clkr.regmap;
	int i, val, div, ret;
	u32 mask;

	/*
	 * If the PLL is in FSM mode, then treat set_rate callback as a
	 * no-operation.
	 */
	ret = regmap_read(regmap, PLL_USER_CTL(pll), &val);
	if (ret)
		return ret;

	if (val & enable_vote_run)
		return 0;

	if (!pll->post_div_table) {
		pr_err("Missing the post_div_table for the %s PLL\n",
		       clk_hw_get_name(&pll->clkr.hw));
		return -EINVAL;
	}

	div = DIV_ROUND_UP_ULL((u64)parent_rate, rate);
	for (i = 0; i < pll->num_post_div; i++) {
		if (pll->post_div_table[i].div == div) {
			val = pll->post_div_table[i].val;
			break;
		}
	}

	mask = GENMASK(pll->width + pll->post_div_shift - 1, pll->post_div_shift);
	return regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
				  mask, val << pll->post_div_shift);
}

static int clk_lucid_5lpe_pll_postdiv_set_rate(struct clk_hw *hw, unsigned long rate,
					       unsigned long parent_rate)
{
	return __clk_lucid_pll_postdiv_set_rate(hw, rate, parent_rate, LUCID_5LPE_ENABLE_VOTE_RUN);
}

const struct clk_ops clk_alpha_pll_lucid_5lpe_ops = {
	.prepare = alpha_pll_lucid_5lpe_prepare,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = alpha_pll_lucid_5lpe_enable,
	.disable = alpha_pll_lucid_5lpe_disable,
	.is_enabled = clk_trion_pll_is_enabled,
	.recalc_rate = clk_trion_pll_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.set_rate = alpha_pll_lucid_5lpe_set_rate,
	.debug_init = clk_common_debug_init,
	.init = clk_lucid_pll_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_lucid_5lpe_ops);

const struct clk_ops clk_alpha_pll_fixed_lucid_5lpe_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = alpha_pll_lucid_5lpe_enable,
	.disable = alpha_pll_lucid_5lpe_disable,
	.is_enabled = clk_trion_pll_is_enabled,
	.recalc_rate = clk_trion_pll_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.debug_init = clk_common_debug_init,
	.init = clk_lucid_pll_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_fixed_lucid_5lpe_ops);

const struct clk_ops clk_alpha_pll_postdiv_lucid_5lpe_ops = {
	.recalc_rate = clk_alpha_pll_postdiv_fabia_recalc_rate,
	.round_rate = clk_alpha_pll_postdiv_fabia_round_rate,
	.set_rate = clk_lucid_5lpe_pll_postdiv_set_rate,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_postdiv_lucid_5lpe_ops);

void clk_zonda_pll_configure(struct clk_alpha_pll *pll, struct regmap *regmap,
			     const struct alpha_pll_config *config)
{
	clk_alpha_pll_write_config(regmap, PLL_L_VAL(pll), config->l);
	clk_alpha_pll_write_config(regmap, PLL_ALPHA_VAL(pll), config->alpha);
	clk_alpha_pll_write_config(regmap, PLL_CONFIG_CTL(pll), config->config_ctl_val);
	clk_alpha_pll_write_config(regmap, PLL_CONFIG_CTL_U(pll), config->config_ctl_hi_val);
	clk_alpha_pll_write_config(regmap, PLL_CONFIG_CTL_U1(pll), config->config_ctl_hi1_val);
	clk_alpha_pll_write_config(regmap, PLL_USER_CTL(pll), config->user_ctl_val);
	clk_alpha_pll_write_config(regmap, PLL_USER_CTL_U(pll), config->user_ctl_hi_val);
	clk_alpha_pll_write_config(regmap, PLL_USER_CTL_U1(pll), config->user_ctl_hi1_val);
	clk_alpha_pll_write_config(regmap, PLL_TEST_CTL(pll), config->test_ctl_val);
	clk_alpha_pll_write_config(regmap, PLL_TEST_CTL_U(pll), config->test_ctl_hi_val);
	clk_alpha_pll_write_config(regmap, PLL_TEST_CTL_U1(pll), config->test_ctl_hi1_val);

	regmap_update_bits(regmap, PLL_MODE(pll), PLL_BYPASSNL, 0);

	/* Disable PLL output */
	regmap_update_bits(regmap, PLL_MODE(pll), PLL_OUTCTRL, 0);

	/* Set operation mode to OFF */
	regmap_write(regmap, PLL_OPMODE(pll), PLL_STANDBY);

	/* Place the PLL in STANDBY mode */
	regmap_update_bits(regmap, PLL_MODE(pll), PLL_RESET_N, PLL_RESET_N);
}
EXPORT_SYMBOL_GPL(clk_zonda_pll_configure);

static int clk_zonda_pll_enable(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	struct regmap *regmap = pll->clkr.regmap;
	u32 val;
	int ret;

	regmap_read(regmap, PLL_MODE(pll), &val);

	/* If in FSM mode, just vote for it */
	if (val & PLL_VOTE_FSM_ENA) {
		ret = clk_enable_regmap(hw);
		if (ret)
			return ret;
		return wait_for_pll_enable_active(pll);
	}

	/* Get the PLL out of bypass mode */
	regmap_update_bits(regmap, PLL_MODE(pll), PLL_BYPASSNL, PLL_BYPASSNL);

	/*
	 * H/W requires a 1us delay between disabling the bypass and
	 * de-asserting the reset.
	 */
	udelay(1);

	regmap_update_bits(regmap, PLL_MODE(pll), PLL_RESET_N, PLL_RESET_N);

	/* Set operation mode to RUN */
	regmap_write(regmap, PLL_OPMODE(pll), PLL_RUN);

	regmap_read(regmap, PLL_TEST_CTL(pll), &val);

	/* If cfa mode then poll for freq lock */
	if (val & ZONDA_STAY_IN_CFA)
		ret = wait_for_zonda_pll_freq_lock(pll);
	else
		ret = wait_for_pll_enable_lock(pll);
	if (ret)
		return ret;

	/* Enable the PLL outputs */
	regmap_update_bits(regmap, PLL_USER_CTL(pll), ZONDA_PLL_OUT_MASK, ZONDA_PLL_OUT_MASK);

	/* Enable the global PLL outputs */
	regmap_update_bits(regmap, PLL_MODE(pll), PLL_OUTCTRL, PLL_OUTCTRL);

	return 0;
}

static void clk_zonda_pll_disable(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	struct regmap *regmap = pll->clkr.regmap;
	u32 val;

	regmap_read(regmap, PLL_MODE(pll), &val);

	/* If in FSM mode, just unvote it */
	if (val & PLL_VOTE_FSM_ENA) {
		clk_disable_regmap(hw);
		return;
	}

	/* Disable the global PLL output */
	regmap_update_bits(regmap, PLL_MODE(pll), PLL_OUTCTRL, 0);

	/* Disable the PLL outputs */
	regmap_update_bits(regmap, PLL_USER_CTL(pll), ZONDA_PLL_OUT_MASK, 0);

	/* Put the PLL in bypass and reset */
	regmap_update_bits(regmap, PLL_MODE(pll), PLL_RESET_N | PLL_BYPASSNL, 0);

	/* Place the PLL mode in OFF state */
	regmap_write(regmap, PLL_OPMODE(pll), 0x0);
}

static int clk_zonda_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long prate)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	unsigned long rrate;
	u32 test_ctl_val;
	u32 l, alpha_width = pll_alpha_width(pll);
	u64 a;
	int ret;

	rrate = alpha_pll_round_rate(rate, prate, &l, &a, alpha_width);

	ret = alpha_pll_check_rate_margin(hw, rrate, rate);
	if (ret < 0)
		return ret;

	regmap_write(pll->clkr.regmap, PLL_ALPHA_VAL(pll), a);
	regmap_write(pll->clkr.regmap, PLL_L_VAL(pll), l);

	/* Wait before polling for the frequency latch */
	udelay(5);

	/* Read stay in cfa mode */
	regmap_read(pll->clkr.regmap, PLL_TEST_CTL(pll), &test_ctl_val);

	/* If cfa mode then poll for freq lock */
	if (test_ctl_val & ZONDA_STAY_IN_CFA)
		ret = wait_for_zonda_pll_freq_lock(pll);
	else
		ret = wait_for_pll_enable_lock(pll);
	if (ret)
		return ret;

	/* Wait for PLL output to stabilize */
	udelay(100);
	return 0;
}

static void clk_alpha_pll_zonda_list_registers(struct seq_file *f,
							struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	int size, i, val;

	static struct clk_register_data data[] = {
		{"PLL_MODE", PLL_OFF_MODE},
		{"PLL_L_VAL", PLL_OFF_L_VAL},
		{"PLL_ALPHA_VAL", PLL_OFF_ALPHA_VAL},
		{"PLL_USER_CTL", PLL_OFF_USER_CTL},
		{"PLL_CONFIG_CTL", PLL_OFF_CONFIG_CTL},
		{"PLL_CONFIG_CTL_U", PLL_OFF_CONFIG_CTL_U},
		{"PLL_CONFIG_CTL_U1", PLL_OFF_CONFIG_CTL_U1},
		{"PLL_TEST_CTL", PLL_OFF_TEST_CTL},
		{"PLL_TEST_CTL_U", PLL_OFF_TEST_CTL_U},
		{"PLL_TEST_CTL_U1", PLL_OFF_TEST_CTL_U1},
		{"PLL_OPMODE", PLL_OFF_OPMODE},
		{"PLL_STATUS", PLL_OFF_STATUS},
		{"PLL_SSC_DELTA_ALPHA", PLL_OFF_SSC_DELTA_ALPHA},
		{"PLL_SSC_UPDATE_RATE", PLL_OFF_SSC_UPDATE_RATE},
	};

	static struct clk_register_data data1[] = {
		{"APSS_PLL_VOTE", 0x0},
	};

	size = ARRAY_SIZE(data);

	for (i = 0; i < size; i++) {
		regmap_read(pll->clkr.regmap, pll->offset +
					pll->regs[data[i].offset], &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data[i].name, val);
	}

	regmap_read(pll->clkr.regmap, pll->offset + pll->regs[data[0].offset],
								&val);

	if (val & PLL_FSM_ENA) {
		regmap_read(pll->clkr.regmap, pll->clkr.enable_reg +
				data1[0].offset, &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data1[0].name, val);
	}
}

static struct clk_regmap_ops clk_alpha_pll_zonda_regmap_ops = {
	.list_registers = clk_alpha_pll_zonda_list_registers,
};

static int clk_alpha_pll_zonda_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_alpha_pll_zonda_regmap_ops;

	return 0;
}

const struct clk_ops clk_alpha_pll_zonda_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = clk_zonda_pll_enable,
	.disable = clk_zonda_pll_disable,
	.is_enabled = clk_trion_pll_is_enabled,
	.recalc_rate = clk_trion_pll_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.set_rate = clk_zonda_pll_set_rate,
	.debug_init = clk_common_debug_init,
	.init = clk_alpha_pll_zonda_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_zonda_ops);

static int clk_zonda_5lpe_pll_enable(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 val, test_ctl_val;
	int ret;

	ret = regmap_read(pll->clkr.regmap, PLL_USER_CTL(pll), &val);
	if (ret)
		return ret;

	/* If in FSM mode, just vote for it */
	if (val & ZONDA_5LPE_ENABLE_VOTE_RUN) {
		ret = clk_enable_regmap(hw);
		if (ret)
			return ret;
		return wait_for_pll_enable_active(pll);
	}

	/* Check if PLL is already enabled */
	ret = trion_pll_is_enabled(pll, pll->clkr.regmap);
	if (ret < 0)
		return ret;
	else if (ret) {
		pr_warn("%s PLL is already enabled\n",
				clk_hw_get_name(&pll->clkr.hw));
		return 0;
	}

	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
						 PLL_RESET_N | PLL_BYPASSNL,
						 PLL_RESET_N | PLL_BYPASSNL);
	if (ret)
		return ret;

	/* Set operation mode to RUN */
	regmap_write(pll->clkr.regmap, PLL_OPMODE(pll), PLL_RUN);

	ret = regmap_read(pll->clkr.regmap, PLL_TEST_CTL(pll), &test_ctl_val);
	if (ret)
		return ret;

	/* If cfa mode then poll for freq lock */
	if (test_ctl_val & ZONDA_STAY_IN_CFA)
		ret = wait_for_zonda_pll_freq_lock(pll);
	else
		ret = wait_for_pll_enable_lock(pll);
	if (ret)
		return ret;

	/* Enable the PLL outputs */
	ret = regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
				ZONDA_PLL_OUT_MASK, ZONDA_PLL_OUT_MASK);
	if (ret)
		return ret;

	/* Enable the global PLL outputs */
	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
				 PLL_OUTCTRL, PLL_OUTCTRL);
	if (ret)
		return ret;

	/* Ensure that the write above goes through before returning. */
	mb();

	return ret;
}

static void clk_zonda_5lpe_pll_disable(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 val, mask;
	int ret;

	ret = regmap_read(pll->clkr.regmap, PLL_USER_CTL(pll), &val);
	if (ret)
		return;

	/* If in FSM mode, just unvote it */
	if (val & ZONDA_5LPE_ENABLE_VOTE_RUN) {
		clk_disable_regmap(hw);
		return;
	}

	/* Disable the global PLL output */
	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
							PLL_OUTCTRL, 0);
	if (ret)
		return;

	/* Disable the PLL outputs */
	ret = regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
			PLL_OUT_MASK, 0);
	if (ret)
		return;

	/* Place the PLL mode in STANDBY */
	regmap_write(pll->clkr.regmap, PLL_OPMODE(pll),
			PLL_STANDBY);

	mask = PLL_RESET_N | PLL_BYPASSNL;
	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll), mask, 0);
	if (ret)
		return;
}

const struct clk_ops clk_alpha_pll_zonda_5lpe_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = clk_zonda_5lpe_pll_enable,
	.disable = clk_zonda_5lpe_pll_disable,
	.is_enabled = clk_trion_pll_is_enabled,
	.recalc_rate = clk_trion_pll_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.set_rate = clk_zonda_pll_set_rate,
	.debug_init = clk_common_debug_init,
	.init = clk_alpha_pll_zonda_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL(clk_alpha_pll_zonda_5lpe_ops);

int clk_regera_pll_configure(struct clk_alpha_pll *pll, struct regmap *regmap,
				const struct alpha_pll_config *config)
{
	u32 mode_regval;
	int ret;

	if (!config) {
		pr_err("PLL configuration missing.\n");
		return -EINVAL;
	}

	ret = regmap_read(regmap, PLL_MODE(pll), &mode_regval);
	if (ret)
		return ret;

	if (mode_regval & PLL_LOCK_DET) {
		pr_warn("PLL is already enabled. Skipping configuration.\n");
		return 0;
	}

	if (config->alpha)
		regmap_write(regmap, PLL_ALPHA_VAL(pll), config->alpha);

	if (config->l)
		regmap_write(regmap, PLL_L_VAL(pll), config->l);

	if (config->config_ctl_val)
		regmap_write(regmap, PLL_CONFIG_CTL(pll),
						config->config_ctl_val);

	if (config->config_ctl_hi_val)
		regmap_write(regmap, PLL_CONFIG_CTL_U(pll),
						config->config_ctl_hi_val);

	if (config->config_ctl_hi1_val)
		regmap_write(regmap, PLL_CONFIG_CTL_U1(pll),
						config->config_ctl_hi1_val);

	if (config->user_ctl_val)
		regmap_write(regmap, PLL_USER_CTL(pll), config->user_ctl_val);

	if (config->test_ctl_val)
		regmap_write(regmap, PLL_TEST_CTL(pll), config->test_ctl_val);

	if (config->test_ctl_hi_val)
		regmap_write(regmap, PLL_TEST_CTL_U(pll),
						config->test_ctl_hi_val);

	if (config->test_ctl_hi1_val)
		regmap_write(regmap, PLL_TEST_CTL_U1(pll),
						config->test_ctl_hi1_val);

	/* Set operation mode to OFF */
	regmap_write(regmap, PLL_OPMODE(pll), PLL_STANDBY);

	return 0;
}
EXPORT_SYMBOL(clk_regera_pll_configure);

static int clk_regera_pll_enable(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 val, l_val;
	int ret;

	ret = regmap_read(pll->clkr.regmap, PLL_MODE(pll), &val);
	if (ret)
		return ret;

	/* If in FSM mode, just vote for it */
	if (val & PLL_VOTE_FSM_ENA) {
		ret = clk_enable_regmap(hw);
		if (ret)
			return ret;
		return wait_for_pll_enable_active(pll);
	}

	ret = regmap_read(pll->clkr.regmap, PLL_L_VAL(pll), &l_val);
	if (ret)
		return ret;

	/* PLL has lost it's L value, needs reconfiguration */
	if (!l_val) {
		ret = clk_regera_pll_configure(pll, pll->clkr.regmap,
						pll->config);
		if (ret) {
			pr_err("Failed to configure %s\n", clk_hw_get_name(hw));
			return ret;
		}
		pr_warn("%s: PLL configuration lost, reconfiguration of PLL done.\n",
				clk_hw_get_name(hw));
	}

	/* Get the PLL out of bypass mode */
	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
						PLL_BYPASSNL, PLL_BYPASSNL);
	if (ret)
		return ret;

	/*
	 * H/W requires a 1us delay between disabling the bypass and
	 * de-asserting the reset.
	 */
	mb();
	udelay(1);

	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
						 PLL_RESET_N, PLL_RESET_N);
	if (ret)
		return ret;

	/* Set operation mode to RUN */
	regmap_write(pll->clkr.regmap, PLL_OPMODE(pll), PLL_RUN);

	ret = wait_for_pll_enable_lock(pll);
	if (ret)
		return ret;

	/* Enable the PLL outputs */
	ret = regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
				ZONDA_PLL_OUT_MASK, ZONDA_PLL_OUT_MASK);
	if (ret)
		return ret;

	/* Enable the global PLL outputs */
	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
				 PLL_OUTCTRL, PLL_OUTCTRL);
	if (ret)
		return ret;

	/* Ensure that the write above goes through before returning. */
	mb();
	return ret;
}

static void clk_regera_pll_disable(struct clk_hw *hw)
{
	int ret;
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 val, mask;

	ret = regmap_read(pll->clkr.regmap, PLL_MODE(pll), &val);
	if (ret)
		return;

	/* If in FSM mode, just unvote it */
	if (val & PLL_VOTE_FSM_ENA) {
		clk_disable_regmap(hw);
		return;
	}

	/* Disable the global PLL output */
	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
							PLL_OUTCTRL, 0);
	if (ret)
		return;

	/* Disable the PLL outputs */
	ret = regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
					ZONDA_PLL_OUT_MASK, 0);

	/* Put the PLL in bypass and reset */
	mask = PLL_RESET_N | PLL_BYPASSNL;
	ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll), mask, 0);
	if (ret)
		return;

	/* Place the PLL mode in OFF state */
	regmap_write(pll->clkr.regmap, PLL_OPMODE(pll), PLL_STANDBY);
}

static int clk_regera_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long prate)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	unsigned long rrate;
	u32 l, regval, alpha_width = pll_alpha_width(pll);
	u64 a;
	int ret;

	ret = regmap_read(pll->clkr.regmap, PLL_L_VAL(pll), &l);
	if (ret)
		return ret;

	/* PLL has lost it's L value, needs reconfiguration */
	if (!l) {
		ret = clk_regera_pll_configure(pll, pll->clkr.regmap,
						pll->config);
		if (ret) {
			pr_err("Failed to configure %s\n", clk_hw_get_name(hw));
			return ret;
		}
		pr_warn("%s: PLL configuration lost, reconfiguration of PLL done.\n",
				clk_hw_get_name(hw));
	}

	rrate = alpha_pll_round_rate(rate, prate, &l, &a, alpha_width);
	/*
	 * Due to a limited number of bits for fractional rate programming, the
	 * rounded up rate could be marginally higher than the requested rate.
	 */
	if (rrate > (rate + PLL_RATE_MARGIN) || rrate < rate) {
		pr_err("Call set rate on the PLL with rounded rates!\n");
		return -EINVAL;
	}

	regmap_write(pll->clkr.regmap, PLL_ALPHA_VAL(pll), a);
	regmap_write(pll->clkr.regmap, PLL_L_VAL(pll), l);

	/* Return early if the PLL is disabled */
	ret = regmap_read(pll->clkr.regmap, PLL_OPMODE(pll), &regval);
	if (ret)
		return ret;

	if (regval == PLL_STANDBY)
		return 0;

	/* Wait before polling for the frequency latch */
	udelay(5);

	ret = wait_for_pll_enable_lock(pll);
	if (ret)
		return ret;

	/* Wait for PLL output to stabilize */
	udelay(100);
	return 0;
}

static unsigned long
clk_regera_pll_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 l, frac, alpha_width = pll_alpha_width(pll);

	regmap_read(pll->clkr.regmap, PLL_L_VAL(pll), &l);
	regmap_read(pll->clkr.regmap, PLL_ALPHA_VAL(pll), &frac);

	return alpha_pll_calc_rate(parent_rate, l, frac, alpha_width);
}

static void clk_regera_pll_list_registers(struct seq_file *f, struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	int size, i, val;

	static struct clk_register_data data[] = {
		{"PLL_MODE", PLL_OFF_MODE},
		{"PLL_L_VAL", PLL_OFF_L_VAL},
		{"PLL_ALPHA_VAL", PLL_OFF_ALPHA_VAL},
		{"PLL_USER_CTL", PLL_OFF_USER_CTL},
		{"PLL_CONFIG_CTL", PLL_OFF_CONFIG_CTL},
		{"PLL_CONFIG_CTL_U", PLL_OFF_CONFIG_CTL_U},
		{"PLL_CONFIG_CTL_U1", PLL_OFF_CONFIG_CTL_U1},
		{"PLL_TEST_CTL", PLL_OFF_TEST_CTL},
		{"PLL_TEST_CTL_U", PLL_OFF_TEST_CTL_U},
		{"PLL_TEST_CTL_U1", PLL_OFF_TEST_CTL_U1},
		{"PLL_OPMODE", PLL_OFF_OPMODE},
		{"PLL_STATUS", PLL_OFF_STATUS},
	};

	static struct clk_register_data data1[] = {
		{"APSS_PLL_VOTE", 0x0},
	};


	size = ARRAY_SIZE(data);

	for (i = 0; i < size; i++) {
		regmap_read(pll->clkr.regmap, pll->offset +
					pll->regs[data[i].offset], &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data[i].name, val);
	}

	regmap_read(pll->clkr.regmap, pll->offset + pll->regs[data[0].offset],
								&val);
	if (val & PLL_FSM_ENA) {
		regmap_read(pll->clkr.regmap, pll->clkr.enable_reg +
				data1[0].offset, &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data1[0].name, val);
	}
}

static struct clk_regmap_ops clk_regera_pll_regmap_ops = {
	.list_registers = clk_regera_pll_list_registers,
};

static int clk_regera_pll_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_regera_pll_regmap_ops;

	return 0;
}

const struct clk_ops clk_regera_pll_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = clk_regera_pll_enable,
	.disable = clk_regera_pll_disable,
	.is_enabled = clk_alpha_pll_is_enabled,
	.recalc_rate = clk_regera_pll_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.set_rate = clk_regera_pll_set_rate,
	.debug_init = clk_common_debug_init,
	.init = clk_regera_pll_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL(clk_regera_pll_ops);

int clk_lucid_evo_pll_configure(struct clk_alpha_pll *pll,
		struct regmap *regmap, const struct alpha_pll_config *config)
{
	int ret;
	u32 regval;

	ret = regmap_update_bits(regmap, PLL_USER_CTL(pll), PLL_OUT_MASK, PLL_OUT_MASK);
	if (ret)
		return ret;

	ret = trion_pll_is_enabled(pll, regmap);
	if (ret)
		return ret;

	regmap_read(regmap, PLL_L_VAL(pll), &regval);
	regval &= LUCID_EVO_PLL_L_VAL_MASK;
	if (regval)
		return 0;

	if (config->l)
		ret |= regmap_update_bits(regmap, PLL_L_VAL(pll),
					LUCID_EVO_PLL_L_VAL_MASK, config->l);

	if (config->cal_l_ringosc) {
		ret |= regmap_update_bits(regmap, PLL_L_VAL(pll),
					  LUCID_OLE_PROCESS_CAL_L_VAL_MASK,
					  config->cal_l << LUCID_OLE_PROCESS_CAL_L_VAL_SHIFT);
		ret |= regmap_update_bits(regmap, PLL_L_VAL(pll),
					  LUCID_OLE_RINGOSC_CAL_L_VAL_MASK,
					  config->cal_l_ringosc <<
					  LUCID_OLE_RINGOSC_CAL_L_VAL_SHIFT);
	} else if (config->cal_l) {
		ret |= regmap_update_bits(regmap, PLL_L_VAL(pll),
					  LUCID_EVO_PLL_CAL_L_VAL_MASK,
					  config->cal_l << LUCID_EVO_PLL_CAL_L_VAL_SHIFT);
	} else {
		ret |= regmap_write(regmap, PLL_CAL_L_VAL(pll),
				    TRION_PLL_CAL_VAL << LUCID_EVO_PLL_CAL_L_VAL_SHIFT);
	}

	if (config->alpha)
		ret |= regmap_write(regmap, PLL_ALPHA_VAL(pll), config->alpha);

	if (config->config_ctl_val)
		ret |= regmap_write(regmap, PLL_CONFIG_CTL(pll),
				config->config_ctl_val);

	if (config->config_ctl_hi_val)
		ret |= regmap_write(regmap, PLL_CONFIG_CTL_U(pll),
				config->config_ctl_hi_val);

	if (config->config_ctl_hi1_val)
		ret |= regmap_write(regmap, PLL_CONFIG_CTL_U1(pll),
				config->config_ctl_hi1_val);

	if (config->user_ctl_val)
		ret |= regmap_write(regmap, PLL_USER_CTL(pll),
				config->user_ctl_val | PLL_OUT_MASK);

	if (config->user_ctl_hi_val)
		ret |= regmap_write(regmap, PLL_USER_CTL_U(pll),
				config->user_ctl_hi_val);

	if (config->test_ctl_val)
		ret |= regmap_write(regmap, PLL_TEST_CTL(pll),
				config->test_ctl_val);

	if (config->test_ctl_hi_val)
		ret |= regmap_write(regmap, PLL_TEST_CTL_U(pll),
				config->test_ctl_hi_val);

	if (config->test_ctl_hi1_val)
		ret |= regmap_write(regmap, PLL_TEST_CTL_U1(pll),
				config->test_ctl_hi1_val);

	if (config->test_ctl_hi2_val)
		ret |= regmap_write(regmap, PLL_TEST_CTL_U2(pll),
				config->test_ctl_hi2_val);

	/* Disable PLL output */
	ret |= regmap_update_bits(regmap, PLL_MODE(pll),
					PLL_OUTCTRL, 0);

	/* Set operation mode to STANDBY */
	ret |= regmap_write(regmap, PLL_OPMODE(pll), PLL_STANDBY);

	/* PLL should be in OFF mode before continuing */
	wmb();

	/* Place the PLL in STANDBY mode */
	ret |= regmap_update_bits(regmap, PLL_MODE(pll),
				 PLL_RESET_N, PLL_RESET_N);

	return ret ? -EIO : 0;
}
EXPORT_SYMBOL(clk_lucid_evo_pll_configure);

static int _alpha_pll_lucid_evo_enable(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	struct regmap *regmap = pll->clkr.regmap;
	u32 val;
	int ret;

	ret = regmap_read(regmap, PLL_USER_CTL(pll), &val);
	if (ret)
		return ret;

	/* If in FSM mode, just vote for it */
	if (val & LUCID_EVO_ENABLE_VOTE_RUN) {
		ret = clk_enable_regmap(hw);
		if (ret)
			return ret;
		return wait_for_pll_enable_lock(pll);
	}

	/* Check if PLL is already enabled */
	ret = trion_pll_is_enabled(pll, regmap);
	if (ret < 0) {
		return ret;
	} else if (ret) {
		pr_warn("%s PLL is already enabled\n", clk_hw_get_name(&pll->clkr.hw));
		return 0;
	}

	ret = regmap_update_bits(regmap, PLL_MODE(pll), PLL_RESET_N, PLL_RESET_N);
	if (ret)
		return ret;

	/* Set operation mode to RUN */
	regmap_write(regmap, PLL_OPMODE(pll), PLL_RUN);

	ret = wait_for_pll_enable_lock(pll);
	if (ret)
		return ret;

	/* Enable the global PLL outputs */
	ret = regmap_update_bits(regmap, PLL_MODE(pll), PLL_OUTCTRL, PLL_OUTCTRL);
	if (ret)
		return ret;

	/* Ensure that the write above goes through before returning. */
	mb();
	return ret;
}

static int alpha_pll_lucid_evo_enable(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);

	if (!(pll->flags & ENABLE_IN_PREPARE))
		return _alpha_pll_lucid_evo_enable(hw);

	return 0;
}

static void _alpha_pll_lucid_evo_disable(struct clk_hw *hw, bool reset)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	struct regmap *regmap = pll->clkr.regmap;
	u32 val;
	int ret;

	ret = regmap_read(regmap, PLL_USER_CTL(pll), &val);
	if (ret)
		return;

	/* If in FSM mode, just unvote it */
	if (val & LUCID_EVO_ENABLE_VOTE_RUN) {
		clk_disable_regmap(hw);
		return;
	}

	/* Disable the global PLL output */
	ret = regmap_update_bits(regmap, PLL_MODE(pll), PLL_OUTCTRL, 0);
	if (ret)
		return;

	/* Place the PLL mode in STANDBY */
	regmap_write(regmap, PLL_OPMODE(pll), PLL_STANDBY);

	if (reset || pll->flags & DISABLE_TO_OFF)
		regmap_update_bits(regmap, PLL_MODE(pll), PLL_RESET_N, 0);
}

/*
 * The Lucid PLL requires a power-on self-calibration which happens when the
 * PLL comes out of reset. The calibration is performed at an output frequency
 * of ~1300 MHz which means that SW will have to vote on a voltage that's
 * equal to or greater than SVS_L1 on the corresponding rail. Since this is not
 * feasable to do in the atomic enable path, temporarily bring up the PLL here,
 * let it calibrate, and place it in standby before returning.
 */
static int _alpha_pll_lucid_evo_prepare(struct clk_hw *hw, bool reset)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	struct clk_hw *p;
	u32 regval;
	unsigned long prate;
	int ret;

	ret = clk_prepare_regmap(hw);
	if (ret)
		return ret;

	/* Return early if calibration is not needed. */
	regmap_read(pll->clkr.regmap, PLL_MODE(pll), &regval);
	if (!(regval & LUCID_EVO_PCAL_NOT_DONE) && !(pll->flags & ENABLE_IN_PREPARE))
		return 0;

	if (pll->config) {
		/*
		 * Reconfigure the PLL if CAL_L_VAL is 0 (which implies that all
		 * clock controller registers have been reset).
		 */
		regmap_read(pll->clkr.regmap, PLL_L_VAL(pll), &regval);
		regval &= LUCID_EVO_PLL_CAL_L_VAL_MASK;
		if (!regval) {
			pr_debug("reconfiguring %s after it was reset\n",
				clk_hw_get_name(hw));
			ret = clk_lucid_evo_pll_configure(pll,
				pll->clkr.regmap, pll->config);
			if (ret) {
				pr_err("pll configuration failed: %u\n", ret);
				return ret;
			}
		}
	}

	p = clk_hw_get_parent(hw);
	if (!p)
		return -EINVAL;

	prate = clk_hw_get_rate(p);
	if (!prate)
		return -EINVAL;

	ret = _alpha_pll_lucid_evo_enable(hw);
	if (ret)
		return ret;

	/* Do not disable pll if ENABLE_IN_PREPARE*/
	if (!(pll->flags & ENABLE_IN_PREPARE))
		_alpha_pll_lucid_evo_disable(hw, reset);

	return 0;
}

static int alpha_pll_lucid_evo_set_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long prate)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	unsigned long rrate;
	u32 regval, l;
	u64 a;
	int ret;

	rrate = alpha_pll_round_rate(rate, prate, &l, &a,
					ALPHA_REG_16BIT_WIDTH);
	/*
	 * Due to a limited number of bits for fractional rate programming, the
	 * rounded up rate could be marginally higher than the requested rate.
	 */
	if (rrate > (rate + PLL_RATE_MARGIN) || rrate < rate) {
		pr_err("Call set rate on the PLL with rounded rates!\n");
		return -EINVAL;
	}

	regmap_update_bits(pll->clkr.regmap, PLL_L_VAL(pll),
			   LUCID_EVO_PLL_L_VAL_MASK, l);
	regmap_write(pll->clkr.regmap, PLL_ALPHA_VAL(pll), a);

	/*
	 * Latch the new L and ALPHA values. This is only necessary when the
	 * PLL is in RUN or STANDBY. If the PLL is in RESET, then the latch
	 * interface is disabled and the ACK won't assert. The PLL will
	 * automatically latch the values when transitioning out of RESET.
	 */
	regmap_read(pll->clkr.regmap, PLL_MODE(pll), &regval);
	if (regval & PLL_RESET_N) {
		ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
				LUCID_5LPE_PLL_LATCH_INPUT, LUCID_5LPE_PLL_LATCH_INPUT);
		if (ret)
			return ret;

		/* Wait for 2 reference cycles before checking the ACK bit. */
		udelay(1);
		regmap_read(pll->clkr.regmap, PLL_MODE(pll), &regval);
		if (!(regval & LUCID_5LPE_ALPHA_PLL_ACK_LATCH)) {
			WARN_CLK(&pll->clkr.hw, 1,
					"PLL latch failed. Output may be unstable!\n");
			return -EINVAL;
		}

		/* Return the latch input to 0 */
		ret = regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
				LUCID_5LPE_PLL_LATCH_INPUT, 0);
		if (ret)
			return ret;
	}

	if (clk_hw_is_enabled(hw)) {
		ret = wait_for_pll_enable_lock(pll);
		if (ret)
			return ret;
	}

	return 0;
}

static void lucid_evo_pll_list_registers(struct seq_file *f,
		struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	int size, i, val;

	static struct clk_register_data data[] = {
		{"PLL_MODE", PLL_OFF_MODE},
		{"PLL_OPMODE", PLL_OFF_OPMODE},
		{"PLL_STATUS", PLL_OFF_STATUS},
		{"PLL_L_VAL", PLL_OFF_L_VAL},
		{"PLL_ALPHA_VAL", PLL_OFF_ALPHA_VAL},
		{"PLL_USER_CTL", PLL_OFF_USER_CTL},
		{"PLL_USER_CTL_U", PLL_OFF_USER_CTL_U},
		{"PLL_CONFIG_CTL", PLL_OFF_CONFIG_CTL},
		{"PLL_CONFIG_CTL_U", PLL_OFF_CONFIG_CTL_U},
		{"PLL_CONFIG_CTL_U1", PLL_OFF_CONFIG_CTL_U1},
		{"PLL_TEST_CTL", PLL_OFF_TEST_CTL},
		{"PLL_TEST_CTL_U", PLL_OFF_TEST_CTL_U},
		{"PLL_TEST_CTL_U1", PLL_OFF_TEST_CTL_U1},
	};

	static struct clk_register_data data1[] = {
		{"APSS_PLL_VOTE", 0x0},
	};

	size = ARRAY_SIZE(data);

	for (i = 0; i < size; i++) {
		regmap_read(pll->clkr.regmap, pll->offset +
					pll->regs[data[i].offset], &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data[i].name, val);
	}

	regmap_read(pll->clkr.regmap, PLL_USER_CTL(pll), &val);

	if (val & LUCID_EVO_ENABLE_VOTE_RUN) {
		regmap_read(pll->clkr.regmap, pll->clkr.enable_reg +
					data1[0].offset, &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data1[0].name, val);
	}
}

static struct clk_regmap_ops clk_lucid_evo_pll_regmap_ops = {
	.list_registers = &lucid_evo_pll_list_registers,
};

static int clk_lucid_evo_pll_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_lucid_evo_pll_regmap_ops;

	return 0;
}

static void alpha_pll_lucid_evo_disable(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);

	if (!(pll->flags & ENABLE_IN_PREPARE))
		_alpha_pll_lucid_evo_disable(hw, false);
}
static int alpha_pll_lucid_evo_prepare(struct clk_hw *hw)
{
	return _alpha_pll_lucid_evo_prepare(hw, false);
}

static void alpha_pll_lucid_evo_unprepare(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);

	if (pll->flags & ENABLE_IN_PREPARE)
		_alpha_pll_lucid_evo_disable(hw, false);

	clk_unprepare_regmap(hw);
}

static void alpha_pll_reset_lucid_evo_disable(struct clk_hw *hw)
{
	_alpha_pll_lucid_evo_disable(hw, true);
}

static int alpha_pll_reset_lucid_evo_prepare(struct clk_hw *hw)
{
	return _alpha_pll_lucid_evo_prepare(hw, true);
}

static unsigned long alpha_pll_lucid_evo_recalc_rate(struct clk_hw *hw,
						     unsigned long parent_rate)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	struct regmap *regmap = pll->clkr.regmap;
	u32 l, frac;

	regmap_read(regmap, PLL_L_VAL(pll), &l);
	l &= LUCID_EVO_PLL_L_VAL_MASK;
	regmap_read(regmap, PLL_ALPHA_VAL(pll), &frac);

	return alpha_pll_calc_rate(parent_rate, l, frac, pll_alpha_width(pll));
}

static int clk_lucid_evo_pll_postdiv_set_rate(struct clk_hw *hw, unsigned long rate,
					      unsigned long parent_rate)
{
	return __clk_lucid_pll_postdiv_set_rate(hw, rate, parent_rate, LUCID_EVO_ENABLE_VOTE_RUN);
}

const struct clk_ops clk_alpha_pll_fixed_lucid_evo_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = alpha_pll_lucid_evo_enable,
	.disable = alpha_pll_lucid_evo_disable,
	.is_enabled = clk_trion_pll_is_enabled,
	.recalc_rate = alpha_pll_lucid_evo_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.debug_init = clk_common_debug_init,
	.init = clk_lucid_evo_pll_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_fixed_lucid_evo_ops);

const struct clk_ops clk_alpha_pll_postdiv_lucid_evo_ops = {
	.recalc_rate = clk_alpha_pll_postdiv_fabia_recalc_rate,
	.round_rate = clk_alpha_pll_postdiv_fabia_round_rate,
	.set_rate = clk_lucid_evo_pll_postdiv_set_rate,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_postdiv_lucid_evo_ops);

const struct clk_ops clk_alpha_pll_lucid_evo_ops = {
	.prepare = alpha_pll_lucid_evo_prepare,
	.unprepare = alpha_pll_lucid_evo_unprepare,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = alpha_pll_lucid_evo_enable,
	.disable = alpha_pll_lucid_evo_disable,
	.is_enabled = clk_trion_pll_is_enabled,
	.recalc_rate = alpha_pll_lucid_evo_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.set_rate = alpha_pll_lucid_evo_set_rate,
	.debug_init = clk_common_debug_init,
	.init = clk_lucid_evo_pll_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL(clk_alpha_pll_lucid_evo_ops);

const struct clk_ops clk_alpha_pll_reset_lucid_evo_ops = {
	.prepare = alpha_pll_reset_lucid_evo_prepare,
	.enable = alpha_pll_lucid_evo_enable,
	.disable = alpha_pll_reset_lucid_evo_disable,
	.is_enabled = clk_trion_pll_is_enabled,
	.recalc_rate = alpha_pll_lucid_evo_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.set_rate = alpha_pll_lucid_5lpe_set_rate,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_reset_lucid_evo_ops);

unsigned long lucid_evo_calc_pll(struct clk_hw *hw, u32 l, u64 a)
{
	struct clk_hw *p;
	unsigned long prate;

	p = clk_hw_get_parent(hw);
	if (!p)
		return 0;

	prate = clk_hw_get_rate(p);
	return alpha_pll_calc_rate(prate, l, a, ALPHA_REG_16BIT_WIDTH);
}

static struct clk_regmap_ops clk_lucid_evo_pll_crm_regmap_ops = {
	.list_registers = lucid_evo_pll_list_registers,
	.calc_pll = lucid_evo_calc_pll,
};

unsigned long lucid_evo_calc_pll_out(struct clk_hw *hw, u32 l, u64 a)
{
	struct clk_hw *p;
	unsigned long parent_rate;

	p = clk_hw_get_parent(hw);
	if (!p)
		return 0;

	parent_rate = lucid_evo_calc_pll(p, l, a);

	return clk_alpha_pll_postdiv_fabia_recalc_rate(hw, parent_rate);
}

static struct clk_regmap_ops clk_lucid_evo_pll_crm_postdiv_regmap_ops = {
	.calc_pll = lucid_evo_calc_pll_out,
};

static int clk_lucid_evo_pll_crm_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_lucid_evo_pll_crm_regmap_ops;

	return 0;
}

const struct clk_ops clk_alpha_pll_crm_lucid_evo_ops = {
	.recalc_rate = alpha_pll_lucid_evo_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.debug_init = clk_common_debug_init,
	.init = clk_lucid_evo_pll_crm_init,
};
EXPORT_SYMBOL(clk_alpha_pll_crm_lucid_evo_ops);

static int clk_lucid_evo_pll_crm_postdiv_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_lucid_evo_pll_crm_postdiv_regmap_ops;

	return 0;
}

const struct clk_ops clk_alpha_pll_crm_postdiv_lucid_evo_ops = {
	.recalc_rate = clk_alpha_pll_postdiv_fabia_recalc_rate,
	.round_rate = clk_alpha_pll_postdiv_fabia_round_rate,
	.init = clk_lucid_evo_pll_crm_postdiv_init,
};
EXPORT_SYMBOL(clk_alpha_pll_crm_postdiv_lucid_evo_ops);

int clk_rivian_evo_pll_configure(struct clk_alpha_pll *pll,
		struct regmap *regmap, const struct alpha_pll_config *config)
{
	u32 mask;
	int ret;

	ret = trion_pll_is_enabled(pll, regmap);
	if (ret)
		return ret;

	if (config->config_ctl_val)
		ret |= regmap_write(regmap, PLL_CONFIG_CTL(pll),
				config->config_ctl_val);

	if (config->config_ctl_hi_val)
		ret |= regmap_write(regmap, PLL_CONFIG_CTL_U(pll),
				config->config_ctl_hi_val);

	if (config->config_ctl_hi1_val)
		ret |= regmap_write(regmap, PLL_CONFIG_CTL_U1(pll),
				config->config_ctl_hi1_val);

	if (config->test_ctl_val)
		ret |= regmap_write(regmap, PLL_TEST_CTL(pll),
				config->test_ctl_val);

	if (config->test_ctl_hi_val)
		ret |= regmap_write(regmap, PLL_TEST_CTL_U(pll),
				config->test_ctl_hi_val);

	if (config->l)
		ret |= regmap_write(regmap, PLL_L_VAL(pll), config->l);

	if (config->user_ctl_val)
		ret |= regmap_write(regmap, PLL_USER_CTL(pll),
				config->user_ctl_val);

	if (config->user_ctl_hi_val)
		ret |= regmap_write(regmap, PLL_USER_CTL_U(pll),
				config->user_ctl_hi_val);

	/* pll_opmode to STANDBY */
	ret |= regmap_write(regmap, PLL_OPMODE(pll), PLL_STANDBY);

	mask = PLL_RESET_N | PLL_BYPASSNL;
	ret |= regmap_update_bits(regmap, PLL_MODE(pll), mask, mask);

	return ret ? -EIO : 0;
}
EXPORT_SYMBOL(clk_rivian_evo_pll_configure);

static unsigned long clk_rivian_evo_pll_recalc_rate(struct clk_hw *hw,
						    unsigned long parent_rate)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	u32 l;

	regmap_read(pll->clkr.regmap, PLL_L_VAL(pll), &l);

	return parent_rate * l;
}

static long clk_rivian_evo_pll_round_rate(struct clk_hw *hw, unsigned long rate,
					  unsigned long *prate)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	unsigned long min_freq, max_freq;
	u32 l;
	u64 a;

	rate = alpha_pll_round_rate(rate, *prate, &l, &a, 0);
	if (!pll->vco_table || alpha_pll_find_vco(pll, rate))
		return rate;

	min_freq = pll->vco_table[0].min_freq;
	max_freq = pll->vco_table[pll->num_vco - 1].max_freq;

	return clamp(rate, min_freq, max_freq);
}

static void clk_rivian_evo_pll_list_registers(struct seq_file *f, struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	int size, i, val;

	static struct clk_register_data data[] = {
		{"PLL_MODE", PLL_OFF_MODE},
		{"PLL_OPMODE", PLL_OFF_OPMODE},
		{"PLL_STATUS", PLL_OFF_STATUS},
		{"PLL_L_VAL", PLL_OFF_L_VAL},
		{"PLL_USER_CTL", PLL_OFF_USER_CTL},
		{"PLL_USER_CTL_U", PLL_OFF_USER_CTL_U},
		{"PLL_CONFIG_CTL", PLL_OFF_CONFIG_CTL},
		{"PLL_CONFIG_CTL_U", PLL_OFF_CONFIG_CTL_U},
		{"PLL_CONFIG_CTL_U1", PLL_OFF_CONFIG_CTL_U1},
		{"PLL_TEST_CTL", PLL_OFF_TEST_CTL},
		{"PLL_TEST_CTL_U", PLL_OFF_TEST_CTL_U},
	};

	size = ARRAY_SIZE(data);

	for (i = 0; i < size; i++) {
		regmap_read(pll->clkr.regmap, pll->offset +
					pll->regs[data[i].offset], &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data[i].name, val);
	}
}

static struct clk_regmap_ops clk_rivian_evo_pll_regmap_ops = {
	.list_registers = &clk_rivian_evo_pll_list_registers,
};

static int clk_rivian_evo_pll_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_rivian_evo_pll_regmap_ops;

	return 0;
}

const struct clk_ops clk_alpha_pll_rivian_evo_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = alpha_pll_lucid_5lpe_enable,
	.disable = alpha_pll_lucid_5lpe_disable,
	.is_enabled = clk_trion_pll_is_enabled,
	.recalc_rate = clk_rivian_evo_pll_recalc_rate,
	.round_rate = clk_rivian_evo_pll_round_rate,
	.debug_init = clk_common_debug_init,
	.init = clk_rivian_evo_pll_init,
	.restore_context = clk_pll_restore_context,
};
EXPORT_SYMBOL_GPL(clk_alpha_pll_rivian_evo_ops);

static int clk_alpha_pll_slew_update(struct clk_alpha_pll *pll)
{
	int ret = 0;
	u32 val;

	regmap_update_bits(pll->clkr.regmap, PLL_MODE(pll),
					PLL_UPDATE, PLL_UPDATE);
	regmap_read(pll->clkr.regmap, PLL_MODE(pll), &val);

	ret = wait_for_pll_update(pll);
	if (ret)
		return ret;
	/*
	 * HPG mandates a wait of at least 570ns before polling the LOCK
	 * detect bit. Have a delay of 1us just to be safe.
	 */
	mb();
	udelay(1);

	ret = wait_for_pll_enable_lock(pll);

	return ret;
}

static int clk_alpha_pll_slew_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	unsigned long freq_hz;
	const struct pll_vco *curr_vco, *vco;
	u32 l, alpha_width = pll_alpha_width(pll);
	u64 a;

	freq_hz =  alpha_pll_round_rate(rate, parent_rate, &l, &a, alpha_width);
	if (freq_hz != rate) {
		pr_err("alpha_pll: Call clk_set_rate with rounded rates!\n");
		return -EINVAL;
	}

	curr_vco = alpha_pll_find_vco(pll, clk_hw_get_rate(hw));
	if (!curr_vco) {
		pr_err("alpha pll: not in a valid vco range\n");
		return -EINVAL;
	}

	vco = alpha_pll_find_vco(pll, freq_hz);
	if (!vco) {
		pr_err("alpha pll: not in a valid vco range\n");
		return -EINVAL;
	}

	/*
	 * Dynamic pll update will not support switching frequencies across
	 * vco ranges. In those cases fall back to normal alpha set rate.
	 */
	if (curr_vco->val != vco->val)
		return clk_alpha_pll_set_rate(hw, rate, parent_rate);

	a = a << (ALPHA_REG_BITWIDTH - ALPHA_BITWIDTH);

	regmap_write(pll->clkr.regmap, PLL_L_VAL(pll), l);
	regmap_write(pll->clkr.regmap, PLL_ALPHA_VAL(pll), a);
	regmap_write(pll->clkr.regmap, PLL_ALPHA_VAL_U(pll), a >> 32);

	/* Ensure that the write above goes through before proceeding. */
	mb();

	if (clk_hw_is_enabled(hw)) {
		if (pll->flags & SUPPORTS_DYNAMIC_UPDATE)
			clk_alpha_pll_dynamic_update(pll);
		else
			clk_alpha_pll_slew_update(pll);
	}

	return 0;
}

/*
 * Slewing plls should be bought up at frequency which is in the middle of the
 * desired VCO range. So after bringing up the pll at calibration freq, set it
 * back to desired frequency(that was set by previous clk_set_rate).
 */
static int clk_alpha_pll_calibrate(struct clk_hw *hw)
{
	unsigned long calibration_freq, freq_hz;
	struct clk_alpha_pll *pll = to_clk_alpha_pll(hw);
	struct clk_hw *parent;
	const struct pll_vco *vco;
	u64 a;
	u32 l, alpha_width = pll_alpha_width(pll);
	int rc;

	parent = clk_hw_get_parent(hw);
	if (!parent) {
		pr_err("alpha pll: no valid parent found\n");
		return -EINVAL;
	}

	vco = alpha_pll_find_vco(pll, clk_hw_get_rate(hw));
	if (!vco) {
		pr_err("alpha pll: not in a valid vco range\n");
		return -EINVAL;
	}

	/*
	 * As during slewing plls vco_sel won't be allowed to change, vco table
	 * should have only one entry table, i.e. index = 0, find the
	 * calibration frequency.
	 */
	calibration_freq = (pll->vco_table[0].min_freq +
					pll->vco_table[0].max_freq)/2;

	freq_hz = alpha_pll_round_rate(calibration_freq,
			clk_hw_get_rate(parent), &l, &a, alpha_width);
	if (freq_hz != calibration_freq) {
		pr_err("alpha_pll: call clk_set_rate with rounded rates!\n");
		return -EINVAL;
	}

	/* Setup PLL for calibration frequency */
	a <<= (ALPHA_REG_BITWIDTH - ALPHA_BITWIDTH);

	regmap_write(pll->clkr.regmap, PLL_L_VAL(pll), l);
	regmap_write(pll->clkr.regmap, PLL_ALPHA_VAL(pll), a);
	regmap_write(pll->clkr.regmap, PLL_ALPHA_VAL_U(pll), a >> 32);

	regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
				PLL_VCO_MASK << PLL_VCO_SHIFT,
				vco->val << PLL_VCO_SHIFT);

	regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
				PLL_ALPHA_EN, PLL_ALPHA_EN);

	/* Bringup the pll at calibration frequency */
	rc = clk_alpha_pll_enable(hw);
	if (rc) {
		pr_err("alpha pll calibration failed\n");
		return rc;
	}

	/*
	 * PLL is already running at calibration frequency.
	 * So slew pll to the previously set frequency.
	 */
	freq_hz = alpha_pll_round_rate(clk_hw_get_rate(hw),
			clk_hw_get_rate(parent), &l, &a, alpha_width);


	pr_debug("pll %s: setting back to required rate %lu, freq_hz %ld\n",
				hw->init->name, clk_hw_get_rate(hw), freq_hz);

	/* Setup the PLL for the new frequency */
	a <<= (ALPHA_REG_BITWIDTH - ALPHA_BITWIDTH);

	regmap_write(pll->clkr.regmap, PLL_L_VAL(pll), l);
	regmap_write(pll->clkr.regmap, PLL_ALPHA_VAL(pll), a);
	regmap_write(pll->clkr.regmap, PLL_ALPHA_VAL_U(pll), a >> 32);

	regmap_update_bits(pll->clkr.regmap, PLL_USER_CTL(pll),
				PLL_ALPHA_EN, PLL_ALPHA_EN);

	if (pll->flags & SUPPORTS_DYNAMIC_UPDATE)
		return clk_alpha_pll_dynamic_update(pll);
	else
		return clk_alpha_pll_slew_update(pll);
}

static int clk_alpha_pll_slew_enable(struct clk_hw *hw)
{
	int rc;

	rc = clk_alpha_pll_calibrate(hw);
	if (rc)
		return rc;

	rc = clk_alpha_pll_enable(hw);

	return rc;
}

const struct clk_ops clk_alpha_pll_slew_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = clk_alpha_pll_slew_enable,
	.disable = clk_alpha_pll_disable,
	.recalc_rate = clk_alpha_pll_recalc_rate,
	.round_rate = clk_alpha_pll_round_rate,
	.set_rate = clk_alpha_pll_slew_set_rate,
	.init = clk_alpha_pll_init,
	.debug_init = clk_common_debug_init,
#ifdef CONFIG_COMMON_CLK_QCOM_DEBUG
	.list_rate_vdd_level = clk_list_rate_vdd_level,
#endif
};
EXPORT_SYMBOL(clk_alpha_pll_slew_ops);
