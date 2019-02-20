// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"

/**
 * ice_err_to_virt err - translate errors for VF return code
 * @ice_err: error return code
 */
static enum virtchnl_status_code ice_err_to_virt_err(enum ice_status ice_err)
{
	switch (ice_err) {
	case ICE_SUCCESS:
		return VIRTCHNL_STATUS_SUCCESS;
	case ICE_ERR_BAD_PTR:
	case ICE_ERR_INVAL_SIZE:
	case ICE_ERR_DEVICE_NOT_SUPPORTED:
	case ICE_ERR_PARAM:
	case ICE_ERR_CFG:
		return VIRTCHNL_STATUS_ERR_PARAM;
	case ICE_ERR_NO_MEMORY:
		return VIRTCHNL_STATUS_ERR_NO_MEMORY;
	case ICE_ERR_NOT_READY:
	case ICE_ERR_RESET_FAILED:
	case ICE_ERR_FW_API_VER:
	case ICE_ERR_AQ_ERROR:
	case ICE_ERR_AQ_TIMEOUT:
	case ICE_ERR_AQ_FULL:
	case ICE_ERR_AQ_NO_WORK:
	case ICE_ERR_AQ_EMPTY:
		return VIRTCHNL_STATUS_ERR_ADMIN_QUEUE_ERROR;
	default:
		return VIRTCHNL_STATUS_ERR_NOT_SUPPORTED;
	}
}

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
		    enum virtchnl_status_code v_retval, u8 *msg, u16 msglen)
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
 * ice_vc_notify_vf_link_state - Inform a VF of link status
 * @vf: pointer to the VF structure
 *
 * send a link status message to a single VF
 */
static void ice_vc_notify_vf_link_state(struct ice_vf *vf)
{
	struct virtchnl_pf_event pfe = { 0 };
	struct ice_link_status *ls;
	struct ice_pf *pf = vf->pf;
	struct ice_hw *hw;

	hw = &pf->hw;
	ls = &hw->port_info->phy.link_info;

	pfe.event = VIRTCHNL_EVENT_LINK_CHANGE;
	pfe.severity = PF_EVENT_SEVERITY_INFO;

	if (vf->link_forced)
		ice_set_pfe_link_forced(vf, &pfe, vf->link_up);
	else
		ice_set_pfe_link(vf, &pfe, ls->link_speed, ls->link_info &
				 ICE_AQ_LINK_UP);

	ice_aq_send_msg_to_vf(hw, vf->vf_id, VIRTCHNL_OP_EVENT,
			      VIRTCHNL_STATUS_SUCCESS, (u8 *)&pfe,
			      sizeof(pfe), NULL);
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
	wr32(hw, VPINT_ALLOC_PCI(vf->vf_id), 0);

	first = vf->first_vector_idx +
		hw->func_caps.common_cap.msix_vector_first_id;
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

	/* Disable IOV before freeing resources. This lets any VF drivers
	 * running in the host get themselves cleaned up before we yank
	 * the carpet out from underneath their feet.
	 */
	if (!pci_vfs_assigned(pf->pdev))
		pci_disable_sriov(pf->pdev);
	else
		dev_warn(&pf->pdev->dev, "VFs are assigned - not disabling SR-IOV\n");

	/* Avoid wait time by stopping all VFs at the same time */
	for (i = 0; i < pf->num_alloc_vfs; i++) {
		struct ice_vsi *vsi;

		if (!test_bit(ICE_VF_STATE_ENA, pf->vf[i].vf_states))
			continue;

		vsi = pf->vsi[pf->vf[i].lan_vsi_idx];
		/* stop rings without wait time */
		ice_vsi_stop_lan_tx_rings(vsi, ICE_NO_RESET, i);
		ice_vsi_stop_rx_rings(vsi);

		clear_bit(ICE_VF_STATE_ENA, pf->vf[i].vf_states);
	}

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

	/* Clear the VF's ARQLEN register. This is how the VF detects reset,
	 * since the VFGEN_RSTAT register doesn't stick at 0 after reset.
	 */
	wr32(hw, VF_MBX_ARQLEN(vf_abs_id), 0);

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
 * ice_vsi_set_pvid_fill_ctxt - Set VSI ctxt for add pvid
 * @ctxt: the vsi ctxt to fill
 * @vid: the VLAN id to set as a PVID
 */
static void ice_vsi_set_pvid_fill_ctxt(struct ice_vsi_ctx *ctxt, u16 vid)
{
	ctxt->info.vlan_flags = (ICE_AQ_VSI_VLAN_MODE_UNTAGGED |
				 ICE_AQ_VSI_PVLAN_INSERT_PVID |
				 ICE_AQ_VSI_VLAN_EMOD_STR);
	ctxt->info.pvid = cpu_to_le16(vid);
	ctxt->info.sw_flags2 |= ICE_AQ_VSI_SW_FLAG_RX_VLAN_PRUNE_ENA;
	ctxt->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID |
						ICE_AQ_VSI_PROP_SW_VALID);
}

/**
 * ice_vsi_kill_pvid_fill_ctxt - Set VSI ctx for remove pvid
 * @ctxt: the VSI ctxt to fill
 */
static void ice_vsi_kill_pvid_fill_ctxt(struct ice_vsi_ctx *ctxt)
{
	ctxt->info.vlan_flags = ICE_AQ_VSI_VLAN_EMOD_NOTHING;
	ctxt->info.vlan_flags |= ICE_AQ_VSI_VLAN_MODE_ALL;
	ctxt->info.sw_flags2 &= ~ICE_AQ_VSI_SW_FLAG_RX_VLAN_PRUNE_ENA;
	ctxt->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID |
						ICE_AQ_VSI_PROP_SW_VALID);
}

/**
 * ice_vsi_manage_pvid - Enable or disable port VLAN for VSI
 * @vsi: the VSI to update
 * @vid: the VLAN id to set as a PVID
 * @enable: true for enable pvid false for disable
 */
