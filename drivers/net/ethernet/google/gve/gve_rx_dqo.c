// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

#include "gve.h"
#include "gve_dqo.h"
#include "gve_adminq.h"
#include "gve_utils.h"
#include <linux/bpf.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/ip6_checksum.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <net/xdp_sock_drv.h>

static void gve_rx_free_hdr_bufs(struct gve_priv *priv, struct gve_rx_ring *rx)
{
	struct device *hdev = &priv->pdev->dev;
	int buf_count = rx->dqo.bufq.mask + 1;

	if (rx->dqo.hdr_bufs.data) {
		dma_free_coherent(hdev, priv->header_buf_size * buf_count,
				  rx->dqo.hdr_bufs.data, rx->dqo.hdr_bufs.addr);
		rx->dqo.hdr_bufs.data = NULL;
	}
}

static void gve_rx_init_ring_state_dqo(struct gve_rx_ring *rx,
				       const u32 buffer_queue_slots,
				       const u32 completion_queue_slots)
{
	int i;

	/* Set buffer queue state */
	rx->dqo.bufq.mask = buffer_queue_slots - 1;
	rx->dqo.bufq.head = 0;
	rx->dqo.bufq.tail = 0;

	/* Set completion queue state */
	rx->dqo.complq.num_free_slots = completion_queue_slots;
	rx->dqo.complq.mask = completion_queue_slots - 1;
	rx->dqo.complq.cur_gen_bit = 0;
	rx->dqo.complq.head = 0;

	/* Set RX SKB context */
	rx->ctx.skb_head = NULL;
	rx->ctx.skb_tail = NULL;

	/* Set up linked list of buffer IDs */
	if (rx->dqo.buf_states) {
		for (i = 0; i < rx->dqo.num_buf_states - 1; i++)
			rx->dqo.buf_states[i].next = i + 1;
		rx->dqo.buf_states[rx->dqo.num_buf_states - 1].next = -1;
	}

	rx->dqo.free_buf_states = 0;
	rx->dqo.recycled_buf_states.head = -1;
	rx->dqo.recycled_buf_states.tail = -1;
	rx->dqo.used_buf_states.head = -1;
	rx->dqo.used_buf_states.tail = -1;
}

static void gve_rx_reset_ring_dqo(struct gve_priv *priv, int idx)
{
	struct gve_rx_ring *rx = &priv->rx[idx];
	size_t size;
	int i;

	const u32 buffer_queue_slots = priv->rx_desc_cnt;
	const u32 completion_queue_slots = priv->rx_desc_cnt;

	/* Reset buffer queue */
	if (rx->dqo.bufq.desc_ring) {
		size = sizeof(rx->dqo.bufq.desc_ring[0]) *
			buffer_queue_slots;
		memset(rx->dqo.bufq.desc_ring, 0, size);
	}

	/* Reset completion queue */
	if (rx->dqo.complq.desc_ring) {
		size = sizeof(rx->dqo.complq.desc_ring[0]) *
			completion_queue_slots;
		memset(rx->dqo.complq.desc_ring, 0, size);
	}

	/* Reset q_resources */
	if (rx->q_resources)
		memset(rx->q_resources, 0, sizeof(*rx->q_resources));

	/* Reset buf states */
	if (rx->dqo.buf_states) {
		for (i = 0; i < rx->dqo.num_buf_states; i++) {
			struct gve_rx_buf_state_dqo *bs = &rx->dqo.buf_states[i];

			if (rx->dqo.page_pool)
				gve_free_to_page_pool(rx, bs, false);
			else
				gve_free_qpl_page_dqo(bs);
		}
	}

	gve_rx_init_ring_state_dqo(rx, buffer_queue_slots,
				   completion_queue_slots);
}

void gve_rx_stop_ring_dqo(struct gve_priv *priv, int idx)
{
	int ntfy_idx = gve_rx_idx_to_ntfy(priv, idx);
	struct gve_rx_ring *rx = &priv->rx[idx];

	if (!gve_rx_was_added_to_block(priv, idx))
		return;

	if (rx->dqo.page_pool)
		page_pool_disable_direct_recycling(rx->dqo.page_pool);
	gve_remove_napi(priv, ntfy_idx);
	gve_rx_remove_from_block(priv, idx);
	gve_rx_reset_ring_dqo(priv, idx);
}

void gve_rx_free_ring_dqo(struct gve_priv *priv, struct gve_rx_ring *rx,
			  struct gve_rx_alloc_rings_cfg *cfg)
{
	struct device *hdev = &priv->pdev->dev;
	size_t completion_queue_slots;
	size_t buffer_queue_slots;
	int idx = rx->q_num;
	size_t size;
	u32 qpl_id;
	int i;

	completion_queue_slots = rx->dqo.complq.mask + 1;
	buffer_queue_slots = rx->dqo.bufq.mask + 1;

