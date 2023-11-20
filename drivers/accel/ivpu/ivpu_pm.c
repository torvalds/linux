// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#include <linux/highmem.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/reboot.h>

#include "vpu_boot_api.h"
#include "ivpu_drv.h"
#include "ivpu_hw.h"
#include "ivpu_fw.h"
#include "ivpu_ipc.h"
#include "ivpu_job.h"
#include "ivpu_mmu.h"
#include "ivpu_pm.h"

static bool ivpu_disable_recovery;
module_param_named_unsafe(disable_recovery, ivpu_disable_recovery, bool, 0644);
MODULE_PARM_DESC(disable_recovery, "Disables recovery when VPU hang is detected");

#define PM_RESCHEDULE_LIMIT     5

static void ivpu_pm_prepare_cold_boot(struct ivpu_device *vdev)
{
	struct ivpu_fw_info *fw = vdev->fw;

	ivpu_cmdq_reset_all_contexts(vdev);
	ivpu_ipc_reset(vdev);
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

	ret = ivpu_shutdown(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to shutdown VPU: %d\n", ret);
		return ret;
	}

	return ret;
}

static int ivpu_resume(struct ivpu_device *vdev)
{
	int ret;

retry:
	ret = ivpu_hw_power_up(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to power up HW: %d\n", ret);
		return ret;
	}

	ret = ivpu_mmu_enable(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to resume MMU: %d\n", ret);
		ivpu_hw_power_down(vdev);
		return ret;
	}

	ret = ivpu_boot(vdev);
	if (ret) {
		ivpu_mmu_disable(vdev);
		ivpu_hw_power_down(vdev);
		if (!ivpu_fw_is_cold_boot(vdev)) {
			ivpu_warn(vdev, "Failed to resume the FW: %d. Retrying cold boot..\n", ret);
			ivpu_pm_prepare_cold_boot(vdev);
			goto retry;
		} else {
			ivpu_err(vdev, "Failed to resume the FW: %d\n", ret);
		}
	}

	return ret;
}

static void ivpu_pm_recovery_work(struct work_struct *work)
{
	struct ivpu_pm_info *pm = container_of(work, struct ivpu_pm_info, recovery_work);
	struct ivpu_device *vdev = pm->vdev;
	char *evt[2] = {"IVPU_PM_EVENT=IVPU_RECOVER", NULL};
	int ret;

retry:
	ret = pci_try_reset_function(to_pci_dev(vdev->drm.dev));
	if (ret == -EAGAIN && !drm_dev_is_unplugged(&vdev->drm)) {
		cond_resched();
		goto retry;
	}

	if (ret && ret != -EAGAIN)
		ivpu_err(vdev, "Failed to reset VPU: %d\n", ret);

	kobject_uevent_env(&vdev->drm.dev->kobj, KOBJ_CHANGE, evt);
}

void ivpu_pm_schedule_recovery(struct ivpu_device *vdev)
{
	struct ivpu_pm_info *pm = vdev->pm;

	if (ivpu_disable_recovery) {
		ivpu_err(vdev, "Recovery not available when disable_recovery param is set\n");
		return;
	}

	if (ivpu_is_fpga(vdev)) {
		ivpu_err(vdev, "Recovery not available on FPGA\n");
		return;
	}

	/* Schedule recovery if it's not in progress */
	if (atomic_cmpxchg(&pm->in_reset, 0, 1) == 0) {
		ivpu_hw_irq_disable(vdev);
		queue_work(system_long_wq, &pm->recovery_work);
	}
}

int ivpu_pm_suspend_cb(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	unsigned long timeout;

	ivpu_dbg(vdev, PM, "Suspend..\n");

	timeout = jiffies + msecs_to_jiffies(vdev->timeout.tdr);
	while (!ivpu_hw_is_idle(vdev)) {
		cond_resched();
		if (time_after_eq(jiffies, timeout)) {
			ivpu_err(vdev, "Failed to enter idle on system suspend\n");
			return -EBUSY;
		}
	}

	ivpu_suspend(vdev);
	ivpu_pm_prepare_warm_boot(vdev);

	pci_save_state(to_pci_dev(dev));
	pci_set_power_state(to_pci_dev(dev), PCI_D3hot);

	ivpu_dbg(vdev, PM, "Suspend done.\n");

	return 0;
}

