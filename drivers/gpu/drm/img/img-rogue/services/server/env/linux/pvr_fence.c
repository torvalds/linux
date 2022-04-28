/*
 * @File
 * @Title       PowerVR Linux fence interface
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
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "pvr_fence.h"
#include "services_kernel_client.h"
#include "sync_checkpoint_external.h"

#define CREATE_TRACE_POINTS
#include "pvr_fence_trace.h"

/* This header must always be included last */
#include "kernel_compatibility.h"

/* Global kmem_cache for pvr_fence object allocations */
static struct kmem_cache *pvr_fence_cache;
static DEFINE_MUTEX(pvr_fence_cache_mutex);
static u32 pvr_fence_cache_refcount;

#define PVR_DUMPDEBUG_LOG(pfnDumpDebugPrintf, pvDumpDebugFile, fmt, ...) \
	do {                                                             \
		if (pfnDumpDebugPrintf)                                  \
			pfnDumpDebugPrintf(pvDumpDebugFile, fmt,         \
					   ## __VA_ARGS__);              \
		else                                                     \
			pr_err(fmt "\n", ## __VA_ARGS__);                \
	} while (0)

static inline void
pvr_fence_sync_signal(struct pvr_fence *pvr_fence, u32 fence_sync_flags)
{
	SyncCheckpointSignal(pvr_fence->sync_checkpoint, fence_sync_flags);
}

static inline bool
pvr_fence_sync_is_signaled(struct pvr_fence *pvr_fence, u32 fence_sync_flags)
{
	return SyncCheckpointIsSignalled(pvr_fence->sync_checkpoint,
					 fence_sync_flags);
}

static inline u32
pvr_fence_sync_value(struct pvr_fence *pvr_fence)
{
	if (SyncCheckpointIsErrored(pvr_fence->sync_checkpoint,
				    PVRSRV_FENCE_FLAG_SUPPRESS_HWP_PKT))
		return PVRSRV_SYNC_CHECKPOINT_ERRORED;
	else if (SyncCheckpointIsSignalled(pvr_fence->sync_checkpoint,
					   PVRSRV_FENCE_FLAG_SUPPRESS_HWP_PKT))
		return PVRSRV_SYNC_CHECKPOINT_SIGNALLED;
	else
		return PVRSRV_SYNC_CHECKPOINT_ACTIVE;
}

static void
pvr_fence_context_check_status(struct work_struct *data)
{
	PVRSRVCheckStatus(NULL);
}

void
pvr_context_value_str(struct pvr_fence_context *fctx, char *str, int size)
{
	snprintf(str, size,
		 "%u ctx=%llu refs=%u",
		 atomic_read(&fctx->fence_seqno),
		 fctx->fence_context,
		 refcount_read(&fctx->kref.refcount));
}

static void
pvr_fence_context_fences_dump(struct pvr_fence_context *fctx,
			      DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
			      void *pvDumpDebugFile)
{
	struct pvr_fence *pvr_fence;
	unsigned long flags;
	char value[128];

	spin_lock_irqsave(&fctx->list_lock, flags);
	pvr_context_value_str(fctx, value, sizeof(value));
	PVR_DUMPDEBUG_LOG(pfnDumpDebugPrintf, pvDumpDebugFile,
			 "%s: @%s", fctx->name, value);
	list_for_each_entry(pvr_fence, &fctx->fence_list, fence_head) {
		struct dma_fence *fence = pvr_fence->fence;
		const char *timeline_value_str = "unknown timeline value";
		const char *fence_value_str = "unknown fence value";

		pvr_fence->base.ops->fence_value_str(&pvr_fence->base, value,
						     sizeof(value));
		PVR_DUMPDEBUG_LOG(pfnDumpDebugPrintf, pvDumpDebugFile,
				  " @%s", value);

		if (is_pvr_fence(fence))
			continue;

		if (fence->ops->timeline_value_str) {
			fence->ops->timeline_value_str(fence, value,
						       sizeof(value));
			timeline_value_str = value;
		}

		PVR_DUMPDEBUG_LOG(pfnDumpDebugPrintf, pvDumpDebugFile,
				  " | %s: %s (driver: %s)",
				  fence->ops->get_timeline_name(fence),
				  timeline_value_str,
				  fence->ops->get_driver_name(fence));

		if (fence->ops->fence_value_str) {
			fence->ops->fence_value_str(fence, value,
						    sizeof(value));
			fence_value_str = value;
		}

		PVR_DUMPDEBUG_LOG(pfnDumpDebugPrintf, pvDumpDebugFile,
				  " |  @%s (foreign)", value);
	}
	spin_unlock_irqrestore(&fctx->list_lock, flags);
}

static inline unsigned int
pvr_fence_context_seqno_next(struct pvr_fence_context *fctx)
{
	return atomic_inc_return(&fctx->fence_seqno) - 1;
}

/* This function prepends seqno to fence name */
static inline void
pvr_fence_prepare_name(char *fence_name, size_t fence_name_size,
		const char *name, unsigned int seqno)
{
	unsigned int len;

	len = OSStringUINT32ToStr(fence_name, fence_name_size, seqno);
	if (likely((len > 0) && (fence_name_size >= (len + 1)))) {
		fence_name[len] = '-';
		fence_name[len + 1] = '\0';
	}
	strlcat(fence_name, name, fence_name_size);
}

static void
pvr_fence_sched_free(struct rcu_head *rcu)
{
	struct pvr_fence *pvr_fence = container_of(rcu, struct pvr_fence, rcu);

	kmem_cache_free(pvr_fence_cache, pvr_fence);
}

static inline void
pvr_fence_context_free_deferred(struct pvr_fence_context *fctx)
{
	struct pvr_fence *pvr_fence, *tmp;
	LIST_HEAD(deferred_free_list);
	unsigned long flags;

	spin_lock_irqsave(&fctx->list_lock, flags);
	list_for_each_entry_safe(pvr_fence, tmp,
				 &fctx->deferred_free_list,
				 fence_head)
		list_move(&pvr_fence->fence_head, &deferred_free_list);
	spin_unlock_irqrestore(&fctx->list_lock, flags);

	list_for_each_entry_safe(pvr_fence, tmp,
				 &deferred_free_list,
				 fence_head) {
		list_del(&pvr_fence->fence_head);
		SyncCheckpointFree(pvr_fence->sync_checkpoint);
		call_rcu(&pvr_fence->rcu, pvr_fence_sched_free);
		module_put(THIS_MODULE);
	}
}

void
pvr_fence_context_free_deferred_callback(void *data)
{
	struct pvr_fence_context *fctx = (struct pvr_fence_context *)data;

	/*
	 * Free up any fence objects we have deferred freeing.
	 */
	pvr_fence_context_free_deferred(fctx);
}

static void
pvr_fence_context_signal_fences(void *data)
{
	struct pvr_fence_context *fctx = (struct pvr_fence_context *)data;
	struct pvr_fence *pvr_fence, *tmp;
	unsigned long flags1;

	LIST_HEAD(signal_list);

	/*
	 * We can't call fence_signal while holding the lock as we can end up
	 * in a situation whereby pvr_fence_foreign_signal_sync, which also
	 * takes the list lock, ends up being called as a result of the
	 * fence_signal below, i.e. fence_signal(fence) -> fence->callback()
	 *  -> fence_signal(foreign_fence) -> foreign_fence->callback() where
	 * the foreign_fence callback is pvr_fence_foreign_signal_sync.
	 *
	 * So extract the items we intend to signal and add them to their own
	 * queue.
	 */
	spin_lock_irqsave(&fctx->list_lock, flags1);
	list_for_each_entry_safe(pvr_fence, tmp, &fctx->signal_list, signal_head) {
		if (pvr_fence_sync_is_signaled(pvr_fence, PVRSRV_FENCE_FLAG_SUPPRESS_HWP_PKT))
			list_move_tail(&pvr_fence->signal_head, &signal_list);
	}
	spin_unlock_irqrestore(&fctx->list_lock, flags1);

	list_for_each_entry_safe(pvr_fence, tmp, &signal_list, signal_head) {

		PVR_FENCE_TRACE(&pvr_fence->base, "signalled fence (%s)\n",
				pvr_fence->name);
		trace_pvr_fence_signal_fence(pvr_fence);
		spin_lock_irqsave(&pvr_fence->fctx->list_lock, flags1);
		list_del(&pvr_fence->signal_head);
		spin_unlock_irqrestore(&pvr_fence->fctx->list_lock, flags1);
		dma_fence_signal(pvr_fence->fence);
		dma_fence_put(pvr_fence->fence);
	}

	/*
	 * Take this opportunity to free up any fence objects we
	 * have deferred freeing.
	 */
	pvr_fence_context_free_deferred(fctx);
}

void
pvr_fence_context_signal_fences_nohw(void *data)
{
	pvr_fence_context_signal_fences(data);
}

static void
pvr_fence_context_destroy_work(struct work_struct *data)
{
	struct pvr_fence_context *fctx =
		container_of(data, struct pvr_fence_context, destroy_work);

	pvr_fence_context_free_deferred(fctx);

	if (WARN_ON(!list_empty_careful(&fctx->fence_list)))
		pvr_fence_context_fences_dump(fctx, NULL, NULL);

	PVRSRVUnregisterDbgRequestNotify(fctx->dbg_request_handle);
	PVRSRVUnregisterCmdCompleteNotify(fctx->cmd_complete_handle);

	// wait for all fences to be freed before kmem_cache_destroy() is called
	rcu_barrier();

	/* Destroy pvr_fence object cache, if no one is using it */
	WARN_ON(pvr_fence_cache == NULL);
	mutex_lock(&pvr_fence_cache_mutex);
	if (--pvr_fence_cache_refcount == 0)
		kmem_cache_destroy(pvr_fence_cache);
	mutex_unlock(&pvr_fence_cache_mutex);

	kfree(fctx);
}

static void
pvr_fence_context_debug_request(void *data, u32 verbosity,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile)
{
	struct pvr_fence_context *fctx = (struct pvr_fence_context *)data;

	if (DD_VERB_LVL_ENABLED(verbosity, DEBUG_REQUEST_VERBOSITY_MEDIUM))
		pvr_fence_context_fences_dump(fctx, pfnDumpDebugPrintf,
					      pvDumpDebugFile);
}

/**
 * pvr_fence_context_create - creates a PVR fence context
 * @dev_cookie: services device cookie
 * @name: context name (used for debugging)
 *
 * Creates a PVR fence context that can be used to create PVR fences or to
 * create PVR fences from an existing fence.
 *
 * pvr_fence_context_destroy should be called to clean up the fence context.
 *
 * Returns NULL if a context cannot be created.
 */
struct pvr_fence_context *
pvr_fence_context_create(void *dev_cookie,
			 struct workqueue_struct *fence_status_wq,
			 const char *name)
{
	struct pvr_fence_context *fctx;
	PVRSRV_ERROR srv_err;

	fctx = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return NULL;

	spin_lock_init(&fctx->lock);
	atomic_set(&fctx->fence_seqno, 0);
	INIT_WORK(&fctx->check_status_work, pvr_fence_context_check_status);
	INIT_WORK(&fctx->destroy_work, pvr_fence_context_destroy_work);
	spin_lock_init(&fctx->list_lock);
	INIT_LIST_HEAD(&fctx->signal_list);
	INIT_LIST_HEAD(&fctx->fence_list);
	INIT_LIST_HEAD(&fctx->deferred_free_list);

	fctx->fence_wq = fence_status_wq;

	fctx->fence_context = dma_fence_context_alloc(1);
	strlcpy(fctx->name, name, sizeof(fctx->name));

	srv_err = PVRSRVRegisterCmdCompleteNotify(&fctx->cmd_complete_handle,
				pvr_fence_context_signal_fences,
				fctx);
	if (srv_err != PVRSRV_OK) {
		pr_err("%s: failed to register command complete callback (%s)\n",
		       __func__, PVRSRVGetErrorString(srv_err));
		goto err_free_fctx;
	}

	/* Create pvr_fence object cache, if not already created */
	mutex_lock(&pvr_fence_cache_mutex);
	if (pvr_fence_cache_refcount == 0) {
		pvr_fence_cache = KMEM_CACHE(pvr_fence, 0);
		if (!pvr_fence_cache) {
			pr_err("%s: failed to allocate pvr_fence cache\n",
					__func__);
			mutex_unlock(&pvr_fence_cache_mutex);
			goto err_unregister_cmd_complete_notify;
		}
	}
	pvr_fence_cache_refcount++;
	mutex_unlock(&pvr_fence_cache_mutex);

	srv_err = PVRSRVRegisterDbgRequestNotify(&fctx->dbg_request_handle,
				dev_cookie,
				pvr_fence_context_debug_request,
				DEBUG_REQUEST_LINUXFENCE,
				fctx);
	if (srv_err != PVRSRV_OK) {
		pr_err("%s: failed to register debug request callback (%s)\n",
		       __func__, PVRSRVGetErrorString(srv_err));
		goto err_free_pvr_fence_cache;
	}

	kref_init(&fctx->kref);

	PVR_FENCE_CTX_TRACE(fctx, "created fence context (%s)\n", name);
	trace_pvr_fence_context_create(fctx);

	return fctx;

err_free_pvr_fence_cache:
	mutex_lock(&pvr_fence_cache_mutex);
	if (--pvr_fence_cache_refcount == 0)
		kmem_cache_destroy(pvr_fence_cache);
	mutex_unlock(&pvr_fence_cache_mutex);
err_unregister_cmd_complete_notify:
	PVRSRVUnregisterCmdCompleteNotify(fctx->cmd_complete_handle);
err_free_fctx:
	kfree(fctx);
	return NULL;
}

static void pvr_fence_context_destroy_kref(struct kref *kref)
{
	struct pvr_fence_context *fctx =
		container_of(kref, struct pvr_fence_context, kref);

	PVR_FENCE_CTX_TRACE(fctx, "destroyed fence context (%s)\n", fctx->name);

	trace_pvr_fence_context_destroy_kref(fctx);

	schedule_work(&fctx->destroy_work);
}

/**
 * pvr_fence_context_destroy - destroys a context
 * @fctx: PVR fence context to destroy
 *
 * Destroys a PVR fence context with the expectation that all fences have been
 * destroyed.
 */
void
pvr_fence_context_destroy(struct pvr_fence_context *fctx)
{
	trace_pvr_fence_context_destroy(fctx);

	kref_put(&fctx->kref, pvr_fence_context_destroy_kref);
}

static const char *
pvr_fence_get_driver_name(struct dma_fence *fence)
{
	return PVR_LDM_DRIVER_REGISTRATION_NAME;
}

static const char *
pvr_fence_get_timeline_name(struct dma_fence *fence)
{
	struct pvr_fence *pvr_fence = to_pvr_fence(fence);

	if (pvr_fence)
		return pvr_fence->fctx->name;
	return NULL;
}

static
void pvr_fence_fence_value_str(struct dma_fence *fence, char *str, int size)
{
	struct pvr_fence *pvr_fence = to_pvr_fence(fence);

	if (!pvr_fence)
		return;

	snprintf(str, size,
		 "%llu: (%s%s) refs=%u fwaddr=%#08x enqueue=%u status=%-9s %s%s",
		 (u64) pvr_fence->fence->seqno,
		 test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
			  &pvr_fence->fence->flags) ? "+" : "-",
		 test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
			  &pvr_fence->fence->flags) ? "+" : "-",
		 refcount_read(&pvr_fence->fence->refcount.refcount),
		 SyncCheckpointGetFirmwareAddr(
			 pvr_fence->sync_checkpoint),
		 SyncCheckpointGetEnqueuedCount(pvr_fence->sync_checkpoint),
		 SyncCheckpointGetStateString(pvr_fence->sync_checkpoint),
		 pvr_fence->name,
		 (&pvr_fence->base != pvr_fence->fence) ?
		 "(foreign)" : "");
}