	if (rx->q_resources) {
		dma_free_coherent(hdev, sizeof(*rx->q_resources),
				  rx->q_resources, rx->q_resources_bus);
		rx->q_resources = NULL;
	}

	for (i = 0; i < rx->dqo.num_buf_states; i++) {
		struct gve_rx_buf_state_dqo *bs = &rx->dqo.buf_states[i];

		if (rx->dqo.page_pool)
			gve_free_to_page_pool(rx, bs, false);
		else
			gve_free_qpl_page_dqo(bs);
		if (gve_buf_state_is_allocated(rx, bs) && bs->xsk_buff) {
			xsk_buff_free(bs->xsk_buff);
			bs->xsk_buff = NULL;
		}
	}

	if (rx->dqo.qpl) {
		qpl_id = gve_get_rx_qpl_id(cfg->qcfg_tx, rx->q_num);
		gve_free_queue_page_list(priv, rx->dqo.qpl, qpl_id);
		rx->dqo.qpl = NULL;
	}

	if (rx->dqo.bufq.desc_ring) {
		size = sizeof(rx->dqo.bufq.desc_ring[0]) * buffer_queue_slots;
		dma_free_coherent(hdev, size, rx->dqo.bufq.desc_ring,
				  rx->dqo.bufq.bus);
		rx->dqo.bufq.desc_ring = NULL;
	}

	if (rx->dqo.complq.desc_ring) {
		size = sizeof(rx->dqo.complq.desc_ring[0]) *
			completion_queue_slots;
		dma_free_coherent(hdev, size, rx->dqo.complq.desc_ring,
				  rx->dqo.complq.bus);
		rx->dqo.complq.desc_ring = NULL;
	}

	kvfree(rx->dqo.buf_states);
	rx->dqo.buf_states = NULL;

	if (rx->dqo.page_pool) {
		page_pool_destroy(rx->dqo.page_pool);
		rx->dqo.page_pool = NULL;
	}

	gve_rx_free_hdr_bufs(priv, rx);

	netif_dbg(priv, drv, priv->dev, "freed rx ring %d\n", idx);
}

static int gve_rx_alloc_hdr_bufs(struct gve_priv *priv, struct gve_rx_ring *rx,
				 const u32 buf_count)
{
	struct device *hdev = &priv->pdev->dev;

	rx->dqo.hdr_bufs.data = dma_alloc_coherent(hdev, priv->header_buf_size * buf_count,
						   &rx->dqo.hdr_bufs.addr, GFP_KERNEL);
	if (!rx->dqo.hdr_bufs.data)
		return -ENOMEM;

	return 0;
}

void gve_rx_start_ring_dqo(struct gve_priv *priv, int idx)
{
	int ntfy_idx = gve_rx_idx_to_ntfy(priv, idx);

	gve_rx_add_to_block(priv, idx);
	gve_add_napi(priv, ntfy_idx, gve_napi_poll_dqo);
}

int gve_rx_alloc_ring_dqo(struct gve_priv *priv,
			  struct gve_rx_alloc_rings_cfg *cfg,
			  struct gve_rx_ring *rx,
			  int idx)
{
	struct device *hdev = &priv->pdev->dev;
	struct page_pool *pool;
	int qpl_page_cnt;
	size_t size;
	u32 qpl_id;

	const u32 buffer_queue_slots = cfg->ring_size;
	const u32 completion_queue_slots = cfg->ring_size;

	netif_dbg(priv, drv, priv->dev, "allocating rx ring DQO\n");

	memset(rx, 0, sizeof(*rx));
	rx->gve = priv;
	rx->q_num = idx;
	rx->packet_buffer_size = cfg->packet_buffer_size;

	if (cfg->xdp) {
		rx->packet_buffer_truesize = GVE_XDP_RX_BUFFER_SIZE_DQO;
		rx->rx_headroom = XDP_PACKET_HEADROOM;
	} else {
		rx->packet_buffer_truesize = rx->packet_buffer_size;
		rx->rx_headroom = 0;
	}

	rx->dqo.num_buf_states = cfg->raw_addressing ? buffer_queue_slots :
		gve_get_rx_pages_per_qpl_dqo(cfg->ring_size);
	rx->dqo.buf_states = kvcalloc_node(rx->dqo.num_buf_states,
					   sizeof(rx->dqo.buf_states[0]),
					   GFP_KERNEL, priv->numa_node);
	if (!rx->dqo.buf_states)
		return -ENOMEM;

	/* Allocate header buffers for header-split */
	if (cfg->enable_header_split)
		if (gve_rx_alloc_hdr_bufs(priv, rx, buffer_queue_slots))
			goto err;

	/* Allocate RX completion queue */
	size = sizeof(rx->dqo.complq.desc_ring[0]) *
		completion_queue_slots;
	rx->dqo.complq.desc_ring =
		dma_alloc_coherent(hdev, size, &rx->dqo.complq.bus, GFP_KERNEL);
	if (!rx->dqo.complq.desc_ring)
		goto err;

	/* Allocate RX buffer queue */
	size = sizeof(rx->dqo.bufq.desc_ring[0]) * buffer_queue_slots;
	rx->dqo.bufq.desc_ring =
		dma_alloc_coherent(hdev, size, &rx->dqo.bufq.bus, GFP_KERNEL);
	if (!rx->dqo.bufq.desc_ring)
		goto err;

	if (cfg->raw_addressing) {
		pool = gve_rx_create_page_pool(priv, rx, cfg->xdp);
		if (IS_ERR(pool))
			goto err;

		rx->dqo.page_pool = pool;
	} else {
		qpl_id = gve_get_rx_qpl_id(cfg->qcfg_tx, rx->q_num);
		qpl_page_cnt = gve_get_rx_pages_per_qpl_dqo(cfg->ring_size);

		rx->dqo.qpl = gve_alloc_queue_page_list(priv, qpl_id,
							qpl_page_cnt);
		if (!rx->dqo.qpl)
			goto err;
		rx->dqo.next_qpl_page_idx = 0;
	}

	rx->q_resources = dma_alloc_coherent(hdev, sizeof(*rx->q_resources),
					     &rx->q_resources_bus, GFP_KERNEL);
	if (!rx->q_resources)
		goto err;

	gve_rx_init_ring_state_dqo(rx, buffer_queue_slots,
				   completion_queue_slots);

	return 0;

err:
	gve_rx_free_ring_dqo(priv, rx, cfg);
	return -ENOMEM;
}

