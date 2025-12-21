// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022, Intel Corporation. */

#include "rss.h"
#include "ice_vf_lib_private.h"
#include "ice.h"

#define FIELD_SELECTOR(proto_hdr_field) \
		BIT((proto_hdr_field) & PROTO_HDR_FIELD_MASK)

struct ice_vc_hdr_match_type {
	u32 vc_hdr;	/* virtchnl headers (VIRTCHNL_PROTO_HDR_XXX) */
	u32 ice_hdr;	/* ice headers (ICE_FLOW_SEG_HDR_XXX) */
};

static const struct ice_vc_hdr_match_type ice_vc_hdr_list[] = {
	{VIRTCHNL_PROTO_HDR_NONE,	ICE_FLOW_SEG_HDR_NONE},
	{VIRTCHNL_PROTO_HDR_ETH,	ICE_FLOW_SEG_HDR_ETH},
	{VIRTCHNL_PROTO_HDR_S_VLAN,	ICE_FLOW_SEG_HDR_VLAN},
	{VIRTCHNL_PROTO_HDR_C_VLAN,	ICE_FLOW_SEG_HDR_VLAN},
	{VIRTCHNL_PROTO_HDR_IPV4,	ICE_FLOW_SEG_HDR_IPV4 |
					ICE_FLOW_SEG_HDR_IPV_OTHER},
	{VIRTCHNL_PROTO_HDR_IPV6,	ICE_FLOW_SEG_HDR_IPV6 |
					ICE_FLOW_SEG_HDR_IPV_OTHER},
	{VIRTCHNL_PROTO_HDR_TCP,	ICE_FLOW_SEG_HDR_TCP},
	{VIRTCHNL_PROTO_HDR_UDP,	ICE_FLOW_SEG_HDR_UDP},
	{VIRTCHNL_PROTO_HDR_SCTP,	ICE_FLOW_SEG_HDR_SCTP},
	{VIRTCHNL_PROTO_HDR_PPPOE,	ICE_FLOW_SEG_HDR_PPPOE},
	{VIRTCHNL_PROTO_HDR_GTPU_IP,	ICE_FLOW_SEG_HDR_GTPU_IP},
	{VIRTCHNL_PROTO_HDR_GTPU_EH,	ICE_FLOW_SEG_HDR_GTPU_EH},
	{VIRTCHNL_PROTO_HDR_GTPU_EH_PDU_DWN,
					ICE_FLOW_SEG_HDR_GTPU_DWN},
	{VIRTCHNL_PROTO_HDR_GTPU_EH_PDU_UP,
					ICE_FLOW_SEG_HDR_GTPU_UP},
	{VIRTCHNL_PROTO_HDR_L2TPV3,	ICE_FLOW_SEG_HDR_L2TPV3},
	{VIRTCHNL_PROTO_HDR_ESP,	ICE_FLOW_SEG_HDR_ESP},
	{VIRTCHNL_PROTO_HDR_AH,		ICE_FLOW_SEG_HDR_AH},
	{VIRTCHNL_PROTO_HDR_PFCP,	ICE_FLOW_SEG_HDR_PFCP_SESSION},
	{VIRTCHNL_PROTO_HDR_GTPC,	ICE_FLOW_SEG_HDR_GTPC},
	{VIRTCHNL_PROTO_HDR_L2TPV2,	ICE_FLOW_SEG_HDR_L2TPV2},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,	ICE_FLOW_SEG_HDR_IPV_FRAG},
	{VIRTCHNL_PROTO_HDR_IPV6_EH_FRAG,	ICE_FLOW_SEG_HDR_IPV_FRAG},
	{VIRTCHNL_PROTO_HDR_GRE,        ICE_FLOW_SEG_HDR_GRE},
};

struct ice_vc_hash_field_match_type {
	u32 vc_hdr;		/* virtchnl headers
				 * (VIRTCHNL_PROTO_HDR_XXX)
				 */
	u32 vc_hash_field;	/* virtchnl hash fields selector
				 * FIELD_SELECTOR((VIRTCHNL_PROTO_HDR_ETH_XXX))
				 */
	u64 ice_hash_field;	/* ice hash fields
				 * (BIT_ULL(ICE_FLOW_FIELD_IDX_XXX))
				 */
};

static const struct
ice_vc_hash_field_match_type ice_vc_hash_field_list[] = {
	{VIRTCHNL_PROTO_HDR_ETH, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_ETH_SRC),
		BIT_ULL(ICE_FLOW_FIELD_IDX_ETH_SA)},
	{VIRTCHNL_PROTO_HDR_ETH, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_ETH_DST),
		BIT_ULL(ICE_FLOW_FIELD_IDX_ETH_DA)},
	{VIRTCHNL_PROTO_HDR_ETH, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_ETH_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_ETH_DST),
		ICE_FLOW_HASH_ETH},
	{VIRTCHNL_PROTO_HDR_ETH,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_ETH_ETHERTYPE),
		BIT_ULL(ICE_FLOW_FIELD_IDX_ETH_TYPE)},
	{VIRTCHNL_PROTO_HDR_S_VLAN,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_S_VLAN_ID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_S_VLAN)},
	{VIRTCHNL_PROTO_HDR_C_VLAN,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_C_VLAN_ID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_C_VLAN)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_SA)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_DA)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST),
		ICE_FLOW_HASH_IPV4},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_SA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_DA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT),
		ICE_FLOW_HASH_IPV4 | BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV4,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_FRAG_PKID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_ID)},
	{VIRTCHNL_PROTO_HDR_IPV4,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_SA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_DA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		ICE_FLOW_HASH_IPV4 | BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_SA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_DA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		ICE_FLOW_HASH_IPV4 | BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV4,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_SA)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_DA)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST),
		ICE_FLOW_HASH_IPV4},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_SA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_DA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT),
		ICE_FLOW_HASH_IPV4 | BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_FRAG_PKID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_ID)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_SA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_DA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		ICE_FLOW_HASH_IPV4 | BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_SA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_DA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		ICE_FLOW_HASH_IPV4 | BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV4_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_IPV6, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_SRC),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_SA)},
	{VIRTCHNL_PROTO_HDR_IPV6, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_DST),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_DA)},
	{VIRTCHNL_PROTO_HDR_IPV6, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_DST),
		ICE_FLOW_HASH_IPV6},
	{VIRTCHNL_PROTO_HDR_IPV6, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_SA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV6, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_DA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV6, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PROT),
		ICE_FLOW_HASH_IPV6 | BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV6, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV6_EH_FRAG,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_EH_FRAG_PKID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_ID)},
	{VIRTCHNL_PROTO_HDR_IPV6,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PREFIX64_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PREFIX64_DST),
		ICE_FLOW_HASH_IPV6_PRE64},
	{VIRTCHNL_PROTO_HDR_IPV6,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PREFIX64_SRC),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_PRE64_SA)},
	{VIRTCHNL_PROTO_HDR_IPV6,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PREFIX64_DST),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_PRE64_DA)},
	{VIRTCHNL_PROTO_HDR_IPV6,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PREFIX64_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PREFIX64_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PROT),
		ICE_FLOW_HASH_IPV6_PRE64 |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV6,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PREFIX64_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_PRE64_SA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV6,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PREFIX64_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_PRE64_DA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_PROT)},
	{VIRTCHNL_PROTO_HDR_TCP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_SRC_PORT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_TCP_SRC_PORT)},
	{VIRTCHNL_PROTO_HDR_TCP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_DST_PORT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_TCP_DST_PORT)},
	{VIRTCHNL_PROTO_HDR_TCP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_SRC_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_DST_PORT),
		ICE_FLOW_HASH_TCP_PORT},
	{VIRTCHNL_PROTO_HDR_TCP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_TCP_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_TCP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_SRC_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_TCP_SRC_PORT) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_TCP_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_TCP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_DST_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_TCP_DST_PORT) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_TCP_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_TCP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_SRC_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_DST_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_CHKSUM),
		ICE_FLOW_HASH_TCP_PORT |
		BIT_ULL(ICE_FLOW_FIELD_IDX_TCP_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_UDP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_SRC_PORT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_UDP_SRC_PORT)},
	{VIRTCHNL_PROTO_HDR_UDP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_DST_PORT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_UDP_DST_PORT)},
	{VIRTCHNL_PROTO_HDR_UDP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_SRC_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_DST_PORT),
		ICE_FLOW_HASH_UDP_PORT},
	{VIRTCHNL_PROTO_HDR_UDP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_UDP_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_UDP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_SRC_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_UDP_SRC_PORT) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_UDP_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_UDP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_DST_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_UDP_DST_PORT) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_UDP_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_UDP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_SRC_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_DST_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_CHKSUM),
		ICE_FLOW_HASH_UDP_PORT |
		BIT_ULL(ICE_FLOW_FIELD_IDX_UDP_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_SCTP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_SRC_PORT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_SCTP_SRC_PORT)},
	{VIRTCHNL_PROTO_HDR_SCTP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_DST_PORT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_SCTP_DST_PORT)},
	{VIRTCHNL_PROTO_HDR_SCTP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_SRC_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_DST_PORT),
		ICE_FLOW_HASH_SCTP_PORT},
	{VIRTCHNL_PROTO_HDR_SCTP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_SCTP_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_SCTP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_SRC_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_SCTP_SRC_PORT) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_SCTP_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_SCTP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_DST_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_CHKSUM),
		BIT_ULL(ICE_FLOW_FIELD_IDX_SCTP_DST_PORT) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_SCTP_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_SCTP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_SRC_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_DST_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_CHKSUM),
		ICE_FLOW_HASH_SCTP_PORT |
		BIT_ULL(ICE_FLOW_FIELD_IDX_SCTP_CHKSUM)},
	{VIRTCHNL_PROTO_HDR_PPPOE,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_PPPOE_SESS_ID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_PPPOE_SESS_ID)},
	{VIRTCHNL_PROTO_HDR_GTPU_IP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_GTPU_IP_TEID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_GTPU_IP_TEID)},
	{VIRTCHNL_PROTO_HDR_L2TPV3,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_L2TPV3_SESS_ID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_L2TPV3_SESS_ID)},
	{VIRTCHNL_PROTO_HDR_ESP, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_ESP_SPI),
		BIT_ULL(ICE_FLOW_FIELD_IDX_ESP_SPI)},
	{VIRTCHNL_PROTO_HDR_AH, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_AH_SPI),
		BIT_ULL(ICE_FLOW_FIELD_IDX_AH_SPI)},
	{VIRTCHNL_PROTO_HDR_PFCP, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_PFCP_SEID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_PFCP_SEID)},
	{VIRTCHNL_PROTO_HDR_GTPC,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_GTPC_TEID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_GTPC_TEID)},
	{VIRTCHNL_PROTO_HDR_L2TPV2,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_L2TPV2_SESS_ID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_L2TPV2_SESS_ID)},
	{VIRTCHNL_PROTO_HDR_L2TPV2,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_L2TPV2_LEN_SESS_ID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_L2TPV2_LEN_SESS_ID)},
};

