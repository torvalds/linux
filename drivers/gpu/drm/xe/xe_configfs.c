// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/configfs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "xe_configfs.h"
#include "xe_module.h"

/**
 * DOC: Xe Configfs
 *
 * Overview
 * =========
 *
 * Configfs is a filesystem-based manager of kernel objects. XE KMD registers a
 * configfs subsystem called ``'xe'`` that creates a directory in the mounted configfs directory
 * The user can create devices under this directory and configure them as necessary
 * See Documentation/filesystems/configfs.rst for more information about how configfs works.
 *
 * Create devices
 * ===============
 *
 * In order to create a device, the user has to create a directory inside ``'xe'``::
 *
 *	mkdir /sys/kernel/config/xe/0000:03:00.0/
 *
 * Every device created is populated by the driver with entries that can be
 * used to configure it::
 *
 *	/sys/kernel/config/xe/
 *		.. 0000:03:00.0/
 *			... survivability_mode
 *
 * Configure Attributes
 * ====================
 *
 * Survivability mode:
 * -------------------
 *
 * Enable survivability mode on supported cards. This setting only takes
 * effect when probing the device. Example to enable it::
 *
 *	# echo 1 > /sys/kernel/config/xe/0000:03:00.0/survivability_mode
 *	# echo 0000:03:00.0 > /sys/bus/pci/drivers/xe/bind  (Enters survivability mode if supported)
 *
 * Remove devices
 * ==============
 *
 * The created device directories can be removed using ``rmdir``::
 *
 *	rmdir /sys/kernel/config/xe/0000:03:00.0/
 */

struct xe_config_device {
	struct config_group group;

	bool survivability_mode;

	/* protects attributes */
	struct mutex lock;
};

static struct xe_config_device *to_xe_config_device(struct config_item *item)
{
	return container_of(to_config_group(item), struct xe_config_device, group);
}

static ssize_t survivability_mode_show(struct config_item *item, char *page)
{
	struct xe_config_device *dev = to_xe_config_device(item);

	return sprintf(page, "%d\n", dev->survivability_mode);
}

static ssize_t survivability_mode_store(struct config_item *item, const char *page, size_t len)
{
	struct xe_config_device *dev = to_xe_config_device(item);
	bool survivability_mode;
	int ret;

	ret = kstrtobool(page, &survivability_mode);
	if (ret)
		return ret;

	mutex_lock(&dev->lock);
	dev->survivability_mode = survivability_mode;
	mutex_unlock(&dev->lock);

	return len;
}

CONFIGFS_ATTR(, survivability_mode);

static struct configfs_attribute *xe_config_device_attrs[] = {
	&attr_survivability_mode,
	NULL,
};

static void xe_config_device_release(struct config_item *item)
{
	struct xe_config_device *dev = to_xe_config_device(item);

	mutex_destroy(&dev->lock);
	kfree(dev);
}

static struct configfs_item_operations xe_config_device_ops = {
	.release	= xe_config_device_release,
};

static const struct config_item_type xe_config_device_type = {
	.ct_item_ops	= &xe_config_device_ops,
	.ct_attrs	= xe_config_device_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *xe_config_make_device_group(struct config_group *group,
							const char *name)
{
	unsigned int domain, bus, slot, function;
	struct xe_config_device *dev;
	struct pci_dev *pdev;
	int ret;

	ret = sscanf(name, "%04x:%02x:%02x.%x", &domain, &bus, &slot, &function);
	if (ret != 4)
		return ERR_PTR(-EINVAL);

	pdev = pci_get_domain_bus_and_slot(domain, bus, PCI_DEVFN(slot, function));
	if (!pdev)
		return ERR_PTR(-EINVAL);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	config_group_init_type_name(&dev->group, name, &xe_config_device_type);

	mutex_init(&dev->lock);

	return &dev->group;
}

static struct configfs_group_operations xe_config_device_group_ops = {
	.make_group	= xe_config_make_device_group,
};

static const struct config_item_type xe_configfs_type = {
	.ct_group_ops	= &xe_config_device_group_ops,
	.ct_owner	= THIS_MODULE,
};

static struct configfs_subsystem xe_configfs = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "xe",
			.ci_type = &xe_configfs_type,
		},
	},
};

static struct xe_config_device *configfs_find_group(struct pci_dev *pdev)
{
	struct config_item *item;
	char name[64];

	snprintf(name, sizeof(name), "%04x:%02x:%02x.%x", pci_domain_nr(pdev->bus),
		 pdev->bus->number, PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));

	mutex_lock(&xe_configfs.su_mutex);
	item = config_group_find_item(&xe_configfs.su_group, name);
	mutex_unlock(&xe_configfs.su_mutex);

	if (!item)
		return NULL;

	return to_xe_config_device(item);
}

/**
 * xe_configfs_get_survivability_mode - get configfs survivability mode attribute
 * @pdev: pci device
 *
 * find the configfs group that belongs to the pci device and return
 * the survivability mode attribute
 *
 * Return: survivability mode if config group is found, false otherwise
 */
bool xe_configfs_get_survivability_mode(struct pci_dev *pdev)
{
	struct xe_config_device *dev = configfs_find_group(pdev);
	bool mode;

	if (!dev)
		return false;

	mode = dev->survivability_mode;
	config_item_put(&dev->group.cg_item);

	return mode;
}

/**
 * xe_configfs_clear_survivability_mode - clear configfs survivability mode attribute
 * @pdev: pci device
 *
 * find the configfs group that belongs to the pci device and clear survivability
 * mode attribute
 */
void xe_configfs_clear_survivability_mode(struct pci_dev *pdev)
{
	struct xe_config_device *dev = configfs_find_group(pdev);

	if (!dev)
		return;

	mutex_lock(&dev->lock);
	dev->survivability_mode = 0;
	mutex_unlock(&dev->lock);

	config_item_put(&dev->group.cg_item);
}

int __init xe_configfs_init(void)
{
	struct config_group *root = &xe_configfs.su_group;
	int ret;

	config_group_init(root);
	mutex_init(&xe_configfs.su_mutex);
	ret = configfs_register_subsystem(&xe_configfs);
	if (ret) {
		pr_err("Error %d while registering %s subsystem\n",
		       ret, root->cg_item.ci_namebuf);
		return ret;
	}

	return 0;
}

void __exit xe_configfs_exit(void)
{
	configfs_unregister_subsystem(&xe_configfs);
}

