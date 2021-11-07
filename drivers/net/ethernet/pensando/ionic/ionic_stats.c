// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>

#include "ionic.h"
#include "ionic_lif.h"
#include "ionic_stats.h"

static const struct ionic_stat_desc ionic_lif_stats_desc[] = {
	IONIC_LIF_STAT_DESC(tx_packets),
	IONIC_LIF_STAT_DESC(tx_bytes),
	IONIC_LIF_STAT_DESC(rx_packets),
	IONIC_LIF_STAT_DESC(rx_bytes),
	IONIC_LIF_STAT_DESC(tx_tso),
	IONIC_LIF_STAT_DESC(tx_tso_bytes),
	IONIC_LIF_STAT_DESC(tx_csum_none),
	IONIC_LIF_STAT_DESC(tx_csum),
	IONIC_LIF_STAT_DESC(rx_csum_none),
	IONIC_LIF_STAT_DESC(rx_csum_complete),
	IONIC_LIF_STAT_DESC(rx_csum_error),
	IONIC_LIF_STAT_DESC(hw_tx_dropped),
	IONIC_LIF_STAT_DESC(hw_rx_dropped),
	IONIC_LIF_STAT_DESC(hw_rx_over_errors),
	IONIC_LIF_STAT_DESC(hw_rx_missed_errors),
	IONIC_LIF_STAT_DESC(hw_tx_aborted_errors),
};

