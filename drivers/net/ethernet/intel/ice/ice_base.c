// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Intel Corporation. */

#include <net/xdp_sock_drv.h>
#include "ice_base.h"
#include "ice_lib.h"
#include "ice_dcb_lib.h"
#include "ice_virtchnl_pf.h"

static bool ice_alloc_rx_buf_zc(struct ice_rx_ring *rx_ring)
{
	rx_ring->xdp_buf = kcalloc(rx_ring->count, sizeof(*rx_ring->xdp_buf), GFP_KERNEL);
	return !!rx_ring->xdp_buf;
}

static bool ice_alloc_rx_buf(struct ice_rx_ring *rx_ring)
{
	rx_ring->rx_buf = kcalloc(rx_ring->count, sizeof(*rx_ring->rx_buf), GFP_KERNEL);
	return !!rx_ring->rx_buf;
}

/**
 * __ice_vsi_get_qs_contig - Assign a contiguous chunk of queues to VSI
 * @qs_cfg: gathered variables needed for PF->VSI queues assignment
 *
 * Return 0 on success and -ENOMEM in case of no left space in PF queue bitmap
 */
static int __ice_vsi_get_qs_contig(struct ice_qs_cfg *qs_cfg)
{
	unsigned int offset, i;

	mutex_lock(qs_cfg->qs_mutex);
	offset = bitmap_find_next_zero_area(qs_cfg->pf_map, qs_cfg->pf_map_size,
					    0, qs_cfg->q_count, 0);
	if (offset >= qs_cfg->pf_map_size) {
		mutex_unlock(qs_cfg->qs_mutex);
		return -ENOMEM;
	}

	bitmap_set(qs_cfg->pf_map, offset, qs_cfg->q_count);
	for (i = 0; i < qs_cfg->q_count; i++)
		qs_cfg->vsi_map[i + qs_cfg->vsi_map_offset] = (u16)(i + offset);
	mutex_unlock(qs_cfg->qs_mutex);

	return 0;
}

/**
 * __ice_vsi_get_qs_sc - Assign a scattered queues from PF to VSI
 * @qs_cfg: gathered variables needed for pf->vsi queues assignment
 *
 * Return 0 on success and -ENOMEM in case of no left space in PF queue bitmap
 */
static int __ice_vsi_get_qs_sc(struct ice_qs_cfg *qs_cfg)
{
	unsigned int i, index = 0;

	mutex_lock(qs_cfg->qs_mutex);
	for (i = 0; i < qs_cfg->q_count; i++) {
		index = find_next_zero_bit(qs_cfg->pf_map,
					   qs_cfg->pf_map_size, index);
		if (index >= qs_cfg->pf_map_size)
			goto err_scatter;
		set_bit(index, qs_cfg->pf_map);
		qs_cfg->vsi_map[i + qs_cfg->vsi_map_offset] = (u16)index;
	}
	mutex_unlock(qs_cfg->qs_mutex);

	return 0;
err_scatter:
	for (index = 0; index < i; index++) {
		clear_bit(qs_cfg->vsi_map[index], qs_cfg->pf_map);
		qs_cfg->vsi_map[index + qs_cfg->vsi_map_offset] = 0;
	}
	mutex_unlock(qs_cfg->qs_mutex);

	return -ENOMEM;
}

/**
 * ice_pf_rxq_wait - Wait for a PF's Rx queue to be enabled or disabled
 * @pf: the PF being configured
 * @pf_q: the PF queue
 * @ena: enable or disable state of the queue
 *
 * This routine will wait for the given Rx queue of the PF to reach the
 * enabled or disabled state.
 * Returns -ETIMEDOUT in case of failing to reach the requested state after
 * multiple retries; else will return 0 in case of success.
 */
static int ice_pf_rxq_wait(struct ice_pf *pf, int pf_q, bool ena)
{
	int i;

	for (i = 0; i < ICE_Q_WAIT_MAX_RETRY; i++) {
		if (ena == !!(rd32(&pf->hw, QRX_CTRL(pf_q)) &
			      QRX_CTRL_QENA_STAT_M))
			return 0;

		usleep_range(20, 40);
	}

	return -ETIMEDOUT;
}

/**
 * ice_vsi_alloc_q_vector - Allocate memory for a single interrupt vector
 * @vsi: the VSI being configured
 * @v_idx: index of the vector in the VSI struct
 *
 * We allocate one q_vector and set default value for ITR setting associated
 * with this q_vector. If allocation fails we return -ENOMEM.
 */
