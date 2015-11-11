/*
 * Copyright (c) 2005-2008 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/prefetch.h>
#include <net/arp.h>
#include "common.h"
#include "regs.h"
#include "sge_defs.h"
#include "t3_cpl.h"
#include "firmware_exports.h"
#include "cxgb3_offload.h"

#define USE_GTS 0

#define SGE_RX_SM_BUF_SIZE 1536

#define SGE_RX_COPY_THRES  256
#define SGE_RX_PULL_LEN    128

#define SGE_PG_RSVD SMP_CACHE_BYTES
/*
 * Page chunk size for FL0 buffers if FL0 is to be populated with page chunks.
 * It must be a divisor of PAGE_SIZE.  If set to 0 FL0 will use sk_buffs
 * directly.
 */
#define FL0_PG_CHUNK_SIZE  2048
#define FL0_PG_ORDER 0
#define FL0_PG_ALLOC_SIZE (PAGE_SIZE << FL0_PG_ORDER)
#define FL1_PG_CHUNK_SIZE (PAGE_SIZE > 8192 ? 16384 : 8192)
#define FL1_PG_ORDER (PAGE_SIZE > 8192 ? 0 : 1)
#define FL1_PG_ALLOC_SIZE (PAGE_SIZE << FL1_PG_ORDER)

#define SGE_RX_DROP_THRES 16
#define RX_RECLAIM_PERIOD (HZ/4)

/*
 * Max number of Rx buffers we replenish at a time.
 */
#define MAX_RX_REFILL 16U
/*
 * Period of the Tx buffer reclaim timer.  This timer does not need to run
 * frequently as Tx buffers are usually reclaimed by new Tx packets.
 */
#define TX_RECLAIM_PERIOD (HZ / 4)
#define TX_RECLAIM_TIMER_CHUNK 64U
#define TX_RECLAIM_CHUNK 16U

/* WR size in bytes */
#define WR_LEN (WR_FLITS * 8)

/*
 * Types of Tx queues in each queue set.  Order here matters, do not change.
 */
enum { TXQ_ETH, TXQ_OFLD, TXQ_CTRL };

/* Values for sge_txq.flags */
enum {
	TXQ_RUNNING = 1 << 0,	/* fetch engine is running */
	TXQ_LAST_PKT_DB = 1 << 1,	/* last packet rang the doorbell */
};

struct tx_desc {
	__be64 flit[TX_DESC_FLITS];
};

struct rx_desc {
	__be32 addr_lo;
	__be32 len_gen;
	__be32 gen2;
	__be32 addr_hi;
};

struct tx_sw_desc {		/* SW state per Tx descriptor */
	struct sk_buff *skb;
	u8 eop;       /* set if last descriptor for packet */
	u8 addr_idx;  /* buffer index of first SGL entry in descriptor */
	u8 fragidx;   /* first page fragment associated with descriptor */
	s8 sflit;     /* start flit of first SGL entry in descriptor */
};

struct rx_sw_desc {                /* SW state per Rx descriptor */
	union {
		struct sk_buff *skb;
		struct fl_pg_chunk pg_chunk;
	};
	DEFINE_DMA_UNMAP_ADDR(dma_addr);
};

struct rsp_desc {		/* response queue descriptor */
	struct rss_header rss_hdr;
	__be32 flags;
	__be32 len_cq;
	u8 imm_data[47];
	u8 intr_gen;
};

/*
 * Holds unmapping information for Tx packets that need deferred unmapping.
 * This structure lives at skb->head and must be allocated by callers.
 */
struct deferred_unmap_info {
	struct pci_dev *pdev;
	dma_addr_t addr[MAX_SKB_FRAGS + 1];
};

/*
 * Maps a number of flits to the number of Tx descriptors that can hold them.
 * The formula is
 *
 * desc = 1 + (flits - 2) / (WR_FLITS - 1).
 *
 * HW allows up to 4 descriptors to be combined into a WR.
 */
static u8 flit_desc_map[] = {
	0,
#if SGE_NUM_GENBITS == 1
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4
#elif SGE_NUM_GENBITS == 2
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
#else
# error "SGE_NUM_GENBITS must be 1 or 2"
#endif
};

static inline struct sge_qset *fl_to_qset(const struct sge_fl *q, int qidx)
{
	return container_of(q, struct sge_qset, fl[qidx]);
}

static inline struct sge_qset *rspq_to_qset(const struct sge_rspq *q)
{
	return container_of(q, struct sge_qset, rspq);
}

static inline struct sge_qset *txq_to_qset(const struct sge_txq *q, int qidx)
{
	return container_of(q, struct sge_qset, txq[qidx]);
}

/**
 *	refill_rspq - replenish an SGE response queue
 *	@adapter: the adapter
 *	@q: the response queue to replenish
 *	@credits: how many new responses to make available
 *
 *	Replenishes a response queue by making the supplied number of responses
 *	available to HW.
 */
static inline void refill_rspq(struct adapter *adapter,
			       const struct sge_rspq *q, unsigned int credits)
{
	rmb();
	t3_write_reg(adapter, A_SG_RSPQ_CREDIT_RETURN,
		     V_RSPQ(q->cntxt_id) | V_CREDITS(credits));
}

/**
 *	need_skb_unmap - does the platform need unmapping of sk_buffs?
 *
 *	Returns true if the platform needs sk_buff unmapping.  The compiler
 *	optimizes away unnecessary code if this returns true.
 */
static inline int need_skb_unmap(void)
{
#ifdef CONFIG_NEED_DMA_MAP_STATE
	return 1;
#else
	return 0;
#endif
}

/**
 *	unmap_skb - unmap a packet main body and its page fragments
 *	@skb: the packet
 *	@q: the Tx queue containing Tx descriptors for the packet
 *	@cidx: index of Tx descriptor
 *	@pdev: the PCI device
 *
 *	Unmap the main body of an sk_buff and its page fragments, if any.
 *	Because of the fairly complicated structure of our SGLs and the desire
 *	to conserve space for metadata, the information necessary to unmap an
 *	sk_buff is spread across the sk_buff itself (buffer lengths), the HW Tx
 *	descriptors (the physical addresses of the various data buffers), and
 *	the SW descriptor state (assorted indices).  The send functions
 *	initialize the indices for the first packet descriptor so we can unmap
 *	the buffers held in the first Tx descriptor here, and we have enough
 *	information at this point to set the state for the next Tx descriptor.
 *
 *	Note that it is possible to clean up the first descriptor of a packet
 *	before the send routines have written the next descriptors, but this
 *	race does not cause any problem.  We just end up writing the unmapping
 *	info for the descriptor first.
 */
static inline void unmap_skb(struct sk_buff *skb, struct sge_txq *q,
			     unsigned int cidx, struct pci_dev *pdev)
{
	const struct sg_ent *sgp;
	struct tx_sw_desc *d = &q->sdesc[cidx];
	int nfrags, frag_idx, curflit, j = d->addr_idx;

	sgp = (struct sg_ent *)&q->desc[cidx].flit[d->sflit];
	frag_idx = d->fragidx;

	if (frag_idx == 0 && skb_headlen(skb)) {
		pci_unmap_single(pdev, be64_to_cpu(sgp->addr[0]),
				 skb_headlen(skb), PCI_DMA_TODEVICE);
		j = 1;
	}

	curflit = d->sflit + 1 + j;
	nfrags = skb_shinfo(skb)->nr_frags;

	while (frag_idx < nfrags && curflit < WR_FLITS) {
		pci_unmap_page(pdev, be64_to_cpu(sgp->addr[j]),
			       skb_frag_size(&skb_shinfo(skb)->frags[frag_idx]),
			       PCI_DMA_TODEVICE);
		j ^= 1;
		if (j == 0) {
			sgp++;
			curflit++;
		}
		curflit++;
		frag_idx++;
	}

	if (frag_idx < nfrags) {   /* SGL continues into next Tx descriptor */
		d = cidx + 1 == q->size ? q->sdesc : d + 1;
		d->fragidx = frag_idx;
		d->addr_idx = j;
		d->sflit = curflit - WR_FLITS - j; /* sflit can be -1 */
	}
}

/**
 *	free_tx_desc - reclaims Tx descriptors and their buffers
 *	@adapter: the adapter
 *	@q: the Tx queue to reclaim descriptors from
 *	@n: the number of descriptors to reclaim
 *
 *	Reclaims Tx descriptors from an SGE Tx queue and frees the associated
 *	Tx buffers.  Called with the Tx queue lock held.
 */
static void free_tx_desc(struct adapter *adapter, struct sge_txq *q,
			 unsigned int n)
{
	struct tx_sw_desc *d;
	struct pci_dev *pdev = adapter->pdev;
	unsigned int cidx = q->cidx;

	const int need_unmap = need_skb_unmap() &&
			       q->cntxt_id >= FW_TUNNEL_SGEEC_START;

	d = &q->sdesc[cidx];
	while (n--) {
		if (d->skb) {	/* an SGL is present */
			if (need_unmap)
				unmap_skb(d->skb, q, cidx, pdev);
			if (d->eop) {
				kfree_skb(d->skb);
				d->skb = NULL;
			}
		}
		++d;
		if (++cidx == q->size) {
			cidx = 0;
			d = q->sdesc;
		}
	}
	q->cidx = cidx;
}

/**
 *	reclaim_completed_tx - reclaims completed Tx descriptors
 *	@adapter: the adapter
 *	@q: the Tx queue to reclaim completed descriptors from
 *	@chunk: maximum number of descriptors to reclaim
 *
 *	Reclaims Tx descriptors that the SGE has indicated it has processed,
 *	and frees the associated buffers if possible.  Called with the Tx
 *	queue's lock held.
 */
static inline unsigned int reclaim_completed_tx(struct adapter *adapter,
						struct sge_txq *q,
						unsigned int chunk)
{
	unsigned int reclaim = q->processed - q->cleaned;

	reclaim = min(chunk, reclaim);
	if (reclaim) {
		free_tx_desc(adapter, q, reclaim);
		q->cleaned += reclaim;
		q->in_use -= reclaim;
	}
	return q->processed - q->cleaned;
}

/**
 *	should_restart_tx - are there enough resources to restart a Tx queue?
 *	@q: the Tx queue
 *
 *	Checks if there are enough descriptors to restart a suspended Tx queue.
 */
static inline int should_restart_tx(const struct sge_txq *q)
{
	unsigned int r = q->processed - q->cleaned;

	return q->in_use - r < (q->size >> 1);
}

static void clear_rx_desc(struct pci_dev *pdev, const struct sge_fl *q,
			  struct rx_sw_desc *d)
{
	if (q->use_pages && d->pg_chunk.page) {
		(*d->pg_chunk.p_cnt)--;
		if (!*d->pg_chunk.p_cnt)
			pci_unmap_page(pdev,
				       d->pg_chunk.mapping,
				       q->alloc_size, PCI_DMA_FROMDEVICE);

		put_page(d->pg_chunk.page);
		d->pg_chunk.page = NULL;
	} else {
		pci_unmap_single(pdev, dma_unmap_addr(d, dma_addr),
				 q->buf_size, PCI_DMA_FROMDEVICE);
		kfree_skb(d->skb);
		d->skb = NULL;
	}
}

/**
 *	free_rx_bufs - free the Rx buffers on an SGE free list
 *	@pdev: the PCI device associated with the adapter
 *	@rxq: the SGE free list to clean up
 *
 *	Release the buffers on an SGE free-buffer Rx queue.  HW fetching from
 *	this queue should be stopped before calling this function.
 */
static void free_rx_bufs(struct pci_dev *pdev, struct sge_fl *q)
{
	unsigned int cidx = q->cidx;

	while (q->credits--) {
		struct rx_sw_desc *d = &q->sdesc[cidx];


		clear_rx_desc(pdev, q, d);
		if (++cidx == q->size)
			cidx = 0;
	}

	if (q->pg_chunk.page) {
		__free_pages(q->pg_chunk.page, q->order);
		q->pg_chunk.page = NULL;
	}
}

/**
 *	add_one_rx_buf - add a packet buffer to a free-buffer list
 *	@va:  buffer start VA
 *	@len: the buffer length
 *	@d: the HW Rx descriptor to write
 *	@sd: the SW Rx descriptor to write
 *	@gen: the generation bit value
 *	@pdev: the PCI device associated with the adapter
 *
 *	Add a buffer of the given length to the supplied HW and SW Rx
 *	descriptors.
 */
static inline int add_one_rx_buf(void *va, unsigned int len,
				 struct rx_desc *d, struct rx_sw_desc *sd,
				 unsigned int gen, struct pci_dev *pdev)
{
	dma_addr_t mapping;

	mapping = pci_map_single(pdev, va, len, PCI_DMA_FROMDEVICE);
	if (unlikely(pci_dma_mapping_error(pdev, mapping)))
		return -ENOMEM;

	dma_unmap_addr_set(sd, dma_addr, mapping);

	d->addr_lo = cpu_to_be32(mapping);
	d->addr_hi = cpu_to_be32((u64) mapping >> 32);
	wmb();
	d->len_gen = cpu_to_be32(V_FLD_GEN1(gen));
	d->gen2 = cpu_to_be32(V_FLD_GEN2(gen));
	return 0;
}

static inline int add_one_rx_chunk(dma_addr_t mapping, struct rx_desc *d,
				   unsigned int gen)
{
	d->addr_lo = cpu_to_be32(mapping);
	d->addr_hi = cpu_to_be32((u64) mapping >> 32);
	wmb();
	d->len_gen = cpu_to_be32(V_FLD_GEN1(gen));
	d->gen2 = cpu_to_be32(V_FLD_GEN2(gen));
	return 0;
}

static int alloc_pg_chunk(struct adapter *adapter, struct sge_fl *q,
			  struct rx_sw_desc *sd, gfp_t gfp,
			  unsigned int order)
{
	if (!q->pg_chunk.page) {
		dma_addr_t mapping;

		q->pg_chunk.page = alloc_pages(gfp, order);
		if (unlikely(!q->pg_chunk.page))
			return -ENOMEM;
		q->pg_chunk.va = page_address(q->pg_chunk.page);
		q->pg_chunk.p_cnt = q->pg_chunk.va + (PAGE_SIZE << order) -
				    SGE_PG_RSVD;
		q->pg_chunk.offset = 0;
		mapping = pci_map_page(adapter->pdev, q->pg_chunk.page,
				       0, q->alloc_size, PCI_DMA_FROMDEVICE);
		q->pg_chunk.mapping = mapping;
	}
	sd->pg_chunk = q->pg_chunk;

	prefetch(sd->pg_chunk.p_cnt);

	q->pg_chunk.offset += q->buf_size;
	if (q->pg_chunk.offset == (PAGE_SIZE << order))
		q->pg_chunk.page = NULL;
	else {
		q->pg_chunk.va += q->buf_size;
		get_page(q->pg_chunk.page);
	}

	if (sd->pg_chunk.offset == 0)
		*sd->pg_chunk.p_cnt = 1;
	else
		*sd->pg_chunk.p_cnt += 1;

	return 0;
}

