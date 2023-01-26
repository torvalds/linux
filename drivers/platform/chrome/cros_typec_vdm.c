// SPDX-License-Identifier: GPL-2.0-only
/*
 * USB Power Delivery Vendor Defined Message (VDM) support code.
 *
 * Copyright 2023 Google LLC
 * Author: Prashant Malani <pmalani@chromium.org>
 */

#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/usb/pd_vdo.h>

#include "cros_ec_typec.h"
#include "cros_typec_vdm.h"

/*
 * Retrieves pending VDM attention messages from the EC and forwards them to the altmode driver
 * based on SVID.
 */
void cros_typec_handle_vdm_attention(struct cros_typec_data *typec, int port_num)
{
	struct ec_response_typec_vdm_response resp;
	struct ec_params_typec_vdm_response req = {
		.port = port_num,
	};
	struct typec_altmode *amode;
	u16 svid;
	u32 hdr;
	int ret;

	do {
		ret = cros_ec_cmd(typec->ec, 0, EC_CMD_TYPEC_VDM_RESPONSE, &req,
				sizeof(req), &resp, sizeof(resp));
		if (ret < 0) {
			dev_warn(typec->dev, "Failed VDM response fetch, port: %d\n", port_num);
			return;
		}

		hdr = resp.vdm_response[0];
		svid = PD_VDO_VID(hdr);
		dev_dbg(typec->dev, "Received VDM Attention header: %x, port: %d\n", hdr, port_num);

		amode = typec_match_altmode(typec->ports[port_num]->port_altmode,
					    CROS_EC_ALTMODE_MAX, svid, PD_VDO_OPOS(hdr));
		if (!amode) {
			dev_err(typec->dev,
				"Received VDM for unregistered altmode (SVID:%x), port: %d\n",
				svid, port_num);
			return;
		}

		typec_altmode_attention(amode, resp.vdm_attention[1]);
	} while (resp.vdm_attention_left);
}

/*
 * Retrieves a VDM response from the EC and forwards it to the altmode driver based on SVID.
 */
void cros_typec_handle_vdm_response(struct cros_typec_data *typec, int port_num)
{
	struct ec_response_typec_vdm_response resp;
	struct ec_params_typec_vdm_response req = {
		.port = port_num,
	};
	struct typec_altmode *amode;
	u16 svid;
	u32 hdr;
	int ret;

	ret = cros_ec_cmd(typec->ec, 0, EC_CMD_TYPEC_VDM_RESPONSE, &req,
			  sizeof(req), &resp, sizeof(resp));
	if (ret < 0) {
		dev_warn(typec->dev, "Failed VDM response fetch, port: %d\n", port_num);
		return;
	}

	hdr = resp.vdm_response[0];
	svid = PD_VDO_VID(hdr);
	dev_dbg(typec->dev, "Received VDM header: %x, port: %d\n", hdr, port_num);

	amode = typec_match_altmode(typec->ports[port_num]->port_altmode, CROS_EC_ALTMODE_MAX,
				    svid, PD_VDO_OPOS(hdr));
	if (!amode) {
		dev_err(typec->dev, "Received VDM for unregistered altmode (SVID:%x), port: %d\n",
			svid, port_num);
		return;
	}

	ret = typec_altmode_vdm(amode, hdr, &resp.vdm_response[1], resp.vdm_data_objects);
	if (ret)
		dev_err(typec->dev, "Failed to forward VDM to altmode (SVID:%x), port: %d\n",
			svid, port_num);
}

static int cros_typec_port_amode_enter(struct typec_altmode *amode, u32 *vdo)
{
	struct cros_typec_port *port = typec_altmode_get_drvdata(amode);
	struct ec_params_typec_control req = {
		.port = port->port_num,
		.command = TYPEC_CONTROL_COMMAND_SEND_VDM_REQ,
	};
	struct typec_vdm_req vdm_req = {};
	u32 hdr;

	hdr = VDO(amode->svid, 1, SVDM_VER_2_0, CMD_ENTER_MODE);
	hdr |= VDO_OPOS(amode->mode);

	vdm_req.vdm_data[0] = hdr;
	vdm_req.vdm_data_objects = 1;
	vdm_req.partner_type = TYPEC_PARTNER_SOP;
	req.vdm_req_params = vdm_req;

	dev_dbg(port->typec_data->dev, "Sending EnterMode VDM, hdr: %x, port: %d\n",
		hdr, port->port_num);

	return cros_ec_cmd(port->typec_data->ec, 0, EC_CMD_TYPEC_CONTROL, &req,
			   sizeof(req), NULL, 0);
}

static int cros_typec_port_amode_vdm(struct typec_altmode *amode, const u32 hdr,
				     const u32 *vdo, int cnt)
{
	struct cros_typec_port *port = typec_altmode_get_drvdata(amode);
	struct ec_params_typec_control req = {
		.port = port->port_num,
		.command = TYPEC_CONTROL_COMMAND_SEND_VDM_REQ,
	};
	struct typec_vdm_req vdm_req = {};
	int i;

	vdm_req.vdm_data[0] = hdr;
	vdm_req.vdm_data_objects = cnt;
	for (i = 1; i < cnt; i++)
		vdm_req.vdm_data[i] = vdo[i-1];
	vdm_req.partner_type = TYPEC_PARTNER_SOP;
	req.vdm_req_params = vdm_req;

	dev_dbg(port->typec_data->dev, "Sending VDM, hdr: %x, num_objects: %d, port: %d\n",
		hdr, cnt, port->port_num);

	return cros_ec_cmd(port->typec_data->ec, 0, EC_CMD_TYPEC_CONTROL, &req,
			   sizeof(req), NULL, 0);
}

struct typec_altmode_ops port_amode_ops = {
	.enter = cros_typec_port_amode_enter,
	.vdm = cros_typec_port_amode_vdm,
};
