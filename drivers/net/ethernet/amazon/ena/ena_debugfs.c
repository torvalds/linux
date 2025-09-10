// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 */

#ifdef CONFIG_DEBUG_FS

#include <linux/seq_file.h>
#include <linux/pci.h>
#include "ena_debugfs.h"
#include "ena_phc.h"

static int phc_stats_show(struct seq_file *file, void *priv)
{
	struct ena_adapter *adapter = file->private;

	if (!ena_phc_is_active(adapter))
		return 0;

	seq_printf(file,
		   "phc_cnt: %llu\n",
		   adapter->ena_dev->phc.stats.phc_cnt);
	seq_printf(file,
		   "phc_exp: %llu\n",
		   adapter->ena_dev->phc.stats.phc_exp);
	seq_printf(file,
		   "phc_skp: %llu\n",
		   adapter->ena_dev->phc.stats.phc_skp);
	seq_printf(file,
		   "phc_err_dv: %llu\n",
		   adapter->ena_dev->phc.stats.phc_err_dv);
	seq_printf(file,
		   "phc_err_ts: %llu\n",
		   adapter->ena_dev->phc.stats.phc_err_ts);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(phc_stats);

void ena_debugfs_init(struct net_device *dev)
{
	struct ena_adapter *adapter = netdev_priv(dev);

	adapter->debugfs_base =
		debugfs_create_dir(dev_name(&adapter->pdev->dev), NULL);

	debugfs_create_file("phc_stats",
			    0400,
			    adapter->debugfs_base,
			    adapter,
			    &phc_stats_fops);
}

void ena_debugfs_terminate(struct net_device *dev)
{
	struct ena_adapter *adapter = netdev_priv(dev);

	debugfs_remove_recursive(adapter->debugfs_base);
}

#endif /* CONFIG_DEBUG_FS */
