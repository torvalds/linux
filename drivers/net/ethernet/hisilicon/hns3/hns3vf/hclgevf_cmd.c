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

static void hclgevf_cmd_config_regs(struct hclgevf_hw *hw,
				    struct hclge_comm_cmq_ring *ring)
{
	u32 reg_val;

	if (ring->ring_type == HCLGEVF_TYPE_CSQ) {
		reg_val = lower_32_bits(ring->desc_dma_addr);
		hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_BASEADDR_L_REG, reg_val);
		reg_val = upper_32_bits(ring->desc_dma_addr);
		hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_BASEADDR_H_REG, reg_val);

		reg_val = hclgevf_read_dev(hw, HCLGEVF_NIC_CSQ_DEPTH_REG);
		reg_val &= HCLGEVF_NIC_SW_RST_RDY;
		reg_val |= (ring->desc_num >> HCLGEVF_NIC_CMQ_DESC_NUM_S);
		hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_DEPTH_REG, reg_val);

		hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_HEAD_REG, 0);
		hclgevf_write_dev(hw, HCLGEVF_NIC_CSQ_TAIL_REG, 0);
	} else {
		reg_val = lower_32_bits(ring->desc_dma_addr);
		hclgevf_write_dev(hw, HCLGEVF_NIC_CRQ_BASEADDR_L_REG, reg_val);
		reg_val = upper_32_bits(ring->desc_dma_addr);
		hclgevf_write_dev(hw, HCLGEVF_NIC_CRQ_BASEADDR_H_REG, reg_val);

		reg_val = (ring->desc_num >> HCLGEVF_NIC_CMQ_DESC_NUM_S);
		hclgevf_write_dev(hw, HCLGEVF_NIC_CRQ_DEPTH_REG, reg_val);

		hclgevf_write_dev(hw, HCLGEVF_NIC_CRQ_HEAD_REG, 0);
		hclgevf_write_dev(hw, HCLGEVF_NIC_CRQ_TAIL_REG, 0);
	}
}

static void hclgevf_cmd_init_regs(struct hclgevf_hw *hw)
{
	hclgevf_cmd_config_regs(hw, &hw->hw.cmq.csq);
	hclgevf_cmd_config_regs(hw, &hw->hw.cmq.crq);
}

static int hclgevf_alloc_cmd_desc(struct hclge_comm_cmq_ring *ring)
{
	int size = ring->desc_num * sizeof(struct hclge_desc);

	ring->desc = dma_alloc_coherent(&ring->pdev->dev, size,
					&ring->desc_dma_addr, GFP_KERNEL);
	if (!ring->desc)
		return -ENOMEM;

	return 0;
}

static void hclgevf_free_cmd_desc(struct hclge_comm_cmq_ring *ring)
{
	int size  = ring->desc_num * sizeof(struct hclge_desc);

	if (ring->desc) {
		dma_free_coherent(&ring->pdev->dev, size,
				  ring->desc, ring->desc_dma_addr);
		ring->desc = NULL;
	}
}

static int hclgevf_alloc_cmd_queue(struct hclgevf_dev *hdev, int ring_type)
{
	struct hclgevf_hw *hw = &hdev->hw;
	struct hclge_comm_cmq_ring *ring =
		(ring_type == HCLGEVF_TYPE_CSQ) ? &hw->hw.cmq.csq :
						  &hw->hw.cmq.crq;
	int ret;

	ring->pdev = hdev->pdev;
	ring->ring_type = ring_type;

	/* allocate CSQ/CRQ descriptor */
	ret = hclgevf_alloc_cmd_desc(ring);
	if (ret)
		dev_err(&hdev->pdev->dev, "failed(%d) to alloc %s desc\n", ret,
			(ring_type == HCLGEVF_TYPE_CSQ) ? "CSQ" : "CRQ");

	return ret;
}

void hclgevf_cmd_setup_basic_desc(struct hclge_desc *desc,
				  enum hclgevf_opcode_type opcode, bool is_read)
{
	memset(desc, 0, sizeof(struct hclge_desc));
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
int hclgevf_cmd_send(struct hclgevf_hw *hw, struct hclge_desc *desc, int num)
{
	return hclge_comm_cmd_send(&hw->hw, desc, num, false);
}

static void hclgevf_set_default_capability(struct hclgevf_dev *hdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);

	set_bit(HNAE3_DEV_SUPPORT_FD_B, ae_dev->caps);
	set_bit(HNAE3_DEV_SUPPORT_GRO_B, ae_dev->caps);
	set_bit(HNAE3_DEV_SUPPORT_FEC_B, ae_dev->caps);
}

