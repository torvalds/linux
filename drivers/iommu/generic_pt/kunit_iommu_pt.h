/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES
 */
#include "kunit_iommu.h"
#include "pt_iter.h"
#include <linux/generic_pt/iommu.h>
#include <linux/iommu.h>

static void do_map(struct kunit *test, pt_vaddr_t va, pt_oaddr_t pa,
		   pt_vaddr_t len);

struct count_valids {
	u64 per_size[PT_VADDR_MAX_LG2];
};

static int __count_valids(struct pt_range *range, void *arg, unsigned int level,
			  struct pt_table_p *table)
{
	struct pt_state pts = pt_init(range, level, table);
	struct count_valids *valids = arg;

	for_each_pt_level_entry(&pts) {
		if (pts.type == PT_ENTRY_TABLE) {
			pt_descend(&pts, arg, __count_valids);
			continue;
		}
		if (pts.type == PT_ENTRY_OA) {
			valids->per_size[pt_entry_oa_lg2sz(&pts)]++;
			continue;
		}
	}
	return 0;
}

/*
 * Number of valid table entries. This counts contiguous entries as a single
 * valid.
 */
static unsigned int count_valids(struct kunit *test)
{
	struct kunit_iommu_priv *priv = test->priv;
	struct pt_range range = pt_top_range(priv->common);
	struct count_valids valids = {};
	u64 total = 0;
	unsigned int i;

	KUNIT_ASSERT_NO_ERRNO(test,
			      pt_walk_range(&range, __count_valids, &valids));

	for (i = 0; i != ARRAY_SIZE(valids.per_size); i++)
		total += valids.per_size[i];
	return total;
}

/* Only a single page size is present, count the number of valid entries */
static unsigned int count_valids_single(struct kunit *test, pt_vaddr_t pgsz)
{
	struct kunit_iommu_priv *priv = test->priv;
	struct pt_range range = pt_top_range(priv->common);
	struct count_valids valids = {};
	u64 total = 0;
	unsigned int i;

	KUNIT_ASSERT_NO_ERRNO(test,
			      pt_walk_range(&range, __count_valids, &valids));

	for (i = 0; i != ARRAY_SIZE(valids.per_size); i++) {
		if ((1ULL << i) == pgsz)
			total = valids.per_size[i];
		else
			KUNIT_ASSERT_EQ(test, valids.per_size[i], 0);
	}
	return total;
}

static void do_unmap(struct kunit *test, pt_vaddr_t va, pt_vaddr_t len)
{
	struct kunit_iommu_priv *priv = test->priv;
	size_t ret;

	ret = iommu_unmap(&priv->domain, va, len);
	KUNIT_ASSERT_EQ(test, ret, len);
}

static void check_iova(struct kunit *test, pt_vaddr_t va, pt_oaddr_t pa,
		       pt_vaddr_t len)
{
	struct kunit_iommu_priv *priv = test->priv;
	pt_vaddr_t pfn = log2_div(va, priv->smallest_pgsz_lg2);
	pt_vaddr_t end_pfn = pfn + log2_div(len, priv->smallest_pgsz_lg2);

	for (; pfn != end_pfn; pfn++) {
		phys_addr_t res = iommu_iova_to_phys(&priv->domain,
						     pfn * priv->smallest_pgsz);

		KUNIT_ASSERT_EQ(test, res, (phys_addr_t)pa);
		if (res != pa)
			break;
		pa += priv->smallest_pgsz;
	}
}

static void test_increase_level(struct kunit *test)
{
	struct kunit_iommu_priv *priv = test->priv;
	struct pt_common *common = priv->common;

	if (!pt_feature(common, PT_FEAT_DYNAMIC_TOP))
		kunit_skip(test, "PT_FEAT_DYNAMIC_TOP not set for this format");

	if (IS_32BIT)
		kunit_skip(test, "Unable to test on 32bit");

	KUNIT_ASSERT_GT(test, common->max_vasz_lg2,
			pt_top_range(common).max_vasz_lg2);

	/* Add every possible level to the max */
	while (common->max_vasz_lg2 != pt_top_range(common).max_vasz_lg2) {
		struct pt_range top_range = pt_top_range(common);

		if (top_range.va == 0)
			do_map(test, top_range.last_va + 1, 0,
			       priv->smallest_pgsz);
		else
			do_map(test, top_range.va - priv->smallest_pgsz, 0,
			       priv->smallest_pgsz);

		KUNIT_ASSERT_EQ(test, pt_top_range(common).top_level,
				top_range.top_level + 1);
		KUNIT_ASSERT_GE(test, common->max_vasz_lg2,
				pt_top_range(common).max_vasz_lg2);
	}
}

