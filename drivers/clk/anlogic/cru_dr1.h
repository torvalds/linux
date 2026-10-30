/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024-2025 Anlogic, Inc.
 * Copyright (C) 2026 Junhui Liu <junhui.liu@pigmoral.tech>
 */

#ifndef _CRU_DR1_H_
#define _CRU_DR1_H_

#include <linux/clk-provider.h>

struct cru_pll {
	struct clk_hw hw;
	void __iomem *reg;
};

struct cru_div_gate {
	struct clk_divider divider;
	u32 val; /* Cached divider value for restoring on enable */
	u8 min; /* Minimum divider value to avoid timing issues */
};

struct cru_clk {
	struct clk_hw *hw;
	void **reg;
};

#define CRU_PARENT_NAME(_name)		{ .fw_name = #_name }
#define CRU_PARENT_HW(_parent)		{ .hw = &_parent.hw }
#define CRU_PARENT_DIV_HW(_parent)	{ .hw = &_parent.divider.hw }

#define CRU_INITHW(_name, _parent, _ops)				\
	.hw.init = &(struct clk_init_data) {				\
		.name		= #_name,				\
		.parent_data	= (const struct clk_parent_data[])	\
					{ _parent },			\
		.num_parents	= 1,					\
		.ops		= &_ops,				\
	}

#define CRU_INITHW_PARENTS(_name, _parents, _ops)			\
	.hw.init = CLK_HW_INIT_PARENTS_DATA(#_name, _parents, &_ops, 0)

#define CRU_PLL_NM_DEFINE(_name, _parent, _reg)				\
static struct cru_pll _name = {						\
	.reg = (void __iomem *)(_reg),					\
	CRU_INITHW(_name, _parent, dr1_cru_pll_nm_ops),			\
}

#define CRU_PLL_C_DEFINE(_name, _parent, _reg)				\
static struct cru_pll _name = {						\
	.reg = (void __iomem *)(_reg),					\
	CRU_INITHW(_name, _parent, dr1_cru_pll_c_ops),			\
}

#define CRU_DIV_DEFINE(_name, _parent, _reg, _shift, _width, _table,	\
		       _flags)						\
static struct clk_divider _name = {					\
	.shift = _shift,						\
	.width = _width,						\
	.flags = _flags,						\
	.table = _table,						\
	.reg = (void __iomem *)(_reg),					\
	CRU_INITHW(_name, _parent, clk_divider_ops),			\
}

#define CRU_DIV_GATE_DEFINE(_name, _parent, _reg, _shift, _width,	\
			    _table, _flags, _min)			\
static struct cru_div_gate _name = {					\
	.min = _min,							\
	.divider = {							\
		.shift = _shift,					\
		.width = _width,					\
		.flags = _flags,					\
		.table = _table,					\
		.reg = (void __iomem *)(_reg),				\
		CRU_INITHW(_name, _parent, dr1_cru_div_gate_ops),	\
	}								\
}

#define CRU_MUX_DEFINE(_name, _parents, _reg, _shift, _width)		\
static struct clk_mux _name = {						\
	.shift = _shift,						\
	.mask = GENMASK(_width - 1, 0),					\
	.reg = (void __iomem *)(_reg),					\
	CRU_INITHW_PARENTS(_name, _parents, clk_mux_ops)		\
}

#define CRU_GATE_DEFINE(_name, _parent, _reg, _bit_idx, _flags)		\
static struct clk_gate _name = {					\
	.bit_idx = _bit_idx,						\
	.flags = _flags,						\
	.reg = (void __iomem *)(_reg),					\
	CRU_INITHW(_name, _parent, clk_gate_ops)			\
}

static inline struct cru_pll *hw_to_cru_pll(struct clk_hw *hw)
{
	return container_of(hw, struct cru_pll, hw);
}

static inline struct cru_div_gate *hw_to_cru_div_gate(struct clk_hw *hw)
{
	struct clk_divider *divider = to_clk_divider(hw);

	return container_of(divider, struct cru_div_gate, divider);
}

extern const struct clk_ops dr1_cru_pll_nm_ops;
extern const struct clk_ops dr1_cru_pll_c_ops;
extern const struct clk_ops dr1_cru_div_gate_ops;

int dr1_cru_clk_register(struct device *dev, void __iomem *base,
			 const struct cru_clk *clks, int nr_clks);
int dr1_cru_reset_register(struct device *dev, void __iomem *base);

#endif /* _CRU_DR1_H_ */
