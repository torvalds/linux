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
 * Virtual interface for hardware counter watchdog.
 */

#ifndef _KBASE_HWCNT_WATCHDOG_IF_H_
#define _KBASE_HWCNT_WATCHDOG_IF_H_

#include <linux/types.h>

/*
 * Opaque structure of information used to create a watchdog timer interface.
 */
struct kbase_hwcnt_watchdog_info;

/**
 * typedef kbase_hwcnt_watchdog_callback_fn - Callback function when watchdog timer is done
 *
 * @user_data: Pointer to the callback user data.
 */
typedef void kbase_hwcnt_watchdog_callback_fn(void *user_data);

/**
 * typedef kbase_hwcnt_watchdog_enable_fn - Enable watchdog timer
 *
 * @timer:     Non-NULL pointer to a watchdog timer interface context
 * @period_ms: Period in milliseconds of the watchdog timer
 * @callback:  Non-NULL pointer to a watchdog callback function
 * @user_data: Pointer to the user data, used when watchdog timer callback is called
 *
 * Return: 0 if the watchdog timer enabled successfully, error code otherwise.
 */
typedef int kbase_hwcnt_watchdog_enable_fn(const struct kbase_hwcnt_watchdog_info *timer,
					   u32 period_ms,
					   kbase_hwcnt_watchdog_callback_fn *callback,
					   void *user_data);

/**
 * typedef kbase_hwcnt_watchdog_disable_fn - Disable watchdog timer
 *
 * @timer: Non-NULL pointer to a watchdog timer interface context
 */
typedef void kbase_hwcnt_watchdog_disable_fn(const struct kbase_hwcnt_watchdog_info *timer);

/**
 * typedef kbase_hwcnt_watchdog_modify_fn - Modify watchdog timer's timeout
 *
 * @timer:    Non-NULL pointer to a watchdog timer interface context
 * @delay_ms: Watchdog timer expiration in milliseconds
 */
typedef void kbase_hwcnt_watchdog_modify_fn(const struct kbase_hwcnt_watchdog_info *timer,
					    u32 delay_ms);

/**
 * struct kbase_hwcnt_watchdog_interface - Hardware counter watchdog virtual interface.
 *
 * @timer:   Immutable watchdog timer info
 * @enable:  Function ptr to enable watchdog
 * @disable: Function ptr to disable watchdog
 * @modify:  Function ptr to modify watchdog
 */
struct kbase_hwcnt_watchdog_interface {
	const struct kbase_hwcnt_watchdog_info *timer;
	kbase_hwcnt_watchdog_enable_fn *enable;
	kbase_hwcnt_watchdog_disable_fn *disable;
	kbase_hwcnt_watchdog_modify_fn *modify;
};

#endif /* _KBASE_HWCNT_WATCHDOG_IF_H_ */
