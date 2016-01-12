/*
 * Copyright (C) 2011-2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_pm.h"
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_osk_mali.h"
#include "mali_scheduler.h"
#include "mali_group.h"
#include "mali_pm_domain.h"
#include "mali_pmu.h"

#include "mali_executor.h"
#include "mali_control_timer.h"

#if defined(DEBUG)
u32 num_pm_runtime_resume = 0;
u32 num_pm_updates = 0;
u32 num_pm_updates_up = 0;
u32 num_pm_updates_down = 0;
#endif

#define MALI_PM_DOMAIN_DUMMY_MASK (1 << MALI_DOMAIN_INDEX_DUMMY)

/* lock protecting power state (including pm_domains) */
static _mali_osk_spinlock_irq_t *pm_lock_state = NULL;

/* the wanted domain mask (protected by pm_lock_state) */
static u32 pd_mask_wanted = 0;

/* used to deferring the actual power changes */
static _mali_osk_wq_work_t *pm_work = NULL;

/* lock protecting power change execution */
static _mali_osk_mutex_t *pm_lock_exec = NULL;

/* PMU domains which are actually powered on (protected by pm_lock_exec) */
static u32 pmu_mask_current = 0;

/*
 * domains which marked as powered on (protected by pm_lock_exec)
 * This can be different from pmu_mask_current right after GPU power on
 * if the PMU domains default to powered up.
 */
static u32 pd_mask_current = 0;

static u16 domain_config[MALI_MAX_NUMBER_OF_DOMAINS] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1 << MALI_DOMAIN_INDEX_DUMMY
};

/* The relative core power cost */
#define MALI_GP_COST 3
#define MALI_PP_COST 6
#define MALI_L2_COST 1

/*
 *We have MALI_MAX_NUMBER_OF_PP_PHYSICAL_CORES + 1 rows in this matrix
 *because we mush store the mask of different pp cores: 0, 1, 2, 3, 4, 5, 6, 7, 8.
 */
static int mali_pm_domain_power_cost_result[MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS + 1][MALI_MAX_NUMBER_OF_DOMAINS];
/*
 * Keep track of runtime PM state, so that we know
 * how to resume during OS resume.
 */
#ifdef CONFIG_PM_RUNTIME
static mali_bool mali_pm_runtime_active = MALI_FALSE;
#else
/* when kernel don't enable PM_RUNTIME, set the flag always true,
 * for GPU will not power off by runtime */
static mali_bool mali_pm_runtime_active = MALI_TRUE;
#endif

static void mali_pm_state_lock(void);
static void mali_pm_state_unlock(void);
static _mali_osk_errcode_t mali_pm_create_pm_domains(void);
static void mali_pm_set_pmu_domain_config(void);
static u32 mali_pm_get_registered_cores_mask(void);
static void mali_pm_update_sync_internal(void);
static mali_bool mali_pm_common_suspend(void);
static void mali_pm_update_work(void *data);
#if defined(DEBUG)
const char *mali_pm_mask_to_string(u32 mask);
const char *mali_pm_group_stats_to_string(void);
#endif

_mali_osk_errcode_t mali_pm_initialize(void)
{
	_mali_osk_errcode_t err;
	struct mali_pmu_core *pmu;

	pm_lock_state = _mali_osk_spinlock_irq_init(_MALI_OSK_LOCKFLAG_ORDERED,
			_MALI_OSK_LOCK_ORDER_PM_STATE);
	if (NULL == pm_lock_state) {
		mali_pm_terminate();
		return _MALI_OSK_ERR_FAULT;
	}

	pm_lock_exec = _mali_osk_mutex_init(_MALI_OSK_LOCKFLAG_ORDERED,
					    _MALI_OSK_LOCK_ORDER_PM_STATE);
	if (NULL == pm_lock_exec) {
		mali_pm_terminate();
		return _MALI_OSK_ERR_FAULT;
	}

	pm_work = _mali_osk_wq_create_work(mali_pm_update_work, NULL);
	if (NULL == pm_work) {
		mali_pm_terminate();
		return _MALI_OSK_ERR_FAULT;
	}

	pmu = mali_pmu_get_global_pmu_core();
	if (NULL != pmu) {
		/*
		 * We have a Mali PMU, set the correct domain
		 * configuration (default or custom)
		 */

		u32 registered_cores_mask;

		mali_pm_set_pmu_domain_config();

		registered_cores_mask = mali_pm_get_registered_cores_mask();
		mali_pmu_set_registered_cores_mask(pmu, registered_cores_mask);

		MALI_DEBUG_ASSERT(0 == pd_mask_wanted);
	}

	/* Create all power domains needed (at least one dummy domain) */
	err = mali_pm_create_pm_domains();
	if (_MALI_OSK_ERR_OK != err) {
		mali_pm_terminate();
		return err;
	}

	return _MALI_OSK_ERR_OK;
}

void mali_pm_terminate(void)
{
	if (NULL != pm_work) {
		_mali_osk_wq_delete_work(pm_work);
		pm_work = NULL;
	}

	mali_pm_domain_terminate();

	if (NULL != pm_lock_exec) {
		_mali_osk_mutex_term(pm_lock_exec);
		pm_lock_exec = NULL;
	}

	if (NULL != pm_lock_state) {
		_mali_osk_spinlock_irq_term(pm_lock_state);
		pm_lock_state = NULL;
	}
}

struct mali_pm_domain *mali_pm_register_l2_cache(u32 domain_index,
		struct mali_l2_cache_core *l2_cache)
{
	struct mali_pm_domain *domain;

	domain = mali_pm_domain_get_from_mask(domain_config[domain_index]);
	if (NULL == domain) {
		MALI_DEBUG_ASSERT(0 == domain_config[domain_index]);
		domain = mali_pm_domain_get_from_index(
				 MALI_DOMAIN_INDEX_DUMMY);
		domain_config[domain_index] = MALI_PM_DOMAIN_DUMMY_MASK;
	} else {
		MALI_DEBUG_ASSERT(0 != domain_config[domain_index]);
	}

