/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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

#include "nfpcore/nfp.h"
#include "nfpcore/nfp_nsp.h"
#include "nfp_app.h"
#include "nfp_net_ctrl.h"
#include "nfp_net.h"
#include "nfp_port.h"

enum nfp_dump_diag {
	NFP_DUMP_NSP_DIAG = 0,
};

/* Support for stats. Returns netdev, driver, and device stats */
enum { NETDEV_ET_STATS, NFP_NET_DRV_ET_STATS, NFP_NET_DEV_ET_STATS };
struct _nfp_net_et_stats {
	char name[ETH_GSTRING_LEN];
	int type;
	int sz;
	int off;
};

#define NN_ET_NETDEV_STAT(m) NETDEV_ET_STATS,			\
		FIELD_SIZEOF(struct net_device_stats, m),	\
		offsetof(struct net_device_stats, m)
/* For stats in the control BAR (other than Q stats) */
#define NN_ET_DEV_STAT(m) NFP_NET_DEV_ET_STATS,			\
		sizeof(u64),					\
		(m)
static const struct _nfp_net_et_stats nfp_net_et_stats[] = {
	/* netdev stats */
	{"rx_packets", NN_ET_NETDEV_STAT(rx_packets)},
	{"tx_packets", NN_ET_NETDEV_STAT(tx_packets)},
	{"rx_bytes", NN_ET_NETDEV_STAT(rx_bytes)},
	{"tx_bytes", NN_ET_NETDEV_STAT(tx_bytes)},
	{"rx_errors", NN_ET_NETDEV_STAT(rx_errors)},
	{"tx_errors", NN_ET_NETDEV_STAT(tx_errors)},
	{"rx_dropped", NN_ET_NETDEV_STAT(rx_dropped)},
	{"tx_dropped", NN_ET_NETDEV_STAT(tx_dropped)},
	{"multicast", NN_ET_NETDEV_STAT(multicast)},
	{"collisions", NN_ET_NETDEV_STAT(collisions)},
	{"rx_over_errors", NN_ET_NETDEV_STAT(rx_over_errors)},
	{"rx_crc_errors", NN_ET_NETDEV_STAT(rx_crc_errors)},
	{"rx_frame_errors", NN_ET_NETDEV_STAT(rx_frame_errors)},
	{"rx_fifo_errors", NN_ET_NETDEV_STAT(rx_fifo_errors)},
	{"rx_missed_errors", NN_ET_NETDEV_STAT(rx_missed_errors)},
	{"tx_aborted_errors", NN_ET_NETDEV_STAT(tx_aborted_errors)},
	{"tx_carrier_errors", NN_ET_NETDEV_STAT(tx_carrier_errors)},
	{"tx_fifo_errors", NN_ET_NETDEV_STAT(tx_fifo_errors)},
	/* Stats from the device */
	{"dev_rx_discards", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_RX_DISCARDS)},
	{"dev_rx_errors", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_RX_ERRORS)},
	{"dev_rx_bytes", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_RX_OCTETS)},
	{"dev_rx_uc_bytes", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_RX_UC_OCTETS)},
	{"dev_rx_mc_bytes", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_RX_MC_OCTETS)},
	{"dev_rx_bc_bytes", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_RX_BC_OCTETS)},
	{"dev_rx_pkts", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_RX_FRAMES)},
	{"dev_rx_mc_pkts", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_RX_MC_FRAMES)},
	{"dev_rx_bc_pkts", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_RX_BC_FRAMES)},

	{"dev_tx_discards", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_TX_DISCARDS)},
	{"dev_tx_errors", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_TX_ERRORS)},
	{"dev_tx_bytes", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_TX_OCTETS)},
	{"dev_tx_uc_bytes", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_TX_UC_OCTETS)},
	{"dev_tx_mc_bytes", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_TX_MC_OCTETS)},
	{"dev_tx_bc_bytes", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_TX_BC_OCTETS)},
	{"dev_tx_pkts", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_TX_FRAMES)},
	{"dev_tx_mc_pkts", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_TX_MC_FRAMES)},
	{"dev_tx_bc_pkts", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_TX_BC_FRAMES)},

	{"bpf_pass_pkts", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_APP0_FRAMES)},
	{"bpf_pass_bytes", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_APP0_BYTES)},
	/* see comments in outro functions in nfp_bpf_jit.c to find out
	 * how different BPF modes use app-specific counters
	 */
	{"bpf_app1_pkts", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_APP1_FRAMES)},
	{"bpf_app1_bytes", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_APP1_BYTES)},
	{"bpf_app2_pkts", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_APP2_FRAMES)},
	{"bpf_app2_bytes", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_APP2_BYTES)},
	{"bpf_app3_pkts", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_APP3_FRAMES)},
	{"bpf_app3_bytes", NN_ET_DEV_STAT(NFP_NET_CFG_STATS_APP3_BYTES)},
};

