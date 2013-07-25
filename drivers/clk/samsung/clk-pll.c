/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Copyright (c) 2013 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file contains the utility functions to register the pll clocks.
*/

#include <linux/errno.h>
#include "clk.h"
#include "clk-pll.h"

/*
 * PLL35xx Clock Type
 */

#define PLL35XX_MDIV_MASK       (0x3FF)
#define PLL35XX_PDIV_MASK       (0x3F)
#define PLL35XX_SDIV_MASK       (0x7)
#define PLL35XX_MDIV_SHIFT      (16)
#define PLL35XX_PDIV_SHIFT      (8)
#define PLL35XX_SDIV_SHIFT      (0)

struct samsung_clk_pll35xx {
	struct clk_hw		hw;
	const void __iomem	*con_reg;
};

#define to_clk_pll35xx(_hw) container_of(_hw, struct samsung_clk_pll35xx, hw)

static unsigned long samsung_pll35xx_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct samsung_clk_pll35xx *pll = to_clk_pll35xx(hw);
	u32 mdiv, pdiv, sdiv, pll_con;
	u64 fvco = parent_rate;

	pll_con = __raw_readl(pll->con_reg);
	mdiv = (pll_con >> PLL35XX_MDIV_SHIFT) & PLL35XX_MDIV_MASK;
	pdiv = (pll_con >> PLL35XX_PDIV_SHIFT) & PLL35XX_PDIV_MASK;
	sdiv = (pll_con >> PLL35XX_SDIV_SHIFT) & PLL35XX_SDIV_MASK;

	fvco *= mdiv;
	do_div(fvco, (pdiv << sdiv));

	return (unsigned long)fvco;
}

static const struct clk_ops samsung_pll35xx_clk_ops = {
	.recalc_rate = samsung_pll35xx_recalc_rate,
};

struct clk * __init samsung_clk_register_pll35xx(const char *name,
			const char *pname, const void __iomem *con_reg)
{
	struct samsung_clk_pll35xx *pll;
	struct clk *clk;
	struct clk_init_data init;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll) {
		pr_err("%s: could not allocate pll clk %s\n", __func__, name);
		return NULL;
	}

	init.name = name;
	init.ops = &samsung_pll35xx_clk_ops;
	init.flags = CLK_GET_RATE_NOCACHE;
	init.parent_names = &pname;
	init.num_parents = 1;

	pll->hw.init = &init;
	pll->con_reg = con_reg;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register pll clock %s\n", __func__,
				name);
		kfree(pll);
	}

	if (clk_register_clkdev(clk, name, NULL))
		pr_err("%s: failed to register lookup for %s", __func__, name);

	return clk;
}

/*
 * PLL36xx Clock Type
 */

#define PLL36XX_KDIV_MASK	(0xFFFF)
#define PLL36XX_MDIV_MASK	(0x1FF)
#define PLL36XX_PDIV_MASK	(0x3F)
#define PLL36XX_SDIV_MASK	(0x7)
#define PLL36XX_MDIV_SHIFT	(16)
#define PLL36XX_PDIV_SHIFT	(8)
#define PLL36XX_SDIV_SHIFT	(0)

struct samsung_clk_pll36xx {
	struct clk_hw		hw;
	const void __iomem	*con_reg;
};

#define to_clk_pll36xx(_hw) container_of(_hw, struct samsung_clk_pll36xx, hw)

static unsigned long samsung_pll36xx_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct samsung_clk_pll36xx *pll = to_clk_pll36xx(hw);
	u32 mdiv, pdiv, sdiv, pll_con0, pll_con1;
	s16 kdiv;
	u64 fvco = parent_rate;

	pll_con0 = __raw_readl(pll->con_reg);
	pll_con1 = __raw_readl(pll->con_reg + 4);
	mdiv = (pll_con0 >> PLL36XX_MDIV_SHIFT) & PLL36XX_MDIV_MASK;
	pdiv = (pll_con0 >> PLL36XX_PDIV_SHIFT) & PLL36XX_PDIV_MASK;
	sdiv = (pll_con0 >> PLL36XX_SDIV_SHIFT) & PLL36XX_SDIV_MASK;
	kdiv = (s16)(pll_con1 & PLL36XX_KDIV_MASK);

	fvco *= (mdiv << 16) + kdiv;
	do_div(fvco, (pdiv << sdiv));
	fvco >>= 16;

	return (unsigned long)fvco;
}

static const struct clk_ops samsung_pll36xx_clk_ops = {
	.recalc_rate = samsung_pll36xx_recalc_rate,
};

