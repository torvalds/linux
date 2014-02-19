/*
 * Copyright (C) 2013 ROCKCHIP, Inc.
 * Author: chenxing <chenxing@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk-private.h>
#include <asm/io.h>

#include "clk-ops.h"
#include "clk-pll.h"


struct rkclk_divmap_table {
	u32		reg_val;
	u32		div_val;
};

struct rkclk_divinfo {
	struct clk_divider *div;
	void __iomem	*addr;
	u32		shift;
	u32		width;
	u32		div_type;
	u32		max_div;
	u32		fixed_div_val;
	u32		clkops_idx;
	const char	*clk_name;
	const char	*parent_name;
	struct clk_div_table		*div_table;
	struct list_head		node;
};

struct rkclk_muxinfo {
	struct clk_mux	*mux;
	void __iomem	*addr;
	u32		shift;
	u32		width;
	u32		parent_num;
	u32		clkops_idx;
	//u8		mux_flags;
	const char	*clk_name;
	const char	**parent_names;
	struct list_head	node;
};

struct rkclk_fracinfo {
	struct clk_hw	hw;
	void __iomem	*addr;
	u32		shift;
	u32		width;
	u32		frac_type;
	u32		clkops_idx;
	const char	*clk_name;
	const char	*parent_name;
	struct list_head	node;
};

struct rkclk_gateinfo {
	struct clk_gate	*gate;
	void __iomem	*addr;
	u32		shift;
	u32		clkops_idx;
	const char	*clk_name;
	const char	*parent_name;
};

struct rkclk_pllinfo {
	struct clk_hw	hw;
	void __iomem	*addr;
	u32		width;
	const char	*clk_name;
	const char	*parent_name;
	/*
	 * const char	**clkout_names;
	 */
	u32		clkops_idx;
	u32		id;
	struct list_head	node;
};

struct rkclk {
	const char	*clk_name;
	u32		clk_type;
	u32		flags;
	/*
	 * store nodes creat this rkclk
	 * */
	struct device_node		*np;
	struct rkclk_pllinfo		*pll_info;
	struct rkclk_muxinfo		*mux_info;
	struct rkclk_divinfo		*div_info;
	struct rkclk_fracinfo		*frac_info;
	struct rkclk_gateinfo		*gate_info;
	struct list_head		node;
};

static DEFINE_SPINLOCK(clk_lock);
LIST_HEAD(rk_clks);

#define RKCLK_PLL_TYPE	(1 << 0)
#define RKCLK_MUX_TYPE	(1 << 1)
#define RKCLK_DIV_TYPE	(1 << 2)
#define RKCLK_FRAC_TYPE	(1 << 3)
#define RKCLK_GATE_TYPE	(1 << 4)

static int rkclk_init_muxinfo(struct device_node *np,
		struct rkclk_muxinfo *mux, void __iomem *addr)
{
	int cnt, i, ret = 0;
	u8 found = 0;
	struct rkclk *rkclk;
	u32 flags;

	mux = kzalloc(sizeof(struct rkclk_muxinfo), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	ret = of_property_read_u32(np, "rockchip,flags", &flags);
	if (ret != 0)
		flags = 0;
	/*
	 * Get control bit addr
	 */
	ret = of_property_read_u32_index(np, "rockchip,bits", 0, &mux->shift);
	if (ret != 0)
		return -EINVAL;

	ret = of_property_read_u32(np, "rockchip,clkops-idx", &mux->clkops_idx);
	if (ret != 0)
		mux->clkops_idx = CLKOPS_TABLE_END;

	ret = of_property_read_u32_index(np, "rockchip,bits", 1, &mux->width);
	if (ret != 0)
		return -EINVAL;
	mux->addr = addr;

	ret = of_property_read_string(np, "clock-output-names", &mux->clk_name);
	if (ret != 0)
		return -EINVAL;

	/*
	 * Get parents' cnt
	 */
	cnt = of_count_phandle_with_args(np, "clocks", "#clock-cells");
	if (cnt< 0)
		return -EINVAL;

	mux->parent_num = cnt;
	mux->parent_names = kzalloc(cnt * sizeof(char *), GFP_KERNEL);

	clk_debug("%s: parent cnt = %d\n", __func__, cnt);
	for (i = 0; i < cnt ; i++) {

		mux->parent_names[i] = of_clk_get_parent_name(np, i);
	}

	found = 0;
	list_for_each_entry(rkclk, &rk_clks, node) {
		if (strcmp(mux->clk_name, rkclk->clk_name) == 0) {
			if (rkclk->mux_info != NULL)
				clk_err("%s(%d): This clk(%s) has been used\n",
						__func__, __LINE__, mux->clk_name);
			clk_debug("%s: find match %s\n", __func__, rkclk->clk_name);
			found = 1;
			rkclk->mux_info = mux;
			rkclk->clk_type |= RKCLK_MUX_TYPE;
			rkclk->flags |= flags;
			break;
		}
	}
	if (!found) {
		rkclk = kzalloc(sizeof(struct rkclk), GFP_KERNEL);
		rkclk->clk_name = mux->clk_name;
		rkclk->mux_info = mux;
		rkclk->clk_type = RKCLK_MUX_TYPE;
		rkclk->flags = flags;
		rkclk->np = np;
		clk_debug("%s: creat %s\n", __func__, rkclk->clk_name);

		list_add_tail(&rkclk->node, &rk_clks);
	}
	return 0;
}

static int rkclk_init_divinfo(struct device_node *np,
		struct rkclk_divinfo *div, void __iomem *addr)
{
	int cnt = 0, i = 0, ret = 0;
	struct rkclk *rkclk;
	u8 found = 0;