	MALI_DEBUG_ASSERT(NULL != domain);

	mali_pm_domain_add_l2_cache(domain, l2_cache);

	return domain; /* return the actual domain this was registered in */
}

struct mali_pm_domain *mali_pm_register_group(u32 domain_index,
		struct mali_group *group)
{
	struct mali_pm_domain *domain;

	domain = mali_pm_domain_get_from_mask(domain_config[domain_index]);
	if (NULL == domain) {
		MALI_DEBUG_ASSERT(0 == domain_config[domain_index]);
		domain = mali_pm_domain_get_from_index(
				 MALI_DOMAIN_INDEX_DUMMY);
		domain_config[domain_index] = MALI_PM_DOMAIN_DUMMY_MASK;
	} else {
		MALI_DEBUG_ASSERT(0 != domain_config[domain_index]);
	}

	MALI_DEBUG_ASSERT(NULL != domain);

	mali_pm_domain_add_group(domain, group);

	return domain; /* return the actual domain this was registered in */
}

mali_bool mali_pm_get_domain_refs(struct mali_pm_domain **domains,
				  struct mali_group **groups,
				  u32 num_domains)
{
	mali_bool ret = MALI_TRUE; /* Assume all is powered on instantly */
	u32 i;

	mali_pm_state_lock();

	for (i = 0; i < num_domains; i++) {
		MALI_DEBUG_ASSERT_POINTER(domains[i]);
		pd_mask_wanted |= mali_pm_domain_ref_get(domains[i]);
		if (MALI_FALSE == mali_pm_domain_power_is_on(domains[i])) {
			/*
			 * Tell caller that the corresponding group
			 * was not already powered on.
			 */
			ret = MALI_FALSE;
		} else {
			/*
			 * There is a time gap between we power on the domain and
			 * set the power state of the corresponding groups to be on.
			 */
			if (NULL != groups[i] &&
			    MALI_FALSE == mali_group_power_is_on(groups[i])) {
				ret = MALI_FALSE;
			}
		}
	}

	MALI_DEBUG_PRINT(3, ("PM: wanted domain mask = 0x%08X (get refs)\n", pd_mask_wanted));

	mali_pm_state_unlock();

	return ret;
}

mali_bool mali_pm_put_domain_refs(struct mali_pm_domain **domains,
				  u32 num_domains)
{
	u32 mask = 0;
	mali_bool ret;
	u32 i;

	mali_pm_state_lock();

	for (i = 0; i < num_domains; i++) {
		MALI_DEBUG_ASSERT_POINTER(domains[i]);
		mask |= mali_pm_domain_ref_put(domains[i]);
	}

	if (0 == mask) {
		/* return false, all domains should still stay on */
		ret = MALI_FALSE;
	} else {
		/* Assert that we are dealing with a change */
		MALI_DEBUG_ASSERT((pd_mask_wanted & mask) == mask);

		/* Update our desired domain mask */
		pd_mask_wanted &= ~mask;

		/* return true; one or more domains can now be powered down */
		ret = MALI_TRUE;
	}

	MALI_DEBUG_PRINT(3, ("PM: wanted domain mask = 0x%08X (put refs)\n", pd_mask_wanted));

	mali_pm_state_unlock();

	return ret;
}

void mali_pm_init_begin(void)
{
	struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();

	_mali_osk_pm_dev_ref_get_sync();

	/* Ensure all PMU domains are on */
	if (NULL != pmu) {
		mali_pmu_power_up_all(pmu);
	}
}

void mali_pm_init_end(void)
{
	struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();

	/* Ensure all PMU domains are off */
	if (NULL != pmu) {
		mali_pmu_power_down_all(pmu);
	}

	_mali_osk_pm_dev_ref_put();
}

void mali_pm_update_sync(void)
{
	mali_pm_exec_lock();

	if (MALI_TRUE == mali_pm_runtime_active) {
		/*
		 * Only update if GPU is powered on.
		 * Deactivation of the last group will result in both a
		 * deferred runtime PM suspend operation and
		 * deferred execution of this function.
		 * mali_pm_runtime_active will be false if runtime PM
		 * executed first and thus the GPU is now fully powered off.
		 */
		mali_pm_update_sync_internal();
	}

	mali_pm_exec_unlock();
}

void mali_pm_update_async(void)
{
	_mali_osk_wq_schedule_work(pm_work);
}

void mali_pm_os_suspend(mali_bool os_suspend)
{
	int ret;

	MALI_DEBUG_PRINT(3, ("Mali PM: OS suspend\n"));

	/* Suspend execution of all jobs, and go to inactive state */
	mali_executor_suspend();

	if (os_suspend) {
		mali_control_timer_suspend(MALI_TRUE);
	}

	mali_pm_exec_lock();

	ret = mali_pm_common_suspend();

	MALI_DEBUG_ASSERT(MALI_TRUE == ret);
	MALI_IGNORE(ret);

	mali_pm_exec_unlock();
}

void mali_pm_os_resume(void)
{
	struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();

	MALI_DEBUG_PRINT(3, ("Mali PM: OS resume\n"));

	mali_pm_exec_lock();

#if defined(DEBUG)
	mali_pm_state_lock();

	/* Assert that things are as we left them in os_suspend(). */
	MALI_DEBUG_ASSERT(0 == pd_mask_wanted);
	MALI_DEBUG_ASSERT(0 == pd_mask_current);
	MALI_DEBUG_ASSERT(0 == pmu_mask_current);

	MALI_DEBUG_ASSERT(MALI_TRUE == mali_pm_domain_all_unused());

	mali_pm_state_unlock();
#endif

	if (MALI_TRUE == mali_pm_runtime_active) {
		/* Runtime PM was active, so reset PMU */
		if (NULL != pmu) {
			mali_pmu_reset(pmu);
			pmu_mask_current = mali_pmu_get_mask(pmu);

			MALI_DEBUG_PRINT(3, ("Mali PM: OS resume 0x%x \n", pmu_mask_current));
		}

		mali_pm_update_sync_internal();
	}

	mali_pm_exec_unlock();

	/* Start executing jobs again */
	mali_executor_resume();
}

