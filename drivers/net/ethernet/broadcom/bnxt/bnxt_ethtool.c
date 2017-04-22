/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 * Copyright (c) 2016-2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/ctype.h>
#include <linux/stringify.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/crc32.h>
#include <linux/firmware.h>
#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_xdp.h"
#include "bnxt_ethtool.h"
#include "bnxt_nvm_defs.h"	/* NVRAM content constant and structure defs */
#include "bnxt_fw_hdr.h"	/* Firmware hdr constant and structure defs */
#define FLASH_NVRAM_TIMEOUT	((HWRM_CMD_TIMEOUT) * 100)
#define FLASH_PACKAGE_TIMEOUT	((HWRM_CMD_TIMEOUT) * 200)
#define INSTALL_PACKAGE_TIMEOUT	((HWRM_CMD_TIMEOUT) * 200)

static char *bnxt_get_pkgver(struct net_device *dev, char *buf, size_t buflen);

static u32 bnxt_get_msglevel(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);

	return bp->msg_enable;
}

static void bnxt_set_msglevel(struct net_device *dev, u32 value)
{
	struct bnxt *bp = netdev_priv(dev);

	bp->msg_enable = value;
}

static int bnxt_get_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *coal)
{
	struct bnxt *bp = netdev_priv(dev);

	memset(coal, 0, sizeof(*coal));

	coal->rx_coalesce_usecs = bp->rx_coal_ticks;
	/* 2 completion records per rx packet */
	coal->rx_max_coalesced_frames = bp->rx_coal_bufs / 2;
	coal->rx_coalesce_usecs_irq = bp->rx_coal_ticks_irq;
	coal->rx_max_coalesced_frames_irq = bp->rx_coal_bufs_irq / 2;

	coal->tx_coalesce_usecs = bp->tx_coal_ticks;
	coal->tx_max_coalesced_frames = bp->tx_coal_bufs;
	coal->tx_coalesce_usecs_irq = bp->tx_coal_ticks_irq;
	coal->tx_max_coalesced_frames_irq = bp->tx_coal_bufs_irq;

	coal->stats_block_coalesce_usecs = bp->stats_coal_ticks;

	return 0;
}

static int bnxt_set_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *coal)
{
	struct bnxt *bp = netdev_priv(dev);
	bool update_stats = false;
	int rc = 0;

	bp->rx_coal_ticks = coal->rx_coalesce_usecs;
	/* 2 completion records per rx packet */
	bp->rx_coal_bufs = coal->rx_max_coalesced_frames * 2;
	bp->rx_coal_ticks_irq = coal->rx_coalesce_usecs_irq;
	bp->rx_coal_bufs_irq = coal->rx_max_coalesced_frames_irq * 2;

	bp->tx_coal_ticks = coal->tx_coalesce_usecs;
	bp->tx_coal_bufs = coal->tx_max_coalesced_frames;
	bp->tx_coal_ticks_irq = coal->tx_coalesce_usecs_irq;
	bp->tx_coal_bufs_irq = coal->tx_max_coalesced_frames_irq;

	if (bp->stats_coal_ticks != coal->stats_block_coalesce_usecs) {
		u32 stats_ticks = coal->stats_block_coalesce_usecs;

		stats_ticks = clamp_t(u32, stats_ticks,
				      BNXT_MIN_STATS_COAL_TICKS,
				      BNXT_MAX_STATS_COAL_TICKS);
		stats_ticks = rounddown(stats_ticks, BNXT_MIN_STATS_COAL_TICKS);
		bp->stats_coal_ticks = stats_ticks;
		update_stats = true;
	}

	if (netif_running(dev)) {
		if (update_stats) {
			rc = bnxt_close_nic(bp, true, false);
			if (!rc)
				rc = bnxt_open_nic(bp, true, false);
		} else {
			rc = bnxt_hwrm_set_coal(bp);
		}
	}

	return rc;
}

#define BNXT_NUM_STATS	21

#define BNXT_RX_STATS_ENTRY(counter)	\
	{ BNXT_RX_STATS_OFFSET(counter), __stringify(counter) }

#define BNXT_TX_STATS_ENTRY(counter)	\
	{ BNXT_TX_STATS_OFFSET(counter), __stringify(counter) }

static const struct {
	long offset;
	char string[ETH_GSTRING_LEN];
} bnxt_port_stats_arr[] = {
	BNXT_RX_STATS_ENTRY(rx_64b_frames),
	BNXT_RX_STATS_ENTRY(rx_65b_127b_frames),
	BNXT_RX_STATS_ENTRY(rx_128b_255b_frames),
	BNXT_RX_STATS_ENTRY(rx_256b_511b_frames),
	BNXT_RX_STATS_ENTRY(rx_512b_1023b_frames),
	BNXT_RX_STATS_ENTRY(rx_1024b_1518_frames),
	BNXT_RX_STATS_ENTRY(rx_good_vlan_frames),
	BNXT_RX_STATS_ENTRY(rx_1519b_2047b_frames),
	BNXT_RX_STATS_ENTRY(rx_2048b_4095b_frames),
	BNXT_RX_STATS_ENTRY(rx_4096b_9216b_frames),
	BNXT_RX_STATS_ENTRY(rx_9217b_16383b_frames),
	BNXT_RX_STATS_ENTRY(rx_total_frames),
	BNXT_RX_STATS_ENTRY(rx_ucast_frames),
	BNXT_RX_STATS_ENTRY(rx_mcast_frames),
	BNXT_RX_STATS_ENTRY(rx_bcast_frames),
	BNXT_RX_STATS_ENTRY(rx_fcs_err_frames),
	BNXT_RX_STATS_ENTRY(rx_ctrl_frames),
	BNXT_RX_STATS_ENTRY(rx_pause_frames),
	BNXT_RX_STATS_ENTRY(rx_pfc_frames),
	BNXT_RX_STATS_ENTRY(rx_align_err_frames),
	BNXT_RX_STATS_ENTRY(rx_ovrsz_frames),
	BNXT_RX_STATS_ENTRY(rx_jbr_frames),
	BNXT_RX_STATS_ENTRY(rx_mtu_err_frames),
	BNXT_RX_STATS_ENTRY(rx_tagged_frames),
	BNXT_RX_STATS_ENTRY(rx_double_tagged_frames),
	BNXT_RX_STATS_ENTRY(rx_good_frames),
	BNXT_RX_STATS_ENTRY(rx_pfc_ena_frames_pri0),
	BNXT_RX_STATS_ENTRY(rx_pfc_ena_frames_pri1),
	BNXT_RX_STATS_ENTRY(rx_pfc_ena_frames_pri2),
	BNXT_RX_STATS_ENTRY(rx_pfc_ena_frames_pri3),
	BNXT_RX_STATS_ENTRY(rx_pfc_ena_frames_pri4),
	BNXT_RX_STATS_ENTRY(rx_pfc_ena_frames_pri5),
	BNXT_RX_STATS_ENTRY(rx_pfc_ena_frames_pri6),
	BNXT_RX_STATS_ENTRY(rx_pfc_ena_frames_pri7),
	BNXT_RX_STATS_ENTRY(rx_undrsz_frames),
	BNXT_RX_STATS_ENTRY(rx_eee_lpi_events),
	BNXT_RX_STATS_ENTRY(rx_eee_lpi_duration),
	BNXT_RX_STATS_ENTRY(rx_bytes),
	BNXT_RX_STATS_ENTRY(rx_runt_bytes),
	BNXT_RX_STATS_ENTRY(rx_runt_frames),

	BNXT_TX_STATS_ENTRY(tx_64b_frames),
	BNXT_TX_STATS_ENTRY(tx_65b_127b_frames),
	BNXT_TX_STATS_ENTRY(tx_128b_255b_frames),
	BNXT_TX_STATS_ENTRY(tx_256b_511b_frames),
	BNXT_TX_STATS_ENTRY(tx_512b_1023b_frames),
	BNXT_TX_STATS_ENTRY(tx_1024b_1518_frames),
	BNXT_TX_STATS_ENTRY(tx_good_vlan_frames),
	BNXT_TX_STATS_ENTRY(tx_1519b_2047_frames),
	BNXT_TX_STATS_ENTRY(tx_2048b_4095b_frames),
	BNXT_TX_STATS_ENTRY(tx_4096b_9216b_frames),
	BNXT_TX_STATS_ENTRY(tx_9217b_16383b_frames),
	BNXT_TX_STATS_ENTRY(tx_good_frames),
	BNXT_TX_STATS_ENTRY(tx_total_frames),
	BNXT_TX_STATS_ENTRY(tx_ucast_frames),
	BNXT_TX_STATS_ENTRY(tx_mcast_frames),
	BNXT_TX_STATS_ENTRY(tx_bcast_frames),
	BNXT_TX_STATS_ENTRY(tx_pause_frames),
	BNXT_TX_STATS_ENTRY(tx_pfc_frames),
	BNXT_TX_STATS_ENTRY(tx_jabber_frames),
	BNXT_TX_STATS_ENTRY(tx_fcs_err_frames),
	BNXT_TX_STATS_ENTRY(tx_err),
	BNXT_TX_STATS_ENTRY(tx_fifo_underruns),
	BNXT_TX_STATS_ENTRY(tx_pfc_ena_frames_pri0),
	BNXT_TX_STATS_ENTRY(tx_pfc_ena_frames_pri1),
	BNXT_TX_STATS_ENTRY(tx_pfc_ena_frames_pri2),
	BNXT_TX_STATS_ENTRY(tx_pfc_ena_frames_pri3),
	BNXT_TX_STATS_ENTRY(tx_pfc_ena_frames_pri4),
	BNXT_TX_STATS_ENTRY(tx_pfc_ena_frames_pri5),
	BNXT_TX_STATS_ENTRY(tx_pfc_ena_frames_pri6),
	BNXT_TX_STATS_ENTRY(tx_pfc_ena_frames_pri7),
	BNXT_TX_STATS_ENTRY(tx_eee_lpi_events),
	BNXT_TX_STATS_ENTRY(tx_eee_lpi_duration),
	BNXT_TX_STATS_ENTRY(tx_total_collisions),
	BNXT_TX_STATS_ENTRY(tx_bytes),
};

#define BNXT_NUM_PORT_STATS ARRAY_SIZE(bnxt_port_stats_arr)

static int bnxt_get_sset_count(struct net_device *dev, int sset)
{
	struct bnxt *bp = netdev_priv(dev);

	switch (sset) {
	case ETH_SS_STATS: {
		int num_stats = BNXT_NUM_STATS * bp->cp_nr_rings;

		if (bp->flags & BNXT_FLAG_PORT_STATS)
			num_stats += BNXT_NUM_PORT_STATS;

		return num_stats;
	}
	case ETH_SS_TEST:
		if (!bp->num_tests)
			return -EOPNOTSUPP;
		return bp->num_tests;
	default:
		return -EOPNOTSUPP;
	}
}

static void bnxt_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *stats, u64 *buf)
{
	u32 i, j = 0;
	struct bnxt *bp = netdev_priv(dev);
	u32 buf_size = sizeof(struct ctx_hw_stats) * bp->cp_nr_rings;
	u32 stat_fields = sizeof(struct ctx_hw_stats) / 8;

	memset(buf, 0, buf_size);

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
		__le64 *hw_stats = (__le64 *)cpr->hw_stats;
		int k;

		for (k = 0; k < stat_fields; j++, k++)
			buf[j] = le64_to_cpu(hw_stats[k]);
		buf[j++] = cpr->rx_l4_csum_errors;
	}
	if (bp->flags & BNXT_FLAG_PORT_STATS) {
		__le64 *port_stats = (__le64 *)bp->hw_rx_port_stats;

		for (i = 0; i < BNXT_NUM_PORT_STATS; i++, j++) {
			buf[j] = le64_to_cpu(*(port_stats +
					       bnxt_port_stats_arr[i].offset));
		}
	}
}

