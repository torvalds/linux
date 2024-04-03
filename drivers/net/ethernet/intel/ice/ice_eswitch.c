// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2021, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"
#include "ice_eswitch.h"
#include "ice_eswitch_br.h"
#include "ice_fltr.h"
#include "ice_repr.h"
#include "ice_devlink.h"
#include "ice_tc_lib.h"

/**
 * ice_eswitch_del_sp_rules - delete adv rules added on PRs
 * @pf: pointer to the PF struct
 *
 * Delete all advanced rules that were used to forward packets with the
 * device's VSI index to the corresponding eswitch ctrl VSI queue.
 */
static void ice_eswitch_del_sp_rules(struct ice_pf *pf)
{
	struct ice_repr *repr;
	unsigned long id;

	xa_for_each(&pf->eswitch.reprs, id, repr) {
		if (repr->sp_rule.rid)
			ice_rem_adv_rule_by_id(&pf->hw, &repr->sp_rule);
	}
}

/**
 * ice_eswitch_add_sp_rule - add adv rule with device's VSI index
 * @pf: pointer to PF struct
 * @repr: pointer to the repr struct
 *
 * This function adds advanced rule that forwards packets with
 * device's VSI index to the corresponding eswitch ctrl VSI queue.
 */
static int ice_eswitch_add_sp_rule(struct ice_pf *pf, struct ice_repr *repr)
{
	struct ice_vsi *ctrl_vsi = pf->eswitch.control_vsi;
	struct ice_adv_rule_info rule_info = { 0 };
	struct ice_adv_lkup_elem *list;
	struct ice_hw *hw = &pf->hw;
	const u16 lkups_cnt = 1;
	int err;

	list = kcalloc(lkups_cnt, sizeof(*list), GFP_ATOMIC);
	if (!list)
		return -ENOMEM;

	ice_rule_add_src_vsi_metadata(list);

	rule_info.sw_act.flag = ICE_FLTR_TX;
	rule_info.sw_act.vsi_handle = ctrl_vsi->idx;
	rule_info.sw_act.fltr_act = ICE_FWD_TO_Q;
	rule_info.sw_act.fwd_id.q_id = hw->func_caps.common_cap.rxq_first_id +
				       ctrl_vsi->rxq_map[repr->q_id];
	rule_info.flags_info.act |= ICE_SINGLE_ACT_LB_ENABLE;
	rule_info.flags_info.act_valid = true;
	rule_info.tun_type = ICE_SW_TUN_AND_NON_TUN;
	rule_info.src_vsi = repr->src_vsi->idx;

	err = ice_add_adv_rule(hw, list, lkups_cnt, &rule_info,
			       &repr->sp_rule);
	if (err)
		dev_err(ice_pf_to_dev(pf), "Unable to add slow-path rule for eswitch for PR %d",
			repr->id);

	kfree(list);
	return err;
}

static int
ice_eswitch_add_sp_rules(struct ice_pf *pf)
{
	struct ice_repr *repr;
	unsigned long id;
	int err;

	xa_for_each(&pf->eswitch.reprs, id, repr) {
		err = ice_eswitch_add_sp_rule(pf, repr);
		if (err) {
			ice_eswitch_del_sp_rules(pf);
			return err;
		}
	}

	return 0;
}

/**
 * ice_eswitch_setup_env - configure eswitch HW filters
 * @pf: pointer to PF struct
 *
 * This function adds HW filters configuration specific for switchdev
 * mode.
 */
