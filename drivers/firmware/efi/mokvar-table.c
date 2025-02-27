// SPDX-License-Identifier: GPL-2.0
/*
 * mokvar-table.c
 *
 * Copyright (c) 2020 Red Hat
 * Author: Lenny Szubowicz <lszubowi@redhat.com>
 *
 * This module contains the kernel support for the Linux EFI Machine
 * Owner Key (MOK) variable configuration table, which is identified by
 * the LINUX_EFI_MOK_VARIABLE_TABLE_GUID.
 *
 * This EFI configuration table provides a more robust alternative to
 * EFI volatile variables by which an EFI boot loader can pass the
 * contents of the Machine Owner Key (MOK) certificate stores to the
 * kernel during boot. If both the EFI MOK config table and corresponding
 * EFI MOK variables are present, the table should be considered as
 * more authoritative.
 *
 * This module includes code that validates and maps the EFI MOK table,
 * if it's presence was detected very early in boot.
 *
 * Kernel interface routines are provided to walk through all the
 * entries in the MOK config table or to search for a specific named
 * entry.
 *
 * The contents of the individual named MOK config table entries are
 * made available to user space via read-only sysfs binary files under:
 *
 * /sys/firmware/efi/mok-variables/
 *
 */
#define pr_fmt(fmt) "mokvar: " fmt

#include <linux/capability.h>
#include <linux/efi.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/slab.h>

#include <asm/early_ioremap.h>

/*
 * The LINUX_EFI_MOK_VARIABLE_TABLE_GUID config table is a packed
 * sequence of struct efi_mokvar_table_entry, one for each named
 * MOK variable. The sequence is terminated by an entry with a
 * completely NULL name and 0 data size.
 *
 * efi_mokvar_table_size is set to the computed size of the
 * MOK config table by efi_mokvar_table_init(). This will be
 * non-zero if and only if the table if present and has been
 * validated by efi_mokvar_table_init().
 */
static size_t efi_mokvar_table_size;

/*
 * efi_mokvar_table_va is the kernel virtual address at which the
 * EFI MOK config table has been mapped by efi_mokvar_sysfs_init().
 */
static struct efi_mokvar_table_entry *efi_mokvar_table_va;

/*
 * Each /sys/firmware/efi/mok-variables/ sysfs file is represented by
 * an instance of struct efi_mokvar_sysfs_attr on efi_mokvar_sysfs_list.
 * bin_attr.private points to the associated EFI MOK config table entry.
 *
 * This list is created during boot and then remains unchanged.
 * So no synchronization is currently required to walk the list.
 */
struct efi_mokvar_sysfs_attr {
	struct bin_attribute bin_attr;
	struct list_head node;
};

static LIST_HEAD(efi_mokvar_sysfs_list);
static struct kobject *mokvar_kobj;

/*
 * efi_mokvar_table_init() - Early boot validation of EFI MOK config table
 *
 * If present, validate and compute the size of the EFI MOK variable
 * configuration table. This table may be provided by an EFI boot loader
 * as an alternative to ordinary EFI variables, due to platform-dependent
 * limitations. The memory occupied by this table is marked as reserved.
 *
 * This routine must be called before efi_free_boot_services() in order
 * to guarantee that it can mark the table as reserved.
 *
 * Implicit inputs:
 * efi.mokvar_table:	Physical address of EFI MOK variable config table
 *			or special value that indicates no such table.
 *
 * Implicit outputs:
 * efi_mokvar_table_size: Computed size of EFI MOK variable config table.
 *			The table is considered present and valid if this
 *			is non-zero.
 */
