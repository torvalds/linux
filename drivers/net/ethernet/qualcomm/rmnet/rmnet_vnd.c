// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2013-2018, The Linux Foundation. All rights reserved.
 *
 * RMNET Data virtual network driver
 */

#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_arp.h>
#include <net/pkt_sched.h>
#include "rmnet_config.h"
#include "rmnet_handlers.h"
#include "rmnet_private.h"
#include "rmnet_map.h"
#include "rmnet_vnd.h"

/* RX/TX Fixup */

void rmnet_vnd_rx_fixup(struct sk_buff *skb, struct net_device *dev)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	struct rmnet_pcpu_stats *pcpu_ptr;

	pcpu_ptr = this_cpu_ptr(priv->pcpu_stats);

	u64_stats_update_begin(&pcpu_ptr->syncp);
	pcpu_ptr->stats.rx_pkts++;
	pcpu_ptr->stats.rx_bytes += skb->len;
	u64_stats_update_end(&pcpu_ptr->syncp);
}

void rmnet_vnd_tx_fixup(struct sk_buff *skb, struct net_device *dev)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	struct rmnet_pcpu_stats *pcpu_ptr;

	pcpu_ptr = this_cpu_ptr(priv->pcpu_stats);

	u64_stats_update_begin(&pcpu_ptr->syncp);
	pcpu_ptr->stats.tx_pkts++;
	pcpu_ptr->stats.tx_bytes += skb->len;
	u64_stats_update_end(&pcpu_ptr->syncp);
}

/* Network Device Operations */

static netdev_tx_t rmnet_vnd_start_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
	struct rmnet_priv *priv;

	priv = netdev_priv(dev);
	if (priv->real_dev) {
		rmnet_egress_handler(skb);
	} else {
		this_cpu_inc(priv->pcpu_stats->stats.tx_drops);
		kfree_skb(skb);
	}
	return NETDEV_TX_OK;
}

static int rmnet_vnd_headroom(struct rmnet_port *port)
{
	u32 headroom;

	headroom = sizeof(struct rmnet_map_header);

	if (port->data_format & RMNET_FLAGS_EGRESS_MAP_CKSUMV4)
		headroom += sizeof(struct rmnet_map_ul_csum_header);

	return headroom;
}

static int rmnet_vnd_change_mtu(struct net_device *rmnet_dev, int new_mtu)
{
	struct rmnet_priv *priv = netdev_priv(rmnet_dev);
	struct rmnet_port *port;
	u32 headroom;

	port = rmnet_get_port_rtnl(priv->real_dev);

	headroom = rmnet_vnd_headroom(port);

	if (new_mtu < 0 || new_mtu > RMNET_MAX_PACKET_SIZE ||
	    new_mtu > (priv->real_dev->mtu - headroom))
		return -EINVAL;

	rmnet_dev->mtu = new_mtu;
	return 0;
}

static int rmnet_vnd_get_iflink(const struct net_device *dev)
{
	struct rmnet_priv *priv = netdev_priv(dev);

	return priv->real_dev->ifindex;
}

static int rmnet_vnd_init(struct net_device *dev)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	int err;

	priv->pcpu_stats = alloc_percpu(struct rmnet_pcpu_stats);
	if (!priv->pcpu_stats)
		return -ENOMEM;

	err = gro_cells_init(&priv->gro_cells, dev);
	if (err) {
		free_percpu(priv->pcpu_stats);
		return err;
	}

	return 0;
}

static void rmnet_vnd_uninit(struct net_device *dev)
{
	struct rmnet_priv *priv = netdev_priv(dev);

	gro_cells_destroy(&priv->gro_cells);
	free_percpu(priv->pcpu_stats);
}

