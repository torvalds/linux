/*
 *  arch/arm/mach-rockchip/last_log.c
 *
 *  Copyright (C) 2011-2014 ROCKCHIP, Inc.
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
#include <linux/rockchip/cpu.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define LOG_BUF_SHIFT	CONFIG_LOG_BUF_SHIFT
#define LOG_BUF_LEN	(1 << LOG_BUF_SHIFT)
#define LOG_BUF_PAGE_ORDER	(LOG_BUF_SHIFT - PAGE_SHIFT)
static char *last_log_buf;
static char *log_buf;
static size_t log_pos;
static char early_log_buf[8192];

char *rk_last_log_get(unsigned *size)
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

static const struct file_operations last_log_fops = {
	.owner = THIS_MODULE,
	.read = last_log_read,
};

static void * __init last_log_vmap(phys_addr_t start, unsigned int page_count)
{
	struct page *pages[page_count + 1];
	unsigned int i;

	for (i = 0; i < page_count; i++) {
		phys_addr_t addr = start + i * PAGE_SIZE;
		pages[i] = pfn_to_page(addr >> PAGE_SHIFT);
	}
	pages[page_count] = pfn_to_page(start >> PAGE_SHIFT);
	return vmap(pages, page_count + 1, VM_MAP, pgprot_noncached(PAGE_KERNEL));
}

static int __init rk_last_log_init(void)
{
	size_t early_log_size;
	char *buf;
	struct proc_dir_entry *entry;

	if (!cpu_is_rockchip())
		return 0;

	buf = (char *)__get_free_pages(GFP_KERNEL, LOG_BUF_PAGE_ORDER);
	if (!buf) {
		pr_err("failed to __get_free_pages(%d)\n", LOG_BUF_PAGE_ORDER);
		return 0;
	}

	log_buf = last_log_vmap(virt_to_phys(buf), 1 << LOG_BUF_PAGE_ORDER);
	if (!log_buf) {
		pr_err("failed to map %d pages at 0x%08x\n", 1 << LOG_BUF_PAGE_ORDER, virt_to_phys(buf));
		return 0;
	}

	last_log_buf = (char *)vmalloc(LOG_BUF_LEN);
	if (!last_log_buf) {
		pr_err("failed to vmalloc(%d)\n", LOG_BUF_LEN);
		return 0;
	}

	memcpy(last_log_buf, buf, LOG_BUF_LEN);
	early_log_size = log_pos > sizeof(early_log_buf) ? sizeof(early_log_buf) : log_pos;
	memcpy(log_buf, early_log_buf, early_log_size);
	memset(log_buf + early_log_size, 0, LOG_BUF_LEN - early_log_size);

	pr_info("0x%08x map to 0x%p and copy to 0x%p, size 0x%x early 0x%x (version 3.0)\n", virt_to_phys(buf), log_buf, last_log_buf, LOG_BUF_LEN, early_log_size);

	entry = proc_create("last_kmsg", S_IRUSR, NULL, &last_log_fops);
	if (!entry) {
		pr_err("failed to create proc entry\n");
		return 0;
	}
	proc_set_size(entry, LOG_BUF_LEN);

	proc_symlink("last_log", NULL, "last_kmsg");

	return 0;
}

early_initcall(rk_last_log_init);

void rk_last_log_text(char *text, size_t size)
{
	char *buf = log_buf ? log_buf : early_log_buf;
	size_t log_size = log_buf ? LOG_BUF_LEN : sizeof(early_log_buf);
	size_t pos;

	/* Check overflow */
	pos = log_pos & (log_size - 1);
	if (likely(size + pos <= log_size))
		memcpy(&buf[pos], text, size);
	else {
		size_t first = log_size - pos;
		size_t second = size - first;
		memcpy(&buf[pos], text, first);
		memcpy(&buf[0], text + first, second);
	}

	log_pos += size;
}
