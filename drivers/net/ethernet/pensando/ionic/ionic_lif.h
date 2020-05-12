/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#ifndef _IONIC_LIF_H_
#define _IONIC_LIF_H_

#include <linux/pci.h>
#include "ionic_rx_filter.h"

#define IONIC_ADMINQ_LENGTH	16	/* must be a power of two */
#define IONIC_NOTIFYQ_LENGTH	64	/* must be a power of two */

#define IONIC_MAX_NUM_NAPI_CNTR		(NAPI_POLL_WEIGHT + 1)
#define IONIC_MAX_NUM_SG_CNTR		(IONIC_TX_MAX_SG_ELEMS + 1)
#define IONIC_RX_COPYBREAK_DEFAULT	256

struct ionic_tx_stats {
	u64 dma_map_err;
	u64 pkts;
	u64 bytes;
	u64 clean;
	u64 linearize;
	u64 csum_none;
	u64 csum;
	u64 crc32_csum;
	u64 tso;
	u64 tso_bytes;
	u64 frags;
	u64 vlan_inserted;
	u64 sg_cntr[IONIC_MAX_NUM_SG_CNTR];
};

struct ionic_rx_stats {
	u64 dma_map_err;
	u64 alloc_err;
	u64 pkts;
	u64 bytes;
	u64 csum_none;
	u64 csum_complete;
	u64 csum_error;
	u64 buffers_posted;
	u64 dropped;
	u64 vlan_stripped;
};

#define IONIC_QCQ_F_INITED		BIT(0)
#define IONIC_QCQ_F_SG			BIT(1)
#define IONIC_QCQ_F_INTR		BIT(2)
#define IONIC_QCQ_F_TX_STATS		BIT(3)
#define IONIC_QCQ_F_RX_STATS		BIT(4)
#define IONIC_QCQ_F_NOTIFYQ		BIT(5)

struct ionic_napi_stats {
	u64 poll_count;
	u64 work_done_cntr[IONIC_MAX_NUM_NAPI_CNTR];
};

struct ionic_q_stats {
	union {
		struct ionic_tx_stats tx;
		struct ionic_rx_stats rx;
	};
};

struct ionic_qcq {
	void *base;
	dma_addr_t base_pa;
	unsigned int total_size;
	struct ionic_queue q;
	struct ionic_cq cq;
	struct ionic_intr_info intr;
	struct napi_struct napi;
	struct ionic_napi_stats napi_stats;
	struct ionic_q_stats *stats;
	unsigned int flags;
	struct dentry *dentry;
};

struct ionic_qcqst {
	struct ionic_qcq *qcq;
	struct ionic_q_stats *stats;
};

#define q_to_qcq(q)		container_of(q, struct ionic_qcq, q)
#define q_to_tx_stats(q)	(&q_to_qcq(q)->stats->tx)
#define q_to_rx_stats(q)	(&q_to_qcq(q)->stats->rx)
#define napi_to_qcq(napi)	container_of(napi, struct ionic_qcq, napi)
#define napi_to_cq(napi)	(&napi_to_qcq(napi)->cq)

enum ionic_deferred_work_type {
	IONIC_DW_TYPE_RX_MODE,
	IONIC_DW_TYPE_RX_ADDR_ADD,
	IONIC_DW_TYPE_RX_ADDR_DEL,
	IONIC_DW_TYPE_LINK_STATUS,
	IONIC_DW_TYPE_LIF_RESET,
};

struct ionic_deferred_work {
	struct list_head list;
	enum ionic_deferred_work_type type;
	union {
		unsigned int rx_mode;
		u8 addr[ETH_ALEN];
		u8 fw_status;
	};
};

struct ionic_deferred {
	spinlock_t lock;		/* lock for deferred work list */
	struct list_head list;
	struct work_struct work;
};

