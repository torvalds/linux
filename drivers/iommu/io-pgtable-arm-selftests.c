// SPDX-License-Identifier: GPL-2.0-only
/*
 * CPU-agnostic ARM page table allocator.
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#define pr_fmt(fmt)	"arm-lpae io-pgtable: " fmt

#include <linux/device/faux.h>
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

#define __FAIL(i) ({							\
		WARN(1, "selftest: test failed for fmt idx %d\n", (i));	\
		-EFAULT;						\
})

static int arm_lpae_run_tests(struct io_pgtable_cfg *cfg)
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
			pr_err("selftest: failed to allocate io pgtable ops\n");
			return -ENOMEM;
		}

		/*
		 * Initial sanity checks.
		 * Empty page tables shouldn't provide any translations.
		 */
		if (ops->iova_to_phys(ops, 42))
			return __FAIL(i);

		if (ops->iova_to_phys(ops, SZ_1G + 42))
			return __FAIL(i);

		if (ops->iova_to_phys(ops, SZ_2G + 42))
			return __FAIL(i);

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
				return __FAIL(i);

			/* Overlapping mappings */
			if (!ops->map_pages(ops, iova, iova + size, size, 1,
					    IOMMU_READ | IOMMU_NOEXEC,
					    GFP_KERNEL, &mapped))
				return __FAIL(i);

			if (ops->iova_to_phys(ops, iova + 42) != (iova + 42))
				return __FAIL(i);

			iova += SZ_1G;
		}

		/* Full unmap */
		iova = 0;
		for_each_set_bit(j, &cfg->pgsize_bitmap, BITS_PER_LONG) {
			size = 1UL << j;

			if (ops->unmap_pages(ops, iova, size, 1, NULL) != size)
				return __FAIL(i);

			if (ops->iova_to_phys(ops, iova + 42))
				return __FAIL(i);

			/* Remap full block */
			if (ops->map_pages(ops, iova, iova, size, 1,
					   IOMMU_WRITE, GFP_KERNEL, &mapped))
				return __FAIL(i);

			if (ops->iova_to_phys(ops, iova + 42) != (iova + 42))
				return __FAIL(i);

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
			return __FAIL(i);
		if (mapped != size)
			return __FAIL(i);
		if (ops->unmap_pages(ops, iova, size, 1, NULL) != size)
			return __FAIL(i);

		free_io_pgtable_ops(ops);
	}

	return 0;
}

static int arm_lpae_do_selftests(void)
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
	struct faux_device *dev;
	struct io_pgtable_cfg cfg = {
		.tlb = &dummy_tlb_ops,
		.coherent_walk = true,
		.quirks = IO_PGTABLE_QUIRK_NO_WARN,
	};

	dev = faux_device_create("io-pgtable-test", NULL, 0);
	if (!dev)
		return -ENOMEM;

	cfg.iommu_dev = &dev->dev;

	for (i = 0; i < ARRAY_SIZE(pgsize); ++i) {
		for (j = 0; j < ARRAY_SIZE(address_size); ++j) {
			/* Don't use ias > oas as it is not valid for stage-2. */
			for (k = 0; k <= j; ++k) {
				cfg.pgsize_bitmap = pgsize[i];
				cfg.ias = address_size[k];
				cfg.oas = address_size[j];
				pr_info("selftest: pgsize_bitmap 0x%08lx, IAS %u OAS %u\n",
					pgsize[i], cfg.ias, cfg.oas);
				if (arm_lpae_run_tests(&cfg))
					fail++;
				else
					pass++;
			}
		}
	}

	pr_info("selftest: completed with %d PASS %d FAIL\n", pass, fail);
	faux_device_destroy(dev);

	return fail ? -EFAULT : 0;
}

static void arm_lpae_exit_selftests(void)
{
}

subsys_initcall(arm_lpae_do_selftests);
module_exit(arm_lpae_exit_selftests);
MODULE_DESCRIPTION("io-pgtable-arm library selftest");
MODULE_LICENSE("GPL");