static void rmnet_get_stats64(struct net_device *dev,
			      struct rtnl_link_stats64 *s)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	struct rmnet_vnd_stats total_stats = { };
	struct rmnet_pcpu_stats *pcpu_ptr;
	struct rmnet_vnd_stats snapshot;
	unsigned int cpu, start;

	for_each_possible_cpu(cpu) {
		pcpu_ptr = per_cpu_ptr(priv->pcpu_stats, cpu);

		do {
			start = u64_stats_fetch_begin_irq(&pcpu_ptr->syncp);
			snapshot = pcpu_ptr->stats;	/* struct assignment */
		} while (u64_stats_fetch_retry_irq(&pcpu_ptr->syncp, start));

		total_stats.rx_pkts += snapshot.rx_pkts;
		total_stats.rx_bytes += snapshot.rx_bytes;
		total_stats.tx_pkts += snapshot.tx_pkts;
		total_stats.tx_bytes += snapshot.tx_bytes;
		total_stats.tx_drops += snapshot.tx_drops;
	}

	s->rx_packets = total_stats.rx_pkts;
	s->rx_bytes = total_stats.rx_bytes;
	s->tx_packets = total_stats.tx_pkts;
	s->tx_bytes = total_stats.tx_bytes;
	s->tx_dropped = total_stats.tx_drops;
}

static const struct net_device_ops rmnet_vnd_ops = {
	.ndo_start_xmit = rmnet_vnd_start_xmit,
	.ndo_change_mtu = rmnet_vnd_change_mtu,
	.ndo_get_iflink = rmnet_vnd_get_iflink,
	.ndo_add_slave  = rmnet_add_bridge,
	.ndo_del_slave  = rmnet_del_bridge,
	.ndo_init       = rmnet_vnd_init,
	.ndo_uninit     = rmnet_vnd_uninit,
	.ndo_get_stats64 = rmnet_get_stats64,
};

static const char rmnet_gstrings_stats[][ETH_GSTRING_LEN] = {
	"Checksum ok",
	"Bad IPv4 header checksum",
	"Checksum valid bit not set",
	"Checksum validation failed",
	"Checksum error bad buffer",
	"Checksum error bad ip version",
	"Checksum error bad transport",
	"Checksum skipped on ip fragment",
	"Checksum skipped",
	"Checksum computed in software",
	"Checksum computed in hardware",
};

static void rmnet_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(buf, &rmnet_gstrings_stats,
		       sizeof(rmnet_gstrings_stats));
		break;
	}
}

static int rmnet_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(rmnet_gstrings_stats);
	default:
		return -EOPNOTSUPP;
	}
}

static void rmnet_get_ethtool_stats(struct net_device *dev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	struct rmnet_priv_stats *st = &priv->stats;

	if (!data)
		return;

	memcpy(data, st, ARRAY_SIZE(rmnet_gstrings_stats) * sizeof(u64));
}

static const struct ethtool_ops rmnet_ethtool_ops = {
	.get_ethtool_stats = rmnet_get_ethtool_stats,
	.get_strings = rmnet_get_strings,
	.get_sset_count = rmnet_get_sset_count,
};

/* Called by kernel whenever a new rmnet<n> device is created. Sets MTU,
 * flags, ARP type, needed headroom, etc...
 */
void rmnet_vnd_setup(struct net_device *rmnet_dev)
{
	rmnet_dev->netdev_ops = &rmnet_vnd_ops;
	rmnet_dev->mtu = RMNET_DFLT_PACKET_SIZE;
	rmnet_dev->needed_headroom = RMNET_NEEDED_HEADROOM;
	eth_hw_addr_random(rmnet_dev);
	rmnet_dev->tx_queue_len = RMNET_TX_QUEUE_LEN;

	/* Raw IP mode */
	rmnet_dev->header_ops = NULL;  /* No header */
	rmnet_dev->type = ARPHRD_RAWIP;
	rmnet_dev->hard_header_len = 0;
	rmnet_dev->flags &= ~(IFF_BROADCAST | IFF_MULTICAST);

	rmnet_dev->needs_free_netdev = true;
	rmnet_dev->ethtool_ops = &rmnet_ethtool_ops;

	rmnet_dev->features |= NETIF_F_LLTX;

	/* This perm addr will be used as interface identifier by IPv6 */
	rmnet_dev->addr_assign_type = NET_ADDR_RANDOM;
	eth_random_addr(rmnet_dev->perm_addr);
}