void gve_rx_write_doorbell_dqo(const struct gve_priv *priv, int queue_idx)
{
	const struct gve_rx_ring *rx = &priv->rx[queue_idx];
	u64 index = be32_to_cpu(rx->q_resources->db_index);

	iowrite32(rx->dqo.bufq.tail, &priv->db_bar2[index]);
}

int gve_rx_alloc_rings_dqo(struct gve_priv *priv,
			   struct gve_rx_alloc_rings_cfg *cfg)
{
	struct gve_rx_ring *rx;
	int err;
	int i;

	rx = kvcalloc(cfg->qcfg_rx->max_queues, sizeof(struct gve_rx_ring),
		      GFP_KERNEL);
	if (!rx)
		return -ENOMEM;

	for (i = 0; i < cfg->qcfg_rx->num_queues; i++) {
		err = gve_rx_alloc_ring_dqo(priv, cfg, &rx[i], i);
		if (err) {
			netif_err(priv, drv, priv->dev,
				  "Failed to alloc rx ring=%d: err=%d\n",
				  i, err);
			goto err;
		}
	}

	cfg->rx = rx;
	return 0;

err:
	for (i--; i >= 0; i--)
		gve_rx_free_ring_dqo(priv, &rx[i], cfg);
	kvfree(rx);
	return err;
}

void gve_rx_free_rings_dqo(struct gve_priv *priv,
			   struct gve_rx_alloc_rings_cfg *cfg)
{
	struct gve_rx_ring *rx = cfg->rx;
	int i;

	if (!rx)
		return;

	for (i = 0; i < cfg->qcfg_rx->num_queues;  i++)
		gve_rx_free_ring_dqo(priv, &rx[i], cfg);

	kvfree(rx);
	cfg->rx = NULL;
}

void gve_rx_post_buffers_dqo(struct gve_rx_ring *rx)
{
	struct gve_rx_compl_queue_dqo *complq = &rx->dqo.complq;
	struct gve_rx_buf_queue_dqo *bufq = &rx->dqo.bufq;
	struct gve_priv *priv = rx->gve;
	u32 num_avail_slots;
	u32 num_full_slots;
	u32 num_posted = 0;

	num_full_slots = (bufq->tail - bufq->head) & bufq->mask;
	num_avail_slots = bufq->mask - num_full_slots;

	num_avail_slots = min_t(u32, num_avail_slots, complq->num_free_slots);
	while (num_posted < num_avail_slots) {
		struct gve_rx_desc_dqo *desc = &bufq->desc_ring[bufq->tail];

		if (unlikely(gve_alloc_buffer(rx, desc))) {
			u64_stats_update_begin(&rx->statss);
			rx->rx_buf_alloc_fail++;
			u64_stats_update_end(&rx->statss);
			break;
		}

		if (rx->dqo.hdr_bufs.data)
			desc->header_buf_addr =
				cpu_to_le64(rx->dqo.hdr_bufs.addr +
					    priv->header_buf_size * bufq->tail);

		bufq->tail = (bufq->tail + 1) & bufq->mask;
		complq->num_free_slots--;
		num_posted++;

		if ((bufq->tail & (GVE_RX_BUF_THRESH_DQO - 1)) == 0)
			gve_rx_write_doorbell_dqo(priv, rx->q_num);
	}

	rx->fill_cnt += num_posted;
}

static void gve_rx_skb_csum(struct sk_buff *skb,
			    const struct gve_rx_compl_desc_dqo *desc,
			    struct gve_ptype ptype)
{
	skb->ip_summed = CHECKSUM_NONE;

