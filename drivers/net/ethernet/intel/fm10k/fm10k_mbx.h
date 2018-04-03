/* SPDX-License-Identifier: GPL-2.0 */
/* Intel(R) Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#ifndef _FM10K_MBX_H_
#define _FM10K_MBX_H_

/* forward declaration */
struct fm10k_mbx_info;

#include "fm10k_type.h"
#include "fm10k_tlv.h"

/* PF Mailbox Registers */
#define FM10K_MBMEM(_n)		((_n) + 0x18000)
#define FM10K_MBMEM_VF(_n, _m)	(((_n) * 0x10) + (_m) + 0x18000)
#define FM10K_MBMEM_SM(_n)	((_n) + 0x18400)
#define FM10K_MBMEM_PF(_n)	((_n) + 0x18600)
/* XOR provides means of switching from Tx to Rx FIFO */
#define FM10K_MBMEM_PF_XOR	(FM10K_MBMEM_SM(0) ^ FM10K_MBMEM_PF(0))
#define FM10K_MBX(_n)		((_n) + 0x18800)
#define FM10K_MBX_REQ				0x00000002
#define FM10K_MBX_ACK				0x00000004
#define FM10K_MBX_REQ_INTERRUPT			0x00000008
#define FM10K_MBX_ACK_INTERRUPT			0x00000010
#define FM10K_MBX_INTERRUPT_ENABLE		0x00000020
#define FM10K_MBX_INTERRUPT_DISABLE		0x00000040
#define FM10K_MBX_GLOBAL_REQ_INTERRUPT		0x00000200
#define FM10K_MBX_GLOBAL_ACK_INTERRUPT		0x00000400
#define FM10K_MBICR(_n)		((_n) + 0x18840)
#define FM10K_GMBX		0x18842

/* VF Mailbox Registers */
#define FM10K_VFMBX		0x00010
#define FM10K_VFMBMEM(_n)	((_n) + 0x00020)
#define FM10K_VFMBMEM_LEN	16
#define FM10K_VFMBMEM_VF_XOR	(FM10K_VFMBMEM_LEN / 2)

/* Delays/timeouts */
#define FM10K_MBX_DISCONNECT_TIMEOUT		500
#define FM10K_MBX_POLL_DELAY			19
#define FM10K_MBX_INT_DELAY			20

/* PF/VF Mailbox state machine
 *
 * +----------+	    connect()	+----------+
 * |  CLOSED  | --------------> |  CONNECT |
 * +----------+			+----------+
 *   ^				  ^	 |
 *   | rcv:	      rcv:	  |	 | rcv:
 *   |  Connect	       Disconnect |	 |  Connect
 *   |  Disconnect     Error	  |	 |  Data
 *   |				  |	 |
 *   |				  |	 V
 * +----------+   disconnect()	+----------+
 * |DISCONNECT| <-------------- |   OPEN   |
 * +----------+			+----------+
 *
 * The diagram above describes the PF/VF mailbox state machine.  There
 * are four main states to this machine.
 * Closed: This state represents a mailbox that is in a standby state
 *	   with interrupts disabled.  In this state the mailbox should not
 *	   read the mailbox or write any data.  The only means of exiting
 *	   this state is for the system to make the connect() call for the
 *	   mailbox, it will then transition to the connect state.
 * Connect: In this state the mailbox is seeking a connection.  It will
 *	    post a connect message with no specified destination and will
 *	    wait for a reply from the other side of the mailbox.  This state
 *	    is exited when either a connect with the local mailbox as the
 *	    destination is received or when a data message is received with
 *	    a valid sequence number.
 * Open: In this state the mailbox is able to transfer data between the local
 *       entity and the remote.  It will fall back to connect in the event of
 *       receiving either an error message, or a disconnect message.  It will
 *       transition to disconnect on a call to disconnect();
 * Disconnect: In this state the mailbox is attempting to gracefully terminate
 *	       the connection.  It will do so at the first point where it knows
 *	       that the remote endpoint is either done sending, or when the
 *	       remote endpoint has fallen back into connect.
 */
enum fm10k_mbx_state {
	FM10K_STATE_CLOSED,
	FM10K_STATE_CONNECT,
	FM10K_STATE_OPEN,
	FM10K_STATE_DISCONNECT,
};

