/* 
   BlueCore Serial Protocol (BCSP) for Linux Bluetooth stack (BlueZ).
   Copyright 2002 by Fabrizio Gennari <fabrizio.gennari@philips.com>

   Based on
       hci_h4.c  by Maxim Krasnyansky <maxk@qualcomm.com>
       ABCSP     by Carl Orsborn <cjo@csr.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

/* 
 * $Id: hci_bcsp.h,v 1.2 2002/09/26 05:05:14 maxk Exp $
 */

#ifndef __HCI_BCSP_H__
#define __HCI_BCSP_H__

#define BCSP_TXWINSIZE  4

#define BCSP_ACK_PKT    0x05
#define BCSP_LE_PKT     0x06

struct bcsp_struct {
	struct sk_buff_head unack;	/* Unack'ed packets queue */
	struct sk_buff_head rel;	/* Reliable packets queue */
	struct sk_buff_head unrel;	/* Unreliable packets queue */

	unsigned long rx_count;
	struct  sk_buff *rx_skb;
	u8      rxseq_txack;		/* rxseq == txack. */
	u8      rxack;			/* Last packet sent by us that the peer ack'ed */
	struct  timer_list tbcsp;
	
	enum {
		BCSP_W4_PKT_DELIMITER,
		BCSP_W4_PKT_START,
		BCSP_W4_BCSP_HDR,
		BCSP_W4_DATA,
		BCSP_W4_CRC
	} rx_state;

	enum {
		BCSP_ESCSTATE_NOESC,
		BCSP_ESCSTATE_ESC
	} rx_esc_state;

	u16     message_crc;
	u8      txack_req;		/* Do we need to send ack's to the peer? */

	/* Reliable packet sequence number - used to assign seq to each rel pkt. */
	u8      msgq_txseq;
};

#endif	/* __HCI_BCSP_H__ */
