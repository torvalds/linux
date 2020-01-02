// SPDX-License-Identifier: GPL-2.0
/* Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/ethtool.h>
#include <linux/vmalloc.h>

#include "hinic_hw_qp.h"
#include "hinic_hw_dev.h"
#include "hinic_port.h"
#include "hinic_tx.h"
#include "hinic_rx.h"
#include "hinic_dev.h"

static void set_link_speed(struct ethtool_link_ksettings *link_ksettings,
			   enum hinic_speed speed)
{
	switch (speed) {
	case HINIC_SPEED_10MB_LINK:
		link_ksettings->base.speed = SPEED_10;
		break;

	case HINIC_SPEED_100MB_LINK:
		link_ksettings->base.speed = SPEED_100;
		break;

	case HINIC_SPEED_1000MB_LINK:
		link_ksettings->base.speed = SPEED_1000;
		break;

	case HINIC_SPEED_10GB_LINK:
		link_ksettings->base.speed = SPEED_10000;
		break;

	case HINIC_SPEED_25GB_LINK:
		link_ksettings->base.speed = SPEED_25000;
		break;

	case HINIC_SPEED_40GB_LINK:
		link_ksettings->base.speed = SPEED_40000;
		break;

	case HINIC_SPEED_100GB_LINK:
		link_ksettings->base.speed = SPEED_100000;
		break;

	default:
		link_ksettings->base.speed = SPEED_UNKNOWN;
		break;
	}
}

static int hinic_get_link_ksettings(struct net_device *netdev,
				    struct ethtool_link_ksettings
				    *link_ksettings)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	enum hinic_port_link_state link_state;
	struct hinic_port_cap port_cap;
	int err;

	ethtool_link_ksettings_zero_link_mode(link_ksettings, advertising);
	ethtool_link_ksettings_add_link_mode(link_ksettings, supported,
					     Autoneg);

	link_ksettings->base.speed = SPEED_UNKNOWN;
	link_ksettings->base.autoneg = AUTONEG_DISABLE;
	link_ksettings->base.duplex = DUPLEX_UNKNOWN;

	err = hinic_port_get_cap(nic_dev, &port_cap);
	if (err)
		return err;

	err = hinic_port_link_state(nic_dev, &link_state);
	if (err)
		return err;

	if (link_state != HINIC_LINK_STATE_UP)
		return err;

	set_link_speed(link_ksettings, port_cap.speed);

	if (!!(port_cap.autoneg_cap & HINIC_AUTONEG_SUPPORTED))
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, Autoneg);

	if (port_cap.autoneg_state == HINIC_AUTONEG_ACTIVE)
		link_ksettings->base.autoneg = AUTONEG_ENABLE;

	link_ksettings->base.duplex = (port_cap.duplex == HINIC_DUPLEX_FULL) ?
					   DUPLEX_FULL : DUPLEX_HALF;
	return 0;
}

static void hinic_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *info)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u8 mgmt_ver[HINIC_MGMT_VERSION_MAX_LEN] = {0};
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	int err;

	strlcpy(info->driver, HINIC_DRV_NAME, sizeof(info->driver));
	strlcpy(info->bus_info, pci_name(hwif->pdev), sizeof(info->bus_info));

	err = hinic_get_mgmt_version(nic_dev, mgmt_ver);
	if (err)
		return;

	snprintf(info->fw_version, sizeof(info->fw_version), "%s", mgmt_ver);
}

static void hinic_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	ring->rx_max_pending = HINIC_RQ_DEPTH;
	ring->tx_max_pending = HINIC_SQ_DEPTH;
	ring->rx_pending = HINIC_RQ_DEPTH;
	ring->tx_pending = HINIC_SQ_DEPTH;
}

static void hinic_get_channels(struct net_device *netdev,
			       struct ethtool_channels *channels)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_hwdev *hwdev = nic_dev->hwdev;

	channels->max_rx = hwdev->nic_cap.max_qps;
	channels->max_tx = hwdev->nic_cap.max_qps;
	channels->max_other = 0;
	channels->max_combined = 0;
	channels->rx_count = hinic_hwdev_num_qps(hwdev);
	channels->tx_count = hinic_hwdev_num_qps(hwdev);
	channels->other_count = 0;
	channels->combined_count = 0;
}

static int hinic_get_rss_hash_opts(struct hinic_dev *nic_dev,
				   struct ethtool_rxnfc *cmd)
{
	struct hinic_rss_type rss_type = { 0 };
	int err;

	cmd->data = 0;

	if (!(nic_dev->flags & HINIC_RSS_ENABLE))
		return 0;

	err = hinic_get_rss_type(nic_dev, nic_dev->rss_tmpl_idx,
				 &rss_type);
	if (err)
		return err;

	cmd->data = RXH_IP_SRC | RXH_IP_DST;
	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		if (rss_type.tcp_ipv4)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case TCP_V6_FLOW:
		if (rss_type.tcp_ipv6)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case UDP_V4_FLOW:
		if (rss_type.udp_ipv4)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case UDP_V6_FLOW:
		if (rss_type.udp_ipv6)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case IPV4_FLOW:
	case IPV6_FLOW:
		break;
	default:
		cmd->data = 0;
		return -EINVAL;
	}

	return 0;
}

static int set_l4_rss_hash_ops(struct ethtool_rxnfc *cmd,
			       struct hinic_rss_type *rss_type)
{
	u8 rss_l4_en = 0;

	switch (cmd->data & (RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
	case 0:
		rss_l4_en = 0;
		break;
	case (RXH_L4_B_0_1 | RXH_L4_B_2_3):
		rss_l4_en = 1;
		break;
	default:
		return -EINVAL;
	}

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		rss_type->tcp_ipv4 = rss_l4_en;
		break;
	case TCP_V6_FLOW:
		rss_type->tcp_ipv6 = rss_l4_en;
		break;
	case UDP_V4_FLOW:
		rss_type->udp_ipv4 = rss_l4_en;
		break;
	case UDP_V6_FLOW:
		rss_type->udp_ipv6 = rss_l4_en;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int hinic_set_rss_hash_opts(struct hinic_dev *nic_dev,
				   struct ethtool_rxnfc *cmd)
{
	struct hinic_rss_type *rss_type = &nic_dev->rss_type;
	int err;

	if (!(nic_dev->flags & HINIC_RSS_ENABLE)) {
		cmd->data = 0;
		return -EOPNOTSUPP;
	}

	/* RSS does not support anything other than hashing
	 * to queues on src and dst IPs and ports
	 */
	if (cmd->data & ~(RXH_IP_SRC | RXH_IP_DST | RXH_L4_B_0_1 |
		RXH_L4_B_2_3))
		return -EINVAL;

	/* We need at least the IP SRC and DEST fields for hashing */
	if (!(cmd->data & RXH_IP_SRC) || !(cmd->data & RXH_IP_DST))
		return -EINVAL;

	err = hinic_get_rss_type(nic_dev,
				 nic_dev->rss_tmpl_idx, rss_type);
	if (err)
		return -EFAULT;

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
	case UDP_V4_FLOW:
	case UDP_V6_FLOW:
		err = set_l4_rss_hash_ops(cmd, rss_type);
		if (err)
			return err;
		break;
	case IPV4_FLOW:
		rss_type->ipv4 = 1;
		break;
	case IPV6_FLOW:
		rss_type->ipv6 = 1;
		break;
	default:
		return -EINVAL;
	}

	err = hinic_set_rss_type(nic_dev, nic_dev->rss_tmpl_idx,
				 *rss_type);
	if (err)
		return -EFAULT;

	return 0;
}

