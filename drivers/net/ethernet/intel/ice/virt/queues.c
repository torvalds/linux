// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022, Intel Corporation. */

#include "virtchnl.h"
#include "queues.h"
#include "ice_vf_lib_private.h"
#include "ice.h"
#include "ice_base.h"
#include "ice_lib.h"

/**
 * ice_vc_get_max_frame_size - get max frame size allowed for VF
 * @vf: VF used to determine max frame size
 *
 * Max frame size is determined based on the current port's max frame size and
 * whether a port VLAN is configured on this VF. The VF is not aware whether
 * it's in a port VLAN so the PF needs to account for this in max frame size
 * checks and sending the max frame size to the VF.
 */
u16 ice_vc_get_max_frame_size(struct ice_vf *vf)
{
	struct ice_port_info *pi = ice_vf_get_port_info(vf);
	u16 max_frame_size;

	max_frame_size = pi->phy.link_info.max_frame_size;

	if (ice_vf_is_port_vlan_ena(vf))
		max_frame_size -= VLAN_HLEN;

	return max_frame_size;
}

/**
 * ice_vc_isvalid_q_id
 * @vsi: VSI to check queue ID against
 * @qid: VSI relative queue ID
 *
 * check for the valid queue ID
 */
static bool ice_vc_isvalid_q_id(struct ice_vsi *vsi, u16 qid)
{
	/* allocated Tx and Rx queues should be always equal for VF VSI */
	return qid < vsi->alloc_txq;
}

/**
 * ice_vc_isvalid_ring_len
 * @ring_len: length of ring
 *
 * check for the valid ring count, should be multiple of ICE_REQ_DESC_MULTIPLE
 * or zero
 */
static bool ice_vc_isvalid_ring_len(u16 ring_len)
{
	return ring_len == 0 ||
	       (ring_len >= ICE_MIN_NUM_DESC &&
		ring_len <= ICE_MAX_NUM_DESC_E810 &&
		!(ring_len % ICE_REQ_DESC_MULTIPLE));
}

/**
 * ice_vf_cfg_qs_bw - Configure per queue bandwidth
 * @vf: pointer to the VF info
 * @num_queues: number of queues to be configured
 *
 * Configure per queue bandwidth.
 *
 * Return: 0 on success or negative error value.
 */
static int ice_vf_cfg_qs_bw(struct ice_vf *vf, u16 num_queues)
{
	struct ice_hw *hw = &vf->pf->hw;
	struct ice_vsi *vsi;
	int ret;
	u16 i;

	vsi = ice_get_vf_vsi(vf);
	if (!vsi)
		return -EINVAL;

	for (i = 0; i < num_queues; i++) {
		u32 p_rate, min_rate;
		u8 tc;

		p_rate = vf->qs_bw[i].peak;
		min_rate = vf->qs_bw[i].committed;
		tc = vf->qs_bw[i].tc;
		if (p_rate)
			ret = ice_cfg_q_bw_lmt(hw->port_info, vsi->idx, tc,
					       vf->qs_bw[i].queue_id,
					       ICE_MAX_BW, p_rate);
		else
			ret = ice_cfg_q_bw_dflt_lmt(hw->port_info, vsi->idx, tc,
						    vf->qs_bw[i].queue_id,
						    ICE_MAX_BW);
		if (ret)
			return ret;

		if (min_rate)
			ret = ice_cfg_q_bw_lmt(hw->port_info, vsi->idx, tc,
					       vf->qs_bw[i].queue_id,
					       ICE_MIN_BW, min_rate);
		else
			ret = ice_cfg_q_bw_dflt_lmt(hw->port_info, vsi->idx, tc,
						    vf->qs_bw[i].queue_id,
						    ICE_MIN_BW);

		if (ret)
			return ret;
	}

	return 0;
}

/**
 * ice_vf_cfg_q_quanta_profile - Configure quanta profile
 * @vf: pointer to the VF info
 * @quanta_prof_idx: pointer to the quanta profile index
 * @quanta_size: quanta size to be set
 *
 * This function chooses available quanta profile and configures the register.
 * The quanta profile is evenly divided by the number of device ports, and then
 * available to the specific PF and VFs. The first profile for each PF is a
 * reserved default profile. Only quanta size of the rest unused profile can be
 * modified.
 *
 * Return: 0 on success or negative error value.
 */