#define NN_ET_GLOBAL_STATS_LEN ARRAY_SIZE(nfp_net_et_stats)
#define NN_ET_RVEC_STATS_LEN (nn->dp.num_r_vecs * 3)
#define NN_ET_RVEC_GATHER_STATS 7
#define NN_ET_QUEUE_STATS_LEN ((nn->dp.num_tx_rings + nn->dp.num_rx_rings) * 2)
#define NN_ET_STATS_LEN (NN_ET_GLOBAL_STATS_LEN + NN_ET_RVEC_GATHER_STATS + \
			 NN_ET_RVEC_STATS_LEN + NN_ET_QUEUE_STATS_LEN)

static void nfp_net_get_nspinfo(struct nfp_app *app, char *version)
{
	struct nfp_nsp *nsp;

	if (!app)
		return;

	nsp = nfp_nsp_open(app->cpp);
	if (IS_ERR(nsp))
		return;

	snprintf(version, ETHTOOL_FWVERS_LEN, "sp:%hu.%hu",
		 nfp_nsp_get_abi_ver_major(nsp),
		 nfp_nsp_get_abi_ver_minor(nsp));

	nfp_nsp_close(nsp);
}

static void nfp_net_get_drvinfo(struct net_device *netdev,
				struct ethtool_drvinfo *drvinfo)
{
	char nsp_version[ETHTOOL_FWVERS_LEN] = {};
	struct nfp_net *nn = netdev_priv(netdev);

	strlcpy(drvinfo->driver, nn->pdev->driver->name,
		sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, nfp_driver_version, sizeof(drvinfo->version));

	nfp_net_get_nspinfo(nn->app, nsp_version);
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%d.%d.%d.%d %s %s %s",
		 nn->fw_ver.resv, nn->fw_ver.class,
		 nn->fw_ver.major, nn->fw_ver.minor, nsp_version,
		 nfp_app_mip_name(nn->app), nfp_app_name(nn->app));
	strlcpy(drvinfo->bus_info, pci_name(nn->pdev),
		sizeof(drvinfo->bus_info));

	drvinfo->n_stats = NN_ET_STATS_LEN;
	drvinfo->regdump_len = NFP_NET_CFG_BAR_SZ;
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
	if (eth_port)
		cmd->base.autoneg = eth_port->aneg != NFP_ANEG_DISABLED ?
			AUTONEG_ENABLE : AUTONEG_DISABLE;

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

	cmd->base.speed = ls_to_ethtool[sts];
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
		netdev_warn(netdev, "Changing settings not allowed on an active interface. It may cause the port to be disabled until reboot.\n");
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

