/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright (C) 2015 EMC Corporation. All Rights Reserved.
 *   Copyright (C) 2016 T-Platforms. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   BSD LICENSE
 *
 *   Copyright (C) 2015 EMC Corporation. All Rights Reserved.
 *   Copyright (C) 2016 T-Platforms. All Rights Reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copy
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * PCIe NTB Linux driver
 *
 * Contact Information:
 * Allen Hubbe <Allen.Hubbe@emc.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/ntb.h>
#include <linux/pci.h>

#define DRIVER_NAME			"ntb"
#define DRIVER_DESCRIPTION		"PCIe NTB Driver Framework"

#define DRIVER_VERSION			"1.0"
#define DRIVER_RELDATE			"24 March 2015"
#define DRIVER_AUTHOR			"Allen Hubbe <Allen.Hubbe@emc.com>"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRIVER_VERSION);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);

static struct bus_type ntb_bus;
static void ntb_dev_release(struct device *dev);

int __ntb_register_client(struct ntb_client *client, struct module *mod,
			  const char *mod_name)
{
	if (!client)
		return -EINVAL;
	if (!ntb_client_ops_is_valid(&client->ops))
		return -EINVAL;

	memset(&client->drv, 0, sizeof(client->drv));
	client->drv.bus = &ntb_bus;
	client->drv.name = mod_name;
	client->drv.owner = mod;

	return driver_register(&client->drv);
}
EXPORT_SYMBOL(__ntb_register_client);

void ntb_unregister_client(struct ntb_client *client)
{
	driver_unregister(&client->drv);
}
EXPORT_SYMBOL(ntb_unregister_client);

int ntb_register_device(struct ntb_dev *ntb)
{
	if (!ntb)
		return -EINVAL;
	if (!ntb->pdev)
		return -EINVAL;
	if (!ntb->ops)
		return -EINVAL;
	if (!ntb_dev_ops_is_valid(ntb->ops))
		return -EINVAL;

	init_completion(&ntb->released);

	ntb->dev.bus = &ntb_bus;
	ntb->dev.parent = &ntb->pdev->dev;
	ntb->dev.release = ntb_dev_release;
	dev_set_name(&ntb->dev, "%s", pci_name(ntb->pdev));

	ntb->ctx = NULL;
	ntb->ctx_ops = NULL;
	spin_lock_init(&ntb->ctx_lock);

	return device_register(&ntb->dev);
}
EXPORT_SYMBOL(ntb_register_device);

void ntb_unregister_device(struct ntb_dev *ntb)
{
	device_unregister(&ntb->dev);
	wait_for_completion(&ntb->released);
}
EXPORT_SYMBOL(ntb_unregister_device);

int ntb_set_ctx(struct ntb_dev *ntb, void *ctx,
		const struct ntb_ctx_ops *ctx_ops)
{
	unsigned long irqflags;

	if (!ntb_ctx_ops_is_valid(ctx_ops))
		return -EINVAL;
	if (ntb->ctx_ops)
		return -EINVAL;

	spin_lock_irqsave(&ntb->ctx_lock, irqflags);
	{
		ntb->ctx = ctx;
		ntb->ctx_ops = ctx_ops;
	}
	spin_unlock_irqrestore(&ntb->ctx_lock, irqflags);

	return 0;
}
EXPORT_SYMBOL(ntb_set_ctx);

void ntb_clear_ctx(struct ntb_dev *ntb)
{
	unsigned long irqflags;

	spin_lock_irqsave(&ntb->ctx_lock, irqflags);
	{
		ntb->ctx_ops = NULL;
		ntb->ctx = NULL;
	}
	spin_unlock_irqrestore(&ntb->ctx_lock, irqflags);
}
EXPORT_SYMBOL(ntb_clear_ctx);

