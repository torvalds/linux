/*
 *
 * (C) COPYRIGHT 2011-2015 ARM Limited. All rights reserved.
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
 * Base kernel core availability APIs
 */

#ifndef _KBASE_PM_CA_H_
#define _KBASE_PM_CA_H_

/**
 * kbase_pm_ca_init - Initialize core availability framework
 *
 * Must be called before calling any other core availability function
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Return: 0 if the core availability framework was successfully initialized,
 *         -errno otherwise
 */
int kbase_pm_ca_init(struct kbase_device *kbdev);

/**
 * kbase_pm_ca_term - Terminate core availability framework
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_ca_term(struct kbase_device *kbdev);

/**
 * kbase_pm_ca_get_core_mask - Get currently available shaders core mask
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Returns a mask of the currently available shader cores.
 * Calls into the core availability policy
 *
 * Return: The bit mask of available cores
 */
u64 kbase_pm_ca_get_core_mask(struct kbase_device *kbdev);

/**
 * kbase_pm_ca_update_core_status - Update core status
 *
 * @kbdev:               The kbase device structure for the device (must be
 *                       a valid pointer)
 * @cores_ready:         The bit mask of cores ready for job submission
 * @cores_transitioning: The bit mask of cores that are transitioning power
 *                       state
 *
 * Update core availability policy with current core power status
 *
 * Calls into the core availability policy
 */
void kbase_pm_ca_update_core_status(struct kbase_device *kbdev, u64 cores_ready,
						u64 cores_transitioning);

/**
 * kbase_pm_ca_instr_enable - Enable override for instrumentation
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This overrides the output of the core availability policy, ensuring that all
 * cores are available
 */
void kbase_pm_ca_instr_enable(struct kbase_device *kbdev);

/**
 * kbase_pm_ca_instr_disable - Disable override for instrumentation
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This disables any previously enabled override, and resumes normal policy
 * functionality
 */
void kbase_pm_ca_instr_disable(struct kbase_device *kbdev);

#endif /* _KBASE_PM_CA_H_ */
