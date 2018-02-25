/*
 *
 * (C) COPYRIGHT 2010-2016 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */



/*
 * A simple demand based power management policy
 */

#include <mali_kbase.h>
#include <mali_kbase_pm.h>

static u64 demand_get_core_mask(struct kbase_device *kbdev)
{
	u64 desired = kbdev->shader_needed_bitmap | kbdev->shader_inuse_bitmap;

	if (0 == kbdev->pm.active_count)
		return 0;

	return desired;
}

static bool demand_get_core_active(struct kbase_device *kbdev)
{
	if (0 == kbdev->pm.active_count && !(kbdev->shader_needed_bitmap |
			kbdev->shader_inuse_bitmap) && !kbdev->tiler_needed_cnt
			&& !kbdev->tiler_inuse_cnt)
		return false;

	return true;
}

static void demand_init(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

static void demand_term(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

/*
 * The struct kbase_pm_policy structure for the demand power policy.
 *
 * This is the static structure that defines the demand power policy's callback
 * and name.
 */
const struct kbase_pm_policy kbase_pm_demand_policy_ops = {
	"demand",			/* name */
	demand_init,			/* init */
	demand_term,			/* term */
	demand_get_core_mask,		/* get_core_mask */
	demand_get_core_active,		/* get_core_active */
	0u,				/* flags */
	KBASE_PM_POLICY_ID_DEMAND,	/* id */
};

KBASE_EXPORT_TEST_API(kbase_pm_demand_policy_ops);
