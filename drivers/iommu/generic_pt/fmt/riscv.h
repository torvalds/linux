/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES
 *
 * RISC-V page table
 *
 * This is described in Sections:
 *  12.3. Sv32: Page-Based 32-bit Virtual-Memory Systems
 *  12.4. Sv39: Page-Based 39-bit Virtual-Memory System
 *  12.5. Sv48: Page-Based 48-bit Virtual-Memory System
 *  12.6. Sv57: Page-Based 57-bit Virtual-Memory System
 * of the "The RISC-V Instruction Set Manual: Volume II"
 *
 * This includes the contiguous page extension from:
 *  Chapter 13. "Svnapot" Extension for NAPOT Translation Contiguity,
 *     Version 1.0
 *
 * The table format is sign extended and supports leafs in every level. The spec
 * doesn't talk a lot about levels, but level here is the same as i=LEVELS-1 in
 * the spec.
 */
#ifndef __GENERIC_PT_FMT_RISCV_H
#define __GENERIC_PT_FMT_RISCV_H

#include "defs_riscv.h"
#include "../pt_defs.h"

#include <linux/bitfield.h>
#include <linux/container_of.h>
#include <linux/log2.h>
#include <linux/sizes.h>

enum {
	PT_ITEM_WORD_SIZE = sizeof(pt_riscv_entry_t),
#ifdef PT_RISCV_32BIT
	PT_MAX_VA_ADDRESS_LG2 = 32,
	PT_MAX_OUTPUT_ADDRESS_LG2 = 34,
	PT_MAX_TOP_LEVEL = 1,
#else
	PT_MAX_VA_ADDRESS_LG2 = 57,
	PT_MAX_OUTPUT_ADDRESS_LG2 = 56,
	PT_MAX_TOP_LEVEL = 4,
#endif
	PT_GRANULE_LG2SZ = 12,
	PT_TABLEMEM_LG2SZ = 12,

	/* fsc.PPN is 44 bits wide, all PPNs are 4k aligned */
	PT_TOP_PHYS_MASK = GENMASK_ULL(55, 12),
};

/* PTE bits */
enum {
	RISCVPT_V = BIT(0),
	RISCVPT_R = BIT(1),
	RISCVPT_W = BIT(2),
	RISCVPT_X = BIT(3),
	RISCVPT_U = BIT(4),
	RISCVPT_G = BIT(5),
	RISCVPT_A = BIT(6),
	RISCVPT_D = BIT(7),
	RISCVPT_RSW = GENMASK(9, 8),
	RISCVPT_PPN32 = GENMASK(31, 10),

	RISCVPT_PPN64 = GENMASK_ULL(53, 10),
	RISCVPT_PPN64_64K = GENMASK_ULL(53, 14),
	RISCVPT_PBMT = GENMASK_ULL(62, 61),
	RISCVPT_N = BIT_ULL(63),

	/* Svnapot encodings for ppn[0] */
	RISCVPT_PPN64_64K_SZ = BIT(13),
};

#ifdef PT_RISCV_32BIT
#define RISCVPT_PPN RISCVPT_PPN32
#define pt_riscv pt_riscv_32
#else
#define RISCVPT_PPN RISCVPT_PPN64
#define pt_riscv pt_riscv_64
#endif

#define common_to_riscvpt(common_ptr) \
	container_of_const(common_ptr, struct pt_riscv, common)
#define to_riscvpt(pts) common_to_riscvpt((pts)->range->common)

static inline pt_oaddr_t riscvpt_table_pa(const struct pt_state *pts)
{
	return oalog2_mul(FIELD_GET(RISCVPT_PPN, pts->entry), PT_GRANULE_LG2SZ);
}
#define pt_table_pa riscvpt_table_pa

static inline pt_oaddr_t riscvpt_entry_oa(const struct pt_state *pts)
{
	if (pts_feature(pts, PT_FEAT_RISCV_SVNAPOT_64K) &&
	    pts->entry & RISCVPT_N) {
		PT_WARN_ON(pts->level != 0);
		return oalog2_mul(FIELD_GET(RISCVPT_PPN64_64K, pts->entry),
				  ilog2(SZ_64K));
	}
	return oalog2_mul(FIELD_GET(RISCVPT_PPN, pts->entry), PT_GRANULE_LG2SZ);
}
#define pt_entry_oa riscvpt_entry_oa

