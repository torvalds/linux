/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _HTT_H_
#define _HTT_H_

#include <linux/bug.h>
#include <linux/interrupt.h>
#include <linux/dmapool.h>
#include <linux/hashtable.h>
#include <linux/kfifo.h>
#include <net/mac80211.h>

#include "htc.h"
#include "hw.h"
#include "rx_desc.h"
#include "hw.h"

enum htt_dbg_stats_type {
	HTT_DBG_STATS_WAL_PDEV_TXRX = 1 << 0,
	HTT_DBG_STATS_RX_REORDER    = 1 << 1,
	HTT_DBG_STATS_RX_RATE_INFO  = 1 << 2,
	HTT_DBG_STATS_TX_PPDU_LOG   = 1 << 3,
	HTT_DBG_STATS_TX_RATE_INFO  = 1 << 4,
	/* bits 5-23 currently reserved */

	HTT_DBG_NUM_STATS /* keep this last */
};

enum htt_h2t_msg_type { /* host-to-target */
	HTT_H2T_MSG_TYPE_VERSION_REQ        = 0,
	HTT_H2T_MSG_TYPE_TX_FRM             = 1,
	HTT_H2T_MSG_TYPE_RX_RING_CFG        = 2,
	HTT_H2T_MSG_TYPE_STATS_REQ          = 3,
	HTT_H2T_MSG_TYPE_SYNC               = 4,
	HTT_H2T_MSG_TYPE_AGGR_CFG           = 5,
	HTT_H2T_MSG_TYPE_FRAG_DESC_BANK_CFG = 6,

	/* This command is used for sending management frames in HTT < 3.0.
	 * HTT >= 3.0 uses TX_FRM for everything.
	 */
	HTT_H2T_MSG_TYPE_MGMT_TX            = 7,
	HTT_H2T_MSG_TYPE_TX_FETCH_RESP      = 11,

	HTT_H2T_NUM_MSGS /* keep this last */
};

struct htt_cmd_hdr {
	u8 msg_type;
} __packed;

struct htt_ver_req {
	u8 pad[sizeof(u32) - sizeof(struct htt_cmd_hdr)];
} __packed;

/*
 * HTT tx MSDU descriptor
 *
 * The HTT tx MSDU descriptor is created by the host HTT SW for each
 * tx MSDU.  The HTT tx MSDU descriptor contains the information that
 * the target firmware needs for the FW's tx processing, particularly
 * for creating the HW msdu descriptor.
 * The same HTT tx descriptor is used for HL and LL systems, though
 * a few fields within the tx descriptor are used only by LL or
 * only by HL.
 * The HTT tx descriptor is defined in two manners: by a struct with
 * bitfields, and by a series of [dword offset, bit mask, bit shift]
 * definitions.
 * The target should use the struct def, for simplicitly and clarity,
 * but the host shall use the bit-mast + bit-shift defs, to be endian-
 * neutral.  Specifically, the host shall use the get/set macros built
 * around the mask + shift defs.
 */
struct htt_data_tx_desc_frag {
	union {
		struct double_word_addr {
			__le32 paddr;
			__le32 len;
		} __packed dword_addr;
		struct triple_word_addr {
			__le32 paddr_lo;
			__le16 paddr_hi;
			__le16 len_16;
		} __packed tword_addr;
	} __packed;
} __packed;

struct htt_msdu_ext_desc {
	__le32 tso_flag[3];
	__le16 ip_identification;
	u8 flags;
	u8 reserved;
	struct htt_data_tx_desc_frag frags[6];
};

#define	HTT_MSDU_EXT_DESC_FLAG_IPV4_CSUM_ENABLE		BIT(0)
#define	HTT_MSDU_EXT_DESC_FLAG_UDP_IPV4_CSUM_ENABLE	BIT(1)
#define	HTT_MSDU_EXT_DESC_FLAG_UDP_IPV6_CSUM_ENABLE	BIT(2)
#define	HTT_MSDU_EXT_DESC_FLAG_TCP_IPV4_CSUM_ENABLE	BIT(3)
#define	HTT_MSDU_EXT_DESC_FLAG_TCP_IPV6_CSUM_ENABLE	BIT(4)

#define HTT_MSDU_CHECKSUM_ENABLE (HTT_MSDU_EXT_DESC_FLAG_IPV4_CSUM_ENABLE \
				 | HTT_MSDU_EXT_DESC_FLAG_UDP_IPV4_CSUM_ENABLE \
				 | HTT_MSDU_EXT_DESC_FLAG_UDP_IPV6_CSUM_ENABLE \
				 | HTT_MSDU_EXT_DESC_FLAG_TCP_IPV4_CSUM_ENABLE \
				 | HTT_MSDU_EXT_DESC_FLAG_TCP_IPV6_CSUM_ENABLE)

enum htt_data_tx_desc_flags0 {
	HTT_DATA_TX_DESC_FLAGS0_MAC_HDR_PRESENT = 1 << 0,
	HTT_DATA_TX_DESC_FLAGS0_NO_AGGR         = 1 << 1,
	HTT_DATA_TX_DESC_FLAGS0_NO_ENCRYPT      = 1 << 2,
	HTT_DATA_TX_DESC_FLAGS0_NO_CLASSIFY     = 1 << 3,
	HTT_DATA_TX_DESC_FLAGS0_RSVD0           = 1 << 4
#define HTT_DATA_TX_DESC_FLAGS0_PKT_TYPE_MASK 0xE0
#define HTT_DATA_TX_DESC_FLAGS0_PKT_TYPE_LSB 5
};

enum htt_data_tx_desc_flags1 {
#define HTT_DATA_TX_DESC_FLAGS1_VDEV_ID_BITS 6
#define HTT_DATA_TX_DESC_FLAGS1_VDEV_ID_MASK 0x003F
#define HTT_DATA_TX_DESC_FLAGS1_VDEV_ID_LSB  0
#define HTT_DATA_TX_DESC_FLAGS1_EXT_TID_BITS 5
#define HTT_DATA_TX_DESC_FLAGS1_EXT_TID_MASK 0x07C0
#define HTT_DATA_TX_DESC_FLAGS1_EXT_TID_LSB  6
	HTT_DATA_TX_DESC_FLAGS1_POSTPONED        = 1 << 11,
	HTT_DATA_TX_DESC_FLAGS1_MORE_IN_BATCH    = 1 << 12,
	HTT_DATA_TX_DESC_FLAGS1_CKSUM_L3_OFFLOAD = 1 << 13,
	HTT_DATA_TX_DESC_FLAGS1_CKSUM_L4_OFFLOAD = 1 << 14,
	HTT_DATA_TX_DESC_FLAGS1_RSVD1            = 1 << 15
};

enum htt_data_tx_ext_tid {
	HTT_DATA_TX_EXT_TID_NON_QOS_MCAST_BCAST = 16,
	HTT_DATA_TX_EXT_TID_MGMT                = 17,
	HTT_DATA_TX_EXT_TID_INVALID             = 31
};

#define HTT_INVALID_PEERID 0xFFFF

/*
 * htt_data_tx_desc - used for data tx path
 *
 * Note: vdev_id irrelevant for pkt_type == raw and no_classify == 1.
 *       ext_tid: for qos-data frames (0-15), see %HTT_DATA_TX_EXT_TID_
 *                for special kinds of tids
 *       postponed: only for HL hosts. indicates if this is a resend
 *                  (HL hosts manage queues on the host )
 *       more_in_batch: only for HL hosts. indicates if more packets are
 *                      pending. this allows target to wait and aggregate
 *       freq: 0 means home channel of given vdev. intended for offchannel
 */
struct htt_data_tx_desc {
	u8 flags0; /* %HTT_DATA_TX_DESC_FLAGS0_ */
	__le16 flags1; /* %HTT_DATA_TX_DESC_FLAGS1_ */
	__le16 len;
	__le16 id;
	__le32 frags_paddr;
	union {
		__le32 peerid;
		struct {
			__le16 peerid;
			__le16 freq;
		} __packed offchan_tx;
	} __packed;
	u8 prefetch[0]; /* start of frame, for FW classification engine */
} __packed;

enum htt_rx_ring_flags {
	HTT_RX_RING_FLAGS_MAC80211_HDR = 1 << 0,
	HTT_RX_RING_FLAGS_MSDU_PAYLOAD = 1 << 1,
	HTT_RX_RING_FLAGS_PPDU_START   = 1 << 2,
	HTT_RX_RING_FLAGS_PPDU_END     = 1 << 3,
	HTT_RX_RING_FLAGS_MPDU_START   = 1 << 4,
	HTT_RX_RING_FLAGS_MPDU_END     = 1 << 5,
	HTT_RX_RING_FLAGS_MSDU_START   = 1 << 6,
	HTT_RX_RING_FLAGS_MSDU_END     = 1 << 7,
	HTT_RX_RING_FLAGS_RX_ATTENTION = 1 << 8,
	HTT_RX_RING_FLAGS_FRAG_INFO    = 1 << 9,
	HTT_RX_RING_FLAGS_UNICAST_RX   = 1 << 10,
	HTT_RX_RING_FLAGS_MULTICAST_RX = 1 << 11,
	HTT_RX_RING_FLAGS_CTRL_RX      = 1 << 12,
	HTT_RX_RING_FLAGS_MGMT_RX      = 1 << 13,
	HTT_RX_RING_FLAGS_NULL_RX      = 1 << 14,
	HTT_RX_RING_FLAGS_PHY_DATA_RX  = 1 << 15
};

#define HTT_RX_RING_SIZE_MIN 128
#define HTT_RX_RING_SIZE_MAX 2048

struct htt_rx_ring_setup_ring {
	__le32 fw_idx_shadow_reg_paddr;
	__le32 rx_ring_base_paddr;
	__le16 rx_ring_len; /* in 4-byte words */
	__le16 rx_ring_bufsize; /* rx skb size - in bytes */
	__le16 flags; /* %HTT_RX_RING_FLAGS_ */
	__le16 fw_idx_init_val;