static int
ice_vc_rss_hash_update(struct ice_hw *hw, struct ice_vsi *vsi, u8 hash_type)
{
	struct ice_vsi_ctx *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	/* clear previous hash_type */
	ctx->info.q_opt_rss = vsi->info.q_opt_rss &
		~ICE_AQ_VSI_Q_OPT_RSS_HASH_M;
	/* hash_type is passed in as ICE_AQ_VSI_Q_OPT_RSS_<XOR|TPLZ|SYM_TPLZ */
	ctx->info.q_opt_rss |= FIELD_PREP(ICE_AQ_VSI_Q_OPT_RSS_HASH_M,
					  hash_type);

	/* Preserve existing queueing option setting */
	ctx->info.q_opt_tc = vsi->info.q_opt_tc;
	ctx->info.q_opt_flags = vsi->info.q_opt_flags;

	ctx->info.valid_sections =
			cpu_to_le16(ICE_AQ_VSI_PROP_Q_OPT_VALID);

	ret = ice_update_vsi(hw, vsi->idx, ctx, NULL);
	if (ret) {
		dev_err(ice_hw_to_dev(hw), "update VSI for RSS failed, err %d aq_err %s\n",
			ret, libie_aq_str(hw->adminq.sq_last_status));
	} else {
		vsi->info.q_opt_rss = ctx->info.q_opt_rss;
	}

	kfree(ctx);

	return ret;
}

/**
 * ice_vc_validate_pattern
 * @vf: pointer to the VF info
 * @proto: virtchnl protocol headers
 *
 * validate the pattern is supported or not.
 *
 * Return: true on success, false on error.
 */
bool
ice_vc_validate_pattern(struct ice_vf *vf, struct virtchnl_proto_hdrs *proto)
{
	bool is_ipv4 = false;
	bool is_ipv6 = false;
	bool is_udp = false;
	u16 ptype = -1;
	int i = 0;

	while (i < proto->count &&
	       proto->proto_hdr[i].type != VIRTCHNL_PROTO_HDR_NONE) {
		switch (proto->proto_hdr[i].type) {
		case VIRTCHNL_PROTO_HDR_ETH:
			ptype = ICE_PTYPE_MAC_PAY;
			break;
		case VIRTCHNL_PROTO_HDR_IPV4:
			ptype = ICE_PTYPE_IPV4_PAY;
			is_ipv4 = true;
			break;
		case VIRTCHNL_PROTO_HDR_IPV6:
			ptype = ICE_PTYPE_IPV6_PAY;
			is_ipv6 = true;
			break;
		case VIRTCHNL_PROTO_HDR_UDP:
			if (is_ipv4)
				ptype = ICE_PTYPE_IPV4_UDP_PAY;
			else if (is_ipv6)
				ptype = ICE_PTYPE_IPV6_UDP_PAY;
			is_udp = true;
			break;
		case VIRTCHNL_PROTO_HDR_TCP:
			if (is_ipv4)
				ptype = ICE_PTYPE_IPV4_TCP_PAY;
			else if (is_ipv6)
				ptype = ICE_PTYPE_IPV6_TCP_PAY;
			break;
		case VIRTCHNL_PROTO_HDR_SCTP:
			if (is_ipv4)
				ptype = ICE_PTYPE_IPV4_SCTP_PAY;
			else if (is_ipv6)
				ptype = ICE_PTYPE_IPV6_SCTP_PAY;
			break;
		case VIRTCHNL_PROTO_HDR_GTPU_IP:
		case VIRTCHNL_PROTO_HDR_GTPU_EH:
			if (is_ipv4)
				ptype = ICE_MAC_IPV4_GTPU;
			else if (is_ipv6)
				ptype = ICE_MAC_IPV6_GTPU;
			goto out;
		case VIRTCHNL_PROTO_HDR_L2TPV3:
			if (is_ipv4)
				ptype = ICE_MAC_IPV4_L2TPV3;
			else if (is_ipv6)
				ptype = ICE_MAC_IPV6_L2TPV3;
			goto out;
		case VIRTCHNL_PROTO_HDR_ESP:
			if (is_ipv4)
				ptype = is_udp ? ICE_MAC_IPV4_NAT_T_ESP :
						ICE_MAC_IPV4_ESP;
			else if (is_ipv6)
				ptype = is_udp ? ICE_MAC_IPV6_NAT_T_ESP :
						ICE_MAC_IPV6_ESP;
			goto out;
		case VIRTCHNL_PROTO_HDR_AH:
			if (is_ipv4)
				ptype = ICE_MAC_IPV4_AH;
			else if (is_ipv6)
				ptype = ICE_MAC_IPV6_AH;
			goto out;
		case VIRTCHNL_PROTO_HDR_PFCP:
			if (is_ipv4)
				ptype = ICE_MAC_IPV4_PFCP_SESSION;
			else if (is_ipv6)
				ptype = ICE_MAC_IPV6_PFCP_SESSION;
			goto out;
		default:
			break;
		}
		i++;
	}

out:
	return ice_hw_ptype_ena(&vf->pf->hw, ptype);
}

/**
 * ice_vc_parse_rss_cfg - parses hash fields and headers from
 * a specific virtchnl RSS cfg
 * @hw: pointer to the hardware
 * @rss_cfg: pointer to the virtchnl RSS cfg
 * @hash_cfg: pointer to the HW hash configuration
 *
 * Return true if all the protocol header and hash fields in the RSS cfg could
 * be parsed, else return false
 *
 * This function parses the virtchnl RSS cfg to be the intended
 * hash fields and the intended header for RSS configuration
 */
static bool ice_vc_parse_rss_cfg(struct ice_hw *hw,
				 struct virtchnl_rss_cfg *rss_cfg,
				 struct ice_rss_hash_cfg *hash_cfg)
{
	const struct ice_vc_hash_field_match_type *hf_list;
	const struct ice_vc_hdr_match_type *hdr_list;
	int i, hf_list_len, hdr_list_len;
	bool outer_ipv4 = false;
	bool outer_ipv6 = false;
	bool inner_hdr = false;
	bool has_gre = false;

	u32 *addl_hdrs = &hash_cfg->addl_hdrs;
	u64 *hash_flds = &hash_cfg->hash_flds;

	/* set outer layer RSS as default */
	hash_cfg->hdr_type = ICE_RSS_OUTER_HEADERS;

	if (rss_cfg->rss_algorithm == VIRTCHNL_RSS_ALG_TOEPLITZ_SYMMETRIC)
		hash_cfg->symm = true;
	else
		hash_cfg->symm = false;

	hf_list = ice_vc_hash_field_list;
	hf_list_len = ARRAY_SIZE(ice_vc_hash_field_list);
	hdr_list = ice_vc_hdr_list;
	hdr_list_len = ARRAY_SIZE(ice_vc_hdr_list);

