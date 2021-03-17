// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2015 Freescale Semiconductor Inc.
 * Copyright 2018-2019 NXP
 */
#include <linux/module.h>
#include <linux/debugfs.h>
#include "dpaa2-eth.h"
#include "dpaa2-eth-debugfs.h"

#define DPAA2_ETH_DBG_ROOT "dpaa2-eth"

static struct dentry *dpaa2_dbg_root;

static int dpaa2_dbg_cpu_show(struct seq_file *file, void *offset)
{
	struct dpaa2_eth_priv *priv = (struct dpaa2_eth_priv *)file->private;
	struct rtnl_link_stats64 *stats;
	struct dpaa2_eth_drv_stats *extras;
	int i;

	seq_printf(file, "Per-CPU stats for %s\n", priv->net_dev->name);
	seq_printf(file, "%s%16s%16s%16s%16s%16s%16s%16s%16s%16s\n",
		   "CPU", "Rx", "Rx Err", "Rx SG", "Tx", "Tx Err", "Tx conf",
		   "Tx SG", "Tx converted to SG", "Enq busy");

	for_each_online_cpu(i) {
		stats = per_cpu_ptr(priv->percpu_stats, i);
		extras = per_cpu_ptr(priv->percpu_extras, i);
		seq_printf(file, "%3d%16llu%16llu%16llu%16llu%16llu%16llu%16llu%16llu%16llu\n",
			   i,
			   stats->rx_packets,
			   stats->rx_errors,
			   extras->rx_sg_frames,
			   stats->tx_packets,
			   stats->tx_errors,
			   extras->tx_conf_frames,
			   extras->tx_sg_frames,
			   extras->tx_converted_sg_frames,
			   extras->tx_portal_busy);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(dpaa2_dbg_cpu);

static char *fq_type_to_str(struct dpaa2_eth_fq *fq)
{
	switch (fq->type) {
	case DPAA2_RX_FQ:
		return "Rx";
	case DPAA2_TX_CONF_FQ:
		return "Tx conf";
	default:
		return "N/A";
	}
}

static int dpaa2_dbg_fqs_show(struct seq_file *file, void *offset)
{
	struct dpaa2_eth_priv *priv = (struct dpaa2_eth_priv *)file->private;
	struct dpaa2_eth_fq *fq;
	u32 fcnt, bcnt;
	int i, err;

	seq_printf(file, "FQ stats for %s:\n", priv->net_dev->name);
	seq_printf(file, "%s%16s%16s%16s%16s%16s\n",
		   "VFQID", "CPU", "TC", "Type", "Frames", "Pending frames");

	for (i = 0; i <  priv->num_fqs; i++) {
		fq = &priv->fq[i];
		err = dpaa2_io_query_fq_count(NULL, fq->fqid, &fcnt, &bcnt);
		if (err)
			fcnt = 0;

		/* Skip FQs with no traffic */
		if (!fq->stats.frames && !fcnt)
			continue;

		seq_printf(file, "%5d%16d%16d%16s%16llu%16u\n",
			   fq->fqid,
			   fq->target_cpu,
			   fq->tc,
			   fq_type_to_str(fq),
			   fq->stats.frames,
			   fcnt);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(dpaa2_dbg_fqs);

static int dpaa2_dbg_ch_show(struct seq_file *file, void *offset)
{
	struct dpaa2_eth_priv *priv = (struct dpaa2_eth_priv *)file->private;
	struct dpaa2_eth_channel *ch;
	int i;

	seq_printf(file, "Channel stats for %s:\n", priv->net_dev->name);
	seq_printf(file, "%s%16s%16s%16s%16s%16s%16s\n",
		   "CHID", "CPU", "Deq busy", "Frames", "CDANs",
		   "Avg Frm/CDAN", "Buf count");

	for (i = 0; i < priv->num_channels; i++) {
		ch = priv->channel[i];
		seq_printf(file, "%4d%16d%16llu%16llu%16llu%16llu%16d\n",
			   ch->ch_id,
			   ch->nctx.desired_cpu,
			   ch->stats.dequeue_portal_busy,
			   ch->stats.frames,
			   ch->stats.cdan,
			   div64_u64(ch->stats.frames, ch->stats.cdan),
			   ch->buf_count);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(dpaa2_dbg_ch);

void dpaa2_dbg_add(struct dpaa2_eth_priv *priv)
{
	struct dentry *dir;

	/* Create a directory for the interface */
	dir = debugfs_create_dir(priv->net_dev->name, dpaa2_dbg_root);
	priv->dbg.dir = dir;

	/* per-cpu stats file */
	debugfs_create_file("cpu_stats", 0444, dir, priv, &dpaa2_dbg_cpu_fops);

	/* per-fq stats file */
	debugfs_create_file("fq_stats", 0444, dir, priv, &dpaa2_dbg_fqs_fops);

	/* per-fq stats file */
	debugfs_create_file("ch_stats", 0444, dir, priv, &dpaa2_dbg_ch_fops);
}

void dpaa2_dbg_remove(struct dpaa2_eth_priv *priv)
{
	debugfs_remove_recursive(priv->dbg.dir);
}

void dpaa2_eth_dbg_init(void)
{
	dpaa2_dbg_root = debugfs_create_dir(DPAA2_ETH_DBG_ROOT, NULL);
	pr_debug("DPAA2-ETH: debugfs created\n");
}

void dpaa2_eth_dbg_exit(void)
{
	debugfs_remove(dpaa2_dbg_root);
}
