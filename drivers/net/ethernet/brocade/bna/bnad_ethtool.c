// SPDX-License-Identifier: GPL-2.0-only
/*
 * Linux network driver for QLogic BR-series Converged Network Adapter.
 */
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014-2015 QLogic Corporation
 * All rights reserved
 * www.qlogic.com
 */

#include "cna.h"

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/rtnetlink.h>

#include "bna.h"

#include "bnad.h"

#define BNAD_NUM_TXF_COUNTERS 12
#define BNAD_NUM_RXF_COUNTERS 10
#define BNAD_NUM_CQ_COUNTERS (3 + 5)
#define BNAD_NUM_RXQ_COUNTERS 7
#define BNAD_NUM_TXQ_COUNTERS 5

static const char *bnad_net_stats_strings[] = {
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
	"rx_crc_errors",
	"rx_frame_errors",
	"tx_fifo_errors",

	"netif_queue_stop",
	"netif_queue_wakeup",
	"netif_queue_stopped",
	"tso4",
	"tso6",
	"tso_err",
	"tcpcsum_offload",
	"udpcsum_offload",
	"csum_help",
	"tx_skb_too_short",
	"tx_skb_stopping",
	"tx_skb_max_vectors",
	"tx_skb_mss_too_long",
	"tx_skb_tso_too_short",
	"tx_skb_tso_prepare",
	"tx_skb_non_tso_too_long",
	"tx_skb_tcp_hdr",
	"tx_skb_udp_hdr",
	"tx_skb_csum_err",
	"tx_skb_headlen_too_long",
	"tx_skb_headlen_zero",
	"tx_skb_frag_zero",
	"tx_skb_len_mismatch",
	"tx_skb_map_failed",
	"hw_stats_updates",
	"netif_rx_dropped",

	"link_toggle",
	"cee_toggle",

	"rxp_info_alloc_failed",
	"mbox_intr_disabled",
	"mbox_intr_enabled",
	"tx_unmap_q_alloc_failed",
	"rx_unmap_q_alloc_failed",
	"rxbuf_alloc_failed",
	"rxbuf_map_failed",

	"mac_stats_clr_cnt",
	"mac_frame_64",
	"mac_frame_65_127",
	"mac_frame_128_255",
	"mac_frame_256_511",
	"mac_frame_512_1023",
	"mac_frame_1024_1518",
	"mac_frame_1518_1522",
	"mac_rx_bytes",
	"mac_rx_packets",
	"mac_rx_fcs_error",
	"mac_rx_multicast",
	"mac_rx_broadcast",
	"mac_rx_control_frames",
	"mac_rx_pause",
	"mac_rx_unknown_opcode",
	"mac_rx_alignment_error",
	"mac_rx_frame_length_error",
	"mac_rx_code_error",
	"mac_rx_carrier_sense_error",
	"mac_rx_undersize",
	"mac_rx_oversize",
	"mac_rx_fragments",
	"mac_rx_jabber",
	"mac_rx_drop",

	"mac_tx_bytes",
	"mac_tx_packets",
	"mac_tx_multicast",
	"mac_tx_broadcast",
	"mac_tx_pause",
	"mac_tx_deferral",
	"mac_tx_excessive_deferral",
	"mac_tx_single_collision",
	"mac_tx_multiple_collision",
	"mac_tx_late_collision",
	"mac_tx_excessive_collision",
	"mac_tx_total_collision",
	"mac_tx_pause_honored",
	"mac_tx_drop",
	"mac_tx_jabber",
	"mac_tx_fcs_error",
	"mac_tx_control_frame",
	"mac_tx_oversize",
	"mac_tx_undersize",
	"mac_tx_fragments",

	"bpc_tx_pause_0",
	"bpc_tx_pause_1",
	"bpc_tx_pause_2",
	"bpc_tx_pause_3",
	"bpc_tx_pause_4",
	"bpc_tx_pause_5",
	"bpc_tx_pause_6",
	"bpc_tx_pause_7",
	"bpc_tx_zero_pause_0",
	"bpc_tx_zero_pause_1",
	"bpc_tx_zero_pause_2",
	"bpc_tx_zero_pause_3",
	"bpc_tx_zero_pause_4",
	"bpc_tx_zero_pause_5",
	"bpc_tx_zero_pause_6",
	"bpc_tx_zero_pause_7",
	"bpc_tx_first_pause_0",
	"bpc_tx_first_pause_1",
	"bpc_tx_first_pause_2",
	"bpc_tx_first_pause_3",
	"bpc_tx_first_pause_4",
	"bpc_tx_first_pause_5",
	"bpc_tx_first_pause_6",
	"bpc_tx_first_pause_7",

	"bpc_rx_pause_0",
	"bpc_rx_pause_1",
	"bpc_rx_pause_2",
	"bpc_rx_pause_3",
	"bpc_rx_pause_4",
	"bpc_rx_pause_5",
	"bpc_rx_pause_6",
	"bpc_rx_pause_7",
	"bpc_rx_zero_pause_0",
	"bpc_rx_zero_pause_1",
	"bpc_rx_zero_pause_2",
	"bpc_rx_zero_pause_3",
	"bpc_rx_zero_pause_4",
	"bpc_rx_zero_pause_5",
	"bpc_rx_zero_pause_6",
	"bpc_rx_zero_pause_7",
	"bpc_rx_first_pause_0",
	"bpc_rx_first_pause_1",
	"bpc_rx_first_pause_2",
	"bpc_rx_first_pause_3",
	"bpc_rx_first_pause_4",
	"bpc_rx_first_pause_5",
	"bpc_rx_first_pause_6",
	"bpc_rx_first_pause_7",

	"rad_rx_frames",
	"rad_rx_octets",
	"rad_rx_vlan_frames",
	"rad_rx_ucast",
	"rad_rx_ucast_octets",
	"rad_rx_ucast_vlan",
	"rad_rx_mcast",
	"rad_rx_mcast_octets",
	"rad_rx_mcast_vlan",
	"rad_rx_bcast",
	"rad_rx_bcast_octets",
	"rad_rx_bcast_vlan",
	"rad_rx_drops",

	"rlb_rad_rx_frames",
	"rlb_rad_rx_octets",
	"rlb_rad_rx_vlan_frames",
	"rlb_rad_rx_ucast",
	"rlb_rad_rx_ucast_octets",
	"rlb_rad_rx_ucast_vlan",
	"rlb_rad_rx_mcast",
	"rlb_rad_rx_mcast_octets",
	"rlb_rad_rx_mcast_vlan",
	"rlb_rad_rx_bcast",
	"rlb_rad_rx_bcast_octets",
	"rlb_rad_rx_bcast_vlan",
	"rlb_rad_rx_drops",

	"fc_rx_ucast_octets",
	"fc_rx_ucast",
	"fc_rx_ucast_vlan",
	"fc_rx_mcast_octets",
	"fc_rx_mcast",
	"fc_rx_mcast_vlan",
	"fc_rx_bcast_octets",
	"fc_rx_bcast",
	"fc_rx_bcast_vlan",

	"fc_tx_ucast_octets",
	"fc_tx_ucast",
	"fc_tx_ucast_vlan",
	"fc_tx_mcast_octets",
	"fc_tx_mcast",
	"fc_tx_mcast_vlan",
	"fc_tx_bcast_octets",
	"fc_tx_bcast",
	"fc_tx_bcast_vlan",
	"fc_tx_parity_errors",
	"fc_tx_timeout",
	"fc_tx_fid_parity_errors",
};

