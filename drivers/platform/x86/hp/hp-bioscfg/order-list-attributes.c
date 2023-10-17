// SPDX-License-Identifier: GPL-2.0
/*
 * Functions corresponding to ordered list type attributes under
 * BIOS ORDERED LIST GUID for use with hp-bioscfg driver.
 *
 * Copyright (c) 2022 HP Development Company, L.P.
 */

#include "bioscfg.h"

GET_INSTANCE_ID(ordered_list);

static ssize_t current_value_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int instance_id = get_ordered_list_instance_id(kobj);

	if (instance_id < 0)
		return -EIO;

	return sysfs_emit(buf, "%s\n",
			 bioscfg_drv.ordered_list_data[instance_id].current_value);
}

static int replace_char_str(u8 *buffer, char *repl_char, char *repl_with)
{
	char *src = buffer;
	int buflen = strlen(buffer);
	int item;

	if (buflen < 1)
		return -EINVAL;

	for (item = 0; item < buflen; item++)
		if (src[item] == *repl_char)
			src[item] = *repl_with;

	return 0;
}

/**
 * validate_ordered_list_input() -
 * Validate input of current_value against possible values
 *
 * @instance: The instance on which input is validated
 * @buf: Input value
 */
static int validate_ordered_list_input(int instance, char *buf)
{
	/* validation is done by BIOS. This validation function will
	 * convert semicolon to commas. BIOS uses commas as
	 * separators when reporting ordered-list values.
	 */
	return replace_char_str(buf, SEMICOLON_SEP, COMMA_SEP);
}

static void update_ordered_list_value(int instance, char *attr_value)
{
	struct ordered_list_data *ordered_list_data = &bioscfg_drv.ordered_list_data[instance];

	strscpy(ordered_list_data->current_value,
		attr_value,
		sizeof(ordered_list_data->current_value));
}

ATTRIBUTE_S_COMMON_PROPERTY_SHOW(display_name, ordered_list);
static struct kobj_attribute ordered_list_display_name =
	__ATTR_RO(display_name);

ATTRIBUTE_PROPERTY_STORE(current_value, ordered_list);
static struct kobj_attribute ordered_list_current_val =
	__ATTR_RW_MODE(current_value, 0644);

ATTRIBUTE_VALUES_PROPERTY_SHOW(elements, ordered_list, SEMICOLON_SEP);
static struct kobj_attribute ordered_list_elements_val =
	__ATTR_RO(elements);

static ssize_t type_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	return sysfs_emit(buf, "ordered-list\n");
}

static struct kobj_attribute ordered_list_type =
	__ATTR_RO(type);

static struct attribute *ordered_list_attrs[] = {
	&common_display_langcode.attr,
	&ordered_list_display_name.attr,
	&ordered_list_current_val.attr,
	&ordered_list_elements_val.attr,
	&ordered_list_type.attr,
	NULL
};

static const struct attribute_group ordered_list_attr_group = {
	.attrs = ordered_list_attrs,
};

int hp_alloc_ordered_list_data(void)
{
	bioscfg_drv.ordered_list_instances_count =
		hp_get_instance_count(HP_WMI_BIOS_ORDERED_LIST_GUID);
	bioscfg_drv.ordered_list_data = kcalloc(bioscfg_drv.ordered_list_instances_count,
						sizeof(*bioscfg_drv.ordered_list_data),
						GFP_KERNEL);
	if (!bioscfg_drv.ordered_list_data) {
		bioscfg_drv.ordered_list_instances_count = 0;
		return -ENOMEM;
	}
	return 0;
}

/* Expected Values types associated with each element */
static const acpi_object_type expected_order_types[] = {
	[NAME]	= ACPI_TYPE_STRING,
	[VALUE] = ACPI_TYPE_STRING,
	[PATH] = ACPI_TYPE_STRING,
	[IS_READONLY] = ACPI_TYPE_INTEGER,
	[DISPLAY_IN_UI] = ACPI_TYPE_INTEGER,
	[REQUIRES_PHYSICAL_PRESENCE] = ACPI_TYPE_INTEGER,
	[SEQUENCE] = ACPI_TYPE_INTEGER,
	[PREREQUISITES_SIZE] = ACPI_TYPE_INTEGER,
	[PREREQUISITES] = ACPI_TYPE_STRING,
	[SECURITY_LEVEL] = ACPI_TYPE_INTEGER,
	[ORD_LIST_SIZE] = ACPI_TYPE_INTEGER,
	[ORD_LIST_ELEMENTS] = ACPI_TYPE_STRING,
};

