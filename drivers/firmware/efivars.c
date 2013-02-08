/*
 * EFI Variables - efivars.c
 *
 * Copyright (C) 2001,2003,2004 Dell <Matt_Domsch@dell.com>
 * Copyright (C) 2004 Intel Corporation <matthew.e.tolentino@intel.com>
 *
 * This code takes all variables accessible from EFI runtime and
 *  exports them via sysfs
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Changelog:
 *
 *  17 May 2004 - Matt Domsch <Matt_Domsch@dell.com>
 *   remove check for efi_enabled in exit
 *   add MODULE_VERSION
 *
 *  26 Apr 2004 - Matt Domsch <Matt_Domsch@dell.com>
 *   minor bug fixes
 *
 *  21 Apr 2004 - Matt Tolentino <matthew.e.tolentino@intel.com)
 *   converted driver to export variable information via sysfs
 *   and moved to drivers/firmware directory
 *   bumped revision number to v0.07 to reflect conversion & move
 *
 *  10 Dec 2002 - Matt Domsch <Matt_Domsch@dell.com>
 *   fix locking per Peter Chubb's findings
 *
 *  25 Mar 2002 - Matt Domsch <Matt_Domsch@dell.com>
 *   move uuid_unparse() to include/asm-ia64/efi.h:efi_guid_unparse()
 *
 *  12 Feb 2002 - Matt Domsch <Matt_Domsch@dell.com>
 *   use list_for_each_safe when deleting vars.
 *   remove ifdef CONFIG_SMP around include <linux/smp.h>
 *   v0.04 release to linux-ia64@linuxia64.org
 *
 *  20 April 2001 - Matt Domsch <Matt_Domsch@dell.com>
 *   Moved vars from /proc/efi to /proc/efi/vars, and made
 *   efi.c own the /proc/efi directory.
 *   v0.03 release to linux-ia64@linuxia64.org
 *
 *  26 March 2001 - Matt Domsch <Matt_Domsch@dell.com>
 *   At the request of Stephane, moved ownership of /proc/efi
 *   to efi.c, and now efivars lives under /proc/efi/vars.
 *
 *  12 March 2001 - Matt Domsch <Matt_Domsch@dell.com>
 *   Feedback received from Stephane Eranian incorporated.
 *   efivar_write() checks copy_from_user() return value.
 *   efivar_read/write() returns proper errno.
 *   v0.02 release to linux-ia64@linuxia64.org
 *
 *  26 February 2001 - Matt Domsch <Matt_Domsch@dell.com>
 *   v0.01 release to linux-ia64@linuxia64.org
 */

#include <linux/capability.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/smp.h>
#include <linux/efi.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/ctype.h>

#include <linux/fs.h>
#include <linux/ramfs.h>
#include <linux/pagemap.h>

#include <asm/uaccess.h>

#define EFIVARS_VERSION "0.08"
#define EFIVARS_DATE "2004-May-17"

MODULE_AUTHOR("Matt Domsch <Matt_Domsch@Dell.com>");
MODULE_DESCRIPTION("sysfs interface to EFI Variables");
MODULE_LICENSE("GPL");
MODULE_VERSION(EFIVARS_VERSION);

LIST_HEAD(efivar_sysfs_list);
EXPORT_SYMBOL_GPL(efivar_sysfs_list);

struct efivar_attribute {
	struct attribute attr;
	ssize_t (*show) (struct efivar_entry *entry, char *buf);
	ssize_t (*store)(struct efivar_entry *entry, const char *buf, size_t count);
};

/* Private pointer to registered efivars */
static struct efivars *__efivars;

static struct kset *efivars_kset;

static struct bin_attribute *efivars_new_var;
static struct bin_attribute *efivars_del_var;

#define EFIVAR_ATTR(_name, _mode, _show, _store) \
struct efivar_attribute efivar_attr_##_name = { \
	.attr = {.name = __stringify(_name), .mode = _mode}, \
	.show = _show, \
	.store = _store, \
};

#define to_efivar_attr(_attr) container_of(_attr, struct efivar_attribute, attr)
#define to_efivar_entry(obj)  container_of(obj, struct efivar_entry, kobj)

/*
 * Prototype for sysfs creation function
 */
static int
efivar_create_sysfs_entry(struct efivar_entry *new_var);

/*
 * Prototype for workqueue functions updating sysfs entry
 */

static void efivar_update_sysfs_entries(struct work_struct *);
static DECLARE_WORK(efivar_work, efivar_update_sysfs_entries);
static bool efivar_wq_enabled = true;

static bool
validate_device_path(struct efi_variable *var, int match, u8 *buffer,
		     unsigned long len)
{
	struct efi_generic_dev_path *node;
	int offset = 0;

	node = (struct efi_generic_dev_path *)buffer;

	if (len < sizeof(*node))
		return false;

	while (offset <= len - sizeof(*node) &&
	       node->length >= sizeof(*node) &&
		node->length <= len - offset) {
		offset += node->length;

		if ((node->type == EFI_DEV_END_PATH ||
		     node->type == EFI_DEV_END_PATH2) &&
		    node->sub_type == EFI_DEV_END_ENTIRE)
			return true;

		node = (struct efi_generic_dev_path *)(buffer + offset);
	}

	/*
	 * If we're here then either node->length pointed past the end
	 * of the buffer or we reached the end of the buffer without
	 * finding a device path end node.
	 */
	return false;
}

static bool
validate_boot_order(struct efi_variable *var, int match, u8 *buffer,
		    unsigned long len)
{
	/* An array of 16-bit integers */
	if ((len % 2) != 0)
		return false;

	return true;
}

static bool
validate_load_option(struct efi_variable *var, int match, u8 *buffer,
		     unsigned long len)
{
	u16 filepathlength;
	int i, desclength = 0, namelen;

	namelen = utf16_strnlen(var->VariableName, sizeof(var->VariableName));

	/* Either "Boot" or "Driver" followed by four digits of hex */
	for (i = match; i < match+4; i++) {
		if (var->VariableName[i] > 127 ||
		    hex_to_bin(var->VariableName[i] & 0xff) < 0)
			return true;
	}

	/* Reject it if there's 4 digits of hex and then further content */
	if (namelen > match + 4)
		return false;

	/* A valid entry must be at least 8 bytes */
	if (len < 8)
		return false;

	filepathlength = buffer[4] | buffer[5] << 8;

	/*
	 * There's no stored length for the description, so it has to be
	 * found by hand
	 */
	desclength = utf16_strsize((efi_char16_t *)(buffer + 6), len - 6) + 2;

	/* Each boot entry must have a descriptor */
	if (!desclength)
		return false;

	/*
	 * If the sum of the length of the description, the claimed filepath
	 * length and the original header are greater than the length of the
	 * variable, it's malformed
	 */
	if ((desclength + filepathlength + 6) > len)
		return false;

	/*
	 * And, finally, check the filepath
	 */
	return validate_device_path(var, match, buffer + desclength + 6,
				    filepathlength);
}

