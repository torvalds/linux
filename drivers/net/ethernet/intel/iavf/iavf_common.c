// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#include <linux/avf/virtchnl.h>
#include <linux/bitfield.h>
#include "iavf_type.h"
#include "iavf_adminq.h"
#include "iavf_prototype.h"

/**
 * iavf_stat_str - convert status err code to a string
 * @hw: pointer to the HW structure
 * @stat_err: the status error code to convert
 **/
const char *iavf_stat_str(struct iavf_hw *hw, enum iavf_status stat_err)
{
	switch (stat_err) {
	case 0:
		return "OK";
	case IAVF_ERR_NVM:
		return "IAVF_ERR_NVM";
	case IAVF_ERR_NVM_CHECKSUM:
		return "IAVF_ERR_NVM_CHECKSUM";
	case IAVF_ERR_PHY:
		return "IAVF_ERR_PHY";
	case IAVF_ERR_CONFIG:
		return "IAVF_ERR_CONFIG";
	case IAVF_ERR_PARAM:
		return "IAVF_ERR_PARAM";
	case IAVF_ERR_MAC_TYPE:
		return "IAVF_ERR_MAC_TYPE";
	case IAVF_ERR_UNKNOWN_PHY:
		return "IAVF_ERR_UNKNOWN_PHY";
	case IAVF_ERR_LINK_SETUP:
		return "IAVF_ERR_LINK_SETUP";
	case IAVF_ERR_ADAPTER_STOPPED:
		return "IAVF_ERR_ADAPTER_STOPPED";
	case IAVF_ERR_INVALID_MAC_ADDR:
		return "IAVF_ERR_INVALID_MAC_ADDR";
	case IAVF_ERR_DEVICE_NOT_SUPPORTED:
		return "IAVF_ERR_DEVICE_NOT_SUPPORTED";
	case IAVF_ERR_PRIMARY_REQUESTS_PENDING:
		return "IAVF_ERR_PRIMARY_REQUESTS_PENDING";
	case IAVF_ERR_INVALID_LINK_SETTINGS:
		return "IAVF_ERR_INVALID_LINK_SETTINGS";
	case IAVF_ERR_AUTONEG_NOT_COMPLETE:
		return "IAVF_ERR_AUTONEG_NOT_COMPLETE";
	case IAVF_ERR_RESET_FAILED:
		return "IAVF_ERR_RESET_FAILED";
	case IAVF_ERR_SWFW_SYNC:
		return "IAVF_ERR_SWFW_SYNC";
	case IAVF_ERR_NO_AVAILABLE_VSI:
		return "IAVF_ERR_NO_AVAILABLE_VSI";
	case IAVF_ERR_NO_MEMORY:
		return "IAVF_ERR_NO_MEMORY";
	case IAVF_ERR_BAD_PTR:
		return "IAVF_ERR_BAD_PTR";
	case IAVF_ERR_RING_FULL:
		return "IAVF_ERR_RING_FULL";
	case IAVF_ERR_INVALID_PD_ID:
		return "IAVF_ERR_INVALID_PD_ID";
	case IAVF_ERR_INVALID_QP_ID:
		return "IAVF_ERR_INVALID_QP_ID";
	case IAVF_ERR_INVALID_CQ_ID:
		return "IAVF_ERR_INVALID_CQ_ID";
	case IAVF_ERR_INVALID_CEQ_ID:
		return "IAVF_ERR_INVALID_CEQ_ID";
	case IAVF_ERR_INVALID_AEQ_ID:
		return "IAVF_ERR_INVALID_AEQ_ID";
	case IAVF_ERR_INVALID_SIZE:
		return "IAVF_ERR_INVALID_SIZE";
	case IAVF_ERR_INVALID_ARP_INDEX:
		return "IAVF_ERR_INVALID_ARP_INDEX";
	case IAVF_ERR_INVALID_FPM_FUNC_ID:
		return "IAVF_ERR_INVALID_FPM_FUNC_ID";
	case IAVF_ERR_QP_INVALID_MSG_SIZE:
		return "IAVF_ERR_QP_INVALID_MSG_SIZE";
	case IAVF_ERR_QP_TOOMANY_WRS_POSTED:
		return "IAVF_ERR_QP_TOOMANY_WRS_POSTED";
	case IAVF_ERR_INVALID_FRAG_COUNT:
		return "IAVF_ERR_INVALID_FRAG_COUNT";
	case IAVF_ERR_QUEUE_EMPTY:
		return "IAVF_ERR_QUEUE_EMPTY";
	case IAVF_ERR_INVALID_ALIGNMENT:
		return "IAVF_ERR_INVALID_ALIGNMENT";
	case IAVF_ERR_FLUSHED_QUEUE:
		return "IAVF_ERR_FLUSHED_QUEUE";
	case IAVF_ERR_INVALID_PUSH_PAGE_INDEX:
		return "IAVF_ERR_INVALID_PUSH_PAGE_INDEX";
	case IAVF_ERR_INVALID_IMM_DATA_SIZE:
		return "IAVF_ERR_INVALID_IMM_DATA_SIZE";
	case IAVF_ERR_TIMEOUT:
		return "IAVF_ERR_TIMEOUT";
	case IAVF_ERR_OPCODE_MISMATCH:
		return "IAVF_ERR_OPCODE_MISMATCH";
	case IAVF_ERR_CQP_COMPL_ERROR:
		return "IAVF_ERR_CQP_COMPL_ERROR";
	case IAVF_ERR_INVALID_VF_ID:
		return "IAVF_ERR_INVALID_VF_ID";
	case IAVF_ERR_INVALID_HMCFN_ID:
		return "IAVF_ERR_INVALID_HMCFN_ID";
	case IAVF_ERR_BACKING_PAGE_ERROR:
		return "IAVF_ERR_BACKING_PAGE_ERROR";
	case IAVF_ERR_NO_PBLCHUNKS_AVAILABLE:
		return "IAVF_ERR_NO_PBLCHUNKS_AVAILABLE";
	case IAVF_ERR_INVALID_PBLE_INDEX:
		return "IAVF_ERR_INVALID_PBLE_INDEX";
	case IAVF_ERR_INVALID_SD_INDEX:
		return "IAVF_ERR_INVALID_SD_INDEX";
	case IAVF_ERR_INVALID_PAGE_DESC_INDEX:
		return "IAVF_ERR_INVALID_PAGE_DESC_INDEX";
	case IAVF_ERR_INVALID_SD_TYPE:
		return "IAVF_ERR_INVALID_SD_TYPE";
	case IAVF_ERR_MEMCPY_FAILED:
		return "IAVF_ERR_MEMCPY_FAILED";
	case IAVF_ERR_INVALID_HMC_OBJ_INDEX:
		return "IAVF_ERR_INVALID_HMC_OBJ_INDEX";
	case IAVF_ERR_INVALID_HMC_OBJ_COUNT:
		return "IAVF_ERR_INVALID_HMC_OBJ_COUNT";
	case IAVF_ERR_INVALID_SRQ_ARM_LIMIT:
		return "IAVF_ERR_INVALID_SRQ_ARM_LIMIT";
	case IAVF_ERR_SRQ_ENABLED:
		return "IAVF_ERR_SRQ_ENABLED";
	case IAVF_ERR_ADMIN_QUEUE_ERROR:
		return "IAVF_ERR_ADMIN_QUEUE_ERROR";
	case IAVF_ERR_ADMIN_QUEUE_TIMEOUT:
		return "IAVF_ERR_ADMIN_QUEUE_TIMEOUT";
	case IAVF_ERR_BUF_TOO_SHORT:
		return "IAVF_ERR_BUF_TOO_SHORT";
	case IAVF_ERR_ADMIN_QUEUE_FULL:
		return "IAVF_ERR_ADMIN_QUEUE_FULL";
	case IAVF_ERR_ADMIN_QUEUE_NO_WORK:
		return "IAVF_ERR_ADMIN_QUEUE_NO_WORK";
	case IAVF_ERR_BAD_RDMA_CQE:
		return "IAVF_ERR_BAD_RDMA_CQE";
	case IAVF_ERR_NVM_BLANK_MODE:
		return "IAVF_ERR_NVM_BLANK_MODE";
	case IAVF_ERR_NOT_IMPLEMENTED:
		return "IAVF_ERR_NOT_IMPLEMENTED";
	case IAVF_ERR_PE_DOORBELL_NOT_ENABLED:
		return "IAVF_ERR_PE_DOORBELL_NOT_ENABLED";
	case IAVF_ERR_DIAG_TEST_FAILED:
		return "IAVF_ERR_DIAG_TEST_FAILED";
	case IAVF_ERR_NOT_READY:
		return "IAVF_ERR_NOT_READY";
	case IAVF_NOT_SUPPORTED:
		return "IAVF_NOT_SUPPORTED";
	case IAVF_ERR_FIRMWARE_API_VERSION:
		return "IAVF_ERR_FIRMWARE_API_VERSION";
	case IAVF_ERR_ADMIN_QUEUE_CRITICAL_ERROR:
		return "IAVF_ERR_ADMIN_QUEUE_CRITICAL_ERROR";
	}

	snprintf(hw->err_str, sizeof(hw->err_str), "%d", stat_err);
	return hw->err_str;
}