	div = kzalloc(sizeof(struct rkclk_divinfo), GFP_KERNEL);
	if (!div)
		return -ENOMEM;

	of_property_read_u32_index(np, "rockchip,bits", 0, &div->shift);
	of_property_read_u32_index(np, "rockchip,bits", 1, &div->width);
	div->addr = addr;

	of_property_read_u32(np, "rockchip,div-type", &div->div_type);

	ret = of_property_read_u32(np, "rockchip,clkops-idx", &div->clkops_idx);
	if (ret != 0)
		div->clkops_idx = CLKOPS_TABLE_END;

	cnt = of_property_count_strings(np, "clock-output-names");
	if (cnt <= 0)
		div->clk_name = of_clk_get_parent_name(np, 0);
	else {
		ret = of_property_read_string(np, "clock-output-names", &div->clk_name);
		if (ret != 0)
			return -EINVAL;
		div->parent_name = of_clk_get_parent_name(np, 0);
	}

	switch (div->div_type) {
		case CLK_DIVIDER_PLUS_ONE:
		case CLK_DIVIDER_ONE_BASED:
		case CLK_DIVIDER_POWER_OF_TWO:
			break;
		case CLK_DIVIDER_FIXED:
			of_property_read_u32_index(np, "rockchip,div-relations", 0,
					&div->fixed_div_val);
			clk_debug("%s:%s fixed_div = %d\n", __func__,
					div->clk_name, div->fixed_div_val);
			break;
		case CLK_DIVIDER_USER_DEFINE:
			of_get_property(np, "rockchip,div-relations", &cnt);
			cnt /= 4 * 2;
			div->div_table = kzalloc(cnt * sizeof(struct clk_div_table),
					GFP_KERNEL);

			for (i = 0; i < cnt; i++) {
				of_property_read_u32_index(np, "rockchip,div-relations", i * 2,
						&div->div_table[i].val);
				of_property_read_u32_index(np, "rockchip,div-relations", i * 2 + 1,
						&div->div_table[i].div);
				clk_debug("\tGet div table %d: val=%d, div=%d\n",
						i, div->div_table[i].val,
						div->div_table[i].div);
			}
			break;
		default:
			clk_err("%s: %s: unknown rockchip,div-type, please check dtsi\n",
					__func__, div->clk_name);
			break;
	}

	found = 0;
	list_for_each_entry(rkclk, &rk_clks, node) {
		if (strcmp(div->clk_name, rkclk->clk_name) == 0) {
			if (rkclk->div_info != NULL)
				clk_err("%s(Line %d): This clk(%s) has been used\n",
						__func__, __LINE__, rkclk->clk_name);
			clk_debug("%s: find match %s\n", __func__, rkclk->clk_name);
			found = 1;
			rkclk->div_info = div;
			rkclk->clk_type |= RKCLK_DIV_TYPE;
			break;
		}
	}
	if (!found) {
		rkclk = kzalloc(sizeof(struct rkclk), GFP_KERNEL);
		rkclk->clk_name = div->clk_name;
		rkclk->div_info = div;
		rkclk->clk_type |= RKCLK_DIV_TYPE;
		rkclk->np = np;
		clk_debug("%s: creat %s\n", __func__, rkclk->clk_name);

		list_add_tail(&rkclk->node, &rk_clks);
	}
	return 0;


}

static int rkclk_init_fracinfo(struct device_node *np,
		struct rkclk_fracinfo *frac, void __iomem *addr)
{
	struct rkclk *rkclk;
	u8 found = 0;
	int ret = 0;

	frac = kzalloc(sizeof(struct rkclk_fracinfo), GFP_KERNEL);
	if (!frac)
		return -ENOMEM;

	of_property_read_u32_index(np, "rockchip,bits", 0, &frac->shift);
	of_property_read_u32_index(np, "rockchip,bits", 1, &frac->width);
	frac->addr = addr;

	ret = of_property_read_u32(np, "rockchip,clkops-idx", &frac->clkops_idx);
	if (ret != 0)
		frac->clkops_idx = CLKOPS_TABLE_END;

	frac->parent_name = of_clk_get_parent_name(np, 0);
	ret = of_property_read_string(np, "clock-output-names", &frac->clk_name);
	if (ret != 0)
		return -EINVAL;

