/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree.
 *
 * THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM "AS IS"
 * WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE
 * OF THE PROGRAM IS WITH YOU. SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME
 * THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION.
 */

#include <linux/debugfs.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <net/netdev_queues.h>
#include <net/page_pool/helpers.h>
#include <net/netlink.h>
#include <net/pkt_cls.h>
#include <net/rtnetlink.h>
#include <net/udp_tunnel.h>

#include "netdevsim.h"

#define NSIM_RING_SIZE		256

static int nsim_napi_rx(struct nsim_rq *rq, struct sk_buff *skb)
{
	if (skb_queue_len(&rq->skb_queue) > NSIM_RING_SIZE) {
		dev_kfree_skb_any(skb);
		return NET_RX_DROP;
	}

	skb_queue_tail(&rq->skb_queue, skb);
	return NET_RX_SUCCESS;
}

static int nsim_forward_skb(struct net_device *dev, struct sk_buff *skb,
			    struct nsim_rq *rq)
{
	return __dev_forward_skb(dev, skb) ?: nsim_napi_rx(rq, skb);
}

static netdev_tx_t nsim_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct net_device *peer_dev;
	unsigned int len = skb->len;
	struct netdevsim *peer_ns;
	struct nsim_rq *rq;
	int rxq;

	rcu_read_lock();
	if (!nsim_ipsec_tx(ns, skb))
		goto out_drop_free;

	peer_ns = rcu_dereference(ns->peer);
	if (!peer_ns)
		goto out_drop_free;

	peer_dev = peer_ns->netdev;
	rxq = skb_get_queue_mapping(skb);
	if (rxq >= peer_dev->num_rx_queues)
		rxq = rxq % peer_dev->num_rx_queues;
	rq = &peer_ns->rq[rxq];

	skb_tx_timestamp(skb);
	if (unlikely(nsim_forward_skb(peer_dev, skb, rq) == NET_RX_DROP))
		goto out_drop_cnt;

	napi_schedule(&rq->napi);

	rcu_read_unlock();
	u64_stats_update_begin(&ns->syncp);
	ns->tx_packets++;
	ns->tx_bytes += len;
	u64_stats_update_end(&ns->syncp);
	return NETDEV_TX_OK;

out_drop_free:
	dev_kfree_skb(skb);
out_drop_cnt:
	rcu_read_unlock();
	u64_stats_update_begin(&ns->syncp);
	ns->tx_dropped++;
	u64_stats_update_end(&ns->syncp);
	return NETDEV_TX_OK;
}

static void nsim_set_rx_mode(struct net_device *dev)
{
}

static int nsim_change_mtu(struct net_device *dev, int new_mtu)
{
	struct netdevsim *ns = netdev_priv(dev);

	if (ns->xdp.prog && new_mtu > NSIM_XDP_MAX_MTU)
		return -EBUSY;

	WRITE_ONCE(dev->mtu, new_mtu);

	return 0;
}

static void
nsim_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct netdevsim *ns = netdev_priv(dev);
	unsigned int start;

	do {
		start = u64_stats_fetch_begin(&ns->syncp);
		stats->tx_bytes = ns->tx_bytes;
		stats->tx_packets = ns->tx_packets;
		stats->tx_dropped = ns->tx_dropped;
	} while (u64_stats_fetch_retry(&ns->syncp, start));
}

static int
nsim_setup_tc_block_cb(enum tc_setup_type type, void *type_data, void *cb_priv)
{
	return nsim_bpf_setup_tc_block_cb(type, type_data, cb_priv);
}

static int nsim_set_vf_mac(struct net_device *dev, int vf, u8 *mac)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_dev *nsim_dev = ns->nsim_dev;

	/* Only refuse multicast addresses, zero address can mean unset/any. */
	if (vf >= nsim_dev_get_vfs(nsim_dev) || is_multicast_ether_addr(mac))
		return -EINVAL;
	memcpy(nsim_dev->vfconfigs[vf].vf_mac, mac, ETH_ALEN);

	return 0;
}

