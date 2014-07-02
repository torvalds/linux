/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * based on
 *
 * samsung/clk.h
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Copyright (c) 2013 Linaro Ltd.
 * Author: Thomas Abraham <thomas.ab@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef CLK_ROCKCHIP_CLK_H
#define CLK_ROCKCHIP_CLK_H

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

#define HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

/* register positions shared by RK2928, RK3066 and RK3188 */
#define RK2928_PLL_CON(x)		(x * 0x4)
#define RK2928_MODE_CON		0x40
#define RK2928_CLKSEL_CON(x)	(x * 0x4 + 0x44)
#define RK2928_CLKGATE_CON(x)	(x * 0x4 + 0xd0)
#define RK2928_GLB_SRST_FST		0x100
#define RK2928_GLB_SRST_SND		0x104
#define RK2928_SOFTRST_CON(x)	(x * 0x4 + 0x110)
#define RK2928_MISC_CON		0x134

#define PNAME(x) static const char *x[] __initconst

enum rockchip_clk_branch_type {
	branch_composite,
	branch_mux,
	branch_divider,
	branch_fraction_divider,
	branch_gate,
};

struct rockchip_clk_branch {
	unsigned int			id;
	enum rockchip_clk_branch_type	branch_type;
	const char			*name;
	const char			**parent_names;
	u8				num_parents;
	unsigned long			flags;
	int				muxdiv_offset;
	u8				mux_shift;
	u8				mux_width;
	u8				mux_flags;
	u8				div_shift;
	u8				div_width;
	u8				div_flags;
	struct clk_div_table		*div_table;
	int				gate_offset;
	u8				gate_shift;
	u8				gate_flags;
};

#define COMPOSITE(_id, cname, pnames, f, mo, ms, mw, mf, ds, dw,\
		  df, go, gs, gf)				\
	{							\
		.id		= _id,				\
		.branch_type	= branch_composite,		\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.mux_shift	= ms,				\
		.mux_width	= mw,				\
		.mux_flags	= mf,				\
		.div_shift	= ds,				\
		.div_width	= dw,				\
		.div_flags	= df,				\
		.gate_offset	= go,				\
		.gate_shift	= gs,				\
		.gate_flags	= gf,				\
	}

#define COMPOSITE_NOMUX(_id, cname, pname, f, mo, ds, dw, df,	\
			go, gs, gf)				\
	{							\
		.id		= _id,				\
		.branch_type	= branch_composite,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.div_shift	= ds,				\
		.div_width	= dw,				\
		.div_flags	= df,				\
		.gate_offset	= go,				\
		.gate_shift	= gs,				\
		.gate_flags	= gf,				\
	}

#define COMPOSITE_NOMUX_DIVTBL(_id, cname, pname, f, mo, ds, dw,\
			       df, dt, go, gs, gf)		\
	{							\
		.id		= _id,				\
		.branch_type	= branch_composite,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.div_shift	= ds,				\
		.div_width	= dw,				\
		.div_flags	= df,				\
		.div_table	= dt,				\
		.gate_offset	= go,				\
		.gate_shift	= gs,				\
		.gate_flags	= gf,				\
	}

#define COMPOSITE_NODIV(_id, cname, pnames, f, mo, ms, mw, mf,	\
			go, gs, gf)				\
	{							\
		.id		= _id,				\
		.branch_type	= branch_composite,		\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.mux_shift	= ms,				\
		.mux_width	= mw,				\
		.mux_flags	= mf,				\
		.gate_offset	= go,				\
		.gate_shift	= gs,				\
		.gate_flags	= gf,				\
	}

#define COMPOSITE_NOGATE(_id, cname, pnames, f, mo, ms, mw, mf,	\
			 ds, dw, df)				\
	{							\
		.id		= _id,				\
		.branch_type	= branch_composite,		\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.mux_shift	= ms,				\
		.mux_width	= mw,				\
		.mux_flags	= mf,				\
		.div_shift	= ds,				\
		.div_width	= dw,				\
		.div_flags	= df,				\
		.gate_offset	= -1,				\
	}

#define COMPOSITE_FRAC(_id, cname, pname, f, mo, df, go, gs, gf)\
	{							\
		.id		= _id,				\
		.branch_type	= branch_fraction_divider,	\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.div_shift	= 16,				\
		.div_width	= 16,				\
		.div_flags	= df,				\
		.gate_offset	= go,				\
		.gate_shift	= gs,				\
		.gate_flags	= gf,				\
	}

#define MUX(_id, cname, pnames, f, o, s, w, mf)			\
	{							\
		.id		= _id,				\
		.branch_type	= branch_mux,			\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset	= o,				\
		.mux_shift	= s,				\
		.mux_width	= w,				\
		.mux_flags	= mf,				\
		.gate_offset	= -1,				\
	}

#define DIV(_id, cname, pname, f, o, s, w, df)			\
	{							\
		.id		= _id,				\
		.branch_type	= branch_divider,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.muxdiv_offset	= o,				\
		.div_shift	= s,				\
		.div_width	= w,				\
		.div_flags	= df,				\
		.gate_offset	= -1,				\
	}

#define DIVTBL(_id, cname, pname, f, o, s, w, df, dt)		\
	{							\
		.id		= _id,				\
		.branch_type	= branch_divider,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.muxdiv_offset	= o,				\
		.div_shift	= s,				\
		.div_width	= w,				\
		.div_flags	= df,				\
		.div_table	= dt,				\
	}

#define GATE(_id, cname, pname, f, o, b, gf)			\
	{							\
		.id		= _id,				\
		.branch_type	= branch_gate,			\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.gate_offset	= o,				\
		.gate_shift	= b,				\
		.gate_flags	= gf,				\
	}


void rockchip_clk_init(struct device_node *np, void __iomem *base,
		       unsigned long nr_clks);
void rockchip_clk_add_lookup(struct clk *clk, unsigned int id);
void rockchip_clk_register_branches(struct rockchip_clk_branch *clk_list,
				    unsigned int nr_clk);

#endif