	/* the following offsets are in 4-byte units */
	__le16 mac80211_hdr_offset;
	__le16 msdu_payload_offset;
	__le16 ppdu_start_offset;
	__le16 ppdu_end_offset;
	__le16 mpdu_start_offset;
	__le16 mpdu_end_offset;
	__le16 msdu_start_offset;
	__le16 msdu_end_offset;
	__le16 rx_attention_offset;
	__le16 frag_info_offset;
} __packed;

struct htt_rx_ring_setup_hdr {
	u8 num_rings; /* supported values: 1, 2 */
	__le16 rsvd0;
} __packed;

struct htt_rx_ring_setup {
	struct htt_rx_ring_setup_hdr hdr;
	struct htt_rx_ring_setup_ring rings[0];
} __packed;

/*
 * htt_stats_req - request target to send specified statistics
 *
 * @msg_type: hardcoded %HTT_H2T_MSG_TYPE_STATS_REQ
 * @upload_types: see %htt_dbg_stats_type. this is 24bit field actually
 *	so make sure its little-endian.
 * @reset_types: see %htt_dbg_stats_type. this is 24bit field actually
 *	so make sure its little-endian.
 * @cfg_val: stat_type specific configuration
 * @stat_type: see %htt_dbg_stats_type
 * @cookie_lsb: used for confirmation message from target->host
 * @cookie_msb: ditto as %cookie
 */
struct htt_stats_req {
	u8 upload_types[3];
	u8 rsvd0;
	u8 reset_types[3];
	struct {
		u8 mpdu_bytes;
		u8 mpdu_num_msdus;
		u8 msdu_bytes;
	} __packed;
	u8 stat_type;
	__le32 cookie_lsb;
	__le32 cookie_msb;
} __packed;

#define HTT_STATS_REQ_CFG_STAT_TYPE_INVALID 0xff

/*
 * htt_oob_sync_req - request out-of-band sync
 *
 * The HTT SYNC tells the target to suspend processing of subsequent
 * HTT host-to-target messages until some other target agent locally
 * informs the target HTT FW that the current sync counter is equal to
 * or greater than (in a modulo sense) the sync counter specified in
 * the SYNC message.
 *
 * This allows other host-target components to synchronize their operation
 * with HTT, e.g. to ensure that tx frames don't get transmitted until a
 * security key has been downloaded to and activated by the target.
 * In the absence of any explicit synchronization counter value
 * specification, the target HTT FW will use zero as the default current
 * sync value.
 *
 * The HTT target FW will suspend its host->target message processing as long
 * as 0 < (in-band sync counter - out-of-band sync counter) & 0xff < 128.
 */
struct htt_oob_sync_req {
	u8 sync_count;
	__le16 rsvd0;
} __packed;

struct htt_aggr_conf {
	u8 max_num_ampdu_subframes;
	/* amsdu_subframes is limited by 0x1F mask */
	u8 max_num_amsdu_subframes;
} __packed;

#define HTT_MGMT_FRM_HDR_DOWNLOAD_LEN 32
struct htt_mgmt_tx_desc_qca99x0 {
	__le32 rate;
} __packed;

struct htt_mgmt_tx_desc {
	u8 pad[sizeof(u32) - sizeof(struct htt_cmd_hdr)];
	__le32 msdu_paddr;
	__le32 desc_id;
	__le32 len;
	__le32 vdev_id;
	u8 hdr[HTT_MGMT_FRM_HDR_DOWNLOAD_LEN];
	union {
		struct htt_mgmt_tx_desc_qca99x0 qca99x0;
	} __packed;
} __packed;

enum htt_mgmt_tx_status {
	HTT_MGMT_TX_STATUS_OK    = 0,
	HTT_MGMT_TX_STATUS_RETRY = 1,
	HTT_MGMT_TX_STATUS_DROP  = 2
};

/*=== target -> host messages ===============================================*/

enum htt_main_t2h_msg_type {
	HTT_MAIN_T2H_MSG_TYPE_VERSION_CONF             = 0x0,
	HTT_MAIN_T2H_MSG_TYPE_RX_IND                   = 0x1,
	HTT_MAIN_T2H_MSG_TYPE_RX_FLUSH                 = 0x2,
	HTT_MAIN_T2H_MSG_TYPE_PEER_MAP                 = 0x3,
	HTT_MAIN_T2H_MSG_TYPE_PEER_UNMAP               = 0x4,
	HTT_MAIN_T2H_MSG_TYPE_RX_ADDBA                 = 0x5,
	HTT_MAIN_T2H_MSG_TYPE_RX_DELBA                 = 0x6,
	HTT_MAIN_T2H_MSG_TYPE_TX_COMPL_IND             = 0x7,
	HTT_MAIN_T2H_MSG_TYPE_PKTLOG                   = 0x8,
	HTT_MAIN_T2H_MSG_TYPE_STATS_CONF               = 0x9,
	HTT_MAIN_T2H_MSG_TYPE_RX_FRAG_IND              = 0xa,
	HTT_MAIN_T2H_MSG_TYPE_SEC_IND                  = 0xb,
	HTT_MAIN_T2H_MSG_TYPE_TX_INSPECT_IND           = 0xd,
	HTT_MAIN_T2H_MSG_TYPE_MGMT_TX_COMPL_IND        = 0xe,
	HTT_MAIN_T2H_MSG_TYPE_TX_CREDIT_UPDATE_IND     = 0xf,
	HTT_MAIN_T2H_MSG_TYPE_RX_PN_IND                = 0x10,
	HTT_MAIN_T2H_MSG_TYPE_RX_OFFLOAD_DELIVER_IND   = 0x11,
	HTT_MAIN_T2H_MSG_TYPE_TEST,
	/* keep this last */
	HTT_MAIN_T2H_NUM_MSGS
};

enum htt_10x_t2h_msg_type {
	HTT_10X_T2H_MSG_TYPE_VERSION_CONF              = 0x0,
	HTT_10X_T2H_MSG_TYPE_RX_IND                    = 0x1,
	HTT_10X_T2H_MSG_TYPE_RX_FLUSH                  = 0x2,
	HTT_10X_T2H_MSG_TYPE_PEER_MAP                  = 0x3,
	HTT_10X_T2H_MSG_TYPE_PEER_UNMAP                = 0x4,
	HTT_10X_T2H_MSG_TYPE_RX_ADDBA                  = 0x5,
	HTT_10X_T2H_MSG_TYPE_RX_DELBA                  = 0x6,
	HTT_10X_T2H_MSG_TYPE_TX_COMPL_IND              = 0x7,
	HTT_10X_T2H_MSG_TYPE_PKTLOG                    = 0x8,
	HTT_10X_T2H_MSG_TYPE_STATS_CONF                = 0x9,
	HTT_10X_T2H_MSG_TYPE_RX_FRAG_IND               = 0xa,
	HTT_10X_T2H_MSG_TYPE_SEC_IND                   = 0xb,
	HTT_10X_T2H_MSG_TYPE_RC_UPDATE_IND             = 0xc,
	HTT_10X_T2H_MSG_TYPE_TX_INSPECT_IND            = 0xd,
	HTT_10X_T2H_MSG_TYPE_TEST                      = 0xe,
	HTT_10X_T2H_MSG_TYPE_CHAN_CHANGE               = 0xf,
	HTT_10X_T2H_MSG_TYPE_AGGR_CONF                 = 0x11,
	HTT_10X_T2H_MSG_TYPE_STATS_NOUPLOAD            = 0x12,
	HTT_10X_T2H_MSG_TYPE_MGMT_TX_COMPL_IND         = 0x13,
	/* keep this last */
	HTT_10X_T2H_NUM_MSGS
};

enum htt_tlv_t2h_msg_type {
	HTT_TLV_T2H_MSG_TYPE_VERSION_CONF              = 0x0,
	HTT_TLV_T2H_MSG_TYPE_RX_IND                    = 0x1,
	HTT_TLV_T2H_MSG_TYPE_RX_FLUSH                  = 0x2,
	HTT_TLV_T2H_MSG_TYPE_PEER_MAP                  = 0x3,
	HTT_TLV_T2H_MSG_TYPE_PEER_UNMAP                = 0x4,
	HTT_TLV_T2H_MSG_TYPE_RX_ADDBA                  = 0x5,
	HTT_TLV_T2H_MSG_TYPE_RX_DELBA                  = 0x6,
	HTT_TLV_T2H_MSG_TYPE_TX_COMPL_IND              = 0x7,
	HTT_TLV_T2H_MSG_TYPE_PKTLOG                    = 0x8,
	HTT_TLV_T2H_MSG_TYPE_STATS_CONF                = 0x9,
	HTT_TLV_T2H_MSG_TYPE_RX_FRAG_IND               = 0xa,
	HTT_TLV_T2H_MSG_TYPE_SEC_IND                   = 0xb,
	HTT_TLV_T2H_MSG_TYPE_RC_UPDATE_IND             = 0xc, /* deprecated */
	HTT_TLV_T2H_MSG_TYPE_TX_INSPECT_IND            = 0xd,
	HTT_TLV_T2H_MSG_TYPE_MGMT_TX_COMPL_IND         = 0xe,
	HTT_TLV_T2H_MSG_TYPE_TX_CREDIT_UPDATE_IND      = 0xf,
	HTT_TLV_T2H_MSG_TYPE_RX_PN_IND                 = 0x10,
	HTT_TLV_T2H_MSG_TYPE_RX_OFFLOAD_DELIVER_IND    = 0x11,
	HTT_TLV_T2H_MSG_TYPE_RX_IN_ORD_PADDR_IND       = 0x12,
	/* 0x13 reservd */
	HTT_TLV_T2H_MSG_TYPE_WDI_IPA_OP_RESPONSE       = 0x14,
	HTT_TLV_T2H_MSG_TYPE_CHAN_CHANGE               = 0x15,
	HTT_TLV_T2H_MSG_TYPE_RX_OFLD_PKT_ERR           = 0x16,
	HTT_TLV_T2H_MSG_TYPE_TEST,
	/* keep this last */
	HTT_TLV_T2H_NUM_MSGS
};

