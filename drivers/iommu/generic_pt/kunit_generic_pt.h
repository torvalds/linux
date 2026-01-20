/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES
 *
 * Test the format API directly.
 *
 */
#include "kunit_iommu.h"
#include "pt_iter.h"

static void do_map(struct kunit *test, pt_vaddr_t va, pt_oaddr_t pa,
		   pt_vaddr_t len)
{
	struct kunit_iommu_priv *priv = test->priv;
	int ret;

	KUNIT_ASSERT_EQ(test, len, (size_t)len);

	ret = iommu_map(&priv->domain, va, pa, len, IOMMU_READ | IOMMU_WRITE,
			GFP_KERNEL);
	KUNIT_ASSERT_NO_ERRNO_FN(test, "map_pages", ret);
}

#define KUNIT_ASSERT_PT_LOAD(test, pts, entry)             \
	({                                                 \
		pt_load_entry(pts);                        \
		KUNIT_ASSERT_EQ(test, (pts)->type, entry); \
	})

struct check_levels_arg {
	struct kunit *test;
	void *fn_arg;
	void (*fn)(struct kunit *test, struct pt_state *pts, void *arg);
};

static int __check_all_levels(struct pt_range *range, void *arg,
			      unsigned int level, struct pt_table_p *table)
{
	struct pt_state pts = pt_init(range, level, table);
	struct check_levels_arg *chk = arg;
	struct kunit *test = chk->test;
	int ret;

	_pt_iter_first(&pts);


	/*
	 * If we were able to use the full VA space this should always be the
	 * last index in each table.
	 */
	if (!(IS_32BIT && range->max_vasz_lg2 > 32)) {
		if (pt_feature(range->common, PT_FEAT_SIGN_EXTEND) &&
		    pts.level == pts.range->top_level)
			KUNIT_ASSERT_EQ(test, pts.index,
					log2_to_int(range->max_vasz_lg2 - 1 -
						    pt_table_item_lg2sz(&pts)) -
						1);
		else
			KUNIT_ASSERT_EQ(test, pts.index,
					log2_to_int(pt_table_oa_lg2sz(&pts) -
						    pt_table_item_lg2sz(&pts)) -
						1);
	}

	if (pt_can_have_table(&pts)) {
		pt_load_single_entry(&pts);
		KUNIT_ASSERT_EQ(test, pts.type, PT_ENTRY_TABLE);
		ret = pt_descend(&pts, arg, __check_all_levels);
		KUNIT_ASSERT_EQ(test, ret, 0);

		/* Index 0 is used by the test */
		if (IS_32BIT && !pts.index)
			return 0;
		KUNIT_ASSERT_NE(chk->test, pts.index, 0);
	}

	/*
	 * A format should not create a table with only one entry, at least this
	 * test approach won't work.
	 */
	KUNIT_ASSERT_GT(chk->test, pts.end_index, 1);

	/*
	 * For increase top we end up using index 0 for the original top's tree,
	 * so use index 1 for testing instead.
	 */
	pts.index = 0;
	pt_index_to_va(&pts);
	pt_load_single_entry(&pts);
	if (pts.type == PT_ENTRY_TABLE && pts.end_index > 2) {
		pts.index = 1;
		pt_index_to_va(&pts);
	}
	(*chk->fn)(chk->test, &pts, chk->fn_arg);
	return 0;
}

/*
 * Call fn for each level in the table with a pts setup to index 0 in a table
 * for that level. This allows writing tests that run on every level.
 * The test can use every index in the table except the last one.
 */
static void check_all_levels(struct kunit *test,
			     void (*fn)(struct kunit *test,
					struct pt_state *pts, void *arg),
			     void *fn_arg)
{
	struct kunit_iommu_priv *priv = test->priv;
	struct pt_range range = pt_top_range(priv->common);
	struct check_levels_arg chk = {
		.test = test,
		.fn = fn,
		.fn_arg = fn_arg,
	};
	int ret;

	if (pt_feature(priv->common, PT_FEAT_DYNAMIC_TOP) &&
	    priv->common->max_vasz_lg2 > range.max_vasz_lg2)
		range.last_va = fvalog2_set_mod_max(range.va,
						    priv->common->max_vasz_lg2);

	/*
	 * Map a page at the highest VA, this will populate all the levels so we
	 * can then iterate over them. Index 0 will be used for testing.
	 */
	if (IS_32BIT && range.max_vasz_lg2 > 32)
		range.last_va = (u32)range.last_va;
	range.va = range.last_va - (priv->smallest_pgsz - 1);
	do_map(test, range.va, 0, priv->smallest_pgsz);

	range = pt_make_range(priv->common, range.va, range.last_va);
	ret = pt_walk_range(&range, __check_all_levels, &chk);
	KUNIT_ASSERT_EQ(test, ret, 0);
}

