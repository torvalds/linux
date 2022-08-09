/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 */

#ifndef __DRV_CLK_GATE_H
#define __DRV_CLK_GATE_H

#include <linux/types.h>

struct clk;
struct clk_onecell_data;
struct clk_ops;
struct device;
struct device_node;

extern const struct clk_ops mtk_clk_gate_ops_setclr;
extern const struct clk_ops mtk_clk_gate_ops_setclr_inv;
extern const struct clk_ops mtk_clk_gate_ops_no_setclr;
extern const struct clk_ops mtk_clk_gate_ops_no_setclr_inv;

struct mtk_gate_regs {
	u32 sta_ofs;
	u32 clr_ofs;
	u32 set_ofs;
};

struct mtk_gate {
	int id;
	const char *name;
	const char *parent_name;
	const struct mtk_gate_regs *regs;
	int shift;
	const struct clk_ops *ops;
	unsigned long flags;
};

#define GATE_MTK_FLAGS(_id, _name, _parent, _regs, _shift,	\
			_ops, _flags) {				\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.regs = _regs,					\
		.shift = _shift,				\
		.ops = _ops,					\
		.flags = _flags,				\
	}

#define GATE_MTK(_id, _name, _parent, _regs, _shift, _ops)		\
	GATE_MTK_FLAGS(_id, _name, _parent, _regs, _shift, _ops, 0)

int mtk_clk_register_gates(struct device_node *node,
			   const struct mtk_gate *clks, int num,
			   struct clk_onecell_data *clk_data);

int mtk_clk_register_gates_with_dev(struct device_node *node,
				    const struct mtk_gate *clks, int num,
				    struct clk_onecell_data *clk_data,
				    struct device *dev);

void mtk_clk_unregister_gates(const struct mtk_gate *clks, int num,
			      struct clk_onecell_data *clk_data);

#endif /* __DRV_CLK_GATE_H */
