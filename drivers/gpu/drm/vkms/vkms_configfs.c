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
 * @planes_group: Default subgroup of @group at "/config/vkms/planes"
 * @lock: Lock used to project concurrent access to the configuration attributes
 * @config: Protected by @lock. Configuration of the VKMS device
 * @enabled: Protected by @lock. The device is created or destroyed when this
 * option changes
 */
struct vkms_configfs_device {
	struct config_group group;
	struct config_group planes_group;

	struct mutex lock;
	struct vkms_config *config;
	bool enabled;
};

/**
 * struct vkms_configfs_plane - Configfs representation of a plane
 *
 * @group: Top level configuration group that represents a plane.
 * Initialized when a new directory is created under "/config/vkms/planes"
 * @dev: The vkms_configfs_device this plane belongs to
 * @config: Configuration of the VKMS plane
 */
struct vkms_configfs_plane {
	struct config_group group;
	struct vkms_configfs_device *dev;
	struct vkms_config_plane *config;
};

#define device_item_to_vkms_configfs_device(item) \
	container_of(to_config_group((item)), struct vkms_configfs_device, \
		     group)

#define child_group_to_vkms_configfs_device(group) \
	device_item_to_vkms_configfs_device((&(group)->cg_item)->ci_parent)

#define plane_item_to_vkms_configfs_plane(item) \
	container_of(to_config_group((item)), struct vkms_configfs_plane, group)

static void plane_release(struct config_item *item)
{
	struct vkms_configfs_plane *plane;
	struct mutex *lock;

	plane = plane_item_to_vkms_configfs_plane(item);
	lock = &plane->dev->lock;

	scoped_guard(mutex, lock) {
		vkms_config_destroy_plane(plane->config);
		kfree(plane);
	}
}

static struct configfs_item_operations plane_item_operations = {
	.release	= &plane_release,
};

static const struct config_item_type plane_item_type = {
	.ct_item_ops	= &plane_item_operations,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *make_plane_group(struct config_group *group,
					     const char *name)
{
	struct vkms_configfs_device *dev;
	struct vkms_configfs_plane *plane;

	dev = child_group_to_vkms_configfs_device(group);

	scoped_guard(mutex, &dev->lock) {
		if (dev->enabled)
			return ERR_PTR(-EBUSY);

		plane = kzalloc(sizeof(*plane), GFP_KERNEL);
		if (!plane)
			return ERR_PTR(-ENOMEM);

		plane->dev = dev;

		plane->config = vkms_config_create_plane(dev->config);
		if (IS_ERR(plane->config)) {
			kfree(plane);
			return ERR_CAST(plane->config);
		}

		config_group_init_type_name(&plane->group, name, &plane_item_type);
	}

	return &plane->group;
}

static struct configfs_group_operations planes_group_operations = {
	.make_group	= &make_plane_group,
};

static const struct config_item_type plane_group_type = {
	.ct_group_ops	= &planes_group_operations,
	.ct_owner	= THIS_MODULE,
};

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

	config_group_init_type_name(&dev->planes_group, "planes",
				    &plane_group_type);
	configfs_add_default_group(&dev->planes_group, &dev->group);

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
