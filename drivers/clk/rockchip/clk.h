/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * Copyright (c) 2015 Rockchip Electronics Co. Ltd.
 * Author: Xing Zheng <zhengxing@rock-chips.com>
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
#include <linux/clk-provider.h>

struct clk;

#define HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

#define RK2928_PLL_CON(x)		((x) * 0x4)
#define RK2928_MODE_CON		0x40
#define RK2928_CLKSEL_CON(x)	((x) * 0x4 + 0x44)
#define RK2928_CLKGATE_CON(x)	((x) * 0x4 + 0xd0)
#define RK2928_GLB_SRST_FST		0x100
#define RK2928_GLB_SRST_SND		0x104
#define RK2928_SOFTRST_CON(x)	((x) * 0x4 + 0x110)
#define RK2928_MISC_CON		0x134

#define RK3036_SDMMC_CON0		0x144
#define RK3036_SDMMC_CON1		0x148
#define RK3036_SDIO_CON0		0x14c
#define RK3036_SDIO_CON1		0x150
#define RK3036_EMMC_CON0		0x154
#define RK3036_EMMC_CON1		0x158

#define RK3228_GLB_SRST_FST		0x1f0
#define RK3228_GLB_SRST_SND		0x1f4
#define RK3228_SDMMC_CON0		0x1c0
#define RK3228_SDMMC_CON1		0x1c4
#define RK3228_SDIO_CON0		0x1c8
#define RK3228_SDIO_CON1		0x1cc
#define RK3228_EMMC_CON0		0x1d8
#define RK3228_EMMC_CON1		0x1dc

#define RK3288_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3288_MODE_CON			0x50
#define RK3288_CLKSEL_CON(x)		((x) * 0x4 + 0x60)
#define RK3288_CLKGATE_CON(x)		((x) * 0x4 + 0x160)
#define RK3288_GLB_SRST_FST		0x1b0
#define RK3288_GLB_SRST_SND		0x1b4
#define RK3288_SOFTRST_CON(x)		((x) * 0x4 + 0x1b8)
#define RK3288_MISC_CON			0x1e8
#define RK3288_SDMMC_CON0		0x200
#define RK3288_SDMMC_CON1		0x204
#define RK3288_SDIO0_CON0		0x208
#define RK3288_SDIO0_CON1		0x20c
#define RK3288_SDIO1_CON0		0x210
#define RK3288_SDIO1_CON1		0x214
#define RK3288_EMMC_CON0		0x218
#define RK3288_EMMC_CON1		0x21c

#define RK3368_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3368_CLKSEL_CON(x)		((x) * 0x4 + 0x100)
#define RK3368_CLKGATE_CON(x)		((x) * 0x4 + 0x200)
#define RK3368_GLB_SRST_FST		0x280
#define RK3368_GLB_SRST_SND		0x284
#define RK3368_SOFTRST_CON(x)		((x) * 0x4 + 0x300)
#define RK3368_MISC_CON			0x380
#define RK3368_SDMMC_CON0		0x400
#define RK3368_SDMMC_CON1		0x404
#define RK3368_SDIO0_CON0		0x408
#define RK3368_SDIO0_CON1		0x40c
#define RK3368_SDIO1_CON0		0x410
#define RK3368_SDIO1_CON1		0x414
#define RK3368_EMMC_CON0		0x418
#define RK3368_EMMC_CON1		0x41c

#define RK3399_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3399_CLKSEL_CON(x)		((x) * 0x4 + 0x100)
#define RK3399_CLKGATE_CON(x)		((x) * 0x4 + 0x300)
#define RK3399_SOFTRST_CON(x)		((x) * 0x4 + 0x400)
#define RK3399_GLB_SRST_FST		0x500
#define RK3399_GLB_SRST_SND		0x504
#define RK3399_GLB_CNT_TH		0x508
#define RK3399_MISC_CON			0x50c
#define RK3399_RST_CON			0x510
#define RK3399_RST_ST			0x514
#define RK3399_SDMMC_CON0		0x580
#define RK3399_SDMMC_CON1		0x584
#define RK3399_SDIO_CON0		0x588
#define RK3399_SDIO_CON1		0x58c

#define RK3399_PMU_PLL_CON(x)		RK2928_PLL_CON(x)
#define RK3399_PMU_CLKSEL_CON(x)	((x) * 0x4 + 0x80)
#define RK3399_PMU_CLKGATE_CON(x)	((x) * 0x4 + 0x100)
#define RK3399_PMU_SOFTRST_CON(x)	((x) * 0x4 + 0x110)

enum rockchip_pll_type {
	pll_rk3036,
	pll_rk3066,
	pll_rk3399,
};

#define RK3036_PLL_RATE(_rate, _refdiv, _fbdiv, _postdiv1,	\
			_postdiv2, _dsmpd, _frac)		\
{								\
	.rate	= _rate##U,					\
	.fbdiv = _fbdiv,					\
	.postdiv1 = _postdiv1,					\
	.refdiv = _refdiv,					\
	.postdiv2 = _postdiv2,					\
	.dsmpd = _dsmpd,					\
	.frac = _frac,						\
}

