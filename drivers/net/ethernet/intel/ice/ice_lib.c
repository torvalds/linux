// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"
#include "ice_dcb_lib.h"

/**
 * ice_setup_rx_ctx - Configure a receive ring context
 * @ring: The Rx ring to configure
 *
 * Configure the Rx descriptor ring in RLAN context.
 */
static int ice_setup_rx_ctx(struct ice_ring *ring)
{
	struct ice_vsi *vsi = ring->vsi;
	struct ice_hw *hw = &vsi->back->hw;
	u32 rxdid = ICE_RXDID_FLEX_NIC;
	struct ice_rlan_ctx rlan_ctx;
	u32 regval;
	u16 pf_q;
	int err;

	/* what is Rx queue number in global space of 2K Rx queues */
	pf_q = vsi->rxq_map[ring->q_index];

	/* clear the context structure first */
	memset(&rlan_ctx, 0, sizeof(rlan_ctx));

	rlan_ctx.base = ring->dma >> 7;

	rlan_ctx.qlen = ring->count;

	/* Receive Packet Data Buffer Size.
	 * The Packet Data Buffer Size is defined in 128 byte units.
	 */
	rlan_ctx.dbuf = vsi->rx_buf_len >> ICE_RLAN_CTX_DBUF_S;

	/* use 32 byte descriptors */
	rlan_ctx.dsize = 1;

	/* Strip the Ethernet CRC bytes before the packet is posted to host
	 * memory.
	 */
	rlan_ctx.crcstrip = 1;

	/* L2TSEL flag defines the reported L2 Tags in the receive descriptor */
	rlan_ctx.l2tsel = 1;

	rlan_ctx.dtype = ICE_RX_DTYPE_NO_SPLIT;
	rlan_ctx.hsplit_0 = ICE_RLAN_RX_HSPLIT_0_NO_SPLIT;
	rlan_ctx.hsplit_1 = ICE_RLAN_RX_HSPLIT_1_NO_SPLIT;

	/* This controls whether VLAN is stripped from inner headers
	 * The VLAN in the inner L2 header is stripped to the receive
	 * descriptor if enabled by this flag.
	 */
	rlan_ctx.showiv = 0;

	/* Max packet size for this queue - must not be set to a larger value
	 * than 5 x DBUF
	 */
	rlan_ctx.rxmax = min_t(u16, vsi->max_frame,
			       ICE_MAX_CHAINED_RX_BUFS * vsi->rx_buf_len);

	/* Rx queue threshold in units of 64 */
	rlan_ctx.lrxqthresh = 1;

	 /* Enable Flexible Descriptors in the queue context which
	  * allows this driver to select a specific receive descriptor format
	  */
	if (vsi->type != ICE_VSI_VF) {
		regval = rd32(hw, QRXFLXP_CNTXT(pf_q));
		regval |= (rxdid << QRXFLXP_CNTXT_RXDID_IDX_S) &
			QRXFLXP_CNTXT_RXDID_IDX_M;

		/* increasing context priority to pick up profile ID;
		 * default is 0x01; setting to 0x03 to ensure profile
		 * is programming if prev context is of same priority
		 */
		regval |= (0x03 << QRXFLXP_CNTXT_RXDID_PRIO_S) &
			QRXFLXP_CNTXT_RXDID_PRIO_M;

		wr32(hw, QRXFLXP_CNTXT(pf_q), regval);
	}

	/* Absolute queue number out of 2K needs to be passed */
	err = ice_write_rxq_ctx(hw, &rlan_ctx, pf_q);
	if (err) {
		dev_err(&vsi->back->pdev->dev,
			"Failed to set LAN Rx queue context for absolute Rx queue %d error: %d\n",
			pf_q, err);
		return -EIO;
	}

	if (vsi->type == ICE_VSI_VF)
		return 0;

	/* init queue specific tail register */
	ring->tail = hw->hw_addr + QRX_TAIL(pf_q);
	writel(0, ring->tail);
	ice_alloc_rx_bufs(ring, ICE_DESC_UNUSED(ring));

	return 0;
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
ice_setup_tx_ctx(struct ice_ring *ring, struct ice_tlan_ctx *tlan_ctx, u16 pf_q)
{
	struct ice_vsi *vsi = ring->vsi;
	struct ice_hw *hw = &vsi->back->hw;

	tlan_ctx->base = ring->dma >> ICE_TLAN_CTX_BASE_S;

	tlan_ctx->port_num = vsi->port_info->lport;

	/* Transmit Queue Length */
	tlan_ctx->qlen = ring->count;

	ice_set_cgd_num(tlan_ctx, ring);

	/* PF number */
	tlan_ctx->pf_num = hw->pf_id;

	/* queue belongs to a specific VSI type
	 * VF / VM index should be programmed per vmvf_type setting:
	 * for vmvf_type = VF, it is VF number between 0-256
	 * for vmvf_type = VM, it is VM number between 0-767
	 * for PF or EMP this field should be set to zero
	 */
	switch (vsi->type) {
	case ICE_VSI_PF:
		tlan_ctx->vmvf_type = ICE_TLAN_CTX_VMVF_TYPE_PF;
		break;
	case ICE_VSI_VF:
		/* Firmware expects vmvf_num to be absolute VF ID */
		tlan_ctx->vmvf_num = hw->func_caps.vf_base_id + vsi->vf_id;
		tlan_ctx->vmvf_type = ICE_TLAN_CTX_VMVF_TYPE_VF;
		break;
	default:
		return;
	}

	/* make sure the context is associated with the right VSI */
	tlan_ctx->src_vsi = ice_get_hw_vsi_num(hw, vsi->idx);

	tlan_ctx->tso_ena = ICE_TX_LEGACY;
	tlan_ctx->tso_qnum = pf_q;

	/* Legacy or Advanced Host Interface:
	 * 0: Advanced Host Interface
	 * 1: Legacy Host Interface
	 */
	tlan_ctx->legacy_int = ICE_TX_LEGACY;
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
 * ice_vsi_ctrl_rx_rings - Start or stop a VSI's Rx rings
 * @vsi: the VSI being configured
 * @ena: start or stop the Rx rings
 */
static int ice_vsi_ctrl_rx_rings(struct ice_vsi *vsi, bool ena)
{
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	int i, ret = 0;

	for (i = 0; i < vsi->num_rxq; i++) {
		int pf_q = vsi->rxq_map[i];
		u32 rx_reg;

		rx_reg = rd32(hw, QRX_CTRL(pf_q));

		/* Skip if the queue is already in the requested state */
		if (ena == !!(rx_reg & QRX_CTRL_QENA_STAT_M))
			continue;

		/* turn on/off the queue */
		if (ena)
			rx_reg |= QRX_CTRL_QENA_REQ_M;
		else
			rx_reg &= ~QRX_CTRL_QENA_REQ_M;
		wr32(hw, QRX_CTRL(pf_q), rx_reg);

		/* wait for the change to finish */
		ret = ice_pf_rxq_wait(pf, pf_q, ena);
		if (ret) {
			dev_err(&pf->pdev->dev,
				"VSI idx %d Rx ring %d %sable timeout\n",
				vsi->idx, pf_q, (ena ? "en" : "dis"));
			break;
		}
	}

	return ret;
}

/**
 * ice_vsi_alloc_arrays - Allocate queue and vector pointer arrays for the VSI
 * @vsi: VSI pointer
 *
 * On error: returns error code (negative)
 * On success: returns 0
 */
static int ice_vsi_alloc_arrays(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;

	/* allocate memory for both Tx and Rx ring pointers */
	vsi->tx_rings = devm_kcalloc(&pf->pdev->dev, vsi->alloc_txq,
				     sizeof(*vsi->tx_rings), GFP_KERNEL);
	if (!vsi->tx_rings)
		goto err_txrings;

	vsi->rx_rings = devm_kcalloc(&pf->pdev->dev, vsi->alloc_rxq,
				     sizeof(*vsi->rx_rings), GFP_KERNEL);
	if (!vsi->rx_rings)
		goto err_rxrings;

	/* allocate memory for q_vector pointers */
	vsi->q_vectors = devm_kcalloc(&pf->pdev->dev, vsi->num_q_vectors,
				      sizeof(*vsi->q_vectors), GFP_KERNEL);
	if (!vsi->q_vectors)
		goto err_vectors;

	return 0;

err_vectors:
	devm_kfree(&pf->pdev->dev, vsi->rx_rings);
err_rxrings:
	devm_kfree(&pf->pdev->dev, vsi->tx_rings);
err_txrings:
	return -ENOMEM;
}

/**
 * ice_vsi_set_num_desc - Set number of descriptors for queues on this VSI
 * @vsi: the VSI being configured
 */
static void ice_vsi_set_num_desc(struct ice_vsi *vsi)
{
	switch (vsi->type) {
	case ICE_VSI_PF:
		vsi->num_rx_desc = ICE_DFLT_NUM_RX_DESC;
		vsi->num_tx_desc = ICE_DFLT_NUM_TX_DESC;
		break;
	default:
		dev_dbg(&vsi->back->pdev->dev,
			"Not setting number of Tx/Rx descriptors for VSI type %d\n",
			vsi->type);
		break;
	}
}

/**
 * ice_vsi_set_num_qs - Set number of queues, descriptors and vectors for a VSI
 * @vsi: the VSI being configured
 * @vf_id: ID of the VF being configured
 *
 * Return 0 on success and a negative value on error
 */
static void ice_vsi_set_num_qs(struct ice_vsi *vsi, u16 vf_id)
{
	struct ice_pf *pf = vsi->back;
	struct ice_vf *vf = NULL;

	if (vsi->type == ICE_VSI_VF)
		vsi->vf_id = vf_id;

	switch (vsi->type) {
	case ICE_VSI_PF:
		vsi->alloc_txq = pf->num_lan_tx;
		vsi->alloc_rxq = pf->num_lan_rx;
		vsi->num_q_vectors = max_t(int, pf->num_lan_rx, pf->num_lan_tx);
		break;
	case ICE_VSI_VF:
		vf = &pf->vf[vsi->vf_id];
		vsi->alloc_txq = vf->num_vf_qs;
		vsi->alloc_rxq = vf->num_vf_qs;
		/* pf->num_vf_msix includes (VF miscellaneous vector +
		 * data queue interrupts). Since vsi->num_q_vectors is number
		 * of queues vectors, subtract 1 from the original vector
		 * count
		 */
		vsi->num_q_vectors = pf->num_vf_msix - 1;
		break;
	default:
		dev_warn(&pf->pdev->dev, "Unknown VSI type %d\n", vsi->type);
		break;
	}

	ice_vsi_set_num_desc(vsi);
}

/**
 * ice_get_free_slot - get the next non-NULL location index in array
 * @array: array to search
 * @size: size of the array
 * @curr: last known occupied index to be used as a search hint
 *
 * void * is being used to keep the functionality generic. This lets us use this
 * function on any array of pointers.
 */
static int ice_get_free_slot(void *array, int size, int curr)
{
	int **tmp_array = (int **)array;
	int next;

	if (curr < (size - 1) && !tmp_array[curr + 1]) {
		next = curr + 1;
	} else {
		int i = 0;

		while ((i < size) && (tmp_array[i]))
			i++;
		if (i == size)
			next = ICE_NO_VSI;
		else
			next = i;
	}
	return next;
}

/**
 * ice_vsi_delete - delete a VSI from the switch
 * @vsi: pointer to VSI being removed
 */
void ice_vsi_delete(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct ice_vsi_ctx *ctxt;
	enum ice_status status;

	ctxt = devm_kzalloc(&pf->pdev->dev, sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return;

	if (vsi->type == ICE_VSI_VF)
		ctxt->vf_num = vsi->vf_id;
	ctxt->vsi_num = vsi->vsi_num;

	memcpy(&ctxt->info, &vsi->info, sizeof(ctxt->info));

	status = ice_free_vsi(&pf->hw, vsi->idx, ctxt, false, NULL);
	if (status)
		dev_err(&pf->pdev->dev, "Failed to delete VSI %i in FW\n",
			vsi->vsi_num);

	devm_kfree(&pf->pdev->dev, ctxt);
}

/**
 * ice_vsi_free_arrays - De-allocate queue and vector pointer arrays for the VSI
 * @vsi: pointer to VSI being cleared
 */
static void ice_vsi_free_arrays(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;

	/* free the ring and vector containers */
	if (vsi->q_vectors) {
		devm_kfree(&pf->pdev->dev, vsi->q_vectors);
		vsi->q_vectors = NULL;
	}
	if (vsi->tx_rings) {
		devm_kfree(&pf->pdev->dev, vsi->tx_rings);
		vsi->tx_rings = NULL;
	}
	if (vsi->rx_rings) {
		devm_kfree(&pf->pdev->dev, vsi->rx_rings);
		vsi->rx_rings = NULL;
	}
}

/**
 * ice_vsi_clear - clean up and deallocate the provided VSI
 * @vsi: pointer to VSI being cleared
 *
 * This deallocates the VSI's queue resources, removes it from the PF's
 * VSI array if necessary, and deallocates the VSI
 *
 * Returns 0 on success, negative on failure
 */
int ice_vsi_clear(struct ice_vsi *vsi)
{
	struct ice_pf *pf = NULL;

	if (!vsi)
		return 0;

	if (!vsi->back)
		return -EINVAL;

	pf = vsi->back;

	if (!pf->vsi[vsi->idx] || pf->vsi[vsi->idx] != vsi) {
		dev_dbg(&pf->pdev->dev, "vsi does not exist at pf->vsi[%d]\n",
			vsi->idx);
		return -EINVAL;
	}

	mutex_lock(&pf->sw_mutex);
	/* updates the PF for this cleared VSI */

	pf->vsi[vsi->idx] = NULL;
	if (vsi->idx < pf->next_vsi)
		pf->next_vsi = vsi->idx;

	ice_vsi_free_arrays(vsi);
	mutex_unlock(&pf->sw_mutex);
	devm_kfree(&pf->pdev->dev, vsi);

	return 0;
}

/**
 * ice_msix_clean_rings - MSIX mode Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 */
static irqreturn_t ice_msix_clean_rings(int __always_unused irq, void *data)
{
	struct ice_q_vector *q_vector = (struct ice_q_vector *)data;

	if (!q_vector->tx.ring && !q_vector->rx.ring)
		return IRQ_HANDLED;

	napi_schedule(&q_vector->napi);

	return IRQ_HANDLED;
}

/**
 * ice_vsi_alloc - Allocates the next available struct VSI in the PF
 * @pf: board private structure
 * @type: type of VSI
 * @vf_id: ID of the VF being configured
 *
 * returns a pointer to a VSI on success, NULL on failure.
 */
static struct ice_vsi *
ice_vsi_alloc(struct ice_pf *pf, enum ice_vsi_type type, u16 vf_id)
{
	struct ice_vsi *vsi = NULL;

	/* Need to protect the allocation of the VSIs at the PF level */
	mutex_lock(&pf->sw_mutex);

	/* If we have already allocated our maximum number of VSIs,
	 * pf->next_vsi will be ICE_NO_VSI. If not, pf->next_vsi index
	 * is available to be populated
	 */
	if (pf->next_vsi == ICE_NO_VSI) {
		dev_dbg(&pf->pdev->dev, "out of VSI slots!\n");
		goto unlock_pf;
	}

	vsi = devm_kzalloc(&pf->pdev->dev, sizeof(*vsi), GFP_KERNEL);
	if (!vsi)
		goto unlock_pf;

	vsi->type = type;
	vsi->back = pf;
	set_bit(__ICE_DOWN, vsi->state);
	vsi->idx = pf->next_vsi;
	vsi->work_lmt = ICE_DFLT_IRQ_WORK;

	if (type == ICE_VSI_VF)
		ice_vsi_set_num_qs(vsi, vf_id);
	else
		ice_vsi_set_num_qs(vsi, ICE_INVAL_VFID);

	switch (vsi->type) {
	case ICE_VSI_PF:
		if (ice_vsi_alloc_arrays(vsi))
			goto err_rings;

		/* Setup default MSIX irq handler for VSI */
		vsi->irq_handler = ice_msix_clean_rings;
		break;
	case ICE_VSI_VF:
		if (ice_vsi_alloc_arrays(vsi))
			goto err_rings;
		break;
	default:
		dev_warn(&pf->pdev->dev, "Unknown VSI type %d\n", vsi->type);
		goto unlock_pf;
	}

	/* fill VSI slot in the PF struct */
	pf->vsi[pf->next_vsi] = vsi;

	/* prepare pf->next_vsi for next use */
	pf->next_vsi = ice_get_free_slot(pf->vsi, pf->num_alloc_vsi,
					 pf->next_vsi);
	goto unlock_pf;

err_rings:
	devm_kfree(&pf->pdev->dev, vsi);
	vsi = NULL;
unlock_pf:
	mutex_unlock(&pf->sw_mutex);
	return vsi;
}

/**
 * __ice_vsi_get_qs_contig - Assign a contiguous chunk of queues to VSI
 * @qs_cfg: gathered variables needed for PF->VSI queues assignment
 *
 * Return 0 on success and -ENOMEM in case of no left space in PF queue bitmap
 */
static int __ice_vsi_get_qs_contig(struct ice_qs_cfg *qs_cfg)
{
	int offset, i;

	mutex_lock(qs_cfg->qs_mutex);
	offset = bitmap_find_next_zero_area(qs_cfg->pf_map, qs_cfg->pf_map_size,
					    0, qs_cfg->q_count, 0);
	if (offset >= qs_cfg->pf_map_size) {
		mutex_unlock(qs_cfg->qs_mutex);
		return -ENOMEM;
	}

	bitmap_set(qs_cfg->pf_map, offset, qs_cfg->q_count);
	for (i = 0; i < qs_cfg->q_count; i++)
		qs_cfg->vsi_map[i + qs_cfg->vsi_map_offset] = i + offset;
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
	int i, index = 0;

	mutex_lock(qs_cfg->qs_mutex);
	for (i = 0; i < qs_cfg->q_count; i++) {
		index = find_next_zero_bit(qs_cfg->pf_map,
					   qs_cfg->pf_map_size, index);
		if (index >= qs_cfg->pf_map_size)
			goto err_scatter;
		set_bit(index, qs_cfg->pf_map);
		qs_cfg->vsi_map[i + qs_cfg->vsi_map_offset] = index;
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
 * __ice_vsi_get_qs - helper function for assigning queues from PF to VSI
 * @qs_cfg: gathered variables needed for pf->vsi queues assignment
 *
 * This function first tries to find contiguous space. If it is not successful,
 * it tries with the scatter approach.
 *
 * Return 0 on success and -ENOMEM in case of no left space in PF queue bitmap
 */
static int __ice_vsi_get_qs(struct ice_qs_cfg *qs_cfg)
{
	int ret = 0;

	ret = __ice_vsi_get_qs_contig(qs_cfg);
	if (ret) {
		/* contig failed, so try with scatter approach */
		qs_cfg->mapping_mode = ICE_VSI_MAP_SCATTER;
		qs_cfg->q_count = min_t(u16, qs_cfg->q_count,
					qs_cfg->scatter_count);
		ret = __ice_vsi_get_qs_sc(qs_cfg);
	}
	return ret;
}

/**
 * ice_vsi_get_qs - Assign queues from PF to VSI
 * @vsi: the VSI to assign queues to
 *
 * Returns 0 on success and a negative value on error
 */
static int ice_vsi_get_qs(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct ice_qs_cfg tx_qs_cfg = {
		.qs_mutex = &pf->avail_q_mutex,
		.pf_map = pf->avail_txqs,
		.pf_map_size = ICE_MAX_TXQS,
		.q_count = vsi->alloc_txq,
		.scatter_count = ICE_MAX_SCATTER_TXQS,
		.vsi_map = vsi->txq_map,
		.vsi_map_offset = 0,
		.mapping_mode = vsi->tx_mapping_mode
	};
	struct ice_qs_cfg rx_qs_cfg = {
		.qs_mutex = &pf->avail_q_mutex,
		.pf_map = pf->avail_rxqs,
		.pf_map_size = ICE_MAX_RXQS,
		.q_count = vsi->alloc_rxq,
		.scatter_count = ICE_MAX_SCATTER_RXQS,
		.vsi_map = vsi->rxq_map,
		.vsi_map_offset = 0,
		.mapping_mode = vsi->rx_mapping_mode
	};
	int ret = 0;

	vsi->tx_mapping_mode = ICE_VSI_MAP_CONTIG;
	vsi->rx_mapping_mode = ICE_VSI_MAP_CONTIG;

	ret = __ice_vsi_get_qs(&tx_qs_cfg);
	if (!ret)
		ret = __ice_vsi_get_qs(&rx_qs_cfg);

	return ret;
}

/**
 * ice_vsi_put_qs - Release queues from VSI to PF
 * @vsi: the VSI that is going to release queues
 */
void ice_vsi_put_qs(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	int i;

	mutex_lock(&pf->avail_q_mutex);

	for (i = 0; i < vsi->alloc_txq; i++) {
		clear_bit(vsi->txq_map[i], pf->avail_txqs);
		vsi->txq_map[i] = ICE_INVAL_Q_INDEX;
	}

	for (i = 0; i < vsi->alloc_rxq; i++) {
		clear_bit(vsi->rxq_map[i], pf->avail_rxqs);
		vsi->rxq_map[i] = ICE_INVAL_Q_INDEX;
	}

	mutex_unlock(&pf->avail_q_mutex);
}

/**
 * ice_rss_clean - Delete RSS related VSI structures that hold user inputs
 * @vsi: the VSI being removed
 */
static void ice_rss_clean(struct ice_vsi *vsi)
{
	struct ice_pf *pf;

	pf = vsi->back;

	if (vsi->rss_hkey_user)
		devm_kfree(&pf->pdev->dev, vsi->rss_hkey_user);
	if (vsi->rss_lut_user)
		devm_kfree(&pf->pdev->dev, vsi->rss_lut_user);
}

/**
 * ice_vsi_set_rss_params - Setup RSS capabilities per VSI type
 * @vsi: the VSI being configured
 */
static void ice_vsi_set_rss_params(struct ice_vsi *vsi)
{
	struct ice_hw_common_caps *cap;
	struct ice_pf *pf = vsi->back;

	if (!test_bit(ICE_FLAG_RSS_ENA, pf->flags)) {
		vsi->rss_size = 1;
		return;
	}

	cap = &pf->hw.func_caps.common_cap;
	switch (vsi->type) {
	case ICE_VSI_PF:
		/* PF VSI will inherit RSS instance of PF */
		vsi->rss_table_size = cap->rss_table_size;
		vsi->rss_size = min_t(int, num_online_cpus(),
				      BIT(cap->rss_table_entry_width));
		vsi->rss_lut_type = ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_PF;
		break;
	case ICE_VSI_VF:
		/* VF VSI will gets a small RSS table
		 * For VSI_LUT, LUT size should be set to 64 bytes
		 */
		vsi->rss_table_size = ICE_VSIQF_HLUT_ARRAY_SIZE;
		vsi->rss_size = min_t(int, num_online_cpus(),
				      BIT(cap->rss_table_entry_width));
		vsi->rss_lut_type = ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_VSI;
		break;
	default:
		dev_warn(&pf->pdev->dev, "Unknown VSI type %d\n",
			 vsi->type);
		break;
	}
}

/**
 * ice_set_dflt_vsi_ctx - Set default VSI context before adding a VSI
 * @ctxt: the VSI context being set
 *
 * This initializes a default VSI context for all sections except the Queues.
 */
static void ice_set_dflt_vsi_ctx(struct ice_vsi_ctx *ctxt)
{
	u32 table = 0;

	memset(&ctxt->info, 0, sizeof(ctxt->info));
	/* VSI's should be allocated from shared pool */
	ctxt->alloc_from_pool = true;
	/* Src pruning enabled by default */
	ctxt->info.sw_flags = ICE_AQ_VSI_SW_FLAG_SRC_PRUNE;
	/* Traffic from VSI can be sent to LAN */
	ctxt->info.sw_flags2 = ICE_AQ_VSI_SW_FLAG_LAN_ENA;
	/* By default bits 3 and 4 in vlan_flags are 0's which results in legacy
	 * behavior (show VLAN, DEI, and UP) in descriptor. Also, allow all
	 * packets untagged/tagged.
	 */
	ctxt->info.vlan_flags = ((ICE_AQ_VSI_VLAN_MODE_ALL &
				  ICE_AQ_VSI_VLAN_MODE_M) >>
				 ICE_AQ_VSI_VLAN_MODE_S);
	/* Have 1:1 UP mapping for both ingress/egress tables */
	table |= ICE_UP_TABLE_TRANSLATE(0, 0);
	table |= ICE_UP_TABLE_TRANSLATE(1, 1);
	table |= ICE_UP_TABLE_TRANSLATE(2, 2);
	table |= ICE_UP_TABLE_TRANSLATE(3, 3);
	table |= ICE_UP_TABLE_TRANSLATE(4, 4);
	table |= ICE_UP_TABLE_TRANSLATE(5, 5);
	table |= ICE_UP_TABLE_TRANSLATE(6, 6);
	table |= ICE_UP_TABLE_TRANSLATE(7, 7);
	ctxt->info.ingress_table = cpu_to_le32(table);
	ctxt->info.egress_table = cpu_to_le32(table);
	/* Have 1:1 UP mapping for outer to inner UP table */
	ctxt->info.outer_up_table = cpu_to_le32(table);
	/* No Outer tag support outer_tag_flags remains to zero */
}

/**
 * ice_vsi_setup_q_map - Setup a VSI queue map
 * @vsi: the VSI being configured
 * @ctxt: VSI context structure
 */
static void ice_vsi_setup_q_map(struct ice_vsi *vsi, struct ice_vsi_ctx *ctxt)
{
	u16 offset = 0, qmap = 0, tx_count = 0;
	u16 qcount_tx = vsi->alloc_txq;
	u16 qcount_rx = vsi->alloc_rxq;
	u16 tx_numq_tc, rx_numq_tc;
	u16 pow = 0, max_rss = 0;
	bool ena_tc0 = false;
	u8 netdev_tc = 0;
	int i;

	/* at least TC0 should be enabled by default */
	if (vsi->tc_cfg.numtc) {
		if (!(vsi->tc_cfg.ena_tc & BIT(0)))
			ena_tc0 = true;
	} else {
		ena_tc0 = true;
	}

	if (ena_tc0) {
		vsi->tc_cfg.numtc++;
		vsi->tc_cfg.ena_tc |= 1;
	}

	rx_numq_tc = qcount_rx / vsi->tc_cfg.numtc;
	if (!rx_numq_tc)
		rx_numq_tc = 1;
	tx_numq_tc = qcount_tx / vsi->tc_cfg.numtc;
	if (!tx_numq_tc)
		tx_numq_tc = 1;

	/* TC mapping is a function of the number of Rx queues assigned to the
	 * VSI for each traffic class and the offset of these queues.
	 * The first 10 bits are for queue offset for TC0, next 4 bits for no:of
	 * queues allocated to TC0. No:of queues is a power-of-2.
	 *
	 * If TC is not enabled, the queue offset is set to 0, and allocate one
	 * queue, this way, traffic for the given TC will be sent to the default
	 * queue.
	 *
	 * Setup number and offset of Rx queues for all TCs for the VSI
	 */

	qcount_rx = rx_numq_tc;

	/* qcount will change if RSS is enabled */
	if (test_bit(ICE_FLAG_RSS_ENA, vsi->back->flags)) {
		if (vsi->type == ICE_VSI_PF || vsi->type == ICE_VSI_VF) {
			if (vsi->type == ICE_VSI_PF)
				max_rss = ICE_MAX_LG_RSS_QS;
			else
				max_rss = ICE_MAX_SMALL_RSS_QS;
			qcount_rx = min_t(int, rx_numq_tc, max_rss);
			qcount_rx = min_t(int, qcount_rx, vsi->rss_size);
		}
	}

	/* find the (rounded up) power-of-2 of qcount */
	pow = order_base_2(qcount_rx);

	ice_for_each_traffic_class(i) {
		if (!(vsi->tc_cfg.ena_tc & BIT(i))) {
			/* TC is not enabled */
			vsi->tc_cfg.tc_info[i].qoffset = 0;
			vsi->tc_cfg.tc_info[i].qcount_rx = 1;
			vsi->tc_cfg.tc_info[i].qcount_tx = 1;
			vsi->tc_cfg.tc_info[i].netdev_tc = 0;
			ctxt->info.tc_mapping[i] = 0;
			continue;
		}

		/* TC is enabled */
		vsi->tc_cfg.tc_info[i].qoffset = offset;
		vsi->tc_cfg.tc_info[i].qcount_rx = qcount_rx;
		vsi->tc_cfg.tc_info[i].qcount_tx = tx_numq_tc;
		vsi->tc_cfg.tc_info[i].netdev_tc = netdev_tc++;

		qmap = ((offset << ICE_AQ_VSI_TC_Q_OFFSET_S) &
			ICE_AQ_VSI_TC_Q_OFFSET_M) |
			((pow << ICE_AQ_VSI_TC_Q_NUM_S) &
			 ICE_AQ_VSI_TC_Q_NUM_M);
		offset += qcount_rx;
		tx_count += tx_numq_tc;
		ctxt->info.tc_mapping[i] = cpu_to_le16(qmap);
	}

	/* if offset is non-zero, means it is calculated correctly based on
	 * enabled TCs for a given VSI otherwise qcount_rx will always
	 * be correct and non-zero because it is based off - VSI's
	 * allocated Rx queues which is at least 1 (hence qcount_tx will be
	 * at least 1)
	 */
	if (offset)
		vsi->num_rxq = offset;
	else
		vsi->num_rxq = qcount_rx;

	vsi->num_txq = tx_count;

	if (vsi->type == ICE_VSI_VF && vsi->num_txq != vsi->num_rxq) {
		dev_dbg(&vsi->back->pdev->dev, "VF VSI should have same number of Tx and Rx queues. Hence making them equal\n");
		/* since there is a chance that num_rxq could have been changed
		 * in the above for loop, make num_txq equal to num_rxq.
		 */
		vsi->num_txq = vsi->num_rxq;
	}

	/* Rx queue mapping */
	ctxt->info.mapping_flags |= cpu_to_le16(ICE_AQ_VSI_Q_MAP_CONTIG);
	/* q_mapping buffer holds the info for the first queue allocated for
	 * this VSI in the PF space and also the number of queues associated
	 * with this VSI.
	 */
	ctxt->info.q_mapping[0] = cpu_to_le16(vsi->rxq_map[0]);
	ctxt->info.q_mapping[1] = cpu_to_le16(vsi->num_rxq);
}

/**
 * ice_set_rss_vsi_ctx - Set RSS VSI context before adding a VSI
 * @ctxt: the VSI context being set
 * @vsi: the VSI being configured
 */
static void ice_set_rss_vsi_ctx(struct ice_vsi_ctx *ctxt, struct ice_vsi *vsi)
{
	u8 lut_type, hash_type;
	struct ice_pf *pf;

	pf = vsi->back;

	switch (vsi->type) {
	case ICE_VSI_PF:
		/* PF VSI will inherit RSS instance of PF */
		lut_type = ICE_AQ_VSI_Q_OPT_RSS_LUT_PF;
		hash_type = ICE_AQ_VSI_Q_OPT_RSS_TPLZ;
		break;
	case ICE_VSI_VF:
		/* VF VSI will gets a small RSS table which is a VSI LUT type */
		lut_type = ICE_AQ_VSI_Q_OPT_RSS_LUT_VSI;
		hash_type = ICE_AQ_VSI_Q_OPT_RSS_TPLZ;
		break;
	default:
		dev_warn(&pf->pdev->dev, "Unknown VSI type %d\n", vsi->type);
		return;
	}

	ctxt->info.q_opt_rss = ((lut_type << ICE_AQ_VSI_Q_OPT_RSS_LUT_S) &
				ICE_AQ_VSI_Q_OPT_RSS_LUT_M) |
				((hash_type << ICE_AQ_VSI_Q_OPT_RSS_HASH_S) &
				 ICE_AQ_VSI_Q_OPT_RSS_HASH_M);
}

/**
 * ice_vsi_init - Create and initialize a VSI
 * @vsi: the VSI being configured
 *
 * This initializes a VSI context depending on the VSI type to be added and
 * passes it down to the add_vsi aq command to create a new VSI.
 */
static int ice_vsi_init(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	struct ice_vsi_ctx *ctxt;
	int ret = 0;

	ctxt = devm_kzalloc(&pf->pdev->dev, sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	ctxt->info = vsi->info;
	switch (vsi->type) {
	case ICE_VSI_PF:
		ctxt->flags = ICE_AQ_VSI_TYPE_PF;
		break;
	case ICE_VSI_VF:
		ctxt->flags = ICE_AQ_VSI_TYPE_VF;
		/* VF number here is the absolute VF number (0-255) */
		ctxt->vf_num = vsi->vf_id + hw->func_caps.vf_base_id;
		break;
	default:
		return -ENODEV;
	}

	ice_set_dflt_vsi_ctx(ctxt);
	/* if the switch is in VEB mode, allow VSI loopback */
	if (vsi->vsw->bridge_mode == BRIDGE_MODE_VEB)
		ctxt->info.sw_flags |= ICE_AQ_VSI_SW_FLAG_ALLOW_LB;

	/* Set LUT type and HASH type if RSS is enabled */
	if (test_bit(ICE_FLAG_RSS_ENA, pf->flags))
		ice_set_rss_vsi_ctx(ctxt, vsi);

	ctxt->info.sw_id = vsi->port_info->sw_id;
	ice_vsi_setup_q_map(vsi, ctxt);

	/* Enable MAC Antispoof with new VSI being initialized or updated */
	if (vsi->type == ICE_VSI_VF && pf->vf[vsi->vf_id].spoofchk) {
		ctxt->info.valid_sections |=
			cpu_to_le16(ICE_AQ_VSI_PROP_SECURITY_VALID);
		ctxt->info.sec_flags |=
			ICE_AQ_VSI_SEC_FLAG_ENA_MAC_ANTI_SPOOF;
	}

	ret = ice_add_vsi(hw, vsi->idx, ctxt, NULL);
	if (ret) {
		dev_err(&pf->pdev->dev,
			"Add VSI failed, err %d\n", ret);
		return -EIO;
	}

	/* keep context for update VSI operations */
	vsi->info = ctxt->info;

	/* record VSI number returned */
	vsi->vsi_num = ctxt->vsi_num;

	devm_kfree(&pf->pdev->dev, ctxt);
	return ret;
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
	struct ice_ring *ring;

	if (!vsi->q_vectors[v_idx]) {
		dev_dbg(&pf->pdev->dev, "Queue vector at index %d not found\n",
			v_idx);
		return;
	}
	q_vector = vsi->q_vectors[v_idx];

	ice_for_each_ring(ring, q_vector->tx)
		ring->q_vector = NULL;
	ice_for_each_ring(ring, q_vector->rx)
		ring->q_vector = NULL;

	/* only VSI with an associated netdev is set up with NAPI */
	if (vsi->netdev)
		netif_napi_del(&q_vector->napi);

	devm_kfree(&pf->pdev->dev, q_vector);
	vsi->q_vectors[v_idx] = NULL;
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
 * ice_vsi_alloc_q_vector - Allocate memory for a single interrupt vector
 * @vsi: the VSI being configured
 * @v_idx: index of the vector in the VSI struct
 *
 * We allocate one q_vector. If allocation fails we return -ENOMEM.
 */
static int ice_vsi_alloc_q_vector(struct ice_vsi *vsi, int v_idx)
{
	struct ice_pf *pf = vsi->back;
	struct ice_q_vector *q_vector;

	/* allocate q_vector */
	q_vector = devm_kzalloc(&pf->pdev->dev, sizeof(*q_vector), GFP_KERNEL);
	if (!q_vector)
		return -ENOMEM;

	q_vector->vsi = vsi;
	q_vector->v_idx = v_idx;
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
 * ice_vsi_alloc_q_vectors - Allocate memory for interrupt vectors
 * @vsi: the VSI being configured
 *
 * We allocate one q_vector per queue interrupt. If allocation fails we
 * return -ENOMEM.
 */
static int ice_vsi_alloc_q_vectors(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	int v_idx = 0, num_q_vectors;
	int err;

	if (vsi->q_vectors[0]) {
		dev_dbg(&pf->pdev->dev, "VSI %d has existing q_vectors\n",
			vsi->vsi_num);
		return -EEXIST;
	}

	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags)) {
		num_q_vectors = vsi->num_q_vectors;
	} else {
		err = -EINVAL;
		goto err_out;
	}

	for (v_idx = 0; v_idx < num_q_vectors; v_idx++) {
		err = ice_vsi_alloc_q_vector(vsi, v_idx);
		if (err)
			goto err_out;
	}

	return 0;

err_out:
	while (v_idx--)
		ice_free_q_vector(vsi, v_idx);

	dev_err(&pf->pdev->dev,
		"Failed to allocate %d q_vector for VSI %d, ret=%d\n",
		vsi->num_q_vectors, vsi->vsi_num, err);
	vsi->num_q_vectors = 0;
	return err;
}

/**
 * ice_vsi_setup_vector_base - Set up the base vector for the given VSI
 * @vsi: ptr to the VSI
 *
 * This should only be called after ice_vsi_alloc() which allocates the
 * corresponding SW VSI structure and initializes num_queue_pairs for the
 * newly allocated VSI.
 *
 * Returns 0 on success or negative on failure
 */
static int ice_vsi_setup_vector_base(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	int num_q_vectors = 0;

	if (vsi->sw_base_vector || vsi->hw_base_vector) {
		dev_dbg(&pf->pdev->dev, "VSI %d has non-zero HW base vector %d or SW base vector %d\n",
			vsi->vsi_num, vsi->hw_base_vector, vsi->sw_base_vector);
		return -EEXIST;
	}

	if (!test_bit(ICE_FLAG_MSIX_ENA, pf->flags))
		return -ENOENT;

	switch (vsi->type) {
	case ICE_VSI_PF:
		num_q_vectors = vsi->num_q_vectors;
		/* reserve slots from OS requested IRQs */
		vsi->sw_base_vector = ice_get_res(pf, pf->sw_irq_tracker,
						  num_q_vectors, vsi->idx);
		if (vsi->sw_base_vector < 0) {
			dev_err(&pf->pdev->dev,
				"Failed to get tracking for %d SW vectors for VSI %d, err=%d\n",
				num_q_vectors, vsi->vsi_num,
				vsi->sw_base_vector);
			return -ENOENT;
		}
		pf->num_avail_sw_msix -= num_q_vectors;

		/* reserve slots from HW interrupts */
		vsi->hw_base_vector = ice_get_res(pf, pf->hw_irq_tracker,
						  num_q_vectors, vsi->idx);
		break;
	case ICE_VSI_VF:
		/* take VF misc vector and data vectors into account */
		num_q_vectors = pf->num_vf_msix;
		/* For VF VSI, reserve slots only from HW interrupts */
		vsi->hw_base_vector = ice_get_res(pf, pf->hw_irq_tracker,
						  num_q_vectors, vsi->idx);
		break;
	default:
		dev_warn(&pf->pdev->dev, "Unknown VSI type %d\n", vsi->type);
		break;
	}

	if (vsi->hw_base_vector < 0) {
		dev_err(&pf->pdev->dev,
			"Failed to get tracking for %d HW vectors for VSI %d, err=%d\n",
			num_q_vectors, vsi->vsi_num, vsi->hw_base_vector);
		if (vsi->type != ICE_VSI_VF) {
			ice_free_res(pf->sw_irq_tracker,
				     vsi->sw_base_vector, vsi->idx);
			pf->num_avail_sw_msix += num_q_vectors;
		}
		return -ENOENT;
	}

	pf->num_avail_hw_msix -= num_q_vectors;

	return 0;
}

/**
 * ice_vsi_clear_rings - Deallocates the Tx and Rx rings for VSI
 * @vsi: the VSI having rings deallocated
 */
static void ice_vsi_clear_rings(struct ice_vsi *vsi)
{
	int i;

	if (vsi->tx_rings) {
		for (i = 0; i < vsi->alloc_txq; i++) {
			if (vsi->tx_rings[i]) {
				kfree_rcu(vsi->tx_rings[i], rcu);
				vsi->tx_rings[i] = NULL;
			}
		}
	}
	if (vsi->rx_rings) {
		for (i = 0; i < vsi->alloc_rxq; i++) {
			if (vsi->rx_rings[i]) {
				kfree_rcu(vsi->rx_rings[i], rcu);
				vsi->rx_rings[i] = NULL;
			}
		}
	}
}

/**
 * ice_vsi_alloc_rings - Allocates Tx and Rx rings for the VSI
 * @vsi: VSI which is having rings allocated
 */
static int ice_vsi_alloc_rings(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	int i;

	/* Allocate Tx rings */
	for (i = 0; i < vsi->alloc_txq; i++) {
		struct ice_ring *ring;

		/* allocate with kzalloc(), free with kfree_rcu() */
		ring = kzalloc(sizeof(*ring), GFP_KERNEL);

		if (!ring)
			goto err_out;

		ring->q_index = i;
		ring->reg_idx = vsi->txq_map[i];
		ring->ring_active = false;
		ring->vsi = vsi;
		ring->dev = &pf->pdev->dev;
		ring->count = vsi->num_tx_desc;
		vsi->tx_rings[i] = ring;
	}

	/* Allocate Rx rings */
	for (i = 0; i < vsi->alloc_rxq; i++) {
		struct ice_ring *ring;

		/* allocate with kzalloc(), free with kfree_rcu() */
		ring = kzalloc(sizeof(*ring), GFP_KERNEL);
		if (!ring)
			goto err_out;

		ring->q_index = i;
		ring->reg_idx = vsi->rxq_map[i];
		ring->ring_active = false;
		ring->vsi = vsi;
		ring->netdev = vsi->netdev;
		ring->dev = &pf->pdev->dev;
		ring->count = vsi->num_rx_desc;
		vsi->rx_rings[i] = ring;
	}

	return 0;

err_out:
	ice_vsi_clear_rings(vsi);
	return -ENOMEM;
}

/**
 * ice_vsi_map_rings_to_vectors - Map VSI rings to interrupt vectors
 * @vsi: the VSI being configured
 *
 * This function maps descriptor rings to the queue-specific vectors allotted
 * through the MSI-X enabling code. On a constrained vector budget, we map Tx
 * and Rx rings to the vector as "efficiently" as possible.
 */
#ifdef CONFIG_DCB
void ice_vsi_map_rings_to_vectors(struct ice_vsi *vsi)
#else
static void ice_vsi_map_rings_to_vectors(struct ice_vsi *vsi)
#endif /* CONFIG_DCB */
{
	int q_vectors = vsi->num_q_vectors;
	int tx_rings_rem, rx_rings_rem;
	int v_id;

	/* initially assigning remaining rings count to VSIs num queue value */
	tx_rings_rem = vsi->num_txq;
	rx_rings_rem = vsi->num_rxq;

	for (v_id = 0; v_id < q_vectors; v_id++) {
		struct ice_q_vector *q_vector = vsi->q_vectors[v_id];
		int tx_rings_per_v, rx_rings_per_v, q_id, q_base;

		/* Tx rings mapping to vector */
		tx_rings_per_v = DIV_ROUND_UP(tx_rings_rem, q_vectors - v_id);
		q_vector->num_ring_tx = tx_rings_per_v;
		q_vector->tx.ring = NULL;
		q_vector->tx.itr_idx = ICE_TX_ITR;
		q_base = vsi->num_txq - tx_rings_rem;

		for (q_id = q_base; q_id < (q_base + tx_rings_per_v); q_id++) {
			struct ice_ring *tx_ring = vsi->tx_rings[q_id];

			tx_ring->q_vector = q_vector;
			tx_ring->next = q_vector->tx.ring;
			q_vector->tx.ring = tx_ring;
		}
		tx_rings_rem -= tx_rings_per_v;

		/* Rx rings mapping to vector */
		rx_rings_per_v = DIV_ROUND_UP(rx_rings_rem, q_vectors - v_id);
		q_vector->num_ring_rx = rx_rings_per_v;
		q_vector->rx.ring = NULL;
		q_vector->rx.itr_idx = ICE_RX_ITR;
		q_base = vsi->num_rxq - rx_rings_rem;

		for (q_id = q_base; q_id < (q_base + rx_rings_per_v); q_id++) {
			struct ice_ring *rx_ring = vsi->rx_rings[q_id];

			rx_ring->q_vector = q_vector;
			rx_ring->next = q_vector->rx.ring;
			q_vector->rx.ring = rx_ring;
		}
		rx_rings_rem -= rx_rings_per_v;
	}
}

/**
 * ice_vsi_manage_rss_lut - disable/enable RSS
 * @vsi: the VSI being changed
 * @ena: boolean value indicating if this is an enable or disable request
 *
 * In the event of disable request for RSS, this function will zero out RSS
 * LUT, while in the event of enable request for RSS, it will reconfigure RSS
 * LUT.
 */
int ice_vsi_manage_rss_lut(struct ice_vsi *vsi, bool ena)
{
	int err = 0;
	u8 *lut;

	lut = devm_kzalloc(&vsi->back->pdev->dev, vsi->rss_table_size,
			   GFP_KERNEL);
	if (!lut)
		return -ENOMEM;

	if (ena) {
		if (vsi->rss_lut_user)
			memcpy(lut, vsi->rss_lut_user, vsi->rss_table_size);
		else
			ice_fill_rss_lut(lut, vsi->rss_table_size,
					 vsi->rss_size);
	}

	err = ice_set_rss(vsi, NULL, lut, vsi->rss_table_size);
	devm_kfree(&vsi->back->pdev->dev, lut);
	return err;
}

/**
 * ice_vsi_cfg_rss_lut_key - Configure RSS params for a VSI
 * @vsi: VSI to be configured
 */
static int ice_vsi_cfg_rss_lut_key(struct ice_vsi *vsi)
{
	struct ice_aqc_get_set_rss_keys *key;
	struct ice_pf *pf = vsi->back;
	enum ice_status status;
	int err = 0;
	u8 *lut;

	vsi->rss_size = min_t(int, vsi->rss_size, vsi->num_rxq);

	lut = devm_kzalloc(&pf->pdev->dev, vsi->rss_table_size, GFP_KERNEL);
	if (!lut)
		return -ENOMEM;

	if (vsi->rss_lut_user)
		memcpy(lut, vsi->rss_lut_user, vsi->rss_table_size);
	else
		ice_fill_rss_lut(lut, vsi->rss_table_size, vsi->rss_size);

	status = ice_aq_set_rss_lut(&pf->hw, vsi->idx, vsi->rss_lut_type, lut,
				    vsi->rss_table_size);

	if (status) {
		dev_err(&pf->pdev->dev,
			"set_rss_lut failed, error %d\n", status);
		err = -EIO;
		goto ice_vsi_cfg_rss_exit;
	}

	key = devm_kzalloc(&pf->pdev->dev, sizeof(*key), GFP_KERNEL);
	if (!key) {
		err = -ENOMEM;
		goto ice_vsi_cfg_rss_exit;
	}

	if (vsi->rss_hkey_user)
		memcpy(key,
		       (struct ice_aqc_get_set_rss_keys *)vsi->rss_hkey_user,
		       ICE_GET_SET_RSS_KEY_EXTEND_KEY_SIZE);
	else
		netdev_rss_key_fill((void *)key,
				    ICE_GET_SET_RSS_KEY_EXTEND_KEY_SIZE);

	status = ice_aq_set_rss_key(&pf->hw, vsi->idx, key);

	if (status) {
		dev_err(&pf->pdev->dev, "set_rss_key failed, error %d\n",
			status);
		err = -EIO;
	}

	devm_kfree(&pf->pdev->dev, key);
ice_vsi_cfg_rss_exit:
	devm_kfree(&pf->pdev->dev, lut);
	return err;
}

/**
 * ice_add_mac_to_list - Add a MAC address filter entry to the list
 * @vsi: the VSI to be forwarded to
 * @add_list: pointer to the list which contains MAC filter entries
 * @macaddr: the MAC address to be added.
 *
 * Adds MAC address filter entry to the temp list
 *
 * Returns 0 on success or ENOMEM on failure.
 */
int ice_add_mac_to_list(struct ice_vsi *vsi, struct list_head *add_list,
			const u8 *macaddr)
{
	struct ice_fltr_list_entry *tmp;
	struct ice_pf *pf = vsi->back;

	tmp = devm_kzalloc(&pf->pdev->dev, sizeof(*tmp), GFP_ATOMIC);
	if (!tmp)
		return -ENOMEM;

	tmp->fltr_info.flag = ICE_FLTR_TX;
	tmp->fltr_info.src_id = ICE_SRC_ID_VSI;
	tmp->fltr_info.lkup_type = ICE_SW_LKUP_MAC;
	tmp->fltr_info.fltr_act = ICE_FWD_TO_VSI;
	tmp->fltr_info.vsi_handle = vsi->idx;
	ether_addr_copy(tmp->fltr_info.l_data.mac.mac_addr, macaddr);

	INIT_LIST_HEAD(&tmp->list_entry);
	list_add(&tmp->list_entry, add_list);

	return 0;
}

/**
 * ice_update_eth_stats - Update VSI-specific ethernet statistics counters
 * @vsi: the VSI to be updated
 */
void ice_update_eth_stats(struct ice_vsi *vsi)
{
	struct ice_eth_stats *prev_es, *cur_es;
	struct ice_hw *hw = &vsi->back->hw;
	u16 vsi_num = vsi->vsi_num;    /* HW absolute index of a VSI */

	prev_es = &vsi->eth_stats_prev;
	cur_es = &vsi->eth_stats;

	ice_stat_update40(hw, GLV_GORCH(vsi_num), GLV_GORCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->rx_bytes,
			  &cur_es->rx_bytes);

	ice_stat_update40(hw, GLV_UPRCH(vsi_num), GLV_UPRCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->rx_unicast,
			  &cur_es->rx_unicast);

	ice_stat_update40(hw, GLV_MPRCH(vsi_num), GLV_MPRCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->rx_multicast,
			  &cur_es->rx_multicast);

	ice_stat_update40(hw, GLV_BPRCH(vsi_num), GLV_BPRCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->rx_broadcast,
			  &cur_es->rx_broadcast);

	ice_stat_update32(hw, GLV_RDPC(vsi_num), vsi->stat_offsets_loaded,
			  &prev_es->rx_discards, &cur_es->rx_discards);

	ice_stat_update40(hw, GLV_GOTCH(vsi_num), GLV_GOTCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->tx_bytes,
			  &cur_es->tx_bytes);

	ice_stat_update40(hw, GLV_UPTCH(vsi_num), GLV_UPTCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->tx_unicast,
			  &cur_es->tx_unicast);

	ice_stat_update40(hw, GLV_MPTCH(vsi_num), GLV_MPTCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->tx_multicast,
			  &cur_es->tx_multicast);

	ice_stat_update40(hw, GLV_BPTCH(vsi_num), GLV_BPTCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->tx_broadcast,
			  &cur_es->tx_broadcast);

	ice_stat_update32(hw, GLV_TEPC(vsi_num), vsi->stat_offsets_loaded,
			  &prev_es->tx_errors, &cur_es->tx_errors);

	vsi->stat_offsets_loaded = true;
}

/**
 * ice_free_fltr_list - free filter lists helper
 * @dev: pointer to the device struct
 * @h: pointer to the list head to be freed
 *
 * Helper function to free filter lists previously created using
 * ice_add_mac_to_list
 */
void ice_free_fltr_list(struct device *dev, struct list_head *h)
{
	struct ice_fltr_list_entry *e, *tmp;

	list_for_each_entry_safe(e, tmp, h, list_entry) {
		list_del(&e->list_entry);
		devm_kfree(dev, e);
	}
}

/**
 * ice_vsi_add_vlan - Add VSI membership for given VLAN
 * @vsi: the VSI being configured
 * @vid: VLAN ID to be added
 */
int ice_vsi_add_vlan(struct ice_vsi *vsi, u16 vid)
{
	struct ice_fltr_list_entry *tmp;
	struct ice_pf *pf = vsi->back;
	LIST_HEAD(tmp_add_list);
	enum ice_status status;
	int err = 0;

	tmp = devm_kzalloc(&pf->pdev->dev, sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	tmp->fltr_info.lkup_type = ICE_SW_LKUP_VLAN;
	tmp->fltr_info.fltr_act = ICE_FWD_TO_VSI;
	tmp->fltr_info.flag = ICE_FLTR_TX;
	tmp->fltr_info.src_id = ICE_SRC_ID_VSI;
	tmp->fltr_info.vsi_handle = vsi->idx;
	tmp->fltr_info.l_data.vlan.vlan_id = vid;

	INIT_LIST_HEAD(&tmp->list_entry);
	list_add(&tmp->list_entry, &tmp_add_list);

	status = ice_add_vlan(&pf->hw, &tmp_add_list);
	if (status) {
		err = -ENODEV;
		dev_err(&pf->pdev->dev, "Failure Adding VLAN %d on VSI %i\n",
			vid, vsi->vsi_num);
	}

	ice_free_fltr_list(&pf->pdev->dev, &tmp_add_list);
	return err;
}

/**
 * ice_vsi_kill_vlan - Remove VSI membership for a given VLAN
 * @vsi: the VSI being configured
 * @vid: VLAN ID to be removed
 *
 * Returns 0 on success and negative on failure
 */
int ice_vsi_kill_vlan(struct ice_vsi *vsi, u16 vid)
{
	struct ice_fltr_list_entry *list;
	struct ice_pf *pf = vsi->back;
	LIST_HEAD(tmp_add_list);
	enum ice_status status;
	int err = 0;

	list = devm_kzalloc(&pf->pdev->dev, sizeof(*list), GFP_KERNEL);
	if (!list)
		return -ENOMEM;

	list->fltr_info.lkup_type = ICE_SW_LKUP_VLAN;
	list->fltr_info.vsi_handle = vsi->idx;
	list->fltr_info.fltr_act = ICE_FWD_TO_VSI;
	list->fltr_info.l_data.vlan.vlan_id = vid;
	list->fltr_info.flag = ICE_FLTR_TX;
	list->fltr_info.src_id = ICE_SRC_ID_VSI;

	INIT_LIST_HEAD(&list->list_entry);
	list_add(&list->list_entry, &tmp_add_list);

	status = ice_remove_vlan(&pf->hw, &tmp_add_list);
	if (status == ICE_ERR_DOES_NOT_EXIST) {
		dev_dbg(&pf->pdev->dev,
			"Failed to remove VLAN %d on VSI %i, it does not exist, status: %d\n",
			vid, vsi->vsi_num, status);
	} else if (status) {
		dev_err(&pf->pdev->dev,
			"Error removing VLAN %d on vsi %i error: %d\n",
			vid, vsi->vsi_num, status);
		err = -EIO;
	}

	ice_free_fltr_list(&pf->pdev->dev, &tmp_add_list);
	return err;
}

/**
 * ice_vsi_cfg_rxqs - Configure the VSI for Rx
 * @vsi: the VSI being configured
 *
 * Return 0 on success and a negative value on error
 * Configure the Rx VSI for operation.
 */
int ice_vsi_cfg_rxqs(struct ice_vsi *vsi)
{
	u16 i;

	if (vsi->type == ICE_VSI_VF)
		goto setup_rings;

	if (vsi->netdev && vsi->netdev->mtu > ETH_DATA_LEN)
		vsi->max_frame = vsi->netdev->mtu +
			ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;
	else
		vsi->max_frame = ICE_RXBUF_2048;

	vsi->rx_buf_len = ICE_RXBUF_2048;
setup_rings:
	/* set up individual rings */
	for (i = 0; i < vsi->num_rxq; i++) {
		int err;

		err = ice_setup_rx_ctx(vsi->rx_rings[i]);
		if (err) {
			dev_err(&vsi->back->pdev->dev,
				"ice_setup_rx_ctx failed for RxQ %d, err %d\n",
				i, err);
			return err;
		}
	}

	return 0;
}

/**
 * ice_vsi_cfg_txqs - Configure the VSI for Tx
 * @vsi: the VSI being configured
 * @rings: Tx ring array to be configured
 * @offset: offset within vsi->txq_map
 *
 * Return 0 on success and a negative value on error
 * Configure the Tx VSI for operation.
 */
static int
ice_vsi_cfg_txqs(struct ice_vsi *vsi, struct ice_ring **rings, int offset)
{
	struct ice_aqc_add_tx_qgrp *qg_buf;
	struct ice_aqc_add_txqs_perq *txq;
	struct ice_pf *pf = vsi->back;
	u8 num_q_grps, q_idx = 0;
	enum ice_status status;
	u16 buf_len, i, pf_q;
	int err = 0, tc;

	buf_len = sizeof(*qg_buf);
	qg_buf = devm_kzalloc(&pf->pdev->dev, buf_len, GFP_KERNEL);
	if (!qg_buf)
		return -ENOMEM;

	qg_buf->num_txqs = 1;
	num_q_grps = 1;

	/* set up and configure the Tx queues for each enabled TC */
	ice_for_each_traffic_class(tc) {
		if (!(vsi->tc_cfg.ena_tc & BIT(tc)))
			break;

		for (i = 0; i < vsi->tc_cfg.tc_info[tc].qcount_tx; i++) {
			struct ice_tlan_ctx tlan_ctx = { 0 };

			pf_q = vsi->txq_map[q_idx + offset];
			ice_setup_tx_ctx(rings[q_idx], &tlan_ctx, pf_q);
			/* copy context contents into the qg_buf */
			qg_buf->txqs[0].txq_id = cpu_to_le16(pf_q);
			ice_set_ctx((u8 *)&tlan_ctx, qg_buf->txqs[0].txq_ctx,
				    ice_tlan_ctx_info);

			/* init queue specific tail reg. It is referred as
			 * transmit comm scheduler queue doorbell.
			 */
			rings[q_idx]->tail =
				pf->hw.hw_addr + QTX_COMM_DBELL(pf_q);
			status = ice_ena_vsi_txq(vsi->port_info, vsi->idx, tc,
						 i, num_q_grps, qg_buf,
						 buf_len, NULL);
			if (status) {
				dev_err(&pf->pdev->dev,
					"Failed to set LAN Tx queue context, error: %d\n",
					status);
				err = -ENODEV;
				goto err_cfg_txqs;
			}

			/* Add Tx Queue TEID into the VSI Tx ring from the
			 * response. This will complete configuring and
			 * enabling the queue.
			 */
			txq = &qg_buf->txqs[0];
			if (pf_q == le16_to_cpu(txq->txq_id))
				rings[q_idx]->txq_teid =
					le32_to_cpu(txq->q_teid);

			q_idx++;
		}
	}
err_cfg_txqs:
	devm_kfree(&pf->pdev->dev, qg_buf);
	return err;
}

/**
 * ice_vsi_cfg_lan_txqs - Configure the VSI for Tx
 * @vsi: the VSI being configured
 *
 * Return 0 on success and a negative value on error
 * Configure the Tx VSI for operation.
 */
int ice_vsi_cfg_lan_txqs(struct ice_vsi *vsi)
{
	return ice_vsi_cfg_txqs(vsi, vsi->tx_rings, 0);
}

/**
 * ice_intrl_usec_to_reg - convert interrupt rate limit to register value
 * @intrl: interrupt rate limit in usecs
 * @gran: interrupt rate limit granularity in usecs
 *
 * This function converts a decimal interrupt rate limit in usecs to the format
 * expected by firmware.
 */
u32 ice_intrl_usec_to_reg(u8 intrl, u8 gran)
{
	u32 val = intrl / gran;

	if (val)
		return val | GLINT_RATE_INTRL_ENA_M;
	return 0;
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
 * ice_cfg_itr - configure the initial interrupt throttle values
 * @hw: pointer to the HW structure
 * @q_vector: interrupt vector that's being configured
 *
 * Configure interrupt throttling values for the ring containers that are
 * associated with the interrupt vector passed in.
 */
static void
ice_cfg_itr(struct ice_hw *hw, struct ice_q_vector *q_vector)
{
	ice_cfg_itr_gran(hw);

	if (q_vector->num_ring_rx) {
		struct ice_ring_container *rc = &q_vector->rx;

		/* if this value is set then don't overwrite with default */
		if (!rc->itr_setting)
			rc->itr_setting = ICE_DFLT_RX_ITR;

		rc->target_itr = ITR_TO_REG(rc->itr_setting);
		rc->next_update = jiffies + 1;
		rc->current_itr = rc->target_itr;
		wr32(hw, GLINT_ITR(rc->itr_idx, q_vector->reg_idx),
		     ITR_REG_ALIGN(rc->current_itr) >> ICE_ITR_GRAN_S);
	}

	if (q_vector->num_ring_tx) {
		struct ice_ring_container *rc = &q_vector->tx;

		/* if this value is set then don't overwrite with default */
		if (!rc->itr_setting)
			rc->itr_setting = ICE_DFLT_TX_ITR;

		rc->target_itr = ITR_TO_REG(rc->itr_setting);
		rc->next_update = jiffies + 1;
		rc->current_itr = rc->target_itr;
		wr32(hw, GLINT_ITR(rc->itr_idx, q_vector->reg_idx),
		     ITR_REG_ALIGN(rc->current_itr) >> ICE_ITR_GRAN_S);
	}
}

/**
 * ice_vsi_cfg_msix - MSIX mode Interrupt Config in the HW
 * @vsi: the VSI being configured
 */
void ice_vsi_cfg_msix(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	u32 txq = 0, rxq = 0;
	int i, q;

	for (i = 0; i < vsi->num_q_vectors; i++) {
		struct ice_q_vector *q_vector = vsi->q_vectors[i];
		u16 reg_idx = q_vector->reg_idx;

		ice_cfg_itr(hw, q_vector);

		wr32(hw, GLINT_RATE(reg_idx),
		     ice_intrl_usec_to_reg(q_vector->intrl, hw->intrl_gran));

		/* Both Transmit Queue Interrupt Cause Control register
		 * and Receive Queue Interrupt Cause control register
		 * expects MSIX_INDX field to be the vector index
		 * within the function space and not the absolute
		 * vector index across PF or across device.
		 * For SR-IOV VF VSIs queue vector index always starts
		 * with 1 since first vector index(0) is used for OICR
		 * in VF space. Since VMDq and other PF VSIs are within
		 * the PF function space, use the vector index that is
		 * tracked for this PF.
		 */
		for (q = 0; q < q_vector->num_ring_tx; q++) {
			int itr_idx = (q_vector->tx.itr_idx <<
				       QINT_TQCTL_ITR_INDX_S) &
				QINT_TQCTL_ITR_INDX_M;
			u32 val;

			if (vsi->type == ICE_VSI_VF)
				val = QINT_TQCTL_CAUSE_ENA_M | itr_idx |
				      (((i + 1) << QINT_TQCTL_MSIX_INDX_S) &
				       QINT_TQCTL_MSIX_INDX_M);
			else
				val = QINT_TQCTL_CAUSE_ENA_M | itr_idx |
				      ((reg_idx << QINT_TQCTL_MSIX_INDX_S) &
				       QINT_TQCTL_MSIX_INDX_M);
			wr32(hw, QINT_TQCTL(vsi->txq_map[txq]), val);
			txq++;
		}

		for (q = 0; q < q_vector->num_ring_rx; q++) {
			int itr_idx = (q_vector->rx.itr_idx <<
				       QINT_RQCTL_ITR_INDX_S) &
				QINT_RQCTL_ITR_INDX_M;
			u32 val;

			if (vsi->type == ICE_VSI_VF)
				val = QINT_RQCTL_CAUSE_ENA_M | itr_idx |
					(((i + 1) << QINT_RQCTL_MSIX_INDX_S) &
					 QINT_RQCTL_MSIX_INDX_M);
			else
				val = QINT_RQCTL_CAUSE_ENA_M | itr_idx |
					((reg_idx << QINT_RQCTL_MSIX_INDX_S) &
					 QINT_RQCTL_MSIX_INDX_M);
			wr32(hw, QINT_RQCTL(vsi->rxq_map[rxq]), val);
			rxq++;
		}
	}

	ice_flush(hw);
}

/**
 * ice_vsi_manage_vlan_insertion - Manage VLAN insertion for the VSI for Tx
 * @vsi: the VSI being changed
 */
int ice_vsi_manage_vlan_insertion(struct ice_vsi *vsi)
{
	struct device *dev = &vsi->back->pdev->dev;
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx *ctxt;
	enum ice_status status;
	int ret = 0;

	ctxt = devm_kzalloc(dev, sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	/* Here we are configuring the VSI to let the driver add VLAN tags by
	 * setting vlan_flags to ICE_AQ_VSI_VLAN_MODE_ALL. The actual VLAN tag
	 * insertion happens in the Tx hot path, in ice_tx_map.
	 */
	ctxt->info.vlan_flags = ICE_AQ_VSI_VLAN_MODE_ALL;

	/* Preserve existing VLAN strip setting */
	ctxt->info.vlan_flags |= (vsi->info.vlan_flags &
				  ICE_AQ_VSI_VLAN_EMOD_M);

	ctxt->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID);

	status = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (status) {
		dev_err(dev, "update VSI for VLAN insert failed, err %d aq_err %d\n",
			status, hw->adminq.sq_last_status);
		ret = -EIO;
		goto out;
	}

	vsi->info.vlan_flags = ctxt->info.vlan_flags;
out:
	devm_kfree(dev, ctxt);
	return ret;
}

/**
 * ice_vsi_manage_vlan_stripping - Manage VLAN stripping for the VSI for Rx
 * @vsi: the VSI being changed
 * @ena: boolean value indicating if this is a enable or disable request
 */
int ice_vsi_manage_vlan_stripping(struct ice_vsi *vsi, bool ena)
{
	struct device *dev = &vsi->back->pdev->dev;
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx *ctxt;
	enum ice_status status;
	int ret = 0;

	ctxt = devm_kzalloc(dev, sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	/* Here we are configuring what the VSI should do with the VLAN tag in
	 * the Rx packet. We can either leave the tag in the packet or put it in
	 * the Rx descriptor.
	 */
	if (ena)
		/* Strip VLAN tag from Rx packet and put it in the desc */
		ctxt->info.vlan_flags = ICE_AQ_VSI_VLAN_EMOD_STR_BOTH;
	else
		/* Disable stripping. Leave tag in packet */
		ctxt->info.vlan_flags = ICE_AQ_VSI_VLAN_EMOD_NOTHING;

	/* Allow all packets untagged/tagged */
	ctxt->info.vlan_flags |= ICE_AQ_VSI_VLAN_MODE_ALL;

	ctxt->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID);

	status = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (status) {
		dev_err(dev, "update VSI for VLAN strip failed, ena = %d err %d aq_err %d\n",
			ena, status, hw->adminq.sq_last_status);
		ret = -EIO;
		goto out;
	}

	vsi->info.vlan_flags = ctxt->info.vlan_flags;
out:
	devm_kfree(dev, ctxt);
	return ret;
}

/**
 * ice_vsi_start_rx_rings - start VSI's Rx rings
 * @vsi: the VSI whose rings are to be started
 *
 * Returns 0 on success and a negative value on error
 */
int ice_vsi_start_rx_rings(struct ice_vsi *vsi)
{
	return ice_vsi_ctrl_rx_rings(vsi, true);
}

/**
 * ice_vsi_stop_rx_rings - stop VSI's Rx rings
 * @vsi: the VSI
 *
 * Returns 0 on success and a negative value on error
 */
int ice_vsi_stop_rx_rings(struct ice_vsi *vsi)
{
	return ice_vsi_ctrl_rx_rings(vsi, false);
}

/**
 * ice_vsi_stop_tx_rings - Disable Tx rings
 * @vsi: the VSI being configured
 * @rst_src: reset source
 * @rel_vmvf_num: Relative ID of VF/VM
 * @rings: Tx ring array to be stopped
 * @offset: offset within vsi->txq_map
 */
static int
ice_vsi_stop_tx_rings(struct ice_vsi *vsi, enum ice_disq_rst_src rst_src,
		      u16 rel_vmvf_num, struct ice_ring **rings, int offset)
{
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	int tc, q_idx = 0, err = 0;
	u16 *q_ids, *q_handles, i;
	enum ice_status status;
	u32 *q_teids, val;

	if (vsi->num_txq > ICE_LAN_TXQ_MAX_QDIS)
		return -EINVAL;

	q_teids = devm_kcalloc(&pf->pdev->dev, vsi->num_txq, sizeof(*q_teids),
			       GFP_KERNEL);
	if (!q_teids)
		return -ENOMEM;

	q_ids = devm_kcalloc(&pf->pdev->dev, vsi->num_txq, sizeof(*q_ids),
			     GFP_KERNEL);
	if (!q_ids) {
		err = -ENOMEM;
		goto err_alloc_q_ids;
	}

	q_handles = devm_kcalloc(&pf->pdev->dev, vsi->num_txq,
				 sizeof(*q_handles), GFP_KERNEL);
	if (!q_handles) {
		err = -ENOMEM;
		goto err_alloc_q_handles;
	}

	/* set up the Tx queue list to be disabled for each enabled TC */
	ice_for_each_traffic_class(tc) {
		if (!(vsi->tc_cfg.ena_tc & BIT(tc)))
			break;

		for (i = 0; i < vsi->tc_cfg.tc_info[tc].qcount_tx; i++) {
			if (!rings || !rings[q_idx] ||
			    !rings[q_idx]->q_vector) {
				err = -EINVAL;
				goto err_out;
			}

			q_ids[i] = vsi->txq_map[q_idx + offset];
			q_teids[i] = rings[q_idx]->txq_teid;
			q_handles[i] = i;

			/* clear cause_ena bit for disabled queues */
			val = rd32(hw, QINT_TQCTL(rings[i]->reg_idx));
			val &= ~QINT_TQCTL_CAUSE_ENA_M;
			wr32(hw, QINT_TQCTL(rings[i]->reg_idx), val);

			/* software is expected to wait for 100 ns */
			ndelay(100);

			/* trigger a software interrupt for the vector
			 * associated to the queue to schedule NAPI handler
			 */
			wr32(hw, GLINT_DYN_CTL(rings[i]->q_vector->reg_idx),
			     GLINT_DYN_CTL_SWINT_TRIG_M |
			     GLINT_DYN_CTL_INTENA_MSK_M);
			q_idx++;
		}
		status = ice_dis_vsi_txq(vsi->port_info, vsi->idx, tc,
					 vsi->num_txq, q_handles, q_ids,
					 q_teids, rst_src, rel_vmvf_num, NULL);

		/* if the disable queue command was exercised during an active
		 * reset flow, ICE_ERR_RESET_ONGOING is returned. This is not
		 * an error as the reset operation disables queues at the
		 * hardware level anyway.
		 */
		if (status == ICE_ERR_RESET_ONGOING) {
			dev_dbg(&pf->pdev->dev,
				"Reset in progress. LAN Tx queues already disabled\n");
		} else if (status) {
			dev_err(&pf->pdev->dev,
				"Failed to disable LAN Tx queues, error: %d\n",
				status);
			err = -ENODEV;
		}
	}

err_out:
	devm_kfree(&pf->pdev->dev, q_handles);

err_alloc_q_handles:
	devm_kfree(&pf->pdev->dev, q_ids);

err_alloc_q_ids:
	devm_kfree(&pf->pdev->dev, q_teids);

	return err;
}

/**
 * ice_vsi_stop_lan_tx_rings - Disable LAN Tx rings
 * @vsi: the VSI being configured
 * @rst_src: reset source
 * @rel_vmvf_num: Relative ID of VF/VM
 */
int
ice_vsi_stop_lan_tx_rings(struct ice_vsi *vsi, enum ice_disq_rst_src rst_src,
			  u16 rel_vmvf_num)
{
	return ice_vsi_stop_tx_rings(vsi, rst_src, rel_vmvf_num, vsi->tx_rings,
				     0);
}

/**
 * ice_cfg_vlan_pruning - enable or disable VLAN pruning on the VSI
 * @vsi: VSI to enable or disable VLAN pruning on
 * @ena: set to true to enable VLAN pruning and false to disable it
 * @vlan_promisc: enable valid security flags if not in VLAN promiscuous mode
 *
 * returns 0 if VSI is updated, negative otherwise
 */
int ice_cfg_vlan_pruning(struct ice_vsi *vsi, bool ena, bool vlan_promisc)
{
	struct ice_vsi_ctx *ctxt;
	struct device *dev;
	struct ice_pf *pf;
	int status;

	if (!vsi)
		return -EINVAL;

	pf = vsi->back;
	dev = &pf->pdev->dev;
	ctxt = devm_kzalloc(dev, sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	ctxt->info = vsi->info;

	if (ena) {
		ctxt->info.sec_flags |=
			ICE_AQ_VSI_SEC_TX_VLAN_PRUNE_ENA <<
			ICE_AQ_VSI_SEC_TX_PRUNE_ENA_S;
		ctxt->info.sw_flags2 |= ICE_AQ_VSI_SW_FLAG_RX_VLAN_PRUNE_ENA;
	} else {
		ctxt->info.sec_flags &=
			~(ICE_AQ_VSI_SEC_TX_VLAN_PRUNE_ENA <<
			  ICE_AQ_VSI_SEC_TX_PRUNE_ENA_S);
		ctxt->info.sw_flags2 &= ~ICE_AQ_VSI_SW_FLAG_RX_VLAN_PRUNE_ENA;
	}

	if (!vlan_promisc)
		ctxt->info.valid_sections =
			cpu_to_le16(ICE_AQ_VSI_PROP_SECURITY_VALID |
				    ICE_AQ_VSI_PROP_SW_VALID);

	status = ice_update_vsi(&pf->hw, vsi->idx, ctxt, NULL);
	if (status) {
		netdev_err(vsi->netdev, "%sabling VLAN pruning on VSI handle: %d, VSI HW ID: %d failed, err = %d, aq_err = %d\n",
			   ena ? "En" : "Dis", vsi->idx, vsi->vsi_num, status,
			   pf->hw.adminq.sq_last_status);
		goto err_out;
	}

	vsi->info.sec_flags = ctxt->info.sec_flags;
	vsi->info.sw_flags2 = ctxt->info.sw_flags2;

	devm_kfree(dev, ctxt);
	return 0;

err_out:
	devm_kfree(dev, ctxt);
	return -EIO;
}

static void ice_vsi_set_tc_cfg(struct ice_vsi *vsi)
{
	struct ice_dcbx_cfg *cfg = &vsi->port_info->local_dcbx_cfg;

	vsi->tc_cfg.ena_tc = ice_dcb_get_ena_tc(cfg);
	vsi->tc_cfg.numtc = ice_dcb_get_num_tc(cfg);
}

/**
 * ice_vsi_set_q_vectors_reg_idx - set the HW register index for all q_vectors
 * @vsi: VSI to set the q_vectors register index on
 */
static int
ice_vsi_set_q_vectors_reg_idx(struct ice_vsi *vsi)
{
	u16 i;

	if (!vsi || !vsi->q_vectors)
		return -EINVAL;

	ice_for_each_q_vector(vsi, i) {
		struct ice_q_vector *q_vector = vsi->q_vectors[i];

		if (!q_vector) {
			dev_err(&vsi->back->pdev->dev,
				"Failed to set reg_idx on q_vector %d VSI %d\n",
				i, vsi->vsi_num);
			goto clear_reg_idx;
		}

		q_vector->reg_idx = q_vector->v_idx + vsi->hw_base_vector;
	}

	return 0;

clear_reg_idx:
	ice_for_each_q_vector(vsi, i) {
		struct ice_q_vector *q_vector = vsi->q_vectors[i];

		if (q_vector)
			q_vector->reg_idx = 0;
	}

	return -EINVAL;
}

/**
 * ice_vsi_add_rem_eth_mac - Program VSI ethertype based filter with rule
 * @vsi: the VSI being configured
 * @add_rule: boolean value to add or remove ethertype filter rule
 */
static void
ice_vsi_add_rem_eth_mac(struct ice_vsi *vsi, bool add_rule)
{
	struct ice_fltr_list_entry *list;
	struct ice_pf *pf = vsi->back;
	LIST_HEAD(tmp_add_list);
	enum ice_status status;

	list = devm_kzalloc(&pf->pdev->dev, sizeof(*list), GFP_KERNEL);
	if (!list)
		return;

	list->fltr_info.lkup_type = ICE_SW_LKUP_ETHERTYPE;
	list->fltr_info.fltr_act = ICE_DROP_PACKET;
	list->fltr_info.flag = ICE_FLTR_TX;
	list->fltr_info.src_id = ICE_SRC_ID_VSI;
	list->fltr_info.vsi_handle = vsi->idx;
	list->fltr_info.l_data.ethertype_mac.ethertype = vsi->ethtype;

	INIT_LIST_HEAD(&list->list_entry);
	list_add(&list->list_entry, &tmp_add_list);

	if (add_rule)
		status = ice_add_eth_mac(&pf->hw, &tmp_add_list);
	else
		status = ice_remove_eth_mac(&pf->hw, &tmp_add_list);

	if (status)
		dev_err(&pf->pdev->dev,
			"Failure Adding or Removing Ethertype on VSI %i error: %d\n",
			vsi->vsi_num, status);

	ice_free_fltr_list(&pf->pdev->dev, &tmp_add_list);
}

/**
 * ice_vsi_setup - Set up a VSI by a given type
 * @pf: board private structure
 * @pi: pointer to the port_info instance
 * @type: VSI type
 * @vf_id: defines VF ID to which this VSI connects. This field is meant to be
 *         used only for ICE_VSI_VF VSI type. For other VSI types, should
 *         fill-in ICE_INVAL_VFID as input.
 *
 * This allocates the sw VSI structure and its queue resources.
 *
 * Returns pointer to the successfully allocated and configured VSI sw struct on
 * success, NULL on failure.
 */
struct ice_vsi *
ice_vsi_setup(struct ice_pf *pf, struct ice_port_info *pi,
	      enum ice_vsi_type type, u16 vf_id)
{
	u16 max_txqs[ICE_MAX_TRAFFIC_CLASS] = { 0 };
	struct device *dev = &pf->pdev->dev;
	struct ice_vsi *vsi;
	int ret, i;

	if (type == ICE_VSI_VF)
		vsi = ice_vsi_alloc(pf, type, vf_id);
	else
		vsi = ice_vsi_alloc(pf, type, ICE_INVAL_VFID);

	if (!vsi) {
		dev_err(dev, "could not allocate VSI\n");
		return NULL;
	}

	vsi->port_info = pi;
	vsi->vsw = pf->first_sw;
	if (vsi->type == ICE_VSI_PF)
		vsi->ethtype = ETH_P_PAUSE;

	if (vsi->type == ICE_VSI_VF)
		vsi->vf_id = vf_id;

	if (ice_vsi_get_qs(vsi)) {
		dev_err(dev, "Failed to allocate queues. vsi->idx = %d\n",
			vsi->idx);
		goto unroll_get_qs;
	}

	/* set RSS capabilities */
	ice_vsi_set_rss_params(vsi);

	/* set TC configuration */
	ice_vsi_set_tc_cfg(vsi);

	/* create the VSI */
	ret = ice_vsi_init(vsi);
	if (ret)
		goto unroll_get_qs;

	switch (vsi->type) {
	case ICE_VSI_PF:
		ret = ice_vsi_alloc_q_vectors(vsi);
		if (ret)
			goto unroll_vsi_init;

		ret = ice_vsi_setup_vector_base(vsi);
		if (ret)
			goto unroll_alloc_q_vector;

		ret = ice_vsi_set_q_vectors_reg_idx(vsi);
		if (ret)
			goto unroll_vector_base;

		ret = ice_vsi_alloc_rings(vsi);
		if (ret)
			goto unroll_vector_base;

		ice_vsi_map_rings_to_vectors(vsi);

		/* Do not exit if configuring RSS had an issue, at least
		 * receive traffic on first queue. Hence no need to capture
		 * return value
		 */
		if (test_bit(ICE_FLAG_RSS_ENA, pf->flags))
			ice_vsi_cfg_rss_lut_key(vsi);
		break;
	case ICE_VSI_VF:
		/* VF driver will take care of creating netdev for this type and
		 * map queues to vectors through Virtchnl, PF driver only
		 * creates a VSI and corresponding structures for bookkeeping
		 * purpose
		 */
		ret = ice_vsi_alloc_q_vectors(vsi);
		if (ret)
			goto unroll_vsi_init;

		ret = ice_vsi_alloc_rings(vsi);
		if (ret)
			goto unroll_alloc_q_vector;

		/* Setup Vector base only during VF init phase or when VF asks
		 * for more vectors than assigned number. In all other cases,
		 * assign hw_base_vector to the value given earlier.
		 */
		if (test_bit(ICE_VF_STATE_CFG_INTR, pf->vf[vf_id].vf_states)) {
			ret = ice_vsi_setup_vector_base(vsi);
			if (ret)
				goto unroll_vector_base;
		} else {
			vsi->hw_base_vector = pf->vf[vf_id].first_vector_idx;
		}
		ret = ice_vsi_set_q_vectors_reg_idx(vsi);
		if (ret)
			goto unroll_vector_base;

		pf->q_left_tx -= vsi->alloc_txq;
		pf->q_left_rx -= vsi->alloc_rxq;
		break;
	default:
		/* clean up the resources and exit */
		goto unroll_vsi_init;
	}

	/* configure VSI nodes based on number of queues and TC's */
	for (i = 0; i < vsi->tc_cfg.numtc; i++)
		max_txqs[i] = pf->num_lan_tx;

	ret = ice_cfg_vsi_lan(vsi->port_info, vsi->idx, vsi->tc_cfg.ena_tc,
			      max_txqs);
	if (ret) {
		dev_err(&pf->pdev->dev,
			"VSI %d failed lan queue config, error %d\n",
			vsi->vsi_num, ret);
		goto unroll_vector_base;
	}

	/* Add switch rule to drop all Tx Flow Control Frames, of look up
	 * type ETHERTYPE from VSIs, and restrict malicious VF from sending
	 * out PAUSE or PFC frames. If enabled, FW can still send FC frames.
	 * The rule is added once for PF VSI in order to create appropriate
	 * recipe, since VSI/VSI list is ignored with drop action...
	 */
	if (vsi->type == ICE_VSI_PF)
		ice_vsi_add_rem_eth_mac(vsi, true);

	return vsi;

unroll_vector_base:
	/* reclaim SW interrupts back to the common pool */
	ice_free_res(pf->sw_irq_tracker, vsi->sw_base_vector, vsi->idx);
	pf->num_avail_sw_msix += vsi->num_q_vectors;
	/* reclaim HW interrupt back to the common pool */
	ice_free_res(pf->hw_irq_tracker, vsi->hw_base_vector, vsi->idx);
	pf->num_avail_hw_msix += vsi->num_q_vectors;
unroll_alloc_q_vector:
	ice_vsi_free_q_vectors(vsi);
unroll_vsi_init:
	ice_vsi_delete(vsi);
unroll_get_qs:
	ice_vsi_put_qs(vsi);
	pf->q_left_tx += vsi->alloc_txq;
	pf->q_left_rx += vsi->alloc_rxq;
	ice_vsi_clear(vsi);

	return NULL;
}

/**
 * ice_vsi_release_msix - Clear the queue to Interrupt mapping in HW
 * @vsi: the VSI being cleaned up
 */
static void ice_vsi_release_msix(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	u16 vector = vsi->hw_base_vector;
	struct ice_hw *hw = &pf->hw;
	u32 txq = 0;
	u32 rxq = 0;
	int i, q;

	for (i = 0; i < vsi->num_q_vectors; i++, vector++) {
		struct ice_q_vector *q_vector = vsi->q_vectors[i];

		wr32(hw, GLINT_ITR(ICE_IDX_ITR0, vector), 0);
		wr32(hw, GLINT_ITR(ICE_IDX_ITR1, vector), 0);
		for (q = 0; q < q_vector->num_ring_tx; q++) {
			wr32(hw, QINT_TQCTL(vsi->txq_map[txq]), 0);
			txq++;
		}

		for (q = 0; q < q_vector->num_ring_rx; q++) {
			wr32(hw, QINT_RQCTL(vsi->rxq_map[rxq]), 0);
			rxq++;
		}
	}

	ice_flush(hw);
}

/**
 * ice_vsi_free_irq - Free the IRQ association with the OS
 * @vsi: the VSI being configured
 */
void ice_vsi_free_irq(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	int base = vsi->sw_base_vector;

	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags)) {
		int i;

		if (!vsi->q_vectors || !vsi->irqs_ready)
			return;

		ice_vsi_release_msix(vsi);
		if (vsi->type == ICE_VSI_VF)
			return;

		vsi->irqs_ready = false;
		ice_for_each_q_vector(vsi, i) {
			u16 vector = i + base;
			int irq_num;

			irq_num = pf->msix_entries[vector].vector;

			/* free only the irqs that were actually requested */
			if (!vsi->q_vectors[i] ||
			    !(vsi->q_vectors[i]->num_ring_tx ||
			      vsi->q_vectors[i]->num_ring_rx))
				continue;

			/* clear the affinity notifier in the IRQ descriptor */
			irq_set_affinity_notifier(irq_num, NULL);

			/* clear the affinity_mask in the IRQ descriptor */
			irq_set_affinity_hint(irq_num, NULL);
			synchronize_irq(irq_num);
			devm_free_irq(&pf->pdev->dev, irq_num,
				      vsi->q_vectors[i]);
		}
	}
}

/**
 * ice_vsi_free_tx_rings - Free Tx resources for VSI queues
 * @vsi: the VSI having resources freed
 */
void ice_vsi_free_tx_rings(struct ice_vsi *vsi)
{
	int i;

	if (!vsi->tx_rings)
		return;

	ice_for_each_txq(vsi, i)
		if (vsi->tx_rings[i] && vsi->tx_rings[i]->desc)
			ice_free_tx_ring(vsi->tx_rings[i]);
}

/**
 * ice_vsi_free_rx_rings - Free Rx resources for VSI queues
 * @vsi: the VSI having resources freed
 */
void ice_vsi_free_rx_rings(struct ice_vsi *vsi)
{
	int i;

	if (!vsi->rx_rings)
		return;

	ice_for_each_rxq(vsi, i)
		if (vsi->rx_rings[i] && vsi->rx_rings[i]->desc)
			ice_free_rx_ring(vsi->rx_rings[i]);
}

/**
 * ice_vsi_close - Shut down a VSI
 * @vsi: the VSI being shut down
 */
void ice_vsi_close(struct ice_vsi *vsi)
{
	if (!test_and_set_bit(__ICE_DOWN, vsi->state))
		ice_down(vsi);

	ice_vsi_free_irq(vsi);
	ice_vsi_free_tx_rings(vsi);
	ice_vsi_free_rx_rings(vsi);
}

/**
 * ice_free_res - free a block of resources
 * @res: pointer to the resource
 * @index: starting index previously returned by ice_get_res
 * @id: identifier to track owner
 *
 * Returns number of resources freed
 */
int ice_free_res(struct ice_res_tracker *res, u16 index, u16 id)
{
	int count = 0;
	int i;

	if (!res || index >= res->num_entries)
		return -EINVAL;

	id |= ICE_RES_VALID_BIT;
	for (i = index; i < res->num_entries && res->list[i] == id; i++) {
		res->list[i] = 0;
		count++;
	}

	return count;
}

/**
 * ice_search_res - Search the tracker for a block of resources
 * @res: pointer to the resource
 * @needed: size of the block needed
 * @id: identifier to track owner
 *
 * Returns the base item index of the block, or -ENOMEM for error
 */
static int ice_search_res(struct ice_res_tracker *res, u16 needed, u16 id)
{
	int start = res->search_hint;
	int end = start;

	if ((start + needed) > res->num_entries)
		return -ENOMEM;

	id |= ICE_RES_VALID_BIT;

	do {
		/* skip already allocated entries */
		if (res->list[end++] & ICE_RES_VALID_BIT) {
			start = end;
			if ((start + needed) > res->num_entries)
				break;
		}

		if (end == (start + needed)) {
			int i = start;

			/* there was enough, so assign it to the requestor */
			while (i != end)
				res->list[i++] = id;

			if (end == res->num_entries)
				end = 0;

			res->search_hint = end;
			return start;
		}
	} while (1);

	return -ENOMEM;
}

/**
 * ice_get_res - get a block of resources
 * @pf: board private structure
 * @res: pointer to the resource
 * @needed: size of the block needed
 * @id: identifier to track owner
 *
 * Returns the base item index of the block, or -ENOMEM for error
 * The search_hint trick and lack of advanced fit-finding only works
 * because we're highly likely to have all the same sized requests.
 * Linear search time and any fragmentation should be minimal.
 */
int
ice_get_res(struct ice_pf *pf, struct ice_res_tracker *res, u16 needed, u16 id)
{
	int ret;

	if (!res || !pf)
		return -EINVAL;

	if (!needed || needed > res->num_entries || id >= ICE_RES_VALID_BIT) {
		dev_err(&pf->pdev->dev,
			"param err: needed=%d, num_entries = %d id=0x%04x\n",
			needed, res->num_entries, id);
		return -EINVAL;
	}

	/* search based on search_hint */
	ret = ice_search_res(res, needed, id);

	if (ret < 0) {
		/* previous search failed. Reset search hint and try again */
		res->search_hint = 0;
		ret = ice_search_res(res, needed, id);
	}

	return ret;
}

/**
 * ice_vsi_dis_irq - Mask off queue interrupt generation on the VSI
 * @vsi: the VSI being un-configured
 */
void ice_vsi_dis_irq(struct ice_vsi *vsi)
{
	int base = vsi->sw_base_vector;
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	u32 val;
	int i;

	/* disable interrupt causation from each queue */
	if (vsi->tx_rings) {
		ice_for_each_txq(vsi, i) {
			if (vsi->tx_rings[i]) {
				u16 reg;

				reg = vsi->tx_rings[i]->reg_idx;
				val = rd32(hw, QINT_TQCTL(reg));
				val &= ~QINT_TQCTL_CAUSE_ENA_M;
				wr32(hw, QINT_TQCTL(reg), val);
			}
		}
	}

	if (vsi->rx_rings) {
		ice_for_each_rxq(vsi, i) {
			if (vsi->rx_rings[i]) {
				u16 reg;

				reg = vsi->rx_rings[i]->reg_idx;
				val = rd32(hw, QINT_RQCTL(reg));
				val &= ~QINT_RQCTL_CAUSE_ENA_M;
				wr32(hw, QINT_RQCTL(reg), val);
			}
		}
	}

	/* disable each interrupt */
	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags)) {
		ice_for_each_q_vector(vsi, i)
			wr32(hw, GLINT_DYN_CTL(vsi->q_vectors[i]->reg_idx), 0);

		ice_flush(hw);

		ice_for_each_q_vector(vsi, i)
			synchronize_irq(pf->msix_entries[i + base].vector);
	}
}

/**
 * ice_vsi_release - Delete a VSI and free its resources
 * @vsi: the VSI being removed
 *
 * Returns 0 on success or < 0 on error
 */
int ice_vsi_release(struct ice_vsi *vsi)
{
	struct ice_vf *vf = NULL;
	struct ice_pf *pf;

	if (!vsi->back)
		return -ENODEV;
	pf = vsi->back;

	if (vsi->type == ICE_VSI_VF)
		vf = &pf->vf[vsi->vf_id];
	/* do not unregister while driver is in the reset recovery pending
	 * state. Since reset/rebuild happens through PF service task workqueue,
	 * it's not a good idea to unregister netdev that is associated to the
	 * PF that is running the work queue items currently. This is done to
	 * avoid check_flush_dependency() warning on this wq
	 */
	if (vsi->netdev && !ice_is_reset_in_progress(pf->state))
		unregister_netdev(vsi->netdev);

	if (test_bit(ICE_FLAG_RSS_ENA, pf->flags))
		ice_rss_clean(vsi);

	/* Disable VSI and free resources */
	ice_vsi_dis_irq(vsi);
	ice_vsi_close(vsi);

	/* reclaim interrupt vectors back to PF */
	if (vsi->type != ICE_VSI_VF) {
		/* reclaim SW interrupts back to the common pool */
		ice_free_res(pf->sw_irq_tracker, vsi->sw_base_vector, vsi->idx);
		pf->num_avail_sw_msix += vsi->num_q_vectors;
		/* reclaim HW interrupts back to the common pool */
		ice_free_res(pf->hw_irq_tracker, vsi->hw_base_vector, vsi->idx);
		pf->num_avail_hw_msix += vsi->num_q_vectors;
	} else if (test_bit(ICE_VF_STATE_CFG_INTR, vf->vf_states)) {
		/* Reclaim VF resources back only while freeing all VFs or
		 * vector reassignment is requested
		 */
		ice_free_res(pf->hw_irq_tracker, vf->first_vector_idx,
			     vsi->idx);
		pf->num_avail_hw_msix += pf->num_vf_msix;
	}

	if (vsi->type == ICE_VSI_PF)
		ice_vsi_add_rem_eth_mac(vsi, false);

	ice_remove_vsi_fltr(&pf->hw, vsi->idx);
	ice_rm_vsi_lan_cfg(vsi->port_info, vsi->idx);
	ice_vsi_delete(vsi);
	ice_vsi_free_q_vectors(vsi);

	/* make sure unregister_netdev() was called by checking __ICE_DOWN */
	if (vsi->netdev && test_bit(__ICE_DOWN, vsi->state)) {
		free_netdev(vsi->netdev);
		vsi->netdev = NULL;
	}

	ice_vsi_clear_rings(vsi);

	ice_vsi_put_qs(vsi);
	pf->q_left_tx += vsi->alloc_txq;
	pf->q_left_rx += vsi->alloc_rxq;

	/* retain SW VSI data structure since it is needed to unregister and
	 * free VSI netdev when PF is not in reset recovery pending state,\
	 * for ex: during rmmod.
	 */
	if (!ice_is_reset_in_progress(pf->state))
		ice_vsi_clear(vsi);

	return 0;
}

/**
 * ice_vsi_rebuild - Rebuild VSI after reset
 * @vsi: VSI to be rebuild
 *
 * Returns 0 on success and negative value on failure
 */
int ice_vsi_rebuild(struct ice_vsi *vsi)
{
	u16 max_txqs[ICE_MAX_TRAFFIC_CLASS] = { 0 };
	struct ice_vf *vf = NULL;
	struct ice_pf *pf;
	int ret, i;

	if (!vsi)
		return -EINVAL;

	pf = vsi->back;
	if (vsi->type == ICE_VSI_VF)
		vf = &pf->vf[vsi->vf_id];

	ice_rm_vsi_lan_cfg(vsi->port_info, vsi->idx);
	ice_vsi_free_q_vectors(vsi);

	if (vsi->type != ICE_VSI_VF) {
		/* reclaim SW interrupts back to the common pool */
		ice_free_res(pf->sw_irq_tracker, vsi->sw_base_vector, vsi->idx);
		pf->num_avail_sw_msix += vsi->num_q_vectors;
		vsi->sw_base_vector = 0;
		/* reclaim HW interrupts back to the common pool */
		ice_free_res(pf->hw_irq_tracker, vsi->hw_base_vector,
			     vsi->idx);
		pf->num_avail_hw_msix += vsi->num_q_vectors;
	} else {
		/* Reclaim VF resources back to the common pool for reset and
		 * and rebuild, with vector reassignment
		 */
		ice_free_res(pf->hw_irq_tracker, vf->first_vector_idx,
			     vsi->idx);
		pf->num_avail_hw_msix += pf->num_vf_msix;
	}
	vsi->hw_base_vector = 0;

	ice_vsi_clear_rings(vsi);
	ice_vsi_free_arrays(vsi);
	ice_dev_onetime_setup(&pf->hw);
	if (vsi->type == ICE_VSI_VF)
		ice_vsi_set_num_qs(vsi, vf->vf_id);
	else
		ice_vsi_set_num_qs(vsi, ICE_INVAL_VFID);
	ice_vsi_set_tc_cfg(vsi);

	/* Initialize VSI struct elements and create VSI in FW */
	ret = ice_vsi_init(vsi);
	if (ret < 0)
		goto err_vsi;

	ret = ice_vsi_alloc_arrays(vsi);
	if (ret < 0)
		goto err_vsi;

	switch (vsi->type) {
	case ICE_VSI_PF:
		ret = ice_vsi_alloc_q_vectors(vsi);
		if (ret)
			goto err_rings;

		ret = ice_vsi_setup_vector_base(vsi);
		if (ret)
			goto err_vectors;

		ret = ice_vsi_set_q_vectors_reg_idx(vsi);
		if (ret)
			goto err_vectors;

		ret = ice_vsi_alloc_rings(vsi);
		if (ret)
			goto err_vectors;

		ice_vsi_map_rings_to_vectors(vsi);
		/* Do not exit if configuring RSS had an issue, at least
		 * receive traffic on first queue. Hence no need to capture
		 * return value
		 */
		if (test_bit(ICE_FLAG_RSS_ENA, pf->flags))
			ice_vsi_cfg_rss_lut_key(vsi);
		break;
	case ICE_VSI_VF:
		ret = ice_vsi_alloc_q_vectors(vsi);
		if (ret)
			goto err_rings;

		ret = ice_vsi_setup_vector_base(vsi);
		if (ret)
			goto err_vectors;

		ret = ice_vsi_set_q_vectors_reg_idx(vsi);
		if (ret)
			goto err_vectors;

		ret = ice_vsi_alloc_rings(vsi);
		if (ret)
			goto err_vectors;

		pf->q_left_tx -= vsi->alloc_txq;
		pf->q_left_rx -= vsi->alloc_rxq;
		break;
	default:
		break;
	}

	/* configure VSI nodes based on number of queues and TC's */
	for (i = 0; i < vsi->tc_cfg.numtc; i++)
		max_txqs[i] = pf->num_lan_tx;

	ret = ice_cfg_vsi_lan(vsi->port_info, vsi->idx, vsi->tc_cfg.ena_tc,
			      max_txqs);
	if (ret) {
		dev_err(&pf->pdev->dev,
			"VSI %d failed lan queue config, error %d\n",
			vsi->vsi_num, ret);
		goto err_vectors;
	}
	return 0;

err_vectors:
	ice_vsi_free_q_vectors(vsi);
err_rings:
	if (vsi->netdev) {
		vsi->current_netdev_flags = 0;
		unregister_netdev(vsi->netdev);
		free_netdev(vsi->netdev);
		vsi->netdev = NULL;
	}
err_vsi:
	ice_vsi_clear(vsi);
	set_bit(__ICE_RESET_FAILED, pf->state);
	return ret;
}

/**
 * ice_is_reset_in_progress - check for a reset in progress
 * @state: pf state field
 */
bool ice_is_reset_in_progress(unsigned long *state)
{
	return test_bit(__ICE_RESET_OICR_RECV, state) ||
	       test_bit(__ICE_PFR_REQ, state) ||
	       test_bit(__ICE_CORER_REQ, state) ||
	       test_bit(__ICE_GLOBR_REQ, state);
}

#ifdef CONFIG_DCB
/**
 * ice_vsi_update_q_map - update our copy of the VSI info with new queue map
 * @vsi: VSI being configured
 * @ctx: the context buffer returned from AQ VSI update command
 */
static void ice_vsi_update_q_map(struct ice_vsi *vsi, struct ice_vsi_ctx *ctx)
{
	vsi->info.mapping_flags = ctx->info.mapping_flags;
	memcpy(&vsi->info.q_mapping, &ctx->info.q_mapping,
	       sizeof(vsi->info.q_mapping));
	memcpy(&vsi->info.tc_mapping, ctx->info.tc_mapping,
	       sizeof(vsi->info.tc_mapping));
}

/**
 * ice_vsi_cfg_netdev_tc - Setup the netdev TC configuration
 * @vsi: the VSI being configured
 * @ena_tc: TC map to be enabled
 */
static void ice_vsi_cfg_netdev_tc(struct ice_vsi *vsi, u8 ena_tc)
{
	struct net_device *netdev = vsi->netdev;
	struct ice_pf *pf = vsi->back;
	struct ice_dcbx_cfg *dcbcfg;
	u8 netdev_tc;
	int i;

	if (!netdev)
		return;

	if (!ena_tc) {
		netdev_reset_tc(netdev);
		return;
	}

	if (netdev_set_num_tc(netdev, vsi->tc_cfg.numtc))
		return;

	dcbcfg = &pf->hw.port_info->local_dcbx_cfg;

	ice_for_each_traffic_class(i)
		if (vsi->tc_cfg.ena_tc & BIT(i))
			netdev_set_tc_queue(netdev,
					    vsi->tc_cfg.tc_info[i].netdev_tc,
					    vsi->tc_cfg.tc_info[i].qcount_tx,
					    vsi->tc_cfg.tc_info[i].qoffset);

	for (i = 0; i < ICE_MAX_USER_PRIORITY; i++) {
		u8 ets_tc = dcbcfg->etscfg.prio_table[i];

		/* Get the mapped netdev TC# for the UP */
		netdev_tc = vsi->tc_cfg.tc_info[ets_tc].netdev_tc;
		netdev_set_prio_tc_map(netdev, i, netdev_tc);
	}
}

/**
 * ice_vsi_cfg_tc - Configure VSI Tx Sched for given TC map
 * @vsi: VSI to be configured
 * @ena_tc: TC bitmap
 *
 * VSI queues expected to be quiesced before calling this function
 */
int ice_vsi_cfg_tc(struct ice_vsi *vsi, u8 ena_tc)
{
	u16 max_txqs[ICE_MAX_TRAFFIC_CLASS] = { 0 };
	struct ice_vsi_ctx *ctx;
	struct ice_pf *pf = vsi->back;
	enum ice_status status;
	int i, ret = 0;
	u8 num_tc = 0;

	ice_for_each_traffic_class(i) {
		/* build bitmap of enabled TCs */
		if (ena_tc & BIT(i))
			num_tc++;
		/* populate max_txqs per TC */
		max_txqs[i] = pf->num_lan_tx;
	}

	vsi->tc_cfg.ena_tc = ena_tc;
	vsi->tc_cfg.numtc = num_tc;

	ctx = devm_kzalloc(&pf->pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->vf_num = 0;
	ctx->info = vsi->info;

	ice_vsi_setup_q_map(vsi, ctx);

	/* must to indicate which section of VSI context are being modified */
	ctx->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_RXQ_MAP_VALID);
	status = ice_update_vsi(&pf->hw, vsi->idx, ctx, NULL);
	if (status) {
		dev_info(&pf->pdev->dev, "Failed VSI Update\n");
		ret = -EIO;
		goto out;
	}

	status = ice_cfg_vsi_lan(vsi->port_info, vsi->idx, vsi->tc_cfg.ena_tc,
				 max_txqs);

	if (status) {
		dev_err(&pf->pdev->dev,
			"VSI %d failed TC config, error %d\n",
			vsi->vsi_num, status);
		ret = -EIO;
		goto out;
	}
	ice_vsi_update_q_map(vsi, ctx);
	vsi->info.valid_sections = 0;

	ice_vsi_cfg_netdev_tc(vsi, ena_tc);
out:
	devm_kfree(&pf->pdev->dev, ctx);
	return ret;
}
#endif /* CONFIG_DCB */
