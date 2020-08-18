/*
 *
 * (C) COPYRIGHT 2014-2015, 2018-2019 ARM Limited. All rights reserved.
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


/**
 * @file mali_kbase_hwaccess_pm.h
 * HW access power manager common APIs
 */

#ifndef _KBASE_HWACCESS_PM_H_
#define _KBASE_HWACCESS_PM_H_

#include <gpu/mali_kbase_gpu_regmap.h>
#include <linux/atomic.h>

#include <mali_kbase_pm_defs.h>

/* Forward definition - see mali_kbase.h */
struct kbase_device;

/* Functions common to all HW access backends */

/**
 * Initialize the power management framework.
 *
 * Must be called before any other power management function
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Return: 0 if the power management framework was successfully initialized.
 */
int kbase_hwaccess_pm_init(struct kbase_device *kbdev);

/**
 * Terminate the power management framework.
 *
 * No power management functions may be called after this
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_hwaccess_pm_term(struct kbase_device *kbdev);

/**
 * kbase_hwaccess_pm_powerup - Power up the GPU.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @flags: Flags to pass on to kbase_pm_init_hw
 *
 * Power up GPU after all modules have been initialized and interrupt handlers
 * installed.
 *
 * Return: 0 if powerup was successful.
 */
int kbase_hwaccess_pm_powerup(struct kbase_device *kbdev,
		unsigned int flags);

/**
 * Halt the power management framework.
 *
 * Should ensure that no new interrupts are generated, but allow any currently
 * running interrupt handlers to complete successfully. The GPU is forced off by
 * the time this function returns, regardless of whether or not the active power
 * policy asks for the GPU to be powered off.
 *
 * @param kbdev The kbase device structure for the device (must be a valid
 *              pointer)
 */
void kbase_hwaccess_pm_halt(struct kbase_device *kbdev);

/**
 * Perform any backend-specific actions to suspend the GPU
 *
 * @param kbdev The kbase device structure for the device (must be a valid
 *              pointer)
 */
void kbase_hwaccess_pm_suspend(struct kbase_device *kbdev);

/**
 * Perform any backend-specific actions to resume the GPU from a suspend
 *
 * @param kbdev The kbase device structure for the device (must be a valid
 *              pointer)
 */
void kbase_hwaccess_pm_resume(struct kbase_device *kbdev);

/**
 * Perform any required actions for activating the GPU. Called when the first
 * context goes active.
 *
 * @param kbdev The kbase device structure for the device (must be a valid
 *              pointer)
 */
void kbase_hwaccess_pm_gpu_active(struct kbase_device *kbdev);

/**
 * Perform any required actions for idling the GPU. Called when the last
 * context goes idle.
 *
 * @param kbdev The kbase device structure for the device (must be a valid
 *              pointer)
 */
void kbase_hwaccess_pm_gpu_idle(struct kbase_device *kbdev);


/**
 * Set the debug core mask.
 *
 * This determines which cores the power manager is allowed to use.
 *
 * @param kbdev         The kbase device structure for the device (must be a
 *                      valid pointer)
 * @param new_core_mask_js0 The core mask to use for job slot 0
 * @param new_core_mask_js0 The core mask to use for job slot 1
 * @param new_core_mask_js0 The core mask to use for job slot 2
 */
void kbase_pm_set_debug_core_mask(struct kbase_device *kbdev,
		u64 new_core_mask_js0, u64 new_core_mask_js1,
		u64 new_core_mask_js2);


/**
 * Get the current policy.
 *
 * Returns the policy that is currently active.
 *
 * @param kbdev The kbase device structure for the device (must be a valid
 *              pointer)
 *
 * @return The current policy
 */
const struct kbase_pm_ca_policy
*kbase_pm_ca_get_policy(struct kbase_device *kbdev);

/**
 * Change the policy to the one specified.
 *
 * @param kbdev  The kbase device structure for the device (must be a valid
 *               pointer)
 * @param policy The policy to change to (valid pointer returned from
 *               @ref kbase_pm_ca_list_policies)
 */
void kbase_pm_ca_set_policy(struct kbase_device *kbdev,
				const struct kbase_pm_ca_policy *policy);

/**
 * Retrieve a static list of the available policies.
 *
 * @param[out] policies An array pointer to take the list of policies. This may
 *                      be NULL. The contents of this array must not be
 *                      modified.
 *
 * @return The number of policies
 */
int
kbase_pm_ca_list_policies(const struct kbase_pm_ca_policy * const **policies);


/**
 * Get the current policy.
 *
 * Returns the policy that is currently active.
 *
 * @param kbdev The kbase device structure for the device (must be a valid
 *              pointer)
 *
 * @return The current policy
 */
const struct kbase_pm_policy *kbase_pm_get_policy(struct kbase_device *kbdev);

/**
 * Change the policy to the one specified.
 *
 * @param kbdev  The kbase device structure for the device (must be a valid
 *               pointer)
 * @param policy The policy to change to (valid pointer returned from
 *               @ref kbase_pm_list_policies)
 */
void kbase_pm_set_policy(struct kbase_device *kbdev,
					const struct kbase_pm_policy *policy);

/**
 * kbase_pm_list_policies - Retrieve a static list of the available policies.
 *
 * @kbdev:   The kbase device structure for the device.
 * @list:    An array pointer to take the list of policies. This may be NULL.
 *           The contents of this array must not be modified.
 *
 * Return: The number of policies
 */
int kbase_pm_list_policies(struct kbase_device *kbdev,
	const struct kbase_pm_policy * const **list);

/**
 * kbase_protected_most_enable - Enable protected mode
 *
 * @kbdev: Address of the instance of a GPU platform device.
 *
 * Return: Zero on success or an error code
 */
int kbase_pm_protected_mode_enable(struct kbase_device *kbdev);

/**
 * kbase_protected_mode_disable - Disable protected mode
 *
 * @kbdev: Address of the instance of a GPU platform device.
 *
 * Return: Zero on success or an error code
 */
int kbase_pm_protected_mode_disable(struct kbase_device *kbdev);

#endif /* _KBASE_HWACCESS_PM_H_ */
