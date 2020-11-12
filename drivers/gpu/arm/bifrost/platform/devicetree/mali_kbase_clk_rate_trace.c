/*
 *
 * (C) COPYRIGHT 2015, 2017-2020 ARM Limited. All rights reserved.
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

#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <linux/clk.h>
#include "mali_kbase_config_platform.h"

static void *enumerate_gpu_clk(struct kbase_device *kbdev,
		unsigned int index)
{
	if (index >= kbdev->nr_clocks)
		return NULL;

	return kbdev->clocks[index];
}

static unsigned long get_gpu_clk_rate(struct kbase_device *kbdev,
		void *gpu_clk_handle)
{
	return clk_get_rate((struct clk *)gpu_clk_handle);
}

static int gpu_clk_notifier_register(struct kbase_device *kbdev,
		void *gpu_clk_handle, struct notifier_block *nb)
{
	compiletime_assert(offsetof(struct clk_notifier_data, clk) ==
		offsetof(struct kbase_gpu_clk_notifier_data, gpu_clk_handle),
		"mismatch in the offset of clk member");

	compiletime_assert(sizeof(((struct clk_notifier_data *)0)->clk) ==
	     sizeof(((struct kbase_gpu_clk_notifier_data *)0)->gpu_clk_handle),
	     "mismatch in the size of clk member");

	return clk_notifier_register((struct clk *)gpu_clk_handle, nb);
}

static void gpu_clk_notifier_unregister(struct kbase_device *kbdev,
		void *gpu_clk_handle, struct notifier_block *nb)
{
	clk_notifier_unregister((struct clk *)gpu_clk_handle, nb);
}

struct kbase_clk_rate_trace_op_conf clk_rate_trace_ops = {
	.get_gpu_clk_rate = get_gpu_clk_rate,
	.enumerate_gpu_clk = enumerate_gpu_clk,
	.gpu_clk_notifier_register = gpu_clk_notifier_register,
	.gpu_clk_notifier_unregister = gpu_clk_notifier_unregister,
};
