/*
 * Copyright (C) 2013-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_timeline.h"
#include "mali_kernel_common.h"
#include "mali_scheduler.h"
#include "mali_soft_job.h"
#include "mali_timeline_fence_wait.h"
#include "mali_timeline_sync_fence.h"
#include "mali_executor.h"
#include "mali_pp_job.h"

#define MALI_TIMELINE_SYSTEM_LOCKED(system) (mali_spinlock_reentrant_is_held((system)->spinlock, _mali_osk_get_tid()))

/*
 * Following three elements are used to record how many
 * gp, physical pp or virtual pp jobs are delayed in the whole
 * timeline system, we can use these three value to decide
 * if need to deactivate idle group.
 */
_mali_osk_atomic_t gp_tracker_count;
_mali_osk_atomic_t phy_pp_tracker_count;
_mali_osk_atomic_t virt_pp_tracker_count;

static mali_scheduler_mask mali_timeline_system_release_waiter(struct mali_timeline_system *system,
		struct mali_timeline_waiter *waiter);

#if defined(CONFIG_SYNC)
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>

struct mali_deferred_fence_put_entry {
	struct hlist_node list;
	struct sync_fence *fence;
};

static HLIST_HEAD(mali_timeline_sync_fence_to_free_list);
static DEFINE_SPINLOCK(mali_timeline_sync_fence_to_free_lock);

static void put_sync_fences(struct work_struct *ignore)
{
	struct hlist_head list;
	struct hlist_node *tmp, *pos;
	unsigned long flags;
	struct mali_deferred_fence_put_entry *o;

	spin_lock_irqsave(&mali_timeline_sync_fence_to_free_lock, flags);
	hlist_move_list(&mali_timeline_sync_fence_to_free_list, &list);
	spin_unlock_irqrestore(&mali_timeline_sync_fence_to_free_lock, flags);

	hlist_for_each_entry_safe(o, pos, tmp, &list, list) {
		sync_fence_put(o->fence);
		kfree(o);
	}
}

static DECLARE_DELAYED_WORK(delayed_sync_fence_put, put_sync_fences);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0) */

/* Callback that is called when a sync fence a tracker is waiting on is signaled. */
static void mali_timeline_sync_fence_callback(struct sync_fence *sync_fence, struct sync_fence_waiter *sync_fence_waiter)
{
	struct mali_timeline_system  *system;
	struct mali_timeline_waiter  *waiter;
	struct mali_timeline_tracker *tracker;
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;
	u32 tid = _mali_osk_get_tid();
	mali_bool is_aborting = MALI_FALSE;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
	int fence_status = sync_fence->status;
#else
	int fence_status = atomic_read(&sync_fence->status);
#endif

	MALI_DEBUG_ASSERT_POINTER(sync_fence);
	MALI_DEBUG_ASSERT_POINTER(sync_fence_waiter);

	tracker = _MALI_OSK_CONTAINER_OF(sync_fence_waiter, struct mali_timeline_tracker, sync_fence_waiter);
	MALI_DEBUG_ASSERT_POINTER(tracker);

	system = tracker->system;
	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT_POINTER(system->session);

	mali_spinlock_reentrant_wait(system->spinlock, tid);

	is_aborting = system->session->is_aborting;
	if (!is_aborting && (0 > fence_status)) {
		MALI_PRINT_ERROR(("Mali Timeline: sync fence fd %d signaled with error %d\n", tracker->fence.sync_fd, fence_status));
		tracker->activation_error |= MALI_TIMELINE_ACTIVATION_ERROR_SYNC_BIT;
	}

	waiter = tracker->waiter_sync;
	MALI_DEBUG_ASSERT_POINTER(waiter);

	tracker->sync_fence = NULL;
	tracker->fence.sync_fd = -1;

	schedule_mask |= mali_timeline_system_release_waiter(system, waiter);

	/* If aborting, wake up sleepers that are waiting for sync fence callbacks to complete. */
	if (is_aborting) {
		_mali_osk_wait_queue_wake_up(system->wait_queue);
	}

	mali_spinlock_reentrant_signal(system->spinlock, tid);

	/*
	 * Older versions of Linux, before 3.5, doesn't support fput() in interrupt
	 * context. For those older kernels, allocate a list object and put the
	 * fence object on that and defer the call to sync_fence_put() to a workqueue.
	 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
	{
		struct mali_deferred_fence_put_entry *obj;

		obj = kzalloc(sizeof(struct mali_deferred_fence_put_entry), GFP_ATOMIC);
		if (obj) {
			unsigned long flags;
			mali_bool schedule = MALI_FALSE;

			obj->fence = sync_fence;

			spin_lock_irqsave(&mali_timeline_sync_fence_to_free_lock, flags);
			if (hlist_empty(&mali_timeline_sync_fence_to_free_list))
				schedule = MALI_TRUE;
			hlist_add_head(&obj->list, &mali_timeline_sync_fence_to_free_list);
			spin_unlock_irqrestore(&mali_timeline_sync_fence_to_free_lock, flags);

			if (schedule)
				schedule_delayed_work(&delayed_sync_fence_put, 0);
		}
	}
#else
	sync_fence_put(sync_fence);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0) */

	if (!is_aborting) {
		mali_executor_schedule_from_mask(schedule_mask, MALI_TRUE);
	}
}
#endif /* defined(CONFIG_SYNC) */

static mali_scheduler_mask mali_timeline_tracker_time_out(struct mali_timeline_tracker *tracker)
{
	MALI_DEBUG_ASSERT_POINTER(tracker);
	MALI_DEBUG_ASSERT(MALI_TIMELINE_TRACKER_SOFT == tracker->type);

	return mali_soft_job_system_timeout_job((struct mali_soft_job *) tracker->job);
}

static void mali_timeline_timer_callback(void *data)
{
	struct mali_timeline_system *system;
	struct mali_timeline_tracker *tracker;
	struct mali_timeline *timeline;
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;
	u32 tid = _mali_osk_get_tid();

	timeline = (struct mali_timeline *) data;
	MALI_DEBUG_ASSERT_POINTER(timeline);

	system = timeline->system;
	MALI_DEBUG_ASSERT_POINTER(system);

	mali_spinlock_reentrant_wait(system->spinlock, tid);

	if (!system->timer_enabled) {
		mali_spinlock_reentrant_signal(system->spinlock, tid);
		return;
	}

	tracker = timeline->tracker_tail;
	timeline->timer_active = MALI_FALSE;

	if (NULL != tracker && MALI_TRUE == tracker->timer_active) {
		/* This is likely the delayed work that has been schedule out before cancelled. */
		if (MALI_TIMELINE_TIMEOUT_HZ > (_mali_osk_time_tickcount() - tracker->os_tick_activate)) {
			mali_spinlock_reentrant_signal(system->spinlock, tid);
			return;
		}

		schedule_mask = mali_timeline_tracker_time_out(tracker);
		tracker->timer_active = MALI_FALSE;
	} else {
		MALI_PRINT_ERROR(("Mali Timeline: Soft job timer callback without a waiting tracker.\n"));
	}

	mali_spinlock_reentrant_signal(system->spinlock, tid);

	mali_executor_schedule_from_mask(schedule_mask, MALI_FALSE);
}

void mali_timeline_system_stop_timer(struct mali_timeline_system *system)
{
	u32 i;
	u32 tid = _mali_osk_get_tid();

	MALI_DEBUG_ASSERT_POINTER(system);

	mali_spinlock_reentrant_wait(system->spinlock, tid);
	system->timer_enabled = MALI_FALSE;
	mali_spinlock_reentrant_signal(system->spinlock, tid);

	for (i = 0; i < MALI_TIMELINE_MAX; ++i) {
		struct mali_timeline *timeline = system->timelines[i];

		MALI_DEBUG_ASSERT_POINTER(timeline);

		if (NULL != timeline->delayed_work) {
			_mali_osk_wq_delayed_cancel_work_sync(timeline->delayed_work);
			timeline->timer_active = MALI_FALSE;
		}
	}
}

