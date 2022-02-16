// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2021, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"
#include "ice_eswitch.h"
#include "ice_fltr.h"
#include "ice_repr.h"
#include "ice_devlink.h"
#include "ice_tc_lib.h"

/**
 * ice_eswitch_add_vf_mac_rule - add adv rule with VF's MAC
 * @pf: pointer to PF struct
 * @vf: pointer to VF struct
 * @mac: VF's MAC address
 *
 * This function adds advanced rule that forwards packets with
 * VF's MAC address (src MAC) to the corresponding switchdev ctrl VSI queue.
 */
int
ice_eswitch_add_vf_mac_rule(struct ice_pf *pf, struct ice_vf *vf, const u8 *mac)
{
	struct ice_vsi *ctrl_vsi = pf->switchdev.control_vsi;
	struct ice_adv_rule_info rule_info = { 0 };
	struct ice_adv_lkup_elem *list;
	struct ice_hw *hw = &pf->hw;
	const u16 lkups_cnt = 1;
	int err;

	list = kcalloc(lkups_cnt, sizeof(*list), GFP_ATOMIC);
	if (!list)
		return -ENOMEM;

	list[0].type = ICE_MAC_OFOS;
	ether_addr_copy(list[0].h_u.eth_hdr.src_addr, mac);
	eth_broadcast_addr(list[0].m_u.eth_hdr.src_addr);

	rule_info.sw_act.flag |= ICE_FLTR_TX;
	rule_info.sw_act.vsi_handle = ctrl_vsi->idx;
	rule_info.sw_act.fltr_act = ICE_FWD_TO_Q;
	rule_info.rx = false;
	rule_info.sw_act.fwd_id.q_id = hw->func_caps.common_cap.rxq_first_id +
				       ctrl_vsi->rxq_map[vf->vf_id];
	rule_info.flags_info.act |= ICE_SINGLE_ACT_LB_ENABLE;
	rule_info.flags_info.act_valid = true;
	rule_info.tun_type = ICE_SW_TUN_AND_NON_TUN;

	err = ice_add_adv_rule(hw, list, lkups_cnt, &rule_info,
			       vf->repr->mac_rule);
	if (err)
		dev_err(ice_pf_to_dev(pf), "Unable to add VF mac rule in switchdev mode for VF %d",
			vf->vf_id);
	else
		vf->repr->rule_added = true;

	kfree(list);
	return err;
}

/**
 * ice_eswitch_replay_vf_mac_rule - replay adv rule with VF's MAC
 * @vf: pointer to vF struct
 *
 * This function replays VF's MAC rule after reset.
 */
void ice_eswitch_replay_vf_mac_rule(struct ice_vf *vf)
{
	int err;

	if (!ice_is_switchdev_running(vf->pf))
		return;

	if (is_valid_ether_addr(vf->hw_lan_addr.addr)) {
		err = ice_eswitch_add_vf_mac_rule(vf->pf, vf,
						  vf->hw_lan_addr.addr);
		if (err) {
			dev_err(ice_pf_to_dev(vf->pf), "Failed to add MAC %pM for VF %d\n, error %d\n",
				vf->hw_lan_addr.addr, vf->vf_id, err);
			return;
		}
		vf->num_mac++;

		ether_addr_copy(vf->dev_lan_addr.addr, vf->hw_lan_addr.addr);
	}
}

/**
 * ice_eswitch_del_vf_mac_rule - delete adv rule with VF's MAC
 * @vf: pointer to the VF struct
 *
 * Delete the advanced rule that was used to forward packets with the VF's MAC
 * address (src MAC) to the corresponding switchdev ctrl VSI queue.
 */
void ice_eswitch_del_vf_mac_rule(struct ice_vf *vf)
{
	if (!ice_is_switchdev_running(vf->pf))
		return;

	if (!vf->repr->rule_added)
		return;

	ice_rem_adv_rule_by_id(&vf->pf->hw, vf->repr->mac_rule);
	vf->repr->rule_added = false;
}