static
void pvr_fence_timeline_value_str(struct dma_fence *fence, char *str, int size)
{
	struct pvr_fence *pvr_fence = to_pvr_fence(fence);

	if (pvr_fence)
		pvr_context_value_str(pvr_fence->fctx, str, size);
}

static bool
pvr_fence_enable_signaling(struct dma_fence *fence)
{
	struct pvr_fence *pvr_fence = to_pvr_fence(fence);
	unsigned long flags;

	if (!pvr_fence)
		return false;

	WARN_ON_SMP(!spin_is_locked(&pvr_fence->fctx->lock));

	if (pvr_fence_sync_is_signaled(pvr_fence, PVRSRV_FENCE_FLAG_SUPPRESS_HWP_PKT))
		return false;

	dma_fence_get(&pvr_fence->base);

	spin_lock_irqsave(&pvr_fence->fctx->list_lock, flags);
	list_add_tail(&pvr_fence->signal_head, &pvr_fence->fctx->signal_list);
	spin_unlock_irqrestore(&pvr_fence->fctx->list_lock, flags);

	PVR_FENCE_TRACE(&pvr_fence->base, "signalling enabled (%s)\n",
			pvr_fence->name);
	trace_pvr_fence_enable_signaling(pvr_fence);

	return true;
}

static bool
pvr_fence_is_signaled(struct dma_fence *fence)
{
	struct pvr_fence *pvr_fence = to_pvr_fence(fence);

	if (pvr_fence)
		return pvr_fence_sync_is_signaled(pvr_fence,
						  PVRSRV_FENCE_FLAG_SUPPRESS_HWP_PKT);
	return false;
}

