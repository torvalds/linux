// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/dma-direction.h>
#include "hclge_cmd.h"
#include "hnae3.h"
#include "hclge_main.h"

static int hclge_alloc_cmd_desc(struct hclge_comm_cmq_ring *ring)
{
	int size  = ring->desc_num * sizeof(struct hclge_desc);

	ring->desc = dma_alloc_coherent(&ring->pdev->dev,
					size, &ring->desc_dma_addr, GFP_KERNEL);
	if (!ring->desc)
		return -ENOMEM;

	return 0;
}

static void hclge_free_cmd_desc(struct hclge_comm_cmq_ring *ring)
{
	int size  = ring->desc_num * sizeof(struct hclge_desc);

	if (ring->desc) {
		dma_free_coherent(&ring->pdev->dev, size,
				  ring->desc, ring->desc_dma_addr);
		ring->desc = NULL;
	}
}

static int hclge_alloc_cmd_queue(struct hclge_dev *hdev, int ring_type)
{
	struct hclge_hw *hw = &hdev->hw;
	struct hclge_comm_cmq_ring *ring =
		(ring_type == HCLGE_TYPE_CSQ) ? &hw->hw.cmq.csq :
						&hw->hw.cmq.crq;
	int ret;

	ring->ring_type = ring_type;
	ring->pdev = hdev->pdev;

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
}

static void hclge_cmd_config_regs(struct hclge_hw *hw,
				  struct hclge_comm_cmq_ring *ring)
{
	dma_addr_t dma = ring->desc_dma_addr;
	u32 reg_val;

	if (ring->ring_type == HCLGE_TYPE_CSQ) {
		hclge_write_dev(hw, HCLGE_NIC_CSQ_BASEADDR_L_REG,
				lower_32_bits(dma));
		hclge_write_dev(hw, HCLGE_NIC_CSQ_BASEADDR_H_REG,
				upper_32_bits(dma));
		reg_val = hclge_read_dev(hw, HCLGE_NIC_CSQ_DEPTH_REG);
		reg_val &= HCLGE_NIC_SW_RST_RDY;
		reg_val |= ring->desc_num >> HCLGE_NIC_CMQ_DESC_NUM_S;
		hclge_write_dev(hw, HCLGE_NIC_CSQ_DEPTH_REG, reg_val);
		hclge_write_dev(hw, HCLGE_NIC_CSQ_HEAD_REG, 0);
		hclge_write_dev(hw, HCLGE_NIC_CSQ_TAIL_REG, 0);
	} else {
		hclge_write_dev(hw, HCLGE_NIC_CRQ_BASEADDR_L_REG,
				lower_32_bits(dma));
		hclge_write_dev(hw, HCLGE_NIC_CRQ_BASEADDR_H_REG,
				upper_32_bits(dma));
		hclge_write_dev(hw, HCLGE_NIC_CRQ_DEPTH_REG,
				ring->desc_num >> HCLGE_NIC_CMQ_DESC_NUM_S);
		hclge_write_dev(hw, HCLGE_NIC_CRQ_HEAD_REG, 0);
		hclge_write_dev(hw, HCLGE_NIC_CRQ_TAIL_REG, 0);
	}
}

static void hclge_cmd_init_regs(struct hclge_hw *hw)
{
	hclge_cmd_config_regs(hw, &hw->hw.cmq.csq);
	hclge_cmd_config_regs(hw, &hw->hw.cmq.crq);
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
	return hclge_comm_cmd_send(&hw->hw, desc, num, true);
}

static void hclge_set_default_capability(struct hclge_dev *hdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);

	set_bit(HNAE3_DEV_SUPPORT_FD_B, ae_dev->caps);
	set_bit(HNAE3_DEV_SUPPORT_GRO_B, ae_dev->caps);
	if (hdev->ae_dev->dev_version == HNAE3_DEVICE_VERSION_V2) {
		set_bit(HNAE3_DEV_SUPPORT_FEC_B, ae_dev->caps);
		set_bit(HNAE3_DEV_SUPPORT_PAUSE_B, ae_dev->caps);
	}
}