static int hp_populate_ordered_list_elements_from_package(union acpi_object *order_obj,
							  int order_obj_count,
							  int instance_id)
{
	char *str_value = NULL;
	int value_len = 0;
	int ret;
	u32 size;
	u32 int_value = 0;
	int elem;
	int olist_elem;
	int reqs;
	int eloc;
	char *tmpstr = NULL;
	char *part_tmp = NULL;
	int tmp_len = 0;
	char *part = NULL;
	struct ordered_list_data *ordered_list_data = &bioscfg_drv.ordered_list_data[instance_id];

	if (!order_obj)
		return -EINVAL;

	for (elem = 1, eloc = 1; eloc < ORD_ELEM_CNT; elem++, eloc++) {

		switch (order_obj[elem].type) {
		case ACPI_TYPE_STRING:
			if (elem != PREREQUISITES && elem != ORD_LIST_ELEMENTS) {
				ret = hp_convert_hexstr_to_str(order_obj[elem].string.pointer,
							       order_obj[elem].string.length,
							       &str_value, &value_len);
				if (ret)
					continue;
			}
			break;
		case ACPI_TYPE_INTEGER:
			int_value = (u32)order_obj[elem].integer.value;
			break;
		default:
			pr_warn("Unsupported object type [%d]\n", order_obj[elem].type);
			continue;
		}

		/* Check that both expected and read object type match */
		if (expected_order_types[eloc] != order_obj[elem].type) {
			pr_err("Error expected type %d for elem %d, but got type %d instead\n",
			       expected_order_types[eloc], elem, order_obj[elem].type);
			kfree(str_value);
			return -EIO;
		}

		/* Assign appropriate element value to corresponding field*/
		switch (eloc) {
		case VALUE:
			strscpy(ordered_list_data->current_value,
				str_value, sizeof(ordered_list_data->current_value));
			replace_char_str(ordered_list_data->current_value, COMMA_SEP, SEMICOLON_SEP);
			break;
		case PATH:
			strscpy(ordered_list_data->common.path, str_value,
				sizeof(ordered_list_data->common.path));
			break;
		case IS_READONLY:
			ordered_list_data->common.is_readonly = int_value;
			break;
		case DISPLAY_IN_UI:
			ordered_list_data->common.display_in_ui = int_value;
			break;
		case REQUIRES_PHYSICAL_PRESENCE:
			ordered_list_data->common.requires_physical_presence = int_value;
			break;
		case SEQUENCE:
			ordered_list_data->common.sequence = int_value;
			break;
		case PREREQUISITES_SIZE:
			if (int_value > MAX_PREREQUISITES_SIZE) {
				pr_warn("Prerequisites size value exceeded the maximum number of elements supported or data may be malformed\n");
				int_value = MAX_PREREQUISITES_SIZE;
			}
			ordered_list_data->common.prerequisites_size = int_value;

			/*
			 * This step is needed to keep the expected
			 * element list pointing to the right obj[elem].type
			 * when the size is zero. PREREQUISITES
			 * object is omitted by BIOS when the size is
			 * zero.
			 */
			if (int_value == 0)
				eloc++;
			break;
		case PREREQUISITES:
			size = min_t(u32, ordered_list_data->common.prerequisites_size,
				     MAX_PREREQUISITES_SIZE);
			for (reqs = 0; reqs < size; reqs++) {
				ret = hp_convert_hexstr_to_str(order_obj[elem + reqs].string.pointer,
							       order_obj[elem + reqs].string.length,
							       &str_value, &value_len);

				if (ret)
					continue;

				strscpy(ordered_list_data->common.prerequisites[reqs],
					str_value,
					sizeof(ordered_list_data->common.prerequisites[reqs]));

				kfree(str_value);
				str_value = NULL;
			}
			break;

		case SECURITY_LEVEL:
			ordered_list_data->common.security_level = int_value;
			break;

		case ORD_LIST_SIZE:
			if (int_value > MAX_ELEMENTS_SIZE) {
				pr_warn("Order List size value exceeded the maximum number of elements supported or data may be malformed\n");
				int_value = MAX_ELEMENTS_SIZE;
			}
			ordered_list_data->elements_size = int_value;

			/*
			 * This step is needed to keep the expected
			 * element list pointing to the right obj[elem].type
			 * when the size is zero. ORD_LIST_ELEMENTS
			 * object is omitted by BIOS when the size is
			 * zero.
			 */
			if (int_value == 0)
				eloc++;
			break;
		case ORD_LIST_ELEMENTS:

			/*
			 * Ordered list data is stored in hex and comma separated format
			 * Convert the data and split it to show each element
			 */
			ret = hp_convert_hexstr_to_str(str_value, value_len, &tmpstr, &tmp_len);
			if (ret)
				goto exit_list;

			part_tmp = tmpstr;
			part = strsep(&part_tmp, COMMA_SEP);

			for (olist_elem = 0; olist_elem < MAX_ELEMENTS_SIZE && part; olist_elem++) {
				strscpy(ordered_list_data->elements[olist_elem],
					part,
					sizeof(ordered_list_data->elements[olist_elem]));
				part = strsep(&part_tmp, COMMA_SEP);
			}
			ordered_list_data->elements_size = olist_elem;

			kfree(str_value);
			str_value = NULL;
			break;
		default:
			pr_warn("Invalid element: %d found in Ordered_List attribute or data may be malformed\n", elem);
			break;
		}
		kfree(tmpstr);
		tmpstr = NULL;
		kfree(str_value);
		str_value = NULL;
	}

exit_list:
	kfree(tmpstr);
	kfree(str_value);
	return 0;
}

