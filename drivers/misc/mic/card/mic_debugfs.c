// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * Disclaimer: The codes contained in these modules may be specific to
 * the Intel Software Development Platform codenamed: Knights Ferry, and
 * the Intel product codenamed: Knights Corner, and are not backward
 * compatible with other Intel products. Additionally, Intel will NOT
 * support the codes or instruction set in future products.
 *
 * Intel MIC Card driver.
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
 * mic_intr_show - Send interrupts to host.
 */
static int mic_intr_show(struct seq_file *s, void *unused)
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

DEFINE_SHOW_ATTRIBUTE(mic_intr);

/**
 * mic_create_card_debug_dir - Initialize MIC debugfs entries.
 */
void __init mic_create_card_debug_dir(struct mic_driver *mdrv)
{
	if (!mic_dbg)
		return;

	mdrv->dbg_dir = debugfs_create_dir(mdrv->name, mic_dbg);

	debugfs_create_file("intr_test", 0444, mdrv->dbg_dir, mdrv,
			    &mic_intr_fops);
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
}

/**
 * mic_exit_card_debugfs - Uninitialize global debugfs entry
 */
void mic_exit_card_debugfs(void)
{
	debugfs_remove(mic_dbg);
}
