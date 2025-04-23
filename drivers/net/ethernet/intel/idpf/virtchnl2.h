/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023 Intel Corporation */

#ifndef _VIRTCHNL2_H_
#define _VIRTCHNL2_H_

#include <linux/if_ether.h>

/* All opcodes associated with virtchnl2 are prefixed with virtchnl2 or
 * VIRTCHNL2. Any future opcodes, offloads/capabilities, structures,
 * and defines must be prefixed with virtchnl2 or VIRTCHNL2 to avoid confusion.
 *
 * PF/VF uses the virtchnl2 interface defined in this header file to communicate
 * with device Control Plane (CP). Driver and the CP may run on different
 * platforms with different endianness. To avoid byte order discrepancies,
 * all the structures in this header follow little-endian format.
 *
 * This is an interface definition file where existing enums and their values
 * must remain unchanged over time, so we specify explicit values for all enums.
 */

/* This macro is used to generate compilation errors if a structure
 * is not exactly the correct length.
 */
#define VIRTCHNL2_CHECK_STRUCT_LEN(n, X)	\
	static_assert((n) == sizeof(struct X))

/* New major set of opcodes introduced and so leaving room for
 * old misc opcodes to be added in future. Also these opcodes may only
 * be used if both the PF and VF have successfully negotiated the
 * VIRTCHNL version as 2.0 during VIRTCHNL2_OP_VERSION exchange.
 */
enum virtchnl2_op {
	VIRTCHNL2_OP_UNKNOWN			= 0,
	VIRTCHNL2_OP_VERSION			= 1,
	VIRTCHNL2_OP_GET_CAPS			= 500,
	VIRTCHNL2_OP_CREATE_VPORT		= 501,
	VIRTCHNL2_OP_DESTROY_VPORT		= 502,
	VIRTCHNL2_OP_ENABLE_VPORT		= 503,
	VIRTCHNL2_OP_DISABLE_VPORT		= 504,
	VIRTCHNL2_OP_CONFIG_TX_QUEUES		= 505,
	VIRTCHNL2_OP_CONFIG_RX_QUEUES		= 506,
	VIRTCHNL2_OP_ENABLE_QUEUES		= 507,
	VIRTCHNL2_OP_DISABLE_QUEUES		= 508,
	VIRTCHNL2_OP_ADD_QUEUES			= 509,
	VIRTCHNL2_OP_DEL_QUEUES			= 510,
	VIRTCHNL2_OP_MAP_QUEUE_VECTOR		= 511,
	VIRTCHNL2_OP_UNMAP_QUEUE_VECTOR		= 512,
	VIRTCHNL2_OP_GET_RSS_KEY		= 513,
	VIRTCHNL2_OP_SET_RSS_KEY		= 514,
	VIRTCHNL2_OP_GET_RSS_LUT		= 515,
	VIRTCHNL2_OP_SET_RSS_LUT		= 516,
	VIRTCHNL2_OP_GET_RSS_HASH		= 517,
	VIRTCHNL2_OP_SET_RSS_HASH		= 518,
	VIRTCHNL2_OP_SET_SRIOV_VFS		= 519,
	VIRTCHNL2_OP_ALLOC_VECTORS		= 520,
	VIRTCHNL2_OP_DEALLOC_VECTORS		= 521,
	VIRTCHNL2_OP_EVENT			= 522,
	VIRTCHNL2_OP_GET_STATS			= 523,
	VIRTCHNL2_OP_RESET_VF			= 524,
	VIRTCHNL2_OP_GET_EDT_CAPS		= 525,
	VIRTCHNL2_OP_GET_PTYPE_INFO		= 526,
	/* Opcode 527 and 528 are reserved for VIRTCHNL2_OP_GET_PTYPE_ID and
	 * VIRTCHNL2_OP_GET_PTYPE_INFO_RAW.
	 */
	VIRTCHNL2_OP_RDMA			= 529,
	/* Opcodes 530 through 533 are reserved. */
	VIRTCHNL2_OP_LOOPBACK			= 534,
	VIRTCHNL2_OP_ADD_MAC_ADDR		= 535,
	VIRTCHNL2_OP_DEL_MAC_ADDR		= 536,
	VIRTCHNL2_OP_CONFIG_PROMISCUOUS_MODE	= 537,

	/* TimeSync opcodes */
	VIRTCHNL2_OP_PTP_GET_CAPS			= 541,
	VIRTCHNL2_OP_PTP_GET_VPORT_TX_TSTAMP		= 542,
	VIRTCHNL2_OP_PTP_GET_DEV_CLK_TIME		= 543,
	VIRTCHNL2_OP_PTP_GET_CROSS_TIME			= 544,
	VIRTCHNL2_OP_PTP_SET_DEV_CLK_TIME		= 545,
	VIRTCHNL2_OP_PTP_ADJ_DEV_CLK_FINE		= 546,
	VIRTCHNL2_OP_PTP_ADJ_DEV_CLK_TIME		= 547,
	VIRTCHNL2_OP_PTP_GET_VPORT_TX_TSTAMP_CAPS	= 548,
	VIRTCHNL2_OP_GET_LAN_MEMORY_REGIONS		= 549,
	/* Opcode 550 is reserved */
	VIRTCHNL2_OP_ADD_FLOW_RULE			= 551,
	VIRTCHNL2_OP_GET_FLOW_RULE			= 552,
	VIRTCHNL2_OP_DEL_FLOW_RULE			= 553,
};

/**
 * enum virtchnl2_vport_type - Type of virtual port.
 * @VIRTCHNL2_VPORT_TYPE_DEFAULT: Default virtual port type.
 */
enum virtchnl2_vport_type {
	VIRTCHNL2_VPORT_TYPE_DEFAULT		= 0,
};

/**
 * enum virtchnl2_queue_model - Type of queue model.
 * @VIRTCHNL2_QUEUE_MODEL_SINGLE: Single queue model.
 * @VIRTCHNL2_QUEUE_MODEL_SPLIT: Split queue model.
 *
 * In the single queue model, the same transmit descriptor queue is used by
 * software to post descriptors to hardware and by hardware to post completed
 * descriptors to software.
 * Likewise, the same receive descriptor queue is used by hardware to post
 * completions to software and by software to post buffers to hardware.
 *
 * In the split queue model, hardware uses transmit completion queues to post
 * descriptor/buffer completions to software, while software uses transmit
 * descriptor queues to post descriptors to hardware.
 * Likewise, hardware posts descriptor completions to the receive descriptor
 * queue, while software uses receive buffer queues to post buffers to hardware.
 */
enum virtchnl2_queue_model {
	VIRTCHNL2_QUEUE_MODEL_SINGLE		= 0,
	VIRTCHNL2_QUEUE_MODEL_SPLIT		= 1,
};

/* Checksum offload capability flags */
enum virtchnl2_cap_txrx_csum {
	VIRTCHNL2_CAP_TX_CSUM_L3_IPV4		= BIT(0),
	VIRTCHNL2_CAP_TX_CSUM_L4_IPV4_TCP	= BIT(1),
	VIRTCHNL2_CAP_TX_CSUM_L4_IPV4_UDP	= BIT(2),
	VIRTCHNL2_CAP_TX_CSUM_L4_IPV4_SCTP	= BIT(3),
	VIRTCHNL2_CAP_TX_CSUM_L4_IPV6_TCP	= BIT(4),
	VIRTCHNL2_CAP_TX_CSUM_L4_IPV6_UDP	= BIT(5),
	VIRTCHNL2_CAP_TX_CSUM_L4_IPV6_SCTP	= BIT(6),
	VIRTCHNL2_CAP_TX_CSUM_GENERIC		= BIT(7),
	VIRTCHNL2_CAP_RX_CSUM_L3_IPV4		= BIT(8),
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_TCP	= BIT(9),
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_UDP	= BIT(10),
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_SCTP	= BIT(11),
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_TCP	= BIT(12),
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_UDP	= BIT(13),
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_SCTP	= BIT(14),
	VIRTCHNL2_CAP_RX_CSUM_GENERIC		= BIT(15),
	VIRTCHNL2_CAP_TX_CSUM_L3_SINGLE_TUNNEL	= BIT(16),
	VIRTCHNL2_CAP_TX_CSUM_L3_DOUBLE_TUNNEL	= BIT(17),
	VIRTCHNL2_CAP_RX_CSUM_L3_SINGLE_TUNNEL	= BIT(18),
	VIRTCHNL2_CAP_RX_CSUM_L3_DOUBLE_TUNNEL	= BIT(19),
	VIRTCHNL2_CAP_TX_CSUM_L4_SINGLE_TUNNEL	= BIT(20),
	VIRTCHNL2_CAP_TX_CSUM_L4_DOUBLE_TUNNEL	= BIT(21),
	VIRTCHNL2_CAP_RX_CSUM_L4_SINGLE_TUNNEL	= BIT(22),
	VIRTCHNL2_CAP_RX_CSUM_L4_DOUBLE_TUNNEL	= BIT(23),
};

/* Segmentation offload capability flags */
enum virtchnl2_cap_seg {
	VIRTCHNL2_CAP_SEG_IPV4_TCP		= BIT(0),
	VIRTCHNL2_CAP_SEG_IPV4_UDP		= BIT(1),
	VIRTCHNL2_CAP_SEG_IPV4_SCTP		= BIT(2),
	VIRTCHNL2_CAP_SEG_IPV6_TCP		= BIT(3),
	VIRTCHNL2_CAP_SEG_IPV6_UDP		= BIT(4),
	VIRTCHNL2_CAP_SEG_IPV6_SCTP		= BIT(5),
	VIRTCHNL2_CAP_SEG_GENERIC		= BIT(6),
	VIRTCHNL2_CAP_SEG_TX_SINGLE_TUNNEL	= BIT(7),
	VIRTCHNL2_CAP_SEG_TX_DOUBLE_TUNNEL	= BIT(8),
};

/* Receive Side Scaling and Flow Steering Flow type capability flags */
enum virtchnl2_flow_types {
	VIRTCHNL2_FLOW_IPV4_TCP		= BIT(0),
	VIRTCHNL2_FLOW_IPV4_UDP		= BIT(1),
	VIRTCHNL2_FLOW_IPV4_SCTP	= BIT(2),
	VIRTCHNL2_FLOW_IPV4_OTHER	= BIT(3),
	VIRTCHNL2_FLOW_IPV6_TCP		= BIT(4),
	VIRTCHNL2_FLOW_IPV6_UDP		= BIT(5),
	VIRTCHNL2_FLOW_IPV6_SCTP	= BIT(6),
	VIRTCHNL2_FLOW_IPV6_OTHER	= BIT(7),
	VIRTCHNL2_FLOW_IPV4_AH		= BIT(8),
	VIRTCHNL2_FLOW_IPV4_ESP		= BIT(9),
	VIRTCHNL2_FLOW_IPV4_AH_ESP	= BIT(10),
	VIRTCHNL2_FLOW_IPV6_AH		= BIT(11),
	VIRTCHNL2_FLOW_IPV6_ESP		= BIT(12),
	VIRTCHNL2_FLOW_IPV6_AH_ESP	= BIT(13),
};

