// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#include "dp_rx.h"
#include "debug.h"
#include "hif.h"

const struct ce_attr ath11k_host_ce_config_ipq8074[] = {
	/* CE0: host->target HTC control and raw streams */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 16,
		.src_sz_max = 2048,
		.dest_nentries = 0,
		.send_cb = ath11k_htc_tx_completion_handler,
	},

	/* CE1: target->host HTT + HTC control */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = ath11k_htc_rx_completion_handler,
	},

	/* CE2: target->host WMI */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = ath11k_htc_rx_completion_handler,
	},

	/* CE3: host->target WMI (mac0) */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 32,
		.src_sz_max = 2048,
		.dest_nentries = 0,
		.send_cb = ath11k_htc_tx_completion_handler,
	},

	/* CE4: host->target HTT */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 2048,
		.src_sz_max = 256,
		.dest_nentries = 0,
	},

	/* CE5: target->host pktlog */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = ath11k_dp_htt_htc_t2h_msg_handler,
	},

	/* CE6: target autonomous hif_memcpy */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},

	/* CE7: host->target WMI (mac1) */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 32,
		.src_sz_max = 2048,
		.dest_nentries = 0,
		.send_cb = ath11k_htc_tx_completion_handler,
	},

	/* CE8: target autonomous hif_memcpy */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},

	/* CE9: host->target WMI (mac2) */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 32,
		.src_sz_max = 2048,
		.dest_nentries = 0,
		.send_cb = ath11k_htc_tx_completion_handler,
	},

	/* CE10: target->host HTT */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = ath11k_htc_rx_completion_handler,
	},

	/* CE11: Not used */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},
};

const struct ce_attr ath11k_host_ce_config_qca6390[] = {
	/* CE0: host->target HTC control and raw streams */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 16,
		.src_sz_max = 2048,
		.dest_nentries = 0,
	},

	/* CE1: target->host HTT + HTC control */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = ath11k_htc_rx_completion_handler,
	},

	/* CE2: target->host WMI */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = ath11k_htc_rx_completion_handler,
	},

	/* CE3: host->target WMI (mac0) */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 32,
		.src_sz_max = 2048,
		.dest_nentries = 0,
		.send_cb = ath11k_htc_tx_completion_handler,
	},

	/* CE4: host->target HTT */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 2048,
		.src_sz_max = 256,
		.dest_nentries = 0,
	},

	/* CE5: target->host pktlog */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = ath11k_dp_htt_htc_t2h_msg_handler,
	},

	/* CE6: target autonomous hif_memcpy */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},

	/* CE7: host->target WMI (mac1) */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 32,
		.src_sz_max = 2048,
		.dest_nentries = 0,
		.send_cb = ath11k_htc_tx_completion_handler,
	},

	/* CE8: target autonomous hif_memcpy */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},

};

const struct ce_attr ath11k_host_ce_config_qcn9074[] = {
	/* CE0: host->target HTC control and raw streams */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 16,
		.src_sz_max = 2048,
		.dest_nentries = 0,
	},

	/* CE1: target->host HTT + HTC control */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = ath11k_htc_rx_completion_handler,
	},

	/* CE2: target->host WMI */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 32,
		.recv_cb = ath11k_htc_rx_completion_handler,
	},

	/* CE3: host->target WMI (mac0) */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 32,
		.src_sz_max = 2048,
		.dest_nentries = 0,
		.send_cb = ath11k_htc_tx_completion_handler,
	},

	/* CE4: host->target HTT */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 2048,
		.src_sz_max = 256,
		.dest_nentries = 0,
	},

	/* CE5: target->host pktlog */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = ath11k_dp_htt_htc_t2h_msg_handler,
	},
};

static bool ath11k_ce_need_shadow_fix(int ce_id)
{
	/* only ce4 needs shadow workaroud*/
	if (ce_id == 4)
		return true;
	return false;
}

