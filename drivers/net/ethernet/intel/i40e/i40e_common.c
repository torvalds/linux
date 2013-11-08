/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#include "i40e_type.h"
#include "i40e_adminq.h"
#include "i40e_prototype.h"
#include "i40e_virtchnl.h"

/**
 * i40e_set_mac_type - Sets MAC type
 * @hw: pointer to the HW structure
 *
 * This function sets the mac type of the adapter based on the
 * vendor ID and device ID stored in the hw structure.
 **/
static i40e_status i40e_set_mac_type(struct i40e_hw *hw)
{
	i40e_status status = 0;

	if (hw->vendor_id == PCI_VENDOR_ID_INTEL) {
		switch (hw->device_id) {
		case I40E_SFP_XL710_DEVICE_ID:
		case I40E_SFP_X710_DEVICE_ID:
		case I40E_QEMU_DEVICE_ID:
		case I40E_KX_A_DEVICE_ID:
		case I40E_KX_B_DEVICE_ID:
		case I40E_KX_C_DEVICE_ID:
		case I40E_KX_D_DEVICE_ID:
		case I40E_QSFP_A_DEVICE_ID:
		case I40E_QSFP_B_DEVICE_ID:
		case I40E_QSFP_C_DEVICE_ID:
			hw->mac.type = I40E_MAC_XL710;
			break;
		case I40E_VF_DEVICE_ID:
		case I40E_VF_HV_DEVICE_ID:
			hw->mac.type = I40E_MAC_VF;
			break;
		default:
			hw->mac.type = I40E_MAC_GENERIC;
			break;
		}
	} else {
		status = I40E_ERR_DEVICE_NOT_SUPPORTED;
	}

	hw_dbg(hw, "i40e_set_mac_type found mac: %d, returns: %d\n",
		  hw->mac.type, status);
	return status;
}

/**
 * i40e_debug_aq
 * @hw: debug mask related to admin queue
 * @cap: pointer to adminq command descriptor
 * @buffer: pointer to command buffer
 *
 * Dumps debug log about adminq command with descriptor contents.
 **/
void i40e_debug_aq(struct i40e_hw *hw, enum i40e_debug_mask mask, void *desc,
		   void *buffer)
{
	struct i40e_aq_desc *aq_desc = (struct i40e_aq_desc *)desc;
	u8 *aq_buffer = (u8 *)buffer;
	u32 data[4];
	u32 i = 0;

	if ((!(mask & hw->debug_mask)) || (desc == NULL))
		return;

	i40e_debug(hw, mask,
		   "AQ CMD: opcode 0x%04X, flags 0x%04X, datalen 0x%04X, retval 0x%04X\n",
		   aq_desc->opcode, aq_desc->flags, aq_desc->datalen,
		   aq_desc->retval);
	i40e_debug(hw, mask, "\tcookie (h,l) 0x%08X 0x%08X\n",
		   aq_desc->cookie_high, aq_desc->cookie_low);
	i40e_debug(hw, mask, "\tparam (0,1)  0x%08X 0x%08X\n",
		   aq_desc->params.internal.param0,
		   aq_desc->params.internal.param1);
	i40e_debug(hw, mask, "\taddr (h,l)   0x%08X 0x%08X\n",
		   aq_desc->params.external.addr_high,
		   aq_desc->params.external.addr_low);

	if ((buffer != NULL) && (aq_desc->datalen != 0)) {
		memset(data, 0, sizeof(data));
		i40e_debug(hw, mask, "AQ CMD Buffer:\n");
		for (i = 0; i < le16_to_cpu(aq_desc->datalen); i++) {
			data[((i % 16) / 4)] |=
				((u32)aq_buffer[i]) << (8 * (i % 4));
			if ((i % 16) == 15) {
				i40e_debug(hw, mask,
					   "\t0x%04X  %08X %08X %08X %08X\n",
					   i - 15, data[0], data[1], data[2],
					   data[3]);
				memset(data, 0, sizeof(data));
			}
		}
		if ((i % 16) != 0)
			i40e_debug(hw, mask, "\t0x%04X  %08X %08X %08X %08X\n",
				   i - (i % 16), data[0], data[1], data[2],
				   data[3]);
	}
}

/**
 * i40e_init_shared_code - Initialize the shared code
 * @hw: pointer to hardware structure
 *
 * This assigns the MAC type and PHY code and inits the NVM.
 * Does not touch the hardware. This function must be called prior to any
 * other function in the shared code. The i40e_hw structure should be
 * memset to 0 prior to calling this function.  The following fields in
 * hw structure should be filled in prior to calling this function:
 * hw_addr, back, device_id, vendor_id, subsystem_device_id,
 * subsystem_vendor_id, and revision_id
 **/
i40e_status i40e_init_shared_code(struct i40e_hw *hw)
{
	i40e_status status = 0;
	u32 reg;

	hw->phy.get_link_info = true;

	/* Determine port number */
	reg = rd32(hw, I40E_PFGEN_PORTNUM);
	reg = ((reg & I40E_PFGEN_PORTNUM_PORT_NUM_MASK) >>
	       I40E_PFGEN_PORTNUM_PORT_NUM_SHIFT);
	hw->port = (u8)reg;

	i40e_set_mac_type(hw);

	switch (hw->mac.type) {
	case I40E_MAC_XL710:
		break;
	default:
		return I40E_ERR_DEVICE_NOT_SUPPORTED;
		break;
	}

	status = i40e_init_nvm(hw);
	return status;
}

/**
 * i40e_aq_mac_address_read - Retrieve the MAC addresses
 * @hw: pointer to the hw struct
 * @flags: a return indicator of what addresses were added to the addr store
 * @addrs: the requestor's mac addr store
 * @cmd_details: pointer to command details structure or NULL
 **/
static i40e_status i40e_aq_mac_address_read(struct i40e_hw *hw,
				   u16 *flags,
				   struct i40e_aqc_mac_address_read_data *addrs,
				   struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_mac_address_read *cmd_data =
		(struct i40e_aqc_mac_address_read *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_mac_address_read);
	desc.flags |= cpu_to_le16(I40E_AQ_FLAG_BUF);

	status = i40e_asq_send_command(hw, &desc, addrs,
				       sizeof(*addrs), cmd_details);
	*flags = le16_to_cpu(cmd_data->command_flags);

	return status;
}

/**
 * i40e_aq_mac_address_write - Change the MAC addresses
 * @hw: pointer to the hw struct
 * @flags: indicates which MAC to be written
 * @mac_addr: address to write
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_mac_address_write(struct i40e_hw *hw,
				    u16 flags, u8 *mac_addr,
				    struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_mac_address_write *cmd_data =
		(struct i40e_aqc_mac_address_write *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_mac_address_write);
	cmd_data->command_flags = cpu_to_le16(flags);
	memcpy(&cmd_data->mac_sal, &mac_addr[0], 4);
	memcpy(&cmd_data->mac_sah, &mac_addr[4], 2);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_get_mac_addr - get MAC address
 * @hw: pointer to the HW structure
 * @mac_addr: pointer to MAC address
 *
 * Reads the adapter's MAC address from register
 **/
i40e_status i40e_get_mac_addr(struct i40e_hw *hw, u8 *mac_addr)
{
	struct i40e_aqc_mac_address_read_data addrs;
	i40e_status status;
	u16 flags = 0;

	status = i40e_aq_mac_address_read(hw, &flags, &addrs, NULL);

	if (flags & I40E_AQC_LAN_ADDR_VALID)
		memcpy(mac_addr, &addrs.pf_lan_mac, sizeof(addrs.pf_lan_mac));

	return status;
}

/**
 * i40e_validate_mac_addr - Validate MAC address
 * @mac_addr: pointer to MAC address
 *
 * Tests a MAC address to ensure it is a valid Individual Address
 **/
i40e_status i40e_validate_mac_addr(u8 *mac_addr)
{
	i40e_status status = 0;

	/* Make sure it is not a multicast address */
	if (I40E_IS_MULTICAST(mac_addr)) {
		hw_dbg(hw, "MAC address is multicast\n");
		status = I40E_ERR_INVALID_MAC_ADDR;
	/* Not a broadcast address */
	} else if (I40E_IS_BROADCAST(mac_addr)) {
		hw_dbg(hw, "MAC address is broadcast\n");
		status = I40E_ERR_INVALID_MAC_ADDR;
	/* Reject the zero address */
	} else if (mac_addr[0] == 0 && mac_addr[1] == 0 && mac_addr[2] == 0 &&
		   mac_addr[3] == 0 && mac_addr[4] == 0 && mac_addr[5] == 0) {
		hw_dbg(hw, "MAC address is all zeros\n");
		status = I40E_ERR_INVALID_MAC_ADDR;
	}
	return status;
}