mali_bool mali_pm_runtime_suspend(void)
{
	mali_bool ret;

	MALI_DEBUG_PRINT(3, ("Mali PM: Runtime suspend\n"));

	mali_pm_exec_lock();

	/*
	 * Put SW state directly into "off" state, and do not bother to power
	 * down each power domain, because entire GPU will be powered off
	 * when we return.
	 * For runtime PM suspend, in contrast to OS suspend, there is a race
	 * between this function and the mali_pm_update_sync_internal(), which
	 * is fine...
	 */
	ret = mali_pm_common_suspend();
	if (MALI_TRUE == ret) {
		mali_pm_runtime_active = MALI_FALSE;
	} else {
		/*
		 * Process the "power up" instead,
		 * which could have been "lost"
		 */
		mali_pm_update_sync_internal();
	}

	mali_pm_exec_unlock();

	return ret;
}

void mali_pm_runtime_resume(void)
{
	struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();

	mali_pm_exec_lock();

	mali_pm_runtime_active = MALI_TRUE;

#if defined(DEBUG)
	++num_pm_runtime_resume;

	mali_pm_state_lock();

	/*
	 * Assert that things are as we left them in runtime_suspend(),
	 * except for pd_mask_wanted which normally will be the reason we
	 * got here (job queued => domains wanted)
	 */
	MALI_DEBUG_ASSERT(0 == pd_mask_current);
	MALI_DEBUG_ASSERT(0 == pmu_mask_current);

	mali_pm_state_unlock();
#endif

	if (NULL != pmu) {
		mali_pmu_reset(pmu);
		pmu_mask_current = mali_pmu_get_mask(pmu);
		MALI_DEBUG_PRINT(3, ("Mali PM: Runtime resume 0x%x \n", pmu_mask_current));
	}

	/*
	 * Normally we are resumed because a job has just been queued.
	 * pd_mask_wanted should thus be != 0.
	 * It is however possible for others to take a Mali Runtime PM ref
	 * without having a job queued.
	 * We should however always call mali_pm_update_sync_internal(),
	 * because this will take care of any potential mismatch between
	 * pmu_mask_current and pd_mask_current.
	 */
	mali_pm_update_sync_internal();

	mali_pm_exec_unlock();
}

#if MALI_STATE_TRACKING
u32 mali_pm_dump_state_domain(struct mali_pm_domain *domain,
			      char *buf, u32 size)
{
	int n = 0;

	n += _mali_osk_snprintf(buf + n, size - n,
				"\tPower domain: id %u\n",
				mali_pm_domain_get_id(domain));

	n += _mali_osk_snprintf(buf + n, size - n,
				"\t\tMask: 0x%04x\n",
				mali_pm_domain_get_mask(domain));

	n += _mali_osk_snprintf(buf + n, size - n,
				"\t\tUse count: %u\n",
				mali_pm_domain_get_use_count(domain));

	n += _mali_osk_snprintf(buf + n, size - n,
				"\t\tCurrent power state: %s\n",
				(mali_pm_domain_get_mask(domain) & pd_mask_current) ?
				"On" : "Off");

	n += _mali_osk_snprintf(buf + n, size - n,
				"\t\tWanted power state: %s\n",
				(mali_pm_domain_get_mask(domain) & pd_mask_wanted) ?
				"On" : "Off");

	return n;
}
#endif

static void mali_pm_state_lock(void)
{
	_mali_osk_spinlock_irq_lock(pm_lock_state);
}

static void mali_pm_state_unlock(void)
{
	_mali_osk_spinlock_irq_unlock(pm_lock_state);
}

void mali_pm_exec_lock(void)
{
	_mali_osk_mutex_wait(pm_lock_exec);
}

void mali_pm_exec_unlock(void)
{
	_mali_osk_mutex_signal(pm_lock_exec);
}

