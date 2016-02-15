/*
 * Copyright (C) 2011-2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_GROUP_H__
#define __MALI_GROUP_H__

#include "mali_osk.h"
#include "mali_l2_cache.h"
#include "mali_mmu.h"
#include "mali_gp.h"
#include "mali_pp.h"
#include "mali_session.h"
#include "mali_osk_profiling.h"

/**
 * @brief Default max runtime [ms] for a core job - used by timeout timers
 */
#define MALI_MAX_JOB_RUNTIME_DEFAULT 5000

extern int mali_max_job_runtime;

#define MALI_MAX_NUMBER_OF_GROUPS 10
#define MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS 8

enum mali_group_state {
	MALI_GROUP_STATE_INACTIVE,
	MALI_GROUP_STATE_ACTIVATION_PENDING,
	MALI_GROUP_STATE_ACTIVE,
};

/**
 * The structure represents a render group
 * A render group is defined by all the cores that share the same Mali MMU
 */

struct mali_group {
	struct mali_mmu_core        *mmu;
	struct mali_session_data    *session;

	enum mali_group_state        state;
	mali_bool                    power_is_on;

	mali_bool                    is_working;
	unsigned long                start_time; /* in ticks */

	struct mali_gp_core         *gp_core;
	struct mali_gp_job          *gp_running_job;

	struct mali_pp_core         *pp_core;
	struct mali_pp_job          *pp_running_job;
	u32                         pp_running_sub_job;

	struct mali_pm_domain       *pm_domain;

	struct mali_l2_cache_core   *l2_cache_core[2];
	u32                         l2_cache_core_ref_count[2];

	/* Parent virtual group (if any) */
	struct mali_group           *parent_group;

	struct mali_dlbu_core       *dlbu_core;
	struct mali_bcast_unit      *bcast_core;

	/* Used for working groups which needs to be disabled */
	mali_bool                    disable_requested;

	/* Used by group to link child groups (for virtual group) */
	_mali_osk_list_t            group_list;

	/* Used by executor module in order to link groups of same state */
	_mali_osk_list_t            executor_list;

	/* Used by PM domains to link groups of same domain */
	_mali_osk_list_t             pm_domain_list;

	_mali_osk_wq_work_t         *bottom_half_work_mmu;
	_mali_osk_wq_work_t         *bottom_half_work_gp;
	_mali_osk_wq_work_t         *bottom_half_work_pp;

	_mali_osk_timer_t           *timeout_timer;
};

/** @brief Create a new Mali group object
 *
 * @return A pointer to a new group object
 */
struct mali_group *mali_group_create(struct mali_l2_cache_core *core,
				     struct mali_dlbu_core *dlbu,
				     struct mali_bcast_unit *bcast,
				     u32 domain_index);

void mali_group_dump_status(struct mali_group *group);

void mali_group_delete(struct mali_group *group);

_mali_osk_errcode_t mali_group_add_mmu_core(struct mali_group *group,
		struct mali_mmu_core *mmu_core);
void mali_group_remove_mmu_core(struct mali_group *group);

_mali_osk_errcode_t mali_group_add_gp_core(struct mali_group *group,
		struct mali_gp_core *gp_core);
void mali_group_remove_gp_core(struct mali_group *group);

_mali_osk_errcode_t mali_group_add_pp_core(struct mali_group *group,
		struct mali_pp_core *pp_core);
void mali_group_remove_pp_core(struct mali_group *group);

MALI_STATIC_INLINE const char *mali_group_core_description(
	struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	if (NULL != group->pp_core) {
		return mali_pp_core_description(group->pp_core);
	} else {
		MALI_DEBUG_ASSERT_POINTER(group->gp_core);
		return mali_gp_core_description(group->gp_core);
	}
}

MALI_STATIC_INLINE mali_bool mali_group_is_virtual(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);

#if (defined(CONFIG_MALI450) || defined(CONFIG_MALI470))
	return (NULL != group->dlbu_core);
#else
	return MALI_FALSE;
#endif
}

/** @brief Check if a group is a part of a virtual group or not
 */
