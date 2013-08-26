/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Copyright (c) 2013 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for all PLL's in Samsung platforms
*/

#ifndef __SAMSUNG_CLK_PLL_H
#define __SAMSUNG_CLK_PLL_H

enum samsung_pll_type {
	pll_35xx,
	pll_36xx,
	pll_2550,
	pll_2650,
	pll_4500,
	pll_4502,
	pll_4508,
	pll_6552,
	pll_6553,
};

#define PLL_35XX_RATE(_rate, _m, _p, _s)			\
	{							\
		.rate	=	(_rate),				\
		.mdiv	=	(_m),				\
		.pdiv	=	(_p),				\
		.sdiv	=	(_s),				\
	}

#define PLL_36XX_RATE(_rate, _m, _p, _s, _k)			\
	{							\
		.rate	=	(_rate),				\
		.mdiv	=	(_m),				\
		.pdiv	=	(_p),				\
		.sdiv	=	(_s),				\
		.kdiv	=	(_k),				\
	}

/* NOTE: Rate table should be kept sorted in descending order. */

struct samsung_pll_rate_table {
	unsigned int rate;
	unsigned int pdiv;
	unsigned int mdiv;
	unsigned int sdiv;
	unsigned int kdiv;
};

enum pll46xx_type {
	pll_4600,
	pll_4650,
	pll_4650c,
};

extern struct clk * __init samsung_clk_register_pll46xx(const char *name,
			const char *pname, const void __iomem *con_reg,
			enum pll46xx_type type);
extern struct clk * __init samsung_clk_register_pll2550x(const char *name,
			const char *pname, const void __iomem *reg_base,
			const unsigned long offset);

#endif /* __SAMSUNG_CLK_PLL_H */
