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

#define cmq_ring_to_dev(ring)   (&(ring)->dev->pdev->dev)

static int hclge_ring_space(struct hclge_cmq_ring *ring)
{
	int ntu = ring->next_to_use;
	int ntc = ring->next_to_clean;
	int used = (ntu - ntc + ring->desc_num) % ring->desc_num;

	return ring->desc_num - used - 1;
}

static int is_valid_csq_clean_head(struct hclge_cmq_ring *ring, int head)
{
	int ntu = ring->next_to_use;
	int ntc = ring->next_to_clean;

	if (ntu > ntc)
		return head >= ntc && head <= ntu;

	return head >= ntc || head <= ntu;
}

static int hclge_alloc_cmd_desc(struct hclge_cmq_ring *ring)
{
	int size  = ring->desc_num * sizeof(struct hclge_desc);

	ring->desc = dma_alloc_coherent(cmq_ring_to_dev(ring), size,
					&ring->desc_dma_addr, GFP_KERNEL);
	if (!ring->desc)
		return -ENOMEM;

	return 0;
}

static void hclge_free_cmd_desc(struct hclge_cmq_ring *ring)
{
	int size  = ring->desc_num * sizeof(struct hclge_desc);

	if (ring->desc) {
		dma_free_coherent(cmq_ring_to_dev(ring), size,
				  ring->desc, ring->desc_dma_addr);
		ring->desc = NULL;
	}
}

static int hclge_alloc_cmd_queue(struct hclge_dev *hdev, int ring_type)
{
	struct hclge_hw *hw = &hdev->hw;
	struct hclge_cmq_ring *ring =
		(ring_type == HCLGE_TYPE_CSQ) ? &hw->cmq.csq : &hw->cmq.crq;
	int ret;

	ring->ring_type = ring_type;
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
}

static void hclge_cmd_config_regs(struct hclge_cmq_ring *ring)
{
	dma_addr_t dma = ring->desc_dma_addr;
	struct hclge_dev *hdev = ring->dev;
	struct hclge_hw *hw = &hdev->hw;
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
	hclge_cmd_config_regs(&hw->cmq.csq);
	hclge_cmd_config_regs(&hw->cmq.crq);
}

static int hclge_cmd_csq_clean(struct hclge_hw *hw)
{
	struct hclge_dev *hdev = container_of(hw, struct hclge_dev, hw);
	struct hclge_cmq_ring *csq = &hw->cmq.csq;
	u32 head;
	int clean;

	head = hclge_read_dev(hw, HCLGE_NIC_CSQ_HEAD_REG);
	rmb(); /* Make sure head is ready before touch any data */

	if (!is_valid_csq_clean_head(csq, head)) {
		dev_warn(&hdev->pdev->dev, "wrong cmd head (%u, %d-%d)\n", head,
			 csq->next_to_use, csq->next_to_clean);
		dev_warn(&hdev->pdev->dev,
			 "Disabling any further commands to IMP firmware\n");
		set_bit(HCLGE_STATE_CMD_DISABLE, &hdev->state);
		dev_warn(&hdev->pdev->dev,
			 "IMP firmware watchdog reset soon expected!\n");
		return -EIO;
	}

	clean = (head - csq->next_to_clean + csq->desc_num) % csq->desc_num;
	csq->next_to_clean = head;
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
	u16 spec_opcode[] = {HCLGE_OPC_STATS_64_BIT,
			     HCLGE_OPC_STATS_32_BIT,
			     HCLGE_OPC_STATS_MAC,
			     HCLGE_OPC_STATS_MAC_ALL,
			     HCLGE_OPC_QUERY_32_BIT_REG,
			     HCLGE_OPC_QUERY_64_BIT_REG,
			     HCLGE_QUERY_CLEAR_MPF_RAS_INT,
			     HCLGE_QUERY_CLEAR_PF_RAS_INT,
			     HCLGE_QUERY_CLEAR_ALL_MPF_MSIX_INT,
			     HCLGE_QUERY_CLEAR_ALL_PF_MSIX_INT,
			     HCLGE_QUERY_ALL_ERR_INFO};
	int i;

	for (i = 0; i < ARRAY_SIZE(spec_opcode); i++) {
		if (spec_opcode[i] == opcode)
			return true;
	}

	return false;
}

