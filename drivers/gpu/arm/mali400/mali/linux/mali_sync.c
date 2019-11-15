/*
 * Copyright (C) 2012-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_sync.h"

#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_timeline.h"
#include "mali_executor.h"

#include <linux/file.h>
#include <linux/seq_file.h>
#include <linux/module.h>

struct mali_sync_pt {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
	struct sync_pt         sync_pt;
#else
	struct mali_internal_sync_point         sync_pt;
#endif
	struct mali_sync_flag *flag;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
	struct sync_timeline *sync_tl;  /**< Sync timeline this pt is connected to. */
#else
	struct mali_internal_sync_timeline *sync_tl;  /**< Sync timeline this pt is connected to. */
#endif
};

/**
 * The sync flag is used to connect sync fences to the Mali Timeline system.  Sync fences can be
 * created from a sync flag, and when the flag is signaled, the sync fences will also be signaled.
 */
struct mali_sync_flag {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
	struct sync_timeline *sync_tl;  /**< Sync timeline this flag is connected to. */
#else
	struct mali_internal_sync_timeline *sync_tl;  /**< Sync timeline this flag is connected to. */
#endif
	u32                   point;    /**< Point on timeline. */
	int                   status;   /**< 0 if unsignaled, 1 if signaled without error or negative if signaled with error. */
	struct kref           refcount; /**< Reference count. */
};

/**
 * Mali sync timeline is used to connect mali timeline to sync_timeline.
 * When fence timeout can print more detailed mali timeline system info.
 */
