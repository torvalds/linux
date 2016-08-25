/*
 * Copyright (C) 2011-2016 ARM Limited. All rights reserved.
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
#include "mali_l2_cache.h"
#include "mali_gp.h"
#include "mali_pp.h"
#include "mali_mmu.h"
#include "mali_dlbu.h"
#include "mali_broadcast.h"
#include "mali_scheduler.h"
#include "mali_osk_profiling.h"
#include "mali_osk_mali.h"
#include "mali_pm_domain.h"
#include "mali_pm.h"
#include "mali_executor.h"

#if defined(CONFIG_GPU_TRACEPOINTS) && defined(CONFIG_TRACEPOINTS)
#include <linux/sched.h>
#include <trace/events/gpu.h>
#endif

#define MALI_MAX_NUM_DOMAIN_REFS (MALI_MAX_NUMBER_OF_GROUPS * 2)

#if defined(CONFIG_MALI400_PROFILING)
static void mali_group_report_l2_cache_counters_per_core(struct mali_group *group, u32 core_num);
#endif /* #if defined(CONFIG_MALI400_PROFILING) */

static struct mali_group *mali_global_groups[MALI_MAX_NUMBER_OF_GROUPS] = { NULL, };
static u32 mali_global_num_groups = 0;

/* SW timer for job execution */
int mali_max_job_runtime = MALI_MAX_JOB_RUNTIME_DEFAULT;

/* local helper functions */
static void mali_group_bottom_half_mmu(void *data);
static void mali_group_bottom_half_gp(void *data);
static void mali_group_bottom_half_pp(void *data);
static void mali_group_timeout(void *data);
static void mali_group_reset_pp(struct mali_group *group);
static void mali_group_reset_mmu(struct mali_group *group);

static void mali_group_activate_page_directory(struct mali_group *group, struct mali_session_data *session, mali_bool is_reload);
static void mali_group_recovery_reset(struct mali_group *group);

struct mali_group *mali_group_create(struct mali_l2_cache_core *core,
				     struct mali_dlbu_core *dlbu,
				     struct mali_bcast_unit *bcast,
				     u32 domain_index)
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
			_mali_osk_timer_setcallback(group->timeout_timer, mali_group_timeout, (void *)group);

			group->l2_cache_core[0] = core;
			_mali_osk_list_init(&group->group_list);
			_mali_osk_list_init(&group->executor_list);
			_mali_osk_list_init(&group->pm_domain_list);
			group->bcast_core = bcast;
			group->dlbu_core = dlbu;

			/* register this object as a part of the correct power domain */
			if ((NULL != core) || (NULL != dlbu) || (NULL != bcast))
				group->pm_domain = mali_pm_register_group(domain_index, group);

			mali_global_groups[mali_global_num_groups] = group;
			mali_global_num_groups++;

			return group;
		}
		_mali_osk_free(group);
	}

	return NULL;
}

void mali_group_delete(struct mali_group *group)
{
	u32 i;

	MALI_DEBUG_PRINT(4, ("Deleting group %s\n",
			     mali_group_core_description(group)));

	MALI_DEBUG_ASSERT(NULL == group->parent_group);
	MALI_DEBUG_ASSERT((MALI_GROUP_STATE_INACTIVE == group->state) || ((MALI_GROUP_STATE_ACTIVATION_PENDING == group->state)));

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

	_mali_osk_free(group);
}

