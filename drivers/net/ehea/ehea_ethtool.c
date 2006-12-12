/*
 *  linux/drivers/net/ehea/ehea_ethtool.c
 *
 *  eHEA ethernet device driver for IBM eServer System p
 *
 *  (C) Copyright IBM Corp. 2006
 *
 *  Authors:
 *       Christoph Raisch <raisch@de.ibm.com>
 *       Jan-Bernd Themann <themann@de.ibm.com>
 *       Thomas Klein <tklein@de.ibm.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "ehea.h"
#include "ehea_phyp.h"

static int ehea_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct ehea_port *port = netdev_priv(dev);
	int ret;

	ret = ehea_sense_port_attr(port);

	if (ret)
		return ret;

	if (netif_carrier_ok(dev)) {
		switch(port->port_speed) {
		case EHEA_SPEED_10M: cmd->speed = SPEED_10; break;
		case EHEA_SPEED_100M: cmd->speed = SPEED_100; break;
		case EHEA_SPEED_1G: cmd->speed = SPEED_1000; break;
		case EHEA_SPEED_10G: cmd->speed = SPEED_10000; break;
		}
		cmd->duplex = port->full_duplex == 1 ?
						     DUPLEX_FULL : DUPLEX_HALF;
	} else {
		cmd->speed = -1;
		cmd->duplex = -1;
	}

	cmd->supported = (SUPPORTED_10000baseT_Full | SUPPORTED_1000baseT_Full
		       | SUPPORTED_100baseT_Full |  SUPPORTED_100baseT_Half
		       | SUPPORTED_10baseT_Full | SUPPORTED_10baseT_Half
		       | SUPPORTED_Autoneg | SUPPORTED_FIBRE);

	cmd->advertising = (ADVERTISED_10000baseT_Full | ADVERTISED_Autoneg
			 | ADVERTISED_FIBRE);

	cmd->port = PORT_FIBRE;
	cmd->autoneg = port->autoneg == 1 ? AUTONEG_ENABLE : AUTONEG_DISABLE;

	return 0;
}

static int ehea_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct ehea_port *port = netdev_priv(dev);
	int ret = 0;
	u32 sp;

	if (cmd->autoneg == AUTONEG_ENABLE) {
		sp = EHEA_SPEED_AUTONEG;
		goto doit;
	}

	switch(cmd->speed) {
	case SPEED_10:
		if (cmd->duplex == DUPLEX_FULL)
			sp = H_SPEED_10M_F;
		else
			sp = H_SPEED_10M_H;
		break;

	case SPEED_100:
		if (cmd->duplex == DUPLEX_FULL)
			sp = H_SPEED_100M_F;
		else
			sp = H_SPEED_100M_H;
		break;

	case SPEED_1000:
		if (cmd->duplex == DUPLEX_FULL)
			sp = H_SPEED_1G_F;
		else
			ret = -EINVAL;
		break;

	case SPEED_10000:
		if (cmd->duplex == DUPLEX_FULL)
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
		ehea_info("%s: Port speed succesfully set: %dMbps "
			  "%s Duplex",
			  port->netdev->name, port->port_speed,
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
		ehea_info("%s: Port speed succesfully set: %dMbps "
			  "%s Duplex",
			  port->netdev->name, port->port_speed,
			  port->full_duplex == 1 ? "Full" : "Half");
	return ret;
}

static void ehea_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver) - 1);
	strlcpy(info->version, DRV_VERSION, sizeof(info->version) - 1);
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

static u32 ehea_get_rx_csum(struct net_device *dev)
{
	return 1;
}

static char ehea_ethtool_stats_keys[][ETH_GSTRING_LEN] = {
	{"poll_max_processed"},
	{"queue_stopped"},
	{"min_swqe_avail"},
	{"poll_receive_err"},
	{"pkt_send"},
	{"pkt_xmit"},
	{"send_tasklet"},
	{"ehea_poll"},
	{"nwqe"},
	{"swqe_available_0"},
	{"sig_comp_iv"},
	{"swqe_refill_th"},
	{"port resets"},
	{"rxo"},
	{"rx64"},
	{"rx65"},
	{"rx128"},
	{"rx256"},
	{"rx512"},
	{"rx1024"},
	{"txo"},
	{"tx64"},
	{"tx65"},
	{"tx128"},
	{"tx256"},
	{"tx512"},
	{"tx1024"},
};

static void ehea_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	if (stringset == ETH_SS_STATS) {
		memcpy(data, &ehea_ethtool_stats_keys,
		       sizeof(ehea_ethtool_stats_keys));
	}
}

static int ehea_get_stats_count(struct net_device *dev)
{
	return ARRAY_SIZE(ehea_ethtool_stats_keys);
}

static void ehea_get_ethtool_stats(struct net_device *dev,
				     struct ethtool_stats *stats, u64 *data)
{
	u64 hret;
	int i;
	struct ehea_port *port = netdev_priv(dev);
	struct ehea_adapter *adapter = port->adapter;
	struct ehea_port_res *pr = &port->port_res[0];
	struct port_state *p_state = &pr->p_state;
	struct hcp_ehea_port_cb6 *cb6;

	for (i = 0; i < ehea_get_stats_count(dev); i++)
		data[i] = 0;

	i = 0;

	data[i++] = p_state->poll_max_processed;
	data[i++] = p_state->queue_stopped;
	data[i++] = p_state->min_swqe_avail;
	data[i++] = p_state->poll_receive_errors;
	data[i++] = p_state->pkt_send;
	data[i++] = p_state->pkt_xmit;
	data[i++] = p_state->send_tasklet;
	data[i++] = p_state->ehea_poll;
	data[i++] = p_state->nwqe;
	data[i++] = atomic_read(&port->port_res[0].swqe_avail);
	data[i++] = port->sig_comp_iv;
	data[i++] = port->port_res[0].swqe_refill_th;
	data[i++] = port->resets;

	cb6 = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!cb6) {
		ehea_error("no mem for cb6");
		return;
	}

	hret = ehea_h_query_ehea_port(adapter->handle, port->logical_port_id,
				      H_PORT_CB6, H_PORT_CB6_ALL, cb6);
	if (netif_msg_hw(port))
		ehea_dump(cb6, sizeof(*cb6), "ehea_get_ethtool_stats");

	if (hret == H_SUCCESS) {
		data[i++] = cb6->rxo;
		data[i++] = cb6->rx64;
		data[i++] = cb6->rx65;
		data[i++] = cb6->rx128;
		data[i++] = cb6->rx256;
		data[i++] = cb6->rx512;
		data[i++] = cb6->rx1024;
		data[i++] = cb6->txo;
		data[i++] = cb6->tx64;
		data[i++] = cb6->tx65;
		data[i++] = cb6->tx128;
		data[i++] = cb6->tx256;
		data[i++] = cb6->tx512;
		data[i++] = cb6->tx1024;
	} else
		ehea_error("query_ehea_port failed");

	kfree(cb6);
}

const struct ethtool_ops ehea_ethtool_ops = {
	.get_settings = ehea_get_settings,
	.get_drvinfo = ehea_get_drvinfo,
	.get_msglevel = ehea_get_msglevel,
	.set_msglevel = ehea_set_msglevel,
	.get_link = ethtool_op_get_link,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.get_sg = ethtool_op_get_sg,
	.get_tso = ethtool_op_get_tso,
	.set_tso = ethtool_op_set_tso,
	.get_strings = ehea_get_strings,
	.get_stats_count = ehea_get_stats_count,
	.get_ethtool_stats = ehea_get_ethtool_stats,
	.get_rx_csum = ehea_get_rx_csum,
	.set_settings = ehea_set_settings,
	.nway_reset = ehea_nway_reset,		/* Restart autonegotiation */
};

void ehea_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &ehea_ethtool_ops);
}
