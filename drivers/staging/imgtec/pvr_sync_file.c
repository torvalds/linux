/* -*- mode: c; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* vi: set ts=8 sw=8 sts=8: */
/*************************************************************************/ /*!
@File           pvr_sync_file.c
@Title          Kernel driver for Android's sync mechanism
@Codingstyle    LinuxKernel
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include "pvr_sync.h"
#include "pvr_fd_sync_kernel.h"
#include "services_kernel_client.h"

#include <linux/sync_file.h>
#include <linux/dma-fence.h>
#include <linux/dma-fence-array.h>

#include <linux/slab.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/syscalls.h>
#include <linux/miscdevice.h>
#include <linux/anon_inodes.h>

#include "kernel_compatibility.h"

#include <linux/kref.h>

#define for_each_sync_pt(s, f, c) \
	for ((c) = 0, (s) = (f)->num_fences == 0 ? \
		NULL : (struct sync_pt *)(f)->cbs[0].sync_pt; \
	     (c) < (f)->num_fences; \
	     (c)++,   (s) = (c) < (f)->num_fences ? \
		(struct sync_pt *)(f)->cbs[c].sync_pt : NULL)


/* #define DEBUG_OUTPUT 1 */

#ifdef DEBUG_OUTPUT
#define DPF(fmt, ...) pr_err("pvr_sync: " fmt "\n", __VA_ARGS__)
#else
#define DPF(fmt, ...) do {} while (0)
#endif

#define PVR_DUMPDEBUG_LOG(pfnDumpDebugPrintf, pvDumpDebugFile, fmt, ...) \
	do { \
		if (pfnDumpDebugPrintf) { \
			pfnDumpDebugPrintf(pvDumpDebugFile, fmt, __VA_ARGS__); \
		} else { \
			pr_info("pvr_sync: " fmt, __VA_ARGS__); \
		} \
	} while (0)

#define SYNC_MAX_POOL_SIZE 10

enum {
	SYNC_TL_TYPE = 0,
	SYNC_PT_FENCE_TYPE = 1,
	SYNC_PT_CLEANUP_TYPE = 2,
	SYNC_PT_FOREIGN_FENCE_TYPE = 3,
	SYNC_PT_FOREIGN_CLEANUP_TYPE = 4,
};

struct pvr_sync_append_data {
	u32					nr_updates;
	struct _RGXFWIF_DEV_VIRTADDR_		*update_ufo_addresses;
	u32					*update_values;
	u32					nr_checks;
	struct _RGXFWIF_DEV_VIRTADDR_		*check_ufo_addresses;
	u32					*check_values;

	/* The cleanup list is needed for rollback (as that's the only op
	 * taken).
	 */
	u32					nr_cleanup_syncs;
	struct pvr_sync_native_sync_prim	**cleanup_syncs;

	/* A FD is reserved in append_fences, but is not associated with
	 * the update fence until pvr_sync_get_update_fd().
	 */
	int					update_fence_fd;

	/* Keep the sync points around for fput and if rollback is needed */
	struct sync_file			*update_fence;
	struct pvr_sync_native_sync_prim	*update_sync;
	struct pvr_sync_native_sync_prim	*update_timeline_sync;
	struct sync_file			*check_fence;
};

/* Services client sync prim wrapper. This is used to hold debug information
 * and make it possible to cache unused syncs.
 */
struct pvr_sync_native_sync_prim {
	/* List for the sync pool support. */
	struct list_head list;

	/* Base services sync prim structure */
	struct PVRSRV_CLIENT_SYNC_PRIM *client_sync;

	/* The next queued value which should be used */
	u32 next_value;

	/* Every sync data will get some unique id */
	u32 id;

	/* FWAddr used by the client sync */
	u32 vaddr;

	/* The type this sync is used for in our driver. Used in
	 * pvr_sync_debug_request().
	 */
	u8 type;

	/* A debug class name also printed in pvr_sync_debug_request(). */
	char class[32];

	/* List for the cleanup syncs attached to a sync_pt */
	struct list_head cleanup_list;
};

/*
 * for sync_file, to define new pvr_dma_fence_timeline
 * refer to sw_sync.c
 *
 * struct sync_timeline - sync object
 * @kref:       reference count on fence.
 * @name:       name of the sync_timeline. Useful for debugging
 * @lock:       lock protecting @pt_list and @value
 * @pt_tree:        rbtree of active (unsignaled/errored) sync_pts
 * @pt_list:        list of active (unsignaled/errored) sync_pts
 * @sync_timeline_list: membership in global sync_timeline_list
 */
struct dma_fence_timeline { // 对标 sync_timeline
	struct kref     kref;
	const struct dma_fence_ops *ops;
	char            name[32];

	/* protected by lock */
	u64         context;
	int         value;

	struct rb_root      pt_tree;
	struct list_head    pt_list;
	spinlock_t      lock;

	struct list_head    sync_timeline_list;
};

/* This is the actual timeline metadata. We might keep this around after the
 * base sync driver has destroyed the pvr_sync_timeline_wrapper object.
 */
struct pvr_sync_timeline {
	/* Back reference to the sync_timeline. Not always valid */
	struct dma_fence_timeline *obj;

	/* Global timeline list support */
	struct list_head list;

	/* Timeline sync */
	struct pvr_sync_kernel_pair *kernel;

	/* Reference count for this object */
	struct kref kref;

	/* Used only by pvr_sync_update_all_timelines(). False if the timeline
	 * has been detected as racing with pvr_sync_destroy_timeline().
	 */
	bool valid;
};

/* This is the IMG extension of a sync_timeline */
struct pvr_sync_timeline_wrapper {
	/* Original timeline struct. Needs to come first. */
	struct dma_fence_timeline obj;

	/* Pointer to extra timeline data. Separated life-cycle. */
	struct pvr_sync_timeline *timeline;
};

struct pvr_sync_kernel_pair {
	/* Binary sync point representing the android native sync in hw. */
	struct pvr_sync_native_sync_prim *fence_sync;

	/* Cleanup sync list. If the base sync prim is used for "checking"
	 * only within a GL stream, there is no way of knowing when this has
	 * happened. So each check appends another sync prim just used for
	 * update at the end of the command, so we know if all syncs in this
	 * cleanup list are complete there are no outstanding renders waiting
	 * to check this, so it can safely be freed.
	 */
	struct list_head cleanup_sync_list;
	/*  A temporary pointer used to track the 'new' cleanup_sync added to
	 *  cleanup_sync_list within pvr_sync_append_fences()
	 */
	struct pvr_sync_native_sync_prim *current_cleanup_sync;

	/* Sync points can go away when there are deferred hardware operations
	 * still outstanding. We must not free the SERVER_SYNC_PRIMITIVE until
	 * the hardware is finished, so we add it to a defer list which is
	 * processed periodically ("defer-free").
	 *
	 * Note that the defer-free list is global, not per-timeline.
	 */
	struct list_head list;
};

struct pvr_sync_data {
	/* Every sync point has a services sync object. This object is used
	 * by the hardware to enforce ordering -- it is attached as a source
	 * dependency to various commands.
	 */
	struct pvr_sync_kernel_pair *kernel;

	/* The timeline update value for this sync point. */
	u32 timeline_update_value;

	/* This refcount is incremented at create and dup time, and decremented
	 * at free time. It ensures the object doesn't start the defer-free
	 * process until it is no longer referenced.
	 */
	struct kref kref;
};

/*
 * define dma_fence_pt
 *
 * struct sync_pt - sync_pt object
 * @base: base fence object
 * @link: link on the sync timeline's list
 * @node: node in the sync timeline's tree
 */
struct dma_fence_pt { // 对标 sync_pt
	struct dma_fence base;
	struct list_head link;
	struct rb_node node;
};

/* This is the IMG extension of a sync_pt */
struct pvr_sync_pt {
	/* Original sync_pt structure. Needs to come first. */
	struct dma_fence_pt pt;

	/* Private shared data */
	struct pvr_sync_data *sync_data;
};

/* This is the IMG extension of a sync_fence */
struct pvr_sync_fence {
	/* Original sync_fence structure. Needs to come first. */
	struct sync_file *fence;

	/* To ensure callbacks are always received for fences / sync_pts, even
	 * after the fence has been 'put' (freed), we must take a reference to
	 * the fence. We still need to 'put' the fence ourselves, but this might
	 * happen in irq context, where fput() is not allowed (in kernels <3.6).
	 * We must add the fence to a list which is processed in WQ context.
	 */
	struct list_head list;
};

/* Any sync point from a foreign (non-PVR) timeline needs to have a "shadow"
 * sync prim. This is modelled as a software operation. The foreign driver
 * completes the operation by calling a callback we registered with it.
 */
struct pvr_sync_fence_waiter {
	/* Base sync driver waiter structure */
	struct dma_fence_cb waiter;

	/* "Shadow" sync prim backing the foreign driver's sync_pt */
	struct pvr_sync_kernel_pair *kernel;

	/* Optimizes lookup of fence for defer-put operation */
	struct pvr_sync_fence *sync_fence;
};

/* Global data for the sync driver */
static struct {
	/* Complete notify handle */
	void *command_complete_handle;

	/* Defer-free workqueue. Syncs may still be in use by the HW when freed,
	 * so we have to keep them around until the HW is done with them at
	 * some later time. This workqueue iterates over the list of free'd
	 * syncs, checks if they are in use, and frees the sync device memory
	 * when done with.
	 */
	struct workqueue_struct *defer_free_wq;
	struct work_struct defer_free_work;

