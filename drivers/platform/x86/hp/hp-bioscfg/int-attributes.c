// SPDX-License-Identifier: GPL-2.0
/*
 * Functions corresponding to integer type attributes under
 * BIOS Enumeration GUID for use with hp-bioscfg driver.
 *
 * Copyright (c) 2022 Hewlett-Packard Inc.
 */

#include "bioscfg.h"

GET_INSTANCE_ID(integer);

static ssize_t current_value_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int instance_id = get_integer_instance_id(kobj);

	if (instance_id < 0)
		return -EIO;

	return sysfs_emit(buf, "%d\n",
			  bioscfg_drv.integer_data[instance_id].current_value);
}

/**
 * validate_integer_input() -
 * Validate input of current_value against lower and upper bound
 *
 * @instance_id: The instance on which input is validated
 * @buf: Input value
 */
static int validate_integer_input(int instance_id, char *buf)
{
	int in_val;
	int ret;
	struct integer_data *integer_data = &bioscfg_drv.integer_data[instance_id];

	/* BIOS treats it as a read only attribute */
	if (integer_data->common.is_readonly)
		return -EIO;

	ret = kstrtoint(buf, 10, &in_val);
	if (ret < 0)
		return ret;

	if (in_val < integer_data->lower_bound ||
	    in_val > integer_data->upper_bound)
		return -ERANGE;

	return 0;
}

static void update_integer_value(int instance_id, char *attr_value)
{
	int in_val;
	int ret;
	struct integer_data *integer_data = &bioscfg_drv.integer_data[instance_id];

	ret = kstrtoint(attr_value, 10, &in_val);
	if (ret == 0)
		integer_data->current_value = in_val;
	else
		pr_warn("Invalid integer value found: %s\n", attr_value);
}

ATTRIBUTE_S_COMMON_PROPERTY_SHOW(display_name, integer);
static struct kobj_attribute integer_display_name =
	__ATTR_RO(display_name);

ATTRIBUTE_PROPERTY_STORE(current_value, integer);
static struct kobj_attribute integer_current_val =
	__ATTR_RW_MODE(current_value, 0644);

ATTRIBUTE_N_PROPERTY_SHOW(lower_bound, integer);
static struct kobj_attribute integer_lower_bound =
	__ATTR_RO(lower_bound);

ATTRIBUTE_N_PROPERTY_SHOW(upper_bound, integer);
static struct kobj_attribute integer_upper_bound =
	__ATTR_RO(upper_bound);

ATTRIBUTE_N_PROPERTY_SHOW(scalar_increment, integer);
static struct kobj_attribute integer_scalar_increment =
	__ATTR_RO(scalar_increment);

static ssize_t type_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	return sysfs_emit(buf, "integer\n");
}

static struct kobj_attribute integer_type =
	__ATTR_RO(type);

static struct attribute *integer_attrs[] = {
	&common_display_langcode.attr,
	&integer_display_name.attr,
	&integer_current_val.attr,
	&integer_lower_bound.attr,
	&integer_upper_bound.attr,
	&integer_scalar_increment.attr,
	&integer_type.attr,
	NULL
};

static const struct attribute_group integer_attr_group = {
	.attrs = integer_attrs,
};

int hp_alloc_integer_data(void)
{
	bioscfg_drv.integer_instances_count = hp_get_instance_count(HP_WMI_BIOS_INTEGER_GUID);
	bioscfg_drv.integer_data = kcalloc(bioscfg_drv.integer_instances_count,
					   sizeof(*bioscfg_drv.integer_data), GFP_KERNEL);

	if (!bioscfg_drv.integer_data) {
		bioscfg_drv.integer_instances_count = 0;
		return -ENOMEM;
	}
	return 0;
}

/* Expected Values types associated with each element */
static const acpi_object_type expected_integer_types[] = {
	[NAME] = ACPI_TYPE_STRING,
	[VALUE] = ACPI_TYPE_STRING,
	[PATH] = ACPI_TYPE_STRING,
	[IS_READONLY] = ACPI_TYPE_INTEGER,
	[DISPLAY_IN_UI] = ACPI_TYPE_INTEGER,
	[REQUIRES_PHYSICAL_PRESENCE] = ACPI_TYPE_INTEGER,
	[SEQUENCE] = ACPI_TYPE_INTEGER,
	[PREREQUISITES_SIZE] = ACPI_TYPE_INTEGER,
	[PREREQUISITES] = ACPI_TYPE_STRING,
	[SECURITY_LEVEL] = ACPI_TYPE_INTEGER,
	[INT_LOWER_BOUND] = ACPI_TYPE_INTEGER,
	[INT_UPPER_BOUND] = ACPI_TYPE_INTEGER,
	[INT_SCALAR_INCREMENT] = ACPI_TYPE_INTEGER,
};