static void bnxt_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	struct bnxt *bp = netdev_priv(dev);
	u32 i;

	switch (stringset) {
	/* The number of strings must match BNXT_NUM_STATS defined above. */
	case ETH_SS_STATS:
		for (i = 0; i < bp->cp_nr_rings; i++) {
			sprintf(buf, "[%d]: rx_ucast_packets", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: rx_mcast_packets", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: rx_bcast_packets", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: rx_discards", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: rx_drops", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: rx_ucast_bytes", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: rx_mcast_bytes", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: rx_bcast_bytes", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: tx_ucast_packets", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: tx_mcast_packets", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: tx_bcast_packets", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: tx_discards", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: tx_drops", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: tx_ucast_bytes", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: tx_mcast_bytes", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: tx_bcast_bytes", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: tpa_packets", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: tpa_bytes", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: tpa_events", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: tpa_aborts", i);
			buf += ETH_GSTRING_LEN;
			sprintf(buf, "[%d]: rx_l4_csum_errors", i);
			buf += ETH_GSTRING_LEN;
		}
		if (bp->flags & BNXT_FLAG_PORT_STATS) {
			for (i = 0; i < BNXT_NUM_PORT_STATS; i++) {
				strcpy(buf, bnxt_port_stats_arr[i].string);
				buf += ETH_GSTRING_LEN;
			}
		}
		break;
	case ETH_SS_TEST:
		if (bp->num_tests)
			memcpy(buf, bp->test_info->string,
			       bp->num_tests * ETH_GSTRING_LEN);
		break;
	default:
		netdev_err(bp->dev, "bnxt_get_strings invalid request %x\n",
			   stringset);
		break;
	}
}

static void bnxt_get_ringparam(struct net_device *dev,
			       struct ethtool_ringparam *ering)
{
	struct bnxt *bp = netdev_priv(dev);

	ering->rx_max_pending = BNXT_MAX_RX_DESC_CNT;
	ering->rx_jumbo_max_pending = BNXT_MAX_RX_JUM_DESC_CNT;
	ering->tx_max_pending = BNXT_MAX_TX_DESC_CNT;

	ering->rx_pending = bp->rx_ring_size;
	ering->rx_jumbo_pending = bp->rx_agg_ring_size;
	ering->tx_pending = bp->tx_ring_size;
}

static int bnxt_set_ringparam(struct net_device *dev,
			      struct ethtool_ringparam *ering)
{
	struct bnxt *bp = netdev_priv(dev);

	if ((ering->rx_pending > BNXT_MAX_RX_DESC_CNT) ||
	    (ering->tx_pending > BNXT_MAX_TX_DESC_CNT) ||
	    (ering->tx_pending <= MAX_SKB_FRAGS))
		return -EINVAL;

	if (netif_running(dev))
		bnxt_close_nic(bp, false, false);

	bp->rx_ring_size = ering->rx_pending;
	bp->tx_ring_size = ering->tx_pending;
	bnxt_set_ring_params(bp);

	if (netif_running(dev))
		return bnxt_open_nic(bp, false, false);

	return 0;
}

static void bnxt_get_channels(struct net_device *dev,
			      struct ethtool_channels *channel)
{
	struct bnxt *bp = netdev_priv(dev);
	int max_rx_rings, max_tx_rings, tcs;

	bnxt_get_max_rings(bp, &max_rx_rings, &max_tx_rings, true);
	channel->max_combined = min_t(int, max_rx_rings, max_tx_rings);

	if (bnxt_get_max_rings(bp, &max_rx_rings, &max_tx_rings, false)) {
		max_rx_rings = 0;
		max_tx_rings = 0;
	}

	tcs = netdev_get_num_tc(dev);
	if (tcs > 1)
		max_tx_rings /= tcs;

	channel->max_rx = max_rx_rings;
	channel->max_tx = max_tx_rings;
	channel->max_other = 0;
	if (bp->flags & BNXT_FLAG_SHARED_RINGS) {
		channel->combined_count = bp->rx_nr_rings;
		if (BNXT_CHIP_TYPE_NITRO_A0(bp))
			channel->combined_count--;
	} else {
		if (!BNXT_CHIP_TYPE_NITRO_A0(bp)) {
			channel->rx_count = bp->rx_nr_rings;
			channel->tx_count = bp->tx_nr_rings_per_tc;
		}
	}
}

static int bnxt_set_channels(struct net_device *dev,
			     struct ethtool_channels *channel)
{
	struct bnxt *bp = netdev_priv(dev);
	int req_tx_rings, req_rx_rings, tcs;
	bool sh = false;
	int tx_xdp = 0;
	int rc = 0;

	if (channel->other_count)
		return -EINVAL;

	if (!channel->combined_count &&
	    (!channel->rx_count || !channel->tx_count))
		return -EINVAL;

	if (channel->combined_count &&
	    (channel->rx_count || channel->tx_count))
		return -EINVAL;

	if (BNXT_CHIP_TYPE_NITRO_A0(bp) && (channel->rx_count ||
					    channel->tx_count))
		return -EINVAL;

	if (channel->combined_count)
		sh = true;

	tcs = netdev_get_num_tc(dev);

	req_tx_rings = sh ? channel->combined_count : channel->tx_count;
	req_rx_rings = sh ? channel->combined_count : channel->rx_count;
	if (bp->tx_nr_rings_xdp) {
		if (!sh) {
			netdev_err(dev, "Only combined mode supported when XDP is enabled.\n");
			return -EINVAL;
		}
		tx_xdp = req_rx_rings;
	}
	rc = bnxt_reserve_rings(bp, req_tx_rings, req_rx_rings, tcs, tx_xdp);
	if (rc) {
		netdev_warn(dev, "Unable to allocate the requested rings\n");
		return rc;
	}

	if (netif_running(dev)) {
		if (BNXT_PF(bp)) {
			/* TODO CHIMP_FW: Send message to all VF's
			 * before PF unload
			 */
		}
		rc = bnxt_close_nic(bp, true, false);
		if (rc) {
			netdev_err(bp->dev, "Set channel failure rc :%x\n",
				   rc);
			return rc;
		}
	}

	if (sh) {
		bp->flags |= BNXT_FLAG_SHARED_RINGS;
		bp->rx_nr_rings = channel->combined_count;
		bp->tx_nr_rings_per_tc = channel->combined_count;
	} else {
		bp->flags &= ~BNXT_FLAG_SHARED_RINGS;
		bp->rx_nr_rings = channel->rx_count;
		bp->tx_nr_rings_per_tc = channel->tx_count;
	}
	bp->tx_nr_rings_xdp = tx_xdp;
	bp->tx_nr_rings = bp->tx_nr_rings_per_tc + tx_xdp;
	if (tcs > 1)
		bp->tx_nr_rings = bp->tx_nr_rings_per_tc * tcs + tx_xdp;

	bp->cp_nr_rings = sh ? max_t(int, bp->tx_nr_rings, bp->rx_nr_rings) :
			       bp->tx_nr_rings + bp->rx_nr_rings;

	bp->num_stat_ctxs = bp->cp_nr_rings;

	/* After changing number of rx channels, update NTUPLE feature. */
	netdev_update_features(dev);
	if (netif_running(dev)) {
		rc = bnxt_open_nic(bp, true, false);
		if ((!rc) && BNXT_PF(bp)) {
			/* TODO CHIMP_FW: Send message to all VF's
			 * to renable
			 */
		}
	}

	return rc;
}

#ifdef CONFIG_RFS_ACCEL
static int bnxt_grxclsrlall(struct bnxt *bp, struct ethtool_rxnfc *cmd,
			    u32 *rule_locs)
{
	int i, j = 0;

	cmd->data = bp->ntp_fltr_count;
	for (i = 0; i < BNXT_NTP_FLTR_HASH_SIZE; i++) {
		struct hlist_head *head;
		struct bnxt_ntuple_filter *fltr;

		head = &bp->ntp_fltr_hash_tbl[i];
		rcu_read_lock();
		hlist_for_each_entry_rcu(fltr, head, hash) {
			if (j == cmd->rule_cnt)
				break;
			rule_locs[j++] = fltr->sw_id;
		}
		rcu_read_unlock();
		if (j == cmd->rule_cnt)
			break;
	}
	cmd->rule_cnt = j;
	return 0;
}

static int bnxt_grxclsrule(struct bnxt *bp, struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fs =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct bnxt_ntuple_filter *fltr;
	struct flow_keys *fkeys;
	int i, rc = -EINVAL;

	if (fs->location < 0 || fs->location >= BNXT_NTP_FLTR_MAX_FLTR)
		return rc;

	for (i = 0; i < BNXT_NTP_FLTR_HASH_SIZE; i++) {
		struct hlist_head *head;

		head = &bp->ntp_fltr_hash_tbl[i];
		rcu_read_lock();
		hlist_for_each_entry_rcu(fltr, head, hash) {
			if (fltr->sw_id == fs->location)
				goto fltr_found;
		}
		rcu_read_unlock();
	}
	return rc;

fltr_found:
	fkeys = &fltr->fkeys;
	if (fkeys->basic.n_proto == htons(ETH_P_IP)) {
		if (fkeys->basic.ip_proto == IPPROTO_TCP)
			fs->flow_type = TCP_V4_FLOW;
		else if (fkeys->basic.ip_proto == IPPROTO_UDP)
			fs->flow_type = UDP_V4_FLOW;
		else
			goto fltr_err;

		fs->h_u.tcp_ip4_spec.ip4src = fkeys->addrs.v4addrs.src;
		fs->m_u.tcp_ip4_spec.ip4src = cpu_to_be32(~0);

		fs->h_u.tcp_ip4_spec.ip4dst = fkeys->addrs.v4addrs.dst;
		fs->m_u.tcp_ip4_spec.ip4dst = cpu_to_be32(~0);

		fs->h_u.tcp_ip4_spec.psrc = fkeys->ports.src;
		fs->m_u.tcp_ip4_spec.psrc = cpu_to_be16(~0);

		fs->h_u.tcp_ip4_spec.pdst = fkeys->ports.dst;
		fs->m_u.tcp_ip4_spec.pdst = cpu_to_be16(~0);
	} else {
		int i;

		if (fkeys->basic.ip_proto == IPPROTO_TCP)
			fs->flow_type = TCP_V6_FLOW;
		else if (fkeys->basic.ip_proto == IPPROTO_UDP)
			fs->flow_type = UDP_V6_FLOW;
		else
			goto fltr_err;

		*(struct in6_addr *)&fs->h_u.tcp_ip6_spec.ip6src[0] =
			fkeys->addrs.v6addrs.src;
		*(struct in6_addr *)&fs->h_u.tcp_ip6_spec.ip6dst[0] =
			fkeys->addrs.v6addrs.dst;
		for (i = 0; i < 4; i++) {
			fs->m_u.tcp_ip6_spec.ip6src[i] = cpu_to_be32(~0);
			fs->m_u.tcp_ip6_spec.ip6dst[i] = cpu_to_be32(~0);
		}
		fs->h_u.tcp_ip6_spec.psrc = fkeys->ports.src;
		fs->m_u.tcp_ip6_spec.psrc = cpu_to_be16(~0);

		fs->h_u.tcp_ip6_spec.pdst = fkeys->ports.dst;
		fs->m_u.tcp_ip6_spec.pdst = cpu_to_be16(~0);
	}

	fs->ring_cookie = fltr->rxq;
	rc = 0;

fltr_err:
	rcu_read_unlock();

	return rc;
}
#endif

static u64 get_ethtool_ipv4_rss(struct bnxt *bp)
{
	if (bp->rss_hash_cfg & VNIC_RSS_CFG_REQ_HASH_TYPE_IPV4)
		return RXH_IP_SRC | RXH_IP_DST;
	return 0;
}

static u64 get_ethtool_ipv6_rss(struct bnxt *bp)
{
	if (bp->rss_hash_cfg & VNIC_RSS_CFG_REQ_HASH_TYPE_IPV6)
		return RXH_IP_SRC | RXH_IP_DST;
	return 0;
}