static bool
validate_uint16(struct efi_variable *var, int match, u8 *buffer,
		unsigned long len)
{
	/* A single 16-bit integer */
	if (len != 2)
		return false;

	return true;
}

static bool
validate_ascii_string(struct efi_variable *var, int match, u8 *buffer,
		      unsigned long len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (buffer[i] > 127)
			return false;

		if (buffer[i] == 0)
			return true;
	}

	return false;
}

struct variable_validate {
	char *name;
	bool (*validate)(struct efi_variable *var, int match, u8 *data,
			 unsigned long len);
};

static const struct variable_validate variable_validate[] = {
	{ "BootNext", validate_uint16 },
	{ "BootOrder", validate_boot_order },
	{ "DriverOrder", validate_boot_order },
	{ "Boot*", validate_load_option },
	{ "Driver*", validate_load_option },
	{ "ConIn", validate_device_path },
	{ "ConInDev", validate_device_path },
	{ "ConOut", validate_device_path },
	{ "ConOutDev", validate_device_path },
	{ "ErrOut", validate_device_path },
	{ "ErrOutDev", validate_device_path },
	{ "Timeout", validate_uint16 },
	{ "Lang", validate_ascii_string },
	{ "PlatformLang", validate_ascii_string },
	{ "", NULL },
};

bool
efivar_validate(struct efi_variable *var, u8 *data, unsigned long len)
{
	int i;
	u16 *unicode_name = var->VariableName;

	for (i = 0; variable_validate[i].validate != NULL; i++) {
		const char *name = variable_validate[i].name;
		int match;

		for (match = 0; ; match++) {
			char c = name[match];
			u16 u = unicode_name[match];

			/* All special variables are plain ascii */
			if (u > 127)
				return true;

			/* Wildcard in the matching name means we've matched */
			if (c == '*')
				return variable_validate[i].validate(var,
							     match, data, len);

			/* Case sensitive match */
			if (c != u)
				break;

			/* Reached the end of the string while matching */
			if (!c)
				return variable_validate[i].validate(var,
							     match, data, len);
		}
	}

	return true;
}
EXPORT_SYMBOL_GPL(efivar_validate);

static efi_status_t
check_var_size(u32 attributes, unsigned long size)
{
	u64 storage_size, remaining_size, max_size;
	efi_status_t status;
	const struct efivar_operations *fops = __efivars->ops;

	if (!fops->query_variable_info)
		return EFI_UNSUPPORTED;

	status = fops->query_variable_info(attributes, &storage_size,
					   &remaining_size, &max_size);

	if (status != EFI_SUCCESS)
		return status;

	if (!storage_size || size > remaining_size || size > max_size ||
	    (remaining_size - size) < (storage_size / 2))
		return EFI_OUT_OF_RESOURCES;

	return status;
}

static ssize_t
efivar_guid_read(struct efivar_entry *entry, char *buf)
{
	struct efi_variable *var = &entry->var;
	char *str = buf;

	if (!entry || !buf)
		return 0;

	efi_guid_unparse(&var->VendorGuid, str);
	str += strlen(str);
	str += sprintf(str, "\n");

	return str - buf;
}

static ssize_t
efivar_attr_read(struct efivar_entry *entry, char *buf)
{
	struct efi_variable *var = &entry->var;
	char *str = buf;

	if (!entry || !buf)
		return -EINVAL;

	var->DataSize = 1024;
	if (efivar_entry_get(entry, &var->Attributes, &var->DataSize, var->Data))
		return -EIO;

	if (var->Attributes & EFI_VARIABLE_NON_VOLATILE)
		str += sprintf(str, "EFI_VARIABLE_NON_VOLATILE\n");
	if (var->Attributes & EFI_VARIABLE_BOOTSERVICE_ACCESS)
		str += sprintf(str, "EFI_VARIABLE_BOOTSERVICE_ACCESS\n");
	if (var->Attributes & EFI_VARIABLE_RUNTIME_ACCESS)
		str += sprintf(str, "EFI_VARIABLE_RUNTIME_ACCESS\n");
	if (var->Attributes & EFI_VARIABLE_HARDWARE_ERROR_RECORD)
		str += sprintf(str, "EFI_VARIABLE_HARDWARE_ERROR_RECORD\n");
	if (var->Attributes & EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS)
		str += sprintf(str,
			"EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS\n");
	if (var->Attributes &
			EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS)
		str += sprintf(str,
			"EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS\n");
	if (var->Attributes & EFI_VARIABLE_APPEND_WRITE)
		str += sprintf(str, "EFI_VARIABLE_APPEND_WRITE\n");
	return str - buf;
}

static ssize_t
efivar_size_read(struct efivar_entry *entry, char *buf)
{
	struct efi_variable *var = &entry->var;
	char *str = buf;

	if (!entry || !buf)
		return -EINVAL;

	var->DataSize = 1024;
	if (efivar_entry_get(entry, &var->Attributes, &var->DataSize, var->Data))
		return -EIO;

	str += sprintf(str, "0x%lx\n", var->DataSize);
	return str - buf;
}

static ssize_t
efivar_data_read(struct efivar_entry *entry, char *buf)
{
	struct efi_variable *var = &entry->var;

	if (!entry || !buf)
		return -EINVAL;

	var->DataSize = 1024;
	if (efivar_entry_get(entry, &var->Attributes, &var->DataSize, var->Data))
		return -EIO;

	memcpy(buf, var->Data, var->DataSize);
	return var->DataSize;
}
/*
 * We allow each variable to be edited via rewriting the
 * entire efi variable structure.
 */
