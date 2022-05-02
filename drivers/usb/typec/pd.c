// SPDX-License-Identifier: GPL-2.0
/*
 * USB Power Delivery sysfs entries
 *
 * Copyright (C) 2022, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/slab.h>
#include <linux/usb/pd.h>

#include "pd.h"

static DEFINE_IDA(pd_ida);

static struct class pd_class = {
	.name = "usb_power_delivery",
	.owner = THIS_MODULE,
};

#define to_pdo(o) container_of(o, struct pdo, dev)

struct pdo {
	struct device dev;
	int object_position;
	u32 pdo;
};

static void pdo_release(struct device *dev)
{
	kfree(to_pdo(dev));
}

/* -------------------------------------------------------------------------- */
/* Fixed Supply */

static ssize_t
dual_role_power_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", !!(to_pdo(dev)->pdo & PDO_FIXED_DUAL_ROLE));
}
static DEVICE_ATTR_RO(dual_role_power);

static ssize_t
usb_suspend_supported_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", !!(to_pdo(dev)->pdo & PDO_FIXED_SUSPEND));
}
static DEVICE_ATTR_RO(usb_suspend_supported);

static ssize_t
unconstrained_power_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", !!(to_pdo(dev)->pdo & PDO_FIXED_EXTPOWER));
}
static DEVICE_ATTR_RO(unconstrained_power);

static ssize_t
usb_communication_capable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", !!(to_pdo(dev)->pdo & PDO_FIXED_USB_COMM));
}
static DEVICE_ATTR_RO(usb_communication_capable);

static ssize_t
dual_role_data_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", !!(to_pdo(dev)->pdo & PDO_FIXED_DATA_SWAP));
}
static DEVICE_ATTR_RO(dual_role_data);

static ssize_t
unchunked_extended_messages_supported_show(struct device *dev,
					   struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", !!(to_pdo(dev)->pdo & PDO_FIXED_UNCHUNK_EXT));
}
static DEVICE_ATTR_RO(unchunked_extended_messages_supported);

/*
 * REVISIT: Peak Current requires access also to the RDO.
static ssize_t
peak_current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	...
}
*/

static ssize_t
fast_role_swap_current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", to_pdo(dev)->pdo >> PDO_FIXED_FRS_CURR_SHIFT) & 3;
}
static DEVICE_ATTR_RO(fast_role_swap_current);

static ssize_t voltage_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umV\n", pdo_fixed_voltage(to_pdo(dev)->pdo));
}
static DEVICE_ATTR_RO(voltage);

/* Shared with Variable supplies, both source and sink */
static ssize_t current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umA\n", pdo_max_current(to_pdo(dev)->pdo));
}

/* Shared with Variable type supplies */
static struct device_attribute maximum_current_attr = {
	.attr = {
		.name = "maximum_current",
		.mode = 0444,
	},
	.show = current_show,
};

static struct device_attribute operational_current_attr = {
	.attr = {
		.name = "operational_current",
		.mode = 0444,
	},
	.show = current_show,
};

static struct attribute *source_fixed_supply_attrs[] = {
	&dev_attr_dual_role_power.attr,
	&dev_attr_usb_suspend_supported.attr,
	&dev_attr_unconstrained_power.attr,
	&dev_attr_usb_communication_capable.attr,
	&dev_attr_dual_role_data.attr,
	&dev_attr_unchunked_extended_messages_supported.attr,
	/*&dev_attr_peak_current.attr,*/
	&dev_attr_voltage.attr,
	&maximum_current_attr.attr,
	NULL
};

static umode_t fixed_attr_is_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	if (to_pdo(kobj_to_dev(kobj))->object_position &&
	    /*attr != &dev_attr_peak_current.attr &&*/
	    attr != &dev_attr_voltage.attr &&
	    attr != &maximum_current_attr.attr &&
	    attr != &operational_current_attr.attr)
		return 0;

	return attr->mode;
}