static int ice_vsi_alloc_q_vector(struct ice_vsi *vsi, u16 v_idx)
{
	struct ice_pf *pf = vsi->back;
	struct ice_q_vector *q_vector;

	/* allocate q_vector */
	q_vector = devm_kzalloc(ice_pf_to_dev(pf), sizeof(*q_vector),
				GFP_KERNEL);
	if (!q_vector)
		return -ENOMEM;

	q_vector->vsi = vsi;
	q_vector->v_idx = v_idx;
	q_vector->tx.itr_setting = ICE_DFLT_TX_ITR;
	q_vector->rx.itr_setting = ICE_DFLT_RX_ITR;
	q_vector->tx.itr_mode = ITR_DYNAMIC;
	q_vector->rx.itr_mode = ITR_DYNAMIC;
	q_vector->tx.type = ICE_TX_CONTAINER;
	q_vector->rx.type = ICE_RX_CONTAINER;

	if (vsi->type == ICE_VSI_VF)
		goto out;
	/* only set affinity_mask if the CPU is online */
	if (cpu_online(v_idx))
		cpumask_set_cpu(v_idx, &q_vector->affinity_mask);

	/* This will not be called in the driver load path because the netdev
	 * will not be created yet. All other cases with register the NAPI
	 * handler here (i.e. resume, reset/rebuild, etc.)
	 */
	if (vsi->netdev)
		netif_napi_add(vsi->netdev, &q_vector->napi, ice_napi_poll,
			       NAPI_POLL_WEIGHT);

out:
	/* tie q_vector and VSI together */
	vsi->q_vectors[v_idx] = q_vector;

	return 0;
}

/**
 * ice_free_q_vector - Free memory allocated for a specific interrupt vector
 * @vsi: VSI having the memory freed
 * @v_idx: index of the vector to be freed
 */
static void ice_free_q_vector(struct ice_vsi *vsi, int v_idx)
{
	struct ice_q_vector *q_vector;
	struct ice_pf *pf = vsi->back;
	struct ice_tx_ring *tx_ring;
	struct ice_rx_ring *rx_ring;
	struct device *dev;

	dev = ice_pf_to_dev(pf);
	if (!vsi->q_vectors[v_idx]) {
		dev_dbg(dev, "Queue vector at index %d not found\n", v_idx);
		return;
	}
	q_vector = vsi->q_vectors[v_idx];

	ice_for_each_tx_ring(tx_ring, q_vector->tx)
		tx_ring->q_vector = NULL;
	ice_for_each_rx_ring(rx_ring, q_vector->rx)
		rx_ring->q_vector = NULL;

	/* only VSI with an associated netdev is set up with NAPI */
	if (vsi->netdev)
		netif_napi_del(&q_vector->napi);

	devm_kfree(dev, q_vector);
	vsi->q_vectors[v_idx] = NULL;
}

/**
 * ice_cfg_itr_gran - set the ITR granularity to 2 usecs if not already set
 * @hw: board specific structure
 */
static void ice_cfg_itr_gran(struct ice_hw *hw)
{
	u32 regval = rd32(hw, GLINT_CTL);

	/* no need to update global register if ITR gran is already set */
	if (!(regval & GLINT_CTL_DIS_AUTOMASK_M) &&
	    (((regval & GLINT_CTL_ITR_GRAN_200_M) >>
	     GLINT_CTL_ITR_GRAN_200_S) == ICE_ITR_GRAN_US) &&
	    (((regval & GLINT_CTL_ITR_GRAN_100_M) >>
	     GLINT_CTL_ITR_GRAN_100_S) == ICE_ITR_GRAN_US) &&
	    (((regval & GLINT_CTL_ITR_GRAN_50_M) >>
	     GLINT_CTL_ITR_GRAN_50_S) == ICE_ITR_GRAN_US) &&
	    (((regval & GLINT_CTL_ITR_GRAN_25_M) >>
	      GLINT_CTL_ITR_GRAN_25_S) == ICE_ITR_GRAN_US))
		return;

	regval = ((ICE_ITR_GRAN_US << GLINT_CTL_ITR_GRAN_200_S) &
		  GLINT_CTL_ITR_GRAN_200_M) |
		 ((ICE_ITR_GRAN_US << GLINT_CTL_ITR_GRAN_100_S) &
		  GLINT_CTL_ITR_GRAN_100_M) |
		 ((ICE_ITR_GRAN_US << GLINT_CTL_ITR_GRAN_50_S) &
		  GLINT_CTL_ITR_GRAN_50_M) |
		 ((ICE_ITR_GRAN_US << GLINT_CTL_ITR_GRAN_25_S) &
		  GLINT_CTL_ITR_GRAN_25_M);
	wr32(hw, GLINT_CTL, regval);
}

/**
 * ice_calc_txq_handle - calculate the queue handle
 * @vsi: VSI that ring belongs to
 * @ring: ring to get the absolute queue index
 * @tc: traffic class number
 */
static u16 ice_calc_txq_handle(struct ice_vsi *vsi, struct ice_tx_ring *ring, u8 tc)
{
	WARN_ONCE(ice_ring_is_xdp(ring) && tc, "XDP ring can't belong to TC other than 0\n");

	if (ring->ch)
		return ring->q_index - ring->ch->base_q;

	/* Idea here for calculation is that we subtract the number of queue
	 * count from TC that ring belongs to from it's absolute queue index
	 * and as a result we get the queue's index within TC.
	 */
	return ring->q_index - vsi->tc_cfg.tc_info[tc].qoffset;
}

