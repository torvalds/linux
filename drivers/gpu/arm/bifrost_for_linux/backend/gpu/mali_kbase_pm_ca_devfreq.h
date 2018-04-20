/*
 *
 * (C) COPYRIGHT 2017 ARM Limited. All rights reserved.
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
 * A core availability policy for use with devfreq, where core masks are
 * associated with OPPs.
 */

#ifndef MALI_KBASE_PM_CA_DEVFREQ_H
#define MALI_KBASE_PM_CA_DEVFREQ_H

/**
 * struct kbasep_pm_ca_policy_devfreq - Private structure for devfreq ca policy
 *
 * This contains data that is private to the devfreq core availability
 * policy.
 *
 * @cores_desired: Cores that the policy wants to be available
 * @cores_enabled: Cores that the policy is currently returning as available
 * @cores_used: Cores currently powered or transitioning
 */
struct kbasep_pm_ca_policy_devfreq {
	u64 cores_desired;
	u64 cores_enabled;
	u64 cores_used;
};

extern const struct kbase_pm_ca_policy kbase_pm_ca_devfreq_policy_ops;

/**
 * kbase_devfreq_set_core_mask - Set core mask for policy to use
 * @kbdev: Device pointer
 * @core_mask: New core mask
 *
 * The new core mask will have immediate effect if the GPU is powered, or will
 * take effect when it is next powered on.
 */
void kbase_devfreq_set_core_mask(struct kbase_device *kbdev, u64 core_mask);

#endif /* MALI_KBASE_PM_CA_DEVFREQ_H */

