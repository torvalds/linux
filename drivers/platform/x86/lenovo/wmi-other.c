// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Lenovo Other Mode WMI interface driver.
 *
 * This driver uses the fw_attributes class to expose the various WMI functions
 * provided by the "Other Mode" WMI interface. This enables CPU and GPU power
 * limit as well as various other attributes for devices that fall under the
 * "Gaming Series" of Lenovo laptop devices. Each attribute exposed by the
 * "Other Mode" interface has a corresponding Capability Data struct that
 * allows the driver to probe details about the attribute such as if it is
 * supported by the hardware, the default_value, max_value, min_value, and step
 * increment.
 *
 * These attributes typically don't fit anywhere else in the sysfs and are set
 * in Windows using one of Lenovo's multiple user applications.
 *
 * Copyright (C) 2025 Derek J. Clark <derekjohn.clark@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/component.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/gfp_types.h>
#include <linux/idr.h>
#include <linux/kdev_t.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_profile.h>
#include <linux/types.h>
#include <linux/wmi.h>

#include "wmi-capdata01.h"
#include "wmi-events.h"
#include "wmi-gamezone.h"
#include "wmi-helpers.h"
#include "wmi-other.h"
#include "../firmware_attributes_class.h"

#define LENOVO_OTHER_MODE_GUID "DC2A8805-3A8C-41BA-A6F7-092E0089CD3B"

#define LWMI_DEVICE_ID_CPU 0x01

#define LWMI_FEATURE_ID_CPU_SPPT 0x01
#define LWMI_FEATURE_ID_CPU_SPL 0x02
#define LWMI_FEATURE_ID_CPU_FPPT 0x03

#define LWMI_TYPE_ID_NONE 0x00

#define LWMI_FEATURE_VALUE_GET 17
#define LWMI_FEATURE_VALUE_SET 18

#define LWMI_ATTR_DEV_ID_MASK GENMASK(31, 24)
#define LWMI_ATTR_FEAT_ID_MASK GENMASK(23, 16)
#define LWMI_ATTR_MODE_ID_MASK GENMASK(15, 8)
#define LWMI_ATTR_TYPE_ID_MASK GENMASK(7, 0)

#define LWMI_OM_FW_ATTR_BASE_PATH "lenovo-wmi-other"

static BLOCKING_NOTIFIER_HEAD(om_chain_head);
static DEFINE_IDA(lwmi_om_ida);

enum attribute_property {
	DEFAULT_VAL,
	MAX_VAL,
	MIN_VAL,
	STEP_VAL,
	SUPPORTED,
};

struct lwmi_om_priv {
	struct component_master_ops *ops;
	struct cd01_list *cd01_list; /* only valid after capdata01 bind */
	struct device *fw_attr_dev;
	struct kset *fw_attr_kset;
	struct notifier_block nb;
	struct wmi_device *wdev;
	int ida_id;
};

struct tunable_attr_01 {
	struct capdata01 *capdata;
	struct device *dev;
	u32 feature_id;
	u32 device_id;
	u32 type_id;
};

static struct tunable_attr_01 ppt_pl1_spl = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_SPL,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 ppt_pl2_sppt = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_SPPT,
	.type_id = LWMI_TYPE_ID_NONE,
};

static struct tunable_attr_01 ppt_pl3_fppt = {
	.device_id = LWMI_DEVICE_ID_CPU,
	.feature_id = LWMI_FEATURE_ID_CPU_FPPT,
	.type_id = LWMI_TYPE_ID_NONE,
};

struct capdata01_attr_group {
	const struct attribute_group *attr_group;
	struct tunable_attr_01 *tunable_attr;
};

/**
 * lwmi_om_register_notifier() - Add a notifier to the blocking notifier chain
 * @nb: The notifier_block struct to register
 *
 * Call blocking_notifier_chain_register to register the notifier block to the
 * lenovo-wmi-other driver notifier chain.
 *
 * Return: 0 on success, %-EEXIST on error.
 */
int lwmi_om_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&om_chain_head, nb);
}
EXPORT_SYMBOL_NS_GPL(lwmi_om_register_notifier, "LENOVO_WMI_OTHER");

