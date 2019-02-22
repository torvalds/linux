// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2018 Netronome Systems, Inc. */

/*
 * nfp_net_ethtool.c
 * Netronome network device driver: ethtool support
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 *          Brad Petrus <brad.petrus@netronome.com>
 */

#include <linux/bitfield.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/ethtool.h>
#include <linux/firmware.h>

#include "nfpcore/nfp.h"
#include "nfpcore/nfp_nsp.h"
#include "nfp_app.h"
#include "nfp_main.h"
#include "nfp_net_ctrl.h"
#include "nfp_net.h"
#include "nfp_port.h"

struct nfp_et_stat {
	char name[ETH_GSTRING_LEN];
	int off;
};

static const struct nfp_et_stat nfp_net_et_stats[] = {
	/* Stats from the device */
	{ "dev_rx_discards",	NFP_NET_CFG_STATS_RX_DISCARDS },
	{ "dev_rx_errors",	NFP_NET_CFG_STATS_RX_ERRORS },
	{ "dev_rx_bytes",	NFP_NET_CFG_STATS_RX_OCTETS },
	{ "dev_rx_uc_bytes",	NFP_NET_CFG_STATS_RX_UC_OCTETS },
	{ "dev_rx_mc_bytes",	NFP_NET_CFG_STATS_RX_MC_OCTETS },
	{ "dev_rx_bc_bytes",	NFP_NET_CFG_STATS_RX_BC_OCTETS },
	{ "dev_rx_pkts",	NFP_NET_CFG_STATS_RX_FRAMES },
	{ "dev_rx_mc_pkts",	NFP_NET_CFG_STATS_RX_MC_FRAMES },
	{ "dev_rx_bc_pkts",	NFP_NET_CFG_STATS_RX_BC_FRAMES },

	{ "dev_tx_discards",	NFP_NET_CFG_STATS_TX_DISCARDS },
	{ "dev_tx_errors",	NFP_NET_CFG_STATS_TX_ERRORS },
	{ "dev_tx_bytes",	NFP_NET_CFG_STATS_TX_OCTETS },
	{ "dev_tx_uc_bytes",	NFP_NET_CFG_STATS_TX_UC_OCTETS },
	{ "dev_tx_mc_bytes",	NFP_NET_CFG_STATS_TX_MC_OCTETS },
	{ "dev_tx_bc_bytes",	NFP_NET_CFG_STATS_TX_BC_OCTETS },
	{ "dev_tx_pkts",	NFP_NET_CFG_STATS_TX_FRAMES },
	{ "dev_tx_mc_pkts",	NFP_NET_CFG_STATS_TX_MC_FRAMES },
	{ "dev_tx_bc_pkts",	NFP_NET_CFG_STATS_TX_BC_FRAMES },

	{ "bpf_pass_pkts",	NFP_NET_CFG_STATS_APP0_FRAMES },
	{ "bpf_pass_bytes",	NFP_NET_CFG_STATS_APP0_BYTES },
	/* see comments in outro functions in nfp_bpf_jit.c to find out
	 * how different BPF modes use app-specific counters
	 */
	{ "bpf_app1_pkts",	NFP_NET_CFG_STATS_APP1_FRAMES },
	{ "bpf_app1_bytes",	NFP_NET_CFG_STATS_APP1_BYTES },
	{ "bpf_app2_pkts",	NFP_NET_CFG_STATS_APP2_FRAMES },
	{ "bpf_app2_bytes",	NFP_NET_CFG_STATS_APP2_BYTES },
	{ "bpf_app3_pkts",	NFP_NET_CFG_STATS_APP3_FRAMES },
	{ "bpf_app3_bytes",	NFP_NET_CFG_STATS_APP3_BYTES },
};

