/*
 * Originally from efivars.c,
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

#include <linux/efi.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ucs2_string.h>

#define EFIVARS_VERSION "0.08"
#define EFIVARS_DATE "2004-May-17"

MODULE_AUTHOR("Matt Domsch <Matt_Domsch@Dell.com>");
MODULE_DESCRIPTION("sysfs interface to EFI Variables");
MODULE_LICENSE("GPL");
MODULE_VERSION(EFIVARS_VERSION);

LIST_HEAD(efivar_sysfs_list);
EXPORT_SYMBOL_GPL(efivar_sysfs_list);

static struct kset *efivars_kset;

static struct bin_attribute *efivars_new_var;
static struct bin_attribute *efivars_del_var;

struct efivar_attribute {
	struct attribute attr;
	ssize_t (*show) (struct efivar_entry *entry, char *buf);
	ssize_t (*store)(struct efivar_entry *entry, const char *buf, size_t count);
};

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

	if (err) {
		efivar_entry_iter_end();
		return err;
	}

	if (!entry->scanning) {
		efivar_entry_iter_end();
		efivar_unregister(entry);
	} else
		efivar_entry_iter_end();

	/* It's dead Jim.... */
	return count;
}

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
	variable_name_size = ucs2_strlen(variable_name) * sizeof(efi_char16_t);

	/*
	 * Length of the variable bytes in ASCII, plus the '-' separator,
	 * plus the GUID, plus trailing NUL
	 */
	short_name_size = variable_name_size / sizeof(efi_char16_t)
				+ 1 + EFI_VARIABLE_GUID_LEN + 1;

	short_name = kzalloc(short_name_size, GFP_KERNEL);

	if (!short_name)
		return 1;

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

static void efivar_update_sysfs_entries(struct work_struct *work)
{
	struct efivar_entry *entry;
	int err;

	/* Add new sysfs entries */
	while (1) {
		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry)
			return;

		err = efivar_init(efivar_update_sysfs_entry, entry,
				  true, false, &efivar_sysfs_list);
		if (!err)
			break;

		efivar_create_sysfs_entry(entry);
	}

	kfree(entry);
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

static void efivars_sysfs_exit(void)
{
	/* Remove all entries and destroy */
	__efivar_entry_iter(efivar_sysfs_destroy, &efivar_sysfs_list, NULL, NULL);

	if (efivars_new_var)
		sysfs_remove_bin_file(&efivars_kset->kobj, efivars_new_var);
	if (efivars_del_var)
		sysfs_remove_bin_file(&efivars_kset->kobj, efivars_del_var);
	kfree(efivars_new_var);
	kfree(efivars_del_var);
	kset_unregister(efivars_kset);
}

int efivars_sysfs_init(void)
{
	struct kobject *parent_kobj = efivars_kobject();
	int error = 0;

	if (!efi_enabled(EFI_RUNTIME_SERVICES))
		return -ENODEV;

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

	efivar_init(efivars_sysfs_callback, NULL, false,
		    true, &efivar_sysfs_list);

	error = create_efivars_bin_attributes();
	if (error) {
		efivars_sysfs_exit();
		return error;
	}

	INIT_WORK(&efivar_work, efivar_update_sysfs_entries);

	return 0;
}
EXPORT_SYMBOL_GPL(efivars_sysfs_init);

module_init(efivars_sysfs_init);
module_exit(efivars_sysfs_exit);