static void mali_timeline_destroy(struct mali_timeline *timeline)
{
	MALI_DEBUG_ASSERT_POINTER(timeline);
	if (NULL != timeline) {
		/* Assert that the timeline object has been properly cleaned up before destroying it. */
		MALI_DEBUG_ASSERT(timeline->point_oldest == timeline->point_next);
		MALI_DEBUG_ASSERT(NULL == timeline->tracker_head);
		MALI_DEBUG_ASSERT(NULL == timeline->tracker_tail);
		MALI_DEBUG_ASSERT(NULL == timeline->waiter_head);
		MALI_DEBUG_ASSERT(NULL == timeline->waiter_tail);
		MALI_DEBUG_ASSERT(NULL != timeline->system);
		MALI_DEBUG_ASSERT(MALI_TIMELINE_MAX > timeline->id);

		if (NULL != timeline->delayed_work) {
			_mali_osk_wq_delayed_cancel_work_sync(timeline->delayed_work);
			_mali_osk_wq_delayed_delete_work_nonflush(timeline->delayed_work);
		}

#if defined(CONFIG_SYNC)
		if (NULL != timeline->sync_tl) {
			sync_timeline_destroy(timeline->sync_tl);
		}
#endif /* defined(CONFIG_SYNC) */

#ifndef CONFIG_SYNC
		_mali_osk_free(timeline);
#endif
	}
}

static struct mali_timeline *mali_timeline_create(struct mali_timeline_system *system, enum mali_timeline_id id)
{
	struct mali_timeline *timeline;

	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT(id < MALI_TIMELINE_MAX);

	timeline = (struct mali_timeline *) _mali_osk_calloc(1, sizeof(struct mali_timeline));
	if (NULL == timeline) {
		return NULL;
	}

	/* Initially the timeline is empty. */
#if defined(MALI_TIMELINE_DEBUG_START_POINT)
	/* Start the timeline a bit before wrapping when debugging. */
	timeline->point_next = UINT_MAX - MALI_TIMELINE_MAX_POINT_SPAN - 128;
#else
	timeline->point_next = 1;
#endif
	timeline->point_oldest = timeline->point_next;

	/* The tracker and waiter lists will initially be empty. */

	timeline->system = system;
	timeline->id = id;

	timeline->delayed_work = _mali_osk_wq_delayed_create_work(mali_timeline_timer_callback, timeline);
	if (NULL == timeline->delayed_work) {
		mali_timeline_destroy(timeline);
		return NULL;
	}

	timeline->timer_active = MALI_FALSE;

#if defined(CONFIG_SYNC)
	{
		char timeline_name[32];

		switch (id) {
		case MALI_TIMELINE_GP:
			_mali_osk_snprintf(timeline_name, 32, "mali-%u-gp", _mali_osk_get_pid());
			break;
		case MALI_TIMELINE_PP:
			_mali_osk_snprintf(timeline_name, 32, "mali-%u-pp", _mali_osk_get_pid());
			break;
		case MALI_TIMELINE_SOFT:
			_mali_osk_snprintf(timeline_name, 32, "mali-%u-soft", _mali_osk_get_pid());
			break;
		default:
			MALI_PRINT_ERROR(("Mali Timeline: Invalid timeline id %d\n", id));
			mali_timeline_destroy(timeline);
			return NULL;
		}

		timeline->destroyed = MALI_FALSE;

		timeline->sync_tl = mali_sync_timeline_create(timeline, timeline_name);
		if (NULL == timeline->sync_tl) {
			mali_timeline_destroy(timeline);
			return NULL;
		}

		timeline->spinlock = mali_spinlock_reentrant_init(_MALI_OSK_LOCK_ORDER_TIMELINE_SYSTEM);
		if (NULL == timeline->spinlock) {
			mali_timeline_destroy(timeline);
			return NULL;
		}
	}
#endif /* defined(CONFIG_SYNC) */

	return timeline;
}

static void mali_timeline_insert_tracker(struct mali_timeline *timeline, struct mali_timeline_tracker *tracker)
{
	MALI_DEBUG_ASSERT_POINTER(timeline);
	MALI_DEBUG_ASSERT_POINTER(tracker);

	if (mali_timeline_is_full(timeline)) {
		/* Don't add tracker if timeline is full. */
		tracker->point = MALI_TIMELINE_NO_POINT;
		return;
	}

	tracker->timeline = timeline;
	tracker->point    = timeline->point_next;

	/* Find next available point. */
	timeline->point_next++;
	if (MALI_TIMELINE_NO_POINT == timeline->point_next) {
		timeline->point_next++;
	}

	MALI_DEBUG_ASSERT(!mali_timeline_is_empty(timeline));

	if (MALI_TIMELINE_TRACKER_GP == tracker->type) {
		_mali_osk_atomic_inc(&gp_tracker_count);
	} else if (MALI_TIMELINE_TRACKER_PP == tracker->type) {
		if (mali_pp_job_is_virtual((struct mali_pp_job *)tracker->job)) {
			_mali_osk_atomic_inc(&virt_pp_tracker_count);
		} else {
			_mali_osk_atomic_inc(&phy_pp_tracker_count);
		}
	}

	/* Add tracker as new head on timeline's tracker list. */
	if (NULL == timeline->tracker_head) {
		/* Tracker list is empty. */
		MALI_DEBUG_ASSERT(NULL == timeline->tracker_tail);

		timeline->tracker_tail = tracker;

		MALI_DEBUG_ASSERT(NULL == tracker->timeline_next);
		MALI_DEBUG_ASSERT(NULL == tracker->timeline_prev);
	} else {
		MALI_DEBUG_ASSERT(NULL == timeline->tracker_head->timeline_next);

		tracker->timeline_prev = timeline->tracker_head;
		timeline->tracker_head->timeline_next = tracker;

		MALI_DEBUG_ASSERT(NULL == tracker->timeline_next);
	}
	timeline->tracker_head = tracker;

	MALI_DEBUG_ASSERT(NULL == timeline->tracker_head->timeline_next);
	MALI_DEBUG_ASSERT(NULL == timeline->tracker_tail->timeline_prev);
}

/* Inserting the waiter object into the given timeline */
static void mali_timeline_insert_waiter(struct mali_timeline *timeline, struct mali_timeline_waiter *waiter_new)
{
	struct mali_timeline_waiter *waiter_prev;
	struct mali_timeline_waiter *waiter_next;

	/* Waiter time must be between timeline head and tail, and there must
	 * be less than MALI_TIMELINE_MAX_POINT_SPAN elements between */
	MALI_DEBUG_ASSERT((waiter_new->point - timeline->point_oldest) < MALI_TIMELINE_MAX_POINT_SPAN);
	MALI_DEBUG_ASSERT((-waiter_new->point + timeline->point_next) < MALI_TIMELINE_MAX_POINT_SPAN);

	/* Finding out where to put this waiter, in the linked waiter list of the given timeline **/
	waiter_prev = timeline->waiter_head; /* Insert new after  waiter_prev */
	waiter_next = NULL;                  /* Insert new before waiter_next */

	/* Iterating backwards from head (newest) to tail (oldest) until we
	 * find the correct spot to insert the new waiter */
	while (waiter_prev && mali_timeline_point_after(waiter_prev->point, waiter_new->point)) {
		waiter_next = waiter_prev;
		waiter_prev = waiter_prev->timeline_prev;
	}

	if (NULL == waiter_prev && NULL == waiter_next) {
		/* list is empty */
		timeline->waiter_head = waiter_new;
		timeline->waiter_tail = waiter_new;
	} else if (NULL == waiter_next) {
		/* insert at head */
		waiter_new->timeline_prev = timeline->waiter_head;
		timeline->waiter_head->timeline_next = waiter_new;
		timeline->waiter_head = waiter_new;
	} else if (NULL == waiter_prev) {
		/* insert at tail */
		waiter_new->timeline_next = timeline->waiter_tail;
		timeline->waiter_tail->timeline_prev = waiter_new;
		timeline->waiter_tail = waiter_new;
	} else {
		/* insert between */
		waiter_new->timeline_next = waiter_next;
		waiter_new->timeline_prev = waiter_prev;
		waiter_next->timeline_prev = waiter_new;
		waiter_prev->timeline_next = waiter_new;
	}
}