/* Header split capability flags */
enum virtchnl2_cap_rx_hsplit_at {
	/* for prepended metadata  */
	VIRTCHNL2_CAP_RX_HSPLIT_AT_L2		= BIT(0),
	/* all VLANs go into header buffer */
	VIRTCHNL2_CAP_RX_HSPLIT_AT_L3		= BIT(1),
	VIRTCHNL2_CAP_RX_HSPLIT_AT_L4V4		= BIT(2),
	VIRTCHNL2_CAP_RX_HSPLIT_AT_L4V6		= BIT(3),
};

/* Receive Side Coalescing offload capability flags */
enum virtchnl2_cap_rsc {
	VIRTCHNL2_CAP_RSC_IPV4_TCP		= BIT(0),
	VIRTCHNL2_CAP_RSC_IPV4_SCTP		= BIT(1),
	VIRTCHNL2_CAP_RSC_IPV6_TCP		= BIT(2),
	VIRTCHNL2_CAP_RSC_IPV6_SCTP		= BIT(3),
};

/* Other capability flags */
enum virtchnl2_cap_other {
	VIRTCHNL2_CAP_RDMA			= BIT_ULL(0),
	VIRTCHNL2_CAP_SRIOV			= BIT_ULL(1),
	VIRTCHNL2_CAP_MACFILTER			= BIT_ULL(2),
	/* Other capability 3 is available
	 * Queue based scheduling using split queue model
	 */
	VIRTCHNL2_CAP_SPLITQ_QSCHED		= BIT_ULL(4),
	VIRTCHNL2_CAP_CRC			= BIT_ULL(5),
	VIRTCHNL2_CAP_ADQ			= BIT_ULL(6),
	VIRTCHNL2_CAP_WB_ON_ITR			= BIT_ULL(7),
	VIRTCHNL2_CAP_PROMISC			= BIT_ULL(8),
	VIRTCHNL2_CAP_LINK_SPEED		= BIT_ULL(9),
	VIRTCHNL2_CAP_INLINE_IPSEC		= BIT_ULL(10),
	VIRTCHNL2_CAP_LARGE_NUM_QUEUES		= BIT_ULL(11),
	VIRTCHNL2_CAP_VLAN			= BIT_ULL(12),
	VIRTCHNL2_CAP_PTP			= BIT_ULL(13),
	/* EDT: Earliest Departure Time capability used for Timing Wheel */
	VIRTCHNL2_CAP_EDT			= BIT_ULL(14),
	VIRTCHNL2_CAP_ADV_RSS			= BIT_ULL(15),
	/* Other capability 16 is available */
	VIRTCHNL2_CAP_RX_FLEX_DESC		= BIT_ULL(17),
	VIRTCHNL2_CAP_PTYPE			= BIT_ULL(18),
	VIRTCHNL2_CAP_LOOPBACK			= BIT_ULL(19),
	/* Other capability 20 is reserved */
	VIRTCHNL2_CAP_FLOW_STEER		= BIT_ULL(21),
	VIRTCHNL2_CAP_LAN_MEMORY_REGIONS	= BIT_ULL(22),

	/* this must be the last capability */
	VIRTCHNL2_CAP_OEM			= BIT_ULL(63),
};

/**
 * enum virtchnl2_action_types - Available actions for sideband flow steering
 * @VIRTCHNL2_ACTION_DROP: Drop the packet
 * @VIRTCHNL2_ACTION_PASSTHRU: Forward the packet to the next classifier/stage
 * @VIRTCHNL2_ACTION_QUEUE: Forward the packet to a receive queue
 * @VIRTCHNL2_ACTION_Q_GROUP: Forward the packet to a receive queue group
 * @VIRTCHNL2_ACTION_MARK: Mark the packet with specific marker value
 * @VIRTCHNL2_ACTION_COUNT: Increment the corresponding counter
 */

enum virtchnl2_action_types {
	VIRTCHNL2_ACTION_DROP		= BIT(0),
	VIRTCHNL2_ACTION_PASSTHRU	= BIT(1),
	VIRTCHNL2_ACTION_QUEUE		= BIT(2),
	VIRTCHNL2_ACTION_Q_GROUP	= BIT(3),
	VIRTCHNL2_ACTION_MARK		= BIT(4),
	VIRTCHNL2_ACTION_COUNT		= BIT(5),
};

/* underlying device type */
enum virtchl2_device_type {
	VIRTCHNL2_MEV_DEVICE			= 0,
};

/**
 * enum virtchnl2_txq_sched_mode - Transmit Queue Scheduling Modes.
 * @VIRTCHNL2_TXQ_SCHED_MODE_QUEUE: Queue mode is the legacy mode i.e. inorder
 *				    completions where descriptors and buffers
 *				    are completed at the same time.
 * @VIRTCHNL2_TXQ_SCHED_MODE_FLOW: Flow scheduling mode allows for out of order
 *				   packet processing where descriptors are
 *				   cleaned in order, but buffers can be
 *				   completed out of order.
 */
enum virtchnl2_txq_sched_mode {
	VIRTCHNL2_TXQ_SCHED_MODE_QUEUE		= 0,
	VIRTCHNL2_TXQ_SCHED_MODE_FLOW		= 1,
};

/**
 * enum virtchnl2_rxq_flags - Receive Queue Feature flags.
 * @VIRTCHNL2_RXQ_RSC: Rx queue RSC flag.
 * @VIRTCHNL2_RXQ_HDR_SPLIT: Rx queue header split flag.
 * @VIRTCHNL2_RXQ_IMMEDIATE_WRITE_BACK: When set, packet descriptors are flushed
 *					by hardware immediately after processing
 *					each packet.
 * @VIRTCHNL2_RX_DESC_SIZE_16BYTE: Rx queue 16 byte descriptor size.
 * @VIRTCHNL2_RX_DESC_SIZE_32BYTE: Rx queue 32 byte descriptor size.
 */
enum virtchnl2_rxq_flags {
	VIRTCHNL2_RXQ_RSC			= BIT(0),
	VIRTCHNL2_RXQ_HDR_SPLIT			= BIT(1),
	VIRTCHNL2_RXQ_IMMEDIATE_WRITE_BACK	= BIT(2),
	VIRTCHNL2_RX_DESC_SIZE_16BYTE		= BIT(3),
	VIRTCHNL2_RX_DESC_SIZE_32BYTE		= BIT(4),
};

/* Type of RSS algorithm */
enum virtchnl2_rss_alg {
	VIRTCHNL2_RSS_ALG_TOEPLITZ_ASYMMETRIC	= 0,
	VIRTCHNL2_RSS_ALG_R_ASYMMETRIC		= 1,
	VIRTCHNL2_RSS_ALG_TOEPLITZ_SYMMETRIC	= 2,
	VIRTCHNL2_RSS_ALG_XOR_SYMMETRIC		= 3,
};

/* Type of event */
enum virtchnl2_event_codes {
	VIRTCHNL2_EVENT_UNKNOWN			= 0,
	VIRTCHNL2_EVENT_LINK_CHANGE		= 1,
	/* Event type 2, 3 are reserved */
};

/* Transmit and Receive queue types are valid in legacy as well as split queue
 * models. With Split Queue model, 2 additional types are introduced -
 * TX_COMPLETION and RX_BUFFER. In split queue model, receive  corresponds to
 * the queue where hardware posts completions.
 */
enum virtchnl2_queue_type {
	VIRTCHNL2_QUEUE_TYPE_TX			= 0,
	VIRTCHNL2_QUEUE_TYPE_RX			= 1,
	VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION	= 2,
	VIRTCHNL2_QUEUE_TYPE_RX_BUFFER		= 3,
	VIRTCHNL2_QUEUE_TYPE_CONFIG_TX		= 4,
	VIRTCHNL2_QUEUE_TYPE_CONFIG_RX		= 5,
	/* Queue types 6, 7, 8, 9 are reserved */
	VIRTCHNL2_QUEUE_TYPE_MBX_TX		= 10,
	VIRTCHNL2_QUEUE_TYPE_MBX_RX		= 11,
};

/* Interrupt throttling rate index */
enum virtchnl2_itr_idx {
	VIRTCHNL2_ITR_IDX_0			= 0,
	VIRTCHNL2_ITR_IDX_1			= 1,
};

/**
 * enum virtchnl2_mac_addr_type - MAC address types.
 * @VIRTCHNL2_MAC_ADDR_PRIMARY: PF/VF driver should set this type for the
 *				primary/device unicast MAC address filter for
 *				VIRTCHNL2_OP_ADD_MAC_ADDR and
 *				VIRTCHNL2_OP_DEL_MAC_ADDR. This allows for the
 *				underlying control plane function to accurately
 *				track the MAC address and for VM/function reset.
 *
 * @VIRTCHNL2_MAC_ADDR_EXTRA: PF/VF driver should set this type for any extra
 *			      unicast and/or multicast filters that are being
 *			      added/deleted via VIRTCHNL2_OP_ADD_MAC_ADDR or
 *			      VIRTCHNL2_OP_DEL_MAC_ADDR.
 */
enum virtchnl2_mac_addr_type {
	VIRTCHNL2_MAC_ADDR_PRIMARY		= 1,
	VIRTCHNL2_MAC_ADDR_EXTRA		= 2,
};

/* Flags used for promiscuous mode */
enum virtchnl2_promisc_flags {
	VIRTCHNL2_UNICAST_PROMISC		= BIT(0),
	VIRTCHNL2_MULTICAST_PROMISC		= BIT(1),
};

/* Protocol header type within a packet segment. A segment consists of one or
 * more protocol headers that make up a logical group of protocol headers. Each
 * logical group of protocol headers encapsulates or is encapsulated using/by
 * tunneling or encapsulation protocols for network virtualization.
 */
