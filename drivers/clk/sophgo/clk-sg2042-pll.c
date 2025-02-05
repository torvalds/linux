// SPDX-License-Identifier: GPL-2.0
/*
 * Sophgo SG2042 PLL clock Driver
 *
 * Copyright (C) 2024 Sophgo Technology Inc.
 * Copyright (C) 2024 Chen Wang <unicorn_wang@outlook.com>
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include <asm/div64.h>

#include <dt-bindings/clock/sophgo,sg2042-pll.h>

#include "clk-sg2042.h"

/* Registers defined in SYS_CTRL */
#define R_PLL_BEGIN		0xC0
#define R_PLL_STAT		(0xC0 - R_PLL_BEGIN)
#define R_PLL_CLKEN_CONTROL	(0xC4 - R_PLL_BEGIN)
#define R_MPLL_CONTROL		(0xE8 - R_PLL_BEGIN)
#define R_FPLL_CONTROL		(0xF4 - R_PLL_BEGIN)
#define R_DPLL0_CONTROL		(0xF8 - R_PLL_BEGIN)
#define R_DPLL1_CONTROL		(0xFC - R_PLL_BEGIN)

/**
 * struct sg2042_pll_clock - PLL clock
 * @hw:				clk_hw for initialization
 * @id:				used to map clk_onecell_data
 * @base:			used for readl/writel.
 *				**NOTE**: PLL registers are all in SYS_CTRL!
 * @lock:			spinlock to protect register access, modification
 *				of frequency can only be served one at the time.
 * @offset_ctrl:		offset of pll control registers
 * @shift_status_lock:		shift of XXX_LOCK in pll status register
 * @shift_status_updating:	shift of UPDATING_XXX in pll status register
 * @shift_enable:		shift of XXX_CLK_EN in pll enable register
 */
struct sg2042_pll_clock {
	struct clk_hw hw;

	unsigned int id;
	void __iomem *base;
	/* protect register access */
	spinlock_t *lock;

	u32 offset_ctrl;
	u8 shift_status_lock;
	u8 shift_status_updating;
	u8 shift_enable;
};

#define to_sg2042_pll_clk(_hw) container_of(_hw, struct sg2042_pll_clock, hw)

#define KHZ 1000UL
#define MHZ (KHZ * KHZ)

#define REFDIV_MIN 1
#define REFDIV_MAX 63
#define FBDIV_MIN 16
#define FBDIV_MAX 320

#define PLL_FREF_SG2042 (25 * MHZ)

#define PLL_FOUTPOSTDIV_MIN (16 * MHZ)
#define PLL_FOUTPOSTDIV_MAX (3200 * MHZ)

#define PLL_FOUTVCO_MIN (800 * MHZ)
#define PLL_FOUTVCO_MAX (3200 * MHZ)

struct sg2042_pll_ctrl {
	unsigned long freq;
	unsigned int fbdiv;
	unsigned int postdiv1;
	unsigned int postdiv2;
	unsigned int refdiv;
};

#define PLLCTRL_FBDIV_MASK	GENMASK(27, 16)
#define PLLCTRL_POSTDIV2_MASK	GENMASK(14, 12)
#define PLLCTRL_POSTDIV1_MASK	GENMASK(10, 8)
#define PLLCTRL_REFDIV_MASK	GENMASK(5, 0)

static inline u32 sg2042_pll_ctrl_encode(struct sg2042_pll_ctrl *ctrl)
{
	return FIELD_PREP(PLLCTRL_FBDIV_MASK, ctrl->fbdiv) |
	       FIELD_PREP(PLLCTRL_POSTDIV2_MASK, ctrl->postdiv2) |
	       FIELD_PREP(PLLCTRL_POSTDIV1_MASK, ctrl->postdiv1) |
	       FIELD_PREP(PLLCTRL_REFDIV_MASK, ctrl->refdiv);
}

static inline void sg2042_pll_ctrl_decode(unsigned int reg_value,
					  struct sg2042_pll_ctrl *ctrl)
{
	ctrl->fbdiv = FIELD_GET(PLLCTRL_FBDIV_MASK, reg_value);
	ctrl->refdiv = FIELD_GET(PLLCTRL_REFDIV_MASK, reg_value);
	ctrl->postdiv1 = FIELD_GET(PLLCTRL_POSTDIV1_MASK, reg_value);
	ctrl->postdiv2 = FIELD_GET(PLLCTRL_POSTDIV2_MASK, reg_value);
}

