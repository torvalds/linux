// SPDX-License-Identifier: GPL-2.0
/*
 * USB Type-C Connector Class
 *
 * Copyright (C) 2017, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/slab.h>

#include "bus.h"

struct typec_plug {
	struct device			dev;
	enum typec_plug_index		index;
	struct ida			mode_ids;
};

struct typec_cable {
	struct device			dev;
	enum typec_plug_type		type;
	struct usb_pd_identity		*identity;
	unsigned int			active:1;
};

struct typec_partner {
	struct device			dev;
	unsigned int			usb_pd:1;
	struct usb_pd_identity		*identity;
	enum typec_accessory		accessory;
	struct ida			mode_ids;
};

struct typec_port {
	unsigned int			id;
	struct device			dev;
	struct ida			mode_ids;

	int				prefer_role;
	enum typec_data_role		data_role;
	enum typec_role			pwr_role;
	enum typec_role			vconn_role;
	enum typec_pwr_opmode		pwr_opmode;
	enum typec_port_type		port_type;
	struct mutex			port_type_lock;

	enum typec_orientation		orientation;
	struct typec_switch		*sw;
	struct typec_mux		*mux;

	const struct typec_capability	*cap;
};

#define to_typec_port(_dev_) container_of(_dev_, struct typec_port, dev)
#define to_typec_plug(_dev_) container_of(_dev_, struct typec_plug, dev)
#define to_typec_cable(_dev_) container_of(_dev_, struct typec_cable, dev)
#define to_typec_partner(_dev_) container_of(_dev_, struct typec_partner, dev)

static const struct device_type typec_partner_dev_type;
static const struct device_type typec_cable_dev_type;
static const struct device_type typec_plug_dev_type;

#define is_typec_partner(_dev_) (_dev_->type == &typec_partner_dev_type)
#define is_typec_cable(_dev_) (_dev_->type == &typec_cable_dev_type)
#define is_typec_plug(_dev_) (_dev_->type == &typec_plug_dev_type)

static DEFINE_IDA(typec_index_ida);
static struct class *typec_class;

/* ------------------------------------------------------------------------- */
/* Common attributes */

static const char * const typec_accessory_modes[] = {
	[TYPEC_ACCESSORY_NONE]		= "none",
	[TYPEC_ACCESSORY_AUDIO]		= "analog_audio",
	[TYPEC_ACCESSORY_DEBUG]		= "debug",
};

static struct usb_pd_identity *get_pd_identity(struct device *dev)
{
	if (is_typec_partner(dev)) {
		struct typec_partner *partner = to_typec_partner(dev);

		return partner->identity;
	} else if (is_typec_cable(dev)) {
		struct typec_cable *cable = to_typec_cable(dev);

		return cable->identity;
	}
	return NULL;
}

static ssize_t id_header_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct usb_pd_identity *id = get_pd_identity(dev);

	return sprintf(buf, "0x%08x\n", id->id_header);
}
static DEVICE_ATTR_RO(id_header);

static ssize_t cert_stat_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct usb_pd_identity *id = get_pd_identity(dev);

	return sprintf(buf, "0x%08x\n", id->cert_stat);
}
static DEVICE_ATTR_RO(cert_stat);

static ssize_t product_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct usb_pd_identity *id = get_pd_identity(dev);

	return sprintf(buf, "0x%08x\n", id->product);
}
static DEVICE_ATTR_RO(product);

static struct attribute *usb_pd_id_attrs[] = {
	&dev_attr_id_header.attr,
	&dev_attr_cert_stat.attr,
	&dev_attr_product.attr,
	NULL
};

static const struct attribute_group usb_pd_id_group = {
	.name = "identity",
	.attrs = usb_pd_id_attrs,
};

static const struct attribute_group *usb_pd_id_groups[] = {
	&usb_pd_id_group,
	NULL,
};

static void typec_report_identity(struct device *dev)
{
	sysfs_notify(&dev->kobj, "identity", "id_header");
	sysfs_notify(&dev->kobj, "identity", "cert_stat");
	sysfs_notify(&dev->kobj, "identity", "product");
}

/* ------------------------------------------------------------------------- */
/* Alternate Modes */

static int altmode_match(struct device *dev, void *data)
{
	struct typec_altmode *adev = to_typec_altmode(dev);
	struct typec_device_id *id = data;

	if (!is_typec_altmode(dev))
		return 0;

	return ((adev->svid == id->svid) && (adev->mode == id->mode));
}

static void typec_altmode_set_partner(struct altmode *altmode)
{
	struct typec_altmode *adev = &altmode->adev;
	struct typec_device_id id = { adev->svid, adev->mode, };
	struct typec_port *port = typec_altmode2port(adev);
	struct altmode *partner;
	struct device *dev;

	dev = device_find_child(&port->dev, &id, altmode_match);
	if (!dev)
		return;

	/* Bind the port alt mode to the partner/plug alt mode. */
	partner = to_altmode(to_typec_altmode(dev));
	altmode->partner = partner;

	/* Bind the partner/plug alt mode to the port alt mode. */
	if (is_typec_plug(adev->dev.parent)) {
		struct typec_plug *plug = to_typec_plug(adev->dev.parent);

		partner->plug[plug->index] = altmode;
	} else {
		partner->partner = altmode;
	}
}

static void typec_altmode_put_partner(struct altmode *altmode)
{
	struct altmode *partner = altmode->partner;
	struct typec_altmode *adev;

	if (!partner)
		return;

	adev = &partner->adev;

	if (is_typec_plug(adev->dev.parent)) {
		struct typec_plug *plug = to_typec_plug(adev->dev.parent);

		partner->plug[plug->index] = NULL;
	} else {
		partner->partner = NULL;
	}
	put_device(&adev->dev);
}

static int typec_port_fwnode_match(struct device *dev, const void *fwnode)
{
	return dev_fwnode(dev) == fwnode;
}

static int typec_port_name_match(struct device *dev, const void *name)
{
	return !strcmp((const char *)name, dev_name(dev));
}