void __init efi_mokvar_table_init(void)
{
	struct efi_mokvar_table_entry __aligned(1) *mokvar_entry, *next_entry;
	efi_memory_desc_t md;
	void *va = NULL;
	unsigned long cur_offset = 0;
	unsigned long offset_limit;
	unsigned long map_size_needed = 0;
	unsigned long size;
	int err;

	if (!efi_enabled(EFI_MEMMAP))
		return;

	if (efi.mokvar_table == EFI_INVALID_TABLE_ADDR)
		return;
	/*
	 * The EFI MOK config table must fit within a single EFI memory
	 * descriptor range.
	 */
	err = efi_mem_desc_lookup(efi.mokvar_table, &md);
	if (err) {
		pr_warn("EFI MOKvar config table is not within the EFI memory map\n");
		return;
	}

	offset_limit = efi_mem_desc_end(&md) - efi.mokvar_table;

	/*
	 * Validate the MOK config table. Since there is no table header
	 * from which we could get the total size of the MOK config table,
	 * we compute the total size as we validate each variably sized
	 * entry, remapping as necessary.
	 */
	err = -EINVAL;
	while (cur_offset + sizeof(*mokvar_entry) <= offset_limit) {
		if (va)
			early_memunmap(va, sizeof(*mokvar_entry));
		va = early_memremap(efi.mokvar_table + cur_offset, sizeof(*mokvar_entry));
		if (!va) {
			pr_err("Failed to map EFI MOKvar config table pa=0x%lx, size=%zu.\n",
			       efi.mokvar_table + cur_offset, sizeof(*mokvar_entry));
			return;
		}
		mokvar_entry = va;
next:
		/* Check for last sentinel entry */
		if (mokvar_entry->name[0] == '\0') {
			if (mokvar_entry->data_size != 0)
				break;
			err = 0;
			map_size_needed = cur_offset + sizeof(*mokvar_entry);
			break;
		}

		/* Enforce that the name is NUL terminated */
		mokvar_entry->name[sizeof(mokvar_entry->name) - 1] = '\0';

		/* Advance to the next entry */
		size = sizeof(*mokvar_entry) + mokvar_entry->data_size;
		cur_offset += size;

		/*
		 * Don't bother remapping if the current entry header and the
		 * next one end on the same page.
		 */
		next_entry = (void *)((unsigned long)mokvar_entry + size);
		if (((((unsigned long)(mokvar_entry + 1) - 1) ^
		      ((unsigned long)(next_entry + 1) - 1)) & PAGE_MASK) == 0) {
			mokvar_entry = next_entry;
			goto next;
		}
	}

	if (va)
		early_memunmap(va, sizeof(*mokvar_entry));
	if (err) {
		pr_err("EFI MOKvar config table is not valid\n");
		return;
	}

	if (md.type == EFI_BOOT_SERVICES_DATA)
		efi_mem_reserve(efi.mokvar_table, map_size_needed);

	efi_mokvar_table_size = map_size_needed;
}

/*
 * efi_mokvar_entry_next() - Get next entry in the EFI MOK config table
 *
 * mokvar_entry:	Pointer to current EFI MOK config table entry
 *			or null. Null indicates get first entry.
 *			Passed by reference. This is updated to the
 *			same value as the return value.
 *
 * Returns:		Pointer to next EFI MOK config table entry
 *			or null, if there are no more entries.
 *			Same value is returned in the mokvar_entry
 *			parameter.
 *
 * This routine depends on the EFI MOK config table being entirely
 * mapped with it's starting virtual address in efi_mokvar_table_va.
 */
struct efi_mokvar_table_entry *efi_mokvar_entry_next(
			struct efi_mokvar_table_entry **mokvar_entry)
{
	struct efi_mokvar_table_entry *mokvar_cur;
	struct efi_mokvar_table_entry *mokvar_next;
	size_t size_cur;

	mokvar_cur = *mokvar_entry;
	*mokvar_entry = NULL;

	if (efi_mokvar_table_va == NULL)
		return NULL;

	if (mokvar_cur == NULL) {
		mokvar_next = efi_mokvar_table_va;
	} else {
		if (mokvar_cur->name[0] == '\0')
			return NULL;
		size_cur = sizeof(*mokvar_cur) + mokvar_cur->data_size;
		mokvar_next = (void *)mokvar_cur + size_cur;
	}

	if (mokvar_next->name[0] == '\0')
		return NULL;

	*mokvar_entry = mokvar_next;
	return mokvar_next;
}

/*
 * efi_mokvar_entry_find() - Find EFI MOK config entry by name
 *
 * name:	Name of the entry to look for.
 *
 * Returns:	Pointer to EFI MOK config table entry if found;
 *		null otherwise.
 *
 * This routine depends on the EFI MOK config table being entirely
 * mapped with it's starting virtual address in efi_mokvar_table_va.
 */
struct efi_mokvar_table_entry *efi_mokvar_entry_find(const char *name)
{
	struct efi_mokvar_table_entry *mokvar_entry = NULL;