static int ice_vsi_manage_pvid(struct ice_vsi *vsi, u16 vid, bool enable)
{
	struct device *dev = &vsi->back->pdev->dev;
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx *ctxt;
	enum ice_status status;
	int ret = 0;

	ctxt = devm_kzalloc(dev, sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	ctxt->info = vsi->info;
	if (enable)
		ice_vsi_set_pvid_fill_ctxt(ctxt, vid);
	else
		ice_vsi_kill_pvid_fill_ctxt(ctxt);

	status = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (status) {
		dev_info(dev, "update VSI for port VLAN failed, err %d aq_err %d\n",
			 status, hw->adminq.sq_last_status);
		ret = -EIO;
		goto out;
	}

	vsi->info = ctxt->info;
out:
	devm_kfree(dev, ctxt);
	return ret;
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
	if (vf->port_vlan_id) {
		ice_vsi_manage_pvid(vsi, vf->port_vlan_id, true);
		ice_vsi_add_vlan(vsi, vf->port_vlan_id & ICE_VLAN_M);
	}

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
	 * We dont want to reconfigure interrupts since AVF driver doesn't
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
	struct ice_pf *pf = vf->pf;
	int tx_rx_queue_left;
	int status;

	/* setup VF VSI and necessary resources */
	status = ice_alloc_vsi_res(vf);
	if (status)
		goto ice_alloc_vf_res_exit;

	/* Update number of VF queues, in case VF had requested for queue
	 * changes
	 */
	tx_rx_queue_left = min_t(int, pf->q_left_tx, pf->q_left_rx);
	tx_rx_queue_left += ICE_DFLT_QS_PER_VF;
	if (vf->num_req_qs && vf->num_req_qs <= tx_rx_queue_left &&
	    vf->num_req_qs != vf->num_vf_qs)
		vf->num_vf_qs = vf->num_req_qs;

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
	first = vf->first_vector_idx +
		hw->func_caps.common_cap.msix_vector_first_id;
	last = (first + pf->num_vf_msix) - 1;
	abs_vf_id = vf->vf_id + hw->func_caps.vf_base_id;

	/* VF Vector allocation */
	reg = (((first << VPINT_ALLOC_FIRST_S) & VPINT_ALLOC_FIRST_M) |
	       ((last << VPINT_ALLOC_LAST_S) & VPINT_ALLOC_LAST_M) |
	       VPINT_ALLOC_VALID_M);
	wr32(hw, VPINT_ALLOC(vf->vf_id), reg);

	reg = (((first << VPINT_ALLOC_PCI_FIRST_S) & VPINT_ALLOC_PCI_FIRST_M) |
	       ((last << VPINT_ALLOC_PCI_LAST_S) & VPINT_ALLOC_PCI_LAST_M) |
	       VPINT_ALLOC_PCI_VALID_M);
	wr32(hw, VPINT_ALLOC_PCI(vf->vf_id), reg);
	/* map the interrupts to its functions */
	for (v = first; v <= last; v++) {
		reg = (((abs_vf_id << GLINT_VECT2FUNC_VF_NUM_S) &
			GLINT_VECT2FUNC_VF_NUM_M) |
		       ((hw->pf_id << GLINT_VECT2FUNC_PF_NUM_S) &
			GLINT_VECT2FUNC_PF_NUM_M));
		wr32(hw, GLINT_VECT2FUNC(v), reg);
	}

	/* Map mailbox interrupt. We put an explicit 0 here to remind us that
	 * VF admin queue interrupts will go to VF MSI-X vector 0.
	 */
	wr32(hw, VPINT_MBX_CTL(abs_vf_id), VPINT_MBX_CTL_CAUSE_ENA_M | 0);
	/* set regardless of mapping mode */
	wr32(hw, VPLAN_TXQ_MAPENA(vf->vf_id), VPLAN_TXQ_MAPENA_TX_ENA_M);

	/* VF Tx queues allocation */
	if (vsi->tx_mapping_mode == ICE_VSI_MAP_CONTIG) {
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

	/* set regardless of mapping mode */
	wr32(hw, VPLAN_RXQ_MAPENA(vf->vf_id), VPLAN_RXQ_MAPENA_RX_ENA_M);

	/* VF Rx queues allocation */
	if (vsi->rx_mapping_mode == ICE_VSI_MAP_CONTIG) {
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
 * ice_vf_set_vsi_promisc - set given VF VSI to given promiscuous mode(s)
 * @vf: pointer to the VF info
 * @vsi: the VSI being configured
 * @promisc_m: mask of promiscuous config bits
 * @rm_promisc: promisc flag request from the VF to remove or add filter
 *
 * This function configures VF VSI promiscuous mode, based on the VF requests,
 * for Unicast, Multicast and VLAN
 */
static enum ice_status
ice_vf_set_vsi_promisc(struct ice_vf *vf, struct ice_vsi *vsi, u8 promisc_m,
		       bool rm_promisc)
{
	struct ice_pf *pf = vf->pf;
	enum ice_status status = 0;
	struct ice_hw *hw;

	hw = &pf->hw;
	if (vf->num_vlan) {
		status = ice_set_vlan_vsi_promisc(hw, vsi->idx, promisc_m,
						  rm_promisc);
	} else if (vf->port_vlan_id) {
		if (rm_promisc)
			status = ice_clear_vsi_promisc(hw, vsi->idx, promisc_m,
						       vf->port_vlan_id);
		else
			status = ice_set_vsi_promisc(hw, vsi->idx, promisc_m,
						     vf->port_vlan_id);
	} else {
		if (rm_promisc)
			status = ice_clear_vsi_promisc(hw, vsi->idx, promisc_m,
						       0);
		else
			status = ice_set_vsi_promisc(hw, vsi->idx, promisc_m,
						     0);
	}

	return status;
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
	struct ice_vf *vf;
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

	for (v = 0; v < pf->num_alloc_vfs; v++) {
		struct ice_vsi *vsi;

		vf = &pf->vf[v];
		vsi = pf->vsi[vf->lan_vsi_idx];
		if (test_bit(ICE_VF_STATE_ENA, vf->vf_states)) {
			ice_vsi_stop_lan_tx_rings(vsi, ICE_VF_RESET, vf->vf_id);
			ice_vsi_stop_rx_rings(vsi);
			clear_bit(ICE_VF_STATE_ENA, vf->vf_states);
		}
	}

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
			u32 reg;

			vf = &pf->vf[v];
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
	for (v = 0; v < pf->num_alloc_vfs; v++) {
		vf = &pf->vf[v];

		ice_free_vf_res(vf);

		/* Free VF queues as well, and reallocate later.
		 * If a given VF has different number of queues
		 * configured, the request for update will come
		 * via mailbox communication.
		 */
		vf->num_vf_qs = 0;
	}

	if (ice_check_avail_res(pf)) {
		dev_err(&pf->pdev->dev,
			"Cannot allocate VF resources, try with fewer number of VFs\n");
		return false;
	}

	/* Finish the reset on each VF */
	for (v = 0; v < pf->num_alloc_vfs; v++) {
		vf = &pf->vf[v];

		vf->num_vf_qs = pf->num_vf_qps;
		dev_dbg(&pf->pdev->dev,
			"VF-id %d has %d queues configured\n",
			vf->vf_id, vf->num_vf_qs);
		ice_cleanup_and_realloc_vf(vf);
	}

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
	struct ice_vsi *vsi;
	struct ice_hw *hw;
	bool rsd = false;
	u8 promisc_m;
	u32 reg;
	int i;

	/* If the VFs have been disabled, this means something else is
	 * resetting the VF, so we shouldn't continue.
	 */
	if (test_and_set_bit(__ICE_VF_DIS, pf->state))
		return false;

	ice_trigger_vf_reset(vf, is_vflr);

	vsi = pf->vsi[vf->lan_vsi_idx];

	if (test_bit(ICE_VF_STATE_ENA, vf->vf_states)) {
		ice_vsi_stop_lan_tx_rings(vsi, ICE_VF_RESET, vf->vf_id);
		ice_vsi_stop_rx_rings(vsi);
		clear_bit(ICE_VF_STATE_ENA, vf->vf_states);
	} else {
		/* Call Disable LAN Tx queue AQ call even when queues are not
		 * enabled. This is needed for successful completiom of VFR
		 */
		ice_dis_vsi_txq(vsi->port_info, 0, NULL, NULL, ICE_VF_RESET,
				vf->vf_id, NULL);
	}

	hw = &pf->hw;
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

	/* disable promiscuous modes in case they were enabled
	 * ignore any error if disabling process failed
	 */
	if (test_bit(ICE_VF_STATE_UC_PROMISC, vf->vf_states) ||
	    test_bit(ICE_VF_STATE_MC_PROMISC, vf->vf_states)) {
		if (vf->port_vlan_id ||  vf->num_vlan)
			promisc_m = ICE_UCAST_VLAN_PROMISC_BITS;
		else
			promisc_m = ICE_UCAST_PROMISC_BITS;

		vsi = pf->vsi[vf->lan_vsi_idx];
		if (ice_vf_set_vsi_promisc(vf, vsi, promisc_m, true))
			dev_err(&pf->pdev->dev, "disabling promiscuous mode failed\n");
	}

	/* free VF resources to begin resetting the VSI state */
	ice_free_vf_res(vf);

	ice_cleanup_and_realloc_vf(vf);

	ice_flush(hw);
	clear_bit(__ICE_VF_DIS, pf->state);

	return true;
}

/**
 * ice_vc_notify_link_state - Inform all VFs on a PF of link status
 * @pf: pointer to the PF structure
 */
void ice_vc_notify_link_state(struct ice_pf *pf)
{
	int i;

	for (i = 0; i < pf->num_alloc_vfs; i++)
		ice_vc_notify_vf_link_state(&pf->vf[i]);
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
	ice_vc_vf_broadcast(pf, VIRTCHNL_OP_EVENT, VIRTCHNL_STATUS_SUCCESS,
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
	ice_aq_send_msg_to_vf(&vf->pf->hw, vf->vf_id, VIRTCHNL_OP_EVENT,
			      VIRTCHNL_STATUS_SUCCESS, (u8 *)&pfe, sizeof(pfe),
			      NULL);
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
	if (!ice_reset_all_vfs(pf, true))
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
 * called from the VFLR IRQ handler to
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
 * ice_vc_send_msg_to_vf - Send message to VF
 * @vf: pointer to the VF info
 * @v_opcode: virtual channel opcode
 * @v_retval: virtual channel return value
 * @msg: pointer to the msg buffer
 * @msglen: msg length
 *
 * send msg to VF
 */
static int
ice_vc_send_msg_to_vf(struct ice_vf *vf, u32 v_opcode,
		      enum virtchnl_status_code v_retval, u8 *msg, u16 msglen)
{
	enum ice_status aq_ret;
	struct ice_pf *pf;

	/* validate the request */
	if (!vf || vf->vf_id >= vf->pf->num_alloc_vfs)
		return -EINVAL;

	pf = vf->pf;

	/* single place to detect unsuccessful return values */
	if (v_retval) {
		vf->num_inval_msgs++;
		dev_info(&pf->pdev->dev, "VF %d failed opcode %d, retval: %d\n",
			 vf->vf_id, v_opcode, v_retval);
		if (vf->num_inval_msgs > ICE_DFLT_NUM_INVAL_MSGS_ALLOWED) {
			dev_err(&pf->pdev->dev,
				"Number of invalid messages exceeded for VF %d\n",
				vf->vf_id);
			dev_err(&pf->pdev->dev, "Use PF Control I/F to enable the VF\n");
			set_bit(ICE_VF_STATE_DIS, vf->vf_states);
			return -EIO;
		}
	} else {
		vf->num_valid_msgs++;
		/* reset the invalid counter, if a valid message is received. */
		vf->num_inval_msgs = 0;
	}

	aq_ret = ice_aq_send_msg_to_vf(&pf->hw, vf->vf_id, v_opcode, v_retval,
				       msg, msglen, NULL);
	if (aq_ret) {
		dev_info(&pf->pdev->dev,
			 "Unable to send the message to VF %d aq_err %d\n",
			 vf->vf_id, pf->hw.mailboxq.sq_last_status);
		return -EIO;
	}

	return 0;
}

/**
 * ice_vc_get_ver_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to request the API version used by the PF
 */
static int ice_vc_get_ver_msg(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_version_info info = {
		VIRTCHNL_VERSION_MAJOR, VIRTCHNL_VERSION_MINOR
	};

	vf->vf_ver = *(struct virtchnl_version_info *)msg;
	/* VFs running the 1.0 API expect to get 1.0 back or they will cry. */
	if (VF_IS_V10(&vf->vf_ver))
		info.minor = VIRTCHNL_VERSION_MINOR_NO_VF_CAPS;

	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_VERSION,
				     VIRTCHNL_STATUS_SUCCESS, (u8 *)&info,
				     sizeof(struct virtchnl_version_info));
}

/**
 * ice_vc_get_vf_res_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to request its resources
 */
static int ice_vc_get_vf_res_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vf_resource *vfres = NULL;
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi;
	int len = 0;
	int ret;

	if (!test_bit(ICE_VF_STATE_INIT, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	len = sizeof(struct virtchnl_vf_resource);

	vfres = devm_kzalloc(&pf->pdev->dev, len, GFP_KERNEL);
	if (!vfres) {
		v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
		len = 0;
		goto err;
	}
	if (VF_IS_V11(&vf->vf_ver))
		vf->driver_caps = *(u32 *)msg;
	else
		vf->driver_caps = VIRTCHNL_VF_OFFLOAD_L2 |
				  VIRTCHNL_VF_OFFLOAD_RSS_REG |
				  VIRTCHNL_VF_OFFLOAD_VLAN;

	vfres->vf_cap_flags = VIRTCHNL_VF_OFFLOAD_L2;
	vsi = pf->vsi[vf->lan_vsi_idx];
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	if (!vsi->info.pvid)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_VLAN;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_RSS_PF) {
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_RSS_PF;
	} else {
		if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_RSS_AQ)
			vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_RSS_AQ;
		else
			vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_RSS_REG;
	}

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_RSS_PCTYPE_V2)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_RSS_PCTYPE_V2;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_ENCAP)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_ENCAP;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_ENCAP_CSUM)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_ENCAP_CSUM;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_RX_POLLING)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_RX_POLLING;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_WB_ON_ITR)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_WB_ON_ITR;

	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_REQ_QUEUES)
		vfres->vf_cap_flags |= VIRTCHNL_VF_OFFLOAD_REQ_QUEUES;

	if (vf->driver_caps & VIRTCHNL_VF_CAP_ADV_LINK_SPEED)
		vfres->vf_cap_flags |= VIRTCHNL_VF_CAP_ADV_LINK_SPEED;

	vfres->num_vsis = 1;
	/* Tx and Rx queue are equal for VF */
	vfres->num_queue_pairs = vsi->num_txq;
	vfres->max_vectors = pf->num_vf_msix;
	vfres->rss_key_size = ICE_VSIQF_HKEY_ARRAY_SIZE;
	vfres->rss_lut_size = ICE_VSIQF_HLUT_ARRAY_SIZE;

	vfres->vsi_res[0].vsi_id = vf->lan_vsi_num;
	vfres->vsi_res[0].vsi_type = VIRTCHNL_VSI_SRIOV;
	vfres->vsi_res[0].num_queue_pairs = vsi->num_txq;
	ether_addr_copy(vfres->vsi_res[0].default_mac_addr,
			vf->dflt_lan_addr.addr);

	set_bit(ICE_VF_STATE_ACTIVE, vf->vf_states);