/**
 * iavf_debug_aq
 * @hw: debug mask related to admin queue
 * @mask: debug mask
 * @desc: pointer to admin queue descriptor
 * @buffer: pointer to command buffer
 * @buf_len: max length of buffer
 *
 * Dumps debug log about adminq command with descriptor contents.
 **/
void iavf_debug_aq(struct iavf_hw *hw, enum iavf_debug_mask mask, void *desc,
		   void *buffer, u16 buf_len)
{
	struct libie_aq_desc *aq_desc = (struct libie_aq_desc *)desc;
	u8 *buf = (u8 *)buffer;

	if ((!(mask & hw->debug_mask)) || !desc)
		return;

	iavf_debug(hw, mask,
		   "AQ CMD: opcode 0x%04X, flags 0x%04X, datalen 0x%04X, retval 0x%04X\n",
		   le16_to_cpu(aq_desc->opcode),
		   le16_to_cpu(aq_desc->flags),
		   le16_to_cpu(aq_desc->datalen),
		   le16_to_cpu(aq_desc->retval));
	iavf_debug(hw, mask, "\tcookie (h,l) 0x%08X 0x%08X\n",
		   le32_to_cpu(aq_desc->cookie_high),
		   le32_to_cpu(aq_desc->cookie_low));
	iavf_debug(hw, mask, "\tparam (0,1)  0x%08X 0x%08X\n",
		   le32_to_cpu(aq_desc->params.generic.param0),
		   le32_to_cpu(aq_desc->params.generic.param1));
	iavf_debug(hw, mask, "\taddr (h,l)   0x%08X 0x%08X\n",
		   le32_to_cpu(aq_desc->params.generic.addr_high),
		   le32_to_cpu(aq_desc->params.generic.addr_low));

	if (buffer && aq_desc->datalen) {
		u16 len = le16_to_cpu(aq_desc->datalen);

		iavf_debug(hw, mask, "AQ CMD Buffer:\n");
		if (buf_len < len)
			len = buf_len;
		/* write the full 16-byte chunks */
		if (hw->debug_mask & mask) {
			char prefix[27];

			snprintf(prefix, sizeof(prefix),
				 "iavf %02x:%02x.%x: \t0x",
				 hw->bus.bus_id,
				 hw->bus.device,
				 hw->bus.func);

			print_hex_dump(KERN_INFO, prefix, DUMP_PREFIX_OFFSET,
				       16, 1, buf, len, false);
		}
	}
}

