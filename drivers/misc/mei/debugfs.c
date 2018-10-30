/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2012-2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/debugfs.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "client.h"
#include "hw.h"

static ssize_t mei_dbgfs_read_meclients(struct file *fp, char __user *ubuf,
					size_t cnt, loff_t *ppos)
{
	struct mei_device *dev = fp->private_data;
	struct mei_me_client *me_cl;
	size_t bufsz = 1;
	char *buf;
	int i = 0;
	int pos = 0;
	int ret;

#define HDR \
"  |id|fix|         UUID                       |con|msg len|sb|refc|\n"

	down_read(&dev->me_clients_rwsem);
	list_for_each_entry(me_cl, &dev->me_clients, list)
		bufsz++;

	bufsz *= sizeof(HDR) + 1;
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		up_read(&dev->me_clients_rwsem);
		return -ENOMEM;
	}

	pos += scnprintf(buf + pos, bufsz - pos, HDR);
#undef HDR

	/*  if the driver is not enabled the list won't be consistent */
	if (dev->dev_state != MEI_DEV_ENABLED)
		goto out;

	list_for_each_entry(me_cl, &dev->me_clients, list) {

		if (mei_me_cl_get(me_cl)) {
			pos += scnprintf(buf + pos, bufsz - pos,
				"%2d|%2d|%3d|%pUl|%3d|%7d|%2d|%4d|\n",
				i++, me_cl->client_id,
				me_cl->props.fixed_address,
				&me_cl->props.protocol_name,
				me_cl->props.max_number_of_connections,
				me_cl->props.max_msg_length,
				me_cl->props.single_recv_buf,
				kref_read(&me_cl->refcnt));

			mei_me_cl_put(me_cl);
		}
	}

