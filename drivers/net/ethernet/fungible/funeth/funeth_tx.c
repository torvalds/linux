// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)

#include <linux/dma-mapping.h>
#include <linux/ip.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <uapi/linux/udp.h>
#include "funeth.h"
#include "funeth_ktls.h"
#include "funeth_txrx.h"
#include "funeth_trace.h"
#include "fun_queue.h"

#define FUN_XDP_CLEAN_THRES 32
#define FUN_XDP_CLEAN_BATCH 16

/* DMA-map a packet and return the (length, DMA_address) pairs for its
 * segments. If a mapping error occurs -ENOMEM is returned.
 */
static int map_skb(const struct sk_buff *skb, struct device *dev,
		   dma_addr_t *addr, unsigned int *len)
{
	const struct skb_shared_info *si;
	const skb_frag_t *fp, *end;

	*len = skb_headlen(skb);
	*addr = dma_map_single(dev, skb->data, *len, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, *addr))
		return -ENOMEM;

	si = skb_shinfo(skb);
	end = &si->frags[si->nr_frags];

	for (fp = si->frags; fp < end; fp++) {
		*++len = skb_frag_size(fp);
		*++addr = skb_frag_dma_map(dev, fp, 0, *len, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, *addr))
			goto unwind;
	}
	return 0;

unwind:
	while (fp-- > si->frags)
		dma_unmap_page(dev, *--addr, skb_frag_size(fp), DMA_TO_DEVICE);

	dma_unmap_single(dev, addr[-1], skb_headlen(skb), DMA_TO_DEVICE);
	return -ENOMEM;
}

/* Return the address just past the end of a Tx queue's descriptor ring.
 * It exploits the fact that the HW writeback area is just after the end
 * of the descriptor ring.
 */
static void *txq_end(const struct funeth_txq *q)
{
	return (void *)q->hw_wb;
}

/* Return the amount of space within a Tx ring from the given address to the
 * end.
 */
static unsigned int txq_to_end(const struct funeth_txq *q, void *p)
{
	return txq_end(q) - p;
}

/* Return the number of Tx descriptors occupied by a Tx request. */
static unsigned int tx_req_ndesc(const struct fun_eth_tx_req *req)
{
	return DIV_ROUND_UP(req->len8, FUNETH_SQE_SIZE / 8);
}

static __be16 tcp_hdr_doff_flags(const struct tcphdr *th)
{
	return *(__be16 *)&tcp_flag_word(th);
}

static struct sk_buff *fun_tls_tx(struct sk_buff *skb, struct funeth_txq *q,
				  unsigned int *tls_len)
{
#if IS_ENABLED(CONFIG_TLS_DEVICE)
	const struct fun_ktls_tx_ctx *tls_ctx;
	u32 datalen, seq;

	datalen = skb->len - (skb_transport_offset(skb) + tcp_hdrlen(skb));
	if (!datalen)
		return skb;

	if (likely(!tls_offload_tx_resync_pending(skb->sk))) {
		seq = ntohl(tcp_hdr(skb)->seq);
		tls_ctx = tls_driver_ctx(skb->sk, TLS_OFFLOAD_CTX_DIR_TX);

		if (likely(tls_ctx->next_seq == seq)) {
			*tls_len = datalen;
			return skb;
		}
		if (seq - tls_ctx->next_seq < U32_MAX / 4) {
			tls_offload_tx_resync_request(skb->sk, seq,
						      tls_ctx->next_seq);
		}
	}

	FUN_QSTAT_INC(q, tx_tls_fallback);
	skb = tls_encrypt_skb(skb);
	if (!skb)
		FUN_QSTAT_INC(q, tx_tls_drops);

	return skb;
#else
	return NULL;
#endif
}

/* Write as many descriptors as needed for the supplied skb starting at the
 * current producer location. The caller has made certain enough descriptors
 * are available.
 *
 * Returns the number of descriptors written, 0 on error.
 */
