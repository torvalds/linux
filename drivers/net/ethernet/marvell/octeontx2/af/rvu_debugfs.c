// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2019 Marvell.
 *
 */

#ifdef CONFIG_DEBUG_FS

#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "rvu_struct.h"
#include "rvu_reg.h"
#include "rvu.h"
#include "cgx.h"
#include "lmac_common.h"
#include "npc.h"
#include "rvu_npc_hash.h"
#include "mcs.h"

#define DEBUGFS_DIR_NAME "octeontx2"

enum {
	CGX_STAT0,
	CGX_STAT1,
	CGX_STAT2,
	CGX_STAT3,
	CGX_STAT4,
	CGX_STAT5,
	CGX_STAT6,
	CGX_STAT7,
	CGX_STAT8,
	CGX_STAT9,
	CGX_STAT10,
	CGX_STAT11,
	CGX_STAT12,
	CGX_STAT13,
	CGX_STAT14,
	CGX_STAT15,
	CGX_STAT16,
	CGX_STAT17,
	CGX_STAT18,
};

/* NIX TX stats */
enum nix_stat_lf_tx {
	TX_UCAST	= 0x0,
	TX_BCAST	= 0x1,
	TX_MCAST	= 0x2,
	TX_DROP		= 0x3,
	TX_OCTS		= 0x4,
	TX_STATS_ENUM_LAST,
};

/* NIX RX stats */
enum nix_stat_lf_rx {
	RX_OCTS		= 0x0,
	RX_UCAST	= 0x1,
	RX_BCAST	= 0x2,
	RX_MCAST	= 0x3,
	RX_DROP		= 0x4,
	RX_DROP_OCTS	= 0x5,
	RX_FCS		= 0x6,
	RX_ERR		= 0x7,
	RX_DRP_BCAST	= 0x8,
	RX_DRP_MCAST	= 0x9,
	RX_DRP_L3BCAST	= 0xa,
	RX_DRP_L3MCAST	= 0xb,
	RX_STATS_ENUM_LAST,
};

static char *cgx_rx_stats_fields[] = {
	[CGX_STAT0]	= "Received packets",
	[CGX_STAT1]	= "Octets of received packets",
	[CGX_STAT2]	= "Received PAUSE packets",
	[CGX_STAT3]	= "Received PAUSE and control packets",
	[CGX_STAT4]	= "Filtered DMAC0 (NIX-bound) packets",
	[CGX_STAT5]	= "Filtered DMAC0 (NIX-bound) octets",
	[CGX_STAT6]	= "Packets dropped due to RX FIFO full",
	[CGX_STAT7]	= "Octets dropped due to RX FIFO full",
	[CGX_STAT8]	= "Error packets",
	[CGX_STAT9]	= "Filtered DMAC1 (NCSI-bound) packets",
	[CGX_STAT10]	= "Filtered DMAC1 (NCSI-bound) octets",
	[CGX_STAT11]	= "NCSI-bound packets dropped",
	[CGX_STAT12]	= "NCSI-bound octets dropped",
};

static char *cgx_tx_stats_fields[] = {
	[CGX_STAT0]	= "Packets dropped due to excessive collisions",
	[CGX_STAT1]	= "Packets dropped due to excessive deferral",
	[CGX_STAT2]	= "Multiple collisions before successful transmission",
	[CGX_STAT3]	= "Single collisions before successful transmission",
	[CGX_STAT4]	= "Total octets sent on the interface",
	[CGX_STAT5]	= "Total frames sent on the interface",
	[CGX_STAT6]	= "Packets sent with an octet count < 64",
	[CGX_STAT7]	= "Packets sent with an octet count == 64",
	[CGX_STAT8]	= "Packets sent with an octet count of 65-127",
	[CGX_STAT9]	= "Packets sent with an octet count of 128-255",
	[CGX_STAT10]	= "Packets sent with an octet count of 256-511",
	[CGX_STAT11]	= "Packets sent with an octet count of 512-1023",
	[CGX_STAT12]	= "Packets sent with an octet count of 1024-1518",
	[CGX_STAT13]	= "Packets sent with an octet count of > 1518",
	[CGX_STAT14]	= "Packets sent to a broadcast DMAC",
	[CGX_STAT15]	= "Packets sent to the multicast DMAC",
	[CGX_STAT16]	= "Transmit underflow and were truncated",
	[CGX_STAT17]	= "Control/PAUSE packets sent",
};

static char *rpm_rx_stats_fields[] = {
	"Octets of received packets",
	"Octets of received packets with out error",
	"Received packets with alignment errors",
	"Control/PAUSE packets received",
	"Packets received with Frame too long Errors",
	"Packets received with a1nrange length Errors",
	"Received packets",
	"Packets received with FrameCheckSequenceErrors",
	"Packets received with VLAN header",
	"Error packets",
	"Packets received with unicast DMAC",
	"Packets received with multicast DMAC",
	"Packets received with broadcast DMAC",
	"Dropped packets",
	"Total frames received on interface",
	"Packets received with an octet count < 64",
	"Packets received with an octet count == 64",
	"Packets received with an octet count of 65-127",
	"Packets received with an octet count of 128-255",
	"Packets received with an octet count of 256-511",
	"Packets received with an octet count of 512-1023",
	"Packets received with an octet count of 1024-1518",
	"Packets received with an octet count of > 1518",
	"Oversized Packets",
	"Jabber Packets",
	"Fragmented Packets",
	"CBFC(class based flow control) pause frames received for class 0",
	"CBFC pause frames received for class 1",
	"CBFC pause frames received for class 2",
	"CBFC pause frames received for class 3",
	"CBFC pause frames received for class 4",
	"CBFC pause frames received for class 5",
	"CBFC pause frames received for class 6",
	"CBFC pause frames received for class 7",
	"CBFC pause frames received for class 8",
	"CBFC pause frames received for class 9",
	"CBFC pause frames received for class 10",
	"CBFC pause frames received for class 11",
	"CBFC pause frames received for class 12",
	"CBFC pause frames received for class 13",
	"CBFC pause frames received for class 14",
	"CBFC pause frames received for class 15",
	"MAC control packets received",
};

static char *rpm_tx_stats_fields[] = {
	"Total octets sent on the interface",
	"Total octets transmitted OK",
	"Control/Pause frames sent",
	"Total frames transmitted OK",
	"Total frames sent with VLAN header",
	"Error Packets",
	"Packets sent to unicast DMAC",
	"Packets sent to the multicast DMAC",
	"Packets sent to a broadcast DMAC",
	"Packets sent with an octet count == 64",
	"Packets sent with an octet count of 65-127",
	"Packets sent with an octet count of 128-255",
	"Packets sent with an octet count of 256-511",
	"Packets sent with an octet count of 512-1023",
	"Packets sent with an octet count of 1024-1518",
	"Packets sent with an octet count of > 1518",
	"CBFC(class based flow control) pause frames transmitted for class 0",
	"CBFC pause frames transmitted for class 1",
	"CBFC pause frames transmitted for class 2",
	"CBFC pause frames transmitted for class 3",
	"CBFC pause frames transmitted for class 4",
	"CBFC pause frames transmitted for class 5",
	"CBFC pause frames transmitted for class 6",
	"CBFC pause frames transmitted for class 7",
	"CBFC pause frames transmitted for class 8",
	"CBFC pause frames transmitted for class 9",
	"CBFC pause frames transmitted for class 10",
	"CBFC pause frames transmitted for class 11",
	"CBFC pause frames transmitted for class 12",
	"CBFC pause frames transmitted for class 13",
	"CBFC pause frames transmitted for class 14",
	"CBFC pause frames transmitted for class 15",
	"MAC control packets sent",
	"Total frames sent on the interface"
};

enum cpt_eng_type {
	CPT_AE_TYPE = 1,
	CPT_SE_TYPE = 2,
	CPT_IE_TYPE = 3,
};

#define rvu_dbg_NULL NULL
#define rvu_dbg_open_NULL NULL

#define RVU_DEBUG_SEQ_FOPS(name, read_op, write_op)	\
static int rvu_dbg_open_##name(struct inode *inode, struct file *file) \
{ \
	return single_open(file, rvu_dbg_##read_op, inode->i_private); \
} \
static const struct file_operations rvu_dbg_##name##_fops = { \
	.owner		= THIS_MODULE, \
	.open		= rvu_dbg_open_##name, \
	.read		= seq_read, \
	.write		= rvu_dbg_##write_op, \
	.llseek		= seq_lseek, \
	.release	= single_release, \
}

#define RVU_DEBUG_FOPS(name, read_op, write_op) \
static const struct file_operations rvu_dbg_##name##_fops = { \
	.owner = THIS_MODULE, \
	.open = simple_open, \
	.read = rvu_dbg_##read_op, \
	.write = rvu_dbg_##write_op \
}

static void print_nix_qsize(struct seq_file *filp, struct rvu_pfvf *pfvf);

static int rvu_dbg_mcs_port_stats_display(struct seq_file *filp, void *unused, int dir)
{
	struct mcs *mcs = filp->private;
	struct mcs_port_stats stats;
	int lmac;

	seq_puts(filp, "\n port stats\n");
	mutex_lock(&mcs->stats_lock);
	for_each_set_bit(lmac, &mcs->hw->lmac_bmap, mcs->hw->lmac_cnt) {
		mcs_get_port_stats(mcs, &stats, lmac, dir);
		seq_printf(filp, "port%d: Tcam Miss: %lld\n", lmac, stats.tcam_miss_cnt);
		seq_printf(filp, "port%d: Parser errors: %lld\n", lmac, stats.parser_err_cnt);

		if (dir == MCS_RX && mcs->hw->mcs_blks > 1)
			seq_printf(filp, "port%d: Preempt error: %lld\n", lmac,
				   stats.preempt_err_cnt);
		if (dir == MCS_TX)
			seq_printf(filp, "port%d: Sectag insert error: %lld\n", lmac,
				   stats.sectag_insert_err_cnt);
	}
	mutex_unlock(&mcs->stats_lock);
	return 0;
}

static int rvu_dbg_mcs_rx_port_stats_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_mcs_port_stats_display(filp, unused, MCS_RX);
}

RVU_DEBUG_SEQ_FOPS(mcs_rx_port_stats, mcs_rx_port_stats_display, NULL);

static int rvu_dbg_mcs_tx_port_stats_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_mcs_port_stats_display(filp, unused, MCS_TX);
}

RVU_DEBUG_SEQ_FOPS(mcs_tx_port_stats, mcs_tx_port_stats_display, NULL);

static int rvu_dbg_mcs_sa_stats_display(struct seq_file *filp, void *unused, int dir)
{
	struct mcs *mcs = filp->private;
	struct mcs_sa_stats stats;
	struct rsrc_bmap *map;
	int sa_id;

	if (dir == MCS_TX) {
		map = &mcs->tx.sa;
		mutex_lock(&mcs->stats_lock);
		for_each_set_bit(sa_id, map->bmap, mcs->hw->sa_entries) {
			seq_puts(filp, "\n TX SA stats\n");
			mcs_get_sa_stats(mcs, &stats, sa_id, MCS_TX);
			seq_printf(filp, "sa%d: Pkts encrypted: %lld\n", sa_id,
				   stats.pkt_encrypt_cnt);

			seq_printf(filp, "sa%d: Pkts protected: %lld\n", sa_id,
				   stats.pkt_protected_cnt);
		}
		mutex_unlock(&mcs->stats_lock);
		return 0;
	}

	/* RX stats */
	map = &mcs->rx.sa;
	mutex_lock(&mcs->stats_lock);
	for_each_set_bit(sa_id, map->bmap, mcs->hw->sa_entries) {
		seq_puts(filp, "\n RX SA stats\n");
		mcs_get_sa_stats(mcs, &stats, sa_id, MCS_RX);
		seq_printf(filp, "sa%d: Invalid pkts: %lld\n", sa_id, stats.pkt_invalid_cnt);
		seq_printf(filp, "sa%d: Pkts no sa error: %lld\n", sa_id, stats.pkt_nosaerror_cnt);
		seq_printf(filp, "sa%d: Pkts not valid: %lld\n", sa_id, stats.pkt_notvalid_cnt);
		seq_printf(filp, "sa%d: Pkts ok: %lld\n", sa_id, stats.pkt_ok_cnt);
		seq_printf(filp, "sa%d: Pkts no sa: %lld\n", sa_id, stats.pkt_nosa_cnt);
	}
	mutex_unlock(&mcs->stats_lock);
	return 0;
}

static int rvu_dbg_mcs_rx_sa_stats_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_mcs_sa_stats_display(filp, unused, MCS_RX);
}

RVU_DEBUG_SEQ_FOPS(mcs_rx_sa_stats, mcs_rx_sa_stats_display, NULL);

static int rvu_dbg_mcs_tx_sa_stats_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_mcs_sa_stats_display(filp, unused, MCS_TX);
}

RVU_DEBUG_SEQ_FOPS(mcs_tx_sa_stats, mcs_tx_sa_stats_display, NULL);

static int rvu_dbg_mcs_tx_sc_stats_display(struct seq_file *filp, void *unused)
{
	struct mcs *mcs = filp->private;
	struct mcs_sc_stats stats;
	struct rsrc_bmap *map;
	int sc_id;

	map = &mcs->tx.sc;
	seq_puts(filp, "\n SC stats\n");

	mutex_lock(&mcs->stats_lock);
	for_each_set_bit(sc_id, map->bmap, mcs->hw->sc_entries) {
		mcs_get_sc_stats(mcs, &stats, sc_id, MCS_TX);
		seq_printf(filp, "\n=======sc%d======\n\n", sc_id);
		seq_printf(filp, "sc%d: Pkts encrypted: %lld\n", sc_id, stats.pkt_encrypt_cnt);
		seq_printf(filp, "sc%d: Pkts protected: %lld\n", sc_id, stats.pkt_protected_cnt);

		if (mcs->hw->mcs_blks == 1) {
			seq_printf(filp, "sc%d: Octets encrypted: %lld\n", sc_id,
				   stats.octet_encrypt_cnt);
			seq_printf(filp, "sc%d: Octets protected: %lld\n", sc_id,
				   stats.octet_protected_cnt);
		}
	}
	mutex_unlock(&mcs->stats_lock);
	return 0;
}

RVU_DEBUG_SEQ_FOPS(mcs_tx_sc_stats, mcs_tx_sc_stats_display, NULL);

static int rvu_dbg_mcs_rx_sc_stats_display(struct seq_file *filp, void *unused)
{
	struct mcs *mcs = filp->private;
	struct mcs_sc_stats stats;
	struct rsrc_bmap *map;
	int sc_id;

	map = &mcs->rx.sc;
	seq_puts(filp, "\n SC stats\n");

	mutex_lock(&mcs->stats_lock);
	for_each_set_bit(sc_id, map->bmap, mcs->hw->sc_entries) {
		mcs_get_sc_stats(mcs, &stats, sc_id, MCS_RX);
		seq_printf(filp, "\n=======sc%d======\n\n", sc_id);
		seq_printf(filp, "sc%d: Cam hits: %lld\n", sc_id, stats.hit_cnt);
		seq_printf(filp, "sc%d: Invalid pkts: %lld\n", sc_id, stats.pkt_invalid_cnt);
		seq_printf(filp, "sc%d: Late pkts: %lld\n", sc_id, stats.pkt_late_cnt);
		seq_printf(filp, "sc%d: Notvalid pkts: %lld\n", sc_id, stats.pkt_notvalid_cnt);
		seq_printf(filp, "sc%d: Unchecked pkts: %lld\n", sc_id, stats.pkt_unchecked_cnt);

		if (mcs->hw->mcs_blks > 1) {
			seq_printf(filp, "sc%d: Delay pkts: %lld\n", sc_id, stats.pkt_delay_cnt);
			seq_printf(filp, "sc%d: Pkts ok: %lld\n", sc_id, stats.pkt_ok_cnt);
		}
		if (mcs->hw->mcs_blks == 1) {
			seq_printf(filp, "sc%d: Octets decrypted: %lld\n", sc_id,
				   stats.octet_decrypt_cnt);
			seq_printf(filp, "sc%d: Octets validated: %lld\n", sc_id,
				   stats.octet_validate_cnt);
		}
	}
	mutex_unlock(&mcs->stats_lock);
	return 0;
}

RVU_DEBUG_SEQ_FOPS(mcs_rx_sc_stats, mcs_rx_sc_stats_display, NULL);

static int rvu_dbg_mcs_flowid_stats_display(struct seq_file *filp, void *unused, int dir)
{
	struct mcs *mcs = filp->private;
	struct mcs_flowid_stats stats;
	struct rsrc_bmap *map;
	int flow_id;

	seq_puts(filp, "\n Flowid stats\n");

	if (dir == MCS_RX)
		map = &mcs->rx.flow_ids;
	else
		map = &mcs->tx.flow_ids;

	mutex_lock(&mcs->stats_lock);
	for_each_set_bit(flow_id, map->bmap, mcs->hw->tcam_entries) {
		mcs_get_flowid_stats(mcs, &stats, flow_id, dir);
		seq_printf(filp, "Flowid%d: Hit:%lld\n", flow_id, stats.tcam_hit_cnt);
	}
	mutex_unlock(&mcs->stats_lock);
	return 0;
}

static int rvu_dbg_mcs_tx_flowid_stats_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_mcs_flowid_stats_display(filp, unused, MCS_TX);
}

RVU_DEBUG_SEQ_FOPS(mcs_tx_flowid_stats, mcs_tx_flowid_stats_display, NULL);

static int rvu_dbg_mcs_rx_flowid_stats_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_mcs_flowid_stats_display(filp, unused, MCS_RX);
}

RVU_DEBUG_SEQ_FOPS(mcs_rx_flowid_stats, mcs_rx_flowid_stats_display, NULL);

static int rvu_dbg_mcs_tx_secy_stats_display(struct seq_file *filp, void *unused)
{
	struct mcs *mcs = filp->private;
	struct mcs_secy_stats stats;
	struct rsrc_bmap *map;
	int secy_id;

	map = &mcs->tx.secy;
	seq_puts(filp, "\n MCS TX secy stats\n");

	mutex_lock(&mcs->stats_lock);
	for_each_set_bit(secy_id, map->bmap, mcs->hw->secy_entries) {
		mcs_get_tx_secy_stats(mcs, &stats, secy_id);
		seq_printf(filp, "\n=======Secy%d======\n\n", secy_id);
		seq_printf(filp, "secy%d: Ctrl bcast pkts: %lld\n", secy_id,
			   stats.ctl_pkt_bcast_cnt);
		seq_printf(filp, "secy%d: Ctrl Mcast pkts: %lld\n", secy_id,
			   stats.ctl_pkt_mcast_cnt);
		seq_printf(filp, "secy%d: Ctrl ucast pkts: %lld\n", secy_id,
			   stats.ctl_pkt_ucast_cnt);
		seq_printf(filp, "secy%d: Ctrl octets: %lld\n", secy_id, stats.ctl_octet_cnt);
		seq_printf(filp, "secy%d: Unctrl bcast cnt: %lld\n", secy_id,
			   stats.unctl_pkt_bcast_cnt);
		seq_printf(filp, "secy%d: Unctrl mcast pkts: %lld\n", secy_id,
			   stats.unctl_pkt_mcast_cnt);
		seq_printf(filp, "secy%d: Unctrl ucast pkts: %lld\n", secy_id,
			   stats.unctl_pkt_ucast_cnt);
		seq_printf(filp, "secy%d: Unctrl octets: %lld\n", secy_id, stats.unctl_octet_cnt);
		seq_printf(filp, "secy%d: Octet encrypted: %lld\n", secy_id,
			   stats.octet_encrypted_cnt);
		seq_printf(filp, "secy%d: octet protected: %lld\n", secy_id,
			   stats.octet_protected_cnt);
		seq_printf(filp, "secy%d: Pkts on active sa: %lld\n", secy_id,
			   stats.pkt_noactivesa_cnt);
		seq_printf(filp, "secy%d: Pkts too long: %lld\n", secy_id, stats.pkt_toolong_cnt);
		seq_printf(filp, "secy%d: Pkts untagged: %lld\n", secy_id, stats.pkt_untagged_cnt);
	}
	mutex_unlock(&mcs->stats_lock);
	return 0;
}

