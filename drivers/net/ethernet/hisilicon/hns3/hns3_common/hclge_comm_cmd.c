// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2021-2021 Hisilicon Limited.

#include "hnae3.h"
#include "hclge_comm_cmd.h"

static void hclge_comm_cmd_config_regs(struct hclge_comm_hw *hw,
				       struct hclge_comm_cmq_ring *ring)
{
	dma_addr_t dma = ring->desc_dma_addr;
	u32 reg_val;

	if (ring->ring_type == HCLGE_COMM_TYPE_CSQ) {
		hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CSQ_BASEADDR_L_REG,
				     lower_32_bits(dma));
		hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CSQ_BASEADDR_H_REG,
				     upper_32_bits(dma));
		reg_val = hclge_comm_read_dev(hw, HCLGE_COMM_NIC_CSQ_DEPTH_REG);
		reg_val &= HCLGE_COMM_NIC_SW_RST_RDY;
		reg_val |= ring->desc_num >> HCLGE_COMM_NIC_CMQ_DESC_NUM_S;
		hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CSQ_DEPTH_REG, reg_val);
		hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CSQ_HEAD_REG, 0);
		hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CSQ_TAIL_REG, 0);
	} else {
		hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CRQ_BASEADDR_L_REG,
				     lower_32_bits(dma));
		hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CRQ_BASEADDR_H_REG,
				     upper_32_bits(dma));
		reg_val = ring->desc_num >> HCLGE_COMM_NIC_CMQ_DESC_NUM_S;
		hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CRQ_DEPTH_REG, reg_val);
		hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CRQ_HEAD_REG, 0);
		hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CRQ_TAIL_REG, 0);
	}
}

void hclge_comm_cmd_init_regs(struct hclge_comm_hw *hw)
{
	hclge_comm_cmd_config_regs(hw, &hw->cmq.csq);
	hclge_comm_cmd_config_regs(hw, &hw->cmq.crq);
}

void hclge_comm_cmd_reuse_desc(struct hclge_desc *desc, bool is_read)
{
	desc->flag = cpu_to_le16(HCLGE_COMM_CMD_FLAG_NO_INTR |
				 HCLGE_COMM_CMD_FLAG_IN);
	if (is_read)
		desc->flag |= cpu_to_le16(HCLGE_COMM_CMD_FLAG_WR);
	else
		desc->flag &= cpu_to_le16(~HCLGE_COMM_CMD_FLAG_WR);
}
EXPORT_SYMBOL_GPL(hclge_comm_cmd_reuse_desc);

static void hclge_comm_set_default_capability(struct hnae3_ae_dev *ae_dev,
					      bool is_pf)
{
	set_bit(HNAE3_DEV_SUPPORT_GRO_B, ae_dev->caps);
	if (is_pf) {
		set_bit(HNAE3_DEV_SUPPORT_FD_B, ae_dev->caps);
		set_bit(HNAE3_DEV_SUPPORT_FEC_B, ae_dev->caps);
		set_bit(HNAE3_DEV_SUPPORT_PAUSE_B, ae_dev->caps);
	}
}

void hclge_comm_cmd_setup_basic_desc(struct hclge_desc *desc,
				     enum hclge_opcode_type opcode,
				     bool is_read)
{
	memset((void *)desc, 0, sizeof(struct hclge_desc));
	desc->opcode = cpu_to_le16(opcode);
	desc->flag = cpu_to_le16(HCLGE_COMM_CMD_FLAG_NO_INTR |
				 HCLGE_COMM_CMD_FLAG_IN);

	if (is_read)
		desc->flag |= cpu_to_le16(HCLGE_COMM_CMD_FLAG_WR);
}
EXPORT_SYMBOL_GPL(hclge_comm_cmd_setup_basic_desc);

int hclge_comm_firmware_compat_config(struct hnae3_ae_dev *ae_dev,
				      struct hclge_comm_hw *hw, bool en)
{
	struct hclge_comm_firmware_compat_cmd *req;
	struct hclge_desc desc;
	u32 compat = 0;

