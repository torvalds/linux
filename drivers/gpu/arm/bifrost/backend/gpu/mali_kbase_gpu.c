/*
 *
 * (C) COPYRIGHT 2014-2017 ARM Limited. All rights reserved.
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


/*
 * Register-based HW access backend APIs
 */
#include <mali_kbase.h>
#include <mali_kbase_hwaccess_backend.h>
#include <backend/gpu/mali_kbase_irq_internal.h>
#include <backend/gpu/mali_kbase_jm_internal.h>
#include <backend/gpu/mali_kbase_js_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

int kbase_backend_early_init(struct kbase_device *kbdev)
{
	int err;

	err = kbasep_platform_device_init(kbdev);
	if (err)
		return err;

	err = kbase_pm_runtime_init(kbdev);
	if (err)
		goto fail_runtime_pm;

	/* Ensure we can access the GPU registers */
	kbase_pm_register_access_enable(kbdev);

	/* Find out GPU properties based on the GPU feature registers */
	kbase_gpuprops_set(kbdev);

	/* We're done accessing the GPU registers for now. */
	kbase_pm_register_access_disable(kbdev);

	err = kbase_install_interrupts(kbdev);
	if (err)
		goto fail_interrupts;

	err = kbase_hwaccess_pm_init(kbdev);
	if (err)
		goto fail_pm;

	return 0;

fail_pm:
	kbase_release_interrupts(kbdev);
fail_interrupts:
	kbase_pm_runtime_term(kbdev);
fail_runtime_pm:
	kbasep_platform_device_term(kbdev);

	return err;
}

void kbase_backend_early_term(struct kbase_device *kbdev)
{
	kbase_hwaccess_pm_term(kbdev);
	kbase_release_interrupts(kbdev);
	kbase_pm_runtime_term(kbdev);
	kbasep_platform_device_term(kbdev);
}

int kbase_backend_late_init(struct kbase_device *kbdev)
{
	int err;

	err = kbase_hwaccess_pm_powerup(kbdev, PM_HW_ISSUES_DETECT);
	if (err)
		return err;

	err = kbase_backend_timer_init(kbdev);
	if (err)
		goto fail_timer;

#ifdef CONFIG_MALI_BIFROST_DEBUG
#ifndef CONFIG_MALI_BIFROST_NO_MALI
	if (kbasep_common_test_interrupt_handlers(kbdev) != 0) {
		dev_err(kbdev->dev, "Interrupt assigment check failed.\n");
		err = -EINVAL;
		goto fail_interrupt_test;
	}
#endif /* !CONFIG_MALI_BIFROST_NO_MALI */
#endif /* CONFIG_MALI_BIFROST_DEBUG */

	err = kbase_job_slot_init(kbdev);
	if (err)
		goto fail_job_slot;

	init_waitqueue_head(&kbdev->hwaccess.backend.reset_wait);

	return 0;

fail_job_slot:

#ifdef CONFIG_MALI_BIFROST_DEBUG
#ifndef CONFIG_MALI_BIFROST_NO_MALI
fail_interrupt_test:
#endif /* !CONFIG_MALI_BIFROST_NO_MALI */
#endif /* CONFIG_MALI_BIFROST_DEBUG */

	kbase_backend_timer_term(kbdev);
fail_timer:
	kbase_hwaccess_pm_halt(kbdev);

	return err;
}

void kbase_backend_late_term(struct kbase_device *kbdev)
{
	kbase_job_slot_halt(kbdev);
	kbase_job_slot_term(kbdev);
	kbase_backend_timer_term(kbdev);
	kbase_hwaccess_pm_halt(kbdev);
}

