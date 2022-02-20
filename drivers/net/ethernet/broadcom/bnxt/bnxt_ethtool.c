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
#include <linux/linkmode.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/crc32.h>
#include <linux/firmware.h>
#include <linux/utsname.h>
#include <linux/time.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/net_tstamp.h>
#include <linux/timecounter.h>
#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_hwrm.h"
#include "bnxt_ulp.h"
#include "bnxt_xdp.h"
#include "bnxt_ptp.h"
#include "bnxt_ethtool.h"
#include "bnxt_nvm_defs.h"	/* NVRAM content constant and structure defs */
#include "bnxt_fw_hdr.h"	/* Firmware hdr constant and structure defs */
#include "bnxt_coredump.h"

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
			     struct ethtool_coalesce *coal,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_coal *hw_coal;
	u16 mult;

	memset(coal, 0, sizeof(*coal));

	coal->use_adaptive_rx_coalesce = bp->flags & BNXT_FLAG_DIM;

	hw_coal = &bp->rx_coal;
	mult = hw_coal->bufs_per_record;
	coal->rx_coalesce_usecs = hw_coal->coal_ticks;
	coal->rx_max_coalesced_frames = hw_coal->coal_bufs / mult;
	coal->rx_coalesce_usecs_irq = hw_coal->coal_ticks_irq;
	coal->rx_max_coalesced_frames_irq = hw_coal->coal_bufs_irq / mult;
	if (hw_coal->flags &
	    RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_TIMER_RESET)
		kernel_coal->use_cqe_mode_rx = true;

	hw_coal = &bp->tx_coal;
	mult = hw_coal->bufs_per_record;
	coal->tx_coalesce_usecs = hw_coal->coal_ticks;
	coal->tx_max_coalesced_frames = hw_coal->coal_bufs / mult;
	coal->tx_coalesce_usecs_irq = hw_coal->coal_ticks_irq;
	coal->tx_max_coalesced_frames_irq = hw_coal->coal_bufs_irq / mult;
	if (hw_coal->flags &
	    RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_TIMER_RESET)
		kernel_coal->use_cqe_mode_tx = true;

	coal->stats_block_coalesce_usecs = bp->stats_coal_ticks;

	return 0;
}