void ath11k_ce_stop_shadow_timers(struct ath11k_base *ab)
{
	int i;

	if (!ab->hw_params.supports_shadow_regs)
		return;

	for (i = 0; i < ab->hw_params.ce_count; i++)
		if (ath11k_ce_need_shadow_fix(i))
			ath11k_dp_shadow_stop_timer(ab, &ab->ce.hp_timer[i]);
}

static int ath11k_ce_rx_buf_enqueue_pipe(struct ath11k_ce_pipe *pipe,
					 struct sk_buff *skb, dma_addr_t paddr)
{
	struct ath11k_base *ab = pipe->ab;
	struct ath11k_ce_ring *ring = pipe->dest_ring;
	struct hal_srng *srng;
	unsigned int write_index;
	unsigned int nentries_mask = ring->nentries_mask;
	u32 *desc;
	int ret;

	lockdep_assert_held(&ab->ce.ce_lock);

	write_index = ring->write_index;

	srng = &ab->hal.srng_list[ring->hal_ring_id];

	spin_lock_bh(&srng->lock);

	ath11k_hal_srng_access_begin(ab, srng);

	if (unlikely(ath11k_hal_srng_src_num_free(ab, srng, false) < 1)) {
		ret = -ENOSPC;
		goto exit;
	}

	desc = ath11k_hal_srng_src_get_next_entry(ab, srng);
	if (!desc) {
		ret = -ENOSPC;
		goto exit;
	}

	ath11k_hal_ce_dst_set_desc(desc, paddr);

	ring->skb[write_index] = skb;
	write_index = CE_RING_IDX_INCR(nentries_mask, write_index);
	ring->write_index = write_index;

	pipe->rx_buf_needed--;

	ret = 0;
exit:
	ath11k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	return ret;
}

static int ath11k_ce_rx_post_pipe(struct ath11k_ce_pipe *pipe)
{
	struct ath11k_base *ab = pipe->ab;
	struct sk_buff *skb;
	dma_addr_t paddr;
	int ret = 0;

	if (!(pipe->dest_ring || pipe->status_ring))
		return 0;

	spin_lock_bh(&ab->ce.ce_lock);
	while (pipe->rx_buf_needed) {
		skb = dev_alloc_skb(pipe->buf_sz);
		if (!skb) {
			ret = -ENOMEM;
			goto exit;
		}

		WARN_ON_ONCE(!IS_ALIGNED((unsigned long)skb->data, 4));

		paddr = dma_map_single(ab->dev, skb->data,
				       skb->len + skb_tailroom(skb),
				       DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(ab->dev, paddr))) {
			ath11k_warn(ab, "failed to dma map ce rx buf\n");
			dev_kfree_skb_any(skb);
			ret = -EIO;
			goto exit;
		}

		ATH11K_SKB_RXCB(skb)->paddr = paddr;

		ret = ath11k_ce_rx_buf_enqueue_pipe(pipe, skb, paddr);

		if (ret) {
			ath11k_warn(ab, "failed to enqueue rx buf: %d\n", ret);
			dma_unmap_single(ab->dev, paddr,
					 skb->len + skb_tailroom(skb),
					 DMA_FROM_DEVICE);
			dev_kfree_skb_any(skb);
			goto exit;
		}
	}

exit:
	spin_unlock_bh(&ab->ce.ce_lock);
	return ret;
}

static int ath11k_ce_completed_recv_next(struct ath11k_ce_pipe *pipe,
					 struct sk_buff **skb, int *nbytes)
{
	struct ath11k_base *ab = pipe->ab;
	struct hal_srng *srng;
	unsigned int sw_index;
	unsigned int nentries_mask;
	u32 *desc;
	int ret = 0;

	spin_lock_bh(&ab->ce.ce_lock);

	sw_index = pipe->dest_ring->sw_index;
	nentries_mask = pipe->dest_ring->nentries_mask;

	srng = &ab->hal.srng_list[pipe->status_ring->hal_ring_id];

	spin_lock_bh(&srng->lock);

	ath11k_hal_srng_access_begin(ab, srng);

	desc = ath11k_hal_srng_dst_get_next_entry(ab, srng);
	if (!desc) {
		ret = -EIO;
		goto err;
	}