RVU_DEBUG_SEQ_FOPS(mcs_tx_secy_stats, mcs_tx_secy_stats_display, NULL);

static int rvu_dbg_mcs_rx_secy_stats_display(struct seq_file *filp, void *unused)
{
	struct mcs *mcs = filp->private;
	struct mcs_secy_stats stats;
	struct rsrc_bmap *map;
	int secy_id;

	map = &mcs->rx.secy;
	seq_puts(filp, "\n MCS secy stats\n");

	mutex_lock(&mcs->stats_lock);
	for_each_set_bit(secy_id, map->bmap, mcs->hw->secy_entries) {
		mcs_get_rx_secy_stats(mcs, &stats, secy_id);
		seq_printf(filp, "\n=======Secy%d======\n\n", secy_id);
		seq_printf(filp, "secy%d: Ctrl bcast pkts: %lld\n", secy_id,
			   stats.ctl_pkt_bcast_cnt);
		seq_printf(filp, "secy%d: Ctrl Mcast pkts: %lld\n", secy_id,
			   stats.ctl_pkt_mcast_cnt);
		seq_printf(filp, "secy%d: Ctrl ucast pkts: %lld\n", secy_id,
			   stats.ctl_pkt_ucast_cnt);
		seq_printf(filp, "secy%d: Ctrl octets: %lld\n", secy_id, stats.ctl_octet_cnt);
		seq_printf(filp, "secy%d: Unctrl bcast cnt: %lld\n", secy_id,
			   stats.unctl_pkt_bcast_cnt);
		seq_printf(filp, "secy%d: Unctrl mcast pkts: %lld\n", secy_id,
			   stats.unctl_pkt_mcast_cnt);
		seq_printf(filp, "secy%d: Unctrl ucast pkts: %lld\n", secy_id,
			   stats.unctl_pkt_ucast_cnt);
		seq_printf(filp, "secy%d: Unctrl octets: %lld\n", secy_id, stats.unctl_octet_cnt);
		seq_printf(filp, "secy%d: Octet decrypted: %lld\n", secy_id,
			   stats.octet_decrypted_cnt);
		seq_printf(filp, "secy%d: octet validated: %lld\n", secy_id,
			   stats.octet_validated_cnt);
		seq_printf(filp, "secy%d: Pkts on disable port: %lld\n", secy_id,
			   stats.pkt_port_disabled_cnt);
		seq_printf(filp, "secy%d: Pkts with badtag: %lld\n", secy_id, stats.pkt_badtag_cnt);
		seq_printf(filp, "secy%d: Pkts with no SA(sectag.tci.c=0): %lld\n", secy_id,
			   stats.pkt_nosa_cnt);
		seq_printf(filp, "secy%d: Pkts with nosaerror: %lld\n", secy_id,
			   stats.pkt_nosaerror_cnt);
		seq_printf(filp, "secy%d: Tagged ctrl pkts: %lld\n", secy_id,
			   stats.pkt_tagged_ctl_cnt);
		seq_printf(filp, "secy%d: Untaged pkts: %lld\n", secy_id, stats.pkt_untaged_cnt);
		seq_printf(filp, "secy%d: Ctrl pkts: %lld\n", secy_id, stats.pkt_ctl_cnt);
		if (mcs->hw->mcs_blks > 1)
			seq_printf(filp, "secy%d: pkts notag: %lld\n", secy_id,
				   stats.pkt_notag_cnt);
	}
	mutex_unlock(&mcs->stats_lock);
	return 0;
}

RVU_DEBUG_SEQ_FOPS(mcs_rx_secy_stats, mcs_rx_secy_stats_display, NULL);

static void rvu_dbg_mcs_init(struct rvu *rvu)
{
	struct mcs *mcs;
	char dname[10];
	int i;

	if (!rvu->mcs_blk_cnt)
		return;

	rvu->rvu_dbg.mcs_root = debugfs_create_dir("mcs", rvu->rvu_dbg.root);

	for (i = 0; i < rvu->mcs_blk_cnt; i++) {
		mcs = mcs_get_pdata(i);

		sprintf(dname, "mcs%d", i);
		rvu->rvu_dbg.mcs = debugfs_create_dir(dname,
						      rvu->rvu_dbg.mcs_root);

		rvu->rvu_dbg.mcs_rx = debugfs_create_dir("rx_stats", rvu->rvu_dbg.mcs);

		debugfs_create_file("flowid", 0600, rvu->rvu_dbg.mcs_rx, mcs,
				    &rvu_dbg_mcs_rx_flowid_stats_fops);

		debugfs_create_file("secy", 0600, rvu->rvu_dbg.mcs_rx, mcs,
				    &rvu_dbg_mcs_rx_secy_stats_fops);

		debugfs_create_file("sc", 0600, rvu->rvu_dbg.mcs_rx, mcs,
				    &rvu_dbg_mcs_rx_sc_stats_fops);

		debugfs_create_file("sa", 0600, rvu->rvu_dbg.mcs_rx, mcs,
				    &rvu_dbg_mcs_rx_sa_stats_fops);

		debugfs_create_file("port", 0600, rvu->rvu_dbg.mcs_rx, mcs,
				    &rvu_dbg_mcs_rx_port_stats_fops);

		rvu->rvu_dbg.mcs_tx = debugfs_create_dir("tx_stats", rvu->rvu_dbg.mcs);

		debugfs_create_file("flowid", 0600, rvu->rvu_dbg.mcs_tx, mcs,
				    &rvu_dbg_mcs_tx_flowid_stats_fops);

		debugfs_create_file("secy", 0600, rvu->rvu_dbg.mcs_tx, mcs,
				    &rvu_dbg_mcs_tx_secy_stats_fops);

		debugfs_create_file("sc", 0600, rvu->rvu_dbg.mcs_tx, mcs,
				    &rvu_dbg_mcs_tx_sc_stats_fops);

		debugfs_create_file("sa", 0600, rvu->rvu_dbg.mcs_tx, mcs,
				    &rvu_dbg_mcs_tx_sa_stats_fops);

		debugfs_create_file("port", 0600, rvu->rvu_dbg.mcs_tx, mcs,
				    &rvu_dbg_mcs_tx_port_stats_fops);
	}
}

#define LMT_MAPTBL_ENTRY_SIZE 16
/* Dump LMTST map table */
static ssize_t rvu_dbg_lmtst_map_table_display(struct file *filp,
					       char __user *buffer,
					       size_t count, loff_t *ppos)
{
	struct rvu *rvu = filp->private_data;
	u64 lmt_addr, val, tbl_base;
	int pf, vf, num_vfs, hw_vfs;
	void __iomem *lmt_map_base;
	int buf_size = 10240;
	size_t off = 0;
	int index = 0;
	char *buf;
	int ret;

	/* don't allow partial reads */
	if (*ppos != 0)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	tbl_base = rvu_read64(rvu, BLKADDR_APR, APR_AF_LMT_MAP_BASE);

	lmt_map_base = ioremap_wc(tbl_base, 128 * 1024);
	if (!lmt_map_base) {
		dev_err(rvu->dev, "Failed to setup lmt map table mapping!!\n");
		kfree(buf);
		return false;
	}

	off +=	scnprintf(&buf[off], buf_size - 1 - off,
			  "\n\t\t\t\t\tLmtst Map Table Entries");
	off +=	scnprintf(&buf[off], buf_size - 1 - off,
			  "\n\t\t\t\t\t=======================");
	off +=	scnprintf(&buf[off], buf_size - 1 - off, "\nPcifunc\t\t\t");
	off +=	scnprintf(&buf[off], buf_size - 1 - off, "Table Index\t\t");
	off +=	scnprintf(&buf[off], buf_size - 1 - off,
			  "Lmtline Base (word 0)\t\t");
	off +=	scnprintf(&buf[off], buf_size - 1 - off,
			  "Lmt Map Entry (word 1)");
	off += scnprintf(&buf[off], buf_size - 1 - off, "\n");
	for (pf = 0; pf < rvu->hw->total_pfs; pf++) {
		off += scnprintf(&buf[off], buf_size - 1 - off, "PF%d  \t\t\t",
				    pf);

		index = pf * rvu->hw->total_vfs * LMT_MAPTBL_ENTRY_SIZE;
		off += scnprintf(&buf[off], buf_size - 1 - off, " 0x%llx\t\t",
				 (tbl_base + index));
		lmt_addr = readq(lmt_map_base + index);
		off += scnprintf(&buf[off], buf_size - 1 - off,
				 " 0x%016llx\t\t", lmt_addr);
		index += 8;
		val = readq(lmt_map_base + index);
		off += scnprintf(&buf[off], buf_size - 1 - off, " 0x%016llx\n",
				 val);
		/* Reading num of VFs per PF */
		rvu_get_pf_numvfs(rvu, pf, &num_vfs, &hw_vfs);
		for (vf = 0; vf < num_vfs; vf++) {
			index = (pf * rvu->hw->total_vfs * 16) +
				((vf + 1)  * LMT_MAPTBL_ENTRY_SIZE);
			off += scnprintf(&buf[off], buf_size - 1 - off,
					    "PF%d:VF%d  \t\t", pf, vf);
			off += scnprintf(&buf[off], buf_size - 1 - off,
					 " 0x%llx\t\t", (tbl_base + index));
			lmt_addr = readq(lmt_map_base + index);
			off += scnprintf(&buf[off], buf_size - 1 - off,
					 " 0x%016llx\t\t", lmt_addr);
			index += 8;
			val = readq(lmt_map_base + index);
			off += scnprintf(&buf[off], buf_size - 1 - off,
					 " 0x%016llx\n", val);
		}
	}
	off +=	scnprintf(&buf[off], buf_size - 1 - off, "\n");

	ret = min(off, count);
	if (copy_to_user(buffer, buf, ret))
		ret = -EFAULT;
	kfree(buf);

	iounmap(lmt_map_base);
	if (ret < 0)
		return ret;

	*ppos = ret;
	return ret;
}

RVU_DEBUG_FOPS(lmtst_map_table, lmtst_map_table_display, NULL);

static void get_lf_str_list(struct rvu_block block, int pcifunc,
			    char *lfs)
{
	int lf = 0, seq = 0, len = 0, prev_lf = block.lf.max;

	for_each_set_bit(lf, block.lf.bmap, block.lf.max) {
		if (lf >= block.lf.max)
			break;

		if (block.fn_map[lf] != pcifunc)
			continue;

		if (lf == prev_lf + 1) {
			prev_lf = lf;
			seq = 1;
			continue;
		}

		if (seq)
			len += sprintf(lfs + len, "-%d,%d", prev_lf, lf);
		else
			len += (len ? sprintf(lfs + len, ",%d", lf) :
				      sprintf(lfs + len, "%d", lf));

		prev_lf = lf;
		seq = 0;
	}

	if (seq)
		len += sprintf(lfs + len, "-%d", prev_lf);

	lfs[len] = '\0';
}

static int get_max_column_width(struct rvu *rvu)
{
	int index, pf, vf, lf_str_size = 12, buf_size = 256;
	struct rvu_block block;
	u16 pcifunc;
	char *buf;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (pf = 0; pf < rvu->hw->total_pfs; pf++) {
		for (vf = 0; vf <= rvu->hw->total_vfs; vf++) {
			pcifunc = pf << 10 | vf;
			if (!pcifunc)
				continue;

			for (index = 0; index < BLK_COUNT; index++) {
				block = rvu->hw->block[index];
				if (!strlen(block.name))
					continue;

				get_lf_str_list(block, pcifunc, buf);
				if (lf_str_size <= strlen(buf))
					lf_str_size = strlen(buf) + 1;
			}
		}
	}

	kfree(buf);
	return lf_str_size;
}

/* Dumps current provisioning status of all RVU block LFs */
static ssize_t rvu_dbg_rsrc_attach_status(struct file *filp,
					  char __user *buffer,
					  size_t count, loff_t *ppos)
{
	int index, off = 0, flag = 0, len = 0, i = 0;
	struct rvu *rvu = filp->private_data;
	int bytes_not_copied = 0;
	struct rvu_block block;
	int pf, vf, pcifunc;
	int buf_size = 2048;
	int lf_str_size;
	char *lfs;
	char *buf;

	/* don't allow partial reads */
	if (*ppos != 0)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Get the maximum width of a column */
	lf_str_size = get_max_column_width(rvu);

	lfs = kzalloc(lf_str_size, GFP_KERNEL);
	if (!lfs) {
		kfree(buf);
		return -ENOMEM;
	}
	off +=	scnprintf(&buf[off], buf_size - 1 - off, "%-*s", lf_str_size,
			  "pcifunc");
	for (index = 0; index < BLK_COUNT; index++)
		if (strlen(rvu->hw->block[index].name)) {
			off += scnprintf(&buf[off], buf_size - 1 - off,
					 "%-*s", lf_str_size,
					 rvu->hw->block[index].name);
		}

	off += scnprintf(&buf[off], buf_size - 1 - off, "\n");
	bytes_not_copied = copy_to_user(buffer + (i * off), buf, off);
	if (bytes_not_copied)
		goto out;

	i++;
	*ppos += off;
	for (pf = 0; pf < rvu->hw->total_pfs; pf++) {
		for (vf = 0; vf <= rvu->hw->total_vfs; vf++) {
			off = 0;
			flag = 0;
			pcifunc = pf << 10 | vf;
			if (!pcifunc)
				continue;

			if (vf) {
				sprintf(lfs, "PF%d:VF%d", pf, vf - 1);
				off = scnprintf(&buf[off],
						buf_size - 1 - off,
						"%-*s", lf_str_size, lfs);
			} else {
				sprintf(lfs, "PF%d", pf);
				off = scnprintf(&buf[off],
						buf_size - 1 - off,
						"%-*s", lf_str_size, lfs);
			}

			for (index = 0; index < BLK_COUNT; index++) {
				block = rvu->hw->block[index];
				if (!strlen(block.name))
					continue;
				len = 0;
				lfs[len] = '\0';
				get_lf_str_list(block, pcifunc, lfs);
				if (strlen(lfs))
					flag = 1;

				off += scnprintf(&buf[off], buf_size - 1 - off,
						 "%-*s", lf_str_size, lfs);
			}
			if (flag) {
				off +=	scnprintf(&buf[off],
						  buf_size - 1 - off, "\n");
				bytes_not_copied = copy_to_user(buffer +
								(i * off),
								buf, off);
				if (bytes_not_copied)
					goto out;

				i++;
				*ppos += off;
			}
		}
	}

out:
	kfree(lfs);
	kfree(buf);
	if (bytes_not_copied)
		return -EFAULT;

	return *ppos;
}

RVU_DEBUG_FOPS(rsrc_status, rsrc_attach_status, NULL);

static int rvu_dbg_rvu_pf_cgx_map_display(struct seq_file *filp, void *unused)
{
	struct rvu *rvu = filp->private;
	struct pci_dev *pdev = NULL;
	struct mac_ops *mac_ops;
	char cgx[10], lmac[10];
	struct rvu_pfvf *pfvf;
	int pf, domain, blkid;
	u8 cgx_id, lmac_id;
	u16 pcifunc;

	domain = 2;
	mac_ops = get_mac_ops(rvu_first_cgx_pdata(rvu));
	/* There can be no CGX devices at all */
	if (!mac_ops)
		return 0;
	seq_printf(filp, "PCI dev\t\tRVU PF Func\tNIX block\t%s\tLMAC\n",
		   mac_ops->name);
	for (pf = 0; pf < rvu->hw->total_pfs; pf++) {
		if (!is_pf_cgxmapped(rvu, pf))
			continue;

		pdev =  pci_get_domain_bus_and_slot(domain, pf + 1, 0);
		if (!pdev)
			continue;

		cgx[0] = 0;
		lmac[0] = 0;
		pcifunc = pf << 10;
		pfvf = rvu_get_pfvf(rvu, pcifunc);

		if (pfvf->nix_blkaddr == BLKADDR_NIX0)
			blkid = 0;
		else
			blkid = 1;

		rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id,
				    &lmac_id);
		sprintf(cgx, "%s%d", mac_ops->name, cgx_id);
		sprintf(lmac, "LMAC%d", lmac_id);
		seq_printf(filp, "%s\t0x%x\t\tNIX%d\t\t%s\t%s\n",
			   dev_name(&pdev->dev), pcifunc, blkid, cgx, lmac);

		pci_dev_put(pdev);
	}
	return 0;
}

RVU_DEBUG_SEQ_FOPS(rvu_pf_cgx_map, rvu_pf_cgx_map_display, NULL);

static bool rvu_dbg_is_valid_lf(struct rvu *rvu, int blkaddr, int lf,
				u16 *pcifunc)
{
	struct rvu_block *block;
	struct rvu_hwinfo *hw;

	hw = rvu->hw;
	block = &hw->block[blkaddr];

	if (lf < 0 || lf >= block->lf.max) {
		dev_warn(rvu->dev, "Invalid LF: valid range: 0-%d\n",
			 block->lf.max - 1);
		return false;
	}

	*pcifunc = block->fn_map[lf];
	if (!*pcifunc) {
		dev_warn(rvu->dev,
			 "This LF is not attached to any RVU PFFUNC\n");
		return false;
	}
	return true;
}

static void print_npa_qsize(struct seq_file *m, struct rvu_pfvf *pfvf)
{
	char *buf;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return;

	if (!pfvf->aura_ctx) {
		seq_puts(m, "Aura context is not initialized\n");
	} else {
		bitmap_print_to_pagebuf(false, buf, pfvf->aura_bmap,
					pfvf->aura_ctx->qsize);
		seq_printf(m, "Aura count : %d\n", pfvf->aura_ctx->qsize);
		seq_printf(m, "Aura context ena/dis bitmap : %s\n", buf);
	}

	if (!pfvf->pool_ctx) {
		seq_puts(m, "Pool context is not initialized\n");
	} else {
		bitmap_print_to_pagebuf(false, buf, pfvf->pool_bmap,
					pfvf->pool_ctx->qsize);
		seq_printf(m, "Pool count : %d\n", pfvf->pool_ctx->qsize);
		seq_printf(m, "Pool context ena/dis bitmap : %s\n", buf);
	}
	kfree(buf);
}

/* The 'qsize' entry dumps current Aura/Pool context Qsize
 * and each context's current enable/disable status in a bitmap.
 */
