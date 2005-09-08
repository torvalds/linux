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
ACPI_MODULE_NAME("acpi_system")
#define ACPI_SYSTEM_CLASS		"system"
#define ACPI_SYSTEM_DRIVER_NAME		"ACPI System Driver"
#define ACPI_SYSTEM_DEVICE_NAME		"System"
#define ACPI_SYSTEM_FILE_INFO		"info"
#define ACPI_SYSTEM_FILE_EVENT		"event"
#define ACPI_SYSTEM_FILE_DSDT		"dsdt"
#define ACPI_SYSTEM_FILE_FADT		"fadt"
extern FADT_DESCRIPTOR acpi_fadt;

/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

static int acpi_system_read_info(struct seq_file *seq, void *offset)
{
	ACPI_FUNCTION_TRACE("acpi_system_read_info");

	seq_printf(seq, "version:                 %x\n", ACPI_CA_VERSION);
	return_VALUE(0);
}

static int acpi_system_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_system_read_info, PDE(inode)->data);
}

static struct file_operations acpi_system_info_ops = {
	.open = acpi_system_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t acpi_system_read_dsdt(struct file *, char __user *, size_t,
				     loff_t *);

static struct file_operations acpi_system_dsdt_ops = {
	.read = acpi_system_read_dsdt,
};

static ssize_t
acpi_system_read_dsdt(struct file *file,
		      char __user * buffer, size_t count, loff_t * ppos)
{
	acpi_status status = AE_OK;
	struct acpi_buffer dsdt = { ACPI_ALLOCATE_BUFFER, NULL };
	ssize_t res;

	ACPI_FUNCTION_TRACE("acpi_system_read_dsdt");

	status = acpi_get_table(ACPI_TABLE_DSDT, 1, &dsdt);
	if (ACPI_FAILURE(status))
		return_VALUE(-ENODEV);

	res = simple_read_from_buffer(buffer, count, ppos,
				      dsdt.pointer, dsdt.length);
	acpi_os_free(dsdt.pointer);

	return_VALUE(res);
}

static ssize_t acpi_system_read_fadt(struct file *, char __user *, size_t,
				     loff_t *);

static struct file_operations acpi_system_fadt_ops = {
	.read = acpi_system_read_fadt,
};

static ssize_t
acpi_system_read_fadt(struct file *file,
		      char __user * buffer, size_t count, loff_t * ppos)
{
	acpi_status status = AE_OK;
	struct acpi_buffer fadt = { ACPI_ALLOCATE_BUFFER, NULL };
	ssize_t res;

	ACPI_FUNCTION_TRACE("acpi_system_read_fadt");

	status = acpi_get_table(ACPI_TABLE_FADT, 1, &fadt);
	if (ACPI_FAILURE(status))
		return_VALUE(-ENODEV);

	res = simple_read_from_buffer(buffer, count, ppos,
				      fadt.pointer, fadt.length);
	acpi_os_free(fadt.pointer);

	return_VALUE(res);
}

static int __init acpi_system_init(void)
{
	struct proc_dir_entry *entry;
	int error = 0;
	char *name;

	ACPI_FUNCTION_TRACE("acpi_system_init");

	if (acpi_disabled)
		return_VALUE(0);

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
	return_VALUE(error);

      Error:
	ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			  "Unable to create '%s' proc fs entry\n", name));

	remove_proc_entry(ACPI_SYSTEM_FILE_FADT, acpi_root_dir);
	remove_proc_entry(ACPI_SYSTEM_FILE_DSDT, acpi_root_dir);
	remove_proc_entry(ACPI_SYSTEM_FILE_INFO, acpi_root_dir);

	error = -EFAULT;
	goto Done;
}

subsys_initcall(acpi_system_init);