static int __set_rss_rxfh(struct net_device *netdev,
			  const u32 *indir, const u8 *key)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int err;

	if (indir) {
		if (!nic_dev->rss_indir_user) {
			nic_dev->rss_indir_user =
				kzalloc(sizeof(u32) * HINIC_RSS_INDIR_SIZE,
					GFP_KERNEL);
			if (!nic_dev->rss_indir_user)
				return -ENOMEM;
		}

		memcpy(nic_dev->rss_indir_user, indir,
		       sizeof(u32) * HINIC_RSS_INDIR_SIZE);

		err = hinic_rss_set_indir_tbl(nic_dev,
					      nic_dev->rss_tmpl_idx, indir);
		if (err)
			return -EFAULT;
	}

	if (key) {
		if (!nic_dev->rss_hkey_user) {
			nic_dev->rss_hkey_user =
				kzalloc(HINIC_RSS_KEY_SIZE * 2, GFP_KERNEL);

			if (!nic_dev->rss_hkey_user)
				return -ENOMEM;
		}

		memcpy(nic_dev->rss_hkey_user, key, HINIC_RSS_KEY_SIZE);

		err = hinic_rss_set_template_tbl(nic_dev,
						 nic_dev->rss_tmpl_idx, key);
		if (err)
			return -EFAULT;
	}

	return 0;
}

