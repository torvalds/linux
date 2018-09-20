// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"

/**
 * ice_vc_vf_broadcast - Broadcast a message to all VFs on PF
 * @pf: pointer to the PF structure
 * @v_opcode: operation code
 * @v_retval: return value
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 */
static void
ice_vc_vf_broadcast(struct ice_pf *pf, enum virtchnl_ops v_opcode,
		    enum ice_status v_retval, u8 *msg, u16 msglen)
{
	struct ice_hw *hw = &pf->hw;
	struct ice_vf *vf = pf->vf;
	int i;

	for (i = 0; i < pf->num_alloc_vfs; i++, vf++) {
		/* Not all vfs are enabled so skip the ones that are not */
		if (!test_bit(ICE_VF_STATE_INIT, vf->vf_states) &&
		    !test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states))
			continue;

		/* Ignore return value on purpose - a given VF may fail, but
		 * we need to keep going and send to all of them
		 */
		ice_aq_send_msg_to_vf(hw, vf->vf_id, v_opcode, v_retval, msg,
				      msglen, NULL);
	}
}

/**
 * ice_set_pfe_link - Set the link speed/status of the virtchnl_pf_event
 * @vf: pointer to the VF structure
 * @pfe: pointer to the virtchnl_pf_event to set link speed/status for
 * @ice_link_speed: link speed specified by ICE_AQ_LINK_SPEED_*
 * @link_up: whether or not to set the link up/down
 */
static void
ice_set_pfe_link(struct ice_vf *vf, struct virtchnl_pf_event *pfe,
		 int ice_link_speed, bool link_up)
{
	if (vf->driver_caps & VIRTCHNL_VF_CAP_ADV_LINK_SPEED) {
		pfe->event_data.link_event_adv.link_status = link_up;
		/* Speed in Mbps */
		pfe->event_data.link_event_adv.link_speed =
			ice_conv_link_speed_to_virtchnl(true, ice_link_speed);
	} else {
		pfe->event_data.link_event.link_status = link_up;
		/* Legacy method for virtchnl link speeds */
		pfe->event_data.link_event.link_speed =
			(enum virtchnl_link_speed)
			ice_conv_link_speed_to_virtchnl(false, ice_link_speed);
	}
}

/**
 * ice_set_pfe_link_forced - Force the virtchnl_pf_event link speed/status
 * @vf: pointer to the VF structure
 * @pfe: pointer to the virtchnl_pf_event to set link speed/status for
 * @link_up: whether or not to set the link up/down
 */
static void
ice_set_pfe_link_forced(struct ice_vf *vf, struct virtchnl_pf_event *pfe,
			bool link_up)
{
	u16 link_speed;

	if (link_up)
		link_speed = ICE_AQ_LINK_SPEED_40GB;
	else
		link_speed = ICE_AQ_LINK_SPEED_UNKNOWN;

	ice_set_pfe_link(vf, pfe, link_speed, link_up);
}

/**
 * ice_get_vf_vector - get VF interrupt vector register offset
 * @vf_msix: number of MSIx vector per VF on a PF
 * @vf_id: VF identifier
 * @i: index of MSIx vector
 */
static u32 ice_get_vf_vector(int vf_msix, int vf_id, int i)
{
	return ((i == 0) ? VFINT_DYN_CTLN(vf_id) :
		 VFINT_DYN_CTLN(((vf_msix - 1) * (vf_id)) + (i - 1)));
}

/**
 * ice_free_vf_res - Free a VF's resources
 * @vf: pointer to the VF info
 */
static void ice_free_vf_res(struct ice_vf *vf)
{
	struct ice_pf *pf = vf->pf;
	int i, pf_vf_msix;

	/* First, disable VF's configuration API to prevent OS from
	 * accessing the VF's VSI after it's freed or invalidated.
	 */
	clear_bit(ICE_VF_STATE_INIT, vf->vf_states);

	/* free vsi & disconnect it from the parent uplink */
	if (vf->lan_vsi_idx) {
		ice_vsi_release(pf->vsi[vf->lan_vsi_idx]);
		vf->lan_vsi_idx = 0;
		vf->lan_vsi_num = 0;
		vf->num_mac = 0;
	}

	pf_vf_msix = pf->num_vf_msix;
	/* Disable interrupts so that VF starts in a known state */
	for (i = 0; i < pf_vf_msix; i++) {
		u32 reg_idx;

		reg_idx = ice_get_vf_vector(pf_vf_msix, vf->vf_id, i);
		wr32(&pf->hw, reg_idx, VFINT_DYN_CTLN_CLEARPBA_M);
		ice_flush(&pf->hw);
	}
	/* reset some of the state variables keeping track of the resources */
	clear_bit(ICE_VF_STATE_MC_PROMISC, vf->vf_states);
	clear_bit(ICE_VF_STATE_UC_PROMISC, vf->vf_states);
}

/***********************enable_vf routines*****************************/

/**
 * ice_dis_vf_mappings
 * @vf: pointer to the VF structure
 */
static void ice_dis_vf_mappings(struct ice_vf *vf)
{
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi;
	int first, last, v;
	struct ice_hw *hw;

	hw = &pf->hw;
	vsi = pf->vsi[vf->lan_vsi_idx];

	wr32(hw, VPINT_ALLOC(vf->vf_id), 0);

	first = vf->first_vector_idx;
	last = first + pf->num_vf_msix - 1;
	for (v = first; v <= last; v++) {
		u32 reg;

		reg = (((1 << GLINT_VECT2FUNC_IS_PF_S) &
			GLINT_VECT2FUNC_IS_PF_M) |
		       ((hw->pf_id << GLINT_VECT2FUNC_PF_NUM_S) &
			GLINT_VECT2FUNC_PF_NUM_M));
		wr32(hw, GLINT_VECT2FUNC(v), reg);
	}

	if (vsi->tx_mapping_mode == ICE_VSI_MAP_CONTIG)
		wr32(hw, VPLAN_TX_QBASE(vf->vf_id), 0);
	else
		dev_err(&pf->pdev->dev,
			"Scattered mode for VF Tx queues is not yet implemented\n");

	if (vsi->rx_mapping_mode == ICE_VSI_MAP_CONTIG)
		wr32(hw, VPLAN_RX_QBASE(vf->vf_id), 0);
	else
		dev_err(&pf->pdev->dev,
			"Scattered mode for VF Rx queues is not yet implemented\n");
}

/**
 * ice_free_vfs - Free all VFs
 * @pf: pointer to the PF structure
 */