struct clk * __init samsung_clk_register_pll36xx(const char *name,
			const char *pname, const void __iomem *con_reg)
{
	struct samsung_clk_pll36xx *pll;
	struct clk *clk;
	struct clk_init_data init;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll) {
		pr_err("%s: could not allocate pll clk %s\n", __func__, name);
		return NULL;
	}

	init.name = name;
	init.ops = &samsung_pll36xx_clk_ops;
	init.flags = CLK_GET_RATE_NOCACHE;
	init.parent_names = &pname;
	init.num_parents = 1;

	pll->hw.init = &init;
	pll->con_reg = con_reg;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register pll clock %s\n", __func__,
				name);
		kfree(pll);
	}

	if (clk_register_clkdev(clk, name, NULL))
		pr_err("%s: failed to register lookup for %s", __func__, name);

	return clk;
}

/*
 * PLL45xx Clock Type
 */

#define PLL45XX_MDIV_MASK	(0x3FF)
#define PLL45XX_PDIV_MASK	(0x3F)
#define PLL45XX_SDIV_MASK	(0x7)
#define PLL45XX_MDIV_SHIFT	(16)
#define PLL45XX_PDIV_SHIFT	(8)
#define PLL45XX_SDIV_SHIFT	(0)

struct samsung_clk_pll45xx {
	struct clk_hw		hw;
	enum pll45xx_type	type;
	const void __iomem	*con_reg;
};

#define to_clk_pll45xx(_hw) container_of(_hw, struct samsung_clk_pll45xx, hw)

static unsigned long samsung_pll45xx_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct samsung_clk_pll45xx *pll = to_clk_pll45xx(hw);
	u32 mdiv, pdiv, sdiv, pll_con;
	u64 fvco = parent_rate;

	pll_con = __raw_readl(pll->con_reg);
	mdiv = (pll_con >> PLL45XX_MDIV_SHIFT) & PLL45XX_MDIV_MASK;
	pdiv = (pll_con >> PLL45XX_PDIV_SHIFT) & PLL45XX_PDIV_MASK;
	sdiv = (pll_con >> PLL45XX_SDIV_SHIFT) & PLL45XX_SDIV_MASK;

	if (pll->type == pll_4508)
		sdiv = sdiv - 1;

	fvco *= mdiv;
	do_div(fvco, (pdiv << sdiv));

	return (unsigned long)fvco;
}

static const struct clk_ops samsung_pll45xx_clk_ops = {
	.recalc_rate = samsung_pll45xx_recalc_rate,
};

struct clk * __init samsung_clk_register_pll45xx(const char *name,
			const char *pname, const void __iomem *con_reg,
			enum pll45xx_type type)
{
	struct samsung_clk_pll45xx *pll;
	struct clk *clk;
	struct clk_init_data init;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll) {
		pr_err("%s: could not allocate pll clk %s\n", __func__, name);
		return NULL;
	}

	init.name = name;
	init.ops = &samsung_pll45xx_clk_ops;
	init.flags = CLK_GET_RATE_NOCACHE;
	init.parent_names = &pname;
	init.num_parents = 1;

	pll->hw.init = &init;
	pll->con_reg = con_reg;
	pll->type = type;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register pll clock %s\n", __func__,
				name);
		kfree(pll);
	}

	if (clk_register_clkdev(clk, name, NULL))
		pr_err("%s: failed to register lookup for %s", __func__, name);

	return clk;
}

/*
 * PLL46xx Clock Type
 */

#define PLL46XX_MDIV_MASK	(0x1FF)
#define PLL46XX_PDIV_MASK	(0x3F)
#define PLL46XX_SDIV_MASK	(0x7)
#define PLL46XX_MDIV_SHIFT	(16)
#define PLL46XX_PDIV_SHIFT	(8)
#define PLL46XX_SDIV_SHIFT	(0)

#define PLL46XX_KDIV_MASK	(0xFFFF)
#define PLL4650C_KDIV_MASK	(0xFFF)
#define PLL46XX_KDIV_SHIFT	(0)

struct samsung_clk_pll46xx {
	struct clk_hw		hw;
	enum pll46xx_type	type;
	const void __iomem	*con_reg;
};

#define to_clk_pll46xx(_hw) container_of(_hw, struct samsung_clk_pll46xx, hw)

