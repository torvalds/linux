/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES
 *
 * x86 page table. Supports the 4 and 5 level variations.
 *
 * The 4 and 5 level version is described in:
 *   Section "4.4 4-Level Paging and 5-Level Paging" of the Intel Software
 *   Developer's Manual Volume 3
 *
 *   Section "9.7 First-Stage Paging Entries" of the "Intel Virtualization
 *   Technology for Directed I/O Architecture Specification"
 *
 *   Section "2.2.6 I/O Page Tables for Guest Translations" of the "AMD I/O
 *   Virtualization Technology (IOMMU) Specification"
 *
 * It is used by x86 CPUs, AMD and VT-d IOMMU HW.
 *
 * Note the 3 level format is very similar and almost implemented here. The
 * reserved/ignored layout is different and there are functional bit
 * differences.
 *
 * This format uses PT_FEAT_SIGN_EXTEND to have a upper/non-canonical/lower
 * split. PT_FEAT_SIGN_EXTEND is optional as AMD IOMMU sometimes uses non-sign
 * extended addressing with this page table format.
 *
 * The named levels in the spec map to the pts->level as:
 *   Table/PTE - 0
 *   Directory/PDE - 1
 *   Directory Ptr/PDPTE - 2
 *   PML4/PML4E - 3
 *   PML5/PML5E - 4
 */
#ifndef __GENERIC_PT_FMT_X86_64_H
#define __GENERIC_PT_FMT_X86_64_H

#include "defs_x86_64.h"
#include "../pt_defs.h"

#include <linux/bitfield.h>
#include <linux/container_of.h>
#include <linux/log2.h>
#include <linux/mem_encrypt.h>

enum {
	PT_MAX_OUTPUT_ADDRESS_LG2 = 52,
	PT_MAX_VA_ADDRESS_LG2 = 57,
	PT_ITEM_WORD_SIZE = sizeof(u64),
	PT_MAX_TOP_LEVEL = 4,
	PT_GRANULE_LG2SZ = 12,
	PT_TABLEMEM_LG2SZ = 12,

	/*
	 * For AMD the GCR3 Base only has these bits. For VT-d FSPTPTR is 4k
	 * aligned and is limited by the architected HAW
	 */
	PT_TOP_PHYS_MASK = GENMASK_ULL(51, 12),
};

/* Shared descriptor bits */
enum {
	X86_64_FMT_P = BIT(0),
	X86_64_FMT_RW = BIT(1),
	X86_64_FMT_U = BIT(2),
	X86_64_FMT_A = BIT(5),
	X86_64_FMT_D = BIT(6),
	X86_64_FMT_OA = GENMASK_ULL(51, 12),
	X86_64_FMT_XD = BIT_ULL(63),
};

/* PDPTE/PDE */
enum {
	X86_64_FMT_PS = BIT(7),
};

static inline pt_oaddr_t x86_64_pt_table_pa(const struct pt_state *pts)
{
	u64 entry = pts->entry;

	if (pts_feature(pts, PT_FEAT_X86_64_AMD_ENCRYPT_TABLES))
		entry = __sme_clr(entry);
	return oalog2_mul(FIELD_GET(X86_64_FMT_OA, entry),
			  PT_TABLEMEM_LG2SZ);
}
#define pt_table_pa x86_64_pt_table_pa

static inline pt_oaddr_t x86_64_pt_entry_oa(const struct pt_state *pts)
{
	u64 entry = pts->entry;

	if (pts_feature(pts, PT_FEAT_X86_64_AMD_ENCRYPT_TABLES))
		entry = __sme_clr(entry);
	return oalog2_mul(FIELD_GET(X86_64_FMT_OA, entry),
			  PT_GRANULE_LG2SZ);
}
#define pt_entry_oa x86_64_pt_entry_oa

static inline bool x86_64_pt_can_have_leaf(const struct pt_state *pts)
{
	return pts->level <= 2;
}
#define pt_can_have_leaf x86_64_pt_can_have_leaf

static inline unsigned int x86_64_pt_num_items_lg2(const struct pt_state *pts)
{
	return PT_TABLEMEM_LG2SZ - ilog2(sizeof(u64));
}
#define pt_num_items_lg2 x86_64_pt_num_items_lg2

static inline enum pt_entry_type x86_64_pt_load_entry_raw(struct pt_state *pts)
{
	const u64 *tablep = pt_cur_table(pts, u64);
	u64 entry;

	pts->entry = entry = READ_ONCE(tablep[pts->index]);
	if (!(entry & X86_64_FMT_P))
		return PT_ENTRY_EMPTY;
	if (pts->level == 0 ||
	    (x86_64_pt_can_have_leaf(pts) && (entry & X86_64_FMT_PS)))
		return PT_ENTRY_OA;
	return PT_ENTRY_TABLE;
}
#define pt_load_entry_raw x86_64_pt_load_entry_raw

static inline void
x86_64_pt_install_leaf_entry(struct pt_state *pts, pt_oaddr_t oa,
			     unsigned int oasz_lg2,
			     const struct pt_write_attrs *attrs)
{
	u64 *tablep = pt_cur_table(pts, u64);
	u64 entry;

	if (!pt_check_install_leaf_args(pts, oa, oasz_lg2))
		return;

	entry = X86_64_FMT_P |
		FIELD_PREP(X86_64_FMT_OA, log2_div(oa, PT_GRANULE_LG2SZ)) |
		attrs->descriptor_bits;
	if (pts->level != 0)
		entry |= X86_64_FMT_PS;

	WRITE_ONCE(tablep[pts->index], entry);
	pts->entry = entry;
}
#define pt_install_leaf_entry x86_64_pt_install_leaf_entry

