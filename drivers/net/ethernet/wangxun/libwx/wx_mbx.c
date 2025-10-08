// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#include <linux/pci.h>
#include "wx_type.h"
#include "wx_mbx.h"

/**
 *  wx_obtain_mbx_lock_pf - obtain mailbox lock
 *  @wx: pointer to the HW structure
 *  @vf: the VF index
 *
 *  Return: return 0 on success and -EBUSY on failure
 **/
static int wx_obtain_mbx_lock_pf(struct wx *wx, u16 vf)
{
	int count = 5;
	u32 mailbox;

	while (count--) {
		/* Take ownership of the buffer */
		wr32(wx, WX_PXMAILBOX(vf), WX_PXMAILBOX_PFU);

		/* reserve mailbox for vf use */
		mailbox = rd32(wx, WX_PXMAILBOX(vf));
		if (mailbox & WX_PXMAILBOX_PFU)
			return 0;
		else if (count)
			udelay(10);
	}
	wx_err(wx, "Failed to obtain mailbox lock for PF%d", vf);

	return -EBUSY;
}

static int wx_check_for_bit_pf(struct wx *wx, u32 mask, int index)
{
	u32 mbvficr = rd32(wx, WX_MBVFICR(index));

	if (!(mbvficr & mask))
		return -EBUSY;
	wr32(wx, WX_MBVFICR(index), mask);

	return 0;
}

/**
 *  wx_check_for_ack_pf - checks to see if the VF has acked
 *  @wx: pointer to the HW structure
 *  @vf: the VF index
 *
 *  Return: return 0 if the VF has set the status bit or else -EBUSY
 **/
int wx_check_for_ack_pf(struct wx *wx, u16 vf)
{
	u32 index = vf / 16, vf_bit = vf % 16;

	return wx_check_for_bit_pf(wx,
				   FIELD_PREP(WX_MBVFICR_VFACK_MASK,
					      BIT(vf_bit)),
				   index);
}

/**
 *  wx_check_for_msg_pf - checks to see if the VF has sent mail
 *  @wx: pointer to the HW structure
 *  @vf: the VF index
 *
 *  Return: return 0 if the VF has got req bit or else -EBUSY
 **/
int wx_check_for_msg_pf(struct wx *wx, u16 vf)
{
	u32 index = vf / 16, vf_bit = vf % 16;

	return wx_check_for_bit_pf(wx,
				   FIELD_PREP(WX_MBVFICR_VFREQ_MASK,
					      BIT(vf_bit)),
				   index);
}

/**
 *  wx_write_mbx_pf - Places a message in the mailbox
 *  @wx: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @vf: the VF index
 *
 *  Return: return 0 on success and -EINVAL/-EBUSY on failure
 **/
int wx_write_mbx_pf(struct wx *wx, u32 *msg, u16 size, u16 vf)
{
	struct wx_mbx_info *mbx = &wx->mbx;
	int ret, i;

	/* mbx->size is up to 15 */
	if (size > mbx->size) {
		wx_err(wx, "Invalid mailbox message size %d", size);
		return -EINVAL;
	}

	/* lock the mailbox to prevent pf/vf race condition */
	ret = wx_obtain_mbx_lock_pf(wx, vf);
	if (ret)
		return ret;

	/* flush msg and acks as we are overwriting the message buffer */
	wx_check_for_msg_pf(wx, vf);
	wx_check_for_ack_pf(wx, vf);

	/* copy the caller specified message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		wr32a(wx, WX_PXMBMEM(vf), i, msg[i]);

	/* Interrupt VF to tell it a message has been sent and release buffer */
	/* set mirrored mailbox flags */
	wr32a(wx, WX_PXMBMEM(vf), WX_VXMAILBOX_SIZE, WX_PXMAILBOX_STS);
	wr32(wx, WX_PXMAILBOX(vf), WX_PXMAILBOX_STS);

	return 0;
}

/**
 *  wx_read_mbx_pf - Read a message from the mailbox
 *  @wx: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @vf: the VF index
 *
 *  Return: return 0 on success and -EBUSY on failure
 **/
