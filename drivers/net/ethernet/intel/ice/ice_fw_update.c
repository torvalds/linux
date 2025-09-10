// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2019, Intel Corporation. */

#include <linux/unaligned.h>
#include <linux/uuid.h>
#include <linux/crc32.h>
#include <linux/pldmfw.h>
#include "ice.h"
#include "ice_lib.h"
#include "ice_fw_update.h"

struct ice_fwu_priv {
	struct pldmfw context;

	struct ice_pf *pf;
	struct netlink_ext_ack *extack;

	/* Track which NVM banks to activate at the end of the update */
	u8 activate_flags;

	/* Track the firmware response of the required reset to complete the
	 * flash update.
	 *
	 * 0 - ICE_AQC_NVM_POR_FLAG - A full power on is required
	 * 1 - ICE_AQC_NVM_PERST_FLAG - A cold PCIe reset is required
	 * 2 - ICE_AQC_NVM_EMPR_FLAG - An EMP reset is required
	 */
	u8 reset_level;

	/* Track if EMP reset is available */
	u8 emp_reset_available;
};

/**
 * ice_send_package_data - Send record package data to firmware
 * @context: PLDM fw update structure
 * @data: pointer to the package data
 * @length: length of the package data
 *
 * Send a copy of the package data associated with the PLDM record matching
 * this device to the firmware.
 *
 * Note that this function sends an AdminQ command that will fail unless the
 * NVM resource has been acquired.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
static int
ice_send_package_data(struct pldmfw *context, const u8 *data, u16 length)
{
	struct ice_fwu_priv *priv = container_of(context, struct ice_fwu_priv, context);
	struct netlink_ext_ack *extack = priv->extack;
	struct device *dev = context->dev;
	struct ice_pf *pf = priv->pf;
	struct ice_hw *hw = &pf->hw;
	u8 *package_data;
	int status;

	dev_dbg(dev, "Sending PLDM record package data to firmware\n");

	package_data = kmemdup(data, length, GFP_KERNEL);
	if (!package_data)
		return -ENOMEM;

	status = ice_nvm_set_pkg_data(hw, false, package_data, length, NULL);

	kfree(package_data);

	if (status) {
		dev_err(dev, "Failed to send record package data to firmware, err %d aq_err %s\n",
			status, libie_aq_str(hw->adminq.sq_last_status));
		NL_SET_ERR_MSG_MOD(extack, "Failed to record package data to firmware");
		return -EIO;
	}

	return 0;
}

/**
 * ice_check_component_response - Report firmware response to a component
 * @pf: device private data structure
 * @id: component id being checked
 * @response: indicates whether this component can be updated
 * @code: code indicating reason for response
 * @extack: netlink extended ACK structure
 *
 * Check whether firmware indicates if this component can be updated. Report
 * a suitable error message over the netlink extended ACK if the component
 * cannot be updated.
 *
 * Returns: zero if the component can be updated, or -ECANCELED of the
 * firmware indicates the component cannot be updated.
 */
static int
ice_check_component_response(struct ice_pf *pf, u16 id, u8 response, u8 code,
			     struct netlink_ext_ack *extack)
{
	struct device *dev = ice_pf_to_dev(pf);
	const char *component;

	switch (id) {
	case NVM_COMP_ID_OROM:
		component = "fw.undi";
		break;
	case NVM_COMP_ID_NVM:
		component = "fw.mgmt";
		break;
	case NVM_COMP_ID_NETLIST:
		component = "fw.netlist";
		break;
	default:
		WARN(1, "Unexpected unknown component identifier 0x%02x", id);
		return -EINVAL;
	}

	dev_dbg(dev, "%s: firmware response 0x%x, code 0x%x\n",
		component, response, code);

	switch (response) {
	case ICE_AQ_NVM_PASS_COMP_CAN_BE_UPDATED:
		/* firmware indicated this update is good to proceed */
		return 0;
	case ICE_AQ_NVM_PASS_COMP_CAN_MAY_BE_UPDATEABLE:
		dev_warn(dev, "firmware recommends not updating %s, as it may result in a downgrade. continuing anyways\n", component);
		return 0;
	case ICE_AQ_NVM_PASS_COMP_CAN_NOT_BE_UPDATED:
		dev_info(dev, "firmware has rejected updating %s\n", component);
		break;
	case ICE_AQ_NVM_PASS_COMP_PARTIAL_CHECK:
		if (ice_is_recovery_mode(&pf->hw))
			return 0;
		break;
	}

