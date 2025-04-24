// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2024 Intel Corporation. */

#include "ixgbe_common.h"
#include "ixgbe_e610.h"
#include "ixgbe_x550.h"
#include "ixgbe_type.h"
#include "ixgbe_x540.h"
#include "ixgbe_mbx.h"
#include "ixgbe_phy.h"

/**
 * ixgbe_should_retry_aci_send_cmd_execute - decide if ACI command should
 * be resent
 * @opcode: ACI opcode
 *
 * Check if ACI command should be sent again depending on the provided opcode.
 * It may happen when CSR is busy during link state changes.
 *
 * Return: true if the sending command routine should be repeated,
 * otherwise false.
 */
static bool ixgbe_should_retry_aci_send_cmd_execute(u16 opcode)
{
	switch (opcode) {
	case ixgbe_aci_opc_disable_rxen:
	case ixgbe_aci_opc_get_phy_caps:
	case ixgbe_aci_opc_get_link_status:
	case ixgbe_aci_opc_get_link_topo:
		return true;
	}

	return false;
}

/**
 * ixgbe_aci_send_cmd_execute - execute sending FW Admin Command to FW Admin
 * Command Interface
 * @hw: pointer to the HW struct
 * @desc: descriptor describing the command
 * @buf: buffer to use for indirect commands (NULL for direct commands)
 * @buf_size: size of buffer for indirect commands (0 for direct commands)
 *
 * Admin Command is sent using CSR by setting descriptor and buffer in specific
 * registers.
 *
 * Return: the exit code of the operation.
 * * - 0 - success.
 * * - -EIO - CSR mechanism is not enabled.
 * * - -EBUSY - CSR mechanism is busy.
 * * - -EINVAL - buf_size is too big or
 * invalid argument buf or buf_size.
 * * - -ETIME - Admin Command X command timeout.
 * * - -EIO - Admin Command X invalid state of HICR register or
 * Admin Command failed because of bad opcode was returned or
 * Admin Command failed with error Y.
 */
static int ixgbe_aci_send_cmd_execute(struct ixgbe_hw *hw,
				      struct ixgbe_aci_desc *desc,
				      void *buf, u16 buf_size)
{
	u16 opcode, buf_tail_size = buf_size % 4;
	u32 *raw_desc = (u32 *)desc;
	u32 hicr, i, buf_tail = 0;
	bool valid_buf = false;

	hw->aci.last_status = IXGBE_ACI_RC_OK;

	/* It's necessary to check if mechanism is enabled */
	hicr = IXGBE_READ_REG(hw, IXGBE_PF_HICR);

	if (!(hicr & IXGBE_PF_HICR_EN))
		return -EIO;

	if (hicr & IXGBE_PF_HICR_C) {
		hw->aci.last_status = IXGBE_ACI_RC_EBUSY;
		return -EBUSY;
	}

	opcode = le16_to_cpu(desc->opcode);

	if (buf_size > IXGBE_ACI_MAX_BUFFER_SIZE)
		return -EINVAL;

	if (buf)
		desc->flags |= cpu_to_le16(IXGBE_ACI_FLAG_BUF);

	if (desc->flags & cpu_to_le16(IXGBE_ACI_FLAG_BUF)) {
		if ((buf && !buf_size) ||
		    (!buf && buf_size))
			return -EINVAL;
		if (buf && buf_size)
			valid_buf = true;
	}

	if (valid_buf) {
		if (buf_tail_size)
			memcpy(&buf_tail, buf + buf_size - buf_tail_size,
			       buf_tail_size);

		if (((buf_size + 3) & ~0x3) > IXGBE_ACI_LG_BUF)
			desc->flags |= cpu_to_le16(IXGBE_ACI_FLAG_LB);

		desc->datalen = cpu_to_le16(buf_size);

		if (desc->flags & cpu_to_le16(IXGBE_ACI_FLAG_RD)) {
			for (i = 0; i < buf_size / 4; i++)
				IXGBE_WRITE_REG(hw, IXGBE_PF_HIBA(i), ((u32 *)buf)[i]);
			if (buf_tail_size)
				IXGBE_WRITE_REG(hw, IXGBE_PF_HIBA(i), buf_tail);
		}
	}

	/* Descriptor is written to specific registers */
	for (i = 0; i < IXGBE_ACI_DESC_SIZE_IN_DWORDS; i++)
		IXGBE_WRITE_REG(hw, IXGBE_PF_HIDA(i), raw_desc[i]);

	/* SW has to set PF_HICR.C bit and clear PF_HICR.SV and
	 * PF_HICR_EV
	 */
	hicr = (IXGBE_READ_REG(hw, IXGBE_PF_HICR) | IXGBE_PF_HICR_C) &
	       ~(IXGBE_PF_HICR_SV | IXGBE_PF_HICR_EV);
	IXGBE_WRITE_REG(hw, IXGBE_PF_HICR, hicr);

#define MAX_SLEEP_RESP_US 1000
#define MAX_TMOUT_RESP_SYNC_US 100000000

	/* Wait for sync Admin Command response */
	read_poll_timeout(IXGBE_READ_REG, hicr,
			  (hicr & IXGBE_PF_HICR_SV) ||
			  !(hicr & IXGBE_PF_HICR_C),
			  MAX_SLEEP_RESP_US, MAX_TMOUT_RESP_SYNC_US, true, hw,
			  IXGBE_PF_HICR);

#define MAX_TMOUT_RESP_ASYNC_US 150000000

	/* Wait for async Admin Command response */
	read_poll_timeout(IXGBE_READ_REG, hicr,
			  (hicr & IXGBE_PF_HICR_EV) ||
			  !(hicr & IXGBE_PF_HICR_C),
			  MAX_SLEEP_RESP_US, MAX_TMOUT_RESP_ASYNC_US, true, hw,
			  IXGBE_PF_HICR);

	/* Read sync Admin Command response */
	if ((hicr & IXGBE_PF_HICR_SV)) {
		for (i = 0; i < IXGBE_ACI_DESC_SIZE_IN_DWORDS; i++) {
			raw_desc[i] = IXGBE_READ_REG(hw, IXGBE_PF_HIDA(i));
			raw_desc[i] = raw_desc[i];
		}
	}

	/* Read async Admin Command response */
	if ((hicr & IXGBE_PF_HICR_EV) && !(hicr & IXGBE_PF_HICR_C)) {
		for (i = 0; i < IXGBE_ACI_DESC_SIZE_IN_DWORDS; i++) {
			raw_desc[i] = IXGBE_READ_REG(hw, IXGBE_PF_HIDA_2(i));
			raw_desc[i] = raw_desc[i];
		}
	}

	/* Handle timeout and invalid state of HICR register */
	if (hicr & IXGBE_PF_HICR_C)
		return -ETIME;

	if (!(hicr & IXGBE_PF_HICR_SV) && !(hicr & IXGBE_PF_HICR_EV))
		return -EIO;

	/* For every command other than 0x0014 treat opcode mismatch
	 * as an error. Response to 0x0014 command read from HIDA_2
	 * is a descriptor of an event which is expected to contain
	 * different opcode than the command.
	 */
	if (desc->opcode != cpu_to_le16(opcode) &&
	    opcode != ixgbe_aci_opc_get_fw_event)
		return -EIO;

	if (desc->retval) {
		hw->aci.last_status = (enum ixgbe_aci_err)
			le16_to_cpu(desc->retval);
		return -EIO;
	}

	/* Write a response values to a buf */
	if (valid_buf) {
		for (i = 0; i < buf_size / 4; i++)
			((u32 *)buf)[i] = IXGBE_READ_REG(hw, IXGBE_PF_HIBA(i));
		if (buf_tail_size) {
			buf_tail = IXGBE_READ_REG(hw, IXGBE_PF_HIBA(i));
			memcpy(buf + buf_size - buf_tail_size, &buf_tail,
			       buf_tail_size);
		}
	}

	return 0;
}

/**
 * ixgbe_aci_send_cmd - send FW Admin Command to FW Admin Command Interface
 * @hw: pointer to the HW struct
 * @desc: descriptor describing the command
 * @buf: buffer to use for indirect commands (NULL for direct commands)
 * @buf_size: size of buffer for indirect commands (0 for direct commands)
 *
 * Helper function to send FW Admin Commands to the FW Admin Command Interface.
 *
 * Retry sending the FW Admin Command multiple times to the FW ACI
 * if the EBUSY Admin Command error is returned.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_aci_send_cmd(struct ixgbe_hw *hw, struct ixgbe_aci_desc *desc,
		       void *buf, u16 buf_size)
{
	u16 opcode = le16_to_cpu(desc->opcode);
	struct ixgbe_aci_desc desc_cpy;
	enum ixgbe_aci_err last_status;
	u8 idx = 0, *buf_cpy = NULL;
	bool is_cmd_for_retry;
	unsigned long timeout;
	int err;

	is_cmd_for_retry = ixgbe_should_retry_aci_send_cmd_execute(opcode);
	if (is_cmd_for_retry) {
		if (buf) {
			buf_cpy = kmalloc(buf_size, GFP_KERNEL);
			if (!buf_cpy)
				return -ENOMEM;
			*buf_cpy = *(u8 *)buf;
		}
		desc_cpy = *desc;
	}

	timeout = jiffies + msecs_to_jiffies(IXGBE_ACI_SEND_TIMEOUT_MS);
	do {
		mutex_lock(&hw->aci.lock);
		err = ixgbe_aci_send_cmd_execute(hw, desc, buf, buf_size);
		last_status = hw->aci.last_status;
		mutex_unlock(&hw->aci.lock);

		if (!is_cmd_for_retry || !err ||
		    last_status != IXGBE_ACI_RC_EBUSY)
			break;

		if (buf)
			memcpy(buf, buf_cpy, buf_size);
		*desc = desc_cpy;

		msleep(IXGBE_ACI_SEND_DELAY_TIME_MS);
	} while (++idx < IXGBE_ACI_SEND_MAX_EXECUTE &&
		 time_before(jiffies, timeout));

	kfree(buf_cpy);

	return err;
}

/**
 * ixgbe_aci_check_event_pending - check if there are any pending events
 * @hw: pointer to the HW struct
 *
 * Determine if there are any pending events.
 *
 * Return: true if there are any currently pending events
 * otherwise false.
 */
bool ixgbe_aci_check_event_pending(struct ixgbe_hw *hw)
{
	u32 ep_bit_mask = hw->bus.func ? GL_FWSTS_EP_PF1 : GL_FWSTS_EP_PF0;
	u32 fwsts = IXGBE_READ_REG(hw, GL_FWSTS);

	return (fwsts & ep_bit_mask) ? true : false;
}

/**
 * ixgbe_aci_get_event - get an event from ACI
 * @hw: pointer to the HW struct
 * @e: event information structure
 * @pending: optional flag signaling that there are more pending events
 *
 * Obtain an event from ACI and return its content
 * through 'e' using ACI command (0x0014).
 * Provide information if there are more events
 * to retrieve through 'pending'.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_aci_get_event(struct ixgbe_hw *hw, struct ixgbe_aci_event *e,
			bool *pending)
{
	struct ixgbe_aci_desc desc;
	int err;

	if (!e || (!e->msg_buf && e->buf_len))
		return -EINVAL;

	mutex_lock(&hw->aci.lock);

	/* Check if there are any events pending */
	if (!ixgbe_aci_check_event_pending(hw)) {
		err = -ENOENT;
		goto aci_get_event_exit;
	}

	/* Obtain pending event */
	ixgbe_fill_dflt_direct_cmd_desc(&desc, ixgbe_aci_opc_get_fw_event);
	err = ixgbe_aci_send_cmd_execute(hw, &desc, e->msg_buf, e->buf_len);
	if (err)
		goto aci_get_event_exit;

	/* Returned 0x0014 opcode indicates that no event was obtained */
	if (desc.opcode == cpu_to_le16(ixgbe_aci_opc_get_fw_event)) {
		err = -ENOENT;
		goto aci_get_event_exit;
	}

	/* Determine size of event data */
	e->msg_len = min_t(u16, le16_to_cpu(desc.datalen), e->buf_len);
	/* Write event descriptor to event info structure */
	memcpy(&e->desc, &desc, sizeof(e->desc));

	/* Check if there are any further events pending */
	if (pending)
		*pending = ixgbe_aci_check_event_pending(hw);

aci_get_event_exit:
	mutex_unlock(&hw->aci.lock);

	return err;
}

