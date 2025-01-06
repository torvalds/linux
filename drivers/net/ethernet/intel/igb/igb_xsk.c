// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2018 Intel Corporation. */

#include <linux/bpf_trace.h>
#include <net/xdp_sock_drv.h>
#include <net/xdp.h>

#include "e1000_hw.h"
#include "igb.h"

static int igb_realloc_rx_buffer_info(struct igb_ring *ring, bool pool_present)
{
	int size = pool_present ?
		sizeof(*ring->rx_buffer_info_zc) * ring->count :
		sizeof(*ring->rx_buffer_info) * ring->count;
	void *buff_info = vmalloc(size);

	if (!buff_info)
		return -ENOMEM;

	if (pool_present) {
		vfree(ring->rx_buffer_info);
		ring->rx_buffer_info = NULL;
		ring->rx_buffer_info_zc = buff_info;
	} else {
		vfree(ring->rx_buffer_info_zc);
		ring->rx_buffer_info_zc = NULL;
		ring->rx_buffer_info = buff_info;
	}

	return 0;
}

static void igb_txrx_ring_disable(struct igb_adapter *adapter, u16 qid)
{
	struct igb_ring *tx_ring = adapter->tx_ring[qid];
	struct igb_ring *rx_ring = adapter->rx_ring[qid];
	struct e1000_hw *hw = &adapter->hw;

	set_bit(IGB_RING_FLAG_TX_DISABLED, &tx_ring->flags);

	wr32(E1000_TXDCTL(tx_ring->reg_idx), 0);
	wr32(E1000_RXDCTL(rx_ring->reg_idx), 0);

	synchronize_net();

	/* Rx/Tx share the same napi context. */
	napi_disable(&rx_ring->q_vector->napi);

	igb_clean_tx_ring(tx_ring);
	igb_clean_rx_ring(rx_ring);

	memset(&rx_ring->rx_stats, 0, sizeof(rx_ring->rx_stats));
	memset(&tx_ring->tx_stats, 0, sizeof(tx_ring->tx_stats));
}

static void igb_txrx_ring_enable(struct igb_adapter *adapter, u16 qid)
{
	struct igb_ring *tx_ring = adapter->tx_ring[qid];
	struct igb_ring *rx_ring = adapter->rx_ring[qid];

	igb_configure_tx_ring(adapter, tx_ring);
	igb_configure_rx_ring(adapter, rx_ring);

	synchronize_net();

	clear_bit(IGB_RING_FLAG_TX_DISABLED, &tx_ring->flags);

	/* call igb_desc_unused which always leaves
	 * at least 1 descriptor unused to make sure
	 * next_to_use != next_to_clean
	 */
	if (rx_ring->xsk_pool)
		igb_alloc_rx_buffers_zc(rx_ring, rx_ring->xsk_pool,
					igb_desc_unused(rx_ring));
	else
		igb_alloc_rx_buffers(rx_ring, igb_desc_unused(rx_ring));

	/* Rx/Tx share the same napi context. */
	napi_enable(&rx_ring->q_vector->napi);
}

struct xsk_buff_pool *igb_xsk_pool(struct igb_adapter *adapter,
				   struct igb_ring *ring)
{
	int qid = ring->queue_index;
	struct xsk_buff_pool *pool;

	pool = xsk_get_pool_from_qid(adapter->netdev, qid);

	if (!igb_xdp_is_enabled(adapter))
		return NULL;

	return (pool && pool->dev) ? pool : NULL;
}

