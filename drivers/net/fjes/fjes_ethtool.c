/*
 *  FUJITSU Extended Socket Network Device driver
 *  Copyright (c) 2015 FUJITSU LIMITED
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 */

/* ethtool support for fjes */

#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/platform_device.h>

#include "fjes.h"

struct fjes_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define FJES_STAT(name, stat) { \
	.stat_string = name, \
	.sizeof_stat = FIELD_SIZEOF(struct fjes_adapter, stat), \
	.stat_offset = offsetof(struct fjes_adapter, stat) \
}

static const struct fjes_stats fjes_gstrings_stats[] = {
	FJES_STAT("rx_packets", stats64.rx_packets),
	FJES_STAT("tx_packets", stats64.tx_packets),
	FJES_STAT("rx_bytes", stats64.rx_bytes),
	FJES_STAT("tx_bytes", stats64.rx_bytes),
	FJES_STAT("rx_dropped", stats64.rx_dropped),
	FJES_STAT("tx_dropped", stats64.tx_dropped),
};

static void fjes_get_ethtool_stats(struct net_device *netdev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	char *p;
	int i;

	for (i = 0; i < ARRAY_SIZE(fjes_gstrings_stats); i++) {
		p = (char *)adapter + fjes_gstrings_stats[i].stat_offset;
		data[i] = (fjes_gstrings_stats[i].sizeof_stat == sizeof(u64))
			? *(u64 *)p : *(u32 *)p;
	}
}

static void fjes_get_strings(struct net_device *netdev,
			     u32 stringset, u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(fjes_gstrings_stats); i++) {
			memcpy(p, fjes_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static int fjes_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(fjes_gstrings_stats);
	default:
		return -EOPNOTSUPP;
	}
}

static void fjes_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *drvinfo)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	struct platform_device *plat_dev;

	plat_dev = adapter->plat_dev;

	strlcpy(drvinfo->driver, fjes_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, fjes_driver_version,
		sizeof(drvinfo->version));

	strlcpy(drvinfo->fw_version, "none", sizeof(drvinfo->fw_version));
	snprintf(drvinfo->bus_info, sizeof(drvinfo->bus_info),
		 "platform:%s", plat_dev->name);
	drvinfo->regdump_len = 0;
	drvinfo->eedump_len = 0;
}

static int fjes_get_settings(struct net_device *netdev,
			     struct ethtool_cmd *ecmd)
{
	ecmd->supported = 0;
	ecmd->advertising = 0;
	ecmd->duplex = DUPLEX_FULL;
	ecmd->autoneg = AUTONEG_DISABLE;
	ecmd->transceiver = XCVR_DUMMY1;
	ecmd->port = PORT_NONE;
	ethtool_cmd_speed_set(ecmd, 20000);	/* 20Gb/s */

	return 0;
}

static const struct ethtool_ops fjes_ethtool_ops = {
		.get_settings		= fjes_get_settings,
		.get_drvinfo		= fjes_get_drvinfo,
		.get_ethtool_stats = fjes_get_ethtool_stats,
		.get_strings      = fjes_get_strings,
		.get_sset_count   = fjes_get_sset_count,
};

void fjes_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &fjes_ethtool_ops;
}
