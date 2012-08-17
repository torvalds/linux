/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_group.h"
#include "mali_osk.h"
#include "mali_cluster.h"
#include "mali_gp.h"
#include "mali_pp.h"
#include "mali_mmu.h"
#include "mali_gp_scheduler.h"
#include "mali_pp_scheduler.h"
#include "mali_pm.h"

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

/**
 * The structure represents a render group
 * A render group is defined by all the cores that share the same Mali MMU
 */

struct mali_group
{
	struct mali_cluster *cluster;

	struct mali_mmu_core *mmu;
	struct mali_session_data *session;
	int page_dir_ref_count;
	mali_bool power_is_on;
#if defined(USING_MALI200)
	mali_bool pagedir_activation_failed;
#endif

	struct mali_gp_core         *gp_core;
	enum mali_group_core_state  gp_state;
	struct mali_gp_job          *gp_running_job;

	struct mali_pp_core         *pp_core;
	enum mali_group_core_state  pp_state;
	struct mali_pp_job          *pp_running_job;
	u32                         pp_running_sub_job;

	_mali_osk_lock_t *lock;
};

static struct mali_group *mali_global_groups[MALI_MAX_NUMBER_OF_GROUPS];
static u32 mali_global_num_groups = 0;

enum mali_group_activate_pd_status
{
	MALI_GROUP_ACTIVATE_PD_STATUS_FAILED,
	MALI_GROUP_ACTIVATE_PD_STATUS_OK_KEPT_PD,
	MALI_GROUP_ACTIVATE_PD_STATUS_OK_SWITCHED_PD,
};

/* local helper functions */
static enum mali_group_activate_pd_status mali_group_activate_page_directory(struct mali_group *group, struct mali_session_data *session);
static void mali_group_deactivate_page_directory(struct mali_group *group, struct mali_session_data *session);
static void mali_group_recovery_reset(struct mali_group *group);
static void mali_group_complete_jobs(struct mali_group *group, mali_bool complete_gp, mali_bool complete_pp, bool success);

void mali_group_lock(struct mali_group *group)
{
	if(_MALI_OSK_ERR_OK != _mali_osk_lock_wait(group->lock, _MALI_OSK_LOCKMODE_RW))
	{
		/* Non-interruptable lock failed: this should never happen. */
		MALI_DEBUG_ASSERT(0);
	}
	MALI_DEBUG_PRINT(5, ("Mali group: Group lock taken 0x%08X\n", group));
}

void mali_group_unlock(struct mali_group *group)
{
	MALI_DEBUG_PRINT(5, ("Mali group: Releasing group lock 0x%08X\n", group));
	_mali_osk_lock_signal(group->lock, _MALI_OSK_LOCKMODE_RW);
}

#ifdef DEBUG
void mali_group_assert_locked(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_LOCK_HELD(group->lock);
}
#endif


struct mali_group *mali_group_create(struct mali_cluster *cluster, struct mali_mmu_core *mmu)
{
	struct mali_group *group = NULL;

	if (mali_global_num_groups >= MALI_MAX_NUMBER_OF_GROUPS)
	{
		MALI_PRINT_ERROR(("Mali group: Too many group objects created\n"));
		return NULL;
	}

	group = _mali_osk_malloc(sizeof(struct mali_group));
	if (NULL != group)
	{
		_mali_osk_memset(group, 0, sizeof(struct mali_group));
		group->lock = _mali_osk_lock_init(_MALI_OSK_LOCKFLAG_ORDERED | _MALI_OSK_LOCKFLAG_SPINLOCK_IRQ |_MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, 0, _MALI_OSK_LOCK_ORDER_GROUP);
		if (NULL != group->lock)
		{
			group->cluster = cluster;
			group->mmu = mmu; /* This group object now owns the MMU object */
			group->session = NULL;
			group->page_dir_ref_count = 0;
			group->power_is_on = MALI_TRUE;

			group->gp_state = MALI_GROUP_CORE_STATE_IDLE;
			group->pp_state = MALI_GROUP_CORE_STATE_IDLE;
#if defined(USING_MALI200)
			group->pagedir_activation_failed = MALI_FALSE;
#endif
			mali_global_groups[mali_global_num_groups] = group;
			mali_global_num_groups++;

			return group;
		}
		_mali_osk_free(group);
	}

