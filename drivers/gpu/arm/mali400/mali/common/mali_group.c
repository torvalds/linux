/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2011-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include "mali_kernel_common.h"
#include "mali_group.h"
#include "mali_osk.h"
#include "mali_l2_cache.h"
#include "mali_gp.h"
#include "mali_pp.h"
#include "mali_mmu.h"
#include "mali_dlbu.h"
#include "mali_broadcast.h"
#include "mali_scheduler.h"
#include "mali_osk_profiling.h"
#include "mali_pm_domain.h"
#include "mali_pm.h"
#if defined(CONFIG_GPU_TRACEPOINTS) && defined(CONFIG_TRACEPOINTS)
#include <linux/sched.h>
#include <trace/events/gpu.h>
#endif


static void mali_group_bottom_half_mmu(void *data);
static void mali_group_bottom_half_gp(void *data);
static void mali_group_bottom_half_pp(void *data);

static void mali_group_timeout(void *data);
static void mali_group_reset_pp(struct mali_group *group);
static void mali_group_reset_mmu(struct mali_group *group);

#if defined(CONFIG_MALI400_PROFILING)
static void mali_group_report_l2_cache_counters_per_core(struct mali_group *group, u32 core_num);
#endif /* #if defined(CONFIG_MALI400_PROFILING) */

/*
 * The group object is the most important object in the device driver,
 * and acts as the center of many HW operations.
 * The reason for this is that operations on the MMU will affect all
 * cores connected to this MMU (a group is defined by the MMU and the
 * cores which are connected to this).
 * The group lock is thus the most important lock, followed by the
 * GP and PP scheduler locks. They must be taken in the following
 * order:
 * GP/PP lock first, then group lock(s).
 */

static struct mali_group *mali_global_groups[MALI_MAX_NUMBER_OF_GROUPS] = { NULL, };
static u32 mali_global_num_groups = 0;

/* timer related */
int mali_max_job_runtime = MALI_MAX_JOB_RUNTIME_DEFAULT;
extern u32 mali_group_error;
/* local helper functions */
static void mali_group_activate_page_directory(struct mali_group *group, struct mali_session_data *session);
static void mali_group_remove_session_if_unused(struct mali_group *group, struct mali_session_data *session);
static void mali_group_recovery_reset(struct mali_group *group);
static void mali_group_mmu_page_fault_and_unlock(struct mali_group *group);

static void mali_group_post_process_job_pp(struct mali_group *group);
static void mali_group_post_process_job_gp(struct mali_group *group, mali_bool suspend);

void mali_group_lock(struct mali_group *group)
{
#ifdef MALI_UPPER_HALF_SCHEDULING
	_mali_osk_spinlock_irq_lock(group->lock);
#else
	_mali_osk_spinlock_lock(group->lock);
#endif
	MALI_DEBUG_PRINT(5, ("Mali group: Group lock taken 0x%08X\n", group));
}

void mali_group_unlock(struct mali_group *group)
{
	MALI_DEBUG_PRINT(5, ("Mali group: Releasing group lock 0x%08X\n", group));
#ifdef MALI_UPPER_HALF_SCHEDULING
	_mali_osk_spinlock_irq_unlock(group->lock);
#else
	_mali_osk_spinlock_unlock(group->lock);
#endif
}

#ifdef DEBUG
void mali_group_assert_locked(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_LOCK_HELD(group->lock);
}
#endif


struct mali_group *mali_group_create(struct mali_l2_cache_core *core, struct mali_dlbu_core *dlbu, struct mali_bcast_unit *bcast)
{
	struct mali_group *group = NULL;

	if (mali_global_num_groups >= MALI_MAX_NUMBER_OF_GROUPS) {
		MALI_PRINT_ERROR(("Mali group: Too many group objects created\n"));
		return NULL;
	}

	group = _mali_osk_calloc(1, sizeof(struct mali_group));
	if (NULL != group) {
		group->timeout_timer = _mali_osk_timer_init();

		if (NULL != group->timeout_timer) {
			_mali_osk_lock_order_t order;
			_mali_osk_timer_setcallback(group->timeout_timer, mali_group_timeout, (void *)group);

			if (NULL != dlbu) {
				order = _MALI_OSK_LOCK_ORDER_GROUP_VIRTUAL;
			} else {
				order = _MALI_OSK_LOCK_ORDER_GROUP;
			}

#ifdef MALI_UPPER_HALF_SCHEDULING
			group->lock = _mali_osk_spinlock_irq_init(_MALI_OSK_LOCKFLAG_ORDERED, order);
#else
			group->lock = _mali_osk_spinlock_init(_MALI_OSK_LOCKFLAG_ORDERED, order);
#endif

			if (NULL != group->lock) {
				group->l2_cache_core[0] = core;
				group->session = NULL;
				group->power_is_on = MALI_TRUE;
				group->state = MALI_GROUP_STATE_IDLE;
				_mali_osk_list_init(&group->group_list);
				_mali_osk_list_init(&group->pp_scheduler_list);
				group->parent_group = NULL;
				group->l2_cache_core_ref_count[0] = 0;
				group->l2_cache_core_ref_count[1] = 0;
				group->bcast_core = bcast;
				group->dlbu_core = dlbu;

				mali_global_groups[mali_global_num_groups] = group;
				mali_global_num_groups++;

				return group;
			}
			_mali_osk_timer_term(group->timeout_timer);
		}
		_mali_osk_free(group);
	}

	return NULL;
}

_mali_osk_errcode_t mali_group_add_mmu_core(struct mali_group *group, struct mali_mmu_core* mmu_core)
{
	/* This group object now owns the MMU core object */
	group->mmu= mmu_core;
	group->bottom_half_work_mmu = _mali_osk_wq_create_work(mali_group_bottom_half_mmu, group);
	if (NULL == group->bottom_half_work_mmu) {
		return _MALI_OSK_ERR_FAULT;
	}
	return _MALI_OSK_ERR_OK;
}

void mali_group_remove_mmu_core(struct mali_group *group)
{
	/* This group object no longer owns the MMU core object */
	group->mmu = NULL;
	if (NULL != group->bottom_half_work_mmu) {
		_mali_osk_wq_delete_work(group->bottom_half_work_mmu);
	}
}

_mali_osk_errcode_t mali_group_add_gp_core(struct mali_group *group, struct mali_gp_core* gp_core)
{
	/* This group object now owns the GP core object */
	group->gp_core = gp_core;
	group->bottom_half_work_gp = _mali_osk_wq_create_work(mali_group_bottom_half_gp, group);
	if (NULL == group->bottom_half_work_gp) {
		return _MALI_OSK_ERR_FAULT;
	}
	return _MALI_OSK_ERR_OK;
}

void mali_group_remove_gp_core(struct mali_group *group)
{
	/* This group object no longer owns the GP core object */
	group->gp_core = NULL;
	if (NULL != group->bottom_half_work_gp) {
		_mali_osk_wq_delete_work(group->bottom_half_work_gp);
	}
}

_mali_osk_errcode_t mali_group_add_pp_core(struct mali_group *group, struct mali_pp_core* pp_core)
{
	/* This group object now owns the PP core object */
	group->pp_core = pp_core;
	group->bottom_half_work_pp = _mali_osk_wq_create_work(mali_group_bottom_half_pp, group);
	if (NULL == group->bottom_half_work_pp) {
		return _MALI_OSK_ERR_FAULT;
	}
	return _MALI_OSK_ERR_OK;
}

void mali_group_remove_pp_core(struct mali_group *group)
{
	/* This group object no longer owns the PP core object */
	group->pp_core = NULL;
	if (NULL != group->bottom_half_work_pp) {
		_mali_osk_wq_delete_work(group->bottom_half_work_pp);
	}
}

void mali_group_set_pm_domain(struct mali_group *group, struct mali_pm_domain *domain)
{
	group->pm_domain = domain;
}

void mali_group_delete(struct mali_group *group)
{
	u32 i;

	MALI_DEBUG_PRINT(4, ("Deleting group %p\n", group));

	MALI_DEBUG_ASSERT(NULL == group->parent_group);

	/* Delete the resources that this group owns */
	if (NULL != group->gp_core) {
		mali_gp_delete(group->gp_core);
	}

	if (NULL != group->pp_core) {
		mali_pp_delete(group->pp_core);
	}

	if (NULL != group->mmu) {
		mali_mmu_delete(group->mmu);
	}

	if (mali_group_is_virtual(group)) {
		/* Remove all groups from virtual group */
		struct mali_group *child;
		struct mali_group *temp;

		_MALI_OSK_LIST_FOREACHENTRY(child, temp, &group->group_list, struct mali_group, group_list) {
			child->parent_group = NULL;
			mali_group_delete(child);
		}

		mali_dlbu_delete(group->dlbu_core);

		if (NULL != group->bcast_core) {
			mali_bcast_unit_delete(group->bcast_core);
		}
	}

	for (i = 0; i < mali_global_num_groups; i++) {
		if (mali_global_groups[i] == group) {
			mali_global_groups[i] = NULL;
			mali_global_num_groups--;

			if (i != mali_global_num_groups) {
				/* We removed a group from the middle of the array -- move the last
				 * group to the current position to close the gap */
				mali_global_groups[i] = mali_global_groups[mali_global_num_groups];
				mali_global_groups[mali_global_num_groups] = NULL;
			}

			break;
		}
	}

	if (NULL != group->timeout_timer) {
		_mali_osk_timer_del(group->timeout_timer);
		_mali_osk_timer_term(group->timeout_timer);
	}

	if (NULL != group->bottom_half_work_mmu) {
		_mali_osk_wq_delete_work(group->bottom_half_work_mmu);
	}

	if (NULL != group->bottom_half_work_gp) {
		_mali_osk_wq_delete_work(group->bottom_half_work_gp);
	}

	if (NULL != group->bottom_half_work_pp) {
		_mali_osk_wq_delete_work(group->bottom_half_work_pp);
	}

#ifdef MALI_UPPER_HALF_SCHEDULING
	_mali_osk_spinlock_irq_term(group->lock);
#else
	_mali_osk_spinlock_term(group->lock);
#endif
	_mali_osk_free(group);
}