	*nbytes = ath11k_hal_ce_dst_status_get_length(desc);
	if (*nbytes == 0) {
		ret = -EIO;
		goto err;
	}

	*skb = pipe->dest_ring->skb[sw_index];
	pipe->dest_ring->skb[sw_index] = NULL;

	sw_index = CE_RING_IDX_INCR(nentries_mask, sw_index);
	pipe->dest_ring->sw_index = sw_index;

	pipe->rx_buf_needed++;
err:
	ath11k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	spin_unlock_bh(&ab->ce.ce_lock);

	return ret;
}

static void ath11k_ce_recv_process_cb(struct ath11k_ce_pipe *pipe)
{
	struct ath11k_base *ab = pipe->ab;
	struct sk_buff *skb;
	struct sk_buff_head list;
	unsigned int nbytes, max_nbytes;
	int ret;

	__skb_queue_head_init(&list);
	while (ath11k_ce_completed_recv_next(pipe, &skb, &nbytes) == 0) {
		max_nbytes = skb->len + skb_tailroom(skb);
		dma_unmap_single(ab->dev, ATH11K_SKB_RXCB(skb)->paddr,
				 max_nbytes, DMA_FROM_DEVICE);

		if (unlikely(max_nbytes < nbytes)) {
			ath11k_warn(ab, "rxed more than expected (nbytes %d, max %d)",
				    nbytes, max_nbytes);
			dev_kfree_skb_any(skb);
			continue;
		}

		skb_put(skb, nbytes);
		__skb_queue_tail(&list, skb);
	}

	while ((skb = __skb_dequeue(&list))) {
		ath11k_dbg(ab, ATH11K_DBG_AHB, "rx ce pipe %d len %d\n",
			   pipe->pipe_num, skb->len);
		pipe->recv_cb(ab, skb);
	}

	ret = ath11k_ce_rx_post_pipe(pipe);
	if (ret && ret != -ENOSPC) {
		ath11k_warn(ab, "failed to post rx buf to pipe: %d err: %d\n",
			    pipe->pipe_num, ret);
		mod_timer(&ab->rx_replenish_retry,
			  jiffies + ATH11K_CE_RX_POST_RETRY_JIFFIES);
	}
}

static struct sk_buff *ath11k_ce_completed_send_next(struct ath11k_ce_pipe *pipe)
{
	struct ath11k_base *ab = pipe->ab;
	struct hal_srng *srng;
	unsigned int sw_index;
	unsigned int nentries_mask;
	struct sk_buff *skb;
	u32 *desc;

	spin_lock_bh(&ab->ce.ce_lock);

	sw_index = pipe->src_ring->sw_index;
	nentries_mask = pipe->src_ring->nentries_mask;

	srng = &ab->hal.srng_list[pipe->src_ring->hal_ring_id];

	spin_lock_bh(&srng->lock);

	ath11k_hal_srng_access_begin(ab, srng);

	desc = ath11k_hal_srng_src_reap_next(ab, srng);
	if (!desc) {
		skb = ERR_PTR(-EIO);
		goto err_unlock;
	}

	skb = pipe->src_ring->skb[sw_index];

	pipe->src_ring->skb[sw_index] = NULL;

	sw_index = CE_RING_IDX_INCR(nentries_mask, sw_index);
	pipe->src_ring->sw_index = sw_index;

err_unlock:
	spin_unlock_bh(&srng->lock);

	spin_unlock_bh(&ab->ce.ce_lock);

	return skb;
}

static void ath11k_ce_tx_process_cb(struct ath11k_ce_pipe *pipe)
{
	struct ath11k_base *ab = pipe->ab;
	struct sk_buff *skb;
	struct sk_buff_head list;

	__skb_queue_head_init(&list);
	while (!IS_ERR(skb = ath11k_ce_completed_send_next(pipe))) {
		if (!skb)
			continue;

		dma_unmap_single(ab->dev, ATH11K_SKB_CB(skb)->paddr, skb->len,
				 DMA_TO_DEVICE);

		if ((!pipe->send_cb) || ab->hw_params.credit_flow) {
			dev_kfree_skb_any(skb);
			continue;
		}

		__skb_queue_tail(&list, skb);
	}

	while ((skb = __skb_dequeue(&list))) {
		ath11k_dbg(ab, ATH11K_DBG_AHB, "tx ce pipe %d len %d\n",
			   pipe->pipe_num, skb->len);
		pipe->send_cb(ab, skb);
	}
}

