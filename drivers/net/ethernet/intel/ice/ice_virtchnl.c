// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022, Intel Corporation. */

#include "ice_virtchnl.h"
#include "ice_vf_lib_private.h"
#include "ice.h"
#include "ice_base.h"
#include "ice_lib.h"
#include "ice_fltr.h"
#include "ice_virtchnl_allowlist.h"
#include "ice_vf_vsi_vlan_ops.h"
#include "ice_vlan.h"
#include "ice_flex_pipe.h"
#include "ice_dcb_lib.h"

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
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT)},
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
};

/**
 * ice_vc_vf_broadcast - Broadcast a message to all VFs on PF
 * @pf: pointer to the PF structure
 * @v_opcode: operation code
 * @v_retval: return value
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 */
static void
ice_vc_vf_broadcast(struct ice_pf *pf, enum virtchnl_ops v_opcode,
		    enum virtchnl_status_code v_retval, u8 *msg, u16 msglen)
{
	struct ice_hw *hw = &pf->hw;
	struct ice_vf *vf;
	unsigned int bkt;

	mutex_lock(&pf->vfs.table_lock);
	ice_for_each_vf(pf, bkt, vf) {
		/* Not all vfs are enabled so skip the ones that are not */
		if (!test_bit(ICE_VF_STATE_INIT, vf->vf_states) &&
		    !test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states))
			continue;

		/* Ignore return value on purpose - a given VF may fail, but
		 * we need to keep going and send to all of them
		 */
		ice_aq_send_msg_to_vf(hw, vf->vf_id, v_opcode, v_retval, msg,
				      msglen, NULL);
	}
	mutex_unlock(&pf->vfs.table_lock);
}

/**
 * ice_set_pfe_link - Set the link speed/status of the virtchnl_pf_event
 * @vf: pointer to the VF structure
 * @pfe: pointer to the virtchnl_pf_event to set link speed/status for
 * @ice_link_speed: link speed specified by ICE_AQ_LINK_SPEED_*
 * @link_up: whether or not to set the link up/down
 */
static void
ice_set_pfe_link(struct ice_vf *vf, struct virtchnl_pf_event *pfe,
		 int ice_link_speed, bool link_up)
{
	if (vf->driver_caps & VIRTCHNL_VF_CAP_ADV_LINK_SPEED) {
		pfe->event_data.link_event_adv.link_status = link_up;
		/* Speed in Mbps */
		pfe->event_data.link_event_adv.link_speed =
			ice_conv_link_speed_to_virtchnl(true, ice_link_speed);
	} else {
		pfe->event_data.link_event.link_status = link_up;
		/* Legacy method for virtchnl link speeds */
		pfe->event_data.link_event.link_speed =
			(enum virtchnl_link_speed)
			ice_conv_link_speed_to_virtchnl(false, ice_link_speed);
	}
}

/**
 * ice_vc_notify_vf_link_state - Inform a VF of link status
 * @vf: pointer to the VF structure
 *
 * send a link status message to a single VF
 */
void ice_vc_notify_vf_link_state(struct ice_vf *vf)
{
	struct virtchnl_pf_event pfe = { 0 };
	struct ice_hw *hw = &vf->pf->hw;

	pfe.event = VIRTCHNL_EVENT_LINK_CHANGE;
	pfe.severity = PF_EVENT_SEVERITY_INFO;

	if (ice_is_vf_link_up(vf))
		ice_set_pfe_link(vf, &pfe,
				 hw->port_info->phy.link_info.link_speed, true);
	else
		ice_set_pfe_link(vf, &pfe, ICE_AQ_LINK_SPEED_UNKNOWN, false);

	ice_aq_send_msg_to_vf(hw, vf->vf_id, VIRTCHNL_OP_EVENT,
			      VIRTCHNL_STATUS_SUCCESS, (u8 *)&pfe,
			      sizeof(pfe), NULL);
}

/**
 * ice_vc_notify_link_state - Inform all VFs on a PF of link status
 * @pf: pointer to the PF structure
 */
void ice_vc_notify_link_state(struct ice_pf *pf)
{
	struct ice_vf *vf;
	unsigned int bkt;

	mutex_lock(&pf->vfs.table_lock);
	ice_for_each_vf(pf, bkt, vf)
		ice_vc_notify_vf_link_state(vf);
	mutex_unlock(&pf->vfs.table_lock);
}

/**
 * ice_vc_notify_reset - Send pending reset message to all VFs
 * @pf: pointer to the PF structure
 *
 * indicate a pending reset to all VFs on a given PF
 */
void ice_vc_notify_reset(struct ice_pf *pf)
{
	struct virtchnl_pf_event pfe;

	if (!ice_has_vfs(pf))
		return;

	pfe.event = VIRTCHNL_EVENT_RESET_IMPENDING;
	pfe.severity = PF_EVENT_SEVERITY_CERTAIN_DOOM;
	ice_vc_vf_broadcast(pf, VIRTCHNL_OP_EVENT, VIRTCHNL_STATUS_SUCCESS,
			    (u8 *)&pfe, sizeof(struct virtchnl_pf_event));
}

/**
 * ice_vc_send_msg_to_vf - Send message to VF
 * @vf: pointer to the VF info
 * @v_opcode: virtual channel opcode
 * @v_retval: virtual channel return value
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * send msg to VF
 */
int
ice_vc_send_msg_to_vf(struct ice_vf *vf, u32 v_opcode,
		      enum virtchnl_status_code v_retval, u8 *msg, u16 msglen)
{
	struct device *dev;
	struct ice_pf *pf;
	int aq_ret;

	pf = vf->pf;
	dev = ice_pf_to_dev(pf);

	aq_ret = ice_aq_send_msg_to_vf(&pf->hw, vf->vf_id, v_opcode, v_retval,
				       msg, msglen, NULL);
	if (aq_ret && pf->hw.mailboxq.sq_last_status != ICE_AQ_RC_ENOSYS) {
		dev_info(dev, "Unable to send the message to VF %d ret %d aq_err %s\n",
			 vf->vf_id, aq_ret,
			 ice_aq_str(pf->hw.mailboxq.sq_last_status));
		return -EIO;
	}

	return 0;
}

/**
 * ice_vc_get_ver_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to request the API version used by the PF
 */
static int ice_vc_get_ver_msg(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_version_info info = {
		VIRTCHNL_VERSION_MAJOR, VIRTCHNL_VERSION_MINOR
	};

	vf->vf_ver = *(struct virtchnl_version_info *)msg;
	/* VFs running the 1.0 API expect to get 1.0 back or they will cry. */
	if (VF_IS_V10(&vf->vf_ver))
		info.minor = VIRTCHNL_VERSION_MINOR_NO_VF_CAPS;

	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_VERSION,
				     VIRTCHNL_STATUS_SUCCESS, (u8 *)&info,
				     sizeof(struct virtchnl_version_info));
}

/**
 * ice_vc_get_max_frame_size - get max frame size allowed for VF
 * @vf: VF used to determine max frame size
 *
 * Max frame size is determined based on the current port's max frame size and
 * whether a port VLAN is configured on this VF. The VF is not aware whether
 * it's in a port VLAN so the PF needs to account for this in max frame size
 * checks and sending the max frame size to the VF.
 */
static u16 ice_vc_get_max_frame_size(struct ice_vf *vf)
{
	struct ice_port_info *pi = ice_vf_get_port_info(vf);
	u16 max_frame_size;

	max_frame_size = pi->phy.link_info.max_frame_size;

	if (ice_vf_is_port_vlan_ena(vf))
		max_frame_size -= VLAN_HLEN;

	return max_frame_size;
}

/**
 * ice_vc_get_vlan_caps
 * @hw: pointer to the hw
 * @vf: pointer to the VF info
 * @vsi: pointer to the VSI
 * @driver_caps: current driver caps
 *
 * Return 0 if there is no VLAN caps supported, or VLAN caps value
 */
static u32
ice_vc_get_vlan_caps(struct ice_hw *hw, struct ice_vf *vf, struct ice_vsi *vsi,
		     u32 driver_caps)
{
	if (ice_is_eswitch_mode_switchdev(vf->pf))
		/* In switchdev setting VLAN from VF isn't supported */
		return 0;

	if (driver_caps & VIRTCHNL_VF_OFFLOAD_VLAN_V2) {
		/* VLAN offloads based on current device configuration */
		return VIRTCHNL_VF_OFFLOAD_VLAN_V2;
	} else if (driver_caps & VIRTCHNL_VF_OFFLOAD_VLAN) {
		/* allow VF to negotiate VIRTCHNL_VF_OFFLOAD explicitly for
		 * these two conditions, which amounts to guest VLAN filtering
		 * and offloads being based on the inner VLAN or the
		 * inner/single VLAN respectively and don't allow VF to
		 * negotiate VIRTCHNL_VF_OFFLOAD in any other cases
		 */
		if (ice_is_dvm_ena(hw) && ice_vf_is_port_vlan_ena(vf)) {
			return VIRTCHNL_VF_OFFLOAD_VLAN;
		} else if (!ice_is_dvm_ena(hw) &&
			   !ice_vf_is_port_vlan_ena(vf)) {
			/* configure backward compatible support for VFs that
			 * only support VIRTCHNL_VF_OFFLOAD_VLAN, the PF is
			 * configured in SVM, and no port VLAN is configured
			 */
			ice_vf_vsi_cfg_svm_legacy_vlan_mode(vsi);
			return VIRTCHNL_VF_OFFLOAD_VLAN;
		} else if (ice_is_dvm_ena(hw)) {
			/* configure software offloaded VLAN support when DVM
			 * is enabled, but no port VLAN is enabled
			 */
			ice_vf_vsi_cfg_dvm_legacy_vlan_mode(vsi);
		}
	}

	return 0;
}

/**
 * ice_vc_get_vf_res_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to request its resources
 */
static int ice_vc_get_vf_res_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vf_resource *vfres = NULL;
	struct ice_hw *hw = &vf->pf->hw;
	struct ice_vsi *vsi;
	int len = 0;
	int ret;

	if (ice_check_vf_init(vf)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	len = virtchnl_struct_size(vfres, vsi_res, 0);

	vfres = kzalloc(len, GFP_KERNEL);
	if (!vfres) {
		v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
		len = 0;
		goto err;
	}
	if (VF_IS_V11(&vf->vf_ver))
		vf->driver_caps = *(u32 *)msg;
	else
		vf->driver_caps = VIRTCHNL_VF_OFFLOAD_L2 |
				  VIRTCHNL_VF_OFFLOAD_VLAN;

	vfres->vf_cap_flags = VIRTCHNL_VF_OFFLOAD_L2;
	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	vfres->vf_cap_flags |= ice_vc_get_vlan_caps(hw, vf, vsi,
						    vf->driver_caps);

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_RSS_PF)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_RSS_PF;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_RX_FLEX_DESC)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_RX_FLEX_DESC;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_FDIR_PF)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_FDIR_PF;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_TC_U32 &&
	    vfres->vf_cap_flags & VIRTCHNL_VF_OFFLOAD_FDIR_PF)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_TC_U32;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_RSS_PCTYPE_V2)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_RSS_PCTYPE_V2;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_ENCAP)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_ENCAP;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_ENCAP_CSUM)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_ENCAP_CSUM;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_RX_POLLING)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_RX_POLLING;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_WB_ON_ITR)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_WB_ON_ITR;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_REQ_QUEUES)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_REQ_QUEUES;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_CRC)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_CRC;

	if (vf->driver_caps & VIRTCHNL_VF_CAP_ADV_LINK_SPEED)
		vfres->vf_cap_flags |= VIRTCHNL_VF_CAP_ADV_LINK_SPEED;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_ADV_RSS_PF)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_ADV_RSS_PF;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_USO)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_USO;

	vfres->num_vsis = 1;
	/* Tx and Rx queue are equal for VF */
	vfres->num_queue_pairs = vsi->num_txq;
	vfres->max_vectors = vf->num_msix;
	vfres->rss_key_size = ICE_VSIQF_HKEY_ARRAY_SIZE;
	vfres->rss_lut_size = ICE_LUT_VSI_SIZE;
	vfres->max_mtu = ice_vc_get_max_frame_size(vf);

	vfres->vsi_res[0].vsi_id = ICE_VF_VSI_ID;
	vfres->vsi_res[0].vsi_type = VIRTCHNL_VSI_SRIOV;
	vfres->vsi_res[0].num_queue_pairs = vsi->num_txq;
	ether_addr_copy(vfres->vsi_res[0].default_mac_addr,
			vf->hw_lan_addr);

	/* match guest capabilities */
	vf->driver_caps = vfres->vf_cap_flags;

	ice_vc_set_caps_allowlist(vf);
	ice_vc_set_working_allowlist(vf);

	set_bit(ICE_VF_STATE_ACTIVE, vf->vf_states);

err:
	/* send the response back to the VF */
	ret = ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_GET_VF_RESOURCES, v_ret,
				    (u8 *)vfres, len);

	kfree(vfres);
	return ret;
}

/**
 * ice_vc_reset_vf_msg
 * @vf: pointer to the VF info
 *
 * called from the VF to reset itself,
 * unlike other virtchnl messages, PF driver
 * doesn't send the response back to the VF
 */
static void ice_vc_reset_vf_msg(struct ice_vf *vf)
{
	if (test_bit(ICE_VF_STATE_INIT, vf->vf_states))
		ice_reset_vf(vf, 0);
}

/**
 * ice_vc_isvalid_vsi_id
 * @vf: pointer to the VF info
 * @vsi_id: VF relative VSI ID
 *
 * check for the valid VSI ID
 */
bool ice_vc_isvalid_vsi_id(struct ice_vf *vf, u16 vsi_id)
{
	return vsi_id == ICE_VF_VSI_ID;
}

/**
 * ice_vc_isvalid_q_id
 * @vsi: VSI to check queue ID against
 * @qid: VSI relative queue ID
 *
 * check for the valid queue ID
 */
static bool ice_vc_isvalid_q_id(struct ice_vsi *vsi, u8 qid)
{
	/* allocated Tx and Rx queues should be always equal for VF VSI */
	return qid < vsi->alloc_txq;
}

/**
 * ice_vc_isvalid_ring_len
 * @ring_len: length of ring
 *
 * check for the valid ring count, should be multiple of ICE_REQ_DESC_MULTIPLE
 * or zero
 */
