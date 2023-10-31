// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Intel Corporation. */

#include <linux/bpf_trace.h>
#include <net/xdp_sock_drv.h>
#include <net/xdp.h>
#include "ice.h"
#include "ice_base.h"
#include "ice_type.h"
#include "ice_xsk.h"
#include "ice_txrx.h"
#include "ice_txrx_lib.h"
#include "ice_lib.h"

static struct xdp_buff **ice_xdp_buf(struct ice_rx_ring *rx_ring, u32 idx)
{
	return &rx_ring->xdp_buf[idx];
}

/**
 * ice_qp_reset_stats - Resets all stats for rings of given index
 * @vsi: VSI that contains rings of interest
 * @q_idx: ring index in array
 */
static void ice_qp_reset_stats(struct ice_vsi *vsi, u16 q_idx)
{
	struct ice_vsi_stats *vsi_stat;
	struct ice_pf *pf;

	pf = vsi->back;
	if (!pf->vsi_stats)
		return;

	vsi_stat = pf->vsi_stats[vsi->idx];
	if (!vsi_stat)
		return;

	memset(&vsi_stat->rx_ring_stats[q_idx]->rx_stats, 0,
	       sizeof(vsi_stat->rx_ring_stats[q_idx]->rx_stats));
	memset(&vsi_stat->tx_ring_stats[q_idx]->stats, 0,
	       sizeof(vsi_stat->tx_ring_stats[q_idx]->stats));
	if (ice_is_xdp_ena_vsi(vsi))
		memset(&vsi->xdp_rings[q_idx]->ring_stats->stats, 0,
		       sizeof(vsi->xdp_rings[q_idx]->ring_stats->stats));
}

/**
 * ice_qp_clean_rings - Cleans all the rings of a given index
 * @vsi: VSI that contains rings of interest
 * @q_idx: ring index in array
 */
static void ice_qp_clean_rings(struct ice_vsi *vsi, u16 q_idx)
{
	ice_clean_tx_ring(vsi->tx_rings[q_idx]);
	if (ice_is_xdp_ena_vsi(vsi)) {
		synchronize_rcu();
		ice_clean_tx_ring(vsi->xdp_rings[q_idx]);
	}
	ice_clean_rx_ring(vsi->rx_rings[q_idx]);
}

/**
 * ice_qvec_toggle_napi - Enables/disables NAPI for a given q_vector
 * @vsi: VSI that has netdev
 * @q_vector: q_vector that has NAPI context
 * @enable: true for enable, false for disable
 */
static void
ice_qvec_toggle_napi(struct ice_vsi *vsi, struct ice_q_vector *q_vector,
		     bool enable)
{
	if (!vsi->netdev || !q_vector)
		return;

	if (enable)
		napi_enable(&q_vector->napi);
	else
		napi_disable(&q_vector->napi);
}

/**
 * ice_qvec_dis_irq - Mask off queue interrupt generation on given ring
 * @vsi: the VSI that contains queue vector being un-configured
 * @rx_ring: Rx ring that will have its IRQ disabled
 * @q_vector: queue vector
 */
static void
ice_qvec_dis_irq(struct ice_vsi *vsi, struct ice_rx_ring *rx_ring,
		 struct ice_q_vector *q_vector)
{
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	u16 reg;
	u32 val;

	/* QINT_TQCTL is being cleared in ice_vsi_stop_tx_ring, so handle
	 * here only QINT_RQCTL
	 */
	reg = rx_ring->reg_idx;
	val = rd32(hw, QINT_RQCTL(reg));
	val &= ~QINT_RQCTL_CAUSE_ENA_M;
	wr32(hw, QINT_RQCTL(reg), val);

	if (q_vector) {
		wr32(hw, GLINT_DYN_CTL(q_vector->reg_idx), 0);
		ice_flush(hw);
		synchronize_irq(q_vector->irq.virq);
	}
}

/**
 * ice_qvec_cfg_msix - Enable IRQ for given queue vector
 * @vsi: the VSI that contains queue vector
 * @q_vector: queue vector
 */
static void
ice_qvec_cfg_msix(struct ice_vsi *vsi, struct ice_q_vector *q_vector)
{
	u16 reg_idx = q_vector->reg_idx;
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	struct ice_tx_ring *tx_ring;
	struct ice_rx_ring *rx_ring;

	ice_cfg_itr(hw, q_vector);

	ice_for_each_tx_ring(tx_ring, q_vector->tx)
		ice_cfg_txq_interrupt(vsi, tx_ring->reg_idx, reg_idx,
				      q_vector->tx.itr_idx);

	ice_for_each_rx_ring(rx_ring, q_vector->rx)
		ice_cfg_rxq_interrupt(vsi, rx_ring->reg_idx, reg_idx,
				      q_vector->rx.itr_idx);

	ice_flush(hw);
}

/**
 * ice_qvec_ena_irq - Enable IRQ for given queue vector
 * @vsi: the VSI that contains queue vector
 * @q_vector: queue vector
 */
static void ice_qvec_ena_irq(struct ice_vsi *vsi, struct ice_q_vector *q_vector)
{
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;

	ice_irq_dynamic_ena(hw, vsi, q_vector);

	ice_flush(hw);
}

/**
 * ice_qp_dis - Disables a queue pair
 * @vsi: VSI of interest
 * @q_idx: ring index in array
 *
 * Returns 0 on success, negative on failure.
 */
