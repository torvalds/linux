// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2013 Cisco Systems, Inc.  All rights reserved.

#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/net_tstamp.h>

#include "enic_res.h"
#include "enic.h"
#include "enic_dev.h"
#include "enic_clsf.h"
#include "vnic_rss.h"
#include "vnic_stats.h"

struct enic_stat {
	char name[ETH_GSTRING_LEN];
	unsigned int index;
};

#define ENIC_TX_STAT(stat) { \
	.name = #stat, \
	.index = offsetof(struct vnic_tx_stats, stat) / sizeof(u64) \
}

#define ENIC_RX_STAT(stat) { \
	.name = #stat, \
	.index = offsetof(struct vnic_rx_stats, stat) / sizeof(u64) \
}

#define ENIC_GEN_STAT(stat) { \
	.name = #stat, \
	.index = offsetof(struct vnic_gen_stats, stat) / sizeof(u64)\
}

static const struct enic_stat enic_tx_stats[] = {
	ENIC_TX_STAT(tx_frames_ok),
	ENIC_TX_STAT(tx_unicast_frames_ok),
	ENIC_TX_STAT(tx_multicast_frames_ok),
	ENIC_TX_STAT(tx_broadcast_frames_ok),
	ENIC_TX_STAT(tx_bytes_ok),
	ENIC_TX_STAT(tx_unicast_bytes_ok),
	ENIC_TX_STAT(tx_multicast_bytes_ok),
	ENIC_TX_STAT(tx_broadcast_bytes_ok),
	ENIC_TX_STAT(tx_drops),
	ENIC_TX_STAT(tx_errors),
	ENIC_TX_STAT(tx_tso),
};

static const struct enic_stat enic_rx_stats[] = {
	ENIC_RX_STAT(rx_frames_ok),
	ENIC_RX_STAT(rx_frames_total),
	ENIC_RX_STAT(rx_unicast_frames_ok),
	ENIC_RX_STAT(rx_multicast_frames_ok),
	ENIC_RX_STAT(rx_broadcast_frames_ok),
	ENIC_RX_STAT(rx_bytes_ok),
	ENIC_RX_STAT(rx_unicast_bytes_ok),
	ENIC_RX_STAT(rx_multicast_bytes_ok),
	ENIC_RX_STAT(rx_broadcast_bytes_ok),
	ENIC_RX_STAT(rx_drop),
	ENIC_RX_STAT(rx_no_bufs),
	ENIC_RX_STAT(rx_errors),
	ENIC_RX_STAT(rx_rss),
	ENIC_RX_STAT(rx_crc_errors),
	ENIC_RX_STAT(rx_frames_64),
	ENIC_RX_STAT(rx_frames_127),
	ENIC_RX_STAT(rx_frames_255),
	ENIC_RX_STAT(rx_frames_511),
	ENIC_RX_STAT(rx_frames_1023),
	ENIC_RX_STAT(rx_frames_1518),
	ENIC_RX_STAT(rx_frames_to_max),
};

static const struct enic_stat enic_gen_stats[] = {
	ENIC_GEN_STAT(dma_map_error),
};

static const unsigned int enic_n_tx_stats = ARRAY_SIZE(enic_tx_stats);
static const unsigned int enic_n_rx_stats = ARRAY_SIZE(enic_rx_stats);
static const unsigned int enic_n_gen_stats = ARRAY_SIZE(enic_gen_stats);

static void enic_intr_coal_set_rx(struct enic *enic, u32 timer)
{
	int i;
	int intr;

	for (i = 0; i < enic->rq_count; i++) {
		intr = enic_msix_rq_intr(enic, i);
		vnic_intr_coalescing_timer_set(&enic->intr[intr], timer);
	}
}

static int enic_get_ksettings(struct net_device *netdev,
			      struct ethtool_link_ksettings *ecmd)
{
	struct enic *enic = netdev_priv(netdev);
	struct ethtool_link_settings *base = &ecmd->base;

	ethtool_link_ksettings_add_link_mode(ecmd, supported,
					     10000baseT_Full);
	ethtool_link_ksettings_add_link_mode(ecmd, supported, FIBRE);
	ethtool_link_ksettings_add_link_mode(ecmd, advertising,
					     10000baseT_Full);
	ethtool_link_ksettings_add_link_mode(ecmd, advertising, FIBRE);
	base->port = PORT_FIBRE;

