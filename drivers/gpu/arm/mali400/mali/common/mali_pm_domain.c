/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2013-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_pm_domain.h"
#include "mali_pmu.h"
#include "mali_group.h"

static struct mali_pm_domain *mali_pm_domains[MALI_MAX_NUMBER_OF_DOMAINS] = { NULL, };

static void mali_pm_domain_lock(struct mali_pm_domain *domain)
{
	_mali_osk_spinlock_irq_lock(domain->lock);
}

static void mali_pm_domain_unlock(struct mali_pm_domain *domain)
{
	_mali_osk_spinlock_irq_unlock(domain->lock);
}

MALI_STATIC_INLINE void mali_pm_domain_state_set(struct mali_pm_domain *domain, mali_pm_domain_state state)
{
	domain->state = state;
}

struct mali_pm_domain *mali_pm_domain_create(u32 pmu_mask)
{
	struct mali_pm_domain *domain = NULL;
	u32 domain_id = 0;

	domain = mali_pm_domain_get_from_mask(pmu_mask);
	if (NULL != domain) return domain;

	MALI_DEBUG_PRINT(2, ("Mali PM domain: Creating Mali PM domain (mask=0x%08X)\n", pmu_mask));

	domain = (struct mali_pm_domain *)_mali_osk_malloc(sizeof(struct mali_pm_domain));
	if (NULL != domain) {
		domain->lock = _mali_osk_spinlock_irq_init(_MALI_OSK_LOCKFLAG_ORDERED, _MALI_OSK_LOCK_ORDER_PM_DOMAIN);
		if (NULL == domain->lock) {
			_mali_osk_free(domain);
			return NULL;
		}

		domain->state = MALI_PM_DOMAIN_ON;
		domain->pmu_mask = pmu_mask;
		domain->use_count = 0;
		domain->group_list = NULL;
		domain->group_count = 0;
		domain->l2 = NULL;

		domain_id = _mali_osk_fls(pmu_mask) - 1;
		/* Verify the domain_id */
		MALI_DEBUG_ASSERT(MALI_MAX_NUMBER_OF_DOMAINS > domain_id);
		/* Verify that pmu_mask only one bit is set */
		MALI_DEBUG_ASSERT((1 << domain_id) == pmu_mask);
		mali_pm_domains[domain_id] = domain;

		return domain;
	} else {
		MALI_DEBUG_PRINT_ERROR(("Unable to create PM domain\n"));
	}

	return NULL;
}

void mali_pm_domain_delete(struct mali_pm_domain *domain)
{
	if (NULL == domain) {
		return;
	}
	_mali_osk_spinlock_irq_term(domain->lock);

	_mali_osk_free(domain);
}

void mali_pm_domain_terminate(void)
{
	int i;

	/* Delete all domains */
	for (i = 0; i < MALI_MAX_NUMBER_OF_DOMAINS; i++) {
		mali_pm_domain_delete(mali_pm_domains[i]);
	}
}

void mali_pm_domain_add_group(u32 mask, struct mali_group *group)
{
	struct mali_pm_domain *domain = mali_pm_domain_get_from_mask(mask);
	struct mali_group *next;

	if (NULL == domain) return;

	MALI_DEBUG_ASSERT_POINTER(group);

	++domain->group_count;
	next = domain->group_list;

	domain->group_list = group;

	group->pm_domain_list = next;

	mali_group_set_pm_domain(group, domain);

	/* Get pm domain ref after mali_group_set_pm_domain */
	mali_group_get_pm_domain_ref(group);
}

void mali_pm_domain_add_l2(u32 mask, struct mali_l2_cache_core *l2)
{
	struct mali_pm_domain *domain = mali_pm_domain_get_from_mask(mask);

	if (NULL == domain) return;

	MALI_DEBUG_ASSERT(NULL == domain->l2);
	MALI_DEBUG_ASSERT(NULL != l2);

	domain->l2 = l2;

	mali_l2_cache_set_pm_domain(l2, domain);
}

struct mali_pm_domain *mali_pm_domain_get_from_mask(u32 mask)
{
	u32 id = 0;

	if (0 == mask) return NULL;

	id = _mali_osk_fls(mask) - 1;

	MALI_DEBUG_ASSERT(MALI_MAX_NUMBER_OF_DOMAINS > id);
	/* Verify that pmu_mask only one bit is set */
	MALI_DEBUG_ASSERT((1 << id) == mask);

	return mali_pm_domains[id];
}

struct mali_pm_domain *mali_pm_domain_get_from_index(u32 id)
{
	MALI_DEBUG_ASSERT(MALI_MAX_NUMBER_OF_DOMAINS > id);

	return mali_pm_domains[id];
}

void mali_pm_domain_ref_get(struct mali_pm_domain *domain)
{
	if (NULL == domain) return;

	mali_pm_domain_lock(domain);
	++domain->use_count;

	if (MALI_PM_DOMAIN_ON != domain->state) {
		/* Power on */
		struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();

		MALI_DEBUG_PRINT(3, ("PM Domain: Powering on 0x%08x\n", domain->pmu_mask));

		if (NULL != pmu) {
			_mali_osk_errcode_t err;

			err = mali_pmu_power_up(pmu, domain->pmu_mask);

			if (_MALI_OSK_ERR_OK != err && _MALI_OSK_ERR_BUSY != err) {
				MALI_PRINT_ERROR(("PM Domain: Failed to power up PM domain 0x%08x\n",
						  domain->pmu_mask));
			}
		}
		mali_pm_domain_state_set(domain, MALI_PM_DOMAIN_ON);
	} else {
		MALI_DEBUG_ASSERT(MALI_PM_DOMAIN_ON == mali_pm_domain_state_get(domain));
	}

	mali_pm_domain_unlock(domain);
}

void mali_pm_domain_ref_put(struct mali_pm_domain *domain)
{
	if (NULL == domain) return;

	mali_pm_domain_lock(domain);
	--domain->use_count;

	if (0 == domain->use_count && MALI_PM_DOMAIN_OFF != domain->state) {
		/* Power off */
		struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();

		MALI_DEBUG_PRINT(3, ("PM Domain: Powering off 0x%08x\n", domain->pmu_mask));

		mali_pm_domain_state_set(domain, MALI_PM_DOMAIN_OFF);

		if (NULL != pmu) {
			_mali_osk_errcode_t err;

			err = mali_pmu_power_down(pmu, domain->pmu_mask);

			if (_MALI_OSK_ERR_OK != err && _MALI_OSK_ERR_BUSY != err) {
				MALI_PRINT_ERROR(("PM Domain: Failed to power down PM domain 0x%08x\n",
						  domain->pmu_mask));
			}
		}
	}
	mali_pm_domain_unlock(domain);
}

mali_bool mali_pm_domain_lock_state(struct mali_pm_domain *domain)
{
	mali_bool is_powered = MALI_TRUE;

	/* Take a reference without powering on */
	if (NULL != domain) {
		mali_pm_domain_lock(domain);
		++domain->use_count;

		if (MALI_PM_DOMAIN_ON != domain->state) {
			is_powered = MALI_FALSE;
		}
		mali_pm_domain_unlock(domain);
	}

	if (!_mali_osk_pm_dev_ref_add_no_power_on()) {
		is_powered = MALI_FALSE;
	}

	return is_powered;
}

void mali_pm_domain_unlock_state(struct mali_pm_domain *domain)
{
	_mali_osk_pm_dev_ref_dec_no_power_on();

	if (NULL != domain) {
		mali_pm_domain_ref_put(domain);
	}
}
