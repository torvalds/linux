/*
 * Copyright (C) 2013-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_TIMELINE_H__
#define __MALI_TIMELINE_H__

#include "mali_osk.h"
#include "mali_ukk.h"
#include "mali_session.h"
#include "mali_kernel_common.h"
#include "mali_spinlock_reentrant.h"
#include "mali_sync.h"
#include "mali_scheduler_types.h"
#include <linux/version.h>

/**
 * Soft job timeout.
 *
 * Soft jobs have to be signaled as complete after activation.  Normally this is done by user space,
 * but in order to guarantee that every soft job is completed, we also have a timer.
 */
#define MALI_TIMELINE_TIMEOUT_HZ ((unsigned long) (HZ * 3 / 2)) /* 1500 ms. */

/**
 * Timeline type.
 */
typedef enum mali_timeline_id {
	MALI_TIMELINE_GP   = MALI_UK_TIMELINE_GP,   /**< GP job timeline. */
	MALI_TIMELINE_PP   = MALI_UK_TIMELINE_PP,   /**< PP job timeline. */
	MALI_TIMELINE_SOFT = MALI_UK_TIMELINE_SOFT, /**< Soft job timeline. */
	MALI_TIMELINE_MAX  = MALI_UK_TIMELINE_MAX
} mali_timeline_id;

/**
 * Used by trackers that should not be added to a timeline (@ref mali_timeline_system_add_tracker).
 */
#define MALI_TIMELINE_NONE MALI_TIMELINE_MAX

/**
 * Tracker type.
 */
typedef enum mali_timeline_tracker_type {
	MALI_TIMELINE_TRACKER_GP   = 0, /**< Tracker used by GP jobs. */
	MALI_TIMELINE_TRACKER_PP   = 1, /**< Tracker used by PP jobs. */
	MALI_TIMELINE_TRACKER_SOFT = 2, /**< Tracker used by soft jobs. */
	MALI_TIMELINE_TRACKER_WAIT = 3, /**< Tracker used for fence wait. */
	MALI_TIMELINE_TRACKER_SYNC = 4, /**< Tracker used for sync fence. */
	MALI_TIMELINE_TRACKER_MAX  = 5,
} mali_timeline_tracker_type;

/**
 * Tracker activation error.
 */
typedef u32 mali_timeline_activation_error;
#define MALI_TIMELINE_ACTIVATION_ERROR_NONE      0
#define MALI_TIMELINE_ACTIVATION_ERROR_SYNC_BIT  (1<<1)
#define MALI_TIMELINE_ACTIVATION_ERROR_FATAL_BIT (1<<0)

/**
 * Type used to represent a point on a timeline.
 */
typedef u32 mali_timeline_point;

/**
 * Used to represent that no point on a timeline.
 */
#define MALI_TIMELINE_NO_POINT ((mali_timeline_point) 0)

/**
 * The maximum span of points on a timeline.  A timeline will be considered full if the difference
 * between the oldest and newest points is equal or larger to this value.
 */
#define MALI_TIMELINE_MAX_POINT_SPAN 65536

/**
 * Magic value used to assert on validity of trackers.
 */
#define MALI_TIMELINE_TRACKER_MAGIC 0xabcdabcd

struct mali_timeline;
struct mali_timeline_waiter;
struct mali_timeline_tracker;

/**
 * Timeline fence.
 */
struct mali_timeline_fence {
	mali_timeline_point points[MALI_TIMELINE_MAX]; /**< For each timeline, a point or MALI_TIMELINE_NO_POINT. */
	s32                 sync_fd;                   /**< A file descriptor representing a sync fence, or -1. */
};

/**
 * Timeline system.
 *
 * The Timeline system has a set of timelines associated with a session.
 */
struct mali_timeline_system {
	struct mali_spinlock_reentrant *spinlock;   /**< Spin lock protecting the timeline system */
	struct mali_timeline           *timelines[MALI_TIMELINE_MAX]; /**< The timelines in this system */

	/* Single-linked list of unused waiter objects.  Uses the tracker_next field in tracker. */
	struct mali_timeline_waiter    *waiter_empty_list;

	struct mali_session_data       *session;    /**< Session that owns this system. */

	mali_bool                       timer_enabled; /**< Set to MALI_TRUE if soft job timer should be enabled, MALI_FALSE if not. */