static int ice_eswitch_setup_env(struct ice_pf *pf)
{
	struct ice_vsi *uplink_vsi = pf->eswitch.uplink_vsi;
	struct ice_vsi *ctrl_vsi = pf->eswitch.control_vsi;
	struct net_device *netdev = uplink_vsi->netdev;
	struct ice_vsi_vlan_ops *vlan_ops;
	bool rule_added = false;

	ice_remove_vsi_fltr(&pf->hw, uplink_vsi->idx);

	netif_addr_lock_bh(netdev);
	__dev_uc_unsync(netdev, NULL);
	__dev_mc_unsync(netdev, NULL);
	netif_addr_unlock_bh(netdev);

	if (ice_vsi_add_vlan_zero(uplink_vsi))
		goto err_def_rx;

	if (!ice_is_dflt_vsi_in_use(uplink_vsi->port_info)) {
		if (ice_set_dflt_vsi(uplink_vsi))
			goto err_def_rx;
		rule_added = true;
	}

	vlan_ops = ice_get_compat_vsi_vlan_ops(uplink_vsi);
	if (vlan_ops->dis_rx_filtering(uplink_vsi))
		goto err_dis_rx;

	if (ice_vsi_update_security(uplink_vsi, ice_vsi_ctx_set_allow_override))
		goto err_override_uplink;

	if (ice_vsi_update_security(ctrl_vsi, ice_vsi_ctx_set_allow_override))
		goto err_override_control;

	if (ice_vsi_update_local_lb(uplink_vsi, true))
		goto err_override_local_lb;

	return 0;

err_override_local_lb:
	ice_vsi_update_security(ctrl_vsi, ice_vsi_ctx_clear_allow_override);
err_override_control:
	ice_vsi_update_security(uplink_vsi, ice_vsi_ctx_clear_allow_override);
err_override_uplink:
	vlan_ops->ena_rx_filtering(uplink_vsi);
err_dis_rx:
	if (rule_added)
		ice_clear_dflt_vsi(uplink_vsi);
err_def_rx:
	ice_fltr_add_mac_and_broadcast(uplink_vsi,
				       uplink_vsi->port_info->mac.perm_addr,
				       ICE_FWD_TO_VSI);
	return -ENODEV;
}

/**
 * ice_eswitch_remap_rings_to_vectors - reconfigure rings of eswitch ctrl VSI
 * @eswitch: pointer to eswitch struct
 *
 * In eswitch number of allocated Tx/Rx rings is equal.
 *
 * This function fills q_vectors structures associated with representor and
 * move each ring pairs to port representor netdevs. Each port representor
 * will have dedicated 1 Tx/Rx ring pair, so number of rings pair is equal to
 * number of VFs.
 */
static void ice_eswitch_remap_rings_to_vectors(struct ice_eswitch *eswitch)
{
	struct ice_vsi *vsi = eswitch->control_vsi;
	unsigned long repr_id = 0;
	int q_id;

	ice_for_each_txq(vsi, q_id) {
		struct ice_q_vector *q_vector;
		struct ice_tx_ring *tx_ring;
		struct ice_rx_ring *rx_ring;
		struct ice_repr *repr;

		repr = xa_find(&eswitch->reprs, &repr_id, U32_MAX,
			       XA_PRESENT);
		if (!repr)
			break;

		repr_id += 1;
		repr->q_id = q_id;
		q_vector = repr->q_vector;
		tx_ring = vsi->tx_rings[q_id];
		rx_ring = vsi->rx_rings[q_id];

		q_vector->vsi = vsi;
		q_vector->reg_idx = vsi->q_vectors[0]->reg_idx;

		q_vector->num_ring_tx = 1;
		q_vector->tx.tx_ring = tx_ring;
		tx_ring->q_vector = q_vector;
		tx_ring->next = NULL;
		tx_ring->netdev = repr->netdev;
		/* In switchdev mode, from OS stack perspective, there is only
		 * one queue for given netdev, so it needs to be indexed as 0.
		 */
		tx_ring->q_index = 0;

		q_vector->num_ring_rx = 1;
		q_vector->rx.rx_ring = rx_ring;
		rx_ring->q_vector = q_vector;
		rx_ring->next = NULL;
		rx_ring->netdev = repr->netdev;
	}
}