	found = 0;
	list_for_each_entry(rkclk, &rk_clks, node) {
		if (strcmp(frac->clk_name, rkclk->clk_name) == 0) {
			if (rkclk->frac_info != NULL)
				clk_err("%s(%d): This clk(%s) has been used\n",
						__func__, __LINE__, frac->clk_name);
			clk_debug("%s: find match %s\n", __func__, rkclk->clk_name);
			found = 1;
			rkclk->frac_info = frac;
			rkclk->clk_type |= RKCLK_FRAC_TYPE;
			rkclk->flags |= CLK_SET_RATE_PARENT;
			break;
		}
	}
	if (!found) {
		rkclk = kzalloc(sizeof(struct rkclk), GFP_KERNEL);
		rkclk->clk_name = frac->clk_name;
		rkclk->frac_info = frac;
		rkclk->clk_type = RKCLK_FRAC_TYPE;
		rkclk->flags = CLK_SET_RATE_PARENT;
		rkclk->np = np;
		clk_debug("%s: creat %s\n", __func__, rkclk->clk_name);

		list_add_tail(&rkclk->node, &rk_clks);
	}
	return 0;
}

static int __init rkclk_init_selcon(struct device_node *np)
{
	struct device_node *node_con, *node;
	void __iomem *reg = 0;

	struct rkclk_divinfo *divinfo;
	struct rkclk_muxinfo *muxinfo;
	struct rkclk_fracinfo *fracinfo;

	for_each_available_child_of_node(np, node_con) {

		reg = of_iomap(node_con, 0);

		for_each_available_child_of_node(node_con, node) {

			if (of_device_is_compatible(node, "rockchip,rk3188-div-con"))
				rkclk_init_divinfo(node, divinfo, reg);

			else if (of_device_is_compatible(node, "rockchip,rk3188-mux-con"))
				rkclk_init_muxinfo(node, muxinfo, reg);

			else if (of_device_is_compatible(node, "rockchip,rk3188-frac-con"))
				rkclk_init_fracinfo(node, fracinfo, reg);

			else if (of_device_is_compatible(node, "rockchip,rk3188-inv-con"))
				clk_debug("INV clk\n");

			else
				clk_err("%s: unknown controler type, plz check dtsi "
						"or add type support\n", __func__);

		}
	}
	return 0;
}

static int __init rkclk_init_gatecon(struct device_node *np)
{
	struct clk_onecell_data *clk_data;
	struct device_node *node;
	const char *clk_parent;
	const char *clk_name;
	void __iomem *reg;
	void __iomem *reg_idx;
	int cnt;
	int reg_bit;
	int i;
	struct rkclk_gateinfo *gateinfo;
	u8 found = 0;
	struct rkclk *rkclk;

	for_each_available_child_of_node(np, node) {
		cnt = of_property_count_strings(node, "clock-output-names");
		if (cnt < 0) {
			clk_err("%s: error in clock-output-names %d\n",
					__func__, cnt);
			continue;
		}

		if (cnt == 0) {
			pr_info("%s: nothing to do\n", __func__);
			continue;
		}

		reg = of_iomap(node, 0);

		clk_data = kzalloc(sizeof(struct clk_onecell_data), GFP_KERNEL);
		if (!clk_data)
			return -ENOMEM;

		clk_data->clks = kzalloc(cnt * sizeof(struct clk *), GFP_KERNEL);
		if (!clk_data->clks) {
			kfree(clk_data);
			return -ENOMEM;
		}

		for (i = 0; i < cnt; i++) {
			of_property_read_string_index(node, "clock-output-names",
					i, &clk_name);

			/* ignore empty slots */
			if (!strcmp("reserved", clk_name))
				continue;

			clk_parent = of_clk_get_parent_name(node, i);

			reg_idx = reg + (4 * (i / 16));
			reg_bit = (i % 16);

			gateinfo = kzalloc(sizeof(struct rkclk_gateinfo), GFP_KERNEL);
			gateinfo->clk_name = clk_name;
			gateinfo->parent_name = clk_parent;
			gateinfo->addr = reg;
			gateinfo->shift = reg_bit;
			found = 0;
			list_for_each_entry(rkclk, &rk_clks, node) {
				if (strcmp(clk_name, rkclk->clk_name) == 0) {
					if (rkclk->gate_info != NULL)
						clk_err("%s(%d): This clk(%s) has been used\n",
								__func__, __LINE__, clk_name);
					clk_debug("%s: find match %s\n", __func__, rkclk->clk_name);
					found = 1;
					rkclk->gate_info = gateinfo;
					rkclk->clk_type |= RKCLK_GATE_TYPE;
					break;
				}
			}
			if (!found) {
				rkclk = kzalloc(sizeof(struct rkclk), GFP_KERNEL);
				rkclk->clk_name = gateinfo->clk_name;
				rkclk->gate_info = gateinfo;
				rkclk->clk_type |= RKCLK_GATE_TYPE;
				rkclk->np = node;
				clk_debug("%s: creat %s\n", __func__, rkclk->clk_name);

				list_add_tail(&rkclk->node, &rk_clks);
			}
		}

	}
	return 0;
}

static int __init rkclk_init_pllcon(struct device_node *np)
{
	struct rkclk_pllinfo *pllinfo;
	struct device_node *node;
	struct rkclk *rkclk;
	void __iomem	*reg;
	int i = 0;
	int ret = 0, clknum = 0;
	u8 found = 0;

	for_each_available_child_of_node(np, node) {
		clknum = of_property_count_strings(node, "clock-output-names");
		if (clknum < 0) {
			clk_err("%s: error in get clock-output-names numbers = %d\n",
					__func__, clknum);
			return -EINVAL;
		}
		reg = of_iomap(node, 0);

		for (i = 0; i < clknum; i++) {
			pllinfo = kzalloc(sizeof(struct rkclk_pllinfo), GFP_KERNEL);
			if (!pllinfo)
				return -ENOMEM;

			/*
			 * Get pll parent name
			 */
			pllinfo->parent_name = of_clk_get_parent_name(node, i);

			/*
			 * Get pll output name
			 */
			of_property_read_string_index(node, "clock-output-names",
					i, &pllinfo->clk_name);

			pllinfo->addr = reg;

			ret = of_property_read_u32_index(node, "reg", 1, &pllinfo->width);
			if (ret != 0) {
				clk_err("%s: can not get reg info\n", __func__);
			}

			ret = of_property_read_u32(node, "rockchip,pll-id", &pllinfo->id);
			if (ret != 0) {
				clk_err("%s: can not get pll-id\n", __func__);
			}

			clk_debug("%s: parent=%s, pllname=%s, reg =%08x, id = %d, cnt=%d\n",
					__func__,pllinfo->parent_name,
					pllinfo->clk_name,(u32)pllinfo->addr,
					pllinfo->id,pllinfo->width);

			found = 0;
			list_for_each_entry(rkclk, &rk_clks, node) {
				if (strcmp(pllinfo->clk_name, rkclk->clk_name) == 0) {
					if (rkclk->pll_info != NULL)
						clk_err("%s(%d): This clk(%s) has been used\n",
								__func__, __LINE__, pllinfo->clk_name);
					clk_debug("%s: find match %s\n", __func__, rkclk->clk_name);
					found = 1;
					rkclk->pll_info = pllinfo;
					rkclk->clk_type |= RKCLK_PLL_TYPE;
					break;
				}
			}
			if (!found) {
				rkclk = kzalloc(sizeof(struct rkclk), GFP_KERNEL);
				rkclk->clk_name = pllinfo->clk_name;
				rkclk->pll_info = pllinfo;
				rkclk->clk_type |= RKCLK_PLL_TYPE;
				rkclk->np = node;

				list_add_tail(&rkclk->node, &rk_clks);
			}
		}
	}

	return 0;
}

static unsigned long clk_div_special_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return parent_rate;
}
static long clk_div_special_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return rate;
}
static int clk_div_special_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	return 0;
}