	hclge_comm_cmd_setup_basic_desc(&desc, HCLGE_OPC_IMP_COMPAT_CFG, false);

	if (en) {
		req = (struct hclge_comm_firmware_compat_cmd *)desc.data;

		hnae3_set_bit(compat, HCLGE_COMM_LINK_EVENT_REPORT_EN_B, 1);
		hnae3_set_bit(compat, HCLGE_COMM_NCSI_ERROR_REPORT_EN_B, 1);
		if (hclge_comm_dev_phy_imp_supported(ae_dev))
			hnae3_set_bit(compat, HCLGE_COMM_PHY_IMP_EN_B, 1);
		hnae3_set_bit(compat, HCLGE_COMM_MAC_STATS_EXT_EN_B, 1);
		hnae3_set_bit(compat, HCLGE_COMM_SYNC_RX_RING_HEAD_EN_B, 1);
		hnae3_set_bit(compat, HCLGE_COMM_LLRS_FEC_EN_B, 1);

		req->compat = cpu_to_le32(compat);
	}

	return hclge_comm_cmd_send(hw, &desc, 1);
}

void hclge_comm_free_cmd_desc(struct hclge_comm_cmq_ring *ring)
{
	int size  = ring->desc_num * sizeof(struct hclge_desc);

	if (!ring->desc)
		return;

	dma_free_coherent(&ring->pdev->dev, size,
			  ring->desc, ring->desc_dma_addr);
	ring->desc = NULL;
}

static int hclge_comm_alloc_cmd_desc(struct hclge_comm_cmq_ring *ring)
{
	int size  = ring->desc_num * sizeof(struct hclge_desc);

	ring->desc = dma_alloc_coherent(&ring->pdev->dev,
					size, &ring->desc_dma_addr, GFP_KERNEL);
	if (!ring->desc)
		return -ENOMEM;

	return 0;
}

static __le32 hclge_comm_build_api_caps(void)
{
	u32 api_caps = 0;

	hnae3_set_bit(api_caps, HCLGE_COMM_API_CAP_FLEX_RSS_TBL_B, 1);

	return cpu_to_le32(api_caps);
}

