/*
 * Hisilicon Hi3620 clock gate driver
 *
 * Copyright (c) 2012-2013 Hisilicon Limited.
 * Copyright (c) 2012-2013 Linaro Limited.
 *
 * Author: Haojian Zhuang <haojian.zhuang@linaro.org>
 *	   Xin Li <li.xin@linaro.org>
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
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef	__HISI_CLK_H
#define	__HISI_CLK_H

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/spinlock.h>

struct hisi_clock_data {
	struct clk_onecell_data	clk_data;
	void __iomem		*base;
};

struct hisi_fixed_rate_clock {
	unsigned int		id;
	char			*name;
	const char		*parent_name;
	unsigned long		flags;
	unsigned long		fixed_rate;
};

struct hisi_fixed_factor_clock {
	unsigned int		id;
	char			*name;
	const char		*parent_name;
	unsigned long		mult;
	unsigned long		div;
	unsigned long		flags;
};

struct hisi_mux_clock {
	unsigned int		id;
	const char		*name;
	const char		**parent_names;
	u8			num_parents;
	unsigned long		flags;
	unsigned long		offset;
	u8			shift;
	u8			width;
	u8			mux_flags;
	const char		*alias;
};

struct hisi_divider_clock {
	unsigned int		id;
	const char		*name;
	const char		*parent_name;
	unsigned long		flags;
	unsigned long		offset;
	u8			shift;
	u8			width;
	u8			div_flags;
	struct clk_div_table	*table;
	const char		*alias;
};

struct hisi_gate_clock {
	unsigned int		id;
	const char		*name;
	const char		*parent_name;
	unsigned long		flags;
	unsigned long		offset;
	u8			bit_idx;
	u8			gate_flags;
	const char		*alias;
};

struct clk *hisi_register_clkgate_sep(struct device *, const char *,
				const char *, unsigned long,
				void __iomem *, u8,
				u8, spinlock_t *);

struct hisi_clock_data __init *hisi_clk_init(struct device_node *, int);
void __init hisi_clk_register_fixed_rate(struct hisi_fixed_rate_clock *,
					int, struct hisi_clock_data *);
void __init hisi_clk_register_fixed_factor(struct hisi_fixed_factor_clock *,
					int, struct hisi_clock_data *);
void __init hisi_clk_register_mux(struct hisi_mux_clock *, int,
				struct hisi_clock_data *);
void __init hisi_clk_register_divider(struct hisi_divider_clock *,
				int, struct hisi_clock_data *);
void __init hisi_clk_register_gate_sep(struct hisi_gate_clock *,
					int, struct hisi_clock_data *);
#endif	/* __HISI_CLK_H */