/**
 * i40e_pf_reset - Reset the PF
 * @hw: pointer to the hardware structure
 *
 * Assuming someone else has triggered a global reset,
 * assure the global reset is complete and then reset the PF
 **/
i40e_status i40e_pf_reset(struct i40e_hw *hw)
{
	u32 wait_cnt = 0;
	u32 reg = 0;
	u32 grst_del;

	/* Poll for Global Reset steady state in case of recent GRST.
	 * The grst delay value is in 100ms units, and we'll wait a
	 * couple counts longer to be sure we don't just miss the end.
	 */
	grst_del = rd32(hw, I40E_GLGEN_RSTCTL) & I40E_GLGEN_RSTCTL_GRSTDEL_MASK
			>> I40E_GLGEN_RSTCTL_GRSTDEL_SHIFT;
	for (wait_cnt = 0; wait_cnt < grst_del + 2; wait_cnt++) {
		reg = rd32(hw, I40E_GLGEN_RSTAT);
		if (!(reg & I40E_GLGEN_RSTAT_DEVSTATE_MASK))
			break;
		msleep(100);
	}
	if (reg & I40E_GLGEN_RSTAT_DEVSTATE_MASK) {
		hw_dbg(hw, "Global reset polling failed to complete.\n");
		return I40E_ERR_RESET_FAILED;
	}

	/* Determine the PF number based on the PCI fn */
	hw->pf_id = (u8)hw->bus.func;

	/* If there was a Global Reset in progress when we got here,
	 * we don't need to do the PF Reset
	 */
	if (!wait_cnt) {
		reg = rd32(hw, I40E_PFGEN_CTRL);
		wr32(hw, I40E_PFGEN_CTRL,
		     (reg | I40E_PFGEN_CTRL_PFSWR_MASK));
		for (wait_cnt = 0; wait_cnt < 10; wait_cnt++) {
			reg = rd32(hw, I40E_PFGEN_CTRL);
			if (!(reg & I40E_PFGEN_CTRL_PFSWR_MASK))
				break;
			usleep_range(1000, 2000);
		}
		if (reg & I40E_PFGEN_CTRL_PFSWR_MASK) {
			hw_dbg(hw, "PF reset polling failed to complete.\n");
			return I40E_ERR_RESET_FAILED;
		}
	}

	i40e_clear_pxe_mode(hw);
	return 0;
}

/**
 * i40e_clear_pxe_mode - clear pxe operations mode
 * @hw: pointer to the hw struct
 *
 * Make sure all PXE mode settings are cleared, including things
 * like descriptor fetch/write-back mode.
 **/
void i40e_clear_pxe_mode(struct i40e_hw *hw)
{
	u32 reg;

	/* Clear single descriptor fetch/write-back mode */
	reg = rd32(hw, I40E_GLLAN_RCTL_0);
	wr32(hw, I40E_GLLAN_RCTL_0, (reg | I40E_GLLAN_RCTL_0_PXE_MODE_MASK));
}

/**
 * i40e_led_get - return current on/off mode
 * @hw: pointer to the hw struct
 *
 * The value returned is the 'mode' field as defined in the
 * GPIO register definitions: 0x0 = off, 0xf = on, and other
 * values are variations of possible behaviors relating to
 * blink, link, and wire.
 **/
u32 i40e_led_get(struct i40e_hw *hw)
{
	u32 gpio_val = 0;
	u32 mode = 0;
	u32 port;
	int i;

	for (i = 0; i < I40E_HW_CAP_MAX_GPIO; i++) {
		if (!hw->func_caps.led[i])
			continue;

		gpio_val = rd32(hw, I40E_GLGEN_GPIO_CTL(i));
		port = (gpio_val & I40E_GLGEN_GPIO_CTL_PRT_NUM_MASK)
			>> I40E_GLGEN_GPIO_CTL_PRT_NUM_SHIFT;

		if (port != hw->port)
			continue;

		mode = (gpio_val & I40E_GLGEN_GPIO_CTL_LED_MODE_MASK)
				>> I40E_GLGEN_GPIO_CTL_INT_MODE_SHIFT;
		break;
	}

	return mode;
}

/**
 * i40e_led_set - set new on/off mode
 * @hw: pointer to the hw struct
 * @mode: 0=off, else on (see EAS for mode details)
 **/
void i40e_led_set(struct i40e_hw *hw, u32 mode)
{
	u32 gpio_val = 0;
	u32 led_mode = 0;
	u32 port;
	int i;

	for (i = 0; i < I40E_HW_CAP_MAX_GPIO; i++) {
		if (!hw->func_caps.led[i])
			continue;

		gpio_val = rd32(hw, I40E_GLGEN_GPIO_CTL(i));
		port = (gpio_val & I40E_GLGEN_GPIO_CTL_PRT_NUM_MASK)
			>> I40E_GLGEN_GPIO_CTL_PRT_NUM_SHIFT;

		if (port != hw->port)
			continue;

		led_mode = (mode << I40E_GLGEN_GPIO_CTL_LED_MODE_SHIFT) &
			    I40E_GLGEN_GPIO_CTL_LED_MODE_MASK;
		gpio_val &= ~I40E_GLGEN_GPIO_CTL_LED_MODE_MASK;
		gpio_val |= led_mode;
		wr32(hw, I40E_GLGEN_GPIO_CTL(i), gpio_val);
	}
}

/* Admin command wrappers */
/**
 * i40e_aq_queue_shutdown
 * @hw: pointer to the hw struct
 * @unloading: is the driver unloading itself
 *
 * Tell the Firmware that we're shutting down the AdminQ and whether
 * or not the driver is unloading as well.
 **/
i40e_status i40e_aq_queue_shutdown(struct i40e_hw *hw,
					     bool unloading)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_queue_shutdown *cmd =
		(struct i40e_aqc_queue_shutdown *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_queue_shutdown);

	if (unloading)
		cmd->driver_unloading = cpu_to_le32(I40E_AQ_DRIVER_UNLOADING);
	status = i40e_asq_send_command(hw, &desc, NULL, 0, NULL);

	return status;
}

/**
 * i40e_aq_set_link_restart_an
 * @hw: pointer to the hw struct
 * @cmd_details: pointer to command details structure or NULL
 *
 * Sets up the link and restarts the Auto-Negotiation over the link.
 **/
i40e_status i40e_aq_set_link_restart_an(struct i40e_hw *hw,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_set_link_restart_an *cmd =
		(struct i40e_aqc_set_link_restart_an *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_set_link_restart_an);

	cmd->command = I40E_AQ_PHY_RESTART_AN;

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_aq_get_link_info
 * @hw: pointer to the hw struct
 * @enable_lse: enable/disable LinkStatusEvent reporting
 * @link: pointer to link status structure - optional
 * @cmd_details: pointer to command details structure or NULL
 *
 * Returns the link status of the adapter.
 **/
i40e_status i40e_aq_get_link_info(struct i40e_hw *hw,
				bool enable_lse, struct i40e_link_status *link,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_get_link_status *resp =
		(struct i40e_aqc_get_link_status *)&desc.params.raw;
	struct i40e_link_status *hw_link_info = &hw->phy.link_info;
	i40e_status status;
	u16 command_flags;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_get_link_status);

	if (enable_lse)
		command_flags = I40E_AQ_LSE_ENABLE;
	else
		command_flags = I40E_AQ_LSE_DISABLE;
	resp->command_flags = cpu_to_le16(command_flags);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	if (status)
		goto aq_get_link_info_exit;

	/* save off old link status information */
	memcpy(&hw->phy.link_info_old, hw_link_info,
	       sizeof(struct i40e_link_status));

	/* update link status */
	hw_link_info->phy_type = (enum i40e_aq_phy_type)resp->phy_type;
	hw_link_info->link_speed = (enum i40e_aq_link_speed)resp->link_speed;
	hw_link_info->link_info = resp->link_info;
	hw_link_info->an_info = resp->an_info;
	hw_link_info->ext_info = resp->ext_info;

	if (resp->command_flags & cpu_to_le16(I40E_AQ_LSE_ENABLE))
		hw_link_info->lse_enable = true;
	else
		hw_link_info->lse_enable = false;

	/* save link status information */
	if (link)
		*link = *hw_link_info;

	/* flag cleared so helper functions don't call AQ again */
	hw->phy.get_link_info = false;

aq_get_link_info_exit:
	return status;
}

