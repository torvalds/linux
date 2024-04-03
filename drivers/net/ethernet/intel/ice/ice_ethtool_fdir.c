// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2023, Intel Corporation. */

/* flow director ethtool support for ice */

#include "ice.h"
#include "ice_lib.h"
#include "ice_fdir.h"
#include "ice_flow.h"

static struct in6_addr full_ipv6_addr_mask = {
	.in6_u = {
		.u6_addr8 = {
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		}
	}
};

static struct in6_addr zero_ipv6_addr_mask = {
	.in6_u = {
		.u6_addr8 = {
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		}
	}
};

/* calls to ice_flow_add_prof require the number of segments in the array
 * for segs_cnt. In this code that is one more than the index.
 */
#define TNL_SEG_CNT(_TNL_) ((_TNL_) + 1)

/**
 * ice_fltr_to_ethtool_flow - convert filter type values to ethtool
 * flow type values
 * @flow: filter type to be converted
 *
 * Returns the corresponding ethtool flow type.
 */
static int ice_fltr_to_ethtool_flow(enum ice_fltr_ptype flow)
{
	switch (flow) {
	case ICE_FLTR_PTYPE_NONF_ETH:
		return ETHER_FLOW;
	case ICE_FLTR_PTYPE_NONF_IPV4_TCP:
		return TCP_V4_FLOW;
	case ICE_FLTR_PTYPE_NONF_IPV4_UDP:
		return UDP_V4_FLOW;
	case ICE_FLTR_PTYPE_NONF_IPV4_SCTP:
		return SCTP_V4_FLOW;
	case ICE_FLTR_PTYPE_NONF_IPV4_OTHER:
		return IPV4_USER_FLOW;
	case ICE_FLTR_PTYPE_NONF_IPV6_TCP:
		return TCP_V6_FLOW;
	case ICE_FLTR_PTYPE_NONF_IPV6_UDP:
		return UDP_V6_FLOW;
	case ICE_FLTR_PTYPE_NONF_IPV6_SCTP:
		return SCTP_V6_FLOW;
	case ICE_FLTR_PTYPE_NONF_IPV6_OTHER:
		return IPV6_USER_FLOW;
	default:
		/* 0 is undefined ethtool flow */
		return 0;
	}
}

/**
 * ice_ethtool_flow_to_fltr - convert ethtool flow type to filter enum
 * @eth: Ethtool flow type to be converted
 *
 * Returns flow enum
 */
static enum ice_fltr_ptype ice_ethtool_flow_to_fltr(int eth)
{
	switch (eth) {
	case ETHER_FLOW:
		return ICE_FLTR_PTYPE_NONF_ETH;
	case TCP_V4_FLOW:
		return ICE_FLTR_PTYPE_NONF_IPV4_TCP;
	case UDP_V4_FLOW:
		return ICE_FLTR_PTYPE_NONF_IPV4_UDP;
	case SCTP_V4_FLOW:
		return ICE_FLTR_PTYPE_NONF_IPV4_SCTP;
	case IPV4_USER_FLOW:
		return ICE_FLTR_PTYPE_NONF_IPV4_OTHER;
	case TCP_V6_FLOW:
		return ICE_FLTR_PTYPE_NONF_IPV6_TCP;
	case UDP_V6_FLOW:
		return ICE_FLTR_PTYPE_NONF_IPV6_UDP;
	case SCTP_V6_FLOW:
		return ICE_FLTR_PTYPE_NONF_IPV6_SCTP;
	case IPV6_USER_FLOW:
		return ICE_FLTR_PTYPE_NONF_IPV6_OTHER;
	default:
		return ICE_FLTR_PTYPE_NONF_NONE;
	}
}

/**
 * ice_is_mask_valid - check mask field set
 * @mask: full mask to check
 * @field: field for which mask should be valid
 *
 * If the mask is fully set return true. If it is not valid for field return
 * false.
 */
static bool ice_is_mask_valid(u64 mask, u64 field)
{
	return (mask & field) == field;
}

/**
 * ice_get_ethtool_fdir_entry - fill ethtool structure with fdir filter data
 * @hw: hardware structure that contains filter list
 * @cmd: ethtool command data structure to receive the filter data
 *
 * Returns 0 on success and -EINVAL on failure
 */
int ice_get_ethtool_fdir_entry(struct ice_hw *hw, struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp;
	struct ice_fdir_fltr *rule;
	int ret = 0;
	u16 idx;

	fsp = (struct ethtool_rx_flow_spec *)&cmd->fs;

	mutex_lock(&hw->fdir_fltr_lock);

	rule = ice_fdir_find_fltr_by_idx(hw, fsp->location);

	if (!rule || fsp->location != rule->fltr_id) {
		ret = -EINVAL;
		goto release_lock;
	}

	fsp->flow_type = ice_fltr_to_ethtool_flow(rule->flow_type);

	memset(&fsp->m_u, 0, sizeof(fsp->m_u));
	memset(&fsp->m_ext, 0, sizeof(fsp->m_ext));

	switch (fsp->flow_type) {
	case ETHER_FLOW:
		fsp->h_u.ether_spec = rule->eth;
		fsp->m_u.ether_spec = rule->eth_mask;
		break;
	case IPV4_USER_FLOW:
		fsp->h_u.usr_ip4_spec.ip_ver = ETH_RX_NFC_IP4;
		fsp->h_u.usr_ip4_spec.proto = 0;
		fsp->h_u.usr_ip4_spec.l4_4_bytes = rule->ip.v4.l4_header;
		fsp->h_u.usr_ip4_spec.tos = rule->ip.v4.tos;
		fsp->h_u.usr_ip4_spec.ip4src = rule->ip.v4.src_ip;
		fsp->h_u.usr_ip4_spec.ip4dst = rule->ip.v4.dst_ip;
		fsp->m_u.usr_ip4_spec.ip4src = rule->mask.v4.src_ip;
		fsp->m_u.usr_ip4_spec.ip4dst = rule->mask.v4.dst_ip;
		fsp->m_u.usr_ip4_spec.ip_ver = 0xFF;
		fsp->m_u.usr_ip4_spec.proto = 0;
		fsp->m_u.usr_ip4_spec.l4_4_bytes = rule->mask.v4.l4_header;
		fsp->m_u.usr_ip4_spec.tos = rule->mask.v4.tos;
		break;
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
		fsp->h_u.tcp_ip4_spec.psrc = rule->ip.v4.src_port;
		fsp->h_u.tcp_ip4_spec.pdst = rule->ip.v4.dst_port;
		fsp->h_u.tcp_ip4_spec.ip4src = rule->ip.v4.src_ip;
		fsp->h_u.tcp_ip4_spec.ip4dst = rule->ip.v4.dst_ip;
		fsp->m_u.tcp_ip4_spec.psrc = rule->mask.v4.src_port;
		fsp->m_u.tcp_ip4_spec.pdst = rule->mask.v4.dst_port;
		fsp->m_u.tcp_ip4_spec.ip4src = rule->mask.v4.src_ip;
		fsp->m_u.tcp_ip4_spec.ip4dst = rule->mask.v4.dst_ip;
		break;
	case IPV6_USER_FLOW:
		fsp->h_u.usr_ip6_spec.l4_4_bytes = rule->ip.v6.l4_header;
		fsp->h_u.usr_ip6_spec.tclass = rule->ip.v6.tc;
		fsp->h_u.usr_ip6_spec.l4_proto = rule->ip.v6.proto;
		memcpy(fsp->h_u.tcp_ip6_spec.ip6src, rule->ip.v6.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->h_u.tcp_ip6_spec.ip6dst, rule->ip.v6.dst_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->m_u.tcp_ip6_spec.ip6src, rule->mask.v6.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->m_u.tcp_ip6_spec.ip6dst, rule->mask.v6.dst_ip,
		       sizeof(struct in6_addr));
		fsp->m_u.usr_ip6_spec.l4_4_bytes = rule->mask.v6.l4_header;
		fsp->m_u.usr_ip6_spec.tclass = rule->mask.v6.tc;
		fsp->m_u.usr_ip6_spec.l4_proto = rule->mask.v6.proto;
		break;
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
	case SCTP_V6_FLOW:
		memcpy(fsp->h_u.tcp_ip6_spec.ip6src, rule->ip.v6.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->h_u.tcp_ip6_spec.ip6dst, rule->ip.v6.dst_ip,
		       sizeof(struct in6_addr));
		fsp->h_u.tcp_ip6_spec.psrc = rule->ip.v6.src_port;
		fsp->h_u.tcp_ip6_spec.pdst = rule->ip.v6.dst_port;
		memcpy(fsp->m_u.tcp_ip6_spec.ip6src,
		       rule->mask.v6.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->m_u.tcp_ip6_spec.ip6dst,
		       rule->mask.v6.dst_ip,
		       sizeof(struct in6_addr));
		fsp->m_u.tcp_ip6_spec.psrc = rule->mask.v6.src_port;
		fsp->m_u.tcp_ip6_spec.pdst = rule->mask.v6.dst_port;
		fsp->h_u.tcp_ip6_spec.tclass = rule->ip.v6.tc;
		fsp->m_u.tcp_ip6_spec.tclass = rule->mask.v6.tc;
		break;
	default:
		break;
	}

	if (rule->dest_ctl == ICE_FLTR_PRGM_DESC_DEST_DROP_PKT)
		fsp->ring_cookie = RX_CLS_FLOW_DISC;
	else
		fsp->ring_cookie = rule->orig_q_index;

	idx = ice_ethtool_flow_to_fltr(fsp->flow_type);
	if (idx == ICE_FLTR_PTYPE_NONF_NONE) {
		dev_err(ice_hw_to_dev(hw), "Missing input index for flow_type %d\n",
			rule->flow_type);
		ret = -EINVAL;
	}

release_lock:
	mutex_unlock(&hw->fdir_fltr_lock);
	return ret;
}