	_mali_osk_wait_queue_t         *wait_queue; /**< Wait queue. */

#if defined(CONFIG_SYNC)
	struct sync_timeline           *signaled_sync_tl; /**< Special sync timeline used to create pre-signaled sync fences */
#endif /* defined(CONFIG_SYNC) */
};

/**
 * Timeline.  Each Timeline system will have MALI_TIMELINE_MAX timelines.
 */
struct mali_timeline {
	mali_timeline_point           point_next;   /**< The next available point. */
	mali_timeline_point           point_oldest; /**< The oldest point not released. */

	/* Double-linked list of trackers.  Sorted in ascending order by tracker->time_number with
	 * tail pointing to the tracker with the oldest time. */
	struct mali_timeline_tracker *tracker_head;
	struct mali_timeline_tracker *tracker_tail;

	/* Double-linked list of waiters.  Sorted in ascending order by waiter->time_number_wait
	 * with tail pointing to the waiter with oldest wait time. */
	struct mali_timeline_waiter  *waiter_head;
	struct mali_timeline_waiter  *waiter_tail;

	struct mali_timeline_system  *system;       /**< Timeline system this timeline belongs to. */
	enum mali_timeline_id         id;           /**< Timeline type. */

#if defined(CONFIG_SYNC)
	struct sync_timeline         *sync_tl;      /**< Sync timeline that corresponds to this timeline. */
	mali_bool destroyed;
	struct mali_spinlock_reentrant *spinlock;       /**< Spin lock protecting the timeline system */
#endif /* defined(CONFIG_SYNC) */

	/* The following fields are used to time out soft job trackers. */
	_mali_osk_wq_delayed_work_t  *delayed_work;
	mali_bool                     timer_active;
};

/**
 * Timeline waiter.
 */
struct mali_timeline_waiter {
	mali_timeline_point           point;         /**< Point on timeline we are waiting for to be released. */
	struct mali_timeline_tracker *tracker;       /**< Tracker that is waiting. */

	struct mali_timeline_waiter  *timeline_next; /**< Next waiter on timeline's waiter list. */
	struct mali_timeline_waiter  *timeline_prev; /**< Previous waiter on timeline's waiter list. */

	struct mali_timeline_waiter  *tracker_next;  /**< Next waiter on tracker's waiter list. */
};

/**
 * Timeline tracker.
 */
struct mali_timeline_tracker {
	MALI_DEBUG_CODE(u32            magic); /**< Should always be MALI_TIMELINE_TRACKER_MAGIC for a valid tracker. */

	mali_timeline_point            point; /**< Point on timeline for this tracker */

	struct mali_timeline_tracker  *timeline_next; /**< Next tracker on timeline's tracker list */
	struct mali_timeline_tracker  *timeline_prev; /**< Previous tracker on timeline's tracker list */

	u32                            trigger_ref_count; /**< When zero tracker will be activated */
	mali_timeline_activation_error activation_error;  /**< Activation error. */
	struct mali_timeline_fence     fence;             /**< Fence used to create this tracker */

	/* Single-linked list of waiters.  Sorted in order of insertions with
	 * tail pointing to first waiter. */
	struct mali_timeline_waiter   *waiter_head;
	struct mali_timeline_waiter   *waiter_tail;

#if defined(CONFIG_SYNC)
	/* These are only used if the tracker is waiting on a sync fence. */
	struct mali_timeline_waiter   *waiter_sync; /**< A direct pointer to timeline waiter representing sync fence. */
	struct sync_fence_waiter       sync_fence_waiter; /**< Used to connect sync fence and tracker in sync fence wait callback. */
	struct sync_fence             *sync_fence;   /**< The sync fence this tracker is waiting on. */
	_mali_osk_list_t               sync_fence_cancel_list; /**< List node used to cancel sync fence waiters. */
	_mali_osk_list_t	        sync_fence_signal_list; /** < List node used to singal sync fence callback function. */
#endif /* defined(CONFIG_SYNC) */

#if defined(CONFIG_MALI_DMA_BUF_FENCE)
	struct mali_timeline_waiter   *waiter_dma_fence; /**< A direct pointer to timeline waiter representing dma fence. */
#endif

	struct mali_timeline_system   *system;       /**< Timeline system. */
	struct mali_timeline          *timeline;     /**< Timeline, or NULL if not on a timeline. */
	enum mali_timeline_tracker_type type;        /**< Type of tracker. */
	void                          *job;          /**< Owner of tracker. */