static const struct hclge_comm_caps_bit_map hclge_pf_cmd_caps[] = {
	{HCLGE_COMM_CAP_UDP_GSO_B, HNAE3_DEV_SUPPORT_UDP_GSO_B},
	{HCLGE_COMM_CAP_PTP_B, HNAE3_DEV_SUPPORT_PTP_B},
	{HCLGE_COMM_CAP_INT_QL_B, HNAE3_DEV_SUPPORT_INT_QL_B},
	{HCLGE_COMM_CAP_TQP_TXRX_INDEP_B, HNAE3_DEV_SUPPORT_TQP_TXRX_INDEP_B},
	{HCLGE_COMM_CAP_HW_TX_CSUM_B, HNAE3_DEV_SUPPORT_HW_TX_CSUM_B},
	{HCLGE_COMM_CAP_UDP_TUNNEL_CSUM_B, HNAE3_DEV_SUPPORT_UDP_TUNNEL_CSUM_B},
	{HCLGE_COMM_CAP_FD_FORWARD_TC_B, HNAE3_DEV_SUPPORT_FD_FORWARD_TC_B},
	{HCLGE_COMM_CAP_FEC_B, HNAE3_DEV_SUPPORT_FEC_B},
	{HCLGE_COMM_CAP_PAUSE_B, HNAE3_DEV_SUPPORT_PAUSE_B},
	{HCLGE_COMM_CAP_PHY_IMP_B, HNAE3_DEV_SUPPORT_PHY_IMP_B},
	{HCLGE_COMM_CAP_QB_B, HNAE3_DEV_SUPPORT_QB_B},
	{HCLGE_COMM_CAP_TX_PUSH_B, HNAE3_DEV_SUPPORT_TX_PUSH_B},
	{HCLGE_COMM_CAP_RAS_IMP_B, HNAE3_DEV_SUPPORT_RAS_IMP_B},
	{HCLGE_COMM_CAP_RXD_ADV_LAYOUT_B, HNAE3_DEV_SUPPORT_RXD_ADV_LAYOUT_B},
	{HCLGE_COMM_CAP_PORT_VLAN_BYPASS_B,
	 HNAE3_DEV_SUPPORT_PORT_VLAN_BYPASS_B},
	{HCLGE_COMM_CAP_PORT_VLAN_BYPASS_B, HNAE3_DEV_SUPPORT_VLAN_FLTR_MDF_B},
	{HCLGE_COMM_CAP_CQ_B, HNAE3_DEV_SUPPORT_CQ_B},
	{HCLGE_COMM_CAP_GRO_B, HNAE3_DEV_SUPPORT_GRO_B},
	{HCLGE_COMM_CAP_FD_B, HNAE3_DEV_SUPPORT_FD_B},
	{HCLGE_COMM_CAP_FEC_STATS_B, HNAE3_DEV_SUPPORT_FEC_STATS_B},
	{HCLGE_COMM_CAP_LANE_NUM_B, HNAE3_DEV_SUPPORT_LANE_NUM_B},
	{HCLGE_COMM_CAP_WOL_B, HNAE3_DEV_SUPPORT_WOL_B},
	{HCLGE_COMM_CAP_TM_FLUSH_B, HNAE3_DEV_SUPPORT_TM_FLUSH_B},
	{HCLGE_COMM_CAP_VF_FAULT_B, HNAE3_DEV_SUPPORT_VF_FAULT_B},
	{HCLGE_COMM_CAP_ERR_MOD_GEN_REG_B, HNAE3_DEV_SUPPORT_ERR_MOD_GEN_REG_B},
};

static const struct hclge_comm_caps_bit_map hclge_vf_cmd_caps[] = {
	{HCLGE_COMM_CAP_UDP_GSO_B, HNAE3_DEV_SUPPORT_UDP_GSO_B},
	{HCLGE_COMM_CAP_INT_QL_B, HNAE3_DEV_SUPPORT_INT_QL_B},
	{HCLGE_COMM_CAP_TQP_TXRX_INDEP_B, HNAE3_DEV_SUPPORT_TQP_TXRX_INDEP_B},
	{HCLGE_COMM_CAP_HW_TX_CSUM_B, HNAE3_DEV_SUPPORT_HW_TX_CSUM_B},
	{HCLGE_COMM_CAP_UDP_TUNNEL_CSUM_B, HNAE3_DEV_SUPPORT_UDP_TUNNEL_CSUM_B},
	{HCLGE_COMM_CAP_QB_B, HNAE3_DEV_SUPPORT_QB_B},
	{HCLGE_COMM_CAP_TX_PUSH_B, HNAE3_DEV_SUPPORT_TX_PUSH_B},
	{HCLGE_COMM_CAP_RXD_ADV_LAYOUT_B, HNAE3_DEV_SUPPORT_RXD_ADV_LAYOUT_B},
	{HCLGE_COMM_CAP_CQ_B, HNAE3_DEV_SUPPORT_CQ_B},
	{HCLGE_COMM_CAP_GRO_B, HNAE3_DEV_SUPPORT_GRO_B},
};

static void
hclge_comm_capability_to_bitmap(unsigned long *bitmap, __le32 *caps)
{
	const unsigned int words = HCLGE_COMM_QUERY_CAP_LENGTH;
	u32 val[HCLGE_COMM_QUERY_CAP_LENGTH];
	unsigned int i;

	for (i = 0; i < words; i++)
		val[i] = __le32_to_cpu(caps[i]);

	bitmap_from_arr32(bitmap, val,
			  HCLGE_COMM_QUERY_CAP_LENGTH * BITS_PER_TYPE(u32));
}