/**
 * ice_eswitch_calc_txq_handle
 * @ring: pointer to ring which unique index is needed
 *
 * To correctly work with many netdevs ring->q_index of Tx rings on switchdev
 * VSI can repeat. Hardware ring setup requires unique q_index. Calculate it
 * here by finding index in vsi->tx_rings of this ring.
 *
 * Return ICE_INVAL_Q_INDEX when index wasn't found. Should never happen,
 * because VSI is get from ring->vsi, so it has to be present in this VSI.
 */
static u16 ice_eswitch_calc_txq_handle(struct ice_tx_ring *ring)
{
	struct ice_vsi *vsi = ring->vsi;
	int i;

	ice_for_each_txq(vsi, i) {
		if (vsi->tx_rings[i] == ring)
			return i;
	}

	return ICE_INVAL_Q_INDEX;
}

/**
 * ice_cfg_xps_tx_ring - Configure XPS for a Tx ring
 * @ring: The Tx ring to configure
 *
 * This enables/disables XPS for a given Tx descriptor ring
 * based on the TCs enabled for the VSI that ring belongs to.
 */
static void ice_cfg_xps_tx_ring(struct ice_tx_ring *ring)
{
	if (!ring->q_vector || !ring->netdev)
		return;

	/* We only initialize XPS once, so as not to overwrite user settings */
	if (test_and_set_bit(ICE_TX_XPS_INIT_DONE, ring->xps_state))
		return;

	netif_set_xps_queue(ring->netdev, &ring->q_vector->affinity_mask,
			    ring->q_index);
}

/**
 * ice_setup_tx_ctx - setup a struct ice_tlan_ctx instance
 * @ring: The Tx ring to configure
 * @tlan_ctx: Pointer to the Tx LAN queue context structure to be initialized
 * @pf_q: queue index in the PF space
 *
 * Configure the Tx descriptor ring in TLAN context.
 */
static void
ice_setup_tx_ctx(struct ice_tx_ring *ring, struct ice_tlan_ctx *tlan_ctx, u16 pf_q)
{
	struct ice_vsi *vsi = ring->vsi;
	struct ice_hw *hw = &vsi->back->hw;

	tlan_ctx->base = ring->dma >> ICE_TLAN_CTX_BASE_S;

	tlan_ctx->port_num = vsi->port_info->lport;

	/* Transmit Queue Length */
	tlan_ctx->qlen = ring->count;

	ice_set_cgd_num(tlan_ctx, ring->dcb_tc);

	/* PF number */
	tlan_ctx->pf_num = hw->pf_id;

	/* queue belongs to a specific VSI type
	 * VF / VM index should be programmed per vmvf_type setting:
	 * for vmvf_type = VF, it is VF number between 0-256
	 * for vmvf_type = VM, it is VM number between 0-767
	 * for PF or EMP this field should be set to zero
	 */
	switch (vsi->type) {
	case ICE_VSI_LB:
	case ICE_VSI_CTRL:
	case ICE_VSI_PF:
		if (ring->ch)
			tlan_ctx->vmvf_type = ICE_TLAN_CTX_VMVF_TYPE_VMQ;
		else
			tlan_ctx->vmvf_type = ICE_TLAN_CTX_VMVF_TYPE_PF;
		break;
	case ICE_VSI_VF:
		/* Firmware expects vmvf_num to be absolute VF ID */
		tlan_ctx->vmvf_num = hw->func_caps.vf_base_id + vsi->vf_id;
		tlan_ctx->vmvf_type = ICE_TLAN_CTX_VMVF_TYPE_VF;
		break;
	case ICE_VSI_SWITCHDEV_CTRL:
		tlan_ctx->vmvf_type = ICE_TLAN_CTX_VMVF_TYPE_VMQ;
		break;
	default:
		return;
	}

	/* make sure the context is associated with the right VSI */
	if (ring->ch)
		tlan_ctx->src_vsi = ring->ch->vsi_num;
	else
		tlan_ctx->src_vsi = ice_get_hw_vsi_num(hw, vsi->idx);

	/* Restrict Tx timestamps to the PF VSI */
	switch (vsi->type) {
	case ICE_VSI_PF:
		tlan_ctx->tsyn_ena = 1;
		break;
	default:
		break;
	}

	tlan_ctx->tso_ena = ICE_TX_LEGACY;
	tlan_ctx->tso_qnum = pf_q;

	/* Legacy or Advanced Host Interface:
	 * 0: Advanced Host Interface
	 * 1: Legacy Host Interface
	 */
	tlan_ctx->legacy_int = ICE_TX_LEGACY;
}

/**
 * ice_rx_offset - Return expected offset into page to access data
 * @rx_ring: Ring we are requesting offset of
 *
 * Returns the offset value for ring into the data buffer.
 */
static unsigned int ice_rx_offset(struct ice_rx_ring *rx_ring)
{
	if (ice_ring_uses_build_skb(rx_ring))
		return ICE_SKB_PAD;
	else if (ice_is_xdp_ena_vsi(rx_ring->vsi))
		return XDP_PACKET_HEADROOM;

	return 0;
}