	/* check_status workqueue: When a foreign point is completed, a SW
	 * operation marks the sync as completed to allow the operations to
	 * continue. This completion may require the hardware to be notified,
	 * which may be expensive/take locks, so we push that to a workqueue
	 */
	struct workqueue_struct *check_status_wq;
	struct work_struct check_status_work;

	/* Context used to create client sync prims. */
	struct SYNC_PRIM_CONTEXT *sync_prim_context;

	/* Debug notify handle */
	void *debug_notify_handle;

	/* Unique id counter for the sync prims */
	atomic_t sync_id;

	/* The global event object (used to wait between checks for
	 * deferred-free sync status).
	 */
	void *event_object_handle;
} pvr_sync_data;

static void sync_timeline_get(struct dma_fence_timeline *obj)
{
	kref_get(&obj->kref);
}

static void pvr_sync_release_timeline(struct pvr_sync_timeline *timeline);

static void sync_timeline_free(struct kref *kref)
{
	struct dma_fence_timeline *obj =
		container_of(kref, struct dma_fence_timeline, kref);

	struct pvr_sync_timeline_wrapper *timeline_wrapper =
		(struct pvr_sync_timeline_wrapper *)obj;

	pvr_sync_release_timeline(timeline_wrapper->timeline);

	kfree(obj);
}

static void sync_timeline_put(struct dma_fence_timeline *obj)
{
	kref_put(&obj->kref, sync_timeline_free);
}

/*---------------------------------------------------------------------------*/

static inline
struct dma_fence_timeline *dma_fence_parent(struct dma_fence *fence)
{
	return container_of(fence->lock, struct dma_fence_timeline, lock);
}

/*---------------------------------------------------------------------------*/

#define PVR_DRV_NAME "pvr"
#define PVR_TIMELINE_NAME PVR_DRV_NAME ".timeline"

static const char *
pvr_fence_get_driver_name(struct dma_fence *fence)
{
	return PVR_DRV_NAME;
}

static const char *
pvr_fence_get_timeline_name(struct dma_fence *fence)
{
	struct dma_fence_timeline *parent = dma_fence_parent(fence);

	return parent->name;
}

static bool
pvr_fence_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static void pvr_sync_fence_install(struct sync_file *fence, int fd)
{
	fd_install(fd, fence->file);
}

static inline
struct dma_fence_timeline *pvr_sync_pt_parent(struct dma_fence_pt *pt)
{
	return container_of(pt->base.lock, struct dma_fence_timeline, lock);
}

/* List of timelines created by this driver */
static LIST_HEAD(timeline_list);
static DEFINE_MUTEX(timeline_list_mutex);

/* Sync pool support */
static LIST_HEAD(sync_pool_free_list);
static LIST_HEAD(sync_pool_active_list);
static DEFINE_MUTEX(sync_pool_mutex);
static s32 sync_pool_size;
static u32 sync_pool_created;
static u32 sync_pool_reused;

/* The "defer-free" object list. Driver global. */
static LIST_HEAD(sync_prim_free_list);
static DEFINE_SPINLOCK(sync_prim_free_list_spinlock);

/* The "defer-put" object list. Driver global. */
static LIST_HEAD(sync_fence_put_list);
static DEFINE_SPINLOCK(sync_fence_put_list_spinlock);

static void pvr_sync_update_all_timelines(void *command_complete_handle);

static inline void set_sync_value(struct pvr_sync_native_sync_prim *sync,
				  u32 value)
{
	*(sync->client_sync->pui32LinAddr) = value;
}

static inline u32 get_sync_value(struct pvr_sync_native_sync_prim *sync)
{
	return *(sync->client_sync->pui32LinAddr);
}

static inline void complete_sync(struct pvr_sync_native_sync_prim *sync)
{
	*(sync->client_sync->pui32LinAddr) = sync->next_value;
}

static inline bool is_sync_met(struct pvr_sync_native_sync_prim *sync)
{
	return *(sync->client_sync->pui32LinAddr) == sync->next_value;
}

static inline
struct pvr_sync_timeline *get_timeline(struct dma_fence_timeline *obj)
{
	return ((struct pvr_sync_timeline_wrapper *)obj)->timeline;
}

static inline struct pvr_sync_timeline *get_timeline_pt(struct dma_fence_pt *pt)
{
	return get_timeline(pvr_sync_pt_parent(pt));
}

static inline bool
pvr_sync_has_kernel_signaled(struct pvr_sync_kernel_pair *kernel)
{
	/* Idle syncs are always signaled */
	if (!kernel)
		return 1;

	return is_sync_met(kernel->fence_sync);
}

const struct dma_fence_ops pvr_fence_ops;

static inline
struct dma_fence_pt *dma_fence_to_pvr_dma_fence_pt(struct dma_fence *fence)
{
	if (fence->ops != &pvr_fence_ops) {
		BUG();
		return NULL;
	}

	return container_of(fence, struct dma_fence_pt, base);
}

static inline
struct pvr_sync_pt *dma_fence_to_pvr_sync_pt(struct dma_fence *dma_fence)
{
	struct dma_fence_pt *dma_fence_pt = dma_fence_to_pvr_dma_fence_pt(dma_fence);

	return container_of(dma_fence_pt, struct pvr_sync_pt, pt);
}

#ifdef DEBUG_OUTPUT

static char *debug_info_timeline(struct pvr_sync_timeline *timeline)
{
	static char info[256];

	snprintf(info, sizeof(info),
		 "n='%s' id=%u fw=0x%x tl_curr=%u tl_next=%u",
		 timeline->obj ? timeline->obj->name : "?",
		 timeline->kernel->fence_sync->id,
		 timeline->kernel->fence_sync->vaddr,
		 get_sync_value(timeline->kernel->fence_sync),
		 timeline->kernel->fence_sync->next_value);

	return info;
}

static char *debug_info_sync_pt(struct dma_fence_pt *pt)
{
	struct pvr_sync_timeline *timeline = get_timeline_pt(pt);
	struct pvr_sync_pt *pvr_pt = (struct pvr_sync_pt *)pt;
	struct pvr_sync_kernel_pair *kernel = pvr_pt->sync_data->kernel;
	static char info[256], info1[256];

	if (kernel) {
		unsigned int cleanup_count = 0;
		unsigned int info1_pos = 0;
		struct list_head *pos;

		info1[0] = 0;

		list_for_each(pos, &kernel->cleanup_sync_list) {
			struct pvr_sync_native_sync_prim *cleanup_sync =
				list_entry(pos,
					struct pvr_sync_native_sync_prim,
					cleanup_list);
			int string_size = 0;

			string_size = snprintf(info1 + info1_pos,
				sizeof(info1) - info1_pos,
				" # cleanup %u: id=%u fw=0x%x curr=%u next=%u",
				cleanup_count,
				cleanup_sync->id,
				cleanup_sync->vaddr,
				get_sync_value(cleanup_sync),
				cleanup_sync->next_value);
			cleanup_count++;
			info1_pos += string_size;
			/* Truncate the string and stop if we run out of space
			 * This should stop any underflow of snprintf's 'size'
			 * arg too
			 */
			if (info1_pos >= sizeof(info1))
				break;
		}

		snprintf(info, sizeof(info),
			 "status=%d tl_taken=%u ref=%d # sync: id=%u fw=0x%x curr=%u next=%u%s # tl: %s",
			 pvr_sync_has_kernel_signaled(kernel),
			 pvr_pt->sync_data->timeline_update_value,
			 kref_read(&pvr_pt->sync_data->kref),
			 kernel->fence_sync->id,
			 kernel->fence_sync->vaddr,
			 get_sync_value(kernel->fence_sync),
			 kernel->fence_sync->next_value,
			 info1, debug_info_timeline(timeline));
	} else {
		snprintf(info, sizeof(info),
			 "status=%d tl_taken=%u ref=%d # sync: idle # tl: %s",
			 pvr_sync_has_kernel_signaled(kernel),
			 pvr_pt->sync_data->timeline_update_value,
			 kref_read(&pvr_pt->sync_data->kref),
			 debug_info_timeline(timeline));
	}

	return info;
}

#endif /* DEBUG_OUTPUT */

static enum PVRSRV_ERROR
sync_pool_get(struct pvr_sync_native_sync_prim **_sync,
	      const char *class_name, u8 type)
{
	struct pvr_sync_native_sync_prim *sync;
	enum PVRSRV_ERROR error = PVRSRV_OK;
	u32 sync_addr;

	mutex_lock(&sync_pool_mutex);

	if (list_empty(&sync_pool_free_list)) {
		/* If there is nothing in the pool, create a new sync prim. */
		sync = kmalloc(sizeof(*sync),
			       GFP_KERNEL);
		if (!sync) {
			pr_err("pvr_sync: %s: Failed to allocate sync data\n",
			       __func__);
			error = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto err_unlock;
		}

		error = SyncPrimAlloc(pvr_sync_data.sync_prim_context,
				      &sync->client_sync, class_name);
		if (error != PVRSRV_OK) {
			pr_err("pvr_sync: %s: Failed to allocate sync prim (%s)\n",
			       __func__, PVRSRVGetErrorStringKM(error));
			goto err_free;
		}

		error = SyncPrimGetFirmwareAddr(sync->client_sync, &sync_addr);
		if (error != PVRSRV_OK) {
			pr_err("pvr_sync: %s: Failed to get FW address (%s)\n",
			       __func__, PVRSRVGetErrorStringKM(error));
			goto err_sync_prim_free;
		}
		sync->vaddr = sync_addr;

		list_add_tail(&sync->list, &sync_pool_active_list);
		++sync_pool_created;
	} else {
		sync = list_first_entry(&sync_pool_free_list,
					struct pvr_sync_native_sync_prim, list);
		list_move_tail(&sync->list, &sync_pool_active_list);
		--sync_pool_size;
		++sync_pool_reused;
	}

	sync->id = atomic_inc_return(&pvr_sync_data.sync_id);
	sync->type = type;

	strncpy(sync->class, class_name, sizeof(sync->class));
	sync->class[sizeof(sync->class) - 1] = '\0';
	/* Its crucial to reset the sync to zero */
	set_sync_value(sync, 0);
	sync->next_value = 0;

	*_sync = sync;
err_unlock:
	mutex_unlock(&sync_pool_mutex);
	return error;

err_sync_prim_free:
	SyncPrimFree(sync->client_sync);

err_free:
	kfree(sync);
	goto err_unlock;
}