enum virtchnl2_proto_hdr_type {
	/* VIRTCHNL2_PROTO_HDR_ANY is a mandatory protocol id */
	VIRTCHNL2_PROTO_HDR_ANY			= 0,
	VIRTCHNL2_PROTO_HDR_PRE_MAC		= 1,
	/* VIRTCHNL2_PROTO_HDR_MAC is a mandatory protocol id */
	VIRTCHNL2_PROTO_HDR_MAC			= 2,
	VIRTCHNL2_PROTO_HDR_POST_MAC		= 3,
	VIRTCHNL2_PROTO_HDR_ETHERTYPE		= 4,
	VIRTCHNL2_PROTO_HDR_VLAN		= 5,
	VIRTCHNL2_PROTO_HDR_SVLAN		= 6,
	VIRTCHNL2_PROTO_HDR_CVLAN		= 7,
	VIRTCHNL2_PROTO_HDR_MPLS		= 8,
	VIRTCHNL2_PROTO_HDR_UMPLS		= 9,
	VIRTCHNL2_PROTO_HDR_MMPLS		= 10,
	VIRTCHNL2_PROTO_HDR_PTP			= 11,
	VIRTCHNL2_PROTO_HDR_CTRL		= 12,
	VIRTCHNL2_PROTO_HDR_LLDP		= 13,
	VIRTCHNL2_PROTO_HDR_ARP			= 14,
	VIRTCHNL2_PROTO_HDR_ECP			= 15,
	VIRTCHNL2_PROTO_HDR_EAPOL		= 16,
	VIRTCHNL2_PROTO_HDR_PPPOD		= 17,
	VIRTCHNL2_PROTO_HDR_PPPOE		= 18,
	/* VIRTCHNL2_PROTO_HDR_IPV4 is a mandatory protocol id */
	VIRTCHNL2_PROTO_HDR_IPV4		= 19,
	/* IPv4 and IPv6 Fragment header types are only associated to
	 * VIRTCHNL2_PROTO_HDR_IPV4 and VIRTCHNL2_PROTO_HDR_IPV6 respectively,
	 * cannot be used independently.
	 */
	/* VIRTCHNL2_PROTO_HDR_IPV4_FRAG is a mandatory protocol id */
	VIRTCHNL2_PROTO_HDR_IPV4_FRAG		= 20,
	/* VIRTCHNL2_PROTO_HDR_IPV6 is a mandatory protocol id */
	VIRTCHNL2_PROTO_HDR_IPV6		= 21,
	/* VIRTCHNL2_PROTO_HDR_IPV6_FRAG is a mandatory protocol id */
	VIRTCHNL2_PROTO_HDR_IPV6_FRAG		= 22,
	VIRTCHNL2_PROTO_HDR_IPV6_EH		= 23,
	/* VIRTCHNL2_PROTO_HDR_UDP is a mandatory protocol id */
	VIRTCHNL2_PROTO_HDR_UDP			= 24,
	/* VIRTCHNL2_PROTO_HDR_TCP is a mandatory protocol id */
	VIRTCHNL2_PROTO_HDR_TCP			= 25,
	/* VIRTCHNL2_PROTO_HDR_SCTP is a mandatory protocol id */
	VIRTCHNL2_PROTO_HDR_SCTP		= 26,
	/* VIRTCHNL2_PROTO_HDR_ICMP is a mandatory protocol id */
	VIRTCHNL2_PROTO_HDR_ICMP		= 27,
	/* VIRTCHNL2_PROTO_HDR_ICMPV6 is a mandatory protocol id */
	VIRTCHNL2_PROTO_HDR_ICMPV6		= 28,
	VIRTCHNL2_PROTO_HDR_IGMP		= 29,
	VIRTCHNL2_PROTO_HDR_AH			= 30,
	VIRTCHNL2_PROTO_HDR_ESP			= 31,
	VIRTCHNL2_PROTO_HDR_IKE			= 32,
	VIRTCHNL2_PROTO_HDR_NATT_KEEP		= 33,
	/* VIRTCHNL2_PROTO_HDR_PAY is a mandatory protocol id */
	VIRTCHNL2_PROTO_HDR_PAY			= 34,
	VIRTCHNL2_PROTO_HDR_L2TPV2		= 35,
	VIRTCHNL2_PROTO_HDR_L2TPV2_CONTROL	= 36,
	VIRTCHNL2_PROTO_HDR_L2TPV3		= 37,
	VIRTCHNL2_PROTO_HDR_GTP			= 38,
	VIRTCHNL2_PROTO_HDR_GTP_EH		= 39,
	VIRTCHNL2_PROTO_HDR_GTPCV2		= 40,
	VIRTCHNL2_PROTO_HDR_GTPC_TEID		= 41,
	VIRTCHNL2_PROTO_HDR_GTPU		= 42,
	VIRTCHNL2_PROTO_HDR_GTPU_UL		= 43,
	VIRTCHNL2_PROTO_HDR_GTPU_DL		= 44,
	VIRTCHNL2_PROTO_HDR_ECPRI		= 45,
	VIRTCHNL2_PROTO_HDR_VRRP		= 46,
	VIRTCHNL2_PROTO_HDR_OSPF		= 47,
	/* VIRTCHNL2_PROTO_HDR_TUN is a mandatory protocol id */
	VIRTCHNL2_PROTO_HDR_TUN			= 48,
	VIRTCHNL2_PROTO_HDR_GRE			= 49,
	VIRTCHNL2_PROTO_HDR_NVGRE		= 50,
	VIRTCHNL2_PROTO_HDR_VXLAN		= 51,
	VIRTCHNL2_PROTO_HDR_VXLAN_GPE		= 52,
	VIRTCHNL2_PROTO_HDR_GENEVE		= 53,
	VIRTCHNL2_PROTO_HDR_NSH			= 54,
	VIRTCHNL2_PROTO_HDR_QUIC		= 55,
	VIRTCHNL2_PROTO_HDR_PFCP		= 56,
	VIRTCHNL2_PROTO_HDR_PFCP_NODE		= 57,
	VIRTCHNL2_PROTO_HDR_PFCP_SESSION	= 58,
	VIRTCHNL2_PROTO_HDR_RTP			= 59,
	VIRTCHNL2_PROTO_HDR_ROCE		= 60,
	VIRTCHNL2_PROTO_HDR_ROCEV1		= 61,
	VIRTCHNL2_PROTO_HDR_ROCEV2		= 62,
	/* Protocol ids up to 32767 are reserved.
	 * 32768 - 65534 are used for user defined protocol ids.
	 * VIRTCHNL2_PROTO_HDR_NO_PROTO is a mandatory protocol id.
	 */
	VIRTCHNL2_PROTO_HDR_NO_PROTO		= 65535,
};

enum virtchl2_version {
	VIRTCHNL2_VERSION_MINOR_0		= 0,
	VIRTCHNL2_VERSION_MAJOR_2		= 2,
};

/**
 * struct virtchnl2_edt_caps - Get EDT granularity and time horizon.
 * @tstamp_granularity_ns: Timestamp granularity in nanoseconds.
 * @time_horizon_ns: Total time window in nanoseconds.
 *
 * Associated with VIRTCHNL2_OP_GET_EDT_CAPS.
 */
struct virtchnl2_edt_caps {
	__le64 tstamp_granularity_ns;
	__le64 time_horizon_ns;
};
VIRTCHNL2_CHECK_STRUCT_LEN(16, virtchnl2_edt_caps);

/**
 * struct virtchnl2_version_info - Version information.
 * @major: Major version.
 * @minor: Minor version.
 *
 * PF/VF posts its version number to the CP. CP responds with its version number
 * in the same format, along with a return code.
 * If there is a major version mismatch, then the PF/VF cannot operate.
 * If there is a minor version mismatch, then the PF/VF can operate but should
 * add a warning to the system log.
 *
 * This version opcode MUST always be specified as == 1, regardless of other
 * changes in the API. The CP must always respond to this message without
 * error regardless of version mismatch.
 *
 * Associated with VIRTCHNL2_OP_VERSION.
 */
struct virtchnl2_version_info {
	__le32 major;
	__le32 minor;
};
VIRTCHNL2_CHECK_STRUCT_LEN(8, virtchnl2_version_info);

/**
 * struct virtchnl2_get_capabilities - Capabilities info.
 * @csum_caps: See enum virtchnl2_cap_txrx_csum.
 * @seg_caps: See enum virtchnl2_cap_seg.
 * @hsplit_caps: See enum virtchnl2_cap_rx_hsplit_at.
 * @rsc_caps: See enum virtchnl2_cap_rsc.
 * @rss_caps: See enum virtchnl2_flow_types.
 * @other_caps: See enum virtchnl2_cap_other.
 * @mailbox_dyn_ctl: DYN_CTL register offset and vector id for mailbox
 *		     provided by CP.
 * @mailbox_vector_id: Mailbox vector id.
 * @num_allocated_vectors: Maximum number of allocated vectors for the device.
 * @max_rx_q: Maximum number of supported Rx queues.
 * @max_tx_q: Maximum number of supported Tx queues.
 * @max_rx_bufq: Maximum number of supported buffer queues.
 * @max_tx_complq: Maximum number of supported completion queues.
 * @max_sriov_vfs: The PF sends the maximum VFs it is requesting. The CP
 *		   responds with the maximum VFs granted.
 * @max_vports: Maximum number of vports that can be supported.
 * @default_num_vports: Default number of vports driver should allocate on load.
 * @max_tx_hdr_size: Max header length hardware can parse/checksum, in bytes.
 * @max_sg_bufs_per_tx_pkt: Max number of scatter gather buffers that can be
 *			    sent per transmit packet without needing to be
 *			    linearized.
 * @pad: Padding.
 * @reserved: Reserved.
 * @device_type: See enum virtchl2_device_type.
 * @min_sso_packet_len: Min packet length supported by device for single
 *			segment offload.
 * @max_hdr_buf_per_lso: Max number of header buffers that can be used for
 *			 an LSO.
 * @num_rdma_allocated_vectors: Maximum number of allocated RDMA vectors for
 *				the device.
 * @pad1: Padding for future extensions.
 *
 * Dataplane driver sends this message to CP to negotiate capabilities and
 * provides a virtchnl2_get_capabilities structure with its desired
 * capabilities, max_sriov_vfs and num_allocated_vectors.
 * CP responds with a virtchnl2_get_capabilities structure updated
 * with allowed capabilities and the other fields as below.
 * If PF sets max_sriov_vfs as 0, CP will respond with max number of VFs
 * that can be created by this PF. For any other value 'n', CP responds
 * with max_sriov_vfs set to min(n, x) where x is the max number of VFs
 * allowed by CP's policy. max_sriov_vfs is not applicable for VFs.
 * If dataplane driver sets num_allocated_vectors as 0, CP will respond with 1
 * which is default vector associated with the default mailbox. For any other
 * value 'n', CP responds with a value <= n based on the CP's policy of
 * max number of vectors for a PF.
 * CP will respond with the vector ID of mailbox allocated to the PF in
 * mailbox_vector_id and the number of itr index registers in itr_idx_map.
 * It also responds with default number of vports that the dataplane driver
 * should comeup with in default_num_vports and maximum number of vports that
 * can be supported in max_vports.
 *
 * Associated with VIRTCHNL2_OP_GET_CAPS.
 */
struct virtchnl2_get_capabilities {
	__le32 csum_caps;
	__le32 seg_caps;
	__le32 hsplit_caps;
	__le32 rsc_caps;
	__le64 rss_caps;
	__le64 other_caps;
	__le32 mailbox_dyn_ctl;
	__le16 mailbox_vector_id;
	__le16 num_allocated_vectors;
	__le16 max_rx_q;
	__le16 max_tx_q;
	__le16 max_rx_bufq;
	__le16 max_tx_complq;
	__le16 max_sriov_vfs;
	__le16 max_vports;
	__le16 default_num_vports;
	__le16 max_tx_hdr_size;
	u8 max_sg_bufs_per_tx_pkt;
	u8 pad[3];
	u8 reserved[4];
	__le32 device_type;
	u8 min_sso_packet_len;
	u8 max_hdr_buf_per_lso;
	__le16 num_rdma_allocated_vectors;
	u8 pad1[8];
};
VIRTCHNL2_CHECK_STRUCT_LEN(80, virtchnl2_get_capabilities);

