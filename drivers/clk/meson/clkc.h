/*
 * Copyright (c) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __CLKC_H
#define __CLKC_H

#define PMASK(width)			GENMASK(width - 1, 0)
#define SETPMASK(width, shift)		GENMASK(shift + width - 1, shift)
#define CLRPMASK(width, shift)		(~SETPMASK(width, shift))

#define PARM_GET(width, shift, reg)					\
	(((reg) & SETPMASK(width, shift)) >> (shift))
#define PARM_SET(width, shift, reg, val)				\
	(((reg) & CLRPMASK(width, shift)) | (val << (shift)))

#define MESON_PARM_APPLICABLE(p)		(!!((p)->width))

struct parm {
	u16	reg_off;
	u8	shift;
	u8	width;
};
#define PARM(_r, _s, _w)						\
	{								\
		.reg_off	= (_r),					\
		.shift		= (_s),					\
		.width		= (_w),					\
	}								\

struct pll_rate_table {
	unsigned long	rate;
	u16		m;
	u16		n;
	u16		od;
};
#define PLL_RATE(_r, _m, _n, _od)					\
	{								\
		.rate		= (_r),					\
		.m		= (_m),					\
		.n		= (_n),					\
		.od		= (_od),				\
	}								\

struct pll_conf {
	const struct pll_rate_table	*rate_table;
	struct parm			m;
	struct parm			n;
	struct parm			od;
};

struct fixed_fact_conf {
	unsigned int	div;
	unsigned int	mult;
	struct parm	div_parm;
	struct parm	mult_parm;
};

struct fixed_rate_conf {
	unsigned long	rate;
	struct parm	rate_parm;
};

struct composite_conf {
	struct parm		mux_parm;
	struct parm		div_parm;
	struct parm		gate_parm;
	struct clk_div_table	*div_table;
	u32			*mux_table;
	u8			mux_flags;
	u8			div_flags;
	u8			gate_flags;
};

#define PNAME(x) static const char *x[]

enum clk_type {
	CLK_FIXED_FACTOR,
	CLK_FIXED_RATE,
	CLK_COMPOSITE,
	CLK_CPU,
	CLK_PLL,
};

struct clk_conf {
	u16				reg_off;
	enum clk_type			clk_type;
	unsigned int			clk_id;
	const char			*clk_name;
	const char			**clks_parent;
	int				num_parents;
	unsigned long			flags;
	union {
		struct fixed_fact_conf		fixed_fact;
		struct fixed_rate_conf		fixed_rate;
		const struct composite_conf		*composite;
		struct pll_conf			*pll;
		const struct clk_div_table	*div_table;
	} conf;
};

#define FIXED_RATE_P(_ro, _ci, _cn, _f, _c)				\
	{								\
		.reg_off			= (_ro),		\
		.clk_type			= CLK_FIXED_RATE,	\
		.clk_id				= (_ci),		\
		.clk_name			= (_cn),		\
		.flags				= (_f),			\
		.conf.fixed_rate.rate_parm	= _c,			\
	}								\

#define FIXED_RATE(_ci, _cn, _f, _r)					\
	{								\
		.clk_type			= CLK_FIXED_RATE,	\
		.clk_id				= (_ci),		\
		.clk_name			= (_cn),		\
		.flags				= (_f),			\
		.conf.fixed_rate.rate		= (_r),			\
	}								\

#define PLL(_ro, _ci, _cn, _cp, _f, _c)					\
	{								\
		.reg_off			= (_ro),		\
		.clk_type			= CLK_PLL,		\
		.clk_id				= (_ci),		\
		.clk_name			= (_cn),		\
		.clks_parent			= (_cp),		\
		.num_parents			= ARRAY_SIZE(_cp),	\
		.flags				= (_f),			\
		.conf.pll			= (_c),			\
	}								\

#define FIXED_FACTOR_DIV(_ci, _cn, _cp, _f, _d)				\
	{								\
		.clk_type			= CLK_FIXED_FACTOR,	\
		.clk_id				= (_ci),		\
		.clk_name			= (_cn),		\
		.clks_parent			= (_cp),		\
		.num_parents			= ARRAY_SIZE(_cp),	\
		.conf.fixed_fact.div		= (_d),			\
	}								\

#define CPU(_ro, _ci, _cn, _cp, _dt)					\
	{								\
		.reg_off			= (_ro),		\
		.clk_type			= CLK_CPU,		\
		.clk_id				= (_ci),		\
		.clk_name			= (_cn),		\
		.clks_parent			= (_cp),		\
		.num_parents			= ARRAY_SIZE(_cp),	\
		.conf.div_table			= (_dt),		\
	}								\

#define COMPOSITE(_ro, _ci, _cn, _cp, _f, _c)				\
	{								\
		.reg_off			= (_ro),		\
		.clk_type			= CLK_COMPOSITE,	\
		.clk_id				= (_ci),		\
		.clk_name			= (_cn),		\
		.clks_parent			= (_cp),		\
		.num_parents			= ARRAY_SIZE(_cp),	\
		.flags				= (_f),			\
		.conf.composite			= (_c),			\
	}								\

struct clk **meson_clk_init(struct device_node *np, unsigned long nr_clks);
void meson_clk_register_clks(const struct clk_conf *clk_confs,
			     unsigned int nr_confs, void __iomem *clk_base);
struct clk *meson_clk_register_cpu(const struct clk_conf *clk_conf,
				   void __iomem *reg_base, spinlock_t *lock);
struct clk *meson_clk_register_pll(const struct clk_conf *clk_conf,
				   void __iomem *reg_base, spinlock_t *lock);

#endif /* __CLKC_H */