static const struct nfp_et_stat nfp_mac_et_stats[] = {
	{ "rx_octets",			NFP_MAC_STATS_RX_IN_OCTETS, },
	{ "rx_frame_too_long_errors",
			NFP_MAC_STATS_RX_FRAME_TOO_LONG_ERRORS, },
	{ "rx_range_length_errors",	NFP_MAC_STATS_RX_RANGE_LENGTH_ERRORS, },
	{ "rx_vlan_received_ok",	NFP_MAC_STATS_RX_VLAN_RECEIVED_OK, },
	{ "rx_errors",			NFP_MAC_STATS_RX_IN_ERRORS, },
	{ "rx_broadcast_pkts",		NFP_MAC_STATS_RX_IN_BROADCAST_PKTS, },
	{ "rx_drop_events",		NFP_MAC_STATS_RX_DROP_EVENTS, },
	{ "rx_alignment_errors",	NFP_MAC_STATS_RX_ALIGNMENT_ERRORS, },
	{ "rx_pause_mac_ctrl_frames",
			NFP_MAC_STATS_RX_PAUSE_MAC_CTRL_FRAMES, },
	{ "rx_frames_received_ok",	NFP_MAC_STATS_RX_FRAMES_RECEIVED_OK, },
	{ "rx_frame_check_sequence_errors",
			NFP_MAC_STATS_RX_FRAME_CHECK_SEQUENCE_ERRORS, },
	{ "rx_unicast_pkts",		NFP_MAC_STATS_RX_UNICAST_PKTS, },
	{ "rx_multicast_pkts",		NFP_MAC_STATS_RX_MULTICAST_PKTS, },
	{ "rx_pkts",			NFP_MAC_STATS_RX_PKTS, },
	{ "rx_undersize_pkts",		NFP_MAC_STATS_RX_UNDERSIZE_PKTS, },
	{ "rx_pkts_64_octets",		NFP_MAC_STATS_RX_PKTS_64_OCTETS, },
	{ "rx_pkts_65_to_127_octets",
			NFP_MAC_STATS_RX_PKTS_65_TO_127_OCTETS, },
	{ "rx_pkts_128_to_255_octets",
			NFP_MAC_STATS_RX_PKTS_128_TO_255_OCTETS, },
	{ "rx_pkts_256_to_511_octets",
			NFP_MAC_STATS_RX_PKTS_256_TO_511_OCTETS, },
	{ "rx_pkts_512_to_1023_octets",
			NFP_MAC_STATS_RX_PKTS_512_TO_1023_OCTETS, },
	{ "rx_pkts_1024_to_1518_octets",
			NFP_MAC_STATS_RX_PKTS_1024_TO_1518_OCTETS, },
	{ "rx_pkts_1519_to_max_octets",
			NFP_MAC_STATS_RX_PKTS_1519_TO_MAX_OCTETS, },
	{ "rx_jabbers",			NFP_MAC_STATS_RX_JABBERS, },
	{ "rx_fragments",		NFP_MAC_STATS_RX_FRAGMENTS, },
	{ "rx_oversize_pkts",		NFP_MAC_STATS_RX_OVERSIZE_PKTS, },
	{ "rx_pause_frames_class0",	NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS0, },
	{ "rx_pause_frames_class1",	NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS1, },
	{ "rx_pause_frames_class2",	NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS2, },
	{ "rx_pause_frames_class3",	NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS3, },
	{ "rx_pause_frames_class4",	NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS4, },
	{ "rx_pause_frames_class5",	NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS5, },
	{ "rx_pause_frames_class6",	NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS6, },
	{ "rx_pause_frames_class7",	NFP_MAC_STATS_RX_PAUSE_FRAMES_CLASS7, },
	{ "rx_mac_ctrl_frames_received",
			NFP_MAC_STATS_RX_MAC_CTRL_FRAMES_RECEIVED, },
	{ "rx_mac_head_drop",		NFP_MAC_STATS_RX_MAC_HEAD_DROP, },
	{ "tx_queue_drop",		NFP_MAC_STATS_TX_QUEUE_DROP, },
	{ "tx_octets",			NFP_MAC_STATS_TX_OUT_OCTETS, },
	{ "tx_vlan_transmitted_ok",	NFP_MAC_STATS_TX_VLAN_TRANSMITTED_OK, },
	{ "tx_errors",			NFP_MAC_STATS_TX_OUT_ERRORS, },
	{ "tx_broadcast_pkts",		NFP_MAC_STATS_TX_BROADCAST_PKTS, },
	{ "tx_pause_mac_ctrl_frames",
			NFP_MAC_STATS_TX_PAUSE_MAC_CTRL_FRAMES, },
	{ "tx_frames_transmitted_ok",
			NFP_MAC_STATS_TX_FRAMES_TRANSMITTED_OK, },
	{ "tx_unicast_pkts",		NFP_MAC_STATS_TX_UNICAST_PKTS, },
	{ "tx_multicast_pkts",		NFP_MAC_STATS_TX_MULTICAST_PKTS, },
	{ "tx_pkts_64_octets",		NFP_MAC_STATS_TX_PKTS_64_OCTETS, },
	{ "tx_pkts_65_to_127_octets",
			NFP_MAC_STATS_TX_PKTS_65_TO_127_OCTETS, },
	{ "tx_pkts_128_to_255_octets",
			NFP_MAC_STATS_TX_PKTS_128_TO_255_OCTETS, },
	{ "tx_pkts_256_to_511_octets",
			NFP_MAC_STATS_TX_PKTS_256_TO_511_OCTETS, },
	{ "tx_pkts_512_to_1023_octets",
			NFP_MAC_STATS_TX_PKTS_512_TO_1023_OCTETS, },
	{ "tx_pkts_1024_to_1518_octets",
			NFP_MAC_STATS_TX_PKTS_1024_TO_1518_OCTETS, },
	{ "tx_pkts_1519_to_max_octets",
			NFP_MAC_STATS_TX_PKTS_1519_TO_MAX_OCTETS, },
	{ "tx_pause_frames_class0",	NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS0, },
	{ "tx_pause_frames_class1",	NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS1, },
	{ "tx_pause_frames_class2",	NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS2, },
	{ "tx_pause_frames_class3",	NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS3, },
	{ "tx_pause_frames_class4",	NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS4, },
	{ "tx_pause_frames_class5",	NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS5, },
	{ "tx_pause_frames_class6",	NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS6, },
	{ "tx_pause_frames_class7",	NFP_MAC_STATS_TX_PAUSE_FRAMES_CLASS7, },
};

#define NN_ET_GLOBAL_STATS_LEN ARRAY_SIZE(nfp_net_et_stats)
#define NN_ET_SWITCH_STATS_LEN 9
#define NN_RVEC_GATHER_STATS	9
#define NN_RVEC_PER_Q_STATS	3

static void nfp_net_get_nspinfo(struct nfp_app *app, char *version)
{
	struct nfp_nsp *nsp;

	if (!app)
		return;

	nsp = nfp_nsp_open(app->cpp);
	if (IS_ERR(nsp))
		return;

	snprintf(version, ETHTOOL_FWVERS_LEN, "%hu.%hu",
		 nfp_nsp_get_abi_ver_major(nsp),
		 nfp_nsp_get_abi_ver_minor(nsp));

	nfp_nsp_close(nsp);
}

static void
nfp_get_drvinfo(struct nfp_app *app, struct pci_dev *pdev,
		const char *vnic_version, struct ethtool_drvinfo *drvinfo)
{
	char nsp_version[ETHTOOL_FWVERS_LEN] = {};

	strlcpy(drvinfo->driver, pdev->driver->name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, nfp_driver_version, sizeof(drvinfo->version));

	nfp_net_get_nspinfo(app, nsp_version);
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%s %s %s %s", vnic_version, nsp_version,
		 nfp_app_mip_name(app), nfp_app_name(app));
}

static void
nfp_net_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	char vnic_version[ETHTOOL_FWVERS_LEN] = {};
	struct nfp_net *nn = netdev_priv(netdev);

	snprintf(vnic_version, sizeof(vnic_version), "%d.%d.%d.%d",
		 nn->fw_ver.resv, nn->fw_ver.class,
		 nn->fw_ver.major, nn->fw_ver.minor);
	strlcpy(drvinfo->bus_info, pci_name(nn->pdev),
		sizeof(drvinfo->bus_info));

	nfp_get_drvinfo(nn->app, nn->pdev, vnic_version, drvinfo);
}

static void
nfp_app_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct nfp_app *app = nfp_app_from_netdev(netdev);

	strlcpy(drvinfo->bus_info, pci_name(app->pdev),
		sizeof(drvinfo->bus_info));
	nfp_get_drvinfo(app, app->pdev, "*", drvinfo);
}

static void
nfp_net_set_fec_link_mode(struct nfp_eth_table_port *eth_port,
			  struct ethtool_link_ksettings *c)
{
	unsigned int modes;

	ethtool_link_ksettings_add_link_mode(c, supported, FEC_NONE);
	if (!nfp_eth_can_support_fec(eth_port)) {
		ethtool_link_ksettings_add_link_mode(c, advertising, FEC_NONE);
		return;
	}

	modes = nfp_eth_supported_fec_modes(eth_port);
	if (modes & NFP_FEC_BASER) {
		ethtool_link_ksettings_add_link_mode(c, supported, FEC_BASER);
		ethtool_link_ksettings_add_link_mode(c, advertising, FEC_BASER);
	}

