// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2025 Intel Corporation. */

#include <linux/crc32.h>
#include <linux/pldmfw.h>
#include <linux/uuid.h>

#include "ixgbe.h"
#include "ixgbe_fw_update.h"

struct ixgbe_fwu_priv {
	struct pldmfw context;

	struct ixgbe_adapter *adapter;
	struct netlink_ext_ack *extack;

	/* Track which NVM banks to activate at the end of the update */
	u8 activate_flags;
	bool emp_reset_available;
};

/**
 * ixgbe_send_package_data - Send record package data to firmware
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
 * Return: zero on success, or a negative error code on failure.
 */
static int ixgbe_send_package_data(struct pldmfw *context,
				   const u8 *data, u16 length)
{
	struct ixgbe_fwu_priv *priv = container_of(context,
						   struct ixgbe_fwu_priv,
						   context);
	struct ixgbe_adapter *adapter = priv->adapter;
	struct ixgbe_hw *hw = &adapter->hw;
	u8 *package_data;
	int err;

	package_data = kmemdup(data, length, GFP_KERNEL);
	if (!package_data)
		return -ENOMEM;

	err = ixgbe_nvm_set_pkg_data(hw, false, package_data, length);

	kfree(package_data);

	return err;
}

/**
 * ixgbe_check_component_response - Report firmware response to a component
 * @adapter: device private data structure
 * @response: indicates whether this component can be updated
 * @code: code indicating reason for response
 * @extack: netlink extended ACK structure
 *
 * Check whether firmware indicates if this component can be updated. Report
 * a suitable error message over the netlink extended ACK if the component
 * cannot be updated.
 *
 * Return: 0 if the component can be updated, or -ECANCELED if the
 * firmware indicates the component cannot be updated.
 */
static int ixgbe_check_component_response(struct ixgbe_adapter *adapter,
					  u8 response, u8 code,
					  struct netlink_ext_ack *extack)
{
	struct ixgbe_hw *hw = &adapter->hw;

	switch (response) {
	case IXGBE_ACI_NVM_PASS_COMP_CAN_BE_UPDATED:
		/* Firmware indicated this update is good to proceed. */
		return 0;
	case IXGBE_ACI_NVM_PASS_COMP_CAN_MAY_BE_UPDATEABLE:
		NL_SET_ERR_MSG_MOD(extack,
				   "Firmware recommends not updating, as it may result in a downgrade. Continuing anyways");
		return 0;
	case IXGBE_ACI_NVM_PASS_COMP_CAN_NOT_BE_UPDATED:
		NL_SET_ERR_MSG_MOD(extack, "Firmware has rejected updating.");
		break;
	case IXGBE_ACI_NVM_PASS_COMP_PARTIAL_CHECK:
		if (hw->mac.ops.fw_recovery_mode &&
		    hw->mac.ops.fw_recovery_mode(hw))
			return 0;
		break;
	}

	switch (code) {
	case IXGBE_ACI_NVM_PASS_COMP_STAMP_IDENTICAL_CODE:
		NL_SET_ERR_MSG_MOD(extack,
				   "Component comparison stamp is identical to running image");
		break;
	case IXGBE_ACI_NVM_PASS_COMP_STAMP_LOWER:
		NL_SET_ERR_MSG_MOD(extack,
				   "Component comparison stamp is lower than running image");
		break;
	case IXGBE_ACI_NVM_PASS_COMP_INVALID_STAMP_CODE:
		NL_SET_ERR_MSG_MOD(extack,
				   "Component comparison stamp is invalid");
		break;
	case IXGBE_ACI_NVM_PASS_COMP_CONFLICT_CODE:
		NL_SET_ERR_MSG_MOD(extack,
				   "Component table conflict occurred");
		break;
	case IXGBE_ACI_NVM_PASS_COMP_PRE_REQ_NOT_MET_CODE:
		NL_SET_ERR_MSG_MOD(extack, "Component pre-requisites not met");
		break;
	case IXGBE_ACI_NVM_PASS_COMP_NOT_SUPPORTED_CODE:
		NL_SET_ERR_MSG_MOD(extack, "Component not supported");
		break;
	case IXGBE_ACI_NVM_PASS_COMP_CANNOT_DOWNGRADE_CODE:
		NL_SET_ERR_MSG_MOD(extack, "Component cannot be downgraded");
		break;
	case IXGBE_ACI_NVM_PASS_COMP_INCOMPLETE_IMAGE_CODE:
		NL_SET_ERR_MSG_MOD(extack, "Incomplete component image");
		break;
	case IXGBE_ACI_NVM_PASS_COMP_VER_STR_IDENTICAL_CODE:
		NL_SET_ERR_MSG_MOD(extack,
				   "Component version is identical to running image");
		break;
	case IXGBE_ACI_NVM_PASS_COMP_VER_STR_LOWER_CODE:
		NL_SET_ERR_MSG_MOD(extack,
				   "Component version is lower than the running image");
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack,
				   "Received unexpected response code from firmware");
		break;
	}

	return -ECANCELED;
}

