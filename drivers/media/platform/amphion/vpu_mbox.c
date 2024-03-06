// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "vpu.h"
#include "vpu_mbox.h"
#include "vpu_msgs.h"

static void vpu_mbox_rx_callback(struct mbox_client *cl, void *msg)
{
	struct vpu_mbox *rx = container_of(cl, struct vpu_mbox, cl);
	struct vpu_core *core = container_of(rx, struct vpu_core, rx);

	vpu_isr(core, *(u32 *)msg);
}

static int vpu_mbox_request_channel(struct device *dev, struct vpu_mbox *mbox)
{
	struct mbox_chan *ch;
	struct mbox_client *cl;

	if (!dev || !mbox)
		return -EINVAL;
	if (mbox->ch)
		return 0;

	cl = &mbox->cl;
	cl->dev = dev;
	if (mbox->block) {
		cl->tx_block = true;
		cl->tx_tout = 1000;
	} else {
		cl->tx_block = false;
	}
	cl->knows_txdone = false;
	cl->rx_callback = vpu_mbox_rx_callback;

	ch = mbox_request_channel_byname(cl, mbox->name);
	if (IS_ERR(ch))
		return dev_err_probe(dev, PTR_ERR(ch),
				     "Failed to request mbox chan %s\n",
				     mbox->name);

	mbox->ch = ch;
	return 0;
}

int vpu_mbox_init(struct vpu_core *core)
{
	scnprintf(core->tx_type.name, sizeof(core->tx_type.name) - 1, "tx0");
	core->tx_type.block = true;

	scnprintf(core->tx_data.name, sizeof(core->tx_data.name) - 1, "tx1");
	core->tx_data.block = false;

	scnprintf(core->rx.name, sizeof(core->rx.name) - 1, "rx");
	core->rx.block = true;

	return 0;
}

int vpu_mbox_request(struct vpu_core *core)
{
	int ret;

	ret = vpu_mbox_request_channel(core->dev, &core->tx_type);
	if (ret)
		goto error;
	ret = vpu_mbox_request_channel(core->dev, &core->tx_data);
	if (ret)
		goto error;
	ret = vpu_mbox_request_channel(core->dev, &core->rx);
	if (ret)
		goto error;

	dev_dbg(core->dev, "%s request mbox\n", vpu_core_type_desc(core->type));
	return 0;
error:
	vpu_mbox_free(core);
	return ret;
}

void vpu_mbox_free(struct vpu_core *core)
{
	mbox_free_channel(core->tx_type.ch);
	mbox_free_channel(core->tx_data.ch);
	mbox_free_channel(core->rx.ch);
	core->tx_type.ch = NULL;
	core->tx_data.ch = NULL;
	core->rx.ch = NULL;
	dev_dbg(core->dev, "%s free mbox\n", vpu_core_type_desc(core->type));
}

void vpu_mbox_send_type(struct vpu_core *core, u32 type)
{
	mbox_send_message(core->tx_type.ch, &type);
}

void vpu_mbox_send_msg(struct vpu_core *core, u32 type, u32 data)
{
	mbox_send_message(core->tx_data.ch, &data);
	mbox_send_message(core->tx_type.ch, &type);
}

void vpu_mbox_enable_rx(struct vpu_dev *dev)
{
}