enum htt_10_4_t2h_msg_type {
	HTT_10_4_T2H_MSG_TYPE_VERSION_CONF           = 0x0,
	HTT_10_4_T2H_MSG_TYPE_RX_IND                 = 0x1,
	HTT_10_4_T2H_MSG_TYPE_RX_FLUSH               = 0x2,
	HTT_10_4_T2H_MSG_TYPE_PEER_MAP               = 0x3,
	HTT_10_4_T2H_MSG_TYPE_PEER_UNMAP             = 0x4,
	HTT_10_4_T2H_MSG_TYPE_RX_ADDBA               = 0x5,
	HTT_10_4_T2H_MSG_TYPE_RX_DELBA               = 0x6,
	HTT_10_4_T2H_MSG_TYPE_TX_COMPL_IND           = 0x7,
	HTT_10_4_T2H_MSG_TYPE_PKTLOG                 = 0x8,
	HTT_10_4_T2H_MSG_TYPE_STATS_CONF             = 0x9,
	HTT_10_4_T2H_MSG_TYPE_RX_FRAG_IND            = 0xa,
	HTT_10_4_T2H_MSG_TYPE_SEC_IND                = 0xb,
	HTT_10_4_T2H_MSG_TYPE_RC_UPDATE_IND          = 0xc,
	HTT_10_4_T2H_MSG_TYPE_TX_INSPECT_IND         = 0xd,
	HTT_10_4_T2H_MSG_TYPE_MGMT_TX_COMPL_IND      = 0xe,
	HTT_10_4_T2H_MSG_TYPE_CHAN_CHANGE            = 0xf,
	HTT_10_4_T2H_MSG_TYPE_TX_CREDIT_UPDATE_IND   = 0x10,
	HTT_10_4_T2H_MSG_TYPE_RX_PN_IND              = 0x11,
	HTT_10_4_T2H_MSG_TYPE_RX_OFFLOAD_DELIVER_IND = 0x12,
	HTT_10_4_T2H_MSG_TYPE_TEST                   = 0x13,
	HTT_10_4_T2H_MSG_TYPE_EN_STATS               = 0x14,
	HTT_10_4_T2H_MSG_TYPE_AGGR_CONF              = 0x15,
	HTT_10_4_T2H_MSG_TYPE_TX_FETCH_IND           = 0x16,
	HTT_10_4_T2H_MSG_TYPE_TX_FETCH_CONFIRM       = 0x17,
	HTT_10_4_T2H_MSG_TYPE_STATS_NOUPLOAD         = 0x18,
	/* 0x19 to 0x2f are reserved */
	HTT_10_4_T2H_MSG_TYPE_TX_MODE_SWITCH_IND     = 0x30,
	HTT_10_4_T2H_MSG_TYPE_PEER_STATS	     = 0x31,
	/* keep this last */
	HTT_10_4_T2H_NUM_MSGS
};

enum htt_t2h_msg_type {
	HTT_T2H_MSG_TYPE_VERSION_CONF,
	HTT_T2H_MSG_TYPE_RX_IND,
	HTT_T2H_MSG_TYPE_RX_FLUSH,
	HTT_T2H_MSG_TYPE_PEER_MAP,
	HTT_T2H_MSG_TYPE_PEER_UNMAP,
	HTT_T2H_MSG_TYPE_RX_ADDBA,
	HTT_T2H_MSG_TYPE_RX_DELBA,
	HTT_T2H_MSG_TYPE_TX_COMPL_IND,
	HTT_T2H_MSG_TYPE_PKTLOG,
	HTT_T2H_MSG_TYPE_STATS_CONF,
	HTT_T2H_MSG_TYPE_RX_FRAG_IND,
	HTT_T2H_MSG_TYPE_SEC_IND,
	HTT_T2H_MSG_TYPE_RC_UPDATE_IND,
	HTT_T2H_MSG_TYPE_TX_INSPECT_IND,
	HTT_T2H_MSG_TYPE_MGMT_TX_COMPLETION,
	HTT_T2H_MSG_TYPE_TX_CREDIT_UPDATE_IND,
	HTT_T2H_MSG_TYPE_RX_PN_IND,
	HTT_T2H_MSG_TYPE_RX_OFFLOAD_DELIVER_IND,
	HTT_T2H_MSG_TYPE_RX_IN_ORD_PADDR_IND,
	HTT_T2H_MSG_TYPE_WDI_IPA_OP_RESPONSE,
	HTT_T2H_MSG_TYPE_CHAN_CHANGE,
	HTT_T2H_MSG_TYPE_RX_OFLD_PKT_ERR,
	HTT_T2H_MSG_TYPE_AGGR_CONF,
	HTT_T2H_MSG_TYPE_STATS_NOUPLOAD,
	HTT_T2H_MSG_TYPE_TEST,
	HTT_T2H_MSG_TYPE_EN_STATS,
	HTT_T2H_MSG_TYPE_TX_FETCH_IND,
	HTT_T2H_MSG_TYPE_TX_FETCH_CONFIRM,
	HTT_T2H_MSG_TYPE_TX_MODE_SWITCH_IND,
	HTT_T2H_MSG_TYPE_PEER_STATS,
	/* keep this last */
	HTT_T2H_NUM_MSGS
};

/*
 * htt_resp_hdr - header for target-to-host messages
 *
 * msg_type: see htt_t2h_msg_type
 */
struct htt_resp_hdr {
	u8 msg_type;
} __packed;

#define HTT_RESP_HDR_MSG_TYPE_OFFSET 0
#define HTT_RESP_HDR_MSG_TYPE_MASK   0xff
#define HTT_RESP_HDR_MSG_TYPE_LSB    0

/* htt_ver_resp - response sent for htt_ver_req */
struct htt_ver_resp {
	u8 minor;
	u8 major;
	u8 rsvd0;
} __packed;

struct htt_mgmt_tx_completion {
	u8 rsvd0;
	u8 rsvd1;
	u8 rsvd2;
	__le32 desc_id;
	__le32 status;
} __packed;

#define HTT_RX_INDICATION_INFO0_EXT_TID_MASK  (0x1F)
#define HTT_RX_INDICATION_INFO0_EXT_TID_LSB   (0)
#define HTT_RX_INDICATION_INFO0_FLUSH_VALID   (1 << 5)
#define HTT_RX_INDICATION_INFO0_RELEASE_VALID (1 << 6)

#define HTT_RX_INDICATION_INFO1_FLUSH_START_SEQNO_MASK   0x0000003F
#define HTT_RX_INDICATION_INFO1_FLUSH_START_SEQNO_LSB    0
#define HTT_RX_INDICATION_INFO1_FLUSH_END_SEQNO_MASK     0x00000FC0
#define HTT_RX_INDICATION_INFO1_FLUSH_END_SEQNO_LSB      6
#define HTT_RX_INDICATION_INFO1_RELEASE_START_SEQNO_MASK 0x0003F000
#define HTT_RX_INDICATION_INFO1_RELEASE_START_SEQNO_LSB  12
#define HTT_RX_INDICATION_INFO1_RELEASE_END_SEQNO_MASK   0x00FC0000
#define HTT_RX_INDICATION_INFO1_RELEASE_END_SEQNO_LSB    18
#define HTT_RX_INDICATION_INFO1_NUM_MPDU_RANGES_MASK     0xFF000000
#define HTT_RX_INDICATION_INFO1_NUM_MPDU_RANGES_LSB      24

struct htt_rx_indication_hdr {
	u8 info0; /* %HTT_RX_INDICATION_INFO0_ */
	__le16 peer_id;
	__le32 info1; /* %HTT_RX_INDICATION_INFO1_ */
} __packed;

#define HTT_RX_INDICATION_INFO0_PHY_ERR_VALID    (1 << 0)
#define HTT_RX_INDICATION_INFO0_LEGACY_RATE_MASK (0x1E)
#define HTT_RX_INDICATION_INFO0_LEGACY_RATE_LSB  (1)
#define HTT_RX_INDICATION_INFO0_LEGACY_RATE_CCK  (1 << 5)
#define HTT_RX_INDICATION_INFO0_END_VALID        (1 << 6)
#define HTT_RX_INDICATION_INFO0_START_VALID      (1 << 7)

#define HTT_RX_INDICATION_INFO1_VHT_SIG_A1_MASK    0x00FFFFFF
#define HTT_RX_INDICATION_INFO1_VHT_SIG_A1_LSB     0
#define HTT_RX_INDICATION_INFO1_PREAMBLE_TYPE_MASK 0xFF000000
#define HTT_RX_INDICATION_INFO1_PREAMBLE_TYPE_LSB  24

#define HTT_RX_INDICATION_INFO2_VHT_SIG_A1_MASK 0x00FFFFFF
#define HTT_RX_INDICATION_INFO2_VHT_SIG_A1_LSB  0
#define HTT_RX_INDICATION_INFO2_SERVICE_MASK    0xFF000000
#define HTT_RX_INDICATION_INFO2_SERVICE_LSB     24

enum htt_rx_legacy_rate {
	HTT_RX_OFDM_48 = 0,
	HTT_RX_OFDM_24 = 1,
	HTT_RX_OFDM_12,
	HTT_RX_OFDM_6,
	HTT_RX_OFDM_54,
	HTT_RX_OFDM_36,
	HTT_RX_OFDM_18,
	HTT_RX_OFDM_9,

