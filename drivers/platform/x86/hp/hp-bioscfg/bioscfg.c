// SPDX-License-Identifier: GPL-2.0
/*
 * Common methods for use with hp-bioscfg driver
 *
 *  Copyright (c) 2022 HP Development Company, L.P.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/wmi.h>
#include "bioscfg.h"
#include "../../firmware_attributes_class.h"
#include <linux/nls.h>
#include <linux/errno.h>

MODULE_AUTHOR("Jorge Lopez <jorge.lopez2@hp.com>");
MODULE_DESCRIPTION("HP BIOS Configuration Driver");
MODULE_LICENSE("GPL");

struct bioscfg_priv bioscfg_drv = {
	.mutex = __MUTEX_INITIALIZER(bioscfg_drv.mutex),
};

ssize_t display_name_language_code_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return sysfs_emit(buf, "%s\n", LANG_CODE_STR);
}

struct kobj_attribute common_display_langcode =
	__ATTR_RO(display_name_language_code);

int hp_get_integer_from_buffer(u8 **buffer, u32 *buffer_size, u32 *integer)
{
	int *ptr = PTR_ALIGN((int *)*buffer, sizeof(int));

	/* Ensure there is enough space remaining to read the integer */
	if (*buffer_size < sizeof(int))
		return -EINVAL;

	*integer = *(ptr++);
	*buffer = (u8 *)ptr;
	*buffer_size -= sizeof(int);

	return 0;
}

int hp_get_string_from_buffer(u8 **buffer, u32 *buffer_size, char *dst, u32 dst_size)
{
	u16 *src = (u16 *)*buffer;
	u16 src_size;

	u16 size;
	int i;
	int conv_dst_size;

	if (*buffer_size < sizeof(u16))
		return -EINVAL;

	src_size = *(src++);
	/* size value in u16 chars */
	size = src_size / sizeof(u16);

	/* Ensure there is enough space remaining to read and convert
	 * the string
	 */
	if (*buffer_size < src_size)
		return -EINVAL;

	for (i = 0; i < size; i++)
		if (src[i] == '\\' ||
		    src[i] == '\r' ||
		    src[i] == '\n' ||
		    src[i] == '\t')
			size++;

	/*
	 * Conversion is limited to destination string max number of
	 * bytes.
	 */
	conv_dst_size = size;
	if (size > dst_size)
		conv_dst_size = dst_size - 1;

	/*
	 * convert from UTF-16 unicode to ASCII
	 */
	utf16s_to_utf8s(src, src_size, UTF16_HOST_ENDIAN, dst, conv_dst_size);
	dst[conv_dst_size] = 0;

	for (i = 0; i < conv_dst_size; i++) {
		if (*src == '\\' ||
		    *src == '\r' ||
		    *src == '\n' ||
		    *src == '\t') {
			dst[i++] = '\\';
			if (i == conv_dst_size)
				break;
		}

		if (*src == '\r')
			dst[i] = 'r';
		else if (*src == '\n')
			dst[i] = 'n';
		else if (*src == '\t')
			dst[i] = 't';
		else if (*src == '"')
			dst[i] = '\'';
		else
			dst[i] = *src;
		src++;
	}

	*buffer = (u8 *)src;
	*buffer_size -= size * sizeof(u16);

	return size;
}

int hp_get_common_data_from_buffer(u8 **buffer_ptr, u32 *buffer_size,
				   struct common_data *common_data)
{
	int ret = 0;
	int reqs;

	// PATH:
	ret = hp_get_string_from_buffer(buffer_ptr, buffer_size, common_data->path,
					sizeof(common_data->path));
	if (ret < 0)
		goto common_exit;

	// IS_READONLY:
	ret = hp_get_integer_from_buffer(buffer_ptr, buffer_size,
					 &common_data->is_readonly);
	if (ret < 0)
		goto common_exit;

	//DISPLAY_IN_UI:
	ret = hp_get_integer_from_buffer(buffer_ptr, buffer_size,
					 &common_data->display_in_ui);
	if (ret < 0)
		goto common_exit;