/**
 * ice_setup_rx_ctx - Configure a receive ring context
 * @ring: The Rx ring to configure
 *
 * Configure the Rx descriptor ring in RLAN context.
 */
static int ice_setup_rx_ctx(struct ice_rx_ring *ring)
{
	int chain_len = ICE_MAX_CHAINED_RX_BUFS;
	struct ice_vsi *vsi = ring->vsi;
	u32 rxdid = ICE_RXDID_FLEX_NIC;
	struct ice_rlan_ctx rlan_ctx;
	struct ice_hw *hw;
	u16 pf_q;
	int err;

	hw = &vsi->back->hw;

	/* what is Rx queue number in global space of 2K Rx queues */
	pf_q = vsi->rxq_map[ring->q_index];

	/* clear the context structure first */
	memset(&rlan_ctx, 0, sizeof(rlan_ctx));

	/* Receive Queue Base Address.
	 * Indicates the starting address of the descriptor queue defined in
	 * 128 Byte units.
	 */
	rlan_ctx.base = ring->dma >> 7;

	rlan_ctx.qlen = ring->count;

	/* Receive Packet Data Buffer Size.
	 * The Packet Data Buffer Size is defined in 128 byte units.
	 */
	rlan_ctx.dbuf = ring->rx_buf_len >> ICE_RLAN_CTX_DBUF_S;

	/* use 32 byte descriptors */
	rlan_ctx.dsize = 1;

	/* Strip the Ethernet CRC bytes before the packet is posted to host
	 * memory.
	 */
	rlan_ctx.crcstrip = 1;

	/* L2TSEL flag defines the reported L2 Tags in the receive descriptor
	 * and it needs to remain 1 for non-DVM capable configurations to not
	 * break backward compatibility for VF drivers. Setting this field to 0
	 * will cause the single/outer VLAN tag to be stripped to the L2TAG2_2ND
	 * field in the Rx descriptor. Setting it to 1 allows the VLAN tag to
	 * be stripped in L2TAG1 of the Rx descriptor, which is where VFs will
	 * check for the tag
	 */
	if (ice_is_dvm_ena(hw))
		if (vsi->type == ICE_VSI_VF &&
		    ice_vf_is_port_vlan_ena(&vsi->back->vf[vsi->vf_id]))
			rlan_ctx.l2tsel = 1;
		else
			rlan_ctx.l2tsel = 0;
	else
		rlan_ctx.l2tsel = 1;

	rlan_ctx.dtype = ICE_RX_DTYPE_NO_SPLIT;
	rlan_ctx.hsplit_0 = ICE_RLAN_RX_HSPLIT_0_NO_SPLIT;
	rlan_ctx.hsplit_1 = ICE_RLAN_RX_HSPLIT_1_NO_SPLIT;

	/* This controls whether VLAN is stripped from inner headers
	 * The VLAN in the inner L2 header is stripped to the receive
	 * descriptor if enabled by this flag.
	 */
	rlan_ctx.showiv = 0;

	/* For AF_XDP ZC, we disallow packets to span on
	 * multiple buffers, thus letting us skip that
	 * handling in the fast-path.
	 */
	if (ring->xsk_pool)
		chain_len = 1;
	/* Max packet size for this queue - must not be set to a larger value
	 * than 5 x DBUF
	 */
	rlan_ctx.rxmax = min_t(u32, vsi->max_frame,
			       chain_len * ring->rx_buf_len);

	/* Rx queue threshold in units of 64 */
	rlan_ctx.lrxqthresh = 1;

	/* Enable Flexible Descriptors in the queue context which
	 * allows this driver to select a specific receive descriptor format
	 * increasing context priority to pick up profile ID; default is 0x01;
	 * setting to 0x03 to ensure profile is programming if prev context is
	 * of same priority
	 */
	if (vsi->type != ICE_VSI_VF)
		ice_write_qrxflxp_cntxt(hw, pf_q, rxdid, 0x3, true);
	else
		ice_write_qrxflxp_cntxt(hw, pf_q, ICE_RXDID_LEGACY_1, 0x3,
					false);

	/* Absolute queue number out of 2K needs to be passed */
	err = ice_write_rxq_ctx(hw, &rlan_ctx, pf_q);
	if (err) {
		dev_err(ice_pf_to_dev(vsi->back), "Failed to set LAN Rx queue context for absolute Rx queue %d error: %d\n",
			pf_q, err);
		return -EIO;
	}

	if (vsi->type == ICE_VSI_VF)
		return 0;

	/* configure Rx buffer alignment */
	if (!vsi->netdev || test_bit(ICE_FLAG_LEGACY_RX, vsi->back->flags))
		ice_clear_ring_build_skb_ena(ring);
	else
		ice_set_ring_build_skb_ena(ring);

	ring->rx_offset = ice_rx_offset(ring);

	/* init queue specific tail register */
	ring->tail = hw->hw_addr + QRX_TAIL(pf_q);
	writel(0, ring->tail);

	return 0;
}

/**
 * ice_vsi_cfg_rxq - Configure an Rx queue
 * @ring: the ring being configured
 *
 * Return 0 on success and a negative value on error.
 */