/**
 * i40e_aq_add_vsi
 * @hw: pointer to the hw struct
 * @vsi: pointer to a vsi context struct
 * @cmd_details: pointer to command details structure or NULL
 *
 * Add a VSI context to the hardware.
**/
i40e_status i40e_aq_add_vsi(struct i40e_hw *hw,
				struct i40e_vsi_context *vsi_ctx,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_add_get_update_vsi *cmd =
		(struct i40e_aqc_add_get_update_vsi *)&desc.params.raw;
	struct i40e_aqc_add_get_update_vsi_completion *resp =
		(struct i40e_aqc_add_get_update_vsi_completion *)
		&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_add_vsi);

	cmd->uplink_seid = cpu_to_le16(vsi_ctx->uplink_seid);
	cmd->connection_type = vsi_ctx->connection_type;
	cmd->vf_id = vsi_ctx->vf_num;
	cmd->vsi_flags = cpu_to_le16(vsi_ctx->flags);

	desc.flags |= cpu_to_le16((u16)(I40E_AQ_FLAG_BUF | I40E_AQ_FLAG_RD));
	if (sizeof(vsi_ctx->info) > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);

	status = i40e_asq_send_command(hw, &desc, &vsi_ctx->info,
				    sizeof(vsi_ctx->info), cmd_details);

	if (status)
		goto aq_add_vsi_exit;

	vsi_ctx->seid = le16_to_cpu(resp->seid);
	vsi_ctx->vsi_number = le16_to_cpu(resp->vsi_number);
	vsi_ctx->vsis_allocated = le16_to_cpu(resp->vsi_used);
	vsi_ctx->vsis_unallocated = le16_to_cpu(resp->vsi_free);

aq_add_vsi_exit:
	return status;
}

/**
 * i40e_aq_set_vsi_unicast_promiscuous
 * @hw: pointer to the hw struct
 * @seid: vsi number
 * @set: set unicast promiscuous enable/disable
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_set_vsi_unicast_promiscuous(struct i40e_hw *hw,
				u16 seid, bool set, struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_set_vsi_promiscuous_modes *cmd =
		(struct i40e_aqc_set_vsi_promiscuous_modes *)&desc.params.raw;
	i40e_status status;
	u16 flags = 0;

	i40e_fill_default_direct_cmd_desc(&desc,
					i40e_aqc_opc_set_vsi_promiscuous_modes);

	if (set)
		flags |= I40E_AQC_SET_VSI_PROMISC_UNICAST;

	cmd->promiscuous_flags = cpu_to_le16(flags);

	cmd->valid_flags = cpu_to_le16(I40E_AQC_SET_VSI_PROMISC_UNICAST);

	cmd->seid = cpu_to_le16(seid);
	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_aq_set_vsi_multicast_promiscuous
 * @hw: pointer to the hw struct
 * @seid: vsi number
 * @set: set multicast promiscuous enable/disable
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_set_vsi_multicast_promiscuous(struct i40e_hw *hw,
				u16 seid, bool set, struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_set_vsi_promiscuous_modes *cmd =
		(struct i40e_aqc_set_vsi_promiscuous_modes *)&desc.params.raw;
	i40e_status status;
	u16 flags = 0;

	i40e_fill_default_direct_cmd_desc(&desc,
					i40e_aqc_opc_set_vsi_promiscuous_modes);

	if (set)
		flags |= I40E_AQC_SET_VSI_PROMISC_MULTICAST;

	cmd->promiscuous_flags = cpu_to_le16(flags);

	cmd->valid_flags = cpu_to_le16(I40E_AQC_SET_VSI_PROMISC_MULTICAST);

	cmd->seid = cpu_to_le16(seid);
	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_aq_set_vsi_broadcast
 * @hw: pointer to the hw struct
 * @seid: vsi number
 * @set_filter: true to set filter, false to clear filter
 * @cmd_details: pointer to command details structure or NULL
 *
 * Set or clear the broadcast promiscuous flag (filter) for a given VSI.
 **/
i40e_status i40e_aq_set_vsi_broadcast(struct i40e_hw *hw,
				u16 seid, bool set_filter,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_set_vsi_promiscuous_modes *cmd =
		(struct i40e_aqc_set_vsi_promiscuous_modes *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc,
					i40e_aqc_opc_set_vsi_promiscuous_modes);

	if (set_filter)
		cmd->promiscuous_flags
			    |= cpu_to_le16(I40E_AQC_SET_VSI_PROMISC_BROADCAST);
	else
		cmd->promiscuous_flags
			    &= cpu_to_le16(~I40E_AQC_SET_VSI_PROMISC_BROADCAST);

	cmd->valid_flags = cpu_to_le16(I40E_AQC_SET_VSI_PROMISC_BROADCAST);
	cmd->seid = cpu_to_le16(seid);
	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_get_vsi_params - get VSI configuration info
 * @hw: pointer to the hw struct
 * @vsi: pointer to a vsi context struct
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_get_vsi_params(struct i40e_hw *hw,
				struct i40e_vsi_context *vsi_ctx,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_switch_seid *cmd =
		(struct i40e_aqc_switch_seid *)&desc.params.raw;
	struct i40e_aqc_add_get_update_vsi_completion *resp =
		(struct i40e_aqc_add_get_update_vsi_completion *)
		&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_get_vsi_parameters);

	cmd->seid = cpu_to_le16(vsi_ctx->seid);

	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_BUF);
	if (sizeof(vsi_ctx->info) > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);

	status = i40e_asq_send_command(hw, &desc, &vsi_ctx->info,
				    sizeof(vsi_ctx->info), NULL);

	if (status)
		goto aq_get_vsi_params_exit;

	vsi_ctx->seid = le16_to_cpu(resp->seid);
	vsi_ctx->vsi_number = le16_to_cpu(resp->vsi_number);
	vsi_ctx->vsis_allocated = le16_to_cpu(resp->vsi_used);
	vsi_ctx->vsis_unallocated = le16_to_cpu(resp->vsi_free);

aq_get_vsi_params_exit:
	return status;
}

/**
 * i40e_aq_update_vsi_params
 * @hw: pointer to the hw struct
 * @vsi: pointer to a vsi context struct
 * @cmd_details: pointer to command details structure or NULL
 *
 * Update a VSI context.
 **/
i40e_status i40e_aq_update_vsi_params(struct i40e_hw *hw,
				struct i40e_vsi_context *vsi_ctx,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_switch_seid *cmd =
		(struct i40e_aqc_switch_seid *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_update_vsi_parameters);
	cmd->seid = cpu_to_le16(vsi_ctx->seid);

	desc.flags |= cpu_to_le16((u16)(I40E_AQ_FLAG_BUF | I40E_AQ_FLAG_RD));
	if (sizeof(vsi_ctx->info) > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);

	status = i40e_asq_send_command(hw, &desc, &vsi_ctx->info,
				    sizeof(vsi_ctx->info), cmd_details);

	return status;
}

/**
 * i40e_aq_get_switch_config
 * @hw: pointer to the hardware structure
 * @buf: pointer to the result buffer
 * @buf_size: length of input buffer
 * @start_seid: seid to start for the report, 0 == beginning
 * @cmd_details: pointer to command details structure or NULL
 *
 * Fill the buf with switch configuration returned from AdminQ command
 **/
i40e_status i40e_aq_get_switch_config(struct i40e_hw *hw,
				struct i40e_aqc_get_switch_config_resp *buf,
				u16 buf_size, u16 *start_seid,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_switch_seid *scfg =
		(struct i40e_aqc_switch_seid *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_get_switch_config);
	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_BUF);
	if (buf_size > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);
	scfg->seid = cpu_to_le16(*start_seid);

	status = i40e_asq_send_command(hw, &desc, buf, buf_size, cmd_details);
	*start_seid = le16_to_cpu(scfg->seid);

	return status;
}

/**
 * i40e_aq_get_firmware_version
 * @hw: pointer to the hw struct
 * @fw_major_version: firmware major version
 * @fw_minor_version: firmware minor version
 * @api_major_version: major queue version
 * @api_minor_version: minor queue version
 * @cmd_details: pointer to command details structure or NULL
 *
 * Get the firmware version from the admin queue commands
 **/
i40e_status i40e_aq_get_firmware_version(struct i40e_hw *hw,
				u16 *fw_major_version, u16 *fw_minor_version,
				u16 *api_major_version, u16 *api_minor_version,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_get_version *resp =
		(struct i40e_aqc_get_version *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_get_version);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	if (!status) {
		if (fw_major_version != NULL)
			*fw_major_version = le16_to_cpu(resp->fw_major);
		if (fw_minor_version != NULL)
			*fw_minor_version = le16_to_cpu(resp->fw_minor);
		if (api_major_version != NULL)
			*api_major_version = le16_to_cpu(resp->api_major);
		if (api_minor_version != NULL)
			*api_minor_version = le16_to_cpu(resp->api_minor);
	}

	return status;
}

