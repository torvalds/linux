/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_HW_COMM_H_
#define _HINIC3_HW_COMM_H_

#include "hinic3_hw_intf.h"

struct hinic3_hwdev;

int hinic3_func_reset(struct hinic3_hwdev *hwdev, u16 func_id, u64 reset_flag);

#endif