static inline void ring_fl_db(struct adapter *adap, struct sge_fl *q)
{
	if (q->pend_cred >= q->credits / 4) {
		q->pend_cred = 0;
		wmb();
		t3_write_reg(adap, A_SG_KDOORBELL, V_EGRCNTX(q->cntxt_id));
	}
}

/**
 *	refill_fl - refill an SGE free-buffer list
 *	@adapter: the adapter
 *	@q: the free-list to refill
 *	@n: the number of new buffers to allocate
 *	@gfp: the gfp flags for allocating new buffers
 *
 *	(Re)populate an SGE free-buffer list with up to @n new packet buffers,
 *	allocated with the supplied gfp flags.  The caller must assure that
 *	@n does not exceed the queue's capacity.
 */
static int refill_fl(struct adapter *adap, struct sge_fl *q, int n, gfp_t gfp)
{
	struct rx_sw_desc *sd = &q->sdesc[q->pidx];
	struct rx_desc *d = &q->desc[q->pidx];
	unsigned int count = 0;

	while (n--) {
		dma_addr_t mapping;
		int err;

		if (q->use_pages) {
			if (unlikely(alloc_pg_chunk(adap, q, sd, gfp,
						    q->order))) {
nomem:				q->alloc_failed++;
				break;
			}
			mapping = sd->pg_chunk.mapping + sd->pg_chunk.offset;
			dma_unmap_addr_set(sd, dma_addr, mapping);

			add_one_rx_chunk(mapping, d, q->gen);
			pci_dma_sync_single_for_device(adap->pdev, mapping,
						q->buf_size - SGE_PG_RSVD,
						PCI_DMA_FROMDEVICE);
		} else {
			void *buf_start;

			struct sk_buff *skb = alloc_skb(q->buf_size, gfp);
			if (!skb)
				goto nomem;

			sd->skb = skb;
			buf_start = skb->data;
			err = add_one_rx_buf(buf_start, q->buf_size, d, sd,
					     q->gen, adap->pdev);
			if (unlikely(err)) {
				clear_rx_desc(adap->pdev, q, sd);
				break;
			}
		}

		d++;
		sd++;
		if (++q->pidx == q->size) {
			q->pidx = 0;
			q->gen ^= 1;
			sd = q->sdesc;
			d = q->desc;
		}
		count++;
	}

	q->credits += count;
	q->pend_cred += count;
	ring_fl_db(adap, q);

	return count;
}

static inline void __refill_fl(struct adapter *adap, struct sge_fl *fl)
{
	refill_fl(adap, fl, min(MAX_RX_REFILL, fl->size - fl->credits),
		  GFP_ATOMIC | __GFP_COMP);
}

/**
 *	recycle_rx_buf - recycle a receive buffer
 *	@adapter: the adapter
 *	@q: the SGE free list
 *	@idx: index of buffer to recycle
 *
 *	Recycles the specified buffer on the given free list by adding it at
 *	the next available slot on the list.
 */
static void recycle_rx_buf(struct adapter *adap, struct sge_fl *q,
			   unsigned int idx)
{
	struct rx_desc *from = &q->desc[idx];
	struct rx_desc *to = &q->desc[q->pidx];

	q->sdesc[q->pidx] = q->sdesc[idx];
	to->addr_lo = from->addr_lo;	/* already big endian */
	to->addr_hi = from->addr_hi;	/* likewise */
	wmb();
	to->len_gen = cpu_to_be32(V_FLD_GEN1(q->gen));
	to->gen2 = cpu_to_be32(V_FLD_GEN2(q->gen));

	if (++q->pidx == q->size) {
		q->pidx = 0;
		q->gen ^= 1;
	}

	q->credits++;
	q->pend_cred++;
	ring_fl_db(adap, q);
}

/**
 *	alloc_ring - allocate resources for an SGE descriptor ring
 *	@pdev: the PCI device
 *	@nelem: the number of descriptors
 *	@elem_size: the size of each descriptor
 *	@sw_size: the size of the SW state associated with each ring element
 *	@phys: the physical address of the allocated ring
 *	@metadata: address of the array holding the SW state for the ring
 *
 *	Allocates resources for an SGE descriptor ring, such as Tx queues,
 *	free buffer lists, or response queues.  Each SGE ring requires
 *	space for its HW descriptors plus, optionally, space for the SW state
 *	associated with each HW entry (the metadata).  The function returns
 *	three values: the virtual address for the HW ring (the return value
 *	of the function), the physical address of the HW ring, and the address
 *	of the SW ring.
 */
static void *alloc_ring(struct pci_dev *pdev, size_t nelem, size_t elem_size,
			size_t sw_size, dma_addr_t * phys, void *metadata)
{
	size_t len = nelem * elem_size;
	void *s = NULL;
	void *p = dma_alloc_coherent(&pdev->dev, len, phys, GFP_KERNEL);

	if (!p)
		return NULL;
	if (sw_size && metadata) {
		s = kcalloc(nelem, sw_size, GFP_KERNEL);

		if (!s) {
			dma_free_coherent(&pdev->dev, len, p, *phys);
			return NULL;
		}
		*(void **)metadata = s;
	}
	memset(p, 0, len);
	return p;
}

/**
 *	t3_reset_qset - reset a sge qset
 *	@q: the queue set
 *
 *	Reset the qset structure.
 *	the NAPI structure is preserved in the event of
 *	the qset's reincarnation, for example during EEH recovery.
 */
static void t3_reset_qset(struct sge_qset *q)
{
	if (q->adap &&
	    !(q->adap->flags & NAPI_INIT)) {
		memset(q, 0, sizeof(*q));
		return;
	}

	q->adap = NULL;
	memset(&q->rspq, 0, sizeof(q->rspq));
	memset(q->fl, 0, sizeof(struct sge_fl) * SGE_RXQ_PER_SET);
	memset(q->txq, 0, sizeof(struct sge_txq) * SGE_TXQ_PER_SET);
	q->txq_stopped = 0;
	q->tx_reclaim_timer.function = NULL; /* for t3_stop_sge_timers() */
	q->rx_reclaim_timer.function = NULL;
	q->nomem = 0;
	napi_free_frags(&q->napi);
}


/**
 *	free_qset - free the resources of an SGE queue set
 *	@adapter: the adapter owning the queue set
 *	@q: the queue set
 *
 *	Release the HW and SW resources associated with an SGE queue set, such
 *	as HW contexts, packet buffers, and descriptor rings.  Traffic to the
 *	queue set must be quiesced prior to calling this.
 */
static void t3_free_qset(struct adapter *adapter, struct sge_qset *q)
{
	int i;
	struct pci_dev *pdev = adapter->pdev;

	for (i = 0; i < SGE_RXQ_PER_SET; ++i)
		if (q->fl[i].desc) {
			spin_lock_irq(&adapter->sge.reg_lock);
			t3_sge_disable_fl(adapter, q->fl[i].cntxt_id);
			spin_unlock_irq(&adapter->sge.reg_lock);
			free_rx_bufs(pdev, &q->fl[i]);
			kfree(q->fl[i].sdesc);
			dma_free_coherent(&pdev->dev,
					  q->fl[i].size *
					  sizeof(struct rx_desc), q->fl[i].desc,
					  q->fl[i].phys_addr);
		}

	for (i = 0; i < SGE_TXQ_PER_SET; ++i)
		if (q->txq[i].desc) {
			spin_lock_irq(&adapter->sge.reg_lock);
			t3_sge_enable_ecntxt(adapter, q->txq[i].cntxt_id, 0);
			spin_unlock_irq(&adapter->sge.reg_lock);
			if (q->txq[i].sdesc) {
				free_tx_desc(adapter, &q->txq[i],
					     q->txq[i].in_use);
				kfree(q->txq[i].sdesc);
			}
			dma_free_coherent(&pdev->dev,
					  q->txq[i].size *
					  sizeof(struct tx_desc),
					  q->txq[i].desc, q->txq[i].phys_addr);
			__skb_queue_purge(&q->txq[i].sendq);
		}

	if (q->rspq.desc) {
		spin_lock_irq(&adapter->sge.reg_lock);
		t3_sge_disable_rspcntxt(adapter, q->rspq.cntxt_id);
		spin_unlock_irq(&adapter->sge.reg_lock);
		dma_free_coherent(&pdev->dev,
				  q->rspq.size * sizeof(struct rsp_desc),
				  q->rspq.desc, q->rspq.phys_addr);
	}

	t3_reset_qset(q);
}

/**
 *	init_qset_cntxt - initialize an SGE queue set context info
 *	@qs: the queue set
 *	@id: the queue set id
 *
 *	Initializes the TIDs and context ids for the queues of a queue set.
 */
static void init_qset_cntxt(struct sge_qset *qs, unsigned int id)
{
	qs->rspq.cntxt_id = id;
	qs->fl[0].cntxt_id = 2 * id;
	qs->fl[1].cntxt_id = 2 * id + 1;
	qs->txq[TXQ_ETH].cntxt_id = FW_TUNNEL_SGEEC_START + id;
	qs->txq[TXQ_ETH].token = FW_TUNNEL_TID_START + id;
	qs->txq[TXQ_OFLD].cntxt_id = FW_OFLD_SGEEC_START + id;
	qs->txq[TXQ_CTRL].cntxt_id = FW_CTRL_SGEEC_START + id;
	qs->txq[TXQ_CTRL].token = FW_CTRL_TID_START + id;
}

/**
 *	sgl_len - calculates the size of an SGL of the given capacity
 *	@n: the number of SGL entries
 *
 *	Calculates the number of flits needed for a scatter/gather list that
 *	can hold the given number of entries.
 */
static inline unsigned int sgl_len(unsigned int n)
{
	/* alternatively: 3 * (n / 2) + 2 * (n & 1) */
	return (3 * n) / 2 + (n & 1);
}

/**
 *	flits_to_desc - returns the num of Tx descriptors for the given flits
 *	@n: the number of flits
 *
 *	Calculates the number of Tx descriptors needed for the supplied number
 *	of flits.
 */
static inline unsigned int flits_to_desc(unsigned int n)
{
	BUG_ON(n >= ARRAY_SIZE(flit_desc_map));
	return flit_desc_map[n];
}

/**
 *	get_packet - return the next ingress packet buffer from a free list
 *	@adap: the adapter that received the packet
 *	@fl: the SGE free list holding the packet
 *	@len: the packet length including any SGE padding
 *	@drop_thres: # of remaining buffers before we start dropping packets
 *
 *	Get the next packet from a free list and complete setup of the
 *	sk_buff.  If the packet is small we make a copy and recycle the
 *	original buffer, otherwise we use the original buffer itself.  If a
 *	positive drop threshold is supplied packets are dropped and their
 *	buffers recycled if (a) the number of remaining buffers is under the
 *	threshold and the packet is too big to copy, or (b) the packet should
 *	be copied but there is no memory for the copy.
 */
static struct sk_buff *get_packet(struct adapter *adap, struct sge_fl *fl,
				  unsigned int len, unsigned int drop_thres)
{
	struct sk_buff *skb = NULL;
	struct rx_sw_desc *sd = &fl->sdesc[fl->cidx];

	prefetch(sd->skb->data);
	fl->credits--;

	if (len <= SGE_RX_COPY_THRES) {
		skb = alloc_skb(len, GFP_ATOMIC);
		if (likely(skb != NULL)) {
			__skb_put(skb, len);
			pci_dma_sync_single_for_cpu(adap->pdev,
					    dma_unmap_addr(sd, dma_addr), len,
					    PCI_DMA_FROMDEVICE);
			memcpy(skb->data, sd->skb->data, len);
			pci_dma_sync_single_for_device(adap->pdev,
					    dma_unmap_addr(sd, dma_addr), len,
					    PCI_DMA_FROMDEVICE);
		} else if (!drop_thres)
			goto use_orig_buf;
recycle:
		recycle_rx_buf(adap, fl, fl->cidx);
		return skb;
	}

	if (unlikely(fl->credits < drop_thres) &&
	    refill_fl(adap, fl, min(MAX_RX_REFILL, fl->size - fl->credits - 1),
		      GFP_ATOMIC | __GFP_COMP) == 0)
		goto recycle;

use_orig_buf:
	pci_unmap_single(adap->pdev, dma_unmap_addr(sd, dma_addr),
			 fl->buf_size, PCI_DMA_FROMDEVICE);
	skb = sd->skb;
	skb_put(skb, len);
	__refill_fl(adap, fl);
	return skb;
}

/**
 *	get_packet_pg - return the next ingress packet buffer from a free list
 *	@adap: the adapter that received the packet
 *	@fl: the SGE free list holding the packet
 *	@len: the packet length including any SGE padding
 *	@drop_thres: # of remaining buffers before we start dropping packets
 *
 *	Get the next packet from a free list populated with page chunks.
 *	If the packet is small we make a copy and recycle the original buffer,
 *	otherwise we attach the original buffer as a page fragment to a fresh
 *	sk_buff.  If a positive drop threshold is supplied packets are dropped
 *	and their buffers recycled if (a) the number of remaining buffers is
 *	under the threshold and the packet is too big to copy, or (b) there's
 *	no system memory.
 *
 * 	Note: this function is similar to @get_packet but deals with Rx buffers
 * 	that are page chunks rather than sk_buffs.
 */