/* PF/VF Mailbox header format
 *    3			  2		      1			  0
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |        Size/Err_no/CRC        | Rsvd0 | Head  | Tail  | Type  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * The layout above describes the format for the header used in the PF/VF
 * mailbox.  The header is broken out into the following fields:
 * Type: There are 4 supported message types
 *		0x8: Data header - used to transport message data
 *		0xC: Connect header - used to establish connection
 *		0xD: Disconnect header - used to tear down a connection
 *		0xE: Error header - used to address message exceptions
 * Tail: Tail index for local FIFO
 *		Tail index actually consists of two parts.  The MSB of
 *		the head is a loop tracker, it is 0 on an even numbered
 *		loop through the FIFO, and 1 on the odd numbered loops.
 *		To get the actual mailbox offset based on the tail it
 *		is necessary to add bit 3 to bit 0 and clear bit 3.  This
 *		gives us a valid range of 0x1 - 0xE.
 * Head: Head index for remote FIFO
 *		Head index follows the same format as the tail index.
 * Rsvd0: Reserved 0 portion of the mailbox header
 * CRC: Running CRC for all data since connect plus current message header
 * Size: Maximum message size - Applies only to connect headers
 *		The maximum message size is provided during connect to avoid
 *		jamming the mailbox with messages that do not fit.
 * Err_no: Error number - Applies only to error headers
 *		The error number provides an indication of the type of error
 *		experienced.
 */

