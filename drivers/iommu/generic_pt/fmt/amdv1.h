/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES
 *
 * AMD IOMMU v1 page table
 *
 * This is described in Section "2.2.3 I/O Page Tables for Host Translations"
 * of the "AMD I/O Virtualization Technology (IOMMU) Specification"
 *
 * Note the level numbering here matches the core code, so level 0 is the same
 * as mode 1.
 *
 */
#ifndef __GENERIC_PT_FMT_AMDV1_H
#define __GENERIC_PT_FMT_AMDV1_H

#include "defs_amdv1.h"
#include "../pt_defs.h"

#include <asm/page.h>
#include <linux/bitfield.h>
#include <linux/container_of.h>
#include <linux/mem_encrypt.h>
#include <linux/minmax.h>
#include <linux/sizes.h>
#include <linux/string.h>

enum {
	PT_ITEM_WORD_SIZE = sizeof(u64),
	/*
	 * The IOMMUFD selftest uses the AMDv1 format with some alterations It
	 * uses a 2k page size to test cases where the CPU page size is not the
	 * same.
	 */
#ifdef AMDV1_IOMMUFD_SELFTEST
	PT_MAX_VA_ADDRESS_LG2 = 56,
	PT_MAX_OUTPUT_ADDRESS_LG2 = 51,
	PT_MAX_TOP_LEVEL = 4,
	PT_GRANULE_LG2SZ = 11,
#else
	PT_MAX_VA_ADDRESS_LG2 = 64,
	PT_MAX_OUTPUT_ADDRESS_LG2 = 52,
	PT_MAX_TOP_LEVEL = 5,
	PT_GRANULE_LG2SZ = 12,
#endif
	PT_TABLEMEM_LG2SZ = 12,

	/* The DTE only has these bits for the top phyiscal address */
	PT_TOP_PHYS_MASK = GENMASK_ULL(51, 12),
};

/* PTE bits */
enum {
	AMDV1PT_FMT_PR = BIT(0),
	AMDV1PT_FMT_D = BIT(6),
	AMDV1PT_FMT_NEXT_LEVEL = GENMASK_ULL(11, 9),
	AMDV1PT_FMT_OA = GENMASK_ULL(51, 12),
	AMDV1PT_FMT_FC = BIT_ULL(60),
	AMDV1PT_FMT_IR = BIT_ULL(61),
	AMDV1PT_FMT_IW = BIT_ULL(62),
};

/*
 * gcc 13 has a bug where it thinks the output of FIELD_GET() is an enum, make
 * these defines to avoid it.
 */
#define AMDV1PT_FMT_NL_DEFAULT 0
#define AMDV1PT_FMT_NL_SIZE 7

static inline pt_oaddr_t amdv1pt_table_pa(const struct pt_state *pts)
{
	u64 entry = pts->entry;

	if (pts_feature(pts, PT_FEAT_AMDV1_ENCRYPT_TABLES))
		entry = __sme_clr(entry);
	return oalog2_mul(FIELD_GET(AMDV1PT_FMT_OA, entry), PT_GRANULE_LG2SZ);
}
#define pt_table_pa amdv1pt_table_pa

/* Returns the oa for the start of the contiguous entry */
static inline pt_oaddr_t amdv1pt_entry_oa(const struct pt_state *pts)
{
	u64 entry = pts->entry;
	pt_oaddr_t oa;

	if (pts_feature(pts, PT_FEAT_AMDV1_ENCRYPT_TABLES))
		entry = __sme_clr(entry);
	oa = FIELD_GET(AMDV1PT_FMT_OA, entry);

	if (FIELD_GET(AMDV1PT_FMT_NEXT_LEVEL, entry) == AMDV1PT_FMT_NL_SIZE) {
		unsigned int sz_bits = oaffz(oa);

		oa = oalog2_set_mod(oa, 0, sz_bits);
	} else if (PT_WARN_ON(FIELD_GET(AMDV1PT_FMT_NEXT_LEVEL, entry) !=
			      AMDV1PT_FMT_NL_DEFAULT))
		return 0;
	return oalog2_mul(oa, PT_GRANULE_LG2SZ);
}
#define pt_entry_oa amdv1pt_entry_oa

static inline bool amdv1pt_can_have_leaf(const struct pt_state *pts)
{
	/*
	 * Table 15: Page Table Level Parameters
	 * The top most level cannot have translation entries
	 */
	return pts->level < PT_MAX_TOP_LEVEL;
}
#define pt_can_have_leaf amdv1pt_can_have_leaf