static int hp_populate_integer_elements_from_package(union acpi_object *integer_obj,
						     int integer_obj_count,
						     int instance_id)
{
	char *str_value = NULL;
	int value_len;
	int ret;
	u32 int_value;
	int elem;
	int reqs;
	int eloc;
	int size;
	struct integer_data *integer_data = &bioscfg_drv.integer_data[instance_id];

	if (!integer_obj)
		return -EINVAL;

	for (elem = 1, eloc = 1; elem < integer_obj_count; elem++, eloc++) {
		/* ONLY look at the first INTEGER_ELEM_CNT elements */
		if (eloc == INT_ELEM_CNT)
			goto exit_integer_package;

		switch (integer_obj[elem].type) {
		case ACPI_TYPE_STRING:
			if (elem != PREREQUISITES) {
				ret = hp_convert_hexstr_to_str(integer_obj[elem].string.pointer,
							       integer_obj[elem].string.length,
							       &str_value, &value_len);
				if (ret)
					continue;
			}
			break;
		case ACPI_TYPE_INTEGER:
			int_value = (u32)integer_obj[elem].integer.value;
			break;
		default:
			pr_warn("Unsupported object type [%d]\n", integer_obj[elem].type);
			continue;
		}
		/* Check that both expected and read object type match */
		if (expected_integer_types[eloc] != integer_obj[elem].type) {
			pr_err("Error expected type %d for elem %d, but got type %d instead\n",
			       expected_integer_types[eloc], elem, integer_obj[elem].type);
			return -EIO;
		}
		/* Assign appropriate element value to corresponding field*/
		switch (eloc) {
		case VALUE:
			ret = kstrtoint(str_value, 10, &int_value);
			if (ret)
				continue;

			integer_data->current_value = int_value;
			break;
		case PATH:
			strscpy(integer_data->common.path, str_value,
				sizeof(integer_data->common.path));
			break;
		case IS_READONLY:
			integer_data->common.is_readonly = int_value;
			break;
		case DISPLAY_IN_UI:
			integer_data->common.display_in_ui = int_value;
			break;
		case REQUIRES_PHYSICAL_PRESENCE:
			integer_data->common.requires_physical_presence = int_value;
			break;
		case SEQUENCE:
			integer_data->common.sequence = int_value;
			break;
		case PREREQUISITES_SIZE:
			if (integer_data->common.prerequisites_size > MAX_PREREQUISITES_SIZE)
				pr_warn("Prerequisites size value exceeded the maximum number of elements supported or data may be malformed\n");
			/*
			 * This HACK is needed to keep the expected
			 * element list pointing to the right obj[elem].type
			 * when the size is zero. PREREQUISITES
			 * object is omitted by BIOS when the size is
			 * zero.
			 */
			if (integer_data->common.prerequisites_size == 0)
				eloc++;
			break;
		case PREREQUISITES:
			size = min_t(u32, integer_data->common.prerequisites_size, MAX_PREREQUISITES_SIZE);

			for (reqs = 0; reqs < size; reqs++) {
				if (elem >= integer_obj_count) {
					pr_err("Error elem-objects package is too small\n");
					return -EINVAL;
				}

				ret = hp_convert_hexstr_to_str(integer_obj[elem + reqs].string.pointer,
							       integer_obj[elem + reqs].string.length,
							       &str_value, &value_len);

				if (ret)
					continue;

				strscpy(integer_data->common.prerequisites[reqs],
					str_value,
					sizeof(integer_data->common.prerequisites[reqs]));
				kfree(str_value);
			}
			break;

		case SECURITY_LEVEL:
			integer_data->common.security_level = int_value;
			break;
		case INT_LOWER_BOUND:
			integer_data->lower_bound = int_value;
			break;
		case INT_UPPER_BOUND:
			integer_data->upper_bound = int_value;
			break;
		case INT_SCALAR_INCREMENT:
			integer_data->scalar_increment = int_value;
			break;
		default:
			pr_warn("Invalid element: %d found in Integer attribute or data may be malformed\n", elem);
			break;
		}
	}
exit_integer_package:
	kfree(str_value);
	return 0;
}

/**
 * hp_populate_integer_package_data() -
 * Populate all properties of an instance under integer attribute
 *
 * @integer_obj: ACPI object with integer data
 * @instance_id: The instance to enumerate
 * @attr_name_kobj: The parent kernel object
 */
int hp_populate_integer_package_data(union acpi_object *integer_obj,
				     int instance_id,
				     struct kobject *attr_name_kobj)
{
	struct integer_data *integer_data = &bioscfg_drv.integer_data[instance_id];

