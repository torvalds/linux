/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2013-2015 Freescale Semiconductor Inc.
 */
#ifndef __FSL_DPKG_H_
#define __FSL_DPKG_H_

#include <linux/types.h>

/* Data Path Key Generator API
 * Contains initialization APIs and runtime APIs for the Key Generator
 */

/** Key Generator properties */

/**
 * DPKG_NUM_OF_MASKS - Number of masks per key extraction
 */
#define DPKG_NUM_OF_MASKS		4

/**
 * DPKG_MAX_NUM_OF_EXTRACTS - Number of extractions per key profile
 */
#define DPKG_MAX_NUM_OF_EXTRACTS	10

/**
 * enum dpkg_extract_from_hdr_type - Selecting extraction by header types
 * @DPKG_FROM_HDR: Extract selected bytes from header, by offset
 * @DPKG_FROM_FIELD: Extract selected bytes from header, by offset from field
 * @DPKG_FULL_FIELD: Extract a full field
 */
enum dpkg_extract_from_hdr_type {
	DPKG_FROM_HDR = 0,
	DPKG_FROM_FIELD = 1,
	DPKG_FULL_FIELD = 2
};

/**
 * enum dpkg_extract_type - Enumeration for selecting extraction type
 * @DPKG_EXTRACT_FROM_HDR: Extract from the header
 * @DPKG_EXTRACT_FROM_DATA: Extract from data not in specific header
 * @DPKG_EXTRACT_FROM_PARSE: Extract from parser-result;
 *	e.g. can be used to extract header existence;
 *	please refer to 'Parse Result definition' section in the parser BG
 */
enum dpkg_extract_type {
	DPKG_EXTRACT_FROM_HDR = 0,
	DPKG_EXTRACT_FROM_DATA = 1,
	DPKG_EXTRACT_FROM_PARSE = 3
};

/**
 * struct dpkg_mask - A structure for defining a single extraction mask
 * @mask: Byte mask for the extracted content
 * @offset: Offset within the extracted content
 */
struct dpkg_mask {
	u8 mask;
	u8 offset;
};

/* Protocol fields */

/* Ethernet fields */
#define NH_FLD_ETH_DA				BIT(0)
#define NH_FLD_ETH_SA				BIT(1)
#define NH_FLD_ETH_LENGTH			BIT(2)
#define NH_FLD_ETH_TYPE				BIT(3)
#define NH_FLD_ETH_FINAL_CKSUM			BIT(4)
#define NH_FLD_ETH_PADDING			BIT(5)
#define NH_FLD_ETH_ALL_FIELDS			(BIT(6) - 1)

/* VLAN fields */
#define NH_FLD_VLAN_VPRI			BIT(0)
#define NH_FLD_VLAN_CFI				BIT(1)
#define NH_FLD_VLAN_VID				BIT(2)
#define NH_FLD_VLAN_LENGTH			BIT(3)
#define NH_FLD_VLAN_TYPE			BIT(4)
#define NH_FLD_VLAN_ALL_FIELDS			(BIT(5) - 1)

#define NH_FLD_VLAN_TCI				(NH_FLD_VLAN_VPRI | \
						 NH_FLD_VLAN_CFI | \
						 NH_FLD_VLAN_VID)

/* IP (generic) fields */
#define NH_FLD_IP_VER				BIT(0)
#define NH_FLD_IP_DSCP				BIT(2)
#define NH_FLD_IP_ECN				BIT(3)
#define NH_FLD_IP_PROTO				BIT(4)
#define NH_FLD_IP_SRC				BIT(5)
#define NH_FLD_IP_DST				BIT(6)
#define NH_FLD_IP_TOS_TC			BIT(7)
#define NH_FLD_IP_ID				BIT(8)
#define NH_FLD_IP_ALL_FIELDS			(BIT(9) - 1)