	/* long preamble */
	HTT_RX_CCK_11_LP = 0,
	HTT_RX_CCK_5_5_LP = 1,
	HTT_RX_CCK_2_LP,
	HTT_RX_CCK_1_LP,
	/* short preamble */
	HTT_RX_CCK_11_SP,
	HTT_RX_CCK_5_5_SP,
	HTT_RX_CCK_2_SP
};

enum htt_rx_legacy_rate_type {
	HTT_RX_LEGACY_RATE_OFDM = 0,
	HTT_RX_LEGACY_RATE_CCK
};

enum htt_rx_preamble_type {
	HTT_RX_LEGACY        = 0x4,
	HTT_RX_HT            = 0x8,
	HTT_RX_HT_WITH_TXBF  = 0x9,
	HTT_RX_VHT           = 0xC,
	HTT_RX_VHT_WITH_TXBF = 0xD,
};

/*
 * Fields: phy_err_valid, phy_err_code, tsf,
 * usec_timestamp, sub_usec_timestamp
 * ..are valid only if end_valid == 1.
 *
 * Fields: rssi_chains, legacy_rate_type,
 * legacy_rate_cck, preamble_type, service,
 * vht_sig_*
 * ..are valid only if start_valid == 1;
 */
struct htt_rx_indication_ppdu {
	u8 combined_rssi;
	u8 sub_usec_timestamp;
	u8 phy_err_code;
	u8 info0; /* HTT_RX_INDICATION_INFO0_ */
	struct {
		u8 pri20_db;
		u8 ext20_db;
		u8 ext40_db;
		u8 ext80_db;
	} __packed rssi_chains[4];
	__le32 tsf;
	__le32 usec_timestamp;
	__le32 info1; /* HTT_RX_INDICATION_INFO1_ */
	__le32 info2; /* HTT_RX_INDICATION_INFO2_ */
} __packed;

enum htt_rx_mpdu_status {
	HTT_RX_IND_MPDU_STATUS_UNKNOWN = 0x0,
	HTT_RX_IND_MPDU_STATUS_OK,
	HTT_RX_IND_MPDU_STATUS_ERR_FCS,
	HTT_RX_IND_MPDU_STATUS_ERR_DUP,
	HTT_RX_IND_MPDU_STATUS_ERR_REPLAY,
	HTT_RX_IND_MPDU_STATUS_ERR_INV_PEER,
	/* only accept EAPOL frames */
	HTT_RX_IND_MPDU_STATUS_UNAUTH_PEER,
	HTT_RX_IND_MPDU_STATUS_OUT_OF_SYNC,
	/* Non-data in promiscuous mode */
	HTT_RX_IND_MPDU_STATUS_MGMT_CTRL,
	HTT_RX_IND_MPDU_STATUS_TKIP_MIC_ERR,
	HTT_RX_IND_MPDU_STATUS_DECRYPT_ERR,
	HTT_RX_IND_MPDU_STATUS_MPDU_LENGTH_ERR,
	HTT_RX_IND_MPDU_STATUS_ENCRYPT_REQUIRED_ERR,
	HTT_RX_IND_MPDU_STATUS_PRIVACY_ERR,

	/*
	 * MISC: discard for unspecified reasons.
	 * Leave this enum value last.
	 */
	HTT_RX_IND_MPDU_STATUS_ERR_MISC = 0xFF
};

struct htt_rx_indication_mpdu_range {
	u8 mpdu_count;
	u8 mpdu_range_status; /* %htt_rx_mpdu_status */
	u8 pad0;
	u8 pad1;
} __packed;

struct htt_rx_indication_prefix {
	__le16 fw_rx_desc_bytes;
	u8 pad0;
	u8 pad1;
};

struct htt_rx_indication {
	struct htt_rx_indication_hdr hdr;
	struct htt_rx_indication_ppdu ppdu;
	struct htt_rx_indication_prefix prefix;

	/*
	 * the following fields are both dynamically sized, so
	 * take care addressing them
	 */

	/* the size of this is %fw_rx_desc_bytes */
	struct fw_rx_desc_base fw_desc;

	/*
	 * %mpdu_ranges starts after &%prefix + roundup(%fw_rx_desc_bytes, 4)
	 * and has %num_mpdu_ranges elements.
	 */
	struct htt_rx_indication_mpdu_range mpdu_ranges[0];
} __packed;

static inline struct htt_rx_indication_mpdu_range *
		htt_rx_ind_get_mpdu_ranges(struct htt_rx_indication *rx_ind)
{
	void *ptr = rx_ind;

	ptr += sizeof(rx_ind->hdr)
	     + sizeof(rx_ind->ppdu)
	     + sizeof(rx_ind->prefix)
	     + roundup(__le16_to_cpu(rx_ind->prefix.fw_rx_desc_bytes), 4);
	return ptr;
}

enum htt_rx_flush_mpdu_status {
	HTT_RX_FLUSH_MPDU_DISCARD = 0,
	HTT_RX_FLUSH_MPDU_REORDER = 1,
};

/*
 * htt_rx_flush - discard or reorder given range of mpdus
 *
 * Note: host must check if all sequence numbers between
 *	[seq_num_start, seq_num_end-1] are valid.
 */
struct htt_rx_flush {
	__le16 peer_id;
	u8 tid;
	u8 rsvd0;
	u8 mpdu_status; /* %htt_rx_flush_mpdu_status */
	u8 seq_num_start; /* it is 6 LSBs of 802.11 seq no */
	u8 seq_num_end; /* it is 6 LSBs of 802.11 seq no */
};

struct htt_rx_peer_map {
	u8 vdev_id;
	__le16 peer_id;
	u8 addr[6];
	u8 rsvd0;
	u8 rsvd1;
} __packed;

struct htt_rx_peer_unmap {
	u8 rsvd0;
	__le16 peer_id;
} __packed;

enum htt_security_types {
	HTT_SECURITY_NONE,
	HTT_SECURITY_WEP128,
	HTT_SECURITY_WEP104,
	HTT_SECURITY_WEP40,
	HTT_SECURITY_TKIP,
	HTT_SECURITY_TKIP_NOMIC,
	HTT_SECURITY_AES_CCMP,
	HTT_SECURITY_WAPI,

	HTT_NUM_SECURITY_TYPES /* keep this last! */
};

enum htt_security_flags {
#define HTT_SECURITY_TYPE_MASK 0x7F
#define HTT_SECURITY_TYPE_LSB  0
	HTT_SECURITY_IS_UNICAST = 1 << 7
};

struct htt_security_indication {
	union {
		/* dont use bitfields; undefined behaviour */
		u8 flags; /* %htt_security_flags */
		struct {
			u8 security_type:7, /* %htt_security_types */
			   is_unicast:1;
		} __packed;
	} __packed;
	__le16 peer_id;
	u8 michael_key[8];
	u8 wapi_rsc[16];
} __packed;

#define HTT_RX_BA_INFO0_TID_MASK     0x000F
#define HTT_RX_BA_INFO0_TID_LSB      0
#define HTT_RX_BA_INFO0_PEER_ID_MASK 0xFFF0
#define HTT_RX_BA_INFO0_PEER_ID_LSB  4

struct htt_rx_addba {
	u8 window_size;
	__le16 info0; /* %HTT_RX_BA_INFO0_ */
} __packed;

struct htt_rx_delba {
	u8 rsvd0;
	__le16 info0; /* %HTT_RX_BA_INFO0_ */
} __packed;

enum htt_data_tx_status {
	HTT_DATA_TX_STATUS_OK            = 0,
	HTT_DATA_TX_STATUS_DISCARD       = 1,
	HTT_DATA_TX_STATUS_NO_ACK        = 2,
	HTT_DATA_TX_STATUS_POSTPONE      = 3, /* HL only */
	HTT_DATA_TX_STATUS_DOWNLOAD_FAIL = 128
};

enum htt_data_tx_flags {
#define HTT_DATA_TX_STATUS_MASK 0x07
#define HTT_DATA_TX_STATUS_LSB  0
#define HTT_DATA_TX_TID_MASK    0x78
#define HTT_DATA_TX_TID_LSB     3
	HTT_DATA_TX_TID_INVALID = 1 << 7
};

#define HTT_TX_COMPL_INV_MSDU_ID 0xFFFF

struct htt_data_tx_completion {
	union {
		u8 flags;
		struct {
			u8 status:3,
			   tid:4,
			   tid_invalid:1;
		} __packed;
	} __packed;
	u8 num_msdus;
	u8 rsvd0;
	__le16 msdus[0]; /* variable length based on %num_msdus */
} __packed;

struct htt_tx_compl_ind_base {
	u32 hdr;
	u16 payload[1/*or more*/];
} __packed;

struct htt_rc_tx_done_params {
	u32 rate_code;
	u32 rate_code_flags;
	u32 flags;
	u32 num_enqued; /* 1 for non-AMPDU */
	u32 num_retries;
	u32 num_failed; /* for AMPDU */
	u32 ack_rssi;
	u32 time_stamp;
	u32 is_probe;
};

struct htt_rc_update {
	u8 vdev_id;
	__le16 peer_id;
	u8 addr[6];
	u8 num_elems;
	u8 rsvd0;
	struct htt_rc_tx_done_params params[0]; /* variable length %num_elems */
} __packed;

/* see htt_rx_indication for similar fields and descriptions */
struct htt_rx_fragment_indication {
	union {
		u8 info0; /* %HTT_RX_FRAG_IND_INFO0_ */
		struct {
			u8 ext_tid:5,
			   flush_valid:1;
		} __packed;
	} __packed;
	__le16 peer_id;
	__le32 info1; /* %HTT_RX_FRAG_IND_INFO1_ */
	__le16 fw_rx_desc_bytes;
	__le16 rsvd0;

	u8 fw_msdu_rx_desc[0];
} __packed;

