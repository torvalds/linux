/* 10G controller driver for Samsung SoCs
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Siva Reddy Kallam <siva.kallam@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/phy.h>

#include "sxgbe_common.h"

struct sxgbe_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define SXGBE_STAT(m)						\
{								\
	#m,							\
	FIELD_SIZEOF(struct sxgbe_extra_stats, m),		\
	offsetof(struct sxgbe_priv_data, xstats.m)		\
}

static const struct sxgbe_stats sxgbe_gstrings_stats[] = {
};
#define SXGBE_STATS_LEN ARRAY_SIZE(sxgbe_gstrings_stats)

static const struct ethtool_ops sxgbe_ethtool_ops = {
};

void sxgbe_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &sxgbe_ethtool_ops);
}
