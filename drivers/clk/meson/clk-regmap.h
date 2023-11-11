/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#ifndef __CLK_REGMAP_H
#define __CLK_REGMAP_H

#include <linux/clk-provider.h>
#include <linux/regmap.h>

/**
 * struct clk_regmap - regmap backed clock
 *
 * @hw:		handle between common and hardware-specific interfaces
 * @map:	pointer to the regmap structure controlling the clock
 * @data:	data specific to the clock type
 *
 * Clock which is controlled by regmap backed registers. The actual type of
 * of the clock is controlled by the clock_ops and data.
 */
struct clk_regmap {
	struct clk_hw	hw;
	struct regmap	*map;
	void		*data;
};

static inline struct clk_regmap *to_clk_regmap(struct clk_hw *hw)
{
	return container_of(hw, struct clk_regmap, hw);
}

/**
 * struct clk_regmap_gate_data - regmap backed gate specific data
 *
 * @offset:	offset of the register controlling gate
 * @bit_idx:	single bit controlling gate
 * @flags:	hardware-specific flags
 *
 * Flags:
 * Same as clk_gate except CLK_GATE_HIWORD_MASK which is ignored
 */
struct clk_regmap_gate_data {
	unsigned int	offset;
	u8		bit_idx;
	u8		flags;
};

static inline struct clk_regmap_gate_data *
clk_get_regmap_gate_data(struct clk_regmap *clk)
{
	return (struct clk_regmap_gate_data *)clk->data;
}

extern const struct clk_ops clk_regmap_gate_ops;
extern const struct clk_ops clk_regmap_gate_ro_ops;

/**
 * struct clk_regmap_div_data - regmap backed adjustable divider specific data
 *
 * @offset:	offset of the register controlling the divider
 * @shift:	shift to the divider bit field
 * @width:	width of the divider bit field
 * @table:	array of value/divider pairs, last entry should have div = 0
 *
 * Flags:
 * Same as clk_divider except CLK_DIVIDER_HIWORD_MASK which is ignored
 */
struct clk_regmap_div_data {
	unsigned int	offset;
	u8		shift;
	u8		width;
	u8		flags;
	const struct clk_div_table	*table;
};

static inline struct clk_regmap_div_data *
clk_get_regmap_div_data(struct clk_regmap *clk)
{
	return (struct clk_regmap_div_data *)clk->data;
}

extern const struct clk_ops clk_regmap_divider_ops;
extern const struct clk_ops clk_regmap_divider_ro_ops;

/**
 * struct clk_regmap_mux_data - regmap backed multiplexer clock specific data
 *
 * @hw:		handle between common and hardware-specific interfaces
 * @offset:	offset of theregister controlling multiplexer
 * @table:	array of parent indexed register values
 * @shift:	shift to multiplexer bit field
 * @mask:	mask of mutliplexer bit field
 * @flags:	hardware-specific flags
 *
 * Flags:
 * Same as clk_divider except CLK_MUX_HIWORD_MASK which is ignored
 */
struct clk_regmap_mux_data {
	unsigned int	offset;
	u32		*table;
	u32		mask;
	u8		shift;
	u8		flags;
};

static inline struct clk_regmap_mux_data *
clk_get_regmap_mux_data(struct clk_regmap *clk)
{
	return (struct clk_regmap_mux_data *)clk->data;
}

extern const struct clk_ops clk_regmap_mux_ops;
extern const struct clk_ops clk_regmap_mux_ro_ops;

#define __MESON_PCLK(_name, _reg, _bit, _ops, _pname)			\
struct clk_regmap _name = {						\
	.data = &(struct clk_regmap_gate_data){				\
		.offset = (_reg),					\
		.bit_idx = (_bit),					\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = #_name,						\
		.ops = _ops,						\
		.parent_hws = (const struct clk_hw *[]) { _pname },	\
		.num_parents = 1,					\
		.flags = (CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED),	\
	},								\
}

#define MESON_PCLK(_name, _reg, _bit, _pname)	\
	__MESON_PCLK(_name, _reg, _bit, &clk_regmap_gate_ops, _pname)

#define MESON_PCLK_RO(_name, _reg, _bit, _pname)	\
	__MESON_PCLK(_name, _reg, _bit, &clk_regmap_gate_ro_ops, _pname)
#endif /* __CLK_REGMAP_H */