/**
 * ixgbe_fill_dflt_direct_cmd_desc - fill ACI descriptor with default values.
 * @desc: pointer to the temp descriptor (non DMA mem)
 * @opcode: the opcode can be used to decide which flags to turn off or on
 *
 * Helper function to fill the descriptor desc with default values
 * and the provided opcode.
 */
void ixgbe_fill_dflt_direct_cmd_desc(struct ixgbe_aci_desc *desc, u16 opcode)
{
	/* Zero out the desc. */
	memset(desc, 0, sizeof(*desc));
	desc->opcode = cpu_to_le16(opcode);
	desc->flags = cpu_to_le16(IXGBE_ACI_FLAG_SI);
}

/**
 * ixgbe_aci_req_res - request a common resource
 * @hw: pointer to the HW struct
 * @res: resource ID
 * @access: access type
 * @sdp_number: resource number
 * @timeout: the maximum time in ms that the driver may hold the resource
 *
 * Requests a common resource using the ACI command (0x0008).
 * Specifies the maximum time the driver may hold the resource.
 * If the requested resource is currently occupied by some other driver,
 * a busy return value is returned and the timeout field value indicates the
 * maximum time the current owner has to free it.
 *
 * Return: the exit code of the operation.
 */
static int ixgbe_aci_req_res(struct ixgbe_hw *hw, enum ixgbe_aci_res_ids res,
			     enum ixgbe_aci_res_access_type access,
			     u8 sdp_number, u32 *timeout)
{
	struct ixgbe_aci_cmd_req_res *cmd_resp;
	struct ixgbe_aci_desc desc;
	int err;

	cmd_resp = &desc.params.res_owner;

	ixgbe_fill_dflt_direct_cmd_desc(&desc, ixgbe_aci_opc_req_res);

	cmd_resp->res_id = cpu_to_le16(res);
	cmd_resp->access_type = cpu_to_le16(access);
	cmd_resp->res_number = cpu_to_le32(sdp_number);
	cmd_resp->timeout = cpu_to_le32(*timeout);
	*timeout = 0;

	err = ixgbe_aci_send_cmd(hw, &desc, NULL, 0);

	/* If the resource is held by some other driver, the command completes
	 * with a busy return value and the timeout field indicates the maximum
	 * time the current owner of the resource has to free it.
	 */
	if (!err || hw->aci.last_status == IXGBE_ACI_RC_EBUSY)
		*timeout = le32_to_cpu(cmd_resp->timeout);

	return err;
}

/**
 * ixgbe_aci_release_res - release a common resource using ACI
 * @hw: pointer to the HW struct
 * @res: resource ID
 * @sdp_number: resource number
 *
 * Release a common resource using ACI command (0x0009).
 *
 * Return: the exit code of the operation.
 */
static int ixgbe_aci_release_res(struct ixgbe_hw *hw,
				 enum ixgbe_aci_res_ids res, u8 sdp_number)
{
	struct ixgbe_aci_cmd_req_res *cmd;
	struct ixgbe_aci_desc desc;

	cmd = &desc.params.res_owner;

	ixgbe_fill_dflt_direct_cmd_desc(&desc, ixgbe_aci_opc_release_res);

	cmd->res_id = cpu_to_le16(res);
	cmd->res_number = cpu_to_le32(sdp_number);

	return ixgbe_aci_send_cmd(hw, &desc, NULL, 0);
}

/**
 * ixgbe_acquire_res - acquire the ownership of a resource
 * @hw: pointer to the HW structure
 * @res: resource ID
 * @access: access type (read or write)
 * @timeout: timeout in milliseconds
 *
 * Make an attempt to acquire the ownership of a resource using
 * the ixgbe_aci_req_res to utilize ACI.
 * In case if some other driver has previously acquired the resource and
 * performed any necessary updates, the -EALREADY is returned,
 * and the caller does not obtain the resource and has no further work to do.
 * If needed, the function will poll until the current lock owner timeouts.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_acquire_res(struct ixgbe_hw *hw, enum ixgbe_aci_res_ids res,
		      enum ixgbe_aci_res_access_type access, u32 timeout)
{
#define IXGBE_RES_POLLING_DELAY_MS	10
	u32 delay = IXGBE_RES_POLLING_DELAY_MS;
	u32 res_timeout = timeout;
	u32 retry_timeout;
	int err;

	err = ixgbe_aci_req_res(hw, res, access, 0, &res_timeout);

	/* A return code of -EALREADY means that another driver has
	 * previously acquired the resource and performed any necessary updates;
	 * in this case the caller does not obtain the resource and has no
	 * further work to do.
	 */
	if (err == -EALREADY)
		return err;

	/* If necessary, poll until the current lock owner timeouts.
	 * Set retry_timeout to the timeout value reported by the FW in the
	 * response to the "Request Resource Ownership" (0x0008) Admin Command
	 * as it indicates the maximum time the current owner of the resource
	 * is allowed to hold it.
	 */
	retry_timeout = res_timeout;
	while (err && retry_timeout && res_timeout) {
		msleep(delay);
		retry_timeout = (retry_timeout > delay) ?
			retry_timeout - delay : 0;
		err = ixgbe_aci_req_res(hw, res, access, 0, &res_timeout);

		/* Success - lock acquired.
		 * -EALREADY - lock free, no work to do.
		 */
		if (!err || err == -EALREADY)
			break;
	}

	return err;
}

/**
 * ixgbe_release_res - release a common resource
 * @hw: pointer to the HW structure
 * @res: resource ID
 *
 * Release a common resource using ixgbe_aci_release_res.
 */
void ixgbe_release_res(struct ixgbe_hw *hw, enum ixgbe_aci_res_ids res)
{
	u32 total_delay = 0;
	int err;

	err = ixgbe_aci_release_res(hw, res, 0);

	/* There are some rare cases when trying to release the resource
	 * results in an admin command timeout, so handle them correctly.
	 */
	while (err == -ETIME &&
	       total_delay < IXGBE_ACI_RELEASE_RES_TIMEOUT) {
		usleep_range(1000, 1500);
		err = ixgbe_aci_release_res(hw, res, 0);
		total_delay++;
	}
}

/**
 * ixgbe_parse_e610_caps - Parse common device/function capabilities
 * @hw: pointer to the HW struct
 * @caps: pointer to common capabilities structure
 * @elem: the capability element to parse
 * @prefix: message prefix for tracing capabilities
 *
 * Given a capability element, extract relevant details into the common
 * capability structure.
 *
 * Return: true if the capability matches one of the common capability ids,
 * false otherwise.
 */
static bool ixgbe_parse_e610_caps(struct ixgbe_hw *hw,
				  struct ixgbe_hw_caps *caps,
				  struct ixgbe_aci_cmd_list_caps_elem *elem,
				  const char *prefix)
{
	u32 logical_id = le32_to_cpu(elem->logical_id);
	u32 phys_id = le32_to_cpu(elem->phys_id);
	u32 number = le32_to_cpu(elem->number);
	u16 cap = le16_to_cpu(elem->cap);

	switch (cap) {
	case IXGBE_ACI_CAPS_VALID_FUNCTIONS:
		caps->valid_functions = number;
		break;
	case IXGBE_ACI_CAPS_SRIOV:
		caps->sr_iov_1_1 = (number == 1);
		break;
	case IXGBE_ACI_CAPS_VMDQ:
		caps->vmdq = (number == 1);
		break;
	case IXGBE_ACI_CAPS_DCB:
		caps->dcb = (number == 1);
		caps->active_tc_bitmap = logical_id;
		caps->maxtc = phys_id;
		break;
	case IXGBE_ACI_CAPS_RSS:
		caps->rss_table_size = number;
		caps->rss_table_entry_width = logical_id;
		break;
	case IXGBE_ACI_CAPS_RXQS:
		caps->num_rxq = number;
		caps->rxq_first_id = phys_id;
		break;
	case IXGBE_ACI_CAPS_TXQS:
		caps->num_txq = number;
		caps->txq_first_id = phys_id;
		break;
	case IXGBE_ACI_CAPS_MSIX:
		caps->num_msix_vectors = number;
		caps->msix_vector_first_id = phys_id;
		break;
	case IXGBE_ACI_CAPS_NVM_VER:
		break;
	case IXGBE_ACI_CAPS_MAX_MTU:
		caps->max_mtu = number;
		break;
	case IXGBE_ACI_CAPS_PCIE_RESET_AVOIDANCE:
		caps->pcie_reset_avoidance = (number > 0);
		break;
	case IXGBE_ACI_CAPS_POST_UPDATE_RESET_RESTRICT:
		caps->reset_restrict_support = (number == 1);
		break;
	case IXGBE_ACI_CAPS_EXT_TOPO_DEV_IMG0:
	case IXGBE_ACI_CAPS_EXT_TOPO_DEV_IMG1:
	case IXGBE_ACI_CAPS_EXT_TOPO_DEV_IMG2:
	case IXGBE_ACI_CAPS_EXT_TOPO_DEV_IMG3:
	{
		u8 index = cap - IXGBE_ACI_CAPS_EXT_TOPO_DEV_IMG0;

		caps->ext_topo_dev_img_ver_high[index] = number;
		caps->ext_topo_dev_img_ver_low[index] = logical_id;
		caps->ext_topo_dev_img_part_num[index] =
			FIELD_GET(IXGBE_EXT_TOPO_DEV_IMG_PART_NUM_M, phys_id);
		caps->ext_topo_dev_img_load_en[index] =
			(phys_id & IXGBE_EXT_TOPO_DEV_IMG_LOAD_EN) != 0;
		caps->ext_topo_dev_img_prog_en[index] =
			(phys_id & IXGBE_EXT_TOPO_DEV_IMG_PROG_EN) != 0;
		break;
	}
	default:
		/* Not one of the recognized common capabilities */
		return false;
	}

	return true;
}

/**
 * ixgbe_parse_valid_functions_cap - Parse IXGBE_ACI_CAPS_VALID_FUNCTIONS caps
 * @hw: pointer to the HW struct
 * @dev_p: pointer to device capabilities structure
 * @cap: capability element to parse
 *
 * Parse IXGBE_ACI_CAPS_VALID_FUNCTIONS for device capabilities.
 */
static void
ixgbe_parse_valid_functions_cap(struct ixgbe_hw *hw,
				struct ixgbe_hw_dev_caps *dev_p,
				struct ixgbe_aci_cmd_list_caps_elem *cap)
{
	dev_p->num_funcs = hweight32(le32_to_cpu(cap->number));
}

/**
 * ixgbe_parse_vf_dev_caps - Parse IXGBE_ACI_CAPS_VF device caps
 * @hw: pointer to the HW struct
 * @dev_p: pointer to device capabilities structure
 * @cap: capability element to parse
 *
 * Parse IXGBE_ACI_CAPS_VF for device capabilities.
 */
static void ixgbe_parse_vf_dev_caps(struct ixgbe_hw *hw,
				    struct ixgbe_hw_dev_caps *dev_p,
				    struct ixgbe_aci_cmd_list_caps_elem *cap)
{
	dev_p->num_vfs_exposed = le32_to_cpu(cap->number);
}

/**
 * ixgbe_parse_vsi_dev_caps - Parse IXGBE_ACI_CAPS_VSI device caps
 * @hw: pointer to the HW struct
 * @dev_p: pointer to device capabilities structure
 * @cap: capability element to parse
 *
 * Parse IXGBE_ACI_CAPS_VSI for device capabilities.
 */
static void ixgbe_parse_vsi_dev_caps(struct ixgbe_hw *hw,
				     struct ixgbe_hw_dev_caps *dev_p,
				     struct ixgbe_aci_cmd_list_caps_elem *cap)
{
	dev_p->num_vsi_allocd_to_host = le32_to_cpu(cap->number);
}

/**
 * ixgbe_parse_fdir_dev_caps - Parse IXGBE_ACI_CAPS_FD device caps
 * @hw: pointer to the HW struct
 * @dev_p: pointer to device capabilities structure
 * @cap: capability element to parse
 *
 * Parse IXGBE_ACI_CAPS_FD for device capabilities.
 */
static void ixgbe_parse_fdir_dev_caps(struct ixgbe_hw *hw,
				      struct ixgbe_hw_dev_caps *dev_p,
				      struct ixgbe_aci_cmd_list_caps_elem *cap)
{
	dev_p->num_flow_director_fltr = le32_to_cpu(cap->number);
}

/**
 * ixgbe_parse_dev_caps - Parse device capabilities
 * @hw: pointer to the HW struct
 * @dev_p: pointer to device capabilities structure
 * @buf: buffer containing the device capability records
 * @cap_count: the number of capabilities
 *
 * Helper device to parse device (0x000B) capabilities list. For
 * capabilities shared between device and function, this relies on
 * ixgbe_parse_e610_caps.
 *
 * Loop through the list of provided capabilities and extract the relevant
 * data into the device capabilities structured.
 */
