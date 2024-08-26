/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_PROTOCOL_TYPE_H_
#define _ICE_PROTOCOL_TYPE_H_
#define ICE_IPV6_ADDR_LENGTH 16

/* Each recipe can match up to 5 different fields. Fields to match can be meta-
 * data, values extracted from packet headers, or results from other recipes.
 * Therefore, up to 5 recipes can provide intermediate results to another one
 * through chaining, e.g. recipes 0, 1, 2, 3 and 4 can provide intermediate
 * results to recipe 5. Note that one of the fields in one of the recipes must
 * always be reserved for matching the switch ID.
 */
#define ICE_NUM_WORDS_RECIPE 5

/* Max recipes that can be chained, not including the last one, which combines
 * intermediate results.
 */
#define ICE_MAX_CHAIN_RECIPE 5

/* Total max recipes in chain recipe (including intermediate results) */
#define ICE_MAX_CHAIN_RECIPE_RES (ICE_MAX_CHAIN_RECIPE + 1)

/* A recipe can have max 5 words, and 5 recipes can be chained together (using
 * the 6th one, which would contain only result indexes). So maximum words that
 * can be programmed for lookup is 5 * 5 (not including intermediate results).
 */
#define ICE_MAX_CHAIN_WORDS (ICE_NUM_WORDS_RECIPE * ICE_MAX_CHAIN_RECIPE)

/* Field vector index corresponding to chaining */
#define ICE_CHAIN_FV_INDEX_START 47

enum ice_protocol_type {
	ICE_MAC_OFOS = 0,
	ICE_MAC_IL,
	ICE_ETYPE_OL,
	ICE_ETYPE_IL,
	ICE_VLAN_OFOS,
	ICE_IPV4_OFOS,
	ICE_IPV4_IL,
	ICE_IPV6_OFOS,
	ICE_IPV6_IL,
	ICE_TCP_IL,
	ICE_UDP_OF,
	ICE_UDP_ILOS,
	ICE_VXLAN,
	ICE_GENEVE,
	ICE_NVGRE,
	ICE_GTP,
	ICE_GTP_NO_PAY,
	ICE_PFCP,
	ICE_PPPOE,
	ICE_L2TPV3,
	ICE_VLAN_EX,
	ICE_VLAN_IN,
	ICE_HW_METADATA,
	ICE_VXLAN_GPE,
	ICE_SCTP_IL,
	ICE_PROTOCOL_LAST
};

enum ice_sw_tunnel_type {
	ICE_NON_TUN = 0,
	ICE_SW_TUN_AND_NON_TUN,
	ICE_SW_TUN_VXLAN,
	ICE_SW_TUN_GENEVE,
	ICE_SW_TUN_NVGRE,
	ICE_SW_TUN_GTPU,
	ICE_SW_TUN_GTPC,
	ICE_SW_TUN_PFCP,
	ICE_ALL_TUNNELS /* All tunnel types including NVGRE */
};

/* Decoders for ice_prot_id:
 * - F: First
 * - I: Inner
 * - L: Last
 * - O: Outer
 * - S: Single
 */
enum ice_prot_id {
	ICE_PROT_ID_INVAL	= 0,
	ICE_PROT_MAC_OF_OR_S	= 1,
	ICE_PROT_MAC_IL		= 4,
	ICE_PROT_ETYPE_OL	= 9,
	ICE_PROT_ETYPE_IL	= 10,
	ICE_PROT_IPV4_OF_OR_S	= 32,
	ICE_PROT_IPV4_IL	= 33,
	ICE_PROT_IPV6_OF_OR_S	= 40,
	ICE_PROT_IPV6_IL	= 41,
	ICE_PROT_TCP_IL		= 49,
	ICE_PROT_UDP_OF		= 52,
	ICE_PROT_UDP_IL_OR_S	= 53,
	ICE_PROT_GRE_OF		= 64,
	ICE_PROT_ESP_F		= 88,
	ICE_PROT_ESP_2		= 89,
	ICE_PROT_SCTP_IL	= 96,
	ICE_PROT_ICMP_IL	= 98,
	ICE_PROT_ICMPV6_IL	= 100,
	ICE_PROT_PPPOE		= 103,
	ICE_PROT_L2TPV3		= 104,
	ICE_PROT_ARP_OF		= 118,
	ICE_PROT_META_ID	= 255, /* when offset == metadata */
	ICE_PROT_INVALID	= 255  /* when offset == ICE_FV_OFFSET_INVAL */
};

#define ICE_VNI_OFFSET		12 /* offset of VNI from ICE_PROT_UDP_OF */