/**
 * i40e_aq_send_driver_version
 * @hw: pointer to the hw struct
 * @event: driver event: driver ok, start or stop
 * @dv: driver's major, minor version
 * @cmd_details: pointer to command details structure or NULL
 *
 * Send the driver version to the firmware
 **/
i40e_status i40e_aq_send_driver_version(struct i40e_hw *hw,
				struct i40e_driver_version *dv,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_driver_version *cmd =
		(struct i40e_aqc_driver_version *)&desc.params.raw;
	i40e_status status;

	if (dv == NULL)
		return I40E_ERR_PARAM;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_driver_version);

	desc.flags |= cpu_to_le16(I40E_AQ_FLAG_SI);
	cmd->driver_major_ver = dv->major_version;
	cmd->driver_minor_ver = dv->minor_version;
	cmd->driver_build_ver = dv->build_version;
	cmd->driver_subbuild_ver = dv->subbuild_version;
	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_get_link_status - get status of the HW network link
 * @hw: pointer to the hw struct
 *
 * Returns true if link is up, false if link is down.
 *
 * Side effect: LinkStatusEvent reporting becomes enabled
 **/
bool i40e_get_link_status(struct i40e_hw *hw)
{
	i40e_status status = 0;
	bool link_status = false;

	if (hw->phy.get_link_info) {
		status = i40e_aq_get_link_info(hw, true, NULL, NULL);

		if (status)
			goto i40e_get_link_status_exit;
	}

	link_status = hw->phy.link_info.link_info & I40E_AQ_LINK_UP;

i40e_get_link_status_exit:
	return link_status;
}

/**
 * i40e_aq_add_veb - Insert a VEB between the VSI and the MAC
 * @hw: pointer to the hw struct
 * @uplink_seid: the MAC or other gizmo SEID
 * @downlink_seid: the VSI SEID
 * @enabled_tc: bitmap of TCs to be enabled
 * @default_port: true for default port VSI, false for control port
 * @veb_seid: pointer to where to put the resulting VEB SEID
 * @cmd_details: pointer to command details structure or NULL
 *
 * This asks the FW to add a VEB between the uplink and downlink
 * elements.  If the uplink SEID is 0, this will be a floating VEB.
 **/
i40e_status i40e_aq_add_veb(struct i40e_hw *hw, u16 uplink_seid,
				u16 downlink_seid, u8 enabled_tc,
				bool default_port, u16 *veb_seid,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_add_veb *cmd =
		(struct i40e_aqc_add_veb *)&desc.params.raw;
	struct i40e_aqc_add_veb_completion *resp =
		(struct i40e_aqc_add_veb_completion *)&desc.params.raw;
	i40e_status status;
	u16 veb_flags = 0;

	/* SEIDs need to either both be set or both be 0 for floating VEB */
	if (!!uplink_seid != !!downlink_seid)
		return I40E_ERR_PARAM;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_add_veb);

	cmd->uplink_seid = cpu_to_le16(uplink_seid);
	cmd->downlink_seid = cpu_to_le16(downlink_seid);
	cmd->enable_tcs = enabled_tc;
	if (!uplink_seid)
		veb_flags |= I40E_AQC_ADD_VEB_FLOATING;
	if (default_port)
		veb_flags |= I40E_AQC_ADD_VEB_PORT_TYPE_DEFAULT;
	else
		veb_flags |= I40E_AQC_ADD_VEB_PORT_TYPE_DATA;
	cmd->veb_flags = cpu_to_le16(veb_flags);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	if (!status && veb_seid)
		*veb_seid = le16_to_cpu(resp->veb_seid);

	return status;
}

/**
 * i40e_aq_get_veb_parameters - Retrieve VEB parameters
 * @hw: pointer to the hw struct
 * @veb_seid: the SEID of the VEB to query
 * @switch_id: the uplink switch id
 * @floating_veb: set to true if the VEB is floating
 * @statistic_index: index of the stats counter block for this VEB
 * @vebs_used: number of VEB's used by function
 * @vebs_unallocated: total VEB's not reserved by any function
 * @cmd_details: pointer to command details structure or NULL
 *
 * This retrieves the parameters for a particular VEB, specified by
 * uplink_seid, and returns them to the caller.
 **/
i40e_status i40e_aq_get_veb_parameters(struct i40e_hw *hw,
				u16 veb_seid, u16 *switch_id,
				bool *floating, u16 *statistic_index,
				u16 *vebs_used, u16 *vebs_free,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_get_veb_parameters_completion *cmd_resp =
		(struct i40e_aqc_get_veb_parameters_completion *)
		&desc.params.raw;
	i40e_status status;

	if (veb_seid == 0)
		return I40E_ERR_PARAM;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_get_veb_parameters);
	cmd_resp->seid = cpu_to_le16(veb_seid);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);
	if (status)
		goto get_veb_exit;

	if (switch_id)
		*switch_id = le16_to_cpu(cmd_resp->switch_id);
	if (statistic_index)
		*statistic_index = le16_to_cpu(cmd_resp->statistic_index);
	if (vebs_used)
		*vebs_used = le16_to_cpu(cmd_resp->vebs_used);
	if (vebs_free)
		*vebs_free = le16_to_cpu(cmd_resp->vebs_free);
	if (floating) {
		u16 flags = le16_to_cpu(cmd_resp->veb_flags);
		if (flags & I40E_AQC_ADD_VEB_FLOATING)
			*floating = true;
		else
			*floating = false;
	}

get_veb_exit:
	return status;
}

/**
 * i40e_aq_add_macvlan
 * @hw: pointer to the hw struct
 * @seid: VSI for the mac address
 * @mv_list: list of macvlans to be added
 * @count: length of the list
 * @cmd_details: pointer to command details structure or NULL
 *
 * Add MAC/VLAN addresses to the HW filtering
 **/
i40e_status i40e_aq_add_macvlan(struct i40e_hw *hw, u16 seid,
			struct i40e_aqc_add_macvlan_element_data *mv_list,
			u16 count, struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_macvlan *cmd =
		(struct i40e_aqc_macvlan *)&desc.params.raw;
	i40e_status status;
	u16 buf_size;

	if (count == 0 || !mv_list || !hw)
		return I40E_ERR_PARAM;

	buf_size = count * sizeof(struct i40e_aqc_add_macvlan_element_data);

	/* prep the rest of the request */
	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_add_macvlan);
	cmd->num_addresses = cpu_to_le16(count);
	cmd->seid[0] = cpu_to_le16(I40E_AQC_MACVLAN_CMD_SEID_VALID | seid);
	cmd->seid[1] = 0;
	cmd->seid[2] = 0;

	desc.flags |= cpu_to_le16((u16)(I40E_AQ_FLAG_BUF | I40E_AQ_FLAG_RD));
	if (buf_size > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);

	status = i40e_asq_send_command(hw, &desc, mv_list, buf_size,
				    cmd_details);

	return status;
}

/**
 * i40e_aq_remove_macvlan
 * @hw: pointer to the hw struct
 * @seid: VSI for the mac address
 * @mv_list: list of macvlans to be removed
 * @count: length of the list
 * @cmd_details: pointer to command details structure or NULL
 *
 * Remove MAC/VLAN addresses from the HW filtering
 **/
i40e_status i40e_aq_remove_macvlan(struct i40e_hw *hw, u16 seid,
			struct i40e_aqc_remove_macvlan_element_data *mv_list,
			u16 count, struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_macvlan *cmd =
		(struct i40e_aqc_macvlan *)&desc.params.raw;
	i40e_status status;
	u16 buf_size;

	if (count == 0 || !mv_list || !hw)
		return I40E_ERR_PARAM;

	buf_size = count * sizeof(struct i40e_aqc_remove_macvlan_element_data);

	/* prep the rest of the request */
	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_remove_macvlan);
	cmd->num_addresses = cpu_to_le16(count);
	cmd->seid[0] = cpu_to_le16(I40E_AQC_MACVLAN_CMD_SEID_VALID | seid);
	cmd->seid[1] = 0;
	cmd->seid[2] = 0;

	desc.flags |= cpu_to_le16((u16)(I40E_AQ_FLAG_BUF | I40E_AQ_FLAG_RD));
	if (buf_size > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);

	status = i40e_asq_send_command(hw, &desc, mv_list, buf_size,
				       cmd_details);

	return status;
}

