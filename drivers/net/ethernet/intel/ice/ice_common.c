// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice_common.h"
#include "ice_adminq_cmd.h"

#define ICE_PF_RESET_WAIT_COUNT	200

/**
 * ice_set_mac_type - Sets MAC type
 * @hw: pointer to the HW structure
 *
 * This function sets the MAC type of the adapter based on the
 * vendor ID and device ID stored in the hw structure.
 */
static enum ice_status ice_set_mac_type(struct ice_hw *hw)
{
	if (hw->vendor_id != PCI_VENDOR_ID_INTEL)
		return ICE_ERR_DEVICE_NOT_SUPPORTED;

	hw->mac_type = ICE_MAC_GENERIC;
	return 0;
}

/**
 * ice_clear_pf_cfg - Clear PF configuration
 * @hw: pointer to the hardware structure
 */
enum ice_status ice_clear_pf_cfg(struct ice_hw *hw)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_clear_pf_cfg);

	return ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
}

/**
 * ice_init_hw - main hardware initialization routine
 * @hw: pointer to the hardware structure
 */
enum ice_status ice_init_hw(struct ice_hw *hw)
{
	enum ice_status status;

	/* Set MAC type based on DeviceID */
	status = ice_set_mac_type(hw);
	if (status)
		return status;

	hw->pf_id = (u8)(rd32(hw, PF_FUNC_RID) &
			 PF_FUNC_RID_FUNC_NUM_M) >>
		PF_FUNC_RID_FUNC_NUM_S;

	status = ice_reset(hw, ICE_RESET_PFR);
	if (status)
		return status;

	status = ice_init_all_ctrlq(hw);
	if (status)
		goto err_unroll_cqinit;

	status = ice_clear_pf_cfg(hw);
	if (status)
		goto err_unroll_cqinit;

	ice_clear_pxe_mode(hw);

	status = ice_init_nvm(hw);
	if (status)
		goto err_unroll_cqinit;

	return 0;

err_unroll_cqinit:
	ice_shutdown_all_ctrlq(hw);
	return status;
}

/**
 * ice_deinit_hw - unroll initialization operations done by ice_init_hw
 * @hw: pointer to the hardware structure
 */
void ice_deinit_hw(struct ice_hw *hw)
{
	ice_shutdown_all_ctrlq(hw);
}

/**
 * ice_check_reset - Check to see if a global reset is complete
 * @hw: pointer to the hardware structure
 */
enum ice_status ice_check_reset(struct ice_hw *hw)
{
	u32 cnt, reg = 0, grst_delay;

	/* Poll for Device Active state in case a recent CORER, GLOBR,
	 * or EMPR has occurred. The grst delay value is in 100ms units.
	 * Add 1sec for outstanding AQ commands that can take a long time.
	 */
	grst_delay = ((rd32(hw, GLGEN_RSTCTL) & GLGEN_RSTCTL_GRSTDEL_M) >>
		      GLGEN_RSTCTL_GRSTDEL_S) + 10;

	for (cnt = 0; cnt < grst_delay; cnt++) {
		mdelay(100);
		reg = rd32(hw, GLGEN_RSTAT);
		if (!(reg & GLGEN_RSTAT_DEVSTATE_M))
			break;
	}

	if (cnt == grst_delay) {
		ice_debug(hw, ICE_DBG_INIT,
			  "Global reset polling failed to complete.\n");
		return ICE_ERR_RESET_FAILED;
	}

#define ICE_RESET_DONE_MASK	(GLNVM_ULD_CORER_DONE_M | \
				 GLNVM_ULD_GLOBR_DONE_M)

	/* Device is Active; check Global Reset processes are done */
	for (cnt = 0; cnt < ICE_PF_RESET_WAIT_COUNT; cnt++) {
		reg = rd32(hw, GLNVM_ULD) & ICE_RESET_DONE_MASK;
		if (reg == ICE_RESET_DONE_MASK) {
			ice_debug(hw, ICE_DBG_INIT,
				  "Global reset processes done. %d\n", cnt);
			break;
		}
		mdelay(10);
	}

	if (cnt == ICE_PF_RESET_WAIT_COUNT) {
		ice_debug(hw, ICE_DBG_INIT,
			  "Wait for Reset Done timed out. GLNVM_ULD = 0x%x\n",
			  reg);
		return ICE_ERR_RESET_FAILED;
	}

	return 0;
}

