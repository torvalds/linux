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

#include <linux/module.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/omap-iommu.h>
#include <linux/platform_data/iommu-omap.h>

#include "omap-iopgtable.h"
#include "omap-iommu.h"

#define MAXCOLUMN 100 /* for short messages */

static DEFINE_MUTEX(iommu_debug_lock);

static struct dentry *iommu_debug_root;

static ssize_t debug_read_ver(struct file *file, char __user *userbuf,
			      size_t count, loff_t *ppos)
{
	u32 ver = omap_iommu_arch_version();
	char buf[MAXCOLUMN], *p = buf;

	p += sprintf(p, "H/W version: %d.%d\n", (ver >> 4) & 0xf , ver & 0xf);

	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}

static ssize_t debug_read_regs(struct file *file, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	struct device *dev = file->private_data;
	struct omap_iommu *obj = dev_to_omap_iommu(dev);
	char *p, *buf;
	ssize_t bytes;

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
	struct device *dev = file->private_data;
	struct omap_iommu *obj = dev_to_omap_iommu(dev);
	char *p, *buf;
	ssize_t bytes, rest;

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

static ssize_t debug_write_pagetable(struct file *file,
		     const char __user *userbuf, size_t count, loff_t *ppos)
{
	struct iotlb_entry e;
	struct cr_regs cr;
	int err;
	struct device *dev = file->private_data;
	struct omap_iommu *obj = dev_to_omap_iommu(dev);
	char buf[MAXCOLUMN], *p = buf;

	count = min(count, sizeof(buf));

	mutex_lock(&iommu_debug_lock);
	if (copy_from_user(p, userbuf, count)) {
		mutex_unlock(&iommu_debug_lock);
		return -EFAULT;
	}

	sscanf(p, "%x %x", &cr.cam, &cr.ram);
	if (!cr.cam || !cr.ram) {
		mutex_unlock(&iommu_debug_lock);
		return -EINVAL;
	}

	omap_iotlb_cr_to_e(&cr, &e);
	err = omap_iopgtable_store_entry(obj, &e);
	if (err)
		dev_err(obj->dev, "%s: fail to store cr\n", __func__);

	mutex_unlock(&iommu_debug_lock);
	return count;
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
	struct device *dev = file->private_data;
	struct omap_iommu *obj = dev_to_omap_iommu(dev);
	char *p, *buf;
	size_t bytes;

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

static ssize_t debug_read_mmap(struct file *file, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	struct device *dev = file->private_data;
	struct omap_iommu *obj = dev_to_omap_iommu(dev);
	char *p, *buf;
	struct iovm_struct *tmp;
	int uninitialized_var(i);
	ssize_t bytes;

	buf = (char *)__get_free_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	p = buf;

	p += sprintf(p, "%-3s %-8s %-8s %6s %8s\n",
		     "No", "start", "end", "size", "flags");
	p += sprintf(p, "-------------------------------------------------\n");

	mutex_lock(&iommu_debug_lock);

	list_for_each_entry(tmp, &obj->mmap, list) {
		size_t len;
		const char *str = "%3d %08x-%08x %6x %8x\n";
		const int maxcol = 39;

		len = tmp->da_end - tmp->da_start;
		p += snprintf(p, maxcol, str,
			      i, tmp->da_start, tmp->da_end, len, tmp->flags);

		if (PAGE_SIZE - (p - buf) < maxcol)
			break;
		i++;
	}

	bytes = simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);

	mutex_unlock(&iommu_debug_lock);
	free_page((unsigned long)buf);

	return bytes;
}

static ssize_t debug_read_mem(struct file *file, char __user *userbuf,
			      size_t count, loff_t *ppos)
{
	struct device *dev = file->private_data;
	char *p, *buf;
	struct iovm_struct *area;
	ssize_t bytes;

	count = min_t(ssize_t, count, PAGE_SIZE);

	buf = (char *)__get_free_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	p = buf;

	mutex_lock(&iommu_debug_lock);

	area = omap_find_iovm_area(dev, (u32)ppos);
	if (!area) {
		bytes = -EINVAL;
		goto err_out;
	}
	memcpy(p, area->va, count);
	p += count;

	bytes = simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
err_out:
	mutex_unlock(&iommu_debug_lock);
	free_page((unsigned long)buf);

	return bytes;
}

static ssize_t debug_write_mem(struct file *file, const char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	struct device *dev = file->private_data;
	struct iovm_struct *area;
	char *p, *buf;

	count = min_t(size_t, count, PAGE_SIZE);

	buf = (char *)__get_free_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	p = buf;

	mutex_lock(&iommu_debug_lock);

	if (copy_from_user(p, userbuf, count)) {
		count =  -EFAULT;
		goto err_out;
	}

	area = omap_find_iovm_area(dev, (u32)ppos);
	if (!area) {
		count = -EINVAL;
		goto err_out;
	}
	memcpy(area->va, p, count);
err_out:
	mutex_unlock(&iommu_debug_lock);
	free_page((unsigned long)buf);

	return count;
}

#define DEBUG_FOPS(name)						\
	static const struct file_operations debug_##name##_fops = {	\
		.open = simple_open,					\
		.read = debug_read_##name,				\
		.write = debug_write_##name,				\
		.llseek = generic_file_llseek,				\
	};

