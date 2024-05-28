/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */
#ifndef ADF_SYSFS_RL_H_
#define ADF_SYSFS_RL_H_

struct adf_accel_dev;

int adf_sysfs_rl_add(struct adf_accel_dev *accel_dev);
void adf_sysfs_rl_rm(struct adf_accel_dev *accel_dev);

#endif /* ADF_SYSFS_RL_H_ */
