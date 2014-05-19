/*
 * Copyright (c) 2014 Marvell Technology Group Ltd.
 *
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 * Alexandre Belloni <alexandre.belloni@free-electrons.com>
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
#ifndef __BERLIN2_AVPLL_H
#define __BERLIN2_AVPLL_H

struct clk;

#define BERLIN2_AVPLL_BIT_QUIRK		BIT(0)
#define BERLIN2_AVPLL_SCRAMBLE_QUIRK	BIT(1)

struct clk * __init
berlin2_avpll_vco_register(void __iomem *base, const char *name,
	   const char *parent_name, u8 vco_flags, unsigned long flags);

struct clk * __init
berlin2_avpll_channel_register(void __iomem *base, const char *name,
		       u8 index, const char *parent_name, u8 ch_flags,
		       unsigned long flags);

#endif /* __BERLIN2_AVPLL_H */
