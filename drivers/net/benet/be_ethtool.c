/*
 * Copyright (C) 2005 - 2009 ServerEngines
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@serverengines.com
 *
 * ServerEngines
 * 209 N. Fair Oaks Ave
 * Sunnyvale, CA 94085
 */

#include "be.h"
#include <linux/ethtool.h>

struct be_ethtool_stat {
	char desc[ETH_GSTRING_LEN];
	int type;
	int size;
	int offset;
};

enum {NETSTAT, PORTSTAT, MISCSTAT, DRVSTAT, ERXSTAT};
#define FIELDINFO(_struct, field) FIELD_SIZEOF(_struct, field), \
					offsetof(_struct, field)
#define NETSTAT_INFO(field) 	#field, NETSTAT,\
					FIELDINFO(struct net_device_stats,\
						field)
#define DRVSTAT_INFO(field) 	#field, DRVSTAT,\
					FIELDINFO(struct be_drvr_stats, field)
#define MISCSTAT_INFO(field) 	#field, MISCSTAT,\
					FIELDINFO(struct be_rxf_stats, field)
#define PORTSTAT_INFO(field) 	#field, PORTSTAT,\
					FIELDINFO(struct be_port_rxf_stats, \
						field)
#define ERXSTAT_INFO(field) 	#field, ERXSTAT,\
					FIELDINFO(struct be_erx_stats, field)

static const struct be_ethtool_stat et_stats[] = {
	{NETSTAT_INFO(rx_packets)},
	{NETSTAT_INFO(tx_packets)},
	{NETSTAT_INFO(rx_bytes)},
	{NETSTAT_INFO(tx_bytes)},
	{NETSTAT_INFO(rx_errors)},
	{NETSTAT_INFO(tx_errors)},
	{NETSTAT_INFO(rx_dropped)},
	{NETSTAT_INFO(tx_dropped)},
	{DRVSTAT_INFO(be_tx_reqs)},
	{DRVSTAT_INFO(be_tx_stops)},
	{DRVSTAT_INFO(be_fwd_reqs)},
	{DRVSTAT_INFO(be_tx_wrbs)},
	{DRVSTAT_INFO(be_polls)},
	{DRVSTAT_INFO(be_tx_events)},
	{DRVSTAT_INFO(be_rx_events)},
	{DRVSTAT_INFO(be_tx_compl)},
	{DRVSTAT_INFO(be_rx_compl)},
	{DRVSTAT_INFO(be_ethrx_post_fail)},
	{DRVSTAT_INFO(be_802_3_dropped_frames)},
	{DRVSTAT_INFO(be_802_3_malformed_frames)},
	{DRVSTAT_INFO(be_tx_rate)},
	{DRVSTAT_INFO(be_rx_rate)},
	{PORTSTAT_INFO(rx_unicast_frames)},
	{PORTSTAT_INFO(rx_multicast_frames)},
	{PORTSTAT_INFO(rx_broadcast_frames)},
	{PORTSTAT_INFO(rx_crc_errors)},
	{PORTSTAT_INFO(rx_alignment_symbol_errors)},
	{PORTSTAT_INFO(rx_pause_frames)},
	{PORTSTAT_INFO(rx_control_frames)},
	{PORTSTAT_INFO(rx_in_range_errors)},
	{PORTSTAT_INFO(rx_out_range_errors)},
	{PORTSTAT_INFO(rx_frame_too_long)},
	{PORTSTAT_INFO(rx_address_match_errors)},
	{PORTSTAT_INFO(rx_vlan_mismatch)},
	{PORTSTAT_INFO(rx_dropped_too_small)},
	{PORTSTAT_INFO(rx_dropped_too_short)},
	{PORTSTAT_INFO(rx_dropped_header_too_small)},
	{PORTSTAT_INFO(rx_dropped_tcp_length)},
	{PORTSTAT_INFO(rx_dropped_runt)},
	{PORTSTAT_INFO(rx_fifo_overflow)},
	{PORTSTAT_INFO(rx_input_fifo_overflow)},
	{PORTSTAT_INFO(rx_ip_checksum_errs)},
	{PORTSTAT_INFO(rx_tcp_checksum_errs)},
	{PORTSTAT_INFO(rx_udp_checksum_errs)},
	{PORTSTAT_INFO(rx_non_rss_packets)},
	{PORTSTAT_INFO(rx_ipv4_packets)},
	{PORTSTAT_INFO(rx_ipv6_packets)},
	{PORTSTAT_INFO(tx_unicastframes)},
	{PORTSTAT_INFO(tx_multicastframes)},
	{PORTSTAT_INFO(tx_broadcastframes)},
	{PORTSTAT_INFO(tx_pauseframes)},
	{PORTSTAT_INFO(tx_controlframes)},
	{MISCSTAT_INFO(rx_drops_no_pbuf)},
	{MISCSTAT_INFO(rx_drops_no_txpb)},
	{MISCSTAT_INFO(rx_drops_no_erx_descr)},
	{MISCSTAT_INFO(rx_drops_no_tpre_descr)},
	{MISCSTAT_INFO(rx_drops_too_many_frags)},
	{MISCSTAT_INFO(rx_drops_invalid_ring)},
	{MISCSTAT_INFO(forwarded_packets)},
	{MISCSTAT_INFO(rx_drops_mtu)},
	{ERXSTAT_INFO(rx_drops_no_fragments)},
};
#define ETHTOOL_STATS_NUM ARRAY_SIZE(et_stats)