static int igb_xsk_pool_enable(struct igb_adapter *adapter,
			       struct xsk_buff_pool *pool,
			       u16 qid)
{
	struct net_device *netdev = adapter->netdev;
	struct igb_ring *rx_ring;
	bool if_running;
	int err;

	if (qid >= adapter->num_rx_queues)
		return -EINVAL;

	if (qid >= netdev->real_num_rx_queues ||
	    qid >= netdev->real_num_tx_queues)
		return -EINVAL;

	err = xsk_pool_dma_map(pool, &adapter->pdev->dev, IGB_RX_DMA_ATTR);
	if (err)
		return err;

	rx_ring = adapter->rx_ring[qid];
	if_running = netif_running(adapter->netdev) && igb_xdp_is_enabled(adapter);
	if (if_running)
		igb_txrx_ring_disable(adapter, qid);

	if (if_running) {
		err = igb_realloc_rx_buffer_info(rx_ring, true);
		if (!err) {
			igb_txrx_ring_enable(adapter, qid);
			/* Kick start the NAPI context so that receiving will start */
			err = igb_xsk_wakeup(adapter->netdev, qid, XDP_WAKEUP_RX);
		}

		if (err) {
			xsk_pool_dma_unmap(pool, IGB_RX_DMA_ATTR);
			return err;
		}
	}

	return 0;
}

static int igb_xsk_pool_disable(struct igb_adapter *adapter, u16 qid)
{
	struct xsk_buff_pool *pool;
	struct igb_ring *rx_ring;
	bool if_running;
	int err;

	pool = xsk_get_pool_from_qid(adapter->netdev, qid);
	if (!pool)
		return -EINVAL;

	rx_ring = adapter->rx_ring[qid];
	if_running = netif_running(adapter->netdev) && igb_xdp_is_enabled(adapter);
	if (if_running)
		igb_txrx_ring_disable(adapter, qid);

	xsk_pool_dma_unmap(pool, IGB_RX_DMA_ATTR);

	if (if_running) {
		err = igb_realloc_rx_buffer_info(rx_ring, false);
		if (err)
			return err;

		igb_txrx_ring_enable(adapter, qid);
	}

	return 0;
}

int igb_xsk_pool_setup(struct igb_adapter *adapter,
		       struct xsk_buff_pool *pool,
		       u16 qid)
{
	return pool ? igb_xsk_pool_enable(adapter, pool, qid) :
		igb_xsk_pool_disable(adapter, qid);
}

static u16 igb_fill_rx_descs(struct xsk_buff_pool *pool, struct xdp_buff **xdp,
			     union e1000_adv_rx_desc *rx_desc, u16 count)
{
	dma_addr_t dma;
	u16 buffs;
	int i;

	/* nothing to do */
	if (!count)
		return 0;

	buffs = xsk_buff_alloc_batch(pool, xdp, count);
	for (i = 0; i < buffs; i++) {
		dma = xsk_buff_xdp_get_dma(*xdp);
		rx_desc->read.pkt_addr = cpu_to_le64(dma);
		rx_desc->wb.upper.length = 0;

		rx_desc++;
		xdp++;
	}

	return buffs;
}

bool igb_alloc_rx_buffers_zc(struct igb_ring *rx_ring,
			     struct xsk_buff_pool *xsk_pool, u16 count)
{
	u32 nb_buffs_extra = 0, nb_buffs = 0;
	union e1000_adv_rx_desc *rx_desc;
	u16 ntu = rx_ring->next_to_use;
	u16 total_count = count;
	struct xdp_buff **xdp;

	rx_desc = IGB_RX_DESC(rx_ring, ntu);
	xdp = &rx_ring->rx_buffer_info_zc[ntu];

	if (ntu + count >= rx_ring->count) {
		nb_buffs_extra = igb_fill_rx_descs(xsk_pool, xdp, rx_desc,
						   rx_ring->count - ntu);
		if (nb_buffs_extra != rx_ring->count - ntu) {
			ntu += nb_buffs_extra;
			goto exit;
		}
		rx_desc = IGB_RX_DESC(rx_ring, 0);
		xdp = rx_ring->rx_buffer_info_zc;
		ntu = 0;
		count -= nb_buffs_extra;
	}

