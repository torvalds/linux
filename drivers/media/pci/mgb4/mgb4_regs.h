/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021-2022 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 */

#ifndef __MGB4_REGS_H__
#define __MGB4_REGS_H__

#include <linux/io.h>

struct mgb4_regs {
	resource_size_t mapbase;
	resource_size_t mapsize;
	void __iomem *membase;
};

#define mgb4_write_reg(regs, offset, val) \
	iowrite32(val, (regs)->membase + (offset))
#define  mgb4_read_reg(regs, offset) \
	ioread32((regs)->membase + (offset))

static inline void mgb4_mask_reg(struct mgb4_regs *regs, u32 reg, u32 mask,
				 u32 val)
{
	u32 ret = mgb4_read_reg(regs, reg);

	val |= ret & ~mask;
	mgb4_write_reg(regs, reg, val);
}

int mgb4_regs_map(struct resource *res, struct mgb4_regs *regs);
void mgb4_regs_free(struct mgb4_regs *regs);

#endif
