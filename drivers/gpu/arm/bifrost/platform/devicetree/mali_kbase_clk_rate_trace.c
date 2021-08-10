// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2015, 2017-2021 ARM Limited. All rights reserved.
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
#include <linux/clk.h>
#include "mali_kbase_config_platform.h"

#if MALI_USE_CSF
#include <asm/arch_timer.h>
#endif

static void *enumerate_gpu_clk(struct kbase_device *kbdev,
		unsigned int index)
{
	if (index >= kbdev->nr_clocks)
		return NULL;

#if MALI_USE_CSF
	if (of_machine_is_compatible("arm,juno"))
		WARN_ON(kbdev->nr_clocks != 1);
#endif

	return kbdev->clocks[index];
}

static unsigned long get_gpu_clk_rate(struct kbase_device *kbdev,
		void *gpu_clk_handle)
{
#if MALI_USE_CSF
	/* On Juno fpga platforms, the GPU clock rate is reported as 600 MHZ at
	 * the boot time. Then after the first call to kbase_devfreq_target()
	 * the clock rate is reported as 450 MHZ and the frequency does not
	 * change after that. But the actual frequency at which GPU operates
	 * is always 50 MHz, which is equal to the frequency of system counter
	 * and HW counters also increment at the same rate.
	 * DVFS, which is a client of kbase_ipa_control, needs normalization of
	 * GPU_ACTIVE counter to calculate the time for which GPU has been busy.
	 * So for the correct normalization need to return the system counter
	 * frequency value.
	 * This is a reasonable workaround as the frequency value remains same
	 * throughout. It can be removed after GPUCORE-25693.
	 */
	if (of_machine_is_compatible("arm,juno"))
		return arch_timer_get_cntfrq();
#endif

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

#if MALI_USE_CSF
	/* Frequency is fixed on Juno platforms */
	if (of_machine_is_compatible("arm,juno"))
		return 0;
#endif

	return clk_notifier_register((struct clk *)gpu_clk_handle, nb);
}

static void gpu_clk_notifier_unregister(struct kbase_device *kbdev,
		void *gpu_clk_handle, struct notifier_block *nb)
{
#if MALI_USE_CSF
	if (of_machine_is_compatible("arm,juno"))
		return;
#endif

	clk_notifier_unregister((struct clk *)gpu_clk_handle, nb);
}

struct kbase_clk_rate_trace_op_conf clk_rate_trace_ops = {
	.get_gpu_clk_rate = get_gpu_clk_rate,
	.enumerate_gpu_clk = enumerate_gpu_clk,
	.gpu_clk_notifier_register = gpu_clk_notifier_register,
	.gpu_clk_notifier_unregister = gpu_clk_notifier_unregister,
};