	// REQUIRES_PHYSICAL_PRESENCE:
	ret = hp_get_integer_from_buffer(buffer_ptr, buffer_size,
					 &common_data->requires_physical_presence);
	if (ret < 0)
		goto common_exit;

	// SEQUENCE:
	ret = hp_get_integer_from_buffer(buffer_ptr, buffer_size,
					 &common_data->sequence);
	if (ret < 0)
		goto common_exit;

	// PREREQUISITES_SIZE:
	ret = hp_get_integer_from_buffer(buffer_ptr, buffer_size,
					 &common_data->prerequisites_size);
	if (ret < 0)
		goto common_exit;

	if (common_data->prerequisites_size > MAX_PREREQUISITES_SIZE) {
		/* Report a message and limit prerequisite size to maximum value */
		pr_warn("Prerequisites size value exceeded the maximum number of elements supported or data may be malformed\n");
		common_data->prerequisites_size = MAX_PREREQUISITES_SIZE;
	}

	// PREREQUISITES:
	for (reqs = 0; reqs < common_data->prerequisites_size; reqs++) {
		ret = hp_get_string_from_buffer(buffer_ptr, buffer_size,
						common_data->prerequisites[reqs],
						sizeof(common_data->prerequisites[reqs]));
		if (ret < 0)
			break;
	}

	// SECURITY_LEVEL:
	ret = hp_get_integer_from_buffer(buffer_ptr, buffer_size,
					 &common_data->security_level);

common_exit:
	return ret;
}

int hp_enforce_single_line_input(char *buf, size_t count)
{
	char *p;

	p = memchr(buf, '\n', count);

	if (p == buf + count - 1)
		*p = '\0'; /* strip trailing newline */
	else if (p)
		return -EINVAL;  /* enforce single line input */

	return 0;
}

/* Set pending reboot value and generate KOBJ_NAME event */
void hp_set_reboot_and_signal_event(void)
{
	bioscfg_drv.pending_reboot = true;
	kobject_uevent(&bioscfg_drv.class_dev->kobj, KOBJ_CHANGE);
}

/**
 * hp_calculate_string_buffer() - determines size of string buffer for
 * use with BIOS communication
 *
 * @str: the string to calculate based upon
 */
size_t hp_calculate_string_buffer(const char *str)
{
	size_t length = strlen(str);

	/* BIOS expects 4 bytes when an empty string is found */
	if (length == 0)
		return 4;

	/* u16 length field + one UTF16 char for each input char */
	return sizeof(u16) + strlen(str) * sizeof(u16);
}

