// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2023 ARM Limited. All rights reserved.
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

#include <mali_kbase_hwaccess_backend.h>
#include <hwcnt/backend/mali_kbase_hwcnt_backend_csf_if_fw.h>
#include <hwcnt/mali_kbase_hwcnt_watchdog_if_timer.h>
#include <mali_kbase_ctx_sched.h>
#include <mali_kbase_reset_gpu.h>
#include <csf/mali_kbase_csf.h>
#include <csf/ipa_control/mali_kbase_csf_ipa_control.h>
#include <backend/gpu/mali_kbase_model_linux.h>

#include <mali_kbase.h>
#include <backend/gpu/mali_kbase_irq_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <backend/gpu/mali_kbase_js_internal.h>
#include <backend/gpu/mali_kbase_clk_rate_trace_mgr.h>
#include <csf/mali_kbase_csf_csg_debugfs.h>
#include <hwcnt/mali_kbase_hwcnt_virtualizer.h>
#include <mali_kbase_kinstr_prfcnt.h>
#include <mali_kbase_vinstr.h>
#include <tl/mali_kbase_timeline.h>

/**
 * kbase_device_firmware_hwcnt_term - Terminate CSF firmware and HWC
 *
 * @kbdev: An instance of the GPU platform device, allocated from the probe
 *         method of the driver.
 *
 * When a kbase driver is removed, terminate CSF firmware and hardware counter
 * components.
 */
static void kbase_device_firmware_hwcnt_term(struct kbase_device *kbdev)
{
	if (kbdev->csf.firmware_inited) {
		kbase_kinstr_prfcnt_term(kbdev->kinstr_prfcnt_ctx);
		kbase_vinstr_term(kbdev->vinstr_ctx);
		kbase_hwcnt_virtualizer_term(kbdev->hwcnt_gpu_virt);
		kbase_hwcnt_backend_csf_metadata_term(&kbdev->hwcnt_gpu_iface);
		kbase_csf_firmware_unload_term(kbdev);
	}
}

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
#if IS_ENABLED(CONFIG_MALI_REAL_HW)
	if (kbasep_common_test_interrupt_handlers(kbdev) != 0) {
		dev_err(kbdev->dev, "Interrupt assignment check failed.\n");
		err = -EINVAL;
		goto fail_interrupt_test;
	}
#endif /* IS_ENABLED(CONFIG_MALI_REAL_HW) */
#endif /* CONFIG_MALI_BIFROST_DEBUG */

	kbase_ipa_control_init(kbdev);

	/* Initialise the metrics subsystem, it couldn't be initialized earlier
	 * due to dependency on kbase_ipa_control.
	 */
	err = kbasep_pm_metrics_init(kbdev);
	if (err)
		goto fail_pm_metrics_init;

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

	err = kbase_backend_time_init(kbdev);
	if (err)
		goto fail_update_l2_features;

	init_waitqueue_head(&kbdev->hwaccess.backend.reset_wait);

	kbase_pm_context_idle(kbdev);

	mutex_init(&kbdev->fw_load_lock);

	return 0;

fail_update_l2_features:
	kbase_backend_devfreq_term(kbdev);
fail_devfreq_init:
	kbasep_pm_metrics_term(kbdev);
fail_pm_metrics_init:
	kbase_ipa_control_term(kbdev);

#ifdef CONFIG_MALI_BIFROST_DEBUG
#if IS_ENABLED(CONFIG_MALI_REAL_HW)
fail_interrupt_test:
#endif /* IS_ENABLED(CONFIG_MALI_REAL_HW) */
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
	kbasep_pm_metrics_term(kbdev);
	kbase_ipa_control_term(kbdev);
	kbase_hwaccess_pm_halt(kbdev);
	kbase_reset_gpu_term(kbdev);
	kbase_hwaccess_pm_term(kbdev);
}

/**
 * kbase_csf_early_init - Early initialization for firmware & scheduler.
 * @kbdev:	Device pointer
 *
 * Return: 0 on success, error code otherwise.
 */
