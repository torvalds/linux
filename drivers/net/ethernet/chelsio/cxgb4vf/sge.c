/*
 * This file is part of the Chelsio T4 PCI-E SR-IOV Virtual Function Ethernet
 * driver for Linux.
 *
 * Copyright (c) 2009-2010 Chelsio Communications, Inc. All rights reserved.
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
#include <net/ipv6.h>
#include <net/tcp.h>
#include <linux/dma-mapping.h>
#include <linux/prefetch.h>

#include "t4vf_common.h"
#include "t4vf_defs.h"

#include "../cxgb4/t4_regs.h"
#include "../cxgb4/t4fw_api.h"
#include "../cxgb4/t4_msg.h"

/*
 * Decoded Adapter Parameters.
 */
static u32 FL_PG_ORDER;		/* large page allocation size */
static u32 STAT_LEN;		/* length of status page at ring end */
static u32 PKTSHIFT;		/* padding between CPL and packet data */
static u32 FL_ALIGN;		/* response queue message alignment */

/*
 * Constants ...
 */
enum {
	/*
	 * Egress Queue sizes, producer and consumer indices are all in units
	 * of Egress Context Units bytes.  Note that as far as the hardware is
	 * concerned, the free list is an Egress Queue (the host produces free
	 * buffers which the hardware consumes) and free list entries are
	 * 64-bit PCI DMA addresses.
	 */
	EQ_UNIT = SGE_EQ_IDXSIZE,
	FL_PER_EQ_UNIT = EQ_UNIT / sizeof(__be64),
	TXD_PER_EQ_UNIT = EQ_UNIT / sizeof(__be64),

	/*
	 * Max number of TX descriptors we clean up at a time.  Should be
	 * modest as freeing skbs isn't cheap and it happens while holding
	 * locks.  We just need to free packets faster than they arrive, we
	 * eventually catch up and keep the amortized cost reasonable.
	 */
	MAX_TX_RECLAIM = 16,

	/*
	 * Max number of Rx buffers we replenish at a time.  Again keep this
	 * modest, allocating buffers isn't cheap either.
	 */
	MAX_RX_REFILL = 16,

	/*
	 * Period of the Rx queue check timer.  This timer is infrequent as it
	 * has something to do only when the system experiences severe memory
	 * shortage.
	 */
	RX_QCHECK_PERIOD = (HZ / 2),

	/*
	 * Period of the TX queue check timer and the maximum number of TX
	 * descriptors to be reclaimed by the TX timer.
	 */
	TX_QCHECK_PERIOD = (HZ / 2),
	MAX_TIMER_TX_RECLAIM = 100,

	/*
	 * An FL with <= FL_STARVE_THRES buffers is starving and a periodic
	 * timer will attempt to refill it.
	 */
	FL_STARVE_THRES = 4,

	/*
	 * Suspend an Ethernet TX queue with fewer available descriptors than
	 * this.  We always want to have room for a maximum sized packet:
	 * inline immediate data + MAX_SKB_FRAGS. This is the same as
	 * calc_tx_flits() for a TSO packet with nr_frags == MAX_SKB_FRAGS
	 * (see that function and its helpers for a description of the
	 * calculation).
	 */
	ETHTXQ_MAX_FRAGS = MAX_SKB_FRAGS + 1,
	ETHTXQ_MAX_SGL_LEN = ((3 * (ETHTXQ_MAX_FRAGS-1))/2 +
				   ((ETHTXQ_MAX_FRAGS-1) & 1) +
				   2),
	ETHTXQ_MAX_HDR = (sizeof(struct fw_eth_tx_pkt_vm_wr) +
			  sizeof(struct cpl_tx_pkt_lso_core) +
			  sizeof(struct cpl_tx_pkt_core)) / sizeof(__be64),
	ETHTXQ_MAX_FLITS = ETHTXQ_MAX_SGL_LEN + ETHTXQ_MAX_HDR,

	ETHTXQ_STOP_THRES = 1 + DIV_ROUND_UP(ETHTXQ_MAX_FLITS, TXD_PER_EQ_UNIT),

	/*
	 * Max TX descriptor space we allow for an Ethernet packet to be
	 * inlined into a WR.  This is limited by the maximum value which
	 * we can specify for immediate data in the firmware Ethernet TX
	 * Work Request.
	 */
	MAX_IMM_TX_PKT_LEN = FW_WR_IMMDLEN_MASK,

	/*
	 * Max size of a WR sent through a control TX queue.
	 */
	MAX_CTRL_WR_LEN = 256,

	/*
	 * Maximum amount of data which we'll ever need to inline into a
	 * TX ring: max(MAX_IMM_TX_PKT_LEN, MAX_CTRL_WR_LEN).
	 */
	MAX_IMM_TX_LEN = (MAX_IMM_TX_PKT_LEN > MAX_CTRL_WR_LEN
			  ? MAX_IMM_TX_PKT_LEN
			  : MAX_CTRL_WR_LEN),

	/*
	 * For incoming packets less than RX_COPY_THRES, we copy the data into
	 * an skb rather than referencing the data.  We allocate enough
	 * in-line room in skb's to accommodate pulling in RX_PULL_LEN bytes
	 * of the data (header).
	 */
	RX_COPY_THRES = 256,
	RX_PULL_LEN = 128,

	/*
	 * Main body length for sk_buffs used for RX Ethernet packets with
	 * fragments.  Should be >= RX_PULL_LEN but possibly bigger to give
	 * pskb_may_pull() some room.
	 */
	RX_SKB_LEN = 512,
};

/*
 * Software state per TX descriptor.
 */
struct tx_sw_desc {
	struct sk_buff *skb;		/* socket buffer of TX data source */
	struct ulptx_sgl *sgl;		/* scatter/gather list in TX Queue */
};

/*
 * Software state per RX Free List descriptor.  We keep track of the allocated
 * FL page, its size, and its PCI DMA address (if the page is mapped).  The FL
 * page size and its PCI DMA mapped state are stored in the low bits of the
 * PCI DMA address as per below.
 */
struct rx_sw_desc {
	struct page *page;		/* Free List page buffer */
	dma_addr_t dma_addr;		/* PCI DMA address (if mapped) */
					/*   and flags (see below) */
};

/*
 * The low bits of rx_sw_desc.dma_addr have special meaning.  Note that the
 * SGE also uses the low 4 bits to determine the size of the buffer.  It uses
 * those bits to index into the SGE_FL_BUFFER_SIZE[index] register array.
 * Since we only use SGE_FL_BUFFER_SIZE0 and SGE_FL_BUFFER_SIZE1, these low 4
 * bits can only contain a 0 or a 1 to indicate which size buffer we're giving
 * to the SGE.  Thus, our software state of "is the buffer mapped for DMA" is
 * maintained in an inverse sense so the hardware never sees that bit high.
 */
enum {
	RX_LARGE_BUF    = 1 << 0,	/* buffer is SGE_FL_BUFFER_SIZE[1] */
	RX_UNMAPPED_BUF = 1 << 1,	/* buffer is not mapped */
};

/**
 *	get_buf_addr - return DMA buffer address of software descriptor
 *	@sdesc: pointer to the software buffer descriptor
 *
 *	Return the DMA buffer address of a software descriptor (stripping out
 *	our low-order flag bits).
 */
static inline dma_addr_t get_buf_addr(const struct rx_sw_desc *sdesc)
{
	return sdesc->dma_addr & ~(dma_addr_t)(RX_LARGE_BUF | RX_UNMAPPED_BUF);
}

/**
 *	is_buf_mapped - is buffer mapped for DMA?
 *	@sdesc: pointer to the software buffer descriptor
 *
 *	Determine whether the buffer associated with a software descriptor in
 *	mapped for DMA or not.
 */