	if (modes & NFP_FEC_REED_SOLOMON) {
		ethtool_link_ksettings_add_link_mode(c, supported, FEC_RS);
		ethtool_link_ksettings_add_link_mode(c, advertising, FEC_RS);
	}
}

/**
 * nfp_net_get_link_ksettings - Get Link Speed settings
 * @netdev:	network interface device structure
 * @cmd:	ethtool command
 *
 * Reports speed settings based on info in the BAR provided by the fw.
 */
static int
nfp_net_get_link_ksettings(struct net_device *netdev,
			   struct ethtool_link_ksettings *cmd)
{
	static const u32 ls_to_ethtool[] = {
		[NFP_NET_CFG_STS_LINK_RATE_UNSUPPORTED]	= 0,
		[NFP_NET_CFG_STS_LINK_RATE_UNKNOWN]	= SPEED_UNKNOWN,
		[NFP_NET_CFG_STS_LINK_RATE_1G]		= SPEED_1000,
		[NFP_NET_CFG_STS_LINK_RATE_10G]		= SPEED_10000,
		[NFP_NET_CFG_STS_LINK_RATE_25G]		= SPEED_25000,
		[NFP_NET_CFG_STS_LINK_RATE_40G]		= SPEED_40000,
		[NFP_NET_CFG_STS_LINK_RATE_50G]		= SPEED_50000,
		[NFP_NET_CFG_STS_LINK_RATE_100G]	= SPEED_100000,
	};
	struct nfp_eth_table_port *eth_port;
	struct nfp_port *port;
	struct nfp_net *nn;
	u32 sts, ls;

	/* Init to unknowns */
	ethtool_link_ksettings_add_link_mode(cmd, supported, FIBRE);
	cmd->base.port = PORT_OTHER;
	cmd->base.speed = SPEED_UNKNOWN;
	cmd->base.duplex = DUPLEX_UNKNOWN;

	port = nfp_port_from_netdev(netdev);
	eth_port = nfp_port_get_eth_port(port);
	if (eth_port) {
		cmd->base.autoneg = eth_port->aneg != NFP_ANEG_DISABLED ?
			AUTONEG_ENABLE : AUTONEG_DISABLE;
		nfp_net_set_fec_link_mode(eth_port, cmd);
	}

	if (!netif_carrier_ok(netdev))
		return 0;

	/* Use link speed from ETH table if available, otherwise try the BAR */
	if (eth_port) {
		cmd->base.port = eth_port->port_type;
		cmd->base.speed = eth_port->speed;
		cmd->base.duplex = DUPLEX_FULL;
		return 0;
	}

	if (!nfp_netdev_is_nfp_net(netdev))
		return -EOPNOTSUPP;
	nn = netdev_priv(netdev);

	sts = nn_readl(nn, NFP_NET_CFG_STS);

	ls = FIELD_GET(NFP_NET_CFG_STS_LINK_RATE, sts);
	if (ls == NFP_NET_CFG_STS_LINK_RATE_UNSUPPORTED)
		return -EOPNOTSUPP;

	if (ls == NFP_NET_CFG_STS_LINK_RATE_UNKNOWN ||
	    ls >= ARRAY_SIZE(ls_to_ethtool))
		return 0;

	cmd->base.speed = ls_to_ethtool[ls];
	cmd->base.duplex = DUPLEX_FULL;

	return 0;
}

static int
nfp_net_set_link_ksettings(struct net_device *netdev,
			   const struct ethtool_link_ksettings *cmd)
{
	struct nfp_eth_table_port *eth_port;
	struct nfp_port *port;
	struct nfp_nsp *nsp;
	int err;

	port = nfp_port_from_netdev(netdev);
	eth_port = __nfp_port_get_eth_port(port);
	if (!eth_port)
		return -EOPNOTSUPP;

	if (netif_running(netdev)) {
		netdev_warn(netdev, "Changing settings not allowed on an active interface. It may cause the port to be disabled until driver reload.\n");
		return -EBUSY;
	}

	nsp = nfp_eth_config_start(port->app->cpp, eth_port->index);
	if (IS_ERR(nsp))
		return PTR_ERR(nsp);

	err = __nfp_eth_set_aneg(nsp, cmd->base.autoneg == AUTONEG_ENABLE ?
				 NFP_ANEG_AUTO : NFP_ANEG_DISABLED);
	if (err)
		goto err_bad_set;
	if (cmd->base.speed != SPEED_UNKNOWN) {
		u32 speed = cmd->base.speed / eth_port->lanes;

		err = __nfp_eth_set_speed(nsp, speed);
		if (err)
			goto err_bad_set;
	}

	err = nfp_eth_config_commit_end(nsp);
	if (err > 0)
		return 0; /* no change */

	nfp_net_refresh_port_table(port);

	return err;

err_bad_set:
	nfp_eth_config_cleanup_end(nsp);
	return err;
}

static void nfp_net_get_ringparam(struct net_device *netdev,
				  struct ethtool_ringparam *ring)
{
	struct nfp_net *nn = netdev_priv(netdev);

	ring->rx_max_pending = NFP_NET_MAX_RX_DESCS;
	ring->tx_max_pending = NFP_NET_MAX_TX_DESCS;
	ring->rx_pending = nn->dp.rxd_cnt;
	ring->tx_pending = nn->dp.txd_cnt;
}

static int nfp_net_set_ring_size(struct nfp_net *nn, u32 rxd_cnt, u32 txd_cnt)
{
	struct nfp_net_dp *dp;

	dp = nfp_net_clone_dp(nn);
	if (!dp)
		return -ENOMEM;

	dp->rxd_cnt = rxd_cnt;
	dp->txd_cnt = txd_cnt;

	return nfp_net_ring_reconfig(nn, dp, NULL);
}

static int nfp_net_set_ringparam(struct net_device *netdev,
				 struct ethtool_ringparam *ring)
{
	struct nfp_net *nn = netdev_priv(netdev);
	u32 rxd_cnt, txd_cnt;

	/* We don't have separate queues/rings for small/large frames. */
	if (ring->rx_mini_pending || ring->rx_jumbo_pending)
		return -EINVAL;

	/* Round up to supported values */
	rxd_cnt = roundup_pow_of_two(ring->rx_pending);
	txd_cnt = roundup_pow_of_two(ring->tx_pending);

	if (rxd_cnt < NFP_NET_MIN_RX_DESCS || rxd_cnt > NFP_NET_MAX_RX_DESCS ||
	    txd_cnt < NFP_NET_MIN_TX_DESCS || txd_cnt > NFP_NET_MAX_TX_DESCS)
		return -EINVAL;