static struct sk_buff *get_packet_pg(struct adapter *adap, struct sge_fl *fl,
				     struct sge_rspq *q, unsigned int len,
				     unsigned int drop_thres)
{
	struct sk_buff *newskb, *skb;
	struct rx_sw_desc *sd = &fl->sdesc[fl->cidx];

	dma_addr_t dma_addr = dma_unmap_addr(sd, dma_addr);

	newskb = skb = q->pg_skb;
	if (!skb && (len <= SGE_RX_COPY_THRES)) {
		newskb = alloc_skb(len, GFP_ATOMIC);
		if (likely(newskb != NULL)) {
			__skb_put(newskb, len);
			pci_dma_sync_single_for_cpu(adap->pdev, dma_addr, len,
					    PCI_DMA_FROMDEVICE);
			memcpy(newskb->data, sd->pg_chunk.va, len);
			pci_dma_sync_single_for_device(adap->pdev, dma_addr,
						       len,
						       PCI_DMA_FROMDEVICE);
		} else if (!drop_thres)
			return NULL;
recycle:
		fl->credits--;
		recycle_rx_buf(adap, fl, fl->cidx);
		q->rx_recycle_buf++;
		return newskb;
	}

	if (unlikely(q->rx_recycle_buf || (!skb && fl->credits <= drop_thres)))
		goto recycle;

	prefetch(sd->pg_chunk.p_cnt);

	if (!skb)
		newskb = alloc_skb(SGE_RX_PULL_LEN, GFP_ATOMIC);

	if (unlikely(!newskb)) {
		if (!drop_thres)
			return NULL;
		goto recycle;
	}

	pci_dma_sync_single_for_cpu(adap->pdev, dma_addr, len,
				    PCI_DMA_FROMDEVICE);
	(*sd->pg_chunk.p_cnt)--;
	if (!*sd->pg_chunk.p_cnt && sd->pg_chunk.page != fl->pg_chunk.page)
		pci_unmap_page(adap->pdev,
			       sd->pg_chunk.mapping,
			       fl->alloc_size,
			       PCI_DMA_FROMDEVICE);
	if (!skb) {
		__skb_put(newskb, SGE_RX_PULL_LEN);
		memcpy(newskb->data, sd->pg_chunk.va, SGE_RX_PULL_LEN);
		skb_fill_page_desc(newskb, 0, sd->pg_chunk.page,
				   sd->pg_chunk.offset + SGE_RX_PULL_LEN,
				   len - SGE_RX_PULL_LEN);
		newskb->len = len;
		newskb->data_len = len - SGE_RX_PULL_LEN;
		newskb->truesize += newskb->data_len;
	} else {
		skb_fill_page_desc(newskb, skb_shinfo(newskb)->nr_frags,
				   sd->pg_chunk.page,
				   sd->pg_chunk.offset, len);
		newskb->len += len;
		newskb->data_len += len;
		newskb->truesize += len;
	}

	fl->credits--;
	/*
	 * We do not refill FLs here, we let the caller do it to overlap a
	 * prefetch.
	 */
	return newskb;
}

/**
 *	get_imm_packet - return the next ingress packet buffer from a response
 *	@resp: the response descriptor containing the packet data
 *
 *	Return a packet containing the immediate data of the given response.
 */
static inline struct sk_buff *get_imm_packet(const struct rsp_desc *resp)
{
	struct sk_buff *skb = alloc_skb(IMMED_PKT_SIZE, GFP_ATOMIC);

	if (skb) {
		__skb_put(skb, IMMED_PKT_SIZE);
		skb_copy_to_linear_data(skb, resp->imm_data, IMMED_PKT_SIZE);
	}
	return skb;
}

/**
 *	calc_tx_descs - calculate the number of Tx descriptors for a packet
 *	@skb: the packet
 *
 * 	Returns the number of Tx descriptors needed for the given Ethernet
 * 	packet.  Ethernet packets require addition of WR and CPL headers.
 */
static inline unsigned int calc_tx_descs(const struct sk_buff *skb)
{
	unsigned int flits;

	if (skb->len <= WR_LEN - sizeof(struct cpl_tx_pkt))
		return 1;

	flits = sgl_len(skb_shinfo(skb)->nr_frags + 1) + 2;
	if (skb_shinfo(skb)->gso_size)
		flits++;
	return flits_to_desc(flits);
}

/**
 *	make_sgl - populate a scatter/gather list for a packet
 *	@skb: the packet
 *	@sgp: the SGL to populate
 *	@start: start address of skb main body data to include in the SGL
 *	@len: length of skb main body data to include in the SGL
 *	@pdev: the PCI device
 *
 *	Generates a scatter/gather list for the buffers that make up a packet
 *	and returns the SGL size in 8-byte words.  The caller must size the SGL
 *	appropriately.
 */
static inline unsigned int make_sgl(const struct sk_buff *skb,
				    struct sg_ent *sgp, unsigned char *start,
				    unsigned int len, struct pci_dev *pdev)
{
	dma_addr_t mapping;
	unsigned int i, j = 0, nfrags;

	if (len) {
		mapping = pci_map_single(pdev, start, len, PCI_DMA_TODEVICE);
		sgp->len[0] = cpu_to_be32(len);
		sgp->addr[0] = cpu_to_be64(mapping);
		j = 1;
	}

	nfrags = skb_shinfo(skb)->nr_frags;
	for (i = 0; i < nfrags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		mapping = skb_frag_dma_map(&pdev->dev, frag, 0, skb_frag_size(frag),
					   DMA_TO_DEVICE);
		sgp->len[j] = cpu_to_be32(skb_frag_size(frag));
		sgp->addr[j] = cpu_to_be64(mapping);
		j ^= 1;
		if (j == 0)
			++sgp;
	}
	if (j)
		sgp->len[j] = 0;
	return ((nfrags + (len != 0)) * 3) / 2 + j;
}

/**
 *	check_ring_tx_db - check and potentially ring a Tx queue's doorbell
 *	@adap: the adapter
 *	@q: the Tx queue
 *
 *	Ring the doorbel if a Tx queue is asleep.  There is a natural race,
 *	where the HW is going to sleep just after we checked, however,
 *	then the interrupt handler will detect the outstanding TX packet
 *	and ring the doorbell for us.
 *
 *	When GTS is disabled we unconditionally ring the doorbell.
 */
static inline void check_ring_tx_db(struct adapter *adap, struct sge_txq *q)
{
#if USE_GTS
	clear_bit(TXQ_LAST_PKT_DB, &q->flags);
	if (test_and_set_bit(TXQ_RUNNING, &q->flags) == 0) {
		set_bit(TXQ_LAST_PKT_DB, &q->flags);
		t3_write_reg(adap, A_SG_KDOORBELL,
			     F_SELEGRCNTX | V_EGRCNTX(q->cntxt_id));
	}
#else
	wmb();			/* write descriptors before telling HW */
	t3_write_reg(adap, A_SG_KDOORBELL,
		     F_SELEGRCNTX | V_EGRCNTX(q->cntxt_id));
#endif
}

static inline void wr_gen2(struct tx_desc *d, unsigned int gen)
{
#if SGE_NUM_GENBITS == 2
	d->flit[TX_DESC_FLITS - 1] = cpu_to_be64(gen);
#endif
}

/**
 *	write_wr_hdr_sgl - write a WR header and, optionally, SGL
 *	@ndesc: number of Tx descriptors spanned by the SGL
 *	@skb: the packet corresponding to the WR
 *	@d: first Tx descriptor to be written
 *	@pidx: index of above descriptors
 *	@q: the SGE Tx queue
 *	@sgl: the SGL
 *	@flits: number of flits to the start of the SGL in the first descriptor
 *	@sgl_flits: the SGL size in flits
 *	@gen: the Tx descriptor generation
 *	@wr_hi: top 32 bits of WR header based on WR type (big endian)
 *	@wr_lo: low 32 bits of WR header based on WR type (big endian)
 *
 *	Write a work request header and an associated SGL.  If the SGL is
 *	small enough to fit into one Tx descriptor it has already been written
 *	and we just need to write the WR header.  Otherwise we distribute the
 *	SGL across the number of descriptors it spans.
 */
static void write_wr_hdr_sgl(unsigned int ndesc, struct sk_buff *skb,
			     struct tx_desc *d, unsigned int pidx,
			     const struct sge_txq *q,
			     const struct sg_ent *sgl,
			     unsigned int flits, unsigned int sgl_flits,
			     unsigned int gen, __be32 wr_hi,
			     __be32 wr_lo)
{
	struct work_request_hdr *wrp = (struct work_request_hdr *)d;
	struct tx_sw_desc *sd = &q->sdesc[pidx];

	sd->skb = skb;
	if (need_skb_unmap()) {
		sd->fragidx = 0;
		sd->addr_idx = 0;
		sd->sflit = flits;
	}

	if (likely(ndesc == 1)) {
		sd->eop = 1;
		wrp->wr_hi = htonl(F_WR_SOP | F_WR_EOP | V_WR_DATATYPE(1) |
				   V_WR_SGLSFLT(flits)) | wr_hi;
		wmb();
		wrp->wr_lo = htonl(V_WR_LEN(flits + sgl_flits) |
				   V_WR_GEN(gen)) | wr_lo;
		wr_gen2(d, gen);
	} else {
		unsigned int ogen = gen;
		const u64 *fp = (const u64 *)sgl;
		struct work_request_hdr *wp = wrp;

		wrp->wr_hi = htonl(F_WR_SOP | V_WR_DATATYPE(1) |
				   V_WR_SGLSFLT(flits)) | wr_hi;

		while (sgl_flits) {
			unsigned int avail = WR_FLITS - flits;

			if (avail > sgl_flits)
				avail = sgl_flits;
			memcpy(&d->flit[flits], fp, avail * sizeof(*fp));
			sgl_flits -= avail;
			ndesc--;
			if (!sgl_flits)
				break;

			fp += avail;
			d++;
			sd->eop = 0;
			sd++;
			if (++pidx == q->size) {
				pidx = 0;
				gen ^= 1;
				d = q->desc;
				sd = q->sdesc;
			}

			sd->skb = skb;
			wrp = (struct work_request_hdr *)d;
			wrp->wr_hi = htonl(V_WR_DATATYPE(1) |
					   V_WR_SGLSFLT(1)) | wr_hi;
			wrp->wr_lo = htonl(V_WR_LEN(min(WR_FLITS,
							sgl_flits + 1)) |
					   V_WR_GEN(gen)) | wr_lo;
			wr_gen2(d, gen);
			flits = 1;
		}
		sd->eop = 1;
		wrp->wr_hi |= htonl(F_WR_EOP);
		wmb();
		wp->wr_lo = htonl(V_WR_LEN(WR_FLITS) | V_WR_GEN(ogen)) | wr_lo;
		wr_gen2((struct tx_desc *)wp, ogen);
		WARN_ON(ndesc != 0);
	}
}

/**
 *	write_tx_pkt_wr - write a TX_PKT work request
 *	@adap: the adapter
 *	@skb: the packet to send
 *	@pi: the egress interface
 *	@pidx: index of the first Tx descriptor to write
 *	@gen: the generation value to use
 *	@q: the Tx queue
 *	@ndesc: number of descriptors the packet will occupy
 *	@compl: the value of the COMPL bit to use
 *
 *	Generate a TX_PKT work request to send the supplied packet.
 */
static void write_tx_pkt_wr(struct adapter *adap, struct sk_buff *skb,
			    const struct port_info *pi,
			    unsigned int pidx, unsigned int gen,
			    struct sge_txq *q, unsigned int ndesc,
			    unsigned int compl)
{
	unsigned int flits, sgl_flits, cntrl, tso_info;
	struct sg_ent *sgp, sgl[MAX_SKB_FRAGS / 2 + 1];
	struct tx_desc *d = &q->desc[pidx];
	struct cpl_tx_pkt *cpl = (struct cpl_tx_pkt *)d;

	cpl->len = htonl(skb->len);
	cntrl = V_TXPKT_INTF(pi->port_id);

	if (vlan_tx_tag_present(skb))
		cntrl |= F_TXPKT_VLAN_VLD | V_TXPKT_VLAN(vlan_tx_tag_get(skb));

	tso_info = V_LSO_MSS(skb_shinfo(skb)->gso_size);
	if (tso_info) {
		int eth_type;
		struct cpl_tx_pkt_lso *hdr = (struct cpl_tx_pkt_lso *)cpl;

		d->flit[2] = 0;
		cntrl |= V_TXPKT_OPCODE(CPL_TX_PKT_LSO);
		hdr->cntrl = htonl(cntrl);
		eth_type = skb_network_offset(skb) == ETH_HLEN ?
		    CPL_ETH_II : CPL_ETH_II_VLAN;
		tso_info |= V_LSO_ETH_TYPE(eth_type) |
		    V_LSO_IPHDR_WORDS(ip_hdr(skb)->ihl) |
		    V_LSO_TCPHDR_WORDS(tcp_hdr(skb)->doff);
		hdr->lso_info = htonl(tso_info);
		flits = 3;
	} else {
		cntrl |= V_TXPKT_OPCODE(CPL_TX_PKT);
		cntrl |= F_TXPKT_IPCSUM_DIS;	/* SW calculates IP csum */
		cntrl |= V_TXPKT_L4CSUM_DIS(skb->ip_summed != CHECKSUM_PARTIAL);
		cpl->cntrl = htonl(cntrl);

		if (skb->len <= WR_LEN - sizeof(*cpl)) {
			q->sdesc[pidx].skb = NULL;
			if (!skb->data_len)
				skb_copy_from_linear_data(skb, &d->flit[2],
							  skb->len);
			else
				skb_copy_bits(skb, 0, &d->flit[2], skb->len);

			flits = (skb->len + 7) / 8 + 2;
			cpl->wr.wr_hi = htonl(V_WR_BCNTLFLT(skb->len & 7) |
					      V_WR_OP(FW_WROPCODE_TUNNEL_TX_PKT)
					      | F_WR_SOP | F_WR_EOP | compl);
			wmb();
			cpl->wr.wr_lo = htonl(V_WR_LEN(flits) | V_WR_GEN(gen) |
					      V_WR_TID(q->token));
			wr_gen2(d, gen);
			kfree_skb(skb);
			return;
		}

		flits = 2;
	}

	sgp = ndesc == 1 ? (struct sg_ent *)&d->flit[flits] : sgl;
	sgl_flits = make_sgl(skb, sgp, skb->data, skb_headlen(skb), adap->pdev);

	write_wr_hdr_sgl(ndesc, skb, d, pidx, q, sgl, flits, sgl_flits, gen,
			 htonl(V_WR_OP(FW_WROPCODE_TUNNEL_TX_PKT) | compl),
			 htonl(V_WR_TID(q->token)));
}

static inline void t3_stop_tx_queue(struct netdev_queue *txq,
				    struct sge_qset *qs, struct sge_txq *q)
{
	netif_tx_stop_queue(txq);
	set_bit(TXQ_ETH, &qs->txq_stopped);
	q->stops++;
}

/**
 *	eth_xmit - add a packet to the Ethernet Tx queue
 *	@skb: the packet
 *	@dev: the egress net device
 *
 *	Add a packet to an SGE Tx queue.  Runs with softirqs disabled.
 */