_mali_osk_errcode_t mali_group_add_mmu_core(struct mali_group *group, struct mali_mmu_core *mmu_core)
{
	/* This group object now owns the MMU core object */
	group->mmu = mmu_core;
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

_mali_osk_errcode_t mali_group_add_gp_core(struct mali_group *group, struct mali_gp_core *gp_core)
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

_mali_osk_errcode_t mali_group_add_pp_core(struct mali_group *group, struct mali_pp_core *pp_core)
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

enum mali_group_state mali_group_activate(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	MALI_DEBUG_PRINT(4, ("Group: Activating group %s\n",
			     mali_group_core_description(group)));

	if (MALI_GROUP_STATE_INACTIVE == group->state) {
		/* Group is inactive, get PM refs in order to power up */

		/*
		 * We'll take a maximum of 2 power domain references pr group,
		 * one for the group itself, and one for it's L2 cache.
		 */
		struct mali_pm_domain *domains[MALI_MAX_NUM_DOMAIN_REFS];
		struct mali_group *groups[MALI_MAX_NUM_DOMAIN_REFS];
		u32 num_domains = 0;
		mali_bool all_groups_on;

		/* Deal with child groups first */
		if (mali_group_is_virtual(group)) {
			/*
			 * The virtual group might have 0, 1 or 2 L2s in
			 * its l2_cache_core array, but we ignore these and
			 * let the child groups take the needed L2 cache ref
			 * on behalf of the virtual group.
			 * In other words; The L2 refs are taken in pair with
			 * the physical group which the L2 is attached to.
			 */
			struct mali_group *child;
			struct mali_group *temp;

			/*
			 * Child group is inactive, get PM
			 * refs in order to power up.
			 */
			_MALI_OSK_LIST_FOREACHENTRY(child, temp,
						    &group->group_list,
						    struct mali_group, group_list) {
				MALI_DEBUG_ASSERT(MALI_GROUP_STATE_INACTIVE
						  == child->state);

				child->state = MALI_GROUP_STATE_ACTIVATION_PENDING;

				MALI_DEBUG_ASSERT_POINTER(
					child->pm_domain);
				domains[num_domains] = child->pm_domain;
				groups[num_domains] = child;
				num_domains++;

				/*
				 * Take L2 domain ref for child group.
				 */
				MALI_DEBUG_ASSERT(MALI_MAX_NUM_DOMAIN_REFS
						  > num_domains);
				domains[num_domains] = mali_l2_cache_get_pm_domain(
							       child->l2_cache_core[0]);
				groups[num_domains] = NULL;
				MALI_DEBUG_ASSERT(NULL ==
						  child->l2_cache_core[1]);
				num_domains++;
			}
		} else {
			/* Take L2 domain ref for physical groups. */
			MALI_DEBUG_ASSERT(MALI_MAX_NUM_DOMAIN_REFS >
					  num_domains);

			domains[num_domains] = mali_l2_cache_get_pm_domain(
						       group->l2_cache_core[0]);
			groups[num_domains] = NULL;
			MALI_DEBUG_ASSERT(NULL == group->l2_cache_core[1]);
			num_domains++;
		}

		/* Do the group itself last (it's dependencies first) */

		group->state = MALI_GROUP_STATE_ACTIVATION_PENDING;

		MALI_DEBUG_ASSERT_POINTER(group->pm_domain);
		domains[num_domains] = group->pm_domain;
		groups[num_domains] = group;
		num_domains++;

		all_groups_on = mali_pm_get_domain_refs(domains, groups,
							num_domains);

		/*
		 * Complete activation for group, include
		 * virtual group or physical group.
		 */
		if (MALI_TRUE == all_groups_on) {

			mali_group_set_active(group);
		}
	} else if (MALI_GROUP_STATE_ACTIVE == group->state) {
		/* Already active */
		MALI_DEBUG_ASSERT(MALI_TRUE == group->power_is_on);
	} else {
		/*
		 * Activation already pending, group->power_is_on could
		 * be both true or false. We need to wait for power up
		 * notification anyway.
		 */
		MALI_DEBUG_ASSERT(MALI_GROUP_STATE_ACTIVATION_PENDING
				  == group->state);
	}

	MALI_DEBUG_PRINT(4, ("Group: group %s activation result: %s\n",
			     mali_group_core_description(group),
			     MALI_GROUP_STATE_ACTIVE == group->state ?
			     "ACTIVE" : "PENDING"));

	return group->state;
}

mali_bool mali_group_set_active(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT(MALI_GROUP_STATE_ACTIVATION_PENDING == group->state);
	MALI_DEBUG_ASSERT(MALI_TRUE == group->power_is_on);

	MALI_DEBUG_PRINT(4, ("Group: Activation completed for %s\n",
			     mali_group_core_description(group)));

	if (mali_group_is_virtual(group)) {
		struct mali_group *child;
		struct mali_group *temp;

		_MALI_OSK_LIST_FOREACHENTRY(child, temp, &group->group_list,
					    struct mali_group, group_list) {
			if (MALI_TRUE != child->power_is_on) {
				return MALI_FALSE;
			}

			child->state = MALI_GROUP_STATE_ACTIVE;
		}

		mali_group_reset(group);
	}

	/* Go to ACTIVE state */
	group->state = MALI_GROUP_STATE_ACTIVE;

	return MALI_TRUE;
}

mali_bool mali_group_deactivate(struct mali_group *group)
{
	struct mali_pm_domain *domains[MALI_MAX_NUM_DOMAIN_REFS];
	u32 num_domains = 0;
	mali_bool power_down = MALI_FALSE;

	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT(MALI_GROUP_STATE_INACTIVE != group->state);

	MALI_DEBUG_PRINT(3, ("Group: Deactivating group %s\n",
			     mali_group_core_description(group)));

	group->state = MALI_GROUP_STATE_INACTIVE;

	MALI_DEBUG_ASSERT_POINTER(group->pm_domain);
	domains[num_domains] = group->pm_domain;
	num_domains++;

	if (mali_group_is_virtual(group)) {
		/* Release refs for all child groups */
		struct mali_group *child;
		struct mali_group *temp;

		_MALI_OSK_LIST_FOREACHENTRY(child, temp,
					    &group->group_list,
					    struct mali_group, group_list) {
			child->state = MALI_GROUP_STATE_INACTIVE;

			MALI_DEBUG_ASSERT_POINTER(child->pm_domain);
			domains[num_domains] = child->pm_domain;
			num_domains++;

			/* Release L2 cache domain for child groups */
			MALI_DEBUG_ASSERT(MALI_MAX_NUM_DOMAIN_REFS >
					  num_domains);
			domains[num_domains] = mali_l2_cache_get_pm_domain(
						       child->l2_cache_core[0]);
			MALI_DEBUG_ASSERT(NULL == child->l2_cache_core[1]);
			num_domains++;
		}

		/*
		 * Must do mali_group_power_down() steps right here for
		 * virtual group, because virtual group itself is likely to
		 * stay powered on, however child groups are now very likely
		 * to be powered off (and thus lose their state).
		 */

		mali_group_clear_session(group);
		/*
		 * Disable the broadcast unit (clear it's mask).
		 * This is needed in case the GPU isn't actually
		 * powered down at this point and groups are
		 * removed from an inactive virtual group.
		 * If not, then the broadcast unit will intercept
		 * their interrupts!
		 */
		mali_bcast_disable(group->bcast_core);
	} else {
		/* Release L2 cache domain for physical groups */
		MALI_DEBUG_ASSERT(MALI_MAX_NUM_DOMAIN_REFS >
				  num_domains);
		domains[num_domains] = mali_l2_cache_get_pm_domain(
					       group->l2_cache_core[0]);
		MALI_DEBUG_ASSERT(NULL == group->l2_cache_core[1]);
		num_domains++;
	}

	power_down = mali_pm_put_domain_refs(domains, num_domains);

	return power_down;
}

void mali_group_power_up(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	MALI_DEBUG_PRINT(3, ("Group: Power up for %s\n",
			     mali_group_core_description(group)));

	group->power_is_on = MALI_TRUE;

	if (MALI_FALSE == mali_group_is_virtual(group)
	    && MALI_FALSE == mali_group_is_in_virtual(group)) {
		mali_group_reset(group);
	}

	/*
	 * When we just acquire only one physical group form virt group,
	 * we should remove the bcast&dlbu mask from virt group and
	 * reset bcast and dlbu core, although part of pp cores in virt
	 * group maybe not be powered on.
	 */
	if (MALI_TRUE == mali_group_is_virtual(group)) {
		mali_bcast_reset(group->bcast_core);
		mali_dlbu_update_mask(group->dlbu_core);
	}
}

void mali_group_power_down(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT(MALI_TRUE == group->power_is_on);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	MALI_DEBUG_PRINT(3, ("Group: Power down for %s\n",
			     mali_group_core_description(group)));

	group->power_is_on = MALI_FALSE;

	if (mali_group_is_virtual(group)) {
		/*
		 * What we do for physical jobs in this function should
		 * already have been done in mali_group_deactivate()
		 * for virtual group.
		 */
		MALI_DEBUG_ASSERT(NULL == group->session);
	} else {
		mali_group_clear_session(group);
	}
}

MALI_DEBUG_CODE(static void mali_group_print_virtual(struct mali_group *vgroup)
{
	u32 i;
	struct mali_group *group;
	struct mali_group *temp;

	MALI_DEBUG_PRINT(4, ("Virtual group %s (%p)\n",
			     mali_group_core_description(vgroup),
			     vgroup));
	MALI_DEBUG_PRINT(4, ("l2_cache_core[0] = %p, ref = %d\n", vgroup->l2_cache_core[0], vgroup->l2_cache_core_ref_count[0]));
	MALI_DEBUG_PRINT(4, ("l2_cache_core[1] = %p, ref = %d\n", vgroup->l2_cache_core[1], vgroup->l2_cache_core_ref_count[1]));

	i = 0;
	_MALI_OSK_LIST_FOREACHENTRY(group, temp, &vgroup->group_list, struct mali_group, group_list) {
		MALI_DEBUG_PRINT(4, ("[%d] %s (%p), l2_cache_core[0] = %p\n",
				     i, mali_group_core_description(group),
				     group, group->l2_cache_core[0]));
		i++;
	}
})

static void mali_group_dump_core_status(struct mali_group *group)
{
	u32 i;

	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT(NULL != group->gp_core || (NULL != group->pp_core && !mali_group_is_virtual(group)));

	if (NULL != group->gp_core) {
		MALI_PRINT(("Dump Group %s\n", group->gp_core->hw_core.description));

		for (i = 0; i < 0xA8; i += 0x10) {
			MALI_PRINT(("0x%04x: 0x%08x 0x%08x 0x%08x 0x%08x\n", i, mali_hw_core_register_read(&group->gp_core->hw_core, i),
				    mali_hw_core_register_read(&group->gp_core->hw_core, i + 4),
				    mali_hw_core_register_read(&group->gp_core->hw_core, i + 8),
				    mali_hw_core_register_read(&group->gp_core->hw_core, i + 12)));
		}


	} else {
		MALI_PRINT(("Dump Group %s\n", group->pp_core->hw_core.description));

		for (i = 0; i < 0x5c; i += 0x10) {
			MALI_PRINT(("0x%04x: 0x%08x 0x%08x 0x%08x 0x%08x\n", i, mali_hw_core_register_read(&group->pp_core->hw_core, i),
				    mali_hw_core_register_read(&group->pp_core->hw_core, i + 4),
				    mali_hw_core_register_read(&group->pp_core->hw_core, i + 8),
				    mali_hw_core_register_read(&group->pp_core->hw_core, i + 12)));
		}

		/* Ignore some minor registers */
		for (i = 0x1000; i < 0x1068; i += 0x10) {
			MALI_PRINT(("0x%04x: 0x%08x 0x%08x 0x%08x 0x%08x\n", i, mali_hw_core_register_read(&group->pp_core->hw_core, i),
				    mali_hw_core_register_read(&group->pp_core->hw_core, i + 4),
				    mali_hw_core_register_read(&group->pp_core->hw_core, i + 8),
				    mali_hw_core_register_read(&group->pp_core->hw_core, i + 12)));
		}
	}

	MALI_PRINT(("Dump Group MMU\n"));
	for (i = 0; i < 0x24; i += 0x10) {
		MALI_PRINT(("0x%04x: 0x%08x 0x%08x 0x%08x 0x%08x\n", i, mali_hw_core_register_read(&group->mmu->hw_core, i),
			    mali_hw_core_register_read(&group->mmu->hw_core, i + 4),
			    mali_hw_core_register_read(&group->mmu->hw_core, i + 8),
			    mali_hw_core_register_read(&group->mmu->hw_core, i + 12)));
	}
}


/**
 * @Dump group status
 */
void mali_group_dump_status(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);

	if (mali_group_is_virtual(group)) {
		struct mali_group *group_c;
		struct mali_group *temp;
		_MALI_OSK_LIST_FOREACHENTRY(group_c, temp, &group->group_list, struct mali_group, group_list) {
			mali_group_dump_core_status(group_c);
		}
	} else {
		mali_group_dump_core_status(group);
	}
}