void ice_free_vfs(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;
	int tmp, i;

	if (!pf->vf)
		return;

	while (test_and_set_bit(__ICE_VF_DIS, pf->state))
		usleep_range(1000, 2000);

	/* Avoid wait time by stopping all VFs at the same time */
	for (i = 0; i < pf->num_alloc_vfs; i++) {
		if (!test_bit(ICE_VF_STATE_ENA, pf->vf[i].vf_states))
			continue;

		/* stop rings without wait time */
		ice_vsi_stop_tx_rings(pf->vsi[pf->vf[i].lan_vsi_idx],
				      ICE_NO_RESET, i);
		ice_vsi_stop_rx_rings(pf->vsi[pf->vf[i].lan_vsi_idx]);

		clear_bit(ICE_VF_STATE_ENA, pf->vf[i].vf_states);
	}

	/* Disable IOV before freeing resources. This lets any VF drivers
	 * running in the host get themselves cleaned up before we yank
	 * the carpet out from underneath their feet.
	 */
	if (!pci_vfs_assigned(pf->pdev))
		pci_disable_sriov(pf->pdev);
	else
		dev_warn(&pf->pdev->dev, "VFs are assigned - not disabling SR-IOV\n");

	tmp = pf->num_alloc_vfs;
	pf->num_vf_qps = 0;
	pf->num_alloc_vfs = 0;
	for (i = 0; i < tmp; i++) {
		if (test_bit(ICE_VF_STATE_INIT, pf->vf[i].vf_states)) {
			/* disable VF qp mappings */
			ice_dis_vf_mappings(&pf->vf[i]);

			/* Set this state so that assigned VF vectors can be
			 * reclaimed by PF for reuse in ice_vsi_release(). No
			 * need to clear this bit since pf->vf array is being
			 * freed anyways after this for loop
			 */
			set_bit(ICE_VF_STATE_CFG_INTR, pf->vf[i].vf_states);
			ice_free_vf_res(&pf->vf[i]);
		}
	}

	devm_kfree(&pf->pdev->dev, pf->vf);
	pf->vf = NULL;

	/* This check is for when the driver is unloaded while VFs are
	 * assigned. Setting the number of VFs to 0 through sysfs is caught
	 * before this function ever gets called.
	 */
	if (!pci_vfs_assigned(pf->pdev)) {
		int vf_id;

		/* Acknowledge VFLR for all VFs. Without this, VFs will fail to
		 * work correctly when SR-IOV gets re-enabled.
		 */
		for (vf_id = 0; vf_id < tmp; vf_id++) {
			u32 reg_idx, bit_idx;

			reg_idx = (hw->func_caps.vf_base_id + vf_id) / 32;
			bit_idx = (hw->func_caps.vf_base_id + vf_id) % 32;
			wr32(hw, GLGEN_VFLRSTAT(reg_idx), BIT(bit_idx));
		}
	}
	clear_bit(__ICE_VF_DIS, pf->state);
	clear_bit(ICE_FLAG_SRIOV_ENA, pf->flags);
}

/**
 * ice_trigger_vf_reset - Reset a VF on HW
 * @vf: pointer to the VF structure
 * @is_vflr: true if VFLR was issued, false if not
 *
 * Trigger hardware to start a reset for a particular VF. Expects the caller
 * to wait the proper amount of time to allow hardware to reset the VF before
 * it cleans up and restores VF functionality.
 */
static void ice_trigger_vf_reset(struct ice_vf *vf, bool is_vflr)
{
	struct ice_pf *pf = vf->pf;
	u32 reg, reg_idx, bit_idx;
	struct ice_hw *hw;
	int vf_abs_id, i;

	hw = &pf->hw;
	vf_abs_id = vf->vf_id + hw->func_caps.vf_base_id;

	/* Inform VF that it is no longer active, as a warning */
	clear_bit(ICE_VF_STATE_ACTIVE, vf->vf_states);

	/* Disable VF's configuration API during reset. The flag is re-enabled
	 * in ice_alloc_vf_res(), when it's safe again to access VF's VSI.
	 * It's normally disabled in ice_free_vf_res(), but it's safer
	 * to do it earlier to give some time to finish to any VF config
	 * functions that may still be running at this point.
	 */
	clear_bit(ICE_VF_STATE_INIT, vf->vf_states);

	/* In the case of a VFLR, the HW has already reset the VF and we
	 * just need to clean up, so don't hit the VFRTRIG register.
	 */
	if (!is_vflr) {
		/* reset VF using VPGEN_VFRTRIG reg */
		reg = rd32(hw, VPGEN_VFRTRIG(vf->vf_id));
		reg |= VPGEN_VFRTRIG_VFSWR_M;
		wr32(hw, VPGEN_VFRTRIG(vf->vf_id), reg);
	}
	/* clear the VFLR bit in GLGEN_VFLRSTAT */
	reg_idx = (vf_abs_id) / 32;
	bit_idx = (vf_abs_id) % 32;
	wr32(hw, GLGEN_VFLRSTAT(reg_idx), BIT(bit_idx));
	ice_flush(hw);

	wr32(hw, PF_PCI_CIAA,
	     VF_DEVICE_STATUS | (vf_abs_id << PF_PCI_CIAA_VF_NUM_S));
	for (i = 0; i < 100; i++) {
		reg = rd32(hw, PF_PCI_CIAD);
		if ((reg & VF_TRANS_PENDING_M) != 0)
			dev_err(&pf->pdev->dev,
				"VF %d PCI transactions stuck\n", vf->vf_id);
		udelay(1);
	}
}

/**
 * ice_vsi_set_pvid - Set port VLAN id for the VSI
 * @vsi: the VSI being changed
 * @vid: the VLAN id to set as a PVID
 */
static int ice_vsi_set_pvid(struct ice_vsi *vsi, u16 vid)
{
	struct device *dev = &vsi->back->pdev->dev;
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx ctxt = { 0 };
	enum ice_status status;

	ctxt.info.vlan_flags = ICE_AQ_VSI_VLAN_MODE_TAGGED |
			       ICE_AQ_VSI_PVLAN_INSERT_PVID |
			       ICE_AQ_VSI_VLAN_EMOD_STR;
	ctxt.info.pvid = cpu_to_le16(vid);
	ctxt.info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID);

	status = ice_update_vsi(hw, vsi->idx, &ctxt, NULL);
	if (status) {
		dev_info(dev, "update VSI for VLAN insert failed, err %d aq_err %d\n",
			 status, hw->adminq.sq_last_status);
		return -EIO;
	}

	vsi->info.pvid = ctxt.info.pvid;
	vsi->info.vlan_flags = ctxt.info.vlan_flags;
	return 0;
}

/**
 * ice_vsi_kill_pvid - Remove port VLAN id from the VSI
 * @vsi: the VSI being changed
 */
