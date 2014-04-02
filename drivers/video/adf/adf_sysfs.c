/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <video/adf_client.h>

#include "adf.h"
#include "adf_fops.h"
#include "adf_sysfs.h"

static struct class *adf_class;
static int adf_major;
static DEFINE_IDR(adf_minors);

#define dev_to_adf_interface(p) \
	adf_obj_to_interface(container_of(p, struct adf_obj, dev))

static ssize_t dpms_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct adf_interface *intf = dev_to_adf_interface(dev);
	return scnprintf(buf, PAGE_SIZE, "%u\n",
			adf_interface_dpms_state(intf));
}

static ssize_t dpms_state_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct adf_interface *intf = dev_to_adf_interface(dev);
	u8 dpms_state;
	int err;

	err = kstrtou8(buf, 0, &dpms_state);
	if (err < 0)
		return err;

	err = adf_interface_blank(intf, dpms_state);
	if (err < 0)
		return err;

	return count;
}

static ssize_t current_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct adf_interface *intf = dev_to_adf_interface(dev);
	struct drm_mode_modeinfo mode;

	adf_interface_current_mode(intf, &mode);

	if (mode.name[0]) {
		return scnprintf(buf, PAGE_SIZE, "%s\n", mode.name);
	} else {
		bool interlaced = !!(mode.flags & DRM_MODE_FLAG_INTERLACE);
		return scnprintf(buf, PAGE_SIZE, "%ux%u%s\n", mode.hdisplay,
				mode.vdisplay, interlaced ? "i" : "");
	}
}

static ssize_t type_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct adf_interface *intf = dev_to_adf_interface(dev);
	return scnprintf(buf, PAGE_SIZE, "%s\n",
			adf_interface_type_str(intf));
}

static ssize_t vsync_timestamp_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct adf_interface *intf = dev_to_adf_interface(dev);
	ktime_t timestamp;
	unsigned long flags;

	read_lock_irqsave(&intf->vsync_lock, flags);
	memcpy(&timestamp, &intf->vsync_timestamp, sizeof(timestamp));
	read_unlock_irqrestore(&intf->vsync_lock, flags);

	return scnprintf(buf, PAGE_SIZE, "%llu\n", ktime_to_ns(timestamp));
}

static ssize_t hotplug_detect_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct adf_interface *intf = dev_to_adf_interface(dev);
	return scnprintf(buf, PAGE_SIZE, "%u\n", intf->hotplug_detect);
}

static struct device_attribute adf_interface_attrs[] = {
	__ATTR(dpms_state, S_IRUGO|S_IWUSR, dpms_state_show, dpms_state_store),
	__ATTR_RO(current_mode),
	__ATTR_RO(hotplug_detect),
	__ATTR_RO(type),
	__ATTR_RO(vsync_timestamp),
};

int adf_obj_sysfs_init(struct adf_obj *obj, struct device *parent)
{
	int ret = idr_alloc(&adf_minors, obj, 0, 0, GFP_KERNEL);
	if (ret < 0) {
		pr_err("%s: allocating adf minor failed: %d\n", __func__,
				ret);
		return ret;
	}

	obj->minor = ret;
	obj->dev.parent = parent;
	obj->dev.class = adf_class;
	obj->dev.devt = MKDEV(adf_major, obj->minor);

	ret = device_register(&obj->dev);
	if (ret < 0) {
		pr_err("%s: registering adf object failed: %d\n", __func__,
				ret);
		goto err_device_register;
	}

	return 0;

err_device_register:
	idr_remove(&adf_minors, obj->minor);
	return ret;
}

static char *adf_device_devnode(struct device *dev, umode_t *mode,
		kuid_t *uid, kgid_t *gid)
{
	struct adf_obj *obj = container_of(dev, struct adf_obj, dev);
	return kasprintf(GFP_KERNEL, "adf%d", obj->id);
}

static char *adf_interface_devnode(struct device *dev, umode_t *mode,
		kuid_t *uid, kgid_t *gid)
{
	struct adf_obj *obj = container_of(dev, struct adf_obj, dev);
	struct adf_interface *intf = adf_obj_to_interface(obj);
	struct adf_device *parent = adf_interface_parent(intf);
	return kasprintf(GFP_KERNEL, "adf-interface%d.%d",
			parent->base.id, intf->base.id);
}