static void *typec_port_match(struct device_connection *con, int ep, void *data)
{
	struct device *dev;

	/*
	 * FIXME: Check does the fwnode supports the requested SVID. If it does
	 * we need to return ERR_PTR(-PROBE_DEFER) when there is no device.
	 */
	if (con->fwnode)
		return class_find_device(typec_class, NULL, con->fwnode,
					 typec_port_fwnode_match);

	dev = class_find_device(typec_class, NULL, con->endpoint[ep],
				typec_port_name_match);

	return dev ? dev : ERR_PTR(-EPROBE_DEFER);
}

struct typec_altmode *
typec_altmode_register_notifier(struct device *dev, u16 svid, u8 mode,
				struct notifier_block *nb)
{
	struct typec_device_id id = { svid, mode, };
	struct device *altmode_dev;
	struct device *port_dev;
	struct altmode *altmode;
	int ret;

	/* Find the port linked to the caller */
	port_dev = device_connection_find_match(dev, NULL, NULL,
						typec_port_match);
	if (IS_ERR_OR_NULL(port_dev))
		return port_dev ? ERR_CAST(port_dev) : ERR_PTR(-ENODEV);

	/* Find the altmode with matching svid */
	altmode_dev = device_find_child(port_dev, &id, altmode_match);

	put_device(port_dev);

	if (!altmode_dev)
		return ERR_PTR(-ENODEV);

	altmode = to_altmode(to_typec_altmode(altmode_dev));

	/* Register notifier */
	ret = blocking_notifier_chain_register(&altmode->nh, nb);
	if (ret) {
		put_device(altmode_dev);
		return ERR_PTR(ret);
	}

	return &altmode->adev;
}
EXPORT_SYMBOL_GPL(typec_altmode_register_notifier);

void typec_altmode_unregister_notifier(struct typec_altmode *adev,
				       struct notifier_block *nb)
{
	struct altmode *altmode = to_altmode(adev);

	blocking_notifier_chain_unregister(&altmode->nh, nb);
	put_device(&adev->dev);
}
EXPORT_SYMBOL_GPL(typec_altmode_unregister_notifier);

/**
 * typec_altmode_update_active - Report Enter/Exit mode
 * @adev: Handle to the alternate mode
 * @active: True when the mode has been entered
 *
 * If a partner or cable plug executes Enter/Exit Mode command successfully, the
 * drivers use this routine to report the updated state of the mode.
 */
void typec_altmode_update_active(struct typec_altmode *adev, bool active)
{
	char dir[6];

	if (adev->active == active)
		return;

	if (!is_typec_port(adev->dev.parent) && adev->dev.driver) {
		if (!active)
			module_put(adev->dev.driver->owner);
		else
			WARN_ON(!try_module_get(adev->dev.driver->owner));
	}

	adev->active = active;
	snprintf(dir, sizeof(dir), "mode%d", adev->mode);
	sysfs_notify(&adev->dev.kobj, dir, "active");
	sysfs_notify(&adev->dev.kobj, NULL, "active");
	kobject_uevent(&adev->dev.kobj, KOBJ_CHANGE);
}
EXPORT_SYMBOL_GPL(typec_altmode_update_active);

/**
 * typec_altmode2port - Alternate Mode to USB Type-C port
 * @alt: The Alternate Mode
 *
 * Returns handle to the port that a cable plug or partner with @alt is
 * connected to.
 */
struct typec_port *typec_altmode2port(struct typec_altmode *alt)
{
	if (is_typec_plug(alt->dev.parent))
		return to_typec_port(alt->dev.parent->parent->parent);
	if (is_typec_partner(alt->dev.parent))
		return to_typec_port(alt->dev.parent->parent);
	if (is_typec_port(alt->dev.parent))
		return to_typec_port(alt->dev.parent);

	return NULL;
}
EXPORT_SYMBOL_GPL(typec_altmode2port);

static ssize_t
vdo_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct typec_altmode *alt = to_typec_altmode(dev);

	return sprintf(buf, "0x%08x\n", alt->vdo);
}
static DEVICE_ATTR_RO(vdo);

static ssize_t
description_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct typec_altmode *alt = to_typec_altmode(dev);

	return sprintf(buf, "%s\n", alt->desc ? alt->desc : "");
}
static DEVICE_ATTR_RO(description);

static ssize_t
active_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct typec_altmode *alt = to_typec_altmode(dev);

	return sprintf(buf, "%s\n", alt->active ? "yes" : "no");
}

static ssize_t active_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct typec_altmode *adev = to_typec_altmode(dev);
	struct altmode *altmode = to_altmode(adev);
	bool enter;
	int ret;

	ret = kstrtobool(buf, &enter);
	if (ret)
		return ret;

	if (adev->active == enter)
		return size;

	if (is_typec_port(adev->dev.parent)) {
		typec_altmode_update_active(adev, enter);

		/* Make sure that the partner exits the mode before disabling */
		if (altmode->partner && !enter && altmode->partner->adev.active)
			typec_altmode_exit(&altmode->partner->adev);
	} else if (altmode->partner) {
		if (enter && !altmode->partner->adev.active) {
			dev_warn(dev, "port has the mode disabled\n");
			return -EPERM;
		}
	}

	/* Note: If there is no driver, the mode will not be entered */
	if (adev->ops && adev->ops->activate) {
		ret = adev->ops->activate(adev, enter);
		if (ret)
			return ret;
	}

	return size;
}
static DEVICE_ATTR_RW(active);

static ssize_t
supported_roles_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct altmode *alt = to_altmode(to_typec_altmode(dev));
	ssize_t ret;

	switch (alt->roles) {
	case TYPEC_PORT_SRC:
		ret = sprintf(buf, "source\n");
		break;
	case TYPEC_PORT_SNK:
		ret = sprintf(buf, "sink\n");
		break;
	case TYPEC_PORT_DRP:
	default:
		ret = sprintf(buf, "source sink\n");
		break;
	}
	return ret;
}
static DEVICE_ATTR_RO(supported_roles);

