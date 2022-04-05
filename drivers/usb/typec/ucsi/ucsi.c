// SPDX-License-Identifier: GPL-2.0
/*
 * USB Type-C Connector System Software Interface driver
 *
 * Copyright (C) 2017, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/completion.h>
#include <linux/property.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/usb/typec_dp.h>

#include "ucsi.h"
#include "trace.h"

/*
 * UCSI_TIMEOUT_MS - PPM communication timeout
 *
 * Ideally we could use MIN_TIME_TO_RESPOND_WITH_BUSY (which is defined in UCSI
 * specification) here as reference, but unfortunately we can't. It is very
 * difficult to estimate the time it takes for the system to process the command
 * before it is actually passed to the PPM.
 */
#define UCSI_TIMEOUT_MS		5000

/*
 * UCSI_SWAP_TIMEOUT_MS - Timeout for role swap requests
 *
 * 5 seconds is close to the time it takes for CapsCounter to reach 0, so even
 * if the PPM does not generate Connector Change events before that with
 * partners that do not support USB Power Delivery, this should still work.
 */
#define UCSI_SWAP_TIMEOUT_MS	5000

static int ucsi_acknowledge_command(struct ucsi *ucsi)
{
	u64 ctrl;

	ctrl = UCSI_ACK_CC_CI;
	ctrl |= UCSI_ACK_COMMAND_COMPLETE;

	return ucsi->ops->sync_write(ucsi, UCSI_CONTROL, &ctrl, sizeof(ctrl));
}

static int ucsi_acknowledge_connector_change(struct ucsi *ucsi)
{
	u64 ctrl;

	ctrl = UCSI_ACK_CC_CI;
	ctrl |= UCSI_ACK_CONNECTOR_CHANGE;

	return ucsi->ops->sync_write(ucsi, UCSI_CONTROL, &ctrl, sizeof(ctrl));
}

static int ucsi_exec_command(struct ucsi *ucsi, u64 command);

static int ucsi_read_error(struct ucsi *ucsi)
{
	u16 error;
	int ret;

	/* Acknowledge the command that failed */
	ret = ucsi_acknowledge_command(ucsi);
	if (ret)
		return ret;

	ret = ucsi_exec_command(ucsi, UCSI_GET_ERROR_STATUS);
	if (ret < 0)
		return ret;

	ret = ucsi->ops->read(ucsi, UCSI_MESSAGE_IN, &error, sizeof(error));
	if (ret)
		return ret;

	switch (error) {
	case UCSI_ERROR_INCOMPATIBLE_PARTNER:
		return -EOPNOTSUPP;
	case UCSI_ERROR_CC_COMMUNICATION_ERR:
		return -ECOMM;
	case UCSI_ERROR_CONTRACT_NEGOTIATION_FAIL:
		return -EPROTO;
	case UCSI_ERROR_DEAD_BATTERY:
		dev_warn(ucsi->dev, "Dead battery condition!\n");
		return -EPERM;
	case UCSI_ERROR_INVALID_CON_NUM:
	case UCSI_ERROR_UNREGONIZED_CMD:
	case UCSI_ERROR_INVALID_CMD_ARGUMENT:
		dev_err(ucsi->dev, "possible UCSI driver bug %u\n", error);
		return -EINVAL;
	case UCSI_ERROR_OVERCURRENT:
		dev_warn(ucsi->dev, "Overcurrent condition\n");
		break;
	case UCSI_ERROR_PARTNER_REJECTED_SWAP:
		dev_warn(ucsi->dev, "Partner rejected swap\n");
		break;
	case UCSI_ERROR_HARD_RESET:
		dev_warn(ucsi->dev, "Hard reset occurred\n");
		break;
	case UCSI_ERROR_PPM_POLICY_CONFLICT:
		dev_warn(ucsi->dev, "PPM Policy conflict\n");
		break;
	case UCSI_ERROR_SWAP_REJECTED:
		dev_warn(ucsi->dev, "Swap rejected\n");
		break;
	case UCSI_ERROR_UNDEFINED:
	default:
		dev_err(ucsi->dev, "unknown error %u\n", error);
		break;
	}

	return -EIO;
}