/**
 * ice_eswitch_release_repr - clear PR VSI configuration
 * @pf: poiner to PF struct
 * @repr: pointer to PR
 */
static void
ice_eswitch_release_repr(struct ice_pf *pf, struct ice_repr *repr)
{
	struct ice_vsi *vsi = repr->src_vsi;

	/* Skip representors that aren't configured */
	if (!repr->dst)
		return;

	ice_vsi_update_security(vsi, ice_vsi_ctx_set_antispoof);
	metadata_dst_free(repr->dst);
	repr->dst = NULL;
	ice_fltr_add_mac_and_broadcast(vsi, repr->parent_mac,
				       ICE_FWD_TO_VSI);

	netif_napi_del(&repr->q_vector->napi);
}

/**
 * ice_eswitch_setup_repr - configure PR to run in switchdev mode
 * @pf: pointer to PF struct
 * @repr: pointer to PR struct
 */
static int ice_eswitch_setup_repr(struct ice_pf *pf, struct ice_repr *repr)
{
	struct ice_vsi *ctrl_vsi = pf->eswitch.control_vsi;
	struct ice_vsi *vsi = repr->src_vsi;
	struct metadata_dst *dst;

	ice_remove_vsi_fltr(&pf->hw, vsi->idx);
	repr->dst = metadata_dst_alloc(0, METADATA_HW_PORT_MUX,
				       GFP_KERNEL);
	if (!repr->dst)
		goto err_add_mac_fltr;

	if (ice_vsi_update_security(vsi, ice_vsi_ctx_clear_antispoof))
		goto err_dst_free;

	if (ice_vsi_add_vlan_zero(vsi))
		goto err_update_security;

	netif_napi_add(repr->netdev, &repr->q_vector->napi,
		       ice_napi_poll);

	netif_keep_dst(repr->netdev);

	dst = repr->dst;
	dst->u.port_info.port_id = vsi->vsi_num;
	dst->u.port_info.lower_dev = repr->netdev;
	ice_repr_set_traffic_vsi(repr, ctrl_vsi);

	return 0;

err_update_security:
	ice_vsi_update_security(vsi, ice_vsi_ctx_set_antispoof);
err_dst_free:
	metadata_dst_free(repr->dst);
	repr->dst = NULL;
err_add_mac_fltr:
	ice_fltr_add_mac_and_broadcast(vsi, repr->parent_mac, ICE_FWD_TO_VSI);

	return -ENODEV;
}

/**
 * ice_eswitch_update_repr - reconfigure port representor
 * @repr_id: representor ID
 * @vsi: VSI for which port representor is configured
 */
void ice_eswitch_update_repr(unsigned long repr_id, struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct ice_repr *repr;
	int ret;

	if (!ice_is_switchdev_running(pf))
		return;

	repr = xa_load(&pf->eswitch.reprs, repr_id);
	if (!repr)
		return;

	repr->src_vsi = vsi;
	repr->dst->u.port_info.port_id = vsi->vsi_num;

	if (repr->br_port)
		repr->br_port->vsi = vsi;

	ret = ice_vsi_update_security(vsi, ice_vsi_ctx_clear_antispoof);
	if (ret) {
		ice_fltr_add_mac_and_broadcast(vsi, repr->parent_mac,
					       ICE_FWD_TO_VSI);
		dev_err(ice_pf_to_dev(pf), "Failed to update VSI of port representor %d",
			repr->id);
	}
}

/**
 * ice_eswitch_port_start_xmit - callback for packets transmit
 * @skb: send buffer
 * @netdev: network interface device structure
 *
 * Returns NETDEV_TX_OK if sent, else an error code
 */
