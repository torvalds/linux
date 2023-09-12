/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#ifndef __IVPU_PM_H__
#define __IVPU_PM_H__

#include <linux/types.h>

struct ivpu_device;

struct ivpu_pm_info {
	struct ivpu_device *vdev;
	struct work_struct recovery_work;
	atomic_t in_reset;
	atomic_t reset_counter;
	bool is_warmboot;
	u32 suspend_reschedule_counter;
};

int ivpu_pm_init(struct ivpu_device *vdev);
void ivpu_pm_enable(struct ivpu_device *vdev);
void ivpu_pm_disable(struct ivpu_device *vdev);
void ivpu_pm_cancel_recovery(struct ivpu_device *vdev);

int ivpu_pm_suspend_cb(struct device *dev);
int ivpu_pm_resume_cb(struct device *dev);
int ivpu_pm_runtime_suspend_cb(struct device *dev);
int ivpu_pm_runtime_resume_cb(struct device *dev);

void ivpu_pm_reset_prepare_cb(struct pci_dev *pdev);
void ivpu_pm_reset_done_cb(struct pci_dev *pdev);

int __must_check ivpu_rpm_get(struct ivpu_device *vdev);
void ivpu_rpm_put(struct ivpu_device *vdev);

void ivpu_pm_schedule_recovery(struct ivpu_device *vdev);

#endif /* __IVPU_PM_H__ */