/**
 * i40e_aq_add_vlan - Add VLAN ids to the HW filtering
 * @hw: pointer to the hw struct
 * @seid: VSI for the vlan filters
 * @v_list: list of vlan filters to be added
 * @count: length of the list
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_add_vlan(struct i40e_hw *hw, u16 seid,
			struct i40e_aqc_add_remove_vlan_element_data *v_list,
			u8 count, struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_macvlan *cmd =
		(struct i40e_aqc_macvlan *)&desc.params.raw;
	i40e_status status;
	u16 buf_size;

	if (count == 0 || !v_list || !hw)
		return I40E_ERR_PARAM;

	buf_size = count * sizeof(struct i40e_aqc_add_remove_vlan_element_data);

	/* prep the rest of the request */
	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_add_vlan);
	cmd->num_addresses = cpu_to_le16(count);
	cmd->seid[0] = cpu_to_le16(seid | I40E_AQC_MACVLAN_CMD_SEID_VALID);
	cmd->seid[1] = 0;
	cmd->seid[2] = 0;

	desc.flags |= cpu_to_le16((u16)(I40E_AQ_FLAG_BUF | I40E_AQ_FLAG_RD));
	if (buf_size > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);

	status = i40e_asq_send_command(hw, &desc, v_list, buf_size,
				       cmd_details);

	return status;
}

/**
 * i40e_aq_remove_vlan - Remove VLANs from the HW filtering
 * @hw: pointer to the hw struct
 * @seid: VSI for the vlan filters
 * @v_list: list of macvlans to be removed
 * @count: length of the list
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_remove_vlan(struct i40e_hw *hw, u16 seid,
			struct i40e_aqc_add_remove_vlan_element_data *v_list,
			u8 count, struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_macvlan *cmd =
		(struct i40e_aqc_macvlan *)&desc.params.raw;
	i40e_status status;
	u16 buf_size;

	if (count == 0 || !v_list || !hw)
		return I40E_ERR_PARAM;

	buf_size = count * sizeof(struct i40e_aqc_add_remove_vlan_element_data);

	/* prep the rest of the request */
	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_remove_vlan);
	cmd->num_addresses = cpu_to_le16(count);
	cmd->seid[0] = cpu_to_le16(seid | I40E_AQC_MACVLAN_CMD_SEID_VALID);
	cmd->seid[1] = 0;
	cmd->seid[2] = 0;

	desc.flags |= cpu_to_le16((u16)(I40E_AQ_FLAG_BUF | I40E_AQ_FLAG_RD));
	if (buf_size > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);

	status = i40e_asq_send_command(hw, &desc, v_list, buf_size,
				       cmd_details);

	return status;
}

/**
 * i40e_aq_send_msg_to_vf
 * @hw: pointer to the hardware structure
 * @vfid: vf id to send msg
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 * @cmd_details: pointer to command details
 *
 * send msg to vf
 **/
i40e_status i40e_aq_send_msg_to_vf(struct i40e_hw *hw, u16 vfid,
				u32 v_opcode, u32 v_retval, u8 *msg, u16 msglen,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_pf_vf_message *cmd =
		(struct i40e_aqc_pf_vf_message *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_send_msg_to_vf);
	cmd->id = cpu_to_le32(vfid);
	desc.cookie_high = cpu_to_le32(v_opcode);
	desc.cookie_low = cpu_to_le32(v_retval);
	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_SI);
	if (msglen) {
		desc.flags |= cpu_to_le16((u16)(I40E_AQ_FLAG_BUF |
						I40E_AQ_FLAG_RD));
		if (msglen > I40E_AQ_LARGE_BUF)
			desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);
		desc.datalen = cpu_to_le16(msglen);
	}
	status = i40e_asq_send_command(hw, &desc, msg, msglen, cmd_details);

	return status;
}

/**
 * i40e_aq_set_hmc_resource_profile
 * @hw: pointer to the hw struct
 * @profile: type of profile the HMC is to be set as
 * @pe_vf_enabled_count: the number of PE enabled VFs the system has
 * @cmd_details: pointer to command details structure or NULL
 *
 * set the HMC profile of the device.
 **/
i40e_status i40e_aq_set_hmc_resource_profile(struct i40e_hw *hw,
				enum i40e_aq_hmc_profile profile,
				u8 pe_vf_enabled_count,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aq_get_set_hmc_resource_profile *cmd =
		(struct i40e_aq_get_set_hmc_resource_profile *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc,
					i40e_aqc_opc_set_hmc_resource_profile);

	cmd->pm_profile = (u8)profile;
	cmd->pe_vf_enabled = pe_vf_enabled_count;

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_aq_request_resource
 * @hw: pointer to the hw struct
 * @resource: resource id
 * @access: access type
 * @sdp_number: resource number
 * @timeout: the maximum time in ms that the driver may hold the resource
 * @cmd_details: pointer to command details structure or NULL
 *
 * requests common resource using the admin queue commands
 **/
i40e_status i40e_aq_request_resource(struct i40e_hw *hw,
				enum i40e_aq_resources_ids resource,
				enum i40e_aq_resource_access_type access,
				u8 sdp_number, u64 *timeout,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_request_resource *cmd_resp =
		(struct i40e_aqc_request_resource *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_request_resource);

	cmd_resp->resource_id = cpu_to_le16(resource);
	cmd_resp->access_type = cpu_to_le16(access);
	cmd_resp->resource_number = cpu_to_le32(sdp_number);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);
	/* The completion specifies the maximum time in ms that the driver
	 * may hold the resource in the Timeout field.
	 * If the resource is held by someone else, the command completes with
	 * busy return value and the timeout field indicates the maximum time
	 * the current owner of the resource has to free it.
	 */
	if (!status || hw->aq.asq_last_status == I40E_AQ_RC_EBUSY)
		*timeout = le32_to_cpu(cmd_resp->timeout);

	return status;
}

/**
 * i40e_aq_release_resource
 * @hw: pointer to the hw struct
 * @resource: resource id
 * @sdp_number: resource number
 * @cmd_details: pointer to command details structure or NULL
 *
 * release common resource using the admin queue commands
 **/
i40e_status i40e_aq_release_resource(struct i40e_hw *hw,
				enum i40e_aq_resources_ids resource,
				u8 sdp_number,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_request_resource *cmd =
		(struct i40e_aqc_request_resource *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_release_resource);

	cmd->resource_id = cpu_to_le16(resource);
	cmd->resource_number = cpu_to_le32(sdp_number);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_aq_read_nvm
 * @hw: pointer to the hw struct
 * @module_pointer: module pointer location in words from the NVM beginning
 * @offset: byte offset from the module beginning
 * @length: length of the section to be read (in bytes from the offset)
 * @data: command buffer (size [bytes] = length)
 * @last_command: tells if this is the last command in a series
 * @cmd_details: pointer to command details structure or NULL
 *
 * Read the NVM using the admin queue commands
 **/
i40e_status i40e_aq_read_nvm(struct i40e_hw *hw, u8 module_pointer,
				u32 offset, u16 length, void *data,
				bool last_command,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_nvm_update *cmd =
		(struct i40e_aqc_nvm_update *)&desc.params.raw;
	i40e_status status;

	/* In offset the highest byte must be zeroed. */
	if (offset & 0xFF000000) {
		status = I40E_ERR_PARAM;
		goto i40e_aq_read_nvm_exit;
	}

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_nvm_read);

	/* If this is the last command in a series, set the proper flag. */
	if (last_command)
		cmd->command_flags |= I40E_AQ_NVM_LAST_CMD;
	cmd->module_pointer = module_pointer;
	cmd->offset = cpu_to_le32(offset);
	cmd->length = cpu_to_le16(length);

	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_BUF);
	if (length > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);

	status = i40e_asq_send_command(hw, &desc, data, length, cmd_details);

i40e_aq_read_nvm_exit:
	return status;
}

#define I40E_DEV_FUNC_CAP_SWITCH_MODE	0x01
#define I40E_DEV_FUNC_CAP_MGMT_MODE	0x02
#define I40E_DEV_FUNC_CAP_NPAR		0x03
#define I40E_DEV_FUNC_CAP_OS2BMC	0x04
#define I40E_DEV_FUNC_CAP_VALID_FUNC	0x05
#define I40E_DEV_FUNC_CAP_SRIOV_1_1	0x12
#define I40E_DEV_FUNC_CAP_VF		0x13
#define I40E_DEV_FUNC_CAP_VMDQ		0x14
#define I40E_DEV_FUNC_CAP_802_1_QBG	0x15
#define I40E_DEV_FUNC_CAP_802_1_QBH	0x16
#define I40E_DEV_FUNC_CAP_VSI		0x17
#define I40E_DEV_FUNC_CAP_DCB		0x18
#define I40E_DEV_FUNC_CAP_FCOE		0x21
#define I40E_DEV_FUNC_CAP_RSS		0x40
#define I40E_DEV_FUNC_CAP_RX_QUEUES	0x41
#define I40E_DEV_FUNC_CAP_TX_QUEUES	0x42
#define I40E_DEV_FUNC_CAP_MSIX		0x43
#define I40E_DEV_FUNC_CAP_MSIX_VF	0x44
#define I40E_DEV_FUNC_CAP_FLOW_DIRECTOR	0x45
#define I40E_DEV_FUNC_CAP_IEEE_1588	0x46
#define I40E_DEV_FUNC_CAP_MFP_MODE_1	0xF1
#define I40E_DEV_FUNC_CAP_CEM		0xF2
#define I40E_DEV_FUNC_CAP_IWARP		0x51
#define I40E_DEV_FUNC_CAP_LED		0x61
#define I40E_DEV_FUNC_CAP_SDP		0x62
#define I40E_DEV_FUNC_CAP_MDIO		0x63

