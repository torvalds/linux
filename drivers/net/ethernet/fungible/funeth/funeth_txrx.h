/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */

#ifndef _FUNETH_TXRX_H
#define _FUNETH_TXRX_H

#include <linux/netdevice.h>
#include <linux/u64_stats_sync.h>

/* Tx descriptor size */
#define FUNETH_SQE_SIZE 64U

/* Size of device headers per Tx packet */
#define FUNETH_FUNOS_HDR_SZ (sizeof(struct fun_eth_tx_req))

/* Number of gather list entries per Tx descriptor */
#define FUNETH_GLE_PER_DESC (FUNETH_SQE_SIZE / sizeof(struct fun_dataop_gl))

/* Max gather list size in bytes for an sk_buff. */
#define FUNETH_MAX_GL_SZ ((MAX_SKB_FRAGS + 1) * sizeof(struct fun_dataop_gl))

#if IS_ENABLED(CONFIG_TLS_DEVICE)
# define FUNETH_TLS_SZ sizeof(struct fun_eth_tls)
#else
# define FUNETH_TLS_SZ 0
#endif

/* Max number of Tx descriptors for an sk_buff using a gather list. */
#define FUNETH_MAX_GL_DESC \
	DIV_ROUND_UP((FUNETH_FUNOS_HDR_SZ + FUNETH_MAX_GL_SZ + FUNETH_TLS_SZ), \
		     FUNETH_SQE_SIZE)

/* Max number of Tx descriptors for any packet. */
#define FUNETH_MAX_PKT_DESC FUNETH_MAX_GL_DESC

/* Rx CQ descriptor size. */
#define FUNETH_CQE_SIZE 64U

/* Offset of cqe_info within a CQE. */
#define FUNETH_CQE_INFO_OFFSET (FUNETH_CQE_SIZE - sizeof(struct fun_cqe_info))

/* Construct the IRQ portion of a CQ doorbell. The resulting value arms the
 * interrupt with the supplied time delay and packet count moderation settings.
 */
#define FUN_IRQ_CQ_DB(usec, pkts) \
	(FUN_DB_IRQ_ARM_F | ((usec) << FUN_DB_INTCOAL_USEC_S) | \
	 ((pkts) << FUN_DB_INTCOAL_ENTRIES_S))

/* As above for SQ doorbells. */
#define FUN_IRQ_SQ_DB(usec, pkts) \
	(FUN_DB_IRQ_ARM_F | \
	 ((usec) << FUN_DB_INTCOAL_USEC_S) | \
	 ((pkts) << FUN_DB_INTCOAL_ENTRIES_S))

/* Per packet tailroom. Present only for 1-frag packets. */
#define FUN_RX_TAILROOM SKB_DATA_ALIGN(sizeof(struct skb_shared_info))

/* Per packet headroom for XDP. Preferred over XDP_PACKET_HEADROOM to
 * accommodate two packets per buffer for 4K pages and 1500B MTUs.
 */
#define FUN_XDP_HEADROOM 192

/* Initialization state of a queue. */
enum {
	FUN_QSTATE_DESTROYED, /* what queue? */
	FUN_QSTATE_INIT_SW,   /* exists in SW, not on the device */
	FUN_QSTATE_INIT_FULL, /* exists both in SW and on device */
};

/* Initialization state of an interrupt. */
enum {
	FUN_IRQ_INIT,      /* initialized and in the XArray but inactive */
	FUN_IRQ_REQUESTED, /* request_irq() done */
	FUN_IRQ_ENABLED,   /* processing enabled */
	FUN_IRQ_DISABLED,  /* processing disabled */
};

struct bpf_prog;

struct funeth_txq_stats {  /* per Tx queue SW counters */
	u64 tx_pkts;       /* # of Tx packets */
	u64 tx_bytes;      /* total bytes of Tx packets */
	u64 tx_cso;        /* # of packets with checksum offload */
	u64 tx_tso;        /* # of non-encapsulated TSO super-packets */
	u64 tx_encap_tso;  /* # of encapsulated TSO super-packets */
	u64 tx_more;       /* # of DBs elided due to xmit_more */
	u64 tx_nstops;     /* # of times the queue has stopped */
	u64 tx_nrestarts;  /* # of times the queue has restarted */
	u64 tx_map_err;    /* # of packets dropped due to DMA mapping errors */
	u64 tx_xdp_full;   /* # of XDP packets that could not be enqueued */
	u64 tx_tls_pkts;   /* # of Tx TLS packets offloaded to HW */
	u64 tx_tls_bytes;  /* Tx bytes of HW-handled TLS payload */
	u64 tx_tls_fallback; /* attempted Tx TLS offloads punted to SW */
	u64 tx_tls_drops;  /* attempted Tx TLS offloads dropped */
};

