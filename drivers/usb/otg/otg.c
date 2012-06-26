/*
 * otg.c -- USB OTG utility code
 *
 * Copyright (C) 2004 Texas Instruments
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
#include <linux/slab.h>

#include <linux/usb/otg.h>

static LIST_HEAD(phy_list);
static DEFINE_SPINLOCK(phy_lock);

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

static void devm_usb_phy_release(struct device *dev, void *res)
{
	struct usb_phy *phy = *(struct usb_phy **)res;

	usb_put_phy(phy);
}

static int devm_usb_phy_match(struct device *dev, void *res, void *match_data)
{
	return res == match_data;
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
		return NULL;

	phy = usb_get_phy(type);
	if (!IS_ERR(phy)) {
		*ptr = phy;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return phy;
}
EXPORT_SYMBOL(devm_usb_get_phy);

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
	if (IS_ERR(phy)) {
		pr_err("unable to find transceiver of type %s\n",
			usb_phy_type_string(type));
		return phy;
	}

	get_device(phy->dev);

	spin_unlock_irqrestore(&phy_lock, flags);

	return phy;
}
EXPORT_SYMBOL(usb_get_phy);

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
EXPORT_SYMBOL(devm_usb_put_phy);

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
	if (x)
		put_device(x->dev);
}
EXPORT_SYMBOL(usb_put_phy);

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

	if (x && x->type != USB_PHY_TYPE_UNDEFINED) {
		dev_err(x->dev, "not accepting initialized PHY %s\n", x->label);
		return -EINVAL;
	}

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
EXPORT_SYMBOL(usb_add_phy);

/**
 * usb_remove_phy - remove the OTG PHY
 * @x: the USB OTG PHY to be removed;
 *
 * This reverts the effects of usb_add_phy
 */
void usb_remove_phy(struct usb_phy *x)
{
	unsigned long	flags;

	spin_lock_irqsave(&phy_lock, flags);
	if (x)
		list_del(&x->head);
	spin_unlock_irqrestore(&phy_lock, flags);
}
EXPORT_SYMBOL(usb_remove_phy);

const char *otg_state_string(enum usb_otg_state state)
{
	switch (state) {
	case OTG_STATE_A_IDLE:
		return "a_idle";
	case OTG_STATE_A_WAIT_VRISE:
		return "a_wait_vrise";
	case OTG_STATE_A_WAIT_BCON:
		return "a_wait_bcon";
	case OTG_STATE_A_HOST:
		return "a_host";
	case OTG_STATE_A_SUSPEND:
		return "a_suspend";
	case OTG_STATE_A_PERIPHERAL:
		return "a_peripheral";
	case OTG_STATE_A_WAIT_VFALL:
		return "a_wait_vfall";
	case OTG_STATE_A_VBUS_ERR:
		return "a_vbus_err";
	case OTG_STATE_B_IDLE:
		return "b_idle";
	case OTG_STATE_B_SRP_INIT:
		return "b_srp_init";
	case OTG_STATE_B_PERIPHERAL:
		return "b_peripheral";
	case OTG_STATE_B_WAIT_ACON:
		return "b_wait_acon";
	case OTG_STATE_B_HOST:
		return "b_host";
	default:
		return "UNDEFINED";
	}
}
EXPORT_SYMBOL(otg_state_string);
