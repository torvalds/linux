/*
 *
 * (C) COPYRIGHT 2013-2015 ARM Limited. All rights reserved.
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
 * @file mali_kbase_pm_ca_fixed.c
 * A power policy implementing fixed core availability
 */

#include <mali_kbase.h>
#include <mali_kbase_pm.h>

static void fixed_init(struct kbase_device *kbdev)
{
	kbdev->pm.backend.ca_in_transition = false;
}

static void fixed_term(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

static u64 fixed_get_core_mask(struct kbase_device *kbdev)
{
	return kbdev->shader_present_bitmap;
}

static void fixed_update_core_status(struct kbase_device *kbdev,
					u64 cores_ready,
					u64 cores_transitioning)
{
	CSTD_UNUSED(kbdev);
	CSTD_UNUSED(cores_ready);
	CSTD_UNUSED(cores_transitioning);
}

/** The @ref struct kbase_pm_policy structure for the fixed power policy.
 *
 * This is the static structure that defines the fixed power policy's callback
 * and name.
 */
const struct kbase_pm_ca_policy kbase_pm_ca_fixed_policy_ops = {
	"fixed",			/* name */
	fixed_init,			/* init */
	fixed_term,			/* term */
	fixed_get_core_mask,		/* get_core_mask */
	fixed_update_core_status,	/* update_core_status */
	0u,				/* flags */
	KBASE_PM_CA_POLICY_ID_FIXED,	/* id */
};

KBASE_EXPORT_TEST_API(kbase_pm_ca_fixed_policy_ops);