static int bnxt_set_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *coal,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
{
	struct bnxt *bp = netdev_priv(dev);
	bool update_stats = false;
	struct bnxt_coal *hw_coal;
	int rc = 0;
	u16 mult;

	if (coal->use_adaptive_rx_coalesce) {
		bp->flags |= BNXT_FLAG_DIM;
	} else {
		if (bp->flags & BNXT_FLAG_DIM) {
			bp->flags &= ~(BNXT_FLAG_DIM);
			goto reset_coalesce;
		}
	}

	if ((kernel_coal->use_cqe_mode_rx || kernel_coal->use_cqe_mode_tx) &&
	    !(bp->coal_cap.cmpl_params &
	      RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_TIMER_RESET))
		return -EOPNOTSUPP;

	hw_coal = &bp->rx_coal;
	mult = hw_coal->bufs_per_record;
	hw_coal->coal_ticks = coal->rx_coalesce_usecs;
	hw_coal->coal_bufs = coal->rx_max_coalesced_frames * mult;
	hw_coal->coal_ticks_irq = coal->rx_coalesce_usecs_irq;
	hw_coal->coal_bufs_irq = coal->rx_max_coalesced_frames_irq * mult;
	hw_coal->flags &=
		~RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_TIMER_RESET;
	if (kernel_coal->use_cqe_mode_rx)
		hw_coal->flags |=
			RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_TIMER_RESET;

	hw_coal = &bp->tx_coal;
	mult = hw_coal->bufs_per_record;
	hw_coal->coal_ticks = coal->tx_coalesce_usecs;
	hw_coal->coal_bufs = coal->tx_max_coalesced_frames * mult;
	hw_coal->coal_ticks_irq = coal->tx_coalesce_usecs_irq;
	hw_coal->coal_bufs_irq = coal->tx_max_coalesced_frames_irq * mult;
	hw_coal->flags &=
		~RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_TIMER_RESET;
	if (kernel_coal->use_cqe_mode_tx)
		hw_coal->flags |=
			RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_TIMER_RESET;

	if (bp->stats_coal_ticks != coal->stats_block_coalesce_usecs) {
		u32 stats_ticks = coal->stats_block_coalesce_usecs;

		/* Allow 0, which means disable. */
		if (stats_ticks)
			stats_ticks = clamp_t(u32, stats_ticks,
					      BNXT_MIN_STATS_COAL_TICKS,
					      BNXT_MAX_STATS_COAL_TICKS);
		stats_ticks = rounddown(stats_ticks, BNXT_MIN_STATS_COAL_TICKS);
		bp->stats_coal_ticks = stats_ticks;
		if (bp->stats_coal_ticks)
			bp->current_interval =
				bp->stats_coal_ticks * HZ / 1000000;
		else
			bp->current_interval = BNXT_TIMER_INTERVAL;
		update_stats = true;
	}

reset_coalesce:
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

static const char * const bnxt_ring_rx_stats_str[] = {
	"rx_ucast_packets",
	"rx_mcast_packets",
	"rx_bcast_packets",
	"rx_discards",
	"rx_errors",
	"rx_ucast_bytes",
	"rx_mcast_bytes",
	"rx_bcast_bytes",
};

static const char * const bnxt_ring_tx_stats_str[] = {
	"tx_ucast_packets",
	"tx_mcast_packets",
	"tx_bcast_packets",
	"tx_errors",
	"tx_discards",
	"tx_ucast_bytes",
	"tx_mcast_bytes",
	"tx_bcast_bytes",
};

static const char * const bnxt_ring_tpa_stats_str[] = {
	"tpa_packets",
	"tpa_bytes",
	"tpa_events",
	"tpa_aborts",
};

static const char * const bnxt_ring_tpa2_stats_str[] = {
	"rx_tpa_eligible_pkt",
	"rx_tpa_eligible_bytes",
	"rx_tpa_pkt",
	"rx_tpa_bytes",
	"rx_tpa_errors",
	"rx_tpa_events",
};

static const char * const bnxt_rx_sw_stats_str[] = {
	"rx_l4_csum_errors",
	"rx_resets",
	"rx_buf_errors",
};

static const char * const bnxt_cmn_sw_stats_str[] = {
	"missed_irqs",
};

#define BNXT_RX_STATS_ENTRY(counter)	\
	{ BNXT_RX_STATS_OFFSET(counter), __stringify(counter) }

#define BNXT_TX_STATS_ENTRY(counter)	\
	{ BNXT_TX_STATS_OFFSET(counter), __stringify(counter) }

#define BNXT_RX_STATS_EXT_ENTRY(counter)	\
	{ BNXT_RX_STATS_EXT_OFFSET(counter), __stringify(counter) }

#define BNXT_TX_STATS_EXT_ENTRY(counter)	\
	{ BNXT_TX_STATS_EXT_OFFSET(counter), __stringify(counter) }

#define BNXT_RX_STATS_EXT_PFC_ENTRY(n)				\
	BNXT_RX_STATS_EXT_ENTRY(pfc_pri##n##_rx_duration_us),	\
	BNXT_RX_STATS_EXT_ENTRY(pfc_pri##n##_rx_transitions)

#define BNXT_TX_STATS_EXT_PFC_ENTRY(n)				\
	BNXT_TX_STATS_EXT_ENTRY(pfc_pri##n##_tx_duration_us),	\
	BNXT_TX_STATS_EXT_ENTRY(pfc_pri##n##_tx_transitions)

#define BNXT_RX_STATS_EXT_PFC_ENTRIES				\
	BNXT_RX_STATS_EXT_PFC_ENTRY(0),				\
	BNXT_RX_STATS_EXT_PFC_ENTRY(1),				\
	BNXT_RX_STATS_EXT_PFC_ENTRY(2),				\
	BNXT_RX_STATS_EXT_PFC_ENTRY(3),				\
	BNXT_RX_STATS_EXT_PFC_ENTRY(4),				\
	BNXT_RX_STATS_EXT_PFC_ENTRY(5),				\
	BNXT_RX_STATS_EXT_PFC_ENTRY(6),				\
	BNXT_RX_STATS_EXT_PFC_ENTRY(7)

#define BNXT_TX_STATS_EXT_PFC_ENTRIES				\
	BNXT_TX_STATS_EXT_PFC_ENTRY(0),				\
	BNXT_TX_STATS_EXT_PFC_ENTRY(1),				\
	BNXT_TX_STATS_EXT_PFC_ENTRY(2),				\
	BNXT_TX_STATS_EXT_PFC_ENTRY(3),				\
	BNXT_TX_STATS_EXT_PFC_ENTRY(4),				\
	BNXT_TX_STATS_EXT_PFC_ENTRY(5),				\
	BNXT_TX_STATS_EXT_PFC_ENTRY(6),				\
	BNXT_TX_STATS_EXT_PFC_ENTRY(7)

#define BNXT_RX_STATS_EXT_COS_ENTRY(n)				\
	BNXT_RX_STATS_EXT_ENTRY(rx_bytes_cos##n),		\
	BNXT_RX_STATS_EXT_ENTRY(rx_packets_cos##n)

#define BNXT_TX_STATS_EXT_COS_ENTRY(n)				\
	BNXT_TX_STATS_EXT_ENTRY(tx_bytes_cos##n),		\
	BNXT_TX_STATS_EXT_ENTRY(tx_packets_cos##n)

#define BNXT_RX_STATS_EXT_COS_ENTRIES				\
	BNXT_RX_STATS_EXT_COS_ENTRY(0),				\
	BNXT_RX_STATS_EXT_COS_ENTRY(1),				\
	BNXT_RX_STATS_EXT_COS_ENTRY(2),				\
	BNXT_RX_STATS_EXT_COS_ENTRY(3),				\
	BNXT_RX_STATS_EXT_COS_ENTRY(4),				\
	BNXT_RX_STATS_EXT_COS_ENTRY(5),				\
	BNXT_RX_STATS_EXT_COS_ENTRY(6),				\
	BNXT_RX_STATS_EXT_COS_ENTRY(7)				\

#define BNXT_TX_STATS_EXT_COS_ENTRIES				\
	BNXT_TX_STATS_EXT_COS_ENTRY(0),				\
	BNXT_TX_STATS_EXT_COS_ENTRY(1),				\
	BNXT_TX_STATS_EXT_COS_ENTRY(2),				\
	BNXT_TX_STATS_EXT_COS_ENTRY(3),				\
	BNXT_TX_STATS_EXT_COS_ENTRY(4),				\
	BNXT_TX_STATS_EXT_COS_ENTRY(5),				\
	BNXT_TX_STATS_EXT_COS_ENTRY(6),				\
	BNXT_TX_STATS_EXT_COS_ENTRY(7)				\

#define BNXT_RX_STATS_EXT_DISCARD_COS_ENTRY(n)			\
	BNXT_RX_STATS_EXT_ENTRY(rx_discard_bytes_cos##n),	\
	BNXT_RX_STATS_EXT_ENTRY(rx_discard_packets_cos##n)

#define BNXT_RX_STATS_EXT_DISCARD_COS_ENTRIES				\
	BNXT_RX_STATS_EXT_DISCARD_COS_ENTRY(0),				\
	BNXT_RX_STATS_EXT_DISCARD_COS_ENTRY(1),				\
	BNXT_RX_STATS_EXT_DISCARD_COS_ENTRY(2),				\
	BNXT_RX_STATS_EXT_DISCARD_COS_ENTRY(3),				\
	BNXT_RX_STATS_EXT_DISCARD_COS_ENTRY(4),				\
	BNXT_RX_STATS_EXT_DISCARD_COS_ENTRY(5),				\
	BNXT_RX_STATS_EXT_DISCARD_COS_ENTRY(6),				\
	BNXT_RX_STATS_EXT_DISCARD_COS_ENTRY(7)

#define BNXT_RX_STATS_PRI_ENTRY(counter, n)		\
	{ BNXT_RX_STATS_EXT_OFFSET(counter##_cos0),	\
	  __stringify(counter##_pri##n) }

#define BNXT_TX_STATS_PRI_ENTRY(counter, n)		\
	{ BNXT_TX_STATS_EXT_OFFSET(counter##_cos0),	\
	  __stringify(counter##_pri##n) }

#define BNXT_RX_STATS_PRI_ENTRIES(counter)		\
	BNXT_RX_STATS_PRI_ENTRY(counter, 0),		\
	BNXT_RX_STATS_PRI_ENTRY(counter, 1),		\
	BNXT_RX_STATS_PRI_ENTRY(counter, 2),		\
	BNXT_RX_STATS_PRI_ENTRY(counter, 3),		\
	BNXT_RX_STATS_PRI_ENTRY(counter, 4),		\
	BNXT_RX_STATS_PRI_ENTRY(counter, 5),		\
	BNXT_RX_STATS_PRI_ENTRY(counter, 6),		\
	BNXT_RX_STATS_PRI_ENTRY(counter, 7)

#define BNXT_TX_STATS_PRI_ENTRIES(counter)		\
	BNXT_TX_STATS_PRI_ENTRY(counter, 0),		\
	BNXT_TX_STATS_PRI_ENTRY(counter, 1),		\
	BNXT_TX_STATS_PRI_ENTRY(counter, 2),		\
	BNXT_TX_STATS_PRI_ENTRY(counter, 3),		\
	BNXT_TX_STATS_PRI_ENTRY(counter, 4),		\
	BNXT_TX_STATS_PRI_ENTRY(counter, 5),		\
	BNXT_TX_STATS_PRI_ENTRY(counter, 6),		\
	BNXT_TX_STATS_PRI_ENTRY(counter, 7)

enum {
	RX_TOTAL_DISCARDS,
	TX_TOTAL_DISCARDS,
	RX_NETPOLL_DISCARDS,
};

static struct {
	u64			counter;
	char			string[ETH_GSTRING_LEN];
} bnxt_sw_func_stats[] = {
	{0, "rx_total_discard_pkts"},
	{0, "tx_total_discard_pkts"},
	{0, "rx_total_netpoll_discards"},
};

#define NUM_RING_RX_SW_STATS		ARRAY_SIZE(bnxt_rx_sw_stats_str)
#define NUM_RING_CMN_SW_STATS		ARRAY_SIZE(bnxt_cmn_sw_stats_str)
#define NUM_RING_RX_HW_STATS		ARRAY_SIZE(bnxt_ring_rx_stats_str)
#define NUM_RING_TX_HW_STATS		ARRAY_SIZE(bnxt_ring_tx_stats_str)

static const struct {
	long offset;
	char string[ETH_GSTRING_LEN];
} bnxt_port_stats_arr[] = {
	BNXT_RX_STATS_ENTRY(rx_64b_frames),
	BNXT_RX_STATS_ENTRY(rx_65b_127b_frames),
	BNXT_RX_STATS_ENTRY(rx_128b_255b_frames),
	BNXT_RX_STATS_ENTRY(rx_256b_511b_frames),
	BNXT_RX_STATS_ENTRY(rx_512b_1023b_frames),
	BNXT_RX_STATS_ENTRY(rx_1024b_1518b_frames),
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
	BNXT_RX_STATS_ENTRY(rx_stat_discard),
	BNXT_RX_STATS_ENTRY(rx_stat_err),

	BNXT_TX_STATS_ENTRY(tx_64b_frames),
	BNXT_TX_STATS_ENTRY(tx_65b_127b_frames),
	BNXT_TX_STATS_ENTRY(tx_128b_255b_frames),
	BNXT_TX_STATS_ENTRY(tx_256b_511b_frames),
	BNXT_TX_STATS_ENTRY(tx_512b_1023b_frames),
	BNXT_TX_STATS_ENTRY(tx_1024b_1518b_frames),
	BNXT_TX_STATS_ENTRY(tx_good_vlan_frames),
	BNXT_TX_STATS_ENTRY(tx_1519b_2047b_frames),
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
	BNXT_TX_STATS_ENTRY(tx_xthol_frames),
	BNXT_TX_STATS_ENTRY(tx_stat_discard),
	BNXT_TX_STATS_ENTRY(tx_stat_error),
};

static const struct {
	long offset;
	char string[ETH_GSTRING_LEN];
} bnxt_port_stats_ext_arr[] = {
	BNXT_RX_STATS_EXT_ENTRY(link_down_events),
	BNXT_RX_STATS_EXT_ENTRY(continuous_pause_events),
	BNXT_RX_STATS_EXT_ENTRY(resume_pause_events),
	BNXT_RX_STATS_EXT_ENTRY(continuous_roce_pause_events),
	BNXT_RX_STATS_EXT_ENTRY(resume_roce_pause_events),
	BNXT_RX_STATS_EXT_COS_ENTRIES,
	BNXT_RX_STATS_EXT_PFC_ENTRIES,
	BNXT_RX_STATS_EXT_ENTRY(rx_bits),
	BNXT_RX_STATS_EXT_ENTRY(rx_buffer_passed_threshold),
	BNXT_RX_STATS_EXT_ENTRY(rx_pcs_symbol_err),
	BNXT_RX_STATS_EXT_ENTRY(rx_corrected_bits),
	BNXT_RX_STATS_EXT_DISCARD_COS_ENTRIES,
	BNXT_RX_STATS_EXT_ENTRY(rx_fec_corrected_blocks),
	BNXT_RX_STATS_EXT_ENTRY(rx_fec_uncorrectable_blocks),
};

static const struct {
	long offset;
	char string[ETH_GSTRING_LEN];
} bnxt_tx_port_stats_ext_arr[] = {
	BNXT_TX_STATS_EXT_COS_ENTRIES,
	BNXT_TX_STATS_EXT_PFC_ENTRIES,
};

static const struct {
	long base_off;
	char string[ETH_GSTRING_LEN];
} bnxt_rx_bytes_pri_arr[] = {
	BNXT_RX_STATS_PRI_ENTRIES(rx_bytes),
};

static const struct {
	long base_off;
	char string[ETH_GSTRING_LEN];
} bnxt_rx_pkts_pri_arr[] = {
	BNXT_RX_STATS_PRI_ENTRIES(rx_packets),
};

static const struct {
	long base_off;
	char string[ETH_GSTRING_LEN];
} bnxt_tx_bytes_pri_arr[] = {
	BNXT_TX_STATS_PRI_ENTRIES(tx_bytes),
};

static const struct {
	long base_off;
	char string[ETH_GSTRING_LEN];
} bnxt_tx_pkts_pri_arr[] = {
	BNXT_TX_STATS_PRI_ENTRIES(tx_packets),
};

#define BNXT_NUM_SW_FUNC_STATS	ARRAY_SIZE(bnxt_sw_func_stats)
#define BNXT_NUM_PORT_STATS ARRAY_SIZE(bnxt_port_stats_arr)
#define BNXT_NUM_STATS_PRI			\
	(ARRAY_SIZE(bnxt_rx_bytes_pri_arr) +	\
	 ARRAY_SIZE(bnxt_rx_pkts_pri_arr) +	\
	 ARRAY_SIZE(bnxt_tx_bytes_pri_arr) +	\
	 ARRAY_SIZE(bnxt_tx_pkts_pri_arr))

static int bnxt_get_num_tpa_ring_stats(struct bnxt *bp)
{
	if (BNXT_SUPPORTS_TPA(bp)) {
		if (bp->max_tpa_v2) {
			if (BNXT_CHIP_P5_THOR(bp))
				return BNXT_NUM_TPA_RING_STATS_P5;
			return BNXT_NUM_TPA_RING_STATS_P5_SR2;
		}
		return BNXT_NUM_TPA_RING_STATS;
	}
	return 0;
}

static int bnxt_get_num_ring_stats(struct bnxt *bp)
{
	int rx, tx, cmn;

	rx = NUM_RING_RX_HW_STATS + NUM_RING_RX_SW_STATS +
	     bnxt_get_num_tpa_ring_stats(bp);
	tx = NUM_RING_TX_HW_STATS;
	cmn = NUM_RING_CMN_SW_STATS;
	return rx * bp->rx_nr_rings + tx * bp->tx_nr_rings +
	       cmn * bp->cp_nr_rings;
}

static int bnxt_get_num_stats(struct bnxt *bp)
{
	int num_stats = bnxt_get_num_ring_stats(bp);

	num_stats += BNXT_NUM_SW_FUNC_STATS;

	if (bp->flags & BNXT_FLAG_PORT_STATS)
		num_stats += BNXT_NUM_PORT_STATS;

	if (bp->flags & BNXT_FLAG_PORT_STATS_EXT) {
		num_stats += bp->fw_rx_stats_ext_size +
			     bp->fw_tx_stats_ext_size;
		if (bp->pri2cos_valid)
			num_stats += BNXT_NUM_STATS_PRI;
	}

	return num_stats;
}

static int bnxt_get_sset_count(struct net_device *dev, int sset)
{
	struct bnxt *bp = netdev_priv(dev);

	switch (sset) {
	case ETH_SS_STATS:
		return bnxt_get_num_stats(bp);
	case ETH_SS_TEST:
		if (!bp->num_tests)
			return -EOPNOTSUPP;
		return bp->num_tests;
	default:
		return -EOPNOTSUPP;
	}
}

static bool is_rx_ring(struct bnxt *bp, int ring_num)
{
	return ring_num < bp->rx_nr_rings;
}

static bool is_tx_ring(struct bnxt *bp, int ring_num)
{
	int tx_base = 0;

	if (!(bp->flags & BNXT_FLAG_SHARED_RINGS))
		tx_base = bp->rx_nr_rings;

	if (ring_num >= tx_base && ring_num < (tx_base + bp->tx_nr_rings))
		return true;
	return false;
}

static void bnxt_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *stats, u64 *buf)
{
	u32 i, j = 0;
	struct bnxt *bp = netdev_priv(dev);
	u32 tpa_stats;

	if (!bp->bnapi) {
		j += bnxt_get_num_ring_stats(bp) + BNXT_NUM_SW_FUNC_STATS;
		goto skip_ring_stats;
	}

	for (i = 0; i < BNXT_NUM_SW_FUNC_STATS; i++)
		bnxt_sw_func_stats[i].counter = 0;

	tpa_stats = bnxt_get_num_tpa_ring_stats(bp);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
		u64 *sw_stats = cpr->stats.sw_stats;
		u64 *sw;
		int k;

		if (is_rx_ring(bp, i)) {
			for (k = 0; k < NUM_RING_RX_HW_STATS; j++, k++)
				buf[j] = sw_stats[k];
		}
		if (is_tx_ring(bp, i)) {
			k = NUM_RING_RX_HW_STATS;
			for (; k < NUM_RING_RX_HW_STATS + NUM_RING_TX_HW_STATS;
			       j++, k++)
				buf[j] = sw_stats[k];
		}
		if (!tpa_stats || !is_rx_ring(bp, i))
			goto skip_tpa_ring_stats;

		k = NUM_RING_RX_HW_STATS + NUM_RING_TX_HW_STATS;
		for (; k < NUM_RING_RX_HW_STATS + NUM_RING_TX_HW_STATS +
			   tpa_stats; j++, k++)
			buf[j] = sw_stats[k];

skip_tpa_ring_stats:
		sw = (u64 *)&cpr->sw_stats.rx;
		if (is_rx_ring(bp, i)) {
			for (k = 0; k < NUM_RING_RX_SW_STATS; j++, k++)
				buf[j] = sw[k];
		}

		sw = (u64 *)&cpr->sw_stats.cmn;
		for (k = 0; k < NUM_RING_CMN_SW_STATS; j++, k++)
			buf[j] = sw[k];

		bnxt_sw_func_stats[RX_TOTAL_DISCARDS].counter +=
			BNXT_GET_RING_STATS64(sw_stats, rx_discard_pkts);
		bnxt_sw_func_stats[TX_TOTAL_DISCARDS].counter +=
			BNXT_GET_RING_STATS64(sw_stats, tx_discard_pkts);
		bnxt_sw_func_stats[RX_NETPOLL_DISCARDS].counter +=
			cpr->sw_stats.rx.rx_netpoll_discards;
	}

	for (i = 0; i < BNXT_NUM_SW_FUNC_STATS; i++, j++)
		buf[j] = bnxt_sw_func_stats[i].counter;

skip_ring_stats:
	if (bp->flags & BNXT_FLAG_PORT_STATS) {
		u64 *port_stats = bp->port_stats.sw_stats;

		for (i = 0; i < BNXT_NUM_PORT_STATS; i++, j++)
			buf[j] = *(port_stats + bnxt_port_stats_arr[i].offset);
	}
	if (bp->flags & BNXT_FLAG_PORT_STATS_EXT) {
		u64 *rx_port_stats_ext = bp->rx_port_stats_ext.sw_stats;
		u64 *tx_port_stats_ext = bp->tx_port_stats_ext.sw_stats;

		for (i = 0; i < bp->fw_rx_stats_ext_size; i++, j++) {
			buf[j] = *(rx_port_stats_ext +
				   bnxt_port_stats_ext_arr[i].offset);
		}
		for (i = 0; i < bp->fw_tx_stats_ext_size; i++, j++) {
			buf[j] = *(tx_port_stats_ext +
				   bnxt_tx_port_stats_ext_arr[i].offset);
		}
		if (bp->pri2cos_valid) {
			for (i = 0; i < 8; i++, j++) {
				long n = bnxt_rx_bytes_pri_arr[i].base_off +
					 bp->pri2cos_idx[i];

				buf[j] = *(rx_port_stats_ext + n);
			}
			for (i = 0; i < 8; i++, j++) {
				long n = bnxt_rx_pkts_pri_arr[i].base_off +
					 bp->pri2cos_idx[i];

				buf[j] = *(rx_port_stats_ext + n);
			}
			for (i = 0; i < 8; i++, j++) {
				long n = bnxt_tx_bytes_pri_arr[i].base_off +
					 bp->pri2cos_idx[i];

				buf[j] = *(tx_port_stats_ext + n);
			}
			for (i = 0; i < 8; i++, j++) {
				long n = bnxt_tx_pkts_pri_arr[i].base_off +
					 bp->pri2cos_idx[i];

				buf[j] = *(tx_port_stats_ext + n);
			}
		}
	}
}

static void bnxt_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	struct bnxt *bp = netdev_priv(dev);
	static const char * const *str;
	u32 i, j, num_str;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < bp->cp_nr_rings; i++) {
			if (is_rx_ring(bp, i)) {
				num_str = NUM_RING_RX_HW_STATS;
				for (j = 0; j < num_str; j++) {
					sprintf(buf, "[%d]: %s", i,
						bnxt_ring_rx_stats_str[j]);
					buf += ETH_GSTRING_LEN;
				}
			}
			if (is_tx_ring(bp, i)) {
				num_str = NUM_RING_TX_HW_STATS;
				for (j = 0; j < num_str; j++) {
					sprintf(buf, "[%d]: %s", i,
						bnxt_ring_tx_stats_str[j]);
					buf += ETH_GSTRING_LEN;
				}
			}
			num_str = bnxt_get_num_tpa_ring_stats(bp);
			if (!num_str || !is_rx_ring(bp, i))
				goto skip_tpa_stats;

			if (bp->max_tpa_v2)
				str = bnxt_ring_tpa2_stats_str;
			else
				str = bnxt_ring_tpa_stats_str;

			for (j = 0; j < num_str; j++) {
				sprintf(buf, "[%d]: %s", i, str[j]);
				buf += ETH_GSTRING_LEN;
			}
skip_tpa_stats:
			if (is_rx_ring(bp, i)) {
				num_str = NUM_RING_RX_SW_STATS;
				for (j = 0; j < num_str; j++) {
					sprintf(buf, "[%d]: %s", i,
						bnxt_rx_sw_stats_str[j]);
					buf += ETH_GSTRING_LEN;
				}
			}
			num_str = NUM_RING_CMN_SW_STATS;
			for (j = 0; j < num_str; j++) {
				sprintf(buf, "[%d]: %s", i,
					bnxt_cmn_sw_stats_str[j]);
				buf += ETH_GSTRING_LEN;
			}
		}
		for (i = 0; i < BNXT_NUM_SW_FUNC_STATS; i++) {
			strcpy(buf, bnxt_sw_func_stats[i].string);
			buf += ETH_GSTRING_LEN;
		}

		if (bp->flags & BNXT_FLAG_PORT_STATS) {
			for (i = 0; i < BNXT_NUM_PORT_STATS; i++) {
				strcpy(buf, bnxt_port_stats_arr[i].string);
				buf += ETH_GSTRING_LEN;
			}
		}
		if (bp->flags & BNXT_FLAG_PORT_STATS_EXT) {
			for (i = 0; i < bp->fw_rx_stats_ext_size; i++) {
				strcpy(buf, bnxt_port_stats_ext_arr[i].string);
				buf += ETH_GSTRING_LEN;
			}
			for (i = 0; i < bp->fw_tx_stats_ext_size; i++) {
				strcpy(buf,
				       bnxt_tx_port_stats_ext_arr[i].string);
				buf += ETH_GSTRING_LEN;
			}
			if (bp->pri2cos_valid) {
				for (i = 0; i < 8; i++) {
					strcpy(buf,
					       bnxt_rx_bytes_pri_arr[i].string);
					buf += ETH_GSTRING_LEN;
				}
				for (i = 0; i < 8; i++) {
					strcpy(buf,
					       bnxt_rx_pkts_pri_arr[i].string);
					buf += ETH_GSTRING_LEN;
				}
				for (i = 0; i < 8; i++) {
					strcpy(buf,
					       bnxt_tx_bytes_pri_arr[i].string);
					buf += ETH_GSTRING_LEN;
				}
				for (i = 0; i < 8; i++) {
					strcpy(buf,
					       bnxt_tx_pkts_pri_arr[i].string);
					buf += ETH_GSTRING_LEN;
				}
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
			       struct ethtool_ringparam *ering,
			       struct kernel_ethtool_ringparam *kernel_ering,
			       struct netlink_ext_ack *extack)
{
	struct bnxt *bp = netdev_priv(dev);

	if (bp->flags & BNXT_FLAG_AGG_RINGS) {
		ering->rx_max_pending = BNXT_MAX_RX_DESC_CNT_JUM_ENA;
		ering->rx_jumbo_max_pending = BNXT_MAX_RX_JUM_DESC_CNT;
	} else {
		ering->rx_max_pending = BNXT_MAX_RX_DESC_CNT;
		ering->rx_jumbo_max_pending = 0;
	}
	ering->tx_max_pending = BNXT_MAX_TX_DESC_CNT;

	ering->rx_pending = bp->rx_ring_size;
	ering->rx_jumbo_pending = bp->rx_agg_ring_size;
	ering->tx_pending = bp->tx_ring_size;
}

static int bnxt_set_ringparam(struct net_device *dev,
			      struct ethtool_ringparam *ering,
			      struct kernel_ethtool_ringparam *kernel_ering,
			      struct netlink_ext_ack *extack)
{
	struct bnxt *bp = netdev_priv(dev);

	if ((ering->rx_pending > BNXT_MAX_RX_DESC_CNT) ||
	    (ering->tx_pending > BNXT_MAX_TX_DESC_CNT) ||
	    (ering->tx_pending < BNXT_MIN_TX_DESC_CNT))
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
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;
	int max_rx_rings, max_tx_rings, tcs;
	int max_tx_sch_inputs, tx_grps;

	/* Get the most up-to-date max_tx_sch_inputs. */
	if (netif_running(dev) && BNXT_NEW_RM(bp))
		bnxt_hwrm_func_resc_qcaps(bp, false);
	max_tx_sch_inputs = hw_resc->max_tx_sch_inputs;

	bnxt_get_max_rings(bp, &max_rx_rings, &max_tx_rings, true);
	if (max_tx_sch_inputs)
		max_tx_rings = min_t(int, max_tx_rings, max_tx_sch_inputs);

	tcs = netdev_get_num_tc(dev);
	tx_grps = max(tcs, 1);
	if (bp->tx_nr_rings_xdp)
		tx_grps++;
	max_tx_rings /= tx_grps;
	channel->max_combined = min_t(int, max_rx_rings, max_tx_rings);

	if (bnxt_get_max_rings(bp, &max_rx_rings, &max_tx_rings, false)) {
		max_rx_rings = 0;
		max_tx_rings = 0;
	}
	if (max_tx_sch_inputs)
		max_tx_rings = min_t(int, max_tx_rings, max_tx_sch_inputs);

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
	rc = bnxt_check_rings(bp, req_tx_rings, req_rx_rings, sh, tcs, tx_xdp);
	if (rc) {
		netdev_warn(dev, "Unable to allocate the requested rings\n");
		return rc;
	}

	if (bnxt_get_nr_rss_ctxs(bp, req_rx_rings) !=
	    bnxt_get_nr_rss_ctxs(bp, bp->rx_nr_rings) &&
	    netif_is_rxfh_configured(dev)) {
		netdev_warn(dev, "RSS table size change required, RSS table entries must be default to proceed\n");
		return -EINVAL;
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

	/* After changing number of rx channels, update NTUPLE feature. */
	netdev_update_features(dev);
	if (netif_running(dev)) {
		rc = bnxt_open_nic(bp, true, false);
		if ((!rc) && BNXT_PF(bp)) {
			/* TODO CHIMP_FW: Send message to all VF's
			 * to renable
			 */
		}
	} else {
		rc = bnxt_reserve_rings(bp, true);
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

	if (fs->location >= BNXT_NTP_FLTR_MAX_FLTR)
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
		fallthrough;
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
		fallthrough;
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

u32 bnxt_get_rxfh_indir_size(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);

	if (bp->flags & BNXT_FLAG_CHIP_P5)
		return ALIGN(bp->rx_nr_rings, BNXT_RSS_TABLE_ENTRIES_P5);
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
	struct bnxt_vnic_info *vnic;
	u32 i, tbl_size;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	if (!bp->vnic_info)
		return 0;

	vnic = &bp->vnic_info[0];
	if (indir && bp->rss_indir_tbl) {
		tbl_size = bnxt_get_rxfh_indir_size(dev);
		for (i = 0; i < tbl_size; i++)
			indir[i] = bp->rss_indir_tbl[i];
	}

	if (key && vnic->rss_hash_key)
		memcpy(key, vnic->rss_hash_key, HW_HASH_KEY_SIZE);

	return 0;
}

static int bnxt_set_rxfh(struct net_device *dev, const u32 *indir,
			 const u8 *key, const u8 hfunc)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc = 0;

	if (hfunc && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	if (key)
		return -EOPNOTSUPP;

	if (indir) {
		u32 i, pad, tbl_size = bnxt_get_rxfh_indir_size(dev);

		for (i = 0; i < tbl_size; i++)
			bp->rss_indir_tbl[i] = indir[i];
		pad = bp->rss_indir_tbl_entries - tbl_size;
		if (pad)
			memset(&bp->rss_indir_tbl[i], 0, pad * sizeof(u16));
	}

	if (netif_running(bp->dev)) {
		bnxt_close_nic(bp, false, false);
		rc = bnxt_open_nic(bp, false, false);
	}
	return rc;
}

static void bnxt_get_drvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	struct bnxt *bp = netdev_priv(dev);

	strlcpy(info->driver, DRV_MODULE_NAME, sizeof(info->driver));
	strlcpy(info->fw_version, bp->fw_ver_str, sizeof(info->fw_version));
	strlcpy(info->bus_info, pci_name(bp->pdev), sizeof(info->bus_info));
	info->n_stats = bnxt_get_num_stats(bp);
	info->testinfo_len = bp->num_tests;
	/* TODO CHIMP_FW: eeprom dump details */
	info->eedump_len = 0;
	/* TODO CHIMP FW: reg dump details */
	info->regdump_len = 0;
}

static int bnxt_get_regs_len(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);
	int reg_len;

	if (!BNXT_PF(bp))
		return -EOPNOTSUPP;

	reg_len = BNXT_PXP_REG_LEN;

	if (bp->fw_cap & BNXT_FW_CAP_PCIE_STATS_SUPPORTED)
		reg_len += sizeof(struct pcie_ctx_hw_stats);

	return reg_len;
}

static void bnxt_get_regs(struct net_device *dev, struct ethtool_regs *regs,
			  void *_p)
{
	struct pcie_ctx_hw_stats *hw_pcie_stats;
	struct hwrm_pcie_qstats_input *req;
	struct bnxt *bp = netdev_priv(dev);
	dma_addr_t hw_pcie_stats_addr;
	int rc;

	regs->version = 0;
	bnxt_dbg_hwrm_rd_reg(bp, 0, BNXT_PXP_REG_LEN / 4, _p);

	if (!(bp->fw_cap & BNXT_FW_CAP_PCIE_STATS_SUPPORTED))
		return;

	if (hwrm_req_init(bp, req, HWRM_PCIE_QSTATS))
		return;

	hw_pcie_stats = hwrm_req_dma_slice(bp, req, sizeof(*hw_pcie_stats),
					   &hw_pcie_stats_addr);
	if (!hw_pcie_stats) {
		hwrm_req_drop(bp, req);
		return;
	}

	regs->version = 1;
	hwrm_req_hold(bp, req); /* hold on to slice */
	req->pcie_stat_size = cpu_to_le16(sizeof(*hw_pcie_stats));
	req->pcie_stat_host_addr = cpu_to_le64(hw_pcie_stats_addr);
	rc = hwrm_req_send(bp, req);
	if (!rc) {
		__le64 *src = (__le64 *)hw_pcie_stats;
		u64 *dst = (u64 *)(_p + BNXT_PXP_REG_LEN);
		int i;

		for (i = 0; i < sizeof(*hw_pcie_stats) / sizeof(__le64); i++)
			dst[i] = le64_to_cpu(src[i]);
	}
	hwrm_req_drop(bp, req);
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

#define BNXT_FW_TO_ETHTOOL_PAM4_SPDS(fw_speeds, lk_ksettings, name)	\
{									\
	if ((fw_speeds) & BNXT_LINK_PAM4_SPEED_MSK_50GB)		\
		ethtool_link_ksettings_add_link_mode(lk_ksettings, name,\
						     50000baseCR_Full);	\
	if ((fw_speeds) & BNXT_LINK_PAM4_SPEED_MSK_100GB)		\
		ethtool_link_ksettings_add_link_mode(lk_ksettings, name,\
						     100000baseCR2_Full);\
	if ((fw_speeds) & BNXT_LINK_PAM4_SPEED_MSK_200GB)		\
		ethtool_link_ksettings_add_link_mode(lk_ksettings, name,\
						     200000baseCR4_Full);\
}

#define BNXT_ETHTOOL_TO_FW_PAM4_SPDS(fw_speeds, lk_ksettings, name)	\
{									\
	if (ethtool_link_ksettings_test_link_mode(lk_ksettings, name,	\
						  50000baseCR_Full))	\
		(fw_speeds) |= BNXT_LINK_PAM4_SPEED_MSK_50GB;		\
	if (ethtool_link_ksettings_test_link_mode(lk_ksettings, name,	\
						  100000baseCR2_Full))	\
		(fw_speeds) |= BNXT_LINK_PAM4_SPEED_MSK_100GB;		\
	if (ethtool_link_ksettings_test_link_mode(lk_ksettings, name,	\
						  200000baseCR4_Full))	\
		(fw_speeds) |= BNXT_LINK_PAM4_SPEED_MSK_200GB;		\
}

static void bnxt_fw_to_ethtool_advertised_fec(struct bnxt_link_info *link_info,
				struct ethtool_link_ksettings *lk_ksettings)
{
	u16 fec_cfg = link_info->fec_cfg;

	if ((fec_cfg & BNXT_FEC_NONE) || !(fec_cfg & BNXT_FEC_AUTONEG)) {
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_NONE_BIT,
				 lk_ksettings->link_modes.advertising);
		return;
	}
	if (fec_cfg & BNXT_FEC_ENC_BASE_R)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_BASER_BIT,
				 lk_ksettings->link_modes.advertising);
	if (fec_cfg & BNXT_FEC_ENC_RS)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_RS_BIT,
				 lk_ksettings->link_modes.advertising);
	if (fec_cfg & BNXT_FEC_ENC_LLRS)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_LLRS_BIT,
				 lk_ksettings->link_modes.advertising);
}

static void bnxt_fw_to_ethtool_advertised_spds(struct bnxt_link_info *link_info,
				struct ethtool_link_ksettings *lk_ksettings)
{
	u16 fw_speeds = link_info->advertising;
	u8 fw_pause = 0;

	if (link_info->autoneg & BNXT_AUTONEG_FLOW_CTRL)
		fw_pause = link_info->auto_pause_setting;

	BNXT_FW_TO_ETHTOOL_SPDS(fw_speeds, fw_pause, lk_ksettings, advertising);
	fw_speeds = link_info->advertising_pam4;
	BNXT_FW_TO_ETHTOOL_PAM4_SPDS(fw_speeds, lk_ksettings, advertising);
	bnxt_fw_to_ethtool_advertised_fec(link_info, lk_ksettings);
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
	fw_speeds = link_info->lp_auto_pam4_link_speeds;
	BNXT_FW_TO_ETHTOOL_PAM4_SPDS(fw_speeds, lk_ksettings, lp_advertising);
}

static void bnxt_fw_to_ethtool_support_fec(struct bnxt_link_info *link_info,
				struct ethtool_link_ksettings *lk_ksettings)
{
	u16 fec_cfg = link_info->fec_cfg;

	if (fec_cfg & BNXT_FEC_NONE) {
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_NONE_BIT,
				 lk_ksettings->link_modes.supported);
		return;
	}
	if (fec_cfg & BNXT_FEC_ENC_BASE_R_CAP)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_BASER_BIT,
				 lk_ksettings->link_modes.supported);
	if (fec_cfg & BNXT_FEC_ENC_RS_CAP)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_RS_BIT,
				 lk_ksettings->link_modes.supported);
	if (fec_cfg & BNXT_FEC_ENC_LLRS_CAP)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_LLRS_BIT,
				 lk_ksettings->link_modes.supported);
}

static void bnxt_fw_to_ethtool_support_spds(struct bnxt_link_info *link_info,
				struct ethtool_link_ksettings *lk_ksettings)
{
	u16 fw_speeds = link_info->support_speeds;

	BNXT_FW_TO_ETHTOOL_SPDS(fw_speeds, 0, lk_ksettings, supported);
	fw_speeds = link_info->support_pam4_speeds;
	BNXT_FW_TO_ETHTOOL_PAM4_SPDS(fw_speeds, lk_ksettings, supported);

	ethtool_link_ksettings_add_link_mode(lk_ksettings, supported, Pause);
	ethtool_link_ksettings_add_link_mode(lk_ksettings, supported,
					     Asym_Pause);

	if (link_info->support_auto_speeds ||
	    link_info->support_pam4_auto_speeds)
		ethtool_link_ksettings_add_link_mode(lk_ksettings, supported,
						     Autoneg);
	bnxt_fw_to_ethtool_support_fec(link_info, lk_ksettings);
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
	mutex_lock(&bp->link_lock);
	bnxt_fw_to_ethtool_support_spds(link_info, lk_ksettings);

	ethtool_link_ksettings_zero_link_mode(lk_ksettings, advertising);
	if (link_info->autoneg) {
		bnxt_fw_to_ethtool_advertised_spds(link_info, lk_ksettings);
		ethtool_link_ksettings_add_link_mode(lk_ksettings,
						     advertising, Autoneg);
		base->autoneg = AUTONEG_ENABLE;
		base->duplex = DUPLEX_UNKNOWN;
		if (link_info->phy_link_status == BNXT_LINK_LINK) {
			bnxt_fw_to_ethtool_lp_adv(link_info, lk_ksettings);
			if (link_info->duplex & BNXT_LINK_DUPLEX_FULL)
				base->duplex = DUPLEX_FULL;
			else
				base->duplex = DUPLEX_HALF;
		}
		ethtool_speed = bnxt_fw_to_ethtool_speed(link_info->link_speed);
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
	mutex_unlock(&bp->link_lock);

	return 0;
}

static int bnxt_force_link_speed(struct net_device *dev, u32 ethtool_speed)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_link_info *link_info = &bp->link_info;
	u16 support_pam4_spds = link_info->support_pam4_speeds;
	u16 support_spds = link_info->support_speeds;
	u8 sig_mode = BNXT_SIG_MODE_NRZ;
	u16 fw_speed = 0;

	switch (ethtool_speed) {
	case SPEED_100:
		if (support_spds & BNXT_LINK_SPEED_MSK_100MB)
			fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_100MB;
		break;
	case SPEED_1000:
		if (support_spds & BNXT_LINK_SPEED_MSK_1GB)
			fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_1GB;
		break;
	case SPEED_2500:
		if (support_spds & BNXT_LINK_SPEED_MSK_2_5GB)
			fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_2_5GB;
		break;
	case SPEED_10000:
		if (support_spds & BNXT_LINK_SPEED_MSK_10GB)
			fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_10GB;
		break;
	case SPEED_20000:
		if (support_spds & BNXT_LINK_SPEED_MSK_20GB)
			fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_20GB;
		break;
	case SPEED_25000:
		if (support_spds & BNXT_LINK_SPEED_MSK_25GB)
			fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_25GB;
		break;
	case SPEED_40000:
		if (support_spds & BNXT_LINK_SPEED_MSK_40GB)
			fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_40GB;
		break;
	case SPEED_50000:
		if (support_spds & BNXT_LINK_SPEED_MSK_50GB) {
			fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_50GB;
		} else if (support_pam4_spds & BNXT_LINK_PAM4_SPEED_MSK_50GB) {
			fw_speed = PORT_PHY_CFG_REQ_FORCE_PAM4_LINK_SPEED_50GB;
			sig_mode = BNXT_SIG_MODE_PAM4;
		}
		break;
	case SPEED_100000:
		if (support_spds & BNXT_LINK_SPEED_MSK_100GB) {
			fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_100GB;
		} else if (support_pam4_spds & BNXT_LINK_PAM4_SPEED_MSK_100GB) {
			fw_speed = PORT_PHY_CFG_REQ_FORCE_PAM4_LINK_SPEED_100GB;
			sig_mode = BNXT_SIG_MODE_PAM4;
		}
		break;
	case SPEED_200000:
		if (support_pam4_spds & BNXT_LINK_PAM4_SPEED_MSK_200GB) {
			fw_speed = PORT_PHY_CFG_REQ_FORCE_PAM4_LINK_SPEED_200GB;
			sig_mode = BNXT_SIG_MODE_PAM4;
		}
		break;
	}

	if (!fw_speed) {
		netdev_err(dev, "unsupported speed!\n");
		return -EINVAL;
	}

	if (link_info->req_link_speed == fw_speed &&
	    link_info->req_signal_mode == sig_mode &&
	    link_info->autoneg == 0)
		return -EALREADY;

	link_info->req_link_speed = fw_speed;
	link_info->req_signal_mode = sig_mode;
	link_info->req_duplex = BNXT_LINK_DUPLEX_FULL;
	link_info->autoneg = 0;
	link_info->advertising = 0;
	link_info->advertising_pam4 = 0;

	return 0;
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
	u32 speed;
	int rc = 0;

	if (!BNXT_PHY_CFG_ABLE(bp))
		return -EOPNOTSUPP;

	mutex_lock(&bp->link_lock);
	if (base->autoneg == AUTONEG_ENABLE) {
		link_info->advertising = 0;
		link_info->advertising_pam4 = 0;
		BNXT_ETHTOOL_TO_FW_SPDS(link_info->advertising, lk_ksettings,
					advertising);
		BNXT_ETHTOOL_TO_FW_PAM4_SPDS(link_info->advertising_pam4,
					     lk_ksettings, advertising);
		link_info->autoneg |= BNXT_AUTONEG_SPEED;
		if (!link_info->advertising && !link_info->advertising_pam4) {
			link_info->advertising = link_info->support_auto_speeds;
			link_info->advertising_pam4 =
				link_info->support_pam4_auto_speeds;
		}
		/* any change to autoneg will cause link change, therefore the
		 * driver should put back the original pause setting in autoneg
		 */
		set_pause = true;
	} else {
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
		rc = bnxt_force_link_speed(dev, speed);
		if (rc) {
			if (rc == -EALREADY)
				rc = 0;
			goto set_setting_exit;
		}
	}

	if (netif_running(dev))
		rc = bnxt_hwrm_set_link_setting(bp, set_pause, false);

set_setting_exit:
	mutex_unlock(&bp->link_lock);
	return rc;
}

static int bnxt_get_fecparam(struct net_device *dev,
			     struct ethtool_fecparam *fec)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_link_info *link_info;
	u8 active_fec;
	u16 fec_cfg;

	link_info = &bp->link_info;
	fec_cfg = link_info->fec_cfg;
	active_fec = link_info->active_fec_sig_mode &
		     PORT_PHY_QCFG_RESP_ACTIVE_FEC_MASK;
	if (fec_cfg & BNXT_FEC_NONE) {
		fec->fec = ETHTOOL_FEC_NONE;
		fec->active_fec = ETHTOOL_FEC_NONE;
		return 0;
	}
	if (fec_cfg & BNXT_FEC_AUTONEG)
		fec->fec |= ETHTOOL_FEC_AUTO;
	if (fec_cfg & BNXT_FEC_ENC_BASE_R)
		fec->fec |= ETHTOOL_FEC_BASER;
	if (fec_cfg & BNXT_FEC_ENC_RS)
		fec->fec |= ETHTOOL_FEC_RS;
	if (fec_cfg & BNXT_FEC_ENC_LLRS)
		fec->fec |= ETHTOOL_FEC_LLRS;

	switch (active_fec) {
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_CLAUSE74_ACTIVE:
		fec->active_fec |= ETHTOOL_FEC_BASER;
		break;
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_CLAUSE91_ACTIVE:
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_RS544_1XN_ACTIVE:
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_RS544_IEEE_ACTIVE:
		fec->active_fec |= ETHTOOL_FEC_RS;
		break;
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_RS272_1XN_ACTIVE:
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_RS272_IEEE_ACTIVE:
		fec->active_fec |= ETHTOOL_FEC_LLRS;
		break;
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_NONE_ACTIVE:
		fec->active_fec |= ETHTOOL_FEC_OFF;
		break;
	}
	return 0;
}

static void bnxt_get_fec_stats(struct net_device *dev,
			       struct ethtool_fec_stats *fec_stats)
{
	struct bnxt *bp = netdev_priv(dev);
	u64 *rx;

	if (BNXT_VF(bp) || !(bp->flags & BNXT_FLAG_PORT_STATS_EXT))
		return;

	rx = bp->rx_port_stats_ext.sw_stats;
	fec_stats->corrected_bits.total =
		*(rx + BNXT_RX_STATS_EXT_OFFSET(rx_corrected_bits));
}

static u32 bnxt_ethtool_forced_fec_to_fw(struct bnxt_link_info *link_info,
					 u32 fec)
{
	u32 fw_fec = PORT_PHY_CFG_REQ_FLAGS_FEC_AUTONEG_DISABLE;

	if (fec & ETHTOOL_FEC_BASER)
		fw_fec |= BNXT_FEC_BASE_R_ON(link_info);
	else if (fec & ETHTOOL_FEC_RS)
		fw_fec |= BNXT_FEC_RS_ON(link_info);
	else if (fec & ETHTOOL_FEC_LLRS)
		fw_fec |= BNXT_FEC_LLRS_ON;
	return fw_fec;
}

static int bnxt_set_fecparam(struct net_device *dev,
			     struct ethtool_fecparam *fecparam)
{
	struct hwrm_port_phy_cfg_input *req;
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_link_info *link_info;
	u32 new_cfg, fec = fecparam->fec;
	u16 fec_cfg;
	int rc;

	link_info = &bp->link_info;
	fec_cfg = link_info->fec_cfg;
	if (fec_cfg & BNXT_FEC_NONE)
		return -EOPNOTSUPP;

	if (fec & ETHTOOL_FEC_OFF) {
		new_cfg = PORT_PHY_CFG_REQ_FLAGS_FEC_AUTONEG_DISABLE |
			  BNXT_FEC_ALL_OFF(link_info);
		goto apply_fec;
	}
	if (((fec & ETHTOOL_FEC_AUTO) && !(fec_cfg & BNXT_FEC_AUTONEG_CAP)) ||
	    ((fec & ETHTOOL_FEC_RS) && !(fec_cfg & BNXT_FEC_ENC_RS_CAP)) ||
	    ((fec & ETHTOOL_FEC_LLRS) && !(fec_cfg & BNXT_FEC_ENC_LLRS_CAP)) ||
	    ((fec & ETHTOOL_FEC_BASER) && !(fec_cfg & BNXT_FEC_ENC_BASE_R_CAP)))
		return -EINVAL;

	if (fec & ETHTOOL_FEC_AUTO) {
		if (!link_info->autoneg)
			return -EINVAL;
		new_cfg = PORT_PHY_CFG_REQ_FLAGS_FEC_AUTONEG_ENABLE;
	} else {
		new_cfg = bnxt_ethtool_forced_fec_to_fw(link_info, fec);
	}

apply_fec:
	rc = hwrm_req_init(bp, req, HWRM_PORT_PHY_CFG);
	if (rc)
		return rc;
	req->flags = cpu_to_le32(new_cfg | PORT_PHY_CFG_REQ_FLAGS_RESET_PHY);
	rc = hwrm_req_send(bp, req);
	/* update current settings */
	if (!rc) {
		mutex_lock(&bp->link_lock);
		bnxt_update_link(bp, false);
		mutex_unlock(&bp->link_lock);
	}
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

static void bnxt_get_pause_stats(struct net_device *dev,
				 struct ethtool_pause_stats *epstat)
{
	struct bnxt *bp = netdev_priv(dev);
	u64 *rx, *tx;

	if (BNXT_VF(bp) || !(bp->flags & BNXT_FLAG_PORT_STATS))
		return;

	rx = bp->port_stats.sw_stats;
	tx = bp->port_stats.sw_stats + BNXT_TX_PORT_STATS_BYTE_OFFSET / 8;

	epstat->rx_pause_frames = BNXT_GET_RX_PORT_STATS64(rx, rx_pause_frames);
	epstat->tx_pause_frames = BNXT_GET_TX_PORT_STATS64(tx, tx_pause_frames);
}

static int bnxt_set_pauseparam(struct net_device *dev,
			       struct ethtool_pauseparam *epause)
{
	int rc = 0;
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_link_info *link_info = &bp->link_info;

	if (!BNXT_PHY_CFG_ABLE(bp))
		return -EOPNOTSUPP;

	mutex_lock(&bp->link_lock);
	if (epause->autoneg) {
		if (!(link_info->autoneg & BNXT_AUTONEG_SPEED)) {
			rc = -EINVAL;
			goto pause_exit;
		}

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

pause_exit:
	mutex_unlock(&bp->link_lock);
	return rc;
}

static u32 bnxt_get_link(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);

	/* TODO: handle MF, VF, driver close case */
	return bp->link_info.link_up;
}

int bnxt_hwrm_nvm_get_dev_info(struct bnxt *bp,
			       struct hwrm_nvm_get_dev_info_output *nvm_dev_info)
{
	struct hwrm_nvm_get_dev_info_output *resp;
	struct hwrm_nvm_get_dev_info_input *req;
	int rc;

	if (BNXT_VF(bp))
		return -EOPNOTSUPP;

	rc = hwrm_req_init(bp, req, HWRM_NVM_GET_DEV_INFO);
	if (rc)
		return rc;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc)
		memcpy(nvm_dev_info, resp, sizeof(*resp));
	hwrm_req_drop(bp, req);
	return rc;
}

static void bnxt_print_admin_err(struct bnxt *bp)
{
	netdev_info(bp->dev, "PF does not have admin privileges to flash or reset the device\n");
}

static int bnxt_find_nvram_item(struct net_device *dev, u16 type, u16 ordinal,
				u16 ext, u16 *index, u32 *item_length,
				u32 *data_length);

static int bnxt_flash_nvram(struct net_device *dev, u16 dir_type,
			    u16 dir_ordinal, u16 dir_ext, u16 dir_attr,
			    u32 dir_item_len, const u8 *data,
			    size_t data_len)
{
	struct bnxt *bp = netdev_priv(dev);
	struct hwrm_nvm_write_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_NVM_WRITE);
	if (rc)
		return rc;

	if (data_len && data) {
		dma_addr_t dma_handle;
		u8 *kmem;

		kmem = hwrm_req_dma_slice(bp, req, data_len, &dma_handle);
		if (!kmem) {
			hwrm_req_drop(bp, req);
			return -ENOMEM;
		}

		req->dir_data_length = cpu_to_le32(data_len);

		memcpy(kmem, data, data_len);
		req->host_src_addr = cpu_to_le64(dma_handle);
	}

	hwrm_req_timeout(bp, req, bp->hwrm_cmd_max_timeout);
	req->dir_type = cpu_to_le16(dir_type);
	req->dir_ordinal = cpu_to_le16(dir_ordinal);
	req->dir_ext = cpu_to_le16(dir_ext);
	req->dir_attr = cpu_to_le16(dir_attr);
	req->dir_item_length = cpu_to_le32(dir_item_len);
	rc = hwrm_req_send(bp, req);

	if (rc == -EACCES)
		bnxt_print_admin_err(bp);
	return rc;
}

int bnxt_hwrm_firmware_reset(struct net_device *dev, u8 proc_type,
			     u8 self_reset, u8 flags)
{
	struct bnxt *bp = netdev_priv(dev);
	struct hwrm_fw_reset_input *req;
	int rc;

	if (!bnxt_hwrm_reset_permitted(bp)) {
		netdev_warn(bp->dev, "Reset denied by firmware, it may be inhibited by remote driver");
		return -EPERM;
	}

	rc = hwrm_req_init(bp, req, HWRM_FW_RESET);
	if (rc)
		return rc;

	req->embedded_proc_type = proc_type;
	req->selfrst_status = self_reset;
	req->flags = flags;

	if (proc_type == FW_RESET_REQ_EMBEDDED_PROC_TYPE_AP) {
		rc = hwrm_req_send_silent(bp, req);
	} else {
		rc = hwrm_req_send(bp, req);
		if (rc == -EACCES)
			bnxt_print_admin_err(bp);
	}
	return rc;
}

static int bnxt_firmware_reset(struct net_device *dev,
			       enum bnxt_nvm_directory_type dir_type)
{
	u8 self_reset = FW_RESET_REQ_SELFRST_STATUS_SELFRSTNONE;
	u8 proc_type, flags = 0;

	/* TODO: Address self-reset of APE/KONG/BONO/TANG or ungraceful reset */
	/*       (e.g. when firmware isn't already running) */
	switch (dir_type) {
	case BNX_DIR_TYPE_CHIMP_PATCH:
	case BNX_DIR_TYPE_BOOTCODE:
	case BNX_DIR_TYPE_BOOTCODE_2:
		proc_type = FW_RESET_REQ_EMBEDDED_PROC_TYPE_BOOT;
		/* Self-reset ChiMP upon next PCIe reset: */
		self_reset = FW_RESET_REQ_SELFRST_STATUS_SELFRSTPCIERST;
		break;
	case BNX_DIR_TYPE_APE_FW:
	case BNX_DIR_TYPE_APE_PATCH:
		proc_type = FW_RESET_REQ_EMBEDDED_PROC_TYPE_MGMT;
		/* Self-reset APE upon next PCIe reset: */
		self_reset = FW_RESET_REQ_SELFRST_STATUS_SELFRSTPCIERST;
		break;
	case BNX_DIR_TYPE_KONG_FW:
	case BNX_DIR_TYPE_KONG_PATCH:
		proc_type = FW_RESET_REQ_EMBEDDED_PROC_TYPE_NETCTRL;
		break;
	case BNX_DIR_TYPE_BONO_FW:
	case BNX_DIR_TYPE_BONO_PATCH:
		proc_type = FW_RESET_REQ_EMBEDDED_PROC_TYPE_ROCE;
		break;
	default:
		return -EINVAL;
	}

	return bnxt_hwrm_firmware_reset(dev, proc_type, self_reset, flags);
}

static int bnxt_firmware_reset_chip(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);
	u8 flags = 0;

	if (bp->fw_cap & BNXT_FW_CAP_HOT_RESET)
		flags = FW_RESET_REQ_FLAGS_RESET_GRACEFUL;

	return bnxt_hwrm_firmware_reset(dev,
					FW_RESET_REQ_EMBEDDED_PROC_TYPE_CHIP,
					FW_RESET_REQ_SELFRST_STATUS_SELFRSTASAP,
					flags);
}

static int bnxt_firmware_reset_ap(struct net_device *dev)
{
	return bnxt_hwrm_firmware_reset(dev, FW_RESET_REQ_EMBEDDED_PROC_TYPE_AP,
					FW_RESET_REQ_SELFRST_STATUS_SELFRSTNONE,
					0);
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
			      0, 0, 0, fw_data, fw_size);
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
			      0, 0, 0, fw_data, fw_size);

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
	if (bnxt_dir_type_is_ape_bin_format(dir_type))
		rc = bnxt_flash_firmware(dev, dir_type, fw->data, fw->size);
	else if (bnxt_dir_type_is_other_exec_format(dir_type))
		rc = bnxt_flash_microcode(dev, dir_type, fw->data, fw->size);
	else
		rc = bnxt_flash_nvram(dev, dir_type, BNX_DIR_ORDINAL_FIRST,
				      0, 0, 0, fw->data, fw->size);
	release_firmware(fw);
	return rc;
}

#define BNXT_PKG_DMA_SIZE	0x40000
#define BNXT_NVM_MORE_FLAG	(cpu_to_le16(NVM_MODIFY_REQ_FLAGS_BATCH_MODE))
#define BNXT_NVM_LAST_FLAG	(cpu_to_le16(NVM_MODIFY_REQ_FLAGS_BATCH_LAST))

int bnxt_flash_package_from_fw_obj(struct net_device *dev, const struct firmware *fw,
				   u32 install_type)
{
	struct hwrm_nvm_install_update_input *install;
	struct hwrm_nvm_install_update_output *resp;
	struct hwrm_nvm_modify_input *modify;
	struct bnxt *bp = netdev_priv(dev);
	bool defrag_attempted = false;
	dma_addr_t dma_handle;
	u8 *kmem = NULL;
	u32 modify_len;
	u32 item_len;
	u16 index;
	int rc;

	bnxt_hwrm_fw_set_time(bp);

	rc = hwrm_req_init(bp, modify, HWRM_NVM_MODIFY);
	if (rc)
		return rc;

	/* Try allocating a large DMA buffer first.  Older fw will
	 * cause excessive NVRAM erases when using small blocks.
	 */
	modify_len = roundup_pow_of_two(fw->size);
	modify_len = min_t(u32, modify_len, BNXT_PKG_DMA_SIZE);
	while (1) {
		kmem = hwrm_req_dma_slice(bp, modify, modify_len, &dma_handle);
		if (!kmem && modify_len > PAGE_SIZE)
			modify_len /= 2;
		else
			break;
	}
	if (!kmem) {
		hwrm_req_drop(bp, modify);
		return -ENOMEM;
	}

	rc = hwrm_req_init(bp, install, HWRM_NVM_INSTALL_UPDATE);
	if (rc) {
		hwrm_req_drop(bp, modify);
		return rc;
	}

	hwrm_req_timeout(bp, modify, bp->hwrm_cmd_max_timeout);
	hwrm_req_timeout(bp, install, bp->hwrm_cmd_max_timeout);

	hwrm_req_hold(bp, modify);
	modify->host_src_addr = cpu_to_le64(dma_handle);

	resp = hwrm_req_hold(bp, install);
	if ((install_type & 0xffff) == 0)
		install_type >>= 16;
	install->install_type = cpu_to_le32(install_type);

	do {
		u32 copied = 0, len = modify_len;

		rc = bnxt_find_nvram_item(dev, BNX_DIR_TYPE_UPDATE,
					  BNX_DIR_ORDINAL_FIRST,
					  BNX_DIR_EXT_NONE,
					  &index, &item_len, NULL);
		if (rc) {
			netdev_err(dev, "PKG update area not created in nvram\n");
			break;
		}
		if (fw->size > item_len) {
			netdev_err(dev, "PKG insufficient update area in nvram: %lu\n",
				   (unsigned long)fw->size);
			rc = -EFBIG;
			break;
		}

		modify->dir_idx = cpu_to_le16(index);

		if (fw->size > modify_len)
			modify->flags = BNXT_NVM_MORE_FLAG;
		while (copied < fw->size) {
			u32 balance = fw->size - copied;

			if (balance <= modify_len) {
				len = balance;
				if (copied)
					modify->flags |= BNXT_NVM_LAST_FLAG;
			}
			memcpy(kmem, fw->data + copied, len);
			modify->len = cpu_to_le32(len);
			modify->offset = cpu_to_le32(copied);
			rc = hwrm_req_send(bp, modify);
			if (rc)
				goto pkg_abort;
			copied += len;
		}

		rc = hwrm_req_send_silent(bp, install);

		if (defrag_attempted) {
			/* We have tried to defragment already in the previous
			 * iteration. Return with the result for INSTALL_UPDATE
			 */
			break;
		}

		if (rc && ((struct hwrm_err_output *)resp)->cmd_err ==
		    NVM_INSTALL_UPDATE_CMD_ERR_CODE_FRAG_ERR) {
			install->flags =
				cpu_to_le16(NVM_INSTALL_UPDATE_REQ_FLAGS_ALLOWED_TO_DEFRAG);

			rc = hwrm_req_send_silent(bp, install);

			if (rc && ((struct hwrm_err_output *)resp)->cmd_err ==
			    NVM_INSTALL_UPDATE_CMD_ERR_CODE_NO_SPACE) {
				/* FW has cleared NVM area, driver will create
				 * UPDATE directory and try the flash again
				 */
				defrag_attempted = true;
				install->flags = 0;
				rc = bnxt_flash_nvram(bp->dev,
						      BNX_DIR_TYPE_UPDATE,
						      BNX_DIR_ORDINAL_FIRST,
						      0, 0, item_len, NULL, 0);
			} else if (rc) {
				netdev_err(dev, "HWRM_NVM_INSTALL_UPDATE failure rc :%x\n", rc);
			}
		} else if (rc) {
			netdev_err(dev, "HWRM_NVM_INSTALL_UPDATE failure rc :%x\n", rc);
		}
	} while (defrag_attempted && !rc);

pkg_abort:
	hwrm_req_drop(bp, modify);
	hwrm_req_drop(bp, install);

	if (resp->result) {
		netdev_err(dev, "PKG install error = %d, problem_item = %d\n",
			   (s8)resp->result, (int)resp->problem_item);
		rc = -ENOPKG;
	}
	if (rc == -EACCES)
		bnxt_print_admin_err(bp);
	return rc;
}

static int bnxt_flash_package_from_file(struct net_device *dev, const char *filename,
					u32 install_type)
{
	const struct firmware *fw;
	int rc;

	rc = request_firmware(&fw, filename, &dev->dev);
	if (rc != 0) {
		netdev_err(dev, "PKG error %d requesting file: %s\n",
			   rc, filename);
		return rc;
	}

	rc = bnxt_flash_package_from_fw_obj(dev, fw, install_type);

	release_firmware(fw);

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
	struct hwrm_nvm_get_dir_info_output *output;
	struct hwrm_nvm_get_dir_info_input *req;
	struct bnxt *bp = netdev_priv(dev);
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_NVM_GET_DIR_INFO);
	if (rc)
		return rc;

	output = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc) {
		*entries = le32_to_cpu(output->entries);
		*length = le32_to_cpu(output->entry_length);
	}
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_get_eeprom_len(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);

	if (BNXT_VF(bp))
		return 0;

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
	struct hwrm_nvm_get_dir_entries_input *req;

	rc = nvm_get_dir_info(dev, &dir_entries, &entry_length);
	if (rc != 0)
		return rc;

	if (!dir_entries || !entry_length)
		return -EIO;

	/* Insert 2 bytes of directory info (count and size of entries) */
	if (len < 2)
		return -EINVAL;

	*data++ = dir_entries;
	*data++ = entry_length;
	len -= 2;
	memset(data, 0xff, len);

	rc = hwrm_req_init(bp, req, HWRM_NVM_GET_DIR_ENTRIES);
	if (rc)
		return rc;

	buflen = dir_entries * entry_length;
	buf = hwrm_req_dma_slice(bp, req, buflen, &dma_handle);
	if (!buf) {
		hwrm_req_drop(bp, req);
		return -ENOMEM;
	}
	req->host_dest_addr = cpu_to_le64(dma_handle);

	hwrm_req_hold(bp, req); /* hold the slice */
	rc = hwrm_req_send(bp, req);
	if (rc == 0)
		memcpy(data, buf, len > buflen ? buflen : len);
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_get_nvram_item(struct net_device *dev, u32 index, u32 offset,
			       u32 length, u8 *data)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc;
	u8 *buf;
	dma_addr_t dma_handle;
	struct hwrm_nvm_read_input *req;

	if (!length)
		return -EINVAL;

	rc = hwrm_req_init(bp, req, HWRM_NVM_READ);
	if (rc)
		return rc;

	buf = hwrm_req_dma_slice(bp, req, length, &dma_handle);
	if (!buf) {
		hwrm_req_drop(bp, req);
		return -ENOMEM;
	}

	req->host_dest_addr = cpu_to_le64(dma_handle);
	req->dir_idx = cpu_to_le16(index);
	req->offset = cpu_to_le32(offset);
	req->len = cpu_to_le32(length);

	hwrm_req_hold(bp, req); /* hold the slice */
	rc = hwrm_req_send(bp, req);
	if (rc == 0)
		memcpy(data, buf, length);
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_find_nvram_item(struct net_device *dev, u16 type, u16 ordinal,
				u16 ext, u16 *index, u32 *item_length,
				u32 *data_length)
{
	struct hwrm_nvm_find_dir_entry_output *output;
	struct hwrm_nvm_find_dir_entry_input *req;
	struct bnxt *bp = netdev_priv(dev);
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_NVM_FIND_DIR_ENTRY);
	if (rc)
		return rc;

	req->enables = 0;
	req->dir_idx = 0;
	req->dir_type = cpu_to_le16(type);
	req->dir_ordinal = cpu_to_le16(ordinal);
	req->dir_ext = cpu_to_le16(ext);
	req->opt_ordinal = NVM_FIND_DIR_ENTRY_REQ_OPT_ORDINAL_EQ;
	output = hwrm_req_hold(bp, req);
	rc = hwrm_req_send_silent(bp, req);
	if (rc == 0) {
		if (index)
			*index = le16_to_cpu(output->dir_idx);
		if (item_length)
			*item_length = le32_to_cpu(output->dir_item_length);
		if (data_length)
			*data_length = le32_to_cpu(output->dir_data_length);
	}
	hwrm_req_drop(bp, req);
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

int bnxt_get_pkginfo(struct net_device *dev, char *ver, int size)
{
	struct bnxt *bp = netdev_priv(dev);
	u16 index = 0;
	char *pkgver;
	u32 pkglen;
	u8 *pkgbuf;
	int rc;

	rc = bnxt_find_nvram_item(dev, BNX_DIR_TYPE_PKG_LOG,
				  BNX_DIR_ORDINAL_FIRST, BNX_DIR_EXT_NONE,
				  &index, NULL, &pkglen);
	if (rc)
		return rc;

	pkgbuf = kzalloc(pkglen, GFP_KERNEL);
	if (!pkgbuf) {
		dev_err(&bp->pdev->dev, "Unable to allocate memory for pkg version, length = %u\n",
			pkglen);
		return -ENOMEM;
	}

	rc = bnxt_get_nvram_item(dev, index, 0, pkglen, pkgbuf);
	if (rc)
		goto err;

	pkgver = bnxt_parse_pkglog(BNX_PKG_LOG_FIELD_IDX_PKG_VERSION, pkgbuf,
				   pkglen);
	if (pkgver && *pkgver != 0 && isdigit(*pkgver))
		strscpy(ver, pkgver, size);
	else
		rc = -ENOENT;

err:
	kfree(pkgbuf);

	return rc;
}

static void bnxt_get_pkgver(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);
	char buf[FW_VER_STR_LEN];
	int len;

	if (!bnxt_get_pkginfo(dev, buf, sizeof(buf))) {
		len = strlen(bp->fw_ver_str);
		snprintf(bp->fw_ver_str + len, FW_VER_STR_LEN - len - 1,
			 "/pkg %s", buf);
	}
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
	struct hwrm_nvm_erase_dir_entry_input *req;
	struct bnxt *bp = netdev_priv(dev);
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_NVM_ERASE_DIR_ENTRY);
	if (rc)
		return rc;

	req->dir_idx = cpu_to_le16(index);
	return hwrm_req_send(bp, req);
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
	if (bnxt_dir_type_is_executable(type))
		return -EOPNOTSUPP;
	ext = eeprom->magic & 0xffff;
	ordinal = eeprom->offset >> 16;
	attr = eeprom->offset & 0xffff;

	return bnxt_flash_nvram(dev, type, ordinal, ext, attr, 0, data,
				eeprom->len);
}

