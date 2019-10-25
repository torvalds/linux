// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ultra Wide Band
 * Debug support
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 * Copyright (C) 2008 Cambridge Silicon Radio Ltd.
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

#include "include/debug-cmd.h"
#include "uwb-internal.h"

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

	bool accept;
	struct list_head rsvs;

	struct dentry *root_d;
	struct dentry *command_f;
	struct dentry *reservations_f;
	struct dentry *accept_f;
	struct dentry *drp_avail_f;
	spinlock_t list_lock;
};

static struct dentry *root_dir;

static void uwb_dbg_rsv_cb(struct uwb_rsv *rsv)
{
	struct uwb_dbg *dbg = rsv->pal_priv;

	uwb_rsv_dump("debug", rsv);

	if (rsv->state == UWB_RSV_STATE_NONE) {
		spin_lock(&dbg->list_lock);
		list_del(&rsv->pal_node);
		spin_unlock(&dbg->list_lock);
		uwb_rsv_destroy(rsv);
	}
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

	rsv = uwb_rsv_create(rc, uwb_dbg_rsv_cb, rc->dbg);
	if (rsv == NULL) {
		uwb_dev_put(target);
		return -ENOMEM;
	}

	rsv->target.type  = UWB_RSV_TARGET_DEV;
	rsv->target.dev   = target;
	rsv->type         = cmd->type;
	rsv->max_mas      = cmd->max_mas;
	rsv->min_mas      = cmd->min_mas;
	rsv->max_interval = cmd->max_interval;

	ret = uwb_rsv_establish(rsv);
	if (ret)
		uwb_rsv_destroy(rsv);
	else {
		spin_lock(&(rc->dbg)->list_lock);
		list_add_tail(&rsv->pal_node, &rc->dbg->rsvs);
		spin_unlock(&(rc->dbg)->list_lock);
	}
	return ret;
}

static int cmd_rsv_terminate(struct uwb_rc *rc,
			     struct uwb_dbg_cmd_rsv_terminate *cmd)
{
	struct uwb_rsv *rsv, *found = NULL;
	int i = 0;

	spin_lock(&(rc->dbg)->list_lock);

	list_for_each_entry(rsv, &rc->dbg->rsvs, pal_node) {
		if (i == cmd->index) {
			found = rsv;
			uwb_rsv_get(found);
			break;
		}
		i++;
	}

	spin_unlock(&(rc->dbg)->list_lock);

	if (!found)
		return -EINVAL;

	uwb_rsv_terminate(found);
	uwb_rsv_put(found);

	return 0;
}

static int cmd_ie_add(struct uwb_rc *rc, struct uwb_dbg_cmd_ie *ie_to_add)
{
	return uwb_rc_ie_add(rc,
			     (const struct uwb_ie_hdr *) ie_to_add->data,
			     ie_to_add->len);
}

static int cmd_ie_rm(struct uwb_rc *rc, struct uwb_dbg_cmd_ie *ie_to_rm)
{
	return uwb_rc_ie_rm(rc, ie_to_rm->data[0]);
}

static ssize_t command_write(struct file *file, const char __user *buf,
			 size_t len, loff_t *off)
{
	struct uwb_rc *rc = file->private_data;
	struct uwb_dbg_cmd cmd;
	int ret = 0;
	
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
	case UWB_DBG_CMD_IE_ADD:
		ret = cmd_ie_add(rc, &cmd.ie_add);
		break;
	case UWB_DBG_CMD_IE_RM:
		ret = cmd_ie_rm(rc, &cmd.ie_rm);
		break;
	case UWB_DBG_CMD_RADIO_START:
		ret = uwb_radio_start(&rc->dbg->pal);
		break;
	case UWB_DBG_CMD_RADIO_STOP:
		uwb_radio_stop(&rc->dbg->pal);
		break;
	default:
		return -EINVAL;
	}

	return ret < 0 ? ret : len;
}

static const struct file_operations command_fops = {
	.open	= simple_open,
	.write  = command_write,
	.read   = NULL,
	.llseek = no_llseek,
	.owner  = THIS_MODULE,
};

static int reservations_show(struct seq_file *s, void *p)
{
	struct uwb_rc *rc = s->private;
	struct uwb_rsv *rsv;

	mutex_lock(&rc->rsvs_mutex);

	list_for_each_entry(rsv, &rc->reservations, rc_node) {
		struct uwb_dev_addr devaddr;
		char owner[UWB_ADDR_STRSIZE], target[UWB_ADDR_STRSIZE];
		bool is_owner;

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
		seq_printf(s, "  %*pb\n", UWB_NUM_MAS, rsv->mas.bm);
	}

	mutex_unlock(&rc->rsvs_mutex);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(reservations);

static int drp_avail_show(struct seq_file *s, void *p)
{
	struct uwb_rc *rc = s->private;

	seq_printf(s, "global:  %*pb\n", UWB_NUM_MAS, rc->drp_avail.global);
	seq_printf(s, "local:   %*pb\n", UWB_NUM_MAS, rc->drp_avail.local);
	seq_printf(s, "pending: %*pb\n", UWB_NUM_MAS, rc->drp_avail.pending);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(drp_avail);

static void uwb_dbg_channel_changed(struct uwb_pal *pal, int channel)
{
	struct device *dev = &pal->rc->uwb_dev.dev;

	if (channel > 0)
		dev_info(dev, "debug: channel %d started\n", channel);
	else
		dev_info(dev, "debug: channel stopped\n");
}

static void uwb_dbg_new_rsv(struct uwb_pal *pal, struct uwb_rsv *rsv)
{
	struct uwb_dbg *dbg = container_of(pal, struct uwb_dbg, pal);

	if (dbg->accept) {
		spin_lock(&dbg->list_lock);
		list_add_tail(&rsv->pal_node, &dbg->rsvs);
		spin_unlock(&dbg->list_lock);
		uwb_rsv_accept(rsv, uwb_dbg_rsv_cb, dbg);
	}
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
	spin_lock_init(&(rc->dbg)->list_lock);

	uwb_pal_init(&rc->dbg->pal);
	rc->dbg->pal.rc = rc;
	rc->dbg->pal.channel_changed = uwb_dbg_channel_changed;
	rc->dbg->pal.new_rsv = uwb_dbg_new_rsv;
	uwb_pal_register(&rc->dbg->pal);

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
 * uwb_dbg_del_rc - remove a radio controller's debug interface
 * @rc: the radio controller
 */
void uwb_dbg_del_rc(struct uwb_rc *rc)
{
	struct uwb_rsv *rsv, *t;

	if (rc->dbg == NULL)
		return;

	list_for_each_entry_safe(rsv, t, &rc->dbg->rsvs, pal_node) {
		uwb_rsv_terminate(rsv);
	}

	uwb_pal_unregister(&rc->dbg->pal);

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

/**
 * uwb_dbg_create_pal_dir - create a debugfs directory for a PAL
 * @pal: The PAL.
 */
struct dentry *uwb_dbg_create_pal_dir(struct uwb_pal *pal)
{
	struct uwb_rc *rc = pal->rc;

	if (root_dir && rc->dbg && rc->dbg->root_d && pal->name)
		return debugfs_create_dir(pal->name, rc->dbg->root_d);
	return NULL;
}