static int nsim_set_vf_vlan(struct net_device *dev, int vf,
			    u16 vlan, u8 qos, __be16 vlan_proto)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_dev *nsim_dev = ns->nsim_dev;

	if (vf >= nsim_dev_get_vfs(nsim_dev) || vlan > 4095 || qos > 7)
		return -EINVAL;

	nsim_dev->vfconfigs[vf].vlan = vlan;
	nsim_dev->vfconfigs[vf].qos = qos;
	nsim_dev->vfconfigs[vf].vlan_proto = vlan_proto;

	return 0;
}

static int nsim_set_vf_rate(struct net_device *dev, int vf, int min, int max)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_dev *nsim_dev = ns->nsim_dev;

	if (nsim_esw_mode_is_switchdev(ns->nsim_dev)) {
		pr_err("Not supported in switchdev mode. Please use devlink API.\n");
		return -EOPNOTSUPP;
	}

	if (vf >= nsim_dev_get_vfs(nsim_dev))
		return -EINVAL;

	nsim_dev->vfconfigs[vf].min_tx_rate = min;
	nsim_dev->vfconfigs[vf].max_tx_rate = max;

	return 0;
}

static int nsim_set_vf_spoofchk(struct net_device *dev, int vf, bool val)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_dev *nsim_dev = ns->nsim_dev;

	if (vf >= nsim_dev_get_vfs(nsim_dev))
		return -EINVAL;
	nsim_dev->vfconfigs[vf].spoofchk_enabled = val;

	return 0;
}

static int nsim_set_vf_rss_query_en(struct net_device *dev, int vf, bool val)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_dev *nsim_dev = ns->nsim_dev;

	if (vf >= nsim_dev_get_vfs(nsim_dev))
		return -EINVAL;
	nsim_dev->vfconfigs[vf].rss_query_enabled = val;

	return 0;
}

static int nsim_set_vf_trust(struct net_device *dev, int vf, bool val)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_dev *nsim_dev = ns->nsim_dev;

	if (vf >= nsim_dev_get_vfs(nsim_dev))
		return -EINVAL;
	nsim_dev->vfconfigs[vf].trusted = val;

	return 0;
}

static int
nsim_get_vf_config(struct net_device *dev, int vf, struct ifla_vf_info *ivi)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_dev *nsim_dev = ns->nsim_dev;

	if (vf >= nsim_dev_get_vfs(nsim_dev))
		return -EINVAL;

	ivi->vf = vf;
	ivi->linkstate = nsim_dev->vfconfigs[vf].link_state;
	ivi->min_tx_rate = nsim_dev->vfconfigs[vf].min_tx_rate;
	ivi->max_tx_rate = nsim_dev->vfconfigs[vf].max_tx_rate;
	ivi->vlan = nsim_dev->vfconfigs[vf].vlan;
	ivi->vlan_proto = nsim_dev->vfconfigs[vf].vlan_proto;
	ivi->qos = nsim_dev->vfconfigs[vf].qos;
	memcpy(&ivi->mac, nsim_dev->vfconfigs[vf].vf_mac, ETH_ALEN);
	ivi->spoofchk = nsim_dev->vfconfigs[vf].spoofchk_enabled;
	ivi->trusted = nsim_dev->vfconfigs[vf].trusted;
	ivi->rss_query_en = nsim_dev->vfconfigs[vf].rss_query_enabled;

	return 0;
}

static int nsim_set_vf_link_state(struct net_device *dev, int vf, int state)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_dev *nsim_dev = ns->nsim_dev;

	if (vf >= nsim_dev_get_vfs(nsim_dev))
		return -EINVAL;

	switch (state) {
	case IFLA_VF_LINK_STATE_AUTO:
	case IFLA_VF_LINK_STATE_ENABLE:
	case IFLA_VF_LINK_STATE_DISABLE:
		break;
	default:
		return -EINVAL;
	}

	nsim_dev->vfconfigs[vf].link_state = state;

	return 0;
}

static void nsim_taprio_stats(struct tc_taprio_qopt_stats *stats)
{
	stats->window_drops = 0;
	stats->tx_overruns = 0;
}

static int nsim_setup_tc_taprio(struct net_device *dev,
				struct tc_taprio_qopt_offload *offload)
{
	int err = 0;