/* IPV4 fields */
#define NH_FLD_IPV4_VER				BIT(0)
#define NH_FLD_IPV4_HDR_LEN			BIT(1)
#define NH_FLD_IPV4_TOS				BIT(2)
#define NH_FLD_IPV4_TOTAL_LEN			BIT(3)
#define NH_FLD_IPV4_ID				BIT(4)
#define NH_FLD_IPV4_FLAG_D			BIT(5)
#define NH_FLD_IPV4_FLAG_M			BIT(6)
#define NH_FLD_IPV4_OFFSET			BIT(7)
#define NH_FLD_IPV4_TTL				BIT(8)
#define NH_FLD_IPV4_PROTO			BIT(9)
#define NH_FLD_IPV4_CKSUM			BIT(10)
#define NH_FLD_IPV4_SRC_IP			BIT(11)
#define NH_FLD_IPV4_DST_IP			BIT(12)
#define NH_FLD_IPV4_OPTS			BIT(13)
#define NH_FLD_IPV4_OPTS_COUNT			BIT(14)
#define NH_FLD_IPV4_ALL_FIELDS			(BIT(15) - 1)

/* IPV6 fields */
#define NH_FLD_IPV6_VER				BIT(0)
#define NH_FLD_IPV6_TC				BIT(1)
#define NH_FLD_IPV6_SRC_IP			BIT(2)
#define NH_FLD_IPV6_DST_IP			BIT(3)
#define NH_FLD_IPV6_NEXT_HDR			BIT(4)
#define NH_FLD_IPV6_FL				BIT(5)
#define NH_FLD_IPV6_HOP_LIMIT			BIT(6)
#define NH_FLD_IPV6_ID				BIT(7)
#define NH_FLD_IPV6_ALL_FIELDS			(BIT(8) - 1)

/* ICMP fields */
#define NH_FLD_ICMP_TYPE			BIT(0)
#define NH_FLD_ICMP_CODE			BIT(1)
#define NH_FLD_ICMP_CKSUM			BIT(2)
#define NH_FLD_ICMP_ID				BIT(3)
#define NH_FLD_ICMP_SQ_NUM			BIT(4)
#define NH_FLD_ICMP_ALL_FIELDS			(BIT(5) - 1)

/* IGMP fields */
#define NH_FLD_IGMP_VERSION			BIT(0)
#define NH_FLD_IGMP_TYPE			BIT(1)
#define NH_FLD_IGMP_CKSUM			BIT(2)
#define NH_FLD_IGMP_DATA			BIT(3)
#define NH_FLD_IGMP_ALL_FIELDS			(BIT(4) - 1)

/* TCP fields */
#define NH_FLD_TCP_PORT_SRC			BIT(0)
#define NH_FLD_TCP_PORT_DST			BIT(1)
#define NH_FLD_TCP_SEQ				BIT(2)
#define NH_FLD_TCP_ACK				BIT(3)
#define NH_FLD_TCP_OFFSET			BIT(4)
#define NH_FLD_TCP_FLAGS			BIT(5)
#define NH_FLD_TCP_WINDOW			BIT(6)
#define NH_FLD_TCP_CKSUM			BIT(7)
#define NH_FLD_TCP_URGPTR			BIT(8)
#define NH_FLD_TCP_OPTS				BIT(9)
#define NH_FLD_TCP_OPTS_COUNT			BIT(10)
#define NH_FLD_TCP_ALL_FIELDS			(BIT(11) - 1)

/* UDP fields */
#define NH_FLD_UDP_PORT_SRC			BIT(0)
#define NH_FLD_UDP_PORT_DST			BIT(1)
#define NH_FLD_UDP_LEN				BIT(2)
#define NH_FLD_UDP_CKSUM			BIT(3)
#define NH_FLD_UDP_ALL_FIELDS			(BIT(4) - 1)

/* UDP-lite fields */
#define NH_FLD_UDP_LITE_PORT_SRC		BIT(0)
#define NH_FLD_UDP_LITE_PORT_DST		BIT(1)
#define NH_FLD_UDP_LITE_ALL_FIELDS		(BIT(2) - 1)

/* UDP-encap-ESP fields */
#define NH_FLD_UDP_ENC_ESP_PORT_SRC		BIT(0)
#define NH_FLD_UDP_ENC_ESP_PORT_DST		BIT(1)
#define NH_FLD_UDP_ENC_ESP_LEN			BIT(2)
#define NH_FLD_UDP_ENC_ESP_CKSUM		BIT(3)
#define NH_FLD_UDP_ENC_ESP_SPI			BIT(4)
#define NH_FLD_UDP_ENC_ESP_SEQUENCE_NUM		BIT(5)
#define NH_FLD_UDP_ENC_ESP_ALL_FIELDS		(BIT(6) - 1)