static const struct ionic_stat_desc ionic_port_stats_desc[] = {
	IONIC_PORT_STAT_DESC(frames_rx_ok),
	IONIC_PORT_STAT_DESC(frames_rx_all),
	IONIC_PORT_STAT_DESC(frames_rx_bad_fcs),
	IONIC_PORT_STAT_DESC(frames_rx_bad_all),
	IONIC_PORT_STAT_DESC(octets_rx_ok),
	IONIC_PORT_STAT_DESC(octets_rx_all),
	IONIC_PORT_STAT_DESC(frames_rx_unicast),
	IONIC_PORT_STAT_DESC(frames_rx_multicast),
	IONIC_PORT_STAT_DESC(frames_rx_broadcast),
	IONIC_PORT_STAT_DESC(frames_rx_pause),
	IONIC_PORT_STAT_DESC(frames_rx_bad_length),
	IONIC_PORT_STAT_DESC(frames_rx_undersized),
	IONIC_PORT_STAT_DESC(frames_rx_oversized),
	IONIC_PORT_STAT_DESC(frames_rx_fragments),
	IONIC_PORT_STAT_DESC(frames_rx_jabber),
	IONIC_PORT_STAT_DESC(frames_rx_pripause),
	IONIC_PORT_STAT_DESC(frames_rx_stomped_crc),
	IONIC_PORT_STAT_DESC(frames_rx_too_long),
	IONIC_PORT_STAT_DESC(frames_rx_vlan_good),
	IONIC_PORT_STAT_DESC(frames_rx_dropped),
	IONIC_PORT_STAT_DESC(frames_rx_less_than_64b),
	IONIC_PORT_STAT_DESC(frames_rx_64b),
	IONIC_PORT_STAT_DESC(frames_rx_65b_127b),
	IONIC_PORT_STAT_DESC(frames_rx_128b_255b),
	IONIC_PORT_STAT_DESC(frames_rx_256b_511b),
	IONIC_PORT_STAT_DESC(frames_rx_512b_1023b),
	IONIC_PORT_STAT_DESC(frames_rx_1024b_1518b),
	IONIC_PORT_STAT_DESC(frames_rx_1519b_2047b),
	IONIC_PORT_STAT_DESC(frames_rx_2048b_4095b),
	IONIC_PORT_STAT_DESC(frames_rx_4096b_8191b),
	IONIC_PORT_STAT_DESC(frames_rx_8192b_9215b),
	IONIC_PORT_STAT_DESC(frames_rx_other),
	IONIC_PORT_STAT_DESC(frames_tx_ok),
	IONIC_PORT_STAT_DESC(frames_tx_all),
	IONIC_PORT_STAT_DESC(frames_tx_bad),
	IONIC_PORT_STAT_DESC(octets_tx_ok),
	IONIC_PORT_STAT_DESC(octets_tx_total),
	IONIC_PORT_STAT_DESC(frames_tx_unicast),
	IONIC_PORT_STAT_DESC(frames_tx_multicast),
	IONIC_PORT_STAT_DESC(frames_tx_broadcast),
	IONIC_PORT_STAT_DESC(frames_tx_pause),
	IONIC_PORT_STAT_DESC(frames_tx_pripause),
	IONIC_PORT_STAT_DESC(frames_tx_vlan),
	IONIC_PORT_STAT_DESC(frames_tx_less_than_64b),
	IONIC_PORT_STAT_DESC(frames_tx_64b),
	IONIC_PORT_STAT_DESC(frames_tx_65b_127b),
	IONIC_PORT_STAT_DESC(frames_tx_128b_255b),
	IONIC_PORT_STAT_DESC(frames_tx_256b_511b),
	IONIC_PORT_STAT_DESC(frames_tx_512b_1023b),
	IONIC_PORT_STAT_DESC(frames_tx_1024b_1518b),
	IONIC_PORT_STAT_DESC(frames_tx_1519b_2047b),
	IONIC_PORT_STAT_DESC(frames_tx_2048b_4095b),
	IONIC_PORT_STAT_DESC(frames_tx_4096b_8191b),
	IONIC_PORT_STAT_DESC(frames_tx_8192b_9215b),
	IONIC_PORT_STAT_DESC(frames_tx_other),
	IONIC_PORT_STAT_DESC(frames_tx_pri_0),
	IONIC_PORT_STAT_DESC(frames_tx_pri_1),
	IONIC_PORT_STAT_DESC(frames_tx_pri_2),
	IONIC_PORT_STAT_DESC(frames_tx_pri_3),
	IONIC_PORT_STAT_DESC(frames_tx_pri_4),
	IONIC_PORT_STAT_DESC(frames_tx_pri_5),
	IONIC_PORT_STAT_DESC(frames_tx_pri_6),
	IONIC_PORT_STAT_DESC(frames_tx_pri_7),
	IONIC_PORT_STAT_DESC(frames_rx_pri_0),
	IONIC_PORT_STAT_DESC(frames_rx_pri_1),
	IONIC_PORT_STAT_DESC(frames_rx_pri_2),
	IONIC_PORT_STAT_DESC(frames_rx_pri_3),
	IONIC_PORT_STAT_DESC(frames_rx_pri_4),
	IONIC_PORT_STAT_DESC(frames_rx_pri_5),
	IONIC_PORT_STAT_DESC(frames_rx_pri_6),
	IONIC_PORT_STAT_DESC(frames_rx_pri_7),
	IONIC_PORT_STAT_DESC(tx_pripause_0_1us_count),
	IONIC_PORT_STAT_DESC(tx_pripause_1_1us_count),
	IONIC_PORT_STAT_DESC(tx_pripause_2_1us_count),
	IONIC_PORT_STAT_DESC(tx_pripause_3_1us_count),
	IONIC_PORT_STAT_DESC(tx_pripause_4_1us_count),
	IONIC_PORT_STAT_DESC(tx_pripause_5_1us_count),
	IONIC_PORT_STAT_DESC(tx_pripause_6_1us_count),
	IONIC_PORT_STAT_DESC(tx_pripause_7_1us_count),
	IONIC_PORT_STAT_DESC(rx_pripause_0_1us_count),
	IONIC_PORT_STAT_DESC(rx_pripause_1_1us_count),
	IONIC_PORT_STAT_DESC(rx_pripause_2_1us_count),
	IONIC_PORT_STAT_DESC(rx_pripause_3_1us_count),
	IONIC_PORT_STAT_DESC(rx_pripause_4_1us_count),
	IONIC_PORT_STAT_DESC(rx_pripause_5_1us_count),
	IONIC_PORT_STAT_DESC(rx_pripause_6_1us_count),
	IONIC_PORT_STAT_DESC(rx_pripause_7_1us_count),
	IONIC_PORT_STAT_DESC(rx_pause_1us_count),
	IONIC_PORT_STAT_DESC(frames_tx_truncated),
};

