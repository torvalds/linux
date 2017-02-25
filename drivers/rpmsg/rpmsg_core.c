/*
 * remote processor messaging bus
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 * Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/of_device.h>
#include <linux/slab.h>

#include "rpmsg_internal.h"

/**
 * rpmsg_create_ept() - create a new rpmsg_endpoint
 * @rpdev: rpmsg channel device
 * @cb: rx callback handler
 * @priv: private data for the driver's use
 * @chinfo: channel_info with the local rpmsg address to bind with @cb
 *
 * Every rpmsg address in the system is bound to an rx callback (so when
 * inbound messages arrive, they are dispatched by the rpmsg bus using the
 * appropriate callback handler) by means of an rpmsg_endpoint struct.
 *
 * This function allows drivers to create such an endpoint, and by that,
 * bind a callback, and possibly some private data too, to an rpmsg address
 * (either one that is known in advance, or one that will be dynamically
 * assigned for them).
 *
 * Simple rpmsg drivers need not call rpmsg_create_ept, because an endpoint
 * is already created for them when they are probed by the rpmsg bus
 * (using the rx callback provided when they registered to the rpmsg bus).
 *
 * So things should just work for simple drivers: they already have an
 * endpoint, their rx callback is bound to their rpmsg address, and when
 * relevant inbound messages arrive (i.e. messages which their dst address
 * equals to the src address of their rpmsg channel), the driver's handler
 * is invoked to process it.
 *
 * That said, more complicated drivers might do need to allocate
 * additional rpmsg addresses, and bind them to different rx callbacks.
 * To accomplish that, those drivers need to call this function.
 *
 * Drivers should provide their @rpdev channel (so the new endpoint would belong
 * to the same remote processor their channel belongs to), an rx callback
 * function, an optional private data (which is provided back when the
 * rx callback is invoked), and an address they want to bind with the
 * callback. If @addr is RPMSG_ADDR_ANY, then rpmsg_create_ept will
 * dynamically assign them an available rpmsg address (drivers should have
 * a very good reason why not to always use RPMSG_ADDR_ANY here).
 *
 * Returns a pointer to the endpoint on success, or NULL on error.
 */
struct rpmsg_endpoint *rpmsg_create_ept(struct rpmsg_device *rpdev,
					rpmsg_rx_cb_t cb, void *priv,
					struct rpmsg_channel_info chinfo)
{
	if (WARN_ON(!rpdev))
		return ERR_PTR(-EINVAL);

	return rpdev->ops->create_ept(rpdev, cb, priv, chinfo);
}
EXPORT_SYMBOL(rpmsg_create_ept);

/**
 * rpmsg_destroy_ept() - destroy an existing rpmsg endpoint
 * @ept: endpoing to destroy
 *
 * Should be used by drivers to destroy an rpmsg endpoint previously
 * created with rpmsg_create_ept(). As with other types of "free" NULL
 * is a valid parameter.
 */
void rpmsg_destroy_ept(struct rpmsg_endpoint *ept)
{
	if (ept)
		ept->ops->destroy_ept(ept);
}
EXPORT_SYMBOL(rpmsg_destroy_ept);

/**
 * rpmsg_send() - send a message across to the remote processor
 * @ept: the rpmsg endpoint
 * @data: payload of message
 * @len: length of payload
 *
 * This function sends @data of length @len on the @ept endpoint.
 * The message will be sent to the remote processor which the @ept
 * endpoint belongs to, using @ept's address and its associated rpmsg
 * device destination addresses.
 * In case there are no TX buffers available, the function will block until
 * one becomes available, or a timeout of 15 seconds elapses. When the latter
 * happens, -ERESTARTSYS is returned.
 *
 * Can only be called from process context (for now).
 *
 * Returns 0 on success and an appropriate error value on failure.
 */
int rpmsg_send(struct rpmsg_endpoint *ept, void *data, int len)
{
	if (WARN_ON(!ept))
		return -EINVAL;
	if (!ept->ops->send)
		return -ENXIO;

	return ept->ops->send(ept, data, len);
}
EXPORT_SYMBOL(rpmsg_send);

/**
 * rpmsg_sendto() - send a message across to the remote processor, specify dst
 * @ept: the rpmsg endpoint
 * @data: payload of message
 * @len: length of payload
 * @dst: destination address
 *
 * This function sends @data of length @len to the remote @dst address.
 * The message will be sent to the remote processor which the @ept
 * endpoint belongs to, using @ept's address as source.
 * In case there are no TX buffers available, the function will block until
 * one becomes available, or a timeout of 15 seconds elapses. When the latter
 * happens, -ERESTARTSYS is returned.
 *
 * Can only be called from process context (for now).
 *
 * Returns 0 on success and an appropriate error value on failure.
 */
