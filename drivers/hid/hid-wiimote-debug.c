/*
 * Debug support for HID Nintendo Wiimote devices
 * Copyright (c) 2011 David Herrmann
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include "hid-wiimote.h"

struct wiimote_debug {
	struct wiimote_data *wdata;
	struct dentry *eeprom;
};

static int wiidebug_eeprom_open(struct inode *i, struct file *f)
{
	f->private_data = i->i_private;
	return 0;
}

static ssize_t wiidebug_eeprom_read(struct file *f, char __user *u, size_t s,
								loff_t *off)
{
	struct wiimote_debug *dbg = f->private_data;
	struct wiimote_data *wdata = dbg->wdata;
	unsigned long flags;
	ssize_t ret;
	char buf[16];
	__u16 size;

	if (s == 0)
		return -EINVAL;
	if (*off > 0xffffff)
		return 0;
	if (s > 16)
		s = 16;

	ret = wiimote_cmd_acquire(wdata);
	if (ret)
		return ret;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.cmd_read_size = s;
	wdata->state.cmd_read_buf = buf;
	wiimote_cmd_set(wdata, WIIPROTO_REQ_RMEM, *off & 0xffff);
	wiiproto_req_reeprom(wdata, *off, s);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	ret = wiimote_cmd_wait(wdata);
	if (!ret)
		size = wdata->state.cmd_read_size;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.cmd_read_buf = NULL;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	wiimote_cmd_release(wdata);

	if (ret)
		return ret;
	else if (size == 0)
		return -EIO;

	if (copy_to_user(u, buf, size))
		return -EFAULT;

	*off += size;
	ret = size;

	return ret;
}

static const struct file_operations wiidebug_eeprom_fops = {
	.owner = THIS_MODULE,
	.open = wiidebug_eeprom_open,
	.read = wiidebug_eeprom_read,
	.llseek = generic_file_llseek,
};

int wiidebug_init(struct wiimote_data *wdata)
{
	struct wiimote_debug *dbg;
	unsigned long flags;

	dbg = kzalloc(sizeof(*dbg), GFP_KERNEL);
	if (!dbg)
		return -ENOMEM;

	dbg->wdata = wdata;

	dbg->eeprom = debugfs_create_file("eeprom", S_IRUSR,
		dbg->wdata->hdev->debug_dir, dbg, &wiidebug_eeprom_fops);
	if (!dbg->eeprom) {
		kfree(dbg);
		return -ENOMEM;
	}

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->debug = dbg;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;
}

void wiidebug_deinit(struct wiimote_data *wdata)
{
	struct wiimote_debug *dbg = wdata->debug;
	unsigned long flags;

	if (!dbg)
		return;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->debug = NULL;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	debugfs_remove(dbg->eeprom);
	kfree(dbg);
}
