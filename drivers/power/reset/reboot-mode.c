// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/reboot-mode.h>
#include <linux/slab.h>
#include <linux/string.h>

#define PREFIX "mode-"

struct mode_info {
	const char *mode;
	u32 magic;
	struct list_head list;
};

struct reboot_mode_sysfs_data {
	struct device *reboot_mode_device;
	struct list_head head;
};

static inline void reboot_mode_release_list(struct reboot_mode_sysfs_data *priv)
{
	struct mode_info *info;
	struct mode_info *next;

	list_for_each_entry_safe(info, next, &priv->head, list) {
		list_del(&info->list);
		kfree_const(info->mode);
		kfree(info);
	}
}

static ssize_t reboot_modes_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct reboot_mode_sysfs_data *priv;
	struct mode_info *sysfs_info;
	ssize_t size = 0;

	priv = dev_get_drvdata(dev);
	if (!priv)
		return -ENODATA;

	list_for_each_entry(sysfs_info, &priv->head, list)
		size += sysfs_emit_at(buf, size, "%s ", sysfs_info->mode);

	if (!size)
		return -ENODATA;

	return size + sysfs_emit_at(buf, size - 1, "\n");
}
static DEVICE_ATTR_RO(reboot_modes);

static struct attribute *reboot_mode_attrs[] = {
	&dev_attr_reboot_modes.attr,
	NULL,
};
ATTRIBUTE_GROUPS(reboot_mode);

static const struct class reboot_mode_class = {
	.name = "reboot-mode",
	.dev_groups = reboot_mode_groups,
};

static unsigned int get_reboot_mode_magic(struct reboot_mode_driver *reboot,
					  const char *cmd)
{
	const char *normal = "normal";
	struct mode_info *info;
	char cmd_[110];

	if (!cmd)
		cmd = normal;

	list_for_each_entry(info, &reboot->head, list)
		if (!strcmp(info->mode, cmd))
			return info->magic;

	/* try to match again, replacing characters impossible in DT */
	if (strscpy(cmd_, cmd, sizeof(cmd_)) == -E2BIG)
		return 0;

	strreplace(cmd_, ' ', '-');
	strreplace(cmd_, ',', '-');
	strreplace(cmd_, '/', '-');

	list_for_each_entry(info, &reboot->head, list)
		if (!strcmp(info->mode, cmd_))
			return info->magic;

	return 0;
}

static int reboot_mode_notify(struct notifier_block *this,
			      unsigned long mode, void *cmd)
{
	struct reboot_mode_driver *reboot;
	unsigned int magic;

	reboot = container_of(this, struct reboot_mode_driver, reboot_notifier);
	magic = get_reboot_mode_magic(reboot, cmd);
	if (magic)
		reboot->write(reboot, magic);

	return NOTIFY_DONE;
}

