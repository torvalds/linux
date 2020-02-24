// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

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
	IONIC_LIF_STAT_DESC(tx_no_csum),
	IONIC_LIF_STAT_DESC(tx_csum),
	IONIC_LIF_STAT_DESC(rx_csum_none),
	IONIC_LIF_STAT_DESC(rx_csum_complete),
	IONIC_LIF_STAT_DESC(rx_csum_error),
};

static const struct ionic_stat_desc ionic_tx_stats_desc[] = {
	IONIC_TX_STAT_DESC(pkts),
	IONIC_TX_STAT_DESC(bytes),
	IONIC_TX_STAT_DESC(clean),
	IONIC_TX_STAT_DESC(dma_map_err),
	IONIC_TX_STAT_DESC(linearize),
	IONIC_TX_STAT_DESC(frags),
};

static const struct ionic_stat_desc ionic_rx_stats_desc[] = {
	IONIC_RX_STAT_DESC(pkts),
	IONIC_RX_STAT_DESC(bytes),
	IONIC_RX_STAT_DESC(dma_map_err),
	IONIC_RX_STAT_DESC(alloc_err),
	IONIC_RX_STAT_DESC(csum_none),
	IONIC_RX_STAT_DESC(csum_complete),
	IONIC_RX_STAT_DESC(csum_error),
	IONIC_RX_STAT_DESC(dropped),
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
#define IONIC_NUM_TX_STATS ARRAY_SIZE(ionic_tx_stats_desc)
#define IONIC_NUM_RX_STATS ARRAY_SIZE(ionic_rx_stats_desc)
#define IONIC_NUM_TX_Q_STATS ARRAY_SIZE(ionic_txq_stats_desc)
#define IONIC_NUM_DBG_CQ_STATS ARRAY_SIZE(ionic_dbg_cq_stats_desc)
#define IONIC_NUM_DBG_INTR_STATS ARRAY_SIZE(ionic_dbg_intr_stats_desc)
#define IONIC_NUM_DBG_NAPI_STATS ARRAY_SIZE(ionic_dbg_napi_stats_desc)

#define MAX_Q(lif)   ((lif)->netdev->real_num_tx_queues)

static void ionic_get_lif_stats(struct ionic_lif *lif,
				struct ionic_lif_sw_stats *stats)
{
	struct ionic_tx_stats *tstats;
	struct ionic_rx_stats *rstats;
	struct ionic_qcq *txqcq;
	struct ionic_qcq *rxqcq;
	int q_num;

	memset(stats, 0, sizeof(*stats));

	for (q_num = 0; q_num < MAX_Q(lif); q_num++) {
		txqcq = lif_to_txqcq(lif, q_num);
		if (txqcq && txqcq->stats) {
			tstats = &txqcq->stats->tx;
			stats->tx_packets += tstats->pkts;
			stats->tx_bytes += tstats->bytes;
			stats->tx_tso += tstats->tso;
			stats->tx_no_csum += tstats->no_csum;
			stats->tx_csum += tstats->csum;
		}

		rxqcq = lif_to_rxqcq(lif, q_num);
		if (rxqcq && rxqcq->stats) {
			rstats = &rxqcq->stats->rx;
			stats->rx_packets += rstats->pkts;
			stats->rx_bytes += rstats->bytes;
			stats->rx_csum_none += rstats->csum_none;
			stats->rx_csum_complete += rstats->csum_complete;
			stats->rx_csum_error += rstats->csum_error;
		}
	}
}

static u64 ionic_sw_stats_get_count(struct ionic_lif *lif)
{
	u64 total = 0;

	/* lif stats */
	total += IONIC_NUM_LIF_STATS;

	/* tx stats */
	total += MAX_Q(lif) * IONIC_NUM_TX_STATS;

	/* rx stats */
	total += MAX_Q(lif) * IONIC_NUM_RX_STATS;

	if (test_bit(IONIC_LIF_UP, lif->state) &&
	    test_bit(IONIC_LIF_SW_DEBUG_STATS, lif->state)) {
		/* tx debug stats */
		total += MAX_Q(lif) * (IONIC_NUM_DBG_CQ_STATS +
				      IONIC_NUM_TX_Q_STATS +
				      IONIC_NUM_DBG_INTR_STATS +
				      IONIC_MAX_NUM_SG_CNTR);

		/* rx debug stats */
		total += MAX_Q(lif) * (IONIC_NUM_DBG_CQ_STATS +
				      IONIC_NUM_DBG_INTR_STATS +
				      IONIC_NUM_DBG_NAPI_STATS +
				      IONIC_MAX_NUM_NAPI_CNTR);
	}

	return total;
}

static void ionic_sw_stats_get_strings(struct ionic_lif *lif, u8 **buf)
{
	int i, q_num;

	for (i = 0; i < IONIC_NUM_LIF_STATS; i++) {
		snprintf(*buf, ETH_GSTRING_LEN, ionic_lif_stats_desc[i].name);
		*buf += ETH_GSTRING_LEN;
	}
	for (q_num = 0; q_num < MAX_Q(lif); q_num++) {
		for (i = 0; i < IONIC_NUM_TX_STATS; i++) {
			snprintf(*buf, ETH_GSTRING_LEN, "tx_%d_%s",
				 q_num, ionic_tx_stats_desc[i].name);
			*buf += ETH_GSTRING_LEN;
		}

		if (test_bit(IONIC_LIF_UP, lif->state) &&
		    test_bit(IONIC_LIF_SW_DEBUG_STATS, lif->state)) {
			for (i = 0; i < IONIC_NUM_TX_Q_STATS; i++) {
				snprintf(*buf, ETH_GSTRING_LEN,
					 "txq_%d_%s",
					 q_num,
					 ionic_txq_stats_desc[i].name);
				*buf += ETH_GSTRING_LEN;
			}
			for (i = 0; i < IONIC_NUM_DBG_CQ_STATS; i++) {
				snprintf(*buf, ETH_GSTRING_LEN,
					 "txq_%d_cq_%s",
					 q_num,
					 ionic_dbg_cq_stats_desc[i].name);
				*buf += ETH_GSTRING_LEN;
			}
			for (i = 0; i < IONIC_NUM_DBG_INTR_STATS; i++) {
				snprintf(*buf, ETH_GSTRING_LEN,
					 "txq_%d_intr_%s",
					 q_num,
					 ionic_dbg_intr_stats_desc[i].name);
				*buf += ETH_GSTRING_LEN;
			}
			for (i = 0; i < IONIC_MAX_NUM_SG_CNTR; i++) {
				snprintf(*buf, ETH_GSTRING_LEN,
					 "txq_%d_sg_cntr_%d",
					 q_num, i);
				*buf += ETH_GSTRING_LEN;
			}
		}
	}
	for (q_num = 0; q_num < MAX_Q(lif); q_num++) {
		for (i = 0; i < IONIC_NUM_RX_STATS; i++) {
			snprintf(*buf, ETH_GSTRING_LEN,
				 "rx_%d_%s",
				 q_num, ionic_rx_stats_desc[i].name);
			*buf += ETH_GSTRING_LEN;
		}

		if (test_bit(IONIC_LIF_UP, lif->state) &&
		    test_bit(IONIC_LIF_SW_DEBUG_STATS, lif->state)) {
			for (i = 0; i < IONIC_NUM_DBG_CQ_STATS; i++) {
				snprintf(*buf, ETH_GSTRING_LEN,
					 "rxq_%d_cq_%s",
					 q_num,
					 ionic_dbg_cq_stats_desc[i].name);
				*buf += ETH_GSTRING_LEN;
			}
			for (i = 0; i < IONIC_NUM_DBG_INTR_STATS; i++) {
				snprintf(*buf, ETH_GSTRING_LEN,
					 "rxq_%d_intr_%s",
					 q_num,
					 ionic_dbg_intr_stats_desc[i].name);
				*buf += ETH_GSTRING_LEN;
			}
			for (i = 0; i < IONIC_NUM_DBG_NAPI_STATS; i++) {
				snprintf(*buf, ETH_GSTRING_LEN,
					 "rxq_%d_napi_%s",
					 q_num,
					 ionic_dbg_napi_stats_desc[i].name);
				*buf += ETH_GSTRING_LEN;
			}
			for (i = 0; i < IONIC_MAX_NUM_NAPI_CNTR; i++) {
				snprintf(*buf, ETH_GSTRING_LEN,
					 "rxq_%d_napi_work_done_%d",
					 q_num, i);
				*buf += ETH_GSTRING_LEN;
			}
		}
	}
}

static void ionic_sw_stats_get_values(struct ionic_lif *lif, u64 **buf)
{
	struct ionic_lif_sw_stats lif_stats;
	struct ionic_qcq *txqcq, *rxqcq;
	struct ionic_tx_stats *txstats;
	struct ionic_rx_stats *rxstats;
	int i, q_num;

	ionic_get_lif_stats(lif, &lif_stats);

	for (i = 0; i < IONIC_NUM_LIF_STATS; i++) {
		**buf = IONIC_READ_STAT64(&lif_stats, &ionic_lif_stats_desc[i]);
		(*buf)++;
	}

	for (q_num = 0; q_num < MAX_Q(lif); q_num++) {
		txstats = &lif_to_txstats(lif, q_num);

		for (i = 0; i < IONIC_NUM_TX_STATS; i++) {
			**buf = IONIC_READ_STAT64(txstats,
						  &ionic_tx_stats_desc[i]);
			(*buf)++;
		}

		if (test_bit(IONIC_LIF_UP, lif->state) &&
		    test_bit(IONIC_LIF_SW_DEBUG_STATS, lif->state)) {
			txqcq = lif_to_txqcq(lif, q_num);
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
	}

	for (q_num = 0; q_num < MAX_Q(lif); q_num++) {
		rxstats = &lif_to_rxstats(lif, q_num);

		for (i = 0; i < IONIC_NUM_RX_STATS; i++) {
			**buf = IONIC_READ_STAT64(rxstats,
						  &ionic_rx_stats_desc[i]);
			(*buf)++;
		}

		if (test_bit(IONIC_LIF_UP, lif->state) &&
		    test_bit(IONIC_LIF_SW_DEBUG_STATS, lif->state)) {
			rxqcq = lif_to_rxqcq(lif, q_num);
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
	}
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