static void test_init(struct kunit *test)
{
	struct kunit_iommu_priv *priv = test->priv;

	/* Fixture does the setup */
	KUNIT_ASSERT_NE(test, priv->info.pgsize_bitmap, 0);
}

/*
 * Basic check that the log2_* functions are working, especially at the integer
 * limits.
 */
static void test_bitops(struct kunit *test)
{
	int i;

	KUNIT_ASSERT_EQ(test, fls_t(u32, 0), 0);
	KUNIT_ASSERT_EQ(test, fls_t(u32, 1), 1);
	KUNIT_ASSERT_EQ(test, fls_t(u32, BIT(2)), 3);
	KUNIT_ASSERT_EQ(test, fls_t(u32, U32_MAX), 32);

	KUNIT_ASSERT_EQ(test, fls_t(u64, 0), 0);
	KUNIT_ASSERT_EQ(test, fls_t(u64, 1), 1);
	KUNIT_ASSERT_EQ(test, fls_t(u64, BIT(2)), 3);
	KUNIT_ASSERT_EQ(test, fls_t(u64, U64_MAX), 64);

	KUNIT_ASSERT_EQ(test, ffs_t(u32, 1), 0);
	KUNIT_ASSERT_EQ(test, ffs_t(u32, BIT(2)), 2);
	KUNIT_ASSERT_EQ(test, ffs_t(u32, BIT(31)), 31);

	KUNIT_ASSERT_EQ(test, ffs_t(u64, 1), 0);
	KUNIT_ASSERT_EQ(test, ffs_t(u64, BIT(2)), 2);
	KUNIT_ASSERT_EQ(test, ffs_t(u64, BIT_ULL(63)), 63);

	for (i = 0; i != 31; i++)
		KUNIT_ASSERT_EQ(test, ffz_t(u64, BIT_ULL(i) - 1), i);

	for (i = 0; i != 63; i++)
		KUNIT_ASSERT_EQ(test, ffz_t(u64, BIT_ULL(i) - 1), i);

	for (i = 0; i != 32; i++) {
		u64 val = get_random_u64();

		KUNIT_ASSERT_EQ(test, log2_mod_t(u32, val, ffs_t(u32, val)), 0);
		KUNIT_ASSERT_EQ(test, log2_mod_t(u64, val, ffs_t(u64, val)), 0);

		KUNIT_ASSERT_EQ(test, log2_mod_t(u32, val, ffz_t(u32, val)),
				log2_to_max_int_t(u32, ffz_t(u32, val)));
		KUNIT_ASSERT_EQ(test, log2_mod_t(u64, val, ffz_t(u64, val)),
				log2_to_max_int_t(u64, ffz_t(u64, val)));
	}
}

static unsigned int ref_best_pgsize(pt_vaddr_t pgsz_bitmap, pt_vaddr_t va,
				    pt_vaddr_t last_va, pt_oaddr_t oa)
{
	pt_vaddr_t pgsz_lg2;

	/* Brute force the constraints described in pt_compute_best_pgsize() */
	for (pgsz_lg2 = PT_VADDR_MAX_LG2 - 1; pgsz_lg2 != 0; pgsz_lg2--) {
		if ((pgsz_bitmap & log2_to_int(pgsz_lg2)) &&
		    log2_mod(va, pgsz_lg2) == 0 &&
		    oalog2_mod(oa, pgsz_lg2) == 0 &&
		    va + log2_to_int(pgsz_lg2) - 1 <= last_va &&
		    log2_div_eq(va, va + log2_to_int(pgsz_lg2) - 1, pgsz_lg2) &&
		    oalog2_div_eq(oa, oa + log2_to_int(pgsz_lg2) - 1, pgsz_lg2))
			return pgsz_lg2;
	}
	return 0;
}