static void ath11k_ce_srng_msi_ring_params_setup(struct ath11k_base *ab, u32 ce_id,
						 struct hal_srng_params *ring_params)
{
	u32 msi_data_start;
	u32 msi_data_count, msi_data_idx;
	u32 msi_irq_start;
	u32 addr_lo;
	u32 addr_hi;
	int ret;

	ret = ath11k_get_user_msi_vector(ab, "CE",
					 &msi_data_count, &msi_data_start,
					 &msi_irq_start);

	if (ret)
		return;

	ath11k_get_msi_address(ab, &addr_lo, &addr_hi);
	ath11k_get_ce_msi_idx(ab, ce_id, &msi_data_idx);

	ring_params->msi_addr = addr_lo;
	ring_params->msi_addr |= (dma_addr_t)(((uint64_t)addr_hi) << 32);
	ring_params->msi_data = (msi_data_idx % msi_data_count) + msi_data_start;
	ring_params->flags |= HAL_SRNG_FLAGS_MSI_INTR;
}

static int ath11k_ce_init_ring(struct ath11k_base *ab,
			       struct ath11k_ce_ring *ce_ring,
			       int ce_id, enum hal_ring_type type)
{
	struct hal_srng_params params = { 0 };
	int ret;

	params.ring_base_paddr = ce_ring->base_addr_ce_space;
	params.ring_base_vaddr = ce_ring->base_addr_owner_space;
	params.num_entries = ce_ring->nentries;

	if (!(CE_ATTR_DIS_INTR & ab->hw_params.host_ce_config[ce_id].flags))
		ath11k_ce_srng_msi_ring_params_setup(ab, ce_id, &params);

	switch (type) {
	case HAL_CE_SRC:
		if (!(CE_ATTR_DIS_INTR & ab->hw_params.host_ce_config[ce_id].flags))
			params.intr_batch_cntr_thres_entries = 1;
		break;
	case HAL_CE_DST:
		params.max_buffer_len = ab->hw_params.host_ce_config[ce_id].src_sz_max;
		if (!(ab->hw_params.host_ce_config[ce_id].flags & CE_ATTR_DIS_INTR)) {
			params.intr_timer_thres_us = 1024;
			params.flags |= HAL_SRNG_FLAGS_LOW_THRESH_INTR_EN;
			params.low_threshold = ce_ring->nentries - 3;
		}
		break;
	case HAL_CE_DST_STATUS:
		if (!(ab->hw_params.host_ce_config[ce_id].flags & CE_ATTR_DIS_INTR)) {
			params.intr_batch_cntr_thres_entries = 1;
			params.intr_timer_thres_us = 0x1000;
		}
		break;
	default:
		ath11k_warn(ab, "Invalid CE ring type %d\n", type);
		return -EINVAL;
	}

	/* TODO: Init other params needed by HAL to init the ring */

	ret = ath11k_hal_srng_setup(ab, type, ce_id, 0, &params);
	if (ret < 0) {
		ath11k_warn(ab, "failed to setup srng: %d ring_id %d\n",
			    ret, ce_id);
		return ret;
	}

	ce_ring->hal_ring_id = ret;

	if (ab->hw_params.supports_shadow_regs &&
	    ath11k_ce_need_shadow_fix(ce_id))
		ath11k_dp_shadow_init_timer(ab, &ab->ce.hp_timer[ce_id],
					    ATH11K_SHADOW_CTRL_TIMER_INTERVAL,
					    ce_ring->hal_ring_id);

	return 0;
}