/**
 * lwmi_om_unregister_notifier() - Remove a notifier from the blocking notifier
 * chain.
 * @nb: The notifier_block struct to register
 *
 * Call blocking_notifier_chain_unregister to unregister the notifier block from the
 * lenovo-wmi-other driver notifier chain.
 *
 * Return: 0 on success, %-ENOENT on error.
 */
int lwmi_om_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&om_chain_head, nb);
}
EXPORT_SYMBOL_NS_GPL(lwmi_om_unregister_notifier, "LENOVO_WMI_OTHER");

/**
 * devm_lwmi_om_unregister_notifier() - Remove a notifier from the blocking
 * notifier chain.
 * @data: Void pointer to the notifier_block struct to register.
 *
 * Call lwmi_om_unregister_notifier to unregister the notifier block from the
 * lenovo-wmi-other driver notifier chain.
 *
 * Return: 0 on success, %-ENOENT on error.
 */
static void devm_lwmi_om_unregister_notifier(void *data)
{
	struct notifier_block *nb = data;

	lwmi_om_unregister_notifier(nb);
}

/**
 * devm_lwmi_om_register_notifier() - Add a notifier to the blocking notifier
 * chain.
 * @dev: The parent device of the notifier_block struct.
 * @nb: The notifier_block struct to register
 *
 * Call lwmi_om_register_notifier to register the notifier block to the
 * lenovo-wmi-other driver notifier chain. Then add devm_lwmi_om_unregister_notifier
 * as a device managed action to automatically unregister the notifier block
 * upon parent device removal.
 *
 * Return: 0 on success, or an error code.
 */
int devm_lwmi_om_register_notifier(struct device *dev,
				   struct notifier_block *nb)
{
	int ret;

	ret = lwmi_om_register_notifier(nb);
	if (ret < 0)
		return ret;

	return devm_add_action_or_reset(dev, devm_lwmi_om_unregister_notifier,
					nb);
}
EXPORT_SYMBOL_NS_GPL(devm_lwmi_om_register_notifier, "LENOVO_WMI_OTHER");

/**
 * lwmi_om_notifier_call() - Call functions for the notifier call chain.
 * @mode: Pointer to a thermal mode enum to retrieve the data from.
 *
 * Call blocking_notifier_call_chain to retrieve the thermal mode from the
 * lenovo-wmi-gamezone driver.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_om_notifier_call(enum thermal_mode *mode)
{
	int ret;

	ret = blocking_notifier_call_chain(&om_chain_head,
					   LWMI_GZ_GET_THERMAL_MODE, &mode);
	if ((ret & ~NOTIFY_STOP_MASK) != NOTIFY_OK)
		return -EINVAL;

	return 0;
}

/* Attribute Methods */

/**
 * int_type_show() - Emit the data type for an integer attribute
 * @kobj: Pointer to the driver object.
 * @kattr: Pointer to the attribute calling this function.
 * @buf: The buffer to write to.
 *
 * Return: Number of characters written to buf.
 */
static ssize_t int_type_show(struct kobject *kobj, struct kobj_attribute *kattr,
			     char *buf)
{
	return sysfs_emit(buf, "integer\n");
}

/**
 * attr_capdata01_show() - Get the value of the specified attribute property
 *
 * @kobj: Pointer to the driver object.
 * @kattr: Pointer to the attribute calling this function.
 * @buf: The buffer to write to.
 * @tunable_attr: The attribute to be read.
 * @prop: The property of this attribute to be read.
 *
 * Retrieves the given property from the capability data 01 struct for the
 * specified attribute's "custom" thermal mode. This function is intended
 * to be generic so it can be called from any integer attributes "_show"
 * function.
 *
 * If the WMI is success the sysfs attribute is notified.
 *
 * Return: Either number of characters written to buf, or an error code.
 */
static ssize_t attr_capdata01_show(struct kobject *kobj,
				   struct kobj_attribute *kattr, char *buf,
				   struct tunable_attr_01 *tunable_attr,
				   enum attribute_property prop)
{
	struct lwmi_om_priv *priv = dev_get_drvdata(tunable_attr->dev);
	struct capdata01 capdata;
	u32 attribute_id;
	int value, ret;