static int ice_qp_dis(struct ice_vsi *vsi, u16 q_idx)
{
	struct ice_txq_meta txq_meta = { };
	struct ice_q_vector *q_vector;
	struct ice_tx_ring *tx_ring;
	struct ice_rx_ring *rx_ring;
	int timeout = 50;
	int err;

	if (q_idx >= vsi->num_rxq || q_idx >= vsi->num_txq)
		return -EINVAL;

	tx_ring = vsi->tx_rings[q_idx];
	rx_ring = vsi->rx_rings[q_idx];
	q_vector = rx_ring->q_vector;

	while (test_and_set_bit(ICE_CFG_BUSY, vsi->state)) {
		timeout--;
		if (!timeout)
			return -EBUSY;
		usleep_range(1000, 2000);
	}
	netif_tx_stop_queue(netdev_get_tx_queue(vsi->netdev, q_idx));

	ice_fill_txq_meta(vsi, tx_ring, &txq_meta);
	err = ice_vsi_stop_tx_ring(vsi, ICE_NO_RESET, 0, tx_ring, &txq_meta);
	if (err)
		return err;
	if (ice_is_xdp_ena_vsi(vsi)) {
		struct ice_tx_ring *xdp_ring = vsi->xdp_rings[q_idx];

		memset(&txq_meta, 0, sizeof(txq_meta));
		ice_fill_txq_meta(vsi, xdp_ring, &txq_meta);
		err = ice_vsi_stop_tx_ring(vsi, ICE_NO_RESET, 0, xdp_ring,
					   &txq_meta);
		if (err)
			return err;
	}
	ice_qvec_dis_irq(vsi, rx_ring, q_vector);

	err = ice_vsi_ctrl_one_rx_ring(vsi, false, q_idx, true);
	if (err)
		return err;

	ice_qvec_toggle_napi(vsi, q_vector, false);
	ice_qp_clean_rings(vsi, q_idx);
	ice_qp_reset_stats(vsi, q_idx);

	return 0;
}

/**
 * ice_qp_ena - Enables a queue pair
 * @vsi: VSI of interest
 * @q_idx: ring index in array
 *
 * Returns 0 on success, negative on failure.
 */
static int ice_qp_ena(struct ice_vsi *vsi, u16 q_idx)
{
	DEFINE_FLEX(struct ice_aqc_add_tx_qgrp, qg_buf, txqs, 1);
	u16 size = __struct_size(qg_buf);
	struct ice_q_vector *q_vector;
	struct ice_tx_ring *tx_ring;
	struct ice_rx_ring *rx_ring;
	int err;

	if (q_idx >= vsi->num_rxq || q_idx >= vsi->num_txq)
		return -EINVAL;

	qg_buf->num_txqs = 1;

	tx_ring = vsi->tx_rings[q_idx];
	rx_ring = vsi->rx_rings[q_idx];
	q_vector = rx_ring->q_vector;

	err = ice_vsi_cfg_txq(vsi, tx_ring, qg_buf);
	if (err)
		return err;

	if (ice_is_xdp_ena_vsi(vsi)) {
		struct ice_tx_ring *xdp_ring = vsi->xdp_rings[q_idx];

		memset(qg_buf, 0, size);
		qg_buf->num_txqs = 1;
		err = ice_vsi_cfg_txq(vsi, xdp_ring, qg_buf);
		if (err)
			return err;
		ice_set_ring_xdp(xdp_ring);
		ice_tx_xsk_pool(vsi, q_idx);
	}

	err = ice_vsi_cfg_rxq(rx_ring);
	if (err)
		return err;

	ice_qvec_cfg_msix(vsi, q_vector);

	err = ice_vsi_ctrl_one_rx_ring(vsi, true, q_idx, true);
	if (err)
		return err;

	clear_bit(ICE_CFG_BUSY, vsi->state);
	ice_qvec_toggle_napi(vsi, q_vector, true);
	ice_qvec_ena_irq(vsi, q_vector);

	netif_tx_start_queue(netdev_get_tx_queue(vsi->netdev, q_idx));

	return 0;
}

/**
 * ice_xsk_pool_disable - disable a buffer pool region
 * @vsi: Current VSI
 * @qid: queue ID
 *
 * Returns 0 on success, negative on failure
 */
static int ice_xsk_pool_disable(struct ice_vsi *vsi, u16 qid)
{
	struct xsk_buff_pool *pool = xsk_get_pool_from_qid(vsi->netdev, qid);

	if (!pool)
		return -EINVAL;

	clear_bit(qid, vsi->af_xdp_zc_qps);
	xsk_pool_dma_unmap(pool, ICE_RX_DMA_ATTR);

	return 0;
}

/**
 * ice_xsk_pool_enable - enable a buffer pool region
 * @vsi: Current VSI
 * @pool: pointer to a requested buffer pool region
 * @qid: queue ID
 *
 * Returns 0 on success, negative on failure
 */
static int
ice_xsk_pool_enable(struct ice_vsi *vsi, struct xsk_buff_pool *pool, u16 qid)
{
	int err;

	if (vsi->type != ICE_VSI_PF)
		return -EINVAL;

	if (qid >= vsi->netdev->real_num_rx_queues ||
	    qid >= vsi->netdev->real_num_tx_queues)
		return -EINVAL;

	err = xsk_pool_dma_map(pool, ice_pf_to_dev(vsi->back),
			       ICE_RX_DMA_ATTR);
	if (err)
		return err;

	set_bit(qid, vsi->af_xdp_zc_qps);

	return 0;
}

