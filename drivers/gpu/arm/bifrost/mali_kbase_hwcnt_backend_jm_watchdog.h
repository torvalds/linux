/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2021 ARM Limited. All rights reserved.
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
 * Concrete implementation of mali_kbase_hwcnt_backend interface for job manager
 * backend. This module functionally interleaves between the hardware counter
 * (hwcnt_accumulator) module (the interface consumer) and the job manager
 * backend module (hwcnt_backend_jm). This module provides buffering
 * functionality for the dumping requests requested by the hwcnt_accumulator
 * consumer. This module is NOT multi-thread safe. The programmer must
 * ensure the exposed methods are called by at most one thread at any time.
 */

#ifndef _KBASE_HWCNT_BACKEND_JM_WATCHDOG_H_
#define _KBASE_HWCNT_BACKEND_JM_WATCHDOG_H_

#include <mali_kbase_hwcnt_backend.h>
#include <mali_kbase_hwcnt_watchdog_if.h>

/**
 * kbase_hwcnt_backend_jm_watchdog_create() - Create a job manager hardware counter watchdog
 *                                            backend interface.
 * @backend_iface:  Non-NULL pointer to the backend interface structure that this module will
 *                  extend.
 * @watchdog_iface: Non-NULL pointer to an hardware counter watchdog interface.
 * @out_iface:      Non-NULL pointer to backend interface structure that is filled in
 *                  on creation success.
 *
 * Calls to out_iface->dump_enable_nolock() require kbdev->hwaccess_lock held.
 *
 * Return: 0 on success, error otherwise.
 */
int kbase_hwcnt_backend_jm_watchdog_create(struct kbase_hwcnt_backend_interface *backend_iface,
					   struct kbase_hwcnt_watchdog_interface *watchdog_iface,
					   struct kbase_hwcnt_backend_interface *out_iface);

/**
 * kbase_hwcnt_backend_jm_watchdog_destroy() - Destroy a job manager hardware counter watchdog
 *                                             backend interface.
 * @iface: Pointer to interface to destroy.
 *
 * Can be safely called on an all-zeroed interface, or on an already destroyed
 * interface.
 */
void kbase_hwcnt_backend_jm_watchdog_destroy(struct kbase_hwcnt_backend_interface *iface);

#endif /* _KBASE_HWCNT_BACKEND_JM_WATCHDOG_H_ */
