/*
 *  arch/arm/mach-rk29/last_log.c
 *
 *  Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) "last_log: " fmt
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define LOG_BUF_LEN	(1 << CONFIG_LOG_BUF_SHIFT)
#define LOG_BUF_PAGE_ORDER	(CONFIG_LOG_BUF_SHIFT - PAGE_SHIFT)
static char last_log_buf[LOG_BUF_LEN];
extern void switch_log_buf(char *new_log_buf, unsigned size);

char *last_log_get(unsigned *size)
{
	*size = LOG_BUF_LEN;
	return last_log_buf;
}

static ssize_t last_log_read(struct file *file, char __user *buf,
				    size_t len, loff_t *offset)
{
	loff_t pos = *offset;
	ssize_t count;

	if (pos >= LOG_BUF_LEN)
		return 0;

	count = min(len, (size_t)(LOG_BUF_LEN - pos));
	if (copy_to_user(buf, &last_log_buf[pos], count))
		return -EFAULT;

	*offset += count;
	return count;
}

static const struct file_operations last_log_file_ops = {
	.owner = THIS_MODULE,
	.read = last_log_read,
};

static void * __init last_log_vmap(phys_addr_t start, unsigned int page_count)
{
	struct page *pages[page_count];
	unsigned int i;

	for (i = 0; i < page_count; i++) {
		phys_addr_t addr = start + i * PAGE_SIZE;
		pages[i] = pfn_to_page(addr >> PAGE_SHIFT);
	}
	return vmap(pages, page_count, VM_MAP, pgprot_noncached(PAGE_KERNEL));
}

static int __init last_log_init(void)
{
	char *log_buf, *new_log_buf;
	struct proc_dir_entry *entry;

	log_buf = (char *)__get_free_pages(GFP_KERNEL, LOG_BUF_PAGE_ORDER);
	if (!log_buf) {
		pr_err("failed to __get_free_pages(%d)\n", LOG_BUF_PAGE_ORDER);
		return 0;
	}

	new_log_buf = last_log_vmap(virt_to_phys(log_buf), 1 << LOG_BUF_PAGE_ORDER);
	if (!new_log_buf) {
		pr_err("failed to map %d pages at 0x%08x\n", 1 << LOG_BUF_PAGE_ORDER, virt_to_phys(log_buf));
		return 0;
	}

	pr_info("0x%p map to 0x%p and copy to 0x%p (version 2.0)\n", log_buf, new_log_buf, last_log_buf);

	memcpy(last_log_buf, log_buf, LOG_BUF_LEN);
	switch_log_buf(new_log_buf, LOG_BUF_LEN);

	entry = create_proc_entry("last_log", S_IFREG | S_IRUGO, NULL);
	if (!entry) {
		pr_err("failed to create proc entry\n");
		return 0;
	}

	entry->proc_fops = &last_log_file_ops;
	entry->size = LOG_BUF_LEN;

#ifndef CONFIG_ANDROID_RAM_CONSOLE
	proc_symlink("last_kmsg", NULL, "last_log");
#endif

	return 0;
}

postcore_initcall(last_log_init);

