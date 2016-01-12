/*
 * Copyright (C) 2012-2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_scheduler.h"
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_osk_profiling.h"
#include "mali_kernel_utilization.h"
#include "mali_timeline.h"
#include "mali_gp_job.h"
#include "mali_pp_job.h"
#include "mali_executor.h"
#include "mali_group.h"
#include <linux/wait.h>
#include <linux/sched.h>


#if defined(CONFIG_DMA_SHARED_BUFFER)
#include "mali_memory_dma_buf.h"
#endif

#if defined(CONFIG_GPU_TRACEPOINTS) && defined(CONFIG_TRACEPOINTS)
#include <linux/sched.h>
#include <trace/events/gpu.h>
#endif
/*
 * ---------- static defines/constants ----------
 */

/*
 * If dma_buf with map on demand is used, we defer job queue
 * if in atomic context, since both might sleep.
 */
#if defined(CONFIG_DMA_SHARED_BUFFER)
#if !defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH)
#define MALI_SCHEDULER_USE_DEFERRED_PP_JOB_QUEUE 1
#endif
#endif


/*
 * ---------- global variables (exported due to inline functions) ----------
 */

/* Lock protecting this module */
_mali_osk_spinlock_irq_t *mali_scheduler_lock_obj = NULL;

/* Queue of jobs to be executed on the GP group */
struct mali_scheduler_job_queue job_queue_gp;

/* Queue of PP jobs */
struct mali_scheduler_job_queue job_queue_pp;

_mali_osk_atomic_t mali_job_id_autonumber;
_mali_osk_atomic_t mali_job_cache_order_autonumber;
/*
 * ---------- static variables ----------
 */

_mali_osk_wq_work_t *scheduler_wq_pp_job_delete = NULL;
_mali_osk_spinlock_irq_t *scheduler_pp_job_delete_lock = NULL;
static _MALI_OSK_LIST_HEAD_STATIC_INIT(scheduler_pp_job_deletion_queue);

#if defined(MALI_SCHEDULER_USE_DEFERRED_PP_JOB_QUEUE)
static _mali_osk_wq_work_t *scheduler_wq_pp_job_queue = NULL;
static _mali_osk_spinlock_irq_t *scheduler_pp_job_queue_lock = NULL;
static _MALI_OSK_LIST_HEAD_STATIC_INIT(scheduler_pp_job_queue_list);
#endif

/*
 * ---------- Forward declaration of static functions ----------
 */

static mali_timeline_point mali_scheduler_submit_gp_job(
	struct mali_session_data *session, struct mali_gp_job *job);
static mali_timeline_point mali_scheduler_submit_pp_job(
	struct mali_session_data *session, struct mali_pp_job *job);

static mali_bool mali_scheduler_queue_gp_job(struct mali_gp_job *job);
static mali_bool mali_scheduler_queue_pp_job(struct mali_pp_job *job);

static void mali_scheduler_return_gp_job_to_user(struct mali_gp_job *job,
		mali_bool success);

static void mali_scheduler_deferred_pp_job_delete(struct mali_pp_job *job);
void mali_scheduler_do_pp_job_delete(void *arg);

#if defined(MALI_SCHEDULER_USE_DEFERRED_PP_JOB_QUEUE)
static void mali_scheduler_deferred_pp_job_queue(struct mali_pp_job *job);
static void mali_scheduler_do_pp_job_queue(void *arg);
#endif /* defined(MALI_SCHEDULER_USE_DEFERRED_PP_JOB_QUEUE) */

/*
 * ---------- Actual implementation ----------
 */

_mali_osk_errcode_t mali_scheduler_initialize(void)
{
	_mali_osk_atomic_init(&mali_job_id_autonumber, 0);
	_mali_osk_atomic_init(&mali_job_cache_order_autonumber, 0);

	_MALI_OSK_INIT_LIST_HEAD(&job_queue_gp.normal_pri);
	_MALI_OSK_INIT_LIST_HEAD(&job_queue_gp.high_pri);
	job_queue_gp.depth = 0;
	job_queue_gp.big_job_num = 0;

	_MALI_OSK_INIT_LIST_HEAD(&job_queue_pp.normal_pri);
	_MALI_OSK_INIT_LIST_HEAD(&job_queue_pp.high_pri);
	job_queue_pp.depth = 0;
	job_queue_pp.big_job_num = 0;

	mali_scheduler_lock_obj = _mali_osk_spinlock_irq_init(
					  _MALI_OSK_LOCKFLAG_ORDERED,
					  _MALI_OSK_LOCK_ORDER_SCHEDULER);
	if (NULL == mali_scheduler_lock_obj) {
		mali_scheduler_terminate();
	}

	scheduler_wq_pp_job_delete = _mali_osk_wq_create_work(
					     mali_scheduler_do_pp_job_delete, NULL);
	if (NULL == scheduler_wq_pp_job_delete) {
		mali_scheduler_terminate();
		return _MALI_OSK_ERR_FAULT;
	}

	scheduler_pp_job_delete_lock = _mali_osk_spinlock_irq_init(
					       _MALI_OSK_LOCKFLAG_ORDERED,
					       _MALI_OSK_LOCK_ORDER_SCHEDULER_DEFERRED);
	if (NULL == scheduler_pp_job_delete_lock) {
		mali_scheduler_terminate();
		return _MALI_OSK_ERR_FAULT;
	}

#if defined(MALI_SCHEDULER_USE_DEFERRED_PP_JOB_QUEUE)
	scheduler_wq_pp_job_queue = _mali_osk_wq_create_work(
					    mali_scheduler_do_pp_job_queue, NULL);
	if (NULL == scheduler_wq_pp_job_queue) {
		mali_scheduler_terminate();
		return _MALI_OSK_ERR_FAULT;
	}

	scheduler_pp_job_queue_lock = _mali_osk_spinlock_irq_init(
					      _MALI_OSK_LOCKFLAG_ORDERED,
					      _MALI_OSK_LOCK_ORDER_SCHEDULER_DEFERRED);
	if (NULL == scheduler_pp_job_queue_lock) {
		mali_scheduler_terminate();
		return _MALI_OSK_ERR_FAULT;
	}
#endif /* defined(MALI_SCHEDULER_USE_DEFERRED_PP_JOB_QUEUE) */

	return _MALI_OSK_ERR_OK;
}

