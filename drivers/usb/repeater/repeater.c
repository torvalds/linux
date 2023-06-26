// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/usb/repeater.h>

static LIST_HEAD(repeater_list);
static DEFINE_SPINLOCK(repeater_lock);

void usb_put_repeater(struct usb_repeater *r)
{
	if (r) {
		put_device(r->dev);
		if (r->dev->driver && r->dev->driver->owner)
			module_put(r->dev->driver->owner);
	}
}
EXPORT_SYMBOL(usb_put_repeater);

static struct usb_repeater *of_usb_find_repeater(struct device_node *node)
{
	struct usb_repeater *r;

	if (!of_device_is_available(node))
		return ERR_PTR(-ENODEV);

	list_for_each_entry(r, &repeater_list, head) {
		if (node != r->dev->of_node)
			continue;
		return r;
	}

	return ERR_PTR(-EPROBE_DEFER);
}

static void devm_usb_repeater_release_by_node(struct device *dev, void *res)
{
	usb_put_repeater(res);
}

struct usb_repeater *devm_usb_get_repeater_by_node(struct device *dev,
			struct device_node *node)
{
	struct usb_repeater *r = ERR_PTR(-ENOMEM);
	struct usb_repeater *r_devm;
	unsigned long flags;

	r_devm = devres_alloc(devm_usb_repeater_release_by_node,
					sizeof(*r_devm), GFP_KERNEL);
	if (!r_devm) {
		dev_dbg(dev, "failed to allocate memory for r_devm\n");
		return r;
	}

	spin_lock_irqsave(&repeater_lock, flags);
	r = of_usb_find_repeater(node);
	if (IS_ERR(r)) {
		devres_free(r_devm);
		goto err0;
	}

	if (!try_module_get(r->dev->driver->owner)) {
		r = ERR_PTR(-ENODEV);
		devres_free(r_devm);
		goto err0;
	}

	devres_add(dev, r_devm);
	get_device(r->dev);
err0:
	spin_unlock_irqrestore(&repeater_lock, flags);
	return r;
}
EXPORT_SYMBOL(devm_usb_get_repeater_by_node);

struct usb_repeater *devm_usb_get_repeater_by_phandle(struct device *dev,
	const char *phandle, u8 index)
{
	struct device_node *node;
	struct usb_repeater *r;

	if (!dev->of_node) {
		dev_dbg(dev, "device does not have a device node entry\n");
		return ERR_PTR(-EINVAL);
	}

	node = of_parse_phandle(dev->of_node, phandle, index);
	if (!node) {
		dev_dbg(dev, "failed to get %s phandle in %pOF node\n", phandle,
			dev->of_node);
		return ERR_PTR(-ENODEV);
	}

	r = devm_usb_get_repeater_by_node(dev, node);
	of_node_put(node);
	return r;
}
EXPORT_SYMBOL(devm_usb_get_repeater_by_phandle);

struct usb_repeater *usb_get_repeater_by_node(struct device_node *node)
{
	struct usb_repeater *r = ERR_PTR(-ENODEV);
	unsigned long flags;

	spin_lock_irqsave(&repeater_lock, flags);
	r = of_usb_find_repeater(node);
	spin_unlock_irqrestore(&repeater_lock, flags);

	return r;
}
EXPORT_SYMBOL(usb_get_repeater_by_node);

struct usb_repeater *usb_get_repeater_by_phandle(struct device *dev,
	const char *phandle, u8 index)
{
	struct device_node *node;
	struct usb_repeater *r;

	if (!dev->of_node) {
		dev_dbg(dev, "device does not have a device node entry\n");
		return ERR_PTR(-EINVAL);
	}

	node = of_parse_phandle(dev->of_node, phandle, index);
	if (!node) {
		dev_dbg(dev, "failed to get %s phandle in %pOF node\n", phandle,
			dev->of_node);
		return ERR_PTR(-ENODEV);
	}

	r = usb_get_repeater_by_node(node);
	of_node_put(node);
	return r;
}
EXPORT_SYMBOL(usb_get_repeater_by_phandle);

/**
 * usb_add_repeater_dev - Add repeater device
 * @r: repeater device available
 *
 * It shall used by any USB repeater driver to register
 * device to allow USB PHY driver to get access to it
 * for invoking repeater specific operations.
 */
int usb_add_repeater_dev(struct usb_repeater *r)
{
	if (!r || !r->dev) {
		pr_err("no repeater device\n");
		return -EINVAL;
	}

	spin_lock(&repeater_lock);
	list_add_tail(&r->head, &repeater_list);
	spin_unlock(&repeater_lock);

	return 0;
}
EXPORT_SYMBOL(usb_add_repeater_dev);

/**
 * usb_remove_repeater_dev - remove repeater device
 * @r: USB repeater device to remove from repeater list
 */
void usb_remove_repeater_dev(struct usb_repeater *r)
{
	unsigned long flags;

	spin_lock_irqsave(&repeater_lock, flags);
	if (r)
		list_del(&r->head);
	spin_unlock_irqrestore(&repeater_lock, flags);
}
EXPORT_SYMBOL(usb_remove_repeater_dev);

MODULE_DESCRIPTION("USB repeater framework");
MODULE_LICENSE("GPL");
