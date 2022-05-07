// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012-2016, Intel Corporation. All rights reserved
 * Intel Management Engine Interface (Intel MEI) Linux driver
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "client.h"
#include "hw.h"

static int mei_dbgfs_meclients_show(struct seq_file *m, void *unused)
{
	struct mei_device *dev = m->private;
	struct mei_me_client *me_cl;
	int i = 0;

	if (!dev)
		return -ENODEV;

	down_read(&dev->me_clients_rwsem);

	seq_puts(m, "  |id|fix|         UUID                       |con|msg len|sb|refc|vt|\n");

	/*  if the driver is not enabled the list won't be consistent */
	if (dev->dev_state != MEI_DEV_ENABLED)
		goto out;

	list_for_each_entry(me_cl, &dev->me_clients, list) {
		if (!mei_me_cl_get(me_cl))
			continue;

		seq_printf(m, "%2d|%2d|%3d|%pUl|%3d|%7d|%2d|%4d|%2d|\n",
			   i++, me_cl->client_id,
			   me_cl->props.fixed_address,
			   &me_cl->props.protocol_name,
			   me_cl->props.max_number_of_connections,
			   me_cl->props.max_msg_length,
			   me_cl->props.single_recv_buf,
			   kref_read(&me_cl->refcnt),
			   me_cl->props.vt_supported);
		mei_me_cl_put(me_cl);
	}

out:
	up_read(&dev->me_clients_rwsem);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mei_dbgfs_meclients);

static int mei_dbgfs_active_show(struct seq_file *m, void *unused)
{
	struct mei_device *dev = m->private;
	struct mei_cl *cl;
	int i = 0;

	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	seq_puts(m, "   |me|host|state|rd|wr|wrq\n");

	/*  if the driver is not enabled the list won't be consistent */
	if (dev->dev_state != MEI_DEV_ENABLED)
		goto out;

	list_for_each_entry(cl, &dev->file_list, link) {

		seq_printf(m, "%3d|%2d|%4d|%5d|%2d|%2d|%3u\n",
			   i, mei_cl_me_id(cl), cl->host_client_id, cl->state,
			   !list_empty(&cl->rd_completed), cl->writing_state,
			   cl->tx_cb_queued);
		i++;
	}
out:
	mutex_unlock(&dev->device_lock);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mei_dbgfs_active);

static int mei_dbgfs_devstate_show(struct seq_file *m, void *unused)
{
	struct mei_device *dev = m->private;

	seq_printf(m, "dev: %s\n", mei_dev_state_str(dev->dev_state));
	seq_printf(m, "hbm: %s\n", mei_hbm_state_str(dev->hbm_state));

	if (dev->hbm_state >= MEI_HBM_ENUM_CLIENTS &&
	    dev->hbm_state <= MEI_HBM_STARTED) {
		seq_puts(m, "hbm features:\n");
		seq_printf(m, "\tPG: %01d\n", dev->hbm_f_pg_supported);
		seq_printf(m, "\tDC: %01d\n", dev->hbm_f_dc_supported);
		seq_printf(m, "\tIE: %01d\n", dev->hbm_f_ie_supported);
		seq_printf(m, "\tDOT: %01d\n", dev->hbm_f_dot_supported);
		seq_printf(m, "\tEV: %01d\n", dev->hbm_f_ev_supported);
		seq_printf(m, "\tFA: %01d\n", dev->hbm_f_fa_supported);
		seq_printf(m, "\tOS: %01d\n", dev->hbm_f_os_supported);
		seq_printf(m, "\tDR: %01d\n", dev->hbm_f_dr_supported);
		seq_printf(m, "\tVT: %01d\n", dev->hbm_f_vt_supported);
		seq_printf(m, "\tCAP: %01d\n", dev->hbm_f_cap_supported);
	}

	seq_printf(m, "pg:  %s, %s\n",
		   mei_pg_is_enabled(dev) ? "ENABLED" : "DISABLED",
		   mei_pg_state_str(mei_pg_state(dev)));
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mei_dbgfs_devstate);

static ssize_t mei_dbgfs_write_allow_fa(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct mei_device *dev;
	int ret;

	dev = container_of(file->private_data,
			   struct mei_device, allow_fixed_address);

	ret = debugfs_write_file_bool(file, user_buf, count, ppos);
	if (ret < 0)
		return ret;
	dev->override_fixed_address = true;
	return ret;
}

static const struct file_operations mei_dbgfs_allow_fa_fops = {
	.open = simple_open,
	.read = debugfs_read_file_bool,
	.write = mei_dbgfs_write_allow_fa,
	.llseek = generic_file_llseek,
};

/**
 * mei_dbgfs_deregister - Remove the debugfs files and directories
 *
 * @dev: the mei device structure
 */
void mei_dbgfs_deregister(struct mei_device *dev)
{
	if (!dev->dbgfs_dir)
		return;
	debugfs_remove_recursive(dev->dbgfs_dir);
	dev->dbgfs_dir = NULL;
}

/**
 * mei_dbgfs_register - Add the debugfs files
 *
 * @dev: the mei device structure
 * @name: the mei device name
 */
void mei_dbgfs_register(struct mei_device *dev, const char *name)
{
	struct dentry *dir;

	dir = debugfs_create_dir(name, NULL);
	dev->dbgfs_dir = dir;

	debugfs_create_file("meclients", S_IRUSR, dir, dev,
			    &mei_dbgfs_meclients_fops);
	debugfs_create_file("active", S_IRUSR, dir, dev,
			    &mei_dbgfs_active_fops);
	debugfs_create_file("devstate", S_IRUSR, dir, dev,
			    &mei_dbgfs_devstate_fops);
	debugfs_create_file("allow_fixed_address", S_IRUSR | S_IWUSR, dir,
			    &dev->allow_fixed_address,
			    &mei_dbgfs_allow_fa_fops);
}
