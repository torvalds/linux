// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2011-2022 ARM Limited. All rights reserved.
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

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <mali_kbase_fence.h>
#include <mali_kbase.h>

/* Spin lock protecting all Mali fences as fence->lock. */
static DEFINE_SPINLOCK(kbase_fence_lock);

#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
struct fence *
kbase_fence_out_new(struct kbase_jd_atom *katom)
#else
struct dma_fence *
kbase_fence_out_new(struct kbase_jd_atom *katom)
#endif
{
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
	struct fence *fence;
#else
	struct dma_fence *fence;
#endif

	WARN_ON(katom->dma_fence.fence);

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return NULL;

	dma_fence_init(fence,
		       &kbase_fence_ops,
		       &kbase_fence_lock,
		       katom->dma_fence.context,
		       atomic_inc_return(&katom->dma_fence.seqno));

	katom->dma_fence.fence = fence;

	return fence;
}

