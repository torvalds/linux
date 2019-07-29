/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 */

#ifndef __CLKC_H
#define __CLKC_H

#include <linux/clk-provider.h>
#include "clk-regmap.h"

#define PMASK(width)			GENMASK(width - 1, 0)
#define SETPMASK(width, shift)		GENMASK(shift + width - 1, shift)
#define CLRPMASK(width, shift)		(~SETPMASK(width, shift))

#define PARM_GET(width, shift, reg)					\
	(((reg) & SETPMASK(width, shift)) >> (shift))
#define PARM_SET(width, shift, reg, val)				\
	(((reg) & CLRPMASK(width, shift)) | ((val) << (shift)))

#define MESON_PARM_APPLICABLE(p)		(!!((p)->width))

struct parm {
	u16	reg_off;
	u8	shift;
	u8	width;
};

static inline unsigned int meson_parm_read(struct regmap *map, struct parm *p)
{
	unsigned int val;

	regmap_read(map, p->reg_off, &val);
	return PARM_GET(p->width, p->shift, val);
}

static inline void meson_parm_write(struct regmap *map, struct parm *p,
				    unsigned int val)
{
	regmap_update_bits(map, p->reg_off, SETPMASK(p->width, p->shift),
			   val << p->shift);
}


struct pll_params_table {
	u16		m;
	u16		n;
};

#define PLL_PARAMS(_m, _n)						\
	{								\
		.m		= (_m),					\
		.n		= (_n),					\
	}

#define CLK_MESON_PLL_ROUND_CLOSEST	BIT(0)

struct meson_clk_pll_data {
	struct parm en;
	struct parm m;
	struct parm n;
	struct parm frac;
	struct parm l;
	struct parm rst;
	const struct reg_sequence *init_regs;
	unsigned int init_count;
	const struct pll_params_table *table;
	u8 flags;
};

#define to_meson_clk_pll(_hw) container_of(_hw, struct meson_clk_pll, hw)

struct meson_clk_mpll_data {
	struct parm sdm;
	struct parm sdm_en;
	struct parm n2;
	struct parm ssen;
	struct parm misc;
	spinlock_t *lock;
	u8 flags;
};

#define CLK_MESON_MPLL_ROUND_CLOSEST	BIT(0)

struct meson_clk_phase_data {
	struct parm ph;
};

int meson_clk_degrees_from_val(unsigned int val, unsigned int width);
unsigned int meson_clk_degrees_to_val(int degrees, unsigned int width);

struct meson_vid_pll_div_data {
	struct parm val;
	struct parm sel;
};

#define MESON_GATE(_name, _reg, _bit)					\
struct clk_regmap _name = {						\
	.data = &(struct clk_regmap_gate_data){				\
		.offset = (_reg),					\
		.bit_idx = (_bit),					\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = #_name,						\
		.ops = &clk_regmap_gate_ops,				\
		.parent_names = (const char *[]){ "clk81" },		\
		.num_parents = 1,					\
		.flags = (CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED),	\
	},								\
};

/* clk_ops */
extern const struct clk_ops meson_clk_pll_ro_ops;
extern const struct clk_ops meson_clk_pll_ops;
extern const struct clk_ops meson_clk_cpu_ops;
extern const struct clk_ops meson_clk_mpll_ro_ops;
extern const struct clk_ops meson_clk_mpll_ops;
extern const struct clk_ops meson_clk_phase_ops;
extern const struct clk_ops meson_vid_pll_div_ro_ops;

struct clk_hw *meson_clk_hw_register_input(struct device *dev,
					   const char *of_name,
					   const char *clk_name,
					   unsigned long flags);

#endif /* __CLKC_H */
