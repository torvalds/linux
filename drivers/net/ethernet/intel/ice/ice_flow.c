// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Intel Corporation. */

#include "ice_common.h"
#include "ice_flow.h"
#include <net/gre.h>

/* Describe properties of a protocol header field */
struct ice_flow_field_info {
	enum ice_flow_seg_hdr hdr;
	s16 off;	/* Offset from start of a protocol header, in bits */
	u16 size;	/* Size of fields in bits */
	u16 mask;	/* 16-bit mask for field */
};

#define ICE_FLOW_FLD_INFO(_hdr, _offset_bytes, _size_bytes) { \
	.hdr = _hdr, \
	.off = (_offset_bytes) * BITS_PER_BYTE, \
	.size = (_size_bytes) * BITS_PER_BYTE, \
	.mask = 0, \
}

#define ICE_FLOW_FLD_INFO_MSK(_hdr, _offset_bytes, _size_bytes, _mask) { \
	.hdr = _hdr, \
	.off = (_offset_bytes) * BITS_PER_BYTE, \
	.size = (_size_bytes) * BITS_PER_BYTE, \
	.mask = _mask, \
}

/* Table containing properties of supported protocol header fields */
static const
struct ice_flow_field_info ice_flds_info[ICE_FLOW_FIELD_IDX_MAX] = {
	/* Ether */
	/* ICE_FLOW_FIELD_IDX_ETH_DA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ETH, 0, ETH_ALEN),
	/* ICE_FLOW_FIELD_IDX_ETH_SA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ETH, ETH_ALEN, ETH_ALEN),
	/* ICE_FLOW_FIELD_IDX_S_VLAN */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_VLAN, 12, sizeof(__be16)),
	/* ICE_FLOW_FIELD_IDX_C_VLAN */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_VLAN, 14, sizeof(__be16)),
	/* ICE_FLOW_FIELD_IDX_ETH_TYPE */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ETH, 0, sizeof(__be16)),
	/* IPv4 / IPv6 */
	/* ICE_FLOW_FIELD_IDX_IPV4_DSCP */
	ICE_FLOW_FLD_INFO_MSK(ICE_FLOW_SEG_HDR_IPV4, 0, 1, 0x00fc),
	/* ICE_FLOW_FIELD_IDX_IPV6_DSCP */
	ICE_FLOW_FLD_INFO_MSK(ICE_FLOW_SEG_HDR_IPV6, 0, 1, 0x0ff0),
	/* ICE_FLOW_FIELD_IDX_IPV4_TTL */
	ICE_FLOW_FLD_INFO_MSK(ICE_FLOW_SEG_HDR_NONE, 8, 1, 0xff00),
	/* ICE_FLOW_FIELD_IDX_IPV4_PROT */
	ICE_FLOW_FLD_INFO_MSK(ICE_FLOW_SEG_HDR_NONE, 8, 1, 0x00ff),
	/* ICE_FLOW_FIELD_IDX_IPV6_TTL */
	ICE_FLOW_FLD_INFO_MSK(ICE_FLOW_SEG_HDR_NONE, 6, 1, 0x00ff),
	/* ICE_FLOW_FIELD_IDX_IPV6_PROT */
	ICE_FLOW_FLD_INFO_MSK(ICE_FLOW_SEG_HDR_NONE, 6, 1, 0xff00),
	/* ICE_FLOW_FIELD_IDX_IPV4_SA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_IPV4, 12, sizeof(struct in_addr)),
	/* ICE_FLOW_FIELD_IDX_IPV4_DA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_IPV4, 16, sizeof(struct in_addr)),
	/* ICE_FLOW_FIELD_IDX_IPV6_SA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_IPV6, 8, sizeof(struct in6_addr)),
	/* ICE_FLOW_FIELD_IDX_IPV6_DA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_IPV6, 24, sizeof(struct in6_addr)),
	/* Transport */
	/* ICE_FLOW_FIELD_IDX_TCP_SRC_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_TCP, 0, sizeof(__be16)),
	/* ICE_FLOW_FIELD_IDX_TCP_DST_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_TCP, 2, sizeof(__be16)),
	/* ICE_FLOW_FIELD_IDX_UDP_SRC_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_UDP, 0, sizeof(__be16)),
	/* ICE_FLOW_FIELD_IDX_UDP_DST_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_UDP, 2, sizeof(__be16)),
	/* ICE_FLOW_FIELD_IDX_SCTP_SRC_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_SCTP, 0, sizeof(__be16)),
	/* ICE_FLOW_FIELD_IDX_SCTP_DST_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_SCTP, 2, sizeof(__be16)),
	/* ICE_FLOW_FIELD_IDX_TCP_FLAGS */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_TCP, 13, 1),
	/* ARP */
	/* ICE_FLOW_FIELD_IDX_ARP_SIP */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ARP, 14, sizeof(struct in_addr)),
	/* ICE_FLOW_FIELD_IDX_ARP_DIP */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ARP, 24, sizeof(struct in_addr)),
	/* ICE_FLOW_FIELD_IDX_ARP_SHA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ARP, 8, ETH_ALEN),
	/* ICE_FLOW_FIELD_IDX_ARP_DHA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ARP, 18, ETH_ALEN),
	/* ICE_FLOW_FIELD_IDX_ARP_OP */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ARP, 6, sizeof(__be16)),
	/* ICMP */
	/* ICE_FLOW_FIELD_IDX_ICMP_TYPE */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ICMP, 0, 1),
	/* ICE_FLOW_FIELD_IDX_ICMP_CODE */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ICMP, 1, 1),
	/* GRE */
	/* ICE_FLOW_FIELD_IDX_GRE_KEYID */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_GRE, 12,
			  sizeof_field(struct gre_full_hdr, key)),
	/* GTP */
	/* ICE_FLOW_FIELD_IDX_GTPC_TEID */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_GTPC_TEID, 12, sizeof(__be32)),
	/* ICE_FLOW_FIELD_IDX_GTPU_IP_TEID */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_GTPU_IP, 12, sizeof(__be32)),
	/* ICE_FLOW_FIELD_IDX_GTPU_EH_TEID */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_GTPU_EH, 12, sizeof(__be32)),
	/* ICE_FLOW_FIELD_IDX_GTPU_EH_QFI */
	ICE_FLOW_FLD_INFO_MSK(ICE_FLOW_SEG_HDR_GTPU_EH, 22, sizeof(__be16),
			      0x3f00),
	/* ICE_FLOW_FIELD_IDX_GTPU_UP_TEID */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_GTPU_UP, 12, sizeof(__be32)),
	/* ICE_FLOW_FIELD_IDX_GTPU_DWN_TEID */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_GTPU_DWN, 12, sizeof(__be32)),
	/* PPPoE */
	/* ICE_FLOW_FIELD_IDX_PPPOE_SESS_ID */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_PPPOE, 2, sizeof(__be16)),
	/* PFCP */
	/* ICE_FLOW_FIELD_IDX_PFCP_SEID */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_PFCP_SESSION, 12, sizeof(__be64)),
	/* L2TPv3 */
	/* ICE_FLOW_FIELD_IDX_L2TPV3_SESS_ID */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_L2TPV3, 0, sizeof(__be32)),
	/* ESP */
	/* ICE_FLOW_FIELD_IDX_ESP_SPI */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_ESP, 0, sizeof(__be32)),
	/* AH */
	/* ICE_FLOW_FIELD_IDX_AH_SPI */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_AH, 4, sizeof(__be32)),
	/* NAT_T_ESP */
	/* ICE_FLOW_FIELD_IDX_NAT_T_ESP_SPI */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_NAT_T_ESP, 8, sizeof(__be32)),
};

/* Bitmaps indicating relevant packet types for a particular protocol header
 *
 * Packet types for packets with an Outer/First/Single MAC header
 */
