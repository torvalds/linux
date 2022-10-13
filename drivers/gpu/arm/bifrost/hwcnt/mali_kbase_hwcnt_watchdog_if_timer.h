/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2021-2022 ARM Limited. All rights reserved.
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
 * Concrete implementation of kbase_hwcnt_watchdog_interface for HWC backend
 */

#ifndef _KBASE_HWCNT_WATCHDOG_IF_TIMER_H_
#define _KBASE_HWCNT_WATCHDOG_IF_TIMER_H_

struct kbase_hwcnt_watchdog_interface;

/**
 * kbase_hwcnt_watchdog_if_timer_create() - Create a watchdog interface of hardware counter backend.
 *
 * @watchdog_if: Non-NULL pointer to watchdog interface that is filled in on creation success
 *
 * Return: 0 on success, error otherwise.
 */
int kbase_hwcnt_watchdog_if_timer_create(struct kbase_hwcnt_watchdog_interface *watchdog_if);

/**
 * kbase_hwcnt_watchdog_if_timer_destroy() - Destroy a watchdog interface of hardware counter
 *                                           backend.
 *
 * @watchdog_if: Pointer to watchdog interface to destroy
 */
void kbase_hwcnt_watchdog_if_timer_destroy(struct kbase_hwcnt_watchdog_interface *watchdog_if);

#endif /* _KBASE_HWCNT_WATCHDOG_IF_TIMER_H_ */