	switch (code) {
	case ICE_AQ_NVM_PASS_COMP_STAMP_IDENTICAL_CODE:
		dev_err(dev, "Component comparison stamp for %s is identical to the running image\n",
			component);
		NL_SET_ERR_MSG_MOD(extack, "Component comparison stamp is identical to running image");
		break;
	case ICE_AQ_NVM_PASS_COMP_STAMP_LOWER:
		dev_err(dev, "Component comparison stamp for %s is lower than the running image\n",
			component);
		NL_SET_ERR_MSG_MOD(extack, "Component comparison stamp is lower than running image");
		break;
	case ICE_AQ_NVM_PASS_COMP_INVALID_STAMP_CODE:
		dev_err(dev, "Component comparison stamp for %s is invalid\n",
			component);
		NL_SET_ERR_MSG_MOD(extack, "Component comparison stamp is invalid");
		break;
	case ICE_AQ_NVM_PASS_COMP_CONFLICT_CODE:
		dev_err(dev, "%s conflicts with a previous component table\n",
			component);
		NL_SET_ERR_MSG_MOD(extack, "Component table conflict occurred");
		break;
	case ICE_AQ_NVM_PASS_COMP_PRE_REQ_NOT_MET_CODE:
		dev_err(dev, "Pre-requisites for component %s have not been met\n",
			component);
		NL_SET_ERR_MSG_MOD(extack, "Component pre-requisites not met");
		break;
	case ICE_AQ_NVM_PASS_COMP_NOT_SUPPORTED_CODE:
		dev_err(dev, "%s is not a supported component\n",
			component);
		NL_SET_ERR_MSG_MOD(extack, "Component not supported");
		break;
	case ICE_AQ_NVM_PASS_COMP_CANNOT_DOWNGRADE_CODE:
		dev_err(dev, "Security restrictions prevent %s from being downgraded\n",
			component);
		NL_SET_ERR_MSG_MOD(extack, "Component cannot be downgraded");
		break;
	case ICE_AQ_NVM_PASS_COMP_INCOMPLETE_IMAGE_CODE:
		dev_err(dev, "Received an incomplete component image for %s\n",
			component);
		NL_SET_ERR_MSG_MOD(extack, "Incomplete component image");
		break;
	case ICE_AQ_NVM_PASS_COMP_VER_STR_IDENTICAL_CODE:
		dev_err(dev, "Component version for %s is identical to the running image\n",
			component);
		NL_SET_ERR_MSG_MOD(extack, "Component version is identical to running image");
		break;
	case ICE_AQ_NVM_PASS_COMP_VER_STR_LOWER_CODE:
		dev_err(dev, "Component version for %s is lower than the running image\n",
			component);
		NL_SET_ERR_MSG_MOD(extack, "Component version is lower than the running image");
		break;
	default:
		dev_err(dev, "Unexpected response code 0x02%x for %s\n",
			code, component);
		NL_SET_ERR_MSG_MOD(extack, "Received unexpected response code from firmware");
		break;
	}

	return -ECANCELED;
}

/**
 * ice_send_component_table - Send PLDM component table to firmware
 * @context: PLDM fw update structure
 * @component: the component to process
 * @transfer_flag: relative transfer order of this component
 *
 * Read relevant data from the component and forward it to the device
 * firmware. Check the response to determine if the firmware indicates that
 * the update can proceed.
 *
 * This function sends AdminQ commands related to the NVM, and assumes that
 * the NVM resource has been acquired.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
static int
ice_send_component_table(struct pldmfw *context, struct pldmfw_component *component,
			 u8 transfer_flag)
{
	struct ice_fwu_priv *priv = container_of(context, struct ice_fwu_priv, context);
	struct netlink_ext_ack *extack = priv->extack;
	struct ice_aqc_nvm_comp_tbl *comp_tbl;
	u8 comp_response, comp_response_code;
	struct device *dev = context->dev;
	struct ice_pf *pf = priv->pf;
	struct ice_hw *hw = &pf->hw;
	size_t length;
	int status;

	switch (component->identifier) {
	case NVM_COMP_ID_OROM:
	case NVM_COMP_ID_NVM:
	case NVM_COMP_ID_NETLIST:
		break;
	default:
		dev_err(dev, "Unable to update due to a firmware component with unknown ID %u\n",
			component->identifier);
		NL_SET_ERR_MSG_MOD(extack, "Unable to update due to unknown firmware component");
		return -EOPNOTSUPP;
	}

	length = struct_size(comp_tbl, cvs, component->version_len);
	comp_tbl = kzalloc(length, GFP_KERNEL);
	if (!comp_tbl)
		return -ENOMEM;

	comp_tbl->comp_class = cpu_to_le16(component->classification);
	comp_tbl->comp_id = cpu_to_le16(component->identifier);
	comp_tbl->comp_class_idx = FWU_COMP_CLASS_IDX_NOT_USE;
	comp_tbl->comp_cmp_stamp = cpu_to_le32(component->comparison_stamp);
	comp_tbl->cvs_type = component->version_type;
	comp_tbl->cvs_len = component->version_len;
	memcpy(comp_tbl->cvs, component->version_string, component->version_len);

	dev_dbg(dev, "Sending component table to firmware:\n");

	status = ice_nvm_pass_component_tbl(hw, (u8 *)comp_tbl, length,
					    transfer_flag, &comp_response,
					    &comp_response_code, NULL);

	kfree(comp_tbl);

	if (status) {
		dev_err(dev, "Failed to transfer component table to firmware, err %d aq_err %s\n",
			status, libie_aq_str(hw->adminq.sq_last_status));
		NL_SET_ERR_MSG_MOD(extack, "Failed to transfer component table to firmware");
		return -EIO;
	}

	return ice_check_component_response(pf, component->identifier, comp_response,
					    comp_response_code, extack);
}

/**
 * ice_write_one_nvm_block - Write an NVM block and await completion response
 * @pf: the PF data structure
 * @module: the module to write to
 * @offset: offset in bytes
 * @block_size: size of the block to write, up to 4k
 * @block: pointer to block of data to write
 * @last_cmd: whether this is the last command
 * @reset_level: storage for reset level required
 * @extack: netlink extended ACK structure
 *
 * Write a block of data to a flash module, and await for the completion
 * response message from firmware.
 *
 * Note this function assumes the caller has acquired the NVM resource.
 *
 * On successful return, reset level indicates the device reset required to
 * complete the update.
 *
 *   0 - ICE_AQC_NVM_POR_FLAG - A full power on is required
 *   1 - ICE_AQC_NVM_PERST_FLAG - A cold PCIe reset is required
 *   2 - ICE_AQC_NVM_EMPR_FLAG - An EMP reset is required
 *
 * Returns: zero on success, or a negative error code on failure.
 */
