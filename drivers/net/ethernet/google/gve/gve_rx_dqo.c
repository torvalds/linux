// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

#include "gve.h"
#include "gve_dqo.h"
#include "gve_adminq.h"
#include "gve_utils.h"
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/ip6_checksum.h>
#include <net/ipv6.h>
#include <net/tcp.h>

static int gve_buf_ref_cnt(struct gve_rx_buf_state_dqo *bs)
{
	return page_count(bs->page_info.page) - bs->page_info.pagecnt_bias;
}

static void gve_free_page_dqo(struct gve_priv *priv,
			      struct gve_rx_buf_state_dqo *bs)
{
	page_ref_sub(bs->page_info.page, bs->page_info.pagecnt_bias - 1);
	gve_free_page(&priv->pdev->dev, bs->page_info.page, bs->addr,
		      DMA_FROM_DEVICE);
	bs->page_info.page = NULL;
}

static struct gve_rx_buf_state_dqo *gve_alloc_buf_state(struct gve_rx_ring *rx)
{
	struct gve_rx_buf_state_dqo *buf_state;
	s16 buffer_id;

	buffer_id = rx->dqo.free_buf_states;
	if (unlikely(buffer_id == -1))
		return NULL;

	buf_state = &rx->dqo.buf_states[buffer_id];

	/* Remove buf_state from free list */
	rx->dqo.free_buf_states = buf_state->next;

	/* Point buf_state to itself to mark it as allocated */
	buf_state->next = buffer_id;

	return buf_state;
}

static bool gve_buf_state_is_allocated(struct gve_rx_ring *rx,
				       struct gve_rx_buf_state_dqo *buf_state)
{
	s16 buffer_id = buf_state - rx->dqo.buf_states;

	return buf_state->next == buffer_id;
}

static void gve_free_buf_state(struct gve_rx_ring *rx,
			       struct gve_rx_buf_state_dqo *buf_state)
{
	s16 buffer_id = buf_state - rx->dqo.buf_states;

	buf_state->next = rx->dqo.free_buf_states;
	rx->dqo.free_buf_states = buffer_id;
}

static struct gve_rx_buf_state_dqo *
gve_dequeue_buf_state(struct gve_rx_ring *rx, struct gve_index_list *list)
{
	struct gve_rx_buf_state_dqo *buf_state;
	s16 buffer_id;

	buffer_id = list->head;
	if (unlikely(buffer_id == -1))
		return NULL;

	buf_state = &rx->dqo.buf_states[buffer_id];

	/* Remove buf_state from list */
	list->head = buf_state->next;
	if (buf_state->next == -1)
		list->tail = -1;

	/* Point buf_state to itself to mark it as allocated */
	buf_state->next = buffer_id;

	return buf_state;
}

static void gve_enqueue_buf_state(struct gve_rx_ring *rx,
				  struct gve_index_list *list,
				  struct gve_rx_buf_state_dqo *buf_state)
{
	s16 buffer_id = buf_state - rx->dqo.buf_states;

	buf_state->next = -1;

	if (list->head == -1) {
		list->head = buffer_id;
		list->tail = buffer_id;
	} else {
		int tail = list->tail;

		rx->dqo.buf_states[tail].next = buffer_id;
		list->tail = buffer_id;
	}
}

static struct gve_rx_buf_state_dqo *
gve_get_recycled_buf_state(struct gve_rx_ring *rx)
{
	struct gve_rx_buf_state_dqo *buf_state;
	int i;

	/* Recycled buf states are immediately usable. */
	buf_state = gve_dequeue_buf_state(rx, &rx->dqo.recycled_buf_states);
	if (likely(buf_state))
		return buf_state;

	if (unlikely(rx->dqo.used_buf_states.head == -1))
		return NULL;

	/* Used buf states are only usable when ref count reaches 0, which means
	 * no SKBs refer to them.
	 *
	 * Search a limited number before giving up.
	 */
	for (i = 0; i < 5; i++) {
		buf_state = gve_dequeue_buf_state(rx, &rx->dqo.used_buf_states);
		if (gve_buf_ref_cnt(buf_state) == 0)
			return buf_state;

		gve_enqueue_buf_state(rx, &rx->dqo.used_buf_states, buf_state);
	}

	/* If there are no free buf states discard an entry from
	 * `used_buf_states` so it can be used.
	 */
	if (unlikely(rx->dqo.free_buf_states == -1)) {
		buf_state = gve_dequeue_buf_state(rx, &rx->dqo.used_buf_states);
		if (gve_buf_ref_cnt(buf_state) == 0)
			return buf_state;

		gve_free_page_dqo(rx->gve, buf_state);
		gve_free_buf_state(rx, buf_state);
	}

	return NULL;
}

static int gve_alloc_page_dqo(struct gve_priv *priv,
			      struct gve_rx_buf_state_dqo *buf_state)
{
	int err;