/**
 * iavf_check_asq_alive
 * @hw: pointer to the hw struct
 *
 * Returns true if Queue is enabled else false.
 **/
bool iavf_check_asq_alive(struct iavf_hw *hw)
{
	/* Check if the queue is initialized */
	if (!hw->aq.asq.count)
		return false;

	return !!(rd32(hw, IAVF_VF_ATQLEN1) & IAVF_VF_ATQLEN1_ATQENABLE_MASK);
}

/**
 * iavf_aq_queue_shutdown
 * @hw: pointer to the hw struct
 * @unloading: is the driver unloading itself
 *
 * Tell the Firmware that we're shutting down the AdminQ and whether
 * or not the driver is unloading as well.
 **/
enum iavf_status iavf_aq_queue_shutdown(struct iavf_hw *hw, bool unloading)
{
	struct iavf_aqc_queue_shutdown *cmd;
	struct libie_aq_desc desc;
	enum iavf_status status;

	cmd = libie_aq_raw(&desc);
	iavf_fill_default_direct_cmd_desc(&desc, iavf_aqc_opc_queue_shutdown);

	if (unloading)
		cmd->driver_unloading = cpu_to_le32(IAVF_AQ_DRIVER_UNLOADING);
	status = iavf_asq_send_command(hw, &desc, NULL, 0, NULL);