static unsigned int write_pkt_desc(struct sk_buff *skb, struct funeth_txq *q,
				   unsigned int tls_len)
{
	unsigned int extra_bytes = 0, extra_pkts = 0;
	unsigned int idx = q->prod_cnt & q->mask;
	const struct skb_shared_info *shinfo;
	unsigned int lens[MAX_SKB_FRAGS + 1];
	dma_addr_t addrs[MAX_SKB_FRAGS + 1];
	struct fun_eth_tx_req *req;
	struct fun_dataop_gl *gle;
	const struct tcphdr *th;
	unsigned int ngle, i;
	u16 flags;

	if (unlikely(map_skb(skb, q->dma_dev, addrs, lens))) {
		FUN_QSTAT_INC(q, tx_map_err);
		return 0;
	}

	req = fun_tx_desc_addr(q, idx);
	req->op = FUN_ETH_OP_TX;
	req->len8 = 0;
	req->flags = 0;
	req->suboff8 = offsetof(struct fun_eth_tx_req, dataop);
	req->repr_idn = 0;
	req->encap_proto = 0;

	shinfo = skb_shinfo(skb);
	if (likely(shinfo->gso_size)) {
		if (skb->encapsulation) {
			u16 ol4_ofst;

			flags = FUN_ETH_OUTER_EN | FUN_ETH_INNER_LSO |
				FUN_ETH_UPDATE_INNER_L4_CKSUM |
				FUN_ETH_UPDATE_OUTER_L3_LEN;
			if (shinfo->gso_type & (SKB_GSO_UDP_TUNNEL |
						SKB_GSO_UDP_TUNNEL_CSUM)) {
				flags |= FUN_ETH_UPDATE_OUTER_L4_LEN |
					 FUN_ETH_OUTER_UDP;
				if (shinfo->gso_type & SKB_GSO_UDP_TUNNEL_CSUM)
					flags |= FUN_ETH_UPDATE_OUTER_L4_CKSUM;
				ol4_ofst = skb_transport_offset(skb);
			} else {
				ol4_ofst = skb_inner_network_offset(skb);
			}

			if (ip_hdr(skb)->version == 4)
				flags |= FUN_ETH_UPDATE_OUTER_L3_CKSUM;
			else
				flags |= FUN_ETH_OUTER_IPV6;

			if (skb->inner_network_header) {
				if (inner_ip_hdr(skb)->version == 4)
					flags |= FUN_ETH_UPDATE_INNER_L3_CKSUM |
						 FUN_ETH_UPDATE_INNER_L3_LEN;
				else
					flags |= FUN_ETH_INNER_IPV6 |
						 FUN_ETH_UPDATE_INNER_L3_LEN;
			}
			th = inner_tcp_hdr(skb);
			fun_eth_offload_init(&req->offload, flags,
					     shinfo->gso_size,
					     tcp_hdr_doff_flags(th), 0,
					     skb_inner_network_offset(skb),
					     skb_inner_transport_offset(skb),
					     skb_network_offset(skb), ol4_ofst);
			FUN_QSTAT_INC(q, tx_encap_tso);
		} else {
			/* HW considers one set of headers as inner */
			flags = FUN_ETH_INNER_LSO |
				FUN_ETH_UPDATE_INNER_L4_CKSUM |
				FUN_ETH_UPDATE_INNER_L3_LEN;
			if (shinfo->gso_type & SKB_GSO_TCPV6)
				flags |= FUN_ETH_INNER_IPV6;
			else
				flags |= FUN_ETH_UPDATE_INNER_L3_CKSUM;
			th = tcp_hdr(skb);
			fun_eth_offload_init(&req->offload, flags,
					     shinfo->gso_size,
					     tcp_hdr_doff_flags(th), 0,
					     skb_network_offset(skb),
					     skb_transport_offset(skb), 0, 0);
			FUN_QSTAT_INC(q, tx_tso);
		}

		u64_stats_update_begin(&q->syncp);
		q->stats.tx_cso += shinfo->gso_segs;
		u64_stats_update_end(&q->syncp);

		extra_pkts = shinfo->gso_segs - 1;
		extra_bytes = (be16_to_cpu(req->offload.inner_l4_off) +
			       __tcp_hdrlen(th)) * extra_pkts;
	} else if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		flags = FUN_ETH_UPDATE_INNER_L4_CKSUM;
		if (skb->csum_offset == offsetof(struct udphdr, check))
			flags |= FUN_ETH_INNER_UDP;
		fun_eth_offload_init(&req->offload, flags, 0, 0, 0, 0,
				     skb_checksum_start_offset(skb), 0, 0);
		FUN_QSTAT_INC(q, tx_cso);
	} else {
		fun_eth_offload_init(&req->offload, 0, 0, 0, 0, 0, 0, 0, 0);
	}

	ngle = shinfo->nr_frags + 1;
	req->len8 = (sizeof(*req) + ngle * sizeof(*gle)) / 8;
	req->dataop = FUN_DATAOP_HDR_INIT(ngle, 0, ngle, 0, skb->len);

	for (i = 0, gle = (struct fun_dataop_gl *)req->dataop.imm;
	     i < ngle && txq_to_end(q, gle); i++, gle++)
		fun_dataop_gl_init(gle, 0, 0, lens[i], addrs[i]);

	if (txq_to_end(q, gle) == 0) {
		gle = (struct fun_dataop_gl *)q->desc;
		for ( ; i < ngle; i++, gle++)
			fun_dataop_gl_init(gle, 0, 0, lens[i], addrs[i]);
	}

	if (IS_ENABLED(CONFIG_TLS_DEVICE) && unlikely(tls_len)) {
		struct fun_eth_tls *tls = (struct fun_eth_tls *)gle;
		struct fun_ktls_tx_ctx *tls_ctx;

		req->len8 += FUNETH_TLS_SZ / 8;
		req->flags = cpu_to_be16(FUN_ETH_TX_TLS);

		tls_ctx = tls_driver_ctx(skb->sk, TLS_OFFLOAD_CTX_DIR_TX);
		tls->tlsid = tls_ctx->tlsid;
		tls_ctx->next_seq += tls_len;

		u64_stats_update_begin(&q->syncp);
		q->stats.tx_tls_bytes += tls_len;
		q->stats.tx_tls_pkts += 1 + extra_pkts;
		u64_stats_update_end(&q->syncp);
	}

	u64_stats_update_begin(&q->syncp);
	q->stats.tx_bytes += skb->len + extra_bytes;
	q->stats.tx_pkts += 1 + extra_pkts;
	u64_stats_update_end(&q->syncp);

	q->info[idx].skb = skb;

	trace_funeth_tx(q, skb->len, idx, req->dataop.ngather);
	return tx_req_ndesc(req);
}