static int kbase_csf_early_init(struct kbase_device *kbdev)
{
	int err = kbase_csf_firmware_early_init(kbdev);

	if (err)
		return err;

	err = kbase_csf_scheduler_early_init(kbdev);

	return err;
}

/**
 * kbase_csf_early_term() - Early termination for firmware & scheduler.
 * @kbdev:	Device pointer
 */
static void kbase_csf_early_term(struct kbase_device *kbdev)
{
	kbase_csf_scheduler_early_term(kbdev);
	kbase_csf_firmware_early_term(kbdev);
}

/**
 * kbase_csf_late_init - late initialization for firmware.
 * @kbdev:	Device pointer
 *
 * Return: 0 on success, error code otherwise.
 */
static int kbase_csf_late_init(struct kbase_device *kbdev)
{
	int err = kbase_csf_firmware_late_init(kbdev);

	return err;
}

/**
 * kbase_device_hwcnt_watchdog_if_init - Create hardware counter watchdog
 *                                       interface.
 * @kbdev:	Device pointer
 *
 * Return: 0 if successful or a negative error code on failure.
 */
static int kbase_device_hwcnt_watchdog_if_init(struct kbase_device *kbdev)
{
	return kbase_hwcnt_watchdog_if_timer_create(
		&kbdev->hwcnt_watchdog_timer);
}

/**
 * kbase_device_hwcnt_watchdog_if_term - Terminate hardware counter watchdog
 *                                       interface.
 * @kbdev:	Device pointer
 */
static void kbase_device_hwcnt_watchdog_if_term(struct kbase_device *kbdev)
{
	kbase_hwcnt_watchdog_if_timer_destroy(&kbdev->hwcnt_watchdog_timer);
}

/**
 * kbase_device_hwcnt_backend_csf_if_init - Create hardware counter backend
 *                                          firmware interface.
 * @kbdev:	Device pointer
 * Return: 0 if successful or a negative error code on failure.
 */
static int kbase_device_hwcnt_backend_csf_if_init(struct kbase_device *kbdev)
{
	return kbase_hwcnt_backend_csf_if_fw_create(
		kbdev, &kbdev->hwcnt_backend_csf_if_fw);
}

/**
 * kbase_device_hwcnt_backend_csf_if_term - Terminate hardware counter backend
 *                                          firmware interface.
 * @kbdev:	Device pointer
 */
static void kbase_device_hwcnt_backend_csf_if_term(struct kbase_device *kbdev)
{
	kbase_hwcnt_backend_csf_if_fw_destroy(&kbdev->hwcnt_backend_csf_if_fw);
}

/**
 * kbase_device_hwcnt_backend_csf_init - Create hardware counter backend.
 * @kbdev:	Device pointer
 *
 * Return: 0 if successful or a negative error code on failure.
 */
static int kbase_device_hwcnt_backend_csf_init(struct kbase_device *kbdev)
{
	return kbase_hwcnt_backend_csf_create(
		&kbdev->hwcnt_backend_csf_if_fw,
		KBASE_HWCNT_BACKEND_CSF_RING_BUFFER_COUNT,
		&kbdev->hwcnt_watchdog_timer, &kbdev->hwcnt_gpu_iface);
}

/**
 * kbase_device_hwcnt_backend_csf_term - Terminate hardware counter backend.
 * @kbdev:	Device pointer
 */
static void kbase_device_hwcnt_backend_csf_term(struct kbase_device *kbdev)
{
	kbase_hwcnt_backend_csf_destroy(&kbdev->hwcnt_gpu_iface);
}

