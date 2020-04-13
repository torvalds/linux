/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * drmem.h: Power specific logical memory block representation
 *
 * Copyright 2017 IBM Corporation
 */

#ifndef _ASM_POWERPC_LMB_H
#define _ASM_POWERPC_LMB_H

struct drmem_lmb {
	u64     base_addr;
	u32     drc_index;
	u32     aa_index;
	u32     flags;
#ifdef CONFIG_MEMORY_HOTPLUG
	int	nid;
#endif
};

struct drmem_lmb_info {
	struct drmem_lmb        *lmbs;
	int                     n_lmbs;
	u32                     lmb_size;
};

extern struct drmem_lmb_info *drmem_info;

#define for_each_drmem_lmb_in_range(lmb, start, end)		\
	for ((lmb) = (start); (lmb) < (end); (lmb)++)

#define for_each_drmem_lmb(lmb)					\
	for_each_drmem_lmb_in_range((lmb),			\
		&drmem_info->lmbs[0],				\
		&drmem_info->lmbs[drmem_info->n_lmbs])

/*
 * The of_drconf_cell_v1 struct defines the layout of the LMB data
 * specified in the ibm,dynamic-memory device tree property.
 * The property itself is a 32-bit value specifying the number of
 * LMBs followed by an array of of_drconf_cell_v1 entries, one
 * per LMB.
 */
struct of_drconf_cell_v1 {
	__be64	base_addr;
	__be32	drc_index;
	__be32	reserved;
	__be32	aa_index;
	__be32	flags;
};

/*
 * Version 2 of the ibm,dynamic-memory property is defined as a
 * 32-bit value specifying the number of LMB sets followed by an
 * array of of_drconf_cell_v2 entries, one per LMB set.
 */
struct of_drconf_cell_v2 {
	u32	seq_lmbs;
	u64	base_addr;
	u32	drc_index;
	u32	aa_index;
	u32	flags;
} __packed;

#define DRCONF_MEM_ASSIGNED	0x00000008
#define DRCONF_MEM_AI_INVALID	0x00000040
#define DRCONF_MEM_RESERVED	0x00000080

static inline u32 drmem_lmb_size(void)
{
	return drmem_info->lmb_size;
}

#define DRMEM_LMB_RESERVED	0x80000000

static inline void drmem_mark_lmb_reserved(struct drmem_lmb *lmb)
{
	lmb->flags |= DRMEM_LMB_RESERVED;
}

static inline void drmem_remove_lmb_reservation(struct drmem_lmb *lmb)
{
	lmb->flags &= ~DRMEM_LMB_RESERVED;
}

static inline bool drmem_lmb_reserved(struct drmem_lmb *lmb)
{
	return lmb->flags & DRMEM_LMB_RESERVED;
}

u64 drmem_lmb_memory_max(void);
void __init walk_drmem_lmbs(struct device_node *dn,
			void (*func)(struct drmem_lmb *, const __be32 **));
int drmem_update_dt(void);

#ifdef CONFIG_PPC_PSERIES
void __init walk_drmem_lmbs_early(unsigned long node,
			void (*func)(struct drmem_lmb *, const __be32 **));
#endif

static inline void invalidate_lmb_associativity_index(struct drmem_lmb *lmb)
{
	lmb->aa_index = 0xffffffff;
}

#ifdef CONFIG_MEMORY_HOTPLUG
static inline void lmb_set_nid(struct drmem_lmb *lmb)
{
	lmb->nid = memory_add_physaddr_to_nid(lmb->base_addr);
}
static inline void lmb_clear_nid(struct drmem_lmb *lmb)
{
	lmb->nid = -1;
}
#else
static inline void lmb_set_nid(struct drmem_lmb *lmb)
{
}
static inline void lmb_clear_nid(struct drmem_lmb *lmb)
{
}
#endif

#endif /* _ASM_POWERPC_LMB_H */
