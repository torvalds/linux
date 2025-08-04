// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "dp_rx.h"
#include "debug.h"
#include "hif.h"

const struct ce_attr ath12k_host_ce_config_qcn9274[] = {
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
		.recv_cb = ath12k_htc_rx_completion_handler,
	},

	/* CE2: target->host WMI */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 128,
		.recv_cb = ath12k_htc_rx_completion_handler,
	},

	/* CE3: host->target WMI (mac0) */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 32,
		.src_sz_max = 2048,
		.dest_nentries = 0,
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
		.recv_cb = ath12k_dp_htt_htc_t2h_msg_handler,
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
	},

	/* CE8: target autonomous hif_memcpy */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},

	/* CE9: MHI */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},

	/* CE10: MHI */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},

	/* CE11: MHI */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},

	/* CE12: CV Prefetch */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},

	/* CE13: CV Prefetch */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},

	/* CE14: target->host dbg log */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = ath12k_htc_rx_completion_handler,
	},

	/* CE15: reserved for future use */
	{
		.flags = (CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},
};

const struct ce_attr ath12k_host_ce_config_wcn7850[] = {
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
		.recv_cb = ath12k_htc_rx_completion_handler,
	},

	/* CE2: target->host WMI */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 64,
		.recv_cb = ath12k_htc_rx_completion_handler,
	},

	/* CE3: host->target WMI (mac0) */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 32,
		.src_sz_max = 2048,
		.dest_nentries = 0,
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
		.src_sz_max = 0,
		.dest_nentries = 0,
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
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 0,
	},

	/* CE8: target autonomous hif_memcpy */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},

};

const struct ce_attr ath12k_host_ce_config_ipq5332[] = {
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
		.recv_cb = ath12k_htc_rx_completion_handler,
	},
	/* CE2: target->host WMI */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 128,
		.recv_cb = ath12k_htc_rx_completion_handler,
	},
	/* CE3: host->target WMI */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 32,
		.src_sz_max = 2048,
		.dest_nentries = 0,
	},
	/* CE4: host->target HTT */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 2048,
		.src_sz_max = 256,
		.dest_nentries = 0,
	},
	/* CE5: target -> host PKTLOG */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = ath12k_dp_htt_htc_t2h_msg_handler,
	},
	/* CE6: Target autonomous HIF_memcpy */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},
	/* CE7: CV Prefetch */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},
	/* CE8: Target HIF memcpy (Generic HIF memcypy) */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},
	/* CE9: WMI logging/CFR/Spectral/Radar */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 128,
	},
	/* CE10: Unused */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},
	/* CE11: Unused */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},
};

static int ath12k_ce_rx_buf_enqueue_pipe(struct ath12k_ce_pipe *pipe,
					 struct sk_buff *skb, dma_addr_t paddr)
{
	struct ath12k_base *ab = pipe->ab;
	struct ath12k_ce_ring *ring = pipe->dest_ring;
	struct hal_srng *srng;
	unsigned int write_index;
	unsigned int nentries_mask = ring->nentries_mask;
	struct hal_ce_srng_dest_desc *desc;
	int ret;

	lockdep_assert_held(&ab->ce.ce_lock);

	write_index = ring->write_index;

	srng = &ab->hal.srng_list[ring->hal_ring_id];

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	if (unlikely(ath12k_hal_srng_src_num_free(ab, srng, false) < 1)) {
		ret = -ENOSPC;
		goto exit;
	}

	desc = ath12k_hal_srng_src_get_next_entry(ab, srng);
	if (!desc) {
		ret = -ENOSPC;
		goto exit;
	}

	ath12k_hal_ce_dst_set_desc(desc, paddr);

	ring->skb[write_index] = skb;
	write_index = CE_RING_IDX_INCR(nentries_mask, write_index);
	ring->write_index = write_index;

	pipe->rx_buf_needed--;

	ret = 0;
exit:
	ath12k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	return ret;
}