int rpmsg_sendto(struct rpmsg_endpoint *ept, void *data, int len, u32 dst)
{
	if (WARN_ON(!ept))
		return -EINVAL;
	if (!ept->ops->sendto)
		return -ENXIO;

	return ept->ops->sendto(ept, data, len, dst);
}
EXPORT_SYMBOL(rpmsg_sendto);

/**
 * rpmsg_send_offchannel() - send a message using explicit src/dst addresses
 * @ept: the rpmsg endpoint
 * @src: source address
 * @dst: destination address
 * @data: payload of message
 * @len: length of payload
 *
 * This function sends @data of length @len to the remote @dst address,
 * and uses @src as the source address.
 * The message will be sent to the remote processor which the @ept
 * endpoint belongs to.
 * In case there are no TX buffers available, the function will block until
 * one becomes available, or a timeout of 15 seconds elapses. When the latter
 * happens, -ERESTARTSYS is returned.
 *
 * Can only be called from process context (for now).
 *
 * Returns 0 on success and an appropriate error value on failure.
 */
int rpmsg_send_offchannel(struct rpmsg_endpoint *ept, u32 src, u32 dst,
			  void *data, int len)
{
	if (WARN_ON(!ept))
		return -EINVAL;
	if (!ept->ops->send_offchannel)
		return -ENXIO;

	return ept->ops->send_offchannel(ept, src, dst, data, len);
}
EXPORT_SYMBOL(rpmsg_send_offchannel);

/**
 * rpmsg_send() - send a message across to the remote processor
 * @ept: the rpmsg endpoint
 * @data: payload of message
 * @len: length of payload
 *
 * This function sends @data of length @len on the @ept endpoint.
 * The message will be sent to the remote processor which the @ept
 * endpoint belongs to, using @ept's address as source and its associated
 * rpdev's address as destination.
 * In case there are no TX buffers available, the function will immediately
 * return -ENOMEM without waiting until one becomes available.
 *
 * Can only be called from process context (for now).
 *
 * Returns 0 on success and an appropriate error value on failure.
 */
int rpmsg_trysend(struct rpmsg_endpoint *ept, void *data, int len)
{
	if (WARN_ON(!ept))
		return -EINVAL;
	if (!ept->ops->trysend)
		return -ENXIO;

	return ept->ops->trysend(ept, data, len);
}
EXPORT_SYMBOL(rpmsg_trysend);

/**
 * rpmsg_sendto() - send a message across to the remote processor, specify dst
 * @ept: the rpmsg endpoint
 * @data: payload of message
 * @len: length of payload
 * @dst: destination address
 *
 * This function sends @data of length @len to the remote @dst address.
 * The message will be sent to the remote processor which the @ept
 * endpoint belongs to, using @ept's address as source.
 * In case there are no TX buffers available, the function will immediately
 * return -ENOMEM without waiting until one becomes available.
 *
 * Can only be called from process context (for now).
 *
 * Returns 0 on success and an appropriate error value on failure.
 */
int rpmsg_trysendto(struct rpmsg_endpoint *ept, void *data, int len, u32 dst)
{
	if (WARN_ON(!ept))
		return -EINVAL;
	if (!ept->ops->trysendto)
		return -ENXIO;

	return ept->ops->trysendto(ept, data, len, dst);
}
EXPORT_SYMBOL(rpmsg_trysendto);

/**
 * rpmsg_send_offchannel() - send a message using explicit src/dst addresses
 * @ept: the rpmsg endpoint
 * @src: source address
 * @dst: destination address
 * @data: payload of message
 * @len: length of payload
 *
 * This function sends @data of length @len to the remote @dst address,
 * and uses @src as the source address.
 * The message will be sent to the remote processor which the @ept
 * endpoint belongs to.
 * In case there are no TX buffers available, the function will immediately
 * return -ENOMEM without waiting until one becomes available.
 *
 * Can only be called from process context (for now).
 *
 * Returns 0 on success and an appropriate error value on failure.
 */