static int hinic_get_rxnfc(struct net_device *netdev,
			   struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int err = 0;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = nic_dev->num_qps;
		break;
	case ETHTOOL_GRXFH:
		err = hinic_get_rss_hash_opts(nic_dev, cmd);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int hinic_set_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int err = 0;

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		err = hinic_set_rss_hash_opts(nic_dev, cmd);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int hinic_get_rxfh(struct net_device *netdev,
			  u32 *indir, u8 *key, u8 *hfunc)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u8 hash_engine_type = 0;
	int err = 0;

	if (!(nic_dev->flags & HINIC_RSS_ENABLE))
		return -EOPNOTSUPP;

	if (hfunc) {
		err = hinic_rss_get_hash_engine(nic_dev,
						nic_dev->rss_tmpl_idx,
						&hash_engine_type);
		if (err)
			return -EFAULT;

		*hfunc = hash_engine_type ? ETH_RSS_HASH_TOP : ETH_RSS_HASH_XOR;
	}

	if (indir) {
		err = hinic_rss_get_indir_tbl(nic_dev,
					      nic_dev->rss_tmpl_idx, indir);
		if (err)
			return -EFAULT;
	}

	if (key)
		err = hinic_rss_get_template_tbl(nic_dev,
						 nic_dev->rss_tmpl_idx, key);

	return err;
}

static int hinic_set_rxfh(struct net_device *netdev, const u32 *indir,
			  const u8 *key, const u8 hfunc)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int err = 0;

	if (!(nic_dev->flags & HINIC_RSS_ENABLE))
		return -EOPNOTSUPP;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE) {
		if (hfunc != ETH_RSS_HASH_TOP && hfunc != ETH_RSS_HASH_XOR)
			return -EOPNOTSUPP;

		nic_dev->rss_hash_engine = (hfunc == ETH_RSS_HASH_XOR) ?
			HINIC_RSS_HASH_ENGINE_TYPE_XOR :
			HINIC_RSS_HASH_ENGINE_TYPE_TOEP;
		err = hinic_rss_set_hash_engine
			(nic_dev, nic_dev->rss_tmpl_idx,
			nic_dev->rss_hash_engine);
		if (err)
			return -EFAULT;
	}

	err = __set_rss_rxfh(netdev, indir, key);

	return err;
}

static u32 hinic_get_rxfh_key_size(struct net_device *netdev)
{
	return HINIC_RSS_KEY_SIZE;
}

static u32 hinic_get_rxfh_indir_size(struct net_device *netdev)
{
	return HINIC_RSS_INDIR_SIZE;
}

#define ARRAY_LEN(arr) ((int)((int)sizeof(arr) / (int)sizeof(arr[0])))

#define HINIC_FUNC_STAT(_stat_item) {	\
	.name = #_stat_item, \
	.size = sizeof_field(struct hinic_vport_stats, _stat_item), \
	.offset = offsetof(struct hinic_vport_stats, _stat_item) \
}

