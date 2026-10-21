/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2026 Morse Micro
 */

#ifndef _MM81X_SKBQ_H_
#define _MM81X_SKBQ_H_

#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include "rate_code.h"

/* Sync value of skb header to indicate a valid skb */
#define MM81X_SKB_HEADER_SYNC (0xAA)
/* Sync value indicating that the chip owns this skb */
#define MM81X_SKB_HEADER_CHIP_OWNED_SYNC (0xBB)

enum mm81x_tx_status_and_conf_flags {
	MM81X_TX_STATUS_FLAGS_NO_ACK = BIT(0),
	MM81X_TX_STATUS_FLAGS_NO_REPORT = BIT(1),
	MM81X_TX_CONF_FLAGS_CTL_AMPDU = BIT(2),
	MM81X_TX_CONF_FLAGS_HW_ENCRYPT = BIT(3),
	MM81X_TX_CONF_FLAGS_VIF_ID = (BIT(4) | BIT(5) | BIT(6) | BIT(7) |
				      BIT(8) | BIT(9) | BIT(10) | BIT(11)),
	MM81X_TX_CONF_FLAGS_KEY_IDX = (BIT(12) | BIT(13) | BIT(14)),
	MM81X_TX_STATUS_FLAGS_PS_FILTERED = (BIT(15)),
	MM81X_TX_CONF_IGNORE_TWT = (BIT(16)),
	MM81X_TX_STATUS_PAGE_INVALID = (BIT(17)),
	MM81X_TX_CONF_NO_PS_BUFFER = (BIT(18)),
	MM81X_TX_STATUS_DUTY_CYCLE_CANT_SEND = (BIT(19)),
	MM81X_TX_CONF_HAS_PV1_BPN_IN_BODY = (BIT(21)),
	MM81X_TX_CONF_FLAGS_SEND_AFTER_DTIM = (BIT(22)),
	MM81X_TX_STATUS_WAS_AGGREGATED = (BIT(23)),
	MM81X_TX_CONF_FLAGS_FULLMAC_REPORT = BIT(24),
	MM81X_TX_CONF_FLAGS_IMMEDIATE_REPORT = (BIT(31))
};

/* Getter and setter macros for vif id */
#define MM81X_TX_CONF_FLAGS_VIF_ID_MASK (0xFF)
#define MM81X_TX_CONF_FLAGS_VIF_ID_SET(x) \
	(((x) & MM81X_TX_CONF_FLAGS_VIF_ID_MASK) << 4)
#define MM81X_TX_CONF_FLAGS_VIF_ID_GET(x) \
	(((x) & MM81X_TX_CONF_FLAGS_VIF_ID) >> 4)

/* Getter and setter macros for key index */
#define MM81X_TX_CONF_FLAGS_KEY_IDX_SET(x) (((x) & 0x07) << 12)
#define MM81X_TX_CONF_FLAGS_KEY_IDX_GET(x) \
	(((x) & MM81X_TX_CONF_FLAGS_KEY_IDX) >> 12)

enum mm81x_rx_status_flags {
	MM81X_RX_STATUS_FLAGS_ERROR = BIT(0),
	MM81X_RX_STATUS_FLAGS_DECRYPTED = BIT(1),
	MM81X_RX_STATUS_FLAGS_FCS_INCLUDED = BIT(2),
	MM81X_RX_STATUS_FLAGS_EOF = BIT(3),
	MM81X_RX_STATUS_FLAGS_AMPDU = BIT(4),
	MM81X_RX_STATUS_FLAGS_NDP = BIT(7),
	MM81X_RX_STATUS_FLAGS_UPLINK = BIT(8),
	MM81X_RX_STATUS_FLAGS_RI = (BIT(9) | BIT(10)),
	MM81X_RX_STATUS_FLAGS_NDP_TYPE = (BIT(11) | BIT(12) | BIT(13)),
	MM81X_RX_STATUS_FLAGS_CRC_ERROR = BIT(14),
	MM81X_RX_STATUS_FLAGS_VIF_ID = GENMASK(24, 17),
};