static void test_map_simple(struct kunit *test)
{
	struct kunit_iommu_priv *priv = test->priv;
	struct pt_range range = pt_top_range(priv->common);
	struct count_valids valids = {};
	pt_vaddr_t pgsize_bitmap = priv->safe_pgsize_bitmap;
	unsigned int pgsz_lg2;
	pt_vaddr_t cur_va;

	/* Map every reported page size */
	cur_va = range.va + priv->smallest_pgsz * 256;
	for (pgsz_lg2 = 0; pgsz_lg2 != PT_VADDR_MAX_LG2; pgsz_lg2++) {
		pt_oaddr_t paddr = log2_set_mod(priv->test_oa, 0, pgsz_lg2);
		u64 len = log2_to_int(pgsz_lg2);

		if (!(pgsize_bitmap & len))
			continue;

		cur_va = ALIGN(cur_va, len);
		do_map(test, cur_va, paddr, len);
		if (len <= SZ_2G)
			check_iova(test, cur_va, paddr, len);
		cur_va += len;
	}

	/* The read interface reports that every page size was created */
	range = pt_top_range(priv->common);
	KUNIT_ASSERT_NO_ERRNO(test,
			      pt_walk_range(&range, __count_valids, &valids));
	for (pgsz_lg2 = 0; pgsz_lg2 != PT_VADDR_MAX_LG2; pgsz_lg2++) {
		if (pgsize_bitmap & (1ULL << pgsz_lg2))
			KUNIT_ASSERT_EQ(test, valids.per_size[pgsz_lg2], 1);
		else
			KUNIT_ASSERT_EQ(test, valids.per_size[pgsz_lg2], 0);
	}

	/* Unmap works */
	range = pt_top_range(priv->common);
	cur_va = range.va + priv->smallest_pgsz * 256;
	for (pgsz_lg2 = 0; pgsz_lg2 != PT_VADDR_MAX_LG2; pgsz_lg2++) {
		u64 len = log2_to_int(pgsz_lg2);

		if (!(pgsize_bitmap & len))
			continue;
		cur_va = ALIGN(cur_va, len);
		do_unmap(test, cur_va, len);
		cur_va += len;
	}
	KUNIT_ASSERT_EQ(test, count_valids(test), 0);
}

/*
 * Test to convert a table pointer into an OA by mapping something small,
 * unmapping it so as to leave behind a table pointer, then mapping something
 * larger that will convert the table into an OA.
 */
static void test_map_table_to_oa(struct kunit *test)
{
	struct kunit_iommu_priv *priv = test->priv;
	pt_vaddr_t limited_pgbitmap =
		priv->info.pgsize_bitmap % (IS_32BIT ? SZ_2G : SZ_16G);
	struct pt_range range = pt_top_range(priv->common);
	unsigned int pgsz_lg2;
	pt_vaddr_t max_pgsize;
	pt_vaddr_t cur_va;

	max_pgsize = 1ULL << (vafls(limited_pgbitmap) - 1);
	KUNIT_ASSERT_TRUE(test, priv->info.pgsize_bitmap & max_pgsize);

	for (pgsz_lg2 = 0; pgsz_lg2 != PT_VADDR_MAX_LG2; pgsz_lg2++) {
		pt_oaddr_t paddr = log2_set_mod(priv->test_oa, 0, pgsz_lg2);
		u64 len = log2_to_int(pgsz_lg2);
		pt_vaddr_t offset;

		if (!(priv->info.pgsize_bitmap & len))
			continue;
		if (len > max_pgsize)
			break;

		cur_va = ALIGN(range.va + priv->smallest_pgsz * 256,
			       max_pgsize);
		for (offset = 0; offset != max_pgsize; offset += len)
			do_map(test, cur_va + offset, paddr + offset, len);
		check_iova(test, cur_va, paddr, max_pgsize);
		KUNIT_ASSERT_EQ(test, count_valids_single(test, len),
				log2_div(max_pgsize, pgsz_lg2));

		if (len == max_pgsize) {
			do_unmap(test, cur_va, max_pgsize);
		} else {
			do_unmap(test, cur_va, max_pgsize / 2);
			for (offset = max_pgsize / 2; offset != max_pgsize;
			     offset += len)
				do_unmap(test, cur_va + offset, len);
		}

		KUNIT_ASSERT_EQ(test, count_valids(test), 0);
	}
}

/*
 * Test unmapping a small page at the start of a large page. This always unmaps
 * the large page.
 */
