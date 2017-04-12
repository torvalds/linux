/*
 * Copyright (c) 2014 Marvell Technology Group Ltd.
 *
 * Alexandre Belloni <alexandre.belloni@free-electrons.com>
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
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