#define HTT_RX_FRAG_IND_INFO0_EXT_TID_MASK     0x1F
#define HTT_RX_FRAG_IND_INFO0_EXT_TID_LSB      0
#define HTT_RX_FRAG_IND_INFO0_FLUSH_VALID_MASK 0x20
#define HTT_RX_FRAG_IND_INFO0_FLUSH_VALID_LSB  5

#define HTT_RX_FRAG_IND_INFO1_FLUSH_SEQ_NUM_START_MASK 0x0000003F
#define HTT_RX_FRAG_IND_INFO1_FLUSH_SEQ_NUM_START_LSB  0
#define HTT_RX_FRAG_IND_INFO1_FLUSH_SEQ_NUM_END_MASK   0x00000FC0
#define HTT_RX_FRAG_IND_INFO1_FLUSH_SEQ_NUM_END_LSB    6

struct htt_rx_pn_ind {
	__le16 peer_id;
	u8 tid;
	u8 seqno_start;
	u8 seqno_end;
	u8 pn_ie_count;
	u8 reserved;
	u8 pn_ies[0];
} __packed;

struct htt_rx_offload_msdu {
	__le16 msdu_len;
	__le16 peer_id;
	u8 vdev_id;
	u8 tid;
	u8 fw_desc;
	u8 payload[0];
} __packed;

struct htt_rx_offload_ind {
	u8 reserved;
	__le16 msdu_count;
} __packed;

struct htt_rx_in_ord_msdu_desc {
	__le32 msdu_paddr;
	__le16 msdu_len;
	u8 fw_desc;
	u8 reserved;
} __packed;

struct htt_rx_in_ord_ind {
	u8 info;
	__le16 peer_id;
	u8 vdev_id;
	u8 reserved;
	__le16 msdu_count;
	struct htt_rx_in_ord_msdu_desc msdu_descs[0];
} __packed;

#define HTT_RX_IN_ORD_IND_INFO_TID_MASK		0x0000001f
#define HTT_RX_IN_ORD_IND_INFO_TID_LSB		0
#define HTT_RX_IN_ORD_IND_INFO_OFFLOAD_MASK	0x00000020
#define HTT_RX_IN_ORD_IND_INFO_OFFLOAD_LSB	5
#define HTT_RX_IN_ORD_IND_INFO_FRAG_MASK	0x00000040
#define HTT_RX_IN_ORD_IND_INFO_FRAG_LSB		6

/*
 * target -> host test message definition
 *
 * The following field definitions describe the format of the test
 * message sent from the target to the host.
 * The message consists of a 4-octet header, followed by a variable
 * number of 32-bit integer values, followed by a variable number
 * of 8-bit character values.
 *
 * |31                         16|15           8|7            0|
 * |-----------------------------------------------------------|
 * |          num chars          |   num ints   |   msg type   |
 * |-----------------------------------------------------------|
 * |                           int 0                           |
 * |-----------------------------------------------------------|
 * |                           int 1                           |
 * |-----------------------------------------------------------|
 * |                            ...                            |
 * |-----------------------------------------------------------|
 * |    char 3    |    char 2    |    char 1    |    char 0    |
 * |-----------------------------------------------------------|
 * |              |              |      ...     |    char 4    |
 * |-----------------------------------------------------------|
 *   - MSG_TYPE
 *     Bits 7:0
 *     Purpose: identifies this as a test message
 *     Value: HTT_MSG_TYPE_TEST
 *   - NUM_INTS
 *     Bits 15:8
 *     Purpose: indicate how many 32-bit integers follow the message header
 *   - NUM_CHARS
 *     Bits 31:16
 *     Purpose: indicate how many 8-bit characters follow the series of integers
 */
struct htt_rx_test {
	u8 num_ints;
	__le16 num_chars;

	/* payload consists of 2 lists:
	 *  a) num_ints * sizeof(__le32)
	 *  b) num_chars * sizeof(u8) aligned to 4bytes
	 */
	u8 payload[0];
} __packed;

static inline __le32 *htt_rx_test_get_ints(struct htt_rx_test *rx_test)
{
	return (__le32 *)rx_test->payload;
}

static inline u8 *htt_rx_test_get_chars(struct htt_rx_test *rx_test)
{
	return rx_test->payload + (rx_test->num_ints * sizeof(__le32));
}

/*
 * target -> host packet log message
 *
 * The following field definitions describe the format of the packet log
 * message sent from the target to the host.
 * The message consists of a 4-octet header,followed by a variable number
 * of 32-bit character values.
 *
 * |31          24|23          16|15           8|7            0|
 * |-----------------------------------------------------------|
 * |              |              |              |   msg type   |
 * |-----------------------------------------------------------|
 * |                        payload                            |
 * |-----------------------------------------------------------|
 *   - MSG_TYPE
 *     Bits 7:0
 *     Purpose: identifies this as a test message
 *     Value: HTT_MSG_TYPE_PACKETLOG
 */
struct htt_pktlog_msg {
	u8 pad[3];
	u8 payload[0];
} __packed;

struct htt_dbg_stats_rx_reorder_stats {
	/* Non QoS MPDUs received */
	__le32 deliver_non_qos;

	/* MPDUs received in-order */
	__le32 deliver_in_order;

	/* Flush due to reorder timer expired */
	__le32 deliver_flush_timeout;

	/* Flush due to move out of window */
	__le32 deliver_flush_oow;

	/* Flush due to DELBA */
	__le32 deliver_flush_delba;

	/* MPDUs dropped due to FCS error */
	__le32 fcs_error;

	/* MPDUs dropped due to monitor mode non-data packet */
	__le32 mgmt_ctrl;

	/* MPDUs dropped due to invalid peer */
	__le32 invalid_peer;

	/* MPDUs dropped due to duplication (non aggregation) */
	__le32 dup_non_aggr;

	/* MPDUs dropped due to processed before */
	__le32 dup_past;

	/* MPDUs dropped due to duplicate in reorder queue */
	__le32 dup_in_reorder;

	/* Reorder timeout happened */
	__le32 reorder_timeout;

	/* invalid bar ssn */
	__le32 invalid_bar_ssn;

	/* reorder reset due to bar ssn */
	__le32 ssn_reset;
};

struct htt_dbg_stats_wal_tx_stats {
	/* Num HTT cookies queued to dispatch list */
	__le32 comp_queued;

	/* Num HTT cookies dispatched */
	__le32 comp_delivered;

	/* Num MSDU queued to WAL */
	__le32 msdu_enqued;

	/* Num MPDU queue to WAL */
	__le32 mpdu_enqued;

	/* Num MSDUs dropped by WMM limit */
	__le32 wmm_drop;

	/* Num Local frames queued */
	__le32 local_enqued;

	/* Num Local frames done */
	__le32 local_freed;

	/* Num queued to HW */
	__le32 hw_queued;

	/* Num PPDU reaped from HW */
	__le32 hw_reaped;

	/* Num underruns */
	__le32 underrun;

	/* Num PPDUs cleaned up in TX abort */
	__le32 tx_abort;

	/* Num MPDUs requed by SW */
	__le32 mpdus_requed;

	/* excessive retries */
	__le32 tx_ko;

	/* data hw rate code */
	__le32 data_rc;

	/* Scheduler self triggers */
	__le32 self_triggers;

	/* frames dropped due to excessive sw retries */
	__le32 sw_retry_failure;

	/* illegal rate phy errors  */
	__le32 illgl_rate_phy_err;

	/* wal pdev continuous xretry */
	__le32 pdev_cont_xretry;

	/* wal pdev continuous xretry */
	__le32 pdev_tx_timeout;

	/* wal pdev resets  */
	__le32 pdev_resets;

	__le32 phy_underrun;

	/* MPDU is more than txop limit */
	__le32 txop_ovf;
} __packed;

struct htt_dbg_stats_wal_rx_stats {
	/* Cnts any change in ring routing mid-ppdu */
	__le32 mid_ppdu_route_change;

	/* Total number of statuses processed */
	__le32 status_rcvd;

	/* Extra frags on rings 0-3 */
	__le32 r0_frags;
	__le32 r1_frags;
	__le32 r2_frags;
	__le32 r3_frags;

	/* MSDUs / MPDUs delivered to HTT */
	__le32 htt_msdus;
	__le32 htt_mpdus;

	/* MSDUs / MPDUs delivered to local stack */
	__le32 loc_msdus;
	__le32 loc_mpdus;

	/* AMSDUs that have more MSDUs than the status ring size */
	__le32 oversize_amsdu;

	/* Number of PHY errors */
	__le32 phy_errs;

	/* Number of PHY errors drops */
	__le32 phy_err_drop;

	/* Number of mpdu errors - FCS, MIC, ENC etc. */
	__le32 mpdu_errs;
} __packed;

struct htt_dbg_stats_wal_peer_stats {
	__le32 dummy; /* REMOVE THIS ONCE REAL PEER STAT COUNTERS ARE ADDED */
} __packed;

struct htt_dbg_stats_wal_pdev_txrx {
	struct htt_dbg_stats_wal_tx_stats tx_stats;
	struct htt_dbg_stats_wal_rx_stats rx_stats;
	struct htt_dbg_stats_wal_peer_stats peer_stats;
} __packed;

struct htt_dbg_stats_rx_rate_info {
	__le32 mcs[10];
	__le32 sgi[10];
	__le32 nss[4];
	__le32 stbc[10];
	__le32 bw[3];
	__le32 pream[6];
	__le32 ldpc;
	__le32 txbf;
};