#define BNAD_ETHTOOL_STATS_NUM	ARRAY_SIZE(bnad_net_stats_strings)

static int
bnad_get_link_ksettings(struct net_device *netdev,
			struct ethtool_link_ksettings *cmd)
{
	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_zero_link_mode(cmd, advertising);

	ethtool_link_ksettings_add_link_mode(cmd, supported, 10000baseCR_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 10000baseSR_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 10000baseLR_Full);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, 10000baseCR_Full);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, 10000baseSR_Full);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, 10000baseLR_Full);
	cmd->base.autoneg = AUTONEG_DISABLE;
	ethtool_link_ksettings_add_link_mode(cmd, supported, FIBRE);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, FIBRE);
	cmd->base.port = PORT_FIBRE;
	cmd->base.phy_address = 0;

	if (netif_carrier_ok(netdev)) {
		cmd->base.speed = SPEED_10000;
		cmd->base.duplex = DUPLEX_FULL;
	} else {
		cmd->base.speed = SPEED_UNKNOWN;
		cmd->base.duplex = DUPLEX_UNKNOWN;
	}

	return 0;
}

static int
bnad_set_link_ksettings(struct net_device *netdev,
			const struct ethtool_link_ksettings *cmd)
{
	/* 10G full duplex setting supported only */
	if (cmd->base.autoneg == AUTONEG_ENABLE)
		return -EOPNOTSUPP;

	if ((cmd->base.speed == SPEED_10000) &&
	    (cmd->base.duplex == DUPLEX_FULL))
		return 0;

	return -EOPNOTSUPP;
}

static void
bnad_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct bnad *bnad = netdev_priv(netdev);
	struct bfa_ioc_attr *ioc_attr;
	unsigned long flags;

	strscpy(drvinfo->driver, BNAD_NAME, sizeof(drvinfo->driver));

	ioc_attr = kzalloc(sizeof(*ioc_attr), GFP_KERNEL);
	if (ioc_attr) {
		spin_lock_irqsave(&bnad->bna_lock, flags);
		bfa_nw_ioc_get_attr(&bnad->bna.ioceth.ioc, ioc_attr);
		spin_unlock_irqrestore(&bnad->bna_lock, flags);

		strscpy(drvinfo->fw_version, ioc_attr->adapter_attr.fw_ver,
			sizeof(drvinfo->fw_version));
		kfree(ioc_attr);
	}

	strscpy(drvinfo->bus_info, pci_name(bnad->pcidev),
		sizeof(drvinfo->bus_info));
}

static void
bnad_get_wol(struct net_device *netdev, struct ethtool_wolinfo *wolinfo)
{
	wolinfo->supported = 0;
	wolinfo->wolopts = 0;
}

