// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) IBM Corporation, 2024
 */

#define pr_fmt(fmt) "htmdump: " fmt

#include <linux/debugfs.h>
#include <linux/module.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/plpar_wrappers.h>

static void *htm_buf;
static u32 nodeindex;
static u32 nodalchipindex;
static u32 coreindexonchip;
static u32 htmtype;
static struct dentry *htmdump_debugfs_dir;

static ssize_t htmdump_read(struct file *filp, char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	void *htm_buf = filp->private_data;
	unsigned long page, read_size, available;
	loff_t offset;
	long rc;

	page = ALIGN_DOWN(*ppos, PAGE_SIZE);
	offset = (*ppos) % PAGE_SIZE;

	rc = htm_get_dump_hardware(nodeindex, nodalchipindex, coreindexonchip,
				   htmtype, virt_to_phys(htm_buf), PAGE_SIZE, page);

	switch (rc) {
	case H_SUCCESS:
	/* H_PARTIAL for the case where all available data can't be
	 * returned due to buffer size constraint.
	 */
	case H_PARTIAL:
		break;
	/* H_NOT_AVAILABLE indicates reading from an offset outside the range,
	 * i.e. past end of file.
	 */
	case H_NOT_AVAILABLE:
		return 0;
	case H_BUSY:
	case H_LONG_BUSY_ORDER_1_MSEC:
	case H_LONG_BUSY_ORDER_10_MSEC:
	case H_LONG_BUSY_ORDER_100_MSEC:
	case H_LONG_BUSY_ORDER_1_SEC:
	case H_LONG_BUSY_ORDER_10_SEC:
	case H_LONG_BUSY_ORDER_100_SEC:
		return -EBUSY;
	case H_PARAMETER:
	case H_P2:
	case H_P3:
	case H_P4:
	case H_P5:
	case H_P6:
		return -EINVAL;
	case H_STATE:
		return -EIO;
	case H_AUTHORITY:
		return -EPERM;
	}

	available = PAGE_SIZE;
	read_size = min(count, available);
	*ppos += read_size;
	return simple_read_from_buffer(ubuf, count, &offset, htm_buf, available);
}

static const struct file_operations htmdump_fops = {
	.llseek = NULL,
	.read	= htmdump_read,
	.open	= simple_open,
};

static int htmdump_init_debugfs(void)
{
	htm_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!htm_buf) {
		pr_err("Failed to allocate htmdump buf\n");
		return -ENOMEM;
	}

	htmdump_debugfs_dir = debugfs_create_dir("htmdump",
						  arch_debugfs_dir);

	debugfs_create_u32("nodeindex", 0600,
			htmdump_debugfs_dir, &nodeindex);
	debugfs_create_u32("nodalchipindex", 0600,
			htmdump_debugfs_dir, &nodalchipindex);
	debugfs_create_u32("coreindexonchip", 0600,
			htmdump_debugfs_dir, &coreindexonchip);
	debugfs_create_u32("htmtype", 0600,
			htmdump_debugfs_dir, &htmtype);
	debugfs_create_file("trace", 0400, htmdump_debugfs_dir, htm_buf, &htmdump_fops);

	return 0;
}

static int __init htmdump_init(void)
{
	if (htmdump_init_debugfs())
		return -ENOMEM;

	return 0;
}

static void __exit htmdump_exit(void)
{
	debugfs_remove_recursive(htmdump_debugfs_dir);
	kfree(htm_buf);
}

module_init(htmdump_init);
module_exit(htmdump_exit);
MODULE_DESCRIPTION("PHYP Hardware Trace Macro (HTM) data dumper");
MODULE_LICENSE("GPL");