	while (efi_mokvar_entry_next(&mokvar_entry)) {
		if (!strncmp(name, mokvar_entry->name,
			     sizeof(mokvar_entry->name)))
			return mokvar_entry;
	}
	return NULL;
}

/*
 * efi_mokvar_sysfs_read() - sysfs binary file read routine
 *
 * Returns:	Count of bytes read.
 *
 * Copy EFI MOK config table entry data for this mokvar sysfs binary file
 * to the supplied buffer, starting at the specified offset into mokvar table
 * entry data, for the specified count bytes. The copy is limited by the
 * amount of data in this mokvar config table entry.
 */
static ssize_t efi_mokvar_sysfs_read(struct file *file, struct kobject *kobj,
				 struct bin_attribute *bin_attr, char *buf,
				 loff_t off, size_t count)
{
	struct efi_mokvar_table_entry *mokvar_entry = bin_attr->private;

	if (!capable(CAP_SYS_ADMIN))
		return 0;

	if (off >= mokvar_entry->data_size)
		return 0;
	if (count >  mokvar_entry->data_size - off)
		count = mokvar_entry->data_size - off;

	memcpy(buf, mokvar_entry->data + off, count);
	return count;
}

/*
 * efi_mokvar_sysfs_init() - Map EFI MOK config table and create sysfs
 *
 * Map the EFI MOK variable config table for run-time use by the kernel
 * and create the sysfs entries in /sys/firmware/efi/mok-variables/
 *
 * This routine just returns if a valid EFI MOK variable config table
 * was not found earlier during boot.
 *
 * This routine must be called during a "middle" initcall phase, i.e.
 * after efi_mokvar_table_init() but before UEFI certs are loaded
 * during late init.
 *
 * Implicit inputs:
 * efi.mokvar_table:	Physical address of EFI MOK variable config table
 *			or special value that indicates no such table.
 *
 * efi_mokvar_table_size: Computed size of EFI MOK variable config table.
 *			The table is considered present and valid if this
 *			is non-zero.
 *
 * Implicit outputs:
 * efi_mokvar_table_va:	Start virtual address of the EFI MOK config table.
 */
static int __init efi_mokvar_sysfs_init(void)
{
	void *config_va;
	struct efi_mokvar_table_entry *mokvar_entry = NULL;
	struct efi_mokvar_sysfs_attr *mokvar_sysfs = NULL;
	int err = 0;

	if (efi_mokvar_table_size == 0)
		return -ENOENT;

	config_va = memremap(efi.mokvar_table, efi_mokvar_table_size,
			     MEMREMAP_WB);
	if (!config_va) {
		pr_err("Failed to map EFI MOKvar config table\n");
		return -ENOMEM;
	}
	efi_mokvar_table_va = config_va;

	mokvar_kobj = kobject_create_and_add("mok-variables", efi_kobj);
	if (!mokvar_kobj) {
		pr_err("Failed to create EFI mok-variables sysfs entry\n");
		return -ENOMEM;
	}

	while (efi_mokvar_entry_next(&mokvar_entry)) {
		mokvar_sysfs = kzalloc(sizeof(*mokvar_sysfs), GFP_KERNEL);
		if (!mokvar_sysfs) {
			err = -ENOMEM;
			break;
		}

		sysfs_bin_attr_init(&mokvar_sysfs->bin_attr);
		mokvar_sysfs->bin_attr.private = mokvar_entry;
		mokvar_sysfs->bin_attr.attr.name = mokvar_entry->name;
		mokvar_sysfs->bin_attr.attr.mode = 0400;
		mokvar_sysfs->bin_attr.size = mokvar_entry->data_size;
		mokvar_sysfs->bin_attr.read = efi_mokvar_sysfs_read;

		err = sysfs_create_bin_file(mokvar_kobj,
					   &mokvar_sysfs->bin_attr);
		if (err)
			break;

		list_add_tail(&mokvar_sysfs->node, &efi_mokvar_sysfs_list);
	}

	if (err) {
		pr_err("Failed to create some EFI mok-variables sysfs entries\n");
		kfree(mokvar_sysfs);
	}
	return err;
}
fs_initcall(efi_mokvar_sysfs_init);
