/*
 * HSI core.
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 *
 * Contact: Carlos Chinea <carlos.chinea@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <linux/hsi/hsi.h>
#include <linux/compiler.h>
#include <linux/rwsem.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "hsi_core.h"

static struct device_type hsi_ctrl = {
	.name	= "hsi_controller",
};

static struct device_type hsi_cl = {
	.name	= "hsi_client",
};

static struct device_type hsi_port = {
	.name	= "hsi_port",
};

static ssize_t modalias_show(struct device *dev,
			struct device_attribute *a __maybe_unused, char *buf)
{
	return sprintf(buf, "hsi:%s\n", dev_name(dev));
}

static struct device_attribute hsi_bus_dev_attrs[] = {
	__ATTR_RO(modalias),
	__ATTR_NULL,
};

static int hsi_bus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	if (dev->type == &hsi_cl)
		add_uevent_var(env, "MODALIAS=hsi:%s", dev_name(dev));

	return 0;
}

static int hsi_bus_match(struct device *dev, struct device_driver *driver)
{
	return strcmp(dev_name(dev), driver->name) == 0;
}

static struct bus_type hsi_bus_type = {
	.name		= "hsi",
	.dev_attrs	= hsi_bus_dev_attrs,
	.match		= hsi_bus_match,
	.uevent		= hsi_bus_uevent,
};

static void hsi_client_release(struct device *dev)
{
	kfree(to_hsi_client(dev));
}

static void hsi_new_client(struct hsi_port *port, struct hsi_board_info *info)
{
	struct hsi_client *cl;
	unsigned long flags;

	cl = kzalloc(sizeof(*cl), GFP_KERNEL);
	if (!cl)
		return;
	cl->device.type = &hsi_cl;
	cl->tx_cfg = info->tx_cfg;
	cl->rx_cfg = info->rx_cfg;
	cl->device.bus = &hsi_bus_type;
	cl->device.parent = &port->device;
	cl->device.release = hsi_client_release;
	dev_set_name(&cl->device, info->name);
	cl->device.platform_data = info->platform_data;
	spin_lock_irqsave(&port->clock, flags);
	list_add_tail(&cl->link, &port->clients);
	spin_unlock_irqrestore(&port->clock, flags);
	if (info->archdata)
		cl->device.archdata = *info->archdata;
	if (device_register(&cl->device) < 0) {
		pr_err("hsi: failed to register client: %s\n", info->name);
		kfree(cl);
	}
}

static void hsi_scan_board_info(struct hsi_controller *hsi)
{
	struct hsi_cl_info *cl_info;
	struct hsi_port	*p;

	list_for_each_entry(cl_info, &hsi_board_list, list)
		if (cl_info->info.hsi_id == hsi->id) {
			p = hsi_find_port_num(hsi, cl_info->info.port);
			if (!p)
				continue;
			hsi_new_client(p, &cl_info->info);
		}
}

static int hsi_remove_client(struct device *dev, void *data __maybe_unused)
{
	struct hsi_client *cl = to_hsi_client(dev);
	struct hsi_port *port = to_hsi_port(dev->parent);
	unsigned long flags;

	spin_lock_irqsave(&port->clock, flags);
	list_del(&cl->link);
	spin_unlock_irqrestore(&port->clock, flags);
	device_unregister(dev);

	return 0;
}

static int hsi_remove_port(struct device *dev, void *data __maybe_unused)
{
	device_for_each_child(dev, NULL, hsi_remove_client);
	device_unregister(dev);

	return 0;
}

static void hsi_controller_release(struct device *dev __maybe_unused)
{
}

static void hsi_port_release(struct device *dev __maybe_unused)
{
}

/**
 * hsi_unregister_controller - Unregister an HSI controller
 * @hsi: The HSI controller to register
 */
void hsi_unregister_controller(struct hsi_controller *hsi)
{
	device_for_each_child(&hsi->device, NULL, hsi_remove_port);
	device_unregister(&hsi->device);
}
EXPORT_SYMBOL_GPL(hsi_unregister_controller);

/**
 * hsi_register_controller - Register an HSI controller and its ports
 * @hsi: The HSI controller to register
 *
 * Returns -errno on failure, 0 on success.
 */
