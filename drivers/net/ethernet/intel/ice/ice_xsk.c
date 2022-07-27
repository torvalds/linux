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
ice_qvec_dis_irq(struct ice_vsi *vsi, struct ice_ring *rx_ring,
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
	struct ice_ring *ring;

	ice_cfg_itr(hw, q_vector);

	wr32(hw, GLINT_RATE(reg_idx),
	     ice_intrl_usec_to_reg(q_vector->intrl, hw->intrl_gran));

	ice_for_each_ring(ring, q_vector->tx)
		ice_cfg_txq_interrupt(vsi, ring->reg_idx, reg_idx,
				      q_vector->tx.itr_idx);

	ice_for_each_ring(ring, q_vector->rx)
		ice_cfg_rxq_interrupt(vsi, ring->reg_idx, reg_idx,
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
	struct ice_ring *tx_ring, *rx_ring;
	struct ice_q_vector *q_vector;
	int timeout = 50;
	int err;

	if (q_idx >= vsi->num_rxq || q_idx >= vsi->num_txq)
		return -EINVAL;

	tx_ring = vsi->tx_rings[q_idx];
	rx_ring = vsi->rx_rings[q_idx];
	q_vector = rx_ring->q_vector;

	while (test_and_set_bit(__ICE_CFG_BUSY, vsi->state)) {
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
		struct ice_ring *xdp_ring = vsi->xdp_rings[q_idx];

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
	struct ice_ring *tx_ring, *rx_ring;
	struct ice_q_vector *q_vector;
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
		struct ice_ring *xdp_ring = vsi->xdp_rings[q_idx];

		memset(qg_buf, 0, size);
		qg_buf->num_txqs = 1;
		err = ice_vsi_cfg_txq(vsi, xdp_ring, qg_buf);
		if (err)
			goto free_buf;
		ice_set_ring_xdp(xdp_ring);
		xdp_ring->xsk_pool = ice_xsk_pool(xdp_ring);
	}

	err = ice_setup_rx_ctx(rx_ring);
	if (err)
		goto free_buf;

	ice_qvec_cfg_msix(vsi, q_vector);

	err = ice_vsi_ctrl_one_rx_ring(vsi, true, q_idx, true);
	if (err)
		goto free_buf;

	clear_bit(__ICE_CFG_BUSY, vsi->state);
	ice_qvec_toggle_napi(vsi, q_vector, true);
	ice_qvec_ena_irq(vsi, q_vector);

	netif_tx_start_queue(netdev_get_tx_queue(vsi->netdev, q_idx));
free_buf:
	kfree(qg_buf);
	return err;
}

/**
 * ice_xsk_alloc_pools - allocate a buffer pool for an XDP socket
 * @vsi: VSI to allocate the buffer pool on
 *
 * Returns 0 on success, negative on error
 */
static int ice_xsk_alloc_pools(struct ice_vsi *vsi)
{
	if (vsi->xsk_pools)
		return 0;

	vsi->xsk_pools = kcalloc(vsi->num_xsk_pools, sizeof(*vsi->xsk_pools),
				 GFP_KERNEL);

	if (!vsi->xsk_pools) {
		vsi->num_xsk_pools = 0;
		return -ENOMEM;
	}

	return 0;
}

/**
 * ice_xsk_remove_pool - Remove an buffer pool for a certain ring/qid
 * @vsi: VSI from which the VSI will be removed
 * @qid: Ring/qid associated with the buffer pool
 */
static void ice_xsk_remove_pool(struct ice_vsi *vsi, u16 qid)
{
	vsi->xsk_pools[qid] = NULL;
	vsi->num_xsk_pools_used--;

	if (vsi->num_xsk_pools_used == 0) {
		kfree(vsi->xsk_pools);
		vsi->xsk_pools = NULL;
		vsi->num_xsk_pools = 0;
	}
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
	if (!vsi->xsk_pools || qid >= vsi->num_xsk_pools ||
	    !vsi->xsk_pools[qid])
		return -EINVAL;

	xsk_pool_dma_unmap(vsi->xsk_pools[qid], ICE_RX_DMA_ATTR);
	ice_xsk_remove_pool(vsi, qid);

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

	if (!vsi->num_xsk_pools)
		vsi->num_xsk_pools = min_t(u16, vsi->num_rxq, vsi->num_txq);
	if (qid >= vsi->num_xsk_pools)
		return -EINVAL;

	err = ice_xsk_alloc_pools(vsi);
	if (err)
		return err;

	if (vsi->xsk_pools && vsi->xsk_pools[qid])
		return -EBUSY;

	vsi->xsk_pools[qid] = pool;
	vsi->num_xsk_pools_used++;

	err = xsk_pool_dma_map(vsi->xsk_pools[qid], ice_pf_to_dev(vsi->back),
			       ICE_RX_DMA_ATTR);
	if (err)
		return err;

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

	if (pool_failure) {
		netdev_err(vsi->netdev, "Could not %sable buffer pool, error = %d\n",
			   pool_present ? "en" : "dis", pool_failure);
		return pool_failure;
	}

	return ret;
}

/**
 * ice_alloc_rx_bufs_zc - allocate a number of Rx buffers
 * @rx_ring: Rx ring
 * @count: The number of buffers to allocate
 *
 * This function allocates a number of Rx buffers from the fill ring
 * or the internal recycle mechanism and places them on the Rx ring.
 *
 * Returns false if all allocations were successful, true if any fail.
 */
bool ice_alloc_rx_bufs_zc(struct ice_ring *rx_ring, u16 count)
{
	union ice_32b_rx_flex_desc *rx_desc;
	u16 ntu = rx_ring->next_to_use;
	struct ice_rx_buf *rx_buf;
	bool ret = false;
	dma_addr_t dma;

	if (!count)
		return false;

	rx_desc = ICE_RX_DESC(rx_ring, ntu);
	rx_buf = &rx_ring->rx_buf[ntu];

	do {
		rx_buf->xdp = xsk_buff_alloc(rx_ring->xsk_pool);
		if (!rx_buf->xdp) {
			ret = true;
			break;
		}

		dma = xsk_buff_xdp_get_dma(rx_buf->xdp);
		rx_desc->read.pkt_addr = cpu_to_le64(dma);
		rx_desc->wb.status_error0 = 0;

		rx_desc++;
		rx_buf++;
		ntu++;

		if (unlikely(ntu == rx_ring->count)) {
			rx_desc = ICE_RX_DESC(rx_ring, 0);
			rx_buf = rx_ring->rx_buf;
			ntu = 0;
		}
	} while (--count);

	if (rx_ring->next_to_use != ntu) {
		/* clear the status bits for the next_to_use descriptor */
		rx_desc->wb.status_error0 = 0;
		ice_release_rx_desc(rx_ring, ntu);
	}

	return ret;
}

/**
 * ice_bump_ntc - Bump the next_to_clean counter of an Rx ring
 * @rx_ring: Rx ring
 */
static void ice_bump_ntc(struct ice_ring *rx_ring)
{
	int ntc = rx_ring->next_to_clean + 1;

	ntc = (ntc < rx_ring->count) ? ntc : 0;
	rx_ring->next_to_clean = ntc;
	prefetch(ICE_RX_DESC(rx_ring, ntc));
}

/**
 * ice_construct_skb_zc - Create an sk_buff from zero-copy buffer
 * @rx_ring: Rx ring
 * @rx_buf: zero-copy Rx buffer
 *
 * This function allocates a new skb from a zero-copy Rx buffer.
 *
 * Returns the skb on success, NULL on failure.
 */
static struct sk_buff *
ice_construct_skb_zc(struct ice_ring *rx_ring, struct ice_rx_buf *rx_buf)
{
	unsigned int metasize = rx_buf->xdp->data - rx_buf->xdp->data_meta;
	unsigned int datasize = rx_buf->xdp->data_end - rx_buf->xdp->data;
	unsigned int datasize_hard = rx_buf->xdp->data_end -
				     rx_buf->xdp->data_hard_start;
	struct sk_buff *skb;

	skb = __napi_alloc_skb(&rx_ring->q_vector->napi, datasize_hard,
			       GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(!skb))
		return NULL;

	skb_reserve(skb, rx_buf->xdp->data - rx_buf->xdp->data_hard_start);
	memcpy(__skb_put(skb, datasize), rx_buf->xdp->data, datasize);
	if (metasize)
		skb_metadata_set(skb, metasize);

	xsk_buff_free(rx_buf->xdp);
	rx_buf->xdp = NULL;
	return skb;
}

/**
 * ice_run_xdp_zc - Executes an XDP program in zero-copy path
 * @rx_ring: Rx ring
 * @xdp: xdp_buff used as input to the XDP program
 *
 * Returns any of ICE_XDP_{PASS, CONSUMED, TX, REDIR}
 */
static int
ice_run_xdp_zc(struct ice_ring *rx_ring, struct xdp_buff *xdp)
{
	int err, result = ICE_XDP_PASS;
	struct bpf_prog *xdp_prog;
	struct ice_ring *xdp_ring;
	u32 act;

	rcu_read_lock();
	xdp_prog = READ_ONCE(rx_ring->xdp_prog);
	if (!xdp_prog) {
		rcu_read_unlock();
		return ICE_XDP_PASS;
	}

	act = bpf_prog_run_xdp(xdp_prog, xdp);

	if (likely(act == XDP_REDIRECT)) {
		err = xdp_do_redirect(rx_ring->netdev, xdp, xdp_prog);
		if (err)
			goto out_failure;
		rcu_read_unlock();
		return ICE_XDP_REDIR;
	}

	switch (act) {
	case XDP_PASS:
		break;
	case XDP_TX:
		xdp_ring = rx_ring->vsi->xdp_rings[rx_ring->q_index];
		result = ice_xmit_xdp_buff(xdp, xdp_ring);
		if (result == ICE_XDP_CONSUMED)
			goto out_failure;
		break;
	default:
		bpf_warn_invalid_xdp_action(act);
		fallthrough;
	case XDP_ABORTED:
out_failure:
		trace_xdp_exception(rx_ring->netdev, xdp_prog, act);
		fallthrough;
	case XDP_DROP:
		result = ICE_XDP_CONSUMED;
		break;
	}

	rcu_read_unlock();
	return result;
}

/**
 * ice_clean_rx_irq_zc - consumes packets from the hardware ring
 * @rx_ring: AF_XDP Rx ring
 * @budget: NAPI budget
 *
 * Returns number of processed packets on success, remaining budget on failure.
 */
int ice_clean_rx_irq_zc(struct ice_ring *rx_ring, int budget)
{
	unsigned int total_rx_bytes = 0, total_rx_packets = 0;
	u16 cleaned_count = ICE_DESC_UNUSED(rx_ring);
	unsigned int xdp_xmit = 0;
	bool failure = false;

	while (likely(total_rx_packets < (unsigned int)budget)) {
		union ice_32b_rx_flex_desc *rx_desc;
		unsigned int size, xdp_res = 0;
		struct ice_rx_buf *rx_buf;
		struct sk_buff *skb;
		u16 stat_err_bits;
		u16 vlan_tag = 0;
		u8 rx_ptype;

		if (cleaned_count >= ICE_RX_BUF_WRITE) {
			failure |= ice_alloc_rx_bufs_zc(rx_ring,
							cleaned_count);
			cleaned_count = 0;
		}

		rx_desc = ICE_RX_DESC(rx_ring, rx_ring->next_to_clean);

		stat_err_bits = BIT(ICE_RX_FLEX_DESC_STATUS0_DD_S);
		if (!ice_test_staterr(rx_desc, stat_err_bits))
			break;

		/* This memory barrier is needed to keep us from reading
		 * any other fields out of the rx_desc until we have
		 * verified the descriptor has been written back.
		 */
		dma_rmb();

		size = le16_to_cpu(rx_desc->wb.pkt_len) &
				   ICE_RX_FLX_DESC_PKT_LEN_M;
		if (!size)
			break;

		rx_buf = &rx_ring->rx_buf[rx_ring->next_to_clean];
		rx_buf->xdp->data_end = rx_buf->xdp->data + size;
		xsk_buff_dma_sync_for_cpu(rx_buf->xdp, rx_ring->xsk_pool);

		xdp_res = ice_run_xdp_zc(rx_ring, rx_buf->xdp);
		if (xdp_res) {
			if (xdp_res & (ICE_XDP_TX | ICE_XDP_REDIR))
				xdp_xmit |= xdp_res;
			else
				xsk_buff_free(rx_buf->xdp);

			rx_buf->xdp = NULL;
			total_rx_bytes += size;
			total_rx_packets++;
			cleaned_count++;

			ice_bump_ntc(rx_ring);
			continue;
		}

		/* XDP_PASS path */
		skb = ice_construct_skb_zc(rx_ring, rx_buf);
		if (!skb) {
			rx_ring->rx_stats.alloc_buf_failed++;
			break;
		}

		cleaned_count++;
		ice_bump_ntc(rx_ring);

		if (eth_skb_pad(skb)) {
			skb = NULL;
			continue;
		}

		total_rx_bytes += skb->len;
		total_rx_packets++;

		stat_err_bits = BIT(ICE_RX_FLEX_DESC_STATUS0_L2TAG1P_S);
		if (ice_test_staterr(rx_desc, stat_err_bits))
			vlan_tag = le16_to_cpu(rx_desc->wb.l2tag1);

		rx_ptype = le16_to_cpu(rx_desc->wb.ptype_flex_flags0) &
				       ICE_RX_FLEX_DESC_PTYPE_M;

		ice_process_skb_fields(rx_ring, rx_desc, skb, rx_ptype);
		ice_receive_skb(rx_ring, skb, vlan_tag);
	}

	ice_finalize_xdp_rx(rx_ring, xdp_xmit);
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
 * ice_xmit_zc - Completes AF_XDP entries, and cleans XDP entries
 * @xdp_ring: XDP Tx ring
 * @budget: max number of frames to xmit
 *
 * Returns true if cleanup/transmission is done.
 */
static bool ice_xmit_zc(struct ice_ring *xdp_ring, int budget)
{
	struct ice_tx_desc *tx_desc = NULL;
	bool work_done = true;
	struct xdp_desc desc;
	dma_addr_t dma;

	while (likely(budget-- > 0)) {
		struct ice_tx_buf *tx_buf;

		if (unlikely(!ICE_DESC_UNUSED(xdp_ring))) {
			xdp_ring->tx_stats.tx_busy++;
			work_done = false;
			break;
		}

		tx_buf = &xdp_ring->tx_buf[xdp_ring->next_to_use];

		if (!xsk_tx_peek_desc(xdp_ring->xsk_pool, &desc))
			break;

		dma = xsk_buff_raw_get_dma(xdp_ring->xsk_pool, desc.addr);
		xsk_buff_raw_dma_sync_for_device(xdp_ring->xsk_pool, dma,
						 desc.len);

		tx_buf->bytecount = desc.len;

		tx_desc = ICE_TX_DESC(xdp_ring, xdp_ring->next_to_use);
		tx_desc->buf_addr = cpu_to_le64(dma);
		tx_desc->cmd_type_offset_bsz =
			ice_build_ctob(ICE_TXD_LAST_DESC_CMD, 0, desc.len, 0);

		xdp_ring->next_to_use++;
		if (xdp_ring->next_to_use == xdp_ring->count)
			xdp_ring->next_to_use = 0;
	}

	if (tx_desc) {
		ice_xdp_ring_update_tail(xdp_ring);
		xsk_tx_release(xdp_ring->xsk_pool);
	}

	return budget > 0 && work_done;
}

/**
 * ice_clean_xdp_tx_buf - Free and unmap XDP Tx buffer
 * @xdp_ring: XDP Tx ring
 * @tx_buf: Tx buffer to clean
 */
static void
ice_clean_xdp_tx_buf(struct ice_ring *xdp_ring, struct ice_tx_buf *tx_buf)
{
	xdp_return_frame((struct xdp_frame *)tx_buf->raw_buf);
	dma_unmap_single(xdp_ring->dev, dma_unmap_addr(tx_buf, dma),
			 dma_unmap_len(tx_buf, len), DMA_TO_DEVICE);
	dma_unmap_len_set(tx_buf, len, 0);
}

/**
 * ice_clean_tx_irq_zc - Completes AF_XDP entries, and cleans XDP entries
 * @xdp_ring: XDP Tx ring
 * @budget: NAPI budget
 *
 * Returns true if cleanup/tranmission is done.
 */
bool ice_clean_tx_irq_zc(struct ice_ring *xdp_ring, int budget)
{
	int total_packets = 0, total_bytes = 0;
	s16 ntc = xdp_ring->next_to_clean;
	struct ice_tx_desc *tx_desc;
	struct ice_tx_buf *tx_buf;
	u32 xsk_frames = 0;
	bool xmit_done;

	tx_desc = ICE_TX_DESC(xdp_ring, ntc);
	tx_buf = &xdp_ring->tx_buf[ntc];
	ntc -= xdp_ring->count;

	do {
		if (!(tx_desc->cmd_type_offset_bsz &
		      cpu_to_le64(ICE_TX_DESC_DTYPE_DESC_DONE)))
			break;

		total_bytes += tx_buf->bytecount;
		total_packets++;

		if (tx_buf->raw_buf) {
			ice_clean_xdp_tx_buf(xdp_ring, tx_buf);
			tx_buf->raw_buf = NULL;
		} else {
			xsk_frames++;
		}

		tx_desc->cmd_type_offset_bsz = 0;
		tx_buf++;
		tx_desc++;
		ntc++;

		if (unlikely(!ntc)) {
			ntc -= xdp_ring->count;
			tx_buf = xdp_ring->tx_buf;
			tx_desc = ICE_TX_DESC(xdp_ring, 0);
		}

		prefetch(tx_desc);

	} while (likely(--budget));

	ntc += xdp_ring->count;
	xdp_ring->next_to_clean = ntc;

	if (xsk_frames)
		xsk_tx_completed(xdp_ring->xsk_pool, xsk_frames);

	if (xsk_uses_need_wakeup(xdp_ring->xsk_pool))
		xsk_set_tx_need_wakeup(xdp_ring->xsk_pool);

	ice_update_tx_ring_stats(xdp_ring, total_packets, total_bytes);
	xmit_done = ice_xmit_zc(xdp_ring, ICE_DFLT_IRQ_WORK);

	return budget > 0 && xmit_done;
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
	struct ice_ring *ring;

	if (test_bit(__ICE_DOWN, vsi->state))
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

	if (!vsi->xsk_pools)
		return false;

	for (i = 0; i < vsi->num_xsk_pools; i++) {
		if (vsi->xsk_pools[i])
			return true;
	}

	return false;
}

/**
 * ice_xsk_clean_rx_ring - clean buffer pool queues connected to a given Rx ring
 * @rx_ring: ring to be cleaned
 */
void ice_xsk_clean_rx_ring(struct ice_ring *rx_ring)
{
	u16 i;

	for (i = 0; i < rx_ring->count; i++) {
		struct ice_rx_buf *rx_buf = &rx_ring->rx_buf[i];

		if (!rx_buf->xdp)
			continue;

		rx_buf->xdp = NULL;
	}
}

/**
 * ice_xsk_clean_xdp_ring - Clean the XDP Tx ring and its buffer pool queues
 * @xdp_ring: XDP_Tx ring
 */
void ice_xsk_clean_xdp_ring(struct ice_ring *xdp_ring)
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