static struct ath11k_ce_ring *
ath11k_ce_alloc_ring(struct ath11k_base *ab, int nentries, int desc_sz)
{
	struct ath11k_ce_ring *ce_ring;
	dma_addr_t base_addr;

	ce_ring = kzalloc(struct_size(ce_ring, skb, nentries), GFP_KERNEL);
	if (ce_ring == NULL)
		return ERR_PTR(-ENOMEM);

	ce_ring->nentries = nentries;
	ce_ring->nentries_mask = nentries - 1;

	/* Legacy platforms that do not support cache
	 * coherent DMA are unsupported
	 */
	ce_ring->base_addr_owner_space_unaligned =
		dma_alloc_coherent(ab->dev,
				   nentries * desc_sz + CE_DESC_RING_ALIGN,
				   &base_addr, GFP_KERNEL);
	if (!ce_ring->base_addr_owner_space_unaligned) {
		kfree(ce_ring);
		return ERR_PTR(-ENOMEM);
	}

	ce_ring->base_addr_ce_space_unaligned = base_addr;

	ce_ring->base_addr_owner_space = PTR_ALIGN(
			ce_ring->base_addr_owner_space_unaligned,
			CE_DESC_RING_ALIGN);
	ce_ring->base_addr_ce_space = ALIGN(
			ce_ring->base_addr_ce_space_unaligned,
			CE_DESC_RING_ALIGN);

	return ce_ring;
}

static int ath11k_ce_alloc_pipe(struct ath11k_base *ab, int ce_id)
{
	struct ath11k_ce_pipe *pipe = &ab->ce.ce_pipe[ce_id];
	const struct ce_attr *attr = &ab->hw_params.host_ce_config[ce_id];
	struct ath11k_ce_ring *ring;
	int nentries;
	int desc_sz;

	pipe->attr_flags = attr->flags;

	if (attr->src_nentries) {
		pipe->send_cb = attr->send_cb;
		nentries = roundup_pow_of_two(attr->src_nentries);
		desc_sz = ath11k_hal_ce_get_desc_size(HAL_CE_DESC_SRC);
		ring = ath11k_ce_alloc_ring(ab, nentries, desc_sz);
		if (IS_ERR(ring))
			return PTR_ERR(ring);
		pipe->src_ring = ring;
	}

	if (attr->dest_nentries) {
		pipe->recv_cb = attr->recv_cb;
		nentries = roundup_pow_of_two(attr->dest_nentries);
		desc_sz = ath11k_hal_ce_get_desc_size(HAL_CE_DESC_DST);
		ring = ath11k_ce_alloc_ring(ab, nentries, desc_sz);
		if (IS_ERR(ring))
			return PTR_ERR(ring);
		pipe->dest_ring = ring;

		desc_sz = ath11k_hal_ce_get_desc_size(HAL_CE_DESC_DST_STATUS);
		ring = ath11k_ce_alloc_ring(ab, nentries, desc_sz);
		if (IS_ERR(ring))
			return PTR_ERR(ring);
		pipe->status_ring = ring;
	}

	return 0;
}

void ath11k_ce_per_engine_service(struct ath11k_base *ab, u16 ce_id)
{
	struct ath11k_ce_pipe *pipe = &ab->ce.ce_pipe[ce_id];
	const struct ce_attr *attr = &ab->hw_params.host_ce_config[ce_id];

	if (attr->src_nentries)
		ath11k_ce_tx_process_cb(pipe);

	if (pipe->recv_cb)
		ath11k_ce_recv_process_cb(pipe);
}

void ath11k_ce_poll_send_completed(struct ath11k_base *ab, u8 pipe_id)
{
	struct ath11k_ce_pipe *pipe = &ab->ce.ce_pipe[pipe_id];
	const struct ce_attr *attr =  &ab->hw_params.host_ce_config[pipe_id];

	if ((pipe->attr_flags & CE_ATTR_DIS_INTR) && attr->src_nentries)
		ath11k_ce_tx_process_cb(pipe);
}
EXPORT_SYMBOL(ath11k_ce_per_engine_service);