static int bnad_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *coalesce,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
{
	struct bnad *bnad = netdev_priv(netdev);
	unsigned long flags;

	/* Lock rqd. to access bnad->bna_lock */
	spin_lock_irqsave(&bnad->bna_lock, flags);
	coalesce->use_adaptive_rx_coalesce =
		(bnad->cfg_flags & BNAD_CF_DIM_ENABLED) ? true : false;
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	coalesce->rx_coalesce_usecs = bnad->rx_coalescing_timeo *
					BFI_COALESCING_TIMER_UNIT;
	coalesce->tx_coalesce_usecs = bnad->tx_coalescing_timeo *
					BFI_COALESCING_TIMER_UNIT;
	coalesce->tx_max_coalesced_frames = BFI_TX_INTERPKT_COUNT;

	return 0;
}

static int bnad_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *coalesce,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
{
	struct bnad *bnad = netdev_priv(netdev);
	unsigned long flags;
	int to_del = 0;

	if (coalesce->rx_coalesce_usecs == 0 ||
	    coalesce->rx_coalesce_usecs >
	    BFI_MAX_COALESCING_TIMEO * BFI_COALESCING_TIMER_UNIT)
		return -EINVAL;

	if (coalesce->tx_coalesce_usecs == 0 ||
	    coalesce->tx_coalesce_usecs >
	    BFI_MAX_COALESCING_TIMEO * BFI_COALESCING_TIMER_UNIT)
		return -EINVAL;

	mutex_lock(&bnad->conf_mutex);
	/*
	 * Do not need to store rx_coalesce_usecs here
	 * Every time DIM is disabled, we can get it from the
	 * stack.
	 */
	spin_lock_irqsave(&bnad->bna_lock, flags);
	if (coalesce->use_adaptive_rx_coalesce) {
		if (!(bnad->cfg_flags & BNAD_CF_DIM_ENABLED)) {
			bnad->cfg_flags |= BNAD_CF_DIM_ENABLED;
			bnad_dim_timer_start(bnad);
		}
	} else {
		if (bnad->cfg_flags & BNAD_CF_DIM_ENABLED) {
			bnad->cfg_flags &= ~BNAD_CF_DIM_ENABLED;
			if (bnad->cfg_flags & BNAD_CF_DIM_ENABLED &&
			    test_bit(BNAD_RF_DIM_TIMER_RUNNING,
			    &bnad->run_flags)) {
				clear_bit(BNAD_RF_DIM_TIMER_RUNNING,
							&bnad->run_flags);
				to_del = 1;
			}
			spin_unlock_irqrestore(&bnad->bna_lock, flags);
			if (to_del)
				del_timer_sync(&bnad->dim_timer);
			spin_lock_irqsave(&bnad->bna_lock, flags);
			bnad_rx_coalescing_timeo_set(bnad);
		}
	}
	if (bnad->tx_coalescing_timeo != coalesce->tx_coalesce_usecs /
					BFI_COALESCING_TIMER_UNIT) {
		bnad->tx_coalescing_timeo = coalesce->tx_coalesce_usecs /
						BFI_COALESCING_TIMER_UNIT;
		bnad_tx_coalescing_timeo_set(bnad);
	}

	if (bnad->rx_coalescing_timeo != coalesce->rx_coalesce_usecs /
					BFI_COALESCING_TIMER_UNIT) {
		bnad->rx_coalescing_timeo = coalesce->rx_coalesce_usecs /
						BFI_COALESCING_TIMER_UNIT;

		if (!(bnad->cfg_flags & BNAD_CF_DIM_ENABLED))
			bnad_rx_coalescing_timeo_set(bnad);

	}

	/* Add Tx Inter-pkt DMA count?  */

	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	mutex_unlock(&bnad->conf_mutex);
	return 0;
}

static void
bnad_get_ringparam(struct net_device *netdev,
		   struct ethtool_ringparam *ringparam,
		   struct kernel_ethtool_ringparam *kernel_ringparam,
		   struct netlink_ext_ack *extack)
{
	struct bnad *bnad = netdev_priv(netdev);

	ringparam->rx_max_pending = BNAD_MAX_RXQ_DEPTH;
	ringparam->tx_max_pending = BNAD_MAX_TXQ_DEPTH;

	ringparam->rx_pending = bnad->rxq_depth;
	ringparam->tx_pending = bnad->txq_depth;
}

static int
bnad_set_ringparam(struct net_device *netdev,
		   struct ethtool_ringparam *ringparam,
		   struct kernel_ethtool_ringparam *kernel_ringparam,
		   struct netlink_ext_ack *extack)
{
	int i, current_err, err = 0;
	struct bnad *bnad = netdev_priv(netdev);
	unsigned long flags;

	mutex_lock(&bnad->conf_mutex);
	if (ringparam->rx_pending == bnad->rxq_depth &&
	    ringparam->tx_pending == bnad->txq_depth) {
		mutex_unlock(&bnad->conf_mutex);
		return 0;
	}