	/* The following fields are used to time out soft job trackers. */
	unsigned long                 os_tick_create;
	unsigned long                 os_tick_activate;
	mali_bool                     timer_active;
};

extern _mali_osk_atomic_t gp_tracker_count;
extern _mali_osk_atomic_t phy_pp_tracker_count;
extern _mali_osk_atomic_t virt_pp_tracker_count;

/**
 * What follows is a set of functions to check the state of a timeline and to determine where on a
 * timeline a given point is.  Most of these checks will translate the timeline so the oldest point
 * on the timeline is aligned with zero.  Remember that all of these calculation are done on
 * unsigned integers.
 *
 * The following example illustrates the three different states a point can be in.  The timeline has
 * been translated to put the oldest point at zero:
 *
 *
 *
 *                               [ point is in forbidden zone ]
 *                                          64k wide
 *                                MALI_TIMELINE_MAX_POINT_SPAN
 *
 *    [ point is on timeline     )                            ( point is released ]
 *
 *    0--------------------------##############################--------------------2^32 - 1
 *    ^                          ^
 *    \                          |
 *     oldest point on timeline  |
 *                               \
 *                                next point on timeline
 */

/**
 * Compare two timeline points
 *
 * Returns true if a is after b, false if a is before or equal to b.
 *
 * This funcion ignores MALI_TIMELINE_MAX_POINT_SPAN. Wrapping is supported and
 * the result will be correct if the points is less then UINT_MAX/2 apart.
 *
 * @param a Point on timeline
 * @param b Point on timeline
 * @return MALI_TRUE if a is after b
 */
MALI_STATIC_INLINE mali_bool mali_timeline_point_after(mali_timeline_point a, mali_timeline_point b)
{
	return 0 > ((s32)b) - ((s32)a);
}

/**
 * Check if a point is on timeline.  A point is on a timeline if it is greater than, or equal to,
 * the oldest point, and less than the next point.
 *
 * @param timeline Timeline.
 * @param point Point on timeline.
 * @return MALI_TRUE if point is on timeline, MALI_FALSE if not.
 */
MALI_STATIC_INLINE mali_bool mali_timeline_is_point_on(struct mali_timeline *timeline, mali_timeline_point point)
{
	MALI_DEBUG_ASSERT_POINTER(timeline);
	MALI_DEBUG_ASSERT(MALI_TIMELINE_NO_POINT != point);

	return (point - timeline->point_oldest) < (timeline->point_next - timeline->point_oldest);
}

/**
 * Check if a point has been released.  A point is released if it is older than the oldest point on
 * the timeline, newer than the next point, and also not in the forbidden zone.
 *
 * @param timeline Timeline.
 * @param point Point on timeline.
 * @return MALI_TRUE if point has been release, MALI_FALSE if not.
 */
MALI_STATIC_INLINE mali_bool mali_timeline_is_point_released(struct mali_timeline *timeline, mali_timeline_point point)
{
	mali_timeline_point point_normalized;
	mali_timeline_point next_normalized;

	MALI_DEBUG_ASSERT_POINTER(timeline);
	MALI_DEBUG_ASSERT(MALI_TIMELINE_NO_POINT != point);

	point_normalized = point - timeline->point_oldest;
	next_normalized = timeline->point_next - timeline->point_oldest;

	return point_normalized > (next_normalized + MALI_TIMELINE_MAX_POINT_SPAN);
}

/**
 * Check if a point is valid.  A point is valid if is on the timeline or has been released.
 *
 * @param timeline Timeline.
 * @param point Point on timeline.
 * @return MALI_TRUE if point is valid, MALI_FALSE if not.
 */
MALI_STATIC_INLINE mali_bool mali_timeline_is_point_valid(struct mali_timeline *timeline, mali_timeline_point point)
{
	MALI_DEBUG_ASSERT_POINTER(timeline);
	return mali_timeline_is_point_on(timeline, point) || mali_timeline_is_point_released(timeline, point);
}

/**
 * Check if timeline is empty (has no points on it).  A timeline is empty if next == oldest.
 *
 * @param timeline Timeline.
 * @return MALI_TRUE if timeline is empty, MALI_FALSE if not.
 */
MALI_STATIC_INLINE mali_bool mali_timeline_is_empty(struct mali_timeline *timeline)
{
	MALI_DEBUG_ASSERT_POINTER(timeline);
	return timeline->point_next == timeline->point_oldest;
}