int ivpu_pm_resume_cb(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	int ret;

	ivpu_dbg(vdev, PM, "Resume..\n");

	pci_set_power_state(to_pci_dev(dev), PCI_D0);
	pci_restore_state(to_pci_dev(dev));

	ret = ivpu_resume(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to resume: %d\n", ret);

	ivpu_dbg(vdev, PM, "Resume done.\n");

	return ret;
}

int ivpu_pm_runtime_suspend_cb(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	int ret;

	ivpu_dbg(vdev, PM, "Runtime suspend..\n");

	if (!ivpu_hw_is_idle(vdev) && vdev->pm->suspend_reschedule_counter) {
		ivpu_dbg(vdev, PM, "Failed to enter idle, rescheduling suspend, retries left %d\n",
			 vdev->pm->suspend_reschedule_counter);
		pm_schedule_suspend(dev, vdev->timeout.reschedule_suspend);
		vdev->pm->suspend_reschedule_counter--;
		return -EAGAIN;
	}

	ret = ivpu_suspend(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to set suspend VPU: %d\n", ret);

	if (!vdev->pm->suspend_reschedule_counter) {
		ivpu_warn(vdev, "VPU failed to enter idle, force suspended.\n");
		ivpu_pm_prepare_cold_boot(vdev);
	} else {
		ivpu_pm_prepare_warm_boot(vdev);
	}

	vdev->pm->suspend_reschedule_counter = PM_RESCHEDULE_LIMIT;

	ivpu_dbg(vdev, PM, "Runtime suspend done.\n");

	return 0;
}

int ivpu_pm_runtime_resume_cb(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	int ret;

	ivpu_dbg(vdev, PM, "Runtime resume..\n");

	ret = ivpu_resume(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to set RESUME state: %d\n", ret);

	ivpu_dbg(vdev, PM, "Runtime resume done.\n");

	return ret;
}

int ivpu_rpm_get(struct ivpu_device *vdev)
{
	int ret;

	ret = pm_runtime_resume_and_get(vdev->drm.dev);
	if (!drm_WARN_ON(&vdev->drm, ret < 0))
		vdev->pm->suspend_reschedule_counter = PM_RESCHEDULE_LIMIT;

	return ret;
}

int ivpu_rpm_get_if_active(struct ivpu_device *vdev)
{
	int ret;

	ret = pm_runtime_get_if_active(vdev->drm.dev, false);
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

	pm_runtime_get_sync(vdev->drm.dev);

	ivpu_dbg(vdev, PM, "Pre-reset..\n");
	atomic_inc(&vdev->pm->reset_counter);
	atomic_set(&vdev->pm->in_reset, 1);
	ivpu_prepare_for_reset(vdev);
	ivpu_hw_reset(vdev);
	ivpu_pm_prepare_cold_boot(vdev);
	ivpu_jobs_abort_all(vdev);
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
	atomic_set(&vdev->pm->in_reset, 0);
	ivpu_dbg(vdev, PM, "Post-reset done.\n");

	pm_runtime_put_autosuspend(vdev->drm.dev);
}

void ivpu_pm_init(struct ivpu_device *vdev)
{
	struct device *dev = vdev->drm.dev;
	struct ivpu_pm_info *pm = vdev->pm;
	int delay;

	pm->vdev = vdev;
	pm->suspend_reschedule_counter = PM_RESCHEDULE_LIMIT;

	atomic_set(&pm->in_reset, 0);
	INIT_WORK(&pm->recovery_work, ivpu_pm_recovery_work);

	if (ivpu_disable_recovery)
		delay = -1;
	else
		delay = vdev->timeout.autosuspend;

	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, delay);

	ivpu_dbg(vdev, PM, "Autosuspend delay = %d\n", delay);
}

void ivpu_pm_cancel_recovery(struct ivpu_device *vdev)
{
	cancel_work_sync(&vdev->pm->recovery_work);
}

void ivpu_pm_enable(struct ivpu_device *vdev)
{
	struct device *dev = vdev->drm.dev;

	pm_runtime_set_active(dev);
	pm_runtime_allow(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
}

void ivpu_pm_disable(struct ivpu_device *vdev)
{
	pm_runtime_get_noresume(vdev->drm.dev);
	pm_runtime_forbid(vdev->drm.dev);
}