/* SCTP fields */
#define NH_FLD_SCTP_PORT_SRC			BIT(0)
#define NH_FLD_SCTP_PORT_DST			BIT(1)
#define NH_FLD_SCTP_VER_TAG			BIT(2)
#define NH_FLD_SCTP_CKSUM			BIT(3)
#define NH_FLD_SCTP_ALL_FIELDS			(BIT(4) - 1)

/* DCCP fields */
#define NH_FLD_DCCP_PORT_SRC			BIT(0)
#define NH_FLD_DCCP_PORT_DST			BIT(1)
#define NH_FLD_DCCP_ALL_FIELDS			(BIT(2) - 1)

/* IPHC fields */
#define NH_FLD_IPHC_CID				BIT(0)
#define NH_FLD_IPHC_CID_TYPE			BIT(1)
#define NH_FLD_IPHC_HCINDEX			BIT(2)
#define NH_FLD_IPHC_GEN				BIT(3)
#define NH_FLD_IPHC_D_BIT			BIT(4)
#define NH_FLD_IPHC_ALL_FIELDS			(BIT(5) - 1)

/* SCTP fields */
#define NH_FLD_SCTP_CHUNK_DATA_TYPE		BIT(0)
#define NH_FLD_SCTP_CHUNK_DATA_FLAGS		BIT(1)
#define NH_FLD_SCTP_CHUNK_DATA_LENGTH		BIT(2)
#define NH_FLD_SCTP_CHUNK_DATA_TSN		BIT(3)
#define NH_FLD_SCTP_CHUNK_DATA_STREAM_ID	BIT(4)
#define NH_FLD_SCTP_CHUNK_DATA_STREAM_SQN	BIT(5)
#define NH_FLD_SCTP_CHUNK_DATA_PAYLOAD_PID	BIT(6)
#define NH_FLD_SCTP_CHUNK_DATA_UNORDERED	BIT(7)
#define NH_FLD_SCTP_CHUNK_DATA_BEGGINING	BIT(8)
#define NH_FLD_SCTP_CHUNK_DATA_END		BIT(9)
#define NH_FLD_SCTP_CHUNK_DATA_ALL_FIELDS	(BIT(10) - 1)

/* L2TPV2 fields */
#define NH_FLD_L2TPV2_TYPE_BIT			BIT(0)
#define NH_FLD_L2TPV2_LENGTH_BIT		BIT(1)
#define NH_FLD_L2TPV2_SEQUENCE_BIT		BIT(2)
#define NH_FLD_L2TPV2_OFFSET_BIT		BIT(3)
#define NH_FLD_L2TPV2_PRIORITY_BIT		BIT(4)
#define NH_FLD_L2TPV2_VERSION			BIT(5)
#define NH_FLD_L2TPV2_LEN			BIT(6)
#define NH_FLD_L2TPV2_TUNNEL_ID			BIT(7)
#define NH_FLD_L2TPV2_SESSION_ID		BIT(8)
#define NH_FLD_L2TPV2_NS			BIT(9)
#define NH_FLD_L2TPV2_NR			BIT(10)
#define NH_FLD_L2TPV2_OFFSET_SIZE		BIT(11)
#define NH_FLD_L2TPV2_FIRST_BYTE		BIT(12)
#define NH_FLD_L2TPV2_ALL_FIELDS		(BIT(13) - 1)

/* L2TPV3 fields */
#define NH_FLD_L2TPV3_CTRL_TYPE_BIT		BIT(0)
#define NH_FLD_L2TPV3_CTRL_LENGTH_BIT		BIT(1)
#define NH_FLD_L2TPV3_CTRL_SEQUENCE_BIT		BIT(2)
#define NH_FLD_L2TPV3_CTRL_VERSION		BIT(3)
#define NH_FLD_L2TPV3_CTRL_LENGTH		BIT(4)
#define NH_FLD_L2TPV3_CTRL_CONTROL		BIT(5)
#define NH_FLD_L2TPV3_CTRL_SENT			BIT(6)
#define NH_FLD_L2TPV3_CTRL_RECV			BIT(7)
#define NH_FLD_L2TPV3_CTRL_FIRST_BYTE		BIT(8)
#define NH_FLD_L2TPV3_CTRL_ALL_FIELDS		(BIT(9) - 1)