	if (ringparam->rx_pending < BNAD_MIN_Q_DEPTH ||
	    ringparam->rx_pending > BNAD_MAX_RXQ_DEPTH ||
	    !is_power_of_2(ringparam->rx_pending)) {
		mutex_unlock(&bnad->conf_mutex);
		return -EINVAL;
	}
	if (ringparam->tx_pending < BNAD_MIN_Q_DEPTH ||
	    ringparam->tx_pending > BNAD_MAX_TXQ_DEPTH ||
	    !is_power_of_2(ringparam->tx_pending)) {
		mutex_unlock(&bnad->conf_mutex);
		return -EINVAL;
	}

	if (ringparam->rx_pending != bnad->rxq_depth) {
		bnad->rxq_depth = ringparam->rx_pending;
		if (!netif_running(netdev)) {
			mutex_unlock(&bnad->conf_mutex);
			return 0;
		}

		for (i = 0; i < bnad->num_rx; i++) {
			if (!bnad->rx_info[i].rx)
				continue;
			bnad_destroy_rx(bnad, i);
			current_err = bnad_setup_rx(bnad, i);
			if (current_err && !err)
				err = current_err;
		}

		if (!err && bnad->rx_info[0].rx) {
			/* restore rx configuration */
			bnad_restore_vlans(bnad, 0);
			bnad_enable_default_bcast(bnad);
			spin_lock_irqsave(&bnad->bna_lock, flags);
			bnad_mac_addr_set_locked(bnad, netdev->dev_addr);
			spin_unlock_irqrestore(&bnad->bna_lock, flags);
			bnad->cfg_flags &= ~(BNAD_CF_ALLMULTI |
					     BNAD_CF_PROMISC);
			bnad_set_rx_mode(netdev);
		}
	}
	if (ringparam->tx_pending != bnad->txq_depth) {
		bnad->txq_depth = ringparam->tx_pending;
		if (!netif_running(netdev)) {
			mutex_unlock(&bnad->conf_mutex);
			return 0;
		}

		for (i = 0; i < bnad->num_tx; i++) {
			if (!bnad->tx_info[i].tx)
				continue;
			bnad_destroy_tx(bnad, i);
			current_err = bnad_setup_tx(bnad, i);
			if (current_err && !err)
				err = current_err;
		}
	}

	mutex_unlock(&bnad->conf_mutex);
	return err;
}

static void
bnad_get_pauseparam(struct net_device *netdev,
		    struct ethtool_pauseparam *pauseparam)
{
	struct bnad *bnad = netdev_priv(netdev);

	pauseparam->autoneg = 0;
	pauseparam->rx_pause = bnad->bna.enet.pause_config.rx_pause;
	pauseparam->tx_pause = bnad->bna.enet.pause_config.tx_pause;
}

static int
bnad_set_pauseparam(struct net_device *netdev,
		    struct ethtool_pauseparam *pauseparam)
{
	struct bnad *bnad = netdev_priv(netdev);
	struct bna_pause_config pause_config;
	unsigned long flags;

	if (pauseparam->autoneg == AUTONEG_ENABLE)
		return -EINVAL;

	mutex_lock(&bnad->conf_mutex);
	if (pauseparam->rx_pause != bnad->bna.enet.pause_config.rx_pause ||
	    pauseparam->tx_pause != bnad->bna.enet.pause_config.tx_pause) {
		pause_config.rx_pause = pauseparam->rx_pause;
		pause_config.tx_pause = pauseparam->tx_pause;
		spin_lock_irqsave(&bnad->bna_lock, flags);
		bna_enet_pause_config(&bnad->bna.enet, &pause_config);
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
	}
	mutex_unlock(&bnad->conf_mutex);
	return 0;
}

static void bnad_get_txf_strings(u8 **string, int f_num)
{
	ethtool_sprintf(string, "txf%d_ucast_octets", f_num);
	ethtool_sprintf(string, "txf%d_ucast", f_num);
	ethtool_sprintf(string, "txf%d_ucast_vlan", f_num);
	ethtool_sprintf(string, "txf%d_mcast_octets", f_num);
	ethtool_sprintf(string, "txf%d_mcast", f_num);
	ethtool_sprintf(string, "txf%d_mcast_vlan", f_num);
	ethtool_sprintf(string, "txf%d_bcast_octets", f_num);
	ethtool_sprintf(string, "txf%d_bcast", f_num);
	ethtool_sprintf(string, "txf%d_bcast_vlan", f_num);
	ethtool_sprintf(string, "txf%d_errors", f_num);
	ethtool_sprintf(string, "txf%d_filter_vlan", f_num);
	ethtool_sprintf(string, "txf%d_filter_mac_sa", f_num);
}

static void bnad_get_rxf_strings(u8 **string, int f_num)
{
	ethtool_sprintf(string, "rxf%d_ucast_octets", f_num);
	ethtool_sprintf(string, "rxf%d_ucast", f_num);
	ethtool_sprintf(string, "rxf%d_ucast_vlan", f_num);
	ethtool_sprintf(string, "rxf%d_mcast_octets", f_num);
	ethtool_sprintf(string, "rxf%d_mcast", f_num);
	ethtool_sprintf(string, "rxf%d_mcast_vlan", f_num);
	ethtool_sprintf(string, "rxf%d_bcast_octets", f_num);
	ethtool_sprintf(string, "rxf%d_bcast", f_num);
	ethtool_sprintf(string, "rxf%d_bcast_vlan", f_num);
	ethtool_sprintf(string, "rxf%d_frame_drops", f_num);
}

