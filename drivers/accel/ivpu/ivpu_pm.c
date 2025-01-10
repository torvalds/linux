// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#include <linux/highmem.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/reboot.h>

#include "ivpu_coredump.h"
#include "ivpu_drv.h"
#include "ivpu_fw.h"
#include "ivpu_fw_log.h"
#include "ivpu_hw.h"
#include "ivpu_ipc.h"
#include "ivpu_job.h"
#include "ivpu_jsm_msg.h"
#include "ivpu_mmu.h"
#include "ivpu_ms.h"
#include "ivpu_pm.h"
#include "ivpu_trace.h"
#include "vpu_boot_api.h"

static bool ivpu_disable_recovery;
#if IS_ENABLED(CONFIG_DRM_ACCEL_IVPU_DEBUG)
module_param_named_unsafe(disable_recovery, ivpu_disable_recovery, bool, 0644);
MODULE_PARM_DESC(disable_recovery, "Disables recovery when NPU hang is detected");
#endif

static unsigned long ivpu_tdr_timeout_ms;
module_param_named(tdr_timeout_ms, ivpu_tdr_timeout_ms, ulong, 0644);
MODULE_PARM_DESC(tdr_timeout_ms, "Timeout for device hang detection, in milliseconds, 0 - default");

#define PM_RESCHEDULE_LIMIT     5

static void ivpu_pm_prepare_cold_boot(struct ivpu_device *vdev)
{
	struct ivpu_fw_info *fw = vdev->fw;

	ivpu_cmdq_reset_all_contexts(vdev);
	ivpu_ipc_reset(vdev);
	ivpu_fw_log_reset(vdev);
	ivpu_fw_load(vdev);
	fw->entry_point = fw->cold_boot_entry_point;
}

static void ivpu_pm_prepare_warm_boot(struct ivpu_device *vdev)
{
	struct ivpu_fw_info *fw = vdev->fw;
	struct vpu_boot_params *bp = ivpu_bo_vaddr(fw->mem);

	if (!bp->save_restore_ret_address) {
		ivpu_pm_prepare_cold_boot(vdev);
		return;
	}

	ivpu_dbg(vdev, FW_BOOT, "Save/restore entry point %llx", bp->save_restore_ret_address);
	fw->entry_point = bp->save_restore_ret_address;
}

