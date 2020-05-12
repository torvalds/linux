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

	/* minus the guaranteed filters programed by this VSI */
	num_guar -= (rd32(hw, VSIQF_FD_CNT(vsi_num)) &
		     VSIQF_FD_CNT_FD_GCNT_M) >> VSIQF_FD_CNT_FD_GCNT_S;

	/* total global best effort filters */
	num_be = hw->func_caps.fd_fltr_best_effort;

	/* minus the global best effort filters programmed */
	num_be -= (rd32(hw, GLQF_FD_CNT) & GLQF_FD_CNT_FD_BCNT_M) >>
		   GLQF_FD_CNT_FD_BCNT_S;

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

		/* if there are FDir filters using this flow,
		 * then return error.
		 */
		if (hw->fdir_fltr_cnt[flow]) {
			dev_err(dev, "Failed to add filter.  Flow director filters on each port must have the same input set.\n");
			return -EINVAL;
		}

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
 * ice_cfg_fdir_xtrct_seq - Configure extraction sequence for the given filter
 * @pf: PF structure
 * @fsp: pointer to ethtool Rx flow specification
 *
 * Returns 0 on success.
 */
static int
ice_cfg_fdir_xtrct_seq(struct ice_pf *pf, struct ethtool_rx_flow_spec *fsp)
{
	struct ice_flow_seg_info *seg, *tun_seg;
	struct device *dev = ice_pf_to_dev(pf);
	enum ice_fltr_ptype fltr_idx;
	struct ice_hw *hw = &pf->hw;
	bool perfect_filter;
	int ret;

	seg = devm_kzalloc(dev, sizeof(*seg), GFP_KERNEL);
	if (!seg)
		return -ENOMEM;

	tun_seg = devm_kzalloc(dev, sizeof(*seg) * ICE_FD_HW_SEG_MAX,
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
	default:
		ret = -EINVAL;
	}
	if (ret)
		goto err_exit;

	/* tunnel segments are shifted up one. */
	memcpy(&tun_seg[1], seg, sizeof(*seg));

	/* add filter for outer headers */
	fltr_idx = ice_ethtool_flow_to_fltr(fsp->flow_type & ~FLOW_EXT);
	ret = ice_fdir_set_hw_fltr_rule(pf, seg, fltr_idx,
					ICE_FD_HW_SEG_NON_TUN);
	if (ret == -EEXIST)
		/* Rule already exists, free memory and continue */
		devm_kfree(dev, seg);
	else if (ret)
		/* could not write filter, free memory */
		goto err_exit;

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

	if (perfect_filter)
		set_bit(fltr_idx, hw->fdir_perfect_fltr);
	else
		clear_bit(fltr_idx, hw->fdir_perfect_fltr);

	return ret;

err_exit:
	devm_kfree(dev, tun_seg);
	devm_kfree(dev, seg);

	return -EOPNOTSUPP;
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
static int
ice_fdir_write_fltr(struct ice_pf *pf, struct ice_fdir_fltr *input, bool add,
		    bool is_tun)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	struct ice_fltr_desc desc;
	struct ice_vsi *ctrl_vsi;
	enum ice_status status;
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
	status = ice_fdir_get_gen_prgm_pkt(hw, input, pkt, false, is_tun);
	if (status) {
		err = ice_status_to_errno(status);
		goto err_free_all;
	}
	err = ice_prgm_fdir_fltr(ctrl_vsi, &desc, pkt);
	if (err)
		goto err_free_all;

	/* repeat for fragment packet */
	has_frag = ice_fdir_has_frag(input->flow_type);
	if (has_frag) {
		/* does not return error */
		ice_fdir_get_prgm_desc(hw, input, &desc, add);
		status = ice_fdir_get_gen_prgm_pkt(hw, input, frag_pkt, true,
						   is_tun);
		if (status) {
			err = ice_status_to_errno(status);
			goto err_frag;
		}
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

		if (is_tun && !ice_get_open_tunnel_port(&pf->hw, TNL_ALL,
							&port_num))
			continue;
		err = ice_fdir_write_fltr(pf, input, add, is_tun);
		if (err)
			return err;
	}
	return 0;
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
	struct ice_fdir_fltr *f_rule, *tmp;
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
	list_for_each_entry_safe(f_rule, tmp, &hw->fdir_list_head, fltr_node) {
		/* ignore return value */
		ice_fdir_write_all_fltr(pf, f_rule, false);
		ice_fdir_update_cntrs(hw, f_rule->flow_type, false);
		list_del(&f_rule->fltr_node);
		devm_kfree(ice_hw_to_dev(hw), f_rule);
	}

	if (hw->fdir_prof)
		for (flow = ICE_FLTR_PTYPE_NONF_NONE; flow < ICE_FLTR_PTYPE_MAX;
		     flow++)
			if (hw->fdir_prof[flow])
				ice_fdir_rem_flow(hw, ICE_BLK_FD, flow);

release_lock:
	mutex_unlock(&hw->fdir_fltr_lock);
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
	int err = -ENOENT;

	/* Do not update filters during reset */
	if (ice_is_reset_in_progress(pf->state))
		return -EBUSY;

	old_fltr = ice_fdir_find_fltr_by_idx(hw, fltr_idx);
	if (old_fltr) {
		err = ice_fdir_write_all_fltr(pf, old_fltr, false);
		if (err)
			return err;
		ice_fdir_update_cntrs(hw, old_fltr->flow_type, false);
		if (!input && !hw->fdir_fltr_cnt[old_fltr->flow_type])
			/* we just deleted the last filter of flow_type so we
			 * should also delete the HW filter info.
			 */
			ice_fdir_rem_flow(hw, ICE_BLK_FD, old_fltr->flow_type);
		list_del(&old_fltr->fltr_node);
		devm_kfree(ice_hw_to_dev(hw), old_fltr);
	}
	if (!input)
		return err;
	ice_fdir_list_add_fltr(hw, input);
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

	if (test_bit(__ICE_FD_FLUSH_REQ, pf->state))
		return -EBUSY;

	mutex_lock(&hw->fdir_fltr_lock);
	val = ice_fdir_update_list_entry(pf, NULL, fsp->location);
	mutex_unlock(&hw->fdir_fltr_lock);

	return val;
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

		dest_ctl = ICE_FLTR_PRGM_DESC_DEST_DIRECT_PKT_QINDEX;
		q_index = ring;
	}

	input->fltr_id = fsp->location;
	input->q_index = q_index;
	flow_type = fsp->flow_type & ~FLOW_EXT;

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
		input->ip.dst_port = fsp->h_u.tcp_ip4_spec.pdst;
		input->ip.src_port = fsp->h_u.tcp_ip4_spec.psrc;
		input->ip.dst_ip = fsp->h_u.tcp_ip4_spec.ip4dst;
		input->ip.src_ip = fsp->h_u.tcp_ip4_spec.ip4src;
		input->mask.dst_port = fsp->m_u.tcp_ip4_spec.pdst;
		input->mask.src_port = fsp->m_u.tcp_ip4_spec.psrc;
		input->mask.dst_ip = fsp->m_u.tcp_ip4_spec.ip4dst;
		input->mask.src_ip = fsp->m_u.tcp_ip4_spec.ip4src;
		break;
	case IPV4_USER_FLOW:
		input->ip.dst_ip = fsp->h_u.usr_ip4_spec.ip4dst;
		input->ip.src_ip = fsp->h_u.usr_ip4_spec.ip4src;
		input->ip.l4_header = fsp->h_u.usr_ip4_spec.l4_4_bytes;
		input->ip.proto = fsp->h_u.usr_ip4_spec.proto;
		input->ip.ip_ver = fsp->h_u.usr_ip4_spec.ip_ver;
		input->ip.tos = fsp->h_u.usr_ip4_spec.tos;
		input->mask.dst_ip = fsp->m_u.usr_ip4_spec.ip4dst;
		input->mask.src_ip = fsp->m_u.usr_ip4_spec.ip4src;
		input->mask.l4_header = fsp->m_u.usr_ip4_spec.l4_4_bytes;
		input->mask.proto = fsp->m_u.usr_ip4_spec.proto;
		input->mask.ip_ver = fsp->m_u.usr_ip4_spec.ip_ver;
		input->mask.tos = fsp->m_u.usr_ip4_spec.tos;
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
	struct ethtool_rx_flow_spec *fsp;
	struct ice_fdir_fltr *input;
	struct device *dev;
	struct ice_pf *pf;
	struct ice_hw *hw;
	int fltrs_needed;
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

	if (fsp->flow_type & FLOW_MAC_EXT)
		return -EINVAL;

	ret = ice_cfg_fdir_xtrct_seq(pf, fsp);
	if (ret)
		return ret;

	if (fsp->location >= ice_get_fdir_cnt_all(hw)) {
		dev_err(dev, "Failed to add filter.  The maximum number of flow director filters has been reached.\n");
		return -ENOSPC;
	}

	/* return error if not an update and no available filters */
	fltrs_needed = ice_get_open_tunnel_port(hw, TNL_ALL, &tunnel_port) ?
		2 : 1;
	if (!ice_fdir_find_fltr_by_idx(hw, fsp->location) &&
	    ice_fdir_num_avail_fltr(hw, pf->vsi[vsi->idx]) < fltrs_needed) {
		dev_err(dev, "Failed to add filter.  The maximum number of flow director filters has been reached.\n");
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

	/* input struct is added to the HW filter list */
	ice_fdir_update_list_entry(pf, input, fsp->location);

	ret = ice_fdir_write_all_fltr(pf, input, true);
	if (ret)
		goto remove_sw_rule;

	goto release_lock;

remove_sw_rule:
	ice_fdir_update_cntrs(hw, input->flow_type, false);
	list_del(&input->fltr_node);
release_lock:
	mutex_unlock(&hw->fdir_fltr_lock);
free_input:
	if (ret)
		devm_kfree(dev, input);

	return ret;
}
