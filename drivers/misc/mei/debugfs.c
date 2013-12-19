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
#include <linux/pci.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "hw.h"

static ssize_t mei_dbgfs_read_meclients(struct file *fp, char __user *ubuf,
					size_t cnt, loff_t *ppos)
{
	struct mei_device *dev = fp->private_data;
	struct mei_me_client *cl;
	const size_t bufsz = 1024;
	char *buf = kzalloc(bufsz, GFP_KERNEL);
	int i;
	int pos = 0;
	int ret;

	if  (!buf)
		return -ENOMEM;

	pos += scnprintf(buf + pos, bufsz - pos,
			"  |id|addr|         UUID                       |con|msg len|\n");

	mutex_lock(&dev->device_lock);

	/*  if the driver is not enabled the list won't b consitent */
	if (dev->dev_state != MEI_DEV_ENABLED)
		goto out;

	for (i = 0; i < dev->me_clients_num; i++) {
		cl = &dev->me_clients[i];

		/* skip me clients that cannot be connected */
		if (cl->props.max_number_of_connections == 0)
			continue;

		pos += scnprintf(buf + pos, bufsz - pos,
			"%2d|%2d|%4d|%pUl|%3d|%7d|\n",
			i, cl->client_id,
			cl->props.fixed_address,
			&cl->props.protocol_name,
			cl->props.max_number_of_connections,
			cl->props.max_msg_length);
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

	pos += scnprintf(buf + pos, bufsz - pos, "%s\n",
			mei_dev_state_str(dev->dev_state));
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
 * @mei - pointer to mei device private dat
 */
void mei_dbgfs_deregister(struct mei_device *dev)
{
	if (!dev->dbgfs_dir)
		return;
	debugfs_remove_recursive(dev->dbgfs_dir);
	dev->dbgfs_dir = NULL;
}

/**
 * Add the debugfs files
 *
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
		dev_err(&dev->pdev->dev, "meclients: registration failed\n");
		goto err;
	}
	f = debugfs_create_file("devstate", S_IRUSR, dir,
				dev, &mei_dbgfs_fops_devstate);
	if (!f) {
		dev_err(&dev->pdev->dev, "devstate: registration failed\n");
		goto err;
	}
	dev->dbgfs_dir = dir;
	return 0;
err:
	mei_dbgfs_deregister(dev);
	return -ENODEV;
}

