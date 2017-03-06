/*
 * Copyright (C) 2012-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_SCHEDULER_H__
#define __MALI_SCHEDULER_H__

#include "mali_osk.h"
#include "mali_osk_list.h"
#include "mali_scheduler_types.h"
#include "mali_session.h"

struct mali_scheduler_job_queue {
	_MALI_OSK_LIST_HEAD(normal_pri); /* Queued jobs with normal priority */
	_MALI_OSK_LIST_HEAD(high_pri);   /* Queued jobs with high priority */
	u32 depth;                       /* Depth of combined queues. */
	u32 big_job_num;
};

extern _mali_osk_spinlock_irq_t *mali_scheduler_lock_obj;

/* Queue of jobs to be executed on the GP group */
extern struct mali_scheduler_job_queue job_queue_gp;

/* Queue of PP jobs */
extern struct mali_scheduler_job_queue job_queue_pp;

extern _mali_osk_atomic_t mali_job_id_autonumber;
extern _mali_osk_atomic_t mali_job_cache_order_autonumber;

#define MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD() MALI_DEBUG_ASSERT_LOCK_HELD(mali_scheduler_lock_obj);

_mali_osk_errcode_t mali_scheduler_initialize(void);
void mali_scheduler_terminate(void);

MALI_STATIC_INLINE void mali_scheduler_lock(void)
{
	_mali_osk_spinlock_irq_lock(mali_scheduler_lock_obj);
	MALI_DEBUG_PRINT(5, ("Mali scheduler: scheduler lock taken.\n"));
}

MALI_STATIC_INLINE void mali_scheduler_unlock(void)
{
	MALI_DEBUG_PRINT(5, ("Mali scheduler: Releasing scheduler lock.\n"));
	_mali_osk_spinlock_irq_unlock(mali_scheduler_lock_obj);
}

MALI_STATIC_INLINE u32 mali_scheduler_job_gp_count(void)
{
	return job_queue_gp.depth;
}
MALI_STATIC_INLINE u32 mali_scheduler_job_gp_big_job_count(void)
{
	return job_queue_gp.big_job_num;
}

u32 mali_scheduler_job_physical_head_count(mali_bool gpu_mode_is_secure);

mali_bool mali_scheduler_job_next_is_virtual(void);
struct mali_pp_job *mali_scheduler_job_pp_next(void);

struct mali_gp_job *mali_scheduler_job_gp_get(void);
struct mali_pp_job *mali_scheduler_job_pp_physical_peek(void);
struct mali_pp_job *mali_scheduler_job_pp_virtual_peek(void);
struct mali_pp_job *mali_scheduler_job_pp_physical_get(u32 *sub_job);
struct mali_pp_job *mali_scheduler_job_pp_virtual_get(void);

MALI_STATIC_INLINE u32 mali_scheduler_get_new_id(void)
{
	return _mali_osk_atomic_inc_return(&mali_job_id_autonumber);
}

MALI_STATIC_INLINE u32 mali_scheduler_get_new_cache_order(void)
{
	return _mali_osk_atomic_inc_return(&mali_job_cache_order_autonumber);
}

/**
 * @brief Used by the Timeline system to queue a GP job.
 *
 * @note @ref mali_executor_schedule_from_mask() should be called if this
 * function returns non-zero.
 *
 * @param job The GP job that is being activated.
 *
 * @return A scheduling bitmask that can be used to decide if scheduling is
 * necessary after this call.
 */
mali_scheduler_mask mali_scheduler_activate_gp_job(struct mali_gp_job *job);

/**
 * @brief Used by the Timeline system to queue a PP job.
 *
 * @note @ref mali_executor_schedule_from_mask() should be called if this
 * function returns non-zero.
 *
 * @param job The PP job that is being activated.
 *
 * @return A scheduling bitmask that can be used to decide if scheduling is
 * necessary after this call.
 */
mali_scheduler_mask mali_scheduler_activate_pp_job(struct mali_pp_job *job);

void mali_scheduler_complete_gp_job(struct mali_gp_job *job,
				    mali_bool success,
				    mali_bool user_notification,
				    mali_bool dequeued);

void mali_scheduler_complete_pp_job(struct mali_pp_job *job,
				    u32 num_cores_in_virtual,
				    mali_bool user_notification,
				    mali_bool dequeued);

void mali_scheduler_abort_session(struct mali_session_data *session);

void mali_scheduler_return_pp_job_to_user(struct mali_pp_job *job,
		u32 num_cores_in_virtual);

#if MALI_STATE_TRACKING
u32 mali_scheduler_dump_state(char *buf, u32 size);
#endif

void mali_scheduler_gp_pp_job_queue_print(void);

#endif /* __MALI_SCHEDULER_H__ */
