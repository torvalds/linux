// SPDX-License-Identifier: GPL-2.0
/*
 * Common methods for use with dell-wmi-sysman
 *
 *  Copyright (c) 2020 Dell Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/wmi.h>
#include "dell-wmi-sysman.h"
#include "../../firmware_attributes_class.h"

#define MAX_TYPES  4
#include <linux/nls.h>

struct wmi_sysman_priv wmi_priv = {
	.mutex = __MUTEX_INITIALIZER(wmi_priv.mutex),
};

/* reset bios to defaults */
static const char * const reset_types[] = {"builtinsafe", "lastknowngood", "factory", "custom"};
static int reset_option = -1;
static struct class *fw_attr_class;


/**
 * populate_string_buffer() - populates a string buffer
 * @buffer: the start of the destination buffer
 * @buffer_len: length of the destination buffer
 * @str: the string to insert into buffer
 */
ssize_t populate_string_buffer(char *buffer, size_t buffer_len, const char *str)
{
	u16 *length = (u16 *)buffer;
	u16 *target = length + 1;
	int ret;

	ret = utf8s_to_utf16s(str, strlen(str), UTF16_HOST_ENDIAN,
			      target, buffer_len - sizeof(u16));
	if (ret < 0) {
		dev_err(wmi_priv.class_dev, "UTF16 conversion failed\n");
		return ret;
	}

	if ((ret * sizeof(u16)) > U16_MAX) {
		dev_err(wmi_priv.class_dev, "Error string too long\n");
		return -ERANGE;
	}

	*length = ret * sizeof(u16);
	return sizeof(u16) + *length;
}

/**
 * calculate_string_buffer() - determines size of string buffer for use with BIOS communication
 * @str: the string to calculate based upon
 *
 */
size_t calculate_string_buffer(const char *str)
{
	/* u16 length field + one UTF16 char for each input char */
	return sizeof(u16) + strlen(str) * sizeof(u16);
}

/**
 * calculate_security_buffer() - determines size of security buffer for authentication scheme
 * @authentication: the authentication content
 *
 * Currently only supported type is Admin password
 */
size_t calculate_security_buffer(char *authentication)
{
	if (strlen(authentication) > 0) {
		return (sizeof(u32) * 2) + strlen(authentication) +
			strlen(authentication) % 2;
	}
	return sizeof(u32) * 2;
}

/**
 * populate_security_buffer() - builds a security buffer for authentication scheme
 * @buffer: the buffer to populate
 * @authentication: the authentication content
 *
 * Currently only supported type is PLAIN TEXT
 */
void populate_security_buffer(char *buffer, char *authentication)
{
	char *auth = buffer + sizeof(u32) * 2;
	u32 *sectype = (u32 *) buffer;
	u32 *seclen = sectype + 1;

	*sectype = strlen(authentication) > 0 ? 1 : 0;
	*seclen = strlen(authentication);

	/* plain text */
	if (strlen(authentication) > 0)
		memcpy(auth, authentication, *seclen);
}

/**
 * map_wmi_error() - map errors from WMI methods to kernel error codes
 * @error_code: integer error code returned from Dell's firmware
 */
int map_wmi_error(int error_code)
{
	switch (error_code) {
	case 0:
		/* success */
		return 0;
	case 1:
		/* failed */
		return -EIO;
	case 2:
		/* invalid parameter */
		return -EINVAL;
	case 3:
		/* access denied */
		return -EACCES;
	case 4:
		/* not supported */
		return -EOPNOTSUPP;
	case 5:
		/* memory error */
		return -ENOMEM;
	case 6:
		/* protocol error */
		return -EPROTO;
	}
	/* unspecified error */
	return -EIO;
}

/**
 * reset_bios_show() - sysfs implementaton for read reset_bios
 * @kobj: Kernel object for this attribute
 * @attr: Kernel object attribute
 * @buf: The buffer to display to userspace
 */
static ssize_t reset_bios_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	char *start = buf;
	int i;

	for (i = 0; i < MAX_TYPES; i++) {
		if (i == reset_option)
			buf += sprintf(buf, "[%s] ", reset_types[i]);
		else
			buf += sprintf(buf, "%s ", reset_types[i]);
	}
	buf += sprintf(buf, "\n");
	return buf-start;
}

/**
 * reset_bios_store() - sysfs implementaton for write reset_bios
 * @kobj: Kernel object for this attribute
 * @attr: Kernel object attribute
 * @buf: The buffer from userspace
 * @count: the size of the buffer from userspace
 */
static ssize_t reset_bios_store(struct kobject *kobj,
				struct kobj_attribute *attr, const char *buf, size_t count)
{
	int type = sysfs_match_string(reset_types, buf);
	int ret;

