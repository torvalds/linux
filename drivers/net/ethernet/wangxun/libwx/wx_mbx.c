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
