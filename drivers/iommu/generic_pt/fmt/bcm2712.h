/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2026 Daniel Drake
 *
 * BCM2712 IOMMU simple two level page table
 */
#ifndef __GENERIC_PT_FMT_BCM2712_H
#define __GENERIC_PT_FMT_BCM2712_H

#include "defs_bcm2712.h"
#include "../pt_defs.h"

#include <linux/bitfield.h>
#include <linux/container_of.h>
#include <linux/log2.h>
#include <linux/sizes.h>

enum {
	/* Hardware provides a two-level page table */
	PT_MAX_TOP_LEVEL = 1,

	/* Hardware page size is strictly 4kb */
	PT_GRANULE_LG2SZ = ilog2(SZ_4K),
	PT_TABLEMEM_LG2SZ = PT_GRANULE_LG2SZ,

	/* Table entries and leaf entries are 32 bits */
	PT_ITEM_WORD_SIZE = sizeof(pt_bcm2712_entry_t),

	/* Leaf entries encode a 25-bit PFN */
	PT_MAX_OUTPUT_ADDRESS_LG2 = 25 + PT_GRANULE_LG2SZ,

	/*
	 * For simplicity, only manage mappings within an address space of
	 * exactly 4GB. This is because our Level 1 directory page will be 4kb
	 * in the smallest case, permitting 1024 entries pointing at Level 0
	 * pages, each permitting 4mb of mapped memory.
	 */
	PT_MAX_VA_ADDRESS_LG2 = ilog2(SZ_4G),

	/* Level 1 base address is programmed as a 32-bit PFN */
	PT_TOP_PHYS_MASK = GENMASK_ULL(31 + PT_GRANULE_LG2SZ, PT_GRANULE_LG2SZ),
};

/* PTE bits */
enum {
	BCM2712PT_VALID = BIT(28),
	BCM2712PT_WRITE = BIT(29),
	BCM2712PT_PAGESIZE = GENMASK(31, 30),
	BCM2712PT_PFN = GENMASK(24, 0),
};

#define common_to_bcm2712pt(common_ptr) \
	container_of_const(common_ptr, struct pt_bcm2712, common)
#define to_bcm2712pt(pts) common_to_bcm2712pt((pts)->range->common)

static inline pt_oaddr_t bcm2712pt_table_pa(const struct pt_state *pts)
{
	return oalog2_mul(FIELD_GET(BCM2712PT_PFN, pts->entry),
			  PT_GRANULE_LG2SZ);
}
#define pt_table_pa bcm2712pt_table_pa
#define pt_item_oa bcm2712pt_table_pa

static inline bool bcm2712pt_can_have_leaf(const struct pt_state *pts)
{
	return true;
}
#define pt_can_have_leaf bcm2712pt_can_have_leaf

/* 4MB pages are installed at level 1, everything else at level 0 */
static inline unsigned int bcm2712pt_pgsz_lg2_to_level(struct pt_common *common,
						       unsigned int pgsize_lg2)
{
	return pgsize_lg2 == ilog2(SZ_4M) ? 1 : 0;
}
#define pt_pgsz_lg2_to_level bcm2712pt_pgsz_lg2_to_level

static inline pt_vaddr_t bcm2712pt_possible_sizes(const struct pt_state *pts)
{
	struct pt_bcm2712 *table = common_to_bcm2712pt(pts->range->common);

	if (pts->level == 1)
		return SZ_4M;

	return SZ_4K |
	       (table->bigpage_lg2 ? BIT_ULL(table->bigpage_lg2) : 0) |
	       (table->superpage_lg2 ? BIT_ULL(table->superpage_lg2) : 0);
}
#define pt_possible_sizes bcm2712pt_possible_sizes