err:
	/* send the response back to the VF */
	ret = ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_GET_VF_RESOURCES, v_ret,
				    (u8 *)vfres, len);

	devm_kfree(&pf->pdev->dev, vfres);
	return ret;
}

/**
 * ice_vc_reset_vf_msg
 * @vf: pointer to the VF info
 *
 * called from the VF to reset itself,
 * unlike other virtchnl messages, PF driver
 * doesn't send the response back to the VF
 */
static void ice_vc_reset_vf_msg(struct ice_vf *vf)
{
	if (test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states))
		ice_reset_vf(vf, false);
}

/**
 * ice_find_vsi_from_id
 * @pf: the pf structure to search for the VSI
 * @id: id of the VSI it is searching for
 *
 * searches for the VSI with the given id
 */
static struct ice_vsi *ice_find_vsi_from_id(struct ice_pf *pf, u16 id)
{
	int i;

	ice_for_each_vsi(pf, i)
		if (pf->vsi[i] && pf->vsi[i]->vsi_num == id)
			return pf->vsi[i];

	return NULL;
}

/**
 * ice_vc_isvalid_vsi_id
 * @vf: pointer to the VF info
 * @vsi_id: VF relative VSI id
 *
 * check for the valid VSI id
 */
static bool ice_vc_isvalid_vsi_id(struct ice_vf *vf, u16 vsi_id)
{
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi;

	vsi = ice_find_vsi_from_id(pf, vsi_id);

	return (vsi && (vsi->vf_id == vf->vf_id));
}

