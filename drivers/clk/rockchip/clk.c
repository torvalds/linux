/*
 * Copyright (C) 2013 ROCKCHIP, Inc.
 * Author: chenxing <chenxing@rock-chips.com>
 *         Dai Kelin <dkl@rock-chips.com>
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


struct rkclk_muxinfo {
	const char		*clk_name;
	struct device_node	*np;
	struct clk_mux		*mux;
	u8			parent_num;
	const char		**parent_names;
	u32			clkops_idx;
};

struct rkclk_divinfo {
	const char		*clk_name;
	struct device_node	*np;
	struct clk_divider	*div;
	u32			div_type;
	const char		*parent_name;
	u32			clkops_idx;
};

struct rkclk_fracinfo {
	const char		*clk_name;
	struct device_node 	*np;
	struct clk_divider	*frac;
	u32			frac_type;
	const char		*parent_name;
	u32			clkops_idx;
};

struct rkclk_gateinfo {
	const char		*clk_name;
	struct device_node 	*np;
	struct clk_gate		*gate;
	const char		*parent_name;
	//u32			clkops_idx;
};

struct rkclk_pllinfo {
	const char		*clk_name;
	struct device_node 	*np;
	struct clk_pll		*pll;
	const char		*parent_name;
	u32			clkops_idx;
};

struct rkclk {
	const char		*clk_name;
	//struct device_node 	*np;
	u32			clk_type;
	u32			flags;
	struct rkclk_muxinfo	*mux_info;
	struct rkclk_divinfo	*div_info;
	struct rkclk_fracinfo	*frac_info;
	struct rkclk_pllinfo	*pll_info;
	struct rkclk_gateinfo	*gate_info;
	struct list_head	node;
};

static DEFINE_SPINLOCK(clk_lock);
LIST_HEAD(rk_clks);

#define RKCLK_PLL_TYPE	(1 << 0)
#define RKCLK_MUX_TYPE	(1 << 1)
#define RKCLK_DIV_TYPE	(1 << 2)
#define RKCLK_FRAC_TYPE	(1 << 3)
#define RKCLK_GATE_TYPE	(1 << 4)

static int rkclk_init_muxinfo(struct device_node *np, void __iomem *addr)
{
	struct rkclk_muxinfo *muxinfo = NULL;
	struct clk_mux *mux = NULL;
	u32 shift, width;
	u32 flags;
	int cnt, i, ret = 0;
	u8 found = 0;
	struct rkclk *rkclk = NULL;


	muxinfo = kzalloc(sizeof(struct rkclk_muxinfo), GFP_KERNEL);
	if (!muxinfo) {
		ret = -ENOMEM;
		goto out;
	}

	muxinfo->mux = kzalloc(sizeof(struct clk_mux), GFP_KERNEL);
	if (!muxinfo->mux) {
		ret = -ENOMEM;
		goto out;
	}
	mux = muxinfo->mux;

	ret = of_property_read_string(np, "clock-output-names",
			&muxinfo->clk_name);
	if (ret != 0)
		goto out;

	muxinfo->np = np;

	cnt = of_count_phandle_with_args(np, "clocks", "#clock-cells");
	if (cnt < 0) {
		ret = -EINVAL;
		goto out;
	} else {
		clk_debug("%s: parent cnt = %d\n", __func__, cnt);
		muxinfo->parent_num = (u8)cnt;
	}

	muxinfo->parent_names = kzalloc(cnt * sizeof(char *), GFP_KERNEL);
	for (i = 0; i < cnt ; i++) {
		muxinfo->parent_names[i] = of_clk_get_parent_name(np, i);
	}

	mux->reg = addr;

	ret = of_property_read_u32_index(np, "rockchip,bits", 0, &shift);
	if (ret != 0) {
		goto out;
	} else {
		mux->shift = (u8)shift;
	}

	ret = of_property_read_u32_index(np, "rockchip,bits", 1, &width);
	if (ret != 0)
		goto out;
	mux->mask = (1 << width) - 1;

	mux->flags = CLK_MUX_HIWORD_MASK;

	ret = of_property_read_u32(np, "rockchip,clkops-idx",
			&muxinfo->clkops_idx);
	if (ret != 0) {
		muxinfo->clkops_idx = CLKOPS_TABLE_END;
		ret = 0;
	}

	ret = of_property_read_u32(np, "rockchip,flags", &flags);
	if (ret != 0) {
		flags = 0;
		ret = 0;
	}

	found = 0;
	list_for_each_entry(rkclk, &rk_clks, node) {
		if (strcmp(muxinfo->clk_name, rkclk->clk_name) == 0) {
			if (rkclk->mux_info != NULL) {
				clk_err("%s %d:\n", __func__, __LINE__);
				clk_err("This clk(%s) has been used,"
						"will be overwrited here!\n",
						rkclk->clk_name);
			}
			clk_debug("%s: find match %s\n", __func__,
					rkclk->clk_name);
			found = 1;
			rkclk->mux_info = muxinfo;
			rkclk->clk_type |= RKCLK_MUX_TYPE;
			rkclk->flags |= flags;
			break;
		}
	}

	if (!found) {
		rkclk = kzalloc(sizeof(struct rkclk), GFP_KERNEL);
		if (!rkclk) {
			ret = -ENOMEM;
			goto out;
		}
		rkclk->clk_name = muxinfo->clk_name;
		rkclk->mux_info = muxinfo;
		rkclk->clk_type = RKCLK_MUX_TYPE;
		rkclk->flags = flags;
		clk_debug("%s: creat %s\n", __func__, rkclk->clk_name);
		list_add_tail(&rkclk->node, &rk_clks);
	}

out:
	if (ret) {
		clk_err("%s error, ret = %d\n", __func__, ret);
		if (muxinfo) {
			if (muxinfo->mux)
				kfree(muxinfo->mux);
			kfree(muxinfo);
		}
		if (rkclk)
			kfree(rkclk);
	}

	return ret;
}

static int rkclk_init_divinfo(struct device_node *np, void __iomem *addr)
{
	int cnt = 0, i = 0, ret = 0;
	struct rkclk *rkclk = NULL;
	u8 found = 0;
	u32 flags;
	u32 shift, width;
	struct rkclk_divinfo *divinfo = NULL;
	struct clk_divider *div = NULL;
	struct clk_div_table	*table;
	u32 table_val, table_div;


	divinfo = kzalloc(sizeof(struct rkclk_divinfo), GFP_KERNEL);
	if (!divinfo) {
		ret = -ENOMEM;
		goto out;
	}

	divinfo->div = kzalloc(sizeof(struct clk_divider), GFP_KERNEL);
	if (!divinfo->div) {
		ret = -ENOMEM;
		goto out;
	}
	div = divinfo->div;

	ret = of_property_read_string(np, "clock-output-names",
			&divinfo->clk_name);
	if (ret != 0)
		goto out;

	divinfo->parent_name = of_clk_get_parent_name(np, 0);

	divinfo->np = np;

	ret = of_property_read_u32(np, "rockchip,clkops-idx",
			&divinfo->clkops_idx);
	if (ret != 0) {
		divinfo->clkops_idx = CLKOPS_TABLE_END;
		ret = 0;
	}

	ret = of_property_read_u32(np, "rockchip,flags", &flags);
	if (ret != 0) {
		flags = 0;
		ret = 0;
	}

	ret = of_property_read_u32(np, "rockchip,div-type", &divinfo->div_type);
	if (ret != 0)
		goto out;

	switch (divinfo->div_type) {
		case CLK_DIVIDER_PLUS_ONE:
		case CLK_DIVIDER_ONE_BASED:
		case CLK_DIVIDER_POWER_OF_TWO:
			break;
		case CLK_DIVIDER_USER_DEFINE:
			of_get_property(np, "rockchip,div-relations", &cnt);
			if (cnt <= 0) {
				ret = -EINVAL;
				goto out;
			}
			cnt /= 4 * 2;
			table = kzalloc(cnt * sizeof(struct clk_div_table),
					GFP_KERNEL);
			if (!table) {
				ret = -ENOMEM;
				goto out;
			}
			for (i = 0; i < cnt; i++) {
				ret = of_property_read_u32_index(np,
						"rockchip,div-relations", i * 2,
						&table_val);
				if (ret)
					goto out;
				ret = of_property_read_u32_index(np,
						"rockchip,div-relations",
						i * 2 + 1, &table_div);
				if (ret)
					goto out;
				table[i].val = (unsigned int)table_val;
				table[i].div = (unsigned int)table_div;
				clk_debug("\tGet div table %d: val=%d, div=%d\n",
						i, table_val, table_div);
			}
			div->table = table;
			break;
		default:
			clk_err("%s: %s: unknown rockchip,div-type\n", __func__,
					divinfo->clk_name);
			ret = -EINVAL;
			goto out;
	}

	div->reg = addr;
	ret = of_property_read_u32_index(np, "rockchip,bits", 0, &shift);
	if (ret)
		goto out;
	ret = of_property_read_u32_index(np, "rockchip,bits", 1, &width);
	if (ret)
		goto out;
	div->shift = (u8)shift;
	div->width = (u8)width;
	div->flags = CLK_DIVIDER_HIWORD_MASK | divinfo->div_type;

	found = 0;
	list_for_each_entry(rkclk, &rk_clks, node) {
		if (strcmp(divinfo->clk_name, rkclk->clk_name) == 0) {
			if (rkclk->div_info != NULL) {
				clk_err("%s %d:\n", __func__, __LINE__);
				clk_err("This clk(%s) has been used,"
						"will be overwrited here!\n",
						rkclk->clk_name);
			}
			clk_debug("%s: find match %s\n", __func__,
					rkclk->clk_name);
			found = 1;
			rkclk->div_info = divinfo;
			rkclk->clk_type |= RKCLK_DIV_TYPE;
			rkclk->flags |= flags;
			break;
		}
	}

	if (!found) {
		rkclk = kzalloc(sizeof(struct rkclk), GFP_KERNEL);
		if (!rkclk) {
			ret = -ENOMEM;
			goto out;
		}
		rkclk->clk_name = divinfo->clk_name;
		rkclk->div_info = divinfo;
		rkclk->clk_type = RKCLK_DIV_TYPE;
		rkclk->flags = flags;
		clk_debug("%s: creat %s\n", __func__, rkclk->clk_name);
		list_add_tail(&rkclk->node, &rk_clks);
	}

out:
	if (ret) {
		clk_err("%s error, ret = %d\n", __func__, ret);
		if(table)
			kfree(table);
		if (divinfo) {
			if (divinfo->div)
				kfree(divinfo->div);
			kfree(divinfo);
		}
		if (rkclk)
			kfree(rkclk);
	}

	return ret;
}

static int rkclk_init_fracinfo(struct device_node *np, void __iomem *addr)
{
	struct rkclk *rkclk = NULL;
	u8 found = 0;
	int ret = 0;
	struct rkclk_fracinfo *fracinfo = NULL;
	struct clk_divider *frac = NULL;
	u32 shift, width, flags;


	fracinfo = kzalloc(sizeof(struct rkclk_fracinfo), GFP_KERNEL);
	if (!fracinfo) {
		ret = -ENOMEM;
		goto out;
	}

	fracinfo->frac = kzalloc(sizeof(struct clk_divider), GFP_KERNEL);
	if (!fracinfo->frac) {
		ret = -ENOMEM;
		goto out;
	}
	frac = fracinfo->frac;

	ret = of_property_read_string(np, "clock-output-names",
			&fracinfo->clk_name);
	if (ret != 0)
		goto out;

	fracinfo->parent_name = of_clk_get_parent_name(np, 0);
	fracinfo->np = np;

	ret = of_property_read_u32(np, "rockchip,clkops-idx",
			&fracinfo->clkops_idx);
	if (ret != 0) {
		fracinfo->clkops_idx = CLKOPS_TABLE_END;
		clk_err("frac node without specified ops!\n");
		ret = -EINVAL;
		goto out;
	}

	ret = of_property_read_u32(np, "rockchip,flags", &flags);
	if (ret != 0) {
		clk_debug("if not specified, frac use CLK_SET_RATE_PARENT flag "
				"as default\n");
		flags = CLK_SET_RATE_PARENT;
		ret = 0;
	}

	frac->reg = addr;
	ret = of_property_read_u32_index(np, "rockchip,bits", 0, &shift);
	if (ret)
		goto out;
	ret = of_property_read_u32_index(np, "rockchip,bits", 1, &width);
	if (ret)
		goto out;
	frac->shift = (u8)shift;
	frac->width = (u8)width;
	frac->flags = 0;

	found = 0;
	list_for_each_entry(rkclk, &rk_clks, node) {
		if (strcmp(fracinfo->clk_name, rkclk->clk_name) == 0) {
			if (rkclk->frac_info != NULL) {
				clk_err("%s %d:\n", __func__, __LINE__);
				clk_err("This clk(%s) has been used,"
						"will be overwrited here!\n",
						rkclk->clk_name);
			}
			clk_debug("%s: find match %s\n", __func__,
					rkclk->clk_name);
			found = 1;
			rkclk->frac_info = fracinfo;
			rkclk->clk_type |= RKCLK_FRAC_TYPE;
			rkclk->flags |= flags;
			break;
		}
	}

	if (!found) {
		rkclk = kzalloc(sizeof(struct rkclk), GFP_KERNEL);
		if (!rkclk) {
			ret = -ENOMEM;
			goto out;
		}
		rkclk->clk_name = fracinfo->clk_name;
		rkclk->frac_info = fracinfo;
		rkclk->clk_type = RKCLK_FRAC_TYPE;
		rkclk->flags = flags;
		clk_debug("%s: creat %s\n", __func__, rkclk->clk_name);
		list_add_tail(&rkclk->node, &rk_clks);
	}

out:
	if (ret) {
		clk_err("%s error, ret = %d\n", __func__, ret);
		if (fracinfo) {
			if (fracinfo->frac)
				kfree(fracinfo->frac);
			kfree(fracinfo);
		}
		if (rkclk)
			kfree(rkclk);
	}

	return ret;
}

static int __init rkclk_init_selcon(struct device_node *np)
{
	struct device_node *node_con, *node;
	void __iomem *reg = 0;
	int ret = 0;


	for_each_available_child_of_node(np, node_con) {
		reg = of_iomap(node_con, 0);
		clk_debug("\n");
		clk_debug("%s: reg = 0x%x\n", __func__, (u32)reg);

		for_each_available_child_of_node(node_con, node) {
			clk_debug("\n");
			if (of_device_is_compatible(node,
						"rockchip,rk3188-div-con"))
				ret = rkclk_init_divinfo(node, reg);

			else if (of_device_is_compatible(node,
						"rockchip,rk3188-mux-con"))
				ret = rkclk_init_muxinfo(node, reg);

			else if (of_device_is_compatible(node,
						"rockchip,rk3188-frac-con"))
				ret = rkclk_init_fracinfo(node, reg);

			else if (of_device_is_compatible(node,
						"rockchip,rk3188-inv-con"))
				clk_debug("INV clk\n");

			else {
				clk_err("%s: unknown controller type\n",
						__func__);
				ret = -EINVAL;
			}
		}
	}

	return ret;
}

static int __init rkclk_init_gatecon(struct device_node *np)
{
	struct device_node *node;
	const char *clk_name;
	void __iomem *reg;
	int cnt;
	int i;
	struct rkclk_gateinfo *gateinfo;
	u8 found = 0;
	struct rkclk *rkclk;
	int ret = 0;
	struct clk_gate *gate = NULL;


	for_each_available_child_of_node(np, node) {
		cnt = of_property_count_strings(node, "clock-output-names");
		if (cnt < 0) {
			clk_err("%s: error in clock-output-names %d\n",
					__func__, cnt);
			continue;
		}

		if (cnt == 0) {
			clk_debug("%s: nothing to do\n", __func__);
			continue;
		}

		reg = of_iomap(node, 0);
		clk_debug("\n");
		clk_debug("%s: reg = 0x%x\n", __func__, (u32)reg);

		for (i = 0; i < cnt; i++) {
			ret = of_property_read_string_index(node,
					"clock-output-names", i, &clk_name);
			if (ret != 0)
				goto out;

			/* ignore empty slots */
			if (!strcmp("reserved", clk_name)) {
				clk_debug("do nothing for reserved clk\n");
				continue;
			}

			gateinfo = kzalloc(sizeof(struct rkclk_gateinfo),
					GFP_KERNEL);
			if (!gateinfo) {
				ret = -ENOMEM;
				goto out;
			}

			gateinfo->gate = kzalloc(sizeof(struct clk_gate),
					GFP_KERNEL);
			if (!gateinfo->gate) {
				ret = -ENOMEM;
				goto out;
			}
			gate = gateinfo->gate;

			gateinfo->clk_name = clk_name;
			gateinfo->parent_name = of_clk_get_parent_name(node, i);
			gateinfo->np = node;

			gate->reg = reg;
			gate->bit_idx = (i % 16);
			gate->flags = CLK_GATE_HIWORD_MASK |
				CLK_GATE_SET_TO_DISABLE;

			found = 0;
			list_for_each_entry(rkclk, &rk_clks, node) {
				if (strcmp(clk_name, rkclk->clk_name) == 0) {
					if (rkclk->gate_info != NULL) {
						clk_err("%s %d:\n", __func__,
								__LINE__);
						clk_err("This clk(%s) has been used,"
								"will be overwrited here!\n",
								rkclk->clk_name);
					}
					clk_debug("%s: find match %s\n",
							__func__, rkclk->clk_name);
					found = 1;
					rkclk->gate_info = gateinfo;
					rkclk->clk_type |= RKCLK_GATE_TYPE;
					break;
				}
			}
			if (!found) {
				rkclk = kzalloc(sizeof(struct rkclk), GFP_KERNEL);
				if (!rkclk) {
					ret = -ENOMEM;
					goto out;
				}
				rkclk->clk_name = gateinfo->clk_name;
				rkclk->gate_info = gateinfo;
				rkclk->clk_type = RKCLK_GATE_TYPE;
				clk_debug("%s: creat %s\n", __func__,
						rkclk->clk_name);
				list_add_tail(&rkclk->node, &rk_clks);
			}

