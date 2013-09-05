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
 * Intel MIC Host driver.
 *
 */
#include <linux/debugfs.h>
#include <linux/pci.h>
#include <linux/seq_file.h>

#include <linux/mic_common.h>
#include "../common/mic_device.h"
#include "mic_device.h"
#include "mic_smpt.h"

/* Debugfs parent dir */
static struct dentry *mic_dbg;

/**
 * mic_log_buf_show - Display MIC kernel log buffer.
 *
 * log_buf addr/len is read from System.map by user space
 * and populated in sysfs entries.
 */
static int mic_log_buf_show(struct seq_file *s, void *unused)
{
	void __iomem *log_buf_va;
	int __iomem *log_buf_len_va;
	struct mic_device *mdev = s->private;
	void *kva;
	int size;
	unsigned long aper_offset;

	if (!mdev || !mdev->log_buf_addr || !mdev->log_buf_len)
		goto done;
	/*
	 * Card kernel will never be relocated and any kernel text/data mapping
	 * can be translated to phys address by subtracting __START_KERNEL_map.
	 */
	aper_offset = (unsigned long)mdev->log_buf_len - __START_KERNEL_map;
	log_buf_len_va = mdev->aper.va + aper_offset;
	aper_offset = (unsigned long)mdev->log_buf_addr - __START_KERNEL_map;
	log_buf_va = mdev->aper.va + aper_offset;
	size = ioread32(log_buf_len_va);

	kva = kmalloc(size, GFP_KERNEL);
	if (!kva)
		goto done;
	mutex_lock(&mdev->mic_mutex);
	memcpy_fromio(kva, log_buf_va, size);
	switch (mdev->state) {
	case MIC_ONLINE:
		/* Fall through */
	case MIC_SHUTTING_DOWN:
		seq_write(s, kva, size);
		break;
	default:
		break;
	}
	mutex_unlock(&mdev->mic_mutex);
	kfree(kva);
done:
	return 0;
}

static int mic_log_buf_open(struct inode *inode, struct file *file)
{
	return single_open(file, mic_log_buf_show, inode->i_private);
}

static int mic_log_buf_release(struct inode *inode, struct file *file)
{
	return single_release(inode, file);
}

static const struct file_operations log_buf_ops = {
	.owner   = THIS_MODULE,
	.open    = mic_log_buf_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = mic_log_buf_release
};

static int mic_smpt_show(struct seq_file *s, void *pos)
{
	int i;
	struct mic_device *mdev = s->private;
	unsigned long flags;

	seq_printf(s, "MIC %-2d |%-10s| %-14s %-10s\n",
		mdev->id, "SMPT entry", "SW DMA addr", "RefCount");
	seq_puts(s, "====================================================\n");

	if (mdev->smpt) {
		struct mic_smpt_info *smpt_info = mdev->smpt;
		spin_lock_irqsave(&smpt_info->smpt_lock, flags);
		for (i = 0; i < smpt_info->info.num_reg; i++) {
			seq_printf(s, "%9s|%-10d| %-#14llx %-10lld\n",
				" ",  i, smpt_info->entry[i].dma_addr,
				smpt_info->entry[i].ref_count);
		}
		spin_unlock_irqrestore(&smpt_info->smpt_lock, flags);
	}
	seq_puts(s, "====================================================\n");
	return 0;
}

static int mic_smpt_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, mic_smpt_show, inode->i_private);
}

static int mic_smpt_debug_release(struct inode *inode, struct file *file)
{
	return single_release(inode, file);
}

static const struct file_operations smpt_file_ops = {
	.owner   = THIS_MODULE,
	.open    = mic_smpt_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = mic_smpt_debug_release
};

static int mic_soft_reset_show(struct seq_file *s, void *pos)
{
	struct mic_device *mdev = s->private;

	mic_stop(mdev, true);
	return 0;
}

static int mic_soft_reset_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, mic_soft_reset_show, inode->i_private);
}

static int mic_soft_reset_debug_release(struct inode *inode, struct file *file)
{
	return single_release(inode, file);
}

static const struct file_operations soft_reset_ops = {
	.owner   = THIS_MODULE,
	.open    = mic_soft_reset_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = mic_soft_reset_debug_release
};

static int mic_post_code_show(struct seq_file *s, void *pos)
{
	struct mic_device *mdev = s->private;
	u32 reg = mdev->ops->get_postcode(mdev);

	seq_printf(s, "%c%c", reg & 0xff, (reg >> 8) & 0xff);
	return 0;
}

static int mic_post_code_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, mic_post_code_show, inode->i_private);
}

static int mic_post_code_debug_release(struct inode *inode, struct file *file)
{
	return single_release(inode, file);
}

static const struct file_operations post_code_ops = {
	.owner   = THIS_MODULE,
	.open    = mic_post_code_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = mic_post_code_debug_release
};