MALI_DEBUG_CODE(static void mali_group_print_virtual(struct mali_group *vgroup)
{
	u32 i;
	struct mali_group *group;
	struct mali_group *temp;

	MALI_DEBUG_PRINT(4, ("Virtual group %p\n", vgroup));
	MALI_DEBUG_PRINT(4, ("l2_cache_core[0] = %p, ref = %d\n", vgroup->l2_cache_core[0], vgroup->l2_cache_core_ref_count[0]));
	MALI_DEBUG_PRINT(4, ("l2_cache_core[1] = %p, ref = %d\n", vgroup->l2_cache_core[1], vgroup->l2_cache_core_ref_count[1]));

	i = 0;
	_MALI_OSK_LIST_FOREACHENTRY(group, temp, &vgroup->group_list, struct mali_group, group_list) {
		MALI_DEBUG_PRINT(4, ("[%d] %p, l2_cache_core[0] = %p\n", i, group, group->l2_cache_core[0]));
		i++;
	}
})

/**
 * @brief Add child group to virtual group parent
 *
 * Before calling this function, child must have it's state set to JOINING_VIRTUAL
 * to ensure it's not touched during the transition period. When this function returns,
 * child's state will be IN_VIRTUAL.
 */
void mali_group_add_group(struct mali_group *parent, struct mali_group *child, mali_bool update_hw)
{
	mali_bool found;
	u32 i;
	struct mali_session_data *child_session;

	MALI_DEBUG_PRINT(3, ("Adding group %p to virtual group %p\n", child, parent));

	MALI_ASSERT_GROUP_LOCKED(parent);

	MALI_DEBUG_ASSERT(mali_group_is_virtual(parent));
	MALI_DEBUG_ASSERT(!mali_group_is_virtual(child));
	MALI_DEBUG_ASSERT(NULL == child->parent_group);
	MALI_DEBUG_ASSERT(MALI_GROUP_STATE_JOINING_VIRTUAL == child->state);

	_mali_osk_list_addtail(&child->group_list, &parent->group_list);

	child->state = MALI_GROUP_STATE_IN_VIRTUAL;
	child->parent_group = parent;

	MALI_DEBUG_ASSERT_POINTER(child->l2_cache_core[0]);

	MALI_DEBUG_PRINT(4, ("parent->l2_cache_core: [0] = %p, [1] = %p\n", parent->l2_cache_core[0], parent->l2_cache_core[1]));
	MALI_DEBUG_PRINT(4, ("child->l2_cache_core: [0] = %p, [1] = %p\n", child->l2_cache_core[0], child->l2_cache_core[1]));

	/* Keep track of the L2 cache cores of child groups */
	found = MALI_FALSE;
	for (i = 0; i < 2; i++) {
		if (parent->l2_cache_core[i] == child->l2_cache_core[0]) {
			MALI_DEBUG_ASSERT(parent->l2_cache_core_ref_count[i] > 0);
			parent->l2_cache_core_ref_count[i]++;
			found = MALI_TRUE;
		}
	}

	if (!found) {
		/* First time we see this L2 cache, add it to our list */
		i = (NULL == parent->l2_cache_core[0]) ? 0 : 1;

		MALI_DEBUG_PRINT(4, ("First time we see l2_cache %p. Adding to [%d] = %p\n", child->l2_cache_core[0], i, parent->l2_cache_core[i]));

		MALI_DEBUG_ASSERT(NULL == parent->l2_cache_core[i]);

		parent->l2_cache_core[i] = child->l2_cache_core[0];
		parent->l2_cache_core_ref_count[i]++;
	}

	/* Update Broadcast Unit and DLBU */
	mali_bcast_add_group(parent->bcast_core, child);
	mali_dlbu_add_group(parent->dlbu_core, child);

	child_session = child->session;
	child->session = NULL;

	/* Above this comment, only software state is updated and the HW is not
	 * touched. Now, check if Mali is powered and skip the rest if it isn't
	 * powered.
	 */

	if (!update_hw) {
		MALI_DEBUG_CODE(mali_group_print_virtual(parent));
		return;
	}

	/* Update MMU */
	if (parent->session == child_session) {
		mali_mmu_zap_tlb(child->mmu);
	} else {
		if (NULL == parent->session) {
			mali_mmu_activate_empty_page_directory(child->mmu);
		} else {
			mali_mmu_activate_page_directory(child->mmu, mali_session_get_page_directory(parent->session));
		}
	}

	/* Update HW only if power is on */
	mali_bcast_reset(parent->bcast_core);
	mali_dlbu_update_mask(parent->dlbu_core);

	/* Start job on child when parent is active */
	if (NULL != parent->pp_running_job) {
		struct mali_pp_job *job = parent->pp_running_job;
		MALI_DEBUG_PRINT(3, ("Group %x joining running job %d on virtual group %x\n",
		                     child, mali_pp_job_get_id(job), parent));
		MALI_DEBUG_ASSERT(MALI_GROUP_STATE_WORKING == parent->state);
		mali_pp_job_start(child->pp_core, job, mali_pp_core_get_id(child->pp_core), MALI_TRUE);

		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE|
		                              MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(child->pp_core))|
		                              MALI_PROFILING_EVENT_REASON_SINGLE_HW_FLUSH,
		                              mali_pp_job_get_frame_builder_id(job), mali_pp_job_get_flush_id(job), 0, 0, 0);

		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START|
		                              MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(child->pp_core))|
		                              MALI_PROFILING_EVENT_REASON_START_STOP_HW_VIRTUAL,
		                              mali_pp_job_get_pid(job), mali_pp_job_get_tid(job), 0, 0, 0);
	}

	MALI_DEBUG_CODE(mali_group_print_virtual(parent);)
}

/**
 * @brief Remove child group from virtual group parent
 *
 * After the child is removed, it's state will be LEAVING_VIRTUAL and must be set
 * to IDLE before it can be used.
 */
void mali_group_remove_group(struct mali_group *parent, struct mali_group *child)
{
	u32 i;

	MALI_ASSERT_GROUP_LOCKED(parent);

	MALI_DEBUG_PRINT(3, ("Removing group %p from virtual group %p\n", child, parent));

	MALI_DEBUG_ASSERT(mali_group_is_virtual(parent));
	MALI_DEBUG_ASSERT(!mali_group_is_virtual(child));
	MALI_DEBUG_ASSERT(parent == child->parent_group);
	MALI_DEBUG_ASSERT(MALI_GROUP_STATE_IN_VIRTUAL == child->state);
	/* Removing groups while running is not yet supported. */
	MALI_DEBUG_ASSERT(MALI_GROUP_STATE_IDLE == parent->state);

	mali_group_lock(child);

	/* Update Broadcast Unit and DLBU */
	mali_bcast_remove_group(parent->bcast_core, child);
	mali_dlbu_remove_group(parent->dlbu_core, child);

	/* Update HW only if power is on */
	if (mali_pm_is_power_on()) {
		mali_bcast_reset(parent->bcast_core);
		mali_dlbu_update_mask(parent->dlbu_core);
	}

	_mali_osk_list_delinit(&child->group_list);

	child->session = parent->session;
	child->parent_group = NULL;
	child->state = MALI_GROUP_STATE_LEAVING_VIRTUAL;

	/* Keep track of the L2 cache cores of child groups */
	i = (child->l2_cache_core[0] == parent->l2_cache_core[0]) ? 0 : 1;

	MALI_DEBUG_ASSERT(child->l2_cache_core[0] == parent->l2_cache_core[i]);

	parent->l2_cache_core_ref_count[i]--;

	if (parent->l2_cache_core_ref_count[i] == 0) {
		parent->l2_cache_core[i] = NULL;
	}

	MALI_DEBUG_CODE(mali_group_print_virtual(parent));

	mali_group_unlock(child);
}

struct mali_group *mali_group_acquire_group(struct mali_group *parent)
{
	struct mali_group *child;