/**
 * ice_pf_reset - Reset the PF
 * @hw: pointer to the hardware structure
 *
 * If a global reset has been triggered, this function checks
 * for its completion and then issues the PF reset
 */
static enum ice_status ice_pf_reset(struct ice_hw *hw)
{
	u32 cnt, reg;

	/* If at function entry a global reset was already in progress, i.e.
	 * state is not 'device active' or any of the reset done bits are not
	 * set in GLNVM_ULD, there is no need for a PF Reset; poll until the
	 * global reset is done.
	 */
	if ((rd32(hw, GLGEN_RSTAT) & GLGEN_RSTAT_DEVSTATE_M) ||
	    (rd32(hw, GLNVM_ULD) & ICE_RESET_DONE_MASK) ^ ICE_RESET_DONE_MASK) {
		/* poll on global reset currently in progress until done */
		if (ice_check_reset(hw))
			return ICE_ERR_RESET_FAILED;

		return 0;
	}

	/* Reset the PF */
	reg = rd32(hw, PFGEN_CTRL);

	wr32(hw, PFGEN_CTRL, (reg | PFGEN_CTRL_PFSWR_M));

	for (cnt = 0; cnt < ICE_PF_RESET_WAIT_COUNT; cnt++) {
		reg = rd32(hw, PFGEN_CTRL);
		if (!(reg & PFGEN_CTRL_PFSWR_M))
			break;

		mdelay(1);
	}

	if (cnt == ICE_PF_RESET_WAIT_COUNT) {
		ice_debug(hw, ICE_DBG_INIT,
			  "PF reset polling failed to complete.\n");
		return ICE_ERR_RESET_FAILED;
	}

	return 0;
}

/**
 * ice_reset - Perform different types of reset
 * @hw: pointer to the hardware structure
 * @req: reset request
 *
 * This function triggers a reset as specified by the req parameter.
 *
 * Note:
 * If anything other than a PF reset is triggered, PXE mode is restored.
 * This has to be cleared using ice_clear_pxe_mode again, once the AQ
 * interface has been restored in the rebuild flow.
 */
enum ice_status ice_reset(struct ice_hw *hw, enum ice_reset_req req)
{
	u32 val = 0;

	switch (req) {
	case ICE_RESET_PFR:
		return ice_pf_reset(hw);
	case ICE_RESET_CORER:
		ice_debug(hw, ICE_DBG_INIT, "CoreR requested\n");
		val = GLGEN_RTRIG_CORER_M;
		break;
	case ICE_RESET_GLOBR:
		ice_debug(hw, ICE_DBG_INIT, "GlobalR requested\n");
		val = GLGEN_RTRIG_GLOBR_M;
		break;
	}

	val |= rd32(hw, GLGEN_RTRIG);
	wr32(hw, GLGEN_RTRIG, val);
	ice_flush(hw);

	/* wait for the FW to be ready */
	return ice_check_reset(hw);
}

/**
 * ice_debug_cq
 * @hw: pointer to the hardware structure
 * @mask: debug mask
 * @desc: pointer to control queue descriptor
 * @buf: pointer to command buffer
 * @buf_len: max length of buf
 *
 * Dumps debug log about control command with descriptor contents.
 */
void ice_debug_cq(struct ice_hw *hw, u32 __maybe_unused mask, void *desc,
		  void *buf, u16 buf_len)
{
	struct ice_aq_desc *cq_desc = (struct ice_aq_desc *)desc;
	u16 len;

#ifndef CONFIG_DYNAMIC_DEBUG
	if (!(mask & hw->debug_mask))
		return;
#endif

	if (!desc)
		return;

	len = le16_to_cpu(cq_desc->datalen);

	ice_debug(hw, mask,
		  "CQ CMD: opcode 0x%04X, flags 0x%04X, datalen 0x%04X, retval 0x%04X\n",
		  le16_to_cpu(cq_desc->opcode),
		  le16_to_cpu(cq_desc->flags),
		  le16_to_cpu(cq_desc->datalen), le16_to_cpu(cq_desc->retval));
	ice_debug(hw, mask, "\tcookie (h,l) 0x%08X 0x%08X\n",
		  le32_to_cpu(cq_desc->cookie_high),
		  le32_to_cpu(cq_desc->cookie_low));
	ice_debug(hw, mask, "\tparam (0,1)  0x%08X 0x%08X\n",
		  le32_to_cpu(cq_desc->params.generic.param0),
		  le32_to_cpu(cq_desc->params.generic.param1));
	ice_debug(hw, mask, "\taddr (h,l)   0x%08X 0x%08X\n",
		  le32_to_cpu(cq_desc->params.generic.addr_high),
		  le32_to_cpu(cq_desc->params.generic.addr_low));
	if (buf && cq_desc->datalen != 0) {
		ice_debug(hw, mask, "Buffer:\n");
		if (buf_len < len)
			len = buf_len;

		ice_debug_array(hw, mask, 16, 1, (u8 *)buf, len);
	}
}

