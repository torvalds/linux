// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2024 Hisilicon Limited.

#include <net/netdev_queues.h>
#include "hbg_common.h"
#include "hbg_irq.h"
#include "hbg_reg.h"
#include "hbg_txrx.h"

#define netdev_get_tx_ring(netdev) \
			(&(((struct hbg_priv *)netdev_priv(netdev))->tx_ring))

#define buffer_to_dma_dir(buffer) (((buffer)->dir == HBG_DIR_RX) ? \
				   DMA_FROM_DEVICE : DMA_TO_DEVICE)

#define hbg_queue_used_num(head, tail, ring) ({ \
	typeof(ring) _ring = (ring); \
	((tail) + _ring->len - (head)) % _ring->len; })
#define hbg_queue_left_num(head, tail, ring) ({ \
	typeof(ring) _r = (ring); \
	_r->len - hbg_queue_used_num((head), (tail), _r) - 1; })
#define hbg_queue_is_empty(head, tail, ring) \
	(hbg_queue_used_num((head), (tail), (ring)) == 0)
#define hbg_queue_next_prt(p, ring) (((p) + 1) % (ring)->len)

#define HBG_TX_STOP_THRS	2
#define HBG_TX_START_THRS	(2 * HBG_TX_STOP_THRS)

static int hbg_dma_map(struct hbg_buffer *buffer)
{
	struct hbg_priv *priv = buffer->priv;

	buffer->skb_dma = dma_map_single(&priv->pdev->dev,
					 buffer->skb->data, buffer->skb_len,
					 buffer_to_dma_dir(buffer));
	if (unlikely(dma_mapping_error(&priv->pdev->dev, buffer->skb_dma)))
		return -ENOMEM;

	return 0;
}

static void hbg_dma_unmap(struct hbg_buffer *buffer)
{
	struct hbg_priv *priv = buffer->priv;

	if (unlikely(!buffer->skb_dma))
		return;

	dma_unmap_single(&priv->pdev->dev, buffer->skb_dma, buffer->skb_len,
			 buffer_to_dma_dir(buffer));
	buffer->skb_dma = 0;
}

static void hbg_init_tx_desc(struct hbg_buffer *buffer,
			     struct hbg_tx_desc *tx_desc)
{
	u32 ip_offset = buffer->skb->network_header - buffer->skb->mac_header;
	u32 word0 = 0;

	word0 |= FIELD_PREP(HBG_TX_DESC_W0_WB_B, HBG_STATUS_ENABLE);
	word0 |= FIELD_PREP(HBG_TX_DESC_W0_IP_OFF_M, ip_offset);
	if (likely(buffer->skb->ip_summed == CHECKSUM_PARTIAL)) {
		word0 |= FIELD_PREP(HBG_TX_DESC_W0_l3_CS_B, HBG_STATUS_ENABLE);
		word0 |= FIELD_PREP(HBG_TX_DESC_W0_l4_CS_B, HBG_STATUS_ENABLE);
	}

	tx_desc->word0 = word0;
	tx_desc->word1 = FIELD_PREP(HBG_TX_DESC_W1_SEND_LEN_M,
				    buffer->skb->len);
	tx_desc->word2 = buffer->skb_dma;
	tx_desc->word3 = buffer->state_dma;
}

netdev_tx_t hbg_net_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct hbg_ring *ring = netdev_get_tx_ring(netdev);
	struct hbg_priv *priv = netdev_priv(netdev);
	/* This smp_load_acquire() pairs with smp_store_release() in
	 * hbg_napi_tx_recycle() called in tx interrupt handle process.
	 */
	u32 ntc = smp_load_acquire(&ring->ntc);
	struct hbg_buffer *buffer;
	struct hbg_tx_desc tx_desc;
	u32 ntu = ring->ntu;

	if (unlikely(!skb->len ||
		     skb->len > hbg_spec_max_frame_len(priv, HBG_DIR_TX))) {
		dev_kfree_skb_any(skb);
		netdev->stats.tx_errors++;
		return NETDEV_TX_OK;
	}

	if (!netif_subqueue_maybe_stop(netdev, 0,
				       hbg_queue_left_num(ntc, ntu, ring),
				       HBG_TX_STOP_THRS, HBG_TX_START_THRS))
		return NETDEV_TX_BUSY;

	buffer = &ring->queue[ntu];
	buffer->skb = skb;
	buffer->skb_len = skb->len;
	if (unlikely(hbg_dma_map(buffer))) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	buffer->state = HBG_TX_STATE_START;
	hbg_init_tx_desc(buffer, &tx_desc);
	hbg_hw_set_tx_desc(priv, &tx_desc);

	/* This smp_store_release() pairs with smp_load_acquire() in
	 * hbg_napi_tx_recycle() called in tx interrupt handle process.
	 */
	smp_store_release(&ring->ntu, hbg_queue_next_prt(ntu, ring));
	dev_sw_netstats_tx_add(netdev, 1, skb->len);
	return NETDEV_TX_OK;
}