/**
 * ice_get_fdir_fltr_ids - fill buffer with filter IDs of active filters
 * @hw: hardware structure containing the filter list
 * @cmd: ethtool command data structure
 * @rule_locs: ethtool array passed in from OS to receive filter IDs
 *
 * Returns 0 as expected for success by ethtool
 */
int
ice_get_fdir_fltr_ids(struct ice_hw *hw, struct ethtool_rxnfc *cmd,
		      u32 *rule_locs)
{
	struct ice_fdir_fltr *f_rule;
	unsigned int cnt = 0;
	int val = 0;

	/* report total rule count */
	cmd->data = ice_get_fdir_cnt_all(hw);

	mutex_lock(&hw->fdir_fltr_lock);

	list_for_each_entry(f_rule, &hw->fdir_list_head, fltr_node) {
		if (cnt == cmd->rule_cnt) {
			val = -EMSGSIZE;
			goto release_lock;
		}
		rule_locs[cnt] = f_rule->fltr_id;
		cnt++;
	}

release_lock:
	mutex_unlock(&hw->fdir_fltr_lock);
	if (!val)
		cmd->rule_cnt = cnt;
	return val;
}

/**
 * ice_fdir_remap_entries - update the FDir entries in profile
 * @prof: FDir structure pointer
 * @tun: tunneled or non-tunneled packet
 * @idx: FDir entry index
 */
static void
ice_fdir_remap_entries(struct ice_fd_hw_prof *prof, int tun, int idx)
{
	if (idx != prof->cnt && tun < ICE_FD_HW_SEG_MAX) {
		int i;

		for (i = idx; i < (prof->cnt - 1); i++) {
			u64 old_entry_h;

			old_entry_h = prof->entry_h[i + 1][tun];
			prof->entry_h[i][tun] = old_entry_h;
			prof->vsi_h[i] = prof->vsi_h[i + 1];
		}

		prof->entry_h[i][tun] = 0;
		prof->vsi_h[i] = 0;
	}
}

/**
 * ice_fdir_rem_adq_chnl - remove an ADQ channel from HW filter rules
 * @hw: hardware structure containing filter list
 * @vsi_idx: VSI handle
 */
void ice_fdir_rem_adq_chnl(struct ice_hw *hw, u16 vsi_idx)
{
	int status, flow;

	if (!hw->fdir_prof)
		return;

	for (flow = 0; flow < ICE_FLTR_PTYPE_MAX; flow++) {
		struct ice_fd_hw_prof *prof = hw->fdir_prof[flow];
		int tun, i;

		if (!prof || !prof->cnt)
			continue;

		for (tun = 0; tun < ICE_FD_HW_SEG_MAX; tun++) {
			u64 prof_id = prof->prof_id[tun];

			for (i = 0; i < prof->cnt; i++) {
				if (prof->vsi_h[i] != vsi_idx)
					continue;

				prof->entry_h[i][tun] = 0;
				prof->vsi_h[i] = 0;
				break;
			}

			/* after clearing FDir entries update the remaining */
			ice_fdir_remap_entries(prof, tun, i);

			/* find flow profile corresponding to prof_id and clear
			 * vsi_idx from bitmap.
			 */
			status = ice_flow_rem_vsi_prof(hw, vsi_idx, prof_id);
			if (status) {
				dev_err(ice_hw_to_dev(hw), "ice_flow_rem_vsi_prof() failed status=%d\n",
					status);
			}
		}
		prof->cnt--;
	}
}

/**
 * ice_fdir_get_hw_prof - return the ice_fd_hw_proc associated with a flow
 * @hw: hardware structure containing the filter list
 * @blk: hardware block
 * @flow: FDir flow type to release
 */
static struct ice_fd_hw_prof *
ice_fdir_get_hw_prof(struct ice_hw *hw, enum ice_block blk, int flow)
{
	if (blk == ICE_BLK_FD && hw->fdir_prof)
		return hw->fdir_prof[flow];

	return NULL;
}

/**
 * ice_fdir_erase_flow_from_hw - remove a flow from the HW profile tables
 * @hw: hardware structure containing the filter list
 * @blk: hardware block
 * @flow: FDir flow type to release
 */
static void
ice_fdir_erase_flow_from_hw(struct ice_hw *hw, enum ice_block blk, int flow)
{
	struct ice_fd_hw_prof *prof = ice_fdir_get_hw_prof(hw, blk, flow);
	int tun;

	if (!prof)
		return;

	for (tun = 0; tun < ICE_FD_HW_SEG_MAX; tun++) {
		u64 prof_id = prof->prof_id[tun];
		int j;

		for (j = 0; j < prof->cnt; j++) {
			u16 vsi_num;

			if (!prof->entry_h[j][tun] || !prof->vsi_h[j])
				continue;
			vsi_num = ice_get_hw_vsi_num(hw, prof->vsi_h[j]);
			ice_rem_prof_id_flow(hw, blk, vsi_num, prof_id);
			ice_flow_rem_entry(hw, blk, prof->entry_h[j][tun]);
			prof->entry_h[j][tun] = 0;
		}
		ice_flow_rem_prof(hw, blk, prof_id);
	}
}

/**
 * ice_fdir_rem_flow - release the ice_flow structures for a filter type
 * @hw: hardware structure containing the filter list
 * @blk: hardware block
 * @flow_type: FDir flow type to release
 */
static void
ice_fdir_rem_flow(struct ice_hw *hw, enum ice_block blk,
		  enum ice_fltr_ptype flow_type)
{
	int flow = (int)flow_type & ~FLOW_EXT;
	struct ice_fd_hw_prof *prof;
	int tun, i;

	prof = ice_fdir_get_hw_prof(hw, blk, flow);
	if (!prof)
		return;

	ice_fdir_erase_flow_from_hw(hw, blk, flow);
	for (i = 0; i < prof->cnt; i++)
		prof->vsi_h[i] = 0;
	for (tun = 0; tun < ICE_FD_HW_SEG_MAX; tun++) {
		if (!prof->fdir_seg[tun])
			continue;
		devm_kfree(ice_hw_to_dev(hw), prof->fdir_seg[tun]);
		prof->fdir_seg[tun] = NULL;
	}
	prof->cnt = 0;
}

/**
 * ice_fdir_release_flows - release all flows in use for later replay
 * @hw: pointer to HW instance
 */
void ice_fdir_release_flows(struct ice_hw *hw)
{
	int flow;

	/* release Flow Director HW table entries */
	for (flow = 0; flow < ICE_FLTR_PTYPE_MAX; flow++)
		ice_fdir_erase_flow_from_hw(hw, ICE_BLK_FD, flow);
}

/**
 * ice_fdir_replay_flows - replay HW Flow Director filter info
 * @hw: pointer to HW instance
 */
void ice_fdir_replay_flows(struct ice_hw *hw)
{
	int flow;

	for (flow = 0; flow < ICE_FLTR_PTYPE_MAX; flow++) {
		int tun;

		if (!hw->fdir_prof[flow] || !hw->fdir_prof[flow]->cnt)
			continue;
		for (tun = 0; tun < ICE_FD_HW_SEG_MAX; tun++) {
			struct ice_flow_prof *hw_prof;
			struct ice_fd_hw_prof *prof;
			int j;

			prof = hw->fdir_prof[flow];
			ice_flow_add_prof(hw, ICE_BLK_FD, ICE_FLOW_RX,
					  prof->fdir_seg[tun], TNL_SEG_CNT(tun),
					  false, &hw_prof);
			for (j = 0; j < prof->cnt; j++) {
				enum ice_flow_priority prio;
				u64 entry_h = 0;
				int err;

				prio = ICE_FLOW_PRIO_NORMAL;
				err = ice_flow_add_entry(hw, ICE_BLK_FD,
							 hw_prof->id,
							 prof->vsi_h[0],
							 prof->vsi_h[j],
							 prio, prof->fdir_seg,
							 &entry_h);
				if (err) {
					dev_err(ice_hw_to_dev(hw), "Could not replay Flow Director, flow type %d\n",
						flow);
					continue;
				}
				prof->prof_id[tun] = hw_prof->id;
				prof->entry_h[j][tun] = entry_h;
			}
		}
	}
}

/**
 * ice_parse_rx_flow_user_data - deconstruct user-defined data
 * @fsp: pointer to ethtool Rx flow specification
 * @data: pointer to userdef data structure for storage
 *
 * Returns 0 on success, negative error value on failure
 */
static int
ice_parse_rx_flow_user_data(struct ethtool_rx_flow_spec *fsp,
			    struct ice_rx_flow_userdef *data)
{
	u64 value, mask;

	memset(data, 0, sizeof(*data));
	if (!(fsp->flow_type & FLOW_EXT))
		return 0;

	value = be64_to_cpu(*((__force __be64 *)fsp->h_ext.data));
	mask = be64_to_cpu(*((__force __be64 *)fsp->m_ext.data));
	if (!mask)
		return 0;

#define ICE_USERDEF_FLEX_WORD_M	GENMASK_ULL(15, 0)
#define ICE_USERDEF_FLEX_OFFS_S	16
#define ICE_USERDEF_FLEX_OFFS_M	GENMASK_ULL(31, ICE_USERDEF_FLEX_OFFS_S)
#define ICE_USERDEF_FLEX_FLTR_M	GENMASK_ULL(31, 0)

	/* 0x1fe is the maximum value for offsets stored in the internal
	 * filtering tables.
	 */
#define ICE_USERDEF_FLEX_MAX_OFFS_VAL 0x1fe

	if (!ice_is_mask_valid(mask, ICE_USERDEF_FLEX_FLTR_M) ||
	    value > ICE_USERDEF_FLEX_FLTR_M)
		return -EINVAL;

	data->flex_word = value & ICE_USERDEF_FLEX_WORD_M;
	data->flex_offset = FIELD_GET(ICE_USERDEF_FLEX_OFFS_M, value);
	if (data->flex_offset > ICE_USERDEF_FLEX_MAX_OFFS_VAL)
		return -EINVAL;