/**
 * i40e_parse_discover_capabilities
 * @hw: pointer to the hw struct
 * @buff: pointer to a buffer containing device/function capability records
 * @cap_count: number of capability records in the list
 * @list_type_opc: type of capabilities list to parse
 *
 * Parse the device/function capabilities list.
 **/
static void i40e_parse_discover_capabilities(struct i40e_hw *hw, void *buff,
				     u32 cap_count,
				     enum i40e_admin_queue_opc list_type_opc)
{
	struct i40e_aqc_list_capabilities_element_resp *cap;
	u32 number, logical_id, phys_id;
	struct i40e_hw_capabilities *p;
	u32 reg_val;
	u32 i = 0;
	u16 id;

	cap = (struct i40e_aqc_list_capabilities_element_resp *) buff;

	if (list_type_opc == i40e_aqc_opc_list_dev_capabilities)
		p = (struct i40e_hw_capabilities *)&hw->dev_caps;
	else if (list_type_opc == i40e_aqc_opc_list_func_capabilities)
		p = (struct i40e_hw_capabilities *)&hw->func_caps;
	else
		return;

	for (i = 0; i < cap_count; i++, cap++) {
		id = le16_to_cpu(cap->id);
		number = le32_to_cpu(cap->number);
		logical_id = le32_to_cpu(cap->logical_id);
		phys_id = le32_to_cpu(cap->phys_id);

		switch (id) {
		case I40E_DEV_FUNC_CAP_SWITCH_MODE:
			p->switch_mode = number;
			break;
		case I40E_DEV_FUNC_CAP_MGMT_MODE:
			p->management_mode = number;
			break;
		case I40E_DEV_FUNC_CAP_NPAR:
			p->npar_enable = number;
			break;
		case I40E_DEV_FUNC_CAP_OS2BMC:
			p->os2bmc = number;
			break;
		case I40E_DEV_FUNC_CAP_VALID_FUNC:
			p->valid_functions = number;
			break;
		case I40E_DEV_FUNC_CAP_SRIOV_1_1:
			if (number == 1)
				p->sr_iov_1_1 = true;
			break;
		case I40E_DEV_FUNC_CAP_VF:
			p->num_vfs = number;
			p->vf_base_id = logical_id;
			break;
		case I40E_DEV_FUNC_CAP_VMDQ:
			if (number == 1)
				p->vmdq = true;
			break;
		case I40E_DEV_FUNC_CAP_802_1_QBG:
			if (number == 1)
				p->evb_802_1_qbg = true;
			break;
		case I40E_DEV_FUNC_CAP_802_1_QBH:
			if (number == 1)
				p->evb_802_1_qbh = true;
			break;
		case I40E_DEV_FUNC_CAP_VSI:
			p->num_vsis = number;
			break;
		case I40E_DEV_FUNC_CAP_DCB:
			if (number == 1) {
				p->dcb = true;
				p->enabled_tcmap = logical_id;
				p->maxtc = phys_id;
			}
			break;
		case I40E_DEV_FUNC_CAP_FCOE:
			if (number == 1)
				p->fcoe = true;
			break;
		case I40E_DEV_FUNC_CAP_RSS:
			p->rss = true;
			reg_val = rd32(hw, I40E_PFQF_CTL_0);
			if (reg_val & I40E_PFQF_CTL_0_HASHLUTSIZE_MASK)
				p->rss_table_size = number;
			else
				p->rss_table_size = 128;
			p->rss_table_entry_width = logical_id;
			break;
		case I40E_DEV_FUNC_CAP_RX_QUEUES:
			p->num_rx_qp = number;
			p->base_queue = phys_id;
			break;
		case I40E_DEV_FUNC_CAP_TX_QUEUES:
			p->num_tx_qp = number;
			p->base_queue = phys_id;
			break;
		case I40E_DEV_FUNC_CAP_MSIX:
			p->num_msix_vectors = number;
			break;
		case I40E_DEV_FUNC_CAP_MSIX_VF:
			p->num_msix_vectors_vf = number;
			break;
		case I40E_DEV_FUNC_CAP_MFP_MODE_1:
			if (number == 1)
				p->mfp_mode_1 = true;
			break;
		case I40E_DEV_FUNC_CAP_CEM:
			if (number == 1)
				p->mgmt_cem = true;
			break;
		case I40E_DEV_FUNC_CAP_IWARP:
			if (number == 1)
				p->iwarp = true;
			break;
		case I40E_DEV_FUNC_CAP_LED:
			if (phys_id < I40E_HW_CAP_MAX_GPIO)
				p->led[phys_id] = true;
			break;
		case I40E_DEV_FUNC_CAP_SDP:
			if (phys_id < I40E_HW_CAP_MAX_GPIO)
				p->sdp[phys_id] = true;
			break;
		case I40E_DEV_FUNC_CAP_MDIO:
			if (number == 1) {
				p->mdio_port_num = phys_id;
				p->mdio_port_mode = logical_id;
			}
			break;
		case I40E_DEV_FUNC_CAP_IEEE_1588:
			if (number == 1)
				p->ieee_1588 = true;
			break;
		case I40E_DEV_FUNC_CAP_FLOW_DIRECTOR:
			p->fd = true;
			p->fd_filters_guaranteed = number;
			p->fd_filters_best_effort = logical_id;
			break;
		default:
			break;
		}
	}

	/* additional HW specific goodies that might
	 * someday be HW version specific
	 */
	p->rx_buf_chain_len = I40E_MAX_CHAINED_RX_BUFFERS;
}

/**
 * i40e_aq_discover_capabilities
 * @hw: pointer to the hw struct
 * @buff: a virtual buffer to hold the capabilities
 * @buff_size: Size of the virtual buffer
 * @data_size: Size of the returned data, or buff size needed if AQ err==ENOMEM
 * @list_type_opc: capabilities type to discover - pass in the command opcode
 * @cmd_details: pointer to command details structure or NULL
 *
 * Get the device capabilities descriptions from the firmware
 **/
i40e_status i40e_aq_discover_capabilities(struct i40e_hw *hw,
				void *buff, u16 buff_size, u16 *data_size,
				enum i40e_admin_queue_opc list_type_opc,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aqc_list_capabilites *cmd;
	i40e_status status = 0;
	struct i40e_aq_desc desc;

	cmd = (struct i40e_aqc_list_capabilites *)&desc.params.raw;

	if (list_type_opc != i40e_aqc_opc_list_func_capabilities &&
		list_type_opc != i40e_aqc_opc_list_dev_capabilities) {
		status = I40E_ERR_PARAM;
		goto exit;
	}

	i40e_fill_default_direct_cmd_desc(&desc, list_type_opc);

	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_BUF);
	if (buff_size > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);

	status = i40e_asq_send_command(hw, &desc, buff, buff_size, cmd_details);
	*data_size = le16_to_cpu(desc.datalen);

	if (status)
		goto exit;

	i40e_parse_discover_capabilities(hw, buff, le32_to_cpu(cmd->count),
					 list_type_opc);

exit:
	return status;
}

/**
 * i40e_aq_get_lldp_mib
 * @hw: pointer to the hw struct
 * @bridge_type: type of bridge requested
 * @mib_type: Local, Remote or both Local and Remote MIBs
 * @buff: pointer to a user supplied buffer to store the MIB block
 * @buff_size: size of the buffer (in bytes)
 * @local_len : length of the returned Local LLDP MIB
 * @remote_len: length of the returned Remote LLDP MIB
 * @cmd_details: pointer to command details structure or NULL
 *
 * Requests the complete LLDP MIB (entire packet).
 **/
