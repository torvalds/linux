/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */
#ifndef ADF_CNV_DBG_H
#define ADF_CNV_DBG_H

struct adf_accel_dev;

void adf_cnv_dbgfs_add(struct adf_accel_dev *accel_dev);
void adf_cnv_dbgfs_rm(struct adf_accel_dev *accel_dev);

#endif