int wx_read_mbx_pf(struct wx *wx, u32 *msg, u16 size, u16 vf)
{
	struct wx_mbx_info *mbx = &wx->mbx;
	int ret;
	u16 i;

	/* limit read to size of mailbox and mbx->size is up to 15 */
	if (size > mbx->size)
		size = mbx->size;

	/* lock the mailbox to prevent pf/vf race condition */
	ret = wx_obtain_mbx_lock_pf(wx, vf);
	if (ret)
		return ret;

	for (i = 0; i < size; i++)
		msg[i] = rd32a(wx, WX_PXMBMEM(vf), i);

	/* Acknowledge the message and release buffer */
	/* set mirrored mailbox flags */
	wr32a(wx, WX_PXMBMEM(vf), WX_VXMAILBOX_SIZE, WX_PXMAILBOX_ACK);
	wr32(wx, WX_PXMAILBOX(vf), WX_PXMAILBOX_ACK);

	return 0;
}

/**
 *  wx_check_for_rst_pf - checks to see if the VF has reset
 *  @wx: pointer to the HW structure
 *  @vf: the VF index
 *
 *  Return: return 0 on success and -EBUSY on failure
 **/
int wx_check_for_rst_pf(struct wx *wx, u16 vf)
{
	u32 reg_offset = WX_VF_REG_OFFSET(vf);
	u32 vf_shift = WX_VF_IND_SHIFT(vf);
	u32 vflre = 0;

	vflre = rd32(wx, WX_VFLRE(reg_offset));
	if (!(vflre & BIT(vf_shift)))
		return -EBUSY;
	wr32(wx, WX_VFLREC(reg_offset), BIT(vf_shift));

	return 0;
}

static u32 wx_read_v2p_mailbox(struct wx *wx)
{
	u32 mailbox = rd32(wx, WX_VXMAILBOX);

	mailbox |= wx->mbx.mailbox;
	wx->mbx.mailbox |= mailbox & WX_VXMAILBOX_R2C_BITS;

	return mailbox;
}

static u32 wx_mailbox_get_lock_vf(struct wx *wx)
{
	wr32(wx, WX_VXMAILBOX, WX_VXMAILBOX_VFU);
	return wx_read_v2p_mailbox(wx);
}

/**
 *  wx_obtain_mbx_lock_vf - obtain mailbox lock
 *  @wx: pointer to the HW structure
 *
 *  Return: return 0 on success and -EBUSY on failure
 **/
static int wx_obtain_mbx_lock_vf(struct wx *wx)
{
	int count = 5, ret;
	u32 mailbox;

	ret = readx_poll_timeout_atomic(wx_mailbox_get_lock_vf, wx, mailbox,
					(mailbox & WX_VXMAILBOX_VFU),
					1, count);
	if (ret)
		wx_err(wx, "Failed to obtain mailbox lock for VF.\n");

	return ret;
}

static int wx_check_for_bit_vf(struct wx *wx, u32 mask)
{
	u32 mailbox = wx_read_v2p_mailbox(wx);

	wx->mbx.mailbox &= ~mask;

	return (mailbox & mask ? 0 : -EBUSY);
}

/**
 *  wx_check_for_ack_vf - checks to see if the PF has ACK'd
 *  @wx: pointer to the HW structure
 *
 *  Return: return 0 if the PF has set the status bit or else -EBUSY
 **/
static int wx_check_for_ack_vf(struct wx *wx)
{
	/* read clear the pf ack bit */
	return wx_check_for_bit_vf(wx, WX_VXMAILBOX_PFACK);
}

/**
 *  wx_check_for_msg_vf - checks to see if the PF has sent mail
 *  @wx: pointer to the HW structure
 *
 *  Return: return 0 if the PF has got req bit or else -EBUSY
 **/
int wx_check_for_msg_vf(struct wx *wx)
{
	/* read clear the pf sts bit */
	return wx_check_for_bit_vf(wx, WX_VXMAILBOX_PFSTS);
}

/**
 *  wx_check_for_rst_vf - checks to see if the PF has reset
 *  @wx: pointer to the HW structure
 *
 *  Return: return 0 if the PF has set the reset done and -EBUSY on failure
 **/
int wx_check_for_rst_vf(struct wx *wx)
{
	/* read clear the pf reset done bit */
	return wx_check_for_bit_vf(wx,
				   WX_VXMAILBOX_RSTD |
				   WX_VXMAILBOX_RSTI);
}

/**
 *  wx_poll_for_msg - Wait for message notification
 *  @wx: pointer to the HW structure
 *
 *  Return: return 0 if the VF has successfully received a message notification
 **/
