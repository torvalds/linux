/*
 * Copyright (c) 2016~2017 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/dma-direction.h>
#include "hclge_cmd.h"
#include "hnae3.h"
#include "hclge_main.h"

#define hclge_is_csq(ring) ((ring)->flag & HCLGE_TYPE_CSQ)
#define hclge_ring_to_dma_dir(ring) (hclge_is_csq(ring) ? \
	DMA_TO_DEVICE : DMA_FROM_DEVICE)
#define cmq_ring_to_dev(ring)   (&(ring)->dev->pdev->dev)

static int hclge_ring_space(struct hclge_cmq_ring *ring)
{
	int ntu = ring->next_to_use;
	int ntc = ring->next_to_clean;
	int used = (ntu - ntc + ring->desc_num) % ring->desc_num;

	return ring->desc_num - used - 1;
}

static int is_valid_csq_clean_head(struct hclge_cmq_ring *ring, int h)
{
	int u = ring->next_to_use;
	int c = ring->next_to_clean;

	if (unlikely(h >= ring->desc_num))
		return 0;

	return u > c ? (h > c && h <= u) : (h > c || h <= u);
}

static int hclge_alloc_cmd_desc(struct hclge_cmq_ring *ring)
{
	int size  = ring->desc_num * sizeof(struct hclge_desc);

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

static void hclge_free_cmd_desc(struct hclge_cmq_ring *ring)
{
	dma_unmap_single(cmq_ring_to_dev(ring), ring->desc_dma_addr,
			 ring->desc_num * sizeof(ring->desc[0]),
			 DMA_BIDIRECTIONAL);

	ring->desc_dma_addr = 0;
	kfree(ring->desc);
	ring->desc = NULL;
}

static int hclge_alloc_cmd_queue(struct hclge_dev *hdev, int ring_type)
{
	struct hclge_hw *hw = &hdev->hw;
	struct hclge_cmq_ring *ring =
		(ring_type == HCLGE_TYPE_CSQ) ? &hw->cmq.csq : &hw->cmq.crq;
	int ret;

	ring->flag = ring_type;
	ring->dev = hdev;

	ret = hclge_alloc_cmd_desc(ring);
	if (ret) {
		dev_err(&hdev->pdev->dev, "descriptor %s alloc error %d\n",
			(ring_type == HCLGE_TYPE_CSQ) ? "CSQ" : "CRQ", ret);
		return ret;
	}

	return 0;
}

void hclge_cmd_reuse_desc(struct hclge_desc *desc, bool is_read)
{
	desc->flag = cpu_to_le16(HCLGE_CMD_FLAG_NO_INTR | HCLGE_CMD_FLAG_IN);
	if (is_read)
		desc->flag |= cpu_to_le16(HCLGE_CMD_FLAG_WR);
	else
		desc->flag &= cpu_to_le16(~HCLGE_CMD_FLAG_WR);
}

void hclge_cmd_setup_basic_desc(struct hclge_desc *desc,
				enum hclge_opcode_type opcode, bool is_read)
{
	memset((void *)desc, 0, sizeof(struct hclge_desc));
	desc->opcode = cpu_to_le16(opcode);
	desc->flag = cpu_to_le16(HCLGE_CMD_FLAG_NO_INTR | HCLGE_CMD_FLAG_IN);

	if (is_read)
		desc->flag |= cpu_to_le16(HCLGE_CMD_FLAG_WR);
	else
		desc->flag &= cpu_to_le16(~HCLGE_CMD_FLAG_WR);
}

static void hclge_cmd_config_regs(struct hclge_cmq_ring *ring)
{
	dma_addr_t dma = ring->desc_dma_addr;
	struct hclge_dev *hdev = ring->dev;
	struct hclge_hw *hw = &hdev->hw;

	if (ring->flag == HCLGE_TYPE_CSQ) {
		hclge_write_dev(hw, HCLGE_NIC_CSQ_BASEADDR_L_REG,
				(u32)dma);
		hclge_write_dev(hw, HCLGE_NIC_CSQ_BASEADDR_H_REG,
				(u32)((dma >> 31) >> 1));
		hclge_write_dev(hw, HCLGE_NIC_CSQ_DEPTH_REG,
				(ring->desc_num >> HCLGE_NIC_CMQ_DESC_NUM_S) |
				HCLGE_NIC_CMQ_ENABLE);
		hclge_write_dev(hw, HCLGE_NIC_CSQ_TAIL_REG, 0);
		hclge_write_dev(hw, HCLGE_NIC_CSQ_HEAD_REG, 0);
	} else {
		hclge_write_dev(hw, HCLGE_NIC_CRQ_BASEADDR_L_REG,
				(u32)dma);
		hclge_write_dev(hw, HCLGE_NIC_CRQ_BASEADDR_H_REG,
				(u32)((dma >> 31) >> 1));
		hclge_write_dev(hw, HCLGE_NIC_CRQ_DEPTH_REG,
				(ring->desc_num >> HCLGE_NIC_CMQ_DESC_NUM_S) |
				HCLGE_NIC_CMQ_ENABLE);
		hclge_write_dev(hw, HCLGE_NIC_CRQ_TAIL_REG, 0);
		hclge_write_dev(hw, HCLGE_NIC_CRQ_HEAD_REG, 0);
	}
}

static void hclge_cmd_init_regs(struct hclge_hw *hw)
{
	hclge_cmd_config_regs(&hw->cmq.csq);
	hclge_cmd_config_regs(&hw->cmq.crq);
}

static int hclge_cmd_csq_clean(struct hclge_hw *hw)
{
	struct hclge_dev *hdev = (struct hclge_dev *)hw->back;
	struct hclge_cmq_ring *csq = &hw->cmq.csq;
	u16 ntc = csq->next_to_clean;
	struct hclge_desc *desc;
	int clean = 0;
	u32 head;

	desc = &csq->desc[ntc];
	head = hclge_read_dev(hw, HCLGE_NIC_CSQ_HEAD_REG);
	rmb(); /* Make sure head is ready before touch any data */

	if (!is_valid_csq_clean_head(csq, head)) {
		dev_warn(&hdev->pdev->dev, "wrong head (%d, %d-%d)\n", head,
			   csq->next_to_use, csq->next_to_clean);
		return 0;
	}

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