/* Check that the bit logic in pt_compute_best_pgsize() works. */
static void test_best_pgsize(struct kunit *test)
{
	unsigned int a_lg2;
	unsigned int b_lg2;
	unsigned int c_lg2;

	/* Try random prefixes with every suffix combination */
	for (a_lg2 = 1; a_lg2 != 10; a_lg2++) {
		for (b_lg2 = 1; b_lg2 != 10; b_lg2++) {
			for (c_lg2 = 1; c_lg2 != 10; c_lg2++) {
				pt_vaddr_t pgsz_bitmap = get_random_u64();
				pt_vaddr_t va = get_random_u64() << a_lg2;
				pt_oaddr_t oa = get_random_u64() << b_lg2;
				pt_vaddr_t last_va = log2_set_mod_max(
					get_random_u64(), c_lg2);

				if (va > last_va)
					swap(va, last_va);
				KUNIT_ASSERT_EQ(
					test,
					pt_compute_best_pgsize(pgsz_bitmap, va,
							       last_va, oa),
					ref_best_pgsize(pgsz_bitmap, va,
							last_va, oa));
			}
		}
	}

	/* 0 prefix, every suffix */
	for (c_lg2 = 1; c_lg2 != PT_VADDR_MAX_LG2 - 1; c_lg2++) {
		pt_vaddr_t pgsz_bitmap = get_random_u64();
		pt_vaddr_t va = 0;
		pt_oaddr_t oa = 0;
		pt_vaddr_t last_va = log2_set_mod_max(0, c_lg2);

		KUNIT_ASSERT_EQ(test,
				pt_compute_best_pgsize(pgsz_bitmap, va, last_va,
						       oa),
				ref_best_pgsize(pgsz_bitmap, va, last_va, oa));
	}

	/* 1's prefix, every suffix */
	for (a_lg2 = 1; a_lg2 != 10; a_lg2++) {
		for (b_lg2 = 1; b_lg2 != 10; b_lg2++) {
			for (c_lg2 = 1; c_lg2 != 10; c_lg2++) {
				pt_vaddr_t pgsz_bitmap = get_random_u64();
				pt_vaddr_t va = PT_VADDR_MAX << a_lg2;
				pt_oaddr_t oa = PT_VADDR_MAX << b_lg2;
				pt_vaddr_t last_va = PT_VADDR_MAX;

				KUNIT_ASSERT_EQ(
					test,
					pt_compute_best_pgsize(pgsz_bitmap, va,
							       last_va, oa),
					ref_best_pgsize(pgsz_bitmap, va,
							last_va, oa));
			}
		}
	}

	/* pgsize_bitmap is always 0 */
	for (a_lg2 = 1; a_lg2 != 10; a_lg2++) {
		for (b_lg2 = 1; b_lg2 != 10; b_lg2++) {
			for (c_lg2 = 1; c_lg2 != 10; c_lg2++) {
				pt_vaddr_t pgsz_bitmap = 0;
				pt_vaddr_t va = get_random_u64() << a_lg2;
				pt_oaddr_t oa = get_random_u64() << b_lg2;
				pt_vaddr_t last_va = log2_set_mod_max(
					get_random_u64(), c_lg2);

				if (va > last_va)
					swap(va, last_va);
				KUNIT_ASSERT_EQ(
					test,
					pt_compute_best_pgsize(pgsz_bitmap, va,
							       last_va, oa),
					0);
			}
		}
	}

	if (sizeof(pt_vaddr_t) <= 4)
		return;

	/* over 32 bit page sizes */
	for (a_lg2 = 32; a_lg2 != 42; a_lg2++) {
		for (b_lg2 = 32; b_lg2 != 42; b_lg2++) {
			for (c_lg2 = 32; c_lg2 != 42; c_lg2++) {
				pt_vaddr_t pgsz_bitmap = get_random_u64();
				pt_vaddr_t va = get_random_u64() << a_lg2;
				pt_oaddr_t oa = get_random_u64() << b_lg2;
				pt_vaddr_t last_va = log2_set_mod_max(
					get_random_u64(), c_lg2);

				if (va > last_va)
					swap(va, last_va);
				KUNIT_ASSERT_EQ(
					test,
					pt_compute_best_pgsize(pgsz_bitmap, va,
							       last_va, oa),
					ref_best_pgsize(pgsz_bitmap, va,
							last_va, oa));
			}
		}
	}
}