int ice_write_one_nvm_block(struct ice_pf *pf, u16 module, u32 offset,
			    u16 block_size, u8 *block, bool last_cmd,
			    u8 *reset_level, struct netlink_ext_ack *extack)
{
	u16 completion_module, completion_retval;
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_aq_task task = {};
	struct ice_hw *hw = &pf->hw;
	struct libie_aq_desc *desc;
	struct ice_aqc_nvm *cmd;
	u32 completion_offset;
	int err;

	dev_dbg(dev, "Writing block of %u bytes for module 0x%02x at offset %u\n",
		block_size, module, offset);

	ice_aq_prep_for_event(pf, &task, ice_aqc_opc_nvm_write);

	err = ice_aq_update_nvm(hw, module, offset, block_size, block,
				last_cmd, 0, NULL);
	if (err) {
		dev_err(dev, "Failed to flash module 0x%02x with block of size %u at offset %u, err %d aq_err %s\n",
			module, block_size, offset, err,
			libie_aq_str(hw->adminq.sq_last_status));
		NL_SET_ERR_MSG_MOD(extack, "Failed to program flash module");
		return -EIO;
	}

	/* In most cases, firmware reports a write completion within a few
	 * milliseconds. However, it has been observed that a completion might
	 * take more than a second to complete in some cases. The timeout here
	 * is conservative and is intended to prevent failure to update when
	 * firmware is slow to respond.
	 */
	err = ice_aq_wait_for_event(pf, &task, 15 * HZ);
	if (err) {
		dev_err(dev, "Timed out while trying to flash module 0x%02x with block of size %u at offset %u, err %d\n",
			module, block_size, offset, err);
		NL_SET_ERR_MSG_MOD(extack, "Timed out waiting for firmware");
		return -EIO;
	}

	desc = &task.event.desc;
	cmd = libie_aq_raw(desc);
	completion_module = le16_to_cpu(cmd->module_typeid);
	completion_retval = le16_to_cpu(desc->retval);

	completion_offset = le16_to_cpu(cmd->offset_low);
	completion_offset |= cmd->offset_high << 16;

	if (completion_module != module) {
		dev_err(dev, "Unexpected module_typeid in write completion: got 0x%x, expected 0x%x\n",
			completion_module, module);
		NL_SET_ERR_MSG_MOD(extack, "Unexpected firmware response");
		return -EIO;
	}

	if (completion_offset != offset) {
		dev_err(dev, "Unexpected offset in write completion: got %u, expected %u\n",
			completion_offset, offset);
		NL_SET_ERR_MSG_MOD(extack, "Unexpected firmware response");
		return -EIO;
	}

	if (completion_retval) {
		dev_err(dev, "Firmware failed to flash module 0x%02x with block of size %u at offset %u, err %s\n",
			module, block_size, offset,
			libie_aq_str((enum libie_aq_err)completion_retval));
		NL_SET_ERR_MSG_MOD(extack, "Firmware failed to program flash module");
		return -EIO;
	}

	/* For the last command to write the NVM bank, newer versions of
	 * firmware indicate the required level of reset to complete
	 * activation of firmware. If the firmware supports this, cache the
	 * response for indicating to the user later. Otherwise, assume that
	 * a full power cycle is required.
	 */
	if (reset_level && last_cmd && module == ICE_SR_1ST_NVM_BANK_PTR) {
		if (hw->dev_caps.common_cap.pcie_reset_avoidance) {
			*reset_level = cmd->cmd_flags &
				       ICE_AQC_NVM_RESET_LVL_M;
			dev_dbg(dev, "Firmware reported required reset level as %u\n",
				*reset_level);
		} else {
			*reset_level = ICE_AQC_NVM_POR_FLAG;
			dev_dbg(dev, "Firmware doesn't support indicating required reset level. Assuming a power cycle is required\n");
		}
	}

	return 0;
}