static void
be_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	strcpy(drvinfo->driver, DRV_NAME);
	strcpy(drvinfo->version, DRV_VER);
	strncpy(drvinfo->fw_version, adapter->fw_ver, FW_VER_LEN);
	strcpy(drvinfo->bus_info, pci_name(adapter->pdev));
	drvinfo->testinfo_len = 0;
	drvinfo->regdump_len = 0;
	drvinfo->eedump_len = 0;
}

static int
be_get_coalesce(struct net_device *netdev, struct ethtool_coalesce *coalesce)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_eq_obj *rx_eq = &adapter->rx_eq;
	struct be_eq_obj *tx_eq = &adapter->tx_eq;

	coalesce->rx_max_coalesced_frames = adapter->max_rx_coal;

	coalesce->rx_coalesce_usecs = rx_eq->cur_eqd;
	coalesce->rx_coalesce_usecs_high = rx_eq->max_eqd;
	coalesce->rx_coalesce_usecs_low = rx_eq->min_eqd;

	coalesce->tx_coalesce_usecs = tx_eq->cur_eqd;
	coalesce->tx_coalesce_usecs_high = tx_eq->max_eqd;
	coalesce->tx_coalesce_usecs_low = tx_eq->min_eqd;

	coalesce->use_adaptive_rx_coalesce = rx_eq->enable_aic;
	coalesce->use_adaptive_tx_coalesce = tx_eq->enable_aic;

	return 0;
}

/*
 * This routine is used to set interrup coalescing delay *as well as*
 * the number of pkts to coalesce for LRO.
 */
static int
be_set_coalesce(struct net_device *netdev, struct ethtool_coalesce *coalesce)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_ctrl_info *ctrl = &adapter->ctrl;
	struct be_eq_obj *rx_eq = &adapter->rx_eq;
	struct be_eq_obj *tx_eq = &adapter->tx_eq;
	u32 tx_max, tx_min, tx_cur;
	u32 rx_max, rx_min, rx_cur;
	int status = 0;

	if (coalesce->use_adaptive_tx_coalesce == 1)
		return -EINVAL;

	adapter->max_rx_coal = coalesce->rx_max_coalesced_frames;
	if (adapter->max_rx_coal > BE_MAX_FRAGS_PER_FRAME)
		adapter->max_rx_coal = BE_MAX_FRAGS_PER_FRAME;

	/* if AIC is being turned on now, start with an EQD of 0 */
	if (rx_eq->enable_aic == 0 &&
		coalesce->use_adaptive_rx_coalesce == 1) {
		rx_eq->cur_eqd = 0;
	}
	rx_eq->enable_aic = coalesce->use_adaptive_rx_coalesce;

	rx_max = coalesce->rx_coalesce_usecs_high;
	rx_min = coalesce->rx_coalesce_usecs_low;
	rx_cur = coalesce->rx_coalesce_usecs;

	tx_max = coalesce->tx_coalesce_usecs_high;
	tx_min = coalesce->tx_coalesce_usecs_low;
	tx_cur = coalesce->tx_coalesce_usecs;

	if (tx_cur > BE_MAX_EQD)
		tx_cur = BE_MAX_EQD;
	if (tx_eq->cur_eqd != tx_cur) {
		status = be_cmd_modify_eqd(ctrl, tx_eq->q.id, tx_cur);
		if (!status)
			tx_eq->cur_eqd = tx_cur;
	}

	if (rx_eq->enable_aic) {
		if (rx_max > BE_MAX_EQD)
			rx_max = BE_MAX_EQD;
		if (rx_min > rx_max)
			rx_min = rx_max;
		rx_eq->max_eqd = rx_max;
		rx_eq->min_eqd = rx_min;
		if (rx_eq->cur_eqd > rx_max)
			rx_eq->cur_eqd = rx_max;
		if (rx_eq->cur_eqd < rx_min)
			rx_eq->cur_eqd = rx_min;
	} else {
		if (rx_cur > BE_MAX_EQD)
			rx_cur = BE_MAX_EQD;
		if (rx_eq->cur_eqd != rx_cur) {
			status = be_cmd_modify_eqd(ctrl, rx_eq->q.id, rx_cur);
			if (!status)
				rx_eq->cur_eqd = rx_cur;
		}
	}
	return 0;
}