	for (i = 0; i < rss_cfg->proto_hdrs.count; i++) {
		struct virtchnl_proto_hdr *proto_hdr =
					&rss_cfg->proto_hdrs.proto_hdr[i];
		u32 hdr_found = 0;
		int j;

		/* Find matched ice headers according to virtchnl headers.
		 * Also figure out the outer type of GTPU headers.
		 */
		for (j = 0; j < hdr_list_len; j++) {
			struct ice_vc_hdr_match_type hdr_map = hdr_list[j];

			if (proto_hdr->type == hdr_map.vc_hdr)
				hdr_found = hdr_map.ice_hdr;
		}

		if (!hdr_found)
			return false;

		/* Find matched ice hash fields according to
		 * virtchnl hash fields.
		 */
		for (j = 0; j < hf_list_len; j++) {
			struct ice_vc_hash_field_match_type hf_map = hf_list[j];

			if (proto_hdr->type == hf_map.vc_hdr &&
			    proto_hdr->field_selector == hf_map.vc_hash_field) {
				*hash_flds |= hf_map.ice_hash_field;
				break;
			}
		}

		if (proto_hdr->type == VIRTCHNL_PROTO_HDR_IPV4 && !inner_hdr)
			outer_ipv4 = true;
		else if (proto_hdr->type == VIRTCHNL_PROTO_HDR_IPV6 &&
			 !inner_hdr)
			outer_ipv6 = true;
		/* for GRE and L2TPv2, take inner header as input set if no
		 * any field is selected from outer headers.
		 * for GTPU, take inner header and GTPU teid as input set.
		 */
		else if ((proto_hdr->type == VIRTCHNL_PROTO_HDR_GTPU_IP ||
			  proto_hdr->type == VIRTCHNL_PROTO_HDR_GTPU_EH ||
			  proto_hdr->type == VIRTCHNL_PROTO_HDR_GTPU_EH_PDU_DWN ||
			  proto_hdr->type ==
				VIRTCHNL_PROTO_HDR_GTPU_EH_PDU_UP) ||
			 ((proto_hdr->type == VIRTCHNL_PROTO_HDR_L2TPV2 ||
			   proto_hdr->type == VIRTCHNL_PROTO_HDR_GRE) &&
			   *hash_flds == 0)) {
			/* set inner_hdr flag, and clean up outer header */
			inner_hdr = true;

			/* clear outer headers */
			*addl_hdrs = 0;

			if (outer_ipv4 && outer_ipv6)
				return false;

			if (outer_ipv4)
				hash_cfg->hdr_type = ICE_RSS_INNER_HEADERS_W_OUTER_IPV4;
			else if (outer_ipv6)
				hash_cfg->hdr_type = ICE_RSS_INNER_HEADERS_W_OUTER_IPV6;
			else
				hash_cfg->hdr_type = ICE_RSS_INNER_HEADERS;

			if (has_gre && outer_ipv4)
				hash_cfg->hdr_type =
					ICE_RSS_INNER_HEADERS_W_OUTER_IPV4_GRE;
			if (has_gre && outer_ipv6)
				hash_cfg->hdr_type =
					ICE_RSS_INNER_HEADERS_W_OUTER_IPV6_GRE;

			if (proto_hdr->type == VIRTCHNL_PROTO_HDR_GRE)
				has_gre = true;
		}

		*addl_hdrs |= hdr_found;

		/* refine hash hdrs and fields for IP fragment */
		if (VIRTCHNL_TEST_PROTO_HDR_FIELD(proto_hdr,
						  VIRTCHNL_PROTO_HDR_IPV4_FRAG_PKID) &&
		    proto_hdr->type == VIRTCHNL_PROTO_HDR_IPV4_FRAG) {
			*addl_hdrs |= ICE_FLOW_SEG_HDR_IPV_FRAG;
			*addl_hdrs &= ~(ICE_FLOW_SEG_HDR_IPV_OTHER);
			*hash_flds |= BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_ID);
			VIRTCHNL_DEL_PROTO_HDR_FIELD(proto_hdr,
						     VIRTCHNL_PROTO_HDR_IPV4_FRAG_PKID);
		}
		if (VIRTCHNL_TEST_PROTO_HDR_FIELD(proto_hdr,
						  VIRTCHNL_PROTO_HDR_IPV6_EH_FRAG_PKID) &&
		    proto_hdr->type == VIRTCHNL_PROTO_HDR_IPV6_EH_FRAG) {
			*addl_hdrs |= ICE_FLOW_SEG_HDR_IPV_FRAG;
			*addl_hdrs &= ~(ICE_FLOW_SEG_HDR_IPV_OTHER);
			*hash_flds |= BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_ID);
			VIRTCHNL_DEL_PROTO_HDR_FIELD(proto_hdr,
						     VIRTCHNL_PROTO_HDR_IPV6_EH_FRAG_PKID);
		}
	}

	/* refine gtpu header if we take outer as input set for a no inner
	 * ip gtpu flow.
	 */
	if (hash_cfg->hdr_type == ICE_RSS_OUTER_HEADERS &&
	    *addl_hdrs & ICE_FLOW_SEG_HDR_GTPU_IP) {
		*addl_hdrs &= ~(ICE_FLOW_SEG_HDR_GTPU_IP);
		*addl_hdrs |= ICE_FLOW_SEG_HDR_GTPU_NON_IP;
	}

	/* refine hash field for esp and nat-t-esp. */
	if ((*addl_hdrs & ICE_FLOW_SEG_HDR_UDP) &&
	    (*addl_hdrs & ICE_FLOW_SEG_HDR_ESP)) {
		*addl_hdrs &= ~(ICE_FLOW_SEG_HDR_ESP | ICE_FLOW_SEG_HDR_UDP);
		*addl_hdrs |= ICE_FLOW_SEG_HDR_NAT_T_ESP;
		*hash_flds &= ~(BIT_ULL(ICE_FLOW_FIELD_IDX_ESP_SPI));
		*hash_flds |= BIT_ULL(ICE_FLOW_FIELD_IDX_NAT_T_ESP_SPI);
	}

	/* refine hash hdrs for L4 udp/tcp/sctp. */
	if (*addl_hdrs & (ICE_FLOW_SEG_HDR_TCP | ICE_FLOW_SEG_HDR_UDP |
			  ICE_FLOW_SEG_HDR_SCTP) &&
	    *addl_hdrs & ICE_FLOW_SEG_HDR_IPV_OTHER)
		*addl_hdrs &= ~ICE_FLOW_SEG_HDR_IPV_OTHER;

	return true;
}

/**
 * ice_vf_adv_rss_offload_ena - determine if capabilities support advanced
 * RSS offloads
 * @caps: VF driver negotiated capabilities
 *
 * Return true if VIRTCHNL_VF_OFFLOAD_ADV_RSS_PF capability is set,
 * else return false
 */
static bool ice_vf_adv_rss_offload_ena(u32 caps)
{
	return !!(caps & VIRTCHNL_VF_OFFLOAD_ADV_RSS_PF);
}

/**
 * ice_is_hash_cfg_valid - Check whether an RSS hash context is valid
 * @cfg: RSS hash configuration to test
 *
 * Return: true if both @cfg->hash_flds and @cfg->addl_hdrs are non-zero; false otherwise.
 */
static bool ice_is_hash_cfg_valid(struct ice_rss_hash_cfg *cfg)
{
	return cfg->hash_flds && cfg->addl_hdrs;
}

/**
 * ice_hash_cfg_reset - Reset an RSS hash context
 * @cfg: RSS hash configuration to reset
 *
 * Reset fields of @cfg that store the active rule information.
 */
static void ice_hash_cfg_reset(struct ice_rss_hash_cfg *cfg)
{
	cfg->hash_flds = 0;
	cfg->addl_hdrs = 0;
	cfg->hdr_type = ICE_RSS_OUTER_HEADERS;
	cfg->symm = 0;
}

/**
 * ice_hash_cfg_record - Record an RSS hash context
 * @ctx: destination (global) RSS hash configuration
 * @cfg: source RSS hash configuration to record
 *
 * Copy the active rule information from @cfg into @ctx.
 */
static void ice_hash_cfg_record(struct ice_rss_hash_cfg *ctx,
				struct ice_rss_hash_cfg *cfg)
{
	ctx->hash_flds = cfg->hash_flds;
	ctx->addl_hdrs = cfg->addl_hdrs;
	ctx->hdr_type = cfg->hdr_type;
	ctx->symm = cfg->symm;
}

/**
 * ice_hash_moveout - Delete an RSS configuration (keep context)
 * @vf: VF pointer
 * @cfg: RSS hash configuration
 *
 * Return: 0 on success (including when already absent); -ENOENT if @cfg is
 * invalid or VSI is missing; -EBUSY on hardware removal failure.
 */