	nb_buffs = igb_fill_rx_descs(xsk_pool, xdp, rx_desc, count);
	ntu += nb_buffs;
	if (ntu == rx_ring->count)
		ntu = 0;

	/* clear the length for the next_to_use descriptor */
	rx_desc = IGB_RX_DESC(rx_ring, ntu);
	rx_desc->wb.upper.length = 0;

exit:
	if (rx_ring->next_to_use != ntu) {
		rx_ring->next_to_use = ntu;

		/* Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.  (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64).
		 */
		wmb();
		writel(ntu, rx_ring->tail);
	}

	return total_count == (nb_buffs + nb_buffs_extra);
}

void igb_clean_rx_ring_zc(struct igb_ring *rx_ring)
{
	u16 ntc = rx_ring->next_to_clean;
	u16 ntu = rx_ring->next_to_use;

	while (ntc != ntu) {
		struct xdp_buff *xdp = rx_ring->rx_buffer_info_zc[ntc];

		xsk_buff_free(xdp);
		ntc++;
		if (ntc >= rx_ring->count)
			ntc = 0;
	}
}

static struct sk_buff *igb_construct_skb_zc(struct igb_ring *rx_ring,
					    struct xdp_buff *xdp,
					    ktime_t timestamp)
{
	unsigned int totalsize = xdp->data_end - xdp->data_meta;
	unsigned int metasize = xdp->data - xdp->data_meta;
	struct sk_buff *skb;

	net_prefetch(xdp->data_meta);

	/* allocate a skb to store the frags */
	skb = napi_alloc_skb(&rx_ring->q_vector->napi, totalsize);
	if (unlikely(!skb))
		return NULL;

	if (timestamp)
		skb_hwtstamps(skb)->hwtstamp = timestamp;

	memcpy(__skb_put(skb, totalsize), xdp->data_meta,
	       ALIGN(totalsize, sizeof(long)));

	if (metasize) {
		skb_metadata_set(skb, metasize);
		__skb_pull(skb, metasize);
	}

	return skb;
}

static int igb_run_xdp_zc(struct igb_adapter *adapter, struct igb_ring *rx_ring,
			  struct xdp_buff *xdp, struct xsk_buff_pool *xsk_pool,
			  struct bpf_prog *xdp_prog)
{
	int err, result = IGB_XDP_PASS;
	u32 act;

	prefetchw(xdp->data_hard_start); /* xdp_frame write */

	act = bpf_prog_run_xdp(xdp_prog, xdp);

	if (likely(act == XDP_REDIRECT)) {
		err = xdp_do_redirect(adapter->netdev, xdp, xdp_prog);
		if (!err)
			return IGB_XDP_REDIR;

		if (xsk_uses_need_wakeup(xsk_pool) &&
		    err == -ENOBUFS)
			result = IGB_XDP_EXIT;
		else
			result = IGB_XDP_CONSUMED;
		goto out_failure;
	}

	switch (act) {
	case XDP_PASS:
		break;
	case XDP_TX:
		result = igb_xdp_xmit_back(adapter, xdp);
		if (result == IGB_XDP_CONSUMED)
			goto out_failure;
		break;
	default:
		bpf_warn_invalid_xdp_action(adapter->netdev, xdp_prog, act);
		fallthrough;
	case XDP_ABORTED:
out_failure:
		trace_xdp_exception(rx_ring->netdev, xdp_prog, act);
		fallthrough;
	case XDP_DROP:
		result = IGB_XDP_CONSUMED;
		break;
	}

	return result;
}

int igb_clean_rx_irq_zc(struct igb_q_vector *q_vector,
			struct xsk_buff_pool *xsk_pool, const int budget)
{
	struct igb_adapter *adapter = q_vector->adapter;
	unsigned int total_bytes = 0, total_packets = 0;
	struct igb_ring *rx_ring = q_vector->rx.ring;
	u32 ntc = rx_ring->next_to_clean;
	struct bpf_prog *xdp_prog;
	unsigned int xdp_xmit = 0;
	bool failure = false;
	u16 entries_to_alloc;
	struct sk_buff *skb;

