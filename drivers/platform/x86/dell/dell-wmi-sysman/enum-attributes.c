// SPDX-License-Identifier: GPL-2.0
/*
 * Functions corresponding to enumeration type attributes under
 * BIOS Enumeration GUID for use with dell-wmi-sysman
 *
 *  Copyright (c) 2020 Dell Inc.
 */

#include "dell-wmi-sysman.h"

get_instance_id(enumeration);

static ssize_t current_value_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int instance_id = get_enumeration_instance_id(kobj);
	union acpi_object *obj;
	ssize_t ret;

	if (instance_id < 0)
		return instance_id;

	/* need to use specific instance_id and guid combination to get right data */
	obj = get_wmiobj_pointer(instance_id, DELL_WMI_BIOS_ENUMERATION_ATTRIBUTE_GUID);
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
 * validate_enumeration_input() - Validate input of current_value against possible values
 * @instance_id: The instance on which input is validated
 * @buf: Input value
 */
static int validate_enumeration_input(int instance_id, const char *buf)
{
	char *options, *tmp, *p;
	int ret = -EINVAL;

	options = tmp = kstrdup(wmi_priv.enumeration_data[instance_id].possible_values,
				 GFP_KERNEL);
	if (!options)
		return -ENOMEM;

	while ((p = strsep(&options, ";")) != NULL) {
		if (!*p)
			continue;
		if (!strcasecmp(p, buf)) {
			ret = 0;
			break;
		}
	}

	kfree(tmp);
	return ret;
}

attribute_s_property_show(display_name_language_code, enumeration);
static struct kobj_attribute displ_langcode =
		__ATTR_RO(display_name_language_code);

attribute_s_property_show(display_name, enumeration);
static struct kobj_attribute displ_name =
		__ATTR_RO(display_name);

attribute_s_property_show(default_value, enumeration);
static struct kobj_attribute default_val =
		__ATTR_RO(default_value);

attribute_property_store(current_value, enumeration);
static struct kobj_attribute current_val =
		__ATTR_RW_MODE(current_value, 0600);

attribute_s_property_show(dell_modifier, enumeration);
static struct kobj_attribute modifier =
		__ATTR_RO(dell_modifier);

attribute_s_property_show(dell_value_modifier, enumeration);
static struct kobj_attribute value_modfr =
		__ATTR_RO(dell_value_modifier);

attribute_s_property_show(possible_values, enumeration);
static struct kobj_attribute poss_val =
		__ATTR_RO(possible_values);

static ssize_t type_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	return sprintf(buf, "enumeration\n");
}
static struct kobj_attribute type =
		__ATTR_RO(type);

static struct attribute *enumeration_attrs[] = {
	&displ_langcode.attr,
	&displ_name.attr,
	&default_val.attr,
	&current_val.attr,
	&modifier.attr,
	&value_modfr.attr,
	&poss_val.attr,
	&type.attr,
	NULL,
};

static const struct attribute_group enumeration_attr_group = {
	.attrs = enumeration_attrs,
};

int alloc_enum_data(void)
{
	int ret = 0;

	wmi_priv.enumeration_instances_count =
		get_instance_count(DELL_WMI_BIOS_ENUMERATION_ATTRIBUTE_GUID);
	wmi_priv.enumeration_data = kcalloc(wmi_priv.enumeration_instances_count,
					sizeof(struct enumeration_data), GFP_KERNEL);
	if (!wmi_priv.enumeration_data) {
		wmi_priv.enumeration_instances_count = 0;
		ret = -ENOMEM;
	}
	return ret;
}

/**
 * populate_enum_data() - Populate all properties of an instance under enumeration attribute
 * @enumeration_obj: ACPI object with enumeration data
 * @instance_id: The instance to enumerate
 * @attr_name_kobj: The parent kernel object
 */
int populate_enum_data(union acpi_object *enumeration_obj, int instance_id,
			struct kobject *attr_name_kobj)
{
	int i, next_obj, value_modifier_count, possible_values_count;

	wmi_priv.enumeration_data[instance_id].attr_name_kobj = attr_name_kobj;
	strlcpy_attr(wmi_priv.enumeration_data[instance_id].attribute_name,
		enumeration_obj[ATTR_NAME].string.pointer);
	strlcpy_attr(wmi_priv.enumeration_data[instance_id].display_name_language_code,
		enumeration_obj[DISPL_NAME_LANG_CODE].string.pointer);
	strlcpy_attr(wmi_priv.enumeration_data[instance_id].display_name,
		enumeration_obj[DISPLAY_NAME].string.pointer);
	strlcpy_attr(wmi_priv.enumeration_data[instance_id].default_value,
		enumeration_obj[DEFAULT_VAL].string.pointer);
	strlcpy_attr(wmi_priv.enumeration_data[instance_id].dell_modifier,
		enumeration_obj[MODIFIER].string.pointer);

	next_obj = MODIFIER + 1;

	value_modifier_count = (uintptr_t)enumeration_obj[next_obj].string.pointer;

	for (i = 0; i < value_modifier_count; i++) {
		strcat(wmi_priv.enumeration_data[instance_id].dell_value_modifier,
			enumeration_obj[++next_obj].string.pointer);
		strcat(wmi_priv.enumeration_data[instance_id].dell_value_modifier, ";");
	}

	possible_values_count = (uintptr_t) enumeration_obj[++next_obj].string.pointer;

	for (i = 0; i < possible_values_count; i++) {
		strcat(wmi_priv.enumeration_data[instance_id].possible_values,
			enumeration_obj[++next_obj].string.pointer);
		strcat(wmi_priv.enumeration_data[instance_id].possible_values, ";");
	}

	return sysfs_create_group(attr_name_kobj, &enumeration_attr_group);
}

/**
 * exit_enum_attributes() - Clear all attribute data
 *
 * Clears all data allocated for this group of attributes
 */
void exit_enum_attributes(void)
{
	int instance_id;

	for (instance_id = 0; instance_id < wmi_priv.enumeration_instances_count; instance_id++) {
		if (wmi_priv.enumeration_data[instance_id].attr_name_kobj)
			sysfs_remove_group(wmi_priv.enumeration_data[instance_id].attr_name_kobj,
								&enumeration_attr_group);
	}
	wmi_priv.enumeration_instances_count = 0;

	kfree(wmi_priv.enumeration_data);
	wmi_priv.enumeration_data = NULL;
}
