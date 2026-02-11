/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025, Advanced Micro Devices, Inc.
 */

#ifndef _AMDXDNA_PM_H_
#define _AMDXDNA_PM_H_

#include "amdxdna_pci_drv.h"

int amdxdna_pm_suspend(struct device *dev);
int amdxdna_pm_resume(struct device  *dev);
int amdxdna_pm_resume_get(struct amdxdna_dev *xdna);
void amdxdna_pm_suspend_put(struct amdxdna_dev *xdna);
void amdxdna_pm_init(struct amdxdna_dev *xdna);
void amdxdna_pm_fini(struct amdxdna_dev *xdna);

static inline int amdxdna_pm_resume_get_locked(struct amdxdna_dev *xdna)
{
	int ret;

	mutex_unlock(&xdna->dev_lock);
	ret = amdxdna_pm_resume_get(xdna);
	mutex_lock(&xdna->dev_lock);

	return ret;
}

#endif /* _AMDXDNA_PM_H_ */
