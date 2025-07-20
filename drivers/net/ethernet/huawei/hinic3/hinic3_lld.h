/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_LLD_H_
#define _HINIC3_LLD_H_

#include <linux/auxiliary_bus.h>

struct hinic3_event_info;

#define HINIC3_NIC_DRV_NAME "hinic3"

int hinic3_lld_init(void);
void hinic3_lld_exit(void);
void hinic3_adev_event_register(struct auxiliary_device *adev,
				void (*event_handler)(struct auxiliary_device *adev,
						      struct hinic3_event_info *event));
void hinic3_adev_event_unregister(struct auxiliary_device *adev);
struct hinic3_hwdev *hinic3_adev_get_hwdev(struct auxiliary_device *adev);

#endif
