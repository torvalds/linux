// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * Intel MIC Host driver.
 */
#include <linux/debugfs.h>
#include <linux/pci.h>
#include <linux/seq_file.h>

#include <linux/mic_common.h>
#include "../common/mic_dev.h"
#include "mic_device.h"
#include "mic_smpt.h"

/* Debugfs parent dir */
static struct dentry *mic_dbg;

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

DEFINE_SHOW_ATTRIBUTE(mic_smpt);

static int mic_post_code_show(struct seq_file *s, void *pos)
{
	struct mic_device *mdev = s->private;
	u32 reg = mdev->ops->get_postcode(mdev);

	seq_printf(s, "%c%c", reg & 0xff, (reg >> 8) & 0xff);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mic_post_code);

static int mic_msi_irq_info_show(struct seq_file *s, void *pos)
{
	struct mic_device *mdev  = s->private;
	int reg;
	int i, j;
	u16 entry;
	u16 vector;
	struct pci_dev *pdev = mdev->pdev;

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
					   (mdev->irq_info.mic_msi_map[i] &
					   BIT(j)) ? 1 : 0);
			seq_puts(s, "\n\n");
		}
	} else {
		seq_puts(s, "MSI/MSIx interrupts not enabled\n");
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mic_msi_irq_info);

/**
 * mic_create_debug_dir - Initialize MIC debugfs entries.
 */
void mic_create_debug_dir(struct mic_device *mdev)
{
	char name[16];

	if (!mic_dbg)
		return;

	scnprintf(name, sizeof(name), "mic%d", mdev->id);
	mdev->dbg_dir = debugfs_create_dir(name, mic_dbg);
	if (!mdev->dbg_dir)
		return;

	debugfs_create_file("smpt", 0444, mdev->dbg_dir, mdev,
			    &mic_smpt_fops);

	debugfs_create_file("post_code", 0444, mdev->dbg_dir, mdev,
			    &mic_post_code_fops);

	debugfs_create_file("msi_irq_info", 0444, mdev->dbg_dir, mdev,
			    &mic_msi_irq_info_fops);
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
