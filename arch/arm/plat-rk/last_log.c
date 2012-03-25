/*
 *  arch/arm/mach-rk29/last_log.c
 *
 *  Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define LOG_BUF_LEN	(1 << CONFIG_LOG_BUF_SHIFT)
static char last_log_buf[LOG_BUF_LEN];
extern void switch_log_buf(char *new_log_buf, unsigned size);

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

static int __init last_log_init(void)
{
	char *log_buf;
	struct proc_dir_entry *entry;

	log_buf = (char *)__get_free_pages(GFP_KERNEL, CONFIG_LOG_BUF_SHIFT - PAGE_SHIFT);
	if (!log_buf) {
		printk(KERN_ERR "last_log: failed to __get_free_pages(%d)\n", CONFIG_LOG_BUF_SHIFT - PAGE_SHIFT);
		return 0;
	}
	printk("last_log: 0x%p 0x%p\n", log_buf, last_log_buf);

	memcpy(last_log_buf, log_buf, LOG_BUF_LEN);
	switch_log_buf(log_buf, LOG_BUF_LEN);

	entry = create_proc_entry("last_log", S_IFREG | S_IRUGO, NULL);
	if (!entry) {
		printk(KERN_ERR "last_log: failed to create proc entry\n");
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

