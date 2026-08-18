// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * NXP NETC switch driver
 * Copyright 2025-2026 NXP
 */

#include <linux/ethtool_netlink.h>

#include "netc_switch.h"

static const struct ethtool_rmon_hist_range netc_rmon_ranges[] = {
	{   64,   64 },
	{   65,  127 },
	{  128,  255 },
	{  256,  511 },
	{  512, 1023 },
	{ 1024, 1522 },
	{ 1523, NETC_MAX_FRAME_LEN },
	{ }
};

static const struct netc_port_stat netc_port_counters[] = {
	{ NETC_PTGSLACR,	"port gate late arrival frames" },
	{ NETC_PSDFTCR,	"port SDF transmit frames" },
	{ NETC_PSDFDDCR,	"port SDF drop duplicate frames" },
	{ NETC_PRXDCR,		"port rx discard frames" },
	{ NETC_PRXDCRRR,	"port rx discard read-reset" },
	{ NETC_PRXDCRR0,	"port rx discard reason 0" },
	{ NETC_PRXDCRR1,	"port rx discard reason 1" },
	{ NETC_PTXDCR,		"port tx discard frames" },
	{ NETC_BPDCR,		"bridge port discard frames" },
};

static const struct netc_port_stat netc_emac_counters[] = {
	{ NETC_PM_ROCT(0),	"eMAC rx octets" },
	{ NETC_PM_RVLAN(0),	"eMAC rx VLAN frames" },
	{ NETC_PM_RERR(0),	"eMAC rx frame errors" },
	{ NETC_PM_RUCA(0),	"eMAC rx unicast frames" },
	{ NETC_PM_RDRP(0),	"eMAC rx dropped packets" },
	{ NETC_PM_RPKT(0),	"eMAC rx packets" },
	{ NETC_PM_TOCT(0),	"eMAC tx octets" },
	{ NETC_PM_TVLAN(0),	"eMAC tx VLAN frames" },
	{ NETC_PM_TFCS(0),	"eMAC tx FCS errors" },
	{ NETC_PM_TUCA(0),	"eMAC tx unicast frames" },
	{ NETC_PM_TPKT(0),	"eMAC tx packets" },
	{ NETC_PM_TUND(0),	"eMAC tx undersized packets" },
	{ NETC_PM_TIOCT(0),	"eMAC tx invalid octets" },
};

static const struct netc_port_stat netc_pmac_counters[] = {
	{ NETC_PM_ROCT(1),	"pMAC rx octets" },
	{ NETC_PM_RVLAN(1),	"pMAC rx VLAN frames" },
	{ NETC_PM_RERR(1),	"pMAC rx frame errors" },
	{ NETC_PM_RUCA(1),	"pMAC rx unicast frames" },
	{ NETC_PM_RDRP(1),	"pMAC rx dropped packets" },
	{ NETC_PM_RPKT(1),	"pMAC rx packets" },
	{ NETC_PM_TOCT(1),	"pMAC tx octets" },
	{ NETC_PM_TVLAN(1),	"pMAC tx VLAN frames" },
	{ NETC_PM_TFCS(1),	"pMAC tx FCS errors" },
	{ NETC_PM_TUCA(1),	"pMAC tx unicast frames" },
	{ NETC_PM_TPKT(1),	"pMAC tx packets" },
	{ NETC_PM_TUND(1),	"pMAC tx undersized packets" },
	{ NETC_PM_TIOCT(1),	"pMAC tx invalid octets" },
};

static void netc_port_pause_stats(struct netc_port *np, int mac,
				  struct ethtool_pause_stats *stats)
{
	if (mac && !np->caps.pmac)
		return;

	stats->tx_pause_frames = netc_port_rd64(np, NETC_PM_TXPF(mac));
	stats->rx_pause_frames = netc_port_rd64(np, NETC_PM_RXPF(mac));
}

void netc_port_get_pause_stats(struct dsa_switch *ds, int port,
			       struct ethtool_pause_stats *pause_stats)
{
	struct netc_port *np = NETC_PORT(ds, port);
	struct net_device *ndev;

