/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 */

#ifndef __DRV_CLK_MTK_H
#define __DRV_CLK_MTK_H

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "reset.h"

#define MAX_MUX_GATE_BIT	31
#define INVALID_MUX_GATE_BIT	(MAX_MUX_GATE_BIT + 1)

#define MHZ (1000 * 1000)

struct platform_device;

/*
 * We need the clock IDs to start from zero but to maintain devicetree
 * backwards compatibility we can't change bindings to start from zero.
 * Only a few platforms are affected, so we solve issues given by the
 * commonized MTK clocks probe function(s) by adding a dummy clock at
 * the beginning where needed.
 */
#define CLK_DUMMY		0

extern const struct clk_ops mtk_clk_dummy_ops;
extern const struct mtk_gate_regs cg_regs_dummy;

#define GATE_DUMMY(_id, _name) {				\
		.id = _id,					\
		.name = _name,					\
		.regs = &cg_regs_dummy,				\
		.ops = &mtk_clk_dummy_ops,			\
	}

struct mtk_fixed_clk {
	int id;
	const char *name;
	const char *parent;
	unsigned long rate;
};

#define FIXED_CLK(_id, _name, _parent, _rate) {		\
		.id = _id,				\
		.name = _name,				\
		.parent = _parent,			\
		.rate = _rate,				\
	}

int mtk_clk_register_fixed_clks(const struct mtk_fixed_clk *clks, int num,
				struct clk_hw_onecell_data *clk_data);
void mtk_clk_unregister_fixed_clks(const struct mtk_fixed_clk *clks, int num,
				   struct clk_hw_onecell_data *clk_data);

struct mtk_fixed_factor {
	int id;
	const char *name;
	const char *parent_name;
	int mult;
	int div;
	unsigned long flags;
};

#define FACTOR_FLAGS(_id, _name, _parent, _mult, _div, _fl) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.mult = _mult,				\
		.div = _div,				\
		.flags = _fl,				\
	}

#define FACTOR(_id, _name, _parent, _mult, _div)	\
	FACTOR_FLAGS(_id, _name, _parent, _mult, _div, CLK_SET_RATE_PARENT)

int mtk_clk_register_factors(const struct mtk_fixed_factor *clks, int num,
			     struct clk_hw_onecell_data *clk_data);
void mtk_clk_unregister_factors(const struct mtk_fixed_factor *clks, int num,
				struct clk_hw_onecell_data *clk_data);

struct mtk_composite {
	int id;
	const char *name;
	const char * const *parent_names;
	const char *parent;
	unsigned flags;

	uint32_t mux_reg;
	uint32_t divider_reg;
	uint32_t gate_reg;

	signed char mux_shift;
	signed char mux_width;
	signed char gate_shift;

	signed char divider_shift;
	signed char divider_width;

	u8 mux_flags;

	signed char num_parents;
};

#define MUX_GATE_FLAGS_2(_id, _name, _parents, _reg, _shift,		\
				_width, _gate, _flags, _muxflags) {	\
		.id = _id,						\
		.name = _name,						\
		.mux_reg = _reg,					\
		.mux_shift = _shift,					\
		.mux_width = _width,					\
		.gate_reg = _reg,					\
		.gate_shift = _gate,					\
		.divider_shift = -1,					\
		.parent_names = _parents,				\
		.num_parents = ARRAY_SIZE(_parents),			\
		.flags = _flags,					\
		.mux_flags = _muxflags,					\
	}

/*
 * In case the rate change propagation to parent clocks is undesirable,
 * this macro allows to specify the clock flags manually.
 */
#define MUX_GATE_FLAGS(_id, _name, _parents, _reg, _shift, _width,	\
			_gate, _flags)					\
		MUX_GATE_FLAGS_2(_id, _name, _parents, _reg,		\
					_shift, _width, _gate, _flags, 0)

/*
 * Unless necessary, all MUX_GATE clocks propagate rate changes to their
 * parent clock by default.
 */
#define MUX_GATE(_id, _name, _parents, _reg, _shift, _width, _gate)	\
	MUX_GATE_FLAGS(_id, _name, _parents, _reg, _shift, _width,	\
		_gate, CLK_SET_RATE_PARENT)