	return status;
}

/**
 * iavf_aq_get_set_rss_lut
 * @hw: pointer to the hardware structure
 * @vsi_id: vsi fw index
 * @pf_lut: for PF table set true, for VSI table set false
 * @lut: pointer to the lut buffer provided by the caller
 * @lut_size: size of the lut buffer
 * @set: set true to set the table, false to get the table
 *
 * Internal function to get or set RSS look up table
 **/
static enum iavf_status iavf_aq_get_set_rss_lut(struct iavf_hw *hw,
						u16 vsi_id, bool pf_lut,
						u8 *lut, u16 lut_size,
						bool set)
{
	struct iavf_aqc_get_set_rss_lut *cmd_resp;
	struct libie_aq_desc desc;
	enum iavf_status status;
	u16 flags;

	cmd_resp = libie_aq_raw(&desc);

	if (set)
		iavf_fill_default_direct_cmd_desc(&desc,
						  iavf_aqc_opc_set_rss_lut);
	else
		iavf_fill_default_direct_cmd_desc(&desc,
						  iavf_aqc_opc_get_rss_lut);

	/* Indirect command */
	desc.flags |= cpu_to_le16((u16)LIBIE_AQ_FLAG_BUF);
	desc.flags |= cpu_to_le16((u16)LIBIE_AQ_FLAG_RD);

	vsi_id = FIELD_PREP(IAVF_AQC_SET_RSS_LUT_VSI_ID_MASK, vsi_id) |
		 FIELD_PREP(IAVF_AQC_SET_RSS_LUT_VSI_VALID, 1);
	cmd_resp->vsi_id = cpu_to_le16(vsi_id);

	if (pf_lut)
		flags = FIELD_PREP(IAVF_AQC_SET_RSS_LUT_TABLE_TYPE_MASK,
				   IAVF_AQC_SET_RSS_LUT_TABLE_TYPE_PF);
	else
		flags = FIELD_PREP(IAVF_AQC_SET_RSS_LUT_TABLE_TYPE_MASK,
				   IAVF_AQC_SET_RSS_LUT_TABLE_TYPE_VSI);

	cmd_resp->flags = cpu_to_le16(flags);

	status = iavf_asq_send_command(hw, &desc, lut, lut_size, NULL);

	return status;
}

/**
 * iavf_aq_set_rss_lut
 * @hw: pointer to the hardware structure
 * @vsi_id: vsi fw index
 * @pf_lut: for PF table set true, for VSI table set false
 * @lut: pointer to the lut buffer provided by the caller
 * @lut_size: size of the lut buffer
 *
 * set the RSS lookup table, PF or VSI type
 **/
enum iavf_status iavf_aq_set_rss_lut(struct iavf_hw *hw, u16 vsi_id,
				     bool pf_lut, u8 *lut, u16 lut_size)
{
	return iavf_aq_get_set_rss_lut(hw, vsi_id, pf_lut, lut, lut_size, true);
}

/**
 * iavf_aq_get_set_rss_key
 * @hw: pointer to the hw struct
 * @vsi_id: vsi fw index
 * @key: pointer to key info struct
 * @set: set true to set the key, false to get the key
 *
 * get the RSS key per VSI
 **/
static enum
iavf_status iavf_aq_get_set_rss_key(struct iavf_hw *hw, u16 vsi_id,
				    struct iavf_aqc_get_set_rss_key_data *key,
				    bool set)
{
	u16 key_size = sizeof(struct iavf_aqc_get_set_rss_key_data);
	struct iavf_aqc_get_set_rss_key *cmd_resp;
	struct libie_aq_desc desc;
	enum iavf_status status;

	cmd_resp = libie_aq_raw(&desc);

	if (set)
		iavf_fill_default_direct_cmd_desc(&desc,
						  iavf_aqc_opc_set_rss_key);
	else
		iavf_fill_default_direct_cmd_desc(&desc,
						  iavf_aqc_opc_get_rss_key);

	/* Indirect command */
	desc.flags |= cpu_to_le16((u16)LIBIE_AQ_FLAG_BUF);
	desc.flags |= cpu_to_le16((u16)LIBIE_AQ_FLAG_RD);

	vsi_id = FIELD_PREP(IAVF_AQC_SET_RSS_KEY_VSI_ID_MASK, vsi_id) |
		 FIELD_PREP(IAVF_AQC_SET_RSS_KEY_VSI_VALID, 1);
	cmd_resp->vsi_id = cpu_to_le16(vsi_id);

	status = iavf_asq_send_command(hw, &desc, key, key_size, NULL);

	return status;
}