/* FW Admin Queue command wrappers */

/**
 * ice_aq_send_cmd - send FW Admin Queue command to FW Admin Queue
 * @hw: pointer to the hw struct
 * @desc: descriptor describing the command
 * @buf: buffer to use for indirect commands (NULL for direct commands)
 * @buf_size: size of buffer for indirect commands (0 for direct commands)
 * @cd: pointer to command details structure
 *
 * Helper function to send FW Admin Queue commands to the FW Admin Queue.
 */
enum ice_status
ice_aq_send_cmd(struct ice_hw *hw, struct ice_aq_desc *desc, void *buf,
		u16 buf_size, struct ice_sq_cd *cd)
{
	return ice_sq_send_cmd(hw, &hw->adminq, desc, buf, buf_size, cd);
}

/**
 * ice_aq_get_fw_ver
 * @hw: pointer to the hw struct
 * @cd: pointer to command details structure or NULL
 *
 * Get the firmware version (0x0001) from the admin queue commands
 */
enum ice_status ice_aq_get_fw_ver(struct ice_hw *hw, struct ice_sq_cd *cd)
{
	struct ice_aqc_get_ver *resp;
	struct ice_aq_desc desc;
	enum ice_status status;

	resp = &desc.params.get_ver;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_ver);

	status = ice_aq_send_cmd(hw, &desc, NULL, 0, cd);

	if (!status) {
		hw->fw_branch = resp->fw_branch;
		hw->fw_maj_ver = resp->fw_major;
		hw->fw_min_ver = resp->fw_minor;
		hw->fw_patch = resp->fw_patch;
		hw->fw_build = le32_to_cpu(resp->fw_build);
		hw->api_branch = resp->api_branch;
		hw->api_maj_ver = resp->api_major;
		hw->api_min_ver = resp->api_minor;
		hw->api_patch = resp->api_patch;
	}

	return status;
}

/**
 * ice_aq_q_shutdown
 * @hw: pointer to the hw struct
 * @unloading: is the driver unloading itself
 *
 * Tell the Firmware that we're shutting down the AdminQ and whether
 * or not the driver is unloading as well (0x0003).
 */
enum ice_status ice_aq_q_shutdown(struct ice_hw *hw, bool unloading)
{
	struct ice_aqc_q_shutdown *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.q_shutdown;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_q_shutdown);

	if (unloading)
		cmd->driver_unloading = cpu_to_le32(ICE_AQC_DRIVER_UNLOADING);

	return ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
}

/**
 * ice_aq_req_res
 * @hw: pointer to the hw struct
 * @res: resource id
 * @access: access type
 * @sdp_number: resource number
 * @timeout: the maximum time in ms that the driver may hold the resource
 * @cd: pointer to command details structure or NULL
 *
 * requests common resource using the admin queue commands (0x0008)
 */
static enum ice_status
ice_aq_req_res(struct ice_hw *hw, enum ice_aq_res_ids res,
	       enum ice_aq_res_access_type access, u8 sdp_number, u32 *timeout,
	       struct ice_sq_cd *cd)
{
	struct ice_aqc_req_res *cmd_resp;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd_resp = &desc.params.res_owner;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_req_res);

	cmd_resp->res_id = cpu_to_le16(res);
	cmd_resp->access_type = cpu_to_le16(access);
	cmd_resp->res_number = cpu_to_le32(sdp_number);

	status = ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
	/* The completion specifies the maximum time in ms that the driver
	 * may hold the resource in the Timeout field.
	 * If the resource is held by someone else, the command completes with
	 * busy return value and the timeout field indicates the maximum time
	 * the current owner of the resource has to free it.
	 */
	if (!status || hw->adminq.sq_last_status == ICE_AQ_RC_EBUSY)
		*timeout = le32_to_cpu(cmd_resp->timeout);

	return status;
}