	if (nn->dp.rxd_cnt == rxd_cnt && nn->dp.txd_cnt == txd_cnt)
		return 0;

	nn_dbg(nn, "Change ring size: RxQ %u->%u, TxQ %u->%u\n",
	       nn->dp.rxd_cnt, rxd_cnt, nn->dp.txd_cnt, txd_cnt);

	return nfp_net_set_ring_size(nn, rxd_cnt, txd_cnt);
}

__printf(2, 3) u8 *nfp_pr_et(u8 *data, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(data, ETH_GSTRING_LEN, fmt, args);
	va_end(args);

	return data + ETH_GSTRING_LEN;
}

static unsigned int nfp_vnic_get_sw_stats_count(struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);

	return NN_RVEC_GATHER_STATS + nn->max_r_vecs * NN_RVEC_PER_Q_STATS;
}

static u8 *nfp_vnic_get_sw_stats_strings(struct net_device *netdev, u8 *data)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int i;

	for (i = 0; i < nn->max_r_vecs; i++) {
		data = nfp_pr_et(data, "rvec_%u_rx_pkts", i);
		data = nfp_pr_et(data, "rvec_%u_tx_pkts", i);
		data = nfp_pr_et(data, "rvec_%u_tx_busy", i);
	}

	data = nfp_pr_et(data, "hw_rx_csum_ok");
	data = nfp_pr_et(data, "hw_rx_csum_inner_ok");
	data = nfp_pr_et(data, "hw_rx_csum_complete");
	data = nfp_pr_et(data, "hw_rx_csum_err");
	data = nfp_pr_et(data, "rx_replace_buf_alloc_fail");
	data = nfp_pr_et(data, "hw_tx_csum");
	data = nfp_pr_et(data, "hw_tx_inner_csum");
	data = nfp_pr_et(data, "tx_gather");
	data = nfp_pr_et(data, "tx_lso");

	return data;
}

static u64 *nfp_vnic_get_sw_stats(struct net_device *netdev, u64 *data)
{
	u64 gathered_stats[NN_RVEC_GATHER_STATS] = {};
	struct nfp_net *nn = netdev_priv(netdev);
	u64 tmp[NN_RVEC_GATHER_STATS];
	unsigned int i, j;

	for (i = 0; i < nn->max_r_vecs; i++) {
		unsigned int start;

		do {
			start = u64_stats_fetch_begin(&nn->r_vecs[i].rx_sync);
			data[0] = nn->r_vecs[i].rx_pkts;
			tmp[0] = nn->r_vecs[i].hw_csum_rx_ok;
			tmp[1] = nn->r_vecs[i].hw_csum_rx_inner_ok;
			tmp[2] = nn->r_vecs[i].hw_csum_rx_complete;
			tmp[3] = nn->r_vecs[i].hw_csum_rx_error;
			tmp[4] = nn->r_vecs[i].rx_replace_buf_alloc_fail;
		} while (u64_stats_fetch_retry(&nn->r_vecs[i].rx_sync, start));

		do {
			start = u64_stats_fetch_begin(&nn->r_vecs[i].tx_sync);
			data[1] = nn->r_vecs[i].tx_pkts;
			data[2] = nn->r_vecs[i].tx_busy;
			tmp[5] = nn->r_vecs[i].hw_csum_tx;
			tmp[6] = nn->r_vecs[i].hw_csum_tx_inner;
			tmp[7] = nn->r_vecs[i].tx_gather;
			tmp[8] = nn->r_vecs[i].tx_lso;
		} while (u64_stats_fetch_retry(&nn->r_vecs[i].tx_sync, start));

		data += NN_RVEC_PER_Q_STATS;

		for (j = 0; j < NN_RVEC_GATHER_STATS; j++)
			gathered_stats[j] += tmp[j];
	}

	for (j = 0; j < NN_RVEC_GATHER_STATS; j++)
		*data++ = gathered_stats[j];

	return data;
}

static unsigned int nfp_vnic_get_hw_stats_count(unsigned int num_vecs)
{
	return NN_ET_GLOBAL_STATS_LEN + num_vecs * 4;
}

static u8 *
nfp_vnic_get_hw_stats_strings(u8 *data, unsigned int num_vecs, bool repr)
{
	int swap_off, i;

	BUILD_BUG_ON(NN_ET_GLOBAL_STATS_LEN < NN_ET_SWITCH_STATS_LEN * 2);
	/* If repr is true first add SWITCH_STATS_LEN and then subtract it
	 * effectively swapping the RX and TX statistics (giving us the RX
	 * and TX from perspective of the switch).
	 */
	swap_off = repr * NN_ET_SWITCH_STATS_LEN;

	for (i = 0; i < NN_ET_SWITCH_STATS_LEN; i++)
		data = nfp_pr_et(data, nfp_net_et_stats[i + swap_off].name);

	for (i = NN_ET_SWITCH_STATS_LEN; i < NN_ET_SWITCH_STATS_LEN * 2; i++)
		data = nfp_pr_et(data, nfp_net_et_stats[i - swap_off].name);

	for (i = NN_ET_SWITCH_STATS_LEN * 2; i < NN_ET_GLOBAL_STATS_LEN; i++)
		data = nfp_pr_et(data, nfp_net_et_stats[i].name);

	for (i = 0; i < num_vecs; i++) {
		data = nfp_pr_et(data, "rxq_%u_pkts", i);
		data = nfp_pr_et(data, "rxq_%u_bytes", i);
		data = nfp_pr_et(data, "txq_%u_pkts", i);
		data = nfp_pr_et(data, "txq_%u_bytes", i);
	}

	return data;
}

static u64 *
nfp_vnic_get_hw_stats(u64 *data, u8 __iomem *mem, unsigned int num_vecs)
{
	unsigned int i;

	for (i = 0; i < NN_ET_GLOBAL_STATS_LEN; i++)
		*data++ = readq(mem + nfp_net_et_stats[i].off);

	for (i = 0; i < num_vecs; i++) {
		*data++ = readq(mem + NFP_NET_CFG_RXR_STATS(i));
		*data++ = readq(mem + NFP_NET_CFG_RXR_STATS(i) + 8);
		*data++ = readq(mem + NFP_NET_CFG_TXR_STATS(i));
		*data++ = readq(mem + NFP_NET_CFG_TXR_STATS(i) + 8);
	}

	return data;
}

static unsigned int nfp_mac_get_stats_count(struct net_device *netdev)
{
	struct nfp_port *port;

	port = nfp_port_from_netdev(netdev);
	if (!__nfp_port_get_eth_port(port) || !port->eth_stats)
		return 0;

	return ARRAY_SIZE(nfp_mac_et_stats);
}

