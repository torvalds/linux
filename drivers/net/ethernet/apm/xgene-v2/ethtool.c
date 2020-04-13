// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Applied Micro X-Gene SoC Ethernet v2 Driver
 *
 * Copyright (c) 2017, Applied Micro Circuits Corporation
 * Author(s): Iyappan Subramanian <isubramanian@apm.com>
 *	      Keyur Chudgar <kchudgar@apm.com>
 */

#include "main.h"

#define XGE_STAT(m)		{ #m, offsetof(struct xge_pdata, stats.m) }
#define XGE_EXTD_STAT(m, n)					\
	{							\
		#m,						\
		n,						\
		0						\
	}

static const struct xge_gstrings_stats gstrings_stats[] = {
	XGE_STAT(rx_packets),
	XGE_STAT(tx_packets),
	XGE_STAT(rx_bytes),
	XGE_STAT(tx_bytes),
	XGE_STAT(rx_errors)
};

static struct xge_gstrings_extd_stats gstrings_extd_stats[] = {
	XGE_EXTD_STAT(tx_rx_64b_frame_cntr, TR64),
	XGE_EXTD_STAT(tx_rx_127b_frame_cntr, TR127),
	XGE_EXTD_STAT(tx_rx_255b_frame_cntr, TR255),
	XGE_EXTD_STAT(tx_rx_511b_frame_cntr, TR511),
	XGE_EXTD_STAT(tx_rx_1023b_frame_cntr, TR1K),
	XGE_EXTD_STAT(tx_rx_1518b_frame_cntr, TRMAX),
	XGE_EXTD_STAT(tx_rx_1522b_frame_cntr, TRMGV),
	XGE_EXTD_STAT(rx_fcs_error_cntr, RFCS),
	XGE_EXTD_STAT(rx_multicast_pkt_cntr, RMCA),
	XGE_EXTD_STAT(rx_broadcast_pkt_cntr, RBCA),
	XGE_EXTD_STAT(rx_ctrl_frame_pkt_cntr, RXCF),
	XGE_EXTD_STAT(rx_pause_frame_pkt_cntr, RXPF),
	XGE_EXTD_STAT(rx_unk_opcode_cntr, RXUO),
	XGE_EXTD_STAT(rx_align_err_cntr, RALN),
	XGE_EXTD_STAT(rx_frame_len_err_cntr, RFLR),
	XGE_EXTD_STAT(rx_code_err_cntr, RCDE),
	XGE_EXTD_STAT(rx_carrier_sense_err_cntr, RCSE),
	XGE_EXTD_STAT(rx_undersize_pkt_cntr, RUND),
	XGE_EXTD_STAT(rx_oversize_pkt_cntr, ROVR),
	XGE_EXTD_STAT(rx_fragments_cntr, RFRG),
	XGE_EXTD_STAT(rx_jabber_cntr, RJBR),
	XGE_EXTD_STAT(rx_dropped_pkt_cntr, RDRP),
	XGE_EXTD_STAT(tx_multicast_pkt_cntr, TMCA),
	XGE_EXTD_STAT(tx_broadcast_pkt_cntr, TBCA),
	XGE_EXTD_STAT(tx_pause_ctrl_frame_cntr, TXPF),
	XGE_EXTD_STAT(tx_defer_pkt_cntr, TDFR),
	XGE_EXTD_STAT(tx_excv_defer_pkt_cntr, TEDF),
	XGE_EXTD_STAT(tx_single_col_pkt_cntr, TSCL),
	XGE_EXTD_STAT(tx_multi_col_pkt_cntr, TMCL),
	XGE_EXTD_STAT(tx_late_col_pkt_cntr, TLCL),
	XGE_EXTD_STAT(tx_excv_col_pkt_cntr, TXCL),
	XGE_EXTD_STAT(tx_total_col_cntr, TNCL),
	XGE_EXTD_STAT(tx_pause_frames_hnrd_cntr, TPFH),
	XGE_EXTD_STAT(tx_drop_frame_cntr, TDRP),
	XGE_EXTD_STAT(tx_jabber_frame_cntr, TJBR),
	XGE_EXTD_STAT(tx_fcs_error_cntr, TFCS),
	XGE_EXTD_STAT(tx_ctrl_frame_cntr, TXCF),
	XGE_EXTD_STAT(tx_oversize_frame_cntr, TOVR),
	XGE_EXTD_STAT(tx_undersize_frame_cntr, TUND),
	XGE_EXTD_STAT(tx_fragments_cntr, TFRG)
};

#define XGE_STATS_LEN		ARRAY_SIZE(gstrings_stats)
#define XGE_EXTD_STATS_LEN	ARRAY_SIZE(gstrings_extd_stats)

static void xge_mac_get_extd_stats(struct xge_pdata *pdata)
{
	u32 data;
	int i;

	for (i = 0; i < XGE_EXTD_STATS_LEN; i++) {
		data = xge_rd_csr(pdata, gstrings_extd_stats[i].addr);
		gstrings_extd_stats[i].value += data;
	}
}

static void xge_get_drvinfo(struct net_device *ndev,
			    struct ethtool_drvinfo *info)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct platform_device *pdev = pdata->pdev;

	strcpy(info->driver, "xgene-enet-v2");
	sprintf(info->bus_info, "%s", pdev->name);
}

static void xge_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	u8 *p = data;
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < XGE_STATS_LEN; i++) {
		memcpy(p, gstrings_stats[i].name, ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
	}

	for (i = 0; i < XGE_EXTD_STATS_LEN; i++) {
		memcpy(p, gstrings_extd_stats[i].name, ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
	}
}

static int xge_get_sset_count(struct net_device *ndev, int sset)
{
	if (sset != ETH_SS_STATS)
		return -EINVAL;

	return XGE_STATS_LEN + XGE_EXTD_STATS_LEN;
}

static void xge_get_ethtool_stats(struct net_device *ndev,
				  struct ethtool_stats *dummy,
				  u64 *data)
{
	void *pdata = netdev_priv(ndev);
	int i;

	for (i = 0; i < XGE_STATS_LEN; i++)
		*data++ = *(u64 *)(pdata + gstrings_stats[i].offset);

	xge_mac_get_extd_stats(pdata);

	for (i = 0; i < XGE_EXTD_STATS_LEN; i++)
		*data++ = gstrings_extd_stats[i].value;
}

static int xge_get_link_ksettings(struct net_device *ndev,
				  struct ethtool_link_ksettings *cmd)
{
	struct phy_device *phydev = ndev->phydev;

	if (!phydev)
		return -ENODEV;

	phy_ethtool_ksettings_get(phydev, cmd);

	return 0;
}

static int xge_set_link_ksettings(struct net_device *ndev,
				  const struct ethtool_link_ksettings *cmd)
{
	struct phy_device *phydev = ndev->phydev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_ksettings_set(phydev, cmd);
}

static const struct ethtool_ops xge_ethtool_ops = {
	.get_drvinfo = xge_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_strings = xge_get_strings,
	.get_sset_count = xge_get_sset_count,
	.get_ethtool_stats = xge_get_ethtool_stats,
	.get_link_ksettings = xge_get_link_ksettings,
	.set_link_ksettings = xge_set_link_ksettings,
};

void xge_set_ethtool_ops(struct net_device *ndev)
{
	ndev->ethtool_ops = &xge_ethtool_ops;
}
