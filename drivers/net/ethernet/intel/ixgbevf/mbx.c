// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 1999 - 2018 Intel Corporation. */

#include "mbx.h"
#include "ixgbevf.h"

/**
 *  ixgbevf_poll_for_msg - Wait for message notification
 *  @hw: pointer to the HW structure
 *
 *  returns 0 if it successfully received a message notification
 **/
static s32 ixgbevf_poll_for_msg(struct ixgbe_hw *hw)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int countdown = mbx->timeout;

	while (countdown && mbx->ops.check_for_msg(hw)) {
		countdown--;
		udelay(mbx->udelay);
	}

	/* if we failed, all future posted messages fail until reset */
	if (!countdown)
		mbx->timeout = 0;

	return countdown ? 0 : IXGBE_ERR_MBX;
}

/**
 *  ixgbevf_poll_for_ack - Wait for message acknowledgment
 *  @hw: pointer to the HW structure
 *
 *  returns 0 if it successfully received a message acknowledgment
 **/
static s32 ixgbevf_poll_for_ack(struct ixgbe_hw *hw)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int countdown = mbx->timeout;

	while (countdown && mbx->ops.check_for_ack(hw)) {
		countdown--;
		udelay(mbx->udelay);
	}

	/* if we failed, all future posted messages fail until reset */
	if (!countdown)
		mbx->timeout = 0;

	return countdown ? 0 : IXGBE_ERR_MBX;
}

/**
 *  ixgbevf_read_posted_mbx - Wait for message notification and receive message
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *
 *  returns 0 if it successfully received a message notification and
 *  copied it into the receive buffer.
 **/
static s32 ixgbevf_read_posted_mbx(struct ixgbe_hw *hw, u32 *msg, u16 size)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	s32 ret_val = IXGBE_ERR_MBX;

	if (!mbx->ops.read)
		goto out;

	ret_val = ixgbevf_poll_for_msg(hw);

	/* if ack received read message, otherwise we timed out */
	if (!ret_val)
		ret_val = mbx->ops.read(hw, msg, size);
out:
	return ret_val;
}

/**
 *  ixgbevf_write_posted_mbx - Write a message to the mailbox, wait for ack
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *
 *  returns 0 if it successfully copied message into the buffer and
 *  received an ack to that message within delay * timeout period
 **/
static s32 ixgbevf_write_posted_mbx(struct ixgbe_hw *hw, u32 *msg, u16 size)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	s32 ret_val = IXGBE_ERR_MBX;

	/* exit if either we can't write or there isn't a defined timeout */
	if (!mbx->ops.write || !mbx->timeout)
		goto out;

	/* send msg */
	ret_val = mbx->ops.write(hw, msg, size);

	/* if msg sent wait until we receive an ack */
	if (!ret_val)
		ret_val = ixgbevf_poll_for_ack(hw);
out:
	return ret_val;
}

/**
 *  ixgbevf_read_v2p_mailbox - read v2p mailbox
 *  @hw: pointer to the HW structure
 *
 *  This function is used to read the v2p mailbox without losing the read to
 *  clear status bits.
 **/
static u32 ixgbevf_read_v2p_mailbox(struct ixgbe_hw *hw)
{
	u32 v2p_mailbox = IXGBE_READ_REG(hw, IXGBE_VFMAILBOX);

	v2p_mailbox |= hw->mbx.v2p_mailbox;
	hw->mbx.v2p_mailbox |= v2p_mailbox & IXGBE_VFMAILBOX_R2C_BITS;

	return v2p_mailbox;
}

/**
 *  ixgbevf_check_for_bit_vf - Determine if a status bit was set
 *  @hw: pointer to the HW structure
 *  @mask: bitmask for bits to be tested and cleared
 *
 *  This function is used to check for the read to clear bits within
 *  the V2P mailbox.
 **/
static s32 ixgbevf_check_for_bit_vf(struct ixgbe_hw *hw, u32 mask)
{
	u32 v2p_mailbox = ixgbevf_read_v2p_mailbox(hw);
	s32 ret_val = IXGBE_ERR_MBX;

	if (v2p_mailbox & mask)
		ret_val = 0;

	hw->mbx.v2p_mailbox &= ~mask;

	return ret_val;
}

/**
 *  ixgbevf_check_for_msg_vf - checks to see if the PF has sent mail
 *  @hw: pointer to the HW structure
 *
 *  returns 0 if the PF has set the Status bit or else ERR_MBX
 **/
static s32 ixgbevf_check_for_msg_vf(struct ixgbe_hw *hw)
{
	s32 ret_val = IXGBE_ERR_MBX;

	if (!ixgbevf_check_for_bit_vf(hw, IXGBE_VFMAILBOX_PFSTS)) {
		ret_val = 0;
		hw->mbx.stats.reqs++;
	}

	return ret_val;
}

/**
 *  ixgbevf_check_for_ack_vf - checks to see if the PF has ACK'd
 *  @hw: pointer to the HW structure
 *
 *  returns 0 if the PF has set the ACK bit or else ERR_MBX
 **/
static s32 ixgbevf_check_for_ack_vf(struct ixgbe_hw *hw)
{
	s32 ret_val = IXGBE_ERR_MBX;

	if (!ixgbevf_check_for_bit_vf(hw, IXGBE_VFMAILBOX_PFACK)) {
		ret_val = 0;
		hw->mbx.stats.acks++;
	}

	return ret_val;
}