/* Getter and Setter macros for vif id */
#define MM81X_RX_STATUS_FLAGS_VIF_ID_MASK (0xFF)
#define MM81X_RX_STATUS_FLAGS_VIF_ID_SET(x) \
	(((x) & MM81X_RX_STATUS_FLAGS_VIF_ID_MASK) << 17)
#define MM81X_RX_STATUS_FLAGS_VIF_ID_GET(x) \
	(((x) & MM81X_RX_STATUS_FLAGS_VIF_ID) >> 17)
#define MM81X_RX_STATUS_FLAGS_VIF_ID_CLEAR(x) \
	((x) & ~(MM81X_RX_STATUS_FLAGS_VIF_ID_MASK << 17))

/* Getter macro for guard interval */
#define MM81X_RX_STATUS_FLAGS_UPL_IND_GET(x) \
	(((x) & MM81X_RX_STATUS_FLAGS_UPLINK) >> 8)

/* Getter macro for response indication */
#define MM81X_RX_STATUS_FLAGS_RI_GET(x) (((x) & MM81X_RX_STATUS_FLAGS_RI) >> 9)

/* Getter macro for NDP type */
#define MM81X_RX_STATUS_FLAGS_NDP_TYPE_GET(x) \
	(((x) & MM81X_RX_STATUS_FLAGS_NDP_TYPE) >> 11)

enum mm81x_skb_channel {
	MM81X_SKB_CHAN_DATA = 0x0,
	MM81X_SKB_CHAN_NDP_FRAMES = 0x1,
	MM81X_SKB_CHAN_DATA_NOACK = 0x2,
	MM81X_SKB_CHAN_BEACON = 0x3,
	MM81X_SKB_CHAN_MGMT = 0x4,
	MM81X_SKB_CHAN_INTERNAL_CRIT_BEACON = 0x80,
	MM81X_SKB_CHAN_COMMAND = 0xFE,
	MM81X_SKB_CHAN_TX_STATUS = 0xFF
};

#define MM81X_SKB_MAX_RATES (4)

struct mm81x_skb_rate_info {
	mm81x_rate_code_t mm81x_ratecode;
	u8 count;
} __packed;

struct mm81x_skb_tx_status {
	__le32 flags;
	__le32 pkt_id;
	u8 tid;
	u8 channel;
	__le16 ampdu_info;
	struct mm81x_skb_rate_info rates[MM81X_SKB_MAX_RATES];
} __packed;

#define MM81X_TXSTS_AMPDU_INFO_GET_TAG(x) (((x) >> 10) & 0x3F)
#define MM81X_TXSTS_AMPDU_INFO_GET_LEN(x) (((x) >> 5) & 0x1F)
#define MM81X_TXSTS_AMPDU_INFO_GET_SUC(x) ((x) & 0x1F)

struct mm81x_skb_tx_info {
	__le32 flags;
	__le32 pkt_id;
	u8 tid;
	u8 tid_params;
	u8 mmss_params;
	u8 padding[1];
	struct mm81x_skb_rate_info rates[MM81X_SKB_MAX_RATES];
} __packed;

#define TX_INFO_TID_PARAMS_MAX_REORDER_BUF 0x1f
#define TX_INFO_TID_PARAMS_AMPDU_ENABLED 0x20
#define TX_INFO_TID_PARAMS_AMSDU_SUPPORTED 0x40
#define TX_INFO_TID_PARAMS_USE_LEGACY_BA 0x80

/* Bitmap for MMSS (Minimum MPDU start spacing) parameters
 * +-----------+-----------+
 * | Morse     | MMSS set  |
 * | MMSS      | by S1G cap|
 * | offset    | IE        |
 * |-----------|-----------|
 * |b7|b6|b5|b4|b3|b2|b1|b0|
 */
#define TX_INFO_MMSS_PARAMS_MMSS_MASK GENMASK(3, 0)
#define TX_INFO_MMSS_PARAMS_MMSS_OFFSET_START 4
#define TX_INFO_MMSS_PARAMS_MMSS_OFFSET_MASK GENMASK(7, 4)
#define TX_INFO_MMSS_PARAMS_SET_MMSS(x) ((x) & TX_INFO_MMSS_PARAMS_MMSS_MASK)
#define TX_INFO_MMSS_PARAMS_SET_MMSS_OFFSET(x)            \
	(((x) << TX_INFO_MMSS_PARAMS_MMSS_OFFSET_START) & \
	 TX_INFO_MMSS_PARAMS_MMSS_OFFSET_MASK)