/**
 * @brief Add child group to virtual group parent
 */
void mali_group_add_group(struct mali_group *parent, struct mali_group *child)
{
	mali_bool found;
	u32 i;

	MALI_DEBUG_PRINT(3, ("Adding group %s to virtual group %s\n",
			     mali_group_core_description(child),
			     mali_group_core_description(parent)));

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT(mali_group_is_virtual(parent));
	MALI_DEBUG_ASSERT(!mali_group_is_virtual(child));
	MALI_DEBUG_ASSERT(NULL == child->parent_group);

	_mali_osk_list_addtail(&child->group_list, &parent->group_list);

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

	if (MALI_TRUE == parent->power_is_on) {
		mali_bcast_reset(parent->bcast_core);
		mali_dlbu_update_mask(parent->dlbu_core);
	}

	if (MALI_TRUE == child->power_is_on) {
		if (NULL == parent->session) {
			if (NULL != child->session) {
				/*
				 * Parent has no session, so clear
				 * child session as well.
				 */
				mali_mmu_activate_empty_page_directory(child->mmu);
			}
		} else {
			if (parent->session == child->session) {
				/* We already have same session as parent,
				 * so a simple zap should be enough.
				 */
				mali_mmu_zap_tlb(child->mmu);
			} else {
				/*
				 * Parent has a different session, so we must
				 * switch to that sessions page table
				 */
				mali_mmu_activate_page_directory(child->mmu, mali_session_get_page_directory(parent->session));
			}

			/* It is the parent which keeps the session from now on */
			child->session = NULL;
		}
	} else {
		/* should have been cleared when child was powered down */
		MALI_DEBUG_ASSERT(NULL == child->session);
	}

	/* Start job on child when parent is active */
	if (NULL != parent->pp_running_job) {
		struct mali_pp_job *job = parent->pp_running_job;

		MALI_DEBUG_PRINT(3, ("Group %x joining running job %d on virtual group %x\n",
				     child, mali_pp_job_get_id(job), parent));

		/* Only allowed to add active child to an active parent */
		MALI_DEBUG_ASSERT(MALI_GROUP_STATE_ACTIVE == parent->state);
		MALI_DEBUG_ASSERT(MALI_GROUP_STATE_ACTIVE == child->state);

		mali_pp_job_start(child->pp_core, job, mali_pp_core_get_id(child->pp_core), MALI_TRUE);

		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
					      MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(child->pp_core)) |
					      MALI_PROFILING_EVENT_REASON_SINGLE_HW_FLUSH,
					      mali_pp_job_get_frame_builder_id(job), mali_pp_job_get_flush_id(job), 0, 0, 0);

		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START |
					      MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(child->pp_core)) |
					      MALI_PROFILING_EVENT_REASON_START_STOP_HW_VIRTUAL,
					      mali_pp_job_get_pid(job), mali_pp_job_get_tid(job), 0, 0, 0);
#if defined(CONFIG_GPU_TRACEPOINTS) && defined(CONFIG_TRACEPOINTS)
		trace_gpu_sched_switch(
			mali_pp_core_description(group->pp_core),
			sched_clock(), mali_pp_job_get_tid(job),
			0, mali_pp_job_get_id(job));
#endif

#if defined(CONFIG_MALI400_PROFILING)
		trace_mali_core_active(mali_pp_job_get_pid(job), 1 /* active */, 0 /* PP */, mali_pp_core_get_id(child->pp_core),
				       mali_pp_job_get_frame_builder_id(job), mali_pp_job_get_flush_id(job));
#endif
	}

	MALI_DEBUG_CODE(mali_group_print_virtual(parent);)
}

/**
 * @brief Remove child group from virtual group parent
 */
void mali_group_remove_group(struct mali_group *parent, struct mali_group *child)
{
	u32 i;

	MALI_DEBUG_PRINT(3, ("Removing group %s from virtual group %s\n",
			     mali_group_core_description(child),
			     mali_group_core_description(parent)));

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT(mali_group_is_virtual(parent));
	MALI_DEBUG_ASSERT(!mali_group_is_virtual(child));
	MALI_DEBUG_ASSERT(parent == child->parent_group);

	/* Update Broadcast Unit and DLBU */
	mali_bcast_remove_group(parent->bcast_core, child);
	mali_dlbu_remove_group(parent->dlbu_core, child);

	if (MALI_TRUE == parent->power_is_on) {
		mali_bcast_reset(parent->bcast_core);
		mali_dlbu_update_mask(parent->dlbu_core);
	}

	child->session = parent->session;
	child->parent_group = NULL;

	_mali_osk_list_delinit(&child->group_list);
	if (_mali_osk_list_empty(&parent->group_list)) {
		parent->session = NULL;
	}

	/* Keep track of the L2 cache cores of child groups */
	i = (child->l2_cache_core[0] == parent->l2_cache_core[0]) ? 0 : 1;

	MALI_DEBUG_ASSERT(child->l2_cache_core[0] == parent->l2_cache_core[i]);

	parent->l2_cache_core_ref_count[i]--;
	if (parent->l2_cache_core_ref_count[i] == 0) {
		parent->l2_cache_core[i] = NULL;
	}

	MALI_DEBUG_CODE(mali_group_print_virtual(parent));
}