/**
 * ice_realloc_rx_xdp_bufs - reallocate for either XSK or normal buffer
 * @rx_ring: Rx ring
 * @pool_present: is pool for XSK present
 *
 * Try allocating memory and return ENOMEM, if failed to allocate.
 * If allocation was successful, substitute buffer with allocated one.
 * Returns 0 on success, negative on failure
 */
static int
ice_realloc_rx_xdp_bufs(struct ice_rx_ring *rx_ring, bool pool_present)
{
	size_t elem_size = pool_present ? sizeof(*rx_ring->xdp_buf) :
					  sizeof(*rx_ring->rx_buf);
	void *sw_ring = kcalloc(rx_ring->count, elem_size, GFP_KERNEL);

	if (!sw_ring)
		return -ENOMEM;

	if (pool_present) {
		kfree(rx_ring->rx_buf);
		rx_ring->rx_buf = NULL;
		rx_ring->xdp_buf = sw_ring;
	} else {
		kfree(rx_ring->xdp_buf);
		rx_ring->xdp_buf = NULL;
		rx_ring->rx_buf = sw_ring;
	}

	return 0;
}

/**
 * ice_realloc_zc_buf - reallocate XDP ZC queue pairs
 * @vsi: Current VSI
 * @zc: is zero copy set
 *
 * Reallocate buffer for rx_rings that might be used by XSK.
 * XDP requires more memory, than rx_buf provides.
 * Returns 0 on success, negative on failure
 */
int ice_realloc_zc_buf(struct ice_vsi *vsi, bool zc)
{
	struct ice_rx_ring *rx_ring;
	unsigned long q;

	for_each_set_bit(q, vsi->af_xdp_zc_qps,
			 max_t(int, vsi->alloc_txq, vsi->alloc_rxq)) {
		rx_ring = vsi->rx_rings[q];
		if (ice_realloc_rx_xdp_bufs(rx_ring, zc))
			return -ENOMEM;
	}

	return 0;
}

/**
 * ice_xsk_pool_setup - enable/disable a buffer pool region depending on its state
 * @vsi: Current VSI
 * @pool: buffer pool to enable/associate to a ring, NULL to disable
 * @qid: queue ID
 *
 * Returns 0 on success, negative on failure
 */
int ice_xsk_pool_setup(struct ice_vsi *vsi, struct xsk_buff_pool *pool, u16 qid)
{
	bool if_running, pool_present = !!pool;
	int ret = 0, pool_failure = 0;

	if (qid >= vsi->num_rxq || qid >= vsi->num_txq) {
		netdev_err(vsi->netdev, "Please use queue id in scope of combined queues count\n");
		pool_failure = -EINVAL;
		goto failure;
	}

	if_running = netif_running(vsi->netdev) && ice_is_xdp_ena_vsi(vsi);

	if (if_running) {
		struct ice_rx_ring *rx_ring = vsi->rx_rings[qid];

		ret = ice_qp_dis(vsi, qid);
		if (ret) {
			netdev_err(vsi->netdev, "ice_qp_dis error = %d\n", ret);
			goto xsk_pool_if_up;
		}

		ret = ice_realloc_rx_xdp_bufs(rx_ring, pool_present);
		if (ret)
			goto xsk_pool_if_up;
	}

	pool_failure = pool_present ? ice_xsk_pool_enable(vsi, pool, qid) :
				      ice_xsk_pool_disable(vsi, qid);

xsk_pool_if_up:
	if (if_running) {
		ret = ice_qp_ena(vsi, qid);
		if (!ret && pool_present)
			napi_schedule(&vsi->rx_rings[qid]->xdp_ring->q_vector->napi);
		else if (ret)
			netdev_err(vsi->netdev, "ice_qp_ena error = %d\n", ret);
	}

failure:
	if (pool_failure) {
		netdev_err(vsi->netdev, "Could not %sable buffer pool, error = %d\n",
			   pool_present ? "en" : "dis", pool_failure);
		return pool_failure;
	}

	return ret;
}

/**
 * ice_fill_rx_descs - pick buffers from XSK buffer pool and use it
 * @pool: XSK Buffer pool to pull the buffers from
 * @xdp: SW ring of xdp_buff that will hold the buffers
 * @rx_desc: Pointer to Rx descriptors that will be filled
 * @count: The number of buffers to allocate
 *
 * This function allocates a number of Rx buffers from the fill ring
 * or the internal recycle mechanism and places them on the Rx ring.
 *
 * Note that ring wrap should be handled by caller of this function.
 *
 * Returns the amount of allocated Rx descriptors
 */
static u16 ice_fill_rx_descs(struct xsk_buff_pool *pool, struct xdp_buff **xdp,
			     union ice_32b_rx_flex_desc *rx_desc, u16 count)
{
	dma_addr_t dma;
	u16 buffs;
	int i;

	buffs = xsk_buff_alloc_batch(pool, xdp, count);
	for (i = 0; i < buffs; i++) {
		dma = xsk_buff_xdp_get_dma(*xdp);
		rx_desc->read.pkt_addr = cpu_to_le64(dma);
		rx_desc->wb.status_error0 = 0;

		rx_desc++;
		xdp++;
	}

	return buffs;
}

