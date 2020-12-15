// SPDX-License-Identifier: GPL-2.0-only
/*
 * CPU-agnostic AMD IO page table allocator.
 *
 * Copyright (C) 2020 Advanced Micro Devices, Inc.
 * Author: Suravee Suthikulpanit <suravee.suthikulpanit@amd.com>
 */

#define pr_fmt(fmt)     "AMD-Vi: " fmt
#define dev_fmt(fmt)    pr_fmt(fmt)

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/io-pgtable.h>
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>

#include <asm/barrier.h>

#include "amd_iommu_types.h"
#include "amd_iommu.h"

static void v1_tlb_flush_all(void *cookie)
{
}

static void v1_tlb_flush_walk(unsigned long iova, size_t size,
				  size_t granule, void *cookie)
{
}

static void v1_tlb_add_page(struct iommu_iotlb_gather *gather,
					 unsigned long iova, size_t granule,
					 void *cookie)
{
}

static const struct iommu_flush_ops v1_flush_ops = {
	.tlb_flush_all	= v1_tlb_flush_all,
	.tlb_flush_walk = v1_tlb_flush_walk,
	.tlb_add_page	= v1_tlb_add_page,
};

/*
 * ----------------------------------------------------
 */
static void v1_free_pgtable(struct io_pgtable *iop)
{
}

static struct io_pgtable *v1_alloc_pgtable(struct io_pgtable_cfg *cfg, void *cookie)
{
	struct amd_io_pgtable *pgtable = io_pgtable_cfg_to_data(cfg);

	cfg->pgsize_bitmap  = AMD_IOMMU_PGSIZES,
	cfg->ias            = IOMMU_IN_ADDR_BIT_SIZE,
	cfg->oas            = IOMMU_OUT_ADDR_BIT_SIZE,
	cfg->tlb            = &v1_flush_ops;

	return &pgtable->iop;
}

struct io_pgtable_init_fns io_pgtable_amd_iommu_v1_init_fns = {
	.alloc	= v1_alloc_pgtable,
	.free	= v1_free_pgtable,
};