netdev_tx_t
ice_eswitch_port_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct ice_netdev_priv *np;
	struct ice_repr *repr;
	struct ice_vsi *vsi;

	np = netdev_priv(netdev);
	vsi = np->vsi;

	if (!vsi || !ice_is_switchdev_running(vsi->back))
		return NETDEV_TX_BUSY;

	if (ice_is_reset_in_progress(vsi->back->state) ||
	    test_bit(ICE_VF_DIS, vsi->back->state))
		return NETDEV_TX_BUSY;

	repr = ice_netdev_to_repr(netdev);
	skb_dst_drop(skb);
	dst_hold((struct dst_entry *)repr->dst);
	skb_dst_set(skb, (struct dst_entry *)repr->dst);
	skb->queue_mapping = repr->q_id;

	return ice_start_xmit(skb, netdev);
}

/**
 * ice_eswitch_set_target_vsi - set eswitch context in Tx context descriptor
 * @skb: pointer to send buffer
 * @off: pointer to offload struct
 */
void
ice_eswitch_set_target_vsi(struct sk_buff *skb,
			   struct ice_tx_offload_params *off)
{
	struct metadata_dst *dst = skb_metadata_dst(skb);
	u64 cd_cmd, dst_vsi;

	if (!dst) {
		cd_cmd = ICE_TX_CTX_DESC_SWTCH_UPLINK << ICE_TXD_CTX_QW1_CMD_S;
		off->cd_qw1 |= (cd_cmd | ICE_TX_DESC_DTYPE_CTX);
	} else {
		cd_cmd = ICE_TX_CTX_DESC_SWTCH_VSI << ICE_TXD_CTX_QW1_CMD_S;
		dst_vsi = FIELD_PREP(ICE_TXD_CTX_QW1_VSI_M,
				     dst->u.port_info.port_id);
		off->cd_qw1 = cd_cmd | dst_vsi | ICE_TX_DESC_DTYPE_CTX;
	}
}

/**
 * ice_eswitch_release_env - clear eswitch HW filters
 * @pf: pointer to PF struct
 *
 * This function removes HW filters configuration specific for switchdev
 * mode and restores default legacy mode settings.
 */
static void ice_eswitch_release_env(struct ice_pf *pf)
{
	struct ice_vsi *uplink_vsi = pf->eswitch.uplink_vsi;
	struct ice_vsi *ctrl_vsi = pf->eswitch.control_vsi;
	struct ice_vsi_vlan_ops *vlan_ops;

	vlan_ops = ice_get_compat_vsi_vlan_ops(uplink_vsi);

	ice_vsi_update_local_lb(uplink_vsi, false);
	ice_vsi_update_security(ctrl_vsi, ice_vsi_ctx_clear_allow_override);
	ice_vsi_update_security(uplink_vsi, ice_vsi_ctx_clear_allow_override);
	vlan_ops->ena_rx_filtering(uplink_vsi);
	ice_clear_dflt_vsi(uplink_vsi);
	ice_fltr_add_mac_and_broadcast(uplink_vsi,
				       uplink_vsi->port_info->mac.perm_addr,
				       ICE_FWD_TO_VSI);
}

/**
 * ice_eswitch_vsi_setup - configure eswitch control VSI
 * @pf: pointer to PF structure
 * @pi: pointer to port_info structure
 */
static struct ice_vsi *
ice_eswitch_vsi_setup(struct ice_pf *pf, struct ice_port_info *pi)
{
	struct ice_vsi_cfg_params params = {};

	params.type = ICE_VSI_SWITCHDEV_CTRL;
	params.pi = pi;
	params.flags = ICE_VSI_FLAG_INIT;

	return ice_vsi_setup(pf, &params);
}

/**
 * ice_eswitch_napi_enable - enable NAPI for all port representors
 * @reprs: xarray of reprs
 */
static void ice_eswitch_napi_enable(struct xarray *reprs)
{
	struct ice_repr *repr;
	unsigned long id;

	xa_for_each(reprs, id, repr)
		napi_enable(&repr->q_vector->napi);
}

/**
 * ice_eswitch_napi_disable - disable NAPI for all port representors
 * @reprs: xarray of reprs
 */