	MALI_ASSERT_GROUP_LOCKED(parent);

	MALI_DEBUG_ASSERT(mali_group_is_virtual(parent));
	MALI_DEBUG_ASSERT(!_mali_osk_list_empty(&parent->group_list));

	child = _MALI_OSK_LIST_ENTRY(parent->group_list.prev, struct mali_group, group_list);

	mali_group_remove_group(parent, child);

	return child;
}

void mali_group_reset(struct mali_group *group)
{
	/*
	 * This function should not be used to abort jobs,
	 * currently only called during insmod and PM resume
	 */
	MALI_DEBUG_ASSERT_LOCK_HELD(group->lock);
	MALI_DEBUG_ASSERT(NULL == group->gp_running_job);
	MALI_DEBUG_ASSERT(NULL == group->pp_running_job);

	group->session = NULL;

	if (NULL != group->dlbu_core) {
		mali_dlbu_reset(group->dlbu_core);
	}

	if (NULL != group->bcast_core) {
		mali_bcast_reset(group->bcast_core);
	}

	if (NULL != group->mmu) {
		mali_group_reset_mmu(group);
	}

	if (NULL != group->gp_core) {
		mali_gp_reset(group->gp_core);
	}

	if (NULL != group->pp_core) {
		mali_group_reset_pp(group);
	}
}

struct mali_gp_core* mali_group_get_gp_core(struct mali_group *group)
{
	return group->gp_core;
}

struct mali_pp_core* mali_group_get_pp_core(struct mali_group *group)
{
	return group->pp_core;
}

void mali_group_start_gp_job(struct mali_group *group, struct mali_gp_job *job)
{
	struct mali_session_data *session;

	MALI_ASSERT_GROUP_LOCKED(group);
	MALI_DEBUG_ASSERT(MALI_GROUP_STATE_IDLE == group->state);

	session = mali_gp_job_get_session(job);

	if (NULL != group->l2_cache_core[0]) {
		mali_l2_cache_invalidate_conditional(group->l2_cache_core[0], mali_gp_job_get_cache_order(job));
	}

	mali_group_activate_page_directory(group, session);

	mali_gp_job_start(group->gp_core, job);

	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
	                              MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(0) |
	                              MALI_PROFILING_EVENT_REASON_SINGLE_HW_FLUSH,
	                              mali_gp_job_get_frame_builder_id(job), mali_gp_job_get_flush_id(job), 0, 0, 0);
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START |
	                              MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(0),
	                              mali_gp_job_get_pid(job), mali_gp_job_get_tid(job), 0, 0, 0);
#if defined(CONFIG_MALI400_PROFILING)
	if ((MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src0(group->l2_cache_core[0])) &&
	    (MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src1(group->l2_cache_core[0])))
		mali_group_report_l2_cache_counters_per_core(group, 0);
#endif /* #if defined(CONFIG_MALI400_PROFILING) */

#if defined(CONFIG_GPU_TRACEPOINTS) && defined(CONFIG_TRACEPOINTS)
	trace_gpu_sched_switch(mali_gp_get_hw_core_desc(group->gp_core), sched_clock(),
	                       mali_gp_job_get_pid(job), 0, mali_gp_job_get_id(job));
#endif

	group->gp_running_job = job;
	group->state = MALI_GROUP_STATE_WORKING;

	/* Setup the timeout timer value and save the job id for the job running on the gp core */
	_mali_osk_timer_mod(group->timeout_timer, _mali_osk_time_mstoticks(mali_max_job_runtime));
}

void mali_group_start_pp_job(struct mali_group *group, struct mali_pp_job *job, u32 sub_job)
{
	struct mali_session_data *session;

	MALI_ASSERT_GROUP_LOCKED(group);
	MALI_DEBUG_ASSERT(MALI_GROUP_STATE_IDLE == group->state);

	session = mali_pp_job_get_session(job);

	if (NULL != group->l2_cache_core[0]) {
		mali_l2_cache_invalidate_conditional(group->l2_cache_core[0], mali_pp_job_get_cache_order(job));
	}

	if (NULL != group->l2_cache_core[1]) {
		mali_l2_cache_invalidate_conditional(group->l2_cache_core[1], mali_pp_job_get_cache_order(job));
	}

	mali_group_activate_page_directory(group, session);

	if (mali_group_is_virtual(group)) {
		struct mali_group *child;
		struct mali_group *temp;
		u32 core_num = 0;

		MALI_DEBUG_ASSERT( mali_pp_job_is_virtual(job));

		/* Configure DLBU for the job */
		mali_dlbu_config_job(group->dlbu_core, job);

		/* Write stack address for each child group */
		_MALI_OSK_LIST_FOREACHENTRY(child, temp, &group->group_list, struct mali_group, group_list) {
			mali_pp_write_addr_stack(child->pp_core, job);
			core_num++;
		}

		/* Try to use DMA unit to start job, fallback to writing directly to the core */
		MALI_DEBUG_ASSERT(mali_dma_cmd_buf_is_valid(&job->dma_cmd_buf));
		if (_MALI_OSK_ERR_OK != mali_dma_start(mali_dma_get_global_dma_core(), &job->dma_cmd_buf)) {
			mali_pp_job_start(group->pp_core, job, sub_job, MALI_FALSE);
		}
	} else {
		mali_pp_job_start(group->pp_core, job, sub_job, MALI_FALSE);
	}

	/* if the group is virtual, loop through physical groups which belong to this group
	 * and call profiling events for its cores as virtual */
	if (MALI_TRUE == mali_group_is_virtual(group)) {
		struct mali_group *child;
		struct mali_group *temp;

		_MALI_OSK_LIST_FOREACHENTRY(child, temp, &group->group_list, struct mali_group, group_list) {
			_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE|
			                              MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(child->pp_core))|
			                              MALI_PROFILING_EVENT_REASON_SINGLE_HW_FLUSH,
			                              mali_pp_job_get_frame_builder_id(job), mali_pp_job_get_flush_id(job), 0, 0, 0);

			_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START|
			                              MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(child->pp_core))|
			                              MALI_PROFILING_EVENT_REASON_START_STOP_HW_VIRTUAL,
			                              mali_pp_job_get_pid(job), mali_pp_job_get_tid(job), 0, 0, 0);
		}
#if defined(CONFIG_MALI400_PROFILING)
		if (0 != group->l2_cache_core_ref_count[0]) {
			if ((MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src0(group->l2_cache_core[0])) &&
			    (MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src1(group->l2_cache_core[0]))) {
				mali_group_report_l2_cache_counters_per_core(group, mali_l2_cache_get_id(group->l2_cache_core[0]));
			}
		}
		if (0 != group->l2_cache_core_ref_count[1]) {
			if ((MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src0(group->l2_cache_core[1])) &&
			    (MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src1(group->l2_cache_core[1]))) {
				mali_group_report_l2_cache_counters_per_core(group, mali_l2_cache_get_id(group->l2_cache_core[1]));
			}
		}
#endif /* #if defined(CONFIG_MALI400_PROFILING) */
	} else { /* group is physical - call profiling events for physical cores */
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE|
		                              MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(group->pp_core))|
		                              MALI_PROFILING_EVENT_REASON_SINGLE_HW_FLUSH,
		                              mali_pp_job_get_frame_builder_id(job), mali_pp_job_get_flush_id(job), 0, 0, 0);

		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START|
		                              MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(group->pp_core))|
		                              MALI_PROFILING_EVENT_REASON_START_STOP_HW_PHYSICAL,
		                              mali_pp_job_get_pid(job), mali_pp_job_get_tid(job), 0, 0, 0);
#if defined(CONFIG_MALI400_PROFILING)
		if ((MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src0(group->l2_cache_core[0])) &&
		    (MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src1(group->l2_cache_core[0]))) {
			mali_group_report_l2_cache_counters_per_core(group, mali_l2_cache_get_id(group->l2_cache_core[0]));
		}
#endif /* #if defined(CONFIG_MALI400_PROFILING) */
	}
#if defined(CONFIG_GPU_TRACEPOINTS) && defined(CONFIG_TRACEPOINTS)
	trace_gpu_sched_switch(mali_pp_get_hw_core_desc(group->pp_core), sched_clock(), mali_pp_job_get_tid(job), 0, mali_pp_job_get_id(job));
#endif
	group->pp_running_job = job;
	group->pp_running_sub_job = sub_job;
	group->state = MALI_GROUP_STATE_WORKING;

	/* Setup the timeout timer value and save the job id for the job running on the pp core */
	_mali_osk_timer_mod(group->timeout_timer, _mali_osk_time_mstoticks(mali_max_job_runtime));
}

struct mali_gp_job *mali_group_resume_gp_with_new_heap(struct mali_group *group, u32 job_id, u32 start_addr, u32 end_addr)
{
	MALI_ASSERT_GROUP_LOCKED(group);

	if (group->state != MALI_GROUP_STATE_OOM ||
	    mali_gp_job_get_id(group->gp_running_job) != job_id) {
		return NULL; /* Illegal request or job has already been aborted */
	}

