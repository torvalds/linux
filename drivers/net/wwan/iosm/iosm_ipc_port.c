// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-21 Intel Corporation.
 */

#include "iosm_ipc_chnl_cfg.h"
#include "iosm_ipc_imem_ops.h"
#include "iosm_ipc_port.h"

/* open logical channel for control communication */
static int ipc_port_ctrl_start(struct wwan_port *port)
{
	struct iosm_cdev *ipc_port = wwan_port_get_drvdata(port);
	int ret = 0;

	ipc_port->channel = ipc_imem_sys_port_open(ipc_port->ipc_imem,
						   ipc_port->chl_id,
						   IPC_HP_CDEV_OPEN);
	if (!ipc_port->channel)
		ret = -EIO;

	return ret;
}

/* close logical channel */
static void ipc_port_ctrl_stop(struct wwan_port *port)
{
	struct iosm_cdev *ipc_port = wwan_port_get_drvdata(port);

	ipc_imem_sys_port_close(ipc_port->ipc_imem, ipc_port->channel);
}

/* transfer control data to modem */
static int ipc_port_ctrl_tx(struct wwan_port *port, struct sk_buff *skb)
{
	struct iosm_cdev *ipc_port = wwan_port_get_drvdata(port);

	return ipc_imem_sys_cdev_write(ipc_port, skb);
}

static const struct wwan_port_ops ipc_wwan_ctrl_ops = {
	.start = ipc_port_ctrl_start,
	.stop = ipc_port_ctrl_stop,
	.tx = ipc_port_ctrl_tx,
};

/* Port init func */
struct iosm_cdev *ipc_port_init(struct iosm_imem *ipc_imem,
				struct ipc_chnl_cfg ipc_port_cfg)
{
	struct iosm_cdev *ipc_port = kzalloc(sizeof(*ipc_port), GFP_KERNEL);
	enum wwan_port_type port_type = ipc_port_cfg.wwan_port_type;
	enum ipc_channel_id chl_id = ipc_port_cfg.id;

	if (!ipc_port)
		return NULL;

	ipc_port->dev = ipc_imem->dev;
	ipc_port->pcie = ipc_imem->pcie;

	ipc_port->port_type = port_type;
	ipc_port->chl_id = chl_id;
	ipc_port->ipc_imem = ipc_imem;

	ipc_port->iosm_port = wwan_create_port(ipc_port->dev, port_type,
					       &ipc_wwan_ctrl_ops, NULL,
					       ipc_port);

	return ipc_port;
}

/* Port deinit func */
void ipc_port_deinit(struct iosm_cdev *port[])
{
	struct iosm_cdev *ipc_port;
	u8 ctrl_chl_nr;

	for (ctrl_chl_nr = 0; ctrl_chl_nr < IPC_MEM_MAX_CHANNELS;
	     ctrl_chl_nr++) {
		if (port[ctrl_chl_nr]) {
			ipc_port = port[ctrl_chl_nr];
			wwan_remove_port(ipc_port->iosm_port);
			kfree(ipc_port);
		}
	}
}