/* Return the number of available descriptors of a Tx queue.
 * HW assumes head==tail means the ring is empty so we need to keep one
 * descriptor unused.
 */
static unsigned int fun_txq_avail(const struct funeth_txq *q)
{
	return q->mask - q->prod_cnt + q->cons_cnt;
}

/* Stop a queue if it can't handle another worst-case packet. */
static void fun_tx_check_stop(struct funeth_txq *q)
{
	if (likely(fun_txq_avail(q) >= FUNETH_MAX_PKT_DESC))
		return;

	netif_tx_stop_queue(q->ndq);

	/* NAPI reclaim is freeing packets in parallel with us and we may race.
	 * We have stopped the queue but check again after synchronizing with
	 * reclaim.
	 */
	smp_mb();
	if (likely(fun_txq_avail(q) < FUNETH_MAX_PKT_DESC))
		FUN_QSTAT_INC(q, tx_nstops);
	else
		netif_tx_start_queue(q->ndq);
}

/* Return true if a queue has enough space to restart. Current condition is
 * that the queue must be >= 1/4 empty.
 */
static bool fun_txq_may_restart(struct funeth_txq *q)
{
	return fun_txq_avail(q) >= q->mask / 4;
}

netdev_tx_t fun_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct funeth_priv *fp = netdev_priv(netdev);
	unsigned int qid = skb_get_queue_mapping(skb);
	struct funeth_txq *q = fp->txqs[qid];
	unsigned int tls_len = 0;
	unsigned int ndesc;

	if (IS_ENABLED(CONFIG_TLS_DEVICE) && skb->sk &&
	    tls_is_sk_tx_device_offloaded(skb->sk)) {
		skb = fun_tls_tx(skb, q, &tls_len);
		if (unlikely(!skb))
			goto dropped;
	}

	ndesc = write_pkt_desc(skb, q, tls_len);
	if (unlikely(!ndesc)) {
		dev_kfree_skb_any(skb);
		goto dropped;
	}

	q->prod_cnt += ndesc;
	fun_tx_check_stop(q);

	skb_tx_timestamp(skb);

	if (__netdev_tx_sent_queue(q->ndq, skb->len, netdev_xmit_more()))
		fun_txq_wr_db(q);
	else
		FUN_QSTAT_INC(q, tx_more);

	return NETDEV_TX_OK;

