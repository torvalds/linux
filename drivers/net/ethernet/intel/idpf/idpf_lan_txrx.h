/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023 Intel Corporation */

#ifndef _IDPF_LAN_TXRX_H_
#define _IDPF_LAN_TXRX_H_

enum idpf_rss_hash {
	IDPF_HASH_INVALID			= 0,
	/* Values 1 - 28 are reserved for future use */
	IDPF_HASH_NONF_UNICAST_IPV4_UDP		= 29,
	IDPF_HASH_NONF_MULTICAST_IPV4_UDP,
	IDPF_HASH_NONF_IPV4_UDP,
	IDPF_HASH_NONF_IPV4_TCP_SYN_NO_ACK,
	IDPF_HASH_NONF_IPV4_TCP,
	IDPF_HASH_NONF_IPV4_SCTP,
	IDPF_HASH_NONF_IPV4_OTHER,
	IDPF_HASH_FRAG_IPV4,
	/* Values 37-38 are reserved */
	IDPF_HASH_NONF_UNICAST_IPV6_UDP		= 39,
	IDPF_HASH_NONF_MULTICAST_IPV6_UDP,
	IDPF_HASH_NONF_IPV6_UDP,
	IDPF_HASH_NONF_IPV6_TCP_SYN_NO_ACK,
	IDPF_HASH_NONF_IPV6_TCP,
	IDPF_HASH_NONF_IPV6_SCTP,
	IDPF_HASH_NONF_IPV6_OTHER,
	IDPF_HASH_FRAG_IPV6,
	IDPF_HASH_NONF_RSVD47,
	IDPF_HASH_NONF_FCOE_OX,
	IDPF_HASH_NONF_FCOE_RX,
	IDPF_HASH_NONF_FCOE_OTHER,
	/* Values 51-62 are reserved */
	IDPF_HASH_L2_PAYLOAD			= 63,

	IDPF_HASH_MAX
};

/* Supported RSS offloads */
#define IDPF_DEFAULT_RSS_HASH			\
	(BIT_ULL(IDPF_HASH_NONF_IPV4_UDP) |	\
	BIT_ULL(IDPF_HASH_NONF_IPV4_SCTP) |	\
	BIT_ULL(IDPF_HASH_NONF_IPV4_TCP) |	\
	BIT_ULL(IDPF_HASH_NONF_IPV4_OTHER) |	\
	BIT_ULL(IDPF_HASH_FRAG_IPV4) |		\
	BIT_ULL(IDPF_HASH_NONF_IPV6_UDP) |	\
	BIT_ULL(IDPF_HASH_NONF_IPV6_TCP) |	\
	BIT_ULL(IDPF_HASH_NONF_IPV6_SCTP) |	\
	BIT_ULL(IDPF_HASH_NONF_IPV6_OTHER) |	\
	BIT_ULL(IDPF_HASH_FRAG_IPV6) |		\
	BIT_ULL(IDPF_HASH_L2_PAYLOAD))

#define IDPF_DEFAULT_RSS_HASH_EXPANDED (IDPF_DEFAULT_RSS_HASH | \
	BIT_ULL(IDPF_HASH_NONF_IPV4_TCP_SYN_NO_ACK) |		\
	BIT_ULL(IDPF_HASH_NONF_UNICAST_IPV4_UDP) |		\
	BIT_ULL(IDPF_HASH_NONF_MULTICAST_IPV4_UDP) |		\
	BIT_ULL(IDPF_HASH_NONF_IPV6_TCP_SYN_NO_ACK) |		\
	BIT_ULL(IDPF_HASH_NONF_UNICAST_IPV6_UDP) |		\
	BIT_ULL(IDPF_HASH_NONF_MULTICAST_IPV6_UDP))