static struct hinic_stats hinic_function_stats[] = {
	HINIC_FUNC_STAT(tx_unicast_pkts_vport),
	HINIC_FUNC_STAT(tx_unicast_bytes_vport),
	HINIC_FUNC_STAT(tx_multicast_pkts_vport),
	HINIC_FUNC_STAT(tx_multicast_bytes_vport),
	HINIC_FUNC_STAT(tx_broadcast_pkts_vport),
	HINIC_FUNC_STAT(tx_broadcast_bytes_vport),

	HINIC_FUNC_STAT(rx_unicast_pkts_vport),
	HINIC_FUNC_STAT(rx_unicast_bytes_vport),
	HINIC_FUNC_STAT(rx_multicast_pkts_vport),
	HINIC_FUNC_STAT(rx_multicast_bytes_vport),
	HINIC_FUNC_STAT(rx_broadcast_pkts_vport),
	HINIC_FUNC_STAT(rx_broadcast_bytes_vport),

	HINIC_FUNC_STAT(tx_discard_vport),
	HINIC_FUNC_STAT(rx_discard_vport),
	HINIC_FUNC_STAT(tx_err_vport),
	HINIC_FUNC_STAT(rx_err_vport),
};

#define HINIC_PORT_STAT(_stat_item) { \
	.name = #_stat_item, \
	.size = sizeof_field(struct hinic_phy_port_stats, _stat_item), \
	.offset = offsetof(struct hinic_phy_port_stats, _stat_item) \
}

static struct hinic_stats hinic_port_stats[] = {
	HINIC_PORT_STAT(mac_rx_total_pkt_num),
	HINIC_PORT_STAT(mac_rx_total_oct_num),
	HINIC_PORT_STAT(mac_rx_bad_pkt_num),
	HINIC_PORT_STAT(mac_rx_bad_oct_num),
	HINIC_PORT_STAT(mac_rx_good_pkt_num),
	HINIC_PORT_STAT(mac_rx_good_oct_num),
	HINIC_PORT_STAT(mac_rx_uni_pkt_num),
	HINIC_PORT_STAT(mac_rx_multi_pkt_num),
	HINIC_PORT_STAT(mac_rx_broad_pkt_num),
	HINIC_PORT_STAT(mac_tx_total_pkt_num),
	HINIC_PORT_STAT(mac_tx_total_oct_num),
	HINIC_PORT_STAT(mac_tx_bad_pkt_num),
	HINIC_PORT_STAT(mac_tx_bad_oct_num),
	HINIC_PORT_STAT(mac_tx_good_pkt_num),
	HINIC_PORT_STAT(mac_tx_good_oct_num),
	HINIC_PORT_STAT(mac_tx_uni_pkt_num),
	HINIC_PORT_STAT(mac_tx_multi_pkt_num),
	HINIC_PORT_STAT(mac_tx_broad_pkt_num),
	HINIC_PORT_STAT(mac_rx_fragment_pkt_num),
	HINIC_PORT_STAT(mac_rx_undersize_pkt_num),
	HINIC_PORT_STAT(mac_rx_undermin_pkt_num),
	HINIC_PORT_STAT(mac_rx_64_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_65_127_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_128_255_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_256_511_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_512_1023_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_1024_1518_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_1519_2047_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_2048_4095_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_4096_8191_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_8192_9216_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_9217_12287_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_12288_16383_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_1519_max_good_pkt_num),
	HINIC_PORT_STAT(mac_rx_1519_max_bad_pkt_num),
	HINIC_PORT_STAT(mac_rx_oversize_pkt_num),
	HINIC_PORT_STAT(mac_rx_jabber_pkt_num),
	HINIC_PORT_STAT(mac_rx_pause_num),
	HINIC_PORT_STAT(mac_rx_pfc_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri0_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri1_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri2_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri3_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri4_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri5_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri6_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri7_pkt_num),
	HINIC_PORT_STAT(mac_rx_control_pkt_num),
	HINIC_PORT_STAT(mac_rx_sym_err_pkt_num),
	HINIC_PORT_STAT(mac_rx_fcs_err_pkt_num),
	HINIC_PORT_STAT(mac_rx_send_app_good_pkt_num),
	HINIC_PORT_STAT(mac_rx_send_app_bad_pkt_num),
	HINIC_PORT_STAT(mac_tx_fragment_pkt_num),
	HINIC_PORT_STAT(mac_tx_undersize_pkt_num),
	HINIC_PORT_STAT(mac_tx_undermin_pkt_num),
	HINIC_PORT_STAT(mac_tx_64_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_65_127_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_128_255_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_256_511_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_512_1023_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_1024_1518_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_1519_2047_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_2048_4095_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_4096_8191_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_8192_9216_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_9217_12287_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_12288_16383_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_1519_max_good_pkt_num),
	HINIC_PORT_STAT(mac_tx_1519_max_bad_pkt_num),
	HINIC_PORT_STAT(mac_tx_oversize_pkt_num),
	HINIC_PORT_STAT(mac_tx_jabber_pkt_num),
	HINIC_PORT_STAT(mac_tx_pause_num),
	HINIC_PORT_STAT(mac_tx_pfc_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri0_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri1_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri2_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri3_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri4_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri5_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri6_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri7_pkt_num),
	HINIC_PORT_STAT(mac_tx_control_pkt_num),
	HINIC_PORT_STAT(mac_tx_err_all_pkt_num),
	HINIC_PORT_STAT(mac_tx_from_app_good_pkt_num),
	HINIC_PORT_STAT(mac_tx_from_app_bad_pkt_num),
};

