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
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/string.h>
#include <asm/uaccess.h>

#include <acpi/acpi_drivers.h>

#define PREFIX "ACPI: "

#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME("system");

#define ACPI_SYSTEM_CLASS		"system"
#define ACPI_SYSTEM_DEVICE_NAME		"System"

u32 acpi_irq_handled;
u32 acpi_irq_not_handled;

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
static struct kobject *tables_kobj;
static struct kobject *dynamic_tables_kobj;

struct acpi_table_attr {
	struct bin_attribute attr;
	char name[8];
	int instance;
	struct list_head node;
};

static ssize_t acpi_table_show(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *bin_attr, char *buf,
			       loff_t offset, size_t count)
{
	struct acpi_table_attr *table_attr =
	    container_of(bin_attr, struct acpi_table_attr, attr);
	struct acpi_table_header *table_header = NULL;
	acpi_status status;
	char name[ACPI_NAME_SIZE];

	if (strncmp(table_attr->name, "NULL", 4))
		memcpy(name, table_attr->name, ACPI_NAME_SIZE);
	else
		memcpy(name, "\0\0\0\0", 4);

	status =
	    acpi_get_table(name, table_attr->instance,
			   &table_header);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	return memory_read_from_buffer(buf, count, &offset,
					table_header, table_header->length);
}

static void acpi_table_attr_init(struct acpi_table_attr *table_attr,
				 struct acpi_table_header *table_header)
{
	struct acpi_table_header *header = NULL;
	struct acpi_table_attr *attr = NULL;

	sysfs_attr_init(&table_attr->attr.attr);
	if (table_header->signature[0] != '\0')
		memcpy(table_attr->name, table_header->signature,
			ACPI_NAME_SIZE);
	else
		memcpy(table_attr->name, "NULL", 4);

	list_for_each_entry(attr, &acpi_table_attr_list, node) {
		if (!memcmp(table_attr->name, attr->name, ACPI_NAME_SIZE))
			if (table_attr->instance < attr->instance)
				table_attr->instance = attr->instance;
	}
	table_attr->instance++;

	if (table_attr->instance > 1 || (table_attr->instance == 1 &&
					!acpi_get_table
					(table_header->signature, 2, &header)))
		sprintf(table_attr->name + ACPI_NAME_SIZE, "%d",
			table_attr->instance);

	table_attr->attr.size = 0;
	table_attr->attr.read = acpi_table_show;
	table_attr->attr.attr.name = table_attr->name;
	table_attr->attr.attr.mode = 0400;

	return;
}

static acpi_status
acpi_sysfs_table_handler(u32 event, void *table, void *context)
{
	struct acpi_table_attr *table_attr;

	switch (event) {
	case ACPI_TABLE_EVENT_LOAD:
		table_attr =
			kzalloc(sizeof(struct acpi_table_attr), GFP_KERNEL);
		if (!table_attr)
			return AE_NO_MEMORY;

		acpi_table_attr_init(table_attr, table);
		if (sysfs_create_bin_file(dynamic_tables_kobj,
					&table_attr->attr)) {
			kfree(table_attr);
			return AE_ERROR;
		} else
			list_add_tail(&table_attr->node,
					&acpi_table_attr_list);
		break;
	case ACPI_TABLE_EVENT_UNLOAD:
		/*
		 * we do not need to do anything right now
		 * because the table is not deleted from the
		 * global table list when unloading it.
		 */
		break;
	default:
		return AE_BAD_PARAMETER;
	}
	return AE_OK;
}

static int acpi_system_sysfs_init(void)
{
	struct acpi_table_attr *table_attr;
	struct acpi_table_header *table_header = NULL;
	int table_index = 0;
	int result;

	tables_kobj = kobject_create_and_add("tables", acpi_kobj);
	if (!tables_kobj)
		goto err;

	dynamic_tables_kobj = kobject_create_and_add("dynamic", tables_kobj);
	if (!dynamic_tables_kobj)
		goto err_dynamic_tables;

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
			    sysfs_create_bin_file(tables_kobj,
						  &table_attr->attr);
			if (result) {
				kfree(table_attr);
				return result;
			} else
				list_add_tail(&table_attr->node,
					      &acpi_table_attr_list);
		}
	} while (!result);
	kobject_uevent(tables_kobj, KOBJ_ADD);
	kobject_uevent(dynamic_tables_kobj, KOBJ_ADD);
	result = acpi_install_table_handler(acpi_sysfs_table_handler, NULL);

	return result == AE_OK ? 0 : -EINVAL;
err_dynamic_tables:
	kobject_put(tables_kobj);
err:
	return -ENOMEM;
}

/*
 * Detailed ACPI IRQ counters in /sys/firmware/acpi/interrupts/
 * See Documentation/ABI/testing/sysfs-firmware-acpi
 */

#define COUNT_GPE 0
#define COUNT_SCI 1	/* acpi_irq_handled */
#define COUNT_SCI_NOT 2	/* acpi_irq_not_handled */
#define COUNT_ERROR 3	/* other */
#define NUM_COUNTERS_EXTRA 4