netdev_tx_t t3_eth_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int qidx;
	unsigned int ndesc, pidx, credits, gen, compl;
	const struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	struct netdev_queue *txq;
	struct sge_qset *qs;
	struct sge_txq *q;

	/*
	 * The chip min packet length is 9 octets but play safe and reject
	 * anything shorter than an Ethernet header.
	 */
	if (unlikely(skb->len < ETH_HLEN)) {
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	qidx = skb_get_queue_mapping(skb);
	qs = &pi->qs[qidx];
	q = &qs->txq[TXQ_ETH];
	txq = netdev_get_tx_queue(dev, qidx);

	reclaim_completed_tx(adap, q, TX_RECLAIM_CHUNK);

	credits = q->size - q->in_use;
	ndesc = calc_tx_descs(skb);

	if (unlikely(credits < ndesc)) {
		t3_stop_tx_queue(txq, qs, q);
		dev_err(&adap->pdev->dev,
			"%s: Tx ring %u full while queue awake!\n",
			dev->name, q->cntxt_id & 7);
		return NETDEV_TX_BUSY;
	}

	q->in_use += ndesc;
	if (unlikely(credits - ndesc < q->stop_thres)) {
		t3_stop_tx_queue(txq, qs, q);

		if (should_restart_tx(q) &&
		    test_and_clear_bit(TXQ_ETH, &qs->txq_stopped)) {
			q->restarts++;
			netif_tx_start_queue(txq);
		}
	}

	gen = q->gen;
	q->unacked += ndesc;
	compl = (q->unacked & 8) << (S_WR_COMPL - 3);
	q->unacked &= 7;
	pidx = q->pidx;
	q->pidx += ndesc;
	if (q->pidx >= q->size) {
		q->pidx -= q->size;
		q->gen ^= 1;
	}

	/* update port statistics */
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		qs->port_stats[SGE_PSTAT_TX_CSUM]++;
	if (skb_shinfo(skb)->gso_size)
		qs->port_stats[SGE_PSTAT_TSO]++;
	if (vlan_tx_tag_present(skb))
		qs->port_stats[SGE_PSTAT_VLANINS]++;

	/*
	 * We do not use Tx completion interrupts to free DMAd Tx packets.
	 * This is good for performance but means that we rely on new Tx
	 * packets arriving to run the destructors of completed packets,
	 * which open up space in their sockets' send queues.  Sometimes
	 * we do not get such new packets causing Tx to stall.  A single
	 * UDP transmitter is a good example of this situation.  We have
	 * a clean up timer that periodically reclaims completed packets
	 * but it doesn't run often enough (nor do we want it to) to prevent
	 * lengthy stalls.  A solution to this problem is to run the
	 * destructor early, after the packet is queued but before it's DMAd.
	 * A cons is that we lie to socket memory accounting, but the amount
	 * of extra memory is reasonable (limited by the number of Tx
	 * descriptors), the packets do actually get freed quickly by new
	 * packets almost always, and for protocols like TCP that wait for
	 * acks to really free up the data the extra memory is even less.
	 * On the positive side we run the destructors on the sending CPU
	 * rather than on a potentially different completing CPU, usually a
	 * good thing.  We also run them without holding our Tx queue lock,
	 * unlike what reclaim_completed_tx() would otherwise do.
	 *
	 * Run the destructor before telling the DMA engine about the packet
	 * to make sure it doesn't complete and get freed prematurely.
	 */
	if (likely(!skb_shared(skb)))
		skb_orphan(skb);

	write_tx_pkt_wr(adap, skb, pi, pidx, gen, q, ndesc, compl);
	check_ring_tx_db(adap, q);
	return NETDEV_TX_OK;
}

/**
 *	write_imm - write a packet into a Tx descriptor as immediate data
 *	@d: the Tx descriptor to write
 *	@skb: the packet
 *	@len: the length of packet data to write as immediate data
 *	@gen: the generation bit value to write
 *
 *	Writes a packet as immediate data into a Tx descriptor.  The packet
 *	contains a work request at its beginning.  We must write the packet
 *	carefully so the SGE doesn't read it accidentally before it's written
 *	in its entirety.
 */
static inline void write_imm(struct tx_desc *d, struct sk_buff *skb,
			     unsigned int len, unsigned int gen)
{
	struct work_request_hdr *from = (struct work_request_hdr *)skb->data;
	struct work_request_hdr *to = (struct work_request_hdr *)d;

	if (likely(!skb->data_len))
		memcpy(&to[1], &from[1], len - sizeof(*from));
	else
		skb_copy_bits(skb, sizeof(*from), &to[1], len - sizeof(*from));

	to->wr_hi = from->wr_hi | htonl(F_WR_SOP | F_WR_EOP |
					V_WR_BCNTLFLT(len & 7));
	wmb();
	to->wr_lo = from->wr_lo | htonl(V_WR_GEN(gen) |
					V_WR_LEN((len + 7) / 8));
	wr_gen2(d, gen);
	kfree_skb(skb);
}

/**
 *	check_desc_avail - check descriptor availability on a send queue
 *	@adap: the adapter
 *	@q: the send queue
 *	@skb: the packet needing the descriptors
 *	@ndesc: the number of Tx descriptors needed
 *	@qid: the Tx queue number in its queue set (TXQ_OFLD or TXQ_CTRL)
 *
 *	Checks if the requested number of Tx descriptors is available on an
 *	SGE send queue.  If the queue is already suspended or not enough
 *	descriptors are available the packet is queued for later transmission.
 *	Must be called with the Tx queue locked.
 *
 *	Returns 0 if enough descriptors are available, 1 if there aren't
 *	enough descriptors and the packet has been queued, and 2 if the caller
 *	needs to retry because there weren't enough descriptors at the
 *	beginning of the call but some freed up in the mean time.
 */
static inline int check_desc_avail(struct adapter *adap, struct sge_txq *q,
				   struct sk_buff *skb, unsigned int ndesc,
				   unsigned int qid)
{
	if (unlikely(!skb_queue_empty(&q->sendq))) {
	      addq_exit:__skb_queue_tail(&q->sendq, skb);
		return 1;
	}
	if (unlikely(q->size - q->in_use < ndesc)) {
		struct sge_qset *qs = txq_to_qset(q, qid);

		set_bit(qid, &qs->txq_stopped);
		smp_mb__after_clear_bit();

		if (should_restart_tx(q) &&
		    test_and_clear_bit(qid, &qs->txq_stopped))
			return 2;

		q->stops++;
		goto addq_exit;
	}
	return 0;
}

/**
 *	reclaim_completed_tx_imm - reclaim completed control-queue Tx descs
 *	@q: the SGE control Tx queue
 *
 *	This is a variant of reclaim_completed_tx() that is used for Tx queues
 *	that send only immediate data (presently just the control queues) and
 *	thus do not have any sk_buffs to release.
 */
static inline void reclaim_completed_tx_imm(struct sge_txq *q)
{
	unsigned int reclaim = q->processed - q->cleaned;

	q->in_use -= reclaim;
	q->cleaned += reclaim;
}

static inline int immediate(const struct sk_buff *skb)
{
	return skb->len <= WR_LEN;
}

/**
 *	ctrl_xmit - send a packet through an SGE control Tx queue
 *	@adap: the adapter
 *	@q: the control queue
 *	@skb: the packet
 *
 *	Send a packet through an SGE control Tx queue.  Packets sent through
 *	a control queue must fit entirely as immediate data in a single Tx
 *	descriptor and have no page fragments.
 */
static int ctrl_xmit(struct adapter *adap, struct sge_txq *q,
		     struct sk_buff *skb)
{
	int ret;
	struct work_request_hdr *wrp = (struct work_request_hdr *)skb->data;

	if (unlikely(!immediate(skb))) {
		WARN_ON(1);
		dev_kfree_skb(skb);
		return NET_XMIT_SUCCESS;
	}

	wrp->wr_hi |= htonl(F_WR_SOP | F_WR_EOP);
	wrp->wr_lo = htonl(V_WR_TID(q->token));

	spin_lock(&q->lock);
      again:reclaim_completed_tx_imm(q);

	ret = check_desc_avail(adap, q, skb, 1, TXQ_CTRL);
	if (unlikely(ret)) {
		if (ret == 1) {
			spin_unlock(&q->lock);
			return NET_XMIT_CN;
		}
		goto again;
	}

	write_imm(&q->desc[q->pidx], skb, skb->len, q->gen);

	q->in_use++;
	if (++q->pidx >= q->size) {
		q->pidx = 0;
		q->gen ^= 1;
	}
	spin_unlock(&q->lock);
	wmb();
	t3_write_reg(adap, A_SG_KDOORBELL,
		     F_SELEGRCNTX | V_EGRCNTX(q->cntxt_id));
	return NET_XMIT_SUCCESS;
}

/**
 *	restart_ctrlq - restart a suspended control queue
 *	@qs: the queue set cotaining the control queue
 *
 *	Resumes transmission on a suspended Tx control queue.
 */
static void restart_ctrlq(unsigned long data)
{
	struct sk_buff *skb;
	struct sge_qset *qs = (struct sge_qset *)data;
	struct sge_txq *q = &qs->txq[TXQ_CTRL];

	spin_lock(&q->lock);
      again:reclaim_completed_tx_imm(q);

	while (q->in_use < q->size &&
	       (skb = __skb_dequeue(&q->sendq)) != NULL) {

		write_imm(&q->desc[q->pidx], skb, skb->len, q->gen);

		if (++q->pidx >= q->size) {
			q->pidx = 0;
			q->gen ^= 1;
		}
		q->in_use++;
	}

	if (!skb_queue_empty(&q->sendq)) {
		set_bit(TXQ_CTRL, &qs->txq_stopped);
		smp_mb__after_clear_bit();

		if (should_restart_tx(q) &&
		    test_and_clear_bit(TXQ_CTRL, &qs->txq_stopped))
			goto again;
		q->stops++;
	}

	spin_unlock(&q->lock);
	wmb();
	t3_write_reg(qs->adap, A_SG_KDOORBELL,
		     F_SELEGRCNTX | V_EGRCNTX(q->cntxt_id));
}

/*
 * Send a management message through control queue 0
 */
int t3_mgmt_tx(struct adapter *adap, struct sk_buff *skb)
{
	int ret;
	local_bh_disable();
	ret = ctrl_xmit(adap, &adap->sge.qs[0].txq[TXQ_CTRL], skb);
	local_bh_enable();

	return ret;
}

/**
 *	deferred_unmap_destructor - unmap a packet when it is freed
 *	@skb: the packet
 *
 *	This is the packet destructor used for Tx packets that need to remain
 *	mapped until they are freed rather than until their Tx descriptors are
 *	freed.
 */
static void deferred_unmap_destructor(struct sk_buff *skb)
{
	int i;
	const dma_addr_t *p;
	const struct skb_shared_info *si;
	const struct deferred_unmap_info *dui;

	dui = (struct deferred_unmap_info *)skb->head;
	p = dui->addr;

	if (skb->tail - skb->transport_header)
		pci_unmap_single(dui->pdev, *p++,
				 skb->tail - skb->transport_header,
				 PCI_DMA_TODEVICE);

	si = skb_shinfo(skb);
	for (i = 0; i < si->nr_frags; i++)
		pci_unmap_page(dui->pdev, *p++, skb_frag_size(&si->frags[i]),
			       PCI_DMA_TODEVICE);
}

static void setup_deferred_unmapping(struct sk_buff *skb, struct pci_dev *pdev,
				     const struct sg_ent *sgl, int sgl_flits)
{
	dma_addr_t *p;
	struct deferred_unmap_info *dui;

	dui = (struct deferred_unmap_info *)skb->head;
	dui->pdev = pdev;
	for (p = dui->addr; sgl_flits >= 3; sgl++, sgl_flits -= 3) {
		*p++ = be64_to_cpu(sgl->addr[0]);
		*p++ = be64_to_cpu(sgl->addr[1]);
	}
	if (sgl_flits)
		*p = be64_to_cpu(sgl->addr[0]);
}

/**
 *	write_ofld_wr - write an offload work request
 *	@adap: the adapter
 *	@skb: the packet to send
 *	@q: the Tx queue
 *	@pidx: index of the first Tx descriptor to write
 *	@gen: the generation value to use
 *	@ndesc: number of descriptors the packet will occupy
 *
 *	Write an offload work request to send the supplied packet.  The packet
 *	data already carry the work request with most fields populated.
 */
static void write_ofld_wr(struct adapter *adap, struct sk_buff *skb,
			  struct sge_txq *q, unsigned int pidx,
			  unsigned int gen, unsigned int ndesc)
{
	unsigned int sgl_flits, flits;
	struct work_request_hdr *from;
	struct sg_ent *sgp, sgl[MAX_SKB_FRAGS / 2 + 1];
	struct tx_desc *d = &q->desc[pidx];

	if (immediate(skb)) {
		q->sdesc[pidx].skb = NULL;
		write_imm(d, skb, skb->len, gen);
		return;
	}

	/* Only TX_DATA builds SGLs */

	from = (struct work_request_hdr *)skb->data;
	memcpy(&d->flit[1], &from[1],
	       skb_transport_offset(skb) - sizeof(*from));

	flits = skb_transport_offset(skb) / 8;
	sgp = ndesc == 1 ? (struct sg_ent *)&d->flit[flits] : sgl;
	sgl_flits = make_sgl(skb, sgp, skb_transport_header(skb),
			     skb->tail - skb->transport_header,
			     adap->pdev);
	if (need_skb_unmap()) {
		setup_deferred_unmapping(skb, adap->pdev, sgp, sgl_flits);
		skb->destructor = deferred_unmap_destructor;
	}

	write_wr_hdr_sgl(ndesc, skb, d, pidx, q, sgl, flits, sgl_flits,
			 gen, from->wr_hi, from->wr_lo);
}

/**
 *	calc_tx_descs_ofld - calculate # of Tx descriptors for an offload packet
 *	@skb: the packet
 *
 * 	Returns the number of Tx descriptors needed for the given offload
 * 	packet.  These packets are already fully constructed.
 */
static inline unsigned int calc_tx_descs_ofld(const struct sk_buff *skb)
{
	unsigned int flits, cnt;

	if (skb->len <= WR_LEN)
		return 1;	/* packet fits as immediate data */

	flits = skb_transport_offset(skb) / 8;	/* headers */
	cnt = skb_shinfo(skb)->nr_frags;
	if (skb->tail != skb->transport_header)
		cnt++;
	return flits_to_desc(flits + sgl_len(cnt));
}

/**
 *	ofld_xmit - send a packet through an offload queue
 *	@adap: the adapter
 *	@q: the Tx offload queue
 *	@skb: the packet
 *
 *	Send an offload packet through an SGE offload queue.
 */
static int ofld_xmit(struct adapter *adap, struct sge_txq *q,
		     struct sk_buff *skb)
{
	int ret;
	unsigned int ndesc = calc_tx_descs_ofld(skb), pidx, gen;

	spin_lock(&q->lock);
again:	reclaim_completed_tx(adap, q, TX_RECLAIM_CHUNK);

	ret = check_desc_avail(adap, q, skb, ndesc, TXQ_OFLD);
	if (unlikely(ret)) {
		if (ret == 1) {
			skb->priority = ndesc;	/* save for restart */
			spin_unlock(&q->lock);
			return NET_XMIT_CN;
		}
		goto again;
	}

	gen = q->gen;
	q->in_use += ndesc;
	pidx = q->pidx;
	q->pidx += ndesc;
	if (q->pidx >= q->size) {
		q->pidx -= q->size;
		q->gen ^= 1;
	}
	spin_unlock(&q->lock);

	write_ofld_wr(adap, skb, q, pidx, gen, ndesc);
	check_ring_tx_db(adap, q);
	return NET_XMIT_SUCCESS;
}

/**
 *	restart_offloadq - restart a suspended offload queue
 *	@qs: the queue set cotaining the offload queue
 *
 *	Resumes transmission on a suspended Tx offload queue.
 */