static const struct kbase_device_init dev_init[] = {
#if !IS_ENABLED(CONFIG_MALI_REAL_HW)
	{ kbase_gpu_device_create, kbase_gpu_device_destroy,
	  "Dummy model initialization failed" },
#else /* !IS_ENABLED(CONFIG_MALI_REAL_HW) */
	{ assign_irqs, NULL, "IRQ search failed" },
#endif /* !IS_ENABLED(CONFIG_MALI_REAL_HW) */
#if !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
	{ registers_map, registers_unmap, "Register map failed" },
#endif /* !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI) */
	{ power_control_init, power_control_term, "Power control initialization failed" },
	{ kbase_device_io_history_init, kbase_device_io_history_term,
	  "Register access history initialization failed" },
	{ kbase_device_early_init, kbase_device_early_term, "Early device initialization failed" },
	{ kbase_device_populate_max_freq, NULL, "Populating max frequency failed" },
	{ kbase_pm_lowest_gpu_freq_init, NULL, "Lowest freq initialization failed" },
	{ kbase_device_misc_init, kbase_device_misc_term,
	  "Miscellaneous device initialization failed" },
	{ kbase_device_pcm_dev_init, kbase_device_pcm_dev_term,
	  "Priority control manager initialization failed" },
	{ kbase_ctx_sched_init, kbase_ctx_sched_term, "Context scheduler initialization failed" },
	{ kbase_mem_init, kbase_mem_term, "Memory subsystem initialization failed" },
	{ kbase_csf_protected_memory_init, kbase_csf_protected_memory_term,
	  "Protected memory allocator initialization failed" },
	{ kbase_device_coherency_init, NULL, "Device coherency init failed" },
	{ kbase_protected_mode_init, kbase_protected_mode_term,
	  "Protected mode subsystem initialization failed" },
	{ kbase_device_list_init, kbase_device_list_term, "Device list setup failed" },
	{ kbase_device_timeline_init, kbase_device_timeline_term,
	  "Timeline stream initialization failed" },
	{ kbase_clk_rate_trace_manager_init, kbase_clk_rate_trace_manager_term,
	  "Clock rate trace manager initialization failed" },
	{ kbase_device_hwcnt_watchdog_if_init, kbase_device_hwcnt_watchdog_if_term,
	  "GPU hwcnt backend watchdog interface creation failed" },
	{ kbase_device_hwcnt_backend_csf_if_init, kbase_device_hwcnt_backend_csf_if_term,
	  "GPU hwcnt backend CSF interface creation failed" },
	{ kbase_device_hwcnt_backend_csf_init, kbase_device_hwcnt_backend_csf_term,
	  "GPU hwcnt backend creation failed" },
	{ kbase_device_hwcnt_context_init, kbase_device_hwcnt_context_term,
	  "GPU hwcnt context initialization failed" },
	{ kbase_csf_early_init, kbase_csf_early_term, "Early CSF initialization failed" },
	{ kbase_backend_late_init, kbase_backend_late_term, "Late backend initialization failed" },
	{ kbase_csf_late_init, NULL, "Late CSF initialization failed" },
	{ NULL, kbase_device_firmware_hwcnt_term, NULL },
	{ kbase_debug_csf_fault_init, kbase_debug_csf_fault_term,
	  "CSF fault debug initialization failed" },
	{ kbase_device_debugfs_init, kbase_device_debugfs_term, "DebugFS initialization failed" },
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
	{ kbase_device_late_init, kbase_device_late_term, "Late device initialization failed" },
#if IS_ENABLED(CONFIG_MALI_CORESIGHT)
	{ kbase_debug_coresight_csf_init, kbase_debug_coresight_csf_term,
	  "Coresight initialization failed" },
#endif /* IS_ENABLED(CONFIG_MALI_CORESIGHT) */
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
				dev_err(kbdev->dev, "%s error = %d\n",
					dev_init[i].err_mes, err);
				kbase_device_term_partial(kbdev, i);
				break;
			}
		}
	}

	return err;
}

/**
 * kbase_device_hwcnt_csf_deferred_init - Initialize CSF deferred HWC components
 *
 * @kbdev: An instance of the GPU platform device, allocated from the probe
 *         method of the driver.
 *
 * Hardware counter components depending on firmware are initialized after CSF
 * firmware is loaded.
 *
 * Return: 0 on success. An error code on failure.
 */
