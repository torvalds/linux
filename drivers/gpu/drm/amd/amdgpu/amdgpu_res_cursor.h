// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König
 */

#ifndef __AMDGPU_RES_CURSOR_H__
#define __AMDGPU_RES_CURSOR_H__

#include <drm/drm_mm.h>
#include <drm/ttm/ttm_resource.h>
#include <drm/ttm/ttm_range_manager.h>

/* state back for walking over vram_mgr and gtt_mgr allocations */
struct amdgpu_res_cursor {
	uint64_t		start;
	uint64_t		size;
	uint64_t		remaining;
	struct drm_mm_node	*node;
};

/**
 * amdgpu_res_first - initialize a amdgpu_res_cursor
 *
 * @res: TTM resource object to walk
 * @start: Start of the range
 * @size: Size of the range
 * @cur: cursor object to initialize
 *
 * Start walking over the range of allocations between @start and @size.
 */
static inline void amdgpu_res_first(struct ttm_resource *res,
				    uint64_t start, uint64_t size,
				    struct amdgpu_res_cursor *cur)
{
	struct drm_mm_node *node;

	if (!res) {
		cur->start = start;
		cur->size = size;
		cur->remaining = size;
		cur->node = NULL;
		return;
	}

	BUG_ON(start + size > res->num_pages << PAGE_SHIFT);

	node = to_ttm_range_mgr_node(res)->mm_nodes;
	while (start >= node->size << PAGE_SHIFT)
		start -= node++->size << PAGE_SHIFT;

	cur->start = (node->start << PAGE_SHIFT) + start;
	cur->size = min((node->size << PAGE_SHIFT) - start, size);
	cur->remaining = size;
	cur->node = node;
}

/**
 * amdgpu_res_next - advance the cursor
 *
 * @cur: the cursor to advance
 * @size: number of bytes to move forward
 *
 * Move the cursor @size bytes forwrad, walking to the next node if necessary.
 */
static inline void amdgpu_res_next(struct amdgpu_res_cursor *cur, uint64_t size)
{
	struct drm_mm_node *node = cur->node;

	BUG_ON(size > cur->remaining);

	cur->remaining -= size;
	if (!cur->remaining)
		return;

	cur->size -= size;
	if (cur->size) {
		cur->start += size;
		return;
	}

	cur->node = ++node;
	cur->start = node->start << PAGE_SHIFT;
	cur->size = min(node->size << PAGE_SHIFT, cur->remaining);
}

#endif