/**
 * ice_write_nvm_module - Write data to an NVM module
 * @pf: the PF driver structure
 * @module: the module id to program
 * @component: the name of the component being updated
 * @image: buffer of image data to write to the NVM
 * @length: length of the buffer
 * @reset_level: storage for reset level required
 * @extack: netlink extended ACK structure
 *
 * Loop over the data for a given NVM module and program it in 4 Kb
 * blocks. Notify devlink core of progress after each block is programmed.
 * Loops over a block of data and programs the NVM in 4k block chunks.
 *
 * Note this function assumes the caller has acquired the NVM resource.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
static int
ice_write_nvm_module(struct ice_pf *pf, u16 module, const char *component,
		     const u8 *image, u32 length, u8 *reset_level,
		     struct netlink_ext_ack *extack)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct devlink *devlink;
	u32 offset = 0;
	bool last_cmd;
	u8 *block;
	int err;

	dev_dbg(dev, "Beginning write of flash component '%s', module 0x%02x\n", component, module);

	devlink = priv_to_devlink(pf);

	devlink_flash_update_status_notify(devlink, "Flashing",
					   component, 0, length);

	block = kzalloc(ICE_AQ_MAX_BUF_LEN, GFP_KERNEL);
	if (!block)
		return -ENOMEM;

	do {
		u32 block_size;

		block_size = min_t(u32, ICE_AQ_MAX_BUF_LEN, length - offset);
		last_cmd = !(offset + block_size < length);

		/* ice_aq_update_nvm may copy the firmware response into the
		 * buffer, so we must make a copy since the source data is
		 * constant.
		 */
		memcpy(block, image + offset, block_size);

		err = ice_write_one_nvm_block(pf, module, offset, block_size,
					      block, last_cmd, reset_level,
					      extack);
		if (err)
			break;

		offset += block_size;

		devlink_flash_update_status_notify(devlink, "Flashing",
						   component, offset, length);
	} while (!last_cmd);

	dev_dbg(dev, "Completed write of flash component '%s', module 0x%02x\n", component, module);

	if (err)
		devlink_flash_update_status_notify(devlink, "Flashing failed",
						   component, length, length);
	else
		devlink_flash_update_status_notify(devlink, "Flashing done",
						   component, length, length);

	kfree(block);
	return err;
}

/* Length in seconds to wait before timing out when erasing a flash module.
 * Yes, erasing really can take minutes to complete.
 */
#define ICE_FW_ERASE_TIMEOUT 300

/**
 * ice_erase_nvm_module - Erase an NVM module and await firmware completion
 * @pf: the PF data structure
 * @module: the module to erase
 * @component: name of the component being updated
 * @extack: netlink extended ACK structure
 *
 * Erase the inactive NVM bank associated with this module, and await for
 * a completion response message from firmware.
 *
 * Note this function assumes the caller has acquired the NVM resource.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
static int
ice_erase_nvm_module(struct ice_pf *pf, u16 module, const char *component,
		     struct netlink_ext_ack *extack)
{
	u16 completion_module, completion_retval;
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_aq_task task = {};
	struct ice_hw *hw = &pf->hw;
	struct libie_aq_desc *desc;
	struct ice_aqc_nvm *cmd;
	struct devlink *devlink;
	int err;

	dev_dbg(dev, "Beginning erase of flash component '%s', module 0x%02x\n", component, module);

	devlink = priv_to_devlink(pf);

	devlink_flash_update_timeout_notify(devlink, "Erasing", component, ICE_FW_ERASE_TIMEOUT);

	ice_aq_prep_for_event(pf, &task, ice_aqc_opc_nvm_erase);

	err = ice_aq_erase_nvm(hw, module, NULL);
	if (err) {
		dev_err(dev, "Failed to erase %s (module 0x%02x), err %d aq_err %s\n",
			component, module, err,
			libie_aq_str(hw->adminq.sq_last_status));
		NL_SET_ERR_MSG_MOD(extack, "Failed to erase flash module");
		err = -EIO;
		goto out_notify_devlink;
	}

	err = ice_aq_wait_for_event(pf, &task, ICE_FW_ERASE_TIMEOUT * HZ);
	if (err) {
		dev_err(dev, "Timed out waiting for firmware to respond with erase completion for %s (module 0x%02x), err %d\n",
			component, module, err);
		NL_SET_ERR_MSG_MOD(extack, "Timed out waiting for firmware");
		goto out_notify_devlink;
	}

	desc = &task.event.desc;
	cmd = libie_aq_raw(desc);
	completion_module = le16_to_cpu(cmd->module_typeid);
	completion_retval = le16_to_cpu(desc->retval);

	if (completion_module != module) {
		dev_err(dev, "Unexpected module_typeid in erase completion for %s: got 0x%x, expected 0x%x\n",
			component, completion_module, module);
		NL_SET_ERR_MSG_MOD(extack, "Unexpected firmware response");
		err = -EIO;
		goto out_notify_devlink;
	}

	if (completion_retval) {
		dev_err(dev, "Firmware failed to erase %s (module 0x02%x), aq_err %s\n",
			component, module,
			libie_aq_str((enum libie_aq_err)completion_retval));
		NL_SET_ERR_MSG_MOD(extack, "Firmware failed to erase flash");
		err = -EIO;
		goto out_notify_devlink;
	}

	dev_dbg(dev, "Completed erase of flash component '%s', module 0x%02x\n", component, module);

out_notify_devlink:
	if (err)
		devlink_flash_update_status_notify(devlink, "Erasing failed",
						   component, 0, 0);
	else
		devlink_flash_update_status_notify(devlink, "Erasing done",
						   component, 0, 0);

	return err;
}

/**
 * ice_switch_flash_banks - Tell firmware to switch NVM banks
 * @pf: Pointer to the PF data structure
 * @activate_flags: flags used for the activation command
 * @emp_reset_available: on return, indicates if EMP reset is available
 * @extack: netlink extended ACK structure
 *
 * Notify firmware to activate the newly written flash banks, and wait for the
 * firmware response.
 *
 * Returns: zero on success or an error code on failure.
 */