/**
 * struct virtchnl2_queue_reg_chunk - Single queue chunk.
 * @type: See enum virtchnl2_queue_type.
 * @start_queue_id: Start Queue ID.
 * @num_queues: Number of queues in the chunk.
 * @pad: Padding.
 * @qtail_reg_start: Queue tail register offset.
 * @qtail_reg_spacing: Queue tail register spacing.
 * @pad1: Padding for future extensions.
 */
struct virtchnl2_queue_reg_chunk {
	__le32 type;
	__le32 start_queue_id;
	__le32 num_queues;
	__le32 pad;
	__le64 qtail_reg_start;
	__le32 qtail_reg_spacing;
	u8 pad1[4];
};
VIRTCHNL2_CHECK_STRUCT_LEN(32, virtchnl2_queue_reg_chunk);

/**
 * struct virtchnl2_queue_reg_chunks - Specify several chunks of contiguous
 *				       queues.
 * @num_chunks: Number of chunks.
 * @pad: Padding.
 * @chunks: Chunks of queue info.
 */
struct virtchnl2_queue_reg_chunks {
	__le16 num_chunks;
	u8 pad[6];
	struct virtchnl2_queue_reg_chunk chunks[] __counted_by_le(num_chunks);
};
VIRTCHNL2_CHECK_STRUCT_LEN(8, virtchnl2_queue_reg_chunks);

/**
 * enum virtchnl2_vport_flags - Vport flags that indicate vport capabilities.
 * @VIRTCHNL2_VPORT_UPLINK_PORT: Representatives of underlying physical ports
 * @VIRTCHNL2_VPORT_INLINE_FLOW_STEER: Inline flow steering enabled
 * @VIRTCHNL2_VPORT_INLINE_FLOW_STEER_RXQ: Inline flow steering enabled
 *  with explicit Rx queue action
 * @VIRTCHNL2_VPORT_SIDEBAND_FLOW_STEER: Sideband flow steering enabled
 * @VIRTCHNL2_VPORT_ENABLE_RDMA: RDMA is enabled for this vport
 */
enum virtchnl2_vport_flags {
	VIRTCHNL2_VPORT_UPLINK_PORT		= BIT(0),
	VIRTCHNL2_VPORT_INLINE_FLOW_STEER	= BIT(1),
	VIRTCHNL2_VPORT_INLINE_FLOW_STEER_RXQ	= BIT(2),
	VIRTCHNL2_VPORT_SIDEBAND_FLOW_STEER	= BIT(3),
	VIRTCHNL2_VPORT_ENABLE_RDMA             = BIT(4),
};

/**
 * struct virtchnl2_create_vport - Create vport config info.
 * @vport_type: See enum virtchnl2_vport_type.
 * @txq_model: See virtchnl2_queue_model.
 * @rxq_model: See virtchnl2_queue_model.
 * @num_tx_q: Number of Tx queues.
 * @num_tx_complq: Valid only if txq_model is split queue.
 * @num_rx_q: Number of Rx queues.
 * @num_rx_bufq: Valid only if rxq_model is split queue.
 * @default_rx_q: Relative receive queue index to be used as default.
 * @vport_index: Used to align PF and CP in case of default multiple vports,
 *		 it is filled by the PF and CP returns the same value, to
 *		 enable the driver to support multiple asynchronous parallel
 *		 CREATE_VPORT requests and associate a response to a specific
 *		 request.
 * @max_mtu: Max MTU. CP populates this field on response.
 * @vport_id: Vport id. CP populates this field on response.
 * @default_mac_addr: Default MAC address.
 * @vport_flags: See enum virtchnl2_vport_flags.
 * @rx_desc_ids: See VIRTCHNL2_RX_DESC_IDS definitions.
 * @tx_desc_ids: See VIRTCHNL2_TX_DESC_IDS definitions.
 * @pad1: Padding.
 * @inline_flow_caps: Bit mask of supported inline-flow-steering
 *  flow types (See enum virtchnl2_flow_types)
 * @sideband_flow_caps: Bit mask of supported sideband-flow-steering
 *  flow types (See enum virtchnl2_flow_types)
 * @sideband_flow_actions: Bit mask of supported action types
 *  for sideband flow steering (See enum virtchnl2_action_types)
 * @flow_steer_max_rules: Max rules allowed for inline and sideband
 *  flow steering combined
 * @rss_algorithm: RSS algorithm.
 * @rss_key_size: RSS key size.
 * @rss_lut_size: RSS LUT size.
 * @rx_split_pos: See enum virtchnl2_cap_rx_hsplit_at.
 * @pad2: Padding.
 * @chunks: Chunks of contiguous queues.
 *
 * PF sends this message to CP to create a vport by filling in required
 * fields of virtchnl2_create_vport structure.
 * CP responds with the updated virtchnl2_create_vport structure containing the
 * necessary fields followed by chunks which in turn will have an array of
 * num_chunks entries of virtchnl2_queue_chunk structures.
 *
 * Associated with VIRTCHNL2_OP_CREATE_VPORT.
 */
struct virtchnl2_create_vport {
	__le16 vport_type;
	__le16 txq_model;
	__le16 rxq_model;
	__le16 num_tx_q;
	__le16 num_tx_complq;
	__le16 num_rx_q;
	__le16 num_rx_bufq;
	__le16 default_rx_q;
	__le16 vport_index;
	/* CP populates the following fields on response */
	__le16 max_mtu;
	__le32 vport_id;
	u8 default_mac_addr[ETH_ALEN];
	__le16 vport_flags;
	__le64 rx_desc_ids;
	__le64 tx_desc_ids;
	u8 pad1[48];
	__le64 inline_flow_caps;
	__le64 sideband_flow_caps;
	__le32 sideband_flow_actions;
	__le32 flow_steer_max_rules;
	__le32 rss_algorithm;
	__le16 rss_key_size;
	__le16 rss_lut_size;
	__le32 rx_split_pos;
	u8 pad2[20];
	struct virtchnl2_queue_reg_chunks chunks;
};
VIRTCHNL2_CHECK_STRUCT_LEN(160, virtchnl2_create_vport);

/**
 * struct virtchnl2_vport - Vport ID info.
 * @vport_id: Vport id.
 * @pad: Padding for future extensions.
 *
 * PF sends this message to CP to destroy, enable or disable a vport by filling
 * in the vport_id in virtchnl2_vport structure.
 * CP responds with the status of the requested operation.
 *
 * Associated with VIRTCHNL2_OP_DESTROY_VPORT, VIRTCHNL2_OP_ENABLE_VPORT,
 * VIRTCHNL2_OP_DISABLE_VPORT.
 */
struct virtchnl2_vport {
	__le32 vport_id;
	u8 pad[4];
};
VIRTCHNL2_CHECK_STRUCT_LEN(8, virtchnl2_vport);

/**
 * struct virtchnl2_txq_info - Transmit queue config info
 * @dma_ring_addr: DMA address.
 * @type: See enum virtchnl2_queue_type.
 * @queue_id: Queue ID.
 * @relative_queue_id: Valid only if queue model is split and type is transmit
 *		       queue. Used in many to one mapping of transmit queues to
 *		       completion queue.
 * @model: See enum virtchnl2_queue_model.
 * @sched_mode: See enum virtchnl2_txq_sched_mode.
 * @qflags: TX queue feature flags.
 * @ring_len: Ring length.
 * @tx_compl_queue_id: Valid only if queue model is split and type is transmit
 *		       queue.
 * @peer_type: Valid only if queue type is VIRTCHNL2_QUEUE_TYPE_MAILBOX_TX
 * @peer_rx_queue_id: Valid only if queue type is CONFIG_TX and used to deliver
 *		      messages for the respective CONFIG_TX queue.
 * @pad: Padding.
 * @egress_pasid: Egress PASID info.
 * @egress_hdr_pasid: Egress HDR passid.
 * @egress_buf_pasid: Egress buf passid.
 * @pad1: Padding for future extensions.
 */
struct virtchnl2_txq_info {
	__le64 dma_ring_addr;
	__le32 type;
	__le32 queue_id;
	__le16 relative_queue_id;
	__le16 model;
	__le16 sched_mode;
	__le16 qflags;
	__le16 ring_len;
	__le16 tx_compl_queue_id;
	__le16 peer_type;
	__le16 peer_rx_queue_id;
	u8 pad[4];
	__le32 egress_pasid;
	__le32 egress_hdr_pasid;
	__le32 egress_buf_pasid;
	u8 pad1[8];
};
VIRTCHNL2_CHECK_STRUCT_LEN(56, virtchnl2_txq_info);

/**
 * struct virtchnl2_config_tx_queues - TX queue config.
 * @vport_id: Vport id.
 * @num_qinfo: Number of virtchnl2_txq_info structs.
 * @pad: Padding.
 * @qinfo: Tx queues config info.
 *
 * PF sends this message to set up parameters for one or more transmit queues.
 * This message contains an array of num_qinfo instances of virtchnl2_txq_info
 * structures. CP configures requested queues and returns a status code. If
 * num_qinfo specified is greater than the number of queues associated with the
 * vport, an error is returned and no queues are configured.
 *
 * Associated with VIRTCHNL2_OP_CONFIG_TX_QUEUES.
 */
struct virtchnl2_config_tx_queues {
	__le32 vport_id;
	__le16 num_qinfo;
	u8 pad[10];
	struct virtchnl2_txq_info qinfo[] __counted_by_le(num_qinfo);
};
VIRTCHNL2_CHECK_STRUCT_LEN(16, virtchnl2_config_tx_queues);

/**
 * struct virtchnl2_rxq_info - Receive queue config info.
 * @desc_ids: See VIRTCHNL2_RX_DESC_IDS definitions.
 * @dma_ring_addr: See VIRTCHNL2_RX_DESC_IDS definitions.
 * @type: See enum virtchnl2_queue_type.
 * @queue_id: Queue id.
 * @model: See enum virtchnl2_queue_model.
 * @hdr_buffer_size: Header buffer size.
 * @data_buffer_size: Data buffer size.
 * @max_pkt_size: Max packet size.
 * @ring_len: Ring length.
 * @buffer_notif_stride: Buffer notification stride in units of 32-descriptors.
 *			 This field must be a power of 2.
 * @pad: Padding.
 * @dma_head_wb_addr: Applicable only for receive buffer queues.
 * @qflags: Applicable only for receive completion queues.
 *	    See enum virtchnl2_rxq_flags.
 * @rx_buffer_low_watermark: Rx buffer low watermark.
 * @rx_bufq1_id: Buffer queue index of the first buffer queue associated with
 *		 the Rx queue. Valid only in split queue model.
 * @rx_bufq2_id: Buffer queue index of the second buffer queue associated with
 *		 the Rx queue. Valid only in split queue model.
 * @bufq2_ena: It indicates if there is a second buffer, rx_bufq2_id is valid
 *	       only if this field is set.
 * @pad1: Padding.
 * @ingress_pasid: Ingress PASID.
 * @ingress_hdr_pasid: Ingress PASID header.
 * @ingress_buf_pasid: Ingress PASID buffer.
 * @pad2: Padding for future extensions.
 */
