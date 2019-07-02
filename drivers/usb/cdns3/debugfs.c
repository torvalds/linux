// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence USBSS DRD Controller DebugFS filer.
 *
 * Copyright (C) 2018-2019 Cadence.
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 */

#include <linux/types.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#include "core.h"
#include "gadget.h"
#include "drd.h"

static const char *const cdns3_mode[] = {
	[USB_DR_MODE_UNKNOWN]		= "unknown",
	[USB_DR_MODE_OTG]		= "otg",
	[USB_DR_MODE_HOST]		= "host",
	[USB_DR_MODE_PERIPHERAL]	= "device",
};

static int cdns3_mode_show(struct seq_file *s, void *unused)
{
	struct cdns3 *cdns = s->private;

	seq_puts(s, cdns3_mode[cdns->current_dr_mode]);

	return 0;
}

static int cdns3_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, cdns3_mode_show, inode->i_private);
}

static ssize_t cdns3_mode_write(struct file *file,
				const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct seq_file	 *s = file->private_data;
	struct cdns3 *cdns = s->private;
	char buf[32];
	int ret;

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (cdns->debug_disable) {
		dev_err(cdns->dev,
			"Mode can't be changed when disable is set\n");
		return -EPERM;
	}

	ret = match_string(cdns3_mode, ARRAY_SIZE(cdns3_mode), buf);
	if (ret < 0 || ret == USB_DR_MODE_UNKNOWN) {
		dev_err(cdns->dev, "Failed: incorrect mode setting\n");
		return -EINVAL;
	}

	if (cdns->current_dr_mode != ret) {
		cdns->desired_dr_mode = ret;
		cdns3_role_stop(cdns);
		ret = cdns3_drd_update_mode(cdns);
		if (ret)
			return ret;

		queue_work(system_freezable_wq, &cdns->role_switch_wq);
	}

	return count;
}

static const struct file_operations cdns3_mode_fops = {
	.open			= cdns3_mode_open,
	.write			= cdns3_mode_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int cdns3_disable_show(struct seq_file *s, void *unused)
{
	struct cdns3 *cdns = s->private;

	if (!cdns->debug_disable)
		seq_puts(s, "0\n");
	else
		seq_puts(s, "1\n");

	return 0;
}

static ssize_t cdns3_disable_write(struct file *file,
				   const char __user *ubuf,
				   size_t count, loff_t *ppos)
{
	struct seq_file	 *s = file->private_data;
	struct cdns3 *cdns = s->private;
	bool disable;
	char buf[16];

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (kstrtobool(buf, &disable)) {
		dev_err(cdns->dev, "wrong setting\n");
		return -EINVAL;
	}

	if (disable != cdns->debug_disable) {
		cdns->debug_disable = disable;
		queue_work(system_freezable_wq, &cdns->role_switch_wq);
	}

	return count;
}

static int cdns3_disable_open(struct inode *inode, struct file *file)
{
	return single_open(file, cdns3_disable_show, inode->i_private);
}

static const struct file_operations cdns3_disable_fops = {
	.open			= cdns3_disable_open,
	.write			= cdns3_disable_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

void cdns3_debugfs_init(struct cdns3 *cdns)
{
	struct dentry *root;

	root = debugfs_create_dir(dev_name(cdns->dev), NULL);
	cdns->root = root;
	if (IS_ENABLED(CONFIG_USB_CDNS3_GADGET) &&
	    IS_ENABLED(CONFIG_USB_CDNS3_HOST))
		debugfs_create_file("mode", 0644, root, cdns,
				    &cdns3_mode_fops);

	debugfs_create_file("disable", 0644, root, cdns,
			    &cdns3_disable_fops);
}

void cdns3_debugfs_exit(struct cdns3 *cdns)
{
	debugfs_remove_recursive(cdns->root);
}
