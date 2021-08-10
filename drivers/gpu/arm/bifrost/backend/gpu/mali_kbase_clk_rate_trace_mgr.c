// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2020-2021 ARM Limited. All rights reserved.
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
 * Implementation of the GPU clock rate trace manager.
 */

#include <mali_kbase.h>
#include <mali_kbase_config_defaults.h>
#include <linux/clk.h>
#include <asm/div64.h>
#include "backend/gpu/mali_kbase_clk_rate_trace_mgr.h"

#ifdef CONFIG_TRACE_POWER_GPU_FREQUENCY
#include <trace/events/power_gpu_frequency.h>
#else
#include "mali_power_gpu_frequency_trace.h"
#endif

#ifndef CLK_RATE_TRACE_OPS
#define CLK_RATE_TRACE_OPS (NULL)
#endif

/**
 * get_clk_rate_trace_callbacks() - Returns pointer to clk trace ops.
 * @kbdev: Pointer to kbase device, used to check if arbitration is enabled
 *         when compiled with arbiter support.
 * Return: Pointer to clk trace ops if supported or NULL.
 */
static struct kbase_clk_rate_trace_op_conf *
get_clk_rate_trace_callbacks(struct kbase_device *kbdev __maybe_unused)
{
	/* base case */
	struct kbase_clk_rate_trace_op_conf *callbacks =
		(struct kbase_clk_rate_trace_op_conf *)CLK_RATE_TRACE_OPS;
#if defined(CONFIG_MALI_ARBITER_SUPPORT) && defined(CONFIG_OF)
	const void *arbiter_if_node;

	if (WARN_ON(!kbdev) || WARN_ON(!kbdev->dev))
		return callbacks;

	arbiter_if_node =
		of_get_property(kbdev->dev->of_node, "arbiter_if", NULL);
	/* Arbitration enabled, override the callback pointer.*/
	if (arbiter_if_node)
		callbacks = &arb_clk_rate_trace_ops;
	else
		dev_dbg(kbdev->dev,
			"Arbitration supported but disabled by platform. Leaving clk rate callbacks as default.\n");

#endif

	return callbacks;
}

static int gpu_clk_rate_change_notifier(struct notifier_block *nb,
			unsigned long event, void *data)
{
	struct kbase_gpu_clk_notifier_data *ndata = data;
	struct kbase_clk_data *clk_data =
		container_of(nb, struct kbase_clk_data, clk_rate_change_nb);
	struct kbase_clk_rate_trace_manager *clk_rtm = clk_data->clk_rtm;
	unsigned long flags;

	if (WARN_ON_ONCE(clk_data->gpu_clk_handle != ndata->gpu_clk_handle))
		return NOTIFY_BAD;

	spin_lock_irqsave(&clk_rtm->lock, flags);
	if (event == POST_RATE_CHANGE) {
		if (!clk_rtm->gpu_idle &&
		    (clk_data->clock_val != ndata->new_rate)) {
			kbase_clk_rate_trace_manager_notify_all(
				clk_rtm, clk_data->index, ndata->new_rate);
		}

		clk_data->clock_val = ndata->new_rate;
	}
	spin_unlock_irqrestore(&clk_rtm->lock, flags);

	return NOTIFY_DONE;
}

static int gpu_clk_data_init(struct kbase_device *kbdev,
		void *gpu_clk_handle, unsigned int index)
{
	struct kbase_clk_rate_trace_op_conf *callbacks;
	struct kbase_clk_data *clk_data;
	struct kbase_clk_rate_trace_manager *clk_rtm = &kbdev->pm.clk_rtm;
	int ret = 0;

	callbacks = get_clk_rate_trace_callbacks(kbdev);

	if (WARN_ON(!callbacks) ||
	    WARN_ON(!gpu_clk_handle) ||
	    WARN_ON(index >= BASE_MAX_NR_CLOCKS_REGULATORS))
		return -EINVAL;

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data) {
		dev_err(kbdev->dev, "Failed to allocate data for clock enumerated at index %u", index);
		return -ENOMEM;
	}

	clk_data->index = (u8)index;
	clk_data->gpu_clk_handle = gpu_clk_handle;
	/* Store the initial value of clock */
	clk_data->clock_val =
		callbacks->get_gpu_clk_rate(kbdev, gpu_clk_handle);

	{
		/* At the initialization time, GPU is powered off. */
		unsigned long flags;

		spin_lock_irqsave(&clk_rtm->lock, flags);
		kbase_clk_rate_trace_manager_notify_all(
			clk_rtm, clk_data->index, 0);
		spin_unlock_irqrestore(&clk_rtm->lock, flags);
	}

	clk_data->clk_rtm = clk_rtm;
	clk_rtm->clks[index] = clk_data;

	clk_data->clk_rate_change_nb.notifier_call =
			gpu_clk_rate_change_notifier;

	if (callbacks->gpu_clk_notifier_register)
		ret = callbacks->gpu_clk_notifier_register(kbdev,
				gpu_clk_handle, &clk_data->clk_rate_change_nb);
	if (ret) {
		dev_err(kbdev->dev, "Failed to register notifier for clock enumerated at index %u", index);
		kfree(clk_data);
	}

	return ret;
}