struct virtchnl2_rxq_info {
	__le64 desc_ids;
	__le64 dma_ring_addr;
	__le32 type;
	__le32 queue_id;
	__le16 model;
	__le16 hdr_buffer_size;
	__le32 data_buffer_size;
	__le32 max_pkt_size;
	__le16 ring_len;
	u8 buffer_notif_stride;
	u8 pad;
	__le64 dma_head_wb_addr;
	__le16 qflags;
	__le16 rx_buffer_low_watermark;
	__le16 rx_bufq1_id;
	__le16 rx_bufq2_id;
	u8 bufq2_ena;
	u8 pad1[3];
	__le32 ingress_pasid;
	__le32 ingress_hdr_pasid;
	__le32 ingress_buf_pasid;
	u8 pad2[16];
};
VIRTCHNL2_CHECK_STRUCT_LEN(88, virtchnl2_rxq_info);

/**
 * struct virtchnl2_config_rx_queues - Rx queues config.
 * @vport_id: Vport id.
 * @num_qinfo: Number of instances.
 * @pad: Padding.
 * @qinfo: Rx queues config info.
 *
 * PF sends this message to set up parameters for one or more receive queues.
 * This message contains an array of num_qinfo instances of virtchnl2_rxq_info
 * structures. CP configures requested queues and returns a status code.
 * If the number of queues specified is greater than the number of queues
 * associated with the vport, an error is returned and no queues are configured.
 *
 * Associated with VIRTCHNL2_OP_CONFIG_RX_QUEUES.
 */
struct virtchnl2_config_rx_queues {
	__le32 vport_id;
	__le16 num_qinfo;
	u8 pad[18];
	struct virtchnl2_rxq_info qinfo[] __counted_by_le(num_qinfo);
};
VIRTCHNL2_CHECK_STRUCT_LEN(24, virtchnl2_config_rx_queues);

/**
 * struct virtchnl2_add_queues - data for VIRTCHNL2_OP_ADD_QUEUES.
 * @vport_id: Vport id.
 * @num_tx_q: Number of Tx qieues.
 * @num_tx_complq: Number of Tx completion queues.
 * @num_rx_q:  Number of Rx queues.
 * @num_rx_bufq:  Number of Rx buffer queues.
 * @pad: Padding.
 * @chunks: Chunks of contiguous queues.
 *
 * PF sends this message to request additional transmit/receive queues beyond
 * the ones that were assigned via CREATE_VPORT request. virtchnl2_add_queues
 * structure is used to specify the number of each type of queues.
 * CP responds with the same structure with the actual number of queues assigned
 * followed by num_chunks of virtchnl2_queue_chunk structures.
 *
 * Associated with VIRTCHNL2_OP_ADD_QUEUES.
 */
struct virtchnl2_add_queues {
	__le32 vport_id;
	__le16 num_tx_q;
	__le16 num_tx_complq;
	__le16 num_rx_q;
	__le16 num_rx_bufq;
	u8 pad[4];
	struct virtchnl2_queue_reg_chunks chunks;
};
VIRTCHNL2_CHECK_STRUCT_LEN(24, virtchnl2_add_queues);

/**
 * struct virtchnl2_vector_chunk - Structure to specify a chunk of contiguous
 *				   interrupt vectors.
 * @start_vector_id: Start vector id.
 * @start_evv_id: Start EVV id.
 * @num_vectors: Number of vectors.
 * @pad: Padding.
 * @dynctl_reg_start: DYN_CTL register offset.
 * @dynctl_reg_spacing: register spacing between DYN_CTL registers of 2
 *			consecutive vectors.
 * @itrn_reg_start: ITRN register offset.
 * @itrn_reg_spacing: Register spacing between dynctl registers of 2
 *		      consecutive vectors.
 * @itrn_index_spacing: Register spacing between itrn registers of the same
 *			vector where n=0..2.
 * @pad1: Padding for future extensions.
 *
 * Register offsets and spacing provided by CP.
 * Dynamic control registers are used for enabling/disabling/re-enabling
 * interrupts and updating interrupt rates in the hotpath. Any changes
 * to interrupt rates in the dynamic control registers will be reflected
 * in the interrupt throttling rate registers.
 * itrn registers are used to update interrupt rates for specific
 * interrupt indices without modifying the state of the interrupt.
 */
struct virtchnl2_vector_chunk {
	__le16 start_vector_id;
	__le16 start_evv_id;
	__le16 num_vectors;
	__le16 pad;
	__le32 dynctl_reg_start;
	__le32 dynctl_reg_spacing;
	__le32 itrn_reg_start;
	__le32 itrn_reg_spacing;
	__le32 itrn_index_spacing;
	u8 pad1[4];
};
VIRTCHNL2_CHECK_STRUCT_LEN(32, virtchnl2_vector_chunk);

/**
 * struct virtchnl2_vector_chunks - chunks of contiguous interrupt vectors.
 * @num_vchunks: number of vector chunks.
 * @pad: Padding.
 * @vchunks: Chunks of contiguous vector info.
 *
 * PF sends virtchnl2_vector_chunks struct to specify the vectors it is giving
 * away. CP performs requested action and returns status.
 *
 * Associated with VIRTCHNL2_OP_DEALLOC_VECTORS.
 */
struct virtchnl2_vector_chunks {
	__le16 num_vchunks;
	u8 pad[14];
	struct virtchnl2_vector_chunk vchunks[] __counted_by_le(num_vchunks);
};
VIRTCHNL2_CHECK_STRUCT_LEN(16, virtchnl2_vector_chunks);

/**
 * struct virtchnl2_alloc_vectors - vector allocation info.
 * @num_vectors: Number of vectors.
 * @pad: Padding.
 * @vchunks: Chunks of contiguous vector info.
 *
 * PF sends this message to request additional interrupt vectors beyond the
 * ones that were assigned via GET_CAPS request. virtchnl2_alloc_vectors
 * structure is used to specify the number of vectors requested. CP responds
 * with the same structure with the actual number of vectors assigned followed
 * by virtchnl2_vector_chunks structure identifying the vector ids.
 *
 * Associated with VIRTCHNL2_OP_ALLOC_VECTORS.
 */
struct virtchnl2_alloc_vectors {
	__le16 num_vectors;
	u8 pad[14];
	struct virtchnl2_vector_chunks vchunks;
};
VIRTCHNL2_CHECK_STRUCT_LEN(32, virtchnl2_alloc_vectors);

/**
 * struct virtchnl2_rss_lut - RSS LUT info.
 * @vport_id: Vport id.
 * @lut_entries_start: Start of LUT entries.
 * @lut_entries: Number of LUT entrties.
 * @pad: Padding.
 * @lut: RSS lookup table.
 *
 * PF sends this message to get or set RSS lookup table. Only supported if
 * both PF and CP drivers set the VIRTCHNL2_CAP_RSS bit during configuration
 * negotiation.
 *
 * Associated with VIRTCHNL2_OP_GET_RSS_LUT and VIRTCHNL2_OP_SET_RSS_LUT.
 */
struct virtchnl2_rss_lut {
	__le32 vport_id;
	__le16 lut_entries_start;
	__le16 lut_entries;
	u8 pad[4];
	__le32 lut[] __counted_by_le(lut_entries);
};
VIRTCHNL2_CHECK_STRUCT_LEN(12, virtchnl2_rss_lut);

/**
 * struct virtchnl2_rss_hash - RSS hash info.
 * @ptype_groups: Packet type groups bitmap.
 * @vport_id: Vport id.
 * @pad: Padding for future extensions.
 *
 * PF sends these messages to get and set the hash filter enable bits for RSS.
 * By default, the CP sets these to all possible traffic types that the
 * hardware supports. The PF can query this value if it wants to change the
 * traffic types that are hashed by the hardware.
 * Only supported if both PF and CP drivers set the VIRTCHNL2_CAP_RSS bit
 * during configuration negotiation.
 *
 * Associated with VIRTCHNL2_OP_GET_RSS_HASH and VIRTCHNL2_OP_SET_RSS_HASH
 */
struct virtchnl2_rss_hash {
	__le64 ptype_groups;
	__le32 vport_id;
	u8 pad[4];
};
VIRTCHNL2_CHECK_STRUCT_LEN(16, virtchnl2_rss_hash);

/**
 * struct virtchnl2_sriov_vfs_info - VFs info.
 * @num_vfs: Number of VFs.
 * @pad: Padding for future extensions.
 *
 * This message is used to set number of SRIOV VFs to be created. The actual
 * allocation of resources for the VFs in terms of vport, queues and interrupts
 * is done by CP. When this call completes, the IDPF driver calls
 * pci_enable_sriov to let the OS instantiate the SRIOV PCIE devices.
 * The number of VFs set to 0 will destroy all the VFs of this function.
 *
 * Associated with VIRTCHNL2_OP_SET_SRIOV_VFS.
 */
struct virtchnl2_sriov_vfs_info {
	__le16 num_vfs;
	__le16 pad;
};
VIRTCHNL2_CHECK_STRUCT_LEN(4, virtchnl2_sriov_vfs_info);

/**
 * struct virtchnl2_ptype - Packet type info.
 * @ptype_id_10: 10-bit packet type.
 * @ptype_id_8: 8-bit packet type.
 * @proto_id_count: Number of protocol ids the packet supports, maximum of 32
 *		    protocol ids are supported.
 * @pad: Padding.
 * @proto_id: proto_id_count decides the allocation of protocol id array.
 *	      See enum virtchnl2_proto_hdr_type.
 *
 * Based on the descriptor type the PF supports, CP fills ptype_id_10 or
 * ptype_id_8 for flex and base descriptor respectively. If ptype_id_10 value
 * is set to 0xFFFF, PF should consider this ptype as dummy one and it is the
 * last ptype.
 */
struct virtchnl2_ptype {
	__le16 ptype_id_10;
	u8 ptype_id_8;
	u8 proto_id_count;
	__le16 pad;
	__le16 proto_id[] __counted_by(proto_id_count);
} __packed __aligned(2);
VIRTCHNL2_CHECK_STRUCT_LEN(6, virtchnl2_ptype);

/**
 * struct virtchnl2_get_ptype_info - Packet type info.
 * @start_ptype_id: Starting ptype ID.
 * @num_ptypes: Number of packet types from start_ptype_id.
 * @pad: Padding for future extensions.
 *
 * The total number of supported packet types is based on the descriptor type.
 * For the flex descriptor, it is 1024 (10-bit ptype), and for the base
 * descriptor, it is 256 (8-bit ptype). Send this message to the CP by
 * populating the 'start_ptype_id' and the 'num_ptypes'. CP responds with the
 * 'start_ptype_id', 'num_ptypes', and the array of ptype (virtchnl2_ptype) that
 * are added at the end of the 'virtchnl2_get_ptype_info' message (Note: There
 * is no specific field for the ptypes but are added at the end of the
 * ptype info message. PF/VF is expected to extract the ptypes accordingly.
 * Reason for doing this is because compiler doesn't allow nested flexible
 * array fields).
 *
 * If all the ptypes don't fit into one mailbox buffer, CP splits the
 * ptype info into multiple messages, where each message will have its own
 * 'start_ptype_id', 'num_ptypes', and the ptype array itself. When CP is done
 * updating all the ptype information extracted from the package (the number of
 * ptypes extracted might be less than what PF/VF expects), it will append a
 * dummy ptype (which has 'ptype_id_10' of 'struct virtchnl2_ptype' as 0xFFFF)
 * to the ptype array.
 *
 * PF/VF is expected to receive multiple VIRTCHNL2_OP_GET_PTYPE_INFO messages.
 *
 * Associated with VIRTCHNL2_OP_GET_PTYPE_INFO.
 */
