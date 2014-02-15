/*
 * Copyright (C) 2011-2013 ARM Limited. All rights reserved.
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
#include "mali_l2_cache.h"
#include "mali_mmu.h"
#include "mali_gp.h"
#include "mali_pp.h"
#include "mali_session.h"

/**
 * @brief Default max runtime [ms] for a core job - used by timeout timers
 */
#define MALI_MAX_JOB_RUNTIME_DEFAULT 4000

/** @brief A mali group object represents a MMU and a PP and/or a GP core.
 *
 */
#define MALI_MAX_NUMBER_OF_GROUPS 10

enum mali_group_core_state {
	MALI_GROUP_STATE_IDLE,
	MALI_GROUP_STATE_WORKING,
	MALI_GROUP_STATE_OOM,
	MALI_GROUP_STATE_IN_VIRTUAL,
	MALI_GROUP_STATE_JOINING_VIRTUAL,
	MALI_GROUP_STATE_LEAVING_VIRTUAL,
	MALI_GROUP_STATE_DISABLED,
};

/* Forward declaration from mali_pm_domain.h */
struct mali_pm_domain;

/**
 * The structure represents a render group
 * A render group is defined by all the cores that share the same Mali MMU
 */

struct mali_group {
	struct mali_mmu_core        *mmu;
	struct mali_session_data    *session;

	mali_bool                   power_is_on;
	enum mali_group_core_state  state;

	struct mali_gp_core         *gp_core;
	struct mali_gp_job          *gp_running_job;

	struct mali_pp_core         *pp_core;
	struct mali_pp_job          *pp_running_job;
	u32                         pp_running_sub_job;

	struct mali_l2_cache_core   *l2_cache_core[2];
	u32                         l2_cache_core_ref_count[2];

	struct mali_dlbu_core       *dlbu_core;
	struct mali_bcast_unit      *bcast_core;

#ifdef MALI_UPPER_HALF_SCHEDULING
	_mali_osk_spinlock_irq_t	*lock;
#else
	_mali_osk_spinlock_t		*lock;
#endif

	_mali_osk_list_t            pp_scheduler_list;

	/* List used for virtual groups. For a virtual group, the list represents the
	 * head element. */
	_mali_osk_list_t            group_list;

	struct mali_group           *pm_domain_list;
	struct mali_pm_domain       *pm_domain;

	/* Parent virtual group (if any) */
	struct mali_group           *parent_group;

	_mali_osk_wq_work_t         *bottom_half_work_mmu;
	_mali_osk_wq_work_t         *bottom_half_work_gp;
	_mali_osk_wq_work_t         *bottom_half_work_pp;

	_mali_osk_timer_t           *timeout_timer;
	mali_bool                   core_timed_out;
};

/** @brief Create a new Mali group object
 *
 * @param cluster Pointer to the cluster to which the group is connected.
 * @param mmu Pointer to the MMU that defines this group
 * @return A pointer to a new group object
 */
struct mali_group *mali_group_create(struct mali_l2_cache_core *core,
                                     struct mali_dlbu_core *dlbu,
                                     struct mali_bcast_unit *bcast);

_mali_osk_errcode_t mali_group_add_mmu_core(struct mali_group *group, struct mali_mmu_core* mmu_core);
void mali_group_remove_mmu_core(struct mali_group *group);

_mali_osk_errcode_t mali_group_add_gp_core(struct mali_group *group, struct mali_gp_core* gp_core);
void mali_group_remove_gp_core(struct mali_group *group);

_mali_osk_errcode_t mali_group_add_pp_core(struct mali_group *group, struct mali_pp_core* pp_core);
void mali_group_remove_pp_core(struct mali_group *group);

void mali_group_set_pm_domain(struct mali_group *group, struct mali_pm_domain *domain);

void mali_group_delete(struct mali_group *group);

/** @brief Virtual groups */
void mali_group_add_group(struct mali_group *parent, struct mali_group *child, mali_bool update_hw);
void mali_group_remove_group(struct mali_group *parent, struct mali_group *child);
struct mali_group *mali_group_acquire_group(struct mali_group *parent);

MALI_STATIC_INLINE mali_bool mali_group_is_virtual(struct mali_group *group)
{
#if defined(CONFIG_MALI450)
	return (NULL != group->dlbu_core);
#else
	return MALI_FALSE;
#endif
}

/** @brief Check if a group is considered as part of a virtual group
 *
 * @note A group is considered to be "part of" a virtual group also during the transition
 *       in to / out of the virtual group.
 */
