/* SPDX-License-Identifier:    GPL-2.0 */
/*
 * Copyright (C) 2017, Intel Corporation
 */

#ifndef	__STRATIX10_CLK_H
#define	__STRATIX10_CLK_H

struct stratix10_clock_data {
	void __iomem		*base;

	/* Must be last */
	struct clk_hw_onecell_data	clk_data;
};

struct stratix10_pll_clock {
	unsigned int		id;
	const char		*name;
	const struct clk_parent_data	*parent_data;
	u8			num_parents;
	unsigned long		flags;
	unsigned long		offset;
};

struct stratix10_perip_c_clock {
	unsigned int		id;
	const char		*name;
	const char		*parent_name;
	const struct clk_parent_data	*parent_data;
	u8			num_parents;
	unsigned long		flags;
	unsigned long		offset;
};

struct n5x_perip_c_clock {
	unsigned int		id;
	const char		*name;
	const char		*parent_name;
	const char		*const *parent_names;
	u8			num_parents;
	unsigned long		flags;
	unsigned long		offset;
	unsigned long		shift;
};

struct stratix10_perip_cnt_clock {
	unsigned int		id;
	const char		*name;
	const char		*parent_name;
	const struct clk_parent_data	*parent_data;
	u8			num_parents;
	unsigned long		flags;
	unsigned long		offset;
	u8			fixed_divider;
	unsigned long		bypass_reg;
	unsigned long		bypass_shift;
};

struct stratix10_gate_clock {
	unsigned int		id;
	const char		*name;
	const char		*parent_name;
	const struct clk_parent_data	*parent_data;
	u8			num_parents;
	unsigned long		flags;
	unsigned long		gate_reg;
	u8			gate_idx;
	unsigned long		div_reg;
	u8			div_offset;
	u8			div_width;
	unsigned long		bypass_reg;
	u8			bypass_shift;
	u8			fixed_div;
};

struct clk_hw *s10_register_pll(const struct stratix10_pll_clock *clks,
			     void __iomem *reg);
struct clk_hw *agilex_register_pll(const struct stratix10_pll_clock *clks,
				void __iomem *reg);
struct clk_hw *n5x_register_pll(const struct stratix10_pll_clock *clks,
			     void __iomem *reg);
struct clk_hw *s10_register_periph(const struct stratix10_perip_c_clock *clks,
				void __iomem *reg);
struct clk_hw *n5x_register_periph(const struct n5x_perip_c_clock *clks,
				void __iomem *reg);
struct clk_hw *s10_register_cnt_periph(const struct stratix10_perip_cnt_clock *clks,
				    void __iomem *reg);
struct clk_hw *s10_register_gate(const struct stratix10_gate_clock *clks,
			      void __iomem *reg);
struct clk_hw *agilex_register_gate(const struct stratix10_gate_clock *clks,
			      void __iomem *reg);
#endif	/* __STRATIX10_CLK_H */
