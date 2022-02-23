// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022, Intel Corporation. */

#include "ice_vf_lib_private.h"
#include "ice.h"
#include "ice_lib.h"
#include "ice_fltr.h"
#include "ice_virtchnl_allowlist.h"

/* Public functions which may be accessed by all driver files */

/**
 * ice_get_vf_by_id - Get pointer to VF by ID
 * @pf: the PF private structure
 * @vf_id: the VF ID to locate
 *
 * Locate and return a pointer to the VF structure associated with a given ID.
 * Returns NULL if the ID does not have a valid VF structure associated with
 * it.
 *
 * This function takes a reference to the VF, which must be released by
 * calling ice_put_vf() once the caller is finished accessing the VF structure
 * returned.
 */
struct ice_vf *ice_get_vf_by_id(struct ice_pf *pf, u16 vf_id)
{
	struct ice_vf *vf;

	rcu_read_lock();
	hash_for_each_possible_rcu(pf->vfs.table, vf, entry, vf_id) {
		if (vf->vf_id == vf_id) {
			struct ice_vf *found;

			if (kref_get_unless_zero(&vf->refcnt))
				found = vf;
			else
				found = NULL;

			rcu_read_unlock();
			return found;
		}
	}
	rcu_read_unlock();

	return NULL;
}

/**
 * ice_release_vf - Release VF associated with a refcount
 * @ref: the kref decremented to zero
 *
 * Callback function for kref_put to release a VF once its reference count has
 * hit zero.
 */
static void ice_release_vf(struct kref *ref)
{
	struct ice_vf *vf = container_of(ref, struct ice_vf, refcnt);

	mutex_destroy(&vf->cfg_lock);

	kfree_rcu(vf, rcu);
}

/**
 * ice_put_vf - Release a reference to a VF
 * @vf: the VF structure to decrease reference count on
 *
 * Decrease the reference count for a VF, and free the entry if it is no
 * longer in use.
 *
 * This must be called after ice_get_vf_by_id() once the reference to the VF
 * structure is no longer used. Otherwise, the VF structure will never be
 * freed.
 */
void ice_put_vf(struct ice_vf *vf)
{
	kref_put(&vf->refcnt, ice_release_vf);
}

/**
 * ice_has_vfs - Return true if the PF has any associated VFs
 * @pf: the PF private structure
 *
 * Return whether or not the PF has any allocated VFs.
 *
 * Note that this function only guarantees that there are no VFs at the point
 * of calling it. It does not guarantee that no more VFs will be added.
 */
bool ice_has_vfs(struct ice_pf *pf)
{
	/* A simple check that the hash table is not empty does not require
	 * the mutex or rcu_read_lock.
	 */
	return !hash_empty(pf->vfs.table);
}

/**
 * ice_get_num_vfs - Get number of allocated VFs
 * @pf: the PF private structure
 *
 * Return the total number of allocated VFs. NOTE: VF IDs are not guaranteed
 * to be contiguous. Do not assume that a VF ID is guaranteed to be less than
 * the output of this function.
 */
u16 ice_get_num_vfs(struct ice_pf *pf)
{
	struct ice_vf *vf;
	unsigned int bkt;
	u16 num_vfs = 0;

	rcu_read_lock();
	ice_for_each_vf_rcu(pf, bkt, vf)
		num_vfs++;
	rcu_read_unlock();

	return num_vfs;
}

/**
 * ice_get_vf_vsi - get VF's VSI based on the stored index
 * @vf: VF used to get VSI
 */
struct ice_vsi *ice_get_vf_vsi(struct ice_vf *vf)
{
	if (vf->lan_vsi_idx == ICE_NO_VSI)
		return NULL;

	return vf->pf->vsi[vf->lan_vsi_idx];
}

/**
 * ice_is_vf_disabled
 * @vf: pointer to the VF info
 *
 * If the PF has been disabled, there is no need resetting VF until PF is
 * active again. Similarly, if the VF has been disabled, this means something
 * else is resetting the VF, so we shouldn't continue.
 *
 * Returns true if the caller should consider the VF as disabled whether
 * because that single VF is explicitly disabled or because the PF is
 * currently disabled.
 */
bool ice_is_vf_disabled(struct ice_vf *vf)
{
	struct ice_pf *pf = vf->pf;

	return (test_bit(ICE_VF_DIS, pf->state) ||
		test_bit(ICE_VF_STATE_DIS, vf->vf_states));
}

