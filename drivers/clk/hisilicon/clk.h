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

struct platform_device;

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
	const char		*const *parent_names;
	u8			num_parents;
	unsigned long		flags;
	unsigned long		offset;
	u8			shift;
	u8			width;
	u8			mux_flags;
	u32			*table;
	const char		*alias;
};

struct hisi_phase_clock {
	unsigned int		id;
	const char		*name;
	const char		*parent_names;
	unsigned long		flags;
	unsigned long		offset;
	u8			shift;
	u8			width;
	u32			*phase_degrees;
	u32			*phase_regvals;
	u8			phase_num;
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

struct hi6220_divider_clock {
	unsigned int		id;
	const char		*name;
	const char		*parent_name;
	unsigned long		flags;
	unsigned long		offset;
	u8			shift;
	u8			width;
	u32			mask_bit;
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
struct clk *hi6220_register_clkdiv(struct device *dev, const char *name,
	const char *parent_name, unsigned long flags, void __iomem *reg,
	u8 shift, u8 width, u32 mask_bit, spinlock_t *lock);

struct hisi_clock_data *hisi_clk_alloc(struct platform_device *, int);
struct hisi_clock_data *hisi_clk_init(struct device_node *, int);
int hisi_clk_register_fixed_rate(const struct hisi_fixed_rate_clock *,
				int, struct hisi_clock_data *);
int hisi_clk_register_fixed_factor(const struct hisi_fixed_factor_clock *,
				int, struct hisi_clock_data *);
int hisi_clk_register_mux(const struct hisi_mux_clock *, int,
				struct hisi_clock_data *);
struct clk *clk_register_hisi_phase(struct device *dev,
				const struct hisi_phase_clock *clks,
				void __iomem *base, spinlock_t *lock);
int hisi_clk_register_phase(struct device *dev,
				const struct hisi_phase_clock *clks,
				int nums, struct hisi_clock_data *data);
int hisi_clk_register_divider(const struct hisi_divider_clock *,
				int, struct hisi_clock_data *);
int hisi_clk_register_gate(const struct hisi_gate_clock *,
				int, struct hisi_clock_data *);
void hisi_clk_register_gate_sep(const struct hisi_gate_clock *,
				int, struct hisi_clock_data *);
void hi6220_clk_register_divider(const struct hi6220_divider_clock *,
				int, struct hisi_clock_data *);

#define hisi_clk_unregister(type) \
static inline \
void hisi_clk_unregister_##type(const struct hisi_##type##_clock *clks, \
				int nums, struct hisi_clock_data *data) \
{ \
	struct clk **clocks = data->clk_data.clks; \
	int i; \
	for (i = 0; i < nums; i++) { \
		int id = clks[i].id; \
		if (clocks[id])  \
			clk_unregister_##type(clocks[id]); \
	} \
}

hisi_clk_unregister(fixed_rate)
hisi_clk_unregister(fixed_factor)
hisi_clk_unregister(mux)
hisi_clk_unregister(divider)
hisi_clk_unregister(gate)

#endif	/* __HISI_CLK_H */