// For fixed div clks and For user defined div clk
const struct clk_ops clk_div_special_ops = {
	.recalc_rate = clk_div_special_recalc_rate,
	.round_rate = clk_div_special_round_rate,
	.set_rate = clk_div_special_set_rate,
};

static int rkclk_register(struct rkclk *rkclk)
{
	struct clk_mux		*mux = NULL;
	struct clk_divider	*div = NULL;
	struct clk_gate		*gate = NULL;
	struct clk_pll		*pll = NULL;

	const struct clk_ops	*rate_ops = NULL;
	const struct clk_ops	*mux_ops = NULL;

	struct clk		*clk = NULL;
	const char		**parent_names = NULL;
	struct clk_hw		*rate_hw;
	int			parent_num;


	clk_debug("%s >>>>>start: clk_name=%s, clk_type=%x\n",
			__func__, rkclk->clk_name, rkclk->clk_type);

	if (rkclk->clk_type & RKCLK_PLL_TYPE) {
		pll = kzalloc(sizeof(struct clk_pll), GFP_KERNEL);
		rate_ops = &clk_pll_ops;
		pll->reg = rkclk->pll_info->addr;
		//pll->shift = 0;
		pll->width = rkclk->pll_info->width;
		pll->id = rkclk->pll_info->id;
		rate_hw = &pll->hw;

		parent_num = 1;
		parent_names = &rkclk->pll_info->parent_name;

	} else if (rkclk->clk_type & RKCLK_FRAC_TYPE) {
		div = kzalloc(sizeof(struct clk_divider), GFP_KERNEL);
		div->reg = rkclk->frac_info->addr;
		div->shift = (u8)rkclk->frac_info->shift;
		div->width = rkclk->frac_info->width;
		div->flags = CLK_DIVIDER_HIWORD_MASK;

		rate_hw = &div->hw;
		rate_ops = rk_get_clkops(rkclk->frac_info->clkops_idx);

		parent_num = 1;
		parent_names = &rkclk->frac_info->parent_name;

	} else if (rkclk->clk_type & RKCLK_DIV_TYPE) {
		div = kzalloc(sizeof(struct clk_divider), GFP_KERNEL);
		if (rkclk->div_info->clkops_idx != CLKOPS_TABLE_END)
			rate_ops = rk_get_clkops(rkclk->div_info->clkops_idx);
		else
			rate_ops = &clk_divider_ops;
		div->reg = rkclk->div_info->addr;
		div->shift = (u8)rkclk->div_info->shift;
		div->width = rkclk->div_info->width;
		div->flags = CLK_DIVIDER_HIWORD_MASK | rkclk->div_info->div_type;
		rate_hw = &div->hw;
		if (rkclk->div_info->div_table)
			div->table = rkclk->div_info->div_table;

		parent_num = 1;
		parent_names = &rkclk->div_info->parent_name;
		if (rkclk->clk_type != (rkclk->clk_type & CLK_DIVIDER_MASK)) {
			// FIXME: fixed div add here
			clk_err("%s: %d, unknown clk_type=%x\n",
					__func__, __LINE__, rkclk->clk_type);

		}
	}

	if (rkclk->clk_type & RKCLK_MUX_TYPE) {
		mux = kzalloc(sizeof(struct clk_mux), GFP_KERNEL);
		mux->reg = rkclk->mux_info->addr;
		mux->shift = (u8)rkclk->mux_info->shift;
		mux->mask = (1 << rkclk->mux_info->width) - 1;
		mux->flags = CLK_MUX_HIWORD_MASK;
		mux_ops = &clk_mux_ops;
		if (rkclk->mux_info->clkops_idx != CLKOPS_TABLE_END) {
			rate_hw = kzalloc(sizeof(struct clk_hw), GFP_KERNEL);
			rate_ops = rk_get_clkops(rkclk->mux_info->clkops_idx);
		}

		parent_num = rkclk->mux_info->parent_num;
		parent_names = rkclk->mux_info->parent_names;
	}

	if (rkclk->clk_type & RKCLK_GATE_TYPE) {
		gate = kzalloc(sizeof(struct clk_gate), GFP_KERNEL);
		gate->reg = rkclk->gate_info->addr;
		gate->bit_idx = rkclk->gate_info->shift;
		gate->flags = CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE;

	}

	// FIXME: flag(CLK_IGNORE_UNUSED) may need an input argument
	if (rkclk->clk_type == RKCLK_MUX_TYPE
			&& rkclk->mux_info->clkops_idx == CLKOPS_TABLE_END) {
		clk_debug("use clk_register_mux\n");
		clk = clk_register_mux(NULL, rkclk->clk_name,
				rkclk->mux_info->parent_names,
				(u8)rkclk->mux_info->parent_num,
				rkclk->flags, mux->reg, mux->shift, mux->mask,
				mux->flags, &clk_lock);
	} else if (rkclk->clk_type == RKCLK_DIV_TYPE) {
		clk_debug("use clk_register_divider\n");
		clk = clk_register_divider(NULL, rkclk->clk_name,
				rkclk->div_info->parent_name,
				rkclk->flags, div->reg, div->shift,
				div->width, div->flags, &clk_lock);
	} else if (rkclk->clk_type == RKCLK_GATE_TYPE) {
		clk_debug("use clk_register_gate\n");
		clk = clk_register_gate(NULL, rkclk->clk_name,
				rkclk->gate_info->parent_name,
				rkclk->flags, gate->reg,
				gate->bit_idx,
				gate->flags, &clk_lock);
	} else if (rkclk->clk_type == RKCLK_PLL_TYPE) {
		clk_debug("use rk_clk_register_pll\n");
		clk = rk_clk_register_pll(NULL, rkclk->clk_name,
				rkclk->pll_info->parent_name,
				rkclk->flags, pll->reg, pll->width,
				pll->id, &clk_lock);
	} else {
		clk_debug("use clk_register_composite\n");
		clk = clk_register_composite(NULL, rkclk->clk_name,
				parent_names, parent_num,
				mux ? &mux->hw : NULL, mux ? mux_ops : NULL,
				rate_hw, rate_ops,
				gate ? &gate->hw : NULL, gate ? &clk_gate_ops : NULL,
				rkclk->flags);
	}

	if (clk) {
		clk_debug("clk name=%s, flags=0x%lx\n", clk->name, clk->flags);
		clk_register_clkdev(clk, rkclk->clk_name, NULL);
	} else {
		clk_err("%s: clk(\"%s\") register clk error\n",
				__func__, rkclk->clk_name);
	}

	return 0;
}