	return NULL;
}

void mali_group_add_gp_core(struct mali_group *group, struct mali_gp_core* gp_core)
{
	/* This group object now owns the GP core object */
	group->gp_core = gp_core;
}

void mali_group_add_pp_core(struct mali_group *group, struct mali_pp_core* pp_core)
{
	/* This group object now owns the PP core object */
	group->pp_core = pp_core;
}

void mali_group_delete(struct mali_group *group)
{
	u32 i;

	/* Delete the resources that this group owns */
	if (NULL != group->gp_core)
	{
		mali_gp_delete(group->gp_core);
	}

	if (NULL != group->pp_core)
	{
		mali_pp_delete(group->pp_core);
	}

	if (NULL != group->mmu)
	{
		mali_mmu_delete(group->mmu);
	}

	for (i = 0; i < mali_global_num_groups; i++)
	{
		if (mali_global_groups[i] == group)
		{
			mali_global_groups[i] = NULL;
			mali_global_num_groups--;
			break;
		}
	}

	_mali_osk_lock_term(group->lock);

	_mali_osk_free(group);
}

/* Called from mali_cluster_reset() when the system is re-turned on */
void mali_group_reset(struct mali_group *group)
{
	mali_group_lock(group);

	group->session = NULL;

	if (NULL != group->mmu)
	{
		mali_mmu_reset(group->mmu);
	}

	if (NULL != group->gp_core)
	{
		mali_gp_reset(group->gp_core);
	}

	if (NULL != group->pp_core)
	{
		mali_pp_reset(group->pp_core);
	}

	mali_group_unlock(group);
}

struct mali_gp_core* mali_group_get_gp_core(struct mali_group *group)
{
	return group->gp_core;
}

struct mali_pp_core* mali_group_get_pp_core(struct mali_group *group)
{
	return group->pp_core;
}

_mali_osk_errcode_t mali_group_start_gp_job(struct mali_group *group, struct mali_gp_job *job)
{
	struct mali_session_data *session;
	enum mali_group_activate_pd_status activate_status;

	MALI_DEBUG_ASSERT(MALI_GROUP_CORE_STATE_IDLE == group->gp_state);

	mali_pm_core_event(MALI_CORE_EVENT_GP_START);

	session = mali_gp_job_get_session(job);

	mali_group_lock(group);

	mali_cluster_l2_cache_invalidate_all(group->cluster, mali_gp_job_get_id(job));

	activate_status = mali_group_activate_page_directory(group, session);
	if (MALI_GROUP_ACTIVATE_PD_STATUS_FAILED != activate_status)
	{
		/* if session is NOT kept Zapping is done as part of session switch */
		if (MALI_GROUP_ACTIVATE_PD_STATUS_OK_KEPT_PD == activate_status)
		{
			mali_mmu_zap_tlb_without_stall(group->mmu);
		}
		mali_gp_job_start(group->gp_core, job);
		group->gp_running_job = job;
		group->gp_state = MALI_GROUP_CORE_STATE_WORKING;

		mali_group_unlock(group);

		return _MALI_OSK_ERR_OK;
	}

#if defined(USING_MALI200)
	group->pagedir_activation_failed = MALI_TRUE;
#endif

	mali_group_unlock(group);

	mali_pm_core_event(MALI_CORE_EVENT_GP_STOP); /* Failed to start, so "cancel" the MALI_CORE_EVENT_GP_START */
	return _MALI_OSK_ERR_FAULT;
}

