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

#define hclgevf_is_csq(ring) ((ring)->flag & HCLGEVF_TYPE_CSQ)
#define hclgevf_ring_to_dma_dir(ring) (hclgevf_is_csq(ring) ? \
					DMA_TO_DEVICE : DMA_FROM_DEVICE)
#define cmq_ring_to_dev(ring)   (&(ring)->dev->pdev->dev)

static int hclgevf_ring_space(struct hclgevf_cmq_ring *ring)
{
	int ntc = ring->next_to_clean;
	int ntu = ring->next_to_use;
	int used;

	used = (ntu - ntc + ring->desc_num) % ring->desc_num;

	return ring->desc_num - used - 1;
}

static int hclgevf_cmd_csq_clean(struct hclgevf_hw *hw)
{
	struct hclgevf_cmq_ring *csq = &hw->cmq.csq;
	u16 ntc = csq->next_to_clean;
	struct hclgevf_desc *desc;
	int clean = 0;
	u32 head;

	desc = &csq->desc[ntc];
	head = hclgevf_read_dev(hw, HCLGEVF_NIC_CSQ_HEAD_REG);
	while (head != ntc) {
		memset(desc, 0, sizeof(*desc));
		ntc++;
		if (ntc == csq->desc_num)
			ntc = 0;
		desc = &csq->desc[ntc];
		clean++;
	}
	csq->next_to_clean = ntc;

	return clean;
}

static bool hclgevf_cmd_csq_done(struct hclgevf_hw *hw)
{
	u32 head;

	head = hclgevf_read_dev(hw, HCLGEVF_NIC_CSQ_HEAD_REG);

	return head == hw->cmq.csq.next_to_use;
}

static bool hclgevf_is_special_opcode(u16 opcode)
{
	u16 spec_opcode[] = {0x30, 0x31, 0x32};
	int i;

	for (i = 0; i < ARRAY_SIZE(spec_opcode); i++) {
		if (spec_opcode[i] == opcode)
			return true;
	}

	return false;
}

static int hclgevf_alloc_cmd_desc(struct hclgevf_cmq_ring *ring)
{
	int size = ring->desc_num * sizeof(struct hclgevf_desc);

	ring->desc = kzalloc(size, GFP_KERNEL);
	if (!ring->desc)
		return -ENOMEM;

	ring->desc_dma_addr = dma_map_single(cmq_ring_to_dev(ring), ring->desc,
					     size, DMA_BIDIRECTIONAL);

	if (dma_mapping_error(cmq_ring_to_dev(ring), ring->desc_dma_addr)) {
		ring->desc_dma_addr = 0;
		kfree(ring->desc);
		ring->desc = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void hclgevf_free_cmd_desc(struct hclgevf_cmq_ring *ring)
{
	dma_unmap_single(cmq_ring_to_dev(ring), ring->desc_dma_addr,
			 ring->desc_num * sizeof(ring->desc[0]),
			 hclgevf_ring_to_dma_dir(ring));

	ring->desc_dma_addr = 0;
	kfree(ring->desc);
	ring->desc = NULL;
}

static int hclgevf_init_cmd_queue(struct hclgevf_dev *hdev,
				  struct hclgevf_cmq_ring *ring)
{
	struct hclgevf_hw *hw = &hdev->hw;
	int ring_type = ring->flag;
	u32 reg_val;
	int ret;

	ring->desc_num = HCLGEVF_NIC_CMQ_DESC_NUM;
	spin_lock_init(&ring->lock);
	ring->next_to_clean = 0;
	ring->next_to_use = 0;
	ring->dev = hdev;

	/* allocate CSQ/CRQ descriptor */
	ret = hclgevf_alloc_cmd_desc(ring);
	if (ret) {
		dev_err(&hdev->pdev->dev, "failed(%d) to alloc %s desc\n", ret,
			(ring_type == HCLGEVF_TYPE_CSQ) ? "CSQ" : "CRQ");
		return ret;
	}

	/* initialize the hardware registers with csq/crq dma-address,
	 * descriptor number, head & tail pointers
	 */
	switch (ring_type) {
	case HCLGEVF_TYPE_CSQ:
		reg_val = (u32)ring->desc_dma_addr;
		hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_BASEADDR_L_REG, reg_val);
		reg_val = (u32)((ring->desc_dma_addr >> 31) >> 1);
		hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_BASEADDR_H_REG, reg_val);

		reg_val = (ring->desc_num >> HCLGEVF_NIC_CMQ_DESC_NUM_S);
		reg_val |= HCLGEVF_NIC_CMQ_ENABLE;
		hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_DEPTH_REG, reg_val);

		hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_TAIL_REG, 0);
		hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_HEAD_REG, 0);
		break;
	case HCLGEVF_TYPE_CRQ:
		reg_val = (u32)ring->desc_dma_addr;
		hclgevf_write_dev(hw, HCLGEVF_NIC_CRQ_BASEADDR_L_REG, reg_val);
		reg_val = (u32)((ring->desc_dma_addr >> 31) >> 1);
		hclgevf_write_dev(hw, HCLGEVF_NIC_CRQ_BASEADDR_H_REG, reg_val);

		reg_val = (ring->desc_num >> HCLGEVF_NIC_CMQ_DESC_NUM_S);
		reg_val |= HCLGEVF_NIC_CMQ_ENABLE;
		hclgevf_write_dev(hw, HCLGEVF_NIC_CRQ_DEPTH_REG, reg_val);

		hclgevf_write_dev(hw, HCLGEVF_NIC_CRQ_TAIL_REG, 0);
		hclgevf_write_dev(hw, HCLGEVF_NIC_CRQ_HEAD_REG, 0);
		break;
	}

	return 0;
}

