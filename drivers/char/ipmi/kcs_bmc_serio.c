// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (c) 2021 IBM Corp. */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/serio.h>
#include <linux/slab.h>

#include "kcs_bmc_client.h"

struct kcs_bmc_serio {
	struct list_head entry;

	struct kcs_bmc_client client;
	struct serio *port;

	spinlock_t lock;
};

static inline struct kcs_bmc_serio *client_to_kcs_bmc_serio(struct kcs_bmc_client *client)
{
	return container_of(client, struct kcs_bmc_serio, client);
}

static irqreturn_t kcs_bmc_serio_event(struct kcs_bmc_client *client)
{
	struct kcs_bmc_serio *priv;
	u8 handled = IRQ_NONE;
	u8 status;

	priv = client_to_kcs_bmc_serio(client);

	spin_lock(&priv->lock);

	status = kcs_bmc_read_status(client->dev);

	if (status & KCS_BMC_STR_IBF)
		handled = serio_interrupt(priv->port, kcs_bmc_read_data(client->dev), 0);

	spin_unlock(&priv->lock);

	return handled;
}

static const struct kcs_bmc_client_ops kcs_bmc_serio_client_ops = {
	.event = kcs_bmc_serio_event,
};

static int kcs_bmc_serio_open(struct serio *port)
{
	struct kcs_bmc_serio *priv = port->port_data;

	return kcs_bmc_enable_device(priv->client.dev, &priv->client);
}

static void kcs_bmc_serio_close(struct serio *port)
{
	struct kcs_bmc_serio *priv = port->port_data;

	kcs_bmc_disable_device(priv->client.dev, &priv->client);
}

static DEFINE_SPINLOCK(kcs_bmc_serio_instances_lock);
static LIST_HEAD(kcs_bmc_serio_instances);

static int kcs_bmc_serio_add_device(struct kcs_bmc_device *kcs_bmc)
{
	struct kcs_bmc_serio *priv;
	struct serio *port;

	priv = devm_kzalloc(kcs_bmc->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Use kzalloc() as the allocation is cleaned up with kfree() via serio_unregister_port() */
	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->id.type = SERIO_8042;
	port->open = kcs_bmc_serio_open;
	port->close = kcs_bmc_serio_close;
	port->port_data = priv;
	port->dev.parent = kcs_bmc->dev;

	spin_lock_init(&priv->lock);
	priv->port = port;
	priv->client.dev = kcs_bmc;
	priv->client.ops = &kcs_bmc_serio_client_ops;

	spin_lock_irq(&kcs_bmc_serio_instances_lock);
	list_add(&priv->entry, &kcs_bmc_serio_instances);
	spin_unlock_irq(&kcs_bmc_serio_instances_lock);

	serio_register_port(port);

	dev_info(kcs_bmc->dev, "Initialised serio client for channel %d", kcs_bmc->channel);

	return 0;
}

static int kcs_bmc_serio_remove_device(struct kcs_bmc_device *kcs_bmc)
{
	struct kcs_bmc_serio *priv = NULL, *pos;

	spin_lock_irq(&kcs_bmc_serio_instances_lock);
	list_for_each_entry(pos, &kcs_bmc_serio_instances, entry) {
		if (pos->client.dev == kcs_bmc) {
			priv = pos;
			list_del(&pos->entry);
			break;
		}
	}
	spin_unlock_irq(&kcs_bmc_serio_instances_lock);

	if (!priv)
		return -ENODEV;

	/* kfree()s priv->port via put_device() */
	serio_unregister_port(priv->port);

	/* Ensure the IBF IRQ is disabled if we were the active client */
	kcs_bmc_disable_device(kcs_bmc, &priv->client);

	devm_kfree(priv->client.dev->dev, priv);

	return 0;
}

static const struct kcs_bmc_driver_ops kcs_bmc_serio_driver_ops = {
	.add_device = kcs_bmc_serio_add_device,
	.remove_device = kcs_bmc_serio_remove_device,
};

static struct kcs_bmc_driver kcs_bmc_serio_driver = {
	.ops = &kcs_bmc_serio_driver_ops,
};

static int kcs_bmc_serio_init(void)
{
	kcs_bmc_register_driver(&kcs_bmc_serio_driver);

	return 0;
}
module_init(kcs_bmc_serio_init);

static void kcs_bmc_serio_exit(void)
{
	kcs_bmc_unregister_driver(&kcs_bmc_serio_driver);
}
module_exit(kcs_bmc_serio_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Andrew Jeffery <andrew@aj.id.au>");
MODULE_DESCRIPTION("Adapter driver for serio access to BMC KCS devices");