static void
pvr_fence_release(struct dma_fence *fence)
{
	struct pvr_fence *pvr_fence = to_pvr_fence(fence);
	unsigned long flags;

	if (pvr_fence) {
		struct pvr_fence_context *fctx = pvr_fence->fctx;

		PVR_FENCE_TRACE(&pvr_fence->base, "released fence (%s)\n",
				pvr_fence->name);
		trace_pvr_fence_release(pvr_fence);

		spin_lock_irqsave(&fctx->list_lock, flags);
		list_move(&pvr_fence->fence_head,
			  &fctx->deferred_free_list);
		spin_unlock_irqrestore(&fctx->list_lock, flags);

		kref_put(&fctx->kref, pvr_fence_context_destroy_kref);
	}
}

const struct dma_fence_ops pvr_fence_ops = {
	.get_driver_name = pvr_fence_get_driver_name,
	.get_timeline_name = pvr_fence_get_timeline_name,
	.fence_value_str = pvr_fence_fence_value_str,
	.timeline_value_str = pvr_fence_timeline_value_str,
	.enable_signaling = pvr_fence_enable_signaling,
	.signaled = pvr_fence_is_signaled,
	.wait = dma_fence_default_wait,
	.release = pvr_fence_release,
};

/**
 * pvr_fence_create - creates a PVR fence
 * @fctx: PVR fence context on which the PVR fence should be created
 * @sync_checkpoint_ctx: context in which to create sync checkpoints
 * @timeline_fd: timeline on which the PVR fence should be created
 * @name: PVR fence name (used for debugging)
 *
 * Creates a PVR fence.
 *
 * Once the fence is finished with, pvr_fence_destroy should be called.
 *
 * Returns NULL if a PVR fence cannot be created.
 */