static int ice_vf_cfg_q_quanta_profile(struct ice_vf *vf, u16 quanta_size,
				       u16 *quanta_prof_idx)
{
	const u16 n_desc = calc_quanta_desc(quanta_size);
	struct ice_hw *hw = &vf->pf->hw;
	const u16 n_cmd = 2 * n_desc;
	struct ice_pf *pf = vf->pf;
	u16 per_pf, begin_id;
	u8 n_used;
	u32 reg;

	begin_id = (GLCOMM_QUANTA_PROF_MAX_INDEX + 1) / hw->dev_caps.num_funcs *
		   hw->logical_pf_id;

	if (quanta_size == ICE_DFLT_QUANTA) {
		*quanta_prof_idx = begin_id;
	} else {
		per_pf = (GLCOMM_QUANTA_PROF_MAX_INDEX + 1) /
			 hw->dev_caps.num_funcs;
		n_used = pf->num_quanta_prof_used;
		if (n_used < per_pf) {
			*quanta_prof_idx = begin_id + 1 + n_used;
			pf->num_quanta_prof_used++;
		} else {
			return -EINVAL;
		}
	}

	reg = FIELD_PREP(GLCOMM_QUANTA_PROF_QUANTA_SIZE_M, quanta_size) |
	      FIELD_PREP(GLCOMM_QUANTA_PROF_MAX_CMD_M, n_cmd) |
	      FIELD_PREP(GLCOMM_QUANTA_PROF_MAX_DESC_M, n_desc);
	wr32(hw, GLCOMM_QUANTA_PROF(*quanta_prof_idx), reg);

	return 0;
}

/**
 * ice_vc_validate_vqs_bitmaps - validate Rx/Tx queue bitmaps from VIRTCHNL
 * @vqs: virtchnl_queue_select structure containing bitmaps to validate
 *
 * Return true on successful validation, else false
 */
static bool ice_vc_validate_vqs_bitmaps(struct virtchnl_queue_select *vqs)
{
	if ((!vqs->rx_queues && !vqs->tx_queues) ||
	    vqs->rx_queues >= BIT(ICE_MAX_RSS_QS_PER_VF) ||
	    vqs->tx_queues >= BIT(ICE_MAX_RSS_QS_PER_VF))
		return false;

	return true;
}

/**
 * ice_vf_ena_txq_interrupt - enable Tx queue interrupt via QINT_TQCTL
 * @vsi: VSI of the VF to configure
 * @q_idx: VF queue index used to determine the queue in the PF's space
 */
void ice_vf_ena_txq_interrupt(struct ice_vsi *vsi, u32 q_idx)
{
	struct ice_hw *hw = &vsi->back->hw;
	u32 pfq = vsi->txq_map[q_idx];
	u32 reg;

	reg = rd32(hw, QINT_TQCTL(pfq));

	/* MSI-X index 0 in the VF's space is always for the OICR, which means
	 * this is most likely a poll mode VF driver, so don't enable an
	 * interrupt that was never configured via VIRTCHNL_OP_CONFIG_IRQ_MAP
	 */
	if (!(reg & QINT_TQCTL_MSIX_INDX_M))
		return;

	wr32(hw, QINT_TQCTL(pfq), reg | QINT_TQCTL_CAUSE_ENA_M);
}

/**
 * ice_vf_ena_rxq_interrupt - enable Tx queue interrupt via QINT_RQCTL
 * @vsi: VSI of the VF to configure
 * @q_idx: VF queue index used to determine the queue in the PF's space
 */
void ice_vf_ena_rxq_interrupt(struct ice_vsi *vsi, u32 q_idx)
{
	struct ice_hw *hw = &vsi->back->hw;
	u32 pfq = vsi->rxq_map[q_idx];
	u32 reg;

	reg = rd32(hw, QINT_RQCTL(pfq));

	/* MSI-X index 0 in the VF's space is always for the OICR, which means
	 * this is most likely a poll mode VF driver, so don't enable an
	 * interrupt that was never configured via VIRTCHNL_OP_CONFIG_IRQ_MAP
	 */
	if (!(reg & QINT_RQCTL_MSIX_INDX_M))
		return;

	wr32(hw, QINT_RQCTL(pfq), reg | QINT_RQCTL_CAUSE_ENA_M);
}