struct virtchnl2_get_ptype_info {
	__le16 start_ptype_id;
	__le16 num_ptypes;
	__le32 pad;
};
VIRTCHNL2_CHECK_STRUCT_LEN(8, virtchnl2_get_ptype_info);

/**
 * struct virtchnl2_vport_stats - Vport statistics.
 * @vport_id: Vport id.
 * @pad: Padding.
 * @rx_bytes: Received bytes.
 * @rx_unicast: Received unicast packets.
 * @rx_multicast: Received multicast packets.
 * @rx_broadcast: Received broadcast packets.
 * @rx_discards: Discarded packets on receive.
 * @rx_errors: Receive errors.
 * @rx_unknown_protocol: Unlnown protocol.
 * @tx_bytes: Transmitted bytes.
 * @tx_unicast: Transmitted unicast packets.
 * @tx_multicast: Transmitted multicast packets.
 * @tx_broadcast: Transmitted broadcast packets.
 * @tx_discards: Discarded packets on transmit.
 * @tx_errors: Transmit errors.
 * @rx_invalid_frame_length: Packets with invalid frame length.
 * @rx_overflow_drop: Packets dropped on buffer overflow.
 *
 * PF/VF sends this message to CP to get the update stats by specifying the
 * vport_id. CP responds with stats in struct virtchnl2_vport_stats.
 *
 * Associated with VIRTCHNL2_OP_GET_STATS.
 */
struct virtchnl2_vport_stats {
	__le32 vport_id;
	u8 pad[4];
	__le64 rx_bytes;
	__le64 rx_unicast;
	__le64 rx_multicast;
	__le64 rx_broadcast;
	__le64 rx_discards;
	__le64 rx_errors;
	__le64 rx_unknown_protocol;
	__le64 tx_bytes;
	__le64 tx_unicast;
	__le64 tx_multicast;
	__le64 tx_broadcast;
	__le64 tx_discards;
	__le64 tx_errors;
	__le64 rx_invalid_frame_length;
	__le64 rx_overflow_drop;
};
VIRTCHNL2_CHECK_STRUCT_LEN(128, virtchnl2_vport_stats);

/**
 * struct virtchnl2_event - Event info.
 * @event: Event opcode. See enum virtchnl2_event_codes.
 * @link_speed: Link_speed provided in Mbps.
 * @vport_id: Vport ID.
 * @link_status: Link status.
 * @pad: Padding.
 * @reserved: Reserved.
 *
 * CP sends this message to inform the PF/VF driver of events that may affect
 * it. No direct response is expected from the driver, though it may generate
 * other messages in response to this one.
 *
 * Associated with VIRTCHNL2_OP_EVENT.
 */
struct virtchnl2_event {
	__le32 event;
	__le32 link_speed;
	__le32 vport_id;
	u8 link_status;
	u8 pad;
	__le16 reserved;
};
VIRTCHNL2_CHECK_STRUCT_LEN(16, virtchnl2_event);

/**
 * struct virtchnl2_rss_key - RSS key info.
 * @vport_id: Vport id.
 * @key_len: Length of RSS key.
 * @pad: Padding.
 * @key_flex: RSS hash key, packed bytes.
 * PF/VF sends this message to get or set RSS key. Only supported if both
 * PF/VF and CP drivers set the VIRTCHNL2_CAP_RSS bit during configuration
 * negotiation.
 *
 * Associated with VIRTCHNL2_OP_GET_RSS_KEY and VIRTCHNL2_OP_SET_RSS_KEY.
 */
struct virtchnl2_rss_key {
	__le32 vport_id;
	__le16 key_len;
	u8 pad;
	u8 key_flex[] __counted_by_le(key_len);
} __packed;
VIRTCHNL2_CHECK_STRUCT_LEN(7, virtchnl2_rss_key);

/**
 * struct virtchnl2_queue_chunk - chunk of contiguous queues
 * @type: See enum virtchnl2_queue_type.
 * @start_queue_id: Starting queue id.
 * @num_queues: Number of queues.
 * @pad: Padding for future extensions.
 */
struct virtchnl2_queue_chunk {
	__le32 type;
	__le32 start_queue_id;
	__le32 num_queues;
	u8 pad[4];
};
VIRTCHNL2_CHECK_STRUCT_LEN(16, virtchnl2_queue_chunk);

/* struct virtchnl2_queue_chunks - chunks of contiguous queues
 * @num_chunks: Number of chunks.
 * @pad: Padding.
 * @chunks: Chunks of contiguous queues info.
 */
struct virtchnl2_queue_chunks {
	__le16 num_chunks;
	u8 pad[6];
	struct virtchnl2_queue_chunk chunks[] __counted_by_le(num_chunks);
};
VIRTCHNL2_CHECK_STRUCT_LEN(8, virtchnl2_queue_chunks);

/**
 * struct virtchnl2_del_ena_dis_queues - Enable/disable queues info.
 * @vport_id: Vport id.
 * @pad: Padding.
 * @chunks: Chunks of contiguous queues info.
 *
 * PF sends these messages to enable, disable or delete queues specified in
 * chunks. PF sends virtchnl2_del_ena_dis_queues struct to specify the queues
 * to be enabled/disabled/deleted. Also applicable to single queue receive or
 * transmit. CP performs requested action and returns status.
 *
 * Associated with VIRTCHNL2_OP_ENABLE_QUEUES, VIRTCHNL2_OP_DISABLE_QUEUES and
 * VIRTCHNL2_OP_DISABLE_QUEUES.
 */
struct virtchnl2_del_ena_dis_queues {
	__le32 vport_id;
	u8 pad[4];
	struct virtchnl2_queue_chunks chunks;
};
VIRTCHNL2_CHECK_STRUCT_LEN(16, virtchnl2_del_ena_dis_queues);

/**
 * struct virtchnl2_queue_vector - Queue to vector mapping.
 * @queue_id: Queue id.
 * @vector_id: Vector id.
 * @pad: Padding.
 * @itr_idx: See enum virtchnl2_itr_idx.
 * @queue_type: See enum virtchnl2_queue_type.
 * @pad1: Padding for future extensions.
 */
struct virtchnl2_queue_vector {
	__le32 queue_id;
	__le16 vector_id;
	u8 pad[2];
	__le32 itr_idx;
	__le32 queue_type;
	u8 pad1[8];
};
VIRTCHNL2_CHECK_STRUCT_LEN(24, virtchnl2_queue_vector);

/**
 * struct virtchnl2_queue_vector_maps - Map/unmap queues info.
 * @vport_id: Vport id.
 * @num_qv_maps: Number of queue vector maps.
 * @pad: Padding.
 * @qv_maps: Queue to vector maps.
 *
 * PF sends this message to map or unmap queues to vectors and interrupt
 * throttling rate index registers. External data buffer contains
 * virtchnl2_queue_vector_maps structure that contains num_qv_maps of
 * virtchnl2_queue_vector structures. CP maps the requested queue vector maps
 * after validating the queue and vector ids and returns a status code.
 *
 * Associated with VIRTCHNL2_OP_MAP_QUEUE_VECTOR and
 * VIRTCHNL2_OP_UNMAP_QUEUE_VECTOR.
 */
struct virtchnl2_queue_vector_maps {
	__le32 vport_id;
	__le16 num_qv_maps;
	u8 pad[10];
	struct virtchnl2_queue_vector qv_maps[] __counted_by_le(num_qv_maps);
};
VIRTCHNL2_CHECK_STRUCT_LEN(16, virtchnl2_queue_vector_maps);

/**
 * struct virtchnl2_loopback - Loopback info.
 * @vport_id: Vport id.
 * @enable: Enable/disable.
 * @pad: Padding for future extensions.
 *
 * PF/VF sends this message to transition to/from the loopback state. Setting
 * the 'enable' to 1 enables the loopback state and setting 'enable' to 0
 * disables it. CP configures the state to loopback and returns status.
 *
 * Associated with VIRTCHNL2_OP_LOOPBACK.
 */
struct virtchnl2_loopback {
	__le32 vport_id;
	u8 enable;
	u8 pad[3];
};
VIRTCHNL2_CHECK_STRUCT_LEN(8, virtchnl2_loopback);

/* struct virtchnl2_mac_addr - MAC address info.
 * @addr: MAC address.
 * @type: MAC type. See enum virtchnl2_mac_addr_type.
 * @pad: Padding for future extensions.
 */
struct virtchnl2_mac_addr {
	u8 addr[ETH_ALEN];
	u8 type;
	u8 pad;
};
VIRTCHNL2_CHECK_STRUCT_LEN(8, virtchnl2_mac_addr);

/**
 * struct virtchnl2_mac_addr_list - List of MAC addresses.
 * @vport_id: Vport id.
 * @num_mac_addr: Number of MAC addresses.
 * @pad: Padding.
 * @mac_addr_list: List with MAC address info.
 *
 * PF/VF driver uses this structure to send list of MAC addresses to be
 * added/deleted to the CP where as CP performs the action and returns the
 * status.
 *
 * Associated with VIRTCHNL2_OP_ADD_MAC_ADDR and VIRTCHNL2_OP_DEL_MAC_ADDR.
 */
struct virtchnl2_mac_addr_list {
	__le32 vport_id;
	__le16 num_mac_addr;
	u8 pad[2];
	struct virtchnl2_mac_addr mac_addr_list[] __counted_by_le(num_mac_addr);
};
VIRTCHNL2_CHECK_STRUCT_LEN(8, virtchnl2_mac_addr_list);

/**
 * struct virtchnl2_promisc_info - Promisc type info.
 * @vport_id: Vport id.
 * @flags: See enum virtchnl2_promisc_flags.
 * @pad: Padding for future extensions.
 *
 * PF/VF sends vport id and flags to the CP where as CP performs the action
 * and returns the status.
 *
 * Associated with VIRTCHNL2_OP_CONFIG_PROMISCUOUS_MODE.
 */
struct virtchnl2_promisc_info {
	__le32 vport_id;
	/* See VIRTCHNL2_PROMISC_FLAGS definitions */
	__le16 flags;
	u8 pad[2];
};
VIRTCHNL2_CHECK_STRUCT_LEN(8, virtchnl2_promisc_info);

