/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 - 2016 Intel Corporation.
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
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
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
		case I40E_DEV_ID_SFP_XL710:
		case I40E_DEV_ID_QEMU:
		case I40E_DEV_ID_KX_B:
		case I40E_DEV_ID_KX_C:
		case I40E_DEV_ID_QSFP_A:
		case I40E_DEV_ID_QSFP_B:
		case I40E_DEV_ID_QSFP_C:
		case I40E_DEV_ID_10G_BASE_T:
		case I40E_DEV_ID_10G_BASE_T4:
		case I40E_DEV_ID_20G_KR2:
		case I40E_DEV_ID_20G_KR2_A:
			hw->mac.type = I40E_MAC_XL710;
			break;
		case I40E_DEV_ID_KX_X722:
		case I40E_DEV_ID_QSFP_X722:
		case I40E_DEV_ID_SFP_X722:
		case I40E_DEV_ID_1G_BASE_T_X722:
		case I40E_DEV_ID_10G_BASE_T_X722:
		case I40E_DEV_ID_SFP_I_X722:
			hw->mac.type = I40E_MAC_X722;
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
 * i40e_aq_str - convert AQ err code to a string
 * @hw: pointer to the HW structure
 * @aq_err: the AQ error code to convert
 **/
const char *i40e_aq_str(struct i40e_hw *hw, enum i40e_admin_queue_err aq_err)
{
	switch (aq_err) {
	case I40E_AQ_RC_OK:
		return "OK";
	case I40E_AQ_RC_EPERM:
		return "I40E_AQ_RC_EPERM";
	case I40E_AQ_RC_ENOENT:
		return "I40E_AQ_RC_ENOENT";
	case I40E_AQ_RC_ESRCH:
		return "I40E_AQ_RC_ESRCH";
	case I40E_AQ_RC_EINTR:
		return "I40E_AQ_RC_EINTR";
	case I40E_AQ_RC_EIO:
		return "I40E_AQ_RC_EIO";
	case I40E_AQ_RC_ENXIO:
		return "I40E_AQ_RC_ENXIO";
	case I40E_AQ_RC_E2BIG:
		return "I40E_AQ_RC_E2BIG";
	case I40E_AQ_RC_EAGAIN:
		return "I40E_AQ_RC_EAGAIN";
	case I40E_AQ_RC_ENOMEM:
		return "I40E_AQ_RC_ENOMEM";
	case I40E_AQ_RC_EACCES:
		return "I40E_AQ_RC_EACCES";
	case I40E_AQ_RC_EFAULT:
		return "I40E_AQ_RC_EFAULT";
	case I40E_AQ_RC_EBUSY:
		return "I40E_AQ_RC_EBUSY";
	case I40E_AQ_RC_EEXIST:
		return "I40E_AQ_RC_EEXIST";
	case I40E_AQ_RC_EINVAL:
		return "I40E_AQ_RC_EINVAL";
	case I40E_AQ_RC_ENOTTY:
		return "I40E_AQ_RC_ENOTTY";
	case I40E_AQ_RC_ENOSPC:
		return "I40E_AQ_RC_ENOSPC";
	case I40E_AQ_RC_ENOSYS:
		return "I40E_AQ_RC_ENOSYS";
	case I40E_AQ_RC_ERANGE:
		return "I40E_AQ_RC_ERANGE";
	case I40E_AQ_RC_EFLUSHED:
		return "I40E_AQ_RC_EFLUSHED";
	case I40E_AQ_RC_BAD_ADDR:
		return "I40E_AQ_RC_BAD_ADDR";
	case I40E_AQ_RC_EMODE:
		return "I40E_AQ_RC_EMODE";
	case I40E_AQ_RC_EFBIG:
		return "I40E_AQ_RC_EFBIG";
	}

	snprintf(hw->err_str, sizeof(hw->err_str), "%d", aq_err);
	return hw->err_str;
}

/**
 * i40e_stat_str - convert status err code to a string
 * @hw: pointer to the HW structure
 * @stat_err: the status error code to convert
 **/
const char *i40e_stat_str(struct i40e_hw *hw, i40e_status stat_err)
{
	switch (stat_err) {
	case 0:
		return "OK";
	case I40E_ERR_NVM:
		return "I40E_ERR_NVM";
	case I40E_ERR_NVM_CHECKSUM:
		return "I40E_ERR_NVM_CHECKSUM";
	case I40E_ERR_PHY:
		return "I40E_ERR_PHY";
	case I40E_ERR_CONFIG:
		return "I40E_ERR_CONFIG";
	case I40E_ERR_PARAM:
		return "I40E_ERR_PARAM";
	case I40E_ERR_MAC_TYPE:
		return "I40E_ERR_MAC_TYPE";
	case I40E_ERR_UNKNOWN_PHY:
		return "I40E_ERR_UNKNOWN_PHY";
	case I40E_ERR_LINK_SETUP:
		return "I40E_ERR_LINK_SETUP";
	case I40E_ERR_ADAPTER_STOPPED:
		return "I40E_ERR_ADAPTER_STOPPED";
	case I40E_ERR_INVALID_MAC_ADDR:
		return "I40E_ERR_INVALID_MAC_ADDR";
	case I40E_ERR_DEVICE_NOT_SUPPORTED:
		return "I40E_ERR_DEVICE_NOT_SUPPORTED";
	case I40E_ERR_MASTER_REQUESTS_PENDING:
		return "I40E_ERR_MASTER_REQUESTS_PENDING";
	case I40E_ERR_INVALID_LINK_SETTINGS:
		return "I40E_ERR_INVALID_LINK_SETTINGS";
	case I40E_ERR_AUTONEG_NOT_COMPLETE:
		return "I40E_ERR_AUTONEG_NOT_COMPLETE";
	case I40E_ERR_RESET_FAILED:
		return "I40E_ERR_RESET_FAILED";
	case I40E_ERR_SWFW_SYNC:
		return "I40E_ERR_SWFW_SYNC";
	case I40E_ERR_NO_AVAILABLE_VSI:
		return "I40E_ERR_NO_AVAILABLE_VSI";
	case I40E_ERR_NO_MEMORY:
		return "I40E_ERR_NO_MEMORY";
	case I40E_ERR_BAD_PTR:
		return "I40E_ERR_BAD_PTR";
	case I40E_ERR_RING_FULL:
		return "I40E_ERR_RING_FULL";
	case I40E_ERR_INVALID_PD_ID:
		return "I40E_ERR_INVALID_PD_ID";
	case I40E_ERR_INVALID_QP_ID:
		return "I40E_ERR_INVALID_QP_ID";
	case I40E_ERR_INVALID_CQ_ID:
		return "I40E_ERR_INVALID_CQ_ID";
	case I40E_ERR_INVALID_CEQ_ID:
		return "I40E_ERR_INVALID_CEQ_ID";
	case I40E_ERR_INVALID_AEQ_ID:
		return "I40E_ERR_INVALID_AEQ_ID";
	case I40E_ERR_INVALID_SIZE:
		return "I40E_ERR_INVALID_SIZE";
	case I40E_ERR_INVALID_ARP_INDEX:
		return "I40E_ERR_INVALID_ARP_INDEX";
	case I40E_ERR_INVALID_FPM_FUNC_ID:
		return "I40E_ERR_INVALID_FPM_FUNC_ID";
	case I40E_ERR_QP_INVALID_MSG_SIZE:
		return "I40E_ERR_QP_INVALID_MSG_SIZE";
	case I40E_ERR_QP_TOOMANY_WRS_POSTED:
		return "I40E_ERR_QP_TOOMANY_WRS_POSTED";
	case I40E_ERR_INVALID_FRAG_COUNT:
		return "I40E_ERR_INVALID_FRAG_COUNT";
	case I40E_ERR_QUEUE_EMPTY:
		return "I40E_ERR_QUEUE_EMPTY";
	case I40E_ERR_INVALID_ALIGNMENT:
		return "I40E_ERR_INVALID_ALIGNMENT";
	case I40E_ERR_FLUSHED_QUEUE:
		return "I40E_ERR_FLUSHED_QUEUE";
	case I40E_ERR_INVALID_PUSH_PAGE_INDEX:
		return "I40E_ERR_INVALID_PUSH_PAGE_INDEX";
	case I40E_ERR_INVALID_IMM_DATA_SIZE:
		return "I40E_ERR_INVALID_IMM_DATA_SIZE";
	case I40E_ERR_TIMEOUT:
		return "I40E_ERR_TIMEOUT";
	case I40E_ERR_OPCODE_MISMATCH:
		return "I40E_ERR_OPCODE_MISMATCH";
	case I40E_ERR_CQP_COMPL_ERROR:
		return "I40E_ERR_CQP_COMPL_ERROR";
	case I40E_ERR_INVALID_VF_ID:
		return "I40E_ERR_INVALID_VF_ID";
	case I40E_ERR_INVALID_HMCFN_ID:
		return "I40E_ERR_INVALID_HMCFN_ID";
	case I40E_ERR_BACKING_PAGE_ERROR:
		return "I40E_ERR_BACKING_PAGE_ERROR";
	case I40E_ERR_NO_PBLCHUNKS_AVAILABLE:
		return "I40E_ERR_NO_PBLCHUNKS_AVAILABLE";
	case I40E_ERR_INVALID_PBLE_INDEX:
		return "I40E_ERR_INVALID_PBLE_INDEX";
	case I40E_ERR_INVALID_SD_INDEX:
		return "I40E_ERR_INVALID_SD_INDEX";
	case I40E_ERR_INVALID_PAGE_DESC_INDEX:
		return "I40E_ERR_INVALID_PAGE_DESC_INDEX";
	case I40E_ERR_INVALID_SD_TYPE:
		return "I40E_ERR_INVALID_SD_TYPE";
	case I40E_ERR_MEMCPY_FAILED:
		return "I40E_ERR_MEMCPY_FAILED";
	case I40E_ERR_INVALID_HMC_OBJ_INDEX:
		return "I40E_ERR_INVALID_HMC_OBJ_INDEX";
	case I40E_ERR_INVALID_HMC_OBJ_COUNT:
		return "I40E_ERR_INVALID_HMC_OBJ_COUNT";
	case I40E_ERR_INVALID_SRQ_ARM_LIMIT:
		return "I40E_ERR_INVALID_SRQ_ARM_LIMIT";
	case I40E_ERR_SRQ_ENABLED:
		return "I40E_ERR_SRQ_ENABLED";
	case I40E_ERR_ADMIN_QUEUE_ERROR:
		return "I40E_ERR_ADMIN_QUEUE_ERROR";
	case I40E_ERR_ADMIN_QUEUE_TIMEOUT:
		return "I40E_ERR_ADMIN_QUEUE_TIMEOUT";
	case I40E_ERR_BUF_TOO_SHORT:
		return "I40E_ERR_BUF_TOO_SHORT";
	case I40E_ERR_ADMIN_QUEUE_FULL:
		return "I40E_ERR_ADMIN_QUEUE_FULL";
	case I40E_ERR_ADMIN_QUEUE_NO_WORK:
		return "I40E_ERR_ADMIN_QUEUE_NO_WORK";
	case I40E_ERR_BAD_IWARP_CQE:
		return "I40E_ERR_BAD_IWARP_CQE";
	case I40E_ERR_NVM_BLANK_MODE:
		return "I40E_ERR_NVM_BLANK_MODE";
	case I40E_ERR_NOT_IMPLEMENTED:
		return "I40E_ERR_NOT_IMPLEMENTED";
	case I40E_ERR_PE_DOORBELL_NOT_ENABLED:
		return "I40E_ERR_PE_DOORBELL_NOT_ENABLED";
	case I40E_ERR_DIAG_TEST_FAILED:
		return "I40E_ERR_DIAG_TEST_FAILED";
	case I40E_ERR_NOT_READY:
		return "I40E_ERR_NOT_READY";
	case I40E_NOT_SUPPORTED:
		return "I40E_NOT_SUPPORTED";
	case I40E_ERR_FIRMWARE_API_VERSION:
		return "I40E_ERR_FIRMWARE_API_VERSION";
	}

	snprintf(hw->err_str, sizeof(hw->err_str), "%d", stat_err);
	return hw->err_str;
}

/**
 * i40e_debug_aq
 * @hw: debug mask related to admin queue
 * @mask: debug mask
 * @desc: pointer to admin queue descriptor
 * @buffer: pointer to command buffer
 * @buf_len: max length of buffer
 *
 * Dumps debug log about adminq command with descriptor contents.
 **/
void i40e_debug_aq(struct i40e_hw *hw, enum i40e_debug_mask mask, void *desc,
		   void *buffer, u16 buf_len)
{
	struct i40e_aq_desc *aq_desc = (struct i40e_aq_desc *)desc;
	u16 len;
	u8 *buf = (u8 *)buffer;
	u16 i = 0;

	if ((!(mask & hw->debug_mask)) || (desc == NULL))
		return;

	len = le16_to_cpu(aq_desc->datalen);

	i40e_debug(hw, mask,
		   "AQ CMD: opcode 0x%04X, flags 0x%04X, datalen 0x%04X, retval 0x%04X\n",
		   le16_to_cpu(aq_desc->opcode),
		   le16_to_cpu(aq_desc->flags),
		   le16_to_cpu(aq_desc->datalen),
		   le16_to_cpu(aq_desc->retval));
	i40e_debug(hw, mask, "\tcookie (h,l) 0x%08X 0x%08X\n",
		   le32_to_cpu(aq_desc->cookie_high),
		   le32_to_cpu(aq_desc->cookie_low));
	i40e_debug(hw, mask, "\tparam (0,1)  0x%08X 0x%08X\n",
		   le32_to_cpu(aq_desc->params.internal.param0),
		   le32_to_cpu(aq_desc->params.internal.param1));
	i40e_debug(hw, mask, "\taddr (h,l)   0x%08X 0x%08X\n",
		   le32_to_cpu(aq_desc->params.external.addr_high),
		   le32_to_cpu(aq_desc->params.external.addr_low));

	if ((buffer != NULL) && (aq_desc->datalen != 0)) {
		i40e_debug(hw, mask, "AQ CMD Buffer:\n");
		if (buf_len < len)
			len = buf_len;
		/* write the full 16-byte chunks */
		for (i = 0; i < (len - 16); i += 16)
			i40e_debug(hw, mask, "\t0x%04X  %16ph\n", i, buf + i);
		/* write whatever's left over without overrunning the buffer */
		if (i < len)
			i40e_debug(hw, mask, "\t0x%04X  %*ph\n",
					     i, len - i, buf + i);
	}
}

/**
 * i40e_check_asq_alive
 * @hw: pointer to the hw struct
 *
 * Returns true if Queue is enabled else false.
 **/
bool i40e_check_asq_alive(struct i40e_hw *hw)
{
	if (hw->aq.asq.len)
		return !!(rd32(hw, hw->aq.asq.len) &
			  I40E_PF_ATQLEN_ATQENABLE_MASK);
	else
		return false;
}

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
 * i40e_aq_get_set_rss_lut
 * @hw: pointer to the hardware structure
 * @vsi_id: vsi fw index
 * @pf_lut: for PF table set true, for VSI table set false
 * @lut: pointer to the lut buffer provided by the caller
 * @lut_size: size of the lut buffer
 * @set: set true to set the table, false to get the table
 *
 * Internal function to get or set RSS look up table
 **/
static i40e_status i40e_aq_get_set_rss_lut(struct i40e_hw *hw,
					   u16 vsi_id, bool pf_lut,
					   u8 *lut, u16 lut_size,
					   bool set)
{
	i40e_status status;
	struct i40e_aq_desc desc;
	struct i40e_aqc_get_set_rss_lut *cmd_resp =
		   (struct i40e_aqc_get_set_rss_lut *)&desc.params.raw;

	if (set)
		i40e_fill_default_direct_cmd_desc(&desc,
						  i40e_aqc_opc_set_rss_lut);
	else
		i40e_fill_default_direct_cmd_desc(&desc,
						  i40e_aqc_opc_get_rss_lut);

	/* Indirect command */
	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_BUF);
	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_RD);

	cmd_resp->vsi_id =
			cpu_to_le16((u16)((vsi_id <<
					  I40E_AQC_SET_RSS_LUT_VSI_ID_SHIFT) &
					  I40E_AQC_SET_RSS_LUT_VSI_ID_MASK));
	cmd_resp->vsi_id |= cpu_to_le16((u16)I40E_AQC_SET_RSS_LUT_VSI_VALID);

	if (pf_lut)
		cmd_resp->flags |= cpu_to_le16((u16)
					((I40E_AQC_SET_RSS_LUT_TABLE_TYPE_PF <<
					I40E_AQC_SET_RSS_LUT_TABLE_TYPE_SHIFT) &
					I40E_AQC_SET_RSS_LUT_TABLE_TYPE_MASK));
	else
		cmd_resp->flags |= cpu_to_le16((u16)
					((I40E_AQC_SET_RSS_LUT_TABLE_TYPE_VSI <<
					I40E_AQC_SET_RSS_LUT_TABLE_TYPE_SHIFT) &
					I40E_AQC_SET_RSS_LUT_TABLE_TYPE_MASK));

	status = i40e_asq_send_command(hw, &desc, lut, lut_size, NULL);

	return status;
}

/**
 * i40e_aq_get_rss_lut
 * @hw: pointer to the hardware structure
 * @vsi_id: vsi fw index
 * @pf_lut: for PF table set true, for VSI table set false
 * @lut: pointer to the lut buffer provided by the caller
 * @lut_size: size of the lut buffer
 *
 * get the RSS lookup table, PF or VSI type
 **/
