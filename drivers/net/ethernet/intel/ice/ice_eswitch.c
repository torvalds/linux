// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2021, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"
#include "ice_eswitch.h"
#include "ice_eswitch_br.h"
#include "ice_fltr.h"
#include "ice_repr.h"
#include "devlink/devlink.h"
#include "ice_tc_lib.h"

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
	struct net_device *netdev = uplink_vsi->netdev;
	bool if_running = netif_running(netdev);
	struct ice_vsi_vlan_ops *vlan_ops;

	if (if_running && !test_and_set_bit(ICE_VSI_DOWN, uplink_vsi->state))
		if (ice_down(uplink_vsi))
			return -ENODEV;

	ice_remove_vsi_fltr(&pf->hw, uplink_vsi->idx);

	netif_addr_lock_bh(netdev);
	__dev_uc_unsync(netdev, NULL);
	__dev_mc_unsync(netdev, NULL);
	netif_addr_unlock_bh(netdev);

	if (ice_vsi_add_vlan_zero(uplink_vsi))
		goto err_vlan_zero;

	if (ice_cfg_dflt_vsi(uplink_vsi->port_info, uplink_vsi->idx, true,
			     ICE_FLTR_RX))
		goto err_def_rx;

	if (ice_cfg_dflt_vsi(uplink_vsi->port_info, uplink_vsi->idx, true,
			     ICE_FLTR_TX))
		goto err_def_tx;

	vlan_ops = ice_get_compat_vsi_vlan_ops(uplink_vsi);
	if (vlan_ops->dis_rx_filtering(uplink_vsi))
		goto err_vlan_filtering;

	if (ice_vsi_update_security(uplink_vsi, ice_vsi_ctx_set_allow_override))
		goto err_override_uplink;

	if (ice_vsi_update_local_lb(uplink_vsi, true))
		goto err_override_local_lb;

	if (if_running && ice_up(uplink_vsi))
		goto err_up;

	return 0;

err_up:
	ice_vsi_update_local_lb(uplink_vsi, false);
err_override_local_lb:
	ice_vsi_update_security(uplink_vsi, ice_vsi_ctx_clear_allow_override);
err_override_uplink:
	vlan_ops->ena_rx_filtering(uplink_vsi);
err_vlan_filtering:
	ice_cfg_dflt_vsi(uplink_vsi->port_info, uplink_vsi->idx, false,
			 ICE_FLTR_TX);
err_def_tx:
	ice_cfg_dflt_vsi(uplink_vsi->port_info, uplink_vsi->idx, false,
			 ICE_FLTR_RX);
err_def_rx:
	ice_vsi_del_vlan_zero(uplink_vsi);
err_vlan_zero:
	ice_fltr_add_mac_and_broadcast(uplink_vsi,
				       uplink_vsi->port_info->mac.perm_addr,
				       ICE_FWD_TO_VSI);
	if (if_running)
		ice_up(uplink_vsi);

	return -ENODEV;
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
}

/**
 * ice_eswitch_setup_repr - configure PR to run in switchdev mode
 * @pf: pointer to PF struct
 * @repr: pointer to PR struct
 */
static int ice_eswitch_setup_repr(struct ice_pf *pf, struct ice_repr *repr)
{
	struct ice_vsi *uplink_vsi = pf->eswitch.uplink_vsi;
	struct ice_vsi *vsi = repr->src_vsi;
	struct metadata_dst *dst;

	repr->dst = metadata_dst_alloc(0, METADATA_HW_PORT_MUX,
				       GFP_KERNEL);
	if (!repr->dst)
		return -ENOMEM;

	netif_keep_dst(uplink_vsi->netdev);

	dst = repr->dst;
	dst->u.port_info.port_id = vsi->vsi_num;
	dst->u.port_info.lower_dev = uplink_vsi->netdev;

	return 0;
}

/**
 * ice_eswitch_cfg_vsi - configure VSI to work in slow-path
 * @vsi: VSI structure of representee
 * @mac: representee MAC
 *
 * Return: 0 on success, non-zero on error.
 */
int ice_eswitch_cfg_vsi(struct ice_vsi *vsi, const u8 *mac)
{
	int err;

	ice_remove_vsi_fltr(&vsi->back->hw, vsi->idx);

	err = ice_vsi_update_security(vsi, ice_vsi_ctx_clear_antispoof);
	if (err)
		goto err_update_security;

	err = ice_vsi_add_vlan_zero(vsi);
	if (err)
		goto err_vlan_zero;

	return 0;

err_vlan_zero:
	ice_vsi_update_security(vsi, ice_vsi_ctx_set_antispoof);
err_update_security:
	ice_fltr_add_mac_and_broadcast(vsi, mac, ICE_FWD_TO_VSI);

	return err;
}

/**
 * ice_eswitch_decfg_vsi - unroll changes done to VSI for switchdev
 * @vsi: VSI structure of representee
 * @mac: representee MAC
 */