static u8 *nfp_mac_get_stats_strings(struct net_device *netdev, u8 *data)
{
	struct nfp_port *port;
	unsigned int i;

	port = nfp_port_from_netdev(netdev);
	if (!__nfp_port_get_eth_port(port) || !port->eth_stats)
		return data;

	for (i = 0; i < ARRAY_SIZE(nfp_mac_et_stats); i++)
		data = nfp_pr_et(data, "mac.%s", nfp_mac_et_stats[i].name);

	return data;
}

static u64 *nfp_mac_get_stats(struct net_device *netdev, u64 *data)
{
	struct nfp_port *port;
	unsigned int i;

	port = nfp_port_from_netdev(netdev);
	if (!__nfp_port_get_eth_port(port) || !port->eth_stats)
		return data;

	for (i = 0; i < ARRAY_SIZE(nfp_mac_et_stats); i++)
		*data++ = readq(port->eth_stats + nfp_mac_et_stats[i].off);

	return data;
}

static void nfp_net_get_strings(struct net_device *netdev,
				u32 stringset, u8 *data)
{
	struct nfp_net *nn = netdev_priv(netdev);

	switch (stringset) {
	case ETH_SS_STATS:
		data = nfp_vnic_get_sw_stats_strings(netdev, data);
		data = nfp_vnic_get_hw_stats_strings(data, nn->max_r_vecs,
						     false);
		data = nfp_mac_get_stats_strings(netdev, data);
		data = nfp_app_port_get_stats_strings(nn->port, data);
		break;
	}
}

static void
nfp_net_get_stats(struct net_device *netdev, struct ethtool_stats *stats,
		  u64 *data)
{
	struct nfp_net *nn = netdev_priv(netdev);

	data = nfp_vnic_get_sw_stats(netdev, data);
	data = nfp_vnic_get_hw_stats(data, nn->dp.ctrl_bar, nn->max_r_vecs);
	data = nfp_mac_get_stats(netdev, data);
	data = nfp_app_port_get_stats(nn->port, data);
}

static int nfp_net_get_sset_count(struct net_device *netdev, int sset)
{
	struct nfp_net *nn = netdev_priv(netdev);

	switch (sset) {
	case ETH_SS_STATS:
		return nfp_vnic_get_sw_stats_count(netdev) +
		       nfp_vnic_get_hw_stats_count(nn->max_r_vecs) +
		       nfp_mac_get_stats_count(netdev) +
		       nfp_app_port_get_stats_count(nn->port);
	default:
		return -EOPNOTSUPP;
	}
}

static void nfp_port_get_strings(struct net_device *netdev,
				 u32 stringset, u8 *data)
{
	struct nfp_port *port = nfp_port_from_netdev(netdev);

	switch (stringset) {
	case ETH_SS_STATS:
		if (nfp_port_is_vnic(port))
			data = nfp_vnic_get_hw_stats_strings(data, 0, true);
		else
			data = nfp_mac_get_stats_strings(netdev, data);
		data = nfp_app_port_get_stats_strings(port, data);
		break;
	}
}

static void
nfp_port_get_stats(struct net_device *netdev, struct ethtool_stats *stats,
		   u64 *data)
{
	struct nfp_port *port = nfp_port_from_netdev(netdev);

	if (nfp_port_is_vnic(port))
		data = nfp_vnic_get_hw_stats(data, port->vnic, 0);
	else
		data = nfp_mac_get_stats(netdev, data);
	data = nfp_app_port_get_stats(port, data);
}

static int nfp_port_get_sset_count(struct net_device *netdev, int sset)
{
	struct nfp_port *port = nfp_port_from_netdev(netdev);
	unsigned int count;

	switch (sset) {
	case ETH_SS_STATS:
		if (nfp_port_is_vnic(port))
			count = nfp_vnic_get_hw_stats_count(0);
		else
			count = nfp_mac_get_stats_count(netdev);
		count += nfp_app_port_get_stats_count(port);
		return count;
	default:
		return -EOPNOTSUPP;
	}
}

static int nfp_port_fec_ethtool_to_nsp(u32 fec)
{
	switch (fec) {
	case ETHTOOL_FEC_AUTO:
		return NFP_FEC_AUTO_BIT;
	case ETHTOOL_FEC_OFF:
		return NFP_FEC_DISABLED_BIT;
	case ETHTOOL_FEC_RS:
		return NFP_FEC_REED_SOLOMON_BIT;
	case ETHTOOL_FEC_BASER:
		return NFP_FEC_BASER_BIT;
	default:
		/* NSP only supports a single mode at a time */
		return -EOPNOTSUPP;
	}
}

static u32 nfp_port_fec_nsp_to_ethtool(u32 fec)
{
	u32 result = 0;

	if (fec & NFP_FEC_AUTO)
		result |= ETHTOOL_FEC_AUTO;
	if (fec & NFP_FEC_BASER)
		result |= ETHTOOL_FEC_BASER;
	if (fec & NFP_FEC_REED_SOLOMON)
		result |= ETHTOOL_FEC_RS;
	if (fec & NFP_FEC_DISABLED)
		result |= ETHTOOL_FEC_OFF;

	return result ?: ETHTOOL_FEC_NONE;
}

static int
nfp_port_get_fecparam(struct net_device *netdev,
		      struct ethtool_fecparam *param)
{
	struct nfp_eth_table_port *eth_port;
	struct nfp_port *port;

	param->active_fec = ETHTOOL_FEC_NONE_BIT;
	param->fec = ETHTOOL_FEC_NONE_BIT;

	port = nfp_port_from_netdev(netdev);
	eth_port = nfp_port_get_eth_port(port);
	if (!eth_port)
		return -EOPNOTSUPP;

	if (!nfp_eth_can_support_fec(eth_port))
		return 0;

	param->fec = nfp_port_fec_nsp_to_ethtool(eth_port->fec_modes_supported);
	param->active_fec = nfp_port_fec_nsp_to_ethtool(eth_port->fec);

	return 0;
}

static int
nfp_port_set_fecparam(struct net_device *netdev,
		      struct ethtool_fecparam *param)
{
	struct nfp_eth_table_port *eth_port;
	struct nfp_port *port;
	int err, fec;

	port = nfp_port_from_netdev(netdev);
	eth_port = nfp_port_get_eth_port(port);
	if (!eth_port)
		return -EOPNOTSUPP;

	if (!nfp_eth_can_support_fec(eth_port))
		return -EOPNOTSUPP;

