/*
 * Ultra Wide Band
 * Debug support
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * FIXME: doc
 */

#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>

#include <linux/uwb/debug-cmd.h>
#define D_LOCAL 0
#include <linux/uwb/debug.h>

#include "uwb-internal.h"

void dump_bytes(struct device *dev, const void *_buf, size_t rsize)
{
	const char *buf = _buf;
	char line[32];
	size_t offset = 0;
	int cnt, cnt2;
	for (cnt = 0; cnt < rsize; cnt += 8) {
		size_t rtop = rsize - cnt < 8 ? rsize - cnt : 8;
		for (offset = cnt2 = 0; cnt2 < rtop; cnt2++) {
			offset += scnprintf(line + offset, sizeof(line) - offset,
					    "%02x ", buf[cnt + cnt2] & 0xff);
		}
		if (dev)
			dev_info(dev, "%s\n", line);
		else
			printk(KERN_INFO "%s\n", line);
	}
}
EXPORT_SYMBOL_GPL(dump_bytes);

/*
 * Debug interface
 *
 * Per radio controller debugfs files (in uwb/uwbN/):
 *
 * command: Flexible command interface (see <linux/uwb/debug-cmd.h>).
 *
 * reservations: information on reservations.
 *
 * accept: Set to true (Y or 1) to accept reservation requests from
 * peers.
 *
 * drp_avail: DRP availability information.
 */

struct uwb_dbg {
	struct uwb_pal pal;

	u32 accept;
	struct list_head rsvs;

	struct dentry *root_d;
	struct dentry *command_f;
	struct dentry *reservations_f;
	struct dentry *accept_f;
	struct dentry *drp_avail_f;
};

static struct dentry *root_dir;

static void uwb_dbg_rsv_cb(struct uwb_rsv *rsv)
{
	struct uwb_rc *rc = rsv->rc;
	struct device *dev = &rc->uwb_dev.dev;
	struct uwb_dev_addr devaddr;
	char owner[UWB_ADDR_STRSIZE], target[UWB_ADDR_STRSIZE];

	uwb_dev_addr_print(owner, sizeof(owner), &rsv->owner->dev_addr);
	if (rsv->target.type == UWB_RSV_TARGET_DEV)
		devaddr = rsv->target.dev->dev_addr;
	else
		devaddr = rsv->target.devaddr;
	uwb_dev_addr_print(target, sizeof(target), &devaddr);

	dev_dbg(dev, "debug: rsv %s -> %s: %s\n",
		owner, target, uwb_rsv_state_str(rsv->state));
}

static int cmd_rsv_establish(struct uwb_rc *rc,
			     struct uwb_dbg_cmd_rsv_establish *cmd)
{
	struct uwb_mac_addr macaddr;
	struct uwb_rsv *rsv;
	struct uwb_dev *target;
	int ret;

	memcpy(&macaddr, cmd->target, sizeof(macaddr));
	target = uwb_dev_get_by_macaddr(rc, &macaddr);
	if (target == NULL)
		return -ENODEV;

	rsv = uwb_rsv_create(rc, uwb_dbg_rsv_cb, NULL);
	if (rsv == NULL) {
		uwb_dev_put(target);
		return -ENOMEM;
	}

	rsv->owner       = &rc->uwb_dev;
	rsv->target.type = UWB_RSV_TARGET_DEV;
	rsv->target.dev  = target;
	rsv->type        = cmd->type;
	rsv->max_mas     = cmd->max_mas;
	rsv->min_mas     = cmd->min_mas;
	rsv->sparsity    = cmd->sparsity;

	ret = uwb_rsv_establish(rsv);
	if (ret)
		uwb_rsv_destroy(rsv);
	else
		list_add_tail(&rsv->pal_node, &rc->dbg->rsvs);

	return ret;
}

static int cmd_rsv_terminate(struct uwb_rc *rc,
			     struct uwb_dbg_cmd_rsv_terminate *cmd)
{
	struct uwb_rsv *rsv, *found = NULL;
	int i = 0;

	list_for_each_entry(rsv, &rc->dbg->rsvs, pal_node) {
		if (i == cmd->index) {
			found = rsv;
			break;
		}
	}
	if (!found)
		return -EINVAL;

	list_del(&found->pal_node);
	uwb_rsv_terminate(found);

	return 0;
}

static int command_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return 0;
}

static ssize_t command_write(struct file *file, const char __user *buf,
			 size_t len, loff_t *off)
{
	struct uwb_rc *rc = file->private_data;
	struct uwb_dbg_cmd cmd;
	int ret;

	if (len != sizeof(struct uwb_dbg_cmd))
		return -EINVAL;

	if (copy_from_user(&cmd, buf, len) != 0)
		return -EFAULT;

	switch (cmd.type) {
	case UWB_DBG_CMD_RSV_ESTABLISH:
		ret = cmd_rsv_establish(rc, &cmd.rsv_establish);
		break;
	case UWB_DBG_CMD_RSV_TERMINATE:
		ret = cmd_rsv_terminate(rc, &cmd.rsv_terminate);
		break;
	default:
		return -EINVAL;
	}

	return ret < 0 ? ret : len;
}

static struct file_operations command_fops = {
	.open   = command_open,
	.write  = command_write,
	.read   = NULL,
	.llseek = no_llseek,
	.owner  = THIS_MODULE,
};

