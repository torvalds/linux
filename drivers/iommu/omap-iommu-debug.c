/*
 * omap iommu: debugfs interface
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/platform_data/iommu-omap.h>

#include "omap-iopgtable.h"
#include "omap-iommu.h"

static DEFINE_MUTEX(iommu_debug_lock);

static struct dentry *iommu_debug_root;

static inline bool is_omap_iommu_detached(struct omap_iommu *obj)
{
	return !obj->domain;
}

static ssize_t debug_read_regs(struct file *file, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	struct omap_iommu *obj = file->private_data;
	char *p, *buf;
	ssize_t bytes;

	if (is_omap_iommu_detached(obj))
		return -EPERM;

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	p = buf;

	mutex_lock(&iommu_debug_lock);

	bytes = omap_iommu_dump_ctx(obj, p, count);
	bytes = simple_read_from_buffer(userbuf, count, ppos, buf, bytes);

	mutex_unlock(&iommu_debug_lock);
	kfree(buf);

	return bytes;
}

static ssize_t debug_read_tlb(struct file *file, char __user *userbuf,
			      size_t count, loff_t *ppos)
{
	struct omap_iommu *obj = file->private_data;
	char *p, *buf;
	ssize_t bytes, rest;

	if (is_omap_iommu_detached(obj))
		return -EPERM;

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	p = buf;

	mutex_lock(&iommu_debug_lock);

	p += sprintf(p, "%8s %8s\n", "cam:", "ram:");
	p += sprintf(p, "-----------------------------------------\n");
	rest = count - (p - buf);
	p += omap_dump_tlb_entries(obj, p, rest);

	bytes = simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);

	mutex_unlock(&iommu_debug_lock);
	kfree(buf);

	return bytes;
}

#define dump_ioptable_entry_one(lv, da, val)			\
	({							\
		int __err = 0;					\
		ssize_t bytes;					\
		const int maxcol = 22;				\
		const char *str = "%d: %08x %08x\n";		\
		bytes = snprintf(p, maxcol, str, lv, da, val);	\
		p += bytes;					\
		len -= bytes;					\
		if (len < maxcol)				\
			__err = -ENOMEM;			\
		__err;						\
	})

static ssize_t dump_ioptable(struct omap_iommu *obj, char *buf, ssize_t len)
{
	int i;
	u32 *iopgd;
	char *p = buf;

	spin_lock(&obj->page_table_lock);

	iopgd = iopgd_offset(obj, 0);
	for (i = 0; i < PTRS_PER_IOPGD; i++, iopgd++) {
		int j, err;
		u32 *iopte;
		u32 da;

		if (!*iopgd)
			continue;

		if (!(*iopgd & IOPGD_TABLE)) {
			da = i << IOPGD_SHIFT;

			err = dump_ioptable_entry_one(1, da, *iopgd);
			if (err)
				goto out;
			continue;
		}

		iopte = iopte_offset(iopgd, 0);

		for (j = 0; j < PTRS_PER_IOPTE; j++, iopte++) {
			if (!*iopte)
				continue;

			da = (i << IOPGD_SHIFT) + (j << IOPTE_SHIFT);
			err = dump_ioptable_entry_one(2, da, *iopgd);
			if (err)
				goto out;
		}
	}
out:
	spin_unlock(&obj->page_table_lock);

	return p - buf;
}

static ssize_t debug_read_pagetable(struct file *file, char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	struct omap_iommu *obj = file->private_data;
	char *p, *buf;
	size_t bytes;

	if (is_omap_iommu_detached(obj))
		return -EPERM;

	buf = (char *)__get_free_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	p = buf;

	p += sprintf(p, "L: %8s %8s\n", "da:", "pa:");
	p += sprintf(p, "-----------------------------------------\n");

	mutex_lock(&iommu_debug_lock);

	bytes = PAGE_SIZE - (p - buf);
	p += dump_ioptable(obj, p, bytes);

	bytes = simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);

	mutex_unlock(&iommu_debug_lock);
	free_page((unsigned long)buf);

	return bytes;
}

#define DEBUG_FOPS_RO(name)						\
	static const struct file_operations debug_##name##_fops = {	\
		.open = simple_open,					\
		.read = debug_read_##name,				\
		.llseek = generic_file_llseek,				\
	};

DEBUG_FOPS_RO(regs);
DEBUG_FOPS_RO(tlb);
DEBUG_FOPS_RO(pagetable);

#define __DEBUG_ADD_FILE(attr, mode)					\
	{								\
		struct dentry *dent;					\
		dent = debugfs_create_file(#attr, mode, obj->debug_dir,	\
					   obj, &debug_##attr##_fops);	\
		if (!dent)						\
			goto err;					\
	}

#define DEBUG_ADD_FILE_RO(name) __DEBUG_ADD_FILE(name, 0400)

void omap_iommu_debugfs_add(struct omap_iommu *obj)
{
	struct dentry *d;

	if (!iommu_debug_root)
		return;

	obj->debug_dir = debugfs_create_dir(obj->name, iommu_debug_root);
	if (!obj->debug_dir)
		return;

	d = debugfs_create_u8("nr_tlb_entries", 0400, obj->debug_dir,
			      (u8 *)&obj->nr_tlb_entries);
	if (!d)
		return;

	DEBUG_ADD_FILE_RO(regs);
	DEBUG_ADD_FILE_RO(tlb);
	DEBUG_ADD_FILE_RO(pagetable);

	return;

err:
	debugfs_remove_recursive(obj->debug_dir);
}

void omap_iommu_debugfs_remove(struct omap_iommu *obj)
{
	if (!obj->debug_dir)
		return;

	debugfs_remove_recursive(obj->debug_dir);
}

void __init omap_iommu_debugfs_init(void)
{
	iommu_debug_root = debugfs_create_dir("omap_iommu", NULL);
	if (!iommu_debug_root)
		pr_err("can't create debugfs dir\n");
}

void __exit omap_iommu_debugfs_exit(void)
{
	debugfs_remove(iommu_debug_root);
}