static ssize_t
mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct typec_altmode *adev = to_typec_altmode(dev);

	return sprintf(buf, "%u\n", adev->mode);
}
static DEVICE_ATTR_RO(mode);

static ssize_t
svid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct typec_altmode *adev = to_typec_altmode(dev);

	return sprintf(buf, "%04x\n", adev->svid);
}
static DEVICE_ATTR_RO(svid);

static struct attribute *typec_altmode_attrs[] = {
	&dev_attr_active.attr,
	&dev_attr_mode.attr,
	&dev_attr_svid.attr,
	&dev_attr_vdo.attr,
	NULL
};
ATTRIBUTE_GROUPS(typec_altmode);

static int altmode_id_get(struct device *dev)
{
	struct ida *ids;

	if (is_typec_partner(dev))
		ids = &to_typec_partner(dev)->mode_ids;
	else if (is_typec_plug(dev))
		ids = &to_typec_plug(dev)->mode_ids;
	else
		ids = &to_typec_port(dev)->mode_ids;

	return ida_simple_get(ids, 0, 0, GFP_KERNEL);
}

static void altmode_id_remove(struct device *dev, int id)
{
	struct ida *ids;

	if (is_typec_partner(dev))
		ids = &to_typec_partner(dev)->mode_ids;
	else if (is_typec_plug(dev))
		ids = &to_typec_plug(dev)->mode_ids;
	else
		ids = &to_typec_port(dev)->mode_ids;

	ida_simple_remove(ids, id);
}

static void typec_altmode_release(struct device *dev)
{
	struct altmode *alt = to_altmode(to_typec_altmode(dev));

	typec_altmode_put_partner(alt);

	altmode_id_remove(alt->adev.dev.parent, alt->id);
	kfree(alt);
}

const struct device_type typec_altmode_dev_type = {
	.name = "typec_alternate_mode",
	.groups = typec_altmode_groups,
	.release = typec_altmode_release,
};

static struct typec_altmode *
typec_register_altmode(struct device *parent,
		       const struct typec_altmode_desc *desc)
{
	unsigned int id = altmode_id_get(parent);
	bool is_port = is_typec_port(parent);
	struct altmode *alt;
	int ret;

	alt = kzalloc(sizeof(*alt), GFP_KERNEL);
	if (!alt)
		return ERR_PTR(-ENOMEM);

	alt->adev.svid = desc->svid;
	alt->adev.mode = desc->mode;
	alt->adev.vdo = desc->vdo;
	alt->roles = desc->roles;
	alt->id = id;

	alt->attrs[0] = &dev_attr_vdo.attr;
	alt->attrs[1] = &dev_attr_description.attr;
	alt->attrs[2] = &dev_attr_active.attr;

	if (is_port) {
		alt->attrs[3] = &dev_attr_supported_roles.attr;
		alt->adev.active = true; /* Enabled by default */
	}

	sprintf(alt->group_name, "mode%d", desc->mode);
	alt->group.name = alt->group_name;
	alt->group.attrs = alt->attrs;
	alt->groups[0] = &alt->group;

	alt->adev.dev.parent = parent;
	alt->adev.dev.groups = alt->groups;
	alt->adev.dev.type = &typec_altmode_dev_type;
	dev_set_name(&alt->adev.dev, "%s.%u", dev_name(parent), id);

	/* Link partners and plugs with the ports */
	if (is_port)
		BLOCKING_INIT_NOTIFIER_HEAD(&alt->nh);
	else
		typec_altmode_set_partner(alt);

	/* The partners are bind to drivers */
	if (is_typec_partner(parent))
		alt->adev.dev.bus = &typec_bus;

	ret = device_register(&alt->adev.dev);
	if (ret) {
		dev_err(parent, "failed to register alternate mode (%d)\n",
			ret);
		put_device(&alt->adev.dev);
		return ERR_PTR(ret);
	}

	return &alt->adev;
}

/**
 * typec_unregister_altmode - Unregister Alternate Mode
 * @adev: The alternate mode to be unregistered
 *
 * Unregister device created with typec_partner_register_altmode(),
 * typec_plug_register_altmode() or typec_port_register_altmode().
 */
void typec_unregister_altmode(struct typec_altmode *adev)
{
	if (IS_ERR_OR_NULL(adev))
		return;
	typec_mux_put(to_altmode(adev)->mux);
	device_unregister(&adev->dev);
}
EXPORT_SYMBOL_GPL(typec_unregister_altmode);

/* ------------------------------------------------------------------------- */
/* Type-C Partners */

static ssize_t accessory_mode_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct typec_partner *p = to_typec_partner(dev);

	return sprintf(buf, "%s\n", typec_accessory_modes[p->accessory]);
}
static DEVICE_ATTR_RO(accessory_mode);

static ssize_t supports_usb_power_delivery_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct typec_partner *p = to_typec_partner(dev);

	return sprintf(buf, "%s\n", p->usb_pd ? "yes" : "no");
}
static DEVICE_ATTR_RO(supports_usb_power_delivery);

static struct attribute *typec_partner_attrs[] = {
	&dev_attr_accessory_mode.attr,
	&dev_attr_supports_usb_power_delivery.attr,
	NULL
};
ATTRIBUTE_GROUPS(typec_partner);

static void typec_partner_release(struct device *dev)
{
	struct typec_partner *partner = to_typec_partner(dev);

	ida_destroy(&partner->mode_ids);
	kfree(partner);
}

static const struct device_type typec_partner_dev_type = {
	.name = "typec_partner",
	.groups = typec_partner_groups,
	.release = typec_partner_release,
};

/**
 * typec_partner_set_identity - Report result from Discover Identity command
 * @partner: The partner updated identity values
 *
 * This routine is used to report that the result of Discover Identity USB power
 * delivery command has become available.
 */
int typec_partner_set_identity(struct typec_partner *partner)
{
	if (!partner->identity)
		return -EINVAL;

	typec_report_identity(&partner->dev);
	return 0;
}
EXPORT_SYMBOL_GPL(typec_partner_set_identity);