/**
 * ixgbe_send_component_table - Send PLDM component table to firmware
 * @context: PLDM fw update structure
 * @component: the component to process
 * @transfer_flag: relative transfer order of this component
 *
 * Read relevant data from the component and forward it to the device
 * firmware. Check the response to determine if the firmware indicates that
 * the update can proceed.
 *
 * This function sends ACI commands related to the NVM, and assumes that
 * the NVM resource has been acquired.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int ixgbe_send_component_table(struct pldmfw *context,
				      struct pldmfw_component *component,
				      u8 transfer_flag)
{
	struct ixgbe_fwu_priv *priv = container_of(context,
						   struct ixgbe_fwu_priv,
						   context);
	struct ixgbe_adapter *adapter = priv->adapter;
	struct netlink_ext_ack *extack = priv->extack;
	struct ixgbe_aci_cmd_nvm_comp_tbl *comp_tbl;
	u8 comp_response, comp_response_code;
	struct ixgbe_hw *hw = &adapter->hw;
	size_t length;
	int err;

	switch (component->identifier) {
	case NVM_COMP_ID_OROM:
	case NVM_COMP_ID_NVM:
	case NVM_COMP_ID_NETLIST:
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack,
				   "Unable to update due to unknown firmware component");
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

	memcpy(comp_tbl->cvs, component->version_string,
	       component->version_len);

	err = ixgbe_nvm_pass_component_tbl(hw, (u8 *)comp_tbl, length,
					   transfer_flag, &comp_response,
					   &comp_response_code);

	kfree(comp_tbl);

	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to transfer component table to firmware");
		return -EIO;
	}

	return ixgbe_check_component_response(adapter,
					      comp_response,
					      comp_response_code, extack);
}

/**
 * ixgbe_write_one_nvm_block - Write an NVM block and await completion response
 * @adapter: the PF data structure
 * @module: the module to write to
 * @offset: offset in bytes
 * @block_size: size of the block to write, up to 4k
 * @block: pointer to block of data to write
 * @last_cmd: whether this is the last command
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
 *   0 - IXGBE_ACI_NVM_POR_FLAG - A full power on is required
 *   1 - IXGBE_ACI_NVM_PERST_FLAG - A cold PCIe reset is required
 *   2 - IXGBE_ACI_NVM_EMPR_FLAG - An EMP reset is required
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int ixgbe_write_one_nvm_block(struct ixgbe_adapter *adapter,
				     u16 module, u32 offset,
				     u16 block_size, u8 *block, bool last_cmd,
				     struct netlink_ext_ack *extack)
{
	struct ixgbe_hw *hw = &adapter->hw;

	return ixgbe_aci_update_nvm(hw, module, offset, block_size, block,
				    last_cmd, 0);
}

/**
 * ixgbe_write_nvm_module - Write data to an NVM module
 * @adapter: the PF driver structure
 * @module: the module id to program
 * @component: the name of the component being updated
 * @image: buffer of image data to write to the NVM
 * @length: length of the buffer
 * @extack: netlink extended ACK structure
 *
 * Loop over the data for a given NVM module and program it in 4 Kb
 * blocks. Notify devlink core of progress after each block is programmed.
 * Loops over a block of data and programs the NVM in 4k block chunks.
 *
 * Note this function assumes the caller has acquired the NVM resource.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int ixgbe_write_nvm_module(struct ixgbe_adapter *adapter, u16 module,
				  const char *component, const u8 *image,
				  u32 length,
				  struct netlink_ext_ack *extack)
{
	struct devlink *devlink = adapter->devlink;
	u32 offset = 0;
	bool last_cmd;
	u8 *block;
	int err;

	devlink_flash_update_status_notify(devlink, "Flashing",
					   component, 0, length);

	block = kzalloc(IXGBE_ACI_MAX_BUFFER_SIZE, GFP_KERNEL);
	if (!block)
		return -ENOMEM;

	do {
		u32 block_size;

		block_size = min_t(u32, IXGBE_ACI_MAX_BUFFER_SIZE,
				   length - offset);
		last_cmd = !(offset + block_size < length);

		memcpy(block, image + offset, block_size);

		err = ixgbe_write_one_nvm_block(adapter, module, offset,
						block_size, block, last_cmd,
						extack);
		if (err)
			break;

		offset += block_size;

		devlink_flash_update_status_notify(devlink, "Flashing",
						   component, offset, length);
	} while (!last_cmd);

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
#define IXGBE_FW_ERASE_TIMEOUT 300

/**
 * ixgbe_erase_nvm_module - Erase an NVM module and await firmware completion
 * @adapter: the PF data structure
 * @module: the module to erase
 * @component: name of the component being updated
 * @extack: netlink extended ACK structure
 *
 * Erase the inactive NVM bank associated with this module, and await for
 * a completion response message from firmware.
 *
 * Note this function assumes the caller has acquired the NVM resource.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int ixgbe_erase_nvm_module(struct ixgbe_adapter *adapter, u16 module,
				  const char *component,
				  struct netlink_ext_ack *extack)
{
	struct devlink *devlink = adapter->devlink;
	struct ixgbe_hw *hw = &adapter->hw;
	int err;

	devlink_flash_update_timeout_notify(devlink, "Erasing", component,
					    IXGBE_FW_ERASE_TIMEOUT);

	err = ixgbe_aci_erase_nvm(hw, module);
	if (err)
		devlink_flash_update_status_notify(devlink, "Erasing failed",
						   component, 0, 0);
	else
		devlink_flash_update_status_notify(devlink, "Erasing done",
						   component, 0, 0);

	return err;
}

/**
 * ixgbe_switch_flash_banks - Tell firmware to switch NVM banks
 * @adapter: Pointer to the PF data structure
 * @activate_flags: flags used for the activation command
 * @emp_reset_available: on return, indicates if EMP reset is available
 * @extack: netlink extended ACK structure
 *
 * Notify firmware to activate the newly written flash banks, and wait for the
 * firmware response.
 *
 * Return: 0 on success or an error code on failure.
 */