/**
 * enum virtchnl2_ptp_caps - PTP capabilities
 * @VIRTCHNL2_CAP_PTP_GET_DEVICE_CLK_TIME: direct access to get the time of
 *					   device clock
 * @VIRTCHNL2_CAP_PTP_GET_DEVICE_CLK_TIME_MB: mailbox access to get the time of
 *					      device clock
 * @VIRTCHNL2_CAP_PTP_GET_CROSS_TIME: direct access to cross timestamp
 * @VIRTCHNL2_CAP_PTP_GET_CROSS_TIME_MB: mailbox access to cross timestamp
 * @VIRTCHNL2_CAP_PTP_SET_DEVICE_CLK_TIME: direct access to set the time of
 *					   device clock
 * @VIRTCHNL2_CAP_PTP_SET_DEVICE_CLK_TIME_MB: mailbox access to set the time of
 *					      device clock
 * @VIRTCHNL2_CAP_PTP_ADJ_DEVICE_CLK: direct access to adjust the time of device
 *				      clock
 * @VIRTCHNL2_CAP_PTP_ADJ_DEVICE_CLK_MB: mailbox access to adjust the time of
 *					 device clock
 * @VIRTCHNL2_CAP_PTP_TX_TSTAMPS: direct access to the Tx timestamping
 * @VIRTCHNL2_CAP_PTP_TX_TSTAMPS_MB: mailbox access to the Tx timestamping
 *
 * PF/VF negotiates a set of supported PTP capabilities with the Control Plane.
 * There are two access methods - mailbox (_MB) and direct.
 * PTP capabilities enables Main Timer operations: get/set/adjust Main Timer,
 * cross timestamping and the Tx timestamping.
 */
enum virtchnl2_ptp_caps {
	VIRTCHNL2_CAP_PTP_GET_DEVICE_CLK_TIME		= BIT(0),
	VIRTCHNL2_CAP_PTP_GET_DEVICE_CLK_TIME_MB	= BIT(1),
	VIRTCHNL2_CAP_PTP_GET_CROSS_TIME		= BIT(2),
	VIRTCHNL2_CAP_PTP_GET_CROSS_TIME_MB		= BIT(3),
	VIRTCHNL2_CAP_PTP_SET_DEVICE_CLK_TIME		= BIT(4),
	VIRTCHNL2_CAP_PTP_SET_DEVICE_CLK_TIME_MB	= BIT(5),
	VIRTCHNL2_CAP_PTP_ADJ_DEVICE_CLK		= BIT(6),
	VIRTCHNL2_CAP_PTP_ADJ_DEVICE_CLK_MB		= BIT(7),
	VIRTCHNL2_CAP_PTP_TX_TSTAMPS			= BIT(8),
	VIRTCHNL2_CAP_PTP_TX_TSTAMPS_MB			= BIT(9),
};

/**
 * struct virtchnl2_ptp_clk_reg_offsets - Offsets of device and PHY clocks
 *					  registers.
 * @dev_clk_ns_l: Device clock low register offset
 * @dev_clk_ns_h: Device clock high register offset
 * @phy_clk_ns_l: PHY clock low register offset
 * @phy_clk_ns_h: PHY clock high register offset
 * @cmd_sync_trigger: The command sync trigger register offset
 * @pad: Padding for future extensions
 */
struct virtchnl2_ptp_clk_reg_offsets {
	__le32 dev_clk_ns_l;
	__le32 dev_clk_ns_h;
	__le32 phy_clk_ns_l;
	__le32 phy_clk_ns_h;
	__le32 cmd_sync_trigger;
	u8 pad[4];
};
VIRTCHNL2_CHECK_STRUCT_LEN(24, virtchnl2_ptp_clk_reg_offsets);

/**
 * struct virtchnl2_ptp_cross_time_reg_offsets - Offsets of the device cross
 *						 time registers.
 * @sys_time_ns_l: System time low register offset
 * @sys_time_ns_h: System time high register offset
 * @cmd_sync_trigger: The command sync trigger register offset
 * @pad: Padding for future extensions
 */
struct virtchnl2_ptp_cross_time_reg_offsets {
	__le32 sys_time_ns_l;
	__le32 sys_time_ns_h;
	__le32 cmd_sync_trigger;
	u8 pad[4];
};
VIRTCHNL2_CHECK_STRUCT_LEN(16, virtchnl2_ptp_cross_time_reg_offsets);

/**
 * struct virtchnl2_ptp_clk_adj_reg_offsets - Offsets of device and PHY clocks
 *					      adjustments registers.
 * @dev_clk_cmd_type: Device clock command type register offset
 * @dev_clk_incval_l: Device clock increment value low register offset
 * @dev_clk_incval_h: Device clock increment value high registers offset
 * @dev_clk_shadj_l: Device clock shadow adjust low register offset
 * @dev_clk_shadj_h: Device clock shadow adjust high register offset
 * @phy_clk_cmd_type: PHY timer command type register offset
 * @phy_clk_incval_l: PHY timer increment value low register offset
 * @phy_clk_incval_h: PHY timer increment value high register offset
 * @phy_clk_shadj_l: PHY timer shadow adjust low register offset
 * @phy_clk_shadj_h: PHY timer shadow adjust high register offset
 */
struct virtchnl2_ptp_clk_adj_reg_offsets {
	__le32 dev_clk_cmd_type;
	__le32 dev_clk_incval_l;
	__le32 dev_clk_incval_h;
	__le32 dev_clk_shadj_l;
	__le32 dev_clk_shadj_h;
	__le32 phy_clk_cmd_type;
	__le32 phy_clk_incval_l;
	__le32 phy_clk_incval_h;
	__le32 phy_clk_shadj_l;
	__le32 phy_clk_shadj_h;
};
VIRTCHNL2_CHECK_STRUCT_LEN(40, virtchnl2_ptp_clk_adj_reg_offsets);

/**
 * struct virtchnl2_ptp_tx_tstamp_latch_caps - PTP Tx timestamp latch
 *					       capabilities.
 * @tx_latch_reg_offset_l: Tx timestamp latch low register offset
 * @tx_latch_reg_offset_h: Tx timestamp latch high register offset
 * @index: Latch index provided to the Tx descriptor
 * @pad: Padding for future extensions
 */
struct virtchnl2_ptp_tx_tstamp_latch_caps {
	__le32 tx_latch_reg_offset_l;
	__le32 tx_latch_reg_offset_h;
	u8 index;
	u8 pad[7];
};
VIRTCHNL2_CHECK_STRUCT_LEN(16, virtchnl2_ptp_tx_tstamp_latch_caps);

/**
 * struct virtchnl2_ptp_get_vport_tx_tstamp_caps - Structure that defines Tx
 *						   tstamp entries.
 * @vport_id: Vport number
 * @num_latches: Total number of latches
 * @tstamp_ns_lo_bit: First bit for nanosecond part of the timestamp
 * @tstamp_ns_hi_bit: Last bit for nanosecond part of the timestamp
 * @pad: Padding for future tstamp granularity extensions
 * @tstamp_latches: Capabilities of Tx timestamp entries
 *
 * PF/VF sends this message to negotiate the Tx timestamp latches for each
 * Vport.
 *
 * Associated with VIRTCHNL2_OP_PTP_GET_VPORT_TX_TSTAMP_CAPS.
 */
struct virtchnl2_ptp_get_vport_tx_tstamp_caps {
	__le32 vport_id;
	__le16 num_latches;
	u8 tstamp_ns_lo_bit;
	u8 tstamp_ns_hi_bit;
	u8 pad[8];

	struct virtchnl2_ptp_tx_tstamp_latch_caps tstamp_latches[]
						  __counted_by_le(num_latches);
};
VIRTCHNL2_CHECK_STRUCT_LEN(16, virtchnl2_ptp_get_vport_tx_tstamp_caps);

/**
 * struct virtchnl2_ptp_get_caps - Get PTP capabilities
 * @caps: PTP capability bitmap. See enum virtchnl2_ptp_caps
 * @max_adj: The maximum possible frequency adjustment
 * @base_incval: The default timer increment value
 * @peer_mbx_q_id: ID of the PTP Device Control daemon queue
 * @peer_id: Peer ID for PTP Device Control daemon
 * @secondary_mbx: Indicates to the driver that it should create a secondary
 *		   mailbox to inetract with control plane for PTP
 * @pad: Padding for future extensions
 * @clk_offsets: Main timer and PHY registers offsets
 * @cross_time_offsets: Cross time registers offsets
 * @clk_adj_offsets: Offsets needed to adjust the PHY and the main timer
 *
 * PF/VF sends this message to negotiate PTP capabilities. CP updates bitmap
 * with supported features and fulfills appropriate structures.
 * If HW uses primary MBX for PTP: secondary_mbx is set to false.
 * If HW uses secondary MBX for PTP: secondary_mbx is set to true.
 *	Control plane has 2 MBX and the driver has 1 MBX, send to peer
 *	driver may be used to send a message using valid ptp_peer_mb_q_id and
 *	ptp_peer_id.
 * If HW does not use send to peer driver: secondary_mbx is no care field and
 * peer_mbx_q_id holds invalid value (0xFFFF).
 *
 * Associated with VIRTCHNL2_OP_PTP_GET_CAPS.
 */
struct virtchnl2_ptp_get_caps {
	__le32 caps;
	__le32 max_adj;
	__le64 base_incval;
	__le16 peer_mbx_q_id;
	u8 peer_id;
	u8 secondary_mbx;
	u8 pad[4];

	struct virtchnl2_ptp_clk_reg_offsets clk_offsets;
	struct virtchnl2_ptp_cross_time_reg_offsets cross_time_offsets;
	struct virtchnl2_ptp_clk_adj_reg_offsets clk_adj_offsets;
};
VIRTCHNL2_CHECK_STRUCT_LEN(104, virtchnl2_ptp_get_caps);

/**
 * struct virtchnl2_ptp_tx_tstamp_latch - Structure that describes tx tstamp
 *					  values, index and validity.
 * @tstamp: Timestamp value
 * @index: Timestamp index from which the value is read
 * @valid: Timestamp validity
 * @pad: Padding for future extensions
 */
struct virtchnl2_ptp_tx_tstamp_latch {
	__le64 tstamp;
	u8 index;
	u8 valid;
	u8 pad[6];
};
VIRTCHNL2_CHECK_STRUCT_LEN(16, virtchnl2_ptp_tx_tstamp_latch);

/**
 * struct virtchnl2_ptp_get_vport_tx_tstamp_latches - Tx timestamp latches
 *						      associated with the vport.
 * @vport_id: Number of vport that requests the timestamp
 * @num_latches: Number of latches
 * @get_devtime_with_txtstmp: Flag to request device time along with Tx timestamp
 * @pad: Padding for future extensions
 * @device_time: device time if get_devtime_with_txtstmp was set in request
 * @tstamp_latches: PTP TX timestamp latch
 *
 * PF/VF sends this message to receive a specified number of timestamps
 * entries.
 *
 * Associated with VIRTCHNL2_OP_PTP_GET_VPORT_TX_TSTAMP.
 */
struct virtchnl2_ptp_get_vport_tx_tstamp_latches {
	__le32 vport_id;
	__le16 num_latches;
	u8 get_devtime_with_txtstmp;
	u8 pad[1];
	__le64 device_time;