static void mali_timeline_update_delayed_work(struct mali_timeline *timeline)
{
	struct mali_timeline_system *system;
	struct mali_timeline_tracker *oldest_tracker;

	MALI_DEBUG_ASSERT_POINTER(timeline);
	MALI_DEBUG_ASSERT(MALI_TIMELINE_SOFT == timeline->id);

	system = timeline->system;
	MALI_DEBUG_ASSERT_POINTER(system);

	MALI_DEBUG_ASSERT(MALI_TIMELINE_SYSTEM_LOCKED(system));

	/* Timer is disabled, early out. */
	if (!system->timer_enabled) return;

	oldest_tracker = timeline->tracker_tail;
	if (NULL != oldest_tracker && 0 == oldest_tracker->trigger_ref_count) {
		if (MALI_FALSE == oldest_tracker->timer_active) {
			if (MALI_TRUE == timeline->timer_active) {
				_mali_osk_wq_delayed_cancel_work_async(timeline->delayed_work);
			}
			_mali_osk_wq_delayed_schedule_work(timeline->delayed_work, MALI_TIMELINE_TIMEOUT_HZ);
			oldest_tracker->timer_active = MALI_TRUE;
			timeline->timer_active = MALI_TRUE;
		}
	} else if (MALI_TRUE == timeline->timer_active) {
		_mali_osk_wq_delayed_cancel_work_async(timeline->delayed_work);
		timeline->timer_active = MALI_FALSE;
	}
}

static mali_scheduler_mask mali_timeline_update_oldest_point(struct mali_timeline *timeline)
{
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;

	MALI_DEBUG_ASSERT_POINTER(timeline);

	MALI_DEBUG_CODE({
		struct mali_timeline_system *system = timeline->system;
		MALI_DEBUG_ASSERT_POINTER(system);

		MALI_DEBUG_ASSERT(MALI_TIMELINE_SYSTEM_LOCKED(system));
	});

	if (NULL != timeline->tracker_tail) {
		/* Set oldest point to oldest tracker's point */
		timeline->point_oldest = timeline->tracker_tail->point;
	} else {
		/* No trackers, mark point list as empty */
		timeline->point_oldest = timeline->point_next;
	}

	/* Release all waiters no longer on the timeline's point list.
	 * Releasing a waiter can trigger this function to be called again, so
	 * we do not store any pointers on stack. */
	while (NULL != timeline->waiter_tail) {
		u32 waiter_time_relative;
		u32 time_head_relative;
		struct mali_timeline_waiter *waiter = timeline->waiter_tail;

		time_head_relative = timeline->point_next - timeline->point_oldest;
		waiter_time_relative = waiter->point - timeline->point_oldest;

		if (waiter_time_relative < time_head_relative) {
			/* This and all following waiters are on the point list, so we are done. */
			break;
		}

		/* Remove waiter from timeline's waiter list. */
		if (NULL != waiter->timeline_next) {
			waiter->timeline_next->timeline_prev = NULL;
		} else {
			/* This was the last waiter */
			timeline->waiter_head = NULL;
		}
		timeline->waiter_tail = waiter->timeline_next;

		/* Release waiter.  This could activate a tracker, if this was
		 * the last waiter for the tracker. */
		schedule_mask |= mali_timeline_system_release_waiter(timeline->system, waiter);
	}

	return schedule_mask;
}

void mali_timeline_tracker_init(struct mali_timeline_tracker *tracker,
				mali_timeline_tracker_type type,
				struct mali_timeline_fence *fence,
				void *job)
{
	MALI_DEBUG_ASSERT_POINTER(tracker);
	MALI_DEBUG_ASSERT_POINTER(job);

	MALI_DEBUG_ASSERT(MALI_TIMELINE_TRACKER_MAX > type);

	/* Zero out all tracker members. */
	_mali_osk_memset(tracker, 0, sizeof(*tracker));

	tracker->type = type;
	tracker->job = job;
	tracker->trigger_ref_count = 1;  /* Prevents any callback from trigging while adding it */
	tracker->os_tick_create = _mali_osk_time_tickcount();
	MALI_DEBUG_CODE(tracker->magic = MALI_TIMELINE_TRACKER_MAGIC);

	tracker->activation_error = MALI_TIMELINE_ACTIVATION_ERROR_NONE;

	/* Copy fence. */
	if (NULL != fence) {
		_mali_osk_memcpy(&tracker->fence, fence, sizeof(struct mali_timeline_fence));
	}
}

mali_scheduler_mask mali_timeline_tracker_release(struct mali_timeline_tracker *tracker)
{
	struct mali_timeline *timeline;
	struct mali_timeline_system *system;
	struct mali_timeline_tracker *tracker_next, *tracker_prev;
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;
	u32 tid = _mali_osk_get_tid();

	/* Upon entry a group lock will be held, but not a scheduler lock. */
	MALI_DEBUG_ASSERT_POINTER(tracker);
	MALI_DEBUG_ASSERT(MALI_TIMELINE_TRACKER_MAGIC == tracker->magic);

	/* Tracker should have been triggered */
	MALI_DEBUG_ASSERT(0 == tracker->trigger_ref_count);

	/* All waiters should have been released at this point */
	MALI_DEBUG_ASSERT(NULL == tracker->waiter_head);
	MALI_DEBUG_ASSERT(NULL == tracker->waiter_tail);

	MALI_DEBUG_PRINT(3, ("Mali Timeline: releasing tracker for job 0x%08X\n", tracker->job));

	timeline = tracker->timeline;
	if (NULL == timeline) {
		/* Tracker was not on a timeline, there is nothing to release. */
		return MALI_SCHEDULER_MASK_EMPTY;
	}

	system = timeline->system;
	MALI_DEBUG_ASSERT_POINTER(system);

	mali_spinlock_reentrant_wait(system->spinlock, tid);

	/* Tracker should still be on timeline */
	MALI_DEBUG_ASSERT(!mali_timeline_is_empty(timeline));
	MALI_DEBUG_ASSERT(mali_timeline_is_point_on(timeline, tracker->point));

	/* Tracker is no longer valid. */
	MALI_DEBUG_CODE(tracker->magic = 0);

	tracker_next = tracker->timeline_next;
	tracker_prev = tracker->timeline_prev;
	tracker->timeline_next = NULL;
	tracker->timeline_prev = NULL;

	/* Removing tracker from timeline's tracker list */
	if (NULL == tracker_next) {
		/* This tracker was the head */
		timeline->tracker_head = tracker_prev;
	} else {
		tracker_next->timeline_prev = tracker_prev;
	}

	if (NULL == tracker_prev) {
		/* This tracker was the tail */
		timeline->tracker_tail = tracker_next;
		MALI_DEBUG_ASSERT(MALI_TIMELINE_SYSTEM_LOCKED(system));
		/* Update the timeline's oldest time and release any waiters */
		schedule_mask |= mali_timeline_update_oldest_point(timeline);
		MALI_DEBUG_ASSERT(MALI_TIMELINE_SYSTEM_LOCKED(system));
	} else {
		tracker_prev->timeline_next = tracker_next;
	}

	MALI_DEBUG_ASSERT(MALI_TIMELINE_SYSTEM_LOCKED(system));

	/* Update delayed work only when it is the soft job timeline */
	if (MALI_TIMELINE_SOFT == tracker->timeline->id) {
		mali_timeline_update_delayed_work(tracker->timeline);
	}

	mali_spinlock_reentrant_signal(system->spinlock, tid);

	return schedule_mask;
}

void mali_timeline_system_release_waiter_list(struct mali_timeline_system *system,
		struct mali_timeline_waiter *tail,
		struct mali_timeline_waiter *head)
{
	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT_POINTER(head);
	MALI_DEBUG_ASSERT_POINTER(tail);
	MALI_DEBUG_ASSERT(MALI_TIMELINE_SYSTEM_LOCKED(system));

	head->tracker_next = system->waiter_empty_list;
	system->waiter_empty_list = tail;
}