int ice_vsi_cfg_rxq(struct ice_rx_ring *ring)
{
	struct device *dev = ice_pf_to_dev(ring->vsi->back);
	u16 num_bufs = ICE_DESC_UNUSED(ring);
	int err;

	ring->rx_buf_len = ring->vsi->rx_buf_len;

	if (ring->vsi->type == ICE_VSI_PF) {
		if (!xdp_rxq_info_is_reg(&ring->xdp_rxq))
			/* coverity[check_return] */
			xdp_rxq_info_reg(&ring->xdp_rxq, ring->netdev,
					 ring->q_index, ring->q_vector->napi.napi_id);

		kfree(ring->rx_buf);
		ring->xsk_pool = ice_xsk_pool(ring);
		if (ring->xsk_pool) {
			if (!ice_alloc_rx_buf_zc(ring))
				return -ENOMEM;
			xdp_rxq_info_unreg_mem_model(&ring->xdp_rxq);

			ring->rx_buf_len =
				xsk_pool_get_rx_frame_size(ring->xsk_pool);
			err = xdp_rxq_info_reg_mem_model(&ring->xdp_rxq,
							 MEM_TYPE_XSK_BUFF_POOL,
							 NULL);
			if (err)
				return err;
			xsk_pool_set_rxq_info(ring->xsk_pool, &ring->xdp_rxq);

			dev_info(dev, "Registered XDP mem model MEM_TYPE_XSK_BUFF_POOL on Rx ring %d\n",
				 ring->q_index);
		} else {
			if (!ice_alloc_rx_buf(ring))
				return -ENOMEM;
			if (!xdp_rxq_info_is_reg(&ring->xdp_rxq))
				/* coverity[check_return] */
				xdp_rxq_info_reg(&ring->xdp_rxq,
						 ring->netdev,
						 ring->q_index, ring->q_vector->napi.napi_id);

			err = xdp_rxq_info_reg_mem_model(&ring->xdp_rxq,
							 MEM_TYPE_PAGE_SHARED,
							 NULL);
			if (err)
				return err;
		}
	}

	err = ice_setup_rx_ctx(ring);
	if (err) {
		dev_err(dev, "ice_setup_rx_ctx failed for RxQ %d, err %d\n",
			ring->q_index, err);
		return err;
	}

	if (ring->xsk_pool) {
		bool ok;

		if (!xsk_buff_can_alloc(ring->xsk_pool, num_bufs)) {
			dev_warn(dev, "XSK buffer pool does not provide enough addresses to fill %d buffers on Rx ring %d\n",
				 num_bufs, ring->q_index);
			dev_warn(dev, "Change Rx ring/fill queue size to avoid performance issues\n");

			return 0;
		}

		ok = ice_alloc_rx_bufs_zc(ring, num_bufs);
		if (!ok) {
			u16 pf_q = ring->vsi->rxq_map[ring->q_index];

			dev_info(dev, "Failed to allocate some buffers on XSK buffer pool enabled Rx ring %d (pf_q %d)\n",
				 ring->q_index, pf_q);
		}

		return 0;
	}

	ice_alloc_rx_bufs(ring, num_bufs);

	return 0;
}

/**
 * __ice_vsi_get_qs - helper function for assigning queues from PF to VSI
 * @qs_cfg: gathered variables needed for pf->vsi queues assignment
 *
 * This function first tries to find contiguous space. If it is not successful,
 * it tries with the scatter approach.
 *
 * Return 0 on success and -ENOMEM in case of no left space in PF queue bitmap
 */
int __ice_vsi_get_qs(struct ice_qs_cfg *qs_cfg)
{
	int ret = 0;

	ret = __ice_vsi_get_qs_contig(qs_cfg);
	if (ret) {
		/* contig failed, so try with scatter approach */
		qs_cfg->mapping_mode = ICE_VSI_MAP_SCATTER;
		qs_cfg->q_count = min_t(unsigned int, qs_cfg->q_count,
					qs_cfg->scatter_count);
		ret = __ice_vsi_get_qs_sc(qs_cfg);
	}
	return ret;
}

/**
 * ice_vsi_ctrl_one_rx_ring - start/stop VSI's Rx ring with no busy wait
 * @vsi: the VSI being configured
 * @ena: start or stop the Rx ring
 * @rxq_idx: 0-based Rx queue index for the VSI passed in
 * @wait: wait or don't wait for configuration to finish in hardware
 *
 * Return 0 on success and negative on error.
 */
int
ice_vsi_ctrl_one_rx_ring(struct ice_vsi *vsi, bool ena, u16 rxq_idx, bool wait)
{
	int pf_q = vsi->rxq_map[rxq_idx];
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	u32 rx_reg;

	rx_reg = rd32(hw, QRX_CTRL(pf_q));

	/* Skip if the queue is already in the requested state */
	if (ena == !!(rx_reg & QRX_CTRL_QENA_STAT_M))
		return 0;

	/* turn on/off the queue */
	if (ena)
		rx_reg |= QRX_CTRL_QENA_REQ_M;
	else
		rx_reg &= ~QRX_CTRL_QENA_REQ_M;
	wr32(hw, QRX_CTRL(pf_q), rx_reg);

	if (!wait)
		return 0;

	ice_flush(hw);
	return ice_pf_rxq_wait(pf, pf_q, ena);
}