static int
ice_switch_flash_banks(struct ice_pf *pf, u8 activate_flags,
		       u8 *emp_reset_available, struct netlink_ext_ack *extack)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_aq_task task = {};
	struct ice_hw *hw = &pf->hw;
	u16 completion_retval;
	u8 response_flags;
	int err;

	ice_aq_prep_for_event(pf, &task, ice_aqc_opc_nvm_write_activate);

	err = ice_nvm_write_activate(hw, activate_flags, &response_flags);
	if (err) {
		dev_err(dev, "Failed to switch active flash banks, err %d aq_err %s\n",
			err, libie_aq_str(hw->adminq.sq_last_status));
		NL_SET_ERR_MSG_MOD(extack, "Failed to switch active flash banks");
		return -EIO;
	}

	/* Newer versions of firmware have support to indicate whether an EMP
	 * reset to reload firmware is available. For older firmware, EMP
	 * reset is always available.
	 */
	if (emp_reset_available) {
		if (hw->dev_caps.common_cap.reset_restrict_support) {
			*emp_reset_available = response_flags & ICE_AQC_NVM_EMPR_ENA;
			dev_dbg(dev, "Firmware indicated that EMP reset is %s\n",
				*emp_reset_available ?
				"available" : "not available");
		} else {
			*emp_reset_available = ICE_AQC_NVM_EMPR_ENA;
			dev_dbg(dev, "Firmware does not support restricting EMP reset availability\n");
		}
	}

	err = ice_aq_wait_for_event(pf, &task, 30 * HZ);
	if (err) {
		dev_err(dev, "Timed out waiting for firmware to switch active flash banks, err %d\n",
			err);
		NL_SET_ERR_MSG_MOD(extack, "Timed out waiting for firmware");
		return err;
	}

	completion_retval = le16_to_cpu(task.event.desc.retval);
	if (completion_retval) {
		dev_err(dev, "Firmware failed to switch active flash banks aq_err %s\n",
			libie_aq_str((enum libie_aq_err)completion_retval));
		NL_SET_ERR_MSG_MOD(extack, "Firmware failed to switch active flash banks");
		return -EIO;
	}

	return 0;
}

