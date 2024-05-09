// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2018 Netronome Systems, Inc */
/* Copyright (C) 2021 Corigine, Inc */

#include <linux/bpf_trace.h>
#include <linux/netdevice.h>

#include "../nfp_app.h"
#include "../nfp_net.h"
#include "../nfp_net_dp.h"
#include "../nfp_net_xsk.h"
#include "nfd3.h"

static bool
nfp_nfd3_xsk_tx_xdp(const struct nfp_net_dp *dp, struct nfp_net_r_vector *r_vec,
		    struct nfp_net_rx_ring *rx_ring,
		    struct nfp_net_tx_ring *tx_ring,
		    struct nfp_net_xsk_rx_buf *xrxbuf, unsigned int pkt_len,
		    int pkt_off)
{
	struct xsk_buff_pool *pool = r_vec->xsk_pool;
	struct nfp_nfd3_tx_buf *txbuf;
	struct nfp_nfd3_tx_desc *txd;
	unsigned int wr_idx;

	if (nfp_net_tx_space(tx_ring) < 1)
		return false;

	xsk_buff_raw_dma_sync_for_device(pool, xrxbuf->dma_addr + pkt_off,
					 pkt_len);

	wr_idx = D_IDX(tx_ring, tx_ring->wr_p);

	txbuf = &tx_ring->txbufs[wr_idx];
	txbuf->xdp = xrxbuf->xdp;
	txbuf->real_len = pkt_len;
	txbuf->is_xsk_tx = true;

	/* Build TX descriptor */
	txd = &tx_ring->txds[wr_idx];
	txd->offset_eop = NFD3_DESC_TX_EOP;
	txd->dma_len = cpu_to_le16(pkt_len);
	nfp_desc_set_dma_addr_40b(txd, xrxbuf->dma_addr + pkt_off);
	txd->data_len = cpu_to_le16(pkt_len);

	txd->flags = 0;
	txd->mss = 0;
	txd->lso_hdrlen = 0;

	tx_ring->wr_ptr_add++;
	tx_ring->wr_p++;

	return true;
}

static void nfp_nfd3_xsk_rx_skb(struct nfp_net_rx_ring *rx_ring,
				const struct nfp_net_rx_desc *rxd,
				struct nfp_net_xsk_rx_buf *xrxbuf,
				const struct nfp_meta_parsed *meta,
				unsigned int pkt_len,
				bool meta_xdp,
				unsigned int *skbs_polled)
{
	struct nfp_net_r_vector *r_vec = rx_ring->r_vec;
	struct nfp_net_dp *dp = &r_vec->nfp_net->dp;
	struct net_device *netdev;
	struct sk_buff *skb;

	if (likely(!meta->portid)) {
		netdev = dp->netdev;
	} else {
		struct nfp_net *nn = netdev_priv(dp->netdev);

		netdev = nfp_app_dev_get(nn->app, meta->portid, NULL);
		if (unlikely(!netdev)) {
			nfp_net_xsk_rx_drop(r_vec, xrxbuf);
			return;
		}
		nfp_repr_inc_rx_stats(netdev, pkt_len);
	}

	skb = napi_alloc_skb(&r_vec->napi, pkt_len);
	if (!skb) {
		nfp_net_xsk_rx_drop(r_vec, xrxbuf);
		return;
	}
	skb_put_data(skb, xrxbuf->xdp->data, pkt_len);

	skb->mark = meta->mark;
	skb_set_hash(skb, meta->hash, meta->hash_type);

	skb_record_rx_queue(skb, rx_ring->idx);
	skb->protocol = eth_type_trans(skb, netdev);

	nfp_nfd3_rx_csum(dp, r_vec, rxd, meta, skb);

	if (unlikely(!nfp_net_vlan_strip(skb, rxd, meta))) {
		dev_kfree_skb_any(skb);
		nfp_net_xsk_rx_drop(r_vec, xrxbuf);
		return;
	}

	if (meta_xdp)
		skb_metadata_set(skb,
				 xrxbuf->xdp->data - xrxbuf->xdp->data_meta);

	napi_gro_receive(&rx_ring->r_vec->napi, skb);

	nfp_net_xsk_rx_free(xrxbuf);

	(*skbs_polled)++;
}