/**
 * ice_vsi_wait_one_rx_ring - wait for a VSI's Rx ring to be stopped/started
 * @vsi: the VSI being configured
 * @ena: true/false to verify Rx ring has been enabled/disabled respectively
 * @rxq_idx: 0-based Rx queue index for the VSI passed in
 *
 * This routine will wait for the given Rx queue of the VSI to reach the
 * enabled or disabled state. Returns -ETIMEDOUT in case of failing to reach
 * the requested state after multiple retries; else will return 0 in case of
 * success.
 */
int ice_vsi_wait_one_rx_ring(struct ice_vsi *vsi, bool ena, u16 rxq_idx)
{
	int pf_q = vsi->rxq_map[rxq_idx];
	struct ice_pf *pf = vsi->back;

	return ice_pf_rxq_wait(pf, pf_q, ena);
}

/**
 * ice_vsi_alloc_q_vectors - Allocate memory for interrupt vectors
 * @vsi: the VSI being configured
 *
 * We allocate one q_vector per queue interrupt. If allocation fails we
 * return -ENOMEM.
 */
int ice_vsi_alloc_q_vectors(struct ice_vsi *vsi)
{
	struct device *dev = ice_pf_to_dev(vsi->back);
	u16 v_idx;
	int err;

	if (vsi->q_vectors[0]) {
		dev_dbg(dev, "VSI %d has existing q_vectors\n", vsi->vsi_num);
		return -EEXIST;
	}

	for (v_idx = 0; v_idx < vsi->num_q_vectors; v_idx++) {
		err = ice_vsi_alloc_q_vector(vsi, v_idx);
		if (err)
			goto err_out;
	}

	return 0;

err_out:
	while (v_idx--)
		ice_free_q_vector(vsi, v_idx);

	dev_err(dev, "Failed to allocate %d q_vector for VSI %d, ret=%d\n",
		vsi->num_q_vectors, vsi->vsi_num, err);
	vsi->num_q_vectors = 0;
	return err;
}

/**
 * ice_vsi_map_rings_to_vectors - Map VSI rings to interrupt vectors
 * @vsi: the VSI being configured
 *
 * This function maps descriptor rings to the queue-specific vectors allotted
 * through the MSI-X enabling code. On a constrained vector budget, we map Tx
 * and Rx rings to the vector as "efficiently" as possible.
 */
void ice_vsi_map_rings_to_vectors(struct ice_vsi *vsi)
{
	int q_vectors = vsi->num_q_vectors;
	u16 tx_rings_rem, rx_rings_rem;
	int v_id;

	/* initially assigning remaining rings count to VSIs num queue value */
	tx_rings_rem = vsi->num_txq;
	rx_rings_rem = vsi->num_rxq;

	for (v_id = 0; v_id < q_vectors; v_id++) {
		struct ice_q_vector *q_vector = vsi->q_vectors[v_id];
		u8 tx_rings_per_v, rx_rings_per_v;
		u16 q_id, q_base;

		/* Tx rings mapping to vector */
		tx_rings_per_v = (u8)DIV_ROUND_UP(tx_rings_rem,
						  q_vectors - v_id);
		q_vector->num_ring_tx = tx_rings_per_v;
		q_vector->tx.tx_ring = NULL;
		q_vector->tx.itr_idx = ICE_TX_ITR;
		q_base = vsi->num_txq - tx_rings_rem;

		for (q_id = q_base; q_id < (q_base + tx_rings_per_v); q_id++) {
			struct ice_tx_ring *tx_ring = vsi->tx_rings[q_id];

			tx_ring->q_vector = q_vector;
			tx_ring->next = q_vector->tx.tx_ring;
			q_vector->tx.tx_ring = tx_ring;
		}
		tx_rings_rem -= tx_rings_per_v;

		/* Rx rings mapping to vector */
		rx_rings_per_v = (u8)DIV_ROUND_UP(rx_rings_rem,
						  q_vectors - v_id);
		q_vector->num_ring_rx = rx_rings_per_v;
		q_vector->rx.rx_ring = NULL;
		q_vector->rx.itr_idx = ICE_RX_ITR;
		q_base = vsi->num_rxq - rx_rings_rem;

		for (q_id = q_base; q_id < (q_base + rx_rings_per_v); q_id++) {
			struct ice_rx_ring *rx_ring = vsi->rx_rings[q_id];

			rx_ring->q_vector = q_vector;
			rx_ring->next = q_vector->rx.rx_ring;
			q_vector->rx.rx_ring = rx_ring;
		}
		rx_rings_rem -= rx_rings_per_v;
	}
}

/**
 * ice_vsi_free_q_vectors - Free memory allocated for interrupt vectors
 * @vsi: the VSI having memory freed
 */