static void
hclge_comm_parse_capability(struct hnae3_ae_dev *ae_dev, bool is_pf,
			    struct hclge_comm_query_version_cmd *cmd)
{
	const struct hclge_comm_caps_bit_map *caps_map =
				is_pf ? hclge_pf_cmd_caps : hclge_vf_cmd_caps;
	u32 size = is_pf ? ARRAY_SIZE(hclge_pf_cmd_caps) :
				ARRAY_SIZE(hclge_vf_cmd_caps);
	DECLARE_BITMAP(caps, HCLGE_COMM_QUERY_CAP_LENGTH * BITS_PER_TYPE(u32));
	u32 i;

	hclge_comm_capability_to_bitmap(caps, cmd->caps);
	for (i = 0; i < size; i++)
		if (test_bit(caps_map[i].imp_bit, caps))
			set_bit(caps_map[i].local_bit, ae_dev->caps);
}

int hclge_comm_alloc_cmd_queue(struct hclge_comm_hw *hw, int ring_type)
{
	struct hclge_comm_cmq_ring *ring =
		(ring_type == HCLGE_COMM_TYPE_CSQ) ? &hw->cmq.csq :
						     &hw->cmq.crq;
	int ret;

	ring->ring_type = ring_type;

	ret = hclge_comm_alloc_cmd_desc(ring);
	if (ret)
		dev_err(&ring->pdev->dev, "descriptor %s alloc error %d\n",
			(ring_type == HCLGE_COMM_TYPE_CSQ) ? "CSQ" : "CRQ",
			ret);

	return ret;
}

int hclge_comm_cmd_query_version_and_capability(struct hnae3_ae_dev *ae_dev,
						struct hclge_comm_hw *hw,
						u32 *fw_version, bool is_pf)
{
	struct hclge_comm_query_version_cmd *resp;
	struct hclge_desc desc;
	int ret;

	hclge_comm_cmd_setup_basic_desc(&desc, HCLGE_OPC_QUERY_FW_VER, 1);
	resp = (struct hclge_comm_query_version_cmd *)desc.data;
	resp->api_caps = hclge_comm_build_api_caps();

	ret = hclge_comm_cmd_send(hw, &desc, 1);
	if (ret)
		return ret;

	*fw_version = le32_to_cpu(resp->firmware);

	ae_dev->dev_version = le32_to_cpu(resp->hardware) <<
					 HNAE3_PCI_REVISION_BIT_SIZE;
	ae_dev->dev_version |= ae_dev->pdev->revision;

	if (ae_dev->dev_version == HNAE3_DEVICE_VERSION_V2) {
		hclge_comm_set_default_capability(ae_dev, is_pf);
		return 0;
	}

	hclge_comm_parse_capability(ae_dev, is_pf, resp);

	return ret;
}

static const u16 spec_opcode[] = { HCLGE_OPC_STATS_64_BIT,
				   HCLGE_OPC_STATS_32_BIT,
				   HCLGE_OPC_STATS_MAC,
				   HCLGE_OPC_STATS_MAC_ALL,
				   HCLGE_OPC_QUERY_32_BIT_REG,
				   HCLGE_OPC_QUERY_64_BIT_REG,
				   HCLGE_QUERY_CLEAR_MPF_RAS_INT,
				   HCLGE_QUERY_CLEAR_PF_RAS_INT,
				   HCLGE_QUERY_CLEAR_ALL_MPF_MSIX_INT,
				   HCLGE_QUERY_CLEAR_ALL_PF_MSIX_INT,
				   HCLGE_QUERY_ALL_ERR_INFO };

static bool hclge_comm_is_special_opcode(u16 opcode)
{
	/* these commands have several descriptors,
	 * and use the first one to save opcode and return value
	 */
	u32 i;

	for (i = 0; i < ARRAY_SIZE(spec_opcode); i++)
		if (spec_opcode[i] == opcode)
			return true;

	return false;
}