/**
 * ice_wait_on_vf_reset - poll to make sure a given VF is ready after reset
 * @vf: The VF being resseting
 *
 * The max poll time is about ~800ms, which is about the maximum time it takes
 * for a VF to be reset and/or a VF driver to be removed.
 */
static void ice_wait_on_vf_reset(struct ice_vf *vf)
{
	int i;

	for (i = 0; i < ICE_MAX_VF_RESET_TRIES; i++) {
		if (test_bit(ICE_VF_STATE_INIT, vf->vf_states))
			break;
		msleep(ICE_MAX_VF_RESET_SLEEP_MS);
	}
}

/**
 * ice_check_vf_ready_for_cfg - check if VF is ready to be configured/queried
 * @vf: VF to check if it's ready to be configured/queried
 *
 * The purpose of this function is to make sure the VF is not in reset, not
 * disabled, and initialized so it can be configured and/or queried by a host
 * administrator.
 */
int ice_check_vf_ready_for_cfg(struct ice_vf *vf)
{
	struct ice_pf *pf;

	ice_wait_on_vf_reset(vf);

	if (ice_is_vf_disabled(vf))
		return -EINVAL;

	pf = vf->pf;
	if (ice_check_vf_init(pf, vf))
		return -EBUSY;

	return 0;
}

/**
 * ice_is_any_vf_in_promisc - check if any VF(s) are in promiscuous mode
 * @pf: PF structure for accessing VF(s)
 *
 * Return false if no VF(s) are in unicast and/or multicast promiscuous mode,
 * else return true
 */
bool ice_is_any_vf_in_promisc(struct ice_pf *pf)
{
	bool is_vf_promisc = false;
	struct ice_vf *vf;
	unsigned int bkt;

	rcu_read_lock();
	ice_for_each_vf_rcu(pf, bkt, vf) {
		/* found a VF that has promiscuous mode configured */
		if (test_bit(ICE_VF_STATE_UC_PROMISC, vf->vf_states) ||
		    test_bit(ICE_VF_STATE_MC_PROMISC, vf->vf_states)) {
			is_vf_promisc = true;
			break;
		}
	}
	rcu_read_unlock();

	return is_vf_promisc;
}

/**
 * ice_vf_set_vsi_promisc - Enable promiscuous mode for a VF VSI
 * @vf: the VF to configure
 * @vsi: the VF's VSI
 * @promisc_m: the promiscuous mode to enable
 */
int
ice_vf_set_vsi_promisc(struct ice_vf *vf, struct ice_vsi *vsi, u8 promisc_m)
{
	struct ice_hw *hw = &vsi->back->hw;
	int status;

	if (ice_vf_is_port_vlan_ena(vf))
		status = ice_fltr_set_vsi_promisc(hw, vsi->idx, promisc_m,
						  ice_vf_get_port_vlan_id(vf));
	else if (ice_vsi_has_non_zero_vlans(vsi))
		status = ice_fltr_set_vlan_vsi_promisc(hw, vsi, promisc_m);
	else
		status = ice_fltr_set_vsi_promisc(hw, vsi->idx, promisc_m, 0);

	if (status && status != -EEXIST) {
		dev_err(ice_pf_to_dev(vsi->back), "enable Tx/Rx filter promiscuous mode on VF-%u failed, error: %d\n",
			vf->vf_id, status);
		return status;
	}

	return 0;
}

/**
 * ice_vf_clear_vsi_promisc - Disable promiscuous mode for a VF VSI
 * @vf: the VF to configure
 * @vsi: the VF's VSI
 * @promisc_m: the promiscuous mode to disable
 */
int
ice_vf_clear_vsi_promisc(struct ice_vf *vf, struct ice_vsi *vsi, u8 promisc_m)
{
	struct ice_hw *hw = &vsi->back->hw;
	int status;

	if (ice_vf_is_port_vlan_ena(vf))
		status = ice_fltr_clear_vsi_promisc(hw, vsi->idx, promisc_m,
						    ice_vf_get_port_vlan_id(vf));
	else if (ice_vsi_has_non_zero_vlans(vsi))
		status = ice_fltr_clear_vlan_vsi_promisc(hw, vsi, promisc_m);
	else
		status = ice_fltr_clear_vsi_promisc(hw, vsi->idx, promisc_m, 0);

	if (status && status != -ENOENT) {
		dev_err(ice_pf_to_dev(vsi->back), "disable Tx/Rx filter promiscuous mode on VF-%u failed, error: %d\n",
			vf->vf_id, status);
		return status;
	}

	return 0;
}

