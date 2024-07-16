/* SPDX-License-Identifier: (GPL-2.0 OR MIT)
 * Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

/* GVE DQO Descriptor formats */

#ifndef _GVE_DESC_DQO_H_
#define _GVE_DESC_DQO_H_

#include <linux/build_bug.h>

#define GVE_TX_MAX_HDR_SIZE_DQO 255
#define GVE_TX_MIN_TSO_MSS_DQO 88

#ifndef __LITTLE_ENDIAN_BITFIELD
#error "Only little endian supported"
#endif

/* Basic TX descriptor (DTYPE 0x0C) */
struct gve_tx_pkt_desc_dqo {
	__le64 buf_addr;

	/* Must be GVE_TX_PKT_DESC_DTYPE_DQO (0xc) */
	u8 dtype: 5;

	/* Denotes the last descriptor of a packet. */
	u8 end_of_packet: 1;
	u8 checksum_offload_enable: 1;

	/* If set, will generate a descriptor completion for this descriptor. */
	u8 report_event: 1;
	u8 reserved0;
	__le16 reserved1;

	/* The TX completion associated with this packet will contain this tag.
	 */
	__le16 compl_tag;
	u16 buf_size: 14;
	u16 reserved2: 2;
} __packed;
static_assert(sizeof(struct gve_tx_pkt_desc_dqo) == 16);

#define GVE_TX_PKT_DESC_DTYPE_DQO 0xc
#define GVE_TX_MAX_BUF_SIZE_DQO ((16 * 1024) - 1)

/* Maximum number of data descriptors allowed per packet, or per-TSO segment. */
#define GVE_TX_MAX_DATA_DESCS 10

/* Min gap between tail and head to avoid cacheline overlap */
#define GVE_TX_MIN_DESC_PREVENT_CACHE_OVERLAP 4

/* "report_event" on TX packet descriptors may only be reported on the last
 * descriptor of a TX packet, and they must be spaced apart with at least this
 * value.
 */
#define GVE_TX_MIN_RE_INTERVAL 32

struct gve_tx_context_cmd_dtype {
	u8 dtype: 5;
	u8 tso: 1;
	u8 reserved1: 2;

	u8 reserved2;
};

static_assert(sizeof(struct gve_tx_context_cmd_dtype) == 2);

/* TX Native TSO Context DTYPE (0x05)
 *
 * "flex" fields allow the driver to send additional packet context to HW.
 */
struct gve_tx_tso_context_desc_dqo {
	/* The L4 payload bytes that should be segmented. */
	u32 tso_total_len: 24;
	u32 flex10: 8;

	/* Max segment size in TSO excluding headers. */
	u16 mss: 14;
	u16 reserved: 2;

	u8 header_len; /* Header length to use for TSO offload */
	u8 flex11;
	struct gve_tx_context_cmd_dtype cmd_dtype;
	u8 flex0;
	u8 flex5;
	u8 flex6;
	u8 flex7;
	u8 flex8;
	u8 flex9;
} __packed;
static_assert(sizeof(struct gve_tx_tso_context_desc_dqo) == 16);

#define GVE_TX_TSO_CTX_DESC_DTYPE_DQO 0x5

/* General context descriptor for sending metadata. */
struct gve_tx_general_context_desc_dqo {
	u8 flex4;
	u8 flex5;
	u8 flex6;
	u8 flex7;
	u8 flex8;
	u8 flex9;
	u8 flex10;
	u8 flex11;
	struct gve_tx_context_cmd_dtype cmd_dtype;
	u16 reserved;
	u8 flex0;
	u8 flex1;
	u8 flex2;
	u8 flex3;
} __packed;
static_assert(sizeof(struct gve_tx_general_context_desc_dqo) == 16);

#define GVE_TX_GENERAL_CTX_DESC_DTYPE_DQO 0x4

/* Logical structure of metadata which is packed into context descriptor flex
 * fields.
 */
struct gve_tx_metadata_dqo {
	union {
		struct {
			u8 version;

			/* If `skb->l4_hash` is set, this value should be
			 * derived from `skb->hash`.
			 *
			 * A zero value means no l4_hash was associated with the
			 * skb.
			 */
			u16 path_hash: 15;