/**
 * ice_eswitch_setup_env - configure switchdev HW filters
 * @pf: pointer to PF struct
 *
 * This function adds HW filters configuration specific for switchdev
 * mode.
 */
static int ice_eswitch_setup_env(struct ice_pf *pf)
{
	struct ice_vsi *uplink_vsi = pf->switchdev.uplink_vsi;
	struct net_device *uplink_netdev = uplink_vsi->netdev;
	struct ice_vsi *ctrl_vsi = pf->switchdev.control_vsi;
	struct ice_vsi_vlan_ops *vlan_ops;
	bool rule_added = false;

	vlan_ops = ice_get_compat_vsi_vlan_ops(ctrl_vsi);
	if (vlan_ops->dis_stripping(ctrl_vsi))
		return -ENODEV;

	ice_remove_vsi_fltr(&pf->hw, uplink_vsi->idx);

	netif_addr_lock_bh(uplink_netdev);
	__dev_uc_unsync(uplink_netdev, NULL);
	__dev_mc_unsync(uplink_netdev, NULL);
	netif_addr_unlock_bh(uplink_netdev);

	if (ice_vsi_add_vlan_zero(uplink_vsi))
		goto err_def_rx;

	if (!ice_is_dflt_vsi_in_use(uplink_vsi->vsw)) {
		if (ice_set_dflt_vsi(uplink_vsi->vsw, uplink_vsi))
			goto err_def_rx;
		rule_added = true;
	}

	if (ice_vsi_update_security(uplink_vsi, ice_vsi_ctx_set_allow_override))
		goto err_override_uplink;

	if (ice_vsi_update_security(ctrl_vsi, ice_vsi_ctx_set_allow_override))
		goto err_override_control;

	return 0;

err_override_control:
	ice_vsi_update_security(uplink_vsi, ice_vsi_ctx_clear_allow_override);
err_override_uplink:
	if (rule_added)
		ice_clear_dflt_vsi(uplink_vsi->vsw);
err_def_rx:
	ice_fltr_add_mac_and_broadcast(uplink_vsi,
				       uplink_vsi->port_info->mac.perm_addr,
				       ICE_FWD_TO_VSI);
	return -ENODEV;
}

/**
 * ice_eswitch_remap_rings_to_vectors - reconfigure rings of switchdev ctrl VSI
 * @pf: pointer to PF struct
 *
 * In switchdev number of allocated Tx/Rx rings is equal.
 *
 * This function fills q_vectors structures associated with representor and
 * move each ring pairs to port representor netdevs. Each port representor
 * will have dedicated 1 Tx/Rx ring pair, so number of rings pair is equal to
 * number of VFs.
 */
