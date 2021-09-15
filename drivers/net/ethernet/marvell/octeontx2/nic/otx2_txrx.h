/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#ifndef OTX2_TXRX_H
#define OTX2_TXRX_H

#include <linux/etherdevice.h>
#include <linux/iommu.h>
#include <linux/if_vlan.h>

#define LBK_CHAN_BASE	0x000
#define SDP_CHAN_BASE	0x700
#define CGX_CHAN_BASE	0x800

#define OTX2_DATA_ALIGN(X)	ALIGN(X, OTX2_ALIGN)
#define OTX2_HEAD_ROOM		OTX2_ALIGN

#define	OTX2_ETH_HLEN		(VLAN_ETH_HLEN + VLAN_HLEN)
#define	OTX2_MIN_MTU		64

#define OTX2_MAX_GSO_SEGS	255
#define OTX2_MAX_FRAGS_IN_SQE	9

/* Rx buffer size should be in multiples of 128bytes */
#define RCV_FRAG_LEN1(x)				\
		((OTX2_HEAD_ROOM + OTX2_DATA_ALIGN(x)) + \
		OTX2_DATA_ALIGN(sizeof(struct skb_shared_info)))

/* Prefer 2048 byte buffers for better last level cache
 * utilization or data distribution across regions.
 */
#define RCV_FRAG_LEN(x)	\
		((RCV_FRAG_LEN1(x) < 2048) ? 2048 : RCV_FRAG_LEN1(x))

#define DMA_BUFFER_LEN(x)		\
		((x) - OTX2_HEAD_ROOM - \
		OTX2_DATA_ALIGN(sizeof(struct skb_shared_info)))

/* IRQ triggered when NIX_LF_CINTX_CNT[ECOUNT]
 * is equal to this value.
 */
#define CQ_CQE_THRESH_DEFAULT	10

/* IRQ triggered when NIX_LF_CINTX_CNT[ECOUNT]
 * is nonzero and this much time elapses after that.
 */
#define CQ_TIMER_THRESH_DEFAULT	1  /* 1 usec */
#define CQ_TIMER_THRESH_MAX     25 /* 25 usec */

/* Min number of CQs (of the ones mapped to this CINT)
 * with valid CQEs.
 */
#define CQ_QCOUNT_DEFAULT	1

struct queue_stats {
	u64	bytes;
	u64	pkts;
};

struct otx2_rcv_queue {
	struct queue_stats	stats;
};

struct sg_list {
	u16	num_segs;
	u64	skb;
	u64	size[OTX2_MAX_FRAGS_IN_SQE];
	u64	dma_addr[OTX2_MAX_FRAGS_IN_SQE];
};

struct otx2_snd_queue {
	u8			aura_id;
	u16			head;
	u16			sqe_size;
	u32			sqe_cnt;
	u16			num_sqbs;
	u16			sqe_thresh;
	u8			sqe_per_sqb;
	u64			 io_addr;
	u64			*aura_fc_addr;
	u64			*lmt_addr;
	void			*sqe_base;
	struct qmem		*sqe;
	struct qmem		*tso_hdrs;
	struct sg_list		*sg;
	struct qmem		*timestamps;
	struct queue_stats	stats;
	u16			sqb_count;
	u64			*sqb_ptrs;
} ____cacheline_aligned_in_smp;

enum cq_type {
	CQ_RX,
	CQ_TX,
	CQS_PER_CINT = 2, /* RQ + SQ */
};

struct otx2_cq_poll {
	void			*dev;
#define CINT_INVALID_CQ		255
	u8			cint_idx;
	u8			cq_ids[CQS_PER_CINT];
	struct napi_struct	napi;
};

struct otx2_pool {
	struct qmem		*stack;
	struct qmem		*fc_addr;
	u16			rbsize;
};

struct otx2_cq_queue {
	u8			cq_idx;
	u8			cq_type;
	u8			cint_idx; /* CQ interrupt id */
	u8			refill_task_sched;
	u16			cqe_size;
	u16			pool_ptrs;
	u32			cqe_cnt;
	u32			cq_head;
	void			*cqe_base;
	struct qmem		*cqe;
	struct otx2_pool	*rbpool;
} ____cacheline_aligned_in_smp;

struct otx2_qset {
	u32			rqe_cnt;
	u32			sqe_cnt; /* Keep these two at top */
#define OTX2_MAX_CQ_CNT		64
	u16			cq_cnt;
	u16			xqe_size;
	struct otx2_pool	*pool;
	struct otx2_cq_poll	*napi;
	struct otx2_cq_queue	*cq;
	struct otx2_snd_queue	*sq;
	struct otx2_rcv_queue	*rq;
};

/* Translate IOVA to physical address */
static inline u64 otx2_iova_to_phys(void *iommu_domain, dma_addr_t dma_addr)
{
	/* Translation is installed only when IOMMU is present */
	if (likely(iommu_domain))
		return iommu_iova_to_phys(iommu_domain, dma_addr);
	return dma_addr;
}

int otx2_napi_handler(struct napi_struct *napi, int budget);
bool otx2_sq_append_skb(struct net_device *netdev, struct otx2_snd_queue *sq,
			struct sk_buff *skb, u16 qidx);
void cn10k_sqe_flush(void *dev, struct otx2_snd_queue *sq,
		     int size, int qidx);
void otx2_sqe_flush(void *dev, struct otx2_snd_queue *sq,
		    int size, int qidx);
void otx2_refill_pool_ptrs(void *dev, struct otx2_cq_queue *cq);
void cn10k_refill_pool_ptrs(void *dev, struct otx2_cq_queue *cq);
#endif /* OTX2_TXRX_H */
