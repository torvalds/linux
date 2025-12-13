// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021, Intel Corporation. */

#include "allowlist.h"

/* Purpose of this file is to share functionality to allowlist or denylist
 * opcodes used in PF <-> VF communication. Group of opcodes:
 * - default -> should be always allowed after creating VF,
 *   default_allowlist_opcodes
 * - opcodes needed by VF to work correctly, but not associated with caps ->
 *   should be allowed after successful VF resources allocation,
 *   working_allowlist_opcodes
 * - opcodes needed by VF when caps are activated
 *
 * Caps that don't use new opcodes (no opcodes should be allowed):
 * - VIRTCHNL_VF_OFFLOAD_WB_ON_ITR
 * - VIRTCHNL_VF_OFFLOAD_CRC
 * - VIRTCHNL_VF_OFFLOAD_RX_POLLING
 * - VIRTCHNL_VF_OFFLOAD_RSS_PCTYPE_V2
 * - VIRTCHNL_VF_OFFLOAD_ENCAP
 * - VIRTCHNL_VF_OFFLOAD_ENCAP_CSUM
 * - VIRTCHNL_VF_OFFLOAD_RX_ENCAP_CSUM
 * - VIRTCHNL_VF_OFFLOAD_USO
 */

/* default opcodes to communicate with VF */
static const u32 default_allowlist_opcodes[] = {
	VIRTCHNL_OP_GET_VF_RESOURCES, VIRTCHNL_OP_VERSION, VIRTCHNL_OP_RESET_VF,
};

/* opcodes supported after successful VIRTCHNL_OP_GET_VF_RESOURCES */
static const u32 working_allowlist_opcodes[] = {
	VIRTCHNL_OP_CONFIG_TX_QUEUE, VIRTCHNL_OP_CONFIG_RX_QUEUE,
	VIRTCHNL_OP_CONFIG_VSI_QUEUES, VIRTCHNL_OP_CONFIG_IRQ_MAP,
	VIRTCHNL_OP_ENABLE_QUEUES, VIRTCHNL_OP_DISABLE_QUEUES,
	VIRTCHNL_OP_GET_STATS, VIRTCHNL_OP_EVENT,
};

/* VIRTCHNL_VF_OFFLOAD_L2 */
static const u32 l2_allowlist_opcodes[] = {
	VIRTCHNL_OP_ADD_ETH_ADDR, VIRTCHNL_OP_DEL_ETH_ADDR,
	VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE,
};

/* VIRTCHNL_VF_OFFLOAD_REQ_QUEUES */
static const u32 req_queues_allowlist_opcodes[] = {
	VIRTCHNL_OP_REQUEST_QUEUES,
};

/* VIRTCHNL_VF_OFFLOAD_VLAN */
static const u32 vlan_allowlist_opcodes[] = {
	VIRTCHNL_OP_ADD_VLAN, VIRTCHNL_OP_DEL_VLAN,
	VIRTCHNL_OP_ENABLE_VLAN_STRIPPING, VIRTCHNL_OP_DISABLE_VLAN_STRIPPING,
};

/* VIRTCHNL_VF_OFFLOAD_VLAN_V2 */
static const u32 vlan_v2_allowlist_opcodes[] = {
	VIRTCHNL_OP_GET_OFFLOAD_VLAN_V2_CAPS, VIRTCHNL_OP_ADD_VLAN_V2,
	VIRTCHNL_OP_DEL_VLAN_V2, VIRTCHNL_OP_ENABLE_VLAN_STRIPPING_V2,
	VIRTCHNL_OP_DISABLE_VLAN_STRIPPING_V2,
	VIRTCHNL_OP_ENABLE_VLAN_INSERTION_V2,
	VIRTCHNL_OP_DISABLE_VLAN_INSERTION_V2,
};

/* VIRTCHNL_VF_OFFLOAD_RSS_PF */
static const u32 rss_pf_allowlist_opcodes[] = {
	VIRTCHNL_OP_CONFIG_RSS_KEY, VIRTCHNL_OP_CONFIG_RSS_LUT,
	VIRTCHNL_OP_GET_RSS_HASHCFG_CAPS, VIRTCHNL_OP_SET_RSS_HASHCFG,
	VIRTCHNL_OP_CONFIG_RSS_HFUNC,
};

/* VIRTCHNL_VF_OFFLOAD_RX_FLEX_DESC */
static const u32 rx_flex_desc_allowlist_opcodes[] = {
	VIRTCHNL_OP_GET_SUPPORTED_RXDIDS,
};

/* VIRTCHNL_VF_OFFLOAD_ADV_RSS_PF */
static const u32 adv_rss_pf_allowlist_opcodes[] = {
	VIRTCHNL_OP_ADD_RSS_CFG, VIRTCHNL_OP_DEL_RSS_CFG,
};

/* VIRTCHNL_VF_OFFLOAD_FDIR_PF */
static const u32 fdir_pf_allowlist_opcodes[] = {
	VIRTCHNL_OP_ADD_FDIR_FILTER, VIRTCHNL_OP_DEL_FDIR_FILTER,
};