static const struct ionic_stat_desc ionic_tx_stats_desc[] = {
	IONIC_TX_STAT_DESC(pkts),
	IONIC_TX_STAT_DESC(bytes),
	IONIC_TX_STAT_DESC(clean),
	IONIC_TX_STAT_DESC(dma_map_err),
	IONIC_TX_STAT_DESC(linearize),
	IONIC_TX_STAT_DESC(frags),
	IONIC_TX_STAT_DESC(tso),
	IONIC_TX_STAT_DESC(tso_bytes),
	IONIC_TX_STAT_DESC(hwstamp_valid),
	IONIC_TX_STAT_DESC(hwstamp_invalid),
	IONIC_TX_STAT_DESC(csum_none),
	IONIC_TX_STAT_DESC(csum),
	IONIC_TX_STAT_DESC(vlan_inserted),
};

static const struct ionic_stat_desc ionic_rx_stats_desc[] = {
	IONIC_RX_STAT_DESC(pkts),
	IONIC_RX_STAT_DESC(bytes),
	IONIC_RX_STAT_DESC(dma_map_err),
	IONIC_RX_STAT_DESC(alloc_err),
	IONIC_RX_STAT_DESC(csum_none),
	IONIC_RX_STAT_DESC(csum_complete),
	IONIC_RX_STAT_DESC(csum_error),
	IONIC_RX_STAT_DESC(hwstamp_valid),
	IONIC_RX_STAT_DESC(hwstamp_invalid),
	IONIC_RX_STAT_DESC(dropped),
	IONIC_RX_STAT_DESC(vlan_stripped),
};

static const struct ionic_stat_desc ionic_txq_stats_desc[] = {
	IONIC_TX_Q_STAT_DESC(stop),
	IONIC_TX_Q_STAT_DESC(wake),
	IONIC_TX_Q_STAT_DESC(drop),
	IONIC_TX_Q_STAT_DESC(dbell_count),
};

static const struct ionic_stat_desc ionic_dbg_cq_stats_desc[] = {
	IONIC_CQ_STAT_DESC(compl_count),
};

static const struct ionic_stat_desc ionic_dbg_intr_stats_desc[] = {
	IONIC_INTR_STAT_DESC(rearm_count),
};

static const struct ionic_stat_desc ionic_dbg_napi_stats_desc[] = {
	IONIC_NAPI_STAT_DESC(poll_count),
};

#define IONIC_NUM_LIF_STATS ARRAY_SIZE(ionic_lif_stats_desc)
#define IONIC_NUM_PORT_STATS ARRAY_SIZE(ionic_port_stats_desc)
#define IONIC_NUM_TX_STATS ARRAY_SIZE(ionic_tx_stats_desc)
#define IONIC_NUM_RX_STATS ARRAY_SIZE(ionic_rx_stats_desc)
#define IONIC_NUM_TX_Q_STATS ARRAY_SIZE(ionic_txq_stats_desc)
#define IONIC_NUM_DBG_CQ_STATS ARRAY_SIZE(ionic_dbg_cq_stats_desc)
#define IONIC_NUM_DBG_INTR_STATS ARRAY_SIZE(ionic_dbg_intr_stats_desc)
#define IONIC_NUM_DBG_NAPI_STATS ARRAY_SIZE(ionic_dbg_napi_stats_desc)

#define MAX_Q(lif)   ((lif)->netdev->real_num_tx_queues)

static void ionic_add_lif_txq_stats(struct ionic_lif *lif, int q_num,
				    struct ionic_lif_sw_stats *stats)
{
	struct ionic_tx_stats *txstats = &lif->txqstats[q_num];

	stats->tx_packets += txstats->pkts;
	stats->tx_bytes += txstats->bytes;
	stats->tx_tso += txstats->tso;
	stats->tx_tso_bytes += txstats->tso_bytes;
	stats->tx_csum_none += txstats->csum_none;
	stats->tx_csum += txstats->csum;
	stats->tx_hwstamp_valid += txstats->hwstamp_valid;
	stats->tx_hwstamp_invalid += txstats->hwstamp_invalid;
}

