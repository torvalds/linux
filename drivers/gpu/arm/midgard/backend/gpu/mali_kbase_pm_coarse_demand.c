/*
 *
 * (C) COPYRIGHT 2012-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





/**
 * @file mali_kbase_pm_coarse_demand.c
 * "Coarse Demand" power management policy
 */

#include <mali_kbase.h>
#include <mali_kbase_pm.h>

static u64 coarse_demand_get_core_mask(struct kbase_device *kbdev)
{
	if (kbdev->pm.active_count == 0)
		return 0;

	return kbdev->shader_present_bitmap;
}

static bool coarse_demand_get_core_active(struct kbase_device *kbdev)
{
	if (kbdev->pm.active_count == 0)
		return false;

	return true;
}

static void coarse_demand_init(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

static void coarse_demand_term(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

/** The @ref struct kbase_pm_policy structure for the demand power policy.
 *
 * This is the static structure that defines the demand power policy's callback
 * and name.
 */
const struct kbase_pm_policy kbase_pm_coarse_demand_policy_ops = {
	"coarse_demand",			/* name */
	coarse_demand_init,			/* init */
	coarse_demand_term,			/* term */
	coarse_demand_get_core_mask,		/* get_core_mask */
	coarse_demand_get_core_active,		/* get_core_active */
	0u,					/* flags */
	KBASE_PM_POLICY_ID_COARSE_DEMAND,	/* id */
};

KBASE_EXPORT_TEST_API(kbase_pm_coarse_demand_policy_ops);
