/*
 * Copyright (C) 2012-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_executor.h"
#include "mali_scheduler.h"
#include "mali_kernel_common.h"
#include "mali_kernel_core.h"
#include "mali_osk.h"
#include "mali_osk_list.h"
#include "mali_pp.h"
#include "mali_pp_job.h"
#include "mali_group.h"
#include "mali_pm.h"
#include "mali_timeline.h"
#include "mali_osk_profiling.h"
#include "mali_session.h"

/*
 * If dma_buf with map on demand is used, we defer job deletion and job queue
 * if in atomic context, since both might sleep.
 */
#if defined(CONFIG_DMA_SHARED_BUFFER) && !defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH)
#define MALI_EXECUTOR_USE_DEFERRED_PP_JOB_DELETE 1
#define MALI_EXECUTOR_USE_DEFERRED_PP_JOB_QUEUE 1
#endif /* !defined(CONFIG_DMA_SHARED_BUFFER) && !defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH) */

/*
 * ---------- static type definitions (structs, enums, etc) ----------
 */

enum mali_executor_state_t {
	EXEC_STATE_NOT_PRESENT, /* Virtual group on Mali-300/400 (do not use) */
	EXEC_STATE_DISABLED,    /* Disabled by core scaling (do not use) */
	EXEC_STATE_EMPTY,       /* No child groups for virtual group (do not use) */
	EXEC_STATE_INACTIVE,    /* Can be used, but must be activate first */
	EXEC_STATE_IDLE,        /* Active and ready to be used */
	EXEC_STATE_WORKING,     /* Executing a job */
};

/*
 * ---------- global variables (exported due to inline functions) ----------
 */

/* Lock for this module (protecting all HW access except L2 caches) */
_mali_osk_spinlock_irq_t *mali_executor_lock_obj = NULL;

mali_bool mali_executor_hints[MALI_EXECUTOR_HINT_MAX];

/*
 * ---------- static variables ----------
 */

/* Used to defer job scheduling */
static _mali_osk_wq_work_t *executor_wq_high_pri = NULL;

/* Store version from GP and PP (user space wants to know this) */
static u32 pp_version = 0;
static u32 gp_version = 0;

/* List of physical PP groups which are disabled by some external source */
static _MALI_OSK_LIST_HEAD_STATIC_INIT(group_list_disabled);
static u32 group_list_disabled_count = 0;

/* List of groups which can be used, but activate first */
static _MALI_OSK_LIST_HEAD_STATIC_INIT(group_list_inactive);
static u32 group_list_inactive_count = 0;

/* List of groups which are active and ready to be used */
static _MALI_OSK_LIST_HEAD_STATIC_INIT(group_list_idle);
static u32 group_list_idle_count = 0;

/* List of groups which are executing a job */
static _MALI_OSK_LIST_HEAD_STATIC_INIT(group_list_working);
static u32 group_list_working_count = 0;

/* Virtual group (if any) */
static struct mali_group *virtual_group = NULL;

/* Virtual group state is tracked with a state variable instead of 4 lists */
static enum mali_executor_state_t virtual_group_state = EXEC_STATE_NOT_PRESENT;

/* GP group */
static struct mali_group *gp_group = NULL;

/* GP group state is tracked with a state variable instead of 4 lists */
static enum mali_executor_state_t gp_group_state = EXEC_STATE_NOT_PRESENT;

static u32 gp_returned_cookie = 0;

/* Total number of physical PP cores present */
static u32 num_physical_pp_cores_total = 0;

/* Number of physical cores which are enabled */
static u32 num_physical_pp_cores_enabled = 0;

/* Enable or disable core scaling */
static mali_bool core_scaling_enabled = MALI_TRUE;

/* Variables to allow safe pausing of the scheduler */
static _mali_osk_wait_queue_t *executor_working_wait_queue = NULL;
static u32 pause_count = 0;

/* PP cores haven't been enabled because of some pp cores haven't been disabled. */
static int core_scaling_delay_up_mask[MALI_MAX_NUMBER_OF_DOMAINS] = { 0 };

/* Variables used to implement notify pp core changes to userspace when core scaling
 * is finished in mali_executor_complete_group() function. */
static _mali_osk_wq_work_t *executor_wq_notify_core_change = NULL;
static _mali_osk_wait_queue_t *executor_notify_core_change_wait_queue = NULL;

/*
 * ---------- Forward declaration of static functions ----------
 */
static void mali_executor_lock(void);
static void mali_executor_unlock(void);
static mali_bool mali_executor_is_suspended(void *data);
static mali_bool mali_executor_is_working(void);
static void mali_executor_disable_empty_virtual(void);
static mali_bool mali_executor_physical_rejoin_virtual(struct mali_group *group);
static mali_bool mali_executor_has_virtual_group(void);
static mali_bool mali_executor_virtual_group_is_usable(void);
static void mali_executor_schedule(void);
static void mali_executor_wq_schedule(void *arg);
static void mali_executor_send_gp_oom_to_user(struct mali_gp_job *job);
static void mali_executor_complete_group(struct mali_group *group,
		mali_bool success,
		mali_bool release_jobs,
		struct mali_gp_job **gp_job_done,
		struct mali_pp_job **pp_job_done);
static void mali_executor_change_state_pp_physical(struct mali_group *group,
		_mali_osk_list_t *old_list,
		u32 *old_count,
		_mali_osk_list_t *new_list,
		u32 *new_count);
static mali_bool mali_executor_group_is_in_state(struct mali_group *group,
		enum mali_executor_state_t state);

static void mali_executor_group_enable_internal(struct mali_group *group);
static void mali_executor_group_disable_internal(struct mali_group *group);
static void mali_executor_core_scale(unsigned int target_core_nr);
static void mali_executor_core_scale_in_group_complete(struct mali_group *group);
static void mali_executor_notify_core_change(u32 num_cores);
static void mali_executor_wq_notify_core_change(void *arg);
static void mali_executor_change_group_status_disabled(struct mali_group *group);
static mali_bool mali_executor_deactivate_list_idle(mali_bool deactivate_idle_group);
static void mali_executor_set_state_pp_physical(struct mali_group *group,
		_mali_osk_list_t *new_list,
		u32 *new_count);

/*
 * ---------- Actual implementation ----------
 */

_mali_osk_errcode_t mali_executor_initialize(void)
{
	mali_executor_lock_obj = _mali_osk_spinlock_irq_init(_MALI_OSK_LOCKFLAG_ORDERED, _MALI_OSK_LOCK_ORDER_EXECUTOR);
	if (NULL == mali_executor_lock_obj) {
		mali_executor_terminate();
		return _MALI_OSK_ERR_NOMEM;
	}

	executor_wq_high_pri = _mali_osk_wq_create_work_high_pri(mali_executor_wq_schedule, NULL);
	if (NULL == executor_wq_high_pri) {
		mali_executor_terminate();
		return _MALI_OSK_ERR_NOMEM;
	}

	executor_working_wait_queue = _mali_osk_wait_queue_init();
	if (NULL == executor_working_wait_queue) {
		mali_executor_terminate();
		return _MALI_OSK_ERR_NOMEM;
	}

	executor_wq_notify_core_change = _mali_osk_wq_create_work(mali_executor_wq_notify_core_change, NULL);
	if (NULL == executor_wq_notify_core_change) {
		mali_executor_terminate();
		return _MALI_OSK_ERR_NOMEM;
	}

	executor_notify_core_change_wait_queue = _mali_osk_wait_queue_init();
	if (NULL == executor_notify_core_change_wait_queue) {
		mali_executor_terminate();
		return _MALI_OSK_ERR_NOMEM;
	}

	return _MALI_OSK_ERR_OK;
}

void mali_executor_terminate(void)
{
	if (NULL != executor_notify_core_change_wait_queue) {
		_mali_osk_wait_queue_term(executor_notify_core_change_wait_queue);
		executor_notify_core_change_wait_queue = NULL;
	}

	if (NULL != executor_wq_notify_core_change) {
		_mali_osk_wq_delete_work(executor_wq_notify_core_change);
		executor_wq_notify_core_change = NULL;
	}

	if (NULL != executor_working_wait_queue) {
		_mali_osk_wait_queue_term(executor_working_wait_queue);
		executor_working_wait_queue = NULL;
	}

	if (NULL != executor_wq_high_pri) {
		_mali_osk_wq_delete_work(executor_wq_high_pri);
		executor_wq_high_pri = NULL;
	}

	if (NULL != mali_executor_lock_obj) {
		_mali_osk_spinlock_irq_term(mali_executor_lock_obj);
		mali_executor_lock_obj = NULL;
	}
}

void mali_executor_populate(void)
{
	u32 num_groups;
	u32 i;

	num_groups = mali_group_get_glob_num_groups();

	/* Do we have a virtual group? */
	for (i = 0; i < num_groups; i++) {
		struct mali_group *group = mali_group_get_glob_group(i);

		if (mali_group_is_virtual(group)) {
			virtual_group = group;
			virtual_group_state = EXEC_STATE_INACTIVE;
			break;
		}
	}

	/* Find all the available physical GP and PP cores */
	for (i = 0; i < num_groups; i++) {
		struct mali_group *group = mali_group_get_glob_group(i);

		if (NULL != group) {
			struct mali_pp_core *pp_core = mali_group_get_pp_core(group);
			struct mali_gp_core *gp_core = mali_group_get_gp_core(group);

			if (!mali_group_is_virtual(group)) {
				if (NULL != pp_core) {
					if (0 == pp_version) {
						/* Retrieve PP version from the first available PP core */
						pp_version = mali_pp_core_get_version(pp_core);
					}

					if (NULL != virtual_group) {
						mali_executor_lock();
						mali_group_add_group(virtual_group, group);
						mali_executor_unlock();
					} else {
						_mali_osk_list_add(&group->executor_list, &group_list_inactive);
						group_list_inactive_count++;
					}

					num_physical_pp_cores_total++;
				} else {
					MALI_DEBUG_ASSERT_POINTER(gp_core);

					if (0 == gp_version) {
						/* Retrieve GP version */
						gp_version = mali_gp_core_get_version(gp_core);
					}

					gp_group = group;
					gp_group_state = EXEC_STATE_INACTIVE;
				}

			}
		}
	}

	num_physical_pp_cores_enabled = num_physical_pp_cores_total;
}