int hp_wmi_error_and_message(int error_code)
{
	char *error_msg = NULL;
	int ret;

	switch (error_code) {
	case SUCCESS:
		error_msg = "Success";
		ret = 0;
		break;
	case CMD_FAILED:
		error_msg = "Command failed";
		ret = -EINVAL;
		break;
	case INVALID_SIGN:
		error_msg = "Invalid signature";
		ret = -EINVAL;
		break;
	case INVALID_CMD_VALUE:
		error_msg = "Invalid command value/Feature not supported";
		ret = -EOPNOTSUPP;
		break;
	case INVALID_CMD_TYPE:
		error_msg = "Invalid command type";
		ret = -EINVAL;
		break;
	case INVALID_DATA_SIZE:
		error_msg = "Invalid data size";
		ret = -EINVAL;
		break;
	case INVALID_CMD_PARAM:
		error_msg = "Invalid command parameter";
		ret = -EINVAL;
		break;
	case ENCRYP_CMD_REQUIRED:
		error_msg = "Secure/encrypted command required";
		ret = -EACCES;
		break;
	case NO_SECURE_SESSION:
		error_msg = "No secure session established";
		ret = -EACCES;
		break;
	case SECURE_SESSION_FOUND:
		error_msg = "Secure session already established";
		ret = -EACCES;
		break;
	case SECURE_SESSION_FAILED:
		error_msg = "Secure session failed";
		ret = -EIO;
		break;
	case AUTH_FAILED:
		error_msg = "Other permission/Authentication failed";
		ret = -EACCES;
		break;
	case INVALID_BIOS_AUTH:
		error_msg = "Invalid BIOS administrator password";
		ret = -EINVAL;
		break;
	case NONCE_DID_NOT_MATCH:
		error_msg = "Nonce did not match";
		ret = -EINVAL;
		break;
	case GENERIC_ERROR:
		error_msg = "Generic/Other error";
		ret = -EIO;
		break;
	case BIOS_ADMIN_POLICY_NOT_MET:
		error_msg = "BIOS Admin password does not meet password policy requirements";
		ret = -EINVAL;
		break;
	case BIOS_ADMIN_NOT_SET:
		error_msg = "BIOS Setup password is not set";
		ret = -EPERM;
		break;
	case P21_NO_PROVISIONED:
		error_msg = "P21 is not provisioned";
		ret = -EPERM;
		break;
	case P21_PROVISION_IN_PROGRESS:
		error_msg = "P21 is already provisioned or provisioning is in progress and a signing key has already been sent";
		ret = -EINPROGRESS;
		break;
	case P21_IN_USE:
		error_msg = "P21 in use (cannot deprovision)";
		ret = -EPERM;
		break;
	case HEP_NOT_ACTIVE:
		error_msg = "HEP not activated";
		ret = -EPERM;
		break;
	case HEP_ALREADY_SET:
		error_msg = "HEP Transport already set";
		ret = -EINVAL;
		break;
	case HEP_CHECK_STATE:
		error_msg = "Check the current HEP state";
		ret = -EINVAL;
		break;
	default:
		error_msg = "Generic/Other error";
		ret = -EIO;
		break;
	}

	if (error_code)
		pr_warn_ratelimited("Returned error 0x%x, \"%s\"\n", error_code, error_msg);

	return ret;
}

static ssize_t pending_reboot_show(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   char *buf)
{
	return sysfs_emit(buf, "%d\n", bioscfg_drv.pending_reboot);
}

static struct kobj_attribute pending_reboot = __ATTR_RO(pending_reboot);

/*
 * create_attributes_level_sysfs_files() - Creates pending_reboot attributes
 */
static int create_attributes_level_sysfs_files(void)
{
	return  sysfs_create_file(&bioscfg_drv.main_dir_kset->kobj,
				  &pending_reboot.attr);
}

static void attr_name_release(struct kobject *kobj)
{
	kfree(kobj);
}

static const struct kobj_type attr_name_ktype = {
	.release	= attr_name_release,
	.sysfs_ops	= &kobj_sysfs_ops,
};

/**
 * hp_get_wmiobj_pointer() - Get Content of WMI block for particular instance
 *
 * @instance_id: WMI instance ID
 * @guid_string: WMI GUID (in str form)
 *
 * Fetches the content for WMI block (instance_id) under GUID (guid_string)
 * Caller must kfree the return
 */
union acpi_object *hp_get_wmiobj_pointer(int instance_id, const char *guid_string)
{
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;

	status = wmi_query_block(guid_string, instance_id, &out);
	return ACPI_SUCCESS(status) ? (union acpi_object *)out.pointer : NULL;
}

/**
 * hp_get_instance_count() - Compute total number of instances under guid_string
 *
 * @guid_string: WMI GUID (in string form)
 */
int hp_get_instance_count(const char *guid_string)
{
	int ret;

	ret = wmi_instance_count(guid_string);
	if (ret < 0)
		return 0;

	return ret;
}

/**
 * hp_alloc_attributes_data() - Allocate attributes data for a particular type
 *
 * @attr_type: Attribute type to allocate
 */
static int hp_alloc_attributes_data(int attr_type)
{
	switch (attr_type) {
	case HPWMI_STRING_TYPE:
		return hp_alloc_string_data();

	case HPWMI_INTEGER_TYPE:
		return hp_alloc_integer_data();

	case HPWMI_ENUMERATION_TYPE:
		return hp_alloc_enumeration_data();

	case HPWMI_ORDERED_LIST_TYPE:
		return hp_alloc_ordered_list_data();

	case HPWMI_PASSWORD_TYPE:
		return hp_alloc_password_data();

	default:
		return 0;
	}
}