static bool ice_vc_isvalid_ring_len(u16 ring_len)
{
	return ring_len == 0 ||
	       (ring_len >= ICE_MIN_NUM_DESC &&
		ring_len <= ICE_MAX_NUM_DESC &&
		!(ring_len % ICE_REQ_DESC_MULTIPLE));
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
		bool hdr_found = false;
		int j;

		/* Find matched ice headers according to virtchnl headers. */
		for (j = 0; j < hdr_list_len; j++) {
			struct ice_vc_hdr_match_type hdr_map = hdr_list[j];

			if (proto_hdr->type == hdr_map.vc_hdr) {
				*addl_hdrs |= hdr_map.ice_hdr;
				hdr_found = true;
			}
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
	}

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
 * ice_vc_handle_rss_cfg
 * @vf: pointer to the VF info
 * @msg: pointer to the message buffer
 * @add: add a RSS config if true, otherwise delete a RSS config
 *
 * This function adds/deletes a RSS config
 */
static int ice_vc_handle_rss_cfg(struct ice_vf *vf, u8 *msg, bool add)
{
	u32 v_opcode = add ? VIRTCHNL_OP_ADD_RSS_CFG : VIRTCHNL_OP_DEL_RSS_CFG;
	struct virtchnl_rss_cfg *rss_cfg = (struct virtchnl_rss_cfg *)msg;
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct device *dev = ice_pf_to_dev(vf->pf);
	struct ice_hw *hw = &vf->pf->hw;
	struct ice_vsi *vsi;

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

	if (!ice_vc_validate_pattern(vf, &rss_cfg->proto_hdrs)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (rss_cfg->rss_algorithm == VIRTCHNL_RSS_ALG_R_ASYMMETRIC) {
		struct ice_vsi_ctx *ctx;
		u8 lut_type, hash_type;
		int status;

		lut_type = ICE_AQ_VSI_Q_OPT_RSS_LUT_VSI;
		hash_type = add ? ICE_AQ_VSI_Q_OPT_RSS_HASH_XOR :
				ICE_AQ_VSI_Q_OPT_RSS_HASH_TPLZ;

		ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
		if (!ctx) {
			v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
			goto error_param;
		}

		ctx->info.q_opt_rss =
			FIELD_PREP(ICE_AQ_VSI_Q_OPT_RSS_LUT_M, lut_type) |
			FIELD_PREP(ICE_AQ_VSI_Q_OPT_RSS_HASH_M, hash_type);

		/* Preserve existing queueing option setting */
		ctx->info.q_opt_rss |= (vsi->info.q_opt_rss &
					  ICE_AQ_VSI_Q_OPT_RSS_GBL_LUT_M);
		ctx->info.q_opt_tc = vsi->info.q_opt_tc;
		ctx->info.q_opt_flags = vsi->info.q_opt_rss;

		ctx->info.valid_sections =
				cpu_to_le16(ICE_AQ_VSI_PROP_Q_OPT_VALID);

		status = ice_update_vsi(hw, vsi->idx, ctx, NULL);
		if (status) {
			dev_err(dev, "update VSI for RSS failed, err %d aq_err %s\n",
				status, ice_aq_str(hw->adminq.sq_last_status));
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		} else {
			vsi->info.q_opt_rss = ctx->info.q_opt_rss;
		}

		kfree(ctx);
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
			if (ice_add_rss_cfg(hw, vsi, &cfg)) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				dev_err(dev, "ice_add_rss_cfg failed for vsi = %d, v_ret = %d\n",
					vsi->vsi_num, v_ret);
			}
		} else {
			int status;

			status = ice_rem_rss_cfg(hw, vsi->idx, &cfg);
			/* We just ignore -ENOENT, because if two configurations
			 * share the same profile remove one of them actually
			 * removes both, since the profile is deleted.
			 */
			if (status && status != -ENOENT) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				dev_err(dev, "ice_rem_rss_cfg failed for VF ID:%d, error:%d\n",
					vf->vf_id, status);
			}
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
static int ice_vc_config_rss_key(struct ice_vf *vf, u8 *msg)
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
static int ice_vc_config_rss_lut(struct ice_vf *vf, u8 *msg)
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
static int ice_vc_config_rss_hfunc(struct ice_vf *vf, u8 *msg)
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
 * ice_vc_cfg_promiscuous_mode_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to configure VF VSIs promiscuous mode
 */
static int ice_vc_cfg_promiscuous_mode_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	bool rm_promisc, alluni = false, allmulti = false;
	struct virtchnl_promisc_info *info =
	    (struct virtchnl_promisc_info *)msg;
	struct ice_vsi_vlan_ops *vlan_ops;
	int mcast_err = 0, ucast_err = 0;
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi;
	u8 mcast_m, ucast_m;
	struct device *dev;
	int ret = 0;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, info->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	dev = ice_pf_to_dev(pf);
	if (!ice_is_vf_trusted(vf)) {
		dev_err(dev, "Unprivileged VF %d is attempting to configure promiscuous mode\n",
			vf->vf_id);
		/* Leave v_ret alone, lie to the VF on purpose. */
		goto error_param;
	}

	if (info->flags & FLAG_VF_UNICAST_PROMISC)
		alluni = true;

	if (info->flags & FLAG_VF_MULTICAST_PROMISC)
		allmulti = true;

	rm_promisc = !allmulti && !alluni;

	vlan_ops = ice_get_compat_vsi_vlan_ops(vsi);
	if (rm_promisc)
		ret = vlan_ops->ena_rx_filtering(vsi);
	else
		ret = vlan_ops->dis_rx_filtering(vsi);
	if (ret) {
		dev_err(dev, "Failed to configure VLAN pruning in promiscuous mode\n");
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	ice_vf_get_promisc_masks(vf, vsi, &ucast_m, &mcast_m);

	if (!test_bit(ICE_FLAG_VF_TRUE_PROMISC_ENA, pf->flags)) {
		if (alluni) {
			/* in this case we're turning on promiscuous mode */
			ret = ice_set_dflt_vsi(vsi);
		} else {
			/* in this case we're turning off promiscuous mode */
			if (ice_is_dflt_vsi_in_use(vsi->port_info))
				ret = ice_clear_dflt_vsi(vsi);
		}

		/* in this case we're turning on/off only
		 * allmulticast
		 */
		if (allmulti)
			mcast_err = ice_vf_set_vsi_promisc(vf, vsi, mcast_m);
		else
			mcast_err = ice_vf_clear_vsi_promisc(vf, vsi, mcast_m);

		if (ret) {
			dev_err(dev, "Turning on/off promiscuous mode for VF %d failed, error: %d\n",
				vf->vf_id, ret);
			v_ret = VIRTCHNL_STATUS_ERR_ADMIN_QUEUE_ERROR;
			goto error_param;
		}
	} else {
		if (alluni)
			ucast_err = ice_vf_set_vsi_promisc(vf, vsi, ucast_m);
		else
			ucast_err = ice_vf_clear_vsi_promisc(vf, vsi, ucast_m);

		if (allmulti)
			mcast_err = ice_vf_set_vsi_promisc(vf, vsi, mcast_m);
		else
			mcast_err = ice_vf_clear_vsi_promisc(vf, vsi, mcast_m);

		if (ucast_err || mcast_err)
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
	}

	if (!mcast_err) {
		if (allmulti &&
		    !test_and_set_bit(ICE_VF_STATE_MC_PROMISC, vf->vf_states))
			dev_info(dev, "VF %u successfully set multicast promiscuous mode\n",
				 vf->vf_id);
		else if (!allmulti &&
			 test_and_clear_bit(ICE_VF_STATE_MC_PROMISC,
					    vf->vf_states))
			dev_info(dev, "VF %u successfully unset multicast promiscuous mode\n",
				 vf->vf_id);
	} else {
		dev_err(dev, "Error while modifying multicast promiscuous mode for VF %u, error: %d\n",
			vf->vf_id, mcast_err);
	}

	if (!ucast_err) {
		if (alluni &&
		    !test_and_set_bit(ICE_VF_STATE_UC_PROMISC, vf->vf_states))
			dev_info(dev, "VF %u successfully set unicast promiscuous mode\n",
				 vf->vf_id);
		else if (!alluni &&
			 test_and_clear_bit(ICE_VF_STATE_UC_PROMISC,
					    vf->vf_states))
			dev_info(dev, "VF %u successfully unset unicast promiscuous mode\n",
				 vf->vf_id);
	} else {
		dev_err(dev, "Error while modifying unicast promiscuous mode for VF %u, error: %d\n",
			vf->vf_id, ucast_err);
	}

error_param:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE,
				     v_ret, NULL, 0);
}

/**
 * ice_vc_get_stats_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to get VSI stats
 */
static int ice_vc_get_stats_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_queue_select *vqs =
		(struct virtchnl_queue_select *)msg;
	struct ice_eth_stats stats = { 0 };
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vqs->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	ice_update_eth_stats(vsi);

	stats = vsi->eth_stats;

error_param:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_GET_STATS, v_ret,
				     (u8 *)&stats, sizeof(stats));
}

/**
 * ice_vc_validate_vqs_bitmaps - validate Rx/Tx queue bitmaps from VIRTCHNL
 * @vqs: virtchnl_queue_select structure containing bitmaps to validate
 *
 * Return true on successful validation, else false
 */
static bool ice_vc_validate_vqs_bitmaps(struct virtchnl_queue_select *vqs)
{
	if ((!vqs->rx_queues && !vqs->tx_queues) ||
	    vqs->rx_queues >= BIT(ICE_MAX_RSS_QS_PER_VF) ||
	    vqs->tx_queues >= BIT(ICE_MAX_RSS_QS_PER_VF))
		return false;

	return true;
}

/**
 * ice_vf_ena_txq_interrupt - enable Tx queue interrupt via QINT_TQCTL
 * @vsi: VSI of the VF to configure
 * @q_idx: VF queue index used to determine the queue in the PF's space
 */
static void ice_vf_ena_txq_interrupt(struct ice_vsi *vsi, u32 q_idx)
{
	struct ice_hw *hw = &vsi->back->hw;
	u32 pfq = vsi->txq_map[q_idx];
	u32 reg;

	reg = rd32(hw, QINT_TQCTL(pfq));

	/* MSI-X index 0 in the VF's space is always for the OICR, which means
	 * this is most likely a poll mode VF driver, so don't enable an
	 * interrupt that was never configured via VIRTCHNL_OP_CONFIG_IRQ_MAP
	 */
	if (!(reg & QINT_TQCTL_MSIX_INDX_M))
		return;

	wr32(hw, QINT_TQCTL(pfq), reg | QINT_TQCTL_CAUSE_ENA_M);
}

/**
 * ice_vf_ena_rxq_interrupt - enable Tx queue interrupt via QINT_RQCTL
 * @vsi: VSI of the VF to configure
 * @q_idx: VF queue index used to determine the queue in the PF's space
 */
static void ice_vf_ena_rxq_interrupt(struct ice_vsi *vsi, u32 q_idx)
{
	struct ice_hw *hw = &vsi->back->hw;
	u32 pfq = vsi->rxq_map[q_idx];
	u32 reg;

	reg = rd32(hw, QINT_RQCTL(pfq));

	/* MSI-X index 0 in the VF's space is always for the OICR, which means
	 * this is most likely a poll mode VF driver, so don't enable an
	 * interrupt that was never configured via VIRTCHNL_OP_CONFIG_IRQ_MAP
	 */
	if (!(reg & QINT_RQCTL_MSIX_INDX_M))
		return;

	wr32(hw, QINT_RQCTL(pfq), reg | QINT_RQCTL_CAUSE_ENA_M);
}

/**
 * ice_vc_ena_qs_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to enable all or specific queue(s)
 */
static int ice_vc_ena_qs_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_queue_select *vqs =
	    (struct virtchnl_queue_select *)msg;
	struct ice_vsi *vsi;
	unsigned long q_map;
	u16 vf_q_id;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vqs->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_validate_vqs_bitmaps(vqs)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	/* Enable only Rx rings, Tx rings were enabled by the FW when the
	 * Tx queue group list was configured and the context bits were
	 * programmed using ice_vsi_cfg_txqs
	 */
	q_map = vqs->rx_queues;
	for_each_set_bit(vf_q_id, &q_map, ICE_MAX_RSS_QS_PER_VF) {
		if (!ice_vc_isvalid_q_id(vsi, vf_q_id)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		/* Skip queue if enabled */
		if (test_bit(vf_q_id, vf->rxq_ena))
			continue;

		if (ice_vsi_ctrl_one_rx_ring(vsi, true, vf_q_id, true)) {
			dev_err(ice_pf_to_dev(vsi->back), "Failed to enable Rx ring %d on VSI %d\n",
				vf_q_id, vsi->vsi_num);
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		ice_vf_ena_rxq_interrupt(vsi, vf_q_id);
		set_bit(vf_q_id, vf->rxq_ena);
	}

	q_map = vqs->tx_queues;
	for_each_set_bit(vf_q_id, &q_map, ICE_MAX_RSS_QS_PER_VF) {
		if (!ice_vc_isvalid_q_id(vsi, vf_q_id)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		/* Skip queue if enabled */
		if (test_bit(vf_q_id, vf->txq_ena))
			continue;

		ice_vf_ena_txq_interrupt(vsi, vf_q_id);
		set_bit(vf_q_id, vf->txq_ena);
	}

	/* Set flag to indicate that queues are enabled */
	if (v_ret == VIRTCHNL_STATUS_SUCCESS)
		set_bit(ICE_VF_STATE_QS_ENA, vf->vf_states);

error_param:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ENABLE_QUEUES, v_ret,
				     NULL, 0);
}

/**
 * ice_vf_vsi_dis_single_txq - disable a single Tx queue
 * @vf: VF to disable queue for
 * @vsi: VSI for the VF
 * @q_id: VF relative (0-based) queue ID
 *
 * Attempt to disable the Tx queue passed in. If the Tx queue was successfully
 * disabled then clear q_id bit in the enabled queues bitmap and return
 * success. Otherwise return error.
 */
static int
ice_vf_vsi_dis_single_txq(struct ice_vf *vf, struct ice_vsi *vsi, u16 q_id)
{
	struct ice_txq_meta txq_meta = { 0 };
	struct ice_tx_ring *ring;
	int err;

	if (!test_bit(q_id, vf->txq_ena))
		dev_dbg(ice_pf_to_dev(vsi->back), "Queue %u on VSI %u is not enabled, but stopping it anyway\n",
			q_id, vsi->vsi_num);

	ring = vsi->tx_rings[q_id];
	if (!ring)
		return -EINVAL;

	ice_fill_txq_meta(vsi, ring, &txq_meta);

	err = ice_vsi_stop_tx_ring(vsi, ICE_NO_RESET, vf->vf_id, ring, &txq_meta);
	if (err) {
		dev_err(ice_pf_to_dev(vsi->back), "Failed to stop Tx ring %d on VSI %d\n",
			q_id, vsi->vsi_num);
		return err;
	}

	/* Clear enabled queues flag */
	clear_bit(q_id, vf->txq_ena);

	return 0;
}

/**
 * ice_vc_dis_qs_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to disable all or specific queue(s)
 */
static int ice_vc_dis_qs_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_queue_select *vqs =
	    (struct virtchnl_queue_select *)msg;
	struct ice_vsi *vsi;
	unsigned long q_map;
	u16 vf_q_id;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states) &&
	    !test_bit(ICE_VF_STATE_QS_ENA, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vqs->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_validate_vqs_bitmaps(vqs)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (vqs->tx_queues) {
		q_map = vqs->tx_queues;

		for_each_set_bit(vf_q_id, &q_map, ICE_MAX_RSS_QS_PER_VF) {
			if (!ice_vc_isvalid_q_id(vsi, vf_q_id)) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				goto error_param;
			}

			if (ice_vf_vsi_dis_single_txq(vf, vsi, vf_q_id)) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				goto error_param;
			}
		}
	}

	q_map = vqs->rx_queues;
	/* speed up Rx queue disable by batching them if possible */
	if (q_map &&
	    bitmap_equal(&q_map, vf->rxq_ena, ICE_MAX_RSS_QS_PER_VF)) {
		if (ice_vsi_stop_all_rx_rings(vsi)) {
			dev_err(ice_pf_to_dev(vsi->back), "Failed to stop all Rx rings on VSI %d\n",
				vsi->vsi_num);
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		bitmap_zero(vf->rxq_ena, ICE_MAX_RSS_QS_PER_VF);
	} else if (q_map) {
		for_each_set_bit(vf_q_id, &q_map, ICE_MAX_RSS_QS_PER_VF) {
			if (!ice_vc_isvalid_q_id(vsi, vf_q_id)) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				goto error_param;
			}

			/* Skip queue if not enabled */
			if (!test_bit(vf_q_id, vf->rxq_ena))
				continue;

			if (ice_vsi_ctrl_one_rx_ring(vsi, false, vf_q_id,
						     true)) {
				dev_err(ice_pf_to_dev(vsi->back), "Failed to stop Rx ring %d on VSI %d\n",
					vf_q_id, vsi->vsi_num);
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				goto error_param;
			}

			/* Clear enabled queues flag */
			clear_bit(vf_q_id, vf->rxq_ena);
		}
	}

	/* Clear enabled queues flag */
	if (v_ret == VIRTCHNL_STATUS_SUCCESS && ice_vf_has_no_qs_ena(vf))
		clear_bit(ICE_VF_STATE_QS_ENA, vf->vf_states);

