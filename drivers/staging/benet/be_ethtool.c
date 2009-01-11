/*
 * Copyright (C) 2005 - 2008 ServerEngines
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
/*
 * be_ethtool.c
 *
 * 	This file contains various functions that ethtool can use
 * 	to talk to the driver and the BE H/W.
 */

#include "benet.h"

#include <linux/ethtool.h>

static const char benet_gstrings_stats[][ETH_GSTRING_LEN] = {
/* net_device_stats */
	"rx_packets",
	"tx_packets",
	"rx_bytes",
	"tx_bytes",
	"rx_errors",
	"tx_errors",
	"rx_dropped",
	"tx_dropped",
	"multicast",
	"collisions",
	"rx_length_errors",
	"rx_over_errors",
	"rx_crc_errors",
	"rx_frame_errors",
	"rx_fifo_errors",
	"rx_missed_errors",
	"tx_aborted_errors",
	"tx_carrier_errors",
	"tx_fifo_errors",
	"tx_heartbeat_errors",
	"tx_window_errors",
	"rx_compressed",
	"tc_compressed",
/* BE driver Stats */
	"bes_tx_reqs",
	"bes_tx_fails",
	"bes_fwd_reqs",
	"bes_tx_wrbs",
	"bes_interrupts",
	"bes_events",
	"bes_tx_events",
	"bes_rx_events",
	"bes_tx_compl",
	"bes_rx_compl",
	"bes_ethrx_post_fail",
	"bes_802_3_dropped_frames",
	"bes_802_3_malformed_frames",
	"bes_rx_misc_pkts",
	"bes_eth_tx_rate",
	"bes_eth_rx_rate",
	"Num Packets collected",
	"Num Times Flushed",
};

#define NET_DEV_STATS_LEN \
	(sizeof(struct net_device_stats)/sizeof(unsigned long))

#define BENET_STATS_LEN  ARRAY_SIZE(benet_gstrings_stats)

static void
be_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct be_net_object *pnob = netdev_priv(netdev);
	struct be_adapter *adapter = pnob->adapter;

	strncpy(drvinfo->driver, be_driver_name, 32);
	strncpy(drvinfo->version, be_drvr_ver, 32);
	strncpy(drvinfo->fw_version, be_fw_ver, 32);
	strcpy(drvinfo->bus_info, pci_name(adapter->pdev));
	drvinfo->testinfo_len = 0;
	drvinfo->regdump_len = 0;
	drvinfo->eedump_len = 0;
}

static int
be_get_coalesce(struct net_device *netdev, struct ethtool_coalesce *coalesce)
{
	struct be_net_object *pnob = netdev_priv(netdev);
	struct be_adapter *adapter = pnob->adapter;

	coalesce->rx_max_coalesced_frames = adapter->max_rx_coal;

	coalesce->rx_coalesce_usecs = adapter->cur_eqd;
	coalesce->rx_coalesce_usecs_high = adapter->max_eqd;
	coalesce->rx_coalesce_usecs_low = adapter->min_eqd;

	coalesce->tx_coalesce_usecs = adapter->cur_eqd;
	coalesce->tx_coalesce_usecs_high = adapter->max_eqd;
	coalesce->tx_coalesce_usecs_low = adapter->min_eqd;

	coalesce->use_adaptive_rx_coalesce = adapter->enable_aic;
	coalesce->use_adaptive_tx_coalesce = adapter->enable_aic;

	return 0;
}

/*
 * This routine is used to set interrup coalescing delay *as well as*
 * the number of pkts to coalesce for LRO.
 */
static int
be_set_coalesce(struct net_device *netdev, struct ethtool_coalesce *coalesce)
{
	struct be_net_object *pnob = netdev_priv(netdev);
	struct be_adapter *adapter = pnob->adapter;
	struct be_eq_object *eq_objectp;
	u32 max, min, cur;
	int status;

	adapter->max_rx_coal = coalesce->rx_max_coalesced_frames;
	if (adapter->max_rx_coal >= BE_LRO_MAX_PKTS)
		adapter->max_rx_coal = BE_LRO_MAX_PKTS;

	if (adapter->enable_aic == 0 &&
		coalesce->use_adaptive_rx_coalesce == 1) {
		/* if AIC is being turned on now, start with an EQD of 0 */
		adapter->cur_eqd = 0;
	}
	adapter->enable_aic = coalesce->use_adaptive_rx_coalesce;

	/* round off to nearest multiple of 8 */
	max = (((coalesce->rx_coalesce_usecs_high + 4) >> 3) << 3);
	min = (((coalesce->rx_coalesce_usecs_low + 4) >> 3) << 3);
	cur = (((coalesce->rx_coalesce_usecs + 4) >> 3) << 3);

	if (adapter->enable_aic) {
		/* accept low and high if AIC is enabled */
		if (max > MAX_EQD)
			max = MAX_EQD;
		if (min > max)
			min = max;
		adapter->max_eqd = max;
		adapter->min_eqd = min;
		if (adapter->cur_eqd > max)
			adapter->cur_eqd = max;
		if (adapter->cur_eqd < min)
			adapter->cur_eqd = min;
	} else {
		/* accept specified coalesce_usecs only if AIC is disabled */
		if (cur > MAX_EQD)
			cur = MAX_EQD;
		eq_objectp = &pnob->event_q_obj;
		status =
		    be_eq_modify_delay(&pnob->fn_obj, 1, &eq_objectp, &cur,
				       NULL, NULL, NULL);
		if (status == BE_SUCCESS)
			adapter->cur_eqd = cur;
	}
	return 0;
}

