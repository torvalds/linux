// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice.h"
#include "ice_base.h"
#include "ice_flow.h"
#include "ice_lib.h"
#include "ice_fltr.h"
#include "ice_dcb_lib.h"
#include "ice_devlink.h"
#include "ice_vsi_vlan_ops.h"

/**
 * ice_vsi_type_str - maps VSI type enum to string equivalents
 * @vsi_type: VSI type enum
 */
const char *ice_vsi_type_str(enum ice_vsi_type vsi_type)
{
	switch (vsi_type) {
	case ICE_VSI_PF:
		return "ICE_VSI_PF";
	case ICE_VSI_VF:
		return "ICE_VSI_VF";
	case ICE_VSI_CTRL:
		return "ICE_VSI_CTRL";
	case ICE_VSI_CHNL:
		return "ICE_VSI_CHNL";
	case ICE_VSI_LB:
		return "ICE_VSI_LB";
	case ICE_VSI_SWITCHDEV_CTRL:
		return "ICE_VSI_SWITCHDEV_CTRL";
	default:
		return "unknown";
	}
}

/**
 * ice_vsi_ctrl_all_rx_rings - Start or stop a VSI's Rx rings
 * @vsi: the VSI being configured
 * @ena: start or stop the Rx rings
 *
 * First enable/disable all of the Rx rings, flush any remaining writes, and
 * then verify that they have all been enabled/disabled successfully. This will
 * let all of the register writes complete when enabling/disabling the Rx rings
 * before waiting for the change in hardware to complete.
 */
static int ice_vsi_ctrl_all_rx_rings(struct ice_vsi *vsi, bool ena)
{
	int ret = 0;
	u16 i;

	ice_for_each_rxq(vsi, i)
		ice_vsi_ctrl_one_rx_ring(vsi, ena, i, false);

	ice_flush(&vsi->back->hw);

	ice_for_each_rxq(vsi, i) {
		ret = ice_vsi_wait_one_rx_ring(vsi, ena, i);
		if (ret)
			break;
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
	struct device *dev;

	dev = ice_pf_to_dev(pf);
	if (vsi->type == ICE_VSI_CHNL)
		return 0;

	/* allocate memory for both Tx and Rx ring pointers */
	vsi->tx_rings = devm_kcalloc(dev, vsi->alloc_txq,
				     sizeof(*vsi->tx_rings), GFP_KERNEL);
	if (!vsi->tx_rings)
		return -ENOMEM;

	vsi->rx_rings = devm_kcalloc(dev, vsi->alloc_rxq,
				     sizeof(*vsi->rx_rings), GFP_KERNEL);
	if (!vsi->rx_rings)
		goto err_rings;

	/* txq_map needs to have enough space to track both Tx (stack) rings
	 * and XDP rings; at this point vsi->num_xdp_txq might not be set,
	 * so use num_possible_cpus() as we want to always provide XDP ring
	 * per CPU, regardless of queue count settings from user that might
	 * have come from ethtool's set_channels() callback;
	 */
	vsi->txq_map = devm_kcalloc(dev, (vsi->alloc_txq + num_possible_cpus()),
				    sizeof(*vsi->txq_map), GFP_KERNEL);

	if (!vsi->txq_map)
		goto err_txq_map;

	vsi->rxq_map = devm_kcalloc(dev, vsi->alloc_rxq,
				    sizeof(*vsi->rxq_map), GFP_KERNEL);
	if (!vsi->rxq_map)
		goto err_rxq_map;

	/* There is no need to allocate q_vectors for a loopback VSI. */
	if (vsi->type == ICE_VSI_LB)
		return 0;

	/* allocate memory for q_vector pointers */
	vsi->q_vectors = devm_kcalloc(dev, vsi->num_q_vectors,
				      sizeof(*vsi->q_vectors), GFP_KERNEL);
	if (!vsi->q_vectors)
		goto err_vectors;

	vsi->af_xdp_zc_qps = bitmap_zalloc(max_t(int, vsi->alloc_txq, vsi->alloc_rxq), GFP_KERNEL);
	if (!vsi->af_xdp_zc_qps)
		goto err_zc_qps;

	return 0;

err_zc_qps:
	devm_kfree(dev, vsi->q_vectors);
err_vectors:
	devm_kfree(dev, vsi->rxq_map);
err_rxq_map:
	devm_kfree(dev, vsi->txq_map);
err_txq_map:
	devm_kfree(dev, vsi->rx_rings);
err_rings:
	devm_kfree(dev, vsi->tx_rings);
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
	case ICE_VSI_SWITCHDEV_CTRL:
	case ICE_VSI_CTRL:
	case ICE_VSI_LB:
		/* a user could change the values of num_[tr]x_desc using
		 * ethtool -G so we should keep those values instead of
		 * overwriting them with the defaults.
		 */
		if (!vsi->num_rx_desc)
			vsi->num_rx_desc = ICE_DFLT_NUM_RX_DESC;
		if (!vsi->num_tx_desc)
			vsi->num_tx_desc = ICE_DFLT_NUM_TX_DESC;
		break;
	default:
		dev_dbg(ice_pf_to_dev(vsi->back), "Not setting number of Tx/Rx descriptors for VSI type %d\n",
			vsi->type);
		break;
	}
}

/**
 * ice_vsi_set_num_qs - Set number of queues, descriptors and vectors for a VSI
 * @vsi: the VSI being configured
 * @vf: the VF associated with this VSI, if any
 *
 * Return 0 on success and a negative value on error
 */
static void ice_vsi_set_num_qs(struct ice_vsi *vsi, struct ice_vf *vf)
{
	enum ice_vsi_type vsi_type = vsi->type;
	struct ice_pf *pf = vsi->back;

	if (WARN_ON(vsi_type == ICE_VSI_VF && !vf))
		return;

	switch (vsi_type) {
	case ICE_VSI_PF:
		if (vsi->req_txq) {
			vsi->alloc_txq = vsi->req_txq;
			vsi->num_txq = vsi->req_txq;
		} else {
			vsi->alloc_txq = min3(pf->num_lan_msix,
					      ice_get_avail_txq_count(pf),
					      (u16)num_online_cpus());
		}

		pf->num_lan_tx = vsi->alloc_txq;

		/* only 1 Rx queue unless RSS is enabled */
		if (!test_bit(ICE_FLAG_RSS_ENA, pf->flags)) {
			vsi->alloc_rxq = 1;
		} else {
			if (vsi->req_rxq) {
				vsi->alloc_rxq = vsi->req_rxq;
				vsi->num_rxq = vsi->req_rxq;
			} else {
				vsi->alloc_rxq = min3(pf->num_lan_msix,
						      ice_get_avail_rxq_count(pf),
						      (u16)num_online_cpus());
			}
		}

		pf->num_lan_rx = vsi->alloc_rxq;

		vsi->num_q_vectors = min_t(int, pf->num_lan_msix,
					   max_t(int, vsi->alloc_rxq,
						 vsi->alloc_txq));
		break;
	case ICE_VSI_SWITCHDEV_CTRL:
		/* The number of queues for ctrl VSI is equal to number of VFs.
		 * Each ring is associated to the corresponding VF_PR netdev.
		 */
		vsi->alloc_txq = ice_get_num_vfs(pf);
		vsi->alloc_rxq = vsi->alloc_txq;
		vsi->num_q_vectors = 1;
		break;
	case ICE_VSI_VF:
		if (vf->num_req_qs)
			vf->num_vf_qs = vf->num_req_qs;
		vsi->alloc_txq = vf->num_vf_qs;
		vsi->alloc_rxq = vf->num_vf_qs;
		/* pf->vfs.num_msix_per includes (VF miscellaneous vector +
		 * data queue interrupts). Since vsi->num_q_vectors is number
		 * of queues vectors, subtract 1 (ICE_NONQ_VECS_VF) from the
		 * original vector count
		 */
		vsi->num_q_vectors = pf->vfs.num_msix_per - ICE_NONQ_VECS_VF;
		break;
	case ICE_VSI_CTRL:
		vsi->alloc_txq = 1;
		vsi->alloc_rxq = 1;
		vsi->num_q_vectors = 1;
		break;
	case ICE_VSI_CHNL:
		vsi->alloc_txq = 0;
		vsi->alloc_rxq = 0;
		break;
	case ICE_VSI_LB:
		vsi->alloc_txq = 1;
		vsi->alloc_rxq = 1;
		break;
	default:
		dev_warn(ice_pf_to_dev(pf), "Unknown VSI type %d\n", vsi_type);
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
	int status;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return;

	if (vsi->type == ICE_VSI_VF)
		ctxt->vf_num = vsi->vf->vf_id;
	ctxt->vsi_num = vsi->vsi_num;

	memcpy(&ctxt->info, &vsi->info, sizeof(ctxt->info));

	status = ice_free_vsi(&pf->hw, vsi->idx, ctxt, false, NULL);
	if (status)
		dev_err(ice_pf_to_dev(pf), "Failed to delete VSI %i in FW - error: %d\n",
			vsi->vsi_num, status);

	kfree(ctxt);
}

/**
 * ice_vsi_free_arrays - De-allocate queue and vector pointer arrays for the VSI
 * @vsi: pointer to VSI being cleared
 */
static void ice_vsi_free_arrays(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct device *dev;

	dev = ice_pf_to_dev(pf);

	if (vsi->af_xdp_zc_qps) {
		bitmap_free(vsi->af_xdp_zc_qps);
		vsi->af_xdp_zc_qps = NULL;
	}
	/* free the ring and vector containers */
	if (vsi->q_vectors) {
		devm_kfree(dev, vsi->q_vectors);
		vsi->q_vectors = NULL;
	}
	if (vsi->tx_rings) {
		devm_kfree(dev, vsi->tx_rings);
		vsi->tx_rings = NULL;
	}
	if (vsi->rx_rings) {
		devm_kfree(dev, vsi->rx_rings);
		vsi->rx_rings = NULL;
	}
	if (vsi->txq_map) {
		devm_kfree(dev, vsi->txq_map);
		vsi->txq_map = NULL;
	}
	if (vsi->rxq_map) {
		devm_kfree(dev, vsi->rxq_map);
		vsi->rxq_map = NULL;
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
	struct device *dev;

	if (!vsi)
		return 0;

	if (!vsi->back)
		return -EINVAL;

	pf = vsi->back;
	dev = ice_pf_to_dev(pf);

	if (!pf->vsi[vsi->idx] || pf->vsi[vsi->idx] != vsi) {
		dev_dbg(dev, "vsi does not exist at pf->vsi[%d]\n", vsi->idx);
		return -EINVAL;
	}

	mutex_lock(&pf->sw_mutex);
	/* updates the PF for this cleared VSI */

	pf->vsi[vsi->idx] = NULL;
	if (vsi->idx < pf->next_vsi && vsi->type != ICE_VSI_CTRL)
		pf->next_vsi = vsi->idx;
	if (vsi->idx < pf->next_vsi && vsi->type == ICE_VSI_CTRL && vsi->vf)
		pf->next_vsi = vsi->idx;

	ice_vsi_free_arrays(vsi);
	mutex_unlock(&pf->sw_mutex);
	devm_kfree(dev, vsi);

	return 0;
}

/**
 * ice_msix_clean_ctrl_vsi - MSIX mode interrupt handler for ctrl VSI
 * @irq: interrupt number
 * @data: pointer to a q_vector
 */
static irqreturn_t ice_msix_clean_ctrl_vsi(int __always_unused irq, void *data)
{
	struct ice_q_vector *q_vector = (struct ice_q_vector *)data;

	if (!q_vector->tx.tx_ring)
		return IRQ_HANDLED;

#define FDIR_RX_DESC_CLEAN_BUDGET 64
	ice_clean_rx_irq(q_vector->rx.rx_ring, FDIR_RX_DESC_CLEAN_BUDGET);
	ice_clean_ctrl_tx_irq(q_vector->tx.tx_ring);

	return IRQ_HANDLED;
}

/**
 * ice_msix_clean_rings - MSIX mode Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 */
static irqreturn_t ice_msix_clean_rings(int __always_unused irq, void *data)
{
	struct ice_q_vector *q_vector = (struct ice_q_vector *)data;

	if (!q_vector->tx.tx_ring && !q_vector->rx.rx_ring)
		return IRQ_HANDLED;

	q_vector->total_events++;

	napi_schedule(&q_vector->napi);

	return IRQ_HANDLED;
}

static irqreturn_t ice_eswitch_msix_clean_rings(int __always_unused irq, void *data)
{
	struct ice_q_vector *q_vector = (struct ice_q_vector *)data;
	struct ice_pf *pf = q_vector->vsi->back;
	struct ice_vf *vf;
	unsigned int bkt;

	if (!q_vector->tx.tx_ring && !q_vector->rx.rx_ring)
		return IRQ_HANDLED;

	rcu_read_lock();
	ice_for_each_vf_rcu(pf, bkt, vf)
		napi_schedule(&vf->repr->q_vector->napi);
	rcu_read_unlock();

	return IRQ_HANDLED;
}

/**
 * ice_vsi_alloc - Allocates the next available struct VSI in the PF
 * @pf: board private structure
 * @vsi_type: type of VSI
 * @ch: ptr to channel
 * @vf: VF for ICE_VSI_VF and ICE_VSI_CTRL
 *
 * The VF pointer is used for ICE_VSI_VF and ICE_VSI_CTRL. For ICE_VSI_CTRL,
 * it may be NULL in the case there is no association with a VF. For
 * ICE_VSI_VF the VF pointer *must not* be NULL.
 *
 * returns a pointer to a VSI on success, NULL on failure.
 */
static struct ice_vsi *
ice_vsi_alloc(struct ice_pf *pf, enum ice_vsi_type vsi_type,
	      struct ice_channel *ch, struct ice_vf *vf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_vsi *vsi = NULL;

	if (WARN_ON(vsi_type == ICE_VSI_VF && !vf))
		return NULL;

	/* Need to protect the allocation of the VSIs at the PF level */
	mutex_lock(&pf->sw_mutex);

	/* If we have already allocated our maximum number of VSIs,
	 * pf->next_vsi will be ICE_NO_VSI. If not, pf->next_vsi index
	 * is available to be populated
	 */
	if (pf->next_vsi == ICE_NO_VSI) {
		dev_dbg(dev, "out of VSI slots!\n");
		goto unlock_pf;
	}

	vsi = devm_kzalloc(dev, sizeof(*vsi), GFP_KERNEL);
	if (!vsi)
		goto unlock_pf;

	vsi->type = vsi_type;
	vsi->back = pf;
	set_bit(ICE_VSI_DOWN, vsi->state);

	if (vsi_type == ICE_VSI_VF)
		ice_vsi_set_num_qs(vsi, vf);
	else if (vsi_type != ICE_VSI_CHNL)
		ice_vsi_set_num_qs(vsi, NULL);

	switch (vsi->type) {
	case ICE_VSI_SWITCHDEV_CTRL:
		if (ice_vsi_alloc_arrays(vsi))
			goto err_rings;

		/* Setup eswitch MSIX irq handler for VSI */
		vsi->irq_handler = ice_eswitch_msix_clean_rings;
		break;
	case ICE_VSI_PF:
		if (ice_vsi_alloc_arrays(vsi))
			goto err_rings;

		/* Setup default MSIX irq handler for VSI */
		vsi->irq_handler = ice_msix_clean_rings;
		break;
	case ICE_VSI_CTRL:
		if (ice_vsi_alloc_arrays(vsi))
			goto err_rings;

		/* Setup ctrl VSI MSIX irq handler */
		vsi->irq_handler = ice_msix_clean_ctrl_vsi;

		/* For the PF control VSI this is NULL, for the VF control VSI
		 * this will be the first VF to allocate it.
		 */
		vsi->vf = vf;
		break;
	case ICE_VSI_VF:
		if (ice_vsi_alloc_arrays(vsi))
			goto err_rings;
		vsi->vf = vf;
		break;
	case ICE_VSI_CHNL:
		if (!ch)
			goto err_rings;
		vsi->num_rxq = ch->num_rxq;
		vsi->num_txq = ch->num_txq;
		vsi->next_base_q = ch->base_q;
		break;
	case ICE_VSI_LB:
		if (ice_vsi_alloc_arrays(vsi))
			goto err_rings;
		break;
	default:
		dev_warn(dev, "Unknown VSI type %d\n", vsi->type);
		goto unlock_pf;
	}

	if (vsi->type == ICE_VSI_CTRL && !vf) {
		/* Use the last VSI slot as the index for PF control VSI */
		vsi->idx = pf->num_alloc_vsi - 1;
		pf->ctrl_vsi_idx = vsi->idx;
		pf->vsi[vsi->idx] = vsi;
	} else {
		/* fill slot and make note of the index */
		vsi->idx = pf->next_vsi;
		pf->vsi[pf->next_vsi] = vsi;

		/* prepare pf->next_vsi for next use */
		pf->next_vsi = ice_get_free_slot(pf->vsi, pf->num_alloc_vsi,
						 pf->next_vsi);
	}

	if (vsi->type == ICE_VSI_CTRL && vf)
		vf->ctrl_vsi_idx = vsi->idx;
	goto unlock_pf;

err_rings:
	devm_kfree(dev, vsi);
	vsi = NULL;
unlock_pf:
	mutex_unlock(&pf->sw_mutex);
	return vsi;
}

/**
 * ice_alloc_fd_res - Allocate FD resource for a VSI
 * @vsi: pointer to the ice_vsi
 *
 * This allocates the FD resources
 *
 * Returns 0 on success, -EPERM on no-op or -EIO on failure
 */
static int ice_alloc_fd_res(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	u32 g_val, b_val;

	/* Flow Director filters are only allocated/assigned to the PF VSI or
	 * CHNL VSI which passes the traffic. The CTRL VSI is only used to
	 * add/delete filters so resources are not allocated to it
	 */
	if (!test_bit(ICE_FLAG_FD_ENA, pf->flags))
		return -EPERM;

	if (!(vsi->type == ICE_VSI_PF || vsi->type == ICE_VSI_VF ||
	      vsi->type == ICE_VSI_CHNL))
		return -EPERM;

	/* FD filters from guaranteed pool per VSI */
	g_val = pf->hw.func_caps.fd_fltr_guar;
	if (!g_val)
		return -EPERM;

	/* FD filters from best effort pool */
	b_val = pf->hw.func_caps.fd_fltr_best_effort;
	if (!b_val)
		return -EPERM;

	/* PF main VSI gets only 64 FD resources from guaranteed pool
	 * when ADQ is configured.
	 */
#define ICE_PF_VSI_GFLTR	64

	/* determine FD filter resources per VSI from shared(best effort) and
	 * dedicated pool
	 */
	if (vsi->type == ICE_VSI_PF) {
		vsi->num_gfltr = g_val;
		/* if MQPRIO is configured, main VSI doesn't get all FD
		 * resources from guaranteed pool. PF VSI gets 64 FD resources
		 */
		if (test_bit(ICE_FLAG_TC_MQPRIO, pf->flags)) {
			if (g_val < ICE_PF_VSI_GFLTR)
				return -EPERM;
			/* allow bare minimum entries for PF VSI */
			vsi->num_gfltr = ICE_PF_VSI_GFLTR;
		}

		/* each VSI gets same "best_effort" quota */
		vsi->num_bfltr = b_val;
	} else if (vsi->type == ICE_VSI_VF) {
		vsi->num_gfltr = 0;

		/* each VSI gets same "best_effort" quota */
		vsi->num_bfltr = b_val;
	} else {
		struct ice_vsi *main_vsi;
		int numtc;

		main_vsi = ice_get_main_vsi(pf);
		if (!main_vsi)
			return -EPERM;

		if (!main_vsi->all_numtc)
			return -EINVAL;

		/* figure out ADQ numtc */
		numtc = main_vsi->all_numtc - ICE_CHNL_START_TC;

		/* only one TC but still asking resources for channels,
		 * invalid config
		 */
		if (numtc < ICE_CHNL_START_TC)
			return -EPERM;

		g_val -= ICE_PF_VSI_GFLTR;
		/* channel VSIs gets equal share from guaranteed pool */
		vsi->num_gfltr = g_val / numtc;

		/* each VSI gets same "best_effort" quota */
		vsi->num_bfltr = b_val;
	}

	return 0;
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
		.pf_map_size = pf->max_pf_txqs,
		.q_count = vsi->alloc_txq,
		.scatter_count = ICE_MAX_SCATTER_TXQS,
		.vsi_map = vsi->txq_map,
		.vsi_map_offset = 0,
		.mapping_mode = ICE_VSI_MAP_CONTIG
	};
	struct ice_qs_cfg rx_qs_cfg = {
		.qs_mutex = &pf->avail_q_mutex,
		.pf_map = pf->avail_rxqs,
		.pf_map_size = pf->max_pf_rxqs,
		.q_count = vsi->alloc_rxq,
		.scatter_count = ICE_MAX_SCATTER_RXQS,
		.vsi_map = vsi->rxq_map,
		.vsi_map_offset = 0,
		.mapping_mode = ICE_VSI_MAP_CONTIG
	};
	int ret;

	if (vsi->type == ICE_VSI_CHNL)
		return 0;

	ret = __ice_vsi_get_qs(&tx_qs_cfg);
	if (ret)
		return ret;
	vsi->tx_mapping_mode = tx_qs_cfg.mapping_mode;

	ret = __ice_vsi_get_qs(&rx_qs_cfg);
	if (ret)
		return ret;
	vsi->rx_mapping_mode = rx_qs_cfg.mapping_mode;

	return 0;
}

/**
 * ice_vsi_put_qs - Release queues from VSI to PF
 * @vsi: the VSI that is going to release queues
 */
static void ice_vsi_put_qs(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	int i;

	mutex_lock(&pf->avail_q_mutex);

	ice_for_each_alloc_txq(vsi, i) {
		clear_bit(vsi->txq_map[i], pf->avail_txqs);
		vsi->txq_map[i] = ICE_INVAL_Q_INDEX;
	}

	ice_for_each_alloc_rxq(vsi, i) {
		clear_bit(vsi->rxq_map[i], pf->avail_rxqs);
		vsi->rxq_map[i] = ICE_INVAL_Q_INDEX;
	}

	mutex_unlock(&pf->avail_q_mutex);
}

/**
 * ice_is_safe_mode
 * @pf: pointer to the PF struct
 *
 * returns true if driver is in safe mode, false otherwise
 */
bool ice_is_safe_mode(struct ice_pf *pf)
{
	return !test_bit(ICE_FLAG_ADV_FEATURES, pf->flags);
}

/**
 * ice_is_rdma_ena
 * @pf: pointer to the PF struct
 *
 * returns true if RDMA is currently supported, false otherwise
 */
bool ice_is_rdma_ena(struct ice_pf *pf)
{
	return test_bit(ICE_FLAG_RDMA_ENA, pf->flags);
}

/**
 * ice_vsi_clean_rss_flow_fld - Delete RSS configuration
 * @vsi: the VSI being cleaned up
 *
 * This function deletes RSS input set for all flows that were configured
 * for this VSI
 */
static void ice_vsi_clean_rss_flow_fld(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	int status;

	if (ice_is_safe_mode(pf))
		return;

	status = ice_rem_vsi_rss_cfg(&pf->hw, vsi->idx);
	if (status)
		dev_dbg(ice_pf_to_dev(pf), "ice_rem_vsi_rss_cfg failed for vsi = %d, error = %d\n",
			vsi->vsi_num, status);
}

/**
 * ice_rss_clean - Delete RSS related VSI structures and configuration
 * @vsi: the VSI being removed
 */
static void ice_rss_clean(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct device *dev;

	dev = ice_pf_to_dev(pf);

	if (vsi->rss_hkey_user)
		devm_kfree(dev, vsi->rss_hkey_user);
	if (vsi->rss_lut_user)
		devm_kfree(dev, vsi->rss_lut_user);

	ice_vsi_clean_rss_flow_fld(vsi);
	/* remove RSS replay list */
	if (!ice_is_safe_mode(pf))
		ice_rem_vsi_rss_list(&pf->hw, vsi->idx);
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
	case ICE_VSI_CHNL:
	case ICE_VSI_PF:
		/* PF VSI will inherit RSS instance of PF */
		vsi->rss_table_size = (u16)cap->rss_table_size;
		if (vsi->type == ICE_VSI_CHNL)
			vsi->rss_size = min_t(u16, vsi->num_rxq,
					      BIT(cap->rss_table_entry_width));
		else
			vsi->rss_size = min_t(u16, num_online_cpus(),
					      BIT(cap->rss_table_entry_width));
		vsi->rss_lut_type = ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_PF;
		break;
	case ICE_VSI_SWITCHDEV_CTRL:
		vsi->rss_table_size = ICE_VSIQF_HLUT_ARRAY_SIZE;
		vsi->rss_size = min_t(u16, num_online_cpus(),
				      BIT(cap->rss_table_entry_width));
		vsi->rss_lut_type = ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_VSI;
		break;
	case ICE_VSI_VF:
		/* VF VSI will get a small RSS table.
		 * For VSI_LUT, LUT size should be set to 64 bytes.
		 */
		vsi->rss_table_size = ICE_VSIQF_HLUT_ARRAY_SIZE;
		vsi->rss_size = ICE_MAX_RSS_QS_PER_VF;
		vsi->rss_lut_type = ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_VSI;
		break;
	case ICE_VSI_LB:
		break;
	default:
		dev_dbg(ice_pf_to_dev(pf), "Unsupported VSI type %s\n",
			ice_vsi_type_str(vsi->type));
		break;
	}
}