out:
			if (ret) {
				clk_err("%s error, ret = %d\n", __func__, ret);
				if (gateinfo) {
					if (gateinfo->gate)
						kfree(gateinfo->gate);
					kfree(gateinfo);
				}
				if (rkclk)
					kfree(rkclk);
			}
		}

	}

	return 0;
}

static int __init rkclk_init_pllcon(struct device_node *np)
{
	struct device_node *node = NULL;
	struct rkclk_pllinfo *pllinfo = NULL;
	void __iomem	*reg;
	struct clk_pll *pll = NULL;
	u8 found = 0;
	int ret = 0;
	struct rkclk *rkclk = NULL;
	u32 flags;
	u32 pll_id;


	for_each_available_child_of_node(np, node) {
		pllinfo = kzalloc(sizeof(struct rkclk_pllinfo), GFP_KERNEL);
		if (!pllinfo) {
			ret = -ENOMEM;
			goto out;
		}

		pllinfo->pll = kzalloc(sizeof(struct clk_pll), GFP_KERNEL);
		if (!pllinfo->pll) {
			ret = -ENOMEM;
			goto out;
		}
		pll = pllinfo->pll;

		pllinfo->np = node;

		ret = of_property_read_string_index(node, "clock-output-names",
				0, &pllinfo->clk_name);
		if (ret)
			goto out;

		pllinfo->parent_name = of_clk_get_parent_name(node, 0);

		ret = of_property_read_u32(np, "rockchip,flags", &flags);
		if (ret != 0) {
			flags = 0;
			ret = 0;
		}

		reg = of_iomap(node, 0);
		if (reg == NULL) {
			clk_err("%s: can not get reg addr info\n", __func__);
			ret = -EINVAL;
			goto out;
		} else {
			pll->reg = reg;
		}

		ret = of_property_read_u32_index(node, "reg", 1, &pll->width);
		if (ret != 0) {
			clk_err("%s: can not get reg length info\n", __func__);
			goto out;
		}

		ret = of_property_read_u32(node, "rockchip,pll-id", &pll_id);
		if (ret != 0) {
			clk_err("%s: can not get pll-id\n", __func__);
			goto out;
		} else {
			pll->id = (u8)pll_id;
		}

		clk_debug("%s: pllname=%s, parent=%s, flags=%u, "
				"addr=0x%x, len=0x%x, id=%d\n",
				__func__, pllinfo->clk_name,
				pllinfo->parent_name, flags,
				(u32)pll->reg, pll->width, pll->id);

		found = 0;
		list_for_each_entry(rkclk, &rk_clks, node) {
			if (strcmp(pllinfo->clk_name, rkclk->clk_name) == 0) {
				if (rkclk->pll_info != NULL) {
					clk_err("%s %d:\n", __func__, __LINE__);
					clk_err("This clk(%s) has been used,"
							"will be overwrited here!\n",
							rkclk->clk_name);
				}
				clk_debug("%s: find match %s\n", __func__,
						rkclk->clk_name);
				found = 1;
				rkclk->pll_info = pllinfo;
				rkclk->clk_type |= RKCLK_PLL_TYPE;
				rkclk->flags |= flags;
				break;
			}
		}

		if (!found) {
			rkclk = kzalloc(sizeof(struct rkclk), GFP_KERNEL);
			if (!rkclk) {
				ret = -ENOMEM;
				goto out;
			}
			rkclk->clk_name = pllinfo->clk_name;
			rkclk->pll_info = pllinfo;
			rkclk->clk_type = RKCLK_PLL_TYPE;
			rkclk->flags = flags;
			list_add_tail(&rkclk->node, &rk_clks);
		}
	}

out:
	if (ret) {
		clk_err("%s error, ret = %d\n", __func__, ret);
		if (pllinfo) {
			if (pllinfo->pll)
				kfree(pllinfo->pll);
			kfree(pllinfo);
		}
		if (rkclk)
			kfree(rkclk);
	}

	return ret;
}