_mali_osk_errcode_t mali_group_start_pp_job(struct mali_group *group, struct mali_pp_job *job, u32 sub_job)
{
	struct mali_session_data *session;
	enum mali_group_activate_pd_status activate_status;

	MALI_DEBUG_ASSERT(MALI_GROUP_CORE_STATE_IDLE == group->pp_state);

	mali_pm_core_event(MALI_CORE_EVENT_PP_START);

	session = mali_pp_job_get_session(job);

	mali_group_lock(group);

	mali_cluster_l2_cache_invalidate_all(group->cluster, mali_pp_job_get_id(job));

	activate_status = mali_group_activate_page_directory(group, session);
	if (MALI_GROUP_ACTIVATE_PD_STATUS_FAILED != activate_status)
	{
		/* if session is NOT kept Zapping is done as part of session switch */
		if (MALI_GROUP_ACTIVATE_PD_STATUS_OK_KEPT_PD == activate_status)
		{
			MALI_DEBUG_PRINT(3, ("PP starting job PD_Switch 0 Flush 1 Zap 1\n"));
			mali_mmu_zap_tlb_without_stall(group->mmu);
		}
		mali_pp_job_start(group->pp_core, job, sub_job);
		group->pp_running_job = job;
		group->pp_running_sub_job = sub_job;
		group->pp_state = MALI_GROUP_CORE_STATE_WORKING;

		mali_group_unlock(group);

		return _MALI_OSK_ERR_OK;
	}

#if defined(USING_MALI200)
	group->pagedir_activation_failed = MALI_TRUE;
#endif

	mali_group_unlock(group);

	mali_pm_core_event(MALI_CORE_EVENT_PP_STOP); /* Failed to start, so "cancel" the MALI_CORE_EVENT_PP_START */
	return _MALI_OSK_ERR_FAULT;
}

void mali_group_resume_gp_with_new_heap(struct mali_group *group, u32 job_id, u32 start_addr, u32 end_addr)
{
	mali_group_lock(group);

	if (group->gp_state != MALI_GROUP_CORE_STATE_OOM ||
	    mali_gp_job_get_id(group->gp_running_job) != job_id)
	{
		mali_group_unlock(group);
		return; /* Illegal request or job has already been aborted */
	}

	mali_cluster_l2_cache_invalidate_all_force(group->cluster);
	mali_mmu_zap_tlb_without_stall(group->mmu);

	mali_gp_resume_with_new_heap(group->gp_core, start_addr, end_addr);
	group->gp_state = MALI_GROUP_CORE_STATE_WORKING;

	mali_group_unlock(group);
}

void mali_group_abort_gp_job(struct mali_group *group, u32 job_id)
{
	mali_group_lock(group);

	if (group->gp_state == MALI_GROUP_CORE_STATE_IDLE ||
	    mali_gp_job_get_id(group->gp_running_job) != job_id)
	{
		mali_group_unlock(group);
		return; /* No need to cancel or job has already been aborted or completed */
	}

	mali_group_complete_jobs(group, MALI_TRUE, MALI_FALSE, MALI_FALSE); /* Will release group lock */
}

void mali_group_abort_pp_job(struct mali_group *group, u32 job_id)
{
	mali_group_lock(group);

	if (group->pp_state == MALI_GROUP_CORE_STATE_IDLE ||
	    mali_pp_job_get_id(group->pp_running_job) != job_id)
	{
		mali_group_unlock(group);
		return; /* No need to cancel or job has already been aborted or completed */
	}

	mali_group_complete_jobs(group, MALI_FALSE, MALI_TRUE, MALI_FALSE); /* Will release group lock */
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

	gp_job = group->gp_running_job;
	pp_job = group->pp_running_job;

	if (gp_job && mali_gp_job_get_session(gp_job) == session)
	{
		MALI_DEBUG_PRINT(4, ("Aborting GP job 0x%08x from session 0x%08x\n", gp_job, session));

		gp_job_id = mali_gp_job_get_id(gp_job);
		abort_gp = MALI_TRUE;
	}

	if (pp_job && mali_pp_job_get_session(pp_job) == session)
	{
		MALI_DEBUG_PRINT(4, ("Mali group: Aborting PP job 0x%08x from session 0x%08x\n", pp_job, session));

		pp_job_id = mali_pp_job_get_id(pp_job);
		abort_pp = MALI_TRUE;
	}

	mali_group_unlock(group);

	/* These functions takes and releases the group lock */
	if (0 != abort_gp)
	{
		mali_group_abort_gp_job(group, gp_job_id);
	}
	if (0 != abort_pp)
	{
		mali_group_abort_pp_job(group, pp_job_id);
	}

	mali_group_lock(group);
	mali_group_remove_session_if_unused(group, session);
	mali_group_unlock(group);
}

enum mali_group_core_state mali_group_gp_state(struct mali_group *group)
{
	return group->gp_state;
}

enum mali_group_core_state mali_group_pp_state(struct mali_group *group)
{
	return group->pp_state;
}