i40e_status i40e_aq_get_rss_lut(struct i40e_hw *hw, u16 vsi_id,
				bool pf_lut, u8 *lut, u16 lut_size)
{
	return i40e_aq_get_set_rss_lut(hw, vsi_id, pf_lut, lut, lut_size,
				       false);
}

/**
 * i40e_aq_set_rss_lut
 * @hw: pointer to the hardware structure
 * @vsi_id: vsi fw index
 * @pf_lut: for PF table set true, for VSI table set false
 * @lut: pointer to the lut buffer provided by the caller
 * @lut_size: size of the lut buffer
 *
 * set the RSS lookup table, PF or VSI type
 **/
i40e_status i40e_aq_set_rss_lut(struct i40e_hw *hw, u16 vsi_id,
				bool pf_lut, u8 *lut, u16 lut_size)
{
	return i40e_aq_get_set_rss_lut(hw, vsi_id, pf_lut, lut, lut_size, true);
}

/**
 * i40e_aq_get_set_rss_key
 * @hw: pointer to the hw struct
 * @vsi_id: vsi fw index
 * @key: pointer to key info struct
 * @set: set true to set the key, false to get the key
 *
 * get the RSS key per VSI
 **/
static i40e_status i40e_aq_get_set_rss_key(struct i40e_hw *hw,
				      u16 vsi_id,
				      struct i40e_aqc_get_set_rss_key_data *key,
				      bool set)
{
	i40e_status status;
	struct i40e_aq_desc desc;
	struct i40e_aqc_get_set_rss_key *cmd_resp =
			(struct i40e_aqc_get_set_rss_key *)&desc.params.raw;
	u16 key_size = sizeof(struct i40e_aqc_get_set_rss_key_data);

	if (set)
		i40e_fill_default_direct_cmd_desc(&desc,
						  i40e_aqc_opc_set_rss_key);
	else
		i40e_fill_default_direct_cmd_desc(&desc,
						  i40e_aqc_opc_get_rss_key);

	/* Indirect command */
	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_BUF);
	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_RD);

	cmd_resp->vsi_id =
			cpu_to_le16((u16)((vsi_id <<
					  I40E_AQC_SET_RSS_KEY_VSI_ID_SHIFT) &
					  I40E_AQC_SET_RSS_KEY_VSI_ID_MASK));
	cmd_resp->vsi_id |= cpu_to_le16((u16)I40E_AQC_SET_RSS_KEY_VSI_VALID);

	status = i40e_asq_send_command(hw, &desc, key, key_size, NULL);

	return status;
}

/**
 * i40e_aq_get_rss_key
 * @hw: pointer to the hw struct
 * @vsi_id: vsi fw index
 * @key: pointer to key info struct
 *
 **/
i40e_status i40e_aq_get_rss_key(struct i40e_hw *hw,
				u16 vsi_id,
				struct i40e_aqc_get_set_rss_key_data *key)
{
	return i40e_aq_get_set_rss_key(hw, vsi_id, key, false);
}

/**
 * i40e_aq_set_rss_key
 * @hw: pointer to the hw struct
 * @vsi_id: vsi fw index
 * @key: pointer to key info struct
 *
 * set the RSS key per VSI
 **/
i40e_status i40e_aq_set_rss_key(struct i40e_hw *hw,
				u16 vsi_id,
				struct i40e_aqc_get_set_rss_key_data *key)
{
	return i40e_aq_get_set_rss_key(hw, vsi_id, key, true);
}

/* The i40e_ptype_lookup table is used to convert from the 8-bit ptype in the
 * hardware to a bit-field that can be used by SW to more easily determine the
 * packet type.
 *
 * Macros are used to shorten the table lines and make this table human
 * readable.
 *
 * We store the PTYPE in the top byte of the bit field - this is just so that
 * we can check that the table doesn't have a row missing, as the index into
 * the table should be the PTYPE.
 *
 * Typical work flow:
 *
 * IF NOT i40e_ptype_lookup[ptype].known
 * THEN
 *      Packet is unknown
 * ELSE IF i40e_ptype_lookup[ptype].outer_ip == I40E_RX_PTYPE_OUTER_IP
 *      Use the rest of the fields to look at the tunnels, inner protocols, etc
 * ELSE
 *      Use the enum i40e_rx_l2_ptype to decode the packet type
 * ENDIF
 */

