// SPDX-License-Identifier: GPL-2.0-only
/*
 * Alt-mode implementation on ChromeOS EC.
 *
 * Copyright 2024 Google LLC
 * Author: Abhishek Pandit-Subedi <abhishekpandit@chromium.org>
 */
#include "cros_ec_typec.h"

#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_tbt.h>
#include <linux/usb/pd_vdo.h>

#include "cros_typec_altmode.h"

struct cros_typec_altmode_data {
	struct work_struct work;
	struct cros_typec_port *port;
	struct typec_altmode *alt;
	bool ap_mode_entry;

	struct mutex lock;
	u32 header;
	u32 *vdo_data;
	u8 vdo_size;

	u16 sid;
	u8 mode;
};

struct cros_typec_dp_data {
	struct cros_typec_altmode_data adata;
	struct typec_displayport_data data;
	bool configured;
	bool pending_status_update;
};

static void cros_typec_altmode_work(struct work_struct *work)
{
	struct cros_typec_altmode_data *data =
		container_of(work, struct cros_typec_altmode_data, work);

	mutex_lock(&data->lock);

	if (typec_altmode_vdm(data->alt, data->header, data->vdo_data,
			      data->vdo_size))
		dev_err(&data->alt->dev, "VDM 0x%x failed\n", data->header);

	data->header = 0;
	data->vdo_data = NULL;
	data->vdo_size = 0;

	mutex_unlock(&data->lock);
}

static int cros_typec_altmode_enter(struct typec_altmode *alt, u32 *vdo)
{
	struct cros_typec_altmode_data *adata = typec_altmode_get_drvdata(alt);
	struct ec_params_typec_control req = {
		.port = adata->port->port_num,
		.command = TYPEC_CONTROL_COMMAND_ENTER_MODE,
	};
	int svdm_version;
	int ret;

	if (!adata->ap_mode_entry) {
		dev_warn(&alt->dev,
			 "EC does not support AP driven mode entry\n");
		return -EOPNOTSUPP;
	}

	if (adata->sid == USB_TYPEC_DP_SID)
		req.mode_to_enter = CROS_EC_ALTMODE_DP;
	else if (adata->sid == USB_TYPEC_TBT_SID)
		req.mode_to_enter = CROS_EC_ALTMODE_TBT;
	else
		return -EOPNOTSUPP;

	ret = cros_ec_cmd(adata->port->typec_data->ec, 0, EC_CMD_TYPEC_CONTROL,
			  &req, sizeof(req), NULL, 0);
	if (ret < 0)
		return ret;

	svdm_version = typec_altmode_get_svdm_version(alt);
	if (svdm_version < 0)
		return svdm_version;

	mutex_lock(&adata->lock);

	adata->header = VDO(adata->sid, 1, svdm_version, CMD_ENTER_MODE);
	adata->header |= VDO_OPOS(adata->mode);
	adata->header |= VDO_CMDT(CMDT_RSP_ACK);
	adata->vdo_data = NULL;
	adata->vdo_size = 1;
	schedule_work(&adata->work);

	mutex_unlock(&adata->lock);
	return ret;
}

static int cros_typec_altmode_exit(struct typec_altmode *alt)
{
	struct cros_typec_altmode_data *adata = typec_altmode_get_drvdata(alt);
	struct ec_params_typec_control req = {
		.port = adata->port->port_num,
		.command = TYPEC_CONTROL_COMMAND_EXIT_MODES,
	};
	int svdm_version;
	int ret;

	if (!adata->ap_mode_entry) {
		dev_warn(&alt->dev,
			 "EC does not support AP driven mode exit\n");
		return -EOPNOTSUPP;
	}

	ret = cros_ec_cmd(adata->port->typec_data->ec, 0, EC_CMD_TYPEC_CONTROL,
			  &req, sizeof(req), NULL, 0);

	if (ret < 0)
		return ret;

	svdm_version = typec_altmode_get_svdm_version(alt);
	if (svdm_version < 0)
		return svdm_version;

	mutex_lock(&adata->lock);

	adata->header = VDO(adata->sid, 1, svdm_version, CMD_EXIT_MODE);
	adata->header |= VDO_OPOS(adata->mode);
	adata->header |= VDO_CMDT(CMDT_RSP_ACK);
	adata->vdo_data = NULL;
	adata->vdo_size = 1;
	schedule_work(&adata->work);

	mutex_unlock(&adata->lock);
	return ret;
}