struct pvr_fence *
pvr_fence_create(struct pvr_fence_context *fctx,
		struct _SYNC_CHECKPOINT_CONTEXT *sync_checkpoint_ctx,
		int timeline_fd, const char *name)
{
	struct pvr_fence *pvr_fence;
	unsigned int seqno;
	unsigned long flags;
	PVRSRV_ERROR srv_err;

	if (!try_module_get(THIS_MODULE))
		goto err_exit;

	/* Note: As kmem_cache is used to allocate pvr_fence objects,
	 * make sure that all members of pvr_fence struct are initialized
	 * here
	 */
	pvr_fence = kmem_cache_alloc(pvr_fence_cache, GFP_KERNEL);
	if (unlikely(!pvr_fence))
		goto err_module_put;

	srv_err = SyncCheckpointAlloc(sync_checkpoint_ctx,
				      (PVRSRV_TIMELINE) timeline_fd, PVRSRV_NO_FENCE,
				      name, &pvr_fence->sync_checkpoint);
	if (unlikely(srv_err != PVRSRV_OK))
		goto err_free_fence;

	INIT_LIST_HEAD(&pvr_fence->fence_head);
	INIT_LIST_HEAD(&pvr_fence->signal_head);
	pvr_fence->fctx = fctx;
	seqno = pvr_fence_context_seqno_next(fctx);
	/* Add the seqno to the fence name for easier debugging */
	pvr_fence_prepare_name(pvr_fence->name, sizeof(pvr_fence->name),
			name, seqno);

	/* Reset cb to zero */
	memset(&pvr_fence->cb, 0, sizeof(pvr_fence->cb));
	pvr_fence->fence = &pvr_fence->base;

	dma_fence_init(&pvr_fence->base, &pvr_fence_ops, &fctx->lock,
		       fctx->fence_context, seqno);

	spin_lock_irqsave(&fctx->list_lock, flags);
	list_add_tail(&pvr_fence->fence_head, &fctx->fence_list);
	spin_unlock_irqrestore(&fctx->list_lock, flags);

	kref_get(&fctx->kref);

	PVR_FENCE_TRACE(&pvr_fence->base, "created fence (%s)\n", name);
	trace_pvr_fence_create(pvr_fence);

	return pvr_fence;

err_free_fence:
	kmem_cache_free(pvr_fence_cache, pvr_fence);
err_module_put:
	module_put(THIS_MODULE);
err_exit:
	return NULL;
}