static int hclge_cmd_csq_done(struct hclge_hw *hw)
{
	u32 head = hclge_read_dev(hw, HCLGE_NIC_CSQ_HEAD_REG);
	return head == hw->cmq.csq.next_to_use;
}

static bool hclge_is_special_opcode(u16 opcode)
{
	/* these commands have several descriptors,
	 * and use the first one to save opcode and return value
	 */
	u16 spec_opcode[3] = {HCLGE_OPC_STATS_64_BIT,
		HCLGE_OPC_STATS_32_BIT, HCLGE_OPC_STATS_MAC};
	int i;

	for (i = 0; i < ARRAY_SIZE(spec_opcode); i++) {
		if (spec_opcode[i] == opcode)
			return true;
	}

	return false;
}

/**
 * hclge_cmd_send - send command to command queue
 * @hw: pointer to the hw struct
 * @desc: prefilled descriptor for describing the command
 * @num : the number of descriptors to be sent
 *
 * This is the main send command for command queue, it
 * sends the queue, cleans the queue, etc
 **/
int hclge_cmd_send(struct hclge_hw *hw, struct hclge_desc *desc, int num)
{
	struct hclge_dev *hdev = (struct hclge_dev *)hw->back;
	struct hclge_desc *desc_to_use;
	bool complete = false;
	u32 timeout = 0;
	int handle = 0;
	int retval = 0;
	u16 opcode, desc_ret;
	int ntc;

	spin_lock_bh(&hw->cmq.csq.lock);

	if (num > hclge_ring_space(&hw->cmq.csq)) {
		spin_unlock_bh(&hw->cmq.csq.lock);
		return -EBUSY;
	}

	/**
	 * Record the location of desc in the ring for this time
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
	hclge_write_dev(hw, HCLGE_NIC_CSQ_TAIL_REG, hw->cmq.csq.next_to_use);

	/**
	 * If the command is sync, wait for the firmware to write back,
	 * if multi descriptors to be sent, use the first one to check
	 */
	if (HCLGE_SEND_SYNC(le16_to_cpu(desc->flag))) {
		do {
			if (hclge_cmd_csq_done(hw))
				break;
			udelay(1);
			timeout++;
		} while (timeout < hw->cmq.tx_timeout);
	}

	if (hclge_cmd_csq_done(hw)) {
		complete = true;
		handle = 0;
		while (handle < num) {
			/* Get the result of hardware write back */
			desc_to_use = &hw->cmq.csq.desc[ntc];
			desc[handle] = *desc_to_use;
			pr_debug("Get cmd desc:\n");

			if (likely(!hclge_is_special_opcode(opcode)))
				desc_ret = le16_to_cpu(desc[handle].retval);
			else
				desc_ret = le16_to_cpu(desc[0].retval);

			if ((enum hclge_cmd_return_status)desc_ret ==
			    HCLGE_CMD_EXEC_SUCCESS)
				retval = 0;
			else
				retval = -EIO;
			hw->cmq.last_status = (enum hclge_cmd_status)desc_ret;
			ntc++;
			handle++;
			if (ntc == hw->cmq.csq.desc_num)
				ntc = 0;
		}
	}

	if (!complete)
		retval = -EAGAIN;

	/* Clean the command send queue */
	handle = hclge_cmd_csq_clean(hw);
	if (handle != num) {
		dev_warn(&hdev->pdev->dev,
			 "cleaned %d, need to clean %d\n", handle, num);
	}

	spin_unlock_bh(&hw->cmq.csq.lock);

	return retval;
}