/**
 * typec_partner_register_altmode - Register USB Type-C Partner Alternate Mode
 * @partner: USB Type-C Partner that supports the alternate mode
 * @desc: Description of the alternate mode
 *
 * This routine is used to register each alternate mode individually that
 * @partner has listed in response to Discover SVIDs command. The modes for a
 * SVID listed in response to Discover Modes command need to be listed in an
 * array in @desc.
 *
 * Returns handle to the alternate mode on success or NULL on failure.
 */
struct typec_altmode *
typec_partner_register_altmode(struct typec_partner *partner,
			       const struct typec_altmode_desc *desc)
{
	return typec_register_altmode(&partner->dev, desc);
}
EXPORT_SYMBOL_GPL(typec_partner_register_altmode);

/**
 * typec_register_partner - Register a USB Type-C Partner
 * @port: The USB Type-C Port the partner is connected to
 * @desc: Description of the partner
 *
 * Registers a device for USB Type-C Partner described in @desc.
 *
 * Returns handle to the partner on success or ERR_PTR on failure.
 */
struct typec_partner *typec_register_partner(struct typec_port *port,
					     struct typec_partner_desc *desc)
{
	struct typec_partner *partner;
	int ret;

	partner = kzalloc(sizeof(*partner), GFP_KERNEL);
	if (!partner)
		return ERR_PTR(-ENOMEM);

	ida_init(&partner->mode_ids);
	partner->usb_pd = desc->usb_pd;
	partner->accessory = desc->accessory;

	if (desc->identity) {
		/*
		 * Creating directory for the identity only if the driver is
		 * able to provide data to it.
		 */
		partner->dev.groups = usb_pd_id_groups;
		partner->identity = desc->identity;
	}

	partner->dev.class = typec_class;
	partner->dev.parent = &port->dev;
	partner->dev.type = &typec_partner_dev_type;
	dev_set_name(&partner->dev, "%s-partner", dev_name(&port->dev));

	ret = device_register(&partner->dev);
	if (ret) {
		dev_err(&port->dev, "failed to register partner (%d)\n", ret);
		put_device(&partner->dev);
		return ERR_PTR(ret);
	}

	return partner;
}
EXPORT_SYMBOL_GPL(typec_register_partner);

/**
 * typec_unregister_partner - Unregister a USB Type-C Partner
 * @partner: The partner to be unregistered
 *
 * Unregister device created with typec_register_partner().
 */
void typec_unregister_partner(struct typec_partner *partner)
{
	if (!IS_ERR_OR_NULL(partner))
		device_unregister(&partner->dev);
}
EXPORT_SYMBOL_GPL(typec_unregister_partner);

/* ------------------------------------------------------------------------- */
/* Type-C Cable Plugs */

static void typec_plug_release(struct device *dev)
{
	struct typec_plug *plug = to_typec_plug(dev);

	ida_destroy(&plug->mode_ids);
	kfree(plug);
}

static const struct device_type typec_plug_dev_type = {
	.name = "typec_plug",
	.release = typec_plug_release,
};

/**
 * typec_plug_register_altmode - Register USB Type-C Cable Plug Alternate Mode
 * @plug: USB Type-C Cable Plug that supports the alternate mode
 * @desc: Description of the alternate mode
 *
 * This routine is used to register each alternate mode individually that @plug
 * has listed in response to Discover SVIDs command. The modes for a SVID that
 * the plug lists in response to Discover Modes command need to be listed in an
 * array in @desc.
 *
 * Returns handle to the alternate mode on success or ERR_PTR on failure.
 */
struct typec_altmode *
typec_plug_register_altmode(struct typec_plug *plug,
			    const struct typec_altmode_desc *desc)
{
	return typec_register_altmode(&plug->dev, desc);
}
EXPORT_SYMBOL_GPL(typec_plug_register_altmode);

/**
 * typec_register_plug - Register a USB Type-C Cable Plug
 * @cable: USB Type-C Cable with the plug
 * @desc: Description of the cable plug
 *
 * Registers a device for USB Type-C Cable Plug described in @desc. A USB Type-C
 * Cable Plug represents a plug with electronics in it that can response to USB
 * Power Delivery SOP Prime or SOP Double Prime packages.
 *
 * Returns handle to the cable plug on success or ERR_PTR on failure.
 */
struct typec_plug *typec_register_plug(struct typec_cable *cable,
				       struct typec_plug_desc *desc)
{
	struct typec_plug *plug;
	char name[8];
	int ret;

	plug = kzalloc(sizeof(*plug), GFP_KERNEL);
	if (!plug)
		return ERR_PTR(-ENOMEM);

	sprintf(name, "plug%d", desc->index);

	ida_init(&plug->mode_ids);
	plug->index = desc->index;
	plug->dev.class = typec_class;
	plug->dev.parent = &cable->dev;
	plug->dev.type = &typec_plug_dev_type;
	dev_set_name(&plug->dev, "%s-%s", dev_name(cable->dev.parent), name);

	ret = device_register(&plug->dev);
	if (ret) {
		dev_err(&cable->dev, "failed to register plug (%d)\n", ret);
		put_device(&plug->dev);
		return ERR_PTR(ret);
	}

	return plug;
}
EXPORT_SYMBOL_GPL(typec_register_plug);

/**
 * typec_unregister_plug - Unregister a USB Type-C Cable Plug
 * @plug: The cable plug to be unregistered
 *
 * Unregister device created with typec_register_plug().
 */
void typec_unregister_plug(struct typec_plug *plug)
{
	if (!IS_ERR_OR_NULL(plug))
		device_unregister(&plug->dev);
}
EXPORT_SYMBOL_GPL(typec_unregister_plug);

/* Type-C Cables */

static ssize_t
type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct typec_cable *cable = to_typec_cable(dev);

	return sprintf(buf, "%s\n", cable->active ? "active" : "passive");
}
static DEVICE_ATTR_RO(type);

static const char * const typec_plug_types[] = {
	[USB_PLUG_NONE]		= "unknown",
	[USB_PLUG_TYPE_A]	= "type-a",
	[USB_PLUG_TYPE_B]	= "type-b",
	[USB_PLUG_TYPE_C]	= "type-c",
	[USB_PLUG_CAPTIVE]	= "captive",
};