struct mm81x_skb_rx_status {
	__le32 flags;
	mm81x_rate_code_t mm81x_ratecode;
	__le16 rssi;
	__le16 freq_100khz;
	u8 bss_color;
	s8 noise_dbm;
	/** Padding for word alignment */
	u8 padding[2];
	__le64 rx_timestamp_us;
} __packed;

struct mm81x_skb_hdr {
	u8 sync;
	u8 channel;
	__le16 len;
	u8 offset;
	u8 checksum_lower;
	__le16 checksum_upper;
	union {
		struct mm81x_skb_tx_info tx_info;
		struct mm81x_skb_tx_status tx_status;
		struct mm81x_skb_rx_status rx_status;
	};
} __packed;

#define MM81X_SKBQ_SIZE (4 * 128 * 1024)

struct mm81x;

struct mm81x_skbq {
	struct mm81x *mors;
	u32 pkt_seq; /* SKB sequence used in tx_status */
	u16 flags;
	u32 skbq_size; /* current off loaded size */
	spinlock_t lock;
	struct sk_buff_head skbq;
	struct sk_buff_head pending; /* packets sent pending feedback */
	struct work_struct dispatch_work;
};

void mm81x_skbq_purge(struct mm81x_skbq *mq, struct sk_buff_head *skbq);
void mm81x_skbq_purge_aged(struct mm81x *mors, struct mm81x_skbq *mq);
u32 mm81x_skbq_space(struct mm81x_skbq *mq);
u32 mm81x_skbq_size(struct mm81x_skbq *mq);
int mm81x_skbq_deq_num_skb(struct mm81x_skbq *mq, struct sk_buff_head *skbq,
			   int num_skb);
struct sk_buff *mm81x_skbq_alloc_skb(struct mm81x_skbq *mq,
				     unsigned int length);
int mm81x_skbq_skb_tx(struct mm81x_skbq *mq, struct sk_buff **skb,
		      struct mm81x_skb_tx_info *tx_info, u8 channel);
int mm81x_skbq_put(struct mm81x_skbq *mq, struct sk_buff *skb);
void mm81x_skbq_enq(struct mm81x_skbq *mq, struct sk_buff_head *skbq);
void mm81x_skbq_enq_prepend(struct mm81x_skbq *mq, struct sk_buff_head *skbq);
void mm81x_skbq_tx_complete(struct mm81x_skbq *mq, struct sk_buff_head *skbq);
struct sk_buff *mm81x_skbq_tx_pending(struct mm81x_skbq *mq);
void mm81x_skbq_init(struct mm81x *mors, struct mm81x_skbq *mq, u16 flags);
void mm81x_skbq_finish(struct mm81x_skbq *mq);
void mm81x_skbq_pull_hdr_post_tx(struct sk_buff *skb);
void mm81x_skbq_mon_dump(struct mm81x *mors, struct seq_file *file);
void mm81x_skbq_skb_finish(struct mm81x_skbq *mq, struct sk_buff *skb,
			   struct mm81x_skb_tx_status *tx_sts);
void mm81x_skbq_tx_flush(struct mm81x_skbq *mq);
int mm81x_skbq_check_for_stale_tx(struct mm81x *mors, struct mm81x_skbq *mq);
void mm81x_skbq_may_wake_tx_queues(struct mm81x *mors);
u32 mm81x_skbq_count_tx_ready(struct mm81x_skbq *mq);
u32 mm81x_skbq_count(struct mm81x_skbq *mq);
u32 mm81x_skbq_pending_count(struct mm81x_skbq *mq);
void mm81x_skbq_data_traffic_pause(struct mm81x *mors);
void mm81x_skbq_data_traffic_resume(struct mm81x *mors);
bool mm81x_skbq_validate_checksum(u8 *data);

#endif /* !_MM81X_SKBQ_H_ */