/* Body in pt_fmt_defaults.h */
static inline unsigned int pt_table_item_lg2sz(const struct pt_state *pts);

static inline unsigned int
amdv1pt_entry_num_contig_lg2(const struct pt_state *pts)
{
	u32 code;

	if (FIELD_GET(AMDV1PT_FMT_NEXT_LEVEL, pts->entry) ==
	    AMDV1PT_FMT_NL_DEFAULT)
		return ilog2(1);

	PT_WARN_ON(FIELD_GET(AMDV1PT_FMT_NEXT_LEVEL, pts->entry) !=
		   AMDV1PT_FMT_NL_SIZE);

	/*
	 * The contiguous size is encoded in the length of a string of 1's in
	 * the low bits of the OA. Reverse the equation:
	 *  code = log2_to_int(num_contig_lg2 + item_lg2sz -
	 *              PT_GRANULE_LG2SZ - 1) - 1
	 * Which can be expressed as:
	 *  num_contig_lg2 = oalog2_ffz(code) + 1 -
	 *              item_lg2sz - PT_GRANULE_LG2SZ
	 *
	 * Assume the bit layout is correct and remove the masking. Reorganize
	 * the equation to move all the arithmetic before the ffz.
	 */
	code = pts->entry >> (__bf_shf(AMDV1PT_FMT_OA) - 1 +
			      pt_table_item_lg2sz(pts) - PT_GRANULE_LG2SZ);
	return ffz_t(u32, code);
}
#define pt_entry_num_contig_lg2 amdv1pt_entry_num_contig_lg2

static inline unsigned int amdv1pt_num_items_lg2(const struct pt_state *pts)
{
	/*
	 * Top entry covers bits [63:57] only, this is handled through
	 * max_vasz_lg2.
	 */
	if (PT_WARN_ON(pts->level == 5))
		return 7;
	return PT_TABLEMEM_LG2SZ - ilog2(sizeof(u64));
}
#define pt_num_items_lg2 amdv1pt_num_items_lg2

static inline pt_vaddr_t amdv1pt_possible_sizes(const struct pt_state *pts)
{
	unsigned int isz_lg2 = pt_table_item_lg2sz(pts);

	if (!amdv1pt_can_have_leaf(pts))
		return 0;

	/*
	 * Table 14: Example Page Size Encodings
	 * Address bits 51:32 can be used to encode page sizes greater than 4
	 * Gbytes. Address bits 63:52 are zero-extended.
	 *
	 * 512GB Pages are not supported due to a hardware bug.
	 * Otherwise every power of two size is supported.
	 */
	return GENMASK_ULL(min(51, isz_lg2 + amdv1pt_num_items_lg2(pts) - 1),
			   isz_lg2) & ~SZ_512G;
}
#define pt_possible_sizes amdv1pt_possible_sizes

static inline enum pt_entry_type amdv1pt_load_entry_raw(struct pt_state *pts)
{
	const u64 *tablep = pt_cur_table(pts, u64) + pts->index;
	unsigned int next_level;
	u64 entry;

	pts->entry = entry = READ_ONCE(*tablep);
	if (!(entry & AMDV1PT_FMT_PR))
		return PT_ENTRY_EMPTY;

	next_level = FIELD_GET(AMDV1PT_FMT_NEXT_LEVEL, pts->entry);
	if (pts->level == 0 || next_level == AMDV1PT_FMT_NL_DEFAULT ||
	    next_level == AMDV1PT_FMT_NL_SIZE)
		return PT_ENTRY_OA;
	return PT_ENTRY_TABLE;
}
#define pt_load_entry_raw amdv1pt_load_entry_raw