	if (NULL != group->l2_cache_core[0]) {
		mali_l2_cache_invalidate(group->l2_cache_core[0]);
	}

	mali_mmu_zap_tlb_without_stall(group->mmu);

	mali_gp_resume_with_new_heap(group->gp_core, start_addr, end_addr);

	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_RESUME|MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(0), 0, 0, 0, 0, 0);

	group->state = MALI_GROUP_STATE_WORKING;

	return group->gp_running_job;
}

static void mali_group_reset_mmu(struct mali_group *group)
{
	struct mali_group *child;
	struct mali_group *temp;
	_mali_osk_errcode_t err;

	if (!mali_group_is_virtual(group)) {
		/* This is a physical group or an idle virtual group -- simply wait for
		 * the reset to complete. */
		err = mali_mmu_reset(group->mmu);
		MALI_DEBUG_ASSERT(_MALI_OSK_ERR_OK == err);
	} else { /* virtual group */
		err = mali_mmu_reset(group->mmu);
		if (_MALI_OSK_ERR_OK == err) {
			return;
		}

		/* Loop through all members of this virtual group and wait
		 * until they are done resetting.
		 */
		_MALI_OSK_LIST_FOREACHENTRY(child, temp, &group->group_list, struct mali_group, group_list) {
			err = mali_mmu_reset(child->mmu);
			MALI_DEBUG_ASSERT(_MALI_OSK_ERR_OK == err);
		}
	}
}

static void mali_group_reset_pp(struct mali_group *group)
{
	struct mali_group *child;
	struct mali_group *temp;

	mali_pp_reset_async(group->pp_core);

	if (!mali_group_is_virtual(group) || NULL == group->pp_running_job) {
		/* This is a physical group or an idle virtual group -- simply wait for
		 * the reset to complete. */
		mali_pp_reset_wait(group->pp_core);
	} else { /* virtual group */
		/* Loop through all members of this virtual group and wait until they
		 * are done resetting.
		 */
		_MALI_OSK_LIST_FOREACHENTRY(child, temp, &group->group_list, struct mali_group, group_list) {
			mali_pp_reset_wait(child->pp_core);
		}
	}
}

/* Group must be locked when entering this function.  Will be unlocked before exiting. */
static void mali_group_complete_pp_and_unlock(struct mali_group *group, mali_bool success, mali_bool in_upper_half)
{
	struct mali_pp_job *pp_job_to_return;
	u32 pp_sub_job_to_return;

	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->pp_core);
	MALI_DEBUG_ASSERT_POINTER(group->pp_running_job);
	MALI_ASSERT_GROUP_LOCKED(group);

	mali_group_post_process_job_pp(group);

	if (success) {
		/* Only do soft reset for successful jobs, a full recovery
		 * reset will be done for failed jobs. */
		mali_pp_reset_async(group->pp_core);
	}

	pp_job_to_return = group->pp_running_job;
	pp_sub_job_to_return = group->pp_running_sub_job;
	group->state = MALI_GROUP_STATE_IDLE;
	group->pp_running_job = NULL;

	if (!success) {
		MALI_DEBUG_PRINT(2, ("Mali group: Executing recovery reset due to job failure\n"));
		mali_group_recovery_reset(group);
	} else if (_MALI_OSK_ERR_OK != mali_pp_reset_wait(group->pp_core)) {
		MALI_PRINT_ERROR(("Mali group: Executing recovery reset due to reset failure\n"));
		mali_group_recovery_reset(group);
	}

	/* Return job to user, schedule and unlock group. */
	mali_pp_scheduler_job_done(group, pp_job_to_return, pp_sub_job_to_return, success, in_upper_half);
}

/* Group must be locked when entering this function.  Will be unlocked before exiting. */
static void mali_group_complete_gp_and_unlock(struct mali_group *group, mali_bool success)
{
	struct mali_gp_job *gp_job_to_return;

	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->gp_core);
	MALI_DEBUG_ASSERT_POINTER(group->gp_running_job);
	MALI_ASSERT_GROUP_LOCKED(group);

	mali_group_post_process_job_gp(group, MALI_FALSE);

	if (success) {
		/* Only do soft reset for successful jobs, a full recovery
		 * reset will be done for failed jobs. */
		mali_gp_reset_async(group->gp_core);
	}

	gp_job_to_return = group->gp_running_job;
	group->state = MALI_GROUP_STATE_IDLE;
	group->gp_running_job = NULL;

	if (!success) {
		MALI_DEBUG_PRINT(2, ("Mali group: Executing recovery reset due to job failure\n"));
		mali_group_recovery_reset(group);
	} else if (_MALI_OSK_ERR_OK != mali_gp_reset_wait(group->gp_core)) {
		MALI_PRINT_ERROR(("Mali group: Executing recovery reset due to reset failure\n"));
		mali_group_recovery_reset(group);
	}

	/* Return job to user, schedule and unlock group. */
	mali_gp_scheduler_job_done(group, gp_job_to_return, success);
}

void mali_group_abort_gp_job(struct mali_group *group, u32 job_id)
{
	MALI_ASSERT_GROUP_LOCKED(group);

	if (MALI_GROUP_STATE_IDLE == group->state ||
	    mali_gp_job_get_id(group->gp_running_job) != job_id) {
		return; /* No need to cancel or job has already been aborted or completed */
	}

	/* Function will unlock the group, so we need to lock it again */
	mali_group_complete_gp_and_unlock(group, MALI_FALSE);
	mali_group_lock(group);
}

static void mali_group_abort_pp_job(struct mali_group *group, u32 job_id)
{
	MALI_ASSERT_GROUP_LOCKED(group);

	if (MALI_GROUP_STATE_IDLE == group->state ||
	    mali_pp_job_get_id(group->pp_running_job) != job_id) {
		return; /* No need to cancel or job has already been aborted or completed */
	}

	mali_group_complete_pp_and_unlock(group, MALI_FALSE, MALI_FALSE);
	mali_group_lock(group);
}

void mali_group_abort_session(struct mali_group *group, struct mali_session_data *session)
{
	struct mali_gp_job *gp_job;
	struct mali_pp_job *pp_job;
	u32 gp_job_id = 0;
	u32 pp_job_id = 0;
	mali_bool abort_pp = MALI_FALSE;
	mali_bool abort_gp = MALI_FALSE;

	mali_group_lock(group);

	if (mali_group_is_in_virtual(group)) {
		/* Group is member of a virtual group, don't touch it! */
		mali_group_unlock(group);
		return;
	}

	gp_job = group->gp_running_job;
	pp_job = group->pp_running_job;

	if ((NULL != gp_job) && (mali_gp_job_get_session(gp_job) == session)) {
		MALI_DEBUG_PRINT(4, ("Aborting GP job 0x%08x from session 0x%08x\n", gp_job, session));

		gp_job_id = mali_gp_job_get_id(gp_job);
		abort_gp = MALI_TRUE;
	}

	if ((NULL != pp_job) && (mali_pp_job_get_session(pp_job) == session)) {
		MALI_DEBUG_PRINT(4, ("Mali group: Aborting PP job 0x%08x from session 0x%08x\n", pp_job, session));

		pp_job_id = mali_pp_job_get_id(pp_job);
		abort_pp = MALI_TRUE;
	}

	if (abort_gp) {
		mali_group_abort_gp_job(group, gp_job_id);
	}
	if (abort_pp) {
		mali_group_abort_pp_job(group, pp_job_id);
	}

	mali_group_remove_session_if_unused(group, session);

	mali_group_unlock(group);
}

struct mali_group *mali_group_get_glob_group(u32 index)
{
	if(mali_global_num_groups > index) {
		return mali_global_groups[index];
	}

	return NULL;
}

u32 mali_group_get_glob_num_groups(void)
{
	return mali_global_num_groups;
}

static void mali_group_activate_page_directory(struct mali_group *group, struct mali_session_data *session)
{
	MALI_ASSERT_GROUP_LOCKED(group);

	MALI_DEBUG_PRINT(5, ("Mali group: Activating page directory 0x%08X from session 0x%08X on group 0x%08X\n", mali_session_get_page_directory(session), session, group));

	if (group->session != session) {
		/* Different session than last time, so we need to do some work */
		MALI_DEBUG_PRINT(5, ("Mali group: Activate session: %08x previous: %08x on group 0x%08X\n", session, group->session, group));
		mali_mmu_activate_page_directory(group->mmu, mali_session_get_page_directory(session));
		group->session = session;
	} else {
		/* Same session as last time, so no work required */
		MALI_DEBUG_PRINT(4, ("Mali group: Activate existing session 0x%08X on group 0x%08X\n", session->page_directory, group));
		mali_mmu_zap_tlb_without_stall(group->mmu);
	}
}

