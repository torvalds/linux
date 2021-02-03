// SPDX-License-Identifier: GPL-2.0
/*
 * Functions corresponding to integer type attributes under BIOS Integer GUID for use with
 * dell-wmi-sysman
 *
 *  Copyright (c) 2020 Dell Inc.
 */

#include "dell-wmi-sysman.h"

enum int_properties {MIN_VALUE = 6, MAX_VALUE, SCALAR_INCR};

get_instance_id(integer);

static ssize_t current_value_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int instance_id = get_integer_instance_id(kobj);
	union acpi_object *obj;
	ssize_t ret;

	if (instance_id < 0)
		return instance_id;

	/* need to use specific instance_id and guid combination to get right data */
	obj = get_wmiobj_pointer(instance_id, DELL_WMI_BIOS_INTEGER_ATTRIBUTE_GUID);
	if (!obj)
		return -EIO;
	if (obj->package.elements[CURRENT_VAL].type != ACPI_TYPE_INTEGER) {
		kfree(obj);
		return -EINVAL;
	}
	ret = snprintf(buf, PAGE_SIZE, "%lld\n", obj->package.elements[CURRENT_VAL].integer.value);
	kfree(obj);
	return ret;
}

/**
 * validate_integer_input() - Validate input of current_value against lower and upper bound
 * @instance_id: The instance on which input is validated
 * @buf: Input value
 */
static int validate_integer_input(int instance_id, char *buf)
{
	int in_val;
	int ret;

	ret = kstrtoint(buf, 0, &in_val);
	if (ret)
		return ret;
	if (in_val < wmi_priv.integer_data[instance_id].min_value ||
			in_val > wmi_priv.integer_data[instance_id].max_value)
		return -EINVAL;

	/* workaround for BIOS error.
	 * validate input to avoid setting 0 when integer input passed with + sign
	 */
	if (*buf == '+')
		memmove(buf, (buf + 1), strlen(buf + 1) + 1);

	return ret;
}

attribute_s_property_show(display_name_language_code, integer);
static struct kobj_attribute integer_displ_langcode =
	__ATTR_RO(display_name_language_code);

attribute_s_property_show(display_name, integer);
static struct kobj_attribute integer_displ_name =
	__ATTR_RO(display_name);

attribute_n_property_show(default_value, integer);
static struct kobj_attribute integer_default_val =
	__ATTR_RO(default_value);

attribute_property_store(current_value, integer);
static struct kobj_attribute integer_current_val =
	__ATTR_RW_MODE(current_value, 0600);

attribute_s_property_show(dell_modifier, integer);
static struct kobj_attribute integer_modifier =
	__ATTR_RO(dell_modifier);

attribute_n_property_show(min_value, integer);
static struct kobj_attribute integer_lower_bound =
	__ATTR_RO(min_value);

attribute_n_property_show(max_value, integer);
static struct kobj_attribute integer_upper_bound =
	__ATTR_RO(max_value);

attribute_n_property_show(scalar_increment, integer);
static struct kobj_attribute integer_scalar_increment =
	__ATTR_RO(scalar_increment);

static ssize_t type_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	return sprintf(buf, "integer\n");
}
static struct kobj_attribute integer_type =
	__ATTR_RO(type);

static struct attribute *integer_attrs[] = {
	&integer_displ_langcode.attr,
	&integer_displ_name.attr,
	&integer_default_val.attr,
	&integer_current_val.attr,
	&integer_modifier.attr,
	&integer_lower_bound.attr,
	&integer_upper_bound.attr,
	&integer_scalar_increment.attr,
	&integer_type.attr,
	NULL,
};

static const struct attribute_group integer_attr_group = {
	.attrs = integer_attrs,
};

int alloc_int_data(void)
{
	int ret = 0;

	wmi_priv.integer_instances_count = get_instance_count(DELL_WMI_BIOS_INTEGER_ATTRIBUTE_GUID);
	wmi_priv.integer_data = kcalloc(wmi_priv.integer_instances_count,
					sizeof(struct integer_data), GFP_KERNEL);
	if (!wmi_priv.integer_data) {
		wmi_priv.integer_instances_count = 0;
		ret = -ENOMEM;
	}
	return ret;
}

/**
 * populate_int_data() - Populate all properties of an instance under integer attribute
 * @integer_obj: ACPI object with integer data
 * @instance_id: The instance to enumerate
 * @attr_name_kobj: The parent kernel object
 */
int populate_int_data(union acpi_object *integer_obj, int instance_id,
			struct kobject *attr_name_kobj)
{
	wmi_priv.integer_data[instance_id].attr_name_kobj = attr_name_kobj;
	strlcpy_attr(wmi_priv.integer_data[instance_id].attribute_name,
		integer_obj[ATTR_NAME].string.pointer);
	strlcpy_attr(wmi_priv.integer_data[instance_id].display_name_language_code,
		integer_obj[DISPL_NAME_LANG_CODE].string.pointer);
	strlcpy_attr(wmi_priv.integer_data[instance_id].display_name,
		integer_obj[DISPLAY_NAME].string.pointer);
	wmi_priv.integer_data[instance_id].default_value =
		(uintptr_t)integer_obj[DEFAULT_VAL].string.pointer;
	strlcpy_attr(wmi_priv.integer_data[instance_id].dell_modifier,
		integer_obj[MODIFIER].string.pointer);
	wmi_priv.integer_data[instance_id].min_value =
		(uintptr_t)integer_obj[MIN_VALUE].string.pointer;
	wmi_priv.integer_data[instance_id].max_value =
		(uintptr_t)integer_obj[MAX_VALUE].string.pointer;
	wmi_priv.integer_data[instance_id].scalar_increment =
		(uintptr_t)integer_obj[SCALAR_INCR].string.pointer;

	return sysfs_create_group(attr_name_kobj, &integer_attr_group);
}

/**
 * exit_int_attributes() - Clear all attribute data
 *
 * Clears all data allocated for this group of attributes
 */
void exit_int_attributes(void)
{
	int instance_id;

	for (instance_id = 0; instance_id < wmi_priv.integer_instances_count; instance_id++) {
		if (wmi_priv.integer_data[instance_id].attr_name_kobj)
			sysfs_remove_group(wmi_priv.integer_data[instance_id].attr_name_kobj,
								&integer_attr_group);
	}
	kfree(wmi_priv.integer_data);
}