i40e_status i40e_aq_get_lldp_mib(struct i40e_hw *hw, u8 bridge_type,
				u8 mib_type, void *buff, u16 buff_size,
				u16 *local_len, u16 *remote_len,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_lldp_get_mib *cmd =
		(struct i40e_aqc_lldp_get_mib *)&desc.params.raw;
	struct i40e_aqc_lldp_get_mib *resp =
		(struct i40e_aqc_lldp_get_mib *)&desc.params.raw;
	i40e_status status;

	if (buff_size == 0 || !buff)
		return I40E_ERR_PARAM;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_lldp_get_mib);
	/* Indirect Command */
	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_BUF);

	cmd->type = mib_type & I40E_AQ_LLDP_MIB_TYPE_MASK;
	cmd->type |= ((bridge_type << I40E_AQ_LLDP_BRIDGE_TYPE_SHIFT) &
		       I40E_AQ_LLDP_BRIDGE_TYPE_MASK);

	desc.datalen = cpu_to_le16(buff_size);

	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_BUF);
	if (buff_size > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);

	status = i40e_asq_send_command(hw, &desc, buff, buff_size, cmd_details);
	if (!status) {
		if (local_len != NULL)
			*local_len = le16_to_cpu(resp->local_len);
		if (remote_len != NULL)
			*remote_len = le16_to_cpu(resp->remote_len);
	}

	return status;
}

/**
 * i40e_aq_cfg_lldp_mib_change_event
 * @hw: pointer to the hw struct
 * @enable_update: Enable or Disable event posting
 * @cmd_details: pointer to command details structure or NULL
 *
 * Enable or Disable posting of an event on ARQ when LLDP MIB
 * associated with the interface changes
 **/