static int bnxt_grxfh(struct bnxt *bp, struct ethtool_rxnfc *cmd)
{
	cmd->data = 0;
	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		if (bp->rss_hash_cfg & VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV4)
			cmd->data |= RXH_IP_SRC | RXH_IP_DST |
				     RXH_L4_B_0_1 | RXH_L4_B_2_3;
		cmd->data |= get_ethtool_ipv4_rss(bp);
		break;
	case UDP_V4_FLOW:
		if (bp->rss_hash_cfg & VNIC_RSS_CFG_REQ_HASH_TYPE_UDP_IPV4)
			cmd->data |= RXH_IP_SRC | RXH_IP_DST |
				     RXH_L4_B_0_1 | RXH_L4_B_2_3;
		/* fall through */
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case IPV4_FLOW:
		cmd->data |= get_ethtool_ipv4_rss(bp);
		break;

	case TCP_V6_FLOW:
		if (bp->rss_hash_cfg & VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV6)
			cmd->data |= RXH_IP_SRC | RXH_IP_DST |
				     RXH_L4_B_0_1 | RXH_L4_B_2_3;
		cmd->data |= get_ethtool_ipv6_rss(bp);
		break;
	case UDP_V6_FLOW:
		if (bp->rss_hash_cfg & VNIC_RSS_CFG_REQ_HASH_TYPE_UDP_IPV6)
			cmd->data |= RXH_IP_SRC | RXH_IP_DST |
				     RXH_L4_B_0_1 | RXH_L4_B_2_3;
		/* fall through */
	case SCTP_V6_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case IPV6_FLOW:
		cmd->data |= get_ethtool_ipv6_rss(bp);
		break;
	}
	return 0;
}

#define RXH_4TUPLE (RXH_IP_SRC | RXH_IP_DST | RXH_L4_B_0_1 | RXH_L4_B_2_3)
#define RXH_2TUPLE (RXH_IP_SRC | RXH_IP_DST)

static int bnxt_srxfh(struct bnxt *bp, struct ethtool_rxnfc *cmd)
{
	u32 rss_hash_cfg = bp->rss_hash_cfg;
	int tuple, rc = 0;

	if (cmd->data == RXH_4TUPLE)
		tuple = 4;
	else if (cmd->data == RXH_2TUPLE)
		tuple = 2;
	else if (!cmd->data)
		tuple = 0;
	else
		return -EINVAL;

	if (cmd->flow_type == TCP_V4_FLOW) {
		rss_hash_cfg &= ~VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV4;
		if (tuple == 4)
			rss_hash_cfg |= VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV4;
	} else if (cmd->flow_type == UDP_V4_FLOW) {
		if (tuple == 4 && !(bp->flags & BNXT_FLAG_UDP_RSS_CAP))
			return -EINVAL;
		rss_hash_cfg &= ~VNIC_RSS_CFG_REQ_HASH_TYPE_UDP_IPV4;
		if (tuple == 4)
			rss_hash_cfg |= VNIC_RSS_CFG_REQ_HASH_TYPE_UDP_IPV4;
	} else if (cmd->flow_type == TCP_V6_FLOW) {
		rss_hash_cfg &= ~VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV6;
		if (tuple == 4)
			rss_hash_cfg |= VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV6;
	} else if (cmd->flow_type == UDP_V6_FLOW) {
		if (tuple == 4 && !(bp->flags & BNXT_FLAG_UDP_RSS_CAP))
			return -EINVAL;
		rss_hash_cfg &= ~VNIC_RSS_CFG_REQ_HASH_TYPE_UDP_IPV6;
		if (tuple == 4)
			rss_hash_cfg |= VNIC_RSS_CFG_REQ_HASH_TYPE_UDP_IPV6;
	} else if (tuple == 4) {
		return -EINVAL;
	}

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case IPV4_FLOW:
		if (tuple == 2)
			rss_hash_cfg |= VNIC_RSS_CFG_REQ_HASH_TYPE_IPV4;
		else if (!tuple)
			rss_hash_cfg &= ~VNIC_RSS_CFG_REQ_HASH_TYPE_IPV4;
		break;

	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
	case SCTP_V6_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case IPV6_FLOW:
		if (tuple == 2)
			rss_hash_cfg |= VNIC_RSS_CFG_REQ_HASH_TYPE_IPV6;
		else if (!tuple)
			rss_hash_cfg &= ~VNIC_RSS_CFG_REQ_HASH_TYPE_IPV6;
		break;
	}

	if (bp->rss_hash_cfg == rss_hash_cfg)
		return 0;

	bp->rss_hash_cfg = rss_hash_cfg;
	if (netif_running(bp->dev)) {
		bnxt_close_nic(bp, false, false);
		rc = bnxt_open_nic(bp, false, false);
	}
	return rc;
}

static int bnxt_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd,
			  u32 *rule_locs)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc = 0;

	switch (cmd->cmd) {
#ifdef CONFIG_RFS_ACCEL
	case ETHTOOL_GRXRINGS:
		cmd->data = bp->rx_nr_rings;
		break;

	case ETHTOOL_GRXCLSRLCNT:
		cmd->rule_cnt = bp->ntp_fltr_count;
		cmd->data = BNXT_NTP_FLTR_MAX_FLTR;
		break;

	case ETHTOOL_GRXCLSRLALL:
		rc = bnxt_grxclsrlall(bp, cmd, (u32 *)rule_locs);
		break;

	case ETHTOOL_GRXCLSRULE:
		rc = bnxt_grxclsrule(bp, cmd);
		break;
#endif

	case ETHTOOL_GRXFH:
		rc = bnxt_grxfh(bp, cmd);
		break;

	default:
		rc = -EOPNOTSUPP;
		break;
	}

	return rc;
}

static int bnxt_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc;

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		rc = bnxt_srxfh(bp, cmd);
		break;

	default:
		rc = -EOPNOTSUPP;
		break;
	}
	return rc;
}

static u32 bnxt_get_rxfh_indir_size(struct net_device *dev)
{
	return HW_HASH_INDEX_SIZE;
}

static u32 bnxt_get_rxfh_key_size(struct net_device *dev)
{
	return HW_HASH_KEY_SIZE;
}

static int bnxt_get_rxfh(struct net_device *dev, u32 *indir, u8 *key,
			 u8 *hfunc)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_vnic_info *vnic = &bp->vnic_info[0];
	int i = 0;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	if (indir)
		for (i = 0; i < HW_HASH_INDEX_SIZE; i++)
			indir[i] = le16_to_cpu(vnic->rss_table[i]);

	if (key)
		memcpy(key, vnic->rss_hash_key, HW_HASH_KEY_SIZE);

	return 0;
}

static void bnxt_get_drvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	struct bnxt *bp = netdev_priv(dev);
	char *pkglog;
	char *pkgver = NULL;

	pkglog = kmalloc(BNX_PKG_LOG_MAX_LENGTH, GFP_KERNEL);
	if (pkglog)
		pkgver = bnxt_get_pkgver(dev, pkglog, BNX_PKG_LOG_MAX_LENGTH);
	strlcpy(info->driver, DRV_MODULE_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_MODULE_VERSION, sizeof(info->version));
	if (pkgver && *pkgver != 0 && isdigit(*pkgver))
		snprintf(info->fw_version, sizeof(info->fw_version) - 1,
			 "%s pkg %s", bp->fw_ver_str, pkgver);
	else
		strlcpy(info->fw_version, bp->fw_ver_str,
			sizeof(info->fw_version));
	strlcpy(info->bus_info, pci_name(bp->pdev), sizeof(info->bus_info));
	info->n_stats = BNXT_NUM_STATS * bp->cp_nr_rings;
	info->testinfo_len = bp->num_tests;
	/* TODO CHIMP_FW: eeprom dump details */
	info->eedump_len = 0;
	/* TODO CHIMP FW: reg dump details */
	info->regdump_len = 0;
	kfree(pkglog);
}

static void bnxt_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bnxt *bp = netdev_priv(dev);

	wol->supported = 0;
	wol->wolopts = 0;
	memset(&wol->sopass, 0, sizeof(wol->sopass));
	if (bp->flags & BNXT_FLAG_WOL_CAP) {
		wol->supported = WAKE_MAGIC;
		if (bp->wol)
			wol->wolopts = WAKE_MAGIC;
	}
}

static int bnxt_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bnxt *bp = netdev_priv(dev);

	if (wol->wolopts & ~WAKE_MAGIC)
		return -EINVAL;

	if (wol->wolopts & WAKE_MAGIC) {
		if (!(bp->flags & BNXT_FLAG_WOL_CAP))
			return -EINVAL;
		if (!bp->wol) {
			if (bnxt_hwrm_alloc_wol_fltr(bp))
				return -EBUSY;
			bp->wol = 1;
		}
	} else {
		if (bp->wol) {
			if (bnxt_hwrm_free_wol_fltr(bp))
				return -EBUSY;
			bp->wol = 0;
		}
	}
	return 0;
}

u32 _bnxt_fw_to_ethtool_adv_spds(u16 fw_speeds, u8 fw_pause)
{
	u32 speed_mask = 0;

	/* TODO: support 25GB, 40GB, 50GB with different cable type */
	/* set the advertised speeds */
	if (fw_speeds & BNXT_LINK_SPEED_MSK_100MB)
		speed_mask |= ADVERTISED_100baseT_Full;
	if (fw_speeds & BNXT_LINK_SPEED_MSK_1GB)
		speed_mask |= ADVERTISED_1000baseT_Full;
	if (fw_speeds & BNXT_LINK_SPEED_MSK_2_5GB)
		speed_mask |= ADVERTISED_2500baseX_Full;
	if (fw_speeds & BNXT_LINK_SPEED_MSK_10GB)
		speed_mask |= ADVERTISED_10000baseT_Full;
	if (fw_speeds & BNXT_LINK_SPEED_MSK_40GB)
		speed_mask |= ADVERTISED_40000baseCR4_Full;

	if ((fw_pause & BNXT_LINK_PAUSE_BOTH) == BNXT_LINK_PAUSE_BOTH)
		speed_mask |= ADVERTISED_Pause;
	else if (fw_pause & BNXT_LINK_PAUSE_TX)
		speed_mask |= ADVERTISED_Asym_Pause;
	else if (fw_pause & BNXT_LINK_PAUSE_RX)
		speed_mask |= ADVERTISED_Pause | ADVERTISED_Asym_Pause;

	return speed_mask;
}

#define BNXT_FW_TO_ETHTOOL_SPDS(fw_speeds, fw_pause, lk_ksettings, name)\
{									\
	if ((fw_speeds) & BNXT_LINK_SPEED_MSK_100MB)			\
		ethtool_link_ksettings_add_link_mode(lk_ksettings, name,\
						     100baseT_Full);	\
	if ((fw_speeds) & BNXT_LINK_SPEED_MSK_1GB)			\
		ethtool_link_ksettings_add_link_mode(lk_ksettings, name,\
						     1000baseT_Full);	\
	if ((fw_speeds) & BNXT_LINK_SPEED_MSK_10GB)			\
		ethtool_link_ksettings_add_link_mode(lk_ksettings, name,\
						     10000baseT_Full);	\
	if ((fw_speeds) & BNXT_LINK_SPEED_MSK_25GB)			\
		ethtool_link_ksettings_add_link_mode(lk_ksettings, name,\
						     25000baseCR_Full);	\
	if ((fw_speeds) & BNXT_LINK_SPEED_MSK_40GB)			\
		ethtool_link_ksettings_add_link_mode(lk_ksettings, name,\
						     40000baseCR4_Full);\
	if ((fw_speeds) & BNXT_LINK_SPEED_MSK_50GB)			\
		ethtool_link_ksettings_add_link_mode(lk_ksettings, name,\
						     50000baseCR2_Full);\
	if ((fw_speeds) & BNXT_LINK_SPEED_MSK_100GB)			\
		ethtool_link_ksettings_add_link_mode(lk_ksettings, name,\
						     100000baseCR4_Full);\
	if ((fw_pause) & BNXT_LINK_PAUSE_RX) {				\
		ethtool_link_ksettings_add_link_mode(lk_ksettings, name,\
						     Pause);		\
		if (!((fw_pause) & BNXT_LINK_PAUSE_TX))			\
			ethtool_link_ksettings_add_link_mode(		\
					lk_ksettings, name, Asym_Pause);\
	} else if ((fw_pause) & BNXT_LINK_PAUSE_TX) {			\
		ethtool_link_ksettings_add_link_mode(lk_ksettings, name,\
						     Asym_Pause);	\
	}								\
}