static void ionic_add_lif_rxq_stats(struct ionic_lif *lif, int q_num,
				    struct ionic_lif_sw_stats *stats)
{
	struct ionic_rx_stats *rxstats = &lif->rxqstats[q_num];

	stats->rx_packets += rxstats->pkts;
	stats->rx_bytes += rxstats->bytes;
	stats->rx_csum_none += rxstats->csum_none;
	stats->rx_csum_complete += rxstats->csum_complete;
	stats->rx_csum_error += rxstats->csum_error;
	stats->rx_hwstamp_valid += rxstats->hwstamp_valid;
	stats->rx_hwstamp_invalid += rxstats->hwstamp_invalid;
}

static void ionic_get_lif_stats(struct ionic_lif *lif,
				struct ionic_lif_sw_stats *stats)
{
	struct rtnl_link_stats64 ns;
	int q_num;

	memset(stats, 0, sizeof(*stats));

	for (q_num = 0; q_num < MAX_Q(lif); q_num++) {
		ionic_add_lif_txq_stats(lif, q_num, stats);
		ionic_add_lif_rxq_stats(lif, q_num, stats);
	}

	if (lif->hwstamp_txq)
		ionic_add_lif_txq_stats(lif, lif->hwstamp_txq->q.index, stats);

	if (lif->hwstamp_rxq)
		ionic_add_lif_rxq_stats(lif, lif->hwstamp_rxq->q.index, stats);

	ionic_get_stats64(lif->netdev, &ns);
	stats->hw_tx_dropped = ns.tx_dropped;
	stats->hw_rx_dropped = ns.rx_dropped;
	stats->hw_rx_over_errors = ns.rx_over_errors;
	stats->hw_rx_missed_errors = ns.rx_missed_errors;
	stats->hw_tx_aborted_errors = ns.tx_aborted_errors;
}

static u64 ionic_sw_stats_get_count(struct ionic_lif *lif)
{
	u64 total = 0, tx_queues = MAX_Q(lif), rx_queues = MAX_Q(lif);

	if (lif->hwstamp_txq)
		tx_queues += 1;

	if (lif->hwstamp_rxq)
		rx_queues += 1;

	total += IONIC_NUM_LIF_STATS;
	total += IONIC_NUM_PORT_STATS;

	total += tx_queues * IONIC_NUM_TX_STATS;
	total += rx_queues * IONIC_NUM_RX_STATS;

	if (test_bit(IONIC_LIF_F_UP, lif->state) &&
	    test_bit(IONIC_LIF_F_SW_DEBUG_STATS, lif->state)) {
		/* tx debug stats */
		total += tx_queues * (IONIC_NUM_DBG_CQ_STATS +
				      IONIC_NUM_TX_Q_STATS +
				      IONIC_NUM_DBG_INTR_STATS +
				      IONIC_MAX_NUM_SG_CNTR);

		/* rx debug stats */
		total += rx_queues * (IONIC_NUM_DBG_CQ_STATS +
				      IONIC_NUM_DBG_INTR_STATS +
				      IONIC_NUM_DBG_NAPI_STATS +
				      IONIC_MAX_NUM_NAPI_CNTR);
	}

	return total;
}

static void ionic_sw_stats_get_tx_strings(struct ionic_lif *lif, u8 **buf,
					  int q_num)
{
	int i;

	for (i = 0; i < IONIC_NUM_TX_STATS; i++)
		ethtool_sprintf(buf, "tx_%d_%s", q_num,
				ionic_tx_stats_desc[i].name);

	if (!test_bit(IONIC_LIF_F_UP, lif->state) ||
	    !test_bit(IONIC_LIF_F_SW_DEBUG_STATS, lif->state))
		return;

	for (i = 0; i < IONIC_NUM_TX_Q_STATS; i++)
		ethtool_sprintf(buf, "txq_%d_%s", q_num,
				ionic_txq_stats_desc[i].name);
	for (i = 0; i < IONIC_NUM_DBG_CQ_STATS; i++)
		ethtool_sprintf(buf, "txq_%d_cq_%s", q_num,
				ionic_dbg_cq_stats_desc[i].name);
	for (i = 0; i < IONIC_NUM_DBG_INTR_STATS; i++)
		ethtool_sprintf(buf, "txq_%d_intr_%s", q_num,
				ionic_dbg_intr_stats_desc[i].name);
	for (i = 0; i < IONIC_MAX_NUM_SG_CNTR; i++)
		ethtool_sprintf(buf, "txq_%d_sg_cntr_%d", q_num, i);
}

