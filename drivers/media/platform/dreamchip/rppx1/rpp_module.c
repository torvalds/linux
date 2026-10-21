// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rppx1.h"
#include "rpp_module.h"

int rpp_module_probe(struct rpp_module *mod, struct rppx1 *rpp,
		     const struct rpp_module_ops *ops, u32 base)
{
	mod->rpp = rpp;
	mod->base = base;
	mod->ops = ops;

	if (ops->probe)
		return ops->probe(mod);

	return 0;
}

void rpp_module_write(struct rpp_module *mod, u32 offset, u32 value)
{
	rppx1_write(mod->rpp, mod->base + offset, value);
}

u32 rpp_module_read(struct rpp_module *mod, u32 offset)
{
	return rppx1_read(mod->rpp, mod->base + offset);
}

void rpp_module_clrset(struct rpp_module *mod, u32 offset, u32 mask, u32 value)
{
	u32 reg = rpp_module_read(mod, offset) & ~mask;

	rpp_module_write(mod, offset, reg | value);
}