static int rkclk_add_provider(struct device_node *np)
{
	int i, cnt, ret = 0;
	const char *name = NULL;
	struct clk *clk = NULL;
	struct clk_onecell_data *clk_data = NULL;


	clk_debug("\n");

	cnt = of_property_count_strings(np, "clock-output-names");
	if (cnt < 0) {
		clk_err("%s: error in clock-output-names, cnt=%d\n", __func__,
				cnt);
		return -EINVAL;
	}

	clk_debug("%s: cnt = %d\n", __func__, cnt);

	if (cnt == 0) {
		clk_debug("%s: nothing to do\n", __func__);
		return 0;
	}

	if (cnt == 1) {
		of_property_read_string(np, "clock-output-names", &name);
		clk_debug("clock-output-names = %s\n", name);

		clk = clk_get_sys(NULL, name);
		if (IS_ERR(clk)) {
			clk_err("%s: fail to get %s\n", __func__, name);
			return -EINVAL;
		}

		ret = of_clk_add_provider(np, of_clk_src_simple_get, clk);
		clk_debug("use of_clk_src_simple_get, ret=%d\n", ret);
	} else {
		clk_data = kzalloc(sizeof(struct clk_onecell_data), GFP_KERNEL);
		if (!clk_data)
			return -ENOMEM;

		clk_data->clks = kzalloc(cnt * sizeof(struct clk *), GFP_KERNEL);
		if (!clk_data->clks) {
			kfree(clk_data);
			return -ENOMEM;
		}

		clk_data->clk_num = cnt;

		for (i=0; i<cnt; i++) {
			of_property_read_string_index(np, "clock-output-names",
					i, &name);
			clk_debug("clock-output-names[%d]=%s\n", i, name);

			/* ignore empty slots */
			if (!strcmp("reserved", name))
				continue;

			clk = clk_get_sys(NULL, name);
			if (IS_ERR(clk)) {
				clk_err("%s: fail to get %s\n", __func__, name);
				continue;
			}

			clk_data->clks[i] = clk;
		}

		ret = of_clk_add_provider(np, of_clk_src_onecell_get, clk_data);
		clk_debug("use of_clk_src_onecell_get, ret=%d\n", ret);
	}

	return ret;
}