static mali_scheduler_mask mali_timeline_tracker_activate(struct mali_timeline_tracker *tracker)
{
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;
	struct mali_timeline_system *system;
	struct mali_timeline *timeline;
	u32 tid = _mali_osk_get_tid();

	MALI_DEBUG_ASSERT_POINTER(tracker);
	MALI_DEBUG_ASSERT(MALI_TIMELINE_TRACKER_MAGIC == tracker->magic);

	system = tracker->system;
	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT(MALI_TIMELINE_SYSTEM_LOCKED(system));

	tracker->os_tick_activate = _mali_osk_time_tickcount();

	if (NULL != tracker->waiter_head) {
		mali_timeline_system_release_waiter_list(system, tracker->waiter_tail, tracker->waiter_head);
		tracker->waiter_head = NULL;
		tracker->waiter_tail = NULL;
	}

	switch (tracker->type) {
	case MALI_TIMELINE_TRACKER_GP:
		schedule_mask = mali_scheduler_activate_gp_job((struct mali_gp_job *) tracker->job);

		_mali_osk_atomic_dec(&gp_tracker_count);
		break;
	case MALI_TIMELINE_TRACKER_PP:
		if (mali_pp_job_is_virtual((struct mali_pp_job *)tracker->job)) {
			_mali_osk_atomic_dec(&virt_pp_tracker_count);
		} else {
			_mali_osk_atomic_dec(&phy_pp_tracker_count);
		}
		schedule_mask = mali_scheduler_activate_pp_job((struct mali_pp_job *) tracker->job);
		break;
	case MALI_TIMELINE_TRACKER_SOFT:
		timeline = tracker->timeline;
		MALI_DEBUG_ASSERT_POINTER(timeline);

		schedule_mask |= mali_soft_job_system_activate_job((struct mali_soft_job *) tracker->job);

		/* Start a soft timer to make sure the soft job be released in a limited time */
		mali_spinlock_reentrant_wait(system->spinlock, tid);
		mali_timeline_update_delayed_work(timeline);
		mali_spinlock_reentrant_signal(system->spinlock, tid);
		break;
	case MALI_TIMELINE_TRACKER_WAIT:
		mali_timeline_fence_wait_activate((struct mali_timeline_fence_wait_tracker *) tracker->job);
		break;
	case MALI_TIMELINE_TRACKER_SYNC:
#if defined(CONFIG_SYNC)
		mali_timeline_sync_fence_activate((struct mali_timeline_sync_fence_tracker *) tracker->job);
#else
		MALI_PRINT_ERROR(("Mali Timeline: sync tracker not supported\n", tracker->type));
#endif /* defined(CONFIG_SYNC) */
		break;
	default:
		MALI_PRINT_ERROR(("Mali Timeline - Illegal tracker type: %d\n", tracker->type));
		break;
	}

	return schedule_mask;
}

void mali_timeline_system_tracker_get(struct mali_timeline_system *system, struct mali_timeline_tracker *tracker)
{
	u32 tid = _mali_osk_get_tid();

	MALI_DEBUG_ASSERT_POINTER(tracker);
	MALI_DEBUG_ASSERT_POINTER(system);

	mali_spinlock_reentrant_wait(system->spinlock, tid);

	MALI_DEBUG_ASSERT(0 < tracker->trigger_ref_count);
	tracker->trigger_ref_count++;

	mali_spinlock_reentrant_signal(system->spinlock, tid);
}

mali_scheduler_mask mali_timeline_system_tracker_put(struct mali_timeline_system *system, struct mali_timeline_tracker *tracker, mali_timeline_activation_error activation_error)
{
	u32 tid = _mali_osk_get_tid();
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;

	MALI_DEBUG_ASSERT_POINTER(tracker);
	MALI_DEBUG_ASSERT_POINTER(system);

	mali_spinlock_reentrant_wait(system->spinlock, tid);

	MALI_DEBUG_ASSERT(0 < tracker->trigger_ref_count);
	tracker->trigger_ref_count--;

	tracker->activation_error |= activation_error;

	if (0 == tracker->trigger_ref_count) {
		schedule_mask |= mali_timeline_tracker_activate(tracker);
		tracker = NULL;
	}

	mali_spinlock_reentrant_signal(system->spinlock, tid);

	return schedule_mask;
}

void mali_timeline_fence_copy_uk_fence(struct mali_timeline_fence *fence, _mali_uk_fence_t *uk_fence)
{
	u32 i;

	MALI_DEBUG_ASSERT_POINTER(fence);
	MALI_DEBUG_ASSERT_POINTER(uk_fence);

	for (i = 0; i < MALI_TIMELINE_MAX; ++i) {
		fence->points[i] = uk_fence->points[i];
	}

	fence->sync_fd = uk_fence->sync_fd;
}

struct mali_timeline_system *mali_timeline_system_create(struct mali_session_data *session)
{
	u32 i;
	struct mali_timeline_system *system;

	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_PRINT(4, ("Mali Timeline: creating timeline system\n"));

	system = (struct mali_timeline_system *) _mali_osk_calloc(1, sizeof(struct mali_timeline_system));
	if (NULL == system) {
		return NULL;
	}

	system->spinlock = mali_spinlock_reentrant_init(_MALI_OSK_LOCK_ORDER_TIMELINE_SYSTEM);
	if (NULL == system->spinlock) {
		mali_timeline_system_destroy(system);
		return NULL;
	}

	for (i = 0; i < MALI_TIMELINE_MAX; ++i) {
		system->timelines[i] = mali_timeline_create(system, (enum mali_timeline_id)i);
		if (NULL == system->timelines[i]) {
			mali_timeline_system_destroy(system);
			return NULL;
		}
	}

#if defined(CONFIG_SYNC)
	system->signaled_sync_tl = mali_sync_timeline_create(NULL, "mali-always-signaled");
	if (NULL == system->signaled_sync_tl) {
		mali_timeline_system_destroy(system);
		return NULL;
	}
#endif /* defined(CONFIG_SYNC) */

	system->waiter_empty_list = NULL;
	system->session = session;
	system->timer_enabled = MALI_TRUE;

	system->wait_queue = _mali_osk_wait_queue_init();
	if (NULL == system->wait_queue) {
		mali_timeline_system_destroy(system);
		return NULL;
	}

	return system;
}

#if defined(CONFIG_SYNC)

/**
 * Check if there are any trackers left on timeline.
 *
 * Used as a wait queue conditional.
 *
 * @param data Timeline.
 * @return MALI_TRUE if there are no trackers on timeline, MALI_FALSE if not.
 */
static mali_bool mali_timeline_has_no_trackers(void *data)
{
	struct mali_timeline *timeline = (struct mali_timeline *) data;

	MALI_DEBUG_ASSERT_POINTER(timeline);

	return mali_timeline_is_empty(timeline);
}

/**
 * Cancel sync fence waiters waited upon by trackers on all timelines.
 *
 * Will return after all timelines have no trackers left.
 *
 * @param system Timeline system.
 */
static void mali_timeline_cancel_sync_fence_waiters(struct mali_timeline_system *system)
{
	u32 i;
	u32 tid = _mali_osk_get_tid();
	struct mali_timeline_tracker *tracker, *tracker_next;
	_MALI_OSK_LIST_HEAD_STATIC_INIT(tracker_list);

	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT_POINTER(system->session);
	MALI_DEBUG_ASSERT(system->session->is_aborting);

	mali_spinlock_reentrant_wait(system->spinlock, tid);

	/* Cancel sync fence waiters. */
	for (i = 0; i < MALI_TIMELINE_MAX; ++i) {
		struct mali_timeline *timeline = system->timelines[i];

		MALI_DEBUG_ASSERT_POINTER(timeline);

		tracker_next = timeline->tracker_tail;
		while (NULL != tracker_next) {
			tracker = tracker_next;
			tracker_next = tracker->timeline_next;

			if (NULL == tracker->sync_fence) continue;

			MALI_DEBUG_PRINT(3, ("Mali Timeline: Cancelling sync fence wait for tracker 0x%08X.\n", tracker));

			/* Cancel sync fence waiter. */
			if (0 == sync_fence_cancel_async(tracker->sync_fence, &tracker->sync_fence_waiter)) {
				/* Callback was not called, move tracker to local list. */
				_mali_osk_list_add(&tracker->sync_fence_cancel_list, &tracker_list);
			}
		}
	}

	mali_spinlock_reentrant_signal(system->spinlock, tid);

	/* Manually call sync fence callback in order to release waiter and trigger activation of tracker. */
	_MALI_OSK_LIST_FOREACHENTRY(tracker, tracker_next, &tracker_list, struct mali_timeline_tracker, sync_fence_cancel_list) {
		mali_timeline_sync_fence_callback(tracker->sync_fence, &tracker->sync_fence_waiter);
	}

	/* Sleep until all sync fence callbacks are done and all timelines are empty. */
	for (i = 0; i < MALI_TIMELINE_MAX; ++i) {
		struct mali_timeline *timeline = system->timelines[i];

		MALI_DEBUG_ASSERT_POINTER(timeline);

		_mali_osk_wait_queue_wait_event(system->wait_queue, mali_timeline_has_no_trackers, (void *) timeline);
	}
}