/* group lock need to be taken before calling mali_group_bottom_half */
void mali_group_bottom_half(struct mali_group *group, enum mali_group_event_t event)
{
	MALI_ASSERT_GROUP_LOCKED(group);

	switch (event)
	{
		case GROUP_EVENT_PP_JOB_COMPLETED:
			mali_group_complete_jobs(group, MALI_FALSE, MALI_TRUE, MALI_TRUE); /* PP job SUCCESS */
			/* group lock is released by mali_group_complete_jobs() call above */
			break;
		case GROUP_EVENT_PP_JOB_FAILED:
			mali_group_complete_jobs(group, MALI_FALSE, MALI_TRUE, MALI_FALSE); /* PP job FAIL */
			/* group lock is released by mali_group_complete_jobs() call above */
			break;
		case GROUP_EVENT_PP_JOB_TIMED_OUT:
			mali_group_complete_jobs(group, MALI_FALSE, MALI_TRUE, MALI_FALSE); /* PP job TIMEOUT */
			/* group lock is released by mali_group_complete_jobs() call above */
			break;
		case GROUP_EVENT_GP_JOB_COMPLETED:
			mali_group_complete_jobs(group, MALI_TRUE, MALI_FALSE, MALI_TRUE); /* GP job SUCCESS */
			/* group lock is released by mali_group_complete_jobs() call above */
			break;
		case GROUP_EVENT_GP_JOB_FAILED:
			mali_group_complete_jobs(group, MALI_TRUE, MALI_FALSE, MALI_FALSE); /* GP job FAIL */
			/* group lock is released by mali_group_complete_jobs() call above */
			break;
		case GROUP_EVENT_GP_JOB_TIMED_OUT:
			mali_group_complete_jobs(group, MALI_TRUE, MALI_FALSE, MALI_FALSE); /* GP job TIMEOUT */
			/* group lock is released by mali_group_complete_jobs() call above */
			break;
		case GROUP_EVENT_GP_OOM:
			group->gp_state = MALI_GROUP_CORE_STATE_OOM;
			mali_group_unlock(group); /* Nothing to do on the HW side, so just release group lock right away */
			mali_gp_scheduler_oom(group, group->gp_running_job);
			break;
		case GROUP_EVENT_MMU_PAGE_FAULT:
			mali_group_complete_jobs(group, MALI_TRUE, MALI_TRUE, MALI_FALSE); /* GP and PP job FAIL */
			/* group lock is released by mali_group_complete_jobs() call above */
			break;
		default:
			break;
	}
}

struct mali_mmu_core *mali_group_get_mmu(struct mali_group *group)
{
	return group->mmu;
}

struct mali_session_data *mali_group_get_session(struct mali_group *group)
{
	return group->session;
}

struct mali_group *mali_group_get_glob_group(u32 index)
{
	if(mali_global_num_groups > index)
	{
		return mali_global_groups[index];
	}

	return NULL;
}

u32 mali_group_get_glob_num_groups(void)
{
	return mali_global_num_groups;
}

/* Used to check if scheduler for the other core type needs to be called on job completion.
 *
 * Used only for Mali-200, where job start may fail if the only MMU is busy
 * with another session's address space.
 */
static inline mali_bool mali_group_other_reschedule_needed(struct mali_group *group)
{
	MALI_ASSERT_GROUP_LOCKED(group);

#if defined(USING_MALI200)
	if (group->pagedir_activation_failed)
	{
		group->pagedir_activation_failed = MALI_FALSE;
		return MALI_TRUE;
	}
	else
#endif
	{
		return MALI_FALSE;
	}
}

static enum mali_group_activate_pd_status mali_group_activate_page_directory(struct mali_group *group, struct mali_session_data *session)
{
	enum mali_group_activate_pd_status retval;
	MALI_ASSERT_GROUP_LOCKED(group);

	MALI_DEBUG_PRINT(5, ("Mali group: Activating page directory 0x%08X from session 0x%08X on group 0x%08X\n", mali_session_get_page_directory(session), session, group));
	MALI_DEBUG_ASSERT(0 <= group->page_dir_ref_count);

