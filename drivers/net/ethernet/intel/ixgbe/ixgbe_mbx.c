// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 1999 - 2018 Intel Corporation. */

#include <linux/pci.h>
#include <linux/delay.h>
#include "ixgbe.h"
#include "ixgbe_mbx.h"

/**
 *  ixgbe_read_mbx - Reads a message from the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to read
 *
 *  returns SUCCESS if it successfully read message from buffer
 **/
int ixgbe_read_mbx(struct ixgbe_hw *hw, u32 *msg, u16 size, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;

	/* limit read to size of mailbox */
	if (size > mbx->size)
		size = mbx->size;

	if (!mbx->ops)
		return -EIO;

	return mbx->ops->read(hw, msg, size, mbx_id);
}

/**
 *  ixgbe_write_mbx - Write a message to the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully copied message into the buffer
 **/
int ixgbe_write_mbx(struct ixgbe_hw *hw, u32 *msg, u16 size, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;

	if (size > mbx->size)
		return -EINVAL;

	if (!mbx->ops)
		return -EIO;

	return mbx->ops->write(hw, msg, size, mbx_id);
}

/**
 *  ixgbe_check_for_msg - checks to see if someone sent us mail
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns SUCCESS if the Status bit was found or else ERR_MBX
 **/
int ixgbe_check_for_msg(struct ixgbe_hw *hw, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;

	if (!mbx->ops)
		return -EIO;

	return mbx->ops->check_for_msg(hw, mbx_id);
}

/**
 *  ixgbe_check_for_ack - checks to see if someone sent us ACK
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns SUCCESS if the Status bit was found or else ERR_MBX
 **/
int ixgbe_check_for_ack(struct ixgbe_hw *hw, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;

	if (!mbx->ops)
		return -EIO;

	return mbx->ops->check_for_ack(hw, mbx_id);
}

/**
 *  ixgbe_check_for_rst - checks to see if other side has reset
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns SUCCESS if the Status bit was found or else ERR_MBX
 **/
int ixgbe_check_for_rst(struct ixgbe_hw *hw, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;

	if (!mbx->ops)
		return -EIO;

	return mbx->ops->check_for_rst(hw, mbx_id);
}

/**
 *  ixgbe_poll_for_msg - Wait for message notification
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully received a message notification
 **/
static int ixgbe_poll_for_msg(struct ixgbe_hw *hw, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int countdown = mbx->timeout;

	if (!countdown || !mbx->ops)
		return -EIO;

	while (mbx->ops->check_for_msg(hw, mbx_id)) {
		countdown--;
		if (!countdown)
			return -EIO;
		udelay(mbx->usec_delay);
	}

	return 0;
}

/**
 *  ixgbe_poll_for_ack - Wait for message acknowledgement
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully received a message acknowledgement
 **/
static int ixgbe_poll_for_ack(struct ixgbe_hw *hw, u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int countdown = mbx->timeout;

	if (!countdown || !mbx->ops)
		return -EIO;

	while (mbx->ops->check_for_ack(hw, mbx_id)) {
		countdown--;
		if (!countdown)
			return -EIO;
		udelay(mbx->usec_delay);
	}

	return 0;
}

/**
 *  ixgbe_read_posted_mbx - Wait for message notification and receive message
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully received a message notification and
 *  copied it into the receive buffer.
 **/
static int ixgbe_read_posted_mbx(struct ixgbe_hw *hw, u32 *msg, u16 size,
				 u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int ret_val;

	if (!mbx->ops)
		return -EIO;

	ret_val = ixgbe_poll_for_msg(hw, mbx_id);
	if (ret_val)
		return ret_val;

	/* if ack received read message */
	return mbx->ops->read(hw, msg, size, mbx_id);
}

/**
 *  ixgbe_write_posted_mbx - Write a message to the mailbox, wait for ack
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully copied message into the buffer and
 *  received an ack to that message within delay * timeout period
 **/
