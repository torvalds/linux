// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <drm/drm_debugfs.h>

#include "lsdc_benchmark.h"
#include "lsdc_drv.h"
#include "lsdc_gem.h"
#include "lsdc_ttm.h"

typedef void (*lsdc_copy_proc_t)(struct lsdc_bo *src_bo,
				 struct lsdc_bo *dst_bo,
				 unsigned int size,
				 int n);

static void lsdc_copy_gtt_to_vram_cpu(struct lsdc_bo *src_bo,
				      struct lsdc_bo *dst_bo,
				      unsigned int size,
				      int n)
{
	lsdc_bo_kmap(src_bo);
	lsdc_bo_kmap(dst_bo);

	while (n--)
		memcpy_toio(dst_bo->kptr, src_bo->kptr, size);

	lsdc_bo_kunmap(src_bo);
	lsdc_bo_kunmap(dst_bo);
}

static void lsdc_copy_vram_to_gtt_cpu(struct lsdc_bo *src_bo,
				      struct lsdc_bo *dst_bo,
				      unsigned int size,
				      int n)
{
	lsdc_bo_kmap(src_bo);
	lsdc_bo_kmap(dst_bo);

	while (n--)
		memcpy_fromio(dst_bo->kptr, src_bo->kptr, size);

	lsdc_bo_kunmap(src_bo);
	lsdc_bo_kunmap(dst_bo);
}

static void lsdc_copy_gtt_to_gtt_cpu(struct lsdc_bo *src_bo,
				     struct lsdc_bo *dst_bo,
				     unsigned int size,
				     int n)
{
	lsdc_bo_kmap(src_bo);
	lsdc_bo_kmap(dst_bo);

	while (n--)
		memcpy(dst_bo->kptr, src_bo->kptr, size);

	lsdc_bo_kunmap(src_bo);
	lsdc_bo_kunmap(dst_bo);
}

static void lsdc_benchmark_copy(struct lsdc_device *ldev,
				unsigned int size,
				unsigned int n,
				u32 src_domain,
				u32 dst_domain,
				lsdc_copy_proc_t copy_proc,
				struct drm_printer *p)
{
	struct drm_device *ddev = &ldev->base;
	struct lsdc_bo *src_bo;
	struct lsdc_bo *dst_bo;
	unsigned long start_jiffies;
	unsigned long end_jiffies;
	unsigned int throughput;
	unsigned int time;

	src_bo = lsdc_bo_create_kernel_pinned(ddev, src_domain, size);
	dst_bo = lsdc_bo_create_kernel_pinned(ddev, dst_domain, size);

	start_jiffies = jiffies;

	copy_proc(src_bo, dst_bo, size, n);

	end_jiffies = jiffies;

	lsdc_bo_free_kernel_pinned(src_bo);
	lsdc_bo_free_kernel_pinned(dst_bo);

	time = jiffies_to_msecs(end_jiffies - start_jiffies);

	throughput = (n * (size >> 10)) / time;

	drm_printf(p,
		   "Copy bo of %uKiB %u times from %s to %s in %ums: %uMB/s\n",
		   size >> 10, n,
		   lsdc_domain_to_str(src_domain),
		   lsdc_domain_to_str(dst_domain),
		   time, throughput);
}

int lsdc_show_benchmark_copy(struct lsdc_device *ldev, struct drm_printer *p)
{
	unsigned int buffer_size = 1920 * 1080 * 4;
	unsigned int iteration = 60;

	lsdc_benchmark_copy(ldev,
			    buffer_size,
			    iteration,
			    LSDC_GEM_DOMAIN_GTT,
			    LSDC_GEM_DOMAIN_GTT,
			    lsdc_copy_gtt_to_gtt_cpu,
			    p);

	lsdc_benchmark_copy(ldev,
			    buffer_size,
			    iteration,
			    LSDC_GEM_DOMAIN_GTT,
			    LSDC_GEM_DOMAIN_VRAM,
			    lsdc_copy_gtt_to_vram_cpu,
			    p);

	lsdc_benchmark_copy(ldev,
			    buffer_size,
			    iteration,
			    LSDC_GEM_DOMAIN_VRAM,
			    LSDC_GEM_DOMAIN_GTT,
			    lsdc_copy_vram_to_gtt_cpu,
			    p);

	return 0;
}