error_param:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DISABLE_QUEUES, v_ret,
				     NULL, 0);
}

/**
 * ice_cfg_interrupt
 * @vf: pointer to the VF info
 * @vsi: the VSI being configured
 * @map: vector map for mapping vectors to queues
 * @q_vector: structure for interrupt vector
 * configure the IRQ to queue map
 */
static enum virtchnl_status_code
ice_cfg_interrupt(struct ice_vf *vf, struct ice_vsi *vsi,
		  struct virtchnl_vector_map *map,
		  struct ice_q_vector *q_vector)
{
	u16 vsi_q_id, vsi_q_id_idx;
	unsigned long qmap;

	q_vector->num_ring_rx = 0;
	q_vector->num_ring_tx = 0;

	qmap = map->rxq_map;
	for_each_set_bit(vsi_q_id_idx, &qmap, ICE_MAX_RSS_QS_PER_VF) {
		vsi_q_id = vsi_q_id_idx;

		if (!ice_vc_isvalid_q_id(vsi, vsi_q_id))
			return VIRTCHNL_STATUS_ERR_PARAM;

		q_vector->num_ring_rx++;
		q_vector->rx.itr_idx = map->rxitr_idx;
		vsi->rx_rings[vsi_q_id]->q_vector = q_vector;
		ice_cfg_rxq_interrupt(vsi, vsi_q_id,
				      q_vector->vf_reg_idx,
				      q_vector->rx.itr_idx);
	}

	qmap = map->txq_map;
	for_each_set_bit(vsi_q_id_idx, &qmap, ICE_MAX_RSS_QS_PER_VF) {
		vsi_q_id = vsi_q_id_idx;

		if (!ice_vc_isvalid_q_id(vsi, vsi_q_id))
			return VIRTCHNL_STATUS_ERR_PARAM;

		q_vector->num_ring_tx++;
		q_vector->tx.itr_idx = map->txitr_idx;
		vsi->tx_rings[vsi_q_id]->q_vector = q_vector;
		ice_cfg_txq_interrupt(vsi, vsi_q_id,
				      q_vector->vf_reg_idx,
				      q_vector->tx.itr_idx);
	}

	return VIRTCHNL_STATUS_SUCCESS;
}

/**
 * ice_vc_cfg_irq_map_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to configure the IRQ to queue map
 */
static int ice_vc_cfg_irq_map_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	u16 num_q_vectors_mapped, vsi_id, vector_id;
	struct virtchnl_irq_map_info *irqmap_info;
	struct virtchnl_vector_map *map;
	struct ice_vsi *vsi;
	int i;

	irqmap_info = (struct virtchnl_irq_map_info *)msg;
	num_q_vectors_mapped = irqmap_info->num_vectors;

	/* Check to make sure number of VF vectors mapped is not greater than
	 * number of VF vectors originally allocated, and check that
	 * there is actually at least a single VF queue vector mapped
	 */
	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states) ||
	    vf->num_msix < num_q_vectors_mapped ||
	    !num_q_vectors_mapped) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	for (i = 0; i < num_q_vectors_mapped; i++) {
		struct ice_q_vector *q_vector;

		map = &irqmap_info->vecmap[i];

		vector_id = map->vector_id;
		vsi_id = map->vsi_id;
		/* vector_id is always 0-based for each VF, and can never be
		 * larger than or equal to the max allowed interrupts per VF
		 */
		if (!(vector_id < vf->num_msix) ||
		    !ice_vc_isvalid_vsi_id(vf, vsi_id) ||
		    (!vector_id && (map->rxq_map || map->txq_map))) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		/* No need to map VF miscellaneous or rogue vector */
		if (!vector_id)
			continue;

		/* Subtract non queue vector from vector_id passed by VF
		 * to get actual number of VSI queue vector array index
		 */
		q_vector = vsi->q_vectors[vector_id - ICE_NONQ_VECS_VF];
		if (!q_vector) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		/* lookout for the invalid queue index */
		v_ret = ice_cfg_interrupt(vf, vsi, map, q_vector);
		if (v_ret)
			goto error_param;
	}

error_param:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_IRQ_MAP, v_ret,
				     NULL, 0);
}

/**
 * ice_vc_cfg_qs_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to configure the Rx/Tx queues
 */
static int ice_vc_cfg_qs_msg(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_vsi_queue_config_info *qci =
	    (struct virtchnl_vsi_queue_config_info *)msg;
	struct virtchnl_queue_pair_info *qpi;
	struct ice_pf *pf = vf->pf;
	struct ice_lag *lag;
	struct ice_vsi *vsi;
	u8 act_prt, pri_prt;
	int i = -1, q_idx;

	lag = pf->lag;
	mutex_lock(&pf->lag_mutex);
	act_prt = ICE_LAG_INVALID_PORT;
	pri_prt = pf->hw.port_info->lport;
	if (lag && lag->bonded && lag->primary) {
		act_prt = lag->active_port;
		if (act_prt != pri_prt && act_prt != ICE_LAG_INVALID_PORT &&
		    lag->upper_netdev)
			ice_lag_move_vf_nodes_cfg(lag, act_prt, pri_prt);
		else
			act_prt = ICE_LAG_INVALID_PORT;
	}

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states))
		goto error_param;

	if (!ice_vc_isvalid_vsi_id(vf, qci->vsi_id))
		goto error_param;

	vsi = ice_get_vf_vsi(vf);
	if (!vsi)
		goto error_param;

	if (qci->num_queue_pairs > ICE_MAX_RSS_QS_PER_VF ||
	    qci->num_queue_pairs > min_t(u16, vsi->alloc_txq, vsi->alloc_rxq)) {
		dev_err(ice_pf_to_dev(pf), "VF-%d requesting more than supported number of queues: %d\n",
			vf->vf_id, min_t(u16, vsi->alloc_txq, vsi->alloc_rxq));
		goto error_param;
	}

	for (i = 0; i < qci->num_queue_pairs; i++) {
		if (!qci->qpair[i].rxq.crc_disable)
			continue;

		if (!(vf->driver_caps & VIRTCHNL_VF_OFFLOAD_CRC) ||
		    vf->vlan_strip_ena)
			goto error_param;
	}

	for (i = 0; i < qci->num_queue_pairs; i++) {
		qpi = &qci->qpair[i];
		if (qpi->txq.vsi_id != qci->vsi_id ||
		    qpi->rxq.vsi_id != qci->vsi_id ||
		    qpi->rxq.queue_id != qpi->txq.queue_id ||
		    qpi->txq.headwb_enabled ||
		    !ice_vc_isvalid_ring_len(qpi->txq.ring_len) ||
		    !ice_vc_isvalid_ring_len(qpi->rxq.ring_len) ||
		    !ice_vc_isvalid_q_id(vsi, qpi->txq.queue_id)) {
			goto error_param;
		}

		q_idx = qpi->rxq.queue_id;

		/* make sure selected "q_idx" is in valid range of queues
		 * for selected "vsi"
		 */
		if (q_idx >= vsi->alloc_txq || q_idx >= vsi->alloc_rxq) {
			goto error_param;
		}

		/* copy Tx queue info from VF into VSI */
		if (qpi->txq.ring_len > 0) {
			vsi->tx_rings[i]->dma = qpi->txq.dma_ring_addr;
			vsi->tx_rings[i]->count = qpi->txq.ring_len;

			/* Disable any existing queue first */
			if (ice_vf_vsi_dis_single_txq(vf, vsi, q_idx))
				goto error_param;

			/* Configure a queue with the requested settings */
			if (ice_vsi_cfg_single_txq(vsi, vsi->tx_rings, q_idx)) {
				dev_warn(ice_pf_to_dev(pf), "VF-%d failed to configure TX queue %d\n",
					 vf->vf_id, i);
				goto error_param;
			}
		}

		/* copy Rx queue info from VF into VSI */
		if (qpi->rxq.ring_len > 0) {
			u16 max_frame_size = ice_vc_get_max_frame_size(vf);
			u32 rxdid;

			vsi->rx_rings[i]->dma = qpi->rxq.dma_ring_addr;
			vsi->rx_rings[i]->count = qpi->rxq.ring_len;

			if (qpi->rxq.crc_disable)
				vsi->rx_rings[q_idx]->flags |=
					ICE_RX_FLAGS_CRC_STRIP_DIS;
			else
				vsi->rx_rings[q_idx]->flags &=
					~ICE_RX_FLAGS_CRC_STRIP_DIS;

			if (qpi->rxq.databuffer_size != 0 &&
			    (qpi->rxq.databuffer_size > ((16 * 1024) - 128) ||
			     qpi->rxq.databuffer_size < 1024))
				goto error_param;
			vsi->rx_buf_len = qpi->rxq.databuffer_size;
			vsi->rx_rings[i]->rx_buf_len = vsi->rx_buf_len;
			if (qpi->rxq.max_pkt_size > max_frame_size ||
			    qpi->rxq.max_pkt_size < 64)
				goto error_param;

			vsi->max_frame = qpi->rxq.max_pkt_size;
			/* add space for the port VLAN since the VF driver is
			 * not expected to account for it in the MTU
			 * calculation
			 */
			if (ice_vf_is_port_vlan_ena(vf))
				vsi->max_frame += VLAN_HLEN;

			if (ice_vsi_cfg_single_rxq(vsi, q_idx)) {
				dev_warn(ice_pf_to_dev(pf), "VF-%d failed to configure RX queue %d\n",
					 vf->vf_id, i);
				goto error_param;
			}

			/* If Rx flex desc is supported, select RXDID for Rx
			 * queues. Otherwise, use legacy 32byte descriptor
			 * format. Legacy 16byte descriptor is not supported.
			 * If this RXDID is selected, return error.
			 */
			if (vf->driver_caps &
			    VIRTCHNL_VF_OFFLOAD_RX_FLEX_DESC) {
				rxdid = qpi->rxq.rxdid;
				if (!(BIT(rxdid) & pf->supported_rxdids))
					goto error_param;
			} else {
				rxdid = ICE_RXDID_LEGACY_1;
			}

			ice_write_qrxflxp_cntxt(&vsi->back->hw,
						vsi->rxq_map[q_idx],
						rxdid, 0x03, false);
		}
	}

	if (lag && lag->bonded && lag->primary &&
	    act_prt != ICE_LAG_INVALID_PORT)
		ice_lag_move_vf_nodes_cfg(lag, pri_prt, act_prt);
	mutex_unlock(&pf->lag_mutex);

	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_VSI_QUEUES,
				     VIRTCHNL_STATUS_SUCCESS, NULL, 0);
error_param:
	/* disable whatever we can */
	for (; i >= 0; i--) {
		if (ice_vsi_ctrl_one_rx_ring(vsi, false, i, true))
			dev_err(ice_pf_to_dev(pf), "VF-%d could not disable RX queue %d\n",
				vf->vf_id, i);
		if (ice_vf_vsi_dis_single_txq(vf, vsi, i))
			dev_err(ice_pf_to_dev(pf), "VF-%d could not disable TX queue %d\n",
				vf->vf_id, i);
	}

	if (lag && lag->bonded && lag->primary &&
	    act_prt != ICE_LAG_INVALID_PORT)
		ice_lag_move_vf_nodes_cfg(lag, pri_prt, act_prt);
	mutex_unlock(&pf->lag_mutex);

	ice_lag_move_new_vf_nodes(vf);

	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_VSI_QUEUES,
				     VIRTCHNL_STATUS_ERR_PARAM, NULL, 0);
}

/**
 * ice_can_vf_change_mac
 * @vf: pointer to the VF info
 *
 * Return true if the VF is allowed to change its MAC filters, false otherwise
 */
static bool ice_can_vf_change_mac(struct ice_vf *vf)
{
	/* If the VF MAC address has been set administratively (via the
	 * ndo_set_vf_mac command), then deny permission to the VF to
	 * add/delete unicast MAC addresses, unless the VF is trusted
	 */
	if (vf->pf_set_mac && !ice_is_vf_trusted(vf))
		return false;

	return true;
}

/**
 * ice_vc_ether_addr_type - get type of virtchnl_ether_addr
 * @vc_ether_addr: used to extract the type
 */
static u8
ice_vc_ether_addr_type(struct virtchnl_ether_addr *vc_ether_addr)
{
	return (vc_ether_addr->type & VIRTCHNL_ETHER_ADDR_TYPE_MASK);
}

/**
 * ice_is_vc_addr_legacy - check if the MAC address is from an older VF
 * @vc_ether_addr: VIRTCHNL structure that contains MAC and type
 */
static bool
ice_is_vc_addr_legacy(struct virtchnl_ether_addr *vc_ether_addr)
{
	u8 type = ice_vc_ether_addr_type(vc_ether_addr);

	return (type == VIRTCHNL_ETHER_ADDR_LEGACY);
}

/**
 * ice_is_vc_addr_primary - check if the MAC address is the VF's primary MAC
 * @vc_ether_addr: VIRTCHNL structure that contains MAC and type
 *
 * This function should only be called when the MAC address in
 * virtchnl_ether_addr is a valid unicast MAC
 */
static bool
ice_is_vc_addr_primary(struct virtchnl_ether_addr __maybe_unused *vc_ether_addr)
{
	u8 type = ice_vc_ether_addr_type(vc_ether_addr);

	return (type == VIRTCHNL_ETHER_ADDR_PRIMARY);
}

/**
 * ice_vfhw_mac_add - update the VF's cached hardware MAC if allowed
 * @vf: VF to update
 * @vc_ether_addr: structure from VIRTCHNL with MAC to add
 */