void ice_vsi_free_q_vectors(struct ice_vsi *vsi)
{
	int v_idx;

	ice_for_each_q_vector(vsi, v_idx)
		ice_free_q_vector(vsi, v_idx);
}

/**
 * ice_vsi_cfg_txq - Configure single Tx queue
 * @vsi: the VSI that queue belongs to
 * @ring: Tx ring to be configured
 * @qg_buf: queue group buffer
 */
int
ice_vsi_cfg_txq(struct ice_vsi *vsi, struct ice_tx_ring *ring,
		struct ice_aqc_add_tx_qgrp *qg_buf)
{
	u8 buf_len = struct_size(qg_buf, txqs, 1);
	struct ice_tlan_ctx tlan_ctx = { 0 };
	struct ice_aqc_add_txqs_perq *txq;
	struct ice_channel *ch = ring->ch;
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	int status;
	u16 pf_q;
	u8 tc;

	/* Configure XPS */
	ice_cfg_xps_tx_ring(ring);

	pf_q = ring->reg_idx;
	ice_setup_tx_ctx(ring, &tlan_ctx, pf_q);
	/* copy context contents into the qg_buf */
	qg_buf->txqs[0].txq_id = cpu_to_le16(pf_q);
	ice_set_ctx(hw, (u8 *)&tlan_ctx, qg_buf->txqs[0].txq_ctx,
		    ice_tlan_ctx_info);

	/* init queue specific tail reg. It is referred as
	 * transmit comm scheduler queue doorbell.
	 */
	ring->tail = hw->hw_addr + QTX_COMM_DBELL(pf_q);

	if (IS_ENABLED(CONFIG_DCB))
		tc = ring->dcb_tc;
	else
		tc = 0;

	/* Add unique software queue handle of the Tx queue per
	 * TC into the VSI Tx ring
	 */
	if (vsi->type == ICE_VSI_SWITCHDEV_CTRL) {
		ring->q_handle = ice_eswitch_calc_txq_handle(ring);

		if (ring->q_handle == ICE_INVAL_Q_INDEX)
			return -ENODEV;
	} else {
		ring->q_handle = ice_calc_txq_handle(vsi, ring, tc);
	}

	if (ch)
		status = ice_ena_vsi_txq(vsi->port_info, ch->ch_vsi->idx, 0,
					 ring->q_handle, 1, qg_buf, buf_len,
					 NULL);
	else
		status = ice_ena_vsi_txq(vsi->port_info, vsi->idx, tc,
					 ring->q_handle, 1, qg_buf, buf_len,
					 NULL);
	if (status) {
		dev_err(ice_pf_to_dev(pf), "Failed to set LAN Tx queue context, error: %d\n",
			status);
		return status;
	}

	/* Add Tx Queue TEID into the VSI Tx ring from the
	 * response. This will complete configuring and
	 * enabling the queue.
	 */
	txq = &qg_buf->txqs[0];
	if (pf_q == le16_to_cpu(txq->txq_id))
		ring->txq_teid = le32_to_cpu(txq->q_teid);

	return 0;
}

/**
 * ice_cfg_itr - configure the initial interrupt throttle values
 * @hw: pointer to the HW structure
 * @q_vector: interrupt vector that's being configured
 *
 * Configure interrupt throttling values for the ring containers that are
 * associated with the interrupt vector passed in.
 */
void ice_cfg_itr(struct ice_hw *hw, struct ice_q_vector *q_vector)
{
	ice_cfg_itr_gran(hw);

	if (q_vector->num_ring_rx)
		ice_write_itr(&q_vector->rx, q_vector->rx.itr_setting);

	if (q_vector->num_ring_tx)
		ice_write_itr(&q_vector->tx, q_vector->tx.itr_setting);

	ice_write_intrl(q_vector, q_vector->intrl);
}

/**
 * ice_cfg_txq_interrupt - configure interrupt on Tx queue
 * @vsi: the VSI being configured
 * @txq: Tx queue being mapped to MSI-X vector
 * @msix_idx: MSI-X vector index within the function
 * @itr_idx: ITR index of the interrupt cause
 *
 * Configure interrupt on Tx queue by associating Tx queue to MSI-X vector
 * within the function space.
 */
void
ice_cfg_txq_interrupt(struct ice_vsi *vsi, u16 txq, u16 msix_idx, u16 itr_idx)
{
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	u32 val;

	itr_idx = (itr_idx << QINT_TQCTL_ITR_INDX_S) & QINT_TQCTL_ITR_INDX_M;

	val = QINT_TQCTL_CAUSE_ENA_M | itr_idx |
	      ((msix_idx << QINT_TQCTL_MSIX_INDX_S) & QINT_TQCTL_MSIX_INDX_M);

	wr32(hw, QINT_TQCTL(vsi->txq_map[txq]), val);
	if (ice_is_xdp_ena_vsi(vsi)) {
		u32 xdp_txq = txq + vsi->num_xdp_txq;

		wr32(hw, QINT_TQCTL(vsi->txq_map[xdp_txq]),
		     val);
	}
	ice_flush(hw);
}

