/*
 * phy.c -- USB phy handling
 *
 * Copyright (C) 2004-2013 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>

#include <linux/usb/phy.h>

static LIST_HEAD(phy_list);
static LIST_HEAD(phy_bind_list);
static DEFINE_SPINLOCK(phy_lock);

struct phy_devm {
	struct usb_phy *phy;
	struct notifier_block *nb;
};

static struct usb_phy *__usb_find_phy(struct list_head *list,
	enum usb_phy_type type)
{
	struct usb_phy  *phy = NULL;

	list_for_each_entry(phy, list, head) {
		if (phy->type != type)
			continue;

		return phy;
	}

	return ERR_PTR(-ENODEV);
}

static struct usb_phy *__usb_find_phy_dev(struct device *dev,
	struct list_head *list, u8 index)
{
	struct usb_phy_bind *phy_bind = NULL;

	list_for_each_entry(phy_bind, list, list) {
		if (!(strcmp(phy_bind->dev_name, dev_name(dev))) &&
				phy_bind->index == index) {
			if (phy_bind->phy)
				return phy_bind->phy;
			else
				return ERR_PTR(-EPROBE_DEFER);
		}
	}

	return ERR_PTR(-ENODEV);
}

static struct usb_phy *__of_usb_find_phy(struct device_node *node)
{
	struct usb_phy  *phy;

	if (!of_device_is_available(node))
		return ERR_PTR(-ENODEV);

	list_for_each_entry(phy, &phy_list, head) {
		if (node != phy->dev->of_node)
			continue;

		return phy;
	}

	return ERR_PTR(-EPROBE_DEFER);
}

static void devm_usb_phy_release(struct device *dev, void *res)
{
	struct usb_phy *phy = *(struct usb_phy **)res;

	usb_put_phy(phy);
}

static void devm_usb_phy_release2(struct device *dev, void *_res)
{
	struct phy_devm *res = _res;

	if (res->nb)
		usb_unregister_notifier(res->phy, res->nb);
	usb_put_phy(res->phy);
}

static int devm_usb_phy_match(struct device *dev, void *res, void *match_data)
{
	struct usb_phy **phy = res;

	return *phy == match_data;
}

static int usb_add_extcon(struct usb_phy *x)
{
	int ret;

	if (of_property_read_bool(x->dev->of_node, "extcon")) {
		x->edev = extcon_get_edev_by_phandle(x->dev, 0);
		if (IS_ERR(x->edev))
			return PTR_ERR(x->edev);

		x->id_edev = extcon_get_edev_by_phandle(x->dev, 1);
		if (IS_ERR(x->id_edev)) {
			x->id_edev = NULL;
			dev_info(x->dev, "No separate ID extcon device\n");
		}

		if (x->vbus_nb.notifier_call) {
			ret = devm_extcon_register_notifier(x->dev, x->edev,
							    EXTCON_USB,
							    &x->vbus_nb);
			if (ret < 0) {
				dev_err(x->dev,
					"register VBUS notifier failed\n");
				return ret;
			}
		}

		if (x->id_nb.notifier_call) {
			struct extcon_dev *id_ext;

			if (x->id_edev)
				id_ext = x->id_edev;
			else
				id_ext = x->edev;

			ret = devm_extcon_register_notifier(x->dev, id_ext,
							    EXTCON_USB_HOST,
							    &x->id_nb);
			if (ret < 0) {
				dev_err(x->dev,
					"register ID notifier failed\n");
				return ret;
			}
		}
	}

	return 0;
}

/**
 * devm_usb_get_phy - find the USB PHY
 * @dev - device that requests this phy
 * @type - the type of the phy the controller requires
 *
 * Gets the phy using usb_get_phy(), and associates a device with it using
 * devres. On driver detach, release function is invoked on the devres data,
 * then, devres data is freed.
 *
 * For use by USB host and peripheral drivers.
 */
struct usb_phy *devm_usb_get_phy(struct device *dev, enum usb_phy_type type)
{
	struct usb_phy **ptr, *phy;

