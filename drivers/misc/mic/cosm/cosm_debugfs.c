/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2015 Intel Corporation.
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
 * Intel MIC Coprocessor State Management (COSM) Driver
 *
 */

#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/io.h>
#include "cosm_main.h"

/* Debugfs parent dir */
static struct dentry *cosm_dbg;

/**
 * log_buf_show - Display MIC kernel log buffer
 *
 * log_buf addr/len is read from System.map by user space
 * and populated in sysfs entries.
 */
static int log_buf_show(struct seq_file *s, void *unused)
{
	void __iomem *log_buf_va;
	int __iomem *log_buf_len_va;
	struct cosm_device *cdev = s->private;
	void *kva;
	int size;
	u64 aper_offset;

	if (!cdev || !cdev->log_buf_addr || !cdev->log_buf_len)
		goto done;

	mutex_lock(&cdev->cosm_mutex);
	switch (cdev->state) {
	case MIC_BOOTING:
	case MIC_ONLINE:
	case MIC_SHUTTING_DOWN:
		break;
	default:
		goto unlock;
	}

	/*
	 * Card kernel will never be relocated and any kernel text/data mapping
	 * can be translated to phys address by subtracting __START_KERNEL_map.
	 */
	aper_offset = (u64)cdev->log_buf_len - __START_KERNEL_map;
	log_buf_len_va = cdev->hw_ops->aper(cdev)->va + aper_offset;
	aper_offset = (u64)cdev->log_buf_addr - __START_KERNEL_map;
	log_buf_va = cdev->hw_ops->aper(cdev)->va + aper_offset;

	size = ioread32(log_buf_len_va);
	kva = kmalloc(size, GFP_KERNEL);
	if (!kva)
		goto unlock;

	memcpy_fromio(kva, log_buf_va, size);
	seq_write(s, kva, size);
	kfree(kva);
unlock:
	mutex_unlock(&cdev->cosm_mutex);
done:
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(log_buf);

/**
 * force_reset_show - Force MIC reset
 *
 * Invokes the force_reset COSM bus op instead of the standard reset
 * op in case a force reset of the MIC device is required
 */
static int force_reset_show(struct seq_file *s, void *pos)
{
	struct cosm_device *cdev = s->private;

	cosm_stop(cdev, true);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(force_reset);

void cosm_create_debug_dir(struct cosm_device *cdev)
{
	char name[16];

	if (!cosm_dbg)
		return;

	scnprintf(name, sizeof(name), "mic%d", cdev->index);
	cdev->dbg_dir = debugfs_create_dir(name, cosm_dbg);
	if (!cdev->dbg_dir)
		return;

	debugfs_create_file("log_buf", 0444, cdev->dbg_dir, cdev,
			    &log_buf_fops);
	debugfs_create_file("force_reset", 0444, cdev->dbg_dir, cdev,
			    &force_reset_fops);
}

void cosm_delete_debug_dir(struct cosm_device *cdev)
{
	if (!cdev->dbg_dir)
		return;

	debugfs_remove_recursive(cdev->dbg_dir);
}

void cosm_init_debugfs(void)
{
	cosm_dbg = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!cosm_dbg)
		pr_err("can't create debugfs dir\n");
}

void cosm_exit_debugfs(void)
{
	debugfs_remove(cosm_dbg);
}