struct funeth_tx_info {      /* per Tx descriptor state */
	union {
		struct sk_buff *skb; /* associated packet */
		void *vaddr;         /* start address for XDP */
	};
};

struct funeth_txq {
	/* RO cacheline of frequently accessed data */
	u32 mask;               /* queue depth - 1 */
	u32 hw_qid;             /* device ID of the queue */
	void *desc;             /* base address of descriptor ring */
	struct funeth_tx_info *info;
	struct device *dma_dev; /* device for DMA mappings */
	volatile __be64 *hw_wb; /* HW write-back location */
	u32 __iomem *db;        /* SQ doorbell register address */
	struct netdev_queue *ndq;
	dma_addr_t dma_addr;    /* DMA address of descriptor ring */
	/* producer R/W cacheline */
	u16 qidx;               /* queue index within net_device */
	u16 ethid;
	u32 prod_cnt;           /* producer counter */
	struct funeth_txq_stats stats;
	/* shared R/W cacheline, primarily accessed by consumer */
	u32 irq_db_val;         /* value written to IRQ doorbell */
	u32 cons_cnt;           /* consumer (cleanup) counter */
	struct net_device *netdev;
	struct fun_irq *irq;
	int numa_node;
	u8 init_state;          /* queue initialization state */
	struct u64_stats_sync syncp;
};

struct funeth_rxq_stats {  /* per Rx queue SW counters */
	u64 rx_pkts;       /* # of received packets, including SW drops */
	u64 rx_bytes;      /* total size of received packets */
	u64 rx_cso;        /* # of packets with checksum offload */
	u64 rx_bufs;       /* total # of Rx buffers provided to device */
	u64 gro_pkts;      /* # of GRO superpackets */
	u64 gro_merged;    /* # of pkts merged into existing GRO superpackets */
	u64 rx_page_alloc; /* # of page allocations for Rx buffers */
	u64 rx_budget;     /* NAPI iterations that exhausted their budget */
	u64 rx_mem_drops;  /* # of packets dropped due to memory shortage */
	u64 rx_map_err;    /* # of page DMA mapping errors */
	u64 xdp_drops;     /* XDP_DROPped packets */
	u64 xdp_tx;        /* successful XDP transmits */
	u64 xdp_redir;     /* successful XDP redirects */
	u64 xdp_err;       /* packets dropped due to XDP errors */
};

struct funeth_rxbuf {          /* per Rx buffer state */
	struct page *page;     /* associated page */
	dma_addr_t dma_addr;   /* DMA address of page start */
	int pg_refs;           /* page refs held by driver */
	int node;              /* page node, or -1 if it is PF_MEMALLOC */
};

struct funeth_rx_cache {       /* cache of DMA-mapped previously used buffers */
	struct funeth_rxbuf *bufs; /* base of Rx buffer state ring */
	unsigned int prod_cnt;     /* producer counter */
	unsigned int cons_cnt;     /* consumer counter */
	unsigned int mask;         /* depth - 1 */
};