void mali_scheduler_terminate(void)
{
#if defined(MALI_SCHEDULER_USE_DEFERRED_PP_JOB_QUEUE)
	if (NULL != scheduler_pp_job_queue_lock) {
		_mali_osk_spinlock_irq_term(scheduler_pp_job_queue_lock);
		scheduler_pp_job_queue_lock = NULL;
	}

	if (NULL != scheduler_wq_pp_job_queue) {
		_mali_osk_wq_delete_work(scheduler_wq_pp_job_queue);
		scheduler_wq_pp_job_queue = NULL;
	}
#endif /* defined(MALI_SCHEDULER_USE_DEFERRED_PP_JOB_QUEUE) */

	if (NULL != scheduler_pp_job_delete_lock) {
		_mali_osk_spinlock_irq_term(scheduler_pp_job_delete_lock);
		scheduler_pp_job_delete_lock = NULL;
	}

	if (NULL != scheduler_wq_pp_job_delete) {
		_mali_osk_wq_delete_work(scheduler_wq_pp_job_delete);
		scheduler_wq_pp_job_delete = NULL;
	}

	if (NULL != mali_scheduler_lock_obj) {
		_mali_osk_spinlock_irq_term(mali_scheduler_lock_obj);
		mali_scheduler_lock_obj = NULL;
	}

	_mali_osk_atomic_term(&mali_job_cache_order_autonumber);
	_mali_osk_atomic_term(&mali_job_id_autonumber);
}

u32 mali_scheduler_job_physical_head_count(void)
{
	/*
	 * Count how many physical sub jobs are present from the head of queue
	 * until the first virtual job is present.
	 * Early out when we have reached maximum number of PP cores (8)
	 */
	u32 count = 0;
	struct mali_pp_job *job;
	struct mali_pp_job *temp;

	/* Check for partially started normal pri jobs */
	if (!_mali_osk_list_empty(&job_queue_pp.normal_pri)) {
		MALI_DEBUG_ASSERT(0 < job_queue_pp.depth);

		job = _MALI_OSK_LIST_ENTRY(job_queue_pp.normal_pri.next,
					   struct mali_pp_job, list);

		MALI_DEBUG_ASSERT_POINTER(job);

		if (MALI_TRUE == mali_pp_job_has_started_sub_jobs(job)) {
			/*
			 * Remember; virtual jobs can't be queued and started
			 * at the same time, so this must be a physical job
			 */
			count += mali_pp_job_unstarted_sub_job_count(job);
			if (MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS <= count) {
				return MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS;
			}
		}
	}

	_MALI_OSK_LIST_FOREACHENTRY(job, temp, &job_queue_pp.high_pri,
				    struct mali_pp_job, list) {
		if (MALI_FALSE == mali_pp_job_is_virtual(job)) {
			count += mali_pp_job_unstarted_sub_job_count(job);
			if (MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS <= count) {
				return MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS;
			}
		} else {
			/* Came across a virtual job, so stop counting */
			return count;
		}
	}

	_MALI_OSK_LIST_FOREACHENTRY(job, temp, &job_queue_pp.normal_pri,
				    struct mali_pp_job, list) {
		if (MALI_FALSE == mali_pp_job_is_virtual(job)) {
			/* any partially started is already counted */
			if (MALI_FALSE == mali_pp_job_has_started_sub_jobs(job)) {
				count += mali_pp_job_unstarted_sub_job_count(job);
				if (MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS <=
				    count) {
					return MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS;
				}
			}
		} else {
			/* Came across a virtual job, so stop counting */
			return count;
		}
	}

	return count;
}

mali_bool mali_scheduler_job_next_is_virtual(void)
{
	struct mali_pp_job *job;

	job = mali_scheduler_job_pp_virtual_peek();
	if (NULL != job) {
		MALI_DEBUG_ASSERT(mali_pp_job_is_virtual(job));

		return MALI_TRUE;
	}

	return MALI_FALSE;
}

struct mali_gp_job *mali_scheduler_job_gp_get(void)
{
	_mali_osk_list_t *queue;
	struct mali_gp_job *job = NULL;

	MALI_DEBUG_ASSERT_LOCK_HELD(mali_scheduler_lock_obj);
	MALI_DEBUG_ASSERT(0 < job_queue_gp.depth);
	MALI_DEBUG_ASSERT(job_queue_gp.big_job_num <= job_queue_gp.depth);

	if (!_mali_osk_list_empty(&job_queue_gp.high_pri)) {
		queue = &job_queue_gp.high_pri;
	} else {
		queue = &job_queue_gp.normal_pri;
		MALI_DEBUG_ASSERT(!_mali_osk_list_empty(queue));
	}

	job = _MALI_OSK_LIST_ENTRY(queue->next, struct mali_gp_job, list);

	MALI_DEBUG_ASSERT_POINTER(job);

	mali_gp_job_list_remove(job);
	job_queue_gp.depth--;
	if (job->big_job) {
		job_queue_gp.big_job_num --;
		if (job_queue_gp.big_job_num < MALI_MAX_PENDING_BIG_JOB) {
			/* wake up process */
			wait_queue_head_t *queue = mali_session_get_wait_queue();
			wake_up(queue);
		}
	}
	return job;
}

struct mali_pp_job *mali_scheduler_job_pp_physical_peek(void)
{
	struct mali_pp_job *job = NULL;
	struct mali_pp_job *tmp_job = NULL;

	MALI_DEBUG_ASSERT_LOCK_HELD(mali_scheduler_lock_obj);

	/*
	 * For PP jobs we favour partially started jobs in normal
	 * priority queue over unstarted jobs in high priority queue
	 */

	if (!_mali_osk_list_empty(&job_queue_pp.normal_pri)) {
		MALI_DEBUG_ASSERT(0 < job_queue_pp.depth);

		tmp_job = _MALI_OSK_LIST_ENTRY(job_queue_pp.normal_pri.next,
					       struct mali_pp_job, list);
		MALI_DEBUG_ASSERT(NULL != tmp_job);

		if (MALI_FALSE == mali_pp_job_is_virtual(tmp_job)) {
			job = tmp_job;
		}
	}