static void rkclk_cache_parents(struct rkclk *rkclk)
{
	struct clk *clk, *parent;
	u8 num_parents;
	int i;


	clk = clk_get(NULL, rkclk->clk_name);
	if (IS_ERR(clk)) {
		clk_err("%s: %s clk_get error\n",
				__func__, rkclk->clk_name);
		return;
	} else {
		clk_debug("%s: %s clk_get success\n",
				__func__, __clk_get_name(clk));
	}

	num_parents = __clk_get_num_parents(clk);
	clk_debug("\t\tnum_parents=%d, parent=%s\n", num_parents,
			__clk_get_name(__clk_get_parent(clk)));

	for (i=0; i<num_parents; i++) {
		/* parent will be cached after this func is called */
		parent = clk_get_parent_by_index(clk, i);
		if (IS_ERR(parent)) {
			clk_err("fail to get parents[%d]=%s\n", i,
					clk->parent_names[i]);
			continue;
		} else {
			clk_debug("\t\tparents[%d]: %s\n", i,
					__clk_get_name(parent));
		}
	}
}

#ifdef RKCLK_DEBUG
void rk_dump_cru(void)
{
	u32 i;

	printk("\n");
	printk("dump cru regs:");
	for (i = 0; i * 4 <= 0xf4; i++) {
		if (i % 4 == 0)
			printk("\n%s: \t[0x%08x]: ",
					__func__, 0x20000000 + i * 4);
		printk("%08x ", readl(RK_CRU_VIRT + i * 4));
	}
	printk("\n\n");
}
#else
void rk_dump_cru(void){}
#endif
EXPORT_SYMBOL_GPL(rk_dump_cru);

#ifdef RKCLK_TEST
struct test_table {
	const char *name;
	u32 rate;
};

struct test_table t_table[] = {
	{.name = "clk_gpu",	.rate = 297000000},
	{.name = "dclk_lcdc0",	.rate = 100000000},
	{.name = "aclk_lcdc0",	.rate = 297000000},

	{.name = "clk_sdmmc",	.rate = 50000000},
	{.name = "clk_emmc",	.rate = 50000000},
	{.name = "clk_sdio",	.rate = 50000000},

	{.name = "clk_i2s_div",	.rate = 300000000},
	{.name = "clk_i2s_frac",.rate = 22579200},
	{.name = "clk_i2s",	.rate = 11289600},
	{.name = "clk_spdif",	.rate = 11289600},

	{.name = "cif_out_pll",	.rate = 48000000},
	{.name = "clk_cif0",	.rate = 12000000},

	{.name = "clk_uart0",	.rate = 12288000},
	{.name = "clk_uart1",	.rate = 48000000},
	{.name = "clk_hsadc",	.rate = 12288000},
	{.name = "clk_mac",  	.rate = 50000000},

	{.name = "clk_apll",	.rate = 500000000},
	{.name = "clk_dpll",	.rate = 400000000},
	{.name = "clk_cpll",	.rate = 600000000},
	{.name = "clk_gpll",	.rate = 800000000},

	{.name = "clk_core",	.rate = 100000000},
	{.name = "clk_core",	.rate = 24000000},
	{.name = "clk_core",	.rate = 500000000},
};