/**
 * iavf_aq_set_rss_key
 * @hw: pointer to the hw struct
 * @vsi_id: vsi fw index
 * @key: pointer to key info struct
 *
 * set the RSS key per VSI
 **/
enum iavf_status iavf_aq_set_rss_key(struct iavf_hw *hw, u16 vsi_id,
				     struct iavf_aqc_get_set_rss_key_data *key)
{
	return iavf_aq_get_set_rss_key(hw, vsi_id, key, true);
}

/**
 * iavf_aq_send_msg_to_pf
 * @hw: pointer to the hardware structure
 * @v_opcode: opcodes for VF-PF communication
 * @v_retval: return error code
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 * @cmd_details: pointer to command details
 *
 * Send message to PF driver using admin queue. By default, this message
 * is sent asynchronously, i.e. iavf_asq_send_command() does not wait for
 * completion before returning.
 **/
enum iavf_status iavf_aq_send_msg_to_pf(struct iavf_hw *hw,
					enum virtchnl_ops v_opcode,
					enum iavf_status v_retval,
					u8 *msg, u16 msglen,
					struct iavf_asq_cmd_details *cmd_details)
{
	struct iavf_asq_cmd_details details;
	struct libie_aq_desc desc;
	enum iavf_status status;

	iavf_fill_default_direct_cmd_desc(&desc, iavf_aqc_opc_send_msg_to_pf);
	desc.flags |= cpu_to_le16((u16)LIBIE_AQ_FLAG_SI);
	desc.cookie_high = cpu_to_le32(v_opcode);
	desc.cookie_low = cpu_to_le32(v_retval);
	if (msglen) {
		desc.flags |= cpu_to_le16((u16)(LIBIE_AQ_FLAG_BUF
						| LIBIE_AQ_FLAG_RD));
		if (msglen > IAVF_AQ_LARGE_BUF)
			desc.flags |= cpu_to_le16((u16)LIBIE_AQ_FLAG_LB);
		desc.datalen = cpu_to_le16(msglen);
	}
	if (!cmd_details) {
		memset(&details, 0, sizeof(details));
		details.async = true;
		cmd_details = &details;
	}
	status = iavf_asq_send_command(hw, &desc, msg, msglen, cmd_details);
	return status;
}

/**
 * iavf_vf_parse_hw_config
 * @hw: pointer to the hardware structure
 * @msg: pointer to the virtual channel VF resource structure
 *
 * Given a VF resource message from the PF, populate the hw struct
 * with appropriate information.
 **/
void iavf_vf_parse_hw_config(struct iavf_hw *hw,
			     struct virtchnl_vf_resource *msg)
{
	struct virtchnl_vsi_resource *vsi_res;
	int i;

	vsi_res = &msg->vsi_res[0];

	hw->dev_caps.num_vsis = msg->num_vsis;
	hw->dev_caps.num_rx_qp = msg->num_queue_pairs;
	hw->dev_caps.num_tx_qp = msg->num_queue_pairs;
	hw->dev_caps.num_msix_vectors_vf = msg->max_vectors;
	hw->dev_caps.dcb = msg->vf_cap_flags &
			   VIRTCHNL_VF_OFFLOAD_L2;
	hw->dev_caps.fcoe = 0;
	for (i = 0; i < msg->num_vsis; i++) {
		if (vsi_res->vsi_type == VIRTCHNL_VSI_SRIOV) {
			ether_addr_copy(hw->mac.perm_addr,
					vsi_res->default_mac_addr);
			ether_addr_copy(hw->mac.addr,
					vsi_res->default_mac_addr);
		}
		vsi_res++;
	}
}