	switch (offload->cmd) {
	case TAPRIO_CMD_REPLACE:
	case TAPRIO_CMD_DESTROY:
		break;
	case TAPRIO_CMD_STATS:
		nsim_taprio_stats(&offload->stats);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	return err;
}

static LIST_HEAD(nsim_block_cb_list);

static int
nsim_setup_tc(struct net_device *dev, enum tc_setup_type type, void *type_data)
{
	struct netdevsim *ns = netdev_priv(dev);

	switch (type) {
	case TC_SETUP_QDISC_TAPRIO:
		return nsim_setup_tc_taprio(dev, type_data);
	case TC_SETUP_BLOCK:
		return flow_block_cb_setup_simple(type_data,
						  &nsim_block_cb_list,
						  nsim_setup_tc_block_cb,
						  ns, ns, true);
	default:
		return -EOPNOTSUPP;
	}
}

static int
nsim_set_features(struct net_device *dev, netdev_features_t features)
{
	struct netdevsim *ns = netdev_priv(dev);

	if ((dev->features & NETIF_F_HW_TC) > (features & NETIF_F_HW_TC))
		return nsim_bpf_disable_tc(ns);

	return 0;
}

static int nsim_get_iflink(const struct net_device *dev)
{
	struct netdevsim *nsim, *peer;
	int iflink;

	nsim = netdev_priv(dev);

	rcu_read_lock();
	peer = rcu_dereference(nsim->peer);
	iflink = peer ? READ_ONCE(peer->netdev->ifindex) :
			READ_ONCE(dev->ifindex);
	rcu_read_unlock();

	return iflink;
}

static int nsim_rcv(struct nsim_rq *rq, int budget)
{
	struct sk_buff *skb;
	int i;

	for (i = 0; i < budget; i++) {
		if (skb_queue_empty(&rq->skb_queue))
			break;

		skb = skb_dequeue(&rq->skb_queue);
		netif_receive_skb(skb);
	}

	return i;
}

static int nsim_poll(struct napi_struct *napi, int budget)
{
	struct nsim_rq *rq = container_of(napi, struct nsim_rq, napi);
	int done;

	done = nsim_rcv(rq, budget);
	napi_complete(napi);

	return done;
}

static int nsim_create_page_pool(struct nsim_rq *rq)
{
	struct page_pool_params p = {
		.order = 0,
		.pool_size = NSIM_RING_SIZE,
		.nid = NUMA_NO_NODE,
		.dev = &rq->napi.dev->dev,
		.napi = &rq->napi,
		.dma_dir = DMA_BIDIRECTIONAL,
		.netdev = rq->napi.dev,
	};

	rq->page_pool = page_pool_create(&p);
	if (IS_ERR(rq->page_pool)) {
		int err = PTR_ERR(rq->page_pool);

		rq->page_pool = NULL;
		return err;
	}
	return 0;
}

static int nsim_init_napi(struct netdevsim *ns)
{
	struct net_device *dev = ns->netdev;
	struct nsim_rq *rq;
	int err, i;

	for (i = 0; i < dev->num_rx_queues; i++) {
		rq = &ns->rq[i];

		netif_napi_add(dev, &rq->napi, nsim_poll);
	}

	for (i = 0; i < dev->num_rx_queues; i++) {
		rq = &ns->rq[i];

		err = nsim_create_page_pool(rq);
		if (err)
			goto err_pp_destroy;
	}

	return 0;

err_pp_destroy:
	while (i--) {
		page_pool_destroy(ns->rq[i].page_pool);
		ns->rq[i].page_pool = NULL;
	}

	for (i = 0; i < dev->num_rx_queues; i++)
		__netif_napi_del(&ns->rq[i].napi);

	return err;
}

static void nsim_enable_napi(struct netdevsim *ns)
{
	struct net_device *dev = ns->netdev;
	int i;

	for (i = 0; i < dev->num_rx_queues; i++) {
		struct nsim_rq *rq = &ns->rq[i];

		netif_queue_set_napi(dev, i, NETDEV_QUEUE_TYPE_RX, &rq->napi);
		napi_enable(&rq->napi);
	}
}

static int nsim_open(struct net_device *dev)
{
	struct netdevsim *ns = netdev_priv(dev);
	int err;

	err = nsim_init_napi(ns);
	if (err)
		return err;

	nsim_enable_napi(ns);

	return 0;
}

static void nsim_del_napi(struct netdevsim *ns)
{
	struct net_device *dev = ns->netdev;
	int i;

	for (i = 0; i < dev->num_rx_queues; i++) {
		struct nsim_rq *rq = &ns->rq[i];

		napi_disable(&rq->napi);
		__netif_napi_del(&rq->napi);
	}
	synchronize_net();

	for (i = 0; i < dev->num_rx_queues; i++) {
		page_pool_destroy(ns->rq[i].page_pool);
		ns->rq[i].page_pool = NULL;
	}
}

static int nsim_stop(struct net_device *dev)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct netdevsim *peer;