static void
ice_vfhw_mac_add(struct ice_vf *vf, struct virtchnl_ether_addr *vc_ether_addr)
{
	u8 *mac_addr = vc_ether_addr->addr;

	if (!is_valid_ether_addr(mac_addr))
		return;

	/* only allow legacy VF drivers to set the device and hardware MAC if it
	 * is zero and allow new VF drivers to set the hardware MAC if the type
	 * was correctly specified over VIRTCHNL
	 */
	if ((ice_is_vc_addr_legacy(vc_ether_addr) &&
	     is_zero_ether_addr(vf->hw_lan_addr)) ||
	    ice_is_vc_addr_primary(vc_ether_addr)) {
		ether_addr_copy(vf->dev_lan_addr, mac_addr);
		ether_addr_copy(vf->hw_lan_addr, mac_addr);
	}

	/* hardware and device MACs are already set, but its possible that the
	 * VF driver sent the VIRTCHNL_OP_ADD_ETH_ADDR message before the
	 * VIRTCHNL_OP_DEL_ETH_ADDR when trying to update its MAC, so save it
	 * away for the legacy VF driver case as it will be updated in the
	 * delete flow for this case
	 */
	if (ice_is_vc_addr_legacy(vc_ether_addr)) {
		ether_addr_copy(vf->legacy_last_added_umac.addr,
				mac_addr);
		vf->legacy_last_added_umac.time_modified = jiffies;
	}
}

/**
 * ice_vc_add_mac_addr - attempt to add the MAC address passed in
 * @vf: pointer to the VF info
 * @vsi: pointer to the VF's VSI
 * @vc_ether_addr: VIRTCHNL MAC address structure used to add MAC
 */
static int
ice_vc_add_mac_addr(struct ice_vf *vf, struct ice_vsi *vsi,
		    struct virtchnl_ether_addr *vc_ether_addr)
{
	struct device *dev = ice_pf_to_dev(vf->pf);
	u8 *mac_addr = vc_ether_addr->addr;
	int ret;

	/* device MAC already added */
	if (ether_addr_equal(mac_addr, vf->dev_lan_addr))
		return 0;

	if (is_unicast_ether_addr(mac_addr) && !ice_can_vf_change_mac(vf)) {
		dev_err(dev, "VF attempting to override administratively set MAC address, bring down and up the VF interface to resume normal operation\n");
		return -EPERM;
	}

	ret = ice_fltr_add_mac(vsi, mac_addr, ICE_FWD_TO_VSI);
	if (ret == -EEXIST) {
		dev_dbg(dev, "MAC %pM already exists for VF %d\n", mac_addr,
			vf->vf_id);
		/* don't return since we might need to update
		 * the primary MAC in ice_vfhw_mac_add() below
		 */
	} else if (ret) {
		dev_err(dev, "Failed to add MAC %pM for VF %d\n, error %d\n",
			mac_addr, vf->vf_id, ret);
		return ret;
	} else {
		vf->num_mac++;
	}

	ice_vfhw_mac_add(vf, vc_ether_addr);

	return ret;
}

/**
 * ice_is_legacy_umac_expired - check if last added legacy unicast MAC expired
 * @last_added_umac: structure used to check expiration
 */
static bool ice_is_legacy_umac_expired(struct ice_time_mac *last_added_umac)
{
#define ICE_LEGACY_VF_MAC_CHANGE_EXPIRE_TIME	msecs_to_jiffies(3000)
	return time_is_before_jiffies(last_added_umac->time_modified +
				      ICE_LEGACY_VF_MAC_CHANGE_EXPIRE_TIME);
}

/**
 * ice_update_legacy_cached_mac - update cached hardware MAC for legacy VF
 * @vf: VF to update
 * @vc_ether_addr: structure from VIRTCHNL with MAC to check
 *
 * only update cached hardware MAC for legacy VF drivers on delete
 * because we cannot guarantee order/type of MAC from the VF driver
 */
static void
ice_update_legacy_cached_mac(struct ice_vf *vf,
			     struct virtchnl_ether_addr *vc_ether_addr)
{
	if (!ice_is_vc_addr_legacy(vc_ether_addr) ||
	    ice_is_legacy_umac_expired(&vf->legacy_last_added_umac))
		return;

	ether_addr_copy(vf->dev_lan_addr, vf->legacy_last_added_umac.addr);
	ether_addr_copy(vf->hw_lan_addr, vf->legacy_last_added_umac.addr);
}

/**
 * ice_vfhw_mac_del - update the VF's cached hardware MAC if allowed
 * @vf: VF to update
 * @vc_ether_addr: structure from VIRTCHNL with MAC to delete
 */
static void
ice_vfhw_mac_del(struct ice_vf *vf, struct virtchnl_ether_addr *vc_ether_addr)
{
	u8 *mac_addr = vc_ether_addr->addr;

	if (!is_valid_ether_addr(mac_addr) ||
	    !ether_addr_equal(vf->dev_lan_addr, mac_addr))
		return;

	/* allow the device MAC to be repopulated in the add flow and don't
	 * clear the hardware MAC (i.e. hw_lan_addr) here as that is meant
	 * to be persistent on VM reboot and across driver unload/load, which
	 * won't work if we clear the hardware MAC here
	 */
	eth_zero_addr(vf->dev_lan_addr);

	ice_update_legacy_cached_mac(vf, vc_ether_addr);
}

/**
 * ice_vc_del_mac_addr - attempt to delete the MAC address passed in
 * @vf: pointer to the VF info
 * @vsi: pointer to the VF's VSI
 * @vc_ether_addr: VIRTCHNL MAC address structure used to delete MAC
 */
static int
ice_vc_del_mac_addr(struct ice_vf *vf, struct ice_vsi *vsi,
		    struct virtchnl_ether_addr *vc_ether_addr)
{
	struct device *dev = ice_pf_to_dev(vf->pf);
	u8 *mac_addr = vc_ether_addr->addr;
	int status;

	if (!ice_can_vf_change_mac(vf) &&
	    ether_addr_equal(vf->dev_lan_addr, mac_addr))
		return 0;

	status = ice_fltr_remove_mac(vsi, mac_addr, ICE_FWD_TO_VSI);
	if (status == -ENOENT) {
		dev_err(dev, "MAC %pM does not exist for VF %d\n", mac_addr,
			vf->vf_id);
		return -ENOENT;
	} else if (status) {
		dev_err(dev, "Failed to delete MAC %pM for VF %d, error %d\n",
			mac_addr, vf->vf_id, status);
		return -EIO;
	}

	ice_vfhw_mac_del(vf, vc_ether_addr);

	vf->num_mac--;

	return 0;
}

/**
 * ice_vc_handle_mac_addr_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 * @set: true if MAC filters are being set, false otherwise
 *
 * add guest MAC address filter
 */
static int
ice_vc_handle_mac_addr_msg(struct ice_vf *vf, u8 *msg, bool set)
{
	int (*ice_vc_cfg_mac)
		(struct ice_vf *vf, struct ice_vsi *vsi,
		 struct virtchnl_ether_addr *virtchnl_ether_addr);
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_ether_addr_list *al =
	    (struct virtchnl_ether_addr_list *)msg;
	struct ice_pf *pf = vf->pf;
	enum virtchnl_ops vc_op;
	struct ice_vsi *vsi;
	int i;

	if (set) {
		vc_op = VIRTCHNL_OP_ADD_ETH_ADDR;
		ice_vc_cfg_mac = ice_vc_add_mac_addr;
	} else {
		vc_op = VIRTCHNL_OP_DEL_ETH_ADDR;
		ice_vc_cfg_mac = ice_vc_del_mac_addr;
	}

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states) ||
	    !ice_vc_isvalid_vsi_id(vf, al->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto handle_mac_exit;
	}

	/* If this VF is not privileged, then we can't add more than a
	 * limited number of addresses. Check to make sure that the
	 * additions do not push us over the limit.
	 */
	if (set && !ice_is_vf_trusted(vf) &&
	    (vf->num_mac + al->num_elements) > ICE_MAX_MACADDR_PER_VF) {
		dev_err(ice_pf_to_dev(pf), "Can't add more MAC addresses, because VF-%d is not trusted, switch the VF to trusted mode in order to add more functionalities\n",
			vf->vf_id);
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto handle_mac_exit;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto handle_mac_exit;
	}

	for (i = 0; i < al->num_elements; i++) {
		u8 *mac_addr = al->list[i].addr;
		int result;

		if (is_broadcast_ether_addr(mac_addr) ||
		    is_zero_ether_addr(mac_addr))
			continue;

		result = ice_vc_cfg_mac(vf, vsi, &al->list[i]);
		if (result == -EEXIST || result == -ENOENT) {
			continue;
		} else if (result) {
			v_ret = VIRTCHNL_STATUS_ERR_ADMIN_QUEUE_ERROR;
			goto handle_mac_exit;
		}
	}

handle_mac_exit:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, vc_op, v_ret, NULL, 0);
}

/**
 * ice_vc_add_mac_addr_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * add guest MAC address filter
 */
static int ice_vc_add_mac_addr_msg(struct ice_vf *vf, u8 *msg)
{
	return ice_vc_handle_mac_addr_msg(vf, msg, true);
}

/**
 * ice_vc_del_mac_addr_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * remove guest MAC address filter
 */
static int ice_vc_del_mac_addr_msg(struct ice_vf *vf, u8 *msg)
{
	return ice_vc_handle_mac_addr_msg(vf, msg, false);
}

/**
 * ice_vc_request_qs_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * VFs get a default number of queues but can use this message to request a
 * different number. If the request is successful, PF will reset the VF and
 * return 0. If unsuccessful, PF will send message informing VF of number of
 * available queue pairs via virtchnl message response to VF.
 */
static int ice_vc_request_qs_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vf_res_request *vfres =
		(struct virtchnl_vf_res_request *)msg;
	u16 req_queues = vfres->num_queue_pairs;
	struct ice_pf *pf = vf->pf;
	u16 max_allowed_vf_queues;
	u16 tx_rx_queue_left;
	struct device *dev;
	u16 cur_queues;

	dev = ice_pf_to_dev(pf);
	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	cur_queues = vf->num_vf_qs;
	tx_rx_queue_left = min_t(u16, ice_get_avail_txq_count(pf),
				 ice_get_avail_rxq_count(pf));
	max_allowed_vf_queues = tx_rx_queue_left + cur_queues;
	if (!req_queues) {
		dev_err(dev, "VF %d tried to request 0 queues. Ignoring.\n",
			vf->vf_id);
	} else if (req_queues > ICE_MAX_RSS_QS_PER_VF) {
		dev_err(dev, "VF %d tried to request more than %d queues.\n",
			vf->vf_id, ICE_MAX_RSS_QS_PER_VF);
		vfres->num_queue_pairs = ICE_MAX_RSS_QS_PER_VF;
	} else if (req_queues > cur_queues &&
		   req_queues - cur_queues > tx_rx_queue_left) {
		dev_warn(dev, "VF %d requested %u more queues, but only %u left.\n",
			 vf->vf_id, req_queues - cur_queues, tx_rx_queue_left);
		vfres->num_queue_pairs = min_t(u16, max_allowed_vf_queues,
					       ICE_MAX_RSS_QS_PER_VF);
	} else {
		/* request is successful, then reset VF */
		vf->num_req_qs = req_queues;
		ice_reset_vf(vf, ICE_VF_RESET_NOTIFY);
		dev_info(dev, "VF %d granted request of %u queues.\n",
			 vf->vf_id, req_queues);
		return 0;
	}

error_param:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_REQUEST_QUEUES,
				     v_ret, (u8 *)vfres, sizeof(*vfres));
}

/**
 * ice_vf_vlan_offload_ena - determine if capabilities support VLAN offloads
 * @caps: VF driver negotiated capabilities
 *
 * Return true if VIRTCHNL_VF_OFFLOAD_VLAN capability is set, else return false
 */
static bool ice_vf_vlan_offload_ena(u32 caps)
{
	return !!(caps & VIRTCHNL_VF_OFFLOAD_VLAN);
}

/**
 * ice_is_vlan_promisc_allowed - check if VLAN promiscuous config is allowed
 * @vf: VF used to determine if VLAN promiscuous config is allowed
 */
static bool ice_is_vlan_promisc_allowed(struct ice_vf *vf)
{
	if ((test_bit(ICE_VF_STATE_UC_PROMISC, vf->vf_states) ||
	     test_bit(ICE_VF_STATE_MC_PROMISC, vf->vf_states)) &&
	    test_bit(ICE_FLAG_VF_TRUE_PROMISC_ENA, vf->pf->flags))
		return true;

	return false;
}

/**
 * ice_vf_ena_vlan_promisc - Enable Tx/Rx VLAN promiscuous for the VLAN
 * @vsi: VF's VSI used to enable VLAN promiscuous mode
 * @vlan: VLAN used to enable VLAN promiscuous
 *
 * This function should only be called if VLAN promiscuous mode is allowed,
 * which can be determined via ice_is_vlan_promisc_allowed().
 */
static int ice_vf_ena_vlan_promisc(struct ice_vsi *vsi, struct ice_vlan *vlan)
{
	u8 promisc_m = ICE_PROMISC_VLAN_TX | ICE_PROMISC_VLAN_RX;
	int status;

	status = ice_fltr_set_vsi_promisc(&vsi->back->hw, vsi->idx, promisc_m,
					  vlan->vid);
	if (status && status != -EEXIST)
		return status;

	return 0;
}

/**
 * ice_vf_dis_vlan_promisc - Disable Tx/Rx VLAN promiscuous for the VLAN
 * @vsi: VF's VSI used to disable VLAN promiscuous mode for
 * @vlan: VLAN used to disable VLAN promiscuous
 *
 * This function should only be called if VLAN promiscuous mode is allowed,
 * which can be determined via ice_is_vlan_promisc_allowed().
 */
static int ice_vf_dis_vlan_promisc(struct ice_vsi *vsi, struct ice_vlan *vlan)
{
	u8 promisc_m = ICE_PROMISC_VLAN_TX | ICE_PROMISC_VLAN_RX;
	int status;

	status = ice_fltr_clear_vsi_promisc(&vsi->back->hw, vsi->idx, promisc_m,
					    vlan->vid);
	if (status && status != -ENOENT)
		return status;

	return 0;
}

/**
 * ice_vf_has_max_vlans - check if VF already has the max allowed VLAN filters
 * @vf: VF to check against
 * @vsi: VF's VSI
 *
 * If the VF is trusted then the VF is allowed to add as many VLANs as it
 * wants to, so return false.
 *
 * When the VF is untrusted compare the number of non-zero VLANs + 1 to the max
 * allowed VLANs for an untrusted VF. Return the result of this comparison.
 */
static bool ice_vf_has_max_vlans(struct ice_vf *vf, struct ice_vsi *vsi)
{
	if (ice_is_vf_trusted(vf))
		return false;

#define ICE_VF_ADDED_VLAN_ZERO_FLTRS	1
	return ((ice_vsi_num_non_zero_vlans(vsi) +
		ICE_VF_ADDED_VLAN_ZERO_FLTRS) >= ICE_MAX_VLAN_PER_VF);
}

/**
 * ice_vc_process_vlan_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 * @add_v: Add VLAN if true, otherwise delete VLAN
 *
 * Process virtchnl op to add or remove programmed guest VLAN ID
 */