#define ICE_MAC_OFOS_HW		1
#define ICE_MAC_IL_HW		4
#define ICE_ETYPE_OL_HW		9
#define ICE_ETYPE_IL_HW		10
#define ICE_VLAN_OF_HW		16
#define ICE_VLAN_OL_HW		17
#define ICE_IPV4_OFOS_HW	32
#define ICE_IPV4_IL_HW		33
#define ICE_IPV6_OFOS_HW	40
#define ICE_IPV6_IL_HW		41
#define ICE_TCP_IL_HW		49
#define ICE_UDP_ILOS_HW		53
#define ICE_GRE_OF_HW		64
#define ICE_PPPOE_HW		103
#define ICE_L2TPV3_HW		104

#define ICE_UDP_OF_HW	52 /* UDP Tunnels */


#define ICE_TUN_FLAG_FV_IND 2

/* Mapping of software defined protocol ID to hardware defined protocol ID */
struct ice_protocol_entry {
	enum ice_protocol_type type;
	u8 protocol_id;
};

struct ice_ether_hdr {
	u8 dst_addr[ETH_ALEN];
	u8 src_addr[ETH_ALEN];
};

struct ice_ethtype_hdr {
	__be16 ethtype_id;
};

struct ice_ether_vlan_hdr {
	u8 dst_addr[ETH_ALEN];
	u8 src_addr[ETH_ALEN];
	__be32 vlan_id;
};

struct ice_vlan_hdr {
	__be16 type;
	__be16 vlan;
};

struct ice_ipv4_hdr {
	u8 version;
	u8 tos;
	__be16 total_length;
	__be16 id;
	__be16 frag_off;
	u8 time_to_live;
	u8 protocol;
	__be16 check;
	__be32 src_addr;
	__be32 dst_addr;
};

struct ice_ipv6_hdr {
	__be32 be_ver_tc_flow;
	__be16 payload_len;
	u8 next_hdr;
	u8 hop_limit;
	u8 src_addr[ICE_IPV6_ADDR_LENGTH];
	u8 dst_addr[ICE_IPV6_ADDR_LENGTH];
};

struct ice_sctp_hdr {
	__be16 src_port;
	__be16 dst_port;
	__be32 verification_tag;
	__be32 check;
};

struct ice_l4_hdr {
	__be16 src_port;
	__be16 dst_port;
	__be16 len;
	__be16 check;
};

struct ice_udp_tnl_hdr {
	__be16 field;
	__be16 proto_type;
	__be32 vni;     /* only use lower 24-bits */
};

struct ice_udp_gtp_hdr {
	u8 flags;
	u8 msg_type;
	__be16 rsrvd_len;
	__be32 teid;
	__be16 rsrvd_seq_nbr;
	u8 rsrvd_n_pdu_nbr;
	u8 rsrvd_next_ext;
	u8 rsvrd_ext_len;
	u8 pdu_type;
	u8 qfi;
	u8 rsvrd;
};

struct ice_pfcp_hdr {
	u8 flags;
	u8 msg_type;
	__be16 length;
	__be64 seid;
	__be32 seq;
	u8 spare;
} __packed __aligned(__alignof__(u16));

struct ice_pppoe_hdr {
	u8 rsrvd_ver_type;
	u8 rsrvd_code;
	__be16 session_id;
	__be16 length;
	__be16 ppp_prot_id; /* control and data only */
};

struct ice_l2tpv3_sess_hdr {
	__be32 session_id;
	__be64 cookie;
};

struct ice_nvgre_hdr {
	__be16 flags;
	__be16 protocol;
	__be32 tni_flow;
};

/* Metadata information
 *
 * Not all MDIDs can be used by switch block. It depends on package version.
 *
 * MDID 16 (Rx offset)
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  A  |   B     |   Reserved    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * A = Source port where the transaction came from (3b).
 *
 * B = Destination TC of the packet. The TC is relative to a port (5b).
 *
 * MDID 17
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      PTYPE        | Reserved  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * PTYPE = Encodes the packet type (10b).
 *
 * MDID 18
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Packet length             | R |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Packet length = Length of the packet in bytes
 *		   (packet always carriers CRC) (14b).
 * R = Reserved (2b).
 *
 * MDID 19
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   Source VSI      | Reserved  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Source VSI = Source VSI of packet loopbacked in switch (for egress) (10b).
 */
#define ICE_MDID_SOURCE_VSI_MASK GENMASK(9, 0)

