// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google Inc
 * Author: Mostafa Saleh <smostafa@google.com>
 * IOMMU API debug page alloc sanitizer
 */
#include <linux/atomic.h>
#include <linux/iommu.h>
#include <linux/iommu-debug-pagealloc.h>
#include <linux/kernel.h>
#include <linux/page_ext.h>

#include "iommu-priv.h"

static bool needed;
DEFINE_STATIC_KEY_FALSE(iommu_debug_initialized);

struct iommu_debug_metadata {
	atomic_t ref;
};

static __init bool need_iommu_debug(void)
{
	return needed;
}

struct page_ext_operations page_iommu_debug_ops = {
	.size = sizeof(struct iommu_debug_metadata),
	.need = need_iommu_debug,
};

void __iommu_debug_map(struct iommu_domain *domain, phys_addr_t phys, size_t size)
{
}

void __iommu_debug_unmap_begin(struct iommu_domain *domain,
			       unsigned long iova, size_t size)
{
}

void __iommu_debug_unmap_end(struct iommu_domain *domain,
			     unsigned long iova, size_t size,
			     size_t unmapped)
{
}

void iommu_debug_init(void)
{
	if (!needed)
		return;

	pr_info("iommu: Debugging page allocations, expect overhead or disable iommu.debug_pagealloc");
	static_branch_enable(&iommu_debug_initialized);
}

static int __init iommu_debug_pagealloc(char *str)
{
	return kstrtobool(str, &needed);
}
early_param("iommu.debug_pagealloc", iommu_debug_pagealloc);