static enum hclge_cmd_status hclge_cmd_query_firmware_version(
		struct hclge_hw *hw, u32 *version)
{
	struct hclge_query_version_cmd *resp;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_QUERY_FW_VER, 1);
	resp = (struct hclge_query_version_cmd *)desc.data;

	ret = hclge_cmd_send(hw, &desc, 1);
	if (!ret)
		*version = le32_to_cpu(resp->firmware);

	return ret;
}

int hclge_cmd_queue_init(struct hclge_dev *hdev)
{
	int ret;

	/* Setup the queue entries for use cmd queue */
	hdev->hw.cmq.csq.desc_num = HCLGE_NIC_CMQ_DESC_NUM;
	hdev->hw.cmq.crq.desc_num = HCLGE_NIC_CMQ_DESC_NUM;

	/* Setup Tx write back timeout */
	hdev->hw.cmq.tx_timeout = HCLGE_CMDQ_TX_TIMEOUT;

	/* Setup queue rings */
	ret = hclge_alloc_cmd_queue(hdev, HCLGE_TYPE_CSQ);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"CSQ ring setup error %d\n", ret);
		return ret;
	}

	ret = hclge_alloc_cmd_queue(hdev, HCLGE_TYPE_CRQ);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"CRQ ring setup error %d\n", ret);
		goto err_csq;
	}

	return 0;
err_csq:
	hclge_free_cmd_desc(&hdev->hw.cmq.csq);
	return ret;
}

int hclge_cmd_init(struct hclge_dev *hdev)
{
	u32 version;
	int ret;

	hdev->hw.cmq.csq.next_to_clean = 0;
	hdev->hw.cmq.csq.next_to_use = 0;
	hdev->hw.cmq.crq.next_to_clean = 0;
	hdev->hw.cmq.crq.next_to_use = 0;

	/* Setup the lock for command queue */
	spin_lock_init(&hdev->hw.cmq.csq.lock);
	spin_lock_init(&hdev->hw.cmq.crq.lock);

	hclge_cmd_init_regs(&hdev->hw);

	ret = hclge_cmd_query_firmware_version(&hdev->hw, &version);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"firmware version query failed %d\n", ret);
		return ret;
	}
	hdev->fw_version = version;

	dev_info(&hdev->pdev->dev, "The firmware version is %08x\n", version);

	return 0;
}

static void hclge_destroy_queue(struct hclge_cmq_ring *ring)
{
	spin_lock(&ring->lock);
	hclge_free_cmd_desc(ring);
	spin_unlock(&ring->lock);
}

void hclge_destroy_cmd_queue(struct hclge_hw *hw)
{
	hclge_destroy_queue(&hw->cmq.csq);
	hclge_destroy_queue(&hw->cmq.crq);
}