MALI_STATIC_INLINE mali_bool mali_group_is_in_virtual(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

#if (defined(CONFIG_MALI450) || defined(CONFIG_MALI470))
	return (NULL != group->parent_group) ? MALI_TRUE : MALI_FALSE;
#else
	return MALI_FALSE;
#endif
}

/** @brief Reset group
 *
 * This function will reset the entire group,
 * including all the cores present in the group.
 *
 * @param group Pointer to the group to reset
 */
void mali_group_reset(struct mali_group *group);

MALI_STATIC_INLINE struct mali_session_data *mali_group_get_session(
	struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	return group->session;
}

MALI_STATIC_INLINE void mali_group_clear_session(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	if (NULL != group->session) {
		mali_mmu_activate_empty_page_directory(group->mmu);
		group->session = NULL;
	}
}

enum mali_group_state mali_group_activate(struct mali_group *group);

/*
 * Change state from ACTIVATION_PENDING to ACTIVE
 * For virtual group, all childs need to be ACTIVE first
 */
mali_bool mali_group_set_active(struct mali_group *group);

/*
 * @return MALI_TRUE means one or more domains can now be powered off,
 * and caller should call either mali_pm_update_async() or
 * mali_pm_update_sync() in order to do so.
 */
mali_bool mali_group_deactivate(struct mali_group *group);

MALI_STATIC_INLINE enum mali_group_state mali_group_get_state(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	return group->state;
}

MALI_STATIC_INLINE mali_bool mali_group_power_is_on(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	return group->power_is_on;
}

void mali_group_power_up(struct mali_group *group);
void mali_group_power_down(struct mali_group *group);

MALI_STATIC_INLINE void mali_group_set_disable_request(
	struct mali_group *group, mali_bool disable)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	group->disable_requested = disable;

	/**
	 * When one of child group's disable_requeset is set TRUE, then
	 * the disable_request of parent group should also be set to TRUE.
	 * While, the disable_request of parent group should only be set to FALSE
	 * only when all of its child group's disable_request are set to FALSE.
	 */
	if (NULL != group->parent_group && MALI_TRUE == disable) {
		group->parent_group->disable_requested = disable;
	}
}

MALI_STATIC_INLINE mali_bool mali_group_disable_requested(
	struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	return group->disable_requested;
}

/** @brief Virtual groups */
void mali_group_add_group(struct mali_group *parent, struct mali_group *child);
struct mali_group *mali_group_acquire_group(struct mali_group *parent);
void mali_group_remove_group(struct mali_group *parent, struct mali_group *child);

/** @brief Checks if the group is working.
 */
MALI_STATIC_INLINE mali_bool mali_group_is_working(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	if (mali_group_is_in_virtual(group)) {
		struct mali_group *tmp_group = mali_executor_get_virtual_group();
		return tmp_group->is_working;
	}
	return group->is_working;
}

MALI_STATIC_INLINE struct mali_gp_job *mali_group_get_running_gp_job(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	return group->gp_running_job;
}

/** @brief Zap MMU TLB on all groups
 *
 * Zap TLB on group if \a session is active.
 */
mali_bool mali_group_zap_session(struct mali_group *group,
				 struct mali_session_data *session);

/** @brief Get pointer to GP core object
 */
MALI_STATIC_INLINE struct mali_gp_core *mali_group_get_gp_core(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	return group->gp_core;
}

/** @brief Get pointer to PP core object
 */
MALI_STATIC_INLINE struct mali_pp_core *mali_group_get_pp_core(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	return group->pp_core;
}

/** @brief Start GP job
 */
void mali_group_start_gp_job(struct mali_group *group, struct mali_gp_job *job);

void mali_group_start_pp_job(struct mali_group *group, struct mali_pp_job *job, u32 sub_job);

/** @brief Start virtual group Job on a virtual group
*/
void mali_group_start_job_on_virtual(struct mali_group *group, struct mali_pp_job *job, u32 first_subjob, u32 last_subjob);


/** @brief Start a subjob from a particular on a specific PP group
*/
void mali_group_start_job_on_group(struct mali_group *group, struct mali_pp_job *job, u32 subjob);


/** @brief remove all the unused groups in tmp_unused group  list, so that the group is in consistent status.
 */
void mali_group_non_dlbu_job_done_virtual(struct mali_group *group);