	err = gve_alloc_page(priv, &priv->pdev->dev, &buf_state->page_info.page,
			     &buf_state->addr, DMA_FROM_DEVICE);
	if (err)
		return err;

	buf_state->page_info.page_offset = 0;
	buf_state->page_info.page_address =
		page_address(buf_state->page_info.page);
	buf_state->last_single_ref_offset = 0;

	/* The page already has 1 ref. */
	page_ref_add(buf_state->page_info.page, INT_MAX - 1);
	buf_state->page_info.pagecnt_bias = INT_MAX;

	return 0;
}

static void gve_rx_free_ring_dqo(struct gve_priv *priv, int idx)
{
	struct gve_rx_ring *rx = &priv->rx[idx];
	struct device *hdev = &priv->pdev->dev;
	size_t completion_queue_slots;
	size_t buffer_queue_slots;
	size_t size;
	int i;

	completion_queue_slots = rx->dqo.complq.mask + 1;
	buffer_queue_slots = rx->dqo.bufq.mask + 1;

	gve_rx_remove_from_block(priv, idx);

	if (rx->q_resources) {
		dma_free_coherent(hdev, sizeof(*rx->q_resources),
				  rx->q_resources, rx->q_resources_bus);
		rx->q_resources = NULL;
	}

	for (i = 0; i < rx->dqo.num_buf_states; i++) {
		struct gve_rx_buf_state_dqo *bs = &rx->dqo.buf_states[i];

		if (bs->page_info.page)
			gve_free_page_dqo(priv, bs);
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

	netif_dbg(priv, drv, priv->dev, "freed rx ring %d\n", idx);
}

static int gve_rx_alloc_ring_dqo(struct gve_priv *priv, int idx)
{
	struct gve_rx_ring *rx = &priv->rx[idx];
	struct device *hdev = &priv->pdev->dev;
	size_t size;
	int i;

	const u32 buffer_queue_slots =
		priv->options_dqo_rda.rx_buff_ring_entries;
	const u32 completion_queue_slots = priv->rx_desc_cnt;

	netif_dbg(priv, drv, priv->dev, "allocating rx ring DQO\n");

	memset(rx, 0, sizeof(*rx));
	rx->gve = priv;
	rx->q_num = idx;
	rx->dqo.bufq.mask = buffer_queue_slots - 1;
	rx->dqo.complq.num_free_slots = completion_queue_slots;
	rx->dqo.complq.mask = completion_queue_slots - 1;
	rx->skb_head = NULL;
	rx->skb_tail = NULL;

	rx->dqo.num_buf_states = min_t(s16, S16_MAX, buffer_queue_slots * 4);
	rx->dqo.buf_states = kvcalloc(rx->dqo.num_buf_states,
				      sizeof(rx->dqo.buf_states[0]),
				      GFP_KERNEL);
	if (!rx->dqo.buf_states)
		return -ENOMEM;

	/* Set up linked list of buffer IDs */
	for (i = 0; i < rx->dqo.num_buf_states - 1; i++)
		rx->dqo.buf_states[i].next = i + 1;

	rx->dqo.buf_states[rx->dqo.num_buf_states - 1].next = -1;
	rx->dqo.recycled_buf_states.head = -1;
	rx->dqo.recycled_buf_states.tail = -1;
	rx->dqo.used_buf_states.head = -1;
	rx->dqo.used_buf_states.tail = -1;

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

	rx->q_resources = dma_alloc_coherent(hdev, sizeof(*rx->q_resources),
					     &rx->q_resources_bus, GFP_KERNEL);
	if (!rx->q_resources)
		goto err;

	gve_rx_add_to_block(priv, idx);

	return 0;

err:
	gve_rx_free_ring_dqo(priv, idx);
	return -ENOMEM;
}

void gve_rx_write_doorbell_dqo(const struct gve_priv *priv, int queue_idx)
{
	const struct gve_rx_ring *rx = &priv->rx[queue_idx];
	u64 index = be32_to_cpu(rx->q_resources->db_index);

	iowrite32(rx->dqo.bufq.tail, &priv->db_bar2[index]);
}

int gve_rx_alloc_rings_dqo(struct gve_priv *priv)
{
	int err = 0;
	int i;

	for (i = 0; i < priv->rx_cfg.num_queues; i++) {
		err = gve_rx_alloc_ring_dqo(priv, i);
		if (err) {
			netif_err(priv, drv, priv->dev,
				  "Failed to alloc rx ring=%d: err=%d\n",
				  i, err);
			goto err;
		}
	}

	return 0;

err:
	for (i--; i >= 0; i--)
		gve_rx_free_ring_dqo(priv, i);

	return err;
}

void gve_rx_free_rings_dqo(struct gve_priv *priv)
{
	int i;

	for (i = 0; i < priv->rx_cfg.num_queues; i++)
		gve_rx_free_ring_dqo(priv, i);
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
		struct gve_rx_buf_state_dqo *buf_state;

		buf_state = gve_get_recycled_buf_state(rx);
		if (unlikely(!buf_state)) {
			buf_state = gve_alloc_buf_state(rx);
			if (unlikely(!buf_state))
				break;

			if (unlikely(gve_alloc_page_dqo(priv, buf_state))) {
				u64_stats_update_begin(&rx->statss);
				rx->rx_buf_alloc_fail++;
				u64_stats_update_end(&rx->statss);
				gve_free_buf_state(rx, buf_state);
				break;
			}
		}

		desc->buf_id = cpu_to_le16(buf_state - rx->dqo.buf_states);
		desc->buf_addr = cpu_to_le64(buf_state->addr +
					     buf_state->page_info.page_offset);

		bufq->tail = (bufq->tail + 1) & bufq->mask;
		complq->num_free_slots--;
		num_posted++;

		if ((bufq->tail & (GVE_RX_BUF_THRESH_DQO - 1)) == 0)
			gve_rx_write_doorbell_dqo(priv, rx->q_num);
	}

	rx->fill_cnt += num_posted;
}

