// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2022 - 2025 Mucse Corporation. */

#include <linux/errno.h>
#include <linux/bitfield.h>
#include <linux/iopoll.h>

#include "rnpgbe_mbx.h"

/**
 * mbx_data_rd32 - Reads reg with base mbx->fwpf_shm_base
 * @mbx: pointer to the MBX structure
 * @reg: register offset
 *
 * Return: register value
 **/
static u32 mbx_data_rd32(struct mucse_mbx_info *mbx, u32 reg)
{
	struct mucse_hw *hw = container_of(mbx, struct mucse_hw, mbx);

	return readl(hw->hw_addr + mbx->fwpf_shm_base + reg);
}

/**
 * mbx_data_wr32 - Writes value to reg with base mbx->fwpf_shm_base
 * @mbx: pointer to the MBX structure
 * @reg: register offset
 * @value: value to be written
 *
 **/
static void mbx_data_wr32(struct mucse_mbx_info *mbx, u32 reg, u32 value)
{
	struct mucse_hw *hw = container_of(mbx, struct mucse_hw, mbx);

	writel(value, hw->hw_addr + mbx->fwpf_shm_base + reg);
}

/**
 * mbx_ctrl_rd32 - Reads reg with base mbx->fwpf_ctrl_base
 * @mbx: pointer to the MBX structure
 * @reg: register offset
 *
 * Return: register value
 **/
static u32 mbx_ctrl_rd32(struct mucse_mbx_info *mbx, u32 reg)
{
	struct mucse_hw *hw = container_of(mbx, struct mucse_hw, mbx);

	return readl(hw->hw_addr + mbx->fwpf_ctrl_base + reg);
}

/**
 * mbx_ctrl_wr32 - Writes value to reg with base mbx->fwpf_ctrl_base
 * @mbx: pointer to the MBX structure
 * @reg: register offset
 * @value: value to be written
 *
 **/
static void mbx_ctrl_wr32(struct mucse_mbx_info *mbx, u32 reg, u32 value)
{
	struct mucse_hw *hw = container_of(mbx, struct mucse_hw, mbx);

	writel(value, hw->hw_addr + mbx->fwpf_ctrl_base + reg);
}

/**
 * mucse_mbx_get_lock_pf - Write ctrl and read back lock status
 * @hw: pointer to the HW structure
 *
 * Return: register value after write
 **/
static u32 mucse_mbx_get_lock_pf(struct mucse_hw *hw)
{
	struct mucse_mbx_info *mbx = &hw->mbx;
	u32 reg = MUCSE_MBX_PF2FW_CTRL(mbx);

	mbx_ctrl_wr32(mbx, reg, MUCSE_MBX_PFU);

	return mbx_ctrl_rd32(mbx, reg);
}

/**
 * mucse_obtain_mbx_lock_pf - Obtain mailbox lock
 * @hw: pointer to the HW structure
 *
 * Pair with mucse_release_mbx_lock_pf()
 * This function maybe used in an irq handler.
 *
 * Return: 0 on success, negative errno on failure
 **/
static int mucse_obtain_mbx_lock_pf(struct mucse_hw *hw)
{
	struct mucse_mbx_info *mbx = &hw->mbx;
	u32 val;

	return read_poll_timeout_atomic(mucse_mbx_get_lock_pf,
					val, val & MUCSE_MBX_PFU,
					mbx->delay_us,
					mbx->timeout_us,
					false, hw);
}

/**
 * mucse_release_mbx_lock_pf - Release mailbox lock
 * @hw: pointer to the HW structure
 * @req: send a request or not
 *
 * Pair with mucse_obtain_mbx_lock_pf():
 * - Releases the mailbox lock by clearing MUCSE_MBX_PFU bit
 * - Simultaneously sends the request by setting MUCSE_MBX_REQ bit
 *   if req is true
 * (Both bits are in the same mailbox control register,
 * so operations are combined)
 **/
static void mucse_release_mbx_lock_pf(struct mucse_hw *hw, bool req)
{
	struct mucse_mbx_info *mbx = &hw->mbx;
	u32 reg = MUCSE_MBX_PF2FW_CTRL(mbx);

	mbx_ctrl_wr32(mbx, reg, req ? MUCSE_MBX_REQ : 0);
}

/**
 * mucse_mbx_get_fwreq - Read fw req from reg
 * @mbx: pointer to the mbx structure
 *
 * Return: the fwreq value
 **/
static u16 mucse_mbx_get_fwreq(struct mucse_mbx_info *mbx)
{
	u32 val = mbx_data_rd32(mbx, MUCSE_MBX_FW2PF_CNT);

	return FIELD_GET(GENMASK_U32(15, 0), val);
}