dropped:
	/* A dropped packet may be the last one in a xmit_more train,
	 * ring the doorbell just in case.
	 */
	if (!netdev_xmit_more())
		fun_txq_wr_db(q);
	return NETDEV_TX_OK;
}

/* Return a Tx queue's HW head index written back to host memory. */
static u16 txq_hw_head(const struct funeth_txq *q)
{
	return (u16)be64_to_cpu(*q->hw_wb);
}

/* Unmap the Tx packet starting at the given descriptor index and
 * return the number of Tx descriptors it occupied.
 */
static unsigned int unmap_skb(const struct funeth_txq *q, unsigned int idx)
{
	const struct fun_eth_tx_req *req = fun_tx_desc_addr(q, idx);
	unsigned int ngle = req->dataop.ngather;
	struct fun_dataop_gl *gle;

	if (ngle) {
		gle = (struct fun_dataop_gl *)req->dataop.imm;
		dma_unmap_single(q->dma_dev, be64_to_cpu(gle->sgl_data),
				 be32_to_cpu(gle->sgl_len), DMA_TO_DEVICE);

		for (gle++; --ngle && txq_to_end(q, gle); gle++)
			dma_unmap_page(q->dma_dev, be64_to_cpu(gle->sgl_data),
				       be32_to_cpu(gle->sgl_len),
				       DMA_TO_DEVICE);

		for (gle = (struct fun_dataop_gl *)q->desc; ngle; ngle--, gle++)
			dma_unmap_page(q->dma_dev, be64_to_cpu(gle->sgl_data),
				       be32_to_cpu(gle->sgl_len),
				       DMA_TO_DEVICE);
	}

	return tx_req_ndesc(req);
}

/* Reclaim completed Tx descriptors and free their packets. Restart a stopped
 * queue if we freed enough descriptors.
 *
 * Return true if we exhausted the budget while there is more work to be done.
 */
static bool fun_txq_reclaim(struct funeth_txq *q, int budget)
{
	unsigned int npkts = 0, nbytes = 0, ndesc = 0;
	unsigned int head, limit, reclaim_idx;

	/* budget may be 0, e.g., netpoll */
	limit = budget ? budget : UINT_MAX;

	for (head = txq_hw_head(q), reclaim_idx = q->cons_cnt & q->mask;
	     head != reclaim_idx && npkts < limit; head = txq_hw_head(q)) {
		/* The HW head is continually updated, ensure we don't read
		 * descriptor state before the head tells us to reclaim it.
		 * On the enqueue side the doorbell is an implicit write
		 * barrier.
		 */
		rmb();

		do {
			unsigned int pkt_desc = unmap_skb(q, reclaim_idx);
			struct sk_buff *skb = q->info[reclaim_idx].skb;

			trace_funeth_tx_free(q, reclaim_idx, pkt_desc, head);

			nbytes += skb->len;
			napi_consume_skb(skb, budget);
			ndesc += pkt_desc;
			reclaim_idx = (reclaim_idx + pkt_desc) & q->mask;
			npkts++;
		} while (reclaim_idx != head && npkts < limit);
	}

	q->cons_cnt += ndesc;
	netdev_tx_completed_queue(q->ndq, npkts, nbytes);
	smp_mb(); /* pairs with the one in fun_tx_check_stop() */

	if (unlikely(netif_tx_queue_stopped(q->ndq) &&
		     fun_txq_may_restart(q))) {
		netif_tx_wake_queue(q->ndq);
		FUN_QSTAT_INC(q, tx_nrestarts);
	}

	return reclaim_idx != head;
}

