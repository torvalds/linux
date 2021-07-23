/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#ifndef _IONIC_LIF_H_
#define _IONIC_LIF_H_

#include <linux/ptp_clock_kernel.h>
#include <linux/timecounter.h>
#include <uapi/linux/net_tstamp.h>
#include <linux/dim.h>
#include <linux/pci.h>
#include "ionic_rx_filter.h"

#define IONIC_ADMINQ_LENGTH	16	/* must be a power of two */
#define IONIC_NOTIFYQ_LENGTH	64	/* must be a power of two */

#define IONIC_MAX_NUM_NAPI_CNTR		(NAPI_POLL_WEIGHT + 1)
#define IONIC_MAX_NUM_SG_CNTR		(IONIC_TX_MAX_SG_ELEMS + 1)

#define ADD_ADDR	true
#define DEL_ADDR	false
#define CAN_SLEEP	true
#define CAN_NOT_SLEEP	false

#define IONIC_RX_COPYBREAK_DEFAULT	256
#define IONIC_TX_BUDGET_DEFAULT		256

struct ionic_tx_stats {
	u64 pkts;
	u64 bytes;
	u64 csum_none;
	u64 csum;
	u64 tso;
	u64 tso_bytes;
	u64 frags;
	u64 vlan_inserted;
	u64 clean;
	u64 linearize;
	u64 crc32_csum;
	u64 sg_cntr[IONIC_MAX_NUM_SG_CNTR];
	u64 dma_map_err;
	u64 hwstamp_valid;
	u64 hwstamp_invalid;
};

struct ionic_rx_stats {
	u64 pkts;
	u64 bytes;
	u64 csum_none;
	u64 csum_complete;
	u64 buffers_posted;
	u64 dropped;
	u64 vlan_stripped;
	u64 csum_error;
	u64 dma_map_err;
	u64 alloc_err;
	u64 hwstamp_valid;
	u64 hwstamp_invalid;
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

struct ionic_qcq {
	void *q_base;
	dma_addr_t q_base_pa;
	u32 q_size;
	void *cq_base;
	dma_addr_t cq_base_pa;
	u32 cq_size;
	void *sg_base;
	dma_addr_t sg_base_pa;
	u32 sg_size;
	struct dim dim;
	struct ionic_queue q;
	struct ionic_cq cq;
	struct ionic_intr_info intr;
	struct napi_struct napi;
	struct ionic_napi_stats napi_stats;
	unsigned int flags;
	struct dentry *dentry;
};

#define q_to_qcq(q)		container_of(q, struct ionic_qcq, q)
#define q_to_tx_stats(q)	(&(q)->lif->txqstats[(q)->index])
#define q_to_rx_stats(q)	(&(q)->lif->rxqstats[(q)->index])
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
	u64 tx_hwstamp_valid;
	u64 tx_hwstamp_invalid;
	u64 rx_hwstamp_valid;
	u64 rx_hwstamp_invalid;
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
	IONIC_LIF_F_FW_RESET,
	IONIC_LIF_F_SPLIT_INTR,
	IONIC_LIF_F_BROKEN,
	IONIC_LIF_F_TX_DIM_INTR,
	IONIC_LIF_F_RX_DIM_INTR,

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

struct ionic_phc;

#define IONIC_LIF_NAME_MAX_SZ		32
struct ionic_lif {
	struct net_device *netdev;
	DECLARE_BITMAP(state, IONIC_LIF_F_STATE_SIZE);
	struct ionic *ionic;
	unsigned int index;
	unsigned int hw_index;
	struct mutex queue_lock;	/* lock for queue structures */
	struct mutex config_lock;	/* lock for config actions */
	spinlock_t adminq_lock;		/* lock for AdminQ operations */
	struct ionic_qcq *adminqcq;
	struct ionic_qcq *notifyqcq;
	struct ionic_qcq **txqcqs;
	struct ionic_qcq *hwstamp_txq;
	struct ionic_tx_stats *txqstats;
	struct ionic_qcq **rxqcqs;
	struct ionic_qcq *hwstamp_rxq;
	struct ionic_rx_stats *rxqstats;
	struct ionic_deferred deferred;
	struct work_struct tx_timeout_work;
	u64 last_eid;
	unsigned int kern_pid;
	u64 __iomem *kern_dbpage;
	unsigned int neqs;
	unsigned int nxqs;
	unsigned int ntxq_descs;
	unsigned int nrxq_descs;
	u32 rx_copybreak;
	u64 rxq_features;
	u16 rx_mode;
	u64 hw_features;
	bool registered;
	bool mc_overflow;
	bool uc_overflow;
	u16 lif_type;
	unsigned int nmcast;
	unsigned int nucast;
	char name[IONIC_LIF_NAME_MAX_SZ];

	union ionic_lif_identity *identity;
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
	u32 rx_coalesce_usecs;		/* what the user asked for */
	u32 rx_coalesce_hw;		/* what the hw is using */
	u32 tx_coalesce_usecs;		/* what the user asked for */
	u32 tx_coalesce_hw;		/* what the hw is using */
	unsigned long *dbid_inuse;
	unsigned int dbid_count;

	struct ionic_phc *phc;