static void ice_eswitch_napi_disable(struct xarray *reprs)
{
	struct ice_repr *repr;
	unsigned long id;

	xa_for_each(reprs, id, repr)
		napi_disable(&repr->q_vector->napi);
}

/**
 * ice_eswitch_enable_switchdev - configure eswitch in switchdev mode
 * @pf: pointer to PF structure
 */
static int ice_eswitch_enable_switchdev(struct ice_pf *pf)
{
	struct ice_vsi *ctrl_vsi, *uplink_vsi;

	uplink_vsi = ice_get_main_vsi(pf);
	if (!uplink_vsi)
		return -ENODEV;

	if (netif_is_any_bridge_port(uplink_vsi->netdev)) {
		dev_err(ice_pf_to_dev(pf),
			"Uplink port cannot be a bridge port\n");
		return -EINVAL;
	}

	pf->eswitch.control_vsi = ice_eswitch_vsi_setup(pf, pf->hw.port_info);
	if (!pf->eswitch.control_vsi)
		return -ENODEV;

	ctrl_vsi = pf->eswitch.control_vsi;
	/* cp VSI is createad with 1 queue as default */
	pf->eswitch.qs.value = 1;
	pf->eswitch.uplink_vsi = uplink_vsi;

	if (ice_eswitch_setup_env(pf))
		goto err_vsi;

	if (ice_eswitch_br_offloads_init(pf))
		goto err_br_offloads;

	pf->eswitch.is_running = true;

	return 0;

err_br_offloads:
	ice_eswitch_release_env(pf);
err_vsi:
	ice_vsi_release(ctrl_vsi);
	return -ENODEV;
}

/**
 * ice_eswitch_disable_switchdev - disable eswitch resources
 * @pf: pointer to PF structure
 */
static void ice_eswitch_disable_switchdev(struct ice_pf *pf)
{
	struct ice_vsi *ctrl_vsi = pf->eswitch.control_vsi;

	ice_eswitch_br_offloads_deinit(pf);
	ice_eswitch_release_env(pf);
	ice_vsi_release(ctrl_vsi);

	pf->eswitch.is_running = false;
	pf->eswitch.qs.is_reaching = false;
}

/**
 * ice_eswitch_mode_set - set new eswitch mode
 * @devlink: pointer to devlink structure
 * @mode: eswitch mode to switch to
 * @extack: pointer to extack structure
 */
int
ice_eswitch_mode_set(struct devlink *devlink, u16 mode,
		     struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_priv(devlink);

	if (pf->eswitch_mode == mode)
		return 0;

	if (ice_has_vfs(pf)) {
		dev_info(ice_pf_to_dev(pf), "Changing eswitch mode is allowed only if there is no VFs created");
		NL_SET_ERR_MSG_MOD(extack, "Changing eswitch mode is allowed only if there is no VFs created");
		return -EOPNOTSUPP;
	}

	switch (mode) {
	case DEVLINK_ESWITCH_MODE_LEGACY:
		dev_info(ice_pf_to_dev(pf), "PF %d changed eswitch mode to legacy",
			 pf->hw.pf_id);
		xa_destroy(&pf->eswitch.reprs);
		NL_SET_ERR_MSG_MOD(extack, "Changed eswitch mode to legacy");
		break;
	case DEVLINK_ESWITCH_MODE_SWITCHDEV:
	{
		if (ice_is_adq_active(pf)) {
			dev_err(ice_pf_to_dev(pf), "Couldn't change eswitch mode to switchdev - ADQ is active. Delete ADQ configs and try again, e.g. tc qdisc del dev $PF root");
			NL_SET_ERR_MSG_MOD(extack, "Couldn't change eswitch mode to switchdev - ADQ is active. Delete ADQ configs and try again, e.g. tc qdisc del dev $PF root");
			return -EOPNOTSUPP;
		}

		dev_info(ice_pf_to_dev(pf), "PF %d changed eswitch mode to switchdev",
			 pf->hw.pf_id);
		xa_init_flags(&pf->eswitch.reprs, XA_FLAGS_ALLOC);
		NL_SET_ERR_MSG_MOD(extack, "Changed eswitch mode to switchdev");
		break;
	}
	default:
		NL_SET_ERR_MSG_MOD(extack, "Unknown eswitch mode");
		return -EINVAL;
	}

	pf->eswitch_mode = mode;
	return 0;
}