static int hclge_comm_ring_space(struct hclge_comm_cmq_ring *ring)
{
	int ntc = ring->next_to_clean;
	int ntu = ring->next_to_use;
	int used = (ntu - ntc + ring->desc_num) % ring->desc_num;

	return ring->desc_num - used - 1;
}

static void hclge_comm_cmd_copy_desc(struct hclge_comm_hw *hw,
				     struct hclge_desc *desc, int num)
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

static int hclge_comm_is_valid_csq_clean_head(struct hclge_comm_cmq_ring *ring,
					      int head)
{
	int ntc = ring->next_to_clean;
	int ntu = ring->next_to_use;

	if (ntu > ntc)
		return head >= ntc && head <= ntu;

	return head >= ntc || head <= ntu;
}

static int hclge_comm_cmd_csq_clean(struct hclge_comm_hw *hw)
{
	struct hclge_comm_cmq_ring *csq = &hw->cmq.csq;
	int clean;
	u32 head;

	head = hclge_comm_read_dev(hw, HCLGE_COMM_NIC_CSQ_HEAD_REG);
	rmb(); /* Make sure head is ready before touch any data */

	if (!hclge_comm_is_valid_csq_clean_head(csq, head)) {
		dev_warn(&hw->cmq.csq.pdev->dev, "wrong cmd head (%u, %d-%d)\n",
			 head, csq->next_to_use, csq->next_to_clean);
		dev_warn(&hw->cmq.csq.pdev->dev,
			 "Disabling any further commands to IMP firmware\n");
		set_bit(HCLGE_COMM_STATE_CMD_DISABLE, &hw->comm_state);
		dev_warn(&hw->cmq.csq.pdev->dev,
			 "IMP firmware watchdog reset soon expected!\n");
		return -EIO;
	}

	clean = (head - csq->next_to_clean + csq->desc_num) % csq->desc_num;
	csq->next_to_clean = head;
	return clean;
}

static int hclge_comm_cmd_csq_done(struct hclge_comm_hw *hw)
{
	u32 head = hclge_comm_read_dev(hw, HCLGE_COMM_NIC_CSQ_HEAD_REG);
	return head == (u32)hw->cmq.csq.next_to_use;
}

static u32 hclge_get_cmdq_tx_timeout(u16 opcode, u32 tx_timeout)
{
	static const struct hclge_cmdq_tx_timeout_map cmdq_tx_timeout_map[] = {
		{HCLGE_OPC_CFG_RST_TRIGGER, HCLGE_COMM_CMDQ_CFG_RST_TIMEOUT},
	};
	u32 i;

	for (i = 0; i < ARRAY_SIZE(cmdq_tx_timeout_map); i++)
		if (cmdq_tx_timeout_map[i].opcode == opcode)
			return cmdq_tx_timeout_map[i].tx_timeout;

	return tx_timeout;
}

static void hclge_comm_wait_for_resp(struct hclge_comm_hw *hw, u16 opcode,
				     bool *is_completed)
{
	u32 cmdq_tx_timeout = hclge_get_cmdq_tx_timeout(opcode,
							hw->cmq.tx_timeout);
	u32 timeout = 0;

	do {
		if (hclge_comm_cmd_csq_done(hw)) {
			*is_completed = true;
			break;
		}
		udelay(1);
		timeout++;
	} while (timeout < cmdq_tx_timeout);
}