void mali_executor_depopulate(void)
{
	struct mali_group *group;
	struct mali_group *temp;

	MALI_DEBUG_ASSERT(EXEC_STATE_WORKING != gp_group_state);

	if (NULL != gp_group) {
		mali_group_delete(gp_group);
		gp_group = NULL;
	}

	MALI_DEBUG_ASSERT(EXEC_STATE_WORKING != virtual_group_state);

	if (NULL != virtual_group) {
		mali_group_delete(virtual_group);
		virtual_group = NULL;
	}

	MALI_DEBUG_ASSERT(_mali_osk_list_empty(&group_list_working));

	_MALI_OSK_LIST_FOREACHENTRY(group, temp, &group_list_idle, struct mali_group, executor_list) {
		mali_group_delete(group);
	}

	_MALI_OSK_LIST_FOREACHENTRY(group, temp, &group_list_inactive, struct mali_group, executor_list) {
		mali_group_delete(group);
	}

	_MALI_OSK_LIST_FOREACHENTRY(group, temp, &group_list_disabled, struct mali_group, executor_list) {
		mali_group_delete(group);
	}
}

void mali_executor_suspend(void)
{
	mali_executor_lock();

	/* Increment the pause_count so that no more jobs will be scheduled */
	pause_count++;

	mali_executor_unlock();

	_mali_osk_wait_queue_wait_event(executor_working_wait_queue,
					mali_executor_is_suspended, NULL);

	/*
	 * mali_executor_complete_XX() leaves jobs in idle state.
	 * deactivate option is used when we are going to power down
	 * the entire GPU (OS suspend) and want a consistent SW vs HW
	 * state.
	 */
	mali_executor_lock();

	mali_executor_deactivate_list_idle(MALI_TRUE);

	/*
	 * The following steps are used to deactive all of activated
	 * (MALI_GROUP_STATE_ACTIVE) and activating (MALI_GROUP
	 * _STAET_ACTIVATION_PENDING) groups, to make sure the variable
	 * pd_mask_wanted is equal with 0. */
	if (MALI_GROUP_STATE_INACTIVE != mali_group_get_state(gp_group)) {
		gp_group_state = EXEC_STATE_INACTIVE;
		mali_group_deactivate(gp_group);
	}

	if (mali_executor_has_virtual_group()) {
		if (MALI_GROUP_STATE_INACTIVE
		    != mali_group_get_state(virtual_group)) {
			virtual_group_state = EXEC_STATE_INACTIVE;
			mali_group_deactivate(virtual_group);
		}
	}

	if (0 < group_list_inactive_count) {
		struct mali_group *group;
		struct mali_group *temp;

		_MALI_OSK_LIST_FOREACHENTRY(group, temp,
					    &group_list_inactive,
					    struct mali_group, executor_list) {
			if (MALI_GROUP_STATE_ACTIVATION_PENDING
			    == mali_group_get_state(group)) {
				mali_group_deactivate(group);
			}

			/*
			 * On mali-450 platform, we may have physical group in the group inactive
			 * list, and its state is MALI_GROUP_STATE_ACTIVATION_PENDING, so we only
			 * deactivate it is not enough, we still also need add it back to virtual group.
			 * And now, virtual group must be in INACTIVE state, so it's safe to add
			 * physical group to virtual group at this point.
			 */
			if (NULL != virtual_group) {
				_mali_osk_list_delinit(&group->executor_list);
				group_list_inactive_count--;

				mali_group_add_group(virtual_group, group);
			}
		}
	}

	mali_executor_unlock();
}

void mali_executor_resume(void)
{
	mali_executor_lock();

	/* Decrement pause_count to allow scheduling again (if it reaches 0) */
	pause_count--;
	if (0 == pause_count) {
		mali_executor_schedule();
	}

	mali_executor_unlock();
}

u32 mali_executor_get_num_cores_total(void)
{
	return num_physical_pp_cores_total;
}

u32 mali_executor_get_num_cores_enabled(void)
{
	return num_physical_pp_cores_enabled;
}

struct mali_pp_core *mali_executor_get_virtual_pp(void)
{
	MALI_DEBUG_ASSERT_POINTER(virtual_group);
	MALI_DEBUG_ASSERT_POINTER(virtual_group->pp_core);
	return virtual_group->pp_core;
}

struct mali_group *mali_executor_get_virtual_group(void)
{
	return virtual_group;
}

void mali_executor_zap_all_active(struct mali_session_data *session)
{
	struct mali_group *group;
	struct mali_group *temp;
	mali_bool ret;

	mali_executor_lock();

	/*
	 * This function is a bit complicated because
	 * mali_group_zap_session() can fail. This only happens because the
	 * group is in an unhandled page fault status.
	 * We need to make sure this page fault is handled before we return,
	 * so that we know every single outstanding MMU transactions have
	 * completed. This will allow caller to safely remove physical pages
	 * when we have returned.
	 */

	MALI_DEBUG_ASSERT(NULL != gp_group);
	ret = mali_group_zap_session(gp_group, session);
	if (MALI_FALSE == ret) {
		struct mali_gp_job *gp_job = NULL;

		mali_executor_complete_group(gp_group, MALI_FALSE,
					     MALI_TRUE, &gp_job, NULL);

		MALI_DEBUG_ASSERT_POINTER(gp_job);

		/* GP job completed, make sure it is freed */
		mali_scheduler_complete_gp_job(gp_job, MALI_FALSE,
					       MALI_TRUE, MALI_TRUE);
	}

	if (mali_executor_has_virtual_group()) {
		ret = mali_group_zap_session(virtual_group, session);
		if (MALI_FALSE == ret) {
			struct mali_pp_job *pp_job = NULL;

			mali_executor_complete_group(virtual_group, MALI_FALSE,
						     MALI_TRUE, NULL, &pp_job);

			if (NULL != pp_job) {
				/* PP job completed, make sure it is freed */
				mali_scheduler_complete_pp_job(pp_job, 0,
							       MALI_FALSE, MALI_TRUE);
			}
		}
	}

	_MALI_OSK_LIST_FOREACHENTRY(group, temp, &group_list_working,
				    struct mali_group, executor_list) {
		ret = mali_group_zap_session(group, session);
		if (MALI_FALSE == ret) {
			ret = mali_group_zap_session(group, session);
			if (MALI_FALSE == ret) {
				struct mali_pp_job *pp_job = NULL;

				mali_executor_complete_group(group, MALI_FALSE,
							     MALI_TRUE, NULL, &pp_job);

				if (NULL != pp_job) {
					/* PP job completed, free it */
					mali_scheduler_complete_pp_job(pp_job,
								       0, MALI_FALSE,
								       MALI_TRUE);
				}
			}
		}
	}

	mali_executor_unlock();
}

void mali_executor_schedule_from_mask(mali_scheduler_mask mask, mali_bool deferred_schedule)
{
	if (MALI_SCHEDULER_MASK_EMPTY != mask) {
		if (MALI_TRUE == deferred_schedule) {
			_mali_osk_wq_schedule_work_high_pri(executor_wq_high_pri);
		} else {
			/* Schedule from this thread*/
			mali_executor_lock();
			mali_executor_schedule();
			mali_executor_unlock();
		}
	}
}

_mali_osk_errcode_t mali_executor_interrupt_gp(struct mali_group *group,
		mali_bool in_upper_half)
{
	enum mali_interrupt_result int_result;
	mali_bool time_out = MALI_FALSE;

	MALI_DEBUG_PRINT(4, ("Executor: GP interrupt from %s in %s half\n",
			     mali_group_core_description(group),
			     in_upper_half ? "upper" : "bottom"));

	mali_executor_lock();
	if (!mali_group_is_working(group)) {
		/* Not working, so nothing to do */
		mali_executor_unlock();
		return _MALI_OSK_ERR_FAULT;
	}

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT(mali_group_is_working(group));

	if (mali_group_has_timed_out(group)) {
		int_result = MALI_INTERRUPT_RESULT_ERROR;
		time_out = MALI_TRUE;
		MALI_PRINT(("Executor GP: Job %d Timeout on %s\n",
			    mali_gp_job_get_id(group->gp_running_job),
			    mali_group_core_description(group)));
	} else {
		int_result = mali_group_get_interrupt_result_gp(group);
		if (MALI_INTERRUPT_RESULT_NONE == int_result) {
			mali_executor_unlock();
			return _MALI_OSK_ERR_FAULT;
		}
	}

#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	if (MALI_INTERRUPT_RESULT_NONE == int_result) {
		/* No interrupts signalled, so nothing to do */
		mali_executor_unlock();
		return _MALI_OSK_ERR_FAULT;
	}
#else
	MALI_DEBUG_ASSERT(MALI_INTERRUPT_RESULT_NONE != int_result);