static const struct hclgevf_caps_bit_map hclgevf_cmd_caps_bit_map0[] = {
	{HCLGEVF_CAP_UDP_GSO_B, HNAE3_DEV_SUPPORT_UDP_GSO_B},
	{HCLGEVF_CAP_INT_QL_B, HNAE3_DEV_SUPPORT_INT_QL_B},
	{HCLGEVF_CAP_TQP_TXRX_INDEP_B, HNAE3_DEV_SUPPORT_TQP_TXRX_INDEP_B},
	{HCLGEVF_CAP_HW_TX_CSUM_B, HNAE3_DEV_SUPPORT_HW_TX_CSUM_B},
	{HCLGEVF_CAP_UDP_TUNNEL_CSUM_B, HNAE3_DEV_SUPPORT_UDP_TUNNEL_CSUM_B},
	{HCLGEVF_CAP_RXD_ADV_LAYOUT_B, HNAE3_DEV_SUPPORT_RXD_ADV_LAYOUT_B},
};

static void hclgevf_parse_capability(struct hclgevf_dev *hdev,
				     struct hclgevf_query_version_cmd *cmd)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);
	u32 caps, i;

	caps = __le32_to_cpu(cmd->caps[0]);
	for (i = 0; i < ARRAY_SIZE(hclgevf_cmd_caps_bit_map0); i++)
		if (hnae3_get_bit(caps, hclgevf_cmd_caps_bit_map0[i].imp_bit))
			set_bit(hclgevf_cmd_caps_bit_map0[i].local_bit,
				ae_dev->caps);
}

static __le32 hclgevf_build_api_caps(void)
{
	u32 api_caps = 0;

	hnae3_set_bit(api_caps, HCLGEVF_API_CAP_FLEX_RSS_TBL_B, 1);

	return cpu_to_le32(api_caps);
}

static int hclgevf_cmd_query_version_and_capability(struct hclgevf_dev *hdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);
	struct hclgevf_query_version_cmd *resp;
	struct hclge_desc desc;
	int status;

	resp = (struct hclgevf_query_version_cmd *)desc.data;

	hclgevf_cmd_setup_basic_desc(&desc, HCLGEVF_OPC_QUERY_FW_VER, 1);
	resp->api_caps = hclgevf_build_api_caps();
	status = hclgevf_cmd_send(&hdev->hw, &desc, 1);
	if (status)
		return status;

	hdev->fw_version = le32_to_cpu(resp->firmware);

	ae_dev->dev_version = le32_to_cpu(resp->hardware) <<
				 HNAE3_PCI_REVISION_BIT_SIZE;
	ae_dev->dev_version |= hdev->pdev->revision;

	if (ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V2)
		hclgevf_set_default_capability(hdev);

	hclgevf_parse_capability(hdev, resp);

	return status;
}

int hclgevf_cmd_queue_init(struct hclgevf_dev *hdev)
{
	struct hclge_comm_cmq *cmdq = &hdev->hw.hw.cmq;
	int ret;

	/* Setup the lock for command queue */
	spin_lock_init(&cmdq->csq.lock);
	spin_lock_init(&cmdq->crq.lock);

	cmdq->csq.pdev = hdev->pdev;
	cmdq->tx_timeout = HCLGEVF_CMDQ_TX_TIMEOUT;
	cmdq->csq.desc_num = HCLGEVF_NIC_CMQ_DESC_NUM;
	cmdq->crq.desc_num = HCLGEVF_NIC_CMQ_DESC_NUM;

	ret = hclgevf_alloc_cmd_queue(hdev, HCLGEVF_TYPE_CSQ);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"CSQ ring setup error %d\n", ret);
		return ret;
	}

	ret = hclgevf_alloc_cmd_queue(hdev, HCLGEVF_TYPE_CRQ);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"CRQ ring setup error %d\n", ret);
		goto err_csq;
	}

	return 0;
err_csq:
	hclgevf_free_cmd_desc(&cmdq->csq);
	return ret;
}

static int hclgevf_firmware_compat_config(struct hclgevf_dev *hdev, bool en)
{
	struct hclgevf_firmware_compat_cmd *req;
	struct hclge_desc desc;
	u32 compat = 0;

	hclgevf_cmd_setup_basic_desc(&desc, HCLGEVF_OPC_IMP_COMPAT_CFG, false);

	if (en) {
		req = (struct hclgevf_firmware_compat_cmd *)desc.data;

		hnae3_set_bit(compat, HCLGEVF_SYNC_RX_RING_HEAD_EN_B, 1);

		req->compat = cpu_to_le32(compat);
	}

	return hclgevf_cmd_send(&hdev->hw, &desc, 1);
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

	hclgevf_cmd_init_regs(&hdev->hw);

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
	ret = hclgevf_cmd_query_version_and_capability(hdev);
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
		ret = hclgevf_firmware_compat_config(hdev, true);
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
	hclgevf_firmware_compat_config(hdev, false);
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

	hclgevf_free_cmd_desc(&cmdq->csq);
	hclgevf_free_cmd_desc(&cmdq->crq);
}