	/* HW did not identify and process L3 and L4 headers. */
	if (unlikely(!desc->l3_l4_processed))
		return;

	if (ptype.l3_type == GVE_L3_TYPE_IPV4) {
		if (unlikely(desc->csum_ip_err || desc->csum_external_ip_err))
			return;
	} else if (ptype.l3_type == GVE_L3_TYPE_IPV6) {
		/* Checksum should be skipped if this flag is set. */
		if (unlikely(desc->ipv6_ex_add))
			return;
	}

	if (unlikely(desc->csum_l4_err))
		return;

	switch (ptype.l4_type) {
	case GVE_L4_TYPE_TCP:
	case GVE_L4_TYPE_UDP:
	case GVE_L4_TYPE_ICMP:
	case GVE_L4_TYPE_SCTP:
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		break;
	default:
		break;
	}
}

static void gve_rx_skb_hash(struct sk_buff *skb,
			    const struct gve_rx_compl_desc_dqo *compl_desc,
			    struct gve_ptype ptype)
{
	enum pkt_hash_types hash_type = PKT_HASH_TYPE_L2;

	if (ptype.l4_type != GVE_L4_TYPE_UNKNOWN)
		hash_type = PKT_HASH_TYPE_L4;
	else if (ptype.l3_type != GVE_L3_TYPE_UNKNOWN)
		hash_type = PKT_HASH_TYPE_L3;

	skb_set_hash(skb, le32_to_cpu(compl_desc->hash), hash_type);
}

/* Expand the hardware timestamp to the full 64 bits of width, and add it to the
 * skb.
 *
 * This algorithm works by using the passed hardware timestamp to generate a
 * diff relative to the last read of the nic clock. This diff can be positive or
 * negative, as it is possible that we have read the clock more recently than
 * the hardware has received this packet. To detect this, we use the high bit of
 * the diff, and assume that the read is more recent if the high bit is set. In
 * this case we invert the process.
 *
 * Note that this means if the time delta between packet reception and the last
 * clock read is greater than ~2 seconds, this will provide invalid results.
 */
static void gve_rx_skb_hwtstamp(struct gve_rx_ring *rx,
				const struct gve_rx_compl_desc_dqo *desc)
{
	u64 last_read = READ_ONCE(rx->gve->last_sync_nic_counter);
	struct sk_buff *skb = rx->ctx.skb_head;
	u32 ts, low;
	s32 diff;

	if (desc->ts_sub_nsecs_low & GVE_DQO_RX_HWTSTAMP_VALID) {
		ts = le32_to_cpu(desc->ts);
		low = (u32)last_read;
		diff = ts - low;
		skb_hwtstamps(skb)->hwtstamp = ns_to_ktime(last_read + diff);
	}
}

static void gve_rx_free_skb(struct napi_struct *napi, struct gve_rx_ring *rx)
{
	if (!rx->ctx.skb_head)
		return;

	if (rx->ctx.skb_head == napi->skb)
		napi->skb = NULL;
	dev_kfree_skb_any(rx->ctx.skb_head);
	rx->ctx.skb_head = NULL;
	rx->ctx.skb_tail = NULL;
}

static bool gve_rx_should_trigger_copy_ondemand(struct gve_rx_ring *rx)
{
	if (!rx->dqo.qpl)
		return false;
	if (rx->dqo.used_buf_states_cnt <
		     (rx->dqo.num_buf_states -
		     GVE_DQO_QPL_ONDEMAND_ALLOC_THRESHOLD))
		return false;
	return true;
}

static int gve_rx_copy_ondemand(struct gve_rx_ring *rx,
				struct gve_rx_buf_state_dqo *buf_state,
				u16 buf_len)
{
	struct page *page = alloc_pages_node(rx->gve->numa_node, GFP_ATOMIC, 0);
	int num_frags;

	if (!page)
		return -ENOMEM;

	memcpy(page_address(page),
	       buf_state->page_info.page_address +
	       buf_state->page_info.page_offset,
	       buf_len);
	num_frags = skb_shinfo(rx->ctx.skb_tail)->nr_frags;
	skb_add_rx_frag(rx->ctx.skb_tail, num_frags, page,
			0, buf_len, PAGE_SIZE);

	u64_stats_update_begin(&rx->statss);
	rx->rx_frag_alloc_cnt++;
	u64_stats_update_end(&rx->statss);
	/* Return unused buffer. */
	gve_enqueue_buf_state(rx, &rx->dqo.recycled_buf_states, buf_state);
	return 0;
}

