/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Stefan Dösinger
 */

#ifndef __DRV_CLK_ZX_H
#define __DRV_CLK_ZX_H

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define CLK_ZX_PLL_PREPARE_IS_ENABLE	1
#define CLK_ZX_MAX_PARENTS		8

enum zx_parent_type {
	ZX_PARENT_FW,
	ZX_PARENT_ID,
};

struct zx_parent_desc {
	enum zx_parent_type		type;
	union {
		const char		*fw_name;
		unsigned int		id;
	};
};

#define PARENT_FW(_name) { .type = ZX_PARENT_FW, .fw_name = (_name) }

#define PARENT_ID(_id) { .type = ZX_PARENT_ID, .id = (_id) }

static inline struct clk_parent_data zx_get_parent(const struct zx_parent_desc *p,
						   struct clk_hw * const *clocks)
{
	struct clk_parent_data ret = {};

	switch (p->type) {
	case ZX_PARENT_FW:
		ret.fw_name = p->fw_name;
		ret.index = -1;
		return ret;

	case ZX_PARENT_ID:
		ret.hw = clocks[p->id];
		ret.index = -1;
		return ret;
	}

	WARN_ON_ONCE(1);
	return ret;
}

struct zx_pll_desc {
	const char			*name;
	const struct zx_parent_desc	*parents;
	unsigned int			num_parents;
	unsigned long			rate;
	u16				reg;
	u16				flags;
};

struct zx_fixed_divider_desc {
	const char			*name;
	struct zx_parent_desc		parent;
	unsigned int			div;
};

struct zx_mux_desc {
	const char			*name;
	const struct zx_parent_desc	*parents;
	unsigned int			num_parents;
	u16				reg;
	u8				shift, size;
};

struct zx_div_desc {
	const char			*name;
	struct zx_parent_desc		parent;
	u16				reg;
	u8				shift, size;
};

struct zx_gate_desc {
	const char			*name;
	struct zx_parent_desc		parent;
	unsigned long			flags;
	u16				reg;
	u8				shift;
};

enum zx_clock_type {
	ZX_CLOCK_INVALID = 0,
	ZX_CLOCK_PLL,
	ZX_CLOCK_FIXED_DIV,
	ZX_CLOCK_MUX,
	ZX_CLOCK_DIV,
	ZX_CLOCK_GATE,
};

struct zx_clock {
	enum zx_clock_type type;
	union {
		struct zx_pll_desc pll;
		struct zx_fixed_divider_desc fixed_div;
		struct zx_mux_desc mux;
		struct zx_div_desc div;
		struct zx_gate_desc gate;
	};
};

struct clk_hw *zx_clk_register_pll(struct device *dev, struct regmap *regmap,
				   const struct zx_pll_desc *desc,
				   struct clk_hw * const *clocks);
struct clk_hw *zx_clk_register_mux(struct device *dev, struct regmap *regmap,
				   const struct zx_mux_desc *desc,
				   struct clk_hw * const *clocks);
struct clk_hw *zx_clk_register_divider(struct device *dev, struct regmap *regmap,
				       const struct zx_div_desc *desc,
				       struct clk_hw * const *clocks);
struct clk_hw *zx_clk_register_gate(struct device *dev, struct regmap *regmap,
				    const struct zx_gate_desc *desc,
				    struct clk_hw * const *clocks);

struct zx_clk_data {
	int (*init)(struct regmap *map);
	const struct zx_clock *clocks;
	unsigned int num_clocks;
	const unsigned int *exports;
	unsigned int num_exports;
};

int zx_clk_common_probe(struct device *dev, struct device_node *of_node,
			const struct zx_clk_data *data);

#endif /* __DRV_CLK_ZX_H */