static void hbg_buffer_free_skb(struct hbg_buffer *buffer)
{
	if (unlikely(!buffer->skb))
		return;

	dev_kfree_skb_any(buffer->skb);
	buffer->skb = NULL;
}

static void hbg_buffer_free(struct hbg_buffer *buffer)
{
	hbg_dma_unmap(buffer);
	hbg_buffer_free_skb(buffer);
}

static int hbg_napi_tx_recycle(struct napi_struct *napi, int budget)
{
	struct hbg_ring *ring = container_of(napi, struct hbg_ring, napi);
	/* This smp_load_acquire() pairs with smp_store_release() in
	 * hbg_net_start_xmit() called in xmit process.
	 */
	u32 ntu = smp_load_acquire(&ring->ntu);
	struct hbg_priv *priv = ring->priv;
	struct hbg_buffer *buffer;
	u32 ntc = ring->ntc;
	int packet_done = 0;

	/* We need do cleanup even if budget is 0.
	 * Per NAPI documentation budget is for Rx.
	 * So We hardcode the amount of work Tx NAPI does to 128.
	 */
	budget = 128;
	while (packet_done < budget) {
		if (unlikely(hbg_queue_is_empty(ntc, ntu, ring)))
			break;

		/* make sure HW write desc complete */
		dma_rmb();

		buffer = &ring->queue[ntc];
		if (buffer->state != HBG_TX_STATE_COMPLETE)
			break;

		hbg_buffer_free(buffer);
		ntc = hbg_queue_next_prt(ntc, ring);
		packet_done++;
	}

	/* This smp_store_release() pairs with smp_load_acquire() in
	 * hbg_net_start_xmit() called in xmit process.
	 */
	smp_store_release(&ring->ntc, ntc);
	netif_wake_queue(priv->netdev);

	if (likely(packet_done < budget &&
		   napi_complete_done(napi, packet_done)))
		hbg_hw_irq_enable(priv, HBG_INT_MSK_TX_B, true);

	return packet_done;
}

static void hbg_ring_uninit(struct hbg_ring *ring)
{
	struct hbg_buffer *buffer;
	u32 i;

	if (!ring->queue)
		return;

	napi_disable(&ring->napi);
	netif_napi_del(&ring->napi);

	for (i = 0; i < ring->len; i++) {
		buffer = &ring->queue[i];
		hbg_buffer_free(buffer);
		buffer->ring = NULL;
		buffer->priv = NULL;
	}

	dma_free_coherent(&ring->priv->pdev->dev,
			  ring->len * sizeof(*ring->queue),
			  ring->queue, ring->queue_dma);
	ring->queue = NULL;
	ring->queue_dma = 0;
	ring->len = 0;
	ring->priv = NULL;
}

static int hbg_ring_init(struct hbg_priv *priv, struct hbg_ring *ring,
			 int (*napi_poll)(struct napi_struct *, int),
			 enum hbg_dir dir)
{
	struct hbg_buffer *buffer;
	u32 i, len;

	len = hbg_get_spec_fifo_max_num(priv, dir) + 1;
	ring->queue = dma_alloc_coherent(&priv->pdev->dev,
					 len * sizeof(*ring->queue),
					 &ring->queue_dma, GFP_KERNEL);
	if (!ring->queue)
		return -ENOMEM;

	for (i = 0; i < len; i++) {
		buffer = &ring->queue[i];
		buffer->skb_len = 0;
		buffer->dir = dir;
		buffer->ring = ring;
		buffer->priv = priv;
		buffer->state_dma = ring->queue_dma + (i * sizeof(*buffer));
	}

	ring->dir = dir;
	ring->priv = priv;
	ring->ntc = 0;
	ring->ntu = 0;
	ring->len = len;

	netif_napi_add_tx(priv->netdev, &ring->napi, napi_poll);
	napi_enable(&ring->napi);
	return 0;
}

static int hbg_tx_ring_init(struct hbg_priv *priv)
{
	struct hbg_ring *tx_ring = &priv->tx_ring;

	if (!tx_ring->tout_log_buf)
		tx_ring->tout_log_buf = devm_kmalloc(&priv->pdev->dev,
						     HBG_TX_TIMEOUT_BUF_LEN,
						     GFP_KERNEL);

	if (!tx_ring->tout_log_buf)
		return -ENOMEM;

	return hbg_ring_init(priv, tx_ring, hbg_napi_tx_recycle, HBG_DIR_TX);
}

int hbg_txrx_init(struct hbg_priv *priv)
{
	int ret;

	ret = hbg_tx_ring_init(priv);
	if (ret)
		dev_err(&priv->pdev->dev,
			"failed to init tx ring, ret = %d\n", ret);

	return ret;
}

void hbg_txrx_uninit(struct hbg_priv *priv)
{
	hbg_ring_uninit(&priv->tx_ring);
}