void rk_clk_test(void)
{
	const char *clk_name;
	struct clk *clk;
	unsigned long rate=0, recalc_rate=0, round_rate=0, get_rate=0;
	u32 j = 0;
	int ret;

	for (j = 0; j < ARRAY_SIZE(t_table); j++) {
		clk_debug(">>>>>>test %u\n", j);

		clk_name = t_table[j].name;
		rate = t_table[j].rate;

		clk = clk_get(NULL, clk_name);
		if (IS_ERR(clk)) {
			clk_err("%s: clk(\"%s\") \tclk_get error\n",
					__func__, clk_name);
		} else
			clk_debug("%s: clk(\"%s\") \tclk_get success\n",
					__func__, clk_name);

		/* TEST: clk_round_rate */
		round_rate = clk_round_rate(clk, rate);
		clk_debug("%s: clk(\"%s\") \tclk_round_rate from %lu to %lu\n",
				__func__, clk_name, rate, round_rate);

		/* TEST: clk_set_rate */
		ret = clk_set_rate(clk, rate);
		if (ret) {
			clk_err("%s: clk(\"%s\") \tclk_set_rate error, ret=%d\n",
					__func__, clk_name, ret);
		} else {
			clk_debug("%s: clk(\"%s\") \tclk_set_rate success\n",
					__func__, clk_name);
		}

		/* TEST: recalc_rate\clk_get_rate */
		if (clk->ops->recalc_rate) {
			recalc_rate = clk->ops->recalc_rate(clk->hw,
					clk->parent->rate);
			clk_debug("%s: clk(\"%s\") \tclk_recalc_rate %lu\n",
					__func__, clk_name, recalc_rate);
		} else {
			clk_debug("%s: clk(\"%s\") have no recalc ops\n",
					__func__, clk_name);
			get_rate = clk_get_rate(clk);
			clk_debug("%s: clk(\"%s\") \tclk_get_rate %lu\n",
					__func__, clk_name, get_rate);
		}

		rk_dump_cru();
	}

}
#else
void rk_clk_test(void){}
#endif
EXPORT_SYMBOL_GPL(rk_clk_test);


void rkclk_init_clks(struct device_node *node);

static void __init rk_clk_tree_init(struct device_node *np)
{
	struct device_node *node, *node_tmp, *node_prd, *node_init;
	struct rkclk *rkclk;
	const char *compatible;


	printk("%s start! cru base = 0x%08x\n", __func__, (u32)RK_CRU_VIRT);

	node_init=of_find_node_by_name(NULL,"clocks-init");
	if (!node_init) {
		clk_err("%s: can not get clocks-init node\n", __func__);
		return;
	}

	for_each_available_child_of_node(np, node) {
		clk_debug("\n");
		of_property_read_string(node, "compatible",
				&compatible);

		if (strcmp(compatible, "fixed-clock") == 0) {
			clk_debug("do nothing for fixed-clock node\n");
			continue;
		} else if (strcmp(compatible, "rockchip,rk-pll-cons") == 0) {
			if (rkclk_init_pllcon(node) != 0) {
				clk_err("%s: init pll cons err\n", __func__);
				return ;
			}
		} else if (strcmp(compatible, "rockchip,rk-sel-cons") == 0) {
			if (rkclk_init_selcon(node) != 0) {
				clk_err("%s: init sel cons err\n", __func__);
				return ;
			}
		} else if (strcmp(compatible, "rockchip,rk-gate-cons") == 0) {
			if (rkclk_init_gatecon(node) != 0) {
				clk_err("%s: init gate cons err\n", __func__);
				return ;
			}
		} else {
			clk_err("%s: unknown\n", __func__);
		}
	}

#if 0
	list_for_each_entry(rkclk, &rk_clks, node) {
		int i;
		clk_debug("%s: clkname = %s; type=%d\n",
				__func__, rkclk->clk_name,
				rkclk->clk_type);
		if (rkclk->pll_info) {
			clk_debug("\t\tpll: name=%s, parent=%s\n",
					rkclk->pll_info->clk_name,
					rkclk->pll_info->parent_name);
		}
		if (rkclk->mux_info) {
			for (i = 0; i < rkclk->mux_info->parent_num; i++)
				clk_debug("\t\tmux name=%s, parent: %s\n",
						rkclk->mux_info->clk_name,
						rkclk->mux_info->parent_names[i]);
		}
		if (rkclk->div_info) {
			clk_debug("\t\tdiv name=%s\n",
					rkclk->div_info->clk_name);
		}
		if (rkclk->frac_info) {
			clk_debug("\t\tfrac name=%s\n",
					rkclk->frac_info->clk_name);
		}
		if (rkclk->gate_info) {
			clk_debug("\t\tgate name=%s, \taddr=%08x, \tshift=%d\n",
					rkclk->gate_info->clk_name,
					(u32)rkclk->gate_info->addr,
					rkclk->gate_info->shift);
		}
	}
#endif

	list_for_each_entry(rkclk, &rk_clks, node) {
		rkclk_register(rkclk);
	}

	for_each_available_child_of_node(np, node) {
		of_property_read_string(node, "compatible",
				&compatible);

		if (strcmp(compatible, "fixed-clock") == 0) {
			clk_debug("do nothing for fixed-clock node\n");
			continue;
		} else if (strcmp(compatible, "rockchip,rk-pll-cons") == 0) {
			for_each_available_child_of_node(node, node_prd) {
				rkclk_add_provider(node_prd);
			}
		} else if (strcmp(compatible, "rockchip,rk-sel-cons") == 0) {
			for_each_available_child_of_node(node, node_tmp) {
				for_each_available_child_of_node(node_tmp,
						node_prd) {
					rkclk_add_provider(node_prd);
				}
			}
		} else if (strcmp(compatible, "rockchip,rk-gate-cons") == 0) {
			for_each_available_child_of_node(node, node_prd) {
				rkclk_add_provider(node_prd);
			}
		} else {
			clk_err("%s: unknown\n", __func__);
		}
	}

	/* fill clock parents cache after all clocks have been registered */
	list_for_each_entry(rkclk, &rk_clks, node) {
		clk_debug("\n");
		rkclk_cache_parents(rkclk);
	}

	rk_clk_test();

	rkclk_init_clks(node_init);

}
CLK_OF_DECLARE(rk_clocks, "rockchip,rk-clock-regs", rk_clk_tree_init);