	data->flex_fltr = true;

	return 0;
}

/**
 * ice_fdir_num_avail_fltr - return the number of unused flow director filters
 * @hw: pointer to hardware structure
 * @vsi: software VSI structure
 *
 * There are 2 filter pools: guaranteed and best effort(shared). Each VSI can
 * use filters from either pool. The guaranteed pool is divided between VSIs.
 * The best effort filter pool is common to all VSIs and is a device shared
 * resource pool. The number of filters available to this VSI is the sum of
 * the VSIs guaranteed filter pool and the global available best effort
 * filter pool.
 *
 * Returns the number of available flow director filters to this VSI
 */
static int ice_fdir_num_avail_fltr(struct ice_hw *hw, struct ice_vsi *vsi)
{
	u16 vsi_num = ice_get_hw_vsi_num(hw, vsi->idx);
	u16 num_guar;
	u16 num_be;

	/* total guaranteed filters assigned to this VSI */
	num_guar = vsi->num_gfltr;

	/* total global best effort filters */
	num_be = hw->func_caps.fd_fltr_best_effort;

	/* Subtract the number of programmed filters from the global values */
	switch (hw->mac_type) {
	case ICE_MAC_E830:
		num_guar -= FIELD_GET(E830_VSIQF_FD_CNT_FD_GCNT_M,
				      rd32(hw, VSIQF_FD_CNT(vsi_num)));
		num_be -= FIELD_GET(E830_GLQF_FD_CNT_FD_BCNT_M,
				    rd32(hw, GLQF_FD_CNT));
		break;
	case ICE_MAC_E810:
	default:
		num_guar -= FIELD_GET(E800_VSIQF_FD_CNT_FD_GCNT_M,
				      rd32(hw, VSIQF_FD_CNT(vsi_num)));
		num_be -= FIELD_GET(E800_GLQF_FD_CNT_FD_BCNT_M,
				    rd32(hw, GLQF_FD_CNT));
	}

	return num_guar + num_be;
}

/**
 * ice_fdir_alloc_flow_prof - allocate FDir flow profile structure(s)
 * @hw: HW structure containing the FDir flow profile structure(s)
 * @flow: flow type to allocate the flow profile for
 *
 * Allocate the fdir_prof and fdir_prof[flow] if not already created. Return 0
 * on success and negative on error.
 */
static int
ice_fdir_alloc_flow_prof(struct ice_hw *hw, enum ice_fltr_ptype flow)
{
	if (!hw)
		return -EINVAL;

	if (!hw->fdir_prof) {
		hw->fdir_prof = devm_kcalloc(ice_hw_to_dev(hw),
					     ICE_FLTR_PTYPE_MAX,
					     sizeof(*hw->fdir_prof),
					     GFP_KERNEL);
		if (!hw->fdir_prof)
			return -ENOMEM;
	}

	if (!hw->fdir_prof[flow]) {
		hw->fdir_prof[flow] = devm_kzalloc(ice_hw_to_dev(hw),
						   sizeof(**hw->fdir_prof),
						   GFP_KERNEL);
		if (!hw->fdir_prof[flow])
			return -ENOMEM;
	}

	return 0;
}

/**
 * ice_fdir_prof_vsi_idx - find or insert a vsi_idx in structure
 * @prof: pointer to flow director HW profile
 * @vsi_idx: vsi_idx to locate
 *
 * return the index of the vsi_idx. if vsi_idx is not found insert it
 * into the vsi_h table.
 */
static u16
ice_fdir_prof_vsi_idx(struct ice_fd_hw_prof *prof, int vsi_idx)
{
	u16 idx = 0;

	for (idx = 0; idx < prof->cnt; idx++)
		if (prof->vsi_h[idx] == vsi_idx)
			return idx;

	if (idx == prof->cnt)
		prof->vsi_h[prof->cnt++] = vsi_idx;
	return idx;
}

/**
 * ice_fdir_set_hw_fltr_rule - Configure HW tables to generate a FDir rule
 * @pf: pointer to the PF structure
 * @seg: protocol header description pointer
 * @flow: filter enum
 * @tun: FDir segment to program
 */
static int
ice_fdir_set_hw_fltr_rule(struct ice_pf *pf, struct ice_flow_seg_info *seg,
			  enum ice_fltr_ptype flow, enum ice_fd_hw_seg tun)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_vsi *main_vsi, *ctrl_vsi;
	struct ice_flow_seg_info *old_seg;
	struct ice_flow_prof *prof = NULL;
	struct ice_fd_hw_prof *hw_prof;
	struct ice_hw *hw = &pf->hw;
	u64 entry1_h = 0;
	u64 entry2_h = 0;
	bool del_last;
	int err;
	int idx;

	main_vsi = ice_get_main_vsi(pf);
	if (!main_vsi)
		return -EINVAL;

	ctrl_vsi = ice_get_ctrl_vsi(pf);
	if (!ctrl_vsi)
		return -EINVAL;

	err = ice_fdir_alloc_flow_prof(hw, flow);
	if (err)
		return err;

	hw_prof = hw->fdir_prof[flow];
	old_seg = hw_prof->fdir_seg[tun];
	if (old_seg) {
		/* This flow_type already has a changed input set.
		 * If it matches the requested input set then we are
		 * done. Or, if it's different then it's an error.
		 */
		if (!memcmp(old_seg, seg, sizeof(*seg)))
			return -EEXIST;

		/* if there are FDir filters using this flow,
		 * then return error.
		 */
		if (hw->fdir_fltr_cnt[flow]) {
			dev_err(dev, "Failed to add filter. Flow director filters on each port must have the same input set.\n");
			return -EINVAL;
		}

		if (ice_is_arfs_using_perfect_flow(hw, flow)) {
			dev_err(dev, "aRFS using perfect flow type %d, cannot change input set\n",
				flow);
			return -EINVAL;
		}

		/* remove HW filter definition */
		ice_fdir_rem_flow(hw, ICE_BLK_FD, flow);
	}

	/* Adding a profile, but there is only one header supported.
	 * That is the final parameters are 1 header (segment), no
	 * actions (NULL) and zero actions 0.
	 */
	err = ice_flow_add_prof(hw, ICE_BLK_FD, ICE_FLOW_RX, seg,
				TNL_SEG_CNT(tun), false, &prof);
	if (err)
		return err;
	err = ice_flow_add_entry(hw, ICE_BLK_FD, prof->id, main_vsi->idx,
				 main_vsi->idx, ICE_FLOW_PRIO_NORMAL,
				 seg, &entry1_h);
	if (err)
		goto err_prof;
	err = ice_flow_add_entry(hw, ICE_BLK_FD, prof->id, main_vsi->idx,
				 ctrl_vsi->idx, ICE_FLOW_PRIO_NORMAL,
				 seg, &entry2_h);
	if (err)
		goto err_entry;

	hw_prof->fdir_seg[tun] = seg;
	hw_prof->prof_id[tun] = prof->id;
	hw_prof->entry_h[0][tun] = entry1_h;
	hw_prof->entry_h[1][tun] = entry2_h;
	hw_prof->vsi_h[0] = main_vsi->idx;
	hw_prof->vsi_h[1] = ctrl_vsi->idx;
	if (!hw_prof->cnt)
		hw_prof->cnt = 2;

	for (idx = 1; idx < ICE_CHNL_MAX_TC; idx++) {
		u16 vsi_idx;
		u16 vsi_h;

		if (!ice_is_adq_active(pf) || !main_vsi->tc_map_vsi[idx])
			continue;

		entry1_h = 0;
		vsi_h = main_vsi->tc_map_vsi[idx]->idx;
		err = ice_flow_add_entry(hw, ICE_BLK_FD, prof->id,
					 main_vsi->idx, vsi_h,
					 ICE_FLOW_PRIO_NORMAL, seg,
					 &entry1_h);
		if (err) {
			dev_err(dev, "Could not add Channel VSI %d to flow group\n",
				idx);
			goto err_unroll;
		}

		vsi_idx = ice_fdir_prof_vsi_idx(hw_prof,
						main_vsi->tc_map_vsi[idx]->idx);
		hw_prof->entry_h[vsi_idx][tun] = entry1_h;
	}

	return 0;

err_unroll:
	entry1_h = 0;
	hw_prof->fdir_seg[tun] = NULL;

	/* The variable del_last will be used to determine when to clean up
	 * the VSI group data. The VSI data is not needed if there are no
	 * segments.
	 */
	del_last = true;
	for (idx = 0; idx < ICE_FD_HW_SEG_MAX; idx++)
		if (hw_prof->fdir_seg[idx]) {
			del_last = false;
			break;
		}

	for (idx = 0; idx < hw_prof->cnt; idx++) {
		u16 vsi_num = ice_get_hw_vsi_num(hw, hw_prof->vsi_h[idx]);

		if (!hw_prof->entry_h[idx][tun])
			continue;
		ice_rem_prof_id_flow(hw, ICE_BLK_FD, vsi_num, prof->id);
		ice_flow_rem_entry(hw, ICE_BLK_FD, hw_prof->entry_h[idx][tun]);
		hw_prof->entry_h[idx][tun] = 0;
		if (del_last)
			hw_prof->vsi_h[idx] = 0;
	}
	if (del_last)
		hw_prof->cnt = 0;
err_entry:
	ice_rem_prof_id_flow(hw, ICE_BLK_FD,
			     ice_get_hw_vsi_num(hw, main_vsi->idx), prof->id);
	ice_flow_rem_entry(hw, ICE_BLK_FD, entry1_h);
err_prof:
	ice_flow_rem_prof(hw, ICE_BLK_FD, prof->id);
	dev_err(dev, "Failed to add filter. Flow director filters on each port must have the same input set.\n");

	return err;
}

