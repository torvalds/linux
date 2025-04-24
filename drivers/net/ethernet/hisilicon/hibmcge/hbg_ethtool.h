/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2024 Hisilicon Limited. */

#ifndef __HBG_ETHTOOL_H
#define __HBG_ETHTOOL_H

#include <linux/netdevice.h>

#define HBG_STATS_FIELD_OFF(f) (offsetof(struct hbg_stats, f))
#define HBG_STATS_R(p, offset) (*(u64 *)((u8 *)(p) + (offset)))
#define HBG_STATS_U(p, offset, val) (HBG_STATS_R(p, offset) += (val))

void hbg_ethtool_set_ops(struct net_device *netdev);
void hbg_update_stats(struct hbg_priv *priv);

#endif