static int ixgbe_write_posted_mbx(struct ixgbe_hw *hw, u32 *msg, u16 size,
				  u16 mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int ret_val;

	/* exit if either we can't write or there isn't a defined timeout */
	if (!mbx->ops || !mbx->timeout)
		return -EIO;

	/* send msg */
	ret_val = mbx->ops->write(hw, msg, size, mbx_id);
	if (ret_val)
		return ret_val;

	/* if msg sent wait until we receive an ack */
	return ixgbe_poll_for_ack(hw, mbx_id);
}

static int ixgbe_check_for_bit_pf(struct ixgbe_hw *hw, u32 mask, s32 index)
{
	u32 mbvficr = IXGBE_READ_REG(hw, IXGBE_MBVFICR(index));

	if (mbvficr & mask) {
		IXGBE_WRITE_REG(hw, IXGBE_MBVFICR(index), mask);
		return 0;
	}

	return -EIO;
}

/**
 *  ixgbe_check_for_msg_pf - checks to see if the VF has sent mail
 *  @hw: pointer to the HW structure
 *  @vf_number: the VF index
 *
 *  returns SUCCESS if the VF has set the Status bit or else ERR_MBX
 **/
static int ixgbe_check_for_msg_pf(struct ixgbe_hw *hw, u16 vf_number)
{
	int index = IXGBE_MBVFICR_INDEX(vf_number);
	u32 vf_bit = vf_number % 16;

	if (!ixgbe_check_for_bit_pf(hw, IXGBE_MBVFICR_VFREQ_VF1 << vf_bit,
				    index)) {
		hw->mbx.stats.reqs++;
		return 0;
	}

	return -EIO;
}

/**
 *  ixgbe_check_for_ack_pf - checks to see if the VF has ACKed
 *  @hw: pointer to the HW structure
 *  @vf_number: the VF index
 *
 *  returns SUCCESS if the VF has set the Status bit or else ERR_MBX
 **/
static int ixgbe_check_for_ack_pf(struct ixgbe_hw *hw, u16 vf_number)
{
	int index = IXGBE_MBVFICR_INDEX(vf_number);
	u32 vf_bit = vf_number % 16;

	if (!ixgbe_check_for_bit_pf(hw, IXGBE_MBVFICR_VFACK_VF1 << vf_bit,
				    index)) {
		hw->mbx.stats.acks++;
		return 0;
	}

	return -EIO;
}

/**
 *  ixgbe_check_for_rst_pf - checks to see if the VF has reset
 *  @hw: pointer to the HW structure
 *  @vf_number: the VF index
 *
 *  returns SUCCESS if the VF has set the Status bit or else ERR_MBX
 **/
static int ixgbe_check_for_rst_pf(struct ixgbe_hw *hw, u16 vf_number)
{
	u32 reg_offset = (vf_number < 32) ? 0 : 1;
	u32 vf_shift = vf_number % 32;
	u32 vflre = 0;

	switch (hw->mac.type) {
	case ixgbe_mac_82599EB:
		vflre = IXGBE_READ_REG(hw, IXGBE_VFLRE(reg_offset));
		break;
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_x550em_a:
		vflre = IXGBE_READ_REG(hw, IXGBE_VFLREC(reg_offset));
		break;
	default:
		break;
	}

	if (vflre & BIT(vf_shift)) {
		IXGBE_WRITE_REG(hw, IXGBE_VFLREC(reg_offset), BIT(vf_shift));
		hw->mbx.stats.rsts++;
		return 0;
	}

	return -EIO;
}

/**
 *  ixgbe_obtain_mbx_lock_pf - obtain mailbox lock
 *  @hw: pointer to the HW structure
 *  @vf_number: the VF index
 *
 *  return SUCCESS if we obtained the mailbox lock
 **/
static int ixgbe_obtain_mbx_lock_pf(struct ixgbe_hw *hw, u16 vf_number)
{
	u32 p2v_mailbox;

	/* Take ownership of the buffer */
	IXGBE_WRITE_REG(hw, IXGBE_PFMAILBOX(vf_number), IXGBE_PFMAILBOX_PFU);

	/* reserve mailbox for vf use */
	p2v_mailbox = IXGBE_READ_REG(hw, IXGBE_PFMAILBOX(vf_number));
	if (p2v_mailbox & IXGBE_PFMAILBOX_PFU)
		return 0;

	return -EIO;
}