static void gve_try_recycle_buf(struct gve_priv *priv, struct gve_rx_ring *rx,
				struct gve_rx_buf_state_dqo *buf_state)
{
	const int data_buffer_size = priv->data_buffer_size_dqo;
	int pagecount;

	/* Can't reuse if we only fit one buffer per page */
	if (data_buffer_size * 2 > PAGE_SIZE)
		goto mark_used;

	pagecount = gve_buf_ref_cnt(buf_state);

	/* Record the offset when we have a single remaining reference.
	 *
	 * When this happens, we know all of the other offsets of the page are
	 * usable.
	 */
	if (pagecount == 1) {
		buf_state->last_single_ref_offset =
			buf_state->page_info.page_offset;
	}

	/* Use the next buffer sized chunk in the page. */
	buf_state->page_info.page_offset += data_buffer_size;
	buf_state->page_info.page_offset &= (PAGE_SIZE - 1);

	/* If we wrap around to the same offset without ever dropping to 1
	 * reference, then we don't know if this offset was ever freed.
	 */
	if (buf_state->page_info.page_offset ==
	    buf_state->last_single_ref_offset) {
		goto mark_used;
	}

	gve_enqueue_buf_state(rx, &rx->dqo.recycled_buf_states, buf_state);
	return;

mark_used:
	gve_enqueue_buf_state(rx, &rx->dqo.used_buf_states, buf_state);
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

static void gve_rx_free_skb(struct gve_rx_ring *rx)
{
	if (!rx->skb_head)
		return;

	dev_kfree_skb_any(rx->skb_head);
	rx->skb_head = NULL;
	rx->skb_tail = NULL;
}

/* Chains multi skbs for single rx packet.
 * Returns 0 if buffer is appended, -1 otherwise.
 */
static int gve_rx_append_frags(struct napi_struct *napi,
			       struct gve_rx_buf_state_dqo *buf_state,
			       u16 buf_len, struct gve_rx_ring *rx,
			       struct gve_priv *priv)
{
	int num_frags = skb_shinfo(rx->skb_tail)->nr_frags;

	if (unlikely(num_frags == MAX_SKB_FRAGS)) {
		struct sk_buff *skb;

		skb = napi_alloc_skb(napi, 0);
		if (!skb)
			return -1;

		skb_shinfo(rx->skb_tail)->frag_list = skb;
		rx->skb_tail = skb;
		num_frags = 0;
	}
	if (rx->skb_tail != rx->skb_head) {
		rx->skb_head->len += buf_len;
		rx->skb_head->data_len += buf_len;
		rx->skb_head->truesize += priv->data_buffer_size_dqo;
	}

	skb_add_rx_frag(rx->skb_tail, num_frags,
			buf_state->page_info.page,
			buf_state->page_info.page_offset,
			buf_len, priv->data_buffer_size_dqo);
	gve_dec_pagecnt_bias(&buf_state->page_info);

	return 0;
}

/* Returns 0 if descriptor is completed successfully.
 * Returns -EINVAL if descriptor is invalid.
 * Returns -ENOMEM if data cannot be copied to skb.
 */
static int gve_rx_dqo(struct napi_struct *napi, struct gve_rx_ring *rx,
		      const struct gve_rx_compl_desc_dqo *compl_desc,
		      int queue_idx)
{
	const u16 buffer_id = le16_to_cpu(compl_desc->buf_id);
	const bool eop = compl_desc->end_of_packet != 0;
	struct gve_rx_buf_state_dqo *buf_state;
	struct gve_priv *priv = rx->gve;
	u16 buf_len;

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
		gve_enqueue_buf_state(rx, &rx->dqo.recycled_buf_states,
				      buf_state);
		return -EINVAL;
	}

