/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 TOSHIBA CORPORATION
 * Copyright (c) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp>
 */

#ifndef _VISCONTI_PLL_H_
#define _VISCONTI_PLL_H_

#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>

struct visconti_pll_provider {
	void __iomem *reg_base;
	struct regmap *regmap;
	struct clk_hw_onecell_data clk_data;
	struct device_node *node;
};

#define VISCONTI_PLL_RATE(_rate, _dacen, _dsmen, \
	_refdiv, _intin, _fracin, _postdiv1, _postdiv2) \
{				\
	.rate = _rate,		\
	.dacen = _dacen,	\
	.dsmen = _dsmen,	\
	.refdiv = _refdiv,	\
	.intin = _intin,	\
	.fracin = _fracin,	\
	.postdiv1 = _postdiv1,	\
	.postdiv2 = _postdiv2	\
}

struct visconti_pll_rate_table {
	unsigned long rate;
	unsigned int dacen;
	unsigned int dsmen;
	unsigned int refdiv;
	unsigned long intin;
	unsigned long fracin;
	unsigned int postdiv1;
	unsigned int postdiv2;
};

struct visconti_pll_info {
	unsigned int id;
	const char *name;
	const char *parent;
	unsigned long base_reg;
	const struct visconti_pll_rate_table *rate_table;
};

struct visconti_pll_provider * __init visconti_init_pll(struct device_node *np,
							void __iomem *base,
							unsigned long nr_plls);
void visconti_register_plls(struct visconti_pll_provider *ctx,
			    const struct visconti_pll_info *list,
			    unsigned int nr_plls, spinlock_t *lock);

#endif /* _VISCONTI_PLL_H_ */
