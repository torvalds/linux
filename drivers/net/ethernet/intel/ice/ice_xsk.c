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
	memset(&vsi->rx_rings[q_idx]->rx_stats, 0,
	       sizeof(vsi->rx_rings[q_idx]->rx_stats));
	memset(&vsi->tx_rings[q_idx]->stats, 0,
	       sizeof(vsi->tx_rings[q_idx]->stats));
	if (ice_is_xdp_ena_vsi(vsi))
		memset(&vsi->xdp_rings[q_idx]->stats, 0,
		       sizeof(vsi->xdp_rings[q_idx]->stats));
}

/**
 * ice_qp_clean_rings - Cleans all the rings of a given index
 * @vsi: VSI that contains rings of interest
 * @q_idx: ring index in array
 */
static void ice_qp_clean_rings(struct ice_vsi *vsi, u16 q_idx)
{
	ice_clean_tx_ring(vsi->tx_rings[q_idx]);
	if (ice_is_xdp_ena_vsi(vsi))
		ice_clean_tx_ring(vsi->xdp_rings[q_idx]);
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
	int base = vsi->base_vector;
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
		u16 v_idx = q_vector->v_idx;

		wr32(hw, GLINT_DYN_CTL(q_vector->reg_idx), 0);
		ice_flush(hw);
		synchronize_irq(pf->msix_entries[v_idx + base].vector);
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

	ice_qvec_dis_irq(vsi, rx_ring, q_vector);

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
	struct ice_aqc_add_tx_qgrp *qg_buf;
	struct ice_q_vector *q_vector;
	struct ice_tx_ring *tx_ring;
	struct ice_rx_ring *rx_ring;
	u16 size;
	int err;

	if (q_idx >= vsi->num_rxq || q_idx >= vsi->num_txq)
		return -EINVAL;

	size = struct_size(qg_buf, txqs, 1);
	qg_buf = kzalloc(size, GFP_KERNEL);
	if (!qg_buf)
		return -ENOMEM;

	qg_buf->num_txqs = 1;

	tx_ring = vsi->tx_rings[q_idx];
	rx_ring = vsi->rx_rings[q_idx];
	q_vector = rx_ring->q_vector;

	err = ice_vsi_cfg_txq(vsi, tx_ring, qg_buf);
	if (err)
		goto free_buf;

	if (ice_is_xdp_ena_vsi(vsi)) {
		struct ice_tx_ring *xdp_ring = vsi->xdp_rings[q_idx];

		memset(qg_buf, 0, size);
		qg_buf->num_txqs = 1;
		err = ice_vsi_cfg_txq(vsi, xdp_ring, qg_buf);
		if (err)
			goto free_buf;
		ice_set_ring_xdp(xdp_ring);
		xdp_ring->xsk_pool = ice_tx_xsk_pool(xdp_ring);
	}

	err = ice_vsi_cfg_rxq(rx_ring);
	if (err)
		goto free_buf;

	ice_qvec_cfg_msix(vsi, q_vector);

	err = ice_vsi_ctrl_one_rx_ring(vsi, true, q_idx, true);
	if (err)
		goto free_buf;

	clear_bit(ICE_CFG_BUSY, vsi->state);
	ice_qvec_toggle_napi(vsi, q_vector, true);
	ice_qvec_ena_irq(vsi, q_vector);

	netif_tx_start_queue(netdev_get_tx_queue(vsi->netdev, q_idx));
free_buf:
	kfree(qg_buf);
	return err;
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

	if (!is_power_of_2(vsi->rx_rings[qid]->count) ||
	    !is_power_of_2(vsi->tx_rings[qid]->count)) {
		netdev_err(vsi->netdev, "Please align ring sizes to power of 2\n");
		pool_failure = -EINVAL;
		goto failure;
	}

	if_running = netif_running(vsi->netdev) && ice_is_xdp_ena_vsi(vsi);

	if (if_running) {
		ret = ice_qp_dis(vsi, qid);
		if (ret) {
			netdev_err(vsi->netdev, "ice_qp_dis error = %d\n", ret);
			goto xsk_pool_if_up;
		}
	}

	pool_failure = pool_present ? ice_xsk_pool_enable(vsi, pool, qid) :
				      ice_xsk_pool_disable(vsi, qid);

xsk_pool_if_up:
	if (if_running) {
		ret = ice_qp_ena(vsi, qid);
		if (!ret && pool_present)
			napi_schedule(&vsi->xdp_rings[qid]->q_vector->napi);
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
	union ice_32b_rx_flex_desc *rx_desc;
	u32 nb_buffs_extra = 0, nb_buffs;
	u16 ntu = rx_ring->next_to_use;
	u16 total_count = count;
	struct xdp_buff **xdp;

	rx_desc = ICE_RX_DESC(rx_ring, ntu);
	xdp = ice_xdp_buf(rx_ring, ntu);

	if (ntu + count >= rx_ring->count) {
		nb_buffs_extra = ice_fill_rx_descs(rx_ring->xsk_pool, xdp,
						   rx_desc,
						   rx_ring->count - ntu);
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
	u16 batched, leftover, i, tail_bumps;

	batched = ALIGN_DOWN(count, rx_thresh);
	tail_bumps = batched / rx_thresh;
	leftover = count & (rx_thresh - 1);

	for (i = 0; i < tail_bumps; i++)
		if (!__ice_alloc_rx_bufs_zc(rx_ring, rx_thresh))
			return false;
	return __ice_alloc_rx_bufs_zc(rx_ring, leftover);
}

/**
 * ice_bump_ntc - Bump the next_to_clean counter of an Rx ring
 * @rx_ring: Rx ring
 */
static void ice_bump_ntc(struct ice_rx_ring *rx_ring)
{
	int ntc = rx_ring->next_to_clean + 1;

	ntc = (ntc < rx_ring->count) ? ntc : 0;
	rx_ring->next_to_clean = ntc;
	prefetch(ICE_RX_DESC(rx_ring, ntc));
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
	struct sk_buff *skb;

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

	xsk_buff_free(xdp);
	return skb;
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
		if (err)
			goto out_failure;
		return ICE_XDP_REDIR;
	}

	switch (act) {
	case XDP_PASS:
		break;
	case XDP_TX:
		result = ice_xmit_xdp_buff(xdp, xdp_ring);
		if (result == ICE_XDP_CONSUMED)
			goto out_failure;
		break;
	default:
		bpf_warn_invalid_xdp_action(rx_ring->netdev, xdp_prog, act);
		fallthrough;
	case XDP_ABORTED:
out_failure:
		trace_xdp_exception(rx_ring->netdev, xdp_prog, act);
		fallthrough;
	case XDP_DROP:
		result = ICE_XDP_CONSUMED;
		break;
	}

	return result;
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
	struct ice_tx_ring *xdp_ring;
	unsigned int xdp_xmit = 0;
	struct bpf_prog *xdp_prog;
	bool failure = false;

	/* ZC patch is enabled only when XDP program is set,
	 * so here it can not be NULL
	 */
	xdp_prog = READ_ONCE(rx_ring->xdp_prog);
	xdp_ring = rx_ring->xdp_ring;

	while (likely(total_rx_packets < (unsigned int)budget)) {
		union ice_32b_rx_flex_desc *rx_desc;
		unsigned int size, xdp_res = 0;
		struct xdp_buff *xdp;
		struct sk_buff *skb;
		u16 stat_err_bits;
		u16 vlan_tag = 0;
		u16 rx_ptype;

		rx_desc = ICE_RX_DESC(rx_ring, rx_ring->next_to_clean);

		stat_err_bits = BIT(ICE_RX_FLEX_DESC_STATUS0_DD_S);
		if (!ice_test_staterr(rx_desc->wb.status_error0, stat_err_bits))
			break;

		/* This memory barrier is needed to keep us from reading
		 * any other fields out of the rx_desc until we have
		 * verified the descriptor has been written back.
		 */
		dma_rmb();

		xdp = *ice_xdp_buf(rx_ring, rx_ring->next_to_clean);

		size = le16_to_cpu(rx_desc->wb.pkt_len) &
				   ICE_RX_FLX_DESC_PKT_LEN_M;
		if (!size) {
			xdp->data = NULL;
			xdp->data_end = NULL;
			xdp->data_hard_start = NULL;
			xdp->data_meta = NULL;
			goto construct_skb;
		}

		xsk_buff_set_size(xdp, size);
		xsk_buff_dma_sync_for_cpu(xdp, rx_ring->xsk_pool);

		xdp_res = ice_run_xdp_zc(rx_ring, xdp, xdp_prog, xdp_ring);
		if (xdp_res) {
			if (xdp_res & (ICE_XDP_TX | ICE_XDP_REDIR))
				xdp_xmit |= xdp_res;
			else
				xsk_buff_free(xdp);

			total_rx_bytes += size;
			total_rx_packets++;

			ice_bump_ntc(rx_ring);
			continue;
		}
construct_skb:
		/* XDP_PASS path */
		skb = ice_construct_skb_zc(rx_ring, xdp);
		if (!skb) {
			rx_ring->rx_stats.alloc_buf_failed++;
			break;
		}

		ice_bump_ntc(rx_ring);

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

	failure = !ice_alloc_rx_bufs_zc(rx_ring, ICE_DESC_UNUSED(rx_ring));

	ice_finalize_xdp_rx(xdp_ring, xdp_xmit);
	ice_update_rx_ring_stats(rx_ring, total_rx_packets, total_rx_bytes);

	if (xsk_uses_need_wakeup(rx_ring->xsk_pool)) {
		if (failure || rx_ring->next_to_clean == rx_ring->next_to_use)
			xsk_set_rx_need_wakeup(rx_ring->xsk_pool);
		else
			xsk_clear_rx_need_wakeup(rx_ring->xsk_pool);

		return (int)total_rx_packets;
	}

	return failure ? budget : (int)total_rx_packets;
}

/**
 * ice_clean_xdp_tx_buf - Free and unmap XDP Tx buffer
 * @xdp_ring: XDP Tx ring
 * @tx_buf: Tx buffer to clean
 */
static void
ice_clean_xdp_tx_buf(struct ice_tx_ring *xdp_ring, struct ice_tx_buf *tx_buf)
{
	xdp_return_frame((struct xdp_frame *)tx_buf->raw_buf);
	xdp_ring->xdp_tx_active--;
	dma_unmap_single(xdp_ring->dev, dma_unmap_addr(tx_buf, dma),
			 dma_unmap_len(tx_buf, len), DMA_TO_DEVICE);
	dma_unmap_len_set(tx_buf, len, 0);
}

/**
 * ice_clean_xdp_irq_zc - Reclaim resources after transmit completes on XDP ring
 * @xdp_ring: XDP ring to clean
 * @napi_budget: amount of descriptors that NAPI allows us to clean
 *
 * Returns count of cleaned descriptors
 */
static u16 ice_clean_xdp_irq_zc(struct ice_tx_ring *xdp_ring, int napi_budget)
{
	u16 tx_thresh = ICE_RING_QUARTER(xdp_ring);
	int budget = napi_budget / tx_thresh;
	u16 next_dd = xdp_ring->next_dd;
	u16 ntc, cleared_dds = 0;

	do {
		struct ice_tx_desc *next_dd_desc;
		u16 desc_cnt = xdp_ring->count;
		struct ice_tx_buf *tx_buf;
		u32 xsk_frames;
		u16 i;

		next_dd_desc = ICE_TX_DESC(xdp_ring, next_dd);
		if (!(next_dd_desc->cmd_type_offset_bsz &
		    cpu_to_le64(ICE_TX_DESC_DTYPE_DESC_DONE)))
			break;

		cleared_dds++;
		xsk_frames = 0;
		if (likely(!xdp_ring->xdp_tx_active)) {
			xsk_frames = tx_thresh;
			goto skip;
		}

		ntc = xdp_ring->next_to_clean;

		for (i = 0; i < tx_thresh; i++) {
			tx_buf = &xdp_ring->tx_buf[ntc];

			if (tx_buf->raw_buf) {
				ice_clean_xdp_tx_buf(xdp_ring, tx_buf);
				tx_buf->raw_buf = NULL;
			} else {
				xsk_frames++;
			}

			ntc++;
			if (ntc >= xdp_ring->count)
				ntc = 0;
		}
skip:
		xdp_ring->next_to_clean += tx_thresh;
		if (xdp_ring->next_to_clean >= desc_cnt)
			xdp_ring->next_to_clean -= desc_cnt;
		if (xsk_frames)
			xsk_tx_completed(xdp_ring->xsk_pool, xsk_frames);
		next_dd_desc->cmd_type_offset_bsz = 0;
		next_dd = next_dd + tx_thresh;
		if (next_dd >= desc_cnt)
			next_dd = tx_thresh - 1;
	} while (budget--);

	xdp_ring->next_dd = next_dd;

	return cleared_dds * tx_thresh;
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
	tx_desc->cmd_type_offset_bsz = ice_build_ctob(ICE_TX_DESC_CMD_EOP,
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
	u16 tx_thresh = ICE_RING_QUARTER(xdp_ring);
	u16 ntu = xdp_ring->next_to_use;
	struct ice_tx_desc *tx_desc;
	u32 i;

	loop_unrolled_for(i = 0; i < PKTS_PER_BATCH; i++) {
		dma_addr_t dma;

		dma = xsk_buff_raw_get_dma(xdp_ring->xsk_pool, descs[i].addr);
		xsk_buff_raw_dma_sync_for_device(xdp_ring->xsk_pool, dma, descs[i].len);

		tx_desc = ICE_TX_DESC(xdp_ring, ntu++);
		tx_desc->buf_addr = cpu_to_le64(dma);
		tx_desc->cmd_type_offset_bsz = ice_build_ctob(ICE_TX_DESC_CMD_EOP,
							      0, descs[i].len, 0);

		*total_bytes += descs[i].len;
	}

	xdp_ring->next_to_use = ntu;

	if (xdp_ring->next_to_use > xdp_ring->next_rs) {
		tx_desc = ICE_TX_DESC(xdp_ring, xdp_ring->next_rs);
		tx_desc->cmd_type_offset_bsz |=
			cpu_to_le64(ICE_TX_DESC_CMD_RS << ICE_TXD_QW1_CMD_S);
		xdp_ring->next_rs += tx_thresh;
	}
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
	u16 tx_thresh = ICE_RING_QUARTER(xdp_ring);
	u32 batched, leftover, i;

	batched = ALIGN_DOWN(nb_pkts, PKTS_PER_BATCH);
	leftover = nb_pkts & (PKTS_PER_BATCH - 1);
	for (i = 0; i < batched; i += PKTS_PER_BATCH)
		ice_xmit_pkt_batch(xdp_ring, &descs[i], total_bytes);
	for (; i < batched + leftover; i++)
		ice_xmit_pkt(xdp_ring, &descs[i], total_bytes);

	if (xdp_ring->next_to_use > xdp_ring->next_rs) {
		struct ice_tx_desc *tx_desc;

		tx_desc = ICE_TX_DESC(xdp_ring, xdp_ring->next_rs);
		tx_desc->cmd_type_offset_bsz |=
			cpu_to_le64(ICE_TX_DESC_CMD_RS << ICE_TXD_QW1_CMD_S);
		xdp_ring->next_rs += tx_thresh;
	}
}

/**
 * ice_xmit_zc - take entries from XSK Tx ring and place them onto HW Tx ring
 * @xdp_ring: XDP ring to produce the HW Tx descriptors on
 * @budget: number of free descriptors on HW Tx ring that can be used
 * @napi_budget: amount of descriptors that NAPI allows us to clean
 *
 * Returns true if there is no more work that needs to be done, false otherwise
 */
bool ice_xmit_zc(struct ice_tx_ring *xdp_ring, u32 budget, int napi_budget)
{
	struct xdp_desc *descs = xdp_ring->xsk_pool->tx_descs;
	u16 tx_thresh = ICE_RING_QUARTER(xdp_ring);
	u32 nb_pkts, nb_processed = 0;
	unsigned int total_bytes = 0;

	if (budget < tx_thresh)
		budget += ice_clean_xdp_irq_zc(xdp_ring, napi_budget);

	nb_pkts = xsk_tx_peek_release_desc_batch(xdp_ring->xsk_pool, budget);
	if (!nb_pkts)
		return true;

	if (xdp_ring->next_to_use + nb_pkts >= xdp_ring->count) {
		struct ice_tx_desc *tx_desc;

		nb_processed = xdp_ring->count - xdp_ring->next_to_use;
		ice_fill_tx_hw_ring(xdp_ring, descs, nb_processed, &total_bytes);
		tx_desc = ICE_TX_DESC(xdp_ring, xdp_ring->next_rs);
		tx_desc->cmd_type_offset_bsz |=
			cpu_to_le64(ICE_TX_DESC_CMD_RS << ICE_TXD_QW1_CMD_S);
		xdp_ring->next_rs = tx_thresh - 1;
		xdp_ring->next_to_use = 0;
	}

	ice_fill_tx_hw_ring(xdp_ring, &descs[nb_processed], nb_pkts - nb_processed,
			    &total_bytes);

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

	if (test_bit(ICE_DOWN, vsi->state))
		return -ENETDOWN;

	if (!ice_is_xdp_ena_vsi(vsi))
		return -ENXIO;

	if (queue_id >= vsi->num_txq)
		return -ENXIO;

	if (!vsi->xdp_rings[queue_id]->xsk_pool)
		return -ENXIO;

	ring = vsi->xdp_rings[queue_id];

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
	u16 count_mask = rx_ring->count - 1;
	u16 ntc = rx_ring->next_to_clean;
	u16 ntu = rx_ring->next_to_use;

	for ( ; ntc != ntu; ntc = (ntc + 1) & count_mask) {
		struct xdp_buff *xdp = *ice_xdp_buf(rx_ring, ntc);

		xsk_buff_free(xdp);
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

		if (tx_buf->raw_buf)
			ice_clean_xdp_tx_buf(xdp_ring, tx_buf);
		else
			xsk_frames++;

		tx_buf->raw_buf = NULL;

		ntc++;
		if (ntc >= xdp_ring->count)
			ntc = 0;
	}

	if (xsk_frames)
		xsk_tx_completed(xdp_ring->xsk_pool, xsk_frames);
}