static void ixgbe_parse_dev_caps(struct ixgbe_hw *hw,
				 struct ixgbe_hw_dev_caps *dev_p,
				 void *buf, u32 cap_count)
{
	struct ixgbe_aci_cmd_list_caps_elem *cap_resp;
	u32 i;

	cap_resp = (struct ixgbe_aci_cmd_list_caps_elem *)buf;

	memset(dev_p, 0, sizeof(*dev_p));

	for (i = 0; i < cap_count; i++) {
		u16 cap = le16_to_cpu(cap_resp[i].cap);

		ixgbe_parse_e610_caps(hw, &dev_p->common_cap, &cap_resp[i],
				      "dev caps");

		switch (cap) {
		case IXGBE_ACI_CAPS_VALID_FUNCTIONS:
			ixgbe_parse_valid_functions_cap(hw, dev_p,
							&cap_resp[i]);
			break;
		case IXGBE_ACI_CAPS_VF:
			ixgbe_parse_vf_dev_caps(hw, dev_p, &cap_resp[i]);
			break;
		case IXGBE_ACI_CAPS_VSI:
			ixgbe_parse_vsi_dev_caps(hw, dev_p, &cap_resp[i]);
			break;
		case  IXGBE_ACI_CAPS_FD:
			ixgbe_parse_fdir_dev_caps(hw, dev_p, &cap_resp[i]);
			break;
		default:
			/* Don't list common capabilities as unknown */
			break;
		}
	}
}

/**
 * ixgbe_parse_vf_func_caps - Parse IXGBE_ACI_CAPS_VF function caps
 * @hw: pointer to the HW struct
 * @func_p: pointer to function capabilities structure
 * @cap: pointer to the capability element to parse
 *
 * Extract function capabilities for IXGBE_ACI_CAPS_VF.
 */
static void ixgbe_parse_vf_func_caps(struct ixgbe_hw *hw,
				     struct ixgbe_hw_func_caps *func_p,
				     struct ixgbe_aci_cmd_list_caps_elem *cap)
{
	func_p->num_allocd_vfs = le32_to_cpu(cap->number);
	func_p->vf_base_id = le32_to_cpu(cap->logical_id);
}

/**
 * ixgbe_get_num_per_func - determine number of resources per PF
 * @hw: pointer to the HW structure
 * @max: value to be evenly split between each PF
 *
 * Determine the number of valid functions by going through the bitmap returned
 * from parsing capabilities and use this to calculate the number of resources
 * per PF based on the max value passed in.
 *
 * Return: the number of resources per PF or 0, if no PH are available.
 */
static u32 ixgbe_get_num_per_func(struct ixgbe_hw *hw, u32 max)
{
#define IXGBE_CAPS_VALID_FUNCS_M	GENMASK(7, 0)
	u8 funcs = hweight8(hw->dev_caps.common_cap.valid_functions &
			    IXGBE_CAPS_VALID_FUNCS_M);

	return funcs ? (max / funcs) : 0;
}

/**
 * ixgbe_parse_vsi_func_caps - Parse IXGBE_ACI_CAPS_VSI function caps
 * @hw: pointer to the HW struct
 * @func_p: pointer to function capabilities structure
 * @cap: pointer to the capability element to parse
 *
 * Extract function capabilities for IXGBE_ACI_CAPS_VSI.
 */
static void ixgbe_parse_vsi_func_caps(struct ixgbe_hw *hw,
				      struct ixgbe_hw_func_caps *func_p,
				      struct ixgbe_aci_cmd_list_caps_elem *cap)
{
	func_p->guar_num_vsi = ixgbe_get_num_per_func(hw, IXGBE_MAX_VSI);
}

/**
 * ixgbe_parse_func_caps - Parse function capabilities
 * @hw: pointer to the HW struct
 * @func_p: pointer to function capabilities structure
 * @buf: buffer containing the function capability records
 * @cap_count: the number of capabilities
 *
 * Helper function to parse function (0x000A) capabilities list. For
 * capabilities shared between device and function, this relies on
 * ixgbe_parse_e610_caps.
 *
 * Loop through the list of provided capabilities and extract the relevant
 * data into the function capabilities structured.
 */
static void ixgbe_parse_func_caps(struct ixgbe_hw *hw,
				  struct ixgbe_hw_func_caps *func_p,
				  void *buf, u32 cap_count)
{
	struct ixgbe_aci_cmd_list_caps_elem *cap_resp;
	u32 i;

	cap_resp = (struct ixgbe_aci_cmd_list_caps_elem *)buf;

	memset(func_p, 0, sizeof(*func_p));

	for (i = 0; i < cap_count; i++) {
		u16 cap = le16_to_cpu(cap_resp[i].cap);

		ixgbe_parse_e610_caps(hw, &func_p->common_cap,
				      &cap_resp[i], "func caps");

		switch (cap) {
		case IXGBE_ACI_CAPS_VF:
			ixgbe_parse_vf_func_caps(hw, func_p, &cap_resp[i]);
			break;
		case IXGBE_ACI_CAPS_VSI:
			ixgbe_parse_vsi_func_caps(hw, func_p, &cap_resp[i]);
			break;
		default:
			/* Don't list common capabilities as unknown */
			break;
		}
	}
}

/**
 * ixgbe_aci_list_caps - query function/device capabilities
 * @hw: pointer to the HW struct
 * @buf: a buffer to hold the capabilities
 * @buf_size: size of the buffer
 * @cap_count: if not NULL, set to the number of capabilities reported
 * @opc: capabilities type to discover, device or function
 *
 * Get the function (0x000A) or device (0x000B) capabilities description from
 * firmware and store it in the buffer.
 *
 * If the cap_count pointer is not NULL, then it is set to the number of
 * capabilities firmware will report. Note that if the buffer size is too
 * small, it is possible the command will return -ENOMEM. The
 * cap_count will still be updated in this case. It is recommended that the
 * buffer size be set to IXGBE_ACI_MAX_BUFFER_SIZE (the largest possible
 * buffer that firmware could return) to avoid this.
 *
 * Return: the exit code of the operation.
 * Exit code of -ENOMEM means the buffer size is too small.
 */
int ixgbe_aci_list_caps(struct ixgbe_hw *hw, void *buf, u16 buf_size,
			u32 *cap_count, enum ixgbe_aci_opc opc)
{
	struct ixgbe_aci_cmd_list_caps *cmd;
	struct ixgbe_aci_desc desc;
	int err;

	cmd = &desc.params.get_cap;

	if (opc != ixgbe_aci_opc_list_func_caps &&
	    opc != ixgbe_aci_opc_list_dev_caps)
		return -EINVAL;

	ixgbe_fill_dflt_direct_cmd_desc(&desc, opc);
	err = ixgbe_aci_send_cmd(hw, &desc, buf, buf_size);

	if (cap_count)
		*cap_count = le32_to_cpu(cmd->count);

	return err;
}

/**
 * ixgbe_discover_dev_caps - Read and extract device capabilities
 * @hw: pointer to the hardware structure
 * @dev_caps: pointer to device capabilities structure
 *
 * Read the device capabilities and extract them into the dev_caps structure
 * for later use.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_discover_dev_caps(struct ixgbe_hw *hw,
			    struct ixgbe_hw_dev_caps *dev_caps)
{
	u32 cap_count;
	u8 *cbuf;
	int err;

	cbuf = kzalloc(IXGBE_ACI_MAX_BUFFER_SIZE, GFP_KERNEL);
	if (!cbuf)
		return -ENOMEM;

	/* Although the driver doesn't know the number of capabilities the
	 * device will return, we can simply send a 4KB buffer, the maximum
	 * possible size that firmware can return.
	 */
	cap_count = IXGBE_ACI_MAX_BUFFER_SIZE /
		    sizeof(struct ixgbe_aci_cmd_list_caps_elem);

	err = ixgbe_aci_list_caps(hw, cbuf, IXGBE_ACI_MAX_BUFFER_SIZE,
				  &cap_count,
				  ixgbe_aci_opc_list_dev_caps);
	if (!err)
		ixgbe_parse_dev_caps(hw, dev_caps, cbuf, cap_count);

	kfree(cbuf);

	return 0;
}

/**
 * ixgbe_discover_func_caps - Read and extract function capabilities
 * @hw: pointer to the hardware structure
 * @func_caps: pointer to function capabilities structure
 *
 * Read the function capabilities and extract them into the func_caps structure
 * for later use.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_discover_func_caps(struct ixgbe_hw *hw,
			     struct ixgbe_hw_func_caps *func_caps)
{
	u32 cap_count;
	u8 *cbuf;
	int err;

	cbuf = kzalloc(IXGBE_ACI_MAX_BUFFER_SIZE, GFP_KERNEL);
	if (!cbuf)
		return -ENOMEM;

	/* Although the driver doesn't know the number of capabilities the
	 * device will return, we can simply send a 4KB buffer, the maximum
	 * possible size that firmware can return.
	 */
	cap_count = IXGBE_ACI_MAX_BUFFER_SIZE /
		    sizeof(struct ixgbe_aci_cmd_list_caps_elem);

	err = ixgbe_aci_list_caps(hw, cbuf, IXGBE_ACI_MAX_BUFFER_SIZE,
				  &cap_count,
				  ixgbe_aci_opc_list_func_caps);
	if (!err)
		ixgbe_parse_func_caps(hw, func_caps, cbuf, cap_count);

	kfree(cbuf);

	return 0;
}

/**
 * ixgbe_get_caps - get info about the HW
 * @hw: pointer to the hardware structure
 *
 * Retrieve both device and function capabilities.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_get_caps(struct ixgbe_hw *hw)
{
	int err;

	err = ixgbe_discover_dev_caps(hw, &hw->dev_caps);
	if (err)
		return err;

	return ixgbe_discover_func_caps(hw, &hw->func_caps);
}

/**
 * ixgbe_aci_disable_rxen - disable RX
 * @hw: pointer to the HW struct
 *
 * Request a safe disable of Receive Enable using ACI command (0x000C).
 *
 * Return: the exit code of the operation.
 */
int ixgbe_aci_disable_rxen(struct ixgbe_hw *hw)
{
	struct ixgbe_aci_cmd_disable_rxen *cmd;
	struct ixgbe_aci_desc desc;

	cmd = &desc.params.disable_rxen;

	ixgbe_fill_dflt_direct_cmd_desc(&desc, ixgbe_aci_opc_disable_rxen);

	cmd->lport_num = hw->bus.func;

	return ixgbe_aci_send_cmd(hw, &desc, NULL, 0);
}

/**
 * ixgbe_aci_get_phy_caps - returns PHY capabilities
 * @hw: pointer to the HW struct
 * @qual_mods: report qualified modules
 * @report_mode: report mode capabilities
 * @pcaps: structure for PHY capabilities to be filled
 *
 * Returns the various PHY capabilities supported on the Port
 * using ACI command (0x0600).
 *
 * Return: the exit code of the operation.
 */
int ixgbe_aci_get_phy_caps(struct ixgbe_hw *hw, bool qual_mods, u8 report_mode,
			   struct ixgbe_aci_cmd_get_phy_caps_data *pcaps)
{
	struct ixgbe_aci_cmd_get_phy_caps *cmd;
	u16 pcaps_size = sizeof(*pcaps);
	struct ixgbe_aci_desc desc;
	int err;

	cmd = &desc.params.get_phy;

	if (!pcaps || (report_mode & ~IXGBE_ACI_REPORT_MODE_M))
		return -EINVAL;

	ixgbe_fill_dflt_direct_cmd_desc(&desc, ixgbe_aci_opc_get_phy_caps);

	if (qual_mods)
		cmd->param0 |= cpu_to_le16(IXGBE_ACI_GET_PHY_RQM);

	cmd->param0 |= cpu_to_le16(report_mode);
	err = ixgbe_aci_send_cmd(hw, &desc, pcaps, pcaps_size);
	if (!err && report_mode == IXGBE_ACI_REPORT_TOPO_CAP_MEDIA) {
		hw->phy.phy_type_low = le64_to_cpu(pcaps->phy_type_low);
		hw->phy.phy_type_high = le64_to_cpu(pcaps->phy_type_high);
		memcpy(hw->link.link_info.module_type, &pcaps->module_type,
		       sizeof(hw->link.link_info.module_type));
	}

	return err;
}

