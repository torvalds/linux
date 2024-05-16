// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2022 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 */

#include <linux/ioport.h>
#include "mgb4_regs.h"

int mgb4_regs_map(struct resource *res, struct mgb4_regs *regs)
{
	regs->mapbase = res->start;
	regs->mapsize = res->end - res->start;

	if (!request_mem_region(regs->mapbase, regs->mapsize, res->name))
		return -EINVAL;
	regs->membase = ioremap(regs->mapbase, regs->mapsize);
	if (!regs->membase) {
		release_mem_region(regs->mapbase, regs->mapsize);
		return -EINVAL;
	}

	return 0;
}

void mgb4_regs_free(struct mgb4_regs *regs)
{
	iounmap(regs->membase);
	release_mem_region(regs->mapbase, regs->mapsize);
}