/**
 *  ixgbe_write_mbx_pf - Places a message in the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @vf_number: the VF index
 *
 *  returns SUCCESS if it successfully copied message into the buffer
 **/
static int ixgbe_write_mbx_pf(struct ixgbe_hw *hw, u32 *msg, u16 size,
			      u16 vf_number)
{
	int ret_val;
	u16 i;

	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = ixgbe_obtain_mbx_lock_pf(hw, vf_number);
	if (ret_val)
		return ret_val;

	/* flush msg and acks as we are overwriting the message buffer */
	ixgbe_check_for_msg_pf(hw, vf_number);
	ixgbe_check_for_ack_pf(hw, vf_number);

	/* copy the caller specified message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		IXGBE_WRITE_REG_ARRAY(hw, IXGBE_PFMBMEM(vf_number), i, msg[i]);

	/* Interrupt VF to tell it a message has been sent and release buffer*/
	IXGBE_WRITE_REG(hw, IXGBE_PFMAILBOX(vf_number), IXGBE_PFMAILBOX_STS);

	/* update stats */
	hw->mbx.stats.msgs_tx++;

	return 0;
}

/**
 *  ixgbe_read_mbx_pf - Read a message from the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @vf_number: the VF index
 *
 *  This function copies a message from the mailbox buffer to the caller's
 *  memory buffer.  The presumption is that the caller knows that there was
 *  a message due to a VF request so no polling for message is needed.
 **/
static int ixgbe_read_mbx_pf(struct ixgbe_hw *hw, u32 *msg, u16 size,
			     u16 vf_number)
{
	int ret_val;
	u16 i;

	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = ixgbe_obtain_mbx_lock_pf(hw, vf_number);
	if (ret_val)
		return ret_val;

	/* copy the message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		msg[i] = IXGBE_READ_REG_ARRAY(hw, IXGBE_PFMBMEM(vf_number), i);

	/* Acknowledge the message and release buffer */
	IXGBE_WRITE_REG(hw, IXGBE_PFMAILBOX(vf_number), IXGBE_PFMAILBOX_ACK);

	/* update stats */
	hw->mbx.stats.msgs_rx++;

	return 0;
}

#ifdef CONFIG_PCI_IOV
/**
 *  ixgbe_init_mbx_params_pf - set initial values for pf mailbox
 *  @hw: pointer to the HW structure
 *
 *  Initializes the hw->mbx struct to correct values for pf mailbox
 */
void ixgbe_init_mbx_params_pf(struct ixgbe_hw *hw)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;

	if (hw->mac.type != ixgbe_mac_82599EB &&
	    hw->mac.type != ixgbe_mac_X550 &&
	    hw->mac.type != ixgbe_mac_X550EM_x &&
	    hw->mac.type != ixgbe_mac_x550em_a &&
	    hw->mac.type != ixgbe_mac_X540)
		return;

	mbx->timeout = 0;
	mbx->usec_delay = 0;

	mbx->stats.msgs_tx = 0;
	mbx->stats.msgs_rx = 0;
	mbx->stats.reqs = 0;
	mbx->stats.acks = 0;
	mbx->stats.rsts = 0;

	mbx->size = IXGBE_VFMAILBOX_SIZE;
}
#endif /* CONFIG_PCI_IOV */

const struct ixgbe_mbx_operations mbx_ops_generic = {
	.read                   = ixgbe_read_mbx_pf,
	.write                  = ixgbe_write_mbx_pf,
	.read_posted            = ixgbe_read_posted_mbx,
	.write_posted           = ixgbe_write_posted_mbx,
	.check_for_msg          = ixgbe_check_for_msg_pf,
	.check_for_ack          = ixgbe_check_for_ack_pf,
	.check_for_rst          = ixgbe_check_for_rst_pf,
};