	attribute_id =
		FIELD_PREP(LWMI_ATTR_DEV_ID_MASK, tunable_attr->device_id) |
		FIELD_PREP(LWMI_ATTR_FEAT_ID_MASK, tunable_attr->feature_id) |
		FIELD_PREP(LWMI_ATTR_MODE_ID_MASK,
			   LWMI_GZ_THERMAL_MODE_CUSTOM) |
		FIELD_PREP(LWMI_ATTR_TYPE_ID_MASK, tunable_attr->type_id);

	ret = lwmi_cd01_get_data(priv->cd01_list, attribute_id, &capdata);
	if (ret)
		return ret;

	switch (prop) {
	case DEFAULT_VAL:
		value = capdata.default_value;
		break;
	case MAX_VAL:
		value = capdata.max_value;
		break;
	case MIN_VAL:
		value = capdata.min_value;
		break;
	case STEP_VAL:
		value = capdata.step;
		break;
	default:
		return -EINVAL;
	}

	return sysfs_emit(buf, "%d\n", value);
}

/**
 * attr_current_value_store() - Set the current value of the given attribute
 * @kobj: Pointer to the driver object.
 * @kattr: Pointer to the attribute calling this function.
 * @buf: The buffer to read from, this is parsed to `int` type.
 * @count: Required by sysfs attribute macros, pass in from the callee attr.
 * @tunable_attr: The attribute to be stored.
 *
 * Sets the value of the given attribute when operating under the "custom"
 * smartfan profile. The current smartfan profile is retrieved from the
 * lenovo-wmi-gamezone driver and error is returned if the result is not
 * "custom". This function is intended to be generic so it can be called from
 * any integer attribute's "_store" function. The integer to be sent to the WMI
 * method is range checked and an error code is returned if out of range.
 *
 * If the value is valid and WMI is success, then the sysfs attribute is
 * notified.
 *
 * Return: Either count, or an error code.
 */
static ssize_t attr_current_value_store(struct kobject *kobj,
					struct kobj_attribute *kattr,
					const char *buf, size_t count,
					struct tunable_attr_01 *tunable_attr)
{
	struct lwmi_om_priv *priv = dev_get_drvdata(tunable_attr->dev);
	struct wmi_method_args_32 args;
	struct capdata01 capdata;
	enum thermal_mode mode;
	u32 attribute_id;
	u32 value;
	int ret;

	ret = lwmi_om_notifier_call(&mode);
	if (ret)
		return ret;

	if (mode != LWMI_GZ_THERMAL_MODE_CUSTOM)
		return -EBUSY;

	attribute_id =
		FIELD_PREP(LWMI_ATTR_DEV_ID_MASK, tunable_attr->device_id) |
		FIELD_PREP(LWMI_ATTR_FEAT_ID_MASK, tunable_attr->feature_id) |
		FIELD_PREP(LWMI_ATTR_MODE_ID_MASK, mode) |
		FIELD_PREP(LWMI_ATTR_TYPE_ID_MASK, tunable_attr->type_id);

	ret = lwmi_cd01_get_data(priv->cd01_list, attribute_id, &capdata);
	if (ret)
		return ret;

	ret = kstrtouint(buf, 10, &value);
	if (ret)
		return ret;

	if (value < capdata.min_value || value > capdata.max_value)
		return -EINVAL;

	args.arg0 = attribute_id;
	args.arg1 = value;

	ret = lwmi_dev_evaluate_int(priv->wdev, 0x0, LWMI_FEATURE_VALUE_SET,
				    (unsigned char *)&args, sizeof(args), NULL);
	if (ret)
		return ret;

	return count;
};