/**
 * hp_populate_ordered_list_package_data() -
 * Populate all properties of an instance under ordered_list attribute
 *
 * @order_obj: ACPI object with ordered_list data
 * @instance_id: The instance to enumerate
 * @attr_name_kobj: The parent kernel object
 */
int hp_populate_ordered_list_package_data(union acpi_object *order_obj, int instance_id,
					  struct kobject *attr_name_kobj)
{
	struct ordered_list_data *ordered_list_data = &bioscfg_drv.ordered_list_data[instance_id];

	ordered_list_data->attr_name_kobj = attr_name_kobj;

	hp_populate_ordered_list_elements_from_package(order_obj,
						       order_obj->package.count,
						       instance_id);
	hp_update_attribute_permissions(ordered_list_data->common.is_readonly,
					&ordered_list_current_val);
	hp_friendly_user_name_update(ordered_list_data->common.path,
				     attr_name_kobj->name,
				     ordered_list_data->common.display_name,
				     sizeof(ordered_list_data->common.display_name));
	return sysfs_create_group(attr_name_kobj, &ordered_list_attr_group);
}

static int hp_populate_ordered_list_elements_from_buffer(u8 *buffer_ptr, u32 *buffer_size,
							 int instance_id)
{
	int values;
	struct ordered_list_data *ordered_list_data = &bioscfg_drv.ordered_list_data[instance_id];
	int ret = 0;

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
	ret = hp_get_string_from_buffer(&buffer_ptr, buffer_size, ordered_list_data->current_value,
					sizeof(ordered_list_data->current_value));
	if (ret < 0)
		goto buffer_exit;

	replace_char_str(ordered_list_data->current_value, COMMA_SEP, SEMICOLON_SEP);

	// COMMON:
	ret = hp_get_common_data_from_buffer(&buffer_ptr, buffer_size,
					     &ordered_list_data->common);
	if (ret < 0)
		goto buffer_exit;

	// ORD_LIST_SIZE:
	ret = hp_get_integer_from_buffer(&buffer_ptr, buffer_size,
					 &ordered_list_data->elements_size);

	if (ordered_list_data->elements_size > MAX_ELEMENTS_SIZE) {
		/* Report a message and limit elements size to maximum value */
		pr_warn("Ordered List size value exceeded the maximum number of elements supported or data may be malformed\n");
		ordered_list_data->elements_size = MAX_ELEMENTS_SIZE;
	}

	// ORD_LIST_ELEMENTS:
	for (values = 0; values < ordered_list_data->elements_size; values++) {
		ret = hp_get_string_from_buffer(&buffer_ptr, buffer_size,
						ordered_list_data->elements[values],
						sizeof(ordered_list_data->elements[values]));
		if (ret < 0)
			break;
	}

buffer_exit:
	return ret;
}

/**
 * hp_populate_ordered_list_buffer_data() - Populate all properties of an
 * instance under ordered list attribute
 *
 * @buffer_ptr: Buffer pointer
 * @buffer_size: Buffer size
 * @instance_id: The instance to enumerate
 * @attr_name_kobj: The parent kernel object
 */
int hp_populate_ordered_list_buffer_data(u8 *buffer_ptr, u32 *buffer_size, int instance_id,
					 struct kobject *attr_name_kobj)
{
	struct ordered_list_data *ordered_list_data = &bioscfg_drv.ordered_list_data[instance_id];
	int ret = 0;

	ordered_list_data->attr_name_kobj = attr_name_kobj;

	/* Populate ordered list elements */
	ret = hp_populate_ordered_list_elements_from_buffer(buffer_ptr, buffer_size,
							    instance_id);
	if (ret < 0)
		return ret;

	hp_update_attribute_permissions(ordered_list_data->common.is_readonly,
					&ordered_list_current_val);
	hp_friendly_user_name_update(ordered_list_data->common.path,
				     attr_name_kobj->name,
				     ordered_list_data->common.display_name,
				     sizeof(ordered_list_data->common.display_name));

	return sysfs_create_group(attr_name_kobj, &ordered_list_attr_group);
}

/**
 * hp_exit_ordered_list_attributes() - Clear all attribute data
 *
 * Clears all data allocated for this group of attributes
 */
void hp_exit_ordered_list_attributes(void)
{
	int instance_id;

	for (instance_id = 0; instance_id < bioscfg_drv.ordered_list_instances_count;
	     instance_id++) {
		struct kobject *attr_name_kobj =
			bioscfg_drv.ordered_list_data[instance_id].attr_name_kobj;

		if (attr_name_kobj)
			sysfs_remove_group(attr_name_kobj,
					   &ordered_list_attr_group);
	}
	bioscfg_drv.ordered_list_instances_count = 0;

	kfree(bioscfg_drv.ordered_list_data);
	bioscfg_drv.ordered_list_data = NULL;
}