	/* xdp_prog cannot be NULL in the ZC path */
	xdp_prog = READ_ONCE(rx_ring->xdp_prog);

	while (likely(total_packets < budget)) {
		union e1000_adv_rx_desc *rx_desc;
		ktime_t timestamp = 0;
		struct xdp_buff *xdp;
		unsigned int size;
		int xdp_res = 0;

		rx_desc = IGB_RX_DESC(rx_ring, ntc);
		size = le16_to_cpu(rx_desc->wb.upper.length);
		if (!size)
			break;

		/* This memory barrier is needed to keep us from reading
		 * any other fields out of the rx_desc until we know the
		 * descriptor has been written back
		 */
		dma_rmb();

		xdp = rx_ring->rx_buffer_info_zc[ntc];
		xsk_buff_set_size(xdp, size);
		xsk_buff_dma_sync_for_cpu(xdp);

		/* pull rx packet timestamp if available and valid */
		if (igb_test_staterr(rx_desc, E1000_RXDADV_STAT_TSIP)) {
			int ts_hdr_len;

			ts_hdr_len = igb_ptp_rx_pktstamp(rx_ring->q_vector,
							 xdp->data,
							 &timestamp);

			xdp->data += ts_hdr_len;
			xdp->data_meta += ts_hdr_len;
			size -= ts_hdr_len;
		}

		xdp_res = igb_run_xdp_zc(adapter, rx_ring, xdp, xsk_pool,
					 xdp_prog);

		if (xdp_res) {
			if (likely(xdp_res & (IGB_XDP_TX | IGB_XDP_REDIR))) {
				xdp_xmit |= xdp_res;
			} else if (xdp_res == IGB_XDP_EXIT) {
				failure = true;
				break;
			} else if (xdp_res == IGB_XDP_CONSUMED) {
				xsk_buff_free(xdp);
			}

			total_packets++;
			total_bytes += size;
			ntc++;
			if (ntc == rx_ring->count)
				ntc = 0;
			continue;
		}

		skb = igb_construct_skb_zc(rx_ring, xdp, timestamp);

		/* exit if we failed to retrieve a buffer */
		if (!skb) {
			rx_ring->rx_stats.alloc_failed++;
			break;
		}

		xsk_buff_free(xdp);
		ntc++;
		if (ntc == rx_ring->count)
			ntc = 0;

		if (eth_skb_pad(skb))
			continue;

		/* probably a little skewed due to removing CRC */
		total_bytes += skb->len;

		/* populate checksum, timestamp, VLAN, and protocol */
		igb_process_skb_fields(rx_ring, rx_desc, skb);

		napi_gro_receive(&q_vector->napi, skb);

		/* update budget accounting */
		total_packets++;
	}

	rx_ring->next_to_clean = ntc;

	if (xdp_xmit)
		igb_finalize_xdp(adapter, xdp_xmit);

	igb_update_rx_stats(q_vector, total_packets, total_bytes);

	entries_to_alloc = igb_desc_unused(rx_ring);
	if (entries_to_alloc >= IGB_RX_BUFFER_WRITE)
		failure |= !igb_alloc_rx_buffers_zc(rx_ring, xsk_pool,
						    entries_to_alloc);

	if (xsk_uses_need_wakeup(xsk_pool)) {
		if (failure || rx_ring->next_to_clean == rx_ring->next_to_use)
			xsk_set_rx_need_wakeup(xsk_pool);
		else
			xsk_clear_rx_need_wakeup(xsk_pool);

		return (int)total_packets;
	}
	return failure ? budget : (int)total_packets;
}

