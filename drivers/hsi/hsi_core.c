// SPDX-License-Identifier: GPL-2.0-only
/*
 * HSI core.
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 *
 * Contact: Carlos Chinea <carlos.chinea@nokia.com>
 */
#include <linux/hsi/hsi.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include "hsi_core.h"

static ssize_t modalias_show(struct device *dev,
			struct device_attribute *a __maybe_unused, char *buf)
{
	return sprintf(buf, "hsi:%s\n", dev_name(dev));
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *hsi_bus_dev_attrs[] = {
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hsi_bus_dev);

static int hsi_bus_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	add_uevent_var(env, "MODALIAS=hsi:%s", dev_name(dev));

	return 0;
}

static int hsi_bus_match(struct device *dev, struct device_driver *driver)
{
	if (of_driver_match_device(dev, driver))
		return true;

	if (strcmp(dev_name(dev), driver->name) == 0)
		return true;

	return false;
}

static struct bus_type hsi_bus_type = {
	.name		= "hsi",
	.dev_groups	= hsi_bus_dev_groups,
	.match		= hsi_bus_match,
	.uevent		= hsi_bus_uevent,
};

static void hsi_client_release(struct device *dev)
{
	struct hsi_client *cl = to_hsi_client(dev);

	kfree(cl->tx_cfg.channels);
	kfree(cl->rx_cfg.channels);
	kfree(cl);
}

struct hsi_client *hsi_new_client(struct hsi_port *port,
						struct hsi_board_info *info)
{
	struct hsi_client *cl;
	size_t size;

	cl = kzalloc(sizeof(*cl), GFP_KERNEL);
	if (!cl)
		goto err;

	cl->tx_cfg = info->tx_cfg;
	if (cl->tx_cfg.channels) {
		size = cl->tx_cfg.num_channels * sizeof(*cl->tx_cfg.channels);
		cl->tx_cfg.channels = kmemdup(info->tx_cfg.channels, size,
					      GFP_KERNEL);
		if (!cl->tx_cfg.channels)
			goto err_tx;
	}

	cl->rx_cfg = info->rx_cfg;
	if (cl->rx_cfg.channels) {
		size = cl->rx_cfg.num_channels * sizeof(*cl->rx_cfg.channels);
		cl->rx_cfg.channels = kmemdup(info->rx_cfg.channels, size,
					      GFP_KERNEL);
		if (!cl->rx_cfg.channels)
			goto err_rx;
	}

	cl->device.bus = &hsi_bus_type;
	cl->device.parent = &port->device;
	cl->device.release = hsi_client_release;
	dev_set_name(&cl->device, "%s", info->name);
	cl->device.platform_data = info->platform_data;
	if (info->archdata)
		cl->device.archdata = *info->archdata;
	if (device_register(&cl->device) < 0) {
		pr_err("hsi: failed to register client: %s\n", info->name);
		put_device(&cl->device);
		goto err;
	}

	return cl;
err_rx:
	kfree(cl->tx_cfg.channels);
err_tx:
	kfree(cl);
err:
	return NULL;
}
EXPORT_SYMBOL_GPL(hsi_new_client);

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

#ifdef CONFIG_OF
static struct hsi_board_info hsi_char_dev_info = {
	.name = "hsi_char",
};

static int hsi_of_property_parse_mode(struct device_node *client, char *name,
				      unsigned int *result)
{
	const char *mode;
	int err;

	err = of_property_read_string(client, name, &mode);
	if (err < 0)
		return err;

	if (strcmp(mode, "stream") == 0)
		*result = HSI_MODE_STREAM;
	else if (strcmp(mode, "frame") == 0)
		*result = HSI_MODE_FRAME;
	else
		return -EINVAL;

	return 0;
}

static int hsi_of_property_parse_flow(struct device_node *client, char *name,
				      unsigned int *result)
{
	const char *flow;
	int err;

	err = of_property_read_string(client, name, &flow);
	if (err < 0)
		return err;

	if (strcmp(flow, "synchronized") == 0)
		*result = HSI_FLOW_SYNC;
	else if (strcmp(flow, "pipeline") == 0)
		*result = HSI_FLOW_PIPE;
	else
		return -EINVAL;

	return 0;
}

static int hsi_of_property_parse_arb_mode(struct device_node *client,
					  char *name, unsigned int *result)
{
	const char *arb_mode;
	int err;

	err = of_property_read_string(client, name, &arb_mode);
	if (err < 0)
		return err;

	if (strcmp(arb_mode, "round-robin") == 0)
		*result = HSI_ARB_RR;
	else if (strcmp(arb_mode, "priority") == 0)
		*result = HSI_ARB_PRIO;
	else
		return -EINVAL;

	return 0;
}

static void hsi_add_client_from_dt(struct hsi_port *port,
						struct device_node *client)
{
	struct hsi_client *cl;
	struct hsi_channel channel;
	struct property *prop;
	char name[32];
	int length, cells, err, i, max_chan, mode;

	cl = kzalloc(sizeof(*cl), GFP_KERNEL);
	if (!cl)
		return;