#define BNXT_ETHTOOL_TO_FW_SPDS(fw_speeds, lk_ksettings, name)		\
{									\
	if (ethtool_link_ksettings_test_link_mode(lk_ksettings, name,	\
						  100baseT_Full) ||	\
	    ethtool_link_ksettings_test_link_mode(lk_ksettings, name,	\
						  100baseT_Half))	\
		(fw_speeds) |= BNXT_LINK_SPEED_MSK_100MB;		\
	if (ethtool_link_ksettings_test_link_mode(lk_ksettings, name,	\
						  1000baseT_Full) ||	\
	    ethtool_link_ksettings_test_link_mode(lk_ksettings, name,	\
						  1000baseT_Half))	\
		(fw_speeds) |= BNXT_LINK_SPEED_MSK_1GB;			\
	if (ethtool_link_ksettings_test_link_mode(lk_ksettings, name,	\
						  10000baseT_Full))	\
		(fw_speeds) |= BNXT_LINK_SPEED_MSK_10GB;		\
	if (ethtool_link_ksettings_test_link_mode(lk_ksettings, name,	\
						  25000baseCR_Full))	\
		(fw_speeds) |= BNXT_LINK_SPEED_MSK_25GB;		\
	if (ethtool_link_ksettings_test_link_mode(lk_ksettings, name,	\
						  40000baseCR4_Full))	\
		(fw_speeds) |= BNXT_LINK_SPEED_MSK_40GB;		\
	if (ethtool_link_ksettings_test_link_mode(lk_ksettings, name,	\
						  50000baseCR2_Full))	\
		(fw_speeds) |= BNXT_LINK_SPEED_MSK_50GB;		\
	if (ethtool_link_ksettings_test_link_mode(lk_ksettings, name,	\
						  100000baseCR4_Full))	\
		(fw_speeds) |= BNXT_LINK_SPEED_MSK_100GB;		\
}

static void bnxt_fw_to_ethtool_advertised_spds(struct bnxt_link_info *link_info,
				struct ethtool_link_ksettings *lk_ksettings)
{
	u16 fw_speeds = link_info->advertising;
	u8 fw_pause = 0;

	if (link_info->autoneg & BNXT_AUTONEG_FLOW_CTRL)
		fw_pause = link_info->auto_pause_setting;

	BNXT_FW_TO_ETHTOOL_SPDS(fw_speeds, fw_pause, lk_ksettings, advertising);
}

static void bnxt_fw_to_ethtool_lp_adv(struct bnxt_link_info *link_info,
				struct ethtool_link_ksettings *lk_ksettings)
{
	u16 fw_speeds = link_info->lp_auto_link_speeds;
	u8 fw_pause = 0;

	if (link_info->autoneg & BNXT_AUTONEG_FLOW_CTRL)
		fw_pause = link_info->lp_pause;

	BNXT_FW_TO_ETHTOOL_SPDS(fw_speeds, fw_pause, lk_ksettings,
				lp_advertising);
}

static void bnxt_fw_to_ethtool_support_spds(struct bnxt_link_info *link_info,
				struct ethtool_link_ksettings *lk_ksettings)
{
	u16 fw_speeds = link_info->support_speeds;

	BNXT_FW_TO_ETHTOOL_SPDS(fw_speeds, 0, lk_ksettings, supported);

	ethtool_link_ksettings_add_link_mode(lk_ksettings, supported, Pause);
	ethtool_link_ksettings_add_link_mode(lk_ksettings, supported,
					     Asym_Pause);

	if (link_info->support_auto_speeds)
		ethtool_link_ksettings_add_link_mode(lk_ksettings, supported,
						     Autoneg);
}

u32 bnxt_fw_to_ethtool_speed(u16 fw_link_speed)
{
	switch (fw_link_speed) {
	case BNXT_LINK_SPEED_100MB:
		return SPEED_100;
	case BNXT_LINK_SPEED_1GB:
		return SPEED_1000;
	case BNXT_LINK_SPEED_2_5GB:
		return SPEED_2500;
	case BNXT_LINK_SPEED_10GB:
		return SPEED_10000;
	case BNXT_LINK_SPEED_20GB:
		return SPEED_20000;
	case BNXT_LINK_SPEED_25GB:
		return SPEED_25000;
	case BNXT_LINK_SPEED_40GB:
		return SPEED_40000;
	case BNXT_LINK_SPEED_50GB:
		return SPEED_50000;
	case BNXT_LINK_SPEED_100GB:
		return SPEED_100000;
	default:
		return SPEED_UNKNOWN;
	}
}

static int bnxt_get_link_ksettings(struct net_device *dev,
				   struct ethtool_link_ksettings *lk_ksettings)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_link_info *link_info = &bp->link_info;
	struct ethtool_link_settings *base = &lk_ksettings->base;
	u32 ethtool_speed;

	ethtool_link_ksettings_zero_link_mode(lk_ksettings, supported);
	bnxt_fw_to_ethtool_support_spds(link_info, lk_ksettings);

	ethtool_link_ksettings_zero_link_mode(lk_ksettings, advertising);
	if (link_info->autoneg) {
		bnxt_fw_to_ethtool_advertised_spds(link_info, lk_ksettings);
		ethtool_link_ksettings_add_link_mode(lk_ksettings,
						     advertising, Autoneg);
		base->autoneg = AUTONEG_ENABLE;
		if (link_info->phy_link_status == BNXT_LINK_LINK)
			bnxt_fw_to_ethtool_lp_adv(link_info, lk_ksettings);
		ethtool_speed = bnxt_fw_to_ethtool_speed(link_info->link_speed);
		if (!netif_carrier_ok(dev))
			base->duplex = DUPLEX_UNKNOWN;
		else if (link_info->duplex & BNXT_LINK_DUPLEX_FULL)
			base->duplex = DUPLEX_FULL;
		else
			base->duplex = DUPLEX_HALF;
	} else {
		base->autoneg = AUTONEG_DISABLE;
		ethtool_speed =
			bnxt_fw_to_ethtool_speed(link_info->req_link_speed);
		base->duplex = DUPLEX_HALF;
		if (link_info->req_duplex == BNXT_LINK_DUPLEX_FULL)
			base->duplex = DUPLEX_FULL;
	}
	base->speed = ethtool_speed;

	base->port = PORT_NONE;
	if (link_info->media_type == PORT_PHY_QCFG_RESP_MEDIA_TYPE_TP) {
		base->port = PORT_TP;
		ethtool_link_ksettings_add_link_mode(lk_ksettings, supported,
						     TP);
		ethtool_link_ksettings_add_link_mode(lk_ksettings, advertising,
						     TP);
	} else {
		ethtool_link_ksettings_add_link_mode(lk_ksettings, supported,
						     FIBRE);
		ethtool_link_ksettings_add_link_mode(lk_ksettings, advertising,
						     FIBRE);

		if (link_info->media_type == PORT_PHY_QCFG_RESP_MEDIA_TYPE_DAC)
			base->port = PORT_DA;
		else if (link_info->media_type ==
			 PORT_PHY_QCFG_RESP_MEDIA_TYPE_FIBRE)
			base->port = PORT_FIBRE;
	}
	base->phy_address = link_info->phy_addr;

	return 0;
}

static u32 bnxt_get_fw_speed(struct net_device *dev, u32 ethtool_speed)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_link_info *link_info = &bp->link_info;
	u16 support_spds = link_info->support_speeds;
	u32 fw_speed = 0;

	switch (ethtool_speed) {
	case SPEED_100:
		if (support_spds & BNXT_LINK_SPEED_MSK_100MB)
			fw_speed = PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_100MB;
		break;
	case SPEED_1000:
		if (support_spds & BNXT_LINK_SPEED_MSK_1GB)
			fw_speed = PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_1GB;
		break;
	case SPEED_2500:
		if (support_spds & BNXT_LINK_SPEED_MSK_2_5GB)
			fw_speed = PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_2_5GB;
		break;
	case SPEED_10000:
		if (support_spds & BNXT_LINK_SPEED_MSK_10GB)
			fw_speed = PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_10GB;
		break;
	case SPEED_20000:
		if (support_spds & BNXT_LINK_SPEED_MSK_20GB)
			fw_speed = PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_20GB;
		break;
	case SPEED_25000:
		if (support_spds & BNXT_LINK_SPEED_MSK_25GB)
			fw_speed = PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_25GB;
		break;
	case SPEED_40000:
		if (support_spds & BNXT_LINK_SPEED_MSK_40GB)
			fw_speed = PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_40GB;
		break;
	case SPEED_50000:
		if (support_spds & BNXT_LINK_SPEED_MSK_50GB)
			fw_speed = PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_50GB;
		break;
	case SPEED_100000:
		if (support_spds & BNXT_LINK_SPEED_MSK_100GB)
			fw_speed = PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_100GB;
		break;
	default:
		netdev_err(dev, "unsupported speed!\n");
		break;
	}
	return fw_speed;
}

u16 bnxt_get_fw_auto_link_speeds(u32 advertising)
{
	u16 fw_speed_mask = 0;

	/* only support autoneg at speed 100, 1000, and 10000 */
	if (advertising & (ADVERTISED_100baseT_Full |
			   ADVERTISED_100baseT_Half)) {
		fw_speed_mask |= BNXT_LINK_SPEED_MSK_100MB;
	}
	if (advertising & (ADVERTISED_1000baseT_Full |
			   ADVERTISED_1000baseT_Half)) {
		fw_speed_mask |= BNXT_LINK_SPEED_MSK_1GB;
	}
	if (advertising & ADVERTISED_10000baseT_Full)
		fw_speed_mask |= BNXT_LINK_SPEED_MSK_10GB;

	if (advertising & ADVERTISED_40000baseCR4_Full)
		fw_speed_mask |= BNXT_LINK_SPEED_MSK_40GB;

	return fw_speed_mask;
}

static int bnxt_set_link_ksettings(struct net_device *dev,
			   const struct ethtool_link_ksettings *lk_ksettings)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_link_info *link_info = &bp->link_info;
	const struct ethtool_link_settings *base = &lk_ksettings->base;
	bool set_pause = false;
	u16 fw_advertising = 0;
	u32 speed;
	int rc = 0;

	if (!BNXT_SINGLE_PF(bp))
		return -EOPNOTSUPP;

	if (base->autoneg == AUTONEG_ENABLE) {
		BNXT_ETHTOOL_TO_FW_SPDS(fw_advertising, lk_ksettings,
					advertising);
		link_info->autoneg |= BNXT_AUTONEG_SPEED;
		if (!fw_advertising)
			link_info->advertising = link_info->support_auto_speeds;
		else
			link_info->advertising = fw_advertising;
		/* any change to autoneg will cause link change, therefore the
		 * driver should put back the original pause setting in autoneg
		 */
		set_pause = true;
	} else {
		u16 fw_speed;
		u8 phy_type = link_info->phy_type;

		if (phy_type == PORT_PHY_QCFG_RESP_PHY_TYPE_BASET  ||
		    phy_type == PORT_PHY_QCFG_RESP_PHY_TYPE_BASETE ||
		    link_info->media_type == PORT_PHY_QCFG_RESP_MEDIA_TYPE_TP) {
			netdev_err(dev, "10GBase-T devices must autoneg\n");
			rc = -EINVAL;
			goto set_setting_exit;
		}
		if (base->duplex == DUPLEX_HALF) {
			netdev_err(dev, "HALF DUPLEX is not supported!\n");
			rc = -EINVAL;
			goto set_setting_exit;
		}
		speed = base->speed;
		fw_speed = bnxt_get_fw_speed(dev, speed);
		if (!fw_speed) {
			rc = -EINVAL;
			goto set_setting_exit;
		}
		link_info->req_link_speed = fw_speed;
		link_info->req_duplex = BNXT_LINK_DUPLEX_FULL;
		link_info->autoneg = 0;
		link_info->advertising = 0;
	}

	if (netif_running(dev))
		rc = bnxt_hwrm_set_link_setting(bp, set_pause, false);

set_setting_exit:
	return rc;
}