static void gve_skb_add_rx_frag(struct gve_rx_ring *rx,
				struct gve_rx_buf_state_dqo *buf_state,
				int num_frags, u16 buf_len)
{
	if (rx->dqo.page_pool) {
		skb_add_rx_frag_netmem(rx->ctx.skb_tail, num_frags,
				       buf_state->page_info.netmem,
				       buf_state->page_info.page_offset +
				       buf_state->page_info.pad, buf_len,
				       buf_state->page_info.buf_size);
	} else {
		skb_add_rx_frag(rx->ctx.skb_tail, num_frags,
				buf_state->page_info.page,
				buf_state->page_info.page_offset +
				buf_state->page_info.pad, buf_len,
				buf_state->page_info.buf_size);
	}
}

/* Chains multi skbs for single rx packet.
 * Returns 0 if buffer is appended, -1 otherwise.
 */
static int gve_rx_append_frags(struct napi_struct *napi,
			       struct gve_rx_buf_state_dqo *buf_state,
			       u16 buf_len, struct gve_rx_ring *rx,
			       struct gve_priv *priv)
{
	int num_frags = skb_shinfo(rx->ctx.skb_tail)->nr_frags;

	if (unlikely(num_frags == MAX_SKB_FRAGS)) {
		struct sk_buff *skb;

		skb = napi_alloc_skb(napi, 0);
		if (!skb)
			return -1;

		if (rx->dqo.page_pool)
			skb_mark_for_recycle(skb);

		if (rx->ctx.skb_tail == rx->ctx.skb_head)
			skb_shinfo(rx->ctx.skb_head)->frag_list = skb;
		else
			rx->ctx.skb_tail->next = skb;
		rx->ctx.skb_tail = skb;
		num_frags = 0;
	}
	if (rx->ctx.skb_tail != rx->ctx.skb_head) {
		rx->ctx.skb_head->len += buf_len;
		rx->ctx.skb_head->data_len += buf_len;
		rx->ctx.skb_head->truesize += buf_state->page_info.buf_size;
	}

	/* Trigger ondemand page allocation if we are running low on buffers */
	if (gve_rx_should_trigger_copy_ondemand(rx))
		return gve_rx_copy_ondemand(rx, buf_state, buf_len);

	gve_skb_add_rx_frag(rx, buf_state, num_frags, buf_len);
	gve_reuse_buffer(rx, buf_state);
	return 0;
}

static int gve_xdp_tx_dqo(struct gve_priv *priv, struct gve_rx_ring *rx,
			  struct xdp_buff *xdp)
{
	struct gve_tx_ring *tx;
	struct xdp_frame *xdpf;
	u32 tx_qid;
	int err;

	xdpf = xdp_convert_buff_to_frame(xdp);
	if (unlikely(!xdpf)) {
		if (rx->xsk_pool)
			xsk_buff_free(xdp);
		return -ENOSPC;
	}

	tx_qid = gve_xdp_tx_queue_id(priv, rx->q_num);
	tx = &priv->tx[tx_qid];
	spin_lock(&tx->dqo_tx.xdp_lock);
	err = gve_xdp_xmit_one_dqo(priv, tx, xdpf);
	spin_unlock(&tx->dqo_tx.xdp_lock);

	return err;
}

static void gve_xsk_done_dqo(struct gve_priv *priv, struct gve_rx_ring *rx,
			     struct xdp_buff *xdp, struct bpf_prog *xprog,
			     int xdp_act)
{
	switch (xdp_act) {
	case XDP_ABORTED:
	case XDP_DROP:
	default:
		xsk_buff_free(xdp);
		break;
	case XDP_TX:
		if (unlikely(gve_xdp_tx_dqo(priv, rx, xdp)))
			goto err;
		break;
	case XDP_REDIRECT:
		if (unlikely(xdp_do_redirect(priv->dev, xdp, xprog)))
			goto err;
		break;
	}

	u64_stats_update_begin(&rx->statss);
	if ((u32)xdp_act < GVE_XDP_ACTIONS)
		rx->xdp_actions[xdp_act]++;
	u64_stats_update_end(&rx->statss);
	return;

err:
	u64_stats_update_begin(&rx->statss);
	if (xdp_act == XDP_TX)
		rx->xdp_tx_errors++;
	if (xdp_act == XDP_REDIRECT)
		rx->xdp_redirect_errors++;
	u64_stats_update_end(&rx->statss);
}

static void gve_xdp_done_dqo(struct gve_priv *priv, struct gve_rx_ring *rx,
			     struct xdp_buff *xdp, struct bpf_prog *xprog,
			     int xdp_act,
			     struct gve_rx_buf_state_dqo *buf_state)
{
	int err;
	switch (xdp_act) {
	case XDP_ABORTED:
	case XDP_DROP:
	default:
		gve_free_buffer(rx, buf_state);
		break;
	case XDP_TX:
		err = gve_xdp_tx_dqo(priv, rx, xdp);
		if (unlikely(err))
			goto err;
		gve_reuse_buffer(rx, buf_state);
		break;
	case XDP_REDIRECT:
		err = xdp_do_redirect(priv->dev, xdp, xprog);
		if (unlikely(err))
			goto err;
		gve_reuse_buffer(rx, buf_state);
		break;
	}
	u64_stats_update_begin(&rx->statss);
	if ((u32)xdp_act < GVE_XDP_ACTIONS)
		rx->xdp_actions[xdp_act]++;
	u64_stats_update_end(&rx->statss);
	return;
err:
	u64_stats_update_begin(&rx->statss);
	if (xdp_act == XDP_TX)
		rx->xdp_tx_errors++;
	else if (xdp_act == XDP_REDIRECT)
		rx->xdp_redirect_errors++;
	u64_stats_update_end(&rx->statss);
	gve_free_buffer(rx, buf_state);
	return;
}