static int ice_vsi_kill_pvid(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;

	if (ice_vsi_manage_vlan_stripping(vsi, false)) {
		dev_err(&pf->pdev->dev, "Error removing Port VLAN on VSI %i\n",
			vsi->vsi_num);
		return -ENODEV;
	}

	vsi->info.pvid = 0;
	return 0;
}

/**
 * ice_vf_vsi_setup - Set up a VF VSI
 * @pf: board private structure
 * @pi: pointer to the port_info instance
 * @vf_id: defines VF id to which this VSI connects.
 *
 * Returns pointer to the successfully allocated VSI struct on success,
 * otherwise returns NULL on failure.
 */
static struct ice_vsi *
ice_vf_vsi_setup(struct ice_pf *pf, struct ice_port_info *pi, u16 vf_id)
{
	return ice_vsi_setup(pf, pi, ICE_VSI_VF, vf_id);
}

/**
 * ice_alloc_vsi_res - Setup VF VSI and its resources
 * @vf: pointer to the VF structure
 *
 * Returns 0 on success, negative value on failure
 */
static int ice_alloc_vsi_res(struct ice_vf *vf)
{
	struct ice_pf *pf = vf->pf;
	LIST_HEAD(tmp_add_list);
	u8 broadcast[ETH_ALEN];
	struct ice_vsi *vsi;
	int status = 0;

	vsi = ice_vf_vsi_setup(pf, pf->hw.port_info, vf->vf_id);

	if (!vsi) {
		dev_err(&pf->pdev->dev, "Failed to create VF VSI\n");
		return -ENOMEM;
	}

	vf->lan_vsi_idx = vsi->idx;
	vf->lan_vsi_num = vsi->vsi_num;

	/* first vector index is the VFs OICR index */
	vf->first_vector_idx = vsi->hw_base_vector;
	/* Since hw_base_vector holds the vector where data queue interrupts
	 * starts, increment by 1 since VFs allocated vectors include OICR intr
	 * as well.
	 */
	vsi->hw_base_vector += 1;

	/* Check if port VLAN exist before, and restore it accordingly */
	if (vf->port_vlan_id)
		ice_vsi_set_pvid(vsi, vf->port_vlan_id);

	eth_broadcast_addr(broadcast);

	status = ice_add_mac_to_list(vsi, &tmp_add_list, broadcast);
	if (status)
		goto ice_alloc_vsi_res_exit;

	if (is_valid_ether_addr(vf->dflt_lan_addr.addr)) {
		status = ice_add_mac_to_list(vsi, &tmp_add_list,
					     vf->dflt_lan_addr.addr);
		if (status)
			goto ice_alloc_vsi_res_exit;
	}

	status = ice_add_mac(&pf->hw, &tmp_add_list);
	if (status)
		dev_err(&pf->pdev->dev, "could not add mac filters\n");

	/* Clear this bit after VF initialization since we shouldn't reclaim
	 * and reassign interrupts for synchronous or asynchronous VFR events.
	 * We don't want to reconfigure interrupts since AVF driver doesn't
	 * expect vector assignment to be changed unless there is a request for
	 * more vectors.
	 */
	clear_bit(ICE_VF_STATE_CFG_INTR, vf->vf_states);
ice_alloc_vsi_res_exit:
	ice_free_fltr_list(&pf->pdev->dev, &tmp_add_list);
	return status;
}

/**
 * ice_alloc_vf_res - Allocate VF resources
 * @vf: pointer to the VF structure
 */
static int ice_alloc_vf_res(struct ice_vf *vf)
{
	int status;

	/* setup VF VSI and necessary resources */
	status = ice_alloc_vsi_res(vf);
	if (status)
		goto ice_alloc_vf_res_exit;

	if (vf->trusted)
		set_bit(ICE_VIRTCHNL_VF_CAP_PRIVILEGE, &vf->vf_caps);
	else
		clear_bit(ICE_VIRTCHNL_VF_CAP_PRIVILEGE, &vf->vf_caps);

	/* VF is now completely initialized */
	set_bit(ICE_VF_STATE_INIT, vf->vf_states);

	return status;

ice_alloc_vf_res_exit:
	ice_free_vf_res(vf);
	return status;
}

/**
 * ice_ena_vf_mappings
 * @vf: pointer to the VF structure
 *
 * Enable VF vectors and queues allocation by writing the details into
 * respective registers.
 */
static void ice_ena_vf_mappings(struct ice_vf *vf)
{
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi;
	int first, last, v;
	struct ice_hw *hw;
	int abs_vf_id;
	u32 reg;

	hw = &pf->hw;
	vsi = pf->vsi[vf->lan_vsi_idx];
	first = vf->first_vector_idx;
	last = (first + pf->num_vf_msix) - 1;
	abs_vf_id = vf->vf_id + hw->func_caps.vf_base_id;

	/* VF Vector allocation */
	reg = (((first << VPINT_ALLOC_FIRST_S) & VPINT_ALLOC_FIRST_M) |
	       ((last << VPINT_ALLOC_LAST_S) & VPINT_ALLOC_LAST_M) |
	       VPINT_ALLOC_VALID_M);
	wr32(hw, VPINT_ALLOC(vf->vf_id), reg);

	/* map the interrupts to its functions */
	for (v = first; v <= last; v++) {
		reg = (((abs_vf_id << GLINT_VECT2FUNC_VF_NUM_S) &
			GLINT_VECT2FUNC_VF_NUM_M) |
		       ((hw->pf_id << GLINT_VECT2FUNC_PF_NUM_S) &
			GLINT_VECT2FUNC_PF_NUM_M));
		wr32(hw, GLINT_VECT2FUNC(v), reg);
	}

	/* VF Tx queues allocation */
	if (vsi->tx_mapping_mode == ICE_VSI_MAP_CONTIG) {
		wr32(hw, VPLAN_TXQ_MAPENA(vf->vf_id),
		     VPLAN_TXQ_MAPENA_TX_ENA_M);
		/* set the VF PF Tx queue range
		 * VFNUMQ value should be set to (number of queues - 1). A value
		 * of 0 means 1 queue and a value of 255 means 256 queues
		 */
		reg = (((vsi->txq_map[0] << VPLAN_TX_QBASE_VFFIRSTQ_S) &
			VPLAN_TX_QBASE_VFFIRSTQ_M) |
		       (((vsi->alloc_txq - 1) << VPLAN_TX_QBASE_VFNUMQ_S) &
			VPLAN_TX_QBASE_VFNUMQ_M));
		wr32(hw, VPLAN_TX_QBASE(vf->vf_id), reg);
	} else {
		dev_err(&pf->pdev->dev,
			"Scattered mode for VF Tx queues is not yet implemented\n");
	}

	/* VF Rx queues allocation */
	if (vsi->rx_mapping_mode == ICE_VSI_MAP_CONTIG) {
		wr32(hw, VPLAN_RXQ_MAPENA(vf->vf_id),
		     VPLAN_RXQ_MAPENA_RX_ENA_M);
		/* set the VF PF Rx queue range
		 * VFNUMQ value should be set to (number of queues - 1). A value
		 * of 0 means 1 queue and a value of 255 means 256 queues
		 */
		reg = (((vsi->rxq_map[0] << VPLAN_RX_QBASE_VFFIRSTQ_S) &
			VPLAN_RX_QBASE_VFFIRSTQ_M) |
		       (((vsi->alloc_txq - 1) << VPLAN_RX_QBASE_VFNUMQ_S) &
			VPLAN_RX_QBASE_VFNUMQ_M));
		wr32(hw, VPLAN_RX_QBASE(vf->vf_id), reg);
	} else {
		dev_err(&pf->pdev->dev,
			"Scattered mode for VF Rx queues is not yet implemented\n");
	}
}