	buf_len = compl_desc->packet_len;

	/* Page might have not been used for awhile and was likely last written
	 * by a different thread.
	 */
	prefetch(buf_state->page_info.page);

	/* Sync the portion of dma buffer for CPU to read. */
	dma_sync_single_range_for_cpu(&priv->pdev->dev, buf_state->addr,
				      buf_state->page_info.page_offset,
				      buf_len, DMA_FROM_DEVICE);

	/* Append to current skb if one exists. */
	if (rx->skb_head) {
		if (unlikely(gve_rx_append_frags(napi, buf_state, buf_len, rx,
						 priv)) != 0) {
			goto error;
		}

		gve_try_recycle_buf(priv, rx, buf_state);
		return 0;
	}

	/* Prefetch the payload header. */
	prefetch((char *)buf_state->addr + buf_state->page_info.page_offset);
#if L1_CACHE_BYTES < 128
	prefetch((char *)buf_state->addr + buf_state->page_info.page_offset +
		 L1_CACHE_BYTES);
#endif

	if (eop && buf_len <= priv->rx_copybreak) {
		rx->skb_head = gve_rx_copy(priv->dev, napi,
					   &buf_state->page_info, buf_len, 0);
		if (unlikely(!rx->skb_head))
			goto error;
		rx->skb_tail = rx->skb_head;

		u64_stats_update_begin(&rx->statss);
		rx->rx_copied_pkt++;
		rx->rx_copybreak_pkt++;
		u64_stats_update_end(&rx->statss);

		gve_enqueue_buf_state(rx, &rx->dqo.recycled_buf_states,
				      buf_state);
		return 0;
	}

	rx->skb_head = napi_get_frags(napi);
	if (unlikely(!rx->skb_head))
		goto error;
	rx->skb_tail = rx->skb_head;

	skb_add_rx_frag(rx->skb_head, 0, buf_state->page_info.page,
			buf_state->page_info.page_offset, buf_len,
			priv->data_buffer_size_dqo);
	gve_dec_pagecnt_bias(&buf_state->page_info);

	gve_try_recycle_buf(priv, rx, buf_state);
	return 0;

error:
	gve_enqueue_buf_state(rx, &rx->dqo.recycled_buf_states, buf_state);
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

	skb_record_rx_queue(rx->skb_head, rx->q_num);

	if (feat & NETIF_F_RXHASH)
		gve_rx_skb_hash(rx->skb_head, desc, ptype);

	if (feat & NETIF_F_RXCSUM)
		gve_rx_skb_csum(rx->skb_head, desc, ptype);

	/* RSC packets must set gso_size otherwise the TCP stack will complain
	 * that packets are larger than MTU.
	 */
	if (desc->rsc) {
		err = gve_rx_complete_rsc(rx->skb_head, desc, ptype);
		if (err < 0)
			return err;
	}

	if (skb_headlen(rx->skb_head) == 0)
		napi_gro_frags(napi);
	else
		napi_gro_receive(napi, rx->skb_head);

	return 0;
}

int gve_rx_poll_dqo(struct gve_notify_block *block, int budget)
{
	struct napi_struct *napi = &block->napi;
	netdev_features_t feat = napi->dev->features;

	struct gve_rx_ring *rx = block->rx;
	struct gve_rx_compl_queue_dqo *complq = &rx->dqo.complq;

	u32 work_done = 0;
	u64 bytes = 0;
	int err;

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

		err = gve_rx_dqo(napi, rx, compl_desc, rx->q_num);
		if (err < 0) {
			gve_rx_free_skb(rx);
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

		if (!rx->skb_head)
			continue;

		if (!compl_desc->end_of_packet)
			continue;

		work_done++;
		pkt_bytes = rx->skb_head->len;
		/* The ethernet header (first ETH_HLEN bytes) is snipped off
		 * by eth_type_trans.
		 */
		if (skb_headlen(rx->skb_head))
			pkt_bytes += ETH_HLEN;

		/* gve_rx_complete_skb() will consume skb if successful */
		if (gve_rx_complete_skb(rx, napi, compl_desc, feat) != 0) {
			gve_rx_free_skb(rx);
			u64_stats_update_begin(&rx->statss);
			rx->rx_desc_err_dropped_pkt++;
			u64_stats_update_end(&rx->statss);
			continue;
		}

		bytes += pkt_bytes;
		rx->skb_head = NULL;
		rx->skb_tail = NULL;
	}

	gve_rx_post_buffers_dqo(rx);

	u64_stats_update_begin(&rx->statss);
	rx->rpackets += work_done;
	rx->rbytes += bytes;
	u64_stats_update_end(&rx->statss);

	return work_done;
}