static const struct attribute_group source_fixed_supply_group = {
	.is_visible = fixed_attr_is_visible,
	.attrs = source_fixed_supply_attrs,
};
__ATTRIBUTE_GROUPS(source_fixed_supply);

static struct device_type source_fixed_supply_type = {
	.name = "pdo",
	.release = pdo_release,
	.groups = source_fixed_supply_groups,
};

static struct attribute *sink_fixed_supply_attrs[] = {
	&dev_attr_dual_role_power.attr,
	&dev_attr_usb_suspend_supported.attr,
	&dev_attr_unconstrained_power.attr,
	&dev_attr_usb_communication_capable.attr,
	&dev_attr_dual_role_data.attr,
	&dev_attr_unchunked_extended_messages_supported.attr,
	&dev_attr_fast_role_swap_current.attr,
	&dev_attr_voltage.attr,
	&operational_current_attr.attr,
	NULL
};

static const struct attribute_group sink_fixed_supply_group = {
	.is_visible = fixed_attr_is_visible,
	.attrs = sink_fixed_supply_attrs,
};
__ATTRIBUTE_GROUPS(sink_fixed_supply);

static struct device_type sink_fixed_supply_type = {
	.name = "pdo",
	.release = pdo_release,
	.groups = sink_fixed_supply_groups,
};

/* -------------------------------------------------------------------------- */
/* Variable Supply */

static ssize_t
maximum_voltage_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umV\n", pdo_max_voltage(to_pdo(dev)->pdo));
}
static DEVICE_ATTR_RO(maximum_voltage);

static ssize_t
minimum_voltage_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umV\n", pdo_min_voltage(to_pdo(dev)->pdo));
}
static DEVICE_ATTR_RO(minimum_voltage);

static struct attribute *source_variable_supply_attrs[] = {
	&dev_attr_maximum_voltage.attr,
	&dev_attr_minimum_voltage.attr,
	&maximum_current_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(source_variable_supply);

static struct device_type source_variable_supply_type = {
	.name = "pdo",
	.release = pdo_release,
	.groups = source_variable_supply_groups,
};

static struct attribute *sink_variable_supply_attrs[] = {
	&dev_attr_maximum_voltage.attr,
	&dev_attr_minimum_voltage.attr,
	&operational_current_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(sink_variable_supply);

static struct device_type sink_variable_supply_type = {
	.name = "pdo",
	.release = pdo_release,
	.groups = sink_variable_supply_groups,
};

/* -------------------------------------------------------------------------- */
/* Battery */

static ssize_t
maximum_power_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umW\n", pdo_max_power(to_pdo(dev)->pdo));
}
static DEVICE_ATTR_RO(maximum_power);

static ssize_t
operational_power_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umW\n", pdo_max_power(to_pdo(dev)->pdo));
}
static DEVICE_ATTR_RO(operational_power);

static struct attribute *source_battery_attrs[] = {
	&dev_attr_maximum_voltage.attr,
	&dev_attr_minimum_voltage.attr,
	&dev_attr_maximum_power.attr,
	NULL
};
ATTRIBUTE_GROUPS(source_battery);

static struct device_type source_battery_type = {
	.name = "pdo",
	.release = pdo_release,
	.groups = source_battery_groups,
};

static struct attribute *sink_battery_attrs[] = {
	&dev_attr_maximum_voltage.attr,
	&dev_attr_minimum_voltage.attr,
	&dev_attr_operational_power.attr,
	NULL
};
ATTRIBUTE_GROUPS(sink_battery);

static struct device_type sink_battery_type = {
	.name = "pdo",
	.release = pdo_release,
	.groups = sink_battery_groups,
};

/* -------------------------------------------------------------------------- */
/* Standard Power Range (SPR) Programmable Power Supply (PPS) */

static ssize_t
pps_power_limited_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", !!(to_pdo(dev)->pdo & BIT(27)));
}
static DEVICE_ATTR_RO(pps_power_limited);

static ssize_t
pps_max_voltage_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umV\n", pdo_pps_apdo_max_voltage(to_pdo(dev)->pdo));
}