/**
 * ice_determine_res
 * @pf: pointer to the PF structure
 * @avail_res: available resources in the PF structure
 * @max_res: maximum resources that can be given per VF
 * @min_res: minimum resources that can be given per VF
 *
 * Returns non-zero value if resources (queues/vectors) are available or
 * returns zero if PF cannot accommodate for all num_alloc_vfs.
 */
static int
ice_determine_res(struct ice_pf *pf, u16 avail_res, u16 max_res, u16 min_res)
{
	bool checked_min_res = false;
	int res;

	/* start by checking if PF can assign max number of resources for
	 * all num_alloc_vfs.
	 * if yes, return number per VF
	 * If no, divide by 2 and roundup, check again
	 * repeat the loop till we reach a point where even minimum resources
	 * are not available, in that case return 0
	 */
	res = max_res;
	while ((res >= min_res) && !checked_min_res) {
		int num_all_res;

		num_all_res = pf->num_alloc_vfs * res;
		if (num_all_res <= avail_res)
			return res;

		if (res == min_res)
			checked_min_res = true;

		res = DIV_ROUND_UP(res, 2);
	}
	return 0;
}

/**
 * ice_check_avail_res - check if vectors and queues are available
 * @pf: pointer to the PF structure
 *
 * This function is where we calculate actual number of resources for VF VSIs,
 * we don't reserve ahead of time during probe. Returns success if vectors and
 * queues resources are available, otherwise returns error code
 */
static int ice_check_avail_res(struct ice_pf *pf)
{
	u16 num_msix, num_txq, num_rxq;

	if (!pf->num_alloc_vfs)
		return -EINVAL;

	/* Grab from HW interrupts common pool
	 * Note: By the time the user decides it needs more vectors in a VF
	 * its already too late since one must decide this prior to creating the
	 * VF interface. So the best we can do is take a guess as to what the
	 * user might want.
	 *
	 * We have two policies for vector allocation:
	 * 1. if num_alloc_vfs is from 1 to 16, then we consider this as small
	 * number of NFV VFs used for NFV appliances, since this is a special
	 * case, we try to assign maximum vectors per VF (65) as much as
	 * possible, based on determine_resources algorithm.
	 * 2. if num_alloc_vfs is from 17 to 256, then its large number of
	 * regular VFs which are not used for any special purpose. Hence try to
	 * grab default interrupt vectors (5 as supported by AVF driver).
	 */
	if (pf->num_alloc_vfs <= 16) {
		num_msix = ice_determine_res(pf, pf->num_avail_hw_msix,
					     ICE_MAX_INTR_PER_VF,
					     ICE_MIN_INTR_PER_VF);
	} else if (pf->num_alloc_vfs <= ICE_MAX_VF_COUNT) {
		num_msix = ice_determine_res(pf, pf->num_avail_hw_msix,
					     ICE_DFLT_INTR_PER_VF,
					     ICE_MIN_INTR_PER_VF);
	} else {
		dev_err(&pf->pdev->dev,
			"Number of VFs %d exceeds max VF count %d\n",
			pf->num_alloc_vfs, ICE_MAX_VF_COUNT);
		return -EIO;
	}

	if (!num_msix)
		return -EIO;

	/* Grab from the common pool
	 * start by requesting Default queues (4 as supported by AVF driver),
	 * Note that, the main difference between queues and vectors is, latter
	 * can only be reserved at init time but queues can be requested by VF
	 * at runtime through Virtchnl, that is the reason we start by reserving
	 * few queues.
	 */
	num_txq = ice_determine_res(pf, pf->q_left_tx, ICE_DFLT_QS_PER_VF,
				    ICE_MIN_QS_PER_VF);

	num_rxq = ice_determine_res(pf, pf->q_left_rx, ICE_DFLT_QS_PER_VF,
				    ICE_MIN_QS_PER_VF);

	if (!num_txq || !num_rxq)
		return -EIO;

	/* since AVF driver works with only queue pairs which means, it expects
	 * to have equal number of Rx and Tx queues, so take the minimum of
	 * available Tx or Rx queues
	 */
	pf->num_vf_qps = min_t(int, num_txq, num_rxq);
	pf->num_vf_msix = num_msix;

	return 0;
}

/**
 * ice_cleanup_and_realloc_vf - Clean up VF and reallocate resources after reset
 * @vf: pointer to the VF structure
 *
 * Cleanup a VF after the hardware reset is finished. Expects the caller to
 * have verified whether the reset is finished properly, and ensure the
 * minimum amount of wait time has passed. Reallocate VF resources back to make
 * VF state active
 */
static void ice_cleanup_and_realloc_vf(struct ice_vf *vf)
{
	struct ice_pf *pf = vf->pf;
	struct ice_hw *hw;
	u32 reg;

	hw = &pf->hw;

	/* PF software completes the flow by notifying VF that reset flow is
	 * completed. This is done by enabling hardware by clearing the reset
	 * bit in the VPGEN_VFRTRIG reg and setting VFR_STATE in the VFGEN_RSTAT
	 * register to VFR completed (done at the end of this function)
	 * By doing this we allow HW to access VF memory at any point. If we
	 * did it any sooner, HW could access memory while it was being freed
	 * in ice_free_vf_res(), causing an IOMMU fault.
	 *
	 * On the other hand, this needs to be done ASAP, because the VF driver
	 * is waiting for this to happen and may report a timeout. It's
	 * harmless, but it gets logged into Guest OS kernel log, so best avoid
	 * it.
	 */
	reg = rd32(hw, VPGEN_VFRTRIG(vf->vf_id));
	reg &= ~VPGEN_VFRTRIG_VFSWR_M;
	wr32(hw, VPGEN_VFRTRIG(vf->vf_id), reg);

	/* reallocate VF resources to finish resetting the VSI state */
	if (!ice_alloc_vf_res(vf)) {
		ice_ena_vf_mappings(vf);
		set_bit(ICE_VF_STATE_ACTIVE, vf->vf_states);
		clear_bit(ICE_VF_STATE_DIS, vf->vf_states);
		vf->num_vlan = 0;
	}

	/* Tell the VF driver the reset is done. This needs to be done only
	 * after VF has been fully initialized, because the VF driver may
	 * request resources immediately after setting this flag.
	 */
	wr32(hw, VFGEN_RSTAT(vf->vf_id), VIRTCHNL_VFR_VFACTIVE);
}

