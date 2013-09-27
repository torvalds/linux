/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Disclaimer: The codes contained in these modules may be specific to
 * the Intel Software Development Platform codenamed: Knights Ferry, and
 * the Intel product codenamed: Knights Corner, and are not backward
 * compatible with other Intel products. Additionally, Intel will NOT
 * support the codes or instruction set in future products.
 *
 * Intel MIC Card driver.
 *
 */
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/device.h>

#include "../common/mic_dev.h"
#include "mic_device.h"

/* Debugfs parent dir */
static struct dentry *mic_dbg;

/**
 * mic_intr_test - Send interrupts to host.
 */
static int mic_intr_test(struct seq_file *s, void *unused)
{
	struct mic_driver *mdrv = s->private;
	struct mic_device *mdev = &mdrv->mdev;

	mic_send_intr(mdev, 0);
	msleep(1000);
	mic_send_intr(mdev, 1);
	msleep(1000);
	mic_send_intr(mdev, 2);
	msleep(1000);
	mic_send_intr(mdev, 3);
	msleep(1000);

	return 0;
}

static int mic_intr_test_open(struct inode *inode, struct file *file)
{
	return single_open(file, mic_intr_test, inode->i_private);
}

static int mic_intr_test_release(struct inode *inode, struct file *file)
{
	return single_release(inode, file);
}

static const struct file_operations intr_test_ops = {
	.owner   = THIS_MODULE,
	.open    = mic_intr_test_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = mic_intr_test_release
};

/**
 * mic_create_card_debug_dir - Initialize MIC debugfs entries.
 */
void __init mic_create_card_debug_dir(struct mic_driver *mdrv)
{
	struct dentry *d;

	if (!mic_dbg)
		return;

	mdrv->dbg_dir = debugfs_create_dir(mdrv->name, mic_dbg);
	if (!mdrv->dbg_dir) {
		dev_err(mdrv->dev, "Cant create dbg_dir %s\n", mdrv->name);
		return;
	}

	d = debugfs_create_file("intr_test", 0444, mdrv->dbg_dir,
		mdrv, &intr_test_ops);

	if (!d) {
		dev_err(mdrv->dev,
			"Cant create dbg intr_test %s\n", mdrv->name);
		return;
	}
}

/**
 * mic_delete_card_debug_dir - Uninitialize MIC debugfs entries.
 */
void mic_delete_card_debug_dir(struct mic_driver *mdrv)
{
	if (!mdrv->dbg_dir)
		return;

	debugfs_remove_recursive(mdrv->dbg_dir);
}

/**
 * mic_init_card_debugfs - Initialize global debugfs entry.
 */
void __init mic_init_card_debugfs(void)
{
	mic_dbg = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!mic_dbg)
		pr_err("can't create debugfs dir\n");
}

/**
 * mic_exit_card_debugfs - Uninitialize global debugfs entry
 */
void mic_exit_card_debugfs(void)
{
	debugfs_remove(mic_dbg);
}