static int hclge_comm_cmd_convert_err_code(u16 desc_ret)
{
	struct hclge_comm_errcode hclge_comm_cmd_errcode[] = {
		{ HCLGE_COMM_CMD_EXEC_SUCCESS, 0 },
		{ HCLGE_COMM_CMD_NO_AUTH, -EPERM },
		{ HCLGE_COMM_CMD_NOT_SUPPORTED, -EOPNOTSUPP },
		{ HCLGE_COMM_CMD_QUEUE_FULL, -EXFULL },
		{ HCLGE_COMM_CMD_NEXT_ERR, -ENOSR },
		{ HCLGE_COMM_CMD_UNEXE_ERR, -ENOTBLK },
		{ HCLGE_COMM_CMD_PARA_ERR, -EINVAL },
		{ HCLGE_COMM_CMD_RESULT_ERR, -ERANGE },
		{ HCLGE_COMM_CMD_TIMEOUT, -ETIME },
		{ HCLGE_COMM_CMD_HILINK_ERR, -ENOLINK },
		{ HCLGE_COMM_CMD_QUEUE_ILLEGAL, -ENXIO },
		{ HCLGE_COMM_CMD_INVALID, -EBADR },
	};
	u32 errcode_count = ARRAY_SIZE(hclge_comm_cmd_errcode);
	u32 i;

	for (i = 0; i < errcode_count; i++)
		if (hclge_comm_cmd_errcode[i].imp_errcode == desc_ret)
			return hclge_comm_cmd_errcode[i].common_errno;

	return -EIO;
}

static int hclge_comm_cmd_check_retval(struct hclge_comm_hw *hw,
				       struct hclge_desc *desc, int num,
				       int ntc)
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
	if (likely(!hclge_comm_is_special_opcode(opcode)))
		desc_ret = le16_to_cpu(desc[num - 1].retval);
	else
		desc_ret = le16_to_cpu(desc[0].retval);

	hw->cmq.last_status = desc_ret;

	return hclge_comm_cmd_convert_err_code(desc_ret);
}

static int hclge_comm_cmd_check_result(struct hclge_comm_hw *hw,
				       struct hclge_desc *desc,
				       int num, int ntc)
{
	bool is_completed = false;
	int handle, ret;

	/* If the command is sync, wait for the firmware to write back,
	 * if multi descriptors to be sent, use the first one to check
	 */
	if (HCLGE_COMM_SEND_SYNC(le16_to_cpu(desc->flag)))
		hclge_comm_wait_for_resp(hw, le16_to_cpu(desc->opcode),
					 &is_completed);

	if (!is_completed)
		ret = -EBADE;
	else
		ret = hclge_comm_cmd_check_retval(hw, desc, num, ntc);

	/* Clean the command send queue */
	handle = hclge_comm_cmd_csq_clean(hw);
	if (handle < 0)
		ret = handle;
	else if (handle != num)
		dev_warn(&hw->cmq.csq.pdev->dev,
			 "cleaned %d, need to clean %d\n", handle, num);
	return ret;
}

/**
 * hclge_comm_cmd_send - send command to command queue
 * @hw: pointer to the hw struct
 * @desc: prefilled descriptor for describing the command
 * @num : the number of descriptors to be sent
 *
 * This is the main send command for command queue, it
 * sends the queue, cleans the queue, etc
 **/
int hclge_comm_cmd_send(struct hclge_comm_hw *hw, struct hclge_desc *desc,
			int num)
{
	bool is_special = hclge_comm_is_special_opcode(le16_to_cpu(desc->opcode));
	struct hclge_comm_cmq_ring *csq = &hw->cmq.csq;
	int ret;
	int ntc;

	if (hw->cmq.ops.trace_cmd_send)
		hw->cmq.ops.trace_cmd_send(hw, desc, num, is_special);

	spin_lock_bh(&hw->cmq.csq.lock);

	if (test_bit(HCLGE_COMM_STATE_CMD_DISABLE, &hw->comm_state)) {
		spin_unlock_bh(&hw->cmq.csq.lock);
		return -EBUSY;
	}

	if (num > hclge_comm_ring_space(&hw->cmq.csq)) {
		/* If CMDQ ring is full, SW HEAD and HW HEAD may be different,
		 * need update the SW HEAD pointer csq->next_to_clean
		 */
		csq->next_to_clean =
			hclge_comm_read_dev(hw, HCLGE_COMM_NIC_CSQ_HEAD_REG);
		spin_unlock_bh(&hw->cmq.csq.lock);
		return -EBUSY;
	}

	/**
	 * Record the location of desc in the ring for this time
	 * which will be use for hardware to write back
	 */
	ntc = hw->cmq.csq.next_to_use;

	hclge_comm_cmd_copy_desc(hw, desc, num);

	/* Write to hardware */
	hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CSQ_TAIL_REG,
			     hw->cmq.csq.next_to_use);

	ret = hclge_comm_cmd_check_result(hw, desc, num, ntc);

	spin_unlock_bh(&hw->cmq.csq.lock);

	if (hw->cmq.ops.trace_cmd_get)
		hw->cmq.ops.trace_cmd_get(hw, desc, num, is_special);

	return ret;
}
EXPORT_SYMBOL_GPL(hclge_comm_cmd_send);