i40e_status i40e_aq_cfg_lldp_mib_change_event(struct i40e_hw *hw,
				bool enable_update,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_lldp_update_mib *cmd =
		(struct i40e_aqc_lldp_update_mib *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_lldp_update_mib);

	if (!enable_update)
		cmd->command |= I40E_AQ_LLDP_MIB_UPDATE_DISABLE;

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_aq_stop_lldp
 * @hw: pointer to the hw struct
 * @shutdown_agent: True if LLDP Agent needs to be Shutdown
 * @cmd_details: pointer to command details structure or NULL
 *
 * Stop or Shutdown the embedded LLDP Agent
 **/
i40e_status i40e_aq_stop_lldp(struct i40e_hw *hw, bool shutdown_agent,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_lldp_stop *cmd =
		(struct i40e_aqc_lldp_stop *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_lldp_stop);

	if (shutdown_agent)
		cmd->command |= I40E_AQ_LLDP_AGENT_SHUTDOWN;

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_aq_start_lldp
 * @hw: pointer to the hw struct
 * @cmd_details: pointer to command details structure or NULL
 *
 * Start the embedded LLDP Agent on all ports.
 **/
i40e_status i40e_aq_start_lldp(struct i40e_hw *hw,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_lldp_start *cmd =
		(struct i40e_aqc_lldp_start *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_lldp_start);

	cmd->command = I40E_AQ_LLDP_AGENT_START;

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_aq_delete_element - Delete switch element
 * @hw: pointer to the hw struct
 * @seid: the SEID to delete from the switch
 * @cmd_details: pointer to command details structure or NULL
 *
 * This deletes a switch element from the switch.
 **/
i40e_status i40e_aq_delete_element(struct i40e_hw *hw, u16 seid,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_switch_seid *cmd =
		(struct i40e_aqc_switch_seid *)&desc.params.raw;
	i40e_status status;

	if (seid == 0)
		return I40E_ERR_PARAM;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_delete_element);

	cmd->seid = cpu_to_le16(seid);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_aq_tx_sched_cmd - generic Tx scheduler AQ command handler
 * @hw: pointer to the hw struct
 * @seid: seid for the physical port/switching component/vsi
 * @buff: Indirect buffer to hold data parameters and response
 * @buff_size: Indirect buffer size
 * @opcode: Tx scheduler AQ command opcode
 * @cmd_details: pointer to command details structure or NULL
 *
 * Generic command handler for Tx scheduler AQ commands
 **/
static i40e_status i40e_aq_tx_sched_cmd(struct i40e_hw *hw, u16 seid,
				void *buff, u16 buff_size,
				 enum i40e_admin_queue_opc opcode,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_tx_sched_ind *cmd =
		(struct i40e_aqc_tx_sched_ind *)&desc.params.raw;
	i40e_status status;
	bool cmd_param_flag = false;

	switch (opcode) {
	case i40e_aqc_opc_configure_vsi_ets_sla_bw_limit:
	case i40e_aqc_opc_configure_vsi_tc_bw:
	case i40e_aqc_opc_enable_switching_comp_ets:
	case i40e_aqc_opc_modify_switching_comp_ets:
	case i40e_aqc_opc_disable_switching_comp_ets:
	case i40e_aqc_opc_configure_switching_comp_ets_bw_limit:
	case i40e_aqc_opc_configure_switching_comp_bw_config:
		cmd_param_flag = true;
		break;
	case i40e_aqc_opc_query_vsi_bw_config:
	case i40e_aqc_opc_query_vsi_ets_sla_config:
	case i40e_aqc_opc_query_switching_comp_ets_config:
	case i40e_aqc_opc_query_port_ets_config:
	case i40e_aqc_opc_query_switching_comp_bw_config:
		cmd_param_flag = false;
		break;
	default:
		return I40E_ERR_PARAM;
	}

	i40e_fill_default_direct_cmd_desc(&desc, opcode);

	/* Indirect command */
	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_BUF);
	if (cmd_param_flag)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_RD);
	if (buff_size > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);

	desc.datalen = cpu_to_le16(buff_size);

	cmd->vsi_seid = cpu_to_le16(seid);

	status = i40e_asq_send_command(hw, &desc, buff, buff_size, cmd_details);

	return status;
}

/**
 * i40e_aq_config_vsi_tc_bw - Config VSI BW Allocation per TC
 * @hw: pointer to the hw struct
 * @seid: VSI seid
 * @bw_data: Buffer holding enabled TCs, relative TC BW limit/credits
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_config_vsi_tc_bw(struct i40e_hw *hw,
			u16 seid,
			struct i40e_aqc_configure_vsi_tc_bw_data *bw_data,
			struct i40e_asq_cmd_details *cmd_details)
{
	return i40e_aq_tx_sched_cmd(hw, seid, (void *)bw_data, sizeof(*bw_data),
				    i40e_aqc_opc_configure_vsi_tc_bw,
				    cmd_details);
}

/**
 * i40e_aq_query_vsi_bw_config - Query VSI BW configuration
 * @hw: pointer to the hw struct
 * @seid: seid of the VSI
 * @bw_data: Buffer to hold VSI BW configuration
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_query_vsi_bw_config(struct i40e_hw *hw,
			u16 seid,
			struct i40e_aqc_query_vsi_bw_config_resp *bw_data,
			struct i40e_asq_cmd_details *cmd_details)
{
	return i40e_aq_tx_sched_cmd(hw, seid, (void *)bw_data, sizeof(*bw_data),
				    i40e_aqc_opc_query_vsi_bw_config,
				    cmd_details);
}

/**
 * i40e_aq_query_vsi_ets_sla_config - Query VSI BW configuration per TC
 * @hw: pointer to the hw struct
 * @seid: seid of the VSI
 * @bw_data: Buffer to hold VSI BW configuration per TC
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_query_vsi_ets_sla_config(struct i40e_hw *hw,
			u16 seid,
			struct i40e_aqc_query_vsi_ets_sla_config_resp *bw_data,
			struct i40e_asq_cmd_details *cmd_details)
{
	return i40e_aq_tx_sched_cmd(hw, seid, (void *)bw_data, sizeof(*bw_data),
				    i40e_aqc_opc_query_vsi_ets_sla_config,
				    cmd_details);
}

/**
 * i40e_aq_query_switch_comp_ets_config - Query Switch comp BW config per TC
 * @hw: pointer to the hw struct
 * @seid: seid of the switching component
 * @bw_data: Buffer to hold switching component's per TC BW config
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_query_switch_comp_ets_config(struct i40e_hw *hw,
		u16 seid,
		struct i40e_aqc_query_switching_comp_ets_config_resp *bw_data,
		struct i40e_asq_cmd_details *cmd_details)
{
	return i40e_aq_tx_sched_cmd(hw, seid, (void *)bw_data, sizeof(*bw_data),
				   i40e_aqc_opc_query_switching_comp_ets_config,
				   cmd_details);
}

/**
 * i40e_aq_query_port_ets_config - Query Physical Port ETS configuration
 * @hw: pointer to the hw struct
 * @seid: seid of the VSI or switching component connected to Physical Port
 * @bw_data: Buffer to hold current ETS configuration for the Physical Port
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_query_port_ets_config(struct i40e_hw *hw,
			u16 seid,
			struct i40e_aqc_query_port_ets_config_resp *bw_data,
			struct i40e_asq_cmd_details *cmd_details)
{
	return i40e_aq_tx_sched_cmd(hw, seid, (void *)bw_data, sizeof(*bw_data),
				    i40e_aqc_opc_query_port_ets_config,
				    cmd_details);
}

/**
 * i40e_aq_query_switch_comp_bw_config - Query Switch comp BW configuration
 * @hw: pointer to the hw struct
 * @seid: seid of the switching component
 * @bw_data: Buffer to hold switching component's BW configuration
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_query_switch_comp_bw_config(struct i40e_hw *hw,
		u16 seid,
		struct i40e_aqc_query_switching_comp_bw_config_resp *bw_data,
		struct i40e_asq_cmd_details *cmd_details)
{
	return i40e_aq_tx_sched_cmd(hw, seid, (void *)bw_data, sizeof(*bw_data),
				    i40e_aqc_opc_query_switching_comp_bw_config,
				    cmd_details);
}

/**
 * i40e_validate_filter_settings
 * @hw: pointer to the hardware structure
 * @settings: Filter control settings
 *
 * Check and validate the filter control settings passed.
 * The function checks for the valid filter/context sizes being
 * passed for FCoE and PE.
 *
 * Returns 0 if the values passed are valid and within
 * range else returns an error.
 **/
static i40e_status i40e_validate_filter_settings(struct i40e_hw *hw,
				struct i40e_filter_control_settings *settings)
{
	u32 fcoe_cntx_size, fcoe_filt_size;
	u32 pe_cntx_size, pe_filt_size;
	u32 fcoe_fmax, pe_fmax;
	u32 val;

	/* Validate FCoE settings passed */
	switch (settings->fcoe_filt_num) {
	case I40E_HASH_FILTER_SIZE_1K:
	case I40E_HASH_FILTER_SIZE_2K:
	case I40E_HASH_FILTER_SIZE_4K:
	case I40E_HASH_FILTER_SIZE_8K:
	case I40E_HASH_FILTER_SIZE_16K:
	case I40E_HASH_FILTER_SIZE_32K:
		fcoe_filt_size = I40E_HASH_FILTER_BASE_SIZE;
		fcoe_filt_size <<= (u32)settings->fcoe_filt_num;
		break;
	default:
		return I40E_ERR_PARAM;
	}

	switch (settings->fcoe_cntx_num) {
	case I40E_DMA_CNTX_SIZE_512:
	case I40E_DMA_CNTX_SIZE_1K:
	case I40E_DMA_CNTX_SIZE_2K:
	case I40E_DMA_CNTX_SIZE_4K:
		fcoe_cntx_size = I40E_DMA_CNTX_BASE_SIZE;
		fcoe_cntx_size <<= (u32)settings->fcoe_cntx_num;
		break;
	default:
		return I40E_ERR_PARAM;
	}

	/* Validate PE settings passed */
	switch (settings->pe_filt_num) {
	case I40E_HASH_FILTER_SIZE_1K:
	case I40E_HASH_FILTER_SIZE_2K:
	case I40E_HASH_FILTER_SIZE_4K:
	case I40E_HASH_FILTER_SIZE_8K:
	case I40E_HASH_FILTER_SIZE_16K:
	case I40E_HASH_FILTER_SIZE_32K:
	case I40E_HASH_FILTER_SIZE_64K:
	case I40E_HASH_FILTER_SIZE_128K:
	case I40E_HASH_FILTER_SIZE_256K:
	case I40E_HASH_FILTER_SIZE_512K:
	case I40E_HASH_FILTER_SIZE_1M:
		pe_filt_size = I40E_HASH_FILTER_BASE_SIZE;
		pe_filt_size <<= (u32)settings->pe_filt_num;
		break;
	default:
		return I40E_ERR_PARAM;
	}

	switch (settings->pe_cntx_num) {
	case I40E_DMA_CNTX_SIZE_512:
	case I40E_DMA_CNTX_SIZE_1K:
	case I40E_DMA_CNTX_SIZE_2K:
	case I40E_DMA_CNTX_SIZE_4K:
	case I40E_DMA_CNTX_SIZE_8K:
	case I40E_DMA_CNTX_SIZE_16K:
	case I40E_DMA_CNTX_SIZE_32K:
	case I40E_DMA_CNTX_SIZE_64K:
	case I40E_DMA_CNTX_SIZE_128K:
	case I40E_DMA_CNTX_SIZE_256K:
		pe_cntx_size = I40E_DMA_CNTX_BASE_SIZE;
		pe_cntx_size <<= (u32)settings->pe_cntx_num;
		break;
	default:
		return I40E_ERR_PARAM;
	}

	/* FCHSIZE + FCDSIZE should not be greater than PMFCOEFMAX */
	val = rd32(hw, I40E_GLHMC_FCOEFMAX);
	fcoe_fmax = (val & I40E_GLHMC_FCOEFMAX_PMFCOEFMAX_MASK)
		     >> I40E_GLHMC_FCOEFMAX_PMFCOEFMAX_SHIFT;
	if (fcoe_filt_size + fcoe_cntx_size >  fcoe_fmax)
		return I40E_ERR_INVALID_SIZE;

	/* PEHSIZE + PEDSIZE should not be greater than PMPEXFMAX */
	val = rd32(hw, I40E_GLHMC_PEXFMAX);
	pe_fmax = (val & I40E_GLHMC_PEXFMAX_PMPEXFMAX_MASK)
		   >> I40E_GLHMC_PEXFMAX_PMPEXFMAX_SHIFT;
	if (pe_filt_size + pe_cntx_size >  pe_fmax)
		return I40E_ERR_INVALID_SIZE;

	return 0;
}

/**
 * i40e_set_filter_control
 * @hw: pointer to the hardware structure
 * @settings: Filter control settings
 *
 * Set the Queue Filters for PE/FCoE and enable filters required
 * for a single PF. It is expected that these settings are programmed
 * at the driver initialization time.
 **/
i40e_status i40e_set_filter_control(struct i40e_hw *hw,
				struct i40e_filter_control_settings *settings)
{
	i40e_status ret = 0;
	u32 hash_lut_size = 0;
	u32 val;

	if (!settings)
		return I40E_ERR_PARAM;

	/* Validate the input settings */
	ret = i40e_validate_filter_settings(hw, settings);
	if (ret)
		return ret;

	/* Read the PF Queue Filter control register */
	val = rd32(hw, I40E_PFQF_CTL_0);

	/* Program required PE hash buckets for the PF */
	val &= ~I40E_PFQF_CTL_0_PEHSIZE_MASK;
	val |= ((u32)settings->pe_filt_num << I40E_PFQF_CTL_0_PEHSIZE_SHIFT) &
		I40E_PFQF_CTL_0_PEHSIZE_MASK;
	/* Program required PE contexts for the PF */
	val &= ~I40E_PFQF_CTL_0_PEDSIZE_MASK;
	val |= ((u32)settings->pe_cntx_num << I40E_PFQF_CTL_0_PEDSIZE_SHIFT) &
		I40E_PFQF_CTL_0_PEDSIZE_MASK;

	/* Program required FCoE hash buckets for the PF */
	val &= ~I40E_PFQF_CTL_0_PFFCHSIZE_MASK;
	val |= ((u32)settings->fcoe_filt_num <<
			I40E_PFQF_CTL_0_PFFCHSIZE_SHIFT) &
		I40E_PFQF_CTL_0_PFFCHSIZE_MASK;
	/* Program required FCoE DDP contexts for the PF */
	val &= ~I40E_PFQF_CTL_0_PFFCDSIZE_MASK;
	val |= ((u32)settings->fcoe_cntx_num <<
			I40E_PFQF_CTL_0_PFFCDSIZE_SHIFT) &
		I40E_PFQF_CTL_0_PFFCDSIZE_MASK;

	/* Program Hash LUT size for the PF */
	val &= ~I40E_PFQF_CTL_0_HASHLUTSIZE_MASK;
	if (settings->hash_lut_size == I40E_HASH_LUT_SIZE_512)
		hash_lut_size = 1;
	val |= (hash_lut_size << I40E_PFQF_CTL_0_HASHLUTSIZE_SHIFT) &
		I40E_PFQF_CTL_0_HASHLUTSIZE_MASK;

	/* Enable FDIR, Ethertype and MACVLAN filters for PF and VFs */
	if (settings->enable_fdir)
		val |= I40E_PFQF_CTL_0_FD_ENA_MASK;
	if (settings->enable_ethtype)
		val |= I40E_PFQF_CTL_0_ETYPE_ENA_MASK;
	if (settings->enable_macvlan)
		val |= I40E_PFQF_CTL_0_MACVLAN_ENA_MASK;

	wr32(hw, I40E_PFQF_CTL_0, val);

	return 0;
}