	if (type < 0)
		return type;

	ret = set_bios_defaults(type);
	pr_debug("reset all attributes request type %d: %d\n", type, ret);
	if (!ret) {
		reset_option = type;
		ret = count;
	}

	return ret;
}

/**
 * pending_reboot_show() - sysfs implementaton for read pending_reboot
 * @kobj: Kernel object for this attribute
 * @attr: Kernel object attribute
 * @buf: The buffer to display to userspace
 *
 * Stores default value as 0
 * When current_value is changed this attribute is set to 1 to notify reboot may be required
 */
static ssize_t pending_reboot_show(struct kobject *kobj, struct kobj_attribute *attr,
				   char *buf)
{
	return sprintf(buf, "%d\n", wmi_priv.pending_changes);
}

static struct kobj_attribute reset_bios = __ATTR_RW(reset_bios);
static struct kobj_attribute pending_reboot = __ATTR_RO(pending_reboot);


/**
 * create_attributes_level_sysfs_files() - Creates reset_bios and
 * pending_reboot attributes
 */
static int create_attributes_level_sysfs_files(void)
{
	int ret;

	ret = sysfs_create_file(&wmi_priv.main_dir_kset->kobj, &reset_bios.attr);
	if (ret)
		return ret;

	ret = sysfs_create_file(&wmi_priv.main_dir_kset->kobj, &pending_reboot.attr);
	if (ret)
		return ret;

	return 0;
}

static ssize_t wmi_sysman_attr_show(struct kobject *kobj, struct attribute *attr,
				    char *buf)
{
	struct kobj_attribute *kattr;
	ssize_t ret = -EIO;

	kattr = container_of(attr, struct kobj_attribute, attr);
	if (kattr->show)
		ret = kattr->show(kobj, kattr, buf);
	return ret;
}

static ssize_t wmi_sysman_attr_store(struct kobject *kobj, struct attribute *attr,
				     const char *buf, size_t count)
{
	struct kobj_attribute *kattr;
	ssize_t ret = -EIO;

	kattr = container_of(attr, struct kobj_attribute, attr);
	if (kattr->store)
		ret = kattr->store(kobj, kattr, buf, count);
	return ret;
}

static const struct sysfs_ops wmi_sysman_kobj_sysfs_ops = {
	.show	= wmi_sysman_attr_show,
	.store	= wmi_sysman_attr_store,
};

static void attr_name_release(struct kobject *kobj)
{
	kfree(kobj);
}

static const struct kobj_type attr_name_ktype = {
	.release	= attr_name_release,
	.sysfs_ops	= &wmi_sysman_kobj_sysfs_ops,
};

/**
 * strlcpy_attr - Copy a length-limited, NULL-terminated string with bound checks
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 */
void strlcpy_attr(char *dest, char *src)
{
	size_t len = strlen(src) + 1;

	if (len > 1 && len <= MAX_BUFF)
		strscpy(dest, src, len);

	/*len can be zero because any property not-applicable to attribute can
	 * be empty so check only for too long buffers and log error
	 */
	if (len > MAX_BUFF)
		pr_err("Source string returned from BIOS is out of bound!\n");
}

/**
 * get_wmiobj_pointer() - Get Content of WMI block for particular instance
 * @instance_id: WMI instance ID
 * @guid_string: WMI GUID (in str form)
 *
 * Fetches the content for WMI block (instance_id) under GUID (guid_string)
 * Caller must kfree the return
 */
union acpi_object *get_wmiobj_pointer(int instance_id, const char *guid_string)
{
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;

	status = wmi_query_block(guid_string, instance_id, &out);

	return ACPI_SUCCESS(status) ? (union acpi_object *)out.pointer : NULL;
}

/**
 * get_instance_count() - Compute total number of instances under guid_string
 * @guid_string: WMI GUID (in string form)
 */
int get_instance_count(const char *guid_string)
{
	int ret;

	ret = wmi_instance_count(guid_string);
	if (ret < 0)
		return 0;

	return ret;
}

/**
 * alloc_attributes_data() - Allocate attributes data for a particular type
 * @attr_type: Attribute type to allocate
 */
static int alloc_attributes_data(int attr_type)
{
	int retval = 0;

	switch (attr_type) {
	case ENUM:
		retval = alloc_enum_data();
		break;
	case INT:
		retval = alloc_int_data();
		break;
	case STR:
		retval = alloc_str_data();
		break;
	case PO:
		retval = alloc_po_data();
		break;
	default:
		break;
	}

	return retval;
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

	list_for_each_entry_safe(pos, next, &kset->list, entry) {
		kobject_put(pos);
	}
}

