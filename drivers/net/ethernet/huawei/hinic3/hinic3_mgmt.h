/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_MGMT_H_
#define _HINIC3_MGMT_H_

#include <linux/types.h>

struct hinic3_hwdev;

void hinic3_flush_mgmt_workq(struct hinic3_hwdev *hwdev);

#endif