static inline bool x86_64_pt_install_table(struct pt_state *pts,
					   pt_oaddr_t table_pa,
					   const struct pt_write_attrs *attrs)
{
	u64 entry;

	entry = X86_64_FMT_P | X86_64_FMT_RW | X86_64_FMT_U | X86_64_FMT_A |
		FIELD_PREP(X86_64_FMT_OA, log2_div(table_pa, PT_GRANULE_LG2SZ));
	if (pts_feature(pts, PT_FEAT_X86_64_AMD_ENCRYPT_TABLES))
		entry = __sme_set(entry);
	return pt_table_install64(pts, entry);
}
#define pt_install_table x86_64_pt_install_table

static inline void x86_64_pt_attr_from_entry(const struct pt_state *pts,
					     struct pt_write_attrs *attrs)
{
	attrs->descriptor_bits = pts->entry &
				 (X86_64_FMT_RW | X86_64_FMT_U | X86_64_FMT_A |
				  X86_64_FMT_D | X86_64_FMT_XD);
}
#define pt_attr_from_entry x86_64_pt_attr_from_entry

static inline unsigned int x86_64_pt_max_sw_bit(struct pt_common *common)
{
	return 12;
}
#define pt_max_sw_bit x86_64_pt_max_sw_bit

static inline u64 x86_64_pt_sw_bit(unsigned int bitnr)
{
	if (__builtin_constant_p(bitnr) && bitnr > 12)
		BUILD_BUG();

	/* Bits marked Ignored/AVL in the specification */
	switch (bitnr) {
	case 0:
		return BIT(9);
	case 1:
		return BIT(11);
	case 2 ... 12:
		return BIT_ULL((bitnr - 2) + 52);
	/* Some bits in 8,6,4,3 are available in some entries */
	default:
		PT_WARN_ON(true);
		return 0;
	}
}
#define pt_sw_bit x86_64_pt_sw_bit

/* --- iommu */
#include <linux/generic_pt/iommu.h>
#include <linux/iommu.h>

#define pt_iommu_table pt_iommu_x86_64

/* The common struct is in the per-format common struct */
static inline struct pt_common *common_from_iommu(struct pt_iommu *iommu_table)
{
	return &container_of(iommu_table, struct pt_iommu_table, iommu)
			->x86_64_pt.common;
}

static inline struct pt_iommu *iommu_from_common(struct pt_common *common)
{
	return &container_of(common, struct pt_iommu_table, x86_64_pt.common)
			->iommu;
}

static inline int x86_64_pt_iommu_set_prot(struct pt_common *common,
					   struct pt_write_attrs *attrs,
					   unsigned int iommu_prot)
{
	u64 pte;

	pte = X86_64_FMT_U | X86_64_FMT_A;
	if (iommu_prot & IOMMU_WRITE)
		pte |= X86_64_FMT_RW | X86_64_FMT_D;

	/*
	 * Ideally we'd have an IOMMU_ENCRYPTED flag set by higher levels to
	 * control this. For now if the tables use sme_set then so do the ptes.
	 */
	if (pt_feature(common, PT_FEAT_X86_64_AMD_ENCRYPT_TABLES) &&
	    !(iommu_prot & IOMMU_MMIO))
		pte = __sme_set(pte);

	attrs->descriptor_bits = pte;
	return 0;
}
#define pt_iommu_set_prot x86_64_pt_iommu_set_prot

static inline int
x86_64_pt_iommu_fmt_init(struct pt_iommu_x86_64 *iommu_table,
			 const struct pt_iommu_x86_64_cfg *cfg)
{
	struct pt_x86_64 *table = &iommu_table->x86_64_pt;

	if (cfg->top_level < 3 || cfg->top_level > 4)
		return -EOPNOTSUPP;

	pt_top_set_level(&table->common, cfg->top_level);

	table->common.max_oasz_lg2 =
		min(PT_MAX_OUTPUT_ADDRESS_LG2, cfg->common.hw_max_oasz_lg2);
	return 0;
}
#define pt_iommu_fmt_init x86_64_pt_iommu_fmt_init

static inline void
x86_64_pt_iommu_fmt_hw_info(struct pt_iommu_x86_64 *table,
			    const struct pt_range *top_range,
			    struct pt_iommu_x86_64_hw_info *info)
{
	info->gcr3_pt = virt_to_phys(top_range->top_table);
	PT_WARN_ON(info->gcr3_pt & ~PT_TOP_PHYS_MASK);
	info->levels = top_range->top_level + 1;
}
#define pt_iommu_fmt_hw_info x86_64_pt_iommu_fmt_hw_info

#if defined(GENERIC_PT_KUNIT)
static const struct pt_iommu_x86_64_cfg x86_64_kunit_fmt_cfgs[] = {
	[0] = { .common.features = BIT(PT_FEAT_SIGN_EXTEND),
		.common.hw_max_vasz_lg2 = 48, .top_level = 3 },
	[1] = { .common.features = BIT(PT_FEAT_SIGN_EXTEND),
		.common.hw_max_vasz_lg2 = 57, .top_level = 4 },
	/* AMD IOMMU PASID 0 formats with no SIGN_EXTEND */
	[2] = { .common.hw_max_vasz_lg2 = 47, .top_level = 3 },
	[3] = { .common.hw_max_vasz_lg2 = 56, .top_level = 4},
};
#define kunit_fmt_cfgs x86_64_kunit_fmt_cfgs
enum { KUNIT_FMT_FEATURES =  BIT(PT_FEAT_SIGN_EXTEND)};
#endif
#endif