	if (NULL == job ||
	    MALI_FALSE == mali_pp_job_has_started_sub_jobs(job)) {
		/*
		 * There isn't a partially started job in normal queue, so
		 * look in high priority queue.
		 */
		if (!_mali_osk_list_empty(&job_queue_pp.high_pri)) {
			MALI_DEBUG_ASSERT(0 < job_queue_pp.depth);

			tmp_job = _MALI_OSK_LIST_ENTRY(job_queue_pp.high_pri.next,
						       struct mali_pp_job, list);
			MALI_DEBUG_ASSERT(NULL != tmp_job);

			if (MALI_FALSE == mali_pp_job_is_virtual(tmp_job)) {
				job = tmp_job;
			}
		}
	}

	return job;
}

struct mali_pp_job *mali_scheduler_job_pp_virtual_peek(void)
{
	struct mali_pp_job *job = NULL;
	struct mali_pp_job *tmp_job = NULL;

	MALI_DEBUG_ASSERT_LOCK_HELD(mali_scheduler_lock_obj);

	if (!_mali_osk_list_empty(&job_queue_pp.high_pri)) {
		MALI_DEBUG_ASSERT(0 < job_queue_pp.depth);

		tmp_job = _MALI_OSK_LIST_ENTRY(job_queue_pp.high_pri.next,
					       struct mali_pp_job, list);

		if (MALI_TRUE == mali_pp_job_is_virtual(tmp_job)) {
			job = tmp_job;
		}
	}

	if (NULL == job) {
		if (!_mali_osk_list_empty(&job_queue_pp.normal_pri)) {
			MALI_DEBUG_ASSERT(0 < job_queue_pp.depth);

			tmp_job = _MALI_OSK_LIST_ENTRY(job_queue_pp.normal_pri.next,
						       struct mali_pp_job, list);

			if (MALI_TRUE == mali_pp_job_is_virtual(tmp_job)) {
				job = tmp_job;
			}
		}
	}

	return job;
}

struct mali_pp_job *mali_scheduler_job_pp_physical_get(u32 *sub_job)
{
	struct mali_pp_job *job = mali_scheduler_job_pp_physical_peek();

	MALI_DEBUG_ASSERT(MALI_FALSE == mali_pp_job_is_virtual(job));

	if (NULL != job) {
		*sub_job = mali_pp_job_get_first_unstarted_sub_job(job);

		mali_pp_job_mark_sub_job_started(job, *sub_job);
		if (MALI_FALSE == mali_pp_job_has_unstarted_sub_jobs(job)) {
			/* Remove from queue when last sub job has been retrieved */
			mali_pp_job_list_remove(job);
		}

		job_queue_pp.depth--;

		/*
		 * Job about to start so it is no longer be
		 * possible to discard WB
		 */
		mali_pp_job_fb_lookup_remove(job);
	}

	return job;
}

struct mali_pp_job *mali_scheduler_job_pp_virtual_get(void)
{
	struct mali_pp_job *job = mali_scheduler_job_pp_virtual_peek();

	MALI_DEBUG_ASSERT(MALI_TRUE == mali_pp_job_is_virtual(job));

	if (NULL != job) {
		MALI_DEBUG_ASSERT(0 ==
				  mali_pp_job_get_first_unstarted_sub_job(job));
		MALI_DEBUG_ASSERT(1 ==
				  mali_pp_job_get_sub_job_count(job));

		mali_pp_job_mark_sub_job_started(job, 0);

		mali_pp_job_list_remove(job);

		job_queue_pp.depth--;

		/*
		 * Job about to start so it is no longer be
		 * possible to discard WB
		 */
		mali_pp_job_fb_lookup_remove(job);
	}

	return job;
}

mali_scheduler_mask mali_scheduler_activate_gp_job(struct mali_gp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);

	MALI_DEBUG_PRINT(4, ("Mali GP scheduler: Timeline activation for job %u (0x%08X).\n",
			     mali_gp_job_get_id(job), job));

	mali_scheduler_lock();

	if (!mali_scheduler_queue_gp_job(job)) {
		/* Failed to enqueue job, release job (with error) */

		mali_scheduler_unlock();

		mali_timeline_tracker_release(mali_gp_job_get_tracker(job));
		mali_gp_job_signal_pp_tracker(job, MALI_FALSE);

		/* This will notify user space and close the job object */
		mali_scheduler_complete_gp_job(job, MALI_FALSE,
					       MALI_TRUE, MALI_FALSE);

		return MALI_SCHEDULER_MASK_EMPTY;
	}

	mali_scheduler_unlock();

	return MALI_SCHEDULER_MASK_GP;
}

mali_scheduler_mask mali_scheduler_activate_pp_job(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);

	MALI_DEBUG_PRINT(4, ("Mali PP scheduler: Timeline activation for job %u (0x%08X).\n",
			     mali_pp_job_get_id(job), job));

	if (MALI_TRUE == mali_timeline_tracker_activation_error(
		    mali_pp_job_get_tracker(job))) {
		MALI_DEBUG_PRINT(3, ("Mali PP scheduler: Job %u (0x%08X) activated with error, aborting.\n",
				     mali_pp_job_get_id(job), job));

		mali_scheduler_lock();
		mali_pp_job_fb_lookup_remove(job);
		mali_pp_job_mark_unstarted_failed(job);
		mali_scheduler_unlock();

		mali_timeline_tracker_release(mali_pp_job_get_tracker(job));

		/* This will notify user space and close the job object */
		mali_scheduler_complete_pp_job(job, 0, MALI_TRUE, MALI_FALSE);

		return MALI_SCHEDULER_MASK_EMPTY;
	}

#if defined(MALI_SCHEDULER_USE_DEFERRED_PP_JOB_QUEUE)
	if (mali_pp_job_needs_dma_buf_mapping(job)) {
		mali_scheduler_deferred_pp_job_queue(job);
		return MALI_SCHEDULER_MASK_EMPTY;
	}
#endif /* defined(MALI_SCHEDULER_USE_DEFERRED_PP_JOB_QUEUE) */

	mali_scheduler_lock();

	if (!mali_scheduler_queue_pp_job(job)) {
		/* Failed to enqueue job, release job (with error) */
		mali_pp_job_fb_lookup_remove(job);
		mali_pp_job_mark_unstarted_failed(job);
		mali_scheduler_unlock();

		mali_timeline_tracker_release(mali_pp_job_get_tracker(job));

		/* This will notify user space and close the job object */
		mali_scheduler_complete_pp_job(job, 0, MALI_TRUE, MALI_FALSE);

		return MALI_SCHEDULER_MASK_EMPTY;
	}

	mali_scheduler_unlock();
	return MALI_SCHEDULER_MASK_PP;
}