static ssize_t plug_type_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct typec_cable *cable = to_typec_cable(dev);

	return sprintf(buf, "%s\n", typec_plug_types[cable->type]);
}
static DEVICE_ATTR_RO(plug_type);

static struct attribute *typec_cable_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_plug_type.attr,
	NULL
};
ATTRIBUTE_GROUPS(typec_cable);

static void typec_cable_release(struct device *dev)
{
	struct typec_cable *cable = to_typec_cable(dev);

	kfree(cable);
}

static const struct device_type typec_cable_dev_type = {
	.name = "typec_cable",
	.groups = typec_cable_groups,
	.release = typec_cable_release,
};

/**
 * typec_cable_set_identity - Report result from Discover Identity command
 * @cable: The cable updated identity values
 *
 * This routine is used to report that the result of Discover Identity USB power
 * delivery command has become available.
 */
int typec_cable_set_identity(struct typec_cable *cable)
{
	if (!cable->identity)
		return -EINVAL;

	typec_report_identity(&cable->dev);
	return 0;
}
EXPORT_SYMBOL_GPL(typec_cable_set_identity);

/**
 * typec_register_cable - Register a USB Type-C Cable
 * @port: The USB Type-C Port the cable is connected to
 * @desc: Description of the cable
 *
 * Registers a device for USB Type-C Cable described in @desc. The cable will be
 * parent for the optional cable plug devises.
 *
 * Returns handle to the cable on success or ERR_PTR on failure.
 */
struct typec_cable *typec_register_cable(struct typec_port *port,
					 struct typec_cable_desc *desc)
{
	struct typec_cable *cable;
	int ret;

	cable = kzalloc(sizeof(*cable), GFP_KERNEL);
	if (!cable)
		return ERR_PTR(-ENOMEM);

	cable->type = desc->type;
	cable->active = desc->active;

	if (desc->identity) {
		/*
		 * Creating directory for the identity only if the driver is
		 * able to provide data to it.
		 */
		cable->dev.groups = usb_pd_id_groups;
		cable->identity = desc->identity;
	}

	cable->dev.class = typec_class;
	cable->dev.parent = &port->dev;
	cable->dev.type = &typec_cable_dev_type;
	dev_set_name(&cable->dev, "%s-cable", dev_name(&port->dev));

	ret = device_register(&cable->dev);
	if (ret) {
		dev_err(&port->dev, "failed to register cable (%d)\n", ret);
		put_device(&cable->dev);
		return ERR_PTR(ret);
	}

	return cable;
}
EXPORT_SYMBOL_GPL(typec_register_cable);

/**
 * typec_unregister_cable - Unregister a USB Type-C Cable
 * @cable: The cable to be unregistered
 *
 * Unregister device created with typec_register_cable().
 */
void typec_unregister_cable(struct typec_cable *cable)
{
	if (!IS_ERR_OR_NULL(cable))
		device_unregister(&cable->dev);
}
EXPORT_SYMBOL_GPL(typec_unregister_cable);

/* ------------------------------------------------------------------------- */
/* USB Type-C ports */

static const char * const typec_roles[] = {
	[TYPEC_SINK]	= "sink",
	[TYPEC_SOURCE]	= "source",
};

static const char * const typec_data_roles[] = {
	[TYPEC_DEVICE]	= "device",
	[TYPEC_HOST]	= "host",
};

static const char * const typec_port_power_roles[] = {
	[TYPEC_PORT_SRC] = "source",
	[TYPEC_PORT_SNK] = "sink",
	[TYPEC_PORT_DRP] = "dual",
};

static const char * const typec_port_data_roles[] = {
	[TYPEC_PORT_DFP] = "host",
	[TYPEC_PORT_UFP] = "device",
	[TYPEC_PORT_DRD] = "dual",
};

static const char * const typec_port_types_drp[] = {
	[TYPEC_PORT_SRC] = "dual [source] sink",
	[TYPEC_PORT_SNK] = "dual source [sink]",
	[TYPEC_PORT_DRP] = "[dual] source sink",
};

static ssize_t
preferred_role_store(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t size)
{
	struct typec_port *port = to_typec_port(dev);
	int role;
	int ret;

	if (port->cap->type != TYPEC_PORT_DRP) {
		dev_dbg(dev, "Preferred role only supported with DRP ports\n");
		return -EOPNOTSUPP;
	}

	if (!port->cap->try_role) {
		dev_dbg(dev, "Setting preferred role not supported\n");
		return -EOPNOTSUPP;
	}

	role = sysfs_match_string(typec_roles, buf);
	if (role < 0) {
		if (sysfs_streq(buf, "none"))
			role = TYPEC_NO_PREFERRED_ROLE;
		else
			return -EINVAL;
	}

	ret = port->cap->try_role(port->cap, role);
	if (ret)
		return ret;

	port->prefer_role = role;
	return size;
}

static ssize_t
preferred_role_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct typec_port *port = to_typec_port(dev);

	if (port->cap->type != TYPEC_PORT_DRP)
		return 0;

	if (port->prefer_role < 0)
		return 0;

	return sprintf(buf, "%s\n", typec_roles[port->prefer_role]);
}
static DEVICE_ATTR_RW(preferred_role);

static ssize_t data_role_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	struct typec_port *port = to_typec_port(dev);
	int ret;

	if (!port->cap->dr_set) {
		dev_dbg(dev, "data role swapping not supported\n");
		return -EOPNOTSUPP;
	}

	ret = sysfs_match_string(typec_data_roles, buf);
	if (ret < 0)
		return ret;

	mutex_lock(&port->port_type_lock);
	if (port->cap->data != TYPEC_PORT_DRD) {
		ret = -EOPNOTSUPP;
		goto unlock_and_ret;
	}

	ret = port->cap->dr_set(port->cap, ret);
	if (ret)
		goto unlock_and_ret;

	ret = size;
