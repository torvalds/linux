/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */
#ifndef ADF_FW_COUNTERS_H
#define ADF_FW_COUNTERS_H

struct adf_accel_dev;

void adf_fw_counters_dbgfs_add(struct adf_accel_dev *accel_dev);
void adf_fw_counters_dbgfs_rm(struct adf_accel_dev *accel_dev);

#endif