static inline bool is_buf_mapped(const struct rx_sw_desc *sdesc)
{
	return !(sdesc->dma_addr & RX_UNMAPPED_BUF);
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
 *	txq_avail - return the number of available slots in a TX queue
 *	@tq: the TX queue
 *
 *	Returns the number of available descriptors in a TX queue.
 */
static inline unsigned int txq_avail(const struct sge_txq *tq)
{
	return tq->size - 1 - tq->in_use;
}

/**
 *	fl_cap - return the capacity of a Free List
 *	@fl: the Free List
 *
 *	Returns the capacity of a Free List.  The capacity is less than the
 *	size because an Egress Queue Index Unit worth of descriptors needs to
 *	be left unpopulated, otherwise the Producer and Consumer indices PIDX
 *	and CIDX will match and the hardware will think the FL is empty.
 */
static inline unsigned int fl_cap(const struct sge_fl *fl)
{
	return fl->size - FL_PER_EQ_UNIT;
}

/**
 *	fl_starving - return whether a Free List is starving.
 *	@fl: the Free List
 *
 *	Tests specified Free List to see whether the number of buffers
 *	available to the hardware has falled below our "starvation"
 *	threshold.
 */
static inline bool fl_starving(const struct sge_fl *fl)
{
	return fl->avail - fl->pend_cred <= FL_STARVE_THRES;
}

/**
 *	map_skb -  map an skb for DMA to the device
 *	@dev: the egress net device
 *	@skb: the packet to map
 *	@addr: a pointer to the base of the DMA mapping array
 *
 *	Map an skb for DMA to the device and return an array of DMA addresses.
 */
static int map_skb(struct device *dev, const struct sk_buff *skb,
		   dma_addr_t *addr)
{
	const skb_frag_t *fp, *end;
	const struct skb_shared_info *si;

	*addr = dma_map_single(dev, skb->data, skb_headlen(skb), DMA_TO_DEVICE);
	if (dma_mapping_error(dev, *addr))
		goto out_err;

	si = skb_shinfo(skb);
	end = &si->frags[si->nr_frags];
	for (fp = si->frags; fp < end; fp++) {
		*++addr = skb_frag_dma_map(dev, fp, 0, skb_frag_size(fp),
					   DMA_TO_DEVICE);
		if (dma_mapping_error(dev, *addr))
			goto unwind;
	}
	return 0;

unwind:
	while (fp-- > si->frags)
		dma_unmap_page(dev, *--addr, skb_frag_size(fp), DMA_TO_DEVICE);
	dma_unmap_single(dev, addr[-1], skb_headlen(skb), DMA_TO_DEVICE);

out_err:
	return -ENOMEM;
}

static void unmap_sgl(struct device *dev, const struct sk_buff *skb,
		      const struct ulptx_sgl *sgl, const struct sge_txq *tq)
{
	const struct ulptx_sge_pair *p;
	unsigned int nfrags = skb_shinfo(skb)->nr_frags;

	if (likely(skb_headlen(skb)))
		dma_unmap_single(dev, be64_to_cpu(sgl->addr0),
				 be32_to_cpu(sgl->len0), DMA_TO_DEVICE);
	else {
		dma_unmap_page(dev, be64_to_cpu(sgl->addr0),
			       be32_to_cpu(sgl->len0), DMA_TO_DEVICE);
		nfrags--;
	}

	/*
	 * the complexity below is because of the possibility of a wrap-around
	 * in the middle of an SGL
	 */
	for (p = sgl->sge; nfrags >= 2; nfrags -= 2) {
		if (likely((u8 *)(p + 1) <= (u8 *)tq->stat)) {
unmap:
			dma_unmap_page(dev, be64_to_cpu(p->addr[0]),
				       be32_to_cpu(p->len[0]), DMA_TO_DEVICE);
			dma_unmap_page(dev, be64_to_cpu(p->addr[1]),
				       be32_to_cpu(p->len[1]), DMA_TO_DEVICE);
			p++;
		} else if ((u8 *)p == (u8 *)tq->stat) {
			p = (const struct ulptx_sge_pair *)tq->desc;
			goto unmap;
		} else if ((u8 *)p + 8 == (u8 *)tq->stat) {
			const __be64 *addr = (const __be64 *)tq->desc;

			dma_unmap_page(dev, be64_to_cpu(addr[0]),
				       be32_to_cpu(p->len[0]), DMA_TO_DEVICE);
			dma_unmap_page(dev, be64_to_cpu(addr[1]),
				       be32_to_cpu(p->len[1]), DMA_TO_DEVICE);
			p = (const struct ulptx_sge_pair *)&addr[2];
		} else {
			const __be64 *addr = (const __be64 *)tq->desc;

			dma_unmap_page(dev, be64_to_cpu(p->addr[0]),
				       be32_to_cpu(p->len[0]), DMA_TO_DEVICE);
			dma_unmap_page(dev, be64_to_cpu(addr[0]),
				       be32_to_cpu(p->len[1]), DMA_TO_DEVICE);
			p = (const struct ulptx_sge_pair *)&addr[1];
		}
	}
	if (nfrags) {
		__be64 addr;

		if ((u8 *)p == (u8 *)tq->stat)
			p = (const struct ulptx_sge_pair *)tq->desc;
		addr = ((u8 *)p + 16 <= (u8 *)tq->stat
			? p->addr[0]
			: *(const __be64 *)tq->desc);
		dma_unmap_page(dev, be64_to_cpu(addr), be32_to_cpu(p->len[0]),
			       DMA_TO_DEVICE);
	}
}

/**
 *	free_tx_desc - reclaims TX descriptors and their buffers
 *	@adapter: the adapter
 *	@tq: the TX queue to reclaim descriptors from
 *	@n: the number of descriptors to reclaim
 *	@unmap: whether the buffers should be unmapped for DMA
 *
 *	Reclaims TX descriptors from an SGE TX queue and frees the associated
 *	TX buffers.  Called with the TX queue lock held.
 */
static void free_tx_desc(struct adapter *adapter, struct sge_txq *tq,
			 unsigned int n, bool unmap)
{
	struct tx_sw_desc *sdesc;
	unsigned int cidx = tq->cidx;
	struct device *dev = adapter->pdev_dev;

	const int need_unmap = need_skb_unmap() && unmap;

	sdesc = &tq->sdesc[cidx];
	while (n--) {
		/*
		 * If we kept a reference to the original TX skb, we need to
		 * unmap it from PCI DMA space (if required) and free it.
		 */
		if (sdesc->skb) {
			if (need_unmap)
				unmap_sgl(dev, sdesc->skb, sdesc->sgl, tq);
			kfree_skb(sdesc->skb);
			sdesc->skb = NULL;
		}

		sdesc++;
		if (++cidx == tq->size) {
			cidx = 0;
			sdesc = tq->sdesc;
		}
	}
	tq->cidx = cidx;
}

/*
 * Return the number of reclaimable descriptors in a TX queue.
 */
static inline int reclaimable(const struct sge_txq *tq)
{
	int hw_cidx = be16_to_cpu(tq->stat->cidx);
	int reclaimable = hw_cidx - tq->cidx;
	if (reclaimable < 0)
		reclaimable += tq->size;
	return reclaimable;
}

/**
 *	reclaim_completed_tx - reclaims completed TX descriptors
 *	@adapter: the adapter
 *	@tq: the TX queue to reclaim completed descriptors from
 *	@unmap: whether the buffers should be unmapped for DMA
 *
 *	Reclaims TX descriptors that the SGE has indicated it has processed,
 *	and frees the associated buffers if possible.  Called with the TX
 *	queue locked.
 */
static inline void reclaim_completed_tx(struct adapter *adapter,
					struct sge_txq *tq,
					bool unmap)
{
	int avail = reclaimable(tq);

	if (avail) {
		/*
		 * Limit the amount of clean up work we do at a time to keep
		 * the TX lock hold time O(1).
		 */
		if (avail > MAX_TX_RECLAIM)
			avail = MAX_TX_RECLAIM;

		free_tx_desc(adapter, tq, avail, unmap);
		tq->in_use -= avail;
	}
}

/**
 *	get_buf_size - return the size of an RX Free List buffer.
 *	@sdesc: pointer to the software buffer descriptor
 */
static inline int get_buf_size(const struct rx_sw_desc *sdesc)
{
	return FL_PG_ORDER > 0 && (sdesc->dma_addr & RX_LARGE_BUF)
		? (PAGE_SIZE << FL_PG_ORDER)
		: PAGE_SIZE;
}

/**
 *	free_rx_bufs - free RX buffers on an SGE Free List
 *	@adapter: the adapter
 *	@fl: the SGE Free List to free buffers from
 *	@n: how many buffers to free
 *
 *	Release the next @n buffers on an SGE Free List RX queue.   The
 *	buffers must be made inaccessible to hardware before calling this
 *	function.
 */
static void free_rx_bufs(struct adapter *adapter, struct sge_fl *fl, int n)
{
	while (n--) {
		struct rx_sw_desc *sdesc = &fl->sdesc[fl->cidx];

		if (is_buf_mapped(sdesc))
			dma_unmap_page(adapter->pdev_dev, get_buf_addr(sdesc),
				       get_buf_size(sdesc), PCI_DMA_FROMDEVICE);
		put_page(sdesc->page);
		sdesc->page = NULL;
		if (++fl->cidx == fl->size)
			fl->cidx = 0;
		fl->avail--;
	}
}

/**
 *	unmap_rx_buf - unmap the current RX buffer on an SGE Free List
 *	@adapter: the adapter
 *	@fl: the SGE Free List
 *
 *	Unmap the current buffer on an SGE Free List RX queue.   The
 *	buffer must be made inaccessible to HW before calling this function.
 *
 *	This is similar to @free_rx_bufs above but does not free the buffer.
 *	Do note that the FL still loses any further access to the buffer.
 *	This is used predominantly to "transfer ownership" of an FL buffer
 *	to another entity (typically an skb's fragment list).
 */
static void unmap_rx_buf(struct adapter *adapter, struct sge_fl *fl)
{
	struct rx_sw_desc *sdesc = &fl->sdesc[fl->cidx];

	if (is_buf_mapped(sdesc))
		dma_unmap_page(adapter->pdev_dev, get_buf_addr(sdesc),
			       get_buf_size(sdesc), PCI_DMA_FROMDEVICE);
	sdesc->page = NULL;
	if (++fl->cidx == fl->size)
		fl->cidx = 0;
	fl->avail--;
}

/**
 *	ring_fl_db - righ doorbell on free list
 *	@adapter: the adapter
 *	@fl: the Free List whose doorbell should be rung ...
 *
 *	Tell the Scatter Gather Engine that there are new free list entries
 *	available.
 */
static inline void ring_fl_db(struct adapter *adapter, struct sge_fl *fl)
{
	/*
	 * The SGE keeps track of its Producer and Consumer Indices in terms
	 * of Egress Queue Units so we can only tell it about integral numbers
	 * of multiples of Free List Entries per Egress Queue Units ...
	 */
	if (fl->pend_cred >= FL_PER_EQ_UNIT) {
		wmb();
		t4_write_reg(adapter, T4VF_SGE_BASE_ADDR + SGE_VF_KDOORBELL,
			     DBPRIO |
			     QID(fl->cntxt_id) |
			     PIDX(fl->pend_cred / FL_PER_EQ_UNIT));
		fl->pend_cred %= FL_PER_EQ_UNIT;
	}
}

/**
 *	set_rx_sw_desc - initialize software RX buffer descriptor
 *	@sdesc: pointer to the softwore RX buffer descriptor
 *	@page: pointer to the page data structure backing the RX buffer
 *	@dma_addr: PCI DMA address (possibly with low-bit flags)
 */
static inline void set_rx_sw_desc(struct rx_sw_desc *sdesc, struct page *page,
				  dma_addr_t dma_addr)
{
	sdesc->page = page;
	sdesc->dma_addr = dma_addr;
}

/*
 * Support for poisoning RX buffers ...
 */
#define POISON_BUF_VAL -1

static inline void poison_buf(struct page *page, size_t sz)
{
#if POISON_BUF_VAL >= 0
	memset(page_address(page), POISON_BUF_VAL, sz);
#endif
}

/**
 *	refill_fl - refill an SGE RX buffer ring
 *	@adapter: the adapter
 *	@fl: the Free List ring to refill
 *	@n: the number of new buffers to allocate
 *	@gfp: the gfp flags for the allocations
 *
 *	(Re)populate an SGE free-buffer queue with up to @n new packet buffers,
 *	allocated with the supplied gfp flags.  The caller must assure that
 *	@n does not exceed the queue's capacity -- i.e. (cidx == pidx) _IN
 *	EGRESS QUEUE UNITS_ indicates an empty Free List!  Returns the number
 *	of buffers allocated.  If afterwards the queue is found critically low,
 *	mark it as starving in the bitmap of starving FLs.
 */
static unsigned int refill_fl(struct adapter *adapter, struct sge_fl *fl,
			      int n, gfp_t gfp)
{
	struct page *page;
	dma_addr_t dma_addr;
	unsigned int cred = fl->avail;
	__be64 *d = &fl->desc[fl->pidx];
	struct rx_sw_desc *sdesc = &fl->sdesc[fl->pidx];

	/*
	 * Sanity: ensure that the result of adding n Free List buffers
	 * won't result in wrapping the SGE's Producer Index around to
	 * it's Consumer Index thereby indicating an empty Free List ...
	 */
	BUG_ON(fl->avail + n > fl->size - FL_PER_EQ_UNIT);

	/*
	 * If we support large pages, prefer large buffers and fail over to
	 * small pages if we can't allocate large pages to satisfy the refill.
	 * If we don't support large pages, drop directly into the small page
	 * allocation code.
	 */
	if (FL_PG_ORDER == 0)
		goto alloc_small_pages;

	while (n) {
		page = alloc_pages(gfp | __GFP_COMP | __GFP_NOWARN,
				   FL_PG_ORDER);
		if (unlikely(!page)) {
			/*
			 * We've failed inour attempt to allocate a "large
			 * page".  Fail over to the "small page" allocation
			 * below.
			 */
			fl->large_alloc_failed++;
			break;
		}
		poison_buf(page, PAGE_SIZE << FL_PG_ORDER);

		dma_addr = dma_map_page(adapter->pdev_dev, page, 0,
					PAGE_SIZE << FL_PG_ORDER,
					PCI_DMA_FROMDEVICE);
		if (unlikely(dma_mapping_error(adapter->pdev_dev, dma_addr))) {
			/*
			 * We've run out of DMA mapping space.  Free up the
			 * buffer and return with what we've managed to put
			 * into the free list.  We don't want to fail over to
			 * the small page allocation below in this case
			 * because DMA mapping resources are typically
			 * critical resources once they become scarse.
			 */
			__free_pages(page, FL_PG_ORDER);
			goto out;
		}
		dma_addr |= RX_LARGE_BUF;
		*d++ = cpu_to_be64(dma_addr);

		set_rx_sw_desc(sdesc, page, dma_addr);
		sdesc++;

		fl->avail++;
		if (++fl->pidx == fl->size) {
			fl->pidx = 0;
			sdesc = fl->sdesc;
			d = fl->desc;
		}
		n--;
	}

alloc_small_pages:
	while (n--) {
		page = __netdev_alloc_page(adapter->port[0],
					   gfp | __GFP_NOWARN);
		if (unlikely(!page)) {
			fl->alloc_failed++;
			break;
		}
		poison_buf(page, PAGE_SIZE);

		dma_addr = dma_map_page(adapter->pdev_dev, page, 0, PAGE_SIZE,
				       PCI_DMA_FROMDEVICE);
		if (unlikely(dma_mapping_error(adapter->pdev_dev, dma_addr))) {
			netdev_free_page(adapter->port[0], page);
			break;
		}
		*d++ = cpu_to_be64(dma_addr);

		set_rx_sw_desc(sdesc, page, dma_addr);
		sdesc++;

		fl->avail++;
		if (++fl->pidx == fl->size) {
			fl->pidx = 0;
			sdesc = fl->sdesc;
			d = fl->desc;
		}
	}

out:
	/*
	 * Update our accounting state to incorporate the new Free List
	 * buffers, tell the hardware about them and return the number of
	 * buffers which we were able to allocate.
	 */
	cred = fl->avail - cred;
	fl->pend_cred += cred;
	ring_fl_db(adapter, fl);

	if (unlikely(fl_starving(fl))) {
		smp_wmb();
		set_bit(fl->cntxt_id, adapter->sge.starving_fl);
	}

	return cred;
}

/*
 * Refill a Free List to its capacity or the Maximum Refill Increment,
 * whichever is smaller ...
 */
static inline void __refill_fl(struct adapter *adapter, struct sge_fl *fl)
{
	refill_fl(adapter, fl,
		  min((unsigned int)MAX_RX_REFILL, fl_cap(fl) - fl->avail),
		  GFP_ATOMIC);
}

/**
 *	alloc_ring - allocate resources for an SGE descriptor ring
 *	@dev: the PCI device's core device
 *	@nelem: the number of descriptors
 *	@hwsize: the size of each hardware descriptor
 *	@swsize: the size of each software descriptor
 *	@busaddrp: the physical PCI bus address of the allocated ring
 *	@swringp: return address pointer for software ring
 *	@stat_size: extra space in hardware ring for status information
 *
 *	Allocates resources for an SGE descriptor ring, such as TX queues,
 *	free buffer lists, response queues, etc.  Each SGE ring requires
 *	space for its hardware descriptors plus, optionally, space for software
 *	state associated with each hardware entry (the metadata).  The function
 *	returns three values: the virtual address for the hardware ring (the
 *	return value of the function), the PCI bus address of the hardware
 *	ring (in *busaddrp), and the address of the software ring (in swringp).
 *	Both the hardware and software rings are returned zeroed out.
 */
static void *alloc_ring(struct device *dev, size_t nelem, size_t hwsize,
			size_t swsize, dma_addr_t *busaddrp, void *swringp,
			size_t stat_size)
{
	/*
	 * Allocate the hardware ring and PCI DMA bus address space for said.
	 */
	size_t hwlen = nelem * hwsize + stat_size;
	void *hwring = dma_alloc_coherent(dev, hwlen, busaddrp, GFP_KERNEL);

	if (!hwring)
		return NULL;

	/*
	 * If the caller wants a software ring, allocate it and return a
	 * pointer to it in *swringp.
	 */
	BUG_ON((swsize != 0) != (swringp != NULL));
	if (swsize) {
		void *swring = kcalloc(nelem, swsize, GFP_KERNEL);

		if (!swring) {
			dma_free_coherent(dev, hwlen, hwring, *busaddrp);
			return NULL;
		}
		*(void **)swringp = swring;
	}

	/*
	 * Zero out the hardware ring and return its address as our function
	 * value.
	 */
	memset(hwring, 0, hwlen);
	return hwring;
}

/**
 *	sgl_len - calculates the size of an SGL of the given capacity
 *	@n: the number of SGL entries
 *
 *	Calculates the number of flits (8-byte units) needed for a Direct
 *	Scatter/Gather List that can hold the given number of entries.
 */
static inline unsigned int sgl_len(unsigned int n)
{
	/*
	 * A Direct Scatter Gather List uses 32-bit lengths and 64-bit PCI DMA
	 * addresses.  The DSGL Work Request starts off with a 32-bit DSGL
	 * ULPTX header, then Length0, then Address0, then, for 1 <= i <= N,
	 * repeated sequences of { Length[i], Length[i+1], Address[i],
	 * Address[i+1] } (this ensures that all addresses are on 64-bit
	 * boundaries).  If N is even, then Length[N+1] should be set to 0 and
	 * Address[N+1] is omitted.
	 *
	 * The following calculation incorporates all of the above.  It's
	 * somewhat hard to follow but, briefly: the "+2" accounts for the
	 * first two flits which include the DSGL header, Length0 and
	 * Address0; the "(3*(n-1))/2" covers the main body of list entries (3
	 * flits for every pair of the remaining N) +1 if (n-1) is odd; and
	 * finally the "+((n-1)&1)" adds the one remaining flit needed if
	 * (n-1) is odd ...
	 */
	n--;
	return (3 * n) / 2 + (n & 1) + 2;
}

/**
 *	flits_to_desc - returns the num of TX descriptors for the given flits
 *	@flits: the number of flits
 *
 *	Returns the number of TX descriptors needed for the supplied number
 *	of flits.
 */
static inline unsigned int flits_to_desc(unsigned int flits)
{
	BUG_ON(flits > SGE_MAX_WR_LEN / sizeof(__be64));
	return DIV_ROUND_UP(flits, TXD_PER_EQ_UNIT);
}

/**
 *	is_eth_imm - can an Ethernet packet be sent as immediate data?
 *	@skb: the packet
 *
 *	Returns whether an Ethernet packet is small enough to fit completely as
 *	immediate data.
 */
static inline int is_eth_imm(const struct sk_buff *skb)
{
	/*
	 * The VF Driver uses the FW_ETH_TX_PKT_VM_WR firmware Work Request
	 * which does not accommodate immediate data.  We could dike out all
	 * of the support code for immediate data but that would tie our hands
	 * too much if we ever want to enhace the firmware.  It would also
	 * create more differences between the PF and VF Drivers.
	 */
	return false;
}

/**
 *	calc_tx_flits - calculate the number of flits for a packet TX WR
 *	@skb: the packet
 *
 *	Returns the number of flits needed for a TX Work Request for the
 *	given Ethernet packet, including the needed WR and CPL headers.
 */
static inline unsigned int calc_tx_flits(const struct sk_buff *skb)
{
	unsigned int flits;

	/*
	 * If the skb is small enough, we can pump it out as a work request
	 * with only immediate data.  In that case we just have to have the
	 * TX Packet header plus the skb data in the Work Request.
	 */
	if (is_eth_imm(skb))
		return DIV_ROUND_UP(skb->len + sizeof(struct cpl_tx_pkt),
				    sizeof(__be64));

	/*
	 * Otherwise, we're going to have to construct a Scatter gather list
	 * of the skb body and fragments.  We also include the flits necessary
	 * for the TX Packet Work Request and CPL.  We always have a firmware
	 * Write Header (incorporated as part of the cpl_tx_pkt_lso and
	 * cpl_tx_pkt structures), followed by either a TX Packet Write CPL
	 * message or, if we're doing a Large Send Offload, an LSO CPL message
	 * with an embeded TX Packet Write CPL message.
	 */
	flits = sgl_len(skb_shinfo(skb)->nr_frags + 1);
	if (skb_shinfo(skb)->gso_size)
		flits += (sizeof(struct fw_eth_tx_pkt_vm_wr) +
			  sizeof(struct cpl_tx_pkt_lso_core) +
			  sizeof(struct cpl_tx_pkt_core)) / sizeof(__be64);
	else
		flits += (sizeof(struct fw_eth_tx_pkt_vm_wr) +
			  sizeof(struct cpl_tx_pkt_core)) / sizeof(__be64);
	return flits;
}

/**
 *	write_sgl - populate a Scatter/Gather List for a packet
 *	@skb: the packet
 *	@tq: the TX queue we are writing into
 *	@sgl: starting location for writing the SGL
 *	@end: points right after the end of the SGL
 *	@start: start offset into skb main-body data to include in the SGL
 *	@addr: the list of DMA bus addresses for the SGL elements
 *
 *	Generates a Scatter/Gather List for the buffers that make up a packet.
 *	The caller must provide adequate space for the SGL that will be written.
 *	The SGL includes all of the packet's page fragments and the data in its
 *	main body except for the first @start bytes.  @pos must be 16-byte
 *	aligned and within a TX descriptor with available space.  @end points
 *	write after the end of the SGL but does not account for any potential
 *	wrap around, i.e., @end > @tq->stat.
 */
static void write_sgl(const struct sk_buff *skb, struct sge_txq *tq,
		      struct ulptx_sgl *sgl, u64 *end, unsigned int start,
		      const dma_addr_t *addr)
{
	unsigned int i, len;
	struct ulptx_sge_pair *to;
	const struct skb_shared_info *si = skb_shinfo(skb);
	unsigned int nfrags = si->nr_frags;
	struct ulptx_sge_pair buf[MAX_SKB_FRAGS / 2 + 1];

	len = skb_headlen(skb) - start;
	if (likely(len)) {
		sgl->len0 = htonl(len);
		sgl->addr0 = cpu_to_be64(addr[0] + start);
		nfrags++;
	} else {
		sgl->len0 = htonl(skb_frag_size(&si->frags[0]));
		sgl->addr0 = cpu_to_be64(addr[1]);
	}

	sgl->cmd_nsge = htonl(ULPTX_CMD(ULP_TX_SC_DSGL) |
			      ULPTX_NSGE(nfrags));
	if (likely(--nfrags == 0))
		return;
	/*
	 * Most of the complexity below deals with the possibility we hit the
	 * end of the queue in the middle of writing the SGL.  For this case
	 * only we create the SGL in a temporary buffer and then copy it.
	 */
	to = (u8 *)end > (u8 *)tq->stat ? buf : sgl->sge;

	for (i = (nfrags != si->nr_frags); nfrags >= 2; nfrags -= 2, to++) {
		to->len[0] = cpu_to_be32(skb_frag_size(&si->frags[i]));
		to->len[1] = cpu_to_be32(skb_frag_size(&si->frags[++i]));
		to->addr[0] = cpu_to_be64(addr[i]);
		to->addr[1] = cpu_to_be64(addr[++i]);
	}
	if (nfrags) {
		to->len[0] = cpu_to_be32(skb_frag_size(&si->frags[i]));
		to->len[1] = cpu_to_be32(0);
		to->addr[0] = cpu_to_be64(addr[i + 1]);
	}
	if (unlikely((u8 *)end > (u8 *)tq->stat)) {
		unsigned int part0 = (u8 *)tq->stat - (u8 *)sgl->sge, part1;

		if (likely(part0))
			memcpy(sgl->sge, buf, part0);
		part1 = (u8 *)end - (u8 *)tq->stat;
		memcpy(tq->desc, (u8 *)buf + part0, part1);
		end = (void *)tq->desc + part1;
	}
	if ((uintptr_t)end & 8)           /* 0-pad to multiple of 16 */
		*(u64 *)end = 0;
}

/**
 *	check_ring_tx_db - check and potentially ring a TX queue's doorbell
 *	@adapter: the adapter
 *	@tq: the TX queue
 *	@n: number of new descriptors to give to HW
 *
 *	Ring the doorbel for a TX queue.
 */
static inline void ring_tx_db(struct adapter *adapter, struct sge_txq *tq,
			      int n)
{
	/*
	 * Warn if we write doorbells with the wrong priority and write
	 * descriptors before telling HW.
	 */
	WARN_ON((QID(tq->cntxt_id) | PIDX(n)) & DBPRIO);
	wmb();
	t4_write_reg(adapter, T4VF_SGE_BASE_ADDR + SGE_VF_KDOORBELL,
		     QID(tq->cntxt_id) | PIDX(n));
}

/**
 *	inline_tx_skb - inline a packet's data into TX descriptors
 *	@skb: the packet
 *	@tq: the TX queue where the packet will be inlined
 *	@pos: starting position in the TX queue to inline the packet
 *
 *	Inline a packet's contents directly into TX descriptors, starting at
 *	the given position within the TX DMA ring.
 *	Most of the complexity of this operation is dealing with wrap arounds
 *	in the middle of the packet we want to inline.
 */
static void inline_tx_skb(const struct sk_buff *skb, const struct sge_txq *tq,
			  void *pos)
{
	u64 *p;
	int left = (void *)tq->stat - pos;

	if (likely(skb->len <= left)) {
		if (likely(!skb->data_len))
			skb_copy_from_linear_data(skb, pos, skb->len);
		else
			skb_copy_bits(skb, 0, pos, skb->len);
		pos += skb->len;
	} else {
		skb_copy_bits(skb, 0, pos, left);
		skb_copy_bits(skb, left, tq->desc, skb->len - left);
		pos = (void *)tq->desc + (skb->len - left);
	}

	/* 0-pad to multiple of 16 */
	p = PTR_ALIGN(pos, 8);
	if ((uintptr_t)p & 8)
		*p = 0;
}

/*
 * Figure out what HW csum a packet wants and return the appropriate control
 * bits.
 */
static u64 hwcsum(const struct sk_buff *skb)
{
	int csum_type;
	const struct iphdr *iph = ip_hdr(skb);

	if (iph->version == 4) {
		if (iph->protocol == IPPROTO_TCP)
			csum_type = TX_CSUM_TCPIP;
		else if (iph->protocol == IPPROTO_UDP)
			csum_type = TX_CSUM_UDPIP;
		else {
nocsum:
			/*
			 * unknown protocol, disable HW csum
			 * and hope a bad packet is detected
			 */
			return TXPKT_L4CSUM_DIS;
		}
	} else {
		/*
		 * this doesn't work with extension headers
		 */
		const struct ipv6hdr *ip6h = (const struct ipv6hdr *)iph;

		if (ip6h->nexthdr == IPPROTO_TCP)
			csum_type = TX_CSUM_TCPIP6;
		else if (ip6h->nexthdr == IPPROTO_UDP)
			csum_type = TX_CSUM_UDPIP6;
		else
			goto nocsum;
	}

	if (likely(csum_type >= TX_CSUM_TCPIP))
		return TXPKT_CSUM_TYPE(csum_type) |
			TXPKT_IPHDR_LEN(skb_network_header_len(skb)) |
			TXPKT_ETHHDR_LEN(skb_network_offset(skb) - ETH_HLEN);
	else {
		int start = skb_transport_offset(skb);

		return TXPKT_CSUM_TYPE(csum_type) |
			TXPKT_CSUM_START(start) |
			TXPKT_CSUM_LOC(start + skb->csum_offset);
	}
}

/*
 * Stop an Ethernet TX queue and record that state change.
 */
static void txq_stop(struct sge_eth_txq *txq)
{
	netif_tx_stop_queue(txq->txq);
	txq->q.stops++;
}

/*
 * Advance our software state for a TX queue by adding n in use descriptors.
 */
static inline void txq_advance(struct sge_txq *tq, unsigned int n)
{
	tq->in_use += n;
	tq->pidx += n;
	if (tq->pidx >= tq->size)
		tq->pidx -= tq->size;
}

/**
 *	t4vf_eth_xmit - add a packet to an Ethernet TX queue
 *	@skb: the packet
 *	@dev: the egress net device
 *
 *	Add a packet to an SGE Ethernet TX queue.  Runs with softirqs disabled.
 */
int t4vf_eth_xmit(struct sk_buff *skb, struct net_device *dev)
{
	u32 wr_mid;
	u64 cntrl, *end;
	int qidx, credits;
	unsigned int flits, ndesc;
	struct adapter *adapter;
	struct sge_eth_txq *txq;
	const struct port_info *pi;
	struct fw_eth_tx_pkt_vm_wr *wr;
	struct cpl_tx_pkt_core *cpl;
	const struct skb_shared_info *ssi;
	dma_addr_t addr[MAX_SKB_FRAGS + 1];
	const size_t fw_hdr_copy_len = (sizeof(wr->ethmacdst) +
					sizeof(wr->ethmacsrc) +
					sizeof(wr->ethtype) +
					sizeof(wr->vlantci));

	/*
	 * The chip minimum packet length is 10 octets but the firmware
	 * command that we are using requires that we copy the Ethernet header
	 * (including the VLAN tag) into the header so we reject anything
	 * smaller than that ...
	 */
	if (unlikely(skb->len < fw_hdr_copy_len))
		goto out_free;

	/*
	 * Figure out which TX Queue we're going to use.
	 */
	pi = netdev_priv(dev);
	adapter = pi->adapter;
	qidx = skb_get_queue_mapping(skb);
	BUG_ON(qidx >= pi->nqsets);
	txq = &adapter->sge.ethtxq[pi->first_qset + qidx];

	/*
	 * Take this opportunity to reclaim any TX Descriptors whose DMA
	 * transfers have completed.
	 */
	reclaim_completed_tx(adapter, &txq->q, true);

	/*
	 * Calculate the number of flits and TX Descriptors we're going to
	 * need along with how many TX Descriptors will be left over after
	 * we inject our Work Request.
	 */
	flits = calc_tx_flits(skb);
	ndesc = flits_to_desc(flits);
	credits = txq_avail(&txq->q) - ndesc;

	if (unlikely(credits < 0)) {
		/*
		 * Not enough room for this packet's Work Request.  Stop the
		 * TX Queue and return a "busy" condition.  The queue will get
		 * started later on when the firmware informs us that space
		 * has opened up.
		 */
		txq_stop(txq);
		dev_err(adapter->pdev_dev,
			"%s: TX ring %u full while queue awake!\n",
			dev->name, qidx);
		return NETDEV_TX_BUSY;
	}

	if (!is_eth_imm(skb) &&
	    unlikely(map_skb(adapter->pdev_dev, skb, addr) < 0)) {
		/*
		 * We need to map the skb into PCI DMA space (because it can't
		 * be in-lined directly into the Work Request) and the mapping
		 * operation failed.  Record the error and drop the packet.
		 */
		txq->mapping_err++;
		goto out_free;
	}

	wr_mid = FW_WR_LEN16(DIV_ROUND_UP(flits, 2));
	if (unlikely(credits < ETHTXQ_STOP_THRES)) {
		/*
		 * After we're done injecting the Work Request for this
		 * packet, we'll be below our "stop threshold" so stop the TX
		 * Queue now and schedule a request for an SGE Egress Queue
		 * Update message.  The queue will get started later on when
		 * the firmware processes this Work Request and sends us an
		 * Egress Queue Status Update message indicating that space
		 * has opened up.
		 */
		txq_stop(txq);
		wr_mid |= FW_WR_EQUEQ | FW_WR_EQUIQ;
	}

	/*
	 * Start filling in our Work Request.  Note that we do _not_ handle
	 * the WR Header wrapping around the TX Descriptor Ring.  If our
	 * maximum header size ever exceeds one TX Descriptor, we'll need to
	 * do something else here.
	 */
	BUG_ON(DIV_ROUND_UP(ETHTXQ_MAX_HDR, TXD_PER_EQ_UNIT) > 1);
	wr = (void *)&txq->q.desc[txq->q.pidx];
	wr->equiq_to_len16 = cpu_to_be32(wr_mid);
	wr->r3[0] = cpu_to_be64(0);
	wr->r3[1] = cpu_to_be64(0);
	skb_copy_from_linear_data(skb, (void *)wr->ethmacdst, fw_hdr_copy_len);
	end = (u64 *)wr + flits;

	/*
	 * If this is a Large Send Offload packet we'll put in an LSO CPL
	 * message with an encapsulated TX Packet CPL message.  Otherwise we
	 * just use a TX Packet CPL message.
	 */
	ssi = skb_shinfo(skb);
	if (ssi->gso_size) {
		struct cpl_tx_pkt_lso_core *lso = (void *)(wr + 1);
		bool v6 = (ssi->gso_type & SKB_GSO_TCPV6) != 0;
		int l3hdr_len = skb_network_header_len(skb);
		int eth_xtra_len = skb_network_offset(skb) - ETH_HLEN;

		wr->op_immdlen =
			cpu_to_be32(FW_WR_OP(FW_ETH_TX_PKT_VM_WR) |
				    FW_WR_IMMDLEN(sizeof(*lso) +
						  sizeof(*cpl)));
		/*
		 * Fill in the LSO CPL message.
		 */
		lso->lso_ctrl =
			cpu_to_be32(LSO_OPCODE(CPL_TX_PKT_LSO) |
				    LSO_FIRST_SLICE |
				    LSO_LAST_SLICE |
				    LSO_IPV6(v6) |
				    LSO_ETHHDR_LEN(eth_xtra_len/4) |
				    LSO_IPHDR_LEN(l3hdr_len/4) |
				    LSO_TCPHDR_LEN(tcp_hdr(skb)->doff));
		lso->ipid_ofst = cpu_to_be16(0);
		lso->mss = cpu_to_be16(ssi->gso_size);
		lso->seqno_offset = cpu_to_be32(0);
		lso->len = cpu_to_be32(skb->len);

		/*
		 * Set up TX Packet CPL pointer, control word and perform
		 * accounting.
		 */
		cpl = (void *)(lso + 1);
		cntrl = (TXPKT_CSUM_TYPE(v6 ? TX_CSUM_TCPIP6 : TX_CSUM_TCPIP) |
			 TXPKT_IPHDR_LEN(l3hdr_len) |
			 TXPKT_ETHHDR_LEN(eth_xtra_len));
		txq->tso++;
		txq->tx_cso += ssi->gso_segs;
	} else {
		int len;

		len = is_eth_imm(skb) ? skb->len + sizeof(*cpl) : sizeof(*cpl);
		wr->op_immdlen =
			cpu_to_be32(FW_WR_OP(FW_ETH_TX_PKT_VM_WR) |
				    FW_WR_IMMDLEN(len));

		/*
		 * Set up TX Packet CPL pointer, control word and perform
		 * accounting.
		 */
		cpl = (void *)(wr + 1);
		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			cntrl = hwcsum(skb) | TXPKT_IPCSUM_DIS;
			txq->tx_cso++;
		} else
			cntrl = TXPKT_L4CSUM_DIS | TXPKT_IPCSUM_DIS;
	}

	/*
	 * If there's a VLAN tag present, add that to the list of things to
	 * do in this Work Request.
	 */
	if (vlan_tx_tag_present(skb)) {
		txq->vlan_ins++;
		cntrl |= TXPKT_VLAN_VLD | TXPKT_VLAN(vlan_tx_tag_get(skb));
	}

	/*
	 * Fill in the TX Packet CPL message header.
	 */
	cpl->ctrl0 = cpu_to_be32(TXPKT_OPCODE(CPL_TX_PKT_XT) |
				 TXPKT_INTF(pi->port_id) |
				 TXPKT_PF(0));
	cpl->pack = cpu_to_be16(0);
	cpl->len = cpu_to_be16(skb->len);
	cpl->ctrl1 = cpu_to_be64(cntrl);

#ifdef T4_TRACE
	T4_TRACE5(adapter->tb[txq->q.cntxt_id & 7],
		  "eth_xmit: ndesc %u, credits %u, pidx %u, len %u, frags %u",
		  ndesc, credits, txq->q.pidx, skb->len, ssi->nr_frags);
#endif

	/*
	 * Fill in the body of the TX Packet CPL message with either in-lined
	 * data or a Scatter/Gather List.
	 */
	if (is_eth_imm(skb)) {
		/*
		 * In-line the packet's data and free the skb since we don't
		 * need it any longer.
		 */
		inline_tx_skb(skb, &txq->q, cpl + 1);
		dev_kfree_skb(skb);
	} else {
		/*
		 * Write the skb's Scatter/Gather list into the TX Packet CPL
		 * message and retain a pointer to the skb so we can free it
		 * later when its DMA completes.  (We store the skb pointer
		 * in the Software Descriptor corresponding to the last TX
		 * Descriptor used by the Work Request.)
		 *
		 * The retained skb will be freed when the corresponding TX
		 * Descriptors are reclaimed after their DMAs complete.
		 * However, this could take quite a while since, in general,
		 * the hardware is set up to be lazy about sending DMA
		 * completion notifications to us and we mostly perform TX
		 * reclaims in the transmit routine.
		 *
		 * This is good for performamce but means that we rely on new
		 * TX packets arriving to run the destructors of completed
		 * packets, which open up space in their sockets' send queues.
		 * Sometimes we do not get such new packets causing TX to
		 * stall.  A single UDP transmitter is a good example of this
		 * situation.  We have a clean up timer that periodically
		 * reclaims completed packets but it doesn't run often enough
		 * (nor do we want it to) to prevent lengthy stalls.  A
		 * solution to this problem is to run the destructor early,
		 * after the packet is queued but before it's DMAd.  A con is
		 * that we lie to socket memory accounting, but the amount of
		 * extra memory is reasonable (limited by the number of TX
		 * descriptors), the packets do actually get freed quickly by
		 * new packets almost always, and for protocols like TCP that
		 * wait for acks to really free up the data the extra memory
		 * is even less.  On the positive side we run the destructors
		 * on the sending CPU rather than on a potentially different
		 * completing CPU, usually a good thing.
		 *
		 * Run the destructor before telling the DMA engine about the
		 * packet to make sure it doesn't complete and get freed
		 * prematurely.
		 */
		struct ulptx_sgl *sgl = (struct ulptx_sgl *)(cpl + 1);
		struct sge_txq *tq = &txq->q;
		int last_desc;

		/*
		 * If the Work Request header was an exact multiple of our TX
		 * Descriptor length, then it's possible that the starting SGL
		 * pointer lines up exactly with the end of our TX Descriptor
		 * ring.  If that's the case, wrap around to the beginning
		 * here ...
		 */
		if (unlikely((void *)sgl == (void *)tq->stat)) {
			sgl = (void *)tq->desc;
			end = (void *)((void *)tq->desc +
				       ((void *)end - (void *)tq->stat));
		}

		write_sgl(skb, tq, sgl, end, 0, addr);
		skb_orphan(skb);

		last_desc = tq->pidx + ndesc - 1;
		if (last_desc >= tq->size)
			last_desc -= tq->size;
		tq->sdesc[last_desc].skb = skb;
		tq->sdesc[last_desc].sgl = sgl;
	}

	/*
	 * Advance our internal TX Queue state, tell the hardware about
	 * the new TX descriptors and return success.
	 */
	txq_advance(&txq->q, ndesc);
	dev->trans_start = jiffies;
	ring_tx_db(adapter, &txq->q, ndesc);
	return NETDEV_TX_OK;

out_free:
	/*
	 * An error of some sort happened.  Free the TX skb and tell the
	 * OS that we've "dealt" with the packet ...
	 */
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

/**
 *	copy_frags - copy fragments from gather list into skb_shared_info
 *	@skb: destination skb
 *	@gl: source internal packet gather list
 *	@offset: packet start offset in first page
 *
 *	Copy an internal packet gather list into a Linux skb_shared_info
 *	structure.
 */
static inline void copy_frags(struct sk_buff *skb,
			      const struct pkt_gl *gl,
			      unsigned int offset)
{
	int i;

	/* usually there's just one frag */
	__skb_fill_page_desc(skb, 0, gl->frags[0].page,
			     gl->frags[0].offset + offset,
			     gl->frags[0].size - offset);
	skb_shinfo(skb)->nr_frags = gl->nfrags;
	for (i = 1; i < gl->nfrags; i++)
		__skb_fill_page_desc(skb, i, gl->frags[i].page,
				     gl->frags[i].offset,
				     gl->frags[i].size);

	/* get a reference to the last page, we don't own it */
	get_page(gl->frags[gl->nfrags - 1].page);
}

/**
 *	t4vf_pktgl_to_skb - build an sk_buff from a packet gather list
 *	@gl: the gather list
 *	@skb_len: size of sk_buff main body if it carries fragments
 *	@pull_len: amount of data to move to the sk_buff's main body
 *
 *	Builds an sk_buff from the given packet gather list.  Returns the
 *	sk_buff or %NULL if sk_buff allocation failed.
 */
struct sk_buff *t4vf_pktgl_to_skb(const struct pkt_gl *gl,
				  unsigned int skb_len, unsigned int pull_len)
{
	struct sk_buff *skb;

	/*
	 * If the ingress packet is small enough, allocate an skb large enough
	 * for all of the data and copy it inline.  Otherwise, allocate an skb
	 * with enough room to pull in the header and reference the rest of
	 * the data via the skb fragment list.
	 *
	 * Below we rely on RX_COPY_THRES being less than the smallest Rx
	 * buff!  size, which is expected since buffers are at least
	 * PAGE_SIZEd.  In this case packets up to RX_COPY_THRES have only one
	 * fragment.
	 */
	if (gl->tot_len <= RX_COPY_THRES) {
		/* small packets have only one fragment */
		skb = alloc_skb(gl->tot_len, GFP_ATOMIC);
		if (unlikely(!skb))
			goto out;
		__skb_put(skb, gl->tot_len);
		skb_copy_to_linear_data(skb, gl->va, gl->tot_len);
	} else {
		skb = alloc_skb(skb_len, GFP_ATOMIC);
		if (unlikely(!skb))
			goto out;
		__skb_put(skb, pull_len);
		skb_copy_to_linear_data(skb, gl->va, pull_len);

		copy_frags(skb, gl, pull_len);
		skb->len = gl->tot_len;
		skb->data_len = skb->len - pull_len;
		skb->truesize += skb->data_len;
	}

out:
	return skb;
}

/**
 *	t4vf_pktgl_free - free a packet gather list
 *	@gl: the gather list
 *
 *	Releases the pages of a packet gather list.  We do not own the last
 *	page on the list and do not free it.
 */
void t4vf_pktgl_free(const struct pkt_gl *gl)
{
	int frag;

	frag = gl->nfrags - 1;
	while (frag--)
		put_page(gl->frags[frag].page);
}

/**
 *	do_gro - perform Generic Receive Offload ingress packet processing
 *	@rxq: ingress RX Ethernet Queue
 *	@gl: gather list for ingress packet
 *	@pkt: CPL header for last packet fragment
 *
 *	Perform Generic Receive Offload (GRO) ingress packet processing.
 *	We use the standard Linux GRO interfaces for this.
 */
static void do_gro(struct sge_eth_rxq *rxq, const struct pkt_gl *gl,
		   const struct cpl_rx_pkt *pkt)
{
	int ret;
	struct sk_buff *skb;

	skb = napi_get_frags(&rxq->rspq.napi);
	if (unlikely(!skb)) {
		t4vf_pktgl_free(gl);
		rxq->stats.rx_drops++;
		return;
	}

	copy_frags(skb, gl, PKTSHIFT);
	skb->len = gl->tot_len - PKTSHIFT;
	skb->data_len = skb->len;
	skb->truesize += skb->data_len;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb_record_rx_queue(skb, rxq->rspq.idx);

	if (pkt->vlan_ex)
		__vlan_hwaccel_put_tag(skb, be16_to_cpu(pkt->vlan));
	ret = napi_gro_frags(&rxq->rspq.napi);

	if (ret == GRO_HELD)
		rxq->stats.lro_pkts++;
	else if (ret == GRO_MERGED || ret == GRO_MERGED_FREE)
		rxq->stats.lro_merged++;
	rxq->stats.pkts++;
	rxq->stats.rx_cso++;
}

/**
 *	t4vf_ethrx_handler - process an ingress ethernet packet
 *	@rspq: the response queue that received the packet
 *	@rsp: the response queue descriptor holding the RX_PKT message
 *	@gl: the gather list of packet fragments
 *
 *	Process an ingress ethernet packet and deliver it to the stack.
 */
int t4vf_ethrx_handler(struct sge_rspq *rspq, const __be64 *rsp,
		       const struct pkt_gl *gl)
{
	struct sk_buff *skb;
	const struct cpl_rx_pkt *pkt = (void *)&rsp[1];
	bool csum_ok = pkt->csum_calc && !pkt->err_vec;
	struct sge_eth_rxq *rxq = container_of(rspq, struct sge_eth_rxq, rspq);

	/*
	 * If this is a good TCP packet and we have Generic Receive Offload
	 * enabled, handle the packet in the GRO path.
	 */
	if ((pkt->l2info & cpu_to_be32(RXF_TCP)) &&
	    (rspq->netdev->features & NETIF_F_GRO) && csum_ok &&
	    !pkt->ip_frag) {
		do_gro(rxq, gl, pkt);
		return 0;
	}

	/*
	 * Convert the Packet Gather List into an skb.
	 */
	skb = t4vf_pktgl_to_skb(gl, RX_SKB_LEN, RX_PULL_LEN);
	if (unlikely(!skb)) {
		t4vf_pktgl_free(gl);
		rxq->stats.rx_drops++;
		return 0;
	}
	__skb_pull(skb, PKTSHIFT);
	skb->protocol = eth_type_trans(skb, rspq->netdev);
	skb_record_rx_queue(skb, rspq->idx);
	rxq->stats.pkts++;

	if (csum_ok && (rspq->netdev->features & NETIF_F_RXCSUM) &&
	    !pkt->err_vec && (be32_to_cpu(pkt->l2info) & (RXF_UDP|RXF_TCP))) {
		if (!pkt->ip_frag)
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else {
			__sum16 c = (__force __sum16)pkt->csum;
			skb->csum = csum_unfold(c);
			skb->ip_summed = CHECKSUM_COMPLETE;
		}
		rxq->stats.rx_cso++;
	} else
		skb_checksum_none_assert(skb);

	if (pkt->vlan_ex) {
		rxq->stats.vlan_ex++;
		__vlan_hwaccel_put_tag(skb, be16_to_cpu(pkt->vlan));
	}

	netif_receive_skb(skb);

	return 0;
}

/**
 *	is_new_response - check if a response is newly written
 *	@rc: the response control descriptor
 *	@rspq: the response queue
 *
 *	Returns true if a response descriptor contains a yet unprocessed
 *	response.
 */
static inline bool is_new_response(const struct rsp_ctrl *rc,
				   const struct sge_rspq *rspq)
{
	return RSPD_GEN(rc->type_gen) == rspq->gen;
}

/**
 *	restore_rx_bufs - put back a packet's RX buffers
 *	@gl: the packet gather list
 *	@fl: the SGE Free List
 *	@nfrags: how many fragments in @si
 *
 *	Called when we find out that the current packet, @si, can't be
 *	processed right away for some reason.  This is a very rare event and
 *	there's no effort to make this suspension/resumption process
 *	particularly efficient.
 *
 *	We implement the suspension by putting all of the RX buffers associated
 *	with the current packet back on the original Free List.  The buffers
 *	have already been unmapped and are left unmapped, we mark them as
 *	unmapped in order to prevent further unmapping attempts.  (Effectively
 *	this function undoes the series of @unmap_rx_buf calls which were done
 *	to create the current packet's gather list.)  This leaves us ready to
 *	restart processing of the packet the next time we start processing the
 *	RX Queue ...
 */
static void restore_rx_bufs(const struct pkt_gl *gl, struct sge_fl *fl,
			    int frags)
{
	struct rx_sw_desc *sdesc;

	while (frags--) {
		if (fl->cidx == 0)
			fl->cidx = fl->size - 1;
		else
			fl->cidx--;
		sdesc = &fl->sdesc[fl->cidx];
		sdesc->page = gl->frags[frags].page;
		sdesc->dma_addr |= RX_UNMAPPED_BUF;
		fl->avail++;
	}
}

/**
 *	rspq_next - advance to the next entry in a response queue
 *	@rspq: the queue
 *
 *	Updates the state of a response queue to advance it to the next entry.
 */
static inline void rspq_next(struct sge_rspq *rspq)
{
	rspq->cur_desc = (void *)rspq->cur_desc + rspq->iqe_len;
	if (unlikely(++rspq->cidx == rspq->size)) {
		rspq->cidx = 0;
		rspq->gen ^= 1;
		rspq->cur_desc = rspq->desc;
	}
}

/**
 *	process_responses - process responses from an SGE response queue
 *	@rspq: the ingress response queue to process
 *	@budget: how many responses can be processed in this round
 *
 *	Process responses from a Scatter Gather Engine response queue up to
 *	the supplied budget.  Responses include received packets as well as
 *	control messages from firmware or hardware.
 *
 *	Additionally choose the interrupt holdoff time for the next interrupt
 *	on this queue.  If the system is under memory shortage use a fairly
 *	long delay to help recovery.
 */
int process_responses(struct sge_rspq *rspq, int budget)
{
	struct sge_eth_rxq *rxq = container_of(rspq, struct sge_eth_rxq, rspq);
	int budget_left = budget;

	while (likely(budget_left)) {
		int ret, rsp_type;
		const struct rsp_ctrl *rc;

		rc = (void *)rspq->cur_desc + (rspq->iqe_len - sizeof(*rc));
		if (!is_new_response(rc, rspq))
			break;

		/*
		 * Figure out what kind of response we've received from the
		 * SGE.
		 */
		rmb();
		rsp_type = RSPD_TYPE(rc->type_gen);
		if (likely(rsp_type == RSP_TYPE_FLBUF)) {
			struct page_frag *fp;
			struct pkt_gl gl;
			const struct rx_sw_desc *sdesc;
			u32 bufsz, frag;
			u32 len = be32_to_cpu(rc->pldbuflen_qid);

			/*
			 * If we get a "new buffer" message from the SGE we
			 * need to move on to the next Free List buffer.
			 */
			if (len & RSPD_NEWBUF) {
				/*
				 * We get one "new buffer" message when we
				 * first start up a queue so we need to ignore
				 * it when our offset into the buffer is 0.
				 */
				if (likely(rspq->offset > 0)) {
					free_rx_bufs(rspq->adapter, &rxq->fl,
						     1);
					rspq->offset = 0;
				}
				len = RSPD_LEN(len);
			}
			gl.tot_len = len;

			/*
			 * Gather packet fragments.
			 */
			for (frag = 0, fp = gl.frags; /**/; frag++, fp++) {
				BUG_ON(frag >= MAX_SKB_FRAGS);
				BUG_ON(rxq->fl.avail == 0);
				sdesc = &rxq->fl.sdesc[rxq->fl.cidx];
				bufsz = get_buf_size(sdesc);
				fp->page = sdesc->page;
				fp->offset = rspq->offset;
				fp->size = min(bufsz, len);
				len -= fp->size;
				if (!len)
					break;
				unmap_rx_buf(rspq->adapter, &rxq->fl);
			}
			gl.nfrags = frag+1;

			/*
			 * Last buffer remains mapped so explicitly make it
			 * coherent for CPU access and start preloading first
			 * cache line ...
			 */
			dma_sync_single_for_cpu(rspq->adapter->pdev_dev,
						get_buf_addr(sdesc),
						fp->size, DMA_FROM_DEVICE);
			gl.va = (page_address(gl.frags[0].page) +
				 gl.frags[0].offset);
			prefetch(gl.va);

			/*
			 * Hand the new ingress packet to the handler for
			 * this Response Queue.
			 */
			ret = rspq->handler(rspq, rspq->cur_desc, &gl);
			if (likely(ret == 0))
				rspq->offset += ALIGN(fp->size, FL_ALIGN);
			else
				restore_rx_bufs(&gl, &rxq->fl, frag);
		} else if (likely(rsp_type == RSP_TYPE_CPL)) {
			ret = rspq->handler(rspq, rspq->cur_desc, NULL);
		} else {
			WARN_ON(rsp_type > RSP_TYPE_CPL);
			ret = 0;
		}

		if (unlikely(ret)) {
			/*
			 * Couldn't process descriptor, back off for recovery.
			 * We use the SGE's last timer which has the longest
			 * interrupt coalescing value ...
			 */
			const int NOMEM_TIMER_IDX = SGE_NTIMERS-1;
			rspq->next_intr_params =
				QINTR_TIMER_IDX(NOMEM_TIMER_IDX);
			break;
		}

		rspq_next(rspq);
		budget_left--;
	}

	/*
	 * If this is a Response Queue with an associated Free List and
	 * at least two Egress Queue units available in the Free List
	 * for new buffer pointers, refill the Free List.
	 */
	if (rspq->offset >= 0 &&
	    rxq->fl.size - rxq->fl.avail >= 2*FL_PER_EQ_UNIT)
		__refill_fl(rspq->adapter, &rxq->fl);
	return budget - budget_left;
}

/**
 *	napi_rx_handler - the NAPI handler for RX processing
 *	@napi: the napi instance
 *	@budget: how many packets we can process in this round
 *
 *	Handler for new data events when using NAPI.  This does not need any
 *	locking or protection from interrupts as data interrupts are off at
 *	this point and other adapter interrupts do not interfere (the latter
 *	in not a concern at all with MSI-X as non-data interrupts then have
 *	a separate handler).
 */
static int napi_rx_handler(struct napi_struct *napi, int budget)
{
	unsigned int intr_params;
	struct sge_rspq *rspq = container_of(napi, struct sge_rspq, napi);
	int work_done = process_responses(rspq, budget);

	if (likely(work_done < budget)) {
		napi_complete(napi);
		intr_params = rspq->next_intr_params;
		rspq->next_intr_params = rspq->intr_params;
	} else
		intr_params = QINTR_TIMER_IDX(SGE_TIMER_UPD_CIDX);

	if (unlikely(work_done == 0))
		rspq->unhandled_irqs++;

	t4_write_reg(rspq->adapter,
		     T4VF_SGE_BASE_ADDR + SGE_VF_GTS,
		     CIDXINC(work_done) |
		     INGRESSQID((u32)rspq->cntxt_id) |
		     SEINTARM(intr_params));
	return work_done;
}

/*
 * The MSI-X interrupt handler for an SGE response queue for the NAPI case
 * (i.e., response queue serviced by NAPI polling).
 */
irqreturn_t t4vf_sge_intr_msix(int irq, void *cookie)
{
	struct sge_rspq *rspq = cookie;

	napi_schedule(&rspq->napi);
	return IRQ_HANDLED;
}

/*
 * Process the indirect interrupt entries in the interrupt queue and kick off
 * NAPI for each queue that has generated an entry.
 */
static unsigned int process_intrq(struct adapter *adapter)
{
	struct sge *s = &adapter->sge;
	struct sge_rspq *intrq = &s->intrq;
	unsigned int work_done;

	spin_lock(&adapter->sge.intrq_lock);
	for (work_done = 0; ; work_done++) {
		const struct rsp_ctrl *rc;
		unsigned int qid, iq_idx;
		struct sge_rspq *rspq;

		/*
		 * Grab the next response from the interrupt queue and bail
		 * out if it's not a new response.
		 */
		rc = (void *)intrq->cur_desc + (intrq->iqe_len - sizeof(*rc));
		if (!is_new_response(rc, intrq))
			break;

		/*
		 * If the response isn't a forwarded interrupt message issue a
		 * error and go on to the next response message.  This should
		 * never happen ...
		 */
		rmb();
		if (unlikely(RSPD_TYPE(rc->type_gen) != RSP_TYPE_INTR)) {
			dev_err(adapter->pdev_dev,
				"Unexpected INTRQ response type %d\n",
				RSPD_TYPE(rc->type_gen));
			continue;
		}

		/*
		 * Extract the Queue ID from the interrupt message and perform
		 * sanity checking to make sure it really refers to one of our
		 * Ingress Queues which is active and matches the queue's ID.
		 * None of these error conditions should ever happen so we may
		 * want to either make them fatal and/or conditionalized under
		 * DEBUG.
		 */
		qid = RSPD_QID(be32_to_cpu(rc->pldbuflen_qid));
		iq_idx = IQ_IDX(s, qid);
		if (unlikely(iq_idx >= MAX_INGQ)) {
			dev_err(adapter->pdev_dev,
				"Ingress QID %d out of range\n", qid);
			continue;
		}
		rspq = s->ingr_map[iq_idx];
		if (unlikely(rspq == NULL)) {
			dev_err(adapter->pdev_dev,
				"Ingress QID %d RSPQ=NULL\n", qid);
			continue;
		}
		if (unlikely(rspq->abs_id != qid)) {
			dev_err(adapter->pdev_dev,
				"Ingress QID %d refers to RSPQ %d\n",
				qid, rspq->abs_id);
			continue;
		}

		/*
		 * Schedule NAPI processing on the indicated Response Queue
		 * and move on to the next entry in the Forwarded Interrupt
		 * Queue.
		 */
		napi_schedule(&rspq->napi);
		rspq_next(intrq);
	}

	t4_write_reg(adapter, T4VF_SGE_BASE_ADDR + SGE_VF_GTS,
		     CIDXINC(work_done) |
		     INGRESSQID(intrq->cntxt_id) |
		     SEINTARM(intrq->intr_params));

	spin_unlock(&adapter->sge.intrq_lock);

	return work_done;
}

/*
 * The MSI interrupt handler handles data events from SGE response queues as
 * well as error and other async events as they all use the same MSI vector.
 */
irqreturn_t t4vf_intr_msi(int irq, void *cookie)
{
	struct adapter *adapter = cookie;

	process_intrq(adapter);
	return IRQ_HANDLED;
}

/**
 *	t4vf_intr_handler - select the top-level interrupt handler
 *	@adapter: the adapter
 *
 *	Selects the top-level interrupt handler based on the type of interrupts
 *	(MSI-X or MSI).
 */
irq_handler_t t4vf_intr_handler(struct adapter *adapter)
{
	BUG_ON((adapter->flags & (USING_MSIX|USING_MSI)) == 0);
	if (adapter->flags & USING_MSIX)
		return t4vf_sge_intr_msix;
	else
		return t4vf_intr_msi;
}

/**
 *	sge_rx_timer_cb - perform periodic maintenance of SGE RX queues
 *	@data: the adapter
 *
 *	Runs periodically from a timer to perform maintenance of SGE RX queues.
 *
 *	a) Replenishes RX queues that have run out due to memory shortage.
 *	Normally new RX buffers are added when existing ones are consumed but
 *	when out of memory a queue can become empty.  We schedule NAPI to do
 *	the actual refill.
 */
static void sge_rx_timer_cb(unsigned long data)
{
	struct adapter *adapter = (struct adapter *)data;
	struct sge *s = &adapter->sge;
	unsigned int i;

	/*
	 * Scan the "Starving Free Lists" flag array looking for any Free
	 * Lists in need of more free buffers.  If we find one and it's not
	 * being actively polled, then bump its "starving" counter and attempt
	 * to refill it.  If we're successful in adding enough buffers to push
	 * the Free List over the starving threshold, then we can clear its
	 * "starving" status.
	 */
	for (i = 0; i < ARRAY_SIZE(s->starving_fl); i++) {
		unsigned long m;

		for (m = s->starving_fl[i]; m; m &= m - 1) {
			unsigned int id = __ffs(m) + i * BITS_PER_LONG;
			struct sge_fl *fl = s->egr_map[id];

			clear_bit(id, s->starving_fl);
			smp_mb__after_clear_bit();

			/*
			 * Since we are accessing fl without a lock there's a
			 * small probability of a false positive where we
			 * schedule napi but the FL is no longer starving.
			 * No biggie.
			 */
			if (fl_starving(fl)) {
				struct sge_eth_rxq *rxq;

				rxq = container_of(fl, struct sge_eth_rxq, fl);
				if (napi_reschedule(&rxq->rspq.napi))
					fl->starving++;
				else
					set_bit(id, s->starving_fl);
			}
		}
	}

	/*
	 * Reschedule the next scan for starving Free Lists ...
	 */
	mod_timer(&s->rx_timer, jiffies + RX_QCHECK_PERIOD);
}

/**
 *	sge_tx_timer_cb - perform periodic maintenance of SGE Tx queues
 *	@data: the adapter
 *
 *	Runs periodically from a timer to perform maintenance of SGE TX queues.
 *
 *	b) Reclaims completed Tx packets for the Ethernet queues.  Normally
 *	packets are cleaned up by new Tx packets, this timer cleans up packets
 *	when no new packets are being submitted.  This is essential for pktgen,
 *	at least.
 */
static void sge_tx_timer_cb(unsigned long data)
{
	struct adapter *adapter = (struct adapter *)data;
	struct sge *s = &adapter->sge;
	unsigned int i, budget;

	budget = MAX_TIMER_TX_RECLAIM;
	i = s->ethtxq_rover;
	do {
		struct sge_eth_txq *txq = &s->ethtxq[i];

		if (reclaimable(&txq->q) && __netif_tx_trylock(txq->txq)) {
			int avail = reclaimable(&txq->q);

			if (avail > budget)
				avail = budget;

			free_tx_desc(adapter, &txq->q, avail, true);
			txq->q.in_use -= avail;
			__netif_tx_unlock(txq->txq);

			budget -= avail;
			if (!budget)
				break;
		}

		i++;
		if (i >= s->ethqsets)
			i = 0;
	} while (i != s->ethtxq_rover);
	s->ethtxq_rover = i;

	/*
	 * If we found too many reclaimable packets schedule a timer in the
	 * near future to continue where we left off.  Otherwise the next timer
	 * will be at its normal interval.
	 */
	mod_timer(&s->tx_timer, jiffies + (budget ? TX_QCHECK_PERIOD : 2));
}

/**
 *	t4vf_sge_alloc_rxq - allocate an SGE RX Queue
 *	@adapter: the adapter
 *	@rspq: pointer to to the new rxq's Response Queue to be filled in
 *	@iqasynch: if 0, a normal rspq; if 1, an asynchronous event queue
 *	@dev: the network device associated with the new rspq
 *	@intr_dest: MSI-X vector index (overriden in MSI mode)
 *	@fl: pointer to the new rxq's Free List to be filled in
 *	@hnd: the interrupt handler to invoke for the rspq
 */
int t4vf_sge_alloc_rxq(struct adapter *adapter, struct sge_rspq *rspq,
		       bool iqasynch, struct net_device *dev,
		       int intr_dest,
		       struct sge_fl *fl, rspq_handler_t hnd)
{
	struct port_info *pi = netdev_priv(dev);
	struct fw_iq_cmd cmd, rpl;
	int ret, iqandst, flsz = 0;

	/*
	 * If we're using MSI interrupts and we're not initializing the
	 * Forwarded Interrupt Queue itself, then set up this queue for
	 * indirect interrupts to the Forwarded Interrupt Queue.  Obviously
	 * the Forwarded Interrupt Queue must be set up before any other
	 * ingress queue ...
	 */
	if ((adapter->flags & USING_MSI) && rspq != &adapter->sge.intrq) {
		iqandst = SGE_INTRDST_IQ;
		intr_dest = adapter->sge.intrq.abs_id;
	} else
		iqandst = SGE_INTRDST_PCI;

	/*
	 * Allocate the hardware ring for the Response Queue.  The size needs
	 * to be a multiple of 16 which includes the mandatory status entry
	 * (regardless of whether the Status Page capabilities are enabled or
	 * not).
	 */
	rspq->size = roundup(rspq->size, 16);
	rspq->desc = alloc_ring(adapter->pdev_dev, rspq->size, rspq->iqe_len,
				0, &rspq->phys_addr, NULL, 0);
	if (!rspq->desc)
		return -ENOMEM;

	/*
	 * Fill in the Ingress Queue Command.  Note: Ideally this code would
	 * be in t4vf_hw.c but there are so many parameters and dependencies
	 * on our Linux SGE state that we would end up having to pass tons of
	 * parameters.  We'll have to think about how this might be migrated
	 * into OS-independent common code ...
	 */
	memset(&cmd, 0, sizeof(cmd));
	cmd.op_to_vfn = cpu_to_be32(FW_CMD_OP(FW_IQ_CMD) |
				    FW_CMD_REQUEST |
				    FW_CMD_WRITE |
				    FW_CMD_EXEC);
	cmd.alloc_to_len16 = cpu_to_be32(FW_IQ_CMD_ALLOC |
					 FW_IQ_CMD_IQSTART(1) |
					 FW_LEN16(cmd));
	cmd.type_to_iqandstindex =
		cpu_to_be32(FW_IQ_CMD_TYPE(FW_IQ_TYPE_FL_INT_CAP) |
			    FW_IQ_CMD_IQASYNCH(iqasynch) |
			    FW_IQ_CMD_VIID(pi->viid) |
			    FW_IQ_CMD_IQANDST(iqandst) |
			    FW_IQ_CMD_IQANUS(1) |
			    FW_IQ_CMD_IQANUD(SGE_UPDATEDEL_INTR) |
			    FW_IQ_CMD_IQANDSTINDEX(intr_dest));
	cmd.iqdroprss_to_iqesize =
		cpu_to_be16(FW_IQ_CMD_IQPCIECH(pi->port_id) |
			    FW_IQ_CMD_IQGTSMODE |
			    FW_IQ_CMD_IQINTCNTTHRESH(rspq->pktcnt_idx) |
			    FW_IQ_CMD_IQESIZE(ilog2(rspq->iqe_len) - 4));
	cmd.iqsize = cpu_to_be16(rspq->size);
	cmd.iqaddr = cpu_to_be64(rspq->phys_addr);

	if (fl) {
		/*
		 * Allocate the ring for the hardware free list (with space
		 * for its status page) along with the associated software
		 * descriptor ring.  The free list size needs to be a multiple
		 * of the Egress Queue Unit.
		 */
		fl->size = roundup(fl->size, FL_PER_EQ_UNIT);
		fl->desc = alloc_ring(adapter->pdev_dev, fl->size,
				      sizeof(__be64), sizeof(struct rx_sw_desc),
				      &fl->addr, &fl->sdesc, STAT_LEN);
		if (!fl->desc) {
			ret = -ENOMEM;
			goto err;
		}

		/*
		 * Calculate the size of the hardware free list ring plus
		 * Status Page (which the SGE will place after the end of the
		 * free list ring) in Egress Queue Units.
		 */
		flsz = (fl->size / FL_PER_EQ_UNIT +
			STAT_LEN / EQ_UNIT);

		/*
		 * Fill in all the relevant firmware Ingress Queue Command
		 * fields for the free list.
		 */
		cmd.iqns_to_fl0congen =
			cpu_to_be32(
				FW_IQ_CMD_FL0HOSTFCMODE(SGE_HOSTFCMODE_NONE) |
				FW_IQ_CMD_FL0PACKEN |
				FW_IQ_CMD_FL0PADEN);
		cmd.fl0dcaen_to_fl0cidxfthresh =
			cpu_to_be16(
				FW_IQ_CMD_FL0FBMIN(SGE_FETCHBURSTMIN_64B) |
				FW_IQ_CMD_FL0FBMAX(SGE_FETCHBURSTMAX_512B));
		cmd.fl0size = cpu_to_be16(flsz);
		cmd.fl0addr = cpu_to_be64(fl->addr);
	}

	/*
	 * Issue the firmware Ingress Queue Command and extract the results if
	 * it completes successfully.
	 */
	ret = t4vf_wr_mbox(adapter, &cmd, sizeof(cmd), &rpl);
	if (ret)
		goto err;

	netif_napi_add(dev, &rspq->napi, napi_rx_handler, 64);
	rspq->cur_desc = rspq->desc;
	rspq->cidx = 0;
	rspq->gen = 1;
	rspq->next_intr_params = rspq->intr_params;
	rspq->cntxt_id = be16_to_cpu(rpl.iqid);
	rspq->abs_id = be16_to_cpu(rpl.physiqid);
	rspq->size--;			/* subtract status entry */
	rspq->adapter = adapter;
	rspq->netdev = dev;
	rspq->handler = hnd;

	/* set offset to -1 to distinguish ingress queues without FL */
	rspq->offset = fl ? 0 : -1;

	if (fl) {
		fl->cntxt_id = be16_to_cpu(rpl.fl0id);
		fl->avail = 0;
		fl->pend_cred = 0;
		fl->pidx = 0;
		fl->cidx = 0;
		fl->alloc_failed = 0;
		fl->large_alloc_failed = 0;
		fl->starving = 0;
		refill_fl(adapter, fl, fl_cap(fl), GFP_KERNEL);
	}

	return 0;

err:
	/*
	 * An error occurred.  Clean up our partial allocation state and
	 * return the error.
	 */
	if (rspq->desc) {
		dma_free_coherent(adapter->pdev_dev, rspq->size * rspq->iqe_len,
				  rspq->desc, rspq->phys_addr);
		rspq->desc = NULL;
	}
	if (fl && fl->desc) {
		kfree(fl->sdesc);
		fl->sdesc = NULL;
		dma_free_coherent(adapter->pdev_dev, flsz * EQ_UNIT,
				  fl->desc, fl->addr);
		fl->desc = NULL;
	}
	return ret;
}

/**
 *	t4vf_sge_alloc_eth_txq - allocate an SGE Ethernet TX Queue
 *	@adapter: the adapter
 *	@txq: pointer to the new txq to be filled in
 *	@devq: the network TX queue associated with the new txq
 *	@iqid: the relative ingress queue ID to which events relating to
 *		the new txq should be directed
 */
int t4vf_sge_alloc_eth_txq(struct adapter *adapter, struct sge_eth_txq *txq,
			   struct net_device *dev, struct netdev_queue *devq,
			   unsigned int iqid)
{
	int ret, nentries;
	struct fw_eq_eth_cmd cmd, rpl;
	struct port_info *pi = netdev_priv(dev);

	/*
	 * Calculate the size of the hardware TX Queue (including the Status
	 * Page on the end of the TX Queue) in units of TX Descriptors.
	 */
	nentries = txq->q.size + STAT_LEN / sizeof(struct tx_desc);

	/*
	 * Allocate the hardware ring for the TX ring (with space for its
	 * status page) along with the associated software descriptor ring.
	 */
	txq->q.desc = alloc_ring(adapter->pdev_dev, txq->q.size,
				 sizeof(struct tx_desc),
				 sizeof(struct tx_sw_desc),
				 &txq->q.phys_addr, &txq->q.sdesc, STAT_LEN);
	if (!txq->q.desc)
		return -ENOMEM;

	/*
	 * Fill in the Egress Queue Command.  Note: As with the direct use of
	 * the firmware Ingress Queue COmmand above in our RXQ allocation
	 * routine, ideally, this code would be in t4vf_hw.c.  Again, we'll
	 * have to see if there's some reasonable way to parameterize it
	 * into the common code ...
	 */
	memset(&cmd, 0, sizeof(cmd));
	cmd.op_to_vfn = cpu_to_be32(FW_CMD_OP(FW_EQ_ETH_CMD) |
				    FW_CMD_REQUEST |
				    FW_CMD_WRITE |
				    FW_CMD_EXEC);
	cmd.alloc_to_len16 = cpu_to_be32(FW_EQ_ETH_CMD_ALLOC |
					 FW_EQ_ETH_CMD_EQSTART |
					 FW_LEN16(cmd));
	cmd.viid_pkd = cpu_to_be32(FW_EQ_ETH_CMD_VIID(pi->viid));
	cmd.fetchszm_to_iqid =
		cpu_to_be32(FW_EQ_ETH_CMD_HOSTFCMODE(SGE_HOSTFCMODE_STPG) |
			    FW_EQ_ETH_CMD_PCIECHN(pi->port_id) |
			    FW_EQ_ETH_CMD_IQID(iqid));
	cmd.dcaen_to_eqsize =
		cpu_to_be32(FW_EQ_ETH_CMD_FBMIN(SGE_FETCHBURSTMIN_64B) |
			    FW_EQ_ETH_CMD_FBMAX(SGE_FETCHBURSTMAX_512B) |
			    FW_EQ_ETH_CMD_CIDXFTHRESH(SGE_CIDXFLUSHTHRESH_32) |
			    FW_EQ_ETH_CMD_EQSIZE(nentries));
	cmd.eqaddr = cpu_to_be64(txq->q.phys_addr);

	/*
	 * Issue the firmware Egress Queue Command and extract the results if
	 * it completes successfully.
	 */
	ret = t4vf_wr_mbox(adapter, &cmd, sizeof(cmd), &rpl);
	if (ret) {
		/*
		 * The girmware Ingress Queue Command failed for some reason.
		 * Free up our partial allocation state and return the error.
		 */
		kfree(txq->q.sdesc);
		txq->q.sdesc = NULL;
		dma_free_coherent(adapter->pdev_dev,
				  nentries * sizeof(struct tx_desc),
				  txq->q.desc, txq->q.phys_addr);
		txq->q.desc = NULL;
		return ret;
	}

	txq->q.in_use = 0;
	txq->q.cidx = 0;
	txq->q.pidx = 0;
	txq->q.stat = (void *)&txq->q.desc[txq->q.size];
	txq->q.cntxt_id = FW_EQ_ETH_CMD_EQID_GET(be32_to_cpu(rpl.eqid_pkd));
	txq->q.abs_id =
		FW_EQ_ETH_CMD_PHYSEQID_GET(be32_to_cpu(rpl.physeqid_pkd));
	txq->txq = devq;
	txq->tso = 0;
	txq->tx_cso = 0;
	txq->vlan_ins = 0;
	txq->q.stops = 0;
	txq->q.restarts = 0;
	txq->mapping_err = 0;
	return 0;
}

/*
 * Free the DMA map resources associated with a TX queue.
 */
static void free_txq(struct adapter *adapter, struct sge_txq *tq)
{
	dma_free_coherent(adapter->pdev_dev,
			  tq->size * sizeof(*tq->desc) + STAT_LEN,
			  tq->desc, tq->phys_addr);
	tq->cntxt_id = 0;
	tq->sdesc = NULL;
	tq->desc = NULL;
}

/*
 * Free the resources associated with a response queue (possibly including a
 * free list).
 */
static void free_rspq_fl(struct adapter *adapter, struct sge_rspq *rspq,
			 struct sge_fl *fl)
{
	unsigned int flid = fl ? fl->cntxt_id : 0xffff;

	t4vf_iq_free(adapter, FW_IQ_TYPE_FL_INT_CAP,
		     rspq->cntxt_id, flid, 0xffff);
	dma_free_coherent(adapter->pdev_dev, (rspq->size + 1) * rspq->iqe_len,
			  rspq->desc, rspq->phys_addr);
	netif_napi_del(&rspq->napi);
	rspq->netdev = NULL;
	rspq->cntxt_id = 0;
	rspq->abs_id = 0;
	rspq->desc = NULL;

	if (fl) {
		free_rx_bufs(adapter, fl, fl->avail);
		dma_free_coherent(adapter->pdev_dev,
				  fl->size * sizeof(*fl->desc) + STAT_LEN,
				  fl->desc, fl->addr);
		kfree(fl->sdesc);
		fl->sdesc = NULL;
		fl->cntxt_id = 0;
		fl->desc = NULL;
	}
}

/**
 *	t4vf_free_sge_resources - free SGE resources
 *	@adapter: the adapter
 *
 *	Frees resources used by the SGE queue sets.
 */
void t4vf_free_sge_resources(struct adapter *adapter)
{
	struct sge *s = &adapter->sge;
	struct sge_eth_rxq *rxq = s->ethrxq;
	struct sge_eth_txq *txq = s->ethtxq;
	struct sge_rspq *evtq = &s->fw_evtq;
	struct sge_rspq *intrq = &s->intrq;
	int qs;

	for (qs = 0; qs < adapter->sge.ethqsets; qs++, rxq++, txq++) {
		if (rxq->rspq.desc)
			free_rspq_fl(adapter, &rxq->rspq, &rxq->fl);
		if (txq->q.desc) {
			t4vf_eth_eq_free(adapter, txq->q.cntxt_id);
			free_tx_desc(adapter, &txq->q, txq->q.in_use, true);
			kfree(txq->q.sdesc);
			free_txq(adapter, &txq->q);
		}
	}
	if (evtq->desc)
		free_rspq_fl(adapter, evtq, NULL);
	if (intrq->desc)
		free_rspq_fl(adapter, intrq, NULL);
}

/**
 *	t4vf_sge_start - enable SGE operation
 *	@adapter: the adapter
 *
 *	Start tasklets and timers associated with the DMA engine.
 */
void t4vf_sge_start(struct adapter *adapter)
{
	adapter->sge.ethtxq_rover = 0;
	mod_timer(&adapter->sge.rx_timer, jiffies + RX_QCHECK_PERIOD);
	mod_timer(&adapter->sge.tx_timer, jiffies + TX_QCHECK_PERIOD);
}

/**
 *	t4vf_sge_stop - disable SGE operation
 *	@adapter: the adapter
 *
 *	Stop tasklets and timers associated with the DMA engine.  Note that
 *	this is effective only if measures have been taken to disable any HW
 *	events that may restart them.
 */
void t4vf_sge_stop(struct adapter *adapter)
{
	struct sge *s = &adapter->sge;

	if (s->rx_timer.function)
		del_timer_sync(&s->rx_timer);
	if (s->tx_timer.function)
		del_timer_sync(&s->tx_timer);
}

/**
 *	t4vf_sge_init - initialize SGE
 *	@adapter: the adapter
 *
 *	Performs SGE initialization needed every time after a chip reset.
 *	We do not initialize any of the queue sets here, instead the driver
 *	top-level must request those individually.  We also do not enable DMA
 *	here, that should be done after the queues have been set up.
 */
int t4vf_sge_init(struct adapter *adapter)
{
	struct sge_params *sge_params = &adapter->params.sge;
	u32 fl0 = sge_params->sge_fl_buffer_size[0];
	u32 fl1 = sge_params->sge_fl_buffer_size[1];
	struct sge *s = &adapter->sge;

	/*
	 * Start by vetting the basic SGE parameters which have been set up by
	 * the Physical Function Driver.  Ideally we should be able to deal
	 * with _any_ configuration.  Practice is different ...
	 */
	if (fl0 != PAGE_SIZE || (fl1 != 0 && fl1 <= fl0)) {
		dev_err(adapter->pdev_dev, "bad SGE FL buffer sizes [%d, %d]\n",
			fl0, fl1);
		return -EINVAL;
	}
	if ((sge_params->sge_control & RXPKTCPLMODE) == 0) {
		dev_err(adapter->pdev_dev, "bad SGE CPL MODE\n");
		return -EINVAL;
	}

	/*
	 * Now translate the adapter parameters into our internal forms.
	 */
	if (fl1)
		FL_PG_ORDER = ilog2(fl1) - PAGE_SHIFT;
	STAT_LEN = ((sge_params->sge_control & EGRSTATUSPAGESIZE) ? 128 : 64);
	PKTSHIFT = PKTSHIFT_GET(sge_params->sge_control);
	FL_ALIGN = 1 << (INGPADBOUNDARY_GET(sge_params->sge_control) +
			 SGE_INGPADBOUNDARY_SHIFT);

	/*
	 * Set up tasklet timers.
	 */
	setup_timer(&s->rx_timer, sge_rx_timer_cb, (unsigned long)adapter);
	setup_timer(&s->tx_timer, sge_tx_timer_cb, (unsigned long)adapter);

	/*
	 * Initialize Forwarded Interrupt Queue lock.
	 */
	spin_lock_init(&s->intrq_lock);

	return 0;
}