struct mali_group *mali_group_acquire_group(struct mali_group *parent)
{
	struct mali_group *child = NULL;

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT(mali_group_is_virtual(parent));

	if (!_mali_osk_list_empty(&parent->group_list)) {
		child = _MALI_OSK_LIST_ENTRY(parent->group_list.prev, struct mali_group, group_list);
		mali_group_remove_group(parent, child);
	}

	if (NULL != child) {
		if (MALI_GROUP_STATE_ACTIVE != parent->state
		    && MALI_TRUE == child->power_is_on) {
			mali_group_reset(child);
		}
	}

	return child;
}

void mali_group_reset(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT(NULL == group->gp_running_job);
	MALI_DEBUG_ASSERT(NULL == group->pp_running_job);

	MALI_DEBUG_PRINT(3, ("Group: reset of %s\n",
			     mali_group_core_description(group)));

	if (NULL != group->dlbu_core) {
		mali_dlbu_reset(group->dlbu_core);
	}

	if (NULL != group->bcast_core) {
		mali_bcast_reset(group->bcast_core);
	}

	MALI_DEBUG_ASSERT(NULL != group->mmu);
	mali_group_reset_mmu(group);

	if (NULL != group->gp_core) {
		MALI_DEBUG_ASSERT(NULL == group->pp_core);
		mali_gp_reset(group->gp_core);
	} else {
		MALI_DEBUG_ASSERT(NULL != group->pp_core);
		mali_group_reset_pp(group);
	}
}

void mali_group_start_gp_job(struct mali_group *group, struct mali_gp_job *job, mali_bool gpu_secure_mode_pre_enabled)
{
	struct mali_session_data *session;

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	MALI_DEBUG_PRINT(3, ("Group: Starting GP job 0x%08X on group %s\n",
			     job,
			     mali_group_core_description(group)));

	session = mali_gp_job_get_session(job);

	MALI_DEBUG_ASSERT_POINTER(group->l2_cache_core[0]);
	mali_l2_cache_invalidate_conditional(group->l2_cache_core[0], mali_gp_job_get_cache_order(job));

	/* Reset GPU and disable gpu secure mode if needed. */
	if (MALI_TRUE == _mali_osk_gpu_secure_mode_is_enabled()) {
		struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();
		_mali_osk_gpu_reset_and_secure_mode_disable();
		/* Need to disable the pmu interrupt mask register */
		if (NULL != pmu) {
			mali_pmu_reset(pmu);
		}
	}

	/* Reload mmu page table if needed */
	if (MALI_TRUE == gpu_secure_mode_pre_enabled) {
		mali_group_reset(group);
		mali_group_activate_page_directory(group, session, MALI_TRUE);
	} else {
		mali_group_activate_page_directory(group, session, MALI_FALSE);
	}

	mali_gp_job_start(group->gp_core, job);

	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
				      MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(0) |
				      MALI_PROFILING_EVENT_REASON_SINGLE_HW_FLUSH,
				      mali_gp_job_get_frame_builder_id(job), mali_gp_job_get_flush_id(job), 0, 0, 0);
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START |
				      MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(0),
				      mali_gp_job_get_pid(job), mali_gp_job_get_tid(job), 0, 0, 0);

#if defined(CONFIG_MALI400_PROFILING)
	trace_mali_core_active(mali_gp_job_get_pid(job), 1 /* active */, 1 /* GP */,  0 /* core */,
			       mali_gp_job_get_frame_builder_id(job), mali_gp_job_get_flush_id(job));
#endif

#if defined(CONFIG_MALI400_PROFILING)
	if ((MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src0(group->l2_cache_core[0])) &&
	    (MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src1(group->l2_cache_core[0]))) {
		mali_group_report_l2_cache_counters_per_core(group, 0);
	}
#endif /* #if defined(CONFIG_MALI400_PROFILING) */

#if defined(CONFIG_GPU_TRACEPOINTS) && defined(CONFIG_TRACEPOINTS)
	trace_gpu_sched_switch(mali_gp_core_description(group->gp_core),
			       sched_clock(), mali_gp_job_get_tid(job),
			       0, mali_gp_job_get_id(job));
#endif

	group->gp_running_job = job;
	group->is_working = MALI_TRUE;

	/* Setup SW timer and record start time */
	group->start_time = _mali_osk_time_tickcount();
	_mali_osk_timer_mod(group->timeout_timer, _mali_osk_time_mstoticks(mali_max_job_runtime));

	MALI_DEBUG_PRINT(4, ("Group: Started GP job 0x%08X on group %s at %u\n",
			     job,
			     mali_group_core_description(group),
			     group->start_time));
}

/* Used to set all the registers except frame renderer list address and fragment shader stack address
 * It means the caller must set these two registers properly before calling this function
 */
void mali_group_start_pp_job(struct mali_group *group, struct mali_pp_job *job, u32 sub_job, mali_bool gpu_secure_mode_pre_enabled)
{
	struct mali_session_data *session;

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	MALI_DEBUG_PRINT(3, ("Group: Starting PP job 0x%08X part %u/%u on group %s\n",
			     job, sub_job + 1,
			     mali_pp_job_get_sub_job_count(job),
			     mali_group_core_description(group)));

	session = mali_pp_job_get_session(job);

	if (NULL != group->l2_cache_core[0]) {
		mali_l2_cache_invalidate_conditional(group->l2_cache_core[0], mali_pp_job_get_cache_order(job));
	}

	if (NULL != group->l2_cache_core[1]) {
		mali_l2_cache_invalidate_conditional(group->l2_cache_core[1], mali_pp_job_get_cache_order(job));
	}

	/* Reset GPU and change gpu secure mode if needed. */
	if (MALI_TRUE == mali_pp_job_is_protected_job(job) && MALI_FALSE == _mali_osk_gpu_secure_mode_is_enabled()) {
		struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();
		_mali_osk_gpu_reset_and_secure_mode_enable();
		/* Need to disable the pmu interrupt mask register */
		if (NULL != pmu) {
			mali_pmu_reset(pmu);
		}
	} else if (MALI_FALSE == mali_pp_job_is_protected_job(job) && MALI_TRUE == _mali_osk_gpu_secure_mode_is_enabled()) {
		struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();
		_mali_osk_gpu_reset_and_secure_mode_disable();
		/* Need to disable the pmu interrupt mask register */
		if (NULL != pmu) {
			mali_pmu_reset(pmu);
		}
	}

	/* Reload the mmu page table if needed */
	if ((MALI_TRUE == mali_pp_job_is_protected_job(job) && MALI_FALSE == gpu_secure_mode_pre_enabled)
		||(MALI_FALSE == mali_pp_job_is_protected_job(job) && MALI_TRUE == gpu_secure_mode_pre_enabled)) {
		mali_group_reset(group);
		mali_group_activate_page_directory(group, session, MALI_TRUE);
	} else {
		mali_group_activate_page_directory(group, session, MALI_FALSE);
	}

	if (mali_group_is_virtual(group)) {
		struct mali_group *child;
		struct mali_group *temp;
		u32 core_num = 0;

		MALI_DEBUG_ASSERT(mali_pp_job_is_virtual(job));

		/* Configure DLBU for the job */
		mali_dlbu_config_job(group->dlbu_core, job);

		/* Write stack address for each child group */
		_MALI_OSK_LIST_FOREACHENTRY(child, temp, &group->group_list, struct mali_group, group_list) {
			mali_pp_write_addr_stack(child->pp_core, job);
			core_num++;
		}

		mali_pp_job_start(group->pp_core, job, sub_job, MALI_FALSE);
	} else {
		mali_pp_job_start(group->pp_core, job, sub_job, MALI_FALSE);
	}

	/* if the group is virtual, loop through physical groups which belong to this group
	 * and call profiling events for its cores as virtual */
	if (MALI_TRUE == mali_group_is_virtual(group)) {
		struct mali_group *child;
		struct mali_group *temp;

		_MALI_OSK_LIST_FOREACHENTRY(child, temp, &group->group_list, struct mali_group, group_list) {
			_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
						      MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(child->pp_core)) |
						      MALI_PROFILING_EVENT_REASON_SINGLE_HW_FLUSH,
						      mali_pp_job_get_frame_builder_id(job), mali_pp_job_get_flush_id(job), 0, 0, 0);

			_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START |
						      MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(child->pp_core)) |
						      MALI_PROFILING_EVENT_REASON_START_STOP_HW_VIRTUAL,
						      mali_pp_job_get_pid(job), mali_pp_job_get_tid(job), 0, 0, 0);

