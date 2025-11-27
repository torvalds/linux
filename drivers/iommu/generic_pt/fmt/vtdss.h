/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES
 *
 * Intel VT-d Second Stange 5/4 level page table
 *
 * This is described in
 *   Section "3.7 Second-Stage Translation"
 *   Section "9.8 Second-Stage Paging Entries"
 *
 * Of the "Intel Virtualization Technology for Directed I/O Architecture
 * Specification".
 *
 * The named levels in the spec map to the pts->level as:
 *   Table/SS-PTE - 0
 *   Directory/SS-PDE - 1
 *   Directory Ptr/SS-PDPTE - 2
 *   PML4/SS-PML4E - 3
 *   PML5/SS-PML5E - 4
 */
#ifndef __GENERIC_PT_FMT_VTDSS_H
#define __GENERIC_PT_FMT_VTDSS_H

#include "defs_vtdss.h"
#include "../pt_defs.h"

#include <linux/bitfield.h>
#include <linux/container_of.h>
#include <linux/log2.h>

enum {
	PT_MAX_OUTPUT_ADDRESS_LG2 = 52,
	PT_MAX_VA_ADDRESS_LG2 = 57,
	PT_ITEM_WORD_SIZE = sizeof(u64),
	PT_MAX_TOP_LEVEL = 4,
	PT_GRANULE_LG2SZ = 12,
	PT_TABLEMEM_LG2SZ = 12,

	/* SSPTPTR is 4k aligned and limited by HAW */
	PT_TOP_PHYS_MASK = GENMASK_ULL(63, 12),
};

/* Shared descriptor bits */
enum {
	VTDSS_FMT_R = BIT(0),
	VTDSS_FMT_W = BIT(1),
	VTDSS_FMT_A = BIT(8),
	VTDSS_FMT_D = BIT(9),
	VTDSS_FMT_SNP = BIT(11),
	VTDSS_FMT_OA = GENMASK_ULL(51, 12),
};

/* PDPTE/PDE */
enum {
	VTDSS_FMT_PS = BIT(7),
};

#define common_to_vtdss_pt(common_ptr) \
	container_of_const(common_ptr, struct pt_vtdss, common)
#define to_vtdss_pt(pts) common_to_vtdss_pt((pts)->range->common)

static inline pt_oaddr_t vtdss_pt_table_pa(const struct pt_state *pts)
{
	return oalog2_mul(FIELD_GET(VTDSS_FMT_OA, pts->entry),
			  PT_TABLEMEM_LG2SZ);
}
#define pt_table_pa vtdss_pt_table_pa

static inline pt_oaddr_t vtdss_pt_entry_oa(const struct pt_state *pts)
{
	return oalog2_mul(FIELD_GET(VTDSS_FMT_OA, pts->entry),
			  PT_GRANULE_LG2SZ);
}
#define pt_entry_oa vtdss_pt_entry_oa

static inline bool vtdss_pt_can_have_leaf(const struct pt_state *pts)
{
	return pts->level <= 2;
}
#define pt_can_have_leaf vtdss_pt_can_have_leaf

static inline unsigned int vtdss_pt_num_items_lg2(const struct pt_state *pts)
{
	return PT_TABLEMEM_LG2SZ - ilog2(sizeof(u64));
}
#define pt_num_items_lg2 vtdss_pt_num_items_lg2

static inline enum pt_entry_type vtdss_pt_load_entry_raw(struct pt_state *pts)
{
	const u64 *tablep = pt_cur_table(pts, u64);
	u64 entry;

	pts->entry = entry = READ_ONCE(tablep[pts->index]);
	if (!entry)
		return PT_ENTRY_EMPTY;
	if (pts->level == 0 ||
	    (vtdss_pt_can_have_leaf(pts) && (pts->entry & VTDSS_FMT_PS)))
		return PT_ENTRY_OA;
	return PT_ENTRY_TABLE;
}
#define pt_load_entry_raw vtdss_pt_load_entry_raw

static inline void
vtdss_pt_install_leaf_entry(struct pt_state *pts, pt_oaddr_t oa,
			    unsigned int oasz_lg2,
			    const struct pt_write_attrs *attrs)
{
	u64 *tablep = pt_cur_table(pts, u64);
	u64 entry;

	if (!pt_check_install_leaf_args(pts, oa, oasz_lg2))
		return;

	entry = FIELD_PREP(VTDSS_FMT_OA, log2_div(oa, PT_GRANULE_LG2SZ)) |
		attrs->descriptor_bits;
	if (pts->level != 0)
		entry |= VTDSS_FMT_PS;

	WRITE_ONCE(tablep[pts->index], entry);
	pts->entry = entry;
}
#define pt_install_leaf_entry vtdss_pt_install_leaf_entry

static inline bool vtdss_pt_install_table(struct pt_state *pts,
					  pt_oaddr_t table_pa,
					  const struct pt_write_attrs *attrs)
{
	u64 entry;

	entry = VTDSS_FMT_R | VTDSS_FMT_W |
		FIELD_PREP(VTDSS_FMT_OA, log2_div(table_pa, PT_GRANULE_LG2SZ));
	return pt_table_install64(pts, entry);
}
#define pt_install_table vtdss_pt_install_table

static inline void vtdss_pt_attr_from_entry(const struct pt_state *pts,
					    struct pt_write_attrs *attrs)
{
	attrs->descriptor_bits = pts->entry &
				 (VTDSS_FMT_R | VTDSS_FMT_W | VTDSS_FMT_SNP);
}
#define pt_attr_from_entry vtdss_pt_attr_from_entry