int ath11k_ce_send(struct ath11k_base *ab, struct sk_buff *skb, u8 pipe_id,
		   u16 transfer_id)
{
	struct ath11k_ce_pipe *pipe = &ab->ce.ce_pipe[pipe_id];
	struct hal_srng *srng;
	u32 *desc;
	unsigned int write_index, sw_index;
	unsigned int nentries_mask;
	int ret = 0;
	u8 byte_swap_data = 0;
	int num_used;

	/* Check if some entries could be regained by handling tx completion if
	 * the CE has interrupts disabled and the used entries is more than the
	 * defined usage threshold.
	 */
	if (pipe->attr_flags & CE_ATTR_DIS_INTR) {
		spin_lock_bh(&ab->ce.ce_lock);
		write_index = pipe->src_ring->write_index;

		sw_index = pipe->src_ring->sw_index;

		if (write_index >= sw_index)
			num_used = write_index - sw_index;
		else
			num_used = pipe->src_ring->nentries - sw_index +
				   write_index;

		spin_unlock_bh(&ab->ce.ce_lock);

		if (num_used > ATH11K_CE_USAGE_THRESHOLD)
			ath11k_ce_poll_send_completed(ab, pipe->pipe_num);
	}

	if (test_bit(ATH11K_FLAG_CRASH_FLUSH, &ab->dev_flags))
		return -ESHUTDOWN;

	spin_lock_bh(&ab->ce.ce_lock);

	write_index = pipe->src_ring->write_index;
	nentries_mask = pipe->src_ring->nentries_mask;

	srng = &ab->hal.srng_list[pipe->src_ring->hal_ring_id];

	spin_lock_bh(&srng->lock);

	ath11k_hal_srng_access_begin(ab, srng);

	if (unlikely(ath11k_hal_srng_src_num_free(ab, srng, false) < 1)) {
		ath11k_hal_srng_access_end(ab, srng);
		ret = -ENOBUFS;
		goto err_unlock;
	}

	desc = ath11k_hal_srng_src_get_next_reaped(ab, srng);
	if (!desc) {
		ath11k_hal_srng_access_end(ab, srng);
		ret = -ENOBUFS;
		goto err_unlock;
	}

	if (pipe->attr_flags & CE_ATTR_BYTE_SWAP_DATA)
		byte_swap_data = 1;

	ath11k_hal_ce_src_set_desc(desc, ATH11K_SKB_CB(skb)->paddr,
				   skb->len, transfer_id, byte_swap_data);

	pipe->src_ring->skb[write_index] = skb;
	pipe->src_ring->write_index = CE_RING_IDX_INCR(nentries_mask,
						       write_index);

	ath11k_hal_srng_access_end(ab, srng);

	if (ath11k_ce_need_shadow_fix(pipe_id))
		ath11k_dp_shadow_start_timer(ab, srng, &ab->ce.hp_timer[pipe_id]);

	spin_unlock_bh(&srng->lock);

	spin_unlock_bh(&ab->ce.ce_lock);

	return 0;

err_unlock:
	spin_unlock_bh(&srng->lock);

	spin_unlock_bh(&ab->ce.ce_lock);

	return ret;
}

static void ath11k_ce_rx_pipe_cleanup(struct ath11k_ce_pipe *pipe)
{
	struct ath11k_base *ab = pipe->ab;
	struct ath11k_ce_ring *ring = pipe->dest_ring;
	struct sk_buff *skb;
	int i;

	if (!(ring && pipe->buf_sz))
		return;

	for (i = 0; i < ring->nentries; i++) {
		skb = ring->skb[i];
		if (!skb)
			continue;

		ring->skb[i] = NULL;
		dma_unmap_single(ab->dev, ATH11K_SKB_RXCB(skb)->paddr,
				 skb->len + skb_tailroom(skb), DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
	}
}

static void ath11k_ce_shadow_config(struct ath11k_base *ab)
{
	int i;

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		if (ab->hw_params.host_ce_config[i].src_nentries)
			ath11k_hal_srng_update_shadow_config(ab,
							     HAL_CE_SRC, i);

		if (ab->hw_params.host_ce_config[i].dest_nentries) {
			ath11k_hal_srng_update_shadow_config(ab,
							     HAL_CE_DST, i);

			ath11k_hal_srng_update_shadow_config(ab,
							     HAL_CE_DST_STATUS, i);
		}
	}
}