/**
 * ice_vc_isvalid_q_id
 * @vf: pointer to the VF info
 * @vsi_id: VSI id
 * @qid: VSI relative queue id
 *
 * check for the valid queue id
 */
static bool ice_vc_isvalid_q_id(struct ice_vf *vf, u16 vsi_id, u8 qid)
{
	struct ice_vsi *vsi = ice_find_vsi_from_id(vf->pf, vsi_id);
	/* allocated Tx and Rx queues should be always equal for VF VSI */
	return (vsi && (qid < vsi->alloc_txq));
}

/**
 * ice_vc_config_rss_key
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * Configure the VF's RSS key
 */
static int ice_vc_config_rss_key(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_rss_key *vrk =
		(struct virtchnl_rss_key *)msg;
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi = NULL;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vrk->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = pf->vsi[vf->lan_vsi_idx];
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (vrk->key_len != ICE_VSIQF_HKEY_ARRAY_SIZE) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!test_bit(ICE_FLAG_RSS_ENA, vf->pf->flags)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (ice_set_rss(vsi, vrk->key, NULL, 0))
		v_ret = VIRTCHNL_STATUS_ERR_ADMIN_QUEUE_ERROR;
error_param:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_RSS_KEY, v_ret,
				     NULL, 0);
}

/**
 * ice_vc_config_rss_lut
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * Configure the VF's RSS LUT
 */
static int ice_vc_config_rss_lut(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_rss_lut *vrl = (struct virtchnl_rss_lut *)msg;
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi = NULL;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vrl->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = pf->vsi[vf->lan_vsi_idx];
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (vrl->lut_entries != ICE_VSIQF_HLUT_ARRAY_SIZE) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!test_bit(ICE_FLAG_RSS_ENA, vf->pf->flags)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (ice_set_rss(vsi, NULL, vrl->lut, ICE_VSIQF_HLUT_ARRAY_SIZE))
		v_ret = VIRTCHNL_STATUS_ERR_ADMIN_QUEUE_ERROR;
error_param:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_RSS_LUT, v_ret,
				     NULL, 0);
}

/**
 * ice_vc_get_stats_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to get VSI stats
 */
static int ice_vc_get_stats_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_queue_select *vqs =
		(struct virtchnl_queue_select *)msg;
	struct ice_pf *pf = vf->pf;
	struct ice_eth_stats stats;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vqs->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = pf->vsi[vf->lan_vsi_idx];
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	memset(&stats, 0, sizeof(struct ice_eth_stats));
	ice_update_eth_stats(vsi);

	stats = vsi->eth_stats;

error_param:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_GET_STATS, v_ret,
				     (u8 *)&stats, sizeof(stats));
}

/**
 * ice_vc_ena_qs_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to enable all or specific queue(s)
 */
static int ice_vc_ena_qs_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_queue_select *vqs =
	    (struct virtchnl_queue_select *)msg;
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vqs->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!vqs->rx_queues && !vqs->tx_queues) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = pf->vsi[vf->lan_vsi_idx];
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	/* Enable only Rx rings, Tx rings were enabled by the FW when the
	 * Tx queue group list was configured and the context bits were
	 * programmed using ice_vsi_cfg_txqs
	 */
	if (ice_vsi_start_rx_rings(vsi))
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;

	/* Set flag to indicate that queues are enabled */
	if (v_ret == VIRTCHNL_STATUS_SUCCESS)
		set_bit(ICE_VF_STATE_ENA, vf->vf_states);

