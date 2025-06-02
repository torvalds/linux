// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <linux/cleanup.h>
#include <drm/drm_managed.h>

#include "xe_assert.h"
#include "xe_bo.h"
#include "xe_gt_printk.h"
#include "xe_guc.h"
#include "xe_guc_buf.h"
#include "xe_sa.h"

static struct xe_guc *cache_to_guc(struct xe_guc_buf_cache *cache)
{
	return container_of(cache, struct xe_guc, buf);
}

static struct xe_gt *cache_to_gt(struct xe_guc_buf_cache *cache)
{
	return guc_to_gt(cache_to_guc(cache));
}

/**
 * xe_guc_buf_cache_init() - Initialize the GuC Buffer Cache.
 * @cache: the &xe_guc_buf_cache to initialize
 *
 * The Buffer Cache allows to obtain a reusable buffer that can be used to pass
 * indirect H2G data to GuC without a need to create a ad-hoc allocation.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_guc_buf_cache_init(struct xe_guc_buf_cache *cache)
{
	struct xe_gt *gt = cache_to_gt(cache);
	struct xe_sa_manager *sam;

	/* XXX: currently it's useful only for the PF actions */
	if (!IS_SRIOV_PF(gt_to_xe(gt)))
		return 0;

	sam = __xe_sa_bo_manager_init(gt_to_tile(gt), SZ_8K, 0, sizeof(u32));
	if (IS_ERR(sam))
		return PTR_ERR(sam);
	cache->sam = sam;

	xe_gt_dbg(gt, "reusable buffer with %u dwords at %#x for %ps\n",
		  xe_guc_buf_cache_dwords(cache), xe_bo_ggtt_addr(sam->bo),
		  __builtin_return_address(0));
	return 0;
}

/**
 * xe_guc_buf_cache_dwords() - Number of dwords the GuC Buffer Cache supports.
 * @cache: the &xe_guc_buf_cache to query
 *
 * Return: a size of the largest reusable buffer (in dwords)
 */
u32 xe_guc_buf_cache_dwords(struct xe_guc_buf_cache *cache)
{
	return cache->sam ? cache->sam->base.size / sizeof(u32) : 0;
}

/**
 * xe_guc_buf_reserve() - Reserve a new sub-allocation.
 * @cache: the &xe_guc_buf_cache where reserve sub-allocation
 * @dwords: the requested size of the buffer in dwords
 *
 * Use xe_guc_buf_is_valid() to check if returned buffer reference is valid.
 * Must use xe_guc_buf_release() to release a sub-allocation.
 *
 * Return: a &xe_guc_buf of new sub-allocation.
 */
struct xe_guc_buf xe_guc_buf_reserve(struct xe_guc_buf_cache *cache, u32 dwords)
{
	struct drm_suballoc *sa;

	if (cache->sam)
		sa = __xe_sa_bo_new(cache->sam, dwords * sizeof(u32), GFP_ATOMIC);
	else
		sa = ERR_PTR(-EOPNOTSUPP);

	return (struct xe_guc_buf){ .sa = sa };
}

/**
 * xe_guc_buf_from_data() - Reserve a new sub-allocation using data.
 * @cache: the &xe_guc_buf_cache where reserve sub-allocation
 * @data: the data to flush the sub-allocation
 * @size: the size of the data
 *
 * Similar to xe_guc_buf_reserve() but flushes @data to the GPU memory.
 *
 * Return: a &xe_guc_buf of new sub-allocation.
 */
struct xe_guc_buf xe_guc_buf_from_data(struct xe_guc_buf_cache *cache,
				       const void *data, size_t size)
{
	struct drm_suballoc *sa;

	sa = __xe_sa_bo_new(cache->sam, size, GFP_ATOMIC);
	if (!IS_ERR(sa))
		memcpy(xe_sa_bo_cpu_addr(sa), data, size);

	return (struct xe_guc_buf){ .sa = sa };
}

/**
 * xe_guc_buf_release() - Release a sub-allocation.
 * @buf: the &xe_guc_buf to release
 *
 * Releases a sub-allocation reserved by the xe_guc_buf_reserve().
 */
void xe_guc_buf_release(const struct xe_guc_buf buf)
{
	if (xe_guc_buf_is_valid(buf))
		xe_sa_bo_free(buf.sa, NULL);
}

/**
 * xe_guc_buf_flush() - Copy the data from the sub-allocation to the GPU memory.
 * @buf: the &xe_guc_buf to flush
 *
 * Return: a GPU address of the sub-allocation.
 */
u64 xe_guc_buf_flush(const struct xe_guc_buf buf)
{
	xe_sa_bo_flush_write(buf.sa);
	return xe_sa_bo_gpu_addr(buf.sa);
}

/**
 * xe_guc_buf_cpu_ptr() - Obtain a CPU pointer to the sub-allocation.
 * @buf: the &xe_guc_buf to query
 *
 * Return: a CPU pointer of the sub-allocation.
 */
void *xe_guc_buf_cpu_ptr(const struct xe_guc_buf buf)
{
	return xe_sa_bo_cpu_addr(buf.sa);
}

/**
 * xe_guc_buf_gpu_addr() - Obtain a GPU address of the sub-allocation.
 * @buf: the &xe_guc_buf to query
 *
 * Return: a GPU address of the sub-allocation.
 */
u64 xe_guc_buf_gpu_addr(const struct xe_guc_buf buf)
{
	return xe_sa_bo_gpu_addr(buf.sa);
}

/**
 * xe_guc_cache_gpu_addr_from_ptr() - Lookup a GPU address using the pointer.
 * @cache: the &xe_guc_buf_cache with sub-allocations
 * @ptr: the CPU pointer of the sub-allocation
 * @size: the size of the data
 *
 * Return: a GPU address on success or 0 if the pointer was unrelated.
 */
u64 xe_guc_cache_gpu_addr_from_ptr(struct xe_guc_buf_cache *cache, const void *ptr, u32 size)
{
	ptrdiff_t offset = ptr - cache->sam->cpu_ptr;

	if (offset < 0 || offset + size > cache->sam->base.size)
		return 0;

	return cache->sam->gpu_addr + offset;
}

#if IS_BUILTIN(CONFIG_DRM_XE_KUNIT_TEST)
#include "tests/xe_guc_buf_kunit.c"
#endif