/* The NAPI handler for Tx queues. */
int fun_txq_napi_poll(struct napi_struct *napi, int budget)
{
	struct fun_irq *irq = container_of(napi, struct fun_irq, napi);
	struct funeth_txq *q = irq->txq;
	unsigned int db_val;

	if (fun_txq_reclaim(q, budget))
		return budget;               /* exhausted budget */

	napi_complete(napi);                 /* exhausted pending work */
	db_val = READ_ONCE(q->irq_db_val) | (q->cons_cnt & q->mask);
	writel(db_val, q->db);
	return 0;
}

static void fun_xdp_unmap(const struct funeth_txq *q, unsigned int idx)
{
	const struct fun_eth_tx_req *req = fun_tx_desc_addr(q, idx);
	const struct fun_dataop_gl *gle;

	gle = (const struct fun_dataop_gl *)req->dataop.imm;
	dma_unmap_single(q->dma_dev, be64_to_cpu(gle->sgl_data),
			 be32_to_cpu(gle->sgl_len), DMA_TO_DEVICE);
}

/* Reclaim up to @budget completed Tx descriptors from a TX XDP queue. */
static unsigned int fun_xdpq_clean(struct funeth_txq *q, unsigned int budget)
{
	unsigned int npkts = 0, head, reclaim_idx;

	for (head = txq_hw_head(q), reclaim_idx = q->cons_cnt & q->mask;
	     head != reclaim_idx && npkts < budget; head = txq_hw_head(q)) {
		/* The HW head is continually updated, ensure we don't read
		 * descriptor state before the head tells us to reclaim it.
		 * On the enqueue side the doorbell is an implicit write
		 * barrier.
		 */
		rmb();

		do {
			fun_xdp_unmap(q, reclaim_idx);
			page_frag_free(q->info[reclaim_idx].vaddr);

			trace_funeth_tx_free(q, reclaim_idx, 1, head);

			reclaim_idx = (reclaim_idx + 1) & q->mask;
			npkts++;
		} while (reclaim_idx != head && npkts < budget);
	}

	q->cons_cnt += npkts;
	return npkts;
}

bool fun_xdp_tx(struct funeth_txq *q, void *data, unsigned int len)
{
	struct fun_eth_tx_req *req;
	struct fun_dataop_gl *gle;
	unsigned int idx;
	dma_addr_t dma;

	if (fun_txq_avail(q) < FUN_XDP_CLEAN_THRES)
		fun_xdpq_clean(q, FUN_XDP_CLEAN_BATCH);

	if (!unlikely(fun_txq_avail(q))) {
		FUN_QSTAT_INC(q, tx_xdp_full);
		return false;
	}

	dma = dma_map_single(q->dma_dev, data, len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(q->dma_dev, dma))) {
		FUN_QSTAT_INC(q, tx_map_err);
		return false;
	}

	idx = q->prod_cnt & q->mask;
	req = fun_tx_desc_addr(q, idx);
	req->op = FUN_ETH_OP_TX;
	req->len8 = (sizeof(*req) + sizeof(*gle)) / 8;
	req->flags = 0;
	req->suboff8 = offsetof(struct fun_eth_tx_req, dataop);
	req->repr_idn = 0;
	req->encap_proto = 0;
	fun_eth_offload_init(&req->offload, 0, 0, 0, 0, 0, 0, 0, 0);
	req->dataop = FUN_DATAOP_HDR_INIT(1, 0, 1, 0, len);

	gle = (struct fun_dataop_gl *)req->dataop.imm;
	fun_dataop_gl_init(gle, 0, 0, len, dma);

	q->info[idx].vaddr = data;

	u64_stats_update_begin(&q->syncp);
	q->stats.tx_bytes += len;
	q->stats.tx_pkts++;
	u64_stats_update_end(&q->syncp);

	trace_funeth_tx(q, len, idx, 1);
	q->prod_cnt++;

	return true;
}

