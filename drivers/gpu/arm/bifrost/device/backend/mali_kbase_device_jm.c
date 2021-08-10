// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
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

#include <device/mali_kbase_device_internal.h>
#include <device/mali_kbase_device.h>
#include <mali_kbase_hwaccess_instr.h>

#include <mali_kbase_config_defaults.h>
#include <mali_kbase_hwaccess_backend.h>
#include <mali_kbase_ctx_sched.h>
#include <mali_kbase_reset_gpu.h>


#ifdef CONFIG_MALI_ARBITER_SUPPORT
#include <arbiter/mali_kbase_arbiter_pm.h>
#endif

#include <mali_kbase.h>
#include <backend/gpu/mali_kbase_irq_internal.h>
#include <backend/gpu/mali_kbase_jm_internal.h>
#include <backend/gpu/mali_kbase_js_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <mali_kbase_dummy_job_wa.h>
#include <backend/gpu/mali_kbase_clk_rate_trace_mgr.h>

/**
 * kbase_backend_late_init - Perform any backend-specific initialization.
 * @kbdev:	Device pointer
 *
 * Return: 0 on success, or an error code on failure.
 */
static int kbase_backend_late_init(struct kbase_device *kbdev)
{
	int err;

	err = kbase_hwaccess_pm_init(kbdev);
	if (err)
		return err;

	err = kbase_reset_gpu_init(kbdev);
	if (err)
		goto fail_reset_gpu_init;

	err = kbase_hwaccess_pm_powerup(kbdev, PM_HW_ISSUES_DETECT);
	if (err)
		goto fail_pm_powerup;

	err = kbase_backend_timer_init(kbdev);
	if (err)
		goto fail_timer;

#ifdef CONFIG_MALI_BIFROST_DEBUG
#ifndef CONFIG_MALI_BIFROST_NO_MALI
	if (kbasep_common_test_interrupt_handlers(kbdev) != 0) {
		dev_err(kbdev->dev, "Interrupt assignment check failed.\n");
		err = -EINVAL;
		goto fail_interrupt_test;
	}
#endif /* !CONFIG_MALI_BIFROST_NO_MALI */
#endif /* CONFIG_MALI_BIFROST_DEBUG */

	err = kbase_job_slot_init(kbdev);
	if (err)
		goto fail_job_slot;

	/* Do the initialisation of devfreq.
	 * Devfreq needs backend_timer_init() for completion of its
	 * initialisation and it also needs to catch the first callback
	 * occurrence of the runtime_suspend event for maintaining state
	 * coherence with the backend power management, hence needs to be
	 * placed before the kbase_pm_context_idle().
	 */
	err = kbase_backend_devfreq_init(kbdev);
	if (err)
		goto fail_devfreq_init;

	/* Update gpuprops with L2_FEATURES if applicable */
	err = kbase_gpuprops_update_l2_features(kbdev);
	if (err)
		goto fail_update_l2_features;

	init_waitqueue_head(&kbdev->hwaccess.backend.reset_wait);

	/* Idle the GPU and/or cores, if the policy wants it to */
	kbase_pm_context_idle(kbdev);

	mutex_init(&kbdev->fw_load_lock);

	return 0;

fail_update_l2_features:
	kbase_backend_devfreq_term(kbdev);
fail_devfreq_init:
	kbase_job_slot_term(kbdev);
fail_job_slot:

#ifdef CONFIG_MALI_BIFROST_DEBUG
#ifndef CONFIG_MALI_BIFROST_NO_MALI
fail_interrupt_test:
#endif /* !CONFIG_MALI_BIFROST_NO_MALI */
#endif /* CONFIG_MALI_BIFROST_DEBUG */

	kbase_backend_timer_term(kbdev);
fail_timer:
	kbase_pm_context_idle(kbdev);
	kbase_hwaccess_pm_halt(kbdev);
fail_pm_powerup:
	kbase_reset_gpu_term(kbdev);
fail_reset_gpu_init:
	kbase_hwaccess_pm_term(kbdev);

	return err;
}

/**
 * kbase_backend_late_term - Perform any backend-specific termination.
 * @kbdev:	Device pointer
 */
static void kbase_backend_late_term(struct kbase_device *kbdev)
{
	kbase_backend_devfreq_term(kbdev);
	kbase_job_slot_halt(kbdev);
	kbase_job_slot_term(kbdev);
	kbase_backend_timer_term(kbdev);
	kbase_hwaccess_pm_halt(kbdev);
	kbase_reset_gpu_term(kbdev);
	kbase_hwaccess_pm_term(kbdev);
}

static int kbase_device_hwcnt_backend_jm_init(struct kbase_device *kbdev)
{
	return kbase_hwcnt_backend_jm_create(kbdev, &kbdev->hwcnt_gpu_iface);
}

static void kbase_device_hwcnt_backend_jm_term(struct kbase_device *kbdev)
{
	kbase_hwcnt_backend_jm_destroy(&kbdev->hwcnt_gpu_iface);
}

