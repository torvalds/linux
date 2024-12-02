// SPDX-License-Identifier: GPL-2.0+
/*
 * Compaq Hot Plug Controller Driver
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001,2003 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 *
 * All rights reserved.
 *
 * Send feedback to <greg@kroah.com>
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/workqueue.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/mutex.h>
#include <linux/debugfs.h>
#include "cpqphp.h"

static DEFINE_MUTEX(cpqphp_mutex);
static int show_ctrl(struct controller *ctrl, char *buf)
{
	char *out = buf;
	int index;
	struct pci_resource *res;

	out += sprintf(buf, "Free resources: memory\n");
	index = 11;
	res = ctrl->mem_head;
	while (res && index--) {
		out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
		res = res->next;
	}
	out += sprintf(out, "Free resources: prefetchable memory\n");
	index = 11;
	res = ctrl->p_mem_head;
	while (res && index--) {
		out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
		res = res->next;
	}
	out += sprintf(out, "Free resources: IO\n");
	index = 11;
	res = ctrl->io_head;
	while (res && index--) {
		out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
		res = res->next;
	}
	out += sprintf(out, "Free resources: bus numbers\n");
	index = 11;
	res = ctrl->bus_head;
	while (res && index--) {
		out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
		res = res->next;
	}

	return out - buf;
}

static int show_dev(struct controller *ctrl, char *buf)
{
	char *out = buf;
	int index;
	struct pci_resource *res;
	struct pci_func *new_slot;
	struct slot *slot;

	slot = ctrl->slot;

	while (slot) {
		new_slot = cpqhp_slot_find(slot->bus, slot->device, 0);
		if (!new_slot)
			break;
		out += sprintf(out, "assigned resources: memory\n");
		index = 11;
		res = new_slot->mem_head;
		while (res && index--) {
			out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
			res = res->next;
		}
		out += sprintf(out, "assigned resources: prefetchable memory\n");
		index = 11;
		res = new_slot->p_mem_head;
		while (res && index--) {
			out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
			res = res->next;
		}
		out += sprintf(out, "assigned resources: IO\n");
		index = 11;
		res = new_slot->io_head;
		while (res && index--) {
			out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
			res = res->next;
		}
		out += sprintf(out, "assigned resources: bus numbers\n");
		index = 11;
		res = new_slot->bus_head;
		while (res && index--) {
			out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
			res = res->next;
		}
		slot = slot->next;
	}

	return out - buf;
}

static int spew_debug_info(struct controller *ctrl, char *data, int size)
{
	int used;

	used = size - show_ctrl(ctrl, data);
	used = (size - used) - show_dev(ctrl, &data[used]);
	return used;
}

struct ctrl_dbg {
	int size;
	char *data;
};

#define MAX_OUTPUT	(4*PAGE_SIZE)

static int open(struct inode *inode, struct file *file)
{
	struct controller *ctrl = inode->i_private;
	struct ctrl_dbg *dbg;
	int retval = -ENOMEM;

	mutex_lock(&cpqphp_mutex);
	dbg = kmalloc(sizeof(*dbg), GFP_KERNEL);
	if (!dbg)
		goto exit;
	dbg->data = kmalloc(MAX_OUTPUT, GFP_KERNEL);
	if (!dbg->data) {
		kfree(dbg);
		goto exit;
	}
	dbg->size = spew_debug_info(ctrl, dbg->data, MAX_OUTPUT);
	file->private_data = dbg;
	retval = 0;
exit:
	mutex_unlock(&cpqphp_mutex);
	return retval;
}

static loff_t lseek(struct file *file, loff_t off, int whence)
{
	struct ctrl_dbg *dbg = file->private_data;
	return fixed_size_llseek(file, off, whence, dbg->size);
}

static ssize_t read(struct file *file, char __user *buf,
		    size_t nbytes, loff_t *ppos)
{
	struct ctrl_dbg *dbg = file->private_data;
	return simple_read_from_buffer(buf, nbytes, ppos, dbg->data, dbg->size);
}

static int release(struct inode *inode, struct file *file)
{
	struct ctrl_dbg *dbg = file->private_data;

	kfree(dbg->data);
	kfree(dbg);
	return 0;
}

static const struct file_operations debug_ops = {
	.owner = THIS_MODULE,
	.open = open,
	.llseek = lseek,
	.read = read,
	.release = release,
};

static struct dentry *root;

void cpqhp_initialize_debugfs(void)
{
	if (!root)
		root = debugfs_create_dir("cpqhp", NULL);
}

void cpqhp_shutdown_debugfs(void)
{
	debugfs_remove(root);
}

void cpqhp_create_debugfs_files(struct controller *ctrl)
{
	ctrl->dentry = debugfs_create_file(dev_name(&ctrl->pci_dev->dev),
					   S_IRUGO, root, ctrl, &debug_ops);
}

void cpqhp_remove_debugfs_files(struct controller *ctrl)
{
	debugfs_remove(ctrl->dentry);
	ctrl->dentry = NULL;
}

