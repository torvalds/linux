/*
 * Roccat Lua driver for Linux
 *
 * Copyright (c) 2012 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*
 * Roccat Lua is a gamer mouse which cpi, button and light settings can be
 * configured.
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/hid-roccat.h>
#include "hid-ids.h"
#include "hid-roccat-common.h"
#include "hid-roccat-lua.h"

static ssize_t lua_sysfs_read(struct file *fp, struct kobject *kobj,
		char *buf, loff_t off, size_t count,
		size_t real_size, uint command)
{
	struct device *dev = kobj_to_dev(kobj);
	struct lua_device *lua = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off >= real_size)
		return 0;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&lua->lua_lock);
	retval = roccat_common2_receive(usb_dev, command, buf, real_size);
	mutex_unlock(&lua->lua_lock);

	return retval ? retval : real_size;
}

static ssize_t lua_sysfs_write(struct file *fp, struct kobject *kobj,
		void const *buf, loff_t off, size_t count,
		size_t real_size, uint command)
{
	struct device *dev = kobj_to_dev(kobj);
	struct lua_device *lua = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&lua->lua_lock);
	retval = roccat_common2_send(usb_dev, command, buf, real_size);
	mutex_unlock(&lua->lua_lock);

	return retval ? retval : real_size;
}

#define LUA_SYSFS_W(thingy, THINGY) \
static ssize_t lua_sysfs_write_ ## thingy(struct file *fp, \
		struct kobject *kobj, struct bin_attribute *attr, \
		char *buf, loff_t off, size_t count) \
{ \
	return lua_sysfs_write(fp, kobj, buf, off, count, \
			LUA_SIZE_ ## THINGY, LUA_COMMAND_ ## THINGY); \
}

#define LUA_SYSFS_R(thingy, THINGY) \
static ssize_t lua_sysfs_read_ ## thingy(struct file *fp, \
		struct kobject *kobj, struct bin_attribute *attr, \
		char *buf, loff_t off, size_t count) \
{ \
	return lua_sysfs_read(fp, kobj, buf, off, count, \
			LUA_SIZE_ ## THINGY, LUA_COMMAND_ ## THINGY); \
}

#define LUA_BIN_ATTRIBUTE_RW(thingy, THINGY) \
LUA_SYSFS_W(thingy, THINGY) \
LUA_SYSFS_R(thingy, THINGY) \
static struct bin_attribute lua_ ## thingy ## _attr = { \
	.attr = { .name = #thingy, .mode = 0660 }, \
	.size = LUA_SIZE_ ## THINGY, \
	.read = lua_sysfs_read_ ## thingy, \
	.write = lua_sysfs_write_ ## thingy \
};

LUA_BIN_ATTRIBUTE_RW(control, CONTROL)

static int lua_create_sysfs_attributes(struct usb_interface *intf)
{
	return sysfs_create_bin_file(&intf->dev.kobj, &lua_control_attr);
}

static void lua_remove_sysfs_attributes(struct usb_interface *intf)
{
	sysfs_remove_bin_file(&intf->dev.kobj, &lua_control_attr);
}

static int lua_init_lua_device_struct(struct usb_device *usb_dev,
		struct lua_device *lua)
{
	mutex_init(&lua->lua_lock);

	return 0;
}

static int lua_init_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct lua_device *lua;
	int retval;

	lua = kzalloc(sizeof(*lua), GFP_KERNEL);
	if (!lua) {
		hid_err(hdev, "can't alloc device descriptor\n");
		return -ENOMEM;
	}
	hid_set_drvdata(hdev, lua);

	retval = lua_init_lua_device_struct(usb_dev, lua);
	if (retval) {
		hid_err(hdev, "couldn't init struct lua_device\n");
		goto exit;
	}

	retval = lua_create_sysfs_attributes(intf);
	if (retval) {
		hid_err(hdev, "cannot create sysfs files\n");
		goto exit;
	}

	return 0;
exit:
	kfree(lua);
	return retval;
}

static void lua_remove_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct lua_device *lua;

	lua_remove_sysfs_attributes(intf);

	lua = hid_get_drvdata(hdev);
	kfree(lua);
}

static int lua_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int retval;

	retval = hid_parse(hdev);
	if (retval) {
		hid_err(hdev, "parse failed\n");
		goto exit;
	}

	retval = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (retval) {
		hid_err(hdev, "hw start failed\n");
		goto exit;
	}

	retval = lua_init_specials(hdev);
	if (retval) {
		hid_err(hdev, "couldn't install mouse\n");
		goto exit_stop;
	}

	return 0;

exit_stop:
	hid_hw_stop(hdev);
exit:
	return retval;
}

static void lua_remove(struct hid_device *hdev)
{
	lua_remove_specials(hdev);
	hid_hw_stop(hdev);
}

static const struct hid_device_id lua_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT, USB_DEVICE_ID_ROCCAT_LUA) },
	{ }
};

MODULE_DEVICE_TABLE(hid, lua_devices);

static struct hid_driver lua_driver = {
		.name = "lua",
		.id_table = lua_devices,
		.probe = lua_probe,
		.remove = lua_remove
};
module_hid_driver(lua_driver);

MODULE_AUTHOR("Stefan Achatz");
MODULE_DESCRIPTION("USB Roccat Lua driver");
MODULE_LICENSE("GPL v2");