static void restart_offloadq(unsigned long data)
{
	struct sk_buff *skb;
	struct sge_qset *qs = (struct sge_qset *)data;
	struct sge_txq *q = &qs->txq[TXQ_OFLD];
	const struct port_info *pi = netdev_priv(qs->netdev);
	struct adapter *adap = pi->adapter;

	spin_lock(&q->lock);
again:	reclaim_completed_tx(adap, q, TX_RECLAIM_CHUNK);

	while ((skb = skb_peek(&q->sendq)) != NULL) {
		unsigned int gen, pidx;
		unsigned int ndesc = skb->priority;

		if (unlikely(q->size - q->in_use < ndesc)) {
			set_bit(TXQ_OFLD, &qs->txq_stopped);
			smp_mb__after_clear_bit();

			if (should_restart_tx(q) &&
			    test_and_clear_bit(TXQ_OFLD, &qs->txq_stopped))
				goto again;
			q->stops++;
			break;
		}

		gen = q->gen;
		q->in_use += ndesc;
		pidx = q->pidx;
		q->pidx += ndesc;
		if (q->pidx >= q->size) {
			q->pidx -= q->size;
			q->gen ^= 1;
		}
		__skb_unlink(skb, &q->sendq);
		spin_unlock(&q->lock);

		write_ofld_wr(adap, skb, q, pidx, gen, ndesc);
		spin_lock(&q->lock);
	}
	spin_unlock(&q->lock);

#if USE_GTS
	set_bit(TXQ_RUNNING, &q->flags);
	set_bit(TXQ_LAST_PKT_DB, &q->flags);
#endif
	wmb();
	t3_write_reg(adap, A_SG_KDOORBELL,
		     F_SELEGRCNTX | V_EGRCNTX(q->cntxt_id));
}

/**
 *	queue_set - return the queue set a packet should use
 *	@skb: the packet
 *
 *	Maps a packet to the SGE queue set it should use.  The desired queue
 *	set is carried in bits 1-3 in the packet's priority.
 */
static inline int queue_set(const struct sk_buff *skb)
{
	return skb->priority >> 1;
}

/**
 *	is_ctrl_pkt - return whether an offload packet is a control packet
 *	@skb: the packet
 *
 *	Determines whether an offload packet should use an OFLD or a CTRL
 *	Tx queue.  This is indicated by bit 0 in the packet's priority.
 */
static inline int is_ctrl_pkt(const struct sk_buff *skb)
{
	return skb->priority & 1;
}

/**
 *	t3_offload_tx - send an offload packet
 *	@tdev: the offload device to send to
 *	@skb: the packet
 *
 *	Sends an offload packet.  We use the packet priority to select the
 *	appropriate Tx queue as follows: bit 0 indicates whether the packet
 *	should be sent as regular or control, bits 1-3 select the queue set.
 */
int t3_offload_tx(struct t3cdev *tdev, struct sk_buff *skb)
{
	struct adapter *adap = tdev2adap(tdev);
	struct sge_qset *qs = &adap->sge.qs[queue_set(skb)];

	if (unlikely(is_ctrl_pkt(skb)))
		return ctrl_xmit(adap, &qs->txq[TXQ_CTRL], skb);

	return ofld_xmit(adap, &qs->txq[TXQ_OFLD], skb);
}

/**
 *	offload_enqueue - add an offload packet to an SGE offload receive queue
 *	@q: the SGE response queue
 *	@skb: the packet
 *
 *	Add a new offload packet to an SGE response queue's offload packet
 *	queue.  If the packet is the first on the queue it schedules the RX
 *	softirq to process the queue.
 */
static inline void offload_enqueue(struct sge_rspq *q, struct sk_buff *skb)
{
	int was_empty = skb_queue_empty(&q->rx_queue);

	__skb_queue_tail(&q->rx_queue, skb);

	if (was_empty) {
		struct sge_qset *qs = rspq_to_qset(q);

		napi_schedule(&qs->napi);
	}
}

/**
 *	deliver_partial_bundle - deliver a (partial) bundle of Rx offload pkts
 *	@tdev: the offload device that will be receiving the packets
 *	@q: the SGE response queue that assembled the bundle
 *	@skbs: the partial bundle
 *	@n: the number of packets in the bundle
 *
 *	Delivers a (partial) bundle of Rx offload packets to an offload device.
 */
static inline void deliver_partial_bundle(struct t3cdev *tdev,
					  struct sge_rspq *q,
					  struct sk_buff *skbs[], int n)
{
	if (n) {
		q->offload_bundles++;
		tdev->recv(tdev, skbs, n);
	}
}

/**
 *	ofld_poll - NAPI handler for offload packets in interrupt mode
 *	@dev: the network device doing the polling
 *	@budget: polling budget
 *
 *	The NAPI handler for offload packets when a response queue is serviced
 *	by the hard interrupt handler, i.e., when it's operating in non-polling
 *	mode.  Creates small packet batches and sends them through the offload
 *	receive handler.  Batches need to be of modest size as we do prefetches
 *	on the packets in each.
 */
static int ofld_poll(struct napi_struct *napi, int budget)
{
	struct sge_qset *qs = container_of(napi, struct sge_qset, napi);
	struct sge_rspq *q = &qs->rspq;
	struct adapter *adapter = qs->adap;
	int work_done = 0;

	while (work_done < budget) {
		struct sk_buff *skb, *tmp, *skbs[RX_BUNDLE_SIZE];
		struct sk_buff_head queue;
		int ngathered;

		spin_lock_irq(&q->lock);
		__skb_queue_head_init(&queue);
		skb_queue_splice_init(&q->rx_queue, &queue);
		if (skb_queue_empty(&queue)) {
			napi_complete(napi);
			spin_unlock_irq(&q->lock);
			return work_done;
		}
		spin_unlock_irq(&q->lock);

		ngathered = 0;
		skb_queue_walk_safe(&queue, skb, tmp) {
			if (work_done >= budget)
				break;
			work_done++;

			__skb_unlink(skb, &queue);
			prefetch(skb->data);
			skbs[ngathered] = skb;
			if (++ngathered == RX_BUNDLE_SIZE) {
				q->offload_bundles++;
				adapter->tdev.recv(&adapter->tdev, skbs,
						   ngathered);
				ngathered = 0;
			}
		}
		if (!skb_queue_empty(&queue)) {
			/* splice remaining packets back onto Rx queue */
			spin_lock_irq(&q->lock);
			skb_queue_splice(&queue, &q->rx_queue);
			spin_unlock_irq(&q->lock);
		}
		deliver_partial_bundle(&adapter->tdev, q, skbs, ngathered);
	}

	return work_done;
}

/**
 *	rx_offload - process a received offload packet
 *	@tdev: the offload device receiving the packet
 *	@rq: the response queue that received the packet
 *	@skb: the packet
 *	@rx_gather: a gather list of packets if we are building a bundle
 *	@gather_idx: index of the next available slot in the bundle
 *
 *	Process an ingress offload pakcet and add it to the offload ingress
 *	queue. 	Returns the index of the next available slot in the bundle.
 */
static inline int rx_offload(struct t3cdev *tdev, struct sge_rspq *rq,
			     struct sk_buff *skb, struct sk_buff *rx_gather[],
			     unsigned int gather_idx)
{
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);

	if (rq->polling) {
		rx_gather[gather_idx++] = skb;
		if (gather_idx == RX_BUNDLE_SIZE) {
			tdev->recv(tdev, rx_gather, RX_BUNDLE_SIZE);
			gather_idx = 0;
			rq->offload_bundles++;
		}
	} else
		offload_enqueue(rq, skb);

	return gather_idx;
}

/**
 *	restart_tx - check whether to restart suspended Tx queues
 *	@qs: the queue set to resume
 *
 *	Restarts suspended Tx queues of an SGE queue set if they have enough
 *	free resources to resume operation.
 */
static void restart_tx(struct sge_qset *qs)
{
	if (test_bit(TXQ_ETH, &qs->txq_stopped) &&
	    should_restart_tx(&qs->txq[TXQ_ETH]) &&
	    test_and_clear_bit(TXQ_ETH, &qs->txq_stopped)) {
		qs->txq[TXQ_ETH].restarts++;
		if (netif_running(qs->netdev))
			netif_tx_wake_queue(qs->tx_q);
	}

	if (test_bit(TXQ_OFLD, &qs->txq_stopped) &&
	    should_restart_tx(&qs->txq[TXQ_OFLD]) &&
	    test_and_clear_bit(TXQ_OFLD, &qs->txq_stopped)) {
		qs->txq[TXQ_OFLD].restarts++;
		tasklet_schedule(&qs->txq[TXQ_OFLD].qresume_tsk);
	}
	if (test_bit(TXQ_CTRL, &qs->txq_stopped) &&
	    should_restart_tx(&qs->txq[TXQ_CTRL]) &&
	    test_and_clear_bit(TXQ_CTRL, &qs->txq_stopped)) {
		qs->txq[TXQ_CTRL].restarts++;
		tasklet_schedule(&qs->txq[TXQ_CTRL].qresume_tsk);
	}
}

/**
 *	cxgb3_arp_process - process an ARP request probing a private IP address
 *	@adapter: the adapter
 *	@skb: the skbuff containing the ARP request
 *
 *	Check if the ARP request is probing the private IP address
 *	dedicated to iSCSI, generate an ARP reply if so.
 */
static void cxgb3_arp_process(struct port_info *pi, struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct arphdr *arp;
	unsigned char *arp_ptr;
	unsigned char *sha;
	__be32 sip, tip;

	if (!dev)
		return;

	skb_reset_network_header(skb);
	arp = arp_hdr(skb);

	if (arp->ar_op != htons(ARPOP_REQUEST))
		return;

	arp_ptr = (unsigned char *)(arp + 1);
	sha = arp_ptr;
	arp_ptr += dev->addr_len;
	memcpy(&sip, arp_ptr, sizeof(sip));
	arp_ptr += sizeof(sip);
	arp_ptr += dev->addr_len;
	memcpy(&tip, arp_ptr, sizeof(tip));

	if (tip != pi->iscsi_ipv4addr)
		return;

	arp_send(ARPOP_REPLY, ETH_P_ARP, sip, dev, tip, sha,
		 pi->iscsic.mac_addr, sha);

}

static inline int is_arp(struct sk_buff *skb)
{
	return skb->protocol == htons(ETH_P_ARP);
}

static void cxgb3_process_iscsi_prov_pack(struct port_info *pi,
					struct sk_buff *skb)
{
	if (is_arp(skb)) {
		cxgb3_arp_process(pi, skb);
		return;
	}

	if (pi->iscsic.recv)
		pi->iscsic.recv(pi, skb);

}

/**
 *	rx_eth - process an ingress ethernet packet
 *	@adap: the adapter
 *	@rq: the response queue that received the packet
 *	@skb: the packet
 *	@pad: amount of padding at the start of the buffer
 *
 *	Process an ingress ethernet pakcet and deliver it to the stack.
 *	The padding is 2 if the packet was delivered in an Rx buffer and 0
 *	if it was immediate data in a response.
 */
static void rx_eth(struct adapter *adap, struct sge_rspq *rq,
		   struct sk_buff *skb, int pad, int lro)
{
	struct cpl_rx_pkt *p = (struct cpl_rx_pkt *)(skb->data + pad);
	struct sge_qset *qs = rspq_to_qset(rq);
	struct port_info *pi;

	skb_pull(skb, sizeof(*p) + pad);
	skb->protocol = eth_type_trans(skb, adap->port[p->iff]);
	pi = netdev_priv(skb->dev);
	if ((skb->dev->features & NETIF_F_RXCSUM) && p->csum_valid &&
	    p->csum == htons(0xffff) && !p->fragment) {
		qs->port_stats[SGE_PSTAT_RX_CSUM_GOOD]++;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else
		skb_checksum_none_assert(skb);
	skb_record_rx_queue(skb, qs - &adap->sge.qs[pi->first_qset]);

	if (p->vlan_valid) {
		qs->port_stats[SGE_PSTAT_VLANEX]++;
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), ntohs(p->vlan));
	}
	if (rq->polling) {
		if (lro)
			napi_gro_receive(&qs->napi, skb);
		else {
			if (unlikely(pi->iscsic.flags))
				cxgb3_process_iscsi_prov_pack(pi, skb);
			netif_receive_skb(skb);
		}
	} else
		netif_rx(skb);
}

static inline int is_eth_tcp(u32 rss)
{
	return G_HASHTYPE(ntohl(rss)) == RSS_HASH_4_TUPLE;
}

/**
 *	lro_add_page - add a page chunk to an LRO session
 *	@adap: the adapter
 *	@qs: the associated queue set
 *	@fl: the free list containing the page chunk to add
 *	@len: packet length
 *	@complete: Indicates the last fragment of a frame
 *
 *	Add a received packet contained in a page chunk to an existing LRO
 *	session.
 */
static void lro_add_page(struct adapter *adap, struct sge_qset *qs,
			 struct sge_fl *fl, int len, int complete)
{
	struct rx_sw_desc *sd = &fl->sdesc[fl->cidx];
	struct port_info *pi = netdev_priv(qs->netdev);
	struct sk_buff *skb = NULL;
	struct cpl_rx_pkt *cpl;
	struct skb_frag_struct *rx_frag;
	int nr_frags;
	int offset = 0;

	if (!qs->nomem) {
		skb = napi_get_frags(&qs->napi);
		qs->nomem = !skb;
	}

	fl->credits--;

	pci_dma_sync_single_for_cpu(adap->pdev,
				    dma_unmap_addr(sd, dma_addr),
				    fl->buf_size - SGE_PG_RSVD,
				    PCI_DMA_FROMDEVICE);

	(*sd->pg_chunk.p_cnt)--;
	if (!*sd->pg_chunk.p_cnt && sd->pg_chunk.page != fl->pg_chunk.page)
		pci_unmap_page(adap->pdev,
			       sd->pg_chunk.mapping,
			       fl->alloc_size,
			       PCI_DMA_FROMDEVICE);

	if (!skb) {
		put_page(sd->pg_chunk.page);
		if (complete)
			qs->nomem = 0;
		return;
	}

	rx_frag = skb_shinfo(skb)->frags;
	nr_frags = skb_shinfo(skb)->nr_frags;

	if (!nr_frags) {
		offset = 2 + sizeof(struct cpl_rx_pkt);
		cpl = qs->lro_va = sd->pg_chunk.va + 2;

		if ((qs->netdev->features & NETIF_F_RXCSUM) &&
		     cpl->csum_valid && cpl->csum == htons(0xffff)) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			qs->port_stats[SGE_PSTAT_RX_CSUM_GOOD]++;
		} else
			skb->ip_summed = CHECKSUM_NONE;
	} else
		cpl = qs->lro_va;

	len -= offset;

	rx_frag += nr_frags;
	__skb_frag_set_page(rx_frag, sd->pg_chunk.page);
	rx_frag->page_offset = sd->pg_chunk.offset + offset;
	skb_frag_size_set(rx_frag, len);

	skb->len += len;
	skb->data_len += len;
	skb->truesize += len;
	skb_shinfo(skb)->nr_frags++;

	if (!complete)
		return;

	skb_record_rx_queue(skb, qs - &adap->sge.qs[pi->first_qset]);

	if (cpl->vlan_valid) {
		qs->port_stats[SGE_PSTAT_VLANEX]++;
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), ntohs(cpl->vlan));
	}
	napi_gro_frags(&qs->napi);
}

