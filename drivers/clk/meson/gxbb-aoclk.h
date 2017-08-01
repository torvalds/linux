/*
 * Copyright (c) 2017 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef __GXBB_AOCLKC_H
#define __GXBB_AOCLKC_H

/* AO Configuration Clock registers offsets */
#define AO_RTI_GEN_CNTL_REG0	0x40

struct aoclk_gate_regmap {
	struct clk_hw hw;
	unsigned bit_idx;
	struct regmap *regmap;
	spinlock_t *lock;
};

#define to_aoclk_gate_regmap(_hw) \
	container_of(_hw, struct aoclk_gate_regmap, hw)

extern const struct clk_ops meson_aoclk_gate_regmap_ops;

#endif /* __GXBB_AOCLKC_H */