static int ivpu_suspend(struct ivpu_device *vdev)
{
	int ret;

	ivpu_prepare_for_reset(vdev);

	ret = ivpu_shutdown(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to shutdown NPU: %d\n", ret);

	return ret;
}

static int ivpu_resume(struct ivpu_device *vdev)
{
	int ret;

retry:
	pci_set_power_state(to_pci_dev(vdev->drm.dev), PCI_D0);
	pci_restore_state(to_pci_dev(vdev->drm.dev));

	ret = ivpu_hw_power_up(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to power up HW: %d\n", ret);
		goto err_power_down;
	}

	ret = ivpu_mmu_enable(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to resume MMU: %d\n", ret);
		goto err_power_down;
	}

	ret = ivpu_boot(vdev);
	if (ret)
		goto err_mmu_disable;

	return 0;

err_mmu_disable:
	ivpu_mmu_disable(vdev);
err_power_down:
	ivpu_hw_power_down(vdev);
	pci_set_power_state(to_pci_dev(vdev->drm.dev), PCI_D3hot);

	if (!ivpu_fw_is_cold_boot(vdev)) {
		ivpu_pm_prepare_cold_boot(vdev);
		goto retry;
	} else {
		ivpu_err(vdev, "Failed to resume the FW: %d\n", ret);
	}

	return ret;
}

static void ivpu_pm_recovery_work(struct work_struct *work)
{
	struct ivpu_pm_info *pm = container_of(work, struct ivpu_pm_info, recovery_work);
	struct ivpu_device *vdev = pm->vdev;
	char *evt[2] = {"IVPU_PM_EVENT=IVPU_RECOVER", NULL};
	int ret;

	ivpu_err(vdev, "Recovering the NPU (reset #%d)\n", atomic_read(&vdev->pm->reset_counter));

	ret = pm_runtime_resume_and_get(vdev->drm.dev);
	if (ret)
		ivpu_err(vdev, "Failed to resume NPU: %d\n", ret);

	ivpu_jsm_state_dump(vdev);
	ivpu_dev_coredump(vdev);

	atomic_inc(&vdev->pm->reset_counter);
	atomic_set(&vdev->pm->reset_pending, 1);
	down_write(&vdev->pm->reset_lock);

	ivpu_suspend(vdev);
	ivpu_pm_prepare_cold_boot(vdev);
	ivpu_jobs_abort_all(vdev);
	ivpu_ms_cleanup_all(vdev);

	ret = ivpu_resume(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to resume NPU: %d\n", ret);

	up_write(&vdev->pm->reset_lock);
	atomic_set(&vdev->pm->reset_pending, 0);

	kobject_uevent_env(&vdev->drm.dev->kobj, KOBJ_CHANGE, evt);
	pm_runtime_mark_last_busy(vdev->drm.dev);
	pm_runtime_put_autosuspend(vdev->drm.dev);
}

void ivpu_pm_trigger_recovery(struct ivpu_device *vdev, const char *reason)
{
	ivpu_err(vdev, "Recovery triggered by %s\n", reason);

	if (ivpu_disable_recovery) {
		ivpu_err(vdev, "Recovery not available when disable_recovery param is set\n");
		return;
	}

	if (ivpu_is_fpga(vdev)) {
		ivpu_err(vdev, "Recovery not available on FPGA\n");
		return;
	}

	/* Trigger recovery if it's not in progress */
	if (atomic_cmpxchg(&vdev->pm->reset_pending, 0, 1) == 0) {
		ivpu_hw_diagnose_failure(vdev);
		ivpu_hw_irq_disable(vdev); /* Disable IRQ early to protect from IRQ storm */
		queue_work(system_long_wq, &vdev->pm->recovery_work);
	}
}

static void ivpu_job_timeout_work(struct work_struct *work)
{
	struct ivpu_pm_info *pm = container_of(work, struct ivpu_pm_info, job_timeout_work.work);
	struct ivpu_device *vdev = pm->vdev;

	ivpu_pm_trigger_recovery(vdev, "TDR");
}

void ivpu_start_job_timeout_detection(struct ivpu_device *vdev)
{
	unsigned long timeout_ms = ivpu_tdr_timeout_ms ? ivpu_tdr_timeout_ms : vdev->timeout.tdr;

	/* No-op if already queued */
	queue_delayed_work(system_wq, &vdev->pm->job_timeout_work, msecs_to_jiffies(timeout_ms));
}

void ivpu_stop_job_timeout_detection(struct ivpu_device *vdev)
{
	cancel_delayed_work_sync(&vdev->pm->job_timeout_work);
}

int ivpu_pm_suspend_cb(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	unsigned long timeout;

	trace_pm("suspend");
	ivpu_dbg(vdev, PM, "Suspend..\n");

	timeout = jiffies + msecs_to_jiffies(vdev->timeout.tdr);
	while (!ivpu_hw_is_idle(vdev)) {
		cond_resched();
		if (time_after_eq(jiffies, timeout)) {
			ivpu_err(vdev, "Failed to enter idle on system suspend\n");
			return -EBUSY;
		}
	}

	ivpu_jsm_pwr_d0i3_enter(vdev);

	ivpu_suspend(vdev);
	ivpu_pm_prepare_warm_boot(vdev);

	ivpu_dbg(vdev, PM, "Suspend done.\n");
	trace_pm("suspend done");

	return 0;
}

int ivpu_pm_resume_cb(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	int ret;

	trace_pm("resume");
	ivpu_dbg(vdev, PM, "Resume..\n");

	ret = ivpu_resume(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to resume: %d\n", ret);

	ivpu_dbg(vdev, PM, "Resume done.\n");
	trace_pm("resume done");

	return ret;
}

int ivpu_pm_runtime_suspend_cb(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	int ret, ret_d0i3;
	bool is_idle;

	drm_WARN_ON(&vdev->drm, !xa_empty(&vdev->submitted_jobs_xa));
	drm_WARN_ON(&vdev->drm, work_pending(&vdev->pm->recovery_work));

	trace_pm("runtime suspend");
	ivpu_dbg(vdev, PM, "Runtime suspend..\n");

	ivpu_mmu_disable(vdev);

	is_idle = ivpu_hw_is_idle(vdev) || vdev->pm->dct_active_percent;
	if (!is_idle)
		ivpu_err(vdev, "NPU is not idle before autosuspend\n");

	ret_d0i3 = ivpu_jsm_pwr_d0i3_enter(vdev);
	if (ret_d0i3)
		ivpu_err(vdev, "Failed to prepare for d0i3: %d\n", ret_d0i3);

	ret = ivpu_suspend(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to suspend NPU: %d\n", ret);

	if (!is_idle || ret_d0i3) {
		ivpu_err(vdev, "Forcing cold boot due to previous errors\n");
		atomic_inc(&vdev->pm->reset_counter);
		ivpu_dev_coredump(vdev);
		ivpu_pm_prepare_cold_boot(vdev);
	} else {
		ivpu_pm_prepare_warm_boot(vdev);
	}

	ivpu_dbg(vdev, PM, "Runtime suspend done.\n");
	trace_pm("runtime suspend done");

	return 0;
}

int ivpu_pm_runtime_resume_cb(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	int ret;

	trace_pm("runtime resume");
	ivpu_dbg(vdev, PM, "Runtime resume..\n");

	ret = ivpu_resume(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to set RESUME state: %d\n", ret);

	ivpu_dbg(vdev, PM, "Runtime resume done.\n");
	trace_pm("runtime resume done");

	return ret;
}

int ivpu_rpm_get(struct ivpu_device *vdev)
{
	int ret;

	ret = pm_runtime_resume_and_get(vdev->drm.dev);
	drm_WARN_ON(&vdev->drm, ret < 0);

	return ret;
}

void ivpu_rpm_put(struct ivpu_device *vdev)
{
	pm_runtime_mark_last_busy(vdev->drm.dev);
	pm_runtime_put_autosuspend(vdev->drm.dev);
}

void ivpu_pm_reset_prepare_cb(struct pci_dev *pdev)
{
	struct ivpu_device *vdev = pci_get_drvdata(pdev);

	ivpu_dbg(vdev, PM, "Pre-reset..\n");
	atomic_inc(&vdev->pm->reset_counter);
	atomic_set(&vdev->pm->reset_pending, 1);

	pm_runtime_get_sync(vdev->drm.dev);
	down_write(&vdev->pm->reset_lock);
	ivpu_prepare_for_reset(vdev);
	ivpu_hw_reset(vdev);
	ivpu_pm_prepare_cold_boot(vdev);
	ivpu_jobs_abort_all(vdev);
	ivpu_ms_cleanup_all(vdev);

	ivpu_dbg(vdev, PM, "Pre-reset done.\n");
}

void ivpu_pm_reset_done_cb(struct pci_dev *pdev)
{
	struct ivpu_device *vdev = pci_get_drvdata(pdev);
	int ret;

	ivpu_dbg(vdev, PM, "Post-reset..\n");
	ret = ivpu_resume(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to set RESUME state: %d\n", ret);
	up_write(&vdev->pm->reset_lock);
	atomic_set(&vdev->pm->reset_pending, 0);
	ivpu_dbg(vdev, PM, "Post-reset done.\n");

	pm_runtime_mark_last_busy(vdev->drm.dev);
	pm_runtime_put_autosuspend(vdev->drm.dev);
}

void ivpu_pm_init(struct ivpu_device *vdev)
{
	struct device *dev = vdev->drm.dev;
	struct ivpu_pm_info *pm = vdev->pm;
	int delay;

	pm->vdev = vdev;

	init_rwsem(&pm->reset_lock);
	atomic_set(&pm->reset_pending, 0);
	atomic_set(&pm->reset_counter, 0);

	INIT_WORK(&pm->recovery_work, ivpu_pm_recovery_work);
	INIT_DELAYED_WORK(&pm->job_timeout_work, ivpu_job_timeout_work);

	if (ivpu_disable_recovery)
		delay = -1;
	else
		delay = vdev->timeout.autosuspend;

	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, delay);
	pm_runtime_set_active(dev);

	ivpu_dbg(vdev, PM, "Autosuspend delay = %d\n", delay);
}

void ivpu_pm_cancel_recovery(struct ivpu_device *vdev)
{
	drm_WARN_ON(&vdev->drm, delayed_work_pending(&vdev->pm->job_timeout_work));
	cancel_work_sync(&vdev->pm->recovery_work);
}

void ivpu_pm_enable(struct ivpu_device *vdev)
{
	struct device *dev = vdev->drm.dev;

	pm_runtime_allow(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
}

void ivpu_pm_disable(struct ivpu_device *vdev)
{
	pm_runtime_get_noresume(vdev->drm.dev);
	pm_runtime_forbid(vdev->drm.dev);
}

int ivpu_pm_dct_init(struct ivpu_device *vdev)
{
	if (vdev->pm->dct_active_percent)
		return ivpu_pm_dct_enable(vdev, vdev->pm->dct_active_percent);

	return 0;
}

int ivpu_pm_dct_enable(struct ivpu_device *vdev, u8 active_percent)
{
	u32 active_us, inactive_us;
	int ret;

	if (active_percent == 0 || active_percent > 100)
		return -EINVAL;

	active_us = (DCT_PERIOD_US * active_percent) / 100;
	inactive_us = DCT_PERIOD_US - active_us;

	ret = ivpu_jsm_dct_enable(vdev, active_us, inactive_us);
	if (ret) {
		ivpu_err_ratelimited(vdev, "Failed to enable DCT: %d\n", ret);
		return ret;
	}

	vdev->pm->dct_active_percent = active_percent;

	ivpu_dbg(vdev, PM, "DCT set to %u%% (D0: %uus, D0i2: %uus)\n",
		 active_percent, active_us, inactive_us);
	return 0;
}

int ivpu_pm_dct_disable(struct ivpu_device *vdev)
{
	int ret;

	ret = ivpu_jsm_dct_disable(vdev);
	if (ret) {
		ivpu_err_ratelimited(vdev, "Failed to disable DCT: %d\n", ret);
		return ret;
	}

	vdev->pm->dct_active_percent = 0;

	ivpu_dbg(vdev, PM, "DCT disabled\n");
	return 0;
}

void ivpu_pm_dct_irq_thread_handler(struct ivpu_device *vdev)
{
	bool enable;
	int ret;

	if (ivpu_hw_btrs_dct_get_request(vdev, &enable))
		return;

	if (vdev->pm->dct_active_percent)
		ret = ivpu_pm_dct_enable(vdev, DCT_DEFAULT_ACTIVE_PERCENT);
	else
		ret = ivpu_pm_dct_disable(vdev);

	if (!ret)
		ivpu_hw_btrs_dct_set_status(vdev, enable, vdev->pm->dct_active_percent);
}
