/*
 * @File
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/spinlock_types.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/bug.h>

#include "pvr_sw_fence.h"

struct pvr_sw_fence_context {
	struct kref kref;
	unsigned int context;
	char context_name[32];
	char driver_name[32];
	atomic_t seqno;
	atomic_t fence_count;
};

struct pvr_sw_fence {
	struct dma_fence base;
	struct pvr_sw_fence_context *fence_context;
	spinlock_t lock;
};

#define to_pvr_sw_fence(fence) container_of(fence, struct pvr_sw_fence, base)

const char *pvr_sw_fence_context_name(struct pvr_sw_fence_context *fctx)
{
	return fctx->context_name;
}

void pvr_sw_fence_context_value_str(struct pvr_sw_fence_context *fctx,
				    char *str, int size)
{
	snprintf(str, size, "%d", atomic_read(&fctx->seqno));
}

static inline unsigned
pvr_sw_fence_context_seqno_next(struct pvr_sw_fence_context *fence_context)
{
	return atomic_inc_return(&fence_context->seqno) - 1;
}

static const char *pvr_sw_fence_get_driver_name(struct dma_fence *fence)
{
	struct pvr_sw_fence *pvr_sw_fence = to_pvr_sw_fence(fence);

	return pvr_sw_fence->fence_context->driver_name;
}

static const char *pvr_sw_fence_get_timeline_name(struct dma_fence *fence)
{
	struct pvr_sw_fence *pvr_sw_fence = to_pvr_sw_fence(fence);

	return pvr_sw_fence_context_name(pvr_sw_fence->fence_context);
}

static void pvr_sw_fence_value_str(struct dma_fence *fence, char *str, int size)
{
	snprintf(str, size, "%llu", (u64) fence->seqno);
}

static void pvr_sw_fence_timeline_value_str(struct dma_fence *fence,
					    char *str, int size)
{
	struct pvr_sw_fence *pvr_sw_fence = to_pvr_sw_fence(fence);

	pvr_sw_fence_context_value_str(pvr_sw_fence->fence_context, str, size);
}

static bool pvr_sw_fence_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static void pvr_sw_fence_context_destroy_kref(struct kref *kref)
{
	struct pvr_sw_fence_context *fence_context =
		container_of(kref, struct pvr_sw_fence_context, kref);
	unsigned int fence_count;

	fence_count = atomic_read(&fence_context->fence_count);
	if (WARN_ON(fence_count))
		pr_debug("%s context has %u fence(s) remaining\n",
			 fence_context->context_name, fence_count);

	kfree(fence_context);
}

static void pvr_sw_fence_release(struct dma_fence *fence)
{
	struct pvr_sw_fence *pvr_sw_fence = to_pvr_sw_fence(fence);

	atomic_dec(&pvr_sw_fence->fence_context->fence_count);
	kref_put(&pvr_sw_fence->fence_context->kref,
		pvr_sw_fence_context_destroy_kref);
	kfree(pvr_sw_fence);
}

static const struct dma_fence_ops pvr_sw_fence_ops = {
	.get_driver_name = pvr_sw_fence_get_driver_name,
	.get_timeline_name = pvr_sw_fence_get_timeline_name,
	.fence_value_str = pvr_sw_fence_value_str,
	.timeline_value_str = pvr_sw_fence_timeline_value_str,
	.enable_signaling = pvr_sw_fence_enable_signaling,
	.wait = dma_fence_default_wait,
	.release = pvr_sw_fence_release,
};

struct pvr_sw_fence_context *
pvr_sw_fence_context_create(const char *context_name, const char *driver_name)
{
	struct pvr_sw_fence_context *fence_context;

	fence_context = kmalloc(sizeof(*fence_context), GFP_KERNEL);
	if (!fence_context)
		return NULL;

	fence_context->context = dma_fence_context_alloc(1);
	strlcpy(fence_context->context_name, context_name,
		sizeof(fence_context->context_name));
	strlcpy(fence_context->driver_name, driver_name,
		sizeof(fence_context->driver_name));
	atomic_set(&fence_context->seqno, 0);
	atomic_set(&fence_context->fence_count, 0);
	kref_init(&fence_context->kref);

	return fence_context;
}

void pvr_sw_fence_context_destroy(struct pvr_sw_fence_context *fence_context)
{
	kref_put(&fence_context->kref, pvr_sw_fence_context_destroy_kref);
}

struct dma_fence *
pvr_sw_fence_create(struct pvr_sw_fence_context *fence_context)
{
	struct pvr_sw_fence *pvr_sw_fence;
	unsigned int seqno;

	pvr_sw_fence = kmalloc(sizeof(*pvr_sw_fence), GFP_KERNEL);
	if (!pvr_sw_fence)
		return NULL;

	spin_lock_init(&pvr_sw_fence->lock);
	pvr_sw_fence->fence_context = fence_context;

	seqno = pvr_sw_fence_context_seqno_next(fence_context);
	dma_fence_init(&pvr_sw_fence->base, &pvr_sw_fence_ops,
		       &pvr_sw_fence->lock, fence_context->context, seqno);

	atomic_inc(&fence_context->fence_count);
	kref_get(&fence_context->kref);

	return &pvr_sw_fence->base;
}
