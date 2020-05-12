// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2020, Intel Corporation. */

/* flow director ethtool support for ice */

#include "ice.h"
#include "ice_lib.h"
#include "ice_flow.h"

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
	case ICE_FLTR_PTYPE_NONF_IPV4_TCP:
		return TCP_V4_FLOW;
	case ICE_FLTR_PTYPE_NONF_IPV4_UDP:
		return UDP_V4_FLOW;
	case ICE_FLTR_PTYPE_NONF_IPV4_SCTP:
		return SCTP_V4_FLOW;
	case ICE_FLTR_PTYPE_NONF_IPV4_OTHER:
		return IPV4_USER_FLOW;
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
	case TCP_V4_FLOW:
		return ICE_FLTR_PTYPE_NONF_IPV4_TCP;
	case UDP_V4_FLOW:
		return ICE_FLTR_PTYPE_NONF_IPV4_UDP;
	case SCTP_V4_FLOW:
		return ICE_FLTR_PTYPE_NONF_IPV4_SCTP;
	case IPV4_USER_FLOW:
		return ICE_FLTR_PTYPE_NONF_IPV4_OTHER;
	default:
		return ICE_FLTR_PTYPE_NONF_NONE;
	}
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
	case IPV4_USER_FLOW:
		fsp->h_u.usr_ip4_spec.ip_ver = ETH_RX_NFC_IP4;
		fsp->h_u.usr_ip4_spec.proto = 0;
		fsp->h_u.usr_ip4_spec.l4_4_bytes = rule->ip.l4_header;
		fsp->h_u.usr_ip4_spec.tos = rule->ip.tos;
		fsp->h_u.usr_ip4_spec.ip4src = rule->ip.src_ip;
		fsp->h_u.usr_ip4_spec.ip4dst = rule->ip.dst_ip;
		fsp->m_u.usr_ip4_spec.ip4src = rule->mask.src_ip;
		fsp->m_u.usr_ip4_spec.ip4dst = rule->mask.dst_ip;
		fsp->m_u.usr_ip4_spec.ip_ver = 0xFF;
		fsp->m_u.usr_ip4_spec.proto = 0;
		fsp->m_u.usr_ip4_spec.l4_4_bytes = rule->mask.l4_header;
		fsp->m_u.usr_ip4_spec.tos = rule->mask.tos;
		break;
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
		fsp->h_u.tcp_ip4_spec.psrc = rule->ip.src_port;
		fsp->h_u.tcp_ip4_spec.pdst = rule->ip.dst_port;
		fsp->h_u.tcp_ip4_spec.ip4src = rule->ip.src_ip;
		fsp->h_u.tcp_ip4_spec.ip4dst = rule->ip.dst_ip;
		fsp->m_u.tcp_ip4_spec.psrc = rule->mask.src_port;
		fsp->m_u.tcp_ip4_spec.pdst = rule->mask.dst_port;
		fsp->m_u.tcp_ip4_spec.ip4src = rule->mask.src_ip;
		fsp->m_u.tcp_ip4_spec.ip4dst = rule->mask.dst_ip;
		break;
	default:
		break;
	}

	if (rule->dest_ctl == ICE_FLTR_PRGM_DESC_DEST_DROP_PKT)
		fsp->ring_cookie = RX_CLS_FLOW_DISC;
	else
		fsp->ring_cookie = rule->q_index;

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
		u64 prof_id;
		int j;

		prof_id = flow + tun * ICE_FLTR_PTYPE_MAX;
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
	enum ice_status status;
	u64 entry1_h = 0;
	u64 entry2_h = 0;
	u64 prof_id;
	int err;

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

		/* remove HW filter definition */
		ice_fdir_rem_flow(hw, ICE_BLK_FD, flow);
	}

	/* Adding a profile, but there is only one header supported.
	 * That is the final parameters are 1 header (segment), no
	 * actions (NULL) and zero actions 0.
	 */
	prof_id = flow + tun * ICE_FLTR_PTYPE_MAX;
	status = ice_flow_add_prof(hw, ICE_BLK_FD, ICE_FLOW_RX, prof_id, seg,
				   TNL_SEG_CNT(tun), &prof);
	if (status)
		return ice_status_to_errno(status);
	status = ice_flow_add_entry(hw, ICE_BLK_FD, prof_id, main_vsi->idx,
				    main_vsi->idx, ICE_FLOW_PRIO_NORMAL,
				    seg, &entry1_h);
	if (status) {
		err = ice_status_to_errno(status);
		goto err_prof;
	}
	status = ice_flow_add_entry(hw, ICE_BLK_FD, prof_id, main_vsi->idx,
				    ctrl_vsi->idx, ICE_FLOW_PRIO_NORMAL,
				    seg, &entry2_h);
	if (status) {
		err = ice_status_to_errno(status);
		goto err_entry;
	}

	hw_prof->fdir_seg[tun] = seg;
	hw_prof->entry_h[0][tun] = entry1_h;
	hw_prof->entry_h[1][tun] = entry2_h;
	hw_prof->vsi_h[0] = main_vsi->idx;
	hw_prof->vsi_h[1] = ctrl_vsi->idx;
	if (!hw_prof->cnt)
		hw_prof->cnt = 2;

	return 0;