static int gve_rx_xsk_dqo(struct napi_struct *napi, struct gve_rx_ring *rx,
			  struct gve_rx_buf_state_dqo *buf_state, int buf_len,
			  struct bpf_prog *xprog)
{
	struct xdp_buff *xdp = buf_state->xsk_buff;
	struct gve_priv *priv = rx->gve;
	int xdp_act;

	xdp->data_end = xdp->data + buf_len;
	xsk_buff_dma_sync_for_cpu(xdp);

	if (xprog) {
		xdp_act = bpf_prog_run_xdp(xprog, xdp);
		buf_len = xdp->data_end - xdp->data;
		if (xdp_act != XDP_PASS) {
			gve_xsk_done_dqo(priv, rx, xdp, xprog, xdp_act);
			gve_free_buf_state(rx, buf_state);
			return 0;
		}
	}

	/* Copy the data to skb */
	rx->ctx.skb_head = gve_rx_copy_data(priv->dev, napi,
					    xdp->data, buf_len);
	if (unlikely(!rx->ctx.skb_head)) {
		xsk_buff_free(xdp);
		gve_free_buf_state(rx, buf_state);
		return -ENOMEM;
	}
	rx->ctx.skb_tail = rx->ctx.skb_head;

	/* Free XSK buffer and Buffer state */
	xsk_buff_free(xdp);
	gve_free_buf_state(rx, buf_state);

	/* Update Stats */
	u64_stats_update_begin(&rx->statss);
	rx->xdp_actions[XDP_PASS]++;
	u64_stats_update_end(&rx->statss);
	return 0;
}

static void gve_dma_sync(struct gve_priv *priv, struct gve_rx_ring *rx,
			 struct gve_rx_buf_state_dqo *buf_state, u16 buf_len)
{
	struct gve_rx_slot_page_info *page_info = &buf_state->page_info;

	if (rx->dqo.page_pool) {
		page_pool_dma_sync_netmem_for_cpu(rx->dqo.page_pool,
						  page_info->netmem,
						  page_info->page_offset,
						  buf_len);
	} else {
		dma_sync_single_range_for_cpu(&priv->pdev->dev, buf_state->addr,
					      page_info->page_offset +
					      page_info->pad,
					      buf_len, DMA_FROM_DEVICE);
	}
}

/* Returns 0 if descriptor is completed successfully.
 * Returns -EINVAL if descriptor is invalid.
 * Returns -ENOMEM if data cannot be copied to skb.
 */
