/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES
 *
 * Default definitions for formats that don't define these functions.
 */
#ifndef __GENERIC_PT_PT_FMT_DEFAULTS_H
#define __GENERIC_PT_PT_FMT_DEFAULTS_H

#include "pt_defs.h"
#include <linux/log2.h>

/* Header self-compile default defines */
#ifndef pt_load_entry_raw
#include "fmt/amdv1.h"
#endif

/*
 * The format must provide PT_GRANULE_LG2SZ, PT_TABLEMEM_LG2SZ, and
 * PT_ITEM_WORD_SIZE. They must be the same at every level excluding the top.
 */
#ifndef pt_table_item_lg2sz
static inline unsigned int pt_table_item_lg2sz(const struct pt_state *pts)
{
	return PT_GRANULE_LG2SZ +
	       (PT_TABLEMEM_LG2SZ - ilog2(PT_ITEM_WORD_SIZE)) * pts->level;
}
#endif

#ifndef pt_pgsz_lg2_to_level
static inline unsigned int pt_pgsz_lg2_to_level(struct pt_common *common,
						unsigned int pgsize_lg2)
{
	return ((unsigned int)(pgsize_lg2 - PT_GRANULE_LG2SZ)) /
	       (PT_TABLEMEM_LG2SZ - ilog2(PT_ITEM_WORD_SIZE));
}
#endif

/*
 * If not supplied by the format then contiguous pages are not supported.
 *
 * If contiguous pages are supported then the format must also provide
 * pt_contig_count_lg2() if it supports a single contiguous size per level,
 * or pt_possible_sizes() if it supports multiple sizes per level.
 */
#ifndef pt_entry_num_contig_lg2
static inline unsigned int pt_entry_num_contig_lg2(const struct pt_state *pts)
{
	return ilog2(1);
}

/*
 * Return the number of contiguous OA items forming an entry at this table level
 */
static inline unsigned short pt_contig_count_lg2(const struct pt_state *pts)
{
	return ilog2(1);
}
#endif

/* If not supplied by the format then dirty tracking is not supported */
#ifndef pt_entry_is_write_dirty
static inline bool pt_entry_is_write_dirty(const struct pt_state *pts)
{
	return false;
}

static inline void pt_entry_make_write_clean(struct pt_state *pts)
{
}

static inline bool pt_dirty_supported(struct pt_common *common)
{
	return false;
}
#else
/* If not supplied then dirty tracking is always enabled */
#ifndef pt_dirty_supported
static inline bool pt_dirty_supported(struct pt_common *common)
{
	return true;
}
#endif
#endif

#ifndef pt_entry_make_write_dirty
static inline bool pt_entry_make_write_dirty(struct pt_state *pts)
{
	return false;
}
#endif

/*
 * Format supplies either:
 *   pt_entry_oa - OA is at the start of a contiguous entry
 * or
 *   pt_item_oa  - OA is adjusted for every item in a contiguous entry
 *
 * Build the missing one
 *
 * The internal helper _pt_entry_oa_fast() allows generating
 * an efficient pt_entry_oa_exact(), it doesn't care which
 * option is selected.
 */
#ifdef pt_entry_oa
static inline pt_oaddr_t pt_item_oa(const struct pt_state *pts)
{
	return pt_entry_oa(pts) |
	       log2_mul(pts->index, pt_table_item_lg2sz(pts));
}
#define _pt_entry_oa_fast pt_entry_oa
#endif

#ifdef pt_item_oa
static inline pt_oaddr_t pt_entry_oa(const struct pt_state *pts)
{
	return log2_set_mod(pt_item_oa(pts), 0,
			    pt_entry_num_contig_lg2(pts) +
				    pt_table_item_lg2sz(pts));
}
#define _pt_entry_oa_fast pt_item_oa
#endif

/*
 * If not supplied by the format then use the constant
 * PT_MAX_OUTPUT_ADDRESS_LG2.
 */
#ifndef pt_max_oa_lg2
static inline unsigned int
pt_max_oa_lg2(const struct pt_common *common)
{
	return PT_MAX_OUTPUT_ADDRESS_LG2;
}
#endif

#ifndef pt_has_system_page_size
static inline bool pt_has_system_page_size(const struct pt_common *common)
{
	return PT_GRANULE_LG2SZ == PAGE_SHIFT;
}
#endif

/*
 * If not supplied by the format then assume only one contiguous size determined
 * by pt_contig_count_lg2()
 */
#ifndef pt_possible_sizes
static inline unsigned short pt_contig_count_lg2(const struct pt_state *pts);

/* Return a bitmap of possible leaf page sizes at this level */
static inline pt_vaddr_t pt_possible_sizes(const struct pt_state *pts)
{
	unsigned int isz_lg2 = pt_table_item_lg2sz(pts);

	if (!pt_can_have_leaf(pts))
		return 0;
	return log2_to_int(isz_lg2) |
	       log2_to_int(pt_contig_count_lg2(pts) + isz_lg2);
}
#endif

