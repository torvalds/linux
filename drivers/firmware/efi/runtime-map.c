// SPDX-License-Identifier: GPL-2.0
/*
 * linux/drivers/efi/runtime-map.c
 * Copyright (C) 2013 Red Hat, Inc., Dave Young <dyoung@redhat.com>
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/efi.h>
#include <linux/slab.h>

#include <asm/setup.h>

struct efi_runtime_map_entry {
	efi_memory_desc_t md;
	struct kobject kobj;   /* kobject for each entry */
};

static struct efi_runtime_map_entry **map_entries;

struct map_attribute {
	struct attribute attr;
	ssize_t (*show)(struct efi_runtime_map_entry *entry, char *buf);
};

static inline struct map_attribute *to_map_attr(struct attribute *attr)
{
	return container_of(attr, struct map_attribute, attr);
}

static ssize_t type_show(struct efi_runtime_map_entry *entry, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n", entry->md.type);
}

#define EFI_RUNTIME_FIELD(var) entry->md.var

#define EFI_RUNTIME_U64_ATTR_SHOW(name) \
static ssize_t name##_show(struct efi_runtime_map_entry *entry, char *buf) \
{ \
	return snprintf(buf, PAGE_SIZE, "0x%llx\n", EFI_RUNTIME_FIELD(name)); \
}

EFI_RUNTIME_U64_ATTR_SHOW(phys_addr);
EFI_RUNTIME_U64_ATTR_SHOW(virt_addr);
EFI_RUNTIME_U64_ATTR_SHOW(num_pages);
EFI_RUNTIME_U64_ATTR_SHOW(attribute);

static inline struct efi_runtime_map_entry *to_map_entry(struct kobject *kobj)
{
	return container_of(kobj, struct efi_runtime_map_entry, kobj);
}

static ssize_t map_attr_show(struct kobject *kobj, struct attribute *attr,
			      char *buf)
{
	struct efi_runtime_map_entry *entry = to_map_entry(kobj);
	struct map_attribute *map_attr = to_map_attr(attr);

	return map_attr->show(entry, buf);
}

static struct map_attribute map_type_attr = __ATTR_RO_MODE(type, 0400);
static struct map_attribute map_phys_addr_attr = __ATTR_RO_MODE(phys_addr, 0400);
static struct map_attribute map_virt_addr_attr = __ATTR_RO_MODE(virt_addr, 0400);
static struct map_attribute map_num_pages_attr = __ATTR_RO_MODE(num_pages, 0400);
static struct map_attribute map_attribute_attr = __ATTR_RO_MODE(attribute, 0400);

/*
 * These are default attributes that are added for every memmap entry.
 */
static struct attribute *def_attrs[] = {
	&map_type_attr.attr,
	&map_phys_addr_attr.attr,
	&map_virt_addr_attr.attr,
	&map_num_pages_attr.attr,
	&map_attribute_attr.attr,
	NULL
};

static const struct sysfs_ops map_attr_ops = {
	.show = map_attr_show,
};

static void map_release(struct kobject *kobj)
{
	struct efi_runtime_map_entry *entry;

	entry = to_map_entry(kobj);
	kfree(entry);
}

static struct kobj_type __refdata map_ktype = {
	.sysfs_ops	= &map_attr_ops,
	.default_attrs	= def_attrs,
	.release	= map_release,
};

static struct kset *map_kset;

static struct efi_runtime_map_entry *
add_sysfs_runtime_map_entry(struct kobject *kobj, int nr,
			    efi_memory_desc_t *md)
{
	int ret;
	struct efi_runtime_map_entry *entry;

	if (!map_kset) {
		map_kset = kset_create_and_add("runtime-map", NULL, kobj);
		if (!map_kset)
			return ERR_PTR(-ENOMEM);
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		kset_unregister(map_kset);
		map_kset = NULL;
		return ERR_PTR(-ENOMEM);
	}

	memcpy(&entry->md, md, sizeof(efi_memory_desc_t));

	kobject_init(&entry->kobj, &map_ktype);
	entry->kobj.kset = map_kset;
	ret = kobject_add(&entry->kobj, NULL, "%d", nr);
	if (ret) {
		kobject_put(&entry->kobj);
		kset_unregister(map_kset);
		map_kset = NULL;
		return ERR_PTR(ret);
	}

	return entry;
}

int efi_get_runtime_map_size(void)
{
	return efi.memmap.nr_map * efi.memmap.desc_size;
}

int efi_get_runtime_map_desc_size(void)
{
	return efi.memmap.desc_size;
}

int efi_runtime_map_copy(void *buf, size_t bufsz)
{
	size_t sz = efi_get_runtime_map_size();

	if (sz > bufsz)
		sz = bufsz;

	memcpy(buf, efi.memmap.map, sz);
	return 0;
}

int __init efi_runtime_map_init(struct kobject *efi_kobj)
{
	int i, j, ret = 0;
	struct efi_runtime_map_entry *entry;
	efi_memory_desc_t *md;

	if (!efi_enabled(EFI_MEMMAP))
		return 0;

	map_entries = kcalloc(efi.memmap.nr_map, sizeof(entry), GFP_KERNEL);
	if (!map_entries) {
		ret = -ENOMEM;
		goto out;
	}

	i = 0;
	for_each_efi_memory_desc(md) {
		entry = add_sysfs_runtime_map_entry(efi_kobj, i, md);
		if (IS_ERR(entry)) {
			ret = PTR_ERR(entry);
			goto out_add_entry;
		}
		*(map_entries + i++) = entry;
	}

	return 0;
out_add_entry:
	for (j = i - 1; j >= 0; j--) {
		entry = *(map_entries + j);
		kobject_put(&entry->kobj);
	}
out:
	return ret;
}