static void hclge_comm_cmd_uninit_regs(struct hclge_comm_hw *hw)
{
	hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CSQ_BASEADDR_L_REG, 0);
	hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CSQ_BASEADDR_H_REG, 0);
	hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CSQ_DEPTH_REG, 0);
	hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CSQ_HEAD_REG, 0);
	hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CSQ_TAIL_REG, 0);
	hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CRQ_BASEADDR_L_REG, 0);
	hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CRQ_BASEADDR_H_REG, 0);
	hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CRQ_DEPTH_REG, 0);
	hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CRQ_HEAD_REG, 0);
	hclge_comm_write_dev(hw, HCLGE_COMM_NIC_CRQ_TAIL_REG, 0);
}

void hclge_comm_cmd_uninit(struct hnae3_ae_dev *ae_dev,
			   struct hclge_comm_hw *hw)
{
	struct hclge_comm_cmq *cmdq = &hw->cmq;

	hclge_comm_firmware_compat_config(ae_dev, hw, false);
	set_bit(HCLGE_COMM_STATE_CMD_DISABLE, &hw->comm_state);

	/* wait to ensure that the firmware completes the possible left
	 * over commands.
	 */
	msleep(HCLGE_COMM_CMDQ_CLEAR_WAIT_TIME);
	spin_lock_bh(&cmdq->csq.lock);
	spin_lock(&cmdq->crq.lock);
	hclge_comm_cmd_uninit_regs(hw);
	spin_unlock(&cmdq->crq.lock);
	spin_unlock_bh(&cmdq->csq.lock);

	hclge_comm_free_cmd_desc(&cmdq->csq);
	hclge_comm_free_cmd_desc(&cmdq->crq);
}
EXPORT_SYMBOL_GPL(hclge_comm_cmd_uninit);

int hclge_comm_cmd_queue_init(struct pci_dev *pdev, struct hclge_comm_hw *hw)
{
	struct hclge_comm_cmq *cmdq = &hw->cmq;
	int ret;

	/* Setup the lock for command queue */
	spin_lock_init(&cmdq->csq.lock);
	spin_lock_init(&cmdq->crq.lock);

	cmdq->csq.pdev = pdev;
	cmdq->crq.pdev = pdev;

	/* Setup the queue entries for use cmd queue */
	cmdq->csq.desc_num = HCLGE_COMM_NIC_CMQ_DESC_NUM;
	cmdq->crq.desc_num = HCLGE_COMM_NIC_CMQ_DESC_NUM;

	/* Setup Tx write back timeout */
	cmdq->tx_timeout = HCLGE_COMM_CMDQ_TX_TIMEOUT_DEFAULT;

	/* Setup queue rings */
	ret = hclge_comm_alloc_cmd_queue(hw, HCLGE_COMM_TYPE_CSQ);
	if (ret) {
		dev_err(&pdev->dev, "CSQ ring setup error %d\n", ret);
		return ret;
	}

	ret = hclge_comm_alloc_cmd_queue(hw, HCLGE_COMM_TYPE_CRQ);
	if (ret) {
		dev_err(&pdev->dev, "CRQ ring setup error %d\n", ret);
		goto err_csq;
	}

	return 0;
err_csq:
	hclge_comm_free_cmd_desc(&hw->cmq.csq);
	return ret;
}
EXPORT_SYMBOL_GPL(hclge_comm_cmd_queue_init);

