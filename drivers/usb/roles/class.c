// SPDX-License-Identifier: GPL-2.0
/*
 * USB Role Switch Support
 *
 * Copyright (C) 2018 Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 *         Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/usb/role.h>
#include <linux/property.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>

static const struct class role_class = {
	.name = "usb_role",
};

struct usb_role_switch {
	struct device dev;
	struct mutex lock; /* device lock*/
	enum usb_role role;

	/* From descriptor */
	struct device *usb2_port;
	struct device *usb3_port;
	struct device *udc;
	usb_role_switch_set_t set;
	usb_role_switch_get_t get;
	bool allow_userspace_control;
};

#define to_role_switch(d)	container_of(d, struct usb_role_switch, dev)

/**
 * usb_role_switch_set_role - Set USB role for a switch
 * @sw: USB role switch
 * @role: USB role to be switched to
 *
 * Set USB role @role for @sw.
 */
int usb_role_switch_set_role(struct usb_role_switch *sw, enum usb_role role)
{
	int ret;

	if (IS_ERR_OR_NULL(sw))
		return 0;

	mutex_lock(&sw->lock);

	ret = sw->set(sw, role);
	if (!ret) {
		sw->role = role;
		kobject_uevent(&sw->dev.kobj, KOBJ_CHANGE);
	}

	mutex_unlock(&sw->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_role_switch_set_role);

/**
 * usb_role_switch_get_role - Get the USB role for a switch
 * @sw: USB role switch
 *
 * Depending on the role-switch-driver this function returns either a cached
 * value of the last set role, or reads back the actual value from the hardware.
 */
enum usb_role usb_role_switch_get_role(struct usb_role_switch *sw)
{
	enum usb_role role;

	if (IS_ERR_OR_NULL(sw))
		return USB_ROLE_NONE;

	mutex_lock(&sw->lock);

	if (sw->get)
		role = sw->get(sw);
	else
		role = sw->role;

	mutex_unlock(&sw->lock);

	return role;
}
EXPORT_SYMBOL_GPL(usb_role_switch_get_role);

static void *usb_role_switch_match(const struct fwnode_handle *fwnode, const char *id,
				   void *data)
{
	struct device *dev;

	if (id && !fwnode_property_present(fwnode, id))
		return NULL;

	dev = class_find_device_by_fwnode(&role_class, fwnode);

	return dev ? to_role_switch(dev) : ERR_PTR(-EPROBE_DEFER);
}

static struct usb_role_switch *
usb_role_switch_is_parent(struct fwnode_handle *fwnode)
{
	struct fwnode_handle *parent = fwnode_get_parent(fwnode);
	struct device *dev;

	if (!fwnode_property_present(parent, "usb-role-switch")) {
		fwnode_handle_put(parent);
		return NULL;
	}

	dev = class_find_device_by_fwnode(&role_class, parent);
	fwnode_handle_put(parent);
	return dev ? to_role_switch(dev) : ERR_PTR(-EPROBE_DEFER);
}

/**
 * usb_role_switch_get - Find USB role switch linked with the caller
 * @dev: The caller device
 *
 * Finds and returns role switch linked with @dev. The reference count for the
 * found switch is incremented.
 */
struct usb_role_switch *usb_role_switch_get(struct device *dev)
{
	struct usb_role_switch *sw;

	sw = usb_role_switch_is_parent(dev_fwnode(dev));
	if (!sw)
		sw = device_connection_find_match(dev, "usb-role-switch", NULL,
						  usb_role_switch_match);

	if (!IS_ERR_OR_NULL(sw))
		WARN_ON(!try_module_get(sw->dev.parent->driver->owner));

	return sw;
}
EXPORT_SYMBOL_GPL(usb_role_switch_get);

/**
 * fwnode_usb_role_switch_get - Find USB role switch linked with the caller
 * @fwnode: The caller device node
 *
 * This is similar to the usb_role_switch_get() function above, but it searches
 * the switch using fwnode instead of device entry.
 */
struct usb_role_switch *fwnode_usb_role_switch_get(struct fwnode_handle *fwnode)
{
	struct usb_role_switch *sw;

	sw = usb_role_switch_is_parent(fwnode);
	if (!sw)
		sw = fwnode_connection_find_match(fwnode, "usb-role-switch",
						  NULL, usb_role_switch_match);
	if (!IS_ERR_OR_NULL(sw))
		WARN_ON(!try_module_get(sw->dev.parent->driver->owner));

	return sw;
}
EXPORT_SYMBOL_GPL(fwnode_usb_role_switch_get);

/**
 * usb_role_switch_put - Release handle to a switch
 * @sw: USB Role Switch
 *
 * Decrement reference count for @sw.
 */
void usb_role_switch_put(struct usb_role_switch *sw)
{
	if (!IS_ERR_OR_NULL(sw)) {
		module_put(sw->dev.parent->driver->owner);
		put_device(&sw->dev);
	}
}
EXPORT_SYMBOL_GPL(usb_role_switch_put);

/**
 * usb_role_switch_find_by_fwnode - Find USB role switch with its fwnode
 * @fwnode: fwnode of the USB Role Switch
 *
 * Finds and returns role switch with @fwnode. The reference count for the
 * found switch is incremented.
 */
struct usb_role_switch *
usb_role_switch_find_by_fwnode(const struct fwnode_handle *fwnode)
{
	struct device *dev;

	if (!fwnode)
		return NULL;

	dev = class_find_device_by_fwnode(&role_class, fwnode);
	if (dev)
		WARN_ON(!try_module_get(dev->parent->driver->owner));

	return dev ? to_role_switch(dev) : NULL;
}
EXPORT_SYMBOL_GPL(usb_role_switch_find_by_fwnode);

static umode_t
usb_role_switch_is_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct usb_role_switch *sw = to_role_switch(dev);

	if (sw->allow_userspace_control)
		return attr->mode;

	return 0;
}