/**
 *	handle_rsp_cntrl_info - handles control information in a response
 *	@qs: the queue set corresponding to the response
 *	@flags: the response control flags
 *
 *	Handles the control information of an SGE response, such as GTS
 *	indications and completion credits for the queue set's Tx queues.
 *	HW coalesces credits, we don't do any extra SW coalescing.
 */
static inline void handle_rsp_cntrl_info(struct sge_qset *qs, u32 flags)
{
	unsigned int credits;

#if USE_GTS
	if (flags & F_RSPD_TXQ0_GTS)
		clear_bit(TXQ_RUNNING, &qs->txq[TXQ_ETH].flags);
#endif

	credits = G_RSPD_TXQ0_CR(flags);
	if (credits)
		qs->txq[TXQ_ETH].processed += credits;

	credits = G_RSPD_TXQ2_CR(flags);
	if (credits)
		qs->txq[TXQ_CTRL].processed += credits;

# if USE_GTS
	if (flags & F_RSPD_TXQ1_GTS)
		clear_bit(TXQ_RUNNING, &qs->txq[TXQ_OFLD].flags);
# endif
	credits = G_RSPD_TXQ1_CR(flags);
	if (credits)
		qs->txq[TXQ_OFLD].processed += credits;
}

/**
 *	check_ring_db - check if we need to ring any doorbells
 *	@adapter: the adapter
 *	@qs: the queue set whose Tx queues are to be examined
 *	@sleeping: indicates which Tx queue sent GTS
 *
 *	Checks if some of a queue set's Tx queues need to ring their doorbells
 *	to resume transmission after idling while they still have unprocessed
 *	descriptors.
 */
static void check_ring_db(struct adapter *adap, struct sge_qset *qs,
			  unsigned int sleeping)
{
	if (sleeping & F_RSPD_TXQ0_GTS) {
		struct sge_txq *txq = &qs->txq[TXQ_ETH];

		if (txq->cleaned + txq->in_use != txq->processed &&
		    !test_and_set_bit(TXQ_LAST_PKT_DB, &txq->flags)) {
			set_bit(TXQ_RUNNING, &txq->flags);
			t3_write_reg(adap, A_SG_KDOORBELL, F_SELEGRCNTX |
				     V_EGRCNTX(txq->cntxt_id));
		}
	}

	if (sleeping & F_RSPD_TXQ1_GTS) {
		struct sge_txq *txq = &qs->txq[TXQ_OFLD];

		if (txq->cleaned + txq->in_use != txq->processed &&
		    !test_and_set_bit(TXQ_LAST_PKT_DB, &txq->flags)) {
			set_bit(TXQ_RUNNING, &txq->flags);
			t3_write_reg(adap, A_SG_KDOORBELL, F_SELEGRCNTX |
				     V_EGRCNTX(txq->cntxt_id));
		}
	}
}

/**
 *	is_new_response - check if a response is newly written
 *	@r: the response descriptor
 *	@q: the response queue
 *
 *	Returns true if a response descriptor contains a yet unprocessed
 *	response.
 */
static inline int is_new_response(const struct rsp_desc *r,
				  const struct sge_rspq *q)
{
	return (r->intr_gen & F_RSPD_GEN2) == q->gen;
}

static inline void clear_rspq_bufstate(struct sge_rspq * const q)
{
	q->pg_skb = NULL;
	q->rx_recycle_buf = 0;
}

#define RSPD_GTS_MASK  (F_RSPD_TXQ0_GTS | F_RSPD_TXQ1_GTS)
#define RSPD_CTRL_MASK (RSPD_GTS_MASK | \
			V_RSPD_TXQ0_CR(M_RSPD_TXQ0_CR) | \
			V_RSPD_TXQ1_CR(M_RSPD_TXQ1_CR) | \
			V_RSPD_TXQ2_CR(M_RSPD_TXQ2_CR))

/* How long to delay the next interrupt in case of memory shortage, in 0.1us. */
#define NOMEM_INTR_DELAY 2500

/**
 *	process_responses - process responses from an SGE response queue
 *	@adap: the adapter
 *	@qs: the queue set to which the response queue belongs
 *	@budget: how many responses can be processed in this round
 *
 *	Process responses from an SGE response queue up to the supplied budget.
 *	Responses include received packets as well as credits and other events
 *	for the queues that belong to the response queue's queue set.
 *	A negative budget is effectively unlimited.
 *
 *	Additionally choose the interrupt holdoff time for the next interrupt
 *	on this queue.  If the system is under memory shortage use a fairly
 *	long delay to help recovery.
 */
static int process_responses(struct adapter *adap, struct sge_qset *qs,
			     int budget)
{
	struct sge_rspq *q = &qs->rspq;
	struct rsp_desc *r = &q->desc[q->cidx];
	int budget_left = budget;
	unsigned int sleeping = 0;
	struct sk_buff *offload_skbs[RX_BUNDLE_SIZE];
	int ngathered = 0;

	q->next_holdoff = q->holdoff_tmr;

	while (likely(budget_left && is_new_response(r, q))) {
		int packet_complete, eth, ethpad = 2;
		int lro = !!(qs->netdev->features & NETIF_F_GRO);
		struct sk_buff *skb = NULL;
		u32 len, flags;
		__be32 rss_hi, rss_lo;

		rmb();
		eth = r->rss_hdr.opcode == CPL_RX_PKT;
		rss_hi = *(const __be32 *)r;
		rss_lo = r->rss_hdr.rss_hash_val;
		flags = ntohl(r->flags);

		if (unlikely(flags & F_RSPD_ASYNC_NOTIF)) {
			skb = alloc_skb(AN_PKT_SIZE, GFP_ATOMIC);
			if (!skb)
				goto no_mem;

			memcpy(__skb_put(skb, AN_PKT_SIZE), r, AN_PKT_SIZE);
			skb->data[0] = CPL_ASYNC_NOTIF;
			rss_hi = htonl(CPL_ASYNC_NOTIF << 24);
			q->async_notif++;
		} else if (flags & F_RSPD_IMM_DATA_VALID) {
			skb = get_imm_packet(r);
			if (unlikely(!skb)) {
no_mem:
				q->next_holdoff = NOMEM_INTR_DELAY;
				q->nomem++;
				/* consume one credit since we tried */
				budget_left--;
				break;
			}
			q->imm_data++;
			ethpad = 0;
		} else if ((len = ntohl(r->len_cq)) != 0) {
			struct sge_fl *fl;

			lro &= eth && is_eth_tcp(rss_hi);

			fl = (len & F_RSPD_FLQ) ? &qs->fl[1] : &qs->fl[0];
			if (fl->use_pages) {
				void *addr = fl->sdesc[fl->cidx].pg_chunk.va;

				prefetch(addr);
#if L1_CACHE_BYTES < 128
				prefetch(addr + L1_CACHE_BYTES);
#endif
				__refill_fl(adap, fl);
				if (lro > 0) {
					lro_add_page(adap, qs, fl,
						     G_RSPD_LEN(len),
						     flags & F_RSPD_EOP);
					 goto next_fl;
				}

				skb = get_packet_pg(adap, fl, q,
						    G_RSPD_LEN(len),
						    eth ?
						    SGE_RX_DROP_THRES : 0);
				q->pg_skb = skb;
			} else
				skb = get_packet(adap, fl, G_RSPD_LEN(len),
						 eth ? SGE_RX_DROP_THRES : 0);
			if (unlikely(!skb)) {
				if (!eth)
					goto no_mem;
				q->rx_drops++;
			} else if (unlikely(r->rss_hdr.opcode == CPL_TRACE_PKT))
				__skb_pull(skb, 2);
next_fl:
			if (++fl->cidx == fl->size)
				fl->cidx = 0;
		} else
			q->pure_rsps++;

		if (flags & RSPD_CTRL_MASK) {
			sleeping |= flags & RSPD_GTS_MASK;
			handle_rsp_cntrl_info(qs, flags);
		}

		r++;
		if (unlikely(++q->cidx == q->size)) {
			q->cidx = 0;
			q->gen ^= 1;
			r = q->desc;
		}
		prefetch(r);

		if (++q->credits >= (q->size / 4)) {
			refill_rspq(adap, q, q->credits);
			q->credits = 0;
		}

		packet_complete = flags &
				  (F_RSPD_EOP | F_RSPD_IMM_DATA_VALID |
				   F_RSPD_ASYNC_NOTIF);

		if (skb != NULL && packet_complete) {
			if (eth)
				rx_eth(adap, q, skb, ethpad, lro);
			else {
				q->offload_pkts++;
				/* Preserve the RSS info in csum & priority */
				skb->csum = rss_hi;
				skb->priority = rss_lo;
				ngathered = rx_offload(&adap->tdev, q, skb,
						       offload_skbs,
						       ngathered);
			}

			if (flags & F_RSPD_EOP)
				clear_rspq_bufstate(q);
		}
		--budget_left;
	}

	deliver_partial_bundle(&adap->tdev, q, offload_skbs, ngathered);

	if (sleeping)
		check_ring_db(adap, qs, sleeping);

	smp_mb();		/* commit Tx queue .processed updates */
	if (unlikely(qs->txq_stopped != 0))
		restart_tx(qs);

	budget -= budget_left;
	return budget;
}

static inline int is_pure_response(const struct rsp_desc *r)
{
	__be32 n = r->flags & htonl(F_RSPD_ASYNC_NOTIF | F_RSPD_IMM_DATA_VALID);

	return (n | r->len_cq) == 0;
}

/**
 *	napi_rx_handler - the NAPI handler for Rx processing
 *	@napi: the napi instance
 *	@budget: how many packets we can process in this round
 *
 *	Handler for new data events when using NAPI.
 */
static int napi_rx_handler(struct napi_struct *napi, int budget)
{
	struct sge_qset *qs = container_of(napi, struct sge_qset, napi);
	struct adapter *adap = qs->adap;
	int work_done = process_responses(adap, qs, budget);

	if (likely(work_done < budget)) {
		napi_complete(napi);

		/*
		 * Because we don't atomically flush the following
		 * write it is possible that in very rare cases it can
		 * reach the device in a way that races with a new
		 * response being written plus an error interrupt
		 * causing the NAPI interrupt handler below to return
		 * unhandled status to the OS.  To protect against
		 * this would require flushing the write and doing
		 * both the write and the flush with interrupts off.
		 * Way too expensive and unjustifiable given the
		 * rarity of the race.
		 *
		 * The race cannot happen at all with MSI-X.
		 */
		t3_write_reg(adap, A_SG_GTS, V_RSPQ(qs->rspq.cntxt_id) |
			     V_NEWTIMER(qs->rspq.next_holdoff) |
			     V_NEWINDEX(qs->rspq.cidx));
	}
	return work_done;
}

/*
 * Returns true if the device is already scheduled for polling.
 */
static inline int napi_is_scheduled(struct napi_struct *napi)
{
	return test_bit(NAPI_STATE_SCHED, &napi->state);
}

/**
 *	process_pure_responses - process pure responses from a response queue
 *	@adap: the adapter
 *	@qs: the queue set owning the response queue
 *	@r: the first pure response to process
 *
 *	A simpler version of process_responses() that handles only pure (i.e.,
 *	non data-carrying) responses.  Such respones are too light-weight to
 *	justify calling a softirq under NAPI, so we handle them specially in
 *	the interrupt handler.  The function is called with a pointer to a
 *	response, which the caller must ensure is a valid pure response.
 *
 *	Returns 1 if it encounters a valid data-carrying response, 0 otherwise.
 */
static int process_pure_responses(struct adapter *adap, struct sge_qset *qs,
				  struct rsp_desc *r)
{
	struct sge_rspq *q = &qs->rspq;
	unsigned int sleeping = 0;

	do {
		u32 flags = ntohl(r->flags);

		r++;
		if (unlikely(++q->cidx == q->size)) {
			q->cidx = 0;
			q->gen ^= 1;
			r = q->desc;
		}
		prefetch(r);

		if (flags & RSPD_CTRL_MASK) {
			sleeping |= flags & RSPD_GTS_MASK;
			handle_rsp_cntrl_info(qs, flags);
		}

		q->pure_rsps++;
		if (++q->credits >= (q->size / 4)) {
			refill_rspq(adap, q, q->credits);
			q->credits = 0;
		}
		if (!is_new_response(r, q))
			break;
		rmb();
	} while (is_pure_response(r));

	if (sleeping)
		check_ring_db(adap, qs, sleeping);

	smp_mb();		/* commit Tx queue .processed updates */
	if (unlikely(qs->txq_stopped != 0))
		restart_tx(qs);

	return is_new_response(r, q);
}

/**
 *	handle_responses - decide what to do with new responses in NAPI mode
 *	@adap: the adapter
 *	@q: the response queue
 *
 *	This is used by the NAPI interrupt handlers to decide what to do with
 *	new SGE responses.  If there are no new responses it returns -1.  If
 *	there are new responses and they are pure (i.e., non-data carrying)
 *	it handles them straight in hard interrupt context as they are very
 *	cheap and don't deliver any packets.  Finally, if there are any data
 *	signaling responses it schedules the NAPI handler.  Returns 1 if it
 *	schedules NAPI, 0 if all new responses were pure.
 *
 *	The caller must ascertain NAPI is not already running.
 */
static inline int handle_responses(struct adapter *adap, struct sge_rspq *q)
{
	struct sge_qset *qs = rspq_to_qset(q);
	struct rsp_desc *r = &q->desc[q->cidx];

	if (!is_new_response(r, q))
		return -1;
	rmb();
	if (is_pure_response(r) && process_pure_responses(adap, qs, r) == 0) {
		t3_write_reg(adap, A_SG_GTS, V_RSPQ(q->cntxt_id) |
			     V_NEWTIMER(q->holdoff_tmr) | V_NEWINDEX(q->cidx));
		return 0;
	}
	napi_schedule(&qs->napi);
	return 1;
}

/*
 * The MSI-X interrupt handler for an SGE response queue for the non-NAPI case
 * (i.e., response queue serviced in hard interrupt).
 */
static irqreturn_t t3_sge_intr_msix(int irq, void *cookie)
{
	struct sge_qset *qs = cookie;
	struct adapter *adap = qs->adap;
	struct sge_rspq *q = &qs->rspq;

	spin_lock(&q->lock);
	if (process_responses(adap, qs, -1) == 0)
		q->unhandled_irqs++;
	t3_write_reg(adap, A_SG_GTS, V_RSPQ(q->cntxt_id) |
		     V_NEWTIMER(q->next_holdoff) | V_NEWINDEX(q->cidx));
	spin_unlock(&q->lock);
	return IRQ_HANDLED;
}

