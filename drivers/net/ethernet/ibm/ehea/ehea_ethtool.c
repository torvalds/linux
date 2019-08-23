// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  linux/drivers/net/ethernet/ibm/ehea/ehea_ethtool.c
 *
 *  eHEA ethernet device driver for IBM eServer System p
 *
 *  (C) Copyright IBM Corp. 2006
 *
 *  Authors:
 *       Christoph Raisch <raisch@de.ibm.com>
 *       Jan-Bernd Themann <themann@de.ibm.com>
 *       Thomas Klein <tklein@de.ibm.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "ehea.h"
#include "ehea_phyp.h"

static int ehea_get_link_ksettings(struct net_device *dev,
				   struct ethtool_link_ksettings *cmd)
{
	struct ehea_port *port = netdev_priv(dev);
	u32 supported, advertising;
	u32 speed;
	int ret;

	ret = ehea_sense_port_attr(port);

	if (ret)
		return ret;

	if (netif_carrier_ok(dev)) {
		switch (port->port_speed) {
		case EHEA_SPEED_10M:
			speed = SPEED_10;
			break;
		case EHEA_SPEED_100M:
			speed = SPEED_100;
			break;
		case EHEA_SPEED_1G:
			speed = SPEED_1000;
			break;
		case EHEA_SPEED_10G:
			speed = SPEED_10000;
			break;
		default:
			speed = -1;
			break; /* BUG */
		}
		cmd->base.duplex = port->full_duplex == 1 ?
						     DUPLEX_FULL : DUPLEX_HALF;
	} else {
		speed = SPEED_UNKNOWN;
		cmd->base.duplex = DUPLEX_UNKNOWN;
	}
	cmd->base.speed = speed;

	if (cmd->base.speed == SPEED_10000) {
		supported = (SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE);
		advertising = (ADVERTISED_10000baseT_Full | ADVERTISED_FIBRE);
		cmd->base.port = PORT_FIBRE;
	} else {
		supported = (SUPPORTED_1000baseT_Full | SUPPORTED_100baseT_Full
			       | SUPPORTED_100baseT_Half | SUPPORTED_10baseT_Full
			       | SUPPORTED_10baseT_Half | SUPPORTED_Autoneg
			       | SUPPORTED_TP);
		advertising = (ADVERTISED_1000baseT_Full | ADVERTISED_Autoneg
				 | ADVERTISED_TP);
		cmd->base.port = PORT_TP;
	}

	cmd->base.autoneg = port->autoneg == 1 ?
		AUTONEG_ENABLE : AUTONEG_DISABLE;

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);

	return 0;
}

static int ehea_set_link_ksettings(struct net_device *dev,
				   const struct ethtool_link_ksettings *cmd)
{
	struct ehea_port *port = netdev_priv(dev);
	int ret = 0;
	u32 sp;

	if (cmd->base.autoneg == AUTONEG_ENABLE) {
		sp = EHEA_SPEED_AUTONEG;
		goto doit;
	}

	switch (cmd->base.speed) {
	case SPEED_10:
		if (cmd->base.duplex == DUPLEX_FULL)
			sp = H_SPEED_10M_F;
		else
			sp = H_SPEED_10M_H;
		break;

	case SPEED_100:
		if (cmd->base.duplex == DUPLEX_FULL)
			sp = H_SPEED_100M_F;
		else
			sp = H_SPEED_100M_H;
		break;

	case SPEED_1000:
		if (cmd->base.duplex == DUPLEX_FULL)
			sp = H_SPEED_1G_F;
		else
			ret = -EINVAL;
		break;

	case SPEED_10000:
		if (cmd->base.duplex == DUPLEX_FULL)
			sp = H_SPEED_10G_F;
		else
			ret = -EINVAL;
		break;

	default:
			ret = -EINVAL;
		break;
	}

	if (ret)
		goto out;
doit:
	ret = ehea_set_portspeed(port, sp);

	if (!ret)
		netdev_info(dev,
			    "Port speed successfully set: %dMbps %s Duplex\n",
			    port->port_speed,
			    port->full_duplex == 1 ? "Full" : "Half");
out:
	return ret;
}