static int
ice_hash_moveout(struct ice_vf *vf, struct ice_rss_hash_cfg *cfg)
{
	struct device *dev = ice_pf_to_dev(vf->pf);
	struct ice_vsi *vsi = ice_get_vf_vsi(vf);
	struct ice_hw *hw = &vf->pf->hw;
	int ret;

	if (!ice_is_hash_cfg_valid(cfg) || !vsi)
		return -ENOENT;

	ret = ice_rem_rss_cfg(hw, vsi->idx, cfg);
	if (ret && ret != -ENOENT) {
		dev_err(dev, "ice_rem_rss_cfg failed for VF %d, VSI %d, error:%d\n",
			vf->vf_id, vf->lan_vsi_idx, ret);
		return -EBUSY;
	}

	return 0;
}

/**
 * ice_hash_moveback - Add an RSS hash configuration for a VF
 * @vf: VF pointer
 * @cfg: RSS hash configuration to apply
 *
 * Add @cfg to @vf if the context is valid and VSI exists; programs HW.
 *
 * Return:
 * * 0 on success
 * * -ENOENT if @cfg is invalid or VSI is missing
 * * -EBUSY if hardware programming fails
 */
static int
ice_hash_moveback(struct ice_vf *vf, struct ice_rss_hash_cfg *cfg)
{
	struct device *dev = ice_pf_to_dev(vf->pf);
	struct ice_vsi *vsi = ice_get_vf_vsi(vf);
	struct ice_hw *hw = &vf->pf->hw;
	int ret;

	if (!ice_is_hash_cfg_valid(cfg) || !vsi)
		return -ENOENT;

	ret = ice_add_rss_cfg(hw, vsi, cfg);
	if (ret) {
		dev_err(dev, "ice_add_rss_cfg failed for VF %d, VSI %d, error:%d\n",
			vf->vf_id, vf->lan_vsi_idx, ret);
		return -EBUSY;
	}

	return 0;
}

/**
 * ice_hash_remove - remove a RSS configuration
 * @vf: pointer to the VF info
 * @cfg: pointer to the RSS hash configuration
 *
 * This function will delete a RSS hash configuration and also delete the
 * hash context which stores the rule info.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int
ice_hash_remove(struct ice_vf *vf, struct ice_rss_hash_cfg *cfg)
{
	int ret;

	ret = ice_hash_moveout(vf, cfg);
	if (ret && ret != -ENOENT)
		return ret;

	ice_hash_cfg_reset(cfg);

	return 0;
}

struct ice_gtpu_ctx_action {
	u32 ctx_idx;
	const u32 *remove_list;
	int remove_count;
	const u32 *moveout_list;
	int moveout_count;
};

/**
 * ice_add_rss_cfg_pre_gtpu - Pre-process the GTPU RSS configuration
 * @vf: pointer to the VF info
 * @ctx: pointer to the context of the GTPU hash
 * @ctx_idx: index of the hash context
 *
 * Pre-processes the GTPU hash configuration before adding a new
 * hash context. It removes or reorders existing hash configurations that may
 * conflict with the new one. For example, if a GTPU_UP or GTPU_DWN rule is
 * configured after a GTPU_EH rule, the GTPU_EH hash will be matched first due
 * to TCAM write and match order (top-down). In such cases, the GTPU_EH rule
 * must be moved after the GTPU_UP/DWN rule. Conversely, if a GTPU_EH rule is
 * configured after a GTPU_UP/DWN rule, the UP/DWN rules should be removed to
 * avoid conflict.
 *
 * Return: 0 on success or a negative error code on failure
 */
static int ice_add_rss_cfg_pre_gtpu(struct ice_vf *vf,
				    struct ice_vf_hash_gtpu_ctx *ctx,
				    u32 ctx_idx)
{
	int ret, i;

	static const u32 remove_eh_ip[] = {
		ICE_HASH_GTPU_CTX_EH_IP_UDP, ICE_HASH_GTPU_CTX_EH_IP_TCP,
		ICE_HASH_GTPU_CTX_UP_IP,     ICE_HASH_GTPU_CTX_UP_IP_UDP,
		ICE_HASH_GTPU_CTX_UP_IP_TCP, ICE_HASH_GTPU_CTX_DW_IP,
		ICE_HASH_GTPU_CTX_DW_IP_UDP, ICE_HASH_GTPU_CTX_DW_IP_TCP,
	};

	static const u32 remove_eh_ip_udp[] = {
		ICE_HASH_GTPU_CTX_UP_IP_UDP,
		ICE_HASH_GTPU_CTX_DW_IP_UDP,
	};
	static const u32 moveout_eh_ip_udp[] = {
		ICE_HASH_GTPU_CTX_UP_IP,
		ICE_HASH_GTPU_CTX_UP_IP_TCP,
		ICE_HASH_GTPU_CTX_DW_IP,
		ICE_HASH_GTPU_CTX_DW_IP_TCP,
	};

	static const u32 remove_eh_ip_tcp[] = {
		ICE_HASH_GTPU_CTX_UP_IP_TCP,
		ICE_HASH_GTPU_CTX_DW_IP_TCP,
	};
	static const u32 moveout_eh_ip_tcp[] = {
		ICE_HASH_GTPU_CTX_UP_IP,
		ICE_HASH_GTPU_CTX_UP_IP_UDP,
		ICE_HASH_GTPU_CTX_DW_IP,
		ICE_HASH_GTPU_CTX_DW_IP_UDP,
	};

	static const u32 remove_up_ip[] = {
		ICE_HASH_GTPU_CTX_UP_IP_UDP,
		ICE_HASH_GTPU_CTX_UP_IP_TCP,
	};
	static const u32 moveout_up_ip[] = {
		ICE_HASH_GTPU_CTX_EH_IP,
		ICE_HASH_GTPU_CTX_EH_IP_UDP,
		ICE_HASH_GTPU_CTX_EH_IP_TCP,
	};

	static const u32 moveout_up_ip_udp_tcp[] = {
		ICE_HASH_GTPU_CTX_EH_IP,
		ICE_HASH_GTPU_CTX_EH_IP_UDP,
		ICE_HASH_GTPU_CTX_EH_IP_TCP,
	};

	static const u32 remove_dw_ip[] = {
		ICE_HASH_GTPU_CTX_DW_IP_UDP,
		ICE_HASH_GTPU_CTX_DW_IP_TCP,
	};
	static const u32 moveout_dw_ip[] = {
		ICE_HASH_GTPU_CTX_EH_IP,
		ICE_HASH_GTPU_CTX_EH_IP_UDP,
		ICE_HASH_GTPU_CTX_EH_IP_TCP,
	};

	static const struct ice_gtpu_ctx_action actions[] = {
		{ ICE_HASH_GTPU_CTX_EH_IP, remove_eh_ip,
		  ARRAY_SIZE(remove_eh_ip), NULL, 0 },
		{ ICE_HASH_GTPU_CTX_EH_IP_UDP, remove_eh_ip_udp,
		  ARRAY_SIZE(remove_eh_ip_udp), moveout_eh_ip_udp,
		  ARRAY_SIZE(moveout_eh_ip_udp) },
		{ ICE_HASH_GTPU_CTX_EH_IP_TCP, remove_eh_ip_tcp,
		  ARRAY_SIZE(remove_eh_ip_tcp), moveout_eh_ip_tcp,
		  ARRAY_SIZE(moveout_eh_ip_tcp) },
		{ ICE_HASH_GTPU_CTX_UP_IP, remove_up_ip,
		  ARRAY_SIZE(remove_up_ip), moveout_up_ip,
		  ARRAY_SIZE(moveout_up_ip) },
		{ ICE_HASH_GTPU_CTX_UP_IP_UDP, NULL, 0, moveout_up_ip_udp_tcp,
		  ARRAY_SIZE(moveout_up_ip_udp_tcp) },
		{ ICE_HASH_GTPU_CTX_UP_IP_TCP, NULL, 0, moveout_up_ip_udp_tcp,
		  ARRAY_SIZE(moveout_up_ip_udp_tcp) },
		{ ICE_HASH_GTPU_CTX_DW_IP, remove_dw_ip,
		  ARRAY_SIZE(remove_dw_ip), moveout_dw_ip,
		  ARRAY_SIZE(moveout_dw_ip) },
		{ ICE_HASH_GTPU_CTX_DW_IP_UDP, NULL, 0, moveout_dw_ip,
		  ARRAY_SIZE(moveout_dw_ip) },
		{ ICE_HASH_GTPU_CTX_DW_IP_TCP, NULL, 0, moveout_dw_ip,
		  ARRAY_SIZE(moveout_dw_ip) },
	};

	for (i = 0; i < ARRAY_SIZE(actions); i++) {
		if (actions[i].ctx_idx != ctx_idx)
			continue;

		if (actions[i].remove_list) {
			for (int j = 0; j < actions[i].remove_count; j++) {
				u16 rm = actions[i].remove_list[j];

				ret = ice_hash_remove(vf, &ctx->ctx[rm]);
				if (ret && ret != -ENOENT)
					return ret;
			}
		}

		if (actions[i].moveout_list) {
			for (int j = 0; j < actions[i].moveout_count; j++) {
				u16 mv = actions[i].moveout_list[j];

				ret = ice_hash_moveout(vf, &ctx->ctx[mv]);
				if (ret && ret != -ENOENT)
					return ret;
			}
		}
		break;
	}

	return 0;
}

