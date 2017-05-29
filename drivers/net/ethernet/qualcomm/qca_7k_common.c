/*
 *   Copyright (c) 2011, 2012, Atheros Communications Inc.
 *   Copyright (c) 2014, I2SE GmbH
 *
 *   Permission to use, copy, modify, and/or distribute this software
 *   for any purpose with or without fee is hereby granted, provided
 *   that the above copyright notice and this permission notice appear
 *   in all copies.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 *   WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *   WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
 *   THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 *   CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 *   LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 *   NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 *   CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*   Atheros ethernet framing. Every Ethernet frame is surrounded
 *   by an atheros frame while transmitted over a serial channel;
 */

#include <linux/kernel.h>

#include "qca_7k_common.h"

u16
qcafrm_create_header(u8 *buf, u16 length)
{
	__le16 len;

	if (!buf)
		return 0;

	len = cpu_to_le16(length);

	buf[0] = 0xAA;
	buf[1] = 0xAA;
	buf[2] = 0xAA;
	buf[3] = 0xAA;
	buf[4] = len & 0xff;
	buf[5] = (len >> 8) & 0xff;
	buf[6] = 0;
	buf[7] = 0;

	return QCAFRM_HEADER_LEN;
}

u16
qcafrm_create_footer(u8 *buf)
{
	if (!buf)
		return 0;

	buf[0] = 0x55;
	buf[1] = 0x55;
	return QCAFRM_FOOTER_LEN;
}

/*   Gather received bytes and try to extract a full ethernet frame by
 *   following a simple state machine.
 *
 * Return:   QCAFRM_GATHER       No ethernet frame fully received yet.
 *           QCAFRM_NOHEAD       Header expected but not found.
 *           QCAFRM_INVLEN       Atheros frame length is invalid
 *           QCAFRM_NOTAIL       Footer expected but not found.
 *           > 0                 Number of byte in the fully received
 *                               Ethernet frame
 */

s32
qcafrm_fsm_decode(struct qcafrm_handle *handle, u8 *buf, u16 buf_len, u8 recv_byte)
{
	s32 ret = QCAFRM_GATHER;
	u16 len;

	switch (handle->state) {
	case QCAFRM_HW_LEN0:
	case QCAFRM_HW_LEN1:
		/* by default, just go to next state */
		handle->state--;

		if (recv_byte != 0x00) {
			/* first two bytes of length must be 0 */
			handle->state = handle->init;
		}
		break;
	case QCAFRM_HW_LEN2:
	case QCAFRM_HW_LEN3:
		handle->state--;
		break;
	/* 4 bytes header pattern */
	case QCAFRM_WAIT_AA1:
	case QCAFRM_WAIT_AA2:
	case QCAFRM_WAIT_AA3:
	case QCAFRM_WAIT_AA4:
		if (recv_byte != 0xAA) {
			ret = QCAFRM_NOHEAD;
			handle->state = handle->init;
		} else {
			handle->state--;
		}
		break;
		/* 2 bytes length. */
		/* Borrow offset field to hold length for now. */
	case QCAFRM_WAIT_LEN_BYTE0:
		handle->offset = recv_byte;
		handle->state = QCAFRM_WAIT_LEN_BYTE1;
		break;
	case QCAFRM_WAIT_LEN_BYTE1:
		handle->offset = handle->offset | (recv_byte << 8);
		handle->state = QCAFRM_WAIT_RSVD_BYTE1;
		break;
	case QCAFRM_WAIT_RSVD_BYTE1:
		handle->state = QCAFRM_WAIT_RSVD_BYTE2;
		break;
	case QCAFRM_WAIT_RSVD_BYTE2:
		len = handle->offset;
		if (len > buf_len || len < QCAFRM_MIN_LEN) {
			ret = QCAFRM_INVLEN;
			handle->state = handle->init;
		} else {
			handle->state = (enum qcafrm_state)(len + 1);
			/* Remaining number of bytes. */
			handle->offset = 0;
		}
		break;
	default:
		/* Receiving Ethernet frame itself. */
		buf[handle->offset] = recv_byte;
		handle->offset++;
		handle->state--;
		break;
	case QCAFRM_WAIT_551:
		if (recv_byte != 0x55) {
			ret = QCAFRM_NOTAIL;
			handle->state = handle->init;
		} else {
			handle->state = QCAFRM_WAIT_552;
		}
		break;
	case QCAFRM_WAIT_552:
		if (recv_byte != 0x55) {
			ret = QCAFRM_NOTAIL;
			handle->state = handle->init;
		} else {
			ret = handle->offset;
			/* Frame is fully received. */
			handle->state = handle->init;
		}
		break;
	}

	return ret;
}