static void bnad_get_cq_strings(u8 **string, int q_num)
{
	ethtool_sprintf(string, "cq%d_producer_index", q_num);
	ethtool_sprintf(string, "cq%d_consumer_index", q_num);
	ethtool_sprintf(string, "cq%d_hw_producer_index", q_num);
	ethtool_sprintf(string, "cq%d_intr", q_num);
	ethtool_sprintf(string, "cq%d_poll", q_num);
	ethtool_sprintf(string, "cq%d_schedule", q_num);
	ethtool_sprintf(string, "cq%d_keep_poll", q_num);
	ethtool_sprintf(string, "cq%d_complete", q_num);
}

static void bnad_get_rxq_strings(u8 **string, int q_num)
{
	ethtool_sprintf(string, "rxq%d_packets", q_num);
	ethtool_sprintf(string, "rxq%d_bytes", q_num);
	ethtool_sprintf(string, "rxq%d_packets_with_error", q_num);
	ethtool_sprintf(string, "rxq%d_allocbuf_failed", q_num);
	ethtool_sprintf(string, "rxq%d_mapbuf_failed", q_num);
	ethtool_sprintf(string, "rxq%d_producer_index", q_num);
	ethtool_sprintf(string, "rxq%d_consumer_index", q_num);
}

static void bnad_get_txq_strings(u8 **string, int q_num)
{
	ethtool_sprintf(string, "txq%d_packets", q_num);
	ethtool_sprintf(string, "txq%d_bytes", q_num);
	ethtool_sprintf(string, "txq%d_producer_index", q_num);
	ethtool_sprintf(string, "txq%d_consumer_index", q_num);
	ethtool_sprintf(string, "txq%d_hw_consumer_index", q_num);
}

static void
bnad_get_strings(struct net_device *netdev, u32 stringset, u8 *string)
{
	struct bnad *bnad = netdev_priv(netdev);
	int i, j, q_num;
	u32 bmap;

	if (stringset != ETH_SS_STATS)
		return;

	mutex_lock(&bnad->conf_mutex);

	for (i = 0; i < BNAD_ETHTOOL_STATS_NUM; i++) {
		BUG_ON(!(strlen(bnad_net_stats_strings[i]) < ETH_GSTRING_LEN));
		ethtool_puts(&string, bnad_net_stats_strings[i]);
	}

	bmap = bna_tx_rid_mask(&bnad->bna);
	for (i = 0; bmap; i++) {
		if (bmap & 1)
			bnad_get_txf_strings(&string, i);
		bmap >>= 1;
	}

	bmap = bna_rx_rid_mask(&bnad->bna);
	for (i = 0; bmap; i++, bmap >>= 1) {
		if (bmap & 1)
			bnad_get_rxf_strings(&string, i);
		bmap >>= 1;
	}

	q_num = 0;
	for (i = 0; i < bnad->num_rx; i++) {
		if (!bnad->rx_info[i].rx)
			continue;
		for (j = 0; j < bnad->num_rxp_per_rx; j++)
			bnad_get_cq_strings(&string, q_num++);
	}

	q_num = 0;
	for (i = 0; i < bnad->num_rx; i++) {
		if (!bnad->rx_info[i].rx)
			continue;
		for (j = 0; j < bnad->num_rxp_per_rx; j++) {
			bnad_get_rxq_strings(&string, q_num++);
			if (bnad->rx_info[i].rx_ctrl[j].ccb &&
			    bnad->rx_info[i].rx_ctrl[j].ccb->rcb[1] &&
			    bnad->rx_info[i].rx_ctrl[j].ccb->rcb[1]->rxq)
				bnad_get_rxq_strings(&string, q_num++);
		}
	}

	q_num = 0;
	for (i = 0; i < bnad->num_tx; i++) {
		if (!bnad->tx_info[i].tx)
			continue;
		for (j = 0; j < bnad->num_txq_per_tx; j++)
			bnad_get_txq_strings(&string, q_num++);
	}

	mutex_unlock(&bnad->conf_mutex);
}

static int
bnad_get_stats_count_locked(struct net_device *netdev)
{
	struct bnad *bnad = netdev_priv(netdev);
	int i, j, count = 0, rxf_active_num = 0, txf_active_num = 0;
	u32 bmap;

	bmap = bna_tx_rid_mask(&bnad->bna);
	for (i = 0; bmap; i++) {
		if (bmap & 1)
			txf_active_num++;
		bmap >>= 1;
	}
	bmap = bna_rx_rid_mask(&bnad->bna);
	for (i = 0; bmap; i++) {
		if (bmap & 1)
			rxf_active_num++;
		bmap >>= 1;
	}
	count = BNAD_ETHTOOL_STATS_NUM +
		txf_active_num * BNAD_NUM_TXF_COUNTERS +
		rxf_active_num * BNAD_NUM_RXF_COUNTERS;

	for (i = 0; i < bnad->num_rx; i++) {
		if (!bnad->rx_info[i].rx)
			continue;
		count += bnad->num_rxp_per_rx * BNAD_NUM_CQ_COUNTERS;
		count += bnad->num_rxp_per_rx * BNAD_NUM_RXQ_COUNTERS;
		for (j = 0; j < bnad->num_rxp_per_rx; j++)
			if (bnad->rx_info[i].rx_ctrl[j].ccb &&
				bnad->rx_info[i].rx_ctrl[j].ccb->rcb[1] &&
				bnad->rx_info[i].rx_ctrl[j].ccb->rcb[1]->rxq)
				count +=  BNAD_NUM_RXQ_COUNTERS;
	}

	for (i = 0; i < bnad->num_tx; i++) {
		if (!bnad->tx_info[i].tx)
			continue;
		count += bnad->num_txq_per_tx * BNAD_NUM_TXQ_COUNTERS;
	}
	return count;
}