/**
 * ice_vc_ena_qs_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to enable all or specific queue(s)
 */
int ice_vc_ena_qs_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_queue_select *vqs =
	    (struct virtchnl_queue_select *)msg;
	struct ice_vsi *vsi;
	unsigned long q_map;
	u16 vf_q_id;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vqs->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_validate_vqs_bitmaps(vqs)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	/* Enable only Rx rings, Tx rings were enabled by the FW when the
	 * Tx queue group list was configured and the context bits were
	 * programmed using ice_vsi_cfg_txqs
	 */
	q_map = vqs->rx_queues;
	for_each_set_bit(vf_q_id, &q_map, ICE_MAX_RSS_QS_PER_VF) {
		if (!ice_vc_isvalid_q_id(vsi, vf_q_id)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		/* Skip queue if enabled */
		if (test_bit(vf_q_id, vf->rxq_ena))
			continue;

		if (ice_vsi_ctrl_one_rx_ring(vsi, true, vf_q_id, true)) {
			dev_err(ice_pf_to_dev(vsi->back), "Failed to enable Rx ring %d on VSI %d\n",
				vf_q_id, vsi->vsi_num);
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		ice_vf_ena_rxq_interrupt(vsi, vf_q_id);
		set_bit(vf_q_id, vf->rxq_ena);
	}

	q_map = vqs->tx_queues;
	for_each_set_bit(vf_q_id, &q_map, ICE_MAX_RSS_QS_PER_VF) {
		if (!ice_vc_isvalid_q_id(vsi, vf_q_id)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		/* Skip queue if enabled */
		if (test_bit(vf_q_id, vf->txq_ena))
			continue;

		ice_vf_ena_txq_interrupt(vsi, vf_q_id);
		set_bit(vf_q_id, vf->txq_ena);
	}

	/* Set flag to indicate that queues are enabled */
	if (v_ret == VIRTCHNL_STATUS_SUCCESS)
		set_bit(ICE_VF_STATE_QS_ENA, vf->vf_states);

error_param:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ENABLE_QUEUES, v_ret,
				     NULL, 0);
}

/**
 * ice_vf_vsi_dis_single_txq - disable a single Tx queue
 * @vf: VF to disable queue for
 * @vsi: VSI for the VF
 * @q_id: VF relative (0-based) queue ID
 *
 * Attempt to disable the Tx queue passed in. If the Tx queue was successfully
 * disabled then clear q_id bit in the enabled queues bitmap and return
 * success. Otherwise return error.
 */
int ice_vf_vsi_dis_single_txq(struct ice_vf *vf, struct ice_vsi *vsi, u16 q_id)
{
	struct ice_txq_meta txq_meta = { 0 };
	struct ice_tx_ring *ring;
	int err;

	if (!test_bit(q_id, vf->txq_ena))
		dev_dbg(ice_pf_to_dev(vsi->back), "Queue %u on VSI %u is not enabled, but stopping it anyway\n",
			q_id, vsi->vsi_num);

	ring = vsi->tx_rings[q_id];
	if (!ring)
		return -EINVAL;

	ice_fill_txq_meta(vsi, ring, &txq_meta);

	err = ice_vsi_stop_tx_ring(vsi, ICE_NO_RESET, vf->vf_id, ring, &txq_meta);
	if (err) {
		dev_err(ice_pf_to_dev(vsi->back), "Failed to stop Tx ring %d on VSI %d\n",
			q_id, vsi->vsi_num);
		return err;
	}

	/* Clear enabled queues flag */
	clear_bit(q_id, vf->txq_ena);

	return 0;
}

/**
 * ice_vc_dis_qs_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to disable all or specific queue(s)
 */
int ice_vc_dis_qs_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_queue_select *vqs =
	    (struct virtchnl_queue_select *)msg;
	struct ice_vsi *vsi;
	unsigned long q_map;
	u16 vf_q_id;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states) &&
	    !test_bit(ICE_VF_STATE_QS_ENA, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vqs->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_validate_vqs_bitmaps(vqs)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (vqs->tx_queues) {
		q_map = vqs->tx_queues;

		for_each_set_bit(vf_q_id, &q_map, ICE_MAX_RSS_QS_PER_VF) {
			if (!ice_vc_isvalid_q_id(vsi, vf_q_id)) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				goto error_param;
			}

			if (ice_vf_vsi_dis_single_txq(vf, vsi, vf_q_id)) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				goto error_param;
			}
		}
	}

	q_map = vqs->rx_queues;
	/* speed up Rx queue disable by batching them if possible */
	if (q_map &&
	    bitmap_equal(&q_map, vf->rxq_ena, ICE_MAX_RSS_QS_PER_VF)) {
		if (ice_vsi_stop_all_rx_rings(vsi)) {
			dev_err(ice_pf_to_dev(vsi->back), "Failed to stop all Rx rings on VSI %d\n",
				vsi->vsi_num);
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		bitmap_zero(vf->rxq_ena, ICE_MAX_RSS_QS_PER_VF);
	} else if (q_map) {
		for_each_set_bit(vf_q_id, &q_map, ICE_MAX_RSS_QS_PER_VF) {
			if (!ice_vc_isvalid_q_id(vsi, vf_q_id)) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				goto error_param;
			}

			/* Skip queue if not enabled */
			if (!test_bit(vf_q_id, vf->rxq_ena))
				continue;

			if (ice_vsi_ctrl_one_rx_ring(vsi, false, vf_q_id,
						     true)) {
				dev_err(ice_pf_to_dev(vsi->back), "Failed to stop Rx ring %d on VSI %d\n",
					vf_q_id, vsi->vsi_num);
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				goto error_param;
			}

			/* Clear enabled queues flag */
			clear_bit(vf_q_id, vf->rxq_ena);
		}
	}

	/* Clear enabled queues flag */
	if (v_ret == VIRTCHNL_STATUS_SUCCESS && ice_vf_has_no_qs_ena(vf))
		clear_bit(ICE_VF_STATE_QS_ENA, vf->vf_states);

