// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/io.h>

#include "hinic3_common.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"

void hinic3_set_msix_state(struct hinic3_hwdev *hwdev, u16 msix_idx,
			   enum hinic3_msix_state flag)
{
	/* Completed by later submission due to LoC limit. */
}

u16 hinic3_global_func_id(struct hinic3_hwdev *hwdev)
{
	return hwdev->hwif->attr.func_global_idx;
}
