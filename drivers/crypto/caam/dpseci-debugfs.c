// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2019 NXP */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include "dpseci-debugfs.h"

static int dpseci_dbg_fqs_show(struct seq_file *file, void *offset)
{
	struct dpaa2_caam_priv *priv = (struct dpaa2_caam_priv *)file->private;
	u32 fqid, fcnt, bcnt;
	int i, err;

	seq_printf(file, "FQ stats for %s:\n", dev_name(priv->dev));
	seq_printf(file, "%s%16s%16s\n",
		   "Rx-VFQID",
		   "Pending frames",
		   "Pending bytes");

	for (i = 0; i <  priv->num_pairs; i++) {
		fqid = priv->rx_queue_attr[i].fqid;
		err = dpaa2_io_query_fq_count(NULL, fqid, &fcnt, &bcnt);
		if (err)
			continue;

		seq_printf(file, "%5d%16u%16u\n", fqid, fcnt, bcnt);
	}

	seq_printf(file, "%s%16s%16s\n",
		   "Tx-VFQID",
		   "Pending frames",
		   "Pending bytes");

	for (i = 0; i <  priv->num_pairs; i++) {
		fqid = priv->tx_queue_attr[i].fqid;
		err = dpaa2_io_query_fq_count(NULL, fqid, &fcnt, &bcnt);
		if (err)
			continue;

		seq_printf(file, "%5d%16u%16u\n", fqid, fcnt, bcnt);
	}

	return 0;
}

static int dpseci_dbg_fqs_open(struct inode *inode, struct file *file)
{
	int err;
	struct dpaa2_caam_priv *priv;

	priv = (struct dpaa2_caam_priv *)inode->i_private;

	err = single_open(file, dpseci_dbg_fqs_show, priv);
	if (err < 0)
		dev_err(priv->dev, "single_open() failed\n");

	return err;
}

static const struct file_operations dpseci_dbg_fq_ops = {
	.open = dpseci_dbg_fqs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void dpaa2_dpseci_debugfs_init(struct dpaa2_caam_priv *priv)
{
	priv->dfs_root = debugfs_create_dir(dev_name(priv->dev), NULL);

	debugfs_create_file("fq_stats", 0444, priv->dfs_root, priv,
			    &dpseci_dbg_fq_ops);
}

void dpaa2_dpseci_debugfs_exit(struct dpaa2_caam_priv *priv)
{
	debugfs_remove_recursive(priv->dfs_root);
}
