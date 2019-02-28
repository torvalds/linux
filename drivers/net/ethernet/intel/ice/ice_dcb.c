// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Intel Corporation. */

#include "ice_common.h"
#include "ice_sched.h"
#include "ice_dcb.h"

/**
 * ice_aq_start_lldp
 * @hw: pointer to the HW struct
 * @cd: pointer to command details structure or NULL
 *
 * Start the embedded LLDP Agent on all ports. (0x0A06)
 */
enum ice_status ice_aq_start_lldp(struct ice_hw *hw, struct ice_sq_cd *cd)
{
	struct ice_aqc_lldp_start *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.lldp_start;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_lldp_start);

	cmd->command = ICE_AQ_LLDP_AGENT_START;

	return ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
}

/**
 * ice_get_dcbx_status
 * @hw: pointer to the HW struct
 *
 * Get the DCBX status from the Firmware
 */
u8 ice_get_dcbx_status(struct ice_hw *hw)
{
	u32 reg;

	reg = rd32(hw, PRTDCB_GENS);
	return (u8)((reg & PRTDCB_GENS_DCBX_STATUS_M) >>
		    PRTDCB_GENS_DCBX_STATUS_S);
}

/**
 * ice_aq_start_stop_dcbx - Start/Stop DCBx service in FW
 * @hw: pointer to the HW struct
 * @start_dcbx_agent: True if DCBx Agent needs to be started
 *		      False if DCBx Agent needs to be stopped
 * @dcbx_agent_status: FW indicates back the DCBx agent status
 *		       True if DCBx Agent is active
 *		       False if DCBx Agent is stopped
 * @cd: pointer to command details structure or NULL
 *
 * Start/Stop the embedded dcbx Agent. In case that this wrapper function
 * returns ICE_SUCCESS, caller will need to check if FW returns back the same
 * value as stated in dcbx_agent_status, and react accordingly. (0x0A09)
 */
enum ice_status
ice_aq_start_stop_dcbx(struct ice_hw *hw, bool start_dcbx_agent,
		       bool *dcbx_agent_status, struct ice_sq_cd *cd)
{
	struct ice_aqc_lldp_stop_start_specific_agent *cmd;
	enum ice_status status;
	struct ice_aq_desc desc;
	u16 opcode;

	cmd = &desc.params.lldp_agent_ctrl;

	opcode = ice_aqc_opc_lldp_stop_start_specific_agent;

	ice_fill_dflt_direct_cmd_desc(&desc, opcode);

	if (start_dcbx_agent)
		cmd->command = ICE_AQC_START_STOP_AGENT_START_DCBX;

	status = ice_aq_send_cmd(hw, &desc, NULL, 0, cd);

	*dcbx_agent_status = false;

	if (!status &&
	    cmd->command == ICE_AQC_START_STOP_AGENT_START_DCBX)
		*dcbx_agent_status = true;

	return status;
}