static int bnxt_set_eee(struct net_device *dev, struct ethtool_eee *edata)
{
	struct bnxt *bp = netdev_priv(dev);
	struct ethtool_eee *eee = &bp->eee;
	struct bnxt_link_info *link_info = &bp->link_info;
	u32 advertising;
	int rc = 0;

	if (!BNXT_PHY_CFG_ABLE(bp))
		return -EOPNOTSUPP;

	if (!(bp->phy_flags & BNXT_PHY_FL_EEE_CAP))
		return -EOPNOTSUPP;

	mutex_lock(&bp->link_lock);
	advertising = _bnxt_fw_to_ethtool_adv_spds(link_info->advertising, 0);
	if (!edata->eee_enabled)
		goto eee_ok;

	if (!(link_info->autoneg & BNXT_AUTONEG_SPEED)) {
		netdev_warn(dev, "EEE requires autoneg\n");
		rc = -EINVAL;
		goto eee_exit;
	}
	if (edata->tx_lpi_enabled) {
		if (bp->lpi_tmr_hi && (edata->tx_lpi_timer > bp->lpi_tmr_hi ||
				       edata->tx_lpi_timer < bp->lpi_tmr_lo)) {
			netdev_warn(dev, "Valid LPI timer range is %d and %d microsecs\n",
				    bp->lpi_tmr_lo, bp->lpi_tmr_hi);
			rc = -EINVAL;
			goto eee_exit;
		} else if (!bp->lpi_tmr_hi) {
			edata->tx_lpi_timer = eee->tx_lpi_timer;
		}
	}
	if (!edata->advertised) {
		edata->advertised = advertising & eee->supported;
	} else if (edata->advertised & ~advertising) {
		netdev_warn(dev, "EEE advertised %x must be a subset of autoneg advertised speeds %x\n",
			    edata->advertised, advertising);
		rc = -EINVAL;
		goto eee_exit;
	}

	eee->advertised = edata->advertised;
	eee->tx_lpi_enabled = edata->tx_lpi_enabled;
	eee->tx_lpi_timer = edata->tx_lpi_timer;
eee_ok:
	eee->eee_enabled = edata->eee_enabled;

	if (netif_running(dev))
		rc = bnxt_hwrm_set_link_setting(bp, false, true);

eee_exit:
	mutex_unlock(&bp->link_lock);
	return rc;
}