static int ath12k_ce_rx_post_pipe(struct ath12k_ce_pipe *pipe)
{
	struct ath12k_base *ab = pipe->ab;
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
			ath12k_warn(ab, "failed to dma map ce rx buf\n");
			dev_kfree_skb_any(skb);
			ret = -EIO;
			goto exit;
		}

		ATH12K_SKB_RXCB(skb)->paddr = paddr;

		ret = ath12k_ce_rx_buf_enqueue_pipe(pipe, skb, paddr);
		if (ret) {
			ath12k_warn(ab, "failed to enqueue rx buf: %d\n", ret);
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

static int ath12k_ce_completed_recv_next(struct ath12k_ce_pipe *pipe,
					 struct sk_buff **skb, int *nbytes)
{
	struct ath12k_base *ab = pipe->ab;
	struct hal_ce_srng_dst_status_desc *desc;
	struct hal_srng *srng;
	unsigned int sw_index;
	unsigned int nentries_mask;
	int ret = 0;

	spin_lock_bh(&ab->ce.ce_lock);

	sw_index = pipe->dest_ring->sw_index;
	nentries_mask = pipe->dest_ring->nentries_mask;

	srng = &ab->hal.srng_list[pipe->status_ring->hal_ring_id];

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	desc = ath12k_hal_srng_dst_get_next_entry(ab, srng);
	if (!desc) {
		ret = -EIO;
		goto err;
	}

	/* Make sure descriptor is read after the head pointer. */
	dma_rmb();

	*nbytes = ath12k_hal_ce_dst_status_get_length(desc);

	*skb = pipe->dest_ring->skb[sw_index];
	pipe->dest_ring->skb[sw_index] = NULL;

	sw_index = CE_RING_IDX_INCR(nentries_mask, sw_index);
	pipe->dest_ring->sw_index = sw_index;

	pipe->rx_buf_needed++;
err:
	ath12k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	spin_unlock_bh(&ab->ce.ce_lock);

	return ret;
}

static void ath12k_ce_recv_process_cb(struct ath12k_ce_pipe *pipe)
{
	struct ath12k_base *ab = pipe->ab;
	struct sk_buff *skb;
	struct sk_buff_head list;
	unsigned int nbytes, max_nbytes;
	int ret;

	__skb_queue_head_init(&list);
	while (ath12k_ce_completed_recv_next(pipe, &skb, &nbytes) == 0) {
		max_nbytes = skb->len + skb_tailroom(skb);
		dma_unmap_single(ab->dev, ATH12K_SKB_RXCB(skb)->paddr,
				 max_nbytes, DMA_FROM_DEVICE);

		if (unlikely(max_nbytes < nbytes || nbytes == 0)) {
			ath12k_warn(ab, "unexpected rx length (nbytes %d, max %d)",
				    nbytes, max_nbytes);
			dev_kfree_skb_any(skb);
			continue;
		}

		skb_put(skb, nbytes);
		__skb_queue_tail(&list, skb);
	}

	while ((skb = __skb_dequeue(&list))) {
		ath12k_dbg(ab, ATH12K_DBG_AHB, "rx ce pipe %d len %d\n",
			   pipe->pipe_num, skb->len);
		pipe->recv_cb(ab, skb);
	}

	ret = ath12k_ce_rx_post_pipe(pipe);
	if (ret && ret != -ENOSPC) {
		ath12k_warn(ab, "failed to post rx buf to pipe: %d err: %d\n",
			    pipe->pipe_num, ret);
		mod_timer(&ab->rx_replenish_retry,
			  jiffies + ATH12K_CE_RX_POST_RETRY_JIFFIES);
	}
}

static struct sk_buff *ath12k_ce_completed_send_next(struct ath12k_ce_pipe *pipe)
{
	struct ath12k_base *ab = pipe->ab;
	struct hal_ce_srng_src_desc *desc;
	struct hal_srng *srng;
	unsigned int sw_index;
	unsigned int nentries_mask;
	struct sk_buff *skb;

	spin_lock_bh(&ab->ce.ce_lock);

	sw_index = pipe->src_ring->sw_index;
	nentries_mask = pipe->src_ring->nentries_mask;

	srng = &ab->hal.srng_list[pipe->src_ring->hal_ring_id];

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	desc = ath12k_hal_srng_src_reap_next(ab, srng);
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

static void ath12k_ce_send_done_cb(struct ath12k_ce_pipe *pipe)
{
	struct ath12k_base *ab = pipe->ab;
	struct sk_buff *skb;

	while (!IS_ERR(skb = ath12k_ce_completed_send_next(pipe))) {
		if (!skb)
			continue;

		dma_unmap_single(ab->dev, ATH12K_SKB_CB(skb)->paddr, skb->len,
				 DMA_TO_DEVICE);
		dev_kfree_skb_any(skb);
	}
}

static void ath12k_ce_srng_msi_ring_params_setup(struct ath12k_base *ab, u32 ce_id,
						 struct hal_srng_params *ring_params)
{
	u32 msi_data_start;
	u32 msi_data_count, msi_data_idx;
	u32 msi_irq_start;
	u32 addr_lo;
	u32 addr_hi;
	int ret;

	ret = ath12k_hif_get_user_msi_vector(ab, "CE",
					     &msi_data_count, &msi_data_start,
					     &msi_irq_start);

	if (ret)
		return;

	ath12k_hif_get_msi_address(ab, &addr_lo, &addr_hi);
	ath12k_hif_get_ce_msi_idx(ab, ce_id, &msi_data_idx);

	ring_params->msi_addr = addr_lo;
	ring_params->msi_addr |= (dma_addr_t)(((uint64_t)addr_hi) << 32);
	ring_params->msi_data = (msi_data_idx % msi_data_count) + msi_data_start;
	ring_params->flags |= HAL_SRNG_FLAGS_MSI_INTR;
}

static int ath12k_ce_init_ring(struct ath12k_base *ab,
			       struct ath12k_ce_ring *ce_ring,
			       int ce_id, enum hal_ring_type type)
{
	struct hal_srng_params params = { 0 };
	int ret;

	params.ring_base_paddr = ce_ring->base_addr_ce_space;
	params.ring_base_vaddr = ce_ring->base_addr_owner_space;
	params.num_entries = ce_ring->nentries;

	if (!(CE_ATTR_DIS_INTR & ab->hw_params->host_ce_config[ce_id].flags))
		ath12k_ce_srng_msi_ring_params_setup(ab, ce_id, &params);

	switch (type) {
	case HAL_CE_SRC:
		if (!(CE_ATTR_DIS_INTR & ab->hw_params->host_ce_config[ce_id].flags))
			params.intr_batch_cntr_thres_entries = 1;
		break;
	case HAL_CE_DST:
		params.max_buffer_len = ab->hw_params->host_ce_config[ce_id].src_sz_max;
		if (!(ab->hw_params->host_ce_config[ce_id].flags & CE_ATTR_DIS_INTR)) {
			params.intr_timer_thres_us = 1024;
			params.flags |= HAL_SRNG_FLAGS_LOW_THRESH_INTR_EN;
			params.low_threshold = ce_ring->nentries - 3;
		}
		break;
	case HAL_CE_DST_STATUS:
		if (!(ab->hw_params->host_ce_config[ce_id].flags & CE_ATTR_DIS_INTR)) {
			params.intr_batch_cntr_thres_entries = 1;
			params.intr_timer_thres_us = 0x1000;
		}
		break;
	default:
		ath12k_warn(ab, "Invalid CE ring type %d\n", type);
		return -EINVAL;
	}

	/* TODO: Init other params needed by HAL to init the ring */

	ret = ath12k_hal_srng_setup(ab, type, ce_id, 0, &params);
	if (ret < 0) {
		ath12k_warn(ab, "failed to setup srng: %d ring_id %d\n",
			    ret, ce_id);
		return ret;
	}

	ce_ring->hal_ring_id = ret;

	return 0;
}

static struct ath12k_ce_ring *
ath12k_ce_alloc_ring(struct ath12k_base *ab, int nentries, int desc_sz)
{
	struct ath12k_ce_ring *ce_ring;
	dma_addr_t base_addr;

	ce_ring = kzalloc(struct_size(ce_ring, skb, nentries), GFP_KERNEL);
	if (!ce_ring)
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

	ce_ring->base_addr_owner_space =
		PTR_ALIGN(ce_ring->base_addr_owner_space_unaligned,
			  CE_DESC_RING_ALIGN);

	ce_ring->base_addr_ce_space = ALIGN(ce_ring->base_addr_ce_space_unaligned,
					    CE_DESC_RING_ALIGN);

	return ce_ring;
}

static int ath12k_ce_alloc_pipe(struct ath12k_base *ab, int ce_id)
{
	struct ath12k_ce_pipe *pipe = &ab->ce.ce_pipe[ce_id];
	const struct ce_attr *attr = &ab->hw_params->host_ce_config[ce_id];
	struct ath12k_ce_ring *ring;
	int nentries;
	int desc_sz;

	pipe->attr_flags = attr->flags;

	if (attr->src_nentries) {
		pipe->send_cb = ath12k_ce_send_done_cb;
		nentries = roundup_pow_of_two(attr->src_nentries);
		desc_sz = ath12k_hal_ce_get_desc_size(HAL_CE_DESC_SRC);
		ring = ath12k_ce_alloc_ring(ab, nentries, desc_sz);
		if (IS_ERR(ring))
			return PTR_ERR(ring);
		pipe->src_ring = ring;
	}

	if (attr->dest_nentries) {
		pipe->recv_cb = attr->recv_cb;
		nentries = roundup_pow_of_two(attr->dest_nentries);
		desc_sz = ath12k_hal_ce_get_desc_size(HAL_CE_DESC_DST);
		ring = ath12k_ce_alloc_ring(ab, nentries, desc_sz);
		if (IS_ERR(ring))
			return PTR_ERR(ring);
		pipe->dest_ring = ring;

		desc_sz = ath12k_hal_ce_get_desc_size(HAL_CE_DESC_DST_STATUS);
		ring = ath12k_ce_alloc_ring(ab, nentries, desc_sz);
		if (IS_ERR(ring))
			return PTR_ERR(ring);
		pipe->status_ring = ring;
	}

	return 0;
}

void ath12k_ce_per_engine_service(struct ath12k_base *ab, u16 ce_id)
{
	struct ath12k_ce_pipe *pipe = &ab->ce.ce_pipe[ce_id];

	if (pipe->send_cb)
		pipe->send_cb(pipe);

	if (pipe->recv_cb)
		ath12k_ce_recv_process_cb(pipe);
}

void ath12k_ce_poll_send_completed(struct ath12k_base *ab, u8 pipe_id)
{
	struct ath12k_ce_pipe *pipe = &ab->ce.ce_pipe[pipe_id];

	if ((pipe->attr_flags & CE_ATTR_DIS_INTR) && pipe->send_cb)
		pipe->send_cb(pipe);
}

int ath12k_ce_send(struct ath12k_base *ab, struct sk_buff *skb, u8 pipe_id,
		   u16 transfer_id)
{
	struct ath12k_ce_pipe *pipe = &ab->ce.ce_pipe[pipe_id];
	struct hal_ce_srng_src_desc *desc;
	struct hal_srng *srng;
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

		if (num_used > ATH12K_CE_USAGE_THRESHOLD)
			ath12k_ce_poll_send_completed(ab, pipe->pipe_num);
	}

	if (test_bit(ATH12K_FLAG_CRASH_FLUSH, &ab->dev_flags))
		return -ESHUTDOWN;

	spin_lock_bh(&ab->ce.ce_lock);

	write_index = pipe->src_ring->write_index;
	nentries_mask = pipe->src_ring->nentries_mask;

	srng = &ab->hal.srng_list[pipe->src_ring->hal_ring_id];

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	if (unlikely(ath12k_hal_srng_src_num_free(ab, srng, false) < 1)) {
		ath12k_hal_srng_access_end(ab, srng);
		ret = -ENOBUFS;
		goto unlock;
	}

	desc = ath12k_hal_srng_src_get_next_reaped(ab, srng);
	if (!desc) {
		ath12k_hal_srng_access_end(ab, srng);
		ret = -ENOBUFS;
		goto unlock;
	}

	if (pipe->attr_flags & CE_ATTR_BYTE_SWAP_DATA)
		byte_swap_data = 1;

	ath12k_hal_ce_src_set_desc(desc, ATH12K_SKB_CB(skb)->paddr,
				   skb->len, transfer_id, byte_swap_data);

	pipe->src_ring->skb[write_index] = skb;
	pipe->src_ring->write_index = CE_RING_IDX_INCR(nentries_mask,
						       write_index);

	ath12k_hal_srng_access_end(ab, srng);

unlock:
	spin_unlock_bh(&srng->lock);

	spin_unlock_bh(&ab->ce.ce_lock);

	return ret;
}

static void ath12k_ce_rx_pipe_cleanup(struct ath12k_ce_pipe *pipe)
{
	struct ath12k_base *ab = pipe->ab;
	struct ath12k_ce_ring *ring = pipe->dest_ring;
	struct sk_buff *skb;
	int i;

	if (!(ring && pipe->buf_sz))
		return;

	for (i = 0; i < ring->nentries; i++) {
		skb = ring->skb[i];
		if (!skb)
			continue;

		ring->skb[i] = NULL;
		dma_unmap_single(ab->dev, ATH12K_SKB_RXCB(skb)->paddr,
				 skb->len + skb_tailroom(skb), DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
	}
}

void ath12k_ce_cleanup_pipes(struct ath12k_base *ab)
{
	struct ath12k_ce_pipe *pipe;
	int pipe_num;

	for (pipe_num = 0; pipe_num < ab->hw_params->ce_count; pipe_num++) {
		pipe = &ab->ce.ce_pipe[pipe_num];
		ath12k_ce_rx_pipe_cleanup(pipe);

		/* Cleanup any src CE's which have interrupts disabled */
		ath12k_ce_poll_send_completed(ab, pipe_num);

		/* NOTE: Should we also clean up tx buffer in all pipes? */
	}
}

void ath12k_ce_rx_post_buf(struct ath12k_base *ab)
{
	struct ath12k_ce_pipe *pipe;
	int i;
	int ret;

	for (i = 0; i < ab->hw_params->ce_count; i++) {
		pipe = &ab->ce.ce_pipe[i];
		ret = ath12k_ce_rx_post_pipe(pipe);
		if (ret) {
			if (ret == -ENOSPC)
				continue;

			ath12k_warn(ab, "failed to post rx buf to pipe: %d err: %d\n",
				    i, ret);
			mod_timer(&ab->rx_replenish_retry,
				  jiffies + ATH12K_CE_RX_POST_RETRY_JIFFIES);

			return;
		}
	}
}

void ath12k_ce_rx_replenish_retry(struct timer_list *t)
{
	struct ath12k_base *ab = timer_container_of(ab, t, rx_replenish_retry);

	ath12k_ce_rx_post_buf(ab);
}

static void ath12k_ce_shadow_config(struct ath12k_base *ab)
{
	int i;

	for (i = 0; i < ab->hw_params->ce_count; i++) {
		if (ab->hw_params->host_ce_config[i].src_nentries)
			ath12k_hal_srng_update_shadow_config(ab, HAL_CE_SRC, i);

		if (ab->hw_params->host_ce_config[i].dest_nentries) {
			ath12k_hal_srng_update_shadow_config(ab, HAL_CE_DST, i);
			ath12k_hal_srng_update_shadow_config(ab, HAL_CE_DST_STATUS, i);
		}
	}
}

void ath12k_ce_get_shadow_config(struct ath12k_base *ab,
				 u32 **shadow_cfg, u32 *shadow_cfg_len)
{
	if (!ab->hw_params->supports_shadow_regs)
		return;

	ath12k_hal_srng_get_shadow_config(ab, shadow_cfg, shadow_cfg_len);

	/* shadow is already configured */
	if (*shadow_cfg_len)
		return;

	/* shadow isn't configured yet, configure now.
	 * non-CE srngs are configured firstly, then
	 * all CE srngs.
	 */
	ath12k_hal_srng_shadow_config(ab);
	ath12k_ce_shadow_config(ab);

	/* get the shadow configuration */
	ath12k_hal_srng_get_shadow_config(ab, shadow_cfg, shadow_cfg_len);
}

int ath12k_ce_init_pipes(struct ath12k_base *ab)
{
	struct ath12k_ce_pipe *pipe;
	int i;
	int ret;

	ath12k_ce_get_shadow_config(ab, &ab->qmi.ce_cfg.shadow_reg_v3,
				    &ab->qmi.ce_cfg.shadow_reg_v3_len);

	for (i = 0; i < ab->hw_params->ce_count; i++) {
		pipe = &ab->ce.ce_pipe[i];

		if (pipe->src_ring) {
			ret = ath12k_ce_init_ring(ab, pipe->src_ring, i,
						  HAL_CE_SRC);
			if (ret) {
				ath12k_warn(ab, "failed to init src ring: %d\n",
					    ret);
				/* Should we clear any partial init */
				return ret;
			}

			pipe->src_ring->write_index = 0;
			pipe->src_ring->sw_index = 0;
		}

		if (pipe->dest_ring) {
			ret = ath12k_ce_init_ring(ab, pipe->dest_ring, i,
						  HAL_CE_DST);
			if (ret) {
				ath12k_warn(ab, "failed to init dest ring: %d\n",
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
			ret = ath12k_ce_init_ring(ab, pipe->status_ring, i,
						  HAL_CE_DST_STATUS);
			if (ret) {
				ath12k_warn(ab, "failed to init dest status ing: %d\n",
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

void ath12k_ce_free_pipes(struct ath12k_base *ab)
{
	struct ath12k_ce_pipe *pipe;
	int desc_sz;
	int i;

	for (i = 0; i < ab->hw_params->ce_count; i++) {
		pipe = &ab->ce.ce_pipe[i];

		if (pipe->src_ring) {
			desc_sz = ath12k_hal_ce_get_desc_size(HAL_CE_DESC_SRC);
			dma_free_coherent(ab->dev,
					  pipe->src_ring->nentries * desc_sz +
					  CE_DESC_RING_ALIGN,
					  pipe->src_ring->base_addr_owner_space,
					  pipe->src_ring->base_addr_ce_space);
			kfree(pipe->src_ring);
			pipe->src_ring = NULL;
		}

		if (pipe->dest_ring) {
			desc_sz = ath12k_hal_ce_get_desc_size(HAL_CE_DESC_DST);
			dma_free_coherent(ab->dev,
					  pipe->dest_ring->nentries * desc_sz +
					  CE_DESC_RING_ALIGN,
					  pipe->dest_ring->base_addr_owner_space,
					  pipe->dest_ring->base_addr_ce_space);
			kfree(pipe->dest_ring);
			pipe->dest_ring = NULL;
		}

		if (pipe->status_ring) {
			desc_sz =
			  ath12k_hal_ce_get_desc_size(HAL_CE_DESC_DST_STATUS);
			dma_free_coherent(ab->dev,
					  pipe->status_ring->nentries * desc_sz +
					  CE_DESC_RING_ALIGN,
					  pipe->status_ring->base_addr_owner_space,
					  pipe->status_ring->base_addr_ce_space);
			kfree(pipe->status_ring);
			pipe->status_ring = NULL;
		}
	}
}

int ath12k_ce_alloc_pipes(struct ath12k_base *ab)
{
	struct ath12k_ce_pipe *pipe;
	int i;
	int ret;
	const struct ce_attr *attr;

	spin_lock_init(&ab->ce.ce_lock);

	for (i = 0; i < ab->hw_params->ce_count; i++) {
		attr = &ab->hw_params->host_ce_config[i];
		pipe = &ab->ce.ce_pipe[i];
		pipe->pipe_num = i;
		pipe->ab = ab;
		pipe->buf_sz = attr->src_sz_max;

		ret = ath12k_ce_alloc_pipe(ab, i);
		if (ret) {
			/* Free any partial successful allocation */
			ath12k_ce_free_pipes(ab);
			return ret;
		}
	}

	return 0;
}

int ath12k_ce_get_attr_flags(struct ath12k_base *ab, int ce_id)
{
	if (ce_id >= ab->hw_params->ce_count)
		return -EINVAL;

	return ab->hw_params->host_ce_config[ce_id].flags;
}
