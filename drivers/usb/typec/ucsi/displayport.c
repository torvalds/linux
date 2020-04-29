// SPDX-License-Identifier: GPL-2.0
/*
 * UCSI DisplayPort Alternate Mode Support
 *
 * Copyright (C) 2018, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/usb/typec_dp.h>
#include <linux/usb/pd_vdo.h>

#include "ucsi.h"

#define UCSI_CMD_SET_NEW_CAM(_con_num_, _enter_, _cam_, _am_)		\
	 (UCSI_SET_NEW_CAM | ((_con_num_) << 16) | ((_enter_) << 23) |	\
	  ((_cam_) << 24) | ((u64)(_am_) << 32))

struct ucsi_dp {
	struct typec_displayport_data data;
	struct ucsi_connector *con;
	struct typec_altmode *alt;
	struct work_struct work;
	int offset;

	bool override;
	bool initialized;

	u32 header;
	u32 *vdo_data;
	u8 vdo_size;
};

/*
 * Note. Alternate mode control is optional feature in UCSI. It means that even
 * if the system supports alternate modes, the OS may not be aware of them.
 *
 * In most cases however, the OS will be able to see the supported alternate
 * modes, but it may still not be able to configure them, not even enter or exit
 * them. That is because UCSI defines alt mode details and alt mode "overriding"
 * as separate options.
 *
 * In case alt mode details are supported, but overriding is not, the driver
 * will still display the supported pin assignments and configuration, but any
 * changes the user attempts to do will lead into failure with return value of
 * -EOPNOTSUPP.
 */

static int ucsi_displayport_enter(struct typec_altmode *alt, u32 *vdo)
{
	struct ucsi_dp *dp = typec_altmode_get_drvdata(alt);
	struct ucsi *ucsi = dp->con->ucsi;
	u64 command;
	u8 cur = 0;
	int ret;

	mutex_lock(&dp->con->lock);

	if (!dp->override && dp->initialized) {
		const struct typec_altmode *p = typec_altmode_get_partner(alt);

		dev_warn(&p->dev,
			 "firmware doesn't support alternate mode overriding\n");
		ret = -EOPNOTSUPP;
		goto err_unlock;
	}

	command = UCSI_GET_CURRENT_CAM | UCSI_CONNECTOR_NUMBER(dp->con->num);
	ret = ucsi_send_command(ucsi, command, &cur, sizeof(cur));
	if (ret < 0) {
		if (ucsi->version > 0x0100)
			goto err_unlock;
		cur = 0xff;
	}

	if (cur != 0xff) {
		ret = dp->con->port_altmode[cur] == alt ? 0 : -EBUSY;
		goto err_unlock;
	}

	/*
	 * We can't send the New CAM command yet to the PPM as it needs the
	 * configuration value as well. Pretending that we have now entered the
	 * mode, and letting the alt mode driver continue.
	 */

	dp->header = VDO(USB_TYPEC_DP_SID, 1, CMD_ENTER_MODE);
	dp->header |= VDO_OPOS(USB_TYPEC_DP_MODE);
	dp->header |= VDO_CMDT(CMDT_RSP_ACK);

	dp->vdo_data = NULL;
	dp->vdo_size = 1;

	schedule_work(&dp->work);
	ret = 0;
err_unlock:
	mutex_unlock(&dp->con->lock);

	return ret;
}

static int ucsi_displayport_exit(struct typec_altmode *alt)
{
	struct ucsi_dp *dp = typec_altmode_get_drvdata(alt);
	u64 command;
	int ret = 0;

	mutex_lock(&dp->con->lock);

	if (!dp->override) {
		const struct typec_altmode *p = typec_altmode_get_partner(alt);

		dev_warn(&p->dev,
			 "firmware doesn't support alternate mode overriding\n");
		ret = -EOPNOTSUPP;
		goto out_unlock;
	}

	command = UCSI_CMD_SET_NEW_CAM(dp->con->num, 0, dp->offset, 0);
	ret = ucsi_send_command(dp->con->ucsi, command, NULL, 0);
	if (ret < 0)
		goto out_unlock;

	dp->header = VDO(USB_TYPEC_DP_SID, 1, CMD_EXIT_MODE);
	dp->header |= VDO_OPOS(USB_TYPEC_DP_MODE);
	dp->header |= VDO_CMDT(CMDT_RSP_ACK);

	dp->vdo_data = NULL;
	dp->vdo_size = 1;

	schedule_work(&dp->work);

out_unlock:
	mutex_unlock(&dp->con->lock);

	return ret;
}

/*
 * We do not actually have access to the Status Update VDO, so we have to guess
 * things.
 */
static int ucsi_displayport_status_update(struct ucsi_dp *dp)
{
	u32 cap = dp->alt->vdo;

	dp->data.status = DP_STATUS_ENABLED;

	/*
	 * If pin assignement D is supported, claiming always
	 * that Multi-function is preferred.
	 */
	if (DP_CAP_CAPABILITY(cap) & DP_CAP_UFP_D) {
		dp->data.status |= DP_STATUS_CON_UFP_D;

		if (DP_CAP_UFP_D_PIN_ASSIGN(cap) & BIT(DP_PIN_ASSIGN_D))
			dp->data.status |= DP_STATUS_PREFER_MULTI_FUNC;
	} else {
		dp->data.status |= DP_STATUS_CON_DFP_D;

		if (DP_CAP_DFP_D_PIN_ASSIGN(cap) & BIT(DP_PIN_ASSIGN_D))
			dp->data.status |= DP_STATUS_PREFER_MULTI_FUNC;
	}

	dp->vdo_data = &dp->data.status;
	dp->vdo_size = 2;

	return 0;
}