/**
 * __ice_alloc_rx_bufs_zc - allocate a number of Rx buffers
 * @rx_ring: Rx ring
 * @count: The number of buffers to allocate
 *
 * Place the @count of descriptors onto Rx ring. Handle the ring wrap
 * for case where space from next_to_use up to the end of ring is less
 * than @count. Finally do a tail bump.
 *
 * Returns true if all allocations were successful, false if any fail.
 */
static bool __ice_alloc_rx_bufs_zc(struct ice_rx_ring *rx_ring, u16 count)
{
	u32 nb_buffs_extra = 0, nb_buffs = 0;
	union ice_32b_rx_flex_desc *rx_desc;
	u16 ntu = rx_ring->next_to_use;
	u16 total_count = count;
	struct xdp_buff **xdp;

	rx_desc = ICE_RX_DESC(rx_ring, ntu);
	xdp = ice_xdp_buf(rx_ring, ntu);

	if (ntu + count >= rx_ring->count) {
		nb_buffs_extra = ice_fill_rx_descs(rx_ring->xsk_pool, xdp,
						   rx_desc,
						   rx_ring->count - ntu);
		if (nb_buffs_extra != rx_ring->count - ntu) {
			ntu += nb_buffs_extra;
			goto exit;
		}
		rx_desc = ICE_RX_DESC(rx_ring, 0);
		xdp = ice_xdp_buf(rx_ring, 0);
		ntu = 0;
		count -= nb_buffs_extra;
		ice_release_rx_desc(rx_ring, 0);
	}

	nb_buffs = ice_fill_rx_descs(rx_ring->xsk_pool, xdp, rx_desc, count);

	ntu += nb_buffs;
	if (ntu == rx_ring->count)
		ntu = 0;

exit:
	if (rx_ring->next_to_use != ntu)
		ice_release_rx_desc(rx_ring, ntu);

	return total_count == (nb_buffs_extra + nb_buffs);
}

/**
 * ice_alloc_rx_bufs_zc - allocate a number of Rx buffers
 * @rx_ring: Rx ring
 * @count: The number of buffers to allocate
 *
 * Wrapper for internal allocation routine; figure out how many tail
 * bumps should take place based on the given threshold
 *
 * Returns true if all calls to internal alloc routine succeeded
 */
bool ice_alloc_rx_bufs_zc(struct ice_rx_ring *rx_ring, u16 count)
{
	u16 rx_thresh = ICE_RING_QUARTER(rx_ring);
	u16 leftover, i, tail_bumps;

	tail_bumps = count / rx_thresh;
	leftover = count - (tail_bumps * rx_thresh);

	for (i = 0; i < tail_bumps; i++)
		if (!__ice_alloc_rx_bufs_zc(rx_ring, rx_thresh))
			return false;
	return __ice_alloc_rx_bufs_zc(rx_ring, leftover);
}

/**
 * ice_construct_skb_zc - Create an sk_buff from zero-copy buffer
 * @rx_ring: Rx ring
 * @xdp: Pointer to XDP buffer
 *
 * This function allocates a new skb from a zero-copy Rx buffer.
 *
 * Returns the skb on success, NULL on failure.
 */
static struct sk_buff *
ice_construct_skb_zc(struct ice_rx_ring *rx_ring, struct xdp_buff *xdp)
{
	unsigned int totalsize = xdp->data_end - xdp->data_meta;
	unsigned int metasize = xdp->data - xdp->data_meta;
	struct skb_shared_info *sinfo = NULL;
	struct sk_buff *skb;
	u32 nr_frags = 0;

	if (unlikely(xdp_buff_has_frags(xdp))) {
		sinfo = xdp_get_shared_info_from_buff(xdp);
		nr_frags = sinfo->nr_frags;
	}
	net_prefetch(xdp->data_meta);

	skb = __napi_alloc_skb(&rx_ring->q_vector->napi, totalsize,
			       GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(!skb))
		return NULL;

	memcpy(__skb_put(skb, totalsize), xdp->data_meta,
	       ALIGN(totalsize, sizeof(long)));

	if (metasize) {
		skb_metadata_set(skb, metasize);
		__skb_pull(skb, metasize);
	}

	if (likely(!xdp_buff_has_frags(xdp)))
		goto out;

	for (int i = 0; i < nr_frags; i++) {
		struct skb_shared_info *skinfo = skb_shinfo(skb);
		skb_frag_t *frag = &sinfo->frags[i];
		struct page *page;
		void *addr;

		page = dev_alloc_page();
		if (!page) {
			dev_kfree_skb(skb);
			return NULL;
		}
		addr = page_to_virt(page);

		memcpy(addr, skb_frag_page(frag), skb_frag_size(frag));

		__skb_fill_page_desc_noacc(skinfo, skinfo->nr_frags++,
					   addr, 0, skb_frag_size(frag));
	}

out:
	xsk_buff_free(xdp);
	return skb;
}

/**
 * ice_clean_xdp_irq_zc - produce AF_XDP descriptors to CQ
 * @xdp_ring: XDP Tx ring
 */