void mali_scheduler_complete_gp_job(struct mali_gp_job *job,
				    mali_bool success,
				    mali_bool user_notification,
				    mali_bool dequeued)
{
	if (user_notification) {
		mali_scheduler_return_gp_job_to_user(job, success);
	}

	if (dequeued) {
		_mali_osk_pm_dev_ref_put();

		if (mali_utilization_enabled()) {
			mali_utilization_gp_end();
		}
	}

	mali_gp_job_delete(job);
}

void mali_scheduler_complete_pp_job(struct mali_pp_job *job,
				    u32 num_cores_in_virtual,
				    mali_bool user_notification,
				    mali_bool dequeued)
{
	job->user_notification = user_notification;
	job->num_pp_cores_in_virtual = num_cores_in_virtual;

	if (dequeued) {
#if defined(CONFIG_MALI_DVFS)
		if (mali_pp_job_is_window_surface(job)) {
			struct mali_session_data *session;
			session = mali_pp_job_get_session(job);
			mali_session_inc_num_window_jobs(session);
		}
#endif

		_mali_osk_pm_dev_ref_put();

		if (mali_utilization_enabled()) {
			mali_utilization_pp_end();
		}
	}

	/* With ZRAM feature enabled, all pp jobs will be force to use deferred delete. */
	mali_scheduler_deferred_pp_job_delete(job);
}

void mali_scheduler_abort_session(struct mali_session_data *session)
{
	struct mali_gp_job *gp_job;
	struct mali_gp_job *gp_tmp;
	struct mali_pp_job *pp_job;
	struct mali_pp_job *pp_tmp;
	_MALI_OSK_LIST_HEAD_STATIC_INIT(removed_jobs_gp);
	_MALI_OSK_LIST_HEAD_STATIC_INIT(removed_jobs_pp);

	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_ASSERT(session->is_aborting);

	MALI_DEBUG_PRINT(3, ("Mali scheduler: Aborting all queued jobs from session 0x%08X.\n",
			     session));

	mali_scheduler_lock();

	/* Remove from GP normal priority queue */
	_MALI_OSK_LIST_FOREACHENTRY(gp_job, gp_tmp, &job_queue_gp.normal_pri,
				    struct mali_gp_job, list) {
		if (mali_gp_job_get_session(gp_job) == session) {
			mali_gp_job_list_move(gp_job, &removed_jobs_gp);
			job_queue_gp.depth--;
			job_queue_gp.big_job_num -= gp_job->big_job ? 1 : 0;
		}
	}

	/* Remove from GP high priority queue */
	_MALI_OSK_LIST_FOREACHENTRY(gp_job, gp_tmp, &job_queue_gp.high_pri,
				    struct mali_gp_job, list) {
		if (mali_gp_job_get_session(gp_job) == session) {
			mali_gp_job_list_move(gp_job, &removed_jobs_gp);
			job_queue_gp.depth--;
			job_queue_gp.big_job_num -= gp_job->big_job ? 1 : 0;
		}
	}

	/* Remove from PP normal priority queue */
	_MALI_OSK_LIST_FOREACHENTRY(pp_job, pp_tmp,
				    &job_queue_pp.normal_pri,
				    struct mali_pp_job, list) {
		if (mali_pp_job_get_session(pp_job) == session) {
			mali_pp_job_fb_lookup_remove(pp_job);

			job_queue_pp.depth -=
				mali_pp_job_unstarted_sub_job_count(
					pp_job);
			mali_pp_job_mark_unstarted_failed(pp_job);

			if (MALI_FALSE == mali_pp_job_has_unstarted_sub_jobs(pp_job)) {
				if (mali_pp_job_is_complete(pp_job)) {
					mali_pp_job_list_move(pp_job,
							      &removed_jobs_pp);
				} else {
					mali_pp_job_list_remove(pp_job);
				}
			}
		}
	}

	/* Remove from PP high priority queue */
	_MALI_OSK_LIST_FOREACHENTRY(pp_job, pp_tmp,
				    &job_queue_pp.high_pri,
				    struct mali_pp_job, list) {
		if (mali_pp_job_get_session(pp_job) == session) {
			mali_pp_job_fb_lookup_remove(pp_job);

			job_queue_pp.depth -=
				mali_pp_job_unstarted_sub_job_count(
					pp_job);
			mali_pp_job_mark_unstarted_failed(pp_job);

			if (MALI_FALSE == mali_pp_job_has_unstarted_sub_jobs(pp_job)) {
				if (mali_pp_job_is_complete(pp_job)) {
					mali_pp_job_list_move(pp_job,
							      &removed_jobs_pp);
				} else {
					mali_pp_job_list_remove(pp_job);
				}
			}
		}
	}

	/*
	 * Release scheduler lock so we can release trackers
	 * (which will potentially queue new jobs)
	 */
	mali_scheduler_unlock();

	/* Release and complete all (non-running) found GP jobs  */
	_MALI_OSK_LIST_FOREACHENTRY(gp_job, gp_tmp, &removed_jobs_gp,
				    struct mali_gp_job, list) {
		mali_timeline_tracker_release(mali_gp_job_get_tracker(gp_job));
		mali_gp_job_signal_pp_tracker(gp_job, MALI_FALSE);
		_mali_osk_list_delinit(&gp_job->list);
		mali_scheduler_complete_gp_job(gp_job,
					       MALI_FALSE, MALI_FALSE, MALI_TRUE);
	}

	/* Release and complete non-running PP jobs */
	_MALI_OSK_LIST_FOREACHENTRY(pp_job, pp_tmp, &removed_jobs_pp,
				    struct mali_pp_job, list) {
		mali_timeline_tracker_release(mali_pp_job_get_tracker(pp_job));
		_mali_osk_list_delinit(&pp_job->list);
		mali_scheduler_complete_pp_job(pp_job, 0,
					       MALI_FALSE, MALI_TRUE);
	}
}