int fun_xdp_xmit_frames(struct net_device *dev, int n,
			struct xdp_frame **frames, u32 flags)
{
	struct funeth_priv *fp = netdev_priv(dev);
	struct funeth_txq *q, **xdpqs;
	int i, q_idx;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	xdpqs = rcu_dereference_bh(fp->xdpqs);
	if (unlikely(!xdpqs))
		return -ENETDOWN;

	q_idx = smp_processor_id();
	if (unlikely(q_idx >= fp->num_xdpqs))
		return -ENXIO;

	for (q = xdpqs[q_idx], i = 0; i < n; i++) {
		const struct xdp_frame *xdpf = frames[i];

		if (!fun_xdp_tx(q, xdpf->data, xdpf->len))
			break;
	}

	if (unlikely(flags & XDP_XMIT_FLUSH))
		fun_txq_wr_db(q);
	return i;
}

/* Purge a Tx queue of any queued packets. Should be called once HW access
 * to the packets has been revoked, e.g., after the queue has been disabled.
 */
static void fun_txq_purge(struct funeth_txq *q)
{
	while (q->cons_cnt != q->prod_cnt) {
		unsigned int idx = q->cons_cnt & q->mask;

		q->cons_cnt += unmap_skb(q, idx);
		dev_kfree_skb_any(q->info[idx].skb);
	}
	netdev_tx_reset_queue(q->ndq);
}

static void fun_xdpq_purge(struct funeth_txq *q)
{
	while (q->cons_cnt != q->prod_cnt) {
		unsigned int idx = q->cons_cnt & q->mask;

		fun_xdp_unmap(q, idx);
		page_frag_free(q->info[idx].vaddr);
		q->cons_cnt++;
	}
}

/* Create a Tx queue, allocating all the host resources needed. */
static struct funeth_txq *fun_txq_create_sw(struct net_device *dev,
					    unsigned int qidx,
					    unsigned int ndesc,
					    struct fun_irq *irq)
{
	struct funeth_priv *fp = netdev_priv(dev);
	struct funeth_txq *q;
	int numa_node;

	if (irq)
		numa_node = fun_irq_node(irq); /* skb Tx queue */
	else
		numa_node = cpu_to_node(qidx); /* XDP Tx queue */

	q = kzalloc_node(sizeof(*q), GFP_KERNEL, numa_node);
	if (!q)
		goto err;

	q->dma_dev = &fp->pdev->dev;
	q->desc = fun_alloc_ring_mem(q->dma_dev, ndesc, FUNETH_SQE_SIZE,
				     sizeof(*q->info), true, numa_node,
				     &q->dma_addr, (void **)&q->info,
				     &q->hw_wb);
	if (!q->desc)
		goto free_q;

	q->netdev = dev;
	q->mask = ndesc - 1;
	q->qidx = qidx;
	q->numa_node = numa_node;
	u64_stats_init(&q->syncp);
	q->init_state = FUN_QSTATE_INIT_SW;
	return q;

free_q:
	kfree(q);
err:
	netdev_err(dev, "Can't allocate memory for %s queue %u\n",
		   irq ? "Tx" : "XDP", qidx);
	return NULL;
}

static void fun_txq_free_sw(struct funeth_txq *q)
{
	struct funeth_priv *fp = netdev_priv(q->netdev);

	fun_free_ring_mem(q->dma_dev, q->mask + 1, FUNETH_SQE_SIZE, true,
			  q->desc, q->dma_addr, q->info);

	fp->tx_packets += q->stats.tx_pkts;
	fp->tx_bytes   += q->stats.tx_bytes;
	fp->tx_dropped += q->stats.tx_map_err;

	kfree(q);
}