/**
 * ice_eswitch_mode_get - get current eswitch mode
 * @devlink: pointer to devlink structure
 * @mode: output parameter for current eswitch mode
 */
int ice_eswitch_mode_get(struct devlink *devlink, u16 *mode)
{
	struct ice_pf *pf = devlink_priv(devlink);

	*mode = pf->eswitch_mode;
	return 0;
}

/**
 * ice_is_eswitch_mode_switchdev - check if eswitch mode is set to switchdev
 * @pf: pointer to PF structure
 *
 * Returns true if eswitch mode is set to DEVLINK_ESWITCH_MODE_SWITCHDEV,
 * false otherwise.
 */
bool ice_is_eswitch_mode_switchdev(struct ice_pf *pf)
{
	return pf->eswitch_mode == DEVLINK_ESWITCH_MODE_SWITCHDEV;
}

/**
 * ice_eswitch_start_all_tx_queues - start Tx queues of all port representors
 * @pf: pointer to PF structure
 */
static void ice_eswitch_start_all_tx_queues(struct ice_pf *pf)
{
	struct ice_repr *repr;
	unsigned long id;

	if (test_bit(ICE_DOWN, pf->state))
		return;

	xa_for_each(&pf->eswitch.reprs, id, repr)
		ice_repr_start_tx_queues(repr);
}

/**
 * ice_eswitch_stop_all_tx_queues - stop Tx queues of all port representors
 * @pf: pointer to PF structure
 */
void ice_eswitch_stop_all_tx_queues(struct ice_pf *pf)
{
	struct ice_repr *repr;
	unsigned long id;

	if (test_bit(ICE_DOWN, pf->state))
		return;

	xa_for_each(&pf->eswitch.reprs, id, repr)
		ice_repr_stop_tx_queues(repr);
}

static void ice_eswitch_stop_reprs(struct ice_pf *pf)
{
	ice_eswitch_del_sp_rules(pf);
	ice_eswitch_stop_all_tx_queues(pf);
	ice_eswitch_napi_disable(&pf->eswitch.reprs);
}

static void ice_eswitch_start_reprs(struct ice_pf *pf)
{
	ice_eswitch_napi_enable(&pf->eswitch.reprs);
	ice_eswitch_start_all_tx_queues(pf);
	ice_eswitch_add_sp_rules(pf);
}

static void
ice_eswitch_cp_change_queues(struct ice_eswitch *eswitch, int change)
{
	struct ice_vsi *cp = eswitch->control_vsi;
	int queues = 0;

	if (eswitch->qs.is_reaching) {
		if (eswitch->qs.to_reach >= eswitch->qs.value + change) {
			queues = eswitch->qs.to_reach;
			eswitch->qs.is_reaching = false;
		} else {
			queues = 0;
		}
	} else if ((change > 0 && cp->alloc_txq <= eswitch->qs.value) ||
		   change < 0) {
		queues = cp->alloc_txq + change;
	}

	if (queues) {
		cp->req_txq = queues;
		cp->req_rxq = queues;
		ice_vsi_close(cp);
		ice_vsi_rebuild(cp, ICE_VSI_FLAG_NO_INIT);
		ice_vsi_open(cp);
	} else if (!change) {
		/* change == 0 means that VSI wasn't open, open it here */
		ice_vsi_open(cp);
	}

	eswitch->qs.value += change;
	ice_eswitch_remap_rings_to_vectors(eswitch);
}

