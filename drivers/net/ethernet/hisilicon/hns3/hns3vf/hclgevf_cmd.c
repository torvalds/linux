// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include "hclgevf_cmd.h"
#include "hclgevf_main.h"
#include "hnae3.h"

/* hclgevf_cmd_send - send command to command queue
 * @hw: pointer to the hw struct
 * @desc: prefilled descriptor for describing the command
 * @num : the number of descriptors to be sent
 *
 * This is the main send command for command queue, it
 * sends the queue, cleans the queue, etc
 */
int hclgevf_cmd_send(struct hclgevf_hw *hw, struct hclge_desc *desc, int num)
{
	return hclge_comm_cmd_send(&hw->hw, desc, num, false);
}

int hclgevf_cmd_queue_init(struct hclgevf_dev *hdev)
{
	struct hclge_comm_cmq *cmdq = &hdev->hw.hw.cmq;
	int ret;

	/* Setup the lock for command queue */
	spin_lock_init(&cmdq->csq.lock);
	spin_lock_init(&cmdq->crq.lock);

	cmdq->csq.pdev = hdev->pdev;
	cmdq->crq.pdev = hdev->pdev;
	cmdq->tx_timeout = HCLGEVF_CMDQ_TX_TIMEOUT;
	cmdq->csq.desc_num = HCLGEVF_NIC_CMQ_DESC_NUM;
	cmdq->crq.desc_num = HCLGEVF_NIC_CMQ_DESC_NUM;

	ret = hclge_comm_alloc_cmd_queue(&hdev->hw.hw, HCLGE_COMM_TYPE_CSQ);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"CSQ ring setup error %d\n", ret);
		return ret;
	}

	ret = hclge_comm_alloc_cmd_queue(&hdev->hw.hw, HCLGE_COMM_TYPE_CRQ);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"CRQ ring setup error %d\n", ret);
		goto err_csq;
	}

	return 0;
err_csq:
	hclge_comm_free_cmd_desc(&cmdq->csq);
	return ret;
}

int hclgevf_cmd_init(struct hclgevf_dev *hdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);
	struct hclge_comm_cmq *cmdq = &hdev->hw.hw.cmq;
	int ret;

	spin_lock_bh(&cmdq->csq.lock);
	spin_lock(&cmdq->crq.lock);

	/* initialize the pointers of async rx queue of mailbox */
	hdev->arq.hdev = hdev;
	hdev->arq.head = 0;
	hdev->arq.tail = 0;
	atomic_set(&hdev->arq.count, 0);
	cmdq->csq.next_to_clean = 0;
	cmdq->csq.next_to_use = 0;
	cmdq->crq.next_to_clean = 0;
	cmdq->crq.next_to_use = 0;

	hclge_comm_cmd_init_regs(&hdev->hw.hw);

	spin_unlock(&cmdq->crq.lock);
	spin_unlock_bh(&cmdq->csq.lock);

	clear_bit(HCLGE_COMM_STATE_CMD_DISABLE, &hdev->hw.hw.comm_state);

	/* Check if there is new reset pending, because the higher level
	 * reset may happen when lower level reset is being processed.
	 */
	if (hclgevf_is_reset_pending(hdev)) {
		ret = -EBUSY;
		goto err_cmd_init;
	}

	/* get version and device capabilities */
	ret = hclge_comm_cmd_query_version_and_capability(hdev->ae_dev,
							  &hdev->hw.hw,
							  &hdev->fw_version,
							  false);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to query version and capabilities, ret = %d\n", ret);
		goto err_cmd_init;
	}

	dev_info(&hdev->pdev->dev, "The firmware version is %lu.%lu.%lu.%lu\n",
		 hnae3_get_field(hdev->fw_version, HNAE3_FW_VERSION_BYTE3_MASK,
				 HNAE3_FW_VERSION_BYTE3_SHIFT),
		 hnae3_get_field(hdev->fw_version, HNAE3_FW_VERSION_BYTE2_MASK,
				 HNAE3_FW_VERSION_BYTE2_SHIFT),
		 hnae3_get_field(hdev->fw_version, HNAE3_FW_VERSION_BYTE1_MASK,
				 HNAE3_FW_VERSION_BYTE1_SHIFT),
		 hnae3_get_field(hdev->fw_version, HNAE3_FW_VERSION_BYTE0_MASK,
				 HNAE3_FW_VERSION_BYTE0_SHIFT));

	if (ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V3) {
		/* ask the firmware to enable some features, driver can work
		 * without it.
		 */
		ret = hclge_comm_firmware_compat_config(hdev->ae_dev, false,
							&hdev->hw.hw, true);
		if (ret)
			dev_warn(&hdev->pdev->dev,
				 "Firmware compatible features not enabled(%d).\n",
				 ret);
	}

	return 0;

err_cmd_init:
	set_bit(HCLGE_COMM_STATE_CMD_DISABLE, &hdev->hw.hw.comm_state);

	return ret;
}

static void hclgevf_cmd_uninit_regs(struct hclgevf_hw *hw)
{
	hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_BASEADDR_L_REG, 0);
	hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_BASEADDR_H_REG, 0);
	hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_DEPTH_REG, 0);
	hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_HEAD_REG, 0);
	hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_TAIL_REG, 0);
	hclgevf_write_dev(hw, HCLGEVF_NIC_CRQ_BASEADDR_L_REG, 0);
	hclgevf_write_dev(hw, HCLGEVF_NIC_CRQ_BASEADDR_H_REG, 0);
	hclgevf_write_dev(hw, HCLGEVF_NIC_CRQ_DEPTH_REG, 0);
	hclgevf_write_dev(hw, HCLGEVF_NIC_CRQ_HEAD_REG, 0);
	hclgevf_write_dev(hw, HCLGEVF_NIC_CRQ_TAIL_REG, 0);
}

void hclgevf_cmd_uninit(struct hclgevf_dev *hdev)
{
	struct hclge_comm_cmq *cmdq = &hdev->hw.hw.cmq;
	hclge_comm_firmware_compat_config(hdev->ae_dev, false, &hdev->hw.hw,
					  false);
	set_bit(HCLGE_COMM_STATE_CMD_DISABLE, &hdev->hw.hw.comm_state);

	/* wait to ensure that the firmware completes the possible left
	 * over commands.
	 */
	msleep(HCLGEVF_CMDQ_CLEAR_WAIT_TIME);
	spin_lock_bh(&cmdq->csq.lock);
	spin_lock(&cmdq->crq.lock);
	hclgevf_cmd_uninit_regs(&hdev->hw);
	spin_unlock(&cmdq->crq.lock);
	spin_unlock_bh(&cmdq->csq.lock);

	hclge_comm_free_cmd_desc(&cmdq->csq);
	hclge_comm_free_cmd_desc(&cmdq->crq);
}