	switch (pause_stats->src) {
	case ETHTOOL_MAC_STATS_SRC_EMAC:
		netc_port_pause_stats(np, 0, pause_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_PMAC:
		netc_port_pause_stats(np, 1, pause_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_AGGREGATE:
		ndev = dsa_to_port(ds, port)->user;
		ethtool_aggregate_pause_stats(ndev, pause_stats);
		break;
	}
}

static void netc_port_rmon_stats(struct netc_port *np, int mac,
				 struct ethtool_rmon_stats *stats)
{
	if (mac && !np->caps.pmac)
		return;

	stats->undersize_pkts = netc_port_rd64(np, NETC_PM_RUND(mac));
	stats->oversize_pkts = netc_port_rd64(np, NETC_PM_ROVR(mac));
	stats->fragments = netc_port_rd64(np, NETC_PM_RFRG(mac));
	stats->jabbers = netc_port_rd64(np, NETC_PM_RJBR(mac));

	stats->hist[0] = netc_port_rd64(np, NETC_PM_R64(mac));
	stats->hist[1] = netc_port_rd64(np, NETC_PM_R127(mac));
	stats->hist[2] = netc_port_rd64(np, NETC_PM_R255(mac));
	stats->hist[3] = netc_port_rd64(np, NETC_PM_R511(mac));
	stats->hist[4] = netc_port_rd64(np, NETC_PM_R1023(mac));
	stats->hist[5] = netc_port_rd64(np, NETC_PM_R1522(mac));
	stats->hist[6] = netc_port_rd64(np, NETC_PM_R1523X(mac));

	stats->hist_tx[0] = netc_port_rd64(np, NETC_PM_T64(mac));
	stats->hist_tx[1] = netc_port_rd64(np, NETC_PM_T127(mac));
	stats->hist_tx[2] = netc_port_rd64(np, NETC_PM_T255(mac));
	stats->hist_tx[3] = netc_port_rd64(np, NETC_PM_T511(mac));
	stats->hist_tx[4] = netc_port_rd64(np, NETC_PM_T1023(mac));
	stats->hist_tx[5] = netc_port_rd64(np, NETC_PM_T1522(mac));
	stats->hist_tx[6] = netc_port_rd64(np, NETC_PM_T1523X(mac));
}

void netc_port_get_rmon_stats(struct dsa_switch *ds, int port,
			      struct ethtool_rmon_stats *rmon_stats,
			      const struct ethtool_rmon_hist_range **ranges)
{
	struct netc_port *np = NETC_PORT(ds, port);
	struct net_device *ndev;

	*ranges = netc_rmon_ranges;

	switch (rmon_stats->src) {
	case ETHTOOL_MAC_STATS_SRC_EMAC:
		netc_port_rmon_stats(np, 0, rmon_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_PMAC:
		netc_port_rmon_stats(np, 1, rmon_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_AGGREGATE:
		ndev = dsa_to_port(ds, port)->user;
		ethtool_aggregate_rmon_stats(ndev, rmon_stats);
		break;
	}
}

static void netc_port_ctrl_stats(struct netc_port *np, int mac,
				 struct ethtool_eth_ctrl_stats *stats)
{
	if (mac && !np->caps.pmac)
		return;

	stats->MACControlFramesTransmitted =
		netc_port_rd64(np, NETC_PM_TCNP(mac));
	stats->MACControlFramesReceived =
		netc_port_rd64(np, NETC_PM_RCNP(mac));
}

void netc_port_get_eth_ctrl_stats(struct dsa_switch *ds, int port,
				  struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct netc_port *np = NETC_PORT(ds, port);
	struct net_device *ndev;

	switch (ctrl_stats->src) {
	case ETHTOOL_MAC_STATS_SRC_EMAC:
		netc_port_ctrl_stats(np, 0, ctrl_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_PMAC:
		netc_port_ctrl_stats(np, 1, ctrl_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_AGGREGATE:
		ndev = dsa_to_port(ds, port)->user;
		ethtool_aggregate_ctrl_stats(ndev, ctrl_stats);
		break;
	}
}

static void netc_port_mac_stats(struct netc_port *np, int mac,
				struct ethtool_eth_mac_stats *stats)
{
	if (mac && !np->caps.pmac)
		return;

	stats->FramesTransmittedOK = netc_port_rd64(np, NETC_PM_TFRM(mac));
	stats->SingleCollisionFrames = netc_port_rd64(np, NETC_PM_TSCOL(mac));
	stats->MultipleCollisionFrames =
		netc_port_rd64(np, NETC_PM_TMCOL(mac));
	stats->FramesReceivedOK = netc_port_rd64(np, NETC_PM_RFRM(mac));
	stats->FrameCheckSequenceErrors =
		netc_port_rd64(np, NETC_PM_RFCS(mac));
	stats->AlignmentErrors = netc_port_rd64(np, NETC_PM_RALN(mac));
	stats->OctetsTransmittedOK = netc_port_rd64(np, NETC_PM_TEOCT(mac));
	stats->FramesWithDeferredXmissions =
		netc_port_rd64(np, NETC_PM_TDFR(mac));
	stats->LateCollisions = netc_port_rd64(np, NETC_PM_TLCOL(mac));
	stats->FramesAbortedDueToXSColls =
		netc_port_rd64(np, NETC_PM_TECOL(mac));
	stats->FramesLostDueToIntMACXmitError =
		netc_port_rd64(np, NETC_PM_TERR(mac));
	stats->OctetsReceivedOK = netc_port_rd64(np, NETC_PM_REOCT(mac));
	stats->FramesLostDueToIntMACRcvError =
		netc_port_rd64(np, NETC_PM_RDRNTP(mac));
	stats->MulticastFramesXmittedOK =
		netc_port_rd64(np, NETC_PM_TMCA(mac));
	stats->BroadcastFramesXmittedOK =
		netc_port_rd64(np, NETC_PM_TBCA(mac));
	stats->MulticastFramesReceivedOK =
		netc_port_rd64(np, NETC_PM_RMCA(mac));
	stats->BroadcastFramesReceivedOK =
		netc_port_rd64(np, NETC_PM_RBCA(mac));
	stats->FramesWithExcessiveDeferral =
		netc_port_rd64(np, NETC_PM_TEDFR(mac));
}

void netc_port_get_eth_mac_stats(struct dsa_switch *ds, int port,
				 struct ethtool_eth_mac_stats *mac_stats)
{
	struct netc_port *np = NETC_PORT(ds, port);
	struct net_device *ndev;

	switch (mac_stats->src) {
	case ETHTOOL_MAC_STATS_SRC_EMAC:
		netc_port_mac_stats(np, 0, mac_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_PMAC:
		netc_port_mac_stats(np, 1, mac_stats);
		break;
	case ETHTOOL_MAC_STATS_SRC_AGGREGATE:
		ndev = dsa_to_port(ds, port)->user;
		ethtool_aggregate_mac_stats(ndev, mac_stats);
		break;
	}
}

int netc_port_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	struct netc_port *np = NETC_PORT(ds, port);
	int size;

	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;

	size = ARRAY_SIZE(netc_port_counters) +
	       ARRAY_SIZE(netc_emac_counters);

	if (np->caps.pmac)
		size += ARRAY_SIZE(netc_pmac_counters);

	return size;
}

void netc_port_get_strings(struct dsa_switch *ds, int port,
			   u32 sset, u8 *data)
{
	struct netc_port *np = NETC_PORT(ds, port);
	int i;

	if (sset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(netc_port_counters); i++)
		ethtool_cpy(&data, netc_port_counters[i].name);

	for (i = 0; i < ARRAY_SIZE(netc_emac_counters); i++)
		ethtool_cpy(&data, netc_emac_counters[i].name);

	if (!np->caps.pmac)
		return;

	for (i = 0; i < ARRAY_SIZE(netc_pmac_counters); i++)
		ethtool_cpy(&data, netc_pmac_counters[i].name);
}

void netc_port_get_ethtool_stats(struct dsa_switch *ds, int port, u64 *data)
{
	struct netc_port *np = NETC_PORT(ds, port);
	int i;

	for (i = 0; i < ARRAY_SIZE(netc_port_counters); i++)
		*data++ = netc_port_rd(np, netc_port_counters[i].reg);

	for (i = 0; i < ARRAY_SIZE(netc_emac_counters); i++)
		*data++ = netc_port_rd64(np, netc_emac_counters[i].reg);

	if (!np->caps.pmac)
		return;

	for (i = 0; i < ARRAY_SIZE(netc_pmac_counters); i++)
		*data++ = netc_port_rd64(np, netc_pmac_counters[i].reg);
}