unlock_and_ret:
	mutex_unlock(&port->port_type_lock);
	return ret;
}

static ssize_t data_role_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct typec_port *port = to_typec_port(dev);

	if (port->cap->data == TYPEC_PORT_DRD)
		return sprintf(buf, "%s\n", port->data_role == TYPEC_HOST ?
			       "[host] device" : "host [device]");

	return sprintf(buf, "[%s]\n", typec_data_roles[port->data_role]);
}
static DEVICE_ATTR_RW(data_role);

static ssize_t power_role_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct typec_port *port = to_typec_port(dev);
	int ret;

	if (!port->cap->pd_revision) {
		dev_dbg(dev, "USB Power Delivery not supported\n");
		return -EOPNOTSUPP;
	}

	if (!port->cap->pr_set) {
		dev_dbg(dev, "power role swapping not supported\n");
		return -EOPNOTSUPP;
	}

	if (port->pwr_opmode != TYPEC_PWR_MODE_PD) {
		dev_dbg(dev, "partner unable to swap power role\n");
		return -EIO;
	}

	ret = sysfs_match_string(typec_roles, buf);
	if (ret < 0)
		return ret;

	mutex_lock(&port->port_type_lock);
	if (port->port_type != TYPEC_PORT_DRP) {
		dev_dbg(dev, "port type fixed at \"%s\"",
			     typec_port_power_roles[port->port_type]);
		ret = -EOPNOTSUPP;
		goto unlock_and_ret;
	}

	ret = port->cap->pr_set(port->cap, ret);
	if (ret)
		goto unlock_and_ret;

	ret = size;
unlock_and_ret:
	mutex_unlock(&port->port_type_lock);
	return ret;
}

static ssize_t power_role_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct typec_port *port = to_typec_port(dev);

	if (port->cap->type == TYPEC_PORT_DRP)
		return sprintf(buf, "%s\n", port->pwr_role == TYPEC_SOURCE ?
			       "[source] sink" : "source [sink]");

	return sprintf(buf, "[%s]\n", typec_roles[port->pwr_role]);
}
static DEVICE_ATTR_RW(power_role);

static ssize_t
port_type_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t size)
{
	struct typec_port *port = to_typec_port(dev);
	int ret;
	enum typec_port_type type;

	if (!port->cap->port_type_set || port->cap->type != TYPEC_PORT_DRP) {
		dev_dbg(dev, "changing port type not supported\n");
		return -EOPNOTSUPP;
	}

	ret = sysfs_match_string(typec_port_power_roles, buf);
	if (ret < 0)
		return ret;

	type = ret;
	mutex_lock(&port->port_type_lock);

	if (port->port_type == type) {
		ret = size;
		goto unlock_and_ret;
	}

	ret = port->cap->port_type_set(port->cap, type);
	if (ret)
		goto unlock_and_ret;

	port->port_type = type;
	ret = size;

unlock_and_ret:
	mutex_unlock(&port->port_type_lock);
	return ret;
}

static ssize_t
port_type_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct typec_port *port = to_typec_port(dev);

	if (port->cap->type == TYPEC_PORT_DRP)
		return sprintf(buf, "%s\n",
			       typec_port_types_drp[port->port_type]);

	return sprintf(buf, "[%s]\n", typec_port_power_roles[port->cap->type]);
}
static DEVICE_ATTR_RW(port_type);

static const char * const typec_pwr_opmodes[] = {
	[TYPEC_PWR_MODE_USB]	= "default",
	[TYPEC_PWR_MODE_1_5A]	= "1.5A",
	[TYPEC_PWR_MODE_3_0A]	= "3.0A",
	[TYPEC_PWR_MODE_PD]	= "usb_power_delivery",
};

static ssize_t power_operation_mode_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct typec_port *port = to_typec_port(dev);

	return sprintf(buf, "%s\n", typec_pwr_opmodes[port->pwr_opmode]);
}
static DEVICE_ATTR_RO(power_operation_mode);

static ssize_t vconn_source_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct typec_port *port = to_typec_port(dev);
	bool source;
	int ret;

	if (!port->cap->pd_revision) {
		dev_dbg(dev, "VCONN swap depends on USB Power Delivery\n");
		return -EOPNOTSUPP;
	}

	if (!port->cap->vconn_set) {
		dev_dbg(dev, "VCONN swapping not supported\n");
		return -EOPNOTSUPP;
	}

	ret = kstrtobool(buf, &source);
	if (ret)
		return ret;

	ret = port->cap->vconn_set(port->cap, (enum typec_role)source);
	if (ret)
		return ret;

	return size;
}

static ssize_t vconn_source_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct typec_port *port = to_typec_port(dev);

	return sprintf(buf, "%s\n",
		       port->vconn_role == TYPEC_SOURCE ? "yes" : "no");
}
static DEVICE_ATTR_RW(vconn_source);

static ssize_t supported_accessory_modes_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct typec_port *port = to_typec_port(dev);
	ssize_t ret = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(port->cap->accessory); i++) {
		if (port->cap->accessory[i])
			ret += sprintf(buf + ret, "%s ",
			       typec_accessory_modes[port->cap->accessory[i]]);
	}

	if (!ret)
		return sprintf(buf, "none\n");

	buf[ret - 1] = '\n';

	return ret;
}
static DEVICE_ATTR_RO(supported_accessory_modes);

static ssize_t usb_typec_revision_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct typec_port *port = to_typec_port(dev);
	u16 rev = port->cap->revision;

	return sprintf(buf, "%d.%d\n", (rev >> 8) & 0xff, (rev >> 4) & 0xf);
}
static DEVICE_ATTR_RO(usb_typec_revision);

static ssize_t usb_power_delivery_revision_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct typec_port *p = to_typec_port(dev);

	return sprintf(buf, "%d\n", (p->cap->pd_revision >> 8) & 0xff);
}
static DEVICE_ATTR_RO(usb_power_delivery_revision);