static ssize_t
efivar_store_raw(struct efivar_entry *entry, const char *buf, size_t count)
{
	struct efi_variable *new_var, *var = &entry->var;
	int err;

	if (count != sizeof(struct efi_variable))
		return -EINVAL;

	new_var = (struct efi_variable *)buf;
	/*
	 * If only updating the variable data, then the name
	 * and guid should remain the same
	 */
	if (memcmp(new_var->VariableName, var->VariableName, sizeof(var->VariableName)) ||
		efi_guidcmp(new_var->VendorGuid, var->VendorGuid)) {
		printk(KERN_ERR "efivars: Cannot edit the wrong variable!\n");
		return -EINVAL;
	}

	if ((new_var->DataSize <= 0) || (new_var->Attributes == 0)){
		printk(KERN_ERR "efivars: DataSize & Attributes must be valid!\n");
		return -EINVAL;
	}

	if ((new_var->Attributes & ~EFI_VARIABLE_MASK) != 0 ||
	    efivar_validate(new_var, new_var->Data, new_var->DataSize) == false) {
		printk(KERN_ERR "efivars: Malformed variable content\n");
		return -EINVAL;
	}

	memcpy(&entry->var, new_var, count);

	err = efivar_entry_set(entry, new_var->Attributes,
			       new_var->DataSize, new_var->Data, false);
	if (err) {
		printk(KERN_WARNING "efivars: set_variable() failed: status=%d\n", err);
		return -EIO;
	}

	return count;
}

static ssize_t
efivar_show_raw(struct efivar_entry *entry, char *buf)
{
	struct efi_variable *var = &entry->var;

	if (!entry || !buf)
		return 0;

	var->DataSize = 1024;
	if (efivar_entry_get(entry, &entry->var.Attributes,
			     &entry->var.DataSize, entry->var.Data))
		return -EIO;

	memcpy(buf, var, sizeof(*var));

	return sizeof(*var);
}

/*
 * Generic read/write functions that call the specific functions of
 * the attributes...
 */
static ssize_t efivar_attr_show(struct kobject *kobj, struct attribute *attr,
				char *buf)
{
	struct efivar_entry *var = to_efivar_entry(kobj);
	struct efivar_attribute *efivar_attr = to_efivar_attr(attr);
	ssize_t ret = -EIO;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (efivar_attr->show) {
		ret = efivar_attr->show(var, buf);
	}
	return ret;
}

static ssize_t efivar_attr_store(struct kobject *kobj, struct attribute *attr,
				const char *buf, size_t count)
{
	struct efivar_entry *var = to_efivar_entry(kobj);
	struct efivar_attribute *efivar_attr = to_efivar_attr(attr);
	ssize_t ret = -EIO;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (efivar_attr->store)
		ret = efivar_attr->store(var, buf, count);

	return ret;
}

static const struct sysfs_ops efivar_attr_ops = {
	.show = efivar_attr_show,
	.store = efivar_attr_store,
};

static void efivar_release(struct kobject *kobj)
{
	struct efivar_entry *var = container_of(kobj, struct efivar_entry, kobj);
	kfree(var);
}

static EFIVAR_ATTR(guid, 0400, efivar_guid_read, NULL);
static EFIVAR_ATTR(attributes, 0400, efivar_attr_read, NULL);
static EFIVAR_ATTR(size, 0400, efivar_size_read, NULL);
static EFIVAR_ATTR(data, 0400, efivar_data_read, NULL);
static EFIVAR_ATTR(raw_var, 0600, efivar_show_raw, efivar_store_raw);

static struct attribute *def_attrs[] = {
	&efivar_attr_guid.attr,
	&efivar_attr_size.attr,
	&efivar_attr_attributes.attr,
	&efivar_attr_data.attr,
	&efivar_attr_raw_var.attr,
	NULL,
};

static struct kobj_type efivar_ktype = {
	.release = efivar_release,
	.sysfs_ops = &efivar_attr_ops,
	.default_attrs = def_attrs,
};

static int efi_status_to_err(efi_status_t status)
{
	int err;

	switch (status) {
	case EFI_SUCCESS:
		err = 0;
		break;
	case EFI_INVALID_PARAMETER:
		err = -EINVAL;
		break;
	case EFI_OUT_OF_RESOURCES:
		err = -ENOSPC;
		break;
	case EFI_DEVICE_ERROR:
		err = -EIO;
		break;
	case EFI_WRITE_PROTECTED:
		err = -EROFS;
		break;
	case EFI_SECURITY_VIOLATION:
		err = -EACCES;
		break;
	case EFI_NOT_FOUND:
		err = -ENOENT;
		break;
	default:
		err = -EINVAL;
	}

	return err;
}

static ssize_t efivar_create(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *bin_attr,
			     char *buf, loff_t pos, size_t count)
{
	struct efi_variable *new_var = (struct efi_variable *)buf;
	struct efivar_entry *new_entry;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if ((new_var->Attributes & ~EFI_VARIABLE_MASK) != 0 ||
	    efivar_validate(new_var, new_var->Data, new_var->DataSize) == false) {
		printk(KERN_ERR "efivars: Malformed variable content\n");
		return -EINVAL;
	}

	new_entry = kzalloc(sizeof(*new_entry), GFP_KERNEL);
	if (!new_entry)
		return -ENOMEM;

	memcpy(&new_entry->var, new_var, sizeof(*new_var));

	err = efivar_entry_set(new_entry, new_var->Attributes, new_var->DataSize,
			       new_var->Data, &efivar_sysfs_list);
	if (err) {
		if (err == -EEXIST)
			err = -EINVAL;
		goto out;
	}

	if (efivar_create_sysfs_entry(new_entry)) {
		printk(KERN_WARNING "efivars: failed to create sysfs entry.\n");
		kfree(new_entry);
	}
	return count;

out:
	kfree(new_entry);
	return err;
}

static ssize_t efivar_delete(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *bin_attr,
			     char *buf, loff_t pos, size_t count)
{
	struct efi_variable *del_var = (struct efi_variable *)buf;
	struct efivar_entry *entry;
	int err = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	efivar_entry_iter_begin();
	entry = efivar_entry_find(del_var->VariableName, del_var->VendorGuid,
				  &efivar_sysfs_list, true);
	if (!entry)
		err = -EINVAL;
	else if (__efivar_entry_delete(entry))
		err = -EIO;

	efivar_entry_iter_end();

	if (err)
		return err;

	efivar_unregister(entry);

	/* It's dead Jim.... */
	return count;
}

static bool variable_is_present(efi_char16_t *variable_name, efi_guid_t *vendor,
				struct list_head *head)
{
	struct efivar_entry *entry, *n;
	unsigned long strsize1, strsize2;
	bool found = false;

	strsize1 = utf16_strsize(variable_name, 1024);
	list_for_each_entry_safe(entry, n, head, list) {
		strsize2 = utf16_strsize(entry->var.VariableName, 1024);
		if (strsize1 == strsize2 &&
			!memcmp(variable_name, &(entry->var.VariableName),
				strsize2) &&
			!efi_guidcmp(entry->var.VendorGuid,
				*vendor)) {
			found = true;
			break;
		}
	}
	return found;
}