	if (netif_carrier_ok(netdev)) {
		base->speed = vnic_dev_port_speed(enic->vdev);
		base->duplex = DUPLEX_FULL;
	} else {
		base->speed = SPEED_UNKNOWN;
		base->duplex = DUPLEX_UNKNOWN;
	}

	base->autoneg = AUTONEG_DISABLE;

	return 0;
}

static void enic_get_drvinfo(struct net_device *netdev,
	struct ethtool_drvinfo *drvinfo)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_devcmd_fw_info *fw_info;
	int err;

	err = enic_dev_fw_info(enic, &fw_info);
	/* return only when dma_alloc_coherent fails in vnic_dev_fw_info
	 * For other failures, like devcmd failure, we return previously
	 * recorded info.
	 */
	if (err == -ENOMEM)
		return;

	strscpy(drvinfo->driver, DRV_NAME, sizeof(drvinfo->driver));
	strscpy(drvinfo->fw_version, fw_info->fw_version,
		sizeof(drvinfo->fw_version));
	strscpy(drvinfo->bus_info, pci_name(enic->pdev),
		sizeof(drvinfo->bus_info));
}

static void enic_get_strings(struct net_device *netdev, u32 stringset,
	u8 *data)
{
	unsigned int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < enic_n_tx_stats; i++) {
			memcpy(data, enic_tx_stats[i].name, ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		for (i = 0; i < enic_n_rx_stats; i++) {
			memcpy(data, enic_rx_stats[i].name, ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		for (i = 0; i < enic_n_gen_stats; i++) {
			memcpy(data, enic_gen_stats[i].name, ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void enic_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kernel_ring,
			       struct netlink_ext_ack *extack)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_enet_config *c = &enic->config;

	ring->rx_max_pending = ENIC_MAX_RQ_DESCS;
	ring->rx_pending = c->rq_desc_count;
	ring->tx_max_pending = ENIC_MAX_WQ_DESCS;
	ring->tx_pending = c->wq_desc_count;
}

static int enic_set_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring,
			      struct kernel_ethtool_ringparam *kernel_ring,
			      struct netlink_ext_ack *extack)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_enet_config *c = &enic->config;
	int running = netif_running(netdev);
	unsigned int rx_pending;
	unsigned int tx_pending;
	int err = 0;

	if (ring->rx_mini_max_pending || ring->rx_mini_pending) {
		netdev_info(netdev,
			    "modifying mini ring params is not supported");
		return -EINVAL;
	}
	if (ring->rx_jumbo_max_pending || ring->rx_jumbo_pending) {
		netdev_info(netdev,
			    "modifying jumbo ring params is not supported");
		return -EINVAL;
	}
	rx_pending = c->rq_desc_count;
	tx_pending = c->wq_desc_count;
	if (ring->rx_pending > ENIC_MAX_RQ_DESCS ||
	    ring->rx_pending < ENIC_MIN_RQ_DESCS) {
		netdev_info(netdev, "rx pending (%u) not in range [%u,%u]",
			    ring->rx_pending, ENIC_MIN_RQ_DESCS,
			    ENIC_MAX_RQ_DESCS);
		return -EINVAL;
	}
	if (ring->tx_pending > ENIC_MAX_WQ_DESCS ||
	    ring->tx_pending < ENIC_MIN_WQ_DESCS) {
		netdev_info(netdev, "tx pending (%u) not in range [%u,%u]",
			    ring->tx_pending, ENIC_MIN_WQ_DESCS,
			    ENIC_MAX_WQ_DESCS);
		return -EINVAL;
	}
	if (running)
		dev_close(netdev);
	c->rq_desc_count =
		ring->rx_pending & 0xffffffe0; /* must be aligned to groups of 32 */
	c->wq_desc_count =
		ring->tx_pending & 0xffffffe0; /* must be aligned to groups of 32 */
	enic_free_vnic_resources(enic);
	err = enic_alloc_vnic_resources(enic);
	if (err) {
		netdev_err(netdev,
			   "Failed to alloc vNIC resources, aborting\n");
		enic_free_vnic_resources(enic);
		goto err_out;
	}
	enic_init_vnic_resources(enic);
	if (running) {
		err = dev_open(netdev, NULL);
		if (err)
			goto err_out;
	}
	return 0;
err_out:
	c->rq_desc_count = rx_pending;
	c->wq_desc_count = tx_pending;
	return err;
}

static int enic_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return enic_n_tx_stats + enic_n_rx_stats + enic_n_gen_stats;
	default:
		return -EOPNOTSUPP;
	}
}

