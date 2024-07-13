// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2024 Intel Corporation */

#include <linux/net/intel/libie/rx.h>

/* O(1) converting i40e/ice/iavf's 8/10-bit hardware packet type to a parsed
 * bitfield struct.
 */

/* A few supplementary definitions for when XDP hash types do not coincide
 * with what can be generated from ptype definitions by means of preprocessor
 * concatenation.
 */
#define XDP_RSS_L3_L2			XDP_RSS_TYPE_NONE
#define XDP_RSS_L4_NONE			XDP_RSS_TYPE_NONE
#define XDP_RSS_L4_TIMESYNC		XDP_RSS_TYPE_NONE
#define XDP_RSS_TYPE_L3			XDP_RSS_TYPE_NONE
#define XDP_RSS_TYPE_L4			XDP_RSS_L4

#define LIBIE_RX_PT(oip, ofrag, tun, tp, tefr, iprot, pl) {		   \
		.outer_ip		= LIBETH_RX_PT_OUTER_##oip,	   \
		.outer_frag		= LIBETH_RX_PT_##ofrag,		   \
		.tunnel_type		= LIBETH_RX_PT_TUNNEL_IP_##tun,	   \
		.tunnel_end_prot	= LIBETH_RX_PT_TUNNEL_END_##tp,	   \
		.tunnel_end_frag	= LIBETH_RX_PT_##tefr,		   \
		.inner_prot		= LIBETH_RX_PT_INNER_##iprot,	   \
		.payload_layer		= LIBETH_RX_PT_PAYLOAD_##pl,	   \
		.hash_type		= XDP_RSS_L3_##oip |		   \
					  XDP_RSS_L4_##iprot |		   \
					  XDP_RSS_TYPE_##pl,		   \
	}

#define LIBIE_RX_PT_UNUSED		{ }

#define __LIBIE_RX_PT_L2(iprot, pl)					   \
	LIBIE_RX_PT(L2, NOT_FRAG, NONE, NONE, NOT_FRAG, iprot, pl)
#define LIBIE_RX_PT_L2		__LIBIE_RX_PT_L2(NONE, L2)
#define LIBIE_RX_PT_TS		__LIBIE_RX_PT_L2(TIMESYNC, L2)
#define LIBIE_RX_PT_L3		__LIBIE_RX_PT_L2(NONE, L3)

#define LIBIE_RX_PT_IP_FRAG(oip)					   \
	LIBIE_RX_PT(IPV##oip, FRAG, NONE, NONE, NOT_FRAG, NONE, L3)
#define LIBIE_RX_PT_IP_L3(oip, tun, teprot, tefr)			   \
	LIBIE_RX_PT(IPV##oip, NOT_FRAG, tun, teprot, tefr, NONE, L3)
#define LIBIE_RX_PT_IP_L4(oip, tun, teprot, iprot)			   \
	LIBIE_RX_PT(IPV##oip, NOT_FRAG, tun, teprot, NOT_FRAG, iprot, L4)

#define LIBIE_RX_PT_IP_NOF(oip, tun, ver)				   \
	LIBIE_RX_PT_IP_L3(oip, tun, ver, NOT_FRAG),			   \
	LIBIE_RX_PT_IP_L4(oip, tun, ver, UDP),				   \
	LIBIE_RX_PT_UNUSED,						   \
	LIBIE_RX_PT_IP_L4(oip, tun, ver, TCP),				   \
	LIBIE_RX_PT_IP_L4(oip, tun, ver, SCTP),				   \
	LIBIE_RX_PT_IP_L4(oip, tun, ver, ICMP)

/* IPv oip --> tun --> IPv ver */
#define LIBIE_RX_PT_IP_TUN_VER(oip, tun, ver)				   \
	LIBIE_RX_PT_IP_L3(oip, tun, ver, FRAG),				   \
	LIBIE_RX_PT_IP_NOF(oip, tun, ver)

/* Non Tunneled IPv oip */
#define LIBIE_RX_PT_IP_RAW(oip)						   \
	LIBIE_RX_PT_IP_FRAG(oip),					   \
	LIBIE_RX_PT_IP_NOF(oip, NONE, NONE)

/* IPv oip --> tun --> { IPv4, IPv6 } */
#define LIBIE_RX_PT_IP_TUN(oip, tun)					   \
	LIBIE_RX_PT_IP_TUN_VER(oip, tun, IPV4),				   \
	LIBIE_RX_PT_IP_TUN_VER(oip, tun, IPV6)

/* IPv oip --> GRE/NAT tun --> { x, IPv4, IPv6 } */
#define LIBIE_RX_PT_IP_GRE(oip, tun)					   \
	LIBIE_RX_PT_IP_L3(oip, tun, NONE, NOT_FRAG),			   \
	LIBIE_RX_PT_IP_TUN(oip, tun)

/* Non Tunneled IPv oip
 * IPv oip --> { IPv4, IPv6 }
 * IPv oip --> GRE/NAT --> { x, IPv4, IPv6 }
 * IPv oip --> GRE/NAT --> MAC --> { x, IPv4, IPv6 }
 * IPv oip --> GRE/NAT --> MAC/VLAN --> { x, IPv4, IPv6 }
 */
#define LIBIE_RX_PT_IP(oip)						   \
	LIBIE_RX_PT_IP_RAW(oip),					   \
	LIBIE_RX_PT_IP_TUN(oip, IP),					   \
	LIBIE_RX_PT_IP_GRE(oip, GRENAT),				   \
	LIBIE_RX_PT_IP_GRE(oip, GRENAT_MAC),				   \
	LIBIE_RX_PT_IP_GRE(oip, GRENAT_MAC_VLAN)

/* Lookup table mapping for O(1) parsing */
const struct libeth_rx_pt libie_rx_pt_lut[LIBIE_RX_PT_NUM] = {
	/* L2 packet types */
	LIBIE_RX_PT_UNUSED,
	LIBIE_RX_PT_L2,
	LIBIE_RX_PT_TS,
	LIBIE_RX_PT_L2,
	LIBIE_RX_PT_UNUSED,
	LIBIE_RX_PT_UNUSED,
	LIBIE_RX_PT_L2,
	LIBIE_RX_PT_L2,
	LIBIE_RX_PT_UNUSED,
	LIBIE_RX_PT_UNUSED,
	LIBIE_RX_PT_L2,
	LIBIE_RX_PT_UNUSED,

	LIBIE_RX_PT_L3,
	LIBIE_RX_PT_L3,
	LIBIE_RX_PT_L3,
	LIBIE_RX_PT_L3,
	LIBIE_RX_PT_L3,
	LIBIE_RX_PT_L3,
	LIBIE_RX_PT_L3,
	LIBIE_RX_PT_L3,
	LIBIE_RX_PT_L3,
	LIBIE_RX_PT_L3,

	LIBIE_RX_PT_IP(4),
	LIBIE_RX_PT_IP(6),
};
EXPORT_SYMBOL_NS_GPL(libie_rx_pt_lut, LIBIE);

MODULE_DESCRIPTION("Intel(R) Ethernet common library");
MODULE_IMPORT_NS(LIBETH);
MODULE_LICENSE("GPL");