static int cros_typec_displayport_vdm(struct typec_altmode *alt, u32 header,
				      const u32 *data, int count)
{
	struct cros_typec_dp_data *dp_data = typec_altmode_get_drvdata(alt);
	struct cros_typec_altmode_data *adata = &dp_data->adata;


	int cmd_type = PD_VDO_CMDT(header);
	int cmd = PD_VDO_CMD(header);
	int svdm_version;

	svdm_version = typec_altmode_get_svdm_version(alt);
	if (svdm_version < 0)
		return svdm_version;

	mutex_lock(&adata->lock);

	switch (cmd_type) {
	case CMDT_INIT:
		if (PD_VDO_SVDM_VER(header) < svdm_version) {
			typec_partner_set_svdm_version(adata->port->partner,
						       PD_VDO_SVDM_VER(header));
			svdm_version = PD_VDO_SVDM_VER(header);
		}

		adata->header = VDO(adata->sid, 1, svdm_version, cmd);
		adata->header |= VDO_OPOS(adata->mode);

		/*
		 * DP_CMD_CONFIGURE: We can't actually do anything with the
		 * provided VDO yet so just send back an ACK.
		 *
		 * DP_CMD_STATUS_UPDATE: We wait for Mux changes to send
		 * DPStatus Acks.
		 */
		switch (cmd) {
		case DP_CMD_CONFIGURE:
			dp_data->data.conf = *data;
			adata->header |= VDO_CMDT(CMDT_RSP_ACK);
			dp_data->configured = true;
			schedule_work(&adata->work);
			break;
		case DP_CMD_STATUS_UPDATE:
			dp_data->pending_status_update = true;
			break;
		default:
			adata->header |= VDO_CMDT(CMDT_RSP_ACK);
			schedule_work(&adata->work);
			break;
		}

		break;
	default:
		break;
	}

	mutex_unlock(&adata->lock);
	return 0;
}

static int cros_typec_thunderbolt_vdm(struct typec_altmode *alt, u32 header,
				      const u32 *data, int count)
{
	struct cros_typec_altmode_data *adata = typec_altmode_get_drvdata(alt);

	int cmd_type = PD_VDO_CMDT(header);
	int cmd = PD_VDO_CMD(header);
	int svdm_version;

	svdm_version = typec_altmode_get_svdm_version(alt);
	if (svdm_version < 0)
		return svdm_version;

	mutex_lock(&adata->lock);

	switch (cmd_type) {
	case CMDT_INIT:
		if (PD_VDO_SVDM_VER(header) < svdm_version) {
			typec_partner_set_svdm_version(adata->port->partner,
						       PD_VDO_SVDM_VER(header));
			svdm_version = PD_VDO_SVDM_VER(header);
		}

		adata->header = VDO(adata->sid, 1, svdm_version, cmd);
		adata->header |= VDO_OPOS(adata->mode);

		switch (cmd) {
		case CMD_ENTER_MODE:
			/* Don't respond to the enter mode vdm because it
			 * triggers mux configuration. This is handled directly
			 * by the cros_ec_typec driver so the Thunderbolt driver
			 * doesn't need to be involved.
			 */
			break;
		default:
			adata->header |= VDO_CMDT(CMDT_RSP_ACK);
			schedule_work(&adata->work);
			break;
		}

		break;
	default:
		break;
	}

	mutex_unlock(&adata->lock);
	return 0;
}


