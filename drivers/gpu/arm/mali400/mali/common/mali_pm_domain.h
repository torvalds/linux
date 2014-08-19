/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2013-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#ifndef __MALI_PM_DOMAIN_H__
#define __MALI_PM_DOMAIN_H__

#include "mali_kernel_common.h"
#include "mali_osk.h"

#include "mali_l2_cache.h"
#include "mali_group.h"
#include "mali_pmu.h"

typedef enum {
	MALI_PM_DOMAIN_ON,
	MALI_PM_DOMAIN_OFF,
} mali_pm_domain_state;

struct mali_pm_domain {
	mali_pm_domain_state state;
	_mali_osk_spinlock_irq_t *lock;

	s32 use_count;

	u32 pmu_mask;

	int group_count;
	struct mali_group *group_list;

	struct mali_l2_cache_core *l2;
};

struct mali_pm_domain *mali_pm_domain_create(u32 pmu_mask);

void mali_pm_domain_add_group(u32 mask, struct mali_group *group);

void mali_pm_domain_add_l2(u32 mask, struct mali_l2_cache_core *l2);
void mali_pm_domain_delete(struct mali_pm_domain *domain);

void mali_pm_domain_terminate(void);

/** Get PM domain from domain ID
 */
struct mali_pm_domain *mali_pm_domain_get_from_mask(u32 mask);
struct mali_pm_domain *mali_pm_domain_get_from_index(u32 id);

/* Ref counting */
void mali_pm_domain_ref_get(struct mali_pm_domain *domain);
void mali_pm_domain_ref_put(struct mali_pm_domain *domain);

MALI_STATIC_INLINE struct mali_l2_cache_core *mali_pm_domain_l2_get(struct mali_pm_domain *domain)
{
	return domain->l2;
}

MALI_STATIC_INLINE mali_pm_domain_state mali_pm_domain_state_get(struct mali_pm_domain *domain)
{
	return domain->state;
}

mali_bool mali_pm_domain_lock_state(struct mali_pm_domain *domain);
void mali_pm_domain_unlock_state(struct mali_pm_domain *domain);

#define MALI_PM_DOMAIN_FOR_EACH_GROUP(group, domain) for ((group) = (domain)->group_list;\
		NULL != (group); (group) = (group)->pm_domain_list)

#endif /* __MALI_PM_DOMAIN_H__ */