	fec = nfp_port_fec_ethtool_to_nsp(param->fec);
	if (fec < 0)
		return fec;

	err = nfp_eth_set_fec(port->app->cpp, eth_port->index, fec);
	if (!err)
		/* Only refresh if we did something */
		nfp_net_refresh_port_table(port);

	return err < 0 ? err : 0;
}

/* RX network flow classification (RSS, filters, etc)
 */
static u32 ethtool_flow_to_nfp_flag(u32 flow_type)
{
	static const u32 xlate_ethtool_to_nfp[IPV6_FLOW + 1] = {
		[TCP_V4_FLOW]	= NFP_NET_CFG_RSS_IPV4_TCP,
		[TCP_V6_FLOW]	= NFP_NET_CFG_RSS_IPV6_TCP,
		[UDP_V4_FLOW]	= NFP_NET_CFG_RSS_IPV4_UDP,
		[UDP_V6_FLOW]	= NFP_NET_CFG_RSS_IPV6_UDP,
		[IPV4_FLOW]	= NFP_NET_CFG_RSS_IPV4,
		[IPV6_FLOW]	= NFP_NET_CFG_RSS_IPV6,
	};

	if (flow_type >= ARRAY_SIZE(xlate_ethtool_to_nfp))
		return 0;

	return xlate_ethtool_to_nfp[flow_type];
}

static int nfp_net_get_rss_hash_opts(struct nfp_net *nn,
				     struct ethtool_rxnfc *cmd)
{
	u32 nfp_rss_flag;

	cmd->data = 0;

	if (!(nn->cap & NFP_NET_CFG_CTRL_RSS_ANY))
		return -EOPNOTSUPP;

	nfp_rss_flag = ethtool_flow_to_nfp_flag(cmd->flow_type);
	if (!nfp_rss_flag)
		return -EINVAL;

	cmd->data |= RXH_IP_SRC | RXH_IP_DST;
	if (nn->rss_cfg & nfp_rss_flag)
		cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;

	return 0;
}

static int nfp_net_get_rxnfc(struct net_device *netdev,
			     struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	struct nfp_net *nn = netdev_priv(netdev);

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = nn->dp.num_rx_rings;
		return 0;
	case ETHTOOL_GRXFH:
		return nfp_net_get_rss_hash_opts(nn, cmd);
	default:
		return -EOPNOTSUPP;
	}
}

static int nfp_net_set_rss_hash_opt(struct nfp_net *nn,
				    struct ethtool_rxnfc *nfc)
{
	u32 new_rss_cfg = nn->rss_cfg;
	u32 nfp_rss_flag;
	int err;

	if (!(nn->cap & NFP_NET_CFG_CTRL_RSS_ANY))
		return -EOPNOTSUPP;

	/* RSS only supports IP SA/DA and L4 src/dst ports  */
	if (nfc->data & ~(RXH_IP_SRC | RXH_IP_DST |
			  RXH_L4_B_0_1 | RXH_L4_B_2_3))
		return -EINVAL;

	/* We need at least the IP SA/DA fields for hashing */
	if (!(nfc->data & RXH_IP_SRC) ||
	    !(nfc->data & RXH_IP_DST))
		return -EINVAL;

	nfp_rss_flag = ethtool_flow_to_nfp_flag(nfc->flow_type);
	if (!nfp_rss_flag)
		return -EINVAL;

	switch (nfc->data & (RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
	case 0:
		new_rss_cfg &= ~nfp_rss_flag;
		break;
	case (RXH_L4_B_0_1 | RXH_L4_B_2_3):
		new_rss_cfg |= nfp_rss_flag;
		break;
	default:
		return -EINVAL;
	}

	new_rss_cfg |= FIELD_PREP(NFP_NET_CFG_RSS_HFUNC, nn->rss_hfunc);
	new_rss_cfg |= NFP_NET_CFG_RSS_MASK;

	if (new_rss_cfg == nn->rss_cfg)
		return 0;

	writel(new_rss_cfg, nn->dp.ctrl_bar + NFP_NET_CFG_RSS_CTRL);
	err = nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_RSS);
	if (err)
		return err;

	nn->rss_cfg = new_rss_cfg;

	nn_dbg(nn, "Changed RSS config to 0x%x\n", nn->rss_cfg);
	return 0;
}

static int nfp_net_set_rxnfc(struct net_device *netdev,
			     struct ethtool_rxnfc *cmd)
{
	struct nfp_net *nn = netdev_priv(netdev);

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		return nfp_net_set_rss_hash_opt(nn, cmd);
	default:
		return -EOPNOTSUPP;
	}
}

static u32 nfp_net_get_rxfh_indir_size(struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);

	if (!(nn->cap & NFP_NET_CFG_CTRL_RSS_ANY))
		return 0;

	return ARRAY_SIZE(nn->rss_itbl);
}

static u32 nfp_net_get_rxfh_key_size(struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);

	if (!(nn->cap & NFP_NET_CFG_CTRL_RSS_ANY))
		return -EOPNOTSUPP;

	return nfp_net_rss_key_sz(nn);
}

static int nfp_net_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			    u8 *hfunc)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int i;

	if (!(nn->cap & NFP_NET_CFG_CTRL_RSS_ANY))
		return -EOPNOTSUPP;

	if (indir)
		for (i = 0; i < ARRAY_SIZE(nn->rss_itbl); i++)
			indir[i] = nn->rss_itbl[i];
	if (key)
		memcpy(key, nn->rss_key, nfp_net_rss_key_sz(nn));
	if (hfunc) {
		*hfunc = nn->rss_hfunc;
		if (*hfunc >= 1 << ETH_RSS_HASH_FUNCS_COUNT)
			*hfunc = ETH_RSS_HASH_UNKNOWN;
	}

	return 0;
}

static int nfp_net_set_rxfh(struct net_device *netdev,
			    const u32 *indir, const u8 *key,
			    const u8 hfunc)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int i;

	if (!(nn->cap & NFP_NET_CFG_CTRL_RSS_ANY) ||
	    !(hfunc == ETH_RSS_HASH_NO_CHANGE || hfunc == nn->rss_hfunc))
		return -EOPNOTSUPP;

	if (!key && !indir)
		return 0;

	if (key) {
		memcpy(nn->rss_key, key, nfp_net_rss_key_sz(nn));
		nfp_net_rss_write_key(nn);
	}
	if (indir) {
		for (i = 0; i < ARRAY_SIZE(nn->rss_itbl); i++)
			nn->rss_itbl[i] = indir[i];

		nfp_net_rss_write_itbl(nn);
	}

	return nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_RSS);
}