int hp_convert_hexstr_to_str(const char *input, u32 input_len, char **str, int *len)
{
	int ret = 0;
	int new_len = 0;
	char tmp[] = "0x00";
	char *new_str = NULL;
	long  ch;
	int i;

	if (input_len <= 0 || !input || !str || !len)
		return -EINVAL;

	*len = 0;
	*str = NULL;

	new_str = kmalloc(input_len, GFP_KERNEL);
	if (!new_str)
		return -ENOMEM;

	for (i = 0; i < input_len; i += 5) {
		strscpy(tmp, input + i);
		if (kstrtol(tmp, 16, &ch) == 0) {
			// escape char
			if (ch == '\\' ||
			    ch == '\r' ||
			    ch == '\n' || ch == '\t') {
				if (ch == '\r')
					ch = 'r';
				else if (ch == '\n')
					ch = 'n';
				else if (ch == '\t')
					ch = 't';
				new_str[new_len++] = '\\';
			}
			new_str[new_len++] = ch;
			if (ch == '\0')
				break;
		}
	}

	if (new_len) {
		new_str[new_len] = '\0';
		*str = krealloc(new_str, (new_len + 1) * sizeof(char),
				GFP_KERNEL);
		if (*str)
			*len = new_len;
		else
			ret = -ENOMEM;
	} else {
		ret = -EFAULT;
	}

	if (ret)
		kfree(new_str);
	return ret;
}

/* map output size to the corresponding WMI method id */
int hp_encode_outsize_for_pvsz(int outsize)
{
	if (outsize > 4096)
		return -EINVAL;
	if (outsize > 1024)
		return 5;
	if (outsize > 128)
		return 4;
	if (outsize > 4)
		return 3;
	if (outsize > 0)
		return 2;
	return 1;
}

/*
 * Update friendly display name for several attributes associated to
 * 'Schedule Power-On'
 */
void hp_friendly_user_name_update(char *path, const char *attr_name,
				  char *attr_display, int attr_size)
{
	if (strstr(path, SCHEDULE_POWER_ON))
		snprintf(attr_display, attr_size, "%s - %s", SCHEDULE_POWER_ON, attr_name);
	else
		strscpy(attr_display, attr_name, attr_size);
}

/**
 * hp_update_attribute_permissions() - Update attributes permissions when
 * isReadOnly value is 1
 *
 * @is_readonly:  bool value to indicate if it a readonly attribute.
 * @current_val: kobj_attribute corresponding to attribute.
 *
 */
void hp_update_attribute_permissions(bool is_readonly, struct kobj_attribute *current_val)
{
	current_val->attr.mode = is_readonly ? 0444 : 0644;
}

/**
 * destroy_attribute_objs() - Free a kset of kobjects
 * @kset: The kset to destroy
 *
 * Fress kobjects created for each attribute_name under attribute type kset
 */
static void destroy_attribute_objs(struct kset *kset)
{
	struct kobject *pos, *next;

	list_for_each_entry_safe(pos, next, &kset->list, entry)
		kobject_put(pos);
}

/**
 * release_attributes_data() - Clean-up all sysfs directories and files created
 */
static void release_attributes_data(void)
{
	mutex_lock(&bioscfg_drv.mutex);

	hp_exit_string_attributes();
	hp_exit_integer_attributes();
	hp_exit_enumeration_attributes();
	hp_exit_ordered_list_attributes();
	hp_exit_password_attributes();
	hp_exit_sure_start_attributes();
	hp_exit_secure_platform_attributes();

	if (bioscfg_drv.authentication_dir_kset) {
		destroy_attribute_objs(bioscfg_drv.authentication_dir_kset);
		kset_unregister(bioscfg_drv.authentication_dir_kset);
		bioscfg_drv.authentication_dir_kset = NULL;
	}
	if (bioscfg_drv.main_dir_kset) {
		sysfs_remove_file(&bioscfg_drv.main_dir_kset->kobj, &pending_reboot.attr);
		destroy_attribute_objs(bioscfg_drv.main_dir_kset);
		kset_unregister(bioscfg_drv.main_dir_kset);
		bioscfg_drv.main_dir_kset = NULL;
	}
	mutex_unlock(&bioscfg_drv.mutex);
}

