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

#endif /* _AMDXDNA_PM_H_ */