/**
 * ice_flash_component - Flash a component of the NVM
 * @context: PLDM fw update structure
 * @component: the component table to program
 *
 * Program the flash contents for a given component. First, determine the
 * module id. Then, erase the secondary bank for this module. Finally, write
 * the contents of the component to the NVM.
 *
 * Note this function assumes the caller has acquired the NVM resource.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
static int
ice_flash_component(struct pldmfw *context, struct pldmfw_component *component)
{
	struct ice_fwu_priv *priv = container_of(context, struct ice_fwu_priv, context);
	struct netlink_ext_ack *extack = priv->extack;
	struct ice_pf *pf = priv->pf;
	const char *name;
	u8 *reset_level;
	u16 module;
	u8 flag;
	int err;

	switch (component->identifier) {
	case NVM_COMP_ID_OROM:
		module = ICE_SR_1ST_OROM_BANK_PTR;
		flag = ICE_AQC_NVM_ACTIV_SEL_OROM;
		reset_level = NULL;
		name = "fw.undi";
		break;
	case NVM_COMP_ID_NVM:
		module = ICE_SR_1ST_NVM_BANK_PTR;
		flag = ICE_AQC_NVM_ACTIV_SEL_NVM;
		reset_level = &priv->reset_level;
		name = "fw.mgmt";
		break;
	case NVM_COMP_ID_NETLIST:
		module = ICE_SR_NETLIST_BANK_PTR;
		flag = ICE_AQC_NVM_ACTIV_SEL_NETLIST;
		reset_level = NULL;
		name = "fw.netlist";
		break;
	default:
		/* This should not trigger, since we check the id before
		 * sending the component table to firmware.
		 */
		WARN(1, "Unexpected unknown component identifier 0x%02x",
		     component->identifier);
		return -EINVAL;
	}

	/* Mark this component for activating at the end */
	priv->activate_flags |= flag;

	err = ice_erase_nvm_module(pf, module, name, extack);
	if (err)
		return err;

	return ice_write_nvm_module(pf, module, name, component->component_data,
				    component->component_size, reset_level,
				    extack);
}

/**
 * ice_finalize_update - Perform last steps to complete device update
 * @context: PLDM fw update structure
 *
 * Called as the last step of the update process. Complete the update by
 * telling the firmware to switch active banks, and perform a reset of
 * configured.
 *
 * Returns: 0 on success, or an error code on failure.
 */
static int ice_finalize_update(struct pldmfw *context)
{
	struct ice_fwu_priv *priv = container_of(context, struct ice_fwu_priv, context);
	struct netlink_ext_ack *extack = priv->extack;
	struct ice_pf *pf = priv->pf;
	struct devlink *devlink;
	int err;

	/* Finally, notify firmware to activate the written NVM banks */
	err = ice_switch_flash_banks(pf, priv->activate_flags,
				     &priv->emp_reset_available, extack);
	if (err)
		return err;

	devlink = priv_to_devlink(pf);

	/* If the required reset is EMPR, but EMPR is disabled, report that
	 * a reboot is required instead.
	 */
	if (priv->reset_level == ICE_AQC_NVM_EMPR_FLAG &&
	    !priv->emp_reset_available) {
		dev_dbg(ice_pf_to_dev(pf), "Firmware indicated EMP reset as sufficient, but EMP reset is disabled\n");
		priv->reset_level = ICE_AQC_NVM_PERST_FLAG;
	}

	switch (priv->reset_level) {
	case ICE_AQC_NVM_EMPR_FLAG:
		devlink_flash_update_status_notify(devlink,
						   "Activate new firmware by devlink reload",
						   NULL, 0, 0);
		break;
	case ICE_AQC_NVM_PERST_FLAG:
		devlink_flash_update_status_notify(devlink,
						   "Activate new firmware by rebooting the system",
						   NULL, 0, 0);
		break;
	case ICE_AQC_NVM_POR_FLAG:
	default:
		devlink_flash_update_status_notify(devlink,
						   "Activate new firmware by power cycling the system",
						   NULL, 0, 0);
		break;
	}

	pf->fw_emp_reset_disabled = !priv->emp_reset_available;

	return 0;
}

struct ice_pldm_pci_record_id {
	u32 vendor;
	u32 device;
	u32 subsystem_vendor;
	u32 subsystem_device;
};

/**
 * ice_op_pci_match_record - Check if a PCI device matches the record
 * @context: PLDM fw update structure
 * @record: list of records extracted from the PLDM image
 *
 * Determine if the PCI device associated with this device matches the record
 * data provided.
 *
 * Searches the descriptor TLVs and extracts the relevant descriptor data into
 * a pldm_pci_record_id. This is then compared against the PCI device ID
 * information.
 *
 * Returns: true if the device matches the record, false otherwise.
 */