/**
 * hp_add_other_attributes() - Initialize HP custom attributes not
 * reported by BIOS and required to support Secure Platform and Sure
 * Start.
 *
 * @attr_type: Custom HP attribute not reported by BIOS
 *
 * Initialize all 2 types of attributes: Platform and Sure Start
 * object.  Populates each attribute types respective properties
 * under sysfs files.
 *
 * Returns zero(0) if successful. Otherwise, a negative value.
 */
static int hp_add_other_attributes(int attr_type)
{
	struct kobject *attr_name_kobj;
	int ret;
	char *attr_name;

	attr_name_kobj = kzalloc(sizeof(*attr_name_kobj), GFP_KERNEL);
	if (!attr_name_kobj)
		return -ENOMEM;

	mutex_lock(&bioscfg_drv.mutex);

	/* Check if attribute type is supported */
	switch (attr_type) {
	case HPWMI_SECURE_PLATFORM_TYPE:
		attr_name_kobj->kset = bioscfg_drv.authentication_dir_kset;
		attr_name = SPM_STR;
		break;

	case HPWMI_SURE_START_TYPE:
		attr_name_kobj->kset = bioscfg_drv.main_dir_kset;
		attr_name = SURE_START_STR;
		break;

	default:
		pr_err("Error: Unknown attr_type: %d\n", attr_type);
		ret = -EINVAL;
		kfree(attr_name_kobj);
		goto unlock_drv_mutex;
	}

	ret = kobject_init_and_add(attr_name_kobj, &attr_name_ktype,
				   NULL, "%s", attr_name);
	if (ret) {
		pr_err("Error encountered [%d]\n", ret);
		goto err_other_attr_init;
	}

	/* Populate attribute data */
	switch (attr_type) {
	case HPWMI_SECURE_PLATFORM_TYPE:
		ret = hp_populate_secure_platform_data(attr_name_kobj);
		break;

	case HPWMI_SURE_START_TYPE:
		ret = hp_populate_sure_start_data(attr_name_kobj);
		break;

	default:
		ret = -EINVAL;
	}

	if (ret)
		goto err_other_attr_init;

	mutex_unlock(&bioscfg_drv.mutex);
	return 0;

err_other_attr_init:
	kobject_put(attr_name_kobj);
unlock_drv_mutex:
	mutex_unlock(&bioscfg_drv.mutex);
	return ret;
}