static int efivar_update_sysfs_entry(efi_char16_t *name, efi_guid_t vendor,
				     unsigned long name_size, void *data)
{
	struct efivar_entry *entry = data;

	if (efivar_entry_find(name, vendor, &efivar_sysfs_list, false))
		return 0;

	memcpy(entry->var.VariableName, name, name_size);
	memcpy(&(entry->var.VendorGuid), &vendor, sizeof(efi_guid_t));

	return 1;
}

/*
 * Returns the size of variable_name, in bytes, including the
 * terminating NULL character, or variable_name_size if no NULL
 * character is found among the first variable_name_size bytes.
 */
static unsigned long var_name_strnsize(efi_char16_t *variable_name,
				       unsigned long variable_name_size)
{
	unsigned long len;
	efi_char16_t c;

	/*
	 * The variable name is, by definition, a NULL-terminated
	 * string, so make absolutely sure that variable_name_size is
	 * the value we expect it to be. If not, return the real size.
	 */
	for (len = 2; len <= variable_name_size; len += sizeof(c)) {
		c = variable_name[(len / sizeof(c)) - 1];
		if (!c)
			break;
	}

	return min(len, variable_name_size);
}

static void efivar_update_sysfs_entries(struct work_struct *work)
{
	struct efivar_entry *entry;
	int err;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return;

	/* Add new sysfs entries */
	while (1) {
		memset(entry, 0, sizeof(*entry));

		err = efivar_init(efivar_update_sysfs_entry, entry,
				  true, false, &efivar_sysfs_list);
		if (!err)
			break;

		efivar_create_sysfs_entry(entry);
	}

	kfree(entry);
}

/*
 * Let's not leave out systab information that snuck into
 * the efivars driver
 */
static ssize_t systab_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	char *str = buf;

	if (!kobj || !buf)
		return -EINVAL;

	if (efi.mps != EFI_INVALID_TABLE_ADDR)
		str += sprintf(str, "MPS=0x%lx\n", efi.mps);
	if (efi.acpi20 != EFI_INVALID_TABLE_ADDR)
		str += sprintf(str, "ACPI20=0x%lx\n", efi.acpi20);
	if (efi.acpi != EFI_INVALID_TABLE_ADDR)
		str += sprintf(str, "ACPI=0x%lx\n", efi.acpi);
	if (efi.smbios != EFI_INVALID_TABLE_ADDR)
		str += sprintf(str, "SMBIOS=0x%lx\n", efi.smbios);
	if (efi.hcdp != EFI_INVALID_TABLE_ADDR)
		str += sprintf(str, "HCDP=0x%lx\n", efi.hcdp);
	if (efi.boot_info != EFI_INVALID_TABLE_ADDR)
		str += sprintf(str, "BOOTINFO=0x%lx\n", efi.boot_info);
	if (efi.uga != EFI_INVALID_TABLE_ADDR)
		str += sprintf(str, "UGA=0x%lx\n", efi.uga);

	return str - buf;
}

static struct kobj_attribute efi_attr_systab =
			__ATTR(systab, 0400, systab_show, NULL);

static struct attribute *efi_subsys_attrs[] = {
	&efi_attr_systab.attr,
	NULL,	/* maybe more in the future? */
};

static struct attribute_group efi_subsys_attr_group = {
	.attrs = efi_subsys_attrs,
};

static struct kobject *efi_kobj;

/**
 * efivar_create_sysfs_entry - create a new entry in sysfs
 * @new_var: efivar entry to create
 *
 * Returns 1 on failure, 0 on success
 */
static int
efivar_create_sysfs_entry(struct efivar_entry *new_var)
{
	int i, short_name_size;
	char *short_name;
	unsigned long variable_name_size;
	efi_char16_t *variable_name;

	variable_name = new_var->var.VariableName;
	variable_name_size = utf16_strlen(variable_name) * sizeof(efi_char16_t);

	/*
	 * Length of the variable bytes in ASCII, plus the '-' separator,
	 * plus the GUID, plus trailing NUL
	 */
	short_name_size = variable_name_size / sizeof(efi_char16_t)
				+ 1 + EFI_VARIABLE_GUID_LEN + 1;

	short_name = kzalloc(short_name_size, GFP_KERNEL);

	if (!short_name) {
		kfree(short_name);
		return 1;
	}

	/* Convert Unicode to normal chars (assume top bits are 0),
	   ala UTF-8 */
	for (i=0; i < (int)(variable_name_size / sizeof(efi_char16_t)); i++) {
		short_name[i] = variable_name[i] & 0xFF;
	}
	/* This is ugly, but necessary to separate one vendor's
	   private variables from another's.         */

	*(short_name + strlen(short_name)) = '-';
	efi_guid_unparse(&new_var->var.VendorGuid,
			 short_name + strlen(short_name));

	new_var->kobj.kset = efivars_kset;

	i = kobject_init_and_add(&new_var->kobj, &efivar_ktype,
				   NULL, "%s", short_name);
	kfree(short_name);
	if (i)
		return 1;

	kobject_uevent(&new_var->kobj, KOBJ_ADD);
	efivar_entry_add(new_var, &efivar_sysfs_list);

	return 0;
}

static int
create_efivars_bin_attributes(void)
{
	struct bin_attribute *attr;
	int error;

	/* new_var */
	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	attr->attr.name = "new_var";
	attr->attr.mode = 0200;
	attr->write = efivar_create;
	efivars_new_var = attr;

	/* del_var */
	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr) {
		error = -ENOMEM;
		goto out_free;
	}
	attr->attr.name = "del_var";
	attr->attr.mode = 0200;
	attr->write = efivar_delete;
	efivars_del_var = attr;

	sysfs_bin_attr_init(efivars_new_var);
	sysfs_bin_attr_init(efivars_del_var);

	/* Register */
	error = sysfs_create_bin_file(&efivars_kset->kobj, efivars_new_var);
	if (error) {
		printk(KERN_ERR "efivars: unable to create new_var sysfs file"
			" due to error %d\n", error);
		goto out_free;
	}

	error = sysfs_create_bin_file(&efivars_kset->kobj, efivars_del_var);
	if (error) {
		printk(KERN_ERR "efivars: unable to create del_var sysfs file"
			" due to error %d\n", error);
		sysfs_remove_bin_file(&efivars_kset->kobj, efivars_new_var);
		goto out_free;
	}

	return 0;
out_free:
	kfree(efivars_del_var);
	efivars_del_var = NULL;
	kfree(efivars_new_var);
	efivars_new_var = NULL;
	return error;
}