static int reservations_print(struct seq_file *s, void *p)
{
	struct uwb_rc *rc = s->private;
	struct uwb_rsv *rsv;

	mutex_lock(&rc->rsvs_mutex);

	list_for_each_entry(rsv, &rc->reservations, rc_node) {
		struct uwb_dev_addr devaddr;
		char owner[UWB_ADDR_STRSIZE], target[UWB_ADDR_STRSIZE];
		bool is_owner;
		char buf[72];

		uwb_dev_addr_print(owner, sizeof(owner), &rsv->owner->dev_addr);
		if (rsv->target.type == UWB_RSV_TARGET_DEV) {
			devaddr = rsv->target.dev->dev_addr;
			is_owner = &rc->uwb_dev == rsv->owner;
		} else {
			devaddr = rsv->target.devaddr;
			is_owner = true;
		}
		uwb_dev_addr_print(target, sizeof(target), &devaddr);

		seq_printf(s, "%c %s -> %s: %s\n",
			   is_owner ? 'O' : 'T',
			   owner, target, uwb_rsv_state_str(rsv->state));
		seq_printf(s, "  stream: %d  type: %s\n",
			   rsv->stream, uwb_rsv_type_str(rsv->type));
		bitmap_scnprintf(buf, sizeof(buf), rsv->mas.bm, UWB_NUM_MAS);
		seq_printf(s, "  %s\n", buf);
	}

	mutex_unlock(&rc->rsvs_mutex);

	return 0;
}

static int reservations_open(struct inode *inode, struct file *file)
{
	return single_open(file, reservations_print, inode->i_private);
}

static struct file_operations reservations_fops = {
	.open    = reservations_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
	.owner   = THIS_MODULE,
};

static int drp_avail_print(struct seq_file *s, void *p)
{
	struct uwb_rc *rc = s->private;
	char buf[72];

	bitmap_scnprintf(buf, sizeof(buf), rc->drp_avail.global, UWB_NUM_MAS);
	seq_printf(s, "global:  %s\n", buf);
	bitmap_scnprintf(buf, sizeof(buf), rc->drp_avail.local, UWB_NUM_MAS);
	seq_printf(s, "local:   %s\n", buf);
	bitmap_scnprintf(buf, sizeof(buf), rc->drp_avail.pending, UWB_NUM_MAS);
	seq_printf(s, "pending: %s\n", buf);

	return 0;
}

static int drp_avail_open(struct inode *inode, struct file *file)
{
	return single_open(file, drp_avail_print, inode->i_private);
}

static struct file_operations drp_avail_fops = {
	.open    = drp_avail_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
	.owner   = THIS_MODULE,
};

static void uwb_dbg_new_rsv(struct uwb_rsv *rsv)
{
	struct uwb_rc *rc = rsv->rc;

	if (rc->dbg->accept)
		uwb_rsv_accept(rsv, uwb_dbg_rsv_cb, NULL);
}

/**
 * uwb_dbg_add_rc - add a debug interface for a radio controller
 * @rc: the radio controller
 */
void uwb_dbg_add_rc(struct uwb_rc *rc)
{
	rc->dbg = kzalloc(sizeof(struct uwb_dbg), GFP_KERNEL);
	if (rc->dbg == NULL)
		return;

	INIT_LIST_HEAD(&rc->dbg->rsvs);

	uwb_pal_init(&rc->dbg->pal);
	rc->dbg->pal.new_rsv = uwb_dbg_new_rsv;
	uwb_pal_register(rc, &rc->dbg->pal);
	if (root_dir) {
		rc->dbg->root_d = debugfs_create_dir(dev_name(&rc->uwb_dev.dev),
						     root_dir);
		rc->dbg->command_f = debugfs_create_file("command", 0200,
							 rc->dbg->root_d, rc,
							 &command_fops);
		rc->dbg->reservations_f = debugfs_create_file("reservations", 0444,
							      rc->dbg->root_d, rc,
							      &reservations_fops);
		rc->dbg->accept_f = debugfs_create_bool("accept", 0644,
							rc->dbg->root_d,
							&rc->dbg->accept);
		rc->dbg->drp_avail_f = debugfs_create_file("drp_avail", 0444,
							   rc->dbg->root_d, rc,
							   &drp_avail_fops);
	}
}

/**
 * uwb_dbg_add_rc - remove a radio controller's debug interface
 * @rc: the radio controller
 */
void uwb_dbg_del_rc(struct uwb_rc *rc)
{
	struct uwb_rsv *rsv, *t;

	if (rc->dbg == NULL)
		return;

	list_for_each_entry_safe(rsv, t, &rc->dbg->rsvs, pal_node) {
		uwb_rsv_destroy(rsv);
	}

	uwb_pal_unregister(rc, &rc->dbg->pal);

	if (root_dir) {
		debugfs_remove(rc->dbg->drp_avail_f);
		debugfs_remove(rc->dbg->accept_f);
		debugfs_remove(rc->dbg->reservations_f);
		debugfs_remove(rc->dbg->command_f);
		debugfs_remove(rc->dbg->root_d);
	}
}

/**
 * uwb_dbg_exit - initialize the debug interface sub-module
 */
void uwb_dbg_init(void)
{
	root_dir = debugfs_create_dir("uwb", NULL);
}

/**
 * uwb_dbg_exit - clean-up the debug interface sub-module
 */
void uwb_dbg_exit(void)
{
	debugfs_remove(root_dir);
}