static const struct hclge_caps_bit_map hclge_cmd_caps_bit_map0[] = {
	{HCLGE_CAP_UDP_GSO_B, HNAE3_DEV_SUPPORT_UDP_GSO_B},
	{HCLGE_CAP_PTP_B, HNAE3_DEV_SUPPORT_PTP_B},
	{HCLGE_CAP_INT_QL_B, HNAE3_DEV_SUPPORT_INT_QL_B},
	{HCLGE_CAP_TQP_TXRX_INDEP_B, HNAE3_DEV_SUPPORT_TQP_TXRX_INDEP_B},
	{HCLGE_CAP_HW_TX_CSUM_B, HNAE3_DEV_SUPPORT_HW_TX_CSUM_B},
	{HCLGE_CAP_UDP_TUNNEL_CSUM_B, HNAE3_DEV_SUPPORT_UDP_TUNNEL_CSUM_B},
	{HCLGE_CAP_FD_FORWARD_TC_B, HNAE3_DEV_SUPPORT_FD_FORWARD_TC_B},
	{HCLGE_CAP_FEC_B, HNAE3_DEV_SUPPORT_FEC_B},
	{HCLGE_CAP_PAUSE_B, HNAE3_DEV_SUPPORT_PAUSE_B},
	{HCLGE_CAP_PHY_IMP_B, HNAE3_DEV_SUPPORT_PHY_IMP_B},
	{HCLGE_CAP_RAS_IMP_B, HNAE3_DEV_SUPPORT_RAS_IMP_B},
	{HCLGE_CAP_RXD_ADV_LAYOUT_B, HNAE3_DEV_SUPPORT_RXD_ADV_LAYOUT_B},
	{HCLGE_CAP_PORT_VLAN_BYPASS_B, HNAE3_DEV_SUPPORT_PORT_VLAN_BYPASS_B},
	{HCLGE_CAP_PORT_VLAN_BYPASS_B, HNAE3_DEV_SUPPORT_VLAN_FLTR_MDF_B},
};

static void hclge_parse_capability(struct hclge_dev *hdev,
				   struct hclge_query_version_cmd *cmd)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);
	u32 caps, i;

	caps = __le32_to_cpu(cmd->caps[0]);
	for (i = 0; i < ARRAY_SIZE(hclge_cmd_caps_bit_map0); i++)
		if (hnae3_get_bit(caps, hclge_cmd_caps_bit_map0[i].imp_bit))
			set_bit(hclge_cmd_caps_bit_map0[i].local_bit,
				ae_dev->caps);
}

static __le32 hclge_build_api_caps(void)
{
	u32 api_caps = 0;

	hnae3_set_bit(api_caps, HCLGE_API_CAP_FLEX_RSS_TBL_B, 1);

	return cpu_to_le32(api_caps);
}

static enum hclge_comm_cmd_status
hclge_cmd_query_version_and_capability(struct hclge_dev *hdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);
	struct hclge_query_version_cmd *resp;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_QUERY_FW_VER, 1);
	resp = (struct hclge_query_version_cmd *)desc.data;
	resp->api_caps = hclge_build_api_caps();

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		return ret;

	hdev->fw_version = le32_to_cpu(resp->firmware);

	ae_dev->dev_version = le32_to_cpu(resp->hardware) <<
					 HNAE3_PCI_REVISION_BIT_SIZE;
	ae_dev->dev_version |= hdev->pdev->revision;

	if (ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V2)
		hclge_set_default_capability(hdev);

	hclge_parse_capability(hdev, resp);

	return ret;
}

int hclge_cmd_queue_init(struct hclge_dev *hdev)
{
	struct hclge_comm_cmq *cmdq = &hdev->hw.hw.cmq;
	int ret;

	/* Setup the lock for command queue */
	spin_lock_init(&cmdq->csq.lock);
	spin_lock_init(&cmdq->crq.lock);

	cmdq->csq.pdev = hdev->pdev;
	cmdq->crq.pdev = hdev->pdev;

	/* Setup the queue entries for use cmd queue */
	cmdq->csq.desc_num = HCLGE_NIC_CMQ_DESC_NUM;
	cmdq->crq.desc_num = HCLGE_NIC_CMQ_DESC_NUM;

	/* Setup Tx write back timeout */
	cmdq->tx_timeout = HCLGE_CMDQ_TX_TIMEOUT;

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
	hclge_free_cmd_desc(&hdev->hw.hw.cmq.csq);
	return ret;
}