/**
 * ice_reset_all_vfs - reset all allocated VFs in one go
 * @pf: pointer to the PF structure
 * @is_vflr: true if VFLR was issued, false if not
 *
 * First, tell the hardware to reset each VF, then do all the waiting in one
 * chunk, and finally finish restoring each VF after the wait. This is useful
 * during PF routines which need to reset all VFs, as otherwise it must perform
 * these resets in a serialized fashion.
 *
 * Returns true if any VFs were reset, and false otherwise.
 */
bool ice_reset_all_vfs(struct ice_pf *pf, bool is_vflr)
{
	struct ice_hw *hw = &pf->hw;
	int v, i;

	/* If we don't have any VFs, then there is nothing to reset */
	if (!pf->num_alloc_vfs)
		return false;

	/* If VFs have been disabled, there is no need to reset */
	if (test_and_set_bit(__ICE_VF_DIS, pf->state))
		return false;

	/* Begin reset on all VFs at once */
	for (v = 0; v < pf->num_alloc_vfs; v++)
		ice_trigger_vf_reset(&pf->vf[v], is_vflr);

	/* Call Disable LAN Tx queue AQ call with VFR bit set and 0
	 * queues to inform Firmware about VF reset.
	 */
	for (v = 0; v < pf->num_alloc_vfs; v++)
		ice_dis_vsi_txq(pf->vsi[0]->port_info, 0, NULL, NULL,
				ICE_VF_RESET, v, NULL);

	/* HW requires some time to make sure it can flush the FIFO for a VF
	 * when it resets it. Poll the VPGEN_VFRSTAT register for each VF in
	 * sequence to make sure that it has completed. We'll keep track of
	 * the VFs using a simple iterator that increments once that VF has
	 * finished resetting.
	 */
	for (i = 0, v = 0; i < 10 && v < pf->num_alloc_vfs; i++) {
		usleep_range(10000, 20000);

		/* Check each VF in sequence */
		while (v < pf->num_alloc_vfs) {
			struct ice_vf *vf = &pf->vf[v];
			u32 reg;

			reg = rd32(hw, VPGEN_VFRSTAT(vf->vf_id));
			if (!(reg & VPGEN_VFRSTAT_VFRD_M))
				break;

			/* If the current VF has finished resetting, move on
			 * to the next VF in sequence.
			 */
			v++;
		}
	}

	/* Display a warning if at least one VF didn't manage to reset in
	 * time, but continue on with the operation.
	 */
	if (v < pf->num_alloc_vfs)
		dev_warn(&pf->pdev->dev, "VF reset check timeout\n");
	usleep_range(10000, 20000);

	/* free VF resources to begin resetting the VSI state */
	for (v = 0; v < pf->num_alloc_vfs; v++)
		ice_free_vf_res(&pf->vf[v]);

	if (ice_check_avail_res(pf)) {
		dev_err(&pf->pdev->dev,
			"Cannot allocate VF resources, try with fewer number of VFs\n");
		return false;
	}

	/* Finish the reset on each VF */
	for (v = 0; v < pf->num_alloc_vfs; v++)
		ice_cleanup_and_realloc_vf(&pf->vf[v]);

	ice_flush(hw);
	clear_bit(__ICE_VF_DIS, pf->state);

	return true;
}

/**
 * ice_reset_vf - Reset a particular VF
 * @vf: pointer to the VF structure
 * @is_vflr: true if VFLR was issued, false if not
 *
 * Returns true if the VF is reset, false otherwise.
 */
static bool ice_reset_vf(struct ice_vf *vf, bool is_vflr)
{
	struct ice_pf *pf = vf->pf;
	struct ice_hw *hw = &pf->hw;
	bool rsd = false;
	u32 reg;
	int i;

	/* If the VFs have been disabled, this means something else is
	 * resetting the VF, so we shouldn't continue.
	 */
	if (test_and_set_bit(__ICE_VF_DIS, pf->state))
		return false;

	ice_trigger_vf_reset(vf, is_vflr);

	if (test_bit(ICE_VF_STATE_ENA, vf->vf_states)) {
		ice_vsi_stop_tx_rings(pf->vsi[vf->lan_vsi_idx], ICE_VF_RESET,
				      vf->vf_id);
		ice_vsi_stop_rx_rings(pf->vsi[vf->lan_vsi_idx]);
		clear_bit(ICE_VF_STATE_ENA, vf->vf_states);
	} else {
		/* Call Disable LAN Tx queue AQ call even when queues are not
		 * enabled. This is needed for successful completiom of VFR
		 */
		ice_dis_vsi_txq(pf->vsi[vf->lan_vsi_idx]->port_info, 0,
				NULL, NULL, ICE_VF_RESET, vf->vf_id, NULL);
	}

	/* poll VPGEN_VFRSTAT reg to make sure
	 * that reset is complete
	 */
	for (i = 0; i < 10; i++) {
		/* VF reset requires driver to first reset the VF and then
		 * poll the status register to make sure that the reset
		 * completed successfully.
		 */
		usleep_range(10000, 20000);
		reg = rd32(hw, VPGEN_VFRSTAT(vf->vf_id));
		if (reg & VPGEN_VFRSTAT_VFRD_M) {
			rsd = true;
			break;
		}
	}

	/* Display a warning if VF didn't manage to reset in time, but need to
	 * continue on with the operation.
	 */
	if (!rsd)
		dev_warn(&pf->pdev->dev, "VF reset check timeout on VF %d\n",
			 vf->vf_id);

	usleep_range(10000, 20000);

	/* free VF resources to begin resetting the VSI state */
	ice_free_vf_res(vf);

	ice_cleanup_and_realloc_vf(vf);

	ice_flush(hw);
	clear_bit(__ICE_VF_DIS, pf->state);

	return true;
}