	netif_carrier_off(dev);
	peer = rtnl_dereference(ns->peer);
	if (peer)
		netif_carrier_off(peer->netdev);

	nsim_del_napi(ns);

	return 0;
}

static const struct net_device_ops nsim_netdev_ops = {
	.ndo_start_xmit		= nsim_start_xmit,
	.ndo_set_rx_mode	= nsim_set_rx_mode,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= nsim_change_mtu,
	.ndo_get_stats64	= nsim_get_stats64,
	.ndo_set_vf_mac		= nsim_set_vf_mac,
	.ndo_set_vf_vlan	= nsim_set_vf_vlan,
	.ndo_set_vf_rate	= nsim_set_vf_rate,
	.ndo_set_vf_spoofchk	= nsim_set_vf_spoofchk,
	.ndo_set_vf_trust	= nsim_set_vf_trust,
	.ndo_get_vf_config	= nsim_get_vf_config,
	.ndo_set_vf_link_state	= nsim_set_vf_link_state,
	.ndo_set_vf_rss_query_en = nsim_set_vf_rss_query_en,
	.ndo_setup_tc		= nsim_setup_tc,
	.ndo_set_features	= nsim_set_features,
	.ndo_get_iflink		= nsim_get_iflink,
	.ndo_bpf		= nsim_bpf,
	.ndo_open		= nsim_open,
	.ndo_stop		= nsim_stop,
};

static const struct net_device_ops nsim_vf_netdev_ops = {
	.ndo_start_xmit		= nsim_start_xmit,
	.ndo_set_rx_mode	= nsim_set_rx_mode,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= nsim_change_mtu,
	.ndo_get_stats64	= nsim_get_stats64,
	.ndo_setup_tc		= nsim_setup_tc,
	.ndo_set_features	= nsim_set_features,
};

/* We don't have true per-queue stats, yet, so do some random fakery here.
 * Only report stuff for queue 0.
 */
static void nsim_get_queue_stats_rx(struct net_device *dev, int idx,
				    struct netdev_queue_stats_rx *stats)
{
	struct rtnl_link_stats64 rtstats = {};

	if (!idx)
		nsim_get_stats64(dev, &rtstats);

	stats->packets = rtstats.rx_packets - !!rtstats.rx_packets;
	stats->bytes = rtstats.rx_bytes;
}

static void nsim_get_queue_stats_tx(struct net_device *dev, int idx,
				    struct netdev_queue_stats_tx *stats)
{
	struct rtnl_link_stats64 rtstats = {};

	if (!idx)
		nsim_get_stats64(dev, &rtstats);

	stats->packets = rtstats.tx_packets - !!rtstats.tx_packets;
	stats->bytes = rtstats.tx_bytes;
}

static void nsim_get_base_stats(struct net_device *dev,
				struct netdev_queue_stats_rx *rx,
				struct netdev_queue_stats_tx *tx)
{
	struct rtnl_link_stats64 rtstats = {};

	nsim_get_stats64(dev, &rtstats);

	rx->packets = !!rtstats.rx_packets;
	rx->bytes = 0;
	tx->packets = !!rtstats.tx_packets;
	tx->bytes = 0;
}

static const struct netdev_stat_ops nsim_stat_ops = {
	.get_queue_stats_tx	= nsim_get_queue_stats_tx,
	.get_queue_stats_rx	= nsim_get_queue_stats_rx,
	.get_base_stats		= nsim_get_base_stats,
};

static ssize_t
nsim_pp_hold_read(struct file *file, char __user *data,
		  size_t count, loff_t *ppos)
{
	struct netdevsim *ns = file->private_data;
	char buf[3] = "n\n";

	if (ns->page)
		buf[0] = 'y';

	return simple_read_from_buffer(data, count, ppos, buf, 2);
}