error_param:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ENABLE_QUEUES, v_ret,
				     NULL, 0);
}

/**
 * ice_vc_dis_qs_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to disable all or specific
 * queue(s)
 */
static int ice_vc_dis_qs_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_queue_select *vqs =
	    (struct virtchnl_queue_select *)msg;
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states) &&
	    !test_bit(ICE_VF_STATE_ENA, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vqs->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!vqs->rx_queues && !vqs->tx_queues) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = pf->vsi[vf->lan_vsi_idx];
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (ice_vsi_stop_lan_tx_rings(vsi, ICE_NO_RESET, vf->vf_id)) {
		dev_err(&vsi->back->pdev->dev,
			"Failed to stop tx rings on VSI %d\n",
			vsi->vsi_num);
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
	}

	if (ice_vsi_stop_rx_rings(vsi)) {
		dev_err(&vsi->back->pdev->dev,
			"Failed to stop rx rings on VSI %d\n",
			vsi->vsi_num);
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
	}

	/* Clear enabled queues flag */
	if (v_ret == VIRTCHNL_STATUS_SUCCESS)
		clear_bit(ICE_VF_STATE_ENA, vf->vf_states);

error_param:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DISABLE_QUEUES, v_ret,
				     NULL, 0);
}

/**
 * ice_vc_cfg_irq_map_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to configure the IRQ to queue map
 */
static int ice_vc_cfg_irq_map_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_irq_map_info *irqmap_info =
	    (struct virtchnl_irq_map_info *)msg;
	u16 vsi_id, vsi_q_id, vector_id;
	struct virtchnl_vector_map *map;
	struct ice_vsi *vsi = NULL;
	struct ice_pf *pf = vf->pf;
	unsigned long qmap;
	int i;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	for (i = 0; i < irqmap_info->num_vectors; i++) {
		map = &irqmap_info->vecmap[i];

		vector_id = map->vector_id;
		vsi_id = map->vsi_id;
		/* validate msg params */
		if (!(vector_id < pf->hw.func_caps.common_cap
		    .num_msix_vectors) || !ice_vc_isvalid_vsi_id(vf, vsi_id)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		vsi = pf->vsi[vf->lan_vsi_idx];
		if (!vsi) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		/* lookout for the invalid queue index */
		qmap = map->rxq_map;
		for_each_set_bit(vsi_q_id, &qmap, ICE_MAX_BASE_QS_PER_VF) {
			struct ice_q_vector *q_vector;

			if (!ice_vc_isvalid_q_id(vf, vsi_id, vsi_q_id)) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				goto error_param;
			}
			q_vector = vsi->q_vectors[i];
			q_vector->num_ring_rx++;
			q_vector->rx.itr_idx = map->rxitr_idx;
			vsi->rx_rings[vsi_q_id]->q_vector = q_vector;
		}

		qmap = map->txq_map;
		for_each_set_bit(vsi_q_id, &qmap, ICE_MAX_BASE_QS_PER_VF) {
			struct ice_q_vector *q_vector;

			if (!ice_vc_isvalid_q_id(vf, vsi_id, vsi_q_id)) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				goto error_param;
			}
			q_vector = vsi->q_vectors[i];
			q_vector->num_ring_tx++;
			q_vector->tx.itr_idx = map->txitr_idx;
			vsi->tx_rings[vsi_q_id]->q_vector = q_vector;
		}
	}

	if (vsi)
		ice_vsi_cfg_msix(vsi);
error_param:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_IRQ_MAP, v_ret,
				     NULL, 0);
}

/**
 * ice_vc_cfg_qs_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * called from the VF to configure the Rx/Tx queues
 */
static int ice_vc_cfg_qs_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vsi_queue_config_info *qci =
	    (struct virtchnl_vsi_queue_config_info *)msg;
	struct virtchnl_queue_pair_info *qpi;
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi;
	int i;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, qci->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = pf->vsi[vf->lan_vsi_idx];
	if (!vsi) {
		goto error_param;
	}

	if (qci->num_queue_pairs > ICE_MAX_BASE_QS_PER_VF) {
		dev_err(&pf->pdev->dev,
			"VF-%d requesting more than supported number of queues: %d\n",
			vf->vf_id, qci->num_queue_pairs);
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	for (i = 0; i < qci->num_queue_pairs; i++) {
		qpi = &qci->qpair[i];
		if (qpi->txq.vsi_id != qci->vsi_id ||
		    qpi->rxq.vsi_id != qci->vsi_id ||
		    qpi->rxq.queue_id != qpi->txq.queue_id ||
		    !ice_vc_isvalid_q_id(vf, qci->vsi_id, qpi->txq.queue_id)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}
		/* copy Tx queue info from VF into VSI */
		vsi->tx_rings[i]->dma = qpi->txq.dma_ring_addr;
		vsi->tx_rings[i]->count = qpi->txq.ring_len;
		/* copy Rx queue info from VF into VSI */
		vsi->rx_rings[i]->dma = qpi->rxq.dma_ring_addr;
		vsi->rx_rings[i]->count = qpi->rxq.ring_len;
		if (qpi->rxq.databuffer_size > ((16 * 1024) - 128)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}
		vsi->rx_buf_len = qpi->rxq.databuffer_size;
		if (qpi->rxq.max_pkt_size >= (16 * 1024) ||
		    qpi->rxq.max_pkt_size < 64) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}
		vsi->max_frame = qpi->rxq.max_pkt_size;
	}

	/* VF can request to configure less than allocated queues
	 * or default allocated queues. So update the VSI with new number
	 */
	vsi->num_txq = qci->num_queue_pairs;
	vsi->num_rxq = qci->num_queue_pairs;
	/* All queues of VF VSI are in TC 0 */
	vsi->tc_cfg.tc_info[0].qcount_tx = qci->num_queue_pairs;
	vsi->tc_cfg.tc_info[0].qcount_rx = qci->num_queue_pairs;

	if (ice_vsi_cfg_lan_txqs(vsi) || ice_vsi_cfg_rxqs(vsi))
		v_ret = VIRTCHNL_STATUS_ERR_ADMIN_QUEUE_ERROR;

error_param:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_VSI_QUEUES, v_ret,
				     NULL, 0);
}

/**
 * ice_is_vf_trusted
 * @vf: pointer to the VF info
 */
static bool ice_is_vf_trusted(struct ice_vf *vf)
{
	return test_bit(ICE_VIRTCHNL_VF_CAP_PRIVILEGE, &vf->vf_caps);
}

