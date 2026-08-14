/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ULTRARISC_CLK_ULTRARISC_H
#define __ULTRARISC_CLK_ULTRARISC_H

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/types.h>

struct ultrarisc_pll_layout {
	u32 cfg1_offset;
	u32 cfg2_offset;
	u32 frac_mask;
	u32 m_mask;
	u32 n_mask;
	u32 oddiv1_mask;
	u32 oddiv2_mask;
};

struct ultrarisc_pll_desc {
	u32 id;
	const char *name;
};

struct ultrarisc_fixed_factor_desc {
	u32 id;
	const char *name;
	u32 parent_id;
	u32 mult;
	u32 div;
};

struct ultrarisc_divider_desc {
	u32 id;
	const char *name;
	u32 offset;
	u32 parent_id;
	unsigned long max_rate;
	u32 load_mask;
	u8 div_shift;
	u8 div_width;
	u8 gate_bit;
	u16 divider_flags;
	u8 gate_flags;
};

struct ultrarisc_gate_desc {
	u32 id;
	const char *name;
	u32 offset;
	u32 parent_id;
	u8 gate_bit;
	u8 gate_flags;
};

struct ultrarisc_clk_soc_data {
	const struct ultrarisc_pll_layout *pll_layout;
	const struct ultrarisc_pll_desc *plls;
	u32 num_plls;
	const struct ultrarisc_fixed_factor_desc *fixed_factors;
	u32 num_fixed_factors;
	const struct ultrarisc_divider_desc *dividers;
	u32 num_dividers;
	const struct ultrarisc_gate_desc *gates;
	u32 num_gates;
	u32 num_clks;
};

int ultrarisc_clk_probe(struct platform_device *pdev,
			const struct ultrarisc_clk_soc_data *soc_data);

#endif /* __ULTRARISC_CLK_ULTRARISC_H */