static const char *
pvr_fence_foreign_get_driver_name(struct dma_fence *fence)
{
	return PVR_LDM_DRIVER_REGISTRATION_NAME;
}

static const char *
pvr_fence_foreign_get_timeline_name(struct dma_fence *fence)
{
	return "foreign";
}

static
void pvr_fence_foreign_fence_value_str(struct dma_fence *fence, char *str,
				       int size)
{
	struct pvr_fence *pvr_fence = to_pvr_fence(fence);
	u32 sync_addr = 0;
	u32 sync_value_next;

	if (WARN_ON(!pvr_fence))
		return;

	sync_addr = SyncCheckpointGetFirmwareAddr(pvr_fence->sync_checkpoint);
	sync_value_next = PVRSRV_SYNC_CHECKPOINT_SIGNALLED;

	/*
	 * Include the fence flag bits from the foreign fence instead of our
	 * shadow copy. This is done as the shadow fence flag bits aren't used.
	 */
	snprintf(str, size,
		 "%llu: (%s%s) refs=%u fwaddr=%#08x cur=%#08x nxt=%#08x %s",
		 (u64) fence->seqno,
		 test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
			  &pvr_fence->fence->flags) ? "+" : "-",
		 test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
			  &pvr_fence->fence->flags) ? "+" : "-",
		 refcount_read(&fence->refcount.refcount),
		 sync_addr,
		 pvr_fence_sync_value(pvr_fence),
		 sync_value_next,
		 pvr_fence->name);
}