static void bnxt_get_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *epause)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_link_info *link_info = &bp->link_info;

	if (BNXT_VF(bp))
		return;
	epause->autoneg = !!(link_info->autoneg & BNXT_AUTONEG_FLOW_CTRL);
	epause->rx_pause = !!(link_info->req_flow_ctrl & BNXT_LINK_PAUSE_RX);
	epause->tx_pause = !!(link_info->req_flow_ctrl & BNXT_LINK_PAUSE_TX);
}

static int bnxt_set_pauseparam(struct net_device *dev,
			       struct ethtool_pauseparam *epause)
{
	int rc = 0;
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_link_info *link_info = &bp->link_info;

	if (!BNXT_SINGLE_PF(bp))
		return -EOPNOTSUPP;

	if (epause->autoneg) {
		if (!(link_info->autoneg & BNXT_AUTONEG_SPEED))
			return -EINVAL;

		link_info->autoneg |= BNXT_AUTONEG_FLOW_CTRL;
		if (bp->hwrm_spec_code >= 0x10201)
			link_info->req_flow_ctrl =
				PORT_PHY_CFG_REQ_AUTO_PAUSE_AUTONEG_PAUSE;
	} else {
		/* when transition from auto pause to force pause,
		 * force a link change
		 */
		if (link_info->autoneg & BNXT_AUTONEG_FLOW_CTRL)
			link_info->force_link_chng = true;
		link_info->autoneg &= ~BNXT_AUTONEG_FLOW_CTRL;
		link_info->req_flow_ctrl = 0;
	}
	if (epause->rx_pause)
		link_info->req_flow_ctrl |= BNXT_LINK_PAUSE_RX;

	if (epause->tx_pause)
		link_info->req_flow_ctrl |= BNXT_LINK_PAUSE_TX;

	if (netif_running(dev))
		rc = bnxt_hwrm_set_pause(bp);
	return rc;
}

static u32 bnxt_get_link(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);

	/* TODO: handle MF, VF, driver close case */
	return bp->link_info.link_up;
}

static int bnxt_find_nvram_item(struct net_device *dev, u16 type, u16 ordinal,
				u16 ext, u16 *index, u32 *item_length,
				u32 *data_length);

static int bnxt_flash_nvram(struct net_device *dev,
			    u16 dir_type,
			    u16 dir_ordinal,
			    u16 dir_ext,
			    u16 dir_attr,
			    const u8 *data,
			    size_t data_len)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc;
	struct hwrm_nvm_write_input req = {0};
	dma_addr_t dma_handle;
	u8 *kmem;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_NVM_WRITE, -1, -1);

	req.dir_type = cpu_to_le16(dir_type);
	req.dir_ordinal = cpu_to_le16(dir_ordinal);
	req.dir_ext = cpu_to_le16(dir_ext);
	req.dir_attr = cpu_to_le16(dir_attr);
	req.dir_data_length = cpu_to_le32(data_len);

	kmem = dma_alloc_coherent(&bp->pdev->dev, data_len, &dma_handle,
				  GFP_KERNEL);
	if (!kmem) {
		netdev_err(dev, "dma_alloc_coherent failure, length = %u\n",
			   (unsigned)data_len);
		return -ENOMEM;
	}
	memcpy(kmem, data, data_len);
	req.host_src_addr = cpu_to_le64(dma_handle);

	rc = hwrm_send_message(bp, &req, sizeof(req), FLASH_NVRAM_TIMEOUT);
	dma_free_coherent(&bp->pdev->dev, data_len, kmem, dma_handle);

	return rc;
}

static int bnxt_firmware_reset(struct net_device *dev,
			       u16 dir_type)
{
	struct bnxt *bp = netdev_priv(dev);
	struct hwrm_fw_reset_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FW_RESET, -1, -1);

	/* TODO: Support ASAP ChiMP self-reset (e.g. upon PF driver unload) */
	/* TODO: Address self-reset of APE/KONG/BONO/TANG or ungraceful reset */
	/*       (e.g. when firmware isn't already running) */
	switch (dir_type) {
	case BNX_DIR_TYPE_CHIMP_PATCH:
	case BNX_DIR_TYPE_BOOTCODE:
	case BNX_DIR_TYPE_BOOTCODE_2:
		req.embedded_proc_type = FW_RESET_REQ_EMBEDDED_PROC_TYPE_BOOT;
		/* Self-reset ChiMP upon next PCIe reset: */
		req.selfrst_status = FW_RESET_REQ_SELFRST_STATUS_SELFRSTPCIERST;
		break;
	case BNX_DIR_TYPE_APE_FW:
	case BNX_DIR_TYPE_APE_PATCH:
		req.embedded_proc_type = FW_RESET_REQ_EMBEDDED_PROC_TYPE_MGMT;
		/* Self-reset APE upon next PCIe reset: */
		req.selfrst_status = FW_RESET_REQ_SELFRST_STATUS_SELFRSTPCIERST;
		break;
	case BNX_DIR_TYPE_KONG_FW:
	case BNX_DIR_TYPE_KONG_PATCH:
		req.embedded_proc_type =
			FW_RESET_REQ_EMBEDDED_PROC_TYPE_NETCTRL;
		break;
	case BNX_DIR_TYPE_BONO_FW:
	case BNX_DIR_TYPE_BONO_PATCH:
		req.embedded_proc_type = FW_RESET_REQ_EMBEDDED_PROC_TYPE_ROCE;
		break;
	default:
		return -EINVAL;
	}

	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_flash_firmware(struct net_device *dev,
			       u16 dir_type,
			       const u8 *fw_data,
			       size_t fw_size)
{
	int	rc = 0;
	u16	code_type;
	u32	stored_crc;
	u32	calculated_crc;
	struct bnxt_fw_header *header = (struct bnxt_fw_header *)fw_data;

	switch (dir_type) {
	case BNX_DIR_TYPE_BOOTCODE:
	case BNX_DIR_TYPE_BOOTCODE_2:
		code_type = CODE_BOOT;
		break;
	case BNX_DIR_TYPE_CHIMP_PATCH:
		code_type = CODE_CHIMP_PATCH;
		break;
	case BNX_DIR_TYPE_APE_FW:
		code_type = CODE_MCTP_PASSTHRU;
		break;
	case BNX_DIR_TYPE_APE_PATCH:
		code_type = CODE_APE_PATCH;
		break;
	case BNX_DIR_TYPE_KONG_FW:
		code_type = CODE_KONG_FW;
		break;
	case BNX_DIR_TYPE_KONG_PATCH:
		code_type = CODE_KONG_PATCH;
		break;
	case BNX_DIR_TYPE_BONO_FW:
		code_type = CODE_BONO_FW;
		break;
	case BNX_DIR_TYPE_BONO_PATCH:
		code_type = CODE_BONO_PATCH;
		break;
	default:
		netdev_err(dev, "Unsupported directory entry type: %u\n",
			   dir_type);
		return -EINVAL;
	}
	if (fw_size < sizeof(struct bnxt_fw_header)) {
		netdev_err(dev, "Invalid firmware file size: %u\n",
			   (unsigned int)fw_size);
		return -EINVAL;
	}
	if (header->signature != cpu_to_le32(BNXT_FIRMWARE_BIN_SIGNATURE)) {
		netdev_err(dev, "Invalid firmware signature: %08X\n",
			   le32_to_cpu(header->signature));
		return -EINVAL;
	}
	if (header->code_type != code_type) {
		netdev_err(dev, "Expected firmware type: %d, read: %d\n",
			   code_type, header->code_type);
		return -EINVAL;
	}
	if (header->device != DEVICE_CUMULUS_FAMILY) {
		netdev_err(dev, "Expected firmware device family %d, read: %d\n",
			   DEVICE_CUMULUS_FAMILY, header->device);
		return -EINVAL;
	}
	/* Confirm the CRC32 checksum of the file: */
	stored_crc = le32_to_cpu(*(__le32 *)(fw_data + fw_size -
					     sizeof(stored_crc)));
	calculated_crc = ~crc32(~0, fw_data, fw_size - sizeof(stored_crc));
	if (calculated_crc != stored_crc) {
		netdev_err(dev, "Firmware file CRC32 checksum (%08lX) does not match calculated checksum (%08lX)\n",
			   (unsigned long)stored_crc,
			   (unsigned long)calculated_crc);
		return -EINVAL;
	}
	rc = bnxt_flash_nvram(dev, dir_type, BNX_DIR_ORDINAL_FIRST,
			      0, 0, fw_data, fw_size);
	if (rc == 0)	/* Firmware update successful */
		rc = bnxt_firmware_reset(dev, dir_type);

	return rc;
}

static int bnxt_flash_microcode(struct net_device *dev,
				u16 dir_type,
				const u8 *fw_data,
				size_t fw_size)
{
	struct bnxt_ucode_trailer *trailer;
	u32 calculated_crc;
	u32 stored_crc;
	int rc = 0;

	if (fw_size < sizeof(struct bnxt_ucode_trailer)) {
		netdev_err(dev, "Invalid microcode file size: %u\n",
			   (unsigned int)fw_size);
		return -EINVAL;
	}
	trailer = (struct bnxt_ucode_trailer *)(fw_data + (fw_size -
						sizeof(*trailer)));
	if (trailer->sig != cpu_to_le32(BNXT_UCODE_TRAILER_SIGNATURE)) {
		netdev_err(dev, "Invalid microcode trailer signature: %08X\n",
			   le32_to_cpu(trailer->sig));
		return -EINVAL;
	}
	if (le16_to_cpu(trailer->dir_type) != dir_type) {
		netdev_err(dev, "Expected microcode type: %d, read: %d\n",
			   dir_type, le16_to_cpu(trailer->dir_type));
		return -EINVAL;
	}
	if (le16_to_cpu(trailer->trailer_length) <
		sizeof(struct bnxt_ucode_trailer)) {
		netdev_err(dev, "Invalid microcode trailer length: %d\n",
			   le16_to_cpu(trailer->trailer_length));
		return -EINVAL;
	}

	/* Confirm the CRC32 checksum of the file: */
	stored_crc = le32_to_cpu(*(__le32 *)(fw_data + fw_size -
					     sizeof(stored_crc)));
	calculated_crc = ~crc32(~0, fw_data, fw_size - sizeof(stored_crc));
	if (calculated_crc != stored_crc) {
		netdev_err(dev,
			   "CRC32 (%08lX) does not match calculated: %08lX\n",
			   (unsigned long)stored_crc,
			   (unsigned long)calculated_crc);
		return -EINVAL;
	}
	rc = bnxt_flash_nvram(dev, dir_type, BNX_DIR_ORDINAL_FIRST,
			      0, 0, fw_data, fw_size);

	return rc;
}

static bool bnxt_dir_type_is_ape_bin_format(u16 dir_type)
{
	switch (dir_type) {
	case BNX_DIR_TYPE_CHIMP_PATCH:
	case BNX_DIR_TYPE_BOOTCODE:
	case BNX_DIR_TYPE_BOOTCODE_2:
	case BNX_DIR_TYPE_APE_FW:
	case BNX_DIR_TYPE_APE_PATCH:
	case BNX_DIR_TYPE_KONG_FW:
	case BNX_DIR_TYPE_KONG_PATCH:
	case BNX_DIR_TYPE_BONO_FW:
	case BNX_DIR_TYPE_BONO_PATCH:
		return true;
	}

	return false;
}

static bool bnxt_dir_type_is_other_exec_format(u16 dir_type)
{
	switch (dir_type) {
	case BNX_DIR_TYPE_AVS:
	case BNX_DIR_TYPE_EXP_ROM_MBA:
	case BNX_DIR_TYPE_PCIE:
	case BNX_DIR_TYPE_TSCF_UCODE:
	case BNX_DIR_TYPE_EXT_PHY:
	case BNX_DIR_TYPE_CCM:
	case BNX_DIR_TYPE_ISCSI_BOOT:
	case BNX_DIR_TYPE_ISCSI_BOOT_IPV6:
	case BNX_DIR_TYPE_ISCSI_BOOT_IPV4N6:
		return true;
	}

	return false;
}