/** @brief Resume GP job that suspended waiting for more heap memory
 */
void mali_group_resume_gp_with_new_heap(struct mali_group *group, u32 job_id, u32 start_addr, u32 end_addr);

MALI_STATIC_INLINE enum mali_interrupt_result mali_group_get_interrupt_result_gp(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->gp_core);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	return mali_gp_get_interrupt_result(group->gp_core);
}

MALI_STATIC_INLINE enum mali_interrupt_result mali_group_get_interrupt_result_pp(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->pp_core);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	return mali_pp_get_interrupt_result(group->pp_core);
}

MALI_STATIC_INLINE enum mali_interrupt_result mali_group_get_interrupt_result_mmu(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->mmu);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	return mali_mmu_get_interrupt_result(group->mmu);
}

MALI_STATIC_INLINE mali_bool mali_group_gp_is_active(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->gp_core);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	return mali_gp_is_active(group->gp_core);
}

MALI_STATIC_INLINE mali_bool mali_group_pp_is_active(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->pp_core);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	return mali_pp_is_active(group->pp_core);
}

MALI_STATIC_INLINE mali_bool mali_group_has_timed_out(struct mali_group *group)
{
	unsigned long time_cost;
	struct mali_group *tmp_group = group;

	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	/* if the group is in virtual need to use virtual_group's start time */
	if (mali_group_is_in_virtual(group)) {
		tmp_group = mali_executor_get_virtual_group();
	}

	time_cost = _mali_osk_time_tickcount() - tmp_group->start_time;
	if (_mali_osk_time_mstoticks(mali_max_job_runtime) <= time_cost) {
		/*
		 * current tick is at or after timeout end time,
		 * so this is a valid timeout
		 */
		return MALI_TRUE;
	} else {
		/*
		 * Not a valid timeout. A HW interrupt probably beat
		 * us to it, and the timer wasn't properly deleted
		 * (async deletion used due to atomic context).
		 */
		return MALI_FALSE;
	}
}

MALI_STATIC_INLINE void mali_group_mask_all_interrupts_gp(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->gp_core);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	return mali_gp_mask_all_interrupts(group->gp_core);
}

MALI_STATIC_INLINE void mali_group_mask_all_interrupts_pp(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->pp_core);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	return mali_pp_mask_all_interrupts(group->pp_core);
}

MALI_STATIC_INLINE void mali_group_enable_interrupts_gp(
	struct mali_group *group,
	enum mali_interrupt_result exceptions)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->gp_core);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	mali_gp_enable_interrupts(group->gp_core, exceptions);
}

MALI_STATIC_INLINE void mali_group_schedule_bottom_half_gp(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->gp_core);
	_mali_osk_wq_schedule_work(group->bottom_half_work_gp);
}


MALI_STATIC_INLINE void mali_group_schedule_bottom_half_pp(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->pp_core);
	_mali_osk_wq_schedule_work(group->bottom_half_work_pp);
}

MALI_STATIC_INLINE void mali_group_schedule_bottom_half_mmu(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->mmu);
	_mali_osk_wq_schedule_work(group->bottom_half_work_mmu);
}

struct mali_pp_job *mali_group_complete_pp(struct mali_group *group, mali_bool success, u32 *sub_job);

struct mali_gp_job *mali_group_complete_gp(struct mali_group *group, mali_bool success);

#if defined(CONFIG_MALI400_PROFILING)
MALI_STATIC_INLINE void mali_group_oom(struct mali_group *group)
{
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SUSPEND |
				      MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(0),
				      0, 0, 0, 0, 0);
}
#endif

struct mali_group *mali_group_get_glob_group(u32 index);
u32 mali_group_get_glob_num_groups(void);

u32 mali_group_dump_state(struct mali_group *group, char *buf, u32 size);


_mali_osk_errcode_t mali_group_upper_half_mmu(void *data);
_mali_osk_errcode_t mali_group_upper_half_gp(void *data);
_mali_osk_errcode_t mali_group_upper_half_pp(void *data);

MALI_STATIC_INLINE mali_bool mali_group_is_empty(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT(mali_group_is_virtual(group));
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	return _mali_osk_list_empty(&group->group_list);
}

#endif /* __MALI_GROUP_H__ */