static int hp_init_bios_package_attribute(enum hp_wmi_data_type attr_type,
					  union acpi_object *obj,
					  const char *guid, int min_elements,
					  int instance_id)
{
	struct kobject *attr_name_kobj, *duplicate;
	union acpi_object *elements;
	struct kset *temp_kset;

	char *str_value = NULL;
	int str_len;
	int ret = 0;

	/* Take action appropriate to each ACPI TYPE */
	if (obj->package.count < min_elements) {
		pr_err("ACPI-package does not have enough elements: %d < %d\n",
		       obj->package.count, min_elements);
		goto pack_attr_exit;
	}

	elements = obj->package.elements;

	/* sanity checking */
	if (elements[NAME].type != ACPI_TYPE_STRING) {
		pr_debug("incorrect element type\n");
		goto pack_attr_exit;
	}
	if (strlen(elements[NAME].string.pointer) == 0) {
		pr_debug("empty attribute found\n");
		goto pack_attr_exit;
	}

	if (attr_type == HPWMI_PASSWORD_TYPE)
		temp_kset = bioscfg_drv.authentication_dir_kset;
	else
		temp_kset = bioscfg_drv.main_dir_kset;

	/* convert attribute name to string */
	ret = hp_convert_hexstr_to_str(elements[NAME].string.pointer,
				       elements[NAME].string.length,
				       &str_value, &str_len);

	if (ret) {
		pr_debug("Failed to populate integer package data. Error [0%0x]\n",
			 ret);
		kfree(str_value);
		return ret;
	}

	/* All duplicate attributes found are ignored */
	duplicate = kset_find_obj(temp_kset, str_value);
	if (duplicate) {
		pr_debug("Duplicate attribute name found - %s\n", str_value);
		/* kset_find_obj() returns a reference */
		kobject_put(duplicate);
		goto pack_attr_exit;
	}

	/* build attribute */
	attr_name_kobj = kzalloc(sizeof(*attr_name_kobj), GFP_KERNEL);
	if (!attr_name_kobj) {
		ret = -ENOMEM;
		goto pack_attr_exit;
	}

	attr_name_kobj->kset = temp_kset;

	ret = kobject_init_and_add(attr_name_kobj, &attr_name_ktype,
				   NULL, "%s", str_value);

	if (ret) {
		kobject_put(attr_name_kobj);
		goto pack_attr_exit;
	}

	/* enumerate all of these attributes */
	switch (attr_type) {
	case HPWMI_STRING_TYPE:
		ret = hp_populate_string_package_data(elements,
						      instance_id,
						      attr_name_kobj);
		break;
	case HPWMI_INTEGER_TYPE:
		ret = hp_populate_integer_package_data(elements,
						       instance_id,
						       attr_name_kobj);
		break;
	case HPWMI_ENUMERATION_TYPE:
		ret = hp_populate_enumeration_package_data(elements,
							   instance_id,
							   attr_name_kobj);
		break;
	case HPWMI_ORDERED_LIST_TYPE:
		ret = hp_populate_ordered_list_package_data(elements,
							    instance_id,
							    attr_name_kobj);
		break;
	case HPWMI_PASSWORD_TYPE:
		ret = hp_populate_password_package_data(elements,
							instance_id,
							attr_name_kobj);
		break;
	default:
		pr_debug("Unknown attribute type found: 0x%x\n", attr_type);
		break;
	}

pack_attr_exit:
	kfree(str_value);
	return ret;
}

static int hp_init_bios_buffer_attribute(enum hp_wmi_data_type attr_type,
					 union acpi_object *obj,
					 const char *guid, int min_elements,
					 int instance_id)
{
	struct kobject *attr_name_kobj, *duplicate;
	struct kset *temp_kset;
	char str[MAX_BUFF_SIZE];

	char *temp_str = NULL;
	char *str_value = NULL;
	u8 *buffer_ptr = NULL;
	int buffer_size;
	int ret = 0;

	buffer_size = obj->buffer.length;
	buffer_ptr = obj->buffer.pointer;

	ret = hp_get_string_from_buffer(&buffer_ptr,
					&buffer_size, str, MAX_BUFF_SIZE);

	if (ret < 0)
		goto buff_attr_exit;

	if (attr_type == HPWMI_PASSWORD_TYPE ||
	    attr_type == HPWMI_SECURE_PLATFORM_TYPE)
		temp_kset = bioscfg_drv.authentication_dir_kset;
	else
		temp_kset = bioscfg_drv.main_dir_kset;

	/* All duplicate attributes found are ignored */
	duplicate = kset_find_obj(temp_kset, str);
	if (duplicate) {
		pr_debug("Duplicate attribute name found - %s\n", str);
		/* kset_find_obj() returns a reference */
		kobject_put(duplicate);
		goto buff_attr_exit;
	}

	/* build attribute */
	attr_name_kobj = kzalloc(sizeof(*attr_name_kobj), GFP_KERNEL);
	if (!attr_name_kobj) {
		ret = -ENOMEM;
		goto buff_attr_exit;
	}

	attr_name_kobj->kset = temp_kset;

	temp_str = str;
	if (attr_type == HPWMI_SECURE_PLATFORM_TYPE)
		temp_str = "SPM";

	ret = kobject_init_and_add(attr_name_kobj,
				   &attr_name_ktype, NULL, "%s", temp_str);
	if (ret) {
		kobject_put(attr_name_kobj);
		goto buff_attr_exit;
	}

	/* enumerate all of these attributes */
	switch (attr_type) {
	case HPWMI_STRING_TYPE:
		ret = hp_populate_string_buffer_data(buffer_ptr,
						     &buffer_size,
						     instance_id,
						     attr_name_kobj);
		break;
	case HPWMI_INTEGER_TYPE:
		ret = hp_populate_integer_buffer_data(buffer_ptr,
						      &buffer_size,
						      instance_id,
						      attr_name_kobj);
		break;
	case HPWMI_ENUMERATION_TYPE:
		ret = hp_populate_enumeration_buffer_data(buffer_ptr,
							  &buffer_size,
							  instance_id,
							  attr_name_kobj);
		break;
	case HPWMI_ORDERED_LIST_TYPE:
		ret = hp_populate_ordered_list_buffer_data(buffer_ptr,
							   &buffer_size,
							   instance_id,
							   attr_name_kobj);
		break;
	case HPWMI_PASSWORD_TYPE:
		ret = hp_populate_password_buffer_data(buffer_ptr,
						       &buffer_size,
						       instance_id,
						       attr_name_kobj);
		break;
	default:
		pr_debug("Unknown attribute type found: 0x%x\n", attr_type);
		break;
	}

buff_attr_exit:
	kfree(str_value);
	return ret;
}