static
void pvr_fence_foreign_timeline_value_str(struct dma_fence *fence, char *str,
					  int size)
{
	struct pvr_fence *pvr_fence = to_pvr_fence(fence);

	if (pvr_fence)
		pvr_context_value_str(pvr_fence->fctx, str, size);
}

static bool
pvr_fence_foreign_enable_signaling(struct dma_fence *fence)
{
	WARN_ON("cannot enable signalling on foreign fence");
	return false;
}

static signed long
pvr_fence_foreign_wait(struct dma_fence *fence, bool intr, signed long timeout)
{
	WARN_ON("cannot wait on foreign fence");
	return 0;
}

static void
pvr_fence_foreign_release(struct dma_fence *fence)
{
	struct pvr_fence *pvr_fence = to_pvr_fence(fence);
	unsigned long flags;

	if (pvr_fence) {
		struct pvr_fence_context *fctx = pvr_fence->fctx;
		struct dma_fence *foreign_fence = pvr_fence->fence;

		PVR_FENCE_TRACE(&pvr_fence->base,
				"released fence for foreign fence %llu#%d (%s)\n",
				(u64) pvr_fence->fence->context,
				pvr_fence->fence->seqno, pvr_fence->name);
		trace_pvr_fence_foreign_release(pvr_fence);

		spin_lock_irqsave(&fctx->list_lock, flags);
		list_move(&pvr_fence->fence_head,
			  &fctx->deferred_free_list);
		spin_unlock_irqrestore(&fctx->list_lock, flags);

		dma_fence_put(foreign_fence);

		kref_put(&fctx->kref,
			 pvr_fence_context_destroy_kref);
	}
}

const struct dma_fence_ops pvr_fence_foreign_ops = {
	.get_driver_name = pvr_fence_foreign_get_driver_name,
	.get_timeline_name = pvr_fence_foreign_get_timeline_name,
	.fence_value_str = pvr_fence_foreign_fence_value_str,
	.timeline_value_str = pvr_fence_foreign_timeline_value_str,
	.enable_signaling = pvr_fence_foreign_enable_signaling,
	.wait = pvr_fence_foreign_wait,
	.release = pvr_fence_foreign_release,
};

static void
pvr_fence_foreign_signal_sync(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct pvr_fence *pvr_fence = container_of(cb, struct pvr_fence, cb);
	struct pvr_fence_context *fctx = pvr_fence->fctx;

	WARN_ON_ONCE(is_pvr_fence(fence));

	/* Callback registered by dma_fence_add_callback can be called from an atomic ctx */
	pvr_fence_sync_signal(pvr_fence, PVRSRV_FENCE_FLAG_CTX_ATOMIC);

	trace_pvr_fence_foreign_signal(pvr_fence);

	queue_work(fctx->fence_wq, &fctx->check_status_work);

	PVR_FENCE_TRACE(&pvr_fence->base,
			"foreign fence %llu#%d signalled (%s)\n",
			(u64) pvr_fence->fence->context,
			pvr_fence->fence->seqno, pvr_fence->name);

	/* Drop the reference on the base fence */
	dma_fence_put(&pvr_fence->base);
}

/**
 * pvr_fence_create_from_fence - creates a PVR fence from a fence
 * @fctx: PVR fence context on which the PVR fence should be created
 * @sync_checkpoint_ctx: context in which to create sync checkpoints
 * @fence: fence from which the PVR fence should be created
 * @fence_fd: fd for the sync file to which the fence belongs. If it doesn't
 *            belong to a sync file then PVRSRV_NO_FENCE should be given
 *            instead.
 * @name: PVR fence name (used for debugging)
 *
 * Creates a PVR fence from an existing fence. If the fence is a foreign fence,
 * i.e. one that doesn't originate from a PVR fence context, then a new PVR
 * fence will be created using the specified sync_checkpoint_context.
 * Otherwise, a reference will be taken on the underlying fence and the PVR
 * fence will be returned.
 *
 * Once the fence is finished with, pvr_fence_destroy should be called.
 *
 * Returns NULL if a PVR fence cannot be created.
 */