static int hclge_firmware_compat_config(struct hclge_dev *hdev, bool en)
{
	struct hclge_firmware_compat_cmd *req;
	struct hclge_desc desc;
	u32 compat = 0;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_IMP_COMPAT_CFG, false);

	if (en) {
		req = (struct hclge_firmware_compat_cmd *)desc.data;

		hnae3_set_bit(compat, HCLGE_LINK_EVENT_REPORT_EN_B, 1);
		hnae3_set_bit(compat, HCLGE_NCSI_ERROR_REPORT_EN_B, 1);
		if (hnae3_dev_phy_imp_supported(hdev))
			hnae3_set_bit(compat, HCLGE_PHY_IMP_EN_B, 1);
		hnae3_set_bit(compat, HCLGE_MAC_STATS_EXT_EN_B, 1);
		hnae3_set_bit(compat, HCLGE_SYNC_RX_RING_HEAD_EN_B, 1);

		req->compat = cpu_to_le32(compat);
	}

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

int hclge_cmd_init(struct hclge_dev *hdev)
{
	struct hclge_comm_cmq *cmdq = &hdev->hw.hw.cmq;
	int ret;

	spin_lock_bh(&cmdq->csq.lock);
	spin_lock(&cmdq->crq.lock);

	cmdq->csq.next_to_clean = 0;
	cmdq->csq.next_to_use = 0;
	cmdq->crq.next_to_clean = 0;
	cmdq->crq.next_to_use = 0;

	hclge_cmd_init_regs(&hdev->hw);

	spin_unlock(&cmdq->crq.lock);
	spin_unlock_bh(&cmdq->csq.lock);

	clear_bit(HCLGE_COMM_STATE_CMD_DISABLE, &hdev->hw.hw.comm_state);

	/* Check if there is new reset pending, because the higher level
	 * reset may happen when lower level reset is being processed.
	 */
	if ((hclge_is_reset_pending(hdev))) {
		dev_err(&hdev->pdev->dev,
			"failed to init cmd since reset %#lx pending\n",
			hdev->reset_pending);
		ret = -EBUSY;
		goto err_cmd_init;
	}

	/* get version and device capabilities */
	ret = hclge_cmd_query_version_and_capability(hdev);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to query version and capabilities, ret = %d\n",
			ret);
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

	/* ask the firmware to enable some features, driver can work without
	 * it.
	 */
	ret = hclge_firmware_compat_config(hdev, true);
	if (ret)
		dev_warn(&hdev->pdev->dev,
			 "Firmware compatible features not enabled(%d).\n",
			 ret);

	return 0;

err_cmd_init:
	set_bit(HCLGE_COMM_STATE_CMD_DISABLE, &hdev->hw.hw.comm_state);

	return ret;
}

static void hclge_cmd_uninit_regs(struct hclge_hw *hw)
{
	hclge_write_dev(hw, HCLGE_NIC_CSQ_BASEADDR_L_REG, 0);
	hclge_write_dev(hw, HCLGE_NIC_CSQ_BASEADDR_H_REG, 0);
	hclge_write_dev(hw, HCLGE_NIC_CSQ_DEPTH_REG, 0);
	hclge_write_dev(hw, HCLGE_NIC_CSQ_HEAD_REG, 0);
	hclge_write_dev(hw, HCLGE_NIC_CSQ_TAIL_REG, 0);
	hclge_write_dev(hw, HCLGE_NIC_CRQ_BASEADDR_L_REG, 0);
	hclge_write_dev(hw, HCLGE_NIC_CRQ_BASEADDR_H_REG, 0);
	hclge_write_dev(hw, HCLGE_NIC_CRQ_DEPTH_REG, 0);
	hclge_write_dev(hw, HCLGE_NIC_CRQ_HEAD_REG, 0);
	hclge_write_dev(hw, HCLGE_NIC_CRQ_TAIL_REG, 0);
}

void hclge_cmd_uninit(struct hclge_dev *hdev)
{
	struct hclge_comm_cmq *cmdq = &hdev->hw.hw.cmq;

	cmdq->csq.pdev = hdev->pdev;

	hclge_firmware_compat_config(hdev, false);

	set_bit(HCLGE_COMM_STATE_CMD_DISABLE, &hdev->hw.hw.comm_state);
	/* wait to ensure that the firmware completes the possible left
	 * over commands.
	 */
	msleep(HCLGE_CMDQ_CLEAR_WAIT_TIME);
	spin_lock_bh(&cmdq->csq.lock);
	spin_lock(&cmdq->crq.lock);
	hclge_cmd_uninit_regs(&hdev->hw);
	spin_unlock(&cmdq->crq.lock);
	spin_unlock_bh(&cmdq->csq.lock);

	hclge_free_cmd_desc(&cmdq->csq);
	hclge_free_cmd_desc(&cmdq->crq);
}