/**
 * ice_vc_notify_reset - Send pending reset message to all VFs
 * @pf: pointer to the PF structure
 *
 * indicate a pending reset to all VFs on a given PF
 */
void ice_vc_notify_reset(struct ice_pf *pf)
{
	struct virtchnl_pf_event pfe;

	if (!pf->num_alloc_vfs)
		return;

	pfe.event = VIRTCHNL_EVENT_RESET_IMPENDING;
	pfe.severity = PF_EVENT_SEVERITY_CERTAIN_DOOM;
	ice_vc_vf_broadcast(pf, VIRTCHNL_OP_EVENT, ICE_SUCCESS,
			    (u8 *)&pfe, sizeof(struct virtchnl_pf_event));
}

/**
 * ice_vc_notify_vf_reset - Notify VF of a reset event
 * @vf: pointer to the VF structure
 */
static void ice_vc_notify_vf_reset(struct ice_vf *vf)
{
	struct virtchnl_pf_event pfe;

	/* validate the request */
	if (!vf || vf->vf_id >= vf->pf->num_alloc_vfs)
		return;

	/* verify if the VF is in either init or active before proceeding */
	if (!test_bit(ICE_VF_STATE_INIT, vf->vf_states) &&
	    !test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states))
		return;

	pfe.event = VIRTCHNL_EVENT_RESET_IMPENDING;
	pfe.severity = PF_EVENT_SEVERITY_CERTAIN_DOOM;
	ice_aq_send_msg_to_vf(&vf->pf->hw, vf->vf_id, VIRTCHNL_OP_EVENT, 0,
			      (u8 *)&pfe, sizeof(pfe), NULL);
}

/**
 * ice_alloc_vfs - Allocate and set up VFs resources
 * @pf: pointer to the PF structure
 * @num_alloc_vfs: number of VFs to allocate
 */
static int ice_alloc_vfs(struct ice_pf *pf, u16 num_alloc_vfs)
{
	struct ice_hw *hw = &pf->hw;
	struct ice_vf *vfs;
	int i, ret;

	/* Disable global interrupt 0 so we don't try to handle the VFLR. */
	wr32(hw, GLINT_DYN_CTL(pf->hw_oicr_idx),
	     ICE_ITR_NONE << GLINT_DYN_CTL_ITR_INDX_S);

	ice_flush(hw);

	ret = pci_enable_sriov(pf->pdev, num_alloc_vfs);
	if (ret) {
		pf->num_alloc_vfs = 0;
		goto err_unroll_intr;
	}
	/* allocate memory */
	vfs = devm_kcalloc(&pf->pdev->dev, num_alloc_vfs, sizeof(*vfs),
			   GFP_KERNEL);
	if (!vfs) {
		ret = -ENOMEM;
		goto err_unroll_sriov;
	}
	pf->vf = vfs;

	/* apply default profile */
	for (i = 0; i < num_alloc_vfs; i++) {
		vfs[i].pf = pf;
		vfs[i].vf_sw_id = pf->first_sw;
		vfs[i].vf_id = i;

		/* assign default capabilities */
		set_bit(ICE_VIRTCHNL_VF_CAP_L2, &vfs[i].vf_caps);
		vfs[i].spoofchk = true;

		/* Set this state so that PF driver does VF vector assignment */
		set_bit(ICE_VF_STATE_CFG_INTR, vfs[i].vf_states);
	}
	pf->num_alloc_vfs = num_alloc_vfs;

	/* VF resources get allocated during reset */
	if (!ice_reset_all_vfs(pf, false))
		goto err_unroll_sriov;

	goto err_unroll_intr;

err_unroll_sriov:
	pci_disable_sriov(pf->pdev);
err_unroll_intr:
	/* rearm interrupts here */
	ice_irq_dynamic_ena(hw, NULL, NULL);
	return ret;
}

/**
 * ice_pf_state_is_nominal - checks the pf for nominal state
 * @pf: pointer to pf to check
 *
 * Check the PF's state for a collection of bits that would indicate
 * the PF is in a state that would inhibit normal operation for
 * driver functionality.
 *
 * Returns true if PF is in a nominal state.
 * Returns false otherwise
 */
static bool ice_pf_state_is_nominal(struct ice_pf *pf)
{
	DECLARE_BITMAP(check_bits, __ICE_STATE_NBITS) = { 0 };

	if (!pf)
		return false;

	bitmap_set(check_bits, 0, __ICE_STATE_NOMINAL_CHECK_BITS);
	if (bitmap_intersects(pf->state, check_bits, __ICE_STATE_NBITS))
		return false;

	return true;
}

/**
 * ice_pci_sriov_ena - Enable or change number of VFs
 * @pf: pointer to the PF structure
 * @num_vfs: number of VFs to allocate
 */
static int ice_pci_sriov_ena(struct ice_pf *pf, int num_vfs)
{
	int pre_existing_vfs = pci_num_vf(pf->pdev);
	struct device *dev = &pf->pdev->dev;
	int err;

	if (!ice_pf_state_is_nominal(pf)) {
		dev_err(dev, "Cannot enable SR-IOV, device not ready\n");
		return -EBUSY;
	}

	if (!test_bit(ICE_FLAG_SRIOV_CAPABLE, pf->flags)) {
		dev_err(dev, "This device is not capable of SR-IOV\n");
		return -ENODEV;
	}

	if (pre_existing_vfs && pre_existing_vfs != num_vfs)
		ice_free_vfs(pf);
	else if (pre_existing_vfs && pre_existing_vfs == num_vfs)
		return num_vfs;

	if (num_vfs > pf->num_vfs_supported) {
		dev_err(dev, "Can't enable %d VFs, max VFs supported is %d\n",
			num_vfs, pf->num_vfs_supported);
		return -ENOTSUPP;
	}

	dev_info(dev, "Allocating %d VFs\n", num_vfs);
	err = ice_alloc_vfs(pf, num_vfs);
	if (err) {
		dev_err(dev, "Failed to enable SR-IOV: %d\n", err);
		return err;
	}

	set_bit(ICE_FLAG_SRIOV_ENA, pf->flags);
	return num_vfs;
}

/**
 * ice_sriov_configure - Enable or change number of VFs via sysfs
 * @pdev: pointer to a pci_dev structure
 * @num_vfs: number of VFs to allocate
 *
 * This function is called when the user updates the number of VFs in sysfs.
 */
int ice_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct ice_pf *pf = pci_get_drvdata(pdev);

	if (num_vfs)
		return ice_pci_sriov_ena(pf, num_vfs);

	if (!pci_vfs_assigned(pdev)) {
		ice_free_vfs(pf);
	} else {
		dev_err(&pf->pdev->dev,
			"can't free VFs because some are assigned to VMs.\n");
		return -EBUSY;
	}

	return 0;
}