/**
 * ice_can_vf_change_mac
 * @vf: pointer to the VF info
 *
 * Return true if the VF is allowed to change its MAC filters, false otherwise
 */
static bool ice_can_vf_change_mac(struct ice_vf *vf)
{
	/* If the VF MAC address has been set administratively (via the
	 * ndo_set_vf_mac command), then deny permission to the VF to
	 * add/delete unicast MAC addresses, unless the VF is trusted
	 */
	if (vf->pf_set_mac && !ice_is_vf_trusted(vf))
		return false;

	return true;
}

/**
 * ice_vc_handle_mac_addr_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 * @set: true if mac filters are being set, false otherwise
 *
 * add guest MAC address filter
 */
static int
ice_vc_handle_mac_addr_msg(struct ice_vf *vf, u8 *msg, bool set)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_ether_addr_list *al =
	    (struct virtchnl_ether_addr_list *)msg;
	struct ice_pf *pf = vf->pf;
	enum virtchnl_ops vc_op;
	LIST_HEAD(mac_list);
	struct ice_vsi *vsi;
	int mac_count = 0;
	int i;

	if (set)
		vc_op = VIRTCHNL_OP_ADD_ETH_ADDR;
	else
		vc_op = VIRTCHNL_OP_DEL_ETH_ADDR;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states) ||
	    !ice_vc_isvalid_vsi_id(vf, al->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto handle_mac_exit;
	}

	if (set && !ice_is_vf_trusted(vf) &&
	    (vf->num_mac + al->num_elements) > ICE_MAX_MACADDR_PER_VF) {
		dev_err(&pf->pdev->dev,
			"Can't add more MAC addresses, because VF-%d is not trusted, switch the VF to trusted mode in order to add more functionalities\n",
			vf->vf_id);
		/* There is no need to let VF know about not being trusted
		 * to add more MAC addr, so we can just return success message.
		 */
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto handle_mac_exit;
	}

	vsi = pf->vsi[vf->lan_vsi_idx];
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto handle_mac_exit;
	}

	for (i = 0; i < al->num_elements; i++) {
		u8 *maddr = al->list[i].addr;

		if (ether_addr_equal(maddr, vf->dflt_lan_addr.addr) ||
		    is_broadcast_ether_addr(maddr)) {
			if (set) {
				/* VF is trying to add filters that the PF
				 * already added. Just continue.
				 */
				dev_info(&pf->pdev->dev,
					 "MAC %pM already set for VF %d\n",
					 maddr, vf->vf_id);
				continue;
			} else {
				/* VF can't remove dflt_lan_addr/bcast mac */
				dev_err(&pf->pdev->dev,
					"VF can't remove default MAC address or MAC %pM programmed by PF for VF %d\n",
					maddr, vf->vf_id);
				continue;
			}
		}

		/* check for the invalid cases and bail if necessary */
		if (is_zero_ether_addr(maddr)) {
			dev_err(&pf->pdev->dev,
				"invalid MAC %pM provided for VF %d\n",
				maddr, vf->vf_id);
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto handle_mac_exit;
		}

		if (is_unicast_ether_addr(maddr) &&
		    !ice_can_vf_change_mac(vf)) {
			dev_err(&pf->pdev->dev,
				"can't change unicast MAC for untrusted VF %d\n",
				vf->vf_id);
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto handle_mac_exit;
		}

		/* get here if maddr is multicast or if VF can change mac */
		if (ice_add_mac_to_list(vsi, &mac_list, al->list[i].addr)) {
			v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
			goto handle_mac_exit;
		}
		mac_count++;
	}

	/* program the updated filter list */
	if (set)
		v_ret = ice_err_to_virt_err(ice_add_mac(&pf->hw, &mac_list));
	else
		v_ret = ice_err_to_virt_err(ice_remove_mac(&pf->hw, &mac_list));

	if (v_ret) {
		dev_err(&pf->pdev->dev,
			"can't update MAC filters for VF %d, error %d\n",
			vf->vf_id, v_ret);
	} else {
		if (set)
			vf->num_mac += mac_count;
		else
			vf->num_mac -= mac_count;
	}

handle_mac_exit:
	ice_free_fltr_list(&pf->pdev->dev, &mac_list);
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, vc_op, v_ret, NULL, 0);
}

/**
 * ice_vc_add_mac_addr_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * add guest MAC address filter
 */
static int ice_vc_add_mac_addr_msg(struct ice_vf *vf, u8 *msg)
{
	return ice_vc_handle_mac_addr_msg(vf, msg, true);
}

/**
 * ice_vc_del_mac_addr_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * remove guest MAC address filter
 */
static int ice_vc_del_mac_addr_msg(struct ice_vf *vf, u8 *msg)
{
	return ice_vc_handle_mac_addr_msg(vf, msg, false);
}

/**
 * ice_vc_request_qs_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * VFs get a default number of queues but can use this message to request a
 * different number. If the request is successful, PF will reset the VF and
 * return 0. If unsuccessful, PF will send message informing VF of number of
 * available queue pairs via virtchnl message response to vf.
 */
static int ice_vc_request_qs_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vf_res_request *vfres =
		(struct virtchnl_vf_res_request *)msg;
	int req_queues = vfres->num_queue_pairs;
	struct ice_pf *pf = vf->pf;
	int max_allowed_vf_queues;
	int tx_rx_queue_left;
	int cur_queues;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	cur_queues = vf->num_vf_qs;
	tx_rx_queue_left = min_t(int, pf->q_left_tx, pf->q_left_rx);
	max_allowed_vf_queues = tx_rx_queue_left + cur_queues;
	if (req_queues <= 0) {
		dev_err(&pf->pdev->dev,
			"VF %d tried to request %d queues. Ignoring.\n",
			vf->vf_id, req_queues);
	} else if (req_queues > ICE_MAX_BASE_QS_PER_VF) {
		dev_err(&pf->pdev->dev,
			"VF %d tried to request more than %d queues.\n",
			vf->vf_id, ICE_MAX_BASE_QS_PER_VF);
		vfres->num_queue_pairs = ICE_MAX_BASE_QS_PER_VF;
	} else if (req_queues - cur_queues > tx_rx_queue_left) {
		dev_warn(&pf->pdev->dev,
			 "VF %d requested %d more queues, but only %d left.\n",
			 vf->vf_id, req_queues - cur_queues, tx_rx_queue_left);
		vfres->num_queue_pairs = min_t(int, max_allowed_vf_queues,
					       ICE_MAX_BASE_QS_PER_VF);
	} else {
		/* request is successful, then reset VF */
		vf->num_req_qs = req_queues;
		ice_vc_dis_vf(vf);
		dev_info(&pf->pdev->dev,
			 "VF %d granted request of %d queues.\n",
			 vf->vf_id, req_queues);
		return 0;
	}