	struct virtchnl2_ptp_tx_tstamp_latch tstamp_latches[]
					     __counted_by_le(num_latches);
};
VIRTCHNL2_CHECK_STRUCT_LEN(16, virtchnl2_ptp_get_vport_tx_tstamp_latches);

/**
 * struct virtchnl2_ptp_get_dev_clk_time - Associated with message
 *					   VIRTCHNL2_OP_PTP_GET_DEV_CLK_TIME.
 * @dev_time_ns: Device clock time value in nanoseconds
 *
 * PF/VF sends this message to receive the time from the main timer.
 */
struct virtchnl2_ptp_get_dev_clk_time {
	__le64 dev_time_ns;
};
VIRTCHNL2_CHECK_STRUCT_LEN(8, virtchnl2_ptp_get_dev_clk_time);

/**
 * struct virtchnl2_ptp_get_cross_time: Associated with message
 *					VIRTCHNL2_OP_PTP_GET_CROSS_TIME.
 * @sys_time_ns: System counter value expressed in nanoseconds, read
 *		 synchronously with device time
 * @dev_time_ns: Device clock time value expressed in nanoseconds
 *
 * PF/VF sends this message to receive the cross time.
 */
struct virtchnl2_ptp_get_cross_time {
	__le64 sys_time_ns;
	__le64 dev_time_ns;
};
VIRTCHNL2_CHECK_STRUCT_LEN(16, virtchnl2_ptp_get_cross_time);

/**
 * struct virtchnl2_ptp_set_dev_clk_time: Associated with message
 *					  VIRTCHNL2_OP_PTP_SET_DEV_CLK_TIME.
 * @dev_time_ns: Device time value expressed in nanoseconds to set
 *
 * PF/VF sends this message to set the time of the main timer.
 */
struct virtchnl2_ptp_set_dev_clk_time {
	__le64 dev_time_ns;
};
VIRTCHNL2_CHECK_STRUCT_LEN(8, virtchnl2_ptp_set_dev_clk_time);

/**
 * struct virtchnl2_ptp_adj_dev_clk_fine: Associated with message
 *					  VIRTCHNL2_OP_PTP_ADJ_DEV_CLK_FINE.
 * @incval: Source timer increment value per clock cycle
 *
 * PF/VF sends this message to adjust the frequency of the main timer by the
 * indicated increment value.
 */
struct virtchnl2_ptp_adj_dev_clk_fine {
	__le64 incval;
};
VIRTCHNL2_CHECK_STRUCT_LEN(8, virtchnl2_ptp_adj_dev_clk_fine);

/**
 * struct virtchnl2_ptp_adj_dev_clk_time: Associated with message
 *					  VIRTCHNL2_OP_PTP_ADJ_DEV_CLK_TIME.
 * @delta: Offset in nanoseconds to adjust the time by
 *
 * PF/VF sends this message to adjust the time of the main timer by the delta.
 */
struct virtchnl2_ptp_adj_dev_clk_time {
	__le64 delta;
};
VIRTCHNL2_CHECK_STRUCT_LEN(8, virtchnl2_ptp_adj_dev_clk_time);

/**
 * struct virtchnl2_mem_region - MMIO memory region
 * @start_offset: starting offset of the MMIO memory region
 * @size: size of the MMIO memory region
 */
struct virtchnl2_mem_region {
	__le64 start_offset;
	__le64 size;
};
VIRTCHNL2_CHECK_STRUCT_LEN(16, virtchnl2_mem_region);

/**
 * struct virtchnl2_get_lan_memory_regions - List of LAN MMIO memory regions
 * @num_memory_regions: number of memory regions
 * @pad: Padding
 * @mem_reg: List with memory region info
 *
 * PF/VF sends this message to learn what LAN MMIO memory regions it should map.
 */
struct virtchnl2_get_lan_memory_regions {
	__le16 num_memory_regions;
	u8 pad[6];
	struct virtchnl2_mem_region mem_reg[];
};
VIRTCHNL2_CHECK_STRUCT_LEN(8, virtchnl2_get_lan_memory_regions);

#define VIRTCHNL2_MAX_NUM_PROTO_HDRS	4
#define VIRTCHNL2_MAX_SIZE_RAW_PACKET	256
#define VIRTCHNL2_MAX_NUM_ACTIONS	8

/**
 * struct virtchnl2_proto_hdr - represent one protocol header
 * @hdr_type: See enum virtchnl2_proto_hdr_type
 * @pad: padding
 * @buffer_spec: binary buffer based on header type.
 * @buffer_mask: mask applied on buffer_spec.
 *
 * Structure to hold protocol headers based on hdr_type
 */
struct virtchnl2_proto_hdr {
	__le32 hdr_type;
	u8 pad[4];
	u8 buffer_spec[64];
	u8 buffer_mask[64];
};
VIRTCHNL2_CHECK_STRUCT_LEN(136, virtchnl2_proto_hdr);

/**
 * struct virtchnl2_proto_hdrs - struct to represent match criteria
 * @tunnel_level: specify where protocol header(s) start from.
 *                 must be 0 when sending a raw packet request.
 *                 0 - from the outer layer
 *                 1 - from the first inner layer
 *                 2 - from the second inner layer
 * @pad: Padding bytes
 * @count: total number of protocol headers in proto_hdr. 0 for raw packet.
 * @proto_hdr: Array of protocol headers
 * @raw: struct holding raw packet buffer when count is 0
 */
struct virtchnl2_proto_hdrs {
	u8 tunnel_level;
	u8 pad[3];
	__le32 count;
	union {
		struct virtchnl2_proto_hdr
			proto_hdr[VIRTCHNL2_MAX_NUM_PROTO_HDRS];
		struct {
			__le16 pkt_len;
			u8 spec[VIRTCHNL2_MAX_SIZE_RAW_PACKET];
			u8 mask[VIRTCHNL2_MAX_SIZE_RAW_PACKET];
		} raw;
	};
};
VIRTCHNL2_CHECK_STRUCT_LEN(552, virtchnl2_proto_hdrs);

/**
 * struct virtchnl2_rule_action - struct representing single action for a flow
 * @action_type: see enum virtchnl2_action_types
 * @act_conf: union representing action depending on action_type.
 * @act_conf.q_id: queue id to redirect the packets to.
 * @act_conf.q_grp_id: queue group id to redirect the packets to.
 * @act_conf.ctr_id: used for count action. If input value 0xFFFFFFFF control
 *                    plane assigns a new counter and returns the counter ID to
 *                    the driver. If input value is not 0xFFFFFFFF then it must
 *                    be an existing counter given to the driver for an earlier
 *                    flow. Then this flow will share the counter.
 * @act_conf.mark_id: Value used to mark the packets. Used for mark action.
 * @act_conf.reserved: Reserved for future use.
 */
struct virtchnl2_rule_action {
	__le32 action_type;
	union {
		__le32 q_id;
		__le32 q_grp_id;
		__le32 ctr_id;
		__le32 mark_id;
		u8 reserved[8];
	} act_conf;
};
VIRTCHNL2_CHECK_STRUCT_LEN(12, virtchnl2_rule_action);

/**
 * struct virtchnl2_rule_action_set - struct representing multiple actions
 * @count: number of valid actions in the action set of a rule
 * @actions: array of struct virtchnl2_rule_action
 */
struct virtchnl2_rule_action_set {
	/* action count must be less than VIRTCHNL2_MAX_NUM_ACTIONS */
	__le32 count;
	struct virtchnl2_rule_action actions[VIRTCHNL2_MAX_NUM_ACTIONS];
};
VIRTCHNL2_CHECK_STRUCT_LEN(100, virtchnl2_rule_action_set);

/**
 * struct virtchnl2_flow_rule - represent one flow steering rule
 * @proto_hdrs: array of protocol header buffers representing match criteria
 * @action_set: series of actions to be applied for given rule
 * @priority: rule priority.
 * @pad: padding for future extensions.
 */
struct virtchnl2_flow_rule {
	struct virtchnl2_proto_hdrs proto_hdrs;
	struct virtchnl2_rule_action_set action_set;
	__le32 priority;
	u8 pad[8];
};
VIRTCHNL2_CHECK_STRUCT_LEN(664, virtchnl2_flow_rule);

enum virtchnl2_flow_rule_status {
	VIRTCHNL2_FLOW_RULE_SUCCESS			= 1,
	VIRTCHNL2_FLOW_RULE_NORESOURCE			= 2,
	VIRTCHNL2_FLOW_RULE_EXIST			= 3,
	VIRTCHNL2_FLOW_RULE_TIMEOUT			= 4,
	VIRTCHNL2_FLOW_RULE_FLOW_TYPE_NOT_SUPPORTED	= 5,
	VIRTCHNL2_FLOW_RULE_MATCH_KEY_NOT_SUPPORTED	= 6,
	VIRTCHNL2_FLOW_RULE_ACTION_NOT_SUPPORTED	= 7,
	VIRTCHNL2_FLOW_RULE_ACTION_COMBINATION_INVALID	= 8,
	VIRTCHNL2_FLOW_RULE_ACTION_DATA_INVALID		= 9,
	VIRTCHNL2_FLOW_RULE_NOT_ADDED			= 10,
};

/**
 * struct virtchnl2_flow_rule_info: structure representing single flow rule
 * @rule_id: rule_id associated with the flow_rule.
 * @rule_cfg: structure representing rule.
 * @status: status of rule programming. See enum virtchnl2_flow_rule_status.
 */
struct virtchnl2_flow_rule_info {
	__le32 rule_id;
	struct virtchnl2_flow_rule rule_cfg;
	__le32 status;
};
VIRTCHNL2_CHECK_STRUCT_LEN(672, virtchnl2_flow_rule_info);

/**
 * struct virtchnl2_flow_rule_add_del - add/delete a flow steering rule
 * @vport_id: vport id for which the rule is to be added or deleted.
 * @count: Indicates number of rules to be added or deleted.
 * @rule_info: Array of flow rules to be added or deleted.
 *
 * For VIRTCHNL2_OP_FLOW_RULE_ADD, rule_info contains list of rules to be
 * added. If rule_id is 0xFFFFFFFF, then the rule is programmed and not cached.
 *
 * For VIRTCHNL2_OP_FLOW_RULE_DEL, there are two possibilities. The structure
 * can contain either array of rule_ids or array of match keys to be deleted.
 * When match keys are used the corresponding rule_ids must be 0xFFFFFFFF.
 *
 * status member of each rule indicates the result. Maximum of 6 rules can be
 * added or deleted using this method. Driver has to retry in case of any
 * failure of ADD or DEL opcode. CP doesn't retry in case of failure.
 */
struct virtchnl2_flow_rule_add_del {
	__le32 vport_id;
	__le32 count;
	struct virtchnl2_flow_rule_info rule_info[] __counted_by_le(count);
};
VIRTCHNL2_CHECK_STRUCT_LEN(8, virtchnl2_flow_rule_add_del);

#endif /* _VIRTCHNL_2_H_ */
