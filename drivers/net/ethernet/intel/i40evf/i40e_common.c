/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Virtual Function Driver
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
i40e_status i40e_set_mac_type(struct i40e_hw *hw)
{
	i40e_status status = 0;

	if (hw->vendor_id == PCI_VENDOR_ID_INTEL) {
		switch (hw->device_id) {
		case I40E_DEV_ID_SFP_XL710:
		case I40E_DEV_ID_SFP_X710:
		case I40E_DEV_ID_QEMU:
		case I40E_DEV_ID_KX_A:
		case I40E_DEV_ID_KX_B:
		case I40E_DEV_ID_KX_C:
		case I40E_DEV_ID_KX_D:
		case I40E_DEV_ID_QSFP_A:
		case I40E_DEV_ID_QSFP_B:
		case I40E_DEV_ID_QSFP_C:
			hw->mac.type = I40E_MAC_XL710;
			break;
		case I40E_DEV_ID_VF:
		case I40E_DEV_ID_VF_HV:
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
 * i40evf_debug_aq
 * @hw: debug mask related to admin queue
 * @mask: debug mask
 * @desc: pointer to admin queue descriptor
 * @buffer: pointer to command buffer
 *
 * Dumps debug log about adminq command with descriptor contents.
 **/
void i40evf_debug_aq(struct i40e_hw *hw, enum i40e_debug_mask mask, void *desc,
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
 * i40evf_check_asq_alive
 * @hw: pointer to the hw struct
 *
 * Returns true if Queue is enabled else false.
 **/
bool i40evf_check_asq_alive(struct i40e_hw *hw)
{
	return !!(rd32(hw, hw->aq.asq.len) & I40E_PF_ATQLEN_ATQENABLE_MASK);
}

/**
 * i40evf_aq_queue_shutdown
 * @hw: pointer to the hw struct
 * @unloading: is the driver unloading itself
 *
 * Tell the Firmware that we're shutting down the AdminQ and whether
 * or not the driver is unloading as well.
 **/
i40e_status i40evf_aq_queue_shutdown(struct i40e_hw *hw,
					     bool unloading)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_queue_shutdown *cmd =
		(struct i40e_aqc_queue_shutdown *)&desc.params.raw;
	i40e_status status;

	i40evf_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_queue_shutdown);

	if (unloading)
		cmd->driver_unloading = cpu_to_le32(I40E_AQ_DRIVER_UNLOADING);
	status = i40evf_asq_send_command(hw, &desc, NULL, 0, NULL);

	return status;
}


/**
 * i40e_aq_send_msg_to_pf
 * @hw: pointer to the hardware structure
 * @v_opcode: opcodes for VF-PF communication
 * @v_retval: return error code
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 * @cmd_details: pointer to command details
 *
 * Send message to PF driver using admin queue. By default, this message
 * is sent asynchronously, i.e. i40evf_asq_send_command() does not wait for
 * completion before returning.
 **/
i40e_status i40e_aq_send_msg_to_pf(struct i40e_hw *hw,
				enum i40e_virtchnl_ops v_opcode,
				i40e_status v_retval,
				u8 *msg, u16 msglen,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	i40e_status status;

	i40evf_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_send_msg_to_pf);
	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_SI);
	desc.cookie_high = cpu_to_le32(v_opcode);
	desc.cookie_low = cpu_to_le32(v_retval);
	if (msglen) {
		desc.flags |= cpu_to_le16((u16)(I40E_AQ_FLAG_BUF
						| I40E_AQ_FLAG_RD));
		if (msglen > I40E_AQ_LARGE_BUF)
			desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);
		desc.datalen = cpu_to_le16(msglen);
	}
	if (!cmd_details) {
		struct i40e_asq_cmd_details details;
		memset(&details, 0, sizeof(details));
		details.async = true;
		cmd_details = &details;
	}
	status = i40evf_asq_send_command(hw, (struct i40e_aq_desc *)&desc, msg,
				       msglen, cmd_details);
	return status;
}

/**
 * i40e_vf_parse_hw_config
 * @hw: pointer to the hardware structure
 * @msg: pointer to the virtual channel VF resource structure
 *
 * Given a VF resource message from the PF, populate the hw struct
 * with appropriate information.
 **/
void i40e_vf_parse_hw_config(struct i40e_hw *hw,
			     struct i40e_virtchnl_vf_resource *msg)
{
	struct i40e_virtchnl_vsi_resource *vsi_res;
	int i;

	vsi_res = &msg->vsi_res[0];

	hw->dev_caps.num_vsis = msg->num_vsis;
	hw->dev_caps.num_rx_qp = msg->num_queue_pairs;
	hw->dev_caps.num_tx_qp = msg->num_queue_pairs;
	hw->dev_caps.num_msix_vectors_vf = msg->max_vectors;
	hw->dev_caps.dcb = msg->vf_offload_flags &
			   I40E_VIRTCHNL_VF_OFFLOAD_L2;
	hw->dev_caps.fcoe = (msg->vf_offload_flags &
			     I40E_VIRTCHNL_VF_OFFLOAD_FCOE) ? 1 : 0;
	for (i = 0; i < msg->num_vsis; i++) {
		if (vsi_res->vsi_type == I40E_VSI_SRIOV) {
			memcpy(hw->mac.perm_addr, vsi_res->default_mac_addr,
			       ETH_ALEN);
			memcpy(hw->mac.addr, vsi_res->default_mac_addr,
			       ETH_ALEN);
		}
		vsi_res++;
	}
}

/**
 * i40e_vf_reset
 * @hw: pointer to the hardware structure
 *
 * Send a VF_RESET message to the PF. Does not wait for response from PF
 * as none will be forthcoming. Immediately after calling this function,
 * the admin queue should be shut down and (optionally) reinitialized.
 **/
i40e_status i40e_vf_reset(struct i40e_hw *hw)
{
	return i40e_aq_send_msg_to_pf(hw, I40E_VIRTCHNL_OP_RESET_VF,
				      0, NULL, 0, NULL);
}