/* If not supplied by the format then use 0. */
#ifndef pt_full_va_prefix
static inline pt_vaddr_t pt_full_va_prefix(const struct pt_common *common)
{
	return 0;
}
#endif

/* If not supplied by the format then zero fill using PT_ITEM_WORD_SIZE */
#ifndef pt_clear_entries
static inline void pt_clear_entries64(struct pt_state *pts,
				      unsigned int num_contig_lg2)
{
	u64 *tablep = pt_cur_table(pts, u64) + pts->index;
	u64 *end = tablep + log2_to_int(num_contig_lg2);

	PT_WARN_ON(log2_mod(pts->index, num_contig_lg2));
	for (; tablep != end; tablep++)
		WRITE_ONCE(*tablep, 0);
}

static inline void pt_clear_entries32(struct pt_state *pts,
				      unsigned int num_contig_lg2)
{
	u32 *tablep = pt_cur_table(pts, u32) + pts->index;
	u32 *end = tablep + log2_to_int(num_contig_lg2);

	PT_WARN_ON(log2_mod(pts->index, num_contig_lg2));
	for (; tablep != end; tablep++)
		WRITE_ONCE(*tablep, 0);
}

static inline void pt_clear_entries(struct pt_state *pts,
				    unsigned int num_contig_lg2)
{
	if (PT_ITEM_WORD_SIZE == sizeof(u32))
		pt_clear_entries32(pts, num_contig_lg2);
	else
		pt_clear_entries64(pts, num_contig_lg2);
}
#define pt_clear_entries pt_clear_entries
#endif

/* If not supplied then SW bits are not supported */
#ifdef pt_sw_bit
static inline bool pt_test_sw_bit_acquire(struct pt_state *pts,
					  unsigned int bitnr)
{
	/* Acquire, pairs with pt_set_sw_bit_release() */
	smp_mb();
	/* For a contiguous entry the sw bit is only stored in the first item. */
	return pts->entry & pt_sw_bit(bitnr);
}
#define pt_test_sw_bit_acquire pt_test_sw_bit_acquire

static inline void pt_set_sw_bit_release(struct pt_state *pts,
					 unsigned int bitnr)
{
#if !IS_ENABLED(CONFIG_GENERIC_ATOMIC64)
	if (PT_ITEM_WORD_SIZE == sizeof(u64)) {
		u64 *entryp = pt_cur_table(pts, u64) + pts->index;
		u64 old_entry = pts->entry;
		u64 new_entry;

		do {
			new_entry = old_entry | pt_sw_bit(bitnr);
		} while (!try_cmpxchg64_release(entryp, &old_entry, new_entry));
		pts->entry = new_entry;
		return;
	}
#endif
	if (PT_ITEM_WORD_SIZE == sizeof(u32)) {
		u32 *entryp = pt_cur_table(pts, u32) + pts->index;
		u32 old_entry = pts->entry;
		u32 new_entry;

		do {
			new_entry = old_entry | pt_sw_bit(bitnr);
		} while (!try_cmpxchg_release(entryp, &old_entry, new_entry));
		pts->entry = new_entry;
	} else
		BUILD_BUG();
}
#define pt_set_sw_bit_release pt_set_sw_bit_release
#else
static inline unsigned int pt_max_sw_bit(struct pt_common *common)
{
	return 0;
}

extern void __pt_no_sw_bit(void);
static inline bool pt_test_sw_bit_acquire(struct pt_state *pts,
					  unsigned int bitnr)
{
	__pt_no_sw_bit();
	return false;
}

static inline void pt_set_sw_bit_release(struct pt_state *pts,
					 unsigned int bitnr)
{
	__pt_no_sw_bit();
}
#endif

/*
 * Format can call in the pt_install_leaf_entry() to check the arguments are all
 * aligned correctly.
 */
static inline bool pt_check_install_leaf_args(struct pt_state *pts,
					      pt_oaddr_t oa,
					      unsigned int oasz_lg2)
{
	unsigned int isz_lg2 = pt_table_item_lg2sz(pts);

	if (PT_WARN_ON(oalog2_mod(oa, oasz_lg2)))
		return false;

#ifdef pt_possible_sizes
	if (PT_WARN_ON(isz_lg2 > oasz_lg2 ||
		       oasz_lg2 > isz_lg2 + pt_num_items_lg2(pts)))
		return false;
#else
	if (PT_WARN_ON(oasz_lg2 != isz_lg2 &&
		       oasz_lg2 != isz_lg2 + pt_contig_count_lg2(pts)))
		return false;
#endif

	if (PT_WARN_ON(oalog2_mod(pts->index, oasz_lg2 - isz_lg2)))
		return false;
	return true;
}

#endif
