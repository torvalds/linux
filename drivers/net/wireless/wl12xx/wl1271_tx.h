/*
 * This file is part of wl1271
 *
 * Copyright (C) 1998-2009 Texas Instruments. All rights reserved.
 * Copyright (C) 2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WL1271_TX_H__
#define __WL1271_TX_H__

#define TX_HW_BLOCK_SPARE                2
#define TX_HW_BLOCK_SIZE                 252

#define TX_HW_MGMT_PKT_LIFETIME_TU       2000
/* The chipset reference driver states, that the "aid" value 1
 * is for infra-BSS, but is still always used */
#define TX_HW_DEFAULT_AID                1

#define TX_HW_ATTR_SAVE_RETRIES          BIT(0)
#define TX_HW_ATTR_HEADER_PAD            BIT(1)
#define TX_HW_ATTR_SESSION_COUNTER       (BIT(2) | BIT(3) | BIT(4))
#define TX_HW_ATTR_RATE_POLICY           (BIT(5) | BIT(6) | BIT(7) | \
					  BIT(8) | BIT(9))
#define TX_HW_ATTR_LAST_WORD_PAD         (BIT(10) | BIT(11))
#define TX_HW_ATTR_TX_CMPLT_REQ          BIT(12)

#define TX_HW_ATTR_OFST_SAVE_RETRIES     0
#define TX_HW_ATTR_OFST_HEADER_PAD       1
#define TX_HW_ATTR_OFST_SESSION_COUNTER  2
#define TX_HW_ATTR_OFST_RATE_POLICY      5
#define TX_HW_ATTR_OFST_LAST_WORD_PAD    10
#define TX_HW_ATTR_OFST_TX_CMPLT_REQ     12

#define TX_HW_RESULT_QUEUE_LEN           16
#define TX_HW_RESULT_QUEUE_LEN_MASK      0xf

#define WL1271_TX_ALIGN_TO 4
#define WL1271_TX_ALIGN(len) (((len) + WL1271_TX_ALIGN_TO - 1) & \
			     ~(WL1271_TX_ALIGN_TO - 1))
#define WL1271_TKIP_IV_SPACE 4

struct wl1271_tx_hw_descr {
	/* Length of packet in words, including descriptor+header+data */
	__le16 length;
	/* Number of extra memory blocks to allocate for this packet in
	   addition to the number of blocks derived from the packet length */
	u8 extra_mem_blocks;
	/* Total number of memory blocks allocated by the host for this packet.
	   Must be equal or greater than the actual blocks number allocated by
	   HW!! */
	u8 total_mem_blocks;
	/* Device time (in us) when the packet arrived to the driver */
	__le32 start_time;
	/* Max delay in TUs until transmission. The last device time the
	   packet can be transmitted is: startTime+(1024*LifeTime) */
	__le16 life_time;
	/* Bitwise fields - see TX_ATTR... definitions above. */
	__le16 tx_attr;
	/* Packet identifier used also in the Tx-Result. */
	u8 id;
	/* The packet TID value (as User-Priority) */
	u8 tid;
	/* Identifier of the remote STA in IBSS, 1 in infra-BSS */
	u8 aid;
	u8 reserved;
} __packed;

enum wl1271_tx_hw_res_status {
	TX_SUCCESS          = 0,
	TX_HW_ERROR         = 1,
	TX_DISABLED         = 2,
	TX_RETRY_EXCEEDED   = 3,
	TX_TIMEOUT          = 4,
	TX_KEY_NOT_FOUND    = 5,
	TX_PEER_NOT_FOUND   = 6,
	TX_SESSION_MISMATCH = 7
};

struct wl1271_tx_hw_res_descr {
	/* Packet Identifier - same value used in the Tx descriptor.*/
	u8 id;
	/* The status of the transmission, indicating success or one of
	   several possible reasons for failure. */
	u8 status;
	/* Total air access duration including all retrys and overheads.*/
	__le16 medium_usage;
	/* The time passed from host xfer to Tx-complete.*/
	__le32 fw_handling_time;
	/* Total media delay
	   (from 1st EDCA AIFS counter until TX Complete). */
	__le32 medium_delay;
	/* LS-byte of last TKIP seq-num (saved per AC for recovery). */
	u8 lsb_security_sequence_number;
	/* Retry count - number of transmissions without successful ACK.*/
	u8 ack_failures;
	/* The rate that succeeded getting ACK
	   (Valid only if status=SUCCESS). */
	u8 rate_class_index;
	/* for 4-byte alignment. */
	u8 spare;
} __packed;

struct wl1271_tx_hw_res_if {
	__le32 tx_result_fw_counter;
	__le32 tx_result_host_counter;
	struct wl1271_tx_hw_res_descr tx_results_queue[TX_HW_RESULT_QUEUE_LEN];
} __packed;

static inline int wl1271_tx_get_queue(int queue)
{
	switch (queue) {
	case 0:
		return CONF_TX_AC_VO;
	case 1:
		return CONF_TX_AC_VI;
	case 2:
		return CONF_TX_AC_BE;
	case 3:
		return CONF_TX_AC_BK;
	default:
		return CONF_TX_AC_BE;
	}
}

/* wl1271 tx descriptor needs the tid and we need to convert it from ac */
static inline int wl1271_tx_ac_to_tid(int ac)
{
	switch (ac) {
	case 0:
		return 0;
	case 1:
		return 2;
	case 2:
		return 4;
	case 3:
		return 6;
	default:
		return 0;
	}
}

void wl1271_tx_work(struct work_struct *work);
void wl1271_tx_complete(struct wl1271 *wl);
void wl1271_tx_reset(struct wl1271 *wl);
void wl1271_tx_flush(struct wl1271 *wl);
u8 wl1271_rate_to_idx(struct wl1271 *wl, int rate);
u32 wl1271_tx_enabled_rates_get(struct wl1271 *wl, u32 rate_set);

#endif