/*
 * MDID 20
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |A|B|C|D|E|F|R|R|G|H|I|J|K|L|M|N|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * A = DSI - set for DSI RX pkts.
 * B = ipsec_decrypted - invalid on NIC.
 * C = marker - this is a marker packet.
 * D = from_network - for TX sets to 0
 *		      for RX:
 *		        * 1 - packet is from external link
 *		        * 0 - packet source is from internal
 * E = source_interface_is_rx - reflect the physical interface from where the
 *				packet was received:
 *				* 1 - Rx
 *				* 0 - Tx
 * F = from_mng - The bit signals that the packet's origin is the management.
 * G = ucast - Outer L2 MAC address is unicast.
 * H = mcast - Outer L2 MAC address is multicast.
 * I = bcast - Outer L2 MAC address is broadcast.
 * J = second_outer_mac_present - 2 outer MAC headers are present in the packet.
 * K = STAG or BVLAN - Outer L2 header has STAG (ethernet type 0x88a8) or
 *		       BVLAN (ethernet type 0x88a8).
 * L = ITAG - Outer L2 header has ITAG *ethernet type 0x88e7)
 * M = EVLAN (0x8100) - Outer L2 header has EVLAN (ethernet type 0x8100)
 * N = EVLAN (0x9100) - Outer L2 header has EVLAN (ethernet type 0x9100)
 */
#define ICE_PKT_FROM_NETWORK	BIT(3)
#define ICE_PKT_VLAN_STAG	BIT(12)
#define ICE_PKT_VLAN_ITAG	BIT(13)
#define ICE_PKT_VLAN_EVLAN	(BIT(14) | BIT(15))
#define ICE_PKT_VLAN_MASK	(ICE_PKT_VLAN_STAG | ICE_PKT_VLAN_ITAG | \
				ICE_PKT_VLAN_EVLAN)
/* MDID 21
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |A|B|C|D|E|F|G|H|I|J|R|R|K|L|M|N|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * A = VLAN (0x8100) - Outer L2 header has VLAN (ethernet type 0x8100)
 * B = NSHoE - Outer L2 header has NSH (ethernet type 0x894f)
 * C = MPLS (0x8847) - There is at least 1 MPLS tag in the outer header
 *		       (ethernet type 0x8847)
 * D = MPLS (0x8848) - There is at least 1 MPLS tag in the outer header
 *		       (ethernet type 0x8848)
 * E = multi MPLS - There is more than a single MPLS tag in the outer header
 * F = inner MPLS - There is inner MPLS tag in the packet
 * G = tunneled MAC - Set if the packet includes a tunneled MAC
 * H = tunneled VLAN - Same as VLAN, but for a tunneled header
 * I = pkt_is_frag - Packet is fragmented (ipv4 or ipv6)
 * J = ipv6_ext - The packet has routing or destination ipv6 extension in inner
 *		  or outer ipv6 headers
 * K = RoCE - UDP packet detected as RoCEv2
 * L = UDP_XSUM_0 - Set to 1 if L4 checksum is 0 in a UDP packet
 * M = ESP - This is a ESP packet
 * N = NAT_ESP - This is a ESP packet encapsulated in UDP NAT
 */
#define ICE_PKT_TUNNEL_MAC	BIT(6)
#define ICE_PKT_TUNNEL_VLAN	BIT(7)
#define ICE_PKT_TUNNEL_MASK	(ICE_PKT_TUNNEL_MAC | ICE_PKT_TUNNEL_VLAN)

/* MDID 22
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |A|B|C|D|E|F|  G  |H|I|J| K |L|M|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * A = fin - fin flag in tcp header
 * B = sync - sync flag in tcp header
 * C = rst - rst flag in tcp header
 * D = psh - psh flag in tcp header
 * E = ack - ack flag in tcp header
 * F = urg - urg flag in tcp header
 * G = tunnel type (3b) - Flags used to decode tunnel type:
 *			  * b000 - not a VXLAN/Geneve/GRE tunnel
 *			  * b001 - VXLAN-GPE
 *			  * b010 - VXLAN (non-GPE)
 *			  * b011 - Geneve
 *			  * b100 - GRE (no key, no xsum)
 *			  * b101 - GREK (key, no xsum)
 *			  * b110 - GREC (no key, xsum)
 *			  * b111 - GREKC (key, xsum)
 * H = UDP_GRE - Packet is UDP (VXLAN or VLAN_GPE or Geneve or MPLSoUDP or GRE)
 *		 tunnel
 * I = OAM - VXLAN/Geneve/tunneled NSH packet with the OAM bit set
 * J = tunneled NSH - Packet has NSHoGRE or NSHoUDP
 * K = switch (2b) - Direction on switch
 *		     * b00 - normal
 *		     * b01 - TX force only LAN
 *		     * b10 - TX disable LAN
 *		     * b11 - direct to VSI
 * L = swpe - Represents SWPE bit in TX command
 * M = sw_cmd - Switch command
 *
 * MDID 23
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |A|B|C|D|        R        |E|F|R|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * A = MAC error - Produced by MAC according to L2 error conditions
 * B = PPRS no offload - FIFO overflow in PPRS or any problematic condition in
 *			 PPRS ANA
 * C = abort - Set when malicious packet is detected
 * D = partial analysis - ANA's analysing got cut in the middle
 *			 (header > 504B etc.)
 * E = FLM - Flow director hit indication
 * F = FDLONG - Flow direector long bucket indication
 *
 */
