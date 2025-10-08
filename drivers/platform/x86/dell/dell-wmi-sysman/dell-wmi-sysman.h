/* SPDX-License-Identifier: GPL-2.0
 * Definitions for kernel modules using Dell WMI System Management Driver
 *
 *  Copyright (c) 2020 Dell Inc.
 */

#ifndef _DELL_WMI_BIOS_ATTR_H_
#define _DELL_WMI_BIOS_ATTR_H_

#include <linux/wmi.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/capability.h>

#define DRIVER_NAME					"dell-wmi-sysman"
#define MAX_BUFF  512

#define DELL_WMI_BIOS_ENUMERATION_ATTRIBUTE_GUID	"F1DDEE52-063C-4784-A11E-8A06684B9BF5"
#define DELL_WMI_BIOS_INTEGER_ATTRIBUTE_GUID		"F1DDEE52-063C-4784-A11E-8A06684B9BFA"
#define DELL_WMI_BIOS_STRING_ATTRIBUTE_GUID		"F1DDEE52-063C-4784-A11E-8A06684B9BF9"
#define DELL_WMI_BIOS_PASSOBJ_ATTRIBUTE_GUID		"0894B8D6-44A6-4719-97D7-6AD24108BFD4"
#define DELL_WMI_BIOS_ATTRIBUTES_INTERFACE_GUID		"F1DDEE52-063C-4784-A11E-8A06684B9BF4"
#define DELL_WMI_BIOS_PASSWORD_INTERFACE_GUID		"70FE8229-D03B-4214-A1C6-1F884B1A892A"

struct enumeration_data {
	struct kobject *attr_name_kobj;
	char display_name_language_code[MAX_BUFF];
	char dell_value_modifier[MAX_BUFF];
	char possible_values[MAX_BUFF];
	char attribute_name[MAX_BUFF];
	char default_value[MAX_BUFF];
	char dell_modifier[MAX_BUFF];
	char display_name[MAX_BUFF];
};

struct integer_data {
	struct kobject *attr_name_kobj;
	char display_name_language_code[MAX_BUFF];
	char attribute_name[MAX_BUFF];
	char dell_modifier[MAX_BUFF];
	char display_name[MAX_BUFF];
	int scalar_increment;
	int default_value;
	int min_value;
	int max_value;
};

struct str_data {
	struct kobject *attr_name_kobj;
	char display_name_language_code[MAX_BUFF];
	char attribute_name[MAX_BUFF];
	char display_name[MAX_BUFF];
	char default_value[MAX_BUFF];
	char dell_modifier[MAX_BUFF];
	int min_length;
	int max_length;
};

struct po_data {
	struct kobject *attr_name_kobj;
	char attribute_name[MAX_BUFF];
	int min_password_length;
	int max_password_length;
};

struct wmi_sysman_priv {
	char current_admin_password[MAX_BUFF];
	char current_system_password[MAX_BUFF];
	struct wmi_device *password_attr_wdev;
	struct wmi_device *bios_attr_wdev;
	struct kset *authentication_dir_kset;
	struct kset *main_dir_kset;
	struct device *class_dev;
	struct enumeration_data *enumeration_data;
	int enumeration_instances_count;
	struct integer_data *integer_data;
	int integer_instances_count;
	struct str_data *str_data;
	int str_instances_count;
	struct po_data *po_data;
	int po_instances_count;
	bool pending_changes;
	struct mutex mutex;
};

/* global structure used by multiple WMI interfaces */
extern struct wmi_sysman_priv wmi_priv;

enum { ENUM, INT, STR, PO };

#define ENUM_MIN_ELEMENTS		8
#define INT_MIN_ELEMENTS		9
#define STR_MIN_ELEMENTS		8
#define PO_MIN_ELEMENTS			4

enum {
	ATTR_NAME,
	DISPL_NAME_LANG_CODE,
	DISPLAY_NAME,
	DEFAULT_VAL,
	CURRENT_VAL,
	MODIFIER
};

#define get_instance_id(type)							\
static int get_##type##_instance_id(struct kobject *kobj)			\
{										\
	int i;									\
	for (i = 0; i <= wmi_priv.type##_instances_count; i++) {		\
		if (!(strcmp(kobj->name, wmi_priv.type##_data[i].attribute_name)))\
			return i;						\
	}									\
	return -EIO;								\
}

#define attribute_s_property_show(name, type)					\
static ssize_t name##_show(struct kobject *kobj, struct kobj_attribute *attr,	\
			   char *buf)						\
{										\
	int i = get_##type##_instance_id(kobj);					\
	if (i >= 0)								\
		return sprintf(buf, "%s\n", wmi_priv.type##_data[i].name);	\
	return 0;								\
}

#define attribute_n_property_show(name, type)					\
static ssize_t name##_show(struct kobject *kobj, struct kobj_attribute *attr,	\
			   char *buf)						\
{										\
	int i = get_##type##_instance_id(kobj);					\
	if (i >= 0)								\
		return sprintf(buf, "%d\n", wmi_priv.type##_data[i].name);	\
	return 0;								\
}

#define attribute_property_store(curr_val, type)				\
static ssize_t curr_val##_store(struct kobject *kobj,				\
				struct kobj_attribute *attr,			\
				const char *buf, size_t count)			\
{										\
	char *p, *buf_cp;							\
	int i, ret = -EIO;							\
	buf_cp = kstrdup(buf, GFP_KERNEL);					\
	if (!buf_cp)								\
		return -ENOMEM;							\
	p = memchr(buf_cp, '\n', count);					\
										\
	if (p != NULL)								\
		*p = '\0';							\
	i = get_##type##_instance_id(kobj);					\
	if (i >= 0)								\
		ret = validate_##type##_input(i, buf_cp);			\
	if (!ret)								\
		ret = set_attribute(kobj->name, buf_cp);			\
	kfree(buf_cp);								\
	return ret ? ret : count;						\
}

#define check_property_type(attr, prop, valuetype)				\
	(attr##_obj[prop].type != valuetype)

union acpi_object *get_wmiobj_pointer(int instance_id, const char *guid_string);
int get_instance_count(const char *guid_string);
void strlcpy_attr(char *dest, char *src);

int populate_enum_data(union acpi_object *enumeration_obj, int instance_id,
			struct kobject *attr_name_kobj, u32 enum_property_count);
int alloc_enum_data(void);
void exit_enum_attributes(void);

int populate_int_data(union acpi_object *integer_obj, int instance_id,
			struct kobject *attr_name_kobj);
int alloc_int_data(void);
void exit_int_attributes(void);

int populate_str_data(union acpi_object *str_obj, int instance_id, struct kobject *attr_name_kobj);
int alloc_str_data(void);
void exit_str_attributes(void);

int populate_po_data(union acpi_object *po_obj, int instance_id, struct kobject *attr_name_kobj);
int alloc_po_data(void);
void exit_po_attributes(void);

int set_attribute(const char *a_name, const char *a_value);
int set_bios_defaults(u8 defType);

void exit_bios_attr_set_interface(void);
int init_bios_attr_set_interface(void);
int map_wmi_error(int error_code);
size_t calculate_string_buffer(const char *str);
size_t calculate_security_buffer(char *authentication);
void populate_security_buffer(char *buffer, char *authentication);
ssize_t populate_string_buffer(char *buffer, size_t buffer_len, const char *str);
int set_new_password(const char *password_type, const char *new);
int init_bios_attr_pass_interface(void);
void exit_bios_attr_pass_interface(void);

#endif