static void sync_pool_put(struct pvr_sync_native_sync_prim *sync)
{
	mutex_lock(&sync_pool_mutex);

	if (sync_pool_size < SYNC_MAX_POOL_SIZE) {
		/* Mark it as unused */
		set_sync_value(sync, 0xffffffff);

		list_move(&sync->list, &sync_pool_free_list);
		++sync_pool_size;
	} else {
		/* Mark it as invalid */
		set_sync_value(sync, 0xdeadbeef);

		list_del(&sync->list);
		SyncPrimFree(sync->client_sync);
		kfree(sync);
	}

	mutex_unlock(&sync_pool_mutex);
}

static void sync_pool_clear(void)
{
	struct pvr_sync_native_sync_prim *sync, *n;

	mutex_lock(&sync_pool_mutex);

	list_for_each_entry_safe(sync, n, &sync_pool_free_list, list) {
		/* Mark it as invalid */
		set_sync_value(sync, 0xdeadbeef);

		list_del(&sync->list);
		SyncPrimFree(sync->client_sync);
		kfree(sync);
		--sync_pool_size;
	}

	mutex_unlock(&sync_pool_mutex);
}

static void pvr_sync_debug_request(void *hDebugRequestHandle,
				   u32 ui32VerbLevel,
				   DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				   void *pvDumpDebugFile)
{
	struct pvr_sync_native_sync_prim *sync;

	static const char *const type_names[] = {
		"Timeline", "Fence", "Cleanup",
		"Foreign Fence", "Foreign Cleanup"
	};

	if (ui32VerbLevel == DEBUG_REQUEST_VERBOSITY_HIGH) {
		mutex_lock(&sync_pool_mutex);

		PVR_DUMPDEBUG_LOG(pfnDumpDebugPrintf, pvDumpDebugFile,
				  "Dumping all pending android native syncs (Pool usage: %d%% - %d %d)",
				  sync_pool_reused ?
				  (10000 /
				   ((sync_pool_created + sync_pool_reused) *
				    100 / sync_pool_reused)) : 0,
				  sync_pool_created, sync_pool_reused);

		list_for_each_entry(sync, &sync_pool_active_list, list) {
			if (is_sync_met(sync))
				continue;

			BUG_ON(sync->type >= ARRAY_SIZE(type_names));

			PVR_DUMPDEBUG_LOG(pfnDumpDebugPrintf, pvDumpDebugFile,
					  "\tID = %d, FWAddr = 0x%08x: Current = 0x%08x, Next = 0x%08x, %s (%s)",
					  sync->id, sync->vaddr,
					  get_sync_value(sync),
					  sync->next_value,
					  sync->class,
					  type_names[sync->type]);
		}
		mutex_unlock(&sync_pool_mutex);
	}
}

/*
 * 调用者必须保证 传入的 'fence' 指向 pvr_dma_fence.
 */
static bool pvr_sync_has_signaled(struct dma_fence *fence)
{
	struct dma_fence_pt *sync_pt = dma_fence_to_pvr_dma_fence_pt(fence);
	struct pvr_sync_pt *pvr_pt = (struct pvr_sync_pt *)sync_pt;

	DPF("%s: # %s", __func__, debug_info_sync_pt(sync_pt));

	return pvr_sync_has_kernel_signaled(pvr_pt->sync_data->kernel);
}

static void wait_for_sync(struct pvr_sync_native_sync_prim *sync)
{
#ifndef NO_HARDWARE
	void *event_object = NULL;
	enum PVRSRV_ERROR error = PVRSRV_OK;

	while (sync && !is_sync_met(sync)) {
		if (!event_object) {
			error = OSEventObjectOpen(
				pvr_sync_data.event_object_handle,
				&event_object);
			if (error != PVRSRV_OK) {
				pr_err("pvr_sync: %s: Error opening event object (%s)\n",
					__func__,
					PVRSRVGetErrorStringKM(error));
				break;
			}
		}
		error = OSEventObjectWait(event_object);
		if (error != PVRSRV_OK && error != PVRSRV_ERROR_TIMEOUT) {
			pr_err("pvr_sync: %s: Error waiting on event object (%s)\n",
				__func__,
				PVRSRVGetErrorStringKM(error));
		}
	}

	if (event_object)
		OSEventObjectClose(event_object);
#endif /* NO_HARDWARE */
}

static void pvr_sync_defer_free(struct pvr_sync_kernel_pair *kernel)
{
	unsigned long flags;

	spin_lock_irqsave(&sync_prim_free_list_spinlock, flags);
	list_add_tail(&kernel->list, &sync_prim_free_list);
	spin_unlock_irqrestore(&sync_prim_free_list_spinlock, flags);

	queue_work(pvr_sync_data.defer_free_wq, &pvr_sync_data.defer_free_work);
}

/* This function assumes the timeline_list_mutex is held while it runs */

static void pvr_sync_destroy_timeline_locked(struct kref *kref)
{
	struct pvr_sync_timeline *timeline = (struct pvr_sync_timeline *)
		container_of(kref, struct pvr_sync_timeline, kref);

	pvr_sync_defer_free(timeline->kernel);
	list_del(&timeline->list);
	kfree(timeline);
}

static void pvr_sync_destroy_timeline(struct kref *kref)
{
	mutex_lock(&timeline_list_mutex);
	pvr_sync_destroy_timeline_locked(kref);
	mutex_unlock(&timeline_list_mutex);
}

static void pvr_sync_release_timeline(struct pvr_sync_timeline *timeline)
{
	/* If pvr_sync_open failed after calling sync_timeline_create, this
	 * can be called with a timeline that has not got a timeline sync
	 * or been added to our timeline list. Use a NULL timeline to
	 * detect and handle this condition
	 */
	if (!timeline)
		return;

	DPF("%s: # %s", __func__, debug_info_timeline(timeline));

	wait_for_sync(timeline->kernel->fence_sync);

	/* Whether or not we're the last reference, obj is going away
	 * after this function returns, so remove our back reference
	 * to it.
	 */
	timeline->obj = NULL;

	/* This might be the last reference to the timeline object.
	 * If so, we'll go ahead and delete it now.
	 */
	kref_put(&timeline->kref, pvr_sync_destroy_timeline);
}

/* pvr_sync_create_sync_data() should be called with the bridge lock held */
static struct pvr_sync_data *
pvr_sync_create_sync_data(struct dma_fence_timeline *obj)
{
	struct pvr_sync_data *sync_data = NULL;
	enum PVRSRV_ERROR error;

	sync_data = kzalloc(sizeof(*sync_data), GFP_KERNEL);
	if (!sync_data)
		goto err_out;

	kref_init(&sync_data->kref);

	sync_data->kernel =
		kzalloc(sizeof(*sync_data->kernel),
		GFP_KERNEL);

	if (!sync_data->kernel)
		goto err_free_data;

	INIT_LIST_HEAD(&sync_data->kernel->cleanup_sync_list);

	error = sync_pool_get(&sync_data->kernel->fence_sync,
			      obj->name, SYNC_PT_FENCE_TYPE);

	if (error != PVRSRV_OK) {
		pr_err("pvr_sync: %s: Failed to allocate sync prim (%s)\n",
		       __func__, PVRSRVGetErrorStringKM(error));
		goto err_free_kernel;
	}

err_out:
	return sync_data;

err_free_kernel:
	kfree(sync_data->kernel);
err_free_data:
	kfree(sync_data);
	sync_data = NULL;
	goto err_out;
}

static void pvr_sync_free_sync_data(struct kref *kref)
{
	struct pvr_sync_data *sync_data = (struct pvr_sync_data *)
		container_of(kref, struct pvr_sync_data, kref);

	if (sync_data->kernel)
		pvr_sync_defer_free(sync_data->kernel);
	kfree(sync_data);
}

static void pvr_sync_free_sync(struct dma_fence_pt *sync_pt)
{
	struct pvr_sync_pt *pvr_pt = (struct pvr_sync_pt *)sync_pt;

	DPF("%s: # %s", __func__, debug_info_sync_pt(sync_pt));

	kref_put(&pvr_pt->sync_data->kref, pvr_sync_free_sync_data);
}

static void timeline_fence_value_str(struct dma_fence *fence,
	char *str, int size)
{
	snprintf(str, size, "%d", fence->seqno);
}

static void timeline_fence_timeline_value_str(struct dma_fence *fence,
	char *str, int size)
{
	struct dma_fence_timeline *parent = dma_fence_parent(fence);

	snprintf(str, size, "%d", parent->value);
}