/* macro to make the table lines short */
#define I40E_PTT(PTYPE, OUTER_IP, OUTER_IP_VER, OUTER_FRAG, T, TE, TEF, I, PL)\
	{	PTYPE, \
		1, \
		I40E_RX_PTYPE_OUTER_##OUTER_IP, \
		I40E_RX_PTYPE_OUTER_##OUTER_IP_VER, \
		I40E_RX_PTYPE_##OUTER_FRAG, \
		I40E_RX_PTYPE_TUNNEL_##T, \
		I40E_RX_PTYPE_TUNNEL_END_##TE, \
		I40E_RX_PTYPE_##TEF, \
		I40E_RX_PTYPE_INNER_PROT_##I, \
		I40E_RX_PTYPE_PAYLOAD_LAYER_##PL }

#define I40E_PTT_UNUSED_ENTRY(PTYPE) \
		{ PTYPE, 0, 0, 0, 0, 0, 0, 0, 0, 0 }

/* shorter macros makes the table fit but are terse */
#define I40E_RX_PTYPE_NOF		I40E_RX_PTYPE_NOT_FRAG
#define I40E_RX_PTYPE_FRG		I40E_RX_PTYPE_FRAG
#define I40E_RX_PTYPE_INNER_PROT_TS	I40E_RX_PTYPE_INNER_PROT_TIMESYNC

/* Lookup table mapping the HW PTYPE to the bit field for decoding */
struct i40e_rx_ptype_decoded i40e_ptype_lookup[] = {
	/* L2 Packet types */
	I40E_PTT_UNUSED_ENTRY(0),
	I40E_PTT(1,  L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY2),
	I40E_PTT(2,  L2, NONE, NOF, NONE, NONE, NOF, TS,   PAY2),
	I40E_PTT(3,  L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY2),
	I40E_PTT_UNUSED_ENTRY(4),
	I40E_PTT_UNUSED_ENTRY(5),
	I40E_PTT(6,  L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY2),
	I40E_PTT(7,  L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY2),
	I40E_PTT_UNUSED_ENTRY(8),
	I40E_PTT_UNUSED_ENTRY(9),
	I40E_PTT(10, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY2),
	I40E_PTT(11, L2, NONE, NOF, NONE, NONE, NOF, NONE, NONE),
	I40E_PTT(12, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	I40E_PTT(13, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	I40E_PTT(14, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	I40E_PTT(15, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	I40E_PTT(16, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	I40E_PTT(17, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	I40E_PTT(18, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	I40E_PTT(19, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	I40E_PTT(20, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	I40E_PTT(21, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),

	/* Non Tunneled IPv4 */
	I40E_PTT(22, IP, IPV4, FRG, NONE, NONE, NOF, NONE, PAY3),
	I40E_PTT(23, IP, IPV4, NOF, NONE, NONE, NOF, NONE, PAY3),
	I40E_PTT(24, IP, IPV4, NOF, NONE, NONE, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(25),
	I40E_PTT(26, IP, IPV4, NOF, NONE, NONE, NOF, TCP,  PAY4),
	I40E_PTT(27, IP, IPV4, NOF, NONE, NONE, NOF, SCTP, PAY4),
	I40E_PTT(28, IP, IPV4, NOF, NONE, NONE, NOF, ICMP, PAY4),

	/* IPv4 --> IPv4 */
	I40E_PTT(29, IP, IPV4, NOF, IP_IP, IPV4, FRG, NONE, PAY3),
	I40E_PTT(30, IP, IPV4, NOF, IP_IP, IPV4, NOF, NONE, PAY3),
	I40E_PTT(31, IP, IPV4, NOF, IP_IP, IPV4, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(32),
	I40E_PTT(33, IP, IPV4, NOF, IP_IP, IPV4, NOF, TCP,  PAY4),
	I40E_PTT(34, IP, IPV4, NOF, IP_IP, IPV4, NOF, SCTP, PAY4),
	I40E_PTT(35, IP, IPV4, NOF, IP_IP, IPV4, NOF, ICMP, PAY4),

	/* IPv4 --> IPv6 */
	I40E_PTT(36, IP, IPV4, NOF, IP_IP, IPV6, FRG, NONE, PAY3),
	I40E_PTT(37, IP, IPV4, NOF, IP_IP, IPV6, NOF, NONE, PAY3),
	I40E_PTT(38, IP, IPV4, NOF, IP_IP, IPV6, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(39),
	I40E_PTT(40, IP, IPV4, NOF, IP_IP, IPV6, NOF, TCP,  PAY4),
	I40E_PTT(41, IP, IPV4, NOF, IP_IP, IPV6, NOF, SCTP, PAY4),
	I40E_PTT(42, IP, IPV4, NOF, IP_IP, IPV6, NOF, ICMP, PAY4),

	/* IPv4 --> GRE/NAT */
	I40E_PTT(43, IP, IPV4, NOF, IP_GRENAT, NONE, NOF, NONE, PAY3),

	/* IPv4 --> GRE/NAT --> IPv4 */
	I40E_PTT(44, IP, IPV4, NOF, IP_GRENAT, IPV4, FRG, NONE, PAY3),
	I40E_PTT(45, IP, IPV4, NOF, IP_GRENAT, IPV4, NOF, NONE, PAY3),
	I40E_PTT(46, IP, IPV4, NOF, IP_GRENAT, IPV4, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(47),
	I40E_PTT(48, IP, IPV4, NOF, IP_GRENAT, IPV4, NOF, TCP,  PAY4),
	I40E_PTT(49, IP, IPV4, NOF, IP_GRENAT, IPV4, NOF, SCTP, PAY4),
	I40E_PTT(50, IP, IPV4, NOF, IP_GRENAT, IPV4, NOF, ICMP, PAY4),

	/* IPv4 --> GRE/NAT --> IPv6 */
	I40E_PTT(51, IP, IPV4, NOF, IP_GRENAT, IPV6, FRG, NONE, PAY3),
	I40E_PTT(52, IP, IPV4, NOF, IP_GRENAT, IPV6, NOF, NONE, PAY3),
	I40E_PTT(53, IP, IPV4, NOF, IP_GRENAT, IPV6, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(54),
	I40E_PTT(55, IP, IPV4, NOF, IP_GRENAT, IPV6, NOF, TCP,  PAY4),
	I40E_PTT(56, IP, IPV4, NOF, IP_GRENAT, IPV6, NOF, SCTP, PAY4),
	I40E_PTT(57, IP, IPV4, NOF, IP_GRENAT, IPV6, NOF, ICMP, PAY4),

	/* IPv4 --> GRE/NAT --> MAC */
	I40E_PTT(58, IP, IPV4, NOF, IP_GRENAT_MAC, NONE, NOF, NONE, PAY3),

	/* IPv4 --> GRE/NAT --> MAC --> IPv4 */
	I40E_PTT(59, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, FRG, NONE, PAY3),
	I40E_PTT(60, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, NOF, NONE, PAY3),
	I40E_PTT(61, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(62),
	I40E_PTT(63, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, NOF, TCP,  PAY4),
	I40E_PTT(64, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, NOF, SCTP, PAY4),
	I40E_PTT(65, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, NOF, ICMP, PAY4),

	/* IPv4 --> GRE/NAT -> MAC --> IPv6 */
	I40E_PTT(66, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, FRG, NONE, PAY3),
	I40E_PTT(67, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, NOF, NONE, PAY3),
	I40E_PTT(68, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(69),
	I40E_PTT(70, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, NOF, TCP,  PAY4),
	I40E_PTT(71, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, NOF, SCTP, PAY4),
	I40E_PTT(72, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, NOF, ICMP, PAY4),

	/* IPv4 --> GRE/NAT --> MAC/VLAN */
	I40E_PTT(73, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, NONE, NOF, NONE, PAY3),

	/* IPv4 ---> GRE/NAT -> MAC/VLAN --> IPv4 */
	I40E_PTT(74, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, FRG, NONE, PAY3),
	I40E_PTT(75, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, NONE, PAY3),
	I40E_PTT(76, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(77),
	I40E_PTT(78, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, TCP,  PAY4),
	I40E_PTT(79, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, SCTP, PAY4),
	I40E_PTT(80, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, ICMP, PAY4),

	/* IPv4 -> GRE/NAT -> MAC/VLAN --> IPv6 */
	I40E_PTT(81, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, FRG, NONE, PAY3),
	I40E_PTT(82, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, NONE, PAY3),
	I40E_PTT(83, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(84),
	I40E_PTT(85, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, TCP,  PAY4),
	I40E_PTT(86, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, SCTP, PAY4),
	I40E_PTT(87, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, ICMP, PAY4),

	/* Non Tunneled IPv6 */
	I40E_PTT(88, IP, IPV6, FRG, NONE, NONE, NOF, NONE, PAY3),
	I40E_PTT(89, IP, IPV6, NOF, NONE, NONE, NOF, NONE, PAY3),
	I40E_PTT(90, IP, IPV6, NOF, NONE, NONE, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(91),
	I40E_PTT(92, IP, IPV6, NOF, NONE, NONE, NOF, TCP,  PAY4),
	I40E_PTT(93, IP, IPV6, NOF, NONE, NONE, NOF, SCTP, PAY4),
	I40E_PTT(94, IP, IPV6, NOF, NONE, NONE, NOF, ICMP, PAY4),

	/* IPv6 --> IPv4 */
	I40E_PTT(95,  IP, IPV6, NOF, IP_IP, IPV4, FRG, NONE, PAY3),
	I40E_PTT(96,  IP, IPV6, NOF, IP_IP, IPV4, NOF, NONE, PAY3),
	I40E_PTT(97,  IP, IPV6, NOF, IP_IP, IPV4, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(98),
	I40E_PTT(99,  IP, IPV6, NOF, IP_IP, IPV4, NOF, TCP,  PAY4),
	I40E_PTT(100, IP, IPV6, NOF, IP_IP, IPV4, NOF, SCTP, PAY4),
	I40E_PTT(101, IP, IPV6, NOF, IP_IP, IPV4, NOF, ICMP, PAY4),

	/* IPv6 --> IPv6 */
	I40E_PTT(102, IP, IPV6, NOF, IP_IP, IPV6, FRG, NONE, PAY3),
	I40E_PTT(103, IP, IPV6, NOF, IP_IP, IPV6, NOF, NONE, PAY3),
	I40E_PTT(104, IP, IPV6, NOF, IP_IP, IPV6, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(105),
	I40E_PTT(106, IP, IPV6, NOF, IP_IP, IPV6, NOF, TCP,  PAY4),
	I40E_PTT(107, IP, IPV6, NOF, IP_IP, IPV6, NOF, SCTP, PAY4),
	I40E_PTT(108, IP, IPV6, NOF, IP_IP, IPV6, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT */
	I40E_PTT(109, IP, IPV6, NOF, IP_GRENAT, NONE, NOF, NONE, PAY3),

	/* IPv6 --> GRE/NAT -> IPv4 */
	I40E_PTT(110, IP, IPV6, NOF, IP_GRENAT, IPV4, FRG, NONE, PAY3),
	I40E_PTT(111, IP, IPV6, NOF, IP_GRENAT, IPV4, NOF, NONE, PAY3),
	I40E_PTT(112, IP, IPV6, NOF, IP_GRENAT, IPV4, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(113),
	I40E_PTT(114, IP, IPV6, NOF, IP_GRENAT, IPV4, NOF, TCP,  PAY4),
	I40E_PTT(115, IP, IPV6, NOF, IP_GRENAT, IPV4, NOF, SCTP, PAY4),
	I40E_PTT(116, IP, IPV6, NOF, IP_GRENAT, IPV4, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT -> IPv6 */
	I40E_PTT(117, IP, IPV6, NOF, IP_GRENAT, IPV6, FRG, NONE, PAY3),
	I40E_PTT(118, IP, IPV6, NOF, IP_GRENAT, IPV6, NOF, NONE, PAY3),
	I40E_PTT(119, IP, IPV6, NOF, IP_GRENAT, IPV6, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(120),
	I40E_PTT(121, IP, IPV6, NOF, IP_GRENAT, IPV6, NOF, TCP,  PAY4),
	I40E_PTT(122, IP, IPV6, NOF, IP_GRENAT, IPV6, NOF, SCTP, PAY4),
	I40E_PTT(123, IP, IPV6, NOF, IP_GRENAT, IPV6, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT -> MAC */
	I40E_PTT(124, IP, IPV6, NOF, IP_GRENAT_MAC, NONE, NOF, NONE, PAY3),

	/* IPv6 --> GRE/NAT -> MAC -> IPv4 */
	I40E_PTT(125, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, FRG, NONE, PAY3),
	I40E_PTT(126, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, NOF, NONE, PAY3),
	I40E_PTT(127, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(128),
	I40E_PTT(129, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, NOF, TCP,  PAY4),
	I40E_PTT(130, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, NOF, SCTP, PAY4),
	I40E_PTT(131, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT -> MAC -> IPv6 */
	I40E_PTT(132, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, FRG, NONE, PAY3),
	I40E_PTT(133, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, NOF, NONE, PAY3),
	I40E_PTT(134, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(135),
	I40E_PTT(136, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, NOF, TCP,  PAY4),
	I40E_PTT(137, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, NOF, SCTP, PAY4),
	I40E_PTT(138, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT -> MAC/VLAN */
	I40E_PTT(139, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, NONE, NOF, NONE, PAY3),

	/* IPv6 --> GRE/NAT -> MAC/VLAN --> IPv4 */
	I40E_PTT(140, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, FRG, NONE, PAY3),
	I40E_PTT(141, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, NONE, PAY3),
	I40E_PTT(142, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(143),
	I40E_PTT(144, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, TCP,  PAY4),
	I40E_PTT(145, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, SCTP, PAY4),
	I40E_PTT(146, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT -> MAC/VLAN --> IPv6 */
	I40E_PTT(147, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, FRG, NONE, PAY3),
	I40E_PTT(148, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, NONE, PAY3),
	I40E_PTT(149, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, UDP,  PAY4),
	I40E_PTT_UNUSED_ENTRY(150),
	I40E_PTT(151, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, TCP,  PAY4),
	I40E_PTT(152, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, SCTP, PAY4),
	I40E_PTT(153, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, ICMP, PAY4),

	/* unused entries */
	I40E_PTT_UNUSED_ENTRY(154),
	I40E_PTT_UNUSED_ENTRY(155),
	I40E_PTT_UNUSED_ENTRY(156),
	I40E_PTT_UNUSED_ENTRY(157),
	I40E_PTT_UNUSED_ENTRY(158),
	I40E_PTT_UNUSED_ENTRY(159),

	I40E_PTT_UNUSED_ENTRY(160),
	I40E_PTT_UNUSED_ENTRY(161),
	I40E_PTT_UNUSED_ENTRY(162),
	I40E_PTT_UNUSED_ENTRY(163),
	I40E_PTT_UNUSED_ENTRY(164),
	I40E_PTT_UNUSED_ENTRY(165),
	I40E_PTT_UNUSED_ENTRY(166),
	I40E_PTT_UNUSED_ENTRY(167),
	I40E_PTT_UNUSED_ENTRY(168),
	I40E_PTT_UNUSED_ENTRY(169),

	I40E_PTT_UNUSED_ENTRY(170),
	I40E_PTT_UNUSED_ENTRY(171),
	I40E_PTT_UNUSED_ENTRY(172),
	I40E_PTT_UNUSED_ENTRY(173),
	I40E_PTT_UNUSED_ENTRY(174),
	I40E_PTT_UNUSED_ENTRY(175),
	I40E_PTT_UNUSED_ENTRY(176),
	I40E_PTT_UNUSED_ENTRY(177),
	I40E_PTT_UNUSED_ENTRY(178),
	I40E_PTT_UNUSED_ENTRY(179),

	I40E_PTT_UNUSED_ENTRY(180),
	I40E_PTT_UNUSED_ENTRY(181),
	I40E_PTT_UNUSED_ENTRY(182),
	I40E_PTT_UNUSED_ENTRY(183),
	I40E_PTT_UNUSED_ENTRY(184),
	I40E_PTT_UNUSED_ENTRY(185),
	I40E_PTT_UNUSED_ENTRY(186),
	I40E_PTT_UNUSED_ENTRY(187),
	I40E_PTT_UNUSED_ENTRY(188),
	I40E_PTT_UNUSED_ENTRY(189),

	I40E_PTT_UNUSED_ENTRY(190),
	I40E_PTT_UNUSED_ENTRY(191),
	I40E_PTT_UNUSED_ENTRY(192),
	I40E_PTT_UNUSED_ENTRY(193),
	I40E_PTT_UNUSED_ENTRY(194),
	I40E_PTT_UNUSED_ENTRY(195),
	I40E_PTT_UNUSED_ENTRY(196),
	I40E_PTT_UNUSED_ENTRY(197),
	I40E_PTT_UNUSED_ENTRY(198),
	I40E_PTT_UNUSED_ENTRY(199),

	I40E_PTT_UNUSED_ENTRY(200),
	I40E_PTT_UNUSED_ENTRY(201),
	I40E_PTT_UNUSED_ENTRY(202),
	I40E_PTT_UNUSED_ENTRY(203),
	I40E_PTT_UNUSED_ENTRY(204),
	I40E_PTT_UNUSED_ENTRY(205),
	I40E_PTT_UNUSED_ENTRY(206),
	I40E_PTT_UNUSED_ENTRY(207),
	I40E_PTT_UNUSED_ENTRY(208),
	I40E_PTT_UNUSED_ENTRY(209),

	I40E_PTT_UNUSED_ENTRY(210),
	I40E_PTT_UNUSED_ENTRY(211),
	I40E_PTT_UNUSED_ENTRY(212),
	I40E_PTT_UNUSED_ENTRY(213),
	I40E_PTT_UNUSED_ENTRY(214),
	I40E_PTT_UNUSED_ENTRY(215),
	I40E_PTT_UNUSED_ENTRY(216),
	I40E_PTT_UNUSED_ENTRY(217),
	I40E_PTT_UNUSED_ENTRY(218),
	I40E_PTT_UNUSED_ENTRY(219),

	I40E_PTT_UNUSED_ENTRY(220),
	I40E_PTT_UNUSED_ENTRY(221),
	I40E_PTT_UNUSED_ENTRY(222),
	I40E_PTT_UNUSED_ENTRY(223),
	I40E_PTT_UNUSED_ENTRY(224),
	I40E_PTT_UNUSED_ENTRY(225),
	I40E_PTT_UNUSED_ENTRY(226),
	I40E_PTT_UNUSED_ENTRY(227),
	I40E_PTT_UNUSED_ENTRY(228),
	I40E_PTT_UNUSED_ENTRY(229),

	I40E_PTT_UNUSED_ENTRY(230),
	I40E_PTT_UNUSED_ENTRY(231),
	I40E_PTT_UNUSED_ENTRY(232),
	I40E_PTT_UNUSED_ENTRY(233),
	I40E_PTT_UNUSED_ENTRY(234),
	I40E_PTT_UNUSED_ENTRY(235),
	I40E_PTT_UNUSED_ENTRY(236),
	I40E_PTT_UNUSED_ENTRY(237),
	I40E_PTT_UNUSED_ENTRY(238),
	I40E_PTT_UNUSED_ENTRY(239),

	I40E_PTT_UNUSED_ENTRY(240),
	I40E_PTT_UNUSED_ENTRY(241),
	I40E_PTT_UNUSED_ENTRY(242),
	I40E_PTT_UNUSED_ENTRY(243),
	I40E_PTT_UNUSED_ENTRY(244),
	I40E_PTT_UNUSED_ENTRY(245),
	I40E_PTT_UNUSED_ENTRY(246),
	I40E_PTT_UNUSED_ENTRY(247),
	I40E_PTT_UNUSED_ENTRY(248),
	I40E_PTT_UNUSED_ENTRY(249),

	I40E_PTT_UNUSED_ENTRY(250),
	I40E_PTT_UNUSED_ENTRY(251),
	I40E_PTT_UNUSED_ENTRY(252),
	I40E_PTT_UNUSED_ENTRY(253),
	I40E_PTT_UNUSED_ENTRY(254),
	I40E_PTT_UNUSED_ENTRY(255)
};

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
	u32 port, ari, func_rid;

	i40e_set_mac_type(hw);

	switch (hw->mac.type) {
	case I40E_MAC_XL710:
	case I40E_MAC_X722:
		break;
	default:
		return I40E_ERR_DEVICE_NOT_SUPPORTED;
	}

	hw->phy.get_link_info = true;

	/* Determine port number and PF number*/
	port = (rd32(hw, I40E_PFGEN_PORTNUM) & I40E_PFGEN_PORTNUM_PORT_NUM_MASK)
					   >> I40E_PFGEN_PORTNUM_PORT_NUM_SHIFT;
	hw->port = (u8)port;
	ari = (rd32(hw, I40E_GLPCI_CAPSUP) & I40E_GLPCI_CAPSUP_ARI_EN_MASK) >>
						 I40E_GLPCI_CAPSUP_ARI_EN_SHIFT;
	func_rid = rd32(hw, I40E_PF_FUNC_RID);
	if (ari)
		hw->pf_id = (u8)(func_rid & 0xff);
	else
		hw->pf_id = (u8)(func_rid & 0x7);

	if (hw->mac.type == I40E_MAC_X722)
		hw->flags |= I40E_HW_FLAG_AQ_SRCTL_ACCESS_ENABLE;

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
	cmd_data->mac_sah = cpu_to_le16((u16)mac_addr[0] << 8 | mac_addr[1]);
	cmd_data->mac_sal = cpu_to_le32(((u32)mac_addr[2] << 24) |
					((u32)mac_addr[3] << 16) |
					((u32)mac_addr[4] << 8) |
					mac_addr[5]);

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
		ether_addr_copy(mac_addr, addrs.pf_lan_mac);

	return status;
}

/**
 * i40e_get_port_mac_addr - get Port MAC address
 * @hw: pointer to the HW structure
 * @mac_addr: pointer to Port MAC address
 *
 * Reads the adapter's Port MAC address
 **/
i40e_status i40e_get_port_mac_addr(struct i40e_hw *hw, u8 *mac_addr)
{
	struct i40e_aqc_mac_address_read_data addrs;
	i40e_status status;
	u16 flags = 0;

	status = i40e_aq_mac_address_read(hw, &flags, &addrs, NULL);
	if (status)
		return status;

	if (flags & I40E_AQC_PORT_ADDR_VALID)
		ether_addr_copy(mac_addr, addrs.port_mac);
	else
		status = I40E_ERR_INVALID_MAC_ADDR;

	return status;
}

/**
 * i40e_pre_tx_queue_cfg - pre tx queue configure
 * @hw: pointer to the HW structure
 * @queue: target PF queue index
 * @enable: state change request
 *
 * Handles hw requirement to indicate intention to enable
 * or disable target queue.
 **/
void i40e_pre_tx_queue_cfg(struct i40e_hw *hw, u32 queue, bool enable)
{
	u32 abs_queue_idx = hw->func_caps.base_queue + queue;
	u32 reg_block = 0;
	u32 reg_val;

	if (abs_queue_idx >= 128) {
		reg_block = abs_queue_idx / 128;
		abs_queue_idx %= 128;
	}

	reg_val = rd32(hw, I40E_GLLAN_TXPRE_QDIS(reg_block));
	reg_val &= ~I40E_GLLAN_TXPRE_QDIS_QINDX_MASK;
	reg_val |= (abs_queue_idx << I40E_GLLAN_TXPRE_QDIS_QINDX_SHIFT);

	if (enable)
		reg_val |= I40E_GLLAN_TXPRE_QDIS_CLEAR_QDIS_MASK;
	else
		reg_val |= I40E_GLLAN_TXPRE_QDIS_SET_QDIS_MASK;

	wr32(hw, I40E_GLLAN_TXPRE_QDIS(reg_block), reg_val);
}
#ifdef I40E_FCOE

/**
 * i40e_get_san_mac_addr - get SAN MAC address
 * @hw: pointer to the HW structure
 * @mac_addr: pointer to SAN MAC address
 *
 * Reads the adapter's SAN MAC address from NVM
 **/
i40e_status i40e_get_san_mac_addr(struct i40e_hw *hw, u8 *mac_addr)
{
	struct i40e_aqc_mac_address_read_data addrs;
	i40e_status status;
	u16 flags = 0;

	status = i40e_aq_mac_address_read(hw, &flags, &addrs, NULL);
	if (status)
		return status;

	if (flags & I40E_AQC_SAN_ADDR_VALID)
		ether_addr_copy(mac_addr, addrs.pf_san_mac);
	else
		status = I40E_ERR_INVALID_MAC_ADDR;

	return status;
}
#endif

/**
 *  i40e_read_pba_string - Reads part number string from EEPROM
 *  @hw: pointer to hardware structure
 *  @pba_num: stores the part number string from the EEPROM
 *  @pba_num_size: part number string buffer length
 *
 *  Reads the part number string from the EEPROM.
 **/
i40e_status i40e_read_pba_string(struct i40e_hw *hw, u8 *pba_num,
				 u32 pba_num_size)
{
	i40e_status status = 0;
	u16 pba_word = 0;
	u16 pba_size = 0;
	u16 pba_ptr = 0;
	u16 i = 0;

	status = i40e_read_nvm_word(hw, I40E_SR_PBA_FLAGS, &pba_word);
	if (status || (pba_word != 0xFAFA)) {
		hw_dbg(hw, "Failed to read PBA flags or flag is invalid.\n");
		return status;
	}

	status = i40e_read_nvm_word(hw, I40E_SR_PBA_BLOCK_PTR, &pba_ptr);
	if (status) {
		hw_dbg(hw, "Failed to read PBA Block pointer.\n");
		return status;
	}

	status = i40e_read_nvm_word(hw, pba_ptr, &pba_size);
	if (status) {
		hw_dbg(hw, "Failed to read PBA Block size.\n");
		return status;
	}

	/* Subtract one to get PBA word count (PBA Size word is included in
	 * total size)
	 */
	pba_size--;
	if (pba_num_size < (((u32)pba_size * 2) + 1)) {
		hw_dbg(hw, "Buffer to small for PBA data.\n");
		return I40E_ERR_PARAM;
	}

	for (i = 0; i < pba_size; i++) {
		status = i40e_read_nvm_word(hw, (pba_ptr + 1) + i, &pba_word);
		if (status) {
			hw_dbg(hw, "Failed to read PBA Block word %d.\n", i);
			return status;
		}

		pba_num[(i * 2)] = (pba_word >> 8) & 0xFF;
		pba_num[(i * 2) + 1] = pba_word & 0xFF;
	}
	pba_num[(pba_size * 2)] = '\0';

	return status;
}

/**
 * i40e_get_media_type - Gets media type
 * @hw: pointer to the hardware structure
 **/
static enum i40e_media_type i40e_get_media_type(struct i40e_hw *hw)
{
	enum i40e_media_type media;

	switch (hw->phy.link_info.phy_type) {
	case I40E_PHY_TYPE_10GBASE_SR:
	case I40E_PHY_TYPE_10GBASE_LR:
	case I40E_PHY_TYPE_1000BASE_SX:
	case I40E_PHY_TYPE_1000BASE_LX:
	case I40E_PHY_TYPE_40GBASE_SR4:
	case I40E_PHY_TYPE_40GBASE_LR4:
		media = I40E_MEDIA_TYPE_FIBER;
		break;
	case I40E_PHY_TYPE_100BASE_TX:
	case I40E_PHY_TYPE_1000BASE_T:
	case I40E_PHY_TYPE_10GBASE_T:
		media = I40E_MEDIA_TYPE_BASET;
		break;
	case I40E_PHY_TYPE_10GBASE_CR1_CU:
	case I40E_PHY_TYPE_40GBASE_CR4_CU:
	case I40E_PHY_TYPE_10GBASE_CR1:
	case I40E_PHY_TYPE_40GBASE_CR4:
	case I40E_PHY_TYPE_10GBASE_SFPP_CU:
	case I40E_PHY_TYPE_40GBASE_AOC:
	case I40E_PHY_TYPE_10GBASE_AOC:
		media = I40E_MEDIA_TYPE_DA;
		break;
	case I40E_PHY_TYPE_1000BASE_KX:
	case I40E_PHY_TYPE_10GBASE_KX4:
	case I40E_PHY_TYPE_10GBASE_KR:
	case I40E_PHY_TYPE_40GBASE_KR4:
	case I40E_PHY_TYPE_20GBASE_KR2:
		media = I40E_MEDIA_TYPE_BACKPLANE;
		break;
	case I40E_PHY_TYPE_SGMII:
	case I40E_PHY_TYPE_XAUI:
	case I40E_PHY_TYPE_XFI:
	case I40E_PHY_TYPE_XLAUI:
	case I40E_PHY_TYPE_XLPPI:
	default:
		media = I40E_MEDIA_TYPE_UNKNOWN;
		break;
	}

	return media;
}

#define I40E_PF_RESET_WAIT_COUNT_A0	200
#define I40E_PF_RESET_WAIT_COUNT	200
/**
 * i40e_pf_reset - Reset the PF
 * @hw: pointer to the hardware structure
 *
 * Assuming someone else has triggered a global reset,
 * assure the global reset is complete and then reset the PF
 **/
i40e_status i40e_pf_reset(struct i40e_hw *hw)
{
	u32 cnt = 0;
	u32 cnt1 = 0;
	u32 reg = 0;
	u32 grst_del;

	/* Poll for Global Reset steady state in case of recent GRST.
	 * The grst delay value is in 100ms units, and we'll wait a
	 * couple counts longer to be sure we don't just miss the end.
	 */
	grst_del = (rd32(hw, I40E_GLGEN_RSTCTL) &
		    I40E_GLGEN_RSTCTL_GRSTDEL_MASK) >>
		    I40E_GLGEN_RSTCTL_GRSTDEL_SHIFT;

	/* It can take upto 15 secs for GRST steady state.
	 * Bump it to 16 secs max to be safe.
	 */
	grst_del = grst_del * 20;

	for (cnt = 0; cnt < grst_del; cnt++) {
		reg = rd32(hw, I40E_GLGEN_RSTAT);
		if (!(reg & I40E_GLGEN_RSTAT_DEVSTATE_MASK))
			break;
		msleep(100);
	}
	if (reg & I40E_GLGEN_RSTAT_DEVSTATE_MASK) {
		hw_dbg(hw, "Global reset polling failed to complete.\n");
		return I40E_ERR_RESET_FAILED;
	}

	/* Now Wait for the FW to be ready */
	for (cnt1 = 0; cnt1 < I40E_PF_RESET_WAIT_COUNT; cnt1++) {
		reg = rd32(hw, I40E_GLNVM_ULD);
		reg &= (I40E_GLNVM_ULD_CONF_CORE_DONE_MASK |
			I40E_GLNVM_ULD_CONF_GLOBAL_DONE_MASK);
		if (reg == (I40E_GLNVM_ULD_CONF_CORE_DONE_MASK |
			    I40E_GLNVM_ULD_CONF_GLOBAL_DONE_MASK)) {
			hw_dbg(hw, "Core and Global modules ready %d\n", cnt1);
			break;
		}
		usleep_range(10000, 20000);
	}
	if (!(reg & (I40E_GLNVM_ULD_CONF_CORE_DONE_MASK |
		     I40E_GLNVM_ULD_CONF_GLOBAL_DONE_MASK))) {
		hw_dbg(hw, "wait for FW Reset complete timedout\n");
		hw_dbg(hw, "I40E_GLNVM_ULD = 0x%x\n", reg);
		return I40E_ERR_RESET_FAILED;
	}

	/* If there was a Global Reset in progress when we got here,
	 * we don't need to do the PF Reset
	 */
	if (!cnt) {
		if (hw->revision_id == 0)
			cnt = I40E_PF_RESET_WAIT_COUNT_A0;
		else
			cnt = I40E_PF_RESET_WAIT_COUNT;
		reg = rd32(hw, I40E_PFGEN_CTRL);
		wr32(hw, I40E_PFGEN_CTRL,
		     (reg | I40E_PFGEN_CTRL_PFSWR_MASK));
		for (; cnt; cnt--) {
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
 * i40e_clear_hw - clear out any left over hw state
 * @hw: pointer to the hw struct
 *
 * Clear queues and interrupts, typically called at init time,
 * but after the capabilities have been found so we know how many
 * queues and msix vectors have been allocated.
 **/
void i40e_clear_hw(struct i40e_hw *hw)
{
	u32 num_queues, base_queue;
	u32 num_pf_int;
	u32 num_vf_int;
	u32 num_vfs;
	u32 i, j;
	u32 val;
	u32 eol = 0x7ff;

	/* get number of interrupts, queues, and VFs */
	val = rd32(hw, I40E_GLPCI_CNF2);
	num_pf_int = (val & I40E_GLPCI_CNF2_MSI_X_PF_N_MASK) >>
		     I40E_GLPCI_CNF2_MSI_X_PF_N_SHIFT;
	num_vf_int = (val & I40E_GLPCI_CNF2_MSI_X_VF_N_MASK) >>
		     I40E_GLPCI_CNF2_MSI_X_VF_N_SHIFT;

	val = rd32(hw, I40E_PFLAN_QALLOC);
	base_queue = (val & I40E_PFLAN_QALLOC_FIRSTQ_MASK) >>
		     I40E_PFLAN_QALLOC_FIRSTQ_SHIFT;
	j = (val & I40E_PFLAN_QALLOC_LASTQ_MASK) >>
	    I40E_PFLAN_QALLOC_LASTQ_SHIFT;
	if (val & I40E_PFLAN_QALLOC_VALID_MASK)
		num_queues = (j - base_queue) + 1;
	else
		num_queues = 0;

	val = rd32(hw, I40E_PF_VT_PFALLOC);
	i = (val & I40E_PF_VT_PFALLOC_FIRSTVF_MASK) >>
	    I40E_PF_VT_PFALLOC_FIRSTVF_SHIFT;
	j = (val & I40E_PF_VT_PFALLOC_LASTVF_MASK) >>
	    I40E_PF_VT_PFALLOC_LASTVF_SHIFT;
	if (val & I40E_PF_VT_PFALLOC_VALID_MASK)
		num_vfs = (j - i) + 1;
	else
		num_vfs = 0;

	/* stop all the interrupts */
	wr32(hw, I40E_PFINT_ICR0_ENA, 0);
	val = 0x3 << I40E_PFINT_DYN_CTLN_ITR_INDX_SHIFT;
	for (i = 0; i < num_pf_int - 2; i++)
		wr32(hw, I40E_PFINT_DYN_CTLN(i), val);

	/* Set the FIRSTQ_INDX field to 0x7FF in PFINT_LNKLSTx */
	val = eol << I40E_PFINT_LNKLST0_FIRSTQ_INDX_SHIFT;
	wr32(hw, I40E_PFINT_LNKLST0, val);
	for (i = 0; i < num_pf_int - 2; i++)
		wr32(hw, I40E_PFINT_LNKLSTN(i), val);
	val = eol << I40E_VPINT_LNKLST0_FIRSTQ_INDX_SHIFT;
	for (i = 0; i < num_vfs; i++)
		wr32(hw, I40E_VPINT_LNKLST0(i), val);
	for (i = 0; i < num_vf_int - 2; i++)
		wr32(hw, I40E_VPINT_LNKLSTN(i), val);

	/* warn the HW of the coming Tx disables */
	for (i = 0; i < num_queues; i++) {
		u32 abs_queue_idx = base_queue + i;
		u32 reg_block = 0;

		if (abs_queue_idx >= 128) {
			reg_block = abs_queue_idx / 128;
			abs_queue_idx %= 128;
		}

		val = rd32(hw, I40E_GLLAN_TXPRE_QDIS(reg_block));
		val &= ~I40E_GLLAN_TXPRE_QDIS_QINDX_MASK;
		val |= (abs_queue_idx << I40E_GLLAN_TXPRE_QDIS_QINDX_SHIFT);
		val |= I40E_GLLAN_TXPRE_QDIS_SET_QDIS_MASK;

		wr32(hw, I40E_GLLAN_TXPRE_QDIS(reg_block), val);
	}
	udelay(400);

	/* stop all the queues */
	for (i = 0; i < num_queues; i++) {
		wr32(hw, I40E_QINT_TQCTL(i), 0);
		wr32(hw, I40E_QTX_ENA(i), 0);
		wr32(hw, I40E_QINT_RQCTL(i), 0);
		wr32(hw, I40E_QRX_ENA(i), 0);
	}

	/* short wait for all queue disables to settle */
	udelay(50);
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

	if (i40e_check_asq_alive(hw))
		i40e_aq_clear_pxe_mode(hw, NULL);

	/* Clear single descriptor fetch/write-back mode */
	reg = rd32(hw, I40E_GLLAN_RCTL_0);

	if (hw->revision_id == 0) {
		/* As a work around clear PXE_MODE instead of setting it */
		wr32(hw, I40E_GLLAN_RCTL_0, (reg & (~I40E_GLLAN_RCTL_0_PXE_MODE_MASK)));
	} else {
		wr32(hw, I40E_GLLAN_RCTL_0, (reg | I40E_GLLAN_RCTL_0_PXE_MODE_MASK));
	}
}

/**
 * i40e_led_is_mine - helper to find matching led
 * @hw: pointer to the hw struct
 * @idx: index into GPIO registers
 *
 * returns: 0 if no match, otherwise the value of the GPIO_CTL register
 */
static u32 i40e_led_is_mine(struct i40e_hw *hw, int idx)
{
	u32 gpio_val = 0;
	u32 port;

	if (!hw->func_caps.led[idx])
		return 0;

	gpio_val = rd32(hw, I40E_GLGEN_GPIO_CTL(idx));
	port = (gpio_val & I40E_GLGEN_GPIO_CTL_PRT_NUM_MASK) >>
		I40E_GLGEN_GPIO_CTL_PRT_NUM_SHIFT;

	/* if PRT_NUM_NA is 1 then this LED is not port specific, OR
	 * if it is not our port then ignore
	 */
	if ((gpio_val & I40E_GLGEN_GPIO_CTL_PRT_NUM_NA_MASK) ||
	    (port != hw->port))
		return 0;

	return gpio_val;
}

#define I40E_COMBINED_ACTIVITY 0xA
#define I40E_FILTER_ACTIVITY 0xE
#define I40E_LINK_ACTIVITY 0xC
#define I40E_MAC_ACTIVITY 0xD
#define I40E_LED0 22

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
	u32 current_mode = 0;
	u32 mode = 0;
	int i;

	/* as per the documentation GPIO 22-29 are the LED
	 * GPIO pins named LED0..LED7
	 */
	for (i = I40E_LED0; i <= I40E_GLGEN_GPIO_CTL_MAX_INDEX; i++) {
		u32 gpio_val = i40e_led_is_mine(hw, i);

		if (!gpio_val)
			continue;

		/* ignore gpio LED src mode entries related to the activity
		 * LEDs
		 */
		current_mode = ((gpio_val & I40E_GLGEN_GPIO_CTL_LED_MODE_MASK)
				>> I40E_GLGEN_GPIO_CTL_LED_MODE_SHIFT);
		switch (current_mode) {
		case I40E_COMBINED_ACTIVITY:
		case I40E_FILTER_ACTIVITY:
		case I40E_MAC_ACTIVITY:
			continue;
		default:
			break;
		}

		mode = (gpio_val & I40E_GLGEN_GPIO_CTL_LED_MODE_MASK) >>
			I40E_GLGEN_GPIO_CTL_LED_MODE_SHIFT;
		break;
	}

	return mode;
}

/**
 * i40e_led_set - set new on/off mode
 * @hw: pointer to the hw struct
 * @mode: 0=off, 0xf=on (else see manual for mode details)
 * @blink: true if the LED should blink when on, false if steady
 *
 * if this function is used to turn on the blink it should
 * be used to disable the blink when restoring the original state.
 **/
void i40e_led_set(struct i40e_hw *hw, u32 mode, bool blink)
{
	u32 current_mode = 0;
	int i;

	if (mode & 0xfffffff0)
		hw_dbg(hw, "invalid mode passed in %X\n", mode);

	/* as per the documentation GPIO 22-29 are the LED
	 * GPIO pins named LED0..LED7
	 */
	for (i = I40E_LED0; i <= I40E_GLGEN_GPIO_CTL_MAX_INDEX; i++) {
		u32 gpio_val = i40e_led_is_mine(hw, i);

		if (!gpio_val)
			continue;

		/* ignore gpio LED src mode entries related to the activity
		 * LEDs
		 */
		current_mode = ((gpio_val & I40E_GLGEN_GPIO_CTL_LED_MODE_MASK)
				>> I40E_GLGEN_GPIO_CTL_LED_MODE_SHIFT);
		switch (current_mode) {
		case I40E_COMBINED_ACTIVITY:
		case I40E_FILTER_ACTIVITY:
		case I40E_MAC_ACTIVITY:
			continue;
		default:
			break;
		}

		gpio_val &= ~I40E_GLGEN_GPIO_CTL_LED_MODE_MASK;
		/* this & is a bit of paranoia, but serves as a range check */
		gpio_val |= ((mode << I40E_GLGEN_GPIO_CTL_LED_MODE_SHIFT) &
			     I40E_GLGEN_GPIO_CTL_LED_MODE_MASK);

		if (mode == I40E_LINK_ACTIVITY)
			blink = false;

		if (blink)
			gpio_val |= BIT(I40E_GLGEN_GPIO_CTL_LED_BLINK_SHIFT);
		else
			gpio_val &= ~BIT(I40E_GLGEN_GPIO_CTL_LED_BLINK_SHIFT);

		wr32(hw, I40E_GLGEN_GPIO_CTL(i), gpio_val);
		break;
	}
}

/* Admin command wrappers */

/**
 * i40e_aq_get_phy_capabilities
 * @hw: pointer to the hw struct
 * @abilities: structure for PHY capabilities to be filled
 * @qualified_modules: report Qualified Modules
 * @report_init: report init capabilities (active are default)
 * @cmd_details: pointer to command details structure or NULL
 *
 * Returns the various PHY abilities supported on the Port.
 **/
i40e_status i40e_aq_get_phy_capabilities(struct i40e_hw *hw,
			bool qualified_modules, bool report_init,
			struct i40e_aq_get_phy_abilities_resp *abilities,
			struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	i40e_status status;
	u16 abilities_size = sizeof(struct i40e_aq_get_phy_abilities_resp);

	if (!abilities)
		return I40E_ERR_PARAM;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_get_phy_abilities);

	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_BUF);
	if (abilities_size > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);

	if (qualified_modules)
		desc.params.external.param0 |=
			cpu_to_le32(I40E_AQ_PHY_REPORT_QUALIFIED_MODULES);

	if (report_init)
		desc.params.external.param0 |=
			cpu_to_le32(I40E_AQ_PHY_REPORT_INITIAL_VALUES);

	status = i40e_asq_send_command(hw, &desc, abilities, abilities_size,
				       cmd_details);

	if (hw->aq.asq_last_status == I40E_AQ_RC_EIO)
		status = I40E_ERR_UNKNOWN_PHY;

	if (report_init)
		hw->phy.phy_types = le32_to_cpu(abilities->phy_type);

	return status;
}

/**
 * i40e_aq_set_phy_config
 * @hw: pointer to the hw struct
 * @config: structure with PHY configuration to be set
 * @cmd_details: pointer to command details structure or NULL
 *
 * Set the various PHY configuration parameters
 * supported on the Port.One or more of the Set PHY config parameters may be
 * ignored in an MFP mode as the PF may not have the privilege to set some
 * of the PHY Config parameters. This status will be indicated by the
 * command response.
 **/
enum i40e_status_code i40e_aq_set_phy_config(struct i40e_hw *hw,
				struct i40e_aq_set_phy_config *config,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aq_set_phy_config *cmd =
			(struct i40e_aq_set_phy_config *)&desc.params.raw;
	enum i40e_status_code status;

	if (!config)
		return I40E_ERR_PARAM;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_set_phy_config);

	*cmd = *config;

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_set_fc
 * @hw: pointer to the hw struct
 *
 * Set the requested flow control mode using set_phy_config.
 **/
enum i40e_status_code i40e_set_fc(struct i40e_hw *hw, u8 *aq_failures,
				  bool atomic_restart)
{
	enum i40e_fc_mode fc_mode = hw->fc.requested_mode;
	struct i40e_aq_get_phy_abilities_resp abilities;
	struct i40e_aq_set_phy_config config;
	enum i40e_status_code status;
	u8 pause_mask = 0x0;

	*aq_failures = 0x0;

	switch (fc_mode) {
	case I40E_FC_FULL:
		pause_mask |= I40E_AQ_PHY_FLAG_PAUSE_TX;
		pause_mask |= I40E_AQ_PHY_FLAG_PAUSE_RX;
		break;
	case I40E_FC_RX_PAUSE:
		pause_mask |= I40E_AQ_PHY_FLAG_PAUSE_RX;
		break;
	case I40E_FC_TX_PAUSE:
		pause_mask |= I40E_AQ_PHY_FLAG_PAUSE_TX;
		break;
	default:
		break;
	}

	/* Get the current phy config */
	status = i40e_aq_get_phy_capabilities(hw, false, false, &abilities,
					      NULL);
	if (status) {
		*aq_failures |= I40E_SET_FC_AQ_FAIL_GET;
		return status;
	}

	memset(&config, 0, sizeof(struct i40e_aq_set_phy_config));
	/* clear the old pause settings */
	config.abilities = abilities.abilities & ~(I40E_AQ_PHY_FLAG_PAUSE_TX) &
			   ~(I40E_AQ_PHY_FLAG_PAUSE_RX);
	/* set the new abilities */
	config.abilities |= pause_mask;
	/* If the abilities have changed, then set the new config */
	if (config.abilities != abilities.abilities) {
		/* Auto restart link so settings take effect */
		if (atomic_restart)
			config.abilities |= I40E_AQ_PHY_ENABLE_ATOMIC_LINK;
		/* Copy over all the old settings */
		config.phy_type = abilities.phy_type;
		config.link_speed = abilities.link_speed;
		config.eee_capability = abilities.eee_capability;
		config.eeer = abilities.eeer_val;
		config.low_power_ctrl = abilities.d3_lpan;
		status = i40e_aq_set_phy_config(hw, &config, NULL);

		if (status)
			*aq_failures |= I40E_SET_FC_AQ_FAIL_SET;
	}
	/* Update the link info */
	status = i40e_update_link_info(hw);
	if (status) {
		/* Wait a little bit (on 40G cards it sometimes takes a really
		 * long time for link to come back from the atomic reset)
		 * and try once more
		 */
		msleep(1000);
		status = i40e_update_link_info(hw);
	}
	if (status)
		*aq_failures |= I40E_SET_FC_AQ_FAIL_UPDATE;

	return status;
}

/**
 * i40e_aq_clear_pxe_mode
 * @hw: pointer to the hw struct
 * @cmd_details: pointer to command details structure or NULL
 *
 * Tell the firmware that the driver is taking over from PXE
 **/
i40e_status i40e_aq_clear_pxe_mode(struct i40e_hw *hw,
				struct i40e_asq_cmd_details *cmd_details)
{
	i40e_status status;
	struct i40e_aq_desc desc;
	struct i40e_aqc_clear_pxe *cmd =
		(struct i40e_aqc_clear_pxe *)&desc.params.raw;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_clear_pxe_mode);

	cmd->rx_cnt = 0x2;

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	wr32(hw, I40E_GLLAN_RCTL_0, 0x1);

	return status;
}

/**
 * i40e_aq_set_link_restart_an
 * @hw: pointer to the hw struct
 * @enable_link: if true: enable link, if false: disable link
 * @cmd_details: pointer to command details structure or NULL
 *
 * Sets up the link and restarts the Auto-Negotiation over the link.
 **/
i40e_status i40e_aq_set_link_restart_an(struct i40e_hw *hw,
					bool enable_link,
					struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_set_link_restart_an *cmd =
		(struct i40e_aqc_set_link_restart_an *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_set_link_restart_an);

	cmd->command = I40E_AQ_PHY_RESTART_AN;
	if (enable_link)
		cmd->command |= I40E_AQ_PHY_LINK_ENABLE;
	else
		cmd->command &= ~I40E_AQ_PHY_LINK_ENABLE;

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
	bool tx_pause, rx_pause;
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
	hw->phy.link_info_old = *hw_link_info;

	/* update link status */
	hw_link_info->phy_type = (enum i40e_aq_phy_type)resp->phy_type;
	hw->phy.media_type = i40e_get_media_type(hw);
	hw_link_info->link_speed = (enum i40e_aq_link_speed)resp->link_speed;
	hw_link_info->link_info = resp->link_info;
	hw_link_info->an_info = resp->an_info;
	hw_link_info->ext_info = resp->ext_info;
	hw_link_info->loopback = resp->loopback;
	hw_link_info->max_frame_size = le16_to_cpu(resp->max_frame_size);
	hw_link_info->pacing = resp->config & I40E_AQ_CONFIG_PACING_MASK;

	/* update fc info */
	tx_pause = !!(resp->an_info & I40E_AQ_LINK_PAUSE_TX);
	rx_pause = !!(resp->an_info & I40E_AQ_LINK_PAUSE_RX);
	if (tx_pause & rx_pause)
		hw->fc.current_mode = I40E_FC_FULL;
	else if (tx_pause)
		hw->fc.current_mode = I40E_FC_TX_PAUSE;
	else if (rx_pause)
		hw->fc.current_mode = I40E_FC_RX_PAUSE;
	else
		hw->fc.current_mode = I40E_FC_NONE;

	if (resp->config & I40E_AQ_CONFIG_CRC_ENA)
		hw_link_info->crc_enable = true;
	else
		hw_link_info->crc_enable = false;

	if (resp->command_flags & cpu_to_le16(I40E_AQ_LSE_IS_ENABLED))
		hw_link_info->lse_enable = true;
	else
		hw_link_info->lse_enable = false;

	if ((hw->aq.fw_maj_ver < 4 || (hw->aq.fw_maj_ver == 4 &&
	     hw->aq.fw_min_ver < 40)) && hw_link_info->phy_type == 0xE)
		hw_link_info->phy_type = I40E_PHY_TYPE_10GBASE_SFPP_CU;

	/* save link status information */
	if (link)
		*link = *hw_link_info;

	/* flag cleared so helper functions don't call AQ again */
	hw->phy.get_link_info = false;

aq_get_link_info_exit:
	return status;
}

/**
 * i40e_aq_set_phy_int_mask
 * @hw: pointer to the hw struct
 * @mask: interrupt mask to be set
 * @cmd_details: pointer to command details structure or NULL
 *
 * Set link interrupt mask.
 **/
i40e_status i40e_aq_set_phy_int_mask(struct i40e_hw *hw,
				     u16 mask,
				     struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_set_phy_int_mask *cmd =
		(struct i40e_aqc_set_phy_int_mask *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_set_phy_int_mask);

	cmd->event_mask = cpu_to_le16(mask);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_aq_set_phy_debug
 * @hw: pointer to the hw struct
 * @cmd_flags: debug command flags
 * @cmd_details: pointer to command details structure or NULL
 *
 * Reset the external PHY.
 **/
i40e_status i40e_aq_set_phy_debug(struct i40e_hw *hw, u8 cmd_flags,
				  struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_set_phy_debug *cmd =
		(struct i40e_aqc_set_phy_debug *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_set_phy_debug);

	cmd->command_flags = cmd_flags;

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_aq_add_vsi
 * @hw: pointer to the hw struct
 * @vsi_ctx: pointer to a vsi context struct
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
 * i40e_aq_set_default_vsi
 * @hw: pointer to the hw struct
 * @seid: vsi number
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_set_default_vsi(struct i40e_hw *hw,
				    u16 seid,
				    struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_set_vsi_promiscuous_modes *cmd =
		(struct i40e_aqc_set_vsi_promiscuous_modes *)
		&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_set_vsi_promiscuous_modes);

	cmd->promiscuous_flags = cpu_to_le16(I40E_AQC_SET_VSI_DEFAULT);
	cmd->valid_flags = cpu_to_le16(I40E_AQC_SET_VSI_DEFAULT);
	cmd->seid = cpu_to_le16(seid);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_aq_clear_default_vsi
 * @hw: pointer to the hw struct
 * @seid: vsi number
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_clear_default_vsi(struct i40e_hw *hw,
				      u16 seid,
				      struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_set_vsi_promiscuous_modes *cmd =
		(struct i40e_aqc_set_vsi_promiscuous_modes *)
		&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_set_vsi_promiscuous_modes);

	cmd->promiscuous_flags = cpu_to_le16(0);
	cmd->valid_flags = cpu_to_le16(I40E_AQC_SET_VSI_DEFAULT);
	cmd->seid = cpu_to_le16(seid);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_aq_set_vsi_unicast_promiscuous
 * @hw: pointer to the hw struct
 * @seid: vsi number
 * @set: set unicast promiscuous enable/disable
 * @cmd_details: pointer to command details structure or NULL
 * @rx_only_promisc: flag to decide if egress traffic gets mirrored in promisc
 **/
i40e_status i40e_aq_set_vsi_unicast_promiscuous(struct i40e_hw *hw,
				u16 seid, bool set,
				struct i40e_asq_cmd_details *cmd_details,
				bool rx_only_promisc)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_set_vsi_promiscuous_modes *cmd =
		(struct i40e_aqc_set_vsi_promiscuous_modes *)&desc.params.raw;
	i40e_status status;
	u16 flags = 0;

	i40e_fill_default_direct_cmd_desc(&desc,
					i40e_aqc_opc_set_vsi_promiscuous_modes);

	if (set) {
		flags |= I40E_AQC_SET_VSI_PROMISC_UNICAST;
		if (rx_only_promisc &&
		    (((hw->aq.api_maj_ver == 1) && (hw->aq.api_min_ver >= 5)) ||
		     (hw->aq.api_maj_ver > 1)))
			flags |= I40E_AQC_SET_VSI_PROMISC_TX;
	}

	cmd->promiscuous_flags = cpu_to_le16(flags);

	cmd->valid_flags = cpu_to_le16(I40E_AQC_SET_VSI_PROMISC_UNICAST);
	if (((hw->aq.api_maj_ver >= 1) && (hw->aq.api_min_ver >= 5)) ||
	    (hw->aq.api_maj_ver > 1))
		cmd->valid_flags |= cpu_to_le16(I40E_AQC_SET_VSI_PROMISC_TX);

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
 * i40e_aq_set_vsi_mc_promisc_on_vlan
 * @hw: pointer to the hw struct
 * @seid: vsi number
 * @enable: set MAC L2 layer unicast promiscuous enable/disable for a given VLAN
 * @vid: The VLAN tag filter - capture any multicast packet with this VLAN tag
 * @cmd_details: pointer to command details structure or NULL
 **/
enum i40e_status_code i40e_aq_set_vsi_mc_promisc_on_vlan(struct i40e_hw *hw,
							 u16 seid, bool enable,
							 u16 vid,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_set_vsi_promiscuous_modes *cmd =
		(struct i40e_aqc_set_vsi_promiscuous_modes *)&desc.params.raw;
	enum i40e_status_code status;
	u16 flags = 0;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_set_vsi_promiscuous_modes);

	if (enable)
		flags |= I40E_AQC_SET_VSI_PROMISC_MULTICAST;

	cmd->promiscuous_flags = cpu_to_le16(flags);
	cmd->valid_flags = cpu_to_le16(I40E_AQC_SET_VSI_PROMISC_MULTICAST);
	cmd->seid = cpu_to_le16(seid);
	cmd->vlan_tag = cpu_to_le16(vid | I40E_AQC_SET_VSI_VLAN_VALID);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_aq_set_vsi_uc_promisc_on_vlan
 * @hw: pointer to the hw struct
 * @seid: vsi number
 * @enable: set MAC L2 layer unicast promiscuous enable/disable for a given VLAN
 * @vid: The VLAN tag filter - capture any unicast packet with this VLAN tag
 * @cmd_details: pointer to command details structure or NULL
 **/
enum i40e_status_code i40e_aq_set_vsi_uc_promisc_on_vlan(struct i40e_hw *hw,
							 u16 seid, bool enable,
							 u16 vid,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_set_vsi_promiscuous_modes *cmd =
		(struct i40e_aqc_set_vsi_promiscuous_modes *)&desc.params.raw;
	enum i40e_status_code status;
	u16 flags = 0;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_set_vsi_promiscuous_modes);

	if (enable)
		flags |= I40E_AQC_SET_VSI_PROMISC_UNICAST;

	cmd->promiscuous_flags = cpu_to_le16(flags);
	cmd->valid_flags = cpu_to_le16(I40E_AQC_SET_VSI_PROMISC_UNICAST);
	cmd->seid = cpu_to_le16(seid);
	cmd->vlan_tag = cpu_to_le16(vid | I40E_AQC_SET_VSI_VLAN_VALID);

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
 * i40e_aq_set_vsi_vlan_promisc - control the VLAN promiscuous setting
 * @hw: pointer to the hw struct
 * @seid: vsi number
 * @enable: set MAC L2 layer unicast promiscuous enable/disable for a given VLAN
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_set_vsi_vlan_promisc(struct i40e_hw *hw,
				       u16 seid, bool enable,
				       struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_set_vsi_promiscuous_modes *cmd =
		(struct i40e_aqc_set_vsi_promiscuous_modes *)&desc.params.raw;
	i40e_status status;
	u16 flags = 0;

	i40e_fill_default_direct_cmd_desc(&desc,
					i40e_aqc_opc_set_vsi_promiscuous_modes);
	if (enable)
		flags |= I40E_AQC_SET_VSI_PROMISC_VLAN;

	cmd->promiscuous_flags = cpu_to_le16(flags);
	cmd->valid_flags = cpu_to_le16(I40E_AQC_SET_VSI_PROMISC_VLAN);
	cmd->seid = cpu_to_le16(seid);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_get_vsi_params - get VSI configuration info
 * @hw: pointer to the hw struct
 * @vsi_ctx: pointer to a vsi context struct
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_get_vsi_params(struct i40e_hw *hw,
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
					  i40e_aqc_opc_get_vsi_parameters);

	cmd->uplink_seid = cpu_to_le16(vsi_ctx->seid);

	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_BUF);

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
 * @vsi_ctx: pointer to a vsi context struct
 * @cmd_details: pointer to command details structure or NULL
 *
 * Update a VSI context.
 **/
i40e_status i40e_aq_update_vsi_params(struct i40e_hw *hw,
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
					  i40e_aqc_opc_update_vsi_parameters);
	cmd->uplink_seid = cpu_to_le16(vsi_ctx->seid);

	desc.flags |= cpu_to_le16((u16)(I40E_AQ_FLAG_BUF | I40E_AQ_FLAG_RD));

	status = i40e_asq_send_command(hw, &desc, &vsi_ctx->info,
				    sizeof(vsi_ctx->info), cmd_details);

	vsi_ctx->vsis_allocated = le16_to_cpu(resp->vsi_used);
	vsi_ctx->vsis_unallocated = le16_to_cpu(resp->vsi_free);

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
 * i40e_aq_set_switch_config
 * @hw: pointer to the hardware structure
 * @flags: bit flag values to set
 * @valid_flags: which bit flags to set
 * @cmd_details: pointer to command details structure or NULL
 *
 * Set switch configuration bits
 **/
enum i40e_status_code i40e_aq_set_switch_config(struct i40e_hw *hw,
						u16 flags,
						u16 valid_flags,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_set_switch_config *scfg =
		(struct i40e_aqc_set_switch_config *)&desc.params.raw;
	enum i40e_status_code status;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_set_switch_config);
	scfg->flags = cpu_to_le16(flags);
	scfg->valid_flags = cpu_to_le16(valid_flags);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_aq_get_firmware_version
 * @hw: pointer to the hw struct
 * @fw_major_version: firmware major version
 * @fw_minor_version: firmware minor version
 * @fw_build: firmware build number
 * @api_major_version: major queue version
 * @api_minor_version: minor queue version
 * @cmd_details: pointer to command details structure or NULL
 *
 * Get the firmware version from the admin queue commands
 **/
i40e_status i40e_aq_get_firmware_version(struct i40e_hw *hw,
				u16 *fw_major_version, u16 *fw_minor_version,
				u32 *fw_build,
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
		if (fw_major_version)
			*fw_major_version = le16_to_cpu(resp->fw_major);
		if (fw_minor_version)
			*fw_minor_version = le16_to_cpu(resp->fw_minor);
		if (fw_build)
			*fw_build = le32_to_cpu(resp->fw_build);
		if (api_major_version)
			*api_major_version = le16_to_cpu(resp->api_major);
		if (api_minor_version)
			*api_minor_version = le16_to_cpu(resp->api_minor);
	}

	return status;
}

/**
 * i40e_aq_send_driver_version
 * @hw: pointer to the hw struct
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
	u16 len;

	if (dv == NULL)
		return I40E_ERR_PARAM;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_driver_version);

	desc.flags |= cpu_to_le16(I40E_AQ_FLAG_BUF | I40E_AQ_FLAG_RD);
	cmd->driver_major_ver = dv->major_version;
	cmd->driver_minor_ver = dv->minor_version;
	cmd->driver_build_ver = dv->build_version;
	cmd->driver_subbuild_ver = dv->subbuild_version;

	len = 0;
	while (len < sizeof(dv->driver_string) &&
	       (dv->driver_string[len] < 0x80) &&
	       dv->driver_string[len])
		len++;
	status = i40e_asq_send_command(hw, &desc, dv->driver_string,
				       len, cmd_details);

	return status;
}

/**
 * i40e_get_link_status - get status of the HW network link
 * @hw: pointer to the hw struct
 * @link_up: pointer to bool (true/false = linkup/linkdown)
 *
 * Variable link_up true if link is up, false if link is down.
 * The variable link_up is invalid if returned value of status != 0
 *
 * Side effect: LinkStatusEvent reporting becomes enabled
 **/
i40e_status i40e_get_link_status(struct i40e_hw *hw, bool *link_up)
{
	i40e_status status = 0;

	if (hw->phy.get_link_info) {
		status = i40e_update_link_info(hw);

		if (status)
			i40e_debug(hw, I40E_DEBUG_LINK, "get link failed: status %d\n",
				   status);
	}

	*link_up = hw->phy.link_info.link_info & I40E_AQ_LINK_UP;

	return status;
}

/**
 * i40e_updatelink_status - update status of the HW network link
 * @hw: pointer to the hw struct
 **/
i40e_status i40e_update_link_info(struct i40e_hw *hw)
{
	struct i40e_aq_get_phy_abilities_resp abilities;
	i40e_status status = 0;

	status = i40e_aq_get_link_info(hw, true, NULL, NULL);
	if (status)
		return status;

	/* extra checking needed to ensure link info to user is timely */
	if ((hw->phy.link_info.link_info & I40E_AQ_MEDIA_AVAILABLE) &&
	    ((hw->phy.link_info.link_info & I40E_AQ_LINK_UP) ||
	     !(hw->phy.link_info_old.link_info & I40E_AQ_LINK_UP))) {
		status = i40e_aq_get_phy_capabilities(hw, false, false,
						      &abilities, NULL);
		if (status)
			return status;

		memcpy(hw->phy.link_info.module_type, &abilities.module_type,
		       sizeof(hw->phy.link_info.module_type));
	}

	return status;
}

/**
 * i40e_aq_add_veb - Insert a VEB between the VSI and the MAC
 * @hw: pointer to the hw struct
 * @uplink_seid: the MAC or other gizmo SEID
 * @downlink_seid: the VSI SEID
 * @enabled_tc: bitmap of TCs to be enabled
 * @default_port: true for default port VSI, false for control port
 * @veb_seid: pointer to where to put the resulting VEB SEID
 * @enable_stats: true to turn on VEB stats
 * @cmd_details: pointer to command details structure or NULL
 *
 * This asks the FW to add a VEB between the uplink and downlink
 * elements.  If the uplink SEID is 0, this will be a floating VEB.
 **/
i40e_status i40e_aq_add_veb(struct i40e_hw *hw, u16 uplink_seid,
				u16 downlink_seid, u8 enabled_tc,
				bool default_port, u16 *veb_seid,
				bool enable_stats,
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

	/* reverse logic here: set the bitflag to disable the stats */
	if (!enable_stats)
		veb_flags |= I40E_AQC_ADD_VEB_ENABLE_DISABLE_STATS;

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
 * @floating: set to true if the VEB is floating
 * @statistic_index: index of the stats counter block for this VEB
 * @vebs_used: number of VEB's used by function
 * @vebs_free: total VEB's not reserved by any function
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
	int i;

	if (count == 0 || !mv_list || !hw)
		return I40E_ERR_PARAM;

	buf_size = count * sizeof(*mv_list);

	/* prep the rest of the request */
	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_add_macvlan);
	cmd->num_addresses = cpu_to_le16(count);
	cmd->seid[0] = cpu_to_le16(I40E_AQC_MACVLAN_CMD_SEID_VALID | seid);
	cmd->seid[1] = 0;
	cmd->seid[2] = 0;

	for (i = 0; i < count; i++)
		if (is_multicast_ether_addr(mv_list[i].mac_addr))
			mv_list[i].flags |=
			       cpu_to_le16(I40E_AQC_MACVLAN_ADD_USE_SHARED_MAC);

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

	buf_size = count * sizeof(*mv_list);

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
 * i40e_mirrorrule_op - Internal helper function to add/delete mirror rule
 * @hw: pointer to the hw struct
 * @opcode: AQ opcode for add or delete mirror rule
 * @sw_seid: Switch SEID (to which rule refers)
 * @rule_type: Rule Type (ingress/egress/VLAN)
 * @id: Destination VSI SEID or Rule ID
 * @count: length of the list
 * @mr_list: list of mirrored VSI SEIDs or VLAN IDs
 * @cmd_details: pointer to command details structure or NULL
 * @rule_id: Rule ID returned from FW
 * @rule_used: Number of rules used in internal switch
 * @rule_free: Number of rules free in internal switch
 *
 * Add/Delete a mirror rule to a specific switch. Mirror rules are supported for
 * VEBs/VEPA elements only
 **/
static i40e_status i40e_mirrorrule_op(struct i40e_hw *hw,
				u16 opcode, u16 sw_seid, u16 rule_type, u16 id,
				u16 count, __le16 *mr_list,
				struct i40e_asq_cmd_details *cmd_details,
				u16 *rule_id, u16 *rules_used, u16 *rules_free)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_add_delete_mirror_rule *cmd =
		(struct i40e_aqc_add_delete_mirror_rule *)&desc.params.raw;
	struct i40e_aqc_add_delete_mirror_rule_completion *resp =
	(struct i40e_aqc_add_delete_mirror_rule_completion *)&desc.params.raw;
	i40e_status status;
	u16 buf_size;

	buf_size = count * sizeof(*mr_list);

	/* prep the rest of the request */
	i40e_fill_default_direct_cmd_desc(&desc, opcode);
	cmd->seid = cpu_to_le16(sw_seid);
	cmd->rule_type = cpu_to_le16(rule_type &
				     I40E_AQC_MIRROR_RULE_TYPE_MASK);
	cmd->num_entries = cpu_to_le16(count);
	/* Dest VSI for add, rule_id for delete */
	cmd->destination = cpu_to_le16(id);
	if (mr_list) {
		desc.flags |= cpu_to_le16((u16)(I40E_AQ_FLAG_BUF |
						I40E_AQ_FLAG_RD));
		if (buf_size > I40E_AQ_LARGE_BUF)
			desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);
	}

	status = i40e_asq_send_command(hw, &desc, mr_list, buf_size,
				       cmd_details);
	if (!status ||
	    hw->aq.asq_last_status == I40E_AQ_RC_ENOSPC) {
		if (rule_id)
			*rule_id = le16_to_cpu(resp->rule_id);
		if (rules_used)
			*rules_used = le16_to_cpu(resp->mirror_rules_used);
		if (rules_free)
			*rules_free = le16_to_cpu(resp->mirror_rules_free);
	}
	return status;
}

/**
 * i40e_aq_add_mirrorrule - add a mirror rule
 * @hw: pointer to the hw struct
 * @sw_seid: Switch SEID (to which rule refers)
 * @rule_type: Rule Type (ingress/egress/VLAN)
 * @dest_vsi: SEID of VSI to which packets will be mirrored
 * @count: length of the list
 * @mr_list: list of mirrored VSI SEIDs or VLAN IDs
 * @cmd_details: pointer to command details structure or NULL
 * @rule_id: Rule ID returned from FW
 * @rule_used: Number of rules used in internal switch
 * @rule_free: Number of rules free in internal switch
 *
 * Add mirror rule. Mirror rules are supported for VEBs or VEPA elements only
 **/
i40e_status i40e_aq_add_mirrorrule(struct i40e_hw *hw, u16 sw_seid,
			u16 rule_type, u16 dest_vsi, u16 count, __le16 *mr_list,
			struct i40e_asq_cmd_details *cmd_details,
			u16 *rule_id, u16 *rules_used, u16 *rules_free)
{
	if (!(rule_type == I40E_AQC_MIRROR_RULE_TYPE_ALL_INGRESS ||
	    rule_type == I40E_AQC_MIRROR_RULE_TYPE_ALL_EGRESS)) {
		if (count == 0 || !mr_list)
			return I40E_ERR_PARAM;
	}

	return i40e_mirrorrule_op(hw, i40e_aqc_opc_add_mirror_rule, sw_seid,
				  rule_type, dest_vsi, count, mr_list,
				  cmd_details, rule_id, rules_used, rules_free);
}

/**
 * i40e_aq_delete_mirrorrule - delete a mirror rule
 * @hw: pointer to the hw struct
 * @sw_seid: Switch SEID (to which rule refers)
 * @rule_type: Rule Type (ingress/egress/VLAN)
 * @count: length of the list
 * @rule_id: Rule ID that is returned in the receive desc as part of
 *		add_mirrorrule.
 * @mr_list: list of mirrored VLAN IDs to be removed
 * @cmd_details: pointer to command details structure or NULL
 * @rule_used: Number of rules used in internal switch
 * @rule_free: Number of rules free in internal switch
 *
 * Delete a mirror rule. Mirror rules are supported for VEBs/VEPA elements only
 **/
i40e_status i40e_aq_delete_mirrorrule(struct i40e_hw *hw, u16 sw_seid,
			u16 rule_type, u16 rule_id, u16 count, __le16 *mr_list,
			struct i40e_asq_cmd_details *cmd_details,
			u16 *rules_used, u16 *rules_free)
{
	/* Rule ID has to be valid except rule_type: INGRESS VLAN mirroring */
	if (rule_type == I40E_AQC_MIRROR_RULE_TYPE_VLAN) {
		/* count and mr_list shall be valid for rule_type INGRESS VLAN
		 * mirroring. For other rule_type, count and rule_type should
		 * not matter.
		 */
		if (count == 0 || !mr_list)
			return I40E_ERR_PARAM;
	}

	return i40e_mirrorrule_op(hw, i40e_aqc_opc_delete_mirror_rule, sw_seid,
				  rule_type, rule_id, count, mr_list,
				  cmd_details, NULL, rules_used, rules_free);
}

/**
 * i40e_aq_send_msg_to_vf
 * @hw: pointer to the hardware structure
 * @vfid: VF id to send msg
 * @v_opcode: opcodes for VF-PF communication
 * @v_retval: return error code
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
 * i40e_aq_debug_read_register
 * @hw: pointer to the hw struct
 * @reg_addr: register address
 * @reg_val: register value
 * @cmd_details: pointer to command details structure or NULL
 *
 * Read the register using the admin queue commands
 **/
i40e_status i40e_aq_debug_read_register(struct i40e_hw *hw,
				u32 reg_addr, u64 *reg_val,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_debug_reg_read_write *cmd_resp =
		(struct i40e_aqc_debug_reg_read_write *)&desc.params.raw;
	i40e_status status;

	if (reg_val == NULL)
		return I40E_ERR_PARAM;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_debug_read_reg);

	cmd_resp->address = cpu_to_le32(reg_addr);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	if (!status) {
		*reg_val = ((u64)le32_to_cpu(cmd_resp->value_high) << 32) |
			   (u64)le32_to_cpu(cmd_resp->value_low);
	}

	return status;
}

/**
 * i40e_aq_debug_write_register
 * @hw: pointer to the hw struct
 * @reg_addr: register address
 * @reg_val: register value
 * @cmd_details: pointer to command details structure or NULL
 *
 * Write to a register using the admin queue commands
 **/
i40e_status i40e_aq_debug_write_register(struct i40e_hw *hw,
					u32 reg_addr, u64 reg_val,
					struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_debug_reg_read_write *cmd =
		(struct i40e_aqc_debug_reg_read_write *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_debug_write_reg);

	cmd->address = cpu_to_le32(reg_addr);
	cmd->value_high = cpu_to_le32((u32)(reg_val >> 32));
	cmd->value_low = cpu_to_le32((u32)(reg_val & 0xFFFFFFFF));

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

/**
 * i40e_aq_erase_nvm
 * @hw: pointer to the hw struct
 * @module_pointer: module pointer location in words from the NVM beginning
 * @offset: offset in the module (expressed in 4 KB from module's beginning)
 * @length: length of the section to be erased (expressed in 4 KB)
 * @last_command: tells if this is the last command in a series
 * @cmd_details: pointer to command details structure or NULL
 *
 * Erase the NVM sector using the admin queue commands
 **/
i40e_status i40e_aq_erase_nvm(struct i40e_hw *hw, u8 module_pointer,
			      u32 offset, u16 length, bool last_command,
			      struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_nvm_update *cmd =
		(struct i40e_aqc_nvm_update *)&desc.params.raw;
	i40e_status status;

	/* In offset the highest byte must be zeroed. */
	if (offset & 0xFF000000) {
		status = I40E_ERR_PARAM;
		goto i40e_aq_erase_nvm_exit;
	}

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_nvm_erase);

	/* If this is the last command in a series, set the proper flag. */
	if (last_command)
		cmd->command_flags |= I40E_AQ_NVM_LAST_CMD;
	cmd->module_pointer = module_pointer;
	cmd->offset = cpu_to_le32(offset);
	cmd->length = cpu_to_le16(length);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

i40e_aq_erase_nvm_exit:
	return status;
}

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
	u32 valid_functions, num_functions;
	u32 number, logical_id, phys_id;
	struct i40e_hw_capabilities *p;
	u8 major_rev;
	u32 i = 0;
	u16 id;

	cap = (struct i40e_aqc_list_capabilities_element_resp *) buff;

	if (list_type_opc == i40e_aqc_opc_list_dev_capabilities)
		p = &hw->dev_caps;
	else if (list_type_opc == i40e_aqc_opc_list_func_capabilities)
		p = &hw->func_caps;
	else
		return;

	for (i = 0; i < cap_count; i++, cap++) {
		id = le16_to_cpu(cap->id);
		number = le32_to_cpu(cap->number);
		logical_id = le32_to_cpu(cap->logical_id);
		phys_id = le32_to_cpu(cap->phys_id);
		major_rev = cap->major_rev;

		switch (id) {
		case I40E_AQ_CAP_ID_SWITCH_MODE:
			p->switch_mode = number;
			break;
		case I40E_AQ_CAP_ID_MNG_MODE:
			p->management_mode = number;
			if (major_rev > 1) {
				p->mng_protocols_over_mctp = logical_id;
				i40e_debug(hw, I40E_DEBUG_INIT,
					   "HW Capability: Protocols over MCTP = %d\n",
					   p->mng_protocols_over_mctp);
			} else {
				p->mng_protocols_over_mctp = 0;
			}
			break;
		case I40E_AQ_CAP_ID_NPAR_ACTIVE:
			p->npar_enable = number;
			break;
		case I40E_AQ_CAP_ID_OS2BMC_CAP:
			p->os2bmc = number;
			break;
		case I40E_AQ_CAP_ID_FUNCTIONS_VALID:
			p->valid_functions = number;
			break;
		case I40E_AQ_CAP_ID_SRIOV:
			if (number == 1)
				p->sr_iov_1_1 = true;
			break;
		case I40E_AQ_CAP_ID_VF:
			p->num_vfs = number;
			p->vf_base_id = logical_id;
			break;
		case I40E_AQ_CAP_ID_VMDQ:
			if (number == 1)
				p->vmdq = true;
			break;
		case I40E_AQ_CAP_ID_8021QBG:
			if (number == 1)
				p->evb_802_1_qbg = true;
			break;
		case I40E_AQ_CAP_ID_8021QBR:
			if (number == 1)
				p->evb_802_1_qbh = true;
			break;
		case I40E_AQ_CAP_ID_VSI:
			p->num_vsis = number;
			break;
		case I40E_AQ_CAP_ID_DCB:
			if (number == 1) {
				p->dcb = true;
				p->enabled_tcmap = logical_id;
				p->maxtc = phys_id;
			}
			break;
		case I40E_AQ_CAP_ID_FCOE:
			if (number == 1)
				p->fcoe = true;
			break;
		case I40E_AQ_CAP_ID_ISCSI:
			if (number == 1)
				p->iscsi = true;
			break;
		case I40E_AQ_CAP_ID_RSS:
			p->rss = true;
			p->rss_table_size = number;
			p->rss_table_entry_width = logical_id;
			break;
		case I40E_AQ_CAP_ID_RXQ:
			p->num_rx_qp = number;
			p->base_queue = phys_id;
			break;
		case I40E_AQ_CAP_ID_TXQ:
			p->num_tx_qp = number;
			p->base_queue = phys_id;
			break;
		case I40E_AQ_CAP_ID_MSIX:
			p->num_msix_vectors = number;
			i40e_debug(hw, I40E_DEBUG_INIT,
				   "HW Capability: MSIX vector count = %d\n",
				   p->num_msix_vectors);
			break;
		case I40E_AQ_CAP_ID_VF_MSIX:
			p->num_msix_vectors_vf = number;
			break;
		case I40E_AQ_CAP_ID_FLEX10:
			if (major_rev == 1) {
				if (number == 1) {
					p->flex10_enable = true;
					p->flex10_capable = true;
				}
			} else {
				/* Capability revision >= 2 */
				if (number & 1)
					p->flex10_enable = true;
				if (number & 2)
					p->flex10_capable = true;
			}
			p->flex10_mode = logical_id;
			p->flex10_status = phys_id;
			break;
		case I40E_AQ_CAP_ID_CEM:
			if (number == 1)
				p->mgmt_cem = true;
			break;
		case I40E_AQ_CAP_ID_IWARP:
			if (number == 1)
				p->iwarp = true;
			break;
		case I40E_AQ_CAP_ID_LED:
			if (phys_id < I40E_HW_CAP_MAX_GPIO)
				p->led[phys_id] = true;
			break;
		case I40E_AQ_CAP_ID_SDP:
			if (phys_id < I40E_HW_CAP_MAX_GPIO)
				p->sdp[phys_id] = true;
			break;
		case I40E_AQ_CAP_ID_MDIO:
			if (number == 1) {
				p->mdio_port_num = phys_id;
				p->mdio_port_mode = logical_id;
			}
			break;
		case I40E_AQ_CAP_ID_1588:
			if (number == 1)
				p->ieee_1588 = true;
			break;
		case I40E_AQ_CAP_ID_FLOW_DIRECTOR:
			p->fd = true;
			p->fd_filters_guaranteed = number;
			p->fd_filters_best_effort = logical_id;
			break;
		case I40E_AQ_CAP_ID_WSR_PROT:
			p->wr_csr_prot = (u64)number;
			p->wr_csr_prot |= (u64)logical_id << 32;
			break;
		case I40E_AQ_CAP_ID_NVM_MGMT:
			if (number & I40E_NVM_MGMT_SEC_REV_DISABLED)
				p->sec_rev_disabled = true;
			if (number & I40E_NVM_MGMT_UPDATE_DISABLED)
				p->update_disabled = true;
			break;
		default:
			break;
		}
	}

	if (p->fcoe)
		i40e_debug(hw, I40E_DEBUG_ALL, "device is FCoE capable\n");

	/* Software override ensuring FCoE is disabled if npar or mfp
	 * mode because it is not supported in these modes.
	 */
	if (p->npar_enable || p->flex10_enable)
		p->fcoe = false;

	/* count the enabled ports (aka the "not disabled" ports) */
	hw->num_ports = 0;
	for (i = 0; i < 4; i++) {
		u32 port_cfg_reg = I40E_PRTGEN_CNF + (4 * i);
		u64 port_cfg = 0;

		/* use AQ read to get the physical register offset instead
		 * of the port relative offset
		 */
		i40e_aq_debug_read_register(hw, port_cfg_reg, &port_cfg, NULL);
		if (!(port_cfg & I40E_PRTGEN_CNF_PORT_DIS_MASK))
			hw->num_ports++;
	}

	valid_functions = p->valid_functions;
	num_functions = 0;
	while (valid_functions) {
		if (valid_functions & 1)
			num_functions++;
		valid_functions >>= 1;
	}

	/* partition id is 1-based, and functions are evenly spread
	 * across the ports as partitions
	 */
	if (hw->num_ports != 0) {
		hw->partition_id = (hw->pf_id / hw->num_ports) + 1;
		hw->num_partitions = num_functions / hw->num_ports;
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
	struct i40e_aq_desc desc;
	i40e_status status = 0;

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
 * i40e_aq_update_nvm
 * @hw: pointer to the hw struct
 * @module_pointer: module pointer location in words from the NVM beginning
 * @offset: byte offset from the module beginning
 * @length: length of the section to be written (in bytes from the offset)
 * @data: command buffer (size [bytes] = length)
 * @last_command: tells if this is the last command in a series
 * @cmd_details: pointer to command details structure or NULL
 *
 * Update the NVM using the admin queue commands
 **/
i40e_status i40e_aq_update_nvm(struct i40e_hw *hw, u8 module_pointer,
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
		goto i40e_aq_update_nvm_exit;
	}

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_nvm_update);

	/* If this is the last command in a series, set the proper flag. */
	if (last_command)
		cmd->command_flags |= I40E_AQ_NVM_LAST_CMD;
	cmd->module_pointer = module_pointer;
	cmd->offset = cpu_to_le32(offset);
	cmd->length = cpu_to_le16(length);

	desc.flags |= cpu_to_le16((u16)(I40E_AQ_FLAG_BUF | I40E_AQ_FLAG_RD));
	if (length > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);

	status = i40e_asq_send_command(hw, &desc, data, length, cmd_details);

i40e_aq_update_nvm_exit:
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
 * i40e_aq_get_cee_dcb_config
 * @hw: pointer to the hw struct
 * @buff: response buffer that stores CEE operational configuration
 * @buff_size: size of the buffer passed
 * @cmd_details: pointer to command details structure or NULL
 *
 * Get CEE DCBX mode operational configuration from firmware
 **/
i40e_status i40e_aq_get_cee_dcb_config(struct i40e_hw *hw,
				       void *buff, u16 buff_size,
				       struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	i40e_status status;

	if (buff_size == 0 || !buff)
		return I40E_ERR_PARAM;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_get_cee_dcb_cfg);

	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_BUF);
	status = i40e_asq_send_command(hw, &desc, (void *)buff, buff_size,
				       cmd_details);

	return status;
}

/**
 * i40e_aq_add_udp_tunnel
 * @hw: pointer to the hw struct
 * @udp_port: the UDP port to add
 * @header_len: length of the tunneling header length in DWords
 * @protocol_index: protocol index type
 * @filter_index: pointer to filter index
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_add_udp_tunnel(struct i40e_hw *hw,
				u16 udp_port, u8 protocol_index,
				u8 *filter_index,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_add_udp_tunnel *cmd =
		(struct i40e_aqc_add_udp_tunnel *)&desc.params.raw;
	struct i40e_aqc_del_udp_tunnel_completion *resp =
		(struct i40e_aqc_del_udp_tunnel_completion *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_add_udp_tunnel);

	cmd->udp_port = cpu_to_le16(udp_port);
	cmd->protocol_type = protocol_index;

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	if (!status && filter_index)
		*filter_index = resp->index;

	return status;
}

/**
 * i40e_aq_del_udp_tunnel
 * @hw: pointer to the hw struct
 * @index: filter index
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_del_udp_tunnel(struct i40e_hw *hw, u8 index,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_remove_udp_tunnel *cmd =
		(struct i40e_aqc_remove_udp_tunnel *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_del_udp_tunnel);

	cmd->index = index;

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
 * i40e_aq_dcb_updated - DCB Updated Command
 * @hw: pointer to the hw struct
 * @cmd_details: pointer to command details structure or NULL
 *
 * EMP will return when the shared RPB settings have been
 * recomputed and modified. The retval field in the descriptor
 * will be set to 0 when RPB is modified.
 **/
i40e_status i40e_aq_dcb_updated(struct i40e_hw *hw,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_dcb_updated);

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
 * i40e_aq_config_vsi_bw_limit - Configure VSI BW Limit
 * @hw: pointer to the hw struct
 * @seid: VSI seid
 * @credit: BW limit credits (0 = disabled)
 * @max_credit: Max BW limit credits
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_config_vsi_bw_limit(struct i40e_hw *hw,
				u16 seid, u16 credit, u8 max_credit,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_configure_vsi_bw_limit *cmd =
		(struct i40e_aqc_configure_vsi_bw_limit *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_configure_vsi_bw_limit);

	cmd->vsi_seid = cpu_to_le16(seid);
	cmd->credit = cpu_to_le16(credit);
	cmd->max_credit = max_credit;

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

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
 * i40e_aq_config_switch_comp_ets - Enable/Disable/Modify ETS on the port
 * @hw: pointer to the hw struct
 * @seid: seid of the switching component connected to Physical Port
 * @ets_data: Buffer holding ETS parameters
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_config_switch_comp_ets(struct i40e_hw *hw,
		u16 seid,
		struct i40e_aqc_configure_switching_comp_ets_data *ets_data,
		enum i40e_admin_queue_opc opcode,
		struct i40e_asq_cmd_details *cmd_details)
{
	return i40e_aq_tx_sched_cmd(hw, seid, (void *)ets_data,
				    sizeof(*ets_data), opcode, cmd_details);
}

/**
 * i40e_aq_config_switch_comp_bw_config - Config Switch comp BW Alloc per TC
 * @hw: pointer to the hw struct
 * @seid: seid of the switching component
 * @bw_data: Buffer holding enabled TCs, relative/absolute TC BW limit/credits
 * @cmd_details: pointer to command details structure or NULL
 **/
i40e_status i40e_aq_config_switch_comp_bw_config(struct i40e_hw *hw,
	u16 seid,
	struct i40e_aqc_configure_switching_comp_bw_config_data *bw_data,
	struct i40e_asq_cmd_details *cmd_details)
{
	return i40e_aq_tx_sched_cmd(hw, seid, (void *)bw_data, sizeof(*bw_data),
			    i40e_aqc_opc_configure_switching_comp_bw_config,
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
	u32 fcoe_fmax;
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
	val = i40e_read_rx_ctl(hw, I40E_PFQF_CTL_0);

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

	i40e_write_rx_ctl(hw, I40E_PFQF_CTL_0, val);

	return 0;
}

/**
 * i40e_aq_add_rem_control_packet_filter - Add or Remove Control Packet Filter
 * @hw: pointer to the hw struct
 * @mac_addr: MAC address to use in the filter
 * @ethtype: Ethertype to use in the filter
 * @flags: Flags that needs to be applied to the filter
 * @vsi_seid: seid of the control VSI
 * @queue: VSI queue number to send the packet to
 * @is_add: Add control packet filter if True else remove
 * @stats: Structure to hold information on control filter counts
 * @cmd_details: pointer to command details structure or NULL
 *
 * This command will Add or Remove control packet filter for a control VSI.
 * In return it will update the total number of perfect filter count in
 * the stats member.
 **/
i40e_status i40e_aq_add_rem_control_packet_filter(struct i40e_hw *hw,
				u8 *mac_addr, u16 ethtype, u16 flags,
				u16 vsi_seid, u16 queue, bool is_add,
				struct i40e_control_filter_stats *stats,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_add_remove_control_packet_filter *cmd =
		(struct i40e_aqc_add_remove_control_packet_filter *)
		&desc.params.raw;
	struct i40e_aqc_add_remove_control_packet_filter_completion *resp =
		(struct i40e_aqc_add_remove_control_packet_filter_completion *)
		&desc.params.raw;
	i40e_status status;

	if (vsi_seid == 0)
		return I40E_ERR_PARAM;

	if (is_add) {
		i40e_fill_default_direct_cmd_desc(&desc,
				i40e_aqc_opc_add_control_packet_filter);
		cmd->queue = cpu_to_le16(queue);
	} else {
		i40e_fill_default_direct_cmd_desc(&desc,
				i40e_aqc_opc_remove_control_packet_filter);
	}

	if (mac_addr)
		ether_addr_copy(cmd->mac, mac_addr);

	cmd->etype = cpu_to_le16(ethtype);
	cmd->flags = cpu_to_le16(flags);
	cmd->seid = cpu_to_le16(vsi_seid);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	if (!status && stats) {
		stats->mac_etype_used = le16_to_cpu(resp->mac_etype_used);
		stats->etype_used = le16_to_cpu(resp->etype_used);
		stats->mac_etype_free = le16_to_cpu(resp->mac_etype_free);
		stats->etype_free = le16_to_cpu(resp->etype_free);
	}

	return status;
}

/**
 * i40e_add_filter_to_drop_tx_flow_control_frames- filter to drop flow control
 * @hw: pointer to the hw struct
 * @seid: VSI seid to add ethertype filter from
 **/
#define I40E_FLOW_CONTROL_ETHTYPE 0x8808
void i40e_add_filter_to_drop_tx_flow_control_frames(struct i40e_hw *hw,
						    u16 seid)
{
	u16 flag = I40E_AQC_ADD_CONTROL_PACKET_FLAGS_IGNORE_MAC |
		   I40E_AQC_ADD_CONTROL_PACKET_FLAGS_DROP |
		   I40E_AQC_ADD_CONTROL_PACKET_FLAGS_TX;
	u16 ethtype = I40E_FLOW_CONTROL_ETHTYPE;
	i40e_status status;

	status = i40e_aq_add_rem_control_packet_filter(hw, NULL, ethtype, flag,
						       seid, 0, true, NULL,
						       NULL);
	if (status)
		hw_dbg(hw, "Ethtype Filter Add failed: Error pruning Tx flow control frames\n");
}

/**
 * i40e_aq_alternate_read
 * @hw: pointer to the hardware structure
 * @reg_addr0: address of first dword to be read
 * @reg_val0: pointer for data read from 'reg_addr0'
 * @reg_addr1: address of second dword to be read
 * @reg_val1: pointer for data read from 'reg_addr1'
 *
 * Read one or two dwords from alternate structure. Fields are indicated
 * by 'reg_addr0' and 'reg_addr1' register numbers. If 'reg_val1' pointer
 * is not passed then only register at 'reg_addr0' is read.
 *
 **/
static i40e_status i40e_aq_alternate_read(struct i40e_hw *hw,
					  u32 reg_addr0, u32 *reg_val0,
					  u32 reg_addr1, u32 *reg_val1)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_alternate_write *cmd_resp =
		(struct i40e_aqc_alternate_write *)&desc.params.raw;
	i40e_status status;

	if (!reg_val0)
		return I40E_ERR_PARAM;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_alternate_read);
	cmd_resp->address0 = cpu_to_le32(reg_addr0);
	cmd_resp->address1 = cpu_to_le32(reg_addr1);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, NULL);

	if (!status) {
		*reg_val0 = le32_to_cpu(cmd_resp->data0);

		if (reg_val1)
			*reg_val1 = le32_to_cpu(cmd_resp->data1);
	}

	return status;
}

/**
 * i40e_aq_resume_port_tx
 * @hw: pointer to the hardware structure
 * @cmd_details: pointer to command details structure or NULL
 *
 * Resume port's Tx traffic
 **/
i40e_status i40e_aq_resume_port_tx(struct i40e_hw *hw,
				   struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_resume_port_tx);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_set_pci_config_data - store PCI bus info
 * @hw: pointer to hardware structure
 * @link_status: the link status word from PCI config space
 *
 * Stores the PCI bus info (speed, width, type) within the i40e_hw structure
 **/
void i40e_set_pci_config_data(struct i40e_hw *hw, u16 link_status)
{
	hw->bus.type = i40e_bus_type_pci_express;

	switch (link_status & PCI_EXP_LNKSTA_NLW) {
	case PCI_EXP_LNKSTA_NLW_X1:
		hw->bus.width = i40e_bus_width_pcie_x1;
		break;
	case PCI_EXP_LNKSTA_NLW_X2:
		hw->bus.width = i40e_bus_width_pcie_x2;
		break;
	case PCI_EXP_LNKSTA_NLW_X4:
		hw->bus.width = i40e_bus_width_pcie_x4;
		break;
	case PCI_EXP_LNKSTA_NLW_X8:
		hw->bus.width = i40e_bus_width_pcie_x8;
		break;
	default:
		hw->bus.width = i40e_bus_width_unknown;
		break;
	}

	switch (link_status & PCI_EXP_LNKSTA_CLS) {
	case PCI_EXP_LNKSTA_CLS_2_5GB:
		hw->bus.speed = i40e_bus_speed_2500;
		break;
	case PCI_EXP_LNKSTA_CLS_5_0GB:
		hw->bus.speed = i40e_bus_speed_5000;
		break;
	case PCI_EXP_LNKSTA_CLS_8_0GB:
		hw->bus.speed = i40e_bus_speed_8000;
		break;
	default:
		hw->bus.speed = i40e_bus_speed_unknown;
		break;
	}
}

/**
 * i40e_aq_debug_dump
 * @hw: pointer to the hardware structure
 * @cluster_id: specific cluster to dump
 * @table_id: table id within cluster
 * @start_index: index of line in the block to read
 * @buff_size: dump buffer size
 * @buff: dump buffer
 * @ret_buff_size: actual buffer size returned
 * @ret_next_table: next block to read
 * @ret_next_index: next index to read
 *
 * Dump internal FW/HW data for debug purposes.
 *
 **/
i40e_status i40e_aq_debug_dump(struct i40e_hw *hw, u8 cluster_id,
			       u8 table_id, u32 start_index, u16 buff_size,
			       void *buff, u16 *ret_buff_size,
			       u8 *ret_next_table, u32 *ret_next_index,
			       struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_debug_dump_internals *cmd =
		(struct i40e_aqc_debug_dump_internals *)&desc.params.raw;
	struct i40e_aqc_debug_dump_internals *resp =
		(struct i40e_aqc_debug_dump_internals *)&desc.params.raw;
	i40e_status status;

	if (buff_size == 0 || !buff)
		return I40E_ERR_PARAM;

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_debug_dump_internals);
	/* Indirect Command */
	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_BUF);
	if (buff_size > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);

	cmd->cluster_id = cluster_id;
	cmd->table_id = table_id;
	cmd->idx = cpu_to_le32(start_index);

	desc.datalen = cpu_to_le16(buff_size);

	status = i40e_asq_send_command(hw, &desc, buff, buff_size, cmd_details);
	if (!status) {
		if (ret_buff_size)
			*ret_buff_size = le16_to_cpu(desc.datalen);
		if (ret_next_table)
			*ret_next_table = resp->table_id;
		if (ret_next_index)
			*ret_next_index = le32_to_cpu(resp->idx);
	}

	return status;
}

/**
 * i40e_read_bw_from_alt_ram
 * @hw: pointer to the hardware structure
 * @max_bw: pointer for max_bw read
 * @min_bw: pointer for min_bw read
 * @min_valid: pointer for bool that is true if min_bw is a valid value
 * @max_valid: pointer for bool that is true if max_bw is a valid value
 *
 * Read bw from the alternate ram for the given pf
 **/
i40e_status i40e_read_bw_from_alt_ram(struct i40e_hw *hw,
				      u32 *max_bw, u32 *min_bw,
				      bool *min_valid, bool *max_valid)
{
	i40e_status status;
	u32 max_bw_addr, min_bw_addr;

	/* Calculate the address of the min/max bw registers */
	max_bw_addr = I40E_ALT_STRUCT_FIRST_PF_OFFSET +
		      I40E_ALT_STRUCT_MAX_BW_OFFSET +
		      (I40E_ALT_STRUCT_DWORDS_PER_PF * hw->pf_id);
	min_bw_addr = I40E_ALT_STRUCT_FIRST_PF_OFFSET +
		      I40E_ALT_STRUCT_MIN_BW_OFFSET +
		      (I40E_ALT_STRUCT_DWORDS_PER_PF * hw->pf_id);

	/* Read the bandwidths from alt ram */
	status = i40e_aq_alternate_read(hw, max_bw_addr, max_bw,
					min_bw_addr, min_bw);

	if (*min_bw & I40E_ALT_BW_VALID_MASK)
		*min_valid = true;
	else
		*min_valid = false;

	if (*max_bw & I40E_ALT_BW_VALID_MASK)
		*max_valid = true;
	else
		*max_valid = false;

	return status;
}

/**
 * i40e_aq_configure_partition_bw
 * @hw: pointer to the hardware structure
 * @bw_data: Buffer holding valid pfs and bw limits
 * @cmd_details: pointer to command details
 *
 * Configure partitions guaranteed/max bw
 **/
i40e_status i40e_aq_configure_partition_bw(struct i40e_hw *hw,
			struct i40e_aqc_configure_partition_bw_data *bw_data,
			struct i40e_asq_cmd_details *cmd_details)
{
	i40e_status status;
	struct i40e_aq_desc desc;
	u16 bwd_size = sizeof(*bw_data);

	i40e_fill_default_direct_cmd_desc(&desc,
					  i40e_aqc_opc_configure_partition_bw);

	/* Indirect command */
	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_BUF);
	desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_RD);

	if (bwd_size > I40E_AQ_LARGE_BUF)
		desc.flags |= cpu_to_le16((u16)I40E_AQ_FLAG_LB);

	desc.datalen = cpu_to_le16(bwd_size);

	status = i40e_asq_send_command(hw, &desc, bw_data, bwd_size,
				       cmd_details);

	return status;
}

/**
 * i40e_read_phy_register_clause22
 * @hw: pointer to the HW structure
 * @reg: register address in the page
 * @phy_adr: PHY address on MDIO interface
 * @value: PHY register value
 *
 * Reads specified PHY register value
 **/
i40e_status i40e_read_phy_register_clause22(struct i40e_hw *hw,
					    u16 reg, u8 phy_addr, u16 *value)
{
	i40e_status status = I40E_ERR_TIMEOUT;
	u8 port_num = (u8)hw->func_caps.mdio_port_num;
	u32 command = 0;
	u16 retry = 1000;

	command = (reg << I40E_GLGEN_MSCA_DEVADD_SHIFT) |
		  (phy_addr << I40E_GLGEN_MSCA_PHYADD_SHIFT) |
		  (I40E_MDIO_CLAUSE22_OPCODE_READ_MASK) |
		  (I40E_MDIO_CLAUSE22_STCODE_MASK) |
		  (I40E_GLGEN_MSCA_MDICMD_MASK);
	wr32(hw, I40E_GLGEN_MSCA(port_num), command);
	do {
		command = rd32(hw, I40E_GLGEN_MSCA(port_num));
		if (!(command & I40E_GLGEN_MSCA_MDICMD_MASK)) {
			status = 0;
			break;
		}
		udelay(10);
		retry--;
	} while (retry);

	if (status) {
		i40e_debug(hw, I40E_DEBUG_PHY,
			   "PHY: Can't write command to external PHY.\n");
		goto phy_read_end;
	}

	if (!status) {
		command = rd32(hw, I40E_GLGEN_MSRWD(port_num));
		*value = (command & I40E_GLGEN_MSRWD_MDIRDDATA_MASK) >>
			 I40E_GLGEN_MSRWD_MDIRDDATA_SHIFT;
	} else {
		i40e_debug(hw, I40E_DEBUG_PHY,
			   "PHY: Can't read register value from external PHY.\n");
	}

phy_read_end:
	return status;
}

/**
 * i40e_write_phy_register_clause22
 * @hw: pointer to the HW structure
 * @reg: register address in the page
 * @phy_adr: PHY address on MDIO interface
 * @value: PHY register value
 *
 * Writes specified PHY register value
 **/
i40e_status i40e_write_phy_register_clause22(struct i40e_hw *hw,
					     u16 reg, u8 phy_addr, u16 value)
{
	i40e_status status = I40E_ERR_TIMEOUT;
	u8 port_num = (u8)hw->func_caps.mdio_port_num;
	u32 command  = 0;
	u16 retry = 1000;

	command = value << I40E_GLGEN_MSRWD_MDIWRDATA_SHIFT;
	wr32(hw, I40E_GLGEN_MSRWD(port_num), command);

	command = (reg << I40E_GLGEN_MSCA_DEVADD_SHIFT) |
		  (phy_addr << I40E_GLGEN_MSCA_PHYADD_SHIFT) |
		  (I40E_MDIO_CLAUSE22_OPCODE_WRITE_MASK) |
		  (I40E_MDIO_CLAUSE22_STCODE_MASK) |
		  (I40E_GLGEN_MSCA_MDICMD_MASK);

	wr32(hw, I40E_GLGEN_MSCA(port_num), command);
	do {
		command = rd32(hw, I40E_GLGEN_MSCA(port_num));
		if (!(command & I40E_GLGEN_MSCA_MDICMD_MASK)) {
			status = 0;
			break;
		}
		udelay(10);
		retry--;
	} while (retry);

	return status;
}

/**
 * i40e_read_phy_register_clause45
 * @hw: pointer to the HW structure
 * @page: registers page number
 * @reg: register address in the page
 * @phy_adr: PHY address on MDIO interface
 * @value: PHY register value
 *
 * Reads specified PHY register value
 **/
i40e_status i40e_read_phy_register_clause45(struct i40e_hw *hw,
				u8 page, u16 reg, u8 phy_addr, u16 *value)
{
	i40e_status status = I40E_ERR_TIMEOUT;
	u32 command = 0;
	u16 retry = 1000;
	u8 port_num = hw->func_caps.mdio_port_num;

	command = (reg << I40E_GLGEN_MSCA_MDIADD_SHIFT) |
		  (page << I40E_GLGEN_MSCA_DEVADD_SHIFT) |
		  (phy_addr << I40E_GLGEN_MSCA_PHYADD_SHIFT) |
		  (I40E_MDIO_CLAUSE45_OPCODE_ADDRESS_MASK) |
		  (I40E_MDIO_CLAUSE45_STCODE_MASK) |
		  (I40E_GLGEN_MSCA_MDICMD_MASK) |
		  (I40E_GLGEN_MSCA_MDIINPROGEN_MASK);
	wr32(hw, I40E_GLGEN_MSCA(port_num), command);
	do {
		command = rd32(hw, I40E_GLGEN_MSCA(port_num));
		if (!(command & I40E_GLGEN_MSCA_MDICMD_MASK)) {
			status = 0;
			break;
		}
		usleep_range(10, 20);
		retry--;
	} while (retry);

	if (status) {
		i40e_debug(hw, I40E_DEBUG_PHY,
			   "PHY: Can't write command to external PHY.\n");
		goto phy_read_end;
	}

	command = (page << I40E_GLGEN_MSCA_DEVADD_SHIFT) |
		  (phy_addr << I40E_GLGEN_MSCA_PHYADD_SHIFT) |
		  (I40E_MDIO_CLAUSE45_OPCODE_READ_MASK) |
		  (I40E_MDIO_CLAUSE45_STCODE_MASK) |
		  (I40E_GLGEN_MSCA_MDICMD_MASK) |
		  (I40E_GLGEN_MSCA_MDIINPROGEN_MASK);
	status = I40E_ERR_TIMEOUT;
	retry = 1000;
	wr32(hw, I40E_GLGEN_MSCA(port_num), command);
	do {
		command = rd32(hw, I40E_GLGEN_MSCA(port_num));
		if (!(command & I40E_GLGEN_MSCA_MDICMD_MASK)) {
			status = 0;
			break;
		}
		usleep_range(10, 20);
		retry--;
	} while (retry);

	if (!status) {
		command = rd32(hw, I40E_GLGEN_MSRWD(port_num));
		*value = (command & I40E_GLGEN_MSRWD_MDIRDDATA_MASK) >>
			 I40E_GLGEN_MSRWD_MDIRDDATA_SHIFT;
	} else {
		i40e_debug(hw, I40E_DEBUG_PHY,
			   "PHY: Can't read register value from external PHY.\n");
	}

phy_read_end:
	return status;
}

/**
 * i40e_write_phy_register_clause45
 * @hw: pointer to the HW structure
 * @page: registers page number
 * @reg: register address in the page
 * @phy_adr: PHY address on MDIO interface
 * @value: PHY register value
 *
 * Writes value to specified PHY register
 **/
i40e_status i40e_write_phy_register_clause45(struct i40e_hw *hw,
				u8 page, u16 reg, u8 phy_addr, u16 value)
{
	i40e_status status = I40E_ERR_TIMEOUT;
	u32 command = 0;
	u16 retry = 1000;
	u8 port_num = hw->func_caps.mdio_port_num;

	command = (reg << I40E_GLGEN_MSCA_MDIADD_SHIFT) |
		  (page << I40E_GLGEN_MSCA_DEVADD_SHIFT) |
		  (phy_addr << I40E_GLGEN_MSCA_PHYADD_SHIFT) |
		  (I40E_MDIO_CLAUSE45_OPCODE_ADDRESS_MASK) |
		  (I40E_MDIO_CLAUSE45_STCODE_MASK) |
		  (I40E_GLGEN_MSCA_MDICMD_MASK) |
		  (I40E_GLGEN_MSCA_MDIINPROGEN_MASK);
	wr32(hw, I40E_GLGEN_MSCA(port_num), command);
	do {
		command = rd32(hw, I40E_GLGEN_MSCA(port_num));
		if (!(command & I40E_GLGEN_MSCA_MDICMD_MASK)) {
			status = 0;
			break;
		}
		usleep_range(10, 20);
		retry--;
	} while (retry);
	if (status) {
		i40e_debug(hw, I40E_DEBUG_PHY,
			   "PHY: Can't write command to external PHY.\n");
		goto phy_write_end;
	}

	command = value << I40E_GLGEN_MSRWD_MDIWRDATA_SHIFT;
	wr32(hw, I40E_GLGEN_MSRWD(port_num), command);

	command = (page << I40E_GLGEN_MSCA_DEVADD_SHIFT) |
		  (phy_addr << I40E_GLGEN_MSCA_PHYADD_SHIFT) |
		  (I40E_MDIO_CLAUSE45_OPCODE_WRITE_MASK) |
		  (I40E_MDIO_CLAUSE45_STCODE_MASK) |
		  (I40E_GLGEN_MSCA_MDICMD_MASK) |
		  (I40E_GLGEN_MSCA_MDIINPROGEN_MASK);
	status = I40E_ERR_TIMEOUT;
	retry = 1000;
	wr32(hw, I40E_GLGEN_MSCA(port_num), command);
	do {
		command = rd32(hw, I40E_GLGEN_MSCA(port_num));
		if (!(command & I40E_GLGEN_MSCA_MDICMD_MASK)) {
			status = 0;
			break;
		}
		usleep_range(10, 20);
		retry--;
	} while (retry);

phy_write_end:
	return status;
}

/**
 * i40e_get_phy_address
 * @hw: pointer to the HW structure
 * @dev_num: PHY port num that address we want
 * @phy_addr: Returned PHY address
 *
 * Gets PHY address for current port
 **/
u8 i40e_get_phy_address(struct i40e_hw *hw, u8 dev_num)
{
	u8 port_num = hw->func_caps.mdio_port_num;
	u32 reg_val = rd32(hw, I40E_GLGEN_MDIO_I2C_SEL(port_num));

	return (u8)(reg_val >> ((dev_num + 1) * 5)) & 0x1f;
}

/**
 * i40e_blink_phy_led
 * @hw: pointer to the HW structure
 * @time: time how long led will blinks in secs
 * @interval: gap between LED on and off in msecs
 *
 * Blinks PHY link LED
 **/
i40e_status i40e_blink_phy_link_led(struct i40e_hw *hw,
				    u32 time, u32 interval)
{
	i40e_status status = 0;
	u32 i;
	u16 led_ctl;
	u16 gpio_led_port;
	u16 led_reg;
	u16 led_addr = I40E_PHY_LED_PROV_REG_1;
	u8 phy_addr = 0;
	u8 port_num;

	i = rd32(hw, I40E_PFGEN_PORTNUM);
	port_num = (u8)(i & I40E_PFGEN_PORTNUM_PORT_NUM_MASK);
	phy_addr = i40e_get_phy_address(hw, port_num);

	for (gpio_led_port = 0; gpio_led_port < 3; gpio_led_port++,
	     led_addr++) {
		status = i40e_read_phy_register_clause45(hw,
							 I40E_PHY_COM_REG_PAGE,
							 led_addr, phy_addr,
							 &led_reg);
		if (status)
			goto phy_blinking_end;
		led_ctl = led_reg;
		if (led_reg & I40E_PHY_LED_LINK_MODE_MASK) {
			led_reg = 0;
			status = i40e_write_phy_register_clause45(hw,
							 I40E_PHY_COM_REG_PAGE,
							 led_addr, phy_addr,
							 led_reg);
			if (status)
				goto phy_blinking_end;
			break;
		}
	}

	if (time > 0 && interval > 0) {
		for (i = 0; i < time * 1000; i += interval) {
			status = i40e_read_phy_register_clause45(hw,
						I40E_PHY_COM_REG_PAGE,
						led_addr, phy_addr, &led_reg);
			if (status)
				goto restore_config;
			if (led_reg & I40E_PHY_LED_MANUAL_ON)
				led_reg = 0;
			else
				led_reg = I40E_PHY_LED_MANUAL_ON;
			status = i40e_write_phy_register_clause45(hw,
						I40E_PHY_COM_REG_PAGE,
						led_addr, phy_addr, led_reg);
			if (status)
				goto restore_config;
			msleep(interval);
		}
	}

restore_config:
	status = i40e_write_phy_register_clause45(hw,
						  I40E_PHY_COM_REG_PAGE,
						  led_addr, phy_addr, led_ctl);

phy_blinking_end:
	return status;
}

/**
 * i40e_led_get_phy - return current on/off mode
 * @hw: pointer to the hw struct
 * @led_addr: address of led register to use
 * @val: original value of register to use
 *
 **/
i40e_status i40e_led_get_phy(struct i40e_hw *hw, u16 *led_addr,
			     u16 *val)
{
	i40e_status status = 0;
	u16 gpio_led_port;
	u8 phy_addr = 0;
	u16 reg_val;
	u16 temp_addr;
	u8 port_num;
	u32 i;

	temp_addr = I40E_PHY_LED_PROV_REG_1;
	i = rd32(hw, I40E_PFGEN_PORTNUM);
	port_num = (u8)(i & I40E_PFGEN_PORTNUM_PORT_NUM_MASK);
	phy_addr = i40e_get_phy_address(hw, port_num);

	for (gpio_led_port = 0; gpio_led_port < 3; gpio_led_port++,
	     temp_addr++) {
		status = i40e_read_phy_register_clause45(hw,
							 I40E_PHY_COM_REG_PAGE,
							 temp_addr, phy_addr,
							 &reg_val);
		if (status)
			return status;
		*val = reg_val;
		if (reg_val & I40E_PHY_LED_LINK_MODE_MASK) {
			*led_addr = temp_addr;
			break;
		}
	}
	return status;
}

/**
 * i40e_led_set_phy
 * @hw: pointer to the HW structure
 * @on: true or false
 * @mode: original val plus bit for set or ignore
 * Set led's on or off when controlled by the PHY
 *
 **/
i40e_status i40e_led_set_phy(struct i40e_hw *hw, bool on,
			     u16 led_addr, u32 mode)
{
	i40e_status status = 0;
	u16 led_ctl = 0;
	u16 led_reg = 0;
	u8 phy_addr = 0;
	u8 port_num;
	u32 i;

	i = rd32(hw, I40E_PFGEN_PORTNUM);
	port_num = (u8)(i & I40E_PFGEN_PORTNUM_PORT_NUM_MASK);
	phy_addr = i40e_get_phy_address(hw, port_num);
	status = i40e_read_phy_register_clause45(hw, I40E_PHY_COM_REG_PAGE,
						 led_addr, phy_addr, &led_reg);
	if (status)
		return status;
	led_ctl = led_reg;
	if (led_reg & I40E_PHY_LED_LINK_MODE_MASK) {
		led_reg = 0;
		status = i40e_write_phy_register_clause45(hw,
							  I40E_PHY_COM_REG_PAGE,
							  led_addr, phy_addr,
							  led_reg);
		if (status)
			return status;
	}
	status = i40e_read_phy_register_clause45(hw, I40E_PHY_COM_REG_PAGE,
						 led_addr, phy_addr, &led_reg);
	if (status)
		goto restore_config;
	if (on)
		led_reg = I40E_PHY_LED_MANUAL_ON;
	else
		led_reg = 0;
	status = i40e_write_phy_register_clause45(hw, I40E_PHY_COM_REG_PAGE,
						  led_addr, phy_addr, led_reg);
	if (status)
		goto restore_config;
	if (mode & I40E_PHY_LED_MODE_ORIG) {
		led_ctl = (mode & I40E_PHY_LED_MODE_MASK);
		status = i40e_write_phy_register_clause45(hw,
						 I40E_PHY_COM_REG_PAGE,
						 led_addr, phy_addr, led_ctl);
	}
	return status;
restore_config:
	status = i40e_write_phy_register_clause45(hw, I40E_PHY_COM_REG_PAGE,
						  led_addr, phy_addr, led_ctl);
	return status;
}

/**
 * i40e_aq_rx_ctl_read_register - use FW to read from an Rx control register
 * @hw: pointer to the hw struct
 * @reg_addr: register address
 * @reg_val: ptr to register value
 * @cmd_details: pointer to command details structure or NULL
 *
 * Use the firmware to read the Rx control register,
 * especially useful if the Rx unit is under heavy pressure
 **/
i40e_status i40e_aq_rx_ctl_read_register(struct i40e_hw *hw,
				u32 reg_addr, u32 *reg_val,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_rx_ctl_reg_read_write *cmd_resp =
		(struct i40e_aqc_rx_ctl_reg_read_write *)&desc.params.raw;
	i40e_status status;

	if (!reg_val)
		return I40E_ERR_PARAM;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_rx_ctl_reg_read);

	cmd_resp->address = cpu_to_le32(reg_addr);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	if (status == 0)
		*reg_val = le32_to_cpu(cmd_resp->value);

	return status;
}

/**
 * i40e_read_rx_ctl - read from an Rx control register
 * @hw: pointer to the hw struct
 * @reg_addr: register address
 **/
u32 i40e_read_rx_ctl(struct i40e_hw *hw, u32 reg_addr)
{
	i40e_status status = 0;
	bool use_register;
	int retry = 5;
	u32 val = 0;

	use_register = (hw->aq.api_maj_ver == 1) && (hw->aq.api_min_ver < 5);
	if (!use_register) {
do_retry:
		status = i40e_aq_rx_ctl_read_register(hw, reg_addr, &val, NULL);
		if (hw->aq.asq_last_status == I40E_AQ_RC_EAGAIN && retry) {
			usleep_range(1000, 2000);
			retry--;
			goto do_retry;
		}
	}

	/* if the AQ access failed, try the old-fashioned way */
	if (status || use_register)
		val = rd32(hw, reg_addr);

	return val;
}

/**
 * i40e_aq_rx_ctl_write_register
 * @hw: pointer to the hw struct
 * @reg_addr: register address
 * @reg_val: register value
 * @cmd_details: pointer to command details structure or NULL
 *
 * Use the firmware to write to an Rx control register,
 * especially useful if the Rx unit is under heavy pressure
 **/
i40e_status i40e_aq_rx_ctl_write_register(struct i40e_hw *hw,
				u32 reg_addr, u32 reg_val,
				struct i40e_asq_cmd_details *cmd_details)
{
	struct i40e_aq_desc desc;
	struct i40e_aqc_rx_ctl_reg_read_write *cmd =
		(struct i40e_aqc_rx_ctl_reg_read_write *)&desc.params.raw;
	i40e_status status;

	i40e_fill_default_direct_cmd_desc(&desc, i40e_aqc_opc_rx_ctl_reg_write);

	cmd->address = cpu_to_le32(reg_addr);
	cmd->value = cpu_to_le32(reg_val);

	status = i40e_asq_send_command(hw, &desc, NULL, 0, cmd_details);

	return status;
}

/**
 * i40e_write_rx_ctl - write to an Rx control register
 * @hw: pointer to the hw struct
 * @reg_addr: register address
 * @reg_val: register value
 **/
void i40e_write_rx_ctl(struct i40e_hw *hw, u32 reg_addr, u32 reg_val)
{
	i40e_status status = 0;
	bool use_register;
	int retry = 5;

	use_register = (hw->aq.api_maj_ver == 1) && (hw->aq.api_min_ver < 5);
	if (!use_register) {
do_retry:
		status = i40e_aq_rx_ctl_write_register(hw, reg_addr,
						       reg_val, NULL);
		if (hw->aq.asq_last_status == I40E_AQ_RC_EAGAIN && retry) {
			usleep_range(1000, 2000);
			retry--;
			goto do_retry;
		}
	}

	/* if the AQ access failed, try the old-fashioned way */
	if (status || use_register)
		wr32(hw, reg_addr, reg_val);
}
