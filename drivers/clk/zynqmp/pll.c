// SPDX-License-Identifier: GPL-2.0
/*
 * Zynq UltraScale+ MPSoC PLL driver
 *
 *  Copyright (C) 2016-2018 Xilinx
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>
#include "clk-zynqmp.h"

/**
 * struct zynqmp_pll - PLL clock
 * @hw:		Handle between common and hardware-specific interfaces
 * @clk_id:	PLL clock ID
 * @set_pll_mode:	Whether an IOCTL_SET_PLL_FRAC_MODE request be sent to ATF
 */
struct zynqmp_pll {
	struct clk_hw hw;
	u32 clk_id;
	bool set_pll_mode;
};

#define to_zynqmp_pll(_hw)	container_of(_hw, struct zynqmp_pll, hw)

#define PLL_FBDIV_MIN	25
#define PLL_FBDIV_MAX	125

#define PS_PLL_VCO_MIN 1500000000
#define PS_PLL_VCO_MAX 3000000000UL

enum pll_mode {
	PLL_MODE_INT = 0,
	PLL_MODE_FRAC = 1,
	PLL_MODE_ERROR = 2,
};

#define FRAC_OFFSET 0x8
#define PLLFCFG_FRAC_EN	BIT(31)
#define FRAC_DIV  BIT(16)  /* 2^16 */

/**
 * zynqmp_pll_get_mode() - Get mode of PLL
 * @hw:		Handle between common and hardware-specific interfaces
 *
 * Return: Mode of PLL
 */
static inline enum pll_mode zynqmp_pll_get_mode(struct clk_hw *hw)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	u32 clk_id = clk->clk_id;
	const char *clk_name = clk_hw_get_name(hw);
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_get_pll_frac_mode(clk_id, ret_payload);
	if (ret) {
		pr_warn_once("%s() PLL get frac mode failed for %s, ret = %d\n",
			     __func__, clk_name, ret);
		return PLL_MODE_ERROR;
	}

	return ret_payload[1];
}

/**
 * zynqmp_pll_set_mode() - Set the PLL mode
 * @hw:		Handle between common and hardware-specific interfaces
 * @on:		Flag to determine the mode
 */
static inline void zynqmp_pll_set_mode(struct clk_hw *hw, bool on)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	u32 clk_id = clk->clk_id;
	const char *clk_name = clk_hw_get_name(hw);
	int ret;
	u32 mode;

	if (on)
		mode = PLL_MODE_FRAC;
	else
		mode = PLL_MODE_INT;

	ret = zynqmp_pm_set_pll_frac_mode(clk_id, mode);
	if (ret)
		pr_warn_once("%s() PLL set frac mode failed for %s, ret = %d\n",
			     __func__, clk_name, ret);
	else
		clk->set_pll_mode = true;
}

/**
 * zynqmp_pll_round_rate() - Round a clock frequency
 * @hw:		Handle between common and hardware-specific interfaces
 * @rate:	Desired clock frequency
 * @prate:	Clock frequency of parent clock
 *
 * Return: Frequency closest to @rate the hardware can generate
 */
static long zynqmp_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *prate)
{
	u32 fbdiv;
	u32 mult, div;

	/* Let rate fall inside the range PS_PLL_VCO_MIN ~ PS_PLL_VCO_MAX */
	if (rate > PS_PLL_VCO_MAX) {
		div = DIV_ROUND_UP(rate, PS_PLL_VCO_MAX);
		rate = rate / div;
	}
	if (rate < PS_PLL_VCO_MIN) {
		mult = DIV_ROUND_UP(PS_PLL_VCO_MIN, rate);
		rate = rate * mult;
	}

	fbdiv = DIV_ROUND_CLOSEST(rate, *prate);
	if (fbdiv < PLL_FBDIV_MIN || fbdiv > PLL_FBDIV_MAX) {
		fbdiv = clamp_t(u32, fbdiv, PLL_FBDIV_MIN, PLL_FBDIV_MAX);
		rate = *prate * fbdiv;
	}

	return rate;
}

/**
 * zynqmp_pll_recalc_rate() - Recalculate clock frequency
 * @hw:			Handle between common and hardware-specific interfaces
 * @parent_rate:	Clock frequency of parent clock
 *
 * Return: Current clock frequency or 0 in case of error
 */
static unsigned long zynqmp_pll_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	u32 clk_id = clk->clk_id;
	const char *clk_name = clk_hw_get_name(hw);
	u32 fbdiv, data;
	unsigned long rate, frac;
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;
	enum pll_mode mode;

	ret = zynqmp_pm_clock_getdivider(clk_id, &fbdiv);
	if (ret) {
		pr_warn_once("%s() get divider failed for %s, ret = %d\n",
			     __func__, clk_name, ret);
		return 0ul;
	}

	mode = zynqmp_pll_get_mode(hw);
	if (mode == PLL_MODE_ERROR)
		return 0ul;

	rate =  parent_rate * fbdiv;
	if (mode == PLL_MODE_FRAC) {
		zynqmp_pm_get_pll_frac_data(clk_id, ret_payload);
		data = ret_payload[1];
		frac = (parent_rate * data) / FRAC_DIV;
		rate = rate + frac;
	}

	return rate;
}