static void pvr_fence_release(struct dma_fence *fence)
{
       // struct pvr_sync_pt *pvr_sync_pt = dma_fence_to_pvr_sync_pt(fence);
	struct dma_fence_pt *pt = dma_fence_to_pvr_dma_fence_pt(fence);
	struct dma_fence_timeline *parent = dma_fence_parent(fence);
	unsigned long flags;

	/*-------------------------------------------------------*/
	spin_lock_irqsave(fence->lock, flags);

	/* 释放当前 pvr_sync_pt 实例的 private_data. */
	pvr_sync_free_sync(pt);

    /*-------------------------------------------------------*/
	/* 参考自 timeline_fence_release(). */

	if (!list_empty(&pt->link)) {
		list_del(&pt->link);
		rb_erase(&pt->node, &parent->pt_tree);
	}

	spin_unlock_irqrestore(fence->lock, flags);

	sync_timeline_put(parent);

	dma_fence_free(fence);
}

const struct dma_fence_ops pvr_fence_ops = {
	//wait: refer to mali
	.wait = dma_fence_default_wait,
	.get_driver_name = pvr_fence_get_driver_name,
	.get_timeline_name = pvr_fence_get_timeline_name,
	.enable_signaling = pvr_fence_enable_signaling,
//Warning
	.fence_value_str = timeline_fence_value_str,
//Warning
	.timeline_value_str = timeline_fence_timeline_value_str,
	.signaled = pvr_sync_has_signaled,
	.release = pvr_fence_release,
};

/**
 * pvr_sync_pt_create() - creates a sync pt
 * @obj:    parent sync_timeline
 * @value:  value of the fence
 *
 * Creates a new sync_pt (fence) as a child of @parent.  @size bytes will be
 * allocated allowing for implementation specific data to be kept after
 * the generic sync_timeline struct. Returns the sync_pt object or
 * NULL in case of error.
 */
struct dma_fence_pt *pvr_sync_pt_create(struct dma_fence_timeline *obj,
		unsigned int value, struct pvr_sync_data *sync_data)
{
	struct dma_fence_pt *pt;
	struct pvr_sync_pt *pvr_pt = NULL;

	pt = kzalloc(sizeof(struct pvr_sync_pt), GFP_KERNEL);
	if (!pt)
		return NULL;

	sync_timeline_get(obj);
	dma_fence_init(&pt->base, &pvr_fence_ops, &obj->lock,
			obj->context, value);
	INIT_LIST_HEAD(&pt->link);

	spin_lock_irq(&obj->lock);

	pvr_pt = (struct pvr_sync_pt *)pt;

	pvr_pt->sync_data = sync_data;

	if (!dma_fence_is_signaled_locked(&pt->base)) {
		struct rb_node **p = &obj->pt_tree.rb_node;
		struct rb_node *parent = NULL;

		while (*p) {
			struct dma_fence_pt *other;
			int cmp;

			parent = *p;
			other = rb_entry(parent, typeof(*pt), node);
			cmp = value - other->base.seqno;
			if (cmp > 0) {
				p = &parent->rb_right;
			} else if (cmp < 0) {
				p = &parent->rb_left;
			} else {
				if (dma_fence_get_rcu(&other->base)) {
					sync_timeline_put(obj);
					kfree(pt);
					pt = other;
					goto unlock;
				}
				p = &parent->rb_left;
			}
		}
		rb_link_node(&pt->node, parent, p);
		rb_insert_color(&pt->node, &obj->pt_tree);

		parent = rb_next(&pt->node);
		list_add_tail(&pt->link,
				parent ? &rb_entry(parent, typeof(*pt), node)->link : &obj->pt_list);
	}
unlock:
	spin_unlock_irq(&obj->lock);

	return pt;
}

/**
 * sync_timeline_create() - creates a sync object
 * @name:   sync_timeline name
 *
 * Creates a new sync_timeline. Returns the sync_timeline object or NULL in
 * case of error.
 */
static struct dma_fence_timeline *pvr_sync_timeline_create(const char *name, int size, const struct dma_fence_ops *ops)
{
	struct dma_fence_timeline *obj;

	obj = kzalloc(size, GFP_KERNEL);
	if (!obj)
		return NULL;

	kref_init(&obj->kref);
	obj->ops = ops;
	obj->context = dma_fence_context_alloc(1);
	strlcpy(obj->name, name, sizeof(obj->name));

	obj->pt_tree = RB_ROOT;
	INIT_LIST_HEAD(&obj->pt_list);
	spin_lock_init(&obj->lock);

	//sync_timeline_debug_add(obj);

	return obj;
}

static bool timeline_fence_signaled(struct dma_fence *fence)
{
	return pvr_sync_has_signaled(fence);
}

/**
 * pvr_sync_timeline_signal() - signal a status change on a sync_timeline
 * @obj:    sync_timeline to signal
 * @inc:    num to increment on timeline->value
 *
 * A sync implementation should call this any time one of it's fences
 * has signaled or has an error condition.
 */
static void pvr_sync_timeline_signal(struct dma_fence_timeline *obj)
{
	struct dma_fence_pt *pt, *next;

	//trace_sync_timeline(obj);

	spin_lock_irq(&obj->lock);

	//obj->value += inc;

	list_for_each_entry_safe(pt, next, &obj->pt_list, link) {
		if (!timeline_fence_signaled(&pt->base))
			break;

		list_del_init(&pt->link);
		rb_erase(&pt->node, &obj->pt_tree);

		/*
		 * A signal callback may release the last reference to this
		 * fence, causing it to be freed. That operation has to be
		 * last to avoid a use after free inside this loop, and must
		 * be after we remove the fence from the timeline in order to
		 * prevent deadlocking on timeline->lock inside
		 * timeline_fence_release().
		 */
		dma_fence_signal_locked(&pt->base);
	}

	spin_unlock_irq(&obj->lock);
}

void pvr_sync_timeline_destroy(struct dma_fence_timeline *obj)
{
    //obj->destroyed = true;
    /*
     * Ensure timeline is marked as destroyed before
     * changing timeline's fences status.
     */
    //smp_wmb();

    /*
     * signal any children that their parent is going away.
     */
	pvr_sync_timeline_signal(obj);
	sync_timeline_put(obj);
}

static inline bool is_pvr_timeline(struct dma_fence_timeline *obj)
{
	return obj->ops == &pvr_fence_ops;
}

static inline bool is_pvr_dma_fence(struct dma_fence *dma_fence)
{
	return dma_fence->ops == &pvr_fence_ops;
}

/* foreign sync handling */

static void pvr_sync_foreign_sync_pt_signaled(struct dma_fence *fence,
					      struct dma_fence_cb *_waiter)
{
	struct pvr_sync_fence_waiter *waiter =
		(struct pvr_sync_fence_waiter *)_waiter;
	unsigned long flags;

	/* Complete the SW operation and free the sync if we can. If we can't,
	 * it will be checked by a later workqueue kick.
	 */
	complete_sync(waiter->kernel->fence_sync);

	/* We can 'put' the fence now, but this function might be called in
	 * irq context so we must defer to WQ.
	 * This WQ is triggered in pvr_sync_defer_free, so adding it to the
	 * put list before that should guarantee it's cleaned up on the next
	 * wq run.
	 */
	spin_lock_irqsave(&sync_fence_put_list_spinlock, flags);
	list_add_tail(&waiter->sync_fence->list, &sync_fence_put_list);
	spin_unlock_irqrestore(&sync_fence_put_list_spinlock, flags);

	pvr_sync_defer_free(waiter->kernel);

	/* The completed sw-sync may allow other tasks to complete,
	 * so we need to allow them to progress.
	 */
	queue_work(pvr_sync_data.check_status_wq,
		&pvr_sync_data.check_status_work);

	kfree(waiter);
}

static struct pvr_sync_kernel_pair *
pvr_sync_create_waiter_for_foreign_sync(int fd)
{
	struct pvr_sync_native_sync_prim *cleanup_sync = NULL;
	struct pvr_sync_kernel_pair *kernel = NULL;
	struct pvr_sync_fence_waiter *waiter;
	struct pvr_sync_fence *sync_fence;

	struct sync_file *fence;

	enum PVRSRV_ERROR error;
	int err;

	//fence = sync_fence_fdget(fd);
	fence = sync_file_get(fd);
	if (!fence) {
		pr_err("pvr_sync: %s: Failed to take reference on fence\n",
		       __func__);
		goto err_out;
	}

	kernel = kmalloc(sizeof(*kernel), GFP_KERNEL);
	if (!kernel) {
		pr_err("pvr_sync: %s: Failed to allocate sync kernel\n",
		       __func__);
		goto err_put_fence;
	}

	INIT_LIST_HEAD(&kernel->cleanup_sync_list);

	sync_fence = kmalloc(sizeof(*sync_fence), GFP_KERNEL);
	if (!sync_fence) {
		pr_err("pvr_sync: %s: Failed to allocate pvr sync fence\n",
		       __func__);
		goto err_free_kernel;
	}

	sync_fence->fence = fence;

	error = sync_pool_get(&kernel->fence_sync,
			      fence->user_name, SYNC_PT_FOREIGN_FENCE_TYPE);
	if (error != PVRSRV_OK) {
		pr_err("pvr_sync: %s: Failed to allocate sync prim (%s)\n",
		       __func__, PVRSRVGetErrorStringKM(error));
		goto err_free_sync_fence;
	}

	kernel->fence_sync->next_value++;

	error = sync_pool_get(&cleanup_sync, fence->user_name,
		SYNC_PT_FOREIGN_CLEANUP_TYPE);
	if (error != PVRSRV_OK) {
		pr_err("pvr_sync: %s: Failed to allocate cleanup sync prim (%s)\n",
		       __func__, PVRSRVGetErrorStringKM(error));
		goto err_free_sync;
	}

	cleanup_sync->next_value++;

	list_add(&cleanup_sync->cleanup_list, &kernel->cleanup_sync_list);

	/* The custom waiter structure is freed in the waiter callback */
	waiter = kmalloc(sizeof(*waiter), GFP_KERNEL);
	if (!waiter) {
		pr_err("pvr_sync: %s: Failed to allocate waiter\n", __func__);
		goto err_free_cleanup_sync;
	}

	waiter->kernel = kernel;
	waiter->sync_fence = sync_fence;

	err = dma_fence_add_callback(fence->fence, &waiter->waiter,
				pvr_sync_foreign_sync_pt_signaled);
	if (-ENOENT == err) {
		// V("'fence->fence' has been already signaled.");
		goto err_free_waiter;
	} else if (-EINVAL == err) {
		pr_err("pvr_sync_file: %s: failed to add callback to dma_fence, err: %d\n",
				__func__, err);
		goto err_free_waiter;
	}

	kernel->current_cleanup_sync = cleanup_sync;

err_out:
	return kernel;
err_free_waiter:
	kfree(waiter);
err_free_cleanup_sync:
	list_del(&cleanup_sync->cleanup_list);
	sync_pool_put(cleanup_sync);
err_free_sync:
	sync_pool_put(kernel->fence_sync);
err_free_sync_fence:
	kfree(sync_fence);
err_free_kernel:
	kfree(kernel);
	kernel = NULL;
err_put_fence:
	sync_file_put(fence);
	goto err_out;
}

