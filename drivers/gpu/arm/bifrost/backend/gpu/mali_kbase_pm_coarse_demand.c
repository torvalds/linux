// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2012-2016, 2018-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

/*
 * "Coarse Demand" power management policy
 */

#include <mali_kbase.h>
#include <mali_kbase_pm.h>

static bool coarse_demand_shaders_needed(struct kbase_device *kbdev)
{
	return kbase_pm_is_active(kbdev);
}

static bool coarse_demand_get_core_active(struct kbase_device *kbdev)
{
	return kbase_pm_is_active(kbdev);
}

static void coarse_demand_init(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

static void coarse_demand_term(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

/* The struct kbase_pm_policy structure for the demand power policy.
 *
 * This is the static structure that defines the demand power policy's callback
 * and name.
 */
const struct kbase_pm_policy kbase_pm_coarse_demand_policy_ops = {
	"coarse_demand",			/* name */
	coarse_demand_init,			/* init */
	coarse_demand_term,			/* term */
	coarse_demand_shaders_needed,		/* shaders_needed */
	coarse_demand_get_core_active,		/* get_core_active */
	NULL,					/* handle_event */
	KBASE_PM_POLICY_ID_COARSE_DEMAND,	/* id */
#if MALI_USE_CSF
	COARSE_ON_DEMAND_PM_SCHED_FLAGS,	/* pm_sched_flags */
#endif
};

KBASE_EXPORT_TEST_API(kbase_pm_coarse_demand_policy_ops);