#if defined(CONFIG_MALI400_PROFILING)
			trace_mali_core_active(mali_pp_job_get_pid(job), 1 /* active */, 0 /* PP */, mali_pp_core_get_id(child->pp_core),
					       mali_pp_job_get_frame_builder_id(job), mali_pp_job_get_flush_id(job));
#endif
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
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
					      MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(group->pp_core)) |
					      MALI_PROFILING_EVENT_REASON_SINGLE_HW_FLUSH,
					      mali_pp_job_get_frame_builder_id(job), mali_pp_job_get_flush_id(job), 0, 0, 0);

		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START |
					      MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(group->pp_core)) |
					      MALI_PROFILING_EVENT_REASON_START_STOP_HW_PHYSICAL,
					      mali_pp_job_get_pid(job), mali_pp_job_get_tid(job), 0, 0, 0);

#if defined(CONFIG_MALI400_PROFILING)
		trace_mali_core_active(mali_pp_job_get_pid(job), 1 /* active */, 0 /* PP */, mali_pp_core_get_id(group->pp_core),
				       mali_pp_job_get_frame_builder_id(job), mali_pp_job_get_flush_id(job));
#endif

#if defined(CONFIG_MALI400_PROFILING)
		if ((MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src0(group->l2_cache_core[0])) &&
		    (MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src1(group->l2_cache_core[0]))) {
			mali_group_report_l2_cache_counters_per_core(group, mali_l2_cache_get_id(group->l2_cache_core[0]));
		}
#endif /* #if defined(CONFIG_MALI400_PROFILING) */
	}

#if defined(CONFIG_GPU_TRACEPOINTS) && defined(CONFIG_TRACEPOINTS)
	trace_gpu_sched_switch(mali_pp_core_description(group->pp_core),
			       sched_clock(), mali_pp_job_get_tid(job),
			       0, mali_pp_job_get_id(job));
#endif

	group->pp_running_job = job;
	group->pp_running_sub_job = sub_job;
	group->is_working = MALI_TRUE;

	/* Setup SW timer and record start time */
	group->start_time = _mali_osk_time_tickcount();
	_mali_osk_timer_mod(group->timeout_timer, _mali_osk_time_mstoticks(mali_max_job_runtime));

	MALI_DEBUG_PRINT(4, ("Group: Started PP job 0x%08X part %u/%u on group %s at %u\n",
			     job, sub_job + 1,
			     mali_pp_job_get_sub_job_count(job),
			     mali_group_core_description(group),
			     group->start_time));

}

void mali_group_resume_gp_with_new_heap(struct mali_group *group, u32 job_id, u32 start_addr, u32 end_addr)
{
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	MALI_DEBUG_ASSERT_POINTER(group->l2_cache_core[0]);
	mali_l2_cache_invalidate(group->l2_cache_core[0]);

	mali_mmu_zap_tlb_without_stall(group->mmu);

	mali_gp_resume_with_new_heap(group->gp_core, start_addr, end_addr);

	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_RESUME |
				      MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(0),
				      0, 0, 0, 0, 0);

#if defined(CONFIG_MALI400_PROFILING)
	trace_mali_core_active(mali_gp_job_get_pid(group->gp_running_job), 1 /* active */, 1 /* GP */,  0 /* core */,
			       mali_gp_job_get_frame_builder_id(group->gp_running_job), mali_gp_job_get_flush_id(group->gp_running_job));
#endif
}