static char *adf_overlay_engine_devnode(struct device *dev, umode_t *mode,
		kuid_t *uid, kgid_t *gid)
{
	struct adf_obj *obj = container_of(dev, struct adf_obj, dev);
	struct adf_overlay_engine *eng = adf_obj_to_overlay_engine(obj);
	struct adf_device *parent = adf_overlay_engine_parent(eng);
	return kasprintf(GFP_KERNEL, "adf-overlay-engine%d.%d",
			parent->base.id, eng->base.id);
}

static void adf_noop_release(struct device *dev)
{
}

static struct device_type adf_device_type = {
	.name = "adf_device",
	.devnode = adf_device_devnode,
	.release = adf_noop_release,
};

static struct device_type adf_interface_type = {
	.name = "adf_interface",
	.devnode = adf_interface_devnode,
	.release = adf_noop_release,
};

static struct device_type adf_overlay_engine_type = {
	.name = "adf_overlay_engine",
	.devnode = adf_overlay_engine_devnode,
	.release = adf_noop_release,
};

int adf_device_sysfs_init(struct adf_device *dev)
{
	dev->base.dev.type = &adf_device_type;
	dev_set_name(&dev->base.dev, "%s", dev->base.name);
	return adf_obj_sysfs_init(&dev->base, dev->dev);
}

int adf_interface_sysfs_init(struct adf_interface *intf)
{
	struct adf_device *parent = adf_interface_parent(intf);
	size_t i, j;
	int ret;

	intf->base.dev.type = &adf_interface_type;
	dev_set_name(&intf->base.dev, "%s-interface%d", parent->base.name,
			intf->base.id);

	ret = adf_obj_sysfs_init(&intf->base, &parent->base.dev);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(adf_interface_attrs); i++) {
		ret = device_create_file(&intf->base.dev,
				&adf_interface_attrs[i]);
		if (ret < 0) {
			dev_err(&intf->base.dev, "creating sysfs attribute %s failed: %d\n",
					adf_interface_attrs[i].attr.name, ret);
			goto err;
		}
	}

	return 0;

err:
	for (j = 0; j < i; j++)
		device_remove_file(&intf->base.dev, &adf_interface_attrs[j]);
	return ret;
}

int adf_overlay_engine_sysfs_init(struct adf_overlay_engine *eng)
{
	struct adf_device *parent = adf_overlay_engine_parent(eng);

	eng->base.dev.type = &adf_overlay_engine_type;
	dev_set_name(&eng->base.dev, "%s-overlay-engine%d", parent->base.name,
			eng->base.id);

	return adf_obj_sysfs_init(&eng->base, &parent->base.dev);
}

struct adf_obj *adf_obj_sysfs_find(int minor)
{
	return idr_find(&adf_minors, minor);
}

void adf_obj_sysfs_destroy(struct adf_obj *obj)
{
	idr_remove(&adf_minors, obj->minor);
	device_unregister(&obj->dev);
}

void adf_device_sysfs_destroy(struct adf_device *dev)
{
	adf_obj_sysfs_destroy(&dev->base);
}

void adf_interface_sysfs_destroy(struct adf_interface *intf)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(adf_interface_attrs); i++)
		device_remove_file(&intf->base.dev, &adf_interface_attrs[i]);
	adf_obj_sysfs_destroy(&intf->base);
}

void adf_overlay_engine_sysfs_destroy(struct adf_overlay_engine *eng)
{
	adf_obj_sysfs_destroy(&eng->base);
}

int adf_sysfs_init(void)
{
	struct class *class;
	int ret;

	class = class_create(THIS_MODULE, "adf");
	if (IS_ERR(class)) {
		ret = PTR_ERR(class);
		pr_err("%s: creating class failed: %d\n", __func__, ret);
		return ret;
	}

	ret = register_chrdev(0, "adf", &adf_fops);
	if (ret < 0) {
		pr_err("%s: registering device failed: %d\n", __func__, ret);
		goto err_chrdev;
	}

	adf_class = class;
	adf_major = ret;
	return 0;

err_chrdev:
	class_destroy(adf_class);
	return ret;
}

void adf_sysfs_destroy(void)
{
	idr_destroy(&adf_minors);
	class_destroy(adf_class);
}
