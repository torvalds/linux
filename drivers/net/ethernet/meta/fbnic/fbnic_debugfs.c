// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/debugfs.h>
#include <linux/pci.h>
#include <linux/rtnetlink.h>
#include <linux/seq_file.h>

#include "fbnic.h"

static struct dentry *fbnic_dbg_root;

static void fbnic_dbg_desc_break(struct seq_file *s, int i)
{
	while (i--)
		seq_putc(s, '-');

	seq_putc(s, '\n');
}

static int fbnic_dbg_mac_addr_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;
	char hdr[80];
	int i;

	/* Generate Header */
	snprintf(hdr, sizeof(hdr), "%3s %s %-17s %s\n",
		 "Idx", "S", "TCAM Bitmap", "Addr/Mask");
	seq_puts(s, hdr);
	fbnic_dbg_desc_break(s, strnlen(hdr, sizeof(hdr)));

	for (i = 0; i < FBNIC_RPC_TCAM_MACDA_NUM_ENTRIES; i++) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[i];

		seq_printf(s, "%02d  %d %64pb %pm\n",
			   i, mac_addr->state, mac_addr->act_tcam,
			   mac_addr->value.addr8);
		seq_printf(s, "                        %pm\n",
			   mac_addr->mask.addr8);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_mac_addr);

static int fbnic_dbg_pcie_stats_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;

	rtnl_lock();
	fbnic_get_hw_stats(fbd);

	seq_printf(s, "ob_rd_tlp: %llu\n", fbd->hw_stats.pcie.ob_rd_tlp.value);
	seq_printf(s, "ob_rd_dword: %llu\n",
		   fbd->hw_stats.pcie.ob_rd_dword.value);
	seq_printf(s, "ob_wr_tlp: %llu\n", fbd->hw_stats.pcie.ob_wr_tlp.value);
	seq_printf(s, "ob_wr_dword: %llu\n",
		   fbd->hw_stats.pcie.ob_wr_dword.value);
	seq_printf(s, "ob_cpl_tlp: %llu\n",
		   fbd->hw_stats.pcie.ob_cpl_tlp.value);
	seq_printf(s, "ob_cpl_dword: %llu\n",
		   fbd->hw_stats.pcie.ob_cpl_dword.value);
	seq_printf(s, "ob_rd_no_tag: %llu\n",
		   fbd->hw_stats.pcie.ob_rd_no_tag.value);
	seq_printf(s, "ob_rd_no_cpl_cred: %llu\n",
		   fbd->hw_stats.pcie.ob_rd_no_cpl_cred.value);
	seq_printf(s, "ob_rd_no_np_cred: %llu\n",
		   fbd->hw_stats.pcie.ob_rd_no_np_cred.value);
	rtnl_unlock();

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_pcie_stats);

void fbnic_dbg_fbd_init(struct fbnic_dev *fbd)
{
	struct pci_dev *pdev = to_pci_dev(fbd->dev);
	const char *name = pci_name(pdev);

	fbd->dbg_fbd = debugfs_create_dir(name, fbnic_dbg_root);
	debugfs_create_file("pcie_stats", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_pcie_stats_fops);
	debugfs_create_file("mac_addr", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_mac_addr_fops);
}

void fbnic_dbg_fbd_exit(struct fbnic_dev *fbd)
{
	debugfs_remove_recursive(fbd->dbg_fbd);
	fbd->dbg_fbd = NULL;
}

void fbnic_dbg_init(void)
{
	fbnic_dbg_root = debugfs_create_dir(fbnic_driver_name, NULL);
}

void fbnic_dbg_exit(void)
{
	debugfs_remove_recursive(fbnic_dbg_root);
	fbnic_dbg_root = NULL;
}