static void mali_pm_domain_power_up(u32 power_up_mask,
				    struct mali_group *groups_up[MALI_MAX_NUMBER_OF_GROUPS],
				    u32 *num_groups_up,
				    struct mali_l2_cache_core *l2_up[MALI_MAX_NUMBER_OF_L2_CACHE_CORES],
				    u32 *num_l2_up)
{
	u32 domain_bit;
	u32 notify_mask = power_up_mask;

	MALI_DEBUG_ASSERT(0 != power_up_mask);
	MALI_DEBUG_ASSERT_POINTER(groups_up);
	MALI_DEBUG_ASSERT_POINTER(num_groups_up);
	MALI_DEBUG_ASSERT(0 == *num_groups_up);
	MALI_DEBUG_ASSERT_POINTER(l2_up);
	MALI_DEBUG_ASSERT_POINTER(num_l2_up);
	MALI_DEBUG_ASSERT(0 == *num_l2_up);

	MALI_DEBUG_ASSERT_LOCK_HELD(pm_lock_exec);
	MALI_DEBUG_ASSERT_LOCK_HELD(pm_lock_state);

	MALI_DEBUG_PRINT(5,
			 ("PM update:      Powering up domains: . [%s]\n",
			  mali_pm_mask_to_string(power_up_mask)));

	pd_mask_current |= power_up_mask;

	domain_bit = _mali_osk_fls(notify_mask);
	while (0 != domain_bit) {
		u32 domain_id = domain_bit - 1;
		struct mali_pm_domain *domain =
			mali_pm_domain_get_from_index(
				domain_id);
		struct mali_l2_cache_core *l2_cache;
		struct mali_l2_cache_core *l2_cache_tmp;
		struct mali_group *group;
		struct mali_group *group_tmp;

		/* Mark domain as powered up */
		mali_pm_domain_set_power_on(domain, MALI_TRUE);

		/*
		 * Make a note of the L2 and/or group(s) to notify
		 * (need to release the PM state lock before doing so)
		 */

		_MALI_OSK_LIST_FOREACHENTRY(l2_cache,
					    l2_cache_tmp,
					    mali_pm_domain_get_l2_cache_list(
						    domain),
					    struct mali_l2_cache_core,
					    pm_domain_list) {
			MALI_DEBUG_ASSERT(*num_l2_up <
					  MALI_MAX_NUMBER_OF_L2_CACHE_CORES);
			l2_up[*num_l2_up] = l2_cache;
			(*num_l2_up)++;
		}

		_MALI_OSK_LIST_FOREACHENTRY(group,
					    group_tmp,
					    mali_pm_domain_get_group_list(domain),
					    struct mali_group,
					    pm_domain_list) {
			MALI_DEBUG_ASSERT(*num_groups_up <
					  MALI_MAX_NUMBER_OF_GROUPS);
			groups_up[*num_groups_up] = group;

			(*num_groups_up)++;
		}

		/* Remove current bit and find next */
		notify_mask &= ~(1 << (domain_id));
		domain_bit = _mali_osk_fls(notify_mask);
	}
}
static void mali_pm_domain_power_down(u32 power_down_mask,
				      struct mali_group *groups_down[MALI_MAX_NUMBER_OF_GROUPS],
				      u32 *num_groups_down,
				      struct mali_l2_cache_core *l2_down[MALI_MAX_NUMBER_OF_L2_CACHE_CORES],
				      u32 *num_l2_down)
{
	u32 domain_bit;
	u32 notify_mask = power_down_mask;

	MALI_DEBUG_ASSERT(0 != power_down_mask);
	MALI_DEBUG_ASSERT_POINTER(groups_down);
	MALI_DEBUG_ASSERT_POINTER(num_groups_down);
	MALI_DEBUG_ASSERT(0 == *num_groups_down);
	MALI_DEBUG_ASSERT_POINTER(l2_down);
	MALI_DEBUG_ASSERT_POINTER(num_l2_down);
	MALI_DEBUG_ASSERT(0 == *num_l2_down);

	MALI_DEBUG_ASSERT_LOCK_HELD(pm_lock_exec);
	MALI_DEBUG_ASSERT_LOCK_HELD(pm_lock_state);

	MALI_DEBUG_PRINT(5,
			 ("PM update:      Powering down domains: [%s]\n",
			  mali_pm_mask_to_string(power_down_mask)));

	pd_mask_current &= ~power_down_mask;

	domain_bit = _mali_osk_fls(notify_mask);
	while (0 != domain_bit) {
		u32 domain_id = domain_bit - 1;
		struct mali_pm_domain *domain =
			mali_pm_domain_get_from_index(domain_id);
		struct mali_l2_cache_core *l2_cache;
		struct mali_l2_cache_core *l2_cache_tmp;
		struct mali_group *group;
		struct mali_group *group_tmp;

		/* Mark domain as powered down */
		mali_pm_domain_set_power_on(domain, MALI_FALSE);

		/*
		 * Make a note of the L2s and/or groups to notify
		 * (need to release the PM state lock before doing so)
		 */

		_MALI_OSK_LIST_FOREACHENTRY(l2_cache,
					    l2_cache_tmp,
					    mali_pm_domain_get_l2_cache_list(domain),
					    struct mali_l2_cache_core,
					    pm_domain_list) {
			MALI_DEBUG_ASSERT(*num_l2_down <
					  MALI_MAX_NUMBER_OF_L2_CACHE_CORES);
			l2_down[*num_l2_down] = l2_cache;
			(*num_l2_down)++;
		}

		_MALI_OSK_LIST_FOREACHENTRY(group,
					    group_tmp,
					    mali_pm_domain_get_group_list(domain),
					    struct mali_group,
					    pm_domain_list) {
			MALI_DEBUG_ASSERT(*num_groups_down <
					  MALI_MAX_NUMBER_OF_GROUPS);
			groups_down[*num_groups_down] = group;
			(*num_groups_down)++;
		}

		/* Remove current bit and find next */
		notify_mask &= ~(1 << (domain_id));
		domain_bit = _mali_osk_fls(notify_mask);
	}
}

/*
 * Execute pending power domain changes
 * pm_lock_exec lock must be taken by caller.
 */