static const char * const usb_roles[] = {
	[USB_ROLE_NONE]		= "none",
	[USB_ROLE_HOST]		= "host",
	[USB_ROLE_DEVICE]	= "device",
};

const char *usb_role_string(enum usb_role role)
{
	if (role < 0 || role >= ARRAY_SIZE(usb_roles))
		return "unknown";

	return usb_roles[role];
}
EXPORT_SYMBOL_GPL(usb_role_string);

static ssize_t
role_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_role_switch *sw = to_role_switch(dev);
	enum usb_role role = usb_role_switch_get_role(sw);

	return sprintf(buf, "%s\n", usb_roles[role]);
}

static ssize_t role_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t size)
{
	struct usb_role_switch *sw = to_role_switch(dev);
	int ret;

	ret = sysfs_match_string(usb_roles, buf);
	if (ret < 0) {
		bool res;

		/* Extra check if the user wants to disable the switch */
		ret = kstrtobool(buf, &res);
		if (ret || res)
			return -EINVAL;
	}

	ret = usb_role_switch_set_role(sw, ret);
	if (ret)
		return ret;

	return size;
}
static DEVICE_ATTR_RW(role);

static struct attribute *usb_role_switch_attrs[] = {
	&dev_attr_role.attr,
	NULL,
};

static const struct attribute_group usb_role_switch_group = {
	.is_visible = usb_role_switch_is_visible,
	.attrs = usb_role_switch_attrs,
};

static const struct attribute_group *usb_role_switch_groups[] = {
	&usb_role_switch_group,
	NULL,
};

static int usb_role_switch_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	int ret;

	ret = add_uevent_var(env, "USB_ROLE_SWITCH=%s", dev_name(dev));
	if (ret)
		dev_err(dev, "failed to add uevent USB_ROLE_SWITCH\n");

	return ret;
}

static void usb_role_switch_release(struct device *dev)
{
	struct usb_role_switch *sw = to_role_switch(dev);

	kfree(sw);
}

static const struct device_type usb_role_dev_type = {
	.name = "usb_role_switch",
	.groups = usb_role_switch_groups,
	.uevent = usb_role_switch_uevent,
	.release = usb_role_switch_release,
};

/**
 * usb_role_switch_register - Register USB Role Switch
 * @parent: Parent device for the switch
 * @desc: Description of the switch
 *
 * USB Role Switch is a device capable or choosing the role for USB connector.
 * On platforms where the USB controller is dual-role capable, the controller
 * driver will need to register the switch. On platforms where the USB host and
 * USB device controllers behind the connector are separate, there will be a
 * mux, and the driver for that mux will need to register the switch.
 *
 * Returns handle to a new role switch or ERR_PTR. The content of @desc is
 * copied.
 */
struct usb_role_switch *
usb_role_switch_register(struct device *parent,
			 const struct usb_role_switch_desc *desc)
{
	struct usb_role_switch *sw;
	int ret;

	if (!desc || !desc->set)
		return ERR_PTR(-EINVAL);

	sw = kzalloc(sizeof(*sw), GFP_KERNEL);
	if (!sw)
		return ERR_PTR(-ENOMEM);

	mutex_init(&sw->lock);

	sw->allow_userspace_control = desc->allow_userspace_control;
	sw->usb2_port = desc->usb2_port;
	sw->usb3_port = desc->usb3_port;
	sw->udc = desc->udc;
	sw->set = desc->set;
	sw->get = desc->get;

	sw->dev.parent = parent;
	sw->dev.fwnode = desc->fwnode;
	sw->dev.class = &role_class;
	sw->dev.type = &usb_role_dev_type;
	dev_set_drvdata(&sw->dev, desc->driver_data);
	dev_set_name(&sw->dev, "%s-role-switch",
		     desc->name ? desc->name : dev_name(parent));

	ret = device_register(&sw->dev);
	if (ret) {
		put_device(&sw->dev);
		return ERR_PTR(ret);
	}

	/* TODO: Symlinks for the host port and the device controller. */

	return sw;
}
EXPORT_SYMBOL_GPL(usb_role_switch_register);

/**
 * usb_role_switch_unregister - Unregsiter USB Role Switch
 * @sw: USB Role Switch
 *
 * Unregister switch that was registered with usb_role_switch_register().
 */
void usb_role_switch_unregister(struct usb_role_switch *sw)
{
	if (!IS_ERR_OR_NULL(sw))
		device_unregister(&sw->dev);
}
EXPORT_SYMBOL_GPL(usb_role_switch_unregister);

/**
 * usb_role_switch_set_drvdata - Assign private data pointer to a switch
 * @sw: USB Role Switch
 * @data: Private data pointer
 */
void usb_role_switch_set_drvdata(struct usb_role_switch *sw, void *data)
{
	dev_set_drvdata(&sw->dev, data);
}
EXPORT_SYMBOL_GPL(usb_role_switch_set_drvdata);

/**
 * usb_role_switch_get_drvdata - Get the private data pointer of a switch
 * @sw: USB Role Switch
 */
void *usb_role_switch_get_drvdata(struct usb_role_switch *sw)
{
	return dev_get_drvdata(&sw->dev);
}
EXPORT_SYMBOL_GPL(usb_role_switch_get_drvdata);

static int __init usb_roles_init(void)
{
	return class_register(&role_class);
}
subsys_initcall(usb_roles_init);

static void __exit usb_roles_exit(void)
{
	class_unregister(&role_class);
}
module_exit(usb_roles_exit);

MODULE_AUTHOR("Heikki Krogerus <heikki.krogerus@linux.intel.com>");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("USB Role Class");
