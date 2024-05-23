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

static int ucsi_read_message_in(struct ucsi *ucsi, void *buf,
					  size_t buf_size)
{
	/*
	 * Below UCSI 2.0, MESSAGE_IN was limited to 16 bytes. Truncate the
	 * reads here.
	 */
	if (ucsi->version <= UCSI_VERSION_1_2)
		buf_size = clamp(buf_size, 0, 16);

	return ucsi->ops->read(ucsi, UCSI_MESSAGE_IN, buf, buf_size);
}

static int ucsi_acknowledge(struct ucsi *ucsi, bool conn_ack)
{
	u64 ctrl;

	ctrl = UCSI_ACK_CC_CI;
	ctrl |= UCSI_ACK_COMMAND_COMPLETE;
	if (conn_ack) {
		clear_bit(EVENT_PENDING, &ucsi->flags);
		ctrl |= UCSI_ACK_CONNECTOR_CHANGE;
	}

	return ucsi->ops->sync_write(ucsi, UCSI_CONTROL, &ctrl, sizeof(ctrl));
}

static int ucsi_exec_command(struct ucsi *ucsi, u64 command);

static int ucsi_read_error(struct ucsi *ucsi)
{
	u16 error;
	int ret;

	/* Acknowledge the command that failed */
	ret = ucsi_acknowledge(ucsi, false);
	if (ret)
		return ret;

	ret = ucsi_exec_command(ucsi, UCSI_GET_ERROR_STATUS);
	if (ret < 0)
		return ret;

	ret = ucsi_read_message_in(ucsi, &error, sizeof(error));
	if (ret)
		return ret;

	ret = ucsi_acknowledge(ucsi, false);
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

	if (cmd != UCSI_CANCEL && cci & UCSI_CCI_BUSY)
		return ucsi_exec_command(ucsi, UCSI_CANCEL);

	if (!(cci & UCSI_CCI_COMMAND_COMPLETE))
		return -EIO;

	if (cci & UCSI_CCI_NOT_SUPPORTED) {
		if (ucsi_acknowledge(ucsi, false) < 0)
			dev_err(ucsi->dev,
				"ACK of unsupported command failed\n");
		return -EOPNOTSUPP;
	}

	if (cci & UCSI_CCI_ERROR) {
		if (cmd == UCSI_GET_ERROR_STATUS)
			return -EIO;
		return ucsi_read_error(ucsi);
	}

	if (cmd == UCSI_CANCEL && cci & UCSI_CCI_CANCEL_COMPLETE) {
		ret = ucsi_acknowledge(ucsi, false);
		return ret ? ret : -EBUSY;
	}

	return UCSI_CCI_LENGTH(cci);
}

static int ucsi_send_command_common(struct ucsi *ucsi, u64 command,
				    void *data, size_t size, bool conn_ack)
{
	u8 length;
	int ret;

	mutex_lock(&ucsi->ppm_lock);

	ret = ucsi_exec_command(ucsi, command);
	if (ret < 0)
		goto out;

	length = ret;

	if (data) {
		ret = ucsi_read_message_in(ucsi, data, size);
		if (ret)
			goto out;
	}

	ret = ucsi_acknowledge(ucsi, conn_ack);
	if (ret)
		goto out;

	ret = length;
out:
	mutex_unlock(&ucsi->ppm_lock);
	return ret;
}

int ucsi_send_command(struct ucsi *ucsi, u64 command,
		      void *data, size_t size)
{
	return ucsi_send_command_common(ucsi, command, data, size, false);
}
EXPORT_SYMBOL_GPL(ucsi_send_command);

/* -------------------------------------------------------------------------- */

struct ucsi_work {
	struct delayed_work work;
	struct list_head node;
	unsigned long delay;
	unsigned int count;
	struct ucsi_connector *con;
	int (*cb)(struct ucsi_connector *);
};

static void ucsi_poll_worker(struct work_struct *work)
{
	struct ucsi_work *uwork = container_of(work, struct ucsi_work, work.work);
	struct ucsi_connector *con = uwork->con;
	int ret;

	mutex_lock(&con->lock);

	if (!con->partner) {
		list_del(&uwork->node);
		mutex_unlock(&con->lock);
		kfree(uwork);
		return;
	}

	ret = uwork->cb(con);

	if (uwork->count-- && (ret == -EBUSY || ret == -ETIMEDOUT)) {
		queue_delayed_work(con->wq, &uwork->work, uwork->delay);
	} else {
		list_del(&uwork->node);
		kfree(uwork);
	}

	mutex_unlock(&con->lock);
}

static int ucsi_partner_task(struct ucsi_connector *con,
			     int (*cb)(struct ucsi_connector *),
			     int retries, unsigned long delay)
{
	struct ucsi_work *uwork;

	if (!con->partner)
		return 0;

	uwork = kzalloc(sizeof(*uwork), GFP_KERNEL);
	if (!uwork)
		return -ENOMEM;

	INIT_DELAYED_WORK(&uwork->work, ucsi_poll_worker);
	uwork->count = retries;
	uwork->delay = delay;
	uwork->con = con;
	uwork->cb = cb;

	list_add_tail(&uwork->node, &con->partner_tasks);
	queue_delayed_work(con->wq, &uwork->work, delay);

	return 0;
}

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