static int rvu_dbg_qsize_display(struct seq_file *filp, void *unsused,
				 int blktype)
{
	void (*print_qsize)(struct seq_file *filp,
			    struct rvu_pfvf *pfvf) = NULL;
	struct dentry *current_dir;
	struct rvu_pfvf *pfvf;
	struct rvu *rvu;
	int qsize_id;
	u16 pcifunc;
	int blkaddr;

	rvu = filp->private;
	switch (blktype) {
	case BLKTYPE_NPA:
		qsize_id = rvu->rvu_dbg.npa_qsize_id;
		print_qsize = print_npa_qsize;
		break;

	case BLKTYPE_NIX:
		qsize_id = rvu->rvu_dbg.nix_qsize_id;
		print_qsize = print_nix_qsize;
		break;

	default:
		return -EINVAL;
	}

	if (blktype == BLKTYPE_NPA) {
		blkaddr = BLKADDR_NPA;
	} else {
		current_dir = filp->file->f_path.dentry->d_parent;
		blkaddr = (!strcmp(current_dir->d_name.name, "nix1") ?
				   BLKADDR_NIX1 : BLKADDR_NIX0);
	}

	if (!rvu_dbg_is_valid_lf(rvu, blkaddr, qsize_id, &pcifunc))
		return -EINVAL;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	print_qsize(filp, pfvf);

	return 0;
}

static ssize_t rvu_dbg_qsize_write(struct file *filp,
				   const char __user *buffer, size_t count,
				   loff_t *ppos, int blktype)
{
	char *blk_string = (blktype == BLKTYPE_NPA) ? "npa" : "nix";
	struct seq_file *seqfile = filp->private_data;
	char *cmd_buf, *cmd_buf_tmp, *subtoken;
	struct rvu *rvu = seqfile->private;
	struct dentry *current_dir;
	int blkaddr;
	u16 pcifunc;
	int ret, lf;

	cmd_buf = memdup_user(buffer, count + 1);
	if (IS_ERR(cmd_buf))
		return -ENOMEM;

	cmd_buf[count] = '\0';

	cmd_buf_tmp = strchr(cmd_buf, '\n');
	if (cmd_buf_tmp) {
		*cmd_buf_tmp = '\0';
		count = cmd_buf_tmp - cmd_buf + 1;
	}

	cmd_buf_tmp = cmd_buf;
	subtoken = strsep(&cmd_buf, " ");
	ret = subtoken ? kstrtoint(subtoken, 10, &lf) : -EINVAL;
	if (cmd_buf)
		ret = -EINVAL;

	if (ret < 0 || !strncmp(subtoken, "help", 4)) {
		dev_info(rvu->dev, "Use echo <%s-lf > qsize\n", blk_string);
		goto qsize_write_done;
	}

	if (blktype == BLKTYPE_NPA) {
		blkaddr = BLKADDR_NPA;
	} else {
		current_dir = filp->f_path.dentry->d_parent;
		blkaddr = (!strcmp(current_dir->d_name.name, "nix1") ?
				   BLKADDR_NIX1 : BLKADDR_NIX0);
	}

	if (!rvu_dbg_is_valid_lf(rvu, blkaddr, lf, &pcifunc)) {
		ret = -EINVAL;
		goto qsize_write_done;
	}
	if (blktype  == BLKTYPE_NPA)
		rvu->rvu_dbg.npa_qsize_id = lf;
	else
		rvu->rvu_dbg.nix_qsize_id = lf;

qsize_write_done:
	kfree(cmd_buf_tmp);
	return ret ? ret : count;
}

static ssize_t rvu_dbg_npa_qsize_write(struct file *filp,
				       const char __user *buffer,
				       size_t count, loff_t *ppos)
{
	return rvu_dbg_qsize_write(filp, buffer, count, ppos,
					    BLKTYPE_NPA);
}

static int rvu_dbg_npa_qsize_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_qsize_display(filp, unused, BLKTYPE_NPA);
}

RVU_DEBUG_SEQ_FOPS(npa_qsize, npa_qsize_display, npa_qsize_write);

/* Dumps given NPA Aura's context */
static void print_npa_aura_ctx(struct seq_file *m, struct npa_aq_enq_rsp *rsp)
{
	struct npa_aura_s *aura = &rsp->aura;
	struct rvu *rvu = m->private;

	seq_printf(m, "W0: Pool addr\t\t%llx\n", aura->pool_addr);

	seq_printf(m, "W1: ena\t\t\t%d\nW1: pool caching\t%d\n",
		   aura->ena, aura->pool_caching);
	seq_printf(m, "W1: pool way mask\t%d\nW1: avg con\t\t%d\n",
		   aura->pool_way_mask, aura->avg_con);
	seq_printf(m, "W1: pool drop ena\t%d\nW1: aura drop ena\t%d\n",
		   aura->pool_drop_ena, aura->aura_drop_ena);
	seq_printf(m, "W1: bp_ena\t\t%d\nW1: aura drop\t\t%d\n",
		   aura->bp_ena, aura->aura_drop);
	seq_printf(m, "W1: aura shift\t\t%d\nW1: avg_level\t\t%d\n",
		   aura->shift, aura->avg_level);

	seq_printf(m, "W2: count\t\t%llu\nW2: nix0_bpid\t\t%d\nW2: nix1_bpid\t\t%d\n",
		   (u64)aura->count, aura->nix0_bpid, aura->nix1_bpid);

	seq_printf(m, "W3: limit\t\t%llu\nW3: bp\t\t\t%d\nW3: fc_ena\t\t%d\n",
		   (u64)aura->limit, aura->bp, aura->fc_ena);

	if (!is_rvu_otx2(rvu))
		seq_printf(m, "W3: fc_be\t\t%d\n", aura->fc_be);
	seq_printf(m, "W3: fc_up_crossing\t%d\nW3: fc_stype\t\t%d\n",
		   aura->fc_up_crossing, aura->fc_stype);
	seq_printf(m, "W3: fc_hyst_bits\t%d\n", aura->fc_hyst_bits);

	seq_printf(m, "W4: fc_addr\t\t%llx\n", aura->fc_addr);

	seq_printf(m, "W5: pool_drop\t\t%d\nW5: update_time\t\t%d\n",
		   aura->pool_drop, aura->update_time);
	seq_printf(m, "W5: err_int \t\t%d\nW5: err_int_ena\t\t%d\n",
		   aura->err_int, aura->err_int_ena);
	seq_printf(m, "W5: thresh_int\t\t%d\nW5: thresh_int_ena \t%d\n",
		   aura->thresh_int, aura->thresh_int_ena);
	seq_printf(m, "W5: thresh_up\t\t%d\nW5: thresh_qint_idx\t%d\n",
		   aura->thresh_up, aura->thresh_qint_idx);
	seq_printf(m, "W5: err_qint_idx \t%d\n", aura->err_qint_idx);

	seq_printf(m, "W6: thresh\t\t%llu\n", (u64)aura->thresh);
	if (!is_rvu_otx2(rvu))
		seq_printf(m, "W6: fc_msh_dst\t\t%d\n", aura->fc_msh_dst);
}

/* Dumps given NPA Pool's context */
static void print_npa_pool_ctx(struct seq_file *m, struct npa_aq_enq_rsp *rsp)
{
	struct npa_pool_s *pool = &rsp->pool;
	struct rvu *rvu = m->private;

	seq_printf(m, "W0: Stack base\t\t%llx\n", pool->stack_base);

	seq_printf(m, "W1: ena \t\t%d\nW1: nat_align \t\t%d\n",
		   pool->ena, pool->nat_align);
	seq_printf(m, "W1: stack_caching\t%d\nW1: stack_way_mask\t%d\n",
		   pool->stack_caching, pool->stack_way_mask);
	seq_printf(m, "W1: buf_offset\t\t%d\nW1: buf_size\t\t%d\n",
		   pool->buf_offset, pool->buf_size);

	seq_printf(m, "W2: stack_max_pages \t%d\nW2: stack_pages\t\t%d\n",
		   pool->stack_max_pages, pool->stack_pages);

	seq_printf(m, "W3: op_pc \t\t%llu\n", (u64)pool->op_pc);

	seq_printf(m, "W4: stack_offset\t%d\nW4: shift\t\t%d\nW4: avg_level\t\t%d\n",
		   pool->stack_offset, pool->shift, pool->avg_level);
	seq_printf(m, "W4: avg_con \t\t%d\nW4: fc_ena\t\t%d\nW4: fc_stype\t\t%d\n",
		   pool->avg_con, pool->fc_ena, pool->fc_stype);
	seq_printf(m, "W4: fc_hyst_bits\t%d\nW4: fc_up_crossing\t%d\n",
		   pool->fc_hyst_bits, pool->fc_up_crossing);
	if (!is_rvu_otx2(rvu))
		seq_printf(m, "W4: fc_be\t\t%d\n", pool->fc_be);
	seq_printf(m, "W4: update_time\t\t%d\n", pool->update_time);

	seq_printf(m, "W5: fc_addr\t\t%llx\n", pool->fc_addr);

	seq_printf(m, "W6: ptr_start\t\t%llx\n", pool->ptr_start);

	seq_printf(m, "W7: ptr_end\t\t%llx\n", pool->ptr_end);

	seq_printf(m, "W8: err_int\t\t%d\nW8: err_int_ena\t\t%d\n",
		   pool->err_int, pool->err_int_ena);
	seq_printf(m, "W8: thresh_int\t\t%d\n", pool->thresh_int);
	seq_printf(m, "W8: thresh_int_ena\t%d\nW8: thresh_up\t\t%d\n",
		   pool->thresh_int_ena, pool->thresh_up);
	seq_printf(m, "W8: thresh_qint_idx\t%d\nW8: err_qint_idx\t%d\n",
		   pool->thresh_qint_idx, pool->err_qint_idx);
	if (!is_rvu_otx2(rvu))
		seq_printf(m, "W8: fc_msh_dst\t\t%d\n", pool->fc_msh_dst);
}

/* Reads aura/pool's ctx from admin queue */
static int rvu_dbg_npa_ctx_display(struct seq_file *m, void *unused, int ctype)
{
	void (*print_npa_ctx)(struct seq_file *m, struct npa_aq_enq_rsp *rsp);
	struct npa_aq_enq_req aq_req;
	struct npa_aq_enq_rsp rsp;
	struct rvu_pfvf *pfvf;
	int aura, rc, max_id;
	int npalf, id, all;
	struct rvu *rvu;
	u16 pcifunc;

	rvu = m->private;

	switch (ctype) {
	case NPA_AQ_CTYPE_AURA:
		npalf = rvu->rvu_dbg.npa_aura_ctx.lf;
		id = rvu->rvu_dbg.npa_aura_ctx.id;
		all = rvu->rvu_dbg.npa_aura_ctx.all;
		break;

	case NPA_AQ_CTYPE_POOL:
		npalf = rvu->rvu_dbg.npa_pool_ctx.lf;
		id = rvu->rvu_dbg.npa_pool_ctx.id;
		all = rvu->rvu_dbg.npa_pool_ctx.all;
		break;
	default:
		return -EINVAL;
	}

	if (!rvu_dbg_is_valid_lf(rvu, BLKADDR_NPA, npalf, &pcifunc))
		return -EINVAL;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	if (ctype == NPA_AQ_CTYPE_AURA && !pfvf->aura_ctx) {
		seq_puts(m, "Aura context is not initialized\n");
		return -EINVAL;
	} else if (ctype == NPA_AQ_CTYPE_POOL && !pfvf->pool_ctx) {
		seq_puts(m, "Pool context is not initialized\n");
		return -EINVAL;
	}

	memset(&aq_req, 0, sizeof(struct npa_aq_enq_req));
	aq_req.hdr.pcifunc = pcifunc;
	aq_req.ctype = ctype;
	aq_req.op = NPA_AQ_INSTOP_READ;
	if (ctype == NPA_AQ_CTYPE_AURA) {
		max_id = pfvf->aura_ctx->qsize;
		print_npa_ctx = print_npa_aura_ctx;
	} else {
		max_id = pfvf->pool_ctx->qsize;
		print_npa_ctx = print_npa_pool_ctx;
	}

	if (id < 0 || id >= max_id) {
		seq_printf(m, "Invalid %s, valid range is 0-%d\n",
			   (ctype == NPA_AQ_CTYPE_AURA) ? "aura" : "pool",
			max_id - 1);
		return -EINVAL;
	}

	if (all)
		id = 0;
	else
		max_id = id + 1;

	for (aura = id; aura < max_id; aura++) {
		aq_req.aura_id = aura;

		/* Skip if queue is uninitialized */
		if (ctype == NPA_AQ_CTYPE_POOL && !test_bit(aura, pfvf->pool_bmap))
			continue;

		seq_printf(m, "======%s : %d=======\n",
			   (ctype == NPA_AQ_CTYPE_AURA) ? "AURA" : "POOL",
			aq_req.aura_id);
		rc = rvu_npa_aq_enq_inst(rvu, &aq_req, &rsp);
		if (rc) {
			seq_puts(m, "Failed to read context\n");
			return -EINVAL;
		}
		print_npa_ctx(m, &rsp);
	}
	return 0;
}

static int write_npa_ctx(struct rvu *rvu, bool all,
			 int npalf, int id, int ctype)
{
	struct rvu_pfvf *pfvf;
	int max_id = 0;
	u16 pcifunc;

	if (!rvu_dbg_is_valid_lf(rvu, BLKADDR_NPA, npalf, &pcifunc))
		return -EINVAL;

	pfvf = rvu_get_pfvf(rvu, pcifunc);

	if (ctype == NPA_AQ_CTYPE_AURA) {
		if (!pfvf->aura_ctx) {
			dev_warn(rvu->dev, "Aura context is not initialized\n");
			return -EINVAL;
		}
		max_id = pfvf->aura_ctx->qsize;
	} else if (ctype == NPA_AQ_CTYPE_POOL) {
		if (!pfvf->pool_ctx) {
			dev_warn(rvu->dev, "Pool context is not initialized\n");
			return -EINVAL;
		}
		max_id = pfvf->pool_ctx->qsize;
	}

	if (id < 0 || id >= max_id) {
		dev_warn(rvu->dev, "Invalid %s, valid range is 0-%d\n",
			 (ctype == NPA_AQ_CTYPE_AURA) ? "aura" : "pool",
			max_id - 1);
		return -EINVAL;
	}

	switch (ctype) {
	case NPA_AQ_CTYPE_AURA:
		rvu->rvu_dbg.npa_aura_ctx.lf = npalf;
		rvu->rvu_dbg.npa_aura_ctx.id = id;
		rvu->rvu_dbg.npa_aura_ctx.all = all;
		break;

	case NPA_AQ_CTYPE_POOL:
		rvu->rvu_dbg.npa_pool_ctx.lf = npalf;
		rvu->rvu_dbg.npa_pool_ctx.id = id;
		rvu->rvu_dbg.npa_pool_ctx.all = all;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int parse_cmd_buffer_ctx(char *cmd_buf, size_t *count,
				const char __user *buffer, int *npalf,
				int *id, bool *all)
{
	int bytes_not_copied;
	char *cmd_buf_tmp;
	char *subtoken;
	int ret;

	bytes_not_copied = copy_from_user(cmd_buf, buffer, *count);
	if (bytes_not_copied)
		return -EFAULT;

	cmd_buf[*count] = '\0';
	cmd_buf_tmp = strchr(cmd_buf, '\n');

	if (cmd_buf_tmp) {
		*cmd_buf_tmp = '\0';
		*count = cmd_buf_tmp - cmd_buf + 1;
	}

	subtoken = strsep(&cmd_buf, " ");
	ret = subtoken ? kstrtoint(subtoken, 10, npalf) : -EINVAL;
	if (ret < 0)
		return ret;
	subtoken = strsep(&cmd_buf, " ");
	if (subtoken && strcmp(subtoken, "all") == 0) {
		*all = true;
	} else {
		ret = subtoken ? kstrtoint(subtoken, 10, id) : -EINVAL;
		if (ret < 0)
			return ret;
	}
	if (cmd_buf)
		return -EINVAL;
	return ret;
}

static ssize_t rvu_dbg_npa_ctx_write(struct file *filp,
				     const char __user *buffer,
				     size_t count, loff_t *ppos, int ctype)
{
	char *cmd_buf, *ctype_string = (ctype == NPA_AQ_CTYPE_AURA) ?
					"aura" : "pool";
	struct seq_file *seqfp = filp->private_data;
	struct rvu *rvu = seqfp->private;
	int npalf, id = 0, ret;
	bool all = false;

	if ((*ppos != 0) || !count)
		return -EINVAL;

	cmd_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!cmd_buf)
		return count;
	ret = parse_cmd_buffer_ctx(cmd_buf, &count, buffer,
				   &npalf, &id, &all);
	if (ret < 0) {
		dev_info(rvu->dev,
			 "Usage: echo <npalf> [%s number/all] > %s_ctx\n",
			 ctype_string, ctype_string);
		goto done;
	} else {
		ret = write_npa_ctx(rvu, all, npalf, id, ctype);
	}
done:
	kfree(cmd_buf);
	return ret ? ret : count;
}

static ssize_t rvu_dbg_npa_aura_ctx_write(struct file *filp,
					  const char __user *buffer,
					  size_t count, loff_t *ppos)
{
	return rvu_dbg_npa_ctx_write(filp, buffer, count, ppos,
				     NPA_AQ_CTYPE_AURA);
}

static int rvu_dbg_npa_aura_ctx_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_npa_ctx_display(filp, unused, NPA_AQ_CTYPE_AURA);
}

RVU_DEBUG_SEQ_FOPS(npa_aura_ctx, npa_aura_ctx_display, npa_aura_ctx_write);

static ssize_t rvu_dbg_npa_pool_ctx_write(struct file *filp,
					  const char __user *buffer,
					  size_t count, loff_t *ppos)
{
	return rvu_dbg_npa_ctx_write(filp, buffer, count, ppos,
				     NPA_AQ_CTYPE_POOL);
}

static int rvu_dbg_npa_pool_ctx_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_npa_ctx_display(filp, unused, NPA_AQ_CTYPE_POOL);
}

RVU_DEBUG_SEQ_FOPS(npa_pool_ctx, npa_pool_ctx_display, npa_pool_ctx_write);

static void ndc_cache_stats(struct seq_file *s, int blk_addr,
			    int ctype, int transaction)
{
	u64 req, out_req, lat, cant_alloc;
	struct nix_hw *nix_hw;
	struct rvu *rvu;
	int port;

	if (blk_addr == BLKADDR_NDC_NPA0) {
		rvu = s->private;
	} else {
		nix_hw = s->private;
		rvu = nix_hw->rvu;
	}

	for (port = 0; port < NDC_MAX_PORT; port++) {
		req = rvu_read64(rvu, blk_addr, NDC_AF_PORTX_RTX_RWX_REQ_PC
						(port, ctype, transaction));
		lat = rvu_read64(rvu, blk_addr, NDC_AF_PORTX_RTX_RWX_LAT_PC
						(port, ctype, transaction));
		out_req = rvu_read64(rvu, blk_addr,
				     NDC_AF_PORTX_RTX_RWX_OSTDN_PC
				     (port, ctype, transaction));
		cant_alloc = rvu_read64(rvu, blk_addr,
					NDC_AF_PORTX_RTX_CANT_ALLOC_PC
					(port, transaction));
		seq_printf(s, "\nPort:%d\n", port);
		seq_printf(s, "\tTotal Requests:\t\t%lld\n", req);
		seq_printf(s, "\tTotal Time Taken:\t%lld cycles\n", lat);
		seq_printf(s, "\tAvg Latency:\t\t%lld cycles\n", lat / req);
		seq_printf(s, "\tOutstanding Requests:\t%lld\n", out_req);
		seq_printf(s, "\tCant Alloc Requests:\t%lld\n", cant_alloc);
	}
}