static int kbase_device_hwcnt_csf_deferred_init(struct kbase_device *kbdev)
{
	int ret = 0;

	/* For CSF GPUs, HWC metadata needs to query information from CSF
	 * firmware, so the initialization of HWC metadata only can be called
	 * after firmware initialized, but firmware initialization depends on
	 * HWC backend initialization, so we need to separate HWC backend
	 * metadata initialization from HWC backend initialization.
	 */
	ret = kbase_hwcnt_backend_csf_metadata_init(&kbdev->hwcnt_gpu_iface);
	if (ret) {
		dev_err(kbdev->dev,
			"GPU hwcnt backend metadata creation failed");
		return ret;
	}

	ret = kbase_hwcnt_virtualizer_init(
		kbdev->hwcnt_gpu_ctx,
		KBASE_HWCNT_GPU_VIRTUALIZER_DUMP_THRESHOLD_NS,
		&kbdev->hwcnt_gpu_virt);
	if (ret) {
		dev_err(kbdev->dev,
			"GPU hwcnt virtualizer initialization failed");
		goto virt_fail;
	}

	ret = kbase_vinstr_init(kbdev->hwcnt_gpu_virt, &kbdev->vinstr_ctx);
	if (ret) {
		dev_err(kbdev->dev,
			"Virtual instrumentation initialization failed");
		goto vinstr_fail;
	}

	ret = kbase_kinstr_prfcnt_init(kbdev->hwcnt_gpu_virt,
				       &kbdev->kinstr_prfcnt_ctx);
	if (ret) {
		dev_err(kbdev->dev,
			"Performance counter instrumentation initialization failed");
		goto kinstr_prfcnt_fail;
	}

	return ret;

kinstr_prfcnt_fail:
	kbase_vinstr_term(kbdev->vinstr_ctx);

vinstr_fail:
	kbase_hwcnt_virtualizer_term(kbdev->hwcnt_gpu_virt);

virt_fail:
	kbase_hwcnt_backend_csf_metadata_term(&kbdev->hwcnt_gpu_iface);
	return ret;
}

/**
 * kbase_csf_firmware_deferred_init - Load and initialize CSF firmware
 *
 * @kbdev: An instance of the GPU platform device, allocated from the probe
 *         method of the driver.
 *
 * Called when a device file is opened for the first time.
 * To meet Android GKI vendor guideline, firmware load is deferred at
 * the time when @ref kbase_open is called for the first time.
 *
 * Return: 0 on success. An error code on failure.
 */
static int kbase_csf_firmware_deferred_init(struct kbase_device *kbdev)
{
	int err = 0;

	lockdep_assert_held(&kbdev->fw_load_lock);

	err = kbase_csf_firmware_load_init(kbdev);
	if (!err) {
		unsigned long flags;

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbdev->pm.backend.mcu_state = KBASE_MCU_ON;
		kbdev->csf.firmware_inited = true;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	} else {
		dev_err(kbdev->dev, "Firmware initialization failed");
	}

	return err;
}

int kbase_device_firmware_init_once(struct kbase_device *kbdev)
{
	int ret = 0;

	mutex_lock(&kbdev->fw_load_lock);

	if (!kbdev->csf.firmware_inited) {
		kbase_pm_context_active(kbdev);

		ret = kbase_csf_firmware_deferred_init(kbdev);
		if (ret)
			goto out;

		ret = kbase_device_hwcnt_csf_deferred_init(kbdev);
		if (ret) {
			kbase_csf_firmware_unload_term(kbdev);
			goto out;
		}

		kbase_csf_debugfs_init(kbdev);
		kbase_timeline_io_debugfs_init(kbdev);
out:
		kbase_pm_context_idle(kbdev);
	}

	mutex_unlock(&kbdev->fw_load_lock);

	return ret;
}
KBASE_EXPORT_TEST_API(kbase_device_firmware_init_once);