static int efivars_sysfs_callback(efi_char16_t *name, efi_guid_t vendor,
				  unsigned long name_size, void *data)
{
	struct efivar_entry *entry;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	memcpy(entry->var.VariableName, name, name_size);
	memcpy(&(entry->var.VendorGuid), &vendor, sizeof(efi_guid_t));

	efivar_create_sysfs_entry(entry);

	return 0;
}

static int efivar_sysfs_destroy(struct efivar_entry *entry, void *data)
{
	efivar_entry_remove(entry);
	efivar_unregister(entry);
	return 0;
}

/*
 * Print a warning when duplicate EFI variables are encountered and
 * disable the sysfs workqueue since the firmware is buggy.
 */
static void dup_variable_bug(efi_char16_t *s16, efi_guid_t *vendor_guid,
			     unsigned long len16)
{
	size_t i, len8 = len16 / sizeof(efi_char16_t);
	char *s8;

	/*
	 * Disable the workqueue since the algorithm it uses for
	 * detecting new variables won't work with this buggy
	 * implementation of GetNextVariableName().
	 */
	efivar_wq_enabled = false;

	s8 = kzalloc(len8, GFP_KERNEL);
	if (!s8)
		return;

	for (i = 0; i < len8; i++)
		s8[i] = s16[i];

	printk(KERN_WARNING "efivars: duplicate variable: %s-%pUl\n",
	       s8, vendor_guid);
	kfree(s8);
}

static struct kobject *efivars_kobj;

void efivars_sysfs_exit(void)
{
	/* Remove all entries and destroy */
	__efivar_entry_iter(efivar_sysfs_destroy, &efivar_sysfs_list, NULL, NULL);

	if (efivars_new_var)
		sysfs_remove_bin_file(&efivars_kset->kobj, efivars_new_var);
	if (efivars_del_var)
		sysfs_remove_bin_file(&efivars_kset->kobj, efivars_del_var);
	kfree(efivars_new_var);
	kfree(efivars_del_var);
	kobject_put(efivars_kobj);
	kset_unregister(efivars_kset);
}

int efivars_sysfs_init(void)
{
	struct kobject *parent_kobj = efivars_kobject();
	int error = 0;

	/* No efivars has been registered yet */
	if (!parent_kobj)
		return 0;

	printk(KERN_INFO "EFI Variables Facility v%s %s\n", EFIVARS_VERSION,
	       EFIVARS_DATE);

	efivars_kset = kset_create_and_add("vars", NULL, parent_kobj);
	if (!efivars_kset) {
		printk(KERN_ERR "efivars: Subsystem registration failed.\n");
		return -ENOMEM;
	}

	efivars_kobj = kobject_create_and_add("efivars", parent_kobj);
	if (!efivars_kobj) {
		pr_err("efivars: Subsystem registration failed.\n");
		kset_unregister(efivars_kset);
		return -ENOMEM;
	}

	efivar_init(efivars_sysfs_callback, NULL, false,
		    true, &efivar_sysfs_list);

	error = create_efivars_bin_attributes();
	if (error)
		efivars_sysfs_exit();

	return error;
}
EXPORT_SYMBOL_GPL(efivars_sysfs_init);

/**
 * efivar_init - build the initial list of EFI variables
 * @func: callback function to invoke for every variable
 * @data: function-specific data to pass to @func
 * @atomic: do we need to execute the @func-loop atomically?
 * @duplicates: error if we encounter duplicates on @head?
 * @head: initialised head of variable list
 *
 * Get every EFI variable from the firmware and invoke @func. @func
 * should call efivar_entry_add() to build the list of variables.
 *
 * Returns 0 on success, or a kernel error code on failure.
 */
int efivar_init(int (*func)(efi_char16_t *, efi_guid_t, unsigned long, void *),
		void *data, bool atomic, bool duplicates,
		struct list_head *head)
{
	const struct efivar_operations *ops = __efivars->ops;
	unsigned long variable_name_size = 1024;
	efi_char16_t *variable_name;
	efi_status_t status;
	efi_guid_t vendor_guid;
	int err = 0;

	variable_name = kzalloc(variable_name_size, GFP_KERNEL);
	if (!variable_name) {
		printk(KERN_ERR "efivars: Memory allocation failed.\n");
		return -ENOMEM;
	}

	spin_lock_irq(&__efivars->lock);

	/*
	 * Per EFI spec, the maximum storage allocated for both
	 * the variable name and variable data is 1024 bytes.
	 */

	do {
		variable_name_size = 1024;

		status = ops->get_next_variable(&variable_name_size,
						variable_name,
						&vendor_guid);
		switch (status) {
		case EFI_SUCCESS:
			if (!atomic)
				spin_unlock_irq(&__efivars->lock);

			variable_name_size = var_name_strnsize(variable_name,
							       variable_name_size);

			/*
			 * Some firmware implementations return the
			 * same variable name on multiple calls to
			 * get_next_variable(). Terminate the loop
			 * immediately as there is no guarantee that
			 * we'll ever see a different variable name,
			 * and may end up looping here forever.
			 */
			if (duplicates &&
			    variable_is_present(variable_name, &vendor_guid, head)) {
				dup_variable_bug(variable_name, &vendor_guid,
						 variable_name_size);
				if (!atomic)
					spin_lock_irq(&__efivars->lock);

				status = EFI_NOT_FOUND;
				break;
			}

			err = func(variable_name, vendor_guid, variable_name_size, data);
			if (err)
				status = EFI_NOT_FOUND;

			if (!atomic)
				spin_lock_irq(&__efivars->lock);

			break;
		case EFI_NOT_FOUND:
			break;
		default:
			printk(KERN_WARNING "efivars: get_next_variable: status=%lx\n",
				status);
			status = EFI_NOT_FOUND;
			break;
		}

	} while (status != EFI_NOT_FOUND);

	spin_unlock_irq(&__efivars->lock);

	kfree(variable_name);

	return err;
}
EXPORT_SYMBOL_GPL(efivar_init);

/**
 * efivar_entry_add - add entry to variable list
 * @entry: entry to add to list
 * @head: list head
 */
void efivar_entry_add(struct efivar_entry *entry, struct list_head *head)
{
	spin_lock_irq(&__efivars->lock);
	list_add(&entry->list, head);
	spin_unlock_irq(&__efivars->lock);
}
EXPORT_SYMBOL_GPL(efivar_entry_add);

/**
 * efivar_entry_remove - remove entry from variable list
 * @entry: entry to remove from list
 */
void efivar_entry_remove(struct efivar_entry *entry)
{
	spin_lock_irq(&__efivars->lock);
	list_del(&entry->list);
	spin_unlock_irq(&__efivars->lock);
}
EXPORT_SYMBOL_GPL(efivar_entry_remove);

