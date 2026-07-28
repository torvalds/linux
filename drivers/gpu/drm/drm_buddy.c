// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <kunit/test-bug.h>

#include <linux/export.h>
#include <linux/kmemleak.h>
#include <linux/module.h>
#include <linux/sizes.h>

#include <linux/gpu_buddy.h>
#include <drm/drm_buddy.h>
#include <drm/drm_print.h>

/**
 * drm_buddy_block_print - print block information
 *
 * @mm: DRM buddy manager
 * @block: DRM buddy block
 * @p: DRM printer to use
 */
void drm_buddy_block_print(struct gpu_buddy *mm,
			   struct gpu_buddy_block *block,
			   struct drm_printer *p)
{
	u64 start = gpu_buddy_block_offset(block);
	u64 size = gpu_buddy_block_size(mm, block);

	drm_printf(p, "%#018llx-%#018llx: %llu\n", start, start + size, size);
}
EXPORT_SYMBOL(drm_buddy_block_print);

/**
 * drm_buddy_print - print allocator state
 *
 * @mm: DRM buddy manager
 * @p: DRM printer to use
 */
void drm_buddy_print(struct gpu_buddy *mm, struct drm_printer *p)
{
	int order;

	gpu_buddy_driver_lock_held(mm);
	drm_printf(p, "chunk_size: %lluKiB, total: %lluMiB, free: %lluMiB, clear_free: %lluMiB\n",
		   mm->chunk_size >> 10, mm->size >> 20, mm->avail >> 20, mm->clear_avail >> 20);

	for (order = mm->max_order; order >= 0; order--) {
		u64 free_count = mm->free_scoreboard[order];
		u64 used_count = mm->used_scoreboard[order];
		u64 block_size = mm->chunk_size << order;
		u64 free = free_count * block_size;
		u64 used = used_count * block_size;

		drm_printf(p, "order-%2d ", order);

		if (block_size < SZ_1M)
			drm_printf(p, "free: %8llu KiB, used: %8llu KiB",
				   free >> 10, used >> 10);
		else
			drm_printf(p, "free: %8llu MiB, used: %8llu MiB",
				   free >> 20, used >> 20);

		drm_printf(p, ", free_blocks: %llu, used_blocks: %llu\n",
			   free_count, used_count);
	}
}
EXPORT_SYMBOL(drm_buddy_print);

MODULE_DESCRIPTION("DRM-specific GPU Buddy Allocator Print Helpers");
MODULE_LICENSE("Dual MIT/GPL");