/**
 * ice_set_vf_state_qs_dis - Set VF queues state to disabled
 * @vf: pointer to the VF structure
 */
void ice_set_vf_state_qs_dis(struct ice_vf *vf)
{
	/* Clear Rx/Tx enabled queues flag */
	bitmap_zero(vf->txq_ena, ICE_MAX_RSS_QS_PER_VF);
	bitmap_zero(vf->rxq_ena, ICE_MAX_RSS_QS_PER_VF);
	clear_bit(ICE_VF_STATE_QS_ENA, vf->vf_states);
}

/* Private functions only accessed from other virtualization files */

/**
 * ice_dis_vf_qs - Disable the VF queues
 * @vf: pointer to the VF structure
 */
void ice_dis_vf_qs(struct ice_vf *vf)
{
	struct ice_vsi *vsi = ice_get_vf_vsi(vf);

	ice_vsi_stop_lan_tx_rings(vsi, ICE_NO_RESET, vf->vf_id);
	ice_vsi_stop_all_rx_rings(vsi);
	ice_set_vf_state_qs_dis(vf);
}

/**
 * ice_check_vf_init - helper to check if VF init complete
 * @pf: pointer to the PF structure
 * @vf: the pointer to the VF to check
 */
int ice_check_vf_init(struct ice_pf *pf, struct ice_vf *vf)
{
	if (!test_bit(ICE_VF_STATE_INIT, vf->vf_states)) {
		dev_err(ice_pf_to_dev(pf), "VF ID: %u in reset. Try again.\n",
			vf->vf_id);
		return -EBUSY;
	}
	return 0;
}

/**
 * ice_vf_get_port_info - Get the VF's port info structure
 * @vf: VF used to get the port info structure for
 */
struct ice_port_info *ice_vf_get_port_info(struct ice_vf *vf)
{
	return vf->pf->hw.port_info;
}

static int ice_cfg_mac_antispoof(struct ice_vsi *vsi, bool enable)
{
	struct ice_vsi_ctx *ctx;
	int err;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->info.sec_flags = vsi->info.sec_flags;
	ctx->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_SECURITY_VALID);

	if (enable)
		ctx->info.sec_flags |= ICE_AQ_VSI_SEC_FLAG_ENA_MAC_ANTI_SPOOF;
	else
		ctx->info.sec_flags &= ~ICE_AQ_VSI_SEC_FLAG_ENA_MAC_ANTI_SPOOF;

	err = ice_update_vsi(&vsi->back->hw, vsi->idx, ctx, NULL);
	if (err)
		dev_err(ice_pf_to_dev(vsi->back), "Failed to configure Tx MAC anti-spoof %s for VSI %d, error %d\n",
			enable ? "ON" : "OFF", vsi->vsi_num, err);
	else
		vsi->info.sec_flags = ctx->info.sec_flags;

	kfree(ctx);

	return err;
}

/**
 * ice_vsi_ena_spoofchk - enable Tx spoof checking for this VSI
 * @vsi: VSI to enable Tx spoof checking for
 */
static int ice_vsi_ena_spoofchk(struct ice_vsi *vsi)
{
	struct ice_vsi_vlan_ops *vlan_ops;
	int err;

	vlan_ops = ice_get_compat_vsi_vlan_ops(vsi);

	err = vlan_ops->ena_tx_filtering(vsi);
	if (err)
		return err;

	return ice_cfg_mac_antispoof(vsi, true);
}

/**
 * ice_vsi_dis_spoofchk - disable Tx spoof checking for this VSI
 * @vsi: VSI to disable Tx spoof checking for
 */
static int ice_vsi_dis_spoofchk(struct ice_vsi *vsi)
{
	struct ice_vsi_vlan_ops *vlan_ops;
	int err;

	vlan_ops = ice_get_compat_vsi_vlan_ops(vsi);

	err = vlan_ops->dis_tx_filtering(vsi);
	if (err)
		return err;

	return ice_cfg_mac_antispoof(vsi, false);
}

/**
 * ice_vsi_apply_spoofchk - Apply Tx spoof checking setting to a VSI
 * @vsi: VSI associated to the VF
 * @enable: whether to enable or disable the spoof checking
 */
