// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 * Author: Elaine Zhang <zhangqing@rock-chips.com>
 */
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/log2.h>

static int cru_debug;

#define cru_dbg(format, ...) do {				\
		if (cru_debug)					\
			pr_info("%s: " format, __func__, ## __VA_ARGS__); \
	} while (0)

#define PNAME(x) static const char *const x[]

enum vop_clk_branch_type {
	branch_mux,
	branch_divider,
	branch_factor,
	branch_virtual,
};

#define VIR(cname)						\
	{							\
		.branch_type	= branch_virtual,		\
		.name		= cname,			\
	}


#define MUX(cname, pnames, f)			\
	{							\
		.branch_type	= branch_mux,			\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
	}

#define FACTOR(cname, pname,  f)			\
	{							\
		.branch_type	= branch_factor,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
	}

#define DIV(cname, pname, f, w)			\
	{							\
		.branch_type	= branch_divider,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.div_width	= w,				\
	}

struct vop2_clk_branch {
	enum vop_clk_branch_type	branch_type;
	const char			*name;
	const char			*const *parent_names;
	u8				num_parents;
	unsigned long			flags;
	u8				div_shift;
	u8				div_width;
	u8				div_flags;
};

PNAME(mux_port0_dclk_src_p)		= { "dclk0", "dclk1" };
PNAME(mux_port2_dclk_src_p)		= { "dclk2", "dclk1" };
PNAME(mux_dp_pixclk_p)			= { "dclk_out0", "dclk_out1", "dclk_out2" };
PNAME(mux_hdmi_edp_clk_src_p)		= { "dclk0", "dclk1", "dclk2" };
PNAME(mux_mipi_clk_src_p)		= { "dclk_out1", "dclk_out2", "dclk_out3" };
PNAME(mux_dsc_8k_clk_src_p)		= { "dclk0", "dclk1", "dclk2", "dclk3" };
PNAME(mux_dsc_4k_clk_src_p)		= { "dclk0", "dclk1", "dclk2", "dclk3" };

/*
 * We only use this clk driver calculate the div
 * of dclk_core/dclk_out/if_pixclk/if_dclk and
 * the rate of the dclk from the soc.
 *
 * We don't touch the cru in the vop here, as
 * these registers has special read andy write
 * limits.
 */
static struct vop2_clk_branch rk3588_vop_clk_branches[] = {
	VIR("dclk0"),
	VIR("dclk1"),
	VIR("dclk2"),
	VIR("dclk3"),