/**
 * mucse_mbx_inc_pf_ack - Increase ack
 * @hw: pointer to the HW structure
 *
 * mucse_mbx_inc_pf_ack reads pf_ack from hw, then writes
 * new value back after increase
 **/
static void mucse_mbx_inc_pf_ack(struct mucse_hw *hw)
{
	struct mucse_mbx_info *mbx = &hw->mbx;
	u16 ack;
	u32 val;

	val = mbx_data_rd32(mbx, MUCSE_MBX_PF2FW_CNT);
	ack = FIELD_GET(GENMASK_U32(31, 16), val);
	ack++;
	val &= ~GENMASK_U32(31, 16);
	val |= FIELD_PREP(GENMASK_U32(31, 16), ack);
	mbx_data_wr32(mbx, MUCSE_MBX_PF2FW_CNT, val);
}

/**
 * mucse_read_mbx_pf - Read a message from the mailbox
 * @hw: pointer to the HW structure
 * @msg: the message buffer
 * @size: length of buffer
 *
 * mucse_read_mbx_pf copies a message from the mbx buffer to the caller's
 * memory buffer. The presumption is that the caller knows that there was
 * a message due to a fw request so no polling for message is needed.
 *
 * Return: 0 on success, negative errno on failure
 **/
static int mucse_read_mbx_pf(struct mucse_hw *hw, u32 *msg, u16 size)
{
	const int size_in_words = size / sizeof(u32);
	struct mucse_mbx_info *mbx = &hw->mbx;
	int err;

	err = mucse_obtain_mbx_lock_pf(hw);
	if (err)
		return err;

	for (int i = 0; i < size_in_words; i++)
		msg[i] = mbx_data_rd32(mbx, MUCSE_MBX_FWPF_SHM + 4 * i);
	/* Hw needs write data_reg at last */
	mbx_data_wr32(mbx, MUCSE_MBX_FWPF_SHM, 0);
	/* flush reqs as we have read this request data */
	hw->mbx.fw_req = mucse_mbx_get_fwreq(mbx);
	mucse_mbx_inc_pf_ack(hw);
	mucse_release_mbx_lock_pf(hw, false);

	return 0;
}

/**
 * mucse_check_for_msg_pf - Check to see if the fw has sent mail
 * @hw: pointer to the HW structure
 *
 * Return: 0 if the fw has set the Status bit or else -EIO
 **/
static int mucse_check_for_msg_pf(struct mucse_hw *hw)
{
	struct mucse_mbx_info *mbx = &hw->mbx;
	u16 fw_req;

	fw_req = mucse_mbx_get_fwreq(mbx);
	/* chip's register is reset to 0 when rc send reset
	 * mbx command. Return -EIO if in this state, others
	 * fw == hw->mbx.fw_req means no new msg.
	 **/
	if (fw_req == 0 || fw_req == hw->mbx.fw_req)
		return -EIO;

	return 0;
}

/**
 * mucse_poll_for_msg - Wait for message notification
 * @hw: pointer to the HW structure
 *
 * Return: 0 on success, negative errno on failure
 **/
static int mucse_poll_for_msg(struct mucse_hw *hw)
{
	struct mucse_mbx_info *mbx = &hw->mbx;
	int val;

	return read_poll_timeout(mucse_check_for_msg_pf,
				 val, !val, mbx->delay_us,
				 mbx->timeout_us,
				 false, hw);
}

/**
 * mucse_poll_and_read_mbx - Wait for message notification and receive message
 * @hw: pointer to the HW structure
 * @msg: the message buffer
 * @size: length of buffer
 *
 * Return: 0 if it successfully received a message notification and
 * copied it into the receive buffer, negative errno on failure
 **/
int mucse_poll_and_read_mbx(struct mucse_hw *hw, u32 *msg, u16 size)
{
	int err;

	err = mucse_poll_for_msg(hw);
	if (err)
		return err;

	return mucse_read_mbx_pf(hw, msg, size);
}

/**
 * mucse_mbx_get_fwack - Read fw ack from reg
 * @mbx: pointer to the MBX structure
 *
 * Return: the fwack value
 **/
static u16 mucse_mbx_get_fwack(struct mucse_mbx_info *mbx)
{
	u32 val = mbx_data_rd32(mbx, MUCSE_MBX_FW2PF_CNT);

	return FIELD_GET(GENMASK_U32(31, 16), val);
}

/**
 * mucse_mbx_inc_pf_req - Increase req
 * @hw: pointer to the HW structure
 *
 * mucse_mbx_inc_pf_req reads pf_req from hw, then writes
 * new value back after increase
 **/