static void test_unmap_split(struct kunit *test)
{
	struct kunit_iommu_priv *priv = test->priv;
	struct pt_range top_range = pt_top_range(priv->common);
	pt_vaddr_t pgsize_bitmap = priv->safe_pgsize_bitmap;
	unsigned int pgsz_lg2;
	unsigned int count = 0;

	for (pgsz_lg2 = 0; pgsz_lg2 != PT_VADDR_MAX_LG2; pgsz_lg2++) {
		pt_vaddr_t base_len = log2_to_int(pgsz_lg2);
		unsigned int next_pgsz_lg2;

		if (!(pgsize_bitmap & base_len))
			continue;

		for (next_pgsz_lg2 = pgsz_lg2 + 1;
		     next_pgsz_lg2 != PT_VADDR_MAX_LG2; next_pgsz_lg2++) {
			pt_vaddr_t next_len = log2_to_int(next_pgsz_lg2);
			pt_vaddr_t vaddr = top_range.va;
			pt_oaddr_t paddr = 0;
			size_t gnmapped;

			if (!(pgsize_bitmap & next_len))
				continue;

			do_map(test, vaddr, paddr, next_len);
			gnmapped = iommu_unmap(&priv->domain, vaddr, base_len);
			KUNIT_ASSERT_EQ(test, gnmapped, next_len);

			/* Make sure unmap doesn't keep going */
			do_map(test, vaddr, paddr, next_len);
			do_map(test, vaddr + next_len, paddr, next_len);
			gnmapped = iommu_unmap(&priv->domain, vaddr, base_len);
			KUNIT_ASSERT_EQ(test, gnmapped, next_len);
			gnmapped = iommu_unmap(&priv->domain, vaddr + next_len,
					       next_len);
			KUNIT_ASSERT_EQ(test, gnmapped, next_len);

			count++;
		}
	}

	if (count == 0)
		kunit_skip(test, "Test needs two page sizes");
}

static void unmap_collisions(struct kunit *test, struct maple_tree *mt,
			     pt_vaddr_t start, pt_vaddr_t last)
{
	struct kunit_iommu_priv *priv = test->priv;
	MA_STATE(mas, mt, start, last);
	void *entry;

	mtree_lock(mt);
	mas_for_each(&mas, entry, last) {
		pt_vaddr_t mas_start = mas.index;
		pt_vaddr_t len = (mas.last - mas_start) + 1;
		pt_oaddr_t paddr;

		mas_erase(&mas);
		mas_pause(&mas);
		mtree_unlock(mt);

		paddr = oalog2_mod(mas_start, priv->common->max_oasz_lg2);
		check_iova(test, mas_start, paddr, len);
		do_unmap(test, mas_start, len);
		mtree_lock(mt);
	}
	mtree_unlock(mt);
}

static void clamp_range(struct kunit *test, struct pt_range *range)
{
	struct kunit_iommu_priv *priv = test->priv;

	if (range->last_va - range->va > SZ_1G)
		range->last_va = range->va + SZ_1G;
	KUNIT_ASSERT_NE(test, range->last_va, PT_VADDR_MAX);
	if (range->va <= MAPLE_RESERVED_RANGE)
		range->va =
			ALIGN(MAPLE_RESERVED_RANGE, priv->smallest_pgsz);
}

/*
 * Randomly map and unmap ranges that can large physical pages. If a random
 * range overlaps with existing ranges then unmap them. This hits all the
 * special cases.
 */
static void test_random_map(struct kunit *test)
{
	struct kunit_iommu_priv *priv = test->priv;
	struct pt_range upper_range = pt_upper_range(priv->common);
	struct pt_range top_range = pt_top_range(priv->common);
	struct maple_tree mt;
	unsigned int iter;

	mt_init(&mt);

	/*
	 * Shrink the range so randomization is more likely to have
	 * intersections
	 */
	clamp_range(test, &top_range);
	clamp_range(test, &upper_range);

	for (iter = 0; iter != 1000; iter++) {
		struct pt_range *range = &top_range;
		pt_oaddr_t paddr;
		pt_vaddr_t start;
		pt_vaddr_t end;
		int ret;

		if (pt_feature(priv->common, PT_FEAT_SIGN_EXTEND) &&
		    ULONG_MAX >= PT_VADDR_MAX && get_random_u32_inclusive(0, 1))
			range = &upper_range;

		start = get_random_u32_below(
			min(U32_MAX, range->last_va - range->va));
		end = get_random_u32_below(
			min(U32_MAX, range->last_va - start));

		start = ALIGN_DOWN(start, priv->smallest_pgsz);
		end = ALIGN(end, priv->smallest_pgsz);
		start += range->va;
		end += start;
		if (start < range->va || end > range->last_va + 1 ||
		    start >= end)
			continue;

		/* Try overmapping to test the failure handling */
		paddr = oalog2_mod(start, priv->common->max_oasz_lg2);
		ret = iommu_map(&priv->domain, start, paddr, end - start,
				IOMMU_READ | IOMMU_WRITE, GFP_KERNEL);
		if (ret) {
			KUNIT_ASSERT_EQ(test, ret, -EADDRINUSE);
			unmap_collisions(test, &mt, start, end - 1);
			do_map(test, start, paddr, end - start);
		}

		KUNIT_ASSERT_NO_ERRNO_FN(test, "mtree_insert_range",
					 mtree_insert_range(&mt, start, end - 1,
							    XA_ZERO_ENTRY,
							    GFP_KERNEL));

		check_iova(test, start, paddr, end - start);
		if (iter % 100)
			cond_resched();
	}

	unmap_collisions(test, &mt, 0, PT_VADDR_MAX);
	KUNIT_ASSERT_EQ(test, count_valids(test), 0);

	mtree_destroy(&mt);
}