static u32 be_get_rx_csum(struct net_device *netdev)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	return adapter->rx_csum;
}

static int be_set_rx_csum(struct net_device *netdev, uint32_t data)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	if (data)
		adapter->rx_csum = true;
	else
		adapter->rx_csum = false;

	return 0;
}

static void
be_get_ethtool_stats(struct net_device *netdev,
		struct ethtool_stats *stats, uint64_t *data)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_drvr_stats *drvr_stats = &adapter->stats.drvr_stats;
	struct be_hw_stats *hw_stats = hw_stats_from_cmd(adapter->stats.cmd.va);
	struct be_rxf_stats *rxf_stats = &hw_stats->rxf;
	struct be_port_rxf_stats *port_stats =
			&rxf_stats->port[adapter->port_num];
	struct net_device_stats *net_stats = &adapter->stats.net_stats;
	struct be_erx_stats *erx_stats = &hw_stats->erx;
	void *p = NULL;
	int i;

	for (i = 0; i < ETHTOOL_STATS_NUM; i++) {
		switch (et_stats[i].type) {
		case NETSTAT:
			p = net_stats;
			break;
		case DRVSTAT:
			p = drvr_stats;
			break;
		case PORTSTAT:
			p = port_stats;
			break;
		case MISCSTAT:
			p = rxf_stats;
			break;
		case ERXSTAT: /* Currently only one ERX stat is provided */
			p = (u32 *)erx_stats + adapter->rx_obj.q.id;
			break;
		}

		p = (u8 *)p + et_stats[i].offset;
		data[i] = (et_stats[i].size == sizeof(u64)) ?
				*(u64 *)p: *(u32 *)p;
	}

	return;
}

static void
be_get_stat_strings(struct net_device *netdev, uint32_t stringset,
		uint8_t *data)
{
	int i;
	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ETHTOOL_STATS_NUM; i++) {
			memcpy(data, et_stats[i].desc, ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		break;
	}
}

static int be_get_stats_count(struct net_device *netdev)
{
	return ETHTOOL_STATS_NUM;
}

static int be_get_settings(struct net_device *netdev, struct ethtool_cmd *ecmd)
{
	ecmd->speed = SPEED_10000;
	ecmd->duplex = DUPLEX_FULL;
	ecmd->autoneg = AUTONEG_DISABLE;
	return 0;
}

static void
be_get_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	ring->rx_max_pending = adapter->rx_obj.q.len;
	ring->tx_max_pending = adapter->tx_obj.q.len;

	ring->rx_pending = atomic_read(&adapter->rx_obj.q.used);
	ring->tx_pending = atomic_read(&adapter->tx_obj.q.used);
}

static void
be_get_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *ecmd)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	be_cmd_get_flow_control(&adapter->ctrl, &ecmd->tx_pause,
		&ecmd->rx_pause);
	ecmd->autoneg = 0;
}

static int
be_set_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *ecmd)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	int status;

	if (ecmd->autoneg != 0)
		return -EINVAL;

	status = be_cmd_set_flow_control(&adapter->ctrl, ecmd->tx_pause,
			ecmd->rx_pause);
	if (!status)
		dev_warn(&adapter->pdev->dev, "Pause param set failed.\n");

	return status;
}

struct ethtool_ops be_ethtool_ops = {
	.get_settings = be_get_settings,
	.get_drvinfo = be_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_coalesce = be_get_coalesce,
	.set_coalesce = be_set_coalesce,
	.get_ringparam = be_get_ringparam,
	.get_pauseparam = be_get_pauseparam,
	.set_pauseparam = be_set_pauseparam,
	.get_rx_csum = be_get_rx_csum,
	.set_rx_csum = be_set_rx_csum,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.set_tx_csum = ethtool_op_set_tx_csum,
	.get_sg = ethtool_op_get_sg,
	.set_sg = ethtool_op_set_sg,
	.get_tso = ethtool_op_get_tso,
	.set_tso = ethtool_op_set_tso,
	.get_strings = be_get_stat_strings,
	.get_stats_count = be_get_stats_count,
	.get_ethtool_stats = be_get_ethtool_stats,
};
