// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include "hinic3_cmdq.h"
#include "hinic3_csr.h"
#include "hinic3_eqs.h"
#include "hinic3_hw_comm.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_mbox.h"
#include "hinic3_mgmt.h"

#define HINIC3_PCIE_SNOOP        0
#define HINIC3_PCIE_TPH_DISABLE  0

#define HINIC3_DMA_ATTR_INDIR_IDX_MASK          GENMASK(9, 0)
#define HINIC3_DMA_ATTR_INDIR_IDX_SET(val, member)  \
	FIELD_PREP(HINIC3_DMA_ATTR_INDIR_##member##_MASK, val)

#define HINIC3_DMA_ATTR_ENTRY_ST_MASK           GENMASK(7, 0)
#define HINIC3_DMA_ATTR_ENTRY_AT_MASK           GENMASK(9, 8)
#define HINIC3_DMA_ATTR_ENTRY_PH_MASK           GENMASK(11, 10)
#define HINIC3_DMA_ATTR_ENTRY_NO_SNOOPING_MASK  BIT(12)
#define HINIC3_DMA_ATTR_ENTRY_TPH_EN_MASK       BIT(13)
#define HINIC3_DMA_ATTR_ENTRY_SET(val, member)  \
	FIELD_PREP(HINIC3_DMA_ATTR_ENTRY_##member##_MASK, val)

#define HINIC3_PCIE_ST_DISABLE       0
#define HINIC3_PCIE_AT_DISABLE       0
#define HINIC3_PCIE_PH_DISABLE       0
#define HINIC3_PCIE_MSIX_ATTR_ENTRY  0

#define HINIC3_DEFAULT_EQ_MSIX_PENDING_LIMIT      0
#define HINIC3_DEFAULT_EQ_MSIX_COALESC_TIMER_CFG  0xFF
#define HINIC3_DEFAULT_EQ_MSIX_RESEND_TIMER_CFG   7

#define HINIC3_HWDEV_WQ_NAME    "hinic3_hardware"
#define HINIC3_WQ_MAX_REQ       10

enum hinic3_hwdev_init_state {
	HINIC3_HWDEV_MBOX_INITED = 2,
	HINIC3_HWDEV_CMDQ_INITED = 3,
};

static int hinic3_comm_aeqs_init(struct hinic3_hwdev *hwdev)
{
	struct msix_entry aeq_msix_entries[HINIC3_MAX_AEQS];
	u16 num_aeqs, resp_num_irq, i;
	int err;

	num_aeqs = hwdev->hwif->attr.num_aeqs;
	if (num_aeqs > HINIC3_MAX_AEQS) {
		dev_warn(hwdev->dev, "Adjust aeq num to %d\n",
			 HINIC3_MAX_AEQS);
		num_aeqs = HINIC3_MAX_AEQS;
	}
	err = hinic3_alloc_irqs(hwdev, num_aeqs, aeq_msix_entries,
				&resp_num_irq);
	if (err) {
		dev_err(hwdev->dev, "Failed to alloc aeq irqs, num_aeqs: %u\n",
			num_aeqs);
		return err;
	}

	if (resp_num_irq < num_aeqs) {
		dev_warn(hwdev->dev, "Adjust aeq num to %u\n",
			 resp_num_irq);
		num_aeqs = resp_num_irq;
	}

	err = hinic3_aeqs_init(hwdev, num_aeqs, aeq_msix_entries);
	if (err) {
		dev_err(hwdev->dev, "Failed to init aeqs\n");
		goto err_free_irqs;
	}

	return 0;

err_free_irqs:
	for (i = 0; i < num_aeqs; i++)
		hinic3_free_irq(hwdev, aeq_msix_entries[i].vector);

	return err;
}

static int hinic3_comm_ceqs_init(struct hinic3_hwdev *hwdev)
{
	struct msix_entry ceq_msix_entries[HINIC3_MAX_CEQS];
	u16 num_ceqs, resp_num_irq, i;
	int err;

	num_ceqs = hwdev->hwif->attr.num_ceqs;
	if (num_ceqs > HINIC3_MAX_CEQS) {
		dev_warn(hwdev->dev, "Adjust ceq num to %d\n",
			 HINIC3_MAX_CEQS);
		num_ceqs = HINIC3_MAX_CEQS;
	}

	err = hinic3_alloc_irqs(hwdev, num_ceqs, ceq_msix_entries,
				&resp_num_irq);
	if (err) {
		dev_err(hwdev->dev, "Failed to alloc ceq irqs, num_ceqs: %u\n",
			num_ceqs);
		return err;
	}

	if (resp_num_irq < num_ceqs) {
		dev_warn(hwdev->dev, "Adjust ceq num to %u\n",
			 resp_num_irq);
		num_ceqs = resp_num_irq;
	}

	err = hinic3_ceqs_init(hwdev, num_ceqs, ceq_msix_entries);
	if (err) {
		dev_err(hwdev->dev,
			"Failed to init ceqs, err:%d\n", err);
		goto err_free_irqs;
	}

	return 0;

err_free_irqs:
	for (i = 0; i < num_ceqs; i++)
		hinic3_free_irq(hwdev, ceq_msix_entries[i].vector);

	return err;
}

static int hinic3_comm_mbox_init(struct hinic3_hwdev *hwdev)
{
	int err;

	err = hinic3_init_mbox(hwdev);
	if (err)
		return err;

	hinic3_aeq_register_cb(hwdev, HINIC3_MBX_FROM_FUNC,
			       hinic3_mbox_func_aeqe_handler);
	hinic3_aeq_register_cb(hwdev, HINIC3_MSG_FROM_FW,
			       hinic3_mgmt_msg_aeqe_handler);

	set_bit(HINIC3_HWDEV_MBOX_INITED, &hwdev->func_state);

	return 0;
}

static void hinic3_comm_mbox_free(struct hinic3_hwdev *hwdev)
{
	spin_lock_bh(&hwdev->channel_lock);
	clear_bit(HINIC3_HWDEV_MBOX_INITED, &hwdev->func_state);
	spin_unlock_bh(&hwdev->channel_lock);
	hinic3_aeq_unregister_cb(hwdev, HINIC3_MBX_FROM_FUNC);
	hinic3_aeq_unregister_cb(hwdev, HINIC3_MSG_FROM_FW);
	hinic3_free_mbox(hwdev);
}

static int init_aeqs_msix_attr(struct hinic3_hwdev *hwdev)
{
	struct hinic3_aeqs *aeqs = hwdev->aeqs;
	struct hinic3_interrupt_info info = {};
	struct hinic3_eq *eq;
	u16 q_id;
	int err;

	info.interrupt_coalesc_set = 1;
	info.pending_limit = HINIC3_DEFAULT_EQ_MSIX_PENDING_LIMIT;
	info.coalesc_timer_cfg = HINIC3_DEFAULT_EQ_MSIX_COALESC_TIMER_CFG;
	info.resend_timer_cfg = HINIC3_DEFAULT_EQ_MSIX_RESEND_TIMER_CFG;

	for (q_id = 0; q_id < aeqs->num_aeqs; q_id++) {
		eq = &aeqs->aeq[q_id];
		info.msix_index = eq->msix_entry_idx;
		err = hinic3_set_interrupt_cfg_direct(hwdev, &info);
		if (err) {
			dev_err(hwdev->dev, "Set msix attr for aeq %d failed\n",
				q_id);
			return err;
		}
	}

	return 0;
}

static int init_ceqs_msix_attr(struct hinic3_hwdev *hwdev)
{
	struct hinic3_ceqs *ceqs = hwdev->ceqs;
	struct hinic3_interrupt_info info = {};
	struct hinic3_eq *eq;
	u16 q_id;
	int err;

	info.interrupt_coalesc_set = 1;
	info.pending_limit = HINIC3_DEFAULT_EQ_MSIX_PENDING_LIMIT;
	info.coalesc_timer_cfg = HINIC3_DEFAULT_EQ_MSIX_COALESC_TIMER_CFG;
	info.resend_timer_cfg = HINIC3_DEFAULT_EQ_MSIX_RESEND_TIMER_CFG;

	for (q_id = 0; q_id < ceqs->num_ceqs; q_id++) {
		eq = &ceqs->ceq[q_id];
		info.msix_index = eq->msix_entry_idx;
		err = hinic3_set_interrupt_cfg_direct(hwdev, &info);
		if (err) {
			dev_err(hwdev->dev, "Set msix attr for ceq %u failed\n",
				q_id);
			return err;
		}
	}

	return 0;
}

static int init_basic_mgmt_channel(struct hinic3_hwdev *hwdev)
{
	int err;

	err = hinic3_comm_aeqs_init(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init async event queues\n");
		return err;
	}

	err = hinic3_comm_mbox_init(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init mailbox\n");
		goto err_free_comm_aeqs;
	}

	err = init_aeqs_msix_attr(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init aeqs msix attr\n");
		goto err_free_comm_mbox;
	}

	return 0;

err_free_comm_mbox:
	hinic3_comm_mbox_free(hwdev);
err_free_comm_aeqs:
	hinic3_aeqs_free(hwdev);

	return err;
}

static void free_base_mgmt_channel(struct hinic3_hwdev *hwdev)
{
	hinic3_comm_mbox_free(hwdev);
	hinic3_aeqs_free(hwdev);
}

static int dma_attr_table_init(struct hinic3_hwdev *hwdev)
{
	u32 addr, val, dst_attr;

	/* Indirect access, set entry_idx first */
	addr = HINIC3_CSR_DMA_ATTR_INDIR_IDX_ADDR;
	val = hinic3_hwif_read_reg(hwdev->hwif, addr);
	val &= ~HINIC3_DMA_ATTR_ENTRY_AT_MASK;
	val |= HINIC3_DMA_ATTR_INDIR_IDX_SET(HINIC3_PCIE_MSIX_ATTR_ENTRY, IDX);
	hinic3_hwif_write_reg(hwdev->hwif, addr, val);

	addr = HINIC3_CSR_DMA_ATTR_TBL_ADDR;
	val = hinic3_hwif_read_reg(hwdev->hwif, addr);

	dst_attr = HINIC3_DMA_ATTR_ENTRY_SET(HINIC3_PCIE_ST_DISABLE, ST) |
		   HINIC3_DMA_ATTR_ENTRY_SET(HINIC3_PCIE_AT_DISABLE, AT) |
		   HINIC3_DMA_ATTR_ENTRY_SET(HINIC3_PCIE_PH_DISABLE, PH) |
		   HINIC3_DMA_ATTR_ENTRY_SET(HINIC3_PCIE_SNOOP, NO_SNOOPING) |
		   HINIC3_DMA_ATTR_ENTRY_SET(HINIC3_PCIE_TPH_DISABLE, TPH_EN);
	if (val == dst_attr)
		return 0;

	return hinic3_set_dma_attr_tbl(hwdev,
				       HINIC3_PCIE_MSIX_ATTR_ENTRY,
				       HINIC3_PCIE_ST_DISABLE,
				       HINIC3_PCIE_AT_DISABLE,
				       HINIC3_PCIE_PH_DISABLE,
				       HINIC3_PCIE_SNOOP,
				       HINIC3_PCIE_TPH_DISABLE);
}

static int init_basic_attributes(struct hinic3_hwdev *hwdev)
{
	struct comm_global_attr glb_attr;
	int err;

	err = hinic3_func_reset(hwdev, hinic3_global_func_id(hwdev),
				COMM_FUNC_RESET_FLAG);
	if (err)
		return err;

	err = hinic3_get_comm_features(hwdev, hwdev->features,
				       COMM_MAX_FEATURE_QWORD);
	if (err)
		return err;

	dev_dbg(hwdev->dev, "Comm hw features: 0x%llx\n", hwdev->features[0]);

	err = hinic3_get_global_attr(hwdev, &glb_attr);
	if (err)
		return err;

	err = hinic3_set_func_svc_used_state(hwdev, COMM_FUNC_SVC_T_COMM, 1);
	if (err)
		return err;

	err = dma_attr_table_init(hwdev);
	if (err)
		return err;

	hwdev->max_cmdq = min(glb_attr.cmdq_num, HINIC3_MAX_CMDQ_TYPES);
	dev_dbg(hwdev->dev,
		"global attribute: max_host: 0x%x, max_pf: 0x%x, vf_id_start: 0x%x, mgmt node id: 0x%x, cmdq_num: 0x%x\n",
		glb_attr.max_host_num, glb_attr.max_pf_num,
		glb_attr.vf_id_start, glb_attr.mgmt_host_node_id,
		glb_attr.cmdq_num);

	return 0;
}

static int hinic3_comm_cmdqs_init(struct hinic3_hwdev *hwdev)
{
	int err;

	err = hinic3_cmdqs_init(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init cmd queues\n");
		return err;
	}

	hinic3_ceq_register_cb(hwdev, HINIC3_CMDQ, hinic3_cmdq_ceq_handler);

	err = hinic3_set_cmdq_depth(hwdev, CMDQ_DEPTH);
	if (err) {
		dev_err(hwdev->dev, "Failed to set cmdq depth\n");
		goto err_free_cmdqs;
	}

	set_bit(HINIC3_HWDEV_CMDQ_INITED, &hwdev->func_state);

	return 0;

err_free_cmdqs:
	hinic3_cmdqs_free(hwdev);

	return err;
}

static void hinic3_comm_cmdqs_free(struct hinic3_hwdev *hwdev)
{
	spin_lock_bh(&hwdev->channel_lock);
	clear_bit(HINIC3_HWDEV_CMDQ_INITED, &hwdev->func_state);
	spin_unlock_bh(&hwdev->channel_lock);

	hinic3_ceq_unregister_cb(hwdev, HINIC3_CMDQ);
	hinic3_cmdqs_free(hwdev);
}

static int init_cmdqs_channel(struct hinic3_hwdev *hwdev)
{
	int err;

	err = hinic3_comm_ceqs_init(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init completion event queues\n");
		return err;
	}

	err = init_ceqs_msix_attr(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init ceqs msix attr\n");
		goto err_free_ceqs;
	}

	hwdev->wq_page_size = HINIC3_MIN_PAGE_SIZE << HINIC3_WQ_PAGE_SIZE_ORDER;
	err = hinic3_set_wq_page_size(hwdev, hinic3_global_func_id(hwdev),
				      hwdev->wq_page_size);
	if (err) {
		dev_err(hwdev->dev, "Failed to set wq page size\n");
		goto err_free_ceqs;
	}

	err = hinic3_comm_cmdqs_init(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init cmd queues\n");
		goto err_reset_wq_page_size;
	}

	return 0;

err_reset_wq_page_size:
	hinic3_set_wq_page_size(hwdev, hinic3_global_func_id(hwdev),
				HINIC3_MIN_PAGE_SIZE);
err_free_ceqs:
	hinic3_ceqs_free(hwdev);

	return err;
}

static void hinic3_free_cmdqs_channel(struct hinic3_hwdev *hwdev)
{
	hinic3_comm_cmdqs_free(hwdev);
	hinic3_ceqs_free(hwdev);
}

static int hinic3_init_comm_ch(struct hinic3_hwdev *hwdev)
{
	int err;

	err = init_basic_mgmt_channel(hwdev);
	if (err)
		return err;

	err = init_basic_attributes(hwdev);
	if (err)
		goto err_free_basic_mgmt_ch;

	err = init_cmdqs_channel(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init cmdq channel\n");
		goto err_clear_func_svc_used_state;
	}

	return 0;

err_clear_func_svc_used_state:
	hinic3_set_func_svc_used_state(hwdev, COMM_FUNC_SVC_T_COMM, 0);
err_free_basic_mgmt_ch:
	free_base_mgmt_channel(hwdev);

	return err;
}

static void hinic3_uninit_comm_ch(struct hinic3_hwdev *hwdev)
{
	hinic3_free_cmdqs_channel(hwdev);
	hinic3_set_func_svc_used_state(hwdev, COMM_FUNC_SVC_T_COMM, 0);
	free_base_mgmt_channel(hwdev);
}

static DEFINE_IDA(hinic3_adev_ida);

static int hinic3_adev_idx_alloc(void)
{
	return ida_alloc(&hinic3_adev_ida, GFP_KERNEL);
}

static void hinic3_adev_idx_free(int id)
{
	ida_free(&hinic3_adev_ida, id);
}

int hinic3_init_hwdev(struct pci_dev *pdev)
{
	struct hinic3_pcidev *pci_adapter = pci_get_drvdata(pdev);
	struct hinic3_hwdev *hwdev;
	int err;

	hwdev = kzalloc(sizeof(*hwdev), GFP_KERNEL);
	if (!hwdev)
		return -ENOMEM;

	pci_adapter->hwdev = hwdev;
	hwdev->adapter = pci_adapter;
	hwdev->pdev = pci_adapter->pdev;
	hwdev->dev = &pci_adapter->pdev->dev;
	hwdev->func_state = 0;
	hwdev->dev_id = hinic3_adev_idx_alloc();
	spin_lock_init(&hwdev->channel_lock);

	err = hinic3_init_hwif(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init hwif\n");
		goto err_free_hwdev;
	}

	hwdev->workq = alloc_workqueue(HINIC3_HWDEV_WQ_NAME, WQ_MEM_RECLAIM,
				       HINIC3_WQ_MAX_REQ);
	if (!hwdev->workq) {
		dev_err(hwdev->dev, "Failed to alloc hardware workq\n");
		err = -ENOMEM;
		goto err_free_hwif;
	}

	err = hinic3_init_cfg_mgmt(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init config mgmt\n");
		goto err_destroy_workqueue;
	}

	err = hinic3_init_comm_ch(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init communication channel\n");
		goto err_free_cfg_mgmt;
	}

	err = hinic3_init_capability(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init capability\n");
		goto err_uninit_comm_ch;
	}

	err = hinic3_set_comm_features(hwdev, hwdev->features,
				       COMM_MAX_FEATURE_QWORD);
	if (err) {
		dev_err(hwdev->dev, "Failed to set comm features\n");
		goto err_uninit_comm_ch;
	}

	return 0;

err_uninit_comm_ch:
	hinic3_uninit_comm_ch(hwdev);
err_free_cfg_mgmt:
	hinic3_free_cfg_mgmt(hwdev);
err_destroy_workqueue:
	destroy_workqueue(hwdev->workq);
err_free_hwif:
	hinic3_free_hwif(hwdev);
err_free_hwdev:
	pci_adapter->hwdev = NULL;
	hinic3_adev_idx_free(hwdev->dev_id);
	kfree(hwdev);

	return err;
}

void hinic3_free_hwdev(struct hinic3_hwdev *hwdev)
{
	u64 drv_features[COMM_MAX_FEATURE_QWORD] = {};

	hinic3_set_comm_features(hwdev, drv_features, COMM_MAX_FEATURE_QWORD);
	hinic3_func_rx_tx_flush(hwdev);
	hinic3_uninit_comm_ch(hwdev);
	hinic3_free_cfg_mgmt(hwdev);
	destroy_workqueue(hwdev->workq);
	hinic3_free_hwif(hwdev);
	hinic3_adev_idx_free(hwdev->dev_id);
	kfree(hwdev);
}

void hinic3_set_api_stop(struct hinic3_hwdev *hwdev)
{
	struct hinic3_mbox *mbox;

	spin_lock_bh(&hwdev->channel_lock);
	if (test_bit(HINIC3_HWDEV_MBOX_INITED, &hwdev->func_state)) {
		mbox = hwdev->mbox;
		spin_lock(&mbox->mbox_lock);
		if (mbox->event_flag == MBOX_EVENT_START)
			mbox->event_flag = MBOX_EVENT_TIMEOUT;
		spin_unlock(&mbox->mbox_lock);
	}

	if (test_bit(HINIC3_HWDEV_CMDQ_INITED, &hwdev->func_state))
		hinic3_cmdq_flush_sync_cmd(hwdev);

	spin_unlock_bh(&hwdev->channel_lock);
}