static void mali_group_remove_session_if_unused(struct mali_group *group, struct mali_session_data *session)
{
	MALI_ASSERT_GROUP_LOCKED(group);

	if (MALI_GROUP_STATE_IDLE == group->state) {
		if (group->session == session) {
			MALI_DEBUG_ASSERT(MALI_GROUP_STATE_WORKING != group->state);
			MALI_DEBUG_ASSERT(MALI_TRUE == group->power_is_on);
			MALI_DEBUG_PRINT(3, ("Mali group: Deactivating unused session 0x%08X on group %08X\n", session, group));
			mali_mmu_activate_empty_page_directory(group->mmu);
			group->session = NULL;
		}
	}
}

mali_bool mali_group_power_is_on(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_LOCK_HELD(group->lock);
	return group->power_is_on;
}

void mali_group_power_on_group(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_LOCK_HELD(group->lock);
	MALI_DEBUG_ASSERT(   MALI_GROUP_STATE_IDLE       == group->state
	                     || MALI_GROUP_STATE_IN_VIRTUAL == group->state
	                     || MALI_GROUP_STATE_JOINING_VIRTUAL == group->state
	                     || MALI_GROUP_STATE_LEAVING_VIRTUAL == group->state
	                     || MALI_GROUP_STATE_DISABLED   == group->state);

	MALI_DEBUG_PRINT(3, ("Group %p powered on\n", group));

	group->power_is_on = MALI_TRUE;
}

void mali_group_power_off_group(struct mali_group *group, mali_bool do_power_change)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_LOCK_HELD(group->lock);
	MALI_DEBUG_ASSERT(   MALI_GROUP_STATE_IDLE       == group->state
	                     || MALI_GROUP_STATE_IN_VIRTUAL == group->state
	                     || MALI_GROUP_STATE_JOINING_VIRTUAL == group->state
	                     || MALI_GROUP_STATE_LEAVING_VIRTUAL == group->state
	                     || MALI_GROUP_STATE_DISABLED   == group->state);

	MALI_DEBUG_PRINT(3, ("Group %p powered off\n", group));

	/* It is necessary to set group->session = NULL so that the powered off MMU is not written
	 * to on map/unmap.  It is also necessary to set group->power_is_on = MALI_FALSE so that
	 * pending bottom_halves does not access powered off cores. */

	group->session = NULL;

	if (do_power_change) {
		group->power_is_on = MALI_FALSE;
	}
}

void mali_group_power_on(void)
{
	int i;
	for (i = 0; i < mali_global_num_groups; i++) {
		struct mali_group *group = mali_global_groups[i];

		mali_group_lock(group);
		if (MALI_GROUP_STATE_DISABLED == group->state) {
			MALI_DEBUG_ASSERT(MALI_FALSE == group->power_is_on);
		} else {
			mali_group_power_on_group(group);
		}
		mali_group_unlock(group);
	}
	MALI_DEBUG_PRINT(4, ("Mali Group: power on\n"));
}

void mali_group_power_off(mali_bool do_power_change)
{
	int i;

	for (i = 0; i < mali_global_num_groups; i++) {
		struct mali_group *group = mali_global_groups[i];

		mali_group_lock(group);
		if (MALI_GROUP_STATE_DISABLED == group->state) {
			MALI_DEBUG_ASSERT(MALI_FALSE == group->power_is_on);
		} else {
			mali_group_power_off_group(group, do_power_change);
		}
		mali_group_unlock(group);
	}
	MALI_DEBUG_PRINT(4, ("Mali Group: power off\n"));
}

static void mali_group_recovery_reset(struct mali_group *group)
{
	_mali_osk_errcode_t err;

	MALI_ASSERT_GROUP_LOCKED(group);

	/* Stop cores, bus stop */
	if (NULL != group->pp_core) {
		mali_pp_stop_bus(group->pp_core);
	} else {
		mali_gp_stop_bus(group->gp_core);
	}

	/* Flush MMU and clear page fault (if any) */
	mali_mmu_activate_fault_flush_page_directory(group->mmu);
	mali_mmu_page_fault_done(group->mmu);

	/* Wait for cores to stop bus, then do a hard reset on them */
	if (NULL != group->pp_core) {
		if (mali_group_is_virtual(group)) {
			struct mali_group *child, *temp;

			/* Disable the broadcast unit while we do reset directly on the member cores. */
			mali_bcast_disable(group->bcast_core);

			_MALI_OSK_LIST_FOREACHENTRY(child, temp, &group->group_list, struct mali_group, group_list) {
				mali_pp_stop_bus_wait(child->pp_core);
				mali_pp_hard_reset(child->pp_core);
			}

			mali_bcast_enable(group->bcast_core);
		} else {
			mali_pp_stop_bus_wait(group->pp_core);
			mali_pp_hard_reset(group->pp_core);
		}
	} else {
		mali_gp_stop_bus_wait(group->gp_core);
		mali_gp_hard_reset(group->gp_core);
	}

	/* Reset MMU */
	err = mali_mmu_reset(group->mmu);
	MALI_DEBUG_ASSERT(_MALI_OSK_ERR_OK == err);
	MALI_IGNORE(err);

	group->session = NULL;
}

#if MALI_STATE_TRACKING
u32 mali_group_dump_state(struct mali_group *group, char *buf, u32 size)
{
	int n = 0;

	n += _mali_osk_snprintf(buf + n, size - n, "Group: %p\n", group);
	n += _mali_osk_snprintf(buf + n, size - n, "\tstate: %d\n", group->state);
	if (group->gp_core) {
		n += mali_gp_dump_state(group->gp_core, buf + n, size - n);
		n += _mali_osk_snprintf(buf + n, size - n, "\tGP job: %p\n", group->gp_running_job);
	}
	if (group->pp_core) {
		n += mali_pp_dump_state(group->pp_core, buf + n, size - n);
		n += _mali_osk_snprintf(buf + n, size - n, "\tPP job: %p, subjob %d \n",
		                        group->pp_running_job, group->pp_running_sub_job);
	}

	return n;
}
#endif

/* Group must be locked when entering this function.  Will be unlocked before exiting. */
static void mali_group_mmu_page_fault_and_unlock(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_ASSERT_GROUP_LOCKED(group);

	if (NULL != group->pp_core) {
		struct mali_pp_job *pp_job_to_return;
		u32 pp_sub_job_to_return;

		MALI_DEBUG_ASSERT_POINTER(group->pp_running_job);

		mali_group_post_process_job_pp(group);

		pp_job_to_return = group->pp_running_job;
		pp_sub_job_to_return = group->pp_running_sub_job;
		group->state = MALI_GROUP_STATE_IDLE;
		group->pp_running_job = NULL;

		mali_group_recovery_reset(group); /* This will also clear the page fault itself */

		/* Will unlock group. */
		mali_pp_scheduler_job_done(group, pp_job_to_return, pp_sub_job_to_return, MALI_FALSE, MALI_FALSE);
	} else {
		struct mali_gp_job *gp_job_to_return;

		MALI_DEBUG_ASSERT_POINTER(group->gp_running_job);

		mali_group_post_process_job_gp(group, MALI_FALSE);

		gp_job_to_return = group->gp_running_job;
		group->state = MALI_GROUP_STATE_IDLE;
		group->gp_running_job = NULL;

		mali_group_recovery_reset(group); /* This will also clear the page fault itself */

		/* Will unlock group. */
		mali_gp_scheduler_job_done(group, gp_job_to_return, MALI_FALSE);
	}
}

_mali_osk_errcode_t mali_group_upper_half_mmu(void * data)
{
	_mali_osk_errcode_t err = _MALI_OSK_ERR_FAULT;
	struct mali_group *group = (struct mali_group *)data;
	struct mali_mmu_core *mmu = group->mmu;
	u32 int_stat;

	MALI_DEBUG_ASSERT_POINTER(mmu);

#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	if (MALI_FALSE == mali_pm_domain_lock_state(group->pm_domain)) {
		goto out;
	}
#endif

	/* Check if it was our device which caused the interrupt (we could be sharing the IRQ line) */
	int_stat = mali_mmu_get_int_status(mmu);
	if (0 != int_stat) {
		struct mali_group *parent = group->parent_group;

		/* page fault or bus error, we thread them both in the same way */
		mali_mmu_mask_all_interrupts(mmu);
		if (NULL == parent) {
			_mali_osk_wq_schedule_work(group->bottom_half_work_mmu);
		} else {
			_mali_osk_wq_schedule_work(parent->bottom_half_work_mmu);
		}
		err = _MALI_OSK_ERR_OK;
		goto out;
	}

out:
#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	mali_pm_domain_unlock_state(group->pm_domain);
#endif

	return err;
}

