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

struct typec_altmode_ops port_amode_ops = {
	.enter = cros_typec_port_amode_enter,
};
