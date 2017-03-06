/*
 * Copyright (C) 2013-2014, 2016-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_PM_DOMAIN_H__
#define __MALI_PM_DOMAIN_H__

#include "mali_kernel_common.h"
#include "mali_osk.h"

#include "mali_l2_cache.h"
#include "mali_group.h"
#include "mali_pmu.h"

/* Instances are protected by PM state lock */
struct mali_pm_domain {
	mali_bool power_is_on;
	s32 use_count;
	u32 pmu_mask;

	/* Zero or more groups can belong to this domain */
	_mali_osk_list_t group_list;

	/* Zero or more L2 caches can belong to this domain */
	_mali_osk_list_t l2_cache_list;
};


void mali_pm_domain_initialize(void);
void mali_pm_domain_terminate(void);

struct mali_pm_domain *mali_pm_domain_create(u32 pmu_mask);
void mali_pm_domain_delete(struct mali_pm_domain *domain);

void mali_pm_domain_add_l2_cache(
	struct mali_pm_domain *domain,
	struct mali_l2_cache_core *l2_cache);
void mali_pm_domain_add_group(struct mali_pm_domain *domain,
			      struct mali_group *group);

struct mali_pm_domain *mali_pm_domain_get_from_mask(u32 mask);
struct mali_pm_domain *mali_pm_domain_get_from_index(u32 id);

/* Ref counting */
u32 mali_pm_domain_ref_get(struct mali_pm_domain *domain);
u32 mali_pm_domain_ref_put(struct mali_pm_domain *domain);

MALI_STATIC_INLINE _mali_osk_list_t *mali_pm_domain_get_group_list(
	struct mali_pm_domain *domain)
{
	MALI_DEBUG_ASSERT_POINTER(domain);
	return &domain->group_list;
}

MALI_STATIC_INLINE _mali_osk_list_t *mali_pm_domain_get_l2_cache_list(
	struct mali_pm_domain *domain)
{
	MALI_DEBUG_ASSERT_POINTER(domain);
	return &domain->l2_cache_list;
}

MALI_STATIC_INLINE mali_bool mali_pm_domain_power_is_on(
	struct mali_pm_domain *domain)
{
	MALI_DEBUG_ASSERT_POINTER(domain);
	return domain->power_is_on;
}

MALI_STATIC_INLINE void mali_pm_domain_set_power_on(
	struct mali_pm_domain *domain,
	mali_bool power_is_on)
{
	MALI_DEBUG_ASSERT_POINTER(domain);
	domain->power_is_on = power_is_on;
}

MALI_STATIC_INLINE u32 mali_pm_domain_get_use_count(
	struct mali_pm_domain *domain)
{
	MALI_DEBUG_ASSERT_POINTER(domain);
	return domain->use_count;
}

#if MALI_STATE_TRACKING
u32 mali_pm_domain_get_id(struct mali_pm_domain *domain);

MALI_STATIC_INLINE u32 mali_pm_domain_get_mask(struct mali_pm_domain *domain)
{
	MALI_DEBUG_ASSERT_POINTER(domain);
	return domain->pmu_mask;
}
#endif

#if defined(DEBUG)
mali_bool mali_pm_domain_all_unused(void);
#endif

#endif /* __MALI_PM_DOMAIN_H__ */