struct errcode {
	u32 imp_errcode;
	int common_errno;
};

static void hclge_cmd_copy_desc(struct hclge_hw *hw, struct hclge_desc *desc,
				int num)
{
	struct hclge_desc *desc_to_use;
	int handle = 0;

	while (handle < num) {
		desc_to_use = &hw->cmq.csq.desc[hw->cmq.csq.next_to_use];
		*desc_to_use = desc[handle];
		(hw->cmq.csq.next_to_use)++;
		if (hw->cmq.csq.next_to_use >= hw->cmq.csq.desc_num)
			hw->cmq.csq.next_to_use = 0;
		handle++;
	}
}

static int hclge_cmd_convert_err_code(u16 desc_ret)
{
	struct errcode hclge_cmd_errcode[] = {
		{HCLGE_CMD_EXEC_SUCCESS, 0},
		{HCLGE_CMD_NO_AUTH, -EPERM},
		{HCLGE_CMD_NOT_SUPPORTED, -EOPNOTSUPP},
		{HCLGE_CMD_QUEUE_FULL, -EXFULL},
		{HCLGE_CMD_NEXT_ERR, -ENOSR},
		{HCLGE_CMD_UNEXE_ERR, -ENOTBLK},
		{HCLGE_CMD_PARA_ERR, -EINVAL},
		{HCLGE_CMD_RESULT_ERR, -ERANGE},
		{HCLGE_CMD_TIMEOUT, -ETIME},
		{HCLGE_CMD_HILINK_ERR, -ENOLINK},
		{HCLGE_CMD_QUEUE_ILLEGAL, -ENXIO},
		{HCLGE_CMD_INVALID, -EBADR},
	};
	u32 errcode_count = ARRAY_SIZE(hclge_cmd_errcode);
	u32 i;

	for (i = 0; i < errcode_count; i++)
		if (hclge_cmd_errcode[i].imp_errcode == desc_ret)
			return hclge_cmd_errcode[i].common_errno;

	return -EIO;
}

static int hclge_cmd_check_retval(struct hclge_hw *hw, struct hclge_desc *desc,
				  int num, int ntc)
{
	u16 opcode, desc_ret;
	int handle;

	opcode = le16_to_cpu(desc[0].opcode);
	for (handle = 0; handle < num; handle++) {
		desc[handle] = hw->cmq.csq.desc[ntc];
		ntc++;
		if (ntc >= hw->cmq.csq.desc_num)
			ntc = 0;
	}
	if (likely(!hclge_is_special_opcode(opcode)))
		desc_ret = le16_to_cpu(desc[num - 1].retval);
	else
		desc_ret = le16_to_cpu(desc[0].retval);

	hw->cmq.last_status = desc_ret;

	return hclge_cmd_convert_err_code(desc_ret);
}