int rpmsg_trysend_offchannel(struct rpmsg_endpoint *ept, u32 src, u32 dst,
			     void *data, int len)
{
	if (WARN_ON(!ept))
		return -EINVAL;
	if (!ept->ops->trysend_offchannel)
		return -ENXIO;

	return ept->ops->trysend_offchannel(ept, src, dst, data, len);
}
EXPORT_SYMBOL(rpmsg_trysend_offchannel);

/*
 * match an rpmsg channel with a channel info struct.
 * this is used to make sure we're not creating rpmsg devices for channels
 * that already exist.
 */
static int rpmsg_device_match(struct device *dev, void *data)
{
	struct rpmsg_channel_info *chinfo = data;
	struct rpmsg_device *rpdev = to_rpmsg_device(dev);

	if (chinfo->src != RPMSG_ADDR_ANY && chinfo->src != rpdev->src)
		return 0;

	if (chinfo->dst != RPMSG_ADDR_ANY && chinfo->dst != rpdev->dst)
		return 0;

	if (strncmp(chinfo->name, rpdev->id.name, RPMSG_NAME_SIZE))
		return 0;

	/* found a match ! */
	return 1;
}

struct device *rpmsg_find_device(struct device *parent,
				 struct rpmsg_channel_info *chinfo)
{
	return device_find_child(parent, chinfo, rpmsg_device_match);

}
EXPORT_SYMBOL(rpmsg_find_device);

/* sysfs show configuration fields */
#define rpmsg_show_attr(field, path, format_string)			\
static ssize_t								\
field##_show(struct device *dev,					\
			struct device_attribute *attr, char *buf)	\
{									\
	struct rpmsg_device *rpdev = to_rpmsg_device(dev);		\
									\
	return sprintf(buf, format_string, rpdev->path);		\
}

/* for more info, see Documentation/ABI/testing/sysfs-bus-rpmsg */
rpmsg_show_attr(name, id.name, "%s\n");
rpmsg_show_attr(src, src, "0x%x\n");
rpmsg_show_attr(dst, dst, "0x%x\n");
rpmsg_show_attr(announce, announce ? "true" : "false", "%s\n");

static ssize_t modalias_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rpmsg_device *rpdev = to_rpmsg_device(dev);

	return sprintf(buf, RPMSG_DEVICE_MODALIAS_FMT "\n", rpdev->id.name);
}

static struct device_attribute rpmsg_dev_attrs[] = {
	__ATTR_RO(name),
	__ATTR_RO(modalias),
	__ATTR_RO(dst),
	__ATTR_RO(src),
	__ATTR_RO(announce),
	__ATTR_NULL
};

/* rpmsg devices and drivers are matched using the service name */
static inline int rpmsg_id_match(const struct rpmsg_device *rpdev,
				  const struct rpmsg_device_id *id)
{
	return strncmp(id->name, rpdev->id.name, RPMSG_NAME_SIZE) == 0;
}

/* match rpmsg channel and rpmsg driver */
static int rpmsg_dev_match(struct device *dev, struct device_driver *drv)
{
	struct rpmsg_device *rpdev = to_rpmsg_device(dev);
	struct rpmsg_driver *rpdrv = to_rpmsg_driver(drv);
	const struct rpmsg_device_id *ids = rpdrv->id_table;
	unsigned int i;

	if (rpdev->driver_override)
		return !strcmp(rpdev->driver_override, drv->name);

	if (ids)
		for (i = 0; ids[i].name[0]; i++)
			if (rpmsg_id_match(rpdev, &ids[i]))
				return 1;

	return of_driver_match_device(dev, drv);
}

static int rpmsg_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct rpmsg_device *rpdev = to_rpmsg_device(dev);

	return add_uevent_var(env, "MODALIAS=" RPMSG_DEVICE_MODALIAS_FMT,
					rpdev->id.name);
}

/*
 * when an rpmsg driver is probed with a channel, we seamlessly create
 * it an endpoint, binding its rx callback to a unique local rpmsg
 * address.
 *
 * if we need to, we also announce about this channel to the remote
 * processor (needed in case the driver is exposing an rpmsg service).
 */