/* Dump BAR registers
 */
static int nfp_net_get_regs_len(struct net_device *netdev)
{
	return NFP_NET_CFG_BAR_SZ;
}

static void nfp_net_get_regs(struct net_device *netdev,
			     struct ethtool_regs *regs, void *p)
{
	struct nfp_net *nn = netdev_priv(netdev);
	u32 *regs_buf = p;
	int i;

	regs->version = nn_readl(nn, NFP_NET_CFG_VERSION);

	for (i = 0; i < NFP_NET_CFG_BAR_SZ / sizeof(u32); i++)
		regs_buf[i] = readl(nn->dp.ctrl_bar + (i * sizeof(u32)));
}

static int nfp_net_get_coalesce(struct net_device *netdev,
				struct ethtool_coalesce *ec)
{
	struct nfp_net *nn = netdev_priv(netdev);

	if (!(nn->cap & NFP_NET_CFG_CTRL_IRQMOD))
		return -EINVAL;

	ec->rx_coalesce_usecs       = nn->rx_coalesce_usecs;
	ec->rx_max_coalesced_frames = nn->rx_coalesce_max_frames;
	ec->tx_coalesce_usecs       = nn->tx_coalesce_usecs;
	ec->tx_max_coalesced_frames = nn->tx_coalesce_max_frames;

	return 0;
}

/* Other debug dumps
 */
static int
nfp_dump_nsp_diag(struct nfp_app *app, struct ethtool_dump *dump, void *buffer)
{
	struct nfp_resource *res;
	int ret;

	if (!app)
		return -EOPNOTSUPP;

	dump->version = 1;
	dump->flag = NFP_DUMP_NSP_DIAG;

	res = nfp_resource_acquire(app->cpp, NFP_RESOURCE_NSP_DIAG);
	if (IS_ERR(res))
		return PTR_ERR(res);

	if (buffer) {
		if (dump->len != nfp_resource_size(res)) {
			ret = -EINVAL;
			goto exit_release;
		}

		ret = nfp_cpp_read(app->cpp, nfp_resource_cpp_id(res),
				   nfp_resource_address(res),
				   buffer, dump->len);
		if (ret != dump->len)
			ret = ret < 0 ? ret : -EIO;
		else
			ret = 0;
	} else {
		dump->len = nfp_resource_size(res);
		ret = 0;
	}
exit_release:
	nfp_resource_release(res);

	return ret;
}

/* Set the dump flag/level. Calculate the dump length for flag > 0 only (new TLV
 * based dumps), since flag 0 (default) calculates the length in
 * nfp_app_get_dump_flag(), and we need to support triggering a level 0 dump
 * without setting the flag first, for backward compatibility.
 */
static int nfp_app_set_dump(struct net_device *netdev, struct ethtool_dump *val)
{
	struct nfp_app *app = nfp_app_from_netdev(netdev);
	s64 len;

	if (!app)
		return -EOPNOTSUPP;

	if (val->flag == NFP_DUMP_NSP_DIAG) {
		app->pf->dump_flag = val->flag;
		return 0;
	}

	if (!app->pf->dumpspec)
		return -EOPNOTSUPP;

	len = nfp_net_dump_calculate_size(app->pf, app->pf->dumpspec,
					  val->flag);
	if (len < 0)
		return len;

	app->pf->dump_flag = val->flag;
	app->pf->dump_len = len;

	return 0;
}

static int
nfp_app_get_dump_flag(struct net_device *netdev, struct ethtool_dump *dump)
{
	struct nfp_app *app = nfp_app_from_netdev(netdev);

	if (!app)
		return -EOPNOTSUPP;

	if (app->pf->dump_flag == NFP_DUMP_NSP_DIAG)
		return nfp_dump_nsp_diag(app, dump, NULL);

	dump->flag = app->pf->dump_flag;
	dump->len = app->pf->dump_len;

	return 0;
}

static int
nfp_app_get_dump_data(struct net_device *netdev, struct ethtool_dump *dump,
		      void *buffer)
{
	struct nfp_app *app = nfp_app_from_netdev(netdev);

	if (!app)
		return -EOPNOTSUPP;

	if (app->pf->dump_flag == NFP_DUMP_NSP_DIAG)
		return nfp_dump_nsp_diag(app, dump, buffer);

	dump->flag = app->pf->dump_flag;
	dump->len = app->pf->dump_len;

	return nfp_net_dump_populate_buffer(app->pf, app->pf->dumpspec, dump,
					    buffer);
}

static int nfp_net_set_coalesce(struct net_device *netdev,
				struct ethtool_coalesce *ec)
{
	struct nfp_net *nn = netdev_priv(netdev);
	unsigned int factor;

	if (ec->rx_coalesce_usecs_irq ||
	    ec->rx_max_coalesced_frames_irq ||
	    ec->tx_coalesce_usecs_irq ||
	    ec->tx_max_coalesced_frames_irq ||
	    ec->stats_block_coalesce_usecs ||
	    ec->use_adaptive_rx_coalesce ||
	    ec->use_adaptive_tx_coalesce ||
	    ec->pkt_rate_low ||
	    ec->rx_coalesce_usecs_low ||
	    ec->rx_max_coalesced_frames_low ||
	    ec->tx_coalesce_usecs_low ||
	    ec->tx_max_coalesced_frames_low ||
	    ec->pkt_rate_high ||
	    ec->rx_coalesce_usecs_high ||
	    ec->rx_max_coalesced_frames_high ||
	    ec->tx_coalesce_usecs_high ||
	    ec->tx_max_coalesced_frames_high ||
	    ec->rate_sample_interval)
		return -EOPNOTSUPP;

	/* Compute factor used to convert coalesce '_usecs' parameters to
	 * ME timestamp ticks.  There are 16 ME clock cycles for each timestamp
	 * count.
	 */
	factor = nn->me_freq_mhz / 16;

	/* Each pair of (usecs, max_frames) fields specifies that interrupts
	 * should be coalesced until
	 *      (usecs > 0 && time_since_first_completion >= usecs) ||
	 *      (max_frames > 0 && completed_frames >= max_frames)
	 *
	 * It is illegal to set both usecs and max_frames to zero as this would
	 * cause interrupts to never be generated.  To disable coalescing, set
	 * usecs = 0 and max_frames = 1.
	 *
	 * Some implementations ignore the value of max_frames and use the
	 * condition time_since_first_completion >= usecs
	 */

	if (!(nn->cap & NFP_NET_CFG_CTRL_IRQMOD))
		return -EINVAL;

