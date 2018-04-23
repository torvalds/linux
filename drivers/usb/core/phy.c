// SPDX-License-Identifier: GPL-2.0+
/*
 * A wrapper for multiple PHYs which passes all phy_* function calls to
 * multiple (actual) PHY devices. This is comes handy when initializing
 * all PHYs on a HCD and to keep them all in the same state.
 *
 * Copyright (C) 2018 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/phy/phy.h>
#include <linux/of.h>

#include "phy.h"

struct usb_phy_roothub {
	struct phy		*phy;
	struct list_head	list;
};

static struct usb_phy_roothub *usb_phy_roothub_alloc(struct device *dev)
{
	struct usb_phy_roothub *roothub_entry;

	roothub_entry = devm_kzalloc(dev, sizeof(*roothub_entry), GFP_KERNEL);
	if (!roothub_entry)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&roothub_entry->list);

	return roothub_entry;
}

static int usb_phy_roothub_add_phy(struct device *dev, int index,
				   struct list_head *list)
{
	struct usb_phy_roothub *roothub_entry;
	struct phy *phy = devm_of_phy_get_by_index(dev, dev->of_node, index);

	if (IS_ERR_OR_NULL(phy)) {
		if (!phy || PTR_ERR(phy) == -ENODEV)
			return 0;
		else
			return PTR_ERR(phy);
	}

	roothub_entry = usb_phy_roothub_alloc(dev);
	if (IS_ERR(roothub_entry))
		return PTR_ERR(roothub_entry);

	roothub_entry->phy = phy;

	list_add_tail(&roothub_entry->list, list);

	return 0;
}

struct usb_phy_roothub *usb_phy_roothub_init(struct device *dev)
{
	struct usb_phy_roothub *phy_roothub;
	struct usb_phy_roothub *roothub_entry;
	struct list_head *head;
	int i, num_phys, err;

	num_phys = of_count_phandle_with_args(dev->of_node, "phys",
					      "#phy-cells");
	if (num_phys <= 0)
		return NULL;

	phy_roothub = usb_phy_roothub_alloc(dev);
	if (IS_ERR(phy_roothub))
		return phy_roothub;

	for (i = 0; i < num_phys; i++) {
		err = usb_phy_roothub_add_phy(dev, i, &phy_roothub->list);
		if (err)
			goto err_out;
	}

	head = &phy_roothub->list;

	list_for_each_entry(roothub_entry, head, list) {
		err = phy_init(roothub_entry->phy);
		if (err)
			goto err_exit_phys;
	}

	return phy_roothub;

err_exit_phys:
	list_for_each_entry_continue_reverse(roothub_entry, head, list)
		phy_exit(roothub_entry->phy);

err_out:
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(usb_phy_roothub_init);

int usb_phy_roothub_exit(struct usb_phy_roothub *phy_roothub)
{
	struct usb_phy_roothub *roothub_entry;
	struct list_head *head;
	int err, ret = 0;

	if (!phy_roothub)
		return 0;

	head = &phy_roothub->list;

	list_for_each_entry(roothub_entry, head, list) {
		err = phy_exit(roothub_entry->phy);
		if (err)
			ret = ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(usb_phy_roothub_exit);

int usb_phy_roothub_power_on(struct usb_phy_roothub *phy_roothub)
{
	struct usb_phy_roothub *roothub_entry;
	struct list_head *head;
	int err;

	if (!phy_roothub)
		return 0;

	head = &phy_roothub->list;

	list_for_each_entry(roothub_entry, head, list) {
		err = phy_power_on(roothub_entry->phy);
		if (err)
			goto err_out;
	}

	return 0;

err_out:
	list_for_each_entry_continue_reverse(roothub_entry, head, list)
		phy_power_off(roothub_entry->phy);

	return err;
}
EXPORT_SYMBOL_GPL(usb_phy_roothub_power_on);

void usb_phy_roothub_power_off(struct usb_phy_roothub *phy_roothub)
{
	struct usb_phy_roothub *roothub_entry;

	if (!phy_roothub)
		return;

	list_for_each_entry_reverse(roothub_entry, &phy_roothub->list, list)
		phy_power_off(roothub_entry->phy);
}
EXPORT_SYMBOL_GPL(usb_phy_roothub_power_off);
