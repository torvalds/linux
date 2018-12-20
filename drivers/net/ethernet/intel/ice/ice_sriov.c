// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice_common.h"
#include "ice_adminq_cmd.h"
#include "ice_sriov.h"

/**
 * ice_aq_send_msg_to_vf
 * @hw: pointer to the hardware structure
 * @vfid: VF ID to send msg
 * @v_opcode: opcodes for VF-PF communication
 * @v_retval: return error code
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 * @cd: pointer to command details
 *
 * Send message to VF driver (0x0802) using mailbox
 * queue and asynchronously sending message via
 * ice_sq_send_cmd() function
 */
enum ice_status
ice_aq_send_msg_to_vf(struct ice_hw *hw, u16 vfid, u32 v_opcode, u32 v_retval,
		      u8 *msg, u16 msglen, struct ice_sq_cd *cd)
{
	struct ice_aqc_pf_vf_msg *cmd;
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_mbx_opc_send_msg_to_vf);

	cmd = &desc.params.virt;
	cmd->id = cpu_to_le32(vfid);

	desc.cookie_high = cpu_to_le32(v_opcode);
	desc.cookie_low = cpu_to_le32(v_retval);

	if (msglen)
		desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);

	return ice_sq_send_cmd(hw, &hw->mailboxq, &desc, msg, msglen, cd);
}

/**
 * ice_conv_link_speed_to_virtchnl
 * @adv_link_support: determines the format of the returned link speed
 * @link_speed: variable containing the link_speed to be converted
 *
 * Convert link speed supported by HW to link speed supported by virtchnl.
 * If adv_link_support is true, then return link speed in Mbps.  Else return
 * link speed as a VIRTCHNL_LINK_SPEED_* casted to a u32. Note that the caller
 * needs to cast back to an enum virtchnl_link_speed in the case where
 * adv_link_support is false, but when adv_link_support is true the caller can
 * expect the speed in Mbps.
 */
u32 ice_conv_link_speed_to_virtchnl(bool adv_link_support, u16 link_speed)
{
	u32 speed;

	if (adv_link_support)
		switch (link_speed) {
		case ICE_AQ_LINK_SPEED_10MB:
			speed = ICE_LINK_SPEED_10MBPS;
			break;
		case ICE_AQ_LINK_SPEED_100MB:
			speed = ICE_LINK_SPEED_100MBPS;
			break;
		case ICE_AQ_LINK_SPEED_1000MB:
			speed = ICE_LINK_SPEED_1000MBPS;
			break;
		case ICE_AQ_LINK_SPEED_2500MB:
			speed = ICE_LINK_SPEED_2500MBPS;
			break;
		case ICE_AQ_LINK_SPEED_5GB:
			speed = ICE_LINK_SPEED_5000MBPS;
			break;
		case ICE_AQ_LINK_SPEED_10GB:
			speed = ICE_LINK_SPEED_10000MBPS;
			break;
		case ICE_AQ_LINK_SPEED_20GB:
			speed = ICE_LINK_SPEED_20000MBPS;
			break;
		case ICE_AQ_LINK_SPEED_25GB:
			speed = ICE_LINK_SPEED_25000MBPS;
			break;
		case ICE_AQ_LINK_SPEED_40GB:
			speed = ICE_LINK_SPEED_40000MBPS;
			break;
		default:
			speed = ICE_LINK_SPEED_UNKNOWN;
			break;
		}
	else
		/* Virtchnl speeds are not defined for every speed supported in
		 * the hardware. To maintain compatibility with older AVF
		 * drivers, while reporting the speed the new speed values are
		 * resolved to the closest known virtchnl speeds
		 */
		switch (link_speed) {
		case ICE_AQ_LINK_SPEED_10MB:
		case ICE_AQ_LINK_SPEED_100MB:
			speed = (u32)VIRTCHNL_LINK_SPEED_100MB;
			break;
		case ICE_AQ_LINK_SPEED_1000MB:
		case ICE_AQ_LINK_SPEED_2500MB:
		case ICE_AQ_LINK_SPEED_5GB:
			speed = (u32)VIRTCHNL_LINK_SPEED_1GB;
			break;
		case ICE_AQ_LINK_SPEED_10GB:
			speed = (u32)VIRTCHNL_LINK_SPEED_10GB;
			break;
		case ICE_AQ_LINK_SPEED_20GB:
			speed = (u32)VIRTCHNL_LINK_SPEED_20GB;
			break;
		case ICE_AQ_LINK_SPEED_25GB:
			speed = (u32)VIRTCHNL_LINK_SPEED_25GB;
			break;
		case ICE_AQ_LINK_SPEED_40GB:
			/* fall through */
			speed = (u32)VIRTCHNL_LINK_SPEED_40GB;
			break;
		default:
			speed = (u32)VIRTCHNL_LINK_SPEED_UNKNOWN;
			break;
		}

	return speed;
}