	MUX("port0_dclk_src", mux_port0_dclk_src_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	DIV("dclk_core0", "port0_dclk_src", CLK_SET_RATE_PARENT, 2),
	DIV("dclk_out0", "port0_dclk_src", CLK_SET_RATE_PARENT, 2),

	FACTOR("port1_dclk_src", "dclk1", CLK_SET_RATE_PARENT),
	DIV("dclk_core1", "port1_dclk_src", CLK_SET_RATE_PARENT, 2),
	DIV("dclk_out1", "port1_dclk_src", CLK_SET_RATE_PARENT, 2),

	MUX("port2_dclk_src", mux_port2_dclk_src_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	DIV("dclk_core2", "port2_dclk_src", CLK_SET_RATE_PARENT, 2),
	DIV("dclk_out2", "port2_dclk_src", CLK_SET_RATE_PARENT, 2),

	FACTOR("port3_dclk_src", "dclk3", CLK_SET_RATE_PARENT),
	DIV("dclk_core3", "port3_dclk_src", CLK_SET_RATE_PARENT, 2),
	DIV("dclk_out3", "port3_dclk_src", CLK_SET_RATE_PARENT, 2),

	MUX("dp0_pixclk", mux_dp_pixclk_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	MUX("dp1_pixclk", mux_dp_pixclk_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),

	MUX("hdmi_edp0_clk_src", mux_hdmi_edp_clk_src_p,
	    CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	DIV("hdmi_edp0_dclk", "hdmi_edp0_clk_src", 0, 2),
	DIV("hdmi_edp0_pixclk", "hdmi_edp0_clk_src", CLK_SET_RATE_PARENT, 1),

	MUX("hdmi_edp1_clk_src", mux_hdmi_edp_clk_src_p,
	    CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	DIV("hdmi_edp1_dclk", "hdmi_edp1_clk_src", 0, 2),
	DIV("hdmi_edp1_pixclk", "hdmi_edp1_clk_src", CLK_SET_RATE_PARENT, 1),

	MUX("mipi0_clk_src", mux_mipi_clk_src_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	DIV("mipi0_pixclk", "mipi0_clk_src", CLK_SET_RATE_PARENT, 2),

	MUX("mipi1_clk_src", mux_mipi_clk_src_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	DIV("mipi1_pixclk", "mipi1_clk_src", CLK_SET_RATE_PARENT, 2),

	FACTOR("rgb_pixclk", "port3_dclk_src", CLK_SET_RATE_PARENT),

	MUX("dsc_8k_txp_clk_src", mux_dsc_8k_clk_src_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	DIV("dsc_8k_txp_clk", "dsc_8k_txp_clk_src", 0, 2),
	DIV("dsc_8k_pxl_clk", "dsc_8k_txp_clk_src", 0, 2),
	DIV("dsc_8k_cds_clk", "dsc_8k_txp_clk_src", 0, 2),

	MUX("dsc_4k_txp_clk_src", mux_dsc_4k_clk_src_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	DIV("dsc_4k_txp_clk", "dsc_4k_txp_clk_src", 0, 2),
	DIV("dsc_4k_pxl_clk", "dsc_4k_txp_clk_src", 0, 2),
	DIV("dsc_4k_cds_clk", "dsc_4k_txp_clk_src", 0, 2),
};

static unsigned long clk_virtual_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct vop2_clk *vop2_clk = to_vop2_clk(hw);

	return (unsigned long)vop2_clk->rate;
}

static long clk_virtual_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct vop2_clk *vop2_clk = to_vop2_clk(hw);

	vop2_clk->rate = rate;

	cru_dbg("%s rate: %ld\n", clk_hw_get_name(hw), rate);
	return rate;
}

static int clk_virtual_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	return 0;
}

const struct clk_ops clk_virtual_ops = {
	.round_rate = clk_virtual_round_rate,
	.set_rate = clk_virtual_set_rate,
	.recalc_rate = clk_virtual_recalc_rate,
};

static u8 vop2_mux_get_parent(struct clk_hw *hw)
{
	struct vop2_clk *vop2_clk = to_vop2_clk(hw);

	cru_dbg("%s index: %d\n", clk_hw_get_name(hw), vop2_clk->parent_index);
	return vop2_clk->parent_index;
}

static int vop2_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct vop2_clk *vop2_clk = to_vop2_clk(hw);

	vop2_clk->parent_index = index;

	cru_dbg("%s index: %d\n", clk_hw_get_name(hw), index);
	return 0;
}

static int vop2_clk_mux_determine_rate(struct clk_hw *hw,
			     struct clk_rate_request *req)
{
	cru_dbg("%s  %ld(min: %ld max: %ld)\n",
	       clk_hw_get_name(hw), req->rate, req->min_rate, req->max_rate);
	return __clk_mux_determine_rate(hw, req);
}

static const struct clk_ops vop2_mux_clk_ops = {
	.get_parent = vop2_mux_get_parent,
	.set_parent = vop2_mux_set_parent,
	.determine_rate = vop2_clk_mux_determine_rate,
};

#define div_mask(width)	((1 << (width)) - 1)

static int vop2_div_get_val(unsigned long rate, unsigned long parent_rate)
{
	unsigned int div, value;

	div = DIV_ROUND_UP_ULL((u64)parent_rate, rate);

	value = ilog2(div);

	return value;
}

static unsigned long vop2_clk_div_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct vop2_clk *vop2_clk = to_vop2_clk(hw);
	unsigned long rate;
	unsigned int div;

	div =  1 << vop2_clk->div_val;
	rate = parent_rate / div;

	cru_dbg("%s rate: %ld(prate: %ld)\n", clk_hw_get_name(hw), rate, parent_rate);

	return rate;
}

static long vop2_clk_div_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct vop2_clk *vop2_clk = to_vop2_clk(hw);

	if (clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) {
		if (*prate < rate)
			*prate = rate;
		if ((*prate >> vop2_clk->div.width) > rate)
			*prate = rate;

		if ((*prate % rate))
			*prate = rate;
	}

	cru_dbg("%s rate: %ld(prate: %ld)\n", clk_hw_get_name(hw), rate, *prate);

	return rate;
}

static int vop2_clk_div_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate)
{
	struct vop2_clk *vop2_clk = to_vop2_clk(hw);
	int div_val;

	div_val = vop2_div_get_val(rate, parent_rate);
	vop2_clk->div_val = div_val;

	cru_dbg("%s prate: %ld rate: %ld div_val: %d\n",
	       clk_hw_get_name(hw), parent_rate, rate, div_val);

	return 0;
}

static const struct clk_ops vop2_div_clk_ops = {
	.recalc_rate = vop2_clk_div_recalc_rate,
	.round_rate = vop2_clk_div_round_rate,
	.set_rate = vop2_clk_div_set_rate,
};

static struct clk *vop2_clk_register(struct vop2 *vop2, struct vop2_clk_branch *branch)
{
	struct clk_init_data init = {};
	struct vop2_clk *vop2_clk;
	struct clk *clk;

	vop2_clk = devm_kzalloc(vop2->dev, sizeof(*vop2_clk), GFP_KERNEL);
	if (!vop2_clk)
		return ERR_PTR(-ENOMEM);

	vop2_clk->vop2 = vop2;
	vop2_clk->hw.init = &init;
	vop2_clk->div.shift = branch->div_shift;
	vop2_clk->div.width = branch->div_width;

	init.name = branch->name;
	init.flags = branch->flags;
	init.num_parents = branch->num_parents;
	init.parent_names = branch->parent_names;
	if (branch->branch_type == branch_divider) {
		init.ops = &vop2_div_clk_ops;
	} else if (branch->branch_type == branch_virtual) {
		init.ops = &clk_virtual_ops;
		init.num_parents = 0;
		init.parent_names = NULL;
	} else {
		init.ops = &vop2_mux_clk_ops;
	}

	clk = devm_clk_register(vop2->dev, &vop2_clk->hw);
	if (!IS_ERR(clk))
		list_add_tail(&vop2_clk->list, &vop2->clk_list_head);
	else
		DRM_DEV_ERROR(vop2->dev, "Register %s failed\n", branch->name);

	return clk;
}

static int vop2_clk_init(struct vop2 *vop2)
{
	struct vop2_clk_branch *branch = rk3588_vop_clk_branches;
	unsigned int nr_clk = ARRAY_SIZE(rk3588_vop_clk_branches);
	unsigned int idx;
	struct vop2_clk *clk, *n;

	INIT_LIST_HEAD(&vop2->clk_list_head);

	if (vop2->version != VOP_VERSION_RK3588)
		return 0;

	list_for_each_entry_safe(clk, n, &vop2->clk_list_head, list) {
		list_del(&clk->list);
	}

	for (idx = 0; idx < nr_clk; idx++, branch++)
		vop2_clk_register(vop2, branch);

	return 0;
}