/**
 * ice_process_vflr_event - Free VF resources via IRQ calls
 * @pf: pointer to the PF structure
 *
 * called from the VLFR IRQ handler to
 * free up VF resources and state variables
 */
void ice_process_vflr_event(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;
	int vf_id;
	u32 reg;

	if (!test_bit(__ICE_VFLR_EVENT_PENDING, pf->state) ||
	    !pf->num_alloc_vfs)
		return;

	/* Re-enable the VFLR interrupt cause here, before looking for which
	 * VF got reset. Otherwise, if another VF gets a reset while the
	 * first one is being processed, that interrupt will be lost, and
	 * that VF will be stuck in reset forever.
	 */
	reg = rd32(hw, PFINT_OICR_ENA);
	reg |= PFINT_OICR_VFLR_M;
	wr32(hw, PFINT_OICR_ENA, reg);
	ice_flush(hw);

	clear_bit(__ICE_VFLR_EVENT_PENDING, pf->state);
	for (vf_id = 0; vf_id < pf->num_alloc_vfs; vf_id++) {
		struct ice_vf *vf = &pf->vf[vf_id];
		u32 reg_idx, bit_idx;

		reg_idx = (hw->func_caps.vf_base_id + vf_id) / 32;
		bit_idx = (hw->func_caps.vf_base_id + vf_id) % 32;
		/* read GLGEN_VFLRSTAT register to find out the flr VFs */
		reg = rd32(hw, GLGEN_VFLRSTAT(reg_idx));
		if (reg & BIT(bit_idx))
			/* GLGEN_VFLRSTAT bit will be cleared in ice_reset_vf */
			ice_reset_vf(vf, true);
	}
}

/**
 * ice_vc_dis_vf - Disable a given VF via SW reset
 * @vf: pointer to the VF info
 *
 * Disable the VF through a SW reset
 */
static void ice_vc_dis_vf(struct ice_vf *vf)
{
	ice_vc_notify_vf_reset(vf);
	ice_reset_vf(vf, false);
}

/**
 * ice_set_vf_port_vlan
 * @netdev: network interface device structure
 * @vf_id: VF identifier
 * @vlan_id: VLAN id being set
 * @qos: priority setting
 * @vlan_proto: VLAN protocol
 *
 * program VF Port VLAN id and/or qos
 */
int
ice_set_vf_port_vlan(struct net_device *netdev, int vf_id, u16 vlan_id, u8 qos,
		     __be16 vlan_proto)
{
	u16 vlanprio = vlan_id | (qos << ICE_VLAN_PRIORITY_S);
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_pf *pf = np->vsi->back;
	struct ice_vsi *vsi;
	struct ice_vf *vf;
	int ret = 0;

	/* validate the request */
	if (vf_id >= pf->num_alloc_vfs) {
		dev_err(&pf->pdev->dev, "invalid VF id: %d\n", vf_id);
		return -EINVAL;
	}

	if (vlan_id > ICE_MAX_VLANID || qos > 7) {
		dev_err(&pf->pdev->dev, "Invalid VF Parameters\n");
		return -EINVAL;
	}

	if (vlan_proto != htons(ETH_P_8021Q)) {
		dev_err(&pf->pdev->dev, "VF VLAN protocol is not supported\n");
		return -EPROTONOSUPPORT;
	}

	vf = &pf->vf[vf_id];
	vsi = pf->vsi[vf->lan_vsi_idx];
	if (!test_bit(ICE_VF_STATE_INIT, vf->vf_states)) {
		dev_err(&pf->pdev->dev, "VF %d in reset. Try again.\n", vf_id);
		return -EBUSY;
	}

	if (le16_to_cpu(vsi->info.pvid) == vlanprio) {
		/* duplicate request, so just return success */
		dev_info(&pf->pdev->dev,
			 "Duplicate pvid %d request\n", vlanprio);
		return ret;
	}

	/* If pvid, then remove all filters on the old VLAN */
	if (vsi->info.pvid)
		ice_vsi_kill_vlan(vsi, (le16_to_cpu(vsi->info.pvid) &
				  VLAN_VID_MASK));

	if (vlan_id || qos) {
		ret = ice_vsi_set_pvid(vsi, vlanprio);
		if (ret)
			goto error_set_pvid;
	} else {
		ice_vsi_kill_pvid(vsi);
	}

	if (vlan_id) {
		dev_info(&pf->pdev->dev, "Setting VLAN %d, QOS 0x%x on VF %d\n",
			 vlan_id, qos, vf_id);

		/* add new VLAN filter for each MAC */
		ret = ice_vsi_add_vlan(vsi, vlan_id);
		if (ret)
			goto error_set_pvid;
	}

	/* The Port VLAN needs to be saved across resets the same as the
	 * default LAN MAC address.
	 */
	vf->port_vlan_id = le16_to_cpu(vsi->info.pvid);

error_set_pvid:
	return ret;
}

/**
 * ice_get_vf_cfg
 * @netdev: network interface device structure
 * @vf_id: VF identifier
 * @ivi: VF configuration structure
 *
 * return VF configuration
 */
int ice_get_vf_cfg(struct net_device *netdev, int vf_id,
		   struct ifla_vf_info *ivi)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	struct ice_vf *vf;

	/* validate the request */
	if (vf_id >= pf->num_alloc_vfs) {
		netdev_err(netdev, "invalid VF id: %d\n", vf_id);
		return -EINVAL;
	}

	vf = &pf->vf[vf_id];
	vsi = pf->vsi[vf->lan_vsi_idx];

	if (!test_bit(ICE_VF_STATE_INIT, vf->vf_states)) {
		netdev_err(netdev, "VF %d in reset. Try again.\n", vf_id);
		return -EBUSY;
	}

	ivi->vf = vf_id;
	ether_addr_copy(ivi->mac, vf->dflt_lan_addr.addr);

	/* VF configuration for VLAN and applicable QoS */
	ivi->vlan = le16_to_cpu(vsi->info.pvid) & ICE_VLAN_M;
	ivi->qos = (le16_to_cpu(vsi->info.pvid) & ICE_PRIORITY_M) >>
		    ICE_VLAN_PRIORITY_S;

	ivi->trusted = vf->trusted;
	ivi->spoofchk = vf->spoofchk;
	if (!vf->link_forced)
		ivi->linkstate = IFLA_VF_LINK_STATE_AUTO;
	else if (vf->link_up)
		ivi->linkstate = IFLA_VF_LINK_STATE_ENABLE;
	else
		ivi->linkstate = IFLA_VF_LINK_STATE_DISABLE;
	ivi->max_tx_rate = vf->tx_rate;
	ivi->min_tx_rate = 0;
	return 0;
}