static struct attribute *typec_attrs[] = {
	&dev_attr_data_role.attr,
	&dev_attr_power_operation_mode.attr,
	&dev_attr_power_role.attr,
	&dev_attr_preferred_role.attr,
	&dev_attr_supported_accessory_modes.attr,
	&dev_attr_usb_power_delivery_revision.attr,
	&dev_attr_usb_typec_revision.attr,
	&dev_attr_vconn_source.attr,
	&dev_attr_port_type.attr,
	NULL,
};
ATTRIBUTE_GROUPS(typec);

static int typec_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	int ret;

	ret = add_uevent_var(env, "TYPEC_PORT=%s", dev_name(dev));
	if (ret)
		dev_err(dev, "failed to add uevent TYPEC_PORT\n");

	return ret;
}

static void typec_release(struct device *dev)
{
	struct typec_port *port = to_typec_port(dev);

	ida_simple_remove(&typec_index_ida, port->id);
	ida_destroy(&port->mode_ids);
	typec_switch_put(port->sw);
	typec_mux_put(port->mux);
	kfree(port);
}

const struct device_type typec_port_dev_type = {
	.name = "typec_port",
	.groups = typec_groups,
	.uevent = typec_uevent,
	.release = typec_release,
};

/* --------------------------------------- */
/* Driver callbacks to report role updates */

/**
 * typec_set_data_role - Report data role change
 * @port: The USB Type-C Port where the role was changed
 * @role: The new data role
 *
 * This routine is used by the port drivers to report data role changes.
 */
void typec_set_data_role(struct typec_port *port, enum typec_data_role role)
{
	if (port->data_role == role)
		return;

	port->data_role = role;
	sysfs_notify(&port->dev.kobj, NULL, "data_role");
	kobject_uevent(&port->dev.kobj, KOBJ_CHANGE);
}
EXPORT_SYMBOL_GPL(typec_set_data_role);

/**
 * typec_set_pwr_role - Report power role change
 * @port: The USB Type-C Port where the role was changed
 * @role: The new data role
 *
 * This routine is used by the port drivers to report power role changes.
 */
void typec_set_pwr_role(struct typec_port *port, enum typec_role role)
{
	if (port->pwr_role == role)
		return;

	port->pwr_role = role;
	sysfs_notify(&port->dev.kobj, NULL, "power_role");
	kobject_uevent(&port->dev.kobj, KOBJ_CHANGE);
}
EXPORT_SYMBOL_GPL(typec_set_pwr_role);

/**
 * typec_set_vconn_role - Report VCONN source change
 * @port: The USB Type-C Port which VCONN role changed
 * @role: Source when @port is sourcing VCONN, or Sink when it's not
 *
 * This routine is used by the port drivers to report if the VCONN source is
 * changes.
 */
void typec_set_vconn_role(struct typec_port *port, enum typec_role role)
{
	if (port->vconn_role == role)
		return;

	port->vconn_role = role;
	sysfs_notify(&port->dev.kobj, NULL, "vconn_source");
	kobject_uevent(&port->dev.kobj, KOBJ_CHANGE);
}
EXPORT_SYMBOL_GPL(typec_set_vconn_role);

static int partner_match(struct device *dev, void *data)
{
	return is_typec_partner(dev);
}

/**
 * typec_set_pwr_opmode - Report changed power operation mode
 * @port: The USB Type-C Port where the mode was changed
 * @opmode: New power operation mode
 *
 * This routine is used by the port drivers to report changed power operation
 * mode in @port. The modes are USB (default), 1.5A, 3.0A as defined in USB
 * Type-C specification, and "USB Power Delivery" when the power levels are
 * negotiated with methods defined in USB Power Delivery specification.
 */
void typec_set_pwr_opmode(struct typec_port *port,
			  enum typec_pwr_opmode opmode)
{
	struct device *partner_dev;

	if (port->pwr_opmode == opmode)
		return;

	port->pwr_opmode = opmode;
	sysfs_notify(&port->dev.kobj, NULL, "power_operation_mode");
	kobject_uevent(&port->dev.kobj, KOBJ_CHANGE);

	partner_dev = device_find_child(&port->dev, NULL, partner_match);
	if (partner_dev) {
		struct typec_partner *partner = to_typec_partner(partner_dev);

		if (opmode == TYPEC_PWR_MODE_PD && !partner->usb_pd) {
			partner->usb_pd = 1;
			sysfs_notify(&partner_dev->kobj, NULL,
				     "supports_usb_power_delivery");
		}
		put_device(partner_dev);
	}
}
EXPORT_SYMBOL_GPL(typec_set_pwr_opmode);

/**
 * typec_find_port_power_role - Get the typec port power capability
 * @name: port power capability string
 *
 * This routine is used to find the typec_port_type by its string name.
 *
 * Returns typec_port_type if success, otherwise negative error code.
 */
int typec_find_port_power_role(const char *name)
{
	return match_string(typec_port_power_roles,
			    ARRAY_SIZE(typec_port_power_roles), name);
}
EXPORT_SYMBOL_GPL(typec_find_port_power_role);

/**
 * typec_find_power_role - Find the typec one specific power role
 * @name: power role string
 *
 * This routine is used to find the typec_role by its string name.
 *
 * Returns typec_role if success, otherwise negative error code.
 */
int typec_find_power_role(const char *name)
{
	return match_string(typec_roles, ARRAY_SIZE(typec_roles), name);
}
EXPORT_SYMBOL_GPL(typec_find_power_role);

/**
 * typec_find_port_data_role - Get the typec port data capability
 * @name: port data capability string
 *
 * This routine is used to find the typec_port_data by its string name.
 *
 * Returns typec_port_data if success, otherwise negative error code.
 */
int typec_find_port_data_role(const char *name)
{
	return match_string(typec_port_data_roles,
			    ARRAY_SIZE(typec_port_data_roles), name);
}
EXPORT_SYMBOL_GPL(typec_find_port_data_role);

/* ------------------------------------------ */
/* API for Multiplexer/DeMultiplexer Switches */

/**
 * typec_set_orientation - Set USB Type-C cable plug orientation
 * @port: USB Type-C Port
 * @orientation: USB Type-C cable plug orientation
 *
 * Set cable plug orientation for @port.
 */
