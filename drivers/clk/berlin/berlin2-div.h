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
#ifndef __BERLIN2_DIV_H
#define __BERLIN2_DIV_H

struct clk;

#define BERLIN2_DIV_HAS_GATE		BIT(0)
#define BERLIN2_DIV_HAS_MUX		BIT(1)

#define BERLIN2_PLL_SELECT(_off, _sh)	\
	.pll_select_offs = _off,	\
	.pll_select_shift = _sh

#define BERLIN2_PLL_SWITCH(_off, _sh)	\
	.pll_switch_offs = _off,	\
	.pll_switch_shift = _sh

#define BERLIN2_DIV_SELECT(_off, _sh)	\
	.div_select_offs = _off,	\
	.div_select_shift = _sh

#define BERLIN2_DIV_SWITCH(_off, _sh)	\
	.div_switch_offs = _off,	\
	.div_switch_shift = _sh

#define BERLIN2_DIV_D3SWITCH(_off, _sh)	\
	.div3_switch_offs = _off,	\
	.div3_switch_shift = _sh

#define BERLIN2_DIV_GATE(_off, _sh)	\
	.gate_offs = _off,		\
	.gate_shift = _sh

#define BERLIN2_SINGLE_DIV(_off)	\
	BERLIN2_DIV_GATE(_off, 0),	\
	BERLIN2_PLL_SELECT(_off, 1),	\
	BERLIN2_PLL_SWITCH(_off, 4),	\
	BERLIN2_DIV_SWITCH(_off, 5),	\
	BERLIN2_DIV_D3SWITCH(_off, 6),	\
	BERLIN2_DIV_SELECT(_off, 7)

struct berlin2_div_map {
	u16 pll_select_offs;
	u16 pll_switch_offs;
	u16 div_select_offs;
	u16 div_switch_offs;
	u16 div3_switch_offs;
	u16 gate_offs;
	u8 pll_select_shift;
	u8 pll_switch_shift;
	u8 div_select_shift;
	u8 div_switch_shift;
	u8 div3_switch_shift;
	u8 gate_shift;
};

struct berlin2_div_data {
	const char *name;
	const u8 *parent_ids;
	int num_parents;
	unsigned long flags;
	struct berlin2_div_map map;
	u8 div_flags;
};

struct clk * __init
berlin2_div_register(const struct berlin2_div_map *map,
	     void __iomem *base,  const char *name, u8 div_flags,
	     const char **parent_names, int num_parents,
	     unsigned long flags,  spinlock_t *lock);

#endif /* __BERLIN2_DIV_H */