error_param:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DISABLE_QUEUES, v_ret,
				     NULL, 0);
}

/**
 * ice_cfg_interrupt
 * @vf: pointer to the VF info
 * @vsi: the VSI being configured
 * @map: vector map for mapping vectors to queues
 * @q_vector: structure for interrupt vector
 * configure the IRQ to queue map
 */
static enum virtchnl_status_code
ice_cfg_interrupt(struct ice_vf *vf, struct ice_vsi *vsi,
		  struct virtchnl_vector_map *map,
		  struct ice_q_vector *q_vector)
{
	u16 vsi_q_id, vsi_q_id_idx;
	unsigned long qmap;

	q_vector->num_ring_rx = 0;
	q_vector->num_ring_tx = 0;

	qmap = map->rxq_map;
	for_each_set_bit(vsi_q_id_idx, &qmap, ICE_MAX_RSS_QS_PER_VF) {
		vsi_q_id = vsi_q_id_idx;

		if (!ice_vc_isvalid_q_id(vsi, vsi_q_id))
			return VIRTCHNL_STATUS_ERR_PARAM;

		q_vector->num_ring_rx++;
		q_vector->rx.itr_idx = map->rxitr_idx;
		vsi->rx_rings[vsi_q_id]->q_vector = q_vector;
		ice_cfg_rxq_interrupt(vsi, vsi_q_id,
				      q_vector->vf_reg_idx,
				      q_vector->rx.itr_idx);
	}

	qmap = map->txq_map;
	for_each_set_bit(vsi_q_id_idx, &qmap, ICE_MAX_RSS_QS_PER_VF) {
		vsi_q_id = vsi_q_id_idx;

		if (!ice_vc_isvalid_q_id(vsi, vsi_q_id))
			return VIRTCHNL_STATUS_ERR_PARAM;

		q_vector->num_ring_tx++;
		q_vector->tx.itr_idx = map->txitr_idx;
		vsi->tx_rings[vsi_q_id]->q_vector = q_vector;
		ice_cfg_txq_interrupt(vsi, vsi_q_id,
				      q_vector->vf_reg_idx,
				      q_vector->tx.itr_idx);
	}