static ssize_t
nsim_pp_hold_write(struct file *file, const char __user *data,
		   size_t count, loff_t *ppos)
{
	struct netdevsim *ns = file->private_data;
	ssize_t ret;
	bool val;

	ret = kstrtobool_from_user(data, count, &val);
	if (ret)
		return ret;

	rtnl_lock();
	ret = count;
	if (val == !!ns->page)
		goto exit;

	if (!netif_running(ns->netdev) && val) {
		ret = -ENETDOWN;
	} else if (val) {
		ns->page = page_pool_dev_alloc_pages(ns->rq[0].page_pool);
		if (!ns->page)
			ret = -ENOMEM;
	} else {
		page_pool_put_full_page(ns->page->pp, ns->page, false);
		ns->page = NULL;
	}

exit:
	rtnl_unlock();
	return ret;
}

static const struct file_operations nsim_pp_hold_fops = {
	.open = simple_open,
	.read = nsim_pp_hold_read,
	.write = nsim_pp_hold_write,
	.llseek = generic_file_llseek,
	.owner = THIS_MODULE,
};

static void nsim_setup(struct net_device *dev)
{
	ether_setup(dev);
	eth_hw_addr_random(dev);

	dev->tx_queue_len = 0;
	dev->flags &= ~IFF_MULTICAST;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE |
			   IFF_NO_QUEUE;
	dev->features |= NETIF_F_HIGHDMA |
			 NETIF_F_SG |
			 NETIF_F_FRAGLIST |
			 NETIF_F_HW_CSUM |
			 NETIF_F_TSO;
	dev->hw_features |= NETIF_F_HW_TC;
	dev->max_mtu = ETH_MAX_MTU;
	dev->xdp_features = NETDEV_XDP_ACT_HW_OFFLOAD;
}

static int nsim_queue_init(struct netdevsim *ns)
{
	struct net_device *dev = ns->netdev;
	int i;

	ns->rq = kvcalloc(dev->num_rx_queues, sizeof(*ns->rq),
			  GFP_KERNEL_ACCOUNT | __GFP_RETRY_MAYFAIL);
	if (!ns->rq)
		return -ENOMEM;

	for (i = 0; i < dev->num_rx_queues; i++)
		skb_queue_head_init(&ns->rq[i].skb_queue);

	return 0;
}

static void nsim_queue_free(struct netdevsim *ns)
{
	struct net_device *dev = ns->netdev;
	int i;

	for (i = 0; i < dev->num_rx_queues; i++)
		skb_queue_purge_reason(&ns->rq[i].skb_queue,
				       SKB_DROP_REASON_QUEUE_PURGE);

	kvfree(ns->rq);
	ns->rq = NULL;
}

static int nsim_init_netdevsim(struct netdevsim *ns)
{
	struct mock_phc *phc;
	int err;

	phc = mock_phc_create(&ns->nsim_bus_dev->dev);
	if (IS_ERR(phc))
		return PTR_ERR(phc);

	ns->phc = phc;
	ns->netdev->netdev_ops = &nsim_netdev_ops;
	ns->netdev->stat_ops = &nsim_stat_ops;

	err = nsim_udp_tunnels_info_create(ns->nsim_dev, ns->netdev);
	if (err)
		goto err_phc_destroy;

	rtnl_lock();
	err = nsim_queue_init(ns);
	if (err)
		goto err_utn_destroy;

	err = nsim_bpf_init(ns);
	if (err)
		goto err_rq_destroy;

	nsim_macsec_init(ns);
	nsim_ipsec_init(ns);

	err = register_netdevice(ns->netdev);
	if (err)
		goto err_ipsec_teardown;
	rtnl_unlock();
	return 0;

err_ipsec_teardown:
	nsim_ipsec_teardown(ns);
	nsim_macsec_teardown(ns);
	nsim_bpf_uninit(ns);
err_rq_destroy:
	nsim_queue_free(ns);
err_utn_destroy:
	rtnl_unlock();
	nsim_udp_tunnels_info_destroy(ns->netdev);
err_phc_destroy:
	mock_phc_destroy(ns->phc);
	return err;
}

static int nsim_init_netdevsim_vf(struct netdevsim *ns)
{
	int err;

	ns->netdev->netdev_ops = &nsim_vf_netdev_ops;
	rtnl_lock();
	err = register_netdevice(ns->netdev);
	rtnl_unlock();
	return err;
}