static void mali_group_bottom_half_mmu(void * data)
{
	struct mali_group *group = (struct mali_group *)data;
	struct mali_mmu_core *mmu = group->mmu;
	u32 rawstat;
	MALI_DEBUG_CODE(u32 status);

	MALI_DEBUG_ASSERT_POINTER(mmu);

	mali_group_lock(group);

	MALI_DEBUG_ASSERT(NULL == group->parent_group);

	if ( MALI_FALSE == mali_group_power_is_on(group) ) {
		MALI_PRINT_ERROR(("Interrupt bottom half of %s when core is OFF.", mmu->hw_core.description));
		mali_group_unlock(group);
		return;
	}

	rawstat = mali_mmu_get_rawstat(mmu);
	MALI_DEBUG_CODE(status = mali_mmu_get_status(mmu));

	MALI_DEBUG_PRINT(4, ("Mali MMU: Bottom half, interrupt 0x%08X, status 0x%08X\n", rawstat, status));

	if (rawstat & (MALI_MMU_INTERRUPT_PAGE_FAULT | MALI_MMU_INTERRUPT_READ_BUS_ERROR)) {
		/* An actual page fault has occurred. */
#ifdef DEBUG
		u32 fault_address = mali_mmu_get_page_fault_addr(mmu);
		MALI_DEBUG_PRINT(2,("Mali MMU: Page fault detected at 0x%x from bus id %d of type %s on %s\n",
		                    (void*)fault_address,
		                    (status >> 6) & 0x1F,
		                    (status & 32) ? "write" : "read",
		                    mmu->hw_core.description));
#endif

		mali_group_mmu_page_fault_and_unlock(group);
		return;
	}

	mali_group_unlock(group);
}

_mali_osk_errcode_t mali_group_upper_half_gp(void *data)
{
	_mali_osk_errcode_t err = _MALI_OSK_ERR_FAULT;
	struct mali_group *group = (struct mali_group *)data;
	struct mali_gp_core *core = group->gp_core;
	u32 irq_readout;

#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	if (MALI_FALSE == mali_pm_domain_lock_state(group->pm_domain)) {
		goto out;
	}
#endif

	irq_readout = mali_gp_get_int_stat(core);

	if (MALIGP2_REG_VAL_IRQ_MASK_NONE != irq_readout) {
		/* Mask out all IRQs from this core until IRQ is handled */
		mali_gp_mask_all_interrupts(core);

		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE|MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(0)|MALI_PROFILING_EVENT_REASON_SINGLE_HW_INTERRUPT, irq_readout, 0, 0, 0, 0);

		/* We do need to handle this in a bottom half */
		_mali_osk_wq_schedule_work(group->bottom_half_work_gp);

		err = _MALI_OSK_ERR_OK;
		goto out;
	}

out:
#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	mali_pm_domain_unlock_state(group->pm_domain);
#endif

	return err;
}

static void mali_group_bottom_half_gp(void *data)
{
	struct mali_group *group = (struct mali_group *)data;
	u32 irq_readout;
	u32 irq_errors;

	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START|MALI_PROFILING_EVENT_CHANNEL_SOFTWARE|MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF, 0, _mali_osk_get_tid(), MALI_PROFILING_MAKE_EVENT_DATA_CORE_GP(0), 0, 0);

	mali_group_lock(group);

	if ( MALI_FALSE == mali_group_power_is_on(group) ) {
		MALI_PRINT_ERROR(("Mali group: Interrupt bottom half of %s when core is OFF.", mali_gp_get_hw_core_desc(group->gp_core)));
		mali_group_unlock(group);
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP|MALI_PROFILING_EVENT_CHANNEL_SOFTWARE|MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF, 0, _mali_osk_get_tid(), 0, 0, 0);
		return;
	}

	irq_readout = mali_gp_read_rawstat(group->gp_core);

	MALI_DEBUG_PRINT(4, ("Mali group: GP bottom half IRQ 0x%08X from core %s\n", irq_readout, mali_gp_get_hw_core_desc(group->gp_core)));

	if (irq_readout & (MALIGP2_REG_VAL_IRQ_VS_END_CMD_LST|MALIGP2_REG_VAL_IRQ_PLBU_END_CMD_LST)) {
		u32 core_status = mali_gp_read_core_status(group->gp_core);
		if (0 == (core_status & MALIGP2_REG_VAL_STATUS_MASK_ACTIVE)) {
			MALI_DEBUG_PRINT(4, ("Mali group: GP job completed, calling group handler\n"));
			group->core_timed_out = MALI_FALSE;
			_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
			                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
			                              MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
			                              0, _mali_osk_get_tid(), 0, 0, 0);

			mali_group_complete_gp_and_unlock(group, MALI_TRUE);
			return;
		}
	}

	/*
	 * Now lets look at the possible error cases (IRQ indicating error or timeout)
	 * END_CMD_LST, HANG and PLBU_OOM interrupts are not considered error.
	 */
	irq_errors = irq_readout & ~(MALIGP2_REG_VAL_IRQ_VS_END_CMD_LST|MALIGP2_REG_VAL_IRQ_PLBU_END_CMD_LST|MALIGP2_REG_VAL_IRQ_HANG|MALIGP2_REG_VAL_IRQ_PLBU_OUT_OF_MEM);
	if (0 != irq_errors) {
		MALI_PRINT_ERROR(("Mali group: Unknown interrupt 0x%08X from core %s, aborting job\n", irq_readout, mali_gp_get_hw_core_desc(group->gp_core)));
		group->core_timed_out = MALI_FALSE;
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
		                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
		                              MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
		                              0, _mali_osk_get_tid(), 0, 0, 0);

		mali_group_complete_gp_and_unlock(group, MALI_FALSE);
		mali_group_error++;
		return;
	} else if (group->core_timed_out) { /* SW timeout */
		group->core_timed_out = MALI_FALSE;
		if (!_mali_osk_timer_pending(group->timeout_timer) && NULL != group->gp_running_job) {
			MALI_PRINT(("Mali group: Job %d timed out\n", mali_gp_job_get_id(group->gp_running_job)));

			mali_group_complete_gp_and_unlock(group, MALI_FALSE);
			mali_group_error++;
			return;
		}
	} else if (irq_readout & MALIGP2_REG_VAL_IRQ_PLBU_OUT_OF_MEM) {
		/* GP wants more memory in order to continue. */
		MALI_DEBUG_PRINT(3, ("Mali group: PLBU needs more heap memory\n"));

		group->state = MALI_GROUP_STATE_OOM;
		mali_group_unlock(group); /* Nothing to do on the HW side, so just release group lock right away */
		mali_gp_scheduler_oom(group, group->gp_running_job);
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP|MALI_PROFILING_EVENT_CHANNEL_SOFTWARE|MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF, 0, _mali_osk_get_tid(), 0, 0, 0);
		return;
	}

	/*
	 * The only way to get here is if we only got one of two needed END_CMD_LST
	 * interrupts. Enable all but not the complete interrupt that has been
	 * received and continue to run.
	 */
	mali_gp_enable_interrupts(group->gp_core, irq_readout & (MALIGP2_REG_VAL_IRQ_PLBU_END_CMD_LST|MALIGP2_REG_VAL_IRQ_VS_END_CMD_LST));
	mali_group_unlock(group);

	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP|MALI_PROFILING_EVENT_CHANNEL_SOFTWARE|MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF, 0, _mali_osk_get_tid(), 0, 0, 0);
}

static void mali_group_post_process_job_gp(struct mali_group *group, mali_bool suspend)
{
	/* Stop the timeout timer. */
	_mali_osk_timer_del_async(group->timeout_timer);

	if (NULL == group->gp_running_job) {
		/* Nothing to do */
		return;
	}

	mali_gp_update_performance_counters(group->gp_core, group->gp_running_job, suspend);

#if defined(CONFIG_MALI400_PROFILING)
	if (suspend) {
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SUSPEND|MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(0),
		                              mali_gp_job_get_perf_counter_value0(group->gp_running_job),
		                              mali_gp_job_get_perf_counter_value1(group->gp_running_job),
		                              mali_gp_job_get_perf_counter_src0(group->gp_running_job) | (mali_gp_job_get_perf_counter_src1(group->gp_running_job) << 8),
		                              0, 0);
	} else {
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP|MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(0),
		                              mali_gp_job_get_perf_counter_value0(group->gp_running_job),
		                              mali_gp_job_get_perf_counter_value1(group->gp_running_job),
		                              mali_gp_job_get_perf_counter_src0(group->gp_running_job) | (mali_gp_job_get_perf_counter_src1(group->gp_running_job) << 8),
		                              0, 0);

		if ((MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src0(group->l2_cache_core[0])) &&
		    (MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src1(group->l2_cache_core[0])))
			mali_group_report_l2_cache_counters_per_core(group, 0);
	}
#endif

	mali_gp_job_set_current_heap_addr(group->gp_running_job,
	                                  mali_gp_read_plbu_alloc_start_addr(group->gp_core));
}

_mali_osk_errcode_t mali_group_upper_half_pp(void *data)
{
	_mali_osk_errcode_t err = _MALI_OSK_ERR_FAULT;
	struct mali_group *group = (struct mali_group *)data;
	struct mali_pp_core *core = group->pp_core;
	u32 irq_readout;

#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	if (MALI_FALSE == mali_pm_domain_lock_state(group->pm_domain)) {
		goto out;
	}