void hclge_comm_cmd_init_ops(struct hclge_comm_hw *hw,
			     const struct hclge_comm_cmq_ops *ops)
{
	struct hclge_comm_cmq *cmdq = &hw->cmq;

	if (ops) {
		cmdq->ops.trace_cmd_send = ops->trace_cmd_send;
		cmdq->ops.trace_cmd_get = ops->trace_cmd_get;
	}
}
EXPORT_SYMBOL_GPL(hclge_comm_cmd_init_ops);

int hclge_comm_cmd_init(struct hnae3_ae_dev *ae_dev, struct hclge_comm_hw *hw,
			u32 *fw_version, bool is_pf,
			unsigned long reset_pending)
{
	struct hclge_comm_cmq *cmdq = &hw->cmq;
	int ret;

	spin_lock_bh(&cmdq->csq.lock);
	spin_lock(&cmdq->crq.lock);

	cmdq->csq.next_to_clean = 0;
	cmdq->csq.next_to_use = 0;
	cmdq->crq.next_to_clean = 0;
	cmdq->crq.next_to_use = 0;

	hclge_comm_cmd_init_regs(hw);

	spin_unlock(&cmdq->crq.lock);
	spin_unlock_bh(&cmdq->csq.lock);

	clear_bit(HCLGE_COMM_STATE_CMD_DISABLE, &hw->comm_state);

	/* Check if there is new reset pending, because the higher level
	 * reset may happen when lower level reset is being processed.
	 */
	if (reset_pending) {
		ret = -EBUSY;
		goto err_cmd_init;
	}

	/* get version and device capabilities */
	ret = hclge_comm_cmd_query_version_and_capability(ae_dev, hw,
							  fw_version, is_pf);
	if (ret) {
		dev_err(&ae_dev->pdev->dev,
			"failed to query version and capabilities, ret = %d\n",
			ret);
		goto err_cmd_init;
	}

	dev_info(&ae_dev->pdev->dev,
		 "The firmware version is %lu.%lu.%lu.%lu\n",
		 hnae3_get_field(*fw_version, HNAE3_FW_VERSION_BYTE3_MASK,
				 HNAE3_FW_VERSION_BYTE3_SHIFT),
		 hnae3_get_field(*fw_version, HNAE3_FW_VERSION_BYTE2_MASK,
				 HNAE3_FW_VERSION_BYTE2_SHIFT),
		 hnae3_get_field(*fw_version, HNAE3_FW_VERSION_BYTE1_MASK,
				 HNAE3_FW_VERSION_BYTE1_SHIFT),
		 hnae3_get_field(*fw_version, HNAE3_FW_VERSION_BYTE0_MASK,
				 HNAE3_FW_VERSION_BYTE0_SHIFT));

	if (!is_pf && ae_dev->dev_version < HNAE3_DEVICE_VERSION_V3)
		return 0;

	/* ask the firmware to enable some features, driver can work without
	 * it.
	 */
	ret = hclge_comm_firmware_compat_config(ae_dev, hw, true);
	if (ret)
		dev_warn(&ae_dev->pdev->dev,
			 "Firmware compatible features not enabled(%d).\n",
			 ret);
	return 0;

err_cmd_init:
	set_bit(HCLGE_COMM_STATE_CMD_DISABLE, &hw->comm_state);

	return ret;
}
EXPORT_SYMBOL_GPL(hclge_comm_cmd_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HNS3: Hisilicon Ethernet PF/VF Common Library");
MODULE_AUTHOR("Huawei Tech. Co., Ltd.");