static inline bool vtdss_pt_entry_is_write_dirty(const struct pt_state *pts)
{
	u64 *tablep = pt_cur_table(pts, u64) + pts->index;

	return READ_ONCE(*tablep) & VTDSS_FMT_D;
}
#define pt_entry_is_write_dirty vtdss_pt_entry_is_write_dirty

static inline void vtdss_pt_entry_make_write_clean(struct pt_state *pts)
{
	u64 *tablep = pt_cur_table(pts, u64) + pts->index;

	WRITE_ONCE(*tablep, READ_ONCE(*tablep) & ~(u64)VTDSS_FMT_D);
}
#define pt_entry_make_write_clean vtdss_pt_entry_make_write_clean

static inline bool vtdss_pt_entry_make_write_dirty(struct pt_state *pts)
{
	u64 *tablep = pt_cur_table(pts, u64) + pts->index;
	u64 new = pts->entry | VTDSS_FMT_D;

	return try_cmpxchg64(tablep, &pts->entry, new);
}
#define pt_entry_make_write_dirty vtdss_pt_entry_make_write_dirty

static inline unsigned int vtdss_pt_max_sw_bit(struct pt_common *common)
{
	return 10;
}
#define pt_max_sw_bit vtdss_pt_max_sw_bit

static inline u64 vtdss_pt_sw_bit(unsigned int bitnr)
{
	if (__builtin_constant_p(bitnr) && bitnr > 10)
		BUILD_BUG();

	/* Bits marked Ignored in the specification */
	switch (bitnr) {
	case 0:
		return BIT(10);
	case 1 ... 9:
		return BIT_ULL((bitnr - 1) + 52);
	case 10:
		return BIT_ULL(63);
	/* Some bits in 9-3 are available in some entries */
	default:
		PT_WARN_ON(true);
		return 0;
	}
}
#define pt_sw_bit vtdss_pt_sw_bit

/* --- iommu */
#include <linux/generic_pt/iommu.h>
#include <linux/iommu.h>

#define pt_iommu_table pt_iommu_vtdss

/* The common struct is in the per-format common struct */
static inline struct pt_common *common_from_iommu(struct pt_iommu *iommu_table)
{
	return &container_of(iommu_table, struct pt_iommu_table, iommu)
			->vtdss_pt.common;
}

static inline struct pt_iommu *iommu_from_common(struct pt_common *common)
{
	return &container_of(common, struct pt_iommu_table, vtdss_pt.common)
			->iommu;
}

static inline int vtdss_pt_iommu_set_prot(struct pt_common *common,
					  struct pt_write_attrs *attrs,
					  unsigned int iommu_prot)
{
	u64 pte = 0;

	/*
	 * VTDSS does not have a present bit, so we tell if any entry is present
	 * by checking for R or W.
	 */
	if (!(iommu_prot & (IOMMU_READ | IOMMU_WRITE)))
		return -EINVAL;

	if (iommu_prot & IOMMU_READ)
		pte |= VTDSS_FMT_R;
	if (iommu_prot & IOMMU_WRITE)
		pte |= VTDSS_FMT_W;
	if (pt_feature(common, PT_FEAT_VTDSS_FORCE_COHERENCE))
		pte |= VTDSS_FMT_SNP;

	if (pt_feature(common, PT_FEAT_VTDSS_FORCE_WRITEABLE) &&
	    !(iommu_prot & IOMMU_WRITE)) {
		pr_err_ratelimited(
			"Read-only mapping is disallowed on the domain which serves as the parent in a nested configuration, due to HW errata (ERRATA_772415_SPR17)\n");
		return -EINVAL;
	}

	attrs->descriptor_bits = pte;
	return 0;
}
#define pt_iommu_set_prot vtdss_pt_iommu_set_prot

static inline int vtdss_pt_iommu_fmt_init(struct pt_iommu_vtdss *iommu_table,
					  const struct pt_iommu_vtdss_cfg *cfg)
{
	struct pt_vtdss *table = &iommu_table->vtdss_pt;

	if (cfg->top_level > 4 || cfg->top_level < 2)
		return -EOPNOTSUPP;

	pt_top_set_level(&table->common, cfg->top_level);
	return 0;
}
#define pt_iommu_fmt_init vtdss_pt_iommu_fmt_init

static inline void
vtdss_pt_iommu_fmt_hw_info(struct pt_iommu_vtdss *table,
			   const struct pt_range *top_range,
			   struct pt_iommu_vtdss_hw_info *info)
{
	info->ssptptr = virt_to_phys(top_range->top_table);
	PT_WARN_ON(info->ssptptr & ~PT_TOP_PHYS_MASK);
	/*
	 * top_level = 2 = 3 level table aw=1
	 * top_level = 3 = 4 level table aw=2
	 * top_level = 4 = 5 level table aw=3
	 */
	info->aw = top_range->top_level - 1;
}
#define pt_iommu_fmt_hw_info vtdss_pt_iommu_fmt_hw_info

#if defined(GENERIC_PT_KUNIT)
static const struct pt_iommu_vtdss_cfg vtdss_kunit_fmt_cfgs[] = {
	[0] = { .common.hw_max_vasz_lg2 = 39, .top_level = 2},
	[1] = { .common.hw_max_vasz_lg2 = 48, .top_level = 3},
	[2] = { .common.hw_max_vasz_lg2 = 57, .top_level = 4},
};
#define kunit_fmt_cfgs vtdss_kunit_fmt_cfgs
enum { KUNIT_FMT_FEATURES = BIT(PT_FEAT_VTDSS_FORCE_WRITEABLE) };
#endif
#endif
