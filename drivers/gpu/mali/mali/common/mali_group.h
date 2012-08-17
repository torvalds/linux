/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_GROUP_H__
#define __MALI_GROUP_H__

#include "linux/jiffies.h"
#include "mali_osk.h"
#include "mali_cluster.h"
#include "mali_mmu.h"
#include "mali_gp.h"
#include "mali_pp.h"
#include "mali_session.h"

/* max runtime [ms] for a core job - used by timeout timers  */
#define MAX_RUNTIME 5000
/** @brief A mali group object represents a MMU and a PP and/or a GP core.
 *
 */
#define MALI_MAX_NUMBER_OF_GROUPS 9

struct mali_group;

enum mali_group_event_t
{
	GROUP_EVENT_PP_JOB_COMPLETED,  /**< PP job completed successfully */
	GROUP_EVENT_PP_JOB_FAILED,     /**< PP job completed with failure */
	GROUP_EVENT_PP_JOB_TIMED_OUT,  /**< PP job reached max runtime */
	GROUP_EVENT_GP_JOB_COMPLETED,  /**< GP job completed successfully */
	GROUP_EVENT_GP_JOB_FAILED,     /**< GP job completed with failure */
	GROUP_EVENT_GP_JOB_TIMED_OUT,  /**< GP job reached max runtime */
	GROUP_EVENT_GP_OOM,            /**< GP job ran out of heap memory */
	GROUP_EVENT_MMU_PAGE_FAULT,    /**< MMU page fault */
};

enum mali_group_core_state
{
	MALI_GROUP_CORE_STATE_IDLE,
	MALI_GROUP_CORE_STATE_WORKING,
	MALI_GROUP_CORE_STATE_OOM
};

/** @brief Create a new Mali group object
 *
 * @param cluster Pointer to the cluster to which the group is connected.
 * @param mmu Pointer to the MMU that defines this group
 * @return A pointer to a new group object
 */
struct mali_group *mali_group_create(struct mali_cluster *cluster, struct mali_mmu_core *mmu);
void mali_group_add_gp_core(struct mali_group *group, struct mali_gp_core* gp_core);
void mali_group_add_pp_core(struct mali_group *group, struct mali_pp_core* pp_core);
void mali_group_delete(struct mali_group *group);

/** @brief Reset group
 *
 * This function will reset the entire group, including all the cores present in the group.
 *
 * @param group Pointer to the group to reset
 */
void mali_group_reset(struct mali_group *group);

/** @brief Get pointer to GP core object
 */
struct mali_gp_core* mali_group_get_gp_core(struct mali_group *group);

/** @brief Get pointer to PP core object
 */
struct mali_pp_core* mali_group_get_pp_core(struct mali_group *group);

/** @brief Lock group object
 *
 * Most group functions will lock the group object themselves. The expection is
 * the group_bottom_half which requires the group to be locked on entry.
 *
 * @param group Pointer to group to lock
 */
void mali_group_lock(struct mali_group *group);

/** @brief Unlock group object
 *
 * @param group Pointer to group to unlock
 */
void mali_group_unlock(struct mali_group *group);
#ifdef DEBUG
void mali_group_assert_locked(struct mali_group *group);
#define MALI_ASSERT_GROUP_LOCKED(group) mali_group_assert_locked(group)
#else
#define MALI_ASSERT_GROUP_LOCKED(group)
#endif

/** @brief Start GP job
 */
_mali_osk_errcode_t mali_group_start_gp_job(struct mali_group *group, struct mali_gp_job *job);
/** @brief Start fragment of PP job
 */
_mali_osk_errcode_t mali_group_start_pp_job(struct mali_group *group, struct mali_pp_job *job, u32 sub_job);

/** @brief Resume GP job that suspended waiting for more heap memory
 */
void mali_group_resume_gp_with_new_heap(struct mali_group *group, u32 job_id, u32 start_addr, u32 end_addr);
/** @brief Abort GP job
 *
 * Used to abort suspended OOM jobs when user space failed to allocte more memory.
 */
void mali_group_abort_gp_job(struct mali_group *group, u32 job_id);
/** @brief Abort all GP jobs from \a session
 *
 * Used on session close when terminating all running and queued jobs from \a session.
 */
void mali_group_abort_session(struct mali_group *group, struct mali_session_data *session);

enum mali_group_core_state mali_group_gp_state(struct mali_group *group);
enum mali_group_core_state mali_group_pp_state(struct mali_group *group);

/** @brief The common group bottom half interrupt handler
 *
 * This is only called from the GP and PP bottom halves.
 *
 * The action taken is dictated by the \a event.
 *
 * @param event The event code
 */
void mali_group_bottom_half(struct mali_group *group, enum mali_group_event_t event);

struct mali_mmu_core *mali_group_get_mmu(struct mali_group *group);
struct mali_session_data *mali_group_get_session(struct mali_group *group);

void mali_group_remove_session_if_unused(struct mali_group *group, struct mali_session_data *session_data);

void mali_group_power_on(void);
void mali_group_power_off(void);
mali_bool mali_group_power_is_on(struct mali_group *group);

struct mali_group *mali_group_get_glob_group(u32 index);
u32 mali_group_get_glob_num_groups(void);

u32 mali_group_dump_state(struct mali_group *group, char *buf, u32 size);

#endif /* __MALI_GROUP_H__ */