static int ice_vc_process_vlan_msg(struct ice_vf *vf, u8 *msg, bool add_v)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vlan_filter_list *vfl =
	    (struct virtchnl_vlan_filter_list *)msg;
	struct ice_pf *pf = vf->pf;
	bool vlan_promisc = false;
	struct ice_vsi *vsi;
	struct device *dev;
	int status = 0;
	int i;

	dev = ice_pf_to_dev(pf);
	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vf_vlan_offload_ena(vf->driver_caps)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vfl->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	for (i = 0; i < vfl->num_elements; i++) {
		if (vfl->vlan_id[i] >= VLAN_N_VID) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			dev_err(dev, "invalid VF VLAN id %d\n",
				vfl->vlan_id[i]);
			goto error_param;
		}
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (add_v && ice_vf_has_max_vlans(vf, vsi)) {
		dev_info(dev, "VF-%d is not trusted, switch the VF to trusted mode, in order to add more VLAN addresses\n",
			 vf->vf_id);
		/* There is no need to let VF know about being not trusted,
		 * so we can just return success message here
		 */
		goto error_param;
	}

	/* in DVM a VF can add/delete inner VLAN filters when
	 * VIRTCHNL_VF_OFFLOAD_VLAN is negotiated, so only reject in SVM
	 */
	if (ice_vf_is_port_vlan_ena(vf) && !ice_is_dvm_ena(&pf->hw)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	/* in DVM VLAN promiscuous is based on the outer VLAN, which would be
	 * the port VLAN if VIRTCHNL_VF_OFFLOAD_VLAN was negotiated, so only
	 * allow vlan_promisc = true in SVM and if no port VLAN is configured
	 */
	vlan_promisc = ice_is_vlan_promisc_allowed(vf) &&
		!ice_is_dvm_ena(&pf->hw) &&
		!ice_vf_is_port_vlan_ena(vf);

	if (add_v) {
		for (i = 0; i < vfl->num_elements; i++) {
			u16 vid = vfl->vlan_id[i];
			struct ice_vlan vlan;

			if (ice_vf_has_max_vlans(vf, vsi)) {
				dev_info(dev, "VF-%d is not trusted, switch the VF to trusted mode, in order to add more VLAN addresses\n",
					 vf->vf_id);
				/* There is no need to let VF know about being
				 * not trusted, so we can just return success
				 * message here as well.
				 */
				goto error_param;
			}

			/* we add VLAN 0 by default for each VF so we can enable
			 * Tx VLAN anti-spoof without triggering MDD events so
			 * we don't need to add it again here
			 */
			if (!vid)
				continue;

			vlan = ICE_VLAN(ETH_P_8021Q, vid, 0);
			status = vsi->inner_vlan_ops.add_vlan(vsi, &vlan);
			if (status) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				goto error_param;
			}

			/* Enable VLAN filtering on first non-zero VLAN */
			if (!vlan_promisc && vid && !ice_is_dvm_ena(&pf->hw)) {
				if (vf->spoofchk) {
					status = vsi->inner_vlan_ops.ena_tx_filtering(vsi);
					if (status) {
						v_ret = VIRTCHNL_STATUS_ERR_PARAM;
						dev_err(dev, "Enable VLAN anti-spoofing on VLAN ID: %d failed error-%d\n",
							vid, status);
						goto error_param;
					}
				}
				if (vsi->inner_vlan_ops.ena_rx_filtering(vsi)) {
					v_ret = VIRTCHNL_STATUS_ERR_PARAM;
					dev_err(dev, "Enable VLAN pruning on VLAN ID: %d failed error-%d\n",
						vid, status);
					goto error_param;
				}
			} else if (vlan_promisc) {
				status = ice_vf_ena_vlan_promisc(vsi, &vlan);
				if (status) {
					v_ret = VIRTCHNL_STATUS_ERR_PARAM;
					dev_err(dev, "Enable Unicast/multicast promiscuous mode on VLAN ID:%d failed error-%d\n",
						vid, status);
				}
			}
		}
	} else {
		/* In case of non_trusted VF, number of VLAN elements passed
		 * to PF for removal might be greater than number of VLANs
		 * filter programmed for that VF - So, use actual number of
		 * VLANS added earlier with add VLAN opcode. In order to avoid
		 * removing VLAN that doesn't exist, which result to sending
		 * erroneous failed message back to the VF
		 */
		int num_vf_vlan;

		num_vf_vlan = vsi->num_vlan;
		for (i = 0; i < vfl->num_elements && i < num_vf_vlan; i++) {
			u16 vid = vfl->vlan_id[i];
			struct ice_vlan vlan;

			/* we add VLAN 0 by default for each VF so we can enable
			 * Tx VLAN anti-spoof without triggering MDD events so
			 * we don't want a VIRTCHNL request to remove it
			 */
			if (!vid)
				continue;

			vlan = ICE_VLAN(ETH_P_8021Q, vid, 0);
			status = vsi->inner_vlan_ops.del_vlan(vsi, &vlan);
			if (status) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				goto error_param;
			}

			/* Disable VLAN filtering when only VLAN 0 is left */
			if (!ice_vsi_has_non_zero_vlans(vsi)) {
				vsi->inner_vlan_ops.dis_tx_filtering(vsi);
				vsi->inner_vlan_ops.dis_rx_filtering(vsi);
			}

			if (vlan_promisc)
				ice_vf_dis_vlan_promisc(vsi, &vlan);
		}
	}

error_param:
	/* send the response to the VF */
	if (add_v)
		return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ADD_VLAN, v_ret,
					     NULL, 0);
	else
		return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DEL_VLAN, v_ret,
					     NULL, 0);
}

/**
 * ice_vc_add_vlan_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * Add and program guest VLAN ID
 */
static int ice_vc_add_vlan_msg(struct ice_vf *vf, u8 *msg)
{
	return ice_vc_process_vlan_msg(vf, msg, true);
}

/**
 * ice_vc_remove_vlan_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * remove programmed guest VLAN ID
 */
static int ice_vc_remove_vlan_msg(struct ice_vf *vf, u8 *msg)
{
	return ice_vc_process_vlan_msg(vf, msg, false);
}

/**
 * ice_vsi_is_rxq_crc_strip_dis - check if Rx queue CRC strip is disabled or not
 * @vsi: pointer to the VF VSI info
 */
static bool ice_vsi_is_rxq_crc_strip_dis(struct ice_vsi *vsi)
{
	unsigned int i;

	ice_for_each_alloc_rxq(vsi, i)
		if (vsi->rx_rings[i]->flags & ICE_RX_FLAGS_CRC_STRIP_DIS)
			return true;

	return false;
}

/**
 * ice_vc_ena_vlan_stripping
 * @vf: pointer to the VF info
 *
 * Enable VLAN header stripping for a given VF
 */
static int ice_vc_ena_vlan_stripping(struct ice_vf *vf)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vf_vlan_offload_ena(vf->driver_caps)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (vsi->inner_vlan_ops.ena_stripping(vsi, ETH_P_8021Q))
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
	else
		vf->vlan_strip_ena |= ICE_INNER_VLAN_STRIP_ENA;

error_param:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ENABLE_VLAN_STRIPPING,
				     v_ret, NULL, 0);
}

/**
 * ice_vc_dis_vlan_stripping
 * @vf: pointer to the VF info
 *
 * Disable VLAN header stripping for a given VF
 */
static int ice_vc_dis_vlan_stripping(struct ice_vf *vf)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vf_vlan_offload_ena(vf->driver_caps)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (vsi->inner_vlan_ops.dis_stripping(vsi))
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
	else
		vf->vlan_strip_ena &= ~ICE_INNER_VLAN_STRIP_ENA;

error_param:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DISABLE_VLAN_STRIPPING,
				     v_ret, NULL, 0);
}

/**
 * ice_vc_get_rss_hena - return the RSS HENA bits allowed by the hardware
 * @vf: pointer to the VF info
 */
static int ice_vc_get_rss_hena(struct ice_vf *vf)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_rss_hena *vrh = NULL;
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

	len = sizeof(struct virtchnl_rss_hena);
	vrh = kzalloc(len, GFP_KERNEL);
	if (!vrh) {
		v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
		len = 0;
		goto err;
	}

	vrh->hena = ICE_DEFAULT_RSS_HENA;
err:
	/* send the response back to the VF */
	ret = ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_GET_RSS_HENA_CAPS, v_ret,
				    (u8 *)vrh, len);
	kfree(vrh);
	return ret;
}

/**
 * ice_vc_set_rss_hena - set RSS HENA bits for the VF
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 */
static int ice_vc_set_rss_hena(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_rss_hena *vrh = (struct virtchnl_rss_hena *)msg;
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
	if (status && !vrh->hena) {
		/* only report failure to clear the current RSS configuration if
		 * that was clearly the VF's intention (i.e. vrh->hena = 0)
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

	if (vrh->hena) {
		status = ice_add_avf_rss_cfg(&pf->hw, vsi, vrh->hena);
		v_ret = ice_err_to_virt_err(status);
	}

	/* send the response to the VF */
err:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_SET_RSS_HENA, v_ret,
				     NULL, 0);
}

/**
 * ice_vc_query_rxdid - query RXDID supported by DDP package
 * @vf: pointer to VF info
 *
 * Called from VF to query a bitmap of supported flexible
 * descriptor RXDIDs of a DDP package.
 */
static int ice_vc_query_rxdid(struct ice_vf *vf)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_supported_rxdids *rxdid = NULL;
	struct ice_hw *hw = &vf->pf->hw;
	struct ice_pf *pf = vf->pf;
	int len = 0;
	int ret, i;
	u32 regval;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	if (!(vf->driver_caps & VIRTCHNL_VF_OFFLOAD_RX_FLEX_DESC)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	len = sizeof(struct virtchnl_supported_rxdids);
	rxdid = kzalloc(len, GFP_KERNEL);
	if (!rxdid) {
		v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
		len = 0;
		goto err;
	}

	/* RXDIDs supported by DDP package can be read from the register
	 * to get the supported RXDID bitmap. But the legacy 32byte RXDID
	 * is not listed in DDP package, add it in the bitmap manually.
	 * Legacy 16byte descriptor is not supported.
	 */
	rxdid->supported_rxdids |= BIT(ICE_RXDID_LEGACY_1);

	for (i = ICE_RXDID_FLEX_NIC; i < ICE_FLEX_DESC_RXDID_MAX_NUM; i++) {
		regval = rd32(hw, GLFLXP_RXDID_FLAGS(i, 0));
		if ((regval >> GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_S)
			& GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_M)
			rxdid->supported_rxdids |= BIT(i);
	}

	pf->supported_rxdids = rxdid->supported_rxdids;

err:
	ret = ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_GET_SUPPORTED_RXDIDS,
				    v_ret, (u8 *)rxdid, len);
	kfree(rxdid);
	return ret;
}

/**
 * ice_vf_init_vlan_stripping - enable/disable VLAN stripping on initialization
 * @vf: VF to enable/disable VLAN stripping for on initialization
 *
 * Set the default for VLAN stripping based on whether a port VLAN is configured
 * and the current VLAN mode of the device.
 */
static int ice_vf_init_vlan_stripping(struct ice_vf *vf)
{
	struct ice_vsi *vsi = ice_get_vf_vsi(vf);

	vf->vlan_strip_ena = 0;

	if (!vsi)
		return -EINVAL;

	/* don't modify stripping if port VLAN is configured in SVM since the
	 * port VLAN is based on the inner/single VLAN in SVM
	 */
	if (ice_vf_is_port_vlan_ena(vf) && !ice_is_dvm_ena(&vsi->back->hw))
		return 0;

	if (ice_vf_vlan_offload_ena(vf->driver_caps)) {
		int err;

		err = vsi->inner_vlan_ops.ena_stripping(vsi, ETH_P_8021Q);
		if (!err)
			vf->vlan_strip_ena |= ICE_INNER_VLAN_STRIP_ENA;
		return err;
	}

	return vsi->inner_vlan_ops.dis_stripping(vsi);
}

static u16 ice_vc_get_max_vlan_fltrs(struct ice_vf *vf)
{
	if (vf->trusted)
		return VLAN_N_VID;
	else
		return ICE_MAX_VLAN_PER_VF;
}

/**
 * ice_vf_outer_vlan_not_allowed - check if outer VLAN can be used
 * @vf: VF that being checked for
 *
 * When the device is in double VLAN mode, check whether or not the outer VLAN
 * is allowed.
 */
static bool ice_vf_outer_vlan_not_allowed(struct ice_vf *vf)
{
	if (ice_vf_is_port_vlan_ena(vf))
		return true;

	return false;
}

/**
 * ice_vc_set_dvm_caps - set VLAN capabilities when the device is in DVM
 * @vf: VF that capabilities are being set for
 * @caps: VLAN capabilities to populate
 *
 * Determine VLAN capabilities support based on whether a port VLAN is
 * configured. If a port VLAN is configured then the VF should use the inner
 * filtering/offload capabilities since the port VLAN is using the outer VLAN
 * capabilies.
 */
static void
ice_vc_set_dvm_caps(struct ice_vf *vf, struct virtchnl_vlan_caps *caps)
{
	struct virtchnl_vlan_supported_caps *supported_caps;

	if (ice_vf_outer_vlan_not_allowed(vf)) {
		/* until support for inner VLAN filtering is added when a port
		 * VLAN is configured, only support software offloaded inner
		 * VLANs when a port VLAN is confgured in DVM
		 */
		supported_caps = &caps->filtering.filtering_support;
		supported_caps->inner = VIRTCHNL_VLAN_UNSUPPORTED;

		supported_caps = &caps->offloads.stripping_support;
		supported_caps->inner = VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;

		supported_caps = &caps->offloads.insertion_support;
		supported_caps->inner = VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;

		caps->offloads.ethertype_init = VIRTCHNL_VLAN_ETHERTYPE_8100;
		caps->offloads.ethertype_match =
			VIRTCHNL_ETHERTYPE_STRIPPING_MATCHES_INSERTION;
	} else {
		supported_caps = &caps->filtering.filtering_support;
		supported_caps->inner = VIRTCHNL_VLAN_UNSUPPORTED;
		supported_caps->outer = VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_ETHERTYPE_88A8 |
					VIRTCHNL_VLAN_ETHERTYPE_9100 |
					VIRTCHNL_VLAN_ETHERTYPE_AND;
		caps->filtering.ethertype_init = VIRTCHNL_VLAN_ETHERTYPE_8100 |
						 VIRTCHNL_VLAN_ETHERTYPE_88A8 |
						 VIRTCHNL_VLAN_ETHERTYPE_9100;

		supported_caps = &caps->offloads.stripping_support;
		supported_caps->inner = VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1;
		supported_caps->outer = VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_ETHERTYPE_88A8 |
					VIRTCHNL_VLAN_ETHERTYPE_9100 |
					VIRTCHNL_VLAN_ETHERTYPE_XOR |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG2_2;

		supported_caps = &caps->offloads.insertion_support;
		supported_caps->inner = VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1;
		supported_caps->outer = VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_ETHERTYPE_88A8 |
					VIRTCHNL_VLAN_ETHERTYPE_9100 |
					VIRTCHNL_VLAN_ETHERTYPE_XOR |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG2;

		caps->offloads.ethertype_init = VIRTCHNL_VLAN_ETHERTYPE_8100;

		caps->offloads.ethertype_match =
			VIRTCHNL_ETHERTYPE_STRIPPING_MATCHES_INSERTION;
	}

	caps->filtering.max_filters = ice_vc_get_max_vlan_fltrs(vf);
}