/*
 * Check that pt_install_table() and pt_table_pa() match
 */
static void test_lvl_table_ptr(struct kunit *test, struct pt_state *pts,
			       void *arg)
{
	struct kunit_iommu_priv *priv = test->priv;
	pt_oaddr_t paddr =
		log2_set_mod(priv->test_oa, 0, priv->smallest_pgsz_lg2);
	struct pt_write_attrs attrs = {};

	if (!pt_can_have_table(pts))
		return;

	KUNIT_ASSERT_NO_ERRNO_FN(test, "pt_iommu_set_prot",
				 pt_iommu_set_prot(pts->range->common, &attrs,
						   IOMMU_READ));

	pt_load_single_entry(pts);
	KUNIT_ASSERT_PT_LOAD(test, pts, PT_ENTRY_EMPTY);

	KUNIT_ASSERT_TRUE(test, pt_install_table(pts, paddr, &attrs));

	/* A second install should pass because install updates pts->entry. */
	KUNIT_ASSERT_EQ(test, pt_install_table(pts, paddr, &attrs), true);

	KUNIT_ASSERT_PT_LOAD(test, pts, PT_ENTRY_TABLE);
	KUNIT_ASSERT_EQ(test, pt_table_pa(pts), paddr);

	pt_clear_entries(pts, ilog2(1));
	KUNIT_ASSERT_PT_LOAD(test, pts, PT_ENTRY_EMPTY);
}

static void test_table_ptr(struct kunit *test)
{
	check_all_levels(test, test_lvl_table_ptr, NULL);
}

struct lvl_radix_arg {
	pt_vaddr_t vbits;
};

/*
 * Check pt_table_oa_lg2sz() and pt_table_item_lg2sz() they need to decode a
 * continuous list of VA across all the levels that covers the entire advertised
 * VA space.
 */
static void test_lvl_radix(struct kunit *test, struct pt_state *pts, void *arg)
{
	unsigned int table_lg2sz = pt_table_oa_lg2sz(pts);
	unsigned int isz_lg2 = pt_table_item_lg2sz(pts);
	struct lvl_radix_arg *radix = arg;

	/* Every bit below us is decoded */
	KUNIT_ASSERT_EQ(test, log2_set_mod_max(0, isz_lg2), radix->vbits);

	/* We are not decoding bits someone else is */
	KUNIT_ASSERT_EQ(test, log2_div(radix->vbits, isz_lg2), 0);

	/* Can't decode past the pt_vaddr_t size */
	KUNIT_ASSERT_LE(test, table_lg2sz, PT_VADDR_MAX_LG2);
	KUNIT_ASSERT_EQ(test, fvalog2_div(table_lg2sz, PT_MAX_VA_ADDRESS_LG2),
			0);

	radix->vbits = fvalog2_set_mod_max(0, table_lg2sz);
}

static void test_max_va(struct kunit *test)
{
	struct kunit_iommu_priv *priv = test->priv;
	struct pt_range range = pt_top_range(priv->common);

	KUNIT_ASSERT_GE(test, priv->common->max_vasz_lg2, range.max_vasz_lg2);
}

static void test_table_radix(struct kunit *test)
{
	struct kunit_iommu_priv *priv = test->priv;
	struct lvl_radix_arg radix = { .vbits = priv->smallest_pgsz - 1 };
	struct pt_range range;

	check_all_levels(test, test_lvl_radix, &radix);

	range = pt_top_range(priv->common);
	if (range.max_vasz_lg2 == PT_VADDR_MAX_LG2) {
		KUNIT_ASSERT_EQ(test, radix.vbits, PT_VADDR_MAX);
	} else {
		if (!IS_32BIT)
			KUNIT_ASSERT_EQ(test,
					log2_set_mod_max(0, range.max_vasz_lg2),
					radix.vbits);
		KUNIT_ASSERT_EQ(test, log2_div(radix.vbits, range.max_vasz_lg2),
				0);
	}
}

