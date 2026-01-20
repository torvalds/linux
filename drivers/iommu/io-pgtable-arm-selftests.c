// SPDX-License-Identifier: GPL-2.0-only
/*
 * CPU-agnostic ARM page table allocator.
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#define pr_fmt(fmt)	"arm-lpae io-pgtable: " fmt

#include <kunit/device.h>
#include <kunit/test.h>
#include <linux/io-pgtable.h>
#include <linux/kernel.h>

#include "io-pgtable-arm.h"

static struct io_pgtable_cfg *cfg_cookie;

static void dummy_tlb_flush_all(void *cookie)
{
	WARN_ON(cookie != cfg_cookie);
}

static void dummy_tlb_flush(unsigned long iova, size_t size,
			    size_t granule, void *cookie)
{
	WARN_ON(cookie != cfg_cookie);
	WARN_ON(!(size & cfg_cookie->pgsize_bitmap));
}

static void dummy_tlb_add_page(struct iommu_iotlb_gather *gather,
			       unsigned long iova, size_t granule,
			       void *cookie)
{
	dummy_tlb_flush(iova, granule, granule, cookie);
}

static const struct iommu_flush_ops dummy_tlb_ops = {
	.tlb_flush_all	= dummy_tlb_flush_all,
	.tlb_flush_walk	= dummy_tlb_flush,
	.tlb_add_page	= dummy_tlb_add_page,
};

#define __FAIL(test, i) ({							\
		KUNIT_FAIL(test, "test failed for fmt idx %d\n", (i));		\
		-EFAULT;							\
})

static int arm_lpae_run_tests(struct kunit *test, struct io_pgtable_cfg *cfg)
{
	static const enum io_pgtable_fmt fmts[] = {
		ARM_64_LPAE_S1,
		ARM_64_LPAE_S2,
	};

	int i, j;
	unsigned long iova;
	size_t size, mapped;
	struct io_pgtable_ops *ops;

	for (i = 0; i < ARRAY_SIZE(fmts); ++i) {
		cfg_cookie = cfg;
		ops = alloc_io_pgtable_ops(fmts[i], cfg, cfg);
		if (!ops) {
			kunit_err(test, "failed to allocate io pgtable ops\n");
			return -ENOMEM;
		}

		/*
		 * Initial sanity checks.
		 * Empty page tables shouldn't provide any translations.
		 */
		if (ops->iova_to_phys(ops, 42))
			return __FAIL(test, i);

		if (ops->iova_to_phys(ops, SZ_1G + 42))
			return __FAIL(test, i);

		if (ops->iova_to_phys(ops, SZ_2G + 42))
			return __FAIL(test, i);

		/*
		 * Distinct mappings of different granule sizes.
		 */
		iova = 0;
		for_each_set_bit(j, &cfg->pgsize_bitmap, BITS_PER_LONG) {
			size = 1UL << j;

			if (ops->map_pages(ops, iova, iova, size, 1,
					   IOMMU_READ | IOMMU_WRITE |
					   IOMMU_NOEXEC | IOMMU_CACHE,
					   GFP_KERNEL, &mapped))
				return __FAIL(test, i);

			/* Overlapping mappings */
			if (!ops->map_pages(ops, iova, iova + size, size, 1,
					    IOMMU_READ | IOMMU_NOEXEC,
					    GFP_KERNEL, &mapped))
				return __FAIL(test, i);

			if (ops->iova_to_phys(ops, iova + 42) != (iova + 42))
				return __FAIL(test, i);

			iova += SZ_1G;
		}

		/* Full unmap */
		iova = 0;
		for_each_set_bit(j, &cfg->pgsize_bitmap, BITS_PER_LONG) {
			size = 1UL << j;

			if (ops->unmap_pages(ops, iova, size, 1, NULL) != size)
				return __FAIL(test, i);

			if (ops->iova_to_phys(ops, iova + 42))
				return __FAIL(test, i);

			/* Remap full block */
			if (ops->map_pages(ops, iova, iova, size, 1,
					   IOMMU_WRITE, GFP_KERNEL, &mapped))
				return __FAIL(test, i);

			if (ops->iova_to_phys(ops, iova + 42) != (iova + 42))
				return __FAIL(test, i);

			iova += SZ_1G;
		}

		/*
		 * Map/unmap the last largest supported page of the IAS, this can
		 * trigger corner cases in the concatednated page tables.
		 */
		mapped = 0;
		size = 1UL << __fls(cfg->pgsize_bitmap);
		iova = (1UL << cfg->ias) - size;
		if (ops->map_pages(ops, iova, iova, size, 1,
				   IOMMU_READ | IOMMU_WRITE |
				   IOMMU_NOEXEC | IOMMU_CACHE,
				   GFP_KERNEL, &mapped))
			return __FAIL(test, i);
		if (mapped != size)
			return __FAIL(test, i);
		if (ops->unmap_pages(ops, iova, size, 1, NULL) != size)
			return __FAIL(test, i);

		free_io_pgtable_ops(ops);
	}

	return 0;
}

static void arm_lpae_do_selftests(struct kunit *test)
{
	static const unsigned long pgsize[] = {
		SZ_4K | SZ_2M | SZ_1G,
		SZ_16K | SZ_32M,
		SZ_64K | SZ_512M,
	};

	static const unsigned int address_size[] = {
		32, 36, 40, 42, 44, 48,
	};

	int i, j, k, pass = 0, fail = 0;
	struct device *dev;
	struct io_pgtable_cfg cfg = {
		.tlb = &dummy_tlb_ops,
		.coherent_walk = true,
		.quirks = IO_PGTABLE_QUIRK_NO_WARN,
	};

	dev = kunit_device_register(test, "io-pgtable-test");
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, dev);
	if (IS_ERR_OR_NULL(dev))
		return;

	cfg.iommu_dev = dev;

	for (i = 0; i < ARRAY_SIZE(pgsize); ++i) {
		for (j = 0; j < ARRAY_SIZE(address_size); ++j) {
			/* Don't use ias > oas as it is not valid for stage-2. */
			for (k = 0; k <= j; ++k) {
				cfg.pgsize_bitmap = pgsize[i];
				cfg.ias = address_size[k];
				cfg.oas = address_size[j];
				kunit_info(test, "pgsize_bitmap 0x%08lx, IAS %u OAS %u\n",
					   pgsize[i], cfg.ias, cfg.oas);
				if (arm_lpae_run_tests(test, &cfg))
					fail++;
				else
					pass++;
			}
		}
	}

	kunit_info(test, "completed with %d PASS %d FAIL\n", pass, fail);
}

static struct kunit_case io_pgtable_arm_test_cases[] = {
	KUNIT_CASE(arm_lpae_do_selftests),
	{},
};

static struct kunit_suite io_pgtable_arm_test = {
	.name = "io-pgtable-arm-test",
	.test_cases = io_pgtable_arm_test_cases,
};

kunit_test_suite(io_pgtable_arm_test);

MODULE_DESCRIPTION("io-pgtable-arm library kunit tests");
MODULE_LICENSE("GPL");