static int ucsi_displayport_configure(struct ucsi_dp *dp)
{
	u32 pins = DP_CONF_GET_PIN_ASSIGN(dp->data.conf);
	u64 command;

	if (!dp->override)
		return 0;

	command = UCSI_CMD_SET_NEW_CAM(dp->con->num, 1, dp->offset, pins);

	return ucsi_send_command(dp->con->ucsi, command, NULL, 0);
}

static int ucsi_displayport_vdm(struct typec_altmode *alt,
				u32 header, const u32 *data, int count)
{
	struct ucsi_dp *dp = typec_altmode_get_drvdata(alt);
	int cmd_type = PD_VDO_CMDT(header);
	int cmd = PD_VDO_CMD(header);

	mutex_lock(&dp->con->lock);

	if (!dp->override && dp->initialized) {
		const struct typec_altmode *p = typec_altmode_get_partner(alt);

		dev_warn(&p->dev,
			 "firmware doesn't support alternate mode overriding\n");
		mutex_unlock(&dp->con->lock);
		return -EOPNOTSUPP;
	}

	switch (cmd_type) {
	case CMDT_INIT:
		dp->header = VDO(USB_TYPEC_DP_SID, 1, cmd);
		dp->header |= VDO_OPOS(USB_TYPEC_DP_MODE);

		switch (cmd) {
		case DP_CMD_STATUS_UPDATE:
			if (ucsi_displayport_status_update(dp))
				dp->header |= VDO_CMDT(CMDT_RSP_NAK);
			else
				dp->header |= VDO_CMDT(CMDT_RSP_ACK);
			break;
		case DP_CMD_CONFIGURE:
			dp->data.conf = *data;
			if (ucsi_displayport_configure(dp)) {
				dp->header |= VDO_CMDT(CMDT_RSP_NAK);
			} else {
				dp->header |= VDO_CMDT(CMDT_RSP_ACK);
				if (dp->initialized)
					ucsi_altmode_update_active(dp->con);
				else
					dp->initialized = true;
			}
			break;
		default:
			dp->header |= VDO_CMDT(CMDT_RSP_ACK);
			break;
		}

		schedule_work(&dp->work);
		break;
	default:
		break;
	}

	mutex_unlock(&dp->con->lock);

	return 0;
}

static const struct typec_altmode_ops ucsi_displayport_ops = {
	.enter = ucsi_displayport_enter,
	.exit = ucsi_displayport_exit,
	.vdm = ucsi_displayport_vdm,
};

static void ucsi_displayport_work(struct work_struct *work)
{
	struct ucsi_dp *dp = container_of(work, struct ucsi_dp, work);
	int ret;

	mutex_lock(&dp->con->lock);

	ret = typec_altmode_vdm(dp->alt, dp->header,
				dp->vdo_data, dp->vdo_size);
	if (ret)
		dev_err(&dp->alt->dev, "VDM 0x%x failed\n", dp->header);

	dp->vdo_data = NULL;
	dp->vdo_size = 0;
	dp->header = 0;

	mutex_unlock(&dp->con->lock);
}

void ucsi_displayport_remove_partner(struct typec_altmode *alt)
{
	struct ucsi_dp *dp;

	if (!alt)
		return;

	dp = typec_altmode_get_drvdata(alt);
	if (!dp)
		return;

	dp->data.conf = 0;
	dp->data.status = 0;
	dp->initialized = false;
}

struct typec_altmode *ucsi_register_displayport(struct ucsi_connector *con,
						bool override, int offset,
						struct typec_altmode_desc *desc)
{
	u8 all_assignments = BIT(DP_PIN_ASSIGN_C) | BIT(DP_PIN_ASSIGN_D) |
			     BIT(DP_PIN_ASSIGN_E);
	struct typec_altmode *alt;
	struct ucsi_dp *dp;

	mutex_lock(&con->lock);

	/* We can't rely on the firmware with the capabilities. */
	desc->vdo |= DP_CAP_DP_SIGNALING | DP_CAP_RECEPTACLE;

	/* Claiming that we support all pin assignments */
	desc->vdo |= all_assignments << 8;
	desc->vdo |= all_assignments << 16;

	alt = typec_port_register_altmode(con->port, desc);
	if (IS_ERR(alt)) {
		mutex_unlock(&con->lock);
		return alt;
	}

	dp = devm_kzalloc(&alt->dev, sizeof(*dp), GFP_KERNEL);
	if (!dp) {
		typec_unregister_altmode(alt);
		mutex_unlock(&con->lock);
		return ERR_PTR(-ENOMEM);
	}

	INIT_WORK(&dp->work, ucsi_displayport_work);
	dp->override = override;
	dp->offset = offset;
	dp->con = con;
	dp->alt = alt;

	alt->ops = &ucsi_displayport_ops;
	typec_altmode_set_drvdata(alt, dp);

	mutex_unlock(&con->lock);

	return alt;
}
