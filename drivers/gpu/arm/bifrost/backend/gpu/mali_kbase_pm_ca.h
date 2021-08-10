/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2011-2018, 2020-2021 ARM Limited. All rights reserved.
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
 * kbase_pm_ca_get_instr_core_mask - Get the PM state sync-ed shaders core mask
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Returns a mask of the PM state synchronised shader cores for arranging
 * HW performance counter dumps
 *
 * Return: The bit mask of PM state synchronised cores
 */
u64 kbase_pm_ca_get_instr_core_mask(struct kbase_device *kbdev);

#endif /* _KBASE_PM_CA_H_ */
