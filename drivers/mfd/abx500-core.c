/*
 * Copyright (C) 2007-2010 ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 * Register access functions for the ABX500 Mixed Signal IC family.
 * Author: Mattias Wallin <mattias.wallin@stericsson.com>
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mfd/abx500.h>

static LIST_HEAD(abx500_list);

struct abx500_device_entry {
	struct list_head list;
	struct abx500_ops ops;
	struct device *dev;
};

static void lookup_ops(struct device *dev, struct abx500_ops **ops)
{
	struct abx500_device_entry *dev_entry;

	*ops = NULL;
	list_for_each_entry(dev_entry, &abx500_list, list) {
		if (dev_entry->dev == dev) {
			*ops = &dev_entry->ops;
			return;
		}
	}
}

int abx500_register_ops(struct device *dev, struct abx500_ops *ops)
{
	struct abx500_device_entry *dev_entry;

	dev_entry = devm_kzalloc(dev,
				 sizeof(struct abx500_device_entry),
				 GFP_KERNEL);
	if (!dev_entry) {
		dev_err(dev, "register_ops kzalloc failed");
		return -ENOMEM;
	}
	dev_entry->dev = dev;
	memcpy(&dev_entry->ops, ops, sizeof(struct abx500_ops));

	list_add_tail(&dev_entry->list, &abx500_list);
	return 0;
}
EXPORT_SYMBOL(abx500_register_ops);

void abx500_remove_ops(struct device *dev)
{
	struct abx500_device_entry *dev_entry, *tmp;

	list_for_each_entry_safe(dev_entry, tmp, &abx500_list, list)
		if (dev_entry->dev == dev)
			list_del(&dev_entry->list);
}
EXPORT_SYMBOL(abx500_remove_ops);

int abx500_set_register_interruptible(struct device *dev, u8 bank, u8 reg,
	u8 value)
{
	struct abx500_ops *ops;

	lookup_ops(dev->parent, &ops);
	if ((ops != NULL) && (ops->set_register != NULL))
		return ops->set_register(dev, bank, reg, value);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL(abx500_set_register_interruptible);

int abx500_get_register_interruptible(struct device *dev, u8 bank, u8 reg,
	u8 *value)
{
	struct abx500_ops *ops;

	lookup_ops(dev->parent, &ops);
	if ((ops != NULL) && (ops->get_register != NULL))
		return ops->get_register(dev, bank, reg, value);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL(abx500_get_register_interruptible);

int abx500_get_register_page_interruptible(struct device *dev, u8 bank,
	u8 first_reg, u8 *regvals, u8 numregs)
{
	struct abx500_ops *ops;

	lookup_ops(dev->parent, &ops);
	if ((ops != NULL) && (ops->get_register_page != NULL))
		return ops->get_register_page(dev, bank,
			first_reg, regvals, numregs);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL(abx500_get_register_page_interruptible);

int abx500_mask_and_set_register_interruptible(struct device *dev, u8 bank,
	u8 reg, u8 bitmask, u8 bitvalues)
{
	struct abx500_ops *ops;

	lookup_ops(dev->parent, &ops);
	if ((ops != NULL) && (ops->mask_and_set_register != NULL))
		return ops->mask_and_set_register(dev, bank,
			reg, bitmask, bitvalues);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL(abx500_mask_and_set_register_interruptible);

int abx500_get_chip_id(struct device *dev)
{
	struct abx500_ops *ops;

	lookup_ops(dev->parent, &ops);
	if ((ops != NULL) && (ops->get_chip_id != NULL))
		return ops->get_chip_id(dev);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL(abx500_get_chip_id);

int abx500_event_registers_startup_state_get(struct device *dev, u8 *event)
{
	struct abx500_ops *ops;

	lookup_ops(dev->parent, &ops);
	if ((ops != NULL) && (ops->event_registers_startup_state_get != NULL))
		return ops->event_registers_startup_state_get(dev, event);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL(abx500_event_registers_startup_state_get);

int abx500_startup_irq_enabled(struct device *dev, unsigned int irq)
{
	struct abx500_ops *ops;

	lookup_ops(dev->parent, &ops);
	if ((ops != NULL) && (ops->startup_irq_enabled != NULL))
		return ops->startup_irq_enabled(dev, irq);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL(abx500_startup_irq_enabled);

MODULE_AUTHOR("Mattias Wallin <mattias.wallin@stericsson.com>");
MODULE_DESCRIPTION("ABX500 core driver");
MODULE_LICENSE("GPL");
