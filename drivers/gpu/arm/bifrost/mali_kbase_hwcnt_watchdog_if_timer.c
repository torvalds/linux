// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
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

#include "mali_kbase.h"
#include "mali_kbase_hwcnt_watchdog_if.h"
#include "mali_kbase_hwcnt_watchdog_if_timer.h"

#include <linux/timer.h>
#include <linux/slab.h>

/**
 * struct kbase_hwcnt_watchdog_if_timer_info - Timer information for watchdog
 *                                             interface.
 *
 * @watchdog_timer: Watchdog timer
 * @timer_enabled:  True if watchdog timer enabled, otherwise false
 * @callback:       Watchdog callback function
 * @user_data:      Pointer to user data passed as argument to the callback
 *                  function
 */
struct kbase_hwcnt_watchdog_if_timer_info {
	struct timer_list watchdog_timer;
	bool timer_enabled;
	kbase_hwcnt_watchdog_callback_fn *callback;
	void *user_data;
};

/**
 * kbasep_hwcnt_watchdog_callback() - Watchdog timer callback
 *
 * @timer: Timer structure
 *
 * Function to be called when watchdog timer expires. Will call the callback
 * function provided at enable().
 */
static void kbasep_hwcnt_watchdog_callback(struct timer_list *const timer)
{
	struct kbase_hwcnt_watchdog_if_timer_info *const info =
		container_of(timer, struct kbase_hwcnt_watchdog_if_timer_info,
			     watchdog_timer);
	if (info->callback)
		info->callback(info->user_data);
}

static int kbasep_hwcnt_watchdog_if_timer_enable(
	const struct kbase_hwcnt_watchdog_info *const timer,
	u32 const period_ms, kbase_hwcnt_watchdog_callback_fn *const callback,
	void *const user_data)
{
	struct kbase_hwcnt_watchdog_if_timer_info *const timer_info =
		(void *)timer;

	if (WARN_ON(!timer) || WARN_ON(!callback))
		return -EINVAL;

	timer_info->callback = callback;
	timer_info->user_data = user_data;

	mod_timer(&timer_info->watchdog_timer,
		  jiffies + msecs_to_jiffies(period_ms));
	timer_info->timer_enabled = true;

	return 0;
}

static void kbasep_hwcnt_watchdog_if_timer_disable(
	const struct kbase_hwcnt_watchdog_info *const timer)
{
	struct kbase_hwcnt_watchdog_if_timer_info *const timer_info =
		(void *)timer;

	if (WARN_ON(!timer))
		return;

	if (!timer_info->timer_enabled)
		return;

	del_timer_sync(&timer_info->watchdog_timer);
	timer_info->timer_enabled = false;
}

static void kbasep_hwcnt_watchdog_if_timer_modify(
	const struct kbase_hwcnt_watchdog_info *const timer, u32 const delay_ms)
{
	struct kbase_hwcnt_watchdog_if_timer_info *const timer_info =
		(void *)timer;

	if (WARN_ON(!timer))
		return;

	mod_timer(&timer_info->watchdog_timer,
		  jiffies + msecs_to_jiffies(delay_ms));
}

void kbase_hwcnt_watchdog_if_timer_destroy(
	struct kbase_hwcnt_watchdog_interface *const watchdog_if)
{
	struct kbase_hwcnt_watchdog_if_timer_info *timer_info;

	if (WARN_ON(!watchdog_if))
		return;

	timer_info = (void *)watchdog_if->timer;

	if (WARN_ON(!timer_info))
		return;

	del_timer_sync(&timer_info->watchdog_timer);
	kfree(timer_info);

	memset(watchdog_if, 0, sizeof(*watchdog_if));
}

int kbase_hwcnt_watchdog_if_timer_create(
	struct kbase_hwcnt_watchdog_interface *const watchdog_if)
{
	struct kbase_hwcnt_watchdog_if_timer_info *timer_info;

	if (WARN_ON(!watchdog_if))
		return -EINVAL;

	timer_info = kmalloc(sizeof(*timer_info), GFP_KERNEL);
	if (!timer_info)
		return -ENOMEM;

	*timer_info =
		(struct kbase_hwcnt_watchdog_if_timer_info){ .timer_enabled =
								     false };

	kbase_timer_setup(&timer_info->watchdog_timer,
			  kbasep_hwcnt_watchdog_callback);

	*watchdog_if = (struct kbase_hwcnt_watchdog_interface){
		.timer = (void *)timer_info,
		.enable = kbasep_hwcnt_watchdog_if_timer_enable,
		.disable = kbasep_hwcnt_watchdog_if_timer_disable,
		.modify = kbasep_hwcnt_watchdog_if_timer_modify,
	};

	return 0;
}