static u32 ice_clean_xdp_irq_zc(struct ice_tx_ring *xdp_ring)
{
	u16 ntc = xdp_ring->next_to_clean;
	struct ice_tx_desc *tx_desc;
	u16 cnt = xdp_ring->count;
	struct ice_tx_buf *tx_buf;
	u16 completed_frames = 0;
	u16 xsk_frames = 0;
	u16 last_rs;
	int i;

	last_rs = xdp_ring->next_to_use ? xdp_ring->next_to_use - 1 : cnt - 1;
	tx_desc = ICE_TX_DESC(xdp_ring, last_rs);
	if (tx_desc->cmd_type_offset_bsz &
	    cpu_to_le64(ICE_TX_DESC_DTYPE_DESC_DONE)) {
		if (last_rs >= ntc)
			completed_frames = last_rs - ntc + 1;
		else
			completed_frames = last_rs + cnt - ntc + 1;
	}

	if (!completed_frames)
		return 0;

	if (likely(!xdp_ring->xdp_tx_active)) {
		xsk_frames = completed_frames;
		goto skip;
	}

	ntc = xdp_ring->next_to_clean;
	for (i = 0; i < completed_frames; i++) {
		tx_buf = &xdp_ring->tx_buf[ntc];

		if (tx_buf->type == ICE_TX_BUF_XSK_TX) {
			tx_buf->type = ICE_TX_BUF_EMPTY;
			xsk_buff_free(tx_buf->xdp);
			xdp_ring->xdp_tx_active--;
		} else {
			xsk_frames++;
		}

		ntc++;
		if (ntc >= xdp_ring->count)
			ntc = 0;
	}
skip:
	tx_desc->cmd_type_offset_bsz = 0;
	xdp_ring->next_to_clean += completed_frames;
	if (xdp_ring->next_to_clean >= cnt)
		xdp_ring->next_to_clean -= cnt;
	if (xsk_frames)
		xsk_tx_completed(xdp_ring->xsk_pool, xsk_frames);

	return completed_frames;
}

/**
 * ice_xmit_xdp_tx_zc - AF_XDP ZC handler for XDP_TX
 * @xdp: XDP buffer to xmit
 * @xdp_ring: XDP ring to produce descriptor onto
 *
 * note that this function works directly on xdp_buff, no need to convert
 * it to xdp_frame. xdp_buff pointer is stored to ice_tx_buf so that cleaning
 * side will be able to xsk_buff_free() it.
 *
 * Returns ICE_XDP_TX for successfully produced desc, ICE_XDP_CONSUMED if there
 * was not enough space on XDP ring
 */
static int ice_xmit_xdp_tx_zc(struct xdp_buff *xdp,
			      struct ice_tx_ring *xdp_ring)
{
	struct skb_shared_info *sinfo = NULL;
	u32 size = xdp->data_end - xdp->data;
	u32 ntu = xdp_ring->next_to_use;
	struct ice_tx_desc *tx_desc;
	struct ice_tx_buf *tx_buf;
	struct xdp_buff *head;
	u32 nr_frags = 0;
	u32 free_space;
	u32 frag = 0;

	free_space = ICE_DESC_UNUSED(xdp_ring);
	if (free_space < ICE_RING_QUARTER(xdp_ring))
		free_space += ice_clean_xdp_irq_zc(xdp_ring);

	if (unlikely(!free_space))
		goto busy;

	if (unlikely(xdp_buff_has_frags(xdp))) {
		sinfo = xdp_get_shared_info_from_buff(xdp);
		nr_frags = sinfo->nr_frags;
		if (free_space < nr_frags + 1)
			goto busy;
	}

	tx_desc = ICE_TX_DESC(xdp_ring, ntu);
	tx_buf = &xdp_ring->tx_buf[ntu];
	head = xdp;

	for (;;) {
		dma_addr_t dma;

		dma = xsk_buff_xdp_get_dma(xdp);
		xsk_buff_raw_dma_sync_for_device(xdp_ring->xsk_pool, dma, size);

		tx_buf->xdp = xdp;
		tx_buf->type = ICE_TX_BUF_XSK_TX;
		tx_desc->buf_addr = cpu_to_le64(dma);
		tx_desc->cmd_type_offset_bsz = ice_build_ctob(0, 0, size, 0);
		/* account for each xdp_buff from xsk_buff_pool */
		xdp_ring->xdp_tx_active++;

		if (++ntu == xdp_ring->count)
			ntu = 0;

		if (frag == nr_frags)
			break;

		tx_desc = ICE_TX_DESC(xdp_ring, ntu);
		tx_buf = &xdp_ring->tx_buf[ntu];

		xdp = xsk_buff_get_frag(head);
		size = skb_frag_size(&sinfo->frags[frag]);
		frag++;
	}

	xdp_ring->next_to_use = ntu;
	/* update last descriptor from a frame with EOP */
	tx_desc->cmd_type_offset_bsz |=
		cpu_to_le64(ICE_TX_DESC_CMD_EOP << ICE_TXD_QW1_CMD_S);

	return ICE_XDP_TX;

busy:
	xdp_ring->ring_stats->tx_stats.tx_busy++;

	return ICE_XDP_CONSUMED;
}

/**
 * ice_run_xdp_zc - Executes an XDP program in zero-copy path
 * @rx_ring: Rx ring
 * @xdp: xdp_buff used as input to the XDP program
 * @xdp_prog: XDP program to run
 * @xdp_ring: ring to be used for XDP_TX action
 *
 * Returns any of ICE_XDP_{PASS, CONSUMED, TX, REDIR}
 */
static int
ice_run_xdp_zc(struct ice_rx_ring *rx_ring, struct xdp_buff *xdp,
	       struct bpf_prog *xdp_prog, struct ice_tx_ring *xdp_ring)
{
	int err, result = ICE_XDP_PASS;
	u32 act;

