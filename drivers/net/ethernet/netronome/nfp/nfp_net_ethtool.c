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
#include <linux/sfp.h>

#include "nfpcore/nfp.h"
#include "nfpcore/nfp_dev.h"
#include "nfpcore/nfp_nsp.h"
#include "nfp_app.h"
#include "nfp_main.h"
#include "nfp_net_ctrl.h"
#include "nfp_net_dp.h"
#include "nfp_net.h"
#include "nfp_port.h"
#include "nfpcore/nfp_cpp.h"

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

static const char nfp_tlv_stat_names[][ETH_GSTRING_LEN] = {
	[1]	= "dev_rx_discards",
	[2]	= "dev_rx_errors",
	[3]	= "dev_rx_bytes",
	[4]	= "dev_rx_uc_bytes",
	[5]	= "dev_rx_mc_bytes",
	[6]	= "dev_rx_bc_bytes",
	[7]	= "dev_rx_pkts",
	[8]	= "dev_rx_mc_pkts",
	[9]	= "dev_rx_bc_pkts",

	[10]	= "dev_tx_discards",
	[11]	= "dev_tx_errors",
	[12]	= "dev_tx_bytes",
	[13]	= "dev_tx_uc_bytes",
	[14]	= "dev_tx_mc_bytes",
	[15]	= "dev_tx_bc_bytes",
	[16]	= "dev_tx_pkts",
	[17]	= "dev_tx_mc_pkts",
	[18]	= "dev_tx_bc_pkts",
};

#define NN_ET_GLOBAL_STATS_LEN ARRAY_SIZE(nfp_net_et_stats)
#define NN_ET_SWITCH_STATS_LEN 9
#define NN_RVEC_GATHER_STATS	13
#define NN_RVEC_PER_Q_STATS	3
#define NN_CTRL_PATH_STATS	4

#define SFP_SFF_REV_COMPLIANCE	1

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

	strscpy(drvinfo->driver, dev_driver_string(&pdev->dev),
		sizeof(drvinfo->driver));
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
		 nn->fw_ver.extend, nn->fw_ver.class,
		 nn->fw_ver.major, nn->fw_ver.minor);
	strscpy(drvinfo->bus_info, pci_name(nn->pdev),
		sizeof(drvinfo->bus_info));

	nfp_get_drvinfo(nn->app, nn->pdev, vnic_version, drvinfo);
}

static int
nfp_net_nway_reset(struct net_device *netdev)
{
	struct nfp_eth_table_port *eth_port;
	struct nfp_port *port;
	int err;

	port = nfp_port_from_netdev(netdev);
	eth_port = nfp_port_get_eth_port(port);
	if (!eth_port)
		return -EOPNOTSUPP;

	if (!netif_running(netdev))
		return 0;

	err = nfp_eth_set_configured(port->app->cpp, eth_port->index, false);
	if (err) {
		netdev_info(netdev, "Link down failed: %d\n", err);
		return err;
	}

	err = nfp_eth_set_configured(port->app->cpp, eth_port->index, true);
	if (err) {
		netdev_info(netdev, "Link up failed: %d\n", err);
		return err;
	}

	netdev_info(netdev, "Link reset succeeded\n");
	return 0;
}