struct pvr_fence *
pvr_fence_create_from_fence(struct pvr_fence_context *fctx,
			    struct _SYNC_CHECKPOINT_CONTEXT *sync_checkpoint_ctx,
			    struct dma_fence *fence,
			    PVRSRV_FENCE fence_fd,
			    const char *name)
{
	struct pvr_fence *pvr_fence = to_pvr_fence(fence);
	unsigned int seqno;
	unsigned long flags;
	PVRSRV_ERROR srv_err;
	int err;

	if (pvr_fence) {
		if (WARN_ON(fence->ops == &pvr_fence_foreign_ops))
			return NULL;
		dma_fence_get(fence);

		PVR_FENCE_TRACE(fence, "created fence from PVR fence (%s)\n",
				name);
		return pvr_fence;
	}

	if (!try_module_get(THIS_MODULE))
		goto err_exit;

	/* Note: As kmem_cache is used to allocate pvr_fence objects,
	 * make sure that all members of pvr_fence struct are initialized
	 * here
	 */
	pvr_fence = kmem_cache_alloc(pvr_fence_cache, GFP_KERNEL);
	if (!pvr_fence)
		goto err_module_put;

	srv_err = SyncCheckpointAlloc(sync_checkpoint_ctx,
					  SYNC_CHECKPOINT_FOREIGN_CHECKPOINT,
					  fence_fd,
					  name, &pvr_fence->sync_checkpoint);
	if (srv_err != PVRSRV_OK)
		goto err_free_pvr_fence;

	INIT_LIST_HEAD(&pvr_fence->fence_head);
	INIT_LIST_HEAD(&pvr_fence->signal_head);
	pvr_fence->fctx = fctx;
	pvr_fence->fence = dma_fence_get(fence);
	seqno = pvr_fence_context_seqno_next(fctx);
	/* Add the seqno to the fence name for easier debugging */
	pvr_fence_prepare_name(pvr_fence->name, sizeof(pvr_fence->name),
			name, seqno);

	/*
	 * We use the base fence to refcount the PVR fence and to do the
	 * necessary clean up once the refcount drops to 0.
	 */
	dma_fence_init(&pvr_fence->base, &pvr_fence_foreign_ops, &fctx->lock,
		       fctx->fence_context, seqno);

	/*
	 * Take an extra reference on the base fence that gets dropped when the
	 * foreign fence is signalled.
	 */
	dma_fence_get(&pvr_fence->base);

	spin_lock_irqsave(&fctx->list_lock, flags);
	list_add_tail(&pvr_fence->fence_head, &fctx->fence_list);
	spin_unlock_irqrestore(&fctx->list_lock, flags);
	kref_get(&fctx->kref);

	PVR_FENCE_TRACE(&pvr_fence->base,
			"created fence from foreign fence %llu#%d (%s)\n",
			(u64) pvr_fence->fence->context,
			pvr_fence->fence->seqno, name);

	err = dma_fence_add_callback(fence, &pvr_fence->cb,
				     pvr_fence_foreign_signal_sync);
	if (err) {
		if (err != -ENOENT) {
			pr_err("%s: failed to add fence callback (err=%d)",
			       __func__, err);
			goto err_put_ref;
		}

		/*
		 * The fence has already signalled so set the sync as signalled.
		 * The "signalled" hwperf packet should be emitted because the
		 * callback won't be called for already signalled fence hence,
		 * PVRSRV_FENCE_FLAG_NONE flag.
		 */
		pvr_fence_sync_signal(pvr_fence, PVRSRV_FENCE_FLAG_NONE);
		PVR_FENCE_TRACE(&pvr_fence->base,
				"foreign fence %llu#%d already signaled (%s)\n",
				(u64) pvr_fence->fence->context,
				pvr_fence->fence->seqno,
				name);
		dma_fence_put(&pvr_fence->base);
	}

	trace_pvr_fence_foreign_create(pvr_fence);

	return pvr_fence;

err_put_ref:
	kref_put(&fctx->kref, pvr_fence_context_destroy_kref);
	spin_lock_irqsave(&fctx->list_lock, flags);
	list_del(&pvr_fence->fence_head);
	spin_unlock_irqrestore(&fctx->list_lock, flags);
	SyncCheckpointFree(pvr_fence->sync_checkpoint);
err_free_pvr_fence:
	kmem_cache_free(pvr_fence_cache, pvr_fence);
err_module_put:
	module_put(THIS_MODULE);
err_exit:
	return NULL;
}

/**
 * pvr_fence_destroy - destroys a PVR fence
 * @pvr_fence: PVR fence to destroy
 *
 * Destroys a PVR fence. Upon return, the PVR fence may still exist if something
 * else still references the underlying fence, e.g. a reservation object, or if
 * software signalling has been enabled and the fence hasn't yet been signalled.
 */
void
pvr_fence_destroy(struct pvr_fence *pvr_fence)
{
	PVR_FENCE_TRACE(&pvr_fence->base, "destroyed fence (%s)\n",
			pvr_fence->name);

	dma_fence_put(&pvr_fence->base);
}

/**
 * pvr_fence_sw_signal - signals a PVR fence sync
 * @pvr_fence: PVR fence to signal
 *
 * Sets the PVR fence sync value to signalled.
 *
 * Returns -EINVAL if the PVR fence represents a foreign fence.
 */