static unsigned long samsung_pll46xx_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct samsung_clk_pll46xx *pll = to_clk_pll46xx(hw);
	u32 mdiv, pdiv, sdiv, kdiv, pll_con0, pll_con1, shift;
	u64 fvco = parent_rate;

	pll_con0 = __raw_readl(pll->con_reg);
	pll_con1 = __raw_readl(pll->con_reg + 4);
	mdiv = (pll_con0 >> PLL46XX_MDIV_SHIFT) & PLL46XX_MDIV_MASK;
	pdiv = (pll_con0 >> PLL46XX_PDIV_SHIFT) & PLL46XX_PDIV_MASK;
	sdiv = (pll_con0 >> PLL46XX_SDIV_SHIFT) & PLL46XX_SDIV_MASK;
	kdiv = pll->type == pll_4650c ? pll_con1 & PLL4650C_KDIV_MASK :
					pll_con1 & PLL46XX_KDIV_MASK;

	shift = pll->type == pll_4600 ? 16 : 10;
	fvco *= (mdiv << shift) + kdiv;
	do_div(fvco, (pdiv << sdiv));
	fvco >>= shift;

	return (unsigned long)fvco;
}

static const struct clk_ops samsung_pll46xx_clk_ops = {
	.recalc_rate = samsung_pll46xx_recalc_rate,
};

struct clk * __init samsung_clk_register_pll46xx(const char *name,
			const char *pname, const void __iomem *con_reg,
			enum pll46xx_type type)
{
	struct samsung_clk_pll46xx *pll;
	struct clk *clk;
	struct clk_init_data init;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll) {
		pr_err("%s: could not allocate pll clk %s\n", __func__, name);
		return NULL;
	}

	init.name = name;
	init.ops = &samsung_pll46xx_clk_ops;
	init.flags = CLK_GET_RATE_NOCACHE;
	init.parent_names = &pname;
	init.num_parents = 1;

	pll->hw.init = &init;
	pll->con_reg = con_reg;
	pll->type = type;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register pll clock %s\n", __func__,
				name);
		kfree(pll);
	}

	if (clk_register_clkdev(clk, name, NULL))
		pr_err("%s: failed to register lookup for %s", __func__, name);

	return clk;
}

/*
 * PLL2550x Clock Type
 */

#define PLL2550X_R_MASK       (0x1)
#define PLL2550X_P_MASK       (0x3F)
#define PLL2550X_M_MASK       (0x3FF)
#define PLL2550X_S_MASK       (0x7)
#define PLL2550X_R_SHIFT      (20)
#define PLL2550X_P_SHIFT      (14)
#define PLL2550X_M_SHIFT      (4)
#define PLL2550X_S_SHIFT      (0)

struct samsung_clk_pll2550x {
	struct clk_hw		hw;
	const void __iomem	*reg_base;
	unsigned long		offset;
};

#define to_clk_pll2550x(_hw) container_of(_hw, struct samsung_clk_pll2550x, hw)

static unsigned long samsung_pll2550x_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct samsung_clk_pll2550x *pll = to_clk_pll2550x(hw);
	u32 r, p, m, s, pll_stat;
	u64 fvco = parent_rate;

	pll_stat = __raw_readl(pll->reg_base + pll->offset * 3);
	r = (pll_stat >> PLL2550X_R_SHIFT) & PLL2550X_R_MASK;
	if (!r)
		return 0;
	p = (pll_stat >> PLL2550X_P_SHIFT) & PLL2550X_P_MASK;
	m = (pll_stat >> PLL2550X_M_SHIFT) & PLL2550X_M_MASK;
	s = (pll_stat >> PLL2550X_S_SHIFT) & PLL2550X_S_MASK;

	fvco *= m;
	do_div(fvco, (p << s));

	return (unsigned long)fvco;
}

static const struct clk_ops samsung_pll2550x_clk_ops = {
	.recalc_rate = samsung_pll2550x_recalc_rate,
};

struct clk * __init samsung_clk_register_pll2550x(const char *name,
			const char *pname, const void __iomem *reg_base,
			const unsigned long offset)
{
	struct samsung_clk_pll2550x *pll;
	struct clk *clk;
	struct clk_init_data init;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll) {
		pr_err("%s: could not allocate pll clk %s\n", __func__, name);
		return NULL;
	}

	init.name = name;
	init.ops = &samsung_pll2550x_clk_ops;
	init.flags = CLK_GET_RATE_NOCACHE;
	init.parent_names = &pname;
	init.num_parents = 1;

	pll->hw.init = &init;
	pll->reg_base = reg_base;
	pll->offset = offset;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register pll clock %s\n", __func__,
				name);
		kfree(pll);
	}

	if (clk_register_clkdev(clk, name, NULL))
		pr_err("%s: failed to register lookup for %s", __func__, name);

	return clk;
}