/*
 * efivar_entry_list_del_unlock - remove entry from variable list
 * @entry: entry to remove
 *
 * Remove @entry from the variable list and release the list lock.
 *
 * NOTE: slightly weird locking semantics here - we expect to be
 * called with the efivars lock already held, and we release it before
 * returning. This is because this function is usually called after
 * set_variable() while the lock is still held.
 */
static void efivar_entry_list_del_unlock(struct efivar_entry *entry)
{
	WARN_ON(!spin_is_locked(&__efivars->lock));

	list_del(&entry->list);
	spin_unlock_irq(&__efivars->lock);
}

/**
 * __efivar_entry_delete - delete an EFI variable
 * @entry: entry containing EFI variable to delete
 *
 * Delete the variable from the firmware and remove @entry from the
 * variable list. It is the caller's responsibility to free @entry
 * once we return.
 *
 * This function differs from efivar_entry_delete() because it is
 * safe to be called from within a efivar_entry_iter_begin() and
 * efivar_entry_iter_end() region, unlike efivar_entry_delete().
 *
 * Returns 0 on success, or a converted EFI status code if
 * set_variable() fails. If set_variable() fails the entry remains
 * on the list.
 */
int __efivar_entry_delete(struct efivar_entry *entry)
{
	const struct efivar_operations *ops = __efivars->ops;
	efi_status_t status;

	WARN_ON(!spin_is_locked(&__efivars->lock));

	status = ops->set_variable(entry->var.VariableName,
				   &entry->var.VendorGuid,
				   0, 0, NULL);
	if (status)
		return efi_status_to_err(status);

	list_del(&entry->list);

	return 0;
}
EXPORT_SYMBOL_GPL(__efivar_entry_delete);

/**
 * efivar_entry_delete - delete variable and remove entry from list
 * @entry: entry containing variable to delete
 *
 * Delete the variable from the firmware and remove @entry from the
 * variable list. It is the caller's responsibility to free @entry
 * once we return.
 *
 * Returns 0 on success, or a converted EFI status code if
 * set_variable() fails.
 */
int efivar_entry_delete(struct efivar_entry *entry)
{
	const struct efivar_operations *ops = __efivars->ops;
	efi_status_t status;

	spin_lock_irq(&__efivars->lock);
	status = ops->set_variable(entry->var.VariableName,
				   &entry->var.VendorGuid,
				   0, 0, NULL);
	if (!(status == EFI_SUCCESS || status == EFI_NOT_FOUND)) {
		spin_unlock_irq(&__efivars->lock);
		return efi_status_to_err(status);
	}

	efivar_entry_list_del_unlock(entry);
	return 0;
}
EXPORT_SYMBOL_GPL(efivar_entry_delete);

/**
 * efivar_entry_set - call set_variable()
 * @entry: entry containing the EFI variable to write
 * @attributes: variable attributes
 * @size: size of @data buffer
 * @data: buffer containing variable data
 * @head: head of variable list
 *
 * Calls set_variable() for an EFI variable. If creating a new EFI
 * variable, this function is usually followed by efivar_entry_add().
 *
 * Before writing the variable, the remaining EFI variable storage
 * space is checked to ensure there is enough room available.
 *
 * If @head is not NULL a lookup is performed to determine whether
 * the entry is already on the list.
 *
 * Returns 0 on success, -EEXIST if a lookup is performed and the entry
 * already exists on the list, or a converted EFI status code if
 * set_variable() fails.
 */
int efivar_entry_set(struct efivar_entry *entry, u32 attributes,
		     unsigned long size, void *data, struct list_head *head)
{
	const struct efivar_operations *ops = __efivars->ops;
	efi_status_t status;
	efi_char16_t *name = entry->var.VariableName;
	efi_guid_t vendor = entry->var.VendorGuid;

	spin_lock_irq(&__efivars->lock);

	if (head && efivar_entry_find(name, vendor, head, false)) {
		spin_unlock_irq(&__efivars->lock);
		return -EEXIST;
	}

	status = check_var_size(attributes, size + utf16_strsize(name, 1024));
	if (status == EFI_SUCCESS || status == EFI_UNSUPPORTED)
		status = ops->set_variable(name, &vendor,
					   attributes, size, data);

	spin_unlock_irq(&__efivars->lock);

	return efi_status_to_err(status);
}
EXPORT_SYMBOL_GPL(efivar_entry_set);

/**
 * efivar_entry_set_safe - call set_variable() if enough space in firmware
 * @name: buffer containing the variable name
 * @vendor: variable vendor guid
 * @attributes: variable attributes
 * @block: can we block in this context?
 * @size: size of @data buffer
 * @data: buffer containing variable data
 *
 * Ensures there is enough free storage in the firmware for this variable, and
 * if so, calls set_variable(). If creating a new EFI variable, this function
 * is usually followed by efivar_entry_add().
 *
 * Returns 0 on success, -ENOSPC if the firmware does not have enough
 * space for set_variable() to succeed, or a converted EFI status code
 * if set_variable() fails.
 */
int efivar_entry_set_safe(efi_char16_t *name, efi_guid_t vendor, u32 attributes,
			  bool block, unsigned long size, void *data)
{
	const struct efivar_operations *ops = __efivars->ops;
	unsigned long flags;
	efi_status_t status;

	if (!ops->query_variable_info)
		return -ENOSYS;

	if (!block && !spin_trylock_irqsave(&__efivars->lock, flags))
		return -EBUSY;
	else
		spin_lock_irqsave(&__efivars->lock, flags);

	status = check_var_size(attributes, size + utf16_strsize(name, 1024));
	if (status != EFI_SUCCESS) {
		spin_unlock_irqrestore(&__efivars->lock, flags);
		return -ENOSPC;
	}

	status = ops->set_variable(name, &vendor, attributes, size, data);

	spin_unlock_irqrestore(&__efivars->lock, flags);

	return efi_status_to_err(status);
}
EXPORT_SYMBOL_GPL(efivar_entry_set_safe);

/**
 * efivar_entry_find - search for an entry
 * @name: the EFI variable name
 * @guid: the EFI variable vendor's guid
 * @head: head of the variable list
 * @remove: should we remove the entry from the list?
 *
 * Search for an entry on the variable list that has the EFI variable
 * name @name and vendor guid @guid. If an entry is found on the list
 * and @remove is true, the entry is removed from the list.
 *
 * The caller MUST call efivar_entry_iter_begin() and
 * efivar_entry_iter_end() before and after the invocation of this
 * function, respectively.
 *
 * Returns the entry if found on the list, %NULL otherwise.
 */