#endif /* defined(CONFIG_SYNC) */

void mali_timeline_system_abort(struct mali_timeline_system *system)
{
	MALI_DEBUG_CODE(u32 tid = _mali_osk_get_tid(););

	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT_POINTER(system->session);
	MALI_DEBUG_ASSERT(system->session->is_aborting);

	MALI_DEBUG_PRINT(3, ("Mali Timeline: Aborting timeline system for session 0x%08X.\n", system->session));

#if defined(CONFIG_SYNC)
	mali_timeline_cancel_sync_fence_waiters(system);
#endif /* defined(CONFIG_SYNC) */

	/* Should not be any waiters or trackers left at this point. */
	MALI_DEBUG_CODE({
		u32 i;
		mali_spinlock_reentrant_wait(system->spinlock, tid);
		for (i = 0; i < MALI_TIMELINE_MAX; ++i)
		{
			struct mali_timeline *timeline = system->timelines[i];
			MALI_DEBUG_ASSERT_POINTER(timeline);
			MALI_DEBUG_ASSERT(timeline->point_oldest == timeline->point_next);
			MALI_DEBUG_ASSERT(NULL == timeline->tracker_head);
			MALI_DEBUG_ASSERT(NULL == timeline->tracker_tail);
			MALI_DEBUG_ASSERT(NULL == timeline->waiter_head);
			MALI_DEBUG_ASSERT(NULL == timeline->waiter_tail);
		}
		mali_spinlock_reentrant_signal(system->spinlock, tid);
	});
}

void mali_timeline_system_destroy(struct mali_timeline_system *system)
{
	u32 i;
	struct mali_timeline_waiter *waiter, *next;
#if defined(CONFIG_SYNC)
	u32 tid = _mali_osk_get_tid();
#endif

	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT_POINTER(system->session);

	MALI_DEBUG_PRINT(4, ("Mali Timeline: destroying timeline system\n"));

	if (NULL != system) {

		/* There should be no waiters left on this queue. */
		if (NULL != system->wait_queue) {
			_mali_osk_wait_queue_term(system->wait_queue);
			system->wait_queue = NULL;
		}

		/* Free all waiters in empty list */
		waiter = system->waiter_empty_list;
		while (NULL != waiter) {
			next = waiter->tracker_next;
			_mali_osk_free(waiter);
			waiter = next;
		}

#if defined(CONFIG_SYNC)
		if (NULL != system->signaled_sync_tl) {
			sync_timeline_destroy(system->signaled_sync_tl);
		}

		for (i = 0; i < MALI_TIMELINE_MAX; ++i) {
			if ((NULL != system->timelines[i]) && (NULL != system->timelines[i]->spinlock)) {
				mali_spinlock_reentrant_wait(system->timelines[i]->spinlock, tid);
				system->timelines[i]->destroyed = MALI_TRUE;
				mali_spinlock_reentrant_signal(system->timelines[i]->spinlock, tid);
			}
		}
#endif /* defined(CONFIG_SYNC) */

		for (i = 0; i < MALI_TIMELINE_MAX; ++i) {
			if (NULL != system->timelines[i]) {
				mali_timeline_destroy(system->timelines[i]);
			}
		}

		if (NULL != system->spinlock) {
			mali_spinlock_reentrant_term(system->spinlock);
		}

		_mali_osk_free(system);
	}
}

/**
 * Find how many waiters are needed for a given fence.
 *
 * @param fence The fence to check.
 * @return Number of waiters needed for fence.
 */
static u32 mali_timeline_fence_num_waiters(struct mali_timeline_fence *fence)
{
	u32 i, num_waiters = 0;

	MALI_DEBUG_ASSERT_POINTER(fence);

	for (i = 0; i < MALI_TIMELINE_MAX; ++i) {
		if (MALI_TIMELINE_NO_POINT != fence->points[i]) {
			++num_waiters;
		}
	}

#if defined(CONFIG_SYNC)
	if (-1 != fence->sync_fd) ++num_waiters;
#endif /* defined(CONFIG_SYNC) */

	return num_waiters;
}

static struct mali_timeline_waiter *mali_timeline_system_get_zeroed_waiter(struct mali_timeline_system *system)
{
	struct mali_timeline_waiter *waiter;

	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT(MALI_TIMELINE_SYSTEM_LOCKED(system));

	waiter = system->waiter_empty_list;
	if (NULL != waiter) {
		/* Remove waiter from empty list and zero it */
		system->waiter_empty_list = waiter->tracker_next;
		_mali_osk_memset(waiter, 0, sizeof(*waiter));
	}

	/* Return NULL if list was empty. */
	return waiter;
}

static void mali_timeline_system_allocate_waiters(struct mali_timeline_system *system,
		struct mali_timeline_waiter **tail,
		struct mali_timeline_waiter **head,
		int max_num_waiters)
{
	u32 i, tid = _mali_osk_get_tid();
	mali_bool do_alloc;
	struct mali_timeline_waiter *waiter;

	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT_POINTER(tail);
	MALI_DEBUG_ASSERT_POINTER(head);

	MALI_DEBUG_ASSERT(MALI_TIMELINE_SYSTEM_LOCKED(system));

	*head = *tail = NULL;
	do_alloc = MALI_FALSE;
	i = 0;
	while (i < max_num_waiters) {
		if (MALI_FALSE == do_alloc) {
			waiter = mali_timeline_system_get_zeroed_waiter(system);
			if (NULL == waiter) {
				do_alloc = MALI_TRUE;
				mali_spinlock_reentrant_signal(system->spinlock, tid);
				continue;
			}
		} else {
			waiter = _mali_osk_calloc(1, sizeof(struct mali_timeline_waiter));
			if (NULL == waiter) break;
		}
		++i;
		if (NULL == *tail) {
			*tail = waiter;
			*head = waiter;
		} else {
			(*head)->tracker_next = waiter;
			*head = waiter;
		}
	}
	if (MALI_TRUE == do_alloc) {
		mali_spinlock_reentrant_wait(system->spinlock, tid);
	}
}

/**
 * Create waiters for the given tracker. The tracker is activated when all waiters are release.
 *
 * @note Tracker can potentially be activated before this function returns.
 *
 * @param system Timeline system.
 * @param tracker Tracker we will create waiters for.
 * @param waiter_tail List of pre-allocated waiters.
 * @param waiter_head List of pre-allocated waiters.
 */
static void mali_timeline_system_create_waiters_and_unlock(struct mali_timeline_system *system,
		struct mali_timeline_tracker *tracker,
		struct mali_timeline_waiter *waiter_tail,
		struct mali_timeline_waiter *waiter_head)
{
	int i;
	u32 tid = _mali_osk_get_tid();
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;
#if defined(CONFIG_SYNC)
	struct sync_fence *sync_fence = NULL;
#endif /* defined(CONFIG_SYNC) */

	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT_POINTER(tracker);

	MALI_DEBUG_ASSERT(MALI_TIMELINE_SYSTEM_LOCKED(system));

	MALI_DEBUG_ASSERT(NULL == tracker->waiter_head);
	MALI_DEBUG_ASSERT(NULL == tracker->waiter_tail);
	MALI_DEBUG_ASSERT(NULL != tracker->job);

