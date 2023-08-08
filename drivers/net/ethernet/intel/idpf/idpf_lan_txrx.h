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

#endif /* _IDPF_LAN_TXRX_H_ */