void hclgevf_cmd_setup_basic_desc(struct hclgevf_desc *desc,
				  enum hclgevf_opcode_type opcode, bool is_read)
{
	memset(desc, 0, sizeof(struct hclgevf_desc));
	desc->opcode = cpu_to_le16(opcode);
	desc->flag = cpu_to_le16(HCLGEVF_CMD_FLAG_NO_INTR |
				 HCLGEVF_CMD_FLAG_IN);
	if (is_read)
		desc->flag |= cpu_to_le16(HCLGEVF_CMD_FLAG_WR);
	else
		desc->flag &= cpu_to_le16(~HCLGEVF_CMD_FLAG_WR);
}

/* hclgevf_cmd_send - send command to command queue
 * @hw: pointer to the hw struct
 * @desc: prefilled descriptor for describing the command
 * @num : the number of descriptors to be sent
 *
 * This is the main send command for command queue, it
 * sends the queue, cleans the queue, etc
 */
int hclgevf_cmd_send(struct hclgevf_hw *hw, struct hclgevf_desc *desc, int num)
{
	struct hclgevf_dev *hdev = (struct hclgevf_dev *)hw->hdev;
	struct hclgevf_desc *desc_to_use;
	bool complete = false;
	u32 timeout = 0;
	int handle = 0;
	int status = 0;
	u16 retval;
	u16 opcode;
	int ntc;

	spin_lock_bh(&hw->cmq.csq.lock);

	if (num > hclgevf_ring_space(&hw->cmq.csq)) {
		spin_unlock_bh(&hw->cmq.csq.lock);
		return -EBUSY;
	}

	/* Record the location of desc in the ring for this time
	 * which will be use for hardware to write back
	 */
	ntc = hw->cmq.csq.next_to_use;
	opcode = le16_to_cpu(desc[0].opcode);
	while (handle < num) {
		desc_to_use = &hw->cmq.csq.desc[hw->cmq.csq.next_to_use];
		*desc_to_use = desc[handle];
		(hw->cmq.csq.next_to_use)++;
		if (hw->cmq.csq.next_to_use == hw->cmq.csq.desc_num)
			hw->cmq.csq.next_to_use = 0;
		handle++;
	}

	/* Write to hardware */
	hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_TAIL_REG,
			  hw->cmq.csq.next_to_use);

	/* If the command is sync, wait for the firmware to write back,
	 * if multi descriptors to be sent, use the first one to check
	 */
	if (HCLGEVF_SEND_SYNC(le16_to_cpu(desc->flag))) {
		do {
			if (hclgevf_cmd_csq_done(hw))
				break;
			udelay(1);
			timeout++;
		} while (timeout < hw->cmq.tx_timeout);
	}

	if (hclgevf_cmd_csq_done(hw)) {
		complete = true;
		handle = 0;

		while (handle < num) {
			/* Get the result of hardware write back */
			desc_to_use = &hw->cmq.csq.desc[ntc];
			desc[handle] = *desc_to_use;

			if (likely(!hclgevf_is_special_opcode(opcode)))
				retval = le16_to_cpu(desc[handle].retval);
			else
				retval = le16_to_cpu(desc[0].retval);

			if ((enum hclgevf_cmd_return_status)retval ==
			    HCLGEVF_CMD_EXEC_SUCCESS)
				status = 0;
			else
				status = -EIO;
			hw->cmq.last_status = (enum hclgevf_cmd_status)retval;
			ntc++;
			handle++;
			if (ntc == hw->cmq.csq.desc_num)
				ntc = 0;
		}
	}

	if (!complete)
		status = -EAGAIN;

	/* Clean the command send queue */
	handle = hclgevf_cmd_csq_clean(hw);
	if (handle != num) {
		dev_warn(&hdev->pdev->dev,
			 "cleaned %d, need to clean %d\n", handle, num);
	}

	spin_unlock_bh(&hw->cmq.csq.lock);

	return status;
}