	/* Creating waiter object for all the timelines the fence is put on. Inserting this waiter
	 * into the timelines sorted list of waiters */
	for (i = 0; i < MALI_TIMELINE_MAX; ++i) {
		mali_timeline_point point;
		struct mali_timeline *timeline;
		struct mali_timeline_waiter *waiter;

		/* Get point on current timeline from tracker's fence. */
		point = tracker->fence.points[i];

		if (likely(MALI_TIMELINE_NO_POINT == point)) {
			/* Fence contains no point on this timeline so we don't need a waiter. */
			continue;
		}

		timeline = system->timelines[i];
		MALI_DEBUG_ASSERT_POINTER(timeline);

		if (unlikely(!mali_timeline_is_point_valid(timeline, point))) {
			MALI_PRINT_ERROR(("Mali Timeline: point %d is not valid (oldest=%d, next=%d)\n",
					  point, timeline->point_oldest, timeline->point_next));
			continue;
		}

		if (likely(mali_timeline_is_point_released(timeline, point))) {
			/* Tracker representing the point has been released so we don't need a
			 * waiter. */
			continue;
		}

		/* The point is on timeline. */
		MALI_DEBUG_ASSERT(mali_timeline_is_point_on(timeline, point));

		/* Get a new zeroed waiter object. */
		if (likely(NULL != waiter_tail)) {
			waiter = waiter_tail;
			waiter_tail = waiter_tail->tracker_next;
		} else {
			MALI_PRINT_ERROR(("Mali Timeline: failed to allocate memory for waiter\n"));
			continue;
		}

		/* Yanking the trigger ref count of the tracker. */
		tracker->trigger_ref_count++;

		waiter->point   = point;
		waiter->tracker = tracker;

		/* Insert waiter on tracker's singly-linked waiter list. */
		if (NULL == tracker->waiter_head) {
			/* list is empty */
			MALI_DEBUG_ASSERT(NULL == tracker->waiter_tail);
			tracker->waiter_tail = waiter;
		} else {
			tracker->waiter_head->tracker_next = waiter;
		}
		tracker->waiter_head = waiter;

		/* Add waiter to timeline. */
		mali_timeline_insert_waiter(timeline, waiter);
	}
#if defined(CONFIG_SYNC)
	if (-1 != tracker->fence.sync_fd) {
		int ret;
		struct mali_timeline_waiter *waiter;

		sync_fence = sync_fence_fdget(tracker->fence.sync_fd);
		if (unlikely(NULL == sync_fence)) {
			MALI_PRINT_ERROR(("Mali Timeline: failed to get sync fence from fd %d\n", tracker->fence.sync_fd));
			goto exit;
		}

		/* Check if we have a zeroed waiter object available. */
		if (unlikely(NULL == waiter_tail)) {
			MALI_PRINT_ERROR(("Mali Timeline: failed to allocate memory for waiter\n"));
			goto exit;
		}

		/* Start asynchronous wait that will release waiter when the fence is signaled. */
		sync_fence_waiter_init(&tracker->sync_fence_waiter, mali_timeline_sync_fence_callback);
		ret = sync_fence_wait_async(sync_fence, &tracker->sync_fence_waiter);
		if (1 == ret) {
			/* Fence already signaled, no waiter needed. */
			tracker->fence.sync_fd = -1;
			goto exit;
		} else if (0 != ret) {
			MALI_PRINT_ERROR(("Mali Timeline: sync fence fd %d signaled with error %d\n", tracker->fence.sync_fd, ret));
			tracker->activation_error |= MALI_TIMELINE_ACTIVATION_ERROR_SYNC_BIT;
			goto exit;
		}

		/* Grab new zeroed waiter object. */
		waiter = waiter_tail;
		waiter_tail = waiter_tail->tracker_next;

		/* Increase the trigger ref count of the tracker. */
		tracker->trigger_ref_count++;

		waiter->point   = MALI_TIMELINE_NO_POINT;
		waiter->tracker = tracker;

		/* Insert waiter on tracker's singly-linked waiter list. */
		if (NULL == tracker->waiter_head) {
			/* list is empty */
			MALI_DEBUG_ASSERT(NULL == tracker->waiter_tail);
			tracker->waiter_tail = waiter;
		} else {
			tracker->waiter_head->tracker_next = waiter;
		}
		tracker->waiter_head = waiter;

		/* Also store waiter in separate field for easy access by sync callback. */
		tracker->waiter_sync = waiter;

		/* Store the sync fence in tracker so we can retrieve in abort session, if needed. */
		tracker->sync_fence = sync_fence;

		sync_fence = NULL;
	}
#endif /* defined(CONFIG_SYNC)*/
#if defined(CONFIG_MALI_DMA_BUF_FENCE)
	if ((NULL != tracker->timeline) && (MALI_TIMELINE_PP == tracker->timeline->id)) {

		struct mali_pp_job *job = (struct mali_pp_job *)tracker->job;

		if (0 < job->dma_fence_context.num_dma_fence_waiter) {
			struct mali_timeline_waiter *waiter;
			/* Check if we have a zeroed waiter object available. */
			if (unlikely(NULL == waiter_tail)) {
				MALI_PRINT_ERROR(("Mali Timeline: failed to allocate memory for waiter\n"));
				goto exit;
			}

			/* Grab new zeroed waiter object. */
			waiter = waiter_tail;
			waiter_tail = waiter_tail->tracker_next;

			/* Increase the trigger ref count of the tracker. */
			tracker->trigger_ref_count++;

			waiter->point   = MALI_TIMELINE_NO_POINT;
			waiter->tracker = tracker;

			/* Insert waiter on tracker's singly-linked waiter list. */
			if (NULL == tracker->waiter_head) {
				/* list is empty */
				MALI_DEBUG_ASSERT(NULL == tracker->waiter_tail);
				tracker->waiter_tail = waiter;
			} else {
				tracker->waiter_head->tracker_next = waiter;
			}
			tracker->waiter_head = waiter;

			/* Also store waiter in separate field for easy access by sync callback. */
			tracker->waiter_dma_fence = waiter;
		}
	}
#endif /* defined(CONFIG_MALI_DMA_BUF_FENCE)*/

#if defined(CONFIG_MALI_DMA_BUF_FENCE) ||defined(CONFIG_SYNC)
exit:
#endif /* defined(CONFIG_MALI_DMA_BUF_FENCE) || defined(CONFIG_SYNC) */

	if (NULL != waiter_tail) {
		mali_timeline_system_release_waiter_list(system, waiter_tail, waiter_head);
	}

	/* Release the initial trigger ref count. */
	tracker->trigger_ref_count--;

	/* If there were no waiters added to this tracker we activate immediately. */
	if (0 == tracker->trigger_ref_count) {
		schedule_mask |= mali_timeline_tracker_activate(tracker);
	}

	mali_spinlock_reentrant_signal(system->spinlock, tid);

#if defined(CONFIG_SYNC)
	if (NULL != sync_fence) {
		sync_fence_put(sync_fence);
	}
#endif /* defined(CONFIG_SYNC) */

	mali_executor_schedule_from_mask(schedule_mask, MALI_FALSE);
}

mali_timeline_point mali_timeline_system_add_tracker(struct mali_timeline_system *system,
		struct mali_timeline_tracker *tracker,
		enum mali_timeline_id timeline_id)
{
	int num_waiters = 0;
	struct mali_timeline_waiter *waiter_tail, *waiter_head;
	u32 tid = _mali_osk_get_tid();

	mali_timeline_point point = MALI_TIMELINE_NO_POINT;

	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT_POINTER(system->session);
	MALI_DEBUG_ASSERT_POINTER(tracker);

	MALI_DEBUG_ASSERT(MALI_FALSE == system->session->is_aborting);
	MALI_DEBUG_ASSERT(MALI_TIMELINE_TRACKER_MAX > tracker->type);
	MALI_DEBUG_ASSERT(MALI_TIMELINE_TRACKER_MAGIC == tracker->magic);