_mali_osk_errcode_t _mali_ukk_gp_start_job(void *ctx,
		_mali_uk_gp_start_job_s *uargs)
{
	struct mali_session_data *session;
	struct mali_gp_job *job;
	mali_timeline_point point;
	u32 __user *point_ptr = NULL;

	MALI_DEBUG_ASSERT_POINTER(uargs);
	MALI_DEBUG_ASSERT_POINTER(ctx);

	session = (struct mali_session_data *)(uintptr_t)ctx;

	job = mali_gp_job_create(session, uargs, mali_scheduler_get_new_id(),
				 NULL);
	if (NULL == job) {
		MALI_PRINT_ERROR(("Failed to create GP job.\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

	point_ptr = (u32 __user *)(uintptr_t)mali_gp_job_get_timeline_point_ptr(job);

	point = mali_scheduler_submit_gp_job(session, job);

	if (0 != _mali_osk_put_user(((u32) point), point_ptr)) {
		/*
		 * Let user space know that something failed
		 * after the job was started.
		 */
		return _MALI_OSK_ERR_ITEM_NOT_FOUND;
	}

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_pp_start_job(void *ctx,
		_mali_uk_pp_start_job_s *uargs)
{
	struct mali_session_data *session;
	struct mali_pp_job *job;
	mali_timeline_point point;
	u32 __user *point_ptr = NULL;

	MALI_DEBUG_ASSERT_POINTER(uargs);
	MALI_DEBUG_ASSERT_POINTER(ctx);

	session = (struct mali_session_data *)(uintptr_t)ctx;

	job = mali_pp_job_create(session, uargs, mali_scheduler_get_new_id());
	if (NULL == job) {
		MALI_PRINT_ERROR(("Failed to create PP job.\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

	point_ptr = (u32 __user *)(uintptr_t)mali_pp_job_get_timeline_point_ptr(job);

	point = mali_scheduler_submit_pp_job(session, job);
	job = NULL;

	if (0 != _mali_osk_put_user(((u32) point), point_ptr)) {
		/*
		 * Let user space know that something failed
		 * after the job was started.
		 */
		return _MALI_OSK_ERR_ITEM_NOT_FOUND;
	}

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_pp_and_gp_start_job(void *ctx,
		_mali_uk_pp_and_gp_start_job_s *uargs)
{
	struct mali_session_data *session;
	_mali_uk_pp_and_gp_start_job_s kargs;
	struct mali_pp_job *pp_job;
	struct mali_gp_job *gp_job;
	u32 __user *point_ptr = NULL;
	mali_timeline_point point;
	_mali_uk_pp_start_job_s __user *pp_args;
	_mali_uk_gp_start_job_s __user *gp_args;

	MALI_DEBUG_ASSERT_POINTER(ctx);
	MALI_DEBUG_ASSERT_POINTER(uargs);

	session = (struct mali_session_data *) ctx;

	if (0 != _mali_osk_copy_from_user(&kargs, uargs,
					  sizeof(_mali_uk_pp_and_gp_start_job_s))) {
		return _MALI_OSK_ERR_NOMEM;
	}

	pp_args = (_mali_uk_pp_start_job_s __user *)(uintptr_t)kargs.pp_args;
	gp_args = (_mali_uk_gp_start_job_s __user *)(uintptr_t)kargs.gp_args;

	pp_job = mali_pp_job_create(session, pp_args,
				    mali_scheduler_get_new_id());
	if (NULL == pp_job) {
		MALI_PRINT_ERROR(("Failed to create PP job.\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

	gp_job = mali_gp_job_create(session, gp_args,
				    mali_scheduler_get_new_id(),
				    mali_pp_job_get_tracker(pp_job));
	if (NULL == gp_job) {
		MALI_PRINT_ERROR(("Failed to create GP job.\n"));
		mali_pp_job_delete(pp_job);
		return _MALI_OSK_ERR_NOMEM;
	}

	point_ptr = (u32 __user *)(uintptr_t)mali_pp_job_get_timeline_point_ptr(pp_job);

	/* Submit GP job. */
	mali_scheduler_submit_gp_job(session, gp_job);
	gp_job = NULL;

	/* Submit PP job. */
	point = mali_scheduler_submit_pp_job(session, pp_job);
	pp_job = NULL;

	if (0 != _mali_osk_put_user(((u32) point), point_ptr)) {
		/*
		 * Let user space know that something failed
		 * after the jobs were started.
		 */
		return _MALI_OSK_ERR_ITEM_NOT_FOUND;
	}

	return _MALI_OSK_ERR_OK;
}

void _mali_ukk_pp_job_disable_wb(_mali_uk_pp_disable_wb_s *args)
{
	struct mali_session_data *session;
	struct mali_pp_job *job;
	struct mali_pp_job *tmp;
	u32 fb_lookup_id;

	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT(NULL != (void *)(uintptr_t)args->ctx);

	session = (struct mali_session_data *)(uintptr_t)args->ctx;

	fb_lookup_id = args->fb_id & MALI_PP_JOB_FB_LOOKUP_LIST_MASK;

	mali_scheduler_lock();

	/* Iterate over all jobs for given frame builder_id. */
	_MALI_OSK_LIST_FOREACHENTRY(job, tmp,
				    &session->pp_job_fb_lookup_list[fb_lookup_id],
				    struct mali_pp_job, session_fb_lookup_list) {
		MALI_DEBUG_CODE(u32 disable_mask = 0);

		if (mali_pp_job_get_frame_builder_id(job) !=
		    (u32) args->fb_id) {
			MALI_DEBUG_PRINT(4, ("Mali PP scheduler: Disable WB mismatching FB.\n"));
			continue;
		}

		MALI_DEBUG_CODE(disable_mask |= 0xD << (4 * 3));

		if (mali_pp_job_get_wb0_source_addr(job) == args->wb0_memory) {
			MALI_DEBUG_CODE(disable_mask |= 0x1 << (4 * 1));
			mali_pp_job_disable_wb0(job);
		}

		if (mali_pp_job_get_wb1_source_addr(job) == args->wb1_memory) {
			MALI_DEBUG_CODE(disable_mask |= 0x2 << (4 * 2));
			mali_pp_job_disable_wb1(job);
		}

		if (mali_pp_job_get_wb2_source_addr(job) == args->wb2_memory) {
			MALI_DEBUG_CODE(disable_mask |= 0x3 << (4 * 3));
			mali_pp_job_disable_wb2(job);
		}
		MALI_DEBUG_PRINT(3, ("Mali PP scheduler: Disable WB: 0x%X.\n",
				     disable_mask));
	}

	mali_scheduler_unlock();
}

#if MALI_STATE_TRACKING
u32 mali_scheduler_dump_state(char *buf, u32 size)
{
	int n = 0;

	n += _mali_osk_snprintf(buf + n, size - n, "GP queues\n");
	n += _mali_osk_snprintf(buf + n, size - n,
				"\tQueue depth: %u\n", job_queue_gp.depth);
	n += _mali_osk_snprintf(buf + n, size - n,
				"\tNormal priority queue is %s\n",
				_mali_osk_list_empty(&job_queue_gp.normal_pri) ?
				"empty" : "not empty");
	n += _mali_osk_snprintf(buf + n, size - n,
				"\tHigh priority queue is %s\n",
				_mali_osk_list_empty(&job_queue_gp.high_pri) ?
				"empty" : "not empty");

	n += _mali_osk_snprintf(buf + n, size - n,
				"PP queues\n");
	n += _mali_osk_snprintf(buf + n, size - n,
				"\tQueue depth: %u\n", job_queue_pp.depth);
	n += _mali_osk_snprintf(buf + n, size - n,
				"\tNormal priority queue is %s\n",
				_mali_osk_list_empty(&job_queue_pp.normal_pri)
				? "empty" : "not empty");
	n += _mali_osk_snprintf(buf + n, size - n,
				"\tHigh priority queue is %s\n",
				_mali_osk_list_empty(&job_queue_pp.high_pri)
				? "empty" : "not empty");

	n += _mali_osk_snprintf(buf + n, size - n, "\n");

	return n;
}
#endif

/*
 * ---------- Implementation of static functions ----------
 */

static mali_timeline_point mali_scheduler_submit_gp_job(
	struct mali_session_data *session, struct mali_gp_job *job)
{
	mali_timeline_point point;

	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_ASSERT_POINTER(job);

	/* Add job to Timeline system. */
	point = mali_timeline_system_add_tracker(session->timeline_system,
			mali_gp_job_get_tracker(job), MALI_TIMELINE_GP);

	return point;
}

static mali_timeline_point mali_scheduler_submit_pp_job(
	struct mali_session_data *session, struct mali_pp_job *job)
{
	mali_timeline_point point;

	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_ASSERT_POINTER(job);

	mali_scheduler_lock();
	/*
	 * Adding job to the lookup list used to quickly discard
	 * writeback units of queued jobs.
	 */
	mali_pp_job_fb_lookup_add(job);
	mali_scheduler_unlock();

	/* Add job to Timeline system. */
	point = mali_timeline_system_add_tracker(session->timeline_system,
			mali_pp_job_get_tracker(job), MALI_TIMELINE_PP);

	return point;
}

static mali_bool mali_scheduler_queue_gp_job(struct mali_gp_job *job)
{
	struct mali_session_data *session;
	_mali_osk_list_t *queue;

	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();
	MALI_DEBUG_ASSERT_POINTER(job);

	session = mali_gp_job_get_session(job);
	MALI_DEBUG_ASSERT_POINTER(session);

	if (unlikely(session->is_aborting)) {
		MALI_DEBUG_PRINT(4, ("Mali GP scheduler: Job %u (0x%08X) queued while session is aborting.\n",
				     mali_gp_job_get_id(job), job));
		return MALI_FALSE; /* job not queued */
	}

	mali_gp_job_set_cache_order(job, mali_scheduler_get_new_cache_order());

	/* Determine which queue the job should be added to. */
	if (session->use_high_priority_job_queue) {
		queue = &job_queue_gp.high_pri;
	} else {
		queue = &job_queue_gp.normal_pri;
	}

	job_queue_gp.depth += 1;
	job_queue_gp.big_job_num += (job->big_job) ? 1 : 0;

	/* Add job to queue (mali_gp_job_queue_add find correct place). */
	mali_gp_job_list_add(job, queue);

	/*
	 * We hold a PM reference for every job we hold queued (and running)
	 * It is important that we take this reference after job has been
	 * added the the queue so that any runtime resume could schedule this
	 * job right there and then.
	 */
	_mali_osk_pm_dev_ref_get_async();

	if (mali_utilization_enabled()) {
		/*
		 * We cheat a little bit by counting the GP as busy from the
		 * time a GP job is queued. This will be fine because we only
		 * loose the tiny idle gap between jobs, but we will instead
		 * get less utilization work to do (less locks taken)
		 */
		mali_utilization_gp_start();
	}

	/* Add profiling events for job enqueued */
	_mali_osk_profiling_add_event(
		MALI_PROFILING_EVENT_TYPE_SINGLE |
		MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
		MALI_PROFILING_EVENT_REASON_SINGLE_SW_GP_ENQUEUE,
		mali_gp_job_get_pid(job),
		mali_gp_job_get_tid(job),
		mali_gp_job_get_frame_builder_id(job),
		mali_gp_job_get_flush_id(job),
		0);

#if defined(CONFIG_GPU_TRACEPOINTS) && defined(CONFIG_TRACEPOINTS)
	trace_gpu_job_enqueue(mali_gp_job_get_tid(job),
			      mali_gp_job_get_id(job), "GP");
#endif

	MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Job %u (0x%08X) queued\n",
			     mali_gp_job_get_id(job), job));

	return MALI_TRUE; /* job queued */
}

static mali_bool mali_scheduler_queue_pp_job(struct mali_pp_job *job)
{
	struct mali_session_data *session;
	_mali_osk_list_t *queue = NULL;

	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();
	MALI_DEBUG_ASSERT_POINTER(job);

	session = mali_pp_job_get_session(job);
	MALI_DEBUG_ASSERT_POINTER(session);

	if (unlikely(session->is_aborting)) {
		MALI_DEBUG_PRINT(2, ("Mali PP scheduler: Job %u (0x%08X) queued while session is aborting.\n",
				     mali_pp_job_get_id(job), job));
		return MALI_FALSE; /* job not queued */
	} else if (unlikely(MALI_SWAP_IN_FAIL == job->swap_status)) {
		MALI_DEBUG_PRINT(2, ("Mali PP scheduler: Job %u (0x%08X) queued while swap in failed.\n",
				     mali_pp_job_get_id(job), job));
		return MALI_FALSE;
	}

	mali_pp_job_set_cache_order(job, mali_scheduler_get_new_cache_order());

	if (session->use_high_priority_job_queue) {
		queue = &job_queue_pp.high_pri;
	} else {
		queue = &job_queue_pp.normal_pri;
	}

	job_queue_pp.depth +=
		mali_pp_job_get_sub_job_count(job);

	/* Add job to queue (mali_gp_job_queue_add find correct place). */
	mali_pp_job_list_add(job, queue);

	/*
	 * We hold a PM reference for every job we hold queued (and running)
	 * It is important that we take this reference after job has been
	 * added the the queue so that any runtime resume could schedule this
	 * job right there and then.
	 */
	_mali_osk_pm_dev_ref_get_async();

	if (mali_utilization_enabled()) {
		/*
		 * We cheat a little bit by counting the PP as busy from the
		 * time a PP job is queued. This will be fine because we only
		 * loose the tiny idle gap between jobs, but we will instead
		 * get less utilization work to do (less locks taken)
		 */
		mali_utilization_pp_start();
	}

	/* Add profiling events for job enqueued */

	_mali_osk_profiling_add_event(
		MALI_PROFILING_EVENT_TYPE_SINGLE |
		MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
		MALI_PROFILING_EVENT_REASON_SINGLE_SW_PP_ENQUEUE,
		mali_pp_job_get_pid(job),
		mali_pp_job_get_tid(job),
		mali_pp_job_get_frame_builder_id(job),
		mali_pp_job_get_flush_id(job),
		0);

#if defined(CONFIG_GPU_TRACEPOINTS) && defined(CONFIG_TRACEPOINTS)
	trace_gpu_job_enqueue(mali_pp_job_get_tid(job),
			      mali_pp_job_get_id(job), "PP");
#endif

	MALI_DEBUG_PRINT(3, ("Mali PP scheduler: %s job %u (0x%08X) with %u parts queued.\n",
			     mali_pp_job_is_virtual(job)
			     ? "Virtual" : "Physical",
			     mali_pp_job_get_id(job), job,
			     mali_pp_job_get_sub_job_count(job)));

	return MALI_TRUE; /* job queued */
}

static void mali_scheduler_return_gp_job_to_user(struct mali_gp_job *job,
		mali_bool success)
{
	_mali_uk_gp_job_finished_s *jobres;
	struct mali_session_data *session;
	_mali_osk_notification_t *notification;

	MALI_DEBUG_ASSERT_POINTER(job);

	session = mali_gp_job_get_session(job);
	MALI_DEBUG_ASSERT_POINTER(session);

	notification = mali_gp_job_get_finished_notification(job);
	MALI_DEBUG_ASSERT_POINTER(notification);

	jobres = notification->result_buffer;
	MALI_DEBUG_ASSERT_POINTER(jobres);

	jobres->pending_big_job_num = mali_scheduler_job_gp_big_job_count();

	jobres->user_job_ptr = mali_gp_job_get_user_id(job);
	if (MALI_TRUE == success) {
		jobres->status = _MALI_UK_JOB_STATUS_END_SUCCESS;
	} else {
		jobres->status = _MALI_UK_JOB_STATUS_END_UNKNOWN_ERR;
	}
	jobres->heap_current_addr = mali_gp_job_get_current_heap_addr(job);
	jobres->perf_counter0 = mali_gp_job_get_perf_counter_value0(job);
	jobres->perf_counter1 = mali_gp_job_get_perf_counter_value1(job);

	mali_session_send_notification(session, notification);
}

void mali_scheduler_return_pp_job_to_user(struct mali_pp_job *job,
		u32 num_cores_in_virtual)
{
	u32 i;
	u32 num_counters_to_copy;
	_mali_uk_pp_job_finished_s *jobres;
	struct mali_session_data *session;
	_mali_osk_notification_t *notification;

	if (MALI_TRUE == mali_pp_job_use_no_notification(job)) {
		return;
	}

	MALI_DEBUG_ASSERT_POINTER(job);

	session = mali_pp_job_get_session(job);
	MALI_DEBUG_ASSERT_POINTER(session);

	notification = mali_pp_job_get_finished_notification(job);
	MALI_DEBUG_ASSERT_POINTER(notification);

	jobres = notification->result_buffer;
	MALI_DEBUG_ASSERT_POINTER(jobres);

	jobres->user_job_ptr = mali_pp_job_get_user_id(job);
	if (MALI_TRUE == mali_pp_job_was_success(job)) {
		jobres->status = _MALI_UK_JOB_STATUS_END_SUCCESS;
	} else {
		jobres->status = _MALI_UK_JOB_STATUS_END_UNKNOWN_ERR;
	}

	if (mali_pp_job_is_virtual(job)) {
		num_counters_to_copy = num_cores_in_virtual;
	} else {
		num_counters_to_copy = mali_pp_job_get_sub_job_count(job);
	}

	for (i = 0; i < num_counters_to_copy; i++) {
		jobres->perf_counter0[i] =
			mali_pp_job_get_perf_counter_value0(job, i);
		jobres->perf_counter1[i] =
			mali_pp_job_get_perf_counter_value1(job, i);
		jobres->perf_counter_src0 =
			mali_pp_job_get_pp_counter_global_src0();
		jobres->perf_counter_src1 =
			mali_pp_job_get_pp_counter_global_src1();
	}

	mali_session_send_notification(session, notification);
}

static void mali_scheduler_deferred_pp_job_delete(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);

	_mali_osk_spinlock_irq_lock(scheduler_pp_job_delete_lock);
	mali_pp_job_list_addtail(job, &scheduler_pp_job_deletion_queue);
	_mali_osk_spinlock_irq_unlock(scheduler_pp_job_delete_lock);

	_mali_osk_wq_schedule_work(scheduler_wq_pp_job_delete);
}

void mali_scheduler_do_pp_job_delete(void *arg)
{
	_MALI_OSK_LIST_HEAD_STATIC_INIT(list);
	struct mali_pp_job *job;
	struct mali_pp_job *tmp;

	MALI_IGNORE(arg);

	/*
	 * Quickly "unhook" the jobs pending to be deleted, so we can release
	 * the lock before we start deleting the job objects
	 * (without any locks held)
	 */
	_mali_osk_spinlock_irq_lock(scheduler_pp_job_delete_lock);
	_mali_osk_list_move_list(&scheduler_pp_job_deletion_queue, &list);
	_mali_osk_spinlock_irq_unlock(scheduler_pp_job_delete_lock);

	_MALI_OSK_LIST_FOREACHENTRY(job, tmp, &list,
				    struct mali_pp_job, list) {
		_mali_osk_list_delinit(&job->list);

		mali_pp_job_delete(job); /* delete the job object itself */
	}
}

#if defined(MALI_SCHEDULER_USE_DEFERRED_PP_JOB_QUEUE)

static void mali_scheduler_deferred_pp_job_queue(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);

	_mali_osk_spinlock_irq_lock(scheduler_pp_job_queue_lock);
	mali_pp_job_list_addtail(job, &scheduler_pp_job_queue_list);
	_mali_osk_spinlock_irq_unlock(scheduler_pp_job_queue_lock);

	_mali_osk_wq_schedule_work(scheduler_wq_pp_job_queue);
}

static void mali_scheduler_do_pp_job_queue(void *arg)
{
	_MALI_OSK_LIST_HEAD_STATIC_INIT(list);
	struct mali_pp_job *job;
	struct mali_pp_job *tmp;
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;

	MALI_IGNORE(arg);

	/*
	 * Quickly "unhook" the jobs pending to be queued, so we can release
	 * the lock before we start queueing the job objects
	 * (without any locks held)
	 */
	_mali_osk_spinlock_irq_lock(scheduler_pp_job_queue_lock);
	_mali_osk_list_move_list(&scheduler_pp_job_queue_list, &list);
	_mali_osk_spinlock_irq_unlock(scheduler_pp_job_queue_lock);

	/* First loop through all jobs and do the pre-work (no locks needed) */
	_MALI_OSK_LIST_FOREACHENTRY(job, tmp, &list,
				    struct mali_pp_job, list) {
		if (mali_pp_job_needs_dma_buf_mapping(job)) {
			/*
			 * This operation could fail, but we continue anyway,
			 * because the worst that could happen is that this
			 * job will fail due to a Mali page fault.
			 */
			mali_dma_buf_map_job(job);
		}
	}

	mali_scheduler_lock();

	/* Then loop through all jobs again to queue them (lock needed) */
	_MALI_OSK_LIST_FOREACHENTRY(job, tmp, &list,
				    struct mali_pp_job, list) {

		/* Remove from scheduler_pp_job_queue_list before queueing */
		mali_pp_job_list_remove(job);

		if (mali_scheduler_queue_pp_job(job)) {
			/* Job queued successfully */
			schedule_mask |= MALI_SCHEDULER_MASK_PP;
		} else {
			/* Failed to enqueue job, release job (with error) */
			mali_pp_job_fb_lookup_remove(job);
			mali_pp_job_mark_unstarted_failed(job);

			/* unlock scheduler in this uncommon case */
			mali_scheduler_unlock();

			schedule_mask |= mali_timeline_tracker_release(
						 mali_pp_job_get_tracker(job));

			/* Notify user space and close the job object */
			mali_scheduler_complete_pp_job(job, 0, MALI_TRUE,
						       MALI_FALSE);

			mali_scheduler_lock();
		}
	}

	mali_scheduler_unlock();

	/* Trigger scheduling of jobs */
	mali_executor_schedule_from_mask(schedule_mask, MALI_FALSE);
}

#endif /* defined(MALI_SCHEDULER_USE_DEFERRED_PP_JOB_QUEUE) */

void mali_scheduler_gp_pp_job_queue_print(void)
{
	struct mali_gp_job *gp_job = NULL;
	struct mali_gp_job *tmp_gp_job = NULL;
	struct mali_pp_job *pp_job = NULL;
	struct mali_pp_job *tmp_pp_job = NULL;

	MALI_DEBUG_ASSERT_LOCK_HELD(mali_scheduler_lock_obj);
	MALI_DEBUG_ASSERT_LOCK_HELD(mali_executor_lock_obj);

	/* dump job queup status */
	if ((0 == job_queue_gp.depth) && (0 == job_queue_pp.depth)) {
		MALI_PRINT(("No GP&PP job in the job queue.\n"));
		return;
	}

	MALI_PRINT(("Total (%d) GP job in the job queue.\n", job_queue_gp.depth));
	if (job_queue_gp.depth > 0) {
		if (!_mali_osk_list_empty(&job_queue_gp.high_pri)) {
			_MALI_OSK_LIST_FOREACHENTRY(gp_job, tmp_gp_job, &job_queue_gp.high_pri,
						    struct mali_gp_job, list) {
				MALI_PRINT(("GP job(%p) id = %d tid = %d pid = %d in the gp job high_pri queue\n", gp_job, gp_job->id, gp_job->tid, gp_job->pid));
			}
		}

		if (!_mali_osk_list_empty(&job_queue_gp.normal_pri)) {
			_MALI_OSK_LIST_FOREACHENTRY(gp_job, tmp_gp_job, &job_queue_gp.normal_pri,
						    struct mali_gp_job, list) {
				MALI_PRINT(("GP job(%p) id = %d tid = %d pid = %d in the gp job normal_pri queue\n", gp_job, gp_job->id, gp_job->tid, gp_job->pid));
			}
		}
	}

	MALI_PRINT(("Total (%d) PP job in the job queue.\n", job_queue_pp.depth));
	if (job_queue_pp.depth > 0) {
		if (!_mali_osk_list_empty(&job_queue_pp.high_pri)) {
			_MALI_OSK_LIST_FOREACHENTRY(pp_job, tmp_pp_job, &job_queue_pp.high_pri,
						    struct mali_pp_job, list) {
				if (mali_pp_job_is_virtual(pp_job)) {
					MALI_PRINT(("PP Virtual job(%p) id = %d tid = %d pid = %d in the pp job high_pri queue\n", pp_job, pp_job->id, pp_job->tid, pp_job->pid));
				} else {
					MALI_PRINT(("PP Physical job(%p) id = %d tid = %d pid = %d in the pp job high_pri queue\n", pp_job, pp_job->id, pp_job->tid, pp_job->pid));
				}
			}
		}

		if (!_mali_osk_list_empty(&job_queue_pp.normal_pri)) {
			_MALI_OSK_LIST_FOREACHENTRY(pp_job, tmp_pp_job, &job_queue_pp.normal_pri,
						    struct mali_pp_job, list) {
				if (mali_pp_job_is_virtual(pp_job)) {
					MALI_PRINT(("PP Virtual job(%p) id = %d tid = %d pid = %d in the pp job normal_pri queue\n", pp_job, pp_job->id, pp_job->tid, pp_job->pid));
				} else {
					MALI_PRINT(("PP Physical job(%p) id = %d tid = %d pid = %d in the pp job normal_pri queue\n", pp_job, pp_job->id, pp_job->tid, pp_job->pid));
				}
			}
		}
	}

	/* dump group running job status */
	mali_executor_running_status_print();
}