MALI_STATIC_INLINE mali_bool mali_group_is_in_virtual(struct mali_group *group)
{
#if defined(CONFIG_MALI450)
	return (MALI_GROUP_STATE_IN_VIRTUAL == group->state ||
	        MALI_GROUP_STATE_JOINING_VIRTUAL == group->state ||
	        MALI_GROUP_STATE_LEAVING_VIRTUAL == group->state);
#else
	return MALI_FALSE;
#endif
}

/** @brief Reset group
 *
 * This function will reset the entire group, including all the cores present in the group.
 *
 * @param group Pointer to the group to reset
 */
void mali_group_reset(struct mali_group *group);

/** @brief Zap MMU TLB on all groups
 *
 * Zap TLB on group if \a session is active.
 */
void mali_group_zap_session(struct mali_group* group, struct mali_session_data *session);

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
void mali_group_start_gp_job(struct mali_group *group, struct mali_gp_job *job);
/** @brief Start fragment of PP job
 */
void mali_group_start_pp_job(struct mali_group *group, struct mali_pp_job *job, u32 sub_job);

/** @brief Resume GP job that suspended waiting for more heap memory
 */
struct mali_gp_job *mali_group_resume_gp_with_new_heap(struct mali_group *group, u32 job_id, u32 start_addr, u32 end_addr);
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

mali_bool mali_group_power_is_on(struct mali_group *group);
void mali_group_power_on_group(struct mali_group *group);
void mali_group_power_off_group(struct mali_group *group, mali_bool power_status);
void mali_group_power_on(void);

/** @brief Prepare group for power off
 *
 * Update the group's state and prepare for the group to be powered off.
 *
 * If do_power_change is MALI_FALSE group session will be set to NULL so that
 * no more activity will happen to this group, but the power state flag will be
 * left unchanged.
 *
 * @do_power_change MALI_TRUE if power status is to be updated
 */
void mali_group_power_off(mali_bool do_power_change);

struct mali_group *mali_group_get_glob_group(u32 index);
u32 mali_group_get_glob_num_groups(void);

u32 mali_group_dump_state(struct mali_group *group, char *buf, u32 size);

/* MMU-related functions */
_mali_osk_errcode_t mali_group_upper_half_mmu(void * data);

/* GP-related functions */
_mali_osk_errcode_t mali_group_upper_half_gp(void *data);

/* PP-related functions */
_mali_osk_errcode_t mali_group_upper_half_pp(void *data);

/** @brief Check if group is enabled
 *
 * @param group group to check
 * @return MALI_TRUE if enabled, MALI_FALSE if not
 */
mali_bool mali_group_is_enabled(struct mali_group *group);

/** @brief Enable group
 *
 * An enabled job is put on the idle scheduler list and can be used to handle jobs.  Does nothing if
 * group is already enabled.
 *
 * @param group group to enable
 */
void mali_group_enable(struct mali_group *group);

/** @brief Disable group
 *
 * A disabled group will no longer be used by the scheduler.  If part of a virtual group, the group
 * will be removed before being disabled.  Cores part of a disabled group is safe to power down.
 *
 * @param group group to disable
 */
void mali_group_disable(struct mali_group *group);

MALI_STATIC_INLINE mali_bool mali_group_virtual_disable_if_empty(struct mali_group *group)
{
	mali_bool empty = MALI_FALSE;

	MALI_ASSERT_GROUP_LOCKED(group);
	MALI_DEBUG_ASSERT(mali_group_is_virtual(group));

	if (_mali_osk_list_empty(&group->group_list)) {
		group->state = MALI_GROUP_STATE_DISABLED;
		group->session = NULL;

		empty = MALI_TRUE;
	}

	return empty;
}

MALI_STATIC_INLINE mali_bool mali_group_virtual_enable_if_empty(struct mali_group *group)
{
	mali_bool empty = MALI_FALSE;

	MALI_ASSERT_GROUP_LOCKED(group);
	MALI_DEBUG_ASSERT(mali_group_is_virtual(group));

	if (_mali_osk_list_empty(&group->group_list)) {
		MALI_DEBUG_ASSERT(MALI_GROUP_STATE_DISABLED == group->state);

		group->state = MALI_GROUP_STATE_IDLE;

		empty = MALI_TRUE;
	}

	return empty;
}

/* Get group used l2 domain and core domain ref */
void mali_group_get_pm_domain_ref(struct mali_group *group);
/* Put group used l2 domain and core domain ref */
void mali_group_put_pm_domain_ref(struct mali_group *group);

#endif /* __MALI_GROUP_H__ */
