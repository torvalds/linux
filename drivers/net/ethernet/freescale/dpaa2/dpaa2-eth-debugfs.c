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
		   "Tx SG", "Tx realloc", "Enq busy");

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
			   extras->tx_reallocs,
			   extras->tx_portal_busy);
	}

	return 0;
}

static int dpaa2_dbg_cpu_open(struct inode *inode, struct file *file)
{
	int err;
	struct dpaa2_eth_priv *priv = (struct dpaa2_eth_priv *)inode->i_private;

	err = single_open(file, dpaa2_dbg_cpu_show, priv);
	if (err < 0)
		netdev_err(priv->net_dev, "single_open() failed\n");

	return err;
}

static const struct file_operations dpaa2_dbg_cpu_ops = {
	.open = dpaa2_dbg_cpu_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

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
	seq_printf(file, "%s%16s%16s%16s%16s\n",
		   "VFQID", "CPU", "Type", "Frames", "Pending frames");

	for (i = 0; i <  priv->num_fqs; i++) {
		fq = &priv->fq[i];
		err = dpaa2_io_query_fq_count(NULL, fq->fqid, &fcnt, &bcnt);
		if (err)
			fcnt = 0;

		seq_printf(file, "%5d%16d%16s%16llu%16u\n",
			   fq->fqid,
			   fq->target_cpu,
			   fq_type_to_str(fq),
			   fq->stats.frames,
			   fcnt);
	}

	return 0;
}

static int dpaa2_dbg_fqs_open(struct inode *inode, struct file *file)
{
	int err;
	struct dpaa2_eth_priv *priv = (struct dpaa2_eth_priv *)inode->i_private;

	err = single_open(file, dpaa2_dbg_fqs_show, priv);
	if (err < 0)
		netdev_err(priv->net_dev, "single_open() failed\n");

	return err;
}

static const struct file_operations dpaa2_dbg_fq_ops = {
	.open = dpaa2_dbg_fqs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int dpaa2_dbg_ch_show(struct seq_file *file, void *offset)
{
	struct dpaa2_eth_priv *priv = (struct dpaa2_eth_priv *)file->private;
	struct dpaa2_eth_channel *ch;
	int i;

	seq_printf(file, "Channel stats for %s:\n", priv->net_dev->name);
	seq_printf(file, "%s%16s%16s%16s%16s\n",
		   "CHID", "CPU", "Deq busy", "CDANs", "Buf count");

	for (i = 0; i < priv->num_channels; i++) {
		ch = priv->channel[i];
		seq_printf(file, "%4d%16d%16llu%16llu%16d\n",
			   ch->ch_id,
			   ch->nctx.desired_cpu,
			   ch->stats.dequeue_portal_busy,
			   ch->stats.cdan,
			   ch->buf_count);
	}

	return 0;
}

static int dpaa2_dbg_ch_open(struct inode *inode, struct file *file)
{
	int err;
	struct dpaa2_eth_priv *priv = (struct dpaa2_eth_priv *)inode->i_private;

	err = single_open(file, dpaa2_dbg_ch_show, priv);
	if (err < 0)
		netdev_err(priv->net_dev, "single_open() failed\n");

	return err;
}

static const struct file_operations dpaa2_dbg_ch_ops = {
	.open = dpaa2_dbg_ch_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void dpaa2_dbg_add(struct dpaa2_eth_priv *priv)
{
	if (!dpaa2_dbg_root)
		return;

	/* Create a directory for the interface */
	priv->dbg.dir = debugfs_create_dir(priv->net_dev->name,
					   dpaa2_dbg_root);
	if (!priv->dbg.dir) {
		netdev_err(priv->net_dev, "debugfs_create_dir() failed\n");
		return;
	}

	/* per-cpu stats file */
	priv->dbg.cpu_stats = debugfs_create_file("cpu_stats", 0444,
						  priv->dbg.dir, priv,
						  &dpaa2_dbg_cpu_ops);
	if (!priv->dbg.cpu_stats) {
		netdev_err(priv->net_dev, "debugfs_create_file() failed\n");
		goto err_cpu_stats;
	}

	/* per-fq stats file */
	priv->dbg.fq_stats = debugfs_create_file("fq_stats", 0444,
						 priv->dbg.dir, priv,
						 &dpaa2_dbg_fq_ops);
	if (!priv->dbg.fq_stats) {
		netdev_err(priv->net_dev, "debugfs_create_file() failed\n");
		goto err_fq_stats;
	}

	/* per-fq stats file */
	priv->dbg.ch_stats = debugfs_create_file("ch_stats", 0444,
						 priv->dbg.dir, priv,
						 &dpaa2_dbg_ch_ops);
	if (!priv->dbg.fq_stats) {
		netdev_err(priv->net_dev, "debugfs_create_file() failed\n");
		goto err_ch_stats;
	}

	return;

err_ch_stats:
	debugfs_remove(priv->dbg.fq_stats);
err_fq_stats:
	debugfs_remove(priv->dbg.cpu_stats);
err_cpu_stats:
	debugfs_remove(priv->dbg.dir);
}

void dpaa2_dbg_remove(struct dpaa2_eth_priv *priv)
{
	debugfs_remove(priv->dbg.fq_stats);
	debugfs_remove(priv->dbg.ch_stats);
	debugfs_remove(priv->dbg.cpu_stats);
	debugfs_remove(priv->dbg.dir);
}

void dpaa2_eth_dbg_init(void)
{
	dpaa2_dbg_root = debugfs_create_dir(DPAA2_ETH_DBG_ROOT, NULL);
	if (!dpaa2_dbg_root) {
		pr_err("DPAA2-ETH: debugfs create failed\n");
		return;
	}

	pr_debug("DPAA2-ETH: debugfs created\n");
}

void dpaa2_eth_dbg_exit(void)
{
	debugfs_remove(dpaa2_dbg_root);
}