/**
 * attr_current_value_show() - Get the current value of the given attribute
 * @kobj: Pointer to the driver object.
 * @kattr: Pointer to the attribute calling this function.
 * @buf: The buffer to write to.
 * @tunable_attr: The attribute to be read.
 *
 * Retrieves the value of the given attribute for the current smartfan profile.
 * The current smartfan profile is retrieved from the lenovo-wmi-gamezone driver.
 * This function is intended to be generic so it can be called from any integer
 * attribute's "_show" function.
 *
 * If the WMI is success the sysfs attribute is notified.
 *
 * Return: Either number of characters written to buf, or an error code.
 */
static ssize_t attr_current_value_show(struct kobject *kobj,
				       struct kobj_attribute *kattr, char *buf,
				       struct tunable_attr_01 *tunable_attr)
{
	struct lwmi_om_priv *priv = dev_get_drvdata(tunable_attr->dev);
	struct wmi_method_args_32 args;
	enum thermal_mode mode;
	u32 attribute_id;
	int retval;
	int ret;

	ret = lwmi_om_notifier_call(&mode);
	if (ret)
		return ret;

	attribute_id =
		FIELD_PREP(LWMI_ATTR_DEV_ID_MASK, tunable_attr->device_id) |
		FIELD_PREP(LWMI_ATTR_FEAT_ID_MASK, tunable_attr->feature_id) |
		FIELD_PREP(LWMI_ATTR_MODE_ID_MASK, mode) |
		FIELD_PREP(LWMI_ATTR_TYPE_ID_MASK, tunable_attr->type_id);

	args.arg0 = attribute_id;

	ret = lwmi_dev_evaluate_int(priv->wdev, 0x0, LWMI_FEATURE_VALUE_GET,
				    (unsigned char *)&args, sizeof(args),
				    &retval);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", retval);
}

/* Lenovo WMI Other Mode Attribute macros */
#define __LWMI_ATTR_RO(_func, _name)                                  \
	{                                                             \
		.attr = { .name = __stringify(_name), .mode = 0444 }, \
		.show = _func##_##_name##_show,                       \
	}

#define __LWMI_ATTR_RO_AS(_name, _show)                               \
	{                                                             \
		.attr = { .name = __stringify(_name), .mode = 0444 }, \
		.show = _show,                                        \
	}

#define __LWMI_ATTR_RW(_func, _name) \
	__ATTR(_name, 0644, _func##_##_name##_show, _func##_##_name##_store)

/* Shows a formatted static variable */
#define __LWMI_ATTR_SHOW_FMT(_prop, _attrname, _fmt, _val)                     \
	static ssize_t _attrname##_##_prop##_show(                             \
		struct kobject *kobj, struct kobj_attribute *kattr, char *buf) \
	{                                                                      \
		return sysfs_emit(buf, _fmt, _val);                            \
	}                                                                      \
	static struct kobj_attribute attr_##_attrname##_##_prop =              \
		__LWMI_ATTR_RO(_attrname, _prop)

/* Attribute current value read/write */
#define __LWMI_TUNABLE_CURRENT_VALUE_CAP01(_attrname)                          \
	static ssize_t _attrname##_current_value_store(                        \
		struct kobject *kobj, struct kobj_attribute *kattr,            \
		const char *buf, size_t count)                                 \
	{                                                                      \
		return attr_current_value_store(kobj, kattr, buf, count,       \
						&_attrname);                   \
	}                                                                      \
	static ssize_t _attrname##_current_value_show(                         \
		struct kobject *kobj, struct kobj_attribute *kattr, char *buf) \
	{                                                                      \
		return attr_current_value_show(kobj, kattr, buf, &_attrname);  \
	}                                                                      \
	static struct kobj_attribute attr_##_attrname##_current_value =        \
		__LWMI_ATTR_RW(_attrname, current_value)

/* Attribute property read only */
#define __LWMI_TUNABLE_RO_CAP01(_prop, _attrname, _prop_type)                  \
	static ssize_t _attrname##_##_prop##_show(                             \
		struct kobject *kobj, struct kobj_attribute *kattr, char *buf) \
	{                                                                      \
		return attr_capdata01_show(kobj, kattr, buf, &_attrname,       \
					   _prop_type);                        \
	}                                                                      \
	static struct kobj_attribute attr_##_attrname##_##_prop =              \
		__LWMI_ATTR_RO(_attrname, _prop)