static void mali_pm_update_sync_internal(void)
{
	/*
	 * This should only be called in non-atomic context
	 * (normally as deferred work)
	 *
	 * Look at the pending power domain changes, and execute these.
	 * Make sure group and schedulers are notified about changes.
	 */

	struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();

	u32 power_down_mask;
	u32 power_up_mask;

	MALI_DEBUG_ASSERT_LOCK_HELD(pm_lock_exec);

#if defined(DEBUG)
	++num_pm_updates;
#endif

	/* Hold PM state lock while we look at (and obey) the wanted state */
	mali_pm_state_lock();

	MALI_DEBUG_PRINT(5, ("PM update pre:  Wanted domain mask: .. [%s]\n",
			     mali_pm_mask_to_string(pd_mask_wanted)));
	MALI_DEBUG_PRINT(5, ("PM update pre:  Current domain mask: . [%s]\n",
			     mali_pm_mask_to_string(pd_mask_current)));
	MALI_DEBUG_PRINT(5, ("PM update pre:  Current PMU mask: .... [%s]\n",
			     mali_pm_mask_to_string(pmu_mask_current)));
	MALI_DEBUG_PRINT(5, ("PM update pre:  Group power stats: ... <%s>\n",
			     mali_pm_group_stats_to_string()));

	/* Figure out which cores we need to power on */
	power_up_mask = pd_mask_wanted &
			(pd_mask_wanted ^ pd_mask_current);

	if (0 != power_up_mask) {
		u32 power_up_mask_pmu;
		struct mali_group *groups_up[MALI_MAX_NUMBER_OF_GROUPS];
		u32 num_groups_up = 0;
		struct mali_l2_cache_core *
			l2_up[MALI_MAX_NUMBER_OF_L2_CACHE_CORES];
		u32 num_l2_up = 0;
		u32 i;

#if defined(DEBUG)
		++num_pm_updates_up;
#endif

		/*
		 * Make sure dummy/global domain is always included when
		 * powering up, since this is controlled by runtime PM,
		 * and device power is on at this stage.
		 */
		power_up_mask |= MALI_PM_DOMAIN_DUMMY_MASK;

		/* Power up only real PMU domains */
		power_up_mask_pmu = power_up_mask & ~MALI_PM_DOMAIN_DUMMY_MASK;

		/* But not those that happen to be powered on already */
		power_up_mask_pmu &= (power_up_mask ^ pmu_mask_current) &
				     power_up_mask;

		if (0 != power_up_mask_pmu) {
			MALI_DEBUG_ASSERT(NULL != pmu);
			pmu_mask_current |= power_up_mask_pmu;
			mali_pmu_power_up(pmu, power_up_mask_pmu);
		}

		/*
		 * Put the domains themselves in power up state.
		 * We get the groups and L2s to notify in return.
		 */
		mali_pm_domain_power_up(power_up_mask,
					groups_up, &num_groups_up,
					l2_up, &num_l2_up);

		/* Need to unlock PM state lock before notifying L2 + groups */
		mali_pm_state_unlock();

		/* Notify each L2 cache that we have be powered up */
		for (i = 0; i < num_l2_up; i++) {
			mali_l2_cache_power_up(l2_up[i]);
		}

		/*
		 * Tell execution module about all the groups we have
		 * powered up. Groups will be notified as a result of this.
		 */
		mali_executor_group_power_up(groups_up, num_groups_up);

		/* Lock state again before checking for power down */
		mali_pm_state_lock();
	}

	/* Figure out which cores we need to power off */
	power_down_mask = pd_mask_current &
			  (pd_mask_wanted ^ pd_mask_current);

	/*
	 * Never power down the dummy/global domain here. This is to be done
	 * from a suspend request (since this domain is only physicall powered
	 * down at that point)
	 */
	power_down_mask &= ~MALI_PM_DOMAIN_DUMMY_MASK;

	if (0 != power_down_mask) {
		u32 power_down_mask_pmu;
		struct mali_group *groups_down[MALI_MAX_NUMBER_OF_GROUPS];
		u32 num_groups_down = 0;
		struct mali_l2_cache_core *
			l2_down[MALI_MAX_NUMBER_OF_L2_CACHE_CORES];
		u32 num_l2_down = 0;
		u32 i;

#if defined(DEBUG)
		++num_pm_updates_down;
#endif

		/*
		 * Put the domains themselves in power down state.
		 * We get the groups and L2s to notify in return.
		 */
		mali_pm_domain_power_down(power_down_mask,
					  groups_down, &num_groups_down,
					  l2_down, &num_l2_down);

		/* Need to unlock PM state lock before notifying L2 + groups */
		mali_pm_state_unlock();

		/*
		 * Tell execution module about all the groups we will be
		 * powering down. Groups will be notified as a result of this.
		 */
		if (0 < num_groups_down) {
			mali_executor_group_power_down(groups_down, num_groups_down);
		}

		/* Notify each L2 cache that we will be powering down */
		for (i = 0; i < num_l2_down; i++) {
			mali_l2_cache_power_down(l2_down[i]);
		}

		/*
		 * Power down only PMU domains which should not stay on
		 * Some domains might for instance currently be incorrectly
		 * powered up if default domain power state is all on.
		 */
		power_down_mask_pmu = pmu_mask_current & (~pd_mask_current);

		if (0 != power_down_mask_pmu) {
			MALI_DEBUG_ASSERT(NULL != pmu);
			pmu_mask_current &= ~power_down_mask_pmu;
			mali_pmu_power_down(pmu, power_down_mask_pmu);

		}
	} else {
		/*
		 * Power down only PMU domains which should not stay on
		 * Some domains might for instance currently be incorrectly
		 * powered up if default domain power state is all on.
		 */
		u32 power_down_mask_pmu;

		/* No need for state lock since we'll only update PMU */
		mali_pm_state_unlock();

		power_down_mask_pmu = pmu_mask_current & (~pd_mask_current);

		if (0 != power_down_mask_pmu) {
			MALI_DEBUG_ASSERT(NULL != pmu);
			pmu_mask_current &= ~power_down_mask_pmu;
			mali_pmu_power_down(pmu, power_down_mask_pmu);
		}
	}

	MALI_DEBUG_PRINT(5, ("PM update post: Current domain mask: . [%s]\n",
			     mali_pm_mask_to_string(pd_mask_current)));
	MALI_DEBUG_PRINT(5, ("PM update post: Current PMU mask: .... [%s]\n",
			     mali_pm_mask_to_string(pmu_mask_current)));
	MALI_DEBUG_PRINT(5, ("PM update post: Group power stats: ... <%s>\n",
			     mali_pm_group_stats_to_string()));
}