static void nfp_net_get_strings(struct net_device *netdev,
				u32 stringset, u8 *data)
{
	struct nfp_net *nn = netdev_priv(netdev);
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < NN_ET_GLOBAL_STATS_LEN; i++) {
			memcpy(p, nfp_net_et_stats[i].name, ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < nn->dp.num_r_vecs; i++) {
			sprintf(p, "rvec_%u_rx_pkts", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "rvec_%u_tx_pkts", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "rvec_%u_tx_busy", i);
			p += ETH_GSTRING_LEN;
		}
		strncpy(p, "hw_rx_csum_ok", ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
		strncpy(p, "hw_rx_csum_inner_ok", ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
		strncpy(p, "hw_rx_csum_err", ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
		strncpy(p, "hw_tx_csum", ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
		strncpy(p, "hw_tx_inner_csum", ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
		strncpy(p, "tx_gather", ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
		strncpy(p, "tx_lso", ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
		for (i = 0; i < nn->dp.num_tx_rings; i++) {
			sprintf(p, "txq_%u_pkts", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "txq_%u_bytes", i);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < nn->dp.num_rx_rings; i++) {
			sprintf(p, "rxq_%u_pkts", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "rxq_%u_bytes", i);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void nfp_net_get_stats(struct net_device *netdev,
			      struct ethtool_stats *stats, u64 *data)
{
	u64 gathered_stats[NN_ET_RVEC_GATHER_STATS] = {};
	struct nfp_net *nn = netdev_priv(netdev);
	struct rtnl_link_stats64 *netdev_stats;
	struct rtnl_link_stats64 temp = {};
	u64 tmp[NN_ET_RVEC_GATHER_STATS];
	u8 __iomem *io_p;
	int i, j, k;
	u8 *p;

	netdev_stats = dev_get_stats(netdev, &temp);

	for (i = 0; i < NN_ET_GLOBAL_STATS_LEN; i++) {
		switch (nfp_net_et_stats[i].type) {
		case NETDEV_ET_STATS:
			p = (char *)netdev_stats + nfp_net_et_stats[i].off;
			data[i] = nfp_net_et_stats[i].sz == sizeof(u64) ?
				*(u64 *)p : *(u32 *)p;
			break;

		case NFP_NET_DEV_ET_STATS:
			io_p = nn->dp.ctrl_bar + nfp_net_et_stats[i].off;
			data[i] = readq(io_p);
			break;
		}
	}
	for (j = 0; j < nn->dp.num_r_vecs; j++) {
		unsigned int start;

		do {
			start = u64_stats_fetch_begin(&nn->r_vecs[j].rx_sync);
			data[i++] = nn->r_vecs[j].rx_pkts;
			tmp[0] = nn->r_vecs[j].hw_csum_rx_ok;
			tmp[1] = nn->r_vecs[j].hw_csum_rx_inner_ok;
			tmp[2] = nn->r_vecs[j].hw_csum_rx_error;
		} while (u64_stats_fetch_retry(&nn->r_vecs[j].rx_sync, start));

		do {
			start = u64_stats_fetch_begin(&nn->r_vecs[j].tx_sync);
			data[i++] = nn->r_vecs[j].tx_pkts;
			data[i++] = nn->r_vecs[j].tx_busy;
			tmp[3] = nn->r_vecs[j].hw_csum_tx;
			tmp[4] = nn->r_vecs[j].hw_csum_tx_inner;
			tmp[5] = nn->r_vecs[j].tx_gather;
			tmp[6] = nn->r_vecs[j].tx_lso;
		} while (u64_stats_fetch_retry(&nn->r_vecs[j].tx_sync, start));

		for (k = 0; k < NN_ET_RVEC_GATHER_STATS; k++)
			gathered_stats[k] += tmp[k];
	}
	for (j = 0; j < NN_ET_RVEC_GATHER_STATS; j++)
		data[i++] = gathered_stats[j];
	for (j = 0; j < nn->dp.num_tx_rings; j++) {
		io_p = nn->dp.ctrl_bar + NFP_NET_CFG_TXR_STATS(j);
		data[i++] = readq(io_p);
		io_p = nn->dp.ctrl_bar + NFP_NET_CFG_TXR_STATS(j) + 8;
		data[i++] = readq(io_p);
	}
	for (j = 0; j < nn->dp.num_rx_rings; j++) {
		io_p = nn->dp.ctrl_bar + NFP_NET_CFG_RXR_STATS(j);
		data[i++] = readq(io_p);
		io_p = nn->dp.ctrl_bar + NFP_NET_CFG_RXR_STATS(j) + 8;
		data[i++] = readq(io_p);
	}
}

static int nfp_net_get_sset_count(struct net_device *netdev, int sset)
{
	struct nfp_net *nn = netdev_priv(netdev);

	switch (sset) {
	case ETH_SS_STATS:
		return NN_ET_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
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
nfp_dump_nsp_diag(struct nfp_net *nn, struct ethtool_dump *dump, void *buffer)
{
	struct nfp_resource *res;
	int ret;

	if (!nn->app)
		return -EOPNOTSUPP;

	dump->version = 1;
	dump->flag = NFP_DUMP_NSP_DIAG;

	res = nfp_resource_acquire(nn->app->cpp, NFP_RESOURCE_NSP_DIAG);
	if (IS_ERR(res))
		return PTR_ERR(res);

	if (buffer) {
		if (dump->len != nfp_resource_size(res)) {
			ret = -EINVAL;
			goto exit_release;
		}

		ret = nfp_cpp_read(nn->app->cpp, nfp_resource_cpp_id(res),
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

static int nfp_net_set_dump(struct net_device *netdev, struct ethtool_dump *val)
{
	struct nfp_net *nn = netdev_priv(netdev);

	if (!nn->app)
		return -EOPNOTSUPP;

	if (val->flag != NFP_DUMP_NSP_DIAG)
		return -EINVAL;

	nn->ethtool_dump_flag = val->flag;

	return 0;
}

static int
nfp_net_get_dump_flag(struct net_device *netdev, struct ethtool_dump *dump)
{
	return nfp_dump_nsp_diag(netdev_priv(netdev), dump, NULL);
}

static int
nfp_net_get_dump_data(struct net_device *netdev, struct ethtool_dump *dump,
		      void *buffer)
{
	return nfp_dump_nsp_diag(netdev_priv(netdev), dump, buffer);
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
	.get_rxfh_indir_size	= nfp_net_get_rxfh_indir_size,
	.get_rxfh_key_size	= nfp_net_get_rxfh_key_size,
	.get_rxfh		= nfp_net_get_rxfh,
	.set_rxfh		= nfp_net_set_rxfh,
	.get_regs_len		= nfp_net_get_regs_len,
	.get_regs		= nfp_net_get_regs,
	.set_dump		= nfp_net_set_dump,
	.get_dump_flag		= nfp_net_get_dump_flag,
	.get_dump_data		= nfp_net_get_dump_data,
	.get_coalesce           = nfp_net_get_coalesce,
	.set_coalesce           = nfp_net_set_coalesce,
	.get_channels		= nfp_net_get_channels,
	.set_channels		= nfp_net_set_channels,
	.get_link_ksettings	= nfp_net_get_link_ksettings,
	.set_link_ksettings	= nfp_net_set_link_ksettings,
};

void nfp_net_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &nfp_net_ethtool_ops;
}