bool igb_xmit_zc(struct igb_ring *tx_ring, struct xsk_buff_pool *xsk_pool)
{
	unsigned int budget = igb_desc_unused(tx_ring);
	u32 cmd_type, olinfo_status, nb_pkts, i = 0;
	struct xdp_desc *descs = xsk_pool->tx_descs;
	union e1000_adv_tx_desc *tx_desc = NULL;
	struct igb_tx_buffer *tx_buffer_info;
	unsigned int total_bytes = 0;
	dma_addr_t dma;

	if (!netif_carrier_ok(tx_ring->netdev))
		return true;

	if (test_bit(IGB_RING_FLAG_TX_DISABLED, &tx_ring->flags))
		return true;

	nb_pkts = xsk_tx_peek_release_desc_batch(xsk_pool, budget);
	if (!nb_pkts)
		return true;

	while (nb_pkts-- > 0) {
		dma = xsk_buff_raw_get_dma(xsk_pool, descs[i].addr);
		xsk_buff_raw_dma_sync_for_device(xsk_pool, dma, descs[i].len);

		tx_buffer_info = &tx_ring->tx_buffer_info[tx_ring->next_to_use];
		tx_buffer_info->bytecount = descs[i].len;
		tx_buffer_info->type = IGB_TYPE_XSK;
		tx_buffer_info->xdpf = NULL;
		tx_buffer_info->gso_segs = 1;
		tx_buffer_info->time_stamp = jiffies;

		tx_desc = IGB_TX_DESC(tx_ring, tx_ring->next_to_use);
		tx_desc->read.buffer_addr = cpu_to_le64(dma);

		/* put descriptor type bits */
		cmd_type = E1000_ADVTXD_DTYP_DATA | E1000_ADVTXD_DCMD_DEXT |
			   E1000_ADVTXD_DCMD_IFCS;
		olinfo_status = descs[i].len << E1000_ADVTXD_PAYLEN_SHIFT;

		/* FIXME: This sets the Report Status (RS) bit for every
		 * descriptor. One nice to have optimization would be to set it
		 * only for the last descriptor in the whole batch. See Intel
		 * ice driver for an example on how to do it.
		 */
		cmd_type |= descs[i].len | IGB_TXD_DCMD;
		tx_desc->read.cmd_type_len = cpu_to_le32(cmd_type);
		tx_desc->read.olinfo_status = cpu_to_le32(olinfo_status);

		total_bytes += descs[i].len;

		i++;
		tx_ring->next_to_use++;
		tx_buffer_info->next_to_watch = tx_desc;
		if (tx_ring->next_to_use == tx_ring->count)
			tx_ring->next_to_use = 0;
	}

	netdev_tx_sent_queue(txring_txq(tx_ring), total_bytes);
	igb_xdp_ring_update_tail(tx_ring);

	return nb_pkts < budget;
}

int igb_xsk_wakeup(struct net_device *dev, u32 qid, u32 flags)
{
	struct igb_adapter *adapter = netdev_priv(dev);
	struct e1000_hw *hw = &adapter->hw;
	struct igb_ring *ring;
	u32 eics = 0;

	if (test_bit(__IGB_DOWN, &adapter->state))
		return -ENETDOWN;

	if (!igb_xdp_is_enabled(adapter))
		return -EINVAL;

	if (qid >= adapter->num_tx_queues)
		return -EINVAL;

	ring = adapter->tx_ring[qid];

	if (test_bit(IGB_RING_FLAG_TX_DISABLED, &ring->flags))
		return -ENETDOWN;

	if (!READ_ONCE(ring->xsk_pool))
		return -EINVAL;

	if (!napi_if_scheduled_mark_missed(&ring->q_vector->napi)) {
		/* Cause software interrupt */
		if (adapter->flags & IGB_FLAG_HAS_MSIX) {
			eics |= ring->q_vector->eims_value;
			wr32(E1000_EICS, eics);
		} else {
			wr32(E1000_ICS, E1000_ICS_RXDMT0);
		}
	}

	return 0;
}