/**
 * release_attributes_data() - Clean-up all sysfs directories and files created
 */
static void release_attributes_data(void)
{
	mutex_lock(&wmi_priv.mutex);
	exit_enum_attributes();
	exit_int_attributes();
	exit_str_attributes();
	exit_po_attributes();
	if (wmi_priv.authentication_dir_kset) {
		destroy_attribute_objs(wmi_priv.authentication_dir_kset);
		kset_unregister(wmi_priv.authentication_dir_kset);
		wmi_priv.authentication_dir_kset = NULL;
	}
	if (wmi_priv.main_dir_kset) {
		sysfs_remove_file(&wmi_priv.main_dir_kset->kobj, &reset_bios.attr);
		sysfs_remove_file(&wmi_priv.main_dir_kset->kobj, &pending_reboot.attr);
		destroy_attribute_objs(wmi_priv.main_dir_kset);
		kset_unregister(wmi_priv.main_dir_kset);
		wmi_priv.main_dir_kset = NULL;
	}
	mutex_unlock(&wmi_priv.mutex);
}

/**
 * init_bios_attributes() - Initialize all attributes for a type
 * @attr_type: The attribute type to initialize
 * @guid: The WMI GUID associated with this type to initialize
 *
 * Initialiaze all 4 types of attributes enumeration, integer, string and password object.
 * Populates each attrbute typ's respective properties under sysfs files
 */
static int init_bios_attributes(int attr_type, const char *guid)
{
	struct kobject *attr_name_kobj; //individual attribute names
	union acpi_object *obj = NULL;
	union acpi_object *elements;
	struct kobject *duplicate;
	struct kset *tmp_set;
	int min_elements;

	/* instance_id needs to be reset for each type GUID
	 * also, instance IDs are unique within GUID but not across
	 */
	int instance_id = 0;
	int retval = 0;

	retval = alloc_attributes_data(attr_type);
	if (retval)
		return retval;

	switch (attr_type) {
	case ENUM:	min_elements = 8;	break;
	case INT:	min_elements = 9;	break;
	case STR:	min_elements = 8;	break;
	case PO:	min_elements = 4;	break;
	default:
		pr_err("Error: Unknown attr_type: %d\n", attr_type);
		return -EINVAL;
	}

	/* need to use specific instance_id and guid combination to get right data */
	obj = get_wmiobj_pointer(instance_id, guid);
	if (!obj)
		return -ENODEV;

	mutex_lock(&wmi_priv.mutex);
	while (obj) {
		if (obj->type != ACPI_TYPE_PACKAGE) {
			pr_err("Error: Expected ACPI-package type, got: %d\n", obj->type);
			retval = -EIO;
			goto err_attr_init;
		}

		if (obj->package.count < min_elements) {
			pr_err("Error: ACPI-package does not have enough elements: %d < %d\n",
			       obj->package.count, min_elements);
			goto nextobj;
		}

		elements = obj->package.elements;

		/* sanity checking */
		if (elements[ATTR_NAME].type != ACPI_TYPE_STRING) {
			pr_debug("incorrect element type\n");
			goto nextobj;
		}
		if (strlen(elements[ATTR_NAME].string.pointer) == 0) {
			pr_debug("empty attribute found\n");
			goto nextobj;
		}
		if (attr_type == PO)
			tmp_set = wmi_priv.authentication_dir_kset;
		else
			tmp_set = wmi_priv.main_dir_kset;

		duplicate = kset_find_obj(tmp_set, elements[ATTR_NAME].string.pointer);
		if (duplicate) {
			pr_debug("Duplicate attribute name found - %s\n",
				 elements[ATTR_NAME].string.pointer);
			kobject_put(duplicate);
			goto nextobj;
		}

		/* build attribute */
		attr_name_kobj = kzalloc(sizeof(*attr_name_kobj), GFP_KERNEL);
		if (!attr_name_kobj) {
			retval = -ENOMEM;
			goto err_attr_init;
		}

		attr_name_kobj->kset = tmp_set;

		retval = kobject_init_and_add(attr_name_kobj, &attr_name_ktype, NULL, "%s",
						elements[ATTR_NAME].string.pointer);
		if (retval) {
			kobject_put(attr_name_kobj);
			goto err_attr_init;
		}

		/* enumerate all of this attribute */
		switch (attr_type) {
		case ENUM:
			retval = populate_enum_data(elements, instance_id, attr_name_kobj,
					obj->package.count);
			break;
		case INT:
			retval = populate_int_data(elements, instance_id, attr_name_kobj);
			break;
		case STR:
			retval = populate_str_data(elements, instance_id, attr_name_kobj);
			break;
		case PO:
			retval = populate_po_data(elements, instance_id, attr_name_kobj);
			break;
		default:
			break;
		}

		if (retval) {
			pr_debug("failed to populate %s\n",
				elements[ATTR_NAME].string.pointer);
			goto err_attr_init;
		}

nextobj:
		kfree(obj);
		instance_id++;
		obj = get_wmiobj_pointer(instance_id, guid);
	}

	mutex_unlock(&wmi_priv.mutex);
	return 0;

err_attr_init:
	mutex_unlock(&wmi_priv.mutex);
	kfree(obj);
	return retval;
}