static bool bnxt_dir_type_is_executable(u16 dir_type)
{
	return bnxt_dir_type_is_ape_bin_format(dir_type) ||
		bnxt_dir_type_is_other_exec_format(dir_type);
}

static int bnxt_flash_firmware_from_file(struct net_device *dev,
					 u16 dir_type,
					 const char *filename)
{
	const struct firmware  *fw;
	int			rc;

	rc = request_firmware(&fw, filename, &dev->dev);
	if (rc != 0) {
		netdev_err(dev, "Error %d requesting firmware file: %s\n",
			   rc, filename);
		return rc;
	}
	if (bnxt_dir_type_is_ape_bin_format(dir_type) == true)
		rc = bnxt_flash_firmware(dev, dir_type, fw->data, fw->size);
	else if (bnxt_dir_type_is_other_exec_format(dir_type) == true)
		rc = bnxt_flash_microcode(dev, dir_type, fw->data, fw->size);
	else
		rc = bnxt_flash_nvram(dev, dir_type, BNX_DIR_ORDINAL_FIRST,
				      0, 0, fw->data, fw->size);
	release_firmware(fw);
	return rc;
}

static int bnxt_flash_package_from_file(struct net_device *dev,
					char *filename, u32 install_type)
{
	struct bnxt *bp = netdev_priv(dev);
	struct hwrm_nvm_install_update_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_nvm_install_update_input install = {0};
	const struct firmware *fw;
	u32 item_len;
	u16 index;
	int rc;

	bnxt_hwrm_fw_set_time(bp);

	if (bnxt_find_nvram_item(dev, BNX_DIR_TYPE_UPDATE,
				 BNX_DIR_ORDINAL_FIRST, BNX_DIR_EXT_NONE,
				 &index, &item_len, NULL) != 0) {
		netdev_err(dev, "PKG update area not created in nvram\n");
		return -ENOBUFS;
	}

	rc = request_firmware(&fw, filename, &dev->dev);
	if (rc != 0) {
		netdev_err(dev, "PKG error %d requesting file: %s\n",
			   rc, filename);
		return rc;
	}

	if (fw->size > item_len) {
		netdev_err(dev, "PKG insufficient update area in nvram: %lu",
			   (unsigned long)fw->size);
		rc = -EFBIG;
	} else {
		dma_addr_t dma_handle;
		u8 *kmem;
		struct hwrm_nvm_modify_input modify = {0};

		bnxt_hwrm_cmd_hdr_init(bp, &modify, HWRM_NVM_MODIFY, -1, -1);

		modify.dir_idx = cpu_to_le16(index);
		modify.len = cpu_to_le32(fw->size);

		kmem = dma_alloc_coherent(&bp->pdev->dev, fw->size,
					  &dma_handle, GFP_KERNEL);
		if (!kmem) {
			netdev_err(dev,
				   "dma_alloc_coherent failure, length = %u\n",
				   (unsigned int)fw->size);
			rc = -ENOMEM;
		} else {
			memcpy(kmem, fw->data, fw->size);
			modify.host_src_addr = cpu_to_le64(dma_handle);

			rc = hwrm_send_message(bp, &modify, sizeof(modify),
					       FLASH_PACKAGE_TIMEOUT);
			dma_free_coherent(&bp->pdev->dev, fw->size, kmem,
					  dma_handle);
		}
	}
	release_firmware(fw);
	if (rc)
		return rc;

	if ((install_type & 0xffff) == 0)
		install_type >>= 16;
	bnxt_hwrm_cmd_hdr_init(bp, &install, HWRM_NVM_INSTALL_UPDATE, -1, -1);
	install.install_type = cpu_to_le32(install_type);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &install, sizeof(install),
				INSTALL_PACKAGE_TIMEOUT);
	if (rc) {
		rc = -EOPNOTSUPP;
		goto flash_pkg_exit;
	}

	if (resp->error_code) {
		u8 error_code = ((struct hwrm_err_output *)resp)->cmd_err;

		if (error_code == NVM_INSTALL_UPDATE_CMD_ERR_CODE_FRAG_ERR) {
			install.flags |= cpu_to_le16(
			       NVM_INSTALL_UPDATE_REQ_FLAGS_ALLOWED_TO_DEFRAG);
			rc = _hwrm_send_message(bp, &install, sizeof(install),
						INSTALL_PACKAGE_TIMEOUT);
			if (rc) {
				rc = -EOPNOTSUPP;
				goto flash_pkg_exit;
			}
		}
	}

	if (resp->result) {
		netdev_err(dev, "PKG install error = %d, problem_item = %d\n",
			   (s8)resp->result, (int)resp->problem_item);
		rc = -ENOPKG;
	}
flash_pkg_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_flash_device(struct net_device *dev,
			     struct ethtool_flash *flash)
{
	if (!BNXT_PF((struct bnxt *)netdev_priv(dev))) {
		netdev_err(dev, "flashdev not supported from a virtual function\n");
		return -EINVAL;
	}

	if (flash->region == ETHTOOL_FLASH_ALL_REGIONS ||
	    flash->region > 0xffff)
		return bnxt_flash_package_from_file(dev, flash->data,
						    flash->region);

	return bnxt_flash_firmware_from_file(dev, flash->region, flash->data);
}

static int nvm_get_dir_info(struct net_device *dev, u32 *entries, u32 *length)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc;
	struct hwrm_nvm_get_dir_info_input req = {0};
	struct hwrm_nvm_get_dir_info_output *output = bp->hwrm_cmd_resp_addr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_NVM_GET_DIR_INFO, -1, -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		*entries = le32_to_cpu(output->entries);
		*length = le32_to_cpu(output->entry_length);
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_get_eeprom_len(struct net_device *dev)
{
	/* The -1 return value allows the entire 32-bit range of offsets to be
	 * passed via the ethtool command-line utility.
	 */
	return -1;
}

static int bnxt_get_nvram_directory(struct net_device *dev, u32 len, u8 *data)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc;
	u32 dir_entries;
	u32 entry_length;
	u8 *buf;
	size_t buflen;
	dma_addr_t dma_handle;
	struct hwrm_nvm_get_dir_entries_input req = {0};

	rc = nvm_get_dir_info(dev, &dir_entries, &entry_length);
	if (rc != 0)
		return rc;

	/* Insert 2 bytes of directory info (count and size of entries) */
	if (len < 2)
		return -EINVAL;

	*data++ = dir_entries;
	*data++ = entry_length;
	len -= 2;
	memset(data, 0xff, len);

	buflen = dir_entries * entry_length;
	buf = dma_alloc_coherent(&bp->pdev->dev, buflen, &dma_handle,
				 GFP_KERNEL);
	if (!buf) {
		netdev_err(dev, "dma_alloc_coherent failure, length = %u\n",
			   (unsigned)buflen);
		return -ENOMEM;
	}
	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_NVM_GET_DIR_ENTRIES, -1, -1);
	req.host_dest_addr = cpu_to_le64(dma_handle);
	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc == 0)
		memcpy(data, buf, len > buflen ? buflen : len);
	dma_free_coherent(&bp->pdev->dev, buflen, buf, dma_handle);
	return rc;
}

static int bnxt_get_nvram_item(struct net_device *dev, u32 index, u32 offset,
			       u32 length, u8 *data)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc;
	u8 *buf;
	dma_addr_t dma_handle;
	struct hwrm_nvm_read_input req = {0};

	buf = dma_alloc_coherent(&bp->pdev->dev, length, &dma_handle,
				 GFP_KERNEL);
	if (!buf) {
		netdev_err(dev, "dma_alloc_coherent failure, length = %u\n",
			   (unsigned)length);
		return -ENOMEM;
	}
	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_NVM_READ, -1, -1);
	req.host_dest_addr = cpu_to_le64(dma_handle);
	req.dir_idx = cpu_to_le16(index);
	req.offset = cpu_to_le32(offset);
	req.len = cpu_to_le32(length);

	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc == 0)
		memcpy(data, buf, length);
	dma_free_coherent(&bp->pdev->dev, length, buf, dma_handle);
	return rc;
}

static int bnxt_find_nvram_item(struct net_device *dev, u16 type, u16 ordinal,
				u16 ext, u16 *index, u32 *item_length,
				u32 *data_length)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc;
	struct hwrm_nvm_find_dir_entry_input req = {0};
	struct hwrm_nvm_find_dir_entry_output *output = bp->hwrm_cmd_resp_addr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_NVM_FIND_DIR_ENTRY, -1, -1);
	req.enables = 0;
	req.dir_idx = 0;
	req.dir_type = cpu_to_le16(type);
	req.dir_ordinal = cpu_to_le16(ordinal);
	req.dir_ext = cpu_to_le16(ext);
	req.opt_ordinal = NVM_FIND_DIR_ENTRY_REQ_OPT_ORDINAL_EQ;
	rc = hwrm_send_message_silent(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc == 0) {
		if (index)
			*index = le16_to_cpu(output->dir_idx);
		if (item_length)
			*item_length = le32_to_cpu(output->dir_item_length);
		if (data_length)
			*data_length = le32_to_cpu(output->dir_data_length);
	}
	return rc;
}

static char *bnxt_parse_pkglog(int desired_field, u8 *data, size_t datalen)
{
	char	*retval = NULL;
	char	*p;
	char	*value;
	int	field = 0;

	if (datalen < 1)
		return NULL;
	/* null-terminate the log data (removing last '\n'): */
	data[datalen - 1] = 0;
	for (p = data; *p != 0; p++) {
		field = 0;
		retval = NULL;
		while (*p != 0 && *p != '\n') {
			value = p;
			while (*p != 0 && *p != '\t' && *p != '\n')
				p++;
			if (field == desired_field)
				retval = value;
			if (*p != '\t')
				break;
			*p = 0;
			field++;
			p++;
		}
		if (*p == 0)
			break;
		*p = 0;
	}
	return retval;
}

static char *bnxt_get_pkgver(struct net_device *dev, char *buf, size_t buflen)
{
	u16 index = 0;
	u32 datalen;

	if (bnxt_find_nvram_item(dev, BNX_DIR_TYPE_PKG_LOG,
				 BNX_DIR_ORDINAL_FIRST, BNX_DIR_EXT_NONE,
				 &index, NULL, &datalen) != 0)
		return NULL;

	memset(buf, 0, buflen);
	if (bnxt_get_nvram_item(dev, index, 0, datalen, buf) != 0)
		return NULL;

	return bnxt_parse_pkglog(BNX_PKG_LOG_FIELD_IDX_PKG_VERSION, buf,
		datalen);
}

static int bnxt_get_eeprom(struct net_device *dev,
			   struct ethtool_eeprom *eeprom,
			   u8 *data)
{
	u32 index;
	u32 offset;

	if (eeprom->offset == 0) /* special offset value to get directory */
		return bnxt_get_nvram_directory(dev, eeprom->len, data);

	index = eeprom->offset >> 24;
	offset = eeprom->offset & 0xffffff;

	if (index == 0) {
		netdev_err(dev, "unsupported index value: %d\n", index);
		return -EINVAL;
	}

	return bnxt_get_nvram_item(dev, index - 1, offset, eeprom->len, data);
}

static int bnxt_erase_nvram_directory(struct net_device *dev, u8 index)
{
	struct bnxt *bp = netdev_priv(dev);
	struct hwrm_nvm_erase_dir_entry_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_NVM_ERASE_DIR_ENTRY, -1, -1);
	req.dir_idx = cpu_to_le16(index);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_set_eeprom(struct net_device *dev,
			   struct ethtool_eeprom *eeprom,
			   u8 *data)
{
	struct bnxt *bp = netdev_priv(dev);
	u8 index, dir_op;
	u16 type, ext, ordinal, attr;

	if (!BNXT_PF(bp)) {
		netdev_err(dev, "NVM write not supported from a virtual function\n");
		return -EINVAL;
	}

	type = eeprom->magic >> 16;

	if (type == 0xffff) { /* special value for directory operations */
		index = eeprom->magic & 0xff;
		dir_op = eeprom->magic >> 8;
		if (index == 0)
			return -EINVAL;
		switch (dir_op) {
		case 0x0e: /* erase */
			if (eeprom->offset != ~eeprom->magic)
				return -EINVAL;
			return bnxt_erase_nvram_directory(dev, index - 1);
		default:
			return -EINVAL;
		}
	}

	/* Create or re-write an NVM item: */
	if (bnxt_dir_type_is_executable(type) == true)
		return -EOPNOTSUPP;
	ext = eeprom->magic & 0xffff;
	ordinal = eeprom->offset >> 16;
	attr = eeprom->offset & 0xffff;

	return bnxt_flash_nvram(dev, type, ordinal, ext, attr, data,
				eeprom->len);
}