/* VIRTCHNL_VF_CAP_PTP */
static const u32 ptp_allowlist_opcodes[] = {
	VIRTCHNL_OP_1588_PTP_GET_CAPS,
	VIRTCHNL_OP_1588_PTP_GET_TIME,
};

static const u32 tc_allowlist_opcodes[] = {
	VIRTCHNL_OP_GET_QOS_CAPS, VIRTCHNL_OP_CONFIG_QUEUE_BW,
	VIRTCHNL_OP_CONFIG_QUANTA,
};

struct allowlist_opcode_info {
	const u32 *opcodes;
	size_t size;
};

#define BIT_INDEX(caps) (HWEIGHT((caps) - 1))
#define ALLOW_ITEM(caps, list) \
	[BIT_INDEX(caps)] = { \
		.opcodes = list, \
		.size = ARRAY_SIZE(list) \
	}
static const struct allowlist_opcode_info allowlist_opcodes[] = {
	ALLOW_ITEM(VIRTCHNL_VF_OFFLOAD_L2, l2_allowlist_opcodes),
	ALLOW_ITEM(VIRTCHNL_VF_OFFLOAD_REQ_QUEUES, req_queues_allowlist_opcodes),
	ALLOW_ITEM(VIRTCHNL_VF_OFFLOAD_VLAN, vlan_allowlist_opcodes),
	ALLOW_ITEM(VIRTCHNL_VF_OFFLOAD_RSS_PF, rss_pf_allowlist_opcodes),
	ALLOW_ITEM(VIRTCHNL_VF_OFFLOAD_RX_FLEX_DESC, rx_flex_desc_allowlist_opcodes),
	ALLOW_ITEM(VIRTCHNL_VF_OFFLOAD_ADV_RSS_PF, adv_rss_pf_allowlist_opcodes),
	ALLOW_ITEM(VIRTCHNL_VF_OFFLOAD_FDIR_PF, fdir_pf_allowlist_opcodes),
	ALLOW_ITEM(VIRTCHNL_VF_OFFLOAD_VLAN_V2, vlan_v2_allowlist_opcodes),
	ALLOW_ITEM(VIRTCHNL_VF_OFFLOAD_QOS, tc_allowlist_opcodes),
	ALLOW_ITEM(VIRTCHNL_VF_CAP_PTP, ptp_allowlist_opcodes),
};

/**
 * ice_vc_is_opcode_allowed - check if this opcode is allowed on this VF
 * @vf: pointer to VF structure
 * @opcode: virtchnl opcode
 *
 * Return true if message is allowed on this VF
 */
bool ice_vc_is_opcode_allowed(struct ice_vf *vf, u32 opcode)
{
	if (opcode >= VIRTCHNL_OP_MAX)
		return false;

	return test_bit(opcode, vf->opcodes_allowlist);
}

/**
 * ice_vc_allowlist_opcodes - allowlist selected opcodes
 * @vf: pointer to VF structure
 * @opcodes: array of opocodes to allowlist
 * @size: size of opcodes array
 *
 * Function should be called to allowlist opcodes on VF.
 */
static void
ice_vc_allowlist_opcodes(struct ice_vf *vf, const u32 *opcodes, size_t size)
{
	unsigned int i;

	for (i = 0; i < size; i++)
		set_bit(opcodes[i], vf->opcodes_allowlist);
}

/**
 * ice_vc_clear_allowlist - clear all allowlist opcodes
 * @vf: pointer to VF structure
 */
static void ice_vc_clear_allowlist(struct ice_vf *vf)
{
	bitmap_zero(vf->opcodes_allowlist, VIRTCHNL_OP_MAX);
}

/**
 * ice_vc_set_default_allowlist - allowlist default opcodes for VF
 * @vf: pointer to VF structure
 */
void ice_vc_set_default_allowlist(struct ice_vf *vf)
{
	ice_vc_clear_allowlist(vf);
	ice_vc_allowlist_opcodes(vf, default_allowlist_opcodes,
				 ARRAY_SIZE(default_allowlist_opcodes));
}

/**
 * ice_vc_set_working_allowlist - allowlist opcodes needed to by VF to work
 * @vf: pointer to VF structure
 *
 * allowlist opcodes that aren't associated with specific caps, but
 * are needed by VF to work.
 */
void ice_vc_set_working_allowlist(struct ice_vf *vf)
{
	ice_vc_allowlist_opcodes(vf, working_allowlist_opcodes,
				 ARRAY_SIZE(working_allowlist_opcodes));
}

/**
 * ice_vc_set_caps_allowlist - allowlist VF opcodes according caps
 * @vf: pointer to VF structure
 */
void ice_vc_set_caps_allowlist(struct ice_vf *vf)
{
	unsigned long caps = vf->driver_caps;
	unsigned int i;

	for_each_set_bit(i, &caps, ARRAY_SIZE(allowlist_opcodes))
		ice_vc_allowlist_opcodes(vf, allowlist_opcodes[i].opcodes,
					 allowlist_opcodes[i].size);
}