static inline void sg2042_pll_enable(struct sg2042_pll_clock *pll, bool en)
{
	u32 value;

	if (en) {
		/* wait pll lock */
		if (readl_poll_timeout_atomic(pll->base + R_PLL_STAT,
					      value,
					      ((value >> pll->shift_status_lock) & 0x1),
					      0,
					      100000))
			pr_warn("%s not locked\n", pll->hw.init->name);

		/* wait pll updating */
		if (readl_poll_timeout_atomic(pll->base + R_PLL_STAT,
					      value,
					      !((value >> pll->shift_status_updating) & 0x1),
					      0,
					      100000))
			pr_warn("%s still updating\n", pll->hw.init->name);

		/* enable pll */
		value = readl(pll->base + R_PLL_CLKEN_CONTROL);
		writel(value | (1 << pll->shift_enable), pll->base + R_PLL_CLKEN_CONTROL);
	} else {
		/* disable pll */
		value = readl(pll->base + R_PLL_CLKEN_CONTROL);
		writel(value & (~(1 << pll->shift_enable)), pll->base + R_PLL_CLKEN_CONTROL);
	}
}

/**
 * sg2042_pll_recalc_rate() - Calculate rate for plls
 * @reg_value: current register value
 * @parent_rate: parent frequency
 *
 * This function is used to calculate below "rate" in equation
 * rate = (parent_rate/REFDIV) x FBDIV/POSTDIV1/POSTDIV2
 *      = (parent_rate x FBDIV) / (REFDIV x POSTDIV1 x POSTDIV2)
 *
 * Return: The rate calculated.
 */
static unsigned long sg2042_pll_recalc_rate(unsigned int reg_value,
					    unsigned long parent_rate)
{
	struct sg2042_pll_ctrl ctrl_table;
	u64 numerator, denominator;

	sg2042_pll_ctrl_decode(reg_value, &ctrl_table);

	numerator = (u64)parent_rate * ctrl_table.fbdiv;
	denominator = ctrl_table.refdiv * ctrl_table.postdiv1 * ctrl_table.postdiv2;
	do_div(numerator, denominator);
	return numerator;
}

/**
 * sg2042_pll_get_postdiv_1_2() - Based on input rate/prate/fbdiv/refdiv,
 * look up the postdiv1_2 table to get the closest postdiiv combination.
 * @rate: FOUTPOSTDIV
 * @prate: parent rate, i.e. FREF
 * @fbdiv: FBDIV
 * @refdiv: REFDIV
 * @postdiv1: POSTDIV1, output
 * @postdiv2: POSTDIV2, output
 *
 * postdiv1_2 contains all the possible combination lists of POSTDIV1 and POSTDIV2
 * for example:
 * postdiv1_2[0] = {2, 4, 8}, where div1 = 2, div2 = 4 , div1 * div2 = 8
 *
 * See TRM:
 * FOUTPOSTDIV = FREF * FBDIV / REFDIV / (POSTDIV1 * POSTDIV2)
 * So we get following formula to get POSTDIV1 and POSTDIV2:
 * POSTDIV = (prate/REFDIV) x FBDIV/rate
 * above POSTDIV = POSTDIV1*POSTDIV2
 *
 * Return:
 * %0 - OK
 * %-EINVAL - invalid argument, which means Failed to get the postdivs.
 */
