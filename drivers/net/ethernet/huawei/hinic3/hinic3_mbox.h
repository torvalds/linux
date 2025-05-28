/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_MBOX_H_
#define _HINIC3_MBOX_H_

#include <linux/bitfield.h>
#include <linux/mutex.h>

struct hinic3_hwdev;

int hinic3_send_mbox_to_mgmt(struct hinic3_hwdev *hwdev, u8 mod, u16 cmd,
			     const struct mgmt_msg_params *msg_params);

#endif