static inline bool riscvpt_can_have_leaf(const struct pt_state *pts)
{
	return true;
}
#define pt_can_have_leaf riscvpt_can_have_leaf

/* Body in pt_fmt_defaults.h */
static inline unsigned int pt_table_item_lg2sz(const struct pt_state *pts);

static inline unsigned int
riscvpt_entry_num_contig_lg2(const struct pt_state *pts)
{
	if (PT_SUPPORTED_FEATURE(PT_FEAT_RISCV_SVNAPOT_64K) &&
	    pts->entry & RISCVPT_N) {
		PT_WARN_ON(!pts_feature(pts, PT_FEAT_RISCV_SVNAPOT_64K));
		PT_WARN_ON(pts->level);
		return ilog2(16);
	}
	return ilog2(1);
}
#define pt_entry_num_contig_lg2 riscvpt_entry_num_contig_lg2

static inline unsigned int riscvpt_num_items_lg2(const struct pt_state *pts)
{
	return PT_TABLEMEM_LG2SZ - ilog2(sizeof(u64));
}
#define pt_num_items_lg2 riscvpt_num_items_lg2

static inline unsigned short
riscvpt_contig_count_lg2(const struct pt_state *pts)
{
	if (pts->level == 0 && pts_feature(pts, PT_FEAT_RISCV_SVNAPOT_64K))
		return ilog2(16);
	return ilog2(1);
}
#define pt_contig_count_lg2 riscvpt_contig_count_lg2

static inline enum pt_entry_type riscvpt_load_entry_raw(struct pt_state *pts)
{
	const pt_riscv_entry_t *tablep = pt_cur_table(pts, pt_riscv_entry_t);
	pt_riscv_entry_t entry;

	pts->entry = entry = READ_ONCE(tablep[pts->index]);
	if (!(entry & RISCVPT_V))
		return PT_ENTRY_EMPTY;
	if (pts->level == 0 ||
	    ((entry & (RISCVPT_X | RISCVPT_W | RISCVPT_R)) != 0))
		return PT_ENTRY_OA;
	return PT_ENTRY_TABLE;
}
#define pt_load_entry_raw riscvpt_load_entry_raw

static inline void
riscvpt_install_leaf_entry(struct pt_state *pts, pt_oaddr_t oa,
			   unsigned int oasz_lg2,
			   const struct pt_write_attrs *attrs)
{
	pt_riscv_entry_t *tablep = pt_cur_table(pts, pt_riscv_entry_t);
	pt_riscv_entry_t entry;

	if (!pt_check_install_leaf_args(pts, oa, oasz_lg2))
		return;

	entry = RISCVPT_V |
		FIELD_PREP(RISCVPT_PPN, log2_div(oa, PT_GRANULE_LG2SZ)) |
		attrs->descriptor_bits;

	if (pts_feature(pts, PT_FEAT_RISCV_SVNAPOT_64K) && pts->level == 0 &&
	    oasz_lg2 != PT_GRANULE_LG2SZ) {
		u64 *end;

		entry |= RISCVPT_N | RISCVPT_PPN64_64K_SZ;
		tablep += pts->index;
		end = tablep + log2_div(SZ_64K, PT_GRANULE_LG2SZ);
		for (; tablep != end; tablep++)
			WRITE_ONCE(*tablep, entry);
	} else {
		/* FIXME does riscv need this to be cmpxchg? */
		WRITE_ONCE(tablep[pts->index], entry);
	}
	pts->entry = entry;
}
#define pt_install_leaf_entry riscvpt_install_leaf_entry

static inline bool riscvpt_install_table(struct pt_state *pts,
					 pt_oaddr_t table_pa,
					 const struct pt_write_attrs *attrs)
{
	pt_riscv_entry_t entry;

	entry = RISCVPT_V |
		FIELD_PREP(RISCVPT_PPN, log2_div(table_pa, PT_GRANULE_LG2SZ));
	return pt_table_install64(pts, entry);
}
#define pt_install_table riscvpt_install_table

static inline void riscvpt_attr_from_entry(const struct pt_state *pts,
					   struct pt_write_attrs *attrs)
{
	attrs->descriptor_bits =
		pts->entry & (RISCVPT_R | RISCVPT_W | RISCVPT_X | RISCVPT_U |
			      RISCVPT_G | RISCVPT_A | RISCVPT_D);
}
#define pt_attr_from_entry riscvpt_attr_from_entry