#define NH_FLD_L2TPV3_SESS_TYPE_BIT		BIT(0)
#define NH_FLD_L2TPV3_SESS_VERSION		BIT(1)
#define NH_FLD_L2TPV3_SESS_ID			BIT(2)
#define NH_FLD_L2TPV3_SESS_COOKIE		BIT(3)
#define NH_FLD_L2TPV3_SESS_ALL_FIELDS		(BIT(4) - 1)

/* PPP fields */
#define NH_FLD_PPP_PID				BIT(0)
#define NH_FLD_PPP_COMPRESSED			BIT(1)
#define NH_FLD_PPP_ALL_FIELDS			(BIT(2) - 1)

/* PPPoE fields */
#define NH_FLD_PPPOE_VER			BIT(0)
#define NH_FLD_PPPOE_TYPE			BIT(1)
#define NH_FLD_PPPOE_CODE			BIT(2)
#define NH_FLD_PPPOE_SID			BIT(3)
#define NH_FLD_PPPOE_LEN			BIT(4)
#define NH_FLD_PPPOE_SESSION			BIT(5)
#define NH_FLD_PPPOE_PID			BIT(6)
#define NH_FLD_PPPOE_ALL_FIELDS			(BIT(7) - 1)

/* PPP-Mux fields */
#define NH_FLD_PPPMUX_PID			BIT(0)
#define NH_FLD_PPPMUX_CKSUM			BIT(1)
#define NH_FLD_PPPMUX_COMPRESSED		BIT(2)
#define NH_FLD_PPPMUX_ALL_FIELDS		(BIT(3) - 1)

/* PPP-Mux sub-frame fields */
#define NH_FLD_PPPMUX_SUBFRM_PFF		BIT(0)
#define NH_FLD_PPPMUX_SUBFRM_LXT		BIT(1)
#define NH_FLD_PPPMUX_SUBFRM_LEN		BIT(2)
#define NH_FLD_PPPMUX_SUBFRM_PID		BIT(3)
#define NH_FLD_PPPMUX_SUBFRM_USE_PID		BIT(4)
#define NH_FLD_PPPMUX_SUBFRM_ALL_FIELDS		(BIT(5) - 1)

/* LLC fields */
#define NH_FLD_LLC_DSAP				BIT(0)
#define NH_FLD_LLC_SSAP				BIT(1)
#define NH_FLD_LLC_CTRL				BIT(2)
#define NH_FLD_LLC_ALL_FIELDS			(BIT(3) - 1)

/* NLPID fields */
#define NH_FLD_NLPID_NLPID			BIT(0)
#define NH_FLD_NLPID_ALL_FIELDS			(BIT(1) - 1)

/* SNAP fields */
#define NH_FLD_SNAP_OUI				BIT(0)
#define NH_FLD_SNAP_PID				BIT(1)
#define NH_FLD_SNAP_ALL_FIELDS			(BIT(2) - 1)

/* LLC SNAP fields */
#define NH_FLD_LLC_SNAP_TYPE			BIT(0)
#define NH_FLD_LLC_SNAP_ALL_FIELDS		(BIT(1) - 1)

/* ARP fields */
#define NH_FLD_ARP_HTYPE			BIT(0)
#define NH_FLD_ARP_PTYPE			BIT(1)
#define NH_FLD_ARP_HLEN				BIT(2)
#define NH_FLD_ARP_PLEN				BIT(3)
#define NH_FLD_ARP_OPER				BIT(4)
#define NH_FLD_ARP_SHA				BIT(5)
#define NH_FLD_ARP_SPA				BIT(6)
#define NH_FLD_ARP_THA				BIT(7)
#define NH_FLD_ARP_TPA				BIT(8)
#define NH_FLD_ARP_ALL_FIELDS			(BIT(9) - 1)