static int bnxt_get_eee(struct net_device *dev, struct ethtool_eee *edata)
{
	struct bnxt *bp = netdev_priv(dev);

	if (!(bp->phy_flags & BNXT_PHY_FL_EEE_CAP))
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
	struct hwrm_port_phy_i2c_read_output *output;
	struct hwrm_port_phy_i2c_read_input *req;
	int rc, byte_offset = 0;

	rc = hwrm_req_init(bp, req, HWRM_PORT_PHY_I2C_READ);
	if (rc)
		return rc;

	output = hwrm_req_hold(bp, req);
	req->i2c_slave_addr = i2c_addr;
	req->page_number = cpu_to_le16(page_number);
	req->port_id = cpu_to_le16(bp->pf.port_id);
	do {
		u16 xfer_size;

		xfer_size = min_t(u16, data_length, BNXT_MAX_PHY_I2C_RESP_SIZE);
		data_length -= xfer_size;
		req->page_offset = cpu_to_le16(start_addr + byte_offset);
		req->data_length = xfer_size;
		req->enables = cpu_to_le32(start_addr + byte_offset ?
				 PORT_PHY_I2C_READ_REQ_ENABLES_PAGE_OFFSET : 0);
		rc = hwrm_req_send(bp, req);
		if (!rc)
			memcpy(buf + byte_offset, output->data, xfer_size);
		byte_offset += xfer_size;
	} while (!rc && data_length > 0);
	hwrm_req_drop(bp, req);

	return rc;
}