	MALI_DEBUG_PRINT(4, ("Mali Timeline: adding tracker for job %p, timeline: %d\n", tracker->job, timeline_id));

	MALI_DEBUG_ASSERT(0 < tracker->trigger_ref_count);
	tracker->system = system;

	mali_spinlock_reentrant_wait(system->spinlock, tid);

	num_waiters = mali_timeline_fence_num_waiters(&tracker->fence);

#if defined(CONFIG_MALI_DMA_BUF_FENCE)
	if (MALI_TIMELINE_PP == timeline_id) {
		struct mali_pp_job *job = (struct mali_pp_job *)tracker->job;
		if (0 < job->dma_fence_context.num_dma_fence_waiter)
			num_waiters++;
	}
#endif

	/* Allocate waiters. */
	mali_timeline_system_allocate_waiters(system, &waiter_tail, &waiter_head, num_waiters);
	MALI_DEBUG_ASSERT(MALI_TIMELINE_SYSTEM_LOCKED(system));

	/* Add tracker to timeline.  This will allocate a point for the tracker on the timeline. If
	 * timeline ID is MALI_TIMELINE_NONE the tracker will NOT be added to a timeline and the
	 * point will be MALI_TIMELINE_NO_POINT.
	 *
	 * NOTE: the tracker can fail to be added if the timeline is full.  If this happens, the
	 * point will be MALI_TIMELINE_NO_POINT. */
	MALI_DEBUG_ASSERT(timeline_id < MALI_TIMELINE_MAX || timeline_id == MALI_TIMELINE_NONE);
	if (likely(timeline_id < MALI_TIMELINE_MAX)) {
		struct mali_timeline *timeline = system->timelines[timeline_id];
		mali_timeline_insert_tracker(timeline, tracker);
		MALI_DEBUG_ASSERT(!mali_timeline_is_empty(timeline));
	}

	point = tracker->point;

	/* Create waiters for tracker based on supplied fence.  Each waiter will increase the
	 * trigger ref count. */
	mali_timeline_system_create_waiters_and_unlock(system, tracker, waiter_tail, waiter_head);
	tracker = NULL;

	/* At this point the tracker object might have been freed so we should no longer
	 * access it. */


	/* The tracker will always be activated after calling add_tracker, even if NO_POINT is
	 * returned. */
	return point;
}

static mali_scheduler_mask mali_timeline_system_release_waiter(struct mali_timeline_system *system,
		struct mali_timeline_waiter *waiter)
{
	struct mali_timeline_tracker *tracker;
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;

	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT_POINTER(waiter);

	MALI_DEBUG_ASSERT(MALI_TIMELINE_SYSTEM_LOCKED(system));

	tracker = waiter->tracker;
	MALI_DEBUG_ASSERT_POINTER(tracker);

	/* At this point the waiter has been removed from the timeline's waiter list, but it is
	 * still on the tracker's waiter list.  All of the tracker's waiters will be released when
	 * the tracker is activated. */

	waiter->point   = MALI_TIMELINE_NO_POINT;
	waiter->tracker = NULL;

	tracker->trigger_ref_count--;
	if (0 == tracker->trigger_ref_count) {
		/* This was the last waiter; activate tracker */
		schedule_mask |= mali_timeline_tracker_activate(tracker);
		tracker = NULL;
	}

	return schedule_mask;
}

mali_timeline_point mali_timeline_system_get_latest_point(struct mali_timeline_system *system,
		enum mali_timeline_id timeline_id)
{
	mali_timeline_point point;
	struct mali_timeline *timeline;
	u32 tid = _mali_osk_get_tid();

	MALI_DEBUG_ASSERT_POINTER(system);

	if (MALI_TIMELINE_MAX <= timeline_id) {
		return MALI_TIMELINE_NO_POINT;
	}

	mali_spinlock_reentrant_wait(system->spinlock, tid);

	timeline = system->timelines[timeline_id];
	MALI_DEBUG_ASSERT_POINTER(timeline);

	point = MALI_TIMELINE_NO_POINT;
	if (timeline->point_oldest != timeline->point_next) {
		point = timeline->point_next - 1;
		if (MALI_TIMELINE_NO_POINT == point) point--;
	}

	mali_spinlock_reentrant_signal(system->spinlock, tid);

	return point;
}

void mali_timeline_initialize(void)
{
	_mali_osk_atomic_init(&gp_tracker_count, 0);
	_mali_osk_atomic_init(&phy_pp_tracker_count, 0);
	_mali_osk_atomic_init(&virt_pp_tracker_count, 0);
}

void mali_timeline_terminate(void)
{
	_mali_osk_atomic_term(&gp_tracker_count);
	_mali_osk_atomic_term(&phy_pp_tracker_count);
	_mali_osk_atomic_term(&virt_pp_tracker_count);
}

#if defined(MALI_TIMELINE_DEBUG_FUNCTIONS)

static mali_bool is_waiting_on_timeline(struct mali_timeline_tracker *tracker, enum mali_timeline_id id)
{
	struct mali_timeline *timeline;
	struct mali_timeline_system *system;

	MALI_DEBUG_ASSERT_POINTER(tracker);

	MALI_DEBUG_ASSERT_POINTER(tracker->timeline);
	timeline = tracker->timeline;

	MALI_DEBUG_ASSERT_POINTER(timeline->system);
	system = timeline->system;

	if (MALI_TIMELINE_MAX > id) {
		if (MALI_TIMELINE_NO_POINT != tracker->fence.points[id]) {
			return mali_timeline_is_point_on(system->timelines[id], tracker->fence.points[id]);
		} else {
			return MALI_FALSE;
		}
	} else {
		MALI_DEBUG_ASSERT(MALI_TIMELINE_NONE == id);
		return MALI_FALSE;
	}
}

static const char *timeline_id_to_string(enum mali_timeline_id id)
{
	switch (id) {
	case MALI_TIMELINE_GP:
		return "GP";
	case MALI_TIMELINE_PP:
		return "PP";
	case MALI_TIMELINE_SOFT:
		return "SOFT";
	default:
		return "NONE";
	}
}

static const char *timeline_tracker_type_to_string(enum mali_timeline_tracker_type type)
{
	switch (type) {
	case MALI_TIMELINE_TRACKER_GP:
		return "GP";
	case MALI_TIMELINE_TRACKER_PP:
		return "PP";
	case MALI_TIMELINE_TRACKER_SOFT:
		return "SOFT";
	case MALI_TIMELINE_TRACKER_WAIT:
		return "WAIT";
	case MALI_TIMELINE_TRACKER_SYNC:
		return "SYNC";
	default:
		return "INVALID";
	}
}

mali_timeline_tracker_state mali_timeline_debug_get_tracker_state(struct mali_timeline_tracker *tracker)
{
	struct mali_timeline *timeline = NULL;

	MALI_DEBUG_ASSERT_POINTER(tracker);
	timeline = tracker->timeline;

	if (0 != tracker->trigger_ref_count) {
		return MALI_TIMELINE_TS_WAITING;
	}

	if (timeline && (timeline->tracker_tail == tracker || NULL != tracker->timeline_prev)) {
		return MALI_TIMELINE_TS_ACTIVE;
	}

	if (timeline && (MALI_TIMELINE_NO_POINT == tracker->point)) {
		return MALI_TIMELINE_TS_INIT;
	}

	return MALI_TIMELINE_TS_FINISH;
}

