/* SPDX-License-Identifier:    GPL-2.0 */
/*
 * Copyright (C) 2017, Intel Corporation
 */

#ifndef	__STRATIX10_CLK_H
#define	__STRATIX10_CLK_H

struct stratix10_clock_data {
	struct clk_onecell_data	clk_data;
	void __iomem		*base;
};

struct stratix10_pll_clock {
	unsigned int		id;
	const char		*name;
	const char		*const *parent_names;
	u8			num_parents;
	unsigned long		flags;
	unsigned long		offset;
};

struct stratix10_perip_c_clock {
	unsigned int		id;
	const char		*name;
	const char		*parent_name;
	const char		*const *parent_names;
	u8			num_parents;
	unsigned long		flags;
	unsigned long		offset;
};

struct stratix10_perip_cnt_clock {
	unsigned int		id;
	const char		*name;
	const char		*parent_name;
	const char		*const *parent_names;
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
	const char		*const *parent_names;
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

struct clk *s10_register_pll(const char *, const char *const *, u8,
			     unsigned long, void __iomem *, unsigned long);

struct clk *s10_register_periph(const char *, const char *,
				const char * const *, u8, unsigned long,
				void __iomem *, unsigned long);
struct clk *s10_register_cnt_periph(const char *, const char *,
				    const char * const *, u8,
				    unsigned long, void __iomem *,
				    unsigned long, u8, unsigned long,
				    unsigned long);
struct clk *s10_register_gate(const char *, const char *,
			      const char * const *, u8,
			      unsigned long, void __iomem *,
			      unsigned long, unsigned long,
			      unsigned long, unsigned long, u8,
			      unsigned long, u8, u8);
#endif	/* __STRATIX10_CLK_H */