#define RK3066_PLL_RATE(_rate, _nr, _nf, _no)	\
{						\
	.rate	= _rate##U,			\
	.nr = _nr,				\
	.nf = _nf,				\
	.no = _no,				\
	.nb = ((_nf) < 2) ? 1 : (_nf) >> 1,	\
}

#define RK3066_PLL_RATE_NB(_rate, _nr, _nf, _no, _nb)		\
{								\
	.rate	= _rate##U,					\
	.nr = _nr,						\
	.nf = _nf,						\
	.no = _no,						\
	.nb = _nb,						\
}

/**
 * struct rockchip_clk_provider - information about clock provider
 * @reg_base: virtual address for the register base.
 * @clk_data: holds clock related data like clk* and number of clocks.
 * @cru_node: device-node of the clock-provider
 * @grf: regmap of the general-register-files syscon
 * @lock: maintains exclusion between callbacks for a given clock-provider.
 */
struct rockchip_clk_provider {
	void __iomem *reg_base;
	struct clk_onecell_data clk_data;
	struct device_node *cru_node;
	struct regmap *grf;
	spinlock_t lock;
};

struct rockchip_pll_rate_table {
	unsigned long rate;
	unsigned int nr;
	unsigned int nf;
	unsigned int no;
	unsigned int nb;
	/* for RK3036/RK3399 */
	unsigned int fbdiv;
	unsigned int postdiv1;
	unsigned int refdiv;
	unsigned int postdiv2;
	unsigned int dsmpd;
	unsigned int frac;
};

/**
 * struct rockchip_pll_clock - information about pll clock
 * @id: platform specific id of the clock.
 * @name: name of this pll clock.
 * @parent_names: name of the parent clock.
 * @num_parents: number of parents
 * @flags: optional flags for basic clock.
 * @con_offset: offset of the register for configuring the PLL.
 * @mode_offset: offset of the register for configuring the PLL-mode.
 * @mode_shift: offset inside the mode-register for the mode of this pll.
 * @lock_shift: offset inside the lock register for the lock status.
 * @type: Type of PLL to be registered.
 * @pll_flags: hardware-specific flags
 * @rate_table: Table of usable pll rates
 *
 * Flags:
 * ROCKCHIP_PLL_SYNC_RATE - check rate parameters to match against the
 *	rate_table parameters and ajust them if necessary.
 */
struct rockchip_pll_clock {
	unsigned int		id;
	const char		*name;
	const char		*const *parent_names;
	u8			num_parents;
	unsigned long		flags;
	int			con_offset;
	int			mode_offset;
	int			mode_shift;
	int			lock_shift;
	enum rockchip_pll_type	type;
	u8			pll_flags;
	struct rockchip_pll_rate_table *rate_table;
};

#define ROCKCHIP_PLL_SYNC_RATE		BIT(0)

#define PLL(_type, _id, _name, _pnames, _flags, _con, _mode, _mshift,	\
		_lshift, _pflags, _rtable)				\
	{								\
		.id		= _id,					\
		.type		= _type,				\
		.name		= _name,				\
		.parent_names	= _pnames,				\
		.num_parents	= ARRAY_SIZE(_pnames),			\
		.flags		= CLK_GET_RATE_NOCACHE | _flags,	\
		.con_offset	= _con,					\
		.mode_offset	= _mode,				\
		.mode_shift	= _mshift,				\
		.lock_shift	= _lshift,				\
		.pll_flags	= _pflags,				\
		.rate_table	= _rtable,				\
	}

struct clk *rockchip_clk_register_pll(struct rockchip_clk_provider *ctx,
		enum rockchip_pll_type pll_type,
		const char *name, const char *const *parent_names,
		u8 num_parents, int con_offset, int grf_lock_offset,
		int lock_shift, int mode_offset, int mode_shift,
		struct rockchip_pll_rate_table *rate_table,
		u8 clk_pll_flags);

struct rockchip_cpuclk_clksel {
	int reg;
	u32 val;
};

#define ROCKCHIP_CPUCLK_NUM_DIVIDERS	2
struct rockchip_cpuclk_rate_table {
	unsigned long prate;
	struct rockchip_cpuclk_clksel divs[ROCKCHIP_CPUCLK_NUM_DIVIDERS];
};

/**
 * struct rockchip_cpuclk_reg_data - register offsets and masks of the cpuclock
 * @core_reg:		register offset of the core settings register
 * @div_core_shift:	core divider offset used to divide the pll value
 * @div_core_mask:	core divider mask
 * @mux_core_alt:	mux value to select alternate parent
 * @mux_core_main:	mux value to select main parent of core
 * @mux_core_shift:	offset of the core multiplexer
 * @mux_core_mask:	core multiplexer mask
 */
