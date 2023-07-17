/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */

#ifndef ADF_DBGFS_H
#define ADF_DBGFS_H

#ifdef CONFIG_DEBUG_FS
void adf_dbgfs_init(struct adf_accel_dev *accel_dev);
void adf_dbgfs_add(struct adf_accel_dev *accel_dev);
void adf_dbgfs_rm(struct adf_accel_dev *accel_dev);
void adf_dbgfs_exit(struct adf_accel_dev *accel_dev);
#else
static inline void adf_dbgfs_init(struct adf_accel_dev *accel_dev)
{
}

static inline void adf_dbgfs_add(struct adf_accel_dev *accel_dev)
{
}

static inline void adf_dbgfs_rm(struct adf_accel_dev *accel_dev)
{
}

static inline void adf_dbgfs_exit(struct adf_accel_dev *accel_dev)
{
}
#endif
#endif
