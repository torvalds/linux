/*
 * Copyright (c) 2010-2012 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * EXYNOS - SATA utility framework.
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

struct sata_phy *sata_get_phy(enum sata_phy_type type)
{
	struct sata_phy *x = NULL;
	unsigned long flag;

	if (list_empty(&phy_list))
		return x;

	spin_lock_irqsave(&phy_lock, flag);

	list_for_each_entry(x, &phy_list, head) {
		if (x->type == type) {
			get_device(x->dev);
			break;
		}
	}

	spin_unlock_irqrestore(&phy_lock, flag);
	return x;
}
EXPORT_SYMBOL(sata_get_phy);

int sata_add_phy(struct sata_phy *phy, enum sata_phy_type type)
{
	unsigned long flag;
	unsigned int ret = -EINVAL;
	struct sata_phy *x;

	spin_lock_irqsave(&phy_lock, flag);

	if (!phy)
		return ret;

	list_for_each_entry(x, &phy_list, head) {
		if (x->type == type) {
			dev_err(phy->dev, "transceiver type already exists\n");
			goto out;
		}
	}
	phy->type = type;
	list_add_tail(&phy->head, &phy_list);
	ret = 0;

 out:
	spin_unlock_irqrestore(&phy_lock, flag);
	return ret;
}
EXPORT_SYMBOL(sata_add_phy);

void sata_remove_phy(struct sata_phy *phy)
{
	unsigned long flag;
	struct sata_phy *x;

	spin_lock_irqsave(&phy_lock, flag);

	if (!phy)
		return;

	list_for_each_entry(x, &phy_list, head) {
		if (x->type == phy->type)
			list_del(&phy->head);
	}

	spin_unlock_irqrestore(&phy_lock, flag);
}
EXPORT_SYMBOL(sata_remove_phy);

void sata_put_phy(struct sata_phy *phy)
{
	unsigned long flag;

	spin_lock_irqsave(&phy_lock, flag);

	if (!phy)
		return;

	put_device(phy->dev);
	spin_unlock_irqrestore(&phy_lock, flag);

}
EXPORT_SYMBOL(sata_put_phy);