static const struct kbase_device_init dev_init[] = {
	{ assign_irqs, NULL, "IRQ search failed" },
	{ registers_map, registers_unmap, "Register map failed" },
	{ kbase_device_io_history_init, kbase_device_io_history_term,
	  "Register access history initialization failed" },
	{ kbase_device_pm_init, kbase_device_pm_term,
	  "Power management initialization failed" },
	{ kbase_device_early_init, kbase_device_early_term,
	  "Early device initialization failed" },
	{ kbase_device_populate_max_freq, NULL,
	  "Populating max frequency failed" },
	{ kbase_device_misc_init, kbase_device_misc_term,
	  "Miscellaneous device initialization failed" },
	{ kbase_device_pcm_dev_init, kbase_device_pcm_dev_term,
	  "Priority control manager initialization failed" },
	{ kbase_ctx_sched_init, kbase_ctx_sched_term,
	  "Context scheduler initialization failed" },
	{ kbase_mem_init, kbase_mem_term,
	  "Memory subsystem initialization failed" },
	{ kbase_device_coherency_init, NULL, "Device coherency init failed" },
	{ kbase_protected_mode_init, kbase_protected_mode_term,
	  "Protected mode subsystem initialization failed" },
	{ kbase_device_list_init, kbase_device_list_term,
	  "Device list setup failed" },
	{ kbasep_js_devdata_init, kbasep_js_devdata_term,
	  "Job JS devdata initialization failed" },
	{ kbase_device_timeline_init, kbase_device_timeline_term,
	  "Timeline stream initialization failed" },
	{ kbase_clk_rate_trace_manager_init, kbase_clk_rate_trace_manager_term,
	  "Clock rate trace manager initialization failed" },
	{ kbase_instr_backend_init, kbase_instr_backend_term,
	  "Instrumentation backend initialization failed" },
	{ kbase_device_hwcnt_backend_jm_init,
	  kbase_device_hwcnt_backend_jm_term,
	  "GPU hwcnt backend creation failed" },
	{ kbase_device_hwcnt_context_init, kbase_device_hwcnt_context_term,
	  "GPU hwcnt context initialization failed" },
	{ kbase_device_hwcnt_virtualizer_init,
	  kbase_device_hwcnt_virtualizer_term,
	  "GPU hwcnt virtualizer initialization failed" },
	{ kbase_device_vinstr_init, kbase_device_vinstr_term,
	  "Virtual instrumentation initialization failed" },
	{ kbase_backend_late_init, kbase_backend_late_term,
	  "Late backend initialization failed" },
#ifdef MALI_KBASE_BUILD
	{ kbase_debug_job_fault_dev_init, kbase_debug_job_fault_dev_term,
	  "Job fault debug initialization failed" },
	{ kbase_device_debugfs_init, kbase_device_debugfs_term,
	  "DebugFS initialization failed" },
	/* Sysfs init needs to happen before registering the device with
	 * misc_register(), otherwise it causes a race condition between
	 * registering the device and a uevent event being generated for
	 * userspace, causing udev rules to run which might expect certain
	 * sysfs attributes present. As a result of the race condition
	 * we avoid, some Mali sysfs entries may have appeared to udev
	 * to not exist.
	 * For more information, see
	 * https://www.kernel.org/doc/Documentation/driver-model/device.txt, the
	 * paragraph that starts with "Word of warning", currently the
	 * second-last paragraph.
	 */
	{ kbase_sysfs_init, kbase_sysfs_term, "SysFS group creation failed" },
	{ kbase_device_misc_register, kbase_device_misc_deregister,
	  "Misc device registration failed" },
	{ kbase_gpuprops_populate_user_buffer, kbase_gpuprops_free_user_buffer,
	  "GPU property population failed" },
#endif
	{ NULL, kbase_dummy_job_wa_cleanup, NULL },
	{ kbase_device_late_init, kbase_device_late_term,
	  "Late device initialization failed" },
};

static void kbase_device_term_partial(struct kbase_device *kbdev,
		unsigned int i)
{
	while (i-- > 0) {
		if (dev_init[i].term)
			dev_init[i].term(kbdev);
	}
}

void kbase_device_term(struct kbase_device *kbdev)
{
	kbase_device_term_partial(kbdev, ARRAY_SIZE(dev_init));
	kbasep_js_devdata_halt(kbdev);
	kbase_mem_halt(kbdev);
}

int kbase_device_init(struct kbase_device *kbdev)
{
	int err = 0;
	unsigned int i = 0;

	dev_info(kbdev->dev, "Kernel DDK version %s", MALI_RELEASE_NAME);

	kbase_device_id_init(kbdev);
	kbase_disjoint_init(kbdev);

	for (i = 0; i < ARRAY_SIZE(dev_init); i++) {
		if (dev_init[i].init) {
			err = dev_init[i].init(kbdev);
			if (err) {
				if (err != -EPROBE_DEFER)
					dev_err(kbdev->dev, "%s error = %d\n",
						dev_init[i].err_mes, err);
				kbase_device_term_partial(kbdev, i);
				break;
			}
		}
	}

	return err;
}

int kbase_device_firmware_init_once(struct kbase_device *kbdev)
{
	int ret = 0;

	mutex_lock(&kbdev->fw_load_lock);

	if (!kbdev->dummy_job_wa_loaded) {
		ret = kbase_dummy_job_wa_load(kbdev);
		if (!ret)
			kbdev->dummy_job_wa_loaded = true;
	}

	mutex_unlock(&kbdev->fw_load_lock);

	return ret;
}