static int gve_rx_dqo(struct napi_struct *napi, struct gve_rx_ring *rx,
		      const struct gve_rx_compl_desc_dqo *compl_desc,
		      u32 desc_idx, int queue_idx)
{
	const u16 buffer_id = le16_to_cpu(compl_desc->buf_id);
	const bool hbo = compl_desc->header_buffer_overflow;
	const bool eop = compl_desc->end_of_packet != 0;
	const bool hsplit = compl_desc->split_header;
	struct gve_rx_buf_state_dqo *buf_state;
	struct gve_priv *priv = rx->gve;
	struct bpf_prog *xprog;
	u16 buf_len;
	u16 hdr_len;

	if (unlikely(buffer_id >= rx->dqo.num_buf_states)) {
		net_err_ratelimited("%s: Invalid RX buffer_id=%u\n",
				    priv->dev->name, buffer_id);
		return -EINVAL;
	}
	buf_state = &rx->dqo.buf_states[buffer_id];
	if (unlikely(!gve_buf_state_is_allocated(rx, buf_state))) {
		net_err_ratelimited("%s: RX buffer_id is not allocated: %u\n",
				    priv->dev->name, buffer_id);
		return -EINVAL;
	}

	if (unlikely(compl_desc->rx_error)) {
		gve_free_buffer(rx, buf_state);
		return -EINVAL;
	}

	buf_len = compl_desc->packet_len;
	hdr_len = compl_desc->header_len;

	xprog = READ_ONCE(priv->xdp_prog);
	if (buf_state->xsk_buff)
		return gve_rx_xsk_dqo(napi, rx, buf_state, buf_len, xprog);

	/* Page might have not been used for awhile and was likely last written
	 * by a different thread.
	 */
	if (rx->dqo.page_pool) {
		if (!netmem_is_net_iov(buf_state->page_info.netmem))
			prefetch(netmem_to_page(buf_state->page_info.netmem));
	} else {
		prefetch(buf_state->page_info.page);
	}

	/* Copy the header into the skb in the case of header split */
	if (hsplit) {
		int unsplit = 0;

		if (hdr_len && !hbo) {
			rx->ctx.skb_head = gve_rx_copy_data(priv->dev, napi,
							    rx->dqo.hdr_bufs.data +
							    desc_idx * priv->header_buf_size,
							    hdr_len);
			if (unlikely(!rx->ctx.skb_head))
				goto error;
			rx->ctx.skb_tail = rx->ctx.skb_head;

			if (rx->dqo.page_pool)
				skb_mark_for_recycle(rx->ctx.skb_head);
		} else {
			unsplit = 1;
		}
		u64_stats_update_begin(&rx->statss);
		rx->rx_hsplit_pkt++;
		rx->rx_hsplit_unsplit_pkt += unsplit;
		rx->rx_hsplit_bytes += hdr_len;
		u64_stats_update_end(&rx->statss);
	} else if (!rx->ctx.skb_head && rx->dqo.page_pool &&
		   netmem_is_net_iov(buf_state->page_info.netmem)) {
		/* when header split is disabled, the header went to the packet
		 * buffer. If the packet buffer is a net_iov, those can't be
		 * easily mapped into the kernel space to access the header
		 * required to process the packet.
		 */
		goto error;
	}

	/* Sync the portion of dma buffer for CPU to read. */
	gve_dma_sync(priv, rx, buf_state, buf_len);

	/* Append to current skb if one exists. */
	if (rx->ctx.skb_head) {
		if (unlikely(gve_rx_append_frags(napi, buf_state, buf_len, rx,
						 priv)) != 0) {
			goto error;
		}
		return 0;
	}

	if (xprog) {
		struct xdp_buff xdp;
		void *old_data;
		int xdp_act;

		xdp_init_buff(&xdp, buf_state->page_info.buf_size,
			      &rx->xdp_rxq);
		xdp_prepare_buff(&xdp,
				 buf_state->page_info.page_address +
				 buf_state->page_info.page_offset,
				 buf_state->page_info.pad,
				 buf_len, false);
		old_data = xdp.data;
		xdp_act = bpf_prog_run_xdp(xprog, &xdp);
		buf_state->page_info.pad += xdp.data - old_data;
		buf_len = xdp.data_end - xdp.data;
		if (xdp_act != XDP_PASS) {
			gve_xdp_done_dqo(priv, rx, &xdp, xprog, xdp_act,
					 buf_state);
			return 0;
		}

		u64_stats_update_begin(&rx->statss);
		rx->xdp_actions[XDP_PASS]++;
		u64_stats_update_end(&rx->statss);
	}

	if (eop && buf_len <= priv->rx_copybreak &&
	    !(rx->dqo.page_pool &&
	      netmem_is_net_iov(buf_state->page_info.netmem))) {
		rx->ctx.skb_head = gve_rx_copy(priv->dev, napi,
					       &buf_state->page_info, buf_len);
		if (unlikely(!rx->ctx.skb_head))
			goto error;
		rx->ctx.skb_tail = rx->ctx.skb_head;

		u64_stats_update_begin(&rx->statss);
		rx->rx_copied_pkt++;
		rx->rx_copybreak_pkt++;
		u64_stats_update_end(&rx->statss);

		gve_free_buffer(rx, buf_state);
		return 0;
	}

	rx->ctx.skb_head = napi_get_frags(napi);
	if (unlikely(!rx->ctx.skb_head))
		goto error;
	rx->ctx.skb_tail = rx->ctx.skb_head;

	if (gve_rx_should_trigger_copy_ondemand(rx)) {
		if (gve_rx_copy_ondemand(rx, buf_state, buf_len) < 0)
			goto error;
		return 0;
	}

	if (rx->dqo.page_pool)
		skb_mark_for_recycle(rx->ctx.skb_head);

	gve_skb_add_rx_frag(rx, buf_state, 0, buf_len);
	gve_reuse_buffer(rx, buf_state);
	return 0;

error:
	gve_free_buffer(rx, buf_state);
	return -ENOMEM;
}

static int gve_rx_complete_rsc(struct sk_buff *skb,
			       const struct gve_rx_compl_desc_dqo *desc,
			       struct gve_ptype ptype)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);

	/* Only TCP is supported right now. */
	if (ptype.l4_type != GVE_L4_TYPE_TCP)
		return -EINVAL;

	switch (ptype.l3_type) {
	case GVE_L3_TYPE_IPV4:
		shinfo->gso_type = SKB_GSO_TCPV4;
		break;
	case GVE_L3_TYPE_IPV6:
		shinfo->gso_type = SKB_GSO_TCPV6;
		break;
	default:
		return -EINVAL;
	}

	shinfo->gso_size = le16_to_cpu(desc->rsc_seg_len);
	return 0;
}