/**
 * ice_set_init_fdir_seg
 * @seg: flow segment for programming
 * @l3_proto: ICE_FLOW_SEG_HDR_IPV4 or ICE_FLOW_SEG_HDR_IPV6
 * @l4_proto: ICE_FLOW_SEG_HDR_TCP or ICE_FLOW_SEG_HDR_UDP
 *
 * Set the configuration for perfect filters to the provided flow segment for
 * programming the HW filter. This is to be called only when initializing
 * filters as this function it assumes no filters exist.
 */
static int
ice_set_init_fdir_seg(struct ice_flow_seg_info *seg,
		      enum ice_flow_seg_hdr l3_proto,
		      enum ice_flow_seg_hdr l4_proto)
{
	enum ice_flow_field src_addr, dst_addr, src_port, dst_port;

	if (!seg)
		return -EINVAL;

	if (l3_proto == ICE_FLOW_SEG_HDR_IPV4) {
		src_addr = ICE_FLOW_FIELD_IDX_IPV4_SA;
		dst_addr = ICE_FLOW_FIELD_IDX_IPV4_DA;
	} else if (l3_proto == ICE_FLOW_SEG_HDR_IPV6) {
		src_addr = ICE_FLOW_FIELD_IDX_IPV6_SA;
		dst_addr = ICE_FLOW_FIELD_IDX_IPV6_DA;
	} else {
		return -EINVAL;
	}

	if (l4_proto == ICE_FLOW_SEG_HDR_TCP) {
		src_port = ICE_FLOW_FIELD_IDX_TCP_SRC_PORT;
		dst_port = ICE_FLOW_FIELD_IDX_TCP_DST_PORT;
	} else if (l4_proto == ICE_FLOW_SEG_HDR_UDP) {
		src_port = ICE_FLOW_FIELD_IDX_UDP_SRC_PORT;
		dst_port = ICE_FLOW_FIELD_IDX_UDP_DST_PORT;
	} else {
		return -EINVAL;
	}

	ICE_FLOW_SET_HDRS(seg, l3_proto | l4_proto);

	/* IP source address */
	ice_flow_set_fld(seg, src_addr, ICE_FLOW_FLD_OFF_INVAL,
			 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL, false);

	/* IP destination address */
	ice_flow_set_fld(seg, dst_addr, ICE_FLOW_FLD_OFF_INVAL,
			 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL, false);

	/* Layer 4 source port */
	ice_flow_set_fld(seg, src_port, ICE_FLOW_FLD_OFF_INVAL,
			 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL, false);

	/* Layer 4 destination port */
	ice_flow_set_fld(seg, dst_port, ICE_FLOW_FLD_OFF_INVAL,
			 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL, false);

	return 0;
}

/**
 * ice_create_init_fdir_rule
 * @pf: PF structure
 * @flow: filter enum
 *
 * Return error value or 0 on success.
 */
static int
ice_create_init_fdir_rule(struct ice_pf *pf, enum ice_fltr_ptype flow)
{
	struct ice_flow_seg_info *seg, *tun_seg;
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	int ret;

	/* if there is already a filter rule for kind return -EINVAL */
	if (hw->fdir_prof && hw->fdir_prof[flow] &&
	    hw->fdir_prof[flow]->fdir_seg[0])
		return -EINVAL;

	seg = devm_kzalloc(dev, sizeof(*seg), GFP_KERNEL);
	if (!seg)
		return -ENOMEM;

	tun_seg = devm_kcalloc(dev, ICE_FD_HW_SEG_MAX, sizeof(*tun_seg),
			       GFP_KERNEL);
	if (!tun_seg) {
		devm_kfree(dev, seg);
		return -ENOMEM;
	}

	if (flow == ICE_FLTR_PTYPE_NONF_IPV4_TCP)
		ret = ice_set_init_fdir_seg(seg, ICE_FLOW_SEG_HDR_IPV4,
					    ICE_FLOW_SEG_HDR_TCP);
	else if (flow == ICE_FLTR_PTYPE_NONF_IPV4_UDP)
		ret = ice_set_init_fdir_seg(seg, ICE_FLOW_SEG_HDR_IPV4,
					    ICE_FLOW_SEG_HDR_UDP);
	else if (flow == ICE_FLTR_PTYPE_NONF_IPV6_TCP)
		ret = ice_set_init_fdir_seg(seg, ICE_FLOW_SEG_HDR_IPV6,
					    ICE_FLOW_SEG_HDR_TCP);
	else if (flow == ICE_FLTR_PTYPE_NONF_IPV6_UDP)
		ret = ice_set_init_fdir_seg(seg, ICE_FLOW_SEG_HDR_IPV6,
					    ICE_FLOW_SEG_HDR_UDP);
	else
		ret = -EINVAL;
	if (ret)
		goto err_exit;

	/* add filter for outer headers */
	ret = ice_fdir_set_hw_fltr_rule(pf, seg, flow, ICE_FD_HW_SEG_NON_TUN);
	if (ret)
		/* could not write filter, free memory */
		goto err_exit;

	/* make tunneled filter HW entries if possible */
	memcpy(&tun_seg[1], seg, sizeof(*seg));
	ret = ice_fdir_set_hw_fltr_rule(pf, tun_seg, flow, ICE_FD_HW_SEG_TUN);
	if (ret)
		/* could not write tunnel filter, but outer header filter
		 * exists
		 */
		devm_kfree(dev, tun_seg);

	set_bit(flow, hw->fdir_perfect_fltr);
	return ret;
err_exit:
	devm_kfree(dev, tun_seg);
	devm_kfree(dev, seg);

	return -EOPNOTSUPP;
}

/**
 * ice_set_fdir_ip4_seg
 * @seg: flow segment for programming
 * @tcp_ip4_spec: mask data from ethtool
 * @l4_proto: Layer 4 protocol to program
 * @perfect_fltr: only valid on success; returns true if perfect filter,
 *		  false if not
 *
 * Set the mask data into the flow segment to be used to program HW
 * table based on provided L4 protocol for IPv4
 */
static int
ice_set_fdir_ip4_seg(struct ice_flow_seg_info *seg,
		     struct ethtool_tcpip4_spec *tcp_ip4_spec,
		     enum ice_flow_seg_hdr l4_proto, bool *perfect_fltr)
{
	enum ice_flow_field src_port, dst_port;

	/* make sure we don't have any empty rule */
	if (!tcp_ip4_spec->psrc && !tcp_ip4_spec->ip4src &&
	    !tcp_ip4_spec->pdst && !tcp_ip4_spec->ip4dst)
		return -EINVAL;

	/* filtering on TOS not supported */
	if (tcp_ip4_spec->tos)
		return -EOPNOTSUPP;

	if (l4_proto == ICE_FLOW_SEG_HDR_TCP) {
		src_port = ICE_FLOW_FIELD_IDX_TCP_SRC_PORT;
		dst_port = ICE_FLOW_FIELD_IDX_TCP_DST_PORT;
	} else if (l4_proto == ICE_FLOW_SEG_HDR_UDP) {
		src_port = ICE_FLOW_FIELD_IDX_UDP_SRC_PORT;
		dst_port = ICE_FLOW_FIELD_IDX_UDP_DST_PORT;
	} else if (l4_proto == ICE_FLOW_SEG_HDR_SCTP) {
		src_port = ICE_FLOW_FIELD_IDX_SCTP_SRC_PORT;
		dst_port = ICE_FLOW_FIELD_IDX_SCTP_DST_PORT;
	} else {
		return -EOPNOTSUPP;
	}

	*perfect_fltr = true;
	ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_IPV4 | l4_proto);

	/* IP source address */
	if (tcp_ip4_spec->ip4src == htonl(0xFFFFFFFF))
		ice_flow_set_fld(seg, ICE_FLOW_FIELD_IDX_IPV4_SA,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);
	else if (!tcp_ip4_spec->ip4src)
		*perfect_fltr = false;
	else
		return -EOPNOTSUPP;

	/* IP destination address */
	if (tcp_ip4_spec->ip4dst == htonl(0xFFFFFFFF))
		ice_flow_set_fld(seg, ICE_FLOW_FIELD_IDX_IPV4_DA,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);
	else if (!tcp_ip4_spec->ip4dst)
		*perfect_fltr = false;
	else
		return -EOPNOTSUPP;

	/* Layer 4 source port */
	if (tcp_ip4_spec->psrc == htons(0xFFFF))
		ice_flow_set_fld(seg, src_port, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 false);
	else if (!tcp_ip4_spec->psrc)
		*perfect_fltr = false;
	else
		return -EOPNOTSUPP;

	/* Layer 4 destination port */
	if (tcp_ip4_spec->pdst == htons(0xFFFF))
		ice_flow_set_fld(seg, dst_port, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 false);
	else if (!tcp_ip4_spec->pdst)
		*perfect_fltr = false;
	else
		return -EOPNOTSUPP;

	return 0;
}

/**
 * ice_set_fdir_ip4_usr_seg
 * @seg: flow segment for programming
 * @usr_ip4_spec: ethtool userdef packet offset
 * @perfect_fltr: only valid on success; returns true if perfect filter,
 *		  false if not
 *
 * Set the offset data into the flow segment to be used to program HW
 * table for IPv4
 */