/**
 * ice_set_dflt_vsi_ctx - Set default VSI context before adding a VSI
 * @hw: HW structure used to determine the VLAN mode of the device
 * @ctxt: the VSI context being set
 *
 * This initializes a default VSI context for all sections except the Queues.
 */
static void ice_set_dflt_vsi_ctx(struct ice_hw *hw, struct ice_vsi_ctx *ctxt)
{
	u32 table = 0;

	memset(&ctxt->info, 0, sizeof(ctxt->info));
	/* VSI's should be allocated from shared pool */
	ctxt->alloc_from_pool = true;
	/* Src pruning enabled by default */
	ctxt->info.sw_flags = ICE_AQ_VSI_SW_FLAG_SRC_PRUNE;
	/* Traffic from VSI can be sent to LAN */
	ctxt->info.sw_flags2 = ICE_AQ_VSI_SW_FLAG_LAN_ENA;
	/* allow all untagged/tagged packets by default on Tx */
	ctxt->info.inner_vlan_flags = ((ICE_AQ_VSI_INNER_VLAN_TX_MODE_ALL &
				  ICE_AQ_VSI_INNER_VLAN_TX_MODE_M) >>
				 ICE_AQ_VSI_INNER_VLAN_TX_MODE_S);
	/* SVM - by default bits 3 and 4 in inner_vlan_flags are 0's which
	 * results in legacy behavior (show VLAN, DEI, and UP) in descriptor.
	 *
	 * DVM - leave inner VLAN in packet by default
	 */
	if (ice_is_dvm_ena(hw)) {
		ctxt->info.inner_vlan_flags |=
			ICE_AQ_VSI_INNER_VLAN_EMODE_NOTHING;
		ctxt->info.outer_vlan_flags =
			(ICE_AQ_VSI_OUTER_VLAN_TX_MODE_ALL <<
			 ICE_AQ_VSI_OUTER_VLAN_TX_MODE_S) &
			ICE_AQ_VSI_OUTER_VLAN_TX_MODE_M;
		ctxt->info.outer_vlan_flags |=
			(ICE_AQ_VSI_OUTER_TAG_VLAN_8100 <<
			 ICE_AQ_VSI_OUTER_TAG_TYPE_S) &
			ICE_AQ_VSI_OUTER_TAG_TYPE_M;
	}
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
	u16 offset = 0, qmap = 0, tx_count = 0, pow = 0;
	u16 num_txq_per_tc, num_rxq_per_tc;
	u16 qcount_tx = vsi->alloc_txq;
	u16 qcount_rx = vsi->alloc_rxq;
	u8 netdev_tc = 0;
	int i;

	if (!vsi->tc_cfg.numtc) {
		/* at least TC0 should be enabled by default */
		vsi->tc_cfg.numtc = 1;
		vsi->tc_cfg.ena_tc = 1;
	}

	num_rxq_per_tc = min_t(u16, qcount_rx / vsi->tc_cfg.numtc, ICE_MAX_RXQS_PER_TC);
	if (!num_rxq_per_tc)
		num_rxq_per_tc = 1;
	num_txq_per_tc = qcount_tx / vsi->tc_cfg.numtc;
	if (!num_txq_per_tc)
		num_txq_per_tc = 1;

	/* find the (rounded up) power-of-2 of qcount */
	pow = (u16)order_base_2(num_rxq_per_tc);

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
		vsi->tc_cfg.tc_info[i].qcount_rx = num_rxq_per_tc;
		vsi->tc_cfg.tc_info[i].qcount_tx = num_txq_per_tc;
		vsi->tc_cfg.tc_info[i].netdev_tc = netdev_tc++;

		qmap = ((offset << ICE_AQ_VSI_TC_Q_OFFSET_S) &
			ICE_AQ_VSI_TC_Q_OFFSET_M) |
			((pow << ICE_AQ_VSI_TC_Q_NUM_S) &
			 ICE_AQ_VSI_TC_Q_NUM_M);
		offset += num_rxq_per_tc;
		tx_count += num_txq_per_tc;
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
		vsi->num_rxq = num_rxq_per_tc;

	vsi->num_txq = tx_count;

	if (vsi->type == ICE_VSI_VF && vsi->num_txq != vsi->num_rxq) {
		dev_dbg(ice_pf_to_dev(vsi->back), "VF VSI should have same number of Tx and Rx queues. Hence making them equal\n");
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
 * ice_set_fd_vsi_ctx - Set FD VSI context before adding a VSI
 * @ctxt: the VSI context being set
 * @vsi: the VSI being configured
 */
static void ice_set_fd_vsi_ctx(struct ice_vsi_ctx *ctxt, struct ice_vsi *vsi)
{
	u8 dflt_q_group, dflt_q_prio;
	u16 dflt_q, report_q, val;

	if (vsi->type != ICE_VSI_PF && vsi->type != ICE_VSI_CTRL &&
	    vsi->type != ICE_VSI_VF && vsi->type != ICE_VSI_CHNL)
		return;

	val = ICE_AQ_VSI_PROP_FLOW_DIR_VALID;
	ctxt->info.valid_sections |= cpu_to_le16(val);
	dflt_q = 0;
	dflt_q_group = 0;
	report_q = 0;
	dflt_q_prio = 0;

	/* enable flow director filtering/programming */
	val = ICE_AQ_VSI_FD_ENABLE | ICE_AQ_VSI_FD_PROG_ENABLE;
	ctxt->info.fd_options = cpu_to_le16(val);
	/* max of allocated flow director filters */
	ctxt->info.max_fd_fltr_dedicated =
			cpu_to_le16(vsi->num_gfltr);
	/* max of shared flow director filters any VSI may program */
	ctxt->info.max_fd_fltr_shared =
			cpu_to_le16(vsi->num_bfltr);
	/* default queue index within the VSI of the default FD */
	val = ((dflt_q << ICE_AQ_VSI_FD_DEF_Q_S) &
	       ICE_AQ_VSI_FD_DEF_Q_M);
	/* target queue or queue group to the FD filter */
	val |= ((dflt_q_group << ICE_AQ_VSI_FD_DEF_GRP_S) &
		ICE_AQ_VSI_FD_DEF_GRP_M);
	ctxt->info.fd_def_q = cpu_to_le16(val);
	/* queue index on which FD filter completion is reported */
	val = ((report_q << ICE_AQ_VSI_FD_REPORT_Q_S) &
	       ICE_AQ_VSI_FD_REPORT_Q_M);
	/* priority of the default qindex action */
	val |= ((dflt_q_prio << ICE_AQ_VSI_FD_DEF_PRIORITY_S) &
		ICE_AQ_VSI_FD_DEF_PRIORITY_M);
	ctxt->info.fd_report_opt = cpu_to_le16(val);
}

/**
 * ice_set_rss_vsi_ctx - Set RSS VSI context before adding a VSI
 * @ctxt: the VSI context being set
 * @vsi: the VSI being configured
 */
static void ice_set_rss_vsi_ctx(struct ice_vsi_ctx *ctxt, struct ice_vsi *vsi)
{
	u8 lut_type, hash_type;
	struct device *dev;
	struct ice_pf *pf;

	pf = vsi->back;
	dev = ice_pf_to_dev(pf);

	switch (vsi->type) {
	case ICE_VSI_CHNL:
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
		dev_dbg(dev, "Unsupported VSI type %s\n",
			ice_vsi_type_str(vsi->type));
		return;
	}

	ctxt->info.q_opt_rss = ((lut_type << ICE_AQ_VSI_Q_OPT_RSS_LUT_S) &
				ICE_AQ_VSI_Q_OPT_RSS_LUT_M) |
				((hash_type << ICE_AQ_VSI_Q_OPT_RSS_HASH_S) &
				 ICE_AQ_VSI_Q_OPT_RSS_HASH_M);
}

static void
ice_chnl_vsi_setup_q_map(struct ice_vsi *vsi, struct ice_vsi_ctx *ctxt)
{
	struct ice_pf *pf = vsi->back;
	u16 qcount, qmap;
	u8 offset = 0;
	int pow;

	qcount = min_t(int, vsi->num_rxq, pf->num_lan_msix);

	pow = order_base_2(qcount);
	qmap = ((offset << ICE_AQ_VSI_TC_Q_OFFSET_S) &
		 ICE_AQ_VSI_TC_Q_OFFSET_M) |
		 ((pow << ICE_AQ_VSI_TC_Q_NUM_S) &
		   ICE_AQ_VSI_TC_Q_NUM_M);

	ctxt->info.tc_mapping[0] = cpu_to_le16(qmap);
	ctxt->info.mapping_flags |= cpu_to_le16(ICE_AQ_VSI_Q_MAP_CONTIG);
	ctxt->info.q_mapping[0] = cpu_to_le16(vsi->next_base_q);
	ctxt->info.q_mapping[1] = cpu_to_le16(qcount);
}

/**
 * ice_vsi_init - Create and initialize a VSI
 * @vsi: the VSI being configured
 * @init_vsi: is this call creating a VSI
 *
 * This initializes a VSI context depending on the VSI type to be added and
 * passes it down to the add_vsi aq command to create a new VSI.
 */
static int ice_vsi_init(struct ice_vsi *vsi, bool init_vsi)
{
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	struct ice_vsi_ctx *ctxt;
	struct device *dev;
	int ret = 0;

	dev = ice_pf_to_dev(pf);
	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	switch (vsi->type) {
	case ICE_VSI_CTRL:
	case ICE_VSI_LB:
	case ICE_VSI_PF:
		ctxt->flags = ICE_AQ_VSI_TYPE_PF;
		break;
	case ICE_VSI_SWITCHDEV_CTRL:
	case ICE_VSI_CHNL:
		ctxt->flags = ICE_AQ_VSI_TYPE_VMDQ2;
		break;
	case ICE_VSI_VF:
		ctxt->flags = ICE_AQ_VSI_TYPE_VF;
		/* VF number here is the absolute VF number (0-255) */
		ctxt->vf_num = vsi->vf->vf_id + hw->func_caps.vf_base_id;
		break;
	default:
		ret = -ENODEV;
		goto out;
	}

	/* Handle VLAN pruning for channel VSI if main VSI has VLAN
	 * prune enabled
	 */
	if (vsi->type == ICE_VSI_CHNL) {
		struct ice_vsi *main_vsi;

		main_vsi = ice_get_main_vsi(pf);
		if (main_vsi && ice_vsi_is_vlan_pruning_ena(main_vsi))
			ctxt->info.sw_flags2 |=
				ICE_AQ_VSI_SW_FLAG_RX_VLAN_PRUNE_ENA;
		else
			ctxt->info.sw_flags2 &=
				~ICE_AQ_VSI_SW_FLAG_RX_VLAN_PRUNE_ENA;
	}

	ice_set_dflt_vsi_ctx(hw, ctxt);
	if (test_bit(ICE_FLAG_FD_ENA, pf->flags))
		ice_set_fd_vsi_ctx(ctxt, vsi);
	/* if the switch is in VEB mode, allow VSI loopback */
	if (vsi->vsw->bridge_mode == BRIDGE_MODE_VEB)
		ctxt->info.sw_flags |= ICE_AQ_VSI_SW_FLAG_ALLOW_LB;

	/* Set LUT type and HASH type if RSS is enabled */
	if (test_bit(ICE_FLAG_RSS_ENA, pf->flags) &&
	    vsi->type != ICE_VSI_CTRL) {
		ice_set_rss_vsi_ctx(ctxt, vsi);
		/* if updating VSI context, make sure to set valid_section:
		 * to indicate which section of VSI context being updated
		 */
		if (!init_vsi)
			ctxt->info.valid_sections |=
				cpu_to_le16(ICE_AQ_VSI_PROP_Q_OPT_VALID);
	}

	ctxt->info.sw_id = vsi->port_info->sw_id;
	if (vsi->type == ICE_VSI_CHNL) {
		ice_chnl_vsi_setup_q_map(vsi, ctxt);
	} else {
		ice_vsi_setup_q_map(vsi, ctxt);
		if (!init_vsi) /* means VSI being updated */
			/* must to indicate which section of VSI context are
			 * being modified
			 */
			ctxt->info.valid_sections |=
				cpu_to_le16(ICE_AQ_VSI_PROP_RXQ_MAP_VALID);
	}

	/* Allow control frames out of main VSI */
	if (vsi->type == ICE_VSI_PF) {
		ctxt->info.sec_flags |= ICE_AQ_VSI_SEC_FLAG_ALLOW_DEST_OVRD;
		ctxt->info.valid_sections |=
			cpu_to_le16(ICE_AQ_VSI_PROP_SECURITY_VALID);
	}

	if (init_vsi) {
		ret = ice_add_vsi(hw, vsi->idx, ctxt, NULL);
		if (ret) {
			dev_err(dev, "Add VSI failed, err %d\n", ret);
			ret = -EIO;
			goto out;
		}
	} else {
		ret = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
		if (ret) {
			dev_err(dev, "Update VSI failed, err %d\n", ret);
			ret = -EIO;
			goto out;
		}
	}

	/* keep context for update VSI operations */
	vsi->info = ctxt->info;

	/* record VSI number returned */
	vsi->vsi_num = ctxt->vsi_num;

out:
	kfree(ctxt);
	return ret;
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

	if (!res || index >= res->end)
		return -EINVAL;

	id |= ICE_RES_VALID_BIT;
	for (i = index; i < res->end && res->list[i] == id; i++) {
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
	u16 start = 0, end = 0;

	if (needed > res->end)
		return -ENOMEM;

	id |= ICE_RES_VALID_BIT;

	do {
		/* skip already allocated entries */
		if (res->list[end++] & ICE_RES_VALID_BIT) {
			start = end;
			if ((start + needed) > res->end)
				break;
		}

		if (end == (start + needed)) {
			int i = start;

			/* there was enough, so assign it to the requestor */
			while (i != end)
				res->list[i++] = id;

			return start;
		}
	} while (end < res->end);

	return -ENOMEM;
}

/**
 * ice_get_free_res_count - Get free count from a resource tracker
 * @res: Resource tracker instance
 */
static u16 ice_get_free_res_count(struct ice_res_tracker *res)
{
	u16 i, count = 0;

	for (i = 0; i < res->end; i++)
		if (!(res->list[i] & ICE_RES_VALID_BIT))
			count++;

	return count;
}

/**
 * ice_get_res - get a block of resources
 * @pf: board private structure
 * @res: pointer to the resource
 * @needed: size of the block needed
 * @id: identifier to track owner
 *
 * Returns the base item index of the block, or negative for error
 */
int
ice_get_res(struct ice_pf *pf, struct ice_res_tracker *res, u16 needed, u16 id)
{
	if (!res || !pf)
		return -EINVAL;

	if (!needed || needed > res->num_entries || id >= ICE_RES_VALID_BIT) {
		dev_err(ice_pf_to_dev(pf), "param err: needed=%d, num_entries = %d id=0x%04x\n",
			needed, res->num_entries, id);
		return -EINVAL;
	}

	return ice_search_res(res, needed, id);
}

/**
 * ice_get_vf_ctrl_res - Get VF control VSI resource
 * @pf: pointer to the PF structure
 * @vsi: the VSI to allocate a resource for
 *
 * Look up whether another VF has already allocated the control VSI resource.
 * If so, re-use this resource so that we share it among all VFs.
 *
 * Otherwise, allocate the resource and return it.
 */
static int ice_get_vf_ctrl_res(struct ice_pf *pf, struct ice_vsi *vsi)
{
	struct ice_vf *vf;
	unsigned int bkt;
	int base;

	rcu_read_lock();
	ice_for_each_vf_rcu(pf, bkt, vf) {
		if (vf != vsi->vf && vf->ctrl_vsi_idx != ICE_NO_VSI) {
			base = pf->vsi[vf->ctrl_vsi_idx]->base_vector;
			rcu_read_unlock();
			return base;
		}
	}
	rcu_read_unlock();

	return ice_get_res(pf, pf->irq_tracker, vsi->num_q_vectors,
			   ICE_RES_VF_CTRL_VEC_ID);
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
	struct device *dev;
	u16 num_q_vectors;
	int base;

	dev = ice_pf_to_dev(pf);
	/* SRIOV doesn't grab irq_tracker entries for each VSI */
	if (vsi->type == ICE_VSI_VF)
		return 0;
	if (vsi->type == ICE_VSI_CHNL)
		return 0;

	if (vsi->base_vector) {
		dev_dbg(dev, "VSI %d has non-zero base vector %d\n",
			vsi->vsi_num, vsi->base_vector);
		return -EEXIST;
	}

	num_q_vectors = vsi->num_q_vectors;
	/* reserve slots from OS requested IRQs */
	if (vsi->type == ICE_VSI_CTRL && vsi->vf) {
		base = ice_get_vf_ctrl_res(pf, vsi);
	} else {
		base = ice_get_res(pf, pf->irq_tracker, num_q_vectors,
				   vsi->idx);
	}

	if (base < 0) {
		dev_err(dev, "%d MSI-X interrupts available. %s %d failed to get %d MSI-X vectors\n",
			ice_get_free_res_count(pf->irq_tracker),
			ice_vsi_type_str(vsi->type), vsi->idx, num_q_vectors);
		return -ENOENT;
	}
	vsi->base_vector = (u16)base;
	pf->num_avail_sw_msix -= num_q_vectors;

	return 0;
}

/**
 * ice_vsi_clear_rings - Deallocates the Tx and Rx rings for VSI
 * @vsi: the VSI having rings deallocated
 */
static void ice_vsi_clear_rings(struct ice_vsi *vsi)
{
	int i;

	/* Avoid stale references by clearing map from vector to ring */
	if (vsi->q_vectors) {
		ice_for_each_q_vector(vsi, i) {
			struct ice_q_vector *q_vector = vsi->q_vectors[i];

			if (q_vector) {
				q_vector->tx.tx_ring = NULL;
				q_vector->rx.rx_ring = NULL;
			}
		}
	}

	if (vsi->tx_rings) {
		ice_for_each_alloc_txq(vsi, i) {
			if (vsi->tx_rings[i]) {
				kfree_rcu(vsi->tx_rings[i], rcu);
				WRITE_ONCE(vsi->tx_rings[i], NULL);
			}
		}
	}
	if (vsi->rx_rings) {
		ice_for_each_alloc_rxq(vsi, i) {
			if (vsi->rx_rings[i]) {
				kfree_rcu(vsi->rx_rings[i], rcu);
				WRITE_ONCE(vsi->rx_rings[i], NULL);
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
	bool dvm_ena = ice_is_dvm_ena(&vsi->back->hw);
	struct ice_pf *pf = vsi->back;
	struct device *dev;
	u16 i;

	dev = ice_pf_to_dev(pf);
	/* Allocate Tx rings */
	ice_for_each_alloc_txq(vsi, i) {
		struct ice_tx_ring *ring;

		/* allocate with kzalloc(), free with kfree_rcu() */
		ring = kzalloc(sizeof(*ring), GFP_KERNEL);

		if (!ring)
			goto err_out;

		ring->q_index = i;
		ring->reg_idx = vsi->txq_map[i];
		ring->vsi = vsi;
		ring->tx_tstamps = &pf->ptp.port.tx;
		ring->dev = dev;
		ring->count = vsi->num_tx_desc;
		if (dvm_ena)
			ring->flags |= ICE_TX_FLAGS_RING_VLAN_L2TAG2;
		else
			ring->flags |= ICE_TX_FLAGS_RING_VLAN_L2TAG1;
		WRITE_ONCE(vsi->tx_rings[i], ring);
	}

	/* Allocate Rx rings */
	ice_for_each_alloc_rxq(vsi, i) {
		struct ice_rx_ring *ring;

		/* allocate with kzalloc(), free with kfree_rcu() */
		ring = kzalloc(sizeof(*ring), GFP_KERNEL);
		if (!ring)
			goto err_out;

		ring->q_index = i;
		ring->reg_idx = vsi->rxq_map[i];
		ring->vsi = vsi;
		ring->netdev = vsi->netdev;
		ring->dev = dev;
		ring->count = vsi->num_rx_desc;
		WRITE_ONCE(vsi->rx_rings[i], ring);
	}

	return 0;

err_out:
	ice_vsi_clear_rings(vsi);
	return -ENOMEM;
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
void ice_vsi_manage_rss_lut(struct ice_vsi *vsi, bool ena)
{
	u8 *lut;

	lut = kzalloc(vsi->rss_table_size, GFP_KERNEL);
	if (!lut)
		return;

	if (ena) {
		if (vsi->rss_lut_user)
			memcpy(lut, vsi->rss_lut_user, vsi->rss_table_size);
		else
			ice_fill_rss_lut(lut, vsi->rss_table_size,
					 vsi->rss_size);
	}

	ice_set_rss_lut(vsi, lut, vsi->rss_table_size);
	kfree(lut);
}

/**
 * ice_vsi_cfg_rss_lut_key - Configure RSS params for a VSI
 * @vsi: VSI to be configured
 */
int ice_vsi_cfg_rss_lut_key(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct device *dev;
	u8 *lut, *key;
	int err;

	dev = ice_pf_to_dev(pf);
	if (vsi->type == ICE_VSI_PF && vsi->ch_rss_size &&
	    (test_bit(ICE_FLAG_TC_MQPRIO, pf->flags))) {
		vsi->rss_size = min_t(u16, vsi->rss_size, vsi->ch_rss_size);
	} else {
		vsi->rss_size = min_t(u16, vsi->rss_size, vsi->num_rxq);

		/* If orig_rss_size is valid and it is less than determined
		 * main VSI's rss_size, update main VSI's rss_size to be
		 * orig_rss_size so that when tc-qdisc is deleted, main VSI
		 * RSS table gets programmed to be correct (whatever it was
		 * to begin with (prior to setup-tc for ADQ config)
		 */
		if (vsi->orig_rss_size && vsi->rss_size < vsi->orig_rss_size &&
		    vsi->orig_rss_size <= vsi->num_rxq) {
			vsi->rss_size = vsi->orig_rss_size;
			/* now orig_rss_size is used, reset it to zero */
			vsi->orig_rss_size = 0;
		}
	}

	lut = kzalloc(vsi->rss_table_size, GFP_KERNEL);
	if (!lut)
		return -ENOMEM;

	if (vsi->rss_lut_user)
		memcpy(lut, vsi->rss_lut_user, vsi->rss_table_size);
	else
		ice_fill_rss_lut(lut, vsi->rss_table_size, vsi->rss_size);

	err = ice_set_rss_lut(vsi, lut, vsi->rss_table_size);
	if (err) {
		dev_err(dev, "set_rss_lut failed, error %d\n", err);
		goto ice_vsi_cfg_rss_exit;
	}

	key = kzalloc(ICE_GET_SET_RSS_KEY_EXTEND_KEY_SIZE, GFP_KERNEL);
	if (!key) {
		err = -ENOMEM;
		goto ice_vsi_cfg_rss_exit;
	}

	if (vsi->rss_hkey_user)
		memcpy(key, vsi->rss_hkey_user, ICE_GET_SET_RSS_KEY_EXTEND_KEY_SIZE);
	else
		netdev_rss_key_fill((void *)key, ICE_GET_SET_RSS_KEY_EXTEND_KEY_SIZE);

	err = ice_set_rss_key(vsi, key);
	if (err)
		dev_err(dev, "set_rss_key failed, error %d\n", err);

	kfree(key);
ice_vsi_cfg_rss_exit:
	kfree(lut);
	return err;
}

/**
 * ice_vsi_set_vf_rss_flow_fld - Sets VF VSI RSS input set for different flows
 * @vsi: VSI to be configured
 *
 * This function will only be called during the VF VSI setup. Upon successful
 * completion of package download, this function will configure default RSS
 * input sets for VF VSI.
 */
static void ice_vsi_set_vf_rss_flow_fld(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct device *dev;
	int status;

	dev = ice_pf_to_dev(pf);
	if (ice_is_safe_mode(pf)) {
		dev_dbg(dev, "Advanced RSS disabled. Package download failed, vsi num = %d\n",
			vsi->vsi_num);
		return;
	}

	status = ice_add_avf_rss_cfg(&pf->hw, vsi->idx, ICE_DEFAULT_RSS_HENA);
	if (status)
		dev_dbg(dev, "ice_add_avf_rss_cfg failed for vsi = %d, error = %d\n",
			vsi->vsi_num, status);
}

/**
 * ice_vsi_set_rss_flow_fld - Sets RSS input set for different flows
 * @vsi: VSI to be configured
 *
 * This function will only be called after successful download package call
 * during initialization of PF. Since the downloaded package will erase the
 * RSS section, this function will configure RSS input sets for different
 * flow types. The last profile added has the highest priority, therefore 2
 * tuple profiles (i.e. IPv4 src/dst) are added before 4 tuple profiles
 * (i.e. IPv4 src/dst TCP src/dst port).
 */
static void ice_vsi_set_rss_flow_fld(struct ice_vsi *vsi)
{
	u16 vsi_handle = vsi->idx, vsi_num = vsi->vsi_num;
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	struct device *dev;
	int status;

	dev = ice_pf_to_dev(pf);
	if (ice_is_safe_mode(pf)) {
		dev_dbg(dev, "Advanced RSS disabled. Package download failed, vsi num = %d\n",
			vsi_num);
		return;
	}
	/* configure RSS for IPv4 with input set IP src/dst */
	status = ice_add_rss_cfg(hw, vsi_handle, ICE_FLOW_HASH_IPV4,
				 ICE_FLOW_SEG_HDR_IPV4);
	if (status)
		dev_dbg(dev, "ice_add_rss_cfg failed for ipv4 flow, vsi = %d, error = %d\n",
			vsi_num, status);

	/* configure RSS for IPv6 with input set IPv6 src/dst */
	status = ice_add_rss_cfg(hw, vsi_handle, ICE_FLOW_HASH_IPV6,
				 ICE_FLOW_SEG_HDR_IPV6);
	if (status)
		dev_dbg(dev, "ice_add_rss_cfg failed for ipv6 flow, vsi = %d, error = %d\n",
			vsi_num, status);

	/* configure RSS for tcp4 with input set IP src/dst, TCP src/dst */
	status = ice_add_rss_cfg(hw, vsi_handle, ICE_HASH_TCP_IPV4,
				 ICE_FLOW_SEG_HDR_TCP | ICE_FLOW_SEG_HDR_IPV4);
	if (status)
		dev_dbg(dev, "ice_add_rss_cfg failed for tcp4 flow, vsi = %d, error = %d\n",
			vsi_num, status);

	/* configure RSS for udp4 with input set IP src/dst, UDP src/dst */
	status = ice_add_rss_cfg(hw, vsi_handle, ICE_HASH_UDP_IPV4,
				 ICE_FLOW_SEG_HDR_UDP | ICE_FLOW_SEG_HDR_IPV4);
	if (status)
		dev_dbg(dev, "ice_add_rss_cfg failed for udp4 flow, vsi = %d, error = %d\n",
			vsi_num, status);

	/* configure RSS for sctp4 with input set IP src/dst */
	status = ice_add_rss_cfg(hw, vsi_handle, ICE_FLOW_HASH_IPV4,
				 ICE_FLOW_SEG_HDR_SCTP | ICE_FLOW_SEG_HDR_IPV4);
	if (status)
		dev_dbg(dev, "ice_add_rss_cfg failed for sctp4 flow, vsi = %d, error = %d\n",
			vsi_num, status);

	/* configure RSS for tcp6 with input set IPv6 src/dst, TCP src/dst */
	status = ice_add_rss_cfg(hw, vsi_handle, ICE_HASH_TCP_IPV6,
				 ICE_FLOW_SEG_HDR_TCP | ICE_FLOW_SEG_HDR_IPV6);
	if (status)
		dev_dbg(dev, "ice_add_rss_cfg failed for tcp6 flow, vsi = %d, error = %d\n",
			vsi_num, status);

	/* configure RSS for udp6 with input set IPv6 src/dst, UDP src/dst */
	status = ice_add_rss_cfg(hw, vsi_handle, ICE_HASH_UDP_IPV6,
				 ICE_FLOW_SEG_HDR_UDP | ICE_FLOW_SEG_HDR_IPV6);
	if (status)
		dev_dbg(dev, "ice_add_rss_cfg failed for udp6 flow, vsi = %d, error = %d\n",
			vsi_num, status);

	/* configure RSS for sctp6 with input set IPv6 src/dst */
	status = ice_add_rss_cfg(hw, vsi_handle, ICE_FLOW_HASH_IPV6,
				 ICE_FLOW_SEG_HDR_SCTP | ICE_FLOW_SEG_HDR_IPV6);
	if (status)
		dev_dbg(dev, "ice_add_rss_cfg failed for sctp6 flow, vsi = %d, error = %d\n",
			vsi_num, status);

	status = ice_add_rss_cfg(hw, vsi_handle, ICE_FLOW_HASH_ESP_SPI,
				 ICE_FLOW_SEG_HDR_ESP);
	if (status)
		dev_dbg(dev, "ice_add_rss_cfg failed for esp/spi flow, vsi = %d, error = %d\n",
			vsi_num, status);
}

/**
 * ice_pf_state_is_nominal - checks the PF for nominal state
 * @pf: pointer to PF to check
 *
 * Check the PF's state for a collection of bits that would indicate
 * the PF is in a state that would inhibit normal operation for
 * driver functionality.
 *
 * Returns true if PF is in a nominal state, false otherwise
 */
bool ice_pf_state_is_nominal(struct ice_pf *pf)
{
	DECLARE_BITMAP(check_bits, ICE_STATE_NBITS) = { 0 };

	if (!pf)
		return false;

	bitmap_set(check_bits, 0, ICE_STATE_NOMINAL_CHECK_BITS);
	if (bitmap_intersects(pf->state, check_bits, ICE_STATE_NBITS))
		return false;

	return true;
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

	ice_stat_update40(hw, GLV_GORCL(vsi_num), vsi->stat_offsets_loaded,
			  &prev_es->rx_bytes, &cur_es->rx_bytes);

	ice_stat_update40(hw, GLV_UPRCL(vsi_num), vsi->stat_offsets_loaded,
			  &prev_es->rx_unicast, &cur_es->rx_unicast);

	ice_stat_update40(hw, GLV_MPRCL(vsi_num), vsi->stat_offsets_loaded,
			  &prev_es->rx_multicast, &cur_es->rx_multicast);

	ice_stat_update40(hw, GLV_BPRCL(vsi_num), vsi->stat_offsets_loaded,
			  &prev_es->rx_broadcast, &cur_es->rx_broadcast);

	ice_stat_update32(hw, GLV_RDPC(vsi_num), vsi->stat_offsets_loaded,
			  &prev_es->rx_discards, &cur_es->rx_discards);

	ice_stat_update40(hw, GLV_GOTCL(vsi_num), vsi->stat_offsets_loaded,
			  &prev_es->tx_bytes, &cur_es->tx_bytes);

	ice_stat_update40(hw, GLV_UPTCL(vsi_num), vsi->stat_offsets_loaded,
			  &prev_es->tx_unicast, &cur_es->tx_unicast);

	ice_stat_update40(hw, GLV_MPTCL(vsi_num), vsi->stat_offsets_loaded,
			  &prev_es->tx_multicast, &cur_es->tx_multicast);

	ice_stat_update40(hw, GLV_BPTCL(vsi_num), vsi->stat_offsets_loaded,
			  &prev_es->tx_broadcast, &cur_es->tx_broadcast);

	ice_stat_update32(hw, GLV_TEPC(vsi_num), vsi->stat_offsets_loaded,
			  &prev_es->tx_errors, &cur_es->tx_errors);

	vsi->stat_offsets_loaded = true;
}

/**
 * ice_vsi_cfg_frame_size - setup max frame size and Rx buffer length
 * @vsi: VSI
 */
void ice_vsi_cfg_frame_size(struct ice_vsi *vsi)
{
	if (!vsi->netdev || test_bit(ICE_FLAG_LEGACY_RX, vsi->back->flags)) {
		vsi->max_frame = ICE_AQ_SET_MAC_FRAME_SIZE_MAX;
		vsi->rx_buf_len = ICE_RXBUF_2048;
#if (PAGE_SIZE < 8192)
	} else if (!ICE_2K_TOO_SMALL_WITH_PADDING &&
		   (vsi->netdev->mtu <= ETH_DATA_LEN)) {
		vsi->max_frame = ICE_RXBUF_1536 - NET_IP_ALIGN;
		vsi->rx_buf_len = ICE_RXBUF_1536 - NET_IP_ALIGN;
#endif
	} else {
		vsi->max_frame = ICE_AQ_SET_MAC_FRAME_SIZE_MAX;
#if (PAGE_SIZE < 8192)
		vsi->rx_buf_len = ICE_RXBUF_3072;
#else
		vsi->rx_buf_len = ICE_RXBUF_2048;
#endif
	}
}

/**
 * ice_write_qrxflxp_cntxt - write/configure QRXFLXP_CNTXT register
 * @hw: HW pointer
 * @pf_q: index of the Rx queue in the PF's queue space
 * @rxdid: flexible descriptor RXDID
 * @prio: priority for the RXDID for this queue
 * @ena_ts: true to enable timestamp and false to disable timestamp
 */
void
ice_write_qrxflxp_cntxt(struct ice_hw *hw, u16 pf_q, u32 rxdid, u32 prio,
			bool ena_ts)
{
	int regval = rd32(hw, QRXFLXP_CNTXT(pf_q));

	/* clear any previous values */
	regval &= ~(QRXFLXP_CNTXT_RXDID_IDX_M |
		    QRXFLXP_CNTXT_RXDID_PRIO_M |
		    QRXFLXP_CNTXT_TS_M);

	regval |= (rxdid << QRXFLXP_CNTXT_RXDID_IDX_S) &
		QRXFLXP_CNTXT_RXDID_IDX_M;

	regval |= (prio << QRXFLXP_CNTXT_RXDID_PRIO_S) &
		QRXFLXP_CNTXT_RXDID_PRIO_M;

	if (ena_ts)
		/* Enable TimeSync on this queue */
		regval |= QRXFLXP_CNTXT_TS_M;

	wr32(hw, QRXFLXP_CNTXT(pf_q), regval);
}

int ice_vsi_cfg_single_rxq(struct ice_vsi *vsi, u16 q_idx)
{
	if (q_idx >= vsi->num_rxq)
		return -EINVAL;

	return ice_vsi_cfg_rxq(vsi->rx_rings[q_idx]);
}

int ice_vsi_cfg_single_txq(struct ice_vsi *vsi, struct ice_tx_ring **tx_rings, u16 q_idx)
{
	struct ice_aqc_add_tx_qgrp *qg_buf;
	int err;

	if (q_idx >= vsi->alloc_txq || !tx_rings || !tx_rings[q_idx])
		return -EINVAL;

	qg_buf = kzalloc(struct_size(qg_buf, txqs, 1), GFP_KERNEL);
	if (!qg_buf)
		return -ENOMEM;

	qg_buf->num_txqs = 1;

	err = ice_vsi_cfg_txq(vsi, tx_rings[q_idx], qg_buf);
	kfree(qg_buf);
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

	ice_vsi_cfg_frame_size(vsi);
setup_rings:
	/* set up individual rings */
	ice_for_each_rxq(vsi, i) {
		int err = ice_vsi_cfg_rxq(vsi->rx_rings[i]);

		if (err)
			return err;
	}

	return 0;
}

/**
 * ice_vsi_cfg_txqs - Configure the VSI for Tx
 * @vsi: the VSI being configured
 * @rings: Tx ring array to be configured
 * @count: number of Tx ring array elements
 *
 * Return 0 on success and a negative value on error
 * Configure the Tx VSI for operation.
 */
static int
ice_vsi_cfg_txqs(struct ice_vsi *vsi, struct ice_tx_ring **rings, u16 count)
{
	struct ice_aqc_add_tx_qgrp *qg_buf;
	u16 q_idx = 0;
	int err = 0;

	qg_buf = kzalloc(struct_size(qg_buf, txqs, 1), GFP_KERNEL);
	if (!qg_buf)
		return -ENOMEM;

	qg_buf->num_txqs = 1;

	for (q_idx = 0; q_idx < count; q_idx++) {
		err = ice_vsi_cfg_txq(vsi, rings[q_idx], qg_buf);
		if (err)
			goto err_cfg_txqs;
	}

err_cfg_txqs:
	kfree(qg_buf);
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
	return ice_vsi_cfg_txqs(vsi, vsi->tx_rings, vsi->num_txq);
}

/**
 * ice_vsi_cfg_xdp_txqs - Configure Tx queues dedicated for XDP in given VSI
 * @vsi: the VSI being configured
 *
 * Return 0 on success and a negative value on error
 * Configure the Tx queues dedicated for XDP in given VSI for operation.
 */
int ice_vsi_cfg_xdp_txqs(struct ice_vsi *vsi)
{
	int ret;
	int i;

	ret = ice_vsi_cfg_txqs(vsi, vsi->xdp_rings, vsi->num_xdp_txq);
	if (ret)
		return ret;

	ice_for_each_xdp_txq(vsi, i)
		vsi->xdp_rings[i]->xsk_pool = ice_tx_xsk_pool(vsi->xdp_rings[i]);

	return ret;
}

/**
 * ice_intrl_usec_to_reg - convert interrupt rate limit to register value
 * @intrl: interrupt rate limit in usecs
 * @gran: interrupt rate limit granularity in usecs
 *
 * This function converts a decimal interrupt rate limit in usecs to the format
 * expected by firmware.
 */
static u32 ice_intrl_usec_to_reg(u8 intrl, u8 gran)
{
	u32 val = intrl / gran;

	if (val)
		return val | GLINT_RATE_INTRL_ENA_M;
	return 0;
}

/**
 * ice_write_intrl - write throttle rate limit to interrupt specific register
 * @q_vector: pointer to interrupt specific structure
 * @intrl: throttle rate limit in microseconds to write
 */
void ice_write_intrl(struct ice_q_vector *q_vector, u8 intrl)
{
	struct ice_hw *hw = &q_vector->vsi->back->hw;

	wr32(hw, GLINT_RATE(q_vector->reg_idx),
	     ice_intrl_usec_to_reg(intrl, ICE_INTRL_GRAN_ABOVE_25));
}

static struct ice_q_vector *ice_pull_qvec_from_rc(struct ice_ring_container *rc)
{
	switch (rc->type) {
	case ICE_RX_CONTAINER:
		if (rc->rx_ring)
			return rc->rx_ring->q_vector;
		break;
	case ICE_TX_CONTAINER:
		if (rc->tx_ring)
			return rc->tx_ring->q_vector;
		break;
	default:
		break;
	}

	return NULL;
}

/**
 * __ice_write_itr - write throttle rate to register
 * @q_vector: pointer to interrupt data structure
 * @rc: pointer to ring container
 * @itr: throttle rate in microseconds to write
 */
static void __ice_write_itr(struct ice_q_vector *q_vector,
			    struct ice_ring_container *rc, u16 itr)
{
	struct ice_hw *hw = &q_vector->vsi->back->hw;

	wr32(hw, GLINT_ITR(rc->itr_idx, q_vector->reg_idx),
	     ITR_REG_ALIGN(itr) >> ICE_ITR_GRAN_S);
}

/**
 * ice_write_itr - write throttle rate to queue specific register
 * @rc: pointer to ring container
 * @itr: throttle rate in microseconds to write
 */
void ice_write_itr(struct ice_ring_container *rc, u16 itr)
{
	struct ice_q_vector *q_vector;

	q_vector = ice_pull_qvec_from_rc(rc);
	if (!q_vector)
		return;

	__ice_write_itr(q_vector, rc, itr);
}

/**
 * ice_set_q_vector_intrl - set up interrupt rate limiting
 * @q_vector: the vector to be configured
 *
 * Interrupt rate limiting is local to the vector, not per-queue so we must
 * detect if either ring container has dynamic moderation enabled to decide
 * what to set the interrupt rate limit to via INTRL settings. In the case that
 * dynamic moderation is disabled on both, write the value with the cached
 * setting to make sure INTRL register matches the user visible value.
 */
void ice_set_q_vector_intrl(struct ice_q_vector *q_vector)
{
	if (ITR_IS_DYNAMIC(&q_vector->tx) || ITR_IS_DYNAMIC(&q_vector->rx)) {
		/* in the case of dynamic enabled, cap each vector to no more
		 * than (4 us) 250,000 ints/sec, which allows low latency
		 * but still less than 500,000 interrupts per second, which
		 * reduces CPU a bit in the case of the lowest latency
		 * setting. The 4 here is a value in microseconds.
		 */
		ice_write_intrl(q_vector, 4);
	} else {
		ice_write_intrl(q_vector, q_vector->intrl);
	}
}

/**
 * ice_vsi_cfg_msix - MSIX mode Interrupt Config in the HW
 * @vsi: the VSI being configured
 *
 * This configures MSIX mode interrupts for the PF VSI, and should not be used
 * for the VF VSI.
 */
void ice_vsi_cfg_msix(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	u16 txq = 0, rxq = 0;
	int i, q;

	ice_for_each_q_vector(vsi, i) {
		struct ice_q_vector *q_vector = vsi->q_vectors[i];
		u16 reg_idx = q_vector->reg_idx;

		ice_cfg_itr(hw, q_vector);

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
			ice_cfg_txq_interrupt(vsi, txq, reg_idx,
					      q_vector->tx.itr_idx);
			txq++;
		}

		for (q = 0; q < q_vector->num_ring_rx; q++) {
			ice_cfg_rxq_interrupt(vsi, rxq, reg_idx,
					      q_vector->rx.itr_idx);
			rxq++;
		}
	}
}

/**
 * ice_vsi_start_all_rx_rings - start/enable all of a VSI's Rx rings
 * @vsi: the VSI whose rings are to be enabled
 *
 * Returns 0 on success and a negative value on error
 */
int ice_vsi_start_all_rx_rings(struct ice_vsi *vsi)
{
	return ice_vsi_ctrl_all_rx_rings(vsi, true);
}

/**
 * ice_vsi_stop_all_rx_rings - stop/disable all of a VSI's Rx rings
 * @vsi: the VSI whose rings are to be disabled
 *
 * Returns 0 on success and a negative value on error
 */
int ice_vsi_stop_all_rx_rings(struct ice_vsi *vsi)
{
	return ice_vsi_ctrl_all_rx_rings(vsi, false);
}

/**
 * ice_vsi_stop_tx_rings - Disable Tx rings
 * @vsi: the VSI being configured
 * @rst_src: reset source
 * @rel_vmvf_num: Relative ID of VF/VM
 * @rings: Tx ring array to be stopped
 * @count: number of Tx ring array elements
 */
static int
ice_vsi_stop_tx_rings(struct ice_vsi *vsi, enum ice_disq_rst_src rst_src,
		      u16 rel_vmvf_num, struct ice_tx_ring **rings, u16 count)
{
	u16 q_idx;

	if (vsi->num_txq > ICE_LAN_TXQ_MAX_QDIS)
		return -EINVAL;

	for (q_idx = 0; q_idx < count; q_idx++) {
		struct ice_txq_meta txq_meta = { };
		int status;

		if (!rings || !rings[q_idx])
			return -EINVAL;

		ice_fill_txq_meta(vsi, rings[q_idx], &txq_meta);
		status = ice_vsi_stop_tx_ring(vsi, rst_src, rel_vmvf_num,
					      rings[q_idx], &txq_meta);

		if (status)
			return status;
	}

	return 0;
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
	return ice_vsi_stop_tx_rings(vsi, rst_src, rel_vmvf_num, vsi->tx_rings, vsi->num_txq);
}

/**
 * ice_vsi_stop_xdp_tx_rings - Disable XDP Tx rings
 * @vsi: the VSI being configured
 */
int ice_vsi_stop_xdp_tx_rings(struct ice_vsi *vsi)
{
	return ice_vsi_stop_tx_rings(vsi, ICE_NO_RESET, 0, vsi->xdp_rings, vsi->num_xdp_txq);
}

/**
 * ice_vsi_is_vlan_pruning_ena - check if VLAN pruning is enabled or not
 * @vsi: VSI to check whether or not VLAN pruning is enabled.
 *
 * returns true if Rx VLAN pruning is enabled and false otherwise.
 */
bool ice_vsi_is_vlan_pruning_ena(struct ice_vsi *vsi)
{
	if (!vsi)
		return false;

	return (vsi->info.sw_flags2 & ICE_AQ_VSI_SW_FLAG_RX_VLAN_PRUNE_ENA);
}

static void ice_vsi_set_tc_cfg(struct ice_vsi *vsi)
{
	if (!test_bit(ICE_FLAG_DCB_ENA, vsi->back->flags)) {
		vsi->tc_cfg.ena_tc = ICE_DFLT_TRAFFIC_CLASS;
		vsi->tc_cfg.numtc = 1;
		return;
	}

	/* set VSI TC information based on DCB config */
	ice_vsi_set_dcb_tc_cfg(vsi);
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
			dev_err(ice_pf_to_dev(vsi->back), "Failed to set reg_idx on q_vector %d VSI %d\n",
				i, vsi->vsi_num);
			goto clear_reg_idx;
		}

		if (vsi->type == ICE_VSI_VF) {
			struct ice_vf *vf = vsi->vf;

			q_vector->reg_idx = ice_calc_vf_reg_idx(vf, q_vector);
		} else {
			q_vector->reg_idx =
				q_vector->v_idx + vsi->base_vector;
		}
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
 * ice_cfg_sw_lldp - Config switch rules for LLDP packet handling
 * @vsi: the VSI being configured
 * @tx: bool to determine Tx or Rx rule
 * @create: bool to determine create or remove Rule
 */
void ice_cfg_sw_lldp(struct ice_vsi *vsi, bool tx, bool create)
{
	int (*eth_fltr)(struct ice_vsi *v, u16 type, u16 flag,
			enum ice_sw_fwd_act_type act);
	struct ice_pf *pf = vsi->back;
	struct device *dev;
	int status;

	dev = ice_pf_to_dev(pf);
	eth_fltr = create ? ice_fltr_add_eth : ice_fltr_remove_eth;

	if (tx) {
		status = eth_fltr(vsi, ETH_P_LLDP, ICE_FLTR_TX,
				  ICE_DROP_PACKET);
	} else {
		if (ice_fw_supports_lldp_fltr_ctrl(&pf->hw)) {
			status = ice_lldp_fltr_add_remove(&pf->hw, vsi->vsi_num,
							  create);
		} else {
			status = eth_fltr(vsi, ETH_P_LLDP, ICE_FLTR_RX,
					  ICE_FWD_TO_VSI);
		}
	}

	if (status)
		dev_dbg(dev, "Fail %s %s LLDP rule on VSI %i error: %d\n",
			create ? "adding" : "removing", tx ? "TX" : "RX",
			vsi->vsi_num, status);
}

/**
 * ice_set_agg_vsi - sets up scheduler aggregator node and move VSI into it
 * @vsi: pointer to the VSI
 *
 * This function will allocate new scheduler aggregator now if needed and will
 * move specified VSI into it.
 */
static void ice_set_agg_vsi(struct ice_vsi *vsi)
{
	struct device *dev = ice_pf_to_dev(vsi->back);
	struct ice_agg_node *agg_node_iter = NULL;
	u32 agg_id = ICE_INVALID_AGG_NODE_ID;
	struct ice_agg_node *agg_node = NULL;
	int node_offset, max_agg_nodes = 0;
	struct ice_port_info *port_info;
	struct ice_pf *pf = vsi->back;
	u32 agg_node_id_start = 0;
	int status;

	/* create (as needed) scheduler aggregator node and move VSI into
	 * corresponding aggregator node
	 * - PF aggregator node to contains VSIs of type _PF and _CTRL
	 * - VF aggregator nodes will contain VF VSI
	 */
	port_info = pf->hw.port_info;
	if (!port_info)
		return;

	switch (vsi->type) {
	case ICE_VSI_CTRL:
	case ICE_VSI_CHNL:
	case ICE_VSI_LB:
	case ICE_VSI_PF:
	case ICE_VSI_SWITCHDEV_CTRL:
		max_agg_nodes = ICE_MAX_PF_AGG_NODES;
		agg_node_id_start = ICE_PF_AGG_NODE_ID_START;
		agg_node_iter = &pf->pf_agg_node[0];
		break;
	case ICE_VSI_VF:
		/* user can create 'n' VFs on a given PF, but since max children
		 * per aggregator node can be only 64. Following code handles
		 * aggregator(s) for VF VSIs, either selects a agg_node which
		 * was already created provided num_vsis < 64, otherwise
		 * select next available node, which will be created
		 */
		max_agg_nodes = ICE_MAX_VF_AGG_NODES;
		agg_node_id_start = ICE_VF_AGG_NODE_ID_START;
		agg_node_iter = &pf->vf_agg_node[0];
		break;
	default:
		/* other VSI type, handle later if needed */
		dev_dbg(dev, "unexpected VSI type %s\n",
			ice_vsi_type_str(vsi->type));
		return;
	}

	/* find the appropriate aggregator node */
	for (node_offset = 0; node_offset < max_agg_nodes; node_offset++) {
		/* see if we can find space in previously created
		 * node if num_vsis < 64, otherwise skip
		 */
		if (agg_node_iter->num_vsis &&
		    agg_node_iter->num_vsis == ICE_MAX_VSIS_IN_AGG_NODE) {
			agg_node_iter++;
			continue;
		}

		if (agg_node_iter->valid &&
		    agg_node_iter->agg_id != ICE_INVALID_AGG_NODE_ID) {
			agg_id = agg_node_iter->agg_id;
			agg_node = agg_node_iter;
			break;
		}

		/* find unclaimed agg_id */
		if (agg_node_iter->agg_id == ICE_INVALID_AGG_NODE_ID) {
			agg_id = node_offset + agg_node_id_start;
			agg_node = agg_node_iter;
			break;
		}
		/* move to next agg_node */
		agg_node_iter++;
	}

	if (!agg_node)
		return;

	/* if selected aggregator node was not created, create it */
	if (!agg_node->valid) {
		status = ice_cfg_agg(port_info, agg_id, ICE_AGG_TYPE_AGG,
				     (u8)vsi->tc_cfg.ena_tc);
		if (status) {
			dev_err(dev, "unable to create aggregator node with agg_id %u\n",
				agg_id);
			return;
		}
		/* aggregator node is created, store the neeeded info */
		agg_node->valid = true;
		agg_node->agg_id = agg_id;
	}

	/* move VSI to corresponding aggregator node */
	status = ice_move_vsi_to_agg(port_info, agg_id, vsi->idx,
				     (u8)vsi->tc_cfg.ena_tc);
	if (status) {
		dev_err(dev, "unable to move VSI idx %u into aggregator %u node",
			vsi->idx, agg_id);
		return;
	}

	/* keep active children count for aggregator node */
	agg_node->num_vsis++;

	/* cache the 'agg_id' in VSI, so that after reset - VSI will be moved
	 * to aggregator node
	 */
	vsi->agg_node = agg_node;
	dev_dbg(dev, "successfully moved VSI idx %u tc_bitmap 0x%x) into aggregator node %d which has num_vsis %u\n",
		vsi->idx, vsi->tc_cfg.ena_tc, vsi->agg_node->agg_id,
		vsi->agg_node->num_vsis);
}

/**
 * ice_vsi_setup - Set up a VSI by a given type
 * @pf: board private structure
 * @pi: pointer to the port_info instance
 * @vsi_type: VSI type
 * @vf: pointer to VF to which this VSI connects. This field is used primarily
 *      for the ICE_VSI_VF type. Other VSI types should pass NULL.
 * @ch: ptr to channel
 *
 * This allocates the sw VSI structure and its queue resources.
 *
 * Returns pointer to the successfully allocated and configured VSI sw struct on
 * success, NULL on failure.
 */
struct ice_vsi *
ice_vsi_setup(struct ice_pf *pf, struct ice_port_info *pi,
	      enum ice_vsi_type vsi_type, struct ice_vf *vf,
	      struct ice_channel *ch)
{
	u16 max_txqs[ICE_MAX_TRAFFIC_CLASS] = { 0 };
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_vsi *vsi;
	int ret, i;

	if (vsi_type == ICE_VSI_CHNL)
		vsi = ice_vsi_alloc(pf, vsi_type, ch, NULL);
	else if (vsi_type == ICE_VSI_VF || vsi_type == ICE_VSI_CTRL)
		vsi = ice_vsi_alloc(pf, vsi_type, NULL, vf);
	else
		vsi = ice_vsi_alloc(pf, vsi_type, NULL, NULL);

	if (!vsi) {
		dev_err(dev, "could not allocate VSI\n");
		return NULL;
	}

	vsi->port_info = pi;
	vsi->vsw = pf->first_sw;
	if (vsi->type == ICE_VSI_PF)
		vsi->ethtype = ETH_P_PAUSE;

	ice_alloc_fd_res(vsi);

	if (vsi_type != ICE_VSI_CHNL) {
		if (ice_vsi_get_qs(vsi)) {
			dev_err(dev, "Failed to allocate queues. vsi->idx = %d\n",
				vsi->idx);
			goto unroll_vsi_alloc;
		}
	}

	/* set RSS capabilities */
	ice_vsi_set_rss_params(vsi);

	/* set TC configuration */
	ice_vsi_set_tc_cfg(vsi);

	/* create the VSI */
	ret = ice_vsi_init(vsi, true);
	if (ret)
		goto unroll_get_qs;

	ice_vsi_init_vlan_ops(vsi);

	switch (vsi->type) {
	case ICE_VSI_CTRL:
	case ICE_VSI_SWITCHDEV_CTRL:
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

		/* ICE_VSI_CTRL does not need RSS so skip RSS processing */
		if (vsi->type != ICE_VSI_CTRL)
			/* Do not exit if configuring RSS had an issue, at
			 * least receive traffic on first queue. Hence no
			 * need to capture return value
			 */
			if (test_bit(ICE_FLAG_RSS_ENA, pf->flags)) {
				ice_vsi_cfg_rss_lut_key(vsi);
				ice_vsi_set_rss_flow_fld(vsi);
			}
		ice_init_arfs(vsi);
		break;
	case ICE_VSI_CHNL:
		if (test_bit(ICE_FLAG_RSS_ENA, pf->flags)) {
			ice_vsi_cfg_rss_lut_key(vsi);
			ice_vsi_set_rss_flow_fld(vsi);
		}
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

		ret = ice_vsi_set_q_vectors_reg_idx(vsi);
		if (ret)
			goto unroll_vector_base;

		/* Do not exit if configuring RSS had an issue, at least
		 * receive traffic on first queue. Hence no need to capture
		 * return value
		 */
		if (test_bit(ICE_FLAG_RSS_ENA, pf->flags)) {
			ice_vsi_cfg_rss_lut_key(vsi);
			ice_vsi_set_vf_rss_flow_fld(vsi);
		}
		break;
	case ICE_VSI_LB:
		ret = ice_vsi_alloc_rings(vsi);
		if (ret)
			goto unroll_vsi_init;
		break;
	default:
		/* clean up the resources and exit */
		goto unroll_vsi_init;
	}

	/* configure VSI nodes based on number of queues and TC's */
	ice_for_each_traffic_class(i) {
		if (!(vsi->tc_cfg.ena_tc & BIT(i)))
			continue;

		if (vsi->type == ICE_VSI_CHNL) {
			if (!vsi->alloc_txq && vsi->num_txq)
				max_txqs[i] = vsi->num_txq;
			else
				max_txqs[i] = pf->num_lan_tx;
		} else {
			max_txqs[i] = vsi->alloc_txq;
		}
	}

	dev_dbg(dev, "vsi->tc_cfg.ena_tc = %d\n", vsi->tc_cfg.ena_tc);
	ret = ice_cfg_vsi_lan(vsi->port_info, vsi->idx, vsi->tc_cfg.ena_tc,
			      max_txqs);
	if (ret) {
		dev_err(dev, "VSI %d failed lan queue config, error %d\n",
			vsi->vsi_num, ret);
		goto unroll_clear_rings;
	}

	/* Add switch rule to drop all Tx Flow Control Frames, of look up
	 * type ETHERTYPE from VSIs, and restrict malicious VF from sending
	 * out PAUSE or PFC frames. If enabled, FW can still send FC frames.
	 * The rule is added once for PF VSI in order to create appropriate
	 * recipe, since VSI/VSI list is ignored with drop action...
	 * Also add rules to handle LLDP Tx packets.  Tx LLDP packets need to
	 * be dropped so that VFs cannot send LLDP packets to reconfig DCB
	 * settings in the HW.
	 */
	if (!ice_is_safe_mode(pf))
		if (vsi->type == ICE_VSI_PF) {
			ice_fltr_add_eth(vsi, ETH_P_PAUSE, ICE_FLTR_TX,
					 ICE_DROP_PACKET);
			ice_cfg_sw_lldp(vsi, true, true);
		}

	if (!vsi->agg_node)
		ice_set_agg_vsi(vsi);
	return vsi;

unroll_clear_rings:
	ice_vsi_clear_rings(vsi);
unroll_vector_base:
	/* reclaim SW interrupts back to the common pool */
	ice_free_res(pf->irq_tracker, vsi->base_vector, vsi->idx);
	pf->num_avail_sw_msix += vsi->num_q_vectors;
unroll_alloc_q_vector:
	ice_vsi_free_q_vectors(vsi);
unroll_vsi_init:
	ice_vsi_delete(vsi);
unroll_get_qs:
	ice_vsi_put_qs(vsi);
unroll_vsi_alloc:
	if (vsi_type == ICE_VSI_VF)
		ice_enable_lag(pf->lag);
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
	struct ice_hw *hw = &pf->hw;
	u32 txq = 0;
	u32 rxq = 0;
	int i, q;

	ice_for_each_q_vector(vsi, i) {
		struct ice_q_vector *q_vector = vsi->q_vectors[i];

		ice_write_intrl(q_vector, 0);
		for (q = 0; q < q_vector->num_ring_tx; q++) {
			ice_write_itr(&q_vector->tx, 0);
			wr32(hw, QINT_TQCTL(vsi->txq_map[txq]), 0);
			if (ice_is_xdp_ena_vsi(vsi)) {
				u32 xdp_txq = txq + vsi->num_xdp_txq;

				wr32(hw, QINT_TQCTL(vsi->txq_map[xdp_txq]), 0);
			}
			txq++;
		}

		for (q = 0; q < q_vector->num_ring_rx; q++) {
			ice_write_itr(&q_vector->rx, 0);
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
		devm_free_irq(ice_pf_to_dev(pf), irq_num, vsi->q_vectors[i]);
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
	if (!test_and_set_bit(ICE_VSI_DOWN, vsi->state))
		ice_down(vsi);

	ice_vsi_free_irq(vsi);
	ice_vsi_free_tx_rings(vsi);
	ice_vsi_free_rx_rings(vsi);
}

/**
 * ice_ena_vsi - resume a VSI
 * @vsi: the VSI being resume
 * @locked: is the rtnl_lock already held
 */
int ice_ena_vsi(struct ice_vsi *vsi, bool locked)
{
	int err = 0;

	if (!test_bit(ICE_VSI_NEEDS_RESTART, vsi->state))
		return 0;

	clear_bit(ICE_VSI_NEEDS_RESTART, vsi->state);

	if (vsi->netdev && vsi->type == ICE_VSI_PF) {
		if (netif_running(vsi->netdev)) {
			if (!locked)
				rtnl_lock();

			err = ice_open_internal(vsi->netdev);

			if (!locked)
				rtnl_unlock();
		}
	} else if (vsi->type == ICE_VSI_CTRL) {
		err = ice_vsi_open_ctrl(vsi);
	}

	return err;
}

/**
 * ice_dis_vsi - pause a VSI
 * @vsi: the VSI being paused
 * @locked: is the rtnl_lock already held
 */
void ice_dis_vsi(struct ice_vsi *vsi, bool locked)
{
	if (test_bit(ICE_VSI_DOWN, vsi->state))
		return;

	set_bit(ICE_VSI_NEEDS_RESTART, vsi->state);

	if (vsi->type == ICE_VSI_PF && vsi->netdev) {
		if (netif_running(vsi->netdev)) {
			if (!locked)
				rtnl_lock();

			ice_vsi_close(vsi);

			if (!locked)
				rtnl_unlock();
		} else {
			ice_vsi_close(vsi);
		}
	} else if (vsi->type == ICE_VSI_CTRL ||
		   vsi->type == ICE_VSI_SWITCHDEV_CTRL) {
		ice_vsi_close(vsi);
	}
}

/**
 * ice_vsi_dis_irq - Mask off queue interrupt generation on the VSI
 * @vsi: the VSI being un-configured
 */
void ice_vsi_dis_irq(struct ice_vsi *vsi)
{
	int base = vsi->base_vector;
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
	ice_for_each_q_vector(vsi, i) {
		if (!vsi->q_vectors[i])
			continue;
		wr32(hw, GLINT_DYN_CTL(vsi->q_vectors[i]->reg_idx), 0);
	}

	ice_flush(hw);

	/* don't call synchronize_irq() for VF's from the host */
	if (vsi->type == ICE_VSI_VF)
		return;

	ice_for_each_q_vector(vsi, i)
		synchronize_irq(pf->msix_entries[i + base].vector);
}

/**
 * ice_napi_del - Remove NAPI handler for the VSI
 * @vsi: VSI for which NAPI handler is to be removed
 */
void ice_napi_del(struct ice_vsi *vsi)
{
	int v_idx;

	if (!vsi->netdev)
		return;

	ice_for_each_q_vector(vsi, v_idx)
		netif_napi_del(&vsi->q_vectors[v_idx]->napi);
}

/**
 * ice_free_vf_ctrl_res - Free the VF control VSI resource
 * @pf: pointer to PF structure
 * @vsi: the VSI to free resources for
 *
 * Check if the VF control VSI resource is still in use. If no VF is using it
 * any more, release the VSI resource. Otherwise, leave it to be cleaned up
 * once no other VF uses it.
 */
static void ice_free_vf_ctrl_res(struct ice_pf *pf,  struct ice_vsi *vsi)
{
	struct ice_vf *vf;
	unsigned int bkt;

	rcu_read_lock();
	ice_for_each_vf_rcu(pf, bkt, vf) {
		if (vf != vsi->vf && vf->ctrl_vsi_idx != ICE_NO_VSI) {
			rcu_read_unlock();
			return;
		}
	}
	rcu_read_unlock();

	/* No other VFs left that have control VSI. It is now safe to reclaim
	 * SW interrupts back to the common pool.
	 */
	ice_free_res(pf->irq_tracker, vsi->base_vector,
		     ICE_RES_VF_CTRL_VEC_ID);
	pf->num_avail_sw_msix += vsi->num_q_vectors;
}

/**
 * ice_vsi_release - Delete a VSI and free its resources
 * @vsi: the VSI being removed
 *
 * Returns 0 on success or < 0 on error
 */
int ice_vsi_release(struct ice_vsi *vsi)
{
	struct ice_pf *pf;
	int err;

	if (!vsi->back)
		return -ENODEV;
	pf = vsi->back;

	/* do not unregister while driver is in the reset recovery pending
	 * state. Since reset/rebuild happens through PF service task workqueue,
	 * it's not a good idea to unregister netdev that is associated to the
	 * PF that is running the work queue items currently. This is done to
	 * avoid check_flush_dependency() warning on this wq
	 */
	if (vsi->netdev && !ice_is_reset_in_progress(pf->state) &&
	    (test_bit(ICE_VSI_NETDEV_REGISTERED, vsi->state))) {
		unregister_netdev(vsi->netdev);
		clear_bit(ICE_VSI_NETDEV_REGISTERED, vsi->state);
	}

	if (vsi->type == ICE_VSI_PF)
		ice_devlink_destroy_pf_port(pf);

	if (test_bit(ICE_FLAG_RSS_ENA, pf->flags))
		ice_rss_clean(vsi);

	/* Disable VSI and free resources */
	if (vsi->type != ICE_VSI_LB)
		ice_vsi_dis_irq(vsi);
	ice_vsi_close(vsi);

	/* SR-IOV determines needed MSIX resources all at once instead of per
	 * VSI since when VFs are spawned we know how many VFs there are and how
	 * many interrupts each VF needs. SR-IOV MSIX resources are also
	 * cleared in the same manner.
	 */
	if (vsi->type == ICE_VSI_CTRL && vsi->vf) {
		ice_free_vf_ctrl_res(pf, vsi);
	} else if (vsi->type != ICE_VSI_VF) {
		/* reclaim SW interrupts back to the common pool */
		ice_free_res(pf->irq_tracker, vsi->base_vector, vsi->idx);
		pf->num_avail_sw_msix += vsi->num_q_vectors;
	}

	if (!ice_is_safe_mode(pf)) {
		if (vsi->type == ICE_VSI_PF) {
			ice_fltr_remove_eth(vsi, ETH_P_PAUSE, ICE_FLTR_TX,
					    ICE_DROP_PACKET);
			ice_cfg_sw_lldp(vsi, true, false);
			/* The Rx rule will only exist to remove if the LLDP FW
			 * engine is currently stopped
			 */
			if (!test_bit(ICE_FLAG_FW_LLDP_AGENT, pf->flags))
				ice_cfg_sw_lldp(vsi, false, false);
		}
	}

	ice_fltr_remove_all(vsi);
	ice_rm_vsi_lan_cfg(vsi->port_info, vsi->idx);
	err = ice_rm_vsi_rdma_cfg(vsi->port_info, vsi->idx);
	if (err)
		dev_err(ice_pf_to_dev(vsi->back), "Failed to remove RDMA scheduler config for VSI %u, err %d\n",
			vsi->vsi_num, err);
	ice_vsi_delete(vsi);
	ice_vsi_free_q_vectors(vsi);

	if (vsi->netdev) {
		if (test_bit(ICE_VSI_NETDEV_REGISTERED, vsi->state)) {
			unregister_netdev(vsi->netdev);
			clear_bit(ICE_VSI_NETDEV_REGISTERED, vsi->state);
		}
		if (test_bit(ICE_VSI_NETDEV_ALLOCD, vsi->state)) {
			free_netdev(vsi->netdev);
			vsi->netdev = NULL;
			clear_bit(ICE_VSI_NETDEV_ALLOCD, vsi->state);
		}
	}

	if (vsi->type == ICE_VSI_VF &&
	    vsi->agg_node && vsi->agg_node->valid)
		vsi->agg_node->num_vsis--;
	ice_vsi_clear_rings(vsi);

	ice_vsi_put_qs(vsi);

	/* retain SW VSI data structure since it is needed to unregister and
	 * free VSI netdev when PF is not in reset recovery pending state,\
	 * for ex: during rmmod.
	 */
	if (!ice_is_reset_in_progress(pf->state))
		ice_vsi_clear(vsi);

	return 0;
}

/**
 * ice_vsi_rebuild_get_coalesce - get coalesce from all q_vectors
 * @vsi: VSI connected with q_vectors
 * @coalesce: array of struct with stored coalesce
 *
 * Returns array size.
 */
static int
ice_vsi_rebuild_get_coalesce(struct ice_vsi *vsi,
			     struct ice_coalesce_stored *coalesce)
{
	int i;

	ice_for_each_q_vector(vsi, i) {
		struct ice_q_vector *q_vector = vsi->q_vectors[i];

		coalesce[i].itr_tx = q_vector->tx.itr_setting;
		coalesce[i].itr_rx = q_vector->rx.itr_setting;
		coalesce[i].intrl = q_vector->intrl;

		if (i < vsi->num_txq)
			coalesce[i].tx_valid = true;
		if (i < vsi->num_rxq)
			coalesce[i].rx_valid = true;
	}

	return vsi->num_q_vectors;
}

/**
 * ice_vsi_rebuild_set_coalesce - set coalesce from earlier saved arrays
 * @vsi: VSI connected with q_vectors
 * @coalesce: pointer to array of struct with stored coalesce
 * @size: size of coalesce array
 *
 * Before this function, ice_vsi_rebuild_get_coalesce should be called to save
 * ITR params in arrays. If size is 0 or coalesce wasn't stored set coalesce
 * to default value.
 */
static void
ice_vsi_rebuild_set_coalesce(struct ice_vsi *vsi,
			     struct ice_coalesce_stored *coalesce, int size)
{
	struct ice_ring_container *rc;
	int i;

	if ((size && !coalesce) || !vsi)
		return;

	/* There are a couple of cases that have to be handled here:
	 *   1. The case where the number of queue vectors stays the same, but
	 *      the number of Tx or Rx rings changes (the first for loop)
	 *   2. The case where the number of queue vectors increased (the
	 *      second for loop)
	 */
	for (i = 0; i < size && i < vsi->num_q_vectors; i++) {
		/* There are 2 cases to handle here and they are the same for
		 * both Tx and Rx:
		 *   if the entry was valid previously (coalesce[i].[tr]x_valid
		 *   and the loop variable is less than the number of rings
		 *   allocated, then write the previous values
		 *
		 *   if the entry was not valid previously, but the number of
		 *   rings is less than are allocated (this means the number of
		 *   rings increased from previously), then write out the
		 *   values in the first element
		 *
		 *   Also, always write the ITR, even if in ITR_IS_DYNAMIC
		 *   as there is no harm because the dynamic algorithm
		 *   will just overwrite.
		 */
		if (i < vsi->alloc_rxq && coalesce[i].rx_valid) {
			rc = &vsi->q_vectors[i]->rx;
			rc->itr_setting = coalesce[i].itr_rx;
			ice_write_itr(rc, rc->itr_setting);
		} else if (i < vsi->alloc_rxq) {
			rc = &vsi->q_vectors[i]->rx;
			rc->itr_setting = coalesce[0].itr_rx;
			ice_write_itr(rc, rc->itr_setting);
		}

		if (i < vsi->alloc_txq && coalesce[i].tx_valid) {
			rc = &vsi->q_vectors[i]->tx;
			rc->itr_setting = coalesce[i].itr_tx;
			ice_write_itr(rc, rc->itr_setting);
		} else if (i < vsi->alloc_txq) {
			rc = &vsi->q_vectors[i]->tx;
			rc->itr_setting = coalesce[0].itr_tx;
			ice_write_itr(rc, rc->itr_setting);
		}

		vsi->q_vectors[i]->intrl = coalesce[i].intrl;
		ice_set_q_vector_intrl(vsi->q_vectors[i]);
	}

	/* the number of queue vectors increased so write whatever is in
	 * the first element
	 */
	for (; i < vsi->num_q_vectors; i++) {
		/* transmit */
		rc = &vsi->q_vectors[i]->tx;
		rc->itr_setting = coalesce[0].itr_tx;
		ice_write_itr(rc, rc->itr_setting);

		/* receive */
		rc = &vsi->q_vectors[i]->rx;
		rc->itr_setting = coalesce[0].itr_rx;
		ice_write_itr(rc, rc->itr_setting);

		vsi->q_vectors[i]->intrl = coalesce[0].intrl;
		ice_set_q_vector_intrl(vsi->q_vectors[i]);
	}
}

/**
 * ice_vsi_rebuild - Rebuild VSI after reset
 * @vsi: VSI to be rebuild
 * @init_vsi: is this an initialization or a reconfigure of the VSI
 *
 * Returns 0 on success and negative value on failure
 */
int ice_vsi_rebuild(struct ice_vsi *vsi, bool init_vsi)
{
	u16 max_txqs[ICE_MAX_TRAFFIC_CLASS] = { 0 };
	struct ice_coalesce_stored *coalesce;
	int prev_num_q_vectors = 0;
	enum ice_vsi_type vtype;
	struct ice_pf *pf;
	int ret, i;

	if (!vsi)
		return -EINVAL;

	pf = vsi->back;
	vtype = vsi->type;
	if (WARN_ON(vtype == ICE_VSI_VF) && !vsi->vf)
		return -EINVAL;

	ice_vsi_init_vlan_ops(vsi);

	coalesce = kcalloc(vsi->num_q_vectors,
			   sizeof(struct ice_coalesce_stored), GFP_KERNEL);
	if (!coalesce)
		return -ENOMEM;

	prev_num_q_vectors = ice_vsi_rebuild_get_coalesce(vsi, coalesce);

	ice_rm_vsi_lan_cfg(vsi->port_info, vsi->idx);
	ret = ice_rm_vsi_rdma_cfg(vsi->port_info, vsi->idx);
	if (ret)
		dev_err(ice_pf_to_dev(vsi->back), "Failed to remove RDMA scheduler config for VSI %u, err %d\n",
			vsi->vsi_num, ret);
	ice_vsi_free_q_vectors(vsi);

	/* SR-IOV determines needed MSIX resources all at once instead of per
	 * VSI since when VFs are spawned we know how many VFs there are and how
	 * many interrupts each VF needs. SR-IOV MSIX resources are also
	 * cleared in the same manner.
	 */
	if (vtype != ICE_VSI_VF) {
		/* reclaim SW interrupts back to the common pool */
		ice_free_res(pf->irq_tracker, vsi->base_vector, vsi->idx);
		pf->num_avail_sw_msix += vsi->num_q_vectors;
		vsi->base_vector = 0;
	}

	if (ice_is_xdp_ena_vsi(vsi))
		/* return value check can be skipped here, it always returns
		 * 0 if reset is in progress
		 */
		ice_destroy_xdp_rings(vsi);
	ice_vsi_put_qs(vsi);
	ice_vsi_clear_rings(vsi);
	ice_vsi_free_arrays(vsi);
	if (vtype == ICE_VSI_VF)
		ice_vsi_set_num_qs(vsi, vsi->vf);
	else
		ice_vsi_set_num_qs(vsi, NULL);

	ret = ice_vsi_alloc_arrays(vsi);
	if (ret < 0)
		goto err_vsi;

	ice_vsi_get_qs(vsi);

	ice_alloc_fd_res(vsi);
	ice_vsi_set_tc_cfg(vsi);

	/* Initialize VSI struct elements and create VSI in FW */
	ret = ice_vsi_init(vsi, init_vsi);
	if (ret < 0)
		goto err_vsi;

	switch (vtype) {
	case ICE_VSI_CTRL:
	case ICE_VSI_SWITCHDEV_CTRL:
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
		if (ice_is_xdp_ena_vsi(vsi)) {
			ret = ice_vsi_determine_xdp_res(vsi);
			if (ret)
				goto err_vectors;
			ret = ice_prepare_xdp_rings(vsi, vsi->xdp_prog);
			if (ret)
				goto err_vectors;
		}
		/* ICE_VSI_CTRL does not need RSS so skip RSS processing */
		if (vtype != ICE_VSI_CTRL)
			/* Do not exit if configuring RSS had an issue, at
			 * least receive traffic on first queue. Hence no
			 * need to capture return value
			 */
			if (test_bit(ICE_FLAG_RSS_ENA, pf->flags))
				ice_vsi_cfg_rss_lut_key(vsi);
		break;
	case ICE_VSI_VF:
		ret = ice_vsi_alloc_q_vectors(vsi);
		if (ret)
			goto err_rings;

		ret = ice_vsi_set_q_vectors_reg_idx(vsi);
		if (ret)
			goto err_vectors;

		ret = ice_vsi_alloc_rings(vsi);
		if (ret)
			goto err_vectors;

		break;
	case ICE_VSI_CHNL:
		if (test_bit(ICE_FLAG_RSS_ENA, pf->flags)) {
			ice_vsi_cfg_rss_lut_key(vsi);
			ice_vsi_set_rss_flow_fld(vsi);
		}
		break;
	default:
		break;
	}

	/* configure VSI nodes based on number of queues and TC's */
	for (i = 0; i < vsi->tc_cfg.numtc; i++) {
		/* configure VSI nodes based on number of queues and TC's.
		 * ADQ creates VSIs for each TC/Channel but doesn't
		 * allocate queues instead it reconfigures the PF queues
		 * as per the TC command. So max_txqs should point to the
		 * PF Tx queues.
		 */
		if (vtype == ICE_VSI_CHNL)
			max_txqs[i] = pf->num_lan_tx;
		else
			max_txqs[i] = vsi->alloc_txq;

		if (ice_is_xdp_ena_vsi(vsi))
			max_txqs[i] += vsi->num_xdp_txq;
	}

	if (test_bit(ICE_FLAG_TC_MQPRIO, pf->flags))
		/* If MQPRIO is set, means channel code path, hence for main
		 * VSI's, use TC as 1
		 */
		ret = ice_cfg_vsi_lan(vsi->port_info, vsi->idx, 1, max_txqs);
	else
		ret = ice_cfg_vsi_lan(vsi->port_info, vsi->idx,
				      vsi->tc_cfg.ena_tc, max_txqs);

	if (ret) {
		dev_err(ice_pf_to_dev(pf), "VSI %d failed lan queue config, error %d\n",
			vsi->vsi_num, ret);
		if (init_vsi) {
			ret = -EIO;
			goto err_vectors;
		} else {
			return ice_schedule_reset(pf, ICE_RESET_PFR);
		}
	}
	ice_vsi_rebuild_set_coalesce(vsi, coalesce, prev_num_q_vectors);
	kfree(coalesce);

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
	set_bit(ICE_RESET_FAILED, pf->state);
	kfree(coalesce);
	return ret;
}

/**
 * ice_is_reset_in_progress - check for a reset in progress
 * @state: PF state field
 */
bool ice_is_reset_in_progress(unsigned long *state)
{
	return test_bit(ICE_RESET_OICR_RECV, state) ||
	       test_bit(ICE_PFR_REQ, state) ||
	       test_bit(ICE_CORER_REQ, state) ||
	       test_bit(ICE_GLOBR_REQ, state);
}

/**
 * ice_wait_for_reset - Wait for driver to finish reset and rebuild
 * @pf: pointer to the PF structure
 * @timeout: length of time to wait, in jiffies
 *
 * Wait (sleep) for a short time until the driver finishes cleaning up from
 * a device reset. The caller must be able to sleep. Use this to delay
 * operations that could fail while the driver is cleaning up after a device
 * reset.
 *
 * Returns 0 on success, -EBUSY if the reset is not finished within the
 * timeout, and -ERESTARTSYS if the thread was interrupted.
 */
int ice_wait_for_reset(struct ice_pf *pf, unsigned long timeout)
{
	long ret;

	ret = wait_event_interruptible_timeout(pf->reset_wait_queue,
					       !ice_is_reset_in_progress(pf->state),
					       timeout);
	if (ret < 0)
		return ret;
	else if (!ret)
		return -EBUSY;
	else
		return 0;
}

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
void ice_vsi_cfg_netdev_tc(struct ice_vsi *vsi, u8 ena_tc)
{
	struct net_device *netdev = vsi->netdev;
	struct ice_pf *pf = vsi->back;
	int numtc = vsi->tc_cfg.numtc;
	struct ice_dcbx_cfg *dcbcfg;
	u8 netdev_tc;
	int i;

	if (!netdev)
		return;

	/* CHNL VSI doesn't have it's own netdev, hence, no netdev_tc */
	if (vsi->type == ICE_VSI_CHNL)
		return;

	if (!ena_tc) {
		netdev_reset_tc(netdev);
		return;
	}

	if (vsi->type == ICE_VSI_PF && ice_is_adq_active(pf))
		numtc = vsi->all_numtc;

	if (netdev_set_num_tc(netdev, numtc))
		return;

	dcbcfg = &pf->hw.port_info->qos_cfg.local_dcbx_cfg;

	ice_for_each_traffic_class(i)
		if (vsi->tc_cfg.ena_tc & BIT(i))
			netdev_set_tc_queue(netdev,
					    vsi->tc_cfg.tc_info[i].netdev_tc,
					    vsi->tc_cfg.tc_info[i].qcount_tx,
					    vsi->tc_cfg.tc_info[i].qoffset);
	/* setup TC queue map for CHNL TCs */
	ice_for_each_chnl_tc(i) {
		if (!(vsi->all_enatc & BIT(i)))
			break;
		if (!vsi->mqprio_qopt.qopt.count[i])
			break;
		netdev_set_tc_queue(netdev, i,
				    vsi->mqprio_qopt.qopt.count[i],
				    vsi->mqprio_qopt.qopt.offset[i]);
	}

	if (test_bit(ICE_FLAG_TC_MQPRIO, pf->flags))
		return;

	for (i = 0; i < ICE_MAX_USER_PRIORITY; i++) {
		u8 ets_tc = dcbcfg->etscfg.prio_table[i];

		/* Get the mapped netdev TC# for the UP */
		netdev_tc = vsi->tc_cfg.tc_info[ets_tc].netdev_tc;
		netdev_set_prio_tc_map(netdev, i, netdev_tc);
	}
}

/**
 * ice_vsi_setup_q_map_mqprio - Prepares mqprio based tc_config
 * @vsi: the VSI being configured,
 * @ctxt: VSI context structure
 * @ena_tc: number of traffic classes to enable
 *
 * Prepares VSI tc_config to have queue configurations based on MQPRIO options.
 */
static void
ice_vsi_setup_q_map_mqprio(struct ice_vsi *vsi, struct ice_vsi_ctx *ctxt,
			   u8 ena_tc)
{
	u16 pow, offset = 0, qcount_tx = 0, qcount_rx = 0, qmap;
	u16 tc0_offset = vsi->mqprio_qopt.qopt.offset[0];
	int tc0_qcount = vsi->mqprio_qopt.qopt.count[0];
	u8 netdev_tc = 0;
	int i;

	vsi->tc_cfg.ena_tc = ena_tc ? ena_tc : 1;

	pow = order_base_2(tc0_qcount);
	qmap = ((tc0_offset << ICE_AQ_VSI_TC_Q_OFFSET_S) &
		ICE_AQ_VSI_TC_Q_OFFSET_M) |
		((pow << ICE_AQ_VSI_TC_Q_NUM_S) & ICE_AQ_VSI_TC_Q_NUM_M);

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

		offset = vsi->mqprio_qopt.qopt.offset[i];
		qcount_rx = vsi->mqprio_qopt.qopt.count[i];
		qcount_tx = vsi->mqprio_qopt.qopt.count[i];
		vsi->tc_cfg.tc_info[i].qoffset = offset;
		vsi->tc_cfg.tc_info[i].qcount_rx = qcount_rx;
		vsi->tc_cfg.tc_info[i].qcount_tx = qcount_tx;
		vsi->tc_cfg.tc_info[i].netdev_tc = netdev_tc++;
	}

	if (vsi->all_numtc && vsi->all_numtc != vsi->tc_cfg.numtc) {
		ice_for_each_chnl_tc(i) {
			if (!(vsi->all_enatc & BIT(i)))
				continue;
			offset = vsi->mqprio_qopt.qopt.offset[i];
			qcount_rx = vsi->mqprio_qopt.qopt.count[i];
			qcount_tx = vsi->mqprio_qopt.qopt.count[i];
		}
	}

	/* Set actual Tx/Rx queue pairs */
	vsi->num_txq = offset + qcount_tx;
	vsi->num_rxq = offset + qcount_rx;

	/* Setup queue TC[0].qmap for given VSI context */
	ctxt->info.tc_mapping[0] = cpu_to_le16(qmap);
	ctxt->info.q_mapping[0] = cpu_to_le16(vsi->rxq_map[0]);
	ctxt->info.q_mapping[1] = cpu_to_le16(tc0_qcount);

	/* Find queue count available for channel VSIs and starting offset
	 * for channel VSIs
	 */
	if (tc0_qcount && tc0_qcount < vsi->num_rxq) {
		vsi->cnt_q_avail = vsi->num_rxq - tc0_qcount;
		vsi->next_base_q = tc0_qcount;
	}
	dev_dbg(ice_pf_to_dev(vsi->back), "vsi->num_txq = %d\n",  vsi->num_txq);
	dev_dbg(ice_pf_to_dev(vsi->back), "vsi->num_rxq = %d\n",  vsi->num_rxq);
	dev_dbg(ice_pf_to_dev(vsi->back), "all_numtc %u, all_enatc: 0x%04x, tc_cfg.numtc %u\n",
		vsi->all_numtc, vsi->all_enatc, vsi->tc_cfg.numtc);
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
	struct ice_pf *pf = vsi->back;
	struct ice_vsi_ctx *ctx;
	struct device *dev;
	int i, ret = 0;
	u8 num_tc = 0;

	dev = ice_pf_to_dev(pf);
	if (vsi->tc_cfg.ena_tc == ena_tc &&
	    vsi->mqprio_qopt.mode != TC_MQPRIO_MODE_CHANNEL)
		return ret;

	ice_for_each_traffic_class(i) {
		/* build bitmap of enabled TCs */
		if (ena_tc & BIT(i))
			num_tc++;
		/* populate max_txqs per TC */
		max_txqs[i] = vsi->alloc_txq;
		/* Update max_txqs if it is CHNL VSI, because alloc_t[r]xq are
		 * zero for CHNL VSI, hence use num_txq instead as max_txqs
		 */
		if (vsi->type == ICE_VSI_CHNL &&
		    test_bit(ICE_FLAG_TC_MQPRIO, pf->flags))
			max_txqs[i] = vsi->num_txq;
	}

	vsi->tc_cfg.ena_tc = ena_tc;
	vsi->tc_cfg.numtc = num_tc;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->vf_num = 0;
	ctx->info = vsi->info;

	if (vsi->type == ICE_VSI_PF &&
	    test_bit(ICE_FLAG_TC_MQPRIO, pf->flags))
		ice_vsi_setup_q_map_mqprio(vsi, ctx, ena_tc);
	else
		ice_vsi_setup_q_map(vsi, ctx);

	/* must to indicate which section of VSI context are being modified */
	ctx->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_RXQ_MAP_VALID);
	ret = ice_update_vsi(&pf->hw, vsi->idx, ctx, NULL);
	if (ret) {
		dev_info(dev, "Failed VSI Update\n");
		goto out;
	}

	if (vsi->type == ICE_VSI_PF &&
	    test_bit(ICE_FLAG_TC_MQPRIO, pf->flags))
		ret = ice_cfg_vsi_lan(vsi->port_info, vsi->idx, 1, max_txqs);
	else
		ret = ice_cfg_vsi_lan(vsi->port_info, vsi->idx,
				      vsi->tc_cfg.ena_tc, max_txqs);

	if (ret) {
		dev_err(dev, "VSI %d failed TC config, error %d\n",
			vsi->vsi_num, ret);
		goto out;
	}
	ice_vsi_update_q_map(vsi, ctx);
	vsi->info.valid_sections = 0;

	ice_vsi_cfg_netdev_tc(vsi, ena_tc);
out:
	kfree(ctx);
	return ret;
}

/**
 * ice_update_ring_stats - Update ring statistics
 * @stats: stats to be updated
 * @pkts: number of processed packets
 * @bytes: number of processed bytes
 *
 * This function assumes that caller has acquired a u64_stats_sync lock.
 */
static void ice_update_ring_stats(struct ice_q_stats *stats, u64 pkts, u64 bytes)
{
	stats->bytes += bytes;
	stats->pkts += pkts;
}

/**
 * ice_update_tx_ring_stats - Update Tx ring specific counters
 * @tx_ring: ring to update
 * @pkts: number of processed packets
 * @bytes: number of processed bytes
 */
void ice_update_tx_ring_stats(struct ice_tx_ring *tx_ring, u64 pkts, u64 bytes)
{
	u64_stats_update_begin(&tx_ring->syncp);
	ice_update_ring_stats(&tx_ring->stats, pkts, bytes);
	u64_stats_update_end(&tx_ring->syncp);
}

/**
 * ice_update_rx_ring_stats - Update Rx ring specific counters
 * @rx_ring: ring to update
 * @pkts: number of processed packets
 * @bytes: number of processed bytes
 */
void ice_update_rx_ring_stats(struct ice_rx_ring *rx_ring, u64 pkts, u64 bytes)
{
	u64_stats_update_begin(&rx_ring->syncp);
	ice_update_ring_stats(&rx_ring->stats, pkts, bytes);
	u64_stats_update_end(&rx_ring->syncp);
}

/**
 * ice_is_dflt_vsi_in_use - check if the default forwarding VSI is being used
 * @sw: switch to check if its default forwarding VSI is free
 *
 * Return true if the default forwarding VSI is already being used, else returns
 * false signalling that it's available to use.
 */
bool ice_is_dflt_vsi_in_use(struct ice_sw *sw)
{
	return (sw->dflt_vsi && sw->dflt_vsi_ena);
}

/**
 * ice_is_vsi_dflt_vsi - check if the VSI passed in is the default VSI
 * @sw: switch for the default forwarding VSI to compare against
 * @vsi: VSI to compare against default forwarding VSI
 *
 * If this VSI passed in is the default forwarding VSI then return true, else
 * return false
 */
bool ice_is_vsi_dflt_vsi(struct ice_sw *sw, struct ice_vsi *vsi)
{
	return (sw->dflt_vsi == vsi && sw->dflt_vsi_ena);
}

/**
 * ice_set_dflt_vsi - set the default forwarding VSI
 * @sw: switch used to assign the default forwarding VSI
 * @vsi: VSI getting set as the default forwarding VSI on the switch
 *
 * If the VSI passed in is already the default VSI and it's enabled just return
 * success.
 *
 * If there is already a default VSI on the switch and it's enabled then return
 * -EEXIST since there can only be one default VSI per switch.
 *
 *  Otherwise try to set the VSI passed in as the switch's default VSI and
 *  return the result.
 */
int ice_set_dflt_vsi(struct ice_sw *sw, struct ice_vsi *vsi)
{
	struct device *dev;
	int status;

	if (!sw || !vsi)
		return -EINVAL;

	dev = ice_pf_to_dev(vsi->back);

	/* the VSI passed in is already the default VSI */
	if (ice_is_vsi_dflt_vsi(sw, vsi)) {
		dev_dbg(dev, "VSI %d passed in is already the default forwarding VSI, nothing to do\n",
			vsi->vsi_num);
		return 0;
	}

	/* another VSI is already the default VSI for this switch */
	if (ice_is_dflt_vsi_in_use(sw)) {
		dev_err(dev, "Default forwarding VSI %d already in use, disable it and try again\n",
			sw->dflt_vsi->vsi_num);
		return -EEXIST;
	}

	status = ice_cfg_dflt_vsi(&vsi->back->hw, vsi->idx, true, ICE_FLTR_RX);
	if (status) {
		dev_err(dev, "Failed to set VSI %d as the default forwarding VSI, error %d\n",
			vsi->vsi_num, status);
		return status;
	}

	sw->dflt_vsi = vsi;
	sw->dflt_vsi_ena = true;

	return 0;
}

/**
 * ice_clear_dflt_vsi - clear the default forwarding VSI
 * @sw: switch used to clear the default VSI
 *
 * If the switch has no default VSI or it's not enabled then return error.
 *
 * Otherwise try to clear the default VSI and return the result.
 */
int ice_clear_dflt_vsi(struct ice_sw *sw)
{
	struct ice_vsi *dflt_vsi;
	struct device *dev;
	int status;

	if (!sw)
		return -EINVAL;

	dev = ice_pf_to_dev(sw->pf);

	dflt_vsi = sw->dflt_vsi;

	/* there is no default VSI configured */
	if (!ice_is_dflt_vsi_in_use(sw))
		return -ENODEV;

	status = ice_cfg_dflt_vsi(&dflt_vsi->back->hw, dflt_vsi->idx, false,
				  ICE_FLTR_RX);
	if (status) {
		dev_err(dev, "Failed to clear the default forwarding VSI %d, error %d\n",
			dflt_vsi->vsi_num, status);
		return -EIO;
	}

	sw->dflt_vsi = NULL;
	sw->dflt_vsi_ena = false;

	return 0;
}

/**
 * ice_get_link_speed_mbps - get link speed in Mbps
 * @vsi: the VSI whose link speed is being queried
 *
 * Return current VSI link speed and 0 if the speed is unknown.
 */
int ice_get_link_speed_mbps(struct ice_vsi *vsi)
{
	switch (vsi->port_info->phy.link_info.link_speed) {
	case ICE_AQ_LINK_SPEED_100GB:
		return SPEED_100000;
	case ICE_AQ_LINK_SPEED_50GB:
		return SPEED_50000;
	case ICE_AQ_LINK_SPEED_40GB:
		return SPEED_40000;
	case ICE_AQ_LINK_SPEED_25GB:
		return SPEED_25000;
	case ICE_AQ_LINK_SPEED_20GB:
		return SPEED_20000;
	case ICE_AQ_LINK_SPEED_10GB:
		return SPEED_10000;
	case ICE_AQ_LINK_SPEED_5GB:
		return SPEED_5000;
	case ICE_AQ_LINK_SPEED_2500MB:
		return SPEED_2500;
	case ICE_AQ_LINK_SPEED_1000MB:
		return SPEED_1000;
	case ICE_AQ_LINK_SPEED_100MB:
		return SPEED_100;
	case ICE_AQ_LINK_SPEED_10MB:
		return SPEED_10;
	case ICE_AQ_LINK_SPEED_UNKNOWN:
	default:
		return 0;
	}
}

/**
 * ice_get_link_speed_kbps - get link speed in Kbps
 * @vsi: the VSI whose link speed is being queried
 *
 * Return current VSI link speed and 0 if the speed is unknown.
 */
int ice_get_link_speed_kbps(struct ice_vsi *vsi)
{
	int speed_mbps;

	speed_mbps = ice_get_link_speed_mbps(vsi);

	return speed_mbps * 1000;
}

/**
 * ice_set_min_bw_limit - setup minimum BW limit for Tx based on min_tx_rate
 * @vsi: VSI to be configured
 * @min_tx_rate: min Tx rate in Kbps to be configured as BW limit
 *
 * If the min_tx_rate is specified as 0 that means to clear the minimum BW limit
 * profile, otherwise a non-zero value will force a minimum BW limit for the VSI
 * on TC 0.
 */
int ice_set_min_bw_limit(struct ice_vsi *vsi, u64 min_tx_rate)
{
	struct ice_pf *pf = vsi->back;
	struct device *dev;
	int status;
	int speed;

	dev = ice_pf_to_dev(pf);
	if (!vsi->port_info) {
		dev_dbg(dev, "VSI %d, type %u specified doesn't have valid port_info\n",
			vsi->idx, vsi->type);
		return -EINVAL;
	}

	speed = ice_get_link_speed_kbps(vsi);
	if (min_tx_rate > (u64)speed) {
		dev_err(dev, "invalid min Tx rate %llu Kbps specified for %s %d is greater than current link speed %u Kbps\n",
			min_tx_rate, ice_vsi_type_str(vsi->type), vsi->idx,
			speed);
		return -EINVAL;
	}

	/* Configure min BW for VSI limit */
	if (min_tx_rate) {
		status = ice_cfg_vsi_bw_lmt_per_tc(vsi->port_info, vsi->idx, 0,
						   ICE_MIN_BW, min_tx_rate);
		if (status) {
			dev_err(dev, "failed to set min Tx rate(%llu Kbps) for %s %d\n",
				min_tx_rate, ice_vsi_type_str(vsi->type),
				vsi->idx);
			return status;
		}

		dev_dbg(dev, "set min Tx rate(%llu Kbps) for %s\n",
			min_tx_rate, ice_vsi_type_str(vsi->type));
	} else {
		status = ice_cfg_vsi_bw_dflt_lmt_per_tc(vsi->port_info,
							vsi->idx, 0,
							ICE_MIN_BW);
		if (status) {
			dev_err(dev, "failed to clear min Tx rate configuration for %s %d\n",
				ice_vsi_type_str(vsi->type), vsi->idx);
			return status;
		}

		dev_dbg(dev, "cleared min Tx rate configuration for %s %d\n",
			ice_vsi_type_str(vsi->type), vsi->idx);
	}

	return 0;
}

/**
 * ice_set_max_bw_limit - setup maximum BW limit for Tx based on max_tx_rate
 * @vsi: VSI to be configured
 * @max_tx_rate: max Tx rate in Kbps to be configured as BW limit
 *
 * If the max_tx_rate is specified as 0 that means to clear the maximum BW limit
 * profile, otherwise a non-zero value will force a maximum BW limit for the VSI
 * on TC 0.
 */
int ice_set_max_bw_limit(struct ice_vsi *vsi, u64 max_tx_rate)
{
	struct ice_pf *pf = vsi->back;
	struct device *dev;
	int status;
	int speed;

	dev = ice_pf_to_dev(pf);
	if (!vsi->port_info) {
		dev_dbg(dev, "VSI %d, type %u specified doesn't have valid port_info\n",
			vsi->idx, vsi->type);
		return -EINVAL;
	}

	speed = ice_get_link_speed_kbps(vsi);
	if (max_tx_rate > (u64)speed) {
		dev_err(dev, "invalid max Tx rate %llu Kbps specified for %s %d is greater than current link speed %u Kbps\n",
			max_tx_rate, ice_vsi_type_str(vsi->type), vsi->idx,
			speed);
		return -EINVAL;
	}

	/* Configure max BW for VSI limit */
	if (max_tx_rate) {
		status = ice_cfg_vsi_bw_lmt_per_tc(vsi->port_info, vsi->idx, 0,
						   ICE_MAX_BW, max_tx_rate);
		if (status) {
			dev_err(dev, "failed setting max Tx rate(%llu Kbps) for %s %d\n",
				max_tx_rate, ice_vsi_type_str(vsi->type),
				vsi->idx);
			return status;
		}

		dev_dbg(dev, "set max Tx rate(%llu Kbps) for %s %d\n",
			max_tx_rate, ice_vsi_type_str(vsi->type), vsi->idx);
	} else {
		status = ice_cfg_vsi_bw_dflt_lmt_per_tc(vsi->port_info,
							vsi->idx, 0,
							ICE_MAX_BW);
		if (status) {
			dev_err(dev, "failed clearing max Tx rate configuration for %s %d\n",
				ice_vsi_type_str(vsi->type), vsi->idx);
			return status;
		}

		dev_dbg(dev, "cleared max Tx rate configuration for %s %d\n",
			ice_vsi_type_str(vsi->type), vsi->idx);
	}

	return 0;
}

/**
 * ice_set_link - turn on/off physical link
 * @vsi: VSI to modify physical link on
 * @ena: turn on/off physical link
 */
int ice_set_link(struct ice_vsi *vsi, bool ena)
{
	struct device *dev = ice_pf_to_dev(vsi->back);
	struct ice_port_info *pi = vsi->port_info;
	struct ice_hw *hw = pi->hw;
	int status;

	if (vsi->type != ICE_VSI_PF)
		return -EINVAL;

	status = ice_aq_set_link_restart_an(pi, ena, NULL);

	/* if link is owned by manageability, FW will return ICE_AQ_RC_EMODE.
	 * this is not a fatal error, so print a warning message and return
	 * a success code. Return an error if FW returns an error code other
	 * than ICE_AQ_RC_EMODE
	 */
	if (status == -EIO) {
		if (hw->adminq.sq_last_status == ICE_AQ_RC_EMODE)
			dev_dbg(dev, "can't set link to %s, err %d aq_err %s. not fatal, continuing\n",
				(ena ? "ON" : "OFF"), status,
				ice_aq_str(hw->adminq.sq_last_status));
	} else if (status) {
		dev_err(dev, "can't set link to %s, err %d aq_err %s\n",
			(ena ? "ON" : "OFF"), status,
			ice_aq_str(hw->adminq.sq_last_status));
		return status;
	}

	return 0;
}

/**
 * ice_vsi_add_vlan_zero - add VLAN 0 filter(s) for this VSI
 * @vsi: VSI used to add VLAN filters
 *
 * In Single VLAN Mode (SVM), single VLAN filters via ICE_SW_LKUP_VLAN are based
 * on the inner VLAN ID, so the VLAN TPID (i.e. 0x8100 or 0x888a8) doesn't
 * matter. In Double VLAN Mode (DVM), outer/single VLAN filters via
 * ICE_SW_LKUP_VLAN are based on the outer/single VLAN ID + VLAN TPID.
 *
 * For both modes add a VLAN 0 + no VLAN TPID filter to handle untagged traffic
 * when VLAN pruning is enabled. Also, this handles VLAN 0 priority tagged
 * traffic in SVM, since the VLAN TPID isn't part of filtering.
 *
 * If DVM is enabled then an explicit VLAN 0 + VLAN TPID filter needs to be
 * added to allow VLAN 0 priority tagged traffic in DVM, since the VLAN TPID is
 * part of filtering.
 */
int ice_vsi_add_vlan_zero(struct ice_vsi *vsi)
{
	struct ice_vsi_vlan_ops *vlan_ops = ice_get_compat_vsi_vlan_ops(vsi);
	struct ice_vlan vlan;
	int err;

	vlan = ICE_VLAN(0, 0, 0);
	err = vlan_ops->add_vlan(vsi, &vlan);
	if (err && err != -EEXIST)
		return err;

	/* in SVM both VLAN 0 filters are identical */
	if (!ice_is_dvm_ena(&vsi->back->hw))
		return 0;

	vlan = ICE_VLAN(ETH_P_8021Q, 0, 0);
	err = vlan_ops->add_vlan(vsi, &vlan);
	if (err && err != -EEXIST)
		return err;

	return 0;
}

/**
 * ice_vsi_del_vlan_zero - delete VLAN 0 filter(s) for this VSI
 * @vsi: VSI used to add VLAN filters
 *
 * Delete the VLAN 0 filters in the same manner that they were added in
 * ice_vsi_add_vlan_zero.
 */
int ice_vsi_del_vlan_zero(struct ice_vsi *vsi)
{
	struct ice_vsi_vlan_ops *vlan_ops = ice_get_compat_vsi_vlan_ops(vsi);
	struct ice_vlan vlan;
	int err;

	vlan = ICE_VLAN(0, 0, 0);
	err = vlan_ops->del_vlan(vsi, &vlan);
	if (err && err != -EEXIST)
		return err;

	/* in SVM both VLAN 0 filters are identical */
	if (!ice_is_dvm_ena(&vsi->back->hw))
		return 0;

	vlan = ICE_VLAN(ETH_P_8021Q, 0, 0);
	err = vlan_ops->del_vlan(vsi, &vlan);
	if (err && err != -EEXIST)
		return err;

	return 0;
}

/**
 * ice_vsi_num_zero_vlans - get number of VLAN 0 filters based on VLAN mode
 * @vsi: VSI used to get the VLAN mode
 *
 * If DVM is enabled then 2 VLAN 0 filters are added, else if SVM is enabled
 * then 1 VLAN 0 filter is added. See ice_vsi_add_vlan_zero for more details.
 */
static u16 ice_vsi_num_zero_vlans(struct ice_vsi *vsi)
{
#define ICE_DVM_NUM_ZERO_VLAN_FLTRS	2
#define ICE_SVM_NUM_ZERO_VLAN_FLTRS	1
	/* no VLAN 0 filter is created when a port VLAN is active */
	if (vsi->type == ICE_VSI_VF) {
		if (WARN_ON(!vsi->vf))
			return 0;

		if (ice_vf_is_port_vlan_ena(vsi->vf))
			return 0;
	}

	if (ice_is_dvm_ena(&vsi->back->hw))
		return ICE_DVM_NUM_ZERO_VLAN_FLTRS;
	else
		return ICE_SVM_NUM_ZERO_VLAN_FLTRS;
}

/**
 * ice_vsi_has_non_zero_vlans - check if VSI has any non-zero VLANs
 * @vsi: VSI used to determine if any non-zero VLANs have been added
 */
bool ice_vsi_has_non_zero_vlans(struct ice_vsi *vsi)
{
	return (vsi->num_vlan > ice_vsi_num_zero_vlans(vsi));
}

/**
 * ice_vsi_num_non_zero_vlans - get the number of non-zero VLANs for this VSI
 * @vsi: VSI used to get the number of non-zero VLANs added
 */
u16 ice_vsi_num_non_zero_vlans(struct ice_vsi *vsi)
{
	return (vsi->num_vlan - ice_vsi_num_zero_vlans(vsi));
}

/**
 * ice_is_feature_supported
 * @pf: pointer to the struct ice_pf instance
 * @f: feature enum to be checked
 *
 * returns true if feature is supported, false otherwise
 */
bool ice_is_feature_supported(struct ice_pf *pf, enum ice_feature f)
{
	if (f < 0 || f >= ICE_F_MAX)
		return false;

	return test_bit(f, pf->features);
}

/**
 * ice_set_feature_support
 * @pf: pointer to the struct ice_pf instance
 * @f: feature enum to set
 */
static void ice_set_feature_support(struct ice_pf *pf, enum ice_feature f)
{
	if (f < 0 || f >= ICE_F_MAX)
		return;

	set_bit(f, pf->features);
}

/**
 * ice_clear_feature_support
 * @pf: pointer to the struct ice_pf instance
 * @f: feature enum to clear
 */
void ice_clear_feature_support(struct ice_pf *pf, enum ice_feature f)
{
	if (f < 0 || f >= ICE_F_MAX)
		return;

	clear_bit(f, pf->features);
}

/**
 * ice_init_feature_support
 * @pf: pointer to the struct ice_pf instance
 *
 * called during init to setup supported feature
 */
void ice_init_feature_support(struct ice_pf *pf)
{
	switch (pf->hw.device_id) {
	case ICE_DEV_ID_E810C_BACKPLANE:
	case ICE_DEV_ID_E810C_QSFP:
	case ICE_DEV_ID_E810C_SFP:
		ice_set_feature_support(pf, ICE_F_DSCP);
		if (ice_is_e810t(&pf->hw)) {
			ice_set_feature_support(pf, ICE_F_SMA_CTRL);
			if (ice_gnss_is_gps_present(&pf->hw))
				ice_set_feature_support(pf, ICE_F_GNSS);
		}
		break;
	default:
		break;
	}
}

/**
 * ice_vsi_update_security - update security block in VSI
 * @vsi: pointer to VSI structure
 * @fill: function pointer to fill ctx
 */
int
ice_vsi_update_security(struct ice_vsi *vsi, void (*fill)(struct ice_vsi_ctx *))
{
	struct ice_vsi_ctx ctx = { 0 };

	ctx.info = vsi->info;
	ctx.info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_SECURITY_VALID);
	fill(&ctx);

	if (ice_update_vsi(&vsi->back->hw, vsi->idx, &ctx, NULL))
		return -ENODEV;

	vsi->info = ctx.info;
	return 0;
}

/**
 * ice_vsi_ctx_set_antispoof - set antispoof function in VSI ctx
 * @ctx: pointer to VSI ctx structure
 */
void ice_vsi_ctx_set_antispoof(struct ice_vsi_ctx *ctx)
{
	ctx->info.sec_flags |= ICE_AQ_VSI_SEC_FLAG_ENA_MAC_ANTI_SPOOF |
			       (ICE_AQ_VSI_SEC_TX_VLAN_PRUNE_ENA <<
				ICE_AQ_VSI_SEC_TX_PRUNE_ENA_S);
}

/**
 * ice_vsi_ctx_clear_antispoof - clear antispoof function in VSI ctx
 * @ctx: pointer to VSI ctx structure
 */
void ice_vsi_ctx_clear_antispoof(struct ice_vsi_ctx *ctx)
{
	ctx->info.sec_flags &= ~ICE_AQ_VSI_SEC_FLAG_ENA_MAC_ANTI_SPOOF &
			       ~(ICE_AQ_VSI_SEC_TX_VLAN_PRUNE_ENA <<
				 ICE_AQ_VSI_SEC_TX_PRUNE_ENA_S);
}

/**
 * ice_vsi_ctx_set_allow_override - allow destination override on VSI
 * @ctx: pointer to VSI ctx structure
 */
void ice_vsi_ctx_set_allow_override(struct ice_vsi_ctx *ctx)
{
	ctx->info.sec_flags |= ICE_AQ_VSI_SEC_FLAG_ALLOW_DEST_OVRD;
}

/**
 * ice_vsi_ctx_clear_allow_override - turn off destination override on VSI
 * @ctx: pointer to VSI ctx structure
 */
void ice_vsi_ctx_clear_allow_override(struct ice_vsi_ctx *ctx)
{
	ctx->info.sec_flags &= ~ICE_AQ_VSI_SEC_FLAG_ALLOW_DEST_OVRD;
}