/**
 * ixgbe_copy_phy_caps_to_cfg - Copy PHY ability data to configuration data
 * @caps: PHY ability structure to copy data from
 * @cfg: PHY configuration structure to copy data to
 *
 * Helper function to copy data from PHY capabilities data structure
 * to PHY configuration data structure
 */
void ixgbe_copy_phy_caps_to_cfg(struct ixgbe_aci_cmd_get_phy_caps_data *caps,
				struct ixgbe_aci_cmd_set_phy_cfg_data *cfg)
{
	if (!caps || !cfg)
		return;

	memset(cfg, 0, sizeof(*cfg));
	cfg->phy_type_low = caps->phy_type_low;
	cfg->phy_type_high = caps->phy_type_high;
	cfg->caps = caps->caps;
	cfg->low_power_ctrl_an = caps->low_power_ctrl_an;
	cfg->eee_cap = caps->eee_cap;
	cfg->eeer_value = caps->eeer_value;
	cfg->link_fec_opt = caps->link_fec_options;
	cfg->module_compliance_enforcement =
		caps->module_compliance_enforcement;
}

/**
 * ixgbe_aci_set_phy_cfg - set PHY configuration
 * @hw: pointer to the HW struct
 * @cfg: structure with PHY configuration data to be set
 *
 * Set the various PHY configuration parameters supported on the Port
 * using ACI command (0x0601).
 * One or more of the Set PHY config parameters may be ignored in an MFP
 * mode as the PF may not have the privilege to set some of the PHY Config
 * parameters.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_aci_set_phy_cfg(struct ixgbe_hw *hw,
			  struct ixgbe_aci_cmd_set_phy_cfg_data *cfg)
{
	struct ixgbe_aci_desc desc;
	int err;

	if (!cfg)
		return -EINVAL;

	/* Ensure that only valid bits of cfg->caps can be turned on. */
	cfg->caps &= IXGBE_ACI_PHY_ENA_VALID_MASK;

	ixgbe_fill_dflt_direct_cmd_desc(&desc, ixgbe_aci_opc_set_phy_cfg);
	desc.params.set_phy.lport_num = hw->bus.func;
	desc.flags |= cpu_to_le16(IXGBE_ACI_FLAG_RD);

	err = ixgbe_aci_send_cmd(hw, &desc, cfg, sizeof(*cfg));
	if (!err)
		hw->phy.curr_user_phy_cfg = *cfg;

	return err;
}

/**
 * ixgbe_aci_set_link_restart_an - set up link and restart AN
 * @hw: pointer to the HW struct
 * @ena_link: if true: enable link, if false: disable link
 *
 * Function sets up the link and restarts the Auto-Negotiation over the link.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_aci_set_link_restart_an(struct ixgbe_hw *hw, bool ena_link)
{
	struct ixgbe_aci_cmd_restart_an *cmd;
	struct ixgbe_aci_desc desc;

	cmd = &desc.params.restart_an;

	ixgbe_fill_dflt_direct_cmd_desc(&desc, ixgbe_aci_opc_restart_an);

	cmd->cmd_flags = IXGBE_ACI_RESTART_AN_LINK_RESTART;
	cmd->lport_num = hw->bus.func;
	if (ena_link)
		cmd->cmd_flags |= IXGBE_ACI_RESTART_AN_LINK_ENABLE;
	else
		cmd->cmd_flags &= ~IXGBE_ACI_RESTART_AN_LINK_ENABLE;

	return ixgbe_aci_send_cmd(hw, &desc, NULL, 0);
}

/**
 * ixgbe_is_media_cage_present - check if media cage is present
 * @hw: pointer to the HW struct
 *
 * Identify presence of media cage using the ACI command (0x06E0).
 *
 * Return: true if media cage is present, else false. If no cage, then
 * media type is backplane or BASE-T.
 */
static bool ixgbe_is_media_cage_present(struct ixgbe_hw *hw)
{
	struct ixgbe_aci_cmd_get_link_topo *cmd;
	struct ixgbe_aci_desc desc;

	cmd = &desc.params.get_link_topo;

	ixgbe_fill_dflt_direct_cmd_desc(&desc, ixgbe_aci_opc_get_link_topo);

	cmd->addr.topo_params.node_type_ctx =
		FIELD_PREP(IXGBE_ACI_LINK_TOPO_NODE_CTX_M,
			   IXGBE_ACI_LINK_TOPO_NODE_CTX_PORT);

	/* Set node type. */
	cmd->addr.topo_params.node_type_ctx |=
		FIELD_PREP(IXGBE_ACI_LINK_TOPO_NODE_TYPE_M,
			   IXGBE_ACI_LINK_TOPO_NODE_TYPE_CAGE);

	/* Node type cage can be used to determine if cage is present. If AQC
	 * returns error (ENOENT), then no cage present. If no cage present then
	 * connection type is backplane or BASE-T.
	 */
	return !ixgbe_aci_get_netlist_node(hw, cmd, NULL, NULL);
}

/**
 * ixgbe_get_media_type_from_phy_type - Gets media type based on phy type
 * @hw: pointer to the HW struct
 *
 * Try to identify the media type based on the phy type.
 * If more than one media type, the ixgbe_media_type_unknown is returned.
 * First, phy_type_low is checked, then phy_type_high.
 * If none are identified, the ixgbe_media_type_unknown is returned
 *
 * Return: type of a media based on phy type in form of enum.
 */
static enum ixgbe_media_type
ixgbe_get_media_type_from_phy_type(struct ixgbe_hw *hw)
{
	struct ixgbe_link_status *hw_link_info;

	if (!hw)
		return ixgbe_media_type_unknown;

	hw_link_info = &hw->link.link_info;
	if (hw_link_info->phy_type_low && hw_link_info->phy_type_high)
		/* If more than one media type is selected, report unknown */
		return ixgbe_media_type_unknown;

	if (hw_link_info->phy_type_low) {
		/* 1G SGMII is a special case where some DA cable PHYs
		 * may show this as an option when it really shouldn't
		 * be since SGMII is meant to be between a MAC and a PHY
		 * in a backplane. Try to detect this case and handle it
		 */
		if (hw_link_info->phy_type_low == IXGBE_PHY_TYPE_LOW_1G_SGMII &&
		    (hw_link_info->module_type[IXGBE_ACI_MOD_TYPE_IDENT] ==
		    IXGBE_ACI_MOD_TYPE_BYTE1_SFP_PLUS_CU_ACTIVE ||
		    hw_link_info->module_type[IXGBE_ACI_MOD_TYPE_IDENT] ==
		    IXGBE_ACI_MOD_TYPE_BYTE1_SFP_PLUS_CU_PASSIVE))
			return ixgbe_media_type_da;

		switch (hw_link_info->phy_type_low) {
		case IXGBE_PHY_TYPE_LOW_1000BASE_SX:
		case IXGBE_PHY_TYPE_LOW_1000BASE_LX:
		case IXGBE_PHY_TYPE_LOW_10GBASE_SR:
		case IXGBE_PHY_TYPE_LOW_10GBASE_LR:
		case IXGBE_PHY_TYPE_LOW_25GBASE_SR:
		case IXGBE_PHY_TYPE_LOW_25GBASE_LR:
			return ixgbe_media_type_fiber;
		case IXGBE_PHY_TYPE_LOW_10G_SFI_AOC_ACC:
		case IXGBE_PHY_TYPE_LOW_25G_AUI_AOC_ACC:
			return ixgbe_media_type_fiber;
		case IXGBE_PHY_TYPE_LOW_100BASE_TX:
		case IXGBE_PHY_TYPE_LOW_1000BASE_T:
		case IXGBE_PHY_TYPE_LOW_2500BASE_T:
		case IXGBE_PHY_TYPE_LOW_5GBASE_T:
		case IXGBE_PHY_TYPE_LOW_10GBASE_T:
		case IXGBE_PHY_TYPE_LOW_25GBASE_T:
			return ixgbe_media_type_copper;
		case IXGBE_PHY_TYPE_LOW_10G_SFI_DA:
		case IXGBE_PHY_TYPE_LOW_25GBASE_CR:
		case IXGBE_PHY_TYPE_LOW_25GBASE_CR_S:
		case IXGBE_PHY_TYPE_LOW_25GBASE_CR1:
			return ixgbe_media_type_da;
		case IXGBE_PHY_TYPE_LOW_25G_AUI_C2C:
			if (ixgbe_is_media_cage_present(hw))
				return ixgbe_media_type_aui;
			fallthrough;
		case IXGBE_PHY_TYPE_LOW_1000BASE_KX:
		case IXGBE_PHY_TYPE_LOW_2500BASE_KX:
		case IXGBE_PHY_TYPE_LOW_2500BASE_X:
		case IXGBE_PHY_TYPE_LOW_5GBASE_KR:
		case IXGBE_PHY_TYPE_LOW_10GBASE_KR_CR1:
		case IXGBE_PHY_TYPE_LOW_10G_SFI_C2C:
		case IXGBE_PHY_TYPE_LOW_25GBASE_KR:
		case IXGBE_PHY_TYPE_LOW_25GBASE_KR1:
		case IXGBE_PHY_TYPE_LOW_25GBASE_KR_S:
			return ixgbe_media_type_backplane;
		}
	} else {
		switch (hw_link_info->phy_type_high) {
		case IXGBE_PHY_TYPE_HIGH_10BASE_T:
			return ixgbe_media_type_copper;
		}
	}
	return ixgbe_media_type_unknown;
}

/**
 * ixgbe_update_link_info - update status of the HW network link
 * @hw: pointer to the HW struct
 *
 * Update the status of the HW network link.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_update_link_info(struct ixgbe_hw *hw)
{
	struct ixgbe_aci_cmd_get_phy_caps_data *pcaps;
	struct ixgbe_link_status *li;
	int err;

	if (!hw)
		return -EINVAL;

	li = &hw->link.link_info;

	err = ixgbe_aci_get_link_info(hw, true, NULL);
	if (err)
		return err;

	if (!(li->link_info & IXGBE_ACI_MEDIA_AVAILABLE))
		return 0;

	pcaps =	kzalloc(sizeof(*pcaps), GFP_KERNEL);
	if (!pcaps)
		return -ENOMEM;

	err = ixgbe_aci_get_phy_caps(hw, false, IXGBE_ACI_REPORT_TOPO_CAP_MEDIA,
				     pcaps);

	if (!err)
		memcpy(li->module_type, &pcaps->module_type,
		       sizeof(li->module_type));

	kfree(pcaps);

	return err;
}

/**
 * ixgbe_get_link_status - get status of the HW network link
 * @hw: pointer to the HW struct
 * @link_up: pointer to bool (true/false = linkup/linkdown)
 *
 * Variable link_up is true if link is up, false if link is down.
 * The variable link_up is invalid if status is non zero. As a
 * result of this call, link status reporting becomes enabled
 *
 * Return: the exit code of the operation.
 */
int ixgbe_get_link_status(struct ixgbe_hw *hw, bool *link_up)
{
	if (!hw || !link_up)
		return -EINVAL;

	if (hw->link.get_link_info) {
		int err = ixgbe_update_link_info(hw);

		if (err)
			return err;
	}

	*link_up = hw->link.link_info.link_info & IXGBE_ACI_LINK_UP;

	return 0;
}

/**
 * ixgbe_aci_get_link_info - get the link status
 * @hw: pointer to the HW struct
 * @ena_lse: enable/disable LinkStatusEvent reporting
 * @link: pointer to link status structure - optional
 *
 * Get the current Link Status using ACI command (0x607).
 * The current link can be optionally provided to update
 * the status.
 *
 * Return: the link status of the adapter.
 */
int ixgbe_aci_get_link_info(struct ixgbe_hw *hw, bool ena_lse,
			    struct ixgbe_link_status *link)
{
	struct ixgbe_aci_cmd_get_link_status_data link_data = {};
	struct ixgbe_aci_cmd_get_link_status *resp;
	struct ixgbe_link_status *li_old, *li;
	struct ixgbe_fc_info *hw_fc_info;
	struct ixgbe_aci_desc desc;
	bool tx_pause, rx_pause;
	u8 cmd_flags;
	int err;

	if (!hw)
		return -EINVAL;

	li_old = &hw->link.link_info_old;
	li = &hw->link.link_info;
	hw_fc_info = &hw->fc;

	ixgbe_fill_dflt_direct_cmd_desc(&desc, ixgbe_aci_opc_get_link_status);
	cmd_flags = (ena_lse) ? IXGBE_ACI_LSE_ENA : IXGBE_ACI_LSE_DIS;
	resp = &desc.params.get_link_status;
	resp->cmd_flags = cpu_to_le16(cmd_flags);
	resp->lport_num = hw->bus.func;

