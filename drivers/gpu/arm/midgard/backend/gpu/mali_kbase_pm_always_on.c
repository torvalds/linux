/*
 *
 * (C) COPYRIGHT 2010-2015 ARM Limited. All rights reserved.
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





/*
 * "Always on" power management policy
 */

#include <mali_kbase.h>
#include <mali_kbase_pm.h>

static u64 always_on_get_core_mask(struct kbase_device *kbdev)
{
	return kbdev->shader_present_bitmap;
}

static bool always_on_get_core_active(struct kbase_device *kbdev)
{
	return true;
}

static void always_on_init(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

static void always_on_term(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

/*
 * The struct kbase_pm_policy structure for the demand power policy.
 *
 * This is the static structure that defines the demand power policy's callback
 * and name.
 */
const struct kbase_pm_policy kbase_pm_always_on_policy_ops = {
	"always_on",			/* name */
	always_on_init,			/* init */
	always_on_term,			/* term */
	always_on_get_core_mask,	/* get_core_mask */
	always_on_get_core_active,	/* get_core_active */
	0u,				/* flags */
	KBASE_PM_POLICY_ID_ALWAYS_ON,	/* id */
};

KBASE_EXPORT_TEST_API(kbase_pm_always_on_policy_ops);
