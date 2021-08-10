// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2015-2021 ARM Limited. All rights reserved.
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
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/regulator/consumer.h>

#include "mali_kbase_config_platform.h"

static void enable_gpu_power_control(struct kbase_device *kbdev)
{
	unsigned int i;

#if defined(CONFIG_REGULATOR)
	for (i = 0; i < kbdev->nr_regulators; i++) {
		if (WARN_ON(kbdev->regulators[i] == NULL))
			;
		else if (!regulator_is_enabled(kbdev->regulators[i]))
			WARN_ON(regulator_enable(kbdev->regulators[i]));
	}
#endif

	for (i = 0; i < kbdev->nr_clocks; i++) {
		if (WARN_ON(kbdev->clocks[i] == NULL))
			;
		else if (!__clk_is_enabled(kbdev->clocks[i]))
			WARN_ON(clk_prepare_enable(kbdev->clocks[i]));
	}
}

static void disable_gpu_power_control(struct kbase_device *kbdev)
{
	unsigned int i;

	for (i = 0; i < kbdev->nr_clocks; i++) {
		if (WARN_ON(kbdev->clocks[i] == NULL))
			;
		else if (__clk_is_enabled(kbdev->clocks[i])) {
			clk_disable_unprepare(kbdev->clocks[i]);
			WARN_ON(__clk_is_enabled(kbdev->clocks[i]));
		}

	}

#if defined(CONFIG_REGULATOR)
	for (i = 0; i < kbdev->nr_regulators; i++) {
		if (WARN_ON(kbdev->regulators[i] == NULL))
			;
		else if (regulator_is_enabled(kbdev->regulators[i]))
			WARN_ON(regulator_disable(kbdev->regulators[i]));
	}
#endif
}

static int pm_callback_power_on(struct kbase_device *kbdev)
{
	int ret = 1; /* Assume GPU has been powered off */
	int error;

	dev_dbg(kbdev->dev, "pm_callback_power_on %p\n",
			(void *)kbdev->dev->pm_domain);

	enable_gpu_power_control(kbdev);

	error = pm_runtime_get_sync(kbdev->dev);
	if (error == 1) {
		/*
		 * Let core know that the chip has not been
		 * powered off, so we can save on re-initialization.
		 */
		ret = 0;
	}

	dev_dbg(kbdev->dev, "pm_runtime_get_sync returned %d\n", error);

	return ret;
}

static void pm_callback_power_off(struct kbase_device *kbdev)
{
	dev_dbg(kbdev->dev, "pm_callback_power_off\n");

	pm_runtime_mark_last_busy(kbdev->dev);
	pm_runtime_put_autosuspend(kbdev->dev);

#ifndef KBASE_PM_RUNTIME
	disable_gpu_power_control(kbdev);
#endif
}

#ifdef KBASE_PM_RUNTIME
static int kbase_device_runtime_init(struct kbase_device *kbdev)
{
	int ret = 0;

	dev_dbg(kbdev->dev, "kbase_device_runtime_init\n");

	pm_runtime_set_autosuspend_delay(kbdev->dev, AUTO_SUSPEND_DELAY);
	pm_runtime_use_autosuspend(kbdev->dev);

	pm_runtime_set_active(kbdev->dev);
	pm_runtime_enable(kbdev->dev);

	if (!pm_runtime_enabled(kbdev->dev)) {
		dev_warn(kbdev->dev, "pm_runtime not enabled");
		ret = -ENOSYS;
	}

	return ret;
}

static void kbase_device_runtime_disable(struct kbase_device *kbdev)
{
	dev_dbg(kbdev->dev, "kbase_device_runtime_disable\n");
	pm_runtime_disable(kbdev->dev);
}
#endif

static int pm_callback_runtime_on(struct kbase_device *kbdev)
{
	dev_dbg(kbdev->dev, "pm_callback_runtime_on\n");

	enable_gpu_power_control(kbdev);
	return 0;
}

static void pm_callback_runtime_off(struct kbase_device *kbdev)
{
	dev_dbg(kbdev->dev, "pm_callback_runtime_off\n");

	disable_gpu_power_control(kbdev);
}

static void pm_callback_resume(struct kbase_device *kbdev)
{
	int ret = pm_callback_runtime_on(kbdev);

	WARN_ON(ret);
}

static void pm_callback_suspend(struct kbase_device *kbdev)
{
	pm_callback_runtime_off(kbdev);
}

struct kbase_pm_callback_conf pm_callbacks = {
	.power_on_callback = pm_callback_power_on,
	.power_off_callback = pm_callback_power_off,
	.power_suspend_callback = pm_callback_suspend,
	.power_resume_callback = pm_callback_resume,
#ifdef KBASE_PM_RUNTIME
	.power_runtime_init_callback = kbase_device_runtime_init,
	.power_runtime_term_callback = kbase_device_runtime_disable,
	.power_runtime_on_callback = pm_callback_runtime_on,
	.power_runtime_off_callback = pm_callback_runtime_off,
#else				/* KBASE_PM_RUNTIME */
	.power_runtime_init_callback = NULL,
	.power_runtime_term_callback = NULL,
	.power_runtime_on_callback = NULL,
	.power_runtime_off_callback = NULL,
#endif				/* KBASE_PM_RUNTIME */
};