void ice_eswitch_decfg_vsi(struct ice_vsi *vsi, const u8 *mac)
{
	ice_vsi_update_security(vsi, ice_vsi_ctx_set_antispoof);
	ice_fltr_add_mac_and_broadcast(vsi, mac, ICE_FWD_TO_VSI);
}

/**
 * ice_eswitch_update_repr - reconfigure port representor
 * @repr_id: representor ID
 * @vsi: VSI for which port representor is configured
 */
void ice_eswitch_update_repr(unsigned long *repr_id, struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct ice_repr *repr;
	int err;

	if (!ice_is_switchdev_running(pf))
		return;

	repr = xa_load(&pf->eswitch.reprs, *repr_id);
	if (!repr)
		return;

	repr->src_vsi = vsi;
	repr->dst->u.port_info.port_id = vsi->vsi_num;

	if (repr->br_port)
		repr->br_port->vsi = vsi;

	err = ice_eswitch_cfg_vsi(vsi, repr->parent_mac);
	if (err)
		dev_err(ice_pf_to_dev(pf), "Failed to update VSI of port representor %d",
			repr->id);

	/* The VSI number is different, reload the PR with new id */
	if (repr->id != vsi->vsi_num) {
		xa_erase(&pf->eswitch.reprs, repr->id);
		repr->id = vsi->vsi_num;
		if (xa_insert(&pf->eswitch.reprs, repr->id, repr, GFP_KERNEL))
			dev_err(ice_pf_to_dev(pf), "Failed to reload port representor %d",
				repr->id);
		*repr_id = repr->id;
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
	struct ice_repr *repr = ice_netdev_to_repr(netdev);
	unsigned int len = skb->len;
	int ret;

	skb_dst_drop(skb);
	dst_hold((struct dst_entry *)repr->dst);
	skb_dst_set(skb, (struct dst_entry *)repr->dst);
	skb->dev = repr->dst->u.port_info.lower_dev;

	ret = dev_queue_xmit(skb);
	ice_repr_inc_tx_stats(repr, len, ret);

	return ret;
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
	struct ice_vsi_vlan_ops *vlan_ops;

	vlan_ops = ice_get_compat_vsi_vlan_ops(uplink_vsi);

	ice_vsi_update_local_lb(uplink_vsi, false);
	ice_vsi_update_security(uplink_vsi, ice_vsi_ctx_clear_allow_override);
	vlan_ops->ena_rx_filtering(uplink_vsi);
	ice_cfg_dflt_vsi(uplink_vsi->port_info, uplink_vsi->idx, false,
			 ICE_FLTR_TX);
	ice_cfg_dflt_vsi(uplink_vsi->port_info, uplink_vsi->idx, false,
			 ICE_FLTR_RX);
	ice_fltr_add_mac_and_broadcast(uplink_vsi,
				       uplink_vsi->port_info->mac.perm_addr,
				       ICE_FWD_TO_VSI);
}

/**
 * ice_eswitch_enable_switchdev - configure eswitch in switchdev mode
 * @pf: pointer to PF structure
 */
static int ice_eswitch_enable_switchdev(struct ice_pf *pf)
{
	struct ice_vsi *uplink_vsi;

	uplink_vsi = ice_get_main_vsi(pf);
	if (!uplink_vsi)
		return -ENODEV;

	if (netif_is_any_bridge_port(uplink_vsi->netdev)) {
		dev_err(ice_pf_to_dev(pf),
			"Uplink port cannot be a bridge port\n");
		return -EINVAL;
	}

	pf->eswitch.uplink_vsi = uplink_vsi;

	if (ice_eswitch_setup_env(pf))
		return -ENODEV;

	if (ice_eswitch_br_offloads_init(pf))
		goto err_br_offloads;

	pf->eswitch.is_running = true;

	return 0;

err_br_offloads:
	ice_eswitch_release_env(pf);
	return -ENODEV;
}

/**
 * ice_eswitch_disable_switchdev - disable eswitch resources
 * @pf: pointer to PF structure
 */
static void ice_eswitch_disable_switchdev(struct ice_pf *pf)
{
	ice_eswitch_br_offloads_deinit(pf);
	ice_eswitch_release_env(pf);

	pf->eswitch.is_running = false;
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
		xa_init(&pf->eswitch.reprs);
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
	ice_eswitch_stop_all_tx_queues(pf);
}

static void ice_eswitch_start_reprs(struct ice_pf *pf)
{
	ice_eswitch_start_all_tx_queues(pf);
}

static int
ice_eswitch_attach(struct ice_pf *pf, struct ice_repr *repr, unsigned long *id)
{
	int err;

	if (pf->eswitch_mode == DEVLINK_ESWITCH_MODE_LEGACY)
		return 0;

	if (xa_empty(&pf->eswitch.reprs)) {
		err = ice_eswitch_enable_switchdev(pf);
		if (err)
			return err;
	}

	ice_eswitch_stop_reprs(pf);

	err = repr->ops.add(repr);
	if (err)
		goto err_create_repr;

	err = ice_eswitch_setup_repr(pf, repr);
	if (err)
		goto err_setup_repr;

	err = xa_insert(&pf->eswitch.reprs, repr->id, repr, GFP_KERNEL);
	if (err)
		goto err_xa_alloc;

	*id = repr->id;

	ice_eswitch_start_reprs(pf);

	return 0;

err_xa_alloc:
	ice_eswitch_release_repr(pf, repr);
err_setup_repr:
	repr->ops.rem(repr);
err_create_repr:
	if (xa_empty(&pf->eswitch.reprs))
		ice_eswitch_disable_switchdev(pf);
	ice_eswitch_start_reprs(pf);

	return err;
}

/**
 * ice_eswitch_attach_vf - attach VF to a eswitch
 * @pf: pointer to PF structure
 * @vf: pointer to VF structure to be attached
 *
 * During attaching port representor for VF is created.
 *
 * Return: zero on success or an error code on failure.
 */
int ice_eswitch_attach_vf(struct ice_pf *pf, struct ice_vf *vf)
{
	struct ice_repr *repr = ice_repr_create_vf(vf);
	struct devlink *devlink = priv_to_devlink(pf);
	int err;

	if (IS_ERR(repr))
		return PTR_ERR(repr);

	devl_lock(devlink);
	err = ice_eswitch_attach(pf, repr, &vf->repr_id);
	if (err)
		ice_repr_destroy(repr);
	devl_unlock(devlink);

	return err;
}

/**
 * ice_eswitch_attach_sf - attach SF to a eswitch
 * @pf: pointer to PF structure
 * @sf: pointer to SF structure to be attached
 *
 * During attaching port representor for SF is created.
 *
 * Return: zero on success or an error code on failure.
 */
int ice_eswitch_attach_sf(struct ice_pf *pf, struct ice_dynamic_port *sf)
{
	struct ice_repr *repr = ice_repr_create_sf(sf);
	int err;

	if (IS_ERR(repr))
		return PTR_ERR(repr);

	err = ice_eswitch_attach(pf, repr, &sf->repr_id);
	if (err)
		ice_repr_destroy(repr);

	return err;
}

static void ice_eswitch_detach(struct ice_pf *pf, struct ice_repr *repr)
{
	ice_eswitch_stop_reprs(pf);
	repr->ops.rem(repr);

	xa_erase(&pf->eswitch.reprs, repr->id);

	if (xa_empty(&pf->eswitch.reprs))
		ice_eswitch_disable_switchdev(pf);

	ice_eswitch_release_repr(pf, repr);
	ice_repr_destroy(repr);

	if (xa_empty(&pf->eswitch.reprs)) {
		struct devlink *devlink = priv_to_devlink(pf);

		/* since all port representors are destroyed, there is
		 * no point in keeping the nodes
		 */
		ice_devlink_rate_clear_tx_topology(ice_get_main_vsi(pf));
		devl_rate_nodes_destroy(devlink);
	} else {
		ice_eswitch_start_reprs(pf);
	}
}

/**
 * ice_eswitch_detach_vf - detach VF from a eswitch
 * @pf: pointer to PF structure
 * @vf: pointer to VF structure to be detached
 */
void ice_eswitch_detach_vf(struct ice_pf *pf, struct ice_vf *vf)
{
	struct ice_repr *repr = xa_load(&pf->eswitch.reprs, vf->repr_id);
	struct devlink *devlink = priv_to_devlink(pf);

	if (!repr)
		return;

	devl_lock(devlink);
	ice_eswitch_detach(pf, repr);
	devl_unlock(devlink);
}

/**
 * ice_eswitch_detach_sf - detach SF from a eswitch
 * @pf: pointer to PF structure
 * @sf: pointer to SF structure to be detached
 */
void ice_eswitch_detach_sf(struct ice_pf *pf, struct ice_dynamic_port *sf)
{
	struct ice_repr *repr = xa_load(&pf->eswitch.reprs, sf->repr_id);

	if (!repr)
		return;

	ice_eswitch_detach(pf, repr);
}

/**
 * ice_eswitch_get_target - get netdev based on src_vsi from descriptor
 * @rx_ring: ring used to receive the packet
 * @rx_desc: descriptor used to get src_vsi value
 *
 * Get src_vsi value from descriptor and load correct representor. If it isn't
 * found return rx_ring->netdev.
 */
struct net_device *ice_eswitch_get_target(struct ice_rx_ring *rx_ring,
					  union ice_32b_rx_flex_desc *rx_desc)
{
	struct ice_eswitch *eswitch = &rx_ring->vsi->back->eswitch;
	struct ice_32b_rx_flex_desc_nic_2 *desc;
	struct ice_repr *repr;

	desc = (struct ice_32b_rx_flex_desc_nic_2 *)rx_desc;
	repr = xa_load(&eswitch->reprs, le16_to_cpu(desc->src_vsi));
	if (!repr)
		return rx_ring->netdev;

	return repr->netdev;
}