static int sg2042_pll_get_postdiv_1_2(unsigned long rate,
				      unsigned long prate,
				      unsigned int fbdiv,
				      unsigned int refdiv,
				      unsigned int *postdiv1,
				      unsigned int *postdiv2)
{
	int index;
	u64 tmp0;

	/* POSTDIV_RESULT_INDEX point to 3rd element in the array postdiv1_2 */
	#define	POSTDIV_RESULT_INDEX	2

	static const int postdiv1_2[][3] = {
		{2, 4,  8}, {3, 3,  9}, {2, 5, 10}, {2, 6, 12},
		{2, 7, 14}, {3, 5, 15}, {4, 4, 16}, {3, 6, 18},
		{4, 5, 20}, {3, 7, 21}, {4, 6, 24}, {5, 5, 25},
		{4, 7, 28}, {5, 6, 30}, {5, 7, 35}, {6, 6, 36},
		{6, 7, 42}, {7, 7, 49}
	};

	/* prate/REFDIV and result save to tmp0 */
	tmp0 = prate;
	do_div(tmp0, refdiv);

	/* ((prate/REFDIV) x FBDIV) and result save to tmp0 */
	tmp0 *= fbdiv;

	/* ((prate/REFDIV) x FBDIV)/rate and result save to tmp0 */
	do_div(tmp0, rate);

	/* tmp0 is POSTDIV1*POSTDIV2, now we calculate div1 and div2 value */
	if (tmp0 <= 7) {
		/* (div1 * div2) <= 7, no need to use array search */
		*postdiv1 = tmp0;
		*postdiv2 = 1;
		return 0;
	}

	/* (div1 * div2) > 7, use array search */
	for (index = 0; index < ARRAY_SIZE(postdiv1_2); index++) {
		if (tmp0 > postdiv1_2[index][POSTDIV_RESULT_INDEX]) {
			continue;
		} else {
			/* found it */
			*postdiv1 = postdiv1_2[index][1];
			*postdiv2 = postdiv1_2[index][0];
			return 0;
		}
	}
	pr_warn("%s can not find in postdiv array!\n", __func__);
	return -EINVAL;
}

/**
 * sg2042_get_pll_ctl_setting() - Based on the given FOUTPISTDIV and the input
 * FREF to calculate the REFDIV/FBDIV/PSTDIV1/POSTDIV2 combination for pllctrl
 * register.
 * @req_rate: expected output clock rate, i.e. FOUTPISTDIV
 * @parent_rate: input parent clock rate, i.e. FREF
 * @best: output to hold calculated combination of REFDIV/FBDIV/PSTDIV1/POSTDIV2
 *
 * Return:
 * %0 - OK
 * %-EINVAL - invalid argument
 */
static int sg2042_get_pll_ctl_setting(struct sg2042_pll_ctrl *best,
				      unsigned long req_rate,
				      unsigned long parent_rate)
{
	unsigned int fbdiv, refdiv, postdiv1, postdiv2;
	unsigned long foutpostdiv;
	u64 foutvco;
	int ret;
	u64 tmp;

	if (parent_rate != PLL_FREF_SG2042) {
		pr_err("INVALID FREF: %ld\n", parent_rate);
		return -EINVAL;
	}

	if (req_rate < PLL_FOUTPOSTDIV_MIN || req_rate > PLL_FOUTPOSTDIV_MAX) {
		pr_alert("INVALID FOUTPOSTDIV: %ld\n", req_rate);
		return -EINVAL;
	}

	memset(best, 0, sizeof(struct sg2042_pll_ctrl));

	for (refdiv = REFDIV_MIN; refdiv < REFDIV_MAX + 1; refdiv++) {
		/* required by hardware: FREF/REFDIV must > 10 */
		tmp = parent_rate;
		do_div(tmp, refdiv);
		if (tmp <= 10)
			continue;

		for (fbdiv = FBDIV_MIN; fbdiv < FBDIV_MAX + 1; fbdiv++) {
			/*
			 * FOUTVCO = FREF*FBDIV/REFDIV validation
			 * required by hardware, FOUTVCO must [800MHz, 3200MHz]
			 */
			foutvco = parent_rate * fbdiv;
			do_div(foutvco, refdiv);
			if (foutvco < PLL_FOUTVCO_MIN || foutvco > PLL_FOUTVCO_MAX)
				continue;

			ret = sg2042_pll_get_postdiv_1_2(req_rate, parent_rate,
							 fbdiv, refdiv,
							 &postdiv1, &postdiv2);
			if (ret)
				continue;

			/*
			 * FOUTPOSTDIV = FREF*FBDIV/REFDIV/(POSTDIV1*POSTDIV2)
			 *             = FOUTVCO/(POSTDIV1*POSTDIV2)
			 */
			tmp = foutvco;
			do_div(tmp, (postdiv1 * postdiv2));
			foutpostdiv = (unsigned long)tmp;
			/* Iterative to approach the expected value */
			if (abs_diff(foutpostdiv, req_rate) < abs_diff(best->freq, req_rate)) {
				best->freq = foutpostdiv;
				best->refdiv = refdiv;
				best->fbdiv = fbdiv;
				best->postdiv1 = postdiv1;
				best->postdiv2 = postdiv2;
				if (foutpostdiv == req_rate)
					return 0;
			}
			continue;
		}
	}

	if (best->freq == 0)
		return -EINVAL;
	else
		return 0;
}