static u32 be_get_rx_csum(struct net_device *netdev)
{
	struct be_net_object *pnob = netdev_priv(netdev);
	struct be_adapter *adapter = pnob->adapter;
	return adapter->rx_csum;
}

static int be_set_rx_csum(struct net_device *netdev, uint32_t data)
{
	struct be_net_object *pnob = netdev_priv(netdev);
	struct be_adapter *adapter = pnob->adapter;

	if (data)
		adapter->rx_csum = 1;
	else
		adapter->rx_csum = 0;

	return 0;
}

static void
be_get_strings(struct net_device *netdev, uint32_t stringset, uint8_t *data)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, *benet_gstrings_stats,
		       sizeof(benet_gstrings_stats));
		break;
	}
}

static int be_get_stats_count(struct net_device *netdev)
{
	return BENET_STATS_LEN;
}

static void
be_get_ethtool_stats(struct net_device *netdev,
		     struct ethtool_stats *stats, uint64_t *data)
{
	struct be_net_object *pnob = netdev_priv(netdev);
	struct be_adapter *adapter = pnob->adapter;
	int i;

	benet_get_stats(netdev);

	for (i = 0; i <= NET_DEV_STATS_LEN; i++)
		data[i] = ((unsigned long *)&adapter->benet_stats)[i];

	data[i] = adapter->be_stat.bes_tx_reqs;
	data[i++] = adapter->be_stat.bes_tx_fails;
	data[i++] = adapter->be_stat.bes_fwd_reqs;
	data[i++] = adapter->be_stat.bes_tx_wrbs;

	data[i++] = adapter->be_stat.bes_ints;
	data[i++] = adapter->be_stat.bes_events;
	data[i++] = adapter->be_stat.bes_tx_events;
	data[i++] = adapter->be_stat.bes_rx_events;
	data[i++] = adapter->be_stat.bes_tx_compl;
	data[i++] = adapter->be_stat.bes_rx_compl;
	data[i++] = adapter->be_stat.bes_ethrx_post_fail;
	data[i++] = adapter->be_stat.bes_802_3_dropped_frames;
	data[i++] = adapter->be_stat.bes_802_3_malformed_frames;
	data[i++] = adapter->be_stat.bes_rx_misc_pkts;
	data[i++] = adapter->be_stat.bes_eth_tx_rate;
	data[i++] = adapter->be_stat.bes_eth_rx_rate;
	data[i++] = adapter->be_stat.bes_rx_coal;
	data[i++] = adapter->be_stat.bes_rx_flush;

}

static int be_get_settings(struct net_device *netdev, struct ethtool_cmd *ecmd)
{
	ecmd->speed = SPEED_10000;
	ecmd->duplex = DUPLEX_FULL;
	ecmd->autoneg = AUTONEG_DISABLE;
	return 0;
}

/* Get the Ring parameters from the pnob */
static void
be_get_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring)
{
	struct be_net_object *pnob = netdev_priv(netdev);

	/* Pre Set Maxims */
	ring->rx_max_pending = pnob->rx_q_len;
	ring->rx_mini_max_pending = ring->rx_mini_max_pending;
	ring->rx_jumbo_max_pending = ring->rx_jumbo_max_pending;
	ring->tx_max_pending = pnob->tx_q_len;

	/* Current hardware Settings                */
	ring->rx_pending = atomic_read(&pnob->rx_q_posted);
	ring->rx_mini_pending = ring->rx_mini_pending;
	ring->rx_jumbo_pending = ring->rx_jumbo_pending;
	ring->tx_pending = atomic_read(&pnob->tx_q_used);

}

static void
be_get_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *ecmd)
{
	struct be_net_object *pnob = netdev_priv(netdev);
	bool rxfc, txfc;
	int status;

	status = be_eth_get_flow_control(&pnob->fn_obj, &txfc, &rxfc);
	if (status != BE_SUCCESS) {
		dev_info(&netdev->dev, "Unable to get pause frame settings\n");
		/* return defaults */
		ecmd->rx_pause = 1;
		ecmd->tx_pause = 0;
		ecmd->autoneg = AUTONEG_ENABLE;
		return;
	}

	if (txfc == true)
		ecmd->tx_pause = 1;
	else
		ecmd->tx_pause = 0;

	if (rxfc == true)
		ecmd->rx_pause = 1;
	else
		ecmd->rx_pause = 0;

	ecmd->autoneg = AUTONEG_ENABLE;
}

static int
be_set_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *ecmd)
{
	struct be_net_object *pnob = netdev_priv(netdev);
	bool txfc, rxfc;
	int status;

	if (ecmd->autoneg != AUTONEG_ENABLE)
		return -EINVAL;

	if (ecmd->tx_pause)
		txfc = true;
	else
		txfc = false;

	if (ecmd->rx_pause)
		rxfc = true;
	else
		rxfc = false;

	status = be_eth_set_flow_control(&pnob->fn_obj, txfc, rxfc);
	if (status != BE_SUCCESS) {
		dev_info(&netdev->dev, "Unable to set pause frame settings\n");
		return -1;
	}
	return 0;
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
	.get_strings = be_get_strings,
	.get_stats_count = be_get_stats_count,
	.get_ethtool_stats = be_get_ethtool_stats,
};