static
struct pvr_sync_pt *pvr_sync_create_pt(struct pvr_sync_timeline *timeline)
{
	struct pvr_sync_data *sync_data;
	struct pvr_sync_pt *pvr_pt = NULL;

	sync_data = pvr_sync_create_sync_data(timeline->obj);
	if (!sync_data) {
		pr_err("pvr_sync: %s: Failed to create sync data\n", __func__);
		goto err_out;
	}

	sync_data->kernel->fence_sync->next_value++;

//Warning
	pvr_pt = (struct pvr_sync_pt *)
		pvr_sync_pt_create(timeline->obj, ++timeline->obj->value, sync_data);

	if (!pvr_pt) {
		pr_err("pvr_sync: %s: Failed to create sync pt\n", __func__);
		goto err_rollback_fence;
	}

	/* Increment the timeline next value */
	pvr_pt->sync_data->timeline_update_value =
		timeline->kernel->fence_sync->next_value++;

	return pvr_pt;

err_rollback_fence:
	sync_data->kernel->fence_sync->next_value--;
	kref_put(&sync_data->kref, pvr_sync_free_sync_data);
err_out:
	return NULL;
}

/* Predeclare the pvr_sync_fops as it's used for comparison to ensure the
 * update_timeline_fd passed in to pvr_sync_append_fences() is a pvr_sync
 * timeline.
 */
static const struct file_operations pvr_sync_fops;

enum PVRSRV_ERROR pvr_sync_append_fences(
	const char				*name,
	const s32				check_fence_fd,
	const s32				update_timeline_fd,
	const u32				nr_updates,
	const struct _RGXFWIF_DEV_VIRTADDR_	*update_ufo_addresses,
	const u32				*update_values,
	const u32				nr_checks,
	const struct _RGXFWIF_DEV_VIRTADDR_	*check_ufo_addresses,
	const u32				*check_values,
	struct pvr_sync_append_data		**append_sync_data)
{
	struct pvr_sync_native_sync_prim **cleanup_sync_pos;
	struct pvr_sync_pt *update_point = NULL;
	struct sync_file *update_fence = NULL;
	struct pvr_sync_append_data *sync_data;
	struct _RGXFWIF_DEV_VIRTADDR_ *update_address_pos;
	struct _RGXFWIF_DEV_VIRTADDR_ *check_address_pos;
	struct pvr_sync_timeline *timeline;
	unsigned int num_used_sync_updates;
	unsigned int num_used_sync_checks;
	enum PVRSRV_ERROR err = PVRSRV_OK;
	u32 *update_value_pos;
	u32 *check_value_pos;

	struct dma_fence **fences = NULL;
	unsigned int num_fences;

	if ((nr_updates && (!update_ufo_addresses || !update_values)) ||
	    (nr_checks && (!check_ufo_addresses || !check_values))) {
		err =  PVRSRV_ERROR_INVALID_PARAMS;
		goto err_out;
	}

	sync_data =
		kzalloc(sizeof(*sync_data), GFP_KERNEL);
	if (!sync_data) {
		err = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_out;
	}

	sync_data->update_fence_fd = -1;

	if (update_timeline_fd >= 0) {
		struct file *timeline_file;

		/* We reserve the update fence FD before taking any operations
		 * as we do not want to fail (e.g. run out of FDs) after the
		 * kick operation has been submitted to the hw.
		 */
		sync_data->update_fence_fd = get_unused_fd();
		if (sync_data->update_fence_fd < 0) {
			err = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto err_free_append_data;
		}

		timeline_file = fget(update_timeline_fd);
		if (!timeline_file) {
			pr_err("pvr_sync: %s: Failed to open supplied timeline fd (%d)\n",
				__func__, update_timeline_fd);
			err = PVRSRV_ERROR_HANDLE_NOT_FOUND;
			goto err_free_append_data;
		}

		if (timeline_file->f_op != &pvr_sync_fops) {
			pr_err("pvr_sync: %s: Supplied timeline not pvr_sync timeline\n",
				__func__);
			fput(timeline_file);
			err = PVRSRV_ERROR_INVALID_PARAMS;
			goto err_free_append_data;
		}

		timeline = get_timeline(timeline_file->private_data);

		/* We know this will not free the timeline as the user still
		 * has the fd referencing it.
		 */
		fput(timeline_file);

		if (!timeline) {
			pr_err("pvr_sync: %s: Supplied timeline has no private data\n",
				__func__);
			err = PVRSRV_ERROR_HANDLE_NOT_FOUND;
			goto err_free_append_data;
		}

		update_point = pvr_sync_create_pt(timeline);
		if (!update_point) {
			printk("rk-debug pvr_sync: %s: Failed to create sync point\n",
				__func__);
			err = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto err_free_append_data;
		}

		//update_fence = sync_fence_create(name, &update_point->pt);
		update_fence = sync_file_create(&update_point->pt.base);
		dma_fence_put(&update_point->pt.base);
		if (!update_fence) {
			struct pvr_sync_native_sync_prim *fence_prim =
				update_point->sync_data->kernel->fence_sync;
			struct pvr_sync_native_sync_prim *timeline_prim =
				timeline->kernel->fence_sync;

			pr_err("pvr_sync: %s: Failed to create sync file\n",
				__func__);
			err = PVRSRV_ERROR_OUT_OF_MEMORY;

			/* If the point was created but the fence failed to be
			 * created, the point must be manually free'd as a
			 * fence has not yet taken ownership.
			 */

			/* First rollback the point's taken operations */
			timeline_prim->next_value--;
			fence_prim->next_value--;
			pvr_sync_free_sync(&update_point->pt);
			goto err_free_append_data;
		}

		sync_data->update_fence = update_fence;
		sync_data->update_sync =
			update_point->sync_data->kernel->fence_sync;
		sync_data->update_timeline_sync =
			timeline->kernel->fence_sync;
	}

	sync_data->nr_checks = nr_checks;
	sync_data->nr_updates = nr_updates;

	if (check_fence_fd >= 0) {
		struct sync_file *fence = sync_file_get(check_fence_fd); // check_fence
		struct pvr_sync_kernel_pair *sync_kernel;
		unsigned int points_on_fence = 0;
		bool has_foreign_point = false;
		// struct dma_fence_pt *sync_pt;
		struct dma_fence *dma_fence;
		int j;

		if (!fence) {
			pr_err("pvr_sync: %s: Failed to read sync private data for fd %d\n",
				__func__, check_fence_fd);
			err = PVRSRV_ERROR_HANDLE_NOT_FOUND;
			goto err_free_fence;
		}

		sync_data->check_fence = fence;

		/*-----------------------------------*/

		if (dma_fence_is_array(fence->fence)) {
			struct dma_fence_array *array = to_dma_fence_array(fence->fence);
			if (!array) {
				pr_err("%s: Failed to resolve dma fence array\n",
					   __func__);
			}

			fences = array->fences;
			num_fences = array->num_fences;
		} else {
			fences = &fence->fence;
			num_fences = 1;
		}

		(void)j;
		for (j = 0,
			 dma_fence = ((num_fences == 0) ? NULL : fences[0]);
			 j < num_fences;
			 j++,
		     dma_fence = ((j < num_fences) ? fences[j] : NULL)) {
			//for_each_sync_pt(sync_pt, fence, j) ...
			struct pvr_sync_native_sync_prim *cleanup_sync = NULL;
			struct dma_fence_pt *sync_pt;
			struct pvr_sync_pt *pvr_pt;

			// if (!is_dma_fence_pt(fences[j])) {
			if (!is_pvr_dma_fence(dma_fence)) {
				// if (!sync_pt_get_status(sync_pt))
				if (!dma_fence_is_signaled(dma_fence))
					has_foreign_point = true;
				continue;
			}

			sync_pt = (struct dma_fence_pt *)dma_fence;
			pvr_pt = (struct pvr_sync_pt *)sync_pt;
			sync_kernel = pvr_pt->sync_data->kernel;

			if (!sync_kernel ||
			    is_sync_met(sync_kernel->fence_sync)) {
				continue;
			}

			/* We will use the above sync for "check" only. In this
			 * case also insert a "cleanup" update command into the
			 * opengl stream. This can later be used for checking
			 * if the sync prim could be freed.
			 */
			err = sync_pool_get(&cleanup_sync,
				pvr_sync_pt_parent(&pvr_pt->pt)->name,
				SYNC_PT_CLEANUP_TYPE);
			if (err != PVRSRV_OK) {
				pr_err("pvr_sync: %s: Failed to allocate cleanup sync prim (%s)\n",
				       __func__,
				       PVRSRVGetErrorStringKM(err));
				goto err_free_append_data;
			}
			list_add(&cleanup_sync->cleanup_list,
				&sync_kernel->cleanup_sync_list);
			sync_kernel->current_cleanup_sync = cleanup_sync;
			points_on_fence++;
		}

		if (has_foreign_point)
			points_on_fence++;

		/* Each point has 1 check value, and 1 update value (for the
		 * cleanup fence).
		 */
		sync_data->nr_checks += points_on_fence;
		sync_data->nr_updates += points_on_fence;
		sync_data->nr_cleanup_syncs += points_on_fence;
	}

