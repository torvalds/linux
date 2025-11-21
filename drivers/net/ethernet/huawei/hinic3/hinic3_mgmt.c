// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include "hinic3_eqs.h"
#include "hinic3_hwdev.h"
#include "hinic3_mbox.h"
#include "hinic3_mgmt.h"

void hinic3_flush_mgmt_workq(struct hinic3_hwdev *hwdev)
{
	if (hwdev->aeqs)
		flush_workqueue(hwdev->aeqs->workq);
}

void hinic3_mgmt_msg_aeqe_handler(struct hinic3_hwdev *hwdev, u8 *header,
				  u8 size)
{
	if (MBOX_MSG_HEADER_GET(*(__force __le64 *)header, SOURCE) ==
				MBOX_MSG_FROM_MBOX)
		hinic3_mbox_func_aeqe_handler(hwdev, header, size);
}