	return VIRTCHNL_STATUS_SUCCESS;
}

/**
 * ice_vc_cfg_irq_map_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to configure the IRQ to queue map
 */
int ice_vc_cfg_irq_map_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	u16 num_q_vectors_mapped, vsi_id, vector_id;
	struct virtchnl_irq_map_info *irqmap_info;
	struct virtchnl_vector_map *map;
	struct ice_vsi *vsi;
	int i;

	irqmap_info = (struct virtchnl_irq_map_info *)msg;
	num_q_vectors_mapped = irqmap_info->num_vectors;

	/* Check to make sure number of VF vectors mapped is not greater than
	 * number of VF vectors originally allocated, and check that
	 * there is actually at least a single VF queue vector mapped
	 */
	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states) ||
	    vf->num_msix < num_q_vectors_mapped ||
	    !num_q_vectors_mapped) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	for (i = 0; i < num_q_vectors_mapped; i++) {
		struct ice_q_vector *q_vector;

		map = &irqmap_info->vecmap[i];

		vector_id = map->vector_id;
		vsi_id = map->vsi_id;
		/* vector_id is always 0-based for each VF, and can never be
		 * larger than or equal to the max allowed interrupts per VF
		 */
		if (!(vector_id < vf->num_msix) ||
		    !ice_vc_isvalid_vsi_id(vf, vsi_id) ||
		    (!vector_id && (map->rxq_map || map->txq_map))) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		/* No need to map VF miscellaneous or rogue vector */
		if (!vector_id)
			continue;

		/* Subtract non queue vector from vector_id passed by VF
		 * to get actual number of VSI queue vector array index
		 */
		q_vector = vsi->q_vectors[vector_id - ICE_NONQ_VECS_VF];
		if (!q_vector) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		/* lookout for the invalid queue index */
		v_ret = ice_cfg_interrupt(vf, vsi, map, q_vector);
		if (v_ret)
			goto error_param;
	}

error_param:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_IRQ_MAP, v_ret,
				     NULL, 0);
}

/**
 * ice_vc_cfg_q_bw - Configure per queue bandwidth
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer which holds the command descriptor
 *
 * Configure VF queues bandwidth.
 *
 * Return: 0 on success or negative error value.
 */
int ice_vc_cfg_q_bw(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_queues_bw_cfg *qbw =
		(struct virtchnl_queues_bw_cfg *)msg;
	struct ice_vsi *vsi;
	u16 i;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states) ||
	    !ice_vc_isvalid_vsi_id(vf, qbw->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	if (qbw->num_queues > ICE_MAX_RSS_QS_PER_VF ||
	    qbw->num_queues > min_t(u16, vsi->alloc_txq, vsi->alloc_rxq)) {
		dev_err(ice_pf_to_dev(vf->pf), "VF-%d trying to configure more than allocated number of queues: %d\n",
			vf->vf_id, min_t(u16, vsi->alloc_txq, vsi->alloc_rxq));
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	for (i = 0; i < qbw->num_queues; i++) {
		if (qbw->cfg[i].shaper.peak != 0 && vf->max_tx_rate != 0 &&
		    qbw->cfg[i].shaper.peak > vf->max_tx_rate) {
			dev_warn(ice_pf_to_dev(vf->pf), "The maximum queue %d rate limit configuration may not take effect because the maximum TX rate for VF-%d is %d\n",
				 qbw->cfg[i].queue_id, vf->vf_id,
				 vf->max_tx_rate);
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto err;
		}
		if (qbw->cfg[i].shaper.committed != 0 && vf->min_tx_rate != 0 &&
		    qbw->cfg[i].shaper.committed < vf->min_tx_rate) {
			dev_warn(ice_pf_to_dev(vf->pf), "The minimum queue %d rate limit configuration may not take effect because the minimum TX rate for VF-%d is %d\n",
				 qbw->cfg[i].queue_id, vf->vf_id,
				 vf->min_tx_rate);
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto err;
		}
		if (qbw->cfg[i].queue_id > vf->num_vf_qs) {
			dev_warn(ice_pf_to_dev(vf->pf), "VF-%d trying to configure invalid queue_id\n",
				 vf->vf_id);
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto err;
		}
		if (qbw->cfg[i].tc >= ICE_MAX_TRAFFIC_CLASS) {
			dev_warn(ice_pf_to_dev(vf->pf), "VF-%d trying to configure a traffic class higher than allowed\n",
				 vf->vf_id);
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto err;
		}
	}

	for (i = 0; i < qbw->num_queues; i++) {
		vf->qs_bw[i].queue_id = qbw->cfg[i].queue_id;
		vf->qs_bw[i].peak = qbw->cfg[i].shaper.peak;
		vf->qs_bw[i].committed = qbw->cfg[i].shaper.committed;
		vf->qs_bw[i].tc = qbw->cfg[i].tc;
	}

	if (ice_vf_cfg_qs_bw(vf, qbw->num_queues))
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;

err:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_QUEUE_BW,
				    v_ret, NULL, 0);
}