	if (update_point) {
		/* A fence update requires 2 update values (fence and timeline)
		 */
		 sync_data->nr_updates += 2;
	}

	if (sync_data->nr_updates > 0) {
		sync_data->update_ufo_addresses =
			kzalloc(sizeof(*sync_data->update_ufo_addresses) *
					sync_data->nr_updates, GFP_KERNEL);
		if (!sync_data->update_ufo_addresses) {
			pr_err("pvr_sync: %s: Failed to allocate update UFO address list\n",
				__func__);
			err = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto err_free_fence;
		}

		sync_data->update_values =
			kzalloc(sizeof(*sync_data->update_values) *
				sync_data->nr_updates, GFP_KERNEL);
		if (!sync_data->update_values) {
			pr_err("pvr_sync: %s: Failed to allocate update value list\n",
				__func__);
			err = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto err_free_fence;
		}
	}

	if (sync_data->nr_checks > 0) {

		sync_data->check_ufo_addresses =
			kzalloc(sizeof(*sync_data->check_ufo_addresses) *
					sync_data->nr_checks, GFP_KERNEL);
		if (!sync_data->check_ufo_addresses) {
			pr_err("pvr_sync: %s: Failed to allocate check UFO address list\n",
				__func__);
			err = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto err_free_fence;
		}

		sync_data->check_values =
			kzalloc(sizeof(*sync_data->check_values) *
				sync_data->nr_checks, GFP_KERNEL);
		if (!sync_data->check_values) {
			pr_err("pvr_sync: %s: Failed to allocate check value list\n",
				__func__);
			err = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto err_free_fence;
		}
	}

	if (sync_data->nr_cleanup_syncs > 0) {
		sync_data->cleanup_syncs =
			kzalloc(sizeof(*sync_data->cleanup_syncs) *
				sync_data->nr_cleanup_syncs, GFP_KERNEL);
		if (!sync_data->cleanup_syncs) {
			pr_err("pvr_sync: %s: Failed to allocate cleanup rollback list\n",
				__func__);
			err = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto err_free_fence;
		}
	}

	update_address_pos = sync_data->update_ufo_addresses;
	update_value_pos = sync_data->update_values;
	check_address_pos = sync_data->check_ufo_addresses;
	check_value_pos = sync_data->check_values;
	cleanup_sync_pos = sync_data->cleanup_syncs;

	/* Everything should be allocated/sanity checked. No errors are
	 * possible after this point.
	 */

	/* Append any check syncs */
	if (sync_data->check_fence) {
		struct sync_file *fence = sync_data->check_fence;
		bool has_foreign_point = false;
		// struct dma_fence_pt *sync_pt;
		struct dma_fence *dma_fence;
		int j;

		if (dma_fence_is_array(fence->fence)) {
			struct dma_fence_array *array = to_dma_fence_array(fence->fence);
			if (!array) {
				pr_err("%s: Failed to resolve dma fence array\n",
					   __func__);
			}

			fences = array->fences;
			num_fences = array->num_fences;
		} else {
			fences = &fence->fence;
			num_fences = 1;
		}

		(void)j;
		for (j = 0,
			 dma_fence = ((num_fences == 0) ? NULL : fences[0]);
			 j < num_fences;
			 j++,
			 dma_fence = ((j < num_fences) ? fences[j] : NULL)) {
			//for_each_sync_pt(sync_pt, fence, j) ...
			struct dma_fence_pt *sync_pt;
			struct pvr_sync_pt *pvr_pt;
			struct pvr_sync_kernel_pair *sync_kernel;

			// if (!is_dma_fence_pt(fences[j])) {
			if (!is_pvr_dma_fence(dma_fence)) {
				// if (!sync_pt_get_status(sync_pt))
				if (!dma_fence_is_signaled(dma_fence))
					has_foreign_point = true;
				continue;
			}

			sync_pt = (struct dma_fence_pt *)dma_fence;
			pvr_pt = (struct pvr_sync_pt *)sync_pt;
			sync_kernel = pvr_pt->sync_data->kernel;

			if (!sync_kernel ||
			    is_sync_met(sync_kernel->fence_sync)) {
				continue;
			}

			(*check_address_pos++).ui32Addr =
				sync_kernel->fence_sync->vaddr;
			*check_value_pos++ =
				sync_kernel->fence_sync->next_value;

			(*update_address_pos++).ui32Addr =
				sync_kernel->current_cleanup_sync->vaddr;
			*update_value_pos++ =
				++sync_kernel->current_cleanup_sync->next_value;
			*cleanup_sync_pos++ = sync_kernel->current_cleanup_sync;

			sync_kernel->current_cleanup_sync = NULL;
		}

		if (has_foreign_point) {
			struct pvr_sync_kernel_pair *foreign_sync_kernel =
				pvr_sync_create_waiter_for_foreign_sync(
					check_fence_fd);

			if (foreign_sync_kernel) {
				struct pvr_sync_native_sync_prim *fence_sync =
					foreign_sync_kernel->fence_sync;
				struct pvr_sync_native_sync_prim *cleanup_sync =
					foreign_sync_kernel->
						current_cleanup_sync;

				(*check_address_pos++).ui32Addr =
					fence_sync->vaddr;
				*check_value_pos++ =
					fence_sync->next_value;

				(*update_address_pos++).ui32Addr =
					cleanup_sync->vaddr;
				*update_value_pos++ =
					++cleanup_sync->next_value;
				*cleanup_sync_pos++ = cleanup_sync;
				foreign_sync_kernel->current_cleanup_sync =
					NULL;
			}
		}
	}

	/* Append the update sync (if requested) */
	if (update_point) {
		struct pvr_sync_data *sync_data =
			update_point->sync_data;
		struct pvr_sync_kernel_pair *sync_kernel =
			sync_data->kernel;

		(*update_address_pos++).ui32Addr =
			sync_kernel->fence_sync->vaddr;
		*update_value_pos++ =
			sync_kernel->fence_sync->next_value;

		(*update_address_pos++).ui32Addr =
			timeline->kernel->fence_sync->vaddr;

		/* Copy in the timeline next value (which was incremented
		 * when this point was created).
		 */
		sync_data->timeline_update_value =
			timeline->kernel->fence_sync->next_value;

		/* ...and set that to be updated when this kick is completed */
		*update_value_pos++ =
			sync_data->timeline_update_value;
	}

	/* We count the total number of sync points we attach, as it's possible
	 * some have become complete since the first loop through, or a waiter
	 * for a foreign point skipped (But they can never become un-complete,
	 * so it will only ever be the same or less, so the allocated arrays
	 * should still be sufficiently sized).
	 */
	num_used_sync_updates =
		update_address_pos - sync_data->update_ufo_addresses;
	num_used_sync_checks =
		check_address_pos - sync_data->check_ufo_addresses;

	sync_data->nr_checks = nr_checks + num_used_sync_checks;
	sync_data->nr_updates = nr_updates + num_used_sync_updates;

	/* Append original check and update sync values/addresses */
	if (update_ufo_addresses)
		memcpy(update_address_pos, update_ufo_addresses,
			sizeof(*update_ufo_addresses) * nr_updates);
	if (update_values)
		memcpy(update_value_pos, update_values,
			sizeof(*update_values) * nr_updates);

	if (check_ufo_addresses)
		memcpy(check_address_pos, check_ufo_addresses,
			sizeof(*check_ufo_addresses) * nr_checks);
	if (check_values)
		memcpy(check_value_pos, check_values,
			sizeof(*check_values) * nr_checks);

	*append_sync_data = sync_data;

	return PVRSRV_OK;

err_free_fence:
	if (update_point) {
		/* First rollback the taken operations */
		timeline->kernel->fence_sync->next_value--;
		update_point->sync_data->kernel->fence_sync->next_value--;
	}
err_free_append_data:
	pvr_sync_free_append_fences_data(sync_data);
err_out:
	return err;
}

void pvr_sync_get_updates(const struct pvr_sync_append_data *sync_data,
	u32 *nr_fences, struct _RGXFWIF_DEV_VIRTADDR_ **ufo_addrs, u32 **values)
{
	*nr_fences = sync_data->nr_updates;
	*ufo_addrs = sync_data->update_ufo_addresses;
	*values = sync_data->update_values;
}

void pvr_sync_get_checks(const struct pvr_sync_append_data *sync_data,
	u32 *nr_fences, struct _RGXFWIF_DEV_VIRTADDR_ **ufo_addrs, u32 **values)
{
	*nr_fences = sync_data->nr_checks;
	*ufo_addrs = sync_data->check_ufo_addresses;
	*values = sync_data->check_values;
}