#define MUX(_id, _name, _parents, _reg, _shift, _width)			\
	MUX_FLAGS(_id, _name, _parents, _reg,				\
		  _shift, _width, CLK_SET_RATE_PARENT)

#define MUX_FLAGS(_id, _name, _parents, _reg, _shift, _width, _flags) {	\
		.id = _id,						\
		.name = _name,						\
		.mux_reg = _reg,					\
		.mux_shift = _shift,					\
		.mux_width = _width,					\
		.gate_shift = -1,					\
		.divider_shift = -1,					\
		.parent_names = _parents,				\
		.num_parents = ARRAY_SIZE(_parents),			\
		.flags = _flags,				\
	}

#define DIV_GATE(_id, _name, _parent, _gate_reg, _gate_shift, _div_reg,	\
					_div_width, _div_shift) {	\
		.id = _id,						\
		.parent = _parent,					\
		.name = _name,						\
		.divider_reg = _div_reg,				\
		.divider_shift = _div_shift,				\
		.divider_width = _div_width,				\
		.gate_reg = _gate_reg,					\
		.gate_shift = _gate_shift,				\
		.mux_shift = -1,					\
		.flags = 0,						\
	}

int mtk_clk_register_composites(struct device *dev,
				const struct mtk_composite *mcs, int num,
				void __iomem *base, spinlock_t *lock,
				struct clk_hw_onecell_data *clk_data);
void mtk_clk_unregister_composites(const struct mtk_composite *mcs, int num,
				   struct clk_hw_onecell_data *clk_data);

struct mtk_clk_divider {
	int id;
	const char *name;
	const char *parent_name;
	unsigned long flags;

	u32 div_reg;
	unsigned char div_shift;
	unsigned char div_width;
	unsigned char clk_divider_flags;
	const struct clk_div_table *clk_div_table;
};

#define DIV_ADJ(_id, _name, _parent, _reg, _shift, _width) {	\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.div_reg = _reg,				\
		.div_shift = _shift,				\
		.div_width = _width,				\
}

int mtk_clk_register_dividers(struct device *dev,
			      const struct mtk_clk_divider *mcds, int num,
			      void __iomem *base, spinlock_t *lock,
			      struct clk_hw_onecell_data *clk_data);
void mtk_clk_unregister_dividers(const struct mtk_clk_divider *mcds, int num,
				 struct clk_hw_onecell_data *clk_data);

struct clk_hw_onecell_data *mtk_alloc_clk_data(unsigned int clk_num);
struct clk_hw_onecell_data *mtk_devm_alloc_clk_data(struct device *dev,
						    unsigned int clk_num);
void mtk_free_clk_data(struct clk_hw_onecell_data *clk_data);

struct clk_hw *mtk_clk_register_ref2usb_tx(const char *name,
			const char *parent_name, void __iomem *reg);
void mtk_clk_unregister_ref2usb_tx(struct clk_hw *hw);

struct mtk_clk_desc {
	const struct mtk_gate *clks;
	size_t num_clks;
	const struct mtk_composite *composite_clks;
	size_t num_composite_clks;
	const struct mtk_clk_divider *divider_clks;
	size_t num_divider_clks;
	const struct mtk_fixed_clk *fixed_clks;
	size_t num_fixed_clks;
	const struct mtk_fixed_factor *factor_clks;
	size_t num_factor_clks;
	const struct mtk_mux *mux_clks;
	size_t num_mux_clks;
	const struct mtk_clk_rst_desc *rst_desc;
	spinlock_t *clk_lock;
	bool shared_io;

	int (*clk_notifier_func)(struct device *dev, struct clk *clk);
	unsigned int mfg_clk_idx;
};

int mtk_clk_pdev_probe(struct platform_device *pdev);
void mtk_clk_pdev_remove(struct platform_device *pdev);
int mtk_clk_simple_probe(struct platform_device *pdev);
void mtk_clk_simple_remove(struct platform_device *pdev);

#endif /* __DRV_CLK_MTK_H */