static int
bnad_per_q_stats_fill(struct bnad *bnad, u64 *buf, int bi)
{
	int i, j;
	struct bna_rcb *rcb = NULL;
	struct bna_tcb *tcb = NULL;

	for (i = 0; i < bnad->num_rx; i++) {
		if (!bnad->rx_info[i].rx)
			continue;
		for (j = 0; j < bnad->num_rxp_per_rx; j++)
			if (bnad->rx_info[i].rx_ctrl[j].ccb &&
				bnad->rx_info[i].rx_ctrl[j].ccb->rcb[0] &&
				bnad->rx_info[i].rx_ctrl[j].ccb->rcb[0]->rxq) {
				buf[bi++] = bnad->rx_info[i].rx_ctrl[j].
						ccb->producer_index;
				buf[bi++] = 0; /* ccb->consumer_index */
				buf[bi++] = *(bnad->rx_info[i].rx_ctrl[j].
						ccb->hw_producer_index);

				buf[bi++] = bnad->rx_info[i].
						rx_ctrl[j].rx_intr_ctr;
				buf[bi++] = bnad->rx_info[i].
						rx_ctrl[j].rx_poll_ctr;
				buf[bi++] = bnad->rx_info[i].
						rx_ctrl[j].rx_schedule;
				buf[bi++] = bnad->rx_info[i].
						rx_ctrl[j].rx_keep_poll;
				buf[bi++] = bnad->rx_info[i].
						rx_ctrl[j].rx_complete;
			}
	}
	for (i = 0; i < bnad->num_rx; i++) {
		if (!bnad->rx_info[i].rx)
			continue;
		for (j = 0; j < bnad->num_rxp_per_rx; j++)
			if (bnad->rx_info[i].rx_ctrl[j].ccb) {
				if (bnad->rx_info[i].rx_ctrl[j].ccb->rcb[0] &&
					bnad->rx_info[i].rx_ctrl[j].ccb->
					rcb[0]->rxq) {
					rcb = bnad->rx_info[i].rx_ctrl[j].
							ccb->rcb[0];
					buf[bi++] = rcb->rxq->rx_packets;
					buf[bi++] = rcb->rxq->rx_bytes;
					buf[bi++] = rcb->rxq->
							rx_packets_with_error;
					buf[bi++] = rcb->rxq->
							rxbuf_alloc_failed;
					buf[bi++] = rcb->rxq->rxbuf_map_failed;
					buf[bi++] = rcb->producer_index;
					buf[bi++] = rcb->consumer_index;
				}
				if (bnad->rx_info[i].rx_ctrl[j].ccb->rcb[1] &&
					bnad->rx_info[i].rx_ctrl[j].ccb->
					rcb[1]->rxq) {
					rcb = bnad->rx_info[i].rx_ctrl[j].
								ccb->rcb[1];
					buf[bi++] = rcb->rxq->rx_packets;
					buf[bi++] = rcb->rxq->rx_bytes;
					buf[bi++] = rcb->rxq->
							rx_packets_with_error;
					buf[bi++] = rcb->rxq->
							rxbuf_alloc_failed;
					buf[bi++] = rcb->rxq->rxbuf_map_failed;
					buf[bi++] = rcb->producer_index;
					buf[bi++] = rcb->consumer_index;
				}
			}
	}

	for (i = 0; i < bnad->num_tx; i++) {
		if (!bnad->tx_info[i].tx)
			continue;
		for (j = 0; j < bnad->num_txq_per_tx; j++)
			if (bnad->tx_info[i].tcb[j] &&
				bnad->tx_info[i].tcb[j]->txq) {
				tcb = bnad->tx_info[i].tcb[j];
				buf[bi++] = tcb->txq->tx_packets;
				buf[bi++] = tcb->txq->tx_bytes;
				buf[bi++] = tcb->producer_index;
				buf[bi++] = tcb->consumer_index;
				buf[bi++] = *(tcb->hw_consumer_index);
			}
	}

	return bi;
}