/* See https://lore.kernel.org/r/b9b18a03-63a2-4065-a27e-d92dd5c860bc@amd.com */
static void test_pgsize_boundary(struct kunit *test)
{
	struct kunit_iommu_priv *priv = test->priv;
	struct pt_range top_range = pt_top_range(priv->common);

	if (top_range.va != 0 || top_range.last_va < 0xfef9ffff ||
	    priv->smallest_pgsz != SZ_4K)
		kunit_skip(test, "Format does not have the required range");

	do_map(test, 0xfef80000, 0x208b95d000, 0xfef9ffff - 0xfef80000 + 1);
}

/* See https://lore.kernel.org/r/20250826143816.38686-1-eugkoira@amazon.com */
static void test_mixed(struct kunit *test)
{
	struct kunit_iommu_priv *priv = test->priv;
	struct pt_range top_range = pt_top_range(priv->common);
	u64 start = 0x3fe400ULL << 12;
	u64 end = 0x4c0600ULL << 12;
	pt_vaddr_t len = end - start;
	pt_oaddr_t oa = start;

	if (top_range.last_va <= start || sizeof(unsigned long) == 4)
		kunit_skip(test, "range is too small");
	if ((priv->safe_pgsize_bitmap & GENMASK(30, 21)) != (BIT(30) | BIT(21)))
		kunit_skip(test, "incompatible psize");

	do_map(test, start, oa, len);
	/* 14 2M, 3 1G, 3 2M */
	KUNIT_ASSERT_EQ(test, count_valids(test), 20);
	check_iova(test, start, oa, len);
}

static struct kunit_case iommu_test_cases[] = {
	KUNIT_CASE_FMT(test_increase_level),
	KUNIT_CASE_FMT(test_map_simple),
	KUNIT_CASE_FMT(test_map_table_to_oa),
	KUNIT_CASE_FMT(test_unmap_split),
	KUNIT_CASE_FMT(test_random_map),
	KUNIT_CASE_FMT(test_pgsize_boundary),
	KUNIT_CASE_FMT(test_mixed),
	{},
};

static int pt_kunit_iommu_init(struct kunit *test)
{
	struct kunit_iommu_priv *priv;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->orig_nr_secondary_pagetable =
		global_node_page_state(NR_SECONDARY_PAGETABLE);
	ret = pt_kunit_priv_init(test, priv);
	if (ret) {
		kunit_kfree(test, priv);
		return ret;
	}
	test->priv = priv;
	return 0;
}

static void pt_kunit_iommu_exit(struct kunit *test)
{
	struct kunit_iommu_priv *priv = test->priv;

	if (!test->priv)
		return;

	pt_iommu_deinit(priv->iommu);
	/*
	 * Look for memory leaks, assumes kunit is running isolated and nothing
	 * else is using secondary page tables.
	 */
	KUNIT_ASSERT_EQ(test, priv->orig_nr_secondary_pagetable,
			global_node_page_state(NR_SECONDARY_PAGETABLE));
	kunit_kfree(test, test->priv);
}

static struct kunit_suite NS(iommu_suite) = {
	.name = __stringify(NS(iommu_test)),
	.init = pt_kunit_iommu_init,
	.exit = pt_kunit_iommu_exit,
	.test_cases = iommu_test_cases,
};
kunit_test_suites(&NS(iommu_suite));

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kunit for generic page table");
MODULE_IMPORT_NS("GENERIC_PT_IOMMU");