static int ndc_blk_cache_stats(struct seq_file *s, int idx, int blk_addr)
{
	seq_puts(s, "\n***** CACHE mode read stats *****\n");
	ndc_cache_stats(s, blk_addr, CACHING, NDC_READ_TRANS);
	seq_puts(s, "\n***** CACHE mode write stats *****\n");
	ndc_cache_stats(s, blk_addr, CACHING, NDC_WRITE_TRANS);
	seq_puts(s, "\n***** BY-PASS mode read stats *****\n");
	ndc_cache_stats(s, blk_addr, BYPASS, NDC_READ_TRANS);
	seq_puts(s, "\n***** BY-PASS mode write stats *****\n");
	ndc_cache_stats(s, blk_addr, BYPASS, NDC_WRITE_TRANS);
	return 0;
}

static int rvu_dbg_npa_ndc_cache_display(struct seq_file *filp, void *unused)
{
	return ndc_blk_cache_stats(filp, NPA0_U, BLKADDR_NDC_NPA0);
}

RVU_DEBUG_SEQ_FOPS(npa_ndc_cache, npa_ndc_cache_display, NULL);

static int ndc_blk_hits_miss_stats(struct seq_file *s, int idx, int blk_addr)
{
	struct nix_hw *nix_hw;
	struct rvu *rvu;
	int bank, max_bank;
	u64 ndc_af_const;

	if (blk_addr == BLKADDR_NDC_NPA0) {
		rvu = s->private;
	} else {
		nix_hw = s->private;
		rvu = nix_hw->rvu;
	}

	ndc_af_const = rvu_read64(rvu, blk_addr, NDC_AF_CONST);
	max_bank = FIELD_GET(NDC_AF_BANK_MASK, ndc_af_const);
	for (bank = 0; bank < max_bank; bank++) {
		seq_printf(s, "BANK:%d\n", bank);
		seq_printf(s, "\tHits:\t%lld\n",
			   (u64)rvu_read64(rvu, blk_addr,
			   NDC_AF_BANKX_HIT_PC(bank)));
		seq_printf(s, "\tMiss:\t%lld\n",
			   (u64)rvu_read64(rvu, blk_addr,
			    NDC_AF_BANKX_MISS_PC(bank)));
	}
	return 0;
}

static int rvu_dbg_nix_ndc_rx_cache_display(struct seq_file *filp, void *unused)
{
	struct nix_hw *nix_hw = filp->private;
	int blkaddr = 0;
	int ndc_idx = 0;

	blkaddr = (nix_hw->blkaddr == BLKADDR_NIX1 ?
		   BLKADDR_NDC_NIX1_RX : BLKADDR_NDC_NIX0_RX);
	ndc_idx = (nix_hw->blkaddr == BLKADDR_NIX1 ? NIX1_RX : NIX0_RX);

	return ndc_blk_cache_stats(filp, ndc_idx, blkaddr);
}

RVU_DEBUG_SEQ_FOPS(nix_ndc_rx_cache, nix_ndc_rx_cache_display, NULL);

static int rvu_dbg_nix_ndc_tx_cache_display(struct seq_file *filp, void *unused)
{
	struct nix_hw *nix_hw = filp->private;
	int blkaddr = 0;
	int ndc_idx = 0;

	blkaddr = (nix_hw->blkaddr == BLKADDR_NIX1 ?
		   BLKADDR_NDC_NIX1_TX : BLKADDR_NDC_NIX0_TX);
	ndc_idx = (nix_hw->blkaddr == BLKADDR_NIX1 ? NIX1_TX : NIX0_TX);

	return ndc_blk_cache_stats(filp, ndc_idx, blkaddr);
}

RVU_DEBUG_SEQ_FOPS(nix_ndc_tx_cache, nix_ndc_tx_cache_display, NULL);

static int rvu_dbg_npa_ndc_hits_miss_display(struct seq_file *filp,
					     void *unused)
{
	return ndc_blk_hits_miss_stats(filp, NPA0_U, BLKADDR_NDC_NPA0);
}

RVU_DEBUG_SEQ_FOPS(npa_ndc_hits_miss, npa_ndc_hits_miss_display, NULL);

static int rvu_dbg_nix_ndc_rx_hits_miss_display(struct seq_file *filp,
						void *unused)
{
	struct nix_hw *nix_hw = filp->private;
	int ndc_idx = NPA0_U;
	int blkaddr = 0;

	blkaddr = (nix_hw->blkaddr == BLKADDR_NIX1 ?
		   BLKADDR_NDC_NIX1_RX : BLKADDR_NDC_NIX0_RX);

	return ndc_blk_hits_miss_stats(filp, ndc_idx, blkaddr);
}

RVU_DEBUG_SEQ_FOPS(nix_ndc_rx_hits_miss, nix_ndc_rx_hits_miss_display, NULL);

static int rvu_dbg_nix_ndc_tx_hits_miss_display(struct seq_file *filp,
						void *unused)
{
	struct nix_hw *nix_hw = filp->private;
	int ndc_idx = NPA0_U;
	int blkaddr = 0;

	blkaddr = (nix_hw->blkaddr == BLKADDR_NIX1 ?
		   BLKADDR_NDC_NIX1_TX : BLKADDR_NDC_NIX0_TX);

	return ndc_blk_hits_miss_stats(filp, ndc_idx, blkaddr);
}

RVU_DEBUG_SEQ_FOPS(nix_ndc_tx_hits_miss, nix_ndc_tx_hits_miss_display, NULL);

static void print_nix_cn10k_sq_ctx(struct seq_file *m,
				   struct nix_cn10k_sq_ctx_s *sq_ctx)
{
	seq_printf(m, "W0: ena \t\t\t%d\nW0: qint_idx \t\t\t%d\n",
		   sq_ctx->ena, sq_ctx->qint_idx);
	seq_printf(m, "W0: substream \t\t\t0x%03x\nW0: sdp_mcast \t\t\t%d\n",
		   sq_ctx->substream, sq_ctx->sdp_mcast);
	seq_printf(m, "W0: cq \t\t\t\t%d\nW0: sqe_way_mask \t\t%d\n\n",
		   sq_ctx->cq, sq_ctx->sqe_way_mask);

	seq_printf(m, "W1: smq \t\t\t%d\nW1: cq_ena \t\t\t%d\nW1: xoff\t\t\t%d\n",
		   sq_ctx->smq, sq_ctx->cq_ena, sq_ctx->xoff);
	seq_printf(m, "W1: sso_ena \t\t\t%d\nW1: smq_rr_weight\t\t%d\n",
		   sq_ctx->sso_ena, sq_ctx->smq_rr_weight);
	seq_printf(m, "W1: default_chan\t\t%d\nW1: sqb_count\t\t\t%d\n\n",
		   sq_ctx->default_chan, sq_ctx->sqb_count);

	seq_printf(m, "W2: smq_rr_count_lb \t\t%d\n", sq_ctx->smq_rr_count_lb);
	seq_printf(m, "W2: smq_rr_count_ub \t\t%d\n", sq_ctx->smq_rr_count_ub);
	seq_printf(m, "W2: sqb_aura \t\t\t%d\nW2: sq_int \t\t\t%d\n",
		   sq_ctx->sqb_aura, sq_ctx->sq_int);
	seq_printf(m, "W2: sq_int_ena \t\t\t%d\nW2: sqe_stype \t\t\t%d\n",
		   sq_ctx->sq_int_ena, sq_ctx->sqe_stype);

	seq_printf(m, "W3: max_sqe_size\t\t%d\nW3: cq_limit\t\t\t%d\n",
		   sq_ctx->max_sqe_size, sq_ctx->cq_limit);
	seq_printf(m, "W3: lmt_dis \t\t\t%d\nW3: mnq_dis \t\t\t%d\n",
		   sq_ctx->mnq_dis, sq_ctx->lmt_dis);
	seq_printf(m, "W3: smq_next_sq\t\t\t%d\nW3: smq_lso_segnum\t\t%d\n",
		   sq_ctx->smq_next_sq, sq_ctx->smq_lso_segnum);
	seq_printf(m, "W3: tail_offset \t\t%d\nW3: smenq_offset\t\t%d\n",
		   sq_ctx->tail_offset, sq_ctx->smenq_offset);
	seq_printf(m, "W3: head_offset\t\t\t%d\nW3: smenq_next_sqb_vld\t\t%d\n\n",
		   sq_ctx->head_offset, sq_ctx->smenq_next_sqb_vld);

	seq_printf(m, "W3: smq_next_sq_vld\t\t%d\nW3: smq_pend\t\t\t%d\n",
		   sq_ctx->smq_next_sq_vld, sq_ctx->smq_pend);
	seq_printf(m, "W4: next_sqb \t\t\t%llx\n\n", sq_ctx->next_sqb);
	seq_printf(m, "W5: tail_sqb \t\t\t%llx\n\n", sq_ctx->tail_sqb);
	seq_printf(m, "W6: smenq_sqb \t\t\t%llx\n\n", sq_ctx->smenq_sqb);
	seq_printf(m, "W7: smenq_next_sqb \t\t%llx\n\n",
		   sq_ctx->smenq_next_sqb);

	seq_printf(m, "W8: head_sqb\t\t\t%llx\n\n", sq_ctx->head_sqb);

	seq_printf(m, "W9: vfi_lso_total\t\t%d\n", sq_ctx->vfi_lso_total);
	seq_printf(m, "W9: vfi_lso_sizem1\t\t%d\nW9: vfi_lso_sb\t\t\t%d\n",
		   sq_ctx->vfi_lso_sizem1, sq_ctx->vfi_lso_sb);
	seq_printf(m, "W9: vfi_lso_mps\t\t\t%d\nW9: vfi_lso_vlan0_ins_ena\t%d\n",
		   sq_ctx->vfi_lso_mps, sq_ctx->vfi_lso_vlan0_ins_ena);
	seq_printf(m, "W9: vfi_lso_vlan1_ins_ena\t%d\nW9: vfi_lso_vld \t\t%d\n\n",
		   sq_ctx->vfi_lso_vld, sq_ctx->vfi_lso_vlan1_ins_ena);

	seq_printf(m, "W10: scm_lso_rem \t\t%llu\n\n",
		   (u64)sq_ctx->scm_lso_rem);
	seq_printf(m, "W11: octs \t\t\t%llu\n\n", (u64)sq_ctx->octs);
	seq_printf(m, "W12: pkts \t\t\t%llu\n\n", (u64)sq_ctx->pkts);
	seq_printf(m, "W14: dropped_octs \t\t%llu\n\n",
		   (u64)sq_ctx->dropped_octs);
	seq_printf(m, "W15: dropped_pkts \t\t%llu\n\n",
		   (u64)sq_ctx->dropped_pkts);
}

/* Dumps given nix_sq's context */
static void print_nix_sq_ctx(struct seq_file *m, struct nix_aq_enq_rsp *rsp)
{
	struct nix_sq_ctx_s *sq_ctx = &rsp->sq;
	struct nix_hw *nix_hw = m->private;
	struct rvu *rvu = nix_hw->rvu;

	if (!is_rvu_otx2(rvu)) {
		print_nix_cn10k_sq_ctx(m, (struct nix_cn10k_sq_ctx_s *)sq_ctx);
		return;
	}
	seq_printf(m, "W0: sqe_way_mask \t\t%d\nW0: cq \t\t\t\t%d\n",
		   sq_ctx->sqe_way_mask, sq_ctx->cq);
	seq_printf(m, "W0: sdp_mcast \t\t\t%d\nW0: substream \t\t\t0x%03x\n",
		   sq_ctx->sdp_mcast, sq_ctx->substream);
	seq_printf(m, "W0: qint_idx \t\t\t%d\nW0: ena \t\t\t%d\n\n",
		   sq_ctx->qint_idx, sq_ctx->ena);

	seq_printf(m, "W1: sqb_count \t\t\t%d\nW1: default_chan \t\t%d\n",
		   sq_ctx->sqb_count, sq_ctx->default_chan);
	seq_printf(m, "W1: smq_rr_quantum \t\t%d\nW1: sso_ena \t\t\t%d\n",
		   sq_ctx->smq_rr_quantum, sq_ctx->sso_ena);
	seq_printf(m, "W1: xoff \t\t\t%d\nW1: cq_ena \t\t\t%d\nW1: smq\t\t\t\t%d\n\n",
		   sq_ctx->xoff, sq_ctx->cq_ena, sq_ctx->smq);

	seq_printf(m, "W2: sqe_stype \t\t\t%d\nW2: sq_int_ena \t\t\t%d\n",
		   sq_ctx->sqe_stype, sq_ctx->sq_int_ena);
	seq_printf(m, "W2: sq_int \t\t\t%d\nW2: sqb_aura \t\t\t%d\n",
		   sq_ctx->sq_int, sq_ctx->sqb_aura);
	seq_printf(m, "W2: smq_rr_count \t\t%d\n\n", sq_ctx->smq_rr_count);

	seq_printf(m, "W3: smq_next_sq_vld\t\t%d\nW3: smq_pend\t\t\t%d\n",
		   sq_ctx->smq_next_sq_vld, sq_ctx->smq_pend);
	seq_printf(m, "W3: smenq_next_sqb_vld \t\t%d\nW3: head_offset\t\t\t%d\n",
		   sq_ctx->smenq_next_sqb_vld, sq_ctx->head_offset);
	seq_printf(m, "W3: smenq_offset\t\t%d\nW3: tail_offset\t\t\t%d\n",
		   sq_ctx->smenq_offset, sq_ctx->tail_offset);
	seq_printf(m, "W3: smq_lso_segnum \t\t%d\nW3: smq_next_sq\t\t\t%d\n",
		   sq_ctx->smq_lso_segnum, sq_ctx->smq_next_sq);
	seq_printf(m, "W3: mnq_dis \t\t\t%d\nW3: lmt_dis \t\t\t%d\n",
		   sq_ctx->mnq_dis, sq_ctx->lmt_dis);
	seq_printf(m, "W3: cq_limit\t\t\t%d\nW3: max_sqe_size\t\t%d\n\n",
		   sq_ctx->cq_limit, sq_ctx->max_sqe_size);

	seq_printf(m, "W4: next_sqb \t\t\t%llx\n\n", sq_ctx->next_sqb);
	seq_printf(m, "W5: tail_sqb \t\t\t%llx\n\n", sq_ctx->tail_sqb);
	seq_printf(m, "W6: smenq_sqb \t\t\t%llx\n\n", sq_ctx->smenq_sqb);
	seq_printf(m, "W7: smenq_next_sqb \t\t%llx\n\n",
		   sq_ctx->smenq_next_sqb);

	seq_printf(m, "W8: head_sqb\t\t\t%llx\n\n", sq_ctx->head_sqb);

	seq_printf(m, "W9: vfi_lso_vld\t\t\t%d\nW9: vfi_lso_vlan1_ins_ena\t%d\n",
		   sq_ctx->vfi_lso_vld, sq_ctx->vfi_lso_vlan1_ins_ena);
	seq_printf(m, "W9: vfi_lso_vlan0_ins_ena\t%d\nW9: vfi_lso_mps\t\t\t%d\n",
		   sq_ctx->vfi_lso_vlan0_ins_ena, sq_ctx->vfi_lso_mps);
	seq_printf(m, "W9: vfi_lso_sb\t\t\t%d\nW9: vfi_lso_sizem1\t\t%d\n",
		   sq_ctx->vfi_lso_sb, sq_ctx->vfi_lso_sizem1);
	seq_printf(m, "W9: vfi_lso_total\t\t%d\n\n", sq_ctx->vfi_lso_total);

	seq_printf(m, "W10: scm_lso_rem \t\t%llu\n\n",
		   (u64)sq_ctx->scm_lso_rem);
	seq_printf(m, "W11: octs \t\t\t%llu\n\n", (u64)sq_ctx->octs);
	seq_printf(m, "W12: pkts \t\t\t%llu\n\n", (u64)sq_ctx->pkts);
	seq_printf(m, "W14: dropped_octs \t\t%llu\n\n",
		   (u64)sq_ctx->dropped_octs);
	seq_printf(m, "W15: dropped_pkts \t\t%llu\n\n",
		   (u64)sq_ctx->dropped_pkts);
}

static void print_nix_cn10k_rq_ctx(struct seq_file *m,
				   struct nix_cn10k_rq_ctx_s *rq_ctx)
{
	seq_printf(m, "W0: ena \t\t\t%d\nW0: sso_ena \t\t\t%d\n",
		   rq_ctx->ena, rq_ctx->sso_ena);
	seq_printf(m, "W0: ipsech_ena \t\t\t%d\nW0: ena_wqwd \t\t\t%d\n",
		   rq_ctx->ipsech_ena, rq_ctx->ena_wqwd);
	seq_printf(m, "W0: cq \t\t\t\t%d\nW0: lenerr_dis \t\t\t%d\n",
		   rq_ctx->cq, rq_ctx->lenerr_dis);
	seq_printf(m, "W0: csum_il4_dis \t\t%d\nW0: csum_ol4_dis \t\t%d\n",
		   rq_ctx->csum_il4_dis, rq_ctx->csum_ol4_dis);
	seq_printf(m, "W0: len_il4_dis \t\t%d\nW0: len_il3_dis \t\t%d\n",
		   rq_ctx->len_il4_dis, rq_ctx->len_il3_dis);
	seq_printf(m, "W0: len_ol4_dis \t\t%d\nW0: len_ol3_dis \t\t%d\n",
		   rq_ctx->len_ol4_dis, rq_ctx->len_ol3_dis);
	seq_printf(m, "W0: wqe_aura \t\t\t%d\n\n", rq_ctx->wqe_aura);

	seq_printf(m, "W1: spb_aura \t\t\t%d\nW1: lpb_aura \t\t\t%d\n",
		   rq_ctx->spb_aura, rq_ctx->lpb_aura);
	seq_printf(m, "W1: spb_aura \t\t\t%d\n", rq_ctx->spb_aura);
	seq_printf(m, "W1: sso_grp \t\t\t%d\nW1: sso_tt \t\t\t%d\n",
		   rq_ctx->sso_grp, rq_ctx->sso_tt);
	seq_printf(m, "W1: pb_caching \t\t\t%d\nW1: wqe_caching \t\t%d\n",
		   rq_ctx->pb_caching, rq_ctx->wqe_caching);
	seq_printf(m, "W1: xqe_drop_ena \t\t%d\nW1: spb_drop_ena \t\t%d\n",
		   rq_ctx->xqe_drop_ena, rq_ctx->spb_drop_ena);
	seq_printf(m, "W1: lpb_drop_ena \t\t%d\nW1: pb_stashing \t\t%d\n",
		   rq_ctx->lpb_drop_ena, rq_ctx->pb_stashing);
	seq_printf(m, "W1: ipsecd_drop_ena \t\t%d\nW1: chi_ena \t\t\t%d\n\n",
		   rq_ctx->ipsecd_drop_ena, rq_ctx->chi_ena);