static int ucsi_exec_command(struct ucsi *ucsi, u64 cmd)
{
	u32 cci;
	int ret;

	ret = ucsi->ops->sync_write(ucsi, UCSI_CONTROL, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	ret = ucsi->ops->read(ucsi, UCSI_CCI, &cci, sizeof(cci));
	if (ret)
		return ret;

	if (cci & UCSI_CCI_BUSY)
		return -EBUSY;

	if (!(cci & UCSI_CCI_COMMAND_COMPLETE))
		return -EIO;

	if (cci & UCSI_CCI_NOT_SUPPORTED)
		return -EOPNOTSUPP;

	if (cci & UCSI_CCI_ERROR) {
		if (cmd == UCSI_GET_ERROR_STATUS)
			return -EIO;
		return ucsi_read_error(ucsi);
	}

	return UCSI_CCI_LENGTH(cci);
}

int ucsi_send_command(struct ucsi *ucsi, u64 command,
		      void *data, size_t size)
{
	u8 length;
	int ret;

	mutex_lock(&ucsi->ppm_lock);

	ret = ucsi_exec_command(ucsi, command);
	if (ret < 0)
		goto out;

	length = ret;

	if (data) {
		ret = ucsi->ops->read(ucsi, UCSI_MESSAGE_IN, data, size);
		if (ret)
			goto out;
	}

	ret = ucsi_acknowledge_command(ucsi);
	if (ret)
		goto out;

	ret = length;
out:
	mutex_unlock(&ucsi->ppm_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(ucsi_send_command);

int ucsi_resume(struct ucsi *ucsi)
{
	u64 command;

	/* Restore UCSI notification enable mask after system resume */
	command = UCSI_SET_NOTIFICATION_ENABLE | ucsi->ntfy;

	return ucsi_send_command(ucsi, command, NULL, 0);
}
EXPORT_SYMBOL_GPL(ucsi_resume);
/* -------------------------------------------------------------------------- */

void ucsi_altmode_update_active(struct ucsi_connector *con)
{
	const struct typec_altmode *altmode = NULL;
	u64 command;
	int ret;
	u8 cur;
	int i;

	command = UCSI_GET_CURRENT_CAM | UCSI_CONNECTOR_NUMBER(con->num);
	ret = ucsi_send_command(con->ucsi, command, &cur, sizeof(cur));
	if (ret < 0) {
		if (con->ucsi->version > 0x0100) {
			dev_err(con->ucsi->dev,
				"GET_CURRENT_CAM command failed\n");
			return;
		}
		cur = 0xff;
	}

	if (cur < UCSI_MAX_ALTMODES)
		altmode = typec_altmode_get_partner(con->port_altmode[cur]);

	for (i = 0; con->partner_altmode[i]; i++)
		typec_altmode_update_active(con->partner_altmode[i],
					    con->partner_altmode[i] == altmode);
}

static int ucsi_altmode_next_mode(struct typec_altmode **alt, u16 svid)
{
	u8 mode = 1;
	int i;

	for (i = 0; alt[i]; i++) {
		if (i > MODE_DISCOVERY_MAX)
			return -ERANGE;

		if (alt[i]->svid == svid)
			mode++;
	}

	return mode;
}

static int ucsi_next_altmode(struct typec_altmode **alt)
{
	int i = 0;

	for (i = 0; i < UCSI_MAX_ALTMODES; i++)
		if (!alt[i])
			return i;

	return -ENOENT;
}

static int ucsi_register_altmode(struct ucsi_connector *con,
				 struct typec_altmode_desc *desc,
				 u8 recipient)
{
	struct typec_altmode *alt;
	bool override;
	int ret;
	int i;

	override = !!(con->ucsi->cap.features & UCSI_CAP_ALT_MODE_OVERRIDE);

	switch (recipient) {
	case UCSI_RECIPIENT_CON:
		i = ucsi_next_altmode(con->port_altmode);
		if (i < 0) {
			ret = i;
			goto err;
		}

		ret = ucsi_altmode_next_mode(con->port_altmode, desc->svid);
		if (ret < 0)
			return ret;

		desc->mode = ret;

		switch (desc->svid) {
		case USB_TYPEC_DP_SID:
			alt = ucsi_register_displayport(con, override, i, desc);
			break;
		case USB_TYPEC_NVIDIA_VLINK_SID:
			if (desc->vdo == USB_TYPEC_NVIDIA_VLINK_DBG_VDO)
				alt = typec_port_register_altmode(con->port,
								  desc);
			else
				alt = ucsi_register_displayport(con, override,
								i, desc);
			break;
		default:
			alt = typec_port_register_altmode(con->port, desc);
			break;
		}

		if (IS_ERR(alt)) {
			ret = PTR_ERR(alt);
			goto err;
		}

		con->port_altmode[i] = alt;
		break;
	case UCSI_RECIPIENT_SOP:
		i = ucsi_next_altmode(con->partner_altmode);
		if (i < 0) {
			ret = i;
			goto err;
		}

		ret = ucsi_altmode_next_mode(con->partner_altmode, desc->svid);
		if (ret < 0)
			return ret;

		desc->mode = ret;

		alt = typec_partner_register_altmode(con->partner, desc);
		if (IS_ERR(alt)) {
			ret = PTR_ERR(alt);
			goto err;
		}

		con->partner_altmode[i] = alt;
		break;
	default:
		return -EINVAL;
	}

	trace_ucsi_register_altmode(recipient, alt);

	return 0;

err:
	dev_err(con->ucsi->dev, "failed to registers svid 0x%04x mode %d\n",
		desc->svid, desc->mode);

	return ret;
}

static int
ucsi_register_altmodes_nvidia(struct ucsi_connector *con, u8 recipient)
{
	int max_altmodes = UCSI_MAX_ALTMODES;
	struct typec_altmode_desc desc;
	struct ucsi_altmode alt;
	struct ucsi_altmode orig[UCSI_MAX_ALTMODES];
	struct ucsi_altmode updated[UCSI_MAX_ALTMODES];
	struct ucsi *ucsi = con->ucsi;
	bool multi_dp = false;
	u64 command;
	int ret;
	int len;
	int i;
	int k = 0;

	if (recipient == UCSI_RECIPIENT_CON)
		max_altmodes = con->ucsi->cap.num_alt_modes;

	memset(orig, 0, sizeof(orig));
	memset(updated, 0, sizeof(updated));

	/* First get all the alternate modes */
	for (i = 0; i < max_altmodes; i++) {
		memset(&alt, 0, sizeof(alt));
		command = UCSI_GET_ALTERNATE_MODES;
		command |= UCSI_GET_ALTMODE_RECIPIENT(recipient);
		command |= UCSI_GET_ALTMODE_CONNECTOR_NUMBER(con->num);
		command |= UCSI_GET_ALTMODE_OFFSET(i);
		len = ucsi_send_command(con->ucsi, command, &alt, sizeof(alt));
		/*
		 * We are collecting all altmodes first and then registering.
		 * Some type-C device will return zero length data beyond last
		 * alternate modes. We should not return if length is zero.
		 */
		if (len < 0)
			return len;

		/* We got all altmodes, now break out and register them */
		if (!len || !alt.svid)
			break;

		orig[k].mid = alt.mid;
		orig[k].svid = alt.svid;
		k++;
	}
	/*
	 * Update the original altmode table as some ppms may report
	 * multiple DP altmodes.
	 */
	if (recipient == UCSI_RECIPIENT_CON)
		multi_dp = ucsi->ops->update_altmodes(ucsi, orig, updated);

	/* now register altmodes */
	for (i = 0; i < max_altmodes; i++) {
		memset(&desc, 0, sizeof(desc));
		if (multi_dp && recipient == UCSI_RECIPIENT_CON) {
			desc.svid = updated[i].svid;
			desc.vdo = updated[i].mid;
		} else {
			desc.svid = orig[i].svid;
			desc.vdo = orig[i].mid;
		}
		desc.roles = TYPEC_PORT_DRD;

		if (!desc.svid)
			return 0;

		ret = ucsi_register_altmode(con, &desc, recipient);
		if (ret)
			return ret;
	}

	return 0;
}

static int ucsi_register_altmodes(struct ucsi_connector *con, u8 recipient)
{
	int max_altmodes = UCSI_MAX_ALTMODES;
	struct typec_altmode_desc desc;
	struct ucsi_altmode alt[2];
	u64 command;
	int num;
	int ret;
	int len;
	int j;
	int i;

	if (!(con->ucsi->cap.features & UCSI_CAP_ALT_MODE_DETAILS))
		return 0;

	if (recipient == UCSI_RECIPIENT_SOP && con->partner_altmode[0])
		return 0;

	if (con->ucsi->ops->update_altmodes)
		return ucsi_register_altmodes_nvidia(con, recipient);

	if (recipient == UCSI_RECIPIENT_CON)
		max_altmodes = con->ucsi->cap.num_alt_modes;

	for (i = 0; i < max_altmodes;) {
		memset(alt, 0, sizeof(alt));
		command = UCSI_GET_ALTERNATE_MODES;
		command |= UCSI_GET_ALTMODE_RECIPIENT(recipient);
		command |= UCSI_GET_ALTMODE_CONNECTOR_NUMBER(con->num);
		command |= UCSI_GET_ALTMODE_OFFSET(i);
		len = ucsi_send_command(con->ucsi, command, alt, sizeof(alt));
		if (len <= 0)
			return len;

		/*
		 * This code is requesting one alt mode at a time, but some PPMs
		 * may still return two. If that happens both alt modes need be
		 * registered and the offset for the next alt mode has to be
		 * incremented.
		 */
		num = len / sizeof(alt[0]);
		i += num;

		for (j = 0; j < num; j++) {
			if (!alt[j].svid)
				return 0;

			memset(&desc, 0, sizeof(desc));
			desc.vdo = alt[j].mid;
			desc.svid = alt[j].svid;
			desc.roles = TYPEC_PORT_DRD;

			ret = ucsi_register_altmode(con, &desc, recipient);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static void ucsi_unregister_altmodes(struct ucsi_connector *con, u8 recipient)
{
	const struct typec_altmode *pdev;
	struct typec_altmode **adev;
	int i = 0;

	switch (recipient) {
	case UCSI_RECIPIENT_CON:
		adev = con->port_altmode;
		break;
	case UCSI_RECIPIENT_SOP:
		adev = con->partner_altmode;
		break;
	default:
		return;
	}

	while (adev[i]) {
		if (recipient == UCSI_RECIPIENT_SOP &&
		    (adev[i]->svid == USB_TYPEC_DP_SID ||
			(adev[i]->svid == USB_TYPEC_NVIDIA_VLINK_SID &&
			adev[i]->vdo != USB_TYPEC_NVIDIA_VLINK_DBG_VDO))) {
			pdev = typec_altmode_get_partner(adev[i]);
			ucsi_displayport_remove_partner((void *)pdev);
		}
		typec_unregister_altmode(adev[i]);
		adev[i++] = NULL;
	}
}

static int ucsi_get_pdos(struct ucsi_connector *con, int is_partner,
			 u32 *pdos, int offset, int num_pdos)
{
	struct ucsi *ucsi = con->ucsi;
	u64 command;
	int ret;

	command = UCSI_COMMAND(UCSI_GET_PDOS) | UCSI_CONNECTOR_NUMBER(con->num);
	command |= UCSI_GET_PDOS_PARTNER_PDO(is_partner);
	command |= UCSI_GET_PDOS_PDO_OFFSET(offset);
	command |= UCSI_GET_PDOS_NUM_PDOS(num_pdos - 1);
	command |= UCSI_GET_PDOS_SRC_PDOS;
	ret = ucsi_send_command(ucsi, command, pdos + offset,
				num_pdos * sizeof(u32));
	if (ret < 0)
		dev_err(ucsi->dev, "UCSI_GET_PDOS failed (%d)\n", ret);
	if (ret == 0 && offset == 0)
		dev_warn(ucsi->dev, "UCSI_GET_PDOS returned 0 bytes\n");

	return ret;
}

static void ucsi_get_src_pdos(struct ucsi_connector *con, int is_partner)
{
	int ret;

	/* UCSI max payload means only getting at most 4 PDOs at a time */
	ret = ucsi_get_pdos(con, 1, con->src_pdos, 0, UCSI_MAX_PDOS);
	if (ret < 0)
		return;

	con->num_pdos = ret / sizeof(u32); /* number of bytes to 32-bit PDOs */
	if (con->num_pdos < UCSI_MAX_PDOS)
		return;

	/* get the remaining PDOs, if any */
	ret = ucsi_get_pdos(con, 1, con->src_pdos, UCSI_MAX_PDOS,
			    PDO_MAX_OBJECTS - UCSI_MAX_PDOS);
	if (ret < 0)
		return;

	con->num_pdos += ret / sizeof(u32);
}

static void ucsi_pwr_opmode_change(struct ucsi_connector *con)
{
	switch (UCSI_CONSTAT_PWR_OPMODE(con->status.flags)) {
	case UCSI_CONSTAT_PWR_OPMODE_PD:
		con->rdo = con->status.request_data_obj;
		typec_set_pwr_opmode(con->port, TYPEC_PWR_MODE_PD);
		ucsi_get_src_pdos(con, 1);
		break;
	case UCSI_CONSTAT_PWR_OPMODE_TYPEC1_5:
		con->rdo = 0;
		typec_set_pwr_opmode(con->port, TYPEC_PWR_MODE_1_5A);
		break;
	case UCSI_CONSTAT_PWR_OPMODE_TYPEC3_0:
		con->rdo = 0;
		typec_set_pwr_opmode(con->port, TYPEC_PWR_MODE_3_0A);
		break;
	default:
		con->rdo = 0;
		typec_set_pwr_opmode(con->port, TYPEC_PWR_MODE_USB);
		break;
	}
}

static int ucsi_register_partner(struct ucsi_connector *con)
{
	u8 pwr_opmode = UCSI_CONSTAT_PWR_OPMODE(con->status.flags);
	struct typec_partner_desc desc;
	struct typec_partner *partner;

	if (con->partner)
		return 0;

	memset(&desc, 0, sizeof(desc));

	switch (UCSI_CONSTAT_PARTNER_TYPE(con->status.flags)) {
	case UCSI_CONSTAT_PARTNER_TYPE_DEBUG:
		desc.accessory = TYPEC_ACCESSORY_DEBUG;
		break;
	case UCSI_CONSTAT_PARTNER_TYPE_AUDIO:
		desc.accessory = TYPEC_ACCESSORY_AUDIO;
		break;
	default:
		break;
	}

	desc.usb_pd = pwr_opmode == UCSI_CONSTAT_PWR_OPMODE_PD;

	partner = typec_register_partner(con->port, &desc);
	if (IS_ERR(partner)) {
		dev_err(con->ucsi->dev,
			"con%d: failed to register partner (%ld)\n", con->num,
			PTR_ERR(partner));
		return PTR_ERR(partner);
	}

	con->partner = partner;

	return 0;
}

static void ucsi_unregister_partner(struct ucsi_connector *con)
{
	if (!con->partner)
		return;

	ucsi_unregister_altmodes(con, UCSI_RECIPIENT_SOP);
	typec_unregister_partner(con->partner);
	con->partner = NULL;
}

static void ucsi_partner_change(struct ucsi_connector *con)
{
	enum usb_role u_role = USB_ROLE_NONE;
	int ret;

	if (!con->partner)
		return;

	switch (UCSI_CONSTAT_PARTNER_TYPE(con->status.flags)) {
	case UCSI_CONSTAT_PARTNER_TYPE_UFP:
	case UCSI_CONSTAT_PARTNER_TYPE_CABLE_AND_UFP:
		u_role = USB_ROLE_HOST;
		fallthrough;
	case UCSI_CONSTAT_PARTNER_TYPE_CABLE:
		typec_set_data_role(con->port, TYPEC_HOST);
		break;
	case UCSI_CONSTAT_PARTNER_TYPE_DFP:
		u_role = USB_ROLE_DEVICE;
		typec_set_data_role(con->port, TYPEC_DEVICE);
		break;
	default:
		break;
	}

	/* Complete pending data role swap */
	if (!completion_done(&con->complete))
		complete(&con->complete);

	/* Only notify USB controller if partner supports USB data */
	if (!(UCSI_CONSTAT_PARTNER_FLAGS(con->status.flags) & UCSI_CONSTAT_PARTNER_FLAG_USB))
		u_role = USB_ROLE_NONE;

	ret = usb_role_switch_set_role(con->usb_role_sw, u_role);
	if (ret)
		dev_err(con->ucsi->dev, "con:%d: failed to set usb role:%d\n",
			con->num, u_role);

	/* Can't rely on Partner Flags field. Always checking the alt modes. */
	ret = ucsi_register_altmodes(con, UCSI_RECIPIENT_SOP);
	if (ret)
		dev_err(con->ucsi->dev,
			"con%d: failed to register partner alternate modes\n",
			con->num);
	else
		ucsi_altmode_update_active(con);
}

static void ucsi_handle_connector_change(struct work_struct *work)
{
	struct ucsi_connector *con = container_of(work, struct ucsi_connector,
						  work);
	struct ucsi *ucsi = con->ucsi;
	struct ucsi_connector_status pre_ack_status;
	struct ucsi_connector_status post_ack_status;
	enum typec_role role;
	enum usb_role u_role = USB_ROLE_NONE;
	u16 inferred_changes;
	u16 changed_flags;
	u64 command;
	int ret;

	mutex_lock(&con->lock);

	/*
	 * Some/many PPMs have an issue where all fields in the change bitfield
	 * are cleared when an ACK is send. This will causes any change
	 * between GET_CONNECTOR_STATUS and ACK to be lost.
	 *
	 * We work around this by re-fetching the connector status afterwards.
	 * We then infer any changes that we see have happened but that may not
	 * be represented in the change bitfield.
	 *
	 * Also, even though we don't need to know the currently supported alt
	 * modes, we run the GET_CAM_SUPPORTED command to ensure the PPM does
	 * not get stuck in case it assumes we do.
	 * Always do this, rather than relying on UCSI_CONSTAT_CAM_CHANGE to be
	 * set in the change bitfield.
	 *
	 * We end up with the following actions:
	 *  1. UCSI_GET_CONNECTOR_STATUS, store result, update unprocessed_changes
	 *  2. UCSI_GET_CAM_SUPPORTED, discard result
	 *  3. ACK connector change
	 *  4. UCSI_GET_CONNECTOR_STATUS, store result
	 *  5. Infere lost changes by comparing UCSI_GET_CONNECTOR_STATUS results
	 *  6. If PPM reported a new change, then restart in order to ACK
	 *  7. Process everything as usual.
	 *
	 * We may end up seeing a change twice, but we can only miss extremely
	 * short transitional changes.
	 */

	/* 1. First UCSI_GET_CONNECTOR_STATUS */
	command = UCSI_GET_CONNECTOR_STATUS | UCSI_CONNECTOR_NUMBER(con->num);
	ret = ucsi_send_command(ucsi, command, &pre_ack_status,
				sizeof(pre_ack_status));
	if (ret < 0) {
		dev_err(ucsi->dev, "%s: GET_CONNECTOR_STATUS failed (%d)\n",
			__func__, ret);
		goto out_unlock;
	}
	con->unprocessed_changes |= pre_ack_status.change;

	/* 2. Run UCSI_GET_CAM_SUPPORTED and discard the result. */
	command = UCSI_GET_CAM_SUPPORTED;
	command |= UCSI_CONNECTOR_NUMBER(con->num);
	ucsi_send_command(con->ucsi, command, NULL, 0);

	/* 3. ACK connector change */
	ret = ucsi_acknowledge_connector_change(ucsi);
	clear_bit(EVENT_PENDING, &ucsi->flags);
	if (ret) {
		dev_err(ucsi->dev, "%s: ACK failed (%d)", __func__, ret);
		goto out_unlock;
	}

	/* 4. Second UCSI_GET_CONNECTOR_STATUS */
	command = UCSI_GET_CONNECTOR_STATUS | UCSI_CONNECTOR_NUMBER(con->num);
	ret = ucsi_send_command(ucsi, command, &post_ack_status,
				sizeof(post_ack_status));
	if (ret < 0) {
		dev_err(ucsi->dev, "%s: GET_CONNECTOR_STATUS failed (%d)\n",
			__func__, ret);
		goto out_unlock;
	}

	/* 5. Inferre any missing changes */
	changed_flags = pre_ack_status.flags ^ post_ack_status.flags;
	inferred_changes = 0;
	if (UCSI_CONSTAT_PWR_OPMODE(changed_flags) != 0)
		inferred_changes |= UCSI_CONSTAT_POWER_OPMODE_CHANGE;

	if (changed_flags & UCSI_CONSTAT_CONNECTED)
		inferred_changes |= UCSI_CONSTAT_CONNECT_CHANGE;

	if (changed_flags & UCSI_CONSTAT_PWR_DIR)
		inferred_changes |= UCSI_CONSTAT_POWER_DIR_CHANGE;

	if (UCSI_CONSTAT_PARTNER_FLAGS(changed_flags) != 0)
		inferred_changes |= UCSI_CONSTAT_PARTNER_CHANGE;

	if (UCSI_CONSTAT_PARTNER_TYPE(changed_flags) != 0)
		inferred_changes |= UCSI_CONSTAT_PARTNER_CHANGE;

	/* Mask out anything that was correctly notified in the later call. */
	inferred_changes &= ~post_ack_status.change;
	if (inferred_changes)
		dev_dbg(ucsi->dev, "%s: Inferred changes that would have been lost: 0x%04x\n",
			__func__, inferred_changes);

	con->unprocessed_changes |= inferred_changes;

	/* 6. If PPM reported a new change, then restart in order to ACK */
	if (post_ack_status.change)
		goto out_unlock;

	/* 7. Continue as if nothing happened */
	con->status = post_ack_status;
	con->status.change = con->unprocessed_changes;
	con->unprocessed_changes = 0;

	role = !!(con->status.flags & UCSI_CONSTAT_PWR_DIR);

	if (con->status.change & UCSI_CONSTAT_POWER_OPMODE_CHANGE ||
	    con->status.change & UCSI_CONSTAT_POWER_LEVEL_CHANGE) {
		ucsi_pwr_opmode_change(con);
		ucsi_port_psy_changed(con);
	}

	if (con->status.change & UCSI_CONSTAT_POWER_DIR_CHANGE) {
		typec_set_pwr_role(con->port, role);

		/* Complete pending power role swap */
		if (!completion_done(&con->complete))
			complete(&con->complete);
	}

	if (con->status.change & UCSI_CONSTAT_CONNECT_CHANGE) {
		typec_set_pwr_role(con->port, role);

		switch (UCSI_CONSTAT_PARTNER_TYPE(con->status.flags)) {
		case UCSI_CONSTAT_PARTNER_TYPE_UFP:
		case UCSI_CONSTAT_PARTNER_TYPE_CABLE_AND_UFP:
			u_role = USB_ROLE_HOST;
			fallthrough;
		case UCSI_CONSTAT_PARTNER_TYPE_CABLE:
			typec_set_data_role(con->port, TYPEC_HOST);
			break;
		case UCSI_CONSTAT_PARTNER_TYPE_DFP:
			u_role = USB_ROLE_DEVICE;
			typec_set_data_role(con->port, TYPEC_DEVICE);
			break;
		default:
			break;
		}

		if (con->status.flags & UCSI_CONSTAT_CONNECTED)
			ucsi_register_partner(con);
		else
			ucsi_unregister_partner(con);

		ucsi_port_psy_changed(con);

		/* Only notify USB controller if partner supports USB data */
		if (!(UCSI_CONSTAT_PARTNER_FLAGS(con->status.flags) &
				UCSI_CONSTAT_PARTNER_FLAG_USB))
			u_role = USB_ROLE_NONE;

		ret = usb_role_switch_set_role(con->usb_role_sw, u_role);
		if (ret)
			dev_err(ucsi->dev, "con:%d: failed to set usb role:%d\n",
				con->num, u_role);
	}

	if (con->status.change & UCSI_CONSTAT_PARTNER_CHANGE)
		ucsi_partner_change(con);

	trace_ucsi_connector_change(con->num, &con->status);

out_unlock:
	if (test_and_clear_bit(EVENT_PENDING, &ucsi->flags)) {
		schedule_work(&con->work);
		mutex_unlock(&con->lock);
		return;
	}

	clear_bit(EVENT_PROCESSING, &ucsi->flags);
	mutex_unlock(&con->lock);
}

/**
 * ucsi_connector_change - Process Connector Change Event
 * @ucsi: UCSI Interface
 * @num: Connector number
 */
void ucsi_connector_change(struct ucsi *ucsi, u8 num)
{
	struct ucsi_connector *con = &ucsi->connector[num - 1];

	if (!(ucsi->ntfy & UCSI_ENABLE_NTFY_CONNECTOR_CHANGE)) {
		dev_dbg(ucsi->dev, "Bogus connector change event\n");
		return;
	}

	set_bit(EVENT_PENDING, &ucsi->flags);

	if (!test_and_set_bit(EVENT_PROCESSING, &ucsi->flags))
		schedule_work(&con->work);
}
EXPORT_SYMBOL_GPL(ucsi_connector_change);

/* -------------------------------------------------------------------------- */

static int ucsi_reset_connector(struct ucsi_connector *con, bool hard)
{
	u64 command;

	command = UCSI_CONNECTOR_RESET | UCSI_CONNECTOR_NUMBER(con->num);
	command |= hard ? UCSI_CONNECTOR_RESET_HARD : 0;

	return ucsi_send_command(con->ucsi, command, NULL, 0);
}

static int ucsi_reset_ppm(struct ucsi *ucsi)
{
	u64 command = UCSI_PPM_RESET;
	unsigned long tmo;
	u32 cci;
	int ret;

	mutex_lock(&ucsi->ppm_lock);

	ret = ucsi->ops->async_write(ucsi, UCSI_CONTROL, &command,
				     sizeof(command));
	if (ret < 0)
		goto out;

	tmo = jiffies + msecs_to_jiffies(UCSI_TIMEOUT_MS);

	do {
		if (time_is_before_jiffies(tmo)) {
			ret = -ETIMEDOUT;
			goto out;
		}

		ret = ucsi->ops->read(ucsi, UCSI_CCI, &cci, sizeof(cci));
		if (ret)
			goto out;

		/* If the PPM is still doing something else, reset it again. */
		if (cci & ~UCSI_CCI_RESET_COMPLETE) {
			ret = ucsi->ops->async_write(ucsi, UCSI_CONTROL,
						     &command,
						     sizeof(command));
			if (ret < 0)
				goto out;
		}

		msleep(20);
	} while (!(cci & UCSI_CCI_RESET_COMPLETE));

out:
	mutex_unlock(&ucsi->ppm_lock);
	return ret;
}

static int ucsi_role_cmd(struct ucsi_connector *con, u64 command)
{
	int ret;

	ret = ucsi_send_command(con->ucsi, command, NULL, 0);
	if (ret == -ETIMEDOUT) {
		u64 c;

		/* PPM most likely stopped responding. Resetting everything. */
		ucsi_reset_ppm(con->ucsi);

		c = UCSI_SET_NOTIFICATION_ENABLE | con->ucsi->ntfy;
		ucsi_send_command(con->ucsi, c, NULL, 0);

		ucsi_reset_connector(con, true);
	}

	return ret;
}

static int ucsi_dr_swap(struct typec_port *port, enum typec_data_role role)
{
	struct ucsi_connector *con = typec_get_drvdata(port);
	u8 partner_type;
	u64 command;
	int ret = 0;

	mutex_lock(&con->lock);

	if (!con->partner) {
		ret = -ENOTCONN;
		goto out_unlock;
	}

	partner_type = UCSI_CONSTAT_PARTNER_TYPE(con->status.flags);
	if ((partner_type == UCSI_CONSTAT_PARTNER_TYPE_DFP &&
	     role == TYPEC_DEVICE) ||
	    (partner_type == UCSI_CONSTAT_PARTNER_TYPE_UFP &&
	     role == TYPEC_HOST))
		goto out_unlock;

	reinit_completion(&con->complete);

	command = UCSI_SET_UOR | UCSI_CONNECTOR_NUMBER(con->num);
	command |= UCSI_SET_UOR_ROLE(role);
	command |= UCSI_SET_UOR_ACCEPT_ROLE_SWAPS;
	ret = ucsi_role_cmd(con, command);
	if (ret < 0)
		goto out_unlock;

	mutex_unlock(&con->lock);

	if (!wait_for_completion_timeout(&con->complete,
					 msecs_to_jiffies(UCSI_SWAP_TIMEOUT_MS)))
		return -ETIMEDOUT;

	return 0;

out_unlock:
	mutex_unlock(&con->lock);

	return ret;
}

static int ucsi_pr_swap(struct typec_port *port, enum typec_role role)
{
	struct ucsi_connector *con = typec_get_drvdata(port);
	enum typec_role cur_role;
	u64 command;
	int ret = 0;

	mutex_lock(&con->lock);

	if (!con->partner) {
		ret = -ENOTCONN;
		goto out_unlock;
	}

	cur_role = !!(con->status.flags & UCSI_CONSTAT_PWR_DIR);

	if (cur_role == role)
		goto out_unlock;

	reinit_completion(&con->complete);

	command = UCSI_SET_PDR | UCSI_CONNECTOR_NUMBER(con->num);
	command |= UCSI_SET_PDR_ROLE(role);
	command |= UCSI_SET_PDR_ACCEPT_ROLE_SWAPS;
	ret = ucsi_role_cmd(con, command);
	if (ret < 0)
		goto out_unlock;

	mutex_unlock(&con->lock);

	if (!wait_for_completion_timeout(&con->complete,
					 msecs_to_jiffies(UCSI_SWAP_TIMEOUT_MS)))
		return -ETIMEDOUT;

	mutex_lock(&con->lock);

	/* Something has gone wrong while swapping the role */
	if (UCSI_CONSTAT_PWR_OPMODE(con->status.flags) !=
	    UCSI_CONSTAT_PWR_OPMODE_PD) {
		ucsi_reset_connector(con, true);
		ret = -EPROTO;
	}

out_unlock:
	mutex_unlock(&con->lock);

	return ret;
}

static const struct typec_operations ucsi_ops = {
	.dr_set = ucsi_dr_swap,
	.pr_set = ucsi_pr_swap
};

/* Caller must call fwnode_handle_put() after use */
static struct fwnode_handle *ucsi_find_fwnode(struct ucsi_connector *con)
{
	struct fwnode_handle *fwnode;
	int i = 1;

	device_for_each_child_node(con->ucsi->dev, fwnode)
		if (i++ == con->num)
			return fwnode;
	return NULL;
}

static int ucsi_register_port(struct ucsi *ucsi, int index)
{
	struct ucsi_connector *con = &ucsi->connector[index];
	struct typec_capability *cap = &con->typec_cap;
	enum typec_accessory *accessory = cap->accessory;
	enum usb_role u_role = USB_ROLE_NONE;
	u64 command;
	int ret;

	INIT_WORK(&con->work, ucsi_handle_connector_change);
	init_completion(&con->complete);
	mutex_init(&con->lock);
	con->num = index + 1;
	con->ucsi = ucsi;

	/* Delay other interactions with the con until registration is complete */
	mutex_lock(&con->lock);

	/* Get connector capability */
	command = UCSI_GET_CONNECTOR_CAPABILITY;
	command |= UCSI_CONNECTOR_NUMBER(con->num);
	ret = ucsi_send_command(ucsi, command, &con->cap, sizeof(con->cap));
	if (ret < 0)
		goto out_unlock;

	if (con->cap.op_mode & UCSI_CONCAP_OPMODE_DRP)
		cap->data = TYPEC_PORT_DRD;
	else if (con->cap.op_mode & UCSI_CONCAP_OPMODE_DFP)
		cap->data = TYPEC_PORT_DFP;
	else if (con->cap.op_mode & UCSI_CONCAP_OPMODE_UFP)
		cap->data = TYPEC_PORT_UFP;

	if ((con->cap.flags & UCSI_CONCAP_FLAG_PROVIDER) &&
	    (con->cap.flags & UCSI_CONCAP_FLAG_CONSUMER))
		cap->type = TYPEC_PORT_DRP;
	else if (con->cap.flags & UCSI_CONCAP_FLAG_PROVIDER)
		cap->type = TYPEC_PORT_SRC;
	else if (con->cap.flags & UCSI_CONCAP_FLAG_CONSUMER)
		cap->type = TYPEC_PORT_SNK;

	cap->revision = ucsi->cap.typec_version;
	cap->pd_revision = ucsi->cap.pd_version;
	cap->svdm_version = SVDM_VER_2_0;
	cap->prefer_role = TYPEC_NO_PREFERRED_ROLE;

	if (con->cap.op_mode & UCSI_CONCAP_OPMODE_AUDIO_ACCESSORY)
		*accessory++ = TYPEC_ACCESSORY_AUDIO;
	if (con->cap.op_mode & UCSI_CONCAP_OPMODE_DEBUG_ACCESSORY)
		*accessory = TYPEC_ACCESSORY_DEBUG;

	cap->fwnode = ucsi_find_fwnode(con);
	cap->driver_data = con;
	cap->ops = &ucsi_ops;

	ret = ucsi_register_port_psy(con);
	if (ret)
		goto out;

	/* Register the connector */
	con->port = typec_register_port(ucsi->dev, cap);
	if (IS_ERR(con->port)) {
		ret = PTR_ERR(con->port);
		goto out;
	}

	/* Alternate modes */
	ret = ucsi_register_altmodes(con, UCSI_RECIPIENT_CON);
	if (ret) {
		dev_err(ucsi->dev, "con%d: failed to register alt modes\n",
			con->num);
		goto out;
	}

	/* Get the status */
	command = UCSI_GET_CONNECTOR_STATUS | UCSI_CONNECTOR_NUMBER(con->num);
	ret = ucsi_send_command(ucsi, command, &con->status, sizeof(con->status));
	if (ret < 0) {
		dev_err(ucsi->dev, "con%d: failed to get status\n", con->num);
		ret = 0;
		goto out;
	}
	ret = 0; /* ucsi_send_command() returns length on success */

	switch (UCSI_CONSTAT_PARTNER_TYPE(con->status.flags)) {
	case UCSI_CONSTAT_PARTNER_TYPE_UFP:
	case UCSI_CONSTAT_PARTNER_TYPE_CABLE_AND_UFP:
		u_role = USB_ROLE_HOST;
		fallthrough;
	case UCSI_CONSTAT_PARTNER_TYPE_CABLE:
		typec_set_data_role(con->port, TYPEC_HOST);
		break;
	case UCSI_CONSTAT_PARTNER_TYPE_DFP:
		u_role = USB_ROLE_DEVICE;
		typec_set_data_role(con->port, TYPEC_DEVICE);
		break;
	default:
		break;
	}

	/* Check if there is already something connected */
	if (con->status.flags & UCSI_CONSTAT_CONNECTED) {
		typec_set_pwr_role(con->port,
				  !!(con->status.flags & UCSI_CONSTAT_PWR_DIR));
		ucsi_pwr_opmode_change(con);
		ucsi_register_partner(con);
		ucsi_port_psy_changed(con);
	}

	con->usb_role_sw = fwnode_usb_role_switch_get(cap->fwnode);
	if (IS_ERR(con->usb_role_sw)) {
		dev_err(ucsi->dev, "con%d: failed to get usb role switch\n",
			con->num);
		con->usb_role_sw = NULL;
	}

	/* Only notify USB controller if partner supports USB data */
	if (!(UCSI_CONSTAT_PARTNER_FLAGS(con->status.flags) & UCSI_CONSTAT_PARTNER_FLAG_USB))
		u_role = USB_ROLE_NONE;

	ret = usb_role_switch_set_role(con->usb_role_sw, u_role);
	if (ret) {
		dev_err(ucsi->dev, "con:%d: failed to set usb role:%d\n",
			con->num, u_role);
		ret = 0;
	}

	if (con->partner) {
		ret = ucsi_register_altmodes(con, UCSI_RECIPIENT_SOP);
		if (ret) {
			dev_err(ucsi->dev,
				"con%d: failed to register alternate modes\n",
				con->num);
			ret = 0;
		} else {
			ucsi_altmode_update_active(con);
		}
	}

	trace_ucsi_register_port(con->num, &con->status);

out:
	fwnode_handle_put(cap->fwnode);
out_unlock:
	mutex_unlock(&con->lock);
	return ret;
}

/**
 * ucsi_init - Initialize UCSI interface
 * @ucsi: UCSI to be initialized
 *
 * Registers all ports @ucsi has and enables all notification events.
 */
static int ucsi_init(struct ucsi *ucsi)
{
	struct ucsi_connector *con;
	u64 command;
	int ret;
	int i;

	/* Reset the PPM */
	ret = ucsi_reset_ppm(ucsi);
	if (ret) {
		dev_err(ucsi->dev, "failed to reset PPM!\n");
		goto err;
	}

	/* Enable basic notifications */
	ucsi->ntfy = UCSI_ENABLE_NTFY_CMD_COMPLETE | UCSI_ENABLE_NTFY_ERROR;
	command = UCSI_SET_NOTIFICATION_ENABLE | ucsi->ntfy;
	ret = ucsi_send_command(ucsi, command, NULL, 0);
	if (ret < 0)
		goto err_reset;

	/* Get PPM capabilities */
	command = UCSI_GET_CAPABILITY;
	ret = ucsi_send_command(ucsi, command, &ucsi->cap, sizeof(ucsi->cap));
	if (ret < 0)
		goto err_reset;

	if (!ucsi->cap.num_connectors) {
		ret = -ENODEV;
		goto err_reset;
	}

	/* Allocate the connectors. Released in ucsi_unregister() */
	ucsi->connector = kcalloc(ucsi->cap.num_connectors + 1,
				  sizeof(*ucsi->connector), GFP_KERNEL);
	if (!ucsi->connector) {
		ret = -ENOMEM;
		goto err_reset;
	}

	/* Register all connectors */
	for (i = 0; i < ucsi->cap.num_connectors; i++) {
		ret = ucsi_register_port(ucsi, i);
		if (ret)
			goto err_unregister;
	}

	/* Enable all notifications */
	ucsi->ntfy = UCSI_ENABLE_NTFY_ALL;
	command = UCSI_SET_NOTIFICATION_ENABLE | ucsi->ntfy;
	ret = ucsi_send_command(ucsi, command, NULL, 0);
	if (ret < 0)
		goto err_unregister;

	return 0;

err_unregister:
	for (con = ucsi->connector; con->port; con++) {
		ucsi_unregister_partner(con);
		ucsi_unregister_altmodes(con, UCSI_RECIPIENT_CON);
		ucsi_unregister_port_psy(con);
		typec_unregister_port(con->port);
		con->port = NULL;
	}

err_reset:
	memset(&ucsi->cap, 0, sizeof(ucsi->cap));
	ucsi_reset_ppm(ucsi);
err:
	return ret;
}

static void ucsi_init_work(struct work_struct *work)
{
	struct ucsi *ucsi = container_of(work, struct ucsi, work);
	int ret;

	ret = ucsi_init(ucsi);
	if (ret)
		dev_err(ucsi->dev, "PPM init failed (%d)\n", ret);
}

/**
 * ucsi_get_drvdata - Return private driver data pointer
 * @ucsi: UCSI interface
 */
void *ucsi_get_drvdata(struct ucsi *ucsi)
{
	return ucsi->driver_data;
}
EXPORT_SYMBOL_GPL(ucsi_get_drvdata);

/**
 * ucsi_set_drvdata - Assign private driver data pointer
 * @ucsi: UCSI interface
 * @data: Private data pointer
 */
void ucsi_set_drvdata(struct ucsi *ucsi, void *data)
{
	ucsi->driver_data = data;
}
EXPORT_SYMBOL_GPL(ucsi_set_drvdata);

/**
 * ucsi_create - Allocate UCSI instance
 * @dev: Device interface to the PPM (Platform Policy Manager)
 * @ops: I/O routines
 */
struct ucsi *ucsi_create(struct device *dev, const struct ucsi_operations *ops)
{
	struct ucsi *ucsi;

	if (!ops || !ops->read || !ops->sync_write || !ops->async_write)
		return ERR_PTR(-EINVAL);

	ucsi = kzalloc(sizeof(*ucsi), GFP_KERNEL);
	if (!ucsi)
		return ERR_PTR(-ENOMEM);

	INIT_WORK(&ucsi->work, ucsi_init_work);
	mutex_init(&ucsi->ppm_lock);
	ucsi->dev = dev;
	ucsi->ops = ops;

	return ucsi;
}
EXPORT_SYMBOL_GPL(ucsi_create);

/**
 * ucsi_destroy - Free UCSI instance
 * @ucsi: UCSI instance to be freed
 */
void ucsi_destroy(struct ucsi *ucsi)
{
	kfree(ucsi);
}
EXPORT_SYMBOL_GPL(ucsi_destroy);

/**
 * ucsi_register - Register UCSI interface
 * @ucsi: UCSI instance
 */
int ucsi_register(struct ucsi *ucsi)
{
	int ret;

	ret = ucsi->ops->read(ucsi, UCSI_VERSION, &ucsi->version,
			      sizeof(ucsi->version));
	if (ret)
		return ret;

	if (!ucsi->version)
		return -ENODEV;

	queue_work(system_long_wq, &ucsi->work);

	return 0;
}
EXPORT_SYMBOL_GPL(ucsi_register);

/**
 * ucsi_unregister - Unregister UCSI interface
 * @ucsi: UCSI interface to be unregistered
 *
 * Unregister UCSI interface that was created with ucsi_register().
 */
void ucsi_unregister(struct ucsi *ucsi)
{
	u64 cmd = UCSI_SET_NOTIFICATION_ENABLE;
	int i;

	/* Make sure that we are not in the middle of driver initialization */
	cancel_work_sync(&ucsi->work);

	/* Disable notifications */
	ucsi->ops->async_write(ucsi, UCSI_CONTROL, &cmd, sizeof(cmd));

	for (i = 0; i < ucsi->cap.num_connectors; i++) {
		cancel_work_sync(&ucsi->connector[i].work);
		ucsi_unregister_partner(&ucsi->connector[i]);
		ucsi_unregister_altmodes(&ucsi->connector[i],
					 UCSI_RECIPIENT_CON);
		ucsi_unregister_port_psy(&ucsi->connector[i]);
		typec_unregister_port(ucsi->connector[i].port);
	}

	kfree(ucsi->connector);
}
EXPORT_SYMBOL_GPL(ucsi_unregister);

MODULE_AUTHOR("Heikki Krogerus <heikki.krogerus@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("USB Type-C Connector System Software Interface driver");
