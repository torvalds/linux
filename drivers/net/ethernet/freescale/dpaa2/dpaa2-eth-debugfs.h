/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2015 Freescale Semiconductor Inc.
 * Copyright 2018-2019 NXP
 */
#ifndef DPAA2_ETH_DEBUGFS_H
#define DPAA2_ETH_DEBUGFS_H

#include <linux/dcache.h>

struct dpaa2_eth_priv;

struct dpaa2_debugfs {
	struct dentry *dir;
	struct dentry *fq_stats;
	struct dentry *ch_stats;
	struct dentry *cpu_stats;
};

#ifdef CONFIG_DEBUG_FS
void dpaa2_eth_dbg_init(void);
void dpaa2_eth_dbg_exit(void);
void dpaa2_dbg_add(struct dpaa2_eth_priv *priv);
void dpaa2_dbg_remove(struct dpaa2_eth_priv *priv);
#else
static inline void dpaa2_eth_dbg_init(void) {}
static inline void dpaa2_eth_dbg_exit(void) {}
static inline void dpaa2_dbg_add(struct dpaa2_eth_priv *priv) {}
static inline void dpaa2_dbg_remove(struct dpaa2_eth_priv *priv) {}
#endif /* CONFIG_DEBUG_FS */

#endif /* DPAA2_ETH_DEBUGFS_H */