	seq_printf(m, "W2: band_prof_id \t\t%d\n", rq_ctx->band_prof_id);
	seq_printf(m, "W2: policer_ena \t\t%d\n", rq_ctx->policer_ena);
	seq_printf(m, "W2: spb_sizem1 \t\t\t%d\n", rq_ctx->spb_sizem1);
	seq_printf(m, "W2: wqe_skip \t\t\t%d\nW2: sqb_ena \t\t\t%d\n",
		   rq_ctx->wqe_skip, rq_ctx->spb_ena);
	seq_printf(m, "W2: lpb_size1 \t\t\t%d\nW2: first_skip \t\t\t%d\n",
		   rq_ctx->lpb_sizem1, rq_ctx->first_skip);
	seq_printf(m, "W2: later_skip\t\t\t%d\nW2: xqe_imm_size\t\t%d\n",
		   rq_ctx->later_skip, rq_ctx->xqe_imm_size);
	seq_printf(m, "W2: xqe_imm_copy \t\t%d\nW2: xqe_hdr_split \t\t%d\n\n",
		   rq_ctx->xqe_imm_copy, rq_ctx->xqe_hdr_split);

	seq_printf(m, "W3: xqe_drop \t\t\t%d\nW3: xqe_pass \t\t\t%d\n",
		   rq_ctx->xqe_drop, rq_ctx->xqe_pass);
	seq_printf(m, "W3: wqe_pool_drop \t\t%d\nW3: wqe_pool_pass \t\t%d\n",
		   rq_ctx->wqe_pool_drop, rq_ctx->wqe_pool_pass);
	seq_printf(m, "W3: spb_pool_drop \t\t%d\nW3: spb_pool_pass \t\t%d\n",
		   rq_ctx->spb_pool_drop, rq_ctx->spb_pool_pass);
	seq_printf(m, "W3: spb_aura_drop \t\t%d\nW3: spb_aura_pass \t\t%d\n\n",
		   rq_ctx->spb_aura_pass, rq_ctx->spb_aura_drop);

	seq_printf(m, "W4: lpb_aura_drop \t\t%d\nW3: lpb_aura_pass \t\t%d\n",
		   rq_ctx->lpb_aura_pass, rq_ctx->lpb_aura_drop);
	seq_printf(m, "W4: lpb_pool_drop \t\t%d\nW3: lpb_pool_pass \t\t%d\n",
		   rq_ctx->lpb_pool_drop, rq_ctx->lpb_pool_pass);
	seq_printf(m, "W4: rq_int \t\t\t%d\nW4: rq_int_ena\t\t\t%d\n",
		   rq_ctx->rq_int, rq_ctx->rq_int_ena);
	seq_printf(m, "W4: qint_idx \t\t\t%d\n\n", rq_ctx->qint_idx);

	seq_printf(m, "W5: ltag \t\t\t%d\nW5: good_utag \t\t\t%d\n",
		   rq_ctx->ltag, rq_ctx->good_utag);
	seq_printf(m, "W5: bad_utag \t\t\t%d\nW5: flow_tagw \t\t\t%d\n",
		   rq_ctx->bad_utag, rq_ctx->flow_tagw);
	seq_printf(m, "W5: ipsec_vwqe \t\t\t%d\nW5: vwqe_ena \t\t\t%d\n",
		   rq_ctx->ipsec_vwqe, rq_ctx->vwqe_ena);
	seq_printf(m, "W5: vwqe_wait \t\t\t%d\nW5: max_vsize_exp\t\t%d\n",
		   rq_ctx->vwqe_wait, rq_ctx->max_vsize_exp);
	seq_printf(m, "W5: vwqe_skip \t\t\t%d\n\n", rq_ctx->vwqe_skip);

	seq_printf(m, "W6: octs \t\t\t%llu\n\n", (u64)rq_ctx->octs);
	seq_printf(m, "W7: pkts \t\t\t%llu\n\n", (u64)rq_ctx->pkts);
	seq_printf(m, "W8: drop_octs \t\t\t%llu\n\n", (u64)rq_ctx->drop_octs);
	seq_printf(m, "W9: drop_pkts \t\t\t%llu\n\n", (u64)rq_ctx->drop_pkts);
	seq_printf(m, "W10: re_pkts \t\t\t%llu\n", (u64)rq_ctx->re_pkts);
}

/* Dumps given nix_rq's context */
static void print_nix_rq_ctx(struct seq_file *m, struct nix_aq_enq_rsp *rsp)
{
	struct nix_rq_ctx_s *rq_ctx = &rsp->rq;
	struct nix_hw *nix_hw = m->private;
	struct rvu *rvu = nix_hw->rvu;

	if (!is_rvu_otx2(rvu)) {
		print_nix_cn10k_rq_ctx(m, (struct nix_cn10k_rq_ctx_s *)rq_ctx);
		return;
	}

	seq_printf(m, "W0: wqe_aura \t\t\t%d\nW0: substream \t\t\t0x%03x\n",
		   rq_ctx->wqe_aura, rq_ctx->substream);
	seq_printf(m, "W0: cq \t\t\t\t%d\nW0: ena_wqwd \t\t\t%d\n",
		   rq_ctx->cq, rq_ctx->ena_wqwd);
	seq_printf(m, "W0: ipsech_ena \t\t\t%d\nW0: sso_ena \t\t\t%d\n",
		   rq_ctx->ipsech_ena, rq_ctx->sso_ena);
	seq_printf(m, "W0: ena \t\t\t%d\n\n", rq_ctx->ena);

	seq_printf(m, "W1: lpb_drop_ena \t\t%d\nW1: spb_drop_ena \t\t%d\n",
		   rq_ctx->lpb_drop_ena, rq_ctx->spb_drop_ena);
	seq_printf(m, "W1: xqe_drop_ena \t\t%d\nW1: wqe_caching \t\t%d\n",
		   rq_ctx->xqe_drop_ena, rq_ctx->wqe_caching);
	seq_printf(m, "W1: pb_caching \t\t\t%d\nW1: sso_tt \t\t\t%d\n",
		   rq_ctx->pb_caching, rq_ctx->sso_tt);
	seq_printf(m, "W1: sso_grp \t\t\t%d\nW1: lpb_aura \t\t\t%d\n",
		   rq_ctx->sso_grp, rq_ctx->lpb_aura);
	seq_printf(m, "W1: spb_aura \t\t\t%d\n\n", rq_ctx->spb_aura);

	seq_printf(m, "W2: xqe_hdr_split \t\t%d\nW2: xqe_imm_copy \t\t%d\n",
		   rq_ctx->xqe_hdr_split, rq_ctx->xqe_imm_copy);
	seq_printf(m, "W2: xqe_imm_size \t\t%d\nW2: later_skip \t\t\t%d\n",
		   rq_ctx->xqe_imm_size, rq_ctx->later_skip);
	seq_printf(m, "W2: first_skip \t\t\t%d\nW2: lpb_sizem1 \t\t\t%d\n",
		   rq_ctx->first_skip, rq_ctx->lpb_sizem1);
	seq_printf(m, "W2: spb_ena \t\t\t%d\nW2: wqe_skip \t\t\t%d\n",
		   rq_ctx->spb_ena, rq_ctx->wqe_skip);
	seq_printf(m, "W2: spb_sizem1 \t\t\t%d\n\n", rq_ctx->spb_sizem1);

	seq_printf(m, "W3: spb_pool_pass \t\t%d\nW3: spb_pool_drop \t\t%d\n",
		   rq_ctx->spb_pool_pass, rq_ctx->spb_pool_drop);
	seq_printf(m, "W3: spb_aura_pass \t\t%d\nW3: spb_aura_drop \t\t%d\n",
		   rq_ctx->spb_aura_pass, rq_ctx->spb_aura_drop);
	seq_printf(m, "W3: wqe_pool_pass \t\t%d\nW3: wqe_pool_drop \t\t%d\n",
		   rq_ctx->wqe_pool_pass, rq_ctx->wqe_pool_drop);
	seq_printf(m, "W3: xqe_pass \t\t\t%d\nW3: xqe_drop \t\t\t%d\n\n",
		   rq_ctx->xqe_pass, rq_ctx->xqe_drop);

	seq_printf(m, "W4: qint_idx \t\t\t%d\nW4: rq_int_ena \t\t\t%d\n",
		   rq_ctx->qint_idx, rq_ctx->rq_int_ena);
	seq_printf(m, "W4: rq_int \t\t\t%d\nW4: lpb_pool_pass \t\t%d\n",
		   rq_ctx->rq_int, rq_ctx->lpb_pool_pass);
	seq_printf(m, "W4: lpb_pool_drop \t\t%d\nW4: lpb_aura_pass \t\t%d\n",
		   rq_ctx->lpb_pool_drop, rq_ctx->lpb_aura_pass);
	seq_printf(m, "W4: lpb_aura_drop \t\t%d\n\n", rq_ctx->lpb_aura_drop);

	seq_printf(m, "W5: flow_tagw \t\t\t%d\nW5: bad_utag \t\t\t%d\n",
		   rq_ctx->flow_tagw, rq_ctx->bad_utag);
	seq_printf(m, "W5: good_utag \t\t\t%d\nW5: ltag \t\t\t%d\n\n",
		   rq_ctx->good_utag, rq_ctx->ltag);

	seq_printf(m, "W6: octs \t\t\t%llu\n\n", (u64)rq_ctx->octs);
	seq_printf(m, "W7: pkts \t\t\t%llu\n\n", (u64)rq_ctx->pkts);
	seq_printf(m, "W8: drop_octs \t\t\t%llu\n\n", (u64)rq_ctx->drop_octs);
	seq_printf(m, "W9: drop_pkts \t\t\t%llu\n\n", (u64)rq_ctx->drop_pkts);
	seq_printf(m, "W10: re_pkts \t\t\t%llu\n", (u64)rq_ctx->re_pkts);
}

/* Dumps given nix_cq's context */
static void print_nix_cq_ctx(struct seq_file *m, struct nix_aq_enq_rsp *rsp)
{
	struct nix_cq_ctx_s *cq_ctx = &rsp->cq;

	seq_printf(m, "W0: base \t\t\t%llx\n\n", cq_ctx->base);

	seq_printf(m, "W1: wrptr \t\t\t%llx\n", (u64)cq_ctx->wrptr);
	seq_printf(m, "W1: avg_con \t\t\t%d\nW1: cint_idx \t\t\t%d\n",
		   cq_ctx->avg_con, cq_ctx->cint_idx);
	seq_printf(m, "W1: cq_err \t\t\t%d\nW1: qint_idx \t\t\t%d\n",
		   cq_ctx->cq_err, cq_ctx->qint_idx);
	seq_printf(m, "W1: bpid \t\t\t%d\nW1: bp_ena \t\t\t%d\n\n",
		   cq_ctx->bpid, cq_ctx->bp_ena);

	seq_printf(m, "W2: update_time \t\t%d\nW2:avg_level \t\t\t%d\n",
		   cq_ctx->update_time, cq_ctx->avg_level);
	seq_printf(m, "W2: head \t\t\t%d\nW2:tail \t\t\t%d\n\n",
		   cq_ctx->head, cq_ctx->tail);

	seq_printf(m, "W3: cq_err_int_ena \t\t%d\nW3:cq_err_int \t\t\t%d\n",
		   cq_ctx->cq_err_int_ena, cq_ctx->cq_err_int);
	seq_printf(m, "W3: qsize \t\t\t%d\nW3:caching \t\t\t%d\n",
		   cq_ctx->qsize, cq_ctx->caching);
	seq_printf(m, "W3: substream \t\t\t0x%03x\nW3: ena \t\t\t%d\n",
		   cq_ctx->substream, cq_ctx->ena);
	seq_printf(m, "W3: drop_ena \t\t\t%d\nW3: drop \t\t\t%d\n",
		   cq_ctx->drop_ena, cq_ctx->drop);
	seq_printf(m, "W3: bp \t\t\t\t%d\n\n", cq_ctx->bp);
}

static int rvu_dbg_nix_queue_ctx_display(struct seq_file *filp,
					 void *unused, int ctype)
{
	void (*print_nix_ctx)(struct seq_file *filp,
			      struct nix_aq_enq_rsp *rsp) = NULL;
	struct nix_hw *nix_hw = filp->private;
	struct rvu *rvu = nix_hw->rvu;
	struct nix_aq_enq_req aq_req;
	struct nix_aq_enq_rsp rsp;
	char *ctype_string = NULL;
	int qidx, rc, max_id = 0;
	struct rvu_pfvf *pfvf;
	int nixlf, id, all;
	u16 pcifunc;

	switch (ctype) {
	case NIX_AQ_CTYPE_CQ:
		nixlf = rvu->rvu_dbg.nix_cq_ctx.lf;
		id = rvu->rvu_dbg.nix_cq_ctx.id;
		all = rvu->rvu_dbg.nix_cq_ctx.all;
		break;

	case NIX_AQ_CTYPE_SQ:
		nixlf = rvu->rvu_dbg.nix_sq_ctx.lf;
		id = rvu->rvu_dbg.nix_sq_ctx.id;
		all = rvu->rvu_dbg.nix_sq_ctx.all;
		break;

	case NIX_AQ_CTYPE_RQ:
		nixlf = rvu->rvu_dbg.nix_rq_ctx.lf;
		id = rvu->rvu_dbg.nix_rq_ctx.id;
		all = rvu->rvu_dbg.nix_rq_ctx.all;
		break;

	default:
		return -EINVAL;
	}

	if (!rvu_dbg_is_valid_lf(rvu, nix_hw->blkaddr, nixlf, &pcifunc))
		return -EINVAL;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	if (ctype == NIX_AQ_CTYPE_SQ && !pfvf->sq_ctx) {
		seq_puts(filp, "SQ context is not initialized\n");
		return -EINVAL;
	} else if (ctype == NIX_AQ_CTYPE_RQ && !pfvf->rq_ctx) {
		seq_puts(filp, "RQ context is not initialized\n");
		return -EINVAL;
	} else if (ctype == NIX_AQ_CTYPE_CQ && !pfvf->cq_ctx) {
		seq_puts(filp, "CQ context is not initialized\n");
		return -EINVAL;
	}

	if (ctype == NIX_AQ_CTYPE_SQ) {
		max_id = pfvf->sq_ctx->qsize;
		ctype_string = "sq";
		print_nix_ctx = print_nix_sq_ctx;
	} else if (ctype == NIX_AQ_CTYPE_RQ) {
		max_id = pfvf->rq_ctx->qsize;
		ctype_string = "rq";
		print_nix_ctx = print_nix_rq_ctx;
	} else if (ctype == NIX_AQ_CTYPE_CQ) {
		max_id = pfvf->cq_ctx->qsize;
		ctype_string = "cq";
		print_nix_ctx = print_nix_cq_ctx;
	}

	memset(&aq_req, 0, sizeof(struct nix_aq_enq_req));
	aq_req.hdr.pcifunc = pcifunc;
	aq_req.ctype = ctype;
	aq_req.op = NIX_AQ_INSTOP_READ;
	if (all)
		id = 0;
	else
		max_id = id + 1;
	for (qidx = id; qidx < max_id; qidx++) {
		aq_req.qidx = qidx;
		seq_printf(filp, "=====%s_ctx for nixlf:%d and qidx:%d is=====\n",
			   ctype_string, nixlf, aq_req.qidx);
		rc = rvu_mbox_handler_nix_aq_enq(rvu, &aq_req, &rsp);
		if (rc) {
			seq_puts(filp, "Failed to read the context\n");
			return -EINVAL;
		}
		print_nix_ctx(filp, &rsp);
	}
	return 0;
}

static int write_nix_queue_ctx(struct rvu *rvu, bool all, int nixlf,
			       int id, int ctype, char *ctype_string,
			       struct seq_file *m)
{
	struct nix_hw *nix_hw = m->private;
	struct rvu_pfvf *pfvf;
	int max_id = 0;
	u16 pcifunc;

	if (!rvu_dbg_is_valid_lf(rvu, nix_hw->blkaddr, nixlf, &pcifunc))
		return -EINVAL;

	pfvf = rvu_get_pfvf(rvu, pcifunc);

	if (ctype == NIX_AQ_CTYPE_SQ) {
		if (!pfvf->sq_ctx) {
			dev_warn(rvu->dev, "SQ context is not initialized\n");
			return -EINVAL;
		}
		max_id = pfvf->sq_ctx->qsize;
	} else if (ctype == NIX_AQ_CTYPE_RQ) {
		if (!pfvf->rq_ctx) {
			dev_warn(rvu->dev, "RQ context is not initialized\n");
			return -EINVAL;
		}
		max_id = pfvf->rq_ctx->qsize;
	} else if (ctype == NIX_AQ_CTYPE_CQ) {
		if (!pfvf->cq_ctx) {
			dev_warn(rvu->dev, "CQ context is not initialized\n");
			return -EINVAL;
		}
		max_id = pfvf->cq_ctx->qsize;
	}

	if (id < 0 || id >= max_id) {
		dev_warn(rvu->dev, "Invalid %s_ctx valid range 0-%d\n",
			 ctype_string, max_id - 1);
		return -EINVAL;
	}
	switch (ctype) {
	case NIX_AQ_CTYPE_CQ:
		rvu->rvu_dbg.nix_cq_ctx.lf = nixlf;
		rvu->rvu_dbg.nix_cq_ctx.id = id;
		rvu->rvu_dbg.nix_cq_ctx.all = all;
		break;

	case NIX_AQ_CTYPE_SQ:
		rvu->rvu_dbg.nix_sq_ctx.lf = nixlf;
		rvu->rvu_dbg.nix_sq_ctx.id = id;
		rvu->rvu_dbg.nix_sq_ctx.all = all;
		break;

	case NIX_AQ_CTYPE_RQ:
		rvu->rvu_dbg.nix_rq_ctx.lf = nixlf;
		rvu->rvu_dbg.nix_rq_ctx.id = id;
		rvu->rvu_dbg.nix_rq_ctx.all = all;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static ssize_t rvu_dbg_nix_queue_ctx_write(struct file *filp,
					   const char __user *buffer,
					   size_t count, loff_t *ppos,
					   int ctype)
{
	struct seq_file *m = filp->private_data;
	struct nix_hw *nix_hw = m->private;
	struct rvu *rvu = nix_hw->rvu;
	char *cmd_buf, *ctype_string;
	int nixlf, id = 0, ret;
	bool all = false;

	if ((*ppos != 0) || !count)
		return -EINVAL;

	switch (ctype) {
	case NIX_AQ_CTYPE_SQ:
		ctype_string = "sq";
		break;
	case NIX_AQ_CTYPE_RQ:
		ctype_string = "rq";
		break;
	case NIX_AQ_CTYPE_CQ:
		ctype_string = "cq";
		break;
	default:
		return -EINVAL;
	}

	cmd_buf = kzalloc(count + 1, GFP_KERNEL);

	if (!cmd_buf)
		return count;

	ret = parse_cmd_buffer_ctx(cmd_buf, &count, buffer,
				   &nixlf, &id, &all);
	if (ret < 0) {
		dev_info(rvu->dev,
			 "Usage: echo <nixlf> [%s number/all] > %s_ctx\n",
			 ctype_string, ctype_string);
		goto done;
	} else {
		ret = write_nix_queue_ctx(rvu, all, nixlf, id, ctype,
					  ctype_string, m);
	}
done:
	kfree(cmd_buf);
	return ret ? ret : count;
}

static ssize_t rvu_dbg_nix_sq_ctx_write(struct file *filp,
					const char __user *buffer,
					size_t count, loff_t *ppos)
{
	return rvu_dbg_nix_queue_ctx_write(filp, buffer, count, ppos,
					    NIX_AQ_CTYPE_SQ);
}

static int rvu_dbg_nix_sq_ctx_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_nix_queue_ctx_display(filp, unused, NIX_AQ_CTYPE_SQ);
}

RVU_DEBUG_SEQ_FOPS(nix_sq_ctx, nix_sq_ctx_display, nix_sq_ctx_write);

static ssize_t rvu_dbg_nix_rq_ctx_write(struct file *filp,
					const char __user *buffer,
					size_t count, loff_t *ppos)
{
	return rvu_dbg_nix_queue_ctx_write(filp, buffer, count, ppos,
					    NIX_AQ_CTYPE_RQ);
}

static int rvu_dbg_nix_rq_ctx_display(struct seq_file *filp, void  *unused)
{
	return rvu_dbg_nix_queue_ctx_display(filp, unused,  NIX_AQ_CTYPE_RQ);
}

RVU_DEBUG_SEQ_FOPS(nix_rq_ctx, nix_rq_ctx_display, nix_rq_ctx_write);

static ssize_t rvu_dbg_nix_cq_ctx_write(struct file *filp,
					const char __user *buffer,
					size_t count, loff_t *ppos)
{
	return rvu_dbg_nix_queue_ctx_write(filp, buffer, count, ppos,
					    NIX_AQ_CTYPE_CQ);
}

static int rvu_dbg_nix_cq_ctx_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_nix_queue_ctx_display(filp, unused, NIX_AQ_CTYPE_CQ);
}