	struct dentry *dentry;
};

struct ionic_phc {
	spinlock_t lock; /* lock for cc and tc */
	struct cyclecounter cc;
	struct timecounter tc;

	struct mutex config_lock; /* lock for ts_config */
	struct hwtstamp_config ts_config;
	u64 ts_config_rx_filt;
	u32 ts_config_tx_mode;

	u32 init_cc_mult;
	long aux_work_delay;

	struct ptp_clock_info ptp_info;
	struct ptp_clock *ptp;
	struct ionic_lif *lif;
};

struct ionic_queue_params {
	unsigned int nxqs;
	unsigned int ntxq_descs;
	unsigned int nrxq_descs;
	unsigned int intr_split;
	u64 rxq_features;
};

static inline void ionic_init_queue_params(struct ionic_lif *lif,
					   struct ionic_queue_params *qparam)
{
	qparam->nxqs = lif->nxqs;
	qparam->ntxq_descs = lif->ntxq_descs;
	qparam->nrxq_descs = lif->nrxq_descs;
	qparam->intr_split = test_bit(IONIC_LIF_F_SPLIT_INTR, lif->state);
	qparam->rxq_features = lif->rxq_features;
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

void ionic_link_status_check_request(struct ionic_lif *lif, bool can_sleep);
void ionic_get_stats64(struct net_device *netdev,
		       struct rtnl_link_stats64 *ns);
void ionic_lif_deferred_enqueue(struct ionic_deferred *def,
				struct ionic_deferred_work *work);
int ionic_lif_alloc(struct ionic *ionic);
int ionic_lif_init(struct ionic_lif *lif);
void ionic_lif_free(struct ionic_lif *lif);
void ionic_lif_deinit(struct ionic_lif *lif);
int ionic_lif_register(struct ionic_lif *lif);
void ionic_lif_unregister(struct ionic_lif *lif);
int ionic_lif_identify(struct ionic *ionic, u8 lif_type,
		       union ionic_lif_identity *lif_ident);
int ionic_lif_size(struct ionic *ionic);

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
void ionic_lif_hwstamp_replay(struct ionic_lif *lif);
int ionic_lif_hwstamp_set(struct ionic_lif *lif, struct ifreq *ifr);
int ionic_lif_hwstamp_get(struct ionic_lif *lif, struct ifreq *ifr);
ktime_t ionic_lif_phc_ktime(struct ionic_lif *lif, u64 counter);
void ionic_lif_register_phc(struct ionic_lif *lif);
void ionic_lif_unregister_phc(struct ionic_lif *lif);
void ionic_lif_alloc_phc(struct ionic_lif *lif);
void ionic_lif_free_phc(struct ionic_lif *lif);
#else
static inline void ionic_lif_hwstamp_replay(struct ionic_lif *lif) {}

static inline int ionic_lif_hwstamp_set(struct ionic_lif *lif, struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}

static inline int ionic_lif_hwstamp_get(struct ionic_lif *lif, struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}

static inline ktime_t ionic_lif_phc_ktime(struct ionic_lif *lif, u64 counter)
{
	return ns_to_ktime(0);
}

static inline void ionic_lif_register_phc(struct ionic_lif *lif) {}
static inline void ionic_lif_unregister_phc(struct ionic_lif *lif) {}
static inline void ionic_lif_alloc_phc(struct ionic_lif *lif) {}
static inline void ionic_lif_free_phc(struct ionic_lif *lif) {}
#endif

int ionic_lif_create_hwstamp_txq(struct ionic_lif *lif);
int ionic_lif_create_hwstamp_rxq(struct ionic_lif *lif);
int ionic_lif_config_hwstamp_rxq_all(struct ionic_lif *lif, bool rx_all);
int ionic_lif_set_hwstamp_txmode(struct ionic_lif *lif, u16 txstamp_mode);
int ionic_lif_set_hwstamp_rxfilt(struct ionic_lif *lif, u64 pkt_class);

int ionic_lif_rss_config(struct ionic_lif *lif, u16 types,
			 const u8 *key, const u32 *indir);
int ionic_reconfigure_queues(struct ionic_lif *lif,
			     struct ionic_queue_params *qparam);

static inline void debug_stats_txq_post(struct ionic_queue *q, bool dbell)
{
	struct ionic_txq_desc *desc = &q->txq[q->head_idx];
	u8 num_sg_elems;

	q->dbell_count += dbell;

	num_sg_elems = ((le64_to_cpu(desc->cmd) >> IONIC_TXQ_DESC_NSGE_SHIFT)
						& IONIC_TXQ_DESC_NSGE_MASK);
	if (num_sg_elems > (IONIC_MAX_NUM_SG_CNTR - 1))
		num_sg_elems = IONIC_MAX_NUM_SG_CNTR - 1;

	q->lif->txqstats[q->index].sg_cntr[num_sg_elems]++;
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
#define DEBUG_STATS_RX_BUFF_CNT(q)	((q)->lif->rxqstats[q->index].buffers_posted++)
#define DEBUG_STATS_TXQ_POST(q, dbell)  debug_stats_txq_post(q, dbell)
#define DEBUG_STATS_NAPI_POLL(qcq, work_done) \
	debug_stats_napi_poll(qcq, work_done)

#endif /* _IONIC_LIF_H_ */