static ssize_t
pps_min_voltage_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umV\n", pdo_pps_apdo_min_voltage(to_pdo(dev)->pdo));
}

static ssize_t
pps_max_current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umA\n", pdo_pps_apdo_max_current(to_pdo(dev)->pdo));
}

static struct device_attribute pps_max_voltage_attr = {
	.attr = {
		.name = "maximum_voltage",
		.mode = 0444,
	},
	.show = pps_max_voltage_show,
};

static struct device_attribute pps_min_voltage_attr = {
	.attr = {
		.name = "minimum_voltage",
		.mode = 0444,
	},
	.show = pps_min_voltage_show,
};

static struct device_attribute pps_max_current_attr = {
	.attr = {
		.name = "maximum_current",
		.mode = 0444,
	},
	.show = pps_max_current_show,
};

static struct attribute *source_pps_attrs[] = {
	&dev_attr_pps_power_limited.attr,
	&pps_max_voltage_attr.attr,
	&pps_min_voltage_attr.attr,
	&pps_max_current_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(source_pps);

static struct device_type source_pps_type = {
	.name = "pdo",
	.release = pdo_release,
	.groups = source_pps_groups,
};

static struct attribute *sink_pps_attrs[] = {
	&pps_max_voltage_attr.attr,
	&pps_min_voltage_attr.attr,
	&pps_max_current_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(sink_pps);

static struct device_type sink_pps_type = {
	.name = "pdo",
	.release = pdo_release,
	.groups = sink_pps_groups,
};

/* -------------------------------------------------------------------------- */

static const char * const supply_name[] = {
	[PDO_TYPE_FIXED] = "fixed_supply",
	[PDO_TYPE_BATT]  = "battery",
	[PDO_TYPE_VAR]	 = "variable_supply",
};

static const char * const apdo_supply_name[] = {
	[APDO_TYPE_PPS]  = "programmable_supply",
};

static struct device_type *source_type[] = {
	[PDO_TYPE_FIXED] = &source_fixed_supply_type,
	[PDO_TYPE_BATT]  = &source_battery_type,
	[PDO_TYPE_VAR]   = &source_variable_supply_type,
};

static struct device_type *source_apdo_type[] = {
	[APDO_TYPE_PPS]  = &source_pps_type,
};

static struct device_type *sink_type[] = {
	[PDO_TYPE_FIXED] = &sink_fixed_supply_type,
	[PDO_TYPE_BATT]  = &sink_battery_type,
	[PDO_TYPE_VAR]   = &sink_variable_supply_type,
};

static struct device_type *sink_apdo_type[] = {
	[APDO_TYPE_PPS]  = &sink_pps_type,
};

/* REVISIT: Export when EPR_*_Capabilities need to be supported. */
static int add_pdo(struct usb_power_delivery_capabilities *cap, u32 pdo, int position)
{
	struct device_type *type;
	const char *name;
	struct pdo *p;
	int ret;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->pdo = pdo;
	p->object_position = position;

	if (pdo_type(pdo) == PDO_TYPE_APDO) {
		/* FIXME: Only PPS supported for now! Skipping others. */
		if (pdo_apdo_type(pdo) > APDO_TYPE_PPS) {
			dev_warn(&cap->dev, "Unknown APDO type. PDO 0x%08x\n", pdo);
			kfree(p);
			return 0;
		}

		if (is_source(cap->role))
			type = source_apdo_type[pdo_apdo_type(pdo)];
		else
			type = sink_apdo_type[pdo_apdo_type(pdo)];

		name = apdo_supply_name[pdo_apdo_type(pdo)];
	} else {
		if (is_source(cap->role))
			type = source_type[pdo_type(pdo)];
		else
			type = sink_type[pdo_type(pdo)];

		name = supply_name[pdo_type(pdo)];
	}

	p->dev.parent = &cap->dev;
	p->dev.type = type;
	dev_set_name(&p->dev, "%u:%s", position + 1, name);

	ret = device_register(&p->dev);
	if (ret) {
		put_device(&p->dev);
		return ret;
	}

	return 0;
}

static int remove_pdo(struct device *dev, void *data)
{
	device_unregister(dev);
	return 0;
}

/* -------------------------------------------------------------------------- */

static const char * const cap_name[] = {
	[TYPEC_SINK]    = "sink-capabilities",
	[TYPEC_SOURCE]  = "source-capabilities",
};

static void pd_capabilities_release(struct device *dev)
{
	kfree(to_usb_power_delivery_capabilities(dev));
}

static struct device_type pd_capabilities_type = {
	.name = "capabilities",
	.release = pd_capabilities_release,
};

/**
 * usb_power_delivery_register_capabilities - Register a set of capabilities.
 * @pd: The USB PD instance that the capabilities belong to.
 * @desc: Description of the Capablities Message.
 *
 * This function registers a Capabilities Message described in @desc. The
 * capabilities will have their own sub-directory under @pd in sysfs.
 *
 * The function returns pointer to struct usb_power_delivery_capabilities, or
 * ERR_PRT(errno).
 */
struct usb_power_delivery_capabilities *
usb_power_delivery_register_capabilities(struct usb_power_delivery *pd,
					 struct usb_power_delivery_capabilities_desc *desc)
{
	struct usb_power_delivery_capabilities *cap;
	int ret;
	int i;