#endif

	mali_group_mask_all_interrupts_gp(group);

	if (MALI_INTERRUPT_RESULT_SUCCESS_VS == int_result) {
		if (mali_group_gp_is_active(group)) {
			/* Only VS completed so far, while PLBU is still active */

			/* Enable all but the current interrupt */
			mali_group_enable_interrupts_gp(group, int_result);

			mali_executor_unlock();
			return _MALI_OSK_ERR_OK;
		}
	} else if (MALI_INTERRUPT_RESULT_SUCCESS_PLBU == int_result) {
		if (mali_group_gp_is_active(group)) {
			/* Only PLBU completed so far, while VS is still active */

			/* Enable all but the current interrupt */
			mali_group_enable_interrupts_gp(group, int_result);

			mali_executor_unlock();
			return _MALI_OSK_ERR_OK;
		}
	} else if (MALI_INTERRUPT_RESULT_OOM == int_result) {
		struct mali_gp_job *job = mali_group_get_running_gp_job(group);

		/* PLBU out of mem */
		MALI_DEBUG_PRINT(3, ("Executor: PLBU needs more heap memory\n"));

#if defined(CONFIG_MALI400_PROFILING)
		/* Give group a chance to generate a SUSPEND event */
		mali_group_oom(group);
#endif

		/*
		 * no need to hold interrupt raised while
		 * waiting for more memory.
		 */
		mali_executor_send_gp_oom_to_user(job);

		mali_executor_unlock();

		return _MALI_OSK_ERR_OK;
	}

	/* We should now have a real interrupt to handle */

	MALI_DEBUG_PRINT(4, ("Executor: Group %s completed with %s\n",
			     mali_group_core_description(group),
			     (MALI_INTERRUPT_RESULT_ERROR == int_result) ?
			     "ERROR" : "success"));

	if (in_upper_half && MALI_INTERRUPT_RESULT_ERROR == int_result) {
		/* Don't bother to do processing of errors in upper half */
		mali_executor_unlock();

		if (MALI_FALSE == time_out) {
			mali_group_schedule_bottom_half_gp(group);
		}
	} else {
		struct mali_gp_job *job;
		mali_bool success;

		success = (int_result != MALI_INTERRUPT_RESULT_ERROR) ?
			  MALI_TRUE : MALI_FALSE;

		mali_executor_complete_group(group, success,
					     MALI_TRUE, &job, NULL);

		mali_executor_unlock();

		/* GP jobs always fully complete */
		MALI_DEBUG_ASSERT(NULL != job);

		/* This will notify user space and close the job object */
		mali_scheduler_complete_gp_job(job, success,
					       MALI_TRUE, MALI_TRUE);
	}

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t mali_executor_interrupt_pp(struct mali_group *group,
		mali_bool in_upper_half)
{
	enum mali_interrupt_result int_result;
	mali_bool time_out = MALI_FALSE;

	MALI_DEBUG_PRINT(4, ("Executor: PP interrupt from %s in %s half\n",
			     mali_group_core_description(group),
			     in_upper_half ? "upper" : "bottom"));

	mali_executor_lock();

	if (!mali_group_is_working(group)) {
		/* Not working, so nothing to do */
		mali_executor_unlock();
		return _MALI_OSK_ERR_FAULT;
	}

	if (in_upper_half) {
		if (mali_group_is_in_virtual(group)) {
			/* Child groups should never handle PP interrupts */
			MALI_DEBUG_ASSERT(!mali_group_has_timed_out(group));
			mali_executor_unlock();
			return _MALI_OSK_ERR_FAULT;
		}
	}
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT(mali_group_is_working(group));
	MALI_DEBUG_ASSERT(!mali_group_is_in_virtual(group));

	if (mali_group_has_timed_out(group)) {
		int_result = MALI_INTERRUPT_RESULT_ERROR;
		time_out = MALI_TRUE;
		MALI_PRINT(("Executor PP: Job %d Timeout on %s\n",
			    mali_pp_job_get_id(group->pp_running_job),
			    mali_group_core_description(group)));
	} else {
		int_result = mali_group_get_interrupt_result_pp(group);
		if (MALI_INTERRUPT_RESULT_NONE == int_result) {
			mali_executor_unlock();
			return _MALI_OSK_ERR_FAULT;
		}
	}

#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	if (MALI_INTERRUPT_RESULT_NONE == int_result) {
		/* No interrupts signalled, so nothing to do */
		mali_executor_unlock();
		return _MALI_OSK_ERR_FAULT;
	} else if (MALI_INTERRUPT_RESULT_SUCCESS == int_result) {
		if (mali_group_is_virtual(group) && mali_group_pp_is_active(group)) {
			/* Some child groups are still working, so nothing to do right now */
			mali_executor_unlock();
			return _MALI_OSK_ERR_FAULT;
		}
	}
#else
	MALI_DEBUG_ASSERT(MALI_INTERRUPT_RESULT_NONE != int_result);
	if (!mali_group_has_timed_out(group)) {
		MALI_DEBUG_ASSERT(!mali_group_pp_is_active(group));
	}
#endif

	/* We should now have a real interrupt to handle */

	MALI_DEBUG_PRINT(4, ("Executor: Group %s completed with %s\n",
			     mali_group_core_description(group),
			     (MALI_INTERRUPT_RESULT_ERROR == int_result) ?
			     "ERROR" : "success"));

	if (in_upper_half && MALI_INTERRUPT_RESULT_ERROR == int_result) {
		/* Don't bother to do processing of errors in upper half */
		mali_group_mask_all_interrupts_pp(group);
		mali_executor_unlock();

		if (MALI_FALSE == time_out) {
			mali_group_schedule_bottom_half_pp(group);
		}
	} else {
		struct mali_pp_job *job = NULL;
		mali_bool success;

		success = (int_result == MALI_INTERRUPT_RESULT_SUCCESS) ?
			  MALI_TRUE : MALI_FALSE;

		mali_executor_complete_group(group, success,
					     MALI_TRUE, NULL, &job);

		mali_executor_unlock();

		if (NULL != job) {
			/* Notify user space and close the job object */
			mali_scheduler_complete_pp_job(job,
						       num_physical_pp_cores_total,
						       MALI_TRUE, MALI_TRUE);
		}
	}

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t mali_executor_interrupt_mmu(struct mali_group *group,
		mali_bool in_upper_half)
{
	enum mali_interrupt_result int_result;

	MALI_DEBUG_PRINT(4, ("Executor: MMU interrupt from %s in %s half\n",
			     mali_group_core_description(group),
			     in_upper_half ? "upper" : "bottom"));

	mali_executor_lock();
	if (!mali_group_is_working(group)) {
		/* Not working, so nothing to do */
		mali_executor_unlock();
		return _MALI_OSK_ERR_FAULT;
	}

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT(mali_group_is_working(group));

	int_result = mali_group_get_interrupt_result_mmu(group);
	if (MALI_INTERRUPT_RESULT_NONE == int_result) {
		mali_executor_unlock();
		return _MALI_OSK_ERR_FAULT;
	}

#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	if (MALI_INTERRUPT_RESULT_NONE == int_result) {
		/* No interrupts signalled, so nothing to do */
		mali_executor_unlock();
		return _MALI_OSK_ERR_FAULT;
	}
#else
	MALI_DEBUG_ASSERT(MALI_INTERRUPT_RESULT_ERROR == int_result);
#endif

	/* We should now have a real interrupt to handle */

	if (in_upper_half) {
		/* Don't bother to do processing of errors in upper half */

		struct mali_group *parent = group->parent_group;

		mali_mmu_mask_all_interrupts(group->mmu);

		mali_executor_unlock();

		if (NULL == parent) {
			mali_group_schedule_bottom_half_mmu(group);
		} else {
			mali_group_schedule_bottom_half_mmu(parent);
		}

	} else {
		struct mali_gp_job *gp_job = NULL;
		struct mali_pp_job *pp_job = NULL;

#ifdef DEBUG

		u32 fault_address = mali_mmu_get_page_fault_addr(group->mmu);
		u32 status = mali_mmu_get_status(group->mmu);
		MALI_DEBUG_PRINT(2, ("Executor: Mali page fault detected at 0x%x from bus id %d of type %s on %s\n",
				     (void *)(uintptr_t)fault_address,
				     (status >> 6) & 0x1F,
				     (status & 32) ? "write" : "read",
				     group->mmu->hw_core.description));
		MALI_DEBUG_PRINT(3, ("Executor: MMU rawstat = 0x%08X, MMU status = 0x%08X\n",
				     mali_mmu_get_rawstat(group->mmu), status));
#endif

		mali_executor_complete_group(group, MALI_FALSE,
					     MALI_TRUE, &gp_job, &pp_job);

		mali_executor_unlock();

		if (NULL != gp_job) {
			MALI_DEBUG_ASSERT(NULL == pp_job);

			/* Notify user space and close the job object */
			mali_scheduler_complete_gp_job(gp_job, MALI_FALSE,
						       MALI_TRUE, MALI_TRUE);
		} else if (NULL != pp_job) {
			MALI_DEBUG_ASSERT(NULL == gp_job);

			/* Notify user space and close the job object */
			mali_scheduler_complete_pp_job(pp_job,
						       num_physical_pp_cores_total,
						       MALI_TRUE, MALI_TRUE);
		}
	}

	return _MALI_OSK_ERR_OK;
}

void mali_executor_group_power_up(struct mali_group *groups[], u32 num_groups)
{
	u32 i;
	mali_bool child_groups_activated = MALI_FALSE;
	mali_bool do_schedule = MALI_FALSE;
#if defined(DEBUG)
	u32 num_activated = 0;
#endif

	MALI_DEBUG_ASSERT_POINTER(groups);
	MALI_DEBUG_ASSERT(0 < num_groups);

	mali_executor_lock();

	MALI_DEBUG_PRINT(3, ("Executor: powering up %u groups\n", num_groups));

	for (i = 0; i < num_groups; i++) {
		MALI_DEBUG_PRINT(3, ("Executor: powering up group %s\n",
				     mali_group_core_description(groups[i])));

		mali_group_power_up(groups[i]);

		if ((MALI_GROUP_STATE_ACTIVATION_PENDING != mali_group_get_state(groups[i]) ||
		     (MALI_TRUE != mali_executor_group_is_in_state(groups[i], EXEC_STATE_INACTIVE)))) {
			/* nothing more to do for this group */
			continue;
		}

		MALI_DEBUG_PRINT(3, ("Executor: activating group %s\n",
				     mali_group_core_description(groups[i])));

#if defined(DEBUG)
		num_activated++;
#endif

		if (mali_group_is_in_virtual(groups[i])) {
			/*
			 * At least one child group of virtual group is powered on.
			 */
			child_groups_activated = MALI_TRUE;
		} else if (MALI_FALSE == mali_group_is_virtual(groups[i])) {
			/* Set gp and pp not in virtual to active. */
			mali_group_set_active(groups[i]);
		}

		/* Move group from inactive to idle list */
		if (groups[i] == gp_group) {
			MALI_DEBUG_ASSERT(EXEC_STATE_INACTIVE ==
					  gp_group_state);
			gp_group_state = EXEC_STATE_IDLE;
		} else if (MALI_FALSE == mali_group_is_in_virtual(groups[i])
			   && MALI_FALSE == mali_group_is_virtual(groups[i])) {
			MALI_DEBUG_ASSERT(MALI_TRUE == mali_executor_group_is_in_state(groups[i],
					  EXEC_STATE_INACTIVE));

			mali_executor_change_state_pp_physical(groups[i],
							       &group_list_inactive,
							       &group_list_inactive_count,
							       &group_list_idle,
							       &group_list_idle_count);
		}

		do_schedule = MALI_TRUE;
	}

	if (mali_executor_has_virtual_group() &&
	    MALI_TRUE == child_groups_activated &&
	    MALI_GROUP_STATE_ACTIVATION_PENDING ==
	    mali_group_get_state(virtual_group)) {
		/*
		 * Try to active virtual group while it may be not sucessful every time,
		 * because there is one situation that not all of child groups are powered on
		 * in one time and virtual group is in activation pending state.
		 */
		if (mali_group_set_active(virtual_group)) {
			/* Move group from inactive to idle */
			MALI_DEBUG_ASSERT(EXEC_STATE_INACTIVE ==
					  virtual_group_state);
			virtual_group_state = EXEC_STATE_IDLE;

			MALI_DEBUG_PRINT(3, ("Executor: powering up %u groups completed, %u  physical activated, 1 virtual activated.\n", num_groups, num_activated));
		} else {
			MALI_DEBUG_PRINT(3, ("Executor: powering up %u groups completed, %u physical activated\n", num_groups, num_activated));
		}
	} else {
		MALI_DEBUG_PRINT(3, ("Executor: powering up %u groups completed, %u physical activated\n", num_groups, num_activated));
	}

	if (MALI_TRUE == do_schedule) {
		/* Trigger a schedule */
		mali_executor_schedule();
	}

	mali_executor_unlock();
}