#endif

	/*
	 * For Mali-450 there is one particular case we need to watch out for:
	 *
	 * Criteria 1) this function call can be due to a shared interrupt,
	 * and not necessary because this core signaled an interrupt.
	 * Criteria 2) this core is a part of a virtual group, and thus it should
	 * not do any post processing.
	 * Criteria 3) this core has actually indicated that is has completed by
	 * having set raw_stat/int_stat registers to != 0
	 *
	 * If all this criteria is meet, then we could incorrectly start post
	 * processing on the wrong group object (this should only happen on the
	 * parent group)
	 */
#if !defined(MALI_UPPER_HALF_SCHEDULING)
	if (mali_group_is_in_virtual(group)) {
		/*
		 * This check is done without the group lock held, which could lead to
		 * a potential race. This is however ok, since we will safely re-check
		 * this with the group lock held at a later stage. This is just an
		 * early out which will strongly benefit shared IRQ systems.
		 */
		err = _MALI_OSK_ERR_OK;
		goto out;
	}
#endif

	irq_readout = mali_pp_get_int_stat(core);
	if (MALI200_REG_VAL_IRQ_MASK_NONE != irq_readout) {
		/* Mask out all IRQs from this core until IRQ is handled */
		mali_pp_mask_all_interrupts(core);

#if defined(CONFIG_MALI400_PROFILING)
		/* Currently no support for this interrupt event for the virtual PP core */
		if (!mali_group_is_virtual(group)) {
			_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
			                              MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(core->core_id) |
			                              MALI_PROFILING_EVENT_REASON_SINGLE_HW_INTERRUPT,
			                              irq_readout, 0, 0, 0, 0);
		}
#endif

#if defined(MALI_UPPER_HALF_SCHEDULING)
		/* Check if job is complete without errors */
		if (MALI200_REG_VAL_IRQ_END_OF_FRAME == irq_readout) {
			_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START |
			                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
			                              MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
			                              0, 0, MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP(core->core_id), 0, 0);

			MALI_DEBUG_PRINT(3, ("Mali PP: Job completed, calling group handler from upper half\n"));

			mali_group_lock(group);

			/* Check if job is complete without errors, again, after taking the group lock */
			irq_readout = mali_pp_read_rawstat(core);
			if (MALI200_REG_VAL_IRQ_END_OF_FRAME != irq_readout) {
				mali_pp_enable_interrupts(core);
				mali_group_unlock(group);
				_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
				                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
				                              MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
				                              0, 0, MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP(core->core_id), 0, 0);
				err = _MALI_OSK_ERR_OK;
				goto out;
			}

			if (mali_group_is_virtual(group)) {
				u32 status_readout = mali_pp_read_status(group->pp_core);
				if (status_readout & MALI200_REG_VAL_STATUS_RENDERING_ACTIVE) {
					MALI_DEBUG_PRINT(6, ("Mali PP: Not all cores in broadcast completed\n"));
					mali_pp_enable_interrupts(core);
					mali_group_unlock(group);
					_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
					                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
					                              MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
					                              0, 0, MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP(core->core_id), 0, 0);
					err = _MALI_OSK_ERR_OK;
					goto out;
				}
			}

			if (mali_group_is_in_virtual(group)) {
				/* We're member of a virtual group, so interrupt should be handled by the virtual group */
				mali_pp_enable_interrupts(core);
				mali_group_unlock(group);
				_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
				                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
				                              MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
				                              0, 0, MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP(core->core_id), 0, 0);
				err =  _MALI_OSK_ERR_FAULT;
				goto out;
			}

			group->core_timed_out = MALI_FALSE;

			mali_group_complete_pp_and_unlock(group, MALI_TRUE, MALI_TRUE);

			/* No need to enable interrupts again, since the core will be reset while completing the job */

			MALI_DEBUG_PRINT(6, ("Mali PP: Upper half job done\n"));

			_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
			                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
			                              MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
			                              0, 0, MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP(core->core_id), 0, 0);

			err = _MALI_OSK_ERR_OK;
			goto out;
		}
#endif

		/* We do need to handle this in a bottom half */
		_mali_osk_wq_schedule_work(group->bottom_half_work_pp);
		err = _MALI_OSK_ERR_OK;
		goto out;
	}

out:
#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	mali_pm_domain_unlock_state(group->pm_domain);
#endif

	return err;
}

static void mali_group_bottom_half_pp(void *data)
{
	struct mali_group *group = (struct mali_group *)data;
	struct mali_pp_core *core = group->pp_core;
	u32 irq_readout;
	u32 irq_errors;

	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START |
	                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
	                              MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
	                              0, _mali_osk_get_tid(), MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP(core->core_id), 0, 0);

	mali_group_lock(group);

	if (mali_group_is_in_virtual(group)) {
		/* We're member of a virtual group, so interrupt should be handled by the virtual group */
		mali_pp_enable_interrupts(core);
		mali_group_unlock(group);
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
		                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
		                              MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
		                              0, _mali_osk_get_tid(), 0, 0, 0);
		return;
	}

	if ( MALI_FALSE == mali_group_power_is_on(group) ) {
		MALI_PRINT_ERROR(("Interrupt bottom half of %s when core is OFF.", mali_pp_get_hw_core_desc(core)));
		mali_group_unlock(group);
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
		                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
		                              MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
		                              0, _mali_osk_get_tid(), 0, 0, 0);
		return;
	}

	irq_readout = mali_pp_read_rawstat(group->pp_core);

	MALI_DEBUG_PRINT(4, ("Mali PP: Bottom half IRQ 0x%08X from core %s\n", irq_readout, mali_pp_get_hw_core_desc(group->pp_core)));

	/* Check if job is complete without errors */
	if (MALI200_REG_VAL_IRQ_END_OF_FRAME == irq_readout) {
		if (mali_group_is_virtual(group)) {
			u32 status_readout = mali_pp_read_status(group->pp_core);

			if (status_readout & MALI200_REG_VAL_STATUS_RENDERING_ACTIVE && !group->core_timed_out) {
				MALI_DEBUG_PRINT(6, ("Mali PP: Not all cores in broadcast completed\n"));
				mali_pp_enable_interrupts(core);
				mali_group_unlock(group);

				_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
				                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
				                              MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
				                              0, _mali_osk_get_tid(), 0, 0, 0);
				return;
			}
		}

		if (!group->core_timed_out) {
			MALI_DEBUG_PRINT(3, ("Mali PP: Job completed, calling group handler\n"));
			group->core_timed_out = MALI_FALSE;

			mali_group_complete_pp_and_unlock(group, MALI_TRUE, MALI_FALSE);

			_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
			                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
			                              MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
			                              0, _mali_osk_get_tid(), 0, 0, 0);
			return;
		}
	}

	/*
	 * Now lets look at the possible error cases (IRQ indicating error or timeout)
	 * END_OF_FRAME and HANG interrupts are not considered error.
	 */
	irq_errors = irq_readout & ~(MALI200_REG_VAL_IRQ_END_OF_FRAME|MALI200_REG_VAL_IRQ_HANG);
	if (0 != irq_errors) {
		MALI_PRINT_ERROR(("Mali PP: Unexpected interrupt 0x%08X from core %s, aborting job\n",
		                  irq_readout, mali_pp_get_hw_core_desc(group->pp_core)));
		group->core_timed_out = MALI_FALSE;

		mali_group_complete_pp_and_unlock(group, MALI_FALSE, MALI_FALSE);

		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
		                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
		                              MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
		                              0, _mali_osk_get_tid(), 0, 0, 0);
		mali_group_error++;
		return;
	} else if (group->core_timed_out) { /* SW timeout */
		group->core_timed_out = MALI_FALSE;
		if (!_mali_osk_timer_pending(group->timeout_timer) && NULL != group->pp_running_job) {
			MALI_PRINT(("Mali PP: Job %d timed out on core %s\n",
			            mali_pp_job_get_id(group->pp_running_job), mali_pp_get_hw_core_desc(core)));

			mali_group_complete_pp_and_unlock(group, MALI_FALSE, MALI_FALSE);
		} else {
			mali_group_unlock(group);
		}

		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
		                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
		                              MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
		                              0, _mali_osk_get_tid(), 0, 0, 0);
		mali_group_error++;
		return;
	}

	/*
	 * We should never get here, re-enable interrupts and continue
	 */
	if (0 == irq_readout) {
		MALI_DEBUG_PRINT(3, ("Mali group: No interrupt found on core %s\n",
		                     mali_pp_get_hw_core_desc(group->pp_core)));
	} else {
		MALI_PRINT_ERROR(("Mali group: Unhandled PP interrupt 0x%08X on %s\n", irq_readout,
		                  mali_pp_get_hw_core_desc(group->pp_core)));
	}
	mali_pp_enable_interrupts(core);
	mali_group_unlock(group);

	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
	                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
	                              MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
	                              0, _mali_osk_get_tid(), 0, 0, 0);
}