err_entry:
	ice_rem_prof_id_flow(hw, ICE_BLK_FD,
			     ice_get_hw_vsi_num(hw, main_vsi->idx), prof_id);
	ice_flow_rem_entry(hw, ICE_BLK_FD, entry1_h);
err_prof:
	ice_flow_rem_prof(hw, ICE_BLK_FD, prof_id);
	dev_err(dev, "Failed to add filter.  Flow director filters on each port must have the same input set.\n");

	return err;
}

/**
 * ice_set_init_fdir_seg
 * @seg: flow segment for programming
 * @l4_proto: ICE_FLOW_SEG_HDR_TCP or ICE_FLOW_SEG_HDR_UDP
 *
 * Set the configuration for perfect filters to the provided flow segment for
 * programming the HW filter. This is to be called only when initializing
 * filters as this function it assumes no filters exist.
 */
static int
ice_set_init_fdir_seg(struct ice_flow_seg_info *seg,
		      enum ice_flow_seg_hdr l4_proto)
{
	enum ice_flow_field src_port, dst_port;

	if (!seg)
		return -EINVAL;

	if (l4_proto == ICE_FLOW_SEG_HDR_TCP) {
		src_port = ICE_FLOW_FIELD_IDX_TCP_SRC_PORT;
		dst_port = ICE_FLOW_FIELD_IDX_TCP_DST_PORT;
	} else if (l4_proto == ICE_FLOW_SEG_HDR_UDP) {
		src_port = ICE_FLOW_FIELD_IDX_UDP_SRC_PORT;
		dst_port = ICE_FLOW_FIELD_IDX_UDP_DST_PORT;
	} else {
		return -EINVAL;
	}

	ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_IPV4 | l4_proto);

	/* IP source address */
	ice_flow_set_fld(seg, ICE_FLOW_FIELD_IDX_IPV4_SA,
			 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
			 ICE_FLOW_FLD_OFF_INVAL, false);

	/* IP destination address */
	ice_flow_set_fld(seg, ICE_FLOW_FIELD_IDX_IPV4_DA,
			 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
			 ICE_FLOW_FLD_OFF_INVAL, false);

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

	tun_seg = devm_kzalloc(dev, sizeof(*seg) * ICE_FD_HW_SEG_MAX,
			       GFP_KERNEL);
	if (!tun_seg) {
		devm_kfree(dev, seg);
		return -ENOMEM;
	}

	if (flow == ICE_FLTR_PTYPE_NONF_IPV4_TCP)
		ret = ice_set_init_fdir_seg(seg, ICE_FLOW_SEG_HDR_TCP);
	else if (flow == ICE_FLTR_PTYPE_NONF_IPV4_UDP)
		ret = ice_set_init_fdir_seg(seg, ICE_FLOW_SEG_HDR_UDP);
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

	return err;
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

	if (hw->fdir_prof)
		for (flow = ICE_FLTR_PTYPE_NONF_NONE; flow < ICE_FLTR_PTYPE_MAX;
		     flow++)
			if (hw->fdir_prof[flow])
				ice_fdir_rem_flow(hw, ICE_BLK_FD, flow);

release_lock:
	mutex_unlock(&hw->fdir_fltr_lock);
}