static void
bnad_get_ethtool_stats(struct net_device *netdev, struct ethtool_stats *stats,
		       u64 *buf)
{
	struct bnad *bnad = netdev_priv(netdev);
	int i, j, bi = 0;
	unsigned long flags;
	struct rtnl_link_stats64 net_stats64;
	u64 *stats64;
	u32 bmap;

	mutex_lock(&bnad->conf_mutex);
	if (bnad_get_stats_count_locked(netdev) != stats->n_stats) {
		mutex_unlock(&bnad->conf_mutex);
		return;
	}

	/*
	 * Used bna_lock to sync reads from bna_stats, which is written
	 * under the same lock
	 */
	spin_lock_irqsave(&bnad->bna_lock, flags);

	memset(&net_stats64, 0, sizeof(net_stats64));
	bnad_netdev_qstats_fill(bnad, &net_stats64);
	bnad_netdev_hwstats_fill(bnad, &net_stats64);

	buf[bi++] = net_stats64.rx_packets;
	buf[bi++] = net_stats64.tx_packets;
	buf[bi++] = net_stats64.rx_bytes;
	buf[bi++] = net_stats64.tx_bytes;
	buf[bi++] = net_stats64.rx_errors;
	buf[bi++] = net_stats64.tx_errors;
	buf[bi++] = net_stats64.rx_dropped;
	buf[bi++] = net_stats64.tx_dropped;
	buf[bi++] = net_stats64.multicast;
	buf[bi++] = net_stats64.collisions;
	buf[bi++] = net_stats64.rx_length_errors;
	buf[bi++] = net_stats64.rx_crc_errors;
	buf[bi++] = net_stats64.rx_frame_errors;
	buf[bi++] = net_stats64.tx_fifo_errors;

	/* Get netif_queue_stopped from stack */
	bnad->stats.drv_stats.netif_queue_stopped = netif_queue_stopped(netdev);

	/* Fill driver stats into ethtool buffers */
	stats64 = (u64 *)&bnad->stats.drv_stats;
	for (i = 0; i < sizeof(struct bnad_drv_stats) / sizeof(u64); i++)
		buf[bi++] = stats64[i];

	/* Fill hardware stats excluding the rxf/txf into ethtool bufs */
	stats64 = (u64 *) &bnad->stats.bna_stats->hw_stats;
	for (i = 0;
	     i < offsetof(struct bfi_enet_stats, rxf_stats[0]) /
		sizeof(u64);
	     i++)
		buf[bi++] = stats64[i];

	/* Fill txf stats into ethtool buffers */
	bmap = bna_tx_rid_mask(&bnad->bna);
	for (i = 0; bmap; i++) {
		if (bmap & 1) {
			stats64 = (u64 *)&bnad->stats.bna_stats->
						hw_stats.txf_stats[i];
			for (j = 0; j < sizeof(struct bfi_enet_stats_txf) /
					sizeof(u64); j++)
				buf[bi++] = stats64[j];
		}
		bmap >>= 1;
	}

	/*  Fill rxf stats into ethtool buffers */
	bmap = bna_rx_rid_mask(&bnad->bna);
	for (i = 0; bmap; i++) {
		if (bmap & 1) {
			stats64 = (u64 *)&bnad->stats.bna_stats->
						hw_stats.rxf_stats[i];
			for (j = 0; j < sizeof(struct bfi_enet_stats_rxf) /
					sizeof(u64); j++)
				buf[bi++] = stats64[j];
		}
		bmap >>= 1;
	}

	/* Fill per Q stats into ethtool buffers */
	bi = bnad_per_q_stats_fill(bnad, buf, bi);

	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	mutex_unlock(&bnad->conf_mutex);
}

static int
bnad_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return bnad_get_stats_count_locked(netdev);
	default:
		return -EOPNOTSUPP;
	}
}

static u32
bnad_get_flash_partition_by_offset(struct bnad *bnad, u32 offset,
				u32 *base_offset)
{
	struct bfa_flash_attr *flash_attr;
	struct bnad_iocmd_comp fcomp;
	u32 i, flash_part = 0, ret;
	unsigned long flags = 0;

	flash_attr = kzalloc(sizeof(struct bfa_flash_attr), GFP_KERNEL);
	if (!flash_attr)
		return 0;

	fcomp.bnad = bnad;
	fcomp.comp_status = 0;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bnad->bna_lock, flags);
	ret = bfa_nw_flash_get_attr(&bnad->bna.flash, flash_attr,
				bnad_cb_completion, &fcomp);
	if (ret != BFA_STATUS_OK) {
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
		kfree(flash_attr);
		return 0;
	}
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
	wait_for_completion(&fcomp.comp);
	ret = fcomp.comp_status;

	/* Check for the flash type & base offset value */
	if (ret == BFA_STATUS_OK) {
		for (i = 0; i < flash_attr->npart; i++) {
			if (offset >= flash_attr->part[i].part_off &&
			    offset < (flash_attr->part[i].part_off +
				      flash_attr->part[i].part_size)) {
				flash_part = flash_attr->part[i].part_type;
				*base_offset = flash_attr->part[i].part_off;
				break;
			}
		}
	}
	kfree(flash_attr);
	return flash_part;
}

static int
bnad_get_eeprom_len(struct net_device *netdev)
{
	return BFA_TOTAL_FLASH_SIZE;
}

