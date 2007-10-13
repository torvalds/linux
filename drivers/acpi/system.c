/*
 *  acpi_system.c - ACPI System Driver ($Revision: 63 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <asm/uaccess.h>

#include <acpi/acpi_drivers.h>

#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME("system");
#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "acpi."

#define ACPI_SYSTEM_CLASS		"system"
#define ACPI_SYSTEM_DEVICE_NAME		"System"

/*
 * Make ACPICA version work as module param
 */
static int param_get_acpica_version(char *buffer, struct kernel_param *kp)
{
	int result;

	result = sprintf(buffer, "%x", ACPI_CA_VERSION);

	return result;
}

module_param_call(acpica_version, NULL, param_get_acpica_version, NULL, 0444);

/* --------------------------------------------------------------------------
                              FS Interface (/sys)
   -------------------------------------------------------------------------- */
static LIST_HEAD(acpi_table_attr_list);
static struct kobject tables_kobj;

struct acpi_table_attr {
	struct bin_attribute attr;
	char name[8];
	int instance;
	struct list_head node;
};

static ssize_t acpi_table_show(struct kobject *kobj,
			       struct bin_attribute *bin_attr, char *buf,
			       loff_t offset, size_t count)
{
	struct acpi_table_attr *table_attr =
	    container_of(bin_attr, struct acpi_table_attr, attr);
	struct acpi_table_header *table_header = NULL;
	acpi_status status;
	ssize_t ret_count = count;

	status =
	    acpi_get_table(table_attr->name, table_attr->instance,
			   &table_header);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	if (offset >= table_header->length) {
		ret_count = 0;
		goto end;
	}

	if (offset + ret_count > table_header->length)
		ret_count = table_header->length - offset;

	memcpy(buf, ((char *)table_header) + offset, ret_count);

      end:
	return ret_count;
}

static void acpi_table_attr_init(struct acpi_table_attr *table_attr,
				 struct acpi_table_header *table_header)
{
	struct acpi_table_header *header = NULL;
	struct acpi_table_attr *attr = NULL;

	memcpy(table_attr->name, table_header->signature, ACPI_NAME_SIZE);

	list_for_each_entry(attr, &acpi_table_attr_list, node) {
		if (!memcmp(table_header->signature, attr->name,
			    ACPI_NAME_SIZE))
			if (table_attr->instance < attr->instance)
				table_attr->instance = attr->instance;
	}
	table_attr->instance++;

	if (table_attr->instance > 1 || (table_attr->instance == 1 &&
					 !acpi_get_table(table_header->
							 signature, 2,
							 &header)))
		sprintf(table_attr->name + 4, "%d", table_attr->instance);

	table_attr->attr.size = 0;
	table_attr->attr.read = acpi_table_show;
	table_attr->attr.attr.name = table_attr->name;
	table_attr->attr.attr.mode = 0444;
	table_attr->attr.attr.owner = THIS_MODULE;

	return;
}

static int acpi_system_sysfs_init(void)
{
	struct acpi_table_attr *table_attr;
	struct acpi_table_header *table_header = NULL;
	int table_index = 0;
	int result;

	tables_kobj.parent = &acpi_subsys.kobj;
	kobject_set_name(&tables_kobj, "tables");
	result = kobject_register(&tables_kobj);
	if (result)
		return result;

	do {
		result = acpi_get_table_by_index(table_index, &table_header);
		if (!result) {
			table_index++;
			table_attr = NULL;
			table_attr =
			    kzalloc(sizeof(struct acpi_table_attr), GFP_KERNEL);
			if (!table_attr)
				return -ENOMEM;

			acpi_table_attr_init(table_attr, table_header);
			result =
			    sysfs_create_bin_file(&tables_kobj,
						  &table_attr->attr);
			if (result) {
				kfree(table_attr);
				return result;
			} else
				list_add_tail(&table_attr->node,
					      &acpi_table_attr_list);
		}
	} while (!result);

	return 0;
}

/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */
#ifdef CONFIG_ACPI_PROCFS
#define ACPI_SYSTEM_FILE_INFO		"info"
#define ACPI_SYSTEM_FILE_EVENT		"event"
#define ACPI_SYSTEM_FILE_DSDT		"dsdt"
#define ACPI_SYSTEM_FILE_FADT		"fadt"

static int acpi_system_read_info(struct seq_file *seq, void *offset)
{

	seq_printf(seq, "version:                 %x\n", ACPI_CA_VERSION);
	return 0;
}

static int acpi_system_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_system_read_info, PDE(inode)->data);
}

static const struct file_operations acpi_system_info_ops = {
	.open = acpi_system_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t acpi_system_read_dsdt(struct file *, char __user *, size_t,
				     loff_t *);

static const struct file_operations acpi_system_dsdt_ops = {
	.read = acpi_system_read_dsdt,
};

static ssize_t
acpi_system_read_dsdt(struct file *file,
		      char __user * buffer, size_t count, loff_t * ppos)
{
	acpi_status status = AE_OK;
	struct acpi_table_header *dsdt = NULL;
	ssize_t res;

	status = acpi_get_table(ACPI_SIG_DSDT, 1, &dsdt);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	res = simple_read_from_buffer(buffer, count, ppos, dsdt, dsdt->length);

	return res;
}

static ssize_t acpi_system_read_fadt(struct file *, char __user *, size_t,
				     loff_t *);

static const struct file_operations acpi_system_fadt_ops = {
	.read = acpi_system_read_fadt,
};

static ssize_t
acpi_system_read_fadt(struct file *file,
		      char __user * buffer, size_t count, loff_t * ppos)
{
	acpi_status status = AE_OK;
	struct acpi_table_header *fadt = NULL;
	ssize_t res;

	status = acpi_get_table(ACPI_SIG_FADT, 1, &fadt);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	res = simple_read_from_buffer(buffer, count, ppos, fadt, fadt->length);

	return res;
}

static int acpi_system_procfs_init(void)
{
	struct proc_dir_entry *entry;
	int error = 0;
	char *name;

	/* 'info' [R] */
	name = ACPI_SYSTEM_FILE_INFO;
	entry = create_proc_entry(name, S_IRUGO, acpi_root_dir);
	if (!entry)
		goto Error;
	else {
		entry->proc_fops = &acpi_system_info_ops;
	}

	/* 'dsdt' [R] */
	name = ACPI_SYSTEM_FILE_DSDT;
	entry = create_proc_entry(name, S_IRUSR, acpi_root_dir);
	if (entry)
		entry->proc_fops = &acpi_system_dsdt_ops;
	else
		goto Error;

	/* 'fadt' [R] */
	name = ACPI_SYSTEM_FILE_FADT;
	entry = create_proc_entry(name, S_IRUSR, acpi_root_dir);
	if (entry)
		entry->proc_fops = &acpi_system_fadt_ops;
	else
		goto Error;

      Done:
	return error;

      Error:
	remove_proc_entry(ACPI_SYSTEM_FILE_FADT, acpi_root_dir);
	remove_proc_entry(ACPI_SYSTEM_FILE_DSDT, acpi_root_dir);
	remove_proc_entry(ACPI_SYSTEM_FILE_INFO, acpi_root_dir);

	error = -EFAULT;
	goto Done;
}
#else
static int acpi_system_procfs_init(void)
{
	return 0;
}
#endif

static int __init acpi_system_init(void)
{
	int result = 0;

	if (acpi_disabled)
		return 0;

	result = acpi_system_procfs_init();
	if (result)
		return result;

	result = acpi_system_sysfs_init();

	return result;
}

subsys_initcall(acpi_system_init);