	cap = kzalloc(sizeof(*cap), GFP_KERNEL);
	if (!cap)
		return ERR_PTR(-ENOMEM);

	cap->pd = pd;
	cap->role = desc->role;

	cap->dev.parent = &pd->dev;
	cap->dev.type = &pd_capabilities_type;
	dev_set_name(&cap->dev, "%s", cap_name[cap->role]);

	ret = device_register(&cap->dev);
	if (ret) {
		put_device(&cap->dev);
		return ERR_PTR(ret);
	}

	for (i = 0; i < PDO_MAX_OBJECTS && desc->pdo[i]; i++) {
		ret = add_pdo(cap, desc->pdo[i], i);
		if (ret) {
			usb_power_delivery_unregister_capabilities(cap);
			return ERR_PTR(ret);
		}
	}

	return cap;
}
EXPORT_SYMBOL_GPL(usb_power_delivery_register_capabilities);

/**
 * usb_power_delivery_unregister_capabilities - Unregister a set of capabilities
 * @cap: The capabilities
 */
void usb_power_delivery_unregister_capabilities(struct usb_power_delivery_capabilities *cap)
{
	if (!cap)
		return;

	device_for_each_child(&cap->dev, NULL, remove_pdo);
	device_unregister(&cap->dev);
}
EXPORT_SYMBOL_GPL(usb_power_delivery_unregister_capabilities);

/* -------------------------------------------------------------------------- */

static ssize_t revision_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_power_delivery *pd = to_usb_power_delivery(dev);

	return sysfs_emit(buf, "%u.%u\n", (pd->revision >> 8) & 0xff, (pd->revision >> 4) & 0xf);
}
static DEVICE_ATTR_RO(revision);

static ssize_t version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_power_delivery *pd = to_usb_power_delivery(dev);

	return sysfs_emit(buf, "%u.%u\n", (pd->version >> 8) & 0xff, (pd->version >> 4) & 0xf);
}
static DEVICE_ATTR_RO(version);

static struct attribute *pd_attrs[] = {
	&dev_attr_revision.attr,
	&dev_attr_version.attr,
	NULL
};

static umode_t pd_attr_is_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	struct usb_power_delivery *pd = to_usb_power_delivery(kobj_to_dev(kobj));

	if (attr == &dev_attr_version.attr && !pd->version)
		return 0;

	return attr->mode;
}

static const struct attribute_group pd_group = {
	.is_visible = pd_attr_is_visible,
	.attrs = pd_attrs,
};
__ATTRIBUTE_GROUPS(pd);

