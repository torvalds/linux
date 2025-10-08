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

static int fbnic_dbg_tce_tcam_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;
	int i, tcam_idx = 0;
	char hdr[80];

	/* Generate Header */
	snprintf(hdr, sizeof(hdr), "%3s %s %-17s %s\n",
		 "Idx", "S", "TCAM Bitmap", "Addr/Mask");
	seq_puts(s, hdr);
	fbnic_dbg_desc_break(s, strnlen(hdr, sizeof(hdr)));

	for (i = 0; i < ARRAY_SIZE(fbd->mac_addr); i++) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[i];

		/* Verify BMC bit is set */
		if (!test_bit(FBNIC_MAC_ADDR_T_BMC, mac_addr->act_tcam))
			continue;

		if (tcam_idx == FBNIC_TCE_TCAM_NUM_ENTRIES)
			break;

		seq_printf(s, "%02d  %d %64pb %pm\n",
			   tcam_idx, mac_addr->state, mac_addr->act_tcam,
			   mac_addr->value.addr8);
		seq_printf(s, "                        %pm\n",
			   mac_addr->mask.addr8);
		tcam_idx++;
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_tce_tcam);

static int fbnic_dbg_act_tcam_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;
	char hdr[80];
	int i;

	/* Generate Header */
	snprintf(hdr, sizeof(hdr), "%3s %s %-55s %-4s %s\n",
		 "Idx", "S", "Value/Mask", "RSS", "Dest");
	seq_puts(s, hdr);
	fbnic_dbg_desc_break(s, strnlen(hdr, sizeof(hdr)));

	for (i = 0; i < FBNIC_RPC_TCAM_ACT_NUM_ENTRIES; i++) {
		struct fbnic_act_tcam *act_tcam = &fbd->act_tcam[i];

		seq_printf(s, "%02d  %d %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x  %04x %08x\n",
			   i, act_tcam->state,
			   act_tcam->value.tcam[10], act_tcam->value.tcam[9],
			   act_tcam->value.tcam[8], act_tcam->value.tcam[7],
			   act_tcam->value.tcam[6], act_tcam->value.tcam[5],
			   act_tcam->value.tcam[4], act_tcam->value.tcam[3],
			   act_tcam->value.tcam[2], act_tcam->value.tcam[1],
			   act_tcam->value.tcam[0], act_tcam->rss_en_mask,
			   act_tcam->dest);
		seq_printf(s, "      %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x\n",
			   act_tcam->mask.tcam[10], act_tcam->mask.tcam[9],
			   act_tcam->mask.tcam[8], act_tcam->mask.tcam[7],
			   act_tcam->mask.tcam[6], act_tcam->mask.tcam[5],
			   act_tcam->mask.tcam[4], act_tcam->mask.tcam[3],
			   act_tcam->mask.tcam[2], act_tcam->mask.tcam[1],
			   act_tcam->mask.tcam[0]);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_act_tcam);

static int fbnic_dbg_ip_addr_show(struct seq_file *s,
				  struct fbnic_ip_addr *ip_addr)
{
	char hdr[80];
	int i;

	/* Generate Header */
	snprintf(hdr, sizeof(hdr), "%3s %s %-17s %s %s\n",
		 "Idx", "S", "TCAM Bitmap", "V", "Addr/Mask");
	seq_puts(s, hdr);
	fbnic_dbg_desc_break(s, strnlen(hdr, sizeof(hdr)));

	for (i = 0; i < FBNIC_RPC_TCAM_IP_ADDR_NUM_ENTRIES; i++, ip_addr++) {
		seq_printf(s, "%02d  %d %64pb %d %pi6\n",
			   i, ip_addr->state, ip_addr->act_tcam,
			   ip_addr->version, &ip_addr->value);
		seq_printf(s, "                          %pi6\n",
			   &ip_addr->mask);
	}

	return 0;
}

static int fbnic_dbg_ip_src_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;

	return fbnic_dbg_ip_addr_show(s, fbd->ip_src);
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_ip_src);

static int fbnic_dbg_ip_dst_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;

	return fbnic_dbg_ip_addr_show(s, fbd->ip_dst);
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_ip_dst);

static int fbnic_dbg_ipo_src_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;

	return fbnic_dbg_ip_addr_show(s, fbd->ipo_src);
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_ipo_src);

static int fbnic_dbg_ipo_dst_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;

	return fbnic_dbg_ip_addr_show(s, fbd->ipo_dst);
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_ipo_dst);

static int fbnic_dbg_fw_log_show(struct seq_file *s, void *v)
{
	struct fbnic_dev *fbd = s->private;
	struct fbnic_fw_log_entry *entry;
	unsigned long flags;

	if (!fbnic_fw_log_ready(fbd))
		return -ENXIO;

	spin_lock_irqsave(&fbd->fw_log.lock, flags);

	list_for_each_entry_reverse(entry, &fbd->fw_log.entries, list) {
		seq_printf(s, FBNIC_FW_LOG_FMT, entry->index,
			   (entry->timestamp / (MSEC_PER_SEC * 60 * 60 * 24)),
			   (entry->timestamp / (MSEC_PER_SEC * 60 * 60)) % 24,
			   ((entry->timestamp / (MSEC_PER_SEC * 60) % 60)),
			   ((entry->timestamp / MSEC_PER_SEC) % 60),
			   (entry->timestamp % MSEC_PER_SEC),
			   entry->msg);
	}

	spin_unlock_irqrestore(&fbd->fw_log.lock, flags);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fbnic_dbg_fw_log);

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
	debugfs_create_file("tce_tcam", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_tce_tcam_fops);
	debugfs_create_file("act_tcam", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_act_tcam_fops);
	debugfs_create_file("ip_src", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_ip_src_fops);
	debugfs_create_file("ip_dst", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_ip_dst_fops);
	debugfs_create_file("ipo_src", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_ipo_src_fops);
	debugfs_create_file("ipo_dst", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_ipo_dst_fops);
	debugfs_create_file("fw_log", 0400, fbd->dbg_fbd, fbd,
			    &fbnic_dbg_fw_log_fops);
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