int
pvr_fence_sw_signal(struct pvr_fence *pvr_fence)
{
	if (!is_our_fence(pvr_fence->fctx, &pvr_fence->base))
		return -EINVAL;

	pvr_fence_sync_signal(pvr_fence, PVRSRV_FENCE_FLAG_NONE);

	queue_work(pvr_fence->fctx->fence_wq,
		   &pvr_fence->fctx->check_status_work);

	PVR_FENCE_TRACE(&pvr_fence->base, "sw set fence sync signalled (%s)\n",
			pvr_fence->name);

	return 0;
}

/**
 * pvr_fence_sw_error - errors the sync checkpoint backing a PVR fence
 * @pvr_fence: PVR fence to error
 *
 * Sets the PVR fence sync checkpoint value to errored.
 *
 * Returns -EINVAL if the PVR fence represents a foreign fence.
 */
int
pvr_fence_sw_error(struct pvr_fence *pvr_fence)
{
	if (!is_our_fence(pvr_fence->fctx, &pvr_fence->base))
		return -EINVAL;

	SyncCheckpointError(pvr_fence->sync_checkpoint, PVRSRV_FENCE_FLAG_NONE);
	PVR_FENCE_TRACE(&pvr_fence->base, "sw set fence sync errored (%s)\n",
			pvr_fence->name);

	return 0;
}

int
pvr_fence_get_checkpoints(struct pvr_fence **pvr_fences, u32 nr_fences,
			  struct _SYNC_CHECKPOINT **fence_checkpoints)
{
	struct _SYNC_CHECKPOINT **next_fence_checkpoint = fence_checkpoints;
	struct pvr_fence **next_pvr_fence = pvr_fences;
	int fence_checkpoint_idx;

	if (nr_fences > 0) {

		for (fence_checkpoint_idx = 0; fence_checkpoint_idx < nr_fences;
		     fence_checkpoint_idx++) {
			struct pvr_fence *next_fence = *next_pvr_fence++;
			*next_fence_checkpoint++ = next_fence->sync_checkpoint;
			/* Take reference on sync checkpoint (will be dropped
			 * later by kick code)
			 */
			SyncCheckpointTakeRef(next_fence->sync_checkpoint);
		}
	}

	return 0;
}

struct _SYNC_CHECKPOINT *
pvr_fence_get_checkpoint(struct pvr_fence *update_fence)
{
	return update_fence->sync_checkpoint;
}

/**
 * pvr_fence_dump_info_on_stalled_ufos - displays debug
 * information on a native fence associated with any of
 * the ufos provided. This function will be called from
 * pvr_sync_file.c if the driver determines any GPU work
 * is stuck waiting for a sync checkpoint representing a
 * foreign sync to be signalled.
 * @nr_ufos: number of ufos in vaddrs
 * @vaddrs:  array of FW addresses of UFOs which the
 *           driver is waiting on.
 *
 * Output debug information to kernel log on linux fences
 * which would be responsible for signalling the sync
 * checkpoints indicated by the ufo vaddresses.
 *
 * Returns the number of ufos in the array which were found
 * to be associated with foreign syncs.
 */
u32 pvr_fence_dump_info_on_stalled_ufos(struct pvr_fence_context *fctx,
					u32 nr_ufos, u32 *vaddrs)
{
	int our_ufo_ct = 0;
	struct pvr_fence *pvr_fence;
	unsigned long flags;

	spin_lock_irqsave(&fctx->list_lock, flags);
	/* dump info on any ufos in our active list */
	list_for_each_entry(pvr_fence, &fctx->fence_list, fence_head) {
		u32 *this_ufo_vaddr = vaddrs;
		int ufo_num;
		DUMPDEBUG_PRINTF_FUNC *pfnDummy = NULL;

		for (ufo_num = 0; ufo_num < nr_ufos; ufo_num++) {
			struct _SYNC_CHECKPOINT *checkpoint =
				pvr_fence->sync_checkpoint;
			const u32 fence_ufo_addr =
				SyncCheckpointGetFirmwareAddr(checkpoint);

			if (fence_ufo_addr != this_ufo_vaddr[ufo_num])
				continue;

			/* Dump sync info */
			PVR_DUMPDEBUG_LOG(pfnDummy, NULL,
					  "\tSyncID = %d, FWAddr = 0x%08x: TLID = %d (Foreign Fence - [%p] %s)",
					  SyncCheckpointGetId(checkpoint),
					  fence_ufo_addr,
					  SyncCheckpointGetTimeline(checkpoint),
					  pvr_fence->fence,
					  pvr_fence->name);
			our_ufo_ct++;
		}
	}
	spin_unlock_irqrestore(&fctx->list_lock, flags);
	return our_ufo_ct;
}
