// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 */

#ifdef CONFIG_DEBUG_FS

#include <linux/seq_file.h>
#include <linux/pci.h>
#include "ena_debugfs.h"

void ena_debugfs_init(struct net_device *dev)
{
	struct ena_adapter *adapter = netdev_priv(dev);

	adapter->debugfs_base =
		debugfs_create_dir(dev_name(&adapter->pdev->dev), NULL);
}

void ena_debugfs_terminate(struct net_device *dev)
{
	struct ena_adapter *adapter = netdev_priv(dev);

	debugfs_remove_recursive(adapter->debugfs_base);
}

#endif /* CONFIG_DEBUG_FS */
