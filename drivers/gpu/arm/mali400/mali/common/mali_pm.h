/*
 * Copyright (C) 2011-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_PM_H__
#define __MALI_PM_H__

#include "mali_osk.h"
#include "mali_pm_domain.h"

#define MALI_DOMAIN_INDEX_GP        0
#define MALI_DOMAIN_INDEX_PP0       1
#define MALI_DOMAIN_INDEX_PP1       2
#define MALI_DOMAIN_INDEX_PP2       3
#define MALI_DOMAIN_INDEX_PP3       4
#define MALI_DOMAIN_INDEX_PP4       5
#define MALI_DOMAIN_INDEX_PP5       6
#define MALI_DOMAIN_INDEX_PP6       7
#define MALI_DOMAIN_INDEX_PP7       8
#define MALI_DOMAIN_INDEX_L20       9
#define MALI_DOMAIN_INDEX_L21      10
#define MALI_DOMAIN_INDEX_L22      11
/*
 * The dummy domain is used when there is no physical power domain
 * (e.g. no PMU or always on cores)
 */
#define MALI_DOMAIN_INDEX_DUMMY    12
#define MALI_MAX_NUMBER_OF_DOMAINS 13

/**
 * Initialize the Mali PM module
 *
 * PM module covers Mali PM core, PM domains and Mali PMU
 */
_mali_osk_errcode_t mali_pm_initialize(void);

/**
 * Terminate the Mali PM module
 */
void mali_pm_terminate(void);

void mali_pm_exec_lock(void);
void mali_pm_exec_unlock(void);


struct mali_pm_domain *mali_pm_register_l2_cache(u32 domain_index,
		struct mali_l2_cache_core *l2_cache);
struct mali_pm_domain *mali_pm_register_group(u32 domain_index,
		struct mali_group *group);

mali_bool mali_pm_get_domain_refs(struct mali_pm_domain **domains,
				  struct mali_group **groups,
				  u32 num_domains);
mali_bool mali_pm_put_domain_refs(struct mali_pm_domain **domains,
				  u32 num_domains);

void mali_pm_init_begin(void);
void mali_pm_init_end(void);

void mali_pm_update_sync(void);
void mali_pm_update_async(void);

/* Callback functions for system power management */
void mali_pm_os_suspend(mali_bool os_suspend);
void mali_pm_os_resume(void);

mali_bool mali_pm_runtime_suspend(void);
void mali_pm_runtime_resume(void);

#if MALI_STATE_TRACKING
u32 mali_pm_dump_state_domain(struct mali_pm_domain *domain,
			      char *buf, u32 size);
#endif

void mali_pm_power_cost_setup(void);

void mali_pm_get_best_power_cost_mask(int num_requested, int *dst);

#if defined(DEBUG)
const char *mali_pm_mask_to_string(u32 mask);
#endif

u32 mali_pm_get_current_mask(void);
u32 mali_pm_get_wanted_mask(void);
#endif /* __MALI_PM_H__ */
