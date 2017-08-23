/**************************************************************************
 *
 * Copyright (c) 2017 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Tom St Denis <tom.stdenis@amd.com>
 */
#include <linux/sched.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/shmem_fs.h>
#include <linux/file.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <drm/drm_cache.h>
#include <drm/ttm/ttm_module.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_page_alloc.h>
#include "ttm_trace.h"

void ttm_trace_dma_map(struct device *dev, struct ttm_dma_tt *tt)
{
	unsigned i;

	if (unlikely(trace_ttm_dma_map_enabled())) {
		for (i = 0; i < tt->ttm.num_pages; i++) {
			trace_ttm_dma_map(
				dev,
				tt->ttm.pages[i],
				tt->dma_address[i]);
		}
	}
}
EXPORT_SYMBOL(ttm_trace_dma_map);

void ttm_trace_dma_unmap(struct device *dev, struct ttm_dma_tt *tt)
{
	unsigned i;

	if (unlikely(trace_ttm_dma_unmap_enabled())) {
		for (i = 0; i < tt->ttm.num_pages; i++) {
			trace_ttm_dma_unmap(
				dev,
				tt->ttm.pages[i],
				tt->dma_address[i]);
		}
	}
}
EXPORT_SYMBOL(ttm_trace_dma_unmap);