	err = of_modalias_node(client, name, sizeof(name));
	if (err)
		goto err;

	err = hsi_of_property_parse_mode(client, "hsi-mode", &mode);
	if (err) {
		err = hsi_of_property_parse_mode(client, "hsi-rx-mode",
						 &cl->rx_cfg.mode);
		if (err)
			goto err;

		err = hsi_of_property_parse_mode(client, "hsi-tx-mode",
						 &cl->tx_cfg.mode);
		if (err)
			goto err;
	} else {
		cl->rx_cfg.mode = mode;
		cl->tx_cfg.mode = mode;
	}

	err = of_property_read_u32(client, "hsi-speed-kbps",
				   &cl->tx_cfg.speed);
	if (err)
		goto err;
	cl->rx_cfg.speed = cl->tx_cfg.speed;

	err = hsi_of_property_parse_flow(client, "hsi-flow",
					 &cl->rx_cfg.flow);
	if (err)
		goto err;

	err = hsi_of_property_parse_arb_mode(client, "hsi-arb-mode",
					     &cl->rx_cfg.arb_mode);
	if (err)
		goto err;

	prop = of_find_property(client, "hsi-channel-ids", &length);
	if (!prop) {
		err = -EINVAL;
		goto err;
	}

	cells = length / sizeof(u32);

	cl->rx_cfg.num_channels = cells;
	cl->tx_cfg.num_channels = cells;
	cl->rx_cfg.channels = kcalloc(cells, sizeof(channel), GFP_KERNEL);
	if (!cl->rx_cfg.channels) {
		err = -ENOMEM;
		goto err;
	}

	cl->tx_cfg.channels = kcalloc(cells, sizeof(channel), GFP_KERNEL);
	if (!cl->tx_cfg.channels) {
		err = -ENOMEM;
		goto err2;
	}

	max_chan = 0;
	for (i = 0; i < cells; i++) {
		err = of_property_read_u32_index(client, "hsi-channel-ids", i,
						 &channel.id);
		if (err)
			goto err3;

		err = of_property_read_string_index(client, "hsi-channel-names",
						    i, &channel.name);
		if (err)
			channel.name = NULL;

		if (channel.id > max_chan)
			max_chan = channel.id;

		cl->rx_cfg.channels[i] = channel;
		cl->tx_cfg.channels[i] = channel;
	}

	cl->rx_cfg.num_hw_channels = max_chan + 1;
	cl->tx_cfg.num_hw_channels = max_chan + 1;

	cl->device.bus = &hsi_bus_type;
	cl->device.parent = &port->device;
	cl->device.release = hsi_client_release;
	cl->device.of_node = client;

	dev_set_name(&cl->device, "%s", name);
	if (device_register(&cl->device) < 0) {
		pr_err("hsi: failed to register client: %s\n", name);
		put_device(&cl->device);
	}

	return;

err3:
	kfree(cl->tx_cfg.channels);
err2:
	kfree(cl->rx_cfg.channels);
err:
	kfree(cl);
	pr_err("hsi client: missing or incorrect of property: err=%d\n", err);
}

void hsi_add_clients_from_dt(struct hsi_port *port, struct device_node *clients)
{
	struct device_node *child;

	/* register hsi-char device */
	hsi_new_client(port, &hsi_char_dev_info);

	for_each_available_child_of_node(clients, child)
		hsi_add_client_from_dt(port, child);
}
EXPORT_SYMBOL_GPL(hsi_add_clients_from_dt);
#endif