	err = ixgbe_aci_send_cmd(hw, &desc, &link_data, sizeof(link_data));
	if (err)
		return err;

	/* Save off old link status information. */
	*li_old = *li;

	/* Update current link status information. */
	li->link_speed = le16_to_cpu(link_data.link_speed);
	li->phy_type_low = le64_to_cpu(link_data.phy_type_low);
	li->phy_type_high = le64_to_cpu(link_data.phy_type_high);
	li->link_info = link_data.link_info;
	li->link_cfg_err = link_data.link_cfg_err;
	li->an_info = link_data.an_info;
	li->ext_info = link_data.ext_info;
	li->max_frame_size = le16_to_cpu(link_data.max_frame_size);
	li->fec_info = link_data.cfg & IXGBE_ACI_FEC_MASK;
	li->topo_media_conflict = link_data.topo_media_conflict;
	li->pacing = link_data.cfg & (IXGBE_ACI_CFG_PACING_M |
				      IXGBE_ACI_CFG_PACING_TYPE_M);

	/* Update fc info. */
	tx_pause = !!(link_data.an_info & IXGBE_ACI_LINK_PAUSE_TX);
	rx_pause = !!(link_data.an_info & IXGBE_ACI_LINK_PAUSE_RX);
	if (tx_pause && rx_pause)
		hw_fc_info->current_mode = ixgbe_fc_full;
	else if (tx_pause)
		hw_fc_info->current_mode = ixgbe_fc_tx_pause;
	else if (rx_pause)
		hw_fc_info->current_mode = ixgbe_fc_rx_pause;
	else
		hw_fc_info->current_mode = ixgbe_fc_none;

	li->lse_ena = !!(le16_to_cpu(resp->cmd_flags) &
			 IXGBE_ACI_LSE_IS_ENABLED);

	/* Save link status information. */
	if (link)
		*link = *li;

	/* Flag cleared so calling functions don't call AQ again. */
	hw->link.get_link_info = false;

	return 0;
}

/**
 * ixgbe_aci_set_event_mask - set event mask
 * @hw: pointer to the HW struct
 * @port_num: port number of the physical function
 * @mask: event mask to be set
 *
 * Set the event mask using ACI command (0x0613).
 *
 * Return: the exit code of the operation.
 */
int ixgbe_aci_set_event_mask(struct ixgbe_hw *hw, u8 port_num, u16 mask)
{
	struct ixgbe_aci_cmd_set_event_mask *cmd;
	struct ixgbe_aci_desc desc;

	cmd = &desc.params.set_event_mask;

	ixgbe_fill_dflt_direct_cmd_desc(&desc, ixgbe_aci_opc_set_event_mask);

	cmd->lport_num = port_num;

	cmd->event_mask = cpu_to_le16(mask);
	return ixgbe_aci_send_cmd(hw, &desc, NULL, 0);
}

/**
 * ixgbe_configure_lse - enable/disable link status events
 * @hw: pointer to the HW struct
 * @activate: true for enable lse, false otherwise
 * @mask: event mask to be set; a set bit means deactivation of the
 * corresponding event
 *
 * Set the event mask and then enable or disable link status events
 *
 * Return: the exit code of the operation.
 */
int ixgbe_configure_lse(struct ixgbe_hw *hw, bool activate, u16 mask)
{
	int err;

	err = ixgbe_aci_set_event_mask(hw, (u8)hw->bus.func, mask);
	if (err)
		return err;

	/* Enabling link status events generation by fw. */
	return ixgbe_aci_get_link_info(hw, activate, NULL);
}

/**
 * ixgbe_get_media_type_e610 - Gets media type
 * @hw: pointer to the HW struct
 *
 * In order to get the media type, the function gets PHY
 * capabilities and later on use them to identify the PHY type
 * checking phy_type_high and phy_type_low.
 *
 * Return: the type of media in form of ixgbe_media_type enum
 * or ixgbe_media_type_unknown in case of an error.
 */
enum ixgbe_media_type ixgbe_get_media_type_e610(struct ixgbe_hw *hw)
{
	struct ixgbe_aci_cmd_get_phy_caps_data pcaps;
	int rc;

	rc = ixgbe_update_link_info(hw);
	if (rc)
		return ixgbe_media_type_unknown;

	/* If there is no link but PHY (dongle) is available SW should use
	 * Get PHY Caps admin command instead of Get Link Status, find most
	 * significant bit that is set in PHY types reported by the command
	 * and use it to discover media type.
	 */
	if (!(hw->link.link_info.link_info & IXGBE_ACI_LINK_UP) &&
	    (hw->link.link_info.link_info & IXGBE_ACI_MEDIA_AVAILABLE)) {
		int highest_bit;

		/* Get PHY Capabilities */
		rc = ixgbe_aci_get_phy_caps(hw, false,
					    IXGBE_ACI_REPORT_TOPO_CAP_MEDIA,
					    &pcaps);
		if (rc)
			return ixgbe_media_type_unknown;

		highest_bit = fls64(le64_to_cpu(pcaps.phy_type_high));
		if (highest_bit) {
			hw->link.link_info.phy_type_high =
				BIT_ULL(highest_bit - 1);
			hw->link.link_info.phy_type_low = 0;
		} else {
			highest_bit = fls64(le64_to_cpu(pcaps.phy_type_low));
			if (highest_bit) {
				hw->link.link_info.phy_type_low =
					BIT_ULL(highest_bit - 1);
				hw->link.link_info.phy_type_high = 0;
			}
		}
	}

	/* Based on link status or search above try to discover media type. */
	hw->phy.media_type = ixgbe_get_media_type_from_phy_type(hw);

	return hw->phy.media_type;
}

/**
 * ixgbe_setup_link_e610 - Set up link
 * @hw: pointer to hardware structure
 * @speed: new link speed
 * @autoneg_wait: true when waiting for completion is needed
 *
 * Set up the link with the specified speed.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_setup_link_e610(struct ixgbe_hw *hw, ixgbe_link_speed speed,
			  bool autoneg_wait)
{
	/* Simply request FW to perform proper PHY setup */
	return hw->phy.ops.setup_link_speed(hw, speed, autoneg_wait);
}

/**
 * ixgbe_check_link_e610 - Determine link and speed status
 * @hw: pointer to hardware structure
 * @speed: pointer to link speed
 * @link_up: true when link is up
 * @link_up_wait_to_complete: bool used to wait for link up or not
 *
 * Determine if the link is up and the current link speed
 * using ACI command (0x0607).
 *
 * Return: the exit code of the operation.
 */
int ixgbe_check_link_e610(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
			  bool *link_up, bool link_up_wait_to_complete)
{
	int err;
	u32 i;

	if (!speed || !link_up)
		return -EINVAL;

	/* Set get_link_info flag to ensure that fresh
	 * link information will be obtained from FW
	 * by sending Get Link Status admin command.
	 */
	hw->link.get_link_info = true;

	/* Update link information in adapter context. */
	err = ixgbe_get_link_status(hw, link_up);
	if (err)
		return err;

	/* Wait for link up if it was requested. */
	if (link_up_wait_to_complete && !(*link_up)) {
		for (i = 0; i < hw->mac.max_link_up_time; i++) {
			msleep(100);
			hw->link.get_link_info = true;
			err = ixgbe_get_link_status(hw, link_up);
			if (err)
				return err;
			if (*link_up)
				break;
		}
	}

	/* Use link information in adapter context updated by the call
	 * to ixgbe_get_link_status() to determine current link speed.
	 * Link speed information is valid only when link up was
	 * reported by FW.
	 */
	if (*link_up) {
		switch (hw->link.link_info.link_speed) {
		case IXGBE_ACI_LINK_SPEED_10MB:
			*speed = IXGBE_LINK_SPEED_10_FULL;
			break;
		case IXGBE_ACI_LINK_SPEED_100MB:
			*speed = IXGBE_LINK_SPEED_100_FULL;
			break;
		case IXGBE_ACI_LINK_SPEED_1000MB:
			*speed = IXGBE_LINK_SPEED_1GB_FULL;
			break;
		case IXGBE_ACI_LINK_SPEED_2500MB:
			*speed = IXGBE_LINK_SPEED_2_5GB_FULL;
			break;
		case IXGBE_ACI_LINK_SPEED_5GB:
			*speed = IXGBE_LINK_SPEED_5GB_FULL;
			break;
		case IXGBE_ACI_LINK_SPEED_10GB:
			*speed = IXGBE_LINK_SPEED_10GB_FULL;
			break;
		default:
			*speed = IXGBE_LINK_SPEED_UNKNOWN;
			break;
		}
	} else {
		*speed = IXGBE_LINK_SPEED_UNKNOWN;
	}

	return 0;
}

/**
 * ixgbe_get_link_capabilities_e610 - Determine link capabilities
 * @hw: pointer to hardware structure
 * @speed: pointer to link speed
 * @autoneg: true when autoneg or autotry is enabled
 *
 * Determine speed and AN parameters of a link.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_get_link_capabilities_e610(struct ixgbe_hw *hw,
				     ixgbe_link_speed *speed,
				     bool *autoneg)
{
	if (!speed || !autoneg)
		return -EINVAL;

	*autoneg = true;
	*speed = hw->phy.speeds_supported;

	return 0;
}

/**
 * ixgbe_cfg_phy_fc - Configure PHY Flow Control (FC) data based on FC mode
 * @hw: pointer to hardware structure
 * @cfg: PHY configuration data to set FC mode
 * @req_mode: FC mode to configure
 *
 * Configures PHY Flow Control according to the provided configuration.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_cfg_phy_fc(struct ixgbe_hw *hw,
		     struct ixgbe_aci_cmd_set_phy_cfg_data *cfg,
		     enum ixgbe_fc_mode req_mode)
{
	u8 pause_mask = 0x0;

	if (!cfg)
		return -EINVAL;

	switch (req_mode) {
	case ixgbe_fc_full:
		pause_mask |= IXGBE_ACI_PHY_EN_TX_LINK_PAUSE;
		pause_mask |= IXGBE_ACI_PHY_EN_RX_LINK_PAUSE;
		break;
	case ixgbe_fc_rx_pause:
		pause_mask |= IXGBE_ACI_PHY_EN_RX_LINK_PAUSE;
		break;
	case ixgbe_fc_tx_pause:
		pause_mask |= IXGBE_ACI_PHY_EN_TX_LINK_PAUSE;
		break;
	default:
		break;
	}

	/* Clear the old pause settings. */
	cfg->caps &= ~(IXGBE_ACI_PHY_EN_TX_LINK_PAUSE |
		IXGBE_ACI_PHY_EN_RX_LINK_PAUSE);

	/* Set the new capabilities. */
	cfg->caps |= pause_mask;

	return 0;
}

/**
 * ixgbe_setup_fc_e610 - Set up flow control
 * @hw: pointer to hardware structure
 *
 * Set up flow control. This has to be done during init time.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_setup_fc_e610(struct ixgbe_hw *hw)
{
	struct ixgbe_aci_cmd_get_phy_caps_data pcaps = {};
	struct ixgbe_aci_cmd_set_phy_cfg_data cfg = {};
	int err;

	/* Get the current PHY config */
	err = ixgbe_aci_get_phy_caps(hw, false,
				     IXGBE_ACI_REPORT_ACTIVE_CFG, &pcaps);
	if (err)
		return err;

	ixgbe_copy_phy_caps_to_cfg(&pcaps, &cfg);

	/* Configure the set PHY data */
	err = ixgbe_cfg_phy_fc(hw, &cfg, hw->fc.requested_mode);
	if (err)
		return err;

	/* If the capabilities have changed, then set the new config */
	if (cfg.caps != pcaps.caps) {
		cfg.caps |= IXGBE_ACI_PHY_ENA_AUTO_LINK_UPDT;

		err = ixgbe_aci_set_phy_cfg(hw, &cfg);
		if (err)
			return err;
	}

	return err;
}

/**
 * ixgbe_fc_autoneg_e610 - Configure flow control
 * @hw: pointer to hardware structure
 *
 * Configure Flow Control.
 */
void ixgbe_fc_autoneg_e610(struct ixgbe_hw *hw)
{
	int err;

	/* Get current link err.
	 * Current FC mode will be stored in the hw context.
	 */
	err = ixgbe_aci_get_link_info(hw, false, NULL);
	if (err)
		goto no_autoneg;

	/* Check if the link is up */
	if (!(hw->link.link_info.link_info & IXGBE_ACI_LINK_UP))
		goto no_autoneg;

	/* Check if auto-negotiation has completed */
	if (!(hw->link.link_info.an_info & IXGBE_ACI_AN_COMPLETED))
		goto no_autoneg;

	hw->fc.fc_was_autonegged = true;
	return;

no_autoneg:
	hw->fc.fc_was_autonegged = false;
	hw->fc.current_mode = hw->fc.requested_mode;
}