RVU_DEBUG_SEQ_FOPS(nix_cq_ctx, nix_cq_ctx_display, nix_cq_ctx_write);

static void print_nix_qctx_qsize(struct seq_file *filp, int qsize,
				 unsigned long *bmap, char *qtype)
{
	char *buf;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return;

	bitmap_print_to_pagebuf(false, buf, bmap, qsize);
	seq_printf(filp, "%s context count : %d\n", qtype, qsize);
	seq_printf(filp, "%s context ena/dis bitmap : %s\n",
		   qtype, buf);
	kfree(buf);
}

static void print_nix_qsize(struct seq_file *filp, struct rvu_pfvf *pfvf)
{
	if (!pfvf->cq_ctx)
		seq_puts(filp, "cq context is not initialized\n");
	else
		print_nix_qctx_qsize(filp, pfvf->cq_ctx->qsize, pfvf->cq_bmap,
				     "cq");

	if (!pfvf->rq_ctx)
		seq_puts(filp, "rq context is not initialized\n");
	else
		print_nix_qctx_qsize(filp, pfvf->rq_ctx->qsize, pfvf->rq_bmap,
				     "rq");

	if (!pfvf->sq_ctx)
		seq_puts(filp, "sq context is not initialized\n");
	else
		print_nix_qctx_qsize(filp, pfvf->sq_ctx->qsize, pfvf->sq_bmap,
				     "sq");
}

static ssize_t rvu_dbg_nix_qsize_write(struct file *filp,
				       const char __user *buffer,
				       size_t count, loff_t *ppos)
{
	return rvu_dbg_qsize_write(filp, buffer, count, ppos,
				   BLKTYPE_NIX);
}

static int rvu_dbg_nix_qsize_display(struct seq_file *filp, void *unused)
{
	return rvu_dbg_qsize_display(filp, unused, BLKTYPE_NIX);
}

RVU_DEBUG_SEQ_FOPS(nix_qsize, nix_qsize_display, nix_qsize_write);

static void print_band_prof_ctx(struct seq_file *m,
				struct nix_bandprof_s *prof)
{
	char *str;

	switch (prof->pc_mode) {
	case NIX_RX_PC_MODE_VLAN:
		str = "VLAN";
		break;
	case NIX_RX_PC_MODE_DSCP:
		str = "DSCP";
		break;
	case NIX_RX_PC_MODE_GEN:
		str = "Generic";
		break;
	case NIX_RX_PC_MODE_RSVD:
		str = "Reserved";
		break;
	}
	seq_printf(m, "W0: pc_mode\t\t%s\n", str);
	str = (prof->icolor == 3) ? "Color blind" :
		(prof->icolor == 0) ? "Green" :
		(prof->icolor == 1) ? "Yellow" : "Red";
	seq_printf(m, "W0: icolor\t\t%s\n", str);
	seq_printf(m, "W0: tnl_ena\t\t%d\n", prof->tnl_ena);
	seq_printf(m, "W0: peir_exponent\t%d\n", prof->peir_exponent);
	seq_printf(m, "W0: pebs_exponent\t%d\n", prof->pebs_exponent);
	seq_printf(m, "W0: cir_exponent\t%d\n", prof->cir_exponent);
	seq_printf(m, "W0: cbs_exponent\t%d\n", prof->cbs_exponent);
	seq_printf(m, "W0: peir_mantissa\t%d\n", prof->peir_mantissa);
	seq_printf(m, "W0: pebs_mantissa\t%d\n", prof->pebs_mantissa);
	seq_printf(m, "W0: cir_mantissa\t%d\n", prof->cir_mantissa);

	seq_printf(m, "W1: cbs_mantissa\t%d\n", prof->cbs_mantissa);
	str = (prof->lmode == 0) ? "byte" : "packet";
	seq_printf(m, "W1: lmode\t\t%s\n", str);
	seq_printf(m, "W1: l_select\t\t%d\n", prof->l_sellect);
	seq_printf(m, "W1: rdiv\t\t%d\n", prof->rdiv);
	seq_printf(m, "W1: adjust_exponent\t%d\n", prof->adjust_exponent);
	seq_printf(m, "W1: adjust_mantissa\t%d\n", prof->adjust_mantissa);
	str = (prof->gc_action == 0) ? "PASS" :
		(prof->gc_action == 1) ? "DROP" : "RED";
	seq_printf(m, "W1: gc_action\t\t%s\n", str);
	str = (prof->yc_action == 0) ? "PASS" :
		(prof->yc_action == 1) ? "DROP" : "RED";
	seq_printf(m, "W1: yc_action\t\t%s\n", str);
	str = (prof->rc_action == 0) ? "PASS" :
		(prof->rc_action == 1) ? "DROP" : "RED";
	seq_printf(m, "W1: rc_action\t\t%s\n", str);
	seq_printf(m, "W1: meter_algo\t\t%d\n", prof->meter_algo);
	seq_printf(m, "W1: band_prof_id\t%d\n", prof->band_prof_id);
	seq_printf(m, "W1: hl_en\t\t%d\n", prof->hl_en);

	seq_printf(m, "W2: ts\t\t\t%lld\n", (u64)prof->ts);
	seq_printf(m, "W3: pe_accum\t\t%d\n", prof->pe_accum);
	seq_printf(m, "W3: c_accum\t\t%d\n", prof->c_accum);
	seq_printf(m, "W4: green_pkt_pass\t%lld\n",
		   (u64)prof->green_pkt_pass);
	seq_printf(m, "W5: yellow_pkt_pass\t%lld\n",
		   (u64)prof->yellow_pkt_pass);
	seq_printf(m, "W6: red_pkt_pass\t%lld\n", (u64)prof->red_pkt_pass);
	seq_printf(m, "W7: green_octs_pass\t%lld\n",
		   (u64)prof->green_octs_pass);
	seq_printf(m, "W8: yellow_octs_pass\t%lld\n",
		   (u64)prof->yellow_octs_pass);
	seq_printf(m, "W9: red_octs_pass\t%lld\n", (u64)prof->red_octs_pass);
	seq_printf(m, "W10: green_pkt_drop\t%lld\n",
		   (u64)prof->green_pkt_drop);
	seq_printf(m, "W11: yellow_pkt_drop\t%lld\n",
		   (u64)prof->yellow_pkt_drop);
	seq_printf(m, "W12: red_pkt_drop\t%lld\n", (u64)prof->red_pkt_drop);
	seq_printf(m, "W13: green_octs_drop\t%lld\n",
		   (u64)prof->green_octs_drop);
	seq_printf(m, "W14: yellow_octs_drop\t%lld\n",
		   (u64)prof->yellow_octs_drop);
	seq_printf(m, "W15: red_octs_drop\t%lld\n", (u64)prof->red_octs_drop);
	seq_puts(m, "==============================\n");
}

static int rvu_dbg_nix_band_prof_ctx_display(struct seq_file *m, void *unused)
{
	struct nix_hw *nix_hw = m->private;
	struct nix_cn10k_aq_enq_req aq_req;
	struct nix_cn10k_aq_enq_rsp aq_rsp;
	struct rvu *rvu = nix_hw->rvu;
	struct nix_ipolicer *ipolicer;
	int layer, prof_idx, idx, rc;
	u16 pcifunc;
	char *str;

	/* Ingress policers do not exist on all platforms */
	if (!nix_hw->ipolicer)
		return 0;

	for (layer = 0; layer < BAND_PROF_NUM_LAYERS; layer++) {
		if (layer == BAND_PROF_INVAL_LAYER)
			continue;
		str = (layer == BAND_PROF_LEAF_LAYER) ? "Leaf" :
			(layer == BAND_PROF_MID_LAYER) ? "Mid" : "Top";

		seq_printf(m, "\n%s bandwidth profiles\n", str);
		seq_puts(m, "=======================\n");

		ipolicer = &nix_hw->ipolicer[layer];

		for (idx = 0; idx < ipolicer->band_prof.max; idx++) {
			if (is_rsrc_free(&ipolicer->band_prof, idx))
				continue;

			prof_idx = (idx & 0x3FFF) | (layer << 14);
			rc = nix_aq_context_read(rvu, nix_hw, &aq_req, &aq_rsp,
						 0x00, NIX_AQ_CTYPE_BANDPROF,
						 prof_idx);
			if (rc) {
				dev_err(rvu->dev,
					"%s: Failed to fetch context of %s profile %d, err %d\n",
					__func__, str, idx, rc);
				return 0;
			}
			seq_printf(m, "\n%s bandwidth profile:: %d\n", str, idx);
			pcifunc = ipolicer->pfvf_map[idx];
			if (!(pcifunc & RVU_PFVF_FUNC_MASK))
				seq_printf(m, "Allocated to :: PF %d\n",
					   rvu_get_pf(pcifunc));
			else
				seq_printf(m, "Allocated to :: PF %d VF %d\n",
					   rvu_get_pf(pcifunc),
					   (pcifunc & RVU_PFVF_FUNC_MASK) - 1);
			print_band_prof_ctx(m, &aq_rsp.prof);
		}
	}
	return 0;
}

RVU_DEBUG_SEQ_FOPS(nix_band_prof_ctx, nix_band_prof_ctx_display, NULL);

static int rvu_dbg_nix_band_prof_rsrc_display(struct seq_file *m, void *unused)
{
	struct nix_hw *nix_hw = m->private;
	struct nix_ipolicer *ipolicer;
	int layer;
	char *str;

	/* Ingress policers do not exist on all platforms */
	if (!nix_hw->ipolicer)
		return 0;

	seq_puts(m, "\nBandwidth profile resource free count\n");
	seq_puts(m, "=====================================\n");
	for (layer = 0; layer < BAND_PROF_NUM_LAYERS; layer++) {
		if (layer == BAND_PROF_INVAL_LAYER)
			continue;
		str = (layer == BAND_PROF_LEAF_LAYER) ? "Leaf" :
			(layer == BAND_PROF_MID_LAYER) ? "Mid " : "Top ";

		ipolicer = &nix_hw->ipolicer[layer];
		seq_printf(m, "%s :: Max: %4d  Free: %4d\n", str,
			   ipolicer->band_prof.max,
			   rvu_rsrc_free_count(&ipolicer->band_prof));
	}
	seq_puts(m, "=====================================\n");

	return 0;
}

RVU_DEBUG_SEQ_FOPS(nix_band_prof_rsrc, nix_band_prof_rsrc_display, NULL);

static void rvu_dbg_nix_init(struct rvu *rvu, int blkaddr)
{
	struct nix_hw *nix_hw;

	if (!is_block_implemented(rvu->hw, blkaddr))
		return;

	if (blkaddr == BLKADDR_NIX0) {
		rvu->rvu_dbg.nix = debugfs_create_dir("nix", rvu->rvu_dbg.root);
		nix_hw = &rvu->hw->nix[0];
	} else {
		rvu->rvu_dbg.nix = debugfs_create_dir("nix1",
						      rvu->rvu_dbg.root);
		nix_hw = &rvu->hw->nix[1];
	}

	debugfs_create_file("sq_ctx", 0600, rvu->rvu_dbg.nix, nix_hw,
			    &rvu_dbg_nix_sq_ctx_fops);
	debugfs_create_file("rq_ctx", 0600, rvu->rvu_dbg.nix, nix_hw,
			    &rvu_dbg_nix_rq_ctx_fops);
	debugfs_create_file("cq_ctx", 0600, rvu->rvu_dbg.nix, nix_hw,
			    &rvu_dbg_nix_cq_ctx_fops);
	debugfs_create_file("ndc_tx_cache", 0600, rvu->rvu_dbg.nix, nix_hw,
			    &rvu_dbg_nix_ndc_tx_cache_fops);
	debugfs_create_file("ndc_rx_cache", 0600, rvu->rvu_dbg.nix, nix_hw,
			    &rvu_dbg_nix_ndc_rx_cache_fops);
	debugfs_create_file("ndc_tx_hits_miss", 0600, rvu->rvu_dbg.nix, nix_hw,
			    &rvu_dbg_nix_ndc_tx_hits_miss_fops);
	debugfs_create_file("ndc_rx_hits_miss", 0600, rvu->rvu_dbg.nix, nix_hw,
			    &rvu_dbg_nix_ndc_rx_hits_miss_fops);
	debugfs_create_file("qsize", 0600, rvu->rvu_dbg.nix, rvu,
			    &rvu_dbg_nix_qsize_fops);
	debugfs_create_file("ingress_policer_ctx", 0600, rvu->rvu_dbg.nix, nix_hw,
			    &rvu_dbg_nix_band_prof_ctx_fops);
	debugfs_create_file("ingress_policer_rsrc", 0600, rvu->rvu_dbg.nix, nix_hw,
			    &rvu_dbg_nix_band_prof_rsrc_fops);
}

static void rvu_dbg_npa_init(struct rvu *rvu)
{
	rvu->rvu_dbg.npa = debugfs_create_dir("npa", rvu->rvu_dbg.root);

	debugfs_create_file("qsize", 0600, rvu->rvu_dbg.npa, rvu,
			    &rvu_dbg_npa_qsize_fops);
	debugfs_create_file("aura_ctx", 0600, rvu->rvu_dbg.npa, rvu,
			    &rvu_dbg_npa_aura_ctx_fops);
	debugfs_create_file("pool_ctx", 0600, rvu->rvu_dbg.npa, rvu,
			    &rvu_dbg_npa_pool_ctx_fops);
	debugfs_create_file("ndc_cache", 0600, rvu->rvu_dbg.npa, rvu,
			    &rvu_dbg_npa_ndc_cache_fops);
	debugfs_create_file("ndc_hits_miss", 0600, rvu->rvu_dbg.npa, rvu,
			    &rvu_dbg_npa_ndc_hits_miss_fops);
}

#define PRINT_CGX_CUML_NIXRX_STATUS(idx, name)				\
	({								\
		u64 cnt;						\
		err = rvu_cgx_nix_cuml_stats(rvu, cgxd, lmac_id, (idx),	\
					     NIX_STATS_RX, &(cnt));	\
		if (!err)						\
			seq_printf(s, "%s: %llu\n", name, cnt);		\
		cnt;							\
	})

#define PRINT_CGX_CUML_NIXTX_STATUS(idx, name)			\
	({								\
		u64 cnt;						\
		err = rvu_cgx_nix_cuml_stats(rvu, cgxd, lmac_id, (idx),	\
					  NIX_STATS_TX, &(cnt));	\
		if (!err)						\
			seq_printf(s, "%s: %llu\n", name, cnt);		\
		cnt;							\
	})

static int cgx_print_stats(struct seq_file *s, int lmac_id)
{
	struct cgx_link_user_info linfo;
	struct mac_ops *mac_ops;
	void *cgxd = s->private;
	u64 ucast, mcast, bcast;
	int stat = 0, err = 0;
	u64 tx_stat, rx_stat;
	struct rvu *rvu;

	rvu = pci_get_drvdata(pci_get_device(PCI_VENDOR_ID_CAVIUM,
					     PCI_DEVID_OCTEONTX2_RVU_AF, NULL));
	if (!rvu)
		return -ENODEV;

	mac_ops = get_mac_ops(cgxd);
	/* There can be no CGX devices at all */
	if (!mac_ops)
		return 0;

	/* Link status */
	seq_puts(s, "\n=======Link Status======\n\n");
	err = cgx_get_link_info(cgxd, lmac_id, &linfo);
	if (err)
		seq_puts(s, "Failed to read link status\n");
	seq_printf(s, "\nLink is %s %d Mbps\n\n",
		   linfo.link_up ? "UP" : "DOWN", linfo.speed);

	/* Rx stats */
	seq_printf(s, "\n=======NIX RX_STATS(%s port level)======\n\n",
		   mac_ops->name);
	ucast = PRINT_CGX_CUML_NIXRX_STATUS(RX_UCAST, "rx_ucast_frames");
	if (err)
		return err;
	mcast = PRINT_CGX_CUML_NIXRX_STATUS(RX_MCAST, "rx_mcast_frames");
	if (err)
		return err;
	bcast = PRINT_CGX_CUML_NIXRX_STATUS(RX_BCAST, "rx_bcast_frames");
	if (err)
		return err;
	seq_printf(s, "rx_frames: %llu\n", ucast + mcast + bcast);
	PRINT_CGX_CUML_NIXRX_STATUS(RX_OCTS, "rx_bytes");
	if (err)
		return err;
	PRINT_CGX_CUML_NIXRX_STATUS(RX_DROP, "rx_drops");
	if (err)
		return err;
	PRINT_CGX_CUML_NIXRX_STATUS(RX_ERR, "rx_errors");
	if (err)
		return err;

	/* Tx stats */
	seq_printf(s, "\n=======NIX TX_STATS(%s port level)======\n\n",
		   mac_ops->name);
	ucast = PRINT_CGX_CUML_NIXTX_STATUS(TX_UCAST, "tx_ucast_frames");
	if (err)
		return err;
	mcast = PRINT_CGX_CUML_NIXTX_STATUS(TX_MCAST, "tx_mcast_frames");
	if (err)
		return err;
	bcast = PRINT_CGX_CUML_NIXTX_STATUS(TX_BCAST, "tx_bcast_frames");
	if (err)
		return err;
	seq_printf(s, "tx_frames: %llu\n", ucast + mcast + bcast);
	PRINT_CGX_CUML_NIXTX_STATUS(TX_OCTS, "tx_bytes");
	if (err)
		return err;
	PRINT_CGX_CUML_NIXTX_STATUS(TX_DROP, "tx_drops");
	if (err)
		return err;

	/* Rx stats */
	seq_printf(s, "\n=======%s RX_STATS======\n\n", mac_ops->name);
	while (stat < mac_ops->rx_stats_cnt) {
		err = mac_ops->mac_get_rx_stats(cgxd, lmac_id, stat, &rx_stat);
		if (err)
			return err;
		if (is_rvu_otx2(rvu))
			seq_printf(s, "%s: %llu\n", cgx_rx_stats_fields[stat],
				   rx_stat);
		else
			seq_printf(s, "%s: %llu\n", rpm_rx_stats_fields[stat],
				   rx_stat);
		stat++;
	}

	/* Tx stats */
	stat = 0;
	seq_printf(s, "\n=======%s TX_STATS======\n\n", mac_ops->name);
	while (stat < mac_ops->tx_stats_cnt) {
		err = mac_ops->mac_get_tx_stats(cgxd, lmac_id, stat, &tx_stat);
		if (err)
			return err;

		if (is_rvu_otx2(rvu))
			seq_printf(s, "%s: %llu\n", cgx_tx_stats_fields[stat],
				   tx_stat);
		else
			seq_printf(s, "%s: %llu\n", rpm_tx_stats_fields[stat],
				   tx_stat);
		stat++;
	}

	return err;
}