/**
 * ice_cfg_rxq_interrupt - configure interrupt on Rx queue
 * @vsi: the VSI being configured
 * @rxq: Rx queue being mapped to MSI-X vector
 * @msix_idx: MSI-X vector index within the function
 * @itr_idx: ITR index of the interrupt cause
 *
 * Configure interrupt on Rx queue by associating Rx queue to MSI-X vector
 * within the function space.
 */
void
ice_cfg_rxq_interrupt(struct ice_vsi *vsi, u16 rxq, u16 msix_idx, u16 itr_idx)
{
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	u32 val;

	itr_idx = (itr_idx << QINT_RQCTL_ITR_INDX_S) & QINT_RQCTL_ITR_INDX_M;

	val = QINT_RQCTL_CAUSE_ENA_M | itr_idx |
	      ((msix_idx << QINT_RQCTL_MSIX_INDX_S) & QINT_RQCTL_MSIX_INDX_M);

	wr32(hw, QINT_RQCTL(vsi->rxq_map[rxq]), val);

	ice_flush(hw);
}

/**
 * ice_trigger_sw_intr - trigger a software interrupt
 * @hw: pointer to the HW structure
 * @q_vector: interrupt vector to trigger the software interrupt for
 */
void ice_trigger_sw_intr(struct ice_hw *hw, struct ice_q_vector *q_vector)
{
	wr32(hw, GLINT_DYN_CTL(q_vector->reg_idx),
	     (ICE_ITR_NONE << GLINT_DYN_CTL_ITR_INDX_S) |
	     GLINT_DYN_CTL_SWINT_TRIG_M |
	     GLINT_DYN_CTL_INTENA_M);
}

/**
 * ice_vsi_stop_tx_ring - Disable single Tx ring
 * @vsi: the VSI being configured
 * @rst_src: reset source
 * @rel_vmvf_num: Relative ID of VF/VM
 * @ring: Tx ring to be stopped
 * @txq_meta: Meta data of Tx ring to be stopped
 */
int
ice_vsi_stop_tx_ring(struct ice_vsi *vsi, enum ice_disq_rst_src rst_src,
		     u16 rel_vmvf_num, struct ice_tx_ring *ring,
		     struct ice_txq_meta *txq_meta)
{
	struct ice_pf *pf = vsi->back;
	struct ice_q_vector *q_vector;
	struct ice_hw *hw = &pf->hw;
	int status;
	u32 val;

	/* clear cause_ena bit for disabled queues */
	val = rd32(hw, QINT_TQCTL(ring->reg_idx));
	val &= ~QINT_TQCTL_CAUSE_ENA_M;
	wr32(hw, QINT_TQCTL(ring->reg_idx), val);

	/* software is expected to wait for 100 ns */
	ndelay(100);

	/* trigger a software interrupt for the vector
	 * associated to the queue to schedule NAPI handler
	 */
	q_vector = ring->q_vector;
	if (q_vector)
		ice_trigger_sw_intr(hw, q_vector);

	status = ice_dis_vsi_txq(vsi->port_info, txq_meta->vsi_idx,
				 txq_meta->tc, 1, &txq_meta->q_handle,
				 &txq_meta->q_id, &txq_meta->q_teid, rst_src,
				 rel_vmvf_num, NULL);

	/* if the disable queue command was exercised during an
	 * active reset flow, -EBUSY is returned.
	 * This is not an error as the reset operation disables
	 * queues at the hardware level anyway.
	 */
	if (status == -EBUSY) {
		dev_dbg(ice_pf_to_dev(vsi->back), "Reset in progress. LAN Tx queues already disabled\n");
	} else if (status == -ENOENT) {
		dev_dbg(ice_pf_to_dev(vsi->back), "LAN Tx queues do not exist, nothing to disable\n");
	} else if (status) {
		dev_dbg(ice_pf_to_dev(vsi->back), "Failed to disable LAN Tx queues, error: %d\n",
			status);
		return status;
	}

	return 0;
}

/**
 * ice_fill_txq_meta - Prepare the Tx queue's meta data
 * @vsi: VSI that ring belongs to
 * @ring: ring that txq_meta will be based on
 * @txq_meta: a helper struct that wraps Tx queue's information
 *
 * Set up a helper struct that will contain all the necessary fields that
 * are needed for stopping Tx queue
 */
void
ice_fill_txq_meta(struct ice_vsi *vsi, struct ice_tx_ring *ring,
		  struct ice_txq_meta *txq_meta)
{
	struct ice_channel *ch = ring->ch;
	u8 tc;

	if (IS_ENABLED(CONFIG_DCB))
		tc = ring->dcb_tc;
	else
		tc = 0;

	txq_meta->q_id = ring->reg_idx;
	txq_meta->q_teid = ring->txq_teid;
	txq_meta->q_handle = ring->q_handle;
	if (ch) {
		txq_meta->vsi_idx = ch->ch_vsi->idx;
		txq_meta->tc = 0;
	} else {
		txq_meta->vsi_idx = vsi->idx;
		txq_meta->tc = tc;
	}
}
