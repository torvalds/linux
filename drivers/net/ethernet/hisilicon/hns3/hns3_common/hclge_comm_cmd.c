// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2021-2021 Hisilicon Limited.

#include "hnae3.h"
#include "hclge_comm_cmd.h"

static bool hclge_is_elem_in_array(const u16 *spec_opcode, u32 size, u16 opcode)
{
	u32 i;

	for (i = 0; i < size; i++) {
		if (spec_opcode[i] == opcode)
			return true;
	}

	return false;
}

static const u16 pf_spec_opcode[] = { HCLGE_COMM_OPC_STATS_64_BIT,
				      HCLGE_COMM_OPC_STATS_32_BIT,
				      HCLGE_COMM_OPC_STATS_MAC,
				      HCLGE_COMM_OPC_STATS_MAC_ALL,
				      HCLGE_COMM_OPC_QUERY_32_BIT_REG,
				      HCLGE_COMM_OPC_QUERY_64_BIT_REG,
				      HCLGE_COMM_QUERY_CLEAR_MPF_RAS_INT,
				      HCLGE_COMM_QUERY_CLEAR_PF_RAS_INT,
				      HCLGE_COMM_QUERY_CLEAR_ALL_MPF_MSIX_INT,
				      HCLGE_COMM_QUERY_CLEAR_ALL_PF_MSIX_INT,
				      HCLGE_COMM_QUERY_ALL_ERR_INFO };

static const u16 vf_spec_opcode[] = { HCLGE_COMM_OPC_STATS_64_BIT,
				      HCLGE_COMM_OPC_STATS_32_BIT,
				      HCLGE_COMM_OPC_STATS_MAC };

static bool hclge_comm_is_special_opcode(u16 opcode, bool is_pf)
{
	/* these commands have several descriptors,
	 * and use the first one to save opcode and return value
	 */
	const u16 *spec_opcode = is_pf ? pf_spec_opcode : vf_spec_opcode;
	u32 size = is_pf ? ARRAY_SIZE(pf_spec_opcode) :
				ARRAY_SIZE(vf_spec_opcode);

	return hclge_is_elem_in_array(spec_opcode, size, opcode);
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
	return head == hw->cmq.csq.next_to_use;
}

static void hclge_comm_wait_for_resp(struct hclge_comm_hw *hw,
				     bool *is_completed)
{
	u32 timeout = 0;

	do {
		if (hclge_comm_cmd_csq_done(hw)) {
			*is_completed = true;
			break;
		}
		udelay(1);
		timeout++;
	} while (timeout < hw->cmq.tx_timeout);
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
				       int ntc, bool is_pf)
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
	if (likely(!hclge_comm_is_special_opcode(opcode, is_pf)))
		desc_ret = le16_to_cpu(desc[num - 1].retval);
	else
		desc_ret = le16_to_cpu(desc[0].retval);

	hw->cmq.last_status = desc_ret;

	return hclge_comm_cmd_convert_err_code(desc_ret);
}

static int hclge_comm_cmd_check_result(struct hclge_comm_hw *hw,
				       struct hclge_desc *desc,
				       int num, int ntc, bool is_pf)
{
	bool is_completed = false;
	int handle, ret;

	/* If the command is sync, wait for the firmware to write back,
	 * if multi descriptors to be sent, use the first one to check
	 */
	if (HCLGE_COMM_SEND_SYNC(le16_to_cpu(desc->flag)))
		hclge_comm_wait_for_resp(hw, &is_completed);

	if (!is_completed)
		ret = -EBADE;
	else
		ret = hclge_comm_cmd_check_retval(hw, desc, num, ntc, is_pf);

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
 * @is_pf: bool to judge pf/vf module
 *
 * This is the main send command for command queue, it
 * sends the queue, cleans the queue, etc
 **/
int hclge_comm_cmd_send(struct hclge_comm_hw *hw, struct hclge_desc *desc,
			int num, bool is_pf)
{
	struct hclge_comm_cmq_ring *csq = &hw->cmq.csq;
	int ret;
	int ntc;

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

	ret = hclge_comm_cmd_check_result(hw, desc, num, ntc, is_pf);

	spin_unlock_bh(&hw->cmq.csq.lock);

	return ret;
}