/**
 * ice_add_rss_cfg_pre_ip - Pre-process IP-layer RSS configuration
 * @vf: VF pointer
 * @ctx: IP L4 hash context (ESP/UDP-ESP/AH/PFCP and UDP/TCP/SCTP)
 *
 * Remove covered/recorded IP RSS configurations prior to adding a new one.
 *
 * Return: 0 on success; negative error code on failure.
 */
static int
ice_add_rss_cfg_pre_ip(struct ice_vf *vf, struct ice_vf_hash_ip_ctx *ctx)
{
	int i, ret;

	for (i = 1; i < ICE_HASH_IP_CTX_MAX; i++)
		if (ice_is_hash_cfg_valid(&ctx->ctx[i])) {
			ret = ice_hash_remove(vf, &ctx->ctx[i]);
			if (ret)
				return ret;
		}

	return 0;
}

/**
 * ice_calc_gtpu_ctx_idx - Calculate GTPU hash context index
 * @hdrs: Bitmask of protocol headers prefixed with ICE_FLOW_SEG_HDR_*
 *
 * Determine the GTPU hash context index based on the combination of
 * encapsulation headers (GTPU_EH, GTPU_UP, GTPU_DWN) and transport
 * protocols (UDP, TCP) within IPv4 or IPv6 flows.
 *
 * Return: A valid context index (0-8) if the header combination is supported,
 *         or ICE_HASH_GTPU_CTX_MAX if the combination is invalid.
 */
static enum ice_hash_gtpu_ctx_type ice_calc_gtpu_ctx_idx(u32 hdrs)
{
	u32 eh_idx, ip_idx;

	if (hdrs & ICE_FLOW_SEG_HDR_GTPU_EH)
		eh_idx = 0;
	else if (hdrs & ICE_FLOW_SEG_HDR_GTPU_UP)
		eh_idx = 1;
	else if (hdrs & ICE_FLOW_SEG_HDR_GTPU_DWN)
		eh_idx = 2;
	else
		return ICE_HASH_GTPU_CTX_MAX;

	ip_idx = 0;
	if (hdrs & ICE_FLOW_SEG_HDR_UDP)
		ip_idx = 1;
	else if (hdrs & ICE_FLOW_SEG_HDR_TCP)
		ip_idx = 2;

	if (hdrs & (ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_IPV6))
		return eh_idx * 3 + ip_idx;
	else
		return ICE_HASH_GTPU_CTX_MAX;
}

/**
 * ice_map_ip_ctx_idx - map the index of the IP L4 hash context
 * @hdrs: protocol headers prefix with ICE_FLOW_SEG_HDR_XXX.
 *
 * The IP L4 hash context use the index to classify for IPv4/IPv6 with
 * ESP/UDP_ESP/AH/PFCP and non-tunnel UDP/TCP/SCTP
 * this function map the index based on the protocol headers.
 *
 * Return: The mapped IP context index on success, or ICE_HASH_IP_CTX_MAX
 *         if no matching context is found.
 */
static u8 ice_map_ip_ctx_idx(u32 hdrs)
{
	u8 i;

	static struct {
		u32 hdrs;
		u8 ctx_idx;
	} ip_ctx_idx_map[] = {
		{ ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_IPV_OTHER |
			ICE_FLOW_SEG_HDR_ESP,
			ICE_HASH_IP_CTX_IP_ESP },
		{ ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_IPV_OTHER |
			ICE_FLOW_SEG_HDR_NAT_T_ESP,
			ICE_HASH_IP_CTX_IP_UDP_ESP },
		{ ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_IPV_OTHER |
			ICE_FLOW_SEG_HDR_AH,
			ICE_HASH_IP_CTX_IP_AH },
		{ ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_IPV_OTHER |
			ICE_FLOW_SEG_HDR_PFCP_SESSION,
			ICE_HASH_IP_CTX_IP_PFCP },
		{ ICE_FLOW_SEG_HDR_ETH | ICE_FLOW_SEG_HDR_VLAN |
			ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_UDP,
			ICE_HASH_IP_CTX_IP_UDP },
		{ ICE_FLOW_SEG_HDR_ETH | ICE_FLOW_SEG_HDR_VLAN |
			ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_TCP,
			ICE_HASH_IP_CTX_IP_TCP },
		{ ICE_FLOW_SEG_HDR_ETH | ICE_FLOW_SEG_HDR_VLAN |
			ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_SCTP,
			ICE_HASH_IP_CTX_IP_SCTP },
		{ ICE_FLOW_SEG_HDR_ETH | ICE_FLOW_SEG_HDR_VLAN |
			ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_IPV_OTHER,
			ICE_HASH_IP_CTX_IP },
		{ ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_IPV_OTHER |
			ICE_FLOW_SEG_HDR_ESP,
			ICE_HASH_IP_CTX_IP_ESP },
		{ ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_IPV_OTHER |
			ICE_FLOW_SEG_HDR_NAT_T_ESP,
			ICE_HASH_IP_CTX_IP_UDP_ESP },
		{ ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_IPV_OTHER |
			ICE_FLOW_SEG_HDR_AH,
			ICE_HASH_IP_CTX_IP_AH },
		{ ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_IPV_OTHER |
			ICE_FLOW_SEG_HDR_PFCP_SESSION,
			ICE_HASH_IP_CTX_IP_PFCP },
		{ ICE_FLOW_SEG_HDR_ETH | ICE_FLOW_SEG_HDR_VLAN |
			ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_UDP,
			ICE_HASH_IP_CTX_IP_UDP },
		{ ICE_FLOW_SEG_HDR_ETH | ICE_FLOW_SEG_HDR_VLAN |
			ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_TCP,
			ICE_HASH_IP_CTX_IP_TCP },
		{ ICE_FLOW_SEG_HDR_ETH | ICE_FLOW_SEG_HDR_VLAN |
			ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_SCTP,
			ICE_HASH_IP_CTX_IP_SCTP },
		{ ICE_FLOW_SEG_HDR_ETH | ICE_FLOW_SEG_HDR_VLAN |
			ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_IPV_OTHER,
			ICE_HASH_IP_CTX_IP },
		/* the remaining mappings are used for default RSS */
		{ ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_UDP,
			ICE_HASH_IP_CTX_IP_UDP },
		{ ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_TCP,
			ICE_HASH_IP_CTX_IP_TCP },
		{ ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_SCTP,
			ICE_HASH_IP_CTX_IP_SCTP },
		{ ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_IPV_OTHER,
			ICE_HASH_IP_CTX_IP },
		{ ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_UDP,
			ICE_HASH_IP_CTX_IP_UDP },
		{ ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_TCP,
			ICE_HASH_IP_CTX_IP_TCP },
		{ ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_SCTP,
			ICE_HASH_IP_CTX_IP_SCTP },
		{ ICE_FLOW_SEG_HDR_IPV6 | ICE_FLOW_SEG_HDR_IPV_OTHER,
			ICE_HASH_IP_CTX_IP },
	};

	for (i = 0; i < ARRAY_SIZE(ip_ctx_idx_map); i++) {
		if (hdrs == ip_ctx_idx_map[i].hdrs)
			return ip_ctx_idx_map[i].ctx_idx;
	}

	return ICE_HASH_IP_CTX_MAX;
}

/**
 * ice_add_rss_cfg_pre - Prepare RSS configuration context for a VF
 * @vf: pointer to the VF structure
 * @cfg: pointer to the RSS hash configuration
 *
 * Prepare the RSS hash context for a given VF based on the additional
 * protocol headers specified in @cfg. This includes pre-configuration
 * for IP and GTPU-based flows.
 *
 * If the configuration matches a known IP context, the function sets up
 * the appropriate IP hash context. If the configuration includes GTPU
 * headers, it prepares the GTPU-specific context accordingly.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int
ice_add_rss_cfg_pre(struct ice_vf *vf, struct ice_rss_hash_cfg *cfg)
{
	u32 ice_gtpu_ctx_idx = ice_calc_gtpu_ctx_idx(cfg->addl_hdrs);
	u8 ip_ctx_idx = ice_map_ip_ctx_idx(cfg->addl_hdrs);

	if (ip_ctx_idx == ICE_HASH_IP_CTX_IP) {
		int ret = 0;

		if (cfg->addl_hdrs & ICE_FLOW_SEG_HDR_IPV4)
			ret = ice_add_rss_cfg_pre_ip(vf, &vf->hash_ctx.v4);
		else if (cfg->addl_hdrs & ICE_FLOW_SEG_HDR_IPV6)
			ret = ice_add_rss_cfg_pre_ip(vf, &vf->hash_ctx.v6);

		if (ret)
			return ret;
	}

	if (cfg->addl_hdrs & ICE_FLOW_SEG_HDR_IPV4) {
		return ice_add_rss_cfg_pre_gtpu(vf, &vf->hash_ctx.ipv4,
						ice_gtpu_ctx_idx);
	} else if (cfg->addl_hdrs & ICE_FLOW_SEG_HDR_IPV6) {
		return ice_add_rss_cfg_pre_gtpu(vf, &vf->hash_ctx.ipv6,
						ice_gtpu_ctx_idx);
	}

	return 0;
}

/**
 * ice_add_rss_cfg_post_gtpu - Post-process GTPU RSS configuration
 * @vf: pointer to the VF info
 * @ctx: pointer to the context of the GTPU hash
 * @cfg: pointer to the RSS hash configuration
 * @ctx_idx: index of the hash context
 *
 * Post-processes the GTPU hash configuration after a new hash
 * context has been successfully added. It updates the context with the new
 * configuration and restores any previously removed hash contexts that need
 * to be re-applied. This ensures proper TCAM rule ordering and avoids
 * conflicts between overlapping GTPU rules.
 *
 * Return: 0 on success or a negative error code on failure
 */
