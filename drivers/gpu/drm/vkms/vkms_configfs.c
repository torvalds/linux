// SPDX-License-Identifier: GPL-2.0+
#include <linux/cleanup.h>
#include <linux/configfs.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include "vkms_drv.h"
#include "vkms_config.h"
#include "vkms_configfs.h"

/* To avoid registering configfs more than once or unregistering on error */
static bool is_configfs_registered;

/**
 * struct vkms_configfs_device - Configfs representation of a VKMS device
 *
 * @group: Top level configuration group that represents a VKMS device.
 * Initialized when a new directory is created under "/config/vkms/"
 * @lock: Lock used to project concurrent access to the configuration attributes
 * @config: Protected by @lock. Configuration of the VKMS device
 * @enabled: Protected by @lock. The device is created or destroyed when this
 * option changes
 */
struct vkms_configfs_device {
	struct config_group group;

	struct mutex lock;
	struct vkms_config *config;
	bool enabled;
};

#define device_item_to_vkms_configfs_device(item) \
	container_of(to_config_group((item)), struct vkms_configfs_device, \
		     group)

static ssize_t device_enabled_show(struct config_item *item, char *page)
{
	struct vkms_configfs_device *dev;
	bool enabled;

	dev = device_item_to_vkms_configfs_device(item);

	scoped_guard(mutex, &dev->lock)
		enabled = dev->enabled;

	return sprintf(page, "%d\n", enabled);
}

static ssize_t device_enabled_store(struct config_item *item, const char *page,
				    size_t count)
{
	struct vkms_configfs_device *dev;
	bool enabled;
	int ret = 0;

	dev = device_item_to_vkms_configfs_device(item);

	if (kstrtobool(page, &enabled))
		return -EINVAL;

	scoped_guard(mutex, &dev->lock) {
		if (!dev->enabled && enabled) {
			if (!vkms_config_is_valid(dev->config))
				return -EINVAL;

			ret = vkms_create(dev->config);
			if (ret)
				return ret;
		} else if (dev->enabled && !enabled) {
			vkms_destroy(dev->config);
		}

		dev->enabled = enabled;
	}

	return (ssize_t)count;
}

CONFIGFS_ATTR(device_, enabled);

static struct configfs_attribute *device_item_attrs[] = {
	&device_attr_enabled,
	NULL,
};

static void device_release(struct config_item *item)
{
	struct vkms_configfs_device *dev;

	dev = device_item_to_vkms_configfs_device(item);

	if (dev->enabled)
		vkms_destroy(dev->config);

	mutex_destroy(&dev->lock);
	vkms_config_destroy(dev->config);
	kfree(dev);
}

static struct configfs_item_operations device_item_operations = {
	.release	= &device_release,
};

static const struct config_item_type device_item_type = {
	.ct_attrs	= device_item_attrs,
	.ct_item_ops	= &device_item_operations,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *make_device_group(struct config_group *group,
					      const char *name)
{
	struct vkms_configfs_device *dev;

	if (strcmp(name, DEFAULT_DEVICE_NAME) == 0)
		return ERR_PTR(-EINVAL);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->config = vkms_config_create(name);
	if (IS_ERR(dev->config)) {
		kfree(dev);
		return ERR_CAST(dev->config);
	}

	config_group_init_type_name(&dev->group, name, &device_item_type);
	mutex_init(&dev->lock);

	return &dev->group;
}

static struct configfs_group_operations device_group_ops = {
	.make_group = &make_device_group,
};

static const struct config_item_type device_group_type = {
	.ct_group_ops	= &device_group_ops,
	.ct_owner	= THIS_MODULE,
};

static struct configfs_subsystem vkms_subsys = {
	.su_group = {
		.cg_item = {
			.ci_name = "vkms",
			.ci_type = &device_group_type,
		},
	},
	.su_mutex = __MUTEX_INITIALIZER(vkms_subsys.su_mutex),
};

int vkms_configfs_register(void)
{
	int ret;

	if (is_configfs_registered)
		return 0;

	config_group_init(&vkms_subsys.su_group);
	ret = configfs_register_subsystem(&vkms_subsys);

	is_configfs_registered = ret == 0;

	return ret;
}

void vkms_configfs_unregister(void)
{
	if (is_configfs_registered)
		configfs_unregister_subsystem(&vkms_subsys);
}