static unsigned int safe_pt_num_items_lg2(const struct pt_state *pts)
{
	struct pt_range top_range = pt_top_range(pts->range->common);
	struct pt_state top_pts = pt_init_top(&top_range);

	/*
	 * Avoid calling pt_num_items_lg2() on the top, instead we can derive
	 * the size of the top table from the top range.
	 */
	if (pts->level == top_range.top_level)
		return ilog2(pt_range_to_end_index(&top_pts));
	return pt_num_items_lg2(pts);
}

static void test_lvl_possible_sizes(struct kunit *test, struct pt_state *pts,
				    void *arg)
{
	unsigned int num_items_lg2 = safe_pt_num_items_lg2(pts);
	pt_vaddr_t pgsize_bitmap = pt_possible_sizes(pts);
	unsigned int isz_lg2 = pt_table_item_lg2sz(pts);

	if (!pt_can_have_leaf(pts)) {
		KUNIT_ASSERT_EQ(test, pgsize_bitmap, 0);
		return;
	}

	/* No bits for sizes that would be outside this table */
	KUNIT_ASSERT_EQ(test, log2_mod(pgsize_bitmap, isz_lg2), 0);
	KUNIT_ASSERT_EQ(
		test, fvalog2_div(pgsize_bitmap, num_items_lg2 + isz_lg2), 0);

	/*
	 * Non contiguous must be supported. AMDv1 has a HW bug where it does
	 * not support it on one of the levels.
	 */
	if ((u64)pgsize_bitmap != 0xff0000000000ULL ||
	    strcmp(__stringify(PTPFX_RAW), "amdv1") != 0)
		KUNIT_ASSERT_TRUE(test, pgsize_bitmap & log2_to_int(isz_lg2));
	else
		KUNIT_ASSERT_NE(test, pgsize_bitmap, 0);

	/* A contiguous entry should not span the whole table */
	if (num_items_lg2 + isz_lg2 != PT_VADDR_MAX_LG2)
		KUNIT_ASSERT_FALSE(
			test,
			pgsize_bitmap & log2_to_int(num_items_lg2 + isz_lg2));
}

static void test_entry_possible_sizes(struct kunit *test)
{
	check_all_levels(test, test_lvl_possible_sizes, NULL);
}

static void sweep_all_pgsizes(struct kunit *test, struct pt_state *pts,
			      struct pt_write_attrs *attrs,
			      pt_oaddr_t test_oaddr)
{
	pt_vaddr_t pgsize_bitmap = pt_possible_sizes(pts);
	unsigned int isz_lg2 = pt_table_item_lg2sz(pts);
	unsigned int len_lg2;

	if (pts->index != 0)
		return;

	for (len_lg2 = 0; len_lg2 < PT_VADDR_MAX_LG2 - 1; len_lg2++) {
		struct pt_state sub_pts = *pts;
		pt_oaddr_t oaddr;

		if (!(pgsize_bitmap & log2_to_int(len_lg2)))
			continue;

		oaddr = log2_set_mod(test_oaddr, 0, len_lg2);
		pt_install_leaf_entry(pts, oaddr, len_lg2, attrs);
		/* Verify that every contiguous item translates correctly */
		for (sub_pts.index = 0;
		     sub_pts.index != log2_to_int(len_lg2 - isz_lg2);
		     sub_pts.index++) {
			KUNIT_ASSERT_PT_LOAD(test, &sub_pts, PT_ENTRY_OA);
			KUNIT_ASSERT_EQ(test, pt_item_oa(&sub_pts),
					oaddr + sub_pts.index *
							oalog2_mul(1, isz_lg2));
			KUNIT_ASSERT_EQ(test, pt_entry_oa(&sub_pts), oaddr);
			KUNIT_ASSERT_EQ(test, pt_entry_num_contig_lg2(&sub_pts),
					len_lg2 - isz_lg2);
		}

		pt_clear_entries(pts, len_lg2 - isz_lg2);
		KUNIT_ASSERT_PT_LOAD(test, pts, PT_ENTRY_EMPTY);
	}
}

/*
 * Check that pt_install_leaf_entry() and pt_entry_oa() match.
 * Check that pt_clear_entries() works.
 */
