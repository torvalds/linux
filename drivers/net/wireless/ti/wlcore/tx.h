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

#ifndef __TX_H__
#define __TX_H__

#define TX_HW_MGMT_PKT_LIFETIME_TU       2000
#define TX_HW_AP_MODE_PKT_LIFETIME_TU    8000

#define TX_HW_ATTR_SAVE_RETRIES          BIT(0)
#define TX_HW_ATTR_HEADER_PAD            BIT(1)
#define TX_HW_ATTR_SESSION_COUNTER       (BIT(2) | BIT(3) | BIT(4))
#define TX_HW_ATTR_RATE_POLICY           (BIT(5) | BIT(6) | BIT(7) | \
					  BIT(8) | BIT(9))
#define TX_HW_ATTR_LAST_WORD_PAD         (BIT(10) | BIT(11))
#define TX_HW_ATTR_TX_CMPLT_REQ          BIT(12)
#define TX_HW_ATTR_TX_DUMMY_REQ          BIT(13)
#define TX_HW_ATTR_HOST_ENCRYPT          BIT(14)
#define TX_HW_ATTR_EAPOL_FRAME           BIT(15)

#define TX_HW_ATTR_OFST_SAVE_RETRIES     0
#define TX_HW_ATTR_OFST_HEADER_PAD       1
#define TX_HW_ATTR_OFST_SESSION_COUNTER  2
#define TX_HW_ATTR_OFST_RATE_POLICY      5
#define TX_HW_ATTR_OFST_LAST_WORD_PAD    10
#define TX_HW_ATTR_OFST_TX_CMPLT_REQ     12

#define TX_HW_RESULT_QUEUE_LEN           16
#define TX_HW_RESULT_QUEUE_LEN_MASK      0xf

#define WL1271_TX_ALIGN_TO 4
#define WL1271_EXTRA_SPACE_TKIP 4
#define WL1271_EXTRA_SPACE_AES  8
#define WL1271_EXTRA_SPACE_MAX  8

/* Used for management frames and dummy packets */
#define WL1271_TID_MGMT 7

/* stop a ROC for pending authentication reply after this time (ms) */
#define WLCORE_PEND_AUTH_ROC_TIMEOUT     1000

struct wl127x_tx_mem {
	/*
	 * Number of extra memory blocks to allocate for this packet
	 * in addition to the number of blocks derived from the packet
	 * length.
	 */
	u8 extra_blocks;
	/*
	 * Total number of memory blocks allocated by the host for
	 * this packet. Must be equal or greater than the actual
	 * blocks number allocated by HW.
	 */
	u8 total_mem_blocks;
} __packed;

struct wl128x_tx_mem {
	/*
	 * Total number of memory blocks allocated by the host for
	 * this packet.
	 */
	u8 total_mem_blocks;
	/*
	 * Number of extra bytes, at the end of the frame. the host
	 * uses this padding to complete each frame to integer number
	 * of SDIO blocks.
	 */
	u8 extra_bytes;
} __packed;

struct wl18xx_tx_mem {
	/*
	 * Total number of memory blocks allocated by the host for
	 * this packet.
	 */
	u8 total_mem_blocks;

	/*
	 * control bits
	 */
	u8 ctrl;
} __packed;

/*
 * On wl128x based devices, when TX packets are aggregated, each packet
 * size must be aligned to the SDIO block size. The maximum block size
 * is bounded by the type of the padded bytes field that is sent to the
 * FW. Currently the type is u8, so the maximum block size is 256 bytes.
 */
#define WL12XX_BUS_BLOCK_SIZE min(512u,	\
	    (1u << (8 * sizeof(((struct wl128x_tx_mem *) 0)->extra_bytes))))

struct wl1271_tx_hw_descr {
	/* Length of packet in words, including descriptor+header+data */
	__le16 length;
	union {
		struct wl127x_tx_mem wl127x_mem;
		struct wl128x_tx_mem wl128x_mem;
		struct wl18xx_tx_mem wl18xx_mem;
	} __packed;
	/* Device time (in us) when the packet arrived to the driver */
	__le32 start_time;
	/*
	 * Max delay in TUs until transmission. The last device time the
	 * packet can be transmitted is: start_time + (1024 * life_time)
	 */
	__le16 life_time;
	/* Bitwise fields - see TX_ATTR... definitions above. */
	__le16 tx_attr;
	/* Packet identifier used also in the Tx-Result. */
	u8 id;
	/* The packet TID value (as User-Priority) */
	u8 tid;
	/* host link ID (HLID) */
	u8 hlid;

	union {
		u8 wl12xx_reserved;

		/*
		 * bit 0   -> 0 = udp, 1 = tcp
		 * bit 1:7 -> IP header offset
		 */
		u8 wl18xx_checksum_data;
	} __packed;
} __packed;

