/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2012-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include "mali_sync.h"

#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_timeline.h"

#include <linux/file.h>
#include <linux/seq_file.h>
#include <linux/module.h>

struct mali_sync_pt {
	struct sync_pt         sync_pt;
	struct mali_sync_flag *flag;
};

/**
 * The sync flag is used to connect sync fences to the Mali Timeline system.  Sync fences can be
 * created from a sync flag, and when the flag is signaled, the sync fences will also be signaled.
 */
struct mali_sync_flag {
	struct sync_timeline *sync_tl;  /**< Sync timeline this flag is connected to. */
	u32                   point;    /**< Point on timeline. */
	int                   status;   /**< 0 if unsignaled, 1 if signaled without error or negative if signaled with error. */
	struct kref           refcount; /**< Reference count. */
};

MALI_STATIC_INLINE struct mali_sync_pt *to_mali_sync_pt(struct sync_pt *pt)
{
	return container_of(pt, struct mali_sync_pt, sync_pt);
}

static struct sync_pt *timeline_dup(struct sync_pt *pt)
{
	struct mali_sync_pt *mpt, *new_mpt;
	struct sync_pt *new_pt;

	MALI_DEBUG_ASSERT_POINTER(pt);
	mpt = to_mali_sync_pt(pt);

	new_pt = sync_pt_create(pt->parent, sizeof(struct mali_sync_pt));
	if (NULL == new_pt) return NULL;

	new_mpt = to_mali_sync_pt(new_pt);

	mali_sync_flag_get(mpt->flag);
	new_mpt->flag = mpt->flag;

	return new_pt;
}

static int timeline_has_signaled(struct sync_pt *pt)
{
	struct mali_sync_pt *mpt;

	MALI_DEBUG_ASSERT_POINTER(pt);
	mpt = to_mali_sync_pt(pt);

	MALI_DEBUG_ASSERT_POINTER(mpt->flag);

	return mpt->flag->status;
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
	b = mpta->flag->point;

	if (a == b) return 0;

	return ((b - a) < (a - b) ? -1 : 1);
}

static void timeline_free_pt(struct sync_pt *pt)
{
	struct mali_sync_pt *mpt;

	MALI_DEBUG_ASSERT_POINTER(pt);
	mpt = to_mali_sync_pt(pt);

	mali_sync_flag_put(mpt->flag);
}

static void timeline_release(struct sync_timeline *sync_timeline)
{
	module_put(THIS_MODULE);
}

static void timeline_print_pt(struct seq_file *s, struct sync_pt *sync_pt)
{
	struct mali_sync_pt *mpt;

	MALI_DEBUG_ASSERT_POINTER(s);
	MALI_DEBUG_ASSERT_POINTER(sync_pt);

	mpt = to_mali_sync_pt(sync_pt);
	MALI_DEBUG_ASSERT_POINTER(mpt->flag);

	seq_printf(s, "%u", mpt->flag->point);
}

static struct sync_timeline_ops mali_timeline_ops = {
	.driver_name    = "Mali",
	.dup            = timeline_dup,
	.has_signaled   = timeline_has_signaled,
	.compare        = timeline_compare,
	.free_pt        = timeline_free_pt,
	.release_obj    = timeline_release,
	.print_pt       = timeline_print_pt,
};

struct sync_timeline *mali_sync_timeline_create(const char *name)
{
	struct sync_timeline *sync_tl;

	sync_tl = sync_timeline_create(&mali_timeline_ops, sizeof(struct sync_timeline), name);
	if (NULL == sync_tl) return NULL;

	/* Grab a reference on the module to ensure the callbacks are present
	 * as long some timeline exists. The reference is released when the
	 * timeline is freed.
	 * Since this function is called from a ioctl on an open file we know
	 * we already have a reference, so using __module_get is safe. */
	__module_get(THIS_MODULE);

	return sync_tl;
}

mali_bool mali_sync_timeline_is_ours(struct sync_timeline *sync_tl)
{
	MALI_DEBUG_ASSERT_POINTER(sync_tl);
	return (sync_tl->ops == &mali_timeline_ops) ? MALI_TRUE : MALI_FALSE;
}

s32 mali_sync_fence_fd_alloc(struct sync_fence *sync_fence)
{
	s32 fd = -1;

	fd = get_unused_fd();
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

	sync_timeline_signal(flag->sync_tl);
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
