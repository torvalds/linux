// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include <linux/pci.h>

#include "core.h"

static struct dentry *pdsc_dir;

void pdsc_debugfs_create(void)
{
	pdsc_dir = debugfs_create_dir(PDS_CORE_DRV_NAME, NULL);
}

void pdsc_debugfs_destroy(void)
{
	debugfs_remove_recursive(pdsc_dir);
}

void pdsc_debugfs_add_dev(struct pdsc *pdsc)
{
	pdsc->dentry = debugfs_create_dir(pci_name(pdsc->pdev), pdsc_dir);

	debugfs_create_ulong("state", 0400, pdsc->dentry, &pdsc->state);
}

void pdsc_debugfs_del_dev(struct pdsc *pdsc)
{
	debugfs_remove_recursive(pdsc->dentry);
	pdsc->dentry = NULL;
}

static int identity_show(struct seq_file *seq, void *v)
{
	struct pdsc *pdsc = seq->private;
	struct pds_core_dev_identity *ident;
	int vt;

	ident = &pdsc->dev_ident;

	seq_printf(seq, "fw_heartbeat:     0x%x\n",
		   ioread32(&pdsc->info_regs->fw_heartbeat));

	seq_printf(seq, "nlifs:            %d\n",
		   le32_to_cpu(ident->nlifs));
	seq_printf(seq, "nintrs:           %d\n",
		   le32_to_cpu(ident->nintrs));
	seq_printf(seq, "ndbpgs_per_lif:   %d\n",
		   le32_to_cpu(ident->ndbpgs_per_lif));
	seq_printf(seq, "intr_coal_mult:   %d\n",
		   le32_to_cpu(ident->intr_coal_mult));
	seq_printf(seq, "intr_coal_div:    %d\n",
		   le32_to_cpu(ident->intr_coal_div));

	seq_puts(seq, "vif_types:        ");
	for (vt = 0; vt < PDS_DEV_TYPE_MAX; vt++)
		seq_printf(seq, "%d ",
			   le16_to_cpu(pdsc->dev_ident.vif_types[vt]));
	seq_puts(seq, "\n");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(identity);

void pdsc_debugfs_add_ident(struct pdsc *pdsc)
{
	debugfs_create_file("identity", 0400, pdsc->dentry,
			    pdsc, &identity_fops);
}