/* Returns 0 if skb is completed successfully, -1 otherwise. */
static int gve_rx_complete_skb(struct gve_rx_ring *rx, struct napi_struct *napi,
			       const struct gve_rx_compl_desc_dqo *desc,
			       netdev_features_t feat)
{
	struct gve_ptype ptype =
		rx->gve->ptype_lut_dqo->ptypes[desc->packet_type];
	int err;

	skb_record_rx_queue(rx->ctx.skb_head, rx->q_num);

	if (feat & NETIF_F_RXHASH)
		gve_rx_skb_hash(rx->ctx.skb_head, desc, ptype);

	if (feat & NETIF_F_RXCSUM)
		gve_rx_skb_csum(rx->ctx.skb_head, desc, ptype);

	if (rx->gve->ts_config.rx_filter == HWTSTAMP_FILTER_ALL)
		gve_rx_skb_hwtstamp(rx, desc);

	/* RSC packets must set gso_size otherwise the TCP stack will complain
	 * that packets are larger than MTU.
	 */
	if (desc->rsc) {
		err = gve_rx_complete_rsc(rx->ctx.skb_head, desc, ptype);
		if (err < 0)
			return err;
	}

	if (skb_headlen(rx->ctx.skb_head) == 0)
		napi_gro_frags(napi);
	else
		napi_gro_receive(napi, rx->ctx.skb_head);

	return 0;
}

int gve_rx_poll_dqo(struct gve_notify_block *block, int budget)
{
	struct gve_rx_compl_queue_dqo *complq;
	struct napi_struct *napi;
	netdev_features_t feat;
	struct gve_rx_ring *rx;
	struct gve_priv *priv;
	u64 xdp_redirects;
	u32 work_done = 0;
	u64 bytes = 0;
	u64 xdp_txs;
	int err;

	napi = &block->napi;
	feat = napi->dev->features;

	rx = block->rx;
	priv = rx->gve;
	complq = &rx->dqo.complq;

	xdp_redirects = rx->xdp_actions[XDP_REDIRECT];
	xdp_txs = rx->xdp_actions[XDP_TX];

	while (work_done < budget) {
		struct gve_rx_compl_desc_dqo *compl_desc =
			&complq->desc_ring[complq->head];
		u32 pkt_bytes;

		/* No more new packets */
		if (compl_desc->generation == complq->cur_gen_bit)
			break;

		/* Prefetch the next two descriptors. */
		prefetch(&complq->desc_ring[(complq->head + 1) & complq->mask]);
		prefetch(&complq->desc_ring[(complq->head + 2) & complq->mask]);

		/* Do not read data until we own the descriptor */
		dma_rmb();

		err = gve_rx_dqo(napi, rx, compl_desc, complq->head, rx->q_num);
		if (err < 0) {
			gve_rx_free_skb(napi, rx);
			u64_stats_update_begin(&rx->statss);
			if (err == -ENOMEM)
				rx->rx_skb_alloc_fail++;
			else if (err == -EINVAL)
				rx->rx_desc_err_dropped_pkt++;
			u64_stats_update_end(&rx->statss);
		}

		complq->head = (complq->head + 1) & complq->mask;
		complq->num_free_slots++;

		/* When the ring wraps, the generation bit is flipped. */
		complq->cur_gen_bit ^= (complq->head == 0);

		/* Receiving a completion means we have space to post another
		 * buffer on the buffer queue.
		 */
		{
			struct gve_rx_buf_queue_dqo *bufq = &rx->dqo.bufq;

			bufq->head = (bufq->head + 1) & bufq->mask;
		}

		/* Free running counter of completed descriptors */
		rx->cnt++;

		if (!rx->ctx.skb_head)
			continue;

		if (!compl_desc->end_of_packet)
			continue;

		work_done++;
		pkt_bytes = rx->ctx.skb_head->len;
		/* The ethernet header (first ETH_HLEN bytes) is snipped off
		 * by eth_type_trans.
		 */
		if (skb_headlen(rx->ctx.skb_head))
			pkt_bytes += ETH_HLEN;

		/* gve_rx_complete_skb() will consume skb if successful */
		if (gve_rx_complete_skb(rx, napi, compl_desc, feat) != 0) {
			gve_rx_free_skb(napi, rx);
			u64_stats_update_begin(&rx->statss);
			rx->rx_desc_err_dropped_pkt++;
			u64_stats_update_end(&rx->statss);
			continue;
		}

		bytes += pkt_bytes;
		rx->ctx.skb_head = NULL;
		rx->ctx.skb_tail = NULL;
	}

	if (xdp_txs != rx->xdp_actions[XDP_TX])
		gve_xdp_tx_flush_dqo(priv, rx->q_num);

	if (xdp_redirects != rx->xdp_actions[XDP_REDIRECT])
		xdp_do_flush();

	gve_rx_post_buffers_dqo(rx);

	u64_stats_update_begin(&rx->statss);
	rx->rpackets += work_done;
	rx->rbytes += bytes;
	u64_stats_update_end(&rx->statss);

	return work_done;
}
