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
#define hbg_queue_is_full(head, tail, ring) \
	(hbg_queue_left_num((head), (tail), (ring)) == 0)
#define hbg_queue_next_prt(p, ring) (((p) + 1) % (ring)->len)
#define hbg_queue_move_next(p, ring) ({ \
	typeof(ring) _ring = (ring); \
	_ring->p = hbg_queue_next_prt(_ring->p, _ring); })

#define HBG_TX_STOP_THRS	2
#define HBG_TX_START_THRS	(2 * HBG_TX_STOP_THRS)

static int hbg_dma_map(struct hbg_buffer *buffer)
{
	struct hbg_priv *priv = buffer->priv;

	buffer->skb_dma = dma_map_single(&priv->pdev->dev,
					 buffer->skb->data, buffer->skb_len,
					 buffer_to_dma_dir(buffer));
	if (unlikely(dma_mapping_error(&priv->pdev->dev, buffer->skb_dma))) {
		if (buffer->dir == HBG_DIR_RX)
			priv->stats.rx_dma_err_cnt++;
		else
			priv->stats.tx_dma_err_cnt++;

		return -ENOMEM;
	}

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

static int hbg_buffer_alloc_skb(struct hbg_buffer *buffer)
{
	u32 len = hbg_spec_max_frame_len(buffer->priv, buffer->dir);
	struct hbg_priv *priv = buffer->priv;

	buffer->skb = netdev_alloc_skb(priv->netdev, len);
	if (unlikely(!buffer->skb))
		return -ENOMEM;

	buffer->skb_len = len;
	memset(buffer->skb->data, 0, HBG_PACKET_HEAD_SIZE);
	return 0;
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

static bool hbg_rx_check_l3l4_error(struct hbg_priv *priv,
				    struct hbg_rx_desc *desc,
				    struct sk_buff *skb)
{
	bool rx_checksum_offload = !!(priv->netdev->features & NETIF_F_RXCSUM);

	skb->ip_summed = rx_checksum_offload ?
			 CHECKSUM_UNNECESSARY : CHECKSUM_NONE;

	if (likely(!FIELD_GET(HBG_RX_DESC_W4_L3_ERR_CODE_M, desc->word4) &&
		   !FIELD_GET(HBG_RX_DESC_W4_L4_ERR_CODE_M, desc->word4)))
		return true;

	switch (FIELD_GET(HBG_RX_DESC_W4_L3_ERR_CODE_M, desc->word4)) {
	case HBG_L3_OK:
		break;
	case HBG_L3_WRONG_HEAD:
		priv->stats.rx_desc_l3_wrong_head_cnt++;
		return false;
	case HBG_L3_CSUM_ERR:
		skb->ip_summed = CHECKSUM_NONE;
		priv->stats.rx_desc_l3_csum_err_cnt++;

		/* Don't drop packets on csum validation failure,
		 * suggest by Jakub
		 */
		break;
	case HBG_L3_LEN_ERR:
		priv->stats.rx_desc_l3_len_err_cnt++;
		return false;
	case HBG_L3_ZERO_TTL:
		priv->stats.rx_desc_l3_zero_ttl_cnt++;
		return false;
	default:
		priv->stats.rx_desc_l3_other_cnt++;
		return false;
	}

	switch (FIELD_GET(HBG_RX_DESC_W4_L4_ERR_CODE_M, desc->word4)) {
	case HBG_L4_OK:
		break;
	case HBG_L4_WRONG_HEAD:
		priv->stats.rx_desc_l4_wrong_head_cnt++;
		return false;
	case HBG_L4_LEN_ERR:
		priv->stats.rx_desc_l4_len_err_cnt++;
		return false;
	case HBG_L4_CSUM_ERR:
		skb->ip_summed = CHECKSUM_NONE;
		priv->stats.rx_desc_l4_csum_err_cnt++;

		/* Don't drop packets on csum validation failure,
		 * suggest by Jakub
		 */
		break;
	case HBG_L4_ZERO_PORT_NUM:
		priv->stats.rx_desc_l4_zero_port_num_cnt++;
		return false;
	default:
		priv->stats.rx_desc_l4_other_cnt++;
		return false;
	}

	return true;
}

static void hbg_update_rx_ip_protocol_stats(struct hbg_priv *priv,
					    struct hbg_rx_desc *desc)
{
	if (unlikely(!FIELD_GET(HBG_RX_DESC_W4_IP_TCP_UDP_M, desc->word4))) {
		priv->stats.rx_desc_no_ip_pkt_cnt++;
		return;
	}

	if (unlikely(FIELD_GET(HBG_RX_DESC_W4_IP_VERSION_ERR_B, desc->word4))) {
		priv->stats.rx_desc_ip_ver_err_cnt++;
		return;
	}

	/* 0:ipv4, 1:ipv6 */
	if (FIELD_GET(HBG_RX_DESC_W4_IP_VERSION_B, desc->word4))
		priv->stats.rx_desc_ipv6_pkt_cnt++;
	else
		priv->stats.rx_desc_ipv4_pkt_cnt++;

	switch (FIELD_GET(HBG_RX_DESC_W4_IP_TCP_UDP_M, desc->word4)) {
	case HBG_IP_PKT:
		priv->stats.rx_desc_ip_pkt_cnt++;
		if (FIELD_GET(HBG_RX_DESC_W4_OPT_B, desc->word4))
			priv->stats.rx_desc_ip_opt_pkt_cnt++;
		if (FIELD_GET(HBG_RX_DESC_W4_FRAG_B, desc->word4))
			priv->stats.rx_desc_frag_cnt++;

		if (FIELD_GET(HBG_RX_DESC_W4_ICMP_B, desc->word4))
			priv->stats.rx_desc_icmp_pkt_cnt++;
		else if (FIELD_GET(HBG_RX_DESC_W4_IPSEC_B, desc->word4))
			priv->stats.rx_desc_ipsec_pkt_cnt++;
		break;
	case HBG_TCP_PKT:
		priv->stats.rx_desc_tcp_pkt_cnt++;
		break;
	case HBG_UDP_PKT:
		priv->stats.rx_desc_udp_pkt_cnt++;
		break;
	default:
		priv->stats.rx_desc_no_ip_pkt_cnt++;
		break;
	}
}

static void hbg_update_rx_protocol_stats(struct hbg_priv *priv,
					 struct hbg_rx_desc *desc)
{
	if (unlikely(!FIELD_GET(HBG_RX_DESC_W4_IDX_MATCH_B, desc->word4))) {
		priv->stats.rx_desc_key_not_match_cnt++;
		return;
	}

	if (FIELD_GET(HBG_RX_DESC_W4_BRD_CST_B, desc->word4))
		priv->stats.rx_desc_broadcast_pkt_cnt++;
	else if (FIELD_GET(HBG_RX_DESC_W4_MUL_CST_B, desc->word4))
		priv->stats.rx_desc_multicast_pkt_cnt++;

	if (FIELD_GET(HBG_RX_DESC_W4_VLAN_FLAG_B, desc->word4))
		priv->stats.rx_desc_vlan_pkt_cnt++;

	if (FIELD_GET(HBG_RX_DESC_W4_ARP_B, desc->word4)) {
		priv->stats.rx_desc_arp_pkt_cnt++;
		return;
	} else if (FIELD_GET(HBG_RX_DESC_W4_RARP_B, desc->word4)) {
		priv->stats.rx_desc_rarp_pkt_cnt++;
		return;
	}

	hbg_update_rx_ip_protocol_stats(priv, desc);
}

static bool hbg_rx_pkt_check(struct hbg_priv *priv, struct hbg_rx_desc *desc,
			     struct sk_buff *skb)
{
	if (unlikely(FIELD_GET(HBG_RX_DESC_W2_PKT_LEN_M, desc->word2) >
		     priv->dev_specs.max_frame_len)) {
		priv->stats.rx_desc_pkt_len_err_cnt++;
		return false;
	}

	if (unlikely(FIELD_GET(HBG_RX_DESC_W2_PORT_NUM_M, desc->word2) !=
		     priv->dev_specs.mac_id ||
		     FIELD_GET(HBG_RX_DESC_W4_DROP_B, desc->word4))) {
		priv->stats.rx_desc_drop++;
		return false;
	}

	if (unlikely(FIELD_GET(HBG_RX_DESC_W4_L2_ERR_B, desc->word4))) {
		priv->stats.rx_desc_l2_err_cnt++;
		return false;
	}

	if (unlikely(!hbg_rx_check_l3l4_error(priv, desc, skb))) {
		priv->stats.rx_desc_l3l4_err_cnt++;
		return false;
	}

	hbg_update_rx_protocol_stats(priv, desc);
	return true;
}

static int hbg_rx_fill_one_buffer(struct hbg_priv *priv)
{
	struct hbg_ring *ring = &priv->rx_ring;
	struct hbg_buffer *buffer;
	int ret;

	if (hbg_queue_is_full(ring->ntc, ring->ntu, ring))
		return 0;

	buffer = &ring->queue[ring->ntu];
	ret = hbg_buffer_alloc_skb(buffer);
	if (unlikely(ret))
		return ret;

	ret = hbg_dma_map(buffer);
	if (unlikely(ret)) {
		hbg_buffer_free_skb(buffer);
		return ret;
	}

	hbg_hw_fill_buffer(priv, buffer->skb_dma);
	hbg_queue_move_next(ntu, ring);
	return 0;
}

static bool hbg_sync_data_from_hw(struct hbg_priv *priv,
				  struct hbg_buffer *buffer)
{
	struct hbg_rx_desc *rx_desc;

	/* make sure HW write desc complete */
	dma_rmb();

	dma_sync_single_for_cpu(&priv->pdev->dev, buffer->skb_dma,
				buffer->skb_len, DMA_FROM_DEVICE);

	rx_desc = (struct hbg_rx_desc *)buffer->skb->data;
	return FIELD_GET(HBG_RX_DESC_W2_PKT_LEN_M, rx_desc->word2) != 0;
}

static int hbg_napi_rx_poll(struct napi_struct *napi, int budget)
{
	struct hbg_ring *ring = container_of(napi, struct hbg_ring, napi);
	struct hbg_priv *priv = ring->priv;
	struct hbg_rx_desc *rx_desc;
	struct hbg_buffer *buffer;
	u32 packet_done = 0;
	u32 pkt_len;

	while (packet_done < budget) {
		if (unlikely(hbg_queue_is_empty(ring->ntc, ring->ntu, ring)))
			break;

		buffer = &ring->queue[ring->ntc];
		if (unlikely(!buffer->skb))
			goto next_buffer;

		if (unlikely(!hbg_sync_data_from_hw(priv, buffer)))
			break;
		rx_desc = (struct hbg_rx_desc *)buffer->skb->data;
		pkt_len = FIELD_GET(HBG_RX_DESC_W2_PKT_LEN_M, rx_desc->word2);

		if (unlikely(!hbg_rx_pkt_check(priv, rx_desc, buffer->skb))) {
			hbg_buffer_free(buffer);
			goto next_buffer;
		}

		hbg_dma_unmap(buffer);
		skb_reserve(buffer->skb, HBG_PACKET_HEAD_SIZE + NET_IP_ALIGN);
		skb_put(buffer->skb, pkt_len);
		buffer->skb->protocol = eth_type_trans(buffer->skb,
						       priv->netdev);

		dev_sw_netstats_rx_add(priv->netdev, pkt_len);
		napi_gro_receive(napi, buffer->skb);
		buffer->skb = NULL;

next_buffer:
		hbg_rx_fill_one_buffer(priv);
		hbg_queue_move_next(ntc, ring);
		packet_done++;
	}

	if (likely(packet_done < budget &&
		   napi_complete_done(napi, packet_done)))
		hbg_hw_irq_enable(priv, HBG_INT_MSK_RX_B, true);

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

	if (dir == HBG_DIR_TX)
		netif_napi_add_tx(priv->netdev, &ring->napi, napi_poll);
	else
		netif_napi_add(priv->netdev, &ring->napi, napi_poll);

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

static int hbg_rx_ring_init(struct hbg_priv *priv)
{
	int ret;
	u32 i;

	ret = hbg_ring_init(priv, &priv->rx_ring, hbg_napi_rx_poll, HBG_DIR_RX);
	if (ret)
		return ret;

	for (i = 0; i < priv->rx_ring.len - 1; i++) {
		ret = hbg_rx_fill_one_buffer(priv);
		if (ret) {
			hbg_ring_uninit(&priv->rx_ring);
			return ret;
		}
	}

	return 0;
}

int hbg_txrx_init(struct hbg_priv *priv)
{
	int ret;

	ret = hbg_tx_ring_init(priv);
	if (ret) {
		dev_err(&priv->pdev->dev,
			"failed to init tx ring, ret = %d\n", ret);
		return ret;
	}

	ret = hbg_rx_ring_init(priv);
	if (ret) {
		dev_err(&priv->pdev->dev,
			"failed to init rx ring, ret = %d\n", ret);
		hbg_ring_uninit(&priv->tx_ring);
	}

	return ret;
}

void hbg_txrx_uninit(struct hbg_priv *priv)
{
	hbg_ring_uninit(&priv->tx_ring);
	hbg_ring_uninit(&priv->rx_ring);
}