static int reboot_mode_create_device(struct reboot_mode_driver *reboot)
{
	struct reboot_mode_sysfs_data *priv;
	struct mode_info *sysfs_info;
	struct mode_info *info;
	int ret;

	priv = kzalloc_obj(*priv, GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	INIT_LIST_HEAD(&priv->head);

	list_for_each_entry(info, &reboot->head, list) {
		sysfs_info = kzalloc_obj(*sysfs_info, GFP_KERNEL);
		if (!sysfs_info) {
			ret = -ENOMEM;
			goto error;
		}

		sysfs_info->mode = kstrdup_const(info->mode, GFP_KERNEL);
		if (!sysfs_info->mode) {
			kfree(sysfs_info);
			ret = -ENOMEM;
			goto error;
		}

		list_add_tail(&sysfs_info->list, &priv->head);
	}

	priv->reboot_mode_device = device_create(&reboot_mode_class, NULL, 0,
						 (void *)priv, reboot->dev->driver->name);
	if (IS_ERR(priv->reboot_mode_device)) {
		ret = PTR_ERR(priv->reboot_mode_device);
		goto error;
	}

	return 0;

error:
	reboot_mode_release_list(priv);
	kfree(priv);
	return ret;
}

/**
 * reboot_mode_register - register a reboot mode driver
 * @reboot: reboot mode driver
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int reboot_mode_register(struct reboot_mode_driver *reboot)
{
	struct mode_info *info;
	struct property *prop;
	struct device_node *np = reboot->dev->of_node;
	size_t len = strlen(PREFIX);
	int ret;

	INIT_LIST_HEAD(&reboot->head);

	for_each_property_of_node(np, prop) {
		if (strncmp(prop->name, PREFIX, len))
			continue;

		info = devm_kzalloc(reboot->dev, sizeof(*info), GFP_KERNEL);
		if (!info) {
			ret = -ENOMEM;
			goto error;
		}

		if (of_property_read_u32(np, prop->name, &info->magic)) {
			dev_err(reboot->dev, "reboot mode %s without magic number\n",
				info->mode);
			devm_kfree(reboot->dev, info);
			continue;
		}

		info->mode = kstrdup_const(prop->name + len, GFP_KERNEL);
		if (!info->mode) {
			ret =  -ENOMEM;
			goto error;
		} else if (info->mode[0] == '\0') {
			kfree_const(info->mode);
			ret = -EINVAL;
			dev_err(reboot->dev, "invalid mode name(%s): too short!\n",
				prop->name);
			goto error;
		}

		list_add_tail(&info->list, &reboot->head);
	}

	reboot->reboot_notifier.notifier_call = reboot_mode_notify;
	register_reboot_notifier(&reboot->reboot_notifier);

	ret = reboot_mode_create_device(reboot);
	if (ret)
		goto error;

	return 0;

error:
	reboot_mode_unregister(reboot);
	return ret;
}
EXPORT_SYMBOL_GPL(reboot_mode_register);

static int reboot_mode_match_by_name(struct device *dev, const void *data)
{
	const char *name = data;

	if (!dev || !data)
		return 0;

	return dev_name(dev) && strcmp(dev_name(dev), name) == 0;
}

static inline void reboot_mode_unregister_device(struct reboot_mode_driver *reboot)
{
	struct reboot_mode_sysfs_data *priv;
	struct device *reboot_mode_device;

	reboot_mode_device = class_find_device(&reboot_mode_class, NULL, reboot->dev->driver->name,
					       reboot_mode_match_by_name);

	if (!reboot_mode_device)
		return;

	priv = dev_get_drvdata(reboot_mode_device);
	device_unregister(reboot_mode_device);

	if (!priv)
		return;

	reboot_mode_release_list(priv);
	kfree(priv);
}

/**
 * reboot_mode_unregister - unregister a reboot mode driver
 * @reboot: reboot mode driver
 */
int reboot_mode_unregister(struct reboot_mode_driver *reboot)
{
	struct mode_info *info;

	unregister_reboot_notifier(&reboot->reboot_notifier);
	reboot_mode_unregister_device(reboot);

	list_for_each_entry(info, &reboot->head, list)
		kfree_const(info->mode);

	return 0;
}
EXPORT_SYMBOL_GPL(reboot_mode_unregister);

static void devm_reboot_mode_release(struct device *dev, void *res)
{
	reboot_mode_unregister(*(struct reboot_mode_driver **)res);
}

/**
 * devm_reboot_mode_register() - resource managed reboot_mode_register()
 * @dev: device to associate this resource with
 * @reboot: reboot mode driver
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int devm_reboot_mode_register(struct device *dev,
			      struct reboot_mode_driver *reboot)
{
	struct reboot_mode_driver **dr;
	int rc;

	dr = devres_alloc(devm_reboot_mode_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	rc = reboot_mode_register(reboot);
	if (rc) {
		devres_free(dr);
		return rc;
	}

	*dr = reboot;
	devres_add(dev, dr);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_reboot_mode_register);

static int devm_reboot_mode_match(struct device *dev, void *res, void *data)
{
	struct reboot_mode_driver **p = res;

	if (WARN_ON(!p || !*p))
		return 0;

	return *p == data;
}

/**
 * devm_reboot_mode_unregister() - resource managed reboot_mode_unregister()
 * @dev: device to associate this resource with
 * @reboot: reboot mode driver
 */
void devm_reboot_mode_unregister(struct device *dev,
				 struct reboot_mode_driver *reboot)
{
	WARN_ON(devres_release(dev,
			       devm_reboot_mode_release,
			       devm_reboot_mode_match, reboot));
}
EXPORT_SYMBOL_GPL(devm_reboot_mode_unregister);

static int __init reboot_mode_init(void)
{
	return class_register(&reboot_mode_class);
}

static void __exit reboot_mode_exit(void)
{
	class_unregister(&reboot_mode_class);
}

subsys_initcall(reboot_mode_init);
module_exit(reboot_mode_exit);

MODULE_AUTHOR("Andy Yan <andy.yan@rock-chips.com>");
MODULE_DESCRIPTION("System reboot mode core library");
MODULE_LICENSE("GPL v2");