static inline unsigned int
bcm2712pt_entry_num_contig_lg2(const struct pt_state *pts)
{
	struct pt_bcm2712 *table = common_to_bcm2712pt(pts->range->common);
	u32 pgsz = FIELD_GET(BCM2712PT_PAGESIZE, pts->entry);

	if (pts->level != 0 || !(pts->entry & BCM2712PT_VALID))
		return 0;

	/*
	 * Superpage/bigpage contiguous mapping (really just hinting) is handled
	 * via PAGESIZE bits on each leaf entry.
	 */
	if (pgsz == 2 && table->superpage_lg2)
		return table->superpage_lg2 - PT_GRANULE_LG2SZ;
	else if (pgsz == 1 && table->bigpage_lg2)
		return table->bigpage_lg2 - PT_GRANULE_LG2SZ;
	return 0;
}
#define pt_entry_num_contig_lg2 bcm2712pt_entry_num_contig_lg2

static inline unsigned int bcm2712pt_num_items_lg2(const struct pt_state *pts)
{
	return PT_TABLEMEM_LG2SZ - ilog2(PT_ITEM_WORD_SIZE);
}
#define pt_num_items_lg2 bcm2712pt_num_items_lg2

static inline enum pt_entry_type bcm2712pt_load_entry_raw(struct pt_state *pts)
{
	const pt_bcm2712_entry_t *tablep =
		pt_cur_table(pts, pt_bcm2712_entry_t);

	pts->entry = READ_ONCE(tablep[pts->index]);
	if (!(pts->entry & BCM2712PT_VALID))
		return PT_ENTRY_EMPTY;

	if (pts->level == 1) {
		if (FIELD_GET(BCM2712PT_PAGESIZE, pts->entry) == 3)
			return PT_ENTRY_OA;
		return PT_ENTRY_TABLE;
	}

	return PT_ENTRY_OA;
}
#define pt_load_entry_raw bcm2712pt_load_entry_raw

static inline void
bcm2712pt_install_leaf_entry(struct pt_state *pts, pt_oaddr_t oa,
			     unsigned int oasz_lg2,
			     const struct pt_write_attrs *attrs)
{
	pt_bcm2712_entry_t *tablep = pt_cur_table(pts, pt_bcm2712_entry_t);
	pt_bcm2712_entry_t entry;

	if (!pt_check_install_leaf_args(pts, oa, oasz_lg2))
		return;

	entry = BCM2712PT_VALID | attrs->descriptor_bits |
		FIELD_PREP(BCM2712PT_PFN, oalog2_div(oa, PT_GRANULE_LG2SZ));

	if (pts->level == 1) {
		/* Level 1 hugepage (4MB) */
		entry |= FIELD_PREP(BCM2712PT_PAGESIZE, 3);
		WRITE_ONCE(tablep[pts->index], entry);
		pts->entry = entry;
		return;
	}

	if (oasz_lg2 == PT_GRANULE_LG2SZ) {
		WRITE_ONCE(tablep[pts->index], entry);
		pts->entry = entry;
	} else {
		struct pt_bcm2712 *table =
			common_to_bcm2712pt(pts->range->common);
		u32 *end;

		tablep += pts->index;
		end = tablep + log2_to_int(oasz_lg2 - PT_GRANULE_LG2SZ);

		/*
		 * Leaf entries can contain hints indicating bigpage/superpage
		 * contiguous mappings to permit TLB optimization
		 */
		if (oasz_lg2 == table->superpage_lg2)
			entry |= FIELD_PREP(BCM2712PT_PAGESIZE, 2);
		else if (oasz_lg2 == table->bigpage_lg2)
			entry |= FIELD_PREP(BCM2712PT_PAGESIZE, 1);

		pts->entry = entry;
		for (; tablep != end; tablep++, entry++)
			WRITE_ONCE(*tablep, entry);
	}
}
#define pt_install_leaf_entry bcm2712pt_install_leaf_entry

