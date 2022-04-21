// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)

#include <linux/bpf_trace.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/filter.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include "funeth_txrx.h"
#include "funeth.h"
#include "fun_queue.h"

#define CREATE_TRACE_POINTS
#include "funeth_trace.h"

/* Given the device's max supported MTU and pages of at least 4KB a packet can
 * be scattered into at most 4 buffers.
 */
#define RX_MAX_FRAGS 4

/* Per packet headroom in non-XDP mode. Present only for 1-frag packets. */
#define FUN_RX_HEADROOM (NET_SKB_PAD + NET_IP_ALIGN)

/* We try to reuse pages for our buffers. To avoid frequent page ref writes we
 * take EXTRA_PAGE_REFS references at once and then hand them out one per packet
 * occupying the buffer.
 */
#define EXTRA_PAGE_REFS 1000000
#define MIN_PAGE_REFS 1000

enum {
	FUN_XDP_FLUSH_REDIR = 1,
	FUN_XDP_FLUSH_TX = 2,
};

/* See if a page is running low on refs we are holding and if so take more. */
static void refresh_refs(struct funeth_rxbuf *buf)
{
	if (unlikely(buf->pg_refs < MIN_PAGE_REFS)) {
		buf->pg_refs += EXTRA_PAGE_REFS;
		page_ref_add(buf->page, EXTRA_PAGE_REFS);
	}
}

/* Offer a buffer to the Rx buffer cache. The cache will hold the buffer if its
 * page is worth retaining and there's room for it. Otherwise the page is
 * unmapped and our references released.
 */