static int hclge_cmd_check_result(struct hclge_hw *hw, struct hclge_desc *desc,
				  int num, int ntc)
{
	struct hclge_dev *hdev = container_of(hw, struct hclge_dev, hw);
	bool is_completed = false;
	u32 timeout = 0;
	int handle, ret;

	/**
	 * If the command is sync, wait for the firmware to write back,
	 * if multi descriptors to be sent, use the first one to check
	 */
	if (HCLGE_SEND_SYNC(le16_to_cpu(desc->flag))) {
		do {
			if (hclge_cmd_csq_done(hw)) {
				is_completed = true;
				break;
			}
			udelay(1);
			timeout++;
		} while (timeout < hw->cmq.tx_timeout);
	}

	if (!is_completed)
		ret = -EBADE;
	else
		ret = hclge_cmd_check_retval(hw, desc, num, ntc);

	/* Clean the command send queue */
	handle = hclge_cmd_csq_clean(hw);
	if (handle < 0)
		ret = handle;
	else if (handle != num)
		dev_warn(&hdev->pdev->dev,
			 "cleaned %d, need to clean %d\n", handle, num);
	return ret;
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
	struct hclge_dev *hdev = container_of(hw, struct hclge_dev, hw);
	struct hclge_cmq_ring *csq = &hw->cmq.csq;
	int ret;
	int ntc;

	spin_lock_bh(&hw->cmq.csq.lock);

	if (test_bit(HCLGE_STATE_CMD_DISABLE, &hdev->state)) {
		spin_unlock_bh(&hw->cmq.csq.lock);
		return -EBUSY;
	}

	if (num > hclge_ring_space(&hw->cmq.csq)) {
		/* If CMDQ ring is full, SW HEAD and HW HEAD may be different,
		 * need update the SW HEAD pointer csq->next_to_clean
		 */
		csq->next_to_clean = hclge_read_dev(hw, HCLGE_NIC_CSQ_HEAD_REG);
		spin_unlock_bh(&hw->cmq.csq.lock);
		return -EBUSY;
	}

	/**
	 * Record the location of desc in the ring for this time
	 * which will be use for hardware to write back
	 */
	ntc = hw->cmq.csq.next_to_use;

	hclge_cmd_copy_desc(hw, desc, num);

	/* Write to hardware */
	hclge_write_dev(hw, HCLGE_NIC_CSQ_TAIL_REG, hw->cmq.csq.next_to_use);

	ret = hclge_cmd_check_result(hw, desc, num, ntc);

	spin_unlock_bh(&hw->cmq.csq.lock);

	return ret;
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

static void hclge_parse_capability(struct hclge_dev *hdev,
				   struct hclge_query_version_cmd *cmd)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);
	u32 caps;

	caps = __le32_to_cpu(cmd->caps[0]);
	if (hnae3_get_bit(caps, HCLGE_CAP_UDP_GSO_B))
		set_bit(HNAE3_DEV_SUPPORT_UDP_GSO_B, ae_dev->caps);
	if (hnae3_get_bit(caps, HCLGE_CAP_PTP_B))
		set_bit(HNAE3_DEV_SUPPORT_PTP_B, ae_dev->caps);
	if (hnae3_get_bit(caps, HCLGE_CAP_INT_QL_B))
		set_bit(HNAE3_DEV_SUPPORT_INT_QL_B, ae_dev->caps);
	if (hnae3_get_bit(caps, HCLGE_CAP_TQP_TXRX_INDEP_B))
		set_bit(HNAE3_DEV_SUPPORT_TQP_TXRX_INDEP_B, ae_dev->caps);
	if (hnae3_get_bit(caps, HCLGE_CAP_HW_TX_CSUM_B))
		set_bit(HNAE3_DEV_SUPPORT_HW_TX_CSUM_B, ae_dev->caps);
	if (hnae3_get_bit(caps, HCLGE_CAP_UDP_TUNNEL_CSUM_B))
		set_bit(HNAE3_DEV_SUPPORT_UDP_TUNNEL_CSUM_B, ae_dev->caps);
	if (hnae3_get_bit(caps, HCLGE_CAP_FD_FORWARD_TC_B))
		set_bit(HNAE3_DEV_SUPPORT_FD_FORWARD_TC_B, ae_dev->caps);
	if (hnae3_get_bit(caps, HCLGE_CAP_FEC_B))
		set_bit(HNAE3_DEV_SUPPORT_FEC_B, ae_dev->caps);
	if (hnae3_get_bit(caps, HCLGE_CAP_PAUSE_B))
		set_bit(HNAE3_DEV_SUPPORT_PAUSE_B, ae_dev->caps);
	if (hnae3_get_bit(caps, HCLGE_CAP_PHY_IMP_B))
		set_bit(HNAE3_DEV_SUPPORT_PHY_IMP_B, ae_dev->caps);
	if (hnae3_get_bit(caps, HCLGE_CAP_RAS_IMP_B))
		set_bit(HNAE3_DEV_SUPPORT_RAS_IMP_B, ae_dev->caps);
	if (hnae3_get_bit(caps, HCLGE_CAP_RXD_ADV_LAYOUT_B))
		set_bit(HNAE3_DEV_SUPPORT_RXD_ADV_LAYOUT_B, ae_dev->caps);
	if (hnae3_get_bit(caps, HCLGE_CAP_PORT_VLAN_BYPASS_B)) {
		set_bit(HNAE3_DEV_SUPPORT_PORT_VLAN_BYPASS_B, ae_dev->caps);
		set_bit(HNAE3_DEV_SUPPORT_VLAN_FLTR_MDF_B, ae_dev->caps);
	}
}