static bool
ice_op_pci_match_record(struct pldmfw *context, struct pldmfw_record *record)
{
	struct pci_dev *pdev = to_pci_dev(context->dev);
	struct ice_pldm_pci_record_id id = {
		.vendor = PCI_ANY_ID,
		.device = PCI_ANY_ID,
		.subsystem_vendor = PCI_ANY_ID,
		.subsystem_device = PCI_ANY_ID,
	};
	struct pldmfw_desc_tlv *desc;

	list_for_each_entry(desc, &record->descs, entry) {
		u16 value;
		int *ptr;

		switch (desc->type) {
		case PLDM_DESC_ID_PCI_VENDOR_ID:
			ptr = &id.vendor;
			break;
		case PLDM_DESC_ID_PCI_DEVICE_ID:
			ptr = &id.device;
			break;
		case PLDM_DESC_ID_PCI_SUBVENDOR_ID:
			ptr = &id.subsystem_vendor;
			break;
		case PLDM_DESC_ID_PCI_SUBDEV_ID:
			ptr = &id.subsystem_device;
			break;
		default:
			/* Skip unrelated TLVs */
			continue;
		}

		value = get_unaligned_le16(desc->data);
		/* A value of zero for one of the descriptors is sometimes
		 * used when the record should ignore this field when matching
		 * device. For example if the record applies to any subsystem
		 * device or vendor.
		 */
		if (value)
			*ptr = value;
		else
			*ptr = PCI_ANY_ID;
	}

	/* the E822 device can have a generic device ID so check for that */
	if ((id.vendor == PCI_ANY_ID || id.vendor == pdev->vendor) &&
	    (id.device == PCI_ANY_ID || id.device == pdev->device ||
	    id.device == ICE_DEV_ID_E822_SI_DFLT) &&
	    (id.subsystem_vendor == PCI_ANY_ID ||
	    id.subsystem_vendor == pdev->subsystem_vendor) &&
	    (id.subsystem_device == PCI_ANY_ID ||
	    id.subsystem_device == pdev->subsystem_device))
		return true;

	return false;
}

static const struct pldmfw_ops ice_fwu_ops_e810 = {
	.match_record = &pldmfw_op_pci_match_record,
	.send_package_data = &ice_send_package_data,
	.send_component_table = &ice_send_component_table,
	.flash_component = &ice_flash_component,
	.finalize_update = &ice_finalize_update,
};

static const struct pldmfw_ops ice_fwu_ops_e822 = {
	.match_record = &ice_op_pci_match_record,
	.send_package_data = &ice_send_package_data,
	.send_component_table = &ice_send_component_table,
	.flash_component = &ice_flash_component,
	.finalize_update = &ice_finalize_update,
};

/**
 * ice_get_pending_updates - Check if the component has a pending update
 * @pf: the PF driver structure
 * @pending: on return, bitmap of updates pending
 * @extack: Netlink extended ACK
 *
 * Check if the device has any pending updates on any flash components.
 *
 * Returns: zero on success, or a negative error code on failure. Updates
 * pending with the bitmap of pending updates.
 */
int ice_get_pending_updates(struct ice_pf *pf, u8 *pending,
			    struct netlink_ext_ack *extack)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw_dev_caps *dev_caps;
	struct ice_hw *hw = &pf->hw;
	int err;

	dev_caps = kzalloc(sizeof(*dev_caps), GFP_KERNEL);
	if (!dev_caps)
		return -ENOMEM;

	/* Read the most recent device capabilities from firmware. Do not use
	 * the cached values in hw->dev_caps, because the pending update flag
	 * may have changed, e.g. if an update was previously completed and
	 * the system has not yet rebooted.
	 */
	err = ice_discover_dev_caps(hw, dev_caps);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Unable to read device capabilities");
		kfree(dev_caps);
		return err;
	}

	*pending = 0;

	if (dev_caps->common_cap.nvm_update_pending_nvm) {
		dev_info(dev, "The fw.mgmt flash component has a pending update\n");
		*pending |= ICE_AQC_NVM_ACTIV_SEL_NVM;
	}

	if (dev_caps->common_cap.nvm_update_pending_orom) {
		dev_info(dev, "The fw.undi flash component has a pending update\n");
		*pending |= ICE_AQC_NVM_ACTIV_SEL_OROM;
	}

	if (dev_caps->common_cap.nvm_update_pending_netlist) {
		dev_info(dev, "The fw.netlist flash component has a pending update\n");
		*pending |= ICE_AQC_NVM_ACTIV_SEL_NETLIST;
	}

	kfree(dev_caps);

	return 0;
}

/**
 * ice_cancel_pending_update - Cancel any pending update for a component
 * @pf: the PF driver structure
 * @component: if not NULL, the name of the component being updated
 * @extack: Netlink extended ACK structure
 *
 * Cancel any pending update for the specified component. If component is
 * NULL, all device updates will be canceled.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
static int
ice_cancel_pending_update(struct ice_pf *pf, const char *component,
			  struct netlink_ext_ack *extack)
{
	struct devlink *devlink = priv_to_devlink(pf);
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	u8 pending;
	int err;

	err = ice_get_pending_updates(pf, &pending, extack);
	if (err)
		return err;

	/* If the flash_update request is for a specific component, ignore all
	 * of the other components.
	 */
	if (component) {
		if (strcmp(component, "fw.mgmt") == 0)
			pending &= ICE_AQC_NVM_ACTIV_SEL_NVM;
		else if (strcmp(component, "fw.undi") == 0)
			pending &= ICE_AQC_NVM_ACTIV_SEL_OROM;
		else if (strcmp(component, "fw.netlist") == 0)
			pending &= ICE_AQC_NVM_ACTIV_SEL_NETLIST;
		else
			WARN(1, "Unexpected flash component %s", component);
	}

	/* There is no previous pending update, so this request may continue */
	if (!pending)
		return 0;

	/* In order to allow overwriting a previous pending update, notify
	 * firmware to cancel that update by issuing the appropriate command.
	 */
	devlink_flash_update_status_notify(devlink,
					   "Canceling previous pending update",
					   component, 0, 0);

	err = ice_acquire_nvm(hw, ICE_RES_WRITE);
	if (err) {
		dev_err(dev, "Failed to acquire device flash lock, err %d aq_err %s\n",
			err, libie_aq_str(hw->adminq.sq_last_status));
		NL_SET_ERR_MSG_MOD(extack, "Failed to acquire device flash lock");
		return err;
	}

	pending |= ICE_AQC_NVM_REVERT_LAST_ACTIV;
	err = ice_switch_flash_banks(pf, pending, NULL, extack);

	ice_release_nvm(hw);

	/* Since we've canceled the pending update, we no longer know if EMP
	 * reset is restricted.
	 */
	pf->fw_emp_reset_disabled = false;

	return err;
}