static int
bnad_get_eeprom(struct net_device *netdev, struct ethtool_eeprom *eeprom,
		u8 *bytes)
{
	struct bnad *bnad = netdev_priv(netdev);
	struct bnad_iocmd_comp fcomp;
	u32 flash_part = 0, base_offset = 0;
	unsigned long flags = 0;
	int ret = 0;

	/* Fill the magic value */
	eeprom->magic = bnad->pcidev->vendor | (bnad->pcidev->device << 16);

	/* Query the flash partition based on the offset */
	flash_part = bnad_get_flash_partition_by_offset(bnad,
				eeprom->offset, &base_offset);
	if (flash_part == 0)
		return -EFAULT;

	fcomp.bnad = bnad;
	fcomp.comp_status = 0;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bnad->bna_lock, flags);
	ret = bfa_nw_flash_read_part(&bnad->bna.flash, flash_part,
				bnad->id, bytes, eeprom->len,
				eeprom->offset - base_offset,
				bnad_cb_completion, &fcomp);
	if (ret != BFA_STATUS_OK) {
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
		goto done;
	}

	spin_unlock_irqrestore(&bnad->bna_lock, flags);
	wait_for_completion(&fcomp.comp);
	ret = fcomp.comp_status;
done:
	return ret;
}

static int
bnad_set_eeprom(struct net_device *netdev, struct ethtool_eeprom *eeprom,
		u8 *bytes)
{
	struct bnad *bnad = netdev_priv(netdev);
	struct bnad_iocmd_comp fcomp;
	u32 flash_part = 0, base_offset = 0;
	unsigned long flags = 0;
	int ret = 0;

	/* Check if the flash update request is valid */
	if (eeprom->magic != (bnad->pcidev->vendor |
			     (bnad->pcidev->device << 16)))
		return -EINVAL;

	/* Query the flash partition based on the offset */
	flash_part = bnad_get_flash_partition_by_offset(bnad,
				eeprom->offset, &base_offset);
	if (flash_part == 0)
		return -EFAULT;

	fcomp.bnad = bnad;
	fcomp.comp_status = 0;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bnad->bna_lock, flags);
	ret = bfa_nw_flash_update_part(&bnad->bna.flash, flash_part,
				bnad->id, bytes, eeprom->len,
				eeprom->offset - base_offset,
				bnad_cb_completion, &fcomp);
	if (ret != BFA_STATUS_OK) {
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
		goto done;
	}

	spin_unlock_irqrestore(&bnad->bna_lock, flags);
	wait_for_completion(&fcomp.comp);
	ret = fcomp.comp_status;
done:
	return ret;
}

static int
bnad_flash_device(struct net_device *netdev, struct ethtool_flash *eflash)
{
	struct bnad *bnad = netdev_priv(netdev);
	struct bnad_iocmd_comp fcomp;
	const struct firmware *fw;
	int ret = 0;

	ret = request_firmware(&fw, eflash->data, &bnad->pcidev->dev);
	if (ret) {
		netdev_err(netdev, "can't load firmware %s\n", eflash->data);
		goto out;
	}

	fcomp.bnad = bnad;
	fcomp.comp_status = 0;

	init_completion(&fcomp.comp);
	spin_lock_irq(&bnad->bna_lock);
	ret = bfa_nw_flash_update_part(&bnad->bna.flash, BFA_FLASH_PART_FWIMG,
				bnad->id, (u8 *)fw->data, fw->size, 0,
				bnad_cb_completion, &fcomp);
	if (ret != BFA_STATUS_OK) {
		netdev_warn(netdev, "flash update failed with err=%d\n", ret);
		ret = -EIO;
		spin_unlock_irq(&bnad->bna_lock);
		goto out;
	}

	spin_unlock_irq(&bnad->bna_lock);
	wait_for_completion(&fcomp.comp);
	if (fcomp.comp_status != BFA_STATUS_OK) {
		ret = -EIO;
		netdev_warn(netdev,
			    "firmware image update failed with err=%d\n",
			    fcomp.comp_status);
	}
out:
	release_firmware(fw);
	return ret;
}

static const struct ethtool_ops bnad_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_TX_MAX_FRAMES |
				     ETHTOOL_COALESCE_USE_ADAPTIVE_RX,
	.get_drvinfo = bnad_get_drvinfo,
	.get_wol = bnad_get_wol,
	.get_link = ethtool_op_get_link,
	.get_coalesce = bnad_get_coalesce,
	.set_coalesce = bnad_set_coalesce,
	.get_ringparam = bnad_get_ringparam,
	.set_ringparam = bnad_set_ringparam,
	.get_pauseparam = bnad_get_pauseparam,
	.set_pauseparam = bnad_set_pauseparam,
	.get_strings = bnad_get_strings,
	.get_ethtool_stats = bnad_get_ethtool_stats,
	.get_sset_count = bnad_get_sset_count,
	.get_eeprom_len = bnad_get_eeprom_len,
	.get_eeprom = bnad_get_eeprom,
	.set_eeprom = bnad_set_eeprom,
	.flash_device = bnad_flash_device,
	.get_ts_info = ethtool_op_get_ts_info,
	.get_link_ksettings = bnad_get_link_ksettings,
	.set_link_ksettings = bnad_set_link_ksettings,
};

void
bnad_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &bnad_ethtool_ops;
}