static const u32 ice_ptypes_mac_ofos[] = {
	0xFDC00846, 0xBFBF7F7E, 0xF70001DF, 0xFEFDFDFB,
	0x0000077E, 0x00000000, 0x00000000, 0x00000000,
	0x00400000, 0x03FFF000, 0x7FFFFFE0, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last MAC VLAN header */
static const u32 ice_ptypes_macvlan_il[] = {
	0x00000000, 0xBC000000, 0x000001DF, 0xF0000000,
	0x0000077E, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outer/First/Single IPv4 header, does NOT
 * include IPv4 other PTYPEs
 */
static const u32 ice_ptypes_ipv4_ofos[] = {
	0x1DC00000, 0x04000800, 0x00000000, 0x00000000,
	0x00000000, 0x00000155, 0x00000000, 0x00000000,
	0x00000000, 0x000FC000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outer/First/Single IPv4 header, includes
 * IPv4 other PTYPEs
 */
static const u32 ice_ptypes_ipv4_ofos_all[] = {
	0x1DC00000, 0x04000800, 0x00000000, 0x00000000,
	0x00000000, 0x00000155, 0x00000000, 0x00000000,
	0x00000000, 0x000FC000, 0x83E0F800, 0x00000101,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last IPv4 header */
static const u32 ice_ptypes_ipv4_il[] = {
	0xE0000000, 0xB807700E, 0x80000003, 0xE01DC03B,
	0x0000000E, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x001FF800, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outer/First/Single IPv6 header, does NOT
 * include IPv6 other PTYPEs
 */
static const u32 ice_ptypes_ipv6_ofos[] = {
	0x00000000, 0x00000000, 0x77000000, 0x10002000,
	0x00000000, 0x000002AA, 0x00000000, 0x00000000,
	0x00000000, 0x03F00000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outer/First/Single IPv6 header, includes
 * IPv6 other PTYPEs
 */
static const u32 ice_ptypes_ipv6_ofos_all[] = {
	0x00000000, 0x00000000, 0x77000000, 0x10002000,
	0x00000000, 0x000002AA, 0x00000000, 0x00000000,
	0x00080F00, 0x03F00000, 0x7C1F0000, 0x00000206,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last IPv6 header */
static const u32 ice_ptypes_ipv6_il[] = {
	0x00000000, 0x03B80770, 0x000001DC, 0x0EE00000,
	0x00000770, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x7FE00000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outer/First/Single IPv4 header - no L4 */
static const u32 ice_ptypes_ipv4_ofos_no_l4[] = {
	0x10C00000, 0x04000800, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outermost/First ARP header */
static const u32 ice_ptypes_arp_of[] = {
	0x00000800, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last IPv4 header - no L4 */
static const u32 ice_ptypes_ipv4_il_no_l4[] = {
	0x60000000, 0x18043008, 0x80000002, 0x6010c021,
	0x00000008, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outer/First/Single IPv6 header - no L4 */
static const u32 ice_ptypes_ipv6_ofos_no_l4[] = {
	0x00000000, 0x00000000, 0x43000000, 0x10002000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last IPv6 header - no L4 */
static const u32 ice_ptypes_ipv6_il_no_l4[] = {
	0x00000000, 0x02180430, 0x0000010c, 0x086010c0,
	0x00000430, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* UDP Packet types for non-tunneled packets or tunneled
 * packets with inner UDP.
 */
static const u32 ice_ptypes_udp_il[] = {
	0x81000000, 0x20204040, 0x04000010, 0x80810102,
	0x00000040, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00410000, 0x90842000, 0x00000007,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last TCP header */
static const u32 ice_ptypes_tcp_il[] = {
	0x04000000, 0x80810102, 0x10000040, 0x02040408,
	0x00000102, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00820000, 0x21084000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last SCTP header */
static const u32 ice_ptypes_sctp_il[] = {
	0x08000000, 0x01020204, 0x20000081, 0x04080810,
	0x00000204, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x01040000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outermost/First ICMP header */
static const u32 ice_ptypes_icmp_of[] = {
	0x10000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last ICMP header */
static const u32 ice_ptypes_icmp_il[] = {
	0x00000000, 0x02040408, 0x40000102, 0x08101020,
	0x00000408, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x42108000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outermost/First GRE header */
static const u32 ice_ptypes_gre_of[] = {
	0x00000000, 0xBFBF7800, 0x000001DF, 0xFEFDE000,
	0x0000017E, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last MAC header */
static const u32 ice_ptypes_mac_il[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for GTPC */
static const u32 ice_ptypes_gtpc[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000180, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for GTPC with TEID */
static const u32 ice_ptypes_gtpc_tid[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000060, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for GTPU */
static const struct ice_ptype_attributes ice_attr_gtpu_eh[] = {
	{ ICE_MAC_IPV4_GTPU_IPV4_FRAG,	  ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV4_GTPU_IPV4_PAY,	  ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV4_GTPU_IPV4_UDP_PAY, ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV4_GTPU_IPV4_TCP,	  ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV4_GTPU_IPV4_ICMP,	  ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV6_GTPU_IPV4_FRAG,	  ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV6_GTPU_IPV4_PAY,	  ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV6_GTPU_IPV4_UDP_PAY, ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV6_GTPU_IPV4_TCP,	  ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV6_GTPU_IPV4_ICMP,	  ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV4_GTPU_IPV6_FRAG,	  ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV4_GTPU_IPV6_PAY,	  ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV4_GTPU_IPV6_UDP_PAY, ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV4_GTPU_IPV6_TCP,	  ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV4_GTPU_IPV6_ICMPV6,  ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV6_GTPU_IPV6_FRAG,	  ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV6_GTPU_IPV6_PAY,	  ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV6_GTPU_IPV6_UDP_PAY, ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV6_GTPU_IPV6_TCP,	  ICE_PTYPE_ATTR_GTP_PDU_EH },
	{ ICE_MAC_IPV6_GTPU_IPV6_ICMPV6,  ICE_PTYPE_ATTR_GTP_PDU_EH },
};

static const struct ice_ptype_attributes ice_attr_gtpu_down[] = {
	{ ICE_MAC_IPV4_GTPU_IPV4_FRAG,	  ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV4_GTPU_IPV4_PAY,	  ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV4_GTPU_IPV4_UDP_PAY, ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV4_GTPU_IPV4_TCP,	  ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV4_GTPU_IPV4_ICMP,	  ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV6_GTPU_IPV4_FRAG,	  ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV6_GTPU_IPV4_PAY,	  ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV6_GTPU_IPV4_UDP_PAY, ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV6_GTPU_IPV4_TCP,	  ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV6_GTPU_IPV4_ICMP,	  ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV4_GTPU_IPV6_FRAG,	  ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV4_GTPU_IPV6_PAY,	  ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV4_GTPU_IPV6_UDP_PAY, ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV4_GTPU_IPV6_TCP,	  ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV4_GTPU_IPV6_ICMPV6,  ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV6_GTPU_IPV6_FRAG,	  ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV6_GTPU_IPV6_PAY,	  ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV6_GTPU_IPV6_UDP_PAY, ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV6_GTPU_IPV6_TCP,	  ICE_PTYPE_ATTR_GTP_DOWNLINK },
	{ ICE_MAC_IPV6_GTPU_IPV6_ICMPV6,  ICE_PTYPE_ATTR_GTP_DOWNLINK },
};

static const struct ice_ptype_attributes ice_attr_gtpu_up[] = {
	{ ICE_MAC_IPV4_GTPU_IPV4_FRAG,	  ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV4_GTPU_IPV4_PAY,	  ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV4_GTPU_IPV4_UDP_PAY, ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV4_GTPU_IPV4_TCP,	  ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV4_GTPU_IPV4_ICMP,	  ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV6_GTPU_IPV4_FRAG,	  ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV6_GTPU_IPV4_PAY,	  ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV6_GTPU_IPV4_UDP_PAY, ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV6_GTPU_IPV4_TCP,	  ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV6_GTPU_IPV4_ICMP,	  ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV4_GTPU_IPV6_FRAG,	  ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV4_GTPU_IPV6_PAY,	  ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV4_GTPU_IPV6_UDP_PAY, ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV4_GTPU_IPV6_TCP,	  ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV4_GTPU_IPV6_ICMPV6,  ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV6_GTPU_IPV6_FRAG,	  ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV6_GTPU_IPV6_PAY,	  ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV6_GTPU_IPV6_UDP_PAY, ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV6_GTPU_IPV6_TCP,	  ICE_PTYPE_ATTR_GTP_UPLINK },
	{ ICE_MAC_IPV6_GTPU_IPV6_ICMPV6,  ICE_PTYPE_ATTR_GTP_UPLINK },
};

static const u32 ice_ptypes_gtpu[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x7FFFFE00, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for PPPoE */
static const u32 ice_ptypes_pppoe[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x03ffe000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with PFCP NODE header */
static const u32 ice_ptypes_pfcp_node[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x80000000, 0x00000002,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with PFCP SESSION header */
static const u32 ice_ptypes_pfcp_session[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000005,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for L2TPv3 */
static const u32 ice_ptypes_l2tpv3[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000300,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for ESP */
static const u32 ice_ptypes_esp[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000003, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for AH */
static const u32 ice_ptypes_ah[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x0000000C, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with NAT_T ESP header */
static const u32 ice_ptypes_nat_t_esp[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000030, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

static const u32 ice_ptypes_mac_non_ip_ofos[] = {
	0x00000846, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00400000, 0x03FFF000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Manage parameters and info. used during the creation of a flow profile */
struct ice_flow_prof_params {
	enum ice_block blk;
	u16 entry_length; /* # of bytes formatted entry will require */
	u8 es_cnt;
	struct ice_flow_prof *prof;

	/* For ACL, the es[0] will have the data of ICE_RX_MDID_PKT_FLAGS_15_0
	 * This will give us the direction flags.
	 */
	struct ice_fv_word es[ICE_MAX_FV_WORDS];
	/* attributes can be used to add attributes to a particular PTYPE */
	const struct ice_ptype_attributes *attr;
	u16 attr_cnt;

	u16 mask[ICE_MAX_FV_WORDS];
	DECLARE_BITMAP(ptypes, ICE_FLOW_PTYPE_MAX);
};

#define ICE_FLOW_RSS_HDRS_INNER_MASK \
	(ICE_FLOW_SEG_HDR_PPPOE | ICE_FLOW_SEG_HDR_GTPC | \
	ICE_FLOW_SEG_HDR_GTPC_TEID | ICE_FLOW_SEG_HDR_GTPU | \
	ICE_FLOW_SEG_HDR_PFCP_SESSION | ICE_FLOW_SEG_HDR_L2TPV3 | \
	ICE_FLOW_SEG_HDR_ESP | ICE_FLOW_SEG_HDR_AH | \
	ICE_FLOW_SEG_HDR_NAT_T_ESP)

#define ICE_FLOW_SEG_HDRS_L3_MASK	\
	(ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_ARP)
#define ICE_FLOW_SEG_HDRS_L4_MASK	\
	(ICE_FLOW_SEG_HDR_ICMP | ICE_FLOW_SEG_HDR_TCP | ICE_FLOW_SEG_HDR_UDP | \
	 ICE_FLOW_SEG_HDR_SCTP)
/* mask for L4 protocols that are NOT part of IPv4/6 OTHER PTYPE groups */
#define ICE_FLOW_SEG_HDRS_L4_MASK_NO_OTHER	\
	(ICE_FLOW_SEG_HDR_TCP | ICE_FLOW_SEG_HDR_UDP | ICE_FLOW_SEG_HDR_SCTP)

/**
 * ice_flow_val_hdrs - validates packet segments for valid protocol headers
 * @segs: array of one or more packet segments that describe the flow
 * @segs_cnt: number of packet segments provided
 */
static int ice_flow_val_hdrs(struct ice_flow_seg_info *segs, u8 segs_cnt)
{
	u8 i;

	for (i = 0; i < segs_cnt; i++) {
		/* Multiple L3 headers */
		if (segs[i].hdrs & ICE_FLOW_SEG_HDRS_L3_MASK &&
		    !is_power_of_2(segs[i].hdrs & ICE_FLOW_SEG_HDRS_L3_MASK))
			return -EINVAL;

		/* Multiple L4 headers */
		if (segs[i].hdrs & ICE_FLOW_SEG_HDRS_L4_MASK &&
		    !is_power_of_2(segs[i].hdrs & ICE_FLOW_SEG_HDRS_L4_MASK))
			return -EINVAL;
	}

	return 0;
}

/* Sizes of fixed known protocol headers without header options */
#define ICE_FLOW_PROT_HDR_SZ_MAC	14
#define ICE_FLOW_PROT_HDR_SZ_MAC_VLAN	(ICE_FLOW_PROT_HDR_SZ_MAC + 2)
#define ICE_FLOW_PROT_HDR_SZ_IPV4	20
#define ICE_FLOW_PROT_HDR_SZ_IPV6	40
#define ICE_FLOW_PROT_HDR_SZ_ARP	28
#define ICE_FLOW_PROT_HDR_SZ_ICMP	8
#define ICE_FLOW_PROT_HDR_SZ_TCP	20
#define ICE_FLOW_PROT_HDR_SZ_UDP	8
#define ICE_FLOW_PROT_HDR_SZ_SCTP	12

/**
 * ice_flow_calc_seg_sz - calculates size of a packet segment based on headers
 * @params: information about the flow to be processed
 * @seg: index of packet segment whose header size is to be determined
 */
static u16 ice_flow_calc_seg_sz(struct ice_flow_prof_params *params, u8 seg)
{
	u16 sz;

	/* L2 headers */
	sz = (params->prof->segs[seg].hdrs & ICE_FLOW_SEG_HDR_VLAN) ?
		ICE_FLOW_PROT_HDR_SZ_MAC_VLAN : ICE_FLOW_PROT_HDR_SZ_MAC;

	/* L3 headers */
	if (params->prof->segs[seg].hdrs & ICE_FLOW_SEG_HDR_IPV4)
		sz += ICE_FLOW_PROT_HDR_SZ_IPV4;
	else if (params->prof->segs[seg].hdrs & ICE_FLOW_SEG_HDR_IPV6)
		sz += ICE_FLOW_PROT_HDR_SZ_IPV6;
	else if (params->prof->segs[seg].hdrs & ICE_FLOW_SEG_HDR_ARP)
		sz += ICE_FLOW_PROT_HDR_SZ_ARP;
	else if (params->prof->segs[seg].hdrs & ICE_FLOW_SEG_HDRS_L4_MASK)
		/* An L3 header is required if L4 is specified */
		return 0;

	/* L4 headers */
	if (params->prof->segs[seg].hdrs & ICE_FLOW_SEG_HDR_ICMP)
		sz += ICE_FLOW_PROT_HDR_SZ_ICMP;
	else if (params->prof->segs[seg].hdrs & ICE_FLOW_SEG_HDR_TCP)
		sz += ICE_FLOW_PROT_HDR_SZ_TCP;
	else if (params->prof->segs[seg].hdrs & ICE_FLOW_SEG_HDR_UDP)
		sz += ICE_FLOW_PROT_HDR_SZ_UDP;
	else if (params->prof->segs[seg].hdrs & ICE_FLOW_SEG_HDR_SCTP)
		sz += ICE_FLOW_PROT_HDR_SZ_SCTP;

	return sz;
}

/**
 * ice_flow_proc_seg_hdrs - process protocol headers present in pkt segments
 * @params: information about the flow to be processed
 *
 * This function identifies the packet types associated with the protocol
 * headers being present in packet segments of the specified flow profile.
 */
static int ice_flow_proc_seg_hdrs(struct ice_flow_prof_params *params)
{
	struct ice_flow_prof *prof;
	u8 i;

	memset(params->ptypes, 0xff, sizeof(params->ptypes));

	prof = params->prof;

	for (i = 0; i < params->prof->segs_cnt; i++) {
		const unsigned long *src;
		u32 hdrs;

		hdrs = prof->segs[i].hdrs;

		if (hdrs & ICE_FLOW_SEG_HDR_ETH) {
			src = !i ? (const unsigned long *)ice_ptypes_mac_ofos :
				(const unsigned long *)ice_ptypes_mac_il;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		}

		if (i && hdrs & ICE_FLOW_SEG_HDR_VLAN) {
			src = (const unsigned long *)ice_ptypes_macvlan_il;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		}

		if (!i && hdrs & ICE_FLOW_SEG_HDR_ARP) {
			bitmap_and(params->ptypes, params->ptypes,
				   (const unsigned long *)ice_ptypes_arp_of,
				   ICE_FLOW_PTYPE_MAX);
		}

		if ((hdrs & ICE_FLOW_SEG_HDR_IPV4) &&
		    (hdrs & ICE_FLOW_SEG_HDR_IPV_OTHER)) {
			src = i ? (const unsigned long *)ice_ptypes_ipv4_il :
				(const unsigned long *)ice_ptypes_ipv4_ofos_all;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if ((hdrs & ICE_FLOW_SEG_HDR_IPV6) &&
			   (hdrs & ICE_FLOW_SEG_HDR_IPV_OTHER)) {
			src = i ? (const unsigned long *)ice_ptypes_ipv6_il :
				(const unsigned long *)ice_ptypes_ipv6_ofos_all;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if ((hdrs & ICE_FLOW_SEG_HDR_IPV4) &&
			   !(hdrs & ICE_FLOW_SEG_HDRS_L4_MASK_NO_OTHER)) {
			src = !i ? (const unsigned long *)ice_ptypes_ipv4_ofos_no_l4 :
				(const unsigned long *)ice_ptypes_ipv4_il_no_l4;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_IPV4) {
			src = !i ? (const unsigned long *)ice_ptypes_ipv4_ofos :
				(const unsigned long *)ice_ptypes_ipv4_il;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if ((hdrs & ICE_FLOW_SEG_HDR_IPV6) &&
			   !(hdrs & ICE_FLOW_SEG_HDRS_L4_MASK_NO_OTHER)) {
			src = !i ? (const unsigned long *)ice_ptypes_ipv6_ofos_no_l4 :
				(const unsigned long *)ice_ptypes_ipv6_il_no_l4;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_IPV6) {
			src = !i ? (const unsigned long *)ice_ptypes_ipv6_ofos :
				(const unsigned long *)ice_ptypes_ipv6_il;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		}

		if (hdrs & ICE_FLOW_SEG_HDR_ETH_NON_IP) {
			src = (const unsigned long *)ice_ptypes_mac_non_ip_ofos;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_PPPOE) {
			src = (const unsigned long *)ice_ptypes_pppoe;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else {
			src = (const unsigned long *)ice_ptypes_pppoe;
			bitmap_andnot(params->ptypes, params->ptypes, src,
				      ICE_FLOW_PTYPE_MAX);
		}

		if (hdrs & ICE_FLOW_SEG_HDR_UDP) {
			src = (const unsigned long *)ice_ptypes_udp_il;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_TCP) {
			bitmap_and(params->ptypes, params->ptypes,
				   (const unsigned long *)ice_ptypes_tcp_il,
				   ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_SCTP) {
			src = (const unsigned long *)ice_ptypes_sctp_il;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		}

		if (hdrs & ICE_FLOW_SEG_HDR_ICMP) {
			src = !i ? (const unsigned long *)ice_ptypes_icmp_of :
				(const unsigned long *)ice_ptypes_icmp_il;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_GRE) {
			if (!i) {
				src = (const unsigned long *)ice_ptypes_gre_of;
				bitmap_and(params->ptypes, params->ptypes,
					   src, ICE_FLOW_PTYPE_MAX);
			}
		} else if (hdrs & ICE_FLOW_SEG_HDR_GTPC) {
			src = (const unsigned long *)ice_ptypes_gtpc;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_GTPC_TEID) {
			src = (const unsigned long *)ice_ptypes_gtpc_tid;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_GTPU_DWN) {
			src = (const unsigned long *)ice_ptypes_gtpu;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);

			/* Attributes for GTP packet with downlink */
			params->attr = ice_attr_gtpu_down;
			params->attr_cnt = ARRAY_SIZE(ice_attr_gtpu_down);
		} else if (hdrs & ICE_FLOW_SEG_HDR_GTPU_UP) {
			src = (const unsigned long *)ice_ptypes_gtpu;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);

			/* Attributes for GTP packet with uplink */
			params->attr = ice_attr_gtpu_up;
			params->attr_cnt = ARRAY_SIZE(ice_attr_gtpu_up);
		} else if (hdrs & ICE_FLOW_SEG_HDR_GTPU_EH) {
			src = (const unsigned long *)ice_ptypes_gtpu;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);

			/* Attributes for GTP packet with Extension Header */
			params->attr = ice_attr_gtpu_eh;
			params->attr_cnt = ARRAY_SIZE(ice_attr_gtpu_eh);
		} else if (hdrs & ICE_FLOW_SEG_HDR_GTPU_IP) {
			src = (const unsigned long *)ice_ptypes_gtpu;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_L2TPV3) {
			src = (const unsigned long *)ice_ptypes_l2tpv3;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_ESP) {
			src = (const unsigned long *)ice_ptypes_esp;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_AH) {
			src = (const unsigned long *)ice_ptypes_ah;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_NAT_T_ESP) {
			src = (const unsigned long *)ice_ptypes_nat_t_esp;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		}

		if (hdrs & ICE_FLOW_SEG_HDR_PFCP) {
			if (hdrs & ICE_FLOW_SEG_HDR_PFCP_NODE)
				src = (const unsigned long *)ice_ptypes_pfcp_node;
			else
				src = (const unsigned long *)ice_ptypes_pfcp_session;

			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else {
			src = (const unsigned long *)ice_ptypes_pfcp_node;
			bitmap_andnot(params->ptypes, params->ptypes, src,
				      ICE_FLOW_PTYPE_MAX);

			src = (const unsigned long *)ice_ptypes_pfcp_session;
			bitmap_andnot(params->ptypes, params->ptypes, src,
				      ICE_FLOW_PTYPE_MAX);
		}
	}

	return 0;
}

/**
 * ice_flow_xtract_fld - Create an extraction sequence entry for the given field
 * @hw: pointer to the HW struct
 * @params: information about the flow to be processed
 * @seg: packet segment index of the field to be extracted
 * @fld: ID of field to be extracted
 * @match: bit field of all fields
 *
 * This function determines the protocol ID, offset, and size of the given
 * field. It then allocates one or more extraction sequence entries for the
 * given field, and fill the entries with protocol ID and offset information.
 */
static int
ice_flow_xtract_fld(struct ice_hw *hw, struct ice_flow_prof_params *params,
		    u8 seg, enum ice_flow_field fld, u64 match)
{
	enum ice_flow_field sib = ICE_FLOW_FIELD_IDX_MAX;
	enum ice_prot_id prot_id = ICE_PROT_ID_INVAL;
	u8 fv_words = hw->blk[params->blk].es.fvw;
	struct ice_flow_fld_info *flds;
	u16 cnt, ese_bits, i;
	u16 sib_mask = 0;
	u16 mask;
	u16 off;

	flds = params->prof->segs[seg].fields;

	switch (fld) {
	case ICE_FLOW_FIELD_IDX_ETH_DA:
	case ICE_FLOW_FIELD_IDX_ETH_SA:
	case ICE_FLOW_FIELD_IDX_S_VLAN:
	case ICE_FLOW_FIELD_IDX_C_VLAN:
		prot_id = seg == 0 ? ICE_PROT_MAC_OF_OR_S : ICE_PROT_MAC_IL;
		break;
	case ICE_FLOW_FIELD_IDX_ETH_TYPE:
		prot_id = seg == 0 ? ICE_PROT_ETYPE_OL : ICE_PROT_ETYPE_IL;
		break;
	case ICE_FLOW_FIELD_IDX_IPV4_DSCP:
		prot_id = seg == 0 ? ICE_PROT_IPV4_OF_OR_S : ICE_PROT_IPV4_IL;
		break;
	case ICE_FLOW_FIELD_IDX_IPV6_DSCP:
		prot_id = seg == 0 ? ICE_PROT_IPV6_OF_OR_S : ICE_PROT_IPV6_IL;
		break;
	case ICE_FLOW_FIELD_IDX_IPV4_TTL:
	case ICE_FLOW_FIELD_IDX_IPV4_PROT:
		prot_id = seg == 0 ? ICE_PROT_IPV4_OF_OR_S : ICE_PROT_IPV4_IL;

		/* TTL and PROT share the same extraction seq. entry.
		 * Each is considered a sibling to the other in terms of sharing
		 * the same extraction sequence entry.
		 */
		if (fld == ICE_FLOW_FIELD_IDX_IPV4_TTL)
			sib = ICE_FLOW_FIELD_IDX_IPV4_PROT;
		else if (fld == ICE_FLOW_FIELD_IDX_IPV4_PROT)
			sib = ICE_FLOW_FIELD_IDX_IPV4_TTL;

		/* If the sibling field is also included, that field's
		 * mask needs to be included.
		 */
		if (match & BIT(sib))
			sib_mask = ice_flds_info[sib].mask;
		break;
	case ICE_FLOW_FIELD_IDX_IPV6_TTL:
	case ICE_FLOW_FIELD_IDX_IPV6_PROT:
		prot_id = seg == 0 ? ICE_PROT_IPV6_OF_OR_S : ICE_PROT_IPV6_IL;

		/* TTL and PROT share the same extraction seq. entry.
		 * Each is considered a sibling to the other in terms of sharing
		 * the same extraction sequence entry.
		 */
		if (fld == ICE_FLOW_FIELD_IDX_IPV6_TTL)
			sib = ICE_FLOW_FIELD_IDX_IPV6_PROT;
		else if (fld == ICE_FLOW_FIELD_IDX_IPV6_PROT)
			sib = ICE_FLOW_FIELD_IDX_IPV6_TTL;

		/* If the sibling field is also included, that field's
		 * mask needs to be included.
		 */
		if (match & BIT(sib))
			sib_mask = ice_flds_info[sib].mask;
		break;
	case ICE_FLOW_FIELD_IDX_IPV4_SA:
	case ICE_FLOW_FIELD_IDX_IPV4_DA:
		prot_id = seg == 0 ? ICE_PROT_IPV4_OF_OR_S : ICE_PROT_IPV4_IL;
		break;
	case ICE_FLOW_FIELD_IDX_IPV6_SA:
	case ICE_FLOW_FIELD_IDX_IPV6_DA:
		prot_id = seg == 0 ? ICE_PROT_IPV6_OF_OR_S : ICE_PROT_IPV6_IL;
		break;
	case ICE_FLOW_FIELD_IDX_TCP_SRC_PORT:
	case ICE_FLOW_FIELD_IDX_TCP_DST_PORT:
	case ICE_FLOW_FIELD_IDX_TCP_FLAGS:
		prot_id = ICE_PROT_TCP_IL;
		break;
	case ICE_FLOW_FIELD_IDX_UDP_SRC_PORT:
	case ICE_FLOW_FIELD_IDX_UDP_DST_PORT:
		prot_id = ICE_PROT_UDP_IL_OR_S;
		break;
	case ICE_FLOW_FIELD_IDX_SCTP_SRC_PORT:
	case ICE_FLOW_FIELD_IDX_SCTP_DST_PORT:
		prot_id = ICE_PROT_SCTP_IL;
		break;
	case ICE_FLOW_FIELD_IDX_GTPC_TEID:
	case ICE_FLOW_FIELD_IDX_GTPU_IP_TEID:
	case ICE_FLOW_FIELD_IDX_GTPU_UP_TEID:
	case ICE_FLOW_FIELD_IDX_GTPU_DWN_TEID:
	case ICE_FLOW_FIELD_IDX_GTPU_EH_TEID:
	case ICE_FLOW_FIELD_IDX_GTPU_EH_QFI:
		/* GTP is accessed through UDP OF protocol */
		prot_id = ICE_PROT_UDP_OF;
		break;
	case ICE_FLOW_FIELD_IDX_PPPOE_SESS_ID:
		prot_id = ICE_PROT_PPPOE;
		break;
	case ICE_FLOW_FIELD_IDX_PFCP_SEID:
		prot_id = ICE_PROT_UDP_IL_OR_S;
		break;
	case ICE_FLOW_FIELD_IDX_L2TPV3_SESS_ID:
		prot_id = ICE_PROT_L2TPV3;
		break;
	case ICE_FLOW_FIELD_IDX_ESP_SPI:
		prot_id = ICE_PROT_ESP_F;
		break;
	case ICE_FLOW_FIELD_IDX_AH_SPI:
		prot_id = ICE_PROT_ESP_2;
		break;
	case ICE_FLOW_FIELD_IDX_NAT_T_ESP_SPI:
		prot_id = ICE_PROT_UDP_IL_OR_S;
		break;
	case ICE_FLOW_FIELD_IDX_ARP_SIP:
	case ICE_FLOW_FIELD_IDX_ARP_DIP:
	case ICE_FLOW_FIELD_IDX_ARP_SHA:
	case ICE_FLOW_FIELD_IDX_ARP_DHA:
	case ICE_FLOW_FIELD_IDX_ARP_OP:
		prot_id = ICE_PROT_ARP_OF;
		break;
	case ICE_FLOW_FIELD_IDX_ICMP_TYPE:
	case ICE_FLOW_FIELD_IDX_ICMP_CODE:
		/* ICMP type and code share the same extraction seq. entry */
		prot_id = (params->prof->segs[seg].hdrs & ICE_FLOW_SEG_HDR_IPV4) ?
				ICE_PROT_ICMP_IL : ICE_PROT_ICMPV6_IL;
		sib = fld == ICE_FLOW_FIELD_IDX_ICMP_TYPE ?
			ICE_FLOW_FIELD_IDX_ICMP_CODE :
			ICE_FLOW_FIELD_IDX_ICMP_TYPE;
		break;
	case ICE_FLOW_FIELD_IDX_GRE_KEYID:
		prot_id = ICE_PROT_GRE_OF;
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* Each extraction sequence entry is a word in size, and extracts a
	 * word-aligned offset from a protocol header.
	 */
	ese_bits = ICE_FLOW_FV_EXTRACT_SZ * BITS_PER_BYTE;

	flds[fld].xtrct.prot_id = prot_id;
	flds[fld].xtrct.off = (ice_flds_info[fld].off / ese_bits) *
		ICE_FLOW_FV_EXTRACT_SZ;
	flds[fld].xtrct.disp = (u8)(ice_flds_info[fld].off % ese_bits);
	flds[fld].xtrct.idx = params->es_cnt;
	flds[fld].xtrct.mask = ice_flds_info[fld].mask;

	/* Adjust the next field-entry index after accommodating the number of
	 * entries this field consumes
	 */
	cnt = DIV_ROUND_UP(flds[fld].xtrct.disp + ice_flds_info[fld].size,
			   ese_bits);

	/* Fill in the extraction sequence entries needed for this field */
	off = flds[fld].xtrct.off;
	mask = flds[fld].xtrct.mask;
	for (i = 0; i < cnt; i++) {
		/* Only consume an extraction sequence entry if there is no
		 * sibling field associated with this field or the sibling entry
		 * already extracts the word shared with this field.
		 */
		if (sib == ICE_FLOW_FIELD_IDX_MAX ||
		    flds[sib].xtrct.prot_id == ICE_PROT_ID_INVAL ||
		    flds[sib].xtrct.off != off) {
			u8 idx;

			/* Make sure the number of extraction sequence required
			 * does not exceed the block's capability
			 */
			if (params->es_cnt >= fv_words)
				return -ENOSPC;

			/* some blocks require a reversed field vector layout */
			if (hw->blk[params->blk].es.reverse)
				idx = fv_words - params->es_cnt - 1;
			else
				idx = params->es_cnt;

			params->es[idx].prot_id = prot_id;
			params->es[idx].off = off;
			params->mask[idx] = mask | sib_mask;
			params->es_cnt++;
		}

		off += ICE_FLOW_FV_EXTRACT_SZ;
	}

	return 0;
}

/**
 * ice_flow_xtract_raws - Create extract sequence entries for raw bytes
 * @hw: pointer to the HW struct
 * @params: information about the flow to be processed
 * @seg: index of packet segment whose raw fields are to be extracted
 */
static int
ice_flow_xtract_raws(struct ice_hw *hw, struct ice_flow_prof_params *params,
		     u8 seg)
{
	u16 fv_words;
	u16 hdrs_sz;
	u8 i;

	if (!params->prof->segs[seg].raws_cnt)
		return 0;

	if (params->prof->segs[seg].raws_cnt >
	    ARRAY_SIZE(params->prof->segs[seg].raws))
		return -ENOSPC;

	/* Offsets within the segment headers are not supported */
	hdrs_sz = ice_flow_calc_seg_sz(params, seg);
	if (!hdrs_sz)
		return -EINVAL;

	fv_words = hw->blk[params->blk].es.fvw;

	for (i = 0; i < params->prof->segs[seg].raws_cnt; i++) {
		struct ice_flow_seg_fld_raw *raw;
		u16 off, cnt, j;

		raw = &params->prof->segs[seg].raws[i];

		/* Storing extraction information */
		raw->info.xtrct.prot_id = ICE_PROT_MAC_OF_OR_S;
		raw->info.xtrct.off = (raw->off / ICE_FLOW_FV_EXTRACT_SZ) *
			ICE_FLOW_FV_EXTRACT_SZ;
		raw->info.xtrct.disp = (raw->off % ICE_FLOW_FV_EXTRACT_SZ) *
			BITS_PER_BYTE;
		raw->info.xtrct.idx = params->es_cnt;

		/* Determine the number of field vector entries this raw field
		 * consumes.
		 */
		cnt = DIV_ROUND_UP(raw->info.xtrct.disp +
				   (raw->info.src.last * BITS_PER_BYTE),
				   (ICE_FLOW_FV_EXTRACT_SZ * BITS_PER_BYTE));
		off = raw->info.xtrct.off;
		for (j = 0; j < cnt; j++) {
			u16 idx;

			/* Make sure the number of extraction sequence required
			 * does not exceed the block's capability
			 */
			if (params->es_cnt >= hw->blk[params->blk].es.count ||
			    params->es_cnt >= ICE_MAX_FV_WORDS)
				return -ENOSPC;

			/* some blocks require a reversed field vector layout */
			if (hw->blk[params->blk].es.reverse)
				idx = fv_words - params->es_cnt - 1;
			else
				idx = params->es_cnt;

			params->es[idx].prot_id = raw->info.xtrct.prot_id;
			params->es[idx].off = off;
			params->es_cnt++;
			off += ICE_FLOW_FV_EXTRACT_SZ;
		}
	}

	return 0;
}

/**
 * ice_flow_create_xtrct_seq - Create an extraction sequence for given segments
 * @hw: pointer to the HW struct
 * @params: information about the flow to be processed
 *
 * This function iterates through all matched fields in the given segments, and
 * creates an extraction sequence for the fields.
 */
static int
ice_flow_create_xtrct_seq(struct ice_hw *hw,
			  struct ice_flow_prof_params *params)
{
	struct ice_flow_prof *prof = params->prof;
	int status = 0;
	u8 i;

	for (i = 0; i < prof->segs_cnt; i++) {
		u64 match = params->prof->segs[i].match;
		enum ice_flow_field j;

		for_each_set_bit(j, (unsigned long *)&match,
				 ICE_FLOW_FIELD_IDX_MAX) {
			status = ice_flow_xtract_fld(hw, params, i, j, match);
			if (status)
				return status;
			clear_bit(j, (unsigned long *)&match);
		}

		/* Process raw matching bytes */
		status = ice_flow_xtract_raws(hw, params, i);
		if (status)
			return status;
	}

	return status;
}

/**
 * ice_flow_proc_segs - process all packet segments associated with a profile
 * @hw: pointer to the HW struct
 * @params: information about the flow to be processed
 */
static int
ice_flow_proc_segs(struct ice_hw *hw, struct ice_flow_prof_params *params)
{
	int status;

	status = ice_flow_proc_seg_hdrs(params);
	if (status)
		return status;

	status = ice_flow_create_xtrct_seq(hw, params);
	if (status)
		return status;

	switch (params->blk) {
	case ICE_BLK_FD:
	case ICE_BLK_RSS:
		status = 0;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return status;
}

#define ICE_FLOW_FIND_PROF_CHK_FLDS	0x00000001
#define ICE_FLOW_FIND_PROF_CHK_VSI	0x00000002
#define ICE_FLOW_FIND_PROF_NOT_CHK_DIR	0x00000004
#define ICE_FLOW_FIND_PROF_CHK_SYMM	0x00000008

/**
 * ice_flow_find_prof_conds - Find a profile matching headers and conditions
 * @hw: pointer to the HW struct
 * @blk: classification stage
 * @dir: flow direction
 * @segs: array of one or more packet segments that describe the flow
 * @segs_cnt: number of packet segments provided
 * @symm: symmetric setting for RSS profiles
 * @vsi_handle: software VSI handle to check VSI (ICE_FLOW_FIND_PROF_CHK_VSI)
 * @conds: additional conditions to be checked (ICE_FLOW_FIND_PROF_CHK_*)
 */
static struct ice_flow_prof *
ice_flow_find_prof_conds(struct ice_hw *hw, enum ice_block blk,
			 enum ice_flow_dir dir, struct ice_flow_seg_info *segs,
			 u8 segs_cnt, bool symm, u16 vsi_handle, u32 conds)
{
	struct ice_flow_prof *p, *prof = NULL;

	mutex_lock(&hw->fl_profs_locks[blk]);
	list_for_each_entry(p, &hw->fl_profs[blk], l_entry)
		if ((p->dir == dir || conds & ICE_FLOW_FIND_PROF_NOT_CHK_DIR) &&
		    segs_cnt && segs_cnt == p->segs_cnt) {
			u8 i;

			/* Check for profile-VSI association if specified */
			if ((conds & ICE_FLOW_FIND_PROF_CHK_VSI) &&
			    ice_is_vsi_valid(hw, vsi_handle) &&
			    !test_bit(vsi_handle, p->vsis))
				continue;

			/* Check for symmetric settings */
			if ((conds & ICE_FLOW_FIND_PROF_CHK_SYMM) &&
			    p->symm != symm)
				continue;

			/* Protocol headers must be checked. Matched fields are
			 * checked if specified.
			 */
			for (i = 0; i < segs_cnt; i++)
				if (segs[i].hdrs != p->segs[i].hdrs ||
				    ((conds & ICE_FLOW_FIND_PROF_CHK_FLDS) &&
				     segs[i].match != p->segs[i].match))
					break;

			/* A match is found if all segments are matched */
			if (i == segs_cnt) {
				prof = p;
				break;
			}
		}
	mutex_unlock(&hw->fl_profs_locks[blk]);

	return prof;
}

/**
 * ice_flow_find_prof_id - Look up a profile with given profile ID
 * @hw: pointer to the HW struct
 * @blk: classification stage
 * @prof_id: unique ID to identify this flow profile
 */
static struct ice_flow_prof *
ice_flow_find_prof_id(struct ice_hw *hw, enum ice_block blk, u64 prof_id)
{
	struct ice_flow_prof *p;

	list_for_each_entry(p, &hw->fl_profs[blk], l_entry)
		if (p->id == prof_id)
			return p;

	return NULL;
}

/**
 * ice_flow_rem_entry_sync - Remove a flow entry
 * @hw: pointer to the HW struct
 * @blk: classification stage
 * @entry: flow entry to be removed
 */
static int
ice_flow_rem_entry_sync(struct ice_hw *hw, enum ice_block __always_unused blk,
			struct ice_flow_entry *entry)
{
	if (!entry)
		return -EINVAL;

	list_del(&entry->l_entry);

	devm_kfree(ice_hw_to_dev(hw), entry);

	return 0;
}

/**
 * ice_flow_add_prof_sync - Add a flow profile for packet segments and fields
 * @hw: pointer to the HW struct
 * @blk: classification stage
 * @dir: flow direction
 * @segs: array of one or more packet segments that describe the flow
 * @segs_cnt: number of packet segments provided
 * @symm: symmetric setting for RSS profiles
 * @prof: stores the returned flow profile added
 *
 * Assumption: the caller has acquired the lock to the profile list
 */
static int
ice_flow_add_prof_sync(struct ice_hw *hw, enum ice_block blk,
		       enum ice_flow_dir dir,
		       struct ice_flow_seg_info *segs, u8 segs_cnt,
		       bool symm, struct ice_flow_prof **prof)
{
	struct ice_flow_prof_params *params;
	struct ice_prof_id *ids;
	int status;
	u64 prof_id;
	u8 i;

	if (!prof)
		return -EINVAL;

	ids = &hw->blk[blk].prof_id;
	prof_id = find_first_zero_bit(ids->id, ids->count);
	if (prof_id >= ids->count)
		return -ENOSPC;

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	params->prof = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*params->prof),
				    GFP_KERNEL);
	if (!params->prof) {
		status = -ENOMEM;
		goto free_params;
	}

	/* initialize extraction sequence to all invalid (0xff) */
	for (i = 0; i < ICE_MAX_FV_WORDS; i++) {
		params->es[i].prot_id = ICE_PROT_INVALID;
		params->es[i].off = ICE_FV_OFFSET_INVAL;
	}

	params->blk = blk;
	params->prof->id = prof_id;
	params->prof->dir = dir;
	params->prof->segs_cnt = segs_cnt;
	params->prof->symm = symm;

	/* Make a copy of the segments that need to be persistent in the flow
	 * profile instance
	 */
	for (i = 0; i < segs_cnt; i++)
		memcpy(&params->prof->segs[i], &segs[i], sizeof(*segs));

	status = ice_flow_proc_segs(hw, params);
	if (status) {
		ice_debug(hw, ICE_DBG_FLOW, "Error processing a flow's packet segments\n");
		goto out;
	}

	/* Add a HW profile for this flow profile */
	status = ice_add_prof(hw, blk, prof_id, (u8 *)params->ptypes,
			      params->attr, params->attr_cnt, params->es,
			      params->mask, symm);
	if (status) {
		ice_debug(hw, ICE_DBG_FLOW, "Error adding a HW flow profile\n");
		goto out;
	}

	INIT_LIST_HEAD(&params->prof->entries);
	mutex_init(&params->prof->entries_lock);
	set_bit(prof_id, ids->id);
	*prof = params->prof;

out:
	if (status)
		devm_kfree(ice_hw_to_dev(hw), params->prof);
free_params:
	kfree(params);

	return status;
}

/**
 * ice_flow_rem_prof_sync - remove a flow profile
 * @hw: pointer to the hardware structure
 * @blk: classification stage
 * @prof: pointer to flow profile to remove
 *
 * Assumption: the caller has acquired the lock to the profile list
 */
static int
ice_flow_rem_prof_sync(struct ice_hw *hw, enum ice_block blk,
		       struct ice_flow_prof *prof)
{
	int status;

	/* Remove all remaining flow entries before removing the flow profile */
	if (!list_empty(&prof->entries)) {
		struct ice_flow_entry *e, *t;

		mutex_lock(&prof->entries_lock);

		list_for_each_entry_safe(e, t, &prof->entries, l_entry) {
			status = ice_flow_rem_entry_sync(hw, blk, e);
			if (status)
				break;
		}

		mutex_unlock(&prof->entries_lock);
	}

	/* Remove all hardware profiles associated with this flow profile */
	status = ice_rem_prof(hw, blk, prof->id);
	if (!status) {
		clear_bit(prof->id, hw->blk[blk].prof_id.id);
		list_del(&prof->l_entry);
		mutex_destroy(&prof->entries_lock);
		devm_kfree(ice_hw_to_dev(hw), prof);
	}

	return status;
}

/**
 * ice_flow_assoc_prof - associate a VSI with a flow profile
 * @hw: pointer to the hardware structure
 * @blk: classification stage
 * @prof: pointer to flow profile
 * @vsi_handle: software VSI handle
 *
 * Assumption: the caller has acquired the lock to the profile list
 * and the software VSI handle has been validated
 */
static int
ice_flow_assoc_prof(struct ice_hw *hw, enum ice_block blk,
		    struct ice_flow_prof *prof, u16 vsi_handle)
{
	int status = 0;

	if (!test_bit(vsi_handle, prof->vsis)) {
		status = ice_add_prof_id_flow(hw, blk,
					      ice_get_hw_vsi_num(hw,
								 vsi_handle),
					      prof->id);
		if (!status)
			set_bit(vsi_handle, prof->vsis);
		else
			ice_debug(hw, ICE_DBG_FLOW, "HW profile add failed, %d\n",
				  status);
	}

	return status;
}

/**
 * ice_flow_disassoc_prof - disassociate a VSI from a flow profile
 * @hw: pointer to the hardware structure
 * @blk: classification stage
 * @prof: pointer to flow profile
 * @vsi_handle: software VSI handle
 *
 * Assumption: the caller has acquired the lock to the profile list
 * and the software VSI handle has been validated
 */
static int
ice_flow_disassoc_prof(struct ice_hw *hw, enum ice_block blk,
		       struct ice_flow_prof *prof, u16 vsi_handle)
{
	int status = 0;

	if (test_bit(vsi_handle, prof->vsis)) {
		status = ice_rem_prof_id_flow(hw, blk,
					      ice_get_hw_vsi_num(hw,
								 vsi_handle),
					      prof->id);
		if (!status)
			clear_bit(vsi_handle, prof->vsis);
		else
			ice_debug(hw, ICE_DBG_FLOW, "HW profile remove failed, %d\n",
				  status);
	}

	return status;
}

/**
 * ice_flow_add_prof - Add a flow profile for packet segments and matched fields
 * @hw: pointer to the HW struct
 * @blk: classification stage
 * @dir: flow direction
 * @segs: array of one or more packet segments that describe the flow
 * @segs_cnt: number of packet segments provided
 * @symm: symmetric setting for RSS profiles
 * @prof: stores the returned flow profile added
 */
int
ice_flow_add_prof(struct ice_hw *hw, enum ice_block blk, enum ice_flow_dir dir,
		  struct ice_flow_seg_info *segs, u8 segs_cnt,
		  bool symm, struct ice_flow_prof **prof)
{
	int status;

	if (segs_cnt > ICE_FLOW_SEG_MAX)
		return -ENOSPC;

	if (!segs_cnt)
		return -EINVAL;

	if (!segs)
		return -EINVAL;

	status = ice_flow_val_hdrs(segs, segs_cnt);
	if (status)
		return status;

	mutex_lock(&hw->fl_profs_locks[blk]);

	status = ice_flow_add_prof_sync(hw, blk, dir, segs, segs_cnt,
					symm, prof);
	if (!status)
		list_add(&(*prof)->l_entry, &hw->fl_profs[blk]);

	mutex_unlock(&hw->fl_profs_locks[blk]);

	return status;
}

/**
 * ice_flow_rem_prof - Remove a flow profile and all entries associated with it
 * @hw: pointer to the HW struct
 * @blk: the block for which the flow profile is to be removed
 * @prof_id: unique ID of the flow profile to be removed
 */
int ice_flow_rem_prof(struct ice_hw *hw, enum ice_block blk, u64 prof_id)
{
	struct ice_flow_prof *prof;
	int status;

	mutex_lock(&hw->fl_profs_locks[blk]);

	prof = ice_flow_find_prof_id(hw, blk, prof_id);
	if (!prof) {
		status = -ENOENT;
		goto out;
	}

	/* prof becomes invalid after the call */
	status = ice_flow_rem_prof_sync(hw, blk, prof);

out:
	mutex_unlock(&hw->fl_profs_locks[blk]);

	return status;
}

/**
 * ice_flow_add_entry - Add a flow entry
 * @hw: pointer to the HW struct
 * @blk: classification stage
 * @prof_id: ID of the profile to add a new flow entry to
 * @entry_id: unique ID to identify this flow entry
 * @vsi_handle: software VSI handle for the flow entry
 * @prio: priority of the flow entry
 * @data: pointer to a data buffer containing flow entry's match values/masks
 * @entry_h: pointer to buffer that receives the new flow entry's handle
 */
int
ice_flow_add_entry(struct ice_hw *hw, enum ice_block blk, u64 prof_id,
		   u64 entry_id, u16 vsi_handle, enum ice_flow_priority prio,
		   void *data, u64 *entry_h)
{
	struct ice_flow_entry *e = NULL;
	struct ice_flow_prof *prof;
	int status;

	/* No flow entry data is expected for RSS */
	if (!entry_h || (!data && blk != ICE_BLK_RSS))
		return -EINVAL;

	if (!ice_is_vsi_valid(hw, vsi_handle))
		return -EINVAL;

	mutex_lock(&hw->fl_profs_locks[blk]);

	prof = ice_flow_find_prof_id(hw, blk, prof_id);
	if (!prof) {
		status = -ENOENT;
	} else {
		/* Allocate memory for the entry being added and associate
		 * the VSI to the found flow profile
		 */
		e = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*e), GFP_KERNEL);
		if (!e)
			status = -ENOMEM;
		else
			status = ice_flow_assoc_prof(hw, blk, prof, vsi_handle);
	}

	mutex_unlock(&hw->fl_profs_locks[blk]);
	if (status)
		goto out;

	e->id = entry_id;
	e->vsi_handle = vsi_handle;
	e->prof = prof;
	e->priority = prio;

	switch (blk) {
	case ICE_BLK_FD:
	case ICE_BLK_RSS:
		break;
	default:
		status = -EOPNOTSUPP;
		goto out;
	}

	mutex_lock(&prof->entries_lock);
	list_add(&e->l_entry, &prof->entries);
	mutex_unlock(&prof->entries_lock);

	*entry_h = ICE_FLOW_ENTRY_HNDL(e);

out:
	if (status)
		devm_kfree(ice_hw_to_dev(hw), e);

	return status;
}

/**
 * ice_flow_rem_entry - Remove a flow entry
 * @hw: pointer to the HW struct
 * @blk: classification stage
 * @entry_h: handle to the flow entry to be removed
 */
int ice_flow_rem_entry(struct ice_hw *hw, enum ice_block blk, u64 entry_h)
{
	struct ice_flow_entry *entry;
	struct ice_flow_prof *prof;
	int status = 0;

	if (entry_h == ICE_FLOW_ENTRY_HANDLE_INVAL)
		return -EINVAL;

	entry = ICE_FLOW_ENTRY_PTR(entry_h);

	/* Retain the pointer to the flow profile as the entry will be freed */
	prof = entry->prof;

	if (prof) {
		mutex_lock(&prof->entries_lock);
		status = ice_flow_rem_entry_sync(hw, blk, entry);
		mutex_unlock(&prof->entries_lock);
	}

	return status;
}

/**
 * ice_flow_set_fld_ext - specifies locations of field from entry's input buffer
 * @seg: packet segment the field being set belongs to
 * @fld: field to be set
 * @field_type: type of the field
 * @val_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of the value to match from
 *           entry's input buffer
 * @mask_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of mask value from entry's
 *            input buffer
 * @last_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of last/upper value from
 *            entry's input buffer
 *
 * This helper function stores information of a field being matched, including
 * the type of the field and the locations of the value to match, the mask, and
 * the upper-bound value in the start of the input buffer for a flow entry.
 * This function should only be used for fixed-size data structures.
 *
 * This function also opportunistically determines the protocol headers to be
 * present based on the fields being set. Some fields cannot be used alone to
 * determine the protocol headers present. Sometimes, fields for particular
 * protocol headers are not matched. In those cases, the protocol headers
 * must be explicitly set.
 */
static void
ice_flow_set_fld_ext(struct ice_flow_seg_info *seg, enum ice_flow_field fld,
		     enum ice_flow_fld_match_type field_type, u16 val_loc,
		     u16 mask_loc, u16 last_loc)
{
	u64 bit = BIT_ULL(fld);

	seg->match |= bit;
	if (field_type == ICE_FLOW_FLD_TYPE_RANGE)
		seg->range |= bit;

	seg->fields[fld].type = field_type;
	seg->fields[fld].src.val = val_loc;
	seg->fields[fld].src.mask = mask_loc;
	seg->fields[fld].src.last = last_loc;

	ICE_FLOW_SET_HDRS(seg, ice_flds_info[fld].hdr);
}

/**
 * ice_flow_set_fld - specifies locations of field from entry's input buffer
 * @seg: packet segment the field being set belongs to
 * @fld: field to be set
 * @val_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of the value to match from
 *           entry's input buffer
 * @mask_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of mask value from entry's
 *            input buffer
 * @last_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of last/upper value from
 *            entry's input buffer
 * @range: indicate if field being matched is to be in a range
 *
 * This function specifies the locations, in the form of byte offsets from the
 * start of the input buffer for a flow entry, from where the value to match,
 * the mask value, and upper value can be extracted. These locations are then
 * stored in the flow profile. When adding a flow entry associated with the
 * flow profile, these locations will be used to quickly extract the values and
 * create the content of a match entry. This function should only be used for
 * fixed-size data structures.
 */
void
ice_flow_set_fld(struct ice_flow_seg_info *seg, enum ice_flow_field fld,
		 u16 val_loc, u16 mask_loc, u16 last_loc, bool range)
{
	enum ice_flow_fld_match_type t = range ?
		ICE_FLOW_FLD_TYPE_RANGE : ICE_FLOW_FLD_TYPE_REG;

	ice_flow_set_fld_ext(seg, fld, t, val_loc, mask_loc, last_loc);
}

/**
 * ice_flow_add_fld_raw - sets locations of a raw field from entry's input buf
 * @seg: packet segment the field being set belongs to
 * @off: offset of the raw field from the beginning of the segment in bytes
 * @len: length of the raw pattern to be matched
 * @val_loc: location of the value to match from entry's input buffer
 * @mask_loc: location of mask value from entry's input buffer
 *
 * This function specifies the offset of the raw field to be match from the
 * beginning of the specified packet segment, and the locations, in the form of
 * byte offsets from the start of the input buffer for a flow entry, from where
 * the value to match and the mask value to be extracted. These locations are
 * then stored in the flow profile. When adding flow entries to the associated
 * flow profile, these locations can be used to quickly extract the values to
 * create the content of a match entry. This function should only be used for
 * fixed-size data structures.
 */
void
ice_flow_add_fld_raw(struct ice_flow_seg_info *seg, u16 off, u8 len,
		     u16 val_loc, u16 mask_loc)
{
	if (seg->raws_cnt < ICE_FLOW_SEG_RAW_FLD_MAX) {
		seg->raws[seg->raws_cnt].off = off;
		seg->raws[seg->raws_cnt].info.type = ICE_FLOW_FLD_TYPE_SIZE;
		seg->raws[seg->raws_cnt].info.src.val = val_loc;
		seg->raws[seg->raws_cnt].info.src.mask = mask_loc;
		/* The "last" field is used to store the length of the field */
		seg->raws[seg->raws_cnt].info.src.last = len;
	}

	/* Overflows of "raws" will be handled as an error condition later in
	 * the flow when this information is processed.
	 */
	seg->raws_cnt++;
}

/**
 * ice_flow_rem_vsi_prof - remove VSI from flow profile
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 * @prof_id: unique ID to identify this flow profile
 *
 * This function removes the flow entries associated to the input
 * VSI handle and disassociate the VSI from the flow profile.
 */
int ice_flow_rem_vsi_prof(struct ice_hw *hw, u16 vsi_handle, u64 prof_id)
{
	struct ice_flow_prof *prof;
	int status = 0;

	if (!ice_is_vsi_valid(hw, vsi_handle))
		return -EINVAL;

	/* find flow profile pointer with input package block and profile ID */
	prof = ice_flow_find_prof_id(hw, ICE_BLK_FD, prof_id);
	if (!prof) {
		ice_debug(hw, ICE_DBG_PKG, "Cannot find flow profile id=%llu\n",
			  prof_id);
		return -ENOENT;
	}

	/* Remove all remaining flow entries before removing the flow profile */
	if (!list_empty(&prof->entries)) {
		struct ice_flow_entry *e, *t;

		mutex_lock(&prof->entries_lock);
		list_for_each_entry_safe(e, t, &prof->entries, l_entry) {
			if (e->vsi_handle != vsi_handle)
				continue;

			status = ice_flow_rem_entry_sync(hw, ICE_BLK_FD, e);
			if (status)
				break;
		}
		mutex_unlock(&prof->entries_lock);
	}
	if (status)
		return status;

	/* disassociate the flow profile from sw VSI handle */
	status = ice_flow_disassoc_prof(hw, ICE_BLK_FD, prof, vsi_handle);
	if (status)
		ice_debug(hw, ICE_DBG_PKG, "ice_flow_disassoc_prof() failed with status=%d\n",
			  status);
	return status;
}

#define ICE_FLOW_RSS_SEG_HDR_L2_MASKS \
	(ICE_FLOW_SEG_HDR_ETH | ICE_FLOW_SEG_HDR_VLAN)

#define ICE_FLOW_RSS_SEG_HDR_L3_MASKS \
	(ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_IPV6)

#define ICE_FLOW_RSS_SEG_HDR_L4_MASKS \
	(ICE_FLOW_SEG_HDR_TCP | ICE_FLOW_SEG_HDR_UDP | ICE_FLOW_SEG_HDR_SCTP)

#define ICE_FLOW_RSS_SEG_HDR_VAL_MASKS \
	(ICE_FLOW_RSS_SEG_HDR_L2_MASKS | \
	 ICE_FLOW_RSS_SEG_HDR_L3_MASKS | \
	 ICE_FLOW_RSS_SEG_HDR_L4_MASKS)

/**
 * ice_flow_set_rss_seg_info - setup packet segments for RSS
 * @segs: pointer to the flow field segment(s)
 * @seg_cnt: segment count
 * @cfg: configure parameters
 *
 * Helper function to extract fields from hash bitmap and use flow
 * header value to set flow field segment for further use in flow
 * profile entry or removal.
 */
static int
ice_flow_set_rss_seg_info(struct ice_flow_seg_info *segs, u8 seg_cnt,
			  const struct ice_rss_hash_cfg *cfg)
{
	struct ice_flow_seg_info *seg;
	u64 val;
	u16 i;

	/* set inner most segment */
	seg = &segs[seg_cnt - 1];

	for_each_set_bit(i, (const unsigned long *)&cfg->hash_flds,
			 (u16)ICE_FLOW_FIELD_IDX_MAX)
		ice_flow_set_fld(seg, (enum ice_flow_field)i,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);

	ICE_FLOW_SET_HDRS(seg, cfg->addl_hdrs);

	/* set outer most header */
	if (cfg->hdr_type == ICE_RSS_INNER_HEADERS_W_OUTER_IPV4)
		segs[ICE_RSS_OUTER_HEADERS].hdrs |= ICE_FLOW_SEG_HDR_IPV4 |
						    ICE_FLOW_SEG_HDR_IPV_OTHER;
	else if (cfg->hdr_type == ICE_RSS_INNER_HEADERS_W_OUTER_IPV6)
		segs[ICE_RSS_OUTER_HEADERS].hdrs |= ICE_FLOW_SEG_HDR_IPV6 |
						    ICE_FLOW_SEG_HDR_IPV_OTHER;

	if (seg->hdrs & ~ICE_FLOW_RSS_SEG_HDR_VAL_MASKS &
	    ~ICE_FLOW_RSS_HDRS_INNER_MASK & ~ICE_FLOW_SEG_HDR_IPV_OTHER)
		return -EINVAL;

	val = (u64)(seg->hdrs & ICE_FLOW_RSS_SEG_HDR_L3_MASKS);
	if (val && !is_power_of_2(val))
		return -EIO;

	val = (u64)(seg->hdrs & ICE_FLOW_RSS_SEG_HDR_L4_MASKS);
	if (val && !is_power_of_2(val))
		return -EIO;

	return 0;
}

/**
 * ice_rem_vsi_rss_list - remove VSI from RSS list
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 *
 * Remove the VSI from all RSS configurations in the list.
 */
void ice_rem_vsi_rss_list(struct ice_hw *hw, u16 vsi_handle)
{
	struct ice_rss_cfg *r, *tmp;

	if (list_empty(&hw->rss_list_head))
		return;

	mutex_lock(&hw->rss_locks);
	list_for_each_entry_safe(r, tmp, &hw->rss_list_head, l_entry)
		if (test_and_clear_bit(vsi_handle, r->vsis))
			if (bitmap_empty(r->vsis, ICE_MAX_VSI)) {
				list_del(&r->l_entry);
				devm_kfree(ice_hw_to_dev(hw), r);
			}
	mutex_unlock(&hw->rss_locks);
}

/**
 * ice_rem_vsi_rss_cfg - remove RSS configurations associated with VSI
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 *
 * This function will iterate through all flow profiles and disassociate
 * the VSI from that profile. If the flow profile has no VSIs it will
 * be removed.
 */
int ice_rem_vsi_rss_cfg(struct ice_hw *hw, u16 vsi_handle)
{
	const enum ice_block blk = ICE_BLK_RSS;
	struct ice_flow_prof *p, *t;
	int status = 0;

	if (!ice_is_vsi_valid(hw, vsi_handle))
		return -EINVAL;

	if (list_empty(&hw->fl_profs[blk]))
		return 0;

	mutex_lock(&hw->rss_locks);
	list_for_each_entry_safe(p, t, &hw->fl_profs[blk], l_entry)
		if (test_bit(vsi_handle, p->vsis)) {
			status = ice_flow_disassoc_prof(hw, blk, p, vsi_handle);
			if (status)
				break;

			if (bitmap_empty(p->vsis, ICE_MAX_VSI)) {
				status = ice_flow_rem_prof(hw, blk, p->id);
				if (status)
					break;
			}
		}
	mutex_unlock(&hw->rss_locks);

	return status;
}

/**
 * ice_get_rss_hdr_type - get a RSS profile's header type
 * @prof: RSS flow profile
 */
static enum ice_rss_cfg_hdr_type
ice_get_rss_hdr_type(struct ice_flow_prof *prof)
{
	if (prof->segs_cnt == ICE_FLOW_SEG_SINGLE) {
		return ICE_RSS_OUTER_HEADERS;
	} else if (prof->segs_cnt == ICE_FLOW_SEG_MAX) {
		const struct ice_flow_seg_info *s;

		s = &prof->segs[ICE_RSS_OUTER_HEADERS];
		if (s->hdrs == ICE_FLOW_SEG_HDR_NONE)
			return ICE_RSS_INNER_HEADERS;
		if (s->hdrs & ICE_FLOW_SEG_HDR_IPV4)
			return ICE_RSS_INNER_HEADERS_W_OUTER_IPV4;
		if (s->hdrs & ICE_FLOW_SEG_HDR_IPV6)
			return ICE_RSS_INNER_HEADERS_W_OUTER_IPV6;
	}

	return ICE_RSS_ANY_HEADERS;
}

static bool
ice_rss_match_prof(struct ice_rss_cfg *r, struct ice_flow_prof *prof,
		   enum ice_rss_cfg_hdr_type hdr_type)
{
	return (r->hash.hdr_type == hdr_type &&
		r->hash.hash_flds == prof->segs[prof->segs_cnt - 1].match &&
		r->hash.addl_hdrs == prof->segs[prof->segs_cnt - 1].hdrs);
}

/**
 * ice_rem_rss_list - remove RSS configuration from list
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 * @prof: pointer to flow profile
 *
 * Assumption: lock has already been acquired for RSS list
 */
static void
ice_rem_rss_list(struct ice_hw *hw, u16 vsi_handle, struct ice_flow_prof *prof)
{
	enum ice_rss_cfg_hdr_type hdr_type;
	struct ice_rss_cfg *r, *tmp;

	/* Search for RSS hash fields associated to the VSI that match the
	 * hash configurations associated to the flow profile. If found
	 * remove from the RSS entry list of the VSI context and delete entry.
	 */
	hdr_type = ice_get_rss_hdr_type(prof);
	list_for_each_entry_safe(r, tmp, &hw->rss_list_head, l_entry)
		if (ice_rss_match_prof(r, prof, hdr_type)) {
			clear_bit(vsi_handle, r->vsis);
			if (bitmap_empty(r->vsis, ICE_MAX_VSI)) {
				list_del(&r->l_entry);
				devm_kfree(ice_hw_to_dev(hw), r);
			}
			return;
		}
}

/**
 * ice_add_rss_list - add RSS configuration to list
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 * @prof: pointer to flow profile
 *
 * Assumption: lock has already been acquired for RSS list
 */
static int
ice_add_rss_list(struct ice_hw *hw, u16 vsi_handle, struct ice_flow_prof *prof)
{
	enum ice_rss_cfg_hdr_type hdr_type;
	struct ice_rss_cfg *r, *rss_cfg;

	hdr_type = ice_get_rss_hdr_type(prof);
	list_for_each_entry(r, &hw->rss_list_head, l_entry)
		if (ice_rss_match_prof(r, prof, hdr_type)) {
			set_bit(vsi_handle, r->vsis);
			return 0;
		}

	rss_cfg = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*rss_cfg),
			       GFP_KERNEL);
	if (!rss_cfg)
		return -ENOMEM;

	rss_cfg->hash.hash_flds = prof->segs[prof->segs_cnt - 1].match;
	rss_cfg->hash.addl_hdrs = prof->segs[prof->segs_cnt - 1].hdrs;
	rss_cfg->hash.hdr_type = hdr_type;
	rss_cfg->hash.symm = prof->symm;
	set_bit(vsi_handle, rss_cfg->vsis);

	list_add_tail(&rss_cfg->l_entry, &hw->rss_list_head);

	return 0;
}

/**
 * ice_rss_config_xor_word - set the HSYMM registers for one input set word
 * @hw: pointer to the hardware structure
 * @prof_id: RSS hardware profile id
 * @src: the FV index used by the protocol's source field
 * @dst: the FV index used by the protocol's destination field
 *
 * Write to the HSYMM register with the index of @src FV the value of the @dst
 * FV index. This will tell the hardware to XOR HSYMM[src] with INSET[dst]
 * while calculating the RSS input set.
 */
static void
ice_rss_config_xor_word(struct ice_hw *hw, u8 prof_id, u8 src, u8 dst)
{
	u32 val, reg, bits_shift;
	u8 reg_idx;

	reg_idx = src / GLQF_HSYMM_REG_SIZE;
	bits_shift = ((src % GLQF_HSYMM_REG_SIZE) << 3);
	val = dst | GLQF_HSYMM_ENABLE_BIT;

	reg = rd32(hw, GLQF_HSYMM(prof_id, reg_idx));
	reg = (reg & ~(0xff << bits_shift)) | (val << bits_shift);
	wr32(hw, GLQF_HSYMM(prof_id, reg_idx), reg);
}

/**
 * ice_rss_config_xor - set the symmetric registers for a profile's protocol
 * @hw: pointer to the hardware structure
 * @prof_id: RSS hardware profile id
 * @src: the FV index used by the protocol's source field
 * @dst: the FV index used by the protocol's destination field
 * @len: length of the source/destination fields in words
 */
static void
ice_rss_config_xor(struct ice_hw *hw, u8 prof_id, u8 src, u8 dst, u8 len)
{
	int fv_last_word =
		ICE_FLOW_SW_FIELD_VECTOR_MAX / ICE_FLOW_FV_EXTRACT_SZ - 1;
	int i;

	for (i = 0; i < len; i++) {
		ice_rss_config_xor_word(hw, prof_id,
					/* Yes, field vector in GLQF_HSYMM and
					 * GLQF_HINSET is inversed!
					 */
					fv_last_word - (src + i),
					fv_last_word - (dst + i));
		ice_rss_config_xor_word(hw, prof_id,
					fv_last_word - (dst + i),
					fv_last_word - (src + i));
	}
}

/**
 * ice_rss_set_symm - set the symmetric settings for an RSS profile
 * @hw: pointer to the hardware structure
 * @prof: pointer to flow profile
 *
 * The symmetric hash will result from XORing the protocol's fields with
 * indexes in GLQF_HSYMM and GLQF_HINSET. This function configures the profile's
 * GLQF_HSYMM registers.
 */
static void ice_rss_set_symm(struct ice_hw *hw, struct ice_flow_prof *prof)
{
	struct ice_prof_map *map;
	u8 prof_id, m;

	mutex_lock(&hw->blk[ICE_BLK_RSS].es.prof_map_lock);
	map = ice_search_prof_id(hw, ICE_BLK_RSS, prof->id);
	if (map)
		prof_id = map->prof_id;
	mutex_unlock(&hw->blk[ICE_BLK_RSS].es.prof_map_lock);

	if (!map)
		return;

	/* clear to default */
	for (m = 0; m < GLQF_HSYMM_REG_PER_PROF; m++)
		wr32(hw, GLQF_HSYMM(prof_id, m), 0);

	if (prof->symm) {
		struct ice_flow_seg_xtrct *ipv4_src, *ipv4_dst;
		struct ice_flow_seg_xtrct *ipv6_src, *ipv6_dst;
		struct ice_flow_seg_xtrct *sctp_src, *sctp_dst;
		struct ice_flow_seg_xtrct *tcp_src, *tcp_dst;
		struct ice_flow_seg_xtrct *udp_src, *udp_dst;
		struct ice_flow_seg_info *seg;

		seg = &prof->segs[prof->segs_cnt - 1];

		ipv4_src = &seg->fields[ICE_FLOW_FIELD_IDX_IPV4_SA].xtrct;
		ipv4_dst = &seg->fields[ICE_FLOW_FIELD_IDX_IPV4_DA].xtrct;

		ipv6_src = &seg->fields[ICE_FLOW_FIELD_IDX_IPV6_SA].xtrct;
		ipv6_dst = &seg->fields[ICE_FLOW_FIELD_IDX_IPV6_DA].xtrct;

		tcp_src = &seg->fields[ICE_FLOW_FIELD_IDX_TCP_SRC_PORT].xtrct;
		tcp_dst = &seg->fields[ICE_FLOW_FIELD_IDX_TCP_DST_PORT].xtrct;

		udp_src = &seg->fields[ICE_FLOW_FIELD_IDX_UDP_SRC_PORT].xtrct;
		udp_dst = &seg->fields[ICE_FLOW_FIELD_IDX_UDP_DST_PORT].xtrct;

		sctp_src = &seg->fields[ICE_FLOW_FIELD_IDX_SCTP_SRC_PORT].xtrct;
		sctp_dst = &seg->fields[ICE_FLOW_FIELD_IDX_SCTP_DST_PORT].xtrct;

		/* xor IPv4 */
		if (ipv4_src->prot_id != 0 && ipv4_dst->prot_id != 0)
			ice_rss_config_xor(hw, prof_id,
					   ipv4_src->idx, ipv4_dst->idx, 2);

		/* xor IPv6 */
		if (ipv6_src->prot_id != 0 && ipv6_dst->prot_id != 0)
			ice_rss_config_xor(hw, prof_id,
					   ipv6_src->idx, ipv6_dst->idx, 8);

		/* xor TCP */
		if (tcp_src->prot_id != 0 && tcp_dst->prot_id != 0)
			ice_rss_config_xor(hw, prof_id,
					   tcp_src->idx, tcp_dst->idx, 1);

		/* xor UDP */
		if (udp_src->prot_id != 0 && udp_dst->prot_id != 0)
			ice_rss_config_xor(hw, prof_id,
					   udp_src->idx, udp_dst->idx, 1);

		/* xor SCTP */
		if (sctp_src->prot_id != 0 && sctp_dst->prot_id != 0)
			ice_rss_config_xor(hw, prof_id,
					   sctp_src->idx, sctp_dst->idx, 1);
	}
}

/**
 * ice_add_rss_cfg_sync - add an RSS configuration
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 * @cfg: configure parameters
 *
 * Assumption: lock has already been acquired for RSS list
 */
static int
ice_add_rss_cfg_sync(struct ice_hw *hw, u16 vsi_handle,
		     const struct ice_rss_hash_cfg *cfg)
{
	const enum ice_block blk = ICE_BLK_RSS;
	struct ice_flow_prof *prof = NULL;
	struct ice_flow_seg_info *segs;
	u8 segs_cnt;
	int status;

	segs_cnt = (cfg->hdr_type == ICE_RSS_OUTER_HEADERS) ?
			ICE_FLOW_SEG_SINGLE : ICE_FLOW_SEG_MAX;

	segs = kcalloc(segs_cnt, sizeof(*segs), GFP_KERNEL);
	if (!segs)
		return -ENOMEM;

	/* Construct the packet segment info from the hashed fields */
	status = ice_flow_set_rss_seg_info(segs, segs_cnt, cfg);
	if (status)
		goto exit;

	/* Search for a flow profile that has matching headers, hash fields,
	 * symm and has the input VSI associated to it. If found, no further
	 * operations required and exit.
	 */
	prof = ice_flow_find_prof_conds(hw, blk, ICE_FLOW_RX, segs, segs_cnt,
					cfg->symm, vsi_handle,
					ICE_FLOW_FIND_PROF_CHK_FLDS |
					ICE_FLOW_FIND_PROF_CHK_SYMM |
					ICE_FLOW_FIND_PROF_CHK_VSI);
	if (prof)
		goto exit;

	/* Check if a flow profile exists with the same protocol headers and
	 * associated with the input VSI. If so disassociate the VSI from
	 * this profile. The VSI will be added to a new profile created with
	 * the protocol header and new hash field configuration.
	 */
	prof = ice_flow_find_prof_conds(hw, blk, ICE_FLOW_RX, segs, segs_cnt,
					cfg->symm, vsi_handle,
					ICE_FLOW_FIND_PROF_CHK_VSI);
	if (prof) {
		status = ice_flow_disassoc_prof(hw, blk, prof, vsi_handle);
		if (!status)
			ice_rem_rss_list(hw, vsi_handle, prof);
		else
			goto exit;

		/* Remove profile if it has no VSIs associated */
		if (bitmap_empty(prof->vsis, ICE_MAX_VSI)) {
			status = ice_flow_rem_prof(hw, blk, prof->id);
			if (status)
				goto exit;
		}
	}

	/* Search for a profile that has the same match fields and symmetric
	 * setting. If this exists then associate the VSI to this profile.
	 */
	prof = ice_flow_find_prof_conds(hw, blk, ICE_FLOW_RX, segs, segs_cnt,
					cfg->symm, vsi_handle,
					ICE_FLOW_FIND_PROF_CHK_SYMM |
					ICE_FLOW_FIND_PROF_CHK_FLDS);
	if (prof) {
		status = ice_flow_assoc_prof(hw, blk, prof, vsi_handle);
		if (!status)
			status = ice_add_rss_list(hw, vsi_handle, prof);
		goto exit;
	}

	/* Create a new flow profile with packet segment information. */
	status = ice_flow_add_prof(hw, blk, ICE_FLOW_RX,
				   segs, segs_cnt, cfg->symm, &prof);
	if (status)
		goto exit;

	prof->symm = cfg->symm;
	ice_rss_set_symm(hw, prof);
	status = ice_flow_assoc_prof(hw, blk, prof, vsi_handle);
	/* If association to a new flow profile failed then this profile can
	 * be removed.
	 */
	if (status) {
		ice_flow_rem_prof(hw, blk, prof->id);
		goto exit;
	}

	status = ice_add_rss_list(hw, vsi_handle, prof);

exit:
	kfree(segs);
	return status;
}

/**
 * ice_add_rss_cfg - add an RSS configuration with specified hashed fields
 * @hw: pointer to the hardware structure
 * @vsi: VSI to add the RSS configuration to
 * @cfg: configure parameters
 *
 * This function will generate a flow profile based on fields associated with
 * the input fields to hash on, the flow type and use the VSI number to add
 * a flow entry to the profile.
 */
int
ice_add_rss_cfg(struct ice_hw *hw, struct ice_vsi *vsi,
		const struct ice_rss_hash_cfg *cfg)
{
	struct ice_rss_hash_cfg local_cfg;
	u16 vsi_handle;
	int status;

	if (!vsi)
		return -EINVAL;

	vsi_handle = vsi->idx;
	if (!ice_is_vsi_valid(hw, vsi_handle) ||
	    !cfg || cfg->hdr_type > ICE_RSS_ANY_HEADERS ||
	    cfg->hash_flds == ICE_HASH_INVALID)
		return -EINVAL;

	mutex_lock(&hw->rss_locks);
	local_cfg = *cfg;
	if (cfg->hdr_type < ICE_RSS_ANY_HEADERS) {
		status = ice_add_rss_cfg_sync(hw, vsi_handle, &local_cfg);
	} else {
		local_cfg.hdr_type = ICE_RSS_OUTER_HEADERS;
		status = ice_add_rss_cfg_sync(hw, vsi_handle, &local_cfg);
		if (!status) {
			local_cfg.hdr_type = ICE_RSS_INNER_HEADERS;
			status = ice_add_rss_cfg_sync(hw, vsi_handle,
						      &local_cfg);
		}
	}
	mutex_unlock(&hw->rss_locks);

	return status;
}

/**
 * ice_rem_rss_cfg_sync - remove an existing RSS configuration
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 * @cfg: configure parameters
 *
 * Assumption: lock has already been acquired for RSS list
 */
static int
ice_rem_rss_cfg_sync(struct ice_hw *hw, u16 vsi_handle,
		     const struct ice_rss_hash_cfg *cfg)
{
	const enum ice_block blk = ICE_BLK_RSS;
	struct ice_flow_seg_info *segs;
	struct ice_flow_prof *prof;
	u8 segs_cnt;
	int status;

	segs_cnt = (cfg->hdr_type == ICE_RSS_OUTER_HEADERS) ?
			ICE_FLOW_SEG_SINGLE : ICE_FLOW_SEG_MAX;
	segs = kcalloc(segs_cnt, sizeof(*segs), GFP_KERNEL);
	if (!segs)
		return -ENOMEM;

	/* Construct the packet segment info from the hashed fields */
	status = ice_flow_set_rss_seg_info(segs, segs_cnt, cfg);
	if (status)
		goto out;

	prof = ice_flow_find_prof_conds(hw, blk, ICE_FLOW_RX, segs, segs_cnt,
					cfg->symm, vsi_handle,
					ICE_FLOW_FIND_PROF_CHK_FLDS);
	if (!prof) {
		status = -ENOENT;
		goto out;
	}

	status = ice_flow_disassoc_prof(hw, blk, prof, vsi_handle);
	if (status)
		goto out;

	/* Remove RSS configuration from VSI context before deleting
	 * the flow profile.
	 */
	ice_rem_rss_list(hw, vsi_handle, prof);

	if (bitmap_empty(prof->vsis, ICE_MAX_VSI))
		status = ice_flow_rem_prof(hw, blk, prof->id);

out:
	kfree(segs);
	return status;
}

/**
 * ice_rem_rss_cfg - remove an existing RSS config with matching hashed fields
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 * @cfg: configure parameters
 *
 * This function will lookup the flow profile based on the input
 * hash field bitmap, iterate through the profile entry list of
 * that profile and find entry associated with input VSI to be
 * removed. Calls are made to underlying flow apis which will in
 * turn build or update buffers for RSS XLT1 section.
 */
int
ice_rem_rss_cfg(struct ice_hw *hw, u16 vsi_handle,
		const struct ice_rss_hash_cfg *cfg)
{
	struct ice_rss_hash_cfg local_cfg;
	int status;

	if (!ice_is_vsi_valid(hw, vsi_handle) ||
	    !cfg || cfg->hdr_type > ICE_RSS_ANY_HEADERS ||
	    cfg->hash_flds == ICE_HASH_INVALID)
		return -EINVAL;

	mutex_lock(&hw->rss_locks);
	local_cfg = *cfg;
	if (cfg->hdr_type < ICE_RSS_ANY_HEADERS) {
		status = ice_rem_rss_cfg_sync(hw, vsi_handle, &local_cfg);
	} else {
		local_cfg.hdr_type = ICE_RSS_OUTER_HEADERS;
		status = ice_rem_rss_cfg_sync(hw, vsi_handle, &local_cfg);
		if (!status) {
			local_cfg.hdr_type = ICE_RSS_INNER_HEADERS;
			status = ice_rem_rss_cfg_sync(hw, vsi_handle,
						      &local_cfg);
		}
	}
	mutex_unlock(&hw->rss_locks);

	return status;
}

/* Mapping of AVF hash bit fields to an L3-L4 hash combination.
 * As the ice_flow_avf_hdr_field represent individual bit shifts in a hash,
 * convert its values to their appropriate flow L3, L4 values.
 */
#define ICE_FLOW_AVF_RSS_IPV4_MASKS \
	(BIT_ULL(ICE_AVF_FLOW_FIELD_IPV4_OTHER) | \
	 BIT_ULL(ICE_AVF_FLOW_FIELD_FRAG_IPV4))
#define ICE_FLOW_AVF_RSS_TCP_IPV4_MASKS \
	(BIT_ULL(ICE_AVF_FLOW_FIELD_IPV4_TCP_SYN_NO_ACK) | \
	 BIT_ULL(ICE_AVF_FLOW_FIELD_IPV4_TCP))
#define ICE_FLOW_AVF_RSS_UDP_IPV4_MASKS \
	(BIT_ULL(ICE_AVF_FLOW_FIELD_UNICAST_IPV4_UDP) | \
	 BIT_ULL(ICE_AVF_FLOW_FIELD_MULTICAST_IPV4_UDP) | \
	 BIT_ULL(ICE_AVF_FLOW_FIELD_IPV4_UDP))
#define ICE_FLOW_AVF_RSS_ALL_IPV4_MASKS \
	(ICE_FLOW_AVF_RSS_TCP_IPV4_MASKS | ICE_FLOW_AVF_RSS_UDP_IPV4_MASKS | \
	 ICE_FLOW_AVF_RSS_IPV4_MASKS | BIT_ULL(ICE_AVF_FLOW_FIELD_IPV4_SCTP))

#define ICE_FLOW_AVF_RSS_IPV6_MASKS \
	(BIT_ULL(ICE_AVF_FLOW_FIELD_IPV6_OTHER) | \
	 BIT_ULL(ICE_AVF_FLOW_FIELD_FRAG_IPV6))
#define ICE_FLOW_AVF_RSS_UDP_IPV6_MASKS \
	(BIT_ULL(ICE_AVF_FLOW_FIELD_UNICAST_IPV6_UDP) | \
	 BIT_ULL(ICE_AVF_FLOW_FIELD_MULTICAST_IPV6_UDP) | \
	 BIT_ULL(ICE_AVF_FLOW_FIELD_IPV6_UDP))
#define ICE_FLOW_AVF_RSS_TCP_IPV6_MASKS \
	(BIT_ULL(ICE_AVF_FLOW_FIELD_IPV6_TCP_SYN_NO_ACK) | \
	 BIT_ULL(ICE_AVF_FLOW_FIELD_IPV6_TCP))
#define ICE_FLOW_AVF_RSS_ALL_IPV6_MASKS \
	(ICE_FLOW_AVF_RSS_TCP_IPV6_MASKS | ICE_FLOW_AVF_RSS_UDP_IPV6_MASKS | \
	 ICE_FLOW_AVF_RSS_IPV6_MASKS | BIT_ULL(ICE_AVF_FLOW_FIELD_IPV6_SCTP))

/**
 * ice_add_avf_rss_cfg - add an RSS configuration for AVF driver
 * @hw: pointer to the hardware structure
 * @vsi: VF's VSI
 * @avf_hash: hash bit fields (ICE_AVF_FLOW_FIELD_*) to configure
 *
 * This function will take the hash bitmap provided by the AVF driver via a
 * message, convert it to ICE-compatible values, and configure RSS flow
 * profiles.
 */
int ice_add_avf_rss_cfg(struct ice_hw *hw, struct ice_vsi *vsi, u64 avf_hash)
{
	struct ice_rss_hash_cfg hcfg;
	u16 vsi_handle;
	int status = 0;
	u64 hash_flds;

	if (!vsi)
		return -EINVAL;

	vsi_handle = vsi->idx;
	if (avf_hash == ICE_AVF_FLOW_FIELD_INVALID ||
	    !ice_is_vsi_valid(hw, vsi_handle))
		return -EINVAL;

	/* Make sure no unsupported bits are specified */
	if (avf_hash & ~(ICE_FLOW_AVF_RSS_ALL_IPV4_MASKS |
			 ICE_FLOW_AVF_RSS_ALL_IPV6_MASKS))
		return -EIO;

	hash_flds = avf_hash;

	/* Always create an L3 RSS configuration for any L4 RSS configuration */
	if (hash_flds & ICE_FLOW_AVF_RSS_ALL_IPV4_MASKS)
		hash_flds |= ICE_FLOW_AVF_RSS_IPV4_MASKS;

	if (hash_flds & ICE_FLOW_AVF_RSS_ALL_IPV6_MASKS)
		hash_flds |= ICE_FLOW_AVF_RSS_IPV6_MASKS;

	/* Create the corresponding RSS configuration for each valid hash bit */
	while (hash_flds) {
		u64 rss_hash = ICE_HASH_INVALID;

		if (hash_flds & ICE_FLOW_AVF_RSS_ALL_IPV4_MASKS) {
			if (hash_flds & ICE_FLOW_AVF_RSS_IPV4_MASKS) {
				rss_hash = ICE_FLOW_HASH_IPV4;
				hash_flds &= ~ICE_FLOW_AVF_RSS_IPV4_MASKS;
			} else if (hash_flds &
				   ICE_FLOW_AVF_RSS_TCP_IPV4_MASKS) {
				rss_hash = ICE_FLOW_HASH_IPV4 |
					ICE_FLOW_HASH_TCP_PORT;
				hash_flds &= ~ICE_FLOW_AVF_RSS_TCP_IPV4_MASKS;
			} else if (hash_flds &
				   ICE_FLOW_AVF_RSS_UDP_IPV4_MASKS) {
				rss_hash = ICE_FLOW_HASH_IPV4 |
					ICE_FLOW_HASH_UDP_PORT;
				hash_flds &= ~ICE_FLOW_AVF_RSS_UDP_IPV4_MASKS;
			} else if (hash_flds &
				   BIT_ULL(ICE_AVF_FLOW_FIELD_IPV4_SCTP)) {
				rss_hash = ICE_FLOW_HASH_IPV4 |
					ICE_FLOW_HASH_SCTP_PORT;
				hash_flds &=
					~BIT_ULL(ICE_AVF_FLOW_FIELD_IPV4_SCTP);
			}
		} else if (hash_flds & ICE_FLOW_AVF_RSS_ALL_IPV6_MASKS) {
			if (hash_flds & ICE_FLOW_AVF_RSS_IPV6_MASKS) {
				rss_hash = ICE_FLOW_HASH_IPV6;
				hash_flds &= ~ICE_FLOW_AVF_RSS_IPV6_MASKS;
			} else if (hash_flds &
				   ICE_FLOW_AVF_RSS_TCP_IPV6_MASKS) {
				rss_hash = ICE_FLOW_HASH_IPV6 |
					ICE_FLOW_HASH_TCP_PORT;
				hash_flds &= ~ICE_FLOW_AVF_RSS_TCP_IPV6_MASKS;
			} else if (hash_flds &
				   ICE_FLOW_AVF_RSS_UDP_IPV6_MASKS) {
				rss_hash = ICE_FLOW_HASH_IPV6 |
					ICE_FLOW_HASH_UDP_PORT;
				hash_flds &= ~ICE_FLOW_AVF_RSS_UDP_IPV6_MASKS;
			} else if (hash_flds &
				   BIT_ULL(ICE_AVF_FLOW_FIELD_IPV6_SCTP)) {
				rss_hash = ICE_FLOW_HASH_IPV6 |
					ICE_FLOW_HASH_SCTP_PORT;
				hash_flds &=
					~BIT_ULL(ICE_AVF_FLOW_FIELD_IPV6_SCTP);
			}
		}

		if (rss_hash == ICE_HASH_INVALID)
			return -EIO;

		hcfg.addl_hdrs = ICE_FLOW_SEG_HDR_NONE;
		hcfg.hash_flds = rss_hash;
		hcfg.hdr_type = ICE_RSS_ANY_HEADERS;
		hcfg.symm = false;
		status = ice_add_rss_cfg(hw, vsi, &hcfg);
		if (status)
			break;
	}

	return status;
}

static bool rss_cfg_symm_valid(u64 hfld)
{
	return !((!!(hfld & ICE_FLOW_HASH_FLD_IPV4_SA) ^
		  !!(hfld & ICE_FLOW_HASH_FLD_IPV4_DA)) ||
		 (!!(hfld & ICE_FLOW_HASH_FLD_IPV6_SA) ^
		  !!(hfld & ICE_FLOW_HASH_FLD_IPV6_DA)) ||
		 (!!(hfld & ICE_FLOW_HASH_FLD_TCP_SRC_PORT) ^
		  !!(hfld & ICE_FLOW_HASH_FLD_TCP_DST_PORT)) ||
		 (!!(hfld & ICE_FLOW_HASH_FLD_UDP_SRC_PORT) ^
		  !!(hfld & ICE_FLOW_HASH_FLD_UDP_DST_PORT)) ||
		 (!!(hfld & ICE_FLOW_HASH_FLD_SCTP_SRC_PORT) ^
		  !!(hfld & ICE_FLOW_HASH_FLD_SCTP_DST_PORT)));
}

/**
 * ice_set_rss_cfg_symm - set symmtery for all VSI's RSS configurations
 * @hw: pointer to the hardware structure
 * @vsi: VSI to set/unset Symmetric RSS
 * @symm: TRUE to set Symmetric RSS hashing
 */
int ice_set_rss_cfg_symm(struct ice_hw *hw, struct ice_vsi *vsi, bool symm)
{
	struct ice_rss_hash_cfg	local;
	struct ice_rss_cfg *r, *tmp;
	u16 vsi_handle = vsi->idx;
	int status = 0;

	if (!ice_is_vsi_valid(hw, vsi_handle))
		return -EINVAL;

	mutex_lock(&hw->rss_locks);
	list_for_each_entry_safe(r, tmp, &hw->rss_list_head, l_entry) {
		if (test_bit(vsi_handle, r->vsis) && r->hash.symm != symm) {
			local = r->hash;
			local.symm = symm;
			if (symm && !rss_cfg_symm_valid(r->hash.hash_flds))
				continue;

			status = ice_add_rss_cfg_sync(hw, vsi_handle, &local);
			if (status)
				break;
		}
	}
	mutex_unlock(&hw->rss_locks);

	return status;
}

/**
 * ice_replay_rss_cfg - replay RSS configurations associated with VSI
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 */
int ice_replay_rss_cfg(struct ice_hw *hw, u16 vsi_handle)
{
	struct ice_rss_cfg *r;
	int status = 0;

	if (!ice_is_vsi_valid(hw, vsi_handle))
		return -EINVAL;

	mutex_lock(&hw->rss_locks);
	list_for_each_entry(r, &hw->rss_list_head, l_entry) {
		if (test_bit(vsi_handle, r->vsis)) {
			status = ice_add_rss_cfg_sync(hw, vsi_handle, &r->hash);
			if (status)
				break;
		}
	}
	mutex_unlock(&hw->rss_locks);

	return status;
}

/**
 * ice_get_rss_cfg - returns hashed fields for the given header types
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 * @hdrs: protocol header type
 * @symm: whether the RSS is symmetric (bool, output)
 *
 * This function will return the match fields of the first instance of flow
 * profile having the given header types and containing input VSI
 */
u64 ice_get_rss_cfg(struct ice_hw *hw, u16 vsi_handle, u32 hdrs, bool *symm)
{
	u64 rss_hash = ICE_HASH_INVALID;
	struct ice_rss_cfg *r;

	/* verify if the protocol header is non zero and VSI is valid */
	if (hdrs == ICE_FLOW_SEG_HDR_NONE || !ice_is_vsi_valid(hw, vsi_handle))
		return ICE_HASH_INVALID;

	mutex_lock(&hw->rss_locks);
	list_for_each_entry(r, &hw->rss_list_head, l_entry)
		if (test_bit(vsi_handle, r->vsis) &&
		    r->hash.addl_hdrs == hdrs) {
			rss_hash = r->hash.hash_flds;
			*symm = r->hash.symm;
			break;
		}
	mutex_unlock(&hw->rss_locks);

	return rss_hash;
}