	act = bpf_prog_run_xdp(xdp_prog, xdp);

	if (likely(act == XDP_REDIRECT)) {
		err = xdp_do_redirect(rx_ring->netdev, xdp, xdp_prog);
		if (!err)
			return ICE_XDP_REDIR;
		if (xsk_uses_need_wakeup(rx_ring->xsk_pool) && err == -ENOBUFS)
			result = ICE_XDP_EXIT;
		else
			result = ICE_XDP_CONSUMED;
		goto out_failure;
	}

	switch (act) {
	case XDP_PASS:
		break;
	case XDP_TX:
		result = ice_xmit_xdp_tx_zc(xdp, xdp_ring);
		if (result == ICE_XDP_CONSUMED)
			goto out_failure;
		break;
	case XDP_DROP:
		result = ICE_XDP_CONSUMED;
		break;
	default:
		bpf_warn_invalid_xdp_action(rx_ring->netdev, xdp_prog, act);
		fallthrough;
	case XDP_ABORTED:
		result = ICE_XDP_CONSUMED;
out_failure:
		trace_xdp_exception(rx_ring->netdev, xdp_prog, act);
		break;
	}

	return result;
}

static int
ice_add_xsk_frag(struct ice_rx_ring *rx_ring, struct xdp_buff *first,
		 struct xdp_buff *xdp, const unsigned int size)
{
	struct skb_shared_info *sinfo = xdp_get_shared_info_from_buff(first);

	if (!size)
		return 0;

	if (!xdp_buff_has_frags(first)) {
		sinfo->nr_frags = 0;
		sinfo->xdp_frags_size = 0;
		xdp_buff_set_frags_flag(first);
	}

	if (unlikely(sinfo->nr_frags == MAX_SKB_FRAGS)) {
		xsk_buff_free(first);
		return -ENOMEM;
	}

	__skb_fill_page_desc_noacc(sinfo, sinfo->nr_frags++,
				   virt_to_page(xdp->data_hard_start), 0, size);
	sinfo->xdp_frags_size += size;
	xsk_buff_add_frag(xdp);

	return 0;
}

/**
 * ice_clean_rx_irq_zc - consumes packets from the hardware ring
 * @rx_ring: AF_XDP Rx ring
 * @budget: NAPI budget
 *
 * Returns number of processed packets on success, remaining budget on failure.
 */
int ice_clean_rx_irq_zc(struct ice_rx_ring *rx_ring, int budget)
{
	unsigned int total_rx_bytes = 0, total_rx_packets = 0;
	struct xsk_buff_pool *xsk_pool = rx_ring->xsk_pool;
	u32 ntc = rx_ring->next_to_clean;
	u32 ntu = rx_ring->next_to_use;
	struct xdp_buff *first = NULL;
	struct ice_tx_ring *xdp_ring;
	unsigned int xdp_xmit = 0;
	struct bpf_prog *xdp_prog;
	u32 cnt = rx_ring->count;
	bool failure = false;
	int entries_to_alloc;

	/* ZC patch is enabled only when XDP program is set,
	 * so here it can not be NULL
	 */
	xdp_prog = READ_ONCE(rx_ring->xdp_prog);
	xdp_ring = rx_ring->xdp_ring;

	if (ntc != rx_ring->first_desc)
		first = *ice_xdp_buf(rx_ring, rx_ring->first_desc);

	while (likely(total_rx_packets < (unsigned int)budget)) {
		union ice_32b_rx_flex_desc *rx_desc;
		unsigned int size, xdp_res = 0;
		struct xdp_buff *xdp;
		struct sk_buff *skb;
		u16 stat_err_bits;
		u16 vlan_tag = 0;
		u16 rx_ptype;

		rx_desc = ICE_RX_DESC(rx_ring, ntc);

		stat_err_bits = BIT(ICE_RX_FLEX_DESC_STATUS0_DD_S);
		if (!ice_test_staterr(rx_desc->wb.status_error0, stat_err_bits))
			break;

		/* This memory barrier is needed to keep us from reading
		 * any other fields out of the rx_desc until we have
		 * verified the descriptor has been written back.
		 */
		dma_rmb();

		if (unlikely(ntc == ntu))
			break;

		xdp = *ice_xdp_buf(rx_ring, ntc);

		size = le16_to_cpu(rx_desc->wb.pkt_len) &
				   ICE_RX_FLX_DESC_PKT_LEN_M;

		xsk_buff_set_size(xdp, size);
		xsk_buff_dma_sync_for_cpu(xdp, xsk_pool);

		if (!first) {
			first = xdp;
			xdp_buff_clear_frags_flag(first);
		} else if (ice_add_xsk_frag(rx_ring, first, xdp, size)) {
			break;
		}

		if (++ntc == cnt)
			ntc = 0;

		if (ice_is_non_eop(rx_ring, rx_desc))
			continue;

		xdp_res = ice_run_xdp_zc(rx_ring, first, xdp_prog, xdp_ring);
		if (likely(xdp_res & (ICE_XDP_TX | ICE_XDP_REDIR))) {
			xdp_xmit |= xdp_res;
		} else if (xdp_res == ICE_XDP_EXIT) {
			failure = true;
			first = NULL;
			rx_ring->first_desc = ntc;
			break;
		} else if (xdp_res == ICE_XDP_CONSUMED) {
			xsk_buff_free(first);
		} else if (xdp_res == ICE_XDP_PASS) {
			goto construct_skb;
		}

		total_rx_bytes += xdp_get_buff_len(first);
		total_rx_packets++;

		first = NULL;
		rx_ring->first_desc = ntc;
		continue;

construct_skb:
		/* XDP_PASS path */
		skb = ice_construct_skb_zc(rx_ring, first);
		if (!skb) {
			rx_ring->ring_stats->rx_stats.alloc_buf_failed++;
			break;
		}

		first = NULL;
		rx_ring->first_desc = ntc;

		if (eth_skb_pad(skb)) {
			skb = NULL;
			continue;
		}

		total_rx_bytes += skb->len;
		total_rx_packets++;

		vlan_tag = ice_get_vlan_tag_from_rx_desc(rx_desc);

		rx_ptype = le16_to_cpu(rx_desc->wb.ptype_flex_flags0) &
				       ICE_RX_FLEX_DESC_PTYPE_M;

		ice_process_skb_fields(rx_ring, rx_desc, skb, rx_ptype);
		ice_receive_skb(rx_ring, skb, vlan_tag);
	}

	rx_ring->next_to_clean = ntc;
	entries_to_alloc = ICE_RX_DESC_UNUSED(rx_ring);
	if (entries_to_alloc > ICE_RING_QUARTER(rx_ring))
		failure |= !ice_alloc_rx_bufs_zc(rx_ring, entries_to_alloc);

	ice_finalize_xdp_rx(xdp_ring, xdp_xmit, 0);
	ice_update_rx_ring_stats(rx_ring, total_rx_packets, total_rx_bytes);

	if (xsk_uses_need_wakeup(xsk_pool)) {
		/* ntu could have changed when allocating entries above, so
		 * use rx_ring value instead of stack based one
		 */
		if (failure || ntc == rx_ring->next_to_use)
			xsk_set_rx_need_wakeup(xsk_pool);
		else
			xsk_clear_rx_need_wakeup(xsk_pool);

		return (int)total_rx_packets;
	}

	return failure ? budget : (int)total_rx_packets;
}

