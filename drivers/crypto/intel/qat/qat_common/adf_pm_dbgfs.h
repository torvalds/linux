/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */

#ifndef ADF_PM_DBGFS_H_
#define ADF_PM_DBGFS_H_

struct adf_accel_dev;

void adf_pm_dbgfs_rm(struct adf_accel_dev *accel_dev);
void adf_pm_dbgfs_add(struct adf_accel_dev *accel_dev);

#endif /* ADF_PM_DBGFS_H_ */