static void cache_offer(struct funeth_rxq *q, const struct funeth_rxbuf *buf)
{
	struct funeth_rx_cache *c = &q->cache;

	if (c->prod_cnt - c->cons_cnt <= c->mask && buf->node == numa_mem_id()) {
		c->bufs[c->prod_cnt & c->mask] = *buf;
		c->prod_cnt++;
	} else {
		dma_unmap_page_attrs(q->dma_dev, buf->dma_addr, PAGE_SIZE,
				     DMA_FROM_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
		__page_frag_cache_drain(buf->page, buf->pg_refs);
	}
}

/* Get a page from the Rx buffer cache. We only consider the next available
 * page and return it if we own all its references.
 */
static bool cache_get(struct funeth_rxq *q, struct funeth_rxbuf *rb)
{
	struct funeth_rx_cache *c = &q->cache;
	struct funeth_rxbuf *buf;

	if (c->prod_cnt == c->cons_cnt)
		return false;             /* empty cache */

	buf = &c->bufs[c->cons_cnt & c->mask];
	if (page_ref_count(buf->page) == buf->pg_refs) {
		dma_sync_single_for_device(q->dma_dev, buf->dma_addr,
					   PAGE_SIZE, DMA_FROM_DEVICE);
		*rb = *buf;
		buf->page = NULL;
		refresh_refs(rb);
		c->cons_cnt++;
		return true;
	}

	/* Page can't be reused. If the cache is full drop this page. */
	if (c->prod_cnt - c->cons_cnt > c->mask) {
		dma_unmap_page_attrs(q->dma_dev, buf->dma_addr, PAGE_SIZE,
				     DMA_FROM_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
		__page_frag_cache_drain(buf->page, buf->pg_refs);
		buf->page = NULL;
		c->cons_cnt++;
	}
	return false;
}

/* Allocate and DMA-map a page for receive. */
static int funeth_alloc_page(struct funeth_rxq *q, struct funeth_rxbuf *rb,
			     int node, gfp_t gfp)
{
	struct page *p;

	if (cache_get(q, rb))
		return 0;

	p = __alloc_pages_node(node, gfp | __GFP_NOWARN, 0);
	if (unlikely(!p))
		return -ENOMEM;

	rb->dma_addr = dma_map_page(q->dma_dev, p, 0, PAGE_SIZE,
				    DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(q->dma_dev, rb->dma_addr))) {
		FUN_QSTAT_INC(q, rx_map_err);
		__free_page(p);
		return -ENOMEM;
	}

	FUN_QSTAT_INC(q, rx_page_alloc);

	rb->page = p;
	rb->pg_refs = 1;
	refresh_refs(rb);
	rb->node = page_is_pfmemalloc(p) ? -1 : page_to_nid(p);
	return 0;
}

static void funeth_free_page(struct funeth_rxq *q, struct funeth_rxbuf *rb)
{
	if (rb->page) {
		dma_unmap_page(q->dma_dev, rb->dma_addr, PAGE_SIZE,
			       DMA_FROM_DEVICE);
		__page_frag_cache_drain(rb->page, rb->pg_refs);
		rb->page = NULL;
	}
}

/* Run the XDP program assigned to an Rx queue.
 * Return %NULL if the buffer is consumed, or the virtual address of the packet
 * to turn into an skb.
 */
static void *fun_run_xdp(struct funeth_rxq *q, skb_frag_t *frags, void *buf_va,
			 int ref_ok, struct funeth_txq *xdp_q)
{
	struct bpf_prog *xdp_prog;
	struct xdp_buff xdp;
	u32 act;

	/* VA includes the headroom, frag size includes headroom + tailroom */
	xdp_init_buff(&xdp, ALIGN(skb_frag_size(frags), FUN_EPRQ_PKT_ALIGN),
		      &q->xdp_rxq);
	xdp_prepare_buff(&xdp, buf_va, FUN_XDP_HEADROOM, skb_frag_size(frags) -
			 (FUN_RX_TAILROOM + FUN_XDP_HEADROOM), false);

	xdp_prog = READ_ONCE(q->xdp_prog);
	act = bpf_prog_run_xdp(xdp_prog, &xdp);

	switch (act) {
	case XDP_PASS:
		/* remove headroom, which may not be FUN_XDP_HEADROOM now */
		skb_frag_size_set(frags, xdp.data_end - xdp.data);
		skb_frag_off_add(frags, xdp.data - xdp.data_hard_start);
		goto pass;
	case XDP_TX:
		if (unlikely(!ref_ok))
			goto pass;
		if (!fun_xdp_tx(xdp_q, xdp.data, xdp.data_end - xdp.data))
			goto xdp_error;
		FUN_QSTAT_INC(q, xdp_tx);
		q->xdp_flush |= FUN_XDP_FLUSH_TX;
		break;
	case XDP_REDIRECT:
		if (unlikely(!ref_ok))
			goto pass;
		if (unlikely(xdp_do_redirect(q->netdev, &xdp, xdp_prog)))
			goto xdp_error;
		FUN_QSTAT_INC(q, xdp_redir);
		q->xdp_flush |= FUN_XDP_FLUSH_REDIR;
		break;
	default:
		bpf_warn_invalid_xdp_action(q->netdev, xdp_prog, act);
		fallthrough;
	case XDP_ABORTED:
		trace_xdp_exception(q->netdev, xdp_prog, act);
xdp_error:
		q->cur_buf->pg_refs++; /* return frags' page reference */
		FUN_QSTAT_INC(q, xdp_err);
		break;
	case XDP_DROP:
		q->cur_buf->pg_refs++;
		FUN_QSTAT_INC(q, xdp_drops);
		break;
	}
	return NULL;

pass:
	return xdp.data;
}

/* A CQE contains a fixed completion structure along with optional metadata and
 * even packet data. Given the start address of a CQE return the start of the
 * contained fixed structure, which lies at the end.
 */
static const void *cqe_to_info(const void *cqe)
{
	return cqe + FUNETH_CQE_INFO_OFFSET;
}

/* The inverse of cqe_to_info(). */
static const void *info_to_cqe(const void *cqe_info)
{
	return cqe_info - FUNETH_CQE_INFO_OFFSET;
}

/* Return the type of hash provided by the device based on the L3 and L4
 * protocols it parsed for the packet.
 */
static enum pkt_hash_types cqe_to_pkt_hash_type(u16 pkt_parse)
{
	static const enum pkt_hash_types htype_map[] = {
		PKT_HASH_TYPE_NONE, PKT_HASH_TYPE_L3,
		PKT_HASH_TYPE_NONE, PKT_HASH_TYPE_L4,
		PKT_HASH_TYPE_NONE, PKT_HASH_TYPE_L3,
		PKT_HASH_TYPE_NONE, PKT_HASH_TYPE_L3
	};
	u16 key;

	/* Build the key from the TCP/UDP and IP/IPv6 bits */
	key = ((pkt_parse >> FUN_ETH_RX_CV_OL4_PROT_S) & 6) |
	      ((pkt_parse >> (FUN_ETH_RX_CV_OL3_PROT_S + 1)) & 1);

	return htype_map[key];
}

/* Each received packet can be scattered across several Rx buffers or can
 * share a buffer with previously received packets depending on the buffer
 * and packet sizes and the room available in the most recently used buffer.
 *
 * The rules are:
 * - If the buffer at the head of an RQ has not been used it gets (part of) the
 *   next incoming packet.
 * - Otherwise, if the packet fully fits in the buffer's remaining space the
 *   packet is written there.
 * - Otherwise, the packet goes into the next Rx buffer.
 *
 * This function returns the Rx buffer for a packet or fragment thereof of the
 * given length. If it isn't @buf it either recycles or frees that buffer
 * before advancing the queue to the next buffer.
 *
 * If called repeatedly with the remaining length of a packet it will walk
 * through all the buffers containing the packet.
 */
static struct funeth_rxbuf *
get_buf(struct funeth_rxq *q, struct funeth_rxbuf *buf, unsigned int len)
{
	if (q->buf_offset + len <= PAGE_SIZE || !q->buf_offset)
		return buf;            /* @buf holds (part of) the packet */

	/* The packet occupies part of the next buffer. Move there after
	 * replenishing the current buffer slot either with the spare page or
	 * by reusing the slot's existing page. Note that if a spare page isn't
	 * available and the current packet occupies @buf it is a multi-frag
	 * packet that will be dropped leaving @buf available for reuse.
	 */
	if ((page_ref_count(buf->page) == buf->pg_refs &&
	     buf->node == numa_mem_id()) || !q->spare_buf.page) {
		dma_sync_single_for_device(q->dma_dev, buf->dma_addr,
					   PAGE_SIZE, DMA_FROM_DEVICE);
		refresh_refs(buf);
	} else {
		cache_offer(q, buf);
		*buf = q->spare_buf;
		q->spare_buf.page = NULL;
		q->rqes[q->rq_cons & q->rq_mask] =
			FUN_EPRQ_RQBUF_INIT(buf->dma_addr);
	}
	q->buf_offset = 0;
	q->rq_cons++;
	return &q->bufs[q->rq_cons & q->rq_mask];
}

/* Gather the page fragments making up the first Rx packet on @q. Its total
 * length @tot_len includes optional head- and tail-rooms.
 *
 * Return 0 if the device retains ownership of at least some of the pages.
 * In this case the caller may only copy the packet.
 *
 * A non-zero return value gives the caller permission to use references to the
 * pages, e.g., attach them to skbs. Additionally, if the value is <0 at least
 * one of the pages is PF_MEMALLOC.
 *
 * Regardless of outcome the caller is granted a reference to each of the pages.
 */
static int fun_gather_pkt(struct funeth_rxq *q, unsigned int tot_len,
			  skb_frag_t *frags)
{
	struct funeth_rxbuf *buf = q->cur_buf;
	unsigned int frag_len;
	int ref_ok = 1;

	for (;;) {
		buf = get_buf(q, buf, tot_len);

		/* We always keep the RQ full of buffers so before we can give
		 * one of our pages to the stack we require that we can obtain
		 * a replacement page. If we can't the packet will either be
		 * copied or dropped so we can retain ownership of the page and
		 * reuse it.
		 */
		if (!q->spare_buf.page &&
		    funeth_alloc_page(q, &q->spare_buf, numa_mem_id(),
				      GFP_ATOMIC | __GFP_MEMALLOC))
			ref_ok = 0;

		frag_len = min_t(unsigned int, tot_len,
				 PAGE_SIZE - q->buf_offset);
		dma_sync_single_for_cpu(q->dma_dev,
					buf->dma_addr + q->buf_offset,
					frag_len, DMA_FROM_DEVICE);
		buf->pg_refs--;
		if (ref_ok)
			ref_ok |= buf->node;

		__skb_frag_set_page(frags, buf->page);
		skb_frag_off_set(frags, q->buf_offset);
		skb_frag_size_set(frags++, frag_len);

		tot_len -= frag_len;
		if (!tot_len)
			break;

		q->buf_offset = PAGE_SIZE;
	}
	q->buf_offset = ALIGN(q->buf_offset + frag_len, FUN_EPRQ_PKT_ALIGN);
	q->cur_buf = buf;
	return ref_ok;
}

static bool rx_hwtstamp_enabled(const struct net_device *dev)
{
	const struct funeth_priv *d = netdev_priv(dev);

	return d->hwtstamp_cfg.rx_filter == HWTSTAMP_FILTER_ALL;
}

/* Advance the CQ pointers and phase tag to the next CQE. */
static void advance_cq(struct funeth_rxq *q)
{
	if (unlikely(q->cq_head == q->cq_mask)) {
		q->cq_head = 0;
		q->phase ^= 1;
		q->next_cqe_info = cqe_to_info(q->cqes);
	} else {
		q->cq_head++;
		q->next_cqe_info += FUNETH_CQE_SIZE;
	}
	prefetch(q->next_cqe_info);
}

/* Process the packet represented by the head CQE of @q. Gather the packet's
 * fragments, run it through the optional XDP program, and if needed construct
 * an skb and pass it to the stack.
 */
static void fun_handle_cqe_pkt(struct funeth_rxq *q, struct funeth_txq *xdp_q)
{
	const struct fun_eth_cqe *rxreq = info_to_cqe(q->next_cqe_info);
	unsigned int i, tot_len, pkt_len = be32_to_cpu(rxreq->pkt_len);
	struct net_device *ndev = q->netdev;
	skb_frag_t frags[RX_MAX_FRAGS];
	struct skb_shared_info *si;
	unsigned int headroom;
	gro_result_t gro_res;
	struct sk_buff *skb;
	int ref_ok;
	void *va;
	u16 cv;

	u64_stats_update_begin(&q->syncp);
	q->stats.rx_pkts++;
	q->stats.rx_bytes += pkt_len;
	u64_stats_update_end(&q->syncp);

	advance_cq(q);

	/* account for head- and tail-room, present only for 1-buffer packets */
	tot_len = pkt_len;
	headroom = be16_to_cpu(rxreq->headroom);
	if (likely(headroom))
		tot_len += FUN_RX_TAILROOM + headroom;

	ref_ok = fun_gather_pkt(q, tot_len, frags);
	va = skb_frag_address(frags);
	if (xdp_q && headroom == FUN_XDP_HEADROOM) {
		va = fun_run_xdp(q, frags, va, ref_ok, xdp_q);
		if (!va)
			return;
		headroom = 0;   /* XDP_PASS trims it */
	}
	if (unlikely(!ref_ok))
		goto no_mem;

	if (likely(headroom)) {
		/* headroom is either FUN_RX_HEADROOM or FUN_XDP_HEADROOM */
		prefetch(va + headroom);
		skb = napi_build_skb(va, ALIGN(tot_len, FUN_EPRQ_PKT_ALIGN));
		if (unlikely(!skb))
			goto no_mem;

		skb_reserve(skb, headroom);
		__skb_put(skb, pkt_len);
		skb->protocol = eth_type_trans(skb, ndev);
	} else {
		prefetch(va);
		skb = napi_get_frags(q->napi);
		if (unlikely(!skb))
			goto no_mem;

		if (ref_ok < 0)
			skb->pfmemalloc = 1;

		si = skb_shinfo(skb);
		si->nr_frags = rxreq->nsgl;
		for (i = 0; i < si->nr_frags; i++)
			si->frags[i] = frags[i];

		skb->len = pkt_len;
		skb->data_len = pkt_len;
		skb->truesize += round_up(pkt_len, FUN_EPRQ_PKT_ALIGN);
	}

	skb_record_rx_queue(skb, q->qidx);
	cv = be16_to_cpu(rxreq->pkt_cv);
	if (likely((q->netdev->features & NETIF_F_RXHASH) && rxreq->hash))
		skb_set_hash(skb, be32_to_cpu(rxreq->hash),
			     cqe_to_pkt_hash_type(cv));
	if (likely((q->netdev->features & NETIF_F_RXCSUM) && rxreq->csum)) {
		FUN_QSTAT_INC(q, rx_cso);
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb->csum_level = be16_to_cpu(rxreq->csum) - 1;
	}
	if (unlikely(rx_hwtstamp_enabled(q->netdev)))
		skb_hwtstamps(skb)->hwtstamp = be64_to_cpu(rxreq->timestamp);

	trace_funeth_rx(q, rxreq->nsgl, pkt_len, skb->hash, cv);

	gro_res = skb->data_len ? napi_gro_frags(q->napi) :
				  napi_gro_receive(q->napi, skb);
	if (gro_res == GRO_MERGED || gro_res == GRO_MERGED_FREE)
		FUN_QSTAT_INC(q, gro_merged);
	else if (gro_res == GRO_HELD)
		FUN_QSTAT_INC(q, gro_pkts);
	return;

no_mem:
	FUN_QSTAT_INC(q, rx_mem_drops);

	/* Release the references we've been granted for the frag pages.
	 * We return the ref of the last frag and free the rest.
	 */
	q->cur_buf->pg_refs++;
	for (i = 0; i < rxreq->nsgl - 1; i++)
		__free_page(skb_frag_page(frags + i));
}

/* Return 0 if the phase tag of the CQE at the CQ's head matches expectations
 * indicating the CQE is new.
 */
static u16 cqe_phase_mismatch(const struct fun_cqe_info *ci, u16 phase)
{
	u16 sf_p = be16_to_cpu(ci->sf_p);

	return (sf_p & 1) ^ phase;
}

/* Walk through a CQ identifying and processing fresh CQEs up to the given
 * budget. Return the remaining budget.
 */
static int fun_process_cqes(struct funeth_rxq *q, int budget)
{
	struct funeth_priv *fp = netdev_priv(q->netdev);
	struct funeth_txq **xdpqs, *xdp_q = NULL;

	xdpqs = rcu_dereference_bh(fp->xdpqs);
	if (xdpqs)
		xdp_q = xdpqs[smp_processor_id()];

	while (budget && !cqe_phase_mismatch(q->next_cqe_info, q->phase)) {
		/* access other descriptor fields after the phase check */
		dma_rmb();

		fun_handle_cqe_pkt(q, xdp_q);
		budget--;
	}

	if (unlikely(q->xdp_flush)) {
		if (q->xdp_flush & FUN_XDP_FLUSH_TX)
			fun_txq_wr_db(xdp_q);
		if (q->xdp_flush & FUN_XDP_FLUSH_REDIR)
			xdp_do_flush();
		q->xdp_flush = 0;
	}

	return budget;
}

/* NAPI handler for Rx queues. Calls the CQE processing loop and writes RQ/CQ
 * doorbells as needed.
 */
int fun_rxq_napi_poll(struct napi_struct *napi, int budget)
{
	struct fun_irq *irq = container_of(napi, struct fun_irq, napi);
	struct funeth_rxq *q = irq->rxq;
	int work_done = budget - fun_process_cqes(q, budget);
	u32 cq_db_val = q->cq_head;

	if (unlikely(work_done >= budget))
		FUN_QSTAT_INC(q, rx_budget);
	else if (napi_complete_done(napi, work_done))
		cq_db_val |= q->irq_db_val;

	/* check whether to post new Rx buffers */
	if (q->rq_cons - q->rq_cons_db >= q->rq_db_thres) {
		u64_stats_update_begin(&q->syncp);
		q->stats.rx_bufs += q->rq_cons - q->rq_cons_db;
		u64_stats_update_end(&q->syncp);
		q->rq_cons_db = q->rq_cons;
		writel((q->rq_cons - 1) & q->rq_mask, q->rq_db);
	}

	writel(cq_db_val, q->cq_db);
	return work_done;
}

/* Free the Rx buffers of an Rx queue. */
static void fun_rxq_free_bufs(struct funeth_rxq *q)
{
	struct funeth_rxbuf *b = q->bufs;
	unsigned int i;

	for (i = 0; i <= q->rq_mask; i++, b++)
		funeth_free_page(q, b);

	funeth_free_page(q, &q->spare_buf);
	q->cur_buf = NULL;
}

/* Initially provision an Rx queue with Rx buffers. */
static int fun_rxq_alloc_bufs(struct funeth_rxq *q, int node)
{
	struct funeth_rxbuf *b = q->bufs;
	unsigned int i;

	for (i = 0; i <= q->rq_mask; i++, b++) {
		if (funeth_alloc_page(q, b, node, GFP_KERNEL)) {
			fun_rxq_free_bufs(q);
			return -ENOMEM;
		}
		q->rqes[i] = FUN_EPRQ_RQBUF_INIT(b->dma_addr);
	}
	q->cur_buf = q->bufs;
	return 0;
}

/* Initialize a used-buffer cache of the given depth. */
static int fun_rxq_init_cache(struct funeth_rx_cache *c, unsigned int depth,
			      int node)
{
	c->mask = depth - 1;
	c->bufs = kvzalloc_node(depth * sizeof(*c->bufs), GFP_KERNEL, node);
	return c->bufs ? 0 : -ENOMEM;
}

/* Deallocate an Rx queue's used-buffer cache and its contents. */
static void fun_rxq_free_cache(struct funeth_rxq *q)
{
	struct funeth_rxbuf *b = q->cache.bufs;
	unsigned int i;

	for (i = 0; i <= q->cache.mask; i++, b++)
		funeth_free_page(q, b);

	kvfree(q->cache.bufs);
	q->cache.bufs = NULL;
}

int fun_rxq_set_bpf(struct funeth_rxq *q, struct bpf_prog *prog)
{
	struct funeth_priv *fp = netdev_priv(q->netdev);
	struct fun_admin_epcq_req cmd;
	u16 headroom;
	int err;

	headroom = prog ? FUN_XDP_HEADROOM : FUN_RX_HEADROOM;
	if (headroom != q->headroom) {
		cmd.common = FUN_ADMIN_REQ_COMMON_INIT2(FUN_ADMIN_OP_EPCQ,
							sizeof(cmd));
		cmd.u.modify =
			FUN_ADMIN_EPCQ_MODIFY_REQ_INIT(FUN_ADMIN_SUBOP_MODIFY,
						       0, q->hw_cqid, headroom);
		err = fun_submit_admin_sync_cmd(fp->fdev, &cmd.common, NULL, 0,
						0);
		if (err)
			return err;
		q->headroom = headroom;
	}

	WRITE_ONCE(q->xdp_prog, prog);
	return 0;
}

/* Create an Rx queue, allocating the host memory it needs. */
static struct funeth_rxq *fun_rxq_create_sw(struct net_device *dev,
					    unsigned int qidx,
					    unsigned int ncqe,
					    unsigned int nrqe,
					    struct fun_irq *irq)
{
	struct funeth_priv *fp = netdev_priv(dev);
	struct funeth_rxq *q;
	int err = -ENOMEM;
	int numa_node;

	numa_node = fun_irq_node(irq);
	q = kzalloc_node(sizeof(*q), GFP_KERNEL, numa_node);
	if (!q)
		goto err;

	q->qidx = qidx;
	q->netdev = dev;
	q->cq_mask = ncqe - 1;
	q->rq_mask = nrqe - 1;
	q->numa_node = numa_node;
	q->rq_db_thres = nrqe / 4;
	u64_stats_init(&q->syncp);
	q->dma_dev = &fp->pdev->dev;

	q->rqes = fun_alloc_ring_mem(q->dma_dev, nrqe, sizeof(*q->rqes),
				     sizeof(*q->bufs), false, numa_node,
				     &q->rq_dma_addr, (void **)&q->bufs, NULL);
	if (!q->rqes)
		goto free_q;

	q->cqes = fun_alloc_ring_mem(q->dma_dev, ncqe, FUNETH_CQE_SIZE, 0,
				     false, numa_node, &q->cq_dma_addr, NULL,
				     NULL);
	if (!q->cqes)
		goto free_rqes;

	err = fun_rxq_init_cache(&q->cache, nrqe, numa_node);
	if (err)
		goto free_cqes;

	err = fun_rxq_alloc_bufs(q, numa_node);
	if (err)
		goto free_cache;

	q->stats.rx_bufs = q->rq_mask;
	q->init_state = FUN_QSTATE_INIT_SW;
	return q;

free_cache:
	fun_rxq_free_cache(q);
free_cqes:
	dma_free_coherent(q->dma_dev, ncqe * FUNETH_CQE_SIZE, q->cqes,
			  q->cq_dma_addr);
free_rqes:
	fun_free_ring_mem(q->dma_dev, nrqe, sizeof(*q->rqes), false, q->rqes,
			  q->rq_dma_addr, q->bufs);
free_q:
	kfree(q);
err:
	netdev_err(dev, "Unable to allocate memory for Rx queue %u\n", qidx);
	return ERR_PTR(err);
}

static void fun_rxq_free_sw(struct funeth_rxq *q)
{
	struct funeth_priv *fp = netdev_priv(q->netdev);

	fun_rxq_free_cache(q);
	fun_rxq_free_bufs(q);
	fun_free_ring_mem(q->dma_dev, q->rq_mask + 1, sizeof(*q->rqes), false,
			  q->rqes, q->rq_dma_addr, q->bufs);
	dma_free_coherent(q->dma_dev, (q->cq_mask + 1) * FUNETH_CQE_SIZE,
			  q->cqes, q->cq_dma_addr);

	/* Before freeing the queue transfer key counters to the device. */
	fp->rx_packets += q->stats.rx_pkts;
	fp->rx_bytes   += q->stats.rx_bytes;
	fp->rx_dropped += q->stats.rx_map_err + q->stats.rx_mem_drops;

	kfree(q);
}

/* Create an Rx queue's resources on the device. */
int fun_rxq_create_dev(struct funeth_rxq *q, struct fun_irq *irq)
{
	struct funeth_priv *fp = netdev_priv(q->netdev);
	unsigned int ncqe = q->cq_mask + 1;
	unsigned int nrqe = q->rq_mask + 1;
	int err;

	err = xdp_rxq_info_reg(&q->xdp_rxq, q->netdev, q->qidx,
			       irq->napi.napi_id);
	if (err)
		goto out;

	err = xdp_rxq_info_reg_mem_model(&q->xdp_rxq, MEM_TYPE_PAGE_SHARED,
					 NULL);
	if (err)
		goto xdp_unreg;

	q->phase = 1;
	q->irq_cnt = 0;
	q->cq_head = 0;
	q->rq_cons = 0;
	q->rq_cons_db = 0;
	q->buf_offset = 0;
	q->napi = &irq->napi;
	q->irq_db_val = fp->cq_irq_db;
	q->next_cqe_info = cqe_to_info(q->cqes);

	q->xdp_prog = fp->xdp_prog;
	q->headroom = fp->xdp_prog ? FUN_XDP_HEADROOM : FUN_RX_HEADROOM;

	err = fun_sq_create(fp->fdev, FUN_ADMIN_RES_CREATE_FLAG_ALLOCATOR |
			    FUN_ADMIN_EPSQ_CREATE_FLAG_RQ, 0,
			    FUN_HCI_ID_INVALID, 0, nrqe, q->rq_dma_addr, 0, 0,
			    0, 0, fp->fdev->kern_end_qid, PAGE_SHIFT,
			    &q->hw_sqid, &q->rq_db);
	if (err)
		goto xdp_unreg;

	err = fun_cq_create(fp->fdev, FUN_ADMIN_RES_CREATE_FLAG_ALLOCATOR |
			    FUN_ADMIN_EPCQ_CREATE_FLAG_RQ, 0,
			    q->hw_sqid, ilog2(FUNETH_CQE_SIZE), ncqe,
			    q->cq_dma_addr, q->headroom, FUN_RX_TAILROOM, 0, 0,
			    irq->irq_idx, 0, fp->fdev->kern_end_qid,
			    &q->hw_cqid, &q->cq_db);
	if (err)
		goto free_rq;

	irq->rxq = q;
	writel(q->rq_mask, q->rq_db);
	q->init_state = FUN_QSTATE_INIT_FULL;

	netif_info(fp, ifup, q->netdev,
		   "Rx queue %u, depth %u/%u, HW qid %u/%u, IRQ idx %u, node %d, headroom %u\n",
		   q->qidx, ncqe, nrqe, q->hw_cqid, q->hw_sqid, irq->irq_idx,
		   q->numa_node, q->headroom);
	return 0;

free_rq:
	fun_destroy_sq(fp->fdev, q->hw_sqid);
xdp_unreg:
	xdp_rxq_info_unreg(&q->xdp_rxq);
out:
	netdev_err(q->netdev,
		   "Failed to create Rx queue %u on device, error %d\n",
		   q->qidx, err);
	return err;
}

static void fun_rxq_free_dev(struct funeth_rxq *q)
{
	struct funeth_priv *fp = netdev_priv(q->netdev);
	struct fun_irq *irq;

	if (q->init_state < FUN_QSTATE_INIT_FULL)
		return;

	irq = container_of(q->napi, struct fun_irq, napi);
	netif_info(fp, ifdown, q->netdev,
		   "Freeing Rx queue %u (id %u/%u), IRQ %u\n",
		   q->qidx, q->hw_cqid, q->hw_sqid, irq->irq_idx);

	irq->rxq = NULL;
	xdp_rxq_info_unreg(&q->xdp_rxq);
	fun_destroy_sq(fp->fdev, q->hw_sqid);
	fun_destroy_cq(fp->fdev, q->hw_cqid);
	q->init_state = FUN_QSTATE_INIT_SW;
}

/* Create or advance an Rx queue, allocating all the host and device resources
 * needed to reach the target state.
 */
int funeth_rxq_create(struct net_device *dev, unsigned int qidx,
		      unsigned int ncqe, unsigned int nrqe, struct fun_irq *irq,
		      int state, struct funeth_rxq **qp)
{
	struct funeth_rxq *q = *qp;
	int err;

	if (!q) {
		q = fun_rxq_create_sw(dev, qidx, ncqe, nrqe, irq);
		if (IS_ERR(q))
			return PTR_ERR(q);
	}

	if (q->init_state >= state)
		goto out;

	err = fun_rxq_create_dev(q, irq);
	if (err) {
		if (!*qp)
			fun_rxq_free_sw(q);
		return err;
	}

out:
	*qp = q;
	return 0;
}

/* Free Rx queue resources until it reaches the target state. */
struct funeth_rxq *funeth_rxq_free(struct funeth_rxq *q, int state)
{
	if (state < FUN_QSTATE_INIT_FULL)
		fun_rxq_free_dev(q);

	if (state == FUN_QSTATE_DESTROYED) {
		fun_rxq_free_sw(q);
		q = NULL;
	}

	return q;
}
