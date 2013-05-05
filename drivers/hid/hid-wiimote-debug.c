/*
 * Debug support for HID Nintendo Wii / Wii U peripherals
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include "hid-wiimote.h"

struct wiimote_debug {
	struct wiimote_data *wdata;
	struct dentry *eeprom;
	struct dentry *drm;
};

static ssize_t wiidebug_eeprom_read(struct file *f, char __user *u, size_t s,
								loff_t *off)
{
	struct wiimote_debug *dbg = f->private_data;
	struct wiimote_data *wdata = dbg->wdata;
	unsigned long flags;
	ssize_t ret;
	char buf[16];
	__u16 size = 0;

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
	.open = simple_open,
	.read = wiidebug_eeprom_read,
	.llseek = generic_file_llseek,
};

static const char *wiidebug_drmmap[] = {
	[WIIPROTO_REQ_NULL] = "NULL",
	[WIIPROTO_REQ_DRM_K] = "K",
	[WIIPROTO_REQ_DRM_KA] = "KA",
	[WIIPROTO_REQ_DRM_KE] = "KE",
	[WIIPROTO_REQ_DRM_KAI] = "KAI",
	[WIIPROTO_REQ_DRM_KEE] = "KEE",
	[WIIPROTO_REQ_DRM_KAE] = "KAE",
	[WIIPROTO_REQ_DRM_KIE] = "KIE",
	[WIIPROTO_REQ_DRM_KAIE] = "KAIE",
	[WIIPROTO_REQ_DRM_E] = "E",
	[WIIPROTO_REQ_DRM_SKAI1] = "SKAI1",
	[WIIPROTO_REQ_DRM_SKAI2] = "SKAI2",
	[WIIPROTO_REQ_MAX] = NULL
};

static int wiidebug_drm_show(struct seq_file *f, void *p)
{
	struct wiimote_debug *dbg = f->private;
	const char *str = NULL;
	unsigned long flags;
	__u8 drm;

	spin_lock_irqsave(&dbg->wdata->state.lock, flags);
	drm = dbg->wdata->state.drm;
	spin_unlock_irqrestore(&dbg->wdata->state.lock, flags);

	if (drm < WIIPROTO_REQ_MAX)
		str = wiidebug_drmmap[drm];
	if (!str)
		str = "unknown";

	seq_printf(f, "%s\n", str);

	return 0;
}

static int wiidebug_drm_open(struct inode *i, struct file *f)
{
	return single_open(f, wiidebug_drm_show, i->i_private);
}

static ssize_t wiidebug_drm_write(struct file *f, const char __user *u,
							size_t s, loff_t *off)
{
	struct seq_file *sf = f->private_data;
	struct wiimote_debug *dbg = sf->private;
	unsigned long flags;
	char buf[16];
	ssize_t len;
	int i;

	if (s == 0)
		return -EINVAL;

	len = min((size_t) 15, s);
	if (copy_from_user(buf, u, len))
		return -EFAULT;

	buf[15] = 0;

	for (i = 0; i < WIIPROTO_REQ_MAX; ++i) {
		if (!wiidebug_drmmap[i])
			continue;
		if (!strcasecmp(buf, wiidebug_drmmap[i]))
			break;
	}

	if (i == WIIPROTO_REQ_MAX)
		i = simple_strtoul(buf, NULL, 10);

	spin_lock_irqsave(&dbg->wdata->state.lock, flags);
	dbg->wdata->state.flags &= ~WIIPROTO_FLAG_DRM_LOCKED;
	wiiproto_req_drm(dbg->wdata, (__u8) i);
	if (i != WIIPROTO_REQ_NULL)
		dbg->wdata->state.flags |= WIIPROTO_FLAG_DRM_LOCKED;
	spin_unlock_irqrestore(&dbg->wdata->state.lock, flags);

	return len;
}

static const struct file_operations wiidebug_drm_fops = {
	.owner = THIS_MODULE,
	.open = wiidebug_drm_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = wiidebug_drm_write,
	.release = single_release,
};

int wiidebug_init(struct wiimote_data *wdata)
{
	struct wiimote_debug *dbg;
	unsigned long flags;
	int ret = -ENOMEM;

	dbg = kzalloc(sizeof(*dbg), GFP_KERNEL);
	if (!dbg)
		return -ENOMEM;

	dbg->wdata = wdata;

	dbg->eeprom = debugfs_create_file("eeprom", S_IRUSR,
		dbg->wdata->hdev->debug_dir, dbg, &wiidebug_eeprom_fops);
	if (!dbg->eeprom)
		goto err;

	dbg->drm = debugfs_create_file("drm", S_IRUSR,
			dbg->wdata->hdev->debug_dir, dbg, &wiidebug_drm_fops);
	if (!dbg->drm)
		goto err_drm;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->debug = dbg;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;

err_drm:
	debugfs_remove(dbg->eeprom);
err:
	kfree(dbg);
	return ret;
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

	debugfs_remove(dbg->drm);
	debugfs_remove(dbg->eeprom);
	kfree(dbg);
}
