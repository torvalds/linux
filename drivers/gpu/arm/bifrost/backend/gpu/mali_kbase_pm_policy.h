/*
 *
 * (C) COPYRIGHT 2010-2015, 2018 ARM Limited. All rights reserved.
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
 * Power policy API definitions
 */

#ifndef _KBASE_PM_POLICY_H_
#define _KBASE_PM_POLICY_H_

/**
 * kbase_pm_policy_init - Initialize power policy framework
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Must be called before calling any other policy function
 *
 * Return: 0 if the power policy framework was successfully
 *         initialized, -errno otherwise.
 */
int kbase_pm_policy_init(struct kbase_device *kbdev);

/**
 * kbase_pm_policy_term - Terminate power policy framework
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_policy_term(struct kbase_device *kbdev);

/**
 * kbase_pm_update_active - Update the active power state of the GPU
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Calls into the current power policy
 */
void kbase_pm_update_active(struct kbase_device *kbdev);

/**
 * kbase_pm_update_cores - Update the desired core state of the GPU
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Calls into the current power policy
 */
void kbase_pm_update_cores(struct kbase_device *kbdev);

/**
 * kbase_pm_cores_requested - Check that a power request has been locked into
 *                            the HW.
 * @kbdev:           Kbase device
 * @shader_required: true if shaders are required
 *
 * Called by the scheduler to check if a power on request has been locked into
 * the HW.
 *
 * Note that there is no guarantee that the cores are actually ready, however
 * when the request has been locked into the HW, then it is safe to submit work
 * since the HW will wait for the transition to ready.
 *
 * A reference must first be taken prior to making this call.
 *
 * Caller must hold the hwaccess_lock.
 *
 * Return: true if the request to the HW was successfully made else false if the
 *         request is still pending.
 */
static inline bool kbase_pm_cores_requested(struct kbase_device *kbdev,
		bool shader_required)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	/* If the L2 & tiler are not on or pending, then the tiler is not yet
	 * available, and shaders are definitely not powered.
	 */
	if (kbdev->pm.backend.l2_state != KBASE_L2_PEND_ON &&
			kbdev->pm.backend.l2_state != KBASE_L2_ON &&
			kbdev->pm.backend.l2_state != KBASE_L2_ON_HWCNT_ENABLE)
		return false;

	if (shader_required &&
			kbdev->pm.backend.shaders_state != KBASE_SHADERS_PEND_ON_CORESTACK_ON &&
			kbdev->pm.backend.shaders_state != KBASE_SHADERS_ON_CORESTACK_ON &&
			kbdev->pm.backend.shaders_state != KBASE_SHADERS_ON_CORESTACK_ON_RECHECK)
		return false;

	return true;
}

#endif /* _KBASE_PM_POLICY_H_ */