/* macros for retrieving and setting header values */
#define FM10K_MSG_HDR_MASK(name) \
	((0x1u << FM10K_MSG_##name##_SIZE) - 1)
#define FM10K_MSG_HDR_FIELD_SET(value, name) \
	(((u32)(value) & FM10K_MSG_HDR_MASK(name)) << FM10K_MSG_##name##_SHIFT)
#define FM10K_MSG_HDR_FIELD_GET(value, name) \
	((u16)((value) >> FM10K_MSG_##name##_SHIFT) & FM10K_MSG_HDR_MASK(name))

/* offsets shared between all headers */
#define FM10K_MSG_TYPE_SHIFT			0
#define FM10K_MSG_TYPE_SIZE			4
#define FM10K_MSG_TAIL_SHIFT			4
#define FM10K_MSG_TAIL_SIZE			4
#define FM10K_MSG_HEAD_SHIFT			8
#define FM10K_MSG_HEAD_SIZE			4
#define FM10K_MSG_RSVD0_SHIFT			12
#define FM10K_MSG_RSVD0_SIZE			4

/* offsets for data/disconnect headers */
#define FM10K_MSG_CRC_SHIFT			16
#define FM10K_MSG_CRC_SIZE			16

/* offsets for connect headers */
#define FM10K_MSG_CONNECT_SIZE_SHIFT		16
#define FM10K_MSG_CONNECT_SIZE_SIZE		16

/* offsets for error headers */
#define FM10K_MSG_ERR_NO_SHIFT			16
#define FM10K_MSG_ERR_NO_SIZE			16

enum fm10k_msg_type {
	FM10K_MSG_DATA			= 0x8,
	FM10K_MSG_CONNECT		= 0xC,
	FM10K_MSG_DISCONNECT		= 0xD,
	FM10K_MSG_ERROR			= 0xE,
};

/* HNI/SM Mailbox FIFO format
 *    3                   2                   1                   0
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-------+-----------------------+-------+-----------------------+
 * | Error |      Remote Head      |Version|      Local Tail       |
 * +-------+-----------------------+-------+-----------------------+
 * |                                                               |
 * .                        Local FIFO Data                        .
 * .                                                               .
 * +-------+-----------------------+-------+-----------------------+
 *
 * The layout above describes the format for the FIFOs used by the host
 * network interface and the switch manager to communicate messages back
 * and forth.  Both the HNI and the switch maintain one such FIFO.  The
 * layout in memory has the switch manager FIFO followed immediately by
 * the HNI FIFO.  For this reason I am using just the pointer to the
 * HNI FIFO in the mailbox ops as the offset between the two is fixed.
 *
 * The header for the FIFO is broken out into the following fields:
 * Local Tail:  Offset into FIFO region for next DWORD to write.
 * Version:  Version info for mailbox, only values of 0/1 are supported.
 * Remote Head:  Offset into remote FIFO to indicate how much we have read.
 * Error: Error indication, values TBD.
 */

/* version number for switch manager mailboxes */
#define FM10K_SM_MBX_VERSION		1
#define FM10K_SM_MBX_FIFO_LEN		(FM10K_MBMEM_PF_XOR - 1)

/* offsets shared between all SM FIFO headers */
#define FM10K_MSG_SM_TAIL_SHIFT			0
#define FM10K_MSG_SM_TAIL_SIZE			12
#define FM10K_MSG_SM_VER_SHIFT			12
#define FM10K_MSG_SM_VER_SIZE			4
#define FM10K_MSG_SM_HEAD_SHIFT			16
#define FM10K_MSG_SM_HEAD_SIZE			12
#define FM10K_MSG_SM_ERR_SHIFT			28
#define FM10K_MSG_SM_ERR_SIZE			4

/* All error messages returned by mailbox functions
 * The value -511 is 0xFE01 in hex.  The idea is to order the errors
 * from 0xFE01 - 0xFEFF so error codes are easily visible in the mailbox
 * messages.  This also helps to avoid error number collisions as Linux
 * doesn't appear to use error numbers 256 - 511.
 */
#define FM10K_MBX_ERR(_n) ((_n) - 512)
#define FM10K_MBX_ERR_NO_MBX		FM10K_MBX_ERR(0x01)
#define FM10K_MBX_ERR_NO_SPACE		FM10K_MBX_ERR(0x03)
#define FM10K_MBX_ERR_TAIL		FM10K_MBX_ERR(0x05)
#define FM10K_MBX_ERR_HEAD		FM10K_MBX_ERR(0x06)
#define FM10K_MBX_ERR_SRC		FM10K_MBX_ERR(0x08)
#define FM10K_MBX_ERR_TYPE		FM10K_MBX_ERR(0x09)
#define FM10K_MBX_ERR_SIZE		FM10K_MBX_ERR(0x0B)
#define FM10K_MBX_ERR_BUSY		FM10K_MBX_ERR(0x0C)
#define FM10K_MBX_ERR_RSVD0		FM10K_MBX_ERR(0x0E)
#define FM10K_MBX_ERR_CRC		FM10K_MBX_ERR(0x0F)

#define FM10K_MBX_CRC_SEED		0xFFFF

struct fm10k_mbx_ops {
	s32 (*connect)(struct fm10k_hw *, struct fm10k_mbx_info *);
	void (*disconnect)(struct fm10k_hw *, struct fm10k_mbx_info *);
	bool (*rx_ready)(struct fm10k_mbx_info *);
	bool (*tx_ready)(struct fm10k_mbx_info *, u16);
	bool (*tx_complete)(struct fm10k_mbx_info *);
	s32 (*enqueue_tx)(struct fm10k_hw *, struct fm10k_mbx_info *,
			  const u32 *);
	s32 (*process)(struct fm10k_hw *, struct fm10k_mbx_info *);
	s32 (*register_handlers)(struct fm10k_mbx_info *,
				 const struct fm10k_msg_data *);
};

struct fm10k_mbx_fifo {
	u32 *buffer;
	u16 head;
	u16 tail;
	u16 size;
};

/* size of buffer to be stored in mailbox for FIFOs */
#define FM10K_MBX_TX_BUFFER_SIZE	512
#define FM10K_MBX_RX_BUFFER_SIZE	128
#define FM10K_MBX_BUFFER_SIZE \
	(FM10K_MBX_TX_BUFFER_SIZE + FM10K_MBX_RX_BUFFER_SIZE)

/* minimum and maximum message size in dwords */
#define FM10K_MBX_MSG_MAX_SIZE \
	((FM10K_MBX_TX_BUFFER_SIZE - 1) & (FM10K_MBX_RX_BUFFER_SIZE - 1))
#define FM10K_VFMBX_MSG_MTU	((FM10K_VFMBMEM_LEN / 2) - 1)

#define FM10K_MBX_INIT_TIMEOUT	2000 /* number of retries on mailbox */
#define FM10K_MBX_INIT_DELAY	500  /* microseconds between retries */

struct fm10k_mbx_info {
	/* function pointers for mailbox operations */
	struct fm10k_mbx_ops ops;
	const struct fm10k_msg_data *msg_data;

	/* message FIFOs */
	struct fm10k_mbx_fifo rx;
	struct fm10k_mbx_fifo tx;

	/* delay for handling timeouts */
	u32 timeout;
	u32 udelay;

	/* mailbox state info */
	u32 mbx_reg, mbmem_reg, mbx_lock, mbx_hdr;
	u16 max_size, mbmem_len;
	u16 tail, tail_len, pulled;
	u16 head, head_len, pushed;
	u16 local, remote;
	enum fm10k_mbx_state state;

	/* result of last mailbox test */
	s32 test_result;

	/* statistics */
	u64 tx_busy;
	u64 tx_dropped;
	u64 tx_messages;
	u64 tx_dwords;
	u64 tx_mbmem_pulled;
	u64 rx_messages;
	u64 rx_dwords;
	u64 rx_mbmem_pushed;
	u64 rx_parse_err;

	/* Buffer to store messages */
	u32 buffer[FM10K_MBX_BUFFER_SIZE];
};

s32 fm10k_pfvf_mbx_init(struct fm10k_hw *, struct fm10k_mbx_info *,
			const struct fm10k_msg_data *, u8);
s32 fm10k_sm_mbx_init(struct fm10k_hw *, struct fm10k_mbx_info *,
		      const struct fm10k_msg_data *);

#endif /* _FM10K_MBX_H_ */
