/*
 * The Industrial I/O core, software IIO devices functions
 *
 * Copyright (c) 2016 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/slab.h>

#include <linux/iio/sw_device.h>
#include <linux/iio/configfs.h>
#include <linux/configfs.h>

static struct config_group *iio_devices_group;
static const struct config_item_type iio_device_type_group_type;

static const struct config_item_type iio_devices_group_type = {
	.ct_owner = THIS_MODULE,
};

static LIST_HEAD(iio_device_types_list);
static DEFINE_MUTEX(iio_device_types_lock);

static
struct iio_sw_device_type *__iio_find_sw_device_type(const char *name,
						     unsigned len)
{
	struct iio_sw_device_type *d = NULL, *iter;

	list_for_each_entry(iter, &iio_device_types_list, list)
		if (!strcmp(iter->name, name)) {
			d = iter;
			break;
		}

	return d;
}

int iio_register_sw_device_type(struct iio_sw_device_type *d)
{
	struct iio_sw_device_type *iter;
	int ret = 0;

	mutex_lock(&iio_device_types_lock);
	iter = __iio_find_sw_device_type(d->name, strlen(d->name));
	if (iter)
		ret = -EBUSY;
	else
		list_add_tail(&d->list, &iio_device_types_list);
	mutex_unlock(&iio_device_types_lock);

	if (ret)
		return ret;

	d->group = configfs_register_default_group(iio_devices_group, d->name,
						&iio_device_type_group_type);
	if (IS_ERR(d->group))
		ret = PTR_ERR(d->group);

	return ret;
}
EXPORT_SYMBOL(iio_register_sw_device_type);

void iio_unregister_sw_device_type(struct iio_sw_device_type *dt)
{
	struct iio_sw_device_type *iter;

	mutex_lock(&iio_device_types_lock);
	iter = __iio_find_sw_device_type(dt->name, strlen(dt->name));
	if (iter)
		list_del(&dt->list);
	mutex_unlock(&iio_device_types_lock);

	configfs_unregister_default_group(dt->group);
}
EXPORT_SYMBOL(iio_unregister_sw_device_type);

static
struct iio_sw_device_type *iio_get_sw_device_type(const char *name)
{
	struct iio_sw_device_type *dt;

	mutex_lock(&iio_device_types_lock);
	dt = __iio_find_sw_device_type(name, strlen(name));
	if (dt && !try_module_get(dt->owner))
		dt = NULL;
	mutex_unlock(&iio_device_types_lock);

	return dt;
}

struct iio_sw_device *iio_sw_device_create(const char *type, const char *name)
{
	struct iio_sw_device *d;
	struct iio_sw_device_type *dt;

	dt = iio_get_sw_device_type(type);
	if (!dt) {
		pr_err("Invalid device type: %s\n", type);
		return ERR_PTR(-EINVAL);
	}
	d = dt->ops->probe(name);
	if (IS_ERR(d))
		goto out_module_put;

	d->device_type = dt;

	return d;
out_module_put:
	module_put(dt->owner);
	return d;
}
EXPORT_SYMBOL(iio_sw_device_create);

void iio_sw_device_destroy(struct iio_sw_device *d)
{
	struct iio_sw_device_type *dt = d->device_type;

	dt->ops->remove(d);
	module_put(dt->owner);
}
EXPORT_SYMBOL(iio_sw_device_destroy);

static struct config_group *device_make_group(struct config_group *group,
					      const char *name)
{
	struct iio_sw_device *d;

	d = iio_sw_device_create(group->cg_item.ci_name, name);
	if (IS_ERR(d))
		return ERR_CAST(d);

	config_item_set_name(&d->group.cg_item, "%s", name);

	return &d->group;
}

static void device_drop_group(struct config_group *group,
			      struct config_item *item)
{
	struct iio_sw_device *d = to_iio_sw_device(item);

	iio_sw_device_destroy(d);
	config_item_put(item);
}

static struct configfs_group_operations device_ops = {
	.make_group	= &device_make_group,
	.drop_item	= &device_drop_group,
};

static const struct config_item_type iio_device_type_group_type = {
	.ct_group_ops = &device_ops,
	.ct_owner       = THIS_MODULE,
};

static int __init iio_sw_device_init(void)
{
	iio_devices_group =
		configfs_register_default_group(&iio_configfs_subsys.su_group,
						"devices",
						&iio_devices_group_type);
	return PTR_ERR_OR_ZERO(iio_devices_group);
}
module_init(iio_sw_device_init);

static void __exit iio_sw_device_exit(void)
{
	configfs_unregister_default_group(iio_devices_group);
}
module_exit(iio_sw_device_exit);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@intel.com>");
MODULE_DESCRIPTION("Industrial I/O software devices support");
MODULE_LICENSE("GPL v2");
