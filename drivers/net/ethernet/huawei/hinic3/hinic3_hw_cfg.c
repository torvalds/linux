// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/device.h>

#include "hinic3_hw_cfg.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_mbox.h"

int hinic3_alloc_irqs(struct hinic3_hwdev *hwdev, u16 num,
		      struct msix_entry *alloc_arr, u16 *act_num)
{
	struct hinic3_irq_info *irq_info;
	struct hinic3_irq *curr;
	u16 i, found = 0;

	irq_info = &hwdev->cfg_mgmt->irq_info;
	mutex_lock(&irq_info->irq_mutex);
	for (i = 0; i < irq_info->num_irq && found < num; i++) {
		curr = irq_info->irq + i;
		if (curr->allocated)
			continue;
		curr->allocated = true;
		alloc_arr[found].vector = curr->irq_id;
		alloc_arr[found].entry = curr->msix_entry_idx;
		found++;
	}
	mutex_unlock(&irq_info->irq_mutex);

	*act_num = found;

	return found == 0 ? -ENOMEM : 0;
}

void hinic3_free_irq(struct hinic3_hwdev *hwdev, u32 irq_id)
{
	struct hinic3_irq_info *irq_info;
	struct hinic3_irq *curr;
	u16 i;

	irq_info = &hwdev->cfg_mgmt->irq_info;
	mutex_lock(&irq_info->irq_mutex);
	for (i = 0; i < irq_info->num_irq; i++) {
		curr = irq_info->irq + i;
		if (curr->irq_id == irq_id) {
			curr->allocated = false;
			break;
		}
	}
	mutex_unlock(&irq_info->irq_mutex);
}

bool hinic3_support_nic(struct hinic3_hwdev *hwdev)
{
	return hwdev->cfg_mgmt->cap.supp_svcs_bitmap &
	       BIT(HINIC3_SERVICE_T_NIC);
}

u16 hinic3_func_max_qnum(struct hinic3_hwdev *hwdev)
{
	return hwdev->cfg_mgmt->cap.nic_svc_cap.max_sqs;
}

u8 hinic3_physical_port_id(struct hinic3_hwdev *hwdev)
{
	return hwdev->cfg_mgmt->cap.port_id;
}
