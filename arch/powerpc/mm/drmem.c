/*
 * Dynamic reconfiguration memory support
 *
 * Copyright 2017 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) "drmem: " fmt

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/memblock.h>
#include <asm/prom.h>
#include <asm/drmem.h>

static struct drmem_lmb_info __drmem_info;
struct drmem_lmb_info *drmem_info = &__drmem_info;

#ifdef CONFIG_PPC_PSERIES
static void __init read_drconf_v1_cell(struct drmem_lmb *lmb,
				       const __be32 **prop)
{
	const __be32 *p = *prop;

	lmb->base_addr = dt_mem_next_cell(dt_root_addr_cells, &p);
	lmb->drc_index = of_read_number(p++, 1);

	p++; /* skip reserved field */

	lmb->aa_index = of_read_number(p++, 1);
	lmb->flags = of_read_number(p++, 1);

	*prop = p;
}

static void __init __walk_drmem_v1_lmbs(const __be32 *prop, const __be32 *usm,
			void (*func)(struct drmem_lmb *, const __be32 **))
{
	struct drmem_lmb lmb;
	u32 i, n_lmbs;

	n_lmbs = of_read_number(prop++, 1);

	for (i = 0; i < n_lmbs; i++) {
		read_drconf_v1_cell(&lmb, &prop);
		func(&lmb, &usm);
	}
}

void __init walk_drmem_lmbs_early(unsigned long node,
			void (*func)(struct drmem_lmb *, const __be32 **))
{
	const __be32 *prop, *usm;
	int len;

	prop = of_get_flat_dt_prop(node, "ibm,lmb-size", &len);
	if (!prop || len < dt_root_size_cells * sizeof(__be32))
		return;

	drmem_info->lmb_size = dt_mem_next_cell(dt_root_size_cells, &prop);

	usm = of_get_flat_dt_prop(node, "linux,drconf-usable-memory", &len);

	prop = of_get_flat_dt_prop(node, "ibm,dynamic-memory", &len);
	if (prop)
		__walk_drmem_v1_lmbs(prop, usm, func);

	memblock_dump_all();
}

#endif