struct rockchip_cpuclk_reg_data {
	int		core_reg;
	u8		div_core_shift;
	u32		div_core_mask;
	u8		mux_core_alt;
	u8		mux_core_main;
	u8		mux_core_shift;
	u32		mux_core_mask;
};

struct clk *rockchip_clk_register_cpuclk(const char *name,
			const char *const *parent_names, u8 num_parents,
			const struct rockchip_cpuclk_reg_data *reg_data,
			const struct rockchip_cpuclk_rate_table *rates,
			int nrates, void __iomem *reg_base, spinlock_t *lock);

struct clk *rockchip_clk_register_mmc(const char *name,
				const char *const *parent_names, u8 num_parents,
				void __iomem *reg, int shift);

#define ROCKCHIP_INVERTER_HIWORD_MASK	BIT(0)

struct clk *rockchip_clk_register_inverter(const char *name,
				const char *const *parent_names, u8 num_parents,
				void __iomem *reg, int shift, int flags,
				spinlock_t *lock);

#define PNAME(x) static const char *const x[] __initconst

enum rockchip_clk_branch_type {
	branch_composite,
	branch_mux,
	branch_divider,
	branch_fraction_divider,
	branch_gate,
	branch_mmc,
	branch_inverter,
	branch_factor,
};

struct rockchip_clk_branch {
	unsigned int			id;
	enum rockchip_clk_branch_type	branch_type;
	const char			*name;
	const char			*const *parent_names;
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
	struct rockchip_clk_branch	*child;
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

#define COMPOSITE_NOGATE_DIVTBL(_id, cname, pnames, f, mo, ms,	\
				mw, mf, ds, dw, df, dt)		\
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
		.div_table	= dt,				\
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

#define COMPOSITE_FRACMUX(_id, cname, pname, f, mo, df, go, gs, gf, ch) \
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
		.child		= ch,				\
	}

#define COMPOSITE_FRACMUX_NOGATE(_id, cname, pname, f, mo, df, ch) \
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
		.gate_offset	= -1,				\
		.child		= ch,				\
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

#define MMC(_id, cname, pname, offset, shift)			\
	{							\
		.id		= _id,				\
		.branch_type	= branch_mmc,			\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.muxdiv_offset	= offset,			\
		.div_shift	= shift,			\
	}

#define INVERTER(_id, cname, pname, io, is, if)			\
	{							\
		.id		= _id,				\
		.branch_type	= branch_inverter,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.muxdiv_offset	= io,				\
		.div_shift	= is,				\
		.div_flags	= if,				\
	}

#define FACTOR(_id, cname, pname,  f, fm, fd)			\
	{							\
		.id		= _id,				\
		.branch_type	= branch_factor,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.div_shift	= fm,				\
		.div_width	= fd,				\
	}

#define FACTOR_GATE(_id, cname, pname,  f, fm, fd, go, gb, gf)	\
	{							\
		.id		= _id,				\
		.branch_type	= branch_factor,		\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.div_shift	= fm,				\
		.div_width	= fd,				\
		.gate_offset	= go,				\
		.gate_shift	= gb,				\
		.gate_flags	= gf,				\
	}

struct rockchip_clk_provider *rockchip_clk_init(struct device_node *np,
			void __iomem *base, unsigned long nr_clks);
void rockchip_clk_of_add_provider(struct device_node *np,
				struct rockchip_clk_provider *ctx);
struct regmap *rockchip_clk_get_grf(struct rockchip_clk_provider *ctx);
void rockchip_clk_add_lookup(struct rockchip_clk_provider *ctx,
			     struct clk *clk, unsigned int id);
void rockchip_clk_register_branches(struct rockchip_clk_provider *ctx,
				    struct rockchip_clk_branch *list,
				    unsigned int nr_clk);
void rockchip_clk_register_plls(struct rockchip_clk_provider *ctx,
				struct rockchip_pll_clock *pll_list,
				unsigned int nr_pll, int grf_lock_offset);
void rockchip_clk_register_armclk(struct rockchip_clk_provider *ctx,
			unsigned int lookup_id, const char *name,
			const char *const *parent_names, u8 num_parents,
			const struct rockchip_cpuclk_reg_data *reg_data,
			const struct rockchip_cpuclk_rate_table *rates,
			int nrates);
void rockchip_clk_protect_critical(const char *const clocks[], int nclocks);
void rockchip_register_restart_notifier(struct rockchip_clk_provider *ctx,
					unsigned int reg, void (*cb)(void));

#define ROCKCHIP_SOFTRST_HIWORD_MASK	BIT(0)

#ifdef CONFIG_RESET_CONTROLLER
void rockchip_register_softrst(struct device_node *np,
			       unsigned int num_regs,
			       void __iomem *base, u8 flags);
#else
static inline void rockchip_register_softrst(struct device_node *np,
			       unsigned int num_regs,
			       void __iomem *base, u8 flags)
{
}
#endif

#endif
