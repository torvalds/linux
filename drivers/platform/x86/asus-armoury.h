/* SPDX-License-Identifier: GPL-2.0
 *
 * Definitions for kernel modules using asus-armoury driver
 *
 * Copyright (c) 2024 Luke Jones <luke@ljones.dev>
 */

#ifndef _ASUS_ARMOURY_H_
#define _ASUS_ARMOURY_H_

#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#define DRIVER_NAME "asus-armoury"

/**
 * armoury_attr_uint_store() - Send an uint to WMI method if within min/max.
 * @kobj: Pointer to the driver object.
 * @attr: Pointer to the attribute calling this function.
 * @buf: The buffer to read from, this is parsed to `uint` type.
 * @count: Required by sysfs attribute macros, pass in from the callee attr.
 * @min: Minimum accepted value. Below this returns -EINVAL.
 * @max: Maximum accepted value. Above this returns -EINVAL.
 * @store_value: Pointer to where the parsed value should be stored.
 * @wmi_dev: The WMI function ID to use.
 *
 * This function is intended to be generic so it can be called from any "_store"
 * attribute which works only with integers.
 *
 * Integers to be sent to the WMI method is inclusive range checked and
 * an error returned if out of range.
 *
 * If the value is valid and WMI is success then the sysfs attribute is notified
 * and if asus_bios_requires_reboot() is true then reboot attribute
 * is also notified.
 *
 * Returns: Either count, or an error.
 */
ssize_t armoury_attr_uint_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t count, u32 min, u32 max,
				u32 *store_value, u32 wmi_dev);

/**
 * armoury_attr_uint_show() - Receive an uint from a WMI method.
 * @kobj: Pointer to the driver object.
 * @attr: Pointer to the attribute calling this function.
 * @buf: The buffer to write to, as an `uint` type.
 * @wmi_dev: The WMI function ID to use.
 *
 * This function is intended to be generic so it can be called from any "_show"
 * attribute which works only with integers.
 *
 * Returns: Either count, or an error.
 */
ssize_t armoury_attr_uint_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf, u32 wmi_dev);

#define __ASUS_ATTR_RO(_func, _name)					\
	{								\
		.attr = { .name = __stringify(_name), .mode = 0444 },	\
		.show = _func##_##_name##_show,				\
	}

#define __ASUS_ATTR_RO_AS(_name, _show)					\
	{								\
		.attr = { .name = __stringify(_name), .mode = 0444 },	\
		.show = _show,						\
	}

#define __ASUS_ATTR_RW(_func, _name) \
	__ATTR(_name, 0644, _func##_##_name##_show, _func##_##_name##_store)

#define __WMI_STORE_INT(_attr, _min, _max, _wmi)				\
	static ssize_t _attr##_store(struct kobject *kobj,			\
				     struct kobj_attribute *attr,		\
				     const char *buf, size_t count)		\
	{									\
		return armoury_attr_uint_store(kobj, attr, buf, count, _min,	\
					_max, NULL, _wmi);			\
	}

#define ASUS_WMI_SHOW_INT(_attr, _wmi)						\
	static ssize_t _attr##_show(struct kobject *kobj,			\
				    struct kobj_attribute *attr, char *buf)	\
	{									\
		return armoury_attr_uint_show(kobj, attr, buf, _wmi);		\
	}

/* Create functions and attributes for use in other macros or on their own */

/* Shows a formatted static variable */
#define __ATTR_SHOW_FMT(_prop, _attrname, _fmt, _val)				\
	static ssize_t _attrname##_##_prop##_show(				\
		struct kobject *kobj, struct kobj_attribute *attr, char *buf)	\
	{									\
		return sysfs_emit(buf, _fmt, _val);				\
	}									\
	static struct kobj_attribute attr_##_attrname##_##_prop =		\
		__ASUS_ATTR_RO(_attrname, _prop)

#define __ATTR_RO_INT_GROUP_ENUM(_attrname, _wmi, _fsname, _possible, _dispname)\
	ASUS_WMI_SHOW_INT(_attrname##_current_value, _wmi);		\
	static struct kobj_attribute attr_##_attrname##_current_value =		\
		__ASUS_ATTR_RO(_attrname, current_value);			\
	__ATTR_SHOW_FMT(display_name, _attrname, "%s\n", _dispname);		\
	__ATTR_SHOW_FMT(possible_values, _attrname, "%s\n", _possible);		\
	static struct kobj_attribute attr_##_attrname##_type =			\
		__ASUS_ATTR_RO_AS(type, enum_type_show);			\
	static struct attribute *_attrname##_attrs[] = {			\
		&attr_##_attrname##_current_value.attr,				\
		&attr_##_attrname##_display_name.attr,				\
		&attr_##_attrname##_possible_values.attr,			\
		&attr_##_attrname##_type.attr,					\
		NULL								\
	};									\
	static const struct attribute_group _attrname##_attr_group = {		\
		.name = _fsname, .attrs = _attrname##_attrs			\
	}