static void ionic_sw_stats_get_rx_strings(struct ionic_lif *lif, u8 **buf,
					  int q_num)
{
	int i;

	for (i = 0; i < IONIC_NUM_RX_STATS; i++)
		ethtool_sprintf(buf, "rx_%d_%s", q_num,
				ionic_rx_stats_desc[i].name);

	if (!test_bit(IONIC_LIF_F_UP, lif->state) ||
	    !test_bit(IONIC_LIF_F_SW_DEBUG_STATS, lif->state))
		return;

	for (i = 0; i < IONIC_NUM_DBG_CQ_STATS; i++)
		ethtool_sprintf(buf, "rxq_%d_cq_%s", q_num,
				ionic_dbg_cq_stats_desc[i].name);
	for (i = 0; i < IONIC_NUM_DBG_INTR_STATS; i++)
		ethtool_sprintf(buf, "rxq_%d_intr_%s", q_num,
				ionic_dbg_intr_stats_desc[i].name);
	for (i = 0; i < IONIC_NUM_DBG_NAPI_STATS; i++)
		ethtool_sprintf(buf, "rxq_%d_napi_%s", q_num,
				ionic_dbg_napi_stats_desc[i].name);
	for (i = 0; i < IONIC_MAX_NUM_NAPI_CNTR; i++)
		ethtool_sprintf(buf, "rxq_%d_napi_work_done_%d", q_num, i);
}

static void ionic_sw_stats_get_strings(struct ionic_lif *lif, u8 **buf)
{
	int i, q_num;

	for (i = 0; i < IONIC_NUM_LIF_STATS; i++)
		ethtool_sprintf(buf, ionic_lif_stats_desc[i].name);

	for (i = 0; i < IONIC_NUM_PORT_STATS; i++)
		ethtool_sprintf(buf, ionic_port_stats_desc[i].name);

	for (q_num = 0; q_num < MAX_Q(lif); q_num++)
		ionic_sw_stats_get_tx_strings(lif, buf, q_num);

	if (lif->hwstamp_txq)
		ionic_sw_stats_get_tx_strings(lif, buf, lif->hwstamp_txq->q.index);

	for (q_num = 0; q_num < MAX_Q(lif); q_num++)
		ionic_sw_stats_get_rx_strings(lif, buf, q_num);

	if (lif->hwstamp_rxq)
		ionic_sw_stats_get_rx_strings(lif, buf, lif->hwstamp_rxq->q.index);
}

static void ionic_sw_stats_get_txq_values(struct ionic_lif *lif, u64 **buf,
					  int q_num)
{
	struct ionic_tx_stats *txstats;
	struct ionic_qcq *txqcq;
	int i;

	txstats = &lif->txqstats[q_num];

	for (i = 0; i < IONIC_NUM_TX_STATS; i++) {
		**buf = IONIC_READ_STAT64(txstats, &ionic_tx_stats_desc[i]);
		(*buf)++;
	}

	if (!test_bit(IONIC_LIF_F_UP, lif->state) ||
	    !test_bit(IONIC_LIF_F_SW_DEBUG_STATS, lif->state))
		return;

	txqcq = lif->txqcqs[q_num];
	for (i = 0; i < IONIC_NUM_TX_Q_STATS; i++) {
		**buf = IONIC_READ_STAT64(&txqcq->q,
					  &ionic_txq_stats_desc[i]);
		(*buf)++;
	}
	for (i = 0; i < IONIC_NUM_DBG_CQ_STATS; i++) {
		**buf = IONIC_READ_STAT64(&txqcq->cq,
					  &ionic_dbg_cq_stats_desc[i]);
		(*buf)++;
	}
	for (i = 0; i < IONIC_NUM_DBG_INTR_STATS; i++) {
		**buf = IONIC_READ_STAT64(&txqcq->intr,
					  &ionic_dbg_intr_stats_desc[i]);
		(*buf)++;
	}
	for (i = 0; i < IONIC_MAX_NUM_SG_CNTR; i++) {
		**buf = txstats->sg_cntr[i];
		(*buf)++;
	}
}