static void mucse_mbx_inc_pf_req(struct mucse_hw *hw)
{
	struct mucse_mbx_info *mbx = &hw->mbx;
	u16 req;
	u32 val;

	val = mbx_data_rd32(mbx, MUCSE_MBX_PF2FW_CNT);
	req = FIELD_GET(GENMASK_U32(15, 0), val);
	req++;
	val &= ~GENMASK_U32(15, 0);
	val |= FIELD_PREP(GENMASK_U32(15, 0), req);
	mbx_data_wr32(mbx, MUCSE_MBX_PF2FW_CNT, val);
}

/**
 * mucse_write_mbx_pf - Place a message in the mailbox
 * @hw: pointer to the HW structure
 * @msg: the message buffer
 * @size: length of buffer
 *
 * Return: 0 if it successfully copied message into the buffer,
 * negative errno on failure
 **/
static int mucse_write_mbx_pf(struct mucse_hw *hw, u32 *msg, u16 size)
{
	const int size_in_words = size / sizeof(u32);
	struct mucse_mbx_info *mbx = &hw->mbx;
	int err;

	err = mucse_obtain_mbx_lock_pf(hw);
	if (err)
		return err;

	for (int i = 0; i < size_in_words; i++)
		mbx_data_wr32(mbx, MUCSE_MBX_FWPF_SHM + i * 4, msg[i]);

	/* flush acks as we are overwriting the message buffer */
	hw->mbx.fw_ack = mucse_mbx_get_fwack(mbx);
	mucse_mbx_inc_pf_req(hw);
	mucse_release_mbx_lock_pf(hw, true);

	return 0;
}

/**
 * mucse_check_for_ack_pf - Check to see if the fw has ACKed
 * @hw: pointer to the HW structure
 *
 * Return: 0 if the fw has set the Status bit or else -EIO
 **/
static int mucse_check_for_ack_pf(struct mucse_hw *hw)
{
	struct mucse_mbx_info *mbx = &hw->mbx;
	u16 fw_ack;

	fw_ack = mucse_mbx_get_fwack(mbx);
	/* chip's register is reset to 0 when rc send reset
	 * mbx command. Return -EIO if in this state, others
	 * fw_ack == hw->mbx.fw_ack means no new ack.
	 **/
	if (fw_ack == 0 || fw_ack == hw->mbx.fw_ack)
		return -EIO;

	return 0;
}

/**
 * mucse_poll_for_ack - Wait for message acknowledgment
 * @hw: pointer to the HW structure
 *
 * Return: 0 if it successfully received a message acknowledgment,
 * else negative errno
 **/
static int mucse_poll_for_ack(struct mucse_hw *hw)
{
	struct mucse_mbx_info *mbx = &hw->mbx;
	int val;

	return read_poll_timeout(mucse_check_for_ack_pf,
				 val, !val, mbx->delay_us,
				 mbx->timeout_us,
				 false, hw);
}

/**
 * mucse_write_and_wait_ack_mbx - Write a message to the mailbox, wait for ack
 * @hw: pointer to the HW structure
 * @msg: the message buffer
 * @size: length of buffer
 *
 * Return: 0 if it successfully copied message into the buffer and
 * received an ack to that message within delay * timeout_cnt period
 **/
int mucse_write_and_wait_ack_mbx(struct mucse_hw *hw, u32 *msg, u16 size)
{
	int err;

	err = mucse_write_mbx_pf(hw, msg, size);
	if (err)
		return err;

	return mucse_poll_for_ack(hw);
}

/**
 * mucse_mbx_reset - Reset mbx info, sync info from regs
 * @hw: pointer to the HW structure
 *
 * mucse_mbx_reset resets all mbx variables to default.
 **/
static void mucse_mbx_reset(struct mucse_hw *hw)
{
	struct mucse_mbx_info *mbx = &hw->mbx;
	u32 val;

	val = mbx_data_rd32(mbx, MUCSE_MBX_FW2PF_CNT);
	hw->mbx.fw_req = FIELD_GET(GENMASK_U32(15, 0), val);
	hw->mbx.fw_ack = FIELD_GET(GENMASK_U32(31, 16), val);
	mbx_ctrl_wr32(mbx, MUCSE_MBX_PF2FW_CTRL(mbx), 0);
	mbx_ctrl_wr32(mbx, MUCSE_MBX_FWPF_MASK(mbx), GENMASK_U32(31, 16));
}

/**
 * mucse_init_mbx_params_pf - Set initial values for pf mailbox
 * @hw: pointer to the HW structure
 *
 * Initializes the hw->mbx struct to correct values for pf mailbox
 */
void mucse_init_mbx_params_pf(struct mucse_hw *hw)
{
	struct mucse_mbx_info *mbx = &hw->mbx;

	mbx->delay_us = 100;
	mbx->timeout_us = 4 * USEC_PER_SEC;
	mutex_init(&mbx->lock);
	mucse_mbx_reset(hw);
}