void mali_timeline_debug_print_tracker(struct mali_timeline_tracker *tracker, _mali_osk_print_ctx *print_ctx)
{
	const char *tracker_state = "IWAF";
	char state_char = 'I';
	char tracker_type[32] = {0};

	MALI_DEBUG_ASSERT_POINTER(tracker);

	state_char = *(tracker_state + mali_timeline_debug_get_tracker_state(tracker));
	_mali_osk_snprintf(tracker_type, sizeof(tracker_type), "%s", timeline_tracker_type_to_string(tracker->type));

#if defined(CONFIG_SYNC)
	if (0 != tracker->trigger_ref_count) {
		_mali_osk_ctxprintf(print_ctx, "TL:  %s %u %c - ref_wait:%u [%s(%u),%s(%u),%s(%u), fd:%d, fence:(0x%08X)]  job:(0x%08X)\n",
				    tracker_type, tracker->point, state_char, tracker->trigger_ref_count,
				    is_waiting_on_timeline(tracker, MALI_TIMELINE_GP) ? "WaitGP" : " ", tracker->fence.points[0],
				    is_waiting_on_timeline(tracker, MALI_TIMELINE_PP) ? "WaitPP" : " ", tracker->fence.points[1],
				    is_waiting_on_timeline(tracker, MALI_TIMELINE_SOFT) ? "WaitSOFT" : " ", tracker->fence.points[2],
				    tracker->fence.sync_fd, (unsigned int)(uintptr_t)(tracker->sync_fence), (unsigned int)(uintptr_t)(tracker->job));
	} else {
		_mali_osk_ctxprintf(print_ctx, "TL:  %s %u %c  fd:%d  fence:(0x%08X)  job:(0x%08X)\n",
				    tracker_type, tracker->point, state_char,
				    tracker->fence.sync_fd, (unsigned int)(uintptr_t)(tracker->sync_fence), (unsigned int)(uintptr_t)(tracker->job));
	}
#else
	if (0 != tracker->trigger_ref_count) {
		_mali_osk_ctxprintf(print_ctx, "TL:  %s %u %c - ref_wait:%u [%s(%u),%s(%u),%s(%u)]  job:(0x%08X)\n",
				    tracker_type, tracker->point, state_char, tracker->trigger_ref_count,
				    is_waiting_on_timeline(tracker, MALI_TIMELINE_GP) ? "WaitGP" : " ", tracker->fence.points[0],
				    is_waiting_on_timeline(tracker, MALI_TIMELINE_PP) ? "WaitPP" : " ", tracker->fence.points[1],
				    is_waiting_on_timeline(tracker, MALI_TIMELINE_SOFT) ? "WaitSOFT" : " ", tracker->fence.points[2],
				    (unsigned int)(uintptr_t)(tracker->job));
	} else {
		_mali_osk_ctxprintf(print_ctx, "TL:  %s %u %c  job:(0x%08X)\n",
				    tracker_type, tracker->point, state_char,
				    (unsigned int)(uintptr_t)(tracker->job));
	}
#endif
}

void mali_timeline_debug_print_timeline(struct mali_timeline *timeline, _mali_osk_print_ctx *print_ctx)
{
	struct mali_timeline_tracker *tracker = NULL;

	MALI_DEBUG_ASSERT_POINTER(timeline);

	tracker = timeline->tracker_tail;
	while (NULL != tracker) {
		mali_timeline_debug_print_tracker(tracker, print_ctx);
		tracker = tracker->timeline_next;
	}
}

#if !(LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0))
void mali_timeline_debug_direct_print_tracker(struct mali_timeline_tracker *tracker)
{
	const char *tracker_state = "IWAF";
	char state_char = 'I';
	char tracker_type[32] = {0};

	MALI_DEBUG_ASSERT_POINTER(tracker);

	state_char = *(tracker_state + mali_timeline_debug_get_tracker_state(tracker));
	_mali_osk_snprintf(tracker_type, sizeof(tracker_type), "%s", timeline_tracker_type_to_string(tracker->type));

#if defined(CONFIG_SYNC)
	if (0 != tracker->trigger_ref_count) {
		MALI_PRINT(("TL:  %s %u %c - ref_wait:%u [%s(%u),%s(%u),%s(%u), fd:%d, fence:(0x%08X)]  job:(0x%08X)\n",
			    tracker_type, tracker->point, state_char, tracker->trigger_ref_count,
			    is_waiting_on_timeline(tracker, MALI_TIMELINE_GP) ? "WaitGP" : " ", tracker->fence.points[0],
			    is_waiting_on_timeline(tracker, MALI_TIMELINE_PP) ? "WaitPP" : " ", tracker->fence.points[1],
			    is_waiting_on_timeline(tracker, MALI_TIMELINE_SOFT) ? "WaitSOFT" : " ", tracker->fence.points[2],
			    tracker->fence.sync_fd, tracker->sync_fence, tracker->job));
	} else {
		MALI_PRINT(("TL:  %s %u %c  fd:%d  fence:(0x%08X)  job:(0x%08X)\n",
			    tracker_type, tracker->point, state_char,
			    tracker->fence.sync_fd, tracker->sync_fence, tracker->job));
	}
#else
	if (0 != tracker->trigger_ref_count) {
		MALI_PRINT(("TL:  %s %u %c - ref_wait:%u [%s(%u),%s(%u),%s(%u)]  job:(0x%08X)\n",
			    tracker_type, tracker->point, state_char, tracker->trigger_ref_count,
			    is_waiting_on_timeline(tracker, MALI_TIMELINE_GP) ? "WaitGP" : " ", tracker->fence.points[0],
			    is_waiting_on_timeline(tracker, MALI_TIMELINE_PP) ? "WaitPP" : " ", tracker->fence.points[1],
			    is_waiting_on_timeline(tracker, MALI_TIMELINE_SOFT) ? "WaitSOFT" : " ", tracker->fence.points[2],
			    tracker->job));
	} else {
		MALI_PRINT(("TL:  %s %u %c  job:(0x%08X)\n",
			    tracker_type, tracker->point, state_char,
			    tracker->job));
	}
#endif
}

void mali_timeline_debug_direct_print_timeline(struct mali_timeline *timeline)
{
	struct mali_timeline_tracker *tracker = NULL;

	MALI_DEBUG_ASSERT_POINTER(timeline);

	tracker = timeline->tracker_tail;
	while (NULL != tracker) {
		mali_timeline_debug_direct_print_tracker(tracker);
		tracker = tracker->timeline_next;
	}
}

#endif

void mali_timeline_debug_print_system(struct mali_timeline_system *system, _mali_osk_print_ctx *print_ctx)
{
	int i;
	int num_printed = 0;
	u32 tid = _mali_osk_get_tid();

	MALI_DEBUG_ASSERT_POINTER(system);

	mali_spinlock_reentrant_wait(system->spinlock, tid);

	/* Print all timelines */
	for (i = 0; i < MALI_TIMELINE_MAX; ++i) {
		struct mali_timeline *timeline = system->timelines[i];

		MALI_DEBUG_ASSERT_POINTER(timeline);

		if (NULL == timeline->tracker_head) continue;

		_mali_osk_ctxprintf(print_ctx, "TL: Timeline %s:\n",
				    timeline_id_to_string((enum mali_timeline_id)i));

		mali_timeline_debug_print_timeline(timeline, print_ctx);
		num_printed++;
	}

	if (0 == num_printed) {
		_mali_osk_ctxprintf(print_ctx, "TL: All timelines empty\n");
	}

	mali_spinlock_reentrant_signal(system->spinlock, tid);
}

#endif /* defined(MALI_TIMELINE_DEBUG_FUNCTIONS) */

#if defined(CONFIG_MALI_DMA_BUF_FENCE)
void mali_timeline_dma_fence_callback(void *pp_job_ptr)
{
	struct mali_timeline_system  *system;
	struct mali_timeline_waiter  *waiter;
	struct mali_timeline_tracker *tracker;
	struct mali_pp_job *pp_job = (struct mali_pp_job *)pp_job_ptr;
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;
	u32 tid = _mali_osk_get_tid();
	mali_bool is_aborting = MALI_FALSE;

	MALI_DEBUG_ASSERT_POINTER(pp_job);


	tracker = &pp_job->tracker;
	MALI_DEBUG_ASSERT_POINTER(tracker);

	system = tracker->system;
	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT_POINTER(system->session);

	mali_spinlock_reentrant_wait(system->spinlock, tid);

	waiter = tracker->waiter_dma_fence;
	MALI_DEBUG_ASSERT_POINTER(waiter);

	schedule_mask |= mali_timeline_system_release_waiter(system, waiter);

	is_aborting = system->session->is_aborting;

	mali_spinlock_reentrant_signal(system->spinlock, tid);

	if (!is_aborting) {
		mali_executor_schedule_from_mask(schedule_mask, MALI_TRUE);
	}
}
#endif