/**
 * ice_devlink_flash_update - Write a firmware image to the device
 * @devlink: pointer to devlink associated with the device to update
 * @params: devlink flash update parameters
 * @extack: netlink extended ACK structure
 *
 * Parse the data for a given firmware file, verifying that it is a valid PLDM
 * formatted image that matches this device.
 *
 * Extract the device record Package Data and Component Tables and send them
 * to the firmware. Extract and write the flash data for each of the three
 * main flash components, "fw.mgmt", "fw.undi", and "fw.netlist". Notify
 * firmware once the data is written to the inactive banks.
 *
 * Returns: zero on success or a negative error code on failure.
 */
int ice_devlink_flash_update(struct devlink *devlink,
			     struct devlink_flash_update_params *params,
			     struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_priv(devlink);
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	struct ice_fwu_priv priv;
	u8 preservation;
	int err;

	if (!params->overwrite_mask) {
		/* preserve all settings and identifiers */
		preservation = ICE_AQC_NVM_PRESERVE_ALL;
	} else if (params->overwrite_mask == DEVLINK_FLASH_OVERWRITE_SETTINGS) {
		/* overwrite settings, but preserve the vital device identifiers */
		preservation = ICE_AQC_NVM_PRESERVE_SELECTED;
	} else if (params->overwrite_mask == (DEVLINK_FLASH_OVERWRITE_SETTINGS |
					      DEVLINK_FLASH_OVERWRITE_IDENTIFIERS)) {
		/* overwrite both settings and identifiers, preserve nothing */
		preservation = ICE_AQC_NVM_NO_PRESERVATION;
	} else {
		NL_SET_ERR_MSG_MOD(extack, "Requested overwrite mask is not supported");
		return -EOPNOTSUPP;
	}

	if (!hw->dev_caps.common_cap.nvm_unified_update && !ice_is_recovery_mode(hw)) {
		NL_SET_ERR_MSG_MOD(extack, "Current firmware does not support unified update");
		return -EOPNOTSUPP;
	}

	memset(&priv, 0, sizeof(priv));

	if (params->component && strcmp(params->component, "fw.mgmt") == 0) {
		priv.context.mode = PLDMFW_UPDATE_MODE_SINGLE_COMPONENT;
		priv.context.component_identifier = NVM_COMP_ID_NVM;
	} else if (params->component) {
		return -EOPNOTSUPP;
	}

	/* the E822 device needs a slightly different ops */
	if (hw->mac_type == ICE_MAC_GENERIC)
		priv.context.ops = &ice_fwu_ops_e822;
	else
		priv.context.ops = &ice_fwu_ops_e810;
	priv.context.dev = dev;
	priv.extack = extack;
	priv.pf = pf;
	priv.activate_flags = preservation;

	devlink_flash_update_status_notify(devlink, "Preparing to flash", NULL, 0, 0);

	err = ice_cancel_pending_update(pf, NULL, extack);
	if (err)
		return err;

	err = ice_acquire_nvm(hw, ICE_RES_WRITE);
	if (err) {
		dev_err(dev, "Failed to acquire device flash lock, err %d aq_err %s\n",
			err, libie_aq_str(hw->adminq.sq_last_status));
		NL_SET_ERR_MSG_MOD(extack, "Failed to acquire device flash lock");
		return err;
	}

	err = pldmfw_flash_image(&priv.context, params->fw);
	if (err == -ENOENT) {
		dev_err(dev, "Firmware image has no record matching this device\n");
		NL_SET_ERR_MSG_MOD(extack, "Firmware image has no record matching this device");
	} else if (err) {
		/* Do not set a generic extended ACK message here. A more
		 * specific message may already have been set by one of our
		 * ops.
		 */
		dev_err(dev, "Failed to flash PLDM image, err %d", err);
	}

	ice_release_nvm(hw);

	return err;
}