static inline void
amdv1pt_install_leaf_entry(struct pt_state *pts, pt_oaddr_t oa,
			   unsigned int oasz_lg2,
			   const struct pt_write_attrs *attrs)
{
	unsigned int isz_lg2 = pt_table_item_lg2sz(pts);
	u64 *tablep = pt_cur_table(pts, u64) + pts->index;
	u64 entry;

	if (!pt_check_install_leaf_args(pts, oa, oasz_lg2))
		return;

	entry = AMDV1PT_FMT_PR |
		FIELD_PREP(AMDV1PT_FMT_OA, log2_div(oa, PT_GRANULE_LG2SZ)) |
		attrs->descriptor_bits;

	if (oasz_lg2 == isz_lg2) {
		entry |= FIELD_PREP(AMDV1PT_FMT_NEXT_LEVEL,
				    AMDV1PT_FMT_NL_DEFAULT);
		WRITE_ONCE(*tablep, entry);
	} else {
		unsigned int num_contig_lg2 = oasz_lg2 - isz_lg2;
		u64 *end = tablep + log2_to_int(num_contig_lg2);

		entry |= FIELD_PREP(AMDV1PT_FMT_NEXT_LEVEL,
				    AMDV1PT_FMT_NL_SIZE) |
			 FIELD_PREP(AMDV1PT_FMT_OA,
				    oalog2_to_int(oasz_lg2 - PT_GRANULE_LG2SZ -
						  1) -
					    1);

		/* See amdv1pt_clear_entries() */
		if (num_contig_lg2 <= ilog2(32)) {
			for (; tablep != end; tablep++)
				WRITE_ONCE(*tablep, entry);
		} else {
			memset64(tablep, entry, log2_to_int(num_contig_lg2));
		}
	}
	pts->entry = entry;
}
#define pt_install_leaf_entry amdv1pt_install_leaf_entry

static inline bool amdv1pt_install_table(struct pt_state *pts,
					 pt_oaddr_t table_pa,
					 const struct pt_write_attrs *attrs)
{
	u64 entry;

	/*
	 * IR and IW are ANDed from the table levels along with the PTE. We
	 * always control permissions from the PTE, so always set IR and IW for
	 * tables.
	 */
	entry = AMDV1PT_FMT_PR |
		FIELD_PREP(AMDV1PT_FMT_NEXT_LEVEL, pts->level) |
		FIELD_PREP(AMDV1PT_FMT_OA,
			   log2_div(table_pa, PT_GRANULE_LG2SZ)) |
		AMDV1PT_FMT_IR | AMDV1PT_FMT_IW;
	if (pts_feature(pts, PT_FEAT_AMDV1_ENCRYPT_TABLES))
		entry = __sme_set(entry);
	return pt_table_install64(pts, entry);
}
#define pt_install_table amdv1pt_install_table

static inline void amdv1pt_attr_from_entry(const struct pt_state *pts,
					   struct pt_write_attrs *attrs)
{
	attrs->descriptor_bits =
		pts->entry & (AMDV1PT_FMT_FC | AMDV1PT_FMT_IR | AMDV1PT_FMT_IW);
}
#define pt_attr_from_entry amdv1pt_attr_from_entry

static inline void amdv1pt_clear_entries(struct pt_state *pts,
					 unsigned int num_contig_lg2)
{
	u64 *tablep = pt_cur_table(pts, u64) + pts->index;
	u64 *end = tablep + log2_to_int(num_contig_lg2);

	/*
	 * gcc generates rep stos for the io-pgtable code, and this difference
	 * can show in microbenchmarks with larger contiguous page sizes.
	 * rep is slower for small cases.
	 */
	if (num_contig_lg2 <= ilog2(32)) {
		for (; tablep != end; tablep++)
			WRITE_ONCE(*tablep, 0);
	} else {
		memset64(tablep, 0, log2_to_int(num_contig_lg2));
	}
}
#define pt_clear_entries amdv1pt_clear_entries

static inline bool amdv1pt_entry_is_write_dirty(const struct pt_state *pts)
{
	unsigned int num_contig_lg2 = amdv1pt_entry_num_contig_lg2(pts);
	u64 *tablep = pt_cur_table(pts, u64) +
		      log2_set_mod(pts->index, 0, num_contig_lg2);
	u64 *end = tablep + log2_to_int(num_contig_lg2);

	for (; tablep != end; tablep++)
		if (READ_ONCE(*tablep) & AMDV1PT_FMT_D)
			return true;
	return false;
}
#define pt_entry_is_write_dirty amdv1pt_entry_is_write_dirty

static inline void amdv1pt_entry_make_write_clean(struct pt_state *pts)
{
	unsigned int num_contig_lg2 = amdv1pt_entry_num_contig_lg2(pts);
	u64 *tablep = pt_cur_table(pts, u64) +
		      log2_set_mod(pts->index, 0, num_contig_lg2);
	u64 *end = tablep + log2_to_int(num_contig_lg2);

	for (; tablep != end; tablep++)
		WRITE_ONCE(*tablep, READ_ONCE(*tablep) & ~(u64)AMDV1PT_FMT_D);
}
#define pt_entry_make_write_clean amdv1pt_entry_make_write_clean