static int
ice_set_fdir_ip4_usr_seg(struct ice_flow_seg_info *seg,
			 struct ethtool_usrip4_spec *usr_ip4_spec,
			 bool *perfect_fltr)
{
	/* first 4 bytes of Layer 4 header */
	if (usr_ip4_spec->l4_4_bytes)
		return -EINVAL;
	if (usr_ip4_spec->tos)
		return -EINVAL;
	if (usr_ip4_spec->ip_ver)
		return -EINVAL;
	/* Filtering on Layer 4 protocol not supported */
	if (usr_ip4_spec->proto)
		return -EOPNOTSUPP;
	/* empty rules are not valid */
	if (!usr_ip4_spec->ip4src && !usr_ip4_spec->ip4dst)
		return -EINVAL;

	*perfect_fltr = true;
	ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_IPV4);

	/* IP source address */
	if (usr_ip4_spec->ip4src == htonl(0xFFFFFFFF))
		ice_flow_set_fld(seg, ICE_FLOW_FIELD_IDX_IPV4_SA,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);
	else if (!usr_ip4_spec->ip4src)
		*perfect_fltr = false;
	else
		return -EOPNOTSUPP;

	/* IP destination address */
	if (usr_ip4_spec->ip4dst == htonl(0xFFFFFFFF))
		ice_flow_set_fld(seg, ICE_FLOW_FIELD_IDX_IPV4_DA,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);
	else if (!usr_ip4_spec->ip4dst)
		*perfect_fltr = false;
	else
		return -EOPNOTSUPP;

	return 0;
}

/**
 * ice_set_fdir_ip6_seg
 * @seg: flow segment for programming
 * @tcp_ip6_spec: mask data from ethtool
 * @l4_proto: Layer 4 protocol to program
 * @perfect_fltr: only valid on success; returns true if perfect filter,
 *		  false if not
 *
 * Set the mask data into the flow segment to be used to program HW
 * table based on provided L4 protocol for IPv6
 */
static int
ice_set_fdir_ip6_seg(struct ice_flow_seg_info *seg,
		     struct ethtool_tcpip6_spec *tcp_ip6_spec,
		     enum ice_flow_seg_hdr l4_proto, bool *perfect_fltr)
{
	enum ice_flow_field src_port, dst_port;

	/* make sure we don't have any empty rule */
	if (!memcmp(tcp_ip6_spec->ip6src, &zero_ipv6_addr_mask,
		    sizeof(struct in6_addr)) &&
	    !memcmp(tcp_ip6_spec->ip6dst, &zero_ipv6_addr_mask,
		    sizeof(struct in6_addr)) &&
	    !tcp_ip6_spec->psrc && !tcp_ip6_spec->pdst)
		return -EINVAL;

	/* filtering on TC not supported */
	if (tcp_ip6_spec->tclass)
		return -EOPNOTSUPP;

	if (l4_proto == ICE_FLOW_SEG_HDR_TCP) {
		src_port = ICE_FLOW_FIELD_IDX_TCP_SRC_PORT;
		dst_port = ICE_FLOW_FIELD_IDX_TCP_DST_PORT;
	} else if (l4_proto == ICE_FLOW_SEG_HDR_UDP) {
		src_port = ICE_FLOW_FIELD_IDX_UDP_SRC_PORT;
		dst_port = ICE_FLOW_FIELD_IDX_UDP_DST_PORT;
	} else if (l4_proto == ICE_FLOW_SEG_HDR_SCTP) {
		src_port = ICE_FLOW_FIELD_IDX_SCTP_SRC_PORT;
		dst_port = ICE_FLOW_FIELD_IDX_SCTP_DST_PORT;
	} else {
		return -EINVAL;
	}

	*perfect_fltr = true;
	ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_IPV6 | l4_proto);

	if (!memcmp(tcp_ip6_spec->ip6src, &full_ipv6_addr_mask,
		    sizeof(struct in6_addr)))
		ice_flow_set_fld(seg, ICE_FLOW_FIELD_IDX_IPV6_SA,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);
	else if (!memcmp(tcp_ip6_spec->ip6src, &zero_ipv6_addr_mask,
			 sizeof(struct in6_addr)))
		*perfect_fltr = false;
	else
		return -EOPNOTSUPP;

	if (!memcmp(tcp_ip6_spec->ip6dst, &full_ipv6_addr_mask,
		    sizeof(struct in6_addr)))
		ice_flow_set_fld(seg, ICE_FLOW_FIELD_IDX_IPV6_DA,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);
	else if (!memcmp(tcp_ip6_spec->ip6dst, &zero_ipv6_addr_mask,
			 sizeof(struct in6_addr)))
		*perfect_fltr = false;
	else
		return -EOPNOTSUPP;

	/* Layer 4 source port */
	if (tcp_ip6_spec->psrc == htons(0xFFFF))
		ice_flow_set_fld(seg, src_port, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 false);
	else if (!tcp_ip6_spec->psrc)
		*perfect_fltr = false;
	else
		return -EOPNOTSUPP;

	/* Layer 4 destination port */
	if (tcp_ip6_spec->pdst == htons(0xFFFF))
		ice_flow_set_fld(seg, dst_port, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 false);
	else if (!tcp_ip6_spec->pdst)
		*perfect_fltr = false;
	else
		return -EOPNOTSUPP;

	return 0;
}

/**
 * ice_set_fdir_ip6_usr_seg
 * @seg: flow segment for programming
 * @usr_ip6_spec: ethtool userdef packet offset
 * @perfect_fltr: only valid on success; returns true if perfect filter,
 *		  false if not
 *
 * Set the offset data into the flow segment to be used to program HW
 * table for IPv6
 */
static int
ice_set_fdir_ip6_usr_seg(struct ice_flow_seg_info *seg,
			 struct ethtool_usrip6_spec *usr_ip6_spec,
			 bool *perfect_fltr)
{
	/* filtering on Layer 4 bytes not supported */
	if (usr_ip6_spec->l4_4_bytes)
		return -EOPNOTSUPP;
	/* filtering on TC not supported */
	if (usr_ip6_spec->tclass)
		return -EOPNOTSUPP;
	/* filtering on Layer 4 protocol not supported */
	if (usr_ip6_spec->l4_proto)
		return -EOPNOTSUPP;
	/* empty rules are not valid */
	if (!memcmp(usr_ip6_spec->ip6src, &zero_ipv6_addr_mask,
		    sizeof(struct in6_addr)) &&
	    !memcmp(usr_ip6_spec->ip6dst, &zero_ipv6_addr_mask,
		    sizeof(struct in6_addr)))
		return -EINVAL;

	*perfect_fltr = true;
	ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_IPV6);

	if (!memcmp(usr_ip6_spec->ip6src, &full_ipv6_addr_mask,
		    sizeof(struct in6_addr)))
		ice_flow_set_fld(seg, ICE_FLOW_FIELD_IDX_IPV6_SA,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);
	else if (!memcmp(usr_ip6_spec->ip6src, &zero_ipv6_addr_mask,
			 sizeof(struct in6_addr)))
		*perfect_fltr = false;
	else
		return -EOPNOTSUPP;

	if (!memcmp(usr_ip6_spec->ip6dst, &full_ipv6_addr_mask,
		    sizeof(struct in6_addr)))
		ice_flow_set_fld(seg, ICE_FLOW_FIELD_IDX_IPV6_DA,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);
	else if (!memcmp(usr_ip6_spec->ip6dst, &zero_ipv6_addr_mask,
			 sizeof(struct in6_addr)))
		*perfect_fltr = false;
	else
		return -EOPNOTSUPP;

	return 0;
}

/**
 * ice_fdir_vlan_valid - validate VLAN data for Flow Director rule
 * @dev: network interface device structure
 * @fsp: pointer to ethtool Rx flow specification
 *
 * Return: true if vlan data is valid, false otherwise
 */
static bool ice_fdir_vlan_valid(struct device *dev,
				struct ethtool_rx_flow_spec *fsp)
{
	if (fsp->m_ext.vlan_etype && !eth_type_vlan(fsp->h_ext.vlan_etype))
		return false;

	if (fsp->m_ext.vlan_tci && ntohs(fsp->h_ext.vlan_tci) >= VLAN_N_VID)
		return false;

	/* proto and vlan must have vlan-etype defined */
	if (fsp->m_u.ether_spec.h_proto && fsp->m_ext.vlan_tci &&
	    !fsp->m_ext.vlan_etype) {
		dev_warn(dev, "Filter with proto and vlan require also vlan-etype");
		return false;
	}

	return true;
}

/**
 * ice_set_ether_flow_seg - set address and protocol segments for ether flow
 * @dev: network interface device structure
 * @seg: flow segment for programming
 * @eth_spec: mask data from ethtool
 *
 * Return: 0 on success and errno in case of error.
 */
static int ice_set_ether_flow_seg(struct device *dev,
				  struct ice_flow_seg_info *seg,
				  struct ethhdr *eth_spec)
{
	ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_ETH);

	/* empty rules are not valid */
	if (is_zero_ether_addr(eth_spec->h_source) &&
	    is_zero_ether_addr(eth_spec->h_dest) &&
	    !eth_spec->h_proto)
		return -EINVAL;

	/* Ethertype */
	if (eth_spec->h_proto == htons(0xFFFF)) {
		ice_flow_set_fld(seg, ICE_FLOW_FIELD_IDX_ETH_TYPE,
				 ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);
	} else if (eth_spec->h_proto) {
		dev_warn(dev, "Only 0x0000 or 0xffff proto mask is allowed for flow-type ether");
		return -EOPNOTSUPP;
	}

	/* Source MAC address */
	if (is_broadcast_ether_addr(eth_spec->h_source))
		ice_flow_set_fld(seg, ICE_FLOW_FIELD_IDX_ETH_SA,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);
	else if (!is_zero_ether_addr(eth_spec->h_source))
		goto err_mask;

	/* Destination MAC address */
	if (is_broadcast_ether_addr(eth_spec->h_dest))
		ice_flow_set_fld(seg, ICE_FLOW_FIELD_IDX_ETH_DA,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);
	else if (!is_zero_ether_addr(eth_spec->h_dest))
		goto err_mask;

	return 0;

err_mask:
	dev_warn(dev, "Only 00:00:00:00:00:00 or ff:ff:ff:ff:ff:ff MAC address mask is allowed for flow-type ether");
	return -EOPNOTSUPP;
}

/**
 * ice_set_fdir_vlan_seg - set vlan segments for ether flow
 * @seg: flow segment for programming
 * @ext_masks: masks for additional RX flow fields
 *
 * Return: 0 on success and errno in case of error.
 */
