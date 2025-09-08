// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/bitops.h>
#include <linux/configfs.h>
#include <linux/find.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/string.h>

#include "xe_configfs.h"
#include "xe_module.h"

#include "xe_hw_engine_types.h"

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
 * Allowed engines:
 * ----------------
 *
 * Allow only a set of engine(s) to be available, disabling the other engines
 * even if they are available in hardware. This is applied after HW fuses are
 * considered on each tile. Examples:
 *
 * Allow only one render and one copy engines, nothing else::
 *
 *	# echo 'rcs0,bcs0' > /sys/kernel/config/xe/0000:03:00.0/engines_allowed
 *
 * Allow only compute engines and first copy engine::
 *
 *	# echo 'ccs*,bcs0' > /sys/kernel/config/xe/0000:03:00.0/engines_allowed
 *
 * Note that the engine names are the per-GT hardware names. On multi-tile
 * platforms, writing ``rcs0,bcs0`` to this file would allow the first render
 * and copy engines on each tile.
 *
 * The requested configuration may not be supported by the platform and driver
 * may fail to probe. For example: if at least one copy engine is expected to be
 * available for migrations, but it's disabled. This is intended for debugging
 * purposes only.
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
	u64 engines_allowed;

	/* protects attributes */
	struct mutex lock;
};

struct engine_info {
	const char *cls;
	u64 mask;
};

/* Some helpful macros to aid on the sizing of buffer allocation when parsing */
#define MAX_ENGINE_CLASS_CHARS 5
#define MAX_ENGINE_INSTANCE_CHARS 2

static const struct engine_info engine_info[] = {
	{ .cls = "rcs", .mask = XE_HW_ENGINE_RCS_MASK },
	{ .cls = "bcs", .mask = XE_HW_ENGINE_BCS_MASK },
	{ .cls = "vcs", .mask = XE_HW_ENGINE_VCS_MASK },
	{ .cls = "vecs", .mask = XE_HW_ENGINE_VECS_MASK },
	{ .cls = "ccs", .mask = XE_HW_ENGINE_CCS_MASK },
	{ .cls = "gsccs", .mask = XE_HW_ENGINE_GSCCS_MASK },
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

static ssize_t engines_allowed_show(struct config_item *item, char *page)
{
	struct xe_config_device *dev = to_xe_config_device(item);
	char *p = page;

	for (size_t i = 0; i < ARRAY_SIZE(engine_info); i++) {
		u64 mask = engine_info[i].mask;

		if ((dev->engines_allowed & mask) == mask) {
			p += sprintf(p, "%s*\n", engine_info[i].cls);
		} else if (mask & dev->engines_allowed) {
			u16 bit0 = __ffs64(mask), bit;

			mask &= dev->engines_allowed;

			for_each_set_bit(bit, (const unsigned long *)&mask, 64)
				p += sprintf(p, "%s%u\n", engine_info[i].cls,
					     bit - bit0);
		}
	}

	return p - page;
}

static bool lookup_engine_mask(const char *pattern, u64 *mask)
{
	for (size_t i = 0; i < ARRAY_SIZE(engine_info); i++) {
		u8 instance;
		u16 bit;

		if (!str_has_prefix(pattern, engine_info[i].cls))
			continue;

		pattern += strlen(engine_info[i].cls);

		if (!strcmp(pattern, "*")) {
			*mask = engine_info[i].mask;
			return true;
		}

		if (kstrtou8(pattern, 10, &instance))
			return false;

		bit = __ffs64(engine_info[i].mask) + instance;
		if (bit >= fls64(engine_info[i].mask))
			return false;

		*mask = BIT_ULL(bit);
		return true;
	}

	return false;
}

static ssize_t engines_allowed_store(struct config_item *item, const char *page,
				     size_t len)
{
	struct xe_config_device *dev = to_xe_config_device(item);
	size_t patternlen, p;
	u64 mask, val = 0;

	for (p = 0; p < len; p += patternlen + 1) {
		char buf[MAX_ENGINE_CLASS_CHARS + MAX_ENGINE_INSTANCE_CHARS + 1];

		patternlen = strcspn(page + p, ",\n");
		if (patternlen >= sizeof(buf))
			return -EINVAL;

		memcpy(buf, page + p, patternlen);
		buf[patternlen] = '\0';

		if (!lookup_engine_mask(buf, &mask))
			return -EINVAL;

		val |= mask;
	}

	mutex_lock(&dev->lock);
	dev->engines_allowed = val;
	mutex_unlock(&dev->lock);

	return len;
}

CONFIGFS_ATTR(, survivability_mode);
CONFIGFS_ATTR(, engines_allowed);

static struct configfs_attribute *xe_config_device_attrs[] = {
	&attr_survivability_mode,
	&attr_engines_allowed,
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
		return ERR_PTR(-ENODEV);
	pci_dev_put(pdev);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	/* Default values */
	dev->engines_allowed = U64_MAX;

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

/**
 * xe_configfs_get_engines_allowed - get engine allowed mask from configfs
 * @pdev: pci device
 *
 * Find the configfs group that belongs to the pci device and return
 * the mask of engines allowed to be used.
 *
 * Return: engine mask with allowed engines
 */
u64 xe_configfs_get_engines_allowed(struct pci_dev *pdev)
{
	struct xe_config_device *dev = configfs_find_group(pdev);
	u64 engines_allowed;

	if (!dev)
		return U64_MAX;

	engines_allowed = dev->engines_allowed;
	config_item_put(&dev->group.cg_item);

	return engines_allowed;
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