static mali_bool mali_pm_common_suspend(void)
{
	mali_pm_state_lock();

	if (0 != pd_mask_wanted) {
		MALI_DEBUG_PRINT(5, ("PM: Aborting suspend operation\n\n\n"));
		mali_pm_state_unlock();
		return MALI_FALSE;
	}

	MALI_DEBUG_PRINT(5, ("PM suspend pre: Wanted domain mask: .. [%s]\n",
			     mali_pm_mask_to_string(pd_mask_wanted)));
	MALI_DEBUG_PRINT(5, ("PM suspend pre: Current domain mask: . [%s]\n",
			     mali_pm_mask_to_string(pd_mask_current)));
	MALI_DEBUG_PRINT(5, ("PM suspend pre: Current PMU mask: .... [%s]\n",
			     mali_pm_mask_to_string(pmu_mask_current)));
	MALI_DEBUG_PRINT(5, ("PM suspend pre: Group power stats: ... <%s>\n",
			     mali_pm_group_stats_to_string()));

	if (0 != pd_mask_current) {
		/*
		 * We have still some domains powered on.
		 * It is for instance very normal that at least the
		 * dummy/global domain is marked as powered on at this point.
		 * (because it is physically powered on until this function
		 * returns)
		 */

		struct mali_group *groups_down[MALI_MAX_NUMBER_OF_GROUPS];
		u32 num_groups_down = 0;
		struct mali_l2_cache_core *
			l2_down[MALI_MAX_NUMBER_OF_L2_CACHE_CORES];
		u32 num_l2_down = 0;
		u32 i;

		/*
		 * Put the domains themselves in power down state.
		 * We get the groups and L2s to notify in return.
		 */
		mali_pm_domain_power_down(pd_mask_current,
					  groups_down,
					  &num_groups_down,
					  l2_down,
					  &num_l2_down);

		MALI_DEBUG_ASSERT(0 == pd_mask_current);
		MALI_DEBUG_ASSERT(MALI_TRUE == mali_pm_domain_all_unused());

		/* Need to unlock PM state lock before notifying L2 + groups */
		mali_pm_state_unlock();

		/*
		 * Tell execution module about all the groups we will be
		 * powering down. Groups will be notified as a result of this.
		 */
		if (0 < num_groups_down) {
			mali_executor_group_power_down(groups_down, num_groups_down);
		}

		/* Notify each L2 cache that we will be powering down */
		for (i = 0; i < num_l2_down; i++) {
			mali_l2_cache_power_down(l2_down[i]);
		}

		pmu_mask_current = 0;
	} else {
		MALI_DEBUG_ASSERT(0 == pmu_mask_current);

		MALI_DEBUG_ASSERT(MALI_TRUE == mali_pm_domain_all_unused());

		mali_pm_state_unlock();
	}

	MALI_DEBUG_PRINT(5, ("PM suspend post: Current domain mask:  [%s]\n",
			     mali_pm_mask_to_string(pd_mask_current)));
	MALI_DEBUG_PRINT(5, ("PM suspend post: Current PMU mask: ... [%s]\n",
			     mali_pm_mask_to_string(pmu_mask_current)));
	MALI_DEBUG_PRINT(5, ("PM suspend post: Group power stats: .. <%s>\n",
			     mali_pm_group_stats_to_string()));

	return MALI_TRUE;
}

static void mali_pm_update_work(void *data)
{
	MALI_IGNORE(data);
	mali_pm_update_sync();
}

static _mali_osk_errcode_t mali_pm_create_pm_domains(void)
{
	int i;

	/* Create all domains (including dummy domain) */
	for (i = 0; i < MALI_MAX_NUMBER_OF_DOMAINS; i++) {
		if (0x0 == domain_config[i]) continue;

		if (NULL == mali_pm_domain_create(domain_config[i])) {
			return _MALI_OSK_ERR_NOMEM;
		}
	}

	return _MALI_OSK_ERR_OK;
}

static void mali_pm_set_default_pm_domain_config(void)
{
	MALI_DEBUG_ASSERT(0 != _mali_osk_resource_base_address());

	/* GP core */
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(
		    MALI_OFFSET_GP, NULL)) {
		domain_config[MALI_DOMAIN_INDEX_GP] = 0x01;
	}

	/* PP0 - PP3 core */
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(
		    MALI_OFFSET_PP0, NULL)) {
		if (mali_is_mali400()) {
			domain_config[MALI_DOMAIN_INDEX_PP0] = 0x01 << 2;
		} else if (mali_is_mali450()) {
			domain_config[MALI_DOMAIN_INDEX_PP0] = 0x01 << 1;
		} else if (mali_is_mali470()) {
			domain_config[MALI_DOMAIN_INDEX_PP0] = 0x01 << 0;
		}
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(
		    MALI_OFFSET_PP1, NULL)) {
		if (mali_is_mali400()) {
			domain_config[MALI_DOMAIN_INDEX_PP1] = 0x01 << 3;
		} else if (mali_is_mali450()) {
			domain_config[MALI_DOMAIN_INDEX_PP1] = 0x01 << 2;
		} else if (mali_is_mali470()) {
			domain_config[MALI_DOMAIN_INDEX_PP1] = 0x01 << 1;
		}
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(
		    MALI_OFFSET_PP2, NULL)) {
		if (mali_is_mali400()) {
			domain_config[MALI_DOMAIN_INDEX_PP2] = 0x01 << 4;
		} else if (mali_is_mali450()) {
			domain_config[MALI_DOMAIN_INDEX_PP2] = 0x01 << 2;
		} else if (mali_is_mali470()) {
			domain_config[MALI_DOMAIN_INDEX_PP2] = 0x01 << 1;
		}
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(
		    MALI_OFFSET_PP3, NULL)) {
		if (mali_is_mali400()) {
			domain_config[MALI_DOMAIN_INDEX_PP3] = 0x01 << 5;
		} else if (mali_is_mali450()) {
			domain_config[MALI_DOMAIN_INDEX_PP3] = 0x01 << 2;
		} else if (mali_is_mali470()) {
			domain_config[MALI_DOMAIN_INDEX_PP3] = 0x01 << 1;
		}
	}

	/* PP4 - PP7 */
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(
		    MALI_OFFSET_PP4, NULL)) {
		domain_config[MALI_DOMAIN_INDEX_PP4] = 0x01 << 3;
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(
		    MALI_OFFSET_PP5, NULL)) {
		domain_config[MALI_DOMAIN_INDEX_PP5] = 0x01 << 3;
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(
		    MALI_OFFSET_PP6, NULL)) {
		domain_config[MALI_DOMAIN_INDEX_PP6] = 0x01 << 3;
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(
		    MALI_OFFSET_PP7, NULL)) {
		domain_config[MALI_DOMAIN_INDEX_PP7] = 0x01 << 3;
	}

	/* L2gp/L2PP0/L2PP4 */
	if (mali_is_mali400()) {
		if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(
			    MALI400_OFFSET_L2_CACHE0, NULL)) {
			domain_config[MALI_DOMAIN_INDEX_L20] = 0x01 << 1;
		}
	} else if (mali_is_mali450()) {
		if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(
			    MALI450_OFFSET_L2_CACHE0, NULL)) {
			domain_config[MALI_DOMAIN_INDEX_L20] = 0x01 << 0;
		}

		if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(
			    MALI450_OFFSET_L2_CACHE1, NULL)) {
			domain_config[MALI_DOMAIN_INDEX_L21] = 0x01 << 1;
		}

		if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(
			    MALI450_OFFSET_L2_CACHE2, NULL)) {
			domain_config[MALI_DOMAIN_INDEX_L22] = 0x01 << 3;
		}
	} else if (mali_is_mali470()) {
		if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(
			    MALI470_OFFSET_L2_CACHE1, NULL)) {
			domain_config[MALI_DOMAIN_INDEX_L21] = 0x01 << 0;
		}
	}
}

