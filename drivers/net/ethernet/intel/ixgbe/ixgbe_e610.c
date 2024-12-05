// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2024 Intel Corporation. */

#include "ixgbe_common.h"
#include "ixgbe_e610.h"
#include "ixgbe_type.h"
#include "ixgbe_x540.h"
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