static void mali_group_post_process_job_pp(struct mali_group *group)
{
	MALI_ASSERT_GROUP_LOCKED(group);

	/* Stop the timeout timer. */
	_mali_osk_timer_del_async(group->timeout_timer);

	if (NULL != group->pp_running_job) {
		if (MALI_TRUE == mali_group_is_virtual(group)) {
			struct mali_group *child;
			struct mali_group *temp;

			/* update performance counters from each physical pp core within this virtual group */
			_MALI_OSK_LIST_FOREACHENTRY(child, temp, &group->group_list, struct mali_group, group_list) {
				mali_pp_update_performance_counters(group->pp_core, child->pp_core, group->pp_running_job, mali_pp_core_get_id(child->pp_core));
			}

#if defined(CONFIG_MALI400_PROFILING)
			/* send profiling data per physical core */
			_MALI_OSK_LIST_FOREACHENTRY(child, temp, &group->group_list, struct mali_group, group_list) {
				_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP|
				                              MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(child->pp_core))|
				                              MALI_PROFILING_EVENT_REASON_START_STOP_HW_VIRTUAL,
				                              mali_pp_job_get_perf_counter_value0(group->pp_running_job, mali_pp_core_get_id(child->pp_core)),
				                              mali_pp_job_get_perf_counter_value1(group->pp_running_job, mali_pp_core_get_id(child->pp_core)),
				                              mali_pp_job_get_perf_counter_src0(group->pp_running_job, group->pp_running_sub_job) | (mali_pp_job_get_perf_counter_src1(group->pp_running_job, group->pp_running_sub_job) << 8),
				                              0, 0);
			}
			if (0 != group->l2_cache_core_ref_count[0]) {
				if ((MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src0(group->l2_cache_core[0])) &&
				    (MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src1(group->l2_cache_core[0]))) {
					mali_group_report_l2_cache_counters_per_core(group, mali_l2_cache_get_id(group->l2_cache_core[0]));
				}
			}
			if (0 != group->l2_cache_core_ref_count[1]) {
				if ((MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src0(group->l2_cache_core[1])) &&
				    (MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src1(group->l2_cache_core[1]))) {
					mali_group_report_l2_cache_counters_per_core(group, mali_l2_cache_get_id(group->l2_cache_core[1]));
				}
			}

#endif
		} else {
			/* update performance counters for a physical group's pp core */
			mali_pp_update_performance_counters(group->pp_core, group->pp_core, group->pp_running_job, group->pp_running_sub_job);

#if defined(CONFIG_MALI400_PROFILING)
			_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP|
			                              MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(group->pp_core))|
			                              MALI_PROFILING_EVENT_REASON_START_STOP_HW_PHYSICAL,
			                              mali_pp_job_get_perf_counter_value0(group->pp_running_job, group->pp_running_sub_job),
			                              mali_pp_job_get_perf_counter_value1(group->pp_running_job, group->pp_running_sub_job),
			                              mali_pp_job_get_perf_counter_src0(group->pp_running_job, group->pp_running_sub_job) | (mali_pp_job_get_perf_counter_src1(group->pp_running_job, group->pp_running_sub_job) << 8),
			                              0, 0);

			if ((MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src0(group->l2_cache_core[0])) &&
			    (MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src1(group->l2_cache_core[0]))) {
				mali_group_report_l2_cache_counters_per_core(group, mali_l2_cache_get_id(group->l2_cache_core[0]));
			}
#endif
		}
	}
}

static void mali_group_timeout(void *data)
{
	struct mali_group *group = (struct mali_group *)data;

	group->core_timed_out = MALI_TRUE;

	if (NULL != group->gp_core) {
		MALI_DEBUG_PRINT(2, ("Mali group: TIMEOUT on %s\n", mali_gp_get_hw_core_desc(group->gp_core)));
		_mali_osk_wq_schedule_work(group->bottom_half_work_gp);
	} else {
		MALI_DEBUG_PRINT(2, ("Mali group: TIMEOUT on %s\n", mali_pp_get_hw_core_desc(group->pp_core)));
		_mali_osk_wq_schedule_work(group->bottom_half_work_pp);
	}
}

void mali_group_zap_session(struct mali_group *group, struct mali_session_data *session)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(session);

	/* Early out - safe even if mutex is not held */
	if (group->session != session) return;

	mali_group_lock(group);

	mali_group_remove_session_if_unused(group, session);

	if (group->session == session) {
		/* The Zap also does the stall and disable_stall */
		mali_bool zap_success = mali_mmu_zap_tlb(group->mmu);
		if (MALI_TRUE != zap_success) {
			MALI_DEBUG_PRINT(2, ("Mali memory unmap failed. Doing pagefault handling.\n"));

			mali_group_mmu_page_fault_and_unlock(group);
			return;
		}
	}

	mali_group_unlock(group);
}

#if defined(CONFIG_MALI400_PROFILING)
static void mali_group_report_l2_cache_counters_per_core(struct mali_group *group, u32 core_num)
{
	u32 source0 = 0;
	u32 value0 = 0;
	u32 source1 = 0;
	u32 value1 = 0;
	u32 profiling_channel = 0;

	switch(core_num) {
	case 0:
		profiling_channel = MALI_PROFILING_EVENT_TYPE_SINGLE |
		                    MALI_PROFILING_EVENT_CHANNEL_GPU |
		                    MALI_PROFILING_EVENT_REASON_SINGLE_GPU_L20_COUNTERS;
		break;
	case 1:
		profiling_channel = MALI_PROFILING_EVENT_TYPE_SINGLE |
		                    MALI_PROFILING_EVENT_CHANNEL_GPU |
		                    MALI_PROFILING_EVENT_REASON_SINGLE_GPU_L21_COUNTERS;
		break;
	case 2:
		profiling_channel = MALI_PROFILING_EVENT_TYPE_SINGLE |
		                    MALI_PROFILING_EVENT_CHANNEL_GPU |
		                    MALI_PROFILING_EVENT_REASON_SINGLE_GPU_L22_COUNTERS;
		break;
	default:
		profiling_channel = MALI_PROFILING_EVENT_TYPE_SINGLE |
		                    MALI_PROFILING_EVENT_CHANNEL_GPU |
		                    MALI_PROFILING_EVENT_REASON_SINGLE_GPU_L20_COUNTERS;
		break;
	}

	if (0 == core_num) {
		mali_l2_cache_core_get_counter_values(group->l2_cache_core[0], &source0, &value0, &source1, &value1);
	}
	if (1 == core_num) {
		if (1 == mali_l2_cache_get_id(group->l2_cache_core[0])) {
			mali_l2_cache_core_get_counter_values(group->l2_cache_core[0], &source0, &value0, &source1, &value1);
		} else if (1 == mali_l2_cache_get_id(group->l2_cache_core[1])) {
			mali_l2_cache_core_get_counter_values(group->l2_cache_core[1], &source0, &value0, &source1, &value1);
		}
	}
	if (2 == core_num) {
		if (2 == mali_l2_cache_get_id(group->l2_cache_core[0])) {
			mali_l2_cache_core_get_counter_values(group->l2_cache_core[0], &source0, &value0, &source1, &value1);
		} else if (2 == mali_l2_cache_get_id(group->l2_cache_core[1])) {
			mali_l2_cache_core_get_counter_values(group->l2_cache_core[1], &source0, &value0, &source1, &value1);
		}
	}

	_mali_osk_profiling_add_event(profiling_channel, source1 << 8 | source0, value0, value1, 0, 0);
}
#endif /* #if defined(CONFIG_MALI400_PROFILING) */

mali_bool mali_group_is_enabled(struct mali_group *group)
{
	mali_bool enabled = MALI_TRUE;

	MALI_DEBUG_ASSERT_POINTER(group);

	mali_group_lock(group);
	if (MALI_GROUP_STATE_DISABLED == group->state) {
		enabled = MALI_FALSE;
	}
	mali_group_unlock(group);

	return enabled;
}

void mali_group_enable(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT(   NULL != mali_group_get_pp_core(group)
	                     || NULL != mali_group_get_gp_core(group));

	if (NULL != mali_group_get_pp_core(group)) {
		mali_pp_scheduler_enable_group(group);
	} else {
		mali_gp_scheduler_enable_group(group);
	}
}

void mali_group_disable(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT(   NULL != mali_group_get_pp_core(group)
	                     || NULL != mali_group_get_gp_core(group));

	if (NULL != mali_group_get_pp_core(group)) {
		mali_pp_scheduler_disable_group(group);
	} else {
		mali_gp_scheduler_disable_group(group);
	}
}

static struct mali_pm_domain* mali_group_get_l2_domain(struct mali_group *group)
{
	MALI_DEBUG_ASSERT(NULL == group->l2_cache_core[1]);

	/* l2_cache_core[0] stores the related l2 domain */
	return group->l2_cache_core[0]->pm_domain;
}

void mali_group_get_pm_domain_ref(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);

	/* Get group used l2 domain ref */
	mali_pm_domain_ref_get(mali_group_get_l2_domain(group));
	/* Get group used core domain ref */
	mali_pm_domain_ref_get(group->pm_domain);
}

void mali_group_put_pm_domain_ref(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);

	/* Put group used core domain ref */
	mali_pm_domain_ref_put(group->pm_domain);
	/* Put group used l2 domain ref */
	mali_pm_domain_ref_put(mali_group_get_l2_domain(group));
}