void ath11k_ce_get_shadow_config(struct ath11k_base *ab,
				 u32 **shadow_cfg, u32 *shadow_cfg_len)
{
	if (!ab->hw_params.supports_shadow_regs)
		return;

	ath11k_hal_srng_get_shadow_config(ab, shadow_cfg, shadow_cfg_len);

	/* shadow is already configured */
	if (*shadow_cfg_len)
		return;

	/* shadow isn't configured yet, configure now.
	 * non-CE srngs are configured firstly, then
	 * all CE srngs.
	 */
	ath11k_hal_srng_shadow_config(ab);
	ath11k_ce_shadow_config(ab);

	/* get the shadow configuration */
	ath11k_hal_srng_get_shadow_config(ab, shadow_cfg, shadow_cfg_len);
}
EXPORT_SYMBOL(ath11k_ce_get_shadow_config);

void ath11k_ce_cleanup_pipes(struct ath11k_base *ab)
{
	struct ath11k_ce_pipe *pipe;
	int pipe_num;

	ath11k_ce_stop_shadow_timers(ab);

	for (pipe_num = 0; pipe_num < ab->hw_params.ce_count; pipe_num++) {
		pipe = &ab->ce.ce_pipe[pipe_num];
		ath11k_ce_rx_pipe_cleanup(pipe);

		/* Cleanup any src CE's which have interrupts disabled */
		ath11k_ce_poll_send_completed(ab, pipe_num);

		/* NOTE: Should we also clean up tx buffer in all pipes? */
	}
}
EXPORT_SYMBOL(ath11k_ce_cleanup_pipes);

void ath11k_ce_rx_post_buf(struct ath11k_base *ab)
{
	struct ath11k_ce_pipe *pipe;
	int i;
	int ret;

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		pipe = &ab->ce.ce_pipe[i];
		ret = ath11k_ce_rx_post_pipe(pipe);
		if (ret) {
			if (ret == -ENOSPC)
				continue;

			ath11k_warn(ab, "failed to post rx buf to pipe: %d err: %d\n",
				    i, ret);
			mod_timer(&ab->rx_replenish_retry,
				  jiffies + ATH11K_CE_RX_POST_RETRY_JIFFIES);

			return;
		}
	}
}
EXPORT_SYMBOL(ath11k_ce_rx_post_buf);

void ath11k_ce_rx_replenish_retry(struct timer_list *t)
{
	struct ath11k_base *ab = from_timer(ab, t, rx_replenish_retry);

	ath11k_ce_rx_post_buf(ab);
}

int ath11k_ce_init_pipes(struct ath11k_base *ab)
{
	struct ath11k_ce_pipe *pipe;
	int i;
	int ret;

	ath11k_ce_get_shadow_config(ab, &ab->qmi.ce_cfg.shadow_reg_v2,
				    &ab->qmi.ce_cfg.shadow_reg_v2_len);

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		pipe = &ab->ce.ce_pipe[i];

		if (pipe->src_ring) {
			ret = ath11k_ce_init_ring(ab, pipe->src_ring, i,
						  HAL_CE_SRC);
			if (ret) {
				ath11k_warn(ab, "failed to init src ring: %d\n",
					    ret);
				/* Should we clear any partial init */
				return ret;
			}

			pipe->src_ring->write_index = 0;
			pipe->src_ring->sw_index = 0;
		}

		if (pipe->dest_ring) {
			ret = ath11k_ce_init_ring(ab, pipe->dest_ring, i,
						  HAL_CE_DST);
			if (ret) {
				ath11k_warn(ab, "failed to init dest ring: %d\n",
					    ret);
				/* Should we clear any partial init */
				return ret;
			}

			pipe->rx_buf_needed = pipe->dest_ring->nentries ?
					      pipe->dest_ring->nentries - 2 : 0;

			pipe->dest_ring->write_index = 0;
			pipe->dest_ring->sw_index = 0;
		}

		if (pipe->status_ring) {
			ret = ath11k_ce_init_ring(ab, pipe->status_ring, i,
						  HAL_CE_DST_STATUS);
			if (ret) {
				ath11k_warn(ab, "failed to init dest status ing: %d\n",
					    ret);
				/* Should we clear any partial init */
				return ret;
			}

			pipe->status_ring->write_index = 0;
			pipe->status_ring->sw_index = 0;
		}
	}

	return 0;
}

