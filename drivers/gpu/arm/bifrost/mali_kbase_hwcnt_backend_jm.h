/*
 *
 * (C) COPYRIGHT 2018, 2020 ARM Limited. All rights reserved.
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
 * Concrete implementation of mali_kbase_hwcnt_backend interface for JM
 * backend.
 */

#ifndef _KBASE_HWCNT_BACKEND_JM_H_
#define _KBASE_HWCNT_BACKEND_JM_H_

#include "mali_kbase_hwcnt_backend.h"

struct kbase_device;

/**
 * kbase_hwcnt_backend_jm_create() - Create a JM hardware counter backend
 *                                    interface.
 * @kbdev: Non-NULL pointer to kbase device.
 * @iface: Non-NULL pointer to backend interface structure that is filled in
 *             on creation success.
 *
 * Calls to iface->dump_enable_nolock() require kbdev->hwaccess_lock held.
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_backend_jm_create(
	struct kbase_device *kbdev,
	struct kbase_hwcnt_backend_interface *iface);

/**
 * kbase_hwcnt_backend_jm_destroy() - Destroy a JM hardware counter backend
 *                                     interface.
 * @iface: Pointer to interface to destroy.
 *
 * Can be safely called on an all-zeroed interface, or on an already destroyed
 * interface.
 */
void kbase_hwcnt_backend_jm_destroy(
	struct kbase_hwcnt_backend_interface *iface);

#endif /* _KBASE_HWCNT_BACKEND_JM_H_ */