static int rvu_dbg_derive_lmacid(struct seq_file *filp, int *lmac_id)
{
	struct dentry *current_dir;
	char *buf;

	current_dir = filp->file->f_path.dentry->d_parent;
	buf = strrchr(current_dir->d_name.name, 'c');
	if (!buf)
		return -EINVAL;

	return kstrtoint(buf + 1, 10, lmac_id);
}

static int rvu_dbg_cgx_stat_display(struct seq_file *filp, void *unused)
{
	int lmac_id, err;

	err = rvu_dbg_derive_lmacid(filp, &lmac_id);
	if (!err)
		return cgx_print_stats(filp, lmac_id);

	return err;
}

RVU_DEBUG_SEQ_FOPS(cgx_stat, cgx_stat_display, NULL);

static int cgx_print_dmac_flt(struct seq_file *s, int lmac_id)
{
	struct pci_dev *pdev = NULL;
	void *cgxd = s->private;
	char *bcast, *mcast;
	u16 index, domain;
	u8 dmac[ETH_ALEN];
	struct rvu *rvu;
	u64 cfg, mac;
	int pf;

	rvu = pci_get_drvdata(pci_get_device(PCI_VENDOR_ID_CAVIUM,
					     PCI_DEVID_OCTEONTX2_RVU_AF, NULL));
	if (!rvu)
		return -ENODEV;

	pf = cgxlmac_to_pf(rvu, cgx_get_cgxid(cgxd), lmac_id);
	domain = 2;

	pdev = pci_get_domain_bus_and_slot(domain, pf + 1, 0);
	if (!pdev)
		return 0;

	cfg = cgx_read_dmac_ctrl(cgxd, lmac_id);
	bcast = cfg & CGX_DMAC_BCAST_MODE ? "ACCEPT" : "REJECT";
	mcast = cfg & CGX_DMAC_MCAST_MODE ? "ACCEPT" : "REJECT";

	seq_puts(s,
		 "PCI dev       RVUPF   BROADCAST  MULTICAST  FILTER-MODE\n");
	seq_printf(s, "%s  PF%d  %9s  %9s",
		   dev_name(&pdev->dev), pf, bcast, mcast);
	if (cfg & CGX_DMAC_CAM_ACCEPT)
		seq_printf(s, "%12s\n\n", "UNICAST");
	else
		seq_printf(s, "%16s\n\n", "PROMISCUOUS");

	seq_puts(s, "\nDMAC-INDEX  ADDRESS\n");

	for (index = 0 ; index < 32 ; index++) {
		cfg = cgx_read_dmac_entry(cgxd, index);
		/* Display enabled dmac entries associated with current lmac */
		if (lmac_id == FIELD_GET(CGX_DMAC_CAM_ENTRY_LMACID, cfg) &&
		    FIELD_GET(CGX_DMAC_CAM_ADDR_ENABLE, cfg)) {
			mac = FIELD_GET(CGX_RX_DMAC_ADR_MASK, cfg);
			u64_to_ether_addr(mac, dmac);
			seq_printf(s, "%7d     %pM\n", index, dmac);
		}
	}

	pci_dev_put(pdev);
	return 0;
}

static int rvu_dbg_cgx_dmac_flt_display(struct seq_file *filp, void *unused)
{
	int err, lmac_id;

	err = rvu_dbg_derive_lmacid(filp, &lmac_id);
	if (!err)
		return cgx_print_dmac_flt(filp, lmac_id);

	return err;
}

RVU_DEBUG_SEQ_FOPS(cgx_dmac_flt, cgx_dmac_flt_display, NULL);

static void rvu_dbg_cgx_init(struct rvu *rvu)
{
	struct mac_ops *mac_ops;
	unsigned long lmac_bmap;
	int i, lmac_id;
	char dname[20];
	void *cgx;

	if (!cgx_get_cgxcnt_max())
		return;

	mac_ops = get_mac_ops(rvu_first_cgx_pdata(rvu));
	if (!mac_ops)
		return;

	rvu->rvu_dbg.cgx_root = debugfs_create_dir(mac_ops->name,
						   rvu->rvu_dbg.root);

	for (i = 0; i < cgx_get_cgxcnt_max(); i++) {
		cgx = rvu_cgx_pdata(i, rvu);
		if (!cgx)
			continue;
		lmac_bmap = cgx_get_lmac_bmap(cgx);
		/* cgx debugfs dir */
		sprintf(dname, "%s%d", mac_ops->name, i);
		rvu->rvu_dbg.cgx = debugfs_create_dir(dname,
						      rvu->rvu_dbg.cgx_root);

		for_each_set_bit(lmac_id, &lmac_bmap, rvu->hw->lmac_per_cgx) {
			/* lmac debugfs dir */
			sprintf(dname, "lmac%d", lmac_id);
			rvu->rvu_dbg.lmac =
				debugfs_create_dir(dname, rvu->rvu_dbg.cgx);

			debugfs_create_file("stats", 0600, rvu->rvu_dbg.lmac,
					    cgx, &rvu_dbg_cgx_stat_fops);
			debugfs_create_file("mac_filter", 0600,
					    rvu->rvu_dbg.lmac, cgx,
					    &rvu_dbg_cgx_dmac_flt_fops);
		}
	}
}

/* NPC debugfs APIs */
static void rvu_print_npc_mcam_info(struct seq_file *s,
				    u16 pcifunc, int blkaddr)
{
	struct rvu *rvu = s->private;
	int entry_acnt, entry_ecnt;
	int cntr_acnt, cntr_ecnt;

	rvu_npc_get_mcam_entry_alloc_info(rvu, pcifunc, blkaddr,
					  &entry_acnt, &entry_ecnt);
	rvu_npc_get_mcam_counter_alloc_info(rvu, pcifunc, blkaddr,
					    &cntr_acnt, &cntr_ecnt);
	if (!entry_acnt && !cntr_acnt)
		return;

	if (!(pcifunc & RVU_PFVF_FUNC_MASK))
		seq_printf(s, "\n\t\t Device \t\t: PF%d\n",
			   rvu_get_pf(pcifunc));
	else
		seq_printf(s, "\n\t\t Device \t\t: PF%d VF%d\n",
			   rvu_get_pf(pcifunc),
			   (pcifunc & RVU_PFVF_FUNC_MASK) - 1);

	if (entry_acnt) {
		seq_printf(s, "\t\t Entries allocated \t: %d\n", entry_acnt);
		seq_printf(s, "\t\t Entries enabled \t: %d\n", entry_ecnt);
	}
	if (cntr_acnt) {
		seq_printf(s, "\t\t Counters allocated \t: %d\n", cntr_acnt);
		seq_printf(s, "\t\t Counters enabled \t: %d\n", cntr_ecnt);
	}
}

static int rvu_dbg_npc_mcam_info_display(struct seq_file *filp, void *unsued)
{
	struct rvu *rvu = filp->private;
	int pf, vf, numvfs, blkaddr;
	struct npc_mcam *mcam;
	u16 pcifunc, counters;
	u64 cfg;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return -ENODEV;

	mcam = &rvu->hw->mcam;
	counters = rvu->hw->npc_counters;

	seq_puts(filp, "\nNPC MCAM info:\n");
	/* MCAM keywidth on receive and transmit sides */
	cfg = rvu_read64(rvu, blkaddr, NPC_AF_INTFX_KEX_CFG(NIX_INTF_RX));
	cfg = (cfg >> 32) & 0x07;
	seq_printf(filp, "\t\t RX keywidth \t: %s\n", (cfg == NPC_MCAM_KEY_X1) ?
		   "112bits" : ((cfg == NPC_MCAM_KEY_X2) ?
		   "224bits" : "448bits"));
	cfg = rvu_read64(rvu, blkaddr, NPC_AF_INTFX_KEX_CFG(NIX_INTF_TX));
	cfg = (cfg >> 32) & 0x07;
	seq_printf(filp, "\t\t TX keywidth \t: %s\n", (cfg == NPC_MCAM_KEY_X1) ?
		   "112bits" : ((cfg == NPC_MCAM_KEY_X2) ?
		   "224bits" : "448bits"));

	mutex_lock(&mcam->lock);
	/* MCAM entries */
	seq_printf(filp, "\n\t\t MCAM entries \t: %d\n", mcam->total_entries);
	seq_printf(filp, "\t\t Reserved \t: %d\n",
		   mcam->total_entries - mcam->bmap_entries);
	seq_printf(filp, "\t\t Available \t: %d\n", mcam->bmap_fcnt);

	/* MCAM counters */
	seq_printf(filp, "\n\t\t MCAM counters \t: %d\n", counters);
	seq_printf(filp, "\t\t Reserved \t: %d\n",
		   counters - mcam->counters.max);
	seq_printf(filp, "\t\t Available \t: %d\n",
		   rvu_rsrc_free_count(&mcam->counters));

	if (mcam->bmap_entries == mcam->bmap_fcnt) {
		mutex_unlock(&mcam->lock);
		return 0;
	}

	seq_puts(filp, "\n\t\t Current allocation\n");
	seq_puts(filp, "\t\t====================\n");
	for (pf = 0; pf < rvu->hw->total_pfs; pf++) {
		pcifunc = (pf << RVU_PFVF_PF_SHIFT);
		rvu_print_npc_mcam_info(filp, pcifunc, blkaddr);

		cfg = rvu_read64(rvu, BLKADDR_RVUM, RVU_PRIV_PFX_CFG(pf));
		numvfs = (cfg >> 12) & 0xFF;
		for (vf = 0; vf < numvfs; vf++) {
			pcifunc = (pf << RVU_PFVF_PF_SHIFT) | (vf + 1);
			rvu_print_npc_mcam_info(filp, pcifunc, blkaddr);
		}
	}

	mutex_unlock(&mcam->lock);
	return 0;
}

RVU_DEBUG_SEQ_FOPS(npc_mcam_info, npc_mcam_info_display, NULL);

static int rvu_dbg_npc_rx_miss_stats_display(struct seq_file *filp,
					     void *unused)
{
	struct rvu *rvu = filp->private;
	struct npc_mcam *mcam;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return -ENODEV;

	mcam = &rvu->hw->mcam;

	seq_puts(filp, "\nNPC MCAM RX miss action stats\n");
	seq_printf(filp, "\t\tStat %d: \t%lld\n", mcam->rx_miss_act_cntr,
		   rvu_read64(rvu, blkaddr,
			      NPC_AF_MATCH_STATX(mcam->rx_miss_act_cntr)));

	return 0;
}

RVU_DEBUG_SEQ_FOPS(npc_rx_miss_act, npc_rx_miss_stats_display, NULL);

static void rvu_dbg_npc_mcam_show_flows(struct seq_file *s,
					struct rvu_npc_mcam_rule *rule)
{
	u8 bit;

	for_each_set_bit(bit, (unsigned long *)&rule->features, 64) {
		seq_printf(s, "\t%s  ", npc_get_field_name(bit));
		switch (bit) {
		case NPC_LXMB:
			if (rule->lxmb == 1)
				seq_puts(s, "\tL2M nibble is set\n");
			else
				seq_puts(s, "\tL2B nibble is set\n");
			break;
		case NPC_DMAC:
			seq_printf(s, "%pM ", rule->packet.dmac);
			seq_printf(s, "mask %pM\n", rule->mask.dmac);
			break;
		case NPC_SMAC:
			seq_printf(s, "%pM ", rule->packet.smac);
			seq_printf(s, "mask %pM\n", rule->mask.smac);
			break;
		case NPC_ETYPE:
			seq_printf(s, "0x%x ", ntohs(rule->packet.etype));
			seq_printf(s, "mask 0x%x\n", ntohs(rule->mask.etype));
			break;
		case NPC_OUTER_VID:
			seq_printf(s, "0x%x ", ntohs(rule->packet.vlan_tci));
			seq_printf(s, "mask 0x%x\n",
				   ntohs(rule->mask.vlan_tci));
			break;
		case NPC_TOS:
			seq_printf(s, "%d ", rule->packet.tos);
			seq_printf(s, "mask 0x%x\n", rule->mask.tos);
			break;
		case NPC_SIP_IPV4:
			seq_printf(s, "%pI4 ", &rule->packet.ip4src);
			seq_printf(s, "mask %pI4\n", &rule->mask.ip4src);
			break;
		case NPC_DIP_IPV4:
			seq_printf(s, "%pI4 ", &rule->packet.ip4dst);
			seq_printf(s, "mask %pI4\n", &rule->mask.ip4dst);
			break;
		case NPC_SIP_IPV6:
			seq_printf(s, "%pI6 ", rule->packet.ip6src);
			seq_printf(s, "mask %pI6\n", rule->mask.ip6src);
			break;
		case NPC_DIP_IPV6:
			seq_printf(s, "%pI6 ", rule->packet.ip6dst);
			seq_printf(s, "mask %pI6\n", rule->mask.ip6dst);
			break;
		case NPC_SPORT_TCP:
		case NPC_SPORT_UDP:
		case NPC_SPORT_SCTP:
			seq_printf(s, "%d ", ntohs(rule->packet.sport));
			seq_printf(s, "mask 0x%x\n", ntohs(rule->mask.sport));
			break;
		case NPC_DPORT_TCP:
		case NPC_DPORT_UDP:
		case NPC_DPORT_SCTP:
			seq_printf(s, "%d ", ntohs(rule->packet.dport));
			seq_printf(s, "mask 0x%x\n", ntohs(rule->mask.dport));
			break;
		default:
			seq_puts(s, "\n");
			break;
		}
	}
}

static void rvu_dbg_npc_mcam_show_action(struct seq_file *s,
					 struct rvu_npc_mcam_rule *rule)
{
	if (is_npc_intf_tx(rule->intf)) {
		switch (rule->tx_action.op) {
		case NIX_TX_ACTIONOP_DROP:
			seq_puts(s, "\taction: Drop\n");
			break;
		case NIX_TX_ACTIONOP_UCAST_DEFAULT:
			seq_puts(s, "\taction: Unicast to default channel\n");
			break;
		case NIX_TX_ACTIONOP_UCAST_CHAN:
			seq_printf(s, "\taction: Unicast to channel %d\n",
				   rule->tx_action.index);
			break;
		case NIX_TX_ACTIONOP_MCAST:
			seq_puts(s, "\taction: Multicast\n");
			break;
		case NIX_TX_ACTIONOP_DROP_VIOL:
			seq_puts(s, "\taction: Lockdown Violation Drop\n");
			break;
		default:
			break;
		}
	} else {
		switch (rule->rx_action.op) {
		case NIX_RX_ACTIONOP_DROP:
			seq_puts(s, "\taction: Drop\n");
			break;
		case NIX_RX_ACTIONOP_UCAST:
			seq_printf(s, "\taction: Direct to queue %d\n",
				   rule->rx_action.index);
			break;
		case NIX_RX_ACTIONOP_RSS:
			seq_puts(s, "\taction: RSS\n");
			break;
		case NIX_RX_ACTIONOP_UCAST_IPSEC:
			seq_puts(s, "\taction: Unicast ipsec\n");
			break;
		case NIX_RX_ACTIONOP_MCAST:
			seq_puts(s, "\taction: Multicast\n");
			break;
		default:
			break;
		}
	}
}

static const char *rvu_dbg_get_intf_name(int intf)
{
	switch (intf) {
	case NIX_INTFX_RX(0):
		return "NIX0_RX";
	case NIX_INTFX_RX(1):
		return "NIX1_RX";
	case NIX_INTFX_TX(0):
		return "NIX0_TX";
	case NIX_INTFX_TX(1):
		return "NIX1_TX";
	default:
		break;
	}

	return "unknown";
}

static int rvu_dbg_npc_mcam_show_rules(struct seq_file *s, void *unused)
{
	struct rvu_npc_mcam_rule *iter;
	struct rvu *rvu = s->private;
	struct npc_mcam *mcam;
	int pf, vf = -1;
	bool enabled;
	int blkaddr;
	u16 target;
	u64 hits;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return 0;

	mcam = &rvu->hw->mcam;

	mutex_lock(&mcam->lock);
	list_for_each_entry(iter, &mcam->mcam_rules, list) {
		pf = (iter->owner >> RVU_PFVF_PF_SHIFT) & RVU_PFVF_PF_MASK;
		seq_printf(s, "\n\tInstalled by: PF%d ", pf);

		if (iter->owner & RVU_PFVF_FUNC_MASK) {
			vf = (iter->owner & RVU_PFVF_FUNC_MASK) - 1;
			seq_printf(s, "VF%d", vf);
		}
		seq_puts(s, "\n");

		seq_printf(s, "\tdirection: %s\n", is_npc_intf_rx(iter->intf) ?
						    "RX" : "TX");
		seq_printf(s, "\tinterface: %s\n",
			   rvu_dbg_get_intf_name(iter->intf));
		seq_printf(s, "\tmcam entry: %d\n", iter->entry);

		rvu_dbg_npc_mcam_show_flows(s, iter);
		if (is_npc_intf_rx(iter->intf)) {
			target = iter->rx_action.pf_func;
			pf = (target >> RVU_PFVF_PF_SHIFT) & RVU_PFVF_PF_MASK;
			seq_printf(s, "\tForward to: PF%d ", pf);

			if (target & RVU_PFVF_FUNC_MASK) {
				vf = (target & RVU_PFVF_FUNC_MASK) - 1;
				seq_printf(s, "VF%d", vf);
			}
			seq_puts(s, "\n");
			seq_printf(s, "\tchannel: 0x%x\n", iter->chan);
			seq_printf(s, "\tchannel_mask: 0x%x\n", iter->chan_mask);
		}

		rvu_dbg_npc_mcam_show_action(s, iter);

		enabled = is_mcam_entry_enabled(rvu, mcam, blkaddr, iter->entry);
		seq_printf(s, "\tenabled: %s\n", enabled ? "yes" : "no");

		if (!iter->has_cntr)
			continue;
		seq_printf(s, "\tcounter: %d\n", iter->cntr);

		hits = rvu_read64(rvu, blkaddr, NPC_AF_MATCH_STATX(iter->cntr));
		seq_printf(s, "\thits: %lld\n", hits);
	}
	mutex_unlock(&mcam->lock);

	return 0;
}

RVU_DEBUG_SEQ_FOPS(npc_mcam_rules, npc_mcam_show_rules, NULL);