static void
nfp_app_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct nfp_app *app = nfp_app_from_netdev(netdev);

	strscpy(drvinfo->bus_info, pci_name(app->pdev),
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
	struct nfp_eth_table_port *eth_port;
	struct nfp_port *port;
	struct nfp_net *nn;
	unsigned int speed;
	u16 sts;

	/* Init to unknowns */
	ethtool_link_ksettings_add_link_mode(cmd, supported, FIBRE);
	cmd->base.port = PORT_OTHER;
	cmd->base.speed = SPEED_UNKNOWN;
	cmd->base.duplex = DUPLEX_UNKNOWN;

	port = nfp_port_from_netdev(netdev);
	eth_port = nfp_port_get_eth_port(port);
	if (eth_port) {
		ethtool_link_ksettings_add_link_mode(cmd, supported, Pause);
		ethtool_link_ksettings_add_link_mode(cmd, advertising, Pause);
		if (eth_port->supp_aneg) {
			ethtool_link_ksettings_add_link_mode(cmd, supported, Autoneg);
			if (eth_port->aneg == NFP_ANEG_AUTO) {
				ethtool_link_ksettings_add_link_mode(cmd, advertising, Autoneg);
				cmd->base.autoneg = AUTONEG_ENABLE;
			}
		}
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

	sts = nn_readw(nn, NFP_NET_CFG_STS);
	speed = nfp_net_lr2speed(FIELD_GET(NFP_NET_CFG_STS_LINK_RATE, sts));
	if (!speed)
		return -EOPNOTSUPP;

	if (speed != SPEED_UNKNOWN) {
		cmd->base.speed = speed;
		cmd->base.duplex = DUPLEX_FULL;
	}

	return 0;
}

static int
nfp_net_set_link_ksettings(struct net_device *netdev,
			   const struct ethtool_link_ksettings *cmd)
{
	bool req_aneg = (cmd->base.autoneg == AUTONEG_ENABLE);
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

	if (req_aneg && !eth_port->supp_aneg) {
		netdev_warn(netdev, "Autoneg is not supported.\n");
		err = -EOPNOTSUPP;
		goto err_bad_set;
	}

	err = __nfp_eth_set_aneg(nsp, req_aneg ? NFP_ANEG_AUTO : NFP_ANEG_DISABLED);
	if (err)
		goto err_bad_set;

	if (cmd->base.speed != SPEED_UNKNOWN) {
		u32 speed = cmd->base.speed / eth_port->lanes;

		if (req_aneg) {
			netdev_err(netdev, "Speed changing is not allowed when working on autoneg mode.\n");
			err = -EINVAL;
			goto err_bad_set;
		}

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
				  struct ethtool_ringparam *ring,
				  struct kernel_ethtool_ringparam *kernel_ring,
				  struct netlink_ext_ack *extack)
{
	struct nfp_net *nn = netdev_priv(netdev);
	u32 qc_max = nn->dev_info->max_qc_size;

	ring->rx_max_pending = qc_max;
	ring->tx_max_pending = qc_max / nn->dp.ops->tx_min_desc_per_pkt;
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
				 struct ethtool_ringparam *ring,
				 struct kernel_ethtool_ringparam *kernel_ring,
				 struct netlink_ext_ack *extack)
{
	u32 tx_dpp, qc_min, qc_max, rxd_cnt, txd_cnt;
	struct nfp_net *nn = netdev_priv(netdev);

	/* We don't have separate queues/rings for small/large frames. */
	if (ring->rx_mini_pending || ring->rx_jumbo_pending)
		return -EINVAL;

	qc_min = nn->dev_info->min_qc_size;
	qc_max = nn->dev_info->max_qc_size;
	tx_dpp = nn->dp.ops->tx_min_desc_per_pkt;
	/* Round up to supported values */
	rxd_cnt = roundup_pow_of_two(ring->rx_pending);
	txd_cnt = roundup_pow_of_two(ring->tx_pending);

	if (rxd_cnt < qc_min || rxd_cnt > qc_max ||
	    txd_cnt < qc_min / tx_dpp || txd_cnt > qc_max / tx_dpp)
		return -EINVAL;

	if (nn->dp.rxd_cnt == rxd_cnt && nn->dp.txd_cnt == txd_cnt)
		return 0;

	nn_dbg(nn, "Change ring size: RxQ %u->%u, TxQ %u->%u\n",
	       nn->dp.rxd_cnt, rxd_cnt, nn->dp.txd_cnt, txd_cnt);

	return nfp_net_set_ring_size(nn, rxd_cnt, txd_cnt);
}

static int nfp_test_link(struct net_device *netdev)
{
	if (!netif_carrier_ok(netdev) || !(netdev->flags & IFF_UP))
		return 1;

	return 0;
}

static int nfp_test_nsp(struct net_device *netdev)
{
	struct nfp_app *app = nfp_app_from_netdev(netdev);
	struct nfp_nsp_identify *nspi;
	struct nfp_nsp *nsp;
	int err;

	nsp = nfp_nsp_open(app->cpp);
	if (IS_ERR(nsp)) {
		err = PTR_ERR(nsp);
		netdev_info(netdev, "NSP Test: failed to access the NSP: %d\n", err);
		goto exit;
	}

	if (nfp_nsp_get_abi_ver_minor(nsp) < 15) {
		err = -EOPNOTSUPP;
		goto exit_close_nsp;
	}

	nspi = kzalloc(sizeof(*nspi), GFP_KERNEL);
	if (!nspi) {
		err = -ENOMEM;
		goto exit_close_nsp;
	}

	err = nfp_nsp_read_identify(nsp, nspi, sizeof(*nspi));
	if (err < 0)
		netdev_info(netdev, "NSP Test: reading bsp version failed %d\n", err);

	kfree(nspi);
exit_close_nsp:
	nfp_nsp_close(nsp);
exit:
	return err;
}

static int nfp_test_fw(struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int err;

	err = nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_GEN);
	if (err)
		netdev_info(netdev, "FW Test: update failed %d\n", err);

	return err;
}

static int nfp_test_reg(struct net_device *netdev)
{
	struct nfp_app *app = nfp_app_from_netdev(netdev);
	struct nfp_cpp *cpp = app->cpp;
	u32 model = nfp_cpp_model(cpp);
	u32 value;
	int err;

	err = nfp_cpp_model_autodetect(cpp, &value);
	if (err < 0) {
		netdev_info(netdev, "REG Test: NFP model detection failed %d\n", err);
		return err;
	}

	return (value == model) ? 0 : 1;
}

static bool link_test_supported(struct net_device *netdev)
{
	return true;
}

static bool nsp_test_supported(struct net_device *netdev)
{
	if (nfp_app_from_netdev(netdev))
		return true;

	return false;
}

static bool fw_test_supported(struct net_device *netdev)
{
	if (nfp_netdev_is_nfp_net(netdev))
		return true;

	return false;
}

static bool reg_test_supported(struct net_device *netdev)
{
	if (nfp_app_from_netdev(netdev))
		return true;

	return false;
}

static struct nfp_self_test_item {
	char name[ETH_GSTRING_LEN];
	bool (*is_supported)(struct net_device *dev);
	int (*func)(struct net_device *dev);
} nfp_self_test[] = {
	{"Link Test", link_test_supported, nfp_test_link},
	{"NSP Test", nsp_test_supported, nfp_test_nsp},
	{"Firmware Test", fw_test_supported, nfp_test_fw},
	{"Register Test", reg_test_supported, nfp_test_reg}
};

#define NFP_TEST_TOTAL_NUM ARRAY_SIZE(nfp_self_test)

static void nfp_get_self_test_strings(struct net_device *netdev, u8 *data)
{
	int i;

	for (i = 0; i < NFP_TEST_TOTAL_NUM; i++)
		if (nfp_self_test[i].is_supported(netdev))
			ethtool_sprintf(&data, nfp_self_test[i].name);
}

static int nfp_get_self_test_count(struct net_device *netdev)
{
	int i, count = 0;

	for (i = 0; i < NFP_TEST_TOTAL_NUM; i++)
		if (nfp_self_test[i].is_supported(netdev))
			count++;

	return count;
}

static void nfp_net_self_test(struct net_device *netdev, struct ethtool_test *eth_test,
			      u64 *data)
{
	int i, ret, count = 0;

	netdev_info(netdev, "Start self test\n");

	for (i = 0; i < NFP_TEST_TOTAL_NUM; i++) {
		if (nfp_self_test[i].is_supported(netdev)) {
			ret = nfp_self_test[i].func(netdev);
			if (ret)
				eth_test->flags |= ETH_TEST_FL_FAILED;
			data[count++] = ret;
		}
	}

	netdev_info(netdev, "Test end\n");
}

static unsigned int nfp_vnic_get_sw_stats_count(struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);

	return NN_RVEC_GATHER_STATS + nn->max_r_vecs * NN_RVEC_PER_Q_STATS +
		NN_CTRL_PATH_STATS;
}

