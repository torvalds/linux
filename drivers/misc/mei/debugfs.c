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

#define HDR "  |id|fix|         UUID                       |con|msg len|sb|\n"

	mutex_lock(&dev->device_lock);

	list_for_each_entry(me_cl, &dev->me_clients, list)
		bufsz++;

	bufsz *= sizeof(HDR) + 1;
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		mutex_unlock(&dev->device_lock);
		return -ENOMEM;
	}

	pos += scnprintf(buf + pos, bufsz - pos, HDR);

	/*  if the driver is not enabled the list won't be consistent */
	if (dev->dev_state != MEI_DEV_ENABLED)
		goto out;

	list_for_each_entry(me_cl, &dev->me_clients, list) {

		pos += scnprintf(buf + pos, bufsz - pos,
			"%2d|%2d|%3d|%pUl|%3d|%7d|%2d|\n",
			i++, me_cl->client_id,
			me_cl->props.fixed_address,
			&me_cl->props.protocol_name,
			me_cl->props.max_number_of_connections,
			me_cl->props.max_msg_length,
			me_cl->props.single_recv_buf);
	}
out:
	mutex_unlock(&dev->device_lock);
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
	const size_t bufsz = 1024;
	char *buf;
	int i = 0;
	int pos = 0;
	int ret;

	if (!dev)
		return -ENODEV;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if  (!buf)
		return -ENOMEM;

	pos += scnprintf(buf + pos, bufsz - pos,
			"  |me|host|state|rd|wr|\n");

	mutex_lock(&dev->device_lock);

	/*  if the driver is not enabled the list won't be consistent */
	if (dev->dev_state != MEI_DEV_ENABLED)
		goto out;

	list_for_each_entry(cl, &dev->file_list, link) {

		pos += scnprintf(buf + pos, bufsz - pos,
			"%2d|%2d|%4d|%5d|%2d|%2d|\n",
			i, cl->me_client_id, cl->host_client_id, cl->state,
			cl->reading_state, cl->writing_state);
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

	f = debugfs_create_file("meclients", S_IRUSR, dir,
				dev, &mei_dbgfs_fops_meclients);
	if (!f) {
		dev_err(dev->dev, "meclients: registration failed\n");
		goto err;
	}
	f = debugfs_create_file("active", S_IRUSR, dir,
				dev, &mei_dbgfs_fops_active);
	if (!f) {
		dev_err(dev->dev, "meclients: registration failed\n");
		goto err;
	}
	f = debugfs_create_file("devstate", S_IRUSR, dir,
				dev, &mei_dbgfs_fops_devstate);
	if (!f) {
		dev_err(dev->dev, "devstate: registration failed\n");
		goto err;
	}
	dev->dbgfs_dir = dir;
	return 0;
err:
	mei_dbgfs_deregister(dev);
	return -ENODEV;
}