static int ixgbe_switch_flash_banks(struct ixgbe_adapter *adapter,
				    u8 activate_flags,
				    bool *emp_reset_available,
				    struct netlink_ext_ack *extack)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u8 response_flags;
	int err;

	err = ixgbe_nvm_write_activate(hw, activate_flags, &response_flags);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to switch active flash banks");
		return err;
	}

	if (emp_reset_available) {
		if (hw->dev_caps.common_cap.reset_restrict_support)
			*emp_reset_available =
				response_flags & IXGBE_ACI_NVM_EMPR_ENA;
		else
			*emp_reset_available = true;
	}

	return 0;
}

/**
 * ixgbe_flash_component - Flash a component of the NVM
 * @context: PLDM fw update structure
 * @component: the component table to program
 *
 * Program the flash contents for a given component. First, determine the
 * module id. Then, erase the secondary bank for this module. Finally, write
 * the contents of the component to the NVM.
 *
 * Note this function assumes the caller has acquired the NVM resource.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int ixgbe_flash_component(struct pldmfw *context,
				 struct pldmfw_component *component)
{
	struct ixgbe_fwu_priv *priv = container_of(context,
						   struct ixgbe_fwu_priv,
						   context);
	struct netlink_ext_ack *extack = priv->extack;
	struct ixgbe_adapter *adapter = priv->adapter;
	const char *name;
	u16 module;
	int err;
	u8 flag;

	switch (component->identifier) {
	case NVM_COMP_ID_OROM:
		module = IXGBE_E610_SR_1ST_OROM_BANK_PTR;
		flag = IXGBE_ACI_NVM_ACTIV_SEL_OROM;
		name = "fw.undi";
		break;
	case NVM_COMP_ID_NVM:
		module = IXGBE_E610_SR_1ST_NVM_BANK_PTR;
		flag = IXGBE_ACI_NVM_ACTIV_SEL_NVM;
		name = "fw.mgmt";
		break;
	case NVM_COMP_ID_NETLIST:
		module = IXGBE_E610_SR_NETLIST_BANK_PTR;
		flag = IXGBE_ACI_NVM_ACTIV_SEL_NETLIST;
		name = "fw.netlist";
		break;

	default:
		return -EOPNOTSUPP;
	}

	/* Mark this component for activating at the end. */
	priv->activate_flags |= flag;

	err = ixgbe_erase_nvm_module(adapter, module, name, extack);
	if (err)
		return err;

	return ixgbe_write_nvm_module(adapter, module, name,
				      component->component_data,
				      component->component_size, extack);
}