struct event_counter {
	u32 count;
	u32 flags;
};

static struct event_counter *all_counters;
static u32 num_gpes;
static u32 num_counters;
static struct attribute **all_attrs;
static u32 acpi_gpe_count;

static struct attribute_group interrupt_stats_attr_group = {
	.name = "interrupts",
};
static struct kobj_attribute *counter_attrs;

static void delete_gpe_attr_array(void)
{
	struct event_counter *tmp = all_counters;

	all_counters = NULL;
	kfree(tmp);

	if (counter_attrs) {
		int i;

		for (i = 0; i < num_gpes; i++)
			kfree(counter_attrs[i].attr.name);

		kfree(counter_attrs);
	}
	kfree(all_attrs);

	return;
}

void acpi_os_gpe_count(u32 gpe_number)
{
	acpi_gpe_count++;

	if (!all_counters)
		return;

	if (gpe_number < num_gpes)
		all_counters[gpe_number].count++;
	else
		all_counters[num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_ERROR].
					count++;

	return;
}

void acpi_os_fixed_event_count(u32 event_number)
{
	if (!all_counters)
		return;

	if (event_number < ACPI_NUM_FIXED_EVENTS)
		all_counters[num_gpes + event_number].count++;
	else
		all_counters[num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_ERROR].
				count++;

	return;
}

static int get_status(u32 index, acpi_event_status *status, acpi_handle *handle)
{
	int result = 0;

	if (index >= num_gpes + ACPI_NUM_FIXED_EVENTS)
		goto end;

	if (index < num_gpes) {
		result = acpi_get_gpe_device(index, handle);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_NOT_FOUND,
				"Invalid GPE 0x%x\n", index));
			goto end;
		}
		result = acpi_get_gpe_status(*handle, index, status);
	} else if (index < (num_gpes + ACPI_NUM_FIXED_EVENTS))
		result = acpi_get_event_status(index - num_gpes, status);

end:
	return result;
}

static ssize_t counter_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int index = attr - counter_attrs;
	int size;
	acpi_handle handle;
	acpi_event_status status;
	int result = 0;

	all_counters[num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_SCI].count =
		acpi_irq_handled;
	all_counters[num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_SCI_NOT].count =
		acpi_irq_not_handled;
	all_counters[num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_GPE].count =
		acpi_gpe_count;

	size = sprintf(buf, "%8d", all_counters[index].count);

	/* "gpe_all" or "sci" */
	if (index >= num_gpes + ACPI_NUM_FIXED_EVENTS)
		goto end;

	result = get_status(index, &status, &handle);
	if (result)
		goto end;

	if (!(status & ACPI_EVENT_FLAG_HANDLE))
		size += sprintf(buf + size, "	invalid");
	else if (status & ACPI_EVENT_FLAG_ENABLED)
		size += sprintf(buf + size, "	enabled");
	else if (status & ACPI_EVENT_FLAG_WAKE_ENABLED)
		size += sprintf(buf + size, "	wake_enabled");
	else
		size += sprintf(buf + size, "	disabled");

end:
	size += sprintf(buf + size, "\n");
	return result ? result : size;
}

/*
 * counter_set() sets the specified counter.
 * setting the total "sci" file to any value clears all counters.
 * enable/disable/clear a gpe/fixed event in user space.
 */
static ssize_t counter_set(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t size)
{
	int index = attr - counter_attrs;
	acpi_event_status status;
	acpi_handle handle;
	int result = 0;

	if (index == num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_SCI) {
		int i;
		for (i = 0; i < num_counters; ++i)
			all_counters[i].count = 0;
		acpi_gpe_count = 0;
		acpi_irq_handled = 0;
		acpi_irq_not_handled = 0;
		goto end;
	}

	/* show the event status for both GPEs and Fixed Events */
	result = get_status(index, &status, &handle);
	if (result)
		goto end;

	if (!(status & ACPI_EVENT_FLAG_HANDLE)) {
		printk(KERN_WARNING PREFIX
			"Can not change Invalid GPE/Fixed Event status\n");
		return -EINVAL;
	}

	if (index < num_gpes) {
		if (!strcmp(buf, "disable\n") &&
				(status & ACPI_EVENT_FLAG_ENABLED))
			result = acpi_disable_gpe(handle, index,
						ACPI_GPE_TYPE_RUNTIME);
		else if (!strcmp(buf, "enable\n") &&
				!(status & ACPI_EVENT_FLAG_ENABLED))
			result = acpi_enable_gpe(handle, index,
						ACPI_GPE_TYPE_RUNTIME);
		else if (!strcmp(buf, "clear\n") &&
				(status & ACPI_EVENT_FLAG_SET))
			result = acpi_clear_gpe(handle, index);
		else
			all_counters[index].count = strtoul(buf, NULL, 0);
	} else if (index < num_gpes + ACPI_NUM_FIXED_EVENTS) {
		int event = index - num_gpes;
		if (!strcmp(buf, "disable\n") &&
				(status & ACPI_EVENT_FLAG_ENABLED))
			result = acpi_disable_event(event, ACPI_NOT_ISR);
		else if (!strcmp(buf, "enable\n") &&
				!(status & ACPI_EVENT_FLAG_ENABLED))
			result = acpi_enable_event(event, ACPI_NOT_ISR);
		else if (!strcmp(buf, "clear\n") &&
				(status & ACPI_EVENT_FLAG_SET))
			result = acpi_clear_event(event);
		else
			all_counters[index].count = strtoul(buf, NULL, 0);
	} else
		all_counters[index].count = strtoul(buf, NULL, 0);

	if (ACPI_FAILURE(result))
		result = -EINVAL;
end:
	return result ? result : size;
}

