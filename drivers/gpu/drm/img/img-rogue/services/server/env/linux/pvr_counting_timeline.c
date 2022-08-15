/*
 * @File
 * @Title       PowerVR Linux software "counting" timeline fence implementation
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
#include <linux/slab.h>
#include <linux/kref.h>

#include "services_kernel_client.h"
#include "pvr_counting_timeline.h"
#include "pvr_sw_fence.h"

#define PVR_DUMPDEBUG_LOG(pfnDumpDebugPrintf, pvDumpDebugFile, fmt, ...) \
	do {                                                             \
		if (pfnDumpDebugPrintf)                                  \
			pfnDumpDebugPrintf(pvDumpDebugFile, fmt,         \
					   ## __VA_ARGS__);              \
		else                                                     \
			pr_err(fmt "\n", ## __VA_ARGS__);                \
	} while (0)

struct pvr_counting_fence_timeline {
	struct pvr_sw_fence_context *context;

	void *dbg_request_handle;

	spinlock_t active_fences_lock;
	u64 current_value; /* guarded by active_fences_lock */
	u64 next_value; /* guarded by active_fences_lock */
	struct list_head active_fences;

	struct kref kref;
};

struct pvr_counting_fence {
	u64 value;
	struct dma_fence *fence;
	struct list_head active_list_entry;
};

void pvr_counting_fence_timeline_dump_timeline(
	void *data,
	DUMPDEBUG_PRINTF_FUNC *dump_debug_printf,
	void *dump_debug_file)
{

	struct pvr_counting_fence_timeline *timeline =
		(struct pvr_counting_fence_timeline *) data;
	unsigned long flags;

	spin_lock_irqsave(&timeline->active_fences_lock, flags);

	PVR_DUMPDEBUG_LOG(dump_debug_printf,
					  dump_debug_file,
					  "TL:%s SeqNum: %llu/%llu",
					  pvr_sw_fence_context_name(
							  timeline->context),
					  timeline->current_value,
					  timeline->next_value);

	spin_unlock_irqrestore(&timeline->active_fences_lock, flags);
}

static void
pvr_counting_fence_timeline_debug_request(void *data, u32 verbosity,
			DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
			void *pvDumpDebugFile)
{
	struct pvr_counting_fence_timeline *timeline =
		(struct pvr_counting_fence_timeline *)data;
	struct pvr_counting_fence *obj;
	unsigned long flags;
	char value[128];

	if (DD_VERB_LVL_ENABLED(verbosity, DEBUG_REQUEST_VERBOSITY_MEDIUM)) {
		spin_lock_irqsave(&timeline->active_fences_lock, flags);
		pvr_sw_fence_context_value_str(timeline->context, value,
					       sizeof(value));
		PVR_DUMPDEBUG_LOG(pfnDumpDebugPrintf, pvDumpDebugFile,
				  "sw: %s @%s cur=%llu",
				  pvr_sw_fence_context_name(timeline->context),
				  value, timeline->current_value);
		list_for_each_entry(obj, &timeline->active_fences,
				    active_list_entry) {
			obj->fence->ops->fence_value_str(obj->fence,
							 value, sizeof(value));
			PVR_DUMPDEBUG_LOG(pfnDumpDebugPrintf, pvDumpDebugFile,
					  " @%s: val=%llu", value, obj->value);
		}
		spin_unlock_irqrestore(&timeline->active_fences_lock, flags);
	}
}

struct pvr_counting_fence_timeline *pvr_counting_fence_timeline_create(
	const char *name)
{
	PVRSRV_ERROR srv_err;
	struct pvr_counting_fence_timeline *timeline =
		kzalloc(sizeof(*timeline), GFP_KERNEL);

	if (!timeline)
		goto err_out;

	timeline->context = pvr_sw_fence_context_create(name,
							"pvr_sw_sync");
	if (!timeline->context)
		goto err_free_timeline;

	srv_err = PVRSRVRegisterDriverDbgRequestNotify(
				&timeline->dbg_request_handle,
				pvr_counting_fence_timeline_debug_request,
				DEBUG_REQUEST_LINUXFENCE,
				timeline);
	if (srv_err != PVRSRV_OK) {
		pr_err("%s: failed to register debug request callback (%s)\n",
			   __func__, PVRSRVGetErrorString(srv_err));
		goto err_free_timeline_ctx;
	}

	timeline->current_value = 0;
	timeline->next_value = 1;
	kref_init(&timeline->kref);
	spin_lock_init(&timeline->active_fences_lock);
	INIT_LIST_HEAD(&timeline->active_fences);

err_out:
	return timeline;

err_free_timeline_ctx:
	pvr_sw_fence_context_destroy(timeline->context);

err_free_timeline:
	kfree(timeline);
	timeline = NULL;
	goto err_out;
}