/**
 * ixgbe_disable_rx_e610 - Disable RX unit
 * @hw: pointer to hardware structure
 *
 * Disable RX DMA unit on E610 with use of ACI command (0x000C).
 *
 * Return: the exit code of the operation.
 */
void ixgbe_disable_rx_e610(struct ixgbe_hw *hw)
{
	u32 rxctrl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	u32 pfdtxgswc;
	int err;

	if (!(rxctrl & IXGBE_RXCTRL_RXEN))
		return;

	pfdtxgswc = IXGBE_READ_REG(hw, IXGBE_PFDTXGSWC);
	if (pfdtxgswc & IXGBE_PFDTXGSWC_VT_LBEN) {
		pfdtxgswc &= ~IXGBE_PFDTXGSWC_VT_LBEN;
		IXGBE_WRITE_REG(hw, IXGBE_PFDTXGSWC, pfdtxgswc);
		hw->mac.set_lben = true;
	} else {
		hw->mac.set_lben = false;
	}

	err = ixgbe_aci_disable_rxen(hw);

	/* If we fail - disable RX using register write */
	if (err) {
		rxctrl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
		if (rxctrl & IXGBE_RXCTRL_RXEN) {
			rxctrl &= ~IXGBE_RXCTRL_RXEN;
			IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, rxctrl);
		}
	}
}

/**
 * ixgbe_init_phy_ops_e610 - PHY specific init
 * @hw: pointer to hardware structure
 *
 * Initialize any function pointers that were not able to be
 * set during init_shared_code because the PHY type was not known.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_init_phy_ops_e610(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	struct ixgbe_phy_info *phy = &hw->phy;

	if (mac->ops.get_media_type(hw) == ixgbe_media_type_copper)
		phy->ops.set_phy_power = ixgbe_set_phy_power_e610;
	else
		phy->ops.set_phy_power = NULL;

	/* Identify the PHY */
	return phy->ops.identify(hw);
}

/**
 * ixgbe_identify_phy_e610 - Identify PHY
 * @hw: pointer to hardware structure
 *
 * Determine PHY type, supported speeds and PHY ID.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_identify_phy_e610(struct ixgbe_hw *hw)
{
	struct ixgbe_aci_cmd_get_phy_caps_data pcaps;
	u64 phy_type_low, phy_type_high;
	int err;

	/* Set PHY type */
	hw->phy.type = ixgbe_phy_fw;

	err = ixgbe_aci_get_phy_caps(hw, false,
				     IXGBE_ACI_REPORT_TOPO_CAP_MEDIA, &pcaps);
	if (err)
		return err;

	if (!(pcaps.module_compliance_enforcement &
	      IXGBE_ACI_MOD_ENFORCE_STRICT_MODE)) {
		/* Handle lenient mode */
		err = ixgbe_aci_get_phy_caps(hw, false,
					     IXGBE_ACI_REPORT_TOPO_CAP_NO_MEDIA,
					     &pcaps);
		if (err)
			return err;
	}

	/* Determine supported speeds */
	hw->phy.speeds_supported = IXGBE_LINK_SPEED_UNKNOWN;
	phy_type_high = le64_to_cpu(pcaps.phy_type_high);
	phy_type_low = le64_to_cpu(pcaps.phy_type_low);

	if (phy_type_high & IXGBE_PHY_TYPE_HIGH_10BASE_T ||
	    phy_type_high & IXGBE_PHY_TYPE_HIGH_10M_SGMII)
		hw->phy.speeds_supported |= IXGBE_LINK_SPEED_10_FULL;
	if (phy_type_low  & IXGBE_PHY_TYPE_LOW_100BASE_TX ||
	    phy_type_low  & IXGBE_PHY_TYPE_LOW_100M_SGMII ||
	    phy_type_high & IXGBE_PHY_TYPE_HIGH_100M_USXGMII)
		hw->phy.speeds_supported |= IXGBE_LINK_SPEED_100_FULL;
	if (phy_type_low  & IXGBE_PHY_TYPE_LOW_1000BASE_T  ||
	    phy_type_low  & IXGBE_PHY_TYPE_LOW_1000BASE_SX ||
	    phy_type_low  & IXGBE_PHY_TYPE_LOW_1000BASE_LX ||
	    phy_type_low  & IXGBE_PHY_TYPE_LOW_1000BASE_KX ||
	    phy_type_low  & IXGBE_PHY_TYPE_LOW_1G_SGMII    ||
	    phy_type_high & IXGBE_PHY_TYPE_HIGH_1G_USXGMII)
		hw->phy.speeds_supported |= IXGBE_LINK_SPEED_1GB_FULL;
	if (phy_type_low  & IXGBE_PHY_TYPE_LOW_10GBASE_T       ||
	    phy_type_low  & IXGBE_PHY_TYPE_LOW_10G_SFI_DA      ||
	    phy_type_low  & IXGBE_PHY_TYPE_LOW_10GBASE_SR      ||
	    phy_type_low  & IXGBE_PHY_TYPE_LOW_10GBASE_LR      ||
	    phy_type_low  & IXGBE_PHY_TYPE_LOW_10GBASE_KR_CR1  ||
	    phy_type_low  & IXGBE_PHY_TYPE_LOW_10G_SFI_AOC_ACC ||
	    phy_type_low  & IXGBE_PHY_TYPE_LOW_10G_SFI_C2C     ||
	    phy_type_high & IXGBE_PHY_TYPE_HIGH_10G_USXGMII)
		hw->phy.speeds_supported |= IXGBE_LINK_SPEED_10GB_FULL;

	/* 2.5 and 5 Gbps link speeds must be excluded from the
	 * auto-negotiation set used during driver initialization due to
	 * compatibility issues with certain switches. Those issues do not
	 * exist in case of E610 2.5G SKU device (0x57b1).
	 */
	if (!hw->phy.autoneg_advertised &&
	    hw->device_id != IXGBE_DEV_ID_E610_2_5G_T)
		hw->phy.autoneg_advertised = hw->phy.speeds_supported;

	if (phy_type_low  & IXGBE_PHY_TYPE_LOW_2500BASE_T   ||
	    phy_type_low  & IXGBE_PHY_TYPE_LOW_2500BASE_X   ||
	    phy_type_low  & IXGBE_PHY_TYPE_LOW_2500BASE_KX  ||
	    phy_type_high & IXGBE_PHY_TYPE_HIGH_2500M_SGMII ||
	    phy_type_high & IXGBE_PHY_TYPE_HIGH_2500M_USXGMII)
		hw->phy.speeds_supported |= IXGBE_LINK_SPEED_2_5GB_FULL;

	if (!hw->phy.autoneg_advertised &&
	    hw->device_id == IXGBE_DEV_ID_E610_2_5G_T)
		hw->phy.autoneg_advertised = hw->phy.speeds_supported;

	if (phy_type_low  & IXGBE_PHY_TYPE_LOW_5GBASE_T  ||
	    phy_type_low  & IXGBE_PHY_TYPE_LOW_5GBASE_KR ||
	    phy_type_high & IXGBE_PHY_TYPE_HIGH_5G_USXGMII)
		hw->phy.speeds_supported |= IXGBE_LINK_SPEED_5GB_FULL;

	/* Set PHY ID */
	memcpy(&hw->phy.id, pcaps.phy_id_oui, sizeof(u32));

	hw->phy.eee_speeds_supported = IXGBE_LINK_SPEED_10_FULL |
				       IXGBE_LINK_SPEED_100_FULL |
				       IXGBE_LINK_SPEED_1GB_FULL;
	hw->phy.eee_speeds_advertised = hw->phy.eee_speeds_supported;

	return 0;
}

/**
 * ixgbe_identify_module_e610 - Identify SFP module type
 * @hw: pointer to hardware structure
 *
 * Identify the SFP module type.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_identify_module_e610(struct ixgbe_hw *hw)
{
	bool media_available;
	u8 module_type;
	int err;

	err = ixgbe_update_link_info(hw);
	if (err)
		return err;

	media_available =
		(hw->link.link_info.link_info & IXGBE_ACI_MEDIA_AVAILABLE);

	if (media_available) {
		hw->phy.sfp_type = ixgbe_sfp_type_unknown;

		/* Get module type from hw context updated by
		 * ixgbe_update_link_info()
		 */
		module_type = hw->link.link_info.module_type[IXGBE_ACI_MOD_TYPE_IDENT];

		if ((module_type & IXGBE_ACI_MOD_TYPE_BYTE1_SFP_PLUS_CU_PASSIVE) ||
		    (module_type & IXGBE_ACI_MOD_TYPE_BYTE1_SFP_PLUS_CU_ACTIVE)) {
			hw->phy.sfp_type = ixgbe_sfp_type_da_cu;
		} else if (module_type & IXGBE_ACI_MOD_TYPE_BYTE1_10G_BASE_SR) {
			hw->phy.sfp_type = ixgbe_sfp_type_sr;
		} else if ((module_type & IXGBE_ACI_MOD_TYPE_BYTE1_10G_BASE_LR) ||
			   (module_type & IXGBE_ACI_MOD_TYPE_BYTE1_10G_BASE_LRM)) {
			hw->phy.sfp_type = ixgbe_sfp_type_lr;
		}
	} else {
		hw->phy.sfp_type = ixgbe_sfp_type_not_present;
		return -ENOENT;
	}

	return 0;
}

/**
 * ixgbe_setup_phy_link_e610 - Sets up firmware-controlled PHYs
 * @hw: pointer to hardware structure
 *
 * Set the parameters for the firmware-controlled PHYs.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_setup_phy_link_e610(struct ixgbe_hw *hw)
{
	struct ixgbe_aci_cmd_get_phy_caps_data pcaps;
	struct ixgbe_aci_cmd_set_phy_cfg_data pcfg;
	u8 rmode = IXGBE_ACI_REPORT_TOPO_CAP_MEDIA;
	u64 sup_phy_type_low, sup_phy_type_high;
	u64 phy_type_low = 0, phy_type_high = 0;
	int err;

	err = ixgbe_aci_get_link_info(hw, false, NULL);
	if (err)
		return err;

	/* If media is not available get default config. */
	if (!(hw->link.link_info.link_info & IXGBE_ACI_MEDIA_AVAILABLE))
		rmode = IXGBE_ACI_REPORT_DFLT_CFG;

	err = ixgbe_aci_get_phy_caps(hw, false, rmode, &pcaps);
	if (err)
		return err;

	sup_phy_type_low = le64_to_cpu(pcaps.phy_type_low);
	sup_phy_type_high = le64_to_cpu(pcaps.phy_type_high);

	/* Get Active configuration to avoid unintended changes. */
	err = ixgbe_aci_get_phy_caps(hw, false, IXGBE_ACI_REPORT_ACTIVE_CFG,
				     &pcaps);
	if (err)
		return err;

	ixgbe_copy_phy_caps_to_cfg(&pcaps, &pcfg);

	if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_10_FULL) {
		phy_type_high |= IXGBE_PHY_TYPE_HIGH_10BASE_T;
		phy_type_high |= IXGBE_PHY_TYPE_HIGH_10M_SGMII;
	}
	if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_100_FULL) {
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_100BASE_TX;
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_100M_SGMII;
		phy_type_high |= IXGBE_PHY_TYPE_HIGH_100M_USXGMII;
	}
	if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_1GB_FULL) {
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_1000BASE_T;
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_1000BASE_SX;
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_1000BASE_LX;
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_1000BASE_KX;
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_1G_SGMII;
		phy_type_high |= IXGBE_PHY_TYPE_HIGH_1G_USXGMII;
	}
	if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_2_5GB_FULL) {
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_2500BASE_T;
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_2500BASE_X;
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_2500BASE_KX;
		phy_type_high |= IXGBE_PHY_TYPE_HIGH_2500M_SGMII;
		phy_type_high |= IXGBE_PHY_TYPE_HIGH_2500M_USXGMII;
	}
	if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_5GB_FULL) {
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_5GBASE_T;
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_5GBASE_KR;
		phy_type_high |= IXGBE_PHY_TYPE_HIGH_5G_USXGMII;
	}
	if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_10GB_FULL) {
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_10GBASE_T;
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_10G_SFI_DA;
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_10GBASE_SR;
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_10GBASE_LR;
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_10GBASE_KR_CR1;
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_10G_SFI_AOC_ACC;
		phy_type_low  |= IXGBE_PHY_TYPE_LOW_10G_SFI_C2C;
		phy_type_high |= IXGBE_PHY_TYPE_HIGH_10G_USXGMII;
	}

	/* Mask the set values to avoid requesting unsupported link types. */
	phy_type_low &= sup_phy_type_low;
	pcfg.phy_type_low = cpu_to_le64(phy_type_low);
	phy_type_high &= sup_phy_type_high;
	pcfg.phy_type_high = cpu_to_le64(phy_type_high);

	if (pcfg.phy_type_high != pcaps.phy_type_high ||
	    pcfg.phy_type_low != pcaps.phy_type_low ||
	    pcfg.caps != pcaps.caps) {
		pcfg.caps |= IXGBE_ACI_PHY_ENA_LINK;
		pcfg.caps |= IXGBE_ACI_PHY_ENA_AUTO_LINK_UPDT;

		err = ixgbe_aci_set_phy_cfg(hw, &pcfg);
		if (err)
			return err;
	}

	return 0;
}

