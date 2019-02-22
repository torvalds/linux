// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/reboot-mode.h>
#include <linux/sysfs.h>

#define PREFIX "mode-"

struct mode_info {
	const char *mode;
	u32 magic;
	struct list_head list;
};

static const char *boot_mode = "coldboot";

static ssize_t boot_mode_show(struct kobject *kobj, struct kobj_attribute *attr,
			      char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", boot_mode);
}

static struct kobj_attribute kobj_boot_mode = __ATTR_RO(boot_mode);

static int get_reboot_mode_magic(struct reboot_mode_driver *reboot,
				 const char *cmd)
{
	const char *normal = "normal";
	int magic = 0;
	struct mode_info *info;

	if (!cmd || !cmd[0])
		cmd = normal;

	list_for_each_entry(info, &reboot->head, list) {
		if (!strcmp(info->mode, cmd)) {
			magic = info->magic;
			break;
		}
	}

	return magic;
}

static void reboot_mode_write(struct reboot_mode_driver *reboot,
			      const void *cmd)
{
	int magic;

	magic = get_reboot_mode_magic(reboot, cmd);
	if (magic)
		reboot->write(reboot, magic);
}

static int reboot_mode_notify(struct notifier_block *this,
			      unsigned long mode, void *cmd)
{
	struct reboot_mode_driver *reboot;

	reboot = container_of(this, struct reboot_mode_driver, reboot_notifier);
	reboot_mode_write(reboot, cmd);

	return NOTIFY_DONE;
}

static int reboot_mode_panic_notify(struct notifier_block *this,
				      unsigned long ev, void *ptr)
{
	struct reboot_mode_driver *reboot;
	const char *cmd = "panic";

	reboot = container_of(this, struct reboot_mode_driver, panic_notifier);
	reboot_mode_write(reboot, cmd);

	return NOTIFY_DONE;
}

static int boot_mode_parse(struct reboot_mode_driver *reboot)
{
	struct mode_info *info;
	unsigned int magic = reboot->read(reboot);

	list_for_each_entry(info, &reboot->head, list) {
		if (info->magic == magic) {
			boot_mode = info->mode;
			break;
		}
	}

	return 0;
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

	boot_mode_parse(reboot);
	reboot->reboot_notifier.notifier_call = reboot_mode_notify;
	reboot->panic_notifier.notifier_call = reboot_mode_panic_notify;
	register_reboot_notifier(&reboot->reboot_notifier);
	atomic_notifier_chain_register(&panic_notifier_list,
				       &reboot->panic_notifier);
	ret = sysfs_create_file(kernel_kobj, &kobj_boot_mode.attr);

	return ret;

error:
	list_for_each_entry(info, &reboot->head, list)
		kfree_const(info->mode);

	return ret;
}
EXPORT_SYMBOL_GPL(reboot_mode_register);

/**
 * reboot_mode_unregister - unregister a reboot mode driver
 * @reboot: reboot mode driver
 */
int reboot_mode_unregister(struct reboot_mode_driver *reboot)
{
	struct mode_info *info;

	unregister_reboot_notifier(&reboot->reboot_notifier);

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

MODULE_AUTHOR("Andy Yan <andy.yan@rock-chips.com>");
MODULE_DESCRIPTION("System reboot mode core library");
MODULE_LICENSE("GPL v2");
