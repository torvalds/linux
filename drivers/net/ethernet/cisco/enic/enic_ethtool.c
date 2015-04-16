/**
 * Copyright 2013 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/netdevice.h>
#include <linux/ethtool.h>

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

static int enic_get_settings(struct net_device *netdev,
	struct ethtool_cmd *ecmd)
{
	struct enic *enic = netdev_priv(netdev);

	ecmd->supported = (SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE);
	ecmd->advertising = (ADVERTISED_10000baseT_Full | ADVERTISED_FIBRE);
	ecmd->port = PORT_FIBRE;
	ecmd->transceiver = XCVR_EXTERNAL;

	if (netif_carrier_ok(netdev)) {
		ethtool_cmd_speed_set(ecmd, vnic_dev_port_speed(enic->vdev));
		ecmd->duplex = DUPLEX_FULL;
	} else {
		ethtool_cmd_speed_set(ecmd, SPEED_UNKNOWN);
		ecmd->duplex = DUPLEX_UNKNOWN;
	}

	ecmd->autoneg = AUTONEG_DISABLE;

	return 0;
}

static void enic_get_drvinfo(struct net_device *netdev,
	struct ethtool_drvinfo *drvinfo)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_devcmd_fw_info *fw_info;

	enic_dev_fw_info(enic, &fw_info);

	strlcpy(drvinfo->driver, DRV_NAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, DRV_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, fw_info->fw_version,
		sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, pci_name(enic->pdev),
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

	enic_dev_stats_dump(enic, &vstats);

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
	struct ethtool_coalesce *ecmd)
{
	struct enic *enic = netdev_priv(netdev);
	struct enic_rx_coal *rxcoal = &enic->rx_coalesce_setting;

	ecmd->tx_coalesce_usecs = enic->tx_coalesce_usecs;
	ecmd->rx_coalesce_usecs = enic->rx_coalesce_usecs;
	if (rxcoal->use_adaptive_rx_coalesce)
		ecmd->use_adaptive_rx_coalesce = 1;
	ecmd->rx_coalesce_usecs_low = rxcoal->small_pkt_range_start;
	ecmd->rx_coalesce_usecs_high = rxcoal->range_end;

	return 0;
}

static int enic_set_coalesce(struct net_device *netdev,
	struct ethtool_coalesce *ecmd)
{
	struct enic *enic = netdev_priv(netdev);
	u32 tx_coalesce_usecs;
	u32 rx_coalesce_usecs;
	u32 rx_coalesce_usecs_low;
	u32 rx_coalesce_usecs_high;
	u32 coalesce_usecs_max;
	unsigned int i, intr;
	struct enic_rx_coal *rxcoal = &enic->rx_coalesce_setting;

	coalesce_usecs_max = vnic_dev_get_intr_coal_timer_max(enic->vdev);
	tx_coalesce_usecs = min_t(u32, ecmd->tx_coalesce_usecs,
				  coalesce_usecs_max);
	rx_coalesce_usecs = min_t(u32, ecmd->rx_coalesce_usecs,
				  coalesce_usecs_max);

	rx_coalesce_usecs_low = min_t(u32, ecmd->rx_coalesce_usecs_low,
				      coalesce_usecs_max);
	rx_coalesce_usecs_high = min_t(u32, ecmd->rx_coalesce_usecs_high,
				       coalesce_usecs_max);

	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:
		if (tx_coalesce_usecs != rx_coalesce_usecs)
			return -EINVAL;
		if (ecmd->use_adaptive_rx_coalesce	||
		    ecmd->rx_coalesce_usecs_low		||
		    ecmd->rx_coalesce_usecs_high)
			return -EINVAL;

		intr = enic_legacy_io_intr();
		vnic_intr_coalescing_timer_set(&enic->intr[intr],
			tx_coalesce_usecs);
		break;
	case VNIC_DEV_INTR_MODE_MSI:
		if (tx_coalesce_usecs != rx_coalesce_usecs)
			return -EINVAL;
		if (ecmd->use_adaptive_rx_coalesce	||
		    ecmd->rx_coalesce_usecs_low		||
		    ecmd->rx_coalesce_usecs_high)
			return -EINVAL;

		vnic_intr_coalescing_timer_set(&enic->intr[0],
			tx_coalesce_usecs);
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		if (ecmd->rx_coalesce_usecs_high &&
		    (rx_coalesce_usecs_high <
		     rx_coalesce_usecs_low + ENIC_AIC_LARGE_PKT_DIFF))
				return -EINVAL;

		for (i = 0; i < enic->wq_count; i++) {
			intr = enic_msix_wq_intr(enic, i);
			vnic_intr_coalescing_timer_set(&enic->intr[intr],
				tx_coalesce_usecs);
		}

		rxcoal->use_adaptive_rx_coalesce =
					!!ecmd->use_adaptive_rx_coalesce;
		if (!rxcoal->use_adaptive_rx_coalesce)
			enic_intr_coal_set_rx(enic, rx_coalesce_usecs);

		if (ecmd->rx_coalesce_usecs_high) {
			rxcoal->range_end = rx_coalesce_usecs_high;
			rxcoal->small_pkt_range_start = rx_coalesce_usecs_low;
			rxcoal->large_pkt_range_start = rx_coalesce_usecs_low +
							ENIC_AIC_LARGE_PKT_DIFF;
		}
		break;
	default:
		break;
	}

	enic->tx_coalesce_usecs = tx_coalesce_usecs;
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
	switch (n->keys.ip_proto) {
	case IPPROTO_TCP:
		fsp->flow_type = TCP_V4_FLOW;
		break;
	case IPPROTO_UDP:
		fsp->flow_type = UDP_V4_FLOW;
		break;
	default:
		return -EINVAL;
		break;
	}

	fsp->h_u.tcp_ip4_spec.ip4src = n->keys.src;
	fsp->m_u.tcp_ip4_spec.ip4src = (__u32)~0;

	fsp->h_u.tcp_ip4_spec.ip4dst = n->keys.dst;
	fsp->m_u.tcp_ip4_spec.ip4dst = (__u32)~0;

	fsp->h_u.tcp_ip4_spec.psrc = n->keys.port16[0];
	fsp->m_u.tcp_ip4_spec.psrc = (__u16)~0;

	fsp->h_u.tcp_ip4_spec.pdst = n->keys.port16[1];
	fsp->m_u.tcp_ip4_spec.pdst = (__u16)~0;

	fsp->ring_cookie = n->rq_id;

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

static const struct ethtool_ops enic_ethtool_ops = {
	.get_settings = enic_get_settings,
	.get_drvinfo = enic_get_drvinfo,
	.get_msglevel = enic_get_msglevel,
	.set_msglevel = enic_set_msglevel,
	.get_link = ethtool_op_get_link,
	.get_strings = enic_get_strings,
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
};

void enic_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &enic_ethtool_ops;
}
