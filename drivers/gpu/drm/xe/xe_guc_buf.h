/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GUC_BUF_H_
#define _XE_GUC_BUF_H_

#include <linux/cleanup.h>
#include <linux/err.h>

#include "xe_guc_buf_types.h"

int xe_guc_buf_cache_init(struct xe_guc_buf_cache *cache);
u32 xe_guc_buf_cache_dwords(struct xe_guc_buf_cache *cache);
struct xe_guc_buf xe_guc_buf_reserve(struct xe_guc_buf_cache *cache, u32 dwords);
struct xe_guc_buf xe_guc_buf_from_data(struct xe_guc_buf_cache *cache,
				       const void *data, size_t size);
void xe_guc_buf_release(const struct xe_guc_buf buf);

/**
 * xe_guc_buf_is_valid() - Check if a buffer reference is valid.
 * @buf: the &xe_guc_buf reference to check
 *
 * Return: true if @ref represents a valid sub-allication.
 */
static inline bool xe_guc_buf_is_valid(const struct xe_guc_buf buf)
{
	return !IS_ERR_OR_NULL(buf.sa);
}

void *xe_guc_buf_cpu_ptr(const struct xe_guc_buf buf);
u64 xe_guc_buf_flush(const struct xe_guc_buf buf);
u64 xe_guc_buf_gpu_addr(const struct xe_guc_buf buf);
u64 xe_guc_cache_gpu_addr_from_ptr(struct xe_guc_buf_cache *cache, const void *ptr, u32 size);

DEFINE_CLASS(xe_guc_buf, struct xe_guc_buf,
	     xe_guc_buf_release(_T),
	     xe_guc_buf_reserve(cache, num),
	     struct xe_guc_buf_cache *cache, u32 num);

DEFINE_CLASS(xe_guc_buf_from_data, struct xe_guc_buf,
	     xe_guc_buf_release(_T),
	     xe_guc_buf_from_data(cache, data, size),
	     struct xe_guc_buf_cache *cache, const void *data, size_t size);

#endif
