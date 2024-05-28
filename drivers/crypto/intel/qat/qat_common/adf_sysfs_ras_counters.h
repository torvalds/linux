/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */

#ifndef ADF_RAS_H
#define ADF_RAS_H

#include <linux/bitops.h>
#include <linux/atomic.h>

struct adf_accel_dev;

void adf_sysfs_start_ras(struct adf_accel_dev *accel_dev);
void adf_sysfs_stop_ras(struct adf_accel_dev *accel_dev);

#define ADF_RAS_ERR_CTR_READ(ras_errors, ERR) \
	atomic_read(&(ras_errors).counter[ERR])

#define ADF_RAS_ERR_CTR_CLEAR(ras_errors) \
	do { \
		for (int err = 0; err < ADF_RAS_ERRORS; ++err) \
			atomic_set(&(ras_errors).counter[err], 0); \
	} while (0)

#define ADF_RAS_ERR_CTR_INC(ras_errors, ERR) \
	atomic_inc(&(ras_errors).counter[ERR])

#endif /* ADF_RAS_H */