#define LWMI_ATTR_GROUP_TUNABLE_CAP01(_attrname, _fsname, _dispname)      \
	__LWMI_TUNABLE_CURRENT_VALUE_CAP01(_attrname);                    \
	__LWMI_TUNABLE_RO_CAP01(default_value, _attrname, DEFAULT_VAL);   \
	__LWMI_ATTR_SHOW_FMT(display_name, _attrname, "%s\n", _dispname); \
	__LWMI_TUNABLE_RO_CAP01(max_value, _attrname, MAX_VAL);           \
	__LWMI_TUNABLE_RO_CAP01(min_value, _attrname, MIN_VAL);           \
	__LWMI_TUNABLE_RO_CAP01(scalar_increment, _attrname, STEP_VAL);   \
	static struct kobj_attribute attr_##_attrname##_type =            \
		__LWMI_ATTR_RO_AS(type, int_type_show);                   \
	static struct attribute *_attrname##_attrs[] = {                  \
		&attr_##_attrname##_current_value.attr,                   \
		&attr_##_attrname##_default_value.attr,                   \
		&attr_##_attrname##_display_name.attr,                    \
		&attr_##_attrname##_max_value.attr,                       \
		&attr_##_attrname##_min_value.attr,                       \
		&attr_##_attrname##_scalar_increment.attr,                \
		&attr_##_attrname##_type.attr,                            \
		NULL,                                                     \
	};                                                                \
	static const struct attribute_group _attrname##_attr_group = {    \
		.name = _fsname, .attrs = _attrname##_attrs               \
	}

LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl1_spl, "ppt_pl1_spl",
			      "Set the CPU sustained power limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl2_sppt, "ppt_pl2_sppt",
			      "Set the CPU slow package power tracking limit");
LWMI_ATTR_GROUP_TUNABLE_CAP01(ppt_pl3_fppt, "ppt_pl3_fppt",
			      "Set the CPU fast package power tracking limit");

static struct capdata01_attr_group cd01_attr_groups[] = {
	{ &ppt_pl1_spl_attr_group, &ppt_pl1_spl },
	{ &ppt_pl2_sppt_attr_group, &ppt_pl2_sppt },
	{ &ppt_pl3_fppt_attr_group, &ppt_pl3_fppt },
	{},
};

/**
 * lwmi_om_fw_attr_add() - Register all firmware_attributes_class members
 * @priv: The Other Mode driver data.
 *
 * Return: Either 0, or an error code.
 */
static int lwmi_om_fw_attr_add(struct lwmi_om_priv *priv)
{
	unsigned int i;
	int err;

	priv->ida_id = ida_alloc(&lwmi_om_ida, GFP_KERNEL);
	if (priv->ida_id < 0)
		return priv->ida_id;

	priv->fw_attr_dev = device_create(&firmware_attributes_class, NULL,
					  MKDEV(0, 0), NULL, "%s-%u",
					  LWMI_OM_FW_ATTR_BASE_PATH,
					  priv->ida_id);
	if (IS_ERR(priv->fw_attr_dev)) {
		err = PTR_ERR(priv->fw_attr_dev);
		goto err_free_ida;
	}

	priv->fw_attr_kset = kset_create_and_add("attributes", NULL,
						 &priv->fw_attr_dev->kobj);
	if (!priv->fw_attr_kset) {
		err = -ENOMEM;
		goto err_destroy_classdev;
	}

	for (i = 0; i < ARRAY_SIZE(cd01_attr_groups) - 1; i++) {
		err = sysfs_create_group(&priv->fw_attr_kset->kobj,
					 cd01_attr_groups[i].attr_group);
		if (err)
			goto err_remove_groups;

		cd01_attr_groups[i].tunable_attr->dev = &priv->wdev->dev;
	}
	return 0;

err_remove_groups:
	while (i--)
		sysfs_remove_group(&priv->fw_attr_kset->kobj,
				   cd01_attr_groups[i].attr_group);

	kset_unregister(priv->fw_attr_kset);

err_destroy_classdev:
	device_unregister(priv->fw_attr_dev);

err_free_ida:
	ida_free(&lwmi_om_ida, priv->ida_id);
	return err;
}

