// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Passthrough DMA device driver
 * -- Based on the CCP driver
 *
 * Copyright (C) 2016,2021 Advanced Micro Devices, Inc.
 *
 * Author: Sanjay R Mehta <sanju.mehta@amd.com>
 * Author: Gary R Hook <gary.hook@amd.com>
 */

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "ptdma.h"

/* DebugFS helpers */
#define	RI_VERSION_NUM	0x0000003F

#define	RI_NUM_VQM	0x00078000
#define	RI_NVQM_SHIFT	15

static int pt_debugfs_info_show(struct seq_file *s, void *p)
{
	struct pt_device *pt = s->private;
	unsigned int regval;

	seq_printf(s, "Device name: %s\n", dev_name(pt->dev));
	seq_printf(s, "   # Queues: %d\n", 1);
	seq_printf(s, "     # Cmds: %d\n", pt->cmd_count);

	regval = ioread32(pt->io_regs + CMD_PT_VERSION);

	seq_printf(s, "    Version: %d\n", regval & RI_VERSION_NUM);
	seq_puts(s, "    Engines:");
	seq_puts(s, "\n");
	seq_printf(s, "     Queues: %d\n", (regval & RI_NUM_VQM) >> RI_NVQM_SHIFT);

	return 0;
}

/*
 * Return a formatted buffer containing the current
 * statistics of queue for PTDMA
 */
static int pt_debugfs_stats_show(struct seq_file *s, void *p)
{
	struct pt_device *pt = s->private;

	seq_printf(s, "Total Interrupts Handled: %ld\n", pt->total_interrupts);

	return 0;
}

static int pt_debugfs_queue_show(struct seq_file *s, void *p)
{
	struct pt_cmd_queue *cmd_q = s->private;
	unsigned int regval;

	if (!cmd_q)
		return 0;

	seq_printf(s, "               Pass-Thru: %ld\n", cmd_q->total_pt_ops);

	regval = ioread32(cmd_q->reg_control + 0x000C);

	seq_puts(s, "      Enabled Interrupts:");
	if (regval & INT_EMPTY_QUEUE)
		seq_puts(s, " EMPTY");
	if (regval & INT_QUEUE_STOPPED)
		seq_puts(s, " STOPPED");
	if (regval & INT_ERROR)
		seq_puts(s, " ERROR");
	if (regval & INT_COMPLETION)
		seq_puts(s, " COMPLETION");
	seq_puts(s, "\n");

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(pt_debugfs_info);
DEFINE_SHOW_ATTRIBUTE(pt_debugfs_queue);
DEFINE_SHOW_ATTRIBUTE(pt_debugfs_stats);

void ptdma_debugfs_setup(struct pt_device *pt)
{
	struct pt_cmd_queue *cmd_q;
	struct dentry *debugfs_q_instance;

	if (!debugfs_initialized())
		return;

	debugfs_create_file("info", 0400, pt->dma_dev.dbg_dev_root, pt,
			    &pt_debugfs_info_fops);

	debugfs_create_file("stats", 0400, pt->dma_dev.dbg_dev_root, pt,
			    &pt_debugfs_stats_fops);

	cmd_q = &pt->cmd_q;

	debugfs_q_instance =
		debugfs_create_dir("q", pt->dma_dev.dbg_dev_root);

	debugfs_create_file("stats", 0400, debugfs_q_instance, cmd_q,
			    &pt_debugfs_queue_fops);
}