/**
 * ixgbe_set_phy_power_e610 - Control power for copper PHY
 * @hw: pointer to hardware structure
 * @on: true for on, false for off
 *
 * Set the power on/off of the PHY
 * by getting its capabilities and setting the appropriate
 * configuration parameters.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_set_phy_power_e610(struct ixgbe_hw *hw, bool on)
{
	struct ixgbe_aci_cmd_get_phy_caps_data phy_caps = {};
	struct ixgbe_aci_cmd_set_phy_cfg_data phy_cfg = {};
	int err;

	err = ixgbe_aci_get_phy_caps(hw, false,
				     IXGBE_ACI_REPORT_ACTIVE_CFG,
				     &phy_caps);
	if (err)
		return err;

	ixgbe_copy_phy_caps_to_cfg(&phy_caps, &phy_cfg);

	if (on)
		phy_cfg.caps &= ~IXGBE_ACI_PHY_ENA_LOW_POWER;
	else
		phy_cfg.caps |= IXGBE_ACI_PHY_ENA_LOW_POWER;

	/* PHY is already in requested power mode. */
	if (phy_caps.caps == phy_cfg.caps)
		return 0;

	phy_cfg.caps |= IXGBE_ACI_PHY_ENA_LINK;
	phy_cfg.caps |= IXGBE_ACI_PHY_ENA_AUTO_LINK_UPDT;

	return ixgbe_aci_set_phy_cfg(hw, &phy_cfg);
}

/**
 * ixgbe_enter_lplu_e610 - Transition to low power states
 * @hw: pointer to hardware structure
 *
 * Configures Low Power Link Up on transition to low power states
 * (from D0 to non-D0). Link is required to enter LPLU so avoid resetting the
 * X557 PHY immediately prior to entering LPLU.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_enter_lplu_e610(struct ixgbe_hw *hw)
{
	struct ixgbe_aci_cmd_get_phy_caps_data phy_caps = {};
	struct ixgbe_aci_cmd_set_phy_cfg_data phy_cfg = {};
	int err;

	err = ixgbe_aci_get_phy_caps(hw, false,
				     IXGBE_ACI_REPORT_ACTIVE_CFG,
				     &phy_caps);
	if (err)
		return err;

	ixgbe_copy_phy_caps_to_cfg(&phy_caps, &phy_cfg);

	phy_cfg.low_power_ctrl_an |= IXGBE_ACI_PHY_EN_D3COLD_LOW_POWER_AUTONEG;

	return ixgbe_aci_set_phy_cfg(hw, &phy_cfg);
}

/**
 * ixgbe_init_eeprom_params_e610 - Initialize EEPROM params
 * @hw: pointer to hardware structure
 *
 * Initialize the EEPROM parameters ixgbe_eeprom_info within the ixgbe_hw
 * struct in order to set up EEPROM access.
 *
 * Return: the operation exit code.
 */
int ixgbe_init_eeprom_params_e610(struct ixgbe_hw *hw)
{
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	u32 gens_stat;
	u8 sr_size;

	if (eeprom->type != ixgbe_eeprom_uninitialized)
		return 0;

	eeprom->type = ixgbe_flash;

	gens_stat = IXGBE_READ_REG(hw, GLNVM_GENS);
	sr_size = FIELD_GET(GLNVM_GENS_SR_SIZE_M, gens_stat);

	/* Switching to words (sr_size contains power of 2). */
	eeprom->word_size = BIT(sr_size) * IXGBE_SR_WORDS_IN_1KB;

	hw_dbg(hw, "Eeprom params: type = %d, size = %d\n", eeprom->type,
	       eeprom->word_size);

	return 0;
}

/**
 * ixgbe_aci_get_netlist_node - get a node handle
 * @hw: pointer to the hw struct
 * @cmd: get_link_topo AQ structure
 * @node_part_number: output node part number if node found
 * @node_handle: output node handle parameter if node found
 *
 * Get the netlist node and assigns it to
 * the provided handle using ACI command (0x06E0).
 *
 * Return: the exit code of the operation.
 */
int ixgbe_aci_get_netlist_node(struct ixgbe_hw *hw,
			       struct ixgbe_aci_cmd_get_link_topo *cmd,
			       u8 *node_part_number, u16 *node_handle)
{
	struct ixgbe_aci_desc desc;

	ixgbe_fill_dflt_direct_cmd_desc(&desc, ixgbe_aci_opc_get_link_topo);
	desc.params.get_link_topo = *cmd;

	if (ixgbe_aci_send_cmd(hw, &desc, NULL, 0))
		return -EOPNOTSUPP;

	if (node_handle)
		*node_handle =
			le16_to_cpu(desc.params.get_link_topo.addr.handle);
	if (node_part_number)
		*node_part_number = desc.params.get_link_topo.node_part_num;

	return 0;
}

/**
 * ixgbe_acquire_nvm - Generic request for acquiring the NVM ownership
 * @hw: pointer to the HW structure
 * @access: NVM access type (read or write)
 *
 * Request NVM ownership.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_acquire_nvm(struct ixgbe_hw *hw,
		      enum ixgbe_aci_res_access_type access)
{
	u32 fla;

	/* Skip if we are in blank NVM programming mode */
	fla = IXGBE_READ_REG(hw, IXGBE_GLNVM_FLA);
	if ((fla & IXGBE_GLNVM_FLA_LOCKED_M) == 0)
		return 0;

	return ixgbe_acquire_res(hw, IXGBE_NVM_RES_ID, access,
				 IXGBE_NVM_TIMEOUT);
}

/**
 * ixgbe_release_nvm - Generic request for releasing the NVM ownership
 * @hw: pointer to the HW structure
 *
 * Release NVM ownership.
 */
void ixgbe_release_nvm(struct ixgbe_hw *hw)
{
	u32 fla;

	/* Skip if we are in blank NVM programming mode */
	fla = IXGBE_READ_REG(hw, IXGBE_GLNVM_FLA);
	if ((fla & IXGBE_GLNVM_FLA_LOCKED_M) == 0)
		return;

	ixgbe_release_res(hw, IXGBE_NVM_RES_ID);
}

/**
 * ixgbe_aci_read_nvm - read NVM
 * @hw: pointer to the HW struct
 * @module_typeid: module pointer location in words from the NVM beginning
 * @offset: byte offset from the module beginning
 * @length: length of the section to be read (in bytes from the offset)
 * @data: command buffer (size [bytes] = length)
 * @last_command: tells if this is the last command in a series
 * @read_shadow_ram: tell if this is a shadow RAM read
 *
 * Read the NVM using ACI command (0x0701).
 *
 * Return: the exit code of the operation.
 */
int ixgbe_aci_read_nvm(struct ixgbe_hw *hw, u16 module_typeid, u32 offset,
		       u16 length, void *data, bool last_command,
		       bool read_shadow_ram)
{
	struct ixgbe_aci_cmd_nvm *cmd;
	struct ixgbe_aci_desc desc;

	if (offset > IXGBE_ACI_NVM_MAX_OFFSET)
		return -EINVAL;

	cmd = &desc.params.nvm;

	ixgbe_fill_dflt_direct_cmd_desc(&desc, ixgbe_aci_opc_nvm_read);

	if (!read_shadow_ram && module_typeid == IXGBE_ACI_NVM_START_POINT)
		cmd->cmd_flags |= IXGBE_ACI_NVM_FLASH_ONLY;

	/* If this is the last command in a series, set the proper flag. */
	if (last_command)
		cmd->cmd_flags |= IXGBE_ACI_NVM_LAST_CMD;
	cmd->module_typeid = cpu_to_le16(module_typeid);
	cmd->offset_low = cpu_to_le16(offset & 0xFFFF);
	cmd->offset_high = (offset >> 16) & 0xFF;
	cmd->length = cpu_to_le16(length);

	return ixgbe_aci_send_cmd(hw, &desc, data, length);
}

/**
 * ixgbe_nvm_validate_checksum - validate checksum
 * @hw: pointer to the HW struct
 *
 * Verify NVM PFA checksum validity using ACI command (0x0706).
 * If the checksum verification failed, IXGBE_ERR_NVM_CHECKSUM is returned.
 * The function acquires and then releases the NVM ownership.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_nvm_validate_checksum(struct ixgbe_hw *hw)
{
	struct ixgbe_aci_cmd_nvm_checksum *cmd;
	struct ixgbe_aci_desc desc;
	int err;

	err = ixgbe_acquire_nvm(hw, IXGBE_RES_READ);
	if (err)
		return err;

	cmd = &desc.params.nvm_checksum;

	ixgbe_fill_dflt_direct_cmd_desc(&desc, ixgbe_aci_opc_nvm_checksum);
	cmd->flags = IXGBE_ACI_NVM_CHECKSUM_VERIFY;

	err = ixgbe_aci_send_cmd(hw, &desc, NULL, 0);

	ixgbe_release_nvm(hw);

	if (!err && cmd->checksum !=
		cpu_to_le16(IXGBE_ACI_NVM_CHECKSUM_CORRECT)) {
		struct ixgbe_adapter *adapter = container_of(hw, struct ixgbe_adapter,
							     hw);

		err = -EIO;
		netdev_err(adapter->netdev, "Invalid Shadow Ram checksum");
	}

	return err;
}

/**
 * ixgbe_read_sr_word_aci - Reads Shadow RAM via ACI
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM word to read (0x000000 - 0x001FFF)
 * @data: word read from the Shadow RAM
 *
 * Reads one 16 bit word from the Shadow RAM using ixgbe_read_flat_nvm.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_read_sr_word_aci(struct ixgbe_hw  *hw, u16 offset, u16 *data)
{
	u32 bytes = sizeof(u16);
	u16 data_local;
	int err;

	err = ixgbe_read_flat_nvm(hw, offset * sizeof(u16), &bytes,
				  (u8 *)&data_local, true);
	if (err)
		return err;

	*data = data_local;
	return 0;
}

/**
 * ixgbe_read_flat_nvm - Read portion of NVM by flat offset
 * @hw: pointer to the HW struct
 * @offset: offset from beginning of NVM
 * @length: (in) number of bytes to read; (out) number of bytes actually read
 * @data: buffer to return data in (sized to fit the specified length)
 * @read_shadow_ram: if true, read from shadow RAM instead of NVM
 *
 * Reads a portion of the NVM, as a flat memory space. This function correctly
 * breaks read requests across Shadow RAM sectors, prevents Shadow RAM size
 * from being exceeded in case of Shadow RAM read requests and ensures that no
 * single read request exceeds the maximum 4KB read for a single admin command.
 *
 * Returns an error code on failure. Note that the data pointer may be
 * partially updated if some reads succeed before a failure.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_read_flat_nvm(struct ixgbe_hw  *hw, u32 offset, u32 *length,
			u8 *data, bool read_shadow_ram)
{
	u32 inlen = *length;
	u32 bytes_read = 0;
	bool last_cmd;
	int err;

	/* Verify the length of the read if this is for the Shadow RAM */
	if (read_shadow_ram && ((offset + inlen) >
				(hw->eeprom.word_size * 2u)))
		return -EINVAL;

	do {
		u32 read_size, sector_offset;

		/* ixgbe_aci_read_nvm cannot read more than 4KB at a time.
		 * Additionally, a read from the Shadow RAM may not cross over
		 * a sector boundary. Conveniently, the sector size is also 4KB.
		 */
		sector_offset = offset % IXGBE_ACI_MAX_BUFFER_SIZE;
		read_size = min_t(u32,
				  IXGBE_ACI_MAX_BUFFER_SIZE - sector_offset,
				  inlen - bytes_read);

		last_cmd = !(bytes_read + read_size < inlen);

		/* ixgbe_aci_read_nvm takes the length as a u16. Our read_size
		 * is calculated using a u32, but the IXGBE_ACI_MAX_BUFFER_SIZE
		 * maximum size guarantees that it will fit within the 2 bytes.
		 */
		err = ixgbe_aci_read_nvm(hw, IXGBE_ACI_NVM_START_POINT,
					 offset, (u16)read_size,
					 data + bytes_read, last_cmd,
					 read_shadow_ram);
		if (err)
			break;

		bytes_read += read_size;
		offset += read_size;
	} while (!last_cmd);

	*length = bytes_read;
	return err;
}