/* Exposed API */

int rmnet_vnd_newlink(u8 id, struct net_device *rmnet_dev,
		      struct rmnet_port *port,
		      struct net_device *real_dev,
		      struct rmnet_endpoint *ep,
		      struct netlink_ext_ack *extack)

{
	struct rmnet_priv *priv = netdev_priv(rmnet_dev);
	u32 headroom;
	int rc;

	if (rmnet_get_endpoint(port, id)) {
		NL_SET_ERR_MSG_MOD(extack, "MUX ID already exists");
		return -EBUSY;
	}

	rmnet_dev->hw_features = NETIF_F_RXCSUM;
	rmnet_dev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
	rmnet_dev->hw_features |= NETIF_F_SG;

	priv->real_dev = real_dev;

	headroom = rmnet_vnd_headroom(port);

	if (rmnet_vnd_change_mtu(rmnet_dev, real_dev->mtu - headroom)) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid MTU on real dev");
		return -EINVAL;
	}

	rc = register_netdevice(rmnet_dev);
	if (!rc) {
		ep->egress_dev = rmnet_dev;
		ep->mux_id = id;
		port->nr_rmnet_devs++;

		rmnet_dev->rtnl_link_ops = &rmnet_link_ops;

		priv->mux_id = id;

		netdev_dbg(rmnet_dev, "rmnet dev created\n");
	}

	return rc;
}

int rmnet_vnd_dellink(u8 id, struct rmnet_port *port,
		      struct rmnet_endpoint *ep)
{
	if (id >= RMNET_MAX_LOGICAL_EP || !ep->egress_dev)
		return -EINVAL;

	ep->egress_dev = NULL;
	port->nr_rmnet_devs--;
	return 0;
}

int rmnet_vnd_do_flow_control(struct net_device *rmnet_dev, int enable)
{
	netdev_dbg(rmnet_dev, "Setting VND TX queue state to %d\n", enable);
	/* Although we expect similar number of enable/disable
	 * commands, optimize for the disable. That is more
	 * latency sensitive than enable
	 */
	if (unlikely(enable))
		netif_wake_queue(rmnet_dev);
	else
		netif_stop_queue(rmnet_dev);

	return 0;
}

int rmnet_vnd_validate_real_dev_mtu(struct net_device *real_dev)
{
	struct hlist_node *tmp_ep;
	struct rmnet_endpoint *ep;
	struct rmnet_port *port;
	unsigned long bkt_ep;
	u32 headroom;

	port = rmnet_get_port_rtnl(real_dev);

	headroom = rmnet_vnd_headroom(port);

	hash_for_each_safe(port->muxed_ep, bkt_ep, tmp_ep, ep, hlnode) {
		if (ep->egress_dev->mtu > (real_dev->mtu - headroom))
			return -1;
	}

	return 0;
}

int rmnet_vnd_update_dev_mtu(struct rmnet_port *port,
			     struct net_device *real_dev)
{
	struct hlist_node *tmp_ep;
	struct rmnet_endpoint *ep;
	unsigned long bkt_ep;
	u32 headroom;

	headroom = rmnet_vnd_headroom(port);

	hash_for_each_safe(port->muxed_ep, bkt_ep, tmp_ep, ep, hlnode) {
		if (ep->egress_dev->mtu <= (real_dev->mtu - headroom))
			continue;

		if (rmnet_vnd_change_mtu(ep->egress_dev,
					 real_dev->mtu - headroom))
			return -1;
	}

	return 0;
}