out:
	up_read(&dev->me_clients_rwsem);
	ret = simple_read_from_buffer(ubuf, cnt, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static const struct file_operations mei_dbgfs_fops_meclients = {
	.open = simple_open,
	.read = mei_dbgfs_read_meclients,
	.llseek = generic_file_llseek,
};

static ssize_t mei_dbgfs_read_active(struct file *fp, char __user *ubuf,
					size_t cnt, loff_t *ppos)
{
	struct mei_device *dev = fp->private_data;
	struct mei_cl *cl;
	size_t bufsz = 1;
	char *buf;
	int i = 0;
	int pos = 0;
	int ret;

#define HDR "   |me|host|state|rd|wr|wrq\n"

	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	/*
	 * if the driver is not enabled the list won't be consistent,
	 * we output empty table
	 */
	if (dev->dev_state == MEI_DEV_ENABLED)
		list_for_each_entry(cl, &dev->file_list, link)
			bufsz++;

	bufsz *= sizeof(HDR) + 1;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if  (!buf) {
		mutex_unlock(&dev->device_lock);
		return -ENOMEM;
	}

	pos += scnprintf(buf + pos, bufsz - pos, HDR);
#undef HDR

	/*  if the driver is not enabled the list won't be consistent */
	if (dev->dev_state != MEI_DEV_ENABLED)
		goto out;

	list_for_each_entry(cl, &dev->file_list, link) {

		pos += scnprintf(buf + pos, bufsz - pos,
			"%3d|%2d|%4d|%5d|%2d|%2d|%3u\n",
			i, mei_cl_me_id(cl), cl->host_client_id, cl->state,
			!list_empty(&cl->rd_completed), cl->writing_state,
			cl->tx_cb_queued);
		i++;
	}
out:
	mutex_unlock(&dev->device_lock);
	ret = simple_read_from_buffer(ubuf, cnt, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static const struct file_operations mei_dbgfs_fops_active = {
	.open = simple_open,
	.read = mei_dbgfs_read_active,
	.llseek = generic_file_llseek,
};

static ssize_t mei_dbgfs_read_devstate(struct file *fp, char __user *ubuf,
					size_t cnt, loff_t *ppos)
{
	struct mei_device *dev = fp->private_data;
	const size_t bufsz = 1024;
	char *buf = kzalloc(bufsz, GFP_KERNEL);
	int pos = 0;
	int ret;

	if  (!buf)
		return -ENOMEM;

	pos += scnprintf(buf + pos, bufsz - pos, "dev: %s\n",
			mei_dev_state_str(dev->dev_state));
	pos += scnprintf(buf + pos, bufsz - pos, "hbm: %s\n",
			mei_hbm_state_str(dev->hbm_state));

	if (dev->hbm_state >= MEI_HBM_ENUM_CLIENTS &&
	    dev->hbm_state <= MEI_HBM_STARTED) {
		pos += scnprintf(buf + pos, bufsz - pos, "hbm features:\n");
		pos += scnprintf(buf + pos, bufsz - pos, "\tPG: %01d\n",
				 dev->hbm_f_pg_supported);
		pos += scnprintf(buf + pos, bufsz - pos, "\tDC: %01d\n",
				 dev->hbm_f_dc_supported);
		pos += scnprintf(buf + pos, bufsz - pos, "\tIE: %01d\n",
				 dev->hbm_f_ie_supported);
		pos += scnprintf(buf + pos, bufsz - pos, "\tDOT: %01d\n",
				 dev->hbm_f_dot_supported);
		pos += scnprintf(buf + pos, bufsz - pos, "\tEV: %01d\n",
				 dev->hbm_f_ev_supported);
		pos += scnprintf(buf + pos, bufsz - pos, "\tFA: %01d\n",
				 dev->hbm_f_fa_supported);
		pos += scnprintf(buf + pos, bufsz - pos, "\tOS: %01d\n",
				 dev->hbm_f_os_supported);
		pos += scnprintf(buf + pos, bufsz - pos, "\tDR: %01d\n",
				 dev->hbm_f_dr_supported);
	}

	pos += scnprintf(buf + pos, bufsz - pos, "pg:  %s, %s\n",
			mei_pg_is_enabled(dev) ? "ENABLED" : "DISABLED",
			mei_pg_state_str(mei_pg_state(dev)));
	ret = simple_read_from_buffer(ubuf, cnt, ppos, buf, pos);
	kfree(buf);
	return ret;
}
static const struct file_operations mei_dbgfs_fops_devstate = {
	.open = simple_open,
	.read = mei_dbgfs_read_devstate,
	.llseek = generic_file_llseek,
};

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

static const struct file_operations mei_dbgfs_fops_allow_fa = {
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
 *
 * Return: 0 on success, <0 on failure.
 */
int mei_dbgfs_register(struct mei_device *dev, const char *name)
{
	struct dentry *dir, *f;

	dir = debugfs_create_dir(name, NULL);
	if (!dir)
		return -ENOMEM;

	dev->dbgfs_dir = dir;

	f = debugfs_create_file("meclients", S_IRUSR, dir,
				dev, &mei_dbgfs_fops_meclients);
	if (!f) {
		dev_err(dev->dev, "meclients: registration failed\n");
		goto err;
	}
	f = debugfs_create_file("active", S_IRUSR, dir,
				dev, &mei_dbgfs_fops_active);
	if (!f) {
		dev_err(dev->dev, "active: registration failed\n");
		goto err;
	}
	f = debugfs_create_file("devstate", S_IRUSR, dir,
				dev, &mei_dbgfs_fops_devstate);
	if (!f) {
		dev_err(dev->dev, "devstate: registration failed\n");
		goto err;
	}
	f = debugfs_create_file("allow_fixed_address", S_IRUSR | S_IWUSR, dir,
				&dev->allow_fixed_address,
				&mei_dbgfs_fops_allow_fa);
	if (!f) {
		dev_err(dev->dev, "allow_fixed_address: registration failed\n");
		goto err;
	}
	return 0;
err:
	mei_dbgfs_deregister(dev);
	return -ENODEV;
}