/**
 * ice_vc_cfg_q_quanta - Configure per queue quanta
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer which holds the command descriptor
 *
 * Configure VF queues quanta.
 *
 * Return: 0 on success or negative error value.
 */
int ice_vc_cfg_q_quanta(struct ice_vf *vf, u8 *msg)
{
	u16 quanta_prof_id, quanta_size, start_qid, num_queues, end_qid, i;
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_quanta_cfg *qquanta =
		(struct virtchnl_quanta_cfg *)msg;
	struct ice_vsi *vsi;
	int ret;

	start_qid = qquanta->queue_select.start_queue_id;
	num_queues = qquanta->queue_select.num_queues;

	if (check_add_overflow(start_qid, num_queues, &end_qid)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	if (end_qid > ICE_MAX_RSS_QS_PER_VF ||
	    end_qid > min_t(u16, vsi->alloc_txq, vsi->alloc_rxq)) {
		dev_err(ice_pf_to_dev(vf->pf), "VF-%d trying to configure more than allocated number of queues: %d\n",
			vf->vf_id, min_t(u16, vsi->alloc_txq, vsi->alloc_rxq));
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	quanta_size = qquanta->quanta_size;
	if (quanta_size > ICE_MAX_QUANTA_SIZE ||
	    quanta_size < ICE_MIN_QUANTA_SIZE) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	if (quanta_size % 64) {
		dev_err(ice_pf_to_dev(vf->pf), "quanta size should be the product of 64\n");
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	ret = ice_vf_cfg_q_quanta_profile(vf, quanta_size,
					  &quanta_prof_id);
	if (ret) {
		v_ret = VIRTCHNL_STATUS_ERR_NOT_SUPPORTED;
		goto err;
	}

	for (i = start_qid; i < end_qid; i++)
		vsi->tx_rings[i]->quanta_prof_id = quanta_prof_id;

err:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_QUANTA,
				     v_ret, NULL, 0);
}

/**
 * ice_vc_cfg_qs_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to configure the Rx/Tx queues
 */
int ice_vc_cfg_qs_msg(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_vsi_queue_config_info *qci =
	    (struct virtchnl_vsi_queue_config_info *)msg;
	struct virtchnl_queue_pair_info *qpi;
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi;
	int i = -1, q_idx;
	bool ena_ts;
	u8 act_prt;

	mutex_lock(&pf->lag_mutex);
	act_prt = ice_lag_prepare_vf_reset(pf->lag);

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states))
		goto error_param;

	if (!ice_vc_isvalid_vsi_id(vf, qci->vsi_id))
		goto error_param;

	vsi = ice_get_vf_vsi(vf);
	if (!vsi)
		goto error_param;

	if (qci->num_queue_pairs > ICE_MAX_RSS_QS_PER_VF ||
	    qci->num_queue_pairs > min_t(u16, vsi->alloc_txq, vsi->alloc_rxq)) {
		dev_err(ice_pf_to_dev(pf), "VF-%d requesting more than supported number of queues: %d\n",
			vf->vf_id, min_t(u16, vsi->alloc_txq, vsi->alloc_rxq));
		goto error_param;
	}

	for (i = 0; i < qci->num_queue_pairs; i++) {
		if (!qci->qpair[i].rxq.crc_disable)
			continue;

		if (!(vf->driver_caps & VIRTCHNL_VF_OFFLOAD_CRC) ||
		    vf->vlan_strip_ena)
			goto error_param;
	}

	for (i = 0; i < qci->num_queue_pairs; i++) {
		qpi = &qci->qpair[i];
		if (qpi->txq.vsi_id != qci->vsi_id ||
		    qpi->rxq.vsi_id != qci->vsi_id ||
		    qpi->rxq.queue_id != qpi->txq.queue_id ||
		    qpi->txq.headwb_enabled ||
		    !ice_vc_isvalid_ring_len(qpi->txq.ring_len) ||
		    !ice_vc_isvalid_ring_len(qpi->rxq.ring_len) ||
		    !ice_vc_isvalid_q_id(vsi, qpi->txq.queue_id)) {
			goto error_param;
		}

		q_idx = qpi->rxq.queue_id;

		/* make sure selected "q_idx" is in valid range of queues
		 * for selected "vsi"
		 */
		if (q_idx >= vsi->alloc_txq || q_idx >= vsi->alloc_rxq) {
			goto error_param;
		}

		/* copy Tx queue info from VF into VSI */
		if (qpi->txq.ring_len > 0) {
			vsi->tx_rings[q_idx]->dma = qpi->txq.dma_ring_addr;
			vsi->tx_rings[q_idx]->count = qpi->txq.ring_len;

			/* Disable any existing queue first */
			if (ice_vf_vsi_dis_single_txq(vf, vsi, q_idx))
				goto error_param;

			/* Configure a queue with the requested settings */
			if (ice_vsi_cfg_single_txq(vsi, vsi->tx_rings, q_idx)) {
				dev_warn(ice_pf_to_dev(pf), "VF-%d failed to configure TX queue %d\n",
					 vf->vf_id, q_idx);
				goto error_param;
			}
		}

		/* copy Rx queue info from VF into VSI */
		if (qpi->rxq.ring_len > 0) {
			u16 max_frame_size = ice_vc_get_max_frame_size(vf);
			struct ice_rx_ring *ring = vsi->rx_rings[q_idx];
			u32 rxdid;

			ring->dma = qpi->rxq.dma_ring_addr;
			ring->count = qpi->rxq.ring_len;

			if (qpi->rxq.crc_disable)
				ring->flags |= ICE_RX_FLAGS_CRC_STRIP_DIS;
			else
				ring->flags &= ~ICE_RX_FLAGS_CRC_STRIP_DIS;

			if (qpi->rxq.databuffer_size != 0 &&
			    (qpi->rxq.databuffer_size > ((16 * 1024) - 128) ||
			     qpi->rxq.databuffer_size < 1024))
				goto error_param;
			ring->rx_buf_len = qpi->rxq.databuffer_size;
			if (qpi->rxq.max_pkt_size > max_frame_size ||
			    qpi->rxq.max_pkt_size < 64)
				goto error_param;

			ring->max_frame = qpi->rxq.max_pkt_size;
			/* add space for the port VLAN since the VF driver is
			 * not expected to account for it in the MTU
			 * calculation
			 */
			if (ice_vf_is_port_vlan_ena(vf))
				ring->max_frame += VLAN_HLEN;

			if (ice_vsi_cfg_single_rxq(vsi, q_idx)) {
				dev_warn(ice_pf_to_dev(pf), "VF-%d failed to configure RX queue %d\n",
					 vf->vf_id, q_idx);
				goto error_param;
			}

			/* If Rx flex desc is supported, select RXDID for Rx
			 * queues. Otherwise, use legacy 32byte descriptor
			 * format. Legacy 16byte descriptor is not supported.
			 * If this RXDID is selected, return error.
			 */
			if (vf->driver_caps &
			    VIRTCHNL_VF_OFFLOAD_RX_FLEX_DESC) {
				rxdid = qpi->rxq.rxdid;
				if (!(BIT(rxdid) & pf->supported_rxdids))
					goto error_param;
			} else {
				rxdid = ICE_RXDID_LEGACY_1;
			}

			ena_ts = ((vf->driver_caps &
				  VIRTCHNL_VF_OFFLOAD_RX_FLEX_DESC) &&
				  (vf->driver_caps & VIRTCHNL_VF_CAP_PTP) &&
				  (qpi->rxq.flags & VIRTCHNL_PTP_RX_TSTAMP));

			ice_write_qrxflxp_cntxt(&vsi->back->hw,
						vsi->rxq_map[q_idx], rxdid,
						ICE_RXDID_PRIO, ena_ts);
		}
	}

	ice_lag_complete_vf_reset(pf->lag, act_prt);
	mutex_unlock(&pf->lag_mutex);

	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_VSI_QUEUES,
				     VIRTCHNL_STATUS_SUCCESS, NULL, 0);