/*
 * htt_dbg_stats_status -
 * present -     The requested stats have been delivered in full.
 *               This indicates that either the stats information was contained
 *               in its entirety within this message, or else this message
 *               completes the delivery of the requested stats info that was
 *               partially delivered through earlier STATS_CONF messages.
 * partial -     The requested stats have been delivered in part.
 *               One or more subsequent STATS_CONF messages with the same
 *               cookie value will be sent to deliver the remainder of the
 *               information.
 * error -       The requested stats could not be delivered, for example due
 *               to a shortage of memory to construct a message holding the
 *               requested stats.
 * invalid -     The requested stat type is either not recognized, or the
 *               target is configured to not gather the stats type in question.
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * series_done - This special value indicates that no further stats info
 *               elements are present within a series of stats info elems
 *               (within a stats upload confirmation message).
 */
enum htt_dbg_stats_status {
	HTT_DBG_STATS_STATUS_PRESENT     = 0,
	HTT_DBG_STATS_STATUS_PARTIAL     = 1,
	HTT_DBG_STATS_STATUS_ERROR       = 2,
	HTT_DBG_STATS_STATUS_INVALID     = 3,
	HTT_DBG_STATS_STATUS_SERIES_DONE = 7
};

/*
 * target -> host statistics upload
 *
 * The following field definitions describe the format of the HTT target
 * to host stats upload confirmation message.
 * The message contains a cookie echoed from the HTT host->target stats
 * upload request, which identifies which request the confirmation is
 * for, and a series of tag-length-value stats information elements.
 * The tag-length header for each stats info element also includes a
 * status field, to indicate whether the request for the stat type in
 * question was fully met, partially met, unable to be met, or invalid
 * (if the stat type in question is disabled in the target).
 * A special value of all 1's in this status field is used to indicate
 * the end of the series of stats info elements.
 *
 *
 * |31                         16|15           8|7   5|4       0|
 * |------------------------------------------------------------|
 * |                  reserved                  |    msg type   |
 * |------------------------------------------------------------|
 * |                        cookie LSBs                         |
 * |------------------------------------------------------------|
 * |                        cookie MSBs                         |
 * |------------------------------------------------------------|
 * |      stats entry length     |   reserved   |  S  |stat type|
 * |------------------------------------------------------------|
 * |                                                            |
 * |                  type-specific stats info                  |
 * |                                                            |
 * |------------------------------------------------------------|
 * |      stats entry length     |   reserved   |  S  |stat type|
 * |------------------------------------------------------------|
 * |                                                            |
 * |                  type-specific stats info                  |
 * |                                                            |
 * |------------------------------------------------------------|
 * |              n/a            |   reserved   | 111 |   n/a   |
 * |------------------------------------------------------------|
 * Header fields:
 *  - MSG_TYPE
 *    Bits 7:0
 *    Purpose: identifies this is a statistics upload confirmation message
 *    Value: 0x9
 *  - COOKIE_LSBS
 *    Bits 31:0
 *    Purpose: Provide a mechanism to match a target->host stats confirmation
 *        message with its preceding host->target stats request message.
 *    Value: LSBs of the opaque cookie specified by the host-side requestor
 *  - COOKIE_MSBS
 *    Bits 31:0
 *    Purpose: Provide a mechanism to match a target->host stats confirmation
 *        message with its preceding host->target stats request message.
 *    Value: MSBs of the opaque cookie specified by the host-side requestor
 *
 * Stats Information Element tag-length header fields:
 *  - STAT_TYPE
 *    Bits 4:0
 *    Purpose: identifies the type of statistics info held in the
 *        following information element
 *    Value: htt_dbg_stats_type
 *  - STATUS
 *    Bits 7:5
 *    Purpose: indicate whether the requested stats are present
 *    Value: htt_dbg_stats_status, including a special value (0x7) to mark
 *        the completion of the stats entry series
 *  - LENGTH
 *    Bits 31:16
 *    Purpose: indicate the stats information size
 *    Value: This field specifies the number of bytes of stats information
 *       that follows the element tag-length header.
 *       It is expected but not required that this length is a multiple of
 *       4 bytes.  Even if the length is not an integer multiple of 4, the
 *       subsequent stats entry header will begin on a 4-byte aligned
 *       boundary.
 */

#define HTT_STATS_CONF_ITEM_INFO_STAT_TYPE_MASK 0x1F
#define HTT_STATS_CONF_ITEM_INFO_STAT_TYPE_LSB  0
#define HTT_STATS_CONF_ITEM_INFO_STATUS_MASK    0xE0
#define HTT_STATS_CONF_ITEM_INFO_STATUS_LSB     5

struct htt_stats_conf_item {
	union {
		u8 info;
		struct {
			u8 stat_type:5; /* %HTT_DBG_STATS_ */
			u8 status:3; /* %HTT_DBG_STATS_STATUS_ */
		} __packed;
	} __packed;
	u8 pad;
	__le16 length;
	u8 payload[0]; /* roundup(length, 4) long */
} __packed;

struct htt_stats_conf {
	u8 pad[3];
	__le32 cookie_lsb;
	__le32 cookie_msb;

	/* each item has variable length! */
	struct htt_stats_conf_item items[0];
} __packed;

static inline struct htt_stats_conf_item *htt_stats_conf_next_item(
					const struct htt_stats_conf_item *item)
{
	return (void *)item + sizeof(*item) + roundup(item->length, 4);
}

/*
 * host -> target FRAG DESCRIPTOR/MSDU_EXT DESC bank
 *
 * The following field definitions describe the format of the HTT host
 * to target frag_desc/msdu_ext bank configuration message.
 * The message contains the based address and the min and max id of the
 * MSDU_EXT/FRAG_DESC that will be used by the HTT to map MSDU DESC and
 * MSDU_EXT/FRAG_DESC.
 * HTT will use id in HTT descriptor instead sending the frag_desc_ptr.
 * For QCA988X HW the firmware will use fragment_desc_ptr but in WIFI2.0
 * the hardware does the mapping/translation.
 *
 * Total banks that can be configured is configured to 16.
 *
 * This should be called before any TX has be initiated by the HTT
 *
 * |31                         16|15           8|7   5|4       0|
 * |------------------------------------------------------------|
 * | DESC_SIZE    |  NUM_BANKS   | RES |SWP|pdev|    msg type   |
 * |------------------------------------------------------------|
 * |                     BANK0_BASE_ADDRESS                     |
 * |------------------------------------------------------------|
 * |                            ...                             |
 * |------------------------------------------------------------|
 * |                    BANK15_BASE_ADDRESS                     |
 * |------------------------------------------------------------|
 * |       BANK0_MAX_ID          |       BANK0_MIN_ID           |
 * |------------------------------------------------------------|
 * |                            ...                             |
 * |------------------------------------------------------------|
 * |       BANK15_MAX_ID         |       BANK15_MIN_ID          |
 * |------------------------------------------------------------|
 * Header fields:
 *  - MSG_TYPE
 *    Bits 7:0
 *    Value: 0x6
 *  - BANKx_BASE_ADDRESS
 *    Bits 31:0
 *    Purpose: Provide a mechanism to specify the base address of the MSDU_EXT
 *         bank physical/bus address.
 *  - BANKx_MIN_ID
 *    Bits 15:0
 *    Purpose: Provide a mechanism to specify the min index that needs to
 *          mapped.
 *  - BANKx_MAX_ID
 *    Bits 31:16
 *    Purpose: Provide a mechanism to specify the max index that needs to
 *
 */
struct htt_frag_desc_bank_id {
	__le16 bank_min_id;
	__le16 bank_max_id;
} __packed;

/* real is 16 but it wouldn't fit in the max htt message size
 * so we use a conservatively safe value for now
 */
#define HTT_FRAG_DESC_BANK_MAX 4

#define HTT_FRAG_DESC_BANK_CFG_INFO_PDEV_ID_MASK		0x03
#define HTT_FRAG_DESC_BANK_CFG_INFO_PDEV_ID_LSB			0
#define HTT_FRAG_DESC_BANK_CFG_INFO_SWAP			BIT(2)
#define HTT_FRAG_DESC_BANK_CFG_INFO_Q_STATE_VALID		BIT(3)
#define HTT_FRAG_DESC_BANK_CFG_INFO_Q_STATE_DEPTH_TYPE_MASK	BIT(4)
#define HTT_FRAG_DESC_BANK_CFG_INFO_Q_STATE_DEPTH_TYPE_LSB	4

enum htt_q_depth_type {
	HTT_Q_DEPTH_TYPE_BYTES = 0,
	HTT_Q_DEPTH_TYPE_MSDUS = 1,
};

#define HTT_TX_Q_STATE_NUM_PEERS		(TARGET_10_4_NUM_QCACHE_PEERS_MAX + \
						 TARGET_10_4_NUM_VDEVS)
#define HTT_TX_Q_STATE_NUM_TIDS			8
#define HTT_TX_Q_STATE_ENTRY_SIZE		1
#define HTT_TX_Q_STATE_ENTRY_MULTIPLIER		0

/**
 * htt_q_state_conf - part of htt_frag_desc_bank_cfg for host q state config
 *
 * Defines host q state format and behavior. See htt_q_state.
 *
 * @record_size: Defines the size of each host q entry in bytes. In practice
 *	however firmware (at least 10.4.3-00191) ignores this host
 *	configuration value and uses hardcoded value of 1.
 * @record_multiplier: This is valid only when q depth type is MSDUs. It
 *	defines the exponent for the power of 2 multiplication.
 */
struct htt_q_state_conf {
	__le32 paddr;
	__le16 num_peers;
	__le16 num_tids;
	u8 record_size;
	u8 record_multiplier;
	u8 pad[2];
} __packed;

struct htt_frag_desc_bank_cfg {
	u8 info; /* HTT_FRAG_DESC_BANK_CFG_INFO_ */
	u8 num_banks;
	u8 desc_size;
	__le32 bank_base_addrs[HTT_FRAG_DESC_BANK_MAX];
	struct htt_frag_desc_bank_id bank_id[HTT_FRAG_DESC_BANK_MAX];
	struct htt_q_state_conf q_state;
} __packed;