#define IDPF_TXD_CTX_QW1_MSS_S		50
#define IDPF_TXD_CTX_QW1_MSS_M		GENMASK_ULL(63, 50)
#define IDPF_TXD_CTX_QW1_TSO_LEN_S	30
#define IDPF_TXD_CTX_QW1_TSO_LEN_M	GENMASK_ULL(47, 30)
#define IDPF_TXD_CTX_QW1_CMD_S		4
#define IDPF_TXD_CTX_QW1_CMD_M		GENMASK_ULL(15, 4)
#define IDPF_TXD_CTX_QW1_DTYPE_S	0
#define IDPF_TXD_CTX_QW1_DTYPE_M	GENMASK_ULL(3, 0)
#define IDPF_TXD_QW1_L2TAG1_S		48
#define IDPF_TXD_QW1_L2TAG1_M		GENMASK_ULL(63, 48)
#define IDPF_TXD_QW1_TX_BUF_SZ_S	34
#define IDPF_TXD_QW1_TX_BUF_SZ_M	GENMASK_ULL(47, 34)
#define IDPF_TXD_QW1_OFFSET_S		16
#define IDPF_TXD_QW1_OFFSET_M		GENMASK_ULL(33, 16)
#define IDPF_TXD_QW1_CMD_S		4
#define IDPF_TXD_QW1_CMD_M		GENMASK_ULL(15, 4)
#define IDPF_TXD_QW1_DTYPE_S		0
#define IDPF_TXD_QW1_DTYPE_M		GENMASK_ULL(3, 0)

enum idpf_tx_desc_dtype_value {
	IDPF_TX_DESC_DTYPE_DATA				= 0,
	IDPF_TX_DESC_DTYPE_CTX				= 1,
	/* DTYPE 2 is reserved
	 * DTYPE 3 is free for future use
	 * DTYPE 4 is reserved
	 */
	IDPF_TX_DESC_DTYPE_FLEX_TSO_CTX			= 5,
	/* DTYPE 6 is reserved */
	IDPF_TX_DESC_DTYPE_FLEX_L2TAG1_L2TAG2		= 7,
	/* DTYPE 8, 9 are free for future use
	 * DTYPE 10 is reserved
	 * DTYPE 11 is free for future use
	 */
	IDPF_TX_DESC_DTYPE_FLEX_FLOW_SCHE		= 12,
	/* DTYPE 13, 14 are free for future use */

	/* DESC_DONE - HW has completed write-back of descriptor */
	IDPF_TX_DESC_DTYPE_DESC_DONE			= 15,
};

enum idpf_tx_base_desc_cmd_bits {
	IDPF_TX_DESC_CMD_EOP			= BIT(0),
	IDPF_TX_DESC_CMD_RS			= BIT(1),
	 /* only on VFs else RSVD */
	IDPF_TX_DESC_CMD_ICRC			= BIT(2),
	IDPF_TX_DESC_CMD_IL2TAG1		= BIT(3),
	IDPF_TX_DESC_CMD_RSVD1			= BIT(4),
	IDPF_TX_DESC_CMD_IIPT_IPV6		= BIT(5),
	IDPF_TX_DESC_CMD_IIPT_IPV4		= BIT(6),
	IDPF_TX_DESC_CMD_IIPT_IPV4_CSUM		= GENMASK(6, 5),
	IDPF_TX_DESC_CMD_RSVD2			= BIT(7),
	IDPF_TX_DESC_CMD_L4T_EOFT_TCP		= BIT(8),
	IDPF_TX_DESC_CMD_L4T_EOFT_SCTP		= BIT(9),
	IDPF_TX_DESC_CMD_L4T_EOFT_UDP		= GENMASK(9, 8),
	IDPF_TX_DESC_CMD_RSVD3			= BIT(10),
	IDPF_TX_DESC_CMD_RSVD4			= BIT(11),
};

/* Transmit descriptors  */
/* splitq tx buf, singleq tx buf and singleq compl desc */
struct idpf_base_tx_desc {
	__le64 buf_addr; /* Address of descriptor's data buf */
	__le64 qw1; /* type_cmd_offset_bsz_l2tag1 */
}; /* read used with buffer queues */

struct idpf_splitq_tx_compl_desc {
	/* qid=[10:0] comptype=[13:11] rsvd=[14] gen=[15] */
	__le16 qid_comptype_gen;
	union {
		__le16 q_head; /* Queue head */
		__le16 compl_tag; /* Completion tag */
	} q_head_compl_tag;
	u8 ts[3];
	u8 rsvd; /* Reserved */
}; /* writeback used with completion queues */