static unsigned int
nfp_nfd3_xsk_rx(struct nfp_net_rx_ring *rx_ring, int budget,
		unsigned int *skbs_polled)
{
	struct nfp_net_r_vector *r_vec = rx_ring->r_vec;
	struct nfp_net_dp *dp = &r_vec->nfp_net->dp;
	struct nfp_net_tx_ring *tx_ring;
	struct bpf_prog *xdp_prog;
	bool xdp_redir = false;
	int pkts_polled = 0;

	xdp_prog = READ_ONCE(dp->xdp_prog);
	tx_ring = r_vec->xdp_ring;

	while (pkts_polled < budget) {
		unsigned int meta_len, data_len, pkt_len, pkt_off;
		struct nfp_net_xsk_rx_buf *xrxbuf;
		struct nfp_net_rx_desc *rxd;
		struct nfp_meta_parsed meta;
		int idx, act;

		idx = D_IDX(rx_ring, rx_ring->rd_p);

		rxd = &rx_ring->rxds[idx];
		if (!(rxd->rxd.meta_len_dd & PCIE_DESC_RX_DD))
			break;

		rx_ring->rd_p++;
		pkts_polled++;

		xrxbuf = &rx_ring->xsk_rxbufs[idx];

		/* If starved of buffers "drop" it and scream. */
		if (rx_ring->rd_p >= rx_ring->wr_p) {
			nn_dp_warn(dp, "Starved of RX buffers\n");
			nfp_net_xsk_rx_drop(r_vec, xrxbuf);
			break;
		}

		/* Memory barrier to ensure that we won't do other reads
		 * before the DD bit.
		 */
		dma_rmb();

		memset(&meta, 0, sizeof(meta));

		/* Only supporting AF_XDP with dynamic metadata so buffer layout
		 * is always:
		 *
		 *  ---------------------------------------------------------
		 * |  off | metadata  |             packet           | XXXX  |
		 *  ---------------------------------------------------------
		 */
		meta_len = rxd->rxd.meta_len_dd & PCIE_DESC_RX_META_LEN_MASK;
		data_len = le16_to_cpu(rxd->rxd.data_len);
		pkt_len = data_len - meta_len;

		if (unlikely(meta_len > NFP_NET_MAX_PREPEND)) {
			nn_dp_warn(dp, "Oversized RX packet metadata %u\n",
				   meta_len);
			nfp_net_xsk_rx_drop(r_vec, xrxbuf);
			continue;
		}

		/* Stats update. */
		u64_stats_update_begin(&r_vec->rx_sync);
		r_vec->rx_pkts++;
		r_vec->rx_bytes += pkt_len;
		u64_stats_update_end(&r_vec->rx_sync);

		xrxbuf->xdp->data += meta_len;
		xrxbuf->xdp->data_end = xrxbuf->xdp->data + pkt_len;
		xdp_set_data_meta_invalid(xrxbuf->xdp);
		xsk_buff_dma_sync_for_cpu(xrxbuf->xdp, r_vec->xsk_pool);
		net_prefetch(xrxbuf->xdp->data);

		if (meta_len) {
			if (unlikely(nfp_nfd3_parse_meta(dp->netdev, &meta,
							 xrxbuf->xdp->data -
							 meta_len,
							 xrxbuf->xdp->data,
							 pkt_len, meta_len))) {
				nn_dp_warn(dp, "Invalid RX packet metadata\n");
				nfp_net_xsk_rx_drop(r_vec, xrxbuf);
				continue;
			}

			if (unlikely(meta.portid)) {
				struct nfp_net *nn = netdev_priv(dp->netdev);

				if (meta.portid != NFP_META_PORT_ID_CTRL) {
					nfp_nfd3_xsk_rx_skb(rx_ring, rxd,
							    xrxbuf, &meta,
							    pkt_len, false,
							    skbs_polled);
					continue;
				}

				nfp_app_ctrl_rx_raw(nn->app, xrxbuf->xdp->data,
						    pkt_len);
				nfp_net_xsk_rx_free(xrxbuf);
				continue;
			}
		}

		act = bpf_prog_run_xdp(xdp_prog, xrxbuf->xdp);

		pkt_len = xrxbuf->xdp->data_end - xrxbuf->xdp->data;
		pkt_off = xrxbuf->xdp->data - xrxbuf->xdp->data_hard_start;

		switch (act) {
		case XDP_PASS:
			nfp_nfd3_xsk_rx_skb(rx_ring, rxd, xrxbuf, &meta, pkt_len,
					    true, skbs_polled);
			break;
		case XDP_TX:
			if (!nfp_nfd3_xsk_tx_xdp(dp, r_vec, rx_ring, tx_ring,
						 xrxbuf, pkt_len, pkt_off))
				nfp_net_xsk_rx_drop(r_vec, xrxbuf);
			else
				nfp_net_xsk_rx_unstash(xrxbuf);
			break;
		case XDP_REDIRECT:
			if (xdp_do_redirect(dp->netdev, xrxbuf->xdp, xdp_prog)) {
				nfp_net_xsk_rx_drop(r_vec, xrxbuf);
			} else {
				nfp_net_xsk_rx_unstash(xrxbuf);
				xdp_redir = true;
			}
			break;
		default:
			bpf_warn_invalid_xdp_action(dp->netdev, xdp_prog, act);
			fallthrough;
		case XDP_ABORTED:
			trace_xdp_exception(dp->netdev, xdp_prog, act);
			fallthrough;
		case XDP_DROP:
			nfp_net_xsk_rx_drop(r_vec, xrxbuf);
			break;
		}
	}

	nfp_net_xsk_rx_ring_fill_freelist(r_vec->rx_ring);

	if (xdp_redir)
		xdp_do_flush();

	if (tx_ring->wr_ptr_add)
		nfp_net_tx_xmit_more_flush(tx_ring);

	return pkts_polled;
}

void nfp_nfd3_xsk_tx_free(struct nfp_nfd3_tx_buf *txbuf)
{
	xsk_buff_free(txbuf->xdp);

	txbuf->dma_addr = 0;
	txbuf->xdp = NULL;
}

