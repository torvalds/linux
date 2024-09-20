/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Neil Armstrong <neil.armstrong@linaro.org>
 */

#ifndef __VCLK_H
#define __VCLK_H

#include "clk-regmap.h"
#include "parm.h"

/**
 * struct meson_vclk_gate_data - vclk_gate regmap backed specific data
 *
 * @enable:	vclk enable field
 * @reset:	vclk reset field
 * @flags:	hardware-specific flags
 *
 * Flags:
 * Same as clk_gate except CLK_GATE_HIWORD_MASK which is ignored
 */
struct meson_vclk_gate_data {
	struct parm enable;
	struct parm reset;
	u8 flags;
};

extern const struct clk_ops meson_vclk_gate_ops;

/**
 * struct meson_vclk_div_data - vclk_div regmap back specific data
 *
 * @div:	divider field
 * @enable:	vclk divider enable field
 * @reset:	vclk divider reset field
 * @table:	array of value/divider pairs, last entry should have div = 0
 *
 * Flags:
 * Same as clk_divider except CLK_DIVIDER_HIWORD_MASK which is ignored
 */
struct meson_vclk_div_data {
	struct parm div;
	struct parm enable;
	struct parm reset;
	const struct clk_div_table *table;
	u8 flags;
};

extern const struct clk_ops meson_vclk_div_ops;

#endif /* __VCLK_H */