/********************************** rockchip clks init****************************************/
const char *of_clk_init_rate_get_info(struct device_node *np, int index,u32 *rate)
{
	struct of_phandle_args clkspec;
	const char *clk_name;
	int rc;

	if (index < 0)
		return NULL;

	rc = of_parse_phandle_with_args(np, "rockchip,clocks-init-rate", "#clock-init-cells", index,
			&clkspec);
	if (rc)
		return NULL;

	if (of_property_read_string_index(clkspec.np, "clock-output-names",0,&clk_name) < 0)
		return NULL;

	*rate= clkspec.args[0];

	of_node_put(clkspec.np);
	return clk_name;
}

const char *of_clk_init_parent_get_info(struct device_node *np, int index,const char **clk_child_name)
{
	struct of_phandle_args clkspec;
	const char *clk_name;
	int rc;
	phandle phandle;
	struct device_node *node = NULL;

	if (index < 0)
		return NULL;

	rc = of_parse_phandle_with_args(np, "rockchip,clocks-init-parent", "#clock-init-cells", index,
			&clkspec);
	if (rc)
		return NULL;

	if (of_property_read_string_index(clkspec.np, "clock-output-names",0,&clk_name) < 0)
		return NULL;


	phandle = clkspec.args[0];

	of_node_put(clkspec.np);

	if (phandle) {

		node = of_find_node_by_phandle(phandle);
		if (!node) {
			return NULL;
		}

		if (of_property_read_string_index(node, "clock-output-names",0,clk_child_name) < 0)
			return NULL;

		of_node_put(node);//???
		node=NULL;
	}
	else
		return NULL;

	return clk_name;
}

void rkclk_init_clks(struct device_node *np)
{
	//struct device_node *np;
	int i,cnt_parent,cnt_rate;
	u32 clk_rate;
	//int ret;
	struct clk *clk_p, *clk_c;
	const char *clk_name, *clk_parent_name;


	cnt_parent = of_count_phandle_with_args(np, "rockchip,clocks-init-parent", "#clock-init-cells");

	printk("%s: cnt_parent = %d\n",__FUNCTION__,cnt_parent);

	for (i = 0; i < cnt_parent; i++) {
		clk_parent_name=NULL;
		clk_name=of_clk_init_parent_get_info(np, i,&clk_parent_name);

		if(clk_name==NULL||clk_parent_name==NULL)
			continue;

		clk_c=clk_get(NULL,clk_name);
		clk_p=clk_get(NULL,clk_parent_name);

		if(IS_ERR(clk_c)||IS_ERR(clk_p))
			continue;

		clk_set_parent(clk_c, clk_p);

		printk("%s: set %s parent = %s\n", __FUNCTION__, clk_name,
				clk_parent_name);
	}

	cnt_rate = of_count_phandle_with_args(np, "rockchip,clocks-init-rate", "#clock-init-cells");

	printk("%s: cnt_rate = %d\n",__FUNCTION__,cnt_rate);

	for (i = 0; i < cnt_rate; i++) {
		clk_name=of_clk_init_rate_get_info(np, i, &clk_rate);

		if(clk_name==NULL)
			continue;

		clk_c = clk_get(NULL, clk_name);

		if(IS_ERR(clk_c))
			continue;

		if((clk_rate<1*MHZ)||(clk_rate>2000*MHZ))
			clk_err("warning: clk_rate < 1*MHZ or > 2000*MHZ\n");

		clk_set_rate(clk_c, clk_rate);

		printk("%s: set %s rate = %u\n", __FUNCTION__, clk_name,
				clk_rate);
	}

}