			/* Should be set to 1 if the flow associated with the
			 * skb had a rehash from the TCP stack.
			 */
			u16 rehash_event: 1;
		}  __packed;
		u8 bytes[12];
	};
}  __packed;
static_assert(sizeof(struct gve_tx_metadata_dqo) == 12);

#define GVE_TX_METADATA_VERSION_DQO 0

/* TX completion descriptor */
struct gve_tx_compl_desc {
	/* For types 0-4 this is the TX queue ID associated with this
	 * completion.
	 */
	u16 id: 11;

	/* See: GVE_COMPL_TYPE_DQO* */
	u16 type: 3;
	u16 reserved0: 1;

	/* Flipped by HW to notify the descriptor is populated. */
	u16 generation: 1;
	union {
		/* For descriptor completions, this is the last index fetched
		 * by HW + 1.
		 */
		__le16 tx_head;

		/* For packet completions, this is the completion tag set on the
		 * TX packet descriptors.
		 */
		__le16 completion_tag;
	};
	__le32 reserved1;
} __packed;
static_assert(sizeof(struct gve_tx_compl_desc) == 8);

#define GVE_COMPL_TYPE_DQO_PKT 0x2 /* Packet completion */
#define GVE_COMPL_TYPE_DQO_DESC 0x4 /* Descriptor completion */
#define GVE_COMPL_TYPE_DQO_MISS 0x1 /* Miss path completion */
#define GVE_COMPL_TYPE_DQO_REINJECTION 0x3 /* Re-injection completion */

/* Descriptor to post buffers to HW on buffer queue. */
struct gve_rx_desc_dqo {
	__le16 buf_id; /* ID returned in Rx completion descriptor */
	__le16 reserved0;
	__le32 reserved1;
	__le64 buf_addr; /* DMA address of the buffer */
	__le64 header_buf_addr;
	__le64 reserved2;
} __packed;
static_assert(sizeof(struct gve_rx_desc_dqo) == 32);

/* Descriptor for HW to notify SW of new packets received on RX queue. */
struct gve_rx_compl_desc_dqo {
	/* Must be 1 */
	u8 rxdid: 4;
	u8 reserved0: 4;

	/* Packet originated from this system rather than the network. */
	u8 loopback: 1;
	/* Set when IPv6 packet contains a destination options header or routing
	 * header.
	 */
	u8 ipv6_ex_add: 1;
	/* Invalid packet was received. */
	u8 rx_error: 1;
	u8 reserved1: 5;

	u16 packet_type: 10;
	u16 ip_hdr_err: 1;
	u16 udp_len_err: 1;
	u16 raw_cs_invalid: 1;
	u16 reserved2: 3;

	u16 packet_len: 14;
	/* Flipped by HW to notify the descriptor is populated. */
	u16 generation: 1;
	/* Should be zero. */
	u16 buffer_queue_id: 1;

	u16 header_len: 10;
	u16 rsc: 1;
	u16 split_header: 1;
	u16 reserved3: 4;

	u8 descriptor_done: 1;
	u8 end_of_packet: 1;
	u8 header_buffer_overflow: 1;
	u8 l3_l4_processed: 1;
	u8 csum_ip_err: 1;
	u8 csum_l4_err: 1;
	u8 csum_external_ip_err: 1;
	u8 csum_external_udp_err: 1;

	u8 status_error1;

	__le16 reserved5;
	__le16 buf_id; /* Buffer ID which was sent on the buffer queue. */

	union {
		/* Packet checksum. */
		__le16 raw_cs;
		/* Segment length for RSC packets. */
		__le16 rsc_seg_len;
	};
	__le32 hash;
	__le32 reserved6;
	__le64 reserved7;
} __packed;

static_assert(sizeof(struct gve_rx_compl_desc_dqo) == 32);

/* Ringing the doorbell too often can hurt performance.
 *
 * HW requires this value to be at least 8.
 */
#define GVE_RX_BUF_THRESH_DQO 32

#endif /* _GVE_DESC_DQO_H_ */