static void mali_group_reset_mmu(struct mali_group *group)
{
	struct mali_group *child;
	struct mali_group *temp;
	_mali_osk_errcode_t err;

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	if (!mali_group_is_virtual(group)) {
		/* This is a physical group or an idle virtual group -- simply wait for
		 * the reset to complete. */
		err = mali_mmu_reset(group->mmu);
		MALI_DEBUG_ASSERT(_MALI_OSK_ERR_OK == err);
	} else { /* virtual group */
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

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	mali_pp_reset_async(group->pp_core);

	if (!mali_group_is_virtual(group) || NULL == group->pp_running_job) {
		/* This is a physical group or an idle virtual group -- simply wait for
		 * the reset to complete. */
		mali_pp_reset_wait(group->pp_core);
	} else {
		/* Loop through all members of this virtual group and wait until they
		 * are done resetting.
		 */
		_MALI_OSK_LIST_FOREACHENTRY(child, temp, &group->group_list, struct mali_group, group_list) {
			mali_pp_reset_wait(child->pp_core);
		}
	}
}

struct mali_pp_job *mali_group_complete_pp(struct mali_group *group, mali_bool success, u32 *sub_job)
{
	struct mali_pp_job *pp_job_to_return;

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->pp_core);
	MALI_DEBUG_ASSERT_POINTER(group->pp_running_job);
	MALI_DEBUG_ASSERT_POINTER(sub_job);
	MALI_DEBUG_ASSERT(MALI_TRUE == group->is_working);

	/* Stop/clear the timeout timer. */
	_mali_osk_timer_del_async(group->timeout_timer);

	if (NULL != group->pp_running_job) {

		/* Deal with HW counters and profiling */

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
				_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
							      MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(child->pp_core)) |
							      MALI_PROFILING_EVENT_REASON_START_STOP_HW_VIRTUAL,
							      mali_pp_job_get_perf_counter_value0(group->pp_running_job, mali_pp_core_get_id(child->pp_core)),
							      mali_pp_job_get_perf_counter_value1(group->pp_running_job, mali_pp_core_get_id(child->pp_core)),
							      mali_pp_job_get_perf_counter_src0(group->pp_running_job, group->pp_running_sub_job) | (mali_pp_job_get_perf_counter_src1(group->pp_running_job, group->pp_running_sub_job) << 8),
							      0, 0);

				trace_mali_core_active(mali_pp_job_get_pid(group->pp_running_job),
						       0 /* active */, 0 /* PP */, mali_pp_core_get_id(child->pp_core),
						       mali_pp_job_get_frame_builder_id(group->pp_running_job),
						       mali_pp_job_get_flush_id(group->pp_running_job));
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
			_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
						      MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(mali_pp_core_get_id(group->pp_core)) |
						      MALI_PROFILING_EVENT_REASON_START_STOP_HW_PHYSICAL,
						      mali_pp_job_get_perf_counter_value0(group->pp_running_job, group->pp_running_sub_job),
						      mali_pp_job_get_perf_counter_value1(group->pp_running_job, group->pp_running_sub_job),
						      mali_pp_job_get_perf_counter_src0(group->pp_running_job, group->pp_running_sub_job) | (mali_pp_job_get_perf_counter_src1(group->pp_running_job, group->pp_running_sub_job) << 8),
						      0, 0);

			trace_mali_core_active(mali_pp_job_get_pid(group->pp_running_job),
					       0 /* active */, 0 /* PP */, mali_pp_core_get_id(group->pp_core),
					       mali_pp_job_get_frame_builder_id(group->pp_running_job),
					       mali_pp_job_get_flush_id(group->pp_running_job));

			if ((MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src0(group->l2_cache_core[0])) &&
			    (MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src1(group->l2_cache_core[0]))) {
				mali_group_report_l2_cache_counters_per_core(group, mali_l2_cache_get_id(group->l2_cache_core[0]));
			}
#endif
		}

#if defined(CONFIG_GPU_TRACEPOINTS) && defined(CONFIG_TRACEPOINTS)
		trace_gpu_sched_switch(
			mali_gp_core_description(group->gp_core),
			sched_clock(), 0, 0, 0);
#endif

	}

	if (success) {
		/* Only do soft reset for successful jobs, a full recovery
		 * reset will be done for failed jobs. */
		mali_pp_reset_async(group->pp_core);
	}

	pp_job_to_return = group->pp_running_job;
	group->pp_running_job = NULL;
	group->is_working = MALI_FALSE;
	*sub_job = group->pp_running_sub_job;

	if (!success) {
		MALI_DEBUG_PRINT(2, ("Mali group: Executing recovery reset due to job failure\n"));
		mali_group_recovery_reset(group);
	} else if (_MALI_OSK_ERR_OK != mali_pp_reset_wait(group->pp_core)) {
		MALI_PRINT_ERROR(("Mali group: Executing recovery reset due to reset failure\n"));
		mali_group_recovery_reset(group);
	}

	return pp_job_to_return;
}

struct mali_gp_job *mali_group_complete_gp(struct mali_group *group, mali_bool success)
{
	struct mali_gp_job *gp_job_to_return;

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->gp_core);
	MALI_DEBUG_ASSERT_POINTER(group->gp_running_job);
	MALI_DEBUG_ASSERT(MALI_TRUE == group->is_working);

	/* Stop/clear the timeout timer. */
	_mali_osk_timer_del_async(group->timeout_timer);

	if (NULL != group->gp_running_job) {
		mali_gp_update_performance_counters(group->gp_core, group->gp_running_job);

#if defined(CONFIG_MALI400_PROFILING)
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP | MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(0),
					      mali_gp_job_get_perf_counter_value0(group->gp_running_job),
					      mali_gp_job_get_perf_counter_value1(group->gp_running_job),
					      mali_gp_job_get_perf_counter_src0(group->gp_running_job) | (mali_gp_job_get_perf_counter_src1(group->gp_running_job) << 8),
					      0, 0);

		if ((MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src0(group->l2_cache_core[0])) &&
		    (MALI_HW_CORE_NO_COUNTER != mali_l2_cache_core_get_counter_src1(group->l2_cache_core[0])))
			mali_group_report_l2_cache_counters_per_core(group, 0);
#endif

#if defined(CONFIG_GPU_TRACEPOINTS) && defined(CONFIG_TRACEPOINTS)
		trace_gpu_sched_switch(
			mali_pp_core_description(group->pp_core),
			sched_clock(), 0, 0, 0);
#endif

#if defined(CONFIG_MALI400_PROFILING)
		trace_mali_core_active(mali_gp_job_get_pid(group->gp_running_job), 0 /* active */, 1 /* GP */,  0 /* core */,
				       mali_gp_job_get_frame_builder_id(group->gp_running_job), mali_gp_job_get_flush_id(group->gp_running_job));
#endif

		mali_gp_job_set_current_heap_addr(group->gp_running_job,
						  mali_gp_read_plbu_alloc_start_addr(group->gp_core));
	}

	if (success) {
		/* Only do soft reset for successful jobs, a full recovery
		 * reset will be done for failed jobs. */
		mali_gp_reset_async(group->gp_core);
	}

	gp_job_to_return = group->gp_running_job;
	group->gp_running_job = NULL;
	group->is_working = MALI_FALSE;

	if (!success) {
		MALI_DEBUG_PRINT(2, ("Mali group: Executing recovery reset due to job failure\n"));
		mali_group_recovery_reset(group);
	} else if (_MALI_OSK_ERR_OK != mali_gp_reset_wait(group->gp_core)) {
		MALI_PRINT_ERROR(("Mali group: Executing recovery reset due to reset failure\n"));
		mali_group_recovery_reset(group);
	}

	return gp_job_to_return;
}

struct mali_group *mali_group_get_glob_group(u32 index)
{
	if (mali_global_num_groups > index) {
		return mali_global_groups[index];
	}

	return NULL;
}

u32 mali_group_get_glob_num_groups(void)
{
	return mali_global_num_groups;
}

static void mali_group_activate_page_directory(struct mali_group *group, struct mali_session_data *session, mali_bool is_reload)
{
	MALI_DEBUG_PRINT(5, ("Mali group: Activating page directory 0x%08X from session 0x%08X on group %s\n",
			     mali_session_get_page_directory(session), session,
			     mali_group_core_description(group)));

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	if (group->session != session ||MALI_TRUE == is_reload) {
		/* Different session than last time, so we need to do some work */
		MALI_DEBUG_PRINT(5, ("Mali group: Activate session: %08x previous: %08x on group %s\n",
				     session, group->session,
				     mali_group_core_description(group)));
		mali_mmu_activate_page_directory(group->mmu, mali_session_get_page_directory(session));
		group->session = session;
	} else {
		/* Same session as last time, so no work required */
		MALI_DEBUG_PRINT(4, ("Mali group: Activate existing session 0x%08X on group %s\n",
				     session->page_directory,
				     mali_group_core_description(group)));
		mali_mmu_zap_tlb_without_stall(group->mmu);
	}
}