/**
 * zynqmp_pll_set_rate() - Set rate of PLL
 * @hw:			Handle between common and hardware-specific interfaces
 * @rate:		Frequency of clock to be set
 * @parent_rate:	Clock frequency of parent clock
 *
 * Set PLL divider to set desired rate.
 *
 * Returns:            rate which is set on success else error code
 */
static int zynqmp_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	u32 clk_id = clk->clk_id;
	const char *clk_name = clk_hw_get_name(hw);
	u32 fbdiv;
	long rate_div, frac, m, f;
	int ret;

	rate_div = (rate * FRAC_DIV) / parent_rate;
	f = rate_div % FRAC_DIV;
	zynqmp_pll_set_mode(hw, !!f);

	if (f) {
		m = rate_div / FRAC_DIV;
		m = clamp_t(u32, m, (PLL_FBDIV_MIN), (PLL_FBDIV_MAX));
		rate = parent_rate * m;
		frac = (parent_rate * f) / FRAC_DIV;

		ret = zynqmp_pm_clock_setdivider(clk_id, m);
		if (ret == -EUSERS)
			WARN(1, "More than allowed devices are using the %s, which is forbidden\n",
			     clk_name);
		else if (ret)
			pr_warn_once("%s() set divider failed for %s, ret = %d\n",
				     __func__, clk_name, ret);
		zynqmp_pm_set_pll_frac_data(clk_id, f);

		return rate + frac;
	}

	fbdiv = DIV_ROUND_CLOSEST(rate, parent_rate);
	fbdiv = clamp_t(u32, fbdiv, PLL_FBDIV_MIN, PLL_FBDIV_MAX);
	ret = zynqmp_pm_clock_setdivider(clk_id, fbdiv);
	if (ret)
		pr_warn_once("%s() set divider failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	return parent_rate * fbdiv;
}

/**
 * zynqmp_pll_is_enabled() - Check if a clock is enabled
 * @hw:		Handle between common and hardware-specific interfaces
 *
 * Return: 1 if the clock is enabled, 0 otherwise
 */
static int zynqmp_pll_is_enabled(struct clk_hw *hw)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = clk->clk_id;
	unsigned int state;
	int ret;

	ret = zynqmp_pm_clock_getstate(clk_id, &state);
	if (ret) {
		pr_warn_once("%s() clock get state failed for %s, ret = %d\n",
			     __func__, clk_name, ret);
		return -EIO;
	}

	return state ? 1 : 0;
}

/**
 * zynqmp_pll_enable() - Enable clock
 * @hw:		Handle between common and hardware-specific interfaces
 *
 * Return: 0 on success else error code
 */
static int zynqmp_pll_enable(struct clk_hw *hw)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = clk->clk_id;
	int ret;

	/*
	 * Don't skip enabling clock if there is an IOCTL_SET_PLL_FRAC_MODE request
	 * that has been sent to ATF.
	 */
	if (zynqmp_pll_is_enabled(hw) && (!clk->set_pll_mode))
		return 0;

	clk->set_pll_mode = false;

	ret = zynqmp_pm_clock_enable(clk_id);
	if (ret)
		pr_warn_once("%s() clock enable failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	return ret;
}

/**
 * zynqmp_pll_disable() - Disable clock
 * @hw:		Handle between common and hardware-specific interfaces
 */
static void zynqmp_pll_disable(struct clk_hw *hw)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = clk->clk_id;
	int ret;

	if (!zynqmp_pll_is_enabled(hw))
		return;

	ret = zynqmp_pm_clock_disable(clk_id);
	if (ret)
		pr_warn_once("%s() clock disable failed for %s, ret = %d\n",
			     __func__, clk_name, ret);
}

static const struct clk_ops zynqmp_pll_ops = {
	.enable = zynqmp_pll_enable,
	.disable = zynqmp_pll_disable,
	.is_enabled = zynqmp_pll_is_enabled,
	.round_rate = zynqmp_pll_round_rate,
	.recalc_rate = zynqmp_pll_recalc_rate,
	.set_rate = zynqmp_pll_set_rate,
};

/**
 * zynqmp_clk_register_pll() - Register PLL with the clock framework
 * @name:		PLL name
 * @clk_id:		Clock ID
 * @parents:		Name of this clock's parents
 * @num_parents:	Number of parents
 * @nodes:		Clock topology node
 *
 * Return: clock hardware to the registered clock
 */
struct clk_hw *zynqmp_clk_register_pll(const char *name, u32 clk_id,
				       const char * const *parents,
				       u8 num_parents,
				       const struct clock_topology *nodes)
{
	struct zynqmp_pll *pll;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	init.name = name;
	init.ops = &zynqmp_pll_ops;

	init.flags = zynqmp_clk_map_common_ccf_flags(nodes->flag);

	init.parent_names = parents;
	init.num_parents = 1;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->hw.init = &init;
	pll->clk_id = clk_id;

	hw = &pll->hw;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(pll);
		return ERR_PTR(ret);
	}

	clk_hw_set_rate_range(hw, PS_PLL_VCO_MIN, PS_PLL_VCO_MAX);

	return hw;
}