int typec_set_orientation(struct typec_port *port,
			  enum typec_orientation orientation)
{
	int ret;

	if (port->sw) {
		ret = port->sw->set(port->sw, orientation);
		if (ret)
			return ret;
	}

	port->orientation = orientation;

	return 0;
}
EXPORT_SYMBOL_GPL(typec_set_orientation);

/**
 * typec_get_orientation - Get USB Type-C cable plug orientation
 * @port: USB Type-C Port
 *
 * Get current cable plug orientation for @port.
 */
enum typec_orientation typec_get_orientation(struct typec_port *port)
{
	return port->orientation;
}
EXPORT_SYMBOL_GPL(typec_get_orientation);

/**
 * typec_set_mode - Set mode of operation for USB Type-C connector
 * @port: USB Type-C connector
 * @mode: Accessory Mode, USB Operation or Safe State
 *
 * Configure @port for Accessory Mode @mode. This function will configure the
 * muxes needed for @mode.
 */
int typec_set_mode(struct typec_port *port, int mode)
{
	return port->mux ? port->mux->set(port->mux, mode) : 0;
}
EXPORT_SYMBOL_GPL(typec_set_mode);

/* --------------------------------------- */

/**
 * typec_port_register_altmode - Register USB Type-C Port Alternate Mode
 * @port: USB Type-C Port that supports the alternate mode
 * @desc: Description of the alternate mode
 *
 * This routine is used to register an alternate mode that @port is capable of
 * supporting.
 *
 * Returns handle to the alternate mode on success or ERR_PTR on failure.
 */
struct typec_altmode *
typec_port_register_altmode(struct typec_port *port,
			    const struct typec_altmode_desc *desc)
{
	struct typec_altmode *adev;
	struct typec_mux *mux;

	mux = typec_mux_get(&port->dev, desc);
	if (IS_ERR(mux))
		return ERR_CAST(mux);

	adev = typec_register_altmode(&port->dev, desc);
	if (IS_ERR(adev))
		typec_mux_put(mux);
	else
		to_altmode(adev)->mux = mux;

	return adev;
}
EXPORT_SYMBOL_GPL(typec_port_register_altmode);

/**
 * typec_register_port - Register a USB Type-C Port
 * @parent: Parent device
 * @cap: Description of the port
 *
 * Registers a device for USB Type-C Port described in @cap.
 *
 * Returns handle to the port on success or ERR_PTR on failure.
 */
struct typec_port *typec_register_port(struct device *parent,
				       const struct typec_capability *cap)
{
	struct typec_port *port;
	int ret;
	int id;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return ERR_PTR(-ENOMEM);

	id = ida_simple_get(&typec_index_ida, 0, 0, GFP_KERNEL);
	if (id < 0) {
		kfree(port);
		return ERR_PTR(id);
	}

	switch (cap->type) {
	case TYPEC_PORT_SRC:
		port->pwr_role = TYPEC_SOURCE;
		port->vconn_role = TYPEC_SOURCE;
		break;
	case TYPEC_PORT_SNK:
		port->pwr_role = TYPEC_SINK;
		port->vconn_role = TYPEC_SINK;
		break;
	case TYPEC_PORT_DRP:
		if (cap->prefer_role != TYPEC_NO_PREFERRED_ROLE)
			port->pwr_role = cap->prefer_role;
		else
			port->pwr_role = TYPEC_SINK;
		break;
	}

	switch (cap->data) {
	case TYPEC_PORT_DFP:
		port->data_role = TYPEC_HOST;
		break;
	case TYPEC_PORT_UFP:
		port->data_role = TYPEC_DEVICE;
		break;
	case TYPEC_PORT_DRD:
		if (cap->prefer_role == TYPEC_SOURCE)
			port->data_role = TYPEC_HOST;
		else
			port->data_role = TYPEC_DEVICE;
		break;
	}

	ida_init(&port->mode_ids);
	mutex_init(&port->port_type_lock);

	port->id = id;
	port->cap = cap;
	port->port_type = cap->type;
	port->prefer_role = cap->prefer_role;

	device_initialize(&port->dev);
	port->dev.class = typec_class;
	port->dev.parent = parent;
	port->dev.fwnode = cap->fwnode;
	port->dev.type = &typec_port_dev_type;
	dev_set_name(&port->dev, "port%d", id);

	port->sw = typec_switch_get(&port->dev);
	if (IS_ERR(port->sw)) {
		put_device(&port->dev);
		return ERR_CAST(port->sw);
	}

	port->mux = typec_mux_get(&port->dev, NULL);
	if (IS_ERR(port->mux)) {
		put_device(&port->dev);
		return ERR_CAST(port->mux);
	}

	ret = device_add(&port->dev);
	if (ret) {
		dev_err(parent, "failed to register port (%d)\n", ret);
		put_device(&port->dev);
		return ERR_PTR(ret);
	}

	return port;
}
EXPORT_SYMBOL_GPL(typec_register_port);

/**
 * typec_unregister_port - Unregister a USB Type-C Port
 * @port: The port to be unregistered
 *
 * Unregister device created with typec_register_port().
 */
void typec_unregister_port(struct typec_port *port)
{
	if (!IS_ERR_OR_NULL(port))
		device_unregister(&port->dev);
}
EXPORT_SYMBOL_GPL(typec_unregister_port);

static int __init typec_init(void)
{
	int ret;

	ret = bus_register(&typec_bus);
	if (ret)
		return ret;

	typec_class = class_create(THIS_MODULE, "typec");
	if (IS_ERR(typec_class)) {
		bus_unregister(&typec_bus);
		return PTR_ERR(typec_class);
	}

	return 0;
}
subsys_initcall(typec_init);

static void __exit typec_exit(void)
{
	class_destroy(typec_class);
	ida_destroy(&typec_index_ida);
	bus_unregister(&typec_bus);
}
module_exit(typec_exit);

MODULE_AUTHOR("Heikki Krogerus <heikki.krogerus@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("USB Type-C Connector Class");