/**
 * ixgbe_read_sr_buf_aci - Read Shadow RAM buffer via ACI
 * @hw: pointer to the HW structure
 * @offset: offset of the Shadow RAM words to read (0x000000 - 0x001FFF)
 * @words: (in) number of words to read; (out) number of words actually read
 * @data: words read from the Shadow RAM
 *
 * Read 16 bit words (data buf) from the Shadow RAM. Acquire/release the NVM
 * ownership.
 *
 * Return: the operation exit code.
 */
int ixgbe_read_sr_buf_aci(struct ixgbe_hw *hw, u16 offset, u16 *words,
			  u16 *data)
{
	u32 bytes = *words * 2;
	int err;

	err = ixgbe_read_flat_nvm(hw, offset * 2, &bytes, (u8 *)data, true);
	if (err)
		return err;

	*words = bytes / 2;

	for (int i = 0; i < *words; i++)
		data[i] = le16_to_cpu(((__le16 *)data)[i]);

	return 0;
}

/**
 * ixgbe_read_ee_aci_e610 - Read EEPROM word using the admin command.
 * @hw: pointer to hardware structure
 * @offset: offset of  word in the EEPROM to read
 * @data: word read from the EEPROM
 *
 * Reads a 16 bit word from the EEPROM using the ACI.
 * If the EEPROM params are not initialized, the function
 * initialize them before proceeding with reading.
 * The function acquires and then releases the NVM ownership.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_read_ee_aci_e610(struct ixgbe_hw *hw, u16 offset, u16 *data)
{
	int err;

	if (hw->eeprom.type == ixgbe_eeprom_uninitialized) {
		err = hw->eeprom.ops.init_params(hw);
		if (err)
			return err;
	}

	err = ixgbe_acquire_nvm(hw, IXGBE_RES_READ);
	if (err)
		return err;

	err = ixgbe_read_sr_word_aci(hw, offset, data);
	ixgbe_release_nvm(hw);

	return err;
}

/**
 * ixgbe_read_ee_aci_buffer_e610 - Read EEPROM words via ACI
 * @hw: pointer to hardware structure
 * @offset: offset of words in the EEPROM to read
 * @words: number of words to read
 * @data: words to read from the EEPROM
 *
 * Read 16 bit words from the EEPROM via the ACI. Initialize the EEPROM params
 * prior to the read. Acquire/release the NVM ownership.
 *
 * Return: the operation exit code.
 */
int ixgbe_read_ee_aci_buffer_e610(struct ixgbe_hw *hw, u16 offset,
				  u16 words, u16 *data)
{
	int err;

	if (hw->eeprom.type == ixgbe_eeprom_uninitialized) {
		err = hw->eeprom.ops.init_params(hw);
		if (err)
			return err;
	}

	err = ixgbe_acquire_nvm(hw, IXGBE_RES_READ);
	if (err)
		return err;

	err = ixgbe_read_sr_buf_aci(hw, offset, &words, data);
	ixgbe_release_nvm(hw);

	return err;
}

/**
 * ixgbe_validate_eeprom_checksum_e610 - Validate EEPROM checksum
 * @hw: pointer to hardware structure
 * @checksum_val: calculated checksum
 *
 * Performs checksum calculation and validates the EEPROM checksum. If the
 * caller does not need checksum_val, the value can be NULL.
 * If the EEPROM params are not initialized, the function
 * initialize them before proceeding.
 * The function acquires and then releases the NVM ownership.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_validate_eeprom_checksum_e610(struct ixgbe_hw *hw, u16 *checksum_val)
{
	int err;

	if (hw->eeprom.type == ixgbe_eeprom_uninitialized) {
		err = hw->eeprom.ops.init_params(hw);
		if (err)
			return err;
	}

	err = ixgbe_nvm_validate_checksum(hw);
	if (err)
		return err;

	if (checksum_val) {
		u16 tmp_checksum;

		err = ixgbe_acquire_nvm(hw, IXGBE_RES_READ);
		if (err)
			return err;

		err = ixgbe_read_sr_word_aci(hw, E610_SR_SW_CHECKSUM_WORD,
					     &tmp_checksum);
		ixgbe_release_nvm(hw);

		if (!err)
			*checksum_val = tmp_checksum;
	}

	return err;
}

/**
 * ixgbe_reset_hw_e610 - Perform hardware reset
 * @hw: pointer to hardware structure
 *
 * Resets the hardware by resetting the transmit and receive units, masks
 * and clears all interrupts, and performs a reset.
 *
 * Return: the exit code of the operation.
 */
int ixgbe_reset_hw_e610(struct ixgbe_hw *hw)
{
	u32 swfw_mask = hw->phy.phy_semaphore_mask;
	u32 ctrl, i;
	int err;

	/* Call adapter stop to disable tx/rx and clear interrupts */
	err = hw->mac.ops.stop_adapter(hw);
	if (err)
		goto reset_hw_out;

	/* Flush pending Tx transactions. */
	ixgbe_clear_tx_pending(hw);

	hw->phy.ops.init(hw);
mac_reset_top:
	err = hw->mac.ops.acquire_swfw_sync(hw, swfw_mask);
	if (err)
		return -EBUSY;
	ctrl = IXGBE_CTRL_RST;
	ctrl |= IXGBE_READ_REG(hw, IXGBE_CTRL);
	IXGBE_WRITE_REG(hw, IXGBE_CTRL, ctrl);
	IXGBE_WRITE_FLUSH(hw);
	hw->mac.ops.release_swfw_sync(hw, swfw_mask);

	/* Poll for reset bit to self-clear indicating reset is complete */
	for (i = 0; i < 10; i++) {
		udelay(1);
		ctrl = IXGBE_READ_REG(hw, IXGBE_CTRL);
		if (!(ctrl & IXGBE_CTRL_RST_MASK))
			break;
	}

	if (ctrl & IXGBE_CTRL_RST_MASK) {
		struct ixgbe_adapter *adapter = container_of(hw, struct ixgbe_adapter,
							     hw);

		err = -EIO;
		netdev_err(adapter->netdev, "Reset polling failed to complete.");
	}

	/* Double resets are required for recovery from certain error
	 * conditions. Between resets, it is necessary to stall to allow time
	 * for any pending HW events to complete.
	 */
	msleep(100);
	if (hw->mac.flags & IXGBE_FLAGS_DOUBLE_RESET_REQUIRED) {
		hw->mac.flags &= ~IXGBE_FLAGS_DOUBLE_RESET_REQUIRED;
		goto mac_reset_top;
	}

	/* Set the Rx packet buffer size. */
	IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(0), GENMASK(18, 17));

	/* Store the permanent mac address */
	hw->mac.ops.get_mac_addr(hw, hw->mac.perm_addr);

	/* Maximum number of Receive Address Registers. */
#define IXGBE_MAX_NUM_RAR		128

	/* Store MAC address from RAR0, clear receive address registers, and
	 * clear the multicast table.  Also reset num_rar_entries to the
	 * maximum number of Receive Address Registers, since we modify this
	 * value when programming the SAN MAC address.
	 */
	hw->mac.num_rar_entries = IXGBE_MAX_NUM_RAR;
	hw->mac.ops.init_rx_addrs(hw);

	/* Initialize bus function number */
	hw->mac.ops.set_lan_id(hw);

reset_hw_out:
	return err;
}

static const struct ixgbe_mac_operations mac_ops_e610 = {
	.init_hw			= ixgbe_init_hw_generic,
	.start_hw			= ixgbe_start_hw_X540,
	.clear_hw_cntrs			= ixgbe_clear_hw_cntrs_generic,
	.enable_rx_dma			= ixgbe_enable_rx_dma_generic,
	.get_mac_addr			= ixgbe_get_mac_addr_generic,
	.get_device_caps		= ixgbe_get_device_caps_generic,
	.stop_adapter			= ixgbe_stop_adapter_generic,
	.set_lan_id			= ixgbe_set_lan_id_multi_port_pcie,
	.set_rxpba			= ixgbe_set_rxpba_generic,
	.check_link			= ixgbe_check_link_e610,
	.blink_led_start		= ixgbe_blink_led_start_X540,
	.blink_led_stop			= ixgbe_blink_led_stop_X540,
	.set_rar			= ixgbe_set_rar_generic,
	.clear_rar			= ixgbe_clear_rar_generic,
	.set_vmdq			= ixgbe_set_vmdq_generic,
	.set_vmdq_san_mac		= ixgbe_set_vmdq_san_mac_generic,
	.clear_vmdq			= ixgbe_clear_vmdq_generic,
	.init_rx_addrs			= ixgbe_init_rx_addrs_generic,
	.update_mc_addr_list		= ixgbe_update_mc_addr_list_generic,
	.enable_mc			= ixgbe_enable_mc_generic,
	.disable_mc			= ixgbe_disable_mc_generic,
	.clear_vfta			= ixgbe_clear_vfta_generic,
	.set_vfta			= ixgbe_set_vfta_generic,
	.fc_enable			= ixgbe_fc_enable_generic,
	.set_fw_drv_ver			= ixgbe_set_fw_drv_ver_x550,
	.init_uta_tables		= ixgbe_init_uta_tables_generic,
	.set_mac_anti_spoofing		= ixgbe_set_mac_anti_spoofing,
	.set_vlan_anti_spoofing		= ixgbe_set_vlan_anti_spoofing,
	.set_source_address_pruning	=
				ixgbe_set_source_address_pruning_x550,
	.set_ethertype_anti_spoofing	=
				ixgbe_set_ethertype_anti_spoofing_x550,
	.disable_rx_buff		= ixgbe_disable_rx_buff_generic,
	.enable_rx_buff			= ixgbe_enable_rx_buff_generic,
	.enable_rx			= ixgbe_enable_rx_generic,
	.disable_rx			= ixgbe_disable_rx_e610,
	.led_on				= ixgbe_led_on_generic,
	.led_off			= ixgbe_led_off_generic,
	.init_led_link_act		= ixgbe_init_led_link_act_generic,
	.reset_hw			= ixgbe_reset_hw_e610,
	.get_media_type			= ixgbe_get_media_type_e610,
	.setup_link			= ixgbe_setup_link_e610,
	.get_link_capabilities		= ixgbe_get_link_capabilities_e610,
	.get_bus_info			= ixgbe_get_bus_info_generic,
	.acquire_swfw_sync		= ixgbe_acquire_swfw_sync_X540,
	.release_swfw_sync		= ixgbe_release_swfw_sync_X540,
	.init_swfw_sync			= ixgbe_init_swfw_sync_X540,
	.prot_autoc_read		= prot_autoc_read_generic,
	.prot_autoc_write		= prot_autoc_write_generic,
	.setup_fc			= ixgbe_setup_fc_e610,
	.fc_autoneg			= ixgbe_fc_autoneg_e610,
};

static const struct ixgbe_phy_operations phy_ops_e610 = {
	.init				= ixgbe_init_phy_ops_e610,
	.identify			= ixgbe_identify_phy_e610,
	.identify_sfp			= ixgbe_identify_module_e610,
	.setup_link_speed		= ixgbe_setup_phy_link_speed_generic,
	.setup_link			= ixgbe_setup_phy_link_e610,
	.enter_lplu			= ixgbe_enter_lplu_e610,
};

static const struct ixgbe_eeprom_operations eeprom_ops_e610 = {
	.read				= ixgbe_read_ee_aci_e610,
	.read_buffer			= ixgbe_read_ee_aci_buffer_e610,
	.validate_checksum		= ixgbe_validate_eeprom_checksum_e610,
};

const struct ixgbe_info ixgbe_e610_info = {
	.mac			= ixgbe_mac_e610,
	.get_invariants		= ixgbe_get_invariants_X540,
	.mac_ops		= &mac_ops_e610,
	.eeprom_ops		= &eeprom_ops_e610,
	.phy_ops		= &phy_ops_e610,
	.mbx_ops		= &mbx_ops_generic,
	.mvals			= ixgbe_mvals_x550em_a,
};