int ice_vsi_apply_spoofchk(struct ice_vsi *vsi, bool enable)
{
	int err;

	if (enable)
		err = ice_vsi_ena_spoofchk(vsi);
	else
		err = ice_vsi_dis_spoofchk(vsi);

	return err;
}

/**
 * ice_is_vf_trusted
 * @vf: pointer to the VF info
 */
bool ice_is_vf_trusted(struct ice_vf *vf)
{
	return test_bit(ICE_VIRTCHNL_VF_CAP_PRIVILEGE, &vf->vf_caps);
}

/**
 * ice_vf_has_no_qs_ena - check if the VF has any Rx or Tx queues enabled
 * @vf: the VF to check
 *
 * Returns true if the VF has no Rx and no Tx queues enabled and returns false
 * otherwise
 */
bool ice_vf_has_no_qs_ena(struct ice_vf *vf)
{
	return (!bitmap_weight(vf->rxq_ena, ICE_MAX_RSS_QS_PER_VF) &&
		!bitmap_weight(vf->txq_ena, ICE_MAX_RSS_QS_PER_VF));
}

/**
 * ice_is_vf_link_up - check if the VF's link is up
 * @vf: VF to check if link is up
 */
bool ice_is_vf_link_up(struct ice_vf *vf)
{
	struct ice_pf *pf = vf->pf;

	if (ice_check_vf_init(pf, vf))
		return false;

	if (ice_vf_has_no_qs_ena(vf))
		return false;
	else if (vf->link_forced)
		return vf->link_up;
	else
		return pf->hw.port_info->phy.link_info.link_info &
			ICE_AQ_LINK_UP;
}

/**
 * ice_vf_ctrl_invalidate_vsi - invalidate ctrl_vsi_idx to remove VSI access
 * @vf: VF that control VSI is being invalidated on
 */
void ice_vf_ctrl_invalidate_vsi(struct ice_vf *vf)
{
	vf->ctrl_vsi_idx = ICE_NO_VSI;
}

/**
 * ice_vf_ctrl_vsi_release - invalidate the VF's control VSI after freeing it
 * @vf: VF that control VSI is being released on
 */
void ice_vf_ctrl_vsi_release(struct ice_vf *vf)
{
	ice_vsi_release(vf->pf->vsi[vf->ctrl_vsi_idx]);
	ice_vf_ctrl_invalidate_vsi(vf);
}

/**
 * ice_vf_ctrl_vsi_setup - Set up a VF control VSI
 * @vf: VF to setup control VSI for
 *
 * Returns pointer to the successfully allocated VSI struct on success,
 * otherwise returns NULL on failure.
 */
struct ice_vsi *ice_vf_ctrl_vsi_setup(struct ice_vf *vf)
{
	struct ice_port_info *pi = ice_vf_get_port_info(vf);
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi;

	vsi = ice_vsi_setup(pf, pi, ICE_VSI_CTRL, vf, NULL);
	if (!vsi) {
		dev_err(ice_pf_to_dev(pf), "Failed to create VF control VSI\n");
		ice_vf_ctrl_invalidate_vsi(vf);
	}

	return vsi;
}

/**
 * ice_vf_invalidate_vsi - invalidate vsi_idx/vsi_num to remove VSI access
 * @vf: VF to remove access to VSI for
 */
void ice_vf_invalidate_vsi(struct ice_vf *vf)
{
	vf->lan_vsi_idx = ICE_NO_VSI;
	vf->lan_vsi_num = ICE_NO_VSI;
}

/**
 * ice_vf_set_initialized - VF is ready for VIRTCHNL communication
 * @vf: VF to set in initialized state
 *
 * After this function the VF will be ready to receive/handle the
 * VIRTCHNL_OP_GET_VF_RESOURCES message
 */
void ice_vf_set_initialized(struct ice_vf *vf)
{
	ice_set_vf_state_qs_dis(vf);
	clear_bit(ICE_VF_STATE_MC_PROMISC, vf->vf_states);
	clear_bit(ICE_VF_STATE_UC_PROMISC, vf->vf_states);
	clear_bit(ICE_VF_STATE_DIS, vf->vf_states);
	set_bit(ICE_VF_STATE_INIT, vf->vf_states);
	memset(&vf->vlan_v2_caps, 0, sizeof(vf->vlan_v2_caps));
}
