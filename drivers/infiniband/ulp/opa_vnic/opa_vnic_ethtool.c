/*
 * Copyright(c) 2017 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * This file contains OPA VNIC ethtool functions
 */

#include <linux/ethtool.h>

#include "opa_vnic_internal.h"

enum {NETDEV_STATS, VNIC_STATS};

struct vnic_stats {
	char stat_string[ETH_GSTRING_LEN];
	struct {
		int sizeof_stat;
		int stat_offset;
	};
};

#define VNIC_STAT(m)            { sizeof_field(struct opa_vnic_stats, m),   \
				  offsetof(struct opa_vnic_stats, m) }

static struct vnic_stats vnic_gstrings_stats[] = {
	/* NETDEV stats */
	{"rx_packets", VNIC_STAT(netstats.rx_packets)},
	{"tx_packets", VNIC_STAT(netstats.tx_packets)},
	{"rx_bytes", VNIC_STAT(netstats.rx_bytes)},
	{"tx_bytes", VNIC_STAT(netstats.tx_bytes)},
	{"rx_errors", VNIC_STAT(netstats.rx_errors)},
	{"tx_errors", VNIC_STAT(netstats.tx_errors)},
	{"rx_dropped", VNIC_STAT(netstats.rx_dropped)},
	{"tx_dropped", VNIC_STAT(netstats.tx_dropped)},

	/* SUMMARY counters */
	{"tx_unicast", VNIC_STAT(tx_grp.unicast)},
	{"tx_mcastbcast", VNIC_STAT(tx_grp.mcastbcast)},
	{"tx_untagged", VNIC_STAT(tx_grp.untagged)},
	{"tx_vlan", VNIC_STAT(tx_grp.vlan)},

	{"tx_64_size", VNIC_STAT(tx_grp.s_64)},
	{"tx_65_127", VNIC_STAT(tx_grp.s_65_127)},
	{"tx_128_255", VNIC_STAT(tx_grp.s_128_255)},
	{"tx_256_511", VNIC_STAT(tx_grp.s_256_511)},
	{"tx_512_1023", VNIC_STAT(tx_grp.s_512_1023)},
	{"tx_1024_1518", VNIC_STAT(tx_grp.s_1024_1518)},
	{"tx_1519_max", VNIC_STAT(tx_grp.s_1519_max)},

	{"rx_unicast", VNIC_STAT(rx_grp.unicast)},
	{"rx_mcastbcast", VNIC_STAT(rx_grp.mcastbcast)},
	{"rx_untagged", VNIC_STAT(rx_grp.untagged)},
	{"rx_vlan", VNIC_STAT(rx_grp.vlan)},

	{"rx_64_size", VNIC_STAT(rx_grp.s_64)},
	{"rx_65_127", VNIC_STAT(rx_grp.s_65_127)},
	{"rx_128_255", VNIC_STAT(rx_grp.s_128_255)},
	{"rx_256_511", VNIC_STAT(rx_grp.s_256_511)},
	{"rx_512_1023", VNIC_STAT(rx_grp.s_512_1023)},
	{"rx_1024_1518", VNIC_STAT(rx_grp.s_1024_1518)},
	{"rx_1519_max", VNIC_STAT(rx_grp.s_1519_max)},

	/* ERROR counters */
	{"rx_fifo_errors", VNIC_STAT(netstats.rx_fifo_errors)},
	{"rx_length_errors", VNIC_STAT(netstats.rx_length_errors)},

	{"tx_fifo_errors", VNIC_STAT(netstats.tx_fifo_errors)},
	{"tx_carrier_errors", VNIC_STAT(netstats.tx_carrier_errors)},

	{"tx_dlid_zero", VNIC_STAT(tx_dlid_zero)},
	{"tx_drop_state", VNIC_STAT(tx_drop_state)},
	{"rx_drop_state", VNIC_STAT(rx_drop_state)},
	{"rx_oversize", VNIC_STAT(rx_oversize)},
	{"rx_runt", VNIC_STAT(rx_runt)},
};

#define VNIC_STATS_LEN  ARRAY_SIZE(vnic_gstrings_stats)

/* vnic_get_drvinfo - get driver info */
static void vnic_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, opa_vnic_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, opa_vnic_driver_version,
		sizeof(drvinfo->version));
	strlcpy(drvinfo->bus_info, dev_name(netdev->dev.parent),
		sizeof(drvinfo->bus_info));
}

/* vnic_get_sset_count - get string set count */
static int vnic_get_sset_count(struct net_device *netdev, int sset)
{
	return (sset == ETH_SS_STATS) ? VNIC_STATS_LEN : -EOPNOTSUPP;
}

/* vnic_get_ethtool_stats - get statistics */
static void vnic_get_ethtool_stats(struct net_device *netdev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct opa_vnic_adapter *adapter = opa_vnic_priv(netdev);
	struct opa_vnic_stats vstats;
	int i;

	memset(&vstats, 0, sizeof(vstats));
	spin_lock(&adapter->stats_lock);
	adapter->rn_ops->ndo_get_stats64(netdev, &vstats.netstats);
	spin_unlock(&adapter->stats_lock);
	for (i = 0; i < VNIC_STATS_LEN; i++) {
		char *p = (char *)&vstats + vnic_gstrings_stats[i].stat_offset;

		data[i] = (vnic_gstrings_stats[i].sizeof_stat ==
			   sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}
}

/* vnic_get_strings - get strings */
static void vnic_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < VNIC_STATS_LEN; i++)
		memcpy(data + i * ETH_GSTRING_LEN,
		       vnic_gstrings_stats[i].stat_string,
		       ETH_GSTRING_LEN);
}

/* ethtool ops */
static const struct ethtool_ops opa_vnic_ethtool_ops = {
	.get_drvinfo = vnic_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_strings = vnic_get_strings,
	.get_sset_count = vnic_get_sset_count,
	.get_ethtool_stats = vnic_get_ethtool_stats,
};

/* opa_vnic_set_ethtool_ops - set ethtool ops */
void opa_vnic_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &opa_vnic_ethtool_ops;
}