/**
 * sg2042_clk_pll_recalc_rate() - recalc_rate callback for pll clks
 * @hw: ccf use to hook get sg2042_pll_clock
 * @parent_rate: parent rate
 *
 * The is function will be called through clk_get_rate
 * and return current rate after decoding reg value
 *
 * Return: Current rate recalculated.
 */
static unsigned long sg2042_clk_pll_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct sg2042_pll_clock *pll = to_sg2042_pll_clk(hw);
	unsigned long rate;
	u32 value;

	value = readl(pll->base + pll->offset_ctrl);
	rate = sg2042_pll_recalc_rate(value, parent_rate);

	pr_debug("--> %s: pll_recalc_rate: val = %ld\n",
		 clk_hw_get_name(hw), rate);
	return rate;
}

static long sg2042_clk_pll_round_rate(struct clk_hw *hw,
				      unsigned long req_rate,
				      unsigned long *prate)
{
	struct sg2042_pll_ctrl pctrl_table;
	unsigned int value;
	long proper_rate;
	int ret;

	ret = sg2042_get_pll_ctl_setting(&pctrl_table, req_rate, *prate);
	if (ret) {
		proper_rate = 0;
		goto out;
	}

	value = sg2042_pll_ctrl_encode(&pctrl_table);
	proper_rate = (long)sg2042_pll_recalc_rate(value, *prate);

out:
	pr_debug("--> %s: pll_round_rate: val = %ld\n",
		 clk_hw_get_name(hw), proper_rate);
	return proper_rate;
}

static int sg2042_clk_pll_determine_rate(struct clk_hw *hw,
					 struct clk_rate_request *req)
{
	req->rate = sg2042_clk_pll_round_rate(hw, min(req->rate, req->max_rate),
					      &req->best_parent_rate);
	pr_debug("--> %s: pll_determine_rate: val = %ld\n",
		 clk_hw_get_name(hw), req->rate);
	return 0;
}

static int sg2042_clk_pll_set_rate(struct clk_hw *hw,
				   unsigned long rate,
				   unsigned long parent_rate)
{
	struct sg2042_pll_clock *pll = to_sg2042_pll_clk(hw);
	struct sg2042_pll_ctrl pctrl_table;
	unsigned long flags;
	u32 value = 0;
	int ret;

	spin_lock_irqsave(pll->lock, flags);

	sg2042_pll_enable(pll, 0);

	ret = sg2042_get_pll_ctl_setting(&pctrl_table, rate, parent_rate);
	if (ret) {
		pr_warn("%s: Can't find a proper pll setting\n", pll->hw.init->name);
		goto out;
	}

	value = sg2042_pll_ctrl_encode(&pctrl_table);

	/* write the value to top register */
	writel(value, pll->base + pll->offset_ctrl);

out:
	sg2042_pll_enable(pll, 1);

	spin_unlock_irqrestore(pll->lock, flags);

	pr_debug("--> %s: pll_set_rate: val = 0x%x\n",
		 clk_hw_get_name(hw), value);
	return ret;
}

static const struct clk_ops sg2042_clk_pll_ops = {
	.recalc_rate = sg2042_clk_pll_recalc_rate,
	.round_rate = sg2042_clk_pll_round_rate,
	.determine_rate = sg2042_clk_pll_determine_rate,
	.set_rate = sg2042_clk_pll_set_rate,
};

static const struct clk_ops sg2042_clk_pll_ro_ops = {
	.recalc_rate = sg2042_clk_pll_recalc_rate,
	.round_rate = sg2042_clk_pll_round_rate,
};

/*
 * Clock initialization macro naming rules:
 * FW: use CLK_HW_INIT_FW_NAME
 * RO: means Read-Only
 */