static __le32 hclge_build_api_caps(void)
{
	u32 api_caps = 0;

	hnae3_set_bit(api_caps, HCLGE_API_CAP_FLEX_RSS_TBL_B, 1);

	return cpu_to_le32(api_caps);
}

static enum hclge_cmd_status
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
	int ret;

	/* Setup the lock for command queue */
	spin_lock_init(&hdev->hw.cmq.csq.lock);
	spin_lock_init(&hdev->hw.cmq.crq.lock);

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

static int hclge_firmware_compat_config(struct hclge_dev *hdev)
{
	struct hclge_firmware_compat_cmd *req;
	struct hclge_desc desc;
	u32 compat = 0;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_IMP_COMPAT_CFG, false);

	req = (struct hclge_firmware_compat_cmd *)desc.data;

	hnae3_set_bit(compat, HCLGE_LINK_EVENT_REPORT_EN_B, 1);
	hnae3_set_bit(compat, HCLGE_NCSI_ERROR_REPORT_EN_B, 1);
	if (hnae3_dev_phy_imp_supported(hdev))
		hnae3_set_bit(compat, HCLGE_PHY_IMP_EN_B, 1);
	req->compat = cpu_to_le32(compat);

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

int hclge_cmd_init(struct hclge_dev *hdev)
{
	int ret;

	spin_lock_bh(&hdev->hw.cmq.csq.lock);
	spin_lock(&hdev->hw.cmq.crq.lock);

	hdev->hw.cmq.csq.next_to_clean = 0;
	hdev->hw.cmq.csq.next_to_use = 0;
	hdev->hw.cmq.crq.next_to_clean = 0;
	hdev->hw.cmq.crq.next_to_use = 0;

	hclge_cmd_init_regs(&hdev->hw);

	spin_unlock(&hdev->hw.cmq.crq.lock);
	spin_unlock_bh(&hdev->hw.cmq.csq.lock);

	clear_bit(HCLGE_STATE_CMD_DISABLE, &hdev->state);

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
	ret = hclge_firmware_compat_config(hdev);
	if (ret)
		dev_warn(&hdev->pdev->dev,
			 "Firmware compatible features not enabled(%d).\n",
			 ret);

	return 0;

err_cmd_init:
	set_bit(HCLGE_STATE_CMD_DISABLE, &hdev->state);

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
	set_bit(HCLGE_STATE_CMD_DISABLE, &hdev->state);
	/* wait to ensure that the firmware completes the possible left
	 * over commands.
	 */
	msleep(HCLGE_CMDQ_CLEAR_WAIT_TIME);
	spin_lock_bh(&hdev->hw.cmq.csq.lock);
	spin_lock(&hdev->hw.cmq.crq.lock);
	hclge_cmd_uninit_regs(&hdev->hw);
	spin_unlock(&hdev->hw.cmq.crq.lock);
	spin_unlock_bh(&hdev->hw.cmq.csq.lock);

	hclge_free_cmd_desc(&hdev->hw.cmq.csq);
	hclge_free_cmd_desc(&hdev->hw.cmq.crq);
}
