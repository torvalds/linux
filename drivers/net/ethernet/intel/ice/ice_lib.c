// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"

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

	/* what is RX queue number in global space of 2K Rx queues */
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
	regval = rd32(hw, QRXFLXP_CNTXT(pf_q));
	regval |= (rxdid << QRXFLXP_CNTXT_RXDID_IDX_S) &
		QRXFLXP_CNTXT_RXDID_IDX_M;

	/* increasing context priority to pick up profile id;
	 * default is 0x01; setting to 0x03 to ensure profile
	 * is programming if prev context is of same priority
	 */
	regval |= (0x03 << QRXFLXP_CNTXT_RXDID_PRIO_S) &
		QRXFLXP_CNTXT_RXDID_PRIO_M;

	wr32(hw, QRXFLXP_CNTXT(pf_q), regval);

	/* Absolute queue number out of 2K needs to be passed */
	err = ice_write_rxq_ctx(hw, &rlan_ctx, pf_q);
	if (err) {
		dev_err(&vsi->back->pdev->dev,
			"Failed to set LAN Rx queue context for absolute Rx queue %d error: %d\n",
			pf_q, err);
		return -EIO;
	}

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
	default:
		return;
	}

	/* make sure the context is associated with the right VSI */
	tlan_ctx->src_vsi = vsi->vsi_num;

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

	for (i = 0; i < ICE_Q_WAIT_RETRY_LIMIT; i++) {
		u32 rx_reg = rd32(&pf->hw, QRX_CTRL(pf_q));

		if (ena == !!(rx_reg & QRX_CTRL_QENA_STAT_M))
			break;

		usleep_range(10, 20);
	}
	if (i >= ICE_Q_WAIT_RETRY_LIMIT)
		return -ETIMEDOUT;

	return 0;
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
	int i, j, ret = 0;

	for (i = 0; i < vsi->num_rxq; i++) {
		int pf_q = vsi->rxq_map[i];
		u32 rx_reg;

		for (j = 0; j < ICE_Q_WAIT_MAX_RETRY; j++) {
			rx_reg = rd32(hw, QRX_CTRL(pf_q));
			if (((rx_reg >> QRX_CTRL_QENA_REQ_S) & 1) ==
			    ((rx_reg >> QRX_CTRL_QENA_STAT_S) & 1))
				break;
			usleep_range(1000, 2000);
		}

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
 * @alloc_qvectors: a bool to specify if q_vectors need to be allocated.
 *
 * On error: returns error code (negative)
 * On success: returns 0
 */
int ice_vsi_alloc_arrays(struct ice_vsi *vsi, bool alloc_qvectors)
{
	struct ice_pf *pf = vsi->back;

	/* allocate memory for both Tx and Rx ring pointers */
	vsi->tx_rings = devm_kcalloc(&pf->pdev->dev, vsi->alloc_txq,
				     sizeof(struct ice_ring *), GFP_KERNEL);
	if (!vsi->tx_rings)
		goto err_txrings;

	vsi->rx_rings = devm_kcalloc(&pf->pdev->dev, vsi->alloc_rxq,
				     sizeof(struct ice_ring *), GFP_KERNEL);
	if (!vsi->rx_rings)
		goto err_rxrings;

	if (alloc_qvectors) {
		/* allocate memory for q_vector pointers */
		vsi->q_vectors = devm_kcalloc(&pf->pdev->dev,
					      vsi->num_q_vectors,
					      sizeof(struct ice_q_vector *),
					      GFP_KERNEL);
		if (!vsi->q_vectors)
			goto err_vectors;
	}

	return 0;

err_vectors:
	devm_kfree(&pf->pdev->dev, vsi->rx_rings);
err_rxrings:
	devm_kfree(&pf->pdev->dev, vsi->tx_rings);
err_txrings:
	return -ENOMEM;
}

/**
 * ice_vsi_set_num_qs - Set num queues, descriptors and vectors for a VSI
 * @vsi: the VSI being configured
 *
 * Return 0 on success and a negative value on error
 */
void ice_vsi_set_num_qs(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;

	switch (vsi->type) {
	case ICE_VSI_PF:
		vsi->alloc_txq = pf->num_lan_tx;
		vsi->alloc_rxq = pf->num_lan_rx;
		vsi->num_desc = ALIGN(ICE_DFLT_NUM_DESC, ICE_REQ_DESC_MULTIPLE);
		vsi->num_q_vectors = max_t(int, pf->num_lan_rx, pf->num_lan_tx);
		break;
	default:
		dev_warn(&vsi->back->pdev->dev, "Unknown VSI type %d\n",
			 vsi->type);
		break;
	}
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
int ice_get_free_slot(void *array, int size, int curr)
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
	struct ice_vsi_ctx ctxt;
	enum ice_status status;

	ctxt.vsi_num = vsi->vsi_num;

	memcpy(&ctxt.info, &vsi->info, sizeof(struct ice_aqc_vsi_props));

	status = ice_free_vsi(&pf->hw, vsi->idx, &ctxt, false, NULL);
	if (status)
		dev_err(&pf->pdev->dev, "Failed to delete VSI %i in FW\n",
			vsi->vsi_num);
}

/**
 * ice_vsi_free_arrays - clean up VSI resources
 * @vsi: pointer to VSI being cleared
 * @free_qvectors: bool to specify if q_vectors should be deallocated
 */
void ice_vsi_free_arrays(struct ice_vsi *vsi, bool free_qvectors)
{
	struct ice_pf *pf = vsi->back;

	/* free the ring and vector containers */
	if (free_qvectors && vsi->q_vectors) {
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

	ice_vsi_free_arrays(vsi, true);
	mutex_unlock(&pf->sw_mutex);
	devm_kfree(&pf->pdev->dev, vsi);

	return 0;
}

/**
 * ice_msix_clean_rings - MSIX mode Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 */
irqreturn_t ice_msix_clean_rings(int __always_unused irq, void *data)
{
	struct ice_q_vector *q_vector = (struct ice_q_vector *)data;

	if (!q_vector->tx.ring && !q_vector->rx.ring)
		return IRQ_HANDLED;

	napi_schedule(&q_vector->napi);

	return IRQ_HANDLED;
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
 * ice_vsi_set_rss_params - Setup RSS capabilities per VSI type
 * @vsi: the VSI being configured
 */
void ice_vsi_set_rss_params(struct ice_vsi *vsi)
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
	u16 offset = 0, qmap = 0, numq_tc;
	u16 pow = 0, max_rss = 0, qcount;
	u16 qcount_tx = vsi->alloc_txq;
	u16 qcount_rx = vsi->alloc_rxq;
	bool ena_tc0 = false;
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

	numq_tc = qcount_rx / vsi->tc_cfg.numtc;

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

	/* qcount will change if RSS is enabled */
	if (test_bit(ICE_FLAG_RSS_ENA, vsi->back->flags)) {
		if (vsi->type == ICE_VSI_PF)
			max_rss = ICE_MAX_LG_RSS_QS;
		else
			max_rss = ICE_MAX_SMALL_RSS_QS;

		qcount = min_t(int, numq_tc, max_rss);
		qcount = min_t(int, qcount, vsi->rss_size);
	} else {
		qcount = numq_tc;
	}

	/* find the (rounded up) power-of-2 of qcount */
	pow = order_base_2(qcount);

	for (i = 0; i < ICE_MAX_TRAFFIC_CLASS; i++) {
		if (!(vsi->tc_cfg.ena_tc & BIT(i))) {
			/* TC is not enabled */
			vsi->tc_cfg.tc_info[i].qoffset = 0;
			vsi->tc_cfg.tc_info[i].qcount = 1;
			ctxt->info.tc_mapping[i] = 0;
			continue;
		}

		/* TC is enabled */
		vsi->tc_cfg.tc_info[i].qoffset = offset;
		vsi->tc_cfg.tc_info[i].qcount = qcount;

		qmap = ((offset << ICE_AQ_VSI_TC_Q_OFFSET_S) &
			ICE_AQ_VSI_TC_Q_OFFSET_M) |
			((pow << ICE_AQ_VSI_TC_Q_NUM_S) &
			 ICE_AQ_VSI_TC_Q_NUM_M);
		offset += qcount;
		ctxt->info.tc_mapping[i] = cpu_to_le16(qmap);
	}

	vsi->num_txq = qcount_tx;
	vsi->num_rxq = offset;

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

	switch (vsi->type) {
	case ICE_VSI_PF:
		/* PF VSI will inherit RSS instance of PF */
		lut_type = ICE_AQ_VSI_Q_OPT_RSS_LUT_PF;
		hash_type = ICE_AQ_VSI_Q_OPT_RSS_TPLZ;
		break;
	default:
		dev_warn(&vsi->back->pdev->dev, "Unknown VSI type %d\n",
			 vsi->type);
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
int ice_vsi_init(struct ice_vsi *vsi)
{
	struct ice_vsi_ctx ctxt = { 0 };
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	int ret = 0;

	switch (vsi->type) {
	case ICE_VSI_PF:
		ctxt.flags = ICE_AQ_VSI_TYPE_PF;
		break;
	default:
		return -ENODEV;
	}

	ice_set_dflt_vsi_ctx(&ctxt);
	/* if the switch is in VEB mode, allow VSI loopback */
	if (vsi->vsw->bridge_mode == BRIDGE_MODE_VEB)
		ctxt.info.sw_flags |= ICE_AQ_VSI_SW_FLAG_ALLOW_LB;

	/* Set LUT type and HASH type if RSS is enabled */
	if (test_bit(ICE_FLAG_RSS_ENA, pf->flags))
		ice_set_rss_vsi_ctx(&ctxt, vsi);

	ctxt.info.sw_id = vsi->port_info->sw_id;
	ice_vsi_setup_q_map(vsi, &ctxt);

	ret = ice_add_vsi(hw, vsi->idx, &ctxt, NULL);
	if (ret) {
		dev_err(&pf->pdev->dev,
			"Add VSI failed, err %d\n", ret);
		return -EIO;
	}

	/* keep context for update VSI operations */
	vsi->info = ctxt.info;

	/* record VSI number returned */
	vsi->vsi_num = ctxt.vsi_num;

	return ret;
}

/**
 * ice_vsi_clear_rings - Deallocates the Tx and Rx rings for VSI
 * @vsi: the VSI having rings deallocated
 */
void ice_vsi_clear_rings(struct ice_vsi *vsi)
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
int ice_vsi_alloc_rings(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	int i;

	/* Allocate tx_rings */
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
		ring->count = vsi->num_desc;
		vsi->tx_rings[i] = ring;
	}

	/* Allocate rx_rings */
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
		ring->count = vsi->num_desc;
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
void ice_vsi_map_rings_to_vectors(struct ice_vsi *vsi)
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
 * ice_add_mac_to_list - Add a mac address filter entry to the list
 * @vsi: the VSI to be forwarded to
 * @add_list: pointer to the list which contains MAC filter entries
 * @macaddr: the MAC address to be added.
 *
 * Adds mac address filter entry to the temp list
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
	tmp->fltr_info.src = vsi->vsi_num;
	tmp->fltr_info.lkup_type = ICE_SW_LKUP_MAC;
	tmp->fltr_info.fltr_act = ICE_FWD_TO_VSI;
	tmp->fltr_info.fwd_id.vsi_id = vsi->vsi_num;
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
 * @vid: VLAN id to be added
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
	tmp->fltr_info.src = vsi->vsi_num;
	tmp->fltr_info.fwd_id.vsi_id = vsi->vsi_num;
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
 * @vid: VLAN id to be removed
 *
 * Returns 0 on success and negative on failure
 */
int ice_vsi_kill_vlan(struct ice_vsi *vsi, u16 vid)
{
	struct ice_fltr_list_entry *list;
	struct ice_pf *pf = vsi->back;
	LIST_HEAD(tmp_add_list);
	int status = 0;

	list = devm_kzalloc(&pf->pdev->dev, sizeof(*list), GFP_KERNEL);
	if (!list)
		return -ENOMEM;

	list->fltr_info.lkup_type = ICE_SW_LKUP_VLAN;
	list->fltr_info.fwd_id.vsi_id = vsi->vsi_num;
	list->fltr_info.fltr_act = ICE_FWD_TO_VSI;
	list->fltr_info.l_data.vlan.vlan_id = vid;
	list->fltr_info.flag = ICE_FLTR_TX;
	list->fltr_info.src = vsi->vsi_num;

	INIT_LIST_HEAD(&list->list_entry);
	list_add(&list->list_entry, &tmp_add_list);

	if (ice_remove_vlan(&pf->hw, &tmp_add_list)) {
		dev_err(&pf->pdev->dev, "Error removing VLAN %d on vsi %i\n",
			vid, vsi->vsi_num);
		status = -EIO;
	}

	ice_free_fltr_list(&pf->pdev->dev, &tmp_add_list);
	return status;
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
	int err = 0;
	u16 i;

	if (vsi->netdev && vsi->netdev->mtu > ETH_DATA_LEN)
		vsi->max_frame = vsi->netdev->mtu +
			ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;
	else
		vsi->max_frame = ICE_RXBUF_2048;

	vsi->rx_buf_len = ICE_RXBUF_2048;
	/* set up individual rings */
	for (i = 0; i < vsi->num_rxq && !err; i++)
		err = ice_setup_rx_ctx(vsi->rx_rings[i]);

	if (err) {
		dev_err(&vsi->back->pdev->dev, "ice_setup_rx_ctx failed\n");
		return -EIO;
	}
	return err;
}

/**
 * ice_vsi_cfg_txqs - Configure the VSI for Tx
 * @vsi: the VSI being configured
 *
 * Return 0 on success and a negative value on error
 * Configure the Tx VSI for operation.
 */
int ice_vsi_cfg_txqs(struct ice_vsi *vsi)
{
	struct ice_aqc_add_tx_qgrp *qg_buf;
	struct ice_aqc_add_txqs_perq *txq;
	struct ice_pf *pf = vsi->back;
	enum ice_status status;
	u16 buf_len, i, pf_q;
	int err = 0, tc = 0;
	u8 num_q_grps;

	buf_len = sizeof(struct ice_aqc_add_tx_qgrp);
	qg_buf = devm_kzalloc(&pf->pdev->dev, buf_len, GFP_KERNEL);
	if (!qg_buf)
		return -ENOMEM;

	if (vsi->num_txq > ICE_MAX_TXQ_PER_TXQG) {
		err = -EINVAL;
		goto err_cfg_txqs;
	}
	qg_buf->num_txqs = 1;
	num_q_grps = 1;

	/* set up and configure the Tx queues */
	ice_for_each_txq(vsi, i) {
		struct ice_tlan_ctx tlan_ctx = { 0 };

		pf_q = vsi->txq_map[i];
		ice_setup_tx_ctx(vsi->tx_rings[i], &tlan_ctx, pf_q);
		/* copy context contents into the qg_buf */
		qg_buf->txqs[0].txq_id = cpu_to_le16(pf_q);
		ice_set_ctx((u8 *)&tlan_ctx, qg_buf->txqs[0].txq_ctx,
			    ice_tlan_ctx_info);

		/* init queue specific tail reg. It is referred as transmit
		 * comm scheduler queue doorbell.
		 */
		vsi->tx_rings[i]->tail = pf->hw.hw_addr + QTX_COMM_DBELL(pf_q);
		status = ice_ena_vsi_txq(vsi->port_info, vsi->vsi_num, tc,
					 num_q_grps, qg_buf, buf_len, NULL);
		if (status) {
			dev_err(&vsi->back->pdev->dev,
				"Failed to set LAN Tx queue context, error: %d\n",
				status);
			err = -ENODEV;
			goto err_cfg_txqs;
		}

		/* Add Tx Queue TEID into the VSI Tx ring from the response
		 * This will complete configuring and enabling the queue.
		 */
		txq = &qg_buf->txqs[0];
		if (pf_q == le16_to_cpu(txq->txq_id))
			vsi->tx_rings[i]->txq_teid =
				le32_to_cpu(txq->q_teid);
	}
err_cfg_txqs:
	devm_kfree(&pf->pdev->dev, qg_buf);
	return err;
}

/**
 * ice_vsi_cfg_msix - MSIX mode Interrupt Config in the HW
 * @vsi: the VSI being configured
 */
void ice_vsi_cfg_msix(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	u16 vector = vsi->base_vector;
	struct ice_hw *hw = &pf->hw;
	u32 txq = 0, rxq = 0;
	int i, q, itr;
	u8 itr_gran;

	for (i = 0; i < vsi->num_q_vectors; i++, vector++) {
		struct ice_q_vector *q_vector = vsi->q_vectors[i];

		itr_gran = hw->itr_gran_200;

		if (q_vector->num_ring_rx) {
			q_vector->rx.itr =
				ITR_TO_REG(vsi->rx_rings[rxq]->rx_itr_setting,
					   itr_gran);
			q_vector->rx.latency_range = ICE_LOW_LATENCY;
		}

		if (q_vector->num_ring_tx) {
			q_vector->tx.itr =
				ITR_TO_REG(vsi->tx_rings[txq]->tx_itr_setting,
					   itr_gran);
			q_vector->tx.latency_range = ICE_LOW_LATENCY;
		}
		wr32(hw, GLINT_ITR(ICE_RX_ITR, vector), q_vector->rx.itr);
		wr32(hw, GLINT_ITR(ICE_TX_ITR, vector), q_vector->tx.itr);

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
			u32 val;

			itr = ICE_ITR_NONE;
			val = QINT_TQCTL_CAUSE_ENA_M |
			      (itr << QINT_TQCTL_ITR_INDX_S)  |
			      (vector << QINT_TQCTL_MSIX_INDX_S);
			wr32(hw, QINT_TQCTL(vsi->txq_map[txq]), val);
			txq++;
		}

		for (q = 0; q < q_vector->num_ring_rx; q++) {
			u32 val;

			itr = ICE_ITR_NONE;
			val = QINT_RQCTL_CAUSE_ENA_M |
			      (itr << QINT_RQCTL_ITR_INDX_S)  |
			      (vector << QINT_RQCTL_MSIX_INDX_S);
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
	struct ice_vsi_ctx ctxt = { 0 };
	enum ice_status status;

	/* Here we are configuring the VSI to let the driver add VLAN tags by
	 * setting vlan_flags to ICE_AQ_VSI_VLAN_MODE_ALL. The actual VLAN tag
	 * insertion happens in the Tx hot path, in ice_tx_map.
	 */
	ctxt.info.vlan_flags = ICE_AQ_VSI_VLAN_MODE_ALL;

	ctxt.info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID);
	ctxt.vsi_num = vsi->vsi_num;

	status = ice_aq_update_vsi(hw, &ctxt, NULL);
	if (status) {
		dev_err(dev, "update VSI for VLAN insert failed, err %d aq_err %d\n",
			status, hw->adminq.sq_last_status);
		return -EIO;
	}

	vsi->info.vlan_flags = ctxt.info.vlan_flags;
	return 0;
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
	struct ice_vsi_ctx ctxt = { 0 };
	enum ice_status status;

	/* Here we are configuring what the VSI should do with the VLAN tag in
	 * the Rx packet. We can either leave the tag in the packet or put it in
	 * the Rx descriptor.
	 */
	if (ena) {
		/* Strip VLAN tag from Rx packet and put it in the desc */
		ctxt.info.vlan_flags = ICE_AQ_VSI_VLAN_EMOD_STR_BOTH;
	} else {
		/* Disable stripping. Leave tag in packet */
		ctxt.info.vlan_flags = ICE_AQ_VSI_VLAN_EMOD_NOTHING;
	}

	/* Allow all packets untagged/tagged */
	ctxt.info.vlan_flags |= ICE_AQ_VSI_VLAN_MODE_ALL;

	ctxt.info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID);
	ctxt.vsi_num = vsi->vsi_num;

	status = ice_aq_update_vsi(hw, &ctxt, NULL);
	if (status) {
		dev_err(dev, "update VSI for VLAN strip failed, ena = %d err %d aq_err %d\n",
			ena, status, hw->adminq.sq_last_status);
		return -EIO;
	}

	vsi->info.vlan_flags = ctxt.info.vlan_flags;
	return 0;
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
 */
int ice_vsi_stop_tx_rings(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	u32 *q_teids, val;
	u16 *q_ids, i;
	int err = 0;

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

	/* set up the Tx queue list to be disabled */
	ice_for_each_txq(vsi, i) {
		u16 v_idx;

		if (!vsi->tx_rings || !vsi->tx_rings[i]) {
			err = -EINVAL;
			goto err_out;
		}

		q_ids[i] = vsi->txq_map[i];
		q_teids[i] = vsi->tx_rings[i]->txq_teid;

		/* clear cause_ena bit for disabled queues */
		val = rd32(hw, QINT_TQCTL(vsi->tx_rings[i]->reg_idx));
		val &= ~QINT_TQCTL_CAUSE_ENA_M;
		wr32(hw, QINT_TQCTL(vsi->tx_rings[i]->reg_idx), val);

		/* software is expected to wait for 100 ns */
		ndelay(100);

		/* trigger a software interrupt for the vector associated to
		 * the queue to schedule NAPI handler
		 */
		v_idx = vsi->tx_rings[i]->q_vector->v_idx;
		wr32(hw, GLINT_DYN_CTL(vsi->base_vector + v_idx),
		     GLINT_DYN_CTL_SWINT_TRIG_M | GLINT_DYN_CTL_INTENA_MSK_M);
	}
	status = ice_dis_vsi_txq(vsi->port_info, vsi->num_txq, q_ids, q_teids,
				 NULL);
	/* if the disable queue command was exercised during an active reset
	 * flow, ICE_ERR_RESET_ONGOING is returned. This is not an error as
	 * the reset operation disables queues at the hardware level anyway.
	 */
	if (status == ICE_ERR_RESET_ONGOING) {
		dev_info(&pf->pdev->dev,
			 "Reset in progress. LAN Tx queues already disabled\n");
	} else if (status) {
		dev_err(&pf->pdev->dev,
			"Failed to disable LAN Tx queues, error: %d\n",
			status);
		err = -ENODEV;
	}

err_out:
	devm_kfree(&pf->pdev->dev, q_ids);

err_alloc_q_ids:
	devm_kfree(&pf->pdev->dev, q_teids);

	return err;
}

/**
 * ice_cfg_vlan_pruning - enable or disable VLAN pruning on the VSI
 * @vsi: VSI to enable or disable VLAN pruning on
 * @ena: set to true to enable VLAN pruning and false to disable it
 *
 * returns 0 if VSI is updated, negative otherwise
 */
int ice_cfg_vlan_pruning(struct ice_vsi *vsi, bool ena)
{
	struct ice_vsi_ctx *ctxt;
	struct device *dev;
	int status;

	if (!vsi)
		return -EINVAL;

	dev = &vsi->back->pdev->dev;
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

	ctxt->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_SECURITY_VALID |
						ICE_AQ_VSI_PROP_SW_VALID);
	ctxt->vsi_num = vsi->vsi_num;
	status = ice_aq_update_vsi(&vsi->back->hw, ctxt, NULL);
	if (status) {
		netdev_err(vsi->netdev, "%sabling VLAN pruning on VSI %d failed, err = %d, aq_err = %d\n",
			   ena ? "Ena" : "Dis", vsi->vsi_num, status,
			   vsi->back->hw.adminq.sq_last_status);
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

/**
 * ice_vsi_release_msix - Clear the queue to Interrupt mapping in HW
 * @vsi: the VSI being cleaned up
 */
static void ice_vsi_release_msix(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	u16 vector = vsi->base_vector;
	struct ice_hw *hw = &pf->hw;
	u32 txq = 0;
	u32 rxq = 0;
	int i, q;

	for (i = 0; i < vsi->num_q_vectors; i++, vector++) {
		struct ice_q_vector *q_vector = vsi->q_vectors[i];

		wr32(hw, GLINT_ITR(ICE_RX_ITR, vector), 0);
		wr32(hw, GLINT_ITR(ICE_TX_ITR, vector), 0);
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
	int base = vsi->base_vector;

	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags)) {
		int i;

		if (!vsi->q_vectors || !vsi->irqs_ready)
			return;

		vsi->irqs_ready = false;
		for (i = 0; i < vsi->num_q_vectors; i++) {
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
		ice_vsi_release_msix(vsi);
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
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	int base = vsi->base_vector;
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
		for (i = vsi->base_vector;
		     i < (vsi->num_q_vectors + vsi->base_vector); i++)
			wr32(hw, GLINT_DYN_CTL(i), 0);

		ice_flush(hw);
		for (i = 0; i < vsi->num_q_vectors; i++)
			synchronize_irq(pf->msix_entries[i + base].vector);
	}
}

/**
 * ice_is_reset_recovery_pending - schedule a reset
 * @state: pf state field
 */
bool ice_is_reset_recovery_pending(unsigned long *state)
{
	return test_bit(__ICE_RESET_RECOVERY_PENDING, state);
}