int kbase_clk_rate_trace_manager_init(struct kbase_device *kbdev)
{
	struct kbase_clk_rate_trace_op_conf *callbacks;
	struct kbase_clk_rate_trace_manager *clk_rtm = &kbdev->pm.clk_rtm;
	unsigned int i;
	int ret = 0;

	callbacks = get_clk_rate_trace_callbacks(kbdev);

	spin_lock_init(&clk_rtm->lock);
	INIT_LIST_HEAD(&clk_rtm->listeners);

	/* Return early if no callbacks provided for clock rate tracing */
	if (!callbacks) {
		WRITE_ONCE(clk_rtm->clk_rate_trace_ops, NULL);
		return 0;
	}

	clk_rtm->gpu_idle = true;

	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++) {
		void *gpu_clk_handle =
			callbacks->enumerate_gpu_clk(kbdev, i);

		if (!gpu_clk_handle)
			break;

		ret = gpu_clk_data_init(kbdev, gpu_clk_handle, i);
		if (ret)
			goto error;
	}

	/* Activate clock rate trace manager if at least one GPU clock was
	 * enumerated.
	 */
	if (i) {
		WRITE_ONCE(clk_rtm->clk_rate_trace_ops, callbacks);
	} else {
		dev_info(kbdev->dev, "No clock(s) available for rate tracing");
		WRITE_ONCE(clk_rtm->clk_rate_trace_ops, NULL);
	}

	return 0;

error:
	while (i--) {
		clk_rtm->clk_rate_trace_ops->gpu_clk_notifier_unregister(
				kbdev, clk_rtm->clks[i]->gpu_clk_handle,
				&clk_rtm->clks[i]->clk_rate_change_nb);
		kfree(clk_rtm->clks[i]);
	}

	return ret;
}

void kbase_clk_rate_trace_manager_term(struct kbase_device *kbdev)
{
	struct kbase_clk_rate_trace_manager *clk_rtm = &kbdev->pm.clk_rtm;
	unsigned int i;

	WARN_ON(!list_empty(&clk_rtm->listeners));

	if (!clk_rtm->clk_rate_trace_ops)
		return;

	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++) {
		if (!clk_rtm->clks[i])
			break;

		if (clk_rtm->clk_rate_trace_ops->gpu_clk_notifier_unregister)
			clk_rtm->clk_rate_trace_ops->gpu_clk_notifier_unregister
			(kbdev, clk_rtm->clks[i]->gpu_clk_handle,
			&clk_rtm->clks[i]->clk_rate_change_nb);
		kfree(clk_rtm->clks[i]);
	}

	WRITE_ONCE(clk_rtm->clk_rate_trace_ops, NULL);
}

void kbase_clk_rate_trace_manager_gpu_active(struct kbase_device *kbdev)
{
	struct kbase_clk_rate_trace_manager *clk_rtm = &kbdev->pm.clk_rtm;
	unsigned int i;
	unsigned long flags;

	if (!clk_rtm->clk_rate_trace_ops)
		return;

	spin_lock_irqsave(&clk_rtm->lock, flags);

	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++) {
		struct kbase_clk_data *clk_data = clk_rtm->clks[i];

		if (!clk_data)
			break;

		if (unlikely(!clk_data->clock_val))
			continue;

		kbase_clk_rate_trace_manager_notify_all(
			clk_rtm, clk_data->index, clk_data->clock_val);
	}

	clk_rtm->gpu_idle = false;
	spin_unlock_irqrestore(&clk_rtm->lock, flags);
}

void kbase_clk_rate_trace_manager_gpu_idle(struct kbase_device *kbdev)
{
	struct kbase_clk_rate_trace_manager *clk_rtm = &kbdev->pm.clk_rtm;
	unsigned int i;
	unsigned long flags;

	if (!clk_rtm->clk_rate_trace_ops)
		return;

	spin_lock_irqsave(&clk_rtm->lock, flags);

	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++) {
		struct kbase_clk_data *clk_data = clk_rtm->clks[i];

		if (!clk_data)
			break;

		if (unlikely(!clk_data->clock_val))
			continue;

		kbase_clk_rate_trace_manager_notify_all(
			clk_rtm, clk_data->index, 0);
	}

	clk_rtm->gpu_idle = true;
	spin_unlock_irqrestore(&clk_rtm->lock, flags);
}

void kbase_clk_rate_trace_manager_notify_all(
	struct kbase_clk_rate_trace_manager *clk_rtm,
	u32 clk_index,
	unsigned long new_rate)
{
	struct kbase_clk_rate_listener *pos;
	struct kbase_device *kbdev;

	lockdep_assert_held(&clk_rtm->lock);

	kbdev = container_of(clk_rtm, struct kbase_device, pm.clk_rtm);

	dev_dbg(kbdev->dev, "%s - GPU clock %u rate changed to %lu, pid: %d",
		__func__, clk_index, new_rate, current->pid);

	/* Raise standard `power/gpu_frequency` ftrace event */
	{
		unsigned long new_rate_khz = new_rate;

#if BITS_PER_LONG == 64
		do_div(new_rate_khz, 1000);
#elif BITS_PER_LONG == 32
		new_rate_khz /= 1000;
#else
#error "unsigned long division is not supported for this architecture"
#endif

		trace_gpu_frequency(new_rate_khz, clk_index);
	}

	/* Notify the listeners. */
	list_for_each_entry(pos, &clk_rtm->listeners, node) {
		pos->notify(pos, clk_index, new_rate);
	}
}
KBASE_EXPORT_TEST_API(kbase_clk_rate_trace_manager_notify_all);