void ntb_link_event(struct ntb_dev *ntb)
{
	unsigned long irqflags;

	spin_lock_irqsave(&ntb->ctx_lock, irqflags);
	{
		if (ntb->ctx_ops && ntb->ctx_ops->link_event)
			ntb->ctx_ops->link_event(ntb->ctx);
	}
	spin_unlock_irqrestore(&ntb->ctx_lock, irqflags);
}
EXPORT_SYMBOL(ntb_link_event);

void ntb_db_event(struct ntb_dev *ntb, int vector)
{
	unsigned long irqflags;

	spin_lock_irqsave(&ntb->ctx_lock, irqflags);
	{
		if (ntb->ctx_ops && ntb->ctx_ops->db_event)
			ntb->ctx_ops->db_event(ntb->ctx, vector);
	}
	spin_unlock_irqrestore(&ntb->ctx_lock, irqflags);
}
EXPORT_SYMBOL(ntb_db_event);

void ntb_msg_event(struct ntb_dev *ntb)
{
	unsigned long irqflags;

	spin_lock_irqsave(&ntb->ctx_lock, irqflags);
	{
		if (ntb->ctx_ops && ntb->ctx_ops->msg_event)
			ntb->ctx_ops->msg_event(ntb->ctx);
	}
	spin_unlock_irqrestore(&ntb->ctx_lock, irqflags);
}
EXPORT_SYMBOL(ntb_msg_event);

int ntb_default_port_number(struct ntb_dev *ntb)
{
	switch (ntb->topo) {
	case NTB_TOPO_PRI:
	case NTB_TOPO_B2B_USD:
		return NTB_PORT_PRI_USD;
	case NTB_TOPO_SEC:
	case NTB_TOPO_B2B_DSD:
		return NTB_PORT_SEC_DSD;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(ntb_default_port_number);

int ntb_default_peer_port_count(struct ntb_dev *ntb)
{
	return NTB_DEF_PEER_CNT;
}
EXPORT_SYMBOL(ntb_default_peer_port_count);

int ntb_default_peer_port_number(struct ntb_dev *ntb, int pidx)
{
	if (pidx != NTB_DEF_PEER_IDX)
		return -EINVAL;

	switch (ntb->topo) {
	case NTB_TOPO_PRI:
	case NTB_TOPO_B2B_USD:
		return NTB_PORT_SEC_DSD;
	case NTB_TOPO_SEC:
	case NTB_TOPO_B2B_DSD:
		return NTB_PORT_PRI_USD;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(ntb_default_peer_port_number);

int ntb_default_peer_port_idx(struct ntb_dev *ntb, int port)
{
	int peer_port = ntb_default_peer_port_number(ntb, NTB_DEF_PEER_IDX);

	if (peer_port == -EINVAL || port != peer_port)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(ntb_default_peer_port_idx);

static int ntb_probe(struct device *dev)
{
	struct ntb_dev *ntb;
	struct ntb_client *client;
	int rc;

	get_device(dev);
	ntb = dev_ntb(dev);
	client = drv_ntb_client(dev->driver);

	rc = client->ops.probe(client, ntb);
	if (rc)
		put_device(dev);

	return rc;
}

static void ntb_remove(struct device *dev)
{
	struct ntb_dev *ntb;
	struct ntb_client *client;

	if (dev->driver) {
		ntb = dev_ntb(dev);
		client = drv_ntb_client(dev->driver);

		client->ops.remove(client, ntb);
		put_device(dev);
	}
}

static void ntb_dev_release(struct device *dev)
{
	struct ntb_dev *ntb = dev_ntb(dev);

	complete(&ntb->released);
}

static struct bus_type ntb_bus = {
	.name = "ntb",
	.probe = ntb_probe,
	.remove = ntb_remove,
};

static int __init ntb_driver_init(void)
{
	return bus_register(&ntb_bus);
}
module_init(ntb_driver_init);

static void __exit ntb_driver_exit(void)
{
	bus_unregister(&ntb_bus);
}
module_exit(ntb_driver_exit);