struct ionic_lif_sw_stats {
	u64 tx_packets;
	u64 tx_bytes;
	u64 rx_packets;
	u64 rx_bytes;
	u64 tx_tso;
	u64 tx_tso_bytes;
	u64 tx_csum_none;
	u64 tx_csum;
	u64 rx_csum_none;
	u64 rx_csum_complete;
	u64 rx_csum_error;
	u64 hw_tx_dropped;
	u64 hw_rx_dropped;
	u64 hw_rx_over_errors;
	u64 hw_rx_missed_errors;
	u64 hw_tx_aborted_errors;
};

enum ionic_lif_state_flags {
	IONIC_LIF_F_INITED,
	IONIC_LIF_F_SW_DEBUG_STATS,
	IONIC_LIF_F_UP,
	IONIC_LIF_F_LINK_CHECK_REQUESTED,
	IONIC_LIF_F_QUEUE_RESET,
	IONIC_LIF_F_FW_RESET,

	/* leave this as last */
	IONIC_LIF_F_STATE_SIZE
};

struct ionic_qtype_info {
	u8  version;
	u8  supported;
	u64 features;
	u16 desc_sz;
	u16 comp_sz;
	u16 sg_desc_sz;
	u16 max_sg_elems;
	u16 sg_desc_stride;
};

#define IONIC_LIF_NAME_MAX_SZ		32
struct ionic_lif {
	char name[IONIC_LIF_NAME_MAX_SZ];
	struct list_head list;
	struct net_device *netdev;
	DECLARE_BITMAP(state, IONIC_LIF_F_STATE_SIZE);
	struct ionic *ionic;
	bool registered;
	unsigned int index;
	unsigned int hw_index;
	unsigned int kern_pid;
	u64 __iomem *kern_dbpage;
	spinlock_t adminq_lock;		/* lock for AdminQ operations */
	struct ionic_qcq *adminqcq;
	struct ionic_qcq *notifyqcq;
	struct ionic_qcqst *txqcqs;
	struct ionic_qcqst *rxqcqs;
	u64 last_eid;
	unsigned int neqs;
	unsigned int nxqs;
	unsigned int ntxq_descs;
	unsigned int nrxq_descs;
	u32 rx_copybreak;
	unsigned int rx_mode;
	u64 hw_features;
	bool mc_overflow;
	unsigned int nmcast;
	bool uc_overflow;
	u16 lif_type;
	unsigned int nucast;

	struct ionic_lif_info *info;
	dma_addr_t info_pa;
	u32 info_sz;
	struct ionic_qtype_info qtype_info[IONIC_QTYPE_MAX];

	u16 rss_types;
	u8 rss_hash_key[IONIC_RSS_HASH_KEY_SIZE];
	u8 *rss_ind_tbl;
	dma_addr_t rss_ind_tbl_pa;
	u32 rss_ind_tbl_sz;

	struct ionic_rx_filters rx_filters;
	struct ionic_deferred deferred;
	unsigned long *dbid_inuse;
	unsigned int dbid_count;
	struct dentry *dentry;
	u32 rx_coalesce_usecs;		/* what the user asked for */
	u32 rx_coalesce_hw;		/* what the hw is using */

	struct work_struct tx_timeout_work;
};

#define lif_to_txqcq(lif, i)	((lif)->txqcqs[i].qcq)
#define lif_to_rxqcq(lif, i)	((lif)->rxqcqs[i].qcq)
#define lif_to_txstats(lif, i)	((lif)->txqcqs[i].stats->tx)
#define lif_to_rxstats(lif, i)	((lif)->rxqcqs[i].stats->rx)
#define lif_to_txq(lif, i)	(&lif_to_txqcq((lif), i)->q)
#define lif_to_rxq(lif, i)	(&lif_to_txqcq((lif), i)->q)

/* return 0 if successfully set the bit, else non-zero */
static inline int ionic_wait_for_bit(struct ionic_lif *lif, int bitname)
{
	return wait_on_bit_lock(lif->state, bitname, TASK_INTERRUPTIBLE);
}