static u32 mali_pm_get_registered_cores_mask(void)
{
	int i = 0;
	u32 mask = 0;

	for (i = 0; i < MALI_DOMAIN_INDEX_DUMMY; i++) {
		mask |= domain_config[i];
	}

	return mask;
}

static void mali_pm_set_pmu_domain_config(void)
{
	int i = 0;

	_mali_osk_device_data_pmu_config_get(domain_config, MALI_MAX_NUMBER_OF_DOMAINS - 1);

	for (i = 0; i < MALI_MAX_NUMBER_OF_DOMAINS - 1; i++) {
		if (0 != domain_config[i]) {
			MALI_DEBUG_PRINT(2, ("Using customer pmu config:\n"));
			break;
		}
	}

	if (MALI_MAX_NUMBER_OF_DOMAINS - 1 == i) {
		MALI_DEBUG_PRINT(2, ("Using hw detect pmu config:\n"));
		mali_pm_set_default_pm_domain_config();
	}

	for (i = 0; i < MALI_MAX_NUMBER_OF_DOMAINS - 1; i++) {
		if (domain_config[i]) {
			MALI_DEBUG_PRINT(2, ("domain_config[%d] = 0x%x \n", i, domain_config[i]));
		}
	}
	/* Can't override dummy domain mask */
	domain_config[MALI_DOMAIN_INDEX_DUMMY] =
		1 << MALI_DOMAIN_INDEX_DUMMY;
}

#if defined(DEBUG)
const char *mali_pm_mask_to_string(u32 mask)
{
	static char bit_str[MALI_MAX_NUMBER_OF_DOMAINS + 1];
	int bit;
	int str_pos = 0;

	/* Must be protected by lock since we use shared string buffer */
	if (NULL != pm_lock_exec) {
		MALI_DEBUG_ASSERT_LOCK_HELD(pm_lock_exec);
	}

	for (bit = MALI_MAX_NUMBER_OF_DOMAINS - 1; bit >= 0; bit--) {
		if (mask & (1 << bit)) {
			bit_str[str_pos] = 'X';
		} else {
			bit_str[str_pos] = '-';
		}
		str_pos++;
	}

	bit_str[MALI_MAX_NUMBER_OF_DOMAINS] = '\0';

	return bit_str;
}

const char *mali_pm_group_stats_to_string(void)
{
	static char bit_str[MALI_MAX_NUMBER_OF_GROUPS + 1];
	u32 num_groups = mali_group_get_glob_num_groups();
	u32 i;

	/* Must be protected by lock since we use shared string buffer */
	if (NULL != pm_lock_exec) {
		MALI_DEBUG_ASSERT_LOCK_HELD(pm_lock_exec);
	}

	for (i = 0; i < num_groups && i < MALI_MAX_NUMBER_OF_GROUPS; i++) {
		struct mali_group *group;

		group = mali_group_get_glob_group(i);

		if (MALI_TRUE == mali_group_power_is_on(group)) {
			bit_str[i] = 'X';
		} else {
			bit_str[i] = '-';
		}
	}

	bit_str[i] = '\0';

	return bit_str;
}
#endif

/*
 * num_pp is the number of PP cores which will be powered on given this mask
 * cost is the total power cost of cores which will be powered on given this mask
 */
static void mali_pm_stat_from_mask(u32 mask, u32 *num_pp, u32 *cost)
{
	u32 i;

	/* loop through all cores */
	for (i = 0; i < MALI_MAX_NUMBER_OF_DOMAINS; i++) {
		if (!(domain_config[i] & mask)) {
			continue;
		}

		switch (i) {
		case MALI_DOMAIN_INDEX_GP:
			*cost += MALI_GP_COST;

			break;
		case MALI_DOMAIN_INDEX_PP0: /* Fall through */
		case MALI_DOMAIN_INDEX_PP1: /* Fall through */
		case MALI_DOMAIN_INDEX_PP2: /* Fall through */
		case MALI_DOMAIN_INDEX_PP3:
			if (mali_is_mali400()) {
				if ((domain_config[MALI_DOMAIN_INDEX_L20] & mask)
				    || (domain_config[MALI_DOMAIN_INDEX_DUMMY]
					== domain_config[MALI_DOMAIN_INDEX_L20])) {
					*num_pp += 1;
				}
			} else {
				if ((domain_config[MALI_DOMAIN_INDEX_L21] & mask)
				    || (domain_config[MALI_DOMAIN_INDEX_DUMMY]
					== domain_config[MALI_DOMAIN_INDEX_L21])) {
					*num_pp += 1;
				}
			}

			*cost += MALI_PP_COST;
			break;
		case MALI_DOMAIN_INDEX_PP4: /* Fall through */
		case MALI_DOMAIN_INDEX_PP5: /* Fall through */
		case MALI_DOMAIN_INDEX_PP6: /* Fall through */
		case MALI_DOMAIN_INDEX_PP7:
			MALI_DEBUG_ASSERT(mali_is_mali450());

			if ((domain_config[MALI_DOMAIN_INDEX_L22] & mask)
			    || (domain_config[MALI_DOMAIN_INDEX_DUMMY]
				== domain_config[MALI_DOMAIN_INDEX_L22])) {
				*num_pp += 1;
			}

			*cost += MALI_PP_COST;
			break;
		case MALI_DOMAIN_INDEX_L20: /* Fall through */
		case MALI_DOMAIN_INDEX_L21: /* Fall through */
		case MALI_DOMAIN_INDEX_L22:
			*cost += MALI_L2_COST;

			break;
		}
	}
}