/**
 * Check if timeline is full.  A valid timeline cannot span more than 64k points (@ref
 * MALI_TIMELINE_MAX_POINT_SPAN).
 *
 * @param timeline Timeline.
 * @return MALI_TRUE if timeline is full, MALI_FALSE if not.
 */
MALI_STATIC_INLINE mali_bool mali_timeline_is_full(struct mali_timeline *timeline)
{
	MALI_DEBUG_ASSERT_POINTER(timeline);
	return MALI_TIMELINE_MAX_POINT_SPAN <= (timeline->point_next - timeline->point_oldest);
}

/**
 * Create a new timeline system.
 *
 * @param session The session this timeline system will belong to.
 * @return New timeline system.
 */
struct mali_timeline_system *mali_timeline_system_create(struct mali_session_data *session);

/**
 * Abort timeline system.
 *
 * This will release all pending waiters in the timeline system causing all trackers to be
 * activated.
 *
 * @param system Timeline system to abort all jobs from.
 */
void mali_timeline_system_abort(struct mali_timeline_system *system);

/**
 * Destroy an empty timeline system.
 *
 * @note @ref mali_timeline_system_abort() should be called prior to this function.
 *
 * @param system Timeline system to destroy.
 */
void mali_timeline_system_destroy(struct mali_timeline_system *system);

/**
 * Stop the soft job timer.
 *
 * @param system Timeline system
 */
void mali_timeline_system_stop_timer(struct mali_timeline_system *system);

/**
 * Add a tracker to a timeline system and optionally also on a timeline.
 *
 * Once added to the timeline system, the tracker is guaranteed to be activated.  The tracker can be
 * activated before this function returns.  Thus, it is also possible that the tracker is released
 * before this function returns, depending on the tracker type.
 *
 * @note Tracker must be initialized (@ref mali_timeline_tracker_init) before being added to the
 * timeline system.
 *
 * @param system Timeline system the tracker will be added to.
 * @param tracker The tracker to be added.
 * @param timeline_id Id of the timeline the tracker will be added to, or
 *                    MALI_TIMELINE_NONE if it should not be added on a timeline.
 * @return Point on timeline identifying this tracker, or MALI_TIMELINE_NO_POINT if not on timeline.
 */
mali_timeline_point mali_timeline_system_add_tracker(struct mali_timeline_system *system,
		struct mali_timeline_tracker *tracker,
		enum mali_timeline_id timeline_id);

/**
 * Get latest point on timeline.
 *
 * @param system Timeline system.
 * @param timeline_id Id of timeline to get latest point from.
 * @return Latest point on timeline, or MALI_TIMELINE_NO_POINT if the timeline is empty.
 */
mali_timeline_point mali_timeline_system_get_latest_point(struct mali_timeline_system *system,
		enum mali_timeline_id timeline_id);

/**
 * Initialize tracker.
 *
 * Must be called before tracker is added to timeline system (@ref mali_timeline_system_add_tracker).
 *
 * @param tracker Tracker to initialize.
 * @param type Type of tracker.
 * @param fence Fence used to set up dependencies for tracker.
 * @param job Pointer to job struct this tracker is associated with.
 */
void mali_timeline_tracker_init(struct mali_timeline_tracker *tracker,
				mali_timeline_tracker_type type,
				struct mali_timeline_fence *fence,
				void *job);

/**
 * Grab trigger ref count on tracker.
 *
 * This will prevent tracker from being activated until the trigger ref count reaches zero.
 *
 * @note Tracker must have been initialized (@ref mali_timeline_tracker_init).
 *
 * @param system Timeline system.
 * @param tracker Tracker.
 */
void mali_timeline_system_tracker_get(struct mali_timeline_system *system, struct mali_timeline_tracker *tracker);

/**
 * Release trigger ref count on tracker.
 *
 * If the trigger ref count reaches zero, the tracker will be activated.
 *
 * @param system Timeline system.
 * @param tracker Tracker.
 * @param activation_error Error bitmask if activated with error, or MALI_TIMELINE_ACTIVATION_ERROR_NONE if no error.
 * @return Scheduling bitmask.
 */
mali_scheduler_mask mali_timeline_system_tracker_put(struct mali_timeline_system *system, struct mali_timeline_tracker *tracker, mali_timeline_activation_error activation_error);