static int ice_add_rss_cfg_post_gtpu(struct ice_vf *vf,
				     struct ice_vf_hash_gtpu_ctx *ctx,
				     struct ice_rss_hash_cfg *cfg, u32 ctx_idx)
{
	/* GTPU hash moveback lookup table indexed by context ID.
	 * Each entry is a bitmap indicating which contexts need moveback
	 * operations when the corresponding context index is processed.
	 */
	static const unsigned long
		ice_gtpu_moveback_tbl[ICE_HASH_GTPU_CTX_MAX] = {
			[ICE_HASH_GTPU_CTX_EH_IP] = 0,
			[ICE_HASH_GTPU_CTX_EH_IP_UDP] =
				BIT(ICE_HASH_GTPU_CTX_UP_IP) |
				BIT(ICE_HASH_GTPU_CTX_UP_IP_TCP) |
				BIT(ICE_HASH_GTPU_CTX_DW_IP) |
				BIT(ICE_HASH_GTPU_CTX_DW_IP_TCP),
			[ICE_HASH_GTPU_CTX_EH_IP_TCP] =
				BIT(ICE_HASH_GTPU_CTX_UP_IP) |
				BIT(ICE_HASH_GTPU_CTX_UP_IP_UDP) |
				BIT(ICE_HASH_GTPU_CTX_DW_IP) |
				BIT(ICE_HASH_GTPU_CTX_DW_IP_UDP),
			[ICE_HASH_GTPU_CTX_UP_IP] =
				BIT(ICE_HASH_GTPU_CTX_EH_IP) |
				BIT(ICE_HASH_GTPU_CTX_EH_IP_UDP) |
				BIT(ICE_HASH_GTPU_CTX_EH_IP_TCP),
			[ICE_HASH_GTPU_CTX_UP_IP_UDP] =
				BIT(ICE_HASH_GTPU_CTX_EH_IP) |
				BIT(ICE_HASH_GTPU_CTX_EH_IP_UDP) |
				BIT(ICE_HASH_GTPU_CTX_EH_IP_TCP),
			[ICE_HASH_GTPU_CTX_UP_IP_TCP] =
				BIT(ICE_HASH_GTPU_CTX_EH_IP) |
				BIT(ICE_HASH_GTPU_CTX_EH_IP_UDP) |
				BIT(ICE_HASH_GTPU_CTX_EH_IP_TCP),
			[ICE_HASH_GTPU_CTX_DW_IP] =
				BIT(ICE_HASH_GTPU_CTX_EH_IP) |
				BIT(ICE_HASH_GTPU_CTX_EH_IP_UDP) |
				BIT(ICE_HASH_GTPU_CTX_EH_IP_TCP),
			[ICE_HASH_GTPU_CTX_DW_IP_UDP] =
				BIT(ICE_HASH_GTPU_CTX_EH_IP) |
				BIT(ICE_HASH_GTPU_CTX_EH_IP_UDP) |
				BIT(ICE_HASH_GTPU_CTX_EH_IP_TCP),
			[ICE_HASH_GTPU_CTX_DW_IP_TCP] =
				BIT(ICE_HASH_GTPU_CTX_EH_IP) |
				BIT(ICE_HASH_GTPU_CTX_EH_IP_UDP) |
				BIT(ICE_HASH_GTPU_CTX_EH_IP_TCP),
		};
	unsigned long moveback_mask;
	int ret;
	int i;

	if (unlikely(ctx_idx >= ICE_HASH_GTPU_CTX_MAX))
		return 0;

	ctx->ctx[ctx_idx].addl_hdrs = cfg->addl_hdrs;
	ctx->ctx[ctx_idx].hash_flds = cfg->hash_flds;
	ctx->ctx[ctx_idx].hdr_type = cfg->hdr_type;
	ctx->ctx[ctx_idx].symm = cfg->symm;

	moveback_mask = ice_gtpu_moveback_tbl[ctx_idx];
	for_each_set_bit(i, &moveback_mask, ICE_HASH_GTPU_CTX_MAX) {
		ret = ice_hash_moveback(vf, &ctx->ctx[i]);
		if (ret && ret != -ENOENT)
			return ret;
	}

	return 0;
}

static int
ice_add_rss_cfg_post(struct ice_vf *vf, struct ice_rss_hash_cfg *cfg)
{
	u32 ice_gtpu_ctx_idx = ice_calc_gtpu_ctx_idx(cfg->addl_hdrs);
	u8 ip_ctx_idx = ice_map_ip_ctx_idx(cfg->addl_hdrs);

	if (ip_ctx_idx && ip_ctx_idx < ICE_HASH_IP_CTX_MAX) {
		if (cfg->addl_hdrs & ICE_FLOW_SEG_HDR_IPV4)
			ice_hash_cfg_record(&vf->hash_ctx.v4.ctx[ip_ctx_idx], cfg);
		else if (cfg->addl_hdrs & ICE_FLOW_SEG_HDR_IPV6)
			ice_hash_cfg_record(&vf->hash_ctx.v6.ctx[ip_ctx_idx], cfg);
	}

	if (cfg->addl_hdrs & ICE_FLOW_SEG_HDR_IPV4) {
		return ice_add_rss_cfg_post_gtpu(vf, &vf->hash_ctx.ipv4,
						 cfg, ice_gtpu_ctx_idx);
	} else if (cfg->addl_hdrs & ICE_FLOW_SEG_HDR_IPV6) {
		return ice_add_rss_cfg_post_gtpu(vf, &vf->hash_ctx.ipv6,
						 cfg, ice_gtpu_ctx_idx);
	}

	return 0;
}

/**
 * ice_rem_rss_cfg_post - post-process the RSS configuration
 * @vf: pointer to the VF info
 * @cfg: pointer to the RSS hash configuration
 *
 * Post process the RSS hash configuration after deleting a hash
 * config. Such as, it will reset the hash context for the GTPU hash.
 */
static void
ice_rem_rss_cfg_post(struct ice_vf *vf, struct ice_rss_hash_cfg *cfg)
{
	u32 ice_gtpu_ctx_idx = ice_calc_gtpu_ctx_idx(cfg->addl_hdrs);
	u8 ip_ctx_idx = ice_map_ip_ctx_idx(cfg->addl_hdrs);

	if (ip_ctx_idx && ip_ctx_idx < ICE_HASH_IP_CTX_MAX) {
		if (cfg->addl_hdrs & ICE_FLOW_SEG_HDR_IPV4)
			ice_hash_cfg_reset(&vf->hash_ctx.v4.ctx[ip_ctx_idx]);
		else if (cfg->addl_hdrs & ICE_FLOW_SEG_HDR_IPV6)
			ice_hash_cfg_reset(&vf->hash_ctx.v6.ctx[ip_ctx_idx]);
	}

	if (ice_gtpu_ctx_idx >= ICE_HASH_GTPU_CTX_MAX)
		return;

	if (cfg->addl_hdrs & ICE_FLOW_SEG_HDR_IPV4)
		ice_hash_cfg_reset(&vf->hash_ctx.ipv4.ctx[ice_gtpu_ctx_idx]);
	else if (cfg->addl_hdrs & ICE_FLOW_SEG_HDR_IPV6)
		ice_hash_cfg_reset(&vf->hash_ctx.ipv6.ctx[ice_gtpu_ctx_idx]);
}

/**
 * ice_rem_rss_cfg_wrap - Wrapper for deleting an RSS configuration
 * @vf: pointer to the VF info
 * @cfg: pointer to the RSS hash configuration
 *
 * Wrapper function to delete a flow profile base on an RSS configuration,
 * and also post process the hash context base on the rollback mechanism
 * which handle some rules conflict by ice_add_rss_cfg_wrap.
 *
 * Return: 0 on success; negative error code on failure.
 */