void mali_executor_group_power_down(struct mali_group *groups[],
				    u32 num_groups)
{
	u32 i;

	MALI_DEBUG_ASSERT_POINTER(groups);
	MALI_DEBUG_ASSERT(0 < num_groups);

	mali_executor_lock();

	MALI_DEBUG_PRINT(3, ("Executor: powering down %u groups\n", num_groups));

	for (i = 0; i < num_groups; i++) {
		/* Groups must be either disabled or inactive */
		MALI_DEBUG_ASSERT(mali_executor_group_is_in_state(groups[i],
				  EXEC_STATE_DISABLED) ||
				  mali_executor_group_is_in_state(groups[i],
						  EXEC_STATE_INACTIVE));

		MALI_DEBUG_PRINT(3, ("Executor: powering down group %s\n",
				     mali_group_core_description(groups[i])));

		mali_group_power_down(groups[i]);
	}

	MALI_DEBUG_PRINT(3, ("Executor: powering down %u groups completed\n", num_groups));

	mali_executor_unlock();
}

void mali_executor_abort_session(struct mali_session_data *session)
{
	struct mali_group *group;
	struct mali_group *tmp_group;

	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_ASSERT(session->is_aborting);

	MALI_DEBUG_PRINT(3,
			 ("Executor: Aborting all jobs from session 0x%08X.\n",
			  session));

	mali_executor_lock();

	if (mali_group_get_session(gp_group) == session) {
		if (EXEC_STATE_WORKING == gp_group_state) {
			struct mali_gp_job *gp_job = NULL;

			mali_executor_complete_group(gp_group, MALI_FALSE,
						     MALI_TRUE, &gp_job, NULL);

			MALI_DEBUG_ASSERT_POINTER(gp_job);

			/* GP job completed, make sure it is freed */
			mali_scheduler_complete_gp_job(gp_job, MALI_FALSE,
						       MALI_FALSE, MALI_TRUE);
		} else {
			/* Same session, but not working, so just clear it */
			mali_group_clear_session(gp_group);
		}
	}

	if (mali_executor_has_virtual_group()) {
		if (EXEC_STATE_WORKING == virtual_group_state
		    && mali_group_get_session(virtual_group) == session) {
			struct mali_pp_job *pp_job = NULL;

			mali_executor_complete_group(virtual_group, MALI_FALSE,
						     MALI_FALSE, NULL, &pp_job);

			if (NULL != pp_job) {
				/* PP job completed, make sure it is freed */
				mali_scheduler_complete_pp_job(pp_job, 0,
							       MALI_FALSE, MALI_TRUE);
			}
		}
	}

	_MALI_OSK_LIST_FOREACHENTRY(group, tmp_group, &group_list_working,
				    struct mali_group, executor_list) {
		if (mali_group_get_session(group) == session) {
			struct mali_pp_job *pp_job = NULL;

			mali_executor_complete_group(group, MALI_FALSE,
						     MALI_FALSE, NULL, &pp_job);

			if (NULL != pp_job) {
				/* PP job completed, make sure it is freed */
				mali_scheduler_complete_pp_job(pp_job, 0,
							       MALI_FALSE, MALI_TRUE);
			}
		}
	}

	_MALI_OSK_LIST_FOREACHENTRY(group, tmp_group, &group_list_idle, struct mali_group, executor_list) {
		mali_group_clear_session(group);
	}

	_MALI_OSK_LIST_FOREACHENTRY(group, tmp_group, &group_list_inactive, struct mali_group, executor_list) {
		mali_group_clear_session(group);
	}

	_MALI_OSK_LIST_FOREACHENTRY(group, tmp_group, &group_list_disabled, struct mali_group, executor_list) {
		mali_group_clear_session(group);
	}

	mali_executor_unlock();
}


void mali_executor_core_scaling_enable(void)
{
	/* PS: Core scaling is by default enabled */
	core_scaling_enabled = MALI_TRUE;
}

void mali_executor_core_scaling_disable(void)
{
	core_scaling_enabled = MALI_FALSE;
}

mali_bool mali_executor_core_scaling_is_enabled(void)
{
	return core_scaling_enabled;
}

void mali_executor_group_enable(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);

	mali_executor_lock();

	if ((NULL != mali_group_get_gp_core(group) || NULL != mali_group_get_pp_core(group))
	    && (mali_executor_group_is_in_state(group, EXEC_STATE_DISABLED))) {
		mali_executor_group_enable_internal(group);
	}

	mali_executor_schedule();
	mali_executor_unlock();

	_mali_osk_wq_schedule_work(executor_wq_notify_core_change);
}

/*
 * If a physical group is inactive or idle, we should disable it immediately,
 * if group is in virtual, and virtual group is idle, disable given physical group in it.
 */
void mali_executor_group_disable(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);

	mali_executor_lock();

	if ((NULL != mali_group_get_gp_core(group) || NULL != mali_group_get_pp_core(group))
	    && (!mali_executor_group_is_in_state(group, EXEC_STATE_DISABLED))) {
		mali_executor_group_disable_internal(group);
	}

	mali_executor_schedule();
	mali_executor_unlock();

	_mali_osk_wq_schedule_work(executor_wq_notify_core_change);
}

mali_bool mali_executor_group_is_disabled(struct mali_group *group)
{
	/* NB: This function is not optimized for time critical usage */

	mali_bool ret;

	MALI_DEBUG_ASSERT_POINTER(group);

	mali_executor_lock();
	ret = mali_executor_group_is_in_state(group, EXEC_STATE_DISABLED);
	mali_executor_unlock();

	return ret;
}

int mali_executor_set_perf_level(unsigned int target_core_nr, mali_bool override)
{
	if (target_core_nr == num_physical_pp_cores_enabled) return 0;
	if (MALI_FALSE == core_scaling_enabled && MALI_FALSE == override) return -EPERM;
	if (target_core_nr > num_physical_pp_cores_total) return -EINVAL;
	if (0 == target_core_nr) return -EINVAL;

	mali_executor_core_scale(target_core_nr);

	_mali_osk_wq_schedule_work(executor_wq_notify_core_change);

	return 0;
}

#if MALI_STATE_TRACKING
u32 mali_executor_dump_state(char *buf, u32 size)
{
	int n = 0;
	struct mali_group *group;
	struct mali_group *temp;

	mali_executor_lock();

	switch (gp_group_state) {
	case EXEC_STATE_INACTIVE:
		n += _mali_osk_snprintf(buf + n, size - n,
					"GP group is in state INACTIVE\n");
		break;
	case EXEC_STATE_IDLE:
		n += _mali_osk_snprintf(buf + n, size - n,
					"GP group is in state IDLE\n");
		break;
	case EXEC_STATE_WORKING:
		n += _mali_osk_snprintf(buf + n, size - n,
					"GP group is in state WORKING\n");
		break;
	default:
		n += _mali_osk_snprintf(buf + n, size - n,
					"GP group is in unknown/illegal state %u\n",
					gp_group_state);
		break;
	}

	n += mali_group_dump_state(gp_group, buf + n, size - n);

	n += _mali_osk_snprintf(buf + n, size - n,
				"Physical PP groups in WORKING state (count = %u):\n",
				group_list_working_count);

	_MALI_OSK_LIST_FOREACHENTRY(group, temp, &group_list_working, struct mali_group, executor_list) {
		n += mali_group_dump_state(group, buf + n, size - n);
	}

	n += _mali_osk_snprintf(buf + n, size - n,
				"Physical PP groups in IDLE state (count = %u):\n",
				group_list_idle_count);

	_MALI_OSK_LIST_FOREACHENTRY(group, temp, &group_list_idle, struct mali_group, executor_list) {
		n += mali_group_dump_state(group, buf + n, size - n);
	}

	n += _mali_osk_snprintf(buf + n, size - n,
				"Physical PP groups in INACTIVE state (count = %u):\n",
				group_list_inactive_count);

	_MALI_OSK_LIST_FOREACHENTRY(group, temp, &group_list_inactive, struct mali_group, executor_list) {
		n += mali_group_dump_state(group, buf + n, size - n);
	}

	n += _mali_osk_snprintf(buf + n, size - n,
				"Physical PP groups in DISABLED state (count = %u):\n",
				group_list_disabled_count);

	_MALI_OSK_LIST_FOREACHENTRY(group, temp, &group_list_disabled, struct mali_group, executor_list) {
		n += mali_group_dump_state(group, buf + n, size - n);
	}

	if (mali_executor_has_virtual_group()) {
		switch (virtual_group_state) {
		case EXEC_STATE_EMPTY:
			n += _mali_osk_snprintf(buf + n, size - n,
						"Virtual PP group is in state EMPTY\n");
			break;
		case EXEC_STATE_INACTIVE:
			n += _mali_osk_snprintf(buf + n, size - n,
						"Virtual PP group is in state INACTIVE\n");
			break;
		case EXEC_STATE_IDLE:
			n += _mali_osk_snprintf(buf + n, size - n,
						"Virtual PP group is in state IDLE\n");
			break;
		case EXEC_STATE_WORKING:
			n += _mali_osk_snprintf(buf + n, size - n,
						"Virtual PP group is in state WORKING\n");
			break;
		default:
			n += _mali_osk_snprintf(buf + n, size - n,
						"Virtual PP group is in unknown/illegal state %u\n",
						virtual_group_state);
			break;
		}

		n += mali_group_dump_state(virtual_group, buf + n, size - n);
	}

	mali_executor_unlock();

	n += _mali_osk_snprintf(buf + n, size - n, "\n");

	return n;
}
#endif