static int bnxt_get_module_info(struct net_device *dev,
				struct ethtool_modinfo *modinfo)
{
	u8 data[SFF_DIAG_SUPPORT_OFFSET + 1];
	struct bnxt *bp = netdev_priv(dev);
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

	rc = bnxt_read_sfp_module_eeprom_info(bp, I2C_DEV_ADDR_A0, 0, 0,
					      SFF_DIAG_SUPPORT_OFFSET + 1,
					      data);
	if (!rc) {
		u8 module_id = data[0];
		u8 diag_supported = data[SFF_DIAG_SUPPORT_OFFSET];

		switch (module_id) {
		case SFF_MODULE_ID_SFP:
			modinfo->type = ETH_MODULE_SFF_8472;
			modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
			if (!diag_supported)
				modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
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
		rc = bnxt_read_sfp_module_eeprom_info(bp, I2C_DEV_ADDR_A2, 0,
						      start, length, data);
	}
	return rc;
}

static int bnxt_nway_reset(struct net_device *dev)
{
	int rc = 0;

	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_link_info *link_info = &bp->link_info;

	if (!BNXT_PHY_CFG_ABLE(bp))
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
	struct hwrm_port_led_cfg_input *req;
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_pf_info *pf = &bp->pf;
	struct bnxt_led_cfg *led_cfg;
	u8 led_state;
	__le16 duration;
	int rc, i;

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
	rc = hwrm_req_init(bp, req, HWRM_PORT_LED_CFG);
	if (rc)
		return rc;

	req->port_id = cpu_to_le16(pf->port_id);
	req->num_leds = bp->num_leds;
	led_cfg = (struct bnxt_led_cfg *)&req->led0_id;
	for (i = 0; i < bp->num_leds; i++, led_cfg++) {
		req->enables |= BNXT_LED_DFLT_ENABLES(i);
		led_cfg->led_id = bp->leds[i].led_id;
		led_cfg->led_state = led_state;
		led_cfg->led_blink_on = duration;
		led_cfg->led_blink_off = duration;
		led_cfg->led_group_id = bp->leds[i].led_group_id;
	}
	return hwrm_req_send(bp, req);
}

static int bnxt_hwrm_selftest_irq(struct bnxt *bp, u16 cmpl_ring)
{
	struct hwrm_selftest_irq_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_SELFTEST_IRQ);
	if (rc)
		return rc;