/**
 * ixgbe_finalize_update - Perform last steps to complete device update
 * @context: PLDM fw update structure
 *
 * Called as the last step of the update process. Complete the update by
 * telling the firmware to switch active banks, and perform a reset of
 * configured.
 *
 * Return: 0 on success, or an error code on failure.
 */
static int ixgbe_finalize_update(struct pldmfw *context)
{
	struct ixgbe_fwu_priv *priv = container_of(context,
						   struct ixgbe_fwu_priv,
						   context);
	struct ixgbe_adapter *adapter = priv->adapter;
	struct netlink_ext_ack *extack = priv->extack;
	struct devlink *devlink = adapter->devlink;
	int err;

	/* Finally, notify firmware to activate the written NVM banks */
	err = ixgbe_switch_flash_banks(adapter, priv->activate_flags,
				       &priv->emp_reset_available, extack);
	if (err)
		return err;

	adapter->fw_emp_reset_disabled = !priv->emp_reset_available;

	if (!adapter->fw_emp_reset_disabled)
		devlink_flash_update_status_notify(devlink,
						   "Suggested is to activate new firmware by devlink reload, if it doesn't work then a power cycle is required",
						   NULL, 0, 0);

	return 0;
}

static const struct pldmfw_ops ixgbe_fwu_ops_e610 = {
	.match_record = &pldmfw_op_pci_match_record,
	.send_package_data = &ixgbe_send_package_data,
	.send_component_table = &ixgbe_send_component_table,
	.flash_component = &ixgbe_flash_component,
	.finalize_update = &ixgbe_finalize_update,
};

/**
 * ixgbe_get_pending_updates - Check if the component has a pending update
 * @adapter: the PF driver structure
 * @pending: on return, bitmap of updates pending
 * @extack: Netlink extended ACK
 *
 * Check if the device has any pending updates on any flash components.
 *
 * Return: 0 on success, or a negative error code on failure. Update
 * pending with the bitmap of pending updates.
 */
int ixgbe_get_pending_updates(struct ixgbe_adapter *adapter, u8 *pending,
			      struct netlink_ext_ack *extack)
{
	struct ixgbe_hw_dev_caps *dev_caps;
	struct ixgbe_hw *hw = &adapter->hw;
	int err;

	dev_caps = kzalloc(sizeof(*dev_caps), GFP_KERNEL);
	if (!dev_caps)
		return -ENOMEM;

	err = ixgbe_discover_dev_caps(hw, dev_caps);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Unable to read device capabilities");
		kfree(dev_caps);
		return -EIO;
	}

	*pending = 0;

	if (dev_caps->common_cap.nvm_update_pending_nvm)
		*pending |= IXGBE_ACI_NVM_ACTIV_SEL_NVM;

	if (dev_caps->common_cap.nvm_update_pending_orom)
		*pending |= IXGBE_ACI_NVM_ACTIV_SEL_OROM;

	if (dev_caps->common_cap.nvm_update_pending_netlist)
		*pending |= IXGBE_ACI_NVM_ACTIV_SEL_NETLIST;

	kfree(dev_caps);

	return 0;
}