static int rvu_dbg_npc_exact_show_entries(struct seq_file *s, void *unused)
{
	struct npc_exact_table_entry *mem_entry[NPC_EXACT_TBL_MAX_WAYS] = { 0 };
	struct npc_exact_table_entry *cam_entry;
	struct npc_exact_table *table;
	struct rvu *rvu = s->private;
	int i, j;

	u8 bitmap = 0;

	table = rvu->hw->table;

	mutex_lock(&table->lock);

	/* Check if there is at least one entry in mem table */
	if (!table->mem_tbl_entry_cnt)
		goto dump_cam_table;

	/* Print table headers */
	seq_puts(s, "\n\tExact Match MEM Table\n");
	seq_puts(s, "Index\t");

	for (i = 0; i < table->mem_table.ways; i++) {
		mem_entry[i] = list_first_entry_or_null(&table->lhead_mem_tbl_entry[i],
							struct npc_exact_table_entry, list);

		seq_printf(s, "Way-%d\t\t\t\t\t", i);
	}

	seq_puts(s, "\n");
	for (i = 0; i < table->mem_table.ways; i++)
		seq_puts(s, "\tChan  MAC                     \t");

	seq_puts(s, "\n\n");

	/* Print mem table entries */
	for (i = 0; i < table->mem_table.depth; i++) {
		bitmap = 0;
		for (j = 0; j < table->mem_table.ways; j++) {
			if (!mem_entry[j])
				continue;

			if (mem_entry[j]->index != i)
				continue;

			bitmap |= BIT(j);
		}

		/* No valid entries */
		if (!bitmap)
			continue;

		seq_printf(s, "%d\t", i);
		for (j = 0; j < table->mem_table.ways; j++) {
			if (!(bitmap & BIT(j))) {
				seq_puts(s, "nil\t\t\t\t\t");
				continue;
			}

			seq_printf(s, "0x%x %pM\t\t\t", mem_entry[j]->chan,
				   mem_entry[j]->mac);
			mem_entry[j] = list_next_entry(mem_entry[j], list);
		}
		seq_puts(s, "\n");
	}

dump_cam_table:

	if (!table->cam_tbl_entry_cnt)
		goto done;

	seq_puts(s, "\n\tExact Match CAM Table\n");
	seq_puts(s, "index\tchan\tMAC\n");

	/* Traverse cam table entries */
	list_for_each_entry(cam_entry, &table->lhead_cam_tbl_entry, list) {
		seq_printf(s, "%d\t0x%x\t%pM\n", cam_entry->index, cam_entry->chan,
			   cam_entry->mac);
	}

done:
	mutex_unlock(&table->lock);
	return 0;
}

RVU_DEBUG_SEQ_FOPS(npc_exact_entries, npc_exact_show_entries, NULL);

static int rvu_dbg_npc_exact_show_info(struct seq_file *s, void *unused)
{
	struct npc_exact_table *table;
	struct rvu *rvu = s->private;
	int i;

	table = rvu->hw->table;

	seq_puts(s, "\n\tExact Table Info\n");
	seq_printf(s, "Exact Match Feature : %s\n",
		   rvu->hw->cap.npc_exact_match_enabled ? "enabled" : "disable");
	if (!rvu->hw->cap.npc_exact_match_enabled)
		return 0;

	seq_puts(s, "\nMCAM Index\tMAC Filter Rules Count\n");
	for (i = 0; i < table->num_drop_rules; i++)
		seq_printf(s, "%d\t\t%d\n", i, table->cnt_cmd_rules[i]);

	seq_puts(s, "\nMcam Index\tPromisc Mode Status\n");
	for (i = 0; i < table->num_drop_rules; i++)
		seq_printf(s, "%d\t\t%s\n", i, table->promisc_mode[i] ? "on" : "off");

	seq_puts(s, "\n\tMEM Table Info\n");
	seq_printf(s, "Ways : %d\n", table->mem_table.ways);
	seq_printf(s, "Depth : %d\n", table->mem_table.depth);
	seq_printf(s, "Mask : 0x%llx\n", table->mem_table.mask);
	seq_printf(s, "Hash Mask : 0x%x\n", table->mem_table.hash_mask);
	seq_printf(s, "Hash Offset : 0x%x\n", table->mem_table.hash_offset);

	seq_puts(s, "\n\tCAM Table Info\n");
	seq_printf(s, "Depth : %d\n", table->cam_table.depth);

	return 0;
}

RVU_DEBUG_SEQ_FOPS(npc_exact_info, npc_exact_show_info, NULL);

static int rvu_dbg_npc_exact_drop_cnt(struct seq_file *s, void *unused)
{
	struct npc_exact_table *table;
	struct rvu *rvu = s->private;
	struct npc_key_field *field;
	u16 chan, pcifunc;
	int blkaddr, i;
	u64 cfg, cam1;
	char *str;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	table = rvu->hw->table;

	field = &rvu->hw->mcam.rx_key_fields[NPC_CHAN];

	seq_puts(s, "\n\t Exact Hit on drop status\n");
	seq_puts(s, "\npcifunc\tmcam_idx\tHits\tchan\tstatus\n");

	for (i = 0; i < table->num_drop_rules; i++) {
		pcifunc = rvu_npc_exact_drop_rule_to_pcifunc(rvu, i);
		cfg = rvu_read64(rvu, blkaddr, NPC_AF_MCAMEX_BANKX_CFG(i, 0));

		/* channel will be always in keyword 0 */
		cam1 = rvu_read64(rvu, blkaddr,
				  NPC_AF_MCAMEX_BANKX_CAMX_W0(i, 0, 1));
		chan = field->kw_mask[0] & cam1;

		str = (cfg & 1) ? "enabled" : "disabled";

		seq_printf(s, "0x%x\t%d\t\t%llu\t0x%x\t%s\n", pcifunc, i,
			   rvu_read64(rvu, blkaddr,
				      NPC_AF_MATCH_STATX(table->counter_idx[i])),
			   chan, str);
	}

	return 0;
}

RVU_DEBUG_SEQ_FOPS(npc_exact_drop_cnt, npc_exact_drop_cnt, NULL);

static void rvu_dbg_npc_init(struct rvu *rvu)
{
	rvu->rvu_dbg.npc = debugfs_create_dir("npc", rvu->rvu_dbg.root);

	debugfs_create_file("mcam_info", 0444, rvu->rvu_dbg.npc, rvu,
			    &rvu_dbg_npc_mcam_info_fops);
	debugfs_create_file("mcam_rules", 0444, rvu->rvu_dbg.npc, rvu,
			    &rvu_dbg_npc_mcam_rules_fops);

	debugfs_create_file("rx_miss_act_stats", 0444, rvu->rvu_dbg.npc, rvu,
			    &rvu_dbg_npc_rx_miss_act_fops);

	if (!rvu->hw->cap.npc_exact_match_enabled)
		return;

	debugfs_create_file("exact_entries", 0444, rvu->rvu_dbg.npc, rvu,
			    &rvu_dbg_npc_exact_entries_fops);

	debugfs_create_file("exact_info", 0444, rvu->rvu_dbg.npc, rvu,
			    &rvu_dbg_npc_exact_info_fops);

	debugfs_create_file("exact_drop_cnt", 0444, rvu->rvu_dbg.npc, rvu,
			    &rvu_dbg_npc_exact_drop_cnt_fops);

}

static int cpt_eng_sts_display(struct seq_file *filp, u8 eng_type)
{
	struct cpt_ctx *ctx = filp->private;
	u64 busy_sts = 0, free_sts = 0;
	u32 e_min = 0, e_max = 0, e, i;
	u16 max_ses, max_ies, max_aes;
	struct rvu *rvu = ctx->rvu;
	int blkaddr = ctx->blkaddr;
	u64 reg;

	reg = rvu_read64(rvu, blkaddr, CPT_AF_CONSTANTS1);
	max_ses = reg & 0xffff;
	max_ies = (reg >> 16) & 0xffff;
	max_aes = (reg >> 32) & 0xffff;

	switch (eng_type) {
	case CPT_AE_TYPE:
		e_min = max_ses + max_ies;
		e_max = max_ses + max_ies + max_aes;
		break;
	case CPT_SE_TYPE:
		e_min = 0;
		e_max = max_ses;
		break;
	case CPT_IE_TYPE:
		e_min = max_ses;
		e_max = max_ses + max_ies;
		break;
	default:
		return -EINVAL;
	}

	for (e = e_min, i = 0; e < e_max; e++, i++) {
		reg = rvu_read64(rvu, blkaddr, CPT_AF_EXEX_STS(e));
		if (reg & 0x1)
			busy_sts |= 1ULL << i;

		if (reg & 0x2)
			free_sts |= 1ULL << i;
	}
	seq_printf(filp, "FREE STS : 0x%016llx\n", free_sts);
	seq_printf(filp, "BUSY STS : 0x%016llx\n", busy_sts);

	return 0;
}

static int rvu_dbg_cpt_ae_sts_display(struct seq_file *filp, void *unused)
{
	return cpt_eng_sts_display(filp, CPT_AE_TYPE);
}

RVU_DEBUG_SEQ_FOPS(cpt_ae_sts, cpt_ae_sts_display, NULL);

static int rvu_dbg_cpt_se_sts_display(struct seq_file *filp, void *unused)
{
	return cpt_eng_sts_display(filp, CPT_SE_TYPE);
}

RVU_DEBUG_SEQ_FOPS(cpt_se_sts, cpt_se_sts_display, NULL);

static int rvu_dbg_cpt_ie_sts_display(struct seq_file *filp, void *unused)
{
	return cpt_eng_sts_display(filp, CPT_IE_TYPE);
}

RVU_DEBUG_SEQ_FOPS(cpt_ie_sts, cpt_ie_sts_display, NULL);

static int rvu_dbg_cpt_engines_info_display(struct seq_file *filp, void *unused)
{
	struct cpt_ctx *ctx = filp->private;
	u16 max_ses, max_ies, max_aes;
	struct rvu *rvu = ctx->rvu;
	int blkaddr = ctx->blkaddr;
	u32 e_max, e;
	u64 reg;

	reg = rvu_read64(rvu, blkaddr, CPT_AF_CONSTANTS1);
	max_ses = reg & 0xffff;
	max_ies = (reg >> 16) & 0xffff;
	max_aes = (reg >> 32) & 0xffff;

	e_max = max_ses + max_ies + max_aes;

	seq_puts(filp, "===========================================\n");
	for (e = 0; e < e_max; e++) {
		reg = rvu_read64(rvu, blkaddr, CPT_AF_EXEX_CTL2(e));
		seq_printf(filp, "CPT Engine[%u] Group Enable   0x%02llx\n", e,
			   reg & 0xff);
		reg = rvu_read64(rvu, blkaddr, CPT_AF_EXEX_ACTIVE(e));
		seq_printf(filp, "CPT Engine[%u] Active Info    0x%llx\n", e,
			   reg);
		reg = rvu_read64(rvu, blkaddr, CPT_AF_EXEX_CTL(e));
		seq_printf(filp, "CPT Engine[%u] Control        0x%llx\n", e,
			   reg);
		seq_puts(filp, "===========================================\n");
	}
	return 0;
}

RVU_DEBUG_SEQ_FOPS(cpt_engines_info, cpt_engines_info_display, NULL);

static int rvu_dbg_cpt_lfs_info_display(struct seq_file *filp, void *unused)
{
	struct cpt_ctx *ctx = filp->private;
	int blkaddr = ctx->blkaddr;
	struct rvu *rvu = ctx->rvu;
	struct rvu_block *block;
	struct rvu_hwinfo *hw;
	u64 reg;
	u32 lf;

	hw = rvu->hw;
	block = &hw->block[blkaddr];
	if (!block->lf.bmap)
		return -ENODEV;

	seq_puts(filp, "===========================================\n");
	for (lf = 0; lf < block->lf.max; lf++) {
		reg = rvu_read64(rvu, blkaddr, CPT_AF_LFX_CTL(lf));
		seq_printf(filp, "CPT Lf[%u] CTL          0x%llx\n", lf, reg);
		reg = rvu_read64(rvu, blkaddr, CPT_AF_LFX_CTL2(lf));
		seq_printf(filp, "CPT Lf[%u] CTL2         0x%llx\n", lf, reg);
		reg = rvu_read64(rvu, blkaddr, CPT_AF_LFX_PTR_CTL(lf));
		seq_printf(filp, "CPT Lf[%u] PTR_CTL      0x%llx\n", lf, reg);
		reg = rvu_read64(rvu, blkaddr, block->lfcfg_reg |
				(lf << block->lfshift));
		seq_printf(filp, "CPT Lf[%u] CFG          0x%llx\n", lf, reg);
		seq_puts(filp, "===========================================\n");
	}
	return 0;
}

RVU_DEBUG_SEQ_FOPS(cpt_lfs_info, cpt_lfs_info_display, NULL);

static int rvu_dbg_cpt_err_info_display(struct seq_file *filp, void *unused)
{
	struct cpt_ctx *ctx = filp->private;
	struct rvu *rvu = ctx->rvu;
	int blkaddr = ctx->blkaddr;
	u64 reg0, reg1;

	reg0 = rvu_read64(rvu, blkaddr, CPT_AF_FLTX_INT(0));
	reg1 = rvu_read64(rvu, blkaddr, CPT_AF_FLTX_INT(1));
	seq_printf(filp, "CPT_AF_FLTX_INT:       0x%llx 0x%llx\n", reg0, reg1);
	reg0 = rvu_read64(rvu, blkaddr, CPT_AF_PSNX_EXE(0));
	reg1 = rvu_read64(rvu, blkaddr, CPT_AF_PSNX_EXE(1));
	seq_printf(filp, "CPT_AF_PSNX_EXE:       0x%llx 0x%llx\n", reg0, reg1);
	reg0 = rvu_read64(rvu, blkaddr, CPT_AF_PSNX_LF(0));
	seq_printf(filp, "CPT_AF_PSNX_LF:        0x%llx\n", reg0);
	reg0 = rvu_read64(rvu, blkaddr, CPT_AF_RVU_INT);
	seq_printf(filp, "CPT_AF_RVU_INT:        0x%llx\n", reg0);
	reg0 = rvu_read64(rvu, blkaddr, CPT_AF_RAS_INT);
	seq_printf(filp, "CPT_AF_RAS_INT:        0x%llx\n", reg0);
	reg0 = rvu_read64(rvu, blkaddr, CPT_AF_EXE_ERR_INFO);
	seq_printf(filp, "CPT_AF_EXE_ERR_INFO:   0x%llx\n", reg0);

	return 0;
}

RVU_DEBUG_SEQ_FOPS(cpt_err_info, cpt_err_info_display, NULL);

static int rvu_dbg_cpt_pc_display(struct seq_file *filp, void *unused)
{
	struct cpt_ctx *ctx = filp->private;
	struct rvu *rvu = ctx->rvu;
	int blkaddr = ctx->blkaddr;
	u64 reg;

	reg = rvu_read64(rvu, blkaddr, CPT_AF_INST_REQ_PC);
	seq_printf(filp, "CPT instruction requests   %llu\n", reg);
	reg = rvu_read64(rvu, blkaddr, CPT_AF_INST_LATENCY_PC);
	seq_printf(filp, "CPT instruction latency    %llu\n", reg);
	reg = rvu_read64(rvu, blkaddr, CPT_AF_RD_REQ_PC);
	seq_printf(filp, "CPT NCB read requests      %llu\n", reg);
	reg = rvu_read64(rvu, blkaddr, CPT_AF_RD_LATENCY_PC);
	seq_printf(filp, "CPT NCB read latency       %llu\n", reg);
	reg = rvu_read64(rvu, blkaddr, CPT_AF_RD_UC_PC);
	seq_printf(filp, "CPT read requests caused by UC fills   %llu\n", reg);
	reg = rvu_read64(rvu, blkaddr, CPT_AF_ACTIVE_CYCLES_PC);
	seq_printf(filp, "CPT active cycles pc       %llu\n", reg);
	reg = rvu_read64(rvu, blkaddr, CPT_AF_CPTCLK_CNT);
	seq_printf(filp, "CPT clock count pc         %llu\n", reg);

	return 0;
}

RVU_DEBUG_SEQ_FOPS(cpt_pc, cpt_pc_display, NULL);

static void rvu_dbg_cpt_init(struct rvu *rvu, int blkaddr)
{
	struct cpt_ctx *ctx;

	if (!is_block_implemented(rvu->hw, blkaddr))
		return;

	if (blkaddr == BLKADDR_CPT0) {
		rvu->rvu_dbg.cpt = debugfs_create_dir("cpt", rvu->rvu_dbg.root);
		ctx = &rvu->rvu_dbg.cpt_ctx[0];
		ctx->blkaddr = BLKADDR_CPT0;
		ctx->rvu = rvu;
	} else {
		rvu->rvu_dbg.cpt = debugfs_create_dir("cpt1",
						      rvu->rvu_dbg.root);
		ctx = &rvu->rvu_dbg.cpt_ctx[1];
		ctx->blkaddr = BLKADDR_CPT1;
		ctx->rvu = rvu;
	}

	debugfs_create_file("cpt_pc", 0600, rvu->rvu_dbg.cpt, ctx,
			    &rvu_dbg_cpt_pc_fops);
	debugfs_create_file("cpt_ae_sts", 0600, rvu->rvu_dbg.cpt, ctx,
			    &rvu_dbg_cpt_ae_sts_fops);
	debugfs_create_file("cpt_se_sts", 0600, rvu->rvu_dbg.cpt, ctx,
			    &rvu_dbg_cpt_se_sts_fops);
	debugfs_create_file("cpt_ie_sts", 0600, rvu->rvu_dbg.cpt, ctx,
			    &rvu_dbg_cpt_ie_sts_fops);
	debugfs_create_file("cpt_engines_info", 0600, rvu->rvu_dbg.cpt, ctx,
			    &rvu_dbg_cpt_engines_info_fops);
	debugfs_create_file("cpt_lfs_info", 0600, rvu->rvu_dbg.cpt, ctx,
			    &rvu_dbg_cpt_lfs_info_fops);
	debugfs_create_file("cpt_err_info", 0600, rvu->rvu_dbg.cpt, ctx,
			    &rvu_dbg_cpt_err_info_fops);
}

static const char *rvu_get_dbg_dir_name(struct rvu *rvu)
{
	if (!is_rvu_otx2(rvu))
		return "cn10k";
	else
		return "octeontx2";
}

void rvu_dbg_init(struct rvu *rvu)
{
	rvu->rvu_dbg.root = debugfs_create_dir(rvu_get_dbg_dir_name(rvu), NULL);

	debugfs_create_file("rsrc_alloc", 0444, rvu->rvu_dbg.root, rvu,
			    &rvu_dbg_rsrc_status_fops);

	if (!is_rvu_otx2(rvu))
		debugfs_create_file("lmtst_map_table", 0444, rvu->rvu_dbg.root,
				    rvu, &rvu_dbg_lmtst_map_table_fops);

	if (!cgx_get_cgxcnt_max())
		goto create;

	if (is_rvu_otx2(rvu))
		debugfs_create_file("rvu_pf_cgx_map", 0444, rvu->rvu_dbg.root,
				    rvu, &rvu_dbg_rvu_pf_cgx_map_fops);
	else
		debugfs_create_file("rvu_pf_rpm_map", 0444, rvu->rvu_dbg.root,
				    rvu, &rvu_dbg_rvu_pf_cgx_map_fops);

create:
	rvu_dbg_npa_init(rvu);
	rvu_dbg_nix_init(rvu, BLKADDR_NIX0);

	rvu_dbg_nix_init(rvu, BLKADDR_NIX1);
	rvu_dbg_cgx_init(rvu);
	rvu_dbg_npc_init(rvu);
	rvu_dbg_cpt_init(rvu, BLKADDR_CPT0);
	rvu_dbg_cpt_init(rvu, BLKADDR_CPT1);
	rvu_dbg_mcs_init(rvu);
}

void rvu_dbg_exit(struct rvu *rvu)
{
	debugfs_remove_recursive(rvu->rvu_dbg.root);
}

#endif /* CONFIG_DEBUG_FS */
