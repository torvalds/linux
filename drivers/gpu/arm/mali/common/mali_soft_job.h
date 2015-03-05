/*
 * Copyright (C) 2013-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_SOFT_JOB_H__
#define __MALI_SOFT_JOB_H__

#include "mali_osk.h"

#include "mali_timeline.h"

struct mali_timeline_fence;
struct mali_session_data;
struct mali_soft_job;
struct mali_soft_job_system;

/**
 * Soft job types.
 *
 * Soft jobs of type MALI_SOFT_JOB_TYPE_USER_SIGNALED will only complete after activation if either
 * they are signaled by user-space (@ref mali_soft_job_system_signaled_job) or if they are timed out
 * by the Timeline system.
 * Soft jobs of type MALI_SOFT_JOB_TYPE_SELF_SIGNALED will release job resource automatically
 * in kernel when the job is activated.
 */
typedef enum mali_soft_job_type {
	MALI_SOFT_JOB_TYPE_SELF_SIGNALED,
	MALI_SOFT_JOB_TYPE_USER_SIGNALED,
} mali_soft_job_type;

/**
 * Soft job state.
 *
 * mali_soft_job_system_start_job a job will first be allocated.The job's state set to MALI_SOFT_JOB_STATE_ALLOCATED.
 * Once the job is added to the timeline system, the state changes to MALI_SOFT_JOB_STATE_STARTED.
 *
 * For soft jobs of type MALI_SOFT_JOB_TYPE_USER_SIGNALED the state is changed to
 * MALI_SOFT_JOB_STATE_SIGNALED when @ref mali_soft_job_system_signal_job is called and the soft
 * job's state is MALI_SOFT_JOB_STATE_STARTED or MALI_SOFT_JOB_STATE_TIMED_OUT.
 *
 * If a soft job of type MALI_SOFT_JOB_TYPE_USER_SIGNALED is timed out before being signaled, the
 * state is changed to MALI_SOFT_JOB_STATE_TIMED_OUT.  This can only happen to soft jobs in state
 * MALI_SOFT_JOB_STATE_STARTED.
 *
 */
typedef enum mali_soft_job_state {
	MALI_SOFT_JOB_STATE_ALLOCATED,
	MALI_SOFT_JOB_STATE_STARTED,
	MALI_SOFT_JOB_STATE_SIGNALED,
	MALI_SOFT_JOB_STATE_TIMED_OUT,
} mali_soft_job_state;

#define MALI_SOFT_JOB_INVALID_ID ((u32) -1)

/**
 * Soft job struct.
 *
 * Soft job can be used to represent any kind of CPU work done in kernel-space.
 */
typedef struct mali_soft_job {
	mali_soft_job_type            type;                   /**< Soft job type.  Must be one of MALI_SOFT_JOB_TYPE_*. */
	u64                           user_job;               /**< Identifier for soft job in user space. */
	_mali_osk_atomic_t            refcount;               /**< Soft jobs are reference counted to prevent premature deletion. */
	struct mali_timeline_tracker  tracker;                /**< Timeline tracker for soft job. */
	mali_bool                     activated;              /**< MALI_TRUE if the job has been activated, MALI_FALSE if not. */
	_mali_osk_notification_t     *activated_notification; /**< Pre-allocated notification object for ACTIVATED_NOTIFICATION. */

	/* Protected by soft job system lock. */
	u32                           id;                     /**< Used by user-space to find corresponding soft job in kernel-space. */
	mali_soft_job_state           state;                  /**< State of soft job, must be one of MALI_SOFT_JOB_STATE_*. */
	struct mali_soft_job_system  *system;                 /**< The soft job system this job is in. */
	_mali_osk_list_t              system_list;            /**< List element used by soft job system. */
} mali_soft_job;

/**
 * Per-session soft job system.
 *
 * The soft job system is used to manage all soft jobs that belongs to a session.
 */