void mali_pm_power_cost_setup(void)
{
	/*
	 * Two parallel arrays which store the best domain mask and its cost
	 * The index is the number of PP cores, E.g. Index 0 is for 1 PP option,
	 * might have mask 0x2 and with cost of 1, lower cost is better
	 */
	u32 best_mask[MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS] = { 0 };
	u32 best_cost[MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS] = { 0 };
	/* Array cores_in_domain is used to store the total pp cores in each pm domain. */
	u32 cores_in_domain[MALI_MAX_NUMBER_OF_DOMAINS] = { 0 };
	/* Domain_count is used to represent the max domain we have.*/
	u32 max_domain_mask = 0;
	u32 max_domain_id = 0;
	u32 always_on_pp_cores = 0;

	u32 num_pp, cost, mask;
	u32 i, j , k;

	/* Initialize statistics */
	for (i = 0; i < MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS; i++) {
		best_mask[i] = 0;
		best_cost[i] = 0xFFFFFFFF; /* lower cost is better */
	}

	for (i = 0; i < MALI_MAX_NUMBER_OF_PHYSICAL_PP_GROUPS + 1; i++) {
		for (j = 0; j < MALI_MAX_NUMBER_OF_DOMAINS; j++) {
			mali_pm_domain_power_cost_result[i][j] = 0;
		}
	}

	/* Caculate number of pp cores of a given domain config. */
	for (i = MALI_DOMAIN_INDEX_PP0; i <= MALI_DOMAIN_INDEX_PP7; i++) {
		if (0 < domain_config[i]) {
			/* Get the max domain mask value used to caculate power cost
			 * and we don't count in always on pp cores. */
			if (MALI_PM_DOMAIN_DUMMY_MASK != domain_config[i]
			    && max_domain_mask < domain_config[i]) {
				max_domain_mask = domain_config[i];
			}

			if (MALI_PM_DOMAIN_DUMMY_MASK == domain_config[i]) {
				always_on_pp_cores++;
			}
		}
	}
	max_domain_id = _mali_osk_fls(max_domain_mask);

	/*
	 * Try all combinations of power domains and check how many PP cores
	 * they have and their power cost.
	 */
	for (mask = 0; mask < (1 << max_domain_id); mask++) {
		num_pp = 0;
		cost = 0;

		mali_pm_stat_from_mask(mask, &num_pp, &cost);

		/* This mask is usable for all MP1 up to num_pp PP cores, check statistics for all */
		for (i = 0; i < num_pp; i++) {
			if (best_cost[i] >= cost) {
				best_cost[i] = cost;
				best_mask[i] = mask;
			}
		}
	}

	/*
	 * If we want to enable x pp cores, if x is less than number of always_on pp cores,
	 * all of pp cores we will enable must be always_on pp cores.
	 */
	for (i = 0; i < mali_executor_get_num_cores_total(); i++) {
		if (i < always_on_pp_cores) {
			mali_pm_domain_power_cost_result[i + 1][MALI_MAX_NUMBER_OF_DOMAINS - 1]
				= i + 1;
		} else {
			mali_pm_domain_power_cost_result[i + 1][MALI_MAX_NUMBER_OF_DOMAINS - 1]
				= always_on_pp_cores;
		}
	}

	/* In this loop, variable i represent for the number of non-always on pp cores we want to enabled. */
	for (i = 0; i < (mali_executor_get_num_cores_total() - always_on_pp_cores); i++) {
		if (best_mask[i] == 0) {
			/* This MP variant is not available */
			continue;
		}

		for (j = 0; j < MALI_MAX_NUMBER_OF_DOMAINS; j++) {
			cores_in_domain[j] = 0;
		}

		for (j = MALI_DOMAIN_INDEX_PP0; j <= MALI_DOMAIN_INDEX_PP7; j++) {
			if (0 < domain_config[j]
			    && (MALI_PM_DOMAIN_DUMMY_MASK != domain_config[i])) {
				cores_in_domain[_mali_osk_fls(domain_config[j]) - 1]++;
			}
		}

		/* In this loop, j represent for the number we have already enabled.*/
		for (j = 0; j <= i;) {
			/* j used to visit all of domain to get the number of pp cores remained in it. */
			for (k = 0; k < max_domain_id; k++) {
				/* If domain k in best_mask[i] is enabled and this domain has extra pp cores,
				 * we know we must pick at least one pp core from this domain.
				 * And then we move to next enabled pm domain. */
				if ((best_mask[i] & (0x1 << k)) && (0 < cores_in_domain[k])) {
					cores_in_domain[k]--;
					mali_pm_domain_power_cost_result[always_on_pp_cores + i + 1][k]++;
					j++;
					if (j > i) {
						break;
					}
				}
			}
		}
	}
}

/*
 * When we are doing core scaling,
 * this function is called to return the best mask to
 * achieve the best pp group power cost.
 */
void mali_pm_get_best_power_cost_mask(int num_requested, int *dst)
{
	MALI_DEBUG_ASSERT((mali_executor_get_num_cores_total() >= num_requested) && (0 <= num_requested));

	_mali_osk_memcpy(dst, mali_pm_domain_power_cost_result[num_requested], MALI_MAX_NUMBER_OF_DOMAINS * sizeof(int));
}

u32 mali_pm_get_current_mask(void)
{
	return pd_mask_current;
}

u32 mali_pm_get_wanted_mask(void)
{
	return pd_mask_wanted;
}