/* An Rx queue consists of a CQ and an SQ used to provide Rx buffers. */
struct funeth_rxq {
	struct net_device *netdev;
	struct napi_struct *napi;
	struct device *dma_dev;    /* device for DMA mappings */
	void *cqes;                /* base of CQ descriptor ring */
	const void *next_cqe_info; /* fun_cqe_info of next CQE */
	u32 __iomem *cq_db;        /* CQ doorbell register address */
	unsigned int cq_head;      /* CQ head index */
	unsigned int cq_mask;      /* CQ depth - 1 */
	u16 phase;                 /* CQ phase tag */
	u16 qidx;                  /* queue index within net_device */
	unsigned int irq_db_val;   /* IRQ info for CQ doorbell */
	struct fun_eprq_rqbuf *rqes; /* base of RQ descriptor ring */
	struct funeth_rxbuf *bufs; /* base of Rx buffer state ring */
	struct funeth_rxbuf *cur_buf; /* currently active buffer */
	u32 __iomem *rq_db;        /* RQ doorbell register address */
	unsigned int rq_cons;      /* RQ consumer counter */
	unsigned int rq_mask;      /* RQ depth - 1 */
	unsigned int buf_offset;   /* offset of next pkt in head buffer */
	u8 xdp_flush;              /* XDP flush types needed at NAPI end */
	u8 init_state;             /* queue initialization state */
	u16 headroom;              /* per packet headroom */
	unsigned int rq_cons_db;   /* value of rq_cons at last RQ db */
	unsigned int rq_db_thres;  /* # of new buffers needed to write RQ db */
	struct funeth_rxbuf spare_buf; /* spare for next buffer replacement */
	struct funeth_rx_cache cache; /* used buffer cache */
	struct bpf_prog *xdp_prog; /* optional XDP BPF program */
	struct funeth_rxq_stats stats;
	dma_addr_t cq_dma_addr;    /* DMA address of CQE ring */
	dma_addr_t rq_dma_addr;    /* DMA address of RQE ring */
	u16 irq_cnt;
	u32 hw_cqid;               /* device ID of the queue's CQ */
	u32 hw_sqid;               /* device ID of the queue's SQ */
	int numa_node;
	struct u64_stats_sync syncp;
	struct xdp_rxq_info xdp_rxq;
};

#define FUN_QSTAT_INC(q, counter) \
	do { \
		u64_stats_update_begin(&(q)->syncp); \
		(q)->stats.counter++; \
		u64_stats_update_end(&(q)->syncp); \
	} while (0)

#define FUN_QSTAT_READ(q, seq, stats_copy) \
	do { \
		seq = u64_stats_fetch_begin(&(q)->syncp); \
		stats_copy = (q)->stats; \
	} while (u64_stats_fetch_retry(&(q)->syncp, (seq)))

#define FUN_INT_NAME_LEN (IFNAMSIZ + 16)

struct fun_irq {
	struct napi_struct napi;
	struct funeth_txq *txq;
	struct funeth_rxq *rxq;
	u8 state;
	u16 irq_idx;              /* index of MSI-X interrupt */
	int irq;                  /* Linux IRQ vector */
	cpumask_t affinity_mask;  /* IRQ affinity */
	struct irq_affinity_notify aff_notify;
	char name[FUN_INT_NAME_LEN];
} ____cacheline_internodealigned_in_smp;

/* Return the start address of the idx-th Tx descriptor. */
static inline void *fun_tx_desc_addr(const struct funeth_txq *q,
				     unsigned int idx)
{
	return q->desc + idx * FUNETH_SQE_SIZE;
}

static inline void fun_txq_wr_db(const struct funeth_txq *q)
{
	unsigned int tail = q->prod_cnt & q->mask;

	writel(tail, q->db);
}

static inline int fun_irq_node(const struct fun_irq *p)
{
	return cpu_to_mem(cpumask_first(&p->affinity_mask));
}

int fun_rxq_napi_poll(struct napi_struct *napi, int budget);
int fun_txq_napi_poll(struct napi_struct *napi, int budget);
netdev_tx_t fun_start_xmit(struct sk_buff *skb, struct net_device *netdev);
bool fun_xdp_tx(struct funeth_txq *q, void *data, unsigned int len);
int fun_xdp_xmit_frames(struct net_device *dev, int n,
			struct xdp_frame **frames, u32 flags);

int funeth_txq_create(struct net_device *dev, unsigned int qidx,
		      unsigned int ndesc, struct fun_irq *irq, int state,
		      struct funeth_txq **qp);
int fun_txq_create_dev(struct funeth_txq *q, struct fun_irq *irq);
struct funeth_txq *funeth_txq_free(struct funeth_txq *q, int state);
int funeth_rxq_create(struct net_device *dev, unsigned int qidx,
		      unsigned int ncqe, unsigned int nrqe, struct fun_irq *irq,
		      int state, struct funeth_rxq **qp);
int fun_rxq_create_dev(struct funeth_rxq *q, struct fun_irq *irq);
struct funeth_rxq *funeth_rxq_free(struct funeth_rxq *q, int state);
int fun_rxq_set_bpf(struct funeth_rxq *q, struct bpf_prog *prog);

#endif /* _FUNETH_TXRX_H */