/*
 * The MSI-X interrupt handler for an SGE response queue for the NAPI case
 * (i.e., response queue serviced by NAPI polling).
 */
static irqreturn_t t3_sge_intr_msix_napi(int irq, void *cookie)
{
	struct sge_qset *qs = cookie;
	struct sge_rspq *q = &qs->rspq;

	spin_lock(&q->lock);

	if (handle_responses(qs->adap, q) < 0)
		q->unhandled_irqs++;
	spin_unlock(&q->lock);
	return IRQ_HANDLED;
}

/*
 * The non-NAPI MSI interrupt handler.  This needs to handle data events from
 * SGE response queues as well as error and other async events as they all use
 * the same MSI vector.  We use one SGE response queue per port in this mode
 * and protect all response queues with queue 0's lock.
 */
static irqreturn_t t3_intr_msi(int irq, void *cookie)
{
	int new_packets = 0;
	struct adapter *adap = cookie;
	struct sge_rspq *q = &adap->sge.qs[0].rspq;

	spin_lock(&q->lock);

	if (process_responses(adap, &adap->sge.qs[0], -1)) {
		t3_write_reg(adap, A_SG_GTS, V_RSPQ(q->cntxt_id) |
			     V_NEWTIMER(q->next_holdoff) | V_NEWINDEX(q->cidx));
		new_packets = 1;
	}

	if (adap->params.nports == 2 &&
	    process_responses(adap, &adap->sge.qs[1], -1)) {
		struct sge_rspq *q1 = &adap->sge.qs[1].rspq;

		t3_write_reg(adap, A_SG_GTS, V_RSPQ(q1->cntxt_id) |
			     V_NEWTIMER(q1->next_holdoff) |
			     V_NEWINDEX(q1->cidx));
		new_packets = 1;
	}

	if (!new_packets && t3_slow_intr_handler(adap) == 0)
		q->unhandled_irqs++;

	spin_unlock(&q->lock);
	return IRQ_HANDLED;
}

static int rspq_check_napi(struct sge_qset *qs)
{
	struct sge_rspq *q = &qs->rspq;

	if (!napi_is_scheduled(&qs->napi) &&
	    is_new_response(&q->desc[q->cidx], q)) {
		napi_schedule(&qs->napi);
		return 1;
	}
	return 0;
}

/*
 * The MSI interrupt handler for the NAPI case (i.e., response queues serviced
 * by NAPI polling).  Handles data events from SGE response queues as well as
 * error and other async events as they all use the same MSI vector.  We use
 * one SGE response queue per port in this mode and protect all response
 * queues with queue 0's lock.
 */
static irqreturn_t t3_intr_msi_napi(int irq, void *cookie)
{
	int new_packets;
	struct adapter *adap = cookie;
	struct sge_rspq *q = &adap->sge.qs[0].rspq;

	spin_lock(&q->lock);

	new_packets = rspq_check_napi(&adap->sge.qs[0]);
	if (adap->params.nports == 2)
		new_packets += rspq_check_napi(&adap->sge.qs[1]);
	if (!new_packets && t3_slow_intr_handler(adap) == 0)
		q->unhandled_irqs++;

	spin_unlock(&q->lock);
	return IRQ_HANDLED;
}

/*
 * A helper function that processes responses and issues GTS.
 */
static inline int process_responses_gts(struct adapter *adap,
					struct sge_rspq *rq)
{
	int work;

	work = process_responses(adap, rspq_to_qset(rq), -1);
	t3_write_reg(adap, A_SG_GTS, V_RSPQ(rq->cntxt_id) |
		     V_NEWTIMER(rq->next_holdoff) | V_NEWINDEX(rq->cidx));
	return work;
}

/*
 * The legacy INTx interrupt handler.  This needs to handle data events from
 * SGE response queues as well as error and other async events as they all use
 * the same interrupt pin.  We use one SGE response queue per port in this mode
 * and protect all response queues with queue 0's lock.
 */
static irqreturn_t t3_intr(int irq, void *cookie)
{
	int work_done, w0, w1;
	struct adapter *adap = cookie;
	struct sge_rspq *q0 = &adap->sge.qs[0].rspq;
	struct sge_rspq *q1 = &adap->sge.qs[1].rspq;

	spin_lock(&q0->lock);

	w0 = is_new_response(&q0->desc[q0->cidx], q0);
	w1 = adap->params.nports == 2 &&
	    is_new_response(&q1->desc[q1->cidx], q1);

	if (likely(w0 | w1)) {
		t3_write_reg(adap, A_PL_CLI, 0);
		t3_read_reg(adap, A_PL_CLI);	/* flush */

		if (likely(w0))
			process_responses_gts(adap, q0);

		if (w1)
			process_responses_gts(adap, q1);

		work_done = w0 | w1;
	} else
		work_done = t3_slow_intr_handler(adap);

	spin_unlock(&q0->lock);
	return IRQ_RETVAL(work_done != 0);
}

/*
 * Interrupt handler for legacy INTx interrupts for T3B-based cards.
 * Handles data events from SGE response queues as well as error and other
 * async events as they all use the same interrupt pin.  We use one SGE
 * response queue per port in this mode and protect all response queues with
 * queue 0's lock.
 */
static irqreturn_t t3b_intr(int irq, void *cookie)
{
	u32 map;
	struct adapter *adap = cookie;
	struct sge_rspq *q0 = &adap->sge.qs[0].rspq;

	t3_write_reg(adap, A_PL_CLI, 0);
	map = t3_read_reg(adap, A_SG_DATA_INTR);

	if (unlikely(!map))	/* shared interrupt, most likely */
		return IRQ_NONE;

	spin_lock(&q0->lock);

	if (unlikely(map & F_ERRINTR))
		t3_slow_intr_handler(adap);

	if (likely(map & 1))
		process_responses_gts(adap, q0);

	if (map & 2)
		process_responses_gts(adap, &adap->sge.qs[1].rspq);

	spin_unlock(&q0->lock);
	return IRQ_HANDLED;
}

/*
 * NAPI interrupt handler for legacy INTx interrupts for T3B-based cards.
 * Handles data events from SGE response queues as well as error and other
 * async events as they all use the same interrupt pin.  We use one SGE
 * response queue per port in this mode and protect all response queues with
 * queue 0's lock.
 */
static irqreturn_t t3b_intr_napi(int irq, void *cookie)
{
	u32 map;
	struct adapter *adap = cookie;
	struct sge_qset *qs0 = &adap->sge.qs[0];
	struct sge_rspq *q0 = &qs0->rspq;

	t3_write_reg(adap, A_PL_CLI, 0);
	map = t3_read_reg(adap, A_SG_DATA_INTR);

	if (unlikely(!map))	/* shared interrupt, most likely */
		return IRQ_NONE;

	spin_lock(&q0->lock);

	if (unlikely(map & F_ERRINTR))
		t3_slow_intr_handler(adap);

	if (likely(map & 1))
		napi_schedule(&qs0->napi);

	if (map & 2)
		napi_schedule(&adap->sge.qs[1].napi);

	spin_unlock(&q0->lock);
	return IRQ_HANDLED;
}

/**
 *	t3_intr_handler - select the top-level interrupt handler
 *	@adap: the adapter
 *	@polling: whether using NAPI to service response queues
 *
 *	Selects the top-level interrupt handler based on the type of interrupts
 *	(MSI-X, MSI, or legacy) and whether NAPI will be used to service the
 *	response queues.
 */
irq_handler_t t3_intr_handler(struct adapter *adap, int polling)
{
	if (adap->flags & USING_MSIX)
		return polling ? t3_sge_intr_msix_napi : t3_sge_intr_msix;
	if (adap->flags & USING_MSI)
		return polling ? t3_intr_msi_napi : t3_intr_msi;
	if (adap->params.rev > 0)
		return polling ? t3b_intr_napi : t3b_intr;
	return t3_intr;
}

#define SGE_PARERR (F_CPPARITYERROR | F_OCPARITYERROR | F_RCPARITYERROR | \
		    F_IRPARITYERROR | V_ITPARITYERROR(M_ITPARITYERROR) | \
		    V_FLPARITYERROR(M_FLPARITYERROR) | F_LODRBPARITYERROR | \
		    F_HIDRBPARITYERROR | F_LORCQPARITYERROR | \
		    F_HIRCQPARITYERROR)
#define SGE_FRAMINGERR (F_UC_REQ_FRAMINGERROR | F_R_REQ_FRAMINGERROR)
#define SGE_FATALERR (SGE_PARERR | SGE_FRAMINGERR | F_RSPQCREDITOVERFOW | \
		      F_RSPQDISABLED)

/**
 *	t3_sge_err_intr_handler - SGE async event interrupt handler
 *	@adapter: the adapter
 *
 *	Interrupt handler for SGE asynchronous (non-data) events.
 */
void t3_sge_err_intr_handler(struct adapter *adapter)
{
	unsigned int v, status = t3_read_reg(adapter, A_SG_INT_CAUSE) &
				 ~F_FLEMPTY;

	if (status & SGE_PARERR)
		CH_ALERT(adapter, "SGE parity error (0x%x)\n",
			 status & SGE_PARERR);
	if (status & SGE_FRAMINGERR)
		CH_ALERT(adapter, "SGE framing error (0x%x)\n",
			 status & SGE_FRAMINGERR);

	if (status & F_RSPQCREDITOVERFOW)
		CH_ALERT(adapter, "SGE response queue credit overflow\n");

	if (status & F_RSPQDISABLED) {
		v = t3_read_reg(adapter, A_SG_RSPQ_FL_STATUS);

		CH_ALERT(adapter,
			 "packet delivered to disabled response queue "
			 "(0x%x)\n", (v >> S_RSPQ0DISABLED) & 0xff);
	}

	if (status & (F_HIPIODRBDROPERR | F_LOPIODRBDROPERR))
		queue_work(cxgb3_wq, &adapter->db_drop_task);

	if (status & (F_HIPRIORITYDBFULL | F_LOPRIORITYDBFULL))
		queue_work(cxgb3_wq, &adapter->db_full_task);

	if (status & (F_HIPRIORITYDBEMPTY | F_LOPRIORITYDBEMPTY))
		queue_work(cxgb3_wq, &adapter->db_empty_task);

	t3_write_reg(adapter, A_SG_INT_CAUSE, status);
	if (status &  SGE_FATALERR)
		t3_fatal_err(adapter);
}

/**
 *	sge_timer_tx - perform periodic maintenance of an SGE qset
 *	@data: the SGE queue set to maintain
 *
 *	Runs periodically from a timer to perform maintenance of an SGE queue
 *	set.  It performs two tasks:
 *
 *	Cleans up any completed Tx descriptors that may still be pending.
 *	Normal descriptor cleanup happens when new packets are added to a Tx
 *	queue so this timer is relatively infrequent and does any cleanup only
 *	if the Tx queue has not seen any new packets in a while.  We make a
 *	best effort attempt to reclaim descriptors, in that we don't wait
 *	around if we cannot get a queue's lock (which most likely is because
 *	someone else is queueing new packets and so will also handle the clean
 *	up).  Since control queues use immediate data exclusively we don't
 *	bother cleaning them up here.
 *
 */
static void sge_timer_tx(unsigned long data)
{
	struct sge_qset *qs = (struct sge_qset *)data;
	struct port_info *pi = netdev_priv(qs->netdev);
	struct adapter *adap = pi->adapter;
	unsigned int tbd[SGE_TXQ_PER_SET] = {0, 0};
	unsigned long next_period;

	if (__netif_tx_trylock(qs->tx_q)) {
                tbd[TXQ_ETH] = reclaim_completed_tx(adap, &qs->txq[TXQ_ETH],
                                                     TX_RECLAIM_TIMER_CHUNK);
		__netif_tx_unlock(qs->tx_q);
	}

	if (spin_trylock(&qs->txq[TXQ_OFLD].lock)) {
		tbd[TXQ_OFLD] = reclaim_completed_tx(adap, &qs->txq[TXQ_OFLD],
						     TX_RECLAIM_TIMER_CHUNK);
		spin_unlock(&qs->txq[TXQ_OFLD].lock);
	}

	next_period = TX_RECLAIM_PERIOD >>
                      (max(tbd[TXQ_ETH], tbd[TXQ_OFLD]) /
                      TX_RECLAIM_TIMER_CHUNK);
	mod_timer(&qs->tx_reclaim_timer, jiffies + next_period);
}

/**
 *	sge_timer_rx - perform periodic maintenance of an SGE qset
 *	@data: the SGE queue set to maintain
 *
 *	a) Replenishes Rx queues that have run out due to memory shortage.
 *	Normally new Rx buffers are added when existing ones are consumed but
 *	when out of memory a queue can become empty.  We try to add only a few
 *	buffers here, the queue will be replenished fully as these new buffers
 *	are used up if memory shortage has subsided.
 *
 *	b) Return coalesced response queue credits in case a response queue is
 *	starved.
 *
 */
static void sge_timer_rx(unsigned long data)
{
	spinlock_t *lock;
	struct sge_qset *qs = (struct sge_qset *)data;
	struct port_info *pi = netdev_priv(qs->netdev);
	struct adapter *adap = pi->adapter;
	u32 status;

	lock = adap->params.rev > 0 ?
	       &qs->rspq.lock : &adap->sge.qs[0].rspq.lock;

	if (!spin_trylock_irq(lock))
		goto out;

	if (napi_is_scheduled(&qs->napi))
		goto unlock;

	if (adap->params.rev < 4) {
		status = t3_read_reg(adap, A_SG_RSPQ_FL_STATUS);

		if (status & (1 << qs->rspq.cntxt_id)) {
			qs->rspq.starved++;
			if (qs->rspq.credits) {
				qs->rspq.credits--;
				refill_rspq(adap, &qs->rspq, 1);
				qs->rspq.restarted++;
				t3_write_reg(adap, A_SG_RSPQ_FL_STATUS,
					     1 << qs->rspq.cntxt_id);
			}
		}
	}

	if (qs->fl[0].credits < qs->fl[0].size)
		__refill_fl(adap, &qs->fl[0]);
	if (qs->fl[1].credits < qs->fl[1].size)
		__refill_fl(adap, &qs->fl[1]);

unlock:
	spin_unlock_irq(lock);
out:
	mod_timer(&qs->rx_reclaim_timer, jiffies + RX_RECLAIM_PERIOD);
}

/**
 *	t3_update_qset_coalesce - update coalescing settings for a queue set
 *	@qs: the SGE queue set
 *	@p: new queue set parameters
 *
 *	Update the coalescing settings for an SGE queue set.  Nothing is done
 *	if the queue set is not initialized yet.
 */
