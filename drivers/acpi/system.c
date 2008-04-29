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

u32 acpi_irq_handled;

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

	tables_kobj = kobject_create_and_add("tables", acpi_kobj);
	if (!tables_kobj)
		return -ENOMEM;

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

	return 0;
}

/*
 * Detailed ACPI IRQ counters in /sys/firmware/acpi/interrupts/
 * See Documentation/ABI/testing/sysfs-firmware-acpi
 */

#define COUNT_GPE 0
#define COUNT_SCI 1	/* acpi_irq_handled */
#define COUNT_ERROR 2	/* other */
#define NUM_COUNTERS_EXTRA 3

static u32 *all_counters;
static u32 num_gpes;
static u32 num_counters;
static struct attribute **all_attrs;
static u32 acpi_gpe_count;

static struct attribute_group interrupt_stats_attr_group = {
	.name = "interrupts",
};
static struct kobj_attribute *counter_attrs;

static int count_num_gpes(void)
{
	int count = 0;
	struct acpi_gpe_xrupt_info *gpe_xrupt_info;
	struct acpi_gpe_block_info *gpe_block;
	acpi_cpu_flags flags;

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	gpe_xrupt_info = acpi_gbl_gpe_xrupt_list_head;
	while (gpe_xrupt_info) {
		gpe_block = gpe_xrupt_info->gpe_block_list_head;
		while (gpe_block) {
			count += gpe_block->register_count *
			    ACPI_GPE_REGISTER_WIDTH;
			gpe_block = gpe_block->next;
		}
		gpe_xrupt_info = gpe_xrupt_info->next;
	}
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);

	return count;
}

static void delete_gpe_attr_array(void)
{
	u32 *tmp = all_counters;

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
		all_counters[gpe_number]++;
	else
		all_counters[num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_ERROR]++;

	return;
}

void acpi_os_fixed_event_count(u32 event_number)
{
	if (!all_counters)
		return;

	if (event_number < ACPI_NUM_FIXED_EVENTS)
		all_counters[num_gpes + event_number]++;
	else
		all_counters[num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_ERROR]++;

	return;
}

static ssize_t counter_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	all_counters[num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_SCI] =
		acpi_irq_handled;
	all_counters[num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_GPE] =
		acpi_gpe_count;

	return sprintf(buf, "%d\n", all_counters[attr - counter_attrs]);
}

/*
 * counter_set() sets the specified counter.
 * setting the total "sci" file to any value clears all counters.
 */
static ssize_t counter_set(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t size)
{
	int index = attr - counter_attrs;

	if (index == num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_SCI) {
		int i;
		for (i = 0; i < num_counters; ++i)
			all_counters[i] = 0;
		acpi_gpe_count = 0;
		acpi_irq_handled = 0;

	} else
		all_counters[index] = strtoul(buf, NULL, 0);

	return size;
}

void acpi_irq_stats_init(void)
{
	int i;

	if (all_counters)
		return;

	num_gpes = count_num_gpes();
	num_counters = num_gpes + ACPI_NUM_FIXED_EVENTS + NUM_COUNTERS_EXTRA;

	all_attrs = kzalloc(sizeof(struct attribute *) * (num_counters + 1),
			GFP_KERNEL);
	if (all_attrs == NULL)
		return;

	all_counters = kzalloc(sizeof(u32) * (num_counters), GFP_KERNEL);
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
		else if (i == num_gpes + ACPI_NUM_FIXED_EVENTS + COUNT_ERROR)
			sprintf(buffer, "error");
		else
			sprintf(buffer, "bug%02X", i);

		name = kzalloc(strlen(buffer) + 1, GFP_KERNEL);
		if (name == NULL)
			goto fail;
		strncpy(name, buffer, strlen(buffer) + 1);

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