static u8 *nfp_vnic_get_sw_stats_strings(struct net_device *netdev, u8 *data)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int i;

	for (i = 0; i < nn->max_r_vecs; i++) {
		ethtool_sprintf(&data, "rvec_%u_rx_pkts", i);
		ethtool_sprintf(&data, "rvec_%u_tx_pkts", i);
		ethtool_sprintf(&data, "rvec_%u_tx_busy", i);
	}

	ethtool_sprintf(&data, "hw_rx_csum_ok");
	ethtool_sprintf(&data, "hw_rx_csum_inner_ok");
	ethtool_sprintf(&data, "hw_rx_csum_complete");
	ethtool_sprintf(&data, "hw_rx_csum_err");
	ethtool_sprintf(&data, "rx_replace_buf_alloc_fail");
	ethtool_sprintf(&data, "rx_tls_decrypted_packets");
	ethtool_sprintf(&data, "hw_tx_csum");
	ethtool_sprintf(&data, "hw_tx_inner_csum");
	ethtool_sprintf(&data, "tx_gather");
	ethtool_sprintf(&data, "tx_lso");
	ethtool_sprintf(&data, "tx_tls_encrypted_packets");
	ethtool_sprintf(&data, "tx_tls_ooo");
	ethtool_sprintf(&data, "tx_tls_drop_no_sync_data");

	ethtool_sprintf(&data, "hw_tls_no_space");
	ethtool_sprintf(&data, "rx_tls_resync_req_ok");
	ethtool_sprintf(&data, "rx_tls_resync_req_ign");
	ethtool_sprintf(&data, "rx_tls_resync_sent");

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
			start = u64_stats_fetch_begin_irq(&nn->r_vecs[i].rx_sync);
			data[0] = nn->r_vecs[i].rx_pkts;
			tmp[0] = nn->r_vecs[i].hw_csum_rx_ok;
			tmp[1] = nn->r_vecs[i].hw_csum_rx_inner_ok;
			tmp[2] = nn->r_vecs[i].hw_csum_rx_complete;
			tmp[3] = nn->r_vecs[i].hw_csum_rx_error;
			tmp[4] = nn->r_vecs[i].rx_replace_buf_alloc_fail;
			tmp[5] = nn->r_vecs[i].hw_tls_rx;
		} while (u64_stats_fetch_retry_irq(&nn->r_vecs[i].rx_sync, start));

		do {
			start = u64_stats_fetch_begin_irq(&nn->r_vecs[i].tx_sync);
			data[1] = nn->r_vecs[i].tx_pkts;
			data[2] = nn->r_vecs[i].tx_busy;
			tmp[6] = nn->r_vecs[i].hw_csum_tx;
			tmp[7] = nn->r_vecs[i].hw_csum_tx_inner;
			tmp[8] = nn->r_vecs[i].tx_gather;
			tmp[9] = nn->r_vecs[i].tx_lso;
			tmp[10] = nn->r_vecs[i].hw_tls_tx;
			tmp[11] = nn->r_vecs[i].tls_tx_fallback;
			tmp[12] = nn->r_vecs[i].tls_tx_no_fallback;
		} while (u64_stats_fetch_retry_irq(&nn->r_vecs[i].tx_sync, start));

		data += NN_RVEC_PER_Q_STATS;

		for (j = 0; j < NN_RVEC_GATHER_STATS; j++)
			gathered_stats[j] += tmp[j];
	}

	for (j = 0; j < NN_RVEC_GATHER_STATS; j++)
		*data++ = gathered_stats[j];

	*data++ = atomic_read(&nn->ktls_no_space);
	*data++ = atomic_read(&nn->ktls_rx_resync_req);
	*data++ = atomic_read(&nn->ktls_rx_resync_ign);
	*data++ = atomic_read(&nn->ktls_rx_resync_sent);

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
		ethtool_sprintf(&data, nfp_net_et_stats[i + swap_off].name);

	for (i = NN_ET_SWITCH_STATS_LEN; i < NN_ET_SWITCH_STATS_LEN * 2; i++)
		ethtool_sprintf(&data, nfp_net_et_stats[i - swap_off].name);

	for (i = NN_ET_SWITCH_STATS_LEN * 2; i < NN_ET_GLOBAL_STATS_LEN; i++)
		ethtool_sprintf(&data, nfp_net_et_stats[i].name);

	for (i = 0; i < num_vecs; i++) {
		ethtool_sprintf(&data, "rxq_%u_pkts", i);
		ethtool_sprintf(&data, "rxq_%u_bytes", i);
		ethtool_sprintf(&data, "txq_%u_pkts", i);
		ethtool_sprintf(&data, "txq_%u_bytes", i);
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

static unsigned int nfp_vnic_get_tlv_stats_count(struct nfp_net *nn)
{
	return nn->tlv_caps.vnic_stats_cnt + nn->max_r_vecs * 4;
}

static u8 *nfp_vnic_get_tlv_stats_strings(struct nfp_net *nn, u8 *data)
{
	unsigned int i, id;
	u8 __iomem *mem;
	u64 id_word = 0;

	mem = nn->dp.ctrl_bar + nn->tlv_caps.vnic_stats_off;
	for (i = 0; i < nn->tlv_caps.vnic_stats_cnt; i++) {
		if (!(i % 4))
			id_word = readq(mem + i * 2);

		id = (u16)id_word;
		id_word >>= 16;

		if (id < ARRAY_SIZE(nfp_tlv_stat_names) &&
		    nfp_tlv_stat_names[id][0]) {
			memcpy(data, nfp_tlv_stat_names[id], ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		} else {
			ethtool_sprintf(&data, "dev_unknown_stat%u", id);
		}
	}

	for (i = 0; i < nn->max_r_vecs; i++) {
		ethtool_sprintf(&data, "rxq_%u_pkts", i);
		ethtool_sprintf(&data, "rxq_%u_bytes", i);
		ethtool_sprintf(&data, "txq_%u_pkts", i);
		ethtool_sprintf(&data, "txq_%u_bytes", i);
	}

	return data;
}

static u64 *nfp_vnic_get_tlv_stats(struct nfp_net *nn, u64 *data)
{
	u8 __iomem *mem;
	unsigned int i;

	mem = nn->dp.ctrl_bar + nn->tlv_caps.vnic_stats_off;
	mem += roundup(2 * nn->tlv_caps.vnic_stats_cnt, 8);
	for (i = 0; i < nn->tlv_caps.vnic_stats_cnt; i++)
		*data++ = readq(mem + i * 8);

	mem = nn->dp.ctrl_bar;
	for (i = 0; i < nn->max_r_vecs; i++) {
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
		ethtool_sprintf(&data, "mac.%s", nfp_mac_et_stats[i].name);

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
		if (!nn->tlv_caps.vnic_stats_off)
			data = nfp_vnic_get_hw_stats_strings(data,
							     nn->max_r_vecs,
							     false);
		else
			data = nfp_vnic_get_tlv_stats_strings(nn, data);
		data = nfp_mac_get_stats_strings(netdev, data);
		data = nfp_app_port_get_stats_strings(nn->port, data);
		break;
	case ETH_SS_TEST:
		nfp_get_self_test_strings(netdev, data);
		break;
	}
}

static void
nfp_net_get_stats(struct net_device *netdev, struct ethtool_stats *stats,
		  u64 *data)
{
	struct nfp_net *nn = netdev_priv(netdev);

	data = nfp_vnic_get_sw_stats(netdev, data);
	if (!nn->tlv_caps.vnic_stats_off)
		data = nfp_vnic_get_hw_stats(data, nn->dp.ctrl_bar,
					     nn->max_r_vecs);
	else
		data = nfp_vnic_get_tlv_stats(nn, data);
	data = nfp_mac_get_stats(netdev, data);
	data = nfp_app_port_get_stats(nn->port, data);
}

static int nfp_net_get_sset_count(struct net_device *netdev, int sset)
{
	struct nfp_net *nn = netdev_priv(netdev);
	unsigned int cnt;

	switch (sset) {
	case ETH_SS_STATS:
		cnt = nfp_vnic_get_sw_stats_count(netdev);
		if (!nn->tlv_caps.vnic_stats_off)
			cnt += nfp_vnic_get_hw_stats_count(nn->max_r_vecs);
		else
			cnt += nfp_vnic_get_tlv_stats_count(nn);
		cnt += nfp_mac_get_stats_count(netdev);
		cnt += nfp_app_port_get_stats_count(nn->port);
		return cnt;
	case ETH_SS_TEST:
		return nfp_get_self_test_count(netdev);
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
	case ETH_SS_TEST:
		nfp_get_self_test_strings(netdev, data);
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
	case ETH_SS_TEST:
		return nfp_get_self_test_count(netdev);
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

	param->active_fec = ETHTOOL_FEC_NONE;
	param->fec = ETHTOOL_FEC_NONE;

	port = nfp_port_from_netdev(netdev);
	eth_port = nfp_port_get_eth_port(port);
	if (!eth_port)
		return -EOPNOTSUPP;

	if (!nfp_eth_can_support_fec(eth_port))
		return 0;

	param->fec = nfp_port_fec_nsp_to_ethtool(eth_port->fec_modes_supported);
	param->active_fec = nfp_port_fec_nsp_to_ethtool(BIT(eth_port->act_fec));

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
				struct ethtool_coalesce *ec,
				struct kernel_ethtool_coalesce *kernel_coal,
				struct netlink_ext_ack *extack)
{
	struct nfp_net *nn = netdev_priv(netdev);

	if (!(nn->cap & NFP_NET_CFG_CTRL_IRQMOD))
		return -EINVAL;

	ec->use_adaptive_rx_coalesce = nn->rx_coalesce_adapt_on;
	ec->use_adaptive_tx_coalesce = nn->tx_coalesce_adapt_on;

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

static int
nfp_port_get_module_info(struct net_device *netdev,
			 struct ethtool_modinfo *modinfo)
{
	struct nfp_eth_table_port *eth_port;
	struct nfp_port *port;
	unsigned int read_len;
	struct nfp_nsp *nsp;
	int err = 0;
	u8 data;

	port = nfp_port_from_netdev(netdev);
	/* update port state to get latest interface */
	set_bit(NFP_PORT_CHANGED, &port->flags);
	eth_port = nfp_port_get_eth_port(port);
	if (!eth_port)
		return -EOPNOTSUPP;

	nsp = nfp_nsp_open(port->app->cpp);
	if (IS_ERR(nsp)) {
		err = PTR_ERR(nsp);
		netdev_err(netdev, "Failed to access the NSP: %d\n", err);
		return err;
	}

	if (!nfp_nsp_has_read_module_eeprom(nsp)) {
		netdev_info(netdev, "reading module EEPROM not supported. Please update flash\n");
		err = -EOPNOTSUPP;
		goto exit_close_nsp;
	}

	switch (eth_port->interface) {
	case NFP_INTERFACE_SFP:
	case NFP_INTERFACE_SFP28:
		err = nfp_nsp_read_module_eeprom(nsp, eth_port->eth_index,
						 SFP_SFF8472_COMPLIANCE, &data,
						 1, &read_len);
		if (err < 0)
			goto exit_close_nsp;

		if (!data) {
			modinfo->type = ETH_MODULE_SFF_8079;
			modinfo->eeprom_len = ETH_MODULE_SFF_8079_LEN;
		} else {
			modinfo->type = ETH_MODULE_SFF_8472;
			modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		}
		break;
	case NFP_INTERFACE_QSFP:
		err = nfp_nsp_read_module_eeprom(nsp, eth_port->eth_index,
						 SFP_SFF_REV_COMPLIANCE, &data,
						 1, &read_len);
		if (err < 0)
			goto exit_close_nsp;

		if (data < 0x3) {
			modinfo->type = ETH_MODULE_SFF_8436;
			modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		} else {
			modinfo->type = ETH_MODULE_SFF_8636;
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		}
		break;
	case NFP_INTERFACE_QSFP28:
		modinfo->type = ETH_MODULE_SFF_8636;
		modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		break;
	default:
		netdev_err(netdev, "Unsupported module 0x%x detected\n",
			   eth_port->interface);
		err = -EINVAL;
	}

exit_close_nsp:
	nfp_nsp_close(nsp);
	return err;
}

static int
nfp_port_get_module_eeprom(struct net_device *netdev,
			   struct ethtool_eeprom *eeprom, u8 *data)
{
	struct nfp_eth_table_port *eth_port;
	struct nfp_port *port;
	struct nfp_nsp *nsp;
	int err;

	port = nfp_port_from_netdev(netdev);
	eth_port = __nfp_port_get_eth_port(port);
	if (!eth_port)
		return -EOPNOTSUPP;

	nsp = nfp_nsp_open(port->app->cpp);
	if (IS_ERR(nsp)) {
		err = PTR_ERR(nsp);
		netdev_err(netdev, "Failed to access the NSP: %d\n", err);
		return err;
	}

	if (!nfp_nsp_has_read_module_eeprom(nsp)) {
		netdev_info(netdev, "reading module EEPROM not supported. Please update flash\n");
		err = -EOPNOTSUPP;
		goto exit_close_nsp;
	}

	err = nfp_nsp_read_module_eeprom(nsp, eth_port->eth_index,
					 eeprom->offset, data, eeprom->len,
					 &eeprom->len);
	if (err < 0) {
		if (eeprom->len) {
			netdev_warn(netdev,
				    "Incomplete read from module EEPROM: %d\n",
				     err);
			err = 0;
		} else {
			netdev_err(netdev,
				   "Reading from module EEPROM failed: %d\n",
				   err);
		}
	}

exit_close_nsp:
	nfp_nsp_close(nsp);
	return err;
}

static int nfp_net_set_coalesce(struct net_device *netdev,
				struct ethtool_coalesce *ec,
				struct kernel_ethtool_coalesce *kernel_coal,
				struct netlink_ext_ack *extack)
{
	struct nfp_net *nn = netdev_priv(netdev);
	unsigned int factor;

	/* Compute factor used to convert coalesce '_usecs' parameters to
	 * ME timestamp ticks.  There are 16 ME clock cycles for each timestamp
	 * count.
	 */
	factor = nn->tlv_caps.me_freq_mhz / 16;

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

	if (nfp_net_coalesce_para_check(ec->rx_coalesce_usecs * factor,
					ec->rx_max_coalesced_frames))
		return -EINVAL;

	if (nfp_net_coalesce_para_check(ec->tx_coalesce_usecs * factor,
					ec->tx_max_coalesced_frames))
		return -EINVAL;

	/* configuration is valid */
	nn->rx_coalesce_adapt_on = !!ec->use_adaptive_rx_coalesce;
	nn->tx_coalesce_adapt_on = !!ec->use_adaptive_tx_coalesce;

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
	if (channel->other_count != NFP_NET_NON_Q_VECTORS ||
	    (channel->rx_count && channel->tx_count))
		return -EINVAL;

	total_rx = channel->combined_count + channel->rx_count;
	total_tx = channel->combined_count + channel->tx_count;

	if (total_rx > min(nn->max_rx_rings, nn->max_r_vecs) ||
	    total_tx > min(nn->max_tx_rings, nn->max_r_vecs))
		return -EINVAL;

	return nfp_net_set_num_rings(nn, total_rx, total_tx);
}

static void nfp_port_get_pauseparam(struct net_device *netdev,
				    struct ethtool_pauseparam *pause)
{
	struct nfp_eth_table_port *eth_port;
	struct nfp_port *port;

	port = nfp_port_from_netdev(netdev);
	eth_port = nfp_port_get_eth_port(port);
	if (!eth_port)
		return;

	/* Currently pause frame support is fixed */
	pause->autoneg = AUTONEG_DISABLE;
	pause->rx_pause = 1;
	pause->tx_pause = 1;
}

static int nfp_net_set_phys_id(struct net_device *netdev,
			       enum ethtool_phys_id_state state)
{
	struct nfp_eth_table_port *eth_port;
	struct nfp_port *port;
	int err;

	port = nfp_port_from_netdev(netdev);
	eth_port = __nfp_port_get_eth_port(port);
	if (!eth_port)
		return -EOPNOTSUPP;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		/* Control LED to blink */
		err = nfp_eth_set_idmode(port->app->cpp, eth_port->index, 1);
		break;

	case ETHTOOL_ID_INACTIVE:
		/* Control LED to normal mode */
		err = nfp_eth_set_idmode(port->app->cpp, eth_port->index, 0);
		break;

	case ETHTOOL_ID_ON:
	case ETHTOOL_ID_OFF:
	default:
		return -EOPNOTSUPP;
	}

	return err;
}

#define NFP_EEPROM_LEN ETH_ALEN

static int
nfp_net_get_eeprom_len(struct net_device *netdev)
{
	struct nfp_eth_table_port *eth_port;
	struct nfp_port *port;

	port = nfp_port_from_netdev(netdev);
	eth_port = __nfp_port_get_eth_port(port);
	if (!eth_port)
		return 0;

	return NFP_EEPROM_LEN;
}

static int
nfp_net_get_nsp_hwindex(struct net_device *netdev,
			struct nfp_nsp **nspptr,
			u32 *index)
{
	struct nfp_eth_table_port *eth_port;
	struct nfp_port *port;
	struct nfp_nsp *nsp;
	int err;

	port = nfp_port_from_netdev(netdev);
	eth_port = __nfp_port_get_eth_port(port);
	if (!eth_port)
		return -EOPNOTSUPP;

	nsp = nfp_nsp_open(port->app->cpp);
	if (IS_ERR(nsp)) {
		err = PTR_ERR(nsp);
		netdev_err(netdev, "Failed to access the NSP: %d\n", err);
		return err;
	}

	if (!nfp_nsp_has_hwinfo_lookup(nsp)) {
		netdev_err(netdev, "NSP doesn't support PF MAC generation\n");
		nfp_nsp_close(nsp);
		return -EOPNOTSUPP;
	}

	*nspptr = nsp;
	*index = eth_port->eth_index;

	return 0;
}

static int
nfp_net_get_port_mac_by_hwinfo(struct net_device *netdev,
			       u8 *mac_addr)
{
	char hwinfo[32] = {};
	struct nfp_nsp *nsp;
	u32 index;
	int err;

	err = nfp_net_get_nsp_hwindex(netdev, &nsp, &index);
	if (err)
		return err;

	snprintf(hwinfo, sizeof(hwinfo), "eth%u.mac", index);
	err = nfp_nsp_hwinfo_lookup(nsp, hwinfo, sizeof(hwinfo));
	nfp_nsp_close(nsp);
	if (err) {
		netdev_err(netdev, "Reading persistent MAC address failed: %d\n",
			   err);
		return -EOPNOTSUPP;
	}

	if (sscanf(hwinfo, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		   &mac_addr[0], &mac_addr[1], &mac_addr[2],
		   &mac_addr[3], &mac_addr[4], &mac_addr[5]) != 6) {
		netdev_err(netdev, "Can't parse persistent MAC address (%s)\n",
			   hwinfo);
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
nfp_net_set_port_mac_by_hwinfo(struct net_device *netdev,
			       u8 *mac_addr)
{
	char hwinfo[32] = {};
	struct nfp_nsp *nsp;
	u32 index;
	int err;

	err = nfp_net_get_nsp_hwindex(netdev, &nsp, &index);
	if (err)
		return err;

	snprintf(hwinfo, sizeof(hwinfo),
		 "eth%u.mac=%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		 index, mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
		 mac_addr[4], mac_addr[5]);

	err = nfp_nsp_hwinfo_set(nsp, hwinfo, sizeof(hwinfo));
	nfp_nsp_close(nsp);
	if (err) {
		netdev_err(netdev, "HWinfo set failed: %d, hwinfo: %s\n",
			   err, hwinfo);
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
nfp_net_get_eeprom(struct net_device *netdev,
		   struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct nfp_net *nn = netdev_priv(netdev);
	u8 buf[NFP_EEPROM_LEN] = {};

	if (eeprom->len == 0)
		return -EINVAL;

	if (nfp_net_get_port_mac_by_hwinfo(netdev, buf))
		return -EOPNOTSUPP;

	eeprom->magic = nn->pdev->vendor | (nn->pdev->device << 16);
	memcpy(bytes, buf + eeprom->offset, eeprom->len);

	return 0;
}

static int
nfp_net_set_eeprom(struct net_device *netdev,
		   struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct nfp_net *nn = netdev_priv(netdev);
	u8 buf[NFP_EEPROM_LEN] = {};

	if (eeprom->len == 0)
		return -EINVAL;

	if (eeprom->magic != (nn->pdev->vendor | nn->pdev->device << 16))
		return -EINVAL;

	if (nfp_net_get_port_mac_by_hwinfo(netdev, buf))
		return -EOPNOTSUPP;

	memcpy(buf + eeprom->offset, bytes, eeprom->len);
	if (nfp_net_set_port_mac_by_hwinfo(netdev, buf))
		return -EOPNOTSUPP;

	return 0;
}

static const struct ethtool_ops nfp_net_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_MAX_FRAMES |
				     ETHTOOL_COALESCE_USE_ADAPTIVE,
	.get_drvinfo		= nfp_net_get_drvinfo,
	.nway_reset             = nfp_net_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_ringparam		= nfp_net_get_ringparam,
	.set_ringparam		= nfp_net_set_ringparam,
	.self_test		= nfp_net_self_test,
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
	.set_dump		= nfp_app_set_dump,
	.get_dump_flag		= nfp_app_get_dump_flag,
	.get_dump_data		= nfp_app_get_dump_data,
	.get_eeprom_len         = nfp_net_get_eeprom_len,
	.get_eeprom             = nfp_net_get_eeprom,
	.set_eeprom             = nfp_net_set_eeprom,
	.get_module_info	= nfp_port_get_module_info,
	.get_module_eeprom	= nfp_port_get_module_eeprom,
	.get_coalesce           = nfp_net_get_coalesce,
	.set_coalesce           = nfp_net_set_coalesce,
	.get_channels		= nfp_net_get_channels,
	.set_channels		= nfp_net_set_channels,
	.get_link_ksettings	= nfp_net_get_link_ksettings,
	.set_link_ksettings	= nfp_net_set_link_ksettings,
	.get_fecparam		= nfp_port_get_fecparam,
	.set_fecparam		= nfp_port_set_fecparam,
	.get_pauseparam		= nfp_port_get_pauseparam,
	.set_phys_id		= nfp_net_set_phys_id,
};

const struct ethtool_ops nfp_port_ethtool_ops = {
	.get_drvinfo		= nfp_app_get_drvinfo,
	.nway_reset             = nfp_net_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_strings		= nfp_port_get_strings,
	.get_ethtool_stats	= nfp_port_get_stats,
	.self_test		= nfp_net_self_test,
	.get_sset_count		= nfp_port_get_sset_count,
	.set_dump		= nfp_app_set_dump,
	.get_dump_flag		= nfp_app_get_dump_flag,
	.get_dump_data		= nfp_app_get_dump_data,
	.get_module_info	= nfp_port_get_module_info,
	.get_module_eeprom	= nfp_port_get_module_eeprom,
	.get_link_ksettings	= nfp_net_get_link_ksettings,
	.set_link_ksettings	= nfp_net_set_link_ksettings,
	.get_fecparam		= nfp_port_get_fecparam,
	.set_fecparam		= nfp_port_set_fecparam,
	.get_pauseparam		= nfp_port_get_pauseparam,
	.set_phys_id		= nfp_net_set_phys_id,
};

void nfp_net_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &nfp_net_ethtool_ops;
}