struct efivar_entry *efivar_entry_find(efi_char16_t *name, efi_guid_t guid,
				       struct list_head *head, bool remove)
{
	struct efivar_entry *entry, *n;
	int strsize1, strsize2;
	bool found = false;

	WARN_ON(!spin_is_locked(&__efivars->lock));

	list_for_each_entry_safe(entry, n, head, list) {
		strsize1 = utf16_strsize(name, 1024);
		strsize2 = utf16_strsize(entry->var.VariableName, 1024);
		if (strsize1 == strsize2 &&
		    !memcmp(name, &(entry->var.VariableName), strsize1) &&
		    !efi_guidcmp(guid, entry->var.VendorGuid)) {
			found = true;
			break;
		}
	}

	if (!found)
		return NULL;

	if (remove)
		list_del(&entry->list);

	return entry;
}
EXPORT_SYMBOL_GPL(efivar_entry_find);

/**
 * __efivar_entry_size - obtain the size of a variable
 * @entry: entry for this variable
 * @size: location to store the variable's size
 *
 * The caller MUST call efivar_entry_iter_begin() and
 * efivar_entry_iter_end() before and after the invocation of this
 * function, respectively.
 */
int __efivar_entry_size(struct efivar_entry *entry, unsigned long *size)
{
	const struct efivar_operations *ops = __efivars->ops;
	efi_status_t status;

	WARN_ON(!spin_is_locked(&__efivars->lock));

	*size = 0;
	status = ops->get_variable(entry->var.VariableName,
				   &entry->var.VendorGuid, NULL, size, NULL);
	if (status != EFI_BUFFER_TOO_SMALL)
		return efi_status_to_err(status);

	return 0;
}
EXPORT_SYMBOL_GPL(__efivar_entry_size);

/**
 * efivar_entry_size - obtain the size of a variable
 * @entry: entry for this variable
 * @size: location to store the variable's size
 */
int efivar_entry_size(struct efivar_entry *entry, unsigned long *size)
{
	const struct efivar_operations *ops = __efivars->ops;
	efi_status_t status;

	*size = 0;

	spin_lock_irq(&__efivars->lock);
	status = ops->get_variable(entry->var.VariableName,
				   &entry->var.VendorGuid, NULL, size, NULL);
	spin_unlock_irq(&__efivars->lock);

	if (status != EFI_BUFFER_TOO_SMALL)
		return efi_status_to_err(status);

	return 0;
}
EXPORT_SYMBOL_GPL(efivar_entry_size);

/**
 * efivar_entry_get - call get_variable()
 * @entry: read data for this variable
 * @attributes: variable attributes
 * @size: size of @data buffer
 * @data: buffer to store variable data
 */
int efivar_entry_get(struct efivar_entry *entry, u32 *attributes,
		     unsigned long *size, void *data)
{
	const struct efivar_operations *ops = __efivars->ops;
	efi_status_t status;

	spin_lock_irq(&__efivars->lock);
	status = ops->get_variable(entry->var.VariableName,
				   &entry->var.VendorGuid,
				   attributes, size, data);
	spin_unlock_irq(&__efivars->lock);

	return efi_status_to_err(status);
}
EXPORT_SYMBOL_GPL(efivar_entry_get);

/**
 * efivar_entry_set_get_size - call set_variable() and get new size (atomic)
 * @entry: entry containing variable to set and get
 * @attributes: attributes of variable to be written
 * @size: size of data buffer
 * @data: buffer containing data to write
 * @set: did the set_variable() call succeed?
 *
 * This is a pretty special (complex) function. See efivarfs_file_write().
 *
 * Atomically call set_variable() for @entry and if the call is
 * successful, return the new size of the variable from get_variable()
 * in @size. The success of set_variable() is indicated by @set.
 *
 * Returns 0 on success, -EINVAL if the variable data is invalid,
 * -ENOSPC if the firmware does not have enough available space, or a
 * converted EFI status code if either of set_variable() or
 * get_variable() fail.
 *
 * If the EFI variable does not exist when calling set_variable()
 * (EFI_NOT_FOUND), @entry is removed from the variable list.
 */
int efivar_entry_set_get_size(struct efivar_entry *entry, u32 attributes,
			      unsigned long *size, void *data, bool *set)
{
	const struct efivar_operations *ops = __efivars->ops;
	efi_char16_t *name = entry->var.VariableName;
	efi_guid_t *vendor = &entry->var.VendorGuid;
	efi_status_t status;
	int err;

	*set = false;

	if (efivar_validate(&entry->var, data, *size) == false)
		return -EINVAL;

	/*
	 * The lock here protects the get_variable call, the conditional
	 * set_variable call, and removal of the variable from the efivars
	 * list (in the case of an authenticated delete).
	 */
	spin_lock_irq(&__efivars->lock);

	/*
	 * Ensure that the available space hasn't shrunk below the safe level
	 */
	status = check_var_size(attributes, *size + utf16_strsize(name, 1024));
	if (status != EFI_SUCCESS) {
		if (status != EFI_UNSUPPORTED) {
			err = efi_status_to_err(status);
			goto out;
		}

		if (*size > 65536) {
			err = -ENOSPC;
			goto out;
		}
	}

	status = ops->set_variable(name, vendor, attributes, *size, data);
	if (status != EFI_SUCCESS) {
		err = efi_status_to_err(status);
		goto out;
	}

	*set = true;

	/*
	 * Writing to the variable may have caused a change in size (which
	 * could either be an append or an overwrite), or the variable to be
	 * deleted. Perform a GetVariable() so we can tell what actually
	 * happened.
	 */
	*size = 0;
	status = ops->get_variable(entry->var.VariableName,
				   &entry->var.VendorGuid,
				   NULL, size, NULL);

	if (status == EFI_NOT_FOUND)
		efivar_entry_list_del_unlock(entry);
	else
		spin_unlock_irq(&__efivars->lock);

	if (status && status != EFI_BUFFER_TOO_SMALL)
		return efi_status_to_err(status);

	return 0;

out:
	spin_unlock_irq(&__efivars->lock);
	return err;

}
EXPORT_SYMBOL_GPL(efivar_entry_set_get_size);

/**
 * efivar_entry_iter_begin - begin iterating the variable list
 *
 * Lock the variable list to prevent entry insertion and removal until
 * efivar_entry_iter_end() is called. This function is usually used in
 * conjunction with __efivar_entry_iter() or efivar_entry_iter().
 */
void efivar_entry_iter_begin(void)
{
	spin_lock_irq(&__efivars->lock);
}
EXPORT_SYMBOL_GPL(efivar_entry_iter_begin);