int hsi_remove_client(struct device *dev, void *data __maybe_unused)
{
	device_unregister(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(hsi_remove_client);

static int hsi_remove_port(struct device *dev, void *data __maybe_unused)
{
	device_for_each_child(dev, NULL, hsi_remove_client);
	device_unregister(dev);

	return 0;
}

static void hsi_controller_release(struct device *dev)
{
	struct hsi_controller *hsi = to_hsi_controller(dev);

	kfree(hsi->port);
	kfree(hsi);
}

static void hsi_port_release(struct device *dev)
{
	kfree(to_hsi_port(dev));
}

/**
 * hsi_port_unregister_clients - Unregister an HSI port
 * @port: The HSI port to unregister
 */
void hsi_port_unregister_clients(struct hsi_port *port)
{
	device_for_each_child(&port->device, NULL, hsi_remove_client);
}
EXPORT_SYMBOL_GPL(hsi_port_unregister_clients);

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

	err = device_add(&hsi->device);
	if (err < 0)
		return err;
	for (i = 0; i < hsi->num_ports; i++) {
		hsi->port[i]->device.parent = &hsi->device;
		err = device_add(&hsi->port[i]->device);
		if (err < 0)
			goto out;
	}
	/* Populate HSI bus with HSI clients */
	hsi_scan_board_info(hsi);

	return 0;
out:
	while (i-- > 0)
		device_del(&hsi->port[i]->device);
	device_del(&hsi->device);

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
 * hsi_put_controller - Free an HSI controller
 *
 * @hsi: Pointer to the HSI controller to freed
 *
 * HSI controller drivers should only use this function if they need
 * to free their allocated hsi_controller structures before a successful
 * call to hsi_register_controller. Other use is not allowed.
 */
void hsi_put_controller(struct hsi_controller *hsi)
{
	unsigned int i;

	if (!hsi)
		return;

	for (i = 0; i < hsi->num_ports; i++)
		if (hsi->port && hsi->port[i])
			put_device(&hsi->port[i]->device);
	put_device(&hsi->device);
}
EXPORT_SYMBOL_GPL(hsi_put_controller);

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
	struct hsi_port		**port;
	unsigned int		i;

	if (!n_ports)
		return NULL;

	hsi = kzalloc(sizeof(*hsi), flags);
	if (!hsi)
		return NULL;
	port = kcalloc(n_ports, sizeof(*port), flags);
	if (!port) {
		kfree(hsi);
		return NULL;
	}
	hsi->num_ports = n_ports;
	hsi->port = port;
	hsi->device.release = hsi_controller_release;
	device_initialize(&hsi->device);

	for (i = 0; i < n_ports; i++) {
		port[i] = kzalloc(sizeof(**port), flags);
		if (port[i] == NULL)
			goto out;
		port[i]->num = i;
		port[i]->async = hsi_dummy_msg;
		port[i]->setup = hsi_dummy_cl;
		port[i]->flush = hsi_dummy_cl;
		port[i]->start_tx = hsi_dummy_cl;
		port[i]->stop_tx = hsi_dummy_cl;
		port[i]->release = hsi_dummy_cl;
		mutex_init(&port[i]->lock);
		BLOCKING_INIT_NOTIFIER_HEAD(&port[i]->n_head);
		dev_set_name(&port[i]->device, "port%d", i);
		hsi->port[i]->device.release = hsi_port_release;
		device_initialize(&hsi->port[i]->device);
	}

	return hsi;
out:
	hsi_put_controller(hsi);

	return NULL;
}
EXPORT_SYMBOL_GPL(hsi_alloc_controller);

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

static int hsi_event_notifier_call(struct notifier_block *nb,
				unsigned long event, void *data __maybe_unused)
{
	struct hsi_client *cl = container_of(nb, struct hsi_client, nb);

	(*cl->ehandler)(cl, event);

	return 0;
}

/**
 * hsi_register_port_event - Register a client to receive port events
 * @cl: HSI client that wants to receive port events
 * @handler: Event handler callback
 *
 * Clients should register a callback to be able to receive
 * events from the ports. Registration should happen after
 * claiming the port.
 * The handler can be called in interrupt context.
 *
 * Returns -errno on error, or 0 on success.
 */
int hsi_register_port_event(struct hsi_client *cl,
			void (*handler)(struct hsi_client *, unsigned long))
{
	struct hsi_port *port = hsi_get_port(cl);

	if (!handler || cl->ehandler)
		return -EINVAL;
	if (!hsi_port_claimed(cl))
		return -EACCES;
	cl->ehandler = handler;
	cl->nb.notifier_call = hsi_event_notifier_call;

	return blocking_notifier_chain_register(&port->n_head, &cl->nb);
}
EXPORT_SYMBOL_GPL(hsi_register_port_event);

/**
 * hsi_unregister_port_event - Stop receiving port events for a client
 * @cl: HSI client that wants to stop receiving port events
 *
 * Clients should call this function before releasing their associated
 * port.
 *
 * Returns -errno on error, or 0 on success.
 */
int hsi_unregister_port_event(struct hsi_client *cl)
{
	struct hsi_port *port = hsi_get_port(cl);
	int err;

	WARN_ON(!hsi_port_claimed(cl));

	err = blocking_notifier_chain_unregister(&port->n_head, &cl->nb);
	if (!err)
		cl->ehandler = NULL;

	return err;
}
EXPORT_SYMBOL_GPL(hsi_unregister_port_event);

/**
 * hsi_event - Notifies clients about port events
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
 *
 * Returns -errno on error, or 0 on success.
 */
int hsi_event(struct hsi_port *port, unsigned long event)
{
	return blocking_notifier_call_chain(&port->n_head, event, NULL);
}
EXPORT_SYMBOL_GPL(hsi_event);

/**
 * hsi_get_channel_id_by_name - acquire channel id by channel name
 * @cl: HSI client, which uses the channel
 * @name: name the channel is known under
 *
 * Clients can call this function to get the hsi channel ids similar to
 * requesting IRQs or GPIOs by name. This function assumes the same
 * channel configuration is used for RX and TX.
 *
 * Returns -errno on error or channel id on success.
 */
int hsi_get_channel_id_by_name(struct hsi_client *cl, char *name)
{
	int i;

	if (!cl->rx_cfg.channels)
		return -ENOENT;

	for (i = 0; i < cl->rx_cfg.num_channels; i++)
		if (!strcmp(cl->rx_cfg.channels[i].name, name))
			return cl->rx_cfg.channels[i].id;

	return -ENXIO;
}
EXPORT_SYMBOL_GPL(hsi_get_channel_id_by_name);

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
