/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl1251
 *
 * Copyright (c) 1998-2007 Texas Instruments Incorporated
 * Copyright (C) 2008 Nokia Corporation
 */

#ifndef __WL1251_TX_H__
#define __WL1251_TX_H__

#include <linux/bitops.h>
#include "acx.h"

/*
 *
 * TX PATH
 *
 * The Tx path uses a double buffer and a tx_control structure, each located
 * at a fixed address in the device's memory. On startup, the host retrieves
 * the pointers to these addresses. A double buffer allows for continuous data
 * flow towards the device. The host keeps track of which buffer is available
 * and alternates between these two buffers on a per packet basis.
 *
 * The size of each of the two buffers is large enough to hold the longest
 * 802.3 packet - maximum size Ethernet packet + header + descriptor.
 * TX complete indication will be received a-synchronously in a TX done cyclic
 * buffer which is composed of 16 tx_result descriptors structures and is used
 * in a cyclic manner.
 *
 * The TX (HOST) procedure is as follows:
 * 1. Read the Tx path status, that will give the data_out_count.
 * 2. goto 1, if not possible.
 *    i.e. if data_in_count - data_out_count >= HwBuffer size (2 for double
 *    buffer).
 * 3. Copy the packet (preceded by double_buffer_desc), if possible.
 *    i.e. if data_in_count - data_out_count < HwBuffer size (2 for double
 *    buffer).
 * 4. increment data_in_count.
 * 5. Inform the firmware by generating a firmware internal interrupt.
 * 6. FW will increment data_out_count after it reads the buffer.
 *
 * The TX Complete procedure:
 * 1. To get a TX complete indication the host enables the tx_complete flag in
 *    the TX descriptor Structure.
 * 2. For each packet with a Tx Complete field set, the firmware adds the
 *    transmit results to the cyclic buffer (txDoneRing) and sets both done_1
 *    and done_2 to 1 to indicate driver ownership.
 * 3. The firmware sends a Tx Complete interrupt to the host to trigger the
 *    host to process the new data. Note: interrupt will be send per packet if
 *    TX complete indication was requested in tx_control or per crossing
 *    aggregation threshold.
 * 4. After receiving the Tx Complete interrupt, the host reads the
 *    TxDescriptorDone information in a cyclic manner and clears both done_1
 *    and done_2 fields.
 *
 */

#define TX_COMPLETE_REQUIRED_BIT	0x80
#define TX_STATUS_DATA_OUT_COUNT_MASK   0xf

#define WL1251_TX_ALIGN_TO 4
#define WL1251_TX_ALIGN(len) (((len) + WL1251_TX_ALIGN_TO - 1) & \
			     ~(WL1251_TX_ALIGN_TO - 1))
#define WL1251_TKIP_IV_SPACE 4

struct tx_control {
	/* Rate Policy (class) index */
	unsigned rate_policy:3;

	/* When set, no ack policy is expected */
	unsigned ack_policy:1;

	/*
	 * Packet type:
	 * 0 -> 802.11
	 * 1 -> 802.3
	 * 2 -> IP
	 * 3 -> raw codec
	 */
	unsigned packet_type:2;

	/* If set, this is a QoS-Null or QoS-Data frame */
	unsigned qos:1;

	/*
	 * If set, the target triggers the tx complete INT
	 * upon frame sending completion.
	 */
	unsigned tx_complete:1;

	/* 2 bytes padding before packet header */
	unsigned xfer_pad:1;

	unsigned reserved:7;
} __packed;


struct tx_double_buffer_desc {
	/* Length of payload, including headers. */
	__le16 length;

	/*
	 * A bit mask that specifies the initial rate to be used
	 * Possible values are:
	 * 0x0001 - 1Mbits
	 * 0x0002 - 2Mbits
	 * 0x0004 - 5.5Mbits
	 * 0x0008 - 6Mbits
	 * 0x0010 - 9Mbits
	 * 0x0020 - 11Mbits
	 * 0x0040 - 12Mbits
	 * 0x0080 - 18Mbits
	 * 0x0100 - 22Mbits
	 * 0x0200 - 24Mbits
	 * 0x0400 - 36Mbits
	 * 0x0800 - 48Mbits
	 * 0x1000 - 54Mbits
	 */
	__le16 rate;

	/* Time in us that a packet can spend in the target */
	__le32 expiry_time;

	/* index of the TX queue used for this packet */
	u8 xmit_queue;

	/* Used to identify a packet */
	u8 id;

	struct tx_control control;

	/*
	 * The FW should cut the packet into fragments
	 * of this size.
	 */
	__le16 frag_threshold;

	/* Numbers of HW queue blocks to be allocated */
	u8 num_mem_blocks;

	u8 reserved;
} __packed;

enum {
	TX_SUCCESS              = 0,
	TX_DMA_ERROR            = BIT(7),
	TX_DISABLED             = BIT(6),
	TX_RETRY_EXCEEDED       = BIT(5),
	TX_TIMEOUT              = BIT(4),
	TX_KEY_NOT_FOUND        = BIT(3),
	TX_ENCRYPT_FAIL         = BIT(2),
	TX_UNAVAILABLE_PRIORITY = BIT(1),
};

struct tx_result {
	/*
	 * Ownership synchronization between the host and
	 * the firmware. If done_1 and done_2 are cleared,
	 * owned by the FW (no info ready).
	 */
	u8 done_1;

	/* same as double_buffer_desc->id */
	u8 id;

	/*
	 * Total air access duration consumed by this
	 * packet, including all retries and overheads.
	 */
	u16 medium_usage;

	/* Total media delay (from 1st EDCA AIFS counter until TX Complete). */
	u32 medium_delay;

	/* Time between host xfer and tx complete */
	u32 fw_hnadling_time;

	/* The LS-byte of the last TKIP sequence number. */
	u8 lsb_seq_num;

	/* Retry count */
	u8 ack_failures;

	/* At which rate we got a ACK */
	u16 rate;

	u16 reserved;

	/* TX_* */
	u8 status;

	/* See done_1 */
	u8 done_2;
} __packed;

static inline int wl1251_tx_get_queue(int queue)
{
	switch (queue) {
	case 0:
		return QOS_AC_VO;
	case 1:
		return QOS_AC_VI;
	case 2:
		return QOS_AC_BE;
	case 3:
		return QOS_AC_BK;
	default:
		return QOS_AC_BE;
	}
}

void wl1251_tx_work(struct work_struct *work);
void wl1251_tx_complete(struct wl1251 *wl);
void wl1251_tx_flush(struct wl1251 *wl);

#endif