static int rkclk_register(struct rkclk *rkclk)
{
	struct clk		*clk = NULL;
	struct clk_mux		*mux = NULL;
	struct clk_divider	*div = NULL;
	struct clk_gate		*gate = NULL;
	struct clk_pll		*pll = NULL;
	struct clk_divider	*frac = NULL;

	struct clk_hw		*mux_hw = NULL;
	const struct clk_ops	*mux_ops = NULL;
	struct clk_hw		*rate_hw = NULL;
	const struct clk_ops	*rate_ops = NULL;
	struct clk_hw		*gate_hw = NULL;
	const struct clk_ops	*gate_ops = NULL;

	int			parent_num;
	const char		**parent_names = NULL;


	if (rkclk && rkclk->clk_name) {
		clk_debug("%s: clk_name=%s, clk_type=0x%x, flags=0x%x\n",
				__func__, rkclk->clk_name, rkclk->clk_type,
				rkclk->flags);
	} else {
		clk_err("%s: rkclk or clk_name is NULL!\n", __func__);
		return -EINVAL;
	}

	if (rkclk->mux_info && rkclk->mux_info->mux)
		mux = rkclk->mux_info->mux;
	if (rkclk->div_info && rkclk->div_info->div)
		div = rkclk->div_info->div;
	if (rkclk->gate_info && rkclk->gate_info->gate)
		gate = rkclk->gate_info->gate;
	if (rkclk->pll_info && rkclk->pll_info->pll)
		pll = rkclk->pll_info->pll;
	if (rkclk->frac_info && rkclk->frac_info->frac)
		frac = rkclk->frac_info->frac;

	switch (rkclk->clk_type) {
		case RKCLK_MUX_TYPE:
			/* only mux without specified ops will be registered here */
			if (rkclk->mux_info->clkops_idx == CLKOPS_TABLE_END) {
				clk_debug("use clk_register_mux\n");
				clk = clk_register_mux_table(NULL, rkclk->clk_name,
						rkclk->mux_info->parent_names,
						rkclk->mux_info->parent_num,
						rkclk->flags, mux->reg, mux->shift,
						mux->mask, mux->flags, NULL, &clk_lock);
				goto add_lookup;
			} else
				goto rgs_comp;
		case RKCLK_PLL_TYPE:
			clk_debug("use rk_clk_register_pll\n");
			clk = rk_clk_register_pll(NULL, rkclk->clk_name,
					rkclk->pll_info->parent_name,
					rkclk->flags, pll->reg, pll->width,
					pll->id, &clk_lock);
			//kfree!!!!!!!
			goto add_lookup;
		case RKCLK_DIV_TYPE:
			/* only div without specified ops will be registered here */
			if (rkclk->div_info->clkops_idx == CLKOPS_TABLE_END) {
				clk_debug("use clk_register_divider\n");
				clk = clk_register_divider(NULL, rkclk->clk_name,
						rkclk->div_info->parent_name,
						rkclk->flags, div->reg, div->shift,
						div->width, div->flags, &clk_lock);
				goto add_lookup;
			} else
				goto rgs_comp;
		case RKCLK_FRAC_TYPE:
			if (rkclk->frac_info->clkops_idx == CLKOPS_TABLE_END) {
				clk_err("frac node without specified ops!\n");
				return -EINVAL;
			} else
				goto rgs_comp;
		case RKCLK_GATE_TYPE:
			clk_debug("use clk_register_gate\n");
			clk = clk_register_gate(NULL, rkclk->clk_name,
					rkclk->gate_info->parent_name, rkclk->flags,
					gate->reg, gate->bit_idx, gate->flags,
					&clk_lock);
			goto add_lookup;
		case (RKCLK_DIV_TYPE|RKCLK_PLL_TYPE):
		case (RKCLK_DIV_TYPE|RKCLK_FRAC_TYPE):
		case (RKCLK_PLL_TYPE|RKCLK_FRAC_TYPE):
		case (RKCLK_DIV_TYPE|RKCLK_PLL_TYPE|RKCLK_FRAC_TYPE):
			clk_err("Invalid rkclk type!\n");
			return -EINVAL;
		default:
			goto rgs_comp;
	}

rgs_comp:
	clk_debug("use clk_register_composite\n");

	/* prepare args for clk_register_composite */

	/* prepare parent_num && parent_names
	 * priority: MUX > DIV=PLL=FRAC > GATE
	 */
	if (rkclk->clk_type & RKCLK_MUX_TYPE) {
		parent_num = rkclk->mux_info->parent_num;
		parent_names = rkclk->mux_info->parent_names;
	} else if (rkclk->clk_type & RKCLK_DIV_TYPE ) {
		parent_num = 1;
		parent_names = &(rkclk->div_info->parent_name);
	} else if (rkclk->clk_type & RKCLK_PLL_TYPE) {
		parent_num = 1;
		parent_names = &(rkclk->pll_info->parent_name);
	} else if (rkclk->clk_type & RKCLK_FRAC_TYPE) {
		parent_num = 1;
		parent_names = &(rkclk->frac_info->parent_name);
	} else if (rkclk->clk_type & RKCLK_GATE_TYPE) {
		parent_num = 1;
		parent_names = &(rkclk->gate_info->parent_name);
	}

	/* prepare mux_hw && mux_ops */
	if (rkclk->clk_type & RKCLK_MUX_TYPE) {
		mux_hw = &mux->hw;
		mux_ops = &clk_mux_ops;
	}

	/* prepare rate_hw && rate_ops
	 * priority: DIV=PLL=FRAC > MUX
	 */
	if (rkclk->clk_type & RKCLK_DIV_TYPE) {
		rate_hw = &div->hw;
		if (rkclk->div_info->clkops_idx != CLKOPS_TABLE_END)
			rate_ops = rk_get_clkops(rkclk->div_info->clkops_idx);
		else
			rate_ops = &clk_divider_ops;
	} else if (rkclk->clk_type & RKCLK_PLL_TYPE) {
		rate_hw = &pll->hw;
		rate_ops = &clk_pll_ops;
	} else if (rkclk->clk_type & RKCLK_FRAC_TYPE) {
		rate_hw = &frac->hw;
		rate_ops = rk_get_clkops(rkclk->frac_info->clkops_idx);
	} else if ((rkclk->clk_type & RKCLK_MUX_TYPE) &&
			(rkclk->mux_info->clkops_idx != CLKOPS_TABLE_END)) {
		/* when a mux node has specified clkops_idx, prepare rate_hw &&
		 * rate_ops and use clk_register_composite to register it later.
		 */
		/*FIXME*/
		rate_hw = kzalloc(sizeof(struct clk_hw), GFP_KERNEL);
		if (!rate_hw) {
			clk_err("%s: fail to alloc rate_hw!\n", __func__);
			return -ENOMEM;
		}
		rate_ops = rk_get_clkops(rkclk->mux_info->clkops_idx);
	}

	if (rkclk->clk_type & RKCLK_GATE_TYPE) {
		gate_hw = &gate->hw;
		gate_ops = &clk_gate_ops;
	}

	clk_debug("parent_num=%d, mux_hw=%d mux_ops=%d, rate_hw=%d rate_ops=%d,"
			" gate_hw=%d gate_ops=%d\n",
			parent_num, mux_hw?1:0, mux_ops?1:0, rate_hw?1:0, 
			rate_ops?1:0, gate_hw?1:0, gate_ops?1:0);

	clk = clk_register_composite(NULL, rkclk->clk_name, parent_names,
			parent_num, mux_hw, mux_ops, rate_hw, rate_ops,
			gate_hw, gate_ops, rkclk->flags);

add_lookup:
	if (clk) {
		clk_debug("clk name=%s, flags=0x%lx\n", clk->name, clk->flags);
		clk_register_clkdev(clk, rkclk->clk_name, NULL);
	} else {
		clk_err("%s: clk(\"%s\") register clk error\n", __func__,
				rkclk->clk_name);
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

void rkclk_dump_info(struct rkclk *rkclk)
{
	struct clk_mux		*mux = NULL;
	struct clk_divider	*div = NULL;
	struct clk_gate		*gate = NULL;
	struct clk_pll		*pll = NULL;
	struct clk_divider	*frac = NULL;
	int i;


	clk_debug("%s: clkname=%s, type=0x%x, flags=0x%x\n",
			__func__, (rkclk->clk_name)?(rkclk->clk_name):NULL,
			rkclk->clk_type, rkclk->flags);

	if (rkclk->mux_info && rkclk->mux_info->mux)
		mux = rkclk->mux_info->mux;
	if (rkclk->div_info && rkclk->div_info->div)
		div = rkclk->div_info->div;
	if (rkclk->pll_info && rkclk->pll_info->pll)
		pll = rkclk->pll_info->pll;
	if (rkclk->frac_info && rkclk->frac_info->frac)
		frac = rkclk->frac_info->frac;
	if (rkclk->gate_info && rkclk->gate_info->gate)
		gate = rkclk->gate_info->gate;

	if (rkclk->mux_info) {
		clk_debug("\t\tmux_info: name=%s, clkops_idx=%u\n",
				rkclk->mux_info->clk_name,
				rkclk->mux_info->clkops_idx);
		for (i = 0; i < rkclk->mux_info->parent_num; i++)
			clk_debug("\t\t\tparent[%d]: %s\n", i,
					rkclk->mux_info->parent_names[i]);
		if (mux) {
			clk_debug("\t\tmux: reg=0x%x, mask=0x%x, shift=0x%x, "
					"mux_flags=0x%x\n", (u32)mux->reg,
					mux->mask, mux->shift, mux->flags);
		}
	}
	if (rkclk->pll_info) {
		clk_debug("\t\tpll_info: name=%s, parent=%s, clkops_idx=0x%x\n",
				rkclk->pll_info->clk_name,
				rkclk->pll_info->parent_name,
				rkclk->pll_info->clkops_idx);
		if (pll) {
			clk_debug("\t\tpll: reg=0x%x, width=0x%x, id=%d\n",
					(u32)pll->reg, pll->width, pll->id);
		}
	}
	if (rkclk->div_info) {
		clk_debug("\t\tdiv_info: name=%s, div_type=0x%x, parent=%s, "
				"clkops_idx=%d\n",
				rkclk->div_info->clk_name,
				rkclk->div_info->div_type,
				rkclk->div_info->parent_name,
				rkclk->div_info->clkops_idx);
		if (div) {
			clk_debug("\t\tdiv: reg=0x%x, shift=0x%x, width=0x%x, "
					"div_flags=0x%x\n", (u32)div->reg,
					div->shift, div->width, div->flags);
		}
	}
	if (rkclk->frac_info) {
		clk_debug("\t\tfrac_info: name=%s, parent=%s, clkops_idx=%d\n",
				rkclk->frac_info->clk_name,
				rkclk->frac_info->parent_name,
				rkclk->frac_info->clkops_idx);
		if (frac) {
			clk_debug("\t\tfrac: reg=0x%x, shift=0x%x, width=0x%x, "
					"div_flags=0x%x\n", (u32)frac->reg,
					frac->shift, frac->width, frac->flags);
		}
	}
	if (rkclk->gate_info) {
		clk_debug("\t\tgate_info: name=%s, parent=%s\n",
				rkclk->gate_info->clk_name,
				rkclk->gate_info->parent_name);
		if (gate) {
			clk_debug("\t\tgate: reg=0x%x, bit_idx=%d, "
					"gate_flags=0x%x\n", (u32)gate->reg,
					gate->bit_idx, gate->flags);
		}
	}
}
#else
void rk_dump_cru(void) {}
void rkclk_dump_info(struct rkclk *rkclk) {}
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
void rk_clk_test(void) {}
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

	list_for_each_entry(rkclk, &rk_clks, node) {
		clk_debug("\n");
		rkclk_dump_info(rkclk);
		clk_debug("\n");
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

	rkclk_init_clks(node_init);

	rk_clk_test();
}
CLK_OF_DECLARE(rk_clocks, "rockchip,rk-clock-regs", rk_clk_tree_init);


/***************************** rockchip clks init******************************/
const char *of_clk_init_rate_get_info(struct device_node *np,
		int index,u32 *rate)
{
	struct of_phandle_args clkspec;
	const char *clk_name;
	int rc;

	if (index < 0)
		return NULL;

	rc = of_parse_phandle_with_args(np, "rockchip,clocks-init-rate",
			"#clock-init-cells", index, &clkspec);
	if (rc)
		return NULL;

	if (of_property_read_string_index(clkspec.np, "clock-output-names", 0,
				&clk_name) < 0)
		return NULL;

	*rate= clkspec.args[0];

	of_node_put(clkspec.np);
	return clk_name;
}

const char *of_clk_init_parent_get_info(struct device_node *np, int index,
		const char **clk_child_name)
{
	struct of_phandle_args clkspec;
	const char *clk_name;
	int rc;
	phandle phandle;
	struct device_node *node = NULL;

	if (index < 0)
		return NULL;

	rc = of_parse_phandle_with_args(np, "rockchip,clocks-init-parent",
			"#clock-init-cells", index, &clkspec);
	if (rc)
		return NULL;

	if (of_property_read_string_index(clkspec.np, "clock-output-names", 0,
				&clk_name) < 0)
		return NULL;


	phandle = clkspec.args[0];

	of_node_put(clkspec.np);

	if (phandle) {

		node = of_find_node_by_phandle(phandle);
		if (!node) {
			return NULL;
		}

		if (of_property_read_string_index(node, "clock-output-names", 0,
					clk_child_name) < 0)
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


	cnt_parent = of_count_phandle_with_args(np,
			"rockchip,clocks-init-parent", "#clock-init-cells");

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

	cnt_rate = of_count_phandle_with_args(np, "rockchip,clocks-init-rate",
			"#clock-init-cells");

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