static int mic_dp_show(struct seq_file *s, void *pos)
{
	struct mic_device *mdev = s->private;
	struct mic_bootparam *bootparam = mdev->dp;

	seq_printf(s, "Bootparam: magic 0x%x\n",
		bootparam->magic);
	seq_printf(s, "Bootparam: h2c_shutdown_db %d\n",
		bootparam->h2c_shutdown_db);
	seq_printf(s, "Bootparam: h2c_config_db %d\n",
		bootparam->h2c_config_db);
	seq_printf(s, "Bootparam: c2h_shutdown_db %d\n",
		bootparam->c2h_shutdown_db);
	seq_printf(s, "Bootparam: shutdown_status %d\n",
		bootparam->shutdown_status);
	seq_printf(s, "Bootparam: shutdown_card %d\n",
		bootparam->shutdown_card);

	return 0;
}

static int mic_dp_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, mic_dp_show, inode->i_private);
}

static int mic_dp_debug_release(struct inode *inode, struct file *file)
{
	return single_release(inode, file);
}

static const struct file_operations dp_ops = {
	.owner   = THIS_MODULE,
	.open    = mic_dp_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = mic_dp_debug_release
};

static int mic_msi_irq_info_show(struct seq_file *s, void *pos)
{
	struct mic_device *mdev  = s->private;
	int reg;
	int i, j;
	u16 entry;
	u16 vector;
	struct pci_dev *pdev = container_of(mdev->sdev->parent,
		struct pci_dev, dev);

	if (pci_dev_msi_enabled(pdev)) {
		for (i = 0; i < mdev->irq_info.num_vectors; i++) {
			if (pdev->msix_enabled) {
				entry = mdev->irq_info.msix_entries[i].entry;
				vector = mdev->irq_info.msix_entries[i].vector;
			} else {
				entry = 0;
				vector = pdev->irq;
			}

			reg = mdev->intr_ops->read_msi_to_src_map(mdev, entry);

			seq_printf(s, "%s %-10d %s %-10d MXAR[%d]: %08X\n",
				"IRQ:", vector, "Entry:", entry, i, reg);

			seq_printf(s, "%-10s", "offset:");
			for (j = (MIC_NUM_OFFSETS - 1); j >= 0; j--)
				seq_printf(s, "%4d ", j);
			seq_puts(s, "\n");


			seq_printf(s, "%-10s", "count:");
			for (j = (MIC_NUM_OFFSETS - 1); j >= 0; j--)
				seq_printf(s, "%4d ",
				(mdev->irq_info.mic_msi_map[i] & BIT(j)) ?
					1 : 0);
			seq_puts(s, "\n\n");
		}
	} else {
		seq_puts(s, "MSI/MSIx interrupts not enabled\n");
	}

	return 0;

}

static int mic_msi_irq_info_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, mic_msi_irq_info_show, inode->i_private);
}

static int
mic_msi_irq_info_debug_release(struct inode *inode, struct file *file)
{
	return single_release(inode, file);
}

static const struct file_operations msi_irq_info_ops = {
	.owner   = THIS_MODULE,
	.open    = mic_msi_irq_info_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = mic_msi_irq_info_debug_release
};

/**
 * mic_create_debug_dir - Initialize MIC debugfs entries.
 */
void mic_create_debug_dir(struct mic_device *mdev)
{
	if (!mic_dbg)
		return;

	mdev->dbg_dir = debugfs_create_dir(dev_name(mdev->sdev), mic_dbg);
	if (!mdev->dbg_dir)
		return;

	debugfs_create_file("log_buf", 0444, mdev->dbg_dir,
		mdev, &log_buf_ops);

	debugfs_create_file("smpt", 0444, mdev->dbg_dir,
		mdev, &smpt_file_ops);

	debugfs_create_file("soft_reset", 0444, mdev->dbg_dir,
		mdev, &soft_reset_ops);

	debugfs_create_file("post_code", 0444, mdev->dbg_dir,
		mdev, &post_code_ops);

	debugfs_create_file("dp", 0444, mdev->dbg_dir,
		mdev, &dp_ops);

	debugfs_create_file("msi_irq_info", 0444, mdev->dbg_dir,
		mdev, &msi_irq_info_ops);
}

/**
 * mic_delete_debug_dir - Uninitialize MIC debugfs entries.
 */
void mic_delete_debug_dir(struct mic_device *mdev)
{
	if (!mdev->dbg_dir)
		return;

	debugfs_remove_recursive(mdev->dbg_dir);
}

/**
 * mic_init_debugfs - Initialize global debugfs entry.
 */
void __init mic_init_debugfs(void)
{
	mic_dbg = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!mic_dbg)
		pr_err("can't create debugfs dir\n");
}

/**
 * mic_exit_debugfs - Uninitialize global debugfs entry
 */
void mic_exit_debugfs(void)
{
	debugfs_remove(mic_dbg);
}