static int __init sysman_init(void)
{
	int ret = 0;

	if (!dmi_find_device(DMI_DEV_TYPE_OEM_STRING, "Dell System", NULL) &&
	    !dmi_find_device(DMI_DEV_TYPE_OEM_STRING, "www.dell.com", NULL)) {
		pr_err("Unable to run on non-Dell system\n");
		return -ENODEV;
	}

	ret = init_bios_attr_set_interface();
	if (ret)
		return ret;

	ret = init_bios_attr_pass_interface();
	if (ret)
		goto err_exit_bios_attr_set_interface;

	if (!wmi_priv.bios_attr_wdev || !wmi_priv.password_attr_wdev) {
		pr_debug("failed to find set or pass interface\n");
		ret = -ENODEV;
		goto err_exit_bios_attr_pass_interface;
	}

	ret = fw_attributes_class_get(&fw_attr_class);
	if (ret)
		goto err_exit_bios_attr_pass_interface;

	wmi_priv.class_dev = device_create(fw_attr_class, NULL, MKDEV(0, 0),
				  NULL, "%s", DRIVER_NAME);
	if (IS_ERR(wmi_priv.class_dev)) {
		ret = PTR_ERR(wmi_priv.class_dev);
		goto err_unregister_class;
	}

	wmi_priv.main_dir_kset = kset_create_and_add("attributes", NULL,
						     &wmi_priv.class_dev->kobj);
	if (!wmi_priv.main_dir_kset) {
		ret = -ENOMEM;
		goto err_destroy_classdev;
	}

	wmi_priv.authentication_dir_kset = kset_create_and_add("authentication", NULL,
								&wmi_priv.class_dev->kobj);
	if (!wmi_priv.authentication_dir_kset) {
		ret = -ENOMEM;
		goto err_release_attributes_data;
	}

	ret = create_attributes_level_sysfs_files();
	if (ret) {
		pr_debug("could not create reset BIOS attribute\n");
		goto err_release_attributes_data;
	}

	ret = init_bios_attributes(ENUM, DELL_WMI_BIOS_ENUMERATION_ATTRIBUTE_GUID);
	if (ret) {
		pr_debug("failed to populate enumeration type attributes\n");
		goto err_release_attributes_data;
	}

	ret = init_bios_attributes(INT, DELL_WMI_BIOS_INTEGER_ATTRIBUTE_GUID);
	if (ret) {
		pr_debug("failed to populate integer type attributes\n");
		goto err_release_attributes_data;
	}

	ret = init_bios_attributes(STR, DELL_WMI_BIOS_STRING_ATTRIBUTE_GUID);
	if (ret) {
		pr_debug("failed to populate string type attributes\n");
		goto err_release_attributes_data;
	}

	ret = init_bios_attributes(PO, DELL_WMI_BIOS_PASSOBJ_ATTRIBUTE_GUID);
	if (ret) {
		pr_debug("failed to populate pass object type attributes\n");
		goto err_release_attributes_data;
	}

	return 0;

err_release_attributes_data:
	release_attributes_data();

err_destroy_classdev:
	device_destroy(fw_attr_class, MKDEV(0, 0));

err_unregister_class:
	fw_attributes_class_put();

err_exit_bios_attr_pass_interface:
	exit_bios_attr_pass_interface();

err_exit_bios_attr_set_interface:
	exit_bios_attr_set_interface();

	return ret;
}

static void __exit sysman_exit(void)
{
	release_attributes_data();
	device_destroy(fw_attr_class, MKDEV(0, 0));
	fw_attributes_class_put();
	exit_bios_attr_set_interface();
	exit_bios_attr_pass_interface();
}

module_init(sysman_init);
module_exit(sysman_exit);

MODULE_AUTHOR("Mario Limonciello <mario.limonciello@outlook.com>");
MODULE_AUTHOR("Prasanth Ksr <prasanth.ksr@dell.com>");
MODULE_AUTHOR("Divya Bharathi <divya.bharathi@dell.com>");
MODULE_DESCRIPTION("Dell platform setting control interface");
MODULE_LICENSE("GPL");