/**
 * lwmi_om_fw_attr_remove() - Unregister all capability data attribute groups
 * @priv: the lenovo-wmi-other driver data.
 */
static void lwmi_om_fw_attr_remove(struct lwmi_om_priv *priv)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(cd01_attr_groups) - 1; i++)
		sysfs_remove_group(&priv->fw_attr_kset->kobj,
				   cd01_attr_groups[i].attr_group);

	kset_unregister(priv->fw_attr_kset);
	device_unregister(priv->fw_attr_dev);
}

/**
 * lwmi_om_master_bind() - Bind all components of the other mode driver
 * @dev: The lenovo-wmi-other driver basic device.
 *
 * Call component_bind_all to bind the lenovo-wmi-capdata01 driver to the
 * lenovo-wmi-other master driver. On success, assign the capability data 01
 * list pointer to the driver data struct for later access. This pointer
 * is only valid while the capdata01 interface exists. Finally, register all
 * firmware attribute groups.
 *
 * Return: 0 on success, or an error code.
 */
static int lwmi_om_master_bind(struct device *dev)
{
	struct lwmi_om_priv *priv = dev_get_drvdata(dev);
	struct cd01_list *tmp_list;
	int ret;

	ret = component_bind_all(dev, &tmp_list);
	if (ret)
		return ret;

	priv->cd01_list = tmp_list;
	if (!priv->cd01_list)
		return -ENODEV;

	return lwmi_om_fw_attr_add(priv);
}

/**
 * lwmi_om_master_unbind() - Unbind all components of the other mode driver
 * @dev: The lenovo-wmi-other driver basic device
 *
 * Unregister all capability data attribute groups. Then call
 * component_unbind_all to unbind the lenovo-wmi-capdata01 driver from the
 * lenovo-wmi-other master driver. Finally, free the IDA for this device.
 */
static void lwmi_om_master_unbind(struct device *dev)
{
	struct lwmi_om_priv *priv = dev_get_drvdata(dev);

	lwmi_om_fw_attr_remove(priv);
	component_unbind_all(dev, NULL);
}

static const struct component_master_ops lwmi_om_master_ops = {
	.bind = lwmi_om_master_bind,
	.unbind = lwmi_om_master_unbind,
};

static int lwmi_other_probe(struct wmi_device *wdev, const void *context)
{
	struct component_match *master_match = NULL;
	struct lwmi_om_priv *priv;

	priv = devm_kzalloc(&wdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->wdev = wdev;
	dev_set_drvdata(&wdev->dev, priv);

	component_match_add(&wdev->dev, &master_match, lwmi_cd01_match, NULL);
	if (IS_ERR(master_match))
		return PTR_ERR(master_match);

	return component_master_add_with_match(&wdev->dev, &lwmi_om_master_ops,
					       master_match);
}

static void lwmi_other_remove(struct wmi_device *wdev)
{
	struct lwmi_om_priv *priv = dev_get_drvdata(&wdev->dev);

	component_master_del(&wdev->dev, &lwmi_om_master_ops);
	ida_free(&lwmi_om_ida, priv->ida_id);
}

static const struct wmi_device_id lwmi_other_id_table[] = {
	{ LENOVO_OTHER_MODE_GUID, NULL },
	{}
};

static struct wmi_driver lwmi_other_driver = {
	.driver = {
		.name = "lenovo_wmi_other",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = lwmi_other_id_table,
	.probe = lwmi_other_probe,
	.remove = lwmi_other_remove,
	.no_singleton = true,
};

module_wmi_driver(lwmi_other_driver);

MODULE_IMPORT_NS("LENOVO_WMI_CD01");
MODULE_IMPORT_NS("LENOVO_WMI_HELPERS");
MODULE_DEVICE_TABLE(wmi, lwmi_other_id_table);
MODULE_AUTHOR("Derek J. Clark <derekjohn.clark@gmail.com>");
MODULE_DESCRIPTION("Lenovo Other Mode WMI Driver");
MODULE_LICENSE("GPL");