static int
ice_set_fdir_vlan_seg(struct ice_flow_seg_info *seg,
		      struct ethtool_flow_ext *ext_masks)
{
	ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_VLAN);

	if (ext_masks->vlan_etype) {
		if (ext_masks->vlan_etype != htons(0xFFFF))
			return -EOPNOTSUPP;

		ice_flow_set_fld(seg, ICE_FLOW_FIELD_IDX_S_VLAN,
				 ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);
	}

	if (ext_masks->vlan_tci) {
		if (ext_masks->vlan_tci != htons(0xFFFF))
			return -EOPNOTSUPP;

		ice_flow_set_fld(seg, ICE_FLOW_FIELD_IDX_C_VLAN,
				 ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);
	}

	return 0;
}

/**
 * ice_cfg_fdir_xtrct_seq - Configure extraction sequence for the given filter
 * @pf: PF structure
 * @fsp: pointer to ethtool Rx flow specification
 * @user: user defined data from flow specification
 *
 * Returns 0 on success.
 */
static int
ice_cfg_fdir_xtrct_seq(struct ice_pf *pf, struct ethtool_rx_flow_spec *fsp,
		       struct ice_rx_flow_userdef *user)
{
	struct ice_flow_seg_info *seg, *tun_seg;
	struct device *dev = ice_pf_to_dev(pf);
	enum ice_fltr_ptype fltr_idx;
	struct ice_hw *hw = &pf->hw;
	bool perfect_filter = false;
	int ret;

	seg = devm_kzalloc(dev, sizeof(*seg), GFP_KERNEL);
	if (!seg)
		return -ENOMEM;

	tun_seg = devm_kcalloc(dev, ICE_FD_HW_SEG_MAX, sizeof(*tun_seg),
			       GFP_KERNEL);
	if (!tun_seg) {
		devm_kfree(dev, seg);
		return -ENOMEM;
	}

	switch (fsp->flow_type & ~FLOW_EXT) {
	case TCP_V4_FLOW:
		ret = ice_set_fdir_ip4_seg(seg, &fsp->m_u.tcp_ip4_spec,
					   ICE_FLOW_SEG_HDR_TCP,
					   &perfect_filter);
		break;
	case UDP_V4_FLOW:
		ret = ice_set_fdir_ip4_seg(seg, &fsp->m_u.tcp_ip4_spec,
					   ICE_FLOW_SEG_HDR_UDP,
					   &perfect_filter);
		break;
	case SCTP_V4_FLOW:
		ret = ice_set_fdir_ip4_seg(seg, &fsp->m_u.tcp_ip4_spec,
					   ICE_FLOW_SEG_HDR_SCTP,
					   &perfect_filter);
		break;
	case IPV4_USER_FLOW:
		ret = ice_set_fdir_ip4_usr_seg(seg, &fsp->m_u.usr_ip4_spec,
					       &perfect_filter);
		break;
	case TCP_V6_FLOW:
		ret = ice_set_fdir_ip6_seg(seg, &fsp->m_u.tcp_ip6_spec,
					   ICE_FLOW_SEG_HDR_TCP,
					   &perfect_filter);
		break;
	case UDP_V6_FLOW:
		ret = ice_set_fdir_ip6_seg(seg, &fsp->m_u.tcp_ip6_spec,
					   ICE_FLOW_SEG_HDR_UDP,
					   &perfect_filter);
		break;
	case SCTP_V6_FLOW:
		ret = ice_set_fdir_ip6_seg(seg, &fsp->m_u.tcp_ip6_spec,
					   ICE_FLOW_SEG_HDR_SCTP,
					   &perfect_filter);
		break;
	case IPV6_USER_FLOW:
		ret = ice_set_fdir_ip6_usr_seg(seg, &fsp->m_u.usr_ip6_spec,
					       &perfect_filter);
		break;
	case ETHER_FLOW:
		ret = ice_set_ether_flow_seg(dev, seg, &fsp->m_u.ether_spec);
		if (!ret && (fsp->m_ext.vlan_etype || fsp->m_ext.vlan_tci)) {
			if (!ice_fdir_vlan_valid(dev, fsp)) {
				ret = -EINVAL;
				break;
			}
			ret = ice_set_fdir_vlan_seg(seg, &fsp->m_ext);
		}
		break;
	default:
		ret = -EINVAL;
	}
	if (ret)
		goto err_exit;

	/* tunnel segments are shifted up one. */
	memcpy(&tun_seg[1], seg, sizeof(*seg));

	if (user && user->flex_fltr) {
		perfect_filter = false;
		ice_flow_add_fld_raw(seg, user->flex_offset,
				     ICE_FLTR_PRGM_FLEX_WORD_SIZE,
				     ICE_FLOW_FLD_OFF_INVAL,
				     ICE_FLOW_FLD_OFF_INVAL);
		ice_flow_add_fld_raw(&tun_seg[1], user->flex_offset,
				     ICE_FLTR_PRGM_FLEX_WORD_SIZE,
				     ICE_FLOW_FLD_OFF_INVAL,
				     ICE_FLOW_FLD_OFF_INVAL);
	}

	fltr_idx = ice_ethtool_flow_to_fltr(fsp->flow_type & ~FLOW_EXT);

	assign_bit(fltr_idx, hw->fdir_perfect_fltr, perfect_filter);

	/* add filter for outer headers */
	ret = ice_fdir_set_hw_fltr_rule(pf, seg, fltr_idx,
					ICE_FD_HW_SEG_NON_TUN);
	if (ret == -EEXIST) {
		/* Rule already exists, free memory and count as success */
		ret = 0;
		goto err_exit;
	} else if (ret) {
		/* could not write filter, free memory */
		goto err_exit;
	}

	/* make tunneled filter HW entries if possible */
	memcpy(&tun_seg[1], seg, sizeof(*seg));
	ret = ice_fdir_set_hw_fltr_rule(pf, tun_seg, fltr_idx,
					ICE_FD_HW_SEG_TUN);
	if (ret == -EEXIST) {
		/* Rule already exists, free memory and count as success */
		devm_kfree(dev, tun_seg);
		ret = 0;
	} else if (ret) {
		/* could not write tunnel filter, but outer filter exists */
		devm_kfree(dev, tun_seg);
	}

	return ret;

err_exit:
	devm_kfree(dev, tun_seg);
	devm_kfree(dev, seg);

	return ret;
}

/**
 * ice_update_per_q_fltr
 * @vsi: ptr to VSI
 * @q_index: queue index
 * @inc: true to increment or false to decrement per queue filter count
 *
 * This function is used to keep track of per queue sideband filters
 */
static void ice_update_per_q_fltr(struct ice_vsi *vsi, u32 q_index, bool inc)
{
	struct ice_rx_ring *rx_ring;

	if (!vsi->num_rxq || q_index >= vsi->num_rxq)
		return;

	rx_ring = vsi->rx_rings[q_index];
	if (!rx_ring || !rx_ring->ch)
		return;

	if (inc)
		atomic_inc(&rx_ring->ch->num_sb_fltr);
	else
		atomic_dec_if_positive(&rx_ring->ch->num_sb_fltr);
}

/**
 * ice_fdir_write_fltr - send a flow director filter to the hardware
 * @pf: PF data structure
 * @input: filter structure
 * @add: true adds filter and false removed filter
 * @is_tun: true adds inner filter on tunnel and false outer headers
 *
 * returns 0 on success and negative value on error
 */
int
ice_fdir_write_fltr(struct ice_pf *pf, struct ice_fdir_fltr *input, bool add,
		    bool is_tun)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	struct ice_fltr_desc desc;
	struct ice_vsi *ctrl_vsi;
	u8 *pkt, *frag_pkt;
	bool has_frag;
	int err;

	ctrl_vsi = ice_get_ctrl_vsi(pf);
	if (!ctrl_vsi)
		return -EINVAL;

	pkt = devm_kzalloc(dev, ICE_FDIR_MAX_RAW_PKT_SIZE, GFP_KERNEL);
	if (!pkt)
		return -ENOMEM;
	frag_pkt = devm_kzalloc(dev, ICE_FDIR_MAX_RAW_PKT_SIZE, GFP_KERNEL);
	if (!frag_pkt) {
		err = -ENOMEM;
		goto err_free;
	}

	ice_fdir_get_prgm_desc(hw, input, &desc, add);
	err = ice_fdir_get_gen_prgm_pkt(hw, input, pkt, false, is_tun);
	if (err)
		goto err_free_all;
	err = ice_prgm_fdir_fltr(ctrl_vsi, &desc, pkt);
	if (err)
		goto err_free_all;

	/* repeat for fragment packet */
	has_frag = ice_fdir_has_frag(input->flow_type);
	if (has_frag) {
		/* does not return error */
		ice_fdir_get_prgm_desc(hw, input, &desc, add);
		err = ice_fdir_get_gen_prgm_pkt(hw, input, frag_pkt, true,
						is_tun);
		if (err)
			goto err_frag;
		err = ice_prgm_fdir_fltr(ctrl_vsi, &desc, frag_pkt);
		if (err)
			goto err_frag;
	} else {
		devm_kfree(dev, frag_pkt);
	}

	return 0;

err_free_all:
	devm_kfree(dev, frag_pkt);
err_free:
	devm_kfree(dev, pkt);
	return err;

err_frag:
	devm_kfree(dev, frag_pkt);
	return err;
}

/**
 * ice_fdir_write_all_fltr - send a flow director filter to the hardware
 * @pf: PF data structure
 * @input: filter structure
 * @add: true adds filter and false removed filter
 *
 * returns 0 on success and negative value on error
 */