/**
 * ixgbe_cancel_pending_update - Cancel any pending update for a component
 * @adapter: the PF driver structure
 * @component: if not NULL, the name of the component being updated
 * @extack: Netlink extended ACK structure
 *
 * Cancel any pending update for the specified component. If component is
 * NULL, all device updates will be canceled.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int ixgbe_cancel_pending_update(struct ixgbe_adapter *adapter,
				       const char *component,
				       struct netlink_ext_ack *extack)
{
	struct devlink *devlink = adapter->devlink;
	struct ixgbe_hw *hw = &adapter->hw;
	u8 pending;
	int err;

	err = ixgbe_get_pending_updates(adapter, &pending, extack);
	if (err)
		return err;

	/* If the flash_update request is for a specific component, ignore all
	 * of the other components.
	 */
	if (component) {
		if (strcmp(component, "fw.mgmt") == 0)
			pending &= IXGBE_ACI_NVM_ACTIV_SEL_NVM;
		else if (strcmp(component, "fw.undi") == 0)
			pending &= IXGBE_ACI_NVM_ACTIV_SEL_OROM;
		else if (strcmp(component, "fw.netlist") == 0)
			pending &= IXGBE_ACI_NVM_ACTIV_SEL_NETLIST;
		else
			return -EINVAL;
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

	err = ixgbe_acquire_nvm(hw, LIBIE_AQC_RES_ACCESS_WRITE);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to acquire device flash lock");
		return -EIO;
	}

	pending |= IXGBE_ACI_NVM_REVERT_LAST_ACTIV;
	err = ixgbe_switch_flash_banks(adapter, pending, NULL, extack);

	ixgbe_release_nvm(hw);

	return err;
}

/**
 * ixgbe_flash_pldm_image - Write a PLDM-formatted firmware image to the device
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
 * Return: 0 on success or a negative error code on failure.
 */
int ixgbe_flash_pldm_image(struct devlink *devlink,
			   struct devlink_flash_update_params *params,
			   struct netlink_ext_ack *extack)
{
	struct ixgbe_adapter *adapter = devlink_priv(devlink);
	struct device *dev = &adapter->pdev->dev;
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_fwu_priv priv;
	u8 preservation;
	int err;

	if (hw->mac.type != ixgbe_mac_e610)
		return -EOPNOTSUPP;

	switch (params->overwrite_mask) {
	case 0:
		/* preserve all settings and identifiers */
		preservation = IXGBE_ACI_NVM_PRESERVE_ALL;
		break;
	case DEVLINK_FLASH_OVERWRITE_SETTINGS:
		/* Overwrite settings, but preserve vital information such as
		 * device identifiers.
		 */
		preservation = IXGBE_ACI_NVM_PRESERVE_SELECTED;
		break;
	case (DEVLINK_FLASH_OVERWRITE_SETTINGS |
	      DEVLINK_FLASH_OVERWRITE_IDENTIFIERS):
		/* overwrite both settings and identifiers, preserve nothing */
		preservation = IXGBE_ACI_NVM_NO_PRESERVATION;
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack,
				   "Requested overwrite mask is not supported");
		return -EOPNOTSUPP;
	}

	/* Cannot get caps in recovery mode, so lack of nvm_unified_update bit
	 * cannot lead to error
	 */
	if (!hw->dev_caps.common_cap.nvm_unified_update &&
	    (hw->mac.ops.fw_recovery_mode &&
	     !hw->mac.ops.fw_recovery_mode(hw))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Current firmware does not support unified update");
		return -EOPNOTSUPP;
	}

	memset(&priv, 0, sizeof(priv));

	priv.context.ops = &ixgbe_fwu_ops_e610;
	priv.context.dev = dev;
	priv.extack = extack;
	priv.adapter = adapter;
	priv.activate_flags = preservation;

	devlink_flash_update_status_notify(devlink,
					   "Preparing to flash", NULL, 0, 0);

	err = ixgbe_cancel_pending_update(adapter, NULL, extack);
	if (err)
		return err;

	err = ixgbe_acquire_nvm(hw, LIBIE_AQC_RES_ACCESS_WRITE);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to acquire device flash lock");
		return -EIO;
	}

	err = pldmfw_flash_image(&priv.context, params->fw);
	if (err == -ENOENT) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Firmware image has no record matching this device");
	} else if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to flash PLDM image");
	}

	ixgbe_release_nvm(hw);

	return err;
}