static int cros_typec_altmode_vdm(struct typec_altmode *alt, u32 header,
				      const u32 *data, int count)
{
	struct cros_typec_altmode_data *adata = typec_altmode_get_drvdata(alt);

	if (!adata->ap_mode_entry)
		return -EOPNOTSUPP;

	if (adata->sid == USB_TYPEC_DP_SID)
		return cros_typec_displayport_vdm(alt, header, data, count);

	if (adata->sid == USB_TYPEC_TBT_SID)
		return cros_typec_thunderbolt_vdm(alt, header, data, count);

	return -EINVAL;
}

static const struct typec_altmode_ops cros_typec_altmode_ops = {
	.enter = cros_typec_altmode_enter,
	.exit = cros_typec_altmode_exit,
	.vdm = cros_typec_altmode_vdm,
};

#if IS_ENABLED(CONFIG_TYPEC_DP_ALTMODE)
int cros_typec_displayport_status_update(struct typec_altmode *altmode,
					 struct typec_displayport_data *data)
{
	struct cros_typec_dp_data *dp_data =
		typec_altmode_get_drvdata(altmode);
	struct cros_typec_altmode_data *adata = &dp_data->adata;

	if (!dp_data->pending_status_update) {
		dev_dbg(&altmode->dev,
			"Got DPStatus without a pending request\n");
		return 0;
	}

	if (dp_data->configured && dp_data->data.conf != data->conf)
		dev_dbg(&altmode->dev,
			"DP Conf doesn't match. Requested 0x%04x, Actual 0x%04x\n",
			dp_data->data.conf, data->conf);

	mutex_lock(&adata->lock);

	dp_data->data = *data;
	dp_data->pending_status_update = false;
	adata->header |= VDO_CMDT(CMDT_RSP_ACK);
	adata->vdo_data = &dp_data->data.status;
	adata->vdo_size = 2;
	schedule_work(&adata->work);

	mutex_unlock(&adata->lock);

	return 0;
}

struct typec_altmode *
cros_typec_register_displayport(struct cros_typec_port *port,
				struct typec_altmode_desc *desc,
				bool ap_mode_entry)
{
	struct typec_altmode *alt;
	struct cros_typec_dp_data *dp_data;
	struct cros_typec_altmode_data *adata;

	alt = typec_port_register_altmode(port->port, desc);
	if (IS_ERR(alt))
		return alt;

	dp_data = devm_kzalloc(&alt->dev, sizeof(*dp_data), GFP_KERNEL);
	if (!dp_data) {
		typec_unregister_altmode(alt);
		return ERR_PTR(-ENOMEM);
	}

	adata = &dp_data->adata;
	INIT_WORK(&adata->work, cros_typec_altmode_work);
	mutex_init(&adata->lock);
	adata->alt = alt;
	adata->port = port;
	adata->ap_mode_entry = ap_mode_entry;
	adata->sid = desc->svid;
	adata->mode = desc->mode;

	typec_altmode_set_ops(alt, &cros_typec_altmode_ops);
	typec_altmode_set_drvdata(alt, adata);

	return alt;
}
#endif

#if IS_ENABLED(CONFIG_TYPEC_TBT_ALTMODE)
struct typec_altmode *
cros_typec_register_thunderbolt(struct cros_typec_port *port,
				struct typec_altmode_desc *desc)
{
	struct typec_altmode *alt;
	struct cros_typec_altmode_data *adata;

	alt = typec_port_register_altmode(port->port, desc);
	if (IS_ERR(alt))
		return alt;

	adata = devm_kzalloc(&alt->dev, sizeof(*adata), GFP_KERNEL);
	if (!adata) {
		typec_unregister_altmode(alt);
		return ERR_PTR(-ENOMEM);
	}

	INIT_WORK(&adata->work, cros_typec_altmode_work);
	adata->alt = alt;
	adata->port = port;
	adata->ap_mode_entry = true;
	adata->sid = desc->svid;
	adata->mode = desc->mode;

	typec_altmode_set_ops(alt, &cros_typec_altmode_ops);
	typec_altmode_set_drvdata(alt, adata);

	return alt;
}
#endif