/**
 * Release a tracker from the timeline system.
 *
 * This is used to signal that the job being tracker is finished, either due to normal circumstances
 * (job complete/abort) or due to a timeout.
 *
 * We may need to schedule some subsystems after a tracker has been released and the returned
 * bitmask will tell us if it is necessary.  If the return value is non-zero, this value needs to be
 * sent as an input parameter to @ref mali_scheduler_schedule_from_mask() to do the scheduling.
 *
 * @note Tracker must have been activated before being released.
 * @warning Not calling @ref mali_scheduler_schedule_from_mask() after releasing a tracker can lead
 * to a deadlock.
 *
 * @param tracker Tracker being released.
 * @return Scheduling bitmask.
 */
mali_scheduler_mask mali_timeline_tracker_release(struct mali_timeline_tracker *tracker);

MALI_STATIC_INLINE mali_bool mali_timeline_tracker_activation_error(
	struct mali_timeline_tracker *tracker)
{
	MALI_DEBUG_ASSERT_POINTER(tracker);
	return (MALI_TIMELINE_ACTIVATION_ERROR_FATAL_BIT &
		tracker->activation_error) ? MALI_TRUE : MALI_FALSE;
}

/**
 * Copy data from a UK fence to a Timeline fence.
 *
 * @param fence Timeline fence.
 * @param uk_fence UK fence.
 */
void mali_timeline_fence_copy_uk_fence(struct mali_timeline_fence *fence, _mali_uk_fence_t *uk_fence);

_mali_osk_errcode_t mali_timeline_initialize(void);

void mali_timeline_terminate(void);

MALI_STATIC_INLINE mali_bool mali_timeline_has_gp_job(void)
{
	return 0 < _mali_osk_atomic_read(&gp_tracker_count);
}

MALI_STATIC_INLINE mali_bool mali_timeline_has_physical_pp_job(void)
{
	return 0 < _mali_osk_atomic_read(&phy_pp_tracker_count);
}

MALI_STATIC_INLINE mali_bool mali_timeline_has_virtual_pp_job(void)
{
	return 0 < _mali_osk_atomic_read(&virt_pp_tracker_count);
}

#if defined(DEBUG)
#define MALI_TIMELINE_DEBUG_FUNCTIONS
#endif /* DEBUG */
#if defined(MALI_TIMELINE_DEBUG_FUNCTIONS)

/**
 * Tracker state.  Used for debug printing.
 */
typedef enum mali_timeline_tracker_state {
	MALI_TIMELINE_TS_INIT    = 0,
	MALI_TIMELINE_TS_WAITING = 1,
	MALI_TIMELINE_TS_ACTIVE  = 2,
	MALI_TIMELINE_TS_FINISH  = 3,
} mali_timeline_tracker_state;

/**
 * Get tracker state.
 *
 * @param tracker Tracker to check.
 * @return State of tracker.
 */
mali_timeline_tracker_state mali_timeline_debug_get_tracker_state(struct mali_timeline_tracker *tracker);

/**
 * Print debug information about tracker.
 *
 * @param tracker Tracker to print.
 */
void mali_timeline_debug_print_tracker(struct mali_timeline_tracker *tracker, _mali_osk_print_ctx *print_ctx);

/**
 * Print debug information about timeline.
 *
 * @param timeline Timeline to print.
 */
void mali_timeline_debug_print_timeline(struct mali_timeline *timeline, _mali_osk_print_ctx *print_ctx);

#if !(LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0))
void mali_timeline_debug_direct_print_tracker(struct mali_timeline_tracker *tracker);
void mali_timeline_debug_direct_print_timeline(struct mali_timeline *timeline);
#endif

/**
 * Print debug information about timeline system.
 *
 * @param system Timeline system to print.
 */
void mali_timeline_debug_print_system(struct mali_timeline_system *system, _mali_osk_print_ctx *print_ctx);

#endif /* defined(MALI_TIMELINE_DEBUG_FUNCTIONS) */

#if defined(CONFIG_MALI_DMA_BUF_FENCE)
/**
 * The timeline dma fence callback when dma fence signal.
 *
 * @param pp_job_ptr The pointer to pp job that link to the signaled dma fence.
 */
void mali_timeline_dma_fence_callback(void *pp_job_ptr);
#endif

#endif /* __MALI_TIMELINE_H__ */