static void ionic_sw_stats_get_rxq_values(struct ionic_lif *lif, u64 **buf,
					  int q_num)
{
	struct ionic_rx_stats *rxstats;
	struct ionic_qcq *rxqcq;
	int i;

	rxstats = &lif->rxqstats[q_num];

	for (i = 0; i < IONIC_NUM_RX_STATS; i++) {
		**buf = IONIC_READ_STAT64(rxstats, &ionic_rx_stats_desc[i]);
		(*buf)++;
	}

	if (!test_bit(IONIC_LIF_F_UP, lif->state) ||
	    !test_bit(IONIC_LIF_F_SW_DEBUG_STATS, lif->state))
		return;

	rxqcq = lif->rxqcqs[q_num];
	for (i = 0; i < IONIC_NUM_DBG_CQ_STATS; i++) {
		**buf = IONIC_READ_STAT64(&rxqcq->cq,
					  &ionic_dbg_cq_stats_desc[i]);
		(*buf)++;
	}
	for (i = 0; i < IONIC_NUM_DBG_INTR_STATS; i++) {
		**buf = IONIC_READ_STAT64(&rxqcq->intr,
					  &ionic_dbg_intr_stats_desc[i]);
		(*buf)++;
	}
	for (i = 0; i < IONIC_NUM_DBG_NAPI_STATS; i++) {
		**buf = IONIC_READ_STAT64(&rxqcq->napi_stats,
					  &ionic_dbg_napi_stats_desc[i]);
		(*buf)++;
	}
	for (i = 0; i < IONIC_MAX_NUM_NAPI_CNTR; i++) {
		**buf = rxqcq->napi_stats.work_done_cntr[i];
		(*buf)++;
	}
}

static void ionic_sw_stats_get_values(struct ionic_lif *lif, u64 **buf)
{
	struct ionic_port_stats *port_stats;
	struct ionic_lif_sw_stats lif_stats;
	int i, q_num;

	ionic_get_lif_stats(lif, &lif_stats);

	for (i = 0; i < IONIC_NUM_LIF_STATS; i++) {
		**buf = IONIC_READ_STAT64(&lif_stats, &ionic_lif_stats_desc[i]);
		(*buf)++;
	}

	port_stats = &lif->ionic->idev.port_info->stats;
	for (i = 0; i < IONIC_NUM_PORT_STATS; i++) {
		**buf = IONIC_READ_STAT_LE64(port_stats,
					     &ionic_port_stats_desc[i]);
		(*buf)++;
	}

	for (q_num = 0; q_num < MAX_Q(lif); q_num++)
		ionic_sw_stats_get_txq_values(lif, buf, q_num);

	if (lif->hwstamp_txq)
		ionic_sw_stats_get_txq_values(lif, buf, lif->hwstamp_txq->q.index);

	for (q_num = 0; q_num < MAX_Q(lif); q_num++)
		ionic_sw_stats_get_rxq_values(lif, buf, q_num);

	if (lif->hwstamp_rxq)
		ionic_sw_stats_get_rxq_values(lif, buf, lif->hwstamp_rxq->q.index);
}

const struct ionic_stats_group_intf ionic_stats_groups[] = {
	/* SW Stats group */
	{
		.get_strings = ionic_sw_stats_get_strings,
		.get_values = ionic_sw_stats_get_values,
		.get_count = ionic_sw_stats_get_count,
	},
	/* Add more stat groups here */
};

const int ionic_num_stats_grps = ARRAY_SIZE(ionic_stats_groups);