	req->cmpl_ring = cpu_to_le16(cmpl_ring);
	return hwrm_req_send(bp, req);
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
	struct hwrm_port_mac_cfg_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_PORT_MAC_CFG);
	if (rc)
		return rc;

	req->enables = cpu_to_le32(PORT_MAC_CFG_REQ_ENABLES_LPBK);
	if (enable)
		req->lpbk = PORT_MAC_CFG_REQ_LPBK_LOCAL;
	else
		req->lpbk = PORT_MAC_CFG_REQ_LPBK_NONE;
	return hwrm_req_send(bp, req);
}

static int bnxt_query_force_speeds(struct bnxt *bp, u16 *force_speeds)
{
	struct hwrm_port_phy_qcaps_output *resp;
	struct hwrm_port_phy_qcaps_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_PORT_PHY_QCAPS);
	if (rc)
		return rc;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc)
		*force_speeds = le16_to_cpu(resp->supported_speeds_force_mode);

	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_disable_an_for_lpbk(struct bnxt *bp,
				    struct hwrm_port_phy_cfg_input *req)
{
	struct bnxt_link_info *link_info = &bp->link_info;
	u16 fw_advertising;
	u16 fw_speed;
	int rc;

	if (!link_info->autoneg ||
	    (bp->phy_flags & BNXT_PHY_FL_AN_PHY_LPBK))
		return 0;