static inline u32 ionic_coal_usec_to_hw(struct ionic *ionic, u32 usecs)
{
	u32 mult = le32_to_cpu(ionic->ident.dev.intr_coal_mult);
	u32 div = le32_to_cpu(ionic->ident.dev.intr_coal_div);

	/* Div-by-zero should never be an issue, but check anyway */
	if (!div || !mult)
		return 0;

	/* Round up in case usecs is close to the next hw unit */
	usecs += (div / mult) >> 1;

	/* Convert from usecs to device units */
	return (usecs * mult) / div;
}

static inline u32 ionic_coal_hw_to_usec(struct ionic *ionic, u32 units)
{
	u32 mult = le32_to_cpu(ionic->ident.dev.intr_coal_mult);
	u32 div = le32_to_cpu(ionic->ident.dev.intr_coal_div);

	/* Div-by-zero should never be an issue, but check anyway */
	if (!div || !mult)
		return 0;

	/* Convert from device units to usec */
	return (units * div) / mult;
}

void ionic_link_status_check_request(struct ionic_lif *lif);
void ionic_get_stats64(struct net_device *netdev,
		       struct rtnl_link_stats64 *ns);
void ionic_lif_deferred_enqueue(struct ionic_deferred *def,
				struct ionic_deferred_work *work);
int ionic_lifs_alloc(struct ionic *ionic);
void ionic_lifs_free(struct ionic *ionic);
void ionic_lifs_deinit(struct ionic *ionic);
int ionic_lifs_init(struct ionic *ionic);
int ionic_lifs_register(struct ionic *ionic);
void ionic_lifs_unregister(struct ionic *ionic);
int ionic_lif_identify(struct ionic *ionic, u8 lif_type,
		       union ionic_lif_identity *lif_ident);
int ionic_lifs_size(struct ionic *ionic);
int ionic_lif_rss_config(struct ionic_lif *lif, u16 types,
			 const u8 *key, const u32 *indir);

int ionic_open(struct net_device *netdev);
int ionic_stop(struct net_device *netdev);
int ionic_reset_queues(struct ionic_lif *lif);

static inline void debug_stats_txq_post(struct ionic_qcq *qcq,
					struct ionic_txq_desc *desc, bool dbell)
{
	u8 num_sg_elems = ((le64_to_cpu(desc->cmd) >> IONIC_TXQ_DESC_NSGE_SHIFT)
						& IONIC_TXQ_DESC_NSGE_MASK);

	qcq->q.dbell_count += dbell;

	if (num_sg_elems > (IONIC_MAX_NUM_SG_CNTR - 1))
		num_sg_elems = IONIC_MAX_NUM_SG_CNTR - 1;

	qcq->stats->tx.sg_cntr[num_sg_elems]++;
}

static inline void debug_stats_napi_poll(struct ionic_qcq *qcq,
					 unsigned int work_done)
{
	qcq->napi_stats.poll_count++;

	if (work_done > (IONIC_MAX_NUM_NAPI_CNTR - 1))
		work_done = IONIC_MAX_NUM_NAPI_CNTR - 1;

	qcq->napi_stats.work_done_cntr[work_done]++;
}

#define DEBUG_STATS_CQE_CNT(cq)		((cq)->compl_count++)
#define DEBUG_STATS_RX_BUFF_CNT(qcq)	((qcq)->stats->rx.buffers_posted++)
#define DEBUG_STATS_INTR_REARM(intr)	((intr)->rearm_count++)
#define DEBUG_STATS_TXQ_POST(qcq, txdesc, dbell) \
	debug_stats_txq_post(qcq, txdesc, dbell)
#define DEBUG_STATS_NAPI_POLL(qcq, work_done) \
	debug_stats_napi_poll(qcq, work_done)

#endif /* _IONIC_LIF_H_ */