int
ice_eswitch_attach(struct ice_pf *pf, struct ice_vf *vf)
{
	struct ice_repr *repr;
	int change = 1;
	int err;

	if (pf->eswitch_mode == DEVLINK_ESWITCH_MODE_LEGACY)
		return 0;

	if (xa_empty(&pf->eswitch.reprs)) {
		err = ice_eswitch_enable_switchdev(pf);
		if (err)
			return err;
		/* Control plane VSI is created with 1 queue as default */
		pf->eswitch.qs.to_reach -= 1;
		change = 0;
	}

	ice_eswitch_stop_reprs(pf);

	repr = ice_repr_add_vf(vf);
	if (IS_ERR(repr)) {
		err = PTR_ERR(repr);
		goto err_create_repr;
	}

	err = ice_eswitch_setup_repr(pf, repr);
	if (err)
		goto err_setup_repr;

	err = xa_alloc(&pf->eswitch.reprs, &repr->id, repr,
		       XA_LIMIT(1, INT_MAX), GFP_KERNEL);
	if (err)
		goto err_xa_alloc;

	vf->repr_id = repr->id;

	ice_eswitch_cp_change_queues(&pf->eswitch, change);
	ice_eswitch_start_reprs(pf);

	return 0;

err_xa_alloc:
	ice_eswitch_release_repr(pf, repr);
err_setup_repr:
	ice_repr_rem_vf(repr);
err_create_repr:
	if (xa_empty(&pf->eswitch.reprs))
		ice_eswitch_disable_switchdev(pf);
	ice_eswitch_start_reprs(pf);

	return err;
}

void ice_eswitch_detach(struct ice_pf *pf, struct ice_vf *vf)
{
	struct ice_repr *repr = xa_load(&pf->eswitch.reprs, vf->repr_id);
	struct devlink *devlink = priv_to_devlink(pf);

	if (!repr)
		return;

	ice_eswitch_stop_reprs(pf);
	xa_erase(&pf->eswitch.reprs, repr->id);

	if (xa_empty(&pf->eswitch.reprs))
		ice_eswitch_disable_switchdev(pf);
	else
		ice_eswitch_cp_change_queues(&pf->eswitch, -1);

	ice_eswitch_release_repr(pf, repr);
	ice_repr_rem_vf(repr);

	if (xa_empty(&pf->eswitch.reprs)) {
		/* since all port representors are destroyed, there is
		 * no point in keeping the nodes
		 */
		ice_devlink_rate_clear_tx_topology(ice_get_main_vsi(pf));
		devl_lock(devlink);
		devl_rate_nodes_destroy(devlink);
		devl_unlock(devlink);
	} else {
		ice_eswitch_start_reprs(pf);
	}
}

/**
 * ice_eswitch_rebuild - rebuild eswitch
 * @pf: pointer to PF structure
 */
int ice_eswitch_rebuild(struct ice_pf *pf)
{
	struct ice_repr *repr;
	unsigned long id;
	int err;

	if (!ice_is_switchdev_running(pf))
		return 0;

	err = ice_vsi_rebuild(pf->eswitch.control_vsi, ICE_VSI_FLAG_INIT);
	if (err)
		return err;

	xa_for_each(&pf->eswitch.reprs, id, repr)
		ice_eswitch_detach(pf, repr->vf);

	return 0;
}

/**
 * ice_eswitch_reserve_cp_queues - reserve control plane VSI queues
 * @pf: pointer to PF structure
 * @change: how many more (or less) queues is needed
 *
 * Remember to call ice_eswitch_attach/detach() the "change" times.
 */
void ice_eswitch_reserve_cp_queues(struct ice_pf *pf, int change)
{
	if (pf->eswitch.qs.value + change < 0)
		return;

	pf->eswitch.qs.to_reach = pf->eswitch.qs.value + change;
	pf->eswitch.qs.is_reaching = true;
}