static int ucsi_get_num_altmode(struct typec_altmode **alt)
{
	int i;

	for (i = 0; i < UCSI_MAX_ALTMODES; i++)
		if (!alt[i])
			break;

	return i;
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
	case UCSI_RECIPIENT_SOP_P:
		i = ucsi_next_altmode(con->plug_altmode);
		if (i < 0) {
			ret = i;
			goto err;
		}

		ret = ucsi_altmode_next_mode(con->plug_altmode, desc->svid);
		if (ret < 0)
			return ret;

		desc->mode = ret;

		alt = typec_plug_register_altmode(con->plug, desc);
		if (IS_ERR(alt)) {
			ret = PTR_ERR(alt);
			goto err;
		}

		con->plug_altmode[i] = alt;
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
		if (len == -EBUSY)
			continue;
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
	case UCSI_RECIPIENT_SOP_P:
		adev = con->plug_altmode;
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

static int ucsi_read_pdos(struct ucsi_connector *con,
			  enum typec_role role, int is_partner,
			  u32 *pdos, int offset, int num_pdos)
{
	struct ucsi *ucsi = con->ucsi;
	u64 command;
	int ret;

	if (is_partner &&
	    ucsi->quirks & UCSI_NO_PARTNER_PDOS &&
	    ((con->status.flags & UCSI_CONSTAT_PWR_DIR) ||
	     !is_source(role)))
		return 0;

	command = UCSI_COMMAND(UCSI_GET_PDOS) | UCSI_CONNECTOR_NUMBER(con->num);
	command |= UCSI_GET_PDOS_PARTNER_PDO(is_partner);
	command |= UCSI_GET_PDOS_PDO_OFFSET(offset);
	command |= UCSI_GET_PDOS_NUM_PDOS(num_pdos - 1);
	command |= is_source(role) ? UCSI_GET_PDOS_SRC_PDOS : 0;
	ret = ucsi_send_command(ucsi, command, pdos + offset,
				num_pdos * sizeof(u32));
	if (ret < 0 && ret != -ETIMEDOUT)
		dev_err(ucsi->dev, "UCSI_GET_PDOS failed (%d)\n", ret);

	return ret;
}

static int ucsi_get_pdos(struct ucsi_connector *con, enum typec_role role,
			 int is_partner, u32 *pdos)
{
	u8 num_pdos;
	int ret;

	/* UCSI max payload means only getting at most 4 PDOs at a time */
	ret = ucsi_read_pdos(con, role, is_partner, pdos, 0, UCSI_MAX_PDOS);
	if (ret < 0)
		return ret;

	num_pdos = ret / sizeof(u32); /* number of bytes to 32-bit PDOs */
	if (num_pdos < UCSI_MAX_PDOS)
		return num_pdos;

	/* get the remaining PDOs, if any */
	ret = ucsi_read_pdos(con, role, is_partner, pdos, UCSI_MAX_PDOS,
			     PDO_MAX_OBJECTS - UCSI_MAX_PDOS);
	if (ret < 0)
		return ret;

	return ret / sizeof(u32) + num_pdos;
}

static int ucsi_get_src_pdos(struct ucsi_connector *con)
{
	int ret;

	ret = ucsi_get_pdos(con, TYPEC_SOURCE, 1, con->src_pdos);
	if (ret < 0)
		return ret;

	con->num_pdos = ret;

	ucsi_port_psy_changed(con);

	return ret;
}

static struct usb_power_delivery_capabilities *ucsi_get_pd_caps(struct ucsi_connector *con,
								enum typec_role role,
								bool is_partner)
{
	struct usb_power_delivery_capabilities_desc pd_caps;
	int ret;

	ret = ucsi_get_pdos(con, role, is_partner, pd_caps.pdo);
	if (ret <= 0)
		return ERR_PTR(ret);

	if (ret < PDO_MAX_OBJECTS)
		pd_caps.pdo[ret] = 0;

	pd_caps.role = role;

	return usb_power_delivery_register_capabilities(is_partner ? con->partner_pd : con->pd,
							&pd_caps);
}

static int ucsi_read_identity(struct ucsi_connector *con, u8 recipient,
			      u8 offset, u8 bytes, void *resp)
{
	struct ucsi *ucsi = con->ucsi;
	u64 command;
	int ret;

	command = UCSI_COMMAND(UCSI_GET_PD_MESSAGE) |
	    UCSI_CONNECTOR_NUMBER(con->num);
	command |= UCSI_GET_PD_MESSAGE_RECIPIENT(recipient);
	command |= UCSI_GET_PD_MESSAGE_OFFSET(offset);
	command |= UCSI_GET_PD_MESSAGE_BYTES(bytes);
	command |= UCSI_GET_PD_MESSAGE_TYPE(UCSI_GET_PD_MESSAGE_TYPE_IDENTITY);

	ret = ucsi_send_command(ucsi, command, resp, bytes);
	if (ret < 0)
		dev_err(ucsi->dev, "UCSI_GET_PD_MESSAGE failed (%d)\n", ret);

	return ret;
}

static int ucsi_get_identity(struct ucsi_connector *con, u8 recipient,
			      struct usb_pd_identity *id)
{
	struct ucsi *ucsi = con->ucsi;
	struct ucsi_pd_message_disc_id resp = {};
	int ret;

	if (ucsi->version < UCSI_VERSION_2_0) {
		/*
		 * Before UCSI v2.0, MESSAGE_IN is 16 bytes which cannot fit
		 * the 28 byte identity response including the VDM header.
		 * First request the VDM header, ID Header VDO, Cert Stat VDO
		 * and Product VDO.
		 */
		ret = ucsi_read_identity(con, recipient, 0, 0x10, &resp);
		if (ret < 0)
			return ret;


		/* Then request Product Type VDO1 through Product Type VDO3. */
		ret = ucsi_read_identity(con, recipient, 0x10, 0xc,
					 &resp.vdo[0]);
		if (ret < 0)
			return ret;

	} else {
		/*
		 * In UCSI v2.0 and after, MESSAGE_IN is large enough to request
		 * the large enough to request the full Discover Identity
		 * response at once.
		 */
		ret = ucsi_read_identity(con, recipient, 0x0, 0x1c, &resp);
		if (ret < 0)
			return ret;
	}

	id->id_header = resp.id_header;
	id->cert_stat = resp.cert_stat;
	id->product = resp.product;
	id->vdo[0] = resp.vdo[0];
	id->vdo[1] = resp.vdo[1];
	id->vdo[2] = resp.vdo[2];
	return 0;
}

static int ucsi_get_partner_identity(struct ucsi_connector *con)
{
	int ret;

	ret = ucsi_get_identity(con, UCSI_RECIPIENT_SOP,
				 &con->partner_identity);
	if (ret < 0)
		return ret;

	ret = typec_partner_set_identity(con->partner);
	if (ret < 0) {
		dev_err(con->ucsi->dev, "Failed to set partner identity (%d)\n",
			ret);
	}

	return ret;
}

static int ucsi_get_cable_identity(struct ucsi_connector *con)
{
	int ret;

	ret = ucsi_get_identity(con, UCSI_RECIPIENT_SOP_P,
				 &con->cable_identity);
	if (ret < 0)
		return ret;

	ret = typec_cable_set_identity(con->cable);
	if (ret < 0) {
		dev_err(con->ucsi->dev, "Failed to set cable identity (%d)\n",
			ret);
	}

	return ret;
}

static int ucsi_check_altmodes(struct ucsi_connector *con)
{
	int ret, num_partner_am;

	ret = ucsi_register_altmodes(con, UCSI_RECIPIENT_SOP);
	if (ret && ret != -ETIMEDOUT)
		dev_err(con->ucsi->dev,
			"con%d: failed to register partner alt modes (%d)\n",
			con->num, ret);

	/* Ignoring the errors in this case. */
	if (con->partner_altmode[0]) {
		num_partner_am = ucsi_get_num_altmode(con->partner_altmode);
		if (num_partner_am > 0)
			typec_partner_set_num_altmodes(con->partner, num_partner_am);
		ucsi_altmode_update_active(con);
		return 0;
	}

	return ret;
}

static void ucsi_register_device_pdos(struct ucsi_connector *con)
{
	struct ucsi *ucsi = con->ucsi;
	struct usb_power_delivery_desc desc = { ucsi->cap.pd_version };
	struct usb_power_delivery_capabilities *pd_cap;

	if (con->pd)
		return;

	con->pd = usb_power_delivery_register(ucsi->dev, &desc);

	pd_cap = ucsi_get_pd_caps(con, TYPEC_SOURCE, false);
	if (!IS_ERR(pd_cap))
		con->port_source_caps = pd_cap;

	pd_cap = ucsi_get_pd_caps(con, TYPEC_SINK, false);
	if (!IS_ERR(pd_cap))
		con->port_sink_caps = pd_cap;

	typec_port_set_usb_power_delivery(con->port, con->pd);
}

static int ucsi_register_partner_pdos(struct ucsi_connector *con)
{
	struct usb_power_delivery_desc desc = { con->ucsi->cap.pd_version };
	struct usb_power_delivery_capabilities *cap;

	if (con->partner_pd)
		return 0;

	con->partner_pd = typec_partner_usb_power_delivery_register(con->partner, &desc);
	if (IS_ERR(con->partner_pd))
		return PTR_ERR(con->partner_pd);

	cap = ucsi_get_pd_caps(con, TYPEC_SOURCE, true);
	if (IS_ERR(cap))
	    return PTR_ERR(cap);

	con->partner_source_caps = cap;

	cap = ucsi_get_pd_caps(con, TYPEC_SINK, true);
	if (IS_ERR(cap))
	    return PTR_ERR(cap);

	con->partner_sink_caps = cap;

	return typec_partner_set_usb_power_delivery(con->partner, con->partner_pd);
}

static void ucsi_unregister_partner_pdos(struct ucsi_connector *con)
{
	usb_power_delivery_unregister_capabilities(con->partner_sink_caps);
	con->partner_sink_caps = NULL;
	usb_power_delivery_unregister_capabilities(con->partner_source_caps);
	con->partner_source_caps = NULL;
	usb_power_delivery_unregister(con->partner_pd);
	con->partner_pd = NULL;
}

static int ucsi_register_plug(struct ucsi_connector *con)
{
	struct typec_plug *plug;
	struct typec_plug_desc desc = {.index = TYPEC_PLUG_SOP_P};

	plug = typec_register_plug(con->cable, &desc);
	if (IS_ERR(plug)) {
		dev_err(con->ucsi->dev,
			"con%d: failed to register plug (%ld)\n", con->num,
			PTR_ERR(plug));
		return PTR_ERR(plug);
	}

	con->plug = plug;
	return 0;
}

static void ucsi_unregister_plug(struct ucsi_connector *con)
{
	if (!con->plug)
		return;

	ucsi_unregister_altmodes(con, UCSI_RECIPIENT_SOP_P);
	typec_unregister_plug(con->plug);
	con->plug = NULL;
}

static int ucsi_register_cable(struct ucsi_connector *con)
{
	struct typec_cable *cable;
	struct typec_cable_desc desc = {};

	switch (UCSI_CABLE_PROP_FLAG_PLUG_TYPE(con->cable_prop.flags)) {
	case UCSI_CABLE_PROPERTY_PLUG_TYPE_A:
		desc.type = USB_PLUG_TYPE_A;
		break;
	case UCSI_CABLE_PROPERTY_PLUG_TYPE_B:
		desc.type = USB_PLUG_TYPE_B;
		break;
	case UCSI_CABLE_PROPERTY_PLUG_TYPE_C:
		desc.type = USB_PLUG_TYPE_C;
		break;
	default:
		desc.type = USB_PLUG_NONE;
		break;
	}

	desc.identity = &con->cable_identity;
	desc.active = !!(UCSI_CABLE_PROP_FLAG_ACTIVE_CABLE &
			 con->cable_prop.flags);
	desc.pd_revision = UCSI_CABLE_PROP_FLAG_PD_MAJOR_REV_AS_BCD(
	    con->cable_prop.flags);

	cable = typec_register_cable(con->port, &desc);
	if (IS_ERR(cable)) {
		dev_err(con->ucsi->dev,
			"con%d: failed to register cable (%ld)\n", con->num,
			PTR_ERR(cable));
		return PTR_ERR(cable);
	}

	con->cable = cable;
	return 0;
}

static void ucsi_unregister_cable(struct ucsi_connector *con)
{
	if (!con->cable)
		return;

	ucsi_unregister_plug(con);
	typec_unregister_cable(con->cable);
	memset(&con->cable_identity, 0, sizeof(con->cable_identity));
	con->cable = NULL;
}

static void ucsi_pwr_opmode_change(struct ucsi_connector *con)
{
	switch (UCSI_CONSTAT_PWR_OPMODE(con->status.flags)) {
	case UCSI_CONSTAT_PWR_OPMODE_PD:
		con->rdo = con->status.request_data_obj;
		typec_set_pwr_opmode(con->port, TYPEC_PWR_MODE_PD);
		ucsi_partner_task(con, ucsi_get_src_pdos, 30, 0);
		ucsi_partner_task(con, ucsi_check_altmodes, 30, 0);
		ucsi_partner_task(con, ucsi_register_partner_pdos, 1, HZ);
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

	if (pwr_opmode == UCSI_CONSTAT_PWR_OPMODE_PD)
		ucsi_register_device_pdos(con);

	desc.identity = &con->partner_identity;
	desc.usb_pd = pwr_opmode == UCSI_CONSTAT_PWR_OPMODE_PD;
	desc.pd_revision = UCSI_CONCAP_FLAG_PARTNER_PD_MAJOR_REV_AS_BCD(con->cap.flags);

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

	typec_set_mode(con->port, TYPEC_STATE_SAFE);

	typec_partner_set_usb_power_delivery(con->partner, NULL);
	ucsi_unregister_partner_pdos(con);
	ucsi_unregister_altmodes(con, UCSI_RECIPIENT_SOP);
	ucsi_unregister_cable(con);
	typec_unregister_partner(con->partner);
	memset(&con->partner_identity, 0, sizeof(con->partner_identity));
	con->partner = NULL;
}

static void ucsi_partner_change(struct ucsi_connector *con)
{
	enum usb_role u_role = USB_ROLE_NONE;
	int ret;

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

	if (con->status.flags & UCSI_CONSTAT_CONNECTED) {
		switch (UCSI_CONSTAT_PARTNER_TYPE(con->status.flags)) {
		case UCSI_CONSTAT_PARTNER_TYPE_DEBUG:
			typec_set_mode(con->port, TYPEC_MODE_DEBUG);
			break;
		case UCSI_CONSTAT_PARTNER_TYPE_AUDIO:
			typec_set_mode(con->port, TYPEC_MODE_AUDIO);
			break;
		default:
			if (UCSI_CONSTAT_PARTNER_FLAGS(con->status.flags) ==
					UCSI_CONSTAT_PARTNER_FLAG_USB)
				typec_set_mode(con->port, TYPEC_STATE_USB);
		}
	}

	/* Only notify USB controller if partner supports USB data */
	if (!(UCSI_CONSTAT_PARTNER_FLAGS(con->status.flags) & UCSI_CONSTAT_PARTNER_FLAG_USB))
		u_role = USB_ROLE_NONE;

	ret = usb_role_switch_set_role(con->usb_role_sw, u_role);
	if (ret)
		dev_err(con->ucsi->dev, "con:%d: failed to set usb role:%d\n",
			con->num, u_role);
}

static int ucsi_check_connector_capability(struct ucsi_connector *con)
{
	u64 command;
	int ret;

	if (!con->partner || con->ucsi->version < UCSI_VERSION_2_0)
		return 0;

	command = UCSI_GET_CONNECTOR_CAPABILITY | UCSI_CONNECTOR_NUMBER(con->num);
	ret = ucsi_send_command(con->ucsi, command, &con->cap, sizeof(con->cap));
	if (ret < 0) {
		dev_err(con->ucsi->dev, "GET_CONNECTOR_CAPABILITY failed (%d)\n", ret);
		return ret;
	}

	typec_partner_set_pd_revision(con->partner,
		UCSI_CONCAP_FLAG_PARTNER_PD_MAJOR_REV_AS_BCD(con->cap.flags));

	return ret;
}

static int ucsi_check_connection(struct ucsi_connector *con)
{
	u8 prev_flags = con->status.flags;
	u64 command;
	int ret;

	command = UCSI_GET_CONNECTOR_STATUS | UCSI_CONNECTOR_NUMBER(con->num);
	ret = ucsi_send_command(con->ucsi, command, &con->status, sizeof(con->status));
	if (ret < 0) {
		dev_err(con->ucsi->dev, "GET_CONNECTOR_STATUS failed (%d)\n", ret);
		return ret;
	}

	if (con->status.flags == prev_flags)
		return 0;

	if (con->status.flags & UCSI_CONSTAT_CONNECTED) {
		ucsi_register_partner(con);
		ucsi_pwr_opmode_change(con);
		ucsi_partner_change(con);
	} else {
		ucsi_partner_change(con);
		ucsi_port_psy_changed(con);
		ucsi_unregister_partner(con);
	}

	return 0;
}

static int ucsi_check_cable(struct ucsi_connector *con)
{
	u64 command;
	int ret;

	if (con->cable)
		return 0;

	command = UCSI_GET_CABLE_PROPERTY | UCSI_CONNECTOR_NUMBER(con->num);
	ret = ucsi_send_command(con->ucsi, command, &con->cable_prop,
				sizeof(con->cable_prop));
	if (ret < 0) {
		dev_err(con->ucsi->dev, "GET_CABLE_PROPERTY failed (%d)\n",
			ret);
		return ret;
	}

	ret = ucsi_register_cable(con);
	if (ret < 0)
		return ret;

	if (con->ucsi->cap.features & UCSI_CAP_GET_PD_MESSAGE) {
		ret = ucsi_get_cable_identity(con);
		if (ret < 0)
			return ret;
	}

	if (con->ucsi->cap.features & UCSI_CAP_ALT_MODE_DETAILS) {
		ret = ucsi_register_plug(con);
		if (ret < 0)
			return ret;

		ret = ucsi_register_altmodes(con, UCSI_RECIPIENT_SOP_P);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void ucsi_handle_connector_change(struct work_struct *work)
{
	struct ucsi_connector *con = container_of(work, struct ucsi_connector,
						  work);
	struct ucsi *ucsi = con->ucsi;
	enum typec_role role;
	u64 command;
	int ret;

	mutex_lock(&con->lock);

	command = UCSI_GET_CONNECTOR_STATUS | UCSI_CONNECTOR_NUMBER(con->num);

	ret = ucsi_send_command_common(ucsi, command, &con->status,
				       sizeof(con->status), true);
	if (ret < 0) {
		dev_err(ucsi->dev, "%s: GET_CONNECTOR_STATUS failed (%d)\n",
			__func__, ret);
		clear_bit(EVENT_PENDING, &con->ucsi->flags);
		goto out_unlock;
	}

	trace_ucsi_connector_change(con->num, &con->status);

	if (ucsi->ops->connector_status)
		ucsi->ops->connector_status(con);

	role = !!(con->status.flags & UCSI_CONSTAT_PWR_DIR);

	if (con->status.change & UCSI_CONSTAT_POWER_DIR_CHANGE) {
		typec_set_pwr_role(con->port, role);

		/* Complete pending power role swap */
		if (!completion_done(&con->complete))
			complete(&con->complete);
	}

	if (con->status.change & UCSI_CONSTAT_CONNECT_CHANGE) {
		typec_set_pwr_role(con->port, role);
		ucsi_port_psy_changed(con);
		ucsi_partner_change(con);

		if (con->status.flags & UCSI_CONSTAT_CONNECTED) {
			ucsi_register_partner(con);
			ucsi_partner_task(con, ucsi_check_connection, 1, HZ);
			ucsi_partner_task(con, ucsi_check_connector_capability, 1, HZ);
			if (con->ucsi->cap.features & UCSI_CAP_GET_PD_MESSAGE)
				ucsi_partner_task(con, ucsi_get_partner_identity, 1, HZ);
			if (con->ucsi->cap.features & UCSI_CAP_CABLE_DETAILS)
				ucsi_partner_task(con, ucsi_check_cable, 1, HZ);

			if (UCSI_CONSTAT_PWR_OPMODE(con->status.flags) ==
			    UCSI_CONSTAT_PWR_OPMODE_PD)
				ucsi_partner_task(con, ucsi_register_partner_pdos, 1, HZ);
		} else {
			ucsi_unregister_partner(con);
		}
	}

	if (con->status.change & UCSI_CONSTAT_POWER_OPMODE_CHANGE ||
	    con->status.change & UCSI_CONSTAT_POWER_LEVEL_CHANGE)
		ucsi_pwr_opmode_change(con);

	if (con->partner && con->status.change & UCSI_CONSTAT_PARTNER_CHANGE) {
		ucsi_partner_change(con);

		/* Complete pending data role swap */
		if (!completion_done(&con->complete))
			complete(&con->complete);
	}

	if (con->status.change & UCSI_CONSTAT_CAM_CHANGE)
		ucsi_partner_task(con, ucsi_check_altmodes, 1, 0);

out_unlock:
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
		dev_dbg(ucsi->dev, "Early connector change event\n");
		return;
	}

	if (!test_and_set_bit(EVENT_PENDING, &ucsi->flags))
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
	u64 command;
	unsigned long tmo;
	u32 cci;
	int ret;

	mutex_lock(&ucsi->ppm_lock);

	ret = ucsi->ops->read(ucsi, UCSI_CCI, &cci, sizeof(cci));
	if (ret < 0)
		goto out;

	/*
	 * If UCSI_CCI_RESET_COMPLETE is already set we must clear
	 * the flag before we start another reset. Send a
	 * UCSI_SET_NOTIFICATION_ENABLE command to achieve this.
	 * Ignore a timeout and try the reset anyway if this fails.
	 */
	if (cci & UCSI_CCI_RESET_COMPLETE) {
		command = UCSI_SET_NOTIFICATION_ENABLE;
		ret = ucsi->ops->async_write(ucsi, UCSI_CONTROL, &command,
					     sizeof(command));
		if (ret < 0)
			goto out;

		tmo = jiffies + msecs_to_jiffies(UCSI_TIMEOUT_MS);
		do {
			ret = ucsi->ops->read(ucsi, UCSI_CCI,
					      &cci, sizeof(cci));
			if (ret < 0)
				goto out;
			if (cci & UCSI_CCI_COMMAND_COMPLETE)
				break;
			if (time_is_before_jiffies(tmo))
				break;
			msleep(20);
		} while (1);

		WARN_ON(cci & UCSI_CCI_RESET_COMPLETE);
	}

	command = UCSI_PPM_RESET;
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

		/* Give the PPM time to process a reset before reading CCI */
		msleep(20);

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

static int ucsi_register_port(struct ucsi *ucsi, struct ucsi_connector *con)
{
	struct typec_capability *cap = &con->typec_cap;
	enum typec_accessory *accessory = cap->accessory;
	enum usb_role u_role = USB_ROLE_NONE;
	u64 command;
	char *name;
	int ret;

	name = kasprintf(GFP_KERNEL, "%s-con%d", dev_name(ucsi->dev), con->num);
	if (!name)
		return -ENOMEM;

	con->wq = create_singlethread_workqueue(name);
	kfree(name);
	if (!con->wq)
		return -ENOMEM;

	INIT_WORK(&con->work, ucsi_handle_connector_change);
	init_completion(&con->complete);
	mutex_init(&con->lock);
	INIT_LIST_HEAD(&con->partner_tasks);
	con->ucsi = ucsi;

	cap->fwnode = ucsi_find_fwnode(con);
	con->usb_role_sw = fwnode_usb_role_switch_get(cap->fwnode);
	if (IS_ERR(con->usb_role_sw))
		return dev_err_probe(ucsi->dev, PTR_ERR(con->usb_role_sw),
			"con%d: failed to get usb role switch\n", con->num);

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

	cap->driver_data = con;
	cap->ops = &ucsi_ops;

	if (ucsi->ops->update_connector)
		ucsi->ops->update_connector(con);

	ret = ucsi_register_port_psy(con);
	if (ret)
		goto out;

	/* Register the connector */
	con->port = typec_register_port(ucsi->dev, cap);
	if (IS_ERR(con->port)) {
		ret = PTR_ERR(con->port);
		goto out;
	}

	if (!(ucsi->quirks & UCSI_DELAY_DEVICE_PDOS))
		ucsi_register_device_pdos(con);

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

	if (ucsi->ops->connector_status)
		ucsi->ops->connector_status(con);

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
		ucsi_register_partner(con);
		ucsi_pwr_opmode_change(con);
		ucsi_port_psy_changed(con);
		if (con->ucsi->cap.features & UCSI_CAP_GET_PD_MESSAGE)
			ucsi_get_partner_identity(con);
		if (con->ucsi->cap.features & UCSI_CAP_CABLE_DETAILS)
			ucsi_check_cable(con);
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

	if (con->partner &&
	    UCSI_CONSTAT_PWR_OPMODE(con->status.flags) ==
	    UCSI_CONSTAT_PWR_OPMODE_PD) {
		ucsi_register_device_pdos(con);
		ucsi_get_src_pdos(con);
		ucsi_check_altmodes(con);
	}

	trace_ucsi_register_port(con->num, &con->status);

out:
	fwnode_handle_put(cap->fwnode);
out_unlock:
	mutex_unlock(&con->lock);

	if (ret && con->wq) {
		destroy_workqueue(con->wq);
		con->wq = NULL;
	}

	return ret;
}

static u64 ucsi_get_supported_notifications(struct ucsi *ucsi)
{
	u8 features = ucsi->cap.features;
	u64 ntfy = UCSI_ENABLE_NTFY_ALL;

	if (!(features & UCSI_CAP_ALT_MODE_DETAILS))
		ntfy &= ~UCSI_ENABLE_NTFY_CAM_CHANGE;

	if (!(features & UCSI_CAP_PDO_DETAILS))
		ntfy &= ~(UCSI_ENABLE_NTFY_PWR_LEVEL_CHANGE |
			  UCSI_ENABLE_NTFY_CAP_CHANGE);

	if (!(features & UCSI_CAP_EXT_SUPPLY_NOTIFICATIONS))
		ntfy &= ~UCSI_ENABLE_NTFY_EXT_PWR_SRC_CHANGE;

	if (!(features & UCSI_CAP_PD_RESET))
		ntfy &= ~UCSI_ENABLE_NTFY_PD_RESET_COMPLETE;

	return ntfy;
}

/**
 * ucsi_init - Initialize UCSI interface
 * @ucsi: UCSI to be initialized
 *
 * Registers all ports @ucsi has and enables all notification events.
 */
static int ucsi_init(struct ucsi *ucsi)
{
	struct ucsi_connector *con, *connector;
	u64 command, ntfy;
	u32 cci;
	int ret;
	int i;

	/* Reset the PPM */
	ret = ucsi_reset_ppm(ucsi);
	if (ret) {
		dev_err(ucsi->dev, "failed to reset PPM!\n");
		goto err;
	}

	/* Enable basic notifications */
	ntfy = UCSI_ENABLE_NTFY_CMD_COMPLETE | UCSI_ENABLE_NTFY_ERROR;
	command = UCSI_SET_NOTIFICATION_ENABLE | ntfy;
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
	connector = kcalloc(ucsi->cap.num_connectors + 1, sizeof(*connector), GFP_KERNEL);
	if (!connector) {
		ret = -ENOMEM;
		goto err_reset;
	}

	/* Register all connectors */
	for (i = 0; i < ucsi->cap.num_connectors; i++) {
		connector[i].num = i + 1;
		ret = ucsi_register_port(ucsi, &connector[i]);
		if (ret)
			goto err_unregister;
	}

	/* Enable all supported notifications */
	ntfy = ucsi_get_supported_notifications(ucsi);
	command = UCSI_SET_NOTIFICATION_ENABLE | ntfy;
	ret = ucsi_send_command(ucsi, command, NULL, 0);
	if (ret < 0)
		goto err_unregister;

	ucsi->connector = connector;
	ucsi->ntfy = ntfy;

	mutex_lock(&ucsi->ppm_lock);
	ret = ucsi->ops->read(ucsi, UCSI_CCI, &cci, sizeof(cci));
	mutex_unlock(&ucsi->ppm_lock);
	if (ret)
		return ret;
	if (UCSI_CCI_CONNECTOR(cci))
		ucsi_connector_change(ucsi, UCSI_CCI_CONNECTOR(cci));

	return 0;

err_unregister:
	for (con = connector; con->port; con++) {
		ucsi_unregister_partner(con);
		ucsi_unregister_altmodes(con, UCSI_RECIPIENT_CON);
		ucsi_unregister_port_psy(con);
		if (con->wq)
			destroy_workqueue(con->wq);

		usb_power_delivery_unregister_capabilities(con->port_sink_caps);
		con->port_sink_caps = NULL;
		usb_power_delivery_unregister_capabilities(con->port_source_caps);
		con->port_source_caps = NULL;
		usb_power_delivery_unregister(con->pd);
		con->pd = NULL;
		typec_unregister_port(con->port);
		con->port = NULL;
	}
	kfree(connector);
err_reset:
	memset(&ucsi->cap, 0, sizeof(ucsi->cap));
	ucsi_reset_ppm(ucsi);
err:
	return ret;
}

static void ucsi_resume_work(struct work_struct *work)
{
	struct ucsi *ucsi = container_of(work, struct ucsi, resume_work);
	struct ucsi_connector *con;
	u64 command;
	int ret;

	/* Restore UCSI notification enable mask after system resume */
	command = UCSI_SET_NOTIFICATION_ENABLE | ucsi->ntfy;
	ret = ucsi_send_command(ucsi, command, NULL, 0);
	if (ret < 0) {
		dev_err(ucsi->dev, "failed to re-enable notifications (%d)\n", ret);
		return;
	}

	for (con = ucsi->connector; con->port; con++) {
		mutex_lock(&con->lock);
		ucsi_partner_task(con, ucsi_check_connection, 1, 0);
		mutex_unlock(&con->lock);
	}
}

int ucsi_resume(struct ucsi *ucsi)
{
	if (ucsi->connector)
		queue_work(system_long_wq, &ucsi->resume_work);
	return 0;
}
EXPORT_SYMBOL_GPL(ucsi_resume);

static void ucsi_init_work(struct work_struct *work)
{
	struct ucsi *ucsi = container_of(work, struct ucsi, work.work);
	int ret;

	ret = ucsi_init(ucsi);
	if (ret)
		dev_err_probe(ucsi->dev, ret, "PPM init failed\n");

	if (ret == -EPROBE_DEFER) {
		if (ucsi->work_count++ > UCSI_ROLE_SWITCH_WAIT_COUNT) {
			dev_err(ucsi->dev, "PPM init failed, stop trying\n");
			return;
		}

		queue_delayed_work(system_long_wq, &ucsi->work,
				   UCSI_ROLE_SWITCH_INTERVAL);
	}
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

	INIT_WORK(&ucsi->resume_work, ucsi_resume_work);
	INIT_DELAYED_WORK(&ucsi->work, ucsi_init_work);
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
	ucsi_debugfs_unregister(ucsi);
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

	/*
	 * Version format is JJ.M.N (JJ = Major version, M = Minor version,
	 * N = sub-minor version).
	 */
	dev_dbg(ucsi->dev, "Registered UCSI interface with version %x.%x.%x",
		UCSI_BCD_GET_MAJOR(ucsi->version),
		UCSI_BCD_GET_MINOR(ucsi->version),
		UCSI_BCD_GET_SUBMINOR(ucsi->version));

	queue_delayed_work(system_long_wq, &ucsi->work, 0);

	ucsi_debugfs_register(ucsi);
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
	cancel_delayed_work_sync(&ucsi->work);
	cancel_work_sync(&ucsi->resume_work);

	/* Disable notifications */
	ucsi->ops->async_write(ucsi, UCSI_CONTROL, &cmd, sizeof(cmd));

	if (!ucsi->connector)
		return;

	for (i = 0; i < ucsi->cap.num_connectors; i++) {
		cancel_work_sync(&ucsi->connector[i].work);
		ucsi_unregister_partner(&ucsi->connector[i]);
		ucsi_unregister_altmodes(&ucsi->connector[i],
					 UCSI_RECIPIENT_CON);
		ucsi_unregister_port_psy(&ucsi->connector[i]);

		if (ucsi->connector[i].wq) {
			struct ucsi_work *uwork;

			mutex_lock(&ucsi->connector[i].lock);
			/*
			 * queue delayed items immediately so they can execute
			 * and free themselves before the wq is destroyed
			 */
			list_for_each_entry(uwork, &ucsi->connector[i].partner_tasks, node)
				mod_delayed_work(ucsi->connector[i].wq, &uwork->work, 0);
			mutex_unlock(&ucsi->connector[i].lock);
			destroy_workqueue(ucsi->connector[i].wq);
		}

		usb_power_delivery_unregister_capabilities(ucsi->connector[i].port_sink_caps);
		ucsi->connector[i].port_sink_caps = NULL;
		usb_power_delivery_unregister_capabilities(ucsi->connector[i].port_source_caps);
		ucsi->connector[i].port_source_caps = NULL;
		usb_power_delivery_unregister(ucsi->connector[i].pd);
		ucsi->connector[i].pd = NULL;
		typec_unregister_port(ucsi->connector[i].port);
	}

	kfree(ucsi->connector);
}
EXPORT_SYMBOL_GPL(ucsi_unregister);

static int __init ucsi_module_init(void)
{
	ucsi_debugfs_init();
	return 0;
}
module_init(ucsi_module_init);

static void __exit ucsi_module_exit(void)
{
	ucsi_debugfs_exit();
}
module_exit(ucsi_module_exit);

MODULE_AUTHOR("Heikki Krogerus <heikki.krogerus@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("USB Type-C Connector System Software Interface driver");
