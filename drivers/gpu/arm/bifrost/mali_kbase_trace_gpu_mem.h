/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _KBASE_TRACE_GPU_MEM_H_
#define _KBASE_TRACE_GPU_MEM_H_

#if IS_ENABLED(CONFIG_TRACE_GPU_MEM)
#include <trace/events/gpu_mem.h>
#endif

#define DEVICE_TGID ((u32) 0U)

static void kbase_trace_gpu_mem_usage(struct kbase_device *kbdev,
				      struct kbase_context *kctx)
{
#if IS_ENABLED(CONFIG_TRACE_GPU_MEM)
	lockdep_assert_held(&kbdev->gpu_mem_usage_lock);

	trace_gpu_mem_total(kbdev->id, DEVICE_TGID,
			    kbdev->total_gpu_pages << PAGE_SHIFT);

	if (likely(kctx))
		trace_gpu_mem_total(kbdev->id, kctx->kprcs->tgid,
				kctx->kprcs->total_gpu_pages << PAGE_SHIFT);
#endif
}

static inline void kbase_trace_gpu_mem_usage_dec(struct kbase_device *kbdev,
				struct kbase_context *kctx, size_t pages)
{
	spin_lock(&kbdev->gpu_mem_usage_lock);

	if (likely(kctx))
		kctx->kprcs->total_gpu_pages -= pages;

	kbdev->total_gpu_pages -= pages;

	kbase_trace_gpu_mem_usage(kbdev, kctx);

	spin_unlock(&kbdev->gpu_mem_usage_lock);
}

static inline void kbase_trace_gpu_mem_usage_inc(struct kbase_device *kbdev,
				struct kbase_context *kctx, size_t pages)
{
	spin_lock(&kbdev->gpu_mem_usage_lock);

	if (likely(kctx))
		kctx->kprcs->total_gpu_pages += pages;

	kbdev->total_gpu_pages += pages;

	kbase_trace_gpu_mem_usage(kbdev, kctx);

	spin_unlock(&kbdev->gpu_mem_usage_lock);
}

/**
 * kbase_remove_dma_buf_usage - Remove a dma-buf entry captured.
 *
 * @kctx: Pointer to the kbase context
 * @alloc: Pointer to the alloc to unmap
 *
 * Remove reference to dma buf been unmapped from kbase_device level
 * rb_tree and Kbase_process level dma buf rb_tree.
 */
void kbase_remove_dma_buf_usage(struct kbase_context *kctx,
				struct kbase_mem_phy_alloc *alloc);

/**
 * kbase_add_dma_buf_usage - Add a dma-buf entry captured.
 *
 * @kctx: Pointer to the kbase context
 * @alloc: Pointer to the alloc to map in
 *
 * Add reference to dma buf been mapped to kbase_device level
 * rb_tree and Kbase_process level dma buf rb_tree.
 */
void kbase_add_dma_buf_usage(struct kbase_context *kctx,
				    struct kbase_mem_phy_alloc *alloc);

#endif /* _KBASE_TRACE_GPU_MEM_H_ */