static void ice_eswitch_remap_rings_to_vectors(struct ice_pf *pf)
{
	struct ice_vsi *vsi = pf->switchdev.control_vsi;
	int q_id;

	ice_for_each_txq(vsi, q_id) {
		struct ice_q_vector *q_vector;
		struct ice_tx_ring *tx_ring;
		struct ice_rx_ring *rx_ring;
		struct ice_repr *repr;
		struct ice_vf *vf;

		vf = ice_get_vf_by_id(pf, q_id);
		if (WARN_ON(!vf))
			continue;

		repr = vf->repr;
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
 * ice_eswitch_release_reprs - clear PR VSIs configuration
 * @pf: poiner to PF struct
 * @ctrl_vsi: pointer to switchdev control VSI
 */
static void
ice_eswitch_release_reprs(struct ice_pf *pf, struct ice_vsi *ctrl_vsi)
{
	struct ice_vf *vf;
	unsigned int bkt;

	ice_for_each_vf(pf, bkt, vf) {
		struct ice_vsi *vsi = vf->repr->src_vsi;

		/* Skip VFs that aren't configured */
		if (!vf->repr->dst)
			continue;

		ice_vsi_update_security(vsi, ice_vsi_ctx_set_antispoof);
		metadata_dst_free(vf->repr->dst);
		vf->repr->dst = NULL;
		ice_fltr_add_mac_and_broadcast(vsi, vf->hw_lan_addr.addr,
					       ICE_FWD_TO_VSI);

		netif_napi_del(&vf->repr->q_vector->napi);
	}
}

/**
 * ice_eswitch_setup_reprs - configure port reprs to run in switchdev mode
 * @pf: pointer to PF struct
 */
static int ice_eswitch_setup_reprs(struct ice_pf *pf)
{
	struct ice_vsi *ctrl_vsi = pf->switchdev.control_vsi;
	int max_vsi_num = 0;
	struct ice_vf *vf;
	unsigned int bkt;

	ice_for_each_vf(pf, bkt, vf) {
		struct ice_vsi *vsi = vf->repr->src_vsi;

		ice_remove_vsi_fltr(&pf->hw, vsi->idx);
		vf->repr->dst = metadata_dst_alloc(0, METADATA_HW_PORT_MUX,
						   GFP_KERNEL);
		if (!vf->repr->dst) {
			ice_fltr_add_mac_and_broadcast(vsi,
						       vf->hw_lan_addr.addr,
						       ICE_FWD_TO_VSI);
			goto err;
		}

		if (ice_vsi_update_security(vsi, ice_vsi_ctx_clear_antispoof)) {
			ice_fltr_add_mac_and_broadcast(vsi,
						       vf->hw_lan_addr.addr,
						       ICE_FWD_TO_VSI);
			metadata_dst_free(vf->repr->dst);
			vf->repr->dst = NULL;
			goto err;
		}

		if (ice_vsi_add_vlan_zero(vsi)) {
			ice_fltr_add_mac_and_broadcast(vsi,
						       vf->hw_lan_addr.addr,
						       ICE_FWD_TO_VSI);
			metadata_dst_free(vf->repr->dst);
			vf->repr->dst = NULL;
			ice_vsi_update_security(vsi, ice_vsi_ctx_set_antispoof);
			goto err;
		}

		if (max_vsi_num < vsi->vsi_num)
			max_vsi_num = vsi->vsi_num;

		netif_napi_add(vf->repr->netdev, &vf->repr->q_vector->napi, ice_napi_poll,
			       NAPI_POLL_WEIGHT);

		netif_keep_dst(vf->repr->netdev);
	}

	ice_for_each_vf(pf, bkt, vf) {
		struct ice_repr *repr = vf->repr;
		struct ice_vsi *vsi = repr->src_vsi;
		struct metadata_dst *dst;

		dst = repr->dst;
		dst->u.port_info.port_id = vsi->vsi_num;
		dst->u.port_info.lower_dev = repr->netdev;
		ice_repr_set_traffic_vsi(repr, ctrl_vsi);
	}

	return 0;

err:
	ice_eswitch_release_reprs(pf, ctrl_vsi);

	return -ENODEV;
}

/**
 * ice_eswitch_update_repr - reconfigure VF port representor
 * @vsi: VF VSI for which port representor is configured
 */
void ice_eswitch_update_repr(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct ice_repr *repr;
	struct ice_vf *vf;
	int ret;

	if (!ice_is_switchdev_running(pf))
		return;

	vf = vsi->vf;
	repr = vf->repr;
	repr->src_vsi = vsi;
	repr->dst->u.port_info.port_id = vsi->vsi_num;

	ret = ice_vsi_update_security(vsi, ice_vsi_ctx_clear_antispoof);
	if (ret) {
		ice_fltr_add_mac_and_broadcast(vsi, vf->hw_lan_addr.addr, ICE_FWD_TO_VSI);
		dev_err(ice_pf_to_dev(pf), "Failed to update VF %d port representor",
			vsi->vf->vf_id);
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

	if (ice_is_reset_in_progress(vsi->back->state))
		return NETDEV_TX_BUSY;

	repr = ice_netdev_to_repr(netdev);
	skb_dst_drop(skb);
	dst_hold((struct dst_entry *)repr->dst);
	skb_dst_set(skb, (struct dst_entry *)repr->dst);
	skb->queue_mapping = repr->vf->vf_id;

	return ice_start_xmit(skb, netdev);
}

/**
 * ice_eswitch_set_target_vsi - set switchdev context in Tx context descriptor
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
		dst_vsi = ((u64)dst->u.port_info.port_id <<
			   ICE_TXD_CTX_QW1_VSI_S) & ICE_TXD_CTX_QW1_VSI_M;
		off->cd_qw1 = cd_cmd | dst_vsi | ICE_TX_DESC_DTYPE_CTX;
	}
}

/**
 * ice_eswitch_release_env - clear switchdev HW filters
 * @pf: pointer to PF struct
 *
 * This function removes HW filters configuration specific for switchdev
 * mode and restores default legacy mode settings.
 */
static void ice_eswitch_release_env(struct ice_pf *pf)
{
	struct ice_vsi *uplink_vsi = pf->switchdev.uplink_vsi;
	struct ice_vsi *ctrl_vsi = pf->switchdev.control_vsi;

	ice_vsi_update_security(ctrl_vsi, ice_vsi_ctx_clear_allow_override);
	ice_vsi_update_security(uplink_vsi, ice_vsi_ctx_clear_allow_override);
	ice_clear_dflt_vsi(uplink_vsi->vsw);
	ice_fltr_add_mac_and_broadcast(uplink_vsi,
				       uplink_vsi->port_info->mac.perm_addr,
				       ICE_FWD_TO_VSI);
}

/**
 * ice_eswitch_vsi_setup - configure switchdev control VSI
 * @pf: pointer to PF structure
 * @pi: pointer to port_info structure
 */
static struct ice_vsi *
ice_eswitch_vsi_setup(struct ice_pf *pf, struct ice_port_info *pi)
{
	return ice_vsi_setup(pf, pi, ICE_VSI_SWITCHDEV_CTRL, NULL, NULL);
}

/**
 * ice_eswitch_napi_del - remove NAPI handle for all port representors
 * @pf: pointer to PF structure
 */
static void ice_eswitch_napi_del(struct ice_pf *pf)
{
	struct ice_vf *vf;
	unsigned int bkt;

	ice_for_each_vf(pf, bkt, vf)
		netif_napi_del(&vf->repr->q_vector->napi);
}

/**
 * ice_eswitch_napi_enable - enable NAPI for all port representors
 * @pf: pointer to PF structure
 */
static void ice_eswitch_napi_enable(struct ice_pf *pf)
{
	struct ice_vf *vf;
	unsigned int bkt;

	ice_for_each_vf(pf, bkt, vf)
		napi_enable(&vf->repr->q_vector->napi);
}

/**
 * ice_eswitch_napi_disable - disable NAPI for all port representors
 * @pf: pointer to PF structure
 */
static void ice_eswitch_napi_disable(struct ice_pf *pf)
{
	struct ice_vf *vf;
	unsigned int bkt;

	ice_for_each_vf(pf, bkt, vf)
		napi_disable(&vf->repr->q_vector->napi);
}

/**
 * ice_eswitch_enable_switchdev - configure eswitch in switchdev mode
 * @pf: pointer to PF structure
 */
static int ice_eswitch_enable_switchdev(struct ice_pf *pf)
{
	struct ice_vsi *ctrl_vsi;

	pf->switchdev.control_vsi = ice_eswitch_vsi_setup(pf, pf->hw.port_info);
	if (!pf->switchdev.control_vsi)
		return -ENODEV;

	ctrl_vsi = pf->switchdev.control_vsi;
	pf->switchdev.uplink_vsi = ice_get_main_vsi(pf);
	if (!pf->switchdev.uplink_vsi)
		goto err_vsi;

	if (ice_eswitch_setup_env(pf))
		goto err_vsi;

	if (ice_repr_add_for_all_vfs(pf))
		goto err_repr_add;

	if (ice_eswitch_setup_reprs(pf))
		goto err_setup_reprs;

	ice_eswitch_remap_rings_to_vectors(pf);

	if (ice_vsi_open(ctrl_vsi))
		goto err_setup_reprs;

	ice_eswitch_napi_enable(pf);

	return 0;

err_setup_reprs:
	ice_repr_rem_from_all_vfs(pf);
err_repr_add:
	ice_eswitch_release_env(pf);
err_vsi:
	ice_vsi_release(ctrl_vsi);
	return -ENODEV;
}

/**
 * ice_eswitch_disable_switchdev - disable switchdev resources
 * @pf: pointer to PF structure
 */
static void ice_eswitch_disable_switchdev(struct ice_pf *pf)
{
	struct ice_vsi *ctrl_vsi = pf->switchdev.control_vsi;

	ice_eswitch_napi_disable(pf);
	ice_eswitch_release_env(pf);
	ice_rem_adv_rule_for_vsi(&pf->hw, ctrl_vsi->idx);
	ice_eswitch_release_reprs(pf, ctrl_vsi);
	ice_vsi_release(ctrl_vsi);
	ice_repr_rem_from_all_vfs(pf);
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
		NL_SET_ERR_MSG_MOD(extack, "Changed eswitch mode to legacy");
		break;
	case DEVLINK_ESWITCH_MODE_SWITCHDEV:
	{
		dev_info(ice_pf_to_dev(pf), "PF %d changed eswitch mode to switchdev",
			 pf->hw.pf_id);
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
 * ice_eswitch_release - cleanup eswitch
 * @pf: pointer to PF structure
 */
void ice_eswitch_release(struct ice_pf *pf)
{
	if (pf->eswitch_mode == DEVLINK_ESWITCH_MODE_LEGACY)
		return;

	ice_eswitch_disable_switchdev(pf);
	pf->switchdev.is_running = false;
}

/**
 * ice_eswitch_configure - configure eswitch
 * @pf: pointer to PF structure
 */
int ice_eswitch_configure(struct ice_pf *pf)
{
	int status;

	if (pf->eswitch_mode == DEVLINK_ESWITCH_MODE_LEGACY || pf->switchdev.is_running)
		return 0;

	status = ice_eswitch_enable_switchdev(pf);
	if (status)
		return status;

	pf->switchdev.is_running = true;
	return 0;
}

/**
 * ice_eswitch_start_all_tx_queues - start Tx queues of all port representors
 * @pf: pointer to PF structure
 */
static void ice_eswitch_start_all_tx_queues(struct ice_pf *pf)
{
	struct ice_vf *vf;
	unsigned int bkt;

	if (test_bit(ICE_DOWN, pf->state))
		return;

	ice_for_each_vf(pf, bkt, vf) {
		if (vf->repr)
			ice_repr_start_tx_queues(vf->repr);
	}
}

/**
 * ice_eswitch_stop_all_tx_queues - stop Tx queues of all port representors
 * @pf: pointer to PF structure
 */
void ice_eswitch_stop_all_tx_queues(struct ice_pf *pf)
{
	struct ice_vf *vf;
	unsigned int bkt;

	if (test_bit(ICE_DOWN, pf->state))
		return;

	ice_for_each_vf(pf, bkt, vf) {
		if (vf->repr)
			ice_repr_stop_tx_queues(vf->repr);
	}
}

/**
 * ice_eswitch_rebuild - rebuild eswitch
 * @pf: pointer to PF structure
 */
int ice_eswitch_rebuild(struct ice_pf *pf)
{
	struct ice_vsi *ctrl_vsi = pf->switchdev.control_vsi;
	int status;

	ice_eswitch_napi_disable(pf);
	ice_eswitch_napi_del(pf);

	status = ice_eswitch_setup_env(pf);
	if (status)
		return status;

	status = ice_eswitch_setup_reprs(pf);
	if (status)
		return status;

	ice_eswitch_remap_rings_to_vectors(pf);

	ice_replay_tc_fltrs(pf);

	status = ice_vsi_open(ctrl_vsi);
	if (status)
		return status;

	ice_eswitch_napi_enable(pf);
	ice_eswitch_start_all_tx_queues(pf);

	return 0;
}