/* RFC2684 fields */
#define NH_FLD_RFC2684_LLC			BIT(0)
#define NH_FLD_RFC2684_NLPID			BIT(1)
#define NH_FLD_RFC2684_OUI			BIT(2)
#define NH_FLD_RFC2684_PID			BIT(3)
#define NH_FLD_RFC2684_VPN_OUI			BIT(4)
#define NH_FLD_RFC2684_VPN_IDX			BIT(5)
#define NH_FLD_RFC2684_ALL_FIELDS		(BIT(6) - 1)

/* User defined fields */
#define NH_FLD_USER_DEFINED_SRCPORT		BIT(0)
#define NH_FLD_USER_DEFINED_PCDID		BIT(1)
#define NH_FLD_USER_DEFINED_ALL_FIELDS		(BIT(2) - 1)

/* Payload fields */
#define NH_FLD_PAYLOAD_BUFFER			BIT(0)
#define NH_FLD_PAYLOAD_SIZE			BIT(1)
#define NH_FLD_MAX_FRM_SIZE			BIT(2)
#define NH_FLD_MIN_FRM_SIZE			BIT(3)
#define NH_FLD_PAYLOAD_TYPE			BIT(4)
#define NH_FLD_FRAME_SIZE			BIT(5)
#define NH_FLD_PAYLOAD_ALL_FIELDS		(BIT(6) - 1)

/* GRE fields */
#define NH_FLD_GRE_TYPE				BIT(0)
#define NH_FLD_GRE_ALL_FIELDS			(BIT(1) - 1)

/* MINENCAP fields */
#define NH_FLD_MINENCAP_SRC_IP			BIT(0)
#define NH_FLD_MINENCAP_DST_IP			BIT(1)
#define NH_FLD_MINENCAP_TYPE			BIT(2)
#define NH_FLD_MINENCAP_ALL_FIELDS		(BIT(3) - 1)

/* IPSEC AH fields */
#define NH_FLD_IPSEC_AH_SPI			BIT(0)
#define NH_FLD_IPSEC_AH_NH			BIT(1)
#define NH_FLD_IPSEC_AH_ALL_FIELDS		(BIT(2) - 1)

/* IPSEC ESP fields */
#define NH_FLD_IPSEC_ESP_SPI			BIT(0)
#define NH_FLD_IPSEC_ESP_SEQUENCE_NUM		BIT(1)
#define NH_FLD_IPSEC_ESP_ALL_FIELDS		(BIT(2) - 1)

/* MPLS fields */
#define NH_FLD_MPLS_LABEL_STACK			BIT(0)
#define NH_FLD_MPLS_LABEL_STACK_ALL_FIELDS	(BIT(1) - 1)

/* MACSEC fields */
#define NH_FLD_MACSEC_SECTAG			BIT(0)
#define NH_FLD_MACSEC_ALL_FIELDS		(BIT(1) - 1)

/* GTP fields */
#define NH_FLD_GTP_TEID				BIT(0)

/* Supported protocols */
enum net_prot {
	NET_PROT_NONE = 0,
	NET_PROT_PAYLOAD,
	NET_PROT_ETH,
	NET_PROT_VLAN,
	NET_PROT_IPV4,
	NET_PROT_IPV6,
	NET_PROT_IP,
	NET_PROT_TCP,
	NET_PROT_UDP,
	NET_PROT_UDP_LITE,
	NET_PROT_IPHC,
	NET_PROT_SCTP,
	NET_PROT_SCTP_CHUNK_DATA,
	NET_PROT_PPPOE,
	NET_PROT_PPP,
	NET_PROT_PPPMUX,
	NET_PROT_PPPMUX_SUBFRM,
	NET_PROT_L2TPV2,
	NET_PROT_L2TPV3_CTRL,
	NET_PROT_L2TPV3_SESS,
	NET_PROT_LLC,
	NET_PROT_LLC_SNAP,
	NET_PROT_NLPID,
	NET_PROT_SNAP,
	NET_PROT_MPLS,
	NET_PROT_IPSEC_AH,
	NET_PROT_IPSEC_ESP,
	NET_PROT_UDP_ENC_ESP, /* RFC 3948 */
	NET_PROT_MACSEC,
	NET_PROT_GRE,
	NET_PROT_MINENCAP,
	NET_PROT_DCCP,
	NET_PROT_ICMP,
	NET_PROT_IGMP,
	NET_PROT_ARP,
	NET_PROT_CAPWAP_DATA,
	NET_PROT_CAPWAP_CTRL,
	NET_PROT_RFC2684,
	NET_PROT_ICMPV6,
	NET_PROT_FCOE,
	NET_PROT_FIP,
	NET_PROT_ISCSI,
	NET_PROT_GTP,
	NET_PROT_USER_DEFINED_L2,
	NET_PROT_USER_DEFINED_L3,
	NET_PROT_USER_DEFINED_L4,
	NET_PROT_USER_DEFINED_L5,
	NET_PROT_USER_DEFINED_SHIM1,
	NET_PROT_USER_DEFINED_SHIM2,