	ptr = devres_alloc(devm_usb_phy_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	phy = usb_get_phy(type);
	if (!IS_ERR(phy)) {
		*ptr = phy;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return phy;
}
EXPORT_SYMBOL_GPL(devm_usb_get_phy);

/**
 * usb_get_phy - find the USB PHY
 * @type - the type of the phy the controller requires
 *
 * Returns the phy driver, after getting a refcount to it; or
 * -ENODEV if there is no such phy.  The caller is responsible for
 * calling usb_put_phy() to release that count.
 *
 * For use by USB host and peripheral drivers.
 */
struct usb_phy *usb_get_phy(enum usb_phy_type type)
{
	struct usb_phy	*phy = NULL;
	unsigned long	flags;

	spin_lock_irqsave(&phy_lock, flags);

	phy = __usb_find_phy(&phy_list, type);
	if (IS_ERR(phy) || !try_module_get(phy->dev->driver->owner)) {
		pr_debug("PHY: unable to find transceiver of type %s\n",
			usb_phy_type_string(type));
		if (!IS_ERR(phy))
			phy = ERR_PTR(-ENODEV);

		goto err0;
	}

	get_device(phy->dev);

err0:
	spin_unlock_irqrestore(&phy_lock, flags);

	return phy;
}
EXPORT_SYMBOL_GPL(usb_get_phy);

/**
 * devm_usb_get_phy_by_node - find the USB PHY by device_node
 * @dev - device that requests this phy
 * @node - the device_node for the phy device.
 * @nb - a notifier_block to register with the phy.
 *
 * Returns the phy driver associated with the given device_node,
 * after getting a refcount to it, -ENODEV if there is no such phy or
 * -EPROBE_DEFER if the device is not yet loaded. While at that, it
 * also associates the device with
 * the phy using devres. On driver detach, release function is invoked
 * on the devres data, then, devres data is freed.
 *
 * For use by peripheral drivers for devices related to a phy,
 * such as a charger.
 */
struct  usb_phy *devm_usb_get_phy_by_node(struct device *dev,
					  struct device_node *node,
					  struct notifier_block *nb)
{
	struct usb_phy	*phy = ERR_PTR(-ENOMEM);
	struct phy_devm	*ptr;
	unsigned long	flags;

	ptr = devres_alloc(devm_usb_phy_release2, sizeof(*ptr), GFP_KERNEL);
	if (!ptr) {
		dev_dbg(dev, "failed to allocate memory for devres\n");
		goto err0;
	}

	spin_lock_irqsave(&phy_lock, flags);

	phy = __of_usb_find_phy(node);
	if (IS_ERR(phy)) {
		devres_free(ptr);
		goto err1;
	}

	if (!try_module_get(phy->dev->driver->owner)) {
		phy = ERR_PTR(-ENODEV);
		devres_free(ptr);
		goto err1;
	}
	if (nb)
		usb_register_notifier(phy, nb);
	ptr->phy = phy;
	ptr->nb = nb;
	devres_add(dev, ptr);

	get_device(phy->dev);

err1:
	spin_unlock_irqrestore(&phy_lock, flags);

err0:

	return phy;
}
EXPORT_SYMBOL_GPL(devm_usb_get_phy_by_node);

/**
 * devm_usb_get_phy_by_phandle - find the USB PHY by phandle
 * @dev - device that requests this phy
 * @phandle - name of the property holding the phy phandle value
 * @index - the index of the phy
 *
 * Returns the phy driver associated with the given phandle value,
 * after getting a refcount to it, -ENODEV if there is no such phy or
 * -EPROBE_DEFER if there is a phandle to the phy, but the device is
 * not yet loaded. While at that, it also associates the device with
 * the phy using devres. On driver detach, release function is invoked
 * on the devres data, then, devres data is freed.
 *
 * For use by USB host and peripheral drivers.
 */
struct usb_phy *devm_usb_get_phy_by_phandle(struct device *dev,
	const char *phandle, u8 index)
{
	struct device_node *node;
	struct usb_phy	*phy;

	if (!dev->of_node) {
		dev_dbg(dev, "device does not have a device node entry\n");
		return ERR_PTR(-EINVAL);
	}

	node = of_parse_phandle(dev->of_node, phandle, index);
	if (!node) {
		dev_dbg(dev, "failed to get %s phandle in %s node\n", phandle,
			dev->of_node->full_name);
		return ERR_PTR(-ENODEV);
	}
	phy = devm_usb_get_phy_by_node(dev, node, NULL);
	of_node_put(node);
	return phy;
}
EXPORT_SYMBOL_GPL(devm_usb_get_phy_by_phandle);

/**
 * usb_get_phy_dev - find the USB PHY
 * @dev - device that requests this phy
 * @index - the index of the phy
 *
 * Returns the phy driver, after getting a refcount to it; or
 * -ENODEV if there is no such phy.  The caller is responsible for
 * calling usb_put_phy() to release that count.
 *
 * For use by USB host and peripheral drivers.
 */
struct usb_phy *usb_get_phy_dev(struct device *dev, u8 index)
{
	struct usb_phy	*phy = NULL;
	unsigned long	flags;

	spin_lock_irqsave(&phy_lock, flags);

	phy = __usb_find_phy_dev(dev, &phy_bind_list, index);
	if (IS_ERR(phy) || !try_module_get(phy->dev->driver->owner)) {
		dev_dbg(dev, "unable to find transceiver\n");
		if (!IS_ERR(phy))
			phy = ERR_PTR(-ENODEV);

		goto err0;
	}

	get_device(phy->dev);

err0:
	spin_unlock_irqrestore(&phy_lock, flags);

	return phy;
}
EXPORT_SYMBOL_GPL(usb_get_phy_dev);

/**
 * devm_usb_get_phy_dev - find the USB PHY using device ptr and index
 * @dev - device that requests this phy
 * @index - the index of the phy
 *
 * Gets the phy using usb_get_phy_dev(), and associates a device with it using
 * devres. On driver detach, release function is invoked on the devres data,
 * then, devres data is freed.
 *
 * For use by USB host and peripheral drivers.
 */
struct usb_phy *devm_usb_get_phy_dev(struct device *dev, u8 index)
{
	struct usb_phy **ptr, *phy;

	ptr = devres_alloc(devm_usb_phy_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	phy = usb_get_phy_dev(dev, index);
	if (!IS_ERR(phy)) {
		*ptr = phy;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return phy;
}
EXPORT_SYMBOL_GPL(devm_usb_get_phy_dev);

/**
 * devm_usb_put_phy - release the USB PHY
 * @dev - device that wants to release this phy
 * @phy - the phy returned by devm_usb_get_phy()
 *
 * destroys the devres associated with this phy and invokes usb_put_phy
 * to release the phy.
 *
 * For use by USB host and peripheral drivers.
 */
void devm_usb_put_phy(struct device *dev, struct usb_phy *phy)
{
	int r;

	r = devres_destroy(dev, devm_usb_phy_release, devm_usb_phy_match, phy);
	dev_WARN_ONCE(dev, r, "couldn't find PHY resource\n");
}
EXPORT_SYMBOL_GPL(devm_usb_put_phy);

/**
 * usb_put_phy - release the USB PHY
 * @x: the phy returned by usb_get_phy()
 *
 * Releases a refcount the caller received from usb_get_phy().
 *
 * For use by USB host and peripheral drivers.
 */
void usb_put_phy(struct usb_phy *x)
{
	if (x) {
		struct module *owner = x->dev->driver->owner;

		put_device(x->dev);
		module_put(owner);
	}
}
EXPORT_SYMBOL_GPL(usb_put_phy);

/**
 * usb_add_phy - declare the USB PHY
 * @x: the USB phy to be used; or NULL
 * @type - the type of this PHY
 *
 * This call is exclusively for use by phy drivers, which
 * coordinate the activities of drivers for host and peripheral
 * controllers, and in some cases for VBUS current regulation.
 */
int usb_add_phy(struct usb_phy *x, enum usb_phy_type type)
{
	int		ret = 0;
	unsigned long	flags;
	struct usb_phy	*phy;

	if (x->type != USB_PHY_TYPE_UNDEFINED) {
		dev_err(x->dev, "not accepting initialized PHY %s\n", x->label);
		return -EINVAL;
	}

	ret = usb_add_extcon(x);
	if (ret)
		return ret;

	ATOMIC_INIT_NOTIFIER_HEAD(&x->notifier);

	spin_lock_irqsave(&phy_lock, flags);

	list_for_each_entry(phy, &phy_list, head) {
		if (phy->type == type) {
			ret = -EBUSY;
			dev_err(x->dev, "transceiver type %s already exists\n",
						usb_phy_type_string(type));
			goto out;
		}
	}

	x->type = type;
	list_add_tail(&x->head, &phy_list);

out:
	spin_unlock_irqrestore(&phy_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(usb_add_phy);

/**
 * usb_add_phy_dev - declare the USB PHY
 * @x: the USB phy to be used; or NULL
 *
 * This call is exclusively for use by phy drivers, which
 * coordinate the activities of drivers for host and peripheral
 * controllers, and in some cases for VBUS current regulation.
 */
int usb_add_phy_dev(struct usb_phy *x)
{
	struct usb_phy_bind *phy_bind;
	unsigned long flags;
	int ret;

	if (!x->dev) {
		dev_err(x->dev, "no device provided for PHY\n");
		return -EINVAL;
	}

	ret = usb_add_extcon(x);
	if (ret)
		return ret;

	ATOMIC_INIT_NOTIFIER_HEAD(&x->notifier);

	spin_lock_irqsave(&phy_lock, flags);
	list_for_each_entry(phy_bind, &phy_bind_list, list)
		if (!(strcmp(phy_bind->phy_dev_name, dev_name(x->dev))))
			phy_bind->phy = x;

	list_add_tail(&x->head, &phy_list);

	spin_unlock_irqrestore(&phy_lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(usb_add_phy_dev);

/**
 * usb_remove_phy - remove the OTG PHY
 * @x: the USB OTG PHY to be removed;
 *
 * This reverts the effects of usb_add_phy
 */
void usb_remove_phy(struct usb_phy *x)
{
	unsigned long	flags;
	struct usb_phy_bind *phy_bind;

	spin_lock_irqsave(&phy_lock, flags);
	if (x) {
		list_for_each_entry(phy_bind, &phy_bind_list, list)
			if (phy_bind->phy == x)
				phy_bind->phy = NULL;
		list_del(&x->head);
	}
	spin_unlock_irqrestore(&phy_lock, flags);
}
EXPORT_SYMBOL_GPL(usb_remove_phy);

/**
 * usb_bind_phy - bind the phy and the controller that uses the phy
 * @dev_name: the device name of the device that will bind to the phy
 * @index: index to specify the port number
 * @phy_dev_name: the device name of the phy
 *
 * Fills the phy_bind structure with the dev_name and phy_dev_name. This will
 * be used when the phy driver registers the phy and when the controller
 * requests this phy.
 *
 * To be used by platform specific initialization code.
 */
int usb_bind_phy(const char *dev_name, u8 index,
				const char *phy_dev_name)
{
	struct usb_phy_bind *phy_bind;
	unsigned long flags;

	phy_bind = kzalloc(sizeof(*phy_bind), GFP_KERNEL);
	if (!phy_bind)
		return -ENOMEM;

	phy_bind->dev_name = dev_name;
	phy_bind->phy_dev_name = phy_dev_name;
	phy_bind->index = index;

	spin_lock_irqsave(&phy_lock, flags);
	list_add_tail(&phy_bind->list, &phy_bind_list);
	spin_unlock_irqrestore(&phy_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(usb_bind_phy);

/**
 * usb_phy_set_event - set event to phy event
 * @x: the phy returned by usb_get_phy();
 *
 * This sets event to phy event
 */
void usb_phy_set_event(struct usb_phy *x, unsigned long event)
{
	x->last_event = event;
}
EXPORT_SYMBOL_GPL(usb_phy_set_event);