static bool nfp_nfd3_xsk_complete(struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;
	u32 done_pkts = 0, done_bytes = 0, reused = 0;
	bool done_all;
	int idx, todo;
	u32 qcp_rd_p;

	if (tx_ring->wr_p == tx_ring->rd_p)
		return true;

	/* Work out how many descriptors have been transmitted. */
	qcp_rd_p = nfp_qcp_rd_ptr_read(tx_ring->qcp_q);

	if (qcp_rd_p == tx_ring->qcp_rd_p)
		return true;

	todo = D_IDX(tx_ring, qcp_rd_p - tx_ring->qcp_rd_p);

	done_all = todo <= NFP_NET_XDP_MAX_COMPLETE;
	todo = min(todo, NFP_NET_XDP_MAX_COMPLETE);

	tx_ring->qcp_rd_p = D_IDX(tx_ring, tx_ring->qcp_rd_p + todo);

	done_pkts = todo;
	while (todo--) {
		struct nfp_nfd3_tx_buf *txbuf;

		idx = D_IDX(tx_ring, tx_ring->rd_p);
		tx_ring->rd_p++;

		txbuf = &tx_ring->txbufs[idx];
		if (unlikely(!txbuf->real_len))
			continue;

		done_bytes += txbuf->real_len;
		txbuf->real_len = 0;

		if (txbuf->is_xsk_tx) {
			nfp_nfd3_xsk_tx_free(txbuf);
			reused++;
		}
	}

	u64_stats_update_begin(&r_vec->tx_sync);
	r_vec->tx_bytes += done_bytes;
	r_vec->tx_pkts += done_pkts;
	u64_stats_update_end(&r_vec->tx_sync);

	xsk_tx_completed(r_vec->xsk_pool, done_pkts - reused);

	WARN_ONCE(tx_ring->wr_p - tx_ring->rd_p > tx_ring->cnt,
		  "XDP TX ring corruption rd_p=%u wr_p=%u cnt=%u\n",
		  tx_ring->rd_p, tx_ring->wr_p, tx_ring->cnt);

	return done_all;
}

static void nfp_nfd3_xsk_tx(struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;
	struct xdp_desc desc[NFP_NET_XSK_TX_BATCH];
	struct xsk_buff_pool *xsk_pool;
	struct nfp_nfd3_tx_desc *txd;
	u32 pkts = 0, wr_idx;
	u32 i, got;

	xsk_pool = r_vec->xsk_pool;

	while (nfp_net_tx_space(tx_ring) >= NFP_NET_XSK_TX_BATCH) {
		for (i = 0; i < NFP_NET_XSK_TX_BATCH; i++)
			if (!xsk_tx_peek_desc(xsk_pool, &desc[i]))
				break;
		got = i;
		if (!got)
			break;

		wr_idx = D_IDX(tx_ring, tx_ring->wr_p + i);
		prefetchw(&tx_ring->txds[wr_idx]);

		for (i = 0; i < got; i++)
			xsk_buff_raw_dma_sync_for_device(xsk_pool, desc[i].addr,
							 desc[i].len);

		for (i = 0; i < got; i++) {
			wr_idx = D_IDX(tx_ring, tx_ring->wr_p + i);

			tx_ring->txbufs[wr_idx].real_len = desc[i].len;
			tx_ring->txbufs[wr_idx].is_xsk_tx = false;

			/* Build TX descriptor. */
			txd = &tx_ring->txds[wr_idx];
			nfp_desc_set_dma_addr_40b(txd,
						  xsk_buff_raw_get_dma(xsk_pool, desc[i].addr));
			txd->offset_eop = NFD3_DESC_TX_EOP;
			txd->dma_len = cpu_to_le16(desc[i].len);
			txd->data_len = cpu_to_le16(desc[i].len);
		}

		tx_ring->wr_p += got;
		pkts += got;
	}

	if (!pkts)
		return;

	xsk_tx_release(xsk_pool);
	/* Ensure all records are visible before incrementing write counter. */
	wmb();
	nfp_qcp_wr_ptr_add(tx_ring->qcp_q, pkts);
}

int nfp_nfd3_xsk_poll(struct napi_struct *napi, int budget)
{
	struct nfp_net_r_vector *r_vec =
		container_of(napi, struct nfp_net_r_vector, napi);
	unsigned int pkts_polled, skbs = 0;

	pkts_polled = nfp_nfd3_xsk_rx(r_vec->rx_ring, budget, &skbs);

	if (pkts_polled < budget) {
		if (r_vec->tx_ring)
			nfp_nfd3_tx_complete(r_vec->tx_ring, budget);

		if (!nfp_nfd3_xsk_complete(r_vec->xdp_ring))
			pkts_polled = budget;

		nfp_nfd3_xsk_tx(r_vec->xdp_ring);

		if (pkts_polled < budget && napi_complete_done(napi, skbs))
			nfp_net_irq_unmask(r_vec->nfp_net, r_vec->irq_entry);
	}

	return pkts_polled;
}