#define SG2042_PLL_FW(_id, _name, _parent, _r_ctrl, _shift)		\
	{								\
		.id = _id,						\
		.hw.init = CLK_HW_INIT_FW_NAME(				\
				_name,					\
				_parent,				\
				&sg2042_clk_pll_ops,			\
				CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE),\
		.offset_ctrl = _r_ctrl,					\
		.shift_status_lock = 8 + (_shift),			\
		.shift_status_updating = _shift,			\
		.shift_enable = _shift,					\
	}

#define SG2042_PLL_FW_RO(_id, _name, _parent, _r_ctrl, _shift)		\
	{								\
		.id = _id,						\
		.hw.init = CLK_HW_INIT_FW_NAME(				\
				_name,					\
				_parent,				\
				&sg2042_clk_pll_ro_ops,			\
				CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE),\
		.offset_ctrl = _r_ctrl,					\
		.shift_status_lock = 8 + (_shift),			\
		.shift_status_updating = _shift,			\
		.shift_enable = _shift,					\
	}

static struct sg2042_pll_clock sg2042_pll_clks[] = {
	SG2042_PLL_FW(MPLL_CLK, "mpll_clock", "cgi_main", R_MPLL_CONTROL, 0),
	SG2042_PLL_FW_RO(FPLL_CLK, "fpll_clock", "cgi_main", R_FPLL_CONTROL, 3),
	SG2042_PLL_FW_RO(DPLL0_CLK, "dpll0_clock", "cgi_dpll0", R_DPLL0_CONTROL, 4),
	SG2042_PLL_FW_RO(DPLL1_CLK, "dpll1_clock", "cgi_dpll1", R_DPLL1_CONTROL, 5),
};

static DEFINE_SPINLOCK(sg2042_clk_lock);

static int sg2042_clk_register_plls(struct device *dev,
				    struct sg2042_clk_data *clk_data,
				    struct sg2042_pll_clock pll_clks[],
				    int num_pll_clks)
{
	struct sg2042_pll_clock *pll;
	struct clk_hw *hw;
	int i, ret = 0;

	for (i = 0; i < num_pll_clks; i++) {
		pll = &pll_clks[i];
		/* assign these for ops usage during registration */
		pll->base = clk_data->iobase;
		pll->lock = &sg2042_clk_lock;

		hw = &pll->hw;
		ret = devm_clk_hw_register(dev, hw);
		if (ret) {
			pr_err("failed to register clock %s\n", pll->hw.init->name);
			break;
		}

		clk_data->onecell_data.hws[pll->id] = hw;
	}

	return ret;
}

static int sg2042_init_clkdata(struct platform_device *pdev,
			       int num_clks,
			       struct sg2042_clk_data **pp_clk_data)
{
	struct sg2042_clk_data *clk_data;

	clk_data = devm_kzalloc(&pdev->dev,
				struct_size(clk_data, onecell_data.hws, num_clks),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->iobase = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(clk_data->iobase)))
		return PTR_ERR(clk_data->iobase);

	clk_data->onecell_data.num = num_clks;

	*pp_clk_data = clk_data;

	return 0;
}

static int sg2042_pll_probe(struct platform_device *pdev)
{
	struct sg2042_clk_data *clk_data = NULL;
	int num_clks;
	int ret;

	num_clks = ARRAY_SIZE(sg2042_pll_clks);

	ret = sg2042_init_clkdata(pdev, num_clks, &clk_data);
	if (ret)
		goto error_out;

	ret = sg2042_clk_register_plls(&pdev->dev, clk_data, sg2042_pll_clks,
				       num_clks);
	if (ret)
		goto error_out;

	return devm_of_clk_add_hw_provider(&pdev->dev,
					   of_clk_hw_onecell_get,
					   &clk_data->onecell_data);

error_out:
	pr_err("%s failed error number %d\n", __func__, ret);
	return ret;
}

static const struct of_device_id sg2042_pll_match[] = {
	{ .compatible = "sophgo,sg2042-pll" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sg2042_pll_match);

static struct platform_driver sg2042_pll_driver = {
	.probe = sg2042_pll_probe,
	.driver = {
		.name = "clk-sophgo-sg2042-pll",
		.of_match_table = sg2042_pll_match,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(sg2042_pll_driver);

MODULE_AUTHOR("Chen Wang");
MODULE_DESCRIPTION("Sophgo SG2042 pll clock driver");
MODULE_LICENSE("GPL");