int hsi_register_controller(struct hsi_controller *hsi)
{
	unsigned int i;
	int err;

	hsi->device.type = &hsi_ctrl;
	hsi->device.bus = &hsi_bus_type;
	hsi->device.release = hsi_controller_release;
	err = device_register(&hsi->device);
	if (err < 0)
		return err;
	for (i = 0; i < hsi->num_ports; i++) {
		hsi->port[i].device.parent = &hsi->device;
		hsi->port[i].device.bus = &hsi_bus_type;
		hsi->port[i].device.release = hsi_port_release;
		hsi->port[i].device.type = &hsi_port;
		INIT_LIST_HEAD(&hsi->port[i].clients);
		spin_lock_init(&hsi->port[i].clock);
		err = device_register(&hsi->port[i].device);
		if (err < 0)
			goto out;
	}
	/* Populate HSI bus with HSI clients */
	hsi_scan_board_info(hsi);

	return 0;
out:
	hsi_unregister_controller(hsi);

	return err;
}
EXPORT_SYMBOL_GPL(hsi_register_controller);

/**
 * hsi_register_client_driver - Register an HSI client to the HSI bus
 * @drv: HSI client driver to register
 *
 * Returns -errno on failure, 0 on success.
 */
int hsi_register_client_driver(struct hsi_client_driver *drv)
{
	drv->driver.bus = &hsi_bus_type;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(hsi_register_client_driver);

static inline int hsi_dummy_msg(struct hsi_msg *msg __maybe_unused)
{
	return 0;
}

static inline int hsi_dummy_cl(struct hsi_client *cl __maybe_unused)
{
	return 0;
}

/**
 * hsi_alloc_controller - Allocate an HSI controller and its ports
 * @n_ports: Number of ports on the HSI controller
 * @flags: Kernel allocation flags
 *
 * Return NULL on failure or a pointer to an hsi_controller on success.
 */
struct hsi_controller *hsi_alloc_controller(unsigned int n_ports, gfp_t flags)
{
	struct hsi_controller	*hsi;
	struct hsi_port		*port;
	unsigned int		i;

	if (!n_ports)
		return NULL;

	port = kzalloc(sizeof(*port)*n_ports, flags);
	if (!port)
		return NULL;
	hsi = kzalloc(sizeof(*hsi), flags);
	if (!hsi)
		goto out;
	for (i = 0; i < n_ports; i++) {
		dev_set_name(&port[i].device, "port%d", i);
		port[i].num = i;
		port[i].async = hsi_dummy_msg;
		port[i].setup = hsi_dummy_cl;
		port[i].flush = hsi_dummy_cl;
		port[i].start_tx = hsi_dummy_cl;
		port[i].stop_tx = hsi_dummy_cl;
		port[i].release = hsi_dummy_cl;
		mutex_init(&port[i].lock);
	}
	hsi->num_ports = n_ports;
	hsi->port = port;

	return hsi;
out:
	kfree(port);

	return NULL;
}
EXPORT_SYMBOL_GPL(hsi_alloc_controller);

/**
 * hsi_free_controller - Free an HSI controller
 * @hsi: Pointer to HSI controller
 */
void hsi_free_controller(struct hsi_controller *hsi)
{
	if (!hsi)
		return;

	kfree(hsi->port);
	kfree(hsi);
}
EXPORT_SYMBOL_GPL(hsi_free_controller);

/**
 * hsi_free_msg - Free an HSI message
 * @msg: Pointer to the HSI message
 *
 * Client is responsible to free the buffers pointed by the scatterlists.
 */
void hsi_free_msg(struct hsi_msg *msg)
{
	if (!msg)
		return;
	sg_free_table(&msg->sgt);
	kfree(msg);
}
EXPORT_SYMBOL_GPL(hsi_free_msg);

/**
 * hsi_alloc_msg - Allocate an HSI message
 * @nents: Number of memory entries
 * @flags: Kernel allocation flags
 *
 * nents can be 0. This mainly makes sense for read transfer.
 * In that case, HSI drivers will call the complete callback when
 * there is data to be read without consuming it.
 *
 * Return NULL on failure or a pointer to an hsi_msg on success.
 */
struct hsi_msg *hsi_alloc_msg(unsigned int nents, gfp_t flags)
{
	struct hsi_msg *msg;
	int err;

	msg = kzalloc(sizeof(*msg), flags);
	if (!msg)
		return NULL;

	if (!nents)
		return msg;

	err = sg_alloc_table(&msg->sgt, nents, flags);
	if (unlikely(err)) {
		kfree(msg);
		msg = NULL;
	}

	return msg;
}
EXPORT_SYMBOL_GPL(hsi_alloc_msg);

/**
 * hsi_async - Submit an HSI transfer to the controller
 * @cl: HSI client sending the transfer
 * @msg: The HSI transfer passed to controller
 *
 * The HSI message must have the channel, ttype, complete and destructor
 * fields set beforehand. If nents > 0 then the client has to initialize
 * also the scatterlists to point to the buffers to write to or read from.
 *
 * HSI controllers relay on pre-allocated buffers from their clients and they
 * do not allocate buffers on their own.
 *
 * Once the HSI message transfer finishes, the HSI controller calls the
 * complete callback with the status and actual_len fields of the HSI message
 * updated. The complete callback can be called before returning from
 * hsi_async.
 *
 * Returns -errno on failure or 0 on success
 */
int hsi_async(struct hsi_client *cl, struct hsi_msg *msg)
{
	struct hsi_port *port = hsi_get_port(cl);

	if (!hsi_port_claimed(cl))
		return -EACCES;

	WARN_ON_ONCE(!msg->destructor || !msg->complete);
	msg->cl = cl;

	return port->async(msg);
}
EXPORT_SYMBOL_GPL(hsi_async);

/**
 * hsi_claim_port - Claim the HSI client's port
 * @cl: HSI client that wants to claim its port
 * @share: Flag to indicate if the client wants to share the port or not.
 *
 * Returns -errno on failure, 0 on success.
 */
int hsi_claim_port(struct hsi_client *cl, unsigned int share)
{
	struct hsi_port *port = hsi_get_port(cl);
	int err = 0;

	mutex_lock(&port->lock);
	if ((port->claimed) && (!port->shared || !share)) {
		err = -EBUSY;
		goto out;
	}
	if (!try_module_get(to_hsi_controller(port->device.parent)->owner)) {
		err = -ENODEV;
		goto out;
	}
	port->claimed++;
	port->shared = !!share;
	cl->pclaimed = 1;
out:
	mutex_unlock(&port->lock);

	return err;
}
EXPORT_SYMBOL_GPL(hsi_claim_port);

/**
 * hsi_release_port - Release the HSI client's port
 * @cl: HSI client which previously claimed its port
 */
void hsi_release_port(struct hsi_client *cl)
{
	struct hsi_port *port = hsi_get_port(cl);

	mutex_lock(&port->lock);
	/* Allow HW driver to do some cleanup */
	port->release(cl);
	if (cl->pclaimed)
		port->claimed--;
	BUG_ON(port->claimed < 0);
	cl->pclaimed = 0;
	if (!port->claimed)
		port->shared = 0;
	module_put(to_hsi_controller(port->device.parent)->owner);
	mutex_unlock(&port->lock);
}
EXPORT_SYMBOL_GPL(hsi_release_port);

static int hsi_start_rx(struct hsi_client *cl, void *data __maybe_unused)
{
	if (cl->hsi_start_rx)
		(*cl->hsi_start_rx)(cl);

	return 0;
}

static int hsi_stop_rx(struct hsi_client *cl, void *data __maybe_unused)
{
	if (cl->hsi_stop_rx)
		(*cl->hsi_stop_rx)(cl);

	return 0;
}

static int hsi_port_for_each_client(struct hsi_port *port, void *data,
				int (*fn)(struct hsi_client *cl, void *data))
{
	struct hsi_client *cl;

	spin_lock(&port->clock);
	list_for_each_entry(cl, &port->clients, link) {
		spin_unlock(&port->clock);
		(*fn)(cl, data);
		spin_lock(&port->clock);
	}
	spin_unlock(&port->clock);

	return 0;
}

/**
 * hsi_event -Notifies clients about port events
 * @port: Port where the event occurred
 * @event: The event type
 *
 * Clients should not be concerned about wake line behavior. However, due
 * to a race condition in HSI HW protocol, clients need to be notified
 * about wake line changes, so they can implement a workaround for it.
 *
 * Events:
 * HSI_EVENT_START_RX - Incoming wake line high
 * HSI_EVENT_STOP_RX - Incoming wake line down
 */
void hsi_event(struct hsi_port *port, unsigned int event)
{
	int (*fn)(struct hsi_client *cl, void *data);

	switch (event) {
	case HSI_EVENT_START_RX:
		fn = hsi_start_rx;
		break;
	case HSI_EVENT_STOP_RX:
		fn = hsi_stop_rx;
		break;
	default:
		return;
	}
	hsi_port_for_each_client(port, NULL, fn);
}
EXPORT_SYMBOL_GPL(hsi_event);

static int __init hsi_init(void)
{
	return bus_register(&hsi_bus_type);
}
postcore_initcall(hsi_init);

static void __exit hsi_exit(void)
{
	bus_unregister(&hsi_bus_type);
}
module_exit(hsi_exit);

MODULE_AUTHOR("Carlos Chinea <carlos.chinea@nokia.com>");
MODULE_DESCRIPTION("High-speed Synchronous Serial Interface (HSI) framework");
MODULE_LICENSE("GPL v2");