static void enic_get_ethtool_stats(struct net_device *netdev,
	struct ethtool_stats *stats, u64 *data)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_stats *vstats;
	unsigned int i;
	int err;

	err = enic_dev_stats_dump(enic, &vstats);
	/* return only when dma_alloc_coherent fails in vnic_dev_stats_dump
	 * For other failures, like devcmd failure, we return previously
	 * recorded stats.
	 */
	if (err == -ENOMEM)
		return;

	for (i = 0; i < enic_n_tx_stats; i++)
		*(data++) = ((u64 *)&vstats->tx)[enic_tx_stats[i].index];
	for (i = 0; i < enic_n_rx_stats; i++)
		*(data++) = ((u64 *)&vstats->rx)[enic_rx_stats[i].index];
	for (i = 0; i < enic_n_gen_stats; i++)
		*(data++) = ((u64 *)&enic->gen_stats)[enic_gen_stats[i].index];
}

static u32 enic_get_msglevel(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	return enic->msg_enable;
}

static void enic_set_msglevel(struct net_device *netdev, u32 value)
{
	struct enic *enic = netdev_priv(netdev);
	enic->msg_enable = value;
}

static int enic_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ecmd,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
{
	struct enic *enic = netdev_priv(netdev);
	struct enic_rx_coal *rxcoal = &enic->rx_coalesce_setting;

	if (vnic_dev_get_intr_mode(enic->vdev) == VNIC_DEV_INTR_MODE_MSIX)
		ecmd->tx_coalesce_usecs = enic->tx_coalesce_usecs;
	ecmd->rx_coalesce_usecs = enic->rx_coalesce_usecs;
	if (rxcoal->use_adaptive_rx_coalesce)
		ecmd->use_adaptive_rx_coalesce = 1;
	ecmd->rx_coalesce_usecs_low = rxcoal->small_pkt_range_start;
	ecmd->rx_coalesce_usecs_high = rxcoal->range_end;

	return 0;
}

static int enic_coalesce_valid(struct enic *enic,
			       struct ethtool_coalesce *ec)
{
	u32 coalesce_usecs_max = vnic_dev_get_intr_coal_timer_max(enic->vdev);
	u32 rx_coalesce_usecs_high = min_t(u32, coalesce_usecs_max,
					   ec->rx_coalesce_usecs_high);
	u32 rx_coalesce_usecs_low = min_t(u32, coalesce_usecs_max,
					  ec->rx_coalesce_usecs_low);

	if ((vnic_dev_get_intr_mode(enic->vdev) != VNIC_DEV_INTR_MODE_MSIX) &&
	    ec->tx_coalesce_usecs)
		return -EINVAL;

	if ((ec->tx_coalesce_usecs > coalesce_usecs_max)	||
	    (ec->rx_coalesce_usecs > coalesce_usecs_max)	||
	    (ec->rx_coalesce_usecs_low > coalesce_usecs_max)	||
	    (ec->rx_coalesce_usecs_high > coalesce_usecs_max))
		netdev_info(enic->netdev, "ethtool_set_coalesce: adaptor supports max coalesce value of %d. Setting max value.\n",
			    coalesce_usecs_max);

	if (ec->rx_coalesce_usecs_high &&
	    (rx_coalesce_usecs_high <
	     rx_coalesce_usecs_low + ENIC_AIC_LARGE_PKT_DIFF))
		return -EINVAL;

	return 0;
}

