/*
 *  linux/arch/arm/mach-mmp/clock.h
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <asm/clkdev.h>

struct clkops {
	void			(*enable)(struct clk *);
	void			(*disable)(struct clk *);
	unsigned long		(*getrate)(struct clk *);
};

struct clk {
	const struct clkops	*ops;

	void __iomem	*clk_rst;	/* clock reset control register */
	int		fnclksel;	/* functional clock select (APBC) */
	uint32_t	enable_val;	/* value for clock enable (APMU) */
	unsigned long	rate;
	int		enabled;
};

extern struct clkops apbc_clk_ops;

#define APBC_CLK(_name, _reg, _fnclksel, _rate)			\
struct clk clk_##_name = {					\
		.clk_rst	= (void __iomem *)APBC_##_reg,	\
		.fnclksel	= _fnclksel,			\
		.rate		= _rate,			\
		.ops		= &apbc_clk_ops,		\
}

#define APBC_CLK_OPS(_name, _reg, _fnclksel, _rate, _ops)	\
struct clk clk_##_name = {					\
		.clk_rst	= (void __iomem *)APBC_##_reg,	\
		.fnclksel	= _fnclksel,			\
		.rate		= _rate,			\
		.ops		= _ops,				\
}

#define APMU_CLK(_name, _reg, _eval, _rate)			\
struct clk clk_##_name = {					\
		.clk_rst	= (void __iomem *)APMU_##_reg,	\
		.enable_val	= _eval,			\
		.rate		= _rate,			\
		.ops		= &apmu_clk_ops,		\
}

#define APMU_CLK_OPS(_name, _reg, _eval, _rate, _ops)		\
struct clk clk_##_name = {					\
		.clk_rst	= (void __iomem *)APMU_##_reg,	\
		.enable_val	= _eval,			\
		.rate		= _rate,			\
		.ops		= _ops,				\
}

#define INIT_CLKREG(_clk, _devname, _conname)			\
	{							\
		.clk		= _clk,				\
		.dev_id		= _devname,			\
		.con_id		= _conname,			\
	}

extern struct clk clk_pxa168_gpio;
extern struct clk clk_pxa168_timers;

extern void clks_register(struct clk_lookup *, size_t);