#define HTT_TX_Q_STATE_ENTRY_COEFFICIENT	128
#define HTT_TX_Q_STATE_ENTRY_FACTOR_MASK	0x3f
#define HTT_TX_Q_STATE_ENTRY_FACTOR_LSB		0
#define HTT_TX_Q_STATE_ENTRY_EXP_MASK		0xc0
#define HTT_TX_Q_STATE_ENTRY_EXP_LSB		6

/**
 * htt_q_state - shared between host and firmware via DMA
 *
 * This structure is used for the host to expose it's software queue state to
 * firmware so that its rate control can schedule fetch requests for optimized
 * performance. This is most notably used for MU-MIMO aggregation when multiple
 * MU clients are connected.
 *
 * @count: Each element defines the host queue depth. When q depth type was
 *	configured as HTT_Q_DEPTH_TYPE_BYTES then each entry is defined as:
 *	FACTOR * 128 * 8^EXP (see HTT_TX_Q_STATE_ENTRY_FACTOR_MASK and
 *	HTT_TX_Q_STATE_ENTRY_EXP_MASK). When q depth type was configured as
 *	HTT_Q_DEPTH_TYPE_MSDUS the number of packets is scaled by 2 **
 *	record_multiplier (see htt_q_state_conf).
 * @map: Used by firmware to quickly check which host queues are not empty. It
 *	is a bitmap simply saying.
 * @seq: Used by firmware to quickly check if the host queues were updated
 *	since it last checked.
 *
 * FIXME: Is the q_state map[] size calculation really correct?
 */
struct htt_q_state {
	u8 count[HTT_TX_Q_STATE_NUM_TIDS][HTT_TX_Q_STATE_NUM_PEERS];
	u32 map[HTT_TX_Q_STATE_NUM_TIDS][(HTT_TX_Q_STATE_NUM_PEERS + 31) / 32];
	__le32 seq;
} __packed;

#define HTT_TX_FETCH_RECORD_INFO_PEER_ID_MASK	0x0fff
#define HTT_TX_FETCH_RECORD_INFO_PEER_ID_LSB	0
#define HTT_TX_FETCH_RECORD_INFO_TID_MASK	0xf000
#define HTT_TX_FETCH_RECORD_INFO_TID_LSB	12

struct htt_tx_fetch_record {
	__le16 info; /* HTT_TX_FETCH_IND_RECORD_INFO_ */
	__le16 num_msdus;
	__le32 num_bytes;
} __packed;

struct htt_tx_fetch_ind {
	u8 pad0;
	__le16 fetch_seq_num;
	__le32 token;
	__le16 num_resp_ids;
	__le16 num_records;
	struct htt_tx_fetch_record records[0];
	__le32 resp_ids[0]; /* ath10k_htt_get_tx_fetch_ind_resp_ids() */
} __packed;

static inline void *
ath10k_htt_get_tx_fetch_ind_resp_ids(struct htt_tx_fetch_ind *ind)
{
	return (void *)&ind->records[le16_to_cpu(ind->num_records)];
}

struct htt_tx_fetch_resp {
	u8 pad0;
	__le16 resp_id;
	__le16 fetch_seq_num;
	__le16 num_records;
	__le32 token;
	struct htt_tx_fetch_record records[0];
} __packed;

struct htt_tx_fetch_confirm {
	u8 pad0;
	__le16 num_resp_ids;
	__le32 resp_ids[0];
} __packed;

enum htt_tx_mode_switch_mode {
	HTT_TX_MODE_SWITCH_PUSH = 0,
	HTT_TX_MODE_SWITCH_PUSH_PULL = 1,
};

#define HTT_TX_MODE_SWITCH_IND_INFO0_ENABLE		BIT(0)
#define HTT_TX_MODE_SWITCH_IND_INFO0_NUM_RECORDS_MASK	0xfffe
#define HTT_TX_MODE_SWITCH_IND_INFO0_NUM_RECORDS_LSB	1

#define HTT_TX_MODE_SWITCH_IND_INFO1_MODE_MASK		0x0003
#define HTT_TX_MODE_SWITCH_IND_INFO1_MODE_LSB		0
#define HTT_TX_MODE_SWITCH_IND_INFO1_THRESHOLD_MASK	0xfffc
#define HTT_TX_MODE_SWITCH_IND_INFO1_THRESHOLD_LSB	2

#define HTT_TX_MODE_SWITCH_RECORD_INFO0_PEER_ID_MASK	0x0fff
#define HTT_TX_MODE_SWITCH_RECORD_INFO0_PEER_ID_LSB	0
#define HTT_TX_MODE_SWITCH_RECORD_INFO0_TID_MASK	0xf000
#define HTT_TX_MODE_SWITCH_RECORD_INFO0_TID_LSB		12

struct htt_tx_mode_switch_record {
	__le16 info0; /* HTT_TX_MODE_SWITCH_RECORD_INFO0_ */
	__le16 num_max_msdus;
} __packed;

struct htt_tx_mode_switch_ind {
	u8 pad0;
	__le16 info0; /* HTT_TX_MODE_SWITCH_IND_INFO0_ */
	__le16 info1; /* HTT_TX_MODE_SWITCH_IND_INFO1_ */
	u8 pad1[2];
	struct htt_tx_mode_switch_record records[0];
} __packed;

struct htt_channel_change {
	u8 pad[3];
	__le32 freq;
	__le32 center_freq1;
	__le32 center_freq2;
	__le32 phymode;
} __packed;

struct htt_per_peer_tx_stats_ind {
	__le32	succ_bytes;
	__le32  retry_bytes;
	__le32  failed_bytes;
	u8	ratecode;
	u8	flags;
	__le16	peer_id;
	__le16  succ_pkts;
	__le16	retry_pkts;
	__le16	failed_pkts;
	__le16	tx_duration;
	__le32	reserved1;
	__le32	reserved2;
} __packed;

struct htt_peer_tx_stats {
	u8 num_ppdu;
	u8 ppdu_len;
	u8 version;
	u8 payload[0];
} __packed;

#define ATH10K_10_2_TX_STATS_OFFSET	136
#define PEER_STATS_FOR_NO_OF_PPDUS	4

struct ath10k_10_2_peer_tx_stats {
	u8 ratecode[PEER_STATS_FOR_NO_OF_PPDUS];
	u8 success_pkts[PEER_STATS_FOR_NO_OF_PPDUS];
	__le16 success_bytes[PEER_STATS_FOR_NO_OF_PPDUS];
	u8 retry_pkts[PEER_STATS_FOR_NO_OF_PPDUS];
	__le16 retry_bytes[PEER_STATS_FOR_NO_OF_PPDUS];
	u8 failed_pkts[PEER_STATS_FOR_NO_OF_PPDUS];
	__le16 failed_bytes[PEER_STATS_FOR_NO_OF_PPDUS];
	u8 flags[PEER_STATS_FOR_NO_OF_PPDUS];
	__le32 tx_duration;
	u8 tx_ppdu_cnt;
	u8 peer_id;
} __packed;

union htt_rx_pn_t {
	/* WEP: 24-bit PN */
	u32 pn24;

	/* TKIP or CCMP: 48-bit PN */
	u64 pn48;

	/* WAPI: 128-bit PN */
	u64 pn128[2];
};

struct htt_cmd {
	struct htt_cmd_hdr hdr;
	union {
		struct htt_ver_req ver_req;
		struct htt_mgmt_tx_desc mgmt_tx;
		struct htt_data_tx_desc data_tx;
		struct htt_rx_ring_setup rx_setup;
		struct htt_stats_req stats_req;
		struct htt_oob_sync_req oob_sync_req;
		struct htt_aggr_conf aggr_conf;
		struct htt_frag_desc_bank_cfg frag_desc_bank_cfg;
		struct htt_tx_fetch_resp tx_fetch_resp;
	};
} __packed;

struct htt_resp {
	struct htt_resp_hdr hdr;
	union {
		struct htt_ver_resp ver_resp;
		struct htt_mgmt_tx_completion mgmt_tx_completion;
		struct htt_data_tx_completion data_tx_completion;
		struct htt_rx_indication rx_ind;
		struct htt_rx_fragment_indication rx_frag_ind;
		struct htt_rx_peer_map peer_map;
		struct htt_rx_peer_unmap peer_unmap;
		struct htt_rx_flush rx_flush;
		struct htt_rx_addba rx_addba;
		struct htt_rx_delba rx_delba;
		struct htt_security_indication security_indication;
		struct htt_rc_update rc_update;
		struct htt_rx_test rx_test;
		struct htt_pktlog_msg pktlog_msg;
		struct htt_stats_conf stats_conf;
		struct htt_rx_pn_ind rx_pn_ind;
		struct htt_rx_offload_ind rx_offload_ind;
		struct htt_rx_in_ord_ind rx_in_ord_ind;
		struct htt_tx_fetch_ind tx_fetch_ind;
		struct htt_tx_fetch_confirm tx_fetch_confirm;
		struct htt_tx_mode_switch_ind tx_mode_switch_ind;
		struct htt_channel_change chan_change;
		struct htt_peer_tx_stats peer_tx_stats;
	};
} __packed;

/*** host side structures follow ***/

struct htt_tx_done {
	u16 msdu_id;
	u16 status;
};

enum htt_tx_compl_state {
	HTT_TX_COMPL_STATE_NONE,
	HTT_TX_COMPL_STATE_ACK,
	HTT_TX_COMPL_STATE_NOACK,
	HTT_TX_COMPL_STATE_DISCARD,
};

struct htt_peer_map_event {
	u8 vdev_id;
	u16 peer_id;
	u8 addr[ETH_ALEN];
};

struct htt_peer_unmap_event {
	u16 peer_id;
};

struct ath10k_htt_txbuf {
	struct htt_data_tx_desc_frag frags[2];
	struct ath10k_htc_hdr htc_hdr;
	struct htt_cmd_hdr cmd_hdr;
	struct htt_data_tx_desc cmd_tx;
} __packed;