/**
 * ice_vc_set_svm_caps - set VLAN capabilities when the device is in SVM
 * @vf: VF that capabilities are being set for
 * @caps: VLAN capabilities to populate
 *
 * Determine VLAN capabilities support based on whether a port VLAN is
 * configured. If a port VLAN is configured then the VF does not have any VLAN
 * filtering or offload capabilities since the port VLAN is using the inner VLAN
 * capabilities in single VLAN mode (SVM). Otherwise allow the VF to use inner
 * VLAN fitlering and offload capabilities.
 */
static void
ice_vc_set_svm_caps(struct ice_vf *vf, struct virtchnl_vlan_caps *caps)
{
	struct virtchnl_vlan_supported_caps *supported_caps;

	if (ice_vf_is_port_vlan_ena(vf)) {
		supported_caps = &caps->filtering.filtering_support;
		supported_caps->inner = VIRTCHNL_VLAN_UNSUPPORTED;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;

		supported_caps = &caps->offloads.stripping_support;
		supported_caps->inner = VIRTCHNL_VLAN_UNSUPPORTED;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;

		supported_caps = &caps->offloads.insertion_support;
		supported_caps->inner = VIRTCHNL_VLAN_UNSUPPORTED;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;

		caps->offloads.ethertype_init = VIRTCHNL_VLAN_UNSUPPORTED;
		caps->offloads.ethertype_match = VIRTCHNL_VLAN_UNSUPPORTED;
		caps->filtering.max_filters = 0;
	} else {
		supported_caps = &caps->filtering.filtering_support;
		supported_caps->inner = VIRTCHNL_VLAN_ETHERTYPE_8100;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;
		caps->filtering.ethertype_init = VIRTCHNL_VLAN_ETHERTYPE_8100;

		supported_caps = &caps->offloads.stripping_support;
		supported_caps->inner = VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;

		supported_caps = &caps->offloads.insertion_support;
		supported_caps->inner = VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;

		caps->offloads.ethertype_init = VIRTCHNL_VLAN_ETHERTYPE_8100;
		caps->offloads.ethertype_match =
			VIRTCHNL_ETHERTYPE_STRIPPING_MATCHES_INSERTION;
		caps->filtering.max_filters = ice_vc_get_max_vlan_fltrs(vf);
	}
}

/**
 * ice_vc_get_offload_vlan_v2_caps - determine VF's VLAN capabilities
 * @vf: VF to determine VLAN capabilities for
 *
 * This will only be called if the VF and PF successfully negotiated
 * VIRTCHNL_VF_OFFLOAD_VLAN_V2.
 *
 * Set VLAN capabilities based on the current VLAN mode and whether a port VLAN
 * is configured or not.
 */
static int ice_vc_get_offload_vlan_v2_caps(struct ice_vf *vf)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vlan_caps *caps = NULL;
	int err, len = 0;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	caps = kzalloc(sizeof(*caps), GFP_KERNEL);
	if (!caps) {
		v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
		goto out;
	}
	len = sizeof(*caps);

	if (ice_is_dvm_ena(&vf->pf->hw))
		ice_vc_set_dvm_caps(vf, caps);
	else
		ice_vc_set_svm_caps(vf, caps);

	/* store negotiated caps to prevent invalid VF messages */
	memcpy(&vf->vlan_v2_caps, caps, sizeof(*caps));

out:
	err = ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_GET_OFFLOAD_VLAN_V2_CAPS,
				    v_ret, (u8 *)caps, len);
	kfree(caps);
	return err;
}

/**
 * ice_vc_validate_vlan_tpid - validate VLAN TPID
 * @filtering_caps: negotiated/supported VLAN filtering capabilities
 * @tpid: VLAN TPID used for validation
 *
 * Convert the VLAN TPID to a VIRTCHNL_VLAN_ETHERTYPE_* and then compare against
 * the negotiated/supported filtering caps to see if the VLAN TPID is valid.
 */
static bool ice_vc_validate_vlan_tpid(u16 filtering_caps, u16 tpid)
{
	enum virtchnl_vlan_support vlan_ethertype = VIRTCHNL_VLAN_UNSUPPORTED;

	switch (tpid) {
	case ETH_P_8021Q:
		vlan_ethertype = VIRTCHNL_VLAN_ETHERTYPE_8100;
		break;
	case ETH_P_8021AD:
		vlan_ethertype = VIRTCHNL_VLAN_ETHERTYPE_88A8;
		break;
	case ETH_P_QINQ1:
		vlan_ethertype = VIRTCHNL_VLAN_ETHERTYPE_9100;
		break;
	}

	if (!(filtering_caps & vlan_ethertype))
		return false;

	return true;
}

/**
 * ice_vc_is_valid_vlan - validate the virtchnl_vlan
 * @vc_vlan: virtchnl_vlan to validate
 *
 * If the VLAN TCI and VLAN TPID are 0, then this filter is invalid, so return
 * false. Otherwise return true.
 */
static bool ice_vc_is_valid_vlan(struct virtchnl_vlan *vc_vlan)
{
	if (!vc_vlan->tci || !vc_vlan->tpid)
		return false;

	return true;
}

/**
 * ice_vc_validate_vlan_filter_list - validate the filter list from the VF
 * @vfc: negotiated/supported VLAN filtering capabilities
 * @vfl: VLAN filter list from VF to validate
 *
 * Validate all of the filters in the VLAN filter list from the VF. If any of
 * the checks fail then return false. Otherwise return true.
 */
static bool
ice_vc_validate_vlan_filter_list(struct virtchnl_vlan_filtering_caps *vfc,
				 struct virtchnl_vlan_filter_list_v2 *vfl)
{
	u16 i;

	if (!vfl->num_elements)
		return false;

	for (i = 0; i < vfl->num_elements; i++) {
		struct virtchnl_vlan_supported_caps *filtering_support =
			&vfc->filtering_support;
		struct virtchnl_vlan_filter *vlan_fltr = &vfl->filters[i];
		struct virtchnl_vlan *outer = &vlan_fltr->outer;
		struct virtchnl_vlan *inner = &vlan_fltr->inner;

		if ((ice_vc_is_valid_vlan(outer) &&
		     filtering_support->outer == VIRTCHNL_VLAN_UNSUPPORTED) ||
		    (ice_vc_is_valid_vlan(inner) &&
		     filtering_support->inner == VIRTCHNL_VLAN_UNSUPPORTED))
			return false;

		if ((outer->tci_mask &&
		     !(filtering_support->outer & VIRTCHNL_VLAN_FILTER_MASK)) ||
		    (inner->tci_mask &&
		     !(filtering_support->inner & VIRTCHNL_VLAN_FILTER_MASK)))
			return false;

		if (((outer->tci & VLAN_PRIO_MASK) &&
		     !(filtering_support->outer & VIRTCHNL_VLAN_PRIO)) ||
		    ((inner->tci & VLAN_PRIO_MASK) &&
		     !(filtering_support->inner & VIRTCHNL_VLAN_PRIO)))
			return false;

		if ((ice_vc_is_valid_vlan(outer) &&
		     !ice_vc_validate_vlan_tpid(filtering_support->outer,
						outer->tpid)) ||
		    (ice_vc_is_valid_vlan(inner) &&
		     !ice_vc_validate_vlan_tpid(filtering_support->inner,
						inner->tpid)))
			return false;
	}

	return true;
}

/**
 * ice_vc_to_vlan - transform from struct virtchnl_vlan to struct ice_vlan
 * @vc_vlan: struct virtchnl_vlan to transform
 */
static struct ice_vlan ice_vc_to_vlan(struct virtchnl_vlan *vc_vlan)
{
	struct ice_vlan vlan = { 0 };

	vlan.prio = FIELD_GET(VLAN_PRIO_MASK, vc_vlan->tci);
	vlan.vid = vc_vlan->tci & VLAN_VID_MASK;
	vlan.tpid = vc_vlan->tpid;

	return vlan;
}

/**
 * ice_vc_vlan_action - action to perform on the virthcnl_vlan
 * @vsi: VF's VSI used to perform the action
 * @vlan_action: function to perform the action with (i.e. add/del)
 * @vlan: VLAN filter to perform the action with
 */
static int
ice_vc_vlan_action(struct ice_vsi *vsi,
		   int (*vlan_action)(struct ice_vsi *, struct ice_vlan *),
		   struct ice_vlan *vlan)
{
	int err;

	err = vlan_action(vsi, vlan);
	if (err)
		return err;

	return 0;
}

/**
 * ice_vc_del_vlans - delete VLAN(s) from the virtchnl filter list
 * @vf: VF used to delete the VLAN(s)
 * @vsi: VF's VSI used to delete the VLAN(s)
 * @vfl: virthchnl filter list used to delete the filters
 */
static int
ice_vc_del_vlans(struct ice_vf *vf, struct ice_vsi *vsi,
		 struct virtchnl_vlan_filter_list_v2 *vfl)
{
	bool vlan_promisc = ice_is_vlan_promisc_allowed(vf);
	int err;
	u16 i;

	for (i = 0; i < vfl->num_elements; i++) {
		struct virtchnl_vlan_filter *vlan_fltr = &vfl->filters[i];
		struct virtchnl_vlan *vc_vlan;

		vc_vlan = &vlan_fltr->outer;
		if (ice_vc_is_valid_vlan(vc_vlan)) {
			struct ice_vlan vlan = ice_vc_to_vlan(vc_vlan);

			err = ice_vc_vlan_action(vsi,
						 vsi->outer_vlan_ops.del_vlan,
						 &vlan);
			if (err)
				return err;

			if (vlan_promisc)
				ice_vf_dis_vlan_promisc(vsi, &vlan);

			/* Disable VLAN filtering when only VLAN 0 is left */
			if (!ice_vsi_has_non_zero_vlans(vsi) && ice_is_dvm_ena(&vsi->back->hw)) {
				err = vsi->outer_vlan_ops.dis_tx_filtering(vsi);
				if (err)
					return err;
			}
		}

		vc_vlan = &vlan_fltr->inner;
		if (ice_vc_is_valid_vlan(vc_vlan)) {
			struct ice_vlan vlan = ice_vc_to_vlan(vc_vlan);

			err = ice_vc_vlan_action(vsi,
						 vsi->inner_vlan_ops.del_vlan,
						 &vlan);
			if (err)
				return err;

			/* no support for VLAN promiscuous on inner VLAN unless
			 * we are in Single VLAN Mode (SVM)
			 */
			if (!ice_is_dvm_ena(&vsi->back->hw)) {
				if (vlan_promisc)
					ice_vf_dis_vlan_promisc(vsi, &vlan);

				/* Disable VLAN filtering when only VLAN 0 is left */
				if (!ice_vsi_has_non_zero_vlans(vsi)) {
					err = vsi->inner_vlan_ops.dis_tx_filtering(vsi);
					if (err)
						return err;
				}
			}
		}
	}

	return 0;
}

/**
 * ice_vc_remove_vlan_v2_msg - virtchnl handler for VIRTCHNL_OP_DEL_VLAN_V2
 * @vf: VF the message was received from
 * @msg: message received from the VF
 */
static int ice_vc_remove_vlan_v2_msg(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_vlan_filter_list_v2 *vfl =
		(struct virtchnl_vlan_filter_list_v2 *)msg;
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct ice_vsi *vsi;

	if (!ice_vc_validate_vlan_filter_list(&vf->vlan_v2_caps.filtering,
					      vfl)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vfl->vport_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (ice_vc_del_vlans(vf, vsi, vfl))
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;

out:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DEL_VLAN_V2, v_ret, NULL,
				     0);
}

/**
 * ice_vc_add_vlans - add VLAN(s) from the virtchnl filter list
 * @vf: VF used to add the VLAN(s)
 * @vsi: VF's VSI used to add the VLAN(s)
 * @vfl: virthchnl filter list used to add the filters
 */
static int
ice_vc_add_vlans(struct ice_vf *vf, struct ice_vsi *vsi,
		 struct virtchnl_vlan_filter_list_v2 *vfl)
{
	bool vlan_promisc = ice_is_vlan_promisc_allowed(vf);
	int err;
	u16 i;

	for (i = 0; i < vfl->num_elements; i++) {
		struct virtchnl_vlan_filter *vlan_fltr = &vfl->filters[i];
		struct virtchnl_vlan *vc_vlan;

		vc_vlan = &vlan_fltr->outer;
		if (ice_vc_is_valid_vlan(vc_vlan)) {
			struct ice_vlan vlan = ice_vc_to_vlan(vc_vlan);

			err = ice_vc_vlan_action(vsi,
						 vsi->outer_vlan_ops.add_vlan,
						 &vlan);
			if (err)
				return err;

			if (vlan_promisc) {
				err = ice_vf_ena_vlan_promisc(vsi, &vlan);
				if (err)
					return err;
			}

			/* Enable VLAN filtering on first non-zero VLAN */
			if (vf->spoofchk && vlan.vid && ice_is_dvm_ena(&vsi->back->hw)) {
				err = vsi->outer_vlan_ops.ena_tx_filtering(vsi);
				if (err)
					return err;
			}
		}

		vc_vlan = &vlan_fltr->inner;
		if (ice_vc_is_valid_vlan(vc_vlan)) {
			struct ice_vlan vlan = ice_vc_to_vlan(vc_vlan);

			err = ice_vc_vlan_action(vsi,
						 vsi->inner_vlan_ops.add_vlan,
						 &vlan);
			if (err)
				return err;

			/* no support for VLAN promiscuous on inner VLAN unless
			 * we are in Single VLAN Mode (SVM)
			 */
			if (!ice_is_dvm_ena(&vsi->back->hw)) {
				if (vlan_promisc) {
					err = ice_vf_ena_vlan_promisc(vsi, &vlan);
					if (err)
						return err;
				}

				/* Enable VLAN filtering on first non-zero VLAN */
				if (vf->spoofchk && vlan.vid) {
					err = vsi->inner_vlan_ops.ena_tx_filtering(vsi);
					if (err)
						return err;
				}
			}
		}
	}

	return 0;
}

/**
 * ice_vc_validate_add_vlan_filter_list - validate add filter list from the VF
 * @vsi: VF VSI used to get number of existing VLAN filters
 * @vfc: negotiated/supported VLAN filtering capabilities
 * @vfl: VLAN filter list from VF to validate
 *
 * Validate all of the filters in the VLAN filter list from the VF during the
 * VIRTCHNL_OP_ADD_VLAN_V2 opcode. If any of the checks fail then return false.
 * Otherwise return true.
 */
static bool
ice_vc_validate_add_vlan_filter_list(struct ice_vsi *vsi,
				     struct virtchnl_vlan_filtering_caps *vfc,
				     struct virtchnl_vlan_filter_list_v2 *vfl)
{
	u16 num_requested_filters = ice_vsi_num_non_zero_vlans(vsi) +
		vfl->num_elements;