	if (0 != group->page_dir_ref_count)
	{
		if (group->session != session)
		{
			MALI_DEBUG_PRINT(4, ("Mali group: Activating session FAILED: 0x%08x on group 0x%08X. Existing session: 0x%08x\n", session, group, group->session));
			return MALI_GROUP_ACTIVATE_PD_STATUS_FAILED;
		}
		else
		{
			MALI_DEBUG_PRINT(4, ("Mali group: Activating session already activated: 0x%08x on group 0x%08X. New Ref: %d\n", session, group, 1+group->page_dir_ref_count));
			retval = MALI_GROUP_ACTIVATE_PD_STATUS_OK_KEPT_PD;

		}
	}
	else
	{
		/* There might be another session here, but it is ok to overwrite it since group->page_dir_ref_count==0 */
		if (group->session != session)
		{
			mali_bool activate_success;
			MALI_DEBUG_PRINT(5, ("Mali group: Activate session: %08x previous: %08x on group 0x%08X. Ref: %d\n", session, group->session, group, 1+group->page_dir_ref_count));

			activate_success = mali_mmu_activate_page_directory(group->mmu, mali_session_get_page_directory(session));
			MALI_DEBUG_ASSERT(activate_success);
			if ( MALI_FALSE== activate_success ) return MALI_FALSE;
			group->session = session;
			retval = MALI_GROUP_ACTIVATE_PD_STATUS_OK_SWITCHED_PD;
		}
		else
		{
			MALI_DEBUG_PRINT(4, ("Mali group: Activate existing session 0x%08X on group 0x%08X. Ref: %d\n", session->page_directory, group, 1+group->page_dir_ref_count));
			retval = MALI_GROUP_ACTIVATE_PD_STATUS_OK_KEPT_PD;
		}
	}

	group->page_dir_ref_count++;
	return retval;
}

static void mali_group_deactivate_page_directory(struct mali_group *group, struct mali_session_data *session)
{
	MALI_ASSERT_GROUP_LOCKED(group);

	MALI_DEBUG_ASSERT(0 < group->page_dir_ref_count);
	MALI_DEBUG_ASSERT(session == group->session);

	group->page_dir_ref_count--;

	/* As an optimization, the MMU still points to the group->session even if (0 == group->page_dir_ref_count),
	   and we do not call mali_mmu_activate_empty_page_directory(group->mmu); */
	MALI_DEBUG_ASSERT(0 <= group->page_dir_ref_count);
}

void mali_group_remove_session_if_unused(struct mali_group *group, struct mali_session_data *session)
{
	MALI_ASSERT_GROUP_LOCKED(group);

	if (0 == group->page_dir_ref_count)
	{
		MALI_DEBUG_ASSERT(MALI_GROUP_CORE_STATE_IDLE == group->gp_state);
		MALI_DEBUG_ASSERT(MALI_GROUP_CORE_STATE_IDLE == group->pp_state);

		if (group->session == session)
		{
			MALI_DEBUG_ASSERT(MALI_TRUE == group->power_is_on);
			MALI_DEBUG_PRINT(3, ("Mali group: Deactivating unused session 0x%08X on group %08X\n", session, group));
			mali_mmu_activate_empty_page_directory(group->mmu);
			group->session = NULL;
		}
	}
}

void mali_group_power_on(void)
{
	int i;
	for (i = 0; i < mali_global_num_groups; i++)
	{
		struct mali_group *group = mali_global_groups[i];
		mali_group_lock(group);
		MALI_DEBUG_ASSERT(MALI_GROUP_CORE_STATE_IDLE == group->gp_state);
		MALI_DEBUG_ASSERT(MALI_GROUP_CORE_STATE_IDLE == group->pp_state);
		MALI_DEBUG_ASSERT_POINTER(group->cluster);
		group->power_is_on = MALI_TRUE;
		mali_cluster_power_is_enabled_set(group->cluster, MALI_TRUE);
		mali_group_unlock(group);
	}
	MALI_DEBUG_PRINT(3,("group: POWER ON\n"));
}

mali_bool mali_group_power_is_on(struct mali_group *group)
{
	MALI_ASSERT_GROUP_LOCKED(group);
	return group->power_is_on;
}