void acpi_irq_stats_init(void)
{
	int i;

	if (all_counters)
		return;

	num_gpes = acpi_current_gpe_count;
	num_counters = num_gpes + ACPI_NUM_FIXED_EVENTS + NUM_COUNTERS_EXTRA;

	all_attrs = kzalloc(sizeof(struct attribute *) * (num_counters + 1),
			GFP_KERNEL);
	if (all_attrs == NULL)
		return;

	all_counters = kzalloc(sizeof(struct event_counter) * (num_counters),
				GFP_KERNEL);
	if (all_counters == NULL)
		goto fail;

	counter_attrs = kzalloc(sizeof(struct kobj_attribute) * (num_counters),
			GFP_KERNEL);
	if (counter_attrs == NULL)
		goto fail;

	for (i = 0; i < num_counters; ++i) {
		char buffer[12];
		char *name;

		if (i < num_gpes)
			sprintf(buffer, "gpe%02X", i);
		else if (i == num_gpes + ACPI_EVENT_PMTIMER)
			sprintf(buffer, "ff_pmtimer");
		else if (i == num_gpes + ACPI_EVENT_GLOBAL)
			sprintf(buffer, "ff_gbl_lock");
		else if (i == num_gpes + ACPI_EVENT_POWER_BUTTON)
			sprintf(buffer, "ff_pwr_btn");
		else if (i == num_gpes + ACPI_EVENT_SLEEP_BUTTON)
			sprintf(buffer, "ff_slp_btn");
		else if (i == num_gpes + ACPI_EVENT_RTC)
			sprintf(buffer, "ff_rt_clk");
		else if (i == num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_GPE)
			sprintf(buffer, "gpe_all");
		else if (i == num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_SCI)
			sprintf(buffer, "sci");
		else if (i == num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_SCI_NOT)
			sprintf(buffer, "sci_not");
		else if (i == num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_ERROR)
			sprintf(buffer, "error");
		else
			sprintf(buffer, "bug%02X", i);

		name = kzalloc(strlen(buffer) + 1, GFP_KERNEL);
		if (name == NULL)
			goto fail;
		strncpy(name, buffer, strlen(buffer) + 1);

		sysfs_attr_init(&counter_attrs[i].attr);
		counter_attrs[i].attr.name = name;
		counter_attrs[i].attr.mode = 0644;
		counter_attrs[i].show = counter_show;
		counter_attrs[i].store = counter_set;

		all_attrs[i] = &counter_attrs[i].attr;
	}

	interrupt_stats_attr_group.attrs = all_attrs;
	if (!sysfs_create_group(acpi_kobj, &interrupt_stats_attr_group))
		return;

fail:
	delete_gpe_attr_array();
	return;
}

static void __exit interrupt_stats_exit(void)
{
	sysfs_remove_group(acpi_kobj, &interrupt_stats_attr_group);

	delete_gpe_attr_array();

	return;
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
	.owner = THIS_MODULE,
	.open = acpi_system_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t acpi_system_read_dsdt(struct file *, char __user *, size_t,
				     loff_t *);

static const struct file_operations acpi_system_dsdt_ops = {
	.owner = THIS_MODULE,
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
	.owner = THIS_MODULE,
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

	/* 'info' [R] */
	entry = proc_create(ACPI_SYSTEM_FILE_INFO, S_IRUGO, acpi_root_dir,
			    &acpi_system_info_ops);
	if (!entry)
		goto Error;

	/* 'dsdt' [R] */
	entry = proc_create(ACPI_SYSTEM_FILE_DSDT, S_IRUSR, acpi_root_dir,
			    &acpi_system_dsdt_ops);
	if (!entry)
		goto Error;

	/* 'fadt' [R] */
	entry = proc_create(ACPI_SYSTEM_FILE_FADT, S_IRUSR, acpi_root_dir,
			    &acpi_system_fadt_ops);
	if (!entry)
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

int __init acpi_system_init(void)
{
	int result;

	result = acpi_system_procfs_init();
	if (result)
		return result;

	result = acpi_system_sysfs_init();

	return result;
}