/* Common cmd field defines for all desc except Flex Flow Scheduler (0x0C) */
enum idpf_tx_flex_desc_cmd_bits {
	IDPF_TX_FLEX_DESC_CMD_EOP			= BIT(0),
	IDPF_TX_FLEX_DESC_CMD_RS			= BIT(1),
	IDPF_TX_FLEX_DESC_CMD_RE			= BIT(2),
	IDPF_TX_FLEX_DESC_CMD_IL2TAG1			= BIT(3),
	IDPF_TX_FLEX_DESC_CMD_DUMMY			= BIT(4),
	IDPF_TX_FLEX_DESC_CMD_CS_EN			= BIT(5),
	IDPF_TX_FLEX_DESC_CMD_FILT_AU_EN		= BIT(6),
	IDPF_TX_FLEX_DESC_CMD_FILT_AU_EVICT		= BIT(7),
};

struct idpf_flex_tx_desc {
	__le64 buf_addr;	/* Packet buffer address */
	struct {
#define IDPF_FLEX_TXD_QW1_DTYPE_S	0
#define IDPF_FLEX_TXD_QW1_DTYPE_M	GENMASK(4, 0)
#define IDPF_FLEX_TXD_QW1_CMD_S		5
#define IDPF_FLEX_TXD_QW1_CMD_M		GENMASK(15, 5)
		__le16 cmd_dtype;
		/* DTYPE=IDPF_TX_DESC_DTYPE_FLEX_L2TAG1_L2TAG2 (0x07) */
		struct {
			__le16 l2tag1;
			__le16 l2tag2;
		} l2tags;
		__le16 buf_size;
	} qw1;
};

struct idpf_flex_tx_sched_desc {
	__le64 buf_addr;	/* Packet buffer address */

	/* DTYPE = IDPF_TX_DESC_DTYPE_FLEX_FLOW_SCHE_16B (0x0C) */
	struct {
		u8 cmd_dtype;
#define IDPF_TXD_FLEX_FLOW_DTYPE_M	GENMASK(4, 0)
#define IDPF_TXD_FLEX_FLOW_CMD_EOP	BIT(5)
#define IDPF_TXD_FLEX_FLOW_CMD_CS_EN	BIT(6)
#define IDPF_TXD_FLEX_FLOW_CMD_RE	BIT(7)

		/* [23:23] Horizon Overflow bit, [22:0] timestamp */
		u8 ts[3];
#define IDPF_TXD_FLOW_SCH_HORIZON_OVERFLOW_M	BIT(7)

		__le16 compl_tag;
		__le16 rxr_bufsize;
#define IDPF_TXD_FLEX_FLOW_RXR		BIT(14)
#define IDPF_TXD_FLEX_FLOW_BUFSIZE_M	GENMASK(13, 0)
	} qw1;
};

/* Common cmd fields for all flex context descriptors
 * Note: these defines already account for the 5 bit dtype in the cmd_dtype
 * field
 */
enum idpf_tx_flex_ctx_desc_cmd_bits {
	IDPF_TX_FLEX_CTX_DESC_CMD_TSO			= BIT(5),
	IDPF_TX_FLEX_CTX_DESC_CMD_TSYN_EN		= BIT(6),
	IDPF_TX_FLEX_CTX_DESC_CMD_L2TAG2		= BIT(7),
	IDPF_TX_FLEX_CTX_DESC_CMD_SWTCH_UPLNK		= BIT(9),
	IDPF_TX_FLEX_CTX_DESC_CMD_SWTCH_LOCAL		= BIT(10),
	IDPF_TX_FLEX_CTX_DESC_CMD_SWTCH_TARGETVSI	= GENMASK(10, 9),
};

/* Standard flex descriptor TSO context quad word */
struct idpf_flex_tx_tso_ctx_qw {
	__le32 flex_tlen;
#define IDPF_TXD_FLEX_CTX_TLEN_M	GENMASK(17, 0)
#define IDPF_TXD_FLEX_TSO_CTX_FLEX_S	24
	__le16 mss_rt;
#define IDPF_TXD_FLEX_CTX_MSS_RT_M	GENMASK(13, 0)
	u8 hdr_len;
	u8 flex;
};

struct idpf_flex_tx_ctx_desc {
	/* DTYPE = IDPF_TX_DESC_DTYPE_FLEX_TSO_CTX (0x05) */
	struct {
		struct idpf_flex_tx_tso_ctx_qw qw0;
		struct {
			__le16 cmd_dtype;
			u8 flex[6];
		} qw1;
	} tso;
};
#endif /* _IDPF_LAN_TXRX_H_ */
