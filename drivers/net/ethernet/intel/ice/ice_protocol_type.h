/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_PROTOCOL_TYPE_H_
#define _ICE_PROTOCOL_TYPE_H_
#define ICE_IPV6_ADDR_LENGTH 16

/* Each recipe can match up to 5 different fields. Fields to match can be meta-
 * data, values extracted from packet headers, or results from other recipes.
 * One of the 5 fields is reserved for matching the switch ID. So, up to 4
 * recipes can provide intermediate results to another one through chaining,
 * e.g. recipes 0, 1, 2, and 3 can provide intermediate results to recipe 4.
 */
#define ICE_NUM_WORDS_RECIPE 4

/* Max recipes that can be chained */
#define ICE_MAX_CHAIN_RECIPE 5

/* 1 word reserved for switch ID from allowed 5 words.
 * So a recipe can have max 4 words. And you can chain 5 such recipes
 * together. So maximum words that can be programmed for look up is 5 * 4.
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
	ICE_PPPOE,
	ICE_L2TPV3,
	ICE_VLAN_EX,
	ICE_VLAN_IN,
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
#define ICE_META_DATA_ID_HW 255 /* this is used for tunnel and VLAN type */

#define ICE_MDID_SIZE 2

#define ICE_TUN_FLAG_MDID 21
#define ICE_TUN_FLAG_MDID_OFF (ICE_MDID_SIZE * ICE_TUN_FLAG_MDID)
#define ICE_TUN_FLAG_MASK 0xFF

#define ICE_VLAN_FLAG_MDID 20
#define ICE_VLAN_FLAG_MDID_OFF (ICE_MDID_SIZE * ICE_VLAN_FLAG_MDID)
#define ICE_PKT_FLAGS_0_TO_15_VLAN_FLAGS_MASK 0xD000

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
	struct ice_pppoe_hdr pppoe_hdr;
	struct ice_l2tpv3_sess_hdr l2tpv3_sess_hdr;
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
	u16 prot_type;
	u8 n_val_words;
	/* create a buffer to hold max words per recipe */
	u16 field_off[ICE_MAX_CHAIN_WORDS];
	u16 field_mask[ICE_MAX_CHAIN_WORDS];

	struct ice_fv_word fv_words[ICE_MAX_CHAIN_WORDS];

	/* Indicate field offsets that have field vector indices assigned */
	DECLARE_BITMAP(done, ICE_MAX_CHAIN_WORDS);
};

struct ice_pref_recipe_group {
	u8 n_val_pairs;		/* Number of valid pairs */
	struct ice_fv_word pairs[ICE_NUM_WORDS_RECIPE];
	u16 mask[ICE_NUM_WORDS_RECIPE];
};

struct ice_recp_grp_entry {
	struct list_head l_entry;

#define ICE_INVAL_CHAIN_IND 0xFF
	u16 rid;
	u8 chain_idx;
	u16 fv_idx[ICE_NUM_WORDS_RECIPE];
	u16 fv_mask[ICE_NUM_WORDS_RECIPE];
	struct ice_pref_recipe_group r_group;
};
#endif /* _ICE_PROTOCOL_TYPE_H_ */