static int ehea_nway_reset(struct net_device *dev)
{
	struct ehea_port *port = netdev_priv(dev);
	int ret;

	ret = ehea_set_portspeed(port, EHEA_SPEED_AUTONEG);

	if (!ret)
		netdev_info(port->netdev,
			    "Port speed successfully set: %dMbps %s Duplex\n",
			    port->port_speed,
			    port->full_duplex == 1 ? "Full" : "Half");
	return ret;
}

static void ehea_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
}

static u32 ehea_get_msglevel(struct net_device *dev)
{
	struct ehea_port *port = netdev_priv(dev);
	return port->msg_enable;
}

static void ehea_set_msglevel(struct net_device *dev, u32 value)
{
	struct ehea_port *port = netdev_priv(dev);
	port->msg_enable = value;
}

static const char ehea_ethtool_stats_keys[][ETH_GSTRING_LEN] = {
	{"sig_comp_iv"},
	{"swqe_refill_th"},
	{"port resets"},
	{"Receive errors"},
	{"TCP cksum errors"},
	{"IP cksum errors"},
	{"Frame cksum errors"},
	{"num SQ stopped"},
	{"PR0 free_swqes"},
	{"PR1 free_swqes"},
	{"PR2 free_swqes"},
	{"PR3 free_swqes"},
	{"PR4 free_swqes"},
	{"PR5 free_swqes"},
	{"PR6 free_swqes"},
	{"PR7 free_swqes"},
	{"PR8 free_swqes"},
	{"PR9 free_swqes"},
	{"PR10 free_swqes"},
	{"PR11 free_swqes"},
	{"PR12 free_swqes"},
	{"PR13 free_swqes"},
	{"PR14 free_swqes"},
	{"PR15 free_swqes"},
};

static void ehea_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	if (stringset == ETH_SS_STATS) {
		memcpy(data, &ehea_ethtool_stats_keys,
		       sizeof(ehea_ethtool_stats_keys));
	}
}

static int ehea_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(ehea_ethtool_stats_keys);
	default:
		return -EOPNOTSUPP;
	}
}

static void ehea_get_ethtool_stats(struct net_device *dev,
				     struct ethtool_stats *stats, u64 *data)
{
	int i, k, tmp;
	struct ehea_port *port = netdev_priv(dev);

	for (i = 0; i < ehea_get_sset_count(dev, ETH_SS_STATS); i++)
		data[i] = 0;
	i = 0;

	data[i++] = port->sig_comp_iv;
	data[i++] = port->port_res[0].swqe_refill_th;
	data[i++] = port->resets;

	for (k = 0, tmp = 0; k < EHEA_MAX_PORT_RES; k++)
		tmp += port->port_res[k].p_stats.poll_receive_errors;
	data[i++] = tmp;

	for (k = 0, tmp = 0; k < EHEA_MAX_PORT_RES; k++)
		tmp += port->port_res[k].p_stats.err_tcp_cksum;
	data[i++] = tmp;

	for (k = 0, tmp = 0; k < EHEA_MAX_PORT_RES; k++)
		tmp += port->port_res[k].p_stats.err_ip_cksum;
	data[i++] = tmp;

	for (k = 0, tmp = 0; k < EHEA_MAX_PORT_RES; k++)
		tmp += port->port_res[k].p_stats.err_frame_crc;
	data[i++] = tmp;

	for (k = 0, tmp = 0; k < EHEA_MAX_PORT_RES; k++)
		tmp += port->port_res[k].p_stats.queue_stopped;
	data[i++] = tmp;

	for (k = 0; k < 16; k++)
		data[i++] = atomic_read(&port->port_res[k].swqe_avail);
}

static const struct ethtool_ops ehea_ethtool_ops = {
	.get_drvinfo = ehea_get_drvinfo,
	.get_msglevel = ehea_get_msglevel,
	.set_msglevel = ehea_set_msglevel,
	.get_link = ethtool_op_get_link,
	.get_strings = ehea_get_strings,
	.get_sset_count = ehea_get_sset_count,
	.get_ethtool_stats = ehea_get_ethtool_stats,
	.nway_reset = ehea_nway_reset,		/* Restart autonegotiation */
	.get_link_ksettings = ehea_get_link_ksettings,
	.set_link_ksettings = ehea_set_link_ksettings,
};

void ehea_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &ehea_ethtool_ops;
}
