/*
 *  drivers/staging/android/ion/rockchip/rockchip_ion_snapshot.c
 *
 *  Copyright (C) 2011-2014 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) "ion_snapshot: " fmt
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <linux/debugfs.h>

#define LOG_BUF_LEN	(1 << CONFIG_ION_SNAPSHOT_BUF_SHIFT)
#define LOG_BUF_PAGE_ORDER	(CONFIG_ION_SNAPSHOT_BUF_SHIFT - PAGE_SHIFT)
// snapshot for last
static char last_ion_buf[LOG_BUF_LEN];
// snapshot for current
static char* ion_snapshot_buf;

static ssize_t last_ion_read(struct file *file, char __user *buf,
				    size_t len, loff_t *offset)
{
	loff_t pos = *offset;
	ssize_t count;

	if (pos >= LOG_BUF_LEN || last_ion_buf[0]==0)
		return 0;

	count = min(len, (size_t)(LOG_BUF_LEN - pos));
	if (copy_to_user(buf, &last_ion_buf[pos], count))
		return -EFAULT;

	*offset += count;
	return count;
}

static const struct file_operations last_ion_fops = {
	.owner = THIS_MODULE,
	.read = last_ion_read,
};

static ssize_t ion_snapshot_read(struct file *file, char __user *buf,
				    size_t len, loff_t *offset)
{
	loff_t pos = *offset;
	ssize_t count;

	if (pos >= LOG_BUF_LEN || ion_snapshot_buf[0]==0)
		return 0;

	count = min(len, (size_t)(LOG_BUF_LEN - pos));
	if (copy_to_user(buf, &ion_snapshot_buf[pos], count))
		return -EFAULT;

	*offset += count;
	return count;
}

static const struct file_operations ion_snapshot_fops = {
	.owner = THIS_MODULE,
	.read = ion_snapshot_read,
};

char *rockchip_ion_snapshot_get(size_t *size)
{
	*size = LOG_BUF_LEN;
	return ion_snapshot_buf;
}

int rockchip_ion_snapshot_debugfs(struct dentry* root)
{
	struct dentry* last_ion_dentry;
	struct dentry* ion_snapshot_dentry;

	last_ion_dentry = debugfs_create_file("last_ion", 0664,
						root,
						NULL, &last_ion_fops);
	if (!last_ion_dentry) {
		char buf[256], *path;
		path = dentry_path(root, buf, 256);
		pr_err("Failed to create client debugfs at %s/%s\n",
			path, "last_ion");
	}

	ion_snapshot_dentry = debugfs_create_file("ion_snapshot", 0664,
						root,
						NULL, &ion_snapshot_fops);
	if (!ion_snapshot_dentry) {
		char buf[256], *path;
		path = dentry_path(root, buf, 256);
		pr_err("Failed to create client debugfs at %s/%s\n",
			path, "ion_snapshot");
	}

	return 0;
}

static void * __init last_ion_vmap(phys_addr_t start, unsigned int page_count)
{
	struct page *pages[page_count + 1];
	unsigned int i;

	for (i = 0; i < page_count; i++) {
		phys_addr_t addr = start + i * PAGE_SIZE;
		pages[i] = pfn_to_page(addr >> PAGE_SHIFT);
	}
	pages[page_count] = pfn_to_page(start >> PAGE_SHIFT);
	return vmap(pages, page_count + 1, VM_MAP, pgprot_writecombine(PAGE_KERNEL));
}

static int __init rockchip_ion_snapshot_init(void)
{
	char *log_buf;

	log_buf = (char *)__get_free_pages(GFP_KERNEL, LOG_BUF_PAGE_ORDER);
	if (!log_buf) {
		pr_err("failed to __get_free_pages(%d)\n", LOG_BUF_PAGE_ORDER);
		return 0;
	}

	ion_snapshot_buf = last_ion_vmap(virt_to_phys(log_buf), 1 << LOG_BUF_PAGE_ORDER);
	if (!ion_snapshot_buf) {
		pr_err("failed to map %d pages at 0x%lx\n", 1 << LOG_BUF_PAGE_ORDER,
			(unsigned long)virt_to_phys(log_buf));
		return 0;
	}

	pr_info("0x%lx map to 0x%p and copy to 0x%p (version 0.1)\n", 
			(unsigned long)virt_to_phys(log_buf), ion_snapshot_buf,
			last_ion_buf);

	memcpy(last_ion_buf, ion_snapshot_buf, LOG_BUF_LEN);
	memset(ion_snapshot_buf, 0, LOG_BUF_LEN);

	return 0;
}

postcore_initcall(rockchip_ion_snapshot_init);