/**
 * efivar_entry_iter_end - finish iterating the variable list
 *
 * Unlock the variable list and allow modifications to the list again.
 */
void efivar_entry_iter_end(void)
{
	spin_unlock_irq(&__efivars->lock);
}
EXPORT_SYMBOL_GPL(efivar_entry_iter_end);

/**
 * __efivar_entry_iter - iterate over variable list
 * @func: callback function
 * @head: head of the variable list
 * @data: function-specific data to pass to callback
 * @prev: entry to begin iterating from
 *
 * Iterate over the list of EFI variables and call @func with every
 * entry on the list. It is safe for @func to remove entries in the
 * list via efivar_entry_delete().
 *
 * You MUST call efivar_enter_iter_begin() before this function, and
 * efivar_entry_iter_end() afterwards.
 *
 * It is possible to begin iteration from an arbitrary entry within
 * the list by passing @prev. @prev is updated on return to point to
 * the last entry passed to @func. To begin iterating from the
 * beginning of the list @prev must be %NULL.
 *
 * The restrictions for @func are the same as documented for
 * efivar_entry_iter().
 */
int __efivar_entry_iter(int (*func)(struct efivar_entry *, void *),
			struct list_head *head, void *data,
			struct efivar_entry **prev)
{
	struct efivar_entry *entry, *n;
	int err = 0;

	if (!prev || !*prev) {
		list_for_each_entry_safe(entry, n, head, list) {
			err = func(entry, data);
			if (err)
				break;
		}

		if (prev)
			*prev = entry;

		return err;
	}


	list_for_each_entry_safe_continue((*prev), n, head, list) {
		err = func(*prev, data);
		if (err)
			break;
	}

	return err;
}
EXPORT_SYMBOL_GPL(__efivar_entry_iter);

/**
 * efivar_entry_iter - iterate over variable list
 * @func: callback function
 * @head: head of variable list
 * @data: function-specific data to pass to callback
 *
 * Iterate over the list of EFI variables and call @func with every
 * entry on the list. It is safe for @func to remove entries in the
 * list via efivar_entry_delete() while iterating.
 *
 * Some notes for the callback function:
 *  - a non-zero return value indicates an error and terminates the loop
 *  - @func is called from atomic context
 */
int efivar_entry_iter(int (*func)(struct efivar_entry *, void *),
		      struct list_head *head, void *data)
{
	int err = 0;

	efivar_entry_iter_begin();
	err = __efivar_entry_iter(func, head, data, NULL);
	efivar_entry_iter_end();

	return err;
}
EXPORT_SYMBOL_GPL(efivar_entry_iter);

/**
 * efivars_kobject - get the kobject for the registered efivars
 *
 * If efivars_register() has not been called we return NULL,
 * otherwise return the kobject used at registration time.
 */
struct kobject *efivars_kobject(void)
{
	if (!__efivars)
		return NULL;

	return __efivars->kobject;
}
EXPORT_SYMBOL_GPL(efivars_kobject);

/**
 * efivar_run_worker - schedule the efivar worker thread
 */
void efivar_run_worker(void)
{
	if (efivar_wq_enabled)
		schedule_work(&efivar_work);
}
EXPORT_SYMBOL_GPL(efivar_run_worker);

/**
 * efivars_register - register an efivars
 * @efivars: efivars to register
 * @ops: efivars operations
 * @kobject: @efivars-specific kobject
 *
 * Only a single efivars can be registered at any time.
 */
int efivars_register(struct efivars *efivars,
		     const struct efivar_operations *ops,
		     struct kobject *kobject)
{
	spin_lock_init(&efivars->lock);
	efivars->ops = ops;
	efivars->kobject = kobject;

	__efivars = efivars;

	return 0;
}
EXPORT_SYMBOL_GPL(efivars_register);

/**
 * efivars_unregister - unregister an efivars
 * @efivars: efivars to unregister
 *
 * The caller must have already removed every entry from the list,
 * failure to do so is an error.
 */
int efivars_unregister(struct efivars *efivars)
{
	int rv;

	if (!__efivars) {
		printk(KERN_ERR "efivars not registered\n");
		rv = -EINVAL;
		goto out;
	}

	if (__efivars != efivars) {
		rv = -EINVAL;
		goto out;
	}

	__efivars = NULL;

	rv = 0;
out:
	return rv;
}
EXPORT_SYMBOL_GPL(efivars_unregister);

static struct efivars generic_efivars;
static struct efivar_operations generic_ops;

static int generic_ops_register(void)
{
	int error;

	generic_ops.get_variable = efi.get_variable;
	generic_ops.set_variable = efi.set_variable;
	generic_ops.get_next_variable = efi.get_next_variable;
	generic_ops.query_variable_info = efi.query_variable_info;

	error = efivars_register(&generic_efivars, &generic_ops, efi_kobj);
	if (error)
		return error;

	error = efivars_sysfs_init();
	if (error)
		efivars_unregister(&generic_efivars);

	return error;
}

static void generic_ops_unregister(void)
{
	efivars_sysfs_exit();
	efivars_unregister(&generic_efivars);
}

/*
 * For now we register the efi subsystem with the firmware subsystem
 * and the vars subsystem with the efi subsystem.  In the future, it
 * might make sense to split off the efi subsystem into its own
 * driver, but for now only efivars will register with it, so just
 * include it here.
 */

static int __init
efivars_init(void)
{
	int error;

	if (!efi_enabled(EFI_RUNTIME_SERVICES))
		return 0;

	/* Register the efi directory at /sys/firmware/efi */
	efi_kobj = kobject_create_and_add("efi", firmware_kobj);
	if (!efi_kobj) {
		printk(KERN_ERR "efivars: Firmware registration failed.\n");
		return -ENOMEM;
	}

	error = generic_ops_register();
	if (error)
		goto err_put;

	/* Don't forget the systab entry */
	error = sysfs_create_group(efi_kobj, &efi_subsys_attr_group);
	if (error) {
		printk(KERN_ERR
		       "efivars: Sysfs attribute export failed with error %d.\n",
		       error);
		goto err_unregister;
	}

	return 0;

err_unregister:
	generic_ops_unregister();
err_put:
	kobject_put(efi_kobj);
	return error;
}

static void __exit
efivars_exit(void)
{
	cancel_work_sync(&efivar_work);

	if (efi_enabled(EFI_RUNTIME_SERVICES)) {
		generic_ops_unregister();
		kobject_put(efi_kobj);
	}
}

module_init(efivars_init);
module_exit(efivars_exit);