	NET_PROT_DUMMY_LAST
};

/**
 * struct dpkg_extract - A structure for defining a single extraction
 * @type: Determines how the union below is interpreted:
 *	DPKG_EXTRACT_FROM_HDR: selects 'from_hdr';
 *	DPKG_EXTRACT_FROM_DATA: selects 'from_data';
 *	DPKG_EXTRACT_FROM_PARSE: selects 'from_parse'
 * @extract: Selects extraction method
 * @extract.from_hdr: Used when 'type = DPKG_EXTRACT_FROM_HDR'
 * @extract.from_data: Used when 'type = DPKG_EXTRACT_FROM_DATA'
 * @extract.from_parse:  Used when 'type = DPKG_EXTRACT_FROM_PARSE'
 * @extract.from_hdr.prot: Any of the supported headers
 * @extract.from_hdr.type: Defines the type of header extraction:
 *	DPKG_FROM_HDR: use size & offset below;
 *	DPKG_FROM_FIELD: use field, size and offset below;
 *	DPKG_FULL_FIELD: use field below
 * @extract.from_hdr.field: One of the supported fields (NH_FLD_)
 * @extract.from_hdr.size: Size in bytes
 * @extract.from_hdr.offset: Byte offset
 * @extract.from_hdr.hdr_index: Clear for cases not listed below;
 *	Used for protocols that may have more than a single
 *	header, 0 indicates an outer header;
 *	Supported protocols (possible values):
 *	NET_PROT_VLAN (0, HDR_INDEX_LAST);
 *	NET_PROT_MPLS (0, 1, HDR_INDEX_LAST);
 *	NET_PROT_IP(0, HDR_INDEX_LAST);
 *	NET_PROT_IPv4(0, HDR_INDEX_LAST);
 *	NET_PROT_IPv6(0, HDR_INDEX_LAST);
 * @extract.from_data.size: Size in bytes
 * @extract.from_data.offset: Byte offset
 * @extract.from_parse.size: Size in bytes
 * @extract.from_parse.offset: Byte offset
 * @num_of_byte_masks: Defines the number of valid entries in the array below;
 *		This is	also the number of bytes to be used as masks
 * @masks: Masks parameters
 */
struct dpkg_extract {
	enum dpkg_extract_type type;
	union {
		struct {
			enum net_prot			prot;
			enum dpkg_extract_from_hdr_type type;
			u32			field;
			u8			size;
			u8			offset;
			u8			hdr_index;
		} from_hdr;
		struct {
			u8 size;
			u8 offset;
		} from_data;
		struct {
			u8 size;
			u8 offset;
		} from_parse;
	} extract;

	u8		num_of_byte_masks;
	struct dpkg_mask	masks[DPKG_NUM_OF_MASKS];
};

/**
 * struct dpkg_profile_cfg - A structure for defining a full Key Generation
 *				profile (rule)
 * @num_extracts: Defines the number of valid entries in the array below
 * @extracts: Array of required extractions
 */
struct dpkg_profile_cfg {
	u8 num_extracts;
	struct dpkg_extract extracts[DPKG_MAX_NUM_OF_EXTRACTS];
};

#endif /* __FSL_DPKG_H_ */