error_param:
	/* disable whatever we can */
	for (; i >= 0; i--) {
		if (ice_vsi_ctrl_one_rx_ring(vsi, false, i, true))
			dev_err(ice_pf_to_dev(pf), "VF-%d could not disable RX queue %d\n",
				vf->vf_id, i);
		if (ice_vf_vsi_dis_single_txq(vf, vsi, i))
			dev_err(ice_pf_to_dev(pf), "VF-%d could not disable TX queue %d\n",
				vf->vf_id, i);
	}

	ice_lag_complete_vf_reset(pf->lag, act_prt);
	mutex_unlock(&pf->lag_mutex);

	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_VSI_QUEUES,
				     VIRTCHNL_STATUS_ERR_PARAM, NULL, 0);
}

/**
 * ice_vc_request_qs_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * VFs get a default number of queues but can use this message to request a
 * different number. If the request is successful, PF will reset the VF and
 * return 0. If unsuccessful, PF will send message informing VF of number of
 * available queue pairs via virtchnl message response to VF.
 */
int ice_vc_request_qs_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vf_res_request *vfres =
		(struct virtchnl_vf_res_request *)msg;
	u16 req_queues = vfres->num_queue_pairs;
	struct ice_pf *pf = vf->pf;
	u16 max_allowed_vf_queues;
	u16 tx_rx_queue_left;
	struct device *dev;
	u16 cur_queues;

	dev = ice_pf_to_dev(pf);
	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	cur_queues = vf->num_vf_qs;
	tx_rx_queue_left = min_t(u16, ice_get_avail_txq_count(pf),
				 ice_get_avail_rxq_count(pf));
	max_allowed_vf_queues = tx_rx_queue_left + cur_queues;
	if (!req_queues) {
		dev_err(dev, "VF %d tried to request 0 queues. Ignoring.\n",
			vf->vf_id);
	} else if (req_queues > ICE_MAX_RSS_QS_PER_VF) {
		dev_err(dev, "VF %d tried to request more than %d queues.\n",
			vf->vf_id, ICE_MAX_RSS_QS_PER_VF);
		vfres->num_queue_pairs = ICE_MAX_RSS_QS_PER_VF;
	} else if (req_queues > cur_queues &&
		   req_queues - cur_queues > tx_rx_queue_left) {
		dev_warn(dev, "VF %d requested %u more queues, but only %u left.\n",
			 vf->vf_id, req_queues - cur_queues, tx_rx_queue_left);
		vfres->num_queue_pairs = min_t(u16, max_allowed_vf_queues,
					       ICE_MAX_RSS_QS_PER_VF);
	} else {
		/* request is successful, then reset VF */
		vf->num_req_qs = req_queues;
		ice_reset_vf(vf, ICE_VF_RESET_NOTIFY);
		dev_info(dev, "VF %d granted request of %u queues.\n",
			 vf->vf_id, req_queues);
		return 0;
	}

error_param:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_REQUEST_QUEUES,
				     v_ret, (u8 *)vfres, sizeof(*vfres));
}

