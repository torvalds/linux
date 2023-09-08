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
	seq_printf(file, "%s  %5s%16s%16s%16s%16s%16s%16s\n",
		   "IDX", "CHID", "CPU", "Deq busy", "Frames", "CDANs",
		   "Avg Frm/CDAN", "Buf count");

	for (i = 0; i < priv->num_channels; i++) {
		ch = priv->channel[i];
		seq_printf(file, "%3s%d%6d%16d%16llu%16llu%16llu%16llu%16d\n",
			   "CH#", i, ch->ch_id,
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

static int dpaa2_dbg_bp_show(struct seq_file *file, void *offset)
{
	struct dpaa2_eth_priv *priv = (struct dpaa2_eth_priv *)file->private;
	int i, j, num_queues, buf_cnt;
	struct dpaa2_eth_bp *bp;
	char ch_name[10];
	int err;

	/* Print out the header */
	seq_printf(file, "Buffer pool info for %s:\n", priv->net_dev->name);
	seq_printf(file, "%s  %10s%15s", "IDX", "BPID", "Buf count");
	num_queues = dpaa2_eth_queue_count(priv);
	for (i = 0; i < num_queues; i++) {
		snprintf(ch_name, sizeof(ch_name), "CH#%d", i);
		seq_printf(file, "%10s", ch_name);
	}
	seq_printf(file, "\n");

	/* For each buffer pool, print out its BPID, the number of buffers in
	 * that buffer pool and the channels which are using it.
	 */
	for (i = 0; i < priv->num_bps; i++) {
		bp = priv->bp[i];

		err = dpaa2_io_query_bp_count(NULL, bp->bpid, &buf_cnt);
		if (err) {
			netdev_warn(priv->net_dev, "Buffer count query error %d\n", err);
			return err;
		}

		seq_printf(file, "%3s%d%10d%15d", "BP#", i, bp->bpid, buf_cnt);
		for (j = 0; j < num_queues; j++) {
			if (priv->channel[j]->bp == bp)
				seq_printf(file, "%10s", "x");
			else
				seq_printf(file, "%10s", "");
		}
		seq_printf(file, "\n");
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(dpaa2_dbg_bp);

void dpaa2_dbg_add(struct dpaa2_eth_priv *priv)
{
	struct fsl_mc_device *dpni_dev;
	struct dentry *dir;
	char name[10];

	/* Create a directory for the interface */
	dpni_dev = to_fsl_mc_device(priv->net_dev->dev.parent);
	snprintf(name, 10, "dpni.%d", dpni_dev->obj_desc.id);
	dir = debugfs_create_dir(name, dpaa2_dbg_root);
	priv->dbg.dir = dir;

	/* per-cpu stats file */
	debugfs_create_file("cpu_stats", 0444, dir, priv, &dpaa2_dbg_cpu_fops);

	/* per-fq stats file */
	debugfs_create_file("fq_stats", 0444, dir, priv, &dpaa2_dbg_fqs_fops);

	/* per-fq stats file */
	debugfs_create_file("ch_stats", 0444, dir, priv, &dpaa2_dbg_ch_fops);

	/* per buffer pool stats file */
	debugfs_create_file("bp_stats", 0444, dir, priv, &dpaa2_dbg_bp_fops);

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
