/*
 * drmem.h: Power specific logical memory block representation
 *
 * Copyright 2017 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_POWERPC_LMB_H
#define _ASM_POWERPC_LMB_H

struct drmem_lmb {
	u64     base_addr;
	u32     drc_index;
	u32     aa_index;
	u32     flags;
};

struct drmem_lmb_info {
	struct drmem_lmb        *lmbs;
	int                     n_lmbs;
	u32                     lmb_size;
};

extern struct drmem_lmb_info *drmem_info;

#define for_each_drmem_lmb_in_range(lmb, start, end)		\
	for ((lmb) = (start); (lmb) <= (end); (lmb)++)

#define for_each_drmem_lmb(lmb)					\
	for_each_drmem_lmb_in_range((lmb),			\
		&drmem_info->lmbs[0],				\
		&drmem_info->lmbs[drmem_info->n_lmbs - 1])

static inline u32 drmem_lmb_size(void)
{
	return drmem_info->lmb_size;
}

u64 drmem_lmb_memory_max(void);
void __init walk_drmem_lmbs(struct device_node *dn,
			void (*func)(struct drmem_lmb *, const __be32 **));

#ifdef CONFIG_PPC_PSERIES
void __init walk_drmem_lmbs_early(unsigned long node,
			void (*func)(struct drmem_lmb *, const __be32 **));
#endif

#endif /* _ASM_POWERPC_LMB_H */