void ath11k_ce_free_pipes(struct ath11k_base *ab)
{
	struct ath11k_ce_pipe *pipe;
	struct ath11k_ce_ring *ce_ring;
	int desc_sz;
	int i;

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		pipe = &ab->ce.ce_pipe[i];

		if (ath11k_ce_need_shadow_fix(i))
			ath11k_dp_shadow_stop_timer(ab, &ab->ce.hp_timer[i]);

		if (pipe->src_ring) {
			desc_sz = ath11k_hal_ce_get_desc_size(HAL_CE_DESC_SRC);
			ce_ring = pipe->src_ring;
			dma_free_coherent(ab->dev,
					  pipe->src_ring->nentries * desc_sz +
					  CE_DESC_RING_ALIGN,
					  ce_ring->base_addr_owner_space_unaligned,
					  ce_ring->base_addr_ce_space_unaligned);
			kfree(pipe->src_ring);
			pipe->src_ring = NULL;
		}

		if (pipe->dest_ring) {
			desc_sz = ath11k_hal_ce_get_desc_size(HAL_CE_DESC_DST);
			ce_ring = pipe->dest_ring;
			dma_free_coherent(ab->dev,
					  pipe->dest_ring->nentries * desc_sz +
					  CE_DESC_RING_ALIGN,
					  ce_ring->base_addr_owner_space_unaligned,
					  ce_ring->base_addr_ce_space_unaligned);
			kfree(pipe->dest_ring);
			pipe->dest_ring = NULL;
		}

		if (pipe->status_ring) {
			desc_sz =
			  ath11k_hal_ce_get_desc_size(HAL_CE_DESC_DST_STATUS);
			ce_ring = pipe->status_ring;
			dma_free_coherent(ab->dev,
					  pipe->status_ring->nentries * desc_sz +
					  CE_DESC_RING_ALIGN,
					  ce_ring->base_addr_owner_space_unaligned,
					  ce_ring->base_addr_ce_space_unaligned);
			kfree(pipe->status_ring);
			pipe->status_ring = NULL;
		}
	}
}
EXPORT_SYMBOL(ath11k_ce_free_pipes);

int ath11k_ce_alloc_pipes(struct ath11k_base *ab)
{
	struct ath11k_ce_pipe *pipe;
	int i;
	int ret;
	const struct ce_attr *attr;

	spin_lock_init(&ab->ce.ce_lock);

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		attr = &ab->hw_params.host_ce_config[i];
		pipe = &ab->ce.ce_pipe[i];
		pipe->pipe_num = i;
		pipe->ab = ab;
		pipe->buf_sz = attr->src_sz_max;

		ret = ath11k_ce_alloc_pipe(ab, i);
		if (ret) {
			/* Free any parial successful allocation */
			ath11k_ce_free_pipes(ab);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL(ath11k_ce_alloc_pipes);

/* For Big Endian Host, Copy Engine byte_swap is enabled
 * When Copy Engine does byte_swap, need to byte swap again for the
 * Host to get/put buffer content in the correct byte order
 */
void ath11k_ce_byte_swap(void *mem, u32 len)
{
	int i;

	if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN)) {
		if (!mem)
			return;

		for (i = 0; i < (len / 4); i++) {
			*(u32 *)mem = swab32(*(u32 *)mem);
			mem += 4;
		}
	}
}

int ath11k_ce_get_attr_flags(struct ath11k_base *ab, int ce_id)
{
	if (ce_id >= ab->hw_params.ce_count)
		return -EINVAL;

	return ab->hw_params.host_ce_config[ce_id].flags;
}
EXPORT_SYMBOL(ath11k_ce_get_attr_flags);
