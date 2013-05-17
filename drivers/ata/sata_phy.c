/*
 * Copyright (c) 2010-2012 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * SATA PHY framework.
 *
 * This file provides a set of functions/interfaces for establishing
 * communication between SATA controller and the PHY controller. A
 * PHY controller driver registers call backs for its initialization and
 * shutdown. The SATA controller driver finds the appropriate PHYs for
 * its implemented ports and initialize/shutdown PHYs through the
 * call backs provided.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/list.h>
#include "sata_phy.h"

static LIST_HEAD(phy_list);
static DEFINE_SPINLOCK(phy_lock);

struct sata_phy *sata_get_phy(struct device_node *phy_np)
{
	struct sata_phy *phy;
	unsigned long flag;

	spin_lock_irqsave(&phy_lock, flag);

	if (list_empty(&phy_list)) {
		spin_unlock_irqrestore(&phy_lock, flag);
		return ERR_PTR(-ENODEV);
	}

	list_for_each_entry(phy, &phy_list, head) {
		if (phy->dev->of_node == phy_np) {
			if (phy->status == IN_USE) {
				pr_info(KERN_INFO
					"PHY already in use\n");
				spin_unlock_irqrestore(&phy_lock, flag);
				return ERR_PTR(-EBUSY);
			}

			get_device(phy->dev);
			phy->status = IN_USE;
			spin_unlock_irqrestore(&phy_lock, flag);
			return phy;
		}
	}

	spin_unlock_irqrestore(&phy_lock, flag);
	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL(sata_get_phy);

int sata_add_phy(struct sata_phy *sataphy)
{
	unsigned long flag;
	unsigned int ret = -EINVAL;
	struct sata_phy *phy;

	if (!sataphy)
		return ret;

	spin_lock_irqsave(&phy_lock, flag);

	list_for_each_entry(phy, &phy_list, head) {
		if (phy->dev->of_node == sataphy->dev->of_node) {
			dev_err(sataphy->dev, "PHY already exists in the list\n");
			goto out;
		}
	}

	sataphy->status = NOT_IN_USE;
	list_add_tail(&sataphy->head, &phy_list);
	ret = 0;

 out:
	spin_unlock_irqrestore(&phy_lock, flag);
	return ret;
}
EXPORT_SYMBOL(sata_add_phy);

void sata_remove_phy(struct sata_phy *sataphy)
{
	unsigned long flag;
	struct sata_phy *phy;

	if (!sataphy)
		return;

	if (sataphy->status == IN_USE) {
		pr_info(KERN_INFO
			"PHY in use, cannot be removed\n");
		return;
	}

	spin_lock_irqsave(&phy_lock, flag);

	list_for_each_entry(phy, &phy_list, head) {
		if (phy->dev->of_node == sataphy->dev->of_node)
			list_del(&phy->head);
	}

	spin_unlock_irqrestore(&phy_lock, flag);
}
EXPORT_SYMBOL(sata_remove_phy);

void sata_put_phy(struct sata_phy *sataphy)
{
	unsigned long flag;

	if (!sataphy)
		return;

	spin_lock_irqsave(&phy_lock, flag);

	put_device(sataphy->dev);
	sataphy->status = NOT_IN_USE;

	spin_unlock_irqrestore(&phy_lock, flag);
}
EXPORT_SYMBOL(sata_put_phy);

int sata_init_phy(struct sata_phy *sataphy)
{
	if (sataphy && sataphy->init)
		return sataphy->init(sataphy);

	return -EINVAL;
}
EXPORT_SYMBOL(sata_init_phy);

void sata_shutdown_phy(struct sata_phy *sataphy)
{
	if (sataphy && sataphy->shutdown)
		sataphy->shutdown(sataphy);
}
EXPORT_SYMBOL(sata_shutdown_phy);