void pvr_sync_rollback_append_fences(struct pvr_sync_append_data *sync_data)
{
	u32 i;

	if (!sync_data)
		return;

	for (i = 0; i < sync_data->nr_cleanup_syncs; i++) {
		struct pvr_sync_native_sync_prim *cleanup_sync =
			sync_data->cleanup_syncs[i];

		/* If this cleanup was called on a partially-created data set
		 * it's possible to have NULL cleanup sync pointers.
		 */
		if (!cleanup_sync)
			continue;
		cleanup_sync->next_value--;
	}

	/* If there was an update, rollback the next values taken on the
	 * fence and timeline. This must be done before the sync_fence_put()
	 * as that may free the corresponding fence.
	 */

	if (sync_data->update_sync) {
		BUG_ON(sync_data->update_sync->next_value != 1);
		sync_data->update_sync->next_value = 0;
		sync_data->update_sync = NULL;
	}

	if (sync_data->update_timeline_sync) {
		BUG_ON(sync_data->update_timeline_sync->next_value == 0);
		sync_data->update_timeline_sync->next_value--;
		sync_data->update_timeline_sync = NULL;
	}
}

int pvr_sync_get_update_fd(struct pvr_sync_append_data *sync_data)
{
	int fd = -EINVAL;

	if (!sync_data || !sync_data->update_fence ||
		sync_data->update_fence_fd < 0)
		goto err_out;

	fd = sync_data->update_fence_fd;
	sync_data->update_fence_fd = -1;

	pvr_sync_fence_install(sync_data->update_fence, fd);

	/* Note: It is invalid for an FD to have been installed on the update
	 * fence then fput called - as this would leave a dangling reference
	 * in the FD table. Set it to NULL so the free_append_fences_data()
	 * call doesn't fput it.
	 */
	sync_data->update_fence = NULL;

err_out:
	return fd;
}

void pvr_sync_free_append_fences_data(struct pvr_sync_append_data *sync_data)
{
	if (!sync_data)
		return;

	if (sync_data->check_fence)
		sync_file_put(sync_data->check_fence);

	if (sync_data->update_fence)
		sync_file_put(sync_data->update_fence);

	if (sync_data->update_fence_fd >= 0)
		put_unused_fd(sync_data->update_fence_fd);

	kfree(sync_data->update_ufo_addresses);
	kfree(sync_data->update_values);
	kfree(sync_data->check_ufo_addresses);
	kfree(sync_data->check_values);
	kfree(sync_data->cleanup_syncs);
	kfree(sync_data);
}

void pvr_sync_nohw_complete_fences(struct pvr_sync_append_data *sync_data)
{
	u32 i;

	if (!sync_data)
		return;

	for (i = 0; i < sync_data->nr_cleanup_syncs; i++) {
		struct pvr_sync_native_sync_prim *cleanup_sync =
			sync_data->cleanup_syncs[i];

		if (!cleanup_sync)
			continue;

		complete_sync(cleanup_sync);
	}

	if (sync_data->update_sync)
		complete_sync(sync_data->update_sync);
	if (sync_data->update_timeline_sync)
		complete_sync(sync_data->update_timeline_sync);

	pvr_sync_update_all_timelines(NULL);
}

/* ioctl and fops handling */

static int pvr_sync_open(struct inode *inode, struct file *file)
{
	struct pvr_sync_timeline_wrapper *timeline_wrapper;
	struct pvr_sync_timeline *timeline;
	char task_comm[TASK_COMM_LEN];
	enum PVRSRV_ERROR error;
	int err = -ENOMEM;

	get_task_comm(task_comm, current);

//Warning
	timeline_wrapper = (struct pvr_sync_timeline_wrapper *)
		pvr_sync_timeline_create(task_comm, sizeof(*timeline_wrapper), &pvr_fence_ops);
	if (!timeline_wrapper) {
		pr_err("pvr_sync: %s: pvr_sync_timeline_create failed\n", __func__);
		goto err_out;
	}

	timeline = kmalloc(sizeof(*timeline), GFP_KERNEL);
	if (!timeline) {
		pr_err("pvr_sync: %s: Out of memory\n", __func__);
		goto err_free_timeline_wrapper;
	}

	timeline->kernel = kzalloc(sizeof(*timeline->kernel),
				   GFP_KERNEL);
	if (!timeline->kernel) {
		pr_err("pvr_sync: %s: Out of memory\n", __func__);
		goto err_free_timeline;
	}

	INIT_LIST_HEAD(&timeline->kernel->cleanup_sync_list);

	OSAcquireBridgeLock();
	error = sync_pool_get(&timeline->kernel->fence_sync,
			      task_comm, SYNC_TL_TYPE);
	OSReleaseBridgeLock();

	if (error != PVRSRV_OK) {
		pr_err("pvr_sync: %s: Failed to allocate sync prim (%s)\n",
			__func__, PVRSRVGetErrorStringKM(error));
		goto err_free_timeline_kernel;
	}

	timeline_wrapper->timeline = timeline;

	timeline->obj = &timeline_wrapper->obj;
	kref_init(&timeline->kref);

	mutex_lock(&timeline_list_mutex);
	list_add_tail(&timeline->list, &timeline_list);
	mutex_unlock(&timeline_list_mutex);

	DPF("%s: # %s", __func__, debug_info_timeline(timeline));

	file->private_data = timeline_wrapper;
	err = 0;
err_out:
	return err;

err_free_timeline_kernel:
	kfree(timeline->kernel);
err_free_timeline:
	kfree(timeline);

	/* Use a NULL timeline to detect this partially-setup timeline in the
	 * timeline release function (called by sync_timeline_destroy) and
	 * handle it appropriately.
	 */
	timeline_wrapper->timeline = NULL;
err_free_timeline_wrapper:
	pvr_sync_timeline_destroy(&timeline_wrapper->obj);
	goto err_out;
}

static int pvr_sync_close(struct inode *inode, struct file *file)
{
	struct dma_fence_timeline *obj = file->private_data;

	if (is_pvr_timeline(obj)) {
		DPF("%s: # %s", __func__,
		    debug_info_timeline(get_timeline(obj)));
	}

	pvr_sync_timeline_destroy(obj);
	return 0;
}

static long pvr_sync_ioctl_rename(struct pvr_sync_timeline *timeline,
	void __user *user_data)
{
	int err = 0;
	struct pvr_sync_rename_ioctl_data data;

	if (!access_ok(VERIFY_READ, user_data, sizeof(data))) {
		err = -EFAULT;
		goto err;
	}

	if (copy_from_user(&data, user_data, sizeof(data))) {
		err = -EFAULT;
		goto err;
	}

	data.szName[sizeof(data.szName) - 1] = '\0';
	strlcpy(timeline->obj->name, data.szName, sizeof(timeline->obj->name));

	mutex_lock(&sync_pool_mutex);
	strlcpy(timeline->kernel->fence_sync->class, data.szName,
		sizeof(timeline->kernel->fence_sync->class));
	mutex_unlock(&sync_pool_mutex);
err:
	return err;
}

static long
pvr_sync_ioctl(struct file *file, unsigned int cmd, unsigned long __user arg)
{
	struct dma_fence_timeline *obj = file->private_data;
	void __user *user_data = (void __user *)arg;
	long err = -ENOTTY;

	if (is_pvr_timeline(obj)) {
		struct pvr_sync_timeline *pvr = get_timeline(obj);

		switch (cmd) {
		case PVR_SYNC_IOC_RENAME:
			err = pvr_sync_ioctl_rename(pvr, user_data);
			break;
		default:
			break;
		}
	}

	return err;
}

static void
pvr_sync_check_status_work_queue_function(struct work_struct *data)
{
	/* A completed SW operation may un-block the GPU */
	PVRSRVCheckStatus(NULL);
}

/* Returns true if the freelist still has entries, else false if empty */
static bool
pvr_sync_clean_freelist(void)
{
	struct pvr_sync_kernel_pair *kernel, *k;
	struct pvr_sync_fence *sync_fence, *f;
	LIST_HEAD(unlocked_free_list);
	unsigned long flags;
	bool freelist_empty;

	/* We can't call PVRSRVServerSyncFreeKM directly in this loop because
	 * that will take the mmap mutex. We can't take mutexes while we have
	 * this list locked with a spinlock. So move all the items we want to
	 * free to another, local list (no locking required) and process it
	 * in a second loop.
	 */

	spin_lock_irqsave(&sync_prim_free_list_spinlock, flags);
	list_for_each_entry_safe(kernel, k, &sync_prim_free_list, list) {
		bool in_use = false;
		struct list_head *pos;

		/* Check if this sync is not used anymore. */
		if (!is_sync_met(kernel->fence_sync))
			continue;
		list_for_each(pos, &kernel->cleanup_sync_list) {
			struct pvr_sync_native_sync_prim *cleanup_sync =
				list_entry(pos,
					struct pvr_sync_native_sync_prim,
					cleanup_list);

			if (!is_sync_met(cleanup_sync)) {
				in_use = true;
				break;
			}
		}

		if (in_use)
			continue;

		/* Remove the entry from the free list. */
		list_move_tail(&kernel->list, &unlocked_free_list);
	}

	/* Wait and loop if there are still syncs on the free list (IE
	 * are still in use by the HW).
	 */
	freelist_empty = list_empty(&sync_prim_free_list);

	spin_unlock_irqrestore(&sync_prim_free_list_spinlock, flags);

	OSAcquireBridgeLock();

	list_for_each_entry_safe(kernel, k, &unlocked_free_list, list) {
		struct list_head *pos, *n;

		list_del(&kernel->list);

		sync_pool_put(kernel->fence_sync);

		list_for_each_safe(pos, n, &kernel->cleanup_sync_list) {
			struct pvr_sync_native_sync_prim *cleanup_sync =
				list_entry(pos,
					struct pvr_sync_native_sync_prim,
					 cleanup_list);
			list_del(&cleanup_sync->cleanup_list);
			sync_pool_put(cleanup_sync);
		}
		kfree(kernel);
	}

	OSReleaseBridgeLock();

	/* sync_fence_put() must be called from process/WQ context
	 * because it uses fput(), which is not allowed to be called
	 * from interrupt context in kernels <3.6.
	 */
	INIT_LIST_HEAD(&unlocked_free_list);

	spin_lock_irqsave(&sync_fence_put_list_spinlock, flags);
	list_for_each_entry_safe(sync_fence, f, &sync_fence_put_list, list) {
		list_move_tail(&sync_fence->list, &unlocked_free_list);
	}
	spin_unlock_irqrestore(&sync_fence_put_list_spinlock, flags);

	list_for_each_entry_safe(sync_fence, f, &unlocked_free_list, list) {
		list_del(&sync_fence->list);
		sync_file_put(sync_fence->fence);
		kfree(sync_fence);
	}

	return !freelist_empty;
}

