/*
 * Compaq Hot Plug Controller Driver
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001,2003 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <greg@kroah.com>
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/workqueue.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/debugfs.h>
#include "cpqphp.h"

static int show_ctrl (struct controller *ctrl, char *buf)
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

static int show_dev (struct controller *ctrl, char *buf)
{
	char * out = buf;
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
		slot=slot->next;
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
	struct controller *ctrl;
};

#define MAX_OUTPUT	(4*PAGE_SIZE)

static int open(struct inode *inode, struct file *file)
{
	struct controller *ctrl = inode->i_private;
	struct ctrl_dbg *dbg;
	int retval = -ENOMEM;

	lock_kernel();
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
	unlock_kernel();
	return retval;
}

static loff_t lseek(struct file *file, loff_t off, int whence)
{
	struct ctrl_dbg *dbg;
	loff_t new = -1;

	lock_kernel();
	dbg = file->private_data;

	switch (whence) {
	case 0:
		new = off;
		break;
	case 1:
		new = file->f_pos + off;
		break;
	}
	if (new < 0 || new > dbg->size) {
		unlock_kernel();
		return -EINVAL;
	}
	unlock_kernel();
	return (file->f_pos = new);
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

static struct file_operations debug_ops = {
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
	ctrl->dentry = debugfs_create_file(ctrl->pci_dev->dev.bus_id, S_IRUGO, root, ctrl, &debug_ops);
}

void cpqhp_remove_debugfs_files(struct controller *ctrl)
{
	if (ctrl->dentry)
		debugfs_remove(ctrl->dentry);
	ctrl->dentry = NULL;
}

