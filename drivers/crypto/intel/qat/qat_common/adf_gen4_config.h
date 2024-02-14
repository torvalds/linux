/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */
#ifndef ADF_GEN4_CONFIG_H_
#define ADF_GEN4_CONFIG_H_

#include "adf_accel_devices.h"

int adf_gen4_dev_config(struct adf_accel_dev *accel_dev);
int adf_gen4_cfg_dev_init(struct adf_accel_dev *accel_dev);

#endif
