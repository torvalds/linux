/*
 * Copyright (C) 2013-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_pm_domain.h"
#include "mali_pmu.h"
#include "mali_group.h"
#include "mali_pm.h"

static struct mali_pm_domain *mali_pm_domains[MALI_MAX_NUMBER_OF_DOMAINS] =
{ NULL, };

void mali_pm_domain_initialize(void)
{
	/* Domains will be initialized/created on demand */
}

void mali_pm_domain_terminate(void)
{
	int i;

	/* Delete all domains that has been created */
	for (i = 0; i < MALI_MAX_NUMBER_OF_DOMAINS; i++) {
		mali_pm_domain_delete(mali_pm_domains[i]);
		mali_pm_domains[i] = NULL;
	}
}

struct mali_pm_domain *mali_pm_domain_create(u32 pmu_mask)
{
	struct mali_pm_domain *domain = NULL;
	u32 domain_id = 0;

	domain = mali_pm_domain_get_from_mask(pmu_mask);
	if (NULL != domain) return domain;

	MALI_DEBUG_PRINT(2,
			 ("Mali PM domain: Creating Mali PM domain (mask=0x%08X)\n",
			  pmu_mask));

	domain = (struct mali_pm_domain *)_mali_osk_malloc(
			 sizeof(struct mali_pm_domain));
	if (NULL != domain) {
		domain->power_is_on = MALI_FALSE;
		domain->pmu_mask = pmu_mask;
		domain->use_count = 0;
		_mali_osk_list_init(&domain->group_list);
		_mali_osk_list_init(&domain->l2_cache_list);

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

	_mali_osk_list_delinit(&domain->group_list);
	_mali_osk_list_delinit(&domain->l2_cache_list);

	_mali_osk_free(domain);
}

void mali_pm_domain_add_group(struct mali_pm_domain *domain,
			      struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(domain);
	MALI_DEBUG_ASSERT_POINTER(group);

	/*
	 * Use addtail because virtual group is created last and it needs
	 * to be at the end of the list (in order to be activated after
	 * all children.
	 */
	_mali_osk_list_addtail(&group->pm_domain_list, &domain->group_list);
}

void mali_pm_domain_add_l2_cache(struct mali_pm_domain *domain,
				 struct mali_l2_cache_core *l2_cache)
{
	MALI_DEBUG_ASSERT_POINTER(domain);
	MALI_DEBUG_ASSERT_POINTER(l2_cache);
	_mali_osk_list_add(&l2_cache->pm_domain_list, &domain->l2_cache_list);
}

struct mali_pm_domain *mali_pm_domain_get_from_mask(u32 mask)
{
	u32 id = 0;

	if (0 == mask) {
		return NULL;
	}

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

u32 mali_pm_domain_ref_get(struct mali_pm_domain *domain)
{
	MALI_DEBUG_ASSERT_POINTER(domain);

	if (0 == domain->use_count) {
		_mali_osk_pm_dev_ref_get_async();
	}

	++domain->use_count;
	MALI_DEBUG_PRINT(4, ("PM domain %p: ref_get, use_count => %u\n", domain, domain->use_count));

	/* Return our mask so caller can check this against wanted mask */
	return domain->pmu_mask;
}

u32 mali_pm_domain_ref_put(struct mali_pm_domain *domain)
{
	MALI_DEBUG_ASSERT_POINTER(domain);

	--domain->use_count;
	MALI_DEBUG_PRINT(4, ("PM domain %p: ref_put, use_count => %u\n", domain, domain->use_count));

	if (0 == domain->use_count) {
		_mali_osk_pm_dev_ref_put();
	}

	/*
	 * Return the PMU mask which now could be be powered down
	 * (the bit for this domain).
	 * This is the responsibility of the caller (mali_pm)
	 */
	return (0 == domain->use_count ? domain->pmu_mask : 0);
}

#if MALI_STATE_TRACKING
u32 mali_pm_domain_get_id(struct mali_pm_domain *domain)
{
	u32 id = 0;

	MALI_DEBUG_ASSERT_POINTER(domain);
	MALI_DEBUG_ASSERT(0 != domain->pmu_mask);

	id = _mali_osk_fls(domain->pmu_mask) - 1;

	MALI_DEBUG_ASSERT(MALI_MAX_NUMBER_OF_DOMAINS > id);
	/* Verify that pmu_mask only one bit is set */
	MALI_DEBUG_ASSERT((1 << id) == domain->pmu_mask);
	/* Verify that we have stored the domain at right id/index */
	MALI_DEBUG_ASSERT(domain == mali_pm_domains[id]);

	return id;
}
#endif

#if defined(DEBUG)
mali_bool mali_pm_domain_all_unused(void)
{
	int i;

	for (i = 0; i < MALI_MAX_NUMBER_OF_DOMAINS; i++) {
		if (NULL == mali_pm_domains[i]) {
			/* Nothing to check */
			continue;
		}

		if (MALI_TRUE == mali_pm_domains[i]->power_is_on) {
			/* Not ready for suspend! */
			return MALI_FALSE;
		}

		if (0 != mali_pm_domains[i]->use_count) {
			/* Not ready for suspend! */
			return MALI_FALSE;
		}
	}

	return MALI_TRUE;
}
#endif