void mali_group_power_off(void)
{
	int i;
	/* It is necessary to set group->session = NULL; so that the powered off MMU is not written to on map /unmap */
	/* It is necessary to set group->power_is_on=MALI_FALSE so that pending bottom_halves does not access powered off cores. */
	for (i = 0; i < mali_global_num_groups; i++)
	{
		struct mali_group *group = mali_global_groups[i];
		mali_group_lock(group);
		MALI_DEBUG_ASSERT(MALI_GROUP_CORE_STATE_IDLE == group->gp_state);
		MALI_DEBUG_ASSERT(MALI_GROUP_CORE_STATE_IDLE == group->pp_state);
		MALI_DEBUG_ASSERT_POINTER(group->cluster);
		group->session = NULL;
		MALI_DEBUG_ASSERT(MALI_TRUE == group->power_is_on);
		group->power_is_on = MALI_FALSE;
		mali_cluster_power_is_enabled_set(group->cluster, MALI_FALSE);
		mali_group_unlock(group);
	}
	MALI_DEBUG_PRINT(3,("group: POWER OFF\n"));
}


static void mali_group_recovery_reset(struct mali_group *group)
{
	MALI_ASSERT_GROUP_LOCKED(group);

	/* Stop cores, bus stop */
	if (NULL != group->pp_core)
	{
		mali_pp_stop_bus(group->pp_core);
	}
	if (NULL != group->gp_core)
	{
		mali_gp_stop_bus(group->gp_core);
	}

	/* Flush MMU */
	mali_mmu_activate_fault_flush_page_directory(group->mmu);
	mali_mmu_page_fault_done(group->mmu);

	/* Wait for cores to stop bus */
	if (NULL != group->pp_core)
	{
		mali_pp_stop_bus_wait(group->pp_core);
	}
	if (NULL != group->gp_core)
	{
		mali_gp_stop_bus_wait(group->gp_core);
	}

	/* Reset cores */
	if (NULL != group->pp_core)
	{
		mali_pp_hard_reset(group->pp_core);
	}
	if (NULL != group->gp_core)
	{
		mali_gp_hard_reset(group->gp_core);
	}

	/* Reset MMU */
	mali_mmu_reset(group->mmu);
	group->session = NULL;
}

