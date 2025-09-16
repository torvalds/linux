/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 */

#ifndef __ENA_DEBUGFS_H__
#define __ENA_DEBUGFS_H__

#include <linux/debugfs.h>
#include <linux/netdevice.h>
#include "ena_netdev.h"

#ifdef CONFIG_DEBUG_FS

void ena_debugfs_init(struct net_device *dev);

void ena_debugfs_terminate(struct net_device *dev);

#else /* CONFIG_DEBUG_FS */

static inline void ena_debugfs_init(struct net_device *dev) {}

static inline void ena_debugfs_terminate(struct net_device *dev) {}

#endif /* CONFIG_DEBUG_FS */

#endif /* __ENA_DEBUGFS_H__ */