static int  hclgevf_cmd_query_firmware_version(struct hclgevf_hw *hw,
					       u32 *version)
{
	struct hclgevf_query_version_cmd *resp;
	struct hclgevf_desc desc;
	int status;

	resp = (struct hclgevf_query_version_cmd *)desc.data;

	hclgevf_cmd_setup_basic_desc(&desc, HCLGEVF_OPC_QUERY_FW_VER, 1);
	status = hclgevf_cmd_send(hw, &desc, 1);
	if (!status)
		*version = le32_to_cpu(resp->firmware);

	return status;
}

int hclgevf_cmd_init(struct hclgevf_dev *hdev)
{
	u32 version;
	int ret;

	/* setup Tx write back timeout */
	hdev->hw.cmq.tx_timeout = HCLGEVF_CMDQ_TX_TIMEOUT;

	/* setup queue CSQ/CRQ rings */
	hdev->hw.cmq.csq.flag = HCLGEVF_TYPE_CSQ;
	ret = hclgevf_init_cmd_queue(hdev, &hdev->hw.cmq.csq);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed(%d) to initialize CSQ ring\n", ret);
		return ret;
	}

	hdev->hw.cmq.crq.flag = HCLGEVF_TYPE_CRQ;
	ret = hclgevf_init_cmd_queue(hdev, &hdev->hw.cmq.crq);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed(%d) to initialize CRQ ring\n", ret);
		goto err_csq;
	}

	/* get firmware version */
	ret = hclgevf_cmd_query_firmware_version(&hdev->hw, &version);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed(%d) to query firmware version\n", ret);
		goto err_crq;
	}
	hdev->fw_version = version;

	dev_info(&hdev->pdev->dev, "The firmware version is %08x\n", version);

	return 0;
err_crq:
	hclgevf_free_cmd_desc(&hdev->hw.cmq.crq);
err_csq:
	hclgevf_free_cmd_desc(&hdev->hw.cmq.csq);

	return ret;
}

void hclgevf_cmd_uninit(struct hclgevf_dev *hdev)
{
	hclgevf_free_cmd_desc(&hdev->hw.cmq.csq);
	hclgevf_free_cmd_desc(&hdev->hw.cmq.crq);
}