static int bnxt_set_eee(struct net_device *dev, struct ethtool_eee *edata)
{
	struct bnxt *bp = netdev_priv(dev);
	struct ethtool_eee *eee = &bp->eee;
	struct bnxt_link_info *link_info = &bp->link_info;
	u32 advertising =
		 _bnxt_fw_to_ethtool_adv_spds(link_info->advertising, 0);
	int rc = 0;

	if (!BNXT_SINGLE_PF(bp))
		return -EOPNOTSUPP;

	if (!(bp->flags & BNXT_FLAG_EEE_CAP))
		return -EOPNOTSUPP;

	if (!edata->eee_enabled)
		goto eee_ok;

	if (!(link_info->autoneg & BNXT_AUTONEG_SPEED)) {
		netdev_warn(dev, "EEE requires autoneg\n");
		return -EINVAL;
	}
	if (edata->tx_lpi_enabled) {
		if (bp->lpi_tmr_hi && (edata->tx_lpi_timer > bp->lpi_tmr_hi ||
				       edata->tx_lpi_timer < bp->lpi_tmr_lo)) {
			netdev_warn(dev, "Valid LPI timer range is %d and %d microsecs\n",
				    bp->lpi_tmr_lo, bp->lpi_tmr_hi);
			return -EINVAL;
		} else if (!bp->lpi_tmr_hi) {
			edata->tx_lpi_timer = eee->tx_lpi_timer;
		}
	}
	if (!edata->advertised) {
		edata->advertised = advertising & eee->supported;
	} else if (edata->advertised & ~advertising) {
		netdev_warn(dev, "EEE advertised %x must be a subset of autoneg advertised speeds %x\n",
			    edata->advertised, advertising);
		return -EINVAL;
	}

	eee->advertised = edata->advertised;
	eee->tx_lpi_enabled = edata->tx_lpi_enabled;
	eee->tx_lpi_timer = edata->tx_lpi_timer;
eee_ok:
	eee->eee_enabled = edata->eee_enabled;

	if (netif_running(dev))
		rc = bnxt_hwrm_set_link_setting(bp, false, true);

	return rc;
}

static int bnxt_get_eee(struct net_device *dev, struct ethtool_eee *edata)
{
	struct bnxt *bp = netdev_priv(dev);

	if (!(bp->flags & BNXT_FLAG_EEE_CAP))
		return -EOPNOTSUPP;

	*edata = bp->eee;
	if (!bp->eee.eee_enabled) {
		/* Preserve tx_lpi_timer so that the last value will be used
		 * by default when it is re-enabled.
		 */
		edata->advertised = 0;
		edata->tx_lpi_enabled = 0;
	}

	if (!bp->eee.eee_active)
		edata->lp_advertised = 0;

	return 0;
}

static int bnxt_read_sfp_module_eeprom_info(struct bnxt *bp, u16 i2c_addr,
					    u16 page_number, u16 start_addr,
					    u16 data_length, u8 *buf)
{
	struct hwrm_port_phy_i2c_read_input req = {0};
	struct hwrm_port_phy_i2c_read_output *output = bp->hwrm_cmd_resp_addr;
	int rc, byte_offset = 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_PHY_I2C_READ, -1, -1);
	req.i2c_slave_addr = i2c_addr;
	req.page_number = cpu_to_le16(page_number);
	req.port_id = cpu_to_le16(bp->pf.port_id);
	do {
		u16 xfer_size;

		xfer_size = min_t(u16, data_length, BNXT_MAX_PHY_I2C_RESP_SIZE);
		data_length -= xfer_size;
		req.page_offset = cpu_to_le16(start_addr + byte_offset);
		req.data_length = xfer_size;
		req.enables = cpu_to_le32(start_addr + byte_offset ?
				 PORT_PHY_I2C_READ_REQ_ENABLES_PAGE_OFFSET : 0);
		mutex_lock(&bp->hwrm_cmd_lock);
		rc = _hwrm_send_message(bp, &req, sizeof(req),
					HWRM_CMD_TIMEOUT);
		if (!rc)
			memcpy(buf + byte_offset, output->data, xfer_size);
		mutex_unlock(&bp->hwrm_cmd_lock);
		byte_offset += xfer_size;
	} while (!rc && data_length > 0);

	return rc;
}

static int bnxt_get_module_info(struct net_device *dev,
				struct ethtool_modinfo *modinfo)
{
	struct bnxt *bp = netdev_priv(dev);
	struct hwrm_port_phy_i2c_read_input req = {0};
	struct hwrm_port_phy_i2c_read_output *output = bp->hwrm_cmd_resp_addr;
	int rc;

	/* No point in going further if phy status indicates
	 * module is not inserted or if it is powered down or
	 * if it is of type 10GBase-T
	 */
	if (bp->link_info.module_status >
		PORT_PHY_QCFG_RESP_MODULE_STATUS_WARNINGMSG)
		return -EOPNOTSUPP;

	/* This feature is not supported in older firmware versions */
	if (bp->hwrm_spec_code < 0x10202)
		return -EOPNOTSUPP;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_PHY_I2C_READ, -1, -1);
	req.i2c_slave_addr = I2C_DEV_ADDR_A0;
	req.page_number = 0;
	req.page_offset = cpu_to_le16(SFP_EEPROM_SFF_8472_COMP_ADDR);
	req.data_length = SFP_EEPROM_SFF_8472_COMP_SIZE;
	req.port_id = cpu_to_le16(bp->pf.port_id);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		u32 module_id = le32_to_cpu(output->data[0]);

		switch (module_id) {
		case SFF_MODULE_ID_SFP:
			modinfo->type = ETH_MODULE_SFF_8472;
			modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
			break;
		case SFF_MODULE_ID_QSFP:
		case SFF_MODULE_ID_QSFP_PLUS:
			modinfo->type = ETH_MODULE_SFF_8436;
			modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
			break;
		case SFF_MODULE_ID_QSFP28:
			modinfo->type = ETH_MODULE_SFF_8636;
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;
			break;
		default:
			rc = -EOPNOTSUPP;
			break;
		}
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_get_module_eeprom(struct net_device *dev,
				  struct ethtool_eeprom *eeprom,
				  u8 *data)
{
	struct bnxt *bp = netdev_priv(dev);
	u16  start = eeprom->offset, length = eeprom->len;
	int rc = 0;

	memset(data, 0, eeprom->len);

	/* Read A0 portion of the EEPROM */
	if (start < ETH_MODULE_SFF_8436_LEN) {
		if (start + eeprom->len > ETH_MODULE_SFF_8436_LEN)
			length = ETH_MODULE_SFF_8436_LEN - start;
		rc = bnxt_read_sfp_module_eeprom_info(bp, I2C_DEV_ADDR_A0, 0,
						      start, length, data);
		if (rc)
			return rc;
		start += length;
		data += length;
		length = eeprom->len - length;
	}

	/* Read A2 portion of the EEPROM */
	if (length) {
		start -= ETH_MODULE_SFF_8436_LEN;
		bnxt_read_sfp_module_eeprom_info(bp, I2C_DEV_ADDR_A2, 1, start,
						 length, data);
	}
	return rc;
}

static int bnxt_nway_reset(struct net_device *dev)
{
	int rc = 0;

	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_link_info *link_info = &bp->link_info;

	if (!BNXT_SINGLE_PF(bp))
		return -EOPNOTSUPP;

	if (!(link_info->autoneg & BNXT_AUTONEG_SPEED))
		return -EINVAL;

	if (netif_running(dev))
		rc = bnxt_hwrm_set_link_setting(bp, true, false);

	return rc;
}

static int bnxt_set_phys_id(struct net_device *dev,
			    enum ethtool_phys_id_state state)
{
	struct hwrm_port_led_cfg_input req = {0};
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_pf_info *pf = &bp->pf;
	struct bnxt_led_cfg *led_cfg;
	u8 led_state;
	__le16 duration;
	int i, rc;

	if (!bp->num_leds || BNXT_VF(bp))
		return -EOPNOTSUPP;

	if (state == ETHTOOL_ID_ACTIVE) {
		led_state = PORT_LED_CFG_REQ_LED0_STATE_BLINKALT;
		duration = cpu_to_le16(500);
	} else if (state == ETHTOOL_ID_INACTIVE) {
		led_state = PORT_LED_CFG_REQ_LED1_STATE_DEFAULT;
		duration = cpu_to_le16(0);
	} else {
		return -EINVAL;
	}
	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_LED_CFG, -1, -1);
	req.port_id = cpu_to_le16(pf->port_id);
	req.num_leds = bp->num_leds;
	led_cfg = (struct bnxt_led_cfg *)&req.led0_id;
	for (i = 0; i < bp->num_leds; i++, led_cfg++) {
		req.enables |= BNXT_LED_DFLT_ENABLES(i);
		led_cfg->led_id = bp->leds[i].led_id;
		led_cfg->led_state = led_state;
		led_cfg->led_blink_on = duration;
		led_cfg->led_blink_off = duration;
		led_cfg->led_group_id = bp->leds[i].led_group_id;
	}
	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		rc = -EIO;
	return rc;
}

static int bnxt_hwrm_selftest_irq(struct bnxt *bp, u16 cmpl_ring)
{
	struct hwrm_selftest_irq_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_SELFTEST_IRQ, cmpl_ring, -1);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_test_irq(struct bnxt *bp)
{
	int i;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		u16 cmpl_ring = bp->grp_info[i].cp_fw_ring_id;
		int rc;

		rc = bnxt_hwrm_selftest_irq(bp, cmpl_ring);
		if (rc)
			return rc;
	}
	return 0;
}

static int bnxt_hwrm_mac_loopback(struct bnxt *bp, bool enable)
{
	struct hwrm_port_mac_cfg_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_MAC_CFG, -1, -1);

	req.enables = cpu_to_le32(PORT_MAC_CFG_REQ_ENABLES_LPBK);
	if (enable)
		req.lpbk = PORT_MAC_CFG_REQ_LPBK_LOCAL;
	else
		req.lpbk = PORT_MAC_CFG_REQ_LPBK_NONE;
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_disable_an_for_lpbk(struct bnxt *bp,
				    struct hwrm_port_phy_cfg_input *req)
{
	struct bnxt_link_info *link_info = &bp->link_info;
	u16 fw_advertising = link_info->advertising;
	u16 fw_speed;
	int rc;

	if (!link_info->autoneg)
		return 0;

	fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_1GB;
	if (netif_carrier_ok(bp->dev))
		fw_speed = bp->link_info.link_speed;
	else if (fw_advertising & BNXT_LINK_SPEED_MSK_10GB)
		fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_10GB;
	else if (fw_advertising & BNXT_LINK_SPEED_MSK_25GB)
		fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_25GB;
	else if (fw_advertising & BNXT_LINK_SPEED_MSK_40GB)
		fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_40GB;
	else if (fw_advertising & BNXT_LINK_SPEED_MSK_50GB)
		fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_50GB;

	req->force_link_speed = cpu_to_le16(fw_speed);
	req->flags |= cpu_to_le32(PORT_PHY_CFG_REQ_FLAGS_FORCE |
				  PORT_PHY_CFG_REQ_FLAGS_RESET_PHY);
	rc = hwrm_send_message(bp, req, sizeof(*req), HWRM_CMD_TIMEOUT);
	req->flags = 0;
	req->force_link_speed = cpu_to_le16(0);
	return rc;
}

