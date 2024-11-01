// SPDX-License-Identifier: GPL-2.0
/*
 * Functions corresponding to string type attributes under BIOS String GUID for use with
 * dell-wmi-sysman
 *
 *  Copyright (c) 2020 Dell Inc.
 */

#include "dell-wmi-sysman.h"

enum string_properties {MIN_LEN = 6, MAX_LEN};

get_instance_id(str);

static ssize_t current_value_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int instance_id = get_str_instance_id(kobj);
	union acpi_object *obj;
	ssize_t ret;

	if (instance_id < 0)
		return -EIO;

	/* need to use specific instance_id and guid combination to get right data */
	obj = get_wmiobj_pointer(instance_id, DELL_WMI_BIOS_STRING_ATTRIBUTE_GUID);
	if (!obj)
		return -EIO;
	if (obj->package.elements[CURRENT_VAL].type != ACPI_TYPE_STRING) {
		kfree(obj);
		return -EINVAL;
	}
	ret = snprintf(buf, PAGE_SIZE, "%s\n", obj->package.elements[CURRENT_VAL].string.pointer);
	kfree(obj);
	return ret;
}

/**
 * validate_str_input() - Validate input of current_value against min and max lengths
 * @instance_id: The instance on which input is validated
 * @buf: Input value
 */
static int validate_str_input(int instance_id, const char *buf)
{
	int in_len = strlen(buf);

	if ((in_len < wmi_priv.str_data[instance_id].min_length) ||
			(in_len > wmi_priv.str_data[instance_id].max_length))
		return -EINVAL;

	return 0;
}

attribute_s_property_show(display_name_language_code, str);
static struct kobj_attribute str_displ_langcode =
		__ATTR_RO(display_name_language_code);

attribute_s_property_show(display_name, str);
static struct kobj_attribute str_displ_name =
		__ATTR_RO(display_name);

attribute_s_property_show(default_value, str);
static struct kobj_attribute str_default_val =
		__ATTR_RO(default_value);

attribute_property_store(current_value, str);
static struct kobj_attribute str_current_val =
		__ATTR_RW_MODE(current_value, 0600);

attribute_s_property_show(dell_modifier, str);
static struct kobj_attribute str_modifier =
		__ATTR_RO(dell_modifier);

attribute_n_property_show(min_length, str);
static struct kobj_attribute str_min_length =
		__ATTR_RO(min_length);

attribute_n_property_show(max_length, str);
static struct kobj_attribute str_max_length =
		__ATTR_RO(max_length);

static ssize_t type_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	return sprintf(buf, "string\n");
}
static struct kobj_attribute str_type =
	__ATTR_RO(type);

static struct attribute *str_attrs[] = {
	&str_displ_langcode.attr,
	&str_displ_name.attr,
	&str_default_val.attr,
	&str_current_val.attr,
	&str_modifier.attr,
	&str_min_length.attr,
	&str_max_length.attr,
	&str_type.attr,
	NULL,
};

static const struct attribute_group str_attr_group = {
	.attrs = str_attrs,
};

int alloc_str_data(void)
{
	int ret = 0;

	wmi_priv.str_instances_count = get_instance_count(DELL_WMI_BIOS_STRING_ATTRIBUTE_GUID);
	wmi_priv.str_data = kcalloc(wmi_priv.str_instances_count,
					sizeof(struct str_data), GFP_KERNEL);
	if (!wmi_priv.str_data) {
		wmi_priv.str_instances_count = 0;
		ret = -ENOMEM;
	}
	return ret;
}

/**
 * populate_str_data() - Populate all properties of an instance under string attribute
 * @str_obj: ACPI object with string data
 * @instance_id: The instance to enumerate
 * @attr_name_kobj: The parent kernel object
 */
int populate_str_data(union acpi_object *str_obj, int instance_id, struct kobject *attr_name_kobj)
{
	wmi_priv.str_data[instance_id].attr_name_kobj = attr_name_kobj;
	if (check_property_type(str, ATTR_NAME, ACPI_TYPE_STRING))
		return -EINVAL;
	strlcpy_attr(wmi_priv.str_data[instance_id].attribute_name,
		     str_obj[ATTR_NAME].string.pointer);
	if (check_property_type(str, DISPL_NAME_LANG_CODE, ACPI_TYPE_STRING))
		return -EINVAL;
	strlcpy_attr(wmi_priv.str_data[instance_id].display_name_language_code,
		     str_obj[DISPL_NAME_LANG_CODE].string.pointer);
	if (check_property_type(str, DISPLAY_NAME, ACPI_TYPE_STRING))
		return -EINVAL;
	strlcpy_attr(wmi_priv.str_data[instance_id].display_name,
		     str_obj[DISPLAY_NAME].string.pointer);
	if (check_property_type(str, DEFAULT_VAL, ACPI_TYPE_STRING))
		return -EINVAL;
	strlcpy_attr(wmi_priv.str_data[instance_id].default_value,
		     str_obj[DEFAULT_VAL].string.pointer);
	if (check_property_type(str, MODIFIER, ACPI_TYPE_STRING))
		return -EINVAL;
	strlcpy_attr(wmi_priv.str_data[instance_id].dell_modifier,
		     str_obj[MODIFIER].string.pointer);
	if (check_property_type(str, MIN_LEN, ACPI_TYPE_INTEGER))
		return -EINVAL;
	wmi_priv.str_data[instance_id].min_length = (uintptr_t)str_obj[MIN_LEN].string.pointer;
	if (check_property_type(str, MAX_LEN, ACPI_TYPE_INTEGER))
		return -EINVAL;
	wmi_priv.str_data[instance_id].max_length = (uintptr_t) str_obj[MAX_LEN].string.pointer;

	return sysfs_create_group(attr_name_kobj, &str_attr_group);
}

/**
 * exit_str_attributes() - Clear all attribute data
 *
 * Clears all data allocated for this group of attributes
 */
void exit_str_attributes(void)
{
	int instance_id;

	for (instance_id = 0; instance_id < wmi_priv.str_instances_count; instance_id++) {
		if (wmi_priv.str_data[instance_id].attr_name_kobj)
			sysfs_remove_group(wmi_priv.str_data[instance_id].attr_name_kobj,
								&str_attr_group);
	}
	wmi_priv.str_instances_count = 0;

	kfree(wmi_priv.str_data);
	wmi_priv.str_data = NULL;
}