	/* ensure valid configuration */
	if (!ec->rx_coalesce_usecs && !ec->rx_max_coalesced_frames)
		return -EINVAL;

	if (!ec->tx_coalesce_usecs && !ec->tx_max_coalesced_frames)
		return -EINVAL;

	if (ec->rx_coalesce_usecs * factor >= ((1 << 16) - 1))
		return -EINVAL;

	if (ec->tx_coalesce_usecs * factor >= ((1 << 16) - 1))
		return -EINVAL;

	if (ec->rx_max_coalesced_frames >= ((1 << 16) - 1))
		return -EINVAL;

	if (ec->tx_max_coalesced_frames >= ((1 << 16) - 1))
		return -EINVAL;

	/* configuration is valid */
	nn->rx_coalesce_usecs      = ec->rx_coalesce_usecs;
	nn->rx_coalesce_max_frames = ec->rx_max_coalesced_frames;
	nn->tx_coalesce_usecs      = ec->tx_coalesce_usecs;
	nn->tx_coalesce_max_frames = ec->tx_max_coalesced_frames;

	/* write configuration to device */
	nfp_net_coalesce_write_cfg(nn);
	return nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_IRQMOD);
}

static void nfp_net_get_channels(struct net_device *netdev,
				 struct ethtool_channels *channel)
{
	struct nfp_net *nn = netdev_priv(netdev);
	unsigned int num_tx_rings;

	num_tx_rings = nn->dp.num_tx_rings;
	if (nn->dp.xdp_prog)
		num_tx_rings -= nn->dp.num_rx_rings;

	channel->max_rx = min(nn->max_rx_rings, nn->max_r_vecs);
	channel->max_tx = min(nn->max_tx_rings, nn->max_r_vecs);
	channel->max_combined = min(channel->max_rx, channel->max_tx);
	channel->max_other = NFP_NET_NON_Q_VECTORS;
	channel->combined_count = min(nn->dp.num_rx_rings, num_tx_rings);
	channel->rx_count = nn->dp.num_rx_rings - channel->combined_count;
	channel->tx_count = num_tx_rings - channel->combined_count;
	channel->other_count = NFP_NET_NON_Q_VECTORS;
}

static int nfp_net_set_num_rings(struct nfp_net *nn, unsigned int total_rx,
				 unsigned int total_tx)
{
	struct nfp_net_dp *dp;

	dp = nfp_net_clone_dp(nn);
	if (!dp)
		return -ENOMEM;

	dp->num_rx_rings = total_rx;
	dp->num_tx_rings = total_tx;
	/* nfp_net_check_config() will catch num_tx_rings > nn->max_tx_rings */
	if (dp->xdp_prog)
		dp->num_tx_rings += total_rx;

	return nfp_net_ring_reconfig(nn, dp, NULL);
}

static int nfp_net_set_channels(struct net_device *netdev,
				struct ethtool_channels *channel)
{
	struct nfp_net *nn = netdev_priv(netdev);
	unsigned int total_rx, total_tx;

	/* Reject unsupported */
	if (!channel->combined_count ||
	    channel->other_count != NFP_NET_NON_Q_VECTORS ||
	    (channel->rx_count && channel->tx_count))
		return -EINVAL;

	total_rx = channel->combined_count + channel->rx_count;
	total_tx = channel->combined_count + channel->tx_count;

	if (total_rx > min(nn->max_rx_rings, nn->max_r_vecs) ||
	    total_tx > min(nn->max_tx_rings, nn->max_r_vecs))
		return -EINVAL;

	return nfp_net_set_num_rings(nn, total_rx, total_tx);
}

static int
nfp_net_flash_device(struct net_device *netdev, struct ethtool_flash *flash)
{
	struct nfp_app *app;
	int ret;

	if (flash->region != ETHTOOL_FLASH_ALL_REGIONS)
		return -EOPNOTSUPP;

	app = nfp_app_from_netdev(netdev);
	if (!app)
		return -EOPNOTSUPP;

	dev_hold(netdev);
	rtnl_unlock();
	ret = nfp_flash_update_common(app->pf, flash->data, NULL);
	rtnl_lock();
	dev_put(netdev);

	return ret;
}

static const struct ethtool_ops nfp_net_ethtool_ops = {
	.get_drvinfo		= nfp_net_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_ringparam		= nfp_net_get_ringparam,
	.set_ringparam		= nfp_net_set_ringparam,
	.get_strings		= nfp_net_get_strings,
	.get_ethtool_stats	= nfp_net_get_stats,
	.get_sset_count		= nfp_net_get_sset_count,
	.get_rxnfc		= nfp_net_get_rxnfc,
	.set_rxnfc		= nfp_net_set_rxnfc,
	.flash_device		= nfp_net_flash_device,
	.get_rxfh_indir_size	= nfp_net_get_rxfh_indir_size,
	.get_rxfh_key_size	= nfp_net_get_rxfh_key_size,
	.get_rxfh		= nfp_net_get_rxfh,
	.set_rxfh		= nfp_net_set_rxfh,
	.get_regs_len		= nfp_net_get_regs_len,
	.get_regs		= nfp_net_get_regs,
	.set_dump		= nfp_app_set_dump,
	.get_dump_flag		= nfp_app_get_dump_flag,
	.get_dump_data		= nfp_app_get_dump_data,
	.get_coalesce           = nfp_net_get_coalesce,
	.set_coalesce           = nfp_net_set_coalesce,
	.get_channels		= nfp_net_get_channels,
	.set_channels		= nfp_net_set_channels,
	.get_link_ksettings	= nfp_net_get_link_ksettings,
	.set_link_ksettings	= nfp_net_set_link_ksettings,
	.get_fecparam		= nfp_port_get_fecparam,
	.set_fecparam		= nfp_port_set_fecparam,
};

const struct ethtool_ops nfp_port_ethtool_ops = {
	.get_drvinfo		= nfp_app_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_strings		= nfp_port_get_strings,
	.get_ethtool_stats	= nfp_port_get_stats,
	.get_sset_count		= nfp_port_get_sset_count,
	.flash_device		= nfp_net_flash_device,
	.set_dump		= nfp_app_set_dump,
	.get_dump_flag		= nfp_app_get_dump_flag,
	.get_dump_data		= nfp_app_get_dump_data,
	.get_link_ksettings	= nfp_net_get_link_ksettings,
	.set_link_ksettings	= nfp_net_set_link_ksettings,
	.get_fecparam		= nfp_port_get_fecparam,
	.set_fecparam		= nfp_port_set_fecparam,
};

void nfp_net_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &nfp_net_ethtool_ops;
}