/**
 * ice_aq_release_res
 * @hw: pointer to the hw struct
 * @res: resource id
 * @sdp_number: resource number
 * @cd: pointer to command details structure or NULL
 *
 * release common resource using the admin queue commands (0x0009)
 */
static enum ice_status
ice_aq_release_res(struct ice_hw *hw, enum ice_aq_res_ids res, u8 sdp_number,
		   struct ice_sq_cd *cd)
{
	struct ice_aqc_req_res *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.res_owner;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_release_res);

	cmd->res_id = cpu_to_le16(res);
	cmd->res_number = cpu_to_le32(sdp_number);

	return ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
}

/**
 * ice_acquire_res
 * @hw: pointer to the HW structure
 * @res: resource id
 * @access: access type (read or write)
 *
 * This function will attempt to acquire the ownership of a resource.
 */
enum ice_status
ice_acquire_res(struct ice_hw *hw, enum ice_aq_res_ids res,
		enum ice_aq_res_access_type access)
{
#define ICE_RES_POLLING_DELAY_MS	10
	u32 delay = ICE_RES_POLLING_DELAY_MS;
	enum ice_status status;
	u32 time_left = 0;
	u32 timeout;

	status = ice_aq_req_res(hw, res, access, 0, &time_left, NULL);

	/* An admin queue return code of ICE_AQ_RC_EEXIST means that another
	 * driver has previously acquired the resource and performed any
	 * necessary updates; in this case the caller does not obtain the
	 * resource and has no further work to do.
	 */
	if (hw->adminq.sq_last_status == ICE_AQ_RC_EEXIST) {
		status = ICE_ERR_AQ_NO_WORK;
		goto ice_acquire_res_exit;
	}

	if (status)
		ice_debug(hw, ICE_DBG_RES,
			  "resource %d acquire type %d failed.\n", res, access);

	/* If necessary, poll until the current lock owner timeouts */
	timeout = time_left;
	while (status && timeout && time_left) {
		mdelay(delay);
		timeout = (timeout > delay) ? timeout - delay : 0;
		status = ice_aq_req_res(hw, res, access, 0, &time_left, NULL);

		if (hw->adminq.sq_last_status == ICE_AQ_RC_EEXIST) {
			/* lock free, but no work to do */
			status = ICE_ERR_AQ_NO_WORK;
			break;
		}

		if (!status)
			/* lock acquired */
			break;
	}
	if (status && status != ICE_ERR_AQ_NO_WORK)
		ice_debug(hw, ICE_DBG_RES, "resource acquire timed out.\n");

ice_acquire_res_exit:
	if (status == ICE_ERR_AQ_NO_WORK) {
		if (access == ICE_RES_WRITE)
			ice_debug(hw, ICE_DBG_RES,
				  "resource indicates no work to do.\n");
		else
			ice_debug(hw, ICE_DBG_RES,
				  "Warning: ICE_ERR_AQ_NO_WORK not expected\n");
	}
	return status;
}

/**
 * ice_release_res
 * @hw: pointer to the HW structure
 * @res: resource id
 *
 * This function will release a resource using the proper Admin Command.
 */
void ice_release_res(struct ice_hw *hw, enum ice_aq_res_ids res)
{
	enum ice_status status;
	u32 total_delay = 0;

	status = ice_aq_release_res(hw, res, 0, NULL);

	/* there are some rare cases when trying to release the resource
	 * results in an admin Q timeout, so handle them correctly
	 */
	while ((status == ICE_ERR_AQ_TIMEOUT) &&
	       (total_delay < hw->adminq.sq_cmd_timeout)) {
		mdelay(1);
		status = ice_aq_release_res(hw, res, 0, NULL);
		total_delay++;
	}
}

/**
 * ice_aq_clear_pxe_mode
 * @hw: pointer to the hw struct
 *
 * Tell the firmware that the driver is taking over from PXE (0x0110).
 */
static enum ice_status ice_aq_clear_pxe_mode(struct ice_hw *hw)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_clear_pxe_mode);
	desc.params.clear_pxe.rx_cnt = ICE_AQC_CLEAR_PXE_RX_CNT;

	return ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
}

/**
 * ice_clear_pxe_mode - clear pxe operations mode
 * @hw: pointer to the hw struct
 *
 * Make sure all PXE mode settings are cleared, including things
 * like descriptor fetch/write-back mode.
 */
void ice_clear_pxe_mode(struct ice_hw *hw)
{
	if (ice_check_sq_alive(hw, &hw->adminq))
		ice_aq_clear_pxe_mode(hw);
}
