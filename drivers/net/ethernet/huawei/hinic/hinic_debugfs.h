/* SPDX-License-Identifier: GPL-2.0-only */
/* Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#ifndef HINIC_DEBUGFS_H
#define HINIC_DEBUGFS_H

#include "hinic_dev.h"

int hinic_sq_debug_add(struct hinic_dev *dev, u16 sq_id);

void hinic_sq_debug_rem(struct hinic_sq *sq);

int hinic_rq_debug_add(struct hinic_dev *dev, u16 rq_id);

void hinic_rq_debug_rem(struct hinic_rq *rq);

void hinic_sq_dbgfs_init(struct hinic_dev *nic_dev);

void hinic_sq_dbgfs_uninit(struct hinic_dev *nic_dev);

void hinic_rq_dbgfs_init(struct hinic_dev *nic_dev);

void hinic_rq_dbgfs_uninit(struct hinic_dev *nic_dev);

void hinic_dbg_init(struct hinic_dev *nic_dev);

void hinic_dbg_uninit(struct hinic_dev *nic_dev);

void hinic_dbg_register_debugfs(const char *debugfs_dir_name);

void hinic_dbg_unregister_debugfs(void);

#endif