	integer_data->attr_name_kobj = attr_name_kobj;
	hp_populate_integer_elements_from_package(integer_obj,
						  integer_obj->package.count,
						  instance_id);
	hp_update_attribute_permissions(integer_data->common.is_readonly,
					&integer_current_val);
	hp_friendly_user_name_update(integer_data->common.path,
				     attr_name_kobj->name,
				     integer_data->common.display_name,
				     sizeof(integer_data->common.display_name));
	return sysfs_create_group(attr_name_kobj, &integer_attr_group);
}

static int hp_populate_integer_elements_from_buffer(u8 *buffer_ptr, u32 *buffer_size,
						    int instance_id)
{
	char *dst = NULL;
	int dst_size = *buffer_size / sizeof(u16);
	struct integer_data *integer_data = &bioscfg_drv.integer_data[instance_id];
	int ret = 0;

	dst = kcalloc(dst_size, sizeof(char), GFP_KERNEL);
	if (!dst)
		return -ENOMEM;

	/*
	 * Only data relevant to this driver and its functionality is
	 * read. BIOS defines the order in which each * element is
	 * read. Element 0 data is not relevant to this
	 * driver hence it is ignored. For clarity, all element names
	 * (DISPLAY_IN_UI) which defines the order in which is read
	 * and the name matches the variable where the data is stored.
	 *
	 * In earlier implementation, reported errors were ignored
	 * causing the data to remain uninitialized. It is not
	 * possible to determine if data read from BIOS is valid or
	 * not. It is for this reason functions may return a error
	 * without validating the data itself.
	 */

	// VALUE:
	integer_data->current_value = 0;

	hp_get_string_from_buffer(&buffer_ptr, buffer_size, dst, dst_size);
	ret = kstrtoint(dst, 10, &integer_data->current_value);
	if (ret)
		pr_warn("Unable to convert string to integer: %s\n", dst);
	kfree(dst);

	// COMMON:
	ret = hp_get_common_data_from_buffer(&buffer_ptr, buffer_size, &integer_data->common);
	if (ret < 0)
		goto buffer_exit;

	// INT_LOWER_BOUND:
	ret = hp_get_integer_from_buffer(&buffer_ptr, buffer_size,
					 &integer_data->lower_bound);
	if (ret < 0)
		goto buffer_exit;

	// INT_UPPER_BOUND:
	ret = hp_get_integer_from_buffer(&buffer_ptr, buffer_size,
					 &integer_data->upper_bound);
	if (ret < 0)
		goto buffer_exit;

	// INT_SCALAR_INCREMENT:
	ret = hp_get_integer_from_buffer(&buffer_ptr, buffer_size,
					 &integer_data->scalar_increment);

buffer_exit:
	return ret;
}

/**
 * hp_populate_integer_buffer_data() -
 * Populate all properties of an instance under integer attribute
 *
 * @buffer_ptr: Buffer pointer
 * @buffer_size: Buffer size
 * @instance_id: The instance to enumerate
 * @attr_name_kobj: The parent kernel object
 */
int hp_populate_integer_buffer_data(u8 *buffer_ptr, u32 *buffer_size, int instance_id,
				    struct kobject *attr_name_kobj)
{
	struct integer_data *integer_data = &bioscfg_drv.integer_data[instance_id];
	int ret = 0;

	integer_data->attr_name_kobj = attr_name_kobj;

	/* Populate integer elements */
	ret = hp_populate_integer_elements_from_buffer(buffer_ptr, buffer_size,
						       instance_id);
	if (ret < 0)
		return ret;

	hp_update_attribute_permissions(integer_data->common.is_readonly,
					&integer_current_val);
	hp_friendly_user_name_update(integer_data->common.path,
				     attr_name_kobj->name,
				     integer_data->common.display_name,
				     sizeof(integer_data->common.display_name));

	return sysfs_create_group(attr_name_kobj, &integer_attr_group);
}

/**
 * hp_exit_integer_attributes() - Clear all attribute data
 *
 * Clears all data allocated for this group of attributes
 */
void hp_exit_integer_attributes(void)
{
	int instance_id;

	for (instance_id = 0; instance_id < bioscfg_drv.integer_instances_count;
	     instance_id++) {
		struct kobject *attr_name_kobj =
			bioscfg_drv.integer_data[instance_id].attr_name_kobj;

		if (attr_name_kobj)
			sysfs_remove_group(attr_name_kobj, &integer_attr_group);
	}
	bioscfg_drv.integer_instances_count = 0;

	kfree(bioscfg_drv.integer_data);
	bioscfg_drv.integer_data = NULL;
}