#define DEBUG_FOPS_RO(name)						\
	static const struct file_operations debug_##name##_fops = {	\
		.open = simple_open,					\
		.read = debug_read_##name,				\
		.llseek = generic_file_llseek,				\
	};

DEBUG_FOPS_RO(ver);
DEBUG_FOPS_RO(regs);
DEBUG_FOPS_RO(tlb);
DEBUG_FOPS(pagetable);
DEBUG_FOPS_RO(mmap);
DEBUG_FOPS(mem);

#define __DEBUG_ADD_FILE(attr, mode)					\
	{								\
		struct dentry *dent;					\
		dent = debugfs_create_file(#attr, mode, parent,		\
					   dev, &debug_##attr##_fops);	\
		if (!dent)						\
			return -ENOMEM;					\
	}

#define DEBUG_ADD_FILE(name) __DEBUG_ADD_FILE(name, 0600)
#define DEBUG_ADD_FILE_RO(name) __DEBUG_ADD_FILE(name, 0400)

static int iommu_debug_register(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct omap_iommu *obj = platform_get_drvdata(pdev);
	struct omap_iommu_arch_data *arch_data;
	struct dentry *d, *parent;

	if (!obj || !obj->dev)
		return -EINVAL;

	arch_data = kzalloc(sizeof(*arch_data), GFP_KERNEL);
	if (!arch_data)
		return -ENOMEM;

	arch_data->iommu_dev = obj;

	dev->archdata.iommu = arch_data;

	d = debugfs_create_dir(obj->name, iommu_debug_root);
	if (!d)
		goto nomem;
	parent = d;

	d = debugfs_create_u8("nr_tlb_entries", 400, parent,
			      (u8 *)&obj->nr_tlb_entries);
	if (!d)
		goto nomem;

	DEBUG_ADD_FILE_RO(ver);
	DEBUG_ADD_FILE_RO(regs);
	DEBUG_ADD_FILE_RO(tlb);
	DEBUG_ADD_FILE(pagetable);
	DEBUG_ADD_FILE_RO(mmap);
	DEBUG_ADD_FILE(mem);

	return 0;

nomem:
	kfree(arch_data);
	return -ENOMEM;
}

static int iommu_debug_unregister(struct device *dev, void *data)
{
	if (!dev->archdata.iommu)
		return 0;

	kfree(dev->archdata.iommu);

	dev->archdata.iommu = NULL;

	return 0;
}

static int __init iommu_debug_init(void)
{
	struct dentry *d;
	int err;

	d = debugfs_create_dir("iommu", NULL);
	if (!d)
		return -ENOMEM;
	iommu_debug_root = d;

	err = omap_foreach_iommu_device(d, iommu_debug_register);
	if (err)
		goto err_out;
	return 0;

err_out:
	debugfs_remove_recursive(iommu_debug_root);
	return err;
}
module_init(iommu_debug_init)

static void __exit iommu_debugfs_exit(void)
{
	debugfs_remove_recursive(iommu_debug_root);
	omap_foreach_iommu_device(NULL, iommu_debug_unregister);
}
module_exit(iommu_debugfs_exit)

MODULE_DESCRIPTION("omap iommu: debugfs interface");
MODULE_AUTHOR("Hiroshi DOYU <Hiroshi.DOYU@nokia.com>");
MODULE_LICENSE("GPL v2");