/**
 * hp_init_bios_attributes() - Initialize all attributes for a type
 * @attr_type: The attribute type to initialize
 * @guid: The WMI GUID associated with this type to initialize
 *
 * Initialize all 5 types of attributes: enumeration, integer,
 * string, password, ordered list  object.  Populates each attribute types
 * respective properties under sysfs files
 */
static int hp_init_bios_attributes(enum hp_wmi_data_type attr_type, const char *guid)
{
	union acpi_object *obj = NULL;
	int min_elements;

	/* instance_id needs to be reset for each type GUID
	 * also, instance IDs are unique within GUID but not across
	 */
	int instance_id = 0;
	int cur_instance_id = instance_id;
	int ret = 0;

	ret = hp_alloc_attributes_data(attr_type);
	if (ret)
		return ret;

	switch (attr_type) {
	case HPWMI_STRING_TYPE:
		min_elements = STR_ELEM_CNT;
		break;
	case HPWMI_INTEGER_TYPE:
		min_elements = INT_ELEM_CNT;
		break;
	case HPWMI_ENUMERATION_TYPE:
		min_elements = ENUM_ELEM_CNT;
		break;
	case HPWMI_ORDERED_LIST_TYPE:
		min_elements = ORD_ELEM_CNT;
		break;
	case HPWMI_PASSWORD_TYPE:
		min_elements = PSWD_ELEM_CNT;
		break;
	default:
		pr_err("Error: Unknown attr_type: %d\n", attr_type);
		return -EINVAL;
	}

	/* need to use specific instance_id and guid combination to get right data */
	obj = hp_get_wmiobj_pointer(instance_id, guid);
	if (!obj)
		return -ENODEV;

	mutex_lock(&bioscfg_drv.mutex);
	while (obj) {
		/* Take action appropriate to each ACPI TYPE */
		if (obj->type == ACPI_TYPE_PACKAGE) {
			ret = hp_init_bios_package_attribute(attr_type, obj,
							     guid, min_elements,
							     cur_instance_id);

		} else if (obj->type == ACPI_TYPE_BUFFER) {
			ret = hp_init_bios_buffer_attribute(attr_type, obj,
							    guid, min_elements,
							    cur_instance_id);

		} else {
			pr_err("Expected ACPI-package or buffer type, got: %d\n",
			       obj->type);
			ret = -EIO;
			goto err_attr_init;
		}

		/*
		 * Failure reported in one attribute must not
		 * stop process of the remaining attribute values.
		 */
		if (ret >= 0)
			cur_instance_id++;

		kfree(obj);
		instance_id++;
		obj = hp_get_wmiobj_pointer(instance_id, guid);
	}

err_attr_init:
	mutex_unlock(&bioscfg_drv.mutex);
	kfree(obj);
	return ret;
}