#define __ATTR_RW_INT_GROUP_ENUM(_attrname, _minv, _maxv, _wmi, _fsname,\
				 _possible, _dispname)			\
	__WMI_STORE_INT(_attrname##_current_value, _minv, _maxv, _wmi);	\
	ASUS_WMI_SHOW_INT(_attrname##_current_value, _wmi);	\
	static struct kobj_attribute attr_##_attrname##_current_value =	\
		__ASUS_ATTR_RW(_attrname, current_value);		\
	__ATTR_SHOW_FMT(display_name, _attrname, "%s\n", _dispname);	\
	__ATTR_SHOW_FMT(possible_values, _attrname, "%s\n", _possible);	\
	static struct kobj_attribute attr_##_attrname##_type =		\
		__ASUS_ATTR_RO_AS(type, enum_type_show);		\
	static struct attribute *_attrname##_attrs[] = {		\
		&attr_##_attrname##_current_value.attr,			\
		&attr_##_attrname##_display_name.attr,			\
		&attr_##_attrname##_possible_values.attr,		\
		&attr_##_attrname##_type.attr,				\
		NULL							\
	};								\
	static const struct attribute_group _attrname##_attr_group = {	\
		.name = _fsname, .attrs = _attrname##_attrs		\
	}

/* Boolean style enumeration, base macro. Requires adding show/store */
#define __ATTR_GROUP_ENUM(_attrname, _fsname, _possible, _dispname)	\
	__ATTR_SHOW_FMT(display_name, _attrname, "%s\n", _dispname);	\
	__ATTR_SHOW_FMT(possible_values, _attrname, "%s\n", _possible);	\
	static struct kobj_attribute attr_##_attrname##_type =		\
		__ASUS_ATTR_RO_AS(type, enum_type_show);		\
	static struct attribute *_attrname##_attrs[] = {		\
		&attr_##_attrname##_current_value.attr,			\
		&attr_##_attrname##_display_name.attr,			\
		&attr_##_attrname##_possible_values.attr,		\
		&attr_##_attrname##_type.attr,				\
		NULL							\
	};								\
	static const struct attribute_group _attrname##_attr_group = {	\
		.name = _fsname, .attrs = _attrname##_attrs		\
	}

#define ASUS_ATTR_GROUP_BOOL_RO(_attrname, _fsname, _wmi, _dispname)	\
	__ATTR_RO_INT_GROUP_ENUM(_attrname, _wmi, _fsname, "0;1", _dispname)


#define ASUS_ATTR_GROUP_BOOL_RW(_attrname, _fsname, _wmi, _dispname)	\
	__ATTR_RW_INT_GROUP_ENUM(_attrname, 0, 1, _wmi, _fsname, "0;1", _dispname)

#define ASUS_ATTR_GROUP_ENUM_INT_RO(_attrname, _fsname, _wmi, _possible, _dispname)	\
	__ATTR_RO_INT_GROUP_ENUM(_attrname, _wmi, _fsname, _possible, _dispname)

/*
 * Requires <name>_current_value_show(), <name>_current_value_show()
 */
#define ASUS_ATTR_GROUP_BOOL(_attrname, _fsname, _dispname)		\
	static struct kobj_attribute attr_##_attrname##_current_value =	\
		__ASUS_ATTR_RW(_attrname, current_value);		\
	__ATTR_GROUP_ENUM(_attrname, _fsname, "0;1", _dispname)

/*
 * Requires <name>_current_value_show(), <name>_current_value_show()
 * and <name>_possible_values_show()
 */
#define ASUS_ATTR_GROUP_ENUM(_attrname, _fsname, _dispname)			\
	__ATTR_SHOW_FMT(display_name, _attrname, "%s\n", _dispname);		\
	static struct kobj_attribute attr_##_attrname##_current_value =		\
		__ASUS_ATTR_RW(_attrname, current_value);			\
	static struct kobj_attribute attr_##_attrname##_possible_values =	\
		__ASUS_ATTR_RO(_attrname, possible_values);			\
	static struct kobj_attribute attr_##_attrname##_type =			\
		__ASUS_ATTR_RO_AS(type, enum_type_show);			\
	static struct attribute *_attrname##_attrs[] = {			\
		&attr_##_attrname##_current_value.attr,				\
		&attr_##_attrname##_display_name.attr,				\
		&attr_##_attrname##_possible_values.attr,			\
		&attr_##_attrname##_type.attr,					\
		NULL								\
	};									\
	static const struct attribute_group _attrname##_attr_group = {		\
		.name = _fsname, .attrs = _attrname##_attrs			\
	}

#endif /* _ASUS_ARMOURY_H_ */