static int
ice_fdir_write_all_fltr(struct ice_pf *pf, struct ice_fdir_fltr *input,
			bool add)
{
	u16 port_num;
	int tun;

	for (tun = 0; tun < ICE_FD_HW_SEG_MAX; tun++) {
		bool is_tun = tun == ICE_FD_HW_SEG_TUN;
		int err;

		if (is_tun && !ice_get_open_tunnel_port(&pf->hw, &port_num, TNL_ALL))
			continue;
		err = ice_fdir_write_fltr(pf, input, add, is_tun);
		if (err)
			return err;
	}
	return 0;
}

/**
 * ice_fdir_replay_fltrs - replay filters from the HW filter list
 * @pf: board private structure
 */
void ice_fdir_replay_fltrs(struct ice_pf *pf)
{
	struct ice_fdir_fltr *f_rule;
	struct ice_hw *hw = &pf->hw;

	list_for_each_entry(f_rule, &hw->fdir_list_head, fltr_node) {
		int err = ice_fdir_write_all_fltr(pf, f_rule, true);

		if (err)
			dev_dbg(ice_pf_to_dev(pf), "Flow Director error %d, could not reprogram filter %d\n",
				err, f_rule->fltr_id);
	}
}

/**
 * ice_fdir_create_dflt_rules - create default perfect filters
 * @pf: PF data structure
 *
 * Returns 0 for success or error.
 */
int ice_fdir_create_dflt_rules(struct ice_pf *pf)
{
	int err;

	/* Create perfect TCP and UDP rules in hardware. */
	err = ice_create_init_fdir_rule(pf, ICE_FLTR_PTYPE_NONF_IPV4_TCP);
	if (err)
		return err;

	err = ice_create_init_fdir_rule(pf, ICE_FLTR_PTYPE_NONF_IPV4_UDP);
	if (err)
		return err;

	err = ice_create_init_fdir_rule(pf, ICE_FLTR_PTYPE_NONF_IPV6_TCP);
	if (err)
		return err;

	err = ice_create_init_fdir_rule(pf, ICE_FLTR_PTYPE_NONF_IPV6_UDP);

	return err;
}

/**
 * ice_fdir_del_all_fltrs - Delete all flow director filters
 * @vsi: the VSI being changed
 *
 * This function needs to be called while holding hw->fdir_fltr_lock
 */
void ice_fdir_del_all_fltrs(struct ice_vsi *vsi)
{
	struct ice_fdir_fltr *f_rule, *tmp;
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;

	list_for_each_entry_safe(f_rule, tmp, &hw->fdir_list_head, fltr_node) {
		ice_fdir_write_all_fltr(pf, f_rule, false);
		ice_fdir_update_cntrs(hw, f_rule->flow_type, false);
		list_del(&f_rule->fltr_node);
		devm_kfree(ice_pf_to_dev(pf), f_rule);
	}
}

/**
 * ice_vsi_manage_fdir - turn on/off flow director
 * @vsi: the VSI being changed
 * @ena: boolean value indicating if this is an enable or disable request
 */
void ice_vsi_manage_fdir(struct ice_vsi *vsi, bool ena)
{
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	enum ice_fltr_ptype flow;

	if (ena) {
		set_bit(ICE_FLAG_FD_ENA, pf->flags);
		ice_fdir_create_dflt_rules(pf);
		return;
	}

	mutex_lock(&hw->fdir_fltr_lock);
	if (!test_and_clear_bit(ICE_FLAG_FD_ENA, pf->flags))
		goto release_lock;

	ice_fdir_del_all_fltrs(vsi);

	if (hw->fdir_prof)
		for (flow = ICE_FLTR_PTYPE_NONF_NONE; flow < ICE_FLTR_PTYPE_MAX;
		     flow++)
			if (hw->fdir_prof[flow])
				ice_fdir_rem_flow(hw, ICE_BLK_FD, flow);

release_lock:
	mutex_unlock(&hw->fdir_fltr_lock);
}

/**
 * ice_fdir_do_rem_flow - delete flow and possibly add perfect flow
 * @pf: PF structure
 * @flow_type: FDir flow type to release
 */
static void
ice_fdir_do_rem_flow(struct ice_pf *pf, enum ice_fltr_ptype flow_type)
{
	struct ice_hw *hw = &pf->hw;
	bool need_perfect = false;

	if (flow_type == ICE_FLTR_PTYPE_NONF_IPV4_TCP ||
	    flow_type == ICE_FLTR_PTYPE_NONF_IPV4_UDP ||
	    flow_type == ICE_FLTR_PTYPE_NONF_IPV6_TCP ||
	    flow_type == ICE_FLTR_PTYPE_NONF_IPV6_UDP)
		need_perfect = true;

	if (need_perfect && test_bit(flow_type, hw->fdir_perfect_fltr))
		return;

	ice_fdir_rem_flow(hw, ICE_BLK_FD, flow_type);
	if (need_perfect)
		ice_create_init_fdir_rule(pf, flow_type);
}

/**
 * ice_fdir_update_list_entry - add or delete a filter from the filter list
 * @pf: PF structure
 * @input: filter structure
 * @fltr_idx: ethtool index of filter to modify
 *
 * returns 0 on success and negative on errors
 */
static int
ice_fdir_update_list_entry(struct ice_pf *pf, struct ice_fdir_fltr *input,
			   int fltr_idx)
{
	struct ice_fdir_fltr *old_fltr;
	struct ice_hw *hw = &pf->hw;
	struct ice_vsi *vsi;
	int err = -ENOENT;

	/* Do not update filters during reset */
	if (ice_is_reset_in_progress(pf->state))
		return -EBUSY;

	vsi = ice_get_main_vsi(pf);
	if (!vsi)
		return -EINVAL;

	old_fltr = ice_fdir_find_fltr_by_idx(hw, fltr_idx);
	if (old_fltr) {
		err = ice_fdir_write_all_fltr(pf, old_fltr, false);
		if (err)
			return err;
		ice_fdir_update_cntrs(hw, old_fltr->flow_type, false);
		/* update sb-filters count, specific to ring->channel */
		ice_update_per_q_fltr(vsi, old_fltr->orig_q_index, false);
		if (!input && !hw->fdir_fltr_cnt[old_fltr->flow_type])
			/* we just deleted the last filter of flow_type so we
			 * should also delete the HW filter info.
			 */
			ice_fdir_do_rem_flow(pf, old_fltr->flow_type);
		list_del(&old_fltr->fltr_node);
		devm_kfree(ice_hw_to_dev(hw), old_fltr);
	}
	if (!input)
		return err;
	ice_fdir_list_add_fltr(hw, input);
	/* update sb-filters count, specific to ring->channel */
	ice_update_per_q_fltr(vsi, input->orig_q_index, true);
	ice_fdir_update_cntrs(hw, input->flow_type, true);
	return 0;
}

/**
 * ice_del_fdir_ethtool - delete Flow Director filter
 * @vsi: pointer to target VSI
 * @cmd: command to add or delete Flow Director filter
 *
 * Returns 0 on success and negative values for failure
 */
int ice_del_fdir_ethtool(struct ice_vsi *vsi, struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	int val;

	if (!test_bit(ICE_FLAG_FD_ENA, pf->flags))
		return -EOPNOTSUPP;

	/* Do not delete filters during reset */
	if (ice_is_reset_in_progress(pf->state)) {
		dev_err(ice_pf_to_dev(pf), "Device is resetting - deleting Flow Director filters not supported during reset\n");
		return -EBUSY;
	}

	if (test_bit(ICE_FD_FLUSH_REQ, pf->state))
		return -EBUSY;

	mutex_lock(&hw->fdir_fltr_lock);
	val = ice_fdir_update_list_entry(pf, NULL, fsp->location);
	mutex_unlock(&hw->fdir_fltr_lock);

	return val;
}

/**
 * ice_update_ring_dest_vsi - update dest ring and dest VSI
 * @vsi: pointer to target VSI
 * @dest_vsi: ptr to dest VSI index
 * @ring: ptr to dest ring
 *
 * This function updates destination VSI and queue if user specifies
 * target queue which falls in channel's (aka ADQ) queue region
 */
static void
ice_update_ring_dest_vsi(struct ice_vsi *vsi, u16 *dest_vsi, u32 *ring)
{
	struct ice_channel *ch;

	list_for_each_entry(ch, &vsi->ch_list, list) {
		if (!ch->ch_vsi)
			continue;

		/* make sure to locate corresponding channel based on "queue"
		 * specified
		 */
		if ((*ring < ch->base_q) ||
		    (*ring >= (ch->base_q + ch->num_rxq)))
			continue;

		/* update the dest_vsi based on channel */
		*dest_vsi = ch->ch_vsi->idx;

		/* update the "ring" to be correct based on channel */
		*ring -= ch->base_q;
	}
}

/**
 * ice_set_fdir_input_set - Set the input set for Flow Director
 * @vsi: pointer to target VSI
 * @fsp: pointer to ethtool Rx flow specification
 * @input: filter structure
 */
static int
ice_set_fdir_input_set(struct ice_vsi *vsi, struct ethtool_rx_flow_spec *fsp,
		       struct ice_fdir_fltr *input)
{
	u16 dest_vsi, q_index = 0;
	u16 orig_q_index = 0;
	struct ice_pf *pf;
	struct ice_hw *hw;
	int flow_type;
	u8 dest_ctl;

	if (!vsi || !fsp || !input)
		return -EINVAL;

	pf = vsi->back;
	hw = &pf->hw;

	dest_vsi = vsi->idx;
	if (fsp->ring_cookie == RX_CLS_FLOW_DISC) {
		dest_ctl = ICE_FLTR_PRGM_DESC_DEST_DROP_PKT;
	} else {
		u32 ring = ethtool_get_flow_spec_ring(fsp->ring_cookie);
		u8 vf = ethtool_get_flow_spec_ring_vf(fsp->ring_cookie);

		if (vf) {
			dev_err(ice_pf_to_dev(pf), "Failed to add filter. Flow director filters are not supported on VF queues.\n");
			return -EINVAL;
		}

		if (ring >= vsi->num_rxq)
			return -EINVAL;

		orig_q_index = ring;
		ice_update_ring_dest_vsi(vsi, &dest_vsi, &ring);
		dest_ctl = ICE_FLTR_PRGM_DESC_DEST_DIRECT_PKT_QINDEX;
		q_index = ring;
	}