#define HINIC_TXQ_STAT(_stat_item) { \
	.name = "txq%d_"#_stat_item, \
	.size = sizeof_field(struct hinic_txq_stats, _stat_item), \
	.offset = offsetof(struct hinic_txq_stats, _stat_item) \
}

static struct hinic_stats hinic_tx_queue_stats[] = {
	HINIC_TXQ_STAT(pkts),
	HINIC_TXQ_STAT(bytes),
	HINIC_TXQ_STAT(tx_busy),
	HINIC_TXQ_STAT(tx_wake),
	HINIC_TXQ_STAT(tx_dropped),
	HINIC_TXQ_STAT(big_frags_pkts),
};

#define HINIC_RXQ_STAT(_stat_item) { \
	.name = "rxq%d_"#_stat_item, \
	.size = sizeof_field(struct hinic_rxq_stats, _stat_item), \
	.offset = offsetof(struct hinic_rxq_stats, _stat_item) \
}

static struct hinic_stats hinic_rx_queue_stats[] = {
	HINIC_RXQ_STAT(pkts),
	HINIC_RXQ_STAT(bytes),
	HINIC_RXQ_STAT(errors),
	HINIC_RXQ_STAT(csum_errors),
	HINIC_RXQ_STAT(other_errors),
};

static void get_drv_queue_stats(struct hinic_dev *nic_dev, u64 *data)
{
	struct hinic_txq_stats txq_stats;
	struct hinic_rxq_stats rxq_stats;
	u16 i = 0, j = 0, qid = 0;
	char *p;

	for (qid = 0; qid < nic_dev->num_qps; qid++) {
		if (!nic_dev->txqs)
			break;

		hinic_txq_get_stats(&nic_dev->txqs[qid], &txq_stats);
		for (j = 0; j < ARRAY_LEN(hinic_tx_queue_stats); j++, i++) {
			p = (char *)&txq_stats +
				hinic_tx_queue_stats[j].offset;
			data[i] = (hinic_tx_queue_stats[j].size ==
					sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
		}
	}

	for (qid = 0; qid < nic_dev->num_qps; qid++) {
		if (!nic_dev->rxqs)
			break;

		hinic_rxq_get_stats(&nic_dev->rxqs[qid], &rxq_stats);
		for (j = 0; j < ARRAY_LEN(hinic_rx_queue_stats); j++, i++) {
			p = (char *)&rxq_stats +
				hinic_rx_queue_stats[j].offset;
			data[i] = (hinic_rx_queue_stats[j].size ==
					sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
		}
	}
}

static void hinic_get_ethtool_stats(struct net_device *netdev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_vport_stats vport_stats = {0};
	struct hinic_phy_port_stats *port_stats;
	u16 i = 0, j = 0;
	char *p;
	int err;

	err = hinic_get_vport_stats(nic_dev, &vport_stats);
	if (err)
		netif_err(nic_dev, drv, netdev,
			  "Failed to get vport stats from firmware\n");

	for (j = 0; j < ARRAY_LEN(hinic_function_stats); j++, i++) {
		p = (char *)&vport_stats + hinic_function_stats[j].offset;
		data[i] = (hinic_function_stats[j].size ==
				sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}

	port_stats = kzalloc(sizeof(*port_stats), GFP_KERNEL);
	if (!port_stats) {
		memset(&data[i], 0,
		       ARRAY_LEN(hinic_port_stats) * sizeof(*data));
		i += ARRAY_LEN(hinic_port_stats);
		goto get_drv_stats;
	}

	err = hinic_get_phy_port_stats(nic_dev, port_stats);
	if (err)
		netif_err(nic_dev, drv, netdev,
			  "Failed to get port stats from firmware\n");

	for (j = 0; j < ARRAY_LEN(hinic_port_stats); j++, i++) {
		p = (char *)port_stats + hinic_port_stats[j].offset;
		data[i] = (hinic_port_stats[j].size ==
				sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}

	kfree(port_stats);

get_drv_stats:
	get_drv_queue_stats(nic_dev, data + i);
}

static int hinic_get_sset_count(struct net_device *netdev, int sset)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int count, q_num;

	switch (sset) {
	case ETH_SS_STATS:
		q_num = nic_dev->num_qps;
		count = ARRAY_LEN(hinic_function_stats) +
			(ARRAY_LEN(hinic_tx_queue_stats) +
			ARRAY_LEN(hinic_rx_queue_stats)) * q_num;

		count += ARRAY_LEN(hinic_port_stats);

		return count;
	default:
		return -EOPNOTSUPP;
	}
}

static void hinic_get_strings(struct net_device *netdev,
			      u32 stringset, u8 *data)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	char *p = (char *)data;
	u16 i, j;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_LEN(hinic_function_stats); i++) {
			memcpy(p, hinic_function_stats[i].name,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < ARRAY_LEN(hinic_port_stats); i++) {
			memcpy(p, hinic_port_stats[i].name,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < nic_dev->num_qps; i++) {
			for (j = 0; j < ARRAY_LEN(hinic_tx_queue_stats); j++) {
				sprintf(p, hinic_tx_queue_stats[j].name, i);
				p += ETH_GSTRING_LEN;
			}
		}

		for (i = 0; i < nic_dev->num_qps; i++) {
			for (j = 0; j < ARRAY_LEN(hinic_rx_queue_stats); j++) {
				sprintf(p, hinic_rx_queue_stats[j].name, i);
				p += ETH_GSTRING_LEN;
			}
		}

		return;
	default:
		return;
	}
}

static const struct ethtool_ops hinic_ethtool_ops = {
	.get_link_ksettings = hinic_get_link_ksettings,
	.get_drvinfo = hinic_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_ringparam = hinic_get_ringparam,
	.get_channels = hinic_get_channels,
	.get_rxnfc = hinic_get_rxnfc,
	.set_rxnfc = hinic_set_rxnfc,
	.get_rxfh_key_size = hinic_get_rxfh_key_size,
	.get_rxfh_indir_size = hinic_get_rxfh_indir_size,
	.get_rxfh = hinic_get_rxfh,
	.set_rxfh = hinic_set_rxfh,
	.get_sset_count = hinic_get_sset_count,
	.get_ethtool_stats = hinic_get_ethtool_stats,
	.get_strings = hinic_get_strings,
};

void hinic_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &hinic_ethtool_ops;
}