static void mali_group_recovery_reset(struct mali_group *group)
{
	_mali_osk_errcode_t err;

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

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
	int i;
	struct mali_group *child;
	struct mali_group *temp;

	if (mali_group_is_virtual(group)) {
		n += _mali_osk_snprintf(buf + n, size - n,
					"Virtual PP Group: %p\n", group);
	} else if (mali_group_is_in_virtual(group)) {
		n += _mali_osk_snprintf(buf + n, size - n,
					"Child PP Group: %p\n", group);
	} else if (NULL != group->pp_core) {
		n += _mali_osk_snprintf(buf + n, size - n,
					"Physical PP Group: %p\n", group);
	} else {
		MALI_DEBUG_ASSERT_POINTER(group->gp_core);
		n += _mali_osk_snprintf(buf + n, size - n,
					"GP Group: %p\n", group);
	}

	switch (group->state) {
	case MALI_GROUP_STATE_INACTIVE:
		n += _mali_osk_snprintf(buf + n, size - n,
					"\tstate: INACTIVE\n");
		break;
	case MALI_GROUP_STATE_ACTIVATION_PENDING:
		n += _mali_osk_snprintf(buf + n, size - n,
					"\tstate: ACTIVATION_PENDING\n");
		break;
	case MALI_GROUP_STATE_ACTIVE:
		n += _mali_osk_snprintf(buf + n, size - n,
					"\tstate: MALI_GROUP_STATE_ACTIVE\n");
		break;
	default:
		n += _mali_osk_snprintf(buf + n, size - n,
					"\tstate: UNKNOWN (%d)\n", group->state);
		MALI_DEBUG_ASSERT(0);
		break;
	}

	n += _mali_osk_snprintf(buf + n, size - n,
				"\tSW power: %s\n",
				group->power_is_on ? "On" : "Off");

	n += mali_pm_dump_state_domain(group->pm_domain, buf + n, size - n);

	for (i = 0; i < 2; i++) {
		if (NULL != group->l2_cache_core[i]) {
			struct mali_pm_domain *domain;
			domain = mali_l2_cache_get_pm_domain(
					 group->l2_cache_core[i]);
			n += mali_pm_dump_state_domain(domain,
						       buf + n, size - n);
		}
	}

	if (group->gp_core) {
		n += mali_gp_dump_state(group->gp_core, buf + n, size - n);
		n += _mali_osk_snprintf(buf + n, size - n,
					"\tGP running job: %p\n", group->gp_running_job);
	}

	if (group->pp_core) {
		n += mali_pp_dump_state(group->pp_core, buf + n, size - n);
		n += _mali_osk_snprintf(buf + n, size - n,
					"\tPP running job: %p, subjob %d \n",
					group->pp_running_job,
					group->pp_running_sub_job);
	}

	_MALI_OSK_LIST_FOREACHENTRY(child, temp, &group->group_list,
				    struct mali_group, group_list) {
		n += mali_group_dump_state(child, buf + n, size - n);
	}

	return n;
}
#endif

_mali_osk_errcode_t mali_group_upper_half_mmu(void *data)
{
	struct mali_group *group = (struct mali_group *)data;
	_mali_osk_errcode_t ret;

	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->mmu);

#if defined(CONFIG_MALI400_PROFILING) && defined (CONFIG_TRACEPOINTS)
#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	mali_executor_lock();
	if (!mali_group_is_working(group)) {
		/* Not working, so nothing to do */
		mali_executor_unlock();
		return _MALI_OSK_ERR_FAULT;
	}
#endif
	if (NULL != group->gp_core) {
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START |
					      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
					      MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
					      0, 0, /* No pid and tid for interrupt handler */
					      MALI_PROFILING_MAKE_EVENT_DATA_CORE_GP_MMU(0),
					      mali_mmu_get_rawstat(group->mmu), 0);
	} else {
		MALI_DEBUG_ASSERT_POINTER(group->pp_core);
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START |
					      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
					      MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
					      0, 0, /* No pid and tid for interrupt handler */
					      MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP_MMU(
						      mali_pp_core_get_id(group->pp_core)),
					      mali_mmu_get_rawstat(group->mmu), 0);
	}
#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	mali_executor_unlock();
#endif
#endif

	ret = mali_executor_interrupt_mmu(group, MALI_TRUE);

#if defined(CONFIG_MALI400_PROFILING) && defined (CONFIG_TRACEPOINTS)
#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	mali_executor_lock();
	if (!mali_group_is_working(group) && (!mali_group_power_is_on(group))) {
		/* group complete and on job shedule on it, it already power off */
		if (NULL != group->gp_core) {
			_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
						      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
						      MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
						      0, 0, /* No pid and tid for interrupt handler */
						      MALI_PROFILING_MAKE_EVENT_DATA_CORE_GP_MMU(0),
						      0xFFFFFFFF, 0);
		} else {
			_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
						      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
						      MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
						      0, 0, /* No pid and tid for interrupt handler */
						      MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP_MMU(
							      mali_pp_core_get_id(group->pp_core)),
						      0xFFFFFFFF, 0);
		}

		mali_executor_unlock();
		return ret;
	}
#endif

	if (NULL != group->gp_core) {
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
					      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
					      MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
					      0, 0, /* No pid and tid for interrupt handler */
					      MALI_PROFILING_MAKE_EVENT_DATA_CORE_GP_MMU(0),
					      mali_mmu_get_rawstat(group->mmu), 0);
	} else {
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
					      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
					      MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
					      0, 0, /* No pid and tid for interrupt handler */
					      MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP_MMU(
						      mali_pp_core_get_id(group->pp_core)),
					      mali_mmu_get_rawstat(group->mmu), 0);
	}
#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	mali_executor_unlock();
#endif
#endif

	return ret;
}

static void mali_group_bottom_half_mmu(void *data)
{
	struct mali_group *group = (struct mali_group *)data;

	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->mmu);

	if (NULL != group->gp_core) {
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START |
					      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
					      MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
					      0, _mali_osk_get_tid(), /* pid and tid */
					      MALI_PROFILING_MAKE_EVENT_DATA_CORE_GP_MMU(0),
					      mali_mmu_get_rawstat(group->mmu), 0);
	} else {
		MALI_DEBUG_ASSERT_POINTER(group->pp_core);
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START |
					      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
					      MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
					      0, _mali_osk_get_tid(), /* pid and tid */
					      MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP_MMU(
						      mali_pp_core_get_id(group->pp_core)),
					      mali_mmu_get_rawstat(group->mmu), 0);
	}

	mali_executor_interrupt_mmu(group, MALI_FALSE);

	if (NULL != group->gp_core) {
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
					      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
					      MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
					      0, _mali_osk_get_tid(), /* pid and tid */
					      MALI_PROFILING_MAKE_EVENT_DATA_CORE_GP_MMU(0),
					      mali_mmu_get_rawstat(group->mmu), 0);
	} else {
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
					      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
					      MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
					      0, _mali_osk_get_tid(), /* pid and tid */
					      MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP_MMU(
						      mali_pp_core_get_id(group->pp_core)),
					      mali_mmu_get_rawstat(group->mmu), 0);
	}
}