static int
ice_rem_rss_cfg_wrap(struct ice_vf *vf, struct ice_rss_hash_cfg *cfg)
{
	struct device *dev = ice_pf_to_dev(vf->pf);
	struct ice_vsi *vsi = ice_get_vf_vsi(vf);
	struct ice_hw *hw = &vf->pf->hw;
	int ret;

	ret = ice_rem_rss_cfg(hw, vsi->idx, cfg);
	/* We just ignore -ENOENT, because if two configurations share the same
	 * profile remove one of them actually removes both, since the
	 * profile is deleted.
	 */
	if (ret && ret != -ENOENT) {
		dev_err(dev, "ice_rem_rss_cfg failed for VF %d, VSI %d, error:%d\n",
			vf->vf_id, vf->lan_vsi_idx, ret);
		return ret;
	}

	ice_rem_rss_cfg_post(vf, cfg);

	return 0;
}

/**
 * ice_add_rss_cfg_wrap - Wrapper for adding an RSS configuration
 * @vf: pointer to the VF info
 * @cfg: pointer to the RSS hash configuration
 *
 * Add a flow profile based on an RSS configuration. Use a rollback
 * mechanism to handle rule conflicts due to TCAM
 * write sequence from top to down.
 *
 * Return: 0 on success; negative error code on failure.
 */
static int
ice_add_rss_cfg_wrap(struct ice_vf *vf, struct ice_rss_hash_cfg *cfg)
{
	struct device *dev = ice_pf_to_dev(vf->pf);
	struct ice_vsi *vsi = ice_get_vf_vsi(vf);
	struct ice_hw *hw = &vf->pf->hw;
	int ret;

	if (ice_add_rss_cfg_pre(vf, cfg))
		return -EINVAL;

	ret = ice_add_rss_cfg(hw, vsi, cfg);
	if (ret) {
		dev_err(dev, "ice_add_rss_cfg failed for VF %d, VSI %d, error:%d\n",
			vf->vf_id, vf->lan_vsi_idx, ret);
		return ret;
	}

	if (ice_add_rss_cfg_post(vf, cfg))
		ret = -EINVAL;

	return ret;
}

/**
 * ice_parse_raw_rss_pattern - Parse raw pattern spec and mask for RSS
 * @vf: pointer to the VF info
 * @proto: pointer to the virtchnl protocol header
 * @raw_cfg: pointer to the RSS raw pattern configuration
 *
 * Parser function to get spec and mask from virtchnl message, and parse
 * them to get the corresponding profile and offset. The profile is used
 * to add RSS configuration.
 *
 * Return: 0 on success; negative error code on failure.
 */
static int
ice_parse_raw_rss_pattern(struct ice_vf *vf, struct virtchnl_proto_hdrs *proto,
			  struct ice_rss_raw_cfg *raw_cfg)
{
	struct ice_parser_result pkt_parsed;
	struct ice_hw *hw = &vf->pf->hw;
	struct ice_parser_profile prof;
	struct ice_parser *psr;
	u8 *pkt_buf, *msk_buf;
	u16 pkt_len;
	int ret = 0;

	pkt_len = proto->raw.pkt_len;
	if (!pkt_len)
		return -EINVAL;
	if (pkt_len > VIRTCHNL_MAX_SIZE_RAW_PACKET)
		pkt_len = VIRTCHNL_MAX_SIZE_RAW_PACKET;

	pkt_buf = kzalloc(pkt_len, GFP_KERNEL);
	msk_buf = kzalloc(pkt_len, GFP_KERNEL);
	if (!pkt_buf || !msk_buf) {
		ret = -ENOMEM;
		goto free_alloc;
	}

	memcpy(pkt_buf, proto->raw.spec, pkt_len);
	memcpy(msk_buf, proto->raw.mask, pkt_len);

	psr = ice_parser_create(hw);
	if (IS_ERR(psr)) {
		ret = PTR_ERR(psr);
		goto free_alloc;
	}

	ret = ice_parser_run(psr, pkt_buf, pkt_len, &pkt_parsed);
	if (ret)
		goto parser_destroy;

	ret = ice_parser_profile_init(&pkt_parsed, pkt_buf, msk_buf,
				      pkt_len, ICE_BLK_RSS, &prof);
	if (ret)
		goto parser_destroy;

	memcpy(&raw_cfg->prof, &prof, sizeof(prof));

parser_destroy:
	ice_parser_destroy(psr);
free_alloc:
	kfree(pkt_buf);
	kfree(msk_buf);
	return ret;
}

/**
 * ice_add_raw_rss_cfg - add RSS configuration for raw pattern
 * @vf: pointer to the VF info
 * @cfg: pointer to the RSS raw pattern configuration
 *
 * This function adds the RSS configuration for raw pattern.
 * Check if current profile is matched. If not, remove the old
 * one and add the new profile to HW directly. Update the symmetric
 * hash configuration as well.
 *
 * Return: 0 on success; negative error code on failure.
 */
static int
ice_add_raw_rss_cfg(struct ice_vf *vf, struct ice_rss_raw_cfg *cfg)
{
	struct ice_parser_profile *prof = &cfg->prof;
	struct device *dev = ice_pf_to_dev(vf->pf);
	struct ice_rss_prof_info *rss_prof;
	struct ice_hw *hw = &vf->pf->hw;
	int i, ptg, ret = 0;
	u16 vsi_handle;
	u64 id;

	vsi_handle = vf->lan_vsi_idx;
	id = find_first_bit(prof->ptypes, ICE_FLOW_PTYPE_MAX);

	ptg = hw->blk[ICE_BLK_RSS].xlt1.t[id];
	rss_prof = &vf->rss_prof_info[ptg];

	/* check if ptg already has a profile */
	if (rss_prof->prof.fv_num) {
		for (i = 0; i < ICE_MAX_FV_WORDS; i++) {
			if (rss_prof->prof.fv[i].proto_id !=
			    prof->fv[i].proto_id ||
			    rss_prof->prof.fv[i].offset !=
			    prof->fv[i].offset)
				break;
		}

		/* current profile is matched, check symmetric hash */
		if (i == ICE_MAX_FV_WORDS) {
			if (rss_prof->symm != cfg->symm)
				goto update_symm;
			return ret;
		}

		/* current profile is not matched, remove it */
		ret =
		ice_rem_prof_id_flow(hw, ICE_BLK_RSS,
				     ice_get_hw_vsi_num(hw, vsi_handle),
				     id);
		if (ret) {
			dev_err(dev, "remove RSS flow failed\n");
			return ret;
		}

		ret = ice_rem_prof(hw, ICE_BLK_RSS, id);
		if (ret) {
			dev_err(dev, "remove RSS profile failed\n");
			return ret;
		}
	}

	/* add new profile */
	ret = ice_flow_set_parser_prof(hw, vsi_handle, 0, prof, ICE_BLK_RSS);
	if (ret) {
		dev_err(dev, "HW profile add failed\n");
		return ret;
	}

	memcpy(&rss_prof->prof, prof, sizeof(struct ice_parser_profile));

update_symm:
	rss_prof->symm = cfg->symm;
	ice_rss_update_raw_symm(hw, cfg, id);
	return ret;
}

/**
 * ice_rem_raw_rss_cfg - remove RSS configuration for raw pattern
 * @vf: pointer to the VF info
 * @cfg: pointer to the RSS raw pattern configuration
 *
 * This function removes the RSS configuration for raw pattern.
 * Check if vsi group is already removed first. If not, remove the
 * profile.
 *
 * Return: 0 on success; negative error code on failure.
 */
static int
ice_rem_raw_rss_cfg(struct ice_vf *vf, struct ice_rss_raw_cfg *cfg)
{
	struct ice_parser_profile *prof = &cfg->prof;
	struct device *dev = ice_pf_to_dev(vf->pf);
	struct ice_hw *hw = &vf->pf->hw;
	int ptg, ret = 0;
	u16 vsig, vsi;
	u64 id;

	id = find_first_bit(prof->ptypes, ICE_FLOW_PTYPE_MAX);

	ptg = hw->blk[ICE_BLK_RSS].xlt1.t[id];

	memset(&vf->rss_prof_info[ptg], 0,
	       sizeof(struct ice_rss_prof_info));

	/* check if vsig is already removed */
	vsi = ice_get_hw_vsi_num(hw, vf->lan_vsi_idx);
	if (vsi >= ICE_MAX_VSI) {
		ret = -EINVAL;
		goto err;
	}

	vsig = hw->blk[ICE_BLK_RSS].xlt2.vsis[vsi].vsig;
	if (vsig) {
		ret = ice_rem_prof_id_flow(hw, ICE_BLK_RSS, vsi, id);
		if (ret)
			goto err;

		ret = ice_rem_prof(hw, ICE_BLK_RSS, id);
		if (ret)
			goto err;
	}

	return ret;

err:
	dev_err(dev, "HW profile remove failed\n");
	return ret;
}

/**
 * ice_vc_handle_rss_cfg
 * @vf: pointer to the VF info
 * @msg: pointer to the message buffer
 * @add: add a RSS config if true, otherwise delete a RSS config
 *
 * This function adds/deletes a RSS config
 */