struct ath10k_htt {
	struct ath10k *ar;
	enum ath10k_htc_ep_id eid;

	u8 target_version_major;
	u8 target_version_minor;
	struct completion target_version_received;
	u8 max_num_amsdu;
	u8 max_num_ampdu;

	const enum htt_t2h_msg_type *t2h_msg_types;
	u32 t2h_msg_types_max;

	struct {
		/*
		 * Ring of network buffer objects - This ring is
		 * used exclusively by the host SW. This ring
		 * mirrors the dev_addrs_ring that is shared
		 * between the host SW and the MAC HW. The host SW
		 * uses this netbufs ring to locate the network
		 * buffer objects whose data buffers the HW has
		 * filled.
		 */
		struct sk_buff **netbufs_ring;

		/* This is used only with firmware supporting IN_ORD_IND.
		 *
		 * With Full Rx Reorder the HTT Rx Ring is more of a temporary
		 * buffer ring from which buffer addresses are copied by the
		 * firmware to MAC Rx ring. Firmware then delivers IN_ORD_IND
		 * pointing to specific (re-ordered) buffers.
		 *
		 * FIXME: With kernel generic hashing functions there's a lot
		 * of hash collisions for sk_buffs.
		 */
		bool in_ord_rx;
		DECLARE_HASHTABLE(skb_table, 4);

		/*
		 * Ring of buffer addresses -
		 * This ring holds the "physical" device address of the
		 * rx buffers the host SW provides for the MAC HW to
		 * fill.
		 */
		__le32 *paddrs_ring;

		/*
		 * Base address of ring, as a "physical" device address
		 * rather than a CPU address.
		 */
		dma_addr_t base_paddr;

		/* how many elems in the ring (power of 2) */
		int size;

		/* size - 1 */
		unsigned int size_mask;

		/* how many rx buffers to keep in the ring */
		int fill_level;

		/* how many rx buffers (full+empty) are in the ring */
		int fill_cnt;

		/*
		 * alloc_idx - where HTT SW has deposited empty buffers
		 * This is allocated in consistent mem, so that the FW can
		 * read this variable, and program the HW's FW_IDX reg with
		 * the value of this shadow register.
		 */
		struct {
			__le32 *vaddr;
			dma_addr_t paddr;
		} alloc_idx;

		/* where HTT SW has processed bufs filled by rx MAC DMA */
		struct {
			unsigned int msdu_payld;
		} sw_rd_idx;

		/*
		 * refill_retry_timer - timer triggered when the ring is
		 * not refilled to the level expected
		 */
		struct timer_list refill_retry_timer;

		/* Protects access to all rx ring buffer state variables */
		spinlock_t lock;
	} rx_ring;

	unsigned int prefetch_len;

	/* Protects access to pending_tx, num_pending_tx */
	spinlock_t tx_lock;
	int max_num_pending_tx;
	int num_pending_tx;
	int num_pending_mgmt_tx;
	struct idr pending_tx;
	wait_queue_head_t empty_tx_wq;

	/* FIFO for storing tx done status {ack, no-ack, discard} and msdu id */
	DECLARE_KFIFO_PTR(txdone_fifo, struct htt_tx_done);

	/* set if host-fw communication goes haywire
	 * used to avoid further failures
	 */
	bool rx_confused;
	atomic_t num_mpdus_ready;

	/* This is used to group tx/rx completions separately and process them
	 * in batches to reduce cache stalls
	 */
	struct sk_buff_head rx_msdus_q;
	struct sk_buff_head rx_in_ord_compl_q;
	struct sk_buff_head tx_fetch_ind_q;

	/* rx_status template */
	struct ieee80211_rx_status rx_status;

	struct {
		dma_addr_t paddr;
		struct htt_msdu_ext_desc *vaddr;
	} frag_desc;

	struct {
		dma_addr_t paddr;
		struct ath10k_htt_txbuf *vaddr;
	} txbuf;

	struct {
		bool enabled;
		struct htt_q_state *vaddr;
		dma_addr_t paddr;
		u16 num_push_allowed;
		u16 num_peers;
		u16 num_tids;
		enum htt_tx_mode_switch_mode mode;
		enum htt_q_depth_type type;
	} tx_q_state;

	bool tx_mem_allocated;
};

#define RX_HTT_HDR_STATUS_LEN 64

/* This structure layout is programmed via rx ring setup
 * so that FW knows how to transfer the rx descriptor to the host.
 * Buffers like this are placed on the rx ring.
 */
struct htt_rx_desc {
	union {
		/* This field is filled on the host using the msdu buffer
		 * from htt_rx_indication
		 */
		struct fw_rx_desc_base fw_desc;
		u32 pad;
	} __packed;
	struct {
		struct rx_attention attention;
		struct rx_frag_info frag_info;
		struct rx_mpdu_start mpdu_start;
		struct rx_msdu_start msdu_start;
		struct rx_msdu_end msdu_end;
		struct rx_mpdu_end mpdu_end;
		struct rx_ppdu_start ppdu_start;
		struct rx_ppdu_end ppdu_end;
	} __packed;
	u8 rx_hdr_status[RX_HTT_HDR_STATUS_LEN];
	u8 msdu_payload[0];
};

#define HTT_RX_DESC_ALIGN 8

#define HTT_MAC_ADDR_LEN 6

/*
 * FIX THIS
 * Should be: sizeof(struct htt_host_rx_desc) + max rx MSDU size,
 * rounded up to a cache line size.
 */
#define HTT_RX_BUF_SIZE 1920
#define HTT_RX_MSDU_SIZE (HTT_RX_BUF_SIZE - (int)sizeof(struct htt_rx_desc))

/* Refill a bunch of RX buffers for each refill round so that FW/HW can handle
 * aggregated traffic more nicely.
 */
#define ATH10K_HTT_MAX_NUM_REFILL 100

/*
 * DMA_MAP expects the buffer to be an integral number of cache lines.
 * Rather than checking the actual cache line size, this code makes a
 * conservative estimate of what the cache line size could be.
 */
#define HTT_LOG2_MAX_CACHE_LINE_SIZE 7	/* 2^7 = 128 */
#define HTT_MAX_CACHE_LINE_SIZE_MASK ((1 << HTT_LOG2_MAX_CACHE_LINE_SIZE) - 1)

/* These values are default in most firmware revisions and apparently are a
 * sweet spot performance wise.
 */
#define ATH10K_HTT_MAX_NUM_AMSDU_DEFAULT 3
#define ATH10K_HTT_MAX_NUM_AMPDU_DEFAULT 64

int ath10k_htt_connect(struct ath10k_htt *htt);
int ath10k_htt_init(struct ath10k *ar);
int ath10k_htt_setup(struct ath10k_htt *htt);

int ath10k_htt_tx_start(struct ath10k_htt *htt);
void ath10k_htt_tx_stop(struct ath10k_htt *htt);
void ath10k_htt_tx_destroy(struct ath10k_htt *htt);
void ath10k_htt_tx_free(struct ath10k_htt *htt);

int ath10k_htt_rx_alloc(struct ath10k_htt *htt);
int ath10k_htt_rx_ring_refill(struct ath10k *ar);
void ath10k_htt_rx_free(struct ath10k_htt *htt);

void ath10k_htt_htc_tx_complete(struct ath10k *ar, struct sk_buff *skb);
void ath10k_htt_htc_t2h_msg_handler(struct ath10k *ar, struct sk_buff *skb);
bool ath10k_htt_t2h_msg_handler(struct ath10k *ar, struct sk_buff *skb);
int ath10k_htt_h2t_ver_req_msg(struct ath10k_htt *htt);
int ath10k_htt_h2t_stats_req(struct ath10k_htt *htt, u8 mask, u64 cookie);
int ath10k_htt_send_frag_desc_bank_cfg(struct ath10k_htt *htt);
int ath10k_htt_send_rx_ring_cfg_ll(struct ath10k_htt *htt);
int ath10k_htt_h2t_aggr_cfg_msg(struct ath10k_htt *htt,
				u8 max_subfrms_ampdu,
				u8 max_subfrms_amsdu);
void ath10k_htt_hif_tx_complete(struct ath10k *ar, struct sk_buff *skb);
int ath10k_htt_tx_fetch_resp(struct ath10k *ar,
			     __le32 token,
			     __le16 fetch_seq_num,
			     struct htt_tx_fetch_record *records,
			     size_t num_records);

void ath10k_htt_tx_txq_update(struct ieee80211_hw *hw,
			      struct ieee80211_txq *txq);
void ath10k_htt_tx_txq_recalc(struct ieee80211_hw *hw,
			      struct ieee80211_txq *txq);
void ath10k_htt_tx_txq_sync(struct ath10k *ar);
void ath10k_htt_tx_dec_pending(struct ath10k_htt *htt);
int ath10k_htt_tx_inc_pending(struct ath10k_htt *htt);
void ath10k_htt_tx_mgmt_dec_pending(struct ath10k_htt *htt);
int ath10k_htt_tx_mgmt_inc_pending(struct ath10k_htt *htt, bool is_mgmt,
				   bool is_presp);

int ath10k_htt_tx_alloc_msdu_id(struct ath10k_htt *htt, struct sk_buff *skb);
void ath10k_htt_tx_free_msdu_id(struct ath10k_htt *htt, u16 msdu_id);
int ath10k_htt_mgmt_tx(struct ath10k_htt *htt, struct sk_buff *msdu);
int ath10k_htt_tx(struct ath10k_htt *htt,
		  enum ath10k_hw_txrx_mode txmode,
		  struct sk_buff *msdu);
void ath10k_htt_rx_pktlog_completion_handler(struct ath10k *ar,
					     struct sk_buff *skb);
int ath10k_htt_txrx_compl_task(struct ath10k *ar, int budget);

#endif
