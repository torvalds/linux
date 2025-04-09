// SPDX-License-Identifier: GPL-2.0
/*
 * UCSI debugfs interface
 *
 * Copyright (C) 2023 Intel Corporation
 *
 * Authors: Rajaram Regupathy <rajaram.regupathy@intel.com>
 *	    Gopal Saranya <saranya.gopal@intel.com>
 */
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/usb.h>

#include <asm/errno.h>

#include "ucsi.h"

static struct dentry *ucsi_debugfs_root;

static int ucsi_cmd(void *data, u64 val)
{
	struct ucsi *ucsi = data;
	int ret;

	memset(&ucsi->debugfs->response, 0, sizeof(ucsi->debugfs->response));
	ucsi->debugfs->status = 0;

	switch (UCSI_COMMAND(val)) {
	case UCSI_SET_CCOM:
	case UCSI_SET_UOR:
	case UCSI_SET_PDR:
	case UCSI_CONNECTOR_RESET:
	case UCSI_SET_SINK_PATH:
	case UCSI_SET_NEW_CAM:
		ret = ucsi_send_command(ucsi, val, NULL, 0);
		break;
	case UCSI_GET_CAPABILITY:
	case UCSI_GET_CONNECTOR_CAPABILITY:
	case UCSI_GET_ALTERNATE_MODES:
	case UCSI_GET_CURRENT_CAM:
	case UCSI_GET_PDOS:
	case UCSI_GET_CABLE_PROPERTY:
	case UCSI_GET_CONNECTOR_STATUS:
	case UCSI_GET_ERROR_STATUS:
	case UCSI_GET_CAM_CS:
	case UCSI_GET_LPM_PPM_INFO:
		ret = ucsi_send_command(ucsi, val,
					&ucsi->debugfs->response,
					sizeof(ucsi->debugfs->response));
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	if (ret < 0) {
		ucsi->debugfs->status = ret;
		return ret;
	}

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(ucsi_cmd_fops, NULL, ucsi_cmd, "0x%llx\n");

static int ucsi_resp_show(struct seq_file *s, void *not_used)
{
	struct ucsi *ucsi = s->private;

	if (ucsi->debugfs->status)
		return ucsi->debugfs->status;

	seq_printf(s, "0x%016llx%016llx\n", ucsi->debugfs->response.high,
		   ucsi->debugfs->response.low);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ucsi_resp);

void ucsi_debugfs_register(struct ucsi *ucsi)
{
	ucsi->debugfs = kzalloc(sizeof(*ucsi->debugfs), GFP_KERNEL);
	if (!ucsi->debugfs)
		return;

	ucsi->debugfs->dentry = debugfs_create_dir(dev_name(ucsi->dev), ucsi_debugfs_root);
	debugfs_create_file("command", 0200, ucsi->debugfs->dentry, ucsi, &ucsi_cmd_fops);
	debugfs_create_file("response", 0400, ucsi->debugfs->dentry, ucsi, &ucsi_resp_fops);
}

void ucsi_debugfs_unregister(struct ucsi *ucsi)
{
	if (IS_ERR_OR_NULL(ucsi) || !ucsi->debugfs)
		return;

	debugfs_remove_recursive(ucsi->debugfs->dentry);
	kfree(ucsi->debugfs);
}

void ucsi_debugfs_init(void)
{
	ucsi_debugfs_root = debugfs_create_dir("ucsi", usb_debug_root);
}

void ucsi_debugfs_exit(void)
{
	debugfs_remove(ucsi_debugfs_root);
}