/* --- iommu */
#include <linux/generic_pt/iommu.h>
#include <linux/iommu.h>

#define pt_iommu_table pt_iommu_riscv_64

/* The common struct is in the per-format common struct */
static inline struct pt_common *common_from_iommu(struct pt_iommu *iommu_table)
{
	return &container_of(iommu_table, struct pt_iommu_table, iommu)
			->riscv_64pt.common;
}

static inline struct pt_iommu *iommu_from_common(struct pt_common *common)
{
	return &container_of(common, struct pt_iommu_table, riscv_64pt.common)
			->iommu;
}

static inline int riscvpt_iommu_set_prot(struct pt_common *common,
					 struct pt_write_attrs *attrs,
					 unsigned int iommu_prot)
{
	u64 pte;

	pte = RISCVPT_A | RISCVPT_U;
	if (iommu_prot & IOMMU_WRITE)
		pte |= RISCVPT_W | RISCVPT_R | RISCVPT_D;
	if (iommu_prot & IOMMU_READ)
		pte |= RISCVPT_R;
	if (!(iommu_prot & IOMMU_NOEXEC))
		pte |= RISCVPT_X;

	/* Caller must specify a supported combination of flags */
	if (unlikely((pte & (RISCVPT_X | RISCVPT_W | RISCVPT_R)) == 0))
		return -EOPNOTSUPP;

	attrs->descriptor_bits = pte;
	return 0;
}
#define pt_iommu_set_prot riscvpt_iommu_set_prot

static inline int
riscvpt_iommu_fmt_init(struct pt_iommu_riscv_64 *iommu_table,
		       const struct pt_iommu_riscv_64_cfg *cfg)
{
	struct pt_riscv *table = &iommu_table->riscv_64pt;

	switch (cfg->common.hw_max_vasz_lg2) {
	case 39:
		pt_top_set_level(&table->common, 2);
		break;
	case 48:
		pt_top_set_level(&table->common, 3);
		break;
	case 57:
		pt_top_set_level(&table->common, 4);
		break;
	default:
		return -EINVAL;
	}
	table->common.max_oasz_lg2 =
		min(PT_MAX_OUTPUT_ADDRESS_LG2, cfg->common.hw_max_oasz_lg2);
	return 0;
}
#define pt_iommu_fmt_init riscvpt_iommu_fmt_init

static inline void
riscvpt_iommu_fmt_hw_info(struct pt_iommu_riscv_64 *table,
			  const struct pt_range *top_range,
			  struct pt_iommu_riscv_64_hw_info *info)
{
	phys_addr_t top_phys = virt_to_phys(top_range->top_table);

	info->ppn = oalog2_div(top_phys, PT_GRANULE_LG2SZ);
	PT_WARN_ON(top_phys & ~PT_TOP_PHYS_MASK);

	/*
	 * See Table 3. Encodings of iosatp.MODE field" for DC.tx.SXL = 0:
	 *  8 = Sv39 = top level 2
	 *  9 = Sv38 = top level 3
	 *  10 = Sv57 = top level 4
	 */
	info->fsc_iosatp_mode = top_range->top_level + 6;
}
#define pt_iommu_fmt_hw_info riscvpt_iommu_fmt_hw_info

#if defined(GENERIC_PT_KUNIT)
static const struct pt_iommu_riscv_64_cfg riscv_64_kunit_fmt_cfgs[] = {
	[0] = { .common.features = BIT(PT_FEAT_RISCV_SVNAPOT_64K),
		.common.hw_max_oasz_lg2 = 56,
		.common.hw_max_vasz_lg2 = 39 },
	[1] = { .common.features = 0,
		.common.hw_max_oasz_lg2 = 56,
		.common.hw_max_vasz_lg2 = 48 },
	[2] = { .common.features = BIT(PT_FEAT_RISCV_SVNAPOT_64K),
		.common.hw_max_oasz_lg2 = 56,
		.common.hw_max_vasz_lg2 = 57 },
};
#define kunit_fmt_cfgs riscv_64_kunit_fmt_cfgs
enum {
	KUNIT_FMT_FEATURES = BIT(PT_FEAT_RISCV_SVNAPOT_64K),
};
#endif

#endif