void pvr_counting_fence_timeline_force_complete(
	struct pvr_counting_fence_timeline *timeline)
{
	struct list_head *entry, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&timeline->active_fences_lock, flags);

#if defined(DEBUG) && !defined(SUPPORT_AUTOVZ)
	/* This is just a safety measure. Normally we should never see any
	 * unsignaled sw fences when we come here. Warn if we still do!
	 */
	WARN_ON(!list_empty(&timeline->active_fences));
#endif

	list_for_each_safe(entry, tmp, &timeline->active_fences) {
		struct pvr_counting_fence *fence =
			list_entry(entry, struct pvr_counting_fence,
			active_list_entry);
		dma_fence_signal(fence->fence);
		dma_fence_put(fence->fence);
		fence->fence = NULL;
		list_del(&fence->active_list_entry);
		kfree(fence);
	}
	spin_unlock_irqrestore(&timeline->active_fences_lock, flags);
}

static void pvr_counting_fence_timeline_destroy(
	struct kref *kref)
{
	struct pvr_counting_fence_timeline *timeline =
		container_of(kref, struct pvr_counting_fence_timeline, kref);

	WARN_ON(!list_empty(&timeline->active_fences));

	PVRSRVUnregisterDriverDbgRequestNotify(timeline->dbg_request_handle);

	pvr_sw_fence_context_destroy(timeline->context);
	kfree(timeline);
}

void pvr_counting_fence_timeline_put(
	struct pvr_counting_fence_timeline *timeline)
{
	kref_put(&timeline->kref, pvr_counting_fence_timeline_destroy);
}

struct pvr_counting_fence_timeline *pvr_counting_fence_timeline_get(
	struct pvr_counting_fence_timeline *timeline)
{
	if (!timeline)
		return NULL;
	kref_get(&timeline->kref);
	return timeline;
}

struct dma_fence *pvr_counting_fence_create(
	struct pvr_counting_fence_timeline *timeline, u64 *sync_pt_idx)
{
	unsigned long flags;
	struct dma_fence *sw_fence;
	struct pvr_counting_fence *fence = kmalloc(sizeof(*fence), GFP_KERNEL);

	if (!fence)
		return NULL;

	sw_fence = pvr_sw_fence_create(timeline->context);
	if (!sw_fence)
		goto err_free_fence;

	fence->fence = dma_fence_get(sw_fence);

	spin_lock_irqsave(&timeline->active_fences_lock, flags);

	fence->value = timeline->next_value++;
	if (sync_pt_idx)
		*sync_pt_idx = fence->value;

	list_add_tail(&fence->active_list_entry, &timeline->active_fences);

	spin_unlock_irqrestore(&timeline->active_fences_lock, flags);

	/* Counting fences can be signalled any time after creation */
	dma_fence_enable_sw_signaling(sw_fence);

	return sw_fence;

err_free_fence:
	kfree(fence);
	return NULL;
}

bool pvr_counting_fence_timeline_inc(
	struct pvr_counting_fence_timeline *timeline, u64 *sync_pt_idx)
{
	struct list_head *entry, *tmp;
	unsigned long flags;
	bool res;

	spin_lock_irqsave(&timeline->active_fences_lock, flags);

	if (timeline->current_value == timeline->next_value-1) {
		res = false;
		goto exit_unlock;
	}

	timeline->current_value++;

	if (sync_pt_idx)
		*sync_pt_idx = timeline->current_value;

	list_for_each_safe(entry, tmp, &timeline->active_fences) {
		struct pvr_counting_fence *fence =
			list_entry(entry, struct pvr_counting_fence,
			active_list_entry);
		if (fence->value <= timeline->current_value) {
			dma_fence_signal(fence->fence);
			dma_fence_put(fence->fence);
			fence->fence = NULL;
			list_del(&fence->active_list_entry);
			kfree(fence);
		}
	}

	res = true;

exit_unlock:
	spin_unlock_irqrestore(&timeline->active_fences_lock, flags);

	return res;
}