static int rpmsg_dev_probe(struct device *dev)
{
	struct rpmsg_device *rpdev = to_rpmsg_device(dev);
	struct rpmsg_driver *rpdrv = to_rpmsg_driver(rpdev->dev.driver);
	struct rpmsg_channel_info chinfo = {};
	struct rpmsg_endpoint *ept = NULL;
	int err;

	if (rpdrv->callback) {
		strncpy(chinfo.name, rpdev->id.name, RPMSG_NAME_SIZE);
		chinfo.src = rpdev->src;
		chinfo.dst = RPMSG_ADDR_ANY;

		ept = rpmsg_create_ept(rpdev, rpdrv->callback, NULL, chinfo);
		if (!ept) {
			dev_err(dev, "failed to create endpoint\n");
			err = -ENOMEM;
			goto out;
		}

		rpdev->ept = ept;
		rpdev->src = ept->addr;
	}

	err = rpdrv->probe(rpdev);
	if (err) {
		dev_err(dev, "%s: failed: %d\n", __func__, err);
		if (ept)
			rpmsg_destroy_ept(ept);
		goto out;
	}

	if (rpdev->ops->announce_create)
		err = rpdev->ops->announce_create(rpdev);
out:
	return err;
}

static int rpmsg_dev_remove(struct device *dev)
{
	struct rpmsg_device *rpdev = to_rpmsg_device(dev);
	struct rpmsg_driver *rpdrv = to_rpmsg_driver(rpdev->dev.driver);
	int err = 0;

	if (rpdev->ops->announce_destroy)
		err = rpdev->ops->announce_destroy(rpdev);

	rpdrv->remove(rpdev);

	if (rpdev->ept)
		rpmsg_destroy_ept(rpdev->ept);

	return err;
}

static struct bus_type rpmsg_bus = {
	.name		= "rpmsg",
	.match		= rpmsg_dev_match,
	.dev_attrs	= rpmsg_dev_attrs,
	.uevent		= rpmsg_uevent,
	.probe		= rpmsg_dev_probe,
	.remove		= rpmsg_dev_remove,
};

static void rpmsg_release_device(struct device *dev)
{
	struct rpmsg_device *rpdev = to_rpmsg_device(dev);

	kfree(rpdev);
}

int rpmsg_register_device(struct rpmsg_device *rpdev)
{
	struct device *dev = &rpdev->dev;
	int ret;

	dev_set_name(&rpdev->dev, "%s.%s.%d.%d", dev_name(dev->parent),
		     rpdev->id.name, rpdev->src, rpdev->dst);

	rpdev->dev.bus = &rpmsg_bus;
	rpdev->dev.release = rpmsg_release_device;

	ret = device_register(&rpdev->dev);
	if (ret) {
		dev_err(dev, "device_register failed: %d\n", ret);
		put_device(&rpdev->dev);
	}

	return ret;
}
EXPORT_SYMBOL(rpmsg_register_device);

/*
 * find an existing channel using its name + address properties,
 * and destroy it
 */
int rpmsg_unregister_device(struct device *parent,
			    struct rpmsg_channel_info *chinfo)
{
	struct device *dev;

	dev = rpmsg_find_device(parent, chinfo);
	if (!dev)
		return -EINVAL;

	device_unregister(dev);

	put_device(dev);

	return 0;
}
EXPORT_SYMBOL(rpmsg_unregister_device);

/**
 * __register_rpmsg_driver() - register an rpmsg driver with the rpmsg bus
 * @rpdrv: pointer to a struct rpmsg_driver
 * @owner: owning module/driver
 *
 * Returns 0 on success, and an appropriate error value on failure.
 */
int __register_rpmsg_driver(struct rpmsg_driver *rpdrv, struct module *owner)
{
	rpdrv->drv.bus = &rpmsg_bus;
	rpdrv->drv.owner = owner;
	return driver_register(&rpdrv->drv);
}
EXPORT_SYMBOL(__register_rpmsg_driver);

/**
 * unregister_rpmsg_driver() - unregister an rpmsg driver from the rpmsg bus
 * @rpdrv: pointer to a struct rpmsg_driver
 *
 * Returns 0 on success, and an appropriate error value on failure.
 */
void unregister_rpmsg_driver(struct rpmsg_driver *rpdrv)
{
	driver_unregister(&rpdrv->drv);
}
EXPORT_SYMBOL(unregister_rpmsg_driver);


static int __init rpmsg_init(void)
{
	int ret;

	ret = bus_register(&rpmsg_bus);
	if (ret)
		pr_err("failed to register rpmsg bus: %d\n", ret);

	return ret;
}
postcore_initcall(rpmsg_init);

static void __exit rpmsg_fini(void)
{
	bus_unregister(&rpmsg_bus);
}
module_exit(rpmsg_fini);

MODULE_DESCRIPTION("remote processor messaging bus");
MODULE_LICENSE("GPL v2");