/**
 *  ixgbevf_check_for_rst_vf - checks to see if the PF has reset
 *  @hw: pointer to the HW structure
 *
 *  returns true if the PF has set the reset done bit or else false
 **/
static s32 ixgbevf_check_for_rst_vf(struct ixgbe_hw *hw)
{
	s32 ret_val = IXGBE_ERR_MBX;

	if (!ixgbevf_check_for_bit_vf(hw, (IXGBE_VFMAILBOX_RSTD |
					   IXGBE_VFMAILBOX_RSTI))) {
		ret_val = 0;
		hw->mbx.stats.rsts++;
	}

	return ret_val;
}

/**
 *  ixgbevf_obtain_mbx_lock_vf - obtain mailbox lock
 *  @hw: pointer to the HW structure
 *
 *  return 0 if we obtained the mailbox lock
 **/
static s32 ixgbevf_obtain_mbx_lock_vf(struct ixgbe_hw *hw)
{
	s32 ret_val = IXGBE_ERR_MBX;

	/* Take ownership of the buffer */
	IXGBE_WRITE_REG(hw, IXGBE_VFMAILBOX, IXGBE_VFMAILBOX_VFU);

	/* reserve mailbox for VF use */
	if (ixgbevf_read_v2p_mailbox(hw) & IXGBE_VFMAILBOX_VFU)
		ret_val = 0;

	return ret_val;
}

/**
 *  ixgbevf_write_mbx_vf - Write a message to the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *
 *  returns 0 if it successfully copied message into the buffer
 **/
static s32 ixgbevf_write_mbx_vf(struct ixgbe_hw *hw, u32 *msg, u16 size)
{
	s32 ret_val;
	u16 i;

	/* lock the mailbox to prevent PF/VF race condition */
	ret_val = ixgbevf_obtain_mbx_lock_vf(hw);
	if (ret_val)
		goto out_no_write;

	/* flush msg and acks as we are overwriting the message buffer */
	ixgbevf_check_for_msg_vf(hw);
	ixgbevf_check_for_ack_vf(hw);

	/* copy the caller specified message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		IXGBE_WRITE_REG_ARRAY(hw, IXGBE_VFMBMEM, i, msg[i]);

	/* update stats */
	hw->mbx.stats.msgs_tx++;

	/* Drop VFU and interrupt the PF to tell it a message has been sent */
	IXGBE_WRITE_REG(hw, IXGBE_VFMAILBOX, IXGBE_VFMAILBOX_REQ);

out_no_write:
	return ret_val;
}

/**
 *  ixgbevf_read_mbx_vf - Reads a message from the inbox intended for VF
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *
 *  returns 0 if it successfully read message from buffer
 **/
static s32 ixgbevf_read_mbx_vf(struct ixgbe_hw *hw, u32 *msg, u16 size)
{
	s32 ret_val = 0;
	u16 i;

	/* lock the mailbox to prevent PF/VF race condition */
	ret_val = ixgbevf_obtain_mbx_lock_vf(hw);
	if (ret_val)
		goto out_no_read;

	/* copy the message from the mailbox memory buffer */
	for (i = 0; i < size; i++)
		msg[i] = IXGBE_READ_REG_ARRAY(hw, IXGBE_VFMBMEM, i);

	/* Acknowledge receipt and release mailbox, then we're done */
	IXGBE_WRITE_REG(hw, IXGBE_VFMAILBOX, IXGBE_VFMAILBOX_ACK);

	/* update stats */
	hw->mbx.stats.msgs_rx++;

out_no_read:
	return ret_val;
}

/**
 *  ixgbevf_init_mbx_params_vf - set initial values for VF mailbox
 *  @hw: pointer to the HW structure
 *
 *  Initializes the hw->mbx struct to correct values for VF mailbox
 */
static s32 ixgbevf_init_mbx_params_vf(struct ixgbe_hw *hw)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;

	/* start mailbox as timed out and let the reset_hw call set the timeout
	 * value to begin communications
	 */
	mbx->timeout = 0;
	mbx->udelay = IXGBE_VF_MBX_INIT_DELAY;

	mbx->size = IXGBE_VFMAILBOX_SIZE;

	mbx->stats.msgs_tx = 0;
	mbx->stats.msgs_rx = 0;
	mbx->stats.reqs = 0;
	mbx->stats.acks = 0;
	mbx->stats.rsts = 0;

	return 0;
}

const struct ixgbe_mbx_operations ixgbevf_mbx_ops = {
	.init_params	= ixgbevf_init_mbx_params_vf,
	.read		= ixgbevf_read_mbx_vf,
	.write		= ixgbevf_write_mbx_vf,
	.read_posted	= ixgbevf_read_posted_mbx,
	.write_posted	= ixgbevf_write_posted_mbx,
	.check_for_msg	= ixgbevf_check_for_msg_vf,
	.check_for_ack	= ixgbevf_check_for_ack_vf,
	.check_for_rst	= ixgbevf_check_for_rst_vf,
};

/* Mailbox operations when running on Hyper-V.
 * On Hyper-V, PF/VF communication is not through the
 * hardware mailbox; this communication is through
 * a software mediated path.
 * Most mail box operations are noop while running on
 * Hyper-V.
 */
const struct ixgbe_mbx_operations ixgbevf_hv_mbx_ops = {
	.init_params	= ixgbevf_init_mbx_params_vf,
	.check_for_rst	= ixgbevf_check_for_rst_vf,
};