static int enic_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ecmd,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
{
	struct enic *enic = netdev_priv(netdev);
	u32 tx_coalesce_usecs;
	u32 rx_coalesce_usecs;
	u32 rx_coalesce_usecs_low;
	u32 rx_coalesce_usecs_high;
	u32 coalesce_usecs_max;
	unsigned int i, intr;
	int ret;
	struct enic_rx_coal *rxcoal = &enic->rx_coalesce_setting;

	ret = enic_coalesce_valid(enic, ecmd);
	if (ret)
		return ret;
	coalesce_usecs_max = vnic_dev_get_intr_coal_timer_max(enic->vdev);
	tx_coalesce_usecs = min_t(u32, ecmd->tx_coalesce_usecs,
				  coalesce_usecs_max);
	rx_coalesce_usecs = min_t(u32, ecmd->rx_coalesce_usecs,
				  coalesce_usecs_max);

	rx_coalesce_usecs_low = min_t(u32, ecmd->rx_coalesce_usecs_low,
				      coalesce_usecs_max);
	rx_coalesce_usecs_high = min_t(u32, ecmd->rx_coalesce_usecs_high,
				       coalesce_usecs_max);

	if (vnic_dev_get_intr_mode(enic->vdev) == VNIC_DEV_INTR_MODE_MSIX) {
		for (i = 0; i < enic->wq_count; i++) {
			intr = enic_msix_wq_intr(enic, i);
			vnic_intr_coalescing_timer_set(&enic->intr[intr],
						       tx_coalesce_usecs);
		}
		enic->tx_coalesce_usecs = tx_coalesce_usecs;
	}
	rxcoal->use_adaptive_rx_coalesce = !!ecmd->use_adaptive_rx_coalesce;
	if (!rxcoal->use_adaptive_rx_coalesce)
		enic_intr_coal_set_rx(enic, rx_coalesce_usecs);
	if (ecmd->rx_coalesce_usecs_high) {
		rxcoal->range_end = rx_coalesce_usecs_high;
		rxcoal->small_pkt_range_start = rx_coalesce_usecs_low;
		rxcoal->large_pkt_range_start = rx_coalesce_usecs_low +
						ENIC_AIC_LARGE_PKT_DIFF;
	}

	enic->rx_coalesce_usecs = rx_coalesce_usecs;

	return 0;
}

static int enic_grxclsrlall(struct enic *enic, struct ethtool_rxnfc *cmd,
			    u32 *rule_locs)
{
	int j, ret = 0, cnt = 0;

	cmd->data = enic->rfs_h.max - enic->rfs_h.free;
	for (j = 0; j < (1 << ENIC_RFS_FLW_BITSHIFT); j++) {
		struct hlist_head *hhead;
		struct hlist_node *tmp;
		struct enic_rfs_fltr_node *n;

		hhead = &enic->rfs_h.ht_head[j];
		hlist_for_each_entry_safe(n, tmp, hhead, node) {
			if (cnt == cmd->rule_cnt)
				return -EMSGSIZE;
			rule_locs[cnt] = n->fltr_id;
			cnt++;
		}
	}
	cmd->rule_cnt = cnt;

	return ret;
}