_mali_osk_errcode_t mali_group_upper_half_gp(void *data)
{
	struct mali_group *group = (struct mali_group *)data;
	_mali_osk_errcode_t ret;

	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->gp_core);
	MALI_DEBUG_ASSERT_POINTER(group->mmu);

#if defined(CONFIG_MALI400_PROFILING) && defined (CONFIG_TRACEPOINTS)
#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	mali_executor_lock();
	if (!mali_group_is_working(group)) {
		/* Not working, so nothing to do */
		mali_executor_unlock();
		return _MALI_OSK_ERR_FAULT;
	}
#endif
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START |
				      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
				      MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
				      0, 0, /* No pid and tid for interrupt handler */
				      MALI_PROFILING_MAKE_EVENT_DATA_CORE_GP(0),
				      mali_gp_get_rawstat(group->gp_core), 0);

	MALI_DEBUG_PRINT(4, ("Group: Interrupt 0x%08X from %s\n",
			     mali_gp_get_rawstat(group->gp_core),
			     mali_group_core_description(group)));
#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	mali_executor_unlock();
#endif
#endif
	ret = mali_executor_interrupt_gp(group, MALI_TRUE);

#if defined(CONFIG_MALI400_PROFILING) && defined (CONFIG_TRACEPOINTS)
#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	mali_executor_lock();
	if (!mali_group_is_working(group) && (!mali_group_power_is_on(group))) {
		/* group complete and on job shedule on it, it already power off */
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
					      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
					      MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
					      0, 0, /* No pid and tid for interrupt handler */
					      MALI_PROFILING_MAKE_EVENT_DATA_CORE_GP(0),
					      0xFFFFFFFF, 0);
		mali_executor_unlock();
		return ret;
	}
#endif
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
				      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
				      MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
				      0, 0, /* No pid and tid for interrupt handler */
				      MALI_PROFILING_MAKE_EVENT_DATA_CORE_GP(0),
				      mali_gp_get_rawstat(group->gp_core), 0);
#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	mali_executor_unlock();
#endif
#endif
	return ret;
}

static void mali_group_bottom_half_gp(void *data)
{
	struct mali_group *group = (struct mali_group *)data;

	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->gp_core);
	MALI_DEBUG_ASSERT_POINTER(group->mmu);

	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START |
				      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
				      MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
				      0, _mali_osk_get_tid(), /* pid and tid */
				      MALI_PROFILING_MAKE_EVENT_DATA_CORE_GP(0),
				      mali_gp_get_rawstat(group->gp_core), 0);

	mali_executor_interrupt_gp(group, MALI_FALSE);

	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
				      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
				      MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
				      0, _mali_osk_get_tid(), /* pid and tid */
				      MALI_PROFILING_MAKE_EVENT_DATA_CORE_GP(0),
				      mali_gp_get_rawstat(group->gp_core), 0);
}

_mali_osk_errcode_t mali_group_upper_half_pp(void *data)
{
	struct mali_group *group = (struct mali_group *)data;
	_mali_osk_errcode_t ret;

	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->pp_core);
	MALI_DEBUG_ASSERT_POINTER(group->mmu);

#if defined(CONFIG_MALI400_PROFILING) && defined (CONFIG_TRACEPOINTS)
#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	mali_executor_lock();
	if (!mali_group_is_working(group)) {
		/* Not working, so nothing to do */
		mali_executor_unlock();
		return _MALI_OSK_ERR_FAULT;
	}
#endif

	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START |
				      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
				      MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
				      0, 0, /* No pid and tid for interrupt handler */
				      MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP(
					      mali_pp_core_get_id(group->pp_core)),
				      mali_pp_get_rawstat(group->pp_core), 0);

	MALI_DEBUG_PRINT(4, ("Group: Interrupt 0x%08X from %s\n",
			     mali_pp_get_rawstat(group->pp_core),
			     mali_group_core_description(group)));
#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	mali_executor_unlock();
#endif
#endif

	ret = mali_executor_interrupt_pp(group, MALI_TRUE);

#if defined(CONFIG_MALI400_PROFILING) && defined (CONFIG_TRACEPOINTS)
#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	mali_executor_lock();
	if (!mali_group_is_working(group) && (!mali_group_power_is_on(group))) {
		/* group complete and on job shedule on it, it already power off */
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
					      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
					      MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
					      0, 0, /* No pid and tid for interrupt handler */
					      MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP(
						      mali_pp_core_get_id(group->pp_core)),
					      0xFFFFFFFF, 0);
		mali_executor_unlock();
		return ret;
	}
#endif
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
				      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
				      MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF,
				      0, 0, /* No pid and tid for interrupt handler */
				      MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP(
					      mali_pp_core_get_id(group->pp_core)),
				      mali_pp_get_rawstat(group->pp_core), 0);
#if defined(CONFIG_MALI_SHARED_INTERRUPTS)
	mali_executor_unlock();
#endif
#endif
	return ret;
}

static void mali_group_bottom_half_pp(void *data)
{
	struct mali_group *group = (struct mali_group *)data;

	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(group->pp_core);
	MALI_DEBUG_ASSERT_POINTER(group->mmu);

	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_START |
				      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
				      MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
				      0, _mali_osk_get_tid(), /* pid and tid */
				      MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP(
					      mali_pp_core_get_id(group->pp_core)),
				      mali_pp_get_rawstat(group->pp_core), 0);

	mali_executor_interrupt_pp(group, MALI_FALSE);

	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_STOP |
				      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
				      MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF,
				      0, _mali_osk_get_tid(), /* pid and tid */
				      MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP(
					      mali_pp_core_get_id(group->pp_core)),
				      mali_pp_get_rawstat(group->pp_core), 0);
}

static void mali_group_timeout(void *data)
{
	struct mali_group *group = (struct mali_group *)data;
	MALI_DEBUG_ASSERT_POINTER(group);

	MALI_DEBUG_PRINT(2, ("Group: timeout handler for %s at %u\n",
			     mali_group_core_description(group),
			     _mali_osk_time_tickcount()));

	if (NULL != group->gp_core) {
		mali_group_schedule_bottom_half_gp(group);
	} else {
		MALI_DEBUG_ASSERT_POINTER(group->pp_core);
		mali_group_schedule_bottom_half_pp(group);
	}
}

mali_bool mali_group_zap_session(struct mali_group *group,
				 struct mali_session_data *session)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	if (group->session != session) {
		/* not running from this session */
		return MALI_TRUE; /* success */
	}

	if (group->is_working) {
		/* The Zap also does the stall and disable_stall */
		mali_bool zap_success = mali_mmu_zap_tlb(group->mmu);
		return zap_success;
	} else {
		/* Just remove the session instead of zapping */
		mali_group_clear_session(group);
		return MALI_TRUE; /* success */
	}
}

#if defined(CONFIG_MALI400_PROFILING)
static void mali_group_report_l2_cache_counters_per_core(struct mali_group *group, u32 core_num)
{
	u32 source0 = 0;
	u32 value0 = 0;
	u32 source1 = 0;
	u32 value1 = 0;
	u32 profiling_channel = 0;

	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();

	switch (core_num) {
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