static void test_lvl_entry_oa(struct kunit *test, struct pt_state *pts,
			      void *arg)
{
	unsigned int max_oa_lg2 = pts->range->common->max_oasz_lg2;
	struct kunit_iommu_priv *priv = test->priv;
	struct pt_write_attrs attrs = {};

	if (!pt_can_have_leaf(pts))
		return;

	KUNIT_ASSERT_NO_ERRNO_FN(test, "pt_iommu_set_prot",
				 pt_iommu_set_prot(pts->range->common, &attrs,
						   IOMMU_READ));

	sweep_all_pgsizes(test, pts, &attrs, priv->test_oa);

	/* Check that the table can store the boundary OAs */
	sweep_all_pgsizes(test, pts, &attrs, 0);
	if (max_oa_lg2 == PT_OADDR_MAX_LG2)
		sweep_all_pgsizes(test, pts, &attrs, PT_OADDR_MAX);
	else
		sweep_all_pgsizes(test, pts, &attrs,
				  oalog2_to_max_int(max_oa_lg2));
}

static void test_entry_oa(struct kunit *test)
{
	check_all_levels(test, test_lvl_entry_oa, NULL);
}

/* Test pt_attr_from_entry() */
static void test_lvl_attr_from_entry(struct kunit *test, struct pt_state *pts,
				     void *arg)
{
	pt_vaddr_t pgsize_bitmap = pt_possible_sizes(pts);
	unsigned int isz_lg2 = pt_table_item_lg2sz(pts);
	struct kunit_iommu_priv *priv = test->priv;
	unsigned int len_lg2;
	unsigned int prot;

	if (!pt_can_have_leaf(pts))
		return;

	for (len_lg2 = 0; len_lg2 < PT_VADDR_MAX_LG2; len_lg2++) {
		if (!(pgsize_bitmap & log2_to_int(len_lg2)))
			continue;
		for (prot = 0; prot <= (IOMMU_READ | IOMMU_WRITE | IOMMU_CACHE |
					IOMMU_NOEXEC | IOMMU_MMIO);
		     prot++) {
			pt_oaddr_t oaddr;
			struct pt_write_attrs attrs = {};
			u64 good_entry;

			/*
			 * If the format doesn't support this combination of
			 * prot bits skip it
			 */
			if (pt_iommu_set_prot(pts->range->common, &attrs,
					      prot)) {
				/* But RW has to be supported */
				KUNIT_ASSERT_NE(test, prot,
						IOMMU_READ | IOMMU_WRITE);
				continue;
			}

			oaddr = log2_set_mod(priv->test_oa, 0, len_lg2);
			pt_install_leaf_entry(pts, oaddr, len_lg2, &attrs);
			KUNIT_ASSERT_PT_LOAD(test, pts, PT_ENTRY_OA);

			good_entry = pts->entry;

			memset(&attrs, 0, sizeof(attrs));
			pt_attr_from_entry(pts, &attrs);

			pt_clear_entries(pts, len_lg2 - isz_lg2);
			KUNIT_ASSERT_PT_LOAD(test, pts, PT_ENTRY_EMPTY);

			pt_install_leaf_entry(pts, oaddr, len_lg2, &attrs);
			KUNIT_ASSERT_PT_LOAD(test, pts, PT_ENTRY_OA);

			/*
			 * The descriptor produced by pt_attr_from_entry()
			 * produce an identical entry value when re-written
			 */
			KUNIT_ASSERT_EQ(test, good_entry, pts->entry);

			pt_clear_entries(pts, len_lg2 - isz_lg2);
		}
	}
}

static void test_attr_from_entry(struct kunit *test)
{
	check_all_levels(test, test_lvl_attr_from_entry, NULL);
}