	if (num_requested_filters > vfc->max_filters)
		return false;

	return ice_vc_validate_vlan_filter_list(vfc, vfl);
}

/**
 * ice_vc_add_vlan_v2_msg - virtchnl handler for VIRTCHNL_OP_ADD_VLAN_V2
 * @vf: VF the message was received from
 * @msg: message received from the VF
 */
static int ice_vc_add_vlan_v2_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vlan_filter_list_v2 *vfl =
		(struct virtchnl_vlan_filter_list_v2 *)msg;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vfl->vport_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (!ice_vc_validate_add_vlan_filter_list(vsi,
						  &vf->vlan_v2_caps.filtering,
						  vfl)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (ice_vc_add_vlans(vf, vsi, vfl))
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;

out:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ADD_VLAN_V2, v_ret, NULL,
				     0);
}

/**
 * ice_vc_valid_vlan_setting - validate VLAN setting
 * @negotiated_settings: negotiated VLAN settings during VF init
 * @ethertype_setting: ethertype(s) requested for the VLAN setting
 */
static bool
ice_vc_valid_vlan_setting(u32 negotiated_settings, u32 ethertype_setting)
{
	if (ethertype_setting && !(negotiated_settings & ethertype_setting))
		return false;

	/* only allow a single VIRTCHNL_VLAN_ETHERTYPE if
	 * VIRTHCNL_VLAN_ETHERTYPE_AND is not negotiated/supported
	 */
	if (!(negotiated_settings & VIRTCHNL_VLAN_ETHERTYPE_AND) &&
	    hweight32(ethertype_setting) > 1)
		return false;

	/* ability to modify the VLAN setting was not negotiated */
	if (!(negotiated_settings & VIRTCHNL_VLAN_TOGGLE))
		return false;

	return true;
}

/**
 * ice_vc_valid_vlan_setting_msg - validate the VLAN setting message
 * @caps: negotiated VLAN settings during VF init
 * @msg: message to validate
 *
 * Used to validate any VLAN virtchnl message sent as a
 * virtchnl_vlan_setting structure. Validates the message against the
 * negotiated/supported caps during VF driver init.
 */
static bool
ice_vc_valid_vlan_setting_msg(struct virtchnl_vlan_supported_caps *caps,
			      struct virtchnl_vlan_setting *msg)
{
	if ((!msg->outer_ethertype_setting &&
	     !msg->inner_ethertype_setting) ||
	    (!caps->outer && !caps->inner))
		return false;

	if (msg->outer_ethertype_setting &&
	    !ice_vc_valid_vlan_setting(caps->outer,
				       msg->outer_ethertype_setting))
		return false;

	if (msg->inner_ethertype_setting &&
	    !ice_vc_valid_vlan_setting(caps->inner,
				       msg->inner_ethertype_setting))
		return false;

	return true;
}

/**
 * ice_vc_get_tpid - transform from VIRTCHNL_VLAN_ETHERTYPE_* to VLAN TPID
 * @ethertype_setting: VIRTCHNL_VLAN_ETHERTYPE_* used to get VLAN TPID
 * @tpid: VLAN TPID to populate
 */
static int ice_vc_get_tpid(u32 ethertype_setting, u16 *tpid)
{
	switch (ethertype_setting) {
	case VIRTCHNL_VLAN_ETHERTYPE_8100:
		*tpid = ETH_P_8021Q;
		break;
	case VIRTCHNL_VLAN_ETHERTYPE_88A8:
		*tpid = ETH_P_8021AD;
		break;
	case VIRTCHNL_VLAN_ETHERTYPE_9100:
		*tpid = ETH_P_QINQ1;
		break;
	default:
		*tpid = 0;
		return -EINVAL;
	}

	return 0;
}

/**
 * ice_vc_ena_vlan_offload - enable VLAN offload based on the ethertype_setting
 * @vsi: VF's VSI used to enable the VLAN offload
 * @ena_offload: function used to enable the VLAN offload
 * @ethertype_setting: VIRTCHNL_VLAN_ETHERTYPE_* to enable offloads for
 */
static int
ice_vc_ena_vlan_offload(struct ice_vsi *vsi,
			int (*ena_offload)(struct ice_vsi *vsi, u16 tpid),
			u32 ethertype_setting)
{
	u16 tpid;
	int err;

	err = ice_vc_get_tpid(ethertype_setting, &tpid);
	if (err)
		return err;

	err = ena_offload(vsi, tpid);
	if (err)
		return err;

	return 0;
}

#define ICE_L2TSEL_QRX_CONTEXT_REG_IDX	3
#define ICE_L2TSEL_BIT_OFFSET		23
enum ice_l2tsel {
	ICE_L2TSEL_EXTRACT_FIRST_TAG_L2TAG2_2ND,
	ICE_L2TSEL_EXTRACT_FIRST_TAG_L2TAG1,
};

/**
 * ice_vsi_update_l2tsel - update l2tsel field for all Rx rings on this VSI
 * @vsi: VSI used to update l2tsel on
 * @l2tsel: l2tsel setting requested
 *
 * Use the l2tsel setting to update all of the Rx queue context bits for l2tsel.
 * This will modify which descriptor field the first offloaded VLAN will be
 * stripped into.
 */
static void ice_vsi_update_l2tsel(struct ice_vsi *vsi, enum ice_l2tsel l2tsel)
{
	struct ice_hw *hw = &vsi->back->hw;
	u32 l2tsel_bit;
	int i;

	if (l2tsel == ICE_L2TSEL_EXTRACT_FIRST_TAG_L2TAG2_2ND)
		l2tsel_bit = 0;
	else
		l2tsel_bit = BIT(ICE_L2TSEL_BIT_OFFSET);

	for (i = 0; i < vsi->alloc_rxq; i++) {
		u16 pfq = vsi->rxq_map[i];
		u32 qrx_context_offset;
		u32 regval;

		qrx_context_offset =
			QRX_CONTEXT(ICE_L2TSEL_QRX_CONTEXT_REG_IDX, pfq);

		regval = rd32(hw, qrx_context_offset);
		regval &= ~BIT(ICE_L2TSEL_BIT_OFFSET);
		regval |= l2tsel_bit;
		wr32(hw, qrx_context_offset, regval);
	}
}

/**
 * ice_vc_ena_vlan_stripping_v2_msg
 * @vf: VF the message was received from
 * @msg: message received from the VF
 *
 * virthcnl handler for VIRTCHNL_OP_ENABLE_VLAN_STRIPPING_V2
 */