static int bnxt_hwrm_phy_loopback(struct bnxt *bp, bool enable)
{
	struct hwrm_port_phy_cfg_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_PHY_CFG, -1, -1);

	if (enable) {
		bnxt_disable_an_for_lpbk(bp, &req);
		req.lpbk = PORT_PHY_CFG_REQ_LPBK_LOCAL;
	} else {
		req.lpbk = PORT_PHY_CFG_REQ_LPBK_NONE;
	}
	req.enables = cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_LPBK);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_rx_loopback(struct bnxt *bp, struct bnxt_napi *bnapi,
			    u32 raw_cons, int pkt_size)
{
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	struct bnxt_rx_ring_info *rxr = bnapi->rx_ring;
	struct bnxt_sw_rx_bd *rx_buf;
	struct rx_cmp *rxcmp;
	u16 cp_cons, cons;
	u8 *data;
	u32 len;
	int i;

	cp_cons = RING_CMP(raw_cons);
	rxcmp = (struct rx_cmp *)
		&cpr->cp_desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];
	cons = rxcmp->rx_cmp_opaque;
	rx_buf = &rxr->rx_buf_ring[cons];
	data = rx_buf->data_ptr;
	len = le32_to_cpu(rxcmp->rx_cmp_len_flags_type) >> RX_CMP_LEN_SHIFT;
	if (len != pkt_size)
		return -EIO;
	i = ETH_ALEN;
	if (!ether_addr_equal(data + i, bnapi->bp->dev->dev_addr))
		return -EIO;
	i += ETH_ALEN;
	for (  ; i < pkt_size; i++) {
		if (data[i] != (u8)(i & 0xff))
			return -EIO;
	}
	return 0;
}

static int bnxt_poll_loopback(struct bnxt *bp, int pkt_size)
{
	struct bnxt_napi *bnapi = bp->bnapi[0];
	struct bnxt_cp_ring_info *cpr;
	struct tx_cmp *txcmp;
	int rc = -EIO;
	u32 raw_cons;
	u32 cons;
	int i;

	cpr = &bnapi->cp_ring;
	raw_cons = cpr->cp_raw_cons;
	for (i = 0; i < 200; i++) {
		cons = RING_CMP(raw_cons);
		txcmp = &cpr->cp_desc_ring[CP_RING(cons)][CP_IDX(cons)];

		if (!TX_CMP_VALID(txcmp, raw_cons)) {
			udelay(5);
			continue;
		}

		/* The valid test of the entry must be done first before
		 * reading any further.
		 */
		dma_rmb();
		if (TX_CMP_TYPE(txcmp) == CMP_TYPE_RX_L2_CMP) {
			rc = bnxt_rx_loopback(bp, bnapi, raw_cons, pkt_size);
			raw_cons = NEXT_RAW_CMP(raw_cons);
			raw_cons = NEXT_RAW_CMP(raw_cons);
			break;
		}
		raw_cons = NEXT_RAW_CMP(raw_cons);
	}
	cpr->cp_raw_cons = raw_cons;
	return rc;
}

static int bnxt_run_loopback(struct bnxt *bp)
{
	struct bnxt_tx_ring_info *txr = &bp->tx_ring[0];
	int pkt_size, i = 0;
	struct sk_buff *skb;
	dma_addr_t map;
	u8 *data;
	int rc;

	pkt_size = min(bp->dev->mtu + ETH_HLEN, bp->rx_copy_thresh);
	skb = netdev_alloc_skb(bp->dev, pkt_size);
	if (!skb)
		return -ENOMEM;
	data = skb_put(skb, pkt_size);
	eth_broadcast_addr(data);
	i += ETH_ALEN;
	ether_addr_copy(&data[i], bp->dev->dev_addr);
	i += ETH_ALEN;
	for ( ; i < pkt_size; i++)
		data[i] = (u8)(i & 0xff);

	map = dma_map_single(&bp->pdev->dev, skb->data, pkt_size,
			     PCI_DMA_TODEVICE);
	if (dma_mapping_error(&bp->pdev->dev, map)) {
		dev_kfree_skb(skb);
		return -EIO;
	}
	bnxt_xmit_xdp(bp, txr, map, pkt_size, 0);

	/* Sync BD data before updating doorbell */
	wmb();

	writel(DB_KEY_TX | txr->tx_prod, txr->tx_doorbell);
	writel(DB_KEY_TX | txr->tx_prod, txr->tx_doorbell);
	rc = bnxt_poll_loopback(bp, pkt_size);

	dma_unmap_single(&bp->pdev->dev, map, pkt_size, PCI_DMA_TODEVICE);
	dev_kfree_skb(skb);
	return rc;
}

static int bnxt_run_fw_tests(struct bnxt *bp, u8 test_mask, u8 *test_results)
{
	struct hwrm_selftest_exec_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_selftest_exec_input req = {0};
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_SELFTEST_EXEC, -1, -1);
	mutex_lock(&bp->hwrm_cmd_lock);
	resp->test_success = 0;
	req.flags = test_mask;
	rc = _hwrm_send_message(bp, &req, sizeof(req), bp->test_info->timeout);
	*test_results = resp->test_success;
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

#define BNXT_DRV_TESTS			3
#define BNXT_MACLPBK_TEST_IDX		(bp->num_tests - BNXT_DRV_TESTS)
#define BNXT_PHYLPBK_TEST_IDX		(BNXT_MACLPBK_TEST_IDX + 1)
#define BNXT_IRQ_TEST_IDX		(BNXT_MACLPBK_TEST_IDX + 2)

static void bnxt_self_test(struct net_device *dev, struct ethtool_test *etest,
			   u64 *buf)
{
	struct bnxt *bp = netdev_priv(dev);
	bool offline = false;
	u8 test_results = 0;
	u8 test_mask = 0;
	int rc, i;

	if (!bp->num_tests || !BNXT_SINGLE_PF(bp))
		return;
	memset(buf, 0, sizeof(u64) * bp->num_tests);
	if (!netif_running(dev)) {
		etest->flags |= ETH_TEST_FL_FAILED;
		return;
	}

	if (etest->flags & ETH_TEST_FL_OFFLINE) {
		if (bp->pf.active_vfs) {
			etest->flags |= ETH_TEST_FL_FAILED;
			netdev_warn(dev, "Offline tests cannot be run with active VFs\n");
			return;
		}
		offline = true;
	}

	for (i = 0; i < bp->num_tests - BNXT_DRV_TESTS; i++) {
		u8 bit_val = 1 << i;

		if (!(bp->test_info->offline_mask & bit_val))
			test_mask |= bit_val;
		else if (offline)
			test_mask |= bit_val;
	}
	if (!offline) {
		bnxt_run_fw_tests(bp, test_mask, &test_results);
	} else {
		rc = bnxt_close_nic(bp, false, false);
		if (rc)
			return;
		bnxt_run_fw_tests(bp, test_mask, &test_results);

		buf[BNXT_MACLPBK_TEST_IDX] = 1;
		bnxt_hwrm_mac_loopback(bp, true);
		msleep(250);
		rc = bnxt_half_open_nic(bp);
		if (rc) {
			bnxt_hwrm_mac_loopback(bp, false);
			etest->flags |= ETH_TEST_FL_FAILED;
			return;
		}
		if (bnxt_run_loopback(bp))
			etest->flags |= ETH_TEST_FL_FAILED;
		else
			buf[BNXT_MACLPBK_TEST_IDX] = 0;

		bnxt_hwrm_mac_loopback(bp, false);
		bnxt_hwrm_phy_loopback(bp, true);
		msleep(1000);
		if (bnxt_run_loopback(bp)) {
			buf[BNXT_PHYLPBK_TEST_IDX] = 1;
			etest->flags |= ETH_TEST_FL_FAILED;
		}
		bnxt_hwrm_phy_loopback(bp, false);
		bnxt_half_close_nic(bp);
		bnxt_open_nic(bp, false, true);
	}
	if (bnxt_test_irq(bp)) {
		buf[BNXT_IRQ_TEST_IDX] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}
	for (i = 0; i < bp->num_tests - BNXT_DRV_TESTS; i++) {
		u8 bit_val = 1 << i;

		if ((test_mask & bit_val) && !(test_results & bit_val)) {
			buf[i] = 1;
			etest->flags |= ETH_TEST_FL_FAILED;
		}
	}
}

void bnxt_ethtool_init(struct bnxt *bp)
{
	struct hwrm_selftest_qlist_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_selftest_qlist_input req = {0};
	struct bnxt_test_info *test_info;
	int i, rc;

	if (bp->hwrm_spec_code < 0x10704 || !BNXT_SINGLE_PF(bp))
		return;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_SELFTEST_QLIST, -1, -1);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		goto ethtool_init_exit;

	test_info = kzalloc(sizeof(*bp->test_info), GFP_KERNEL);
	if (!test_info)
		goto ethtool_init_exit;

	bp->test_info = test_info;
	bp->num_tests = resp->num_tests + BNXT_DRV_TESTS;
	if (bp->num_tests > BNXT_MAX_TEST)
		bp->num_tests = BNXT_MAX_TEST;

	test_info->offline_mask = resp->offline_tests;
	test_info->timeout = le16_to_cpu(resp->test_timeout);
	if (!test_info->timeout)
		test_info->timeout = HWRM_CMD_TIMEOUT;
	for (i = 0; i < bp->num_tests; i++) {
		char *str = test_info->string[i];
		char *fw_str = resp->test0_name + i * 32;

		if (i == BNXT_MACLPBK_TEST_IDX) {
			strcpy(str, "Mac loopback test (offline)");
		} else if (i == BNXT_PHYLPBK_TEST_IDX) {
			strcpy(str, "Phy loopback test (offline)");
		} else if (i == BNXT_IRQ_TEST_IDX) {
			strcpy(str, "Interrupt_test (offline)");
		} else {
			strlcpy(str, fw_str, ETH_GSTRING_LEN);
			strncat(str, " test", ETH_GSTRING_LEN - strlen(str));
			if (test_info->offline_mask & (1 << i))
				strncat(str, " (offline)",
					ETH_GSTRING_LEN - strlen(str));
			else
				strncat(str, " (online)",
					ETH_GSTRING_LEN - strlen(str));
		}
	}

ethtool_init_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
}

void bnxt_ethtool_free(struct bnxt *bp)
{
	kfree(bp->test_info);
	bp->test_info = NULL;
}

const struct ethtool_ops bnxt_ethtool_ops = {
	.get_link_ksettings	= bnxt_get_link_ksettings,
	.set_link_ksettings	= bnxt_set_link_ksettings,
	.get_pauseparam		= bnxt_get_pauseparam,
	.set_pauseparam		= bnxt_set_pauseparam,
	.get_drvinfo		= bnxt_get_drvinfo,
	.get_wol		= bnxt_get_wol,
	.set_wol		= bnxt_set_wol,
	.get_coalesce		= bnxt_get_coalesce,
	.set_coalesce		= bnxt_set_coalesce,
	.get_msglevel		= bnxt_get_msglevel,
	.set_msglevel		= bnxt_set_msglevel,
	.get_sset_count		= bnxt_get_sset_count,
	.get_strings		= bnxt_get_strings,
	.get_ethtool_stats	= bnxt_get_ethtool_stats,
	.set_ringparam		= bnxt_set_ringparam,
	.get_ringparam		= bnxt_get_ringparam,
	.get_channels		= bnxt_get_channels,
	.set_channels		= bnxt_set_channels,
	.get_rxnfc		= bnxt_get_rxnfc,
	.set_rxnfc		= bnxt_set_rxnfc,
	.get_rxfh_indir_size    = bnxt_get_rxfh_indir_size,
	.get_rxfh_key_size      = bnxt_get_rxfh_key_size,
	.get_rxfh               = bnxt_get_rxfh,
	.flash_device		= bnxt_flash_device,
	.get_eeprom_len         = bnxt_get_eeprom_len,
	.get_eeprom             = bnxt_get_eeprom,
	.set_eeprom		= bnxt_set_eeprom,
	.get_link		= bnxt_get_link,
	.get_eee		= bnxt_get_eee,
	.set_eee		= bnxt_set_eee,
	.get_module_info	= bnxt_get_module_info,
	.get_module_eeprom	= bnxt_get_module_eeprom,
	.nway_reset		= bnxt_nway_reset,
	.set_phys_id		= bnxt_set_phys_id,
	.self_test		= bnxt_self_test,
};
