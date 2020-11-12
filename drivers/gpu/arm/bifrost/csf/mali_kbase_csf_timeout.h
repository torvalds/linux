/*
 *
 * (C) COPYRIGHT 2019 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CSF_TIMEOUT_H_
#define _KBASE_CSF_TIMEOUT_H_

struct kbase_device;

/**
 * kbase_csf_timeout_init - Initialize the progress timeout.
 *
 * @kbdev: Instance of a GPU platform device that implements a command
 *         stream front-end interface. Must be zero-initialized.
 *
 * The progress timeout is the number of GPU clock cycles allowed to elapse
 * before the driver terminates a GPU command queue group in which a task is
 * making no forward progress on an endpoint (e.g. a shader core). This function
 * determines the initial value and also creates a sysfs file to allow the
 * timeout to be reconfigured later.
 *
 * Reconfigures the global firmware interface to enable the current timeout.
 *
 * Return: 0 on success, or negative on failure.
 */
int kbase_csf_timeout_init(struct kbase_device *kbdev);

/**
 * kbase_csf_timeout_term - Terminate the progress timeout.
 *
 * @kbdev: Instance of a GPU platform device that implements a command
 *         stream front-end interface.
 *
 * Removes the sysfs file which allowed the timeout to be reconfigured.
 * Does nothing if called on a zero-initialized object.
 */
void kbase_csf_timeout_term(struct kbase_device *kbdev);

/**
 * kbase_csf_timeout_get - get the current global progress timeout.
 *
 * @kbdev: Instance of a GPU platform device that implements a command
 *         stream front-end interface.
 *
 * Return: the maximum number of GPU cycles that is allowed to elapse without
 *         forward progress before the driver terminates a GPU command queue
 *         group.
 */
u64 kbase_csf_timeout_get(struct kbase_device *const kbdev);

#endif /* _KBASE_CSF_TIMEOUT_H_ */