struct mali_sync_timeline_container {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
	struct sync_timeline sync_timeline;
#else
	struct mali_internal_sync_timeline sync_timeline;
#endif
	struct mali_timeline *timeline;
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
MALI_STATIC_INLINE struct mali_sync_pt *to_mali_sync_pt(struct sync_pt *pt)
#else
MALI_STATIC_INLINE struct mali_sync_pt *to_mali_sync_pt(struct mali_internal_sync_point *pt)
#endif
{
	return container_of(pt, struct mali_sync_pt, sync_pt);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
MALI_STATIC_INLINE struct mali_sync_timeline_container *to_mali_sync_tl_container(struct sync_timeline *sync_tl)
#else
MALI_STATIC_INLINE struct mali_sync_timeline_container *to_mali_sync_tl_container(struct mali_internal_sync_timeline *sync_tl)
#endif
{
	return container_of(sync_tl, struct mali_sync_timeline_container, sync_timeline);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
static int timeline_has_signaled(struct sync_pt *pt)
#else
static int timeline_has_signaled(struct mali_internal_sync_point *pt)
#endif
{
	struct mali_sync_pt *mpt;

	MALI_DEBUG_ASSERT_POINTER(pt);
	mpt = to_mali_sync_pt(pt);

	MALI_DEBUG_ASSERT_POINTER(mpt->flag);

	return mpt->flag->status;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
static void timeline_free_pt(struct sync_pt *pt)
#else
static void timeline_free_pt(struct mali_internal_sync_point *pt)
#endif
{
	struct mali_sync_pt *mpt;

	MALI_DEBUG_ASSERT_POINTER(pt);
	mpt = to_mali_sync_pt(pt);

	mali_sync_flag_put(mpt->flag);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
static void timeline_release(struct sync_timeline *sync_timeline)
#else
static void timeline_release(struct mali_internal_sync_timeline *sync_timeline)
#endif
{
	struct mali_sync_timeline_container *mali_sync_tl = NULL;
	struct mali_timeline *mali_tl = NULL;

	MALI_DEBUG_ASSERT_POINTER(sync_timeline);

	mali_sync_tl = to_mali_sync_tl_container(sync_timeline);
	MALI_DEBUG_ASSERT_POINTER(mali_sync_tl);

	mali_tl = mali_sync_tl->timeline;

	/* always signaled timeline didn't have mali container */
	if (mali_tl) {
		if (NULL != mali_tl->spinlock) {
			mali_spinlock_reentrant_term(mali_tl->spinlock);
		}
		_mali_osk_free(mali_tl);
	}

	module_put(THIS_MODULE);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
static struct sync_pt *timeline_dup(struct sync_pt *pt)
{
	struct mali_sync_pt *mpt, *new_mpt;
	struct sync_pt *new_pt;
	MALI_DEBUG_ASSERT_POINTER(pt);

	mpt = to_mali_sync_pt(pt);

	new_pt = sync_pt_create(mpt->sync_tl, sizeof(struct mali_sync_pt));
	if (NULL == new_pt) return NULL;

	new_mpt = to_mali_sync_pt(new_pt);

	mali_sync_flag_get(mpt->flag);
	new_mpt->flag = mpt->flag;
	new_mpt->sync_tl = mpt->sync_tl;

	return new_pt;
}

static int timeline_compare(struct sync_pt *pta, struct sync_pt *ptb)
{
	struct mali_sync_pt *mpta;
	struct mali_sync_pt *mptb;
	u32 a, b;

	MALI_DEBUG_ASSERT_POINTER(pta);
	MALI_DEBUG_ASSERT_POINTER(ptb);
	mpta = to_mali_sync_pt(pta);
	mptb = to_mali_sync_pt(ptb);

	MALI_DEBUG_ASSERT_POINTER(mpta->flag);
	MALI_DEBUG_ASSERT_POINTER(mptb->flag);

	a = mpta->flag->point;
	b = mptb->flag->point;

	if (a == b) return 0;

	return ((b - a) < (a - b) ? -1 : 1);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
static void timeline_print_pt(struct seq_file *s, struct sync_pt *sync_pt)
{
	struct mali_sync_pt *mpt;

	MALI_DEBUG_ASSERT_POINTER(s);
	MALI_DEBUG_ASSERT_POINTER(sync_pt);

	mpt = to_mali_sync_pt(sync_pt);

	/* It is possible this sync point is just under construct,
	 * make sure the flag is valid before accessing it
	*/
	if (mpt->flag) {
		seq_printf(s, "%u", mpt->flag->point);
	} else {
		seq_printf(s, "uninitialized");
	}
}

static void timeline_print_obj(struct seq_file *s, struct sync_timeline *sync_tl)
{
	struct mali_sync_timeline_container *mali_sync_tl = NULL;
	struct mali_timeline *mali_tl = NULL;

	MALI_DEBUG_ASSERT_POINTER(sync_tl);

	mali_sync_tl = to_mali_sync_tl_container(sync_tl);
	MALI_DEBUG_ASSERT_POINTER(mali_sync_tl);

	mali_tl = mali_sync_tl->timeline;

	if (NULL != mali_tl) {
		seq_printf(s, "oldest (%u) ", mali_tl->point_oldest);
		seq_printf(s, "next (%u)", mali_tl->point_next);
		seq_printf(s, "\n");

#if defined(MALI_TIMELINE_DEBUG_FUNCTIONS)
		{
			u32 tid = _mali_osk_get_tid();
			struct mali_timeline_system *system = mali_tl->system;

			mali_spinlock_reentrant_wait(mali_tl->spinlock, tid);
			if (!mali_tl->destroyed) {
				mali_spinlock_reentrant_wait(system->spinlock, tid);
				mali_timeline_debug_print_timeline(mali_tl, s);
				mali_spinlock_reentrant_signal(system->spinlock, tid);
			}
			mali_spinlock_reentrant_signal(mali_tl->spinlock, tid);

			/* dump job queue status and group running status */
			mali_executor_status_dump();
		}
#endif
	}
}
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
static void timeline_pt_value_str(struct sync_pt *pt, char *str, int size)
{
	struct mali_sync_pt *mpt;

	MALI_DEBUG_ASSERT_POINTER(str);
	MALI_DEBUG_ASSERT_POINTER(pt);

	mpt = to_mali_sync_pt(pt);

	/* It is possible this sync point is just under construct,
	 * make sure the flag is valid before accessing it
	*/
	if (mpt->flag) {
		_mali_osk_snprintf(str, size, "%u", mpt->flag->point);
	} else {
		_mali_osk_snprintf(str, size, "uninitialized");
	}
}

static void timeline_value_str(struct sync_timeline *timeline, char *str, int size)
{
	struct mali_sync_timeline_container *mali_sync_tl = NULL;
	struct mali_timeline *mali_tl = NULL;

	MALI_DEBUG_ASSERT_POINTER(timeline);

	mali_sync_tl = to_mali_sync_tl_container(timeline);
	MALI_DEBUG_ASSERT_POINTER(mali_sync_tl);

	mali_tl = mali_sync_tl->timeline;

	if (NULL != mali_tl) {
		_mali_osk_snprintf(str, size, "oldest (%u) ", mali_tl->point_oldest);
		_mali_osk_snprintf(str, size, "next (%u)", mali_tl->point_next);
		_mali_osk_snprintf(str, size, "\n");

#if defined(MALI_TIMELINE_DEBUG_FUNCTIONS)
		{
			u32 tid = _mali_osk_get_tid();
			struct mali_timeline_system *system = mali_tl->system;

			mali_spinlock_reentrant_wait(mali_tl->spinlock, tid);
			if (!mali_tl->destroyed) {
				mali_spinlock_reentrant_wait(system->spinlock, tid);
				mali_timeline_debug_direct_print_timeline(mali_tl);
				mali_spinlock_reentrant_signal(system->spinlock, tid);
			}
			mali_spinlock_reentrant_signal(mali_tl->spinlock, tid);

			/* dump job queue status and group running status */
			mali_executor_status_dump();
		}
#endif
	}
}
#else
static void timeline_print_sync_pt(struct mali_internal_sync_point *sync_pt)
{
	struct mali_sync_pt *mpt;

	MALI_DEBUG_ASSERT_POINTER(sync_pt);

	mpt = to_mali_sync_pt(sync_pt);

	if (mpt->flag) {
		MALI_DEBUG_PRINT(2, ("mali_internal_sync_pt: %u\n", mpt->flag->point));
	} else {
		MALI_DEBUG_PRINT(2, ("uninitialized\n", mpt->flag->point));
	}
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
static struct sync_timeline_ops mali_timeline_ops = {
	.driver_name    = "Mali",
	.dup            = timeline_dup,
	.has_signaled   = timeline_has_signaled,
	.compare        = timeline_compare,
	.free_pt        = timeline_free_pt,
	.release_obj    = timeline_release,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
	.print_pt       = timeline_print_pt,
	.print_obj      = timeline_print_obj,
#else
	.pt_value_str = timeline_pt_value_str,
	.timeline_value_str = timeline_value_str,
#endif
};

struct sync_timeline *mali_sync_timeline_create(struct mali_timeline *timeline, const char *name)
{
	struct sync_timeline *sync_tl;
	struct mali_sync_timeline_container *mali_sync_tl;

	sync_tl = sync_timeline_create(&mali_timeline_ops, sizeof(struct mali_sync_timeline_container), name);
	if (NULL == sync_tl) return NULL;

	mali_sync_tl = to_mali_sync_tl_container(sync_tl);
	mali_sync_tl->timeline = timeline;

	/* Grab a reference on the module to ensure the callbacks are present
	 * as long some timeline exists. The reference is released when the
	 * timeline is freed.
	 * Since this function is called from a ioctl on an open file we know
	 * we already have a reference, so using __module_get is safe. */
	__module_get(THIS_MODULE);

	return sync_tl;
}

s32 mali_sync_fence_fd_alloc(struct sync_fence *sync_fence)
{
	s32 fd = -1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)
	fd = get_unused_fd();
#else
	fd = get_unused_fd_flags(0);
#endif

	if (fd < 0) {
		sync_fence_put(sync_fence);
		return -1;
	}
	sync_fence_install(sync_fence, fd);

	return fd;
}

struct sync_fence *mali_sync_fence_merge(struct sync_fence *sync_fence1, struct sync_fence *sync_fence2)
{
	struct sync_fence *sync_fence;

	MALI_DEBUG_ASSERT_POINTER(sync_fence1);
	MALI_DEBUG_ASSERT_POINTER(sync_fence1);

	sync_fence = sync_fence_merge("mali_merge_fence", sync_fence1, sync_fence2);
	sync_fence_put(sync_fence1);
	sync_fence_put(sync_fence2);

	return sync_fence;
}

struct sync_fence *mali_sync_timeline_create_signaled_fence(struct sync_timeline *sync_tl)
{
	struct mali_sync_flag *flag;
	struct sync_fence *sync_fence;

	MALI_DEBUG_ASSERT_POINTER(sync_tl);

	flag = mali_sync_flag_create(sync_tl, 0);
	if (NULL == flag) return NULL;

	sync_fence = mali_sync_flag_create_fence(flag);

	mali_sync_flag_signal(flag, 0);
	mali_sync_flag_put(flag);

	return sync_fence;
}

struct mali_sync_flag *mali_sync_flag_create(struct sync_timeline *sync_tl, mali_timeline_point point)
{
	struct mali_sync_flag *flag;

	if (NULL == sync_tl) return NULL;

	flag = _mali_osk_calloc(1, sizeof(*flag));
	if (NULL == flag) return NULL;

	flag->sync_tl = sync_tl;
	flag->point = point;

	flag->status = 0;
	kref_init(&flag->refcount);

	return flag;
}

/**
 * Create a sync point attached to given sync flag.
 *
 * @note Sync points must be triggered in *exactly* the same order as they are created.
 *
 * @param flag Sync flag.
 * @return New sync point if successful, NULL if not.
 */
static struct sync_pt *mali_sync_flag_create_pt(struct mali_sync_flag *flag)
{
	struct sync_pt *pt;
	struct mali_sync_pt *mpt;

	MALI_DEBUG_ASSERT_POINTER(flag);
	MALI_DEBUG_ASSERT_POINTER(flag->sync_tl);

	pt = sync_pt_create(flag->sync_tl, sizeof(struct mali_sync_pt));
	if (NULL == pt) return NULL;

	mali_sync_flag_get(flag);

	mpt = to_mali_sync_pt(pt);
	mpt->flag = flag;
	mpt->sync_tl = flag->sync_tl;

	return pt;
}

struct sync_fence *mali_sync_flag_create_fence(struct mali_sync_flag *flag)
{
	struct sync_pt    *sync_pt;
	struct sync_fence *sync_fence;

	MALI_DEBUG_ASSERT_POINTER(flag);
	MALI_DEBUG_ASSERT_POINTER(flag->sync_tl);

	sync_pt = mali_sync_flag_create_pt(flag);
	if (NULL == sync_pt) return NULL;

	sync_fence = sync_fence_create("mali_flag_fence", sync_pt);
	if (NULL == sync_fence) {
		sync_pt_free(sync_pt);
		return NULL;
	}

	return sync_fence;
}
#else
static struct mali_internal_sync_timeline_ops mali_timeline_ops = {
	.driver_name    = "Mali",
	.has_signaled   = timeline_has_signaled,
	.free_pt        = timeline_free_pt,
	.release_obj    = timeline_release,
	.print_sync_pt = timeline_print_sync_pt,
};

struct mali_internal_sync_timeline *mali_sync_timeline_create(struct mali_timeline *timeline, const char *name)
{
	struct mali_internal_sync_timeline *sync_tl;
	struct mali_sync_timeline_container *mali_sync_tl;

	sync_tl = mali_internal_sync_timeline_create(&mali_timeline_ops, sizeof(struct mali_sync_timeline_container), name);
	if (NULL == sync_tl) return NULL;

	mali_sync_tl = to_mali_sync_tl_container(sync_tl);
	mali_sync_tl->timeline = timeline;

	/* Grab a reference on the module to ensure the callbacks are present
	 * as long some timeline exists. The reference is released when the
	 * timeline is freed.
	 * Since this function is called from a ioctl on an open file we know
	 * we already have a reference, so using __module_get is safe. */
	__module_get(THIS_MODULE);

	return sync_tl;
}

s32 mali_sync_fence_fd_alloc(struct mali_internal_sync_fence *sync_fence)
{
	s32 fd = -1;

	fd = get_unused_fd_flags(0);

	if (fd < 0) {
		fput(sync_fence->file);
		return -1;
	}
	fd_install(fd, sync_fence->file);
	return fd;
}

struct mali_internal_sync_fence *mali_sync_fence_merge(struct mali_internal_sync_fence *sync_fence1, struct mali_internal_sync_fence *sync_fence2)
{
	struct mali_internal_sync_fence *sync_fence;

	MALI_DEBUG_ASSERT_POINTER(sync_fence1);
	MALI_DEBUG_ASSERT_POINTER(sync_fence1);

	sync_fence = mali_internal_sync_fence_merge(sync_fence1, sync_fence2);
	fput(sync_fence1->file);
	fput(sync_fence2->file);

	return sync_fence;
}

struct mali_internal_sync_fence *mali_sync_timeline_create_signaled_fence(struct mali_internal_sync_timeline *sync_tl)
{
	struct mali_sync_flag *flag;
	struct mali_internal_sync_fence *sync_fence;

	MALI_DEBUG_ASSERT_POINTER(sync_tl);

	flag = mali_sync_flag_create(sync_tl, 0);
	if (NULL == flag) return NULL;

	sync_fence = mali_sync_flag_create_fence(flag);

	mali_sync_flag_signal(flag, 0);
	mali_sync_flag_put(flag);

	return sync_fence;
}

struct mali_sync_flag *mali_sync_flag_create(struct mali_internal_sync_timeline *sync_tl, mali_timeline_point point)
{
	struct mali_sync_flag *flag;

	if (NULL == sync_tl) return NULL;

	flag = _mali_osk_calloc(1, sizeof(*flag));
	if (NULL == flag) return NULL;

	flag->sync_tl = sync_tl;
	flag->point = point;

	flag->status = 0;
	kref_init(&flag->refcount);

	return flag;
}

/**
 * Create a sync point attached to given sync flag.
 *
 * @note Sync points must be triggered in *exactly* the same order as they are created.
 *
 * @param flag Sync flag.
 * @return New sync point if successful, NULL if not.
 */
static struct mali_internal_sync_point *mali_sync_flag_create_pt(struct mali_sync_flag *flag)
{
	struct mali_internal_sync_point *pt;
	struct mali_sync_pt *mpt;

	MALI_DEBUG_ASSERT_POINTER(flag);
	MALI_DEBUG_ASSERT_POINTER(flag->sync_tl);

	pt = mali_internal_sync_point_create(flag->sync_tl, sizeof(struct mali_sync_pt));

	if (pt == NULL) {
		MALI_PRINT_ERROR(("Mali sync: sync_pt creation failed\n"));
		return NULL;
	}
	mali_sync_flag_get(flag);

	mpt = to_mali_sync_pt(pt);
	mpt->flag = flag;
	mpt->sync_tl = flag->sync_tl;

	return pt;
}

struct mali_internal_sync_fence *mali_sync_flag_create_fence(struct mali_sync_flag *flag)
{
	struct mali_internal_sync_point    *sync_pt;
	struct mali_internal_sync_fence *sync_fence;

	MALI_DEBUG_ASSERT_POINTER(flag);
	MALI_DEBUG_ASSERT_POINTER(flag->sync_tl);

	sync_pt = mali_sync_flag_create_pt(flag);
	if (NULL == sync_pt) {
		MALI_PRINT_ERROR(("Mali sync: sync_pt creation failed\n"));
		return NULL;
	}
	sync_fence = (struct mali_internal_sync_fence *)sync_file_create(&sync_pt->base);
	if (NULL == sync_fence) {
		MALI_PRINT_ERROR(("Mali sync: sync_fence creation failed\n"));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
		dma_fence_put(&sync_pt->base);
#else
		fence_put(&sync_pt->base);
#endif
		return NULL;
	}

	/* 'sync_pt' no longer needs to hold a refcount of '*sync_pt', to put it off. */
	dma_fence_put(&sync_pt->base);
	sync_pt = NULL;

	return sync_fence;
}
#endif

void mali_sync_flag_get(struct mali_sync_flag *flag)
{
	MALI_DEBUG_ASSERT_POINTER(flag);
	kref_get(&flag->refcount);
}

/**
 * Free sync flag.
 *
 * @param ref kref object embedded in sync flag that should be freed.
 */
static void mali_sync_flag_free(struct kref *ref)
{
	struct mali_sync_flag *flag;

	MALI_DEBUG_ASSERT_POINTER(ref);
	flag = container_of(ref, struct mali_sync_flag, refcount);

	_mali_osk_free(flag);
}

void mali_sync_flag_put(struct mali_sync_flag *flag)
{
	MALI_DEBUG_ASSERT_POINTER(flag);
	kref_put(&flag->refcount, mali_sync_flag_free);
}

void mali_sync_flag_signal(struct mali_sync_flag *flag, int error)
{
	MALI_DEBUG_ASSERT_POINTER(flag);

	MALI_DEBUG_ASSERT(0 == flag->status);
	flag->status = (0 > error) ? error : 1;

	_mali_osk_write_mem_barrier();
#if  LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
	sync_timeline_signal(flag->sync_tl);
#else
	mali_internal_sync_timeline_signal(flag->sync_tl);
#endif
}


