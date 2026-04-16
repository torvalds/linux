// SPDX-License-Identifier: GPL-2.0
/*
 * UCSI Thunderbolt Alternate Mode Support
 *
 * Copyright 2026 Google LLC
 */

#include <linux/usb/typec_tbt.h>
#include <linux/usb/pd_vdo.h>
#include <linux/err.h>
#include <linux/dev_printk.h>
#include <linux/device/devres.h>
#include <linux/gfp_types.h>
#include <linux/types.h>
#include <linux/usb/typec_altmode.h>
#include <linux/workqueue.h>

#include "ucsi.h"

/**
 * struct ucsi_tbt - Thunderbolt Alternate Mode private data structure
 * @con: Pointer to UCSI connector structure
 * @alt: Pointer to typec altmode structure
 * @work: Work structure
 * @cam: An offset into the list of alternate modes supported by the PPM
 * @header: VDO header
 */
struct ucsi_tbt {
	struct ucsi_connector *con;
	struct typec_altmode *alt;
	struct work_struct work;
	int cam;
	u32 header;
};

static void ucsi_thunderbolt_work(struct work_struct *work)
{
	struct ucsi_tbt *tbt = container_of(work, struct ucsi_tbt, work);

	if (typec_altmode_vdm(tbt->alt, tbt->header, NULL, 0))
		dev_err(&tbt->alt->dev, "VDM 0x%x failed\n", tbt->header);

	tbt->header = 0;
}

static int ucsi_thunderbolt_set_altmode(struct ucsi_tbt *tbt,
					bool enter, u32 vdo)
{
	int svdm_version;
	int cmd;
	int ret;
	u64 command = UCSI_SET_NEW_CAM |
		      UCSI_CONNECTOR_NUMBER(tbt->con->num) |
		      UCSI_SET_NEW_CAM_SET_AM(tbt->cam) |
		      ((u64)vdo << 32);

	if (enter)
		command |= (1 << 23);

	ret = ucsi_send_command(tbt->con->ucsi, command, NULL, 0);
	if (ret < 0)
		return ret;

	svdm_version = typec_altmode_get_svdm_version(tbt->alt);
	if (svdm_version < 0)
		return svdm_version;

	if (enter)
		cmd = CMD_ENTER_MODE;
	else
		cmd = CMD_EXIT_MODE;
	tbt->header = VDO(USB_TYPEC_TBT_SID, 1, svdm_version, cmd);
	tbt->header |= VDO_OPOS(TYPEC_TBT_MODE);
	tbt->header |= VDO_CMDT(CMDT_RSP_ACK);

	schedule_work(&tbt->work);

	return 0;
}

static int ucsi_thunderbolt_enter(struct typec_altmode *alt, u32 *vdo)
{
	struct ucsi_tbt *tbt = typec_altmode_get_drvdata(alt);
	struct ucsi_connector *con = tbt->con;
	u64 command;
	u8 cur = 0;
	int ret;

	if (!ucsi_con_mutex_lock(con))
		return -ENOTCONN;

	command = UCSI_GET_CURRENT_CAM | UCSI_CONNECTOR_NUMBER(con->num);
	ret = ucsi_send_command(con->ucsi, command, &cur, sizeof(cur));
	if (ret < 0) {
		if (con->ucsi->version > 0x0100)
			goto err_unlock;
		cur = 0xff;
	}

	if (cur != 0xff) {
		if (cur >= UCSI_MAX_ALTMODES || con->port_altmode[cur] != alt)
			ret = -EBUSY;
		else
			ret = 0;
		goto err_unlock;
	}

	ret = ucsi_thunderbolt_set_altmode(tbt, true, *vdo);
	ucsi_altmode_update_active(tbt->con);

err_unlock:
	ucsi_con_mutex_unlock(con);

	return ret;
}

static int ucsi_thunderbolt_exit(struct typec_altmode *alt)
{
	struct ucsi_tbt *tbt = typec_altmode_get_drvdata(alt);
	int ret;

	if (!ucsi_con_mutex_lock(tbt->con))
		return -ENOTCONN;

	ret = ucsi_thunderbolt_set_altmode(tbt, false, 0);

	ucsi_con_mutex_unlock(tbt->con);

	return ret;
}

static int ucsi_thunderbolt_vdm(struct typec_altmode *alt,
				u32 header, const u32 *data, int count)
{
	struct ucsi_tbt *tbt = typec_altmode_get_drvdata(alt);
	int cmd_type = PD_VDO_CMDT(header);
	int cmd = PD_VDO_CMD(header);
	int svdm_version;

	if (!ucsi_con_mutex_lock(tbt->con))
		return -ENOTCONN;

	svdm_version = typec_altmode_get_svdm_version(alt);
	if (svdm_version < 0) {
		ucsi_con_mutex_unlock(tbt->con);
		return svdm_version;
	}

	switch (cmd_type) {
	case CMDT_INIT:
		if (PD_VDO_SVDM_VER(header) < svdm_version) {
			svdm_version = PD_VDO_SVDM_VER(header);
			typec_partner_set_svdm_version(tbt->con->partner, svdm_version);
		}
		tbt->header = VDO(USB_TYPEC_TBT_SID, 1, svdm_version, cmd);
		tbt->header |= VDO_OPOS(TYPEC_TBT_MODE);
		tbt->header |= VDO_CMDT(CMDT_RSP_ACK);

		schedule_work(&tbt->work);
		break;
	default:
		break;
	}

	ucsi_con_mutex_unlock(tbt->con);

	return 0;
}

static const struct typec_altmode_ops ucsi_thunderbolt_ops = {
	.enter = ucsi_thunderbolt_enter,
	.exit = ucsi_thunderbolt_exit,
	.vdm = ucsi_thunderbolt_vdm,
};

struct typec_altmode *ucsi_register_thunderbolt(struct ucsi_connector *con,
						bool override, int offset,
						struct typec_altmode_desc *desc)
{
	struct typec_altmode *alt;
	struct ucsi_tbt *tbt;

	alt = typec_port_register_altmode(con->port, desc);
	if (IS_ERR(alt) || !override)
		return alt;

	tbt = devm_kzalloc(&alt->dev, sizeof(*tbt), GFP_KERNEL);
	if (!tbt) {
		typec_unregister_altmode(alt);
		return ERR_PTR(-ENOMEM);
	}

	tbt->cam = offset;
	tbt->con = con;
	tbt->alt = alt;
	INIT_WORK(&tbt->work, ucsi_thunderbolt_work);
	typec_altmode_set_drvdata(alt, tbt);
	typec_altmode_set_ops(alt, &ucsi_thunderbolt_ops);

	return alt;
}

void ucsi_thunderbolt_remove_partner(struct typec_altmode *alt)
{
	struct ucsi_tbt *tbt;

	if (alt) {
		tbt = typec_altmode_get_drvdata(alt);
		if (tbt)
			cancel_work_sync(&tbt->work);
	}
}