static void test_lvl_dirty(struct kunit *test, struct pt_state *pts, void *arg)
{
	pt_vaddr_t pgsize_bitmap = pt_possible_sizes(pts);
	unsigned int isz_lg2 = pt_table_item_lg2sz(pts);
	struct kunit_iommu_priv *priv = test->priv;
	unsigned int start_idx = pts->index;
	struct pt_write_attrs attrs = {};
	unsigned int len_lg2;

	if (!pt_can_have_leaf(pts))
		return;

	KUNIT_ASSERT_NO_ERRNO_FN(test, "pt_iommu_set_prot",
				 pt_iommu_set_prot(pts->range->common, &attrs,
						   IOMMU_READ | IOMMU_WRITE));

	for (len_lg2 = 0; len_lg2 < PT_VADDR_MAX_LG2; len_lg2++) {
		pt_oaddr_t oaddr;
		unsigned int i;

		if (!(pgsize_bitmap & log2_to_int(len_lg2)))
			continue;

		oaddr = log2_set_mod(priv->test_oa, 0, len_lg2);
		pt_install_leaf_entry(pts, oaddr, len_lg2, &attrs);
		KUNIT_ASSERT_PT_LOAD(test, pts, PT_ENTRY_OA);

		pt_load_entry(pts);
		pt_entry_make_write_clean(pts);
		pt_load_entry(pts);
		KUNIT_ASSERT_FALSE(test, pt_entry_is_write_dirty(pts));

		for (i = 0; i != log2_to_int(len_lg2 - isz_lg2); i++) {
			/* dirty every contiguous entry */
			pts->index = start_idx + i;
			pt_load_entry(pts);
			KUNIT_ASSERT_TRUE(test, pt_entry_make_write_dirty(pts));
			pts->index = start_idx;
			pt_load_entry(pts);
			KUNIT_ASSERT_TRUE(test, pt_entry_is_write_dirty(pts));

			pt_entry_make_write_clean(pts);
			pt_load_entry(pts);
			KUNIT_ASSERT_FALSE(test, pt_entry_is_write_dirty(pts));
		}

		pt_clear_entries(pts, len_lg2 - isz_lg2);
	}
}

static __maybe_unused void test_dirty(struct kunit *test)
{
	struct kunit_iommu_priv *priv = test->priv;

	if (!pt_dirty_supported(priv->common))
		kunit_skip(test,
			   "Page table features do not support dirty tracking");

	check_all_levels(test, test_lvl_dirty, NULL);
}

static void test_lvl_sw_bit_leaf(struct kunit *test, struct pt_state *pts,
				 void *arg)
{
	struct kunit_iommu_priv *priv = test->priv;
	pt_vaddr_t pgsize_bitmap = pt_possible_sizes(pts);
	unsigned int isz_lg2 = pt_table_item_lg2sz(pts);
	struct pt_write_attrs attrs = {};
	unsigned int len_lg2;

	if (!pt_can_have_leaf(pts))
		return;
	if (pts->index != 0)
		return;

	KUNIT_ASSERT_NO_ERRNO_FN(test, "pt_iommu_set_prot",
				 pt_iommu_set_prot(pts->range->common, &attrs,
						   IOMMU_READ));

	for (len_lg2 = 0; len_lg2 < PT_VADDR_MAX_LG2 - 1; len_lg2++) {
		pt_oaddr_t paddr = log2_set_mod(priv->test_oa, 0, len_lg2);
		struct pt_write_attrs new_attrs = {};
		unsigned int bitnr;

		if (!(pgsize_bitmap & log2_to_int(len_lg2)))
			continue;

		pt_install_leaf_entry(pts, paddr, len_lg2, &attrs);

		for (bitnr = 0; bitnr <= pt_max_sw_bit(pts->range->common);
		     bitnr++)
			KUNIT_ASSERT_FALSE(test,
					   pt_test_sw_bit_acquire(pts, bitnr));

		for (bitnr = 0; bitnr <= pt_max_sw_bit(pts->range->common);
		     bitnr++) {
			KUNIT_ASSERT_FALSE(test,
					   pt_test_sw_bit_acquire(pts, bitnr));
			pt_set_sw_bit_release(pts, bitnr);
			KUNIT_ASSERT_TRUE(test,
					  pt_test_sw_bit_acquire(pts, bitnr));
		}

		for (bitnr = 0; bitnr <= pt_max_sw_bit(pts->range->common);
		     bitnr++)
			KUNIT_ASSERT_TRUE(test,
					  pt_test_sw_bit_acquire(pts, bitnr));

		KUNIT_ASSERT_EQ(test, pt_item_oa(pts), paddr);

		/* SW bits didn't leak into the attrs */
		pt_attr_from_entry(pts, &new_attrs);
		KUNIT_ASSERT_MEMEQ(test, &new_attrs, &attrs, sizeof(attrs));

		pt_clear_entries(pts, len_lg2 - isz_lg2);
		KUNIT_ASSERT_PT_LOAD(test, pts, PT_ENTRY_EMPTY);
	}
}