#define ICE_MDID_SIZE 2
#define ICE_META_DATA_ID_HW 255

enum ice_hw_metadata_id {
	ICE_SOURCE_PORT_MDID = 16,
	ICE_PTYPE_MDID = 17,
	ICE_PACKET_LENGTH_MDID = 18,
	ICE_SOURCE_VSI_MDID = 19,
	ICE_PKT_VLAN_MDID = 20,
	ICE_PKT_TUNNEL_MDID = 21,
	ICE_PKT_TCP_MDID = 22,
	ICE_PKT_ERROR_MDID = 23,
};

enum ice_hw_metadata_offset {
	ICE_SOURCE_PORT_MDID_OFFSET = ICE_MDID_SIZE * ICE_SOURCE_PORT_MDID,
	ICE_PTYPE_MDID_OFFSET = ICE_MDID_SIZE * ICE_PTYPE_MDID,
	ICE_PACKET_LENGTH_MDID_OFFSET = ICE_MDID_SIZE * ICE_PACKET_LENGTH_MDID,
	ICE_SOURCE_VSI_MDID_OFFSET = ICE_MDID_SIZE * ICE_SOURCE_VSI_MDID,
	ICE_PKT_VLAN_MDID_OFFSET = ICE_MDID_SIZE * ICE_PKT_VLAN_MDID,
	ICE_PKT_TUNNEL_MDID_OFFSET = ICE_MDID_SIZE * ICE_PKT_TUNNEL_MDID,
	ICE_PKT_TCP_MDID_OFFSET = ICE_MDID_SIZE * ICE_PKT_TCP_MDID,
	ICE_PKT_ERROR_MDID_OFFSET = ICE_MDID_SIZE * ICE_PKT_ERROR_MDID,
};

enum ice_pkt_flags {
	ICE_PKT_FLAGS_MDID20 = 0,
	ICE_PKT_FLAGS_MDID21 = 1,
	ICE_PKT_FLAGS_MDID22 = 2,
	ICE_PKT_FLAGS_MDID23 = 3,
};

struct ice_hw_metadata {
	__be16 source_port;
	__be16 ptype;
	__be16 packet_length;
	__be16 source_vsi;
	__be16 flags[4];
};

union ice_prot_hdr {
	struct ice_ether_hdr eth_hdr;
	struct ice_ethtype_hdr ethertype;
	struct ice_vlan_hdr vlan_hdr;
	struct ice_ipv4_hdr ipv4_hdr;
	struct ice_ipv6_hdr ipv6_hdr;
	struct ice_l4_hdr l4_hdr;
	struct ice_sctp_hdr sctp_hdr;
	struct ice_udp_tnl_hdr tnl_hdr;
	struct ice_nvgre_hdr nvgre_hdr;
	struct ice_udp_gtp_hdr gtp_hdr;
	struct ice_pfcp_hdr pfcp_hdr;
	struct ice_pppoe_hdr pppoe_hdr;
	struct ice_l2tpv3_sess_hdr l2tpv3_sess_hdr;
	struct ice_hw_metadata metadata;
};

/* This is mapping table entry that maps every word within a given protocol
 * structure to the real byte offset as per the specification of that
 * protocol header.
 * for e.g. dst address is 3 words in ethertype header and corresponding bytes
 * are 0, 2, 3 in the actual packet header and src address is at 4, 6, 8
 */
struct ice_prot_ext_tbl_entry {
	enum ice_protocol_type prot_type;
	/* Byte offset into header of given protocol type */
	u8 offs[sizeof(union ice_prot_hdr)];
};

/* Extractions to be looked up for a given recipe */
struct ice_prot_lkup_ext {
	u8 n_val_words;
	/* create a buffer to hold max words per recipe */
	u16 field_mask[ICE_MAX_CHAIN_WORDS];

	struct ice_fv_word fv_words[ICE_MAX_CHAIN_WORDS];
};

#endif /* _ICE_PROTOCOL_TYPE_H_ */