/**
 * ice_set_vf_spoofchk
 * @netdev: network interface device structure
 * @vf_id: VF identifier
 * @ena: flag to enable or disable feature
 *
 * Enable or disable VF spoof checking
 */
int ice_set_vf_spoofchk(struct net_device *netdev, int vf_id, bool ena)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi_ctx ctx = { 0 };
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	struct ice_vf *vf;
	int status;

	/* validate the request */
	if (vf_id >= pf->num_alloc_vfs) {
		netdev_err(netdev, "invalid VF id: %d\n", vf_id);
		return -EINVAL;
	}

	vf = &pf->vf[vf_id];
	if (!test_bit(ICE_VF_STATE_INIT, vf->vf_states)) {
		netdev_err(netdev, "VF %d in reset. Try again.\n", vf_id);
		return -EBUSY;
	}

	if (ena == vf->spoofchk) {
		dev_dbg(&pf->pdev->dev, "VF spoofchk already %s\n",
			ena ? "ON" : "OFF");
		return 0;
	}

	ctx.info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_SECURITY_VALID);

	if (ena) {
		ctx.info.sec_flags |= ICE_AQ_VSI_SEC_FLAG_ENA_MAC_ANTI_SPOOF;
		ctx.info.sw_flags2 |= ICE_AQ_VSI_SW_FLAG_RX_PRUNE_EN_M;
	}

	status = ice_update_vsi(&pf->hw, vsi->idx, &ctx, NULL);
	if (status) {
		dev_dbg(&pf->pdev->dev,
			"Error %d, failed to update VSI* parameters\n", status);
		return -EIO;
	}

	vf->spoofchk = ena;
	vsi->info.sec_flags = ctx.info.sec_flags;
	vsi->info.sw_flags2 = ctx.info.sw_flags2;

	return status;
}

/**
 * ice_set_vf_mac
 * @netdev: network interface device structure
 * @vf_id: VF identifier
 * @mac: mac address
 *
 * program VF mac address
 */
int ice_set_vf_mac(struct net_device *netdev, int vf_id, u8 *mac)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	struct ice_vf *vf;
	int ret = 0;

	/* validate the request */
	if (vf_id >= pf->num_alloc_vfs) {
		netdev_err(netdev, "invalid VF id: %d\n", vf_id);
		return -EINVAL;
	}

	vf = &pf->vf[vf_id];
	if (!test_bit(ICE_VF_STATE_INIT, vf->vf_states)) {
		netdev_err(netdev, "VF %d in reset. Try again.\n", vf_id);
		return -EBUSY;
	}

	if (is_zero_ether_addr(mac) || is_multicast_ether_addr(mac)) {
		netdev_err(netdev, "%pM not a valid unicast address\n", mac);
		return -EINVAL;
	}

	/* copy mac into dflt_lan_addr and trigger a VF reset. The reset
	 * flow will use the updated dflt_lan_addr and add a MAC filter
	 * using ice_add_mac. Also set pf_set_mac to indicate that the PF has
	 * set the MAC address for this VF.
	 */
	ether_addr_copy(vf->dflt_lan_addr.addr, mac);
	vf->pf_set_mac = true;
	netdev_info(netdev,
		    "mac on VF %d set to %pM\n. VF driver will be reinitialized\n",
		    vf_id, mac);

	ice_vc_dis_vf(vf);
	return ret;
}

/**
 * ice_set_vf_trust
 * @netdev: network interface device structure
 * @vf_id: VF identifier
 * @trusted: Boolean value to enable/disable trusted VF
 *
 * Enable or disable a given VF as trusted
 */
int ice_set_vf_trust(struct net_device *netdev, int vf_id, bool trusted)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	struct ice_vf *vf;

	/* validate the request */
	if (vf_id >= pf->num_alloc_vfs) {
		dev_err(&pf->pdev->dev, "invalid VF id: %d\n", vf_id);
		return -EINVAL;
	}

	vf = &pf->vf[vf_id];
	if (!test_bit(ICE_VF_STATE_INIT, vf->vf_states)) {
		dev_err(&pf->pdev->dev, "VF %d in reset. Try again.\n", vf_id);
		return -EBUSY;
	}

	/* Check if already trusted */
	if (trusted == vf->trusted)
		return 0;

	vf->trusted = trusted;
	ice_vc_dis_vf(vf);
	dev_info(&pf->pdev->dev, "VF %u is now %strusted\n",
		 vf_id, trusted ? "" : "un");

	return 0;
}

/**
 * ice_set_vf_link_state
 * @netdev: network interface device structure
 * @vf_id: VF identifier
 * @link_state: required link state
 *
 * Set VF's link state, irrespective of physical link state status
 */
int ice_set_vf_link_state(struct net_device *netdev, int vf_id, int link_state)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_pf *pf = np->vsi->back;
	struct virtchnl_pf_event pfe = { 0 };
	struct ice_link_status *ls;
	struct ice_vf *vf;
	struct ice_hw *hw;

	if (vf_id >= pf->num_alloc_vfs) {
		dev_err(&pf->pdev->dev, "Invalid VF Identifier %d\n", vf_id);
		return -EINVAL;
	}

	vf = &pf->vf[vf_id];
	hw = &pf->hw;
	ls = &pf->hw.port_info->phy.link_info;

	if (!test_bit(ICE_VF_STATE_INIT, vf->vf_states)) {
		dev_err(&pf->pdev->dev, "vf %d in reset. Try again.\n", vf_id);
		return -EBUSY;
	}

	pfe.event = VIRTCHNL_EVENT_LINK_CHANGE;
	pfe.severity = PF_EVENT_SEVERITY_INFO;

	switch (link_state) {
	case IFLA_VF_LINK_STATE_AUTO:
		vf->link_forced = false;
		vf->link_up = ls->link_info & ICE_AQ_LINK_UP;
		break;
	case IFLA_VF_LINK_STATE_ENABLE:
		vf->link_forced = true;
		vf->link_up = true;
		break;
	case IFLA_VF_LINK_STATE_DISABLE:
		vf->link_forced = true;
		vf->link_up = false;
		break;
	default:
		return -EINVAL;
	}

	if (vf->link_forced)
		ice_set_pfe_link_forced(vf, &pfe, vf->link_up);
	else
		ice_set_pfe_link(vf, &pfe, ls->link_speed, vf->link_up);

	/* Notify the VF of its new link state */
	ice_aq_send_msg_to_vf(hw, vf->vf_id, VIRTCHNL_OP_EVENT, 0, (u8 *)&pfe,
			      sizeof(pfe), NULL);

	return 0;
}