	input->fltr_id = fsp->location;
	input->q_index = q_index;
	flow_type = fsp->flow_type & ~FLOW_EXT;

	/* Record the original queue index as specified by user.
	 * with channel configuration 'q_index' becomes relative
	 * to TC (channel).
	 */
	input->orig_q_index = orig_q_index;
	input->dest_vsi = dest_vsi;
	input->dest_ctl = dest_ctl;
	input->fltr_status = ICE_FLTR_PRGM_DESC_FD_STATUS_FD_ID;
	input->cnt_index = ICE_FD_SB_STAT_IDX(hw->fd_ctr_base);
	input->flow_type = ice_ethtool_flow_to_fltr(flow_type);

	if (fsp->flow_type & FLOW_EXT) {
		memcpy(input->ext_data.usr_def, fsp->h_ext.data,
		       sizeof(input->ext_data.usr_def));
		input->ext_data.vlan_type = fsp->h_ext.vlan_etype;
		input->ext_data.vlan_tag = fsp->h_ext.vlan_tci;
		memcpy(input->ext_mask.usr_def, fsp->m_ext.data,
		       sizeof(input->ext_mask.usr_def));
		input->ext_mask.vlan_type = fsp->m_ext.vlan_etype;
		input->ext_mask.vlan_tag = fsp->m_ext.vlan_tci;
	}

	switch (flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
		input->ip.v4.dst_port = fsp->h_u.tcp_ip4_spec.pdst;
		input->ip.v4.src_port = fsp->h_u.tcp_ip4_spec.psrc;
		input->ip.v4.dst_ip = fsp->h_u.tcp_ip4_spec.ip4dst;
		input->ip.v4.src_ip = fsp->h_u.tcp_ip4_spec.ip4src;
		input->mask.v4.dst_port = fsp->m_u.tcp_ip4_spec.pdst;
		input->mask.v4.src_port = fsp->m_u.tcp_ip4_spec.psrc;
		input->mask.v4.dst_ip = fsp->m_u.tcp_ip4_spec.ip4dst;
		input->mask.v4.src_ip = fsp->m_u.tcp_ip4_spec.ip4src;
		break;
	case IPV4_USER_FLOW:
		input->ip.v4.dst_ip = fsp->h_u.usr_ip4_spec.ip4dst;
		input->ip.v4.src_ip = fsp->h_u.usr_ip4_spec.ip4src;
		input->ip.v4.l4_header = fsp->h_u.usr_ip4_spec.l4_4_bytes;
		input->ip.v4.proto = fsp->h_u.usr_ip4_spec.proto;
		input->ip.v4.ip_ver = fsp->h_u.usr_ip4_spec.ip_ver;
		input->ip.v4.tos = fsp->h_u.usr_ip4_spec.tos;
		input->mask.v4.dst_ip = fsp->m_u.usr_ip4_spec.ip4dst;
		input->mask.v4.src_ip = fsp->m_u.usr_ip4_spec.ip4src;
		input->mask.v4.l4_header = fsp->m_u.usr_ip4_spec.l4_4_bytes;
		input->mask.v4.proto = fsp->m_u.usr_ip4_spec.proto;
		input->mask.v4.ip_ver = fsp->m_u.usr_ip4_spec.ip_ver;
		input->mask.v4.tos = fsp->m_u.usr_ip4_spec.tos;
		break;
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
	case SCTP_V6_FLOW:
		memcpy(input->ip.v6.dst_ip, fsp->h_u.usr_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		memcpy(input->ip.v6.src_ip, fsp->h_u.usr_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		input->ip.v6.dst_port = fsp->h_u.tcp_ip6_spec.pdst;
		input->ip.v6.src_port = fsp->h_u.tcp_ip6_spec.psrc;
		input->ip.v6.tc = fsp->h_u.tcp_ip6_spec.tclass;
		memcpy(input->mask.v6.dst_ip, fsp->m_u.tcp_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		memcpy(input->mask.v6.src_ip, fsp->m_u.tcp_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		input->mask.v6.dst_port = fsp->m_u.tcp_ip6_spec.pdst;
		input->mask.v6.src_port = fsp->m_u.tcp_ip6_spec.psrc;
		input->mask.v6.tc = fsp->m_u.tcp_ip6_spec.tclass;
		break;
	case IPV6_USER_FLOW:
		memcpy(input->ip.v6.dst_ip, fsp->h_u.usr_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		memcpy(input->ip.v6.src_ip, fsp->h_u.usr_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		input->ip.v6.l4_header = fsp->h_u.usr_ip6_spec.l4_4_bytes;
		input->ip.v6.tc = fsp->h_u.usr_ip6_spec.tclass;

		/* if no protocol requested, use IPPROTO_NONE */
		if (!fsp->m_u.usr_ip6_spec.l4_proto)
			input->ip.v6.proto = IPPROTO_NONE;
		else
			input->ip.v6.proto = fsp->h_u.usr_ip6_spec.l4_proto;

		memcpy(input->mask.v6.dst_ip, fsp->m_u.usr_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		memcpy(input->mask.v6.src_ip, fsp->m_u.usr_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		input->mask.v6.l4_header = fsp->m_u.usr_ip6_spec.l4_4_bytes;
		input->mask.v6.tc = fsp->m_u.usr_ip6_spec.tclass;
		input->mask.v6.proto = fsp->m_u.usr_ip6_spec.l4_proto;
		break;
	case ETHER_FLOW:
		input->eth = fsp->h_u.ether_spec;
		input->eth_mask = fsp->m_u.ether_spec;
		break;
	default:
		/* not doing un-parsed flow types */
		return -EINVAL;
	}

	return 0;
}

/**
 * ice_add_fdir_ethtool - Add/Remove Flow Director filter
 * @vsi: pointer to target VSI
 * @cmd: command to add or delete Flow Director filter
 *
 * Returns 0 on success and negative values for failure
 */
int ice_add_fdir_ethtool(struct ice_vsi *vsi, struct ethtool_rxnfc *cmd)
{
	struct ice_rx_flow_userdef userdata;
	struct ethtool_rx_flow_spec *fsp;
	struct ice_fdir_fltr *input;
	struct device *dev;
	struct ice_pf *pf;
	struct ice_hw *hw;
	int fltrs_needed;
	u32 max_location;
	u16 tunnel_port;
	int ret;

	if (!vsi)
		return -EINVAL;

	pf = vsi->back;
	hw = &pf->hw;
	dev = ice_pf_to_dev(pf);

	if (!test_bit(ICE_FLAG_FD_ENA, pf->flags))
		return -EOPNOTSUPP;

	/* Do not program filters during reset */
	if (ice_is_reset_in_progress(pf->state)) {
		dev_err(dev, "Device is resetting - adding Flow Director filters not supported during reset\n");
		return -EBUSY;
	}

	fsp = (struct ethtool_rx_flow_spec *)&cmd->fs;

	if (ice_parse_rx_flow_user_data(fsp, &userdata))
		return -EINVAL;

	if (fsp->flow_type & FLOW_MAC_EXT)
		return -EINVAL;

	ret = ice_cfg_fdir_xtrct_seq(pf, fsp, &userdata);
	if (ret)
		return ret;

	max_location = ice_get_fdir_cnt_all(hw);
	if (fsp->location >= max_location) {
		dev_err(dev, "Failed to add filter. The number of ntuple filters or provided location exceed max %d.\n",
			max_location);
		return -ENOSPC;
	}

	/* return error if not an update and no available filters */
	fltrs_needed = ice_get_open_tunnel_port(hw, &tunnel_port, TNL_ALL) ? 2 : 1;
	if (!ice_fdir_find_fltr_by_idx(hw, fsp->location) &&
	    ice_fdir_num_avail_fltr(hw, pf->vsi[vsi->idx]) < fltrs_needed) {
		dev_err(dev, "Failed to add filter. The maximum number of flow director filters has been reached.\n");
		return -ENOSPC;
	}

	input = devm_kzalloc(dev, sizeof(*input), GFP_KERNEL);
	if (!input)
		return -ENOMEM;

	ret = ice_set_fdir_input_set(vsi, fsp, input);
	if (ret)
		goto free_input;

	mutex_lock(&hw->fdir_fltr_lock);
	if (ice_fdir_is_dup_fltr(hw, input)) {
		ret = -EINVAL;
		goto release_lock;
	}

	if (userdata.flex_fltr) {
		input->flex_fltr = true;
		input->flex_word = cpu_to_be16(userdata.flex_word);
		input->flex_offset = userdata.flex_offset;
	}

	input->cnt_ena = ICE_FXD_FLTR_QW0_STAT_ENA_PKTS;
	input->fdid_prio = ICE_FXD_FLTR_QW1_FDID_PRI_THREE;
	input->comp_report = ICE_FXD_FLTR_QW0_COMP_REPORT_SW_FAIL;

	/* input struct is added to the HW filter list */
	ret = ice_fdir_update_list_entry(pf, input, fsp->location);
	if (ret)
		goto release_lock;

	ret = ice_fdir_write_all_fltr(pf, input, true);
	if (ret)
		goto remove_sw_rule;

	goto release_lock;

remove_sw_rule:
	ice_fdir_update_cntrs(hw, input->flow_type, false);
	/* update sb-filters count, specific to ring->channel */
	ice_update_per_q_fltr(vsi, input->orig_q_index, false);
	list_del(&input->fltr_node);
release_lock:
	mutex_unlock(&hw->fdir_fltr_lock);
free_input:
	if (ret)
		devm_kfree(dev, input);

	return ret;
}