/**
 * ice_xmit_pkt - produce a single HW Tx descriptor out of AF_XDP descriptor
 * @xdp_ring: XDP ring to produce the HW Tx descriptor on
 * @desc: AF_XDP descriptor to pull the DMA address and length from
 * @total_bytes: bytes accumulator that will be used for stats update
 */
static void ice_xmit_pkt(struct ice_tx_ring *xdp_ring, struct xdp_desc *desc,
			 unsigned int *total_bytes)
{
	struct ice_tx_desc *tx_desc;
	dma_addr_t dma;

	dma = xsk_buff_raw_get_dma(xdp_ring->xsk_pool, desc->addr);
	xsk_buff_raw_dma_sync_for_device(xdp_ring->xsk_pool, dma, desc->len);

	tx_desc = ICE_TX_DESC(xdp_ring, xdp_ring->next_to_use++);
	tx_desc->buf_addr = cpu_to_le64(dma);
	tx_desc->cmd_type_offset_bsz = ice_build_ctob(xsk_is_eop_desc(desc),
						      0, desc->len, 0);

	*total_bytes += desc->len;
}

/**
 * ice_xmit_pkt_batch - produce a batch of HW Tx descriptors out of AF_XDP descriptors
 * @xdp_ring: XDP ring to produce the HW Tx descriptors on
 * @descs: AF_XDP descriptors to pull the DMA addresses and lengths from
 * @total_bytes: bytes accumulator that will be used for stats update
 */
static void ice_xmit_pkt_batch(struct ice_tx_ring *xdp_ring, struct xdp_desc *descs,
			       unsigned int *total_bytes)
{
	u16 ntu = xdp_ring->next_to_use;
	struct ice_tx_desc *tx_desc;
	u32 i;

	loop_unrolled_for(i = 0; i < PKTS_PER_BATCH; i++) {
		dma_addr_t dma;

		dma = xsk_buff_raw_get_dma(xdp_ring->xsk_pool, descs[i].addr);
		xsk_buff_raw_dma_sync_for_device(xdp_ring->xsk_pool, dma, descs[i].len);

		tx_desc = ICE_TX_DESC(xdp_ring, ntu++);
		tx_desc->buf_addr = cpu_to_le64(dma);
		tx_desc->cmd_type_offset_bsz = ice_build_ctob(xsk_is_eop_desc(&descs[i]),
							      0, descs[i].len, 0);

		*total_bytes += descs[i].len;
	}

	xdp_ring->next_to_use = ntu;
}

/**
 * ice_fill_tx_hw_ring - produce the number of Tx descriptors onto ring
 * @xdp_ring: XDP ring to produce the HW Tx descriptors on
 * @descs: AF_XDP descriptors to pull the DMA addresses and lengths from
 * @nb_pkts: count of packets to be send
 * @total_bytes: bytes accumulator that will be used for stats update
 */
static void ice_fill_tx_hw_ring(struct ice_tx_ring *xdp_ring, struct xdp_desc *descs,
				u32 nb_pkts, unsigned int *total_bytes)
{
	u32 batched, leftover, i;

	batched = ALIGN_DOWN(nb_pkts, PKTS_PER_BATCH);
	leftover = nb_pkts & (PKTS_PER_BATCH - 1);
	for (i = 0; i < batched; i += PKTS_PER_BATCH)
		ice_xmit_pkt_batch(xdp_ring, &descs[i], total_bytes);
	for (; i < batched + leftover; i++)
		ice_xmit_pkt(xdp_ring, &descs[i], total_bytes);
}

