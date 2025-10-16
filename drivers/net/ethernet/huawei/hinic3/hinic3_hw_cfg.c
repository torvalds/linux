// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/device.h>

#include "hinic3_hw_cfg.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_mbox.h"

#define HINIC3_CFG_MAX_QP  256

static void hinic3_parse_pub_res_cap(struct hinic3_hwdev *hwdev,
				     struct hinic3_dev_cap *cap,
				     const struct cfg_cmd_dev_cap *dev_cap,
				     enum hinic3_func_type type)
{
	cap->port_id = dev_cap->port_id;
	cap->supp_svcs_bitmap = dev_cap->svc_cap_en;
}

static void hinic3_parse_l2nic_res_cap(struct hinic3_hwdev *hwdev,
				       struct hinic3_dev_cap *cap,
				       const struct cfg_cmd_dev_cap *dev_cap,
				       enum hinic3_func_type type)
{
	struct hinic3_nic_service_cap *nic_svc_cap = &cap->nic_svc_cap;

	nic_svc_cap->max_sqs = min(dev_cap->nic_max_sq_id + 1,
				   HINIC3_CFG_MAX_QP);
}

static void hinic3_parse_dev_cap(struct hinic3_hwdev *hwdev,
				 const struct cfg_cmd_dev_cap *dev_cap,
				 enum hinic3_func_type type)
{
	struct hinic3_dev_cap *cap = &hwdev->cfg_mgmt->cap;

	/* Public resource */
	hinic3_parse_pub_res_cap(hwdev, cap, dev_cap, type);

	/* L2 NIC resource */
	if (hinic3_support_nic(hwdev))
		hinic3_parse_l2nic_res_cap(hwdev, cap, dev_cap, type);
}

static int get_cap_from_fw(struct hinic3_hwdev *hwdev,
			   enum hinic3_func_type type)
{
	struct mgmt_msg_params msg_params = {};
	struct cfg_cmd_dev_cap dev_cap = {};
	int err;

	dev_cap.func_id = hinic3_global_func_id(hwdev);

	mgmt_msg_params_init_default(&msg_params, &dev_cap, sizeof(dev_cap));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_CFGM,
				       CFG_CMD_GET_DEV_CAP, &msg_params);
	if (err || dev_cap.head.status) {
		dev_err(hwdev->dev,
			"Failed to get capability from FW, err: %d, status: 0x%x\n",
			err, dev_cap.head.status);
		return -EIO;
	}

	hinic3_parse_dev_cap(hwdev, &dev_cap, type);

	return 0;
}

static int hinic3_init_irq_info(struct hinic3_hwdev *hwdev)
{
	struct hinic3_cfg_mgmt_info *cfg_mgmt = hwdev->cfg_mgmt;
	struct hinic3_hwif *hwif = hwdev->hwif;
	u16 intr_num = hwif->attr.num_irqs;
	struct hinic3_irq_info *irq_info;
	u16 intr_needed;

	intr_needed = hwif->attr.msix_flex_en ? (hwif->attr.num_aeqs +
		      hwif->attr.num_ceqs + hwif->attr.num_sq) : intr_num;
	if (intr_needed > intr_num) {
		dev_warn(hwdev->dev, "Irq num cfg %d is less than the needed irq num %d msix_flex_en %d\n",
			 intr_num, intr_needed, hwdev->hwif->attr.msix_flex_en);
		intr_needed = intr_num;
	}

	irq_info = &cfg_mgmt->irq_info;
	irq_info->irq = kcalloc(intr_num, sizeof(struct hinic3_irq),
				GFP_KERNEL);
	if (!irq_info->irq)
		return -ENOMEM;

	irq_info->num_irq_hw = intr_needed;
	mutex_init(&irq_info->irq_mutex);

	return 0;
}

static int hinic3_init_irq_alloc_info(struct hinic3_hwdev *hwdev)
{
	struct hinic3_cfg_mgmt_info *cfg_mgmt = hwdev->cfg_mgmt;
	struct hinic3_irq *irq = cfg_mgmt->irq_info.irq;
	u16 nreq = cfg_mgmt->irq_info.num_irq_hw;
	struct pci_dev *pdev = hwdev->pdev;
	int actual_irq;
	u16 i;

	actual_irq = pci_alloc_irq_vectors(pdev, 2, nreq, PCI_IRQ_MSIX);
	if (actual_irq < 0) {
		dev_err(hwdev->dev, "Alloc msix entries with threshold 2 failed. actual_irq: %d\n",
			actual_irq);
		return -ENOMEM;
	}

	nreq = actual_irq;
	cfg_mgmt->irq_info.num_irq = nreq;

	for (i = 0; i < nreq; ++i) {
		irq[i].msix_entry_idx = i;
		irq[i].irq_id = pci_irq_vector(pdev, i);
		irq[i].allocated = false;
	}

	return 0;
}

int hinic3_init_cfg_mgmt(struct hinic3_hwdev *hwdev)
{
	struct hinic3_cfg_mgmt_info *cfg_mgmt;
	int err;

	cfg_mgmt = kzalloc(sizeof(*cfg_mgmt), GFP_KERNEL);
	if (!cfg_mgmt)
		return -ENOMEM;

	hwdev->cfg_mgmt = cfg_mgmt;

	err = hinic3_init_irq_info(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init hinic3_irq_mgmt_info, err: %d\n",
			err);
		goto err_free_cfg_mgmt;
	}

	err = hinic3_init_irq_alloc_info(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init hinic3_irq_info, err: %d\n",
			err);
		goto err_free_irq_info;
	}

	return 0;

err_free_irq_info:
	kfree(cfg_mgmt->irq_info.irq);
	cfg_mgmt->irq_info.irq = NULL;
err_free_cfg_mgmt:
	kfree(cfg_mgmt);

	return err;
}

void hinic3_free_cfg_mgmt(struct hinic3_hwdev *hwdev)
{
	struct hinic3_cfg_mgmt_info *cfg_mgmt = hwdev->cfg_mgmt;

	pci_free_irq_vectors(hwdev->pdev);
	kfree(cfg_mgmt->irq_info.irq);
	cfg_mgmt->irq_info.irq = NULL;
	kfree(cfg_mgmt);
}

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

int hinic3_init_capability(struct hinic3_hwdev *hwdev)
{
	return get_cap_from_fw(hwdev, HINIC3_FUNC_TYPE_VF);
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