error_param:
	/* send the response to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_REQUEST_QUEUES,
				     v_ret, (u8 *)vfres, sizeof(*vfres));
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
		ret = ice_vsi_manage_pvid(vsi, vlanprio, true);
		if (ret)
			goto error_set_pvid;
	} else {
		ice_vsi_manage_pvid(vsi, 0, false);
		vsi->info.pvid = 0;
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
 * ice_vc_process_vlan_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 * @add_v: Add VLAN if true, otherwise delete VLAN
 *
 * Process virtchnl op to add or remove programmed guest VLAN id
 */
static int ice_vc_process_vlan_msg(struct ice_vf *vf, u8 *msg, bool add_v)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vlan_filter_list *vfl =
	    (struct virtchnl_vlan_filter_list *)msg;
	struct ice_pf *pf = vf->pf;
	bool vlan_promisc = false;
	struct ice_vsi *vsi;
	struct ice_hw *hw;
	int status = 0;
	u8 promisc_m;
	int i;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vfl->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (add_v && !ice_is_vf_trusted(vf) &&
	    vf->num_vlan >= ICE_MAX_VLAN_PER_VF) {
		dev_info(&pf->pdev->dev,
			 "VF-%d is not trusted, switch the VF to trusted mode, in order to add more VLAN addresses\n",
			 vf->vf_id);
		/* There is no need to let VF know about being not trusted,
		 * so we can just return success message here
		 */
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	for (i = 0; i < vfl->num_elements; i++) {
		if (vfl->vlan_id[i] > ICE_MAX_VLANID) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			dev_err(&pf->pdev->dev,
				"invalid VF VLAN id %d\n", vfl->vlan_id[i]);
			goto error_param;
		}
	}

	hw = &pf->hw;
	vsi = pf->vsi[vf->lan_vsi_idx];
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (vsi->info.pvid) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (ice_vsi_manage_vlan_stripping(vsi, add_v)) {
		dev_err(&pf->pdev->dev,
			"%sable VLAN stripping failed for VSI %i\n",
			 add_v ? "en" : "dis", vsi->vsi_num);
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (test_bit(ICE_VF_STATE_UC_PROMISC, vf->vf_states) ||
	    test_bit(ICE_VF_STATE_MC_PROMISC, vf->vf_states))
		vlan_promisc = true;

	if (add_v) {
		for (i = 0; i < vfl->num_elements; i++) {
			u16 vid = vfl->vlan_id[i];

			if (ice_vsi_add_vlan(vsi, vid)) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				goto error_param;
			}

			vf->num_vlan++;
			/* Enable VLAN pruning when VLAN is added */
			if (!vlan_promisc) {
				status = ice_cfg_vlan_pruning(vsi, true, false);
				if (status) {
					v_ret = VIRTCHNL_STATUS_ERR_PARAM;
					dev_err(&pf->pdev->dev,
						"Enable VLAN pruning on VLAN ID: %d failed error-%d\n",
						vid, status);
					goto error_param;
				}
			} else {
				/* Enable Ucast/Mcast VLAN promiscuous mode */
				promisc_m = ICE_PROMISC_VLAN_TX |
					    ICE_PROMISC_VLAN_RX;

				status = ice_set_vsi_promisc(hw, vsi->idx,
							     promisc_m, vid);
				if (status) {
					v_ret = VIRTCHNL_STATUS_ERR_PARAM;
					dev_err(&pf->pdev->dev,
						"Enable Unicast/multicast promiscuous mode on VLAN ID:%d failed error-%d\n",
						vid, status);
				}
			}
		}
	} else {
		for (i = 0; i < vfl->num_elements; i++) {
			u16 vid = vfl->vlan_id[i];

			/* Make sure ice_vsi_kill_vlan is successful before
			 * updating VLAN information
			 */
			if (ice_vsi_kill_vlan(vsi, vid)) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				goto error_param;
			}

			vf->num_vlan--;
			/* Disable VLAN pruning when removing VLAN */
			ice_cfg_vlan_pruning(vsi, false, false);

			/* Disable Unicast/Multicast VLAN promiscuous mode */
			if (vlan_promisc) {
				promisc_m = ICE_PROMISC_VLAN_TX |
					    ICE_PROMISC_VLAN_RX;

				ice_clear_vsi_promisc(hw, vsi->idx,
						      promisc_m, vid);
			}
		}
	}

error_param:
	/* send the response to the VF */
	if (add_v)
		return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ADD_VLAN, v_ret,
					     NULL, 0);
	else
		return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DEL_VLAN, v_ret,
					     NULL, 0);
}

/**
 * ice_vc_add_vlan_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * Add and program guest VLAN id
 */
static int ice_vc_add_vlan_msg(struct ice_vf *vf, u8 *msg)
{
	return ice_vc_process_vlan_msg(vf, msg, true);
}

/**
 * ice_vc_remove_vlan_msg
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * remove programmed guest VLAN id
 */
static int ice_vc_remove_vlan_msg(struct ice_vf *vf, u8 *msg)
{
	return ice_vc_process_vlan_msg(vf, msg, false);
}

/**
 * ice_vc_ena_vlan_stripping
 * @vf: pointer to the VF info
 *
 * Enable VLAN header stripping for a given VF
 */
static int ice_vc_ena_vlan_stripping(struct ice_vf *vf)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = pf->vsi[vf->lan_vsi_idx];
	if (ice_vsi_manage_vlan_stripping(vsi, true))
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;

error_param:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ENABLE_VLAN_STRIPPING,
				     v_ret, NULL, 0);
}

/**
 * ice_vc_dis_vlan_stripping
 * @vf: pointer to the VF info
 *
 * Disable VLAN header stripping for a given VF
 */
static int ice_vc_dis_vlan_stripping(struct ice_vf *vf)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = pf->vsi[vf->lan_vsi_idx];
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (ice_vsi_manage_vlan_stripping(vsi, false))
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;

error_param:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DISABLE_VLAN_STRIPPING,
				     v_ret, NULL, 0);
}

/**
 * ice_vc_process_vf_msg - Process request from VF
 * @pf: pointer to the PF structure
 * @event: pointer to the AQ event
 *
 * called from the common asq/arq handler to
 * process request from VF
 */