static int ice_vc_ena_vlan_stripping_v2_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vlan_supported_caps *stripping_support;
	struct virtchnl_vlan_setting *strip_msg =
		(struct virtchnl_vlan_setting *)msg;
	u32 ethertype_setting;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (!ice_vc_isvalid_vsi_id(vf, strip_msg->vport_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	stripping_support = &vf->vlan_v2_caps.offloads.stripping_support;
	if (!ice_vc_valid_vlan_setting_msg(stripping_support, strip_msg)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (ice_vsi_is_rxq_crc_strip_dis(vsi)) {
		v_ret = VIRTCHNL_STATUS_ERR_NOT_SUPPORTED;
		goto out;
	}

	ethertype_setting = strip_msg->outer_ethertype_setting;
	if (ethertype_setting) {
		if (ice_vc_ena_vlan_offload(vsi,
					    vsi->outer_vlan_ops.ena_stripping,
					    ethertype_setting)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto out;
		} else {
			enum ice_l2tsel l2tsel =
				ICE_L2TSEL_EXTRACT_FIRST_TAG_L2TAG2_2ND;

			/* PF tells the VF that the outer VLAN tag is always
			 * extracted to VIRTCHNL_VLAN_TAG_LOCATION_L2TAG2_2 and
			 * inner is always extracted to
			 * VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1. This is needed to
			 * support outer stripping so the first tag always ends
			 * up in L2TAG2_2ND and the second/inner tag, if
			 * enabled, is extracted in L2TAG1.
			 */
			ice_vsi_update_l2tsel(vsi, l2tsel);

			vf->vlan_strip_ena |= ICE_OUTER_VLAN_STRIP_ENA;
		}
	}

	ethertype_setting = strip_msg->inner_ethertype_setting;
	if (ethertype_setting &&
	    ice_vc_ena_vlan_offload(vsi, vsi->inner_vlan_ops.ena_stripping,
				    ethertype_setting)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (ethertype_setting)
		vf->vlan_strip_ena |= ICE_INNER_VLAN_STRIP_ENA;

out:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ENABLE_VLAN_STRIPPING_V2,
				     v_ret, NULL, 0);
}

/**
 * ice_vc_dis_vlan_stripping_v2_msg
 * @vf: VF the message was received from
 * @msg: message received from the VF
 *
 * virthcnl handler for VIRTCHNL_OP_DISABLE_VLAN_STRIPPING_V2
 */
static int ice_vc_dis_vlan_stripping_v2_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vlan_supported_caps *stripping_support;
	struct virtchnl_vlan_setting *strip_msg =
		(struct virtchnl_vlan_setting *)msg;
	u32 ethertype_setting;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (!ice_vc_isvalid_vsi_id(vf, strip_msg->vport_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	stripping_support = &vf->vlan_v2_caps.offloads.stripping_support;
	if (!ice_vc_valid_vlan_setting_msg(stripping_support, strip_msg)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	ethertype_setting = strip_msg->outer_ethertype_setting;
	if (ethertype_setting) {
		if (vsi->outer_vlan_ops.dis_stripping(vsi)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto out;
		} else {
			enum ice_l2tsel l2tsel =
				ICE_L2TSEL_EXTRACT_FIRST_TAG_L2TAG1;

			/* PF tells the VF that the outer VLAN tag is always
			 * extracted to VIRTCHNL_VLAN_TAG_LOCATION_L2TAG2_2 and
			 * inner is always extracted to
			 * VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1. This is needed to
			 * support inner stripping while outer stripping is
			 * disabled so that the first and only tag is extracted
			 * in L2TAG1.
			 */
			ice_vsi_update_l2tsel(vsi, l2tsel);

			vf->vlan_strip_ena &= ~ICE_OUTER_VLAN_STRIP_ENA;
		}
	}

	ethertype_setting = strip_msg->inner_ethertype_setting;
	if (ethertype_setting && vsi->inner_vlan_ops.dis_stripping(vsi)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (ethertype_setting)
		vf->vlan_strip_ena &= ~ICE_INNER_VLAN_STRIP_ENA;

out:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DISABLE_VLAN_STRIPPING_V2,
				     v_ret, NULL, 0);
}

/**
 * ice_vc_ena_vlan_insertion_v2_msg
 * @vf: VF the message was received from
 * @msg: message received from the VF
 *
 * virthcnl handler for VIRTCHNL_OP_ENABLE_VLAN_INSERTION_V2
 */
static int ice_vc_ena_vlan_insertion_v2_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vlan_supported_caps *insertion_support;
	struct virtchnl_vlan_setting *insertion_msg =
		(struct virtchnl_vlan_setting *)msg;
	u32 ethertype_setting;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (!ice_vc_isvalid_vsi_id(vf, insertion_msg->vport_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	insertion_support = &vf->vlan_v2_caps.offloads.insertion_support;
	if (!ice_vc_valid_vlan_setting_msg(insertion_support, insertion_msg)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	ethertype_setting = insertion_msg->outer_ethertype_setting;
	if (ethertype_setting &&
	    ice_vc_ena_vlan_offload(vsi, vsi->outer_vlan_ops.ena_insertion,
				    ethertype_setting)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	ethertype_setting = insertion_msg->inner_ethertype_setting;
	if (ethertype_setting &&
	    ice_vc_ena_vlan_offload(vsi, vsi->inner_vlan_ops.ena_insertion,
				    ethertype_setting)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

out:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ENABLE_VLAN_INSERTION_V2,
				     v_ret, NULL, 0);
}

/**
 * ice_vc_dis_vlan_insertion_v2_msg
 * @vf: VF the message was received from
 * @msg: message received from the VF
 *
 * virthcnl handler for VIRTCHNL_OP_DISABLE_VLAN_INSERTION_V2
 */
static int ice_vc_dis_vlan_insertion_v2_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vlan_supported_caps *insertion_support;
	struct virtchnl_vlan_setting *insertion_msg =
		(struct virtchnl_vlan_setting *)msg;
	u32 ethertype_setting;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (!ice_vc_isvalid_vsi_id(vf, insertion_msg->vport_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	insertion_support = &vf->vlan_v2_caps.offloads.insertion_support;
	if (!ice_vc_valid_vlan_setting_msg(insertion_support, insertion_msg)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	ethertype_setting = insertion_msg->outer_ethertype_setting;
	if (ethertype_setting && vsi->outer_vlan_ops.dis_insertion(vsi)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	ethertype_setting = insertion_msg->inner_ethertype_setting;
	if (ethertype_setting && vsi->inner_vlan_ops.dis_insertion(vsi)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

out:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DISABLE_VLAN_INSERTION_V2,
				     v_ret, NULL, 0);
}

static const struct ice_virtchnl_ops ice_virtchnl_dflt_ops = {
	.get_ver_msg = ice_vc_get_ver_msg,
	.get_vf_res_msg = ice_vc_get_vf_res_msg,
	.reset_vf = ice_vc_reset_vf_msg,
	.add_mac_addr_msg = ice_vc_add_mac_addr_msg,
	.del_mac_addr_msg = ice_vc_del_mac_addr_msg,
	.cfg_qs_msg = ice_vc_cfg_qs_msg,
	.ena_qs_msg = ice_vc_ena_qs_msg,
	.dis_qs_msg = ice_vc_dis_qs_msg,
	.request_qs_msg = ice_vc_request_qs_msg,
	.cfg_irq_map_msg = ice_vc_cfg_irq_map_msg,
	.config_rss_key = ice_vc_config_rss_key,
	.config_rss_lut = ice_vc_config_rss_lut,
	.config_rss_hfunc = ice_vc_config_rss_hfunc,
	.get_stats_msg = ice_vc_get_stats_msg,
	.cfg_promiscuous_mode_msg = ice_vc_cfg_promiscuous_mode_msg,
	.add_vlan_msg = ice_vc_add_vlan_msg,
	.remove_vlan_msg = ice_vc_remove_vlan_msg,
	.query_rxdid = ice_vc_query_rxdid,
	.get_rss_hena = ice_vc_get_rss_hena,
	.set_rss_hena_msg = ice_vc_set_rss_hena,
	.ena_vlan_stripping = ice_vc_ena_vlan_stripping,
	.dis_vlan_stripping = ice_vc_dis_vlan_stripping,
	.handle_rss_cfg_msg = ice_vc_handle_rss_cfg,
	.add_fdir_fltr_msg = ice_vc_add_fdir_fltr,
	.del_fdir_fltr_msg = ice_vc_del_fdir_fltr,
	.get_offload_vlan_v2_caps = ice_vc_get_offload_vlan_v2_caps,
	.add_vlan_v2_msg = ice_vc_add_vlan_v2_msg,
	.remove_vlan_v2_msg = ice_vc_remove_vlan_v2_msg,
	.ena_vlan_stripping_v2_msg = ice_vc_ena_vlan_stripping_v2_msg,
	.dis_vlan_stripping_v2_msg = ice_vc_dis_vlan_stripping_v2_msg,
	.ena_vlan_insertion_v2_msg = ice_vc_ena_vlan_insertion_v2_msg,
	.dis_vlan_insertion_v2_msg = ice_vc_dis_vlan_insertion_v2_msg,
};

/**
 * ice_virtchnl_set_dflt_ops - Switch to default virtchnl ops
 * @vf: the VF to switch ops
 */
void ice_virtchnl_set_dflt_ops(struct ice_vf *vf)
{
	vf->virtchnl_ops = &ice_virtchnl_dflt_ops;
}

/**
 * ice_vc_repr_add_mac
 * @vf: pointer to VF
 * @msg: virtchannel message
 *
 * When port representors are created, we do not add MAC rule
 * to firmware, we store it so that PF could report same
 * MAC as VF.
 */
static int ice_vc_repr_add_mac(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_ether_addr_list *al =
	    (struct virtchnl_ether_addr_list *)msg;
	struct ice_vsi *vsi;
	struct ice_pf *pf;
	int i;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states) ||
	    !ice_vc_isvalid_vsi_id(vf, al->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto handle_mac_exit;
	}

	pf = vf->pf;

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto handle_mac_exit;
	}

	for (i = 0; i < al->num_elements; i++) {
		u8 *mac_addr = al->list[i].addr;

		if (!is_unicast_ether_addr(mac_addr) ||
		    ether_addr_equal(mac_addr, vf->hw_lan_addr))
			continue;

		if (vf->pf_set_mac) {
			dev_err(ice_pf_to_dev(pf), "VF attempting to override administratively set MAC address\n");
			v_ret = VIRTCHNL_STATUS_ERR_NOT_SUPPORTED;
			goto handle_mac_exit;
		}

		ice_vfhw_mac_add(vf, &al->list[i]);
		vf->num_mac++;
		break;
	}

handle_mac_exit:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ADD_ETH_ADDR,
				     v_ret, NULL, 0);
}

/**
 * ice_vc_repr_del_mac - response with success for deleting MAC
 * @vf: pointer to VF
 * @msg: virtchannel message
 *
 * Respond with success to not break normal VF flow.
 * For legacy VF driver try to update cached MAC address.
 */
static int
ice_vc_repr_del_mac(struct ice_vf __always_unused *vf, u8 __always_unused *msg)
{
	struct virtchnl_ether_addr_list *al =
		(struct virtchnl_ether_addr_list *)msg;

	ice_update_legacy_cached_mac(vf, &al->list[0]);

	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DEL_ETH_ADDR,
				     VIRTCHNL_STATUS_SUCCESS, NULL, 0);
}

static int
ice_vc_repr_cfg_promiscuous_mode(struct ice_vf *vf, u8 __always_unused *msg)
{
	dev_dbg(ice_pf_to_dev(vf->pf),
		"Can't config promiscuous mode in switchdev mode for VF %d\n",
		vf->vf_id);
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE,
				     VIRTCHNL_STATUS_ERR_NOT_SUPPORTED,
				     NULL, 0);
}

static const struct ice_virtchnl_ops ice_virtchnl_repr_ops = {
	.get_ver_msg = ice_vc_get_ver_msg,
	.get_vf_res_msg = ice_vc_get_vf_res_msg,
	.reset_vf = ice_vc_reset_vf_msg,
	.add_mac_addr_msg = ice_vc_repr_add_mac,
	.del_mac_addr_msg = ice_vc_repr_del_mac,
	.cfg_qs_msg = ice_vc_cfg_qs_msg,
	.ena_qs_msg = ice_vc_ena_qs_msg,
	.dis_qs_msg = ice_vc_dis_qs_msg,
	.request_qs_msg = ice_vc_request_qs_msg,
	.cfg_irq_map_msg = ice_vc_cfg_irq_map_msg,
	.config_rss_key = ice_vc_config_rss_key,
	.config_rss_lut = ice_vc_config_rss_lut,
	.config_rss_hfunc = ice_vc_config_rss_hfunc,
	.get_stats_msg = ice_vc_get_stats_msg,
	.cfg_promiscuous_mode_msg = ice_vc_repr_cfg_promiscuous_mode,
	.add_vlan_msg = ice_vc_add_vlan_msg,
	.remove_vlan_msg = ice_vc_remove_vlan_msg,
	.query_rxdid = ice_vc_query_rxdid,
	.get_rss_hena = ice_vc_get_rss_hena,
	.set_rss_hena_msg = ice_vc_set_rss_hena,
	.ena_vlan_stripping = ice_vc_ena_vlan_stripping,
	.dis_vlan_stripping = ice_vc_dis_vlan_stripping,
	.handle_rss_cfg_msg = ice_vc_handle_rss_cfg,
	.add_fdir_fltr_msg = ice_vc_add_fdir_fltr,
	.del_fdir_fltr_msg = ice_vc_del_fdir_fltr,
	.get_offload_vlan_v2_caps = ice_vc_get_offload_vlan_v2_caps,
	.add_vlan_v2_msg = ice_vc_add_vlan_v2_msg,
	.remove_vlan_v2_msg = ice_vc_remove_vlan_v2_msg,
	.ena_vlan_stripping_v2_msg = ice_vc_ena_vlan_stripping_v2_msg,
	.dis_vlan_stripping_v2_msg = ice_vc_dis_vlan_stripping_v2_msg,
	.ena_vlan_insertion_v2_msg = ice_vc_ena_vlan_insertion_v2_msg,
	.dis_vlan_insertion_v2_msg = ice_vc_dis_vlan_insertion_v2_msg,
};

/**
 * ice_virtchnl_set_repr_ops - Switch to representor virtchnl ops
 * @vf: the VF to switch ops
 */
void ice_virtchnl_set_repr_ops(struct ice_vf *vf)
{
	vf->virtchnl_ops = &ice_virtchnl_repr_ops;
}

/**
 * ice_is_malicious_vf - check if this vf might be overflowing mailbox
 * @vf: the VF to check
 * @mbxdata: data about the state of the mailbox
 *
 * Detect if a given VF might be malicious and attempting to overflow the PF
 * mailbox. If so, log a warning message and ignore this event.
 */
static bool
ice_is_malicious_vf(struct ice_vf *vf, struct ice_mbx_data *mbxdata)
{
	bool report_malvf = false;
	struct device *dev;
	struct ice_pf *pf;
	int status;

	pf = vf->pf;
	dev = ice_pf_to_dev(pf);

	if (test_bit(ICE_VF_STATE_DIS, vf->vf_states))
		return vf->mbx_info.malicious;

	/* check to see if we have a newly malicious VF */
	status = ice_mbx_vf_state_handler(&pf->hw, mbxdata, &vf->mbx_info,
					  &report_malvf);
	if (status)
		dev_warn_ratelimited(dev, "Unable to check status of mailbox overflow for VF %u MAC %pM, status %d\n",
				     vf->vf_id, vf->dev_lan_addr, status);

	if (report_malvf) {
		struct ice_vsi *pf_vsi = ice_get_main_vsi(pf);
		u8 zero_addr[ETH_ALEN] = {};

		dev_warn(dev, "VF MAC %pM on PF MAC %pM is generating asynchronous messages and may be overflowing the PF message queue. Please see the Adapter User Guide for more information\n",
			 vf->dev_lan_addr,
			 pf_vsi ? pf_vsi->netdev->dev_addr : zero_addr);
	}

	return vf->mbx_info.malicious;
}

/**
 * ice_vc_process_vf_msg - Process request from VF
 * @pf: pointer to the PF structure
 * @event: pointer to the AQ event
 * @mbxdata: information used to detect VF attempting mailbox overflow
 *
 * called from the common asq/arq handler to
 * process request from VF
 */
void ice_vc_process_vf_msg(struct ice_pf *pf, struct ice_rq_event_info *event,
			   struct ice_mbx_data *mbxdata)
{
	u32 v_opcode = le32_to_cpu(event->desc.cookie_high);
	s16 vf_id = le16_to_cpu(event->desc.retval);
	const struct ice_virtchnl_ops *ops;
	u16 msglen = event->msg_len;
	u8 *msg = event->msg_buf;
	struct ice_vf *vf = NULL;
	struct device *dev;
	int err = 0;

	dev = ice_pf_to_dev(pf);

	vf = ice_get_vf_by_id(pf, vf_id);
	if (!vf) {
		dev_err(dev, "Unable to locate VF for message from VF ID %d, opcode %d, len %d\n",
			vf_id, v_opcode, msglen);
		return;
	}

	mutex_lock(&vf->cfg_lock);

	/* Check if the VF is trying to overflow the mailbox */
	if (ice_is_malicious_vf(vf, mbxdata))
		goto finish;

	/* Check if VF is disabled. */
	if (test_bit(ICE_VF_STATE_DIS, vf->vf_states)) {
		err = -EPERM;
		goto error_handler;
	}

	ops = vf->virtchnl_ops;

	/* Perform basic checks on the msg */
	err = virtchnl_vc_validate_vf_msg(&vf->vf_ver, v_opcode, msg, msglen);
	if (err) {
		if (err == VIRTCHNL_STATUS_ERR_PARAM)
			err = -EPERM;
		else
			err = -EINVAL;
	}

error_handler:
	if (err) {
		ice_vc_send_msg_to_vf(vf, v_opcode, VIRTCHNL_STATUS_ERR_PARAM,
				      NULL, 0);
		dev_err(dev, "Invalid message from VF %d, opcode %d, len %d, error %d\n",
			vf_id, v_opcode, msglen, err);
		goto finish;
	}

	if (!ice_vc_is_opcode_allowed(vf, v_opcode)) {
		ice_vc_send_msg_to_vf(vf, v_opcode,
				      VIRTCHNL_STATUS_ERR_NOT_SUPPORTED, NULL,
				      0);
		goto finish;
	}

	switch (v_opcode) {
	case VIRTCHNL_OP_VERSION:
		err = ops->get_ver_msg(vf, msg);
		break;
	case VIRTCHNL_OP_GET_VF_RESOURCES:
		err = ops->get_vf_res_msg(vf, msg);
		if (ice_vf_init_vlan_stripping(vf))
			dev_dbg(dev, "Failed to initialize VLAN stripping for VF %d\n",
				vf->vf_id);
		ice_vc_notify_vf_link_state(vf);
		break;
	case VIRTCHNL_OP_RESET_VF:
		ops->reset_vf(vf);
		break;
	case VIRTCHNL_OP_ADD_ETH_ADDR:
		err = ops->add_mac_addr_msg(vf, msg);
		break;
	case VIRTCHNL_OP_DEL_ETH_ADDR:
		err = ops->del_mac_addr_msg(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_VSI_QUEUES:
		err = ops->cfg_qs_msg(vf, msg);
		break;
	case VIRTCHNL_OP_ENABLE_QUEUES:
		err = ops->ena_qs_msg(vf, msg);
		ice_vc_notify_vf_link_state(vf);
		break;
	case VIRTCHNL_OP_DISABLE_QUEUES:
		err = ops->dis_qs_msg(vf, msg);
		break;
	case VIRTCHNL_OP_REQUEST_QUEUES:
		err = ops->request_qs_msg(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_IRQ_MAP:
		err = ops->cfg_irq_map_msg(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_RSS_KEY:
		err = ops->config_rss_key(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_RSS_LUT:
		err = ops->config_rss_lut(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_RSS_HFUNC:
		err = ops->config_rss_hfunc(vf, msg);
		break;
	case VIRTCHNL_OP_GET_STATS:
		err = ops->get_stats_msg(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE:
		err = ops->cfg_promiscuous_mode_msg(vf, msg);
		break;
	case VIRTCHNL_OP_ADD_VLAN:
		err = ops->add_vlan_msg(vf, msg);
		break;
	case VIRTCHNL_OP_DEL_VLAN:
		err = ops->remove_vlan_msg(vf, msg);
		break;
	case VIRTCHNL_OP_GET_SUPPORTED_RXDIDS:
		err = ops->query_rxdid(vf);
		break;
	case VIRTCHNL_OP_GET_RSS_HENA_CAPS:
		err = ops->get_rss_hena(vf);
		break;
	case VIRTCHNL_OP_SET_RSS_HENA:
		err = ops->set_rss_hena_msg(vf, msg);
		break;
	case VIRTCHNL_OP_ENABLE_VLAN_STRIPPING:
		err = ops->ena_vlan_stripping(vf);
		break;
	case VIRTCHNL_OP_DISABLE_VLAN_STRIPPING:
		err = ops->dis_vlan_stripping(vf);
		break;
	case VIRTCHNL_OP_ADD_FDIR_FILTER:
		err = ops->add_fdir_fltr_msg(vf, msg);
		break;
	case VIRTCHNL_OP_DEL_FDIR_FILTER:
		err = ops->del_fdir_fltr_msg(vf, msg);
		break;
	case VIRTCHNL_OP_ADD_RSS_CFG:
		err = ops->handle_rss_cfg_msg(vf, msg, true);
		break;
	case VIRTCHNL_OP_DEL_RSS_CFG:
		err = ops->handle_rss_cfg_msg(vf, msg, false);
		break;
	case VIRTCHNL_OP_GET_OFFLOAD_VLAN_V2_CAPS:
		err = ops->get_offload_vlan_v2_caps(vf);
		break;
	case VIRTCHNL_OP_ADD_VLAN_V2:
		err = ops->add_vlan_v2_msg(vf, msg);
		break;
	case VIRTCHNL_OP_DEL_VLAN_V2:
		err = ops->remove_vlan_v2_msg(vf, msg);
		break;
	case VIRTCHNL_OP_ENABLE_VLAN_STRIPPING_V2:
		err = ops->ena_vlan_stripping_v2_msg(vf, msg);
		break;
	case VIRTCHNL_OP_DISABLE_VLAN_STRIPPING_V2:
		err = ops->dis_vlan_stripping_v2_msg(vf, msg);
		break;
	case VIRTCHNL_OP_ENABLE_VLAN_INSERTION_V2:
		err = ops->ena_vlan_insertion_v2_msg(vf, msg);
		break;
	case VIRTCHNL_OP_DISABLE_VLAN_INSERTION_V2:
		err = ops->dis_vlan_insertion_v2_msg(vf, msg);
		break;
	case VIRTCHNL_OP_UNKNOWN:
	default:
		dev_err(dev, "Unsupported opcode %d from VF %d\n", v_opcode,
			vf_id);
		err = ice_vc_send_msg_to_vf(vf, v_opcode,
					    VIRTCHNL_STATUS_ERR_NOT_SUPPORTED,
					    NULL, 0);
		break;
	}
	if (err) {
		/* Helper function cares less about error return values here
		 * as it is busy with pending work.
		 */
		dev_info(dev, "PF failed to honor VF %d, opcode %d, error %d\n",
			 vf_id, v_opcode, err);
	}

finish:
	mutex_unlock(&vf->cfg_lock);
	ice_put_vf(vf);
}