static int __init hp_init(void)
{
	int ret;
	int hp_bios_capable = wmi_has_guid(HP_WMI_BIOS_GUID);
	int set_bios_settings = wmi_has_guid(HP_WMI_SET_BIOS_SETTING_GUID);

	if (!hp_bios_capable) {
		pr_err("Unable to run on non-HP system\n");
		return -ENODEV;
	}

	if (!set_bios_settings) {
		pr_err("Unable to set BIOS settings on HP systems\n");
		return -ENODEV;
	}

	ret = hp_init_attr_set_interface();
	if (ret)
		return ret;

	bioscfg_drv.class_dev = device_create(&firmware_attributes_class, NULL, MKDEV(0, 0),
					      NULL, "%s", DRIVER_NAME);
	if (IS_ERR(bioscfg_drv.class_dev)) {
		ret = PTR_ERR(bioscfg_drv.class_dev);
		goto err_unregister_class;
	}

	bioscfg_drv.main_dir_kset = kset_create_and_add("attributes", NULL,
							&bioscfg_drv.class_dev->kobj);
	if (!bioscfg_drv.main_dir_kset) {
		ret = -ENOMEM;
		pr_debug("Failed to create and add attributes\n");
		goto err_destroy_classdev;
	}

	bioscfg_drv.authentication_dir_kset = kset_create_and_add("authentication", NULL,
								  &bioscfg_drv.class_dev->kobj);
	if (!bioscfg_drv.authentication_dir_kset) {
		ret = -ENOMEM;
		pr_debug("Failed to create and add authentication\n");
		goto err_release_attributes_data;
	}

	/*
	 * sysfs level attributes.
	 * - pending_reboot
	 */
	ret = create_attributes_level_sysfs_files();
	if (ret)
		pr_debug("Failed to create sysfs level attributes\n");

	ret = hp_init_bios_attributes(HPWMI_STRING_TYPE, HP_WMI_BIOS_STRING_GUID);
	if (ret)
		pr_debug("Failed to populate string type attributes\n");

	ret = hp_init_bios_attributes(HPWMI_INTEGER_TYPE, HP_WMI_BIOS_INTEGER_GUID);
	if (ret)
		pr_debug("Failed to populate integer type attributes\n");

	ret = hp_init_bios_attributes(HPWMI_ENUMERATION_TYPE, HP_WMI_BIOS_ENUMERATION_GUID);
	if (ret)
		pr_debug("Failed to populate enumeration type attributes\n");

	ret = hp_init_bios_attributes(HPWMI_ORDERED_LIST_TYPE, HP_WMI_BIOS_ORDERED_LIST_GUID);
	if (ret)
		pr_debug("Failed to populate ordered list object type attributes\n");

	ret = hp_init_bios_attributes(HPWMI_PASSWORD_TYPE, HP_WMI_BIOS_PASSWORD_GUID);
	if (ret)
		pr_debug("Failed to populate password object type attributes\n");

	bioscfg_drv.spm_data.attr_name_kobj = NULL;
	ret = hp_add_other_attributes(HPWMI_SECURE_PLATFORM_TYPE);
	if (ret)
		pr_debug("Failed to populate secure platform object type attribute\n");

	bioscfg_drv.sure_start_attr_kobj = NULL;
	ret = hp_add_other_attributes(HPWMI_SURE_START_TYPE);
	if (ret)
		pr_debug("Failed to populate sure start object type attribute\n");

	return 0;

err_release_attributes_data:
	release_attributes_data();

err_destroy_classdev:
	device_unregister(bioscfg_drv.class_dev);

err_unregister_class:
	hp_exit_attr_set_interface();

	return ret;
}

static void __exit hp_exit(void)
{
	release_attributes_data();
	device_unregister(bioscfg_drv.class_dev);

	hp_exit_attr_set_interface();
}

module_init(hp_init);
module_exit(hp_exit);