enum wl1271_tx_hw_res_status {
	TX_SUCCESS          = 0,
	TX_HW_ERROR         = 1,
	TX_DISABLED         = 2,
	TX_RETRY_EXCEEDED   = 3,
	TX_TIMEOUT          = 4,
	TX_KEY_NOT_FOUND    = 5,
	TX_PEER_NOT_FOUND   = 6,
	TX_SESSION_MISMATCH = 7,
	TX_LINK_NOT_VALID   = 8,
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
	u8 tx_security_sequence_number_lsb;
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

enum wlcore_queue_stop_reason {
	WLCORE_QUEUE_STOP_REASON_WATERMARK,
	WLCORE_QUEUE_STOP_REASON_FW_RESTART,
	WLCORE_QUEUE_STOP_REASON_FLUSH,
	WLCORE_QUEUE_STOP_REASON_SPARE_BLK, /* 18xx specific */
};

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

static inline
int wlcore_tx_get_mac80211_queue(struct wl12xx_vif *wlvif, int queue)
{
	int mac_queue = wlvif->hw_queue_base;

	switch (queue) {
	case CONF_TX_AC_VO:
		return mac_queue + 0;
	case CONF_TX_AC_VI:
		return mac_queue + 1;
	case CONF_TX_AC_BE:
		return mac_queue + 2;
	case CONF_TX_AC_BK:
		return mac_queue + 3;
	default:
		return mac_queue + 2;
	}
}

static inline int wl1271_tx_total_queue_count(struct wl1271 *wl)
{
	int i, count = 0;

	for (i = 0; i < NUM_TX_QUEUES; i++)
		count += wl->tx_queue_count[i];

	return count;
}

void wl1271_tx_work(struct work_struct *work);
int wlcore_tx_work_locked(struct wl1271 *wl);
int wlcore_tx_complete(struct wl1271 *wl);
void wl12xx_tx_reset_wlvif(struct wl1271 *wl, struct wl12xx_vif *wlvif);
void wl12xx_tx_reset(struct wl1271 *wl);
void wl1271_tx_flush(struct wl1271 *wl);
u8 wlcore_rate_to_idx(struct wl1271 *wl, u8 rate, enum nl80211_band band);
u32 wl1271_tx_enabled_rates_get(struct wl1271 *wl, u32 rate_set,
				enum nl80211_band rate_band);
u32 wl1271_tx_min_rate_get(struct wl1271 *wl, u32 rate_set);
u8 wl12xx_tx_get_hlid(struct wl1271 *wl, struct wl12xx_vif *wlvif,
		      struct sk_buff *skb, struct ieee80211_sta *sta);
void wl1271_tx_reset_link_queues(struct wl1271 *wl, u8 hlid);
void wl1271_handle_tx_low_watermark(struct wl1271 *wl);
bool wl12xx_is_dummy_packet(struct wl1271 *wl, struct sk_buff *skb);
void wl12xx_rearm_rx_streaming(struct wl1271 *wl, unsigned long *active_hlids);
unsigned int wlcore_calc_packet_alignment(struct wl1271 *wl,
					  unsigned int packet_length);
void wl1271_free_tx_id(struct wl1271 *wl, int id);
void wlcore_stop_queue_locked(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			      u8 queue, enum wlcore_queue_stop_reason reason);
void wlcore_stop_queue(struct wl1271 *wl, struct wl12xx_vif *wlvif, u8 queue,
		       enum wlcore_queue_stop_reason reason);
void wlcore_wake_queue(struct wl1271 *wl, struct wl12xx_vif *wlvif, u8 queue,
		       enum wlcore_queue_stop_reason reason);
void wlcore_stop_queues(struct wl1271 *wl,
			enum wlcore_queue_stop_reason reason);
void wlcore_wake_queues(struct wl1271 *wl,
			enum wlcore_queue_stop_reason reason);
bool wlcore_is_queue_stopped_by_reason(struct wl1271 *wl,
				       struct wl12xx_vif *wlvif, u8 queue,
				       enum wlcore_queue_stop_reason reason);
bool
wlcore_is_queue_stopped_by_reason_locked(struct wl1271 *wl,
					 struct wl12xx_vif *wlvif,
					 u8 queue,
					 enum wlcore_queue_stop_reason reason);
bool wlcore_is_queue_stopped_locked(struct wl1271 *wl, struct wl12xx_vif *wlvif,
				    u8 queue);

/* from main.c */
void wl1271_free_sta(struct wl1271 *wl, struct wl12xx_vif *wlvif, u8 hlid);
void wl12xx_rearm_tx_watchdog_locked(struct wl1271 *wl);

#endif