static int enic_grxclsrule(struct enic *enic, struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
				(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct enic_rfs_fltr_node *n;

	n = htbl_fltr_search(enic, (u16)fsp->location);
	if (!n)
		return -EINVAL;
	switch (n->keys.basic.ip_proto) {
	case IPPROTO_TCP:
		fsp->flow_type = TCP_V4_FLOW;
		break;
	case IPPROTO_UDP:
		fsp->flow_type = UDP_V4_FLOW;
		break;
	default:
		return -EINVAL;
	}

	fsp->h_u.tcp_ip4_spec.ip4src = flow_get_u32_src(&n->keys);
	fsp->m_u.tcp_ip4_spec.ip4src = (__u32)~0;

	fsp->h_u.tcp_ip4_spec.ip4dst = flow_get_u32_dst(&n->keys);
	fsp->m_u.tcp_ip4_spec.ip4dst = (__u32)~0;

	fsp->h_u.tcp_ip4_spec.psrc = n->keys.ports.src;
	fsp->m_u.tcp_ip4_spec.psrc = (__u16)~0;

	fsp->h_u.tcp_ip4_spec.pdst = n->keys.ports.dst;
	fsp->m_u.tcp_ip4_spec.pdst = (__u16)~0;

	fsp->ring_cookie = n->rq_id;

	return 0;
}

static int enic_get_rx_flow_hash(struct enic *enic, struct ethtool_rxnfc *cmd)
{
	u8 rss_hash_type = 0;
	cmd->data = 0;

	spin_lock_bh(&enic->devcmd_lock);
	(void)vnic_dev_capable_rss_hash_type(enic->vdev, &rss_hash_type);
	spin_unlock_bh(&enic->devcmd_lock);
	switch (cmd->flow_type) {
	case TCP_V6_FLOW:
	case TCP_V4_FLOW:
		cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3 |
			     RXH_IP_SRC | RXH_IP_DST;
		break;
	case UDP_V6_FLOW:
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		if (rss_hash_type & NIC_CFG_RSS_HASH_TYPE_UDP_IPV6)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case UDP_V4_FLOW:
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		if (rss_hash_type & NIC_CFG_RSS_HASH_TYPE_UDP_IPV4)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case SCTP_V6_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case IPV4_FLOW:
	case IPV6_FLOW:
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int enic_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd,
			  u32 *rule_locs)
{
	struct enic *enic = netdev_priv(dev);
	int ret = 0;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = enic->rq_count;
		break;
	case ETHTOOL_GRXCLSRLCNT:
		spin_lock_bh(&enic->rfs_h.lock);
		cmd->rule_cnt = enic->rfs_h.max - enic->rfs_h.free;
		cmd->data = enic->rfs_h.max;
		spin_unlock_bh(&enic->rfs_h.lock);
		break;
	case ETHTOOL_GRXCLSRLALL:
		spin_lock_bh(&enic->rfs_h.lock);
		ret = enic_grxclsrlall(enic, cmd, rule_locs);
		spin_unlock_bh(&enic->rfs_h.lock);
		break;
	case ETHTOOL_GRXCLSRULE:
		spin_lock_bh(&enic->rfs_h.lock);
		ret = enic_grxclsrule(enic, cmd);
		spin_unlock_bh(&enic->rfs_h.lock);
		break;
	case ETHTOOL_GRXFH:
		ret = enic_get_rx_flow_hash(enic, cmd);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int enic_get_tunable(struct net_device *dev,
			    const struct ethtool_tunable *tuna, void *data)
{
	struct enic *enic = netdev_priv(dev);
	int ret = 0;

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		*(u32 *)data = enic->rx_copybreak;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int enic_set_tunable(struct net_device *dev,
			    const struct ethtool_tunable *tuna,
			    const void *data)
{
	struct enic *enic = netdev_priv(dev);
	int ret = 0;

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		enic->rx_copybreak = *(u32 *)data;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static u32 enic_get_rxfh_key_size(struct net_device *netdev)
{
	return ENIC_RSS_LEN;
}

static int enic_get_rxfh(struct net_device *netdev, u32 *indir, u8 *hkey,
			 u8 *hfunc)
{
	struct enic *enic = netdev_priv(netdev);

	if (hkey)
		memcpy(hkey, enic->rss_key, ENIC_RSS_LEN);

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	return 0;
}

static int enic_set_rxfh(struct net_device *netdev, const u32 *indir,
			 const u8 *hkey, const u8 hfunc)
{
	struct enic *enic = netdev_priv(netdev);

	if ((hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP) ||
	    indir)
		return -EINVAL;

	if (hkey)
		memcpy(enic->rss_key, hkey, ENIC_RSS_LEN);

	return __enic_set_rsskey(enic);
}

static int enic_get_ts_info(struct net_device *netdev,
			    struct ethtool_ts_info *info)
{
	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_SOFTWARE;

	return 0;
}

static const struct ethtool_ops enic_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_USE_ADAPTIVE_RX |
				     ETHTOOL_COALESCE_RX_USECS_LOW |
				     ETHTOOL_COALESCE_RX_USECS_HIGH,
	.get_drvinfo = enic_get_drvinfo,
	.get_msglevel = enic_get_msglevel,
	.set_msglevel = enic_set_msglevel,
	.get_link = ethtool_op_get_link,
	.get_strings = enic_get_strings,
	.get_ringparam = enic_get_ringparam,
	.set_ringparam = enic_set_ringparam,
	.get_sset_count = enic_get_sset_count,
	.get_ethtool_stats = enic_get_ethtool_stats,
	.get_coalesce = enic_get_coalesce,
	.set_coalesce = enic_set_coalesce,
	.get_rxnfc = enic_get_rxnfc,
	.get_tunable = enic_get_tunable,
	.set_tunable = enic_set_tunable,
	.get_rxfh_key_size = enic_get_rxfh_key_size,
	.get_rxfh = enic_get_rxfh,
	.set_rxfh = enic_set_rxfh,
	.get_link_ksettings = enic_get_ksettings,
	.get_ts_info = enic_get_ts_info,
};

void enic_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &enic_ethtool_ops;
}