	rc = bnxt_query_force_speeds(bp, &fw_advertising);
	if (rc)
		return rc;

	fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_1GB;
	if (bp->link_info.link_up)
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
	rc = hwrm_req_send(bp, req);
	req->flags = 0;
	req->force_link_speed = cpu_to_le16(0);
	return rc;
}

static int bnxt_hwrm_phy_loopback(struct bnxt *bp, bool enable, bool ext)
{
	struct hwrm_port_phy_cfg_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_PORT_PHY_CFG);
	if (rc)
		return rc;

	/* prevent bnxt_disable_an_for_lpbk() from consuming the request */
	hwrm_req_hold(bp, req);

	if (enable) {
		bnxt_disable_an_for_lpbk(bp, req);
		if (ext)
			req->lpbk = PORT_PHY_CFG_REQ_LPBK_EXTERNAL;
		else
			req->lpbk = PORT_PHY_CFG_REQ_LPBK_LOCAL;
	} else {
		req->lpbk = PORT_PHY_CFG_REQ_LPBK_NONE;
	}
	req->enables = cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_LPBK);
	rc = hwrm_req_send(bp, req);
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_rx_loopback(struct bnxt *bp, struct bnxt_cp_ring_info *cpr,
			    u32 raw_cons, int pkt_size)
{
	struct bnxt_napi *bnapi = cpr->bnapi;
	struct bnxt_rx_ring_info *rxr;
	struct bnxt_sw_rx_bd *rx_buf;
	struct rx_cmp *rxcmp;
	u16 cp_cons, cons;
	u8 *data;
	u32 len;
	int i;

	rxr = bnapi->rx_ring;
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

static int bnxt_poll_loopback(struct bnxt *bp, struct bnxt_cp_ring_info *cpr,
			      int pkt_size)
{
	struct tx_cmp *txcmp;
	int rc = -EIO;
	u32 raw_cons;
	u32 cons;
	int i;

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
			rc = bnxt_rx_loopback(bp, cpr, raw_cons, pkt_size);
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
	struct bnxt_rx_ring_info *rxr = &bp->rx_ring[0];
	struct bnxt_cp_ring_info *cpr;
	int pkt_size, i = 0;
	struct sk_buff *skb;
	dma_addr_t map;
	u8 *data;
	int rc;

	cpr = &rxr->bnapi->cp_ring;
	if (bp->flags & BNXT_FLAG_CHIP_P5)
		cpr = cpr->cp_ring_arr[BNXT_RX_HDL];
	pkt_size = min(bp->dev->mtu + ETH_HLEN, bp->rx_copy_thresh);
	skb = netdev_alloc_skb(bp->dev, pkt_size);
	if (!skb)
		return -ENOMEM;
	data = skb_put(skb, pkt_size);
	ether_addr_copy(&data[i], bp->dev->dev_addr);
	i += ETH_ALEN;
	ether_addr_copy(&data[i], bp->dev->dev_addr);
	i += ETH_ALEN;
	for ( ; i < pkt_size; i++)
		data[i] = (u8)(i & 0xff);

	map = dma_map_single(&bp->pdev->dev, skb->data, pkt_size,
			     DMA_TO_DEVICE);
	if (dma_mapping_error(&bp->pdev->dev, map)) {
		dev_kfree_skb(skb);
		return -EIO;
	}
	bnxt_xmit_bd(bp, txr, map, pkt_size);

	/* Sync BD data before updating doorbell */
	wmb();

	bnxt_db_write(bp, &txr->tx_db, txr->tx_prod);
	rc = bnxt_poll_loopback(bp, cpr, pkt_size);

	dma_unmap_single(&bp->pdev->dev, map, pkt_size, DMA_TO_DEVICE);
	dev_kfree_skb(skb);
	return rc;
}

static int bnxt_run_fw_tests(struct bnxt *bp, u8 test_mask, u8 *test_results)
{
	struct hwrm_selftest_exec_output *resp;
	struct hwrm_selftest_exec_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_SELFTEST_EXEC);
	if (rc)
		return rc;

	hwrm_req_timeout(bp, req, bp->test_info->timeout);
	req->flags = test_mask;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	*test_results = resp->test_success;
	hwrm_req_drop(bp, req);
	return rc;
}

#define BNXT_DRV_TESTS			4
#define BNXT_MACLPBK_TEST_IDX		(bp->num_tests - BNXT_DRV_TESTS)
#define BNXT_PHYLPBK_TEST_IDX		(BNXT_MACLPBK_TEST_IDX + 1)
#define BNXT_EXTLPBK_TEST_IDX		(BNXT_MACLPBK_TEST_IDX + 2)
#define BNXT_IRQ_TEST_IDX		(BNXT_MACLPBK_TEST_IDX + 3)