/* Group lock need to be taken before calling mali_group_complete_jobs. Will release the lock here. */
static void mali_group_complete_jobs(struct mali_group *group, mali_bool complete_gp, mali_bool complete_pp, bool success)
{
	mali_bool need_group_reset = MALI_FALSE;
	mali_bool gp_success = success;
	mali_bool pp_success = success;

	MALI_ASSERT_GROUP_LOCKED(group);

	if (complete_gp && !complete_pp)
	{
		MALI_DEBUG_ASSERT_POINTER(group->gp_core);
		if (_MALI_OSK_ERR_OK == mali_gp_reset(group->gp_core))
		{
			struct mali_gp_job *gp_job_to_return = group->gp_running_job;
			group->gp_state = MALI_GROUP_CORE_STATE_IDLE;
			group->gp_running_job = NULL;

			MALI_DEBUG_ASSERT_POINTER(gp_job_to_return);

			mali_group_deactivate_page_directory(group, mali_gp_job_get_session(gp_job_to_return));

			if(mali_group_other_reschedule_needed(group))
			{
				mali_group_unlock(group);
				mali_pp_scheduler_do_schedule();
			}
			else
			{
				mali_group_unlock(group);
			}

			mali_gp_scheduler_job_done(group, gp_job_to_return, gp_success);
			mali_pm_core_event(MALI_CORE_EVENT_GP_STOP); /* It is ok to do this after schedule, since START/STOP is simply ++ and -- anyways */

			return;
		}
		else
		{
			need_group_reset = MALI_TRUE;
			MALI_DEBUG_PRINT(3, ("Mali group: Failed to reset GP, need to reset entire group\n"));
			pp_success = MALI_FALSE; /* This might kill PP as well, so this should fail */
		}
	}
	if (complete_pp && !complete_gp)
	{
		MALI_DEBUG_ASSERT_POINTER(group->pp_core);
		if (_MALI_OSK_ERR_OK == mali_pp_reset(group->pp_core))
		{
			struct mali_pp_job *pp_job_to_return = group->pp_running_job;
			u32 pp_sub_job_to_return = group->pp_running_sub_job;
			group->pp_state = MALI_GROUP_CORE_STATE_IDLE;
			group->pp_running_job = NULL;

			MALI_DEBUG_ASSERT_POINTER(pp_job_to_return);

			mali_group_deactivate_page_directory(group, mali_pp_job_get_session(pp_job_to_return));

			if(mali_group_other_reschedule_needed(group))
			{
				mali_group_unlock(group);
				mali_gp_scheduler_do_schedule();
			}
			else
			{
				mali_group_unlock(group);
			}

			mali_pp_scheduler_job_done(group, pp_job_to_return, pp_sub_job_to_return, pp_success);
			mali_pm_core_event(MALI_CORE_EVENT_PP_STOP); /* It is ok to do this after schedule, since START/STOP is simply ++ and -- anyways */

			return;
		}
		else
		{
			need_group_reset = MALI_TRUE;
			MALI_DEBUG_PRINT(3, ("Mali group: Failed to reset PP, need to reset entire group\n"));
			gp_success = MALI_FALSE; /* This might kill GP as well, so this should fail */
		}
	}
	else if (complete_gp && complete_pp)
	{
		need_group_reset = MALI_TRUE;
	}

	if (MALI_TRUE == need_group_reset)
	{
		struct mali_gp_job *gp_job_to_return = group->gp_running_job;
		struct mali_pp_job *pp_job_to_return = group->pp_running_job;
		u32 pp_sub_job_to_return = group->pp_running_sub_job;
		mali_bool schedule_other = MALI_FALSE;

		MALI_DEBUG_PRINT(3, ("Mali group: Resetting entire group\n"));

		group->gp_state = MALI_GROUP_CORE_STATE_IDLE;
		group->gp_running_job = NULL;
		if (NULL != gp_job_to_return)
		{
			mali_group_deactivate_page_directory(group, mali_gp_job_get_session(gp_job_to_return));
		}

		group->pp_state = MALI_GROUP_CORE_STATE_IDLE;
		group->pp_running_job = NULL;
		if (NULL != pp_job_to_return)
		{
			mali_group_deactivate_page_directory(group, mali_pp_job_get_session(pp_job_to_return));
		}

		/* The reset has to be done after mali_group_deactivate_page_directory() */
		mali_group_recovery_reset(group);

		if (mali_group_other_reschedule_needed(group) && (NULL == gp_job_to_return || NULL == pp_job_to_return))
		{
			schedule_other = MALI_TRUE;
		}

		mali_group_unlock(group);

		if (NULL != gp_job_to_return)
		{
			mali_gp_scheduler_job_done(group, gp_job_to_return, gp_success);
			mali_pm_core_event(MALI_CORE_EVENT_GP_STOP); /* It is ok to do this after schedule, since START/STOP is simply ++ and -- anyways */
		}
		else if (schedule_other)
		{
			mali_pp_scheduler_do_schedule();
		}

		if (NULL != pp_job_to_return)
		{
			mali_pp_scheduler_job_done(group, pp_job_to_return, pp_sub_job_to_return, pp_success);
			mali_pm_core_event(MALI_CORE_EVENT_PP_STOP); /* It is ok to do this after schedule, since START/STOP is simply ++ and -- anyways */
		}
		else if (schedule_other)
		{
			mali_gp_scheduler_do_schedule();
		}

		return;
	}

	mali_group_unlock(group);
}

#if MALI_STATE_TRACKING
u32 mali_group_dump_state(struct mali_group *group, char *buf, u32 size)
{
	int n = 0;

	n += _mali_osk_snprintf(buf + n, size - n, "Group: %p\n", group);
	if (group->gp_core)
	{
		n += mali_gp_dump_state(group->gp_core, buf + n, size - n);
		n += _mali_osk_snprintf(buf + n, size - n, "\tGP state: %d\n", group->gp_state);
		n += _mali_osk_snprintf(buf + n, size - n, "\tGP job: %p\n", group->gp_running_job);
	}
	if (group->pp_core)
	{
		n += mali_pp_dump_state(group->pp_core, buf + n, size - n);
		n += _mali_osk_snprintf(buf + n, size - n, "\tPP state: %d\n", group->pp_state);
		n += _mali_osk_snprintf(buf + n, size - n, "\tPP job: %p, subjob %d \n",
		                        group->pp_running_job, group->pp_running_sub_job);
	}

	return n;
}
#endif