static void pd_release(struct device *dev)
{
	struct usb_power_delivery *pd = to_usb_power_delivery(dev);

	ida_simple_remove(&pd_ida, pd->id);
	kfree(pd);
}

static struct device_type pd_type = {
	.name = "usb_power_delivery",
	.release = pd_release,
	.groups = pd_groups,
};

struct usb_power_delivery *usb_power_delivery_find(const char *name)
{
	struct device *dev;

	dev = class_find_device_by_name(&pd_class, name);

	return dev ? to_usb_power_delivery(dev) : NULL;
}

/**
 * usb_power_delivery_register - Register USB Power Delivery Support.
 * @parent: Parent device.
 * @desc: Description of the USB PD contract.
 *
 * This routine can be used to register USB Power Delivery capabilities that a
 * device or devices can support. These capabilities represent all the
 * capabilities that can be negotiated with a partner, so not only the Power
 * Capabilities that are negotiated using the USB PD Capabilities Message.
 *
 * The USB Power Delivery Support object that this routine generates can be used
 * as the parent object for all the actual USB Power Delivery Messages and
 * objects that can be negotiated with the partner.
 *
 * Returns handle to struct usb_power_delivery or ERR_PTR.
 */
struct usb_power_delivery *
usb_power_delivery_register(struct device *parent, struct usb_power_delivery_desc *desc)
{
	struct usb_power_delivery *pd;
	int ret;

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	ret = ida_simple_get(&pd_ida, 0, 0, GFP_KERNEL);
	if (ret < 0) {
		kfree(pd);
		return ERR_PTR(ret);
	}

	pd->id = ret;
	pd->revision = desc->revision;
	pd->version = desc->version;

	pd->dev.parent = parent;
	pd->dev.type = &pd_type;
	pd->dev.class = &pd_class;
	dev_set_name(&pd->dev, "pd%d", pd->id);

	ret = device_register(&pd->dev);
	if (ret) {
		put_device(&pd->dev);
		return ERR_PTR(ret);
	}

	return pd;
}
EXPORT_SYMBOL_GPL(usb_power_delivery_register);

/**
 * usb_power_delivery_unregister - Unregister USB Power Delivery Support.
 * @pd: The USB PD contract.
 */
void usb_power_delivery_unregister(struct usb_power_delivery *pd)
{
	if (IS_ERR_OR_NULL(pd))
		return;

	device_unregister(&pd->dev);
}
EXPORT_SYMBOL_GPL(usb_power_delivery_unregister);

/**
 * usb_power_delivery_link_device - Link device to its USB PD object.
 * @pd: The USB PD instance.
 * @dev: The device.
 *
 * This function can be used to create a symlink named "usb_power_delivery" for
 * @dev that points to @pd.
 */
int usb_power_delivery_link_device(struct usb_power_delivery *pd, struct device *dev)
{
	int ret;

	if (IS_ERR_OR_NULL(pd) || !dev)
		return 0;

	ret = sysfs_create_link(&dev->kobj, &pd->dev.kobj, "usb_power_delivery");
	if (ret)
		return ret;

	get_device(&pd->dev);
	get_device(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(usb_power_delivery_link_device);

/**
 * usb_power_delivery_unlink_device - Unlink device from its USB PD object.
 * @pd: The USB PD instance.
 * @dev: The device.
 *
 * Remove the symlink that was previously created with pd_link_device().
 */
void usb_power_delivery_unlink_device(struct usb_power_delivery *pd, struct device *dev)
{
	if (IS_ERR_OR_NULL(pd) || !dev)
		return;

	sysfs_remove_link(&dev->kobj, "usb_power_delivery");
	put_device(&pd->dev);
	put_device(dev);
}
EXPORT_SYMBOL_GPL(usb_power_delivery_unlink_device);

/* -------------------------------------------------------------------------- */

int __init usb_power_delivery_init(void)
{
	return class_register(&pd_class);
}

void __exit usb_power_delivery_exit(void)
{
	ida_destroy(&pd_ida);
	class_unregister(&pd_class);
}