typedef struct mali_soft_job_system {
	struct mali_session_data *session;                    /**< The session this soft job system belongs to. */
	_MALI_OSK_LIST_HEAD(jobs_used);                       /**< List of all allocated soft jobs. */

	_mali_osk_spinlock_irq_t *lock;                       /**< Lock used to protect soft job system and its soft jobs. */
	u32 lock_owner;                                       /**< Contains tid of thread that locked the system or 0, if not locked. */
	u32 last_job_id;                                      /**< Recored the last job id protected by lock. */
} mali_soft_job_system;

/**
 * Create a soft job system.
 *
 * @param session The session this soft job system will belong to.
 * @return The new soft job system, or NULL if unsuccessful.
 */
struct mali_soft_job_system *mali_soft_job_system_create(struct mali_session_data *session);

/**
 * Destroy a soft job system.
 *
 * @note The soft job must not have any started or activated jobs.  Call @ref
 * mali_soft_job_system_abort first.
 *
 * @param system The soft job system we are destroying.
 */
void mali_soft_job_system_destroy(struct mali_soft_job_system *system);

/**
 * Create a soft job.
 *
 * @param system Soft job system to create soft job from.
 * @param type Type of the soft job.
 * @param user_job Identifier for soft job in user space.
 * @return New soft job if successful, NULL if not.
 */
struct mali_soft_job *mali_soft_job_create(struct mali_soft_job_system *system, mali_soft_job_type type, u64 user_job);

/**
 * Destroy soft job.
 *
 * @param job Soft job to destroy.
 */
void mali_soft_job_destroy(struct mali_soft_job *job);

/**
 * Start a soft job.
 *
 * The soft job will be added to the Timeline system which will then activate it after all
 * dependencies have been resolved.
 *
 * Create soft jobs with @ref mali_soft_job_create before starting them.
 *
 * @param job Soft job to start.
 * @param fence Fence representing dependencies for this soft job.
 * @return Point on soft job timeline.
 */
mali_timeline_point mali_soft_job_start(struct mali_soft_job *job, struct mali_timeline_fence *fence);

/**
 * Use by user-space to signal that a soft job has completed.
 *
 * @note Only valid for soft jobs with type MALI_SOFT_JOB_TYPE_USER_SIGNALED.
 *
 * @note The soft job must be in state MALI_SOFT_JOB_STATE_STARTED for the signal to be successful.
 *
 * @note If the soft job was signaled successfully, or it received a time out, the soft job will be
 * destroyed after this call and should no longer be used.
 *
 * @note This function will block until the soft job has been activated.
 *
 * @param system The soft job system the job was started in.
 * @param job_id ID of soft job we are signaling.
 *
 * @return _MALI_OSK_ERR_ITEM_NOT_FOUND if the soft job ID was invalid, _MALI_OSK_ERR_TIMEOUT if the
 * soft job was timed out or _MALI_OSK_ERR_OK if we successfully signaled the soft job.
 */
_mali_osk_errcode_t mali_soft_job_system_signal_job(struct mali_soft_job_system *system, u32 job_id);

/**
 * Used by the Timeline system to activate a soft job.
 *
 * @param job The soft job that is being activated.
 */
void mali_soft_job_system_activate_job(struct mali_soft_job *job);

/**
 * Used by the Timeline system to timeout a soft job.
 *
 * A soft job is timed out if it completes or is signaled later than MALI_TIMELINE_TIMEOUT_HZ after
 * activation.
 *
 * @param job The soft job that is being timed out.
 * @return A scheduling bitmask.
 */
mali_scheduler_mask mali_soft_job_system_timeout_job(struct mali_soft_job *job);

/**
 * Used to cleanup activated soft jobs in the soft job system on session abort.
 *
 * @param system The soft job system that is being aborted.
 */
void mali_soft_job_system_abort(struct mali_soft_job_system *system);

#endif /* __MALI_SOFT_JOB_H__ */
