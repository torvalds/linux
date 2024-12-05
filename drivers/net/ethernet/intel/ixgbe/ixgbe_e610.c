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