_mali_osk_errcode_t _mali_ukk_get_pp_number_of_cores(_mali_uk_get_pp_number_of_cores_s *args)
{
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT(NULL != (void *)(uintptr_t)args->ctx);
	args->number_of_total_cores = num_physical_pp_cores_total;
	args->number_of_enabled_cores = num_physical_pp_cores_enabled;
	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_get_pp_core_version(_mali_uk_get_pp_core_version_s *args)
{
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT(NULL != (void *)(uintptr_t)args->ctx);
	args->version = pp_version;
	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_get_gp_number_of_cores(_mali_uk_get_gp_number_of_cores_s *args)
{
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT(NULL != (void *)(uintptr_t)args->ctx);
	args->number_of_cores = 1;
	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_get_gp_core_version(_mali_uk_get_gp_core_version_s *args)
{
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT(NULL != (void *)(uintptr_t)args->ctx);
	args->version = gp_version;
	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_gp_suspend_response(_mali_uk_gp_suspend_response_s *args)
{
	struct mali_session_data *session;
	struct mali_gp_job *job;

	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT(NULL != (void *)(uintptr_t)args->ctx);

	session = (struct mali_session_data *)(uintptr_t)args->ctx;

	if (_MALIGP_JOB_RESUME_WITH_NEW_HEAP == args->code) {
		_mali_osk_notification_t *new_notification = NULL;

		new_notification = _mali_osk_notification_create(
					   _MALI_NOTIFICATION_GP_STALLED,
					   sizeof(_mali_uk_gp_job_suspended_s));

		if (NULL != new_notification) {
			MALI_DEBUG_PRINT(3, ("Executor: Resuming job %u with new heap; 0x%08X - 0x%08X\n",
					     args->cookie, args->arguments[0], args->arguments[1]));

			mali_executor_lock();

			/* Resume the job in question if it is still running */
			job = mali_group_get_running_gp_job(gp_group);
			if (NULL != job &&
			    args->cookie == mali_gp_job_get_id(job) &&
			    session == mali_gp_job_get_session(job)) {
				/*
				 * Correct job is running, resume with new heap
				 */

				mali_gp_job_set_oom_notification(job,
								 new_notification);

				/* This will also re-enable interrupts */
				mali_group_resume_gp_with_new_heap(gp_group,
								   args->cookie,
								   args->arguments[0],
								   args->arguments[1]);

				mali_executor_unlock();
				return _MALI_OSK_ERR_OK;
			} else {
				MALI_PRINT_ERROR(("Executor: Unable to resume, GP job no longer running.\n"));

				_mali_osk_notification_delete(new_notification);

				mali_executor_unlock();
				return _MALI_OSK_ERR_FAULT;
			}
		} else {
			MALI_PRINT_ERROR(("Executor: Failed to allocate notification object. Will abort GP job.\n"));
		}
	} else {
		MALI_DEBUG_PRINT(2, ("Executor: Aborting job %u, no new heap provided\n", args->cookie));
	}

	mali_executor_lock();

	/* Abort the job in question if it is still running */
	job = mali_group_get_running_gp_job(gp_group);
	if (NULL != job &&
	    args->cookie == mali_gp_job_get_id(job) &&
	    session == mali_gp_job_get_session(job)) {
		/* Correct job is still running */
		struct mali_gp_job *job_done = NULL;

		mali_executor_complete_group(gp_group, MALI_FALSE,
					     MALI_TRUE, &job_done, NULL);

		/* The same job should have completed */
		MALI_DEBUG_ASSERT(job_done == job);

		/* GP job completed, make sure it is freed */
		mali_scheduler_complete_gp_job(job_done, MALI_FALSE,
					       MALI_TRUE, MALI_TRUE);
	}

	mali_executor_unlock();
	return _MALI_OSK_ERR_FAULT;
}


/*
 * ---------- Implementation of static functions ----------
 */

static void mali_executor_lock(void)
{
	_mali_osk_spinlock_irq_lock(mali_executor_lock_obj);
	MALI_DEBUG_PRINT(5, ("Executor: lock taken\n"));
}

static void mali_executor_unlock(void)
{
	MALI_DEBUG_PRINT(5, ("Executor: Releasing lock\n"));
	_mali_osk_spinlock_irq_unlock(mali_executor_lock_obj);
}

static mali_bool mali_executor_is_suspended(void *data)
{
	mali_bool ret;

	/* This callback does not use the data pointer. */
	MALI_IGNORE(data);

	mali_executor_lock();

	ret = pause_count > 0 && !mali_executor_is_working();

	mali_executor_unlock();

	return ret;
}

static mali_bool mali_executor_is_working()
{
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	return (0 != group_list_working_count ||
		EXEC_STATE_WORKING == gp_group_state ||
		EXEC_STATE_WORKING == virtual_group_state);
}

static void mali_executor_disable_empty_virtual(void)
{
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT(virtual_group_state != EXEC_STATE_EMPTY);
	MALI_DEBUG_ASSERT(virtual_group_state != EXEC_STATE_WORKING);

	if (mali_group_is_empty(virtual_group)) {
		virtual_group_state = EXEC_STATE_EMPTY;
	}
}

static mali_bool mali_executor_physical_rejoin_virtual(struct mali_group *group)
{
	mali_bool trigger_pm_update = MALI_FALSE;

	MALI_DEBUG_ASSERT_POINTER(group);
	/* Only rejoining after job has completed (still active) */
	MALI_DEBUG_ASSERT(MALI_GROUP_STATE_ACTIVE ==
			  mali_group_get_state(group));
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT(MALI_TRUE == mali_executor_has_virtual_group());
	MALI_DEBUG_ASSERT(MALI_FALSE == mali_group_is_virtual(group));

	/* Make sure group and virtual group have same status */

	if (MALI_GROUP_STATE_INACTIVE == mali_group_get_state(virtual_group)) {
		if (mali_group_deactivate(group)) {
			trigger_pm_update = MALI_TRUE;
		}

		if (virtual_group_state == EXEC_STATE_EMPTY) {
			virtual_group_state = EXEC_STATE_INACTIVE;
		}
	} else if (MALI_GROUP_STATE_ACTIVATION_PENDING ==
		   mali_group_get_state(virtual_group)) {
		/*
		 * Activation is pending for virtual group, leave
		 * this child group as active.
		 */
		if (virtual_group_state == EXEC_STATE_EMPTY) {
			virtual_group_state = EXEC_STATE_INACTIVE;
		}
	} else {
		MALI_DEBUG_ASSERT(MALI_GROUP_STATE_ACTIVE ==
				  mali_group_get_state(virtual_group));

		if (virtual_group_state == EXEC_STATE_EMPTY) {
			virtual_group_state = EXEC_STATE_IDLE;
		}
	}

	/* Remove group from idle list */
	MALI_DEBUG_ASSERT(mali_executor_group_is_in_state(group,
			  EXEC_STATE_IDLE));
	_mali_osk_list_delinit(&group->executor_list);
	group_list_idle_count--;

	/*
	 * And finally rejoin the virtual group
	 * group will start working on same job as virtual_group,
	 * if virtual_group is working on a job
	 */
	mali_group_add_group(virtual_group, group);

	return trigger_pm_update;
}

static mali_bool mali_executor_has_virtual_group(void)
{
#if defined(CONFIG_MALI450)
	return (NULL != virtual_group) ? MALI_TRUE : MALI_FALSE;
#else
	return MALI_FALSE;
#endif /* defined(CONFIG_MALI450) */
}

static mali_bool mali_executor_virtual_group_is_usable(void)
{
#if defined(CONFIG_MALI450)
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	return (EXEC_STATE_INACTIVE == virtual_group_state ||
		EXEC_STATE_IDLE == virtual_group_state) ?
	       MALI_TRUE : MALI_FALSE;
#else
	return MALI_FALSE;
#endif /* defined(CONFIG_MALI450) */
}

static mali_bool mali_executor_tackle_gp_bound(void)
{
	struct mali_pp_job *job;

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	job = mali_scheduler_job_pp_physical_peek();

	if (NULL != job && MALI_TRUE == mali_is_mali400()) {
		if (0 < group_list_working_count &&
		    mali_pp_job_is_large_and_unstarted(job)) {
			return MALI_TRUE;
		}
	}

	return MALI_FALSE;
}

/*
 * This is where jobs are actually started.
 */
static void mali_executor_schedule(void)
{
	u32 i;
	u32 num_physical_needed = 0;
	u32 num_physical_to_process = 0;
	mali_bool trigger_pm_update = MALI_FALSE;
	mali_bool deactivate_idle_group = MALI_TRUE;

	/* Physical groups + jobs to start in this function */
	struct mali_group *groups_to_start[MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS];
	struct mali_pp_job *jobs_to_start[MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS];
	u32 sub_jobs_to_start[MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS];
	int num_jobs_to_start = 0;

	/* Virtual job to start in this function */
	struct mali_pp_job *virtual_job_to_start = NULL;

	/* GP job to start in this function */
	struct mali_gp_job *gp_job_to_start = NULL;

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	if (pause_count > 0) {
		/* Execution is suspended, don't schedule any jobs. */
		return;
	}

	/* Lock needed in order to safely handle the job queues */
	mali_scheduler_lock();

	/* 1. Activate gp firstly if have gp job queued. */
	if (EXEC_STATE_INACTIVE == gp_group_state &&
	    0 < mali_scheduler_job_gp_count()) {

		enum mali_group_state state =
			mali_group_activate(gp_group);
		if (MALI_GROUP_STATE_ACTIVE == state) {
			/* Set GP group state to idle */
			gp_group_state = EXEC_STATE_IDLE;
		} else {
			trigger_pm_update = MALI_TRUE;
		}
	}

	/* 2. Prepare as many physical groups as needed/possible */

	num_physical_needed = mali_scheduler_job_physical_head_count();

	/* On mali-450 platform, we don't need to enter in this block frequently. */
	if (0 < num_physical_needed) {

		if (num_physical_needed <= group_list_idle_count) {
			/* We have enough groups on idle list already */
			num_physical_to_process = num_physical_needed;
			num_physical_needed = 0;
		} else {
			/* We need to get a hold of some more groups */
			num_physical_to_process = group_list_idle_count;
			num_physical_needed -= group_list_idle_count;
		}

		if (0 < num_physical_needed) {

			/* 2.1. Activate groups which are inactive */

			struct mali_group *group;
			struct mali_group *temp;

			_MALI_OSK_LIST_FOREACHENTRY(group, temp, &group_list_inactive,
						    struct mali_group, executor_list) {
				enum mali_group_state state =
					mali_group_activate(group);
				if (MALI_GROUP_STATE_ACTIVE == state) {
					/* Move from inactive to idle */
					mali_executor_change_state_pp_physical(group,
									       &group_list_inactive,
									       &group_list_inactive_count,
									       &group_list_idle,
									       &group_list_idle_count);
					num_physical_to_process++;
				} else {
					trigger_pm_update = MALI_TRUE;
				}

				num_physical_needed--;
				if (0 == num_physical_needed) {
					/* We have activated all the groups we need */
					break;
				}
			}
		}

		if (mali_executor_virtual_group_is_usable()) {

			/*
			 * 2.2. And finally, steal and activate groups
			 * from virtual group if we need even more
			 */
			while (0 < num_physical_needed) {
				struct mali_group *group;

				group = mali_group_acquire_group(virtual_group);
				if (NULL != group) {
					enum mali_group_state state;

					mali_executor_disable_empty_virtual();

					state = mali_group_activate(group);
					if (MALI_GROUP_STATE_ACTIVE == state) {
						/* Group is ready, add to idle list */
						_mali_osk_list_add(
							&group->executor_list,
							&group_list_idle);
						group_list_idle_count++;
						num_physical_to_process++;
					} else {
						/*
						 * Group is not ready yet,
						 * add to inactive list
						 */
						_mali_osk_list_add(
							&group->executor_list,
							&group_list_inactive);
						group_list_inactive_count++;

						trigger_pm_update = MALI_TRUE;
					}
					num_physical_needed--;
				} else {
					/*
					 * We could not get enough groups
					 * from the virtual group.
					 */
					break;
				}
			}
		}

		/* 2.3. Assign physical jobs to groups */

		if (0 < num_physical_to_process) {
			struct mali_group *group;
			struct mali_group *temp;

			_MALI_OSK_LIST_FOREACHENTRY(group, temp, &group_list_idle,
						    struct mali_group, executor_list) {
				struct mali_pp_job *job = NULL;
				u32 sub_job = MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS;

				MALI_DEBUG_ASSERT(num_jobs_to_start <
						  MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS);

				MALI_DEBUG_ASSERT(0 <
						  mali_scheduler_job_physical_head_count());

				if (mali_executor_hint_is_enabled(
					    MALI_EXECUTOR_HINT_GP_BOUND)) {
					if (MALI_TRUE == mali_executor_tackle_gp_bound()) {
						/*
						* We're gp bound,
						* don't start this right now.
						*/
						deactivate_idle_group = MALI_FALSE;
						num_physical_to_process = 0;
						break;
					}
				}

				job = mali_scheduler_job_pp_physical_get(
					      &sub_job);

				MALI_DEBUG_ASSERT_POINTER(job);
				MALI_DEBUG_ASSERT(sub_job <= MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS);
				
				/* Put job + group on list of jobs to start later on */

				groups_to_start[num_jobs_to_start] = group;
				jobs_to_start[num_jobs_to_start] = job;
				sub_jobs_to_start[num_jobs_to_start] = sub_job;
				num_jobs_to_start++;

				/* Move group from idle to working */
				mali_executor_change_state_pp_physical(group,
								       &group_list_idle,
								       &group_list_idle_count,
								       &group_list_working,
								       &group_list_working_count);

				num_physical_to_process--;
				if (0 == num_physical_to_process) {
					/* Got all we needed */
					break;
				}
			}
		}
	}

	/* 3. Activate virtual group, if needed */

	if (EXEC_STATE_INACTIVE == virtual_group_state &&
	    0 < mali_scheduler_job_next_is_virtual()) {
		enum mali_group_state state =
			mali_group_activate(virtual_group);
		if (MALI_GROUP_STATE_ACTIVE == state) {
			/* Set virtual group state to idle */
			virtual_group_state = EXEC_STATE_IDLE;
		} else {
			trigger_pm_update = MALI_TRUE;
		}
	}

	/* 4. To power up group asap, we trigger pm update here. */

	if (MALI_TRUE == trigger_pm_update) {
		trigger_pm_update = MALI_FALSE;
		mali_pm_update_async();
	}

	/* 5. Deactivate idle pp group */

	if (MALI_TRUE == mali_executor_deactivate_list_idle(deactivate_idle_group
			&& (!mali_timeline_has_physical_pp_job()))) {
		trigger_pm_update = MALI_TRUE;
	}

	/* 6. Assign jobs to idle virtual group (or deactivate if no job) */

	if (EXEC_STATE_IDLE == virtual_group_state) {
		if (0 < mali_scheduler_job_next_is_virtual()) {
			virtual_job_to_start =
				mali_scheduler_job_pp_virtual_get();
			virtual_group_state = EXEC_STATE_WORKING;
		} else if (!mali_timeline_has_virtual_pp_job()) {
			virtual_group_state = EXEC_STATE_INACTIVE;

			if (mali_group_deactivate(virtual_group)) {
				trigger_pm_update = MALI_TRUE;
			}
		}
	}

	/* 7. Assign job to idle GP group (or deactivate if no job) */

	if (EXEC_STATE_IDLE == gp_group_state) {
		if (0 < mali_scheduler_job_gp_count()) {
			gp_job_to_start = mali_scheduler_job_gp_get();
			gp_group_state = EXEC_STATE_WORKING;
		} else if (!mali_timeline_has_gp_job()) {
			gp_group_state = EXEC_STATE_INACTIVE;
			if (mali_group_deactivate(gp_group)) {
				trigger_pm_update = MALI_TRUE;
			}
		}
	}

	/* 8. We no longer need the schedule/queue lock */

	mali_scheduler_unlock();

	/* 9. start jobs */

	if (NULL != virtual_job_to_start) {
		MALI_DEBUG_ASSERT(!mali_group_pp_is_active(virtual_group));
		mali_group_start_pp_job(virtual_group,
					virtual_job_to_start, 0);
	}

	for (i = 0; i < num_jobs_to_start; i++) {
		MALI_DEBUG_ASSERT(!mali_group_pp_is_active(
					  groups_to_start[i]));
		mali_group_start_pp_job(groups_to_start[i],
					jobs_to_start[i],
					sub_jobs_to_start[i]);
	}

	MALI_DEBUG_ASSERT_POINTER(gp_group);

	if (NULL != gp_job_to_start) {
		MALI_DEBUG_ASSERT(!mali_group_gp_is_active(gp_group));
		mali_group_start_gp_job(gp_group, gp_job_to_start);
	}

	/* 10. Trigger any pending PM updates */
	if (MALI_TRUE == trigger_pm_update) {
		mali_pm_update_async();
	}
}

/* Handler for deferred schedule requests */
static void mali_executor_wq_schedule(void *arg)
{
	MALI_IGNORE(arg);
	mali_executor_lock();
	mali_executor_schedule();
	mali_executor_unlock();
}

static void mali_executor_send_gp_oom_to_user(struct mali_gp_job *job)
{
	_mali_uk_gp_job_suspended_s *jobres;
	_mali_osk_notification_t *notification;

	notification = mali_gp_job_get_oom_notification(job);

	/*
	 * Remember the id we send to user space, so we have something to
	 * verify when we get a response
	 */
	gp_returned_cookie = mali_gp_job_get_id(job);

	jobres = (_mali_uk_gp_job_suspended_s *)notification->result_buffer;
	jobres->user_job_ptr = mali_gp_job_get_user_id(job);
	jobres->cookie = gp_returned_cookie;

	mali_session_send_notification(mali_gp_job_get_session(job),
				       notification);
}
static struct mali_gp_job *mali_executor_complete_gp(struct mali_group *group,
		mali_bool success,
		mali_bool release_jobs)
{
	struct mali_gp_job *job;

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	/* Extracts the needed HW status from core and reset */
	job = mali_group_complete_gp(group, success);

	MALI_DEBUG_ASSERT_POINTER(job);

	/* Core is now ready to go into idle list */
	gp_group_state = EXEC_STATE_IDLE;

	if (release_jobs) {
		/* This will potentially queue more GP and PP jobs */
		mali_timeline_tracker_release(&job->tracker);

		/* Signal PP job */
		mali_gp_job_signal_pp_tracker(job, success);
	}

	return job;
}

static struct mali_pp_job *mali_executor_complete_pp(struct mali_group *group,
		mali_bool success,
		mali_bool release_jobs)
{
	struct mali_pp_job *job;
	u32 sub_job;
	mali_bool job_is_done;

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	/* Extracts the needed HW status from core and reset */
	job = mali_group_complete_pp(group, success, &sub_job);

	MALI_DEBUG_ASSERT_POINTER(job);

	/* Core is now ready to go into idle list */
	if (mali_group_is_virtual(group)) {
		virtual_group_state = EXEC_STATE_IDLE;
	} else {
		/* Move from working to idle state */
		mali_executor_change_state_pp_physical(group,
						       &group_list_working,
						       &group_list_working_count,
						       &group_list_idle,
						       &group_list_idle_count);
	}

	/* It is the executor module which owns the jobs themselves by now */
	mali_pp_job_mark_sub_job_completed(job, success);
	job_is_done = mali_pp_job_is_complete(job);

	if (job_is_done && release_jobs) {
		/* This will potentially queue more GP and PP jobs */
		mali_timeline_tracker_release(&job->tracker);
	}

	return job;
}

static void mali_executor_complete_group(struct mali_group *group,
		mali_bool success,
		mali_bool release_jobs,
		struct mali_gp_job **gp_job_done,
		struct mali_pp_job **pp_job_done)
{
	struct mali_gp_core *gp_core = mali_group_get_gp_core(group);
	struct mali_pp_core *pp_core = mali_group_get_pp_core(group);
	struct mali_gp_job *gp_job = NULL;
	struct mali_pp_job *pp_job = NULL;
	mali_bool pp_job_is_done = MALI_TRUE;

	if (NULL != gp_core) {
		gp_job = mali_executor_complete_gp(group,
						   success, release_jobs);
	} else {
		MALI_DEBUG_ASSERT_POINTER(pp_core);
		MALI_IGNORE(pp_core);
		pp_job = mali_executor_complete_pp(group,
						   success, release_jobs);

		pp_job_is_done = mali_pp_job_is_complete(pp_job);
	}

	if (pause_count > 0) {
		/* Execution has been suspended */

		if (!mali_executor_is_working()) {
			/* Last job completed, wake up sleepers */
			_mali_osk_wait_queue_wake_up(
				executor_working_wait_queue);
		}
	} else if (MALI_TRUE == mali_group_disable_requested(group)) {
		mali_executor_core_scale_in_group_complete(group);

		mali_executor_schedule();
	} else {
		/* try to schedule new jobs */
		mali_executor_schedule();
	}

	if (NULL != gp_job) {
		MALI_DEBUG_ASSERT_POINTER(gp_job_done);
		*gp_job_done = gp_job;
	} else if (pp_job_is_done) {
		MALI_DEBUG_ASSERT_POINTER(pp_job);
		MALI_DEBUG_ASSERT_POINTER(pp_job_done);
		*pp_job_done = pp_job;
	}
}

static void mali_executor_change_state_pp_physical(struct mali_group *group,
		_mali_osk_list_t *old_list,
		u32 *old_count,
		_mali_osk_list_t *new_list,
		u32 *new_count)
{
	/*
	 * It's a bit more complicated to change the state for the physical PP
	 * groups since their state is determined by the list they are on.
	 */
#if defined(DEBUG)
	mali_bool found = MALI_FALSE;
	struct mali_group *group_iter;
	struct mali_group *temp;
	u32 old_counted = 0;
	u32 new_counted = 0;

	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(old_list);
	MALI_DEBUG_ASSERT_POINTER(old_count);
	MALI_DEBUG_ASSERT_POINTER(new_list);
	MALI_DEBUG_ASSERT_POINTER(new_count);

	/*
	 * Verify that group is present on old list,
	 * and that the count is correct
	 */

	_MALI_OSK_LIST_FOREACHENTRY(group_iter, temp, old_list,
				    struct mali_group, executor_list) {
		old_counted++;
		if (group == group_iter) {
			found = MALI_TRUE;
		}
	}

	_MALI_OSK_LIST_FOREACHENTRY(group_iter, temp, new_list,
				    struct mali_group, executor_list) {
		new_counted++;
	}

	if (MALI_FALSE == found) {
		if (old_list == &group_list_idle) {
			MALI_DEBUG_PRINT(1, (" old Group list is idle,"));
		} else if (old_list == &group_list_inactive) {
			MALI_DEBUG_PRINT(1, (" old Group list is inactive,"));
		} else if (old_list == &group_list_working) {
			MALI_DEBUG_PRINT(1, (" old Group list is working,"));
		} else if (old_list == &group_list_disabled) {
			MALI_DEBUG_PRINT(1, (" old Group list is disable,"));
		}

		if (MALI_TRUE == mali_executor_group_is_in_state(group, EXEC_STATE_WORKING)) {
			MALI_DEBUG_PRINT(1, (" group in working \n"));
		} else if (MALI_TRUE == mali_executor_group_is_in_state(group, EXEC_STATE_INACTIVE)) {
			MALI_DEBUG_PRINT(1, (" group in inactive \n"));
		} else if (MALI_TRUE == mali_executor_group_is_in_state(group, EXEC_STATE_IDLE)) {
			MALI_DEBUG_PRINT(1, (" group in idle \n"));
		} else if (MALI_TRUE == mali_executor_group_is_in_state(group, EXEC_STATE_DISABLED)) {
			MALI_DEBUG_PRINT(1, (" but group in disabled \n"));
		}
	}

	MALI_DEBUG_ASSERT(MALI_TRUE == found);
	MALI_DEBUG_ASSERT(0 < (*old_count));
	MALI_DEBUG_ASSERT((*old_count) == old_counted);
	MALI_DEBUG_ASSERT((*new_count) == new_counted);
#endif

	_mali_osk_list_move(&group->executor_list, new_list);
	(*old_count)--;
	(*new_count)++;
}

static void mali_executor_set_state_pp_physical(struct mali_group *group,
		_mali_osk_list_t *new_list,
		u32 *new_count)
{
	_mali_osk_list_add(&group->executor_list, new_list);
	(*new_count)++;
}

static mali_bool mali_executor_group_is_in_state(struct mali_group *group,
		enum mali_executor_state_t state)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	if (gp_group == group) {
		if (gp_group_state == state) {
			return MALI_TRUE;
		}
	} else if (virtual_group == group || mali_group_is_in_virtual(group)) {
		if (virtual_group_state == state) {
			return MALI_TRUE;
		}
	} else {
		/* Physical PP group */
		struct mali_group *group_iter;
		struct mali_group *temp;
		_mali_osk_list_t *list;

		if (EXEC_STATE_DISABLED == state) {
			list = &group_list_disabled;
		} else if (EXEC_STATE_INACTIVE == state) {
			list = &group_list_inactive;
		} else if (EXEC_STATE_IDLE == state) {
			list = &group_list_idle;
		} else {
			MALI_DEBUG_ASSERT(EXEC_STATE_WORKING == state);
			list = &group_list_working;
		}

		_MALI_OSK_LIST_FOREACHENTRY(group_iter, temp, list,
					    struct mali_group, executor_list) {
			if (group_iter == group) {
				return MALI_TRUE;
			}
		}
	}

	/* group not in correct state */
	return MALI_FALSE;
}

static void mali_executor_group_enable_internal(struct mali_group *group)
{
	MALI_DEBUG_ASSERT(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT(mali_executor_group_is_in_state(group, EXEC_STATE_DISABLED));

	/* Put into inactive state (== "lowest" enabled state) */
	if (group == gp_group) {
		MALI_DEBUG_ASSERT(EXEC_STATE_DISABLED == gp_group_state);
		gp_group_state = EXEC_STATE_INACTIVE;
	} else {
		mali_executor_change_state_pp_physical(group,
						       &group_list_disabled,
						       &group_list_disabled_count,
						       &group_list_inactive,
						       &group_list_inactive_count);

		++num_physical_pp_cores_enabled;
		MALI_DEBUG_PRINT(4, ("Enabling group id %d \n", group->pp_core->core_id));
	}

	if (MALI_GROUP_STATE_ACTIVE == mali_group_activate(group)) {
		MALI_DEBUG_ASSERT(MALI_TRUE == mali_group_power_is_on(group));

		/* Move from inactive to idle */
		if (group == gp_group) {
			gp_group_state = EXEC_STATE_IDLE;
		} else {
			mali_executor_change_state_pp_physical(group,
							       &group_list_inactive,
							       &group_list_inactive_count,
							       &group_list_idle,
							       &group_list_idle_count);

			if (mali_executor_has_virtual_group()) {
				if (mali_executor_physical_rejoin_virtual(group)) {
					mali_pm_update_async();
				}
			}
		}
	} else {
		mali_pm_update_async();
	}
}

static void mali_executor_group_disable_internal(struct mali_group *group)
{
	mali_bool working;

	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT(!mali_executor_group_is_in_state(group, EXEC_STATE_DISABLED));

	working = mali_executor_group_is_in_state(group, EXEC_STATE_WORKING);
	if (MALI_TRUE == working) {
		/** Group to be disabled once it completes current work,
		 * when virtual group completes, also check child groups for this flag */
		mali_group_set_disable_request(group, MALI_TRUE);
		return;
	}

	/* Put into disabled state */
	if (group == gp_group) {
		/* GP group */
		MALI_DEBUG_ASSERT(EXEC_STATE_WORKING != gp_group_state);
		gp_group_state = EXEC_STATE_DISABLED;
	} else {
		if (mali_group_is_in_virtual(group)) {
			/* A child group of virtual group. move the specific group from virtual group */
			MALI_DEBUG_ASSERT(EXEC_STATE_WORKING != virtual_group_state);

			mali_executor_set_state_pp_physical(group,
							    &group_list_disabled,
							    &group_list_disabled_count);

			mali_group_remove_group(virtual_group, group);
			mali_executor_disable_empty_virtual();
		} else {
			mali_executor_change_group_status_disabled(group);
		}

		--num_physical_pp_cores_enabled;
		MALI_DEBUG_PRINT(4, ("Disabling group id %d \n", group->pp_core->core_id));
	}

	if (MALI_GROUP_STATE_INACTIVE != group->state) {
		if (MALI_TRUE == mali_group_deactivate(group)) {
			mali_pm_update_async();
		}
	}
}

static void mali_executor_notify_core_change(u32 num_cores)
{
	mali_bool done = MALI_FALSE;

	if (mali_is_mali450()) {
		return;
	}

	/*
	 * This function gets a bit complicated because we can't hold the session lock while
	 * allocating notification objects.
	 */
	while (!done) {
		u32 i;
		u32 num_sessions_alloc;
		u32 num_sessions_with_lock;
		u32 used_notification_objects = 0;
		_mali_osk_notification_t **notobjs;

		/* Pre allocate the number of notifications objects we need right now (might change after lock has been taken) */
		num_sessions_alloc = mali_session_get_count();
		if (0 == num_sessions_alloc) {
			/* No sessions to report to */
			return;
		}

		notobjs = (_mali_osk_notification_t **)_mali_osk_malloc(sizeof(_mali_osk_notification_t *) * num_sessions_alloc);
		if (NULL == notobjs) {
			MALI_PRINT_ERROR(("Failed to notify user space session about num PP core change (alloc failure)\n"));
			/* there is probably no point in trying again, system must be really low on memory and probably unusable now anyway */
			return;
		}

		for (i = 0; i < num_sessions_alloc; i++) {
			notobjs[i] = _mali_osk_notification_create(_MALI_NOTIFICATION_PP_NUM_CORE_CHANGE, sizeof(_mali_uk_pp_num_cores_changed_s));
			if (NULL != notobjs[i]) {
				_mali_uk_pp_num_cores_changed_s *data = notobjs[i]->result_buffer;
				data->number_of_enabled_cores = num_cores;
			} else {
				MALI_PRINT_ERROR(("Failed to notify user space session about num PP core change (alloc failure %u)\n", i));
			}
		}

		mali_session_lock();

		/* number of sessions will not change while we hold the lock */
		num_sessions_with_lock = mali_session_get_count();

		if (num_sessions_alloc >= num_sessions_with_lock) {
			/* We have allocated enough notification objects for all the sessions atm */
			struct mali_session_data *session, *tmp;
			MALI_SESSION_FOREACH(session, tmp, link) {
				MALI_DEBUG_ASSERT(used_notification_objects < num_sessions_alloc);
				if (NULL != notobjs[used_notification_objects]) {
					mali_session_send_notification(session, notobjs[used_notification_objects]);
					notobjs[used_notification_objects] = NULL; /* Don't track this notification object any more */
				}
				used_notification_objects++;
			}
			done = MALI_TRUE;
		}

		mali_session_unlock();

		/* Delete any remaining/unused notification objects */
		for (; used_notification_objects < num_sessions_alloc; used_notification_objects++) {
			if (NULL != notobjs[used_notification_objects]) {
				_mali_osk_notification_delete(notobjs[used_notification_objects]);
			}
		}

		_mali_osk_free(notobjs);
	}
}

static mali_bool mali_executor_core_scaling_is_done(void *data)
{
	u32 i;
	u32 num_groups;
	mali_bool ret = MALI_TRUE;

	MALI_IGNORE(data);

	mali_executor_lock();

	num_groups = mali_group_get_glob_num_groups();

	for (i = 0; i < num_groups; i++) {
		struct mali_group *group = mali_group_get_glob_group(i);

		if (NULL != group) {
			if (MALI_TRUE == group->disable_requested && NULL != mali_group_get_pp_core(group)) {
				ret = MALI_FALSE;
				break;
			}
		}
	}
	mali_executor_unlock();

	return ret;
}

static void mali_executor_wq_notify_core_change(void *arg)
{
	MALI_IGNORE(arg);

	if (mali_is_mali450()) {
		return;
	}

	_mali_osk_wait_queue_wait_event(executor_notify_core_change_wait_queue,
					mali_executor_core_scaling_is_done, NULL);

	mali_executor_notify_core_change(num_physical_pp_cores_enabled);
}

/**
 * Clear all disable request from the _last_ core scaling behavior.
 */
static void mali_executor_core_scaling_reset(void)
{
	u32 i;
	u32 num_groups;

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	num_groups = mali_group_get_glob_num_groups();

	for (i = 0; i < num_groups; i++) {
		struct mali_group *group = mali_group_get_glob_group(i);

		if (NULL != group) {
			group->disable_requested = MALI_FALSE;
		}
	}

	for (i = 0; i < MALI_MAX_NUMBER_OF_DOMAINS; i++) {
		core_scaling_delay_up_mask[i] = 0;
	}
}

static void mali_executor_core_scale(unsigned int target_core_nr)
{
	int current_core_scaling_mask[MALI_MAX_NUMBER_OF_DOMAINS] = { 0 };
	int target_core_scaling_mask[MALI_MAX_NUMBER_OF_DOMAINS] = { 0 };
	mali_bool update_global_core_scaling_mask = MALI_FALSE;
	int i;

	MALI_DEBUG_ASSERT(0 < target_core_nr);
	MALI_DEBUG_ASSERT(num_physical_pp_cores_total >= target_core_nr);

	mali_executor_lock();

	if (target_core_nr < num_physical_pp_cores_enabled) {
		MALI_DEBUG_PRINT(2, ("Requesting %d cores: disabling %d cores\n", target_core_nr, num_physical_pp_cores_enabled - target_core_nr));
	} else {
		MALI_DEBUG_PRINT(2, ("Requesting %d cores: enabling %d cores\n", target_core_nr, target_core_nr - num_physical_pp_cores_enabled));
	}

	/* When a new core scaling request is comming,  we should remove the un-doing
	 * part of the last core scaling request.  It's safe because we have only one
	 * lock(executor lock) protection. */
	mali_executor_core_scaling_reset();

	mali_pm_get_best_power_cost_mask(num_physical_pp_cores_enabled, current_core_scaling_mask);
	mali_pm_get_best_power_cost_mask(target_core_nr, target_core_scaling_mask);

	for (i = 0; i < MALI_MAX_NUMBER_OF_DOMAINS; i++) {
		target_core_scaling_mask[i] = target_core_scaling_mask[i] - current_core_scaling_mask[i];
		MALI_DEBUG_PRINT(5, ("target_core_scaling_mask[%d] = %d\n", i, target_core_scaling_mask[i]));
	}

	for (i = 0; i < MALI_MAX_NUMBER_OF_DOMAINS; i++) {
		if (0 > target_core_scaling_mask[i]) {
			struct mali_pm_domain *domain;

			domain = mali_pm_domain_get_from_index(i);

			/* Domain is valid and has pp cores */
			if ((NULL != domain) && !(_mali_osk_list_empty(&domain->group_list))) {
				struct mali_group *group;
				struct mali_group *temp;

				_MALI_OSK_LIST_FOREACHENTRY(group, temp, &domain->group_list, struct mali_group, pm_domain_list) {
					if (NULL != mali_group_get_pp_core(group) && (!mali_executor_group_is_in_state(group, EXEC_STATE_DISABLED))
					    && (!mali_group_is_virtual(group))) {
						mali_executor_group_disable_internal(group);
						target_core_scaling_mask[i]++;
						if ((0 == target_core_scaling_mask[i])) {
							break;
						}

					}
				}
			}
		}
	}

	for (i = 0; i < MALI_MAX_NUMBER_OF_DOMAINS; i++) {
		/**
		 * Target_core_scaling_mask[i] is bigger than 0,
		 * means we need to enable some pp cores in
		 * this domain whose domain index is i.
		 */
		if (0 < target_core_scaling_mask[i]) {
			struct mali_pm_domain *domain;

			if (num_physical_pp_cores_enabled >= target_core_nr) {
				update_global_core_scaling_mask = MALI_TRUE;
				break;
			}

			domain = mali_pm_domain_get_from_index(i);

			/* Domain is valid and has pp cores */
			if ((NULL != domain) && !(_mali_osk_list_empty(&domain->group_list))) {
				struct mali_group *group;
				struct mali_group *temp;

				_MALI_OSK_LIST_FOREACHENTRY(group, temp, &domain->group_list, struct mali_group, pm_domain_list) {
					if (NULL != mali_group_get_pp_core(group) && mali_executor_group_is_in_state(group, EXEC_STATE_DISABLED)
					    && (!mali_group_is_virtual(group))) {
						mali_executor_group_enable_internal(group);
						target_core_scaling_mask[i]--;

						if ((0 == target_core_scaling_mask[i]) || num_physical_pp_cores_enabled == target_core_nr) {
							break;
						}
					}
				}
			}
		}
	}

	/**
	 * Here, we may still have some pp cores not been enabled because of some
	 * pp cores need to be disabled are still in working state.
	 */
	if (update_global_core_scaling_mask) {
		for (i = 0; i < MALI_MAX_NUMBER_OF_DOMAINS; i++) {
			if (0 < target_core_scaling_mask[i]) {
				core_scaling_delay_up_mask[i] = target_core_scaling_mask[i];
			}
		}
	}

	mali_executor_schedule();
	mali_executor_unlock();
}

static void mali_executor_core_scale_in_group_complete(struct mali_group *group)
{
	int num_pp_cores_disabled = 0;
	int num_pp_cores_to_enable = 0;
	int i;

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT(MALI_TRUE == mali_group_disable_requested(group));

	/* Disable child group of virtual group */
	if (mali_group_is_virtual(group)) {
		struct mali_group *child;
		struct mali_group *temp;

		_MALI_OSK_LIST_FOREACHENTRY(child, temp, &group->group_list, struct mali_group, group_list) {
			if (MALI_TRUE == mali_group_disable_requested(child)) {
				mali_group_set_disable_request(child, MALI_FALSE);
				mali_executor_group_disable_internal(child);
				num_pp_cores_disabled++;
			}
		}
		mali_group_set_disable_request(group, MALI_FALSE);
	} else {
		mali_executor_group_disable_internal(group);
		mali_group_set_disable_request(group, MALI_FALSE);
		if (NULL != mali_group_get_pp_core(group)) {
			num_pp_cores_disabled++;
		}
	}

	num_pp_cores_to_enable = num_pp_cores_disabled;

	for (i = 0; i < MALI_MAX_NUMBER_OF_DOMAINS; i++) {
		if (0 < core_scaling_delay_up_mask[i]) {
			struct mali_pm_domain *domain;

			if (0 == num_pp_cores_to_enable) {
				break;
			}

			domain = mali_pm_domain_get_from_index(i);

			/* Domain is valid and has pp cores */
			if ((NULL != domain) && !(_mali_osk_list_empty(&domain->group_list))) {
				struct mali_group *disabled_group;
				struct mali_group *temp;

				_MALI_OSK_LIST_FOREACHENTRY(disabled_group, temp, &domain->group_list, struct mali_group, pm_domain_list) {
					if (NULL != mali_group_get_pp_core(disabled_group) && mali_executor_group_is_in_state(disabled_group, EXEC_STATE_DISABLED)) {
						mali_executor_group_enable_internal(disabled_group);
						core_scaling_delay_up_mask[i]--;
						num_pp_cores_to_enable--;

						if ((0 == core_scaling_delay_up_mask[i]) || 0 == num_pp_cores_to_enable) {
							break;
						}
					}
				}
			}
		}
	}

	_mali_osk_wait_queue_wake_up(executor_notify_core_change_wait_queue);
}

static void mali_executor_change_group_status_disabled(struct mali_group *group)
{
	/* Physical PP group */
	mali_bool idle;

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	idle = mali_executor_group_is_in_state(group, EXEC_STATE_IDLE);
	if (MALI_TRUE == idle) {
		mali_executor_change_state_pp_physical(group,
						       &group_list_idle,
						       &group_list_idle_count,
						       &group_list_disabled,
						       &group_list_disabled_count);
	} else {
		mali_executor_change_state_pp_physical(group,
						       &group_list_inactive,
						       &group_list_inactive_count,
						       &group_list_disabled,
						       &group_list_disabled_count);
	}
}

static mali_bool mali_executor_deactivate_list_idle(mali_bool deactivate_idle_group)
{
	mali_bool trigger_pm_update = MALI_FALSE;

	if (group_list_idle_count > 0) {
		if (mali_executor_has_virtual_group()) {

			/* Rejoin virtual group on Mali-450 */

			struct mali_group *group;
			struct mali_group *temp;

			_MALI_OSK_LIST_FOREACHENTRY(group, temp,
						    &group_list_idle,
						    struct mali_group, executor_list) {
				if (mali_executor_physical_rejoin_virtual(
					    group)) {
					trigger_pm_update = MALI_TRUE;
				}
			}
		} else if (deactivate_idle_group) {
			struct mali_group *group;
			struct mali_group *temp;

			/* Deactivate group on Mali-300/400 */

			_MALI_OSK_LIST_FOREACHENTRY(group, temp,
						    &group_list_idle,
						    struct mali_group, executor_list) {
				if (mali_group_deactivate(group)) {
					trigger_pm_update = MALI_TRUE;
				}

				/* Move from idle to inactive */
				mali_executor_change_state_pp_physical(group,
								       &group_list_idle,
								       &group_list_idle_count,
								       &group_list_inactive,
								       &group_list_inactive_count);
			}
		}
	}

	return trigger_pm_update;
}