int ice_vc_handle_rss_cfg(struct ice_vf *vf, u8 *msg, bool add)
{
	u32 v_opcode = add ? VIRTCHNL_OP_ADD_RSS_CFG : VIRTCHNL_OP_DEL_RSS_CFG;
	struct virtchnl_rss_cfg *rss_cfg = (struct virtchnl_rss_cfg *)msg;
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct device *dev = ice_pf_to_dev(vf->pf);
	struct ice_hw *hw = &vf->pf->hw;
	struct ice_vsi *vsi;
	u8 hash_type;
	bool symm;
	int ret;

	if (!test_bit(ICE_FLAG_RSS_ENA, vf->pf->flags)) {
		dev_dbg(dev, "VF %d attempting to configure RSS, but RSS is not supported by the PF\n",
			vf->vf_id);
		v_ret = VIRTCHNL_STATUS_ERR_NOT_SUPPORTED;
		goto error_param;
	}

	if (!ice_vf_adv_rss_offload_ena(vf->driver_caps)) {
		dev_dbg(dev, "VF %d attempting to configure RSS, but Advanced RSS offload is not supported\n",
			vf->vf_id);
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (rss_cfg->proto_hdrs.count > VIRTCHNL_MAX_NUM_PROTO_HDRS ||
	    rss_cfg->rss_algorithm < VIRTCHNL_RSS_ALG_TOEPLITZ_ASYMMETRIC ||
	    rss_cfg->rss_algorithm > VIRTCHNL_RSS_ALG_XOR_SYMMETRIC) {
		dev_dbg(dev, "VF %d attempting to configure RSS, but RSS configuration is not valid\n",
			vf->vf_id);
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (rss_cfg->rss_algorithm == VIRTCHNL_RSS_ALG_R_ASYMMETRIC) {
		hash_type = add ? ICE_AQ_VSI_Q_OPT_RSS_HASH_XOR :
				  ICE_AQ_VSI_Q_OPT_RSS_HASH_TPLZ;

		ret = ice_vc_rss_hash_update(hw, vsi, hash_type);
		if (ret)
			v_ret = ice_err_to_virt_err(ret);
		goto error_param;
	}

	hash_type = add ? ICE_AQ_VSI_Q_OPT_RSS_HASH_SYM_TPLZ :
			  ICE_AQ_VSI_Q_OPT_RSS_HASH_TPLZ;
	ret = ice_vc_rss_hash_update(hw, vsi, hash_type);
	if (ret) {
		v_ret = ice_err_to_virt_err(ret);
		goto error_param;
	}

	symm = rss_cfg->rss_algorithm == VIRTCHNL_RSS_ALG_TOEPLITZ_SYMMETRIC;
	/* Configure RSS hash for raw pattern */
	if (rss_cfg->proto_hdrs.tunnel_level == 0 &&
	    rss_cfg->proto_hdrs.count == 0) {
		struct ice_rss_raw_cfg raw_cfg;

		if (ice_parse_raw_rss_pattern(vf, &rss_cfg->proto_hdrs,
					      &raw_cfg)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		if (add) {
			raw_cfg.symm = symm;
			if (ice_add_raw_rss_cfg(vf, &raw_cfg))
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		} else {
			if (ice_rem_raw_rss_cfg(vf, &raw_cfg))
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		}
	} else {
		struct ice_rss_hash_cfg cfg;

		/* Only check for none raw pattern case */
		if (!ice_vc_validate_pattern(vf, &rss_cfg->proto_hdrs)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}
		cfg.addl_hdrs = ICE_FLOW_SEG_HDR_NONE;
		cfg.hash_flds = ICE_HASH_INVALID;
		cfg.hdr_type = ICE_RSS_ANY_HEADERS;

		if (!ice_vc_parse_rss_cfg(hw, rss_cfg, &cfg)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		if (add) {
			cfg.symm = symm;
			if (ice_add_rss_cfg_wrap(vf, &cfg))
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		} else {
			if (ice_rem_rss_cfg_wrap(vf, &cfg))
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		}
	}

error_param:
	return ice_vc_send_msg_to_vf(vf, v_opcode, v_ret, NULL, 0);
}

/**
 * ice_vc_config_rss_key
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * Configure the VF's RSS key
 */
int ice_vc_config_rss_key(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_rss_key *vrk =
		(struct virtchnl_rss_key *)msg;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vrk->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (vrk->key_len != ICE_VSIQF_HKEY_ARRAY_SIZE) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!test_bit(ICE_FLAG_RSS_ENA, vf->pf->flags)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (ice_set_rss_key(vsi, vrk->key))
		v_ret = VIRTCHNL_STATUS_ERR_ADMIN_QUEUE_ERROR;
error_param:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_RSS_KEY, v_ret,
				     NULL, 0);
}

/**
 * ice_vc_config_rss_lut
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * Configure the VF's RSS LUT
 */
int ice_vc_config_rss_lut(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_rss_lut *vrl = (struct virtchnl_rss_lut *)msg;
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vrl->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (vrl->lut_entries != ICE_LUT_VSI_SIZE) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!test_bit(ICE_FLAG_RSS_ENA, vf->pf->flags)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (ice_set_rss_lut(vsi, vrl->lut, ICE_LUT_VSI_SIZE))
		v_ret = VIRTCHNL_STATUS_ERR_ADMIN_QUEUE_ERROR;
error_param:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_RSS_LUT, v_ret,
				     NULL, 0);
}

/**
 * ice_vc_config_rss_hfunc
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * Configure the VF's RSS Hash function
 */
int ice_vc_config_rss_hfunc(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_rss_hfunc *vrh = (struct virtchnl_rss_hfunc *)msg;
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	u8 hfunc = ICE_AQ_VSI_Q_OPT_RSS_HASH_TPLZ;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vrh->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!test_bit(ICE_FLAG_RSS_ENA, vf->pf->flags)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (vrh->rss_algorithm == VIRTCHNL_RSS_ALG_TOEPLITZ_SYMMETRIC)
		hfunc = ICE_AQ_VSI_Q_OPT_RSS_HASH_SYM_TPLZ;

	if (ice_set_rss_hfunc(vsi, hfunc))
		v_ret = VIRTCHNL_STATUS_ERR_ADMIN_QUEUE_ERROR;
error_param:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_RSS_HFUNC, v_ret,
				     NULL, 0);
}

/**
 * ice_vc_get_rss_hashcfg - return the RSS Hash configuration
 * @vf: pointer to the VF info
 */
int ice_vc_get_rss_hashcfg(struct ice_vf *vf)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_rss_hashcfg *vrh = NULL;
	int len = 0, ret;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	if (!test_bit(ICE_FLAG_RSS_ENA, vf->pf->flags)) {
		dev_err(ice_pf_to_dev(vf->pf), "RSS not supported by PF\n");
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	len = sizeof(struct virtchnl_rss_hashcfg);
	vrh = kzalloc(len, GFP_KERNEL);
	if (!vrh) {
		v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
		len = 0;
		goto err;
	}

	vrh->hashcfg = ICE_DEFAULT_RSS_HASHCFG;
err:
	/* send the response back to the VF */
	ret = ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_GET_RSS_HASHCFG_CAPS, v_ret,
				    (u8 *)vrh, len);
	kfree(vrh);
	return ret;
}

/**
 * ice_vc_set_rss_hashcfg - set RSS Hash configuration bits for the VF
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 */
int ice_vc_set_rss_hashcfg(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_rss_hashcfg *vrh = (struct virtchnl_rss_hashcfg *)msg;
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi;
	struct device *dev;
	int status;

	dev = ice_pf_to_dev(pf);

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	if (!test_bit(ICE_FLAG_RSS_ENA, pf->flags)) {
		dev_err(dev, "RSS not supported by PF\n");
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	/* clear all previously programmed RSS configuration to allow VF drivers
	 * the ability to customize the RSS configuration and/or completely
	 * disable RSS
	 */
	status = ice_rem_vsi_rss_cfg(&pf->hw, vsi->idx);
	if (status && !vrh->hashcfg) {
		/* only report failure to clear the current RSS configuration if
		 * that was clearly the VF's intention (i.e. vrh->hashcfg = 0)
		 */
		v_ret = ice_err_to_virt_err(status);
		goto err;
	} else if (status) {
		/* allow the VF to update the RSS configuration even on failure
		 * to clear the current RSS confguration in an attempt to keep
		 * RSS in a working state
		 */
		dev_warn(dev, "Failed to clear the RSS configuration for VF %u\n",
			 vf->vf_id);
	}

	if (vrh->hashcfg) {
		status = ice_add_avf_rss_cfg(&pf->hw, vsi, vrh->hashcfg);
		v_ret = ice_err_to_virt_err(status);
	}

	/* save the requested VF configuration */
	if (!v_ret)
		vf->rss_hashcfg = vrh->hashcfg;

	/* send the response to the VF */
err:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_SET_RSS_HASHCFG, v_ret,
				     NULL, 0);
}