/**
 * ice_xmit_zc - take entries from XSK Tx ring and place them onto HW Tx ring
 * @xdp_ring: XDP ring to produce the HW Tx descriptors on
 *
 * Returns true if there is no more work that needs to be done, false otherwise
 */
bool ice_xmit_zc(struct ice_tx_ring *xdp_ring)
{
	struct xdp_desc *descs = xdp_ring->xsk_pool->tx_descs;
	u32 nb_pkts, nb_processed = 0;
	unsigned int total_bytes = 0;
	int budget;

	ice_clean_xdp_irq_zc(xdp_ring);

	budget = ICE_DESC_UNUSED(xdp_ring);
	budget = min_t(u16, budget, ICE_RING_QUARTER(xdp_ring));

	nb_pkts = xsk_tx_peek_release_desc_batch(xdp_ring->xsk_pool, budget);
	if (!nb_pkts)
		return true;

	if (xdp_ring->next_to_use + nb_pkts >= xdp_ring->count) {
		nb_processed = xdp_ring->count - xdp_ring->next_to_use;
		ice_fill_tx_hw_ring(xdp_ring, descs, nb_processed, &total_bytes);
		xdp_ring->next_to_use = 0;
	}

	ice_fill_tx_hw_ring(xdp_ring, &descs[nb_processed], nb_pkts - nb_processed,
			    &total_bytes);

	ice_set_rs_bit(xdp_ring);
	ice_xdp_ring_update_tail(xdp_ring);
	ice_update_tx_ring_stats(xdp_ring, nb_pkts, total_bytes);

	if (xsk_uses_need_wakeup(xdp_ring->xsk_pool))
		xsk_set_tx_need_wakeup(xdp_ring->xsk_pool);

	return nb_pkts < budget;
}

/**
 * ice_xsk_wakeup - Implements ndo_xsk_wakeup
 * @netdev: net_device
 * @queue_id: queue to wake up
 * @flags: ignored in our case, since we have Rx and Tx in the same NAPI
 *
 * Returns negative on error, zero otherwise.
 */
int
ice_xsk_wakeup(struct net_device *netdev, u32 queue_id,
	       u32 __always_unused flags)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_q_vector *q_vector;
	struct ice_vsi *vsi = np->vsi;
	struct ice_tx_ring *ring;

	if (test_bit(ICE_VSI_DOWN, vsi->state))
		return -ENETDOWN;

	if (!ice_is_xdp_ena_vsi(vsi))
		return -EINVAL;

	if (queue_id >= vsi->num_txq || queue_id >= vsi->num_rxq)
		return -EINVAL;

	ring = vsi->rx_rings[queue_id]->xdp_ring;

	if (!ring->xsk_pool)
		return -EINVAL;

	/* The idea here is that if NAPI is running, mark a miss, so
	 * it will run again. If not, trigger an interrupt and
	 * schedule the NAPI from interrupt context. If NAPI would be
	 * scheduled here, the interrupt affinity would not be
	 * honored.
	 */
	q_vector = ring->q_vector;
	if (!napi_if_scheduled_mark_missed(&q_vector->napi))
		ice_trigger_sw_intr(&vsi->back->hw, q_vector);

	return 0;
}

/**
 * ice_xsk_any_rx_ring_ena - Checks if Rx rings have AF_XDP buff pool attached
 * @vsi: VSI to be checked
 *
 * Returns true if any of the Rx rings has an AF_XDP buff pool attached
 */
bool ice_xsk_any_rx_ring_ena(struct ice_vsi *vsi)
{
	int i;

	ice_for_each_rxq(vsi, i) {
		if (xsk_get_pool_from_qid(vsi->netdev, i))
			return true;
	}

	return false;
}

/**
 * ice_xsk_clean_rx_ring - clean buffer pool queues connected to a given Rx ring
 * @rx_ring: ring to be cleaned
 */
void ice_xsk_clean_rx_ring(struct ice_rx_ring *rx_ring)
{
	u16 ntc = rx_ring->next_to_clean;
	u16 ntu = rx_ring->next_to_use;

	while (ntc != ntu) {
		struct xdp_buff *xdp = *ice_xdp_buf(rx_ring, ntc);

		xsk_buff_free(xdp);
		ntc++;
		if (ntc >= rx_ring->count)
			ntc = 0;
	}
}

/**
 * ice_xsk_clean_xdp_ring - Clean the XDP Tx ring and its buffer pool queues
 * @xdp_ring: XDP_Tx ring
 */
void ice_xsk_clean_xdp_ring(struct ice_tx_ring *xdp_ring)
{
	u16 ntc = xdp_ring->next_to_clean, ntu = xdp_ring->next_to_use;
	u32 xsk_frames = 0;

	while (ntc != ntu) {
		struct ice_tx_buf *tx_buf = &xdp_ring->tx_buf[ntc];

		if (tx_buf->type == ICE_TX_BUF_XSK_TX) {
			tx_buf->type = ICE_TX_BUF_EMPTY;
			xsk_buff_free(tx_buf->xdp);
		} else {
			xsk_frames++;
		}

		ntc++;
		if (ntc >= xdp_ring->count)
			ntc = 0;
	}

	if (xsk_frames)
		xsk_tx_completed(xdp_ring->xsk_pool, xsk_frames);
}
