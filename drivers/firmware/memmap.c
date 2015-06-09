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
#include <linux/mm.h>

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

static struct firmware_map_entry * __meminit
firmware_map_find_entry(u64 start, u64 end, const char *type);

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

/* Firmware memory map entries. */
static LIST_HEAD(map_entries);
static DEFINE_SPINLOCK(map_entries_lock);

/*
 * For memory hotplug, there is no way to free memory map entries allocated
 * by boot mem after the system is up. So when we hot-remove memory whose
 * map entry is allocated by bootmem, we need to remember the storage and
 * reuse it when the memory is hot-added again.
 */
static LIST_HEAD(map_entries_bootmem);
static DEFINE_SPINLOCK(map_entries_bootmem_lock);


static inline struct firmware_map_entry *
to_memmap_entry(struct kobject *kobj)
{
	return container_of(kobj, struct firmware_map_entry, kobj);
}

static void __meminit release_firmware_map_entry(struct kobject *kobj)
{
	struct firmware_map_entry *entry = to_memmap_entry(kobj);

	if (PageReserved(virt_to_page(entry))) {
		/*
		 * Remember the storage allocated by bootmem, and reuse it when
		 * the memory is hot-added again. The entry will be added to
		 * map_entries_bootmem here, and deleted from &map_entries in
		 * firmware_map_remove_entry().
		 */
		spin_lock(&map_entries_bootmem_lock);
		list_add(&entry->list, &map_entries_bootmem);
		spin_unlock(&map_entries_bootmem_lock);

		return;
	}

	kfree(entry);
}

static struct kobj_type __refdata memmap_ktype = {
	.release	= release_firmware_map_entry,
	.sysfs_ops	= &memmap_attr_ops,
	.default_attrs	= def_attrs,
};

/*
 * Registration functions ------------------------------------------------------
 */

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

	spin_lock(&map_entries_lock);
	list_add_tail(&entry->list, &map_entries);
	spin_unlock(&map_entries_lock);

	return 0;
}

/**
 * firmware_map_remove_entry() - Does the real work to remove a firmware
 * memmap entry.
 * @entry: removed entry.
 *
 * The caller must hold map_entries_lock, and release it properly.
 **/
static inline void firmware_map_remove_entry(struct firmware_map_entry *entry)
{
	list_del(&entry->list);
}

/*
 * Add memmap entry on sysfs
 */
static int add_sysfs_fw_map_entry(struct firmware_map_entry *entry)
{
	static int map_entries_nr;
	static struct kset *mmap_kset;

	if (entry->kobj.state_in_sysfs)
		return -EEXIST;

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

/*
 * Remove memmap entry on sysfs
 */
static inline void remove_sysfs_fw_map_entry(struct firmware_map_entry *entry)
{
	kobject_put(&entry->kobj);
}

/*
 * firmware_map_find_entry_in_list() - Search memmap entry in a given list.
 * @start: Start of the memory range.
 * @end:   End of the memory range (exclusive).
 * @type:  Type of the memory range.
 * @list:  In which to find the entry.
 *
 * This function is to find the memmap entey of a given memory range in a
 * given list. The caller must hold map_entries_lock, and must not release
 * the lock until the processing of the returned entry has completed.
 *
 * Return: Pointer to the entry to be found on success, or NULL on failure.
 */
static struct firmware_map_entry * __meminit
firmware_map_find_entry_in_list(u64 start, u64 end, const char *type,
				struct list_head *list)
{
	struct firmware_map_entry *entry;

	list_for_each_entry(entry, list, list)
		if ((entry->start == start) && (entry->end == end) &&
		    (!strcmp(entry->type, type))) {
			return entry;
		}

	return NULL;
}

/*
 * firmware_map_find_entry() - Search memmap entry in map_entries.
 * @start: Start of the memory range.
 * @end:   End of the memory range (exclusive).
 * @type:  Type of the memory range.
 *
 * This function is to find the memmap entey of a given memory range.
 * The caller must hold map_entries_lock, and must not release the lock
 * until the processing of the returned entry has completed.
 *
 * Return: Pointer to the entry to be found on success, or NULL on failure.
 */
static struct firmware_map_entry * __meminit
firmware_map_find_entry(u64 start, u64 end, const char *type)
{
	return firmware_map_find_entry_in_list(start, end, type, &map_entries);
}

/*
 * firmware_map_find_entry_bootmem() - Search memmap entry in map_entries_bootmem.
 * @start: Start of the memory range.
 * @end:   End of the memory range (exclusive).
 * @type:  Type of the memory range.
 *
 * This function is similar to firmware_map_find_entry except that it find the
 * given entry in map_entries_bootmem.
 *
 * Return: Pointer to the entry to be found on success, or NULL on failure.
 */
static struct firmware_map_entry * __meminit
firmware_map_find_entry_bootmem(u64 start, u64 end, const char *type)
{
	return firmware_map_find_entry_in_list(start, end, type,
					       &map_entries_bootmem);
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

	entry = firmware_map_find_entry(start, end - 1, type);
	if (entry)
		return 0;

	entry = firmware_map_find_entry_bootmem(start, end - 1, type);
	if (!entry) {
		entry = kzalloc(sizeof(struct firmware_map_entry), GFP_ATOMIC);
		if (!entry)
			return -ENOMEM;
	} else {
		/* Reuse storage allocated by bootmem. */
		spin_lock(&map_entries_bootmem_lock);
		list_del(&entry->list);
		spin_unlock(&map_entries_bootmem_lock);

		memset(entry, 0, sizeof(*entry));
	}

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

	entry = memblock_virt_alloc(sizeof(struct firmware_map_entry), 0);
	if (WARN_ON(!entry))
		return -ENOMEM;

	return firmware_map_add_entry(start, end, type, entry);
}

/**
 * firmware_map_remove() - remove a firmware mapping entry
 * @start: Start of the memory range.
 * @end:   End of the memory range.
 * @type:  Type of the memory range.
 *
 * removes a firmware mapping entry.
 *
 * Returns 0 on success, or -EINVAL if no entry.
 **/
int __meminit firmware_map_remove(u64 start, u64 end, const char *type)
{
	struct firmware_map_entry *entry;

	spin_lock(&map_entries_lock);
	entry = firmware_map_find_entry(start, end - 1, type);
	if (!entry) {
		spin_unlock(&map_entries_lock);
		return -EINVAL;
	}

	firmware_map_remove_entry(entry);
	spin_unlock(&map_entries_lock);

	/* remove the memmap entry */
	remove_sysfs_fw_map_entry(entry);

	return 0;
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

static inline struct memmap_attribute *to_memmap_attr(struct attribute *attr)
{
	return container_of(attr, struct memmap_attribute, attr);
}

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

