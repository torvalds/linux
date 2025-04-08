/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2023 Beijing WangXun Technology Co., Ltd. */

#ifndef _WX_ETHTOOL_H_
#define _WX_ETHTOOL_H_

int wx_get_sset_count(struct net_device *netdev, int sset);
void wx_get_strings(struct net_device *netdev, u32 stringset, u8 *data);
void wx_get_ethtool_stats(struct net_device *netdev,
			  struct ethtool_stats *stats, u64 *data);
void wx_get_mac_stats(struct net_device *netdev,
		      struct ethtool_eth_mac_stats *mac_stats);
void wx_get_pause_stats(struct net_device *netdev,
			struct ethtool_pause_stats *stats);
void wx_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *info);
int wx_nway_reset(struct net_device *netdev);
int wx_get_link_ksettings(struct net_device *netdev,
			  struct ethtool_link_ksettings *cmd);
int wx_set_link_ksettings(struct net_device *netdev,
			  const struct ethtool_link_ksettings *cmd);
void wx_get_pauseparam(struct net_device *netdev,
		       struct ethtool_pauseparam *pause);
int wx_set_pauseparam(struct net_device *netdev,
		      struct ethtool_pauseparam *pause);
void wx_get_ringparam(struct net_device *netdev,
		      struct ethtool_ringparam *ring,
		      struct kernel_ethtool_ringparam *kernel_ring,
		      struct netlink_ext_ack *extack);
int wx_get_coalesce(struct net_device *netdev,
		    struct ethtool_coalesce *ec,
		    struct kernel_ethtool_coalesce *kernel_coal,
		    struct netlink_ext_ack *extack);
int wx_set_coalesce(struct net_device *netdev,
		    struct ethtool_coalesce *ec,
		    struct kernel_ethtool_coalesce *kernel_coal,
		    struct netlink_ext_ack *extack);
void wx_get_channels(struct net_device *dev,
		     struct ethtool_channels *ch);
int wx_set_channels(struct net_device *dev,
		    struct ethtool_channels *ch);
u32 wx_get_msglevel(struct net_device *netdev);
void wx_set_msglevel(struct net_device *netdev, u32 data);
int wx_get_ts_info(struct net_device *dev,
		   struct kernel_ethtool_ts_info *info);
void wx_get_ptp_stats(struct net_device *dev,
		      struct ethtool_ts_stats *ts_stats);
#endif /* _WX_ETHTOOL_H_ */
