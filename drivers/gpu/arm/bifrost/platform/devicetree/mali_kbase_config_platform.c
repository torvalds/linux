// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
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

#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_config.h>
#include "mali_kbase_config_platform.h"
#include <device/mali_kbase_device.h>
#include <mali_kbase_hwaccess_time.h>
#include <gpu/mali_kbase_gpu_regmap.h>

#include <linux/kthread.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/gcd.h>
#include <asm/arch_timer.h>

struct kbase_platform_funcs_conf platform_funcs = {
	.platform_init_func = NULL,
	.platform_term_func = NULL,
	.platform_late_init_func = NULL,
	.platform_late_term_func = NULL,
};