static inline bool bcm2712pt_install_table(struct pt_state *pts,
					   pt_oaddr_t table_pa,
					   const struct pt_write_attrs *attrs)
{
	pt_bcm2712_entry_t entry =
		BCM2712PT_VALID |
		FIELD_PREP(BCM2712PT_PFN,
			   oalog2_div(table_pa, PT_GRANULE_LG2SZ));

	return pt_table_install32(pts, entry);
}
#define pt_install_table bcm2712pt_install_table

static inline void bcm2712pt_attr_from_entry(const struct pt_state *pts,
					     struct pt_write_attrs *attrs)
{
	attrs->descriptor_bits = pts->entry & BCM2712PT_WRITE;
}
#define pt_attr_from_entry bcm2712pt_attr_from_entry

static inline unsigned int bcm2712pt_max_sw_bit(struct pt_common *common)
{
	return 2;
}
#define pt_max_sw_bit bcm2712pt_max_sw_bit

static inline u32 bcm2712pt_sw_bit(unsigned int bitnr)
{
	if (__builtin_constant_p(bitnr) && bitnr > 2)
		BUILD_BUG();
	if (PT_WARN_ON(bitnr > 2))
		return 0;

	/* Bits 27, 26 and 25 are believed spare */
	return BIT(27 - bitnr);
}
#define pt_sw_bit bcm2712pt_sw_bit

/* --- iommu */
#include <linux/generic_pt/iommu.h>
#include <linux/iommu.h>

#define pt_iommu_table pt_iommu_bcm2712

/* The common struct is in the per-format common struct */
static inline struct pt_common *common_from_iommu(struct pt_iommu *iommu_table)
{
	return &container_of(iommu_table, struct pt_iommu_table, iommu)
			->bcm2712pt.common;
}

static inline struct pt_iommu *iommu_from_common(struct pt_common *common)
{
	return &container_of(common, struct pt_iommu_table, bcm2712pt.common)
			->iommu;
}

static inline int bcm2712_pt_iommu_set_prot(struct pt_common *common,
					    struct pt_write_attrs *attrs,
					    unsigned int iommu_prot)
{
	attrs->descriptor_bits = 0;
	if (iommu_prot & IOMMU_WRITE)
		attrs->descriptor_bits |= BCM2712PT_WRITE;
	return 0;
}
#define pt_iommu_set_prot bcm2712_pt_iommu_set_prot

static inline int bcm2712_pt_fmt_init(struct pt_iommu_table *fmt_table,
				      const struct pt_iommu_bcm2712_cfg *cfg)
{
	fmt_table->bcm2712pt.bigpage_lg2 = cfg->bigpage_lg2;
	fmt_table->bcm2712pt.superpage_lg2 = cfg->superpage_lg2;

	pt_top_set_level(&fmt_table->bcm2712pt.common, PT_MAX_TOP_LEVEL);
	return 0;
}
#define pt_iommu_fmt_init bcm2712_pt_fmt_init

static inline void
bcm2712pt_iommu_fmt_hw_info(struct pt_iommu_bcm2712 *table,
			    const struct pt_range *top_range,
			    struct pt_iommu_bcm2712_hw_info *info)
{
	info->pt_base = virt_to_phys(top_range->top_table);
	PT_WARN_ON(info->pt_base & ~PT_TOP_PHYS_MASK);
}
#define pt_iommu_fmt_hw_info bcm2712pt_iommu_fmt_hw_info

#if defined(GENERIC_PT_KUNIT)
static const struct pt_iommu_bcm2712_cfg bcm2712_kunit_fmt_cfgs[] = {
	[0] = {
		.common.hw_max_vasz_lg2 = 32,
		.common.hw_max_oasz_lg2 = 37,
		.bigpage_lg2 = 16,
		.superpage_lg2 = 20,
	},
};
#define kunit_fmt_cfgs bcm2712_kunit_fmt_cfgs
enum { KUNIT_FMT_FEATURES = 0 };
#endif

#endif