static __maybe_unused void test_sw_bit_leaf(struct kunit *test)
{
	check_all_levels(test, test_lvl_sw_bit_leaf, NULL);
}

static void test_lvl_sw_bit_table(struct kunit *test, struct pt_state *pts,
				  void *arg)
{
	struct kunit_iommu_priv *priv = test->priv;
	struct pt_write_attrs attrs = {};
	pt_oaddr_t paddr =
		log2_set_mod(priv->test_oa, 0, priv->smallest_pgsz_lg2);
	unsigned int bitnr;

	if (!pt_can_have_leaf(pts))
		return;
	if (pts->index != 0)
		return;

	KUNIT_ASSERT_NO_ERRNO_FN(test, "pt_iommu_set_prot",
				 pt_iommu_set_prot(pts->range->common, &attrs,
						   IOMMU_READ));

	KUNIT_ASSERT_TRUE(test, pt_install_table(pts, paddr, &attrs));

	for (bitnr = 0; bitnr <= pt_max_sw_bit(pts->range->common); bitnr++)
		KUNIT_ASSERT_FALSE(test, pt_test_sw_bit_acquire(pts, bitnr));

	for (bitnr = 0; bitnr <= pt_max_sw_bit(pts->range->common); bitnr++) {
		KUNIT_ASSERT_FALSE(test, pt_test_sw_bit_acquire(pts, bitnr));
		pt_set_sw_bit_release(pts, bitnr);
		KUNIT_ASSERT_TRUE(test, pt_test_sw_bit_acquire(pts, bitnr));
	}

	for (bitnr = 0; bitnr <= pt_max_sw_bit(pts->range->common); bitnr++)
		KUNIT_ASSERT_TRUE(test, pt_test_sw_bit_acquire(pts, bitnr));

	KUNIT_ASSERT_EQ(test, pt_table_pa(pts), paddr);

	pt_clear_entries(pts, ilog2(1));
	KUNIT_ASSERT_PT_LOAD(test, pts, PT_ENTRY_EMPTY);
}

static __maybe_unused void test_sw_bit_table(struct kunit *test)
{
	check_all_levels(test, test_lvl_sw_bit_table, NULL);
}

static struct kunit_case generic_pt_test_cases[] = {
	KUNIT_CASE_FMT(test_init),
	KUNIT_CASE_FMT(test_bitops),
	KUNIT_CASE_FMT(test_best_pgsize),
	KUNIT_CASE_FMT(test_table_ptr),
	KUNIT_CASE_FMT(test_max_va),
	KUNIT_CASE_FMT(test_table_radix),
	KUNIT_CASE_FMT(test_entry_possible_sizes),
	KUNIT_CASE_FMT(test_entry_oa),
	KUNIT_CASE_FMT(test_attr_from_entry),
#ifdef pt_entry_is_write_dirty
	KUNIT_CASE_FMT(test_dirty),
#endif
#ifdef pt_sw_bit
	KUNIT_CASE_FMT(test_sw_bit_leaf),
	KUNIT_CASE_FMT(test_sw_bit_table),
#endif
	{},
};

static int pt_kunit_generic_pt_init(struct kunit *test)
{
	struct kunit_iommu_priv *priv;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	ret = pt_kunit_priv_init(test, priv);
	if (ret) {
		kunit_kfree(test, priv);
		return ret;
	}
	test->priv = priv;
	return 0;
}

static void pt_kunit_generic_pt_exit(struct kunit *test)
{
	struct kunit_iommu_priv *priv = test->priv;

	if (!test->priv)
		return;

	pt_iommu_deinit(priv->iommu);
	kunit_kfree(test, test->priv);
}

static struct kunit_suite NS(generic_pt_suite) = {
	.name = __stringify(NS(fmt_test)),
	.init = pt_kunit_generic_pt_init,
	.exit = pt_kunit_generic_pt_exit,
	.test_cases = generic_pt_test_cases,
};
kunit_test_suites(&NS(generic_pt_suite));
