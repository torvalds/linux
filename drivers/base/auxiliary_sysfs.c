// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES
 */

#include <linux/auxiliary_bus.h>
#include <linux/slab.h>

#define AUXILIARY_MAX_IRQ_NAME 11

struct auxiliary_irq_info {
	struct device_attribute sysfs_attr;
	char name[AUXILIARY_MAX_IRQ_NAME];
};

static struct attribute *auxiliary_irq_attrs[] = {
	NULL
};

static const struct attribute_group auxiliary_irqs_group = {
	.name = "irqs",
	.attrs = auxiliary_irq_attrs,
};

static int auxiliary_irq_dir_prepare(struct auxiliary_device *auxdev)
{
	int ret = 0;

	guard(mutex)(&auxdev->sysfs.lock);
	if (auxdev->sysfs.irq_dir_exists)
		return 0;

	ret = devm_device_add_group(&auxdev->dev, &auxiliary_irqs_group);
	if (ret)
		return ret;

	auxdev->sysfs.irq_dir_exists = true;
	xa_init(&auxdev->sysfs.irqs);
	return 0;
}

/**
 * auxiliary_device_sysfs_irq_add - add a sysfs entry for the given IRQ
 * @auxdev: auxiliary bus device to add the sysfs entry.
 * @irq: The associated interrupt number.
 *
 * This function should be called after auxiliary device have successfully
 * received the irq.
 * The driver is responsible to add a unique irq for the auxiliary device. The
 * driver can invoke this function from multiple thread context safely for
 * unique irqs of the auxiliary devices. The driver must not invoke this API
 * multiple times if the irq is already added previously.
 *
 * Return: zero on success or an error code on failure.
 */
int auxiliary_device_sysfs_irq_add(struct auxiliary_device *auxdev, int irq)
{
	struct auxiliary_irq_info *info __free(kfree) = NULL;
	struct device *dev = &auxdev->dev;
	int ret;

	ret = auxiliary_irq_dir_prepare(auxdev);
	if (ret)
		return ret;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	sysfs_attr_init(&info->sysfs_attr.attr);
	snprintf(info->name, AUXILIARY_MAX_IRQ_NAME, "%d", irq);

	ret = xa_insert(&auxdev->sysfs.irqs, irq, info, GFP_KERNEL);
	if (ret)
		return ret;

	info->sysfs_attr.attr.name = info->name;
	ret = sysfs_add_file_to_group(&dev->kobj, &info->sysfs_attr.attr,
				      auxiliary_irqs_group.name);
	if (ret)
		goto sysfs_add_err;

	xa_store(&auxdev->sysfs.irqs, irq, no_free_ptr(info), GFP_KERNEL);
	return 0;

sysfs_add_err:
	xa_erase(&auxdev->sysfs.irqs, irq);
	return ret;
}
EXPORT_SYMBOL_GPL(auxiliary_device_sysfs_irq_add);

/**
 * auxiliary_device_sysfs_irq_remove - remove a sysfs entry for the given IRQ
 * @auxdev: auxiliary bus device to add the sysfs entry.
 * @irq: the IRQ to remove.
 *
 * This function should be called to remove an IRQ sysfs entry.
 * The driver must invoke this API when IRQ is released by the device.
 */
void auxiliary_device_sysfs_irq_remove(struct auxiliary_device *auxdev, int irq)
{
	struct auxiliary_irq_info *info __free(kfree) = xa_load(&auxdev->sysfs.irqs, irq);
	struct device *dev = &auxdev->dev;

	if (!info) {
		dev_err(&auxdev->dev, "IRQ %d doesn't exist\n", irq);
		return;
	}
	sysfs_remove_file_from_group(&dev->kobj, &info->sysfs_attr.attr,
				     auxiliary_irqs_group.name);
	xa_erase(&auxdev->sysfs.irqs, irq);
}
EXPORT_SYMBOL_GPL(auxiliary_device_sysfs_irq_remove);