void ice_vc_process_vf_msg(struct ice_pf *pf, struct ice_rq_event_info *event)
{
	u32 v_opcode = le32_to_cpu(event->desc.cookie_high);
	s16 vf_id = le16_to_cpu(event->desc.retval);
	u16 msglen = event->msg_len;
	u8 *msg = event->msg_buf;
	struct ice_vf *vf = NULL;
	int err = 0;

	if (vf_id >= pf->num_alloc_vfs) {
		err = -EINVAL;
		goto error_handler;
	}

	vf = &pf->vf[vf_id];

	/* Check if VF is disabled. */
	if (test_bit(ICE_VF_STATE_DIS, vf->vf_states)) {
		err = -EPERM;
		goto error_handler;
	}

	/* Perform basic checks on the msg */
	err = virtchnl_vc_validate_vf_msg(&vf->vf_ver, v_opcode, msg, msglen);
	if (err) {
		if (err == VIRTCHNL_STATUS_ERR_PARAM)
			err = -EPERM;
		else
			err = -EINVAL;
		goto error_handler;
	}

	/* Perform additional checks specific to RSS and Virtchnl */
	if (v_opcode == VIRTCHNL_OP_CONFIG_RSS_KEY) {
		struct virtchnl_rss_key *vrk = (struct virtchnl_rss_key *)msg;

		if (vrk->key_len != ICE_VSIQF_HKEY_ARRAY_SIZE)
			err = -EINVAL;
	} else if (v_opcode == VIRTCHNL_OP_CONFIG_RSS_LUT) {
		struct virtchnl_rss_lut *vrl = (struct virtchnl_rss_lut *)msg;

		if (vrl->lut_entries != ICE_VSIQF_HLUT_ARRAY_SIZE)
			err = -EINVAL;
	}

error_handler:
	if (err) {
		ice_vc_send_msg_to_vf(vf, v_opcode, VIRTCHNL_STATUS_ERR_PARAM,
				      NULL, 0);
		dev_err(&pf->pdev->dev, "Invalid message from VF %d, opcode %d, len %d, error %d\n",
			vf_id, v_opcode, msglen, err);
		return;
	}

	switch (v_opcode) {
	case VIRTCHNL_OP_VERSION:
		err = ice_vc_get_ver_msg(vf, msg);
		break;
	case VIRTCHNL_OP_GET_VF_RESOURCES:
		err = ice_vc_get_vf_res_msg(vf, msg);
		break;
	case VIRTCHNL_OP_RESET_VF:
		ice_vc_reset_vf_msg(vf);
		break;
	case VIRTCHNL_OP_ADD_ETH_ADDR:
		err = ice_vc_add_mac_addr_msg(vf, msg);
		break;
	case VIRTCHNL_OP_DEL_ETH_ADDR:
		err = ice_vc_del_mac_addr_msg(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_VSI_QUEUES:
		err = ice_vc_cfg_qs_msg(vf, msg);
		break;
	case VIRTCHNL_OP_ENABLE_QUEUES:
		err = ice_vc_ena_qs_msg(vf, msg);
		ice_vc_notify_vf_link_state(vf);
		break;
	case VIRTCHNL_OP_DISABLE_QUEUES:
		err = ice_vc_dis_qs_msg(vf, msg);
		break;
	case VIRTCHNL_OP_REQUEST_QUEUES:
		err = ice_vc_request_qs_msg(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_IRQ_MAP:
		err = ice_vc_cfg_irq_map_msg(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_RSS_KEY:
		err = ice_vc_config_rss_key(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_RSS_LUT:
		err = ice_vc_config_rss_lut(vf, msg);
		break;
	case VIRTCHNL_OP_GET_STATS:
		err = ice_vc_get_stats_msg(vf, msg);
		break;
	case VIRTCHNL_OP_ADD_VLAN:
		err = ice_vc_add_vlan_msg(vf, msg);
		break;
	case VIRTCHNL_OP_DEL_VLAN:
		err = ice_vc_remove_vlan_msg(vf, msg);
		break;
	case VIRTCHNL_OP_ENABLE_VLAN_STRIPPING:
		err = ice_vc_ena_vlan_stripping(vf);
		break;
	case VIRTCHNL_OP_DISABLE_VLAN_STRIPPING:
		err = ice_vc_dis_vlan_stripping(vf);
		break;
	case VIRTCHNL_OP_UNKNOWN:
	default:
		dev_err(&pf->pdev->dev, "Unsupported opcode %d from VF %d\n",
			v_opcode, vf_id);
		err = ice_vc_send_msg_to_vf(vf, v_opcode,
					    VIRTCHNL_STATUS_ERR_NOT_SUPPORTED,
					    NULL, 0);
		break;
	}
	if (err) {
		/* Helper function cares less about error return values here
		 * as it is busy with pending work.
		 */
		dev_info(&pf->pdev->dev,
			 "PF failed to honor VF %d, opcode %d, error %d\n",
			 vf_id, v_opcode, err);
	}
}

/**
 * ice_get_vf_cfg
 * @netdev: network interface device structure
 * @vf_id: VF identifier
 * @ivi: VF configuration structure
 *
 * return VF configuration
 */
int
ice_get_vf_cfg(struct net_device *netdev, int vf_id, struct ifla_vf_info *ivi)
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
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	struct ice_vsi_ctx *ctx;
	enum ice_status status;
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

	if (ena == vf->spoofchk) {
		dev_dbg(&pf->pdev->dev, "VF spoofchk already %s\n",
			ena ? "ON" : "OFF");
		return 0;
	}

	ctx = devm_kzalloc(&pf->pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_SECURITY_VALID);

	if (ena) {
		ctx->info.sec_flags |= ICE_AQ_VSI_SEC_FLAG_ENA_MAC_ANTI_SPOOF;
		ctx->info.sw_flags2 |= ICE_AQ_VSI_SW_FLAG_RX_PRUNE_EN_M;
	}

	status = ice_update_vsi(&pf->hw, vsi->idx, ctx, NULL);
	if (status) {
		dev_dbg(&pf->pdev->dev,
			"Error %d, failed to update VSI* parameters\n", status);
		ret = -EIO;
		goto out;
	}

	vf->spoofchk = ena;
	vsi->info.sec_flags = ctx->info.sec_flags;
	vsi->info.sw_flags2 = ctx->info.sw_flags2;
out:
	devm_kfree(&pf->pdev->dev, ctx);
	return ret;
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
		    "MAC on VF %d set to %pM. VF driver will be reinitialized\n",
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
	ice_aq_send_msg_to_vf(hw, vf->vf_id, VIRTCHNL_OP_EVENT,
			      VIRTCHNL_STATUS_SUCCESS, (u8 *)&pfe,
			      sizeof(pfe), NULL);

	return 0;
}