static inline bool amdv1pt_entry_make_write_dirty(struct pt_state *pts)
{
	u64 *tablep = pt_cur_table(pts, u64) + pts->index;
	u64 new = pts->entry | AMDV1PT_FMT_D;

	return try_cmpxchg64(tablep, &pts->entry, new);
}
#define pt_entry_make_write_dirty amdv1pt_entry_make_write_dirty

/* --- iommu */
#include <linux/generic_pt/iommu.h>
#include <linux/iommu.h>

#define pt_iommu_table pt_iommu_amdv1

/* The common struct is in the per-format common struct */
static inline struct pt_common *common_from_iommu(struct pt_iommu *iommu_table)
{
	return &container_of(iommu_table, struct pt_iommu_amdv1, iommu)
			->amdpt.common;
}

static inline struct pt_iommu *iommu_from_common(struct pt_common *common)
{
	return &container_of(common, struct pt_iommu_amdv1, amdpt.common)->iommu;
}

static inline int amdv1pt_iommu_set_prot(struct pt_common *common,
					 struct pt_write_attrs *attrs,
					 unsigned int iommu_prot)
{
	u64 pte = 0;

	if (pt_feature(common, PT_FEAT_AMDV1_FORCE_COHERENCE))
		pte |= AMDV1PT_FMT_FC;
	if (iommu_prot & IOMMU_READ)
		pte |= AMDV1PT_FMT_IR;
	if (iommu_prot & IOMMU_WRITE)
		pte |= AMDV1PT_FMT_IW;

	/*
	 * Ideally we'd have an IOMMU_ENCRYPTED flag set by higher levels to
	 * control this. For now if the tables use sme_set then so do the ptes.
	 */
	if (pt_feature(common, PT_FEAT_AMDV1_ENCRYPT_TABLES))
		pte = __sme_set(pte);

	attrs->descriptor_bits = pte;
	return 0;
}
#define pt_iommu_set_prot amdv1pt_iommu_set_prot

static inline int amdv1pt_iommu_fmt_init(struct pt_iommu_amdv1 *iommu_table,
					 const struct pt_iommu_amdv1_cfg *cfg)
{
	struct pt_amdv1 *table = &iommu_table->amdpt;
	unsigned int max_vasz_lg2 = PT_MAX_VA_ADDRESS_LG2;

	if (cfg->starting_level == 0 || cfg->starting_level > PT_MAX_TOP_LEVEL)
		return -EINVAL;

	if (!pt_feature(&table->common, PT_FEAT_DYNAMIC_TOP) &&
	    cfg->starting_level != PT_MAX_TOP_LEVEL)
		max_vasz_lg2 = PT_GRANULE_LG2SZ +
			       (PT_TABLEMEM_LG2SZ - ilog2(sizeof(u64))) *
				       (cfg->starting_level + 1);

	table->common.max_vasz_lg2 =
		min(max_vasz_lg2, cfg->common.hw_max_vasz_lg2);
	table->common.max_oasz_lg2 =
		min(PT_MAX_OUTPUT_ADDRESS_LG2, cfg->common.hw_max_oasz_lg2);
	pt_top_set_level(&table->common, cfg->starting_level);
	return 0;
}
#define pt_iommu_fmt_init amdv1pt_iommu_fmt_init

#ifndef PT_FMT_VARIANT
static inline void
amdv1pt_iommu_fmt_hw_info(struct pt_iommu_amdv1 *table,
			  const struct pt_range *top_range,
			  struct pt_iommu_amdv1_hw_info *info)
{
	info->host_pt_root = virt_to_phys(top_range->top_table);
	PT_WARN_ON(info->host_pt_root & ~PT_TOP_PHYS_MASK);
	info->mode = top_range->top_level + 1;
}
#define pt_iommu_fmt_hw_info amdv1pt_iommu_fmt_hw_info
#endif

#if defined(GENERIC_PT_KUNIT)
static const struct pt_iommu_amdv1_cfg amdv1_kunit_fmt_cfgs[] = {
	/* Matches what io_pgtable does */
	[0] = { .starting_level = 2 },
};
#define kunit_fmt_cfgs amdv1_kunit_fmt_cfgs
enum { KUNIT_FMT_FEATURES = 0 };
#endif

#endif