/* Allocate the device portion of a Tx queue. */
int fun_txq_create_dev(struct funeth_txq *q, struct fun_irq *irq)
{
	struct funeth_priv *fp = netdev_priv(q->netdev);
	unsigned int irq_idx, ndesc = q->mask + 1;
	int err;

	q->irq = irq;
	*q->hw_wb = 0;
	q->prod_cnt = 0;
	q->cons_cnt = 0;
	irq_idx = irq ? irq->irq_idx : 0;

	err = fun_sq_create(fp->fdev,
			    FUN_ADMIN_EPSQ_CREATE_FLAG_HEAD_WB_ADDRESS |
			    FUN_ADMIN_RES_CREATE_FLAG_ALLOCATOR, 0,
			    FUN_HCI_ID_INVALID, ilog2(FUNETH_SQE_SIZE), ndesc,
			    q->dma_addr, fp->tx_coal_count, fp->tx_coal_usec,
			    irq_idx, 0, fp->fdev->kern_end_qid, 0,
			    &q->hw_qid, &q->db);
	if (err)
		goto out;

	err = fun_create_and_bind_tx(fp, q->hw_qid);
	if (err < 0)
		goto free_devq;
	q->ethid = err;

	if (irq) {
		irq->txq = q;
		q->ndq = netdev_get_tx_queue(q->netdev, q->qidx);
		q->irq_db_val = FUN_IRQ_SQ_DB(fp->tx_coal_usec,
					      fp->tx_coal_count);
		writel(q->irq_db_val, q->db);
	}

	q->init_state = FUN_QSTATE_INIT_FULL;
	netif_info(fp, ifup, q->netdev,
		   "%s queue %u, depth %u, HW qid %u, IRQ idx %u, eth id %u, node %d\n",
		   irq ? "Tx" : "XDP", q->qidx, ndesc, q->hw_qid, irq_idx,
		   q->ethid, q->numa_node);
	return 0;

free_devq:
	fun_destroy_sq(fp->fdev, q->hw_qid);
out:
	netdev_err(q->netdev,
		   "Failed to create %s queue %u on device, error %d\n",
		   irq ? "Tx" : "XDP", q->qidx, err);
	return err;
}

static void fun_txq_free_dev(struct funeth_txq *q)
{
	struct funeth_priv *fp = netdev_priv(q->netdev);

	if (q->init_state < FUN_QSTATE_INIT_FULL)
		return;

	netif_info(fp, ifdown, q->netdev,
		   "Freeing %s queue %u (id %u), IRQ %u, ethid %u\n",
		   q->irq ? "Tx" : "XDP", q->qidx, q->hw_qid,
		   q->irq ? q->irq->irq_idx : 0, q->ethid);

	fun_destroy_sq(fp->fdev, q->hw_qid);
	fun_res_destroy(fp->fdev, FUN_ADMIN_OP_ETH, 0, q->ethid);

	if (q->irq) {
		q->irq->txq = NULL;
		fun_txq_purge(q);
	} else {
		fun_xdpq_purge(q);
	}

	q->init_state = FUN_QSTATE_INIT_SW;
}

/* Create or advance a Tx queue, allocating all the host and device resources
 * needed to reach the target state.
 */
int funeth_txq_create(struct net_device *dev, unsigned int qidx,
		      unsigned int ndesc, struct fun_irq *irq, int state,
		      struct funeth_txq **qp)
{
	struct funeth_txq *q = *qp;
	int err;

	if (!q)
		q = fun_txq_create_sw(dev, qidx, ndesc, irq);
	if (!q)
		return -ENOMEM;

	if (q->init_state >= state)
		goto out;

	err = fun_txq_create_dev(q, irq);
	if (err) {
		if (!*qp)
			fun_txq_free_sw(q);
		return err;
	}

out:
	*qp = q;
	return 0;
}

/* Free Tx queue resources until it reaches the target state.
 * The queue must be already disconnected from the stack.
 */
struct funeth_txq *funeth_txq_free(struct funeth_txq *q, int state)
{
	if (state < FUN_QSTATE_INIT_FULL)
		fun_txq_free_dev(q);

	if (state == FUN_QSTATE_DESTROYED) {
		fun_txq_free_sw(q);
		q = NULL;
	}

	return q;
}