void t3_update_qset_coalesce(struct sge_qset *qs, const struct qset_params *p)
{
	qs->rspq.holdoff_tmr = max(p->coalesce_usecs * 10, 1U);/* can't be 0 */
	qs->rspq.polling = p->polling;
	qs->napi.poll = p->polling ? napi_rx_handler : ofld_poll;
}

/**
 *	t3_sge_alloc_qset - initialize an SGE queue set
 *	@adapter: the adapter
 *	@id: the queue set id
 *	@nports: how many Ethernet ports will be using this queue set
 *	@irq_vec_idx: the IRQ vector index for response queue interrupts
 *	@p: configuration parameters for this queue set
 *	@ntxq: number of Tx queues for the queue set
 *	@netdev: net device associated with this queue set
 *	@netdevq: net device TX queue associated with this queue set
 *
 *	Allocate resources and initialize an SGE queue set.  A queue set
 *	comprises a response queue, two Rx free-buffer queues, and up to 3
 *	Tx queues.  The Tx queues are assigned roles in the order Ethernet
 *	queue, offload queue, and control queue.
 */
int t3_sge_alloc_qset(struct adapter *adapter, unsigned int id, int nports,
		      int irq_vec_idx, const struct qset_params *p,
		      int ntxq, struct net_device *dev,
		      struct netdev_queue *netdevq)
{
	int i, avail, ret = -ENOMEM;
	struct sge_qset *q = &adapter->sge.qs[id];

	init_qset_cntxt(q, id);
	setup_timer(&q->tx_reclaim_timer, sge_timer_tx, (unsigned long)q);
	setup_timer(&q->rx_reclaim_timer, sge_timer_rx, (unsigned long)q);

	q->fl[0].desc = alloc_ring(adapter->pdev, p->fl_size,
				   sizeof(struct rx_desc),
				   sizeof(struct rx_sw_desc),
				   &q->fl[0].phys_addr, &q->fl[0].sdesc);
	if (!q->fl[0].desc)
		goto err;

	q->fl[1].desc = alloc_ring(adapter->pdev, p->jumbo_size,
				   sizeof(struct rx_desc),
				   sizeof(struct rx_sw_desc),
				   &q->fl[1].phys_addr, &q->fl[1].sdesc);
	if (!q->fl[1].desc)
		goto err;

	q->rspq.desc = alloc_ring(adapter->pdev, p->rspq_size,
				  sizeof(struct rsp_desc), 0,
				  &q->rspq.phys_addr, NULL);
	if (!q->rspq.desc)
		goto err;

	for (i = 0; i < ntxq; ++i) {
		/*
		 * The control queue always uses immediate data so does not
		 * need to keep track of any sk_buffs.
		 */
		size_t sz = i == TXQ_CTRL ? 0 : sizeof(struct tx_sw_desc);

		q->txq[i].desc = alloc_ring(adapter->pdev, p->txq_size[i],
					    sizeof(struct tx_desc), sz,
					    &q->txq[i].phys_addr,
					    &q->txq[i].sdesc);
		if (!q->txq[i].desc)
			goto err;

		q->txq[i].gen = 1;
		q->txq[i].size = p->txq_size[i];
		spin_lock_init(&q->txq[i].lock);
		skb_queue_head_init(&q->txq[i].sendq);
	}

	tasklet_init(&q->txq[TXQ_OFLD].qresume_tsk, restart_offloadq,
		     (unsigned long)q);
	tasklet_init(&q->txq[TXQ_CTRL].qresume_tsk, restart_ctrlq,
		     (unsigned long)q);

	q->fl[0].gen = q->fl[1].gen = 1;
	q->fl[0].size = p->fl_size;
	q->fl[1].size = p->jumbo_size;

	q->rspq.gen = 1;
	q->rspq.size = p->rspq_size;
	spin_lock_init(&q->rspq.lock);
	skb_queue_head_init(&q->rspq.rx_queue);

	q->txq[TXQ_ETH].stop_thres = nports *
	    flits_to_desc(sgl_len(MAX_SKB_FRAGS + 1) + 3);

#if FL0_PG_CHUNK_SIZE > 0
	q->fl[0].buf_size = FL0_PG_CHUNK_SIZE;
#else
	q->fl[0].buf_size = SGE_RX_SM_BUF_SIZE + sizeof(struct cpl_rx_data);
#endif
#if FL1_PG_CHUNK_SIZE > 0
	q->fl[1].buf_size = FL1_PG_CHUNK_SIZE;
#else
	q->fl[1].buf_size = is_offload(adapter) ?
		(16 * 1024) - SKB_DATA_ALIGN(sizeof(struct skb_shared_info)) :
		MAX_FRAME_SIZE + 2 + sizeof(struct cpl_rx_pkt);
#endif

	q->fl[0].use_pages = FL0_PG_CHUNK_SIZE > 0;
	q->fl[1].use_pages = FL1_PG_CHUNK_SIZE > 0;
	q->fl[0].order = FL0_PG_ORDER;
	q->fl[1].order = FL1_PG_ORDER;
	q->fl[0].alloc_size = FL0_PG_ALLOC_SIZE;
	q->fl[1].alloc_size = FL1_PG_ALLOC_SIZE;

	spin_lock_irq(&adapter->sge.reg_lock);

	/* FL threshold comparison uses < */
	ret = t3_sge_init_rspcntxt(adapter, q->rspq.cntxt_id, irq_vec_idx,
				   q->rspq.phys_addr, q->rspq.size,
				   q->fl[0].buf_size - SGE_PG_RSVD, 1, 0);
	if (ret)
		goto err_unlock;

	for (i = 0; i < SGE_RXQ_PER_SET; ++i) {
		ret = t3_sge_init_flcntxt(adapter, q->fl[i].cntxt_id, 0,
					  q->fl[i].phys_addr, q->fl[i].size,
					  q->fl[i].buf_size - SGE_PG_RSVD,
					  p->cong_thres, 1, 0);
		if (ret)
			goto err_unlock;
	}

	ret = t3_sge_init_ecntxt(adapter, q->txq[TXQ_ETH].cntxt_id, USE_GTS,
				 SGE_CNTXT_ETH, id, q->txq[TXQ_ETH].phys_addr,
				 q->txq[TXQ_ETH].size, q->txq[TXQ_ETH].token,
				 1, 0);
	if (ret)
		goto err_unlock;

	if (ntxq > 1) {
		ret = t3_sge_init_ecntxt(adapter, q->txq[TXQ_OFLD].cntxt_id,
					 USE_GTS, SGE_CNTXT_OFLD, id,
					 q->txq[TXQ_OFLD].phys_addr,
					 q->txq[TXQ_OFLD].size, 0, 1, 0);
		if (ret)
			goto err_unlock;
	}

	if (ntxq > 2) {
		ret = t3_sge_init_ecntxt(adapter, q->txq[TXQ_CTRL].cntxt_id, 0,
					 SGE_CNTXT_CTRL, id,
					 q->txq[TXQ_CTRL].phys_addr,
					 q->txq[TXQ_CTRL].size,
					 q->txq[TXQ_CTRL].token, 1, 0);
		if (ret)
			goto err_unlock;
	}

	spin_unlock_irq(&adapter->sge.reg_lock);

	q->adap = adapter;
	q->netdev = dev;
	q->tx_q = netdevq;
	t3_update_qset_coalesce(q, p);

	avail = refill_fl(adapter, &q->fl[0], q->fl[0].size,
			  GFP_KERNEL | __GFP_COMP);
	if (!avail) {
		CH_ALERT(adapter, "free list queue 0 initialization failed\n");
		goto err;
	}
	if (avail < q->fl[0].size)
		CH_WARN(adapter, "free list queue 0 enabled with %d credits\n",
			avail);

	avail = refill_fl(adapter, &q->fl[1], q->fl[1].size,
			  GFP_KERNEL | __GFP_COMP);
	if (avail < q->fl[1].size)
		CH_WARN(adapter, "free list queue 1 enabled with %d credits\n",
			avail);
	refill_rspq(adapter, &q->rspq, q->rspq.size - 1);

	t3_write_reg(adapter, A_SG_GTS, V_RSPQ(q->rspq.cntxt_id) |
		     V_NEWTIMER(q->rspq.holdoff_tmr));

	return 0;

err_unlock:
	spin_unlock_irq(&adapter->sge.reg_lock);
err:
	t3_free_qset(adapter, q);
	return ret;
}

/**
 *      t3_start_sge_timers - start SGE timer call backs
 *      @adap: the adapter
 *
 *      Starts each SGE queue set's timer call back
 */
void t3_start_sge_timers(struct adapter *adap)
{
	int i;

	for (i = 0; i < SGE_QSETS; ++i) {
		struct sge_qset *q = &adap->sge.qs[i];

	if (q->tx_reclaim_timer.function)
		mod_timer(&q->tx_reclaim_timer, jiffies + TX_RECLAIM_PERIOD);

	if (q->rx_reclaim_timer.function)
		mod_timer(&q->rx_reclaim_timer, jiffies + RX_RECLAIM_PERIOD);
	}
}

/**
 *	t3_stop_sge_timers - stop SGE timer call backs
 *	@adap: the adapter
 *
 *	Stops each SGE queue set's timer call back
 */
void t3_stop_sge_timers(struct adapter *adap)
{
	int i;

	for (i = 0; i < SGE_QSETS; ++i) {
		struct sge_qset *q = &adap->sge.qs[i];

		if (q->tx_reclaim_timer.function)
			del_timer_sync(&q->tx_reclaim_timer);
		if (q->rx_reclaim_timer.function)
			del_timer_sync(&q->rx_reclaim_timer);
	}
}

/**
 *	t3_free_sge_resources - free SGE resources
 *	@adap: the adapter
 *
 *	Frees resources used by the SGE queue sets.
 */
void t3_free_sge_resources(struct adapter *adap)
{
	int i;

	for (i = 0; i < SGE_QSETS; ++i)
		t3_free_qset(adap, &adap->sge.qs[i]);
}

/**
 *	t3_sge_start - enable SGE
 *	@adap: the adapter
 *
 *	Enables the SGE for DMAs.  This is the last step in starting packet
 *	transfers.
 */
void t3_sge_start(struct adapter *adap)
{
	t3_set_reg_field(adap, A_SG_CONTROL, F_GLOBALENABLE, F_GLOBALENABLE);
}

/**
 *	t3_sge_stop - disable SGE operation
 *	@adap: the adapter
 *
 *	Disables the DMA engine.  This can be called in emeregencies (e.g.,
 *	from error interrupts) or from normal process context.  In the latter
 *	case it also disables any pending queue restart tasklets.  Note that
 *	if it is called in interrupt context it cannot disable the restart
 *	tasklets as it cannot wait, however the tasklets will have no effect
 *	since the doorbells are disabled and the driver will call this again
 *	later from process context, at which time the tasklets will be stopped
 *	if they are still running.
 */
void t3_sge_stop(struct adapter *adap)
{
	t3_set_reg_field(adap, A_SG_CONTROL, F_GLOBALENABLE, 0);
	if (!in_interrupt()) {
		int i;

		for (i = 0; i < SGE_QSETS; ++i) {
			struct sge_qset *qs = &adap->sge.qs[i];

			tasklet_kill(&qs->txq[TXQ_OFLD].qresume_tsk);
			tasklet_kill(&qs->txq[TXQ_CTRL].qresume_tsk);
		}
	}
}

/**
 *	t3_sge_init - initialize SGE
 *	@adap: the adapter
 *	@p: the SGE parameters
 *
 *	Performs SGE initialization needed every time after a chip reset.
 *	We do not initialize any of the queue sets here, instead the driver
 *	top-level must request those individually.  We also do not enable DMA
 *	here, that should be done after the queues have been set up.
 */
void t3_sge_init(struct adapter *adap, struct sge_params *p)
{
	unsigned int ctrl, ups = ffs(pci_resource_len(adap->pdev, 2) >> 12);

	ctrl = F_DROPPKT | V_PKTSHIFT(2) | F_FLMODE | F_AVOIDCQOVFL |
	    F_CQCRDTCTRL | F_CONGMODE | F_TNLFLMODE | F_FATLPERREN |
	    V_HOSTPAGESIZE(PAGE_SHIFT - 11) | F_BIGENDIANINGRESS |
	    V_USERSPACESIZE(ups ? ups - 1 : 0) | F_ISCSICOALESCING;
#if SGE_NUM_GENBITS == 1
	ctrl |= F_EGRGENCTRL;
#endif
	if (adap->params.rev > 0) {
		if (!(adap->flags & (USING_MSIX | USING_MSI)))
			ctrl |= F_ONEINTMULTQ | F_OPTONEINTMULTQ;
	}
	t3_write_reg(adap, A_SG_CONTROL, ctrl);
	t3_write_reg(adap, A_SG_EGR_RCQ_DRB_THRSH, V_HIRCQDRBTHRSH(512) |
		     V_LORCQDRBTHRSH(512));
	t3_write_reg(adap, A_SG_TIMER_TICK, core_ticks_per_usec(adap) / 10);
	t3_write_reg(adap, A_SG_CMDQ_CREDIT_TH, V_THRESHOLD(32) |
		     V_TIMEOUT(200 * core_ticks_per_usec(adap)));
	t3_write_reg(adap, A_SG_HI_DRB_HI_THRSH,
		     adap->params.rev < T3_REV_C ? 1000 : 500);
	t3_write_reg(adap, A_SG_HI_DRB_LO_THRSH, 256);
	t3_write_reg(adap, A_SG_LO_DRB_HI_THRSH, 1000);
	t3_write_reg(adap, A_SG_LO_DRB_LO_THRSH, 256);
	t3_write_reg(adap, A_SG_OCO_BASE, V_BASE1(0xfff));
	t3_write_reg(adap, A_SG_DRB_PRI_THRESH, 63 * 1024);
}

/**
 *	t3_sge_prep - one-time SGE initialization
 *	@adap: the associated adapter
 *	@p: SGE parameters
 *
 *	Performs one-time initialization of SGE SW state.  Includes determining
 *	defaults for the assorted SGE parameters, which admins can change until
 *	they are used to initialize the SGE.
 */
void t3_sge_prep(struct adapter *adap, struct sge_params *p)
{
	int i;

	p->max_pkt_size = (16 * 1024) - sizeof(struct cpl_rx_data) -
	    SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	for (i = 0; i < SGE_QSETS; ++i) {
		struct qset_params *q = p->qset + i;

		q->polling = adap->params.rev > 0;
		q->coalesce_usecs = 5;
		q->rspq_size = 1024;
		q->fl_size = 1024;
 		q->jumbo_size = 512;
		q->txq_size[TXQ_ETH] = 1024;
		q->txq_size[TXQ_OFLD] = 1024;
		q->txq_size[TXQ_CTRL] = 256;
		q->cong_thres = 0;
	}

	spin_lock_init(&adap->sge.reg_lock);
}