static int wx_poll_for_msg(struct wx *wx)
{
	struct wx_mbx_info *mbx = &wx->mbx;
	u32 val;

	return readx_poll_timeout_atomic(wx_check_for_msg_vf, wx, val,
					 (val == 0), mbx->udelay, mbx->timeout);
}

/**
 *  wx_poll_for_ack - Wait for message acknowledgment
 *  @wx: pointer to the HW structure
 *
 *  Return: return 0 if the VF has successfully received a message ack
 **/
static int wx_poll_for_ack(struct wx *wx)
{
	struct wx_mbx_info *mbx = &wx->mbx;
	u32 val;

	return readx_poll_timeout_atomic(wx_check_for_ack_vf, wx, val,
					 (val == 0), mbx->udelay, mbx->timeout);
}

/**
 *  wx_read_posted_mbx - Wait for message notification and receive message
 *  @wx: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *
 *  Return: returns 0 if it successfully received a message notification and
 *  copied it into the receive buffer.
 **/
int wx_read_posted_mbx(struct wx *wx, u32 *msg, u16 size)
{
	int ret;

	ret = wx_poll_for_msg(wx);
	/* if ack received read message, otherwise we timed out */
	if (ret)
		return ret;

	return wx_read_mbx_vf(wx, msg, size);
}

/**
 *  wx_write_posted_mbx - Write a message to the mailbox, wait for ack
 *  @wx: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *
 *  Return: returns 0 if it successfully copied message into the buffer and
 *  received an ack to that message within delay * timeout period
 **/
int wx_write_posted_mbx(struct wx *wx, u32 *msg, u16 size)
{
	int ret;

	/* send msg */
	ret = wx_write_mbx_vf(wx, msg, size);
	/* if msg sent wait until we receive an ack */
	if (ret)
		return ret;

	return wx_poll_for_ack(wx);
}

/**
 *  wx_write_mbx_vf - Write a message to the mailbox
 *  @wx: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *
 *  Return: returns 0 if it successfully copied message into the buffer
 **/
int wx_write_mbx_vf(struct wx *wx, u32 *msg, u16 size)
{
	struct wx_mbx_info *mbx = &wx->mbx;
	int ret, i;

	/* mbx->size is up to 15 */
	if (size > mbx->size) {
		wx_err(wx, "Invalid mailbox message size %d", size);
		return -EINVAL;
	}

	/* lock the mailbox to prevent pf/vf race condition */
	ret = wx_obtain_mbx_lock_vf(wx);
	if (ret)
		return ret;

	/* flush msg and acks as we are overwriting the message buffer */
	wx_check_for_msg_vf(wx);
	wx_check_for_ack_vf(wx);

	/* copy the caller specified message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		wr32a(wx, WX_VXMBMEM, i, msg[i]);

	/* Drop VFU and interrupt the PF to tell it a message has been sent */
	wr32(wx, WX_VXMAILBOX, WX_VXMAILBOX_REQ);

	return 0;
}

/**
 *  wx_read_mbx_vf - Reads a message from the inbox intended for vf
 *  @wx: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *
 *  Return: returns 0 if it successfully copied message into the buffer
 **/
int wx_read_mbx_vf(struct wx *wx, u32 *msg, u16 size)
{
	struct wx_mbx_info *mbx = &wx->mbx;
	int ret, i;

	/* limit read to size of mailbox and mbx->size is up to 15 */
	if (size > mbx->size)
		size = mbx->size;

	/* lock the mailbox to prevent pf/vf race condition */
	ret = wx_obtain_mbx_lock_vf(wx);
	if (ret)
		return ret;

	/* copy the message from the mailbox memory buffer */
	for (i = 0; i < size; i++)
		msg[i] = rd32a(wx, WX_VXMBMEM, i);

	/* Acknowledge receipt and release mailbox, then we're done */
	wr32(wx, WX_VXMAILBOX, WX_VXMAILBOX_ACK);

	return 0;
}

int wx_init_mbx_params_vf(struct wx *wx)
{
	wx->vfinfo = kzalloc(sizeof(struct vf_data_storage),
			     GFP_KERNEL);
	if (!wx->vfinfo)
		return -ENOMEM;

	/* Initialize mailbox parameters */
	wx->mbx.size = WX_VXMAILBOX_SIZE;
	wx->mbx.mailbox = WX_VXMAILBOX;
	wx->mbx.udelay = 10;
	wx->mbx.timeout = 1000;

	return 0;
}
EXPORT_SYMBOL(wx_init_mbx_params_vf);
