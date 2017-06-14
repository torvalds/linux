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
	pll_2126,
	pll_3000,
	pll_35xx,
	pll_36xx,
	pll_2550,
	pll_2650,
	pll_4500,
	pll_4502,
	pll_4508,
	pll_4600,
	pll_4650,
	pll_4650c,
	pll_6552,
	pll_6552_s3c2416,
	pll_6553,
	pll_s3c2410_mpll,
	pll_s3c2410_upll,
	pll_s3c2440_mpll,
	pll_2550x,
	pll_2550xx,
	pll_2650x,
	pll_2650xx,
	pll_1450x,
	pll_1451x,
	pll_1452x,
	pll_1460x,
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

#define PLL_45XX_RATE(_rate, _m, _p, _s, _afc)			\
	{							\
		.rate	=	(_rate),			\
		.mdiv	=	(_m),				\
		.pdiv	=	(_p),				\
		.sdiv	=	(_s),				\
		.afc	=	(_afc),				\
	}

#define PLL_4600_RATE(_rate, _m, _p, _s, _k, _vsel)		\
	{							\
		.rate	=	(_rate),			\
		.mdiv	=	(_m),				\
		.pdiv	=	(_p),				\
		.sdiv	=	(_s),				\
		.kdiv	=	(_k),				\
		.vsel	=	(_vsel),			\
	}

#define PLL_4650_RATE(_rate, _m, _p, _s, _k, _mfr, _mrr, _vsel)	\
	{							\
		.rate	=	(_rate),			\
		.mdiv	=	(_m),				\
		.pdiv	=	(_p),				\
		.sdiv	=	(_s),				\
		.kdiv	=	(_k),				\
		.mfr	=	(_mfr),				\
		.mrr	=	(_mrr),				\
		.vsel	=	(_vsel),			\
	}

/* NOTE: Rate table should be kept sorted in descending order. */

struct samsung_pll_rate_table {
	unsigned int rate;
	unsigned int pdiv;
	unsigned int mdiv;
	unsigned int sdiv;
	unsigned int kdiv;
	unsigned int afc;
	unsigned int mfr;
	unsigned int mrr;
	unsigned int vsel;
};

#endif /* __SAMSUNG_CLK_PLL_H */
