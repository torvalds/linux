/*
 * linux/drivers/firmware/memmap.c
 *  Copyright (C) 2008 SUSE LINUX Products GmbH
 *  by Bernhard Walle <bernhard.walle@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/string.h>
#include <linux/firmware-map.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/bootmem.h>
#include <linux/slab.h>

/*
 * Data types ------------------------------------------------------------------
 */

/*
 * Firmware map entry. Because firmware memory maps are flat and not
 * hierarchical, it's ok to organise them in a linked list. No parent
 * information is necessary as for the resource tree.
 */
struct firmware_map_entry {
	/*
	 * start and end must be u64 rather than resource_size_t, because e820
	 * resources can lie at addresses above 4G.
	 */
	u64			start;	/* start of the memory range */
	u64			end;	/* end of the memory range (incl.) */
	const char		*type;	/* type of the memory range */
	struct list_head	list;	/* entry for the linked list */
	struct kobject		kobj;   /* kobject for each entry */
};

/*
 * Forward declarations --------------------------------------------------------
 */
static ssize_t memmap_attr_show(struct kobject *kobj,
				struct attribute *attr, char *buf);
static ssize_t start_show(struct firmware_map_entry *entry, char *buf);
static ssize_t end_show(struct firmware_map_entry *entry, char *buf);
static ssize_t type_show(struct firmware_map_entry *entry, char *buf);

/*
 * Static data -----------------------------------------------------------------
 */

struct memmap_attribute {
	struct attribute attr;
	ssize_t (*show)(struct firmware_map_entry *entry, char *buf);
};

static struct memmap_attribute memmap_start_attr = __ATTR_RO(start);
static struct memmap_attribute memmap_end_attr   = __ATTR_RO(end);
static struct memmap_attribute memmap_type_attr  = __ATTR_RO(type);

/*
 * These are default attributes that are added for every memmap entry.
 */
static struct attribute *def_attrs[] = {
	&memmap_start_attr.attr,
	&memmap_end_attr.attr,
	&memmap_type_attr.attr,
	NULL
};

static const struct sysfs_ops memmap_attr_ops = {
	.show = memmap_attr_show,
};

static struct kobj_type memmap_ktype = {
	.sysfs_ops	= &memmap_attr_ops,
	.default_attrs	= def_attrs,
};

/*
 * Registration functions ------------------------------------------------------
 */

/*
 * Firmware memory map entries. No locking is needed because the
 * firmware_map_add() and firmware_map_add_early() functions are called
 * in firmware initialisation code in one single thread of execution.
 */
static LIST_HEAD(map_entries);

/**
 * firmware_map_add_entry() - Does the real work to add a firmware memmap entry.
 * @start: Start of the memory range.
 * @end:   End of the memory range (exclusive).
 * @type:  Type of the memory range.
 * @entry: Pre-allocated (either kmalloc() or bootmem allocator), uninitialised
 *         entry.
 *
 * Common implementation of firmware_map_add() and firmware_map_add_early()
 * which expects a pre-allocated struct firmware_map_entry.
 **/
static int firmware_map_add_entry(u64 start, u64 end,
				  const char *type,
				  struct firmware_map_entry *entry)
{
	BUG_ON(start > end);

	entry->start = start;
	entry->end = end - 1;
	entry->type = type;
	INIT_LIST_HEAD(&entry->list);
	kobject_init(&entry->kobj, &memmap_ktype);

	list_add_tail(&entry->list, &map_entries);

	return 0;
}

/*
 * Add memmap entry on sysfs
 */
static int add_sysfs_fw_map_entry(struct firmware_map_entry *entry)
{
	static int map_entries_nr;
	static struct kset *mmap_kset;

	if (!mmap_kset) {
		mmap_kset = kset_create_and_add("memmap", NULL, firmware_kobj);
		if (!mmap_kset)
			return -ENOMEM;
	}

	entry->kobj.kset = mmap_kset;
	if (kobject_add(&entry->kobj, NULL, "%d", map_entries_nr++))
		kobject_put(&entry->kobj);

	return 0;
}

/**
 * firmware_map_add_hotplug() - Adds a firmware mapping entry when we do
 * memory hotplug.
 * @start: Start of the memory range.
 * @end:   End of the memory range (exclusive)
 * @type:  Type of the memory range.
 *
 * Adds a firmware mapping entry. This function is for memory hotplug, it is
 * similar to function firmware_map_add_early(). The only difference is that
 * it will create the syfs entry dynamically.
 *
 * Returns 0 on success, or -ENOMEM if no memory could be allocated.
 **/
int __meminit firmware_map_add_hotplug(u64 start, u64 end, const char *type)
{
	struct firmware_map_entry *entry;

	entry = kzalloc(sizeof(struct firmware_map_entry), GFP_ATOMIC);
	if (!entry)
		return -ENOMEM;

	firmware_map_add_entry(start, end, type, entry);
	/* create the memmap entry */
	add_sysfs_fw_map_entry(entry);

	return 0;
}

/**
 * firmware_map_add_early() - Adds a firmware mapping entry.
 * @start: Start of the memory range.
 * @end:   End of the memory range.
 * @type:  Type of the memory range.
 *
 * Adds a firmware mapping entry. This function uses the bootmem allocator
 * for memory allocation.
 *
 * That function must be called before late_initcall.
 *
 * Returns 0 on success, or -ENOMEM if no memory could be allocated.
 **/
int __init firmware_map_add_early(u64 start, u64 end, const char *type)
{
	struct firmware_map_entry *entry;

	entry = alloc_bootmem(sizeof(struct firmware_map_entry));
	if (WARN_ON(!entry))
		return -ENOMEM;

	return firmware_map_add_entry(start, end, type, entry);
}

/*
 * Sysfs functions -------------------------------------------------------------
 */

static ssize_t start_show(struct firmware_map_entry *entry, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%llx\n",
		(unsigned long long)entry->start);
}

static ssize_t end_show(struct firmware_map_entry *entry, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%llx\n",
		(unsigned long long)entry->end);
}

static ssize_t type_show(struct firmware_map_entry *entry, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", entry->type);
}

#define to_memmap_attr(_attr) container_of(_attr, struct memmap_attribute, attr)
#define to_memmap_entry(obj) container_of(obj, struct firmware_map_entry, kobj)

static ssize_t memmap_attr_show(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
	struct firmware_map_entry *entry = to_memmap_entry(kobj);
	struct memmap_attribute *memmap_attr = to_memmap_attr(attr);

	return memmap_attr->show(entry, buf);
}

/*
 * Initialises stuff and adds the entries in the map_entries list to
 * sysfs. Important is that firmware_map_add() and firmware_map_add_early()
 * must be called before late_initcall. That's just because that function
 * is called as late_initcall() function, which means that if you call
 * firmware_map_add() or firmware_map_add_early() afterwards, the entries
 * are not added to sysfs.
 */
static int __init firmware_memmap_init(void)
{
	struct firmware_map_entry *entry;

	list_for_each_entry(entry, &map_entries, list)
		add_sysfs_fw_map_entry(entry);

	return 0;
}
late_initcall(firmware_memmap_init);