static void bnxt_self_test(struct net_device *dev, struct ethtool_test *etest,
			   u64 *buf)
{
	struct bnxt *bp = netdev_priv(dev);
	bool do_ext_lpbk = false;
	bool offline = false;
	u8 test_results = 0;
	u8 test_mask = 0;
	int rc = 0, i;

	if (!bp->num_tests || !BNXT_PF(bp))
		return;
	memset(buf, 0, sizeof(u64) * bp->num_tests);
	if (!netif_running(dev)) {
		etest->flags |= ETH_TEST_FL_FAILED;
		return;
	}

	if ((etest->flags & ETH_TEST_FL_EXTERNAL_LB) &&
	    (bp->phy_flags & BNXT_PHY_FL_EXT_LPBK))
		do_ext_lpbk = true;

	if (etest->flags & ETH_TEST_FL_OFFLINE) {
		if (bp->pf.active_vfs || !BNXT_SINGLE_PF(bp)) {
			etest->flags |= ETH_TEST_FL_FAILED;
			netdev_warn(dev, "Offline tests cannot be run with active VFs or on shared PF\n");
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
		bnxt_ulp_stop(bp);
		rc = bnxt_close_nic(bp, true, false);
		if (rc) {
			bnxt_ulp_start(bp, rc);
			return;
		}
		bnxt_run_fw_tests(bp, test_mask, &test_results);

		buf[BNXT_MACLPBK_TEST_IDX] = 1;
		bnxt_hwrm_mac_loopback(bp, true);
		msleep(250);
		rc = bnxt_half_open_nic(bp);
		if (rc) {
			bnxt_hwrm_mac_loopback(bp, false);
			etest->flags |= ETH_TEST_FL_FAILED;
			bnxt_ulp_start(bp, rc);
			return;
		}
		if (bnxt_run_loopback(bp))
			etest->flags |= ETH_TEST_FL_FAILED;
		else
			buf[BNXT_MACLPBK_TEST_IDX] = 0;

		bnxt_hwrm_mac_loopback(bp, false);
		bnxt_hwrm_phy_loopback(bp, true, false);
		msleep(1000);
		if (bnxt_run_loopback(bp)) {
			buf[BNXT_PHYLPBK_TEST_IDX] = 1;
			etest->flags |= ETH_TEST_FL_FAILED;
		}
		if (do_ext_lpbk) {
			etest->flags |= ETH_TEST_FL_EXTERNAL_LB_DONE;
			bnxt_hwrm_phy_loopback(bp, true, true);
			msleep(1000);
			if (bnxt_run_loopback(bp)) {
				buf[BNXT_EXTLPBK_TEST_IDX] = 1;
				etest->flags |= ETH_TEST_FL_FAILED;
			}
		}
		bnxt_hwrm_phy_loopback(bp, false, false);
		bnxt_half_close_nic(bp);
		rc = bnxt_open_nic(bp, true, true);
		bnxt_ulp_start(bp, rc);
	}
	if (rc || bnxt_test_irq(bp)) {
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

static int bnxt_reset(struct net_device *dev, u32 *flags)
{
	struct bnxt *bp = netdev_priv(dev);
	bool reload = false;
	u32 req = *flags;

	if (!req)
		return -EINVAL;

	if (!BNXT_PF(bp)) {
		netdev_err(dev, "Reset is not supported from a VF\n");
		return -EOPNOTSUPP;
	}

	if (pci_vfs_assigned(bp->pdev) &&
	    !(bp->fw_cap & BNXT_FW_CAP_HOT_RESET)) {
		netdev_err(dev,
			   "Reset not allowed when VFs are assigned to VMs\n");
		return -EBUSY;
	}

	if ((req & BNXT_FW_RESET_CHIP) == BNXT_FW_RESET_CHIP) {
		/* This feature is not supported in older firmware versions */
		if (bp->hwrm_spec_code >= 0x10803) {
			if (!bnxt_firmware_reset_chip(dev)) {
				netdev_info(dev, "Firmware reset request successful.\n");
				if (!(bp->fw_cap & BNXT_FW_CAP_HOT_RESET))
					reload = true;
				*flags &= ~BNXT_FW_RESET_CHIP;
			}
		} else if (req == BNXT_FW_RESET_CHIP) {
			return -EOPNOTSUPP; /* only request, fail hard */
		}
	}

	if (req & BNXT_FW_RESET_AP) {
		/* This feature is not supported in older firmware versions */
		if (bp->hwrm_spec_code >= 0x10803) {
			if (!bnxt_firmware_reset_ap(dev)) {
				netdev_info(dev, "Reset application processor successful.\n");
				reload = true;
				*flags &= ~BNXT_FW_RESET_AP;
			}
		} else if (req == BNXT_FW_RESET_AP) {
			return -EOPNOTSUPP; /* only request, fail hard */
		}
	}

	if (reload)
		netdev_info(dev, "Reload driver to complete reset\n");

	return 0;
}

static int bnxt_set_dump(struct net_device *dev, struct ethtool_dump *dump)
{
	struct bnxt *bp = netdev_priv(dev);

	if (dump->flag > BNXT_DUMP_CRASH) {
		netdev_info(dev, "Supports only Live(0) and Crash(1) dumps.\n");
		return -EINVAL;
	}

	if (!IS_ENABLED(CONFIG_TEE_BNXT_FW) && dump->flag == BNXT_DUMP_CRASH) {
		netdev_info(dev, "Cannot collect crash dump as TEE_BNXT_FW config option is not enabled.\n");
		return -EOPNOTSUPP;
	}

	bp->dump_flag = dump->flag;
	return 0;
}

static int bnxt_get_dump_flag(struct net_device *dev, struct ethtool_dump *dump)
{
	struct bnxt *bp = netdev_priv(dev);

	if (bp->hwrm_spec_code < 0x10801)
		return -EOPNOTSUPP;

	dump->version = bp->ver_resp.hwrm_fw_maj_8b << 24 |
			bp->ver_resp.hwrm_fw_min_8b << 16 |
			bp->ver_resp.hwrm_fw_bld_8b << 8 |
			bp->ver_resp.hwrm_fw_rsvd_8b;

	dump->flag = bp->dump_flag;
	dump->len = bnxt_get_coredump_length(bp, bp->dump_flag);
	return 0;
}

static int bnxt_get_dump_data(struct net_device *dev, struct ethtool_dump *dump,
			      void *buf)
{
	struct bnxt *bp = netdev_priv(dev);

	if (bp->hwrm_spec_code < 0x10801)
		return -EOPNOTSUPP;

	memset(buf, 0, dump->len);

	dump->flag = bp->dump_flag;
	return bnxt_get_coredump(bp, dump->flag, buf, &dump->len);
}

static int bnxt_get_ts_info(struct net_device *dev,
			    struct ethtool_ts_info *info)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_ptp_cfg *ptp;

	ptp = bp->ptp_cfg;
	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_SOFTWARE;

	info->phc_index = -1;
	if (!ptp)
		return 0;

	info->so_timestamping |= SOF_TIMESTAMPING_TX_HARDWARE |
				 SOF_TIMESTAMPING_RX_HARDWARE |
				 SOF_TIMESTAMPING_RAW_HARDWARE;
	if (ptp->ptp_clock)
		info->phc_index = ptp_clock_index(ptp->ptp_clock);

	info->tx_types = (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON);

	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			   (1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			   (1 << HWTSTAMP_FILTER_PTP_V2_L4_EVENT);
	return 0;
}

void bnxt_ethtool_init(struct bnxt *bp)
{
	struct hwrm_selftest_qlist_output *resp;
	struct hwrm_selftest_qlist_input *req;
	struct bnxt_test_info *test_info;
	struct net_device *dev = bp->dev;
	int i, rc;

	if (!(bp->fw_cap & BNXT_FW_CAP_PKG_VER))
		bnxt_get_pkgver(dev);

	bp->num_tests = 0;
	if (bp->hwrm_spec_code < 0x10704 || !BNXT_PF(bp))
		return;

	test_info = bp->test_info;
	if (!test_info) {
		test_info = kzalloc(sizeof(*bp->test_info), GFP_KERNEL);
		if (!test_info)
			return;
		bp->test_info = test_info;
	}

	if (hwrm_req_init(bp, req, HWRM_SELFTEST_QLIST))
		return;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send_silent(bp, req);
	if (rc)
		goto ethtool_init_exit;

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
		} else if (i == BNXT_EXTLPBK_TEST_IDX) {
			strcpy(str, "Ext loopback test (offline)");
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
	hwrm_req_drop(bp, req);
}

static void bnxt_get_eth_phy_stats(struct net_device *dev,
				   struct ethtool_eth_phy_stats *phy_stats)
{
	struct bnxt *bp = netdev_priv(dev);
	u64 *rx;

	if (BNXT_VF(bp) || !(bp->flags & BNXT_FLAG_PORT_STATS_EXT))
		return;

	rx = bp->rx_port_stats_ext.sw_stats;
	phy_stats->SymbolErrorDuringCarrier =
		*(rx + BNXT_RX_STATS_EXT_OFFSET(rx_pcs_symbol_err));
}

static void bnxt_get_eth_mac_stats(struct net_device *dev,
				   struct ethtool_eth_mac_stats *mac_stats)
{
	struct bnxt *bp = netdev_priv(dev);
	u64 *rx, *tx;

	if (BNXT_VF(bp) || !(bp->flags & BNXT_FLAG_PORT_STATS))
		return;

	rx = bp->port_stats.sw_stats;
	tx = bp->port_stats.sw_stats + BNXT_TX_PORT_STATS_BYTE_OFFSET / 8;

	mac_stats->FramesReceivedOK =
		BNXT_GET_RX_PORT_STATS64(rx, rx_good_frames);
	mac_stats->FramesTransmittedOK =
		BNXT_GET_TX_PORT_STATS64(tx, tx_good_frames);
	mac_stats->FrameCheckSequenceErrors =
		BNXT_GET_RX_PORT_STATS64(rx, rx_fcs_err_frames);
	mac_stats->AlignmentErrors =
		BNXT_GET_RX_PORT_STATS64(rx, rx_align_err_frames);
	mac_stats->OutOfRangeLengthField =
		BNXT_GET_RX_PORT_STATS64(rx, rx_oor_len_frames);
}

static void bnxt_get_eth_ctrl_stats(struct net_device *dev,
				    struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct bnxt *bp = netdev_priv(dev);
	u64 *rx;

	if (BNXT_VF(bp) || !(bp->flags & BNXT_FLAG_PORT_STATS))
		return;

	rx = bp->port_stats.sw_stats;
	ctrl_stats->MACControlFramesReceived =
		BNXT_GET_RX_PORT_STATS64(rx, rx_ctrl_frames);
}

static const struct ethtool_rmon_hist_range bnxt_rmon_ranges[] = {
	{    0,    64 },
	{   65,   127 },
	{  128,   255 },
	{  256,   511 },
	{  512,  1023 },
	{ 1024,  1518 },
	{ 1519,  2047 },
	{ 2048,  4095 },
	{ 4096,  9216 },
	{ 9217, 16383 },
	{}
};

static void bnxt_get_rmon_stats(struct net_device *dev,
				struct ethtool_rmon_stats *rmon_stats,
				const struct ethtool_rmon_hist_range **ranges)
{
	struct bnxt *bp = netdev_priv(dev);
	u64 *rx, *tx;

	if (BNXT_VF(bp) || !(bp->flags & BNXT_FLAG_PORT_STATS))
		return;

	rx = bp->port_stats.sw_stats;
	tx = bp->port_stats.sw_stats + BNXT_TX_PORT_STATS_BYTE_OFFSET / 8;

	rmon_stats->jabbers =
		BNXT_GET_RX_PORT_STATS64(rx, rx_jbr_frames);
	rmon_stats->oversize_pkts =
		BNXT_GET_RX_PORT_STATS64(rx, rx_ovrsz_frames);
	rmon_stats->undersize_pkts =
		BNXT_GET_RX_PORT_STATS64(rx, rx_undrsz_frames);

	rmon_stats->hist[0] = BNXT_GET_RX_PORT_STATS64(rx, rx_64b_frames);
	rmon_stats->hist[1] = BNXT_GET_RX_PORT_STATS64(rx, rx_65b_127b_frames);
	rmon_stats->hist[2] = BNXT_GET_RX_PORT_STATS64(rx, rx_128b_255b_frames);
	rmon_stats->hist[3] = BNXT_GET_RX_PORT_STATS64(rx, rx_256b_511b_frames);
	rmon_stats->hist[4] =
		BNXT_GET_RX_PORT_STATS64(rx, rx_512b_1023b_frames);
	rmon_stats->hist[5] =
		BNXT_GET_RX_PORT_STATS64(rx, rx_1024b_1518b_frames);
	rmon_stats->hist[6] =
		BNXT_GET_RX_PORT_STATS64(rx, rx_1519b_2047b_frames);
	rmon_stats->hist[7] =
		BNXT_GET_RX_PORT_STATS64(rx, rx_2048b_4095b_frames);
	rmon_stats->hist[8] =
		BNXT_GET_RX_PORT_STATS64(rx, rx_4096b_9216b_frames);
	rmon_stats->hist[9] =
		BNXT_GET_RX_PORT_STATS64(rx, rx_9217b_16383b_frames);

	rmon_stats->hist_tx[0] =
		BNXT_GET_TX_PORT_STATS64(tx, tx_64b_frames);
	rmon_stats->hist_tx[1] =
		BNXT_GET_TX_PORT_STATS64(tx, tx_65b_127b_frames);
	rmon_stats->hist_tx[2] =
		BNXT_GET_TX_PORT_STATS64(tx, tx_128b_255b_frames);
	rmon_stats->hist_tx[3] =
		BNXT_GET_TX_PORT_STATS64(tx, tx_256b_511b_frames);
	rmon_stats->hist_tx[4] =
		BNXT_GET_TX_PORT_STATS64(tx, tx_512b_1023b_frames);
	rmon_stats->hist_tx[5] =
		BNXT_GET_TX_PORT_STATS64(tx, tx_1024b_1518b_frames);
	rmon_stats->hist_tx[6] =
		BNXT_GET_TX_PORT_STATS64(tx, tx_1519b_2047b_frames);
	rmon_stats->hist_tx[7] =
		BNXT_GET_TX_PORT_STATS64(tx, tx_2048b_4095b_frames);
	rmon_stats->hist_tx[8] =
		BNXT_GET_TX_PORT_STATS64(tx, tx_4096b_9216b_frames);
	rmon_stats->hist_tx[9] =
		BNXT_GET_TX_PORT_STATS64(tx, tx_9217b_16383b_frames);

	*ranges = bnxt_rmon_ranges;
}

void bnxt_ethtool_free(struct bnxt *bp)
{
	kfree(bp->test_info);
	bp->test_info = NULL;
}

const struct ethtool_ops bnxt_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_MAX_FRAMES |
				     ETHTOOL_COALESCE_USECS_IRQ |
				     ETHTOOL_COALESCE_MAX_FRAMES_IRQ |
				     ETHTOOL_COALESCE_STATS_BLOCK_USECS |
				     ETHTOOL_COALESCE_USE_ADAPTIVE_RX |
				     ETHTOOL_COALESCE_USE_CQE,
	.get_link_ksettings	= bnxt_get_link_ksettings,
	.set_link_ksettings	= bnxt_set_link_ksettings,
	.get_fec_stats		= bnxt_get_fec_stats,
	.get_fecparam		= bnxt_get_fecparam,
	.set_fecparam		= bnxt_set_fecparam,
	.get_pause_stats	= bnxt_get_pause_stats,
	.get_pauseparam		= bnxt_get_pauseparam,
	.set_pauseparam		= bnxt_set_pauseparam,
	.get_drvinfo		= bnxt_get_drvinfo,
	.get_regs_len		= bnxt_get_regs_len,
	.get_regs		= bnxt_get_regs,
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
	.set_rxfh		= bnxt_set_rxfh,
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
	.get_ts_info		= bnxt_get_ts_info,
	.reset			= bnxt_reset,
	.set_dump		= bnxt_set_dump,
	.get_dump_flag		= bnxt_get_dump_flag,
	.get_dump_data		= bnxt_get_dump_data,
	.get_eth_phy_stats	= bnxt_get_eth_phy_stats,
	.get_eth_mac_stats	= bnxt_get_eth_mac_stats,
	.get_eth_ctrl_stats	= bnxt_get_eth_ctrl_stats,
	.get_rmon_stats		= bnxt_get_rmon_stats,
};
