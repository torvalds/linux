/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2014 Marvell Technology Group Ltd.
 *
 * Alexandre Belloni <alexandre.belloni@free-electrons.com>
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 */
#ifndef __BERLIN2_PLL_H
#define __BERLIN2_PLL_H

struct berlin2_pll_map {
	const u8 vcodiv[16];
	u8 mult;
	u8 fbdiv_shift;
	u8 rfdiv_shift;
	u8 divsel_shift;
};

int berlin2_pll_register(const struct berlin2_pll_map *map,
			 void __iomem *base, const char *name,
			 const char *parent_name, unsigned long flags);

#endif /* __BERLIN2_PLL_H */