static void
pvr_sync_defer_free_work_queue_function(struct work_struct *data)
{
	enum PVRSRV_ERROR error = PVRSRV_OK;
	void *event_object;

	error = OSEventObjectOpen(pvr_sync_data.event_object_handle,
		&event_object);
	if (error != PVRSRV_OK) {
		pr_err("pvr_sync: %s: Error opening event object (%s)\n",
			__func__, PVRSRVGetErrorStringKM(error));
		return;

	}

	while (pvr_sync_clean_freelist()) {

		error = OSEventObjectWait(event_object);

		switch (error) {

		case PVRSRV_OK:
		case PVRSRV_ERROR_TIMEOUT:
			/* Timeout is normal behaviour */
			continue;
		default:
			pr_err("pvr_sync: %s: Error waiting for event object (%s)\n",
				__func__, PVRSRVGetErrorStringKM(error));
			break;
		}
	}
	error = OSEventObjectClose(event_object);
	if (error != PVRSRV_OK) {
		pr_err("pvr_sync: %s: Error closing event object (%s)\n",
			__func__, PVRSRVGetErrorStringKM(error));
	}
}

static const struct file_operations pvr_sync_fops = {
	.owner          = THIS_MODULE,
	.open           = pvr_sync_open,
	.release        = pvr_sync_close,
	.unlocked_ioctl = pvr_sync_ioctl,
	.compat_ioctl   = pvr_sync_ioctl,
};

static struct miscdevice pvr_sync_device = {
	.minor          = MISC_DYNAMIC_MINOR,
	.name           = PVRSYNC_MODNAME,
	.fops           = &pvr_sync_fops,
};

static
void pvr_sync_update_all_timelines(void *command_complete_handle)
{
	struct pvr_sync_timeline *timeline, *n;

	mutex_lock(&timeline_list_mutex);

	list_for_each_entry(timeline, &timeline_list, list) {
		/* If a timeline is destroyed via pvr_sync_release_timeline()
		 * in parallel with a call to pvr_sync_update_all_timelines(),
		 * the timeline_list_mutex will block destruction of the
		 * 'timeline' pointer. Use kref_get_unless_zero() to detect
		 * and handle this race. Skip the timeline if it's being
		 * destroyed, blocked only on the timeline_list_mutex.
		 */
		timeline->valid =
			kref_get_unless_zero(&timeline->kref) ? true : false;
	}

	list_for_each_entry_safe(timeline, n, &timeline_list, list) {
		/* We know timeline is valid at this point because we're
		 * holding the list lock (so pvr_sync_destroy_timeline() has
		 * to wait).
		 */
		void *obj = timeline->obj;

		/* If we're racing with pvr_sync_release_timeline(), ignore */
		if (!timeline->valid)
			continue;

		/* If syncs have signaled on the GPU, echo this in pvr_sync.
		 *
		 * At this point we know the timeline is valid, but obj might
		 * have raced and been set to NULL. It's only important that
		 * we use NULL / non-NULL consistently with the if() and call
		 * to sync_timeline_signal() -- the timeline->obj can't be
		 * freed (pvr_sync_release_timeline() will be stuck waiting
		 * for the timeline_list_mutex) but it might have been made
		 * invalid by the base sync driver, in which case this call
		 * will bounce harmlessly.
		 */
		if (obj)
			pvr_sync_timeline_signal(obj);

		/* We're already holding the timeline_list_mutex */
		kref_put(&timeline->kref, pvr_sync_destroy_timeline_locked);
	}

	mutex_unlock(&timeline_list_mutex);
}

enum PVRSRV_ERROR pvr_sync_init(void *device_cookie)
{
	enum PVRSRV_ERROR error;
	int err;

	DPF("%s", __func__);

	atomic_set(&pvr_sync_data.sync_id, 0);

	error = PVRSRVAcquireGlobalEventObjectKM(
		&pvr_sync_data.event_object_handle);
	if (error != PVRSRV_OK) {
		pr_err("pvr_sync: %s: Failed to acquire global event object (%s)\n",
			__func__, PVRSRVGetErrorStringKM(error));
		goto err_out;
	}

	OSAcquireBridgeLock();

	error = SyncPrimContextCreate(device_cookie,
				      &pvr_sync_data.sync_prim_context);
	if (error != PVRSRV_OK) {
		pr_err("pvr_sync: %s: Failed to create sync prim context (%s)\n",
		       __func__, PVRSRVGetErrorStringKM(error));
		OSReleaseBridgeLock();
		goto err_release_event_object;
	}

	OSReleaseBridgeLock();

	pvr_sync_data.defer_free_wq =
		create_freezable_workqueue("pvr_sync_defer_free_workqueue");
	if (!pvr_sync_data.defer_free_wq) {
		pr_err("pvr_sync: %s: Failed to create pvr_sync defer_free workqueue\n",
		       __func__);
		goto err_free_sync_context;
	}

	INIT_WORK(&pvr_sync_data.defer_free_work,
		pvr_sync_defer_free_work_queue_function);

	pvr_sync_data.check_status_wq =
		create_freezable_workqueue("pvr_sync_check_status_workqueue");
	if (!pvr_sync_data.check_status_wq) {
		pr_err("pvr_sync: %s: Failed to create pvr_sync check_status workqueue\n",
		       __func__);
		goto err_destroy_defer_free_wq;
	}

	INIT_WORK(&pvr_sync_data.check_status_work,
		pvr_sync_check_status_work_queue_function);
	error = PVRSRVRegisterCmdCompleteNotify(
			&pvr_sync_data.command_complete_handle,
			&pvr_sync_update_all_timelines,
			&device_cookie);
	if (error != PVRSRV_OK) {
		pr_err("pvr_sync: %s: Failed to register MISR notification (%s)\n",
		       __func__, PVRSRVGetErrorStringKM(error));
		goto err_destroy_status_wq;
	}

	error = PVRSRVRegisterDbgRequestNotify(
			&pvr_sync_data.debug_notify_handle,
			device_cookie,
			pvr_sync_debug_request,
			DEBUG_REQUEST_ANDROIDSYNC,
			NULL);
	if (error != PVRSRV_OK) {
		pr_err("pvr_sync: %s: Failed to register debug notifier (%s)\n",
			__func__, PVRSRVGetErrorStringKM(error));
		goto err_unregister_cmd_complete;
	}

	err = misc_register(&pvr_sync_device);
	if (err) {
		pr_err("pvr_sync: %s: Failed to register pvr_sync device (%d)\n",
		       __func__, err);
		error = PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
		goto err_unregister_dbg;
	}

	error = PVRSRV_OK;
	return error;

err_unregister_dbg:
	PVRSRVUnregisterDbgRequestNotify(pvr_sync_data.debug_notify_handle);
err_unregister_cmd_complete:
	PVRSRVUnregisterCmdCompleteNotify(
		pvr_sync_data.command_complete_handle);
err_destroy_status_wq:
	destroy_workqueue(pvr_sync_data.check_status_wq);
err_destroy_defer_free_wq:
	destroy_workqueue(pvr_sync_data.defer_free_wq);
err_free_sync_context:
	OSAcquireBridgeLock();
	SyncPrimContextDestroy(pvr_sync_data.sync_prim_context);
	OSReleaseBridgeLock();
err_release_event_object:
	PVRSRVReleaseGlobalEventObjectKM(pvr_sync_data.event_object_handle);
err_out:

	return error;
}

void pvr_sync_deinit(void)
{
	DPF("%s", __func__);

	misc_deregister(&pvr_sync_device);

	PVRSRVUnregisterDbgRequestNotify(pvr_sync_data.debug_notify_handle);

	PVRSRVUnregisterCmdCompleteNotify(
		pvr_sync_data.command_complete_handle);

	/* This will drain the workqueue, so we guarantee that all deferred
	 * syncs are free'd before returning.
	 */
	destroy_workqueue(pvr_sync_data.defer_free_wq);
	destroy_workqueue(pvr_sync_data.check_status_wq);

	OSAcquireBridgeLock();

	sync_pool_clear();

	SyncPrimContextDestroy(pvr_sync_data.sync_prim_context);

	OSReleaseBridgeLock();

	PVRSRVReleaseGlobalEventObjectKM(pvr_sync_data.event_object_handle);
}