static void nsim_exit_netdevsim(struct netdevsim *ns)
{
	nsim_udp_tunnels_info_destroy(ns->netdev);
	mock_phc_destroy(ns->phc);
}

struct netdevsim *
nsim_create(struct nsim_dev *nsim_dev, struct nsim_dev_port *nsim_dev_port)
{
	struct net_device *dev;
	struct netdevsim *ns;
	int err;

	dev = alloc_netdev_mq(sizeof(*ns), "eth%d", NET_NAME_UNKNOWN, nsim_setup,
			      nsim_dev->nsim_bus_dev->num_queues);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev_net_set(dev, nsim_dev_net(nsim_dev));
	ns = netdev_priv(dev);
	ns->netdev = dev;
	u64_stats_init(&ns->syncp);
	ns->nsim_dev = nsim_dev;
	ns->nsim_dev_port = nsim_dev_port;
	ns->nsim_bus_dev = nsim_dev->nsim_bus_dev;
	SET_NETDEV_DEV(dev, &ns->nsim_bus_dev->dev);
	SET_NETDEV_DEVLINK_PORT(dev, &nsim_dev_port->devlink_port);
	nsim_ethtool_init(ns);
	if (nsim_dev_port_is_pf(nsim_dev_port))
		err = nsim_init_netdevsim(ns);
	else
		err = nsim_init_netdevsim_vf(ns);
	if (err)
		goto err_free_netdev;

	ns->pp_dfs = debugfs_create_file("pp_hold", 0600, nsim_dev_port->ddir,
					 ns, &nsim_pp_hold_fops);

	return ns;

err_free_netdev:
	free_netdev(dev);
	return ERR_PTR(err);
}

void nsim_destroy(struct netdevsim *ns)
{
	struct net_device *dev = ns->netdev;
	struct netdevsim *peer;

	debugfs_remove(ns->pp_dfs);

	rtnl_lock();
	peer = rtnl_dereference(ns->peer);
	if (peer)
		RCU_INIT_POINTER(peer->peer, NULL);
	RCU_INIT_POINTER(ns->peer, NULL);
	unregister_netdevice(dev);
	if (nsim_dev_port_is_pf(ns->nsim_dev_port)) {
		nsim_macsec_teardown(ns);
		nsim_ipsec_teardown(ns);
		nsim_bpf_uninit(ns);
		nsim_queue_free(ns);
	}
	rtnl_unlock();
	if (nsim_dev_port_is_pf(ns->nsim_dev_port))
		nsim_exit_netdevsim(ns);

	/* Put this intentionally late to exercise the orphaning path */
	if (ns->page) {
		page_pool_put_full_page(ns->page->pp, ns->page, false);
		ns->page = NULL;
	}

	free_netdev(dev);
}

bool netdev_is_nsim(struct net_device *dev)
{
	return dev->netdev_ops == &nsim_netdev_ops;
}

static int nsim_validate(struct nlattr *tb[], struct nlattr *data[],
			 struct netlink_ext_ack *extack)
{
	NL_SET_ERR_MSG_MOD(extack,
			   "Please use: echo \"[ID] [PORT_COUNT] [NUM_QUEUES]\" > /sys/bus/netdevsim/new_device");
	return -EOPNOTSUPP;
}

static struct rtnl_link_ops nsim_link_ops __read_mostly = {
	.kind		= DRV_NAME,
	.validate	= nsim_validate,
};

static int __init nsim_module_init(void)
{
	int err;

	err = nsim_dev_init();
	if (err)
		return err;

	err = nsim_bus_init();
	if (err)
		goto err_dev_exit;

	err = rtnl_link_register(&nsim_link_ops);
	if (err)
		goto err_bus_exit;

	return 0;

err_bus_exit:
	nsim_bus_exit();
err_dev_exit:
	nsim_dev_exit();
	return err;
}

static void __exit nsim_module_exit(void)
{
	rtnl_link_unregister(&nsim_link_ops);
	nsim_bus_exit();
	nsim_dev_exit();
}

module_init(nsim_module_init);
module_exit(nsim_module_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simulated networking device for testing");
MODULE_ALIAS_RTNL_LINK(DRV_NAME);
