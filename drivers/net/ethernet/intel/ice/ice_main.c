// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018-2023, Intel Corporation. */

/* Intel(R) Ethernet Connection E800 Series Linux Driver */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <generated/utsrelease.h>
#include <linux/crash_dump.h>
#include "ice.h"
#include "ice_base.h"
#include "ice_lib.h"
#include "ice_fltr.h"
#include "ice_dcb_lib.h"
#include "ice_dcb_nl.h"
#include "devlink/devlink.h"
#include "devlink/devlink_port.h"
#include "ice_hwmon.h"
/* Including ice_trace.h with CREATE_TRACE_POINTS defined will generate the
 * ice tracepoint functions. This must be done exactly once across the
 * ice driver.
 */
#define CREATE_TRACE_POINTS
#include "ice_trace.h"
#include "ice_eswitch.h"
#include "ice_tc_lib.h"
#include "ice_vsi_vlan_ops.h"
#include <net/xdp_sock_drv.h>

#define DRV_SUMMARY	"Intel(R) Ethernet Connection E800 Series Linux Driver"
static const char ice_driver_string[] = DRV_SUMMARY;
static const char ice_copyright[] = "Copyright (c) 2018, Intel Corporation.";

/* DDP Package file located in firmware search paths (e.g. /lib/firmware/) */
#define ICE_DDP_PKG_PATH	"intel/ice/ddp/"
#define ICE_DDP_PKG_FILE	ICE_DDP_PKG_PATH "ice.pkg"

MODULE_DESCRIPTION(DRV_SUMMARY);
MODULE_IMPORT_NS(LIBIE);
MODULE_LICENSE("GPL v2");
MODULE_FIRMWARE(ICE_DDP_PKG_FILE);

static int debug = -1;
module_param(debug, int, 0644);
#ifndef CONFIG_DYNAMIC_DEBUG
MODULE_PARM_DESC(debug, "netif level (0=none,...,16=all), hw debug_mask (0x8XXXXXXX)");
#else
MODULE_PARM_DESC(debug, "netif level (0=none,...,16=all)");
#endif /* !CONFIG_DYNAMIC_DEBUG */

DEFINE_STATIC_KEY_FALSE(ice_xdp_locking_key);
EXPORT_SYMBOL(ice_xdp_locking_key);

/**
 * ice_hw_to_dev - Get device pointer from the hardware structure
 * @hw: pointer to the device HW structure
 *
 * Used to access the device pointer from compilation units which can't easily
 * include the definition of struct ice_pf without leading to circular header
 * dependencies.
 */
struct device *ice_hw_to_dev(struct ice_hw *hw)
{
	struct ice_pf *pf = container_of(hw, struct ice_pf, hw);

	return &pf->pdev->dev;
}

static struct workqueue_struct *ice_wq;
struct workqueue_struct *ice_lag_wq;
static const struct net_device_ops ice_netdev_safe_mode_ops;
static const struct net_device_ops ice_netdev_ops;

static void ice_rebuild(struct ice_pf *pf, enum ice_reset_req reset_type);

static void ice_vsi_release_all(struct ice_pf *pf);

static int ice_rebuild_channels(struct ice_pf *pf);
static void ice_remove_q_channels(struct ice_vsi *vsi, bool rem_adv_fltr);

static int
ice_indr_setup_tc_cb(struct net_device *netdev, struct Qdisc *sch,
		     void *cb_priv, enum tc_setup_type type, void *type_data,
		     void *data,
		     void (*cleanup)(struct flow_block_cb *block_cb));

bool netif_is_ice(const struct net_device *dev)
{
	return dev && (dev->netdev_ops == &ice_netdev_ops);
}

/**
 * ice_get_tx_pending - returns number of Tx descriptors not processed
 * @ring: the ring of descriptors
 */
static u16 ice_get_tx_pending(struct ice_tx_ring *ring)
{
	u16 head, tail;

	head = ring->next_to_clean;
	tail = ring->next_to_use;

	if (head != tail)
		return (head < tail) ?
			tail - head : (tail + ring->count - head);
	return 0;
}

/**
 * ice_check_for_hang_subtask - check for and recover hung queues
 * @pf: pointer to PF struct
 */
static void ice_check_for_hang_subtask(struct ice_pf *pf)
{
	struct ice_vsi *vsi = NULL;
	struct ice_hw *hw;
	unsigned int i;
	int packets;
	u32 v;

	ice_for_each_vsi(pf, v)
		if (pf->vsi[v] && pf->vsi[v]->type == ICE_VSI_PF) {
			vsi = pf->vsi[v];
			break;
		}

	if (!vsi || test_bit(ICE_VSI_DOWN, vsi->state))
		return;

	if (!(vsi->netdev && netif_carrier_ok(vsi->netdev)))
		return;

	hw = &vsi->back->hw;

	ice_for_each_txq(vsi, i) {
		struct ice_tx_ring *tx_ring = vsi->tx_rings[i];
		struct ice_ring_stats *ring_stats;

		if (!tx_ring)
			continue;
		if (ice_ring_ch_enabled(tx_ring))
			continue;

		ring_stats = tx_ring->ring_stats;
		if (!ring_stats)
			continue;

		if (tx_ring->desc) {
			/* If packet counter has not changed the queue is
			 * likely stalled, so force an interrupt for this
			 * queue.
			 *
			 * prev_pkt would be negative if there was no
			 * pending work.
			 */
			packets = ring_stats->stats.pkts & INT_MAX;
			if (ring_stats->tx_stats.prev_pkt == packets) {
				/* Trigger sw interrupt to revive the queue */
				ice_trigger_sw_intr(hw, tx_ring->q_vector);
				continue;
			}

			/* Memory barrier between read of packet count and call
			 * to ice_get_tx_pending()
			 */
			smp_rmb();
			ring_stats->tx_stats.prev_pkt =
			    ice_get_tx_pending(tx_ring) ? packets : -1;
		}
	}
}

/**
 * ice_init_mac_fltr - Set initial MAC filters
 * @pf: board private structure
 *
 * Set initial set of MAC filters for PF VSI; configure filters for permanent
 * address and broadcast address. If an error is encountered, netdevice will be
 * unregistered.
 */
static int ice_init_mac_fltr(struct ice_pf *pf)
{
	struct ice_vsi *vsi;
	u8 *perm_addr;

	vsi = ice_get_main_vsi(pf);
	if (!vsi)
		return -EINVAL;

	perm_addr = vsi->port_info->mac.perm_addr;
	return ice_fltr_add_mac_and_broadcast(vsi, perm_addr, ICE_FWD_TO_VSI);
}

/**
 * ice_add_mac_to_sync_list - creates list of MAC addresses to be synced
 * @netdev: the net device on which the sync is happening
 * @addr: MAC address to sync
 *
 * This is a callback function which is called by the in kernel device sync
 * functions (like __dev_uc_sync, __dev_mc_sync, etc). This function only
 * populates the tmp_sync_list, which is later used by ice_add_mac to add the
 * MAC filters from the hardware.
 */
static int ice_add_mac_to_sync_list(struct net_device *netdev, const u8 *addr)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;

	if (ice_fltr_add_mac_to_list(vsi, &vsi->tmp_sync_list, addr,
				     ICE_FWD_TO_VSI))
		return -EINVAL;

	return 0;
}

/**
 * ice_add_mac_to_unsync_list - creates list of MAC addresses to be unsynced
 * @netdev: the net device on which the unsync is happening
 * @addr: MAC address to unsync
 *
 * This is a callback function which is called by the in kernel device unsync
 * functions (like __dev_uc_unsync, __dev_mc_unsync, etc). This function only
 * populates the tmp_unsync_list, which is later used by ice_remove_mac to
 * delete the MAC filters from the hardware.
 */
static int ice_add_mac_to_unsync_list(struct net_device *netdev, const u8 *addr)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;

	/* Under some circumstances, we might receive a request to delete our
	 * own device address from our uc list. Because we store the device
	 * address in the VSI's MAC filter list, we need to ignore such
	 * requests and not delete our device address from this list.
	 */
	if (ether_addr_equal(addr, netdev->dev_addr))
		return 0;

	if (ice_fltr_add_mac_to_list(vsi, &vsi->tmp_unsync_list, addr,
				     ICE_FWD_TO_VSI))
		return -EINVAL;

	return 0;
}

/**
 * ice_vsi_fltr_changed - check if filter state changed
 * @vsi: VSI to be checked
 *
 * returns true if filter state has changed, false otherwise.
 */
static bool ice_vsi_fltr_changed(struct ice_vsi *vsi)
{
	return test_bit(ICE_VSI_UMAC_FLTR_CHANGED, vsi->state) ||
	       test_bit(ICE_VSI_MMAC_FLTR_CHANGED, vsi->state);
}

/**
 * ice_set_promisc - Enable promiscuous mode for a given PF
 * @vsi: the VSI being configured
 * @promisc_m: mask of promiscuous config bits
 *
 */
static int ice_set_promisc(struct ice_vsi *vsi, u8 promisc_m)
{
	int status;

	if (vsi->type != ICE_VSI_PF)
		return 0;

	if (ice_vsi_has_non_zero_vlans(vsi)) {
		promisc_m |= (ICE_PROMISC_VLAN_RX | ICE_PROMISC_VLAN_TX);
		status = ice_fltr_set_vlan_vsi_promisc(&vsi->back->hw, vsi,
						       promisc_m);
	} else {
		status = ice_fltr_set_vsi_promisc(&vsi->back->hw, vsi->idx,
						  promisc_m, 0);
	}
	if (status && status != -EEXIST)
		return status;

	netdev_dbg(vsi->netdev, "set promisc filter bits for VSI %i: 0x%x\n",
		   vsi->vsi_num, promisc_m);
	return 0;
}

/**
 * ice_clear_promisc - Disable promiscuous mode for a given PF
 * @vsi: the VSI being configured
 * @promisc_m: mask of promiscuous config bits
 *
 */
static int ice_clear_promisc(struct ice_vsi *vsi, u8 promisc_m)
{
	int status;

	if (vsi->type != ICE_VSI_PF)
		return 0;

	if (ice_vsi_has_non_zero_vlans(vsi)) {
		promisc_m |= (ICE_PROMISC_VLAN_RX | ICE_PROMISC_VLAN_TX);
		status = ice_fltr_clear_vlan_vsi_promisc(&vsi->back->hw, vsi,
							 promisc_m);
	} else {
		status = ice_fltr_clear_vsi_promisc(&vsi->back->hw, vsi->idx,
						    promisc_m, 0);
	}

	netdev_dbg(vsi->netdev, "clear promisc filter bits for VSI %i: 0x%x\n",
		   vsi->vsi_num, promisc_m);
	return status;
}

/**
 * ice_vsi_sync_fltr - Update the VSI filter list to the HW
 * @vsi: ptr to the VSI
 *
 * Push any outstanding VSI filter changes through the AdminQ.
 */
static int ice_vsi_sync_fltr(struct ice_vsi *vsi)
{
	struct ice_vsi_vlan_ops *vlan_ops = ice_get_compat_vsi_vlan_ops(vsi);
	struct device *dev = ice_pf_to_dev(vsi->back);
	struct net_device *netdev = vsi->netdev;
	bool promisc_forced_on = false;
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	u32 changed_flags = 0;
	int err;

	if (!vsi->netdev)
		return -EINVAL;

	while (test_and_set_bit(ICE_CFG_BUSY, vsi->state))
		usleep_range(1000, 2000);

	changed_flags = vsi->current_netdev_flags ^ vsi->netdev->flags;
	vsi->current_netdev_flags = vsi->netdev->flags;

	INIT_LIST_HEAD(&vsi->tmp_sync_list);
	INIT_LIST_HEAD(&vsi->tmp_unsync_list);

	if (ice_vsi_fltr_changed(vsi)) {
		clear_bit(ICE_VSI_UMAC_FLTR_CHANGED, vsi->state);
		clear_bit(ICE_VSI_MMAC_FLTR_CHANGED, vsi->state);

		/* grab the netdev's addr_list_lock */
		netif_addr_lock_bh(netdev);
		__dev_uc_sync(netdev, ice_add_mac_to_sync_list,
			      ice_add_mac_to_unsync_list);
		__dev_mc_sync(netdev, ice_add_mac_to_sync_list,
			      ice_add_mac_to_unsync_list);
		/* our temp lists are populated. release lock */
		netif_addr_unlock_bh(netdev);
	}

	/* Remove MAC addresses in the unsync list */
	err = ice_fltr_remove_mac_list(vsi, &vsi->tmp_unsync_list);
	ice_fltr_free_list(dev, &vsi->tmp_unsync_list);
	if (err) {
		netdev_err(netdev, "Failed to delete MAC filters\n");
		/* if we failed because of alloc failures, just bail */
		if (err == -ENOMEM)
			goto out;
	}

	/* Add MAC addresses in the sync list */
	err = ice_fltr_add_mac_list(vsi, &vsi->tmp_sync_list);
	ice_fltr_free_list(dev, &vsi->tmp_sync_list);
	/* If filter is added successfully or already exists, do not go into
	 * 'if' condition and report it as error. Instead continue processing
	 * rest of the function.
	 */
	if (err && err != -EEXIST) {
		netdev_err(netdev, "Failed to add MAC filters\n");
		/* If there is no more space for new umac filters, VSI
		 * should go into promiscuous mode. There should be some
		 * space reserved for promiscuous filters.
		 */
		if (hw->adminq.sq_last_status == ICE_AQ_RC_ENOSPC &&
		    !test_and_set_bit(ICE_FLTR_OVERFLOW_PROMISC,
				      vsi->state)) {
			promisc_forced_on = true;
			netdev_warn(netdev, "Reached MAC filter limit, forcing promisc mode on VSI %d\n",
				    vsi->vsi_num);
		} else {
			goto out;
		}
	}
	err = 0;
	/* check for changes in promiscuous modes */
	if (changed_flags & IFF_ALLMULTI) {
		if (vsi->current_netdev_flags & IFF_ALLMULTI) {
			err = ice_set_promisc(vsi, ICE_MCAST_PROMISC_BITS);
			if (err) {
				vsi->current_netdev_flags &= ~IFF_ALLMULTI;
				goto out_promisc;
			}
		} else {
			/* !(vsi->current_netdev_flags & IFF_ALLMULTI) */
			err = ice_clear_promisc(vsi, ICE_MCAST_PROMISC_BITS);
			if (err) {
				vsi->current_netdev_flags |= IFF_ALLMULTI;
				goto out_promisc;
			}
		}
	}

	if (((changed_flags & IFF_PROMISC) || promisc_forced_on) ||
	    test_bit(ICE_VSI_PROMISC_CHANGED, vsi->state)) {
		clear_bit(ICE_VSI_PROMISC_CHANGED, vsi->state);
		if (vsi->current_netdev_flags & IFF_PROMISC) {
			/* Apply Rx filter rule to get traffic from wire */
			if (!ice_is_dflt_vsi_in_use(vsi->port_info)) {
				err = ice_set_dflt_vsi(vsi);
				if (err && err != -EEXIST) {
					netdev_err(netdev, "Error %d setting default VSI %i Rx rule\n",
						   err, vsi->vsi_num);
					vsi->current_netdev_flags &=
						~IFF_PROMISC;
					goto out_promisc;
				}
				err = 0;
				vlan_ops->dis_rx_filtering(vsi);

				/* promiscuous mode implies allmulticast so
				 * that VSIs that are in promiscuous mode are
				 * subscribed to multicast packets coming to
				 * the port
				 */
				err = ice_set_promisc(vsi,
						      ICE_MCAST_PROMISC_BITS);
				if (err)
					goto out_promisc;
			}
		} else {
			/* Clear Rx filter to remove traffic from wire */
			if (ice_is_vsi_dflt_vsi(vsi)) {
				err = ice_clear_dflt_vsi(vsi);
				if (err) {
					netdev_err(netdev, "Error %d clearing default VSI %i Rx rule\n",
						   err, vsi->vsi_num);
					vsi->current_netdev_flags |=
						IFF_PROMISC;
					goto out_promisc;
				}
				if (vsi->netdev->features &
				    NETIF_F_HW_VLAN_CTAG_FILTER)
					vlan_ops->ena_rx_filtering(vsi);
			}

			/* disable allmulti here, but only if allmulti is not
			 * still enabled for the netdev
			 */
			if (!(vsi->current_netdev_flags & IFF_ALLMULTI)) {
				err = ice_clear_promisc(vsi,
							ICE_MCAST_PROMISC_BITS);
				if (err) {
					netdev_err(netdev, "Error %d clearing multicast promiscuous on VSI %i\n",
						   err, vsi->vsi_num);
				}
			}
		}
	}
	goto exit;

out_promisc:
	set_bit(ICE_VSI_PROMISC_CHANGED, vsi->state);
	goto exit;
out:
	/* if something went wrong then set the changed flag so we try again */
	set_bit(ICE_VSI_UMAC_FLTR_CHANGED, vsi->state);
	set_bit(ICE_VSI_MMAC_FLTR_CHANGED, vsi->state);
exit:
	clear_bit(ICE_CFG_BUSY, vsi->state);
	return err;
}

/**
 * ice_sync_fltr_subtask - Sync the VSI filter list with HW
 * @pf: board private structure
 */
static void ice_sync_fltr_subtask(struct ice_pf *pf)
{
	int v;

	if (!pf || !(test_bit(ICE_FLAG_FLTR_SYNC, pf->flags)))
		return;

	clear_bit(ICE_FLAG_FLTR_SYNC, pf->flags);

	ice_for_each_vsi(pf, v)
		if (pf->vsi[v] && ice_vsi_fltr_changed(pf->vsi[v]) &&
		    ice_vsi_sync_fltr(pf->vsi[v])) {
			/* come back and try again later */
			set_bit(ICE_FLAG_FLTR_SYNC, pf->flags);
			break;
		}
}

/**
 * ice_pf_dis_all_vsi - Pause all VSIs on a PF
 * @pf: the PF
 * @locked: is the rtnl_lock already held
 */
static void ice_pf_dis_all_vsi(struct ice_pf *pf, bool locked)
{
	int node;
	int v;

	ice_for_each_vsi(pf, v)
		if (pf->vsi[v])
			ice_dis_vsi(pf->vsi[v], locked);

	for (node = 0; node < ICE_MAX_PF_AGG_NODES; node++)
		pf->pf_agg_node[node].num_vsis = 0;

	for (node = 0; node < ICE_MAX_VF_AGG_NODES; node++)
		pf->vf_agg_node[node].num_vsis = 0;
}

/**
 * ice_clear_sw_switch_recipes - clear switch recipes
 * @pf: board private structure
 *
 * Mark switch recipes as not created in sw structures. There are cases where
 * rules (especially advanced rules) need to be restored, either re-read from
 * hardware or added again. For example after the reset. 'recp_created' flag
 * prevents from doing that and need to be cleared upfront.
 */
static void ice_clear_sw_switch_recipes(struct ice_pf *pf)
{
	struct ice_sw_recipe *recp;
	u8 i;

	recp = pf->hw.switch_info->recp_list;
	for (i = 0; i < ICE_MAX_NUM_RECIPES; i++)
		recp[i].recp_created = false;
}

/**
 * ice_prepare_for_reset - prep for reset
 * @pf: board private structure
 * @reset_type: reset type requested
 *
 * Inform or close all dependent features in prep for reset.
 */
static void
ice_prepare_for_reset(struct ice_pf *pf, enum ice_reset_req reset_type)
{
	struct ice_hw *hw = &pf->hw;
	struct ice_vsi *vsi;
	struct ice_vf *vf;
	unsigned int bkt;

	dev_dbg(ice_pf_to_dev(pf), "reset_type=%d\n", reset_type);

	/* already prepared for reset */
	if (test_bit(ICE_PREPARED_FOR_RESET, pf->state))
		return;

	synchronize_irq(pf->oicr_irq.virq);

	ice_unplug_aux_dev(pf);

	/* Notify VFs of impending reset */
	if (ice_check_sq_alive(hw, &hw->mailboxq))
		ice_vc_notify_reset(pf);

	/* Disable VFs until reset is completed */
	mutex_lock(&pf->vfs.table_lock);
	ice_for_each_vf(pf, bkt, vf)
		ice_set_vf_state_dis(vf);
	mutex_unlock(&pf->vfs.table_lock);

	if (ice_is_eswitch_mode_switchdev(pf)) {
		if (reset_type != ICE_RESET_PFR)
			ice_clear_sw_switch_recipes(pf);
	}

	/* release ADQ specific HW and SW resources */
	vsi = ice_get_main_vsi(pf);
	if (!vsi)
		goto skip;

	/* to be on safe side, reset orig_rss_size so that normal flow
	 * of deciding rss_size can take precedence
	 */
	vsi->orig_rss_size = 0;

	if (test_bit(ICE_FLAG_TC_MQPRIO, pf->flags)) {
		if (reset_type == ICE_RESET_PFR) {
			vsi->old_ena_tc = vsi->all_enatc;
			vsi->old_numtc = vsi->all_numtc;
		} else {
			ice_remove_q_channels(vsi, true);

			/* for other reset type, do not support channel rebuild
			 * hence reset needed info
			 */
			vsi->old_ena_tc = 0;
			vsi->all_enatc = 0;
			vsi->old_numtc = 0;
			vsi->all_numtc = 0;
			vsi->req_txq = 0;
			vsi->req_rxq = 0;
			clear_bit(ICE_FLAG_TC_MQPRIO, pf->flags);
			memset(&vsi->mqprio_qopt, 0, sizeof(vsi->mqprio_qopt));
		}
	}
skip:

	/* clear SW filtering DB */
	ice_clear_hw_tbls(hw);
	/* disable the VSIs and their queues that are not already DOWN */
	ice_pf_dis_all_vsi(pf, false);

	if (test_bit(ICE_FLAG_PTP_SUPPORTED, pf->flags))
		ice_ptp_prepare_for_reset(pf, reset_type);

	if (ice_is_feature_supported(pf, ICE_F_GNSS))
		ice_gnss_exit(pf);

	if (hw->port_info)
		ice_sched_clear_port(hw->port_info);

	ice_shutdown_all_ctrlq(hw, false);

	set_bit(ICE_PREPARED_FOR_RESET, pf->state);
}

/**
 * ice_do_reset - Initiate one of many types of resets
 * @pf: board private structure
 * @reset_type: reset type requested before this function was called.
 */
static void ice_do_reset(struct ice_pf *pf, enum ice_reset_req reset_type)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;

	dev_dbg(dev, "reset_type 0x%x requested\n", reset_type);

	if (pf->lag && pf->lag->bonded && reset_type == ICE_RESET_PFR) {
		dev_dbg(dev, "PFR on a bonded interface, promoting to CORER\n");
		reset_type = ICE_RESET_CORER;
	}

	ice_prepare_for_reset(pf, reset_type);

	/* trigger the reset */
	if (ice_reset(hw, reset_type)) {
		dev_err(dev, "reset %d failed\n", reset_type);
		set_bit(ICE_RESET_FAILED, pf->state);
		clear_bit(ICE_RESET_OICR_RECV, pf->state);
		clear_bit(ICE_PREPARED_FOR_RESET, pf->state);
		clear_bit(ICE_PFR_REQ, pf->state);
		clear_bit(ICE_CORER_REQ, pf->state);
		clear_bit(ICE_GLOBR_REQ, pf->state);
		wake_up(&pf->reset_wait_queue);
		return;
	}

	/* PFR is a bit of a special case because it doesn't result in an OICR
	 * interrupt. So for PFR, rebuild after the reset and clear the reset-
	 * associated state bits.
	 */
	if (reset_type == ICE_RESET_PFR) {
		pf->pfr_count++;
		ice_rebuild(pf, reset_type);
		clear_bit(ICE_PREPARED_FOR_RESET, pf->state);
		clear_bit(ICE_PFR_REQ, pf->state);
		wake_up(&pf->reset_wait_queue);
		ice_reset_all_vfs(pf);
	}
}

/**
 * ice_reset_subtask - Set up for resetting the device and driver
 * @pf: board private structure
 */
static void ice_reset_subtask(struct ice_pf *pf)
{
	enum ice_reset_req reset_type = ICE_RESET_INVAL;

	/* When a CORER/GLOBR/EMPR is about to happen, the hardware triggers an
	 * OICR interrupt. The OICR handler (ice_misc_intr) determines what type
	 * of reset is pending and sets bits in pf->state indicating the reset
	 * type and ICE_RESET_OICR_RECV. So, if the latter bit is set
	 * prepare for pending reset if not already (for PF software-initiated
	 * global resets the software should already be prepared for it as
	 * indicated by ICE_PREPARED_FOR_RESET; for global resets initiated
	 * by firmware or software on other PFs, that bit is not set so prepare
	 * for the reset now), poll for reset done, rebuild and return.
	 */
	if (test_bit(ICE_RESET_OICR_RECV, pf->state)) {
		/* Perform the largest reset requested */
		if (test_and_clear_bit(ICE_CORER_RECV, pf->state))
			reset_type = ICE_RESET_CORER;
		if (test_and_clear_bit(ICE_GLOBR_RECV, pf->state))
			reset_type = ICE_RESET_GLOBR;
		if (test_and_clear_bit(ICE_EMPR_RECV, pf->state))
			reset_type = ICE_RESET_EMPR;
		/* return if no valid reset type requested */
		if (reset_type == ICE_RESET_INVAL)
			return;
		ice_prepare_for_reset(pf, reset_type);

		/* make sure we are ready to rebuild */
		if (ice_check_reset(&pf->hw)) {
			set_bit(ICE_RESET_FAILED, pf->state);
		} else {
			/* done with reset. start rebuild */
			pf->hw.reset_ongoing = false;
			ice_rebuild(pf, reset_type);
			/* clear bit to resume normal operations, but
			 * ICE_NEEDS_RESTART bit is set in case rebuild failed
			 */
			clear_bit(ICE_RESET_OICR_RECV, pf->state);
			clear_bit(ICE_PREPARED_FOR_RESET, pf->state);
			clear_bit(ICE_PFR_REQ, pf->state);
			clear_bit(ICE_CORER_REQ, pf->state);
			clear_bit(ICE_GLOBR_REQ, pf->state);
			wake_up(&pf->reset_wait_queue);
			ice_reset_all_vfs(pf);
		}

		return;
	}

	/* No pending resets to finish processing. Check for new resets */
	if (test_bit(ICE_PFR_REQ, pf->state)) {
		reset_type = ICE_RESET_PFR;
		if (pf->lag && pf->lag->bonded) {
			dev_dbg(ice_pf_to_dev(pf), "PFR on a bonded interface, promoting to CORER\n");
			reset_type = ICE_RESET_CORER;
		}
	}
	if (test_bit(ICE_CORER_REQ, pf->state))
		reset_type = ICE_RESET_CORER;
	if (test_bit(ICE_GLOBR_REQ, pf->state))
		reset_type = ICE_RESET_GLOBR;
	/* If no valid reset type requested just return */
	if (reset_type == ICE_RESET_INVAL)
		return;

	/* reset if not already down or busy */
	if (!test_bit(ICE_DOWN, pf->state) &&
	    !test_bit(ICE_CFG_BUSY, pf->state)) {
		ice_do_reset(pf, reset_type);
	}
}

/**
 * ice_print_topo_conflict - print topology conflict message
 * @vsi: the VSI whose topology status is being checked
 */
static void ice_print_topo_conflict(struct ice_vsi *vsi)
{
	switch (vsi->port_info->phy.link_info.topo_media_conflict) {
	case ICE_AQ_LINK_TOPO_CONFLICT:
	case ICE_AQ_LINK_MEDIA_CONFLICT:
	case ICE_AQ_LINK_TOPO_UNREACH_PRT:
	case ICE_AQ_LINK_TOPO_UNDRUTIL_PRT:
	case ICE_AQ_LINK_TOPO_UNDRUTIL_MEDIA:
		netdev_info(vsi->netdev, "Potential misconfiguration of the Ethernet port detected. If it was not intended, please use the Intel (R) Ethernet Port Configuration Tool to address the issue.\n");
		break;
	case ICE_AQ_LINK_TOPO_UNSUPP_MEDIA:
		if (test_bit(ICE_FLAG_LINK_LENIENT_MODE_ENA, vsi->back->flags))
			netdev_warn(vsi->netdev, "An unsupported module type was detected. Refer to the Intel(R) Ethernet Adapters and Devices User Guide for a list of supported modules\n");
		else
			netdev_err(vsi->netdev, "Rx/Tx is disabled on this device because an unsupported module type was detected. Refer to the Intel(R) Ethernet Adapters and Devices User Guide for a list of supported modules.\n");
		break;
	default:
		break;
	}
}

/**
 * ice_print_link_msg - print link up or down message
 * @vsi: the VSI whose link status is being queried
 * @isup: boolean for if the link is now up or down
 */
void ice_print_link_msg(struct ice_vsi *vsi, bool isup)
{
	struct ice_aqc_get_phy_caps_data *caps;
	const char *an_advertised;
	const char *fec_req;
	const char *speed;
	const char *fec;
	const char *fc;
	const char *an;
	int status;

	if (!vsi)
		return;

	if (vsi->current_isup == isup)
		return;

	vsi->current_isup = isup;

	if (!isup) {
		netdev_info(vsi->netdev, "NIC Link is Down\n");
		return;
	}

	switch (vsi->port_info->phy.link_info.link_speed) {
	case ICE_AQ_LINK_SPEED_200GB:
		speed = "200 G";
		break;
	case ICE_AQ_LINK_SPEED_100GB:
		speed = "100 G";
		break;
	case ICE_AQ_LINK_SPEED_50GB:
		speed = "50 G";
		break;
	case ICE_AQ_LINK_SPEED_40GB:
		speed = "40 G";
		break;
	case ICE_AQ_LINK_SPEED_25GB:
		speed = "25 G";
		break;
	case ICE_AQ_LINK_SPEED_20GB:
		speed = "20 G";
		break;
	case ICE_AQ_LINK_SPEED_10GB:
		speed = "10 G";
		break;
	case ICE_AQ_LINK_SPEED_5GB:
		speed = "5 G";
		break;
	case ICE_AQ_LINK_SPEED_2500MB:
		speed = "2.5 G";
		break;
	case ICE_AQ_LINK_SPEED_1000MB:
		speed = "1 G";
		break;
	case ICE_AQ_LINK_SPEED_100MB:
		speed = "100 M";
		break;
	default:
		speed = "Unknown ";
		break;
	}

	switch (vsi->port_info->fc.current_mode) {
	case ICE_FC_FULL:
		fc = "Rx/Tx";
		break;
	case ICE_FC_TX_PAUSE:
		fc = "Tx";
		break;
	case ICE_FC_RX_PAUSE:
		fc = "Rx";
		break;
	case ICE_FC_NONE:
		fc = "None";
		break;
	default:
		fc = "Unknown";
		break;
	}

	/* Get FEC mode based on negotiated link info */
	switch (vsi->port_info->phy.link_info.fec_info) {
	case ICE_AQ_LINK_25G_RS_528_FEC_EN:
	case ICE_AQ_LINK_25G_RS_544_FEC_EN:
		fec = "RS-FEC";
		break;
	case ICE_AQ_LINK_25G_KR_FEC_EN:
		fec = "FC-FEC/BASE-R";
		break;
	default:
		fec = "NONE";
		break;
	}

	/* check if autoneg completed, might be false due to not supported */
	if (vsi->port_info->phy.link_info.an_info & ICE_AQ_AN_COMPLETED)
		an = "True";
	else
		an = "False";

	/* Get FEC mode requested based on PHY caps last SW configuration */
	caps = kzalloc(sizeof(*caps), GFP_KERNEL);
	if (!caps) {
		fec_req = "Unknown";
		an_advertised = "Unknown";
		goto done;
	}

	status = ice_aq_get_phy_caps(vsi->port_info, false,
				     ICE_AQC_REPORT_ACTIVE_CFG, caps, NULL);
	if (status)
		netdev_info(vsi->netdev, "Get phy capability failed.\n");

	an_advertised = ice_is_phy_caps_an_enabled(caps) ? "On" : "Off";

	if (caps->link_fec_options & ICE_AQC_PHY_FEC_25G_RS_528_REQ ||
	    caps->link_fec_options & ICE_AQC_PHY_FEC_25G_RS_544_REQ)
		fec_req = "RS-FEC";
	else if (caps->link_fec_options & ICE_AQC_PHY_FEC_10G_KR_40G_KR4_REQ ||
		 caps->link_fec_options & ICE_AQC_PHY_FEC_25G_KR_REQ)
		fec_req = "FC-FEC/BASE-R";
	else
		fec_req = "NONE";

	kfree(caps);

done:
	netdev_info(vsi->netdev, "NIC Link is up %sbps Full Duplex, Requested FEC: %s, Negotiated FEC: %s, Autoneg Advertised: %s, Autoneg Negotiated: %s, Flow Control: %s\n",
		    speed, fec_req, fec, an_advertised, an, fc);
	ice_print_topo_conflict(vsi);
}

/**
 * ice_vsi_link_event - update the VSI's netdev
 * @vsi: the VSI on which the link event occurred
 * @link_up: whether or not the VSI needs to be set up or down
 */
static void ice_vsi_link_event(struct ice_vsi *vsi, bool link_up)
{
	if (!vsi)
		return;

	if (test_bit(ICE_VSI_DOWN, vsi->state) || !vsi->netdev)
		return;

	if (vsi->type == ICE_VSI_PF) {
		if (link_up == netif_carrier_ok(vsi->netdev))
			return;

		if (link_up) {
			netif_carrier_on(vsi->netdev);
			netif_tx_wake_all_queues(vsi->netdev);
		} else {
			netif_carrier_off(vsi->netdev);
			netif_tx_stop_all_queues(vsi->netdev);
		}
	}
}

/**
 * ice_set_dflt_mib - send a default config MIB to the FW
 * @pf: private PF struct
 *
 * This function sends a default configuration MIB to the FW.
 *
 * If this function errors out at any point, the driver is still able to
 * function.  The main impact is that LFC may not operate as expected.
 * Therefore an error state in this function should be treated with a DBG
 * message and continue on with driver rebuild/reenable.
 */
static void ice_set_dflt_mib(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	u8 mib_type, *buf, *lldpmib = NULL;
	u16 len, typelen, offset = 0;
	struct ice_lldp_org_tlv *tlv;
	struct ice_hw *hw = &pf->hw;
	u32 ouisubtype;

	mib_type = SET_LOCAL_MIB_TYPE_LOCAL_MIB;
	lldpmib = kzalloc(ICE_LLDPDU_SIZE, GFP_KERNEL);
	if (!lldpmib) {
		dev_dbg(dev, "%s Failed to allocate MIB memory\n",
			__func__);
		return;
	}

	/* Add ETS CFG TLV */
	tlv = (struct ice_lldp_org_tlv *)lldpmib;
	typelen = ((ICE_TLV_TYPE_ORG << ICE_LLDP_TLV_TYPE_S) |
		   ICE_IEEE_ETS_TLV_LEN);
	tlv->typelen = htons(typelen);
	ouisubtype = ((ICE_IEEE_8021QAZ_OUI << ICE_LLDP_TLV_OUI_S) |
		      ICE_IEEE_SUBTYPE_ETS_CFG);
	tlv->ouisubtype = htonl(ouisubtype);

	buf = tlv->tlvinfo;
	buf[0] = 0;

	/* ETS CFG all UPs map to TC 0. Next 4 (1 - 4) Octets = 0.
	 * Octets 5 - 12 are BW values, set octet 5 to 100% BW.
	 * Octets 13 - 20 are TSA values - leave as zeros
	 */
	buf[5] = 0x64;
	len = FIELD_GET(ICE_LLDP_TLV_LEN_M, typelen);
	offset += len + 2;
	tlv = (struct ice_lldp_org_tlv *)
		((char *)tlv + sizeof(tlv->typelen) + len);

	/* Add ETS REC TLV */
	buf = tlv->tlvinfo;
	tlv->typelen = htons(typelen);

	ouisubtype = ((ICE_IEEE_8021QAZ_OUI << ICE_LLDP_TLV_OUI_S) |
		      ICE_IEEE_SUBTYPE_ETS_REC);
	tlv->ouisubtype = htonl(ouisubtype);

	/* First octet of buf is reserved
	 * Octets 1 - 4 map UP to TC - all UPs map to zero
	 * Octets 5 - 12 are BW values - set TC 0 to 100%.
	 * Octets 13 - 20 are TSA value - leave as zeros
	 */
	buf[5] = 0x64;
	offset += len + 2;
	tlv = (struct ice_lldp_org_tlv *)
		((char *)tlv + sizeof(tlv->typelen) + len);

	/* Add PFC CFG TLV */
	typelen = ((ICE_TLV_TYPE_ORG << ICE_LLDP_TLV_TYPE_S) |
		   ICE_IEEE_PFC_TLV_LEN);
	tlv->typelen = htons(typelen);

	ouisubtype = ((ICE_IEEE_8021QAZ_OUI << ICE_LLDP_TLV_OUI_S) |
		      ICE_IEEE_SUBTYPE_PFC_CFG);
	tlv->ouisubtype = htonl(ouisubtype);

	/* Octet 1 left as all zeros - PFC disabled */
	buf[0] = 0x08;
	len = FIELD_GET(ICE_LLDP_TLV_LEN_M, typelen);
	offset += len + 2;

	if (ice_aq_set_lldp_mib(hw, mib_type, (void *)lldpmib, offset, NULL))
		dev_dbg(dev, "%s Failed to set default LLDP MIB\n", __func__);

	kfree(lldpmib);
}

/**
 * ice_check_phy_fw_load - check if PHY FW load failed
 * @pf: pointer to PF struct
 * @link_cfg_err: bitmap from the link info structure
 *
 * check if external PHY FW load failed and print an error message if it did
 */
static void ice_check_phy_fw_load(struct ice_pf *pf, u8 link_cfg_err)
{
	if (!(link_cfg_err & ICE_AQ_LINK_EXTERNAL_PHY_LOAD_FAILURE)) {
		clear_bit(ICE_FLAG_PHY_FW_LOAD_FAILED, pf->flags);
		return;
	}

	if (test_bit(ICE_FLAG_PHY_FW_LOAD_FAILED, pf->flags))
		return;

	if (link_cfg_err & ICE_AQ_LINK_EXTERNAL_PHY_LOAD_FAILURE) {
		dev_err(ice_pf_to_dev(pf), "Device failed to load the FW for the external PHY. Please download and install the latest NVM for your device and try again\n");
		set_bit(ICE_FLAG_PHY_FW_LOAD_FAILED, pf->flags);
	}
}

/**
 * ice_check_module_power
 * @pf: pointer to PF struct
 * @link_cfg_err: bitmap from the link info structure
 *
 * check module power level returned by a previous call to aq_get_link_info
 * and print error messages if module power level is not supported
 */
static void ice_check_module_power(struct ice_pf *pf, u8 link_cfg_err)
{
	/* if module power level is supported, clear the flag */
	if (!(link_cfg_err & (ICE_AQ_LINK_INVAL_MAX_POWER_LIMIT |
			      ICE_AQ_LINK_MODULE_POWER_UNSUPPORTED))) {
		clear_bit(ICE_FLAG_MOD_POWER_UNSUPPORTED, pf->flags);
		return;
	}

	/* if ICE_FLAG_MOD_POWER_UNSUPPORTED was previously set and the
	 * above block didn't clear this bit, there's nothing to do
	 */
	if (test_bit(ICE_FLAG_MOD_POWER_UNSUPPORTED, pf->flags))
		return;

	if (link_cfg_err & ICE_AQ_LINK_INVAL_MAX_POWER_LIMIT) {
		dev_err(ice_pf_to_dev(pf), "The installed module is incompatible with the device's NVM image. Cannot start link\n");
		set_bit(ICE_FLAG_MOD_POWER_UNSUPPORTED, pf->flags);
	} else if (link_cfg_err & ICE_AQ_LINK_MODULE_POWER_UNSUPPORTED) {
		dev_err(ice_pf_to_dev(pf), "The module's power requirements exceed the device's power supply. Cannot start link\n");
		set_bit(ICE_FLAG_MOD_POWER_UNSUPPORTED, pf->flags);
	}
}

/**
 * ice_check_link_cfg_err - check if link configuration failed
 * @pf: pointer to the PF struct
 * @link_cfg_err: bitmap from the link info structure
 *
 * print if any link configuration failure happens due to the value in the
 * link_cfg_err parameter in the link info structure
 */
static void ice_check_link_cfg_err(struct ice_pf *pf, u8 link_cfg_err)
{
	ice_check_module_power(pf, link_cfg_err);
	ice_check_phy_fw_load(pf, link_cfg_err);
}

/**
 * ice_link_event - process the link event
 * @pf: PF that the link event is associated with
 * @pi: port_info for the port that the link event is associated with
 * @link_up: true if the physical link is up and false if it is down
 * @link_speed: current link speed received from the link event
 *
 * Returns 0 on success and negative on failure
 */
static int
ice_link_event(struct ice_pf *pf, struct ice_port_info *pi, bool link_up,
	       u16 link_speed)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_phy_info *phy_info;
	struct ice_vsi *vsi;
	u16 old_link_speed;
	bool old_link;
	int status;

	phy_info = &pi->phy;
	phy_info->link_info_old = phy_info->link_info;

	old_link = !!(phy_info->link_info_old.link_info & ICE_AQ_LINK_UP);
	old_link_speed = phy_info->link_info_old.link_speed;

	/* update the link info structures and re-enable link events,
	 * don't bail on failure due to other book keeping needed
	 */
	status = ice_update_link_info(pi);
	if (status)
		dev_dbg(dev, "Failed to update link status on port %d, err %d aq_err %s\n",
			pi->lport, status,
			ice_aq_str(pi->hw->adminq.sq_last_status));

	ice_check_link_cfg_err(pf, pi->phy.link_info.link_cfg_err);

	/* Check if the link state is up after updating link info, and treat
	 * this event as an UP event since the link is actually UP now.
	 */
	if (phy_info->link_info.link_info & ICE_AQ_LINK_UP)
		link_up = true;

	vsi = ice_get_main_vsi(pf);
	if (!vsi || !vsi->port_info)
		return -EINVAL;

	/* turn off PHY if media was removed */
	if (!test_bit(ICE_FLAG_NO_MEDIA, pf->flags) &&
	    !(pi->phy.link_info.link_info & ICE_AQ_MEDIA_AVAILABLE)) {
		set_bit(ICE_FLAG_NO_MEDIA, pf->flags);
		ice_set_link(vsi, false);
	}

	/* if the old link up/down and speed is the same as the new */
	if (link_up == old_link && link_speed == old_link_speed)
		return 0;

	ice_ptp_link_change(pf, pf->hw.pf_id, link_up);

	if (ice_is_dcb_active(pf)) {
		if (test_bit(ICE_FLAG_DCB_ENA, pf->flags))
			ice_dcb_rebuild(pf);
	} else {
		if (link_up)
			ice_set_dflt_mib(pf);
	}
	ice_vsi_link_event(vsi, link_up);
	ice_print_link_msg(vsi, link_up);

	ice_vc_notify_link_state(pf);

	return 0;
}

/**
 * ice_watchdog_subtask - periodic tasks not using event driven scheduling
 * @pf: board private structure
 */
static void ice_watchdog_subtask(struct ice_pf *pf)
{
	int i;

	/* if interface is down do nothing */
	if (test_bit(ICE_DOWN, pf->state) ||
	    test_bit(ICE_CFG_BUSY, pf->state))
		return;

	/* make sure we don't do these things too often */
	if (time_before(jiffies,
			pf->serv_tmr_prev + pf->serv_tmr_period))
		return;

	pf->serv_tmr_prev = jiffies;

	/* Update the stats for active netdevs so the network stack
	 * can look at updated numbers whenever it cares to
	 */
	ice_update_pf_stats(pf);
	ice_for_each_vsi(pf, i)
		if (pf->vsi[i] && pf->vsi[i]->netdev)
			ice_update_vsi_stats(pf->vsi[i]);
}

/**
 * ice_init_link_events - enable/initialize link events
 * @pi: pointer to the port_info instance
 *
 * Returns -EIO on failure, 0 on success
 */
static int ice_init_link_events(struct ice_port_info *pi)
{
	u16 mask;

	mask = ~((u16)(ICE_AQ_LINK_EVENT_UPDOWN | ICE_AQ_LINK_EVENT_MEDIA_NA |
		       ICE_AQ_LINK_EVENT_MODULE_QUAL_FAIL |
		       ICE_AQ_LINK_EVENT_PHY_FW_LOAD_FAIL));

	if (ice_aq_set_event_mask(pi->hw, pi->lport, mask, NULL)) {
		dev_dbg(ice_hw_to_dev(pi->hw), "Failed to set link event mask for port %d\n",
			pi->lport);
		return -EIO;
	}

	if (ice_aq_get_link_info(pi, true, NULL, NULL)) {
		dev_dbg(ice_hw_to_dev(pi->hw), "Failed to enable link events for port %d\n",
			pi->lport);
		return -EIO;
	}

	return 0;
}

/**
 * ice_handle_link_event - handle link event via ARQ
 * @pf: PF that the link event is associated with
 * @event: event structure containing link status info
 */
static int
ice_handle_link_event(struct ice_pf *pf, struct ice_rq_event_info *event)
{
	struct ice_aqc_get_link_status_data *link_data;
	struct ice_port_info *port_info;
	int status;

	link_data = (struct ice_aqc_get_link_status_data *)event->msg_buf;
	port_info = pf->hw.port_info;
	if (!port_info)
		return -EINVAL;

	status = ice_link_event(pf, port_info,
				!!(link_data->link_info & ICE_AQ_LINK_UP),
				le16_to_cpu(link_data->link_speed));
	if (status)
		dev_dbg(ice_pf_to_dev(pf), "Could not process link event, error %d\n",
			status);

	return status;
}

/**
 * ice_get_fwlog_data - copy the FW log data from ARQ event
 * @pf: PF that the FW log event is associated with
 * @event: event structure containing FW log data
 */
static void
ice_get_fwlog_data(struct ice_pf *pf, struct ice_rq_event_info *event)
{
	struct ice_fwlog_data *fwlog;
	struct ice_hw *hw = &pf->hw;

	fwlog = &hw->fwlog_ring.rings[hw->fwlog_ring.tail];

	memset(fwlog->data, 0, PAGE_SIZE);
	fwlog->data_size = le16_to_cpu(event->desc.datalen);

	memcpy(fwlog->data, event->msg_buf, fwlog->data_size);
	ice_fwlog_ring_increment(&hw->fwlog_ring.tail, hw->fwlog_ring.size);

	if (ice_fwlog_ring_full(&hw->fwlog_ring)) {
		/* the rings are full so bump the head to create room */
		ice_fwlog_ring_increment(&hw->fwlog_ring.head,
					 hw->fwlog_ring.size);
	}
}

/**
 * ice_aq_prep_for_event - Prepare to wait for an AdminQ event from firmware
 * @pf: pointer to the PF private structure
 * @task: intermediate helper storage and identifier for waiting
 * @opcode: the opcode to wait for
 *
 * Prepares to wait for a specific AdminQ completion event on the ARQ for
 * a given PF. Actual wait would be done by a call to ice_aq_wait_for_event().
 *
 * Calls are separated to allow caller registering for event before sending
 * the command, which mitigates a race between registering and FW responding.
 *
 * To obtain only the descriptor contents, pass an task->event with null
 * msg_buf. If the complete data buffer is desired, allocate the
 * task->event.msg_buf with enough space ahead of time.
 */
void ice_aq_prep_for_event(struct ice_pf *pf, struct ice_aq_task *task,
			   u16 opcode)
{
	INIT_HLIST_NODE(&task->entry);
	task->opcode = opcode;
	task->state = ICE_AQ_TASK_WAITING;

	spin_lock_bh(&pf->aq_wait_lock);
	hlist_add_head(&task->entry, &pf->aq_wait_list);
	spin_unlock_bh(&pf->aq_wait_lock);
}

/**
 * ice_aq_wait_for_event - Wait for an AdminQ event from firmware
 * @pf: pointer to the PF private structure
 * @task: ptr prepared by ice_aq_prep_for_event()
 * @timeout: how long to wait, in jiffies
 *
 * Waits for a specific AdminQ completion event on the ARQ for a given PF. The
 * current thread will be put to sleep until the specified event occurs or
 * until the given timeout is reached.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
int ice_aq_wait_for_event(struct ice_pf *pf, struct ice_aq_task *task,
			  unsigned long timeout)
{
	enum ice_aq_task_state *state = &task->state;
	struct device *dev = ice_pf_to_dev(pf);
	unsigned long start = jiffies;
	long ret;
	int err;

	ret = wait_event_interruptible_timeout(pf->aq_wait_queue,
					       *state != ICE_AQ_TASK_WAITING,
					       timeout);
	switch (*state) {
	case ICE_AQ_TASK_NOT_PREPARED:
		WARN(1, "call to %s without ice_aq_prep_for_event()", __func__);
		err = -EINVAL;
		break;
	case ICE_AQ_TASK_WAITING:
		err = ret < 0 ? ret : -ETIMEDOUT;
		break;
	case ICE_AQ_TASK_CANCELED:
		err = ret < 0 ? ret : -ECANCELED;
		break;
	case ICE_AQ_TASK_COMPLETE:
		err = ret < 0 ? ret : 0;
		break;
	default:
		WARN(1, "Unexpected AdminQ wait task state %u", *state);
		err = -EINVAL;
		break;
	}

	dev_dbg(dev, "Waited %u msecs (max %u msecs) for firmware response to op 0x%04x\n",
		jiffies_to_msecs(jiffies - start),
		jiffies_to_msecs(timeout),
		task->opcode);

	spin_lock_bh(&pf->aq_wait_lock);
	hlist_del(&task->entry);
	spin_unlock_bh(&pf->aq_wait_lock);

	return err;
}

/**
 * ice_aq_check_events - Check if any thread is waiting for an AdminQ event
 * @pf: pointer to the PF private structure
 * @opcode: the opcode of the event
 * @event: the event to check
 *
 * Loops over the current list of pending threads waiting for an AdminQ event.
 * For each matching task, copy the contents of the event into the task
 * structure and wake up the thread.
 *
 * If multiple threads wait for the same opcode, they will all be woken up.
 *
 * Note that event->msg_buf will only be duplicated if the event has a buffer
 * with enough space already allocated. Otherwise, only the descriptor and
 * message length will be copied.
 *
 * Returns: true if an event was found, false otherwise
 */
static void ice_aq_check_events(struct ice_pf *pf, u16 opcode,
				struct ice_rq_event_info *event)
{
	struct ice_rq_event_info *task_ev;
	struct ice_aq_task *task;
	bool found = false;

	spin_lock_bh(&pf->aq_wait_lock);
	hlist_for_each_entry(task, &pf->aq_wait_list, entry) {
		if (task->state != ICE_AQ_TASK_WAITING)
			continue;
		if (task->opcode != opcode)
			continue;

		task_ev = &task->event;
		memcpy(&task_ev->desc, &event->desc, sizeof(event->desc));
		task_ev->msg_len = event->msg_len;

		/* Only copy the data buffer if a destination was set */
		if (task_ev->msg_buf && task_ev->buf_len >= event->buf_len) {
			memcpy(task_ev->msg_buf, event->msg_buf,
			       event->buf_len);
			task_ev->buf_len = event->buf_len;
		}

		task->state = ICE_AQ_TASK_COMPLETE;
		found = true;
	}
	spin_unlock_bh(&pf->aq_wait_lock);

	if (found)
		wake_up(&pf->aq_wait_queue);
}

/**
 * ice_aq_cancel_waiting_tasks - Immediately cancel all waiting tasks
 * @pf: the PF private structure
 *
 * Set all waiting tasks to ICE_AQ_TASK_CANCELED, and wake up their threads.
 * This will then cause ice_aq_wait_for_event to exit with -ECANCELED.
 */
static void ice_aq_cancel_waiting_tasks(struct ice_pf *pf)
{
	struct ice_aq_task *task;

	spin_lock_bh(&pf->aq_wait_lock);
	hlist_for_each_entry(task, &pf->aq_wait_list, entry)
		task->state = ICE_AQ_TASK_CANCELED;
	spin_unlock_bh(&pf->aq_wait_lock);

	wake_up(&pf->aq_wait_queue);
}

#define ICE_MBX_OVERFLOW_WATERMARK 64

/**
 * __ice_clean_ctrlq - helper function to clean controlq rings
 * @pf: ptr to struct ice_pf
 * @q_type: specific Control queue type
 */
static int __ice_clean_ctrlq(struct ice_pf *pf, enum ice_ctl_q q_type)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_rq_event_info event;
	struct ice_hw *hw = &pf->hw;
	struct ice_ctl_q_info *cq;
	u16 pending, i = 0;
	const char *qtype;
	u32 oldval, val;

	/* Do not clean control queue if/when PF reset fails */
	if (test_bit(ICE_RESET_FAILED, pf->state))
		return 0;

	switch (q_type) {
	case ICE_CTL_Q_ADMIN:
		cq = &hw->adminq;
		qtype = "Admin";
		break;
	case ICE_CTL_Q_SB:
		cq = &hw->sbq;
		qtype = "Sideband";
		break;
	case ICE_CTL_Q_MAILBOX:
		cq = &hw->mailboxq;
		qtype = "Mailbox";
		/* we are going to try to detect a malicious VF, so set the
		 * state to begin detection
		 */
		hw->mbx_snapshot.mbx_buf.state = ICE_MAL_VF_DETECT_STATE_NEW_SNAPSHOT;
		break;
	default:
		dev_warn(dev, "Unknown control queue type 0x%x\n", q_type);
		return 0;
	}

	/* check for error indications - PF_xx_AxQLEN register layout for
	 * FW/MBX/SB are identical so just use defines for PF_FW_AxQLEN.
	 */
	val = rd32(hw, cq->rq.len);
	if (val & (PF_FW_ARQLEN_ARQVFE_M | PF_FW_ARQLEN_ARQOVFL_M |
		   PF_FW_ARQLEN_ARQCRIT_M)) {
		oldval = val;
		if (val & PF_FW_ARQLEN_ARQVFE_M)
			dev_dbg(dev, "%s Receive Queue VF Error detected\n",
				qtype);
		if (val & PF_FW_ARQLEN_ARQOVFL_M) {
			dev_dbg(dev, "%s Receive Queue Overflow Error detected\n",
				qtype);
		}
		if (val & PF_FW_ARQLEN_ARQCRIT_M)
			dev_dbg(dev, "%s Receive Queue Critical Error detected\n",
				qtype);
		val &= ~(PF_FW_ARQLEN_ARQVFE_M | PF_FW_ARQLEN_ARQOVFL_M |
			 PF_FW_ARQLEN_ARQCRIT_M);
		if (oldval != val)
			wr32(hw, cq->rq.len, val);
	}

	val = rd32(hw, cq->sq.len);
	if (val & (PF_FW_ATQLEN_ATQVFE_M | PF_FW_ATQLEN_ATQOVFL_M |
		   PF_FW_ATQLEN_ATQCRIT_M)) {
		oldval = val;
		if (val & PF_FW_ATQLEN_ATQVFE_M)
			dev_dbg(dev, "%s Send Queue VF Error detected\n",
				qtype);
		if (val & PF_FW_ATQLEN_ATQOVFL_M) {
			dev_dbg(dev, "%s Send Queue Overflow Error detected\n",
				qtype);
		}
		if (val & PF_FW_ATQLEN_ATQCRIT_M)
			dev_dbg(dev, "%s Send Queue Critical Error detected\n",
				qtype);
		val &= ~(PF_FW_ATQLEN_ATQVFE_M | PF_FW_ATQLEN_ATQOVFL_M |
			 PF_FW_ATQLEN_ATQCRIT_M);
		if (oldval != val)
			wr32(hw, cq->sq.len, val);
	}

	event.buf_len = cq->rq_buf_size;
	event.msg_buf = kzalloc(event.buf_len, GFP_KERNEL);
	if (!event.msg_buf)
		return 0;

	do {
		struct ice_mbx_data data = {};
		u16 opcode;
		int ret;

		ret = ice_clean_rq_elem(hw, cq, &event, &pending);
		if (ret == -EALREADY)
			break;
		if (ret) {
			dev_err(dev, "%s Receive Queue event error %d\n", qtype,
				ret);
			break;
		}

		opcode = le16_to_cpu(event.desc.opcode);

		/* Notify any thread that might be waiting for this event */
		ice_aq_check_events(pf, opcode, &event);

		switch (opcode) {
		case ice_aqc_opc_get_link_status:
			if (ice_handle_link_event(pf, &event))
				dev_err(dev, "Could not handle link event\n");
			break;
		case ice_aqc_opc_event_lan_overflow:
			ice_vf_lan_overflow_event(pf, &event);
			break;
		case ice_mbx_opc_send_msg_to_pf:
			data.num_msg_proc = i;
			data.num_pending_arq = pending;
			data.max_num_msgs_mbx = hw->mailboxq.num_rq_entries;
			data.async_watermark_val = ICE_MBX_OVERFLOW_WATERMARK;

			ice_vc_process_vf_msg(pf, &event, &data);
			break;
		case ice_aqc_opc_fw_logs_event:
			ice_get_fwlog_data(pf, &event);
			break;
		case ice_aqc_opc_lldp_set_mib_change:
			ice_dcb_process_lldp_set_mib_change(pf, &event);
			break;
		default:
			dev_dbg(dev, "%s Receive Queue unknown event 0x%04x ignored\n",
				qtype, opcode);
			break;
		}
	} while (pending && (i++ < ICE_DFLT_IRQ_WORK));

	kfree(event.msg_buf);

	return pending && (i == ICE_DFLT_IRQ_WORK);
}

/**
 * ice_ctrlq_pending - check if there is a difference between ntc and ntu
 * @hw: pointer to hardware info
 * @cq: control queue information
 *
 * returns true if there are pending messages in a queue, false if there aren't
 */
static bool ice_ctrlq_pending(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	u16 ntu;

	ntu = (u16)(rd32(hw, cq->rq.head) & cq->rq.head_mask);
	return cq->rq.next_to_clean != ntu;
}

/**
 * ice_clean_adminq_subtask - clean the AdminQ rings
 * @pf: board private structure
 */
static void ice_clean_adminq_subtask(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;

	if (!test_bit(ICE_ADMINQ_EVENT_PENDING, pf->state))
		return;

	if (__ice_clean_ctrlq(pf, ICE_CTL_Q_ADMIN))
		return;

	clear_bit(ICE_ADMINQ_EVENT_PENDING, pf->state);

	/* There might be a situation where new messages arrive to a control
	 * queue between processing the last message and clearing the
	 * EVENT_PENDING bit. So before exiting, check queue head again (using
	 * ice_ctrlq_pending) and process new messages if any.
	 */
	if (ice_ctrlq_pending(hw, &hw->adminq))
		__ice_clean_ctrlq(pf, ICE_CTL_Q_ADMIN);

	ice_flush(hw);
}

/**
 * ice_clean_mailboxq_subtask - clean the MailboxQ rings
 * @pf: board private structure
 */
static void ice_clean_mailboxq_subtask(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;

	if (!test_bit(ICE_MAILBOXQ_EVENT_PENDING, pf->state))
		return;

	if (__ice_clean_ctrlq(pf, ICE_CTL_Q_MAILBOX))
		return;

	clear_bit(ICE_MAILBOXQ_EVENT_PENDING, pf->state);

	if (ice_ctrlq_pending(hw, &hw->mailboxq))
		__ice_clean_ctrlq(pf, ICE_CTL_Q_MAILBOX);

	ice_flush(hw);
}

/**
 * ice_clean_sbq_subtask - clean the Sideband Queue rings
 * @pf: board private structure
 */
static void ice_clean_sbq_subtask(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;

	/* if mac_type is not generic, sideband is not supported
	 * and there's nothing to do here
	 */
	if (!ice_is_generic_mac(hw)) {
		clear_bit(ICE_SIDEBANDQ_EVENT_PENDING, pf->state);
		return;
	}

	if (!test_bit(ICE_SIDEBANDQ_EVENT_PENDING, pf->state))
		return;

	if (__ice_clean_ctrlq(pf, ICE_CTL_Q_SB))
		return;

	clear_bit(ICE_SIDEBANDQ_EVENT_PENDING, pf->state);

	if (ice_ctrlq_pending(hw, &hw->sbq))
		__ice_clean_ctrlq(pf, ICE_CTL_Q_SB);

	ice_flush(hw);
}

/**
 * ice_service_task_schedule - schedule the service task to wake up
 * @pf: board private structure
 *
 * If not already scheduled, this puts the task into the work queue.
 */
void ice_service_task_schedule(struct ice_pf *pf)
{
	if (!test_bit(ICE_SERVICE_DIS, pf->state) &&
	    !test_and_set_bit(ICE_SERVICE_SCHED, pf->state) &&
	    !test_bit(ICE_NEEDS_RESTART, pf->state))
		queue_work(ice_wq, &pf->serv_task);
}

/**
 * ice_service_task_complete - finish up the service task
 * @pf: board private structure
 */
static void ice_service_task_complete(struct ice_pf *pf)
{
	WARN_ON(!test_bit(ICE_SERVICE_SCHED, pf->state));

	/* force memory (pf->state) to sync before next service task */
	smp_mb__before_atomic();
	clear_bit(ICE_SERVICE_SCHED, pf->state);
}

/**
 * ice_service_task_stop - stop service task and cancel works
 * @pf: board private structure
 *
 * Return 0 if the ICE_SERVICE_DIS bit was not already set,
 * 1 otherwise.
 */
static int ice_service_task_stop(struct ice_pf *pf)
{
	int ret;

	ret = test_and_set_bit(ICE_SERVICE_DIS, pf->state);

	if (pf->serv_tmr.function)
		del_timer_sync(&pf->serv_tmr);
	if (pf->serv_task.func)
		cancel_work_sync(&pf->serv_task);

	clear_bit(ICE_SERVICE_SCHED, pf->state);
	return ret;
}

/**
 * ice_service_task_restart - restart service task and schedule works
 * @pf: board private structure
 *
 * This function is needed for suspend and resume works (e.g WoL scenario)
 */
static void ice_service_task_restart(struct ice_pf *pf)
{
	clear_bit(ICE_SERVICE_DIS, pf->state);
	ice_service_task_schedule(pf);
}

/**
 * ice_service_timer - timer callback to schedule service task
 * @t: pointer to timer_list
 */
static void ice_service_timer(struct timer_list *t)
{
	struct ice_pf *pf = from_timer(pf, t, serv_tmr);

	mod_timer(&pf->serv_tmr, round_jiffies(pf->serv_tmr_period + jiffies));
	ice_service_task_schedule(pf);
}

/**
 * ice_mdd_maybe_reset_vf - reset VF after MDD event
 * @pf: pointer to the PF structure
 * @vf: pointer to the VF structure
 * @reset_vf_tx: whether Tx MDD has occurred
 * @reset_vf_rx: whether Rx MDD has occurred
 *
 * Since the queue can get stuck on VF MDD events, the PF can be configured to
 * automatically reset the VF by enabling the private ethtool flag
 * mdd-auto-reset-vf.
 */
static void ice_mdd_maybe_reset_vf(struct ice_pf *pf, struct ice_vf *vf,
				   bool reset_vf_tx, bool reset_vf_rx)
{
	struct device *dev = ice_pf_to_dev(pf);

	if (!test_bit(ICE_FLAG_MDD_AUTO_RESET_VF, pf->flags))
		return;

	/* VF MDD event counters will be cleared by reset, so print the event
	 * prior to reset.
	 */
	if (reset_vf_tx)
		ice_print_vf_tx_mdd_event(vf);

	if (reset_vf_rx)
		ice_print_vf_rx_mdd_event(vf);

	dev_info(dev, "PF-to-VF reset on PF %d VF %d due to MDD event\n",
		 pf->hw.pf_id, vf->vf_id);
	ice_reset_vf(vf, ICE_VF_RESET_NOTIFY | ICE_VF_RESET_LOCK);
}

/**
 * ice_handle_mdd_event - handle malicious driver detect event
 * @pf: pointer to the PF structure
 *
 * Called from service task. OICR interrupt handler indicates MDD event.
 * VF MDD logging is guarded by net_ratelimit. Additional PF and VF log
 * messages are wrapped by netif_msg_[rx|tx]_err. Since VF Rx MDD events
 * disable the queue, the PF can be configured to reset the VF using ethtool
 * private flag mdd-auto-reset-vf.
 */
static void ice_handle_mdd_event(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	struct ice_vf *vf;
	unsigned int bkt;
	u32 reg;

	if (!test_and_clear_bit(ICE_MDD_EVENT_PENDING, pf->state)) {
		/* Since the VF MDD event logging is rate limited, check if
		 * there are pending MDD events.
		 */
		ice_print_vfs_mdd_events(pf);
		return;
	}

	/* find what triggered an MDD event */
	reg = rd32(hw, GL_MDET_TX_PQM);
	if (reg & GL_MDET_TX_PQM_VALID_M) {
		u8 pf_num = FIELD_GET(GL_MDET_TX_PQM_PF_NUM_M, reg);
		u16 vf_num = FIELD_GET(GL_MDET_TX_PQM_VF_NUM_M, reg);
		u8 event = FIELD_GET(GL_MDET_TX_PQM_MAL_TYPE_M, reg);
		u16 queue = FIELD_GET(GL_MDET_TX_PQM_QNUM_M, reg);

		if (netif_msg_tx_err(pf))
			dev_info(dev, "Malicious Driver Detection event %d on TX queue %d PF# %d VF# %d\n",
				 event, queue, pf_num, vf_num);
		wr32(hw, GL_MDET_TX_PQM, 0xffffffff);
	}

	reg = rd32(hw, GL_MDET_TX_TCLAN_BY_MAC(hw));
	if (reg & GL_MDET_TX_TCLAN_VALID_M) {
		u8 pf_num = FIELD_GET(GL_MDET_TX_TCLAN_PF_NUM_M, reg);
		u16 vf_num = FIELD_GET(GL_MDET_TX_TCLAN_VF_NUM_M, reg);
		u8 event = FIELD_GET(GL_MDET_TX_TCLAN_MAL_TYPE_M, reg);
		u16 queue = FIELD_GET(GL_MDET_TX_TCLAN_QNUM_M, reg);

		if (netif_msg_tx_err(pf))
			dev_info(dev, "Malicious Driver Detection event %d on TX queue %d PF# %d VF# %d\n",
				 event, queue, pf_num, vf_num);
		wr32(hw, GL_MDET_TX_TCLAN_BY_MAC(hw), U32_MAX);
	}

	reg = rd32(hw, GL_MDET_RX);
	if (reg & GL_MDET_RX_VALID_M) {
		u8 pf_num = FIELD_GET(GL_MDET_RX_PF_NUM_M, reg);
		u16 vf_num = FIELD_GET(GL_MDET_RX_VF_NUM_M, reg);
		u8 event = FIELD_GET(GL_MDET_RX_MAL_TYPE_M, reg);
		u16 queue = FIELD_GET(GL_MDET_RX_QNUM_M, reg);

		if (netif_msg_rx_err(pf))
			dev_info(dev, "Malicious Driver Detection event %d on RX queue %d PF# %d VF# %d\n",
				 event, queue, pf_num, vf_num);
		wr32(hw, GL_MDET_RX, 0xffffffff);
	}

	/* check to see if this PF caused an MDD event */
	reg = rd32(hw, PF_MDET_TX_PQM);
	if (reg & PF_MDET_TX_PQM_VALID_M) {
		wr32(hw, PF_MDET_TX_PQM, 0xFFFF);
		if (netif_msg_tx_err(pf))
			dev_info(dev, "Malicious Driver Detection event TX_PQM detected on PF\n");
	}

	reg = rd32(hw, PF_MDET_TX_TCLAN_BY_MAC(hw));
	if (reg & PF_MDET_TX_TCLAN_VALID_M) {
		wr32(hw, PF_MDET_TX_TCLAN_BY_MAC(hw), 0xffff);
		if (netif_msg_tx_err(pf))
			dev_info(dev, "Malicious Driver Detection event TX_TCLAN detected on PF\n");
	}

	reg = rd32(hw, PF_MDET_RX);
	if (reg & PF_MDET_RX_VALID_M) {
		wr32(hw, PF_MDET_RX, 0xFFFF);
		if (netif_msg_rx_err(pf))
			dev_info(dev, "Malicious Driver Detection event RX detected on PF\n");
	}

	/* Check to see if one of the VFs caused an MDD event, and then
	 * increment counters and set print pending
	 */
	mutex_lock(&pf->vfs.table_lock);
	ice_for_each_vf(pf, bkt, vf) {
		bool reset_vf_tx = false, reset_vf_rx = false;

		reg = rd32(hw, VP_MDET_TX_PQM(vf->vf_id));
		if (reg & VP_MDET_TX_PQM_VALID_M) {
			wr32(hw, VP_MDET_TX_PQM(vf->vf_id), 0xFFFF);
			vf->mdd_tx_events.count++;
			set_bit(ICE_MDD_VF_PRINT_PENDING, pf->state);
			if (netif_msg_tx_err(pf))
				dev_info(dev, "Malicious Driver Detection event TX_PQM detected on VF %d\n",
					 vf->vf_id);

			reset_vf_tx = true;
		}

		reg = rd32(hw, VP_MDET_TX_TCLAN(vf->vf_id));
		if (reg & VP_MDET_TX_TCLAN_VALID_M) {
			wr32(hw, VP_MDET_TX_TCLAN(vf->vf_id), 0xFFFF);
			vf->mdd_tx_events.count++;
			set_bit(ICE_MDD_VF_PRINT_PENDING, pf->state);
			if (netif_msg_tx_err(pf))
				dev_info(dev, "Malicious Driver Detection event TX_TCLAN detected on VF %d\n",
					 vf->vf_id);

			reset_vf_tx = true;
		}

		reg = rd32(hw, VP_MDET_TX_TDPU(vf->vf_id));
		if (reg & VP_MDET_TX_TDPU_VALID_M) {
			wr32(hw, VP_MDET_TX_TDPU(vf->vf_id), 0xFFFF);
			vf->mdd_tx_events.count++;
			set_bit(ICE_MDD_VF_PRINT_PENDING, pf->state);
			if (netif_msg_tx_err(pf))
				dev_info(dev, "Malicious Driver Detection event TX_TDPU detected on VF %d\n",
					 vf->vf_id);

			reset_vf_tx = true;
		}

		reg = rd32(hw, VP_MDET_RX(vf->vf_id));
		if (reg & VP_MDET_RX_VALID_M) {
			wr32(hw, VP_MDET_RX(vf->vf_id), 0xFFFF);
			vf->mdd_rx_events.count++;
			set_bit(ICE_MDD_VF_PRINT_PENDING, pf->state);
			if (netif_msg_rx_err(pf))
				dev_info(dev, "Malicious Driver Detection event RX detected on VF %d\n",
					 vf->vf_id);

			reset_vf_rx = true;
		}

		if (reset_vf_tx || reset_vf_rx)
			ice_mdd_maybe_reset_vf(pf, vf, reset_vf_tx,
					       reset_vf_rx);
	}
	mutex_unlock(&pf->vfs.table_lock);

	ice_print_vfs_mdd_events(pf);
}

/**
 * ice_force_phys_link_state - Force the physical link state
 * @vsi: VSI to force the physical link state to up/down
 * @link_up: true/false indicates to set the physical link to up/down
 *
 * Force the physical link state by getting the current PHY capabilities from
 * hardware and setting the PHY config based on the determined capabilities. If
 * link changes a link event will be triggered because both the Enable Automatic
 * Link Update and LESM Enable bits are set when setting the PHY capabilities.
 *
 * Returns 0 on success, negative on failure
 */
static int ice_force_phys_link_state(struct ice_vsi *vsi, bool link_up)
{
	struct ice_aqc_get_phy_caps_data *pcaps;
	struct ice_aqc_set_phy_cfg_data *cfg;
	struct ice_port_info *pi;
	struct device *dev;
	int retcode;

	if (!vsi || !vsi->port_info || !vsi->back)
		return -EINVAL;
	if (vsi->type != ICE_VSI_PF)
		return 0;

	dev = ice_pf_to_dev(vsi->back);

	pi = vsi->port_info;

	pcaps = kzalloc(sizeof(*pcaps), GFP_KERNEL);
	if (!pcaps)
		return -ENOMEM;

	retcode = ice_aq_get_phy_caps(pi, false, ICE_AQC_REPORT_ACTIVE_CFG, pcaps,
				      NULL);
	if (retcode) {
		dev_err(dev, "Failed to get phy capabilities, VSI %d error %d\n",
			vsi->vsi_num, retcode);
		retcode = -EIO;
		goto out;
	}

	/* No change in link */
	if (link_up == !!(pcaps->caps & ICE_AQC_PHY_EN_LINK) &&
	    link_up == !!(pi->phy.link_info.link_info & ICE_AQ_LINK_UP))
		goto out;

	/* Use the current user PHY configuration. The current user PHY
	 * configuration is initialized during probe from PHY capabilities
	 * software mode, and updated on set PHY configuration.
	 */
	cfg = kmemdup(&pi->phy.curr_user_phy_cfg, sizeof(*cfg), GFP_KERNEL);
	if (!cfg) {
		retcode = -ENOMEM;
		goto out;
	}

	cfg->caps |= ICE_AQ_PHY_ENA_AUTO_LINK_UPDT;
	if (link_up)
		cfg->caps |= ICE_AQ_PHY_ENA_LINK;
	else
		cfg->caps &= ~ICE_AQ_PHY_ENA_LINK;

	retcode = ice_aq_set_phy_cfg(&vsi->back->hw, pi, cfg, NULL);
	if (retcode) {
		dev_err(dev, "Failed to set phy config, VSI %d error %d\n",
			vsi->vsi_num, retcode);
		retcode = -EIO;
	}

	kfree(cfg);
out:
	kfree(pcaps);
	return retcode;
}

/**
 * ice_init_nvm_phy_type - Initialize the NVM PHY type
 * @pi: port info structure
 *
 * Initialize nvm_phy_type_[low|high] for link lenient mode support
 */
static int ice_init_nvm_phy_type(struct ice_port_info *pi)
{
	struct ice_aqc_get_phy_caps_data *pcaps;
	struct ice_pf *pf = pi->hw->back;
	int err;

	pcaps = kzalloc(sizeof(*pcaps), GFP_KERNEL);
	if (!pcaps)
		return -ENOMEM;

	err = ice_aq_get_phy_caps(pi, false, ICE_AQC_REPORT_TOPO_CAP_NO_MEDIA,
				  pcaps, NULL);

	if (err) {
		dev_err(ice_pf_to_dev(pf), "Get PHY capability failed.\n");
		goto out;
	}

	pf->nvm_phy_type_hi = pcaps->phy_type_high;
	pf->nvm_phy_type_lo = pcaps->phy_type_low;

out:
	kfree(pcaps);
	return err;
}

/**
 * ice_init_link_dflt_override - Initialize link default override
 * @pi: port info structure
 *
 * Initialize link default override and PHY total port shutdown during probe
 */
static void ice_init_link_dflt_override(struct ice_port_info *pi)
{
	struct ice_link_default_override_tlv *ldo;
	struct ice_pf *pf = pi->hw->back;

	ldo = &pf->link_dflt_override;
	if (ice_get_link_default_override(ldo, pi))
		return;

	if (!(ldo->options & ICE_LINK_OVERRIDE_PORT_DIS))
		return;

	/* Enable Total Port Shutdown (override/replace link-down-on-close
	 * ethtool private flag) for ports with Port Disable bit set.
	 */
	set_bit(ICE_FLAG_TOTAL_PORT_SHUTDOWN_ENA, pf->flags);
	set_bit(ICE_FLAG_LINK_DOWN_ON_CLOSE_ENA, pf->flags);
}

/**
 * ice_init_phy_cfg_dflt_override - Initialize PHY cfg default override settings
 * @pi: port info structure
 *
 * If default override is enabled, initialize the user PHY cfg speed and FEC
 * settings using the default override mask from the NVM.
 *
 * The PHY should only be configured with the default override settings the
 * first time media is available. The ICE_LINK_DEFAULT_OVERRIDE_PENDING state
 * is used to indicate that the user PHY cfg default override is initialized
 * and the PHY has not been configured with the default override settings. The
 * state is set here, and cleared in ice_configure_phy the first time the PHY is
 * configured.
 *
 * This function should be called only if the FW doesn't support default
 * configuration mode, as reported by ice_fw_supports_report_dflt_cfg.
 */
static void ice_init_phy_cfg_dflt_override(struct ice_port_info *pi)
{
	struct ice_link_default_override_tlv *ldo;
	struct ice_aqc_set_phy_cfg_data *cfg;
	struct ice_phy_info *phy = &pi->phy;
	struct ice_pf *pf = pi->hw->back;

	ldo = &pf->link_dflt_override;

	/* If link default override is enabled, use to mask NVM PHY capabilities
	 * for speed and FEC default configuration.
	 */
	cfg = &phy->curr_user_phy_cfg;

	if (ldo->phy_type_low || ldo->phy_type_high) {
		cfg->phy_type_low = pf->nvm_phy_type_lo &
				    cpu_to_le64(ldo->phy_type_low);
		cfg->phy_type_high = pf->nvm_phy_type_hi &
				     cpu_to_le64(ldo->phy_type_high);
	}
	cfg->link_fec_opt = ldo->fec_options;
	phy->curr_user_fec_req = ICE_FEC_AUTO;

	set_bit(ICE_LINK_DEFAULT_OVERRIDE_PENDING, pf->state);
}

/**
 * ice_init_phy_user_cfg - Initialize the PHY user configuration
 * @pi: port info structure
 *
 * Initialize the current user PHY configuration, speed, FEC, and FC requested
 * mode to default. The PHY defaults are from get PHY capabilities topology
 * with media so call when media is first available. An error is returned if
 * called when media is not available. The PHY initialization completed state is
 * set here.
 *
 * These configurations are used when setting PHY
 * configuration. The user PHY configuration is updated on set PHY
 * configuration. Returns 0 on success, negative on failure
 */
static int ice_init_phy_user_cfg(struct ice_port_info *pi)
{
	struct ice_aqc_get_phy_caps_data *pcaps;
	struct ice_phy_info *phy = &pi->phy;
	struct ice_pf *pf = pi->hw->back;
	int err;

	if (!(phy->link_info.link_info & ICE_AQ_MEDIA_AVAILABLE))
		return -EIO;

	pcaps = kzalloc(sizeof(*pcaps), GFP_KERNEL);
	if (!pcaps)
		return -ENOMEM;

	if (ice_fw_supports_report_dflt_cfg(pi->hw))
		err = ice_aq_get_phy_caps(pi, false, ICE_AQC_REPORT_DFLT_CFG,
					  pcaps, NULL);
	else
		err = ice_aq_get_phy_caps(pi, false, ICE_AQC_REPORT_TOPO_CAP_MEDIA,
					  pcaps, NULL);
	if (err) {
		dev_err(ice_pf_to_dev(pf), "Get PHY capability failed.\n");
		goto err_out;
	}

	ice_copy_phy_caps_to_cfg(pi, pcaps, &pi->phy.curr_user_phy_cfg);

	/* check if lenient mode is supported and enabled */
	if (ice_fw_supports_link_override(pi->hw) &&
	    !(pcaps->module_compliance_enforcement &
	      ICE_AQC_MOD_ENFORCE_STRICT_MODE)) {
		set_bit(ICE_FLAG_LINK_LENIENT_MODE_ENA, pf->flags);

		/* if the FW supports default PHY configuration mode, then the driver
		 * does not have to apply link override settings. If not,
		 * initialize user PHY configuration with link override values
		 */
		if (!ice_fw_supports_report_dflt_cfg(pi->hw) &&
		    (pf->link_dflt_override.options & ICE_LINK_OVERRIDE_EN)) {
			ice_init_phy_cfg_dflt_override(pi);
			goto out;
		}
	}

	/* if link default override is not enabled, set user flow control and
	 * FEC settings based on what get_phy_caps returned
	 */
	phy->curr_user_fec_req = ice_caps_to_fec_mode(pcaps->caps,
						      pcaps->link_fec_options);
	phy->curr_user_fc_req = ice_caps_to_fc_mode(pcaps->caps);

out:
	phy->curr_user_speed_req = ICE_AQ_LINK_SPEED_M;
	set_bit(ICE_PHY_INIT_COMPLETE, pf->state);
err_out:
	kfree(pcaps);
	return err;
}

/**
 * ice_configure_phy - configure PHY
 * @vsi: VSI of PHY
 *
 * Set the PHY configuration. If the current PHY configuration is the same as
 * the curr_user_phy_cfg, then do nothing to avoid link flap. Otherwise
 * configure the based get PHY capabilities for topology with media.
 */
static int ice_configure_phy(struct ice_vsi *vsi)
{
	struct device *dev = ice_pf_to_dev(vsi->back);
	struct ice_port_info *pi = vsi->port_info;
	struct ice_aqc_get_phy_caps_data *pcaps;
	struct ice_aqc_set_phy_cfg_data *cfg;
	struct ice_phy_info *phy = &pi->phy;
	struct ice_pf *pf = vsi->back;
	int err;

	/* Ensure we have media as we cannot configure a medialess port */
	if (!(phy->link_info.link_info & ICE_AQ_MEDIA_AVAILABLE))
		return -ENOMEDIUM;

	ice_print_topo_conflict(vsi);

	if (!test_bit(ICE_FLAG_LINK_LENIENT_MODE_ENA, pf->flags) &&
	    phy->link_info.topo_media_conflict == ICE_AQ_LINK_TOPO_UNSUPP_MEDIA)
		return -EPERM;

	if (test_bit(ICE_FLAG_LINK_DOWN_ON_CLOSE_ENA, pf->flags))
		return ice_force_phys_link_state(vsi, true);

	pcaps = kzalloc(sizeof(*pcaps), GFP_KERNEL);
	if (!pcaps)
		return -ENOMEM;

	/* Get current PHY config */
	err = ice_aq_get_phy_caps(pi, false, ICE_AQC_REPORT_ACTIVE_CFG, pcaps,
				  NULL);
	if (err) {
		dev_err(dev, "Failed to get PHY configuration, VSI %d error %d\n",
			vsi->vsi_num, err);
		goto done;
	}

	/* If PHY enable link is configured and configuration has not changed,
	 * there's nothing to do
	 */
	if (pcaps->caps & ICE_AQC_PHY_EN_LINK &&
	    ice_phy_caps_equals_cfg(pcaps, &phy->curr_user_phy_cfg))
		goto done;

	/* Use PHY topology as baseline for configuration */
	memset(pcaps, 0, sizeof(*pcaps));
	if (ice_fw_supports_report_dflt_cfg(pi->hw))
		err = ice_aq_get_phy_caps(pi, false, ICE_AQC_REPORT_DFLT_CFG,
					  pcaps, NULL);
	else
		err = ice_aq_get_phy_caps(pi, false, ICE_AQC_REPORT_TOPO_CAP_MEDIA,
					  pcaps, NULL);
	if (err) {
		dev_err(dev, "Failed to get PHY caps, VSI %d error %d\n",
			vsi->vsi_num, err);
		goto done;
	}

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg) {
		err = -ENOMEM;
		goto done;
	}

	ice_copy_phy_caps_to_cfg(pi, pcaps, cfg);

	/* Speed - If default override pending, use curr_user_phy_cfg set in
	 * ice_init_phy_user_cfg_ldo.
	 */
	if (test_and_clear_bit(ICE_LINK_DEFAULT_OVERRIDE_PENDING,
			       vsi->back->state)) {
		cfg->phy_type_low = phy->curr_user_phy_cfg.phy_type_low;
		cfg->phy_type_high = phy->curr_user_phy_cfg.phy_type_high;
	} else {
		u64 phy_low = 0, phy_high = 0;

		ice_update_phy_type(&phy_low, &phy_high,
				    pi->phy.curr_user_speed_req);
		cfg->phy_type_low = pcaps->phy_type_low & cpu_to_le64(phy_low);
		cfg->phy_type_high = pcaps->phy_type_high &
				     cpu_to_le64(phy_high);
	}

	/* Can't provide what was requested; use PHY capabilities */
	if (!cfg->phy_type_low && !cfg->phy_type_high) {
		cfg->phy_type_low = pcaps->phy_type_low;
		cfg->phy_type_high = pcaps->phy_type_high;
	}

	/* FEC */
	ice_cfg_phy_fec(pi, cfg, phy->curr_user_fec_req);

	/* Can't provide what was requested; use PHY capabilities */
	if (cfg->link_fec_opt !=
	    (cfg->link_fec_opt & pcaps->link_fec_options)) {
		cfg->caps |= pcaps->caps & ICE_AQC_PHY_EN_AUTO_FEC;
		cfg->link_fec_opt = pcaps->link_fec_options;
	}

	/* Flow Control - always supported; no need to check against
	 * capabilities
	 */
	ice_cfg_phy_fc(pi, cfg, phy->curr_user_fc_req);

	/* Enable link and link update */
	cfg->caps |= ICE_AQ_PHY_ENA_AUTO_LINK_UPDT | ICE_AQ_PHY_ENA_LINK;

	err = ice_aq_set_phy_cfg(&pf->hw, pi, cfg, NULL);
	if (err)
		dev_err(dev, "Failed to set phy config, VSI %d error %d\n",
			vsi->vsi_num, err);

	kfree(cfg);
done:
	kfree(pcaps);
	return err;
}

/**
 * ice_check_media_subtask - Check for media
 * @pf: pointer to PF struct
 *
 * If media is available, then initialize PHY user configuration if it is not
 * been, and configure the PHY if the interface is up.
 */
static void ice_check_media_subtask(struct ice_pf *pf)
{
	struct ice_port_info *pi;
	struct ice_vsi *vsi;
	int err;

	/* No need to check for media if it's already present */
	if (!test_bit(ICE_FLAG_NO_MEDIA, pf->flags))
		return;

	vsi = ice_get_main_vsi(pf);
	if (!vsi)
		return;

	/* Refresh link info and check if media is present */
	pi = vsi->port_info;
	err = ice_update_link_info(pi);
	if (err)
		return;

	ice_check_link_cfg_err(pf, pi->phy.link_info.link_cfg_err);

	if (pi->phy.link_info.link_info & ICE_AQ_MEDIA_AVAILABLE) {
		if (!test_bit(ICE_PHY_INIT_COMPLETE, pf->state))
			ice_init_phy_user_cfg(pi);

		/* PHY settings are reset on media insertion, reconfigure
		 * PHY to preserve settings.
		 */
		if (test_bit(ICE_VSI_DOWN, vsi->state) &&
		    test_bit(ICE_FLAG_LINK_DOWN_ON_CLOSE_ENA, vsi->back->flags))
			return;

		err = ice_configure_phy(vsi);
		if (!err)
			clear_bit(ICE_FLAG_NO_MEDIA, pf->flags);

		/* A Link Status Event will be generated; the event handler
		 * will complete bringing the interface up
		 */
	}
}

/**
 * ice_service_task - manage and run subtasks
 * @work: pointer to work_struct contained by the PF struct
 */
static void ice_service_task(struct work_struct *work)
{
	struct ice_pf *pf = container_of(work, struct ice_pf, serv_task);
	unsigned long start_time = jiffies;

	/* subtasks */

	/* process reset requests first */
	ice_reset_subtask(pf);

	/* bail if a reset/recovery cycle is pending or rebuild failed */
	if (ice_is_reset_in_progress(pf->state) ||
	    test_bit(ICE_SUSPENDED, pf->state) ||
	    test_bit(ICE_NEEDS_RESTART, pf->state)) {
		ice_service_task_complete(pf);
		return;
	}

	if (test_and_clear_bit(ICE_AUX_ERR_PENDING, pf->state)) {
		struct iidc_event *event;

		event = kzalloc(sizeof(*event), GFP_KERNEL);
		if (event) {
			set_bit(IIDC_EVENT_CRIT_ERR, event->type);
			/* report the entire OICR value to AUX driver */
			swap(event->reg, pf->oicr_err_reg);
			ice_send_event_to_aux(pf, event);
			kfree(event);
		}
	}

	/* unplug aux dev per request, if an unplug request came in
	 * while processing a plug request, this will handle it
	 */
	if (test_and_clear_bit(ICE_FLAG_UNPLUG_AUX_DEV, pf->flags))
		ice_unplug_aux_dev(pf);

	/* Plug aux device per request */
	if (test_and_clear_bit(ICE_FLAG_PLUG_AUX_DEV, pf->flags))
		ice_plug_aux_dev(pf);

	if (test_and_clear_bit(ICE_FLAG_MTU_CHANGED, pf->flags)) {
		struct iidc_event *event;

		event = kzalloc(sizeof(*event), GFP_KERNEL);
		if (event) {
			set_bit(IIDC_EVENT_AFTER_MTU_CHANGE, event->type);
			ice_send_event_to_aux(pf, event);
			kfree(event);
		}
	}

	ice_clean_adminq_subtask(pf);
	ice_check_media_subtask(pf);
	ice_check_for_hang_subtask(pf);
	ice_sync_fltr_subtask(pf);
	ice_handle_mdd_event(pf);
	ice_watchdog_subtask(pf);

	if (ice_is_safe_mode(pf)) {
		ice_service_task_complete(pf);
		return;
	}

	ice_process_vflr_event(pf);
	ice_clean_mailboxq_subtask(pf);
	ice_clean_sbq_subtask(pf);
	ice_sync_arfs_fltrs(pf);
	ice_flush_fdir_ctx(pf);

	/* Clear ICE_SERVICE_SCHED flag to allow scheduling next event */
	ice_service_task_complete(pf);

	/* If the tasks have taken longer than one service timer period
	 * or there is more work to be done, reset the service timer to
	 * schedule the service task now.
	 */
	if (time_after(jiffies, (start_time + pf->serv_tmr_period)) ||
	    test_bit(ICE_MDD_EVENT_PENDING, pf->state) ||
	    test_bit(ICE_VFLR_EVENT_PENDING, pf->state) ||
	    test_bit(ICE_MAILBOXQ_EVENT_PENDING, pf->state) ||
	    test_bit(ICE_FD_VF_FLUSH_CTX, pf->state) ||
	    test_bit(ICE_SIDEBANDQ_EVENT_PENDING, pf->state) ||
	    test_bit(ICE_ADMINQ_EVENT_PENDING, pf->state))
		mod_timer(&pf->serv_tmr, jiffies);
}

/**
 * ice_set_ctrlq_len - helper function to set controlq length
 * @hw: pointer to the HW instance
 */
static void ice_set_ctrlq_len(struct ice_hw *hw)
{
	hw->adminq.num_rq_entries = ICE_AQ_LEN;
	hw->adminq.num_sq_entries = ICE_AQ_LEN;
	hw->adminq.rq_buf_size = ICE_AQ_MAX_BUF_LEN;
	hw->adminq.sq_buf_size = ICE_AQ_MAX_BUF_LEN;
	hw->mailboxq.num_rq_entries = PF_MBX_ARQLEN_ARQLEN_M;
	hw->mailboxq.num_sq_entries = ICE_MBXSQ_LEN;
	hw->mailboxq.rq_buf_size = ICE_MBXQ_MAX_BUF_LEN;
	hw->mailboxq.sq_buf_size = ICE_MBXQ_MAX_BUF_LEN;
	hw->sbq.num_rq_entries = ICE_SBQ_LEN;
	hw->sbq.num_sq_entries = ICE_SBQ_LEN;
	hw->sbq.rq_buf_size = ICE_SBQ_MAX_BUF_LEN;
	hw->sbq.sq_buf_size = ICE_SBQ_MAX_BUF_LEN;
}

/**
 * ice_schedule_reset - schedule a reset
 * @pf: board private structure
 * @reset: reset being requested
 */
int ice_schedule_reset(struct ice_pf *pf, enum ice_reset_req reset)
{
	struct device *dev = ice_pf_to_dev(pf);

	/* bail out if earlier reset has failed */
	if (test_bit(ICE_RESET_FAILED, pf->state)) {
		dev_dbg(dev, "earlier reset has failed\n");
		return -EIO;
	}
	/* bail if reset/recovery already in progress */
	if (ice_is_reset_in_progress(pf->state)) {
		dev_dbg(dev, "Reset already in progress\n");
		return -EBUSY;
	}

	switch (reset) {
	case ICE_RESET_PFR:
		set_bit(ICE_PFR_REQ, pf->state);
		break;
	case ICE_RESET_CORER:
		set_bit(ICE_CORER_REQ, pf->state);
		break;
	case ICE_RESET_GLOBR:
		set_bit(ICE_GLOBR_REQ, pf->state);
		break;
	default:
		return -EINVAL;
	}

	ice_service_task_schedule(pf);
	return 0;
}

/**
 * ice_irq_affinity_notify - Callback for affinity changes
 * @notify: context as to what irq was changed
 * @mask: the new affinity mask
 *
 * This is a callback function used by the irq_set_affinity_notifier function
 * so that we may register to receive changes to the irq affinity masks.
 */
static void
ice_irq_affinity_notify(struct irq_affinity_notify *notify,
			const cpumask_t *mask)
{
	struct ice_q_vector *q_vector =
		container_of(notify, struct ice_q_vector, affinity_notify);

	cpumask_copy(&q_vector->affinity_mask, mask);
}

/**
 * ice_irq_affinity_release - Callback for affinity notifier release
 * @ref: internal core kernel usage
 *
 * This is a callback function used by the irq_set_affinity_notifier function
 * to inform the current notification subscriber that they will no longer
 * receive notifications.
 */
static void ice_irq_affinity_release(struct kref __always_unused *ref) {}

/**
 * ice_vsi_ena_irq - Enable IRQ for the given VSI
 * @vsi: the VSI being configured
 */
static int ice_vsi_ena_irq(struct ice_vsi *vsi)
{
	struct ice_hw *hw = &vsi->back->hw;
	int i;

	ice_for_each_q_vector(vsi, i)
		ice_irq_dynamic_ena(hw, vsi, vsi->q_vectors[i]);

	ice_flush(hw);
	return 0;
}

/**
 * ice_vsi_req_irq_msix - get MSI-X vectors from the OS for the VSI
 * @vsi: the VSI being configured
 * @basename: name for the vector
 */
static int ice_vsi_req_irq_msix(struct ice_vsi *vsi, char *basename)
{
	int q_vectors = vsi->num_q_vectors;
	struct ice_pf *pf = vsi->back;
	struct device *dev;
	int rx_int_idx = 0;
	int tx_int_idx = 0;
	int vector, err;
	int irq_num;

	dev = ice_pf_to_dev(pf);
	for (vector = 0; vector < q_vectors; vector++) {
		struct ice_q_vector *q_vector = vsi->q_vectors[vector];

		irq_num = q_vector->irq.virq;

		if (q_vector->tx.tx_ring && q_vector->rx.rx_ring) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-%s-%d", basename, "TxRx", rx_int_idx++);
			tx_int_idx++;
		} else if (q_vector->rx.rx_ring) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-%s-%d", basename, "rx", rx_int_idx++);
		} else if (q_vector->tx.tx_ring) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-%s-%d", basename, "tx", tx_int_idx++);
		} else {
			/* skip this unused q_vector */
			continue;
		}
		if (vsi->type == ICE_VSI_CTRL && vsi->vf)
			err = devm_request_irq(dev, irq_num, vsi->irq_handler,
					       IRQF_SHARED, q_vector->name,
					       q_vector);
		else
			err = devm_request_irq(dev, irq_num, vsi->irq_handler,
					       0, q_vector->name, q_vector);
		if (err) {
			netdev_err(vsi->netdev, "MSIX request_irq failed, error: %d\n",
				   err);
			goto free_q_irqs;
		}

		/* register for affinity change notifications */
		if (!IS_ENABLED(CONFIG_RFS_ACCEL)) {
			struct irq_affinity_notify *affinity_notify;

			affinity_notify = &q_vector->affinity_notify;
			affinity_notify->notify = ice_irq_affinity_notify;
			affinity_notify->release = ice_irq_affinity_release;
			irq_set_affinity_notifier(irq_num, affinity_notify);
		}

		/* assign the mask for this irq */
		irq_update_affinity_hint(irq_num, &q_vector->affinity_mask);
	}

	err = ice_set_cpu_rx_rmap(vsi);
	if (err) {
		netdev_err(vsi->netdev, "Failed to setup CPU RMAP on VSI %u: %pe\n",
			   vsi->vsi_num, ERR_PTR(err));
		goto free_q_irqs;
	}

	vsi->irqs_ready = true;
	return 0;

free_q_irqs:
	while (vector--) {
		irq_num = vsi->q_vectors[vector]->irq.virq;
		if (!IS_ENABLED(CONFIG_RFS_ACCEL))
			irq_set_affinity_notifier(irq_num, NULL);
		irq_update_affinity_hint(irq_num, NULL);
		devm_free_irq(dev, irq_num, &vsi->q_vectors[vector]);
	}
	return err;
}

/**
 * ice_xdp_alloc_setup_rings - Allocate and setup Tx rings for XDP
 * @vsi: VSI to setup Tx rings used by XDP
 *
 * Return 0 on success and negative value on error
 */
static int ice_xdp_alloc_setup_rings(struct ice_vsi *vsi)
{
	struct device *dev = ice_pf_to_dev(vsi->back);
	struct ice_tx_desc *tx_desc;
	int i, j;

	ice_for_each_xdp_txq(vsi, i) {
		u16 xdp_q_idx = vsi->alloc_txq + i;
		struct ice_ring_stats *ring_stats;
		struct ice_tx_ring *xdp_ring;

		xdp_ring = kzalloc(sizeof(*xdp_ring), GFP_KERNEL);
		if (!xdp_ring)
			goto free_xdp_rings;

		ring_stats = kzalloc(sizeof(*ring_stats), GFP_KERNEL);
		if (!ring_stats) {
			ice_free_tx_ring(xdp_ring);
			goto free_xdp_rings;
		}

		xdp_ring->ring_stats = ring_stats;
		xdp_ring->q_index = xdp_q_idx;
		xdp_ring->reg_idx = vsi->txq_map[xdp_q_idx];
		xdp_ring->vsi = vsi;
		xdp_ring->netdev = NULL;
		xdp_ring->dev = dev;
		xdp_ring->count = vsi->num_tx_desc;
		WRITE_ONCE(vsi->xdp_rings[i], xdp_ring);
		if (ice_setup_tx_ring(xdp_ring))
			goto free_xdp_rings;
		ice_set_ring_xdp(xdp_ring);
		spin_lock_init(&xdp_ring->tx_lock);
		for (j = 0; j < xdp_ring->count; j++) {
			tx_desc = ICE_TX_DESC(xdp_ring, j);
			tx_desc->cmd_type_offset_bsz = 0;
		}
	}

	return 0;

free_xdp_rings:
	for (; i >= 0; i--) {
		if (vsi->xdp_rings[i] && vsi->xdp_rings[i]->desc) {
			kfree_rcu(vsi->xdp_rings[i]->ring_stats, rcu);
			vsi->xdp_rings[i]->ring_stats = NULL;
			ice_free_tx_ring(vsi->xdp_rings[i]);
		}
	}
	return -ENOMEM;
}

/**
 * ice_vsi_assign_bpf_prog - set or clear bpf prog pointer on VSI
 * @vsi: VSI to set the bpf prog on
 * @prog: the bpf prog pointer
 */
static void ice_vsi_assign_bpf_prog(struct ice_vsi *vsi, struct bpf_prog *prog)
{
	struct bpf_prog *old_prog;
	int i;

	old_prog = xchg(&vsi->xdp_prog, prog);
	ice_for_each_rxq(vsi, i)
		WRITE_ONCE(vsi->rx_rings[i]->xdp_prog, vsi->xdp_prog);

	if (old_prog)
		bpf_prog_put(old_prog);
}

static struct ice_tx_ring *ice_xdp_ring_from_qid(struct ice_vsi *vsi, int qid)
{
	struct ice_q_vector *q_vector;
	struct ice_tx_ring *ring;

	if (static_key_enabled(&ice_xdp_locking_key))
		return vsi->xdp_rings[qid % vsi->num_xdp_txq];

	q_vector = vsi->rx_rings[qid]->q_vector;
	ice_for_each_tx_ring(ring, q_vector->tx)
		if (ice_ring_is_xdp(ring))
			return ring;

	return NULL;
}

/**
 * ice_map_xdp_rings - Map XDP rings to interrupt vectors
 * @vsi: the VSI with XDP rings being configured
 *
 * Map XDP rings to interrupt vectors and perform the configuration steps
 * dependent on the mapping.
 */
void ice_map_xdp_rings(struct ice_vsi *vsi)
{
	int xdp_rings_rem = vsi->num_xdp_txq;
	int v_idx, q_idx;

	/* follow the logic from ice_vsi_map_rings_to_vectors */
	ice_for_each_q_vector(vsi, v_idx) {
		struct ice_q_vector *q_vector = vsi->q_vectors[v_idx];
		int xdp_rings_per_v, q_id, q_base;

		xdp_rings_per_v = DIV_ROUND_UP(xdp_rings_rem,
					       vsi->num_q_vectors - v_idx);
		q_base = vsi->num_xdp_txq - xdp_rings_rem;

		for (q_id = q_base; q_id < (q_base + xdp_rings_per_v); q_id++) {
			struct ice_tx_ring *xdp_ring = vsi->xdp_rings[q_id];

			xdp_ring->q_vector = q_vector;
			xdp_ring->next = q_vector->tx.tx_ring;
			q_vector->tx.tx_ring = xdp_ring;
		}
		xdp_rings_rem -= xdp_rings_per_v;
	}

	ice_for_each_rxq(vsi, q_idx) {
		vsi->rx_rings[q_idx]->xdp_ring = ice_xdp_ring_from_qid(vsi,
								       q_idx);
		ice_tx_xsk_pool(vsi, q_idx);
	}
}

/**
 * ice_prepare_xdp_rings - Allocate, configure and setup Tx rings for XDP
 * @vsi: VSI to bring up Tx rings used by XDP
 * @prog: bpf program that will be assigned to VSI
 * @cfg_type: create from scratch or restore the existing configuration
 *
 * Return 0 on success and negative value on error
 */
int ice_prepare_xdp_rings(struct ice_vsi *vsi, struct bpf_prog *prog,
			  enum ice_xdp_cfg cfg_type)
{
	u16 max_txqs[ICE_MAX_TRAFFIC_CLASS] = { 0 };
	struct ice_pf *pf = vsi->back;
	struct ice_qs_cfg xdp_qs_cfg = {
		.qs_mutex = &pf->avail_q_mutex,
		.pf_map = pf->avail_txqs,
		.pf_map_size = pf->max_pf_txqs,
		.q_count = vsi->num_xdp_txq,
		.scatter_count = ICE_MAX_SCATTER_TXQS,
		.vsi_map = vsi->txq_map,
		.vsi_map_offset = vsi->alloc_txq,
		.mapping_mode = ICE_VSI_MAP_CONTIG
	};
	struct device *dev;
	int status, i;

	dev = ice_pf_to_dev(pf);
	vsi->xdp_rings = devm_kcalloc(dev, vsi->num_xdp_txq,
				      sizeof(*vsi->xdp_rings), GFP_KERNEL);
	if (!vsi->xdp_rings)
		return -ENOMEM;

	vsi->xdp_mapping_mode = xdp_qs_cfg.mapping_mode;
	if (__ice_vsi_get_qs(&xdp_qs_cfg))
		goto err_map_xdp;

	if (static_key_enabled(&ice_xdp_locking_key))
		netdev_warn(vsi->netdev,
			    "Could not allocate one XDP Tx ring per CPU, XDP_TX/XDP_REDIRECT actions will be slower\n");

	if (ice_xdp_alloc_setup_rings(vsi))
		goto clear_xdp_rings;

	/* omit the scheduler update if in reset path; XDP queues will be
	 * taken into account at the end of ice_vsi_rebuild, where
	 * ice_cfg_vsi_lan is being called
	 */
	if (cfg_type == ICE_XDP_CFG_PART)
		return 0;

	ice_map_xdp_rings(vsi);

	/* tell the Tx scheduler that right now we have
	 * additional queues
	 */
	for (i = 0; i < vsi->tc_cfg.numtc; i++)
		max_txqs[i] = vsi->num_txq + vsi->num_xdp_txq;

	status = ice_cfg_vsi_lan(vsi->port_info, vsi->idx, vsi->tc_cfg.ena_tc,
				 max_txqs);
	if (status) {
		dev_err(dev, "Failed VSI LAN queue config for XDP, error: %d\n",
			status);
		goto clear_xdp_rings;
	}

	/* assign the prog only when it's not already present on VSI;
	 * this flow is a subject of both ethtool -L and ndo_bpf flows;
	 * VSI rebuild that happens under ethtool -L can expose us to
	 * the bpf_prog refcount issues as we would be swapping same
	 * bpf_prog pointers from vsi->xdp_prog and calling bpf_prog_put
	 * on it as it would be treated as an 'old_prog'; for ndo_bpf
	 * this is not harmful as dev_xdp_install bumps the refcount
	 * before calling the op exposed by the driver;
	 */
	if (!ice_is_xdp_ena_vsi(vsi))
		ice_vsi_assign_bpf_prog(vsi, prog);

	return 0;
clear_xdp_rings:
	ice_for_each_xdp_txq(vsi, i)
		if (vsi->xdp_rings[i]) {
			kfree_rcu(vsi->xdp_rings[i], rcu);
			vsi->xdp_rings[i] = NULL;
		}

err_map_xdp:
	mutex_lock(&pf->avail_q_mutex);
	ice_for_each_xdp_txq(vsi, i) {
		clear_bit(vsi->txq_map[i + vsi->alloc_txq], pf->avail_txqs);
		vsi->txq_map[i + vsi->alloc_txq] = ICE_INVAL_Q_INDEX;
	}
	mutex_unlock(&pf->avail_q_mutex);

	devm_kfree(dev, vsi->xdp_rings);
	return -ENOMEM;
}

/**
 * ice_destroy_xdp_rings - undo the configuration made by ice_prepare_xdp_rings
 * @vsi: VSI to remove XDP rings
 * @cfg_type: disable XDP permanently or allow it to be restored later
 *
 * Detach XDP rings from irq vectors, clean up the PF bitmap and free
 * resources
 */
int ice_destroy_xdp_rings(struct ice_vsi *vsi, enum ice_xdp_cfg cfg_type)
{
	u16 max_txqs[ICE_MAX_TRAFFIC_CLASS] = { 0 };
	struct ice_pf *pf = vsi->back;
	int i, v_idx;

	/* q_vectors are freed in reset path so there's no point in detaching
	 * rings
	 */
	if (cfg_type == ICE_XDP_CFG_PART)
		goto free_qmap;

	ice_for_each_q_vector(vsi, v_idx) {
		struct ice_q_vector *q_vector = vsi->q_vectors[v_idx];
		struct ice_tx_ring *ring;

		ice_for_each_tx_ring(ring, q_vector->tx)
			if (!ring->tx_buf || !ice_ring_is_xdp(ring))
				break;

		/* restore the value of last node prior to XDP setup */
		q_vector->tx.tx_ring = ring;
	}

free_qmap:
	mutex_lock(&pf->avail_q_mutex);
	ice_for_each_xdp_txq(vsi, i) {
		clear_bit(vsi->txq_map[i + vsi->alloc_txq], pf->avail_txqs);
		vsi->txq_map[i + vsi->alloc_txq] = ICE_INVAL_Q_INDEX;
	}
	mutex_unlock(&pf->avail_q_mutex);

	ice_for_each_xdp_txq(vsi, i)
		if (vsi->xdp_rings[i]) {
			if (vsi->xdp_rings[i]->desc) {
				synchronize_rcu();
				ice_free_tx_ring(vsi->xdp_rings[i]);
			}
			kfree_rcu(vsi->xdp_rings[i]->ring_stats, rcu);
			vsi->xdp_rings[i]->ring_stats = NULL;
			kfree_rcu(vsi->xdp_rings[i], rcu);
			vsi->xdp_rings[i] = NULL;
		}

	devm_kfree(ice_pf_to_dev(pf), vsi->xdp_rings);
	vsi->xdp_rings = NULL;

	if (static_key_enabled(&ice_xdp_locking_key))
		static_branch_dec(&ice_xdp_locking_key);

	if (cfg_type == ICE_XDP_CFG_PART)
		return 0;

	ice_vsi_assign_bpf_prog(vsi, NULL);

	/* notify Tx scheduler that we destroyed XDP queues and bring
	 * back the old number of child nodes
	 */
	for (i = 0; i < vsi->tc_cfg.numtc; i++)
		max_txqs[i] = vsi->num_txq;

	/* change number of XDP Tx queues to 0 */
	vsi->num_xdp_txq = 0;

	return ice_cfg_vsi_lan(vsi->port_info, vsi->idx, vsi->tc_cfg.ena_tc,
			       max_txqs);
}

/**
 * ice_vsi_rx_napi_schedule - Schedule napi on RX queues from VSI
 * @vsi: VSI to schedule napi on
 */
static void ice_vsi_rx_napi_schedule(struct ice_vsi *vsi)
{
	int i;

	ice_for_each_rxq(vsi, i) {
		struct ice_rx_ring *rx_ring = vsi->rx_rings[i];

		if (READ_ONCE(rx_ring->xsk_pool))
			napi_schedule(&rx_ring->q_vector->napi);
	}
}

/**
 * ice_vsi_determine_xdp_res - figure out how many Tx qs can XDP have
 * @vsi: VSI to determine the count of XDP Tx qs
 *
 * returns 0 if Tx qs count is higher than at least half of CPU count,
 * -ENOMEM otherwise
 */
int ice_vsi_determine_xdp_res(struct ice_vsi *vsi)
{
	u16 avail = ice_get_avail_txq_count(vsi->back);
	u16 cpus = num_possible_cpus();

	if (avail < cpus / 2)
		return -ENOMEM;

	vsi->num_xdp_txq = min_t(u16, avail, cpus);

	if (vsi->num_xdp_txq < cpus)
		static_branch_inc(&ice_xdp_locking_key);

	return 0;
}

/**
 * ice_max_xdp_frame_size - returns the maximum allowed frame size for XDP
 * @vsi: Pointer to VSI structure
 */
static int ice_max_xdp_frame_size(struct ice_vsi *vsi)
{
	if (test_bit(ICE_FLAG_LEGACY_RX, vsi->back->flags))
		return ICE_RXBUF_1664;
	else
		return ICE_RXBUF_3072;
}

/**
 * ice_xdp_setup_prog - Add or remove XDP eBPF program
 * @vsi: VSI to setup XDP for
 * @prog: XDP program
 * @extack: netlink extended ack
 */
static int
ice_xdp_setup_prog(struct ice_vsi *vsi, struct bpf_prog *prog,
		   struct netlink_ext_ack *extack)
{
	unsigned int frame_size = vsi->netdev->mtu + ICE_ETH_PKT_HDR_PAD;
	bool if_running = netif_running(vsi->netdev);
	int ret = 0, xdp_ring_err = 0;

	if (prog && !prog->aux->xdp_has_frags) {
		if (frame_size > ice_max_xdp_frame_size(vsi)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "MTU is too large for linear frames and XDP prog does not support frags");
			return -EOPNOTSUPP;
		}
	}

	/* hot swap progs and avoid toggling link */
	if (ice_is_xdp_ena_vsi(vsi) == !!prog) {
		ice_vsi_assign_bpf_prog(vsi, prog);
		return 0;
	}

	/* need to stop netdev while setting up the program for Rx rings */
	if (if_running && !test_and_set_bit(ICE_VSI_DOWN, vsi->state)) {
		ret = ice_down(vsi);
		if (ret) {
			NL_SET_ERR_MSG_MOD(extack, "Preparing device for XDP attach failed");
			return ret;
		}
	}

	if (!ice_is_xdp_ena_vsi(vsi) && prog) {
		xdp_ring_err = ice_vsi_determine_xdp_res(vsi);
		if (xdp_ring_err) {
			NL_SET_ERR_MSG_MOD(extack, "Not enough Tx resources for XDP");
		} else {
			xdp_ring_err = ice_prepare_xdp_rings(vsi, prog,
							     ICE_XDP_CFG_FULL);
			if (xdp_ring_err)
				NL_SET_ERR_MSG_MOD(extack, "Setting up XDP Tx resources failed");
		}
		xdp_features_set_redirect_target(vsi->netdev, true);
		/* reallocate Rx queues that are used for zero-copy */
		xdp_ring_err = ice_realloc_zc_buf(vsi, true);
		if (xdp_ring_err)
			NL_SET_ERR_MSG_MOD(extack, "Setting up XDP Rx resources failed");
	} else if (ice_is_xdp_ena_vsi(vsi) && !prog) {
		xdp_features_clear_redirect_target(vsi->netdev);
		xdp_ring_err = ice_destroy_xdp_rings(vsi, ICE_XDP_CFG_FULL);
		if (xdp_ring_err)
			NL_SET_ERR_MSG_MOD(extack, "Freeing XDP Tx resources failed");
		/* reallocate Rx queues that were used for zero-copy */
		xdp_ring_err = ice_realloc_zc_buf(vsi, false);
		if (xdp_ring_err)
			NL_SET_ERR_MSG_MOD(extack, "Freeing XDP Rx resources failed");
	}

	if (if_running)
		ret = ice_up(vsi);

	if (!ret && prog)
		ice_vsi_rx_napi_schedule(vsi);

	return (ret || xdp_ring_err) ? -ENOMEM : 0;
}

/**
 * ice_xdp_safe_mode - XDP handler for safe mode
 * @dev: netdevice
 * @xdp: XDP command
 */
static int ice_xdp_safe_mode(struct net_device __always_unused *dev,
			     struct netdev_bpf *xdp)
{
	NL_SET_ERR_MSG_MOD(xdp->extack,
			   "Please provide working DDP firmware package in order to use XDP\n"
			   "Refer to Documentation/networking/device_drivers/ethernet/intel/ice.rst");
	return -EOPNOTSUPP;
}

/**
 * ice_xdp - implements XDP handler
 * @dev: netdevice
 * @xdp: XDP command
 */
static int ice_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	struct ice_netdev_priv *np = netdev_priv(dev);
	struct ice_vsi *vsi = np->vsi;

	if (vsi->type != ICE_VSI_PF) {
		NL_SET_ERR_MSG_MOD(xdp->extack, "XDP can be loaded only on PF VSI");
		return -EINVAL;
	}

	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return ice_xdp_setup_prog(vsi, xdp->prog, xdp->extack);
	case XDP_SETUP_XSK_POOL:
		return ice_xsk_pool_setup(vsi, xdp->xsk.pool,
					  xdp->xsk.queue_id);
	default:
		return -EINVAL;
	}
}

/**
 * ice_ena_misc_vector - enable the non-queue interrupts
 * @pf: board private structure
 */
static void ice_ena_misc_vector(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;
	u32 pf_intr_start_offset;
	u32 val;

	/* Disable anti-spoof detection interrupt to prevent spurious event
	 * interrupts during a function reset. Anti-spoof functionally is
	 * still supported.
	 */
	val = rd32(hw, GL_MDCK_TX_TDPU);
	val |= GL_MDCK_TX_TDPU_RCU_ANTISPOOF_ITR_DIS_M;
	wr32(hw, GL_MDCK_TX_TDPU, val);

	/* clear things first */
	wr32(hw, PFINT_OICR_ENA, 0);	/* disable all */
	rd32(hw, PFINT_OICR);		/* read to clear */

	val = (PFINT_OICR_ECC_ERR_M |
	       PFINT_OICR_MAL_DETECT_M |
	       PFINT_OICR_GRST_M |
	       PFINT_OICR_PCI_EXCEPTION_M |
	       PFINT_OICR_VFLR_M |
	       PFINT_OICR_HMC_ERR_M |
	       PFINT_OICR_PE_PUSH_M |
	       PFINT_OICR_PE_CRITERR_M);

	wr32(hw, PFINT_OICR_ENA, val);

	/* SW_ITR_IDX = 0, but don't change INTENA */
	wr32(hw, GLINT_DYN_CTL(pf->oicr_irq.index),
	     GLINT_DYN_CTL_SW_ITR_INDX_M | GLINT_DYN_CTL_INTENA_MSK_M);

	if (!pf->hw.dev_caps.ts_dev_info.ts_ll_int_read)
		return;
	pf_intr_start_offset = rd32(hw, PFINT_ALLOC) & PFINT_ALLOC_FIRST;
	wr32(hw, GLINT_DYN_CTL(pf->ll_ts_irq.index + pf_intr_start_offset),
	     GLINT_DYN_CTL_SW_ITR_INDX_M | GLINT_DYN_CTL_INTENA_MSK_M);
}

/**
 * ice_ll_ts_intr - ll_ts interrupt handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 */
static irqreturn_t ice_ll_ts_intr(int __always_unused irq, void *data)
{
	struct ice_pf *pf = data;
	u32 pf_intr_start_offset;
	struct ice_ptp_tx *tx;
	unsigned long flags;
	struct ice_hw *hw;
	u32 val;
	u8 idx;

	hw = &pf->hw;
	tx = &pf->ptp.port.tx;
	spin_lock_irqsave(&tx->lock, flags);
	ice_ptp_complete_tx_single_tstamp(tx);

	idx = find_next_bit_wrap(tx->in_use, tx->len,
				 tx->last_ll_ts_idx_read + 1);
	if (idx != tx->len)
		ice_ptp_req_tx_single_tstamp(tx, idx);
	spin_unlock_irqrestore(&tx->lock, flags);

	val = GLINT_DYN_CTL_INTENA_M | GLINT_DYN_CTL_CLEARPBA_M |
	      (ICE_ITR_NONE << GLINT_DYN_CTL_ITR_INDX_S);
	pf_intr_start_offset = rd32(hw, PFINT_ALLOC) & PFINT_ALLOC_FIRST;
	wr32(hw, GLINT_DYN_CTL(pf->ll_ts_irq.index + pf_intr_start_offset),
	     val);

	return IRQ_HANDLED;
}

/**
 * ice_misc_intr - misc interrupt handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 */
static irqreturn_t ice_misc_intr(int __always_unused irq, void *data)
{
	struct ice_pf *pf = (struct ice_pf *)data;
	irqreturn_t ret = IRQ_HANDLED;
	struct ice_hw *hw = &pf->hw;
	struct device *dev;
	u32 oicr, ena_mask;

	dev = ice_pf_to_dev(pf);
	set_bit(ICE_ADMINQ_EVENT_PENDING, pf->state);
	set_bit(ICE_MAILBOXQ_EVENT_PENDING, pf->state);
	set_bit(ICE_SIDEBANDQ_EVENT_PENDING, pf->state);

	oicr = rd32(hw, PFINT_OICR);
	ena_mask = rd32(hw, PFINT_OICR_ENA);

	if (oicr & PFINT_OICR_SWINT_M) {
		ena_mask &= ~PFINT_OICR_SWINT_M;
		pf->sw_int_count++;
	}

	if (oicr & PFINT_OICR_MAL_DETECT_M) {
		ena_mask &= ~PFINT_OICR_MAL_DETECT_M;
		set_bit(ICE_MDD_EVENT_PENDING, pf->state);
	}
	if (oicr & PFINT_OICR_VFLR_M) {
		/* disable any further VFLR event notifications */
		if (test_bit(ICE_VF_RESETS_DISABLED, pf->state)) {
			u32 reg = rd32(hw, PFINT_OICR_ENA);

			reg &= ~PFINT_OICR_VFLR_M;
			wr32(hw, PFINT_OICR_ENA, reg);
		} else {
			ena_mask &= ~PFINT_OICR_VFLR_M;
			set_bit(ICE_VFLR_EVENT_PENDING, pf->state);
		}
	}

	if (oicr & PFINT_OICR_GRST_M) {
		u32 reset;

		/* we have a reset warning */
		ena_mask &= ~PFINT_OICR_GRST_M;
		reset = FIELD_GET(GLGEN_RSTAT_RESET_TYPE_M,
				  rd32(hw, GLGEN_RSTAT));

		if (reset == ICE_RESET_CORER)
			pf->corer_count++;
		else if (reset == ICE_RESET_GLOBR)
			pf->globr_count++;
		else if (reset == ICE_RESET_EMPR)
			pf->empr_count++;
		else
			dev_dbg(dev, "Invalid reset type %d\n", reset);

		/* If a reset cycle isn't already in progress, we set a bit in
		 * pf->state so that the service task can start a reset/rebuild.
		 */
		if (!test_and_set_bit(ICE_RESET_OICR_RECV, pf->state)) {
			if (reset == ICE_RESET_CORER)
				set_bit(ICE_CORER_RECV, pf->state);
			else if (reset == ICE_RESET_GLOBR)
				set_bit(ICE_GLOBR_RECV, pf->state);
			else
				set_bit(ICE_EMPR_RECV, pf->state);

			/* There are couple of different bits at play here.
			 * hw->reset_ongoing indicates whether the hardware is
			 * in reset. This is set to true when a reset interrupt
			 * is received and set back to false after the driver
			 * has determined that the hardware is out of reset.
			 *
			 * ICE_RESET_OICR_RECV in pf->state indicates
			 * that a post reset rebuild is required before the
			 * driver is operational again. This is set above.
			 *
			 * As this is the start of the reset/rebuild cycle, set
			 * both to indicate that.
			 */
			hw->reset_ongoing = true;
		}
	}

	if (oicr & PFINT_OICR_TSYN_TX_M) {
		ena_mask &= ~PFINT_OICR_TSYN_TX_M;
		if (ice_pf_state_is_nominal(pf) &&
		    pf->hw.dev_caps.ts_dev_info.ts_ll_int_read) {
			struct ice_ptp_tx *tx = &pf->ptp.port.tx;
			unsigned long flags;
			u8 idx;

			spin_lock_irqsave(&tx->lock, flags);
			idx = find_next_bit_wrap(tx->in_use, tx->len,
						 tx->last_ll_ts_idx_read + 1);
			if (idx != tx->len)
				ice_ptp_req_tx_single_tstamp(tx, idx);
			spin_unlock_irqrestore(&tx->lock, flags);
		} else if (ice_ptp_pf_handles_tx_interrupt(pf)) {
			set_bit(ICE_MISC_THREAD_TX_TSTAMP, pf->misc_thread);
			ret = IRQ_WAKE_THREAD;
		}
	}

	if (oicr & PFINT_OICR_TSYN_EVNT_M) {
		u8 tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;
		u32 gltsyn_stat = rd32(hw, GLTSYN_STAT(tmr_idx));

		ena_mask &= ~PFINT_OICR_TSYN_EVNT_M;

		if (ice_pf_src_tmr_owned(pf)) {
			/* Save EVENTs from GLTSYN register */
			pf->ptp.ext_ts_irq |= gltsyn_stat &
					      (GLTSYN_STAT_EVENT0_M |
					       GLTSYN_STAT_EVENT1_M |
					       GLTSYN_STAT_EVENT2_M);

			ice_ptp_extts_event(pf);
		}
	}

#define ICE_AUX_CRIT_ERR (PFINT_OICR_PE_CRITERR_M | PFINT_OICR_HMC_ERR_M | PFINT_OICR_PE_PUSH_M)
	if (oicr & ICE_AUX_CRIT_ERR) {
		pf->oicr_err_reg |= oicr;
		set_bit(ICE_AUX_ERR_PENDING, pf->state);
		ena_mask &= ~ICE_AUX_CRIT_ERR;
	}

	/* Report any remaining unexpected interrupts */
	oicr &= ena_mask;
	if (oicr) {
		dev_dbg(dev, "unhandled interrupt oicr=0x%08x\n", oicr);
		/* If a critical error is pending there is no choice but to
		 * reset the device.
		 */
		if (oicr & (PFINT_OICR_PCI_EXCEPTION_M |
			    PFINT_OICR_ECC_ERR_M)) {
			set_bit(ICE_PFR_REQ, pf->state);
		}
	}
	ice_service_task_schedule(pf);
	if (ret == IRQ_HANDLED)
		ice_irq_dynamic_ena(hw, NULL, NULL);

	return ret;
}

/**
 * ice_misc_intr_thread_fn - misc interrupt thread function
 * @irq: interrupt number
 * @data: pointer to a q_vector
 */
static irqreturn_t ice_misc_intr_thread_fn(int __always_unused irq, void *data)
{
	struct ice_pf *pf = data;
	struct ice_hw *hw;

	hw = &pf->hw;

	if (ice_is_reset_in_progress(pf->state))
		goto skip_irq;

	if (test_and_clear_bit(ICE_MISC_THREAD_TX_TSTAMP, pf->misc_thread)) {
		/* Process outstanding Tx timestamps. If there is more work,
		 * re-arm the interrupt to trigger again.
		 */
		if (ice_ptp_process_ts(pf) == ICE_TX_TSTAMP_WORK_PENDING) {
			wr32(hw, PFINT_OICR, PFINT_OICR_TSYN_TX_M);
			ice_flush(hw);
		}
	}

skip_irq:
	ice_irq_dynamic_ena(hw, NULL, NULL);

	return IRQ_HANDLED;
}

/**
 * ice_dis_ctrlq_interrupts - disable control queue interrupts
 * @hw: pointer to HW structure
 */
static void ice_dis_ctrlq_interrupts(struct ice_hw *hw)
{
	/* disable Admin queue Interrupt causes */
	wr32(hw, PFINT_FW_CTL,
	     rd32(hw, PFINT_FW_CTL) & ~PFINT_FW_CTL_CAUSE_ENA_M);

	/* disable Mailbox queue Interrupt causes */
	wr32(hw, PFINT_MBX_CTL,
	     rd32(hw, PFINT_MBX_CTL) & ~PFINT_MBX_CTL_CAUSE_ENA_M);

	wr32(hw, PFINT_SB_CTL,
	     rd32(hw, PFINT_SB_CTL) & ~PFINT_SB_CTL_CAUSE_ENA_M);

	/* disable Control queue Interrupt causes */
	wr32(hw, PFINT_OICR_CTL,
	     rd32(hw, PFINT_OICR_CTL) & ~PFINT_OICR_CTL_CAUSE_ENA_M);

	ice_flush(hw);
}

/**
 * ice_free_irq_msix_ll_ts- Unroll ll_ts vector setup
 * @pf: board private structure
 */
static void ice_free_irq_msix_ll_ts(struct ice_pf *pf)
{
	int irq_num = pf->ll_ts_irq.virq;

	synchronize_irq(irq_num);
	devm_free_irq(ice_pf_to_dev(pf), irq_num, pf);

	ice_free_irq(pf, pf->ll_ts_irq);
}

/**
 * ice_free_irq_msix_misc - Unroll misc vector setup
 * @pf: board private structure
 */
static void ice_free_irq_msix_misc(struct ice_pf *pf)
{
	int misc_irq_num = pf->oicr_irq.virq;
	struct ice_hw *hw = &pf->hw;

	ice_dis_ctrlq_interrupts(hw);

	/* disable OICR interrupt */
	wr32(hw, PFINT_OICR_ENA, 0);
	ice_flush(hw);

	synchronize_irq(misc_irq_num);
	devm_free_irq(ice_pf_to_dev(pf), misc_irq_num, pf);

	ice_free_irq(pf, pf->oicr_irq);
	if (pf->hw.dev_caps.ts_dev_info.ts_ll_int_read)
		ice_free_irq_msix_ll_ts(pf);
}

/**
 * ice_ena_ctrlq_interrupts - enable control queue interrupts
 * @hw: pointer to HW structure
 * @reg_idx: HW vector index to associate the control queue interrupts with
 */
static void ice_ena_ctrlq_interrupts(struct ice_hw *hw, u16 reg_idx)
{
	u32 val;

	val = ((reg_idx & PFINT_OICR_CTL_MSIX_INDX_M) |
	       PFINT_OICR_CTL_CAUSE_ENA_M);
	wr32(hw, PFINT_OICR_CTL, val);

	/* enable Admin queue Interrupt causes */
	val = ((reg_idx & PFINT_FW_CTL_MSIX_INDX_M) |
	       PFINT_FW_CTL_CAUSE_ENA_M);
	wr32(hw, PFINT_FW_CTL, val);

	/* enable Mailbox queue Interrupt causes */
	val = ((reg_idx & PFINT_MBX_CTL_MSIX_INDX_M) |
	       PFINT_MBX_CTL_CAUSE_ENA_M);
	wr32(hw, PFINT_MBX_CTL, val);

	if (!hw->dev_caps.ts_dev_info.ts_ll_int_read) {
		/* enable Sideband queue Interrupt causes */
		val = ((reg_idx & PFINT_SB_CTL_MSIX_INDX_M) |
		       PFINT_SB_CTL_CAUSE_ENA_M);
		wr32(hw, PFINT_SB_CTL, val);
	}

	ice_flush(hw);
}

/**
 * ice_req_irq_msix_misc - Setup the misc vector to handle non queue events
 * @pf: board private structure
 *
 * This sets up the handler for MSIX 0, which is used to manage the
 * non-queue interrupts, e.g. AdminQ and errors. This is not used
 * when in MSI or Legacy interrupt mode.
 */
static int ice_req_irq_msix_misc(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	u32 pf_intr_start_offset;
	struct msi_map irq;
	int err = 0;

	if (!pf->int_name[0])
		snprintf(pf->int_name, sizeof(pf->int_name) - 1, "%s-%s:misc",
			 dev_driver_string(dev), dev_name(dev));

	if (!pf->int_name_ll_ts[0])
		snprintf(pf->int_name_ll_ts, sizeof(pf->int_name_ll_ts) - 1,
			 "%s-%s:ll_ts", dev_driver_string(dev), dev_name(dev));
	/* Do not request IRQ but do enable OICR interrupt since settings are
	 * lost during reset. Note that this function is called only during
	 * rebuild path and not while reset is in progress.
	 */
	if (ice_is_reset_in_progress(pf->state))
		goto skip_req_irq;

	/* reserve one vector in irq_tracker for misc interrupts */
	irq = ice_alloc_irq(pf, false);
	if (irq.index < 0)
		return irq.index;

	pf->oicr_irq = irq;
	err = devm_request_threaded_irq(dev, pf->oicr_irq.virq, ice_misc_intr,
					ice_misc_intr_thread_fn, 0,
					pf->int_name, pf);
	if (err) {
		dev_err(dev, "devm_request_threaded_irq for %s failed: %d\n",
			pf->int_name, err);
		ice_free_irq(pf, pf->oicr_irq);
		return err;
	}

	/* reserve one vector in irq_tracker for ll_ts interrupt */
	if (!pf->hw.dev_caps.ts_dev_info.ts_ll_int_read)
		goto skip_req_irq;

	irq = ice_alloc_irq(pf, false);
	if (irq.index < 0)
		return irq.index;

	pf->ll_ts_irq = irq;
	err = devm_request_irq(dev, pf->ll_ts_irq.virq, ice_ll_ts_intr, 0,
			       pf->int_name_ll_ts, pf);
	if (err) {
		dev_err(dev, "devm_request_irq for %s failed: %d\n",
			pf->int_name_ll_ts, err);
		ice_free_irq(pf, pf->ll_ts_irq);
		return err;
	}

skip_req_irq:
	ice_ena_misc_vector(pf);

	ice_ena_ctrlq_interrupts(hw, pf->oicr_irq.index);
	/* This enables LL TS interrupt */
	pf_intr_start_offset = rd32(hw, PFINT_ALLOC) & PFINT_ALLOC_FIRST;
	if (pf->hw.dev_caps.ts_dev_info.ts_ll_int_read)
		wr32(hw, PFINT_SB_CTL,
		     ((pf->ll_ts_irq.index + pf_intr_start_offset) &
		      PFINT_SB_CTL_MSIX_INDX_M) | PFINT_SB_CTL_CAUSE_ENA_M);
	wr32(hw, GLINT_ITR(ICE_RX_ITR, pf->oicr_irq.index),
	     ITR_REG_ALIGN(ICE_ITR_8K) >> ICE_ITR_GRAN_S);

	ice_flush(hw);
	ice_irq_dynamic_ena(hw, NULL, NULL);

	return 0;
}

/**
 * ice_napi_add - register NAPI handler for the VSI
 * @vsi: VSI for which NAPI handler is to be registered
 *
 * This function is only called in the driver's load path. Registering the NAPI
 * handler is done in ice_vsi_alloc_q_vector() for all other cases (i.e. resume,
 * reset/rebuild, etc.)
 */
static void ice_napi_add(struct ice_vsi *vsi)
{
	int v_idx;

	if (!vsi->netdev)
		return;

	ice_for_each_q_vector(vsi, v_idx) {
		netif_napi_add(vsi->netdev, &vsi->q_vectors[v_idx]->napi,
			       ice_napi_poll);
		__ice_q_vector_set_napi_queues(vsi->q_vectors[v_idx], false);
	}
}

/**
 * ice_set_ops - set netdev and ethtools ops for the given netdev
 * @vsi: the VSI associated with the new netdev
 */
static void ice_set_ops(struct ice_vsi *vsi)
{
	struct net_device *netdev = vsi->netdev;
	struct ice_pf *pf = ice_netdev_to_pf(netdev);

	if (ice_is_safe_mode(pf)) {
		netdev->netdev_ops = &ice_netdev_safe_mode_ops;
		ice_set_ethtool_safe_mode_ops(netdev);
		return;
	}

	netdev->netdev_ops = &ice_netdev_ops;
	netdev->udp_tunnel_nic_info = &pf->hw.udp_tunnel_nic;
	netdev->xdp_metadata_ops = &ice_xdp_md_ops;
	ice_set_ethtool_ops(netdev);

	if (vsi->type != ICE_VSI_PF)
		return;

	netdev->xdp_features = NETDEV_XDP_ACT_BASIC | NETDEV_XDP_ACT_REDIRECT |
			       NETDEV_XDP_ACT_XSK_ZEROCOPY |
			       NETDEV_XDP_ACT_RX_SG;
	netdev->xdp_zc_max_segs = ICE_MAX_BUF_TXD;
}

/**
 * ice_set_netdev_features - set features for the given netdev
 * @netdev: netdev instance
 */
static void ice_set_netdev_features(struct net_device *netdev)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	bool is_dvm_ena = ice_is_dvm_ena(&pf->hw);
	netdev_features_t csumo_features;
	netdev_features_t vlano_features;
	netdev_features_t dflt_features;
	netdev_features_t tso_features;

	if (ice_is_safe_mode(pf)) {
		/* safe mode */
		netdev->features = NETIF_F_SG | NETIF_F_HIGHDMA;
		netdev->hw_features = netdev->features;
		return;
	}

	dflt_features = NETIF_F_SG	|
			NETIF_F_HIGHDMA	|
			NETIF_F_NTUPLE	|
			NETIF_F_RXHASH;

	csumo_features = NETIF_F_RXCSUM	  |
			 NETIF_F_IP_CSUM  |
			 NETIF_F_SCTP_CRC |
			 NETIF_F_IPV6_CSUM;

	vlano_features = NETIF_F_HW_VLAN_CTAG_FILTER |
			 NETIF_F_HW_VLAN_CTAG_TX     |
			 NETIF_F_HW_VLAN_CTAG_RX;

	/* Enable CTAG/STAG filtering by default in Double VLAN Mode (DVM) */
	if (is_dvm_ena)
		vlano_features |= NETIF_F_HW_VLAN_STAG_FILTER;

	tso_features = NETIF_F_TSO			|
		       NETIF_F_TSO_ECN			|
		       NETIF_F_TSO6			|
		       NETIF_F_GSO_GRE			|
		       NETIF_F_GSO_UDP_TUNNEL		|
		       NETIF_F_GSO_GRE_CSUM		|
		       NETIF_F_GSO_UDP_TUNNEL_CSUM	|
		       NETIF_F_GSO_PARTIAL		|
		       NETIF_F_GSO_IPXIP4		|
		       NETIF_F_GSO_IPXIP6		|
		       NETIF_F_GSO_UDP_L4;

	netdev->gso_partial_features |= NETIF_F_GSO_UDP_TUNNEL_CSUM |
					NETIF_F_GSO_GRE_CSUM;
	/* set features that user can change */
	netdev->hw_features = dflt_features | csumo_features |
			      vlano_features | tso_features;

	/* add support for HW_CSUM on packets with MPLS header */
	netdev->mpls_features =  NETIF_F_HW_CSUM |
				 NETIF_F_TSO     |
				 NETIF_F_TSO6;

	/* enable features */
	netdev->features |= netdev->hw_features;

	netdev->hw_features |= NETIF_F_HW_TC;
	netdev->hw_features |= NETIF_F_LOOPBACK;

	/* encap and VLAN devices inherit default, csumo and tso features */
	netdev->hw_enc_features |= dflt_features | csumo_features |
				   tso_features;
	netdev->vlan_features |= dflt_features | csumo_features |
				 tso_features;

	/* advertise support but don't enable by default since only one type of
	 * VLAN offload can be enabled at a time (i.e. CTAG or STAG). When one
	 * type turns on the other has to be turned off. This is enforced by the
	 * ice_fix_features() ndo callback.
	 */
	if (is_dvm_ena)
		netdev->hw_features |= NETIF_F_HW_VLAN_STAG_RX |
			NETIF_F_HW_VLAN_STAG_TX;

	/* Leave CRC / FCS stripping enabled by default, but allow the value to
	 * be changed at runtime
	 */
	netdev->hw_features |= NETIF_F_RXFCS;

	netif_set_tso_max_size(netdev, ICE_MAX_TSO_SIZE);
}

/**
 * ice_fill_rss_lut - Fill the RSS lookup table with default values
 * @lut: Lookup table
 * @rss_table_size: Lookup table size
 * @rss_size: Range of queue number for hashing
 */
void ice_fill_rss_lut(u8 *lut, u16 rss_table_size, u16 rss_size)
{
	u16 i;

	for (i = 0; i < rss_table_size; i++)
		lut[i] = i % rss_size;
}

/**
 * ice_pf_vsi_setup - Set up a PF VSI
 * @pf: board private structure
 * @pi: pointer to the port_info instance
 *
 * Returns pointer to the successfully allocated VSI software struct
 * on success, otherwise returns NULL on failure.
 */
static struct ice_vsi *
ice_pf_vsi_setup(struct ice_pf *pf, struct ice_port_info *pi)
{
	struct ice_vsi_cfg_params params = {};

	params.type = ICE_VSI_PF;
	params.port_info = pi;
	params.flags = ICE_VSI_FLAG_INIT;

	return ice_vsi_setup(pf, &params);
}

static struct ice_vsi *
ice_chnl_vsi_setup(struct ice_pf *pf, struct ice_port_info *pi,
		   struct ice_channel *ch)
{
	struct ice_vsi_cfg_params params = {};

	params.type = ICE_VSI_CHNL;
	params.port_info = pi;
	params.ch = ch;
	params.flags = ICE_VSI_FLAG_INIT;

	return ice_vsi_setup(pf, &params);
}

/**
 * ice_ctrl_vsi_setup - Set up a control VSI
 * @pf: board private structure
 * @pi: pointer to the port_info instance
 *
 * Returns pointer to the successfully allocated VSI software struct
 * on success, otherwise returns NULL on failure.
 */
static struct ice_vsi *
ice_ctrl_vsi_setup(struct ice_pf *pf, struct ice_port_info *pi)
{
	struct ice_vsi_cfg_params params = {};

	params.type = ICE_VSI_CTRL;
	params.port_info = pi;
	params.flags = ICE_VSI_FLAG_INIT;

	return ice_vsi_setup(pf, &params);
}

/**
 * ice_lb_vsi_setup - Set up a loopback VSI
 * @pf: board private structure
 * @pi: pointer to the port_info instance
 *
 * Returns pointer to the successfully allocated VSI software struct
 * on success, otherwise returns NULL on failure.
 */
struct ice_vsi *
ice_lb_vsi_setup(struct ice_pf *pf, struct ice_port_info *pi)
{
	struct ice_vsi_cfg_params params = {};

	params.type = ICE_VSI_LB;
	params.port_info = pi;
	params.flags = ICE_VSI_FLAG_INIT;

	return ice_vsi_setup(pf, &params);
}

/**
 * ice_vlan_rx_add_vid - Add a VLAN ID filter to HW offload
 * @netdev: network interface to be adjusted
 * @proto: VLAN TPID
 * @vid: VLAN ID to be added
 *
 * net_device_ops implementation for adding VLAN IDs
 */
static int
ice_vlan_rx_add_vid(struct net_device *netdev, __be16 proto, u16 vid)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi_vlan_ops *vlan_ops;
	struct ice_vsi *vsi = np->vsi;
	struct ice_vlan vlan;
	int ret;

	/* VLAN 0 is added by default during load/reset */
	if (!vid)
		return 0;

	while (test_and_set_bit(ICE_CFG_BUSY, vsi->state))
		usleep_range(1000, 2000);

	/* Add multicast promisc rule for the VLAN ID to be added if
	 * all-multicast is currently enabled.
	 */
	if (vsi->current_netdev_flags & IFF_ALLMULTI) {
		ret = ice_fltr_set_vsi_promisc(&vsi->back->hw, vsi->idx,
					       ICE_MCAST_VLAN_PROMISC_BITS,
					       vid);
		if (ret)
			goto finish;
	}

	vlan_ops = ice_get_compat_vsi_vlan_ops(vsi);

	/* Add a switch rule for this VLAN ID so its corresponding VLAN tagged
	 * packets aren't pruned by the device's internal switch on Rx
	 */
	vlan = ICE_VLAN(be16_to_cpu(proto), vid, 0);
	ret = vlan_ops->add_vlan(vsi, &vlan);
	if (ret)
		goto finish;

	/* If all-multicast is currently enabled and this VLAN ID is only one
	 * besides VLAN-0 we have to update look-up type of multicast promisc
	 * rule for VLAN-0 from ICE_SW_LKUP_PROMISC to ICE_SW_LKUP_PROMISC_VLAN.
	 */
	if ((vsi->current_netdev_flags & IFF_ALLMULTI) &&
	    ice_vsi_num_non_zero_vlans(vsi) == 1) {
		ice_fltr_clear_vsi_promisc(&vsi->back->hw, vsi->idx,
					   ICE_MCAST_PROMISC_BITS, 0);
		ice_fltr_set_vsi_promisc(&vsi->back->hw, vsi->idx,
					 ICE_MCAST_VLAN_PROMISC_BITS, 0);
	}

finish:
	clear_bit(ICE_CFG_BUSY, vsi->state);

	return ret;
}

/**
 * ice_vlan_rx_kill_vid - Remove a VLAN ID filter from HW offload
 * @netdev: network interface to be adjusted
 * @proto: VLAN TPID
 * @vid: VLAN ID to be removed
 *
 * net_device_ops implementation for removing VLAN IDs
 */
static int
ice_vlan_rx_kill_vid(struct net_device *netdev, __be16 proto, u16 vid)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi_vlan_ops *vlan_ops;
	struct ice_vsi *vsi = np->vsi;
	struct ice_vlan vlan;
	int ret;

	/* don't allow removal of VLAN 0 */
	if (!vid)
		return 0;

	while (test_and_set_bit(ICE_CFG_BUSY, vsi->state))
		usleep_range(1000, 2000);

	ret = ice_clear_vsi_promisc(&vsi->back->hw, vsi->idx,
				    ICE_MCAST_VLAN_PROMISC_BITS, vid);
	if (ret) {
		netdev_err(netdev, "Error clearing multicast promiscuous mode on VSI %i\n",
			   vsi->vsi_num);
		vsi->current_netdev_flags |= IFF_ALLMULTI;
	}

	vlan_ops = ice_get_compat_vsi_vlan_ops(vsi);

	/* Make sure VLAN delete is successful before updating VLAN
	 * information
	 */
	vlan = ICE_VLAN(be16_to_cpu(proto), vid, 0);
	ret = vlan_ops->del_vlan(vsi, &vlan);
	if (ret)
		goto finish;

	/* Remove multicast promisc rule for the removed VLAN ID if
	 * all-multicast is enabled.
	 */
	if (vsi->current_netdev_flags & IFF_ALLMULTI)
		ice_fltr_clear_vsi_promisc(&vsi->back->hw, vsi->idx,
					   ICE_MCAST_VLAN_PROMISC_BITS, vid);

	if (!ice_vsi_has_non_zero_vlans(vsi)) {
		/* Update look-up type of multicast promisc rule for VLAN 0
		 * from ICE_SW_LKUP_PROMISC_VLAN to ICE_SW_LKUP_PROMISC when
		 * all-multicast is enabled and VLAN 0 is the only VLAN rule.
		 */
		if (vsi->current_netdev_flags & IFF_ALLMULTI) {
			ice_fltr_clear_vsi_promisc(&vsi->back->hw, vsi->idx,
						   ICE_MCAST_VLAN_PROMISC_BITS,
						   0);
			ice_fltr_set_vsi_promisc(&vsi->back->hw, vsi->idx,
						 ICE_MCAST_PROMISC_BITS, 0);
		}
	}

finish:
	clear_bit(ICE_CFG_BUSY, vsi->state);

	return ret;
}

/**
 * ice_rep_indr_tc_block_unbind
 * @cb_priv: indirection block private data
 */
static void ice_rep_indr_tc_block_unbind(void *cb_priv)
{
	struct ice_indr_block_priv *indr_priv = cb_priv;

	list_del(&indr_priv->list);
	kfree(indr_priv);
}

/**
 * ice_tc_indir_block_unregister - Unregister TC indirect block notifications
 * @vsi: VSI struct which has the netdev
 */
static void ice_tc_indir_block_unregister(struct ice_vsi *vsi)
{
	struct ice_netdev_priv *np = netdev_priv(vsi->netdev);

	flow_indr_dev_unregister(ice_indr_setup_tc_cb, np,
				 ice_rep_indr_tc_block_unbind);
}

/**
 * ice_tc_indir_block_register - Register TC indirect block notifications
 * @vsi: VSI struct which has the netdev
 *
 * Returns 0 on success, negative value on failure
 */
static int ice_tc_indir_block_register(struct ice_vsi *vsi)
{
	struct ice_netdev_priv *np;

	if (!vsi || !vsi->netdev)
		return -EINVAL;

	np = netdev_priv(vsi->netdev);

	INIT_LIST_HEAD(&np->tc_indr_block_priv_list);
	return flow_indr_dev_register(ice_indr_setup_tc_cb, np);
}

/**
 * ice_get_avail_q_count - Get count of queues in use
 * @pf_qmap: bitmap to get queue use count from
 * @lock: pointer to a mutex that protects access to pf_qmap
 * @size: size of the bitmap
 */
static u16
ice_get_avail_q_count(unsigned long *pf_qmap, struct mutex *lock, u16 size)
{
	unsigned long bit;
	u16 count = 0;

	mutex_lock(lock);
	for_each_clear_bit(bit, pf_qmap, size)
		count++;
	mutex_unlock(lock);

	return count;
}

/**
 * ice_get_avail_txq_count - Get count of Tx queues in use
 * @pf: pointer to an ice_pf instance
 */
u16 ice_get_avail_txq_count(struct ice_pf *pf)
{
	return ice_get_avail_q_count(pf->avail_txqs, &pf->avail_q_mutex,
				     pf->max_pf_txqs);
}

/**
 * ice_get_avail_rxq_count - Get count of Rx queues in use
 * @pf: pointer to an ice_pf instance
 */
u16 ice_get_avail_rxq_count(struct ice_pf *pf)
{
	return ice_get_avail_q_count(pf->avail_rxqs, &pf->avail_q_mutex,
				     pf->max_pf_rxqs);
}

/**
 * ice_deinit_pf - Unrolls initialziations done by ice_init_pf
 * @pf: board private structure to initialize
 */
static void ice_deinit_pf(struct ice_pf *pf)
{
	ice_service_task_stop(pf);
	mutex_destroy(&pf->lag_mutex);
	mutex_destroy(&pf->adev_mutex);
	mutex_destroy(&pf->sw_mutex);
	mutex_destroy(&pf->tc_mutex);
	mutex_destroy(&pf->avail_q_mutex);
	mutex_destroy(&pf->vfs.table_lock);

	if (pf->avail_txqs) {
		bitmap_free(pf->avail_txqs);
		pf->avail_txqs = NULL;
	}

	if (pf->avail_rxqs) {
		bitmap_free(pf->avail_rxqs);
		pf->avail_rxqs = NULL;
	}

	if (pf->ptp.clock)
		ptp_clock_unregister(pf->ptp.clock);
}

/**
 * ice_set_pf_caps - set PFs capability flags
 * @pf: pointer to the PF instance
 */
static void ice_set_pf_caps(struct ice_pf *pf)
{
	struct ice_hw_func_caps *func_caps = &pf->hw.func_caps;

	clear_bit(ICE_FLAG_RDMA_ENA, pf->flags);
	if (func_caps->common_cap.rdma)
		set_bit(ICE_FLAG_RDMA_ENA, pf->flags);
	clear_bit(ICE_FLAG_DCB_CAPABLE, pf->flags);
	if (func_caps->common_cap.dcb)
		set_bit(ICE_FLAG_DCB_CAPABLE, pf->flags);
	clear_bit(ICE_FLAG_SRIOV_CAPABLE, pf->flags);
	if (func_caps->common_cap.sr_iov_1_1) {
		set_bit(ICE_FLAG_SRIOV_CAPABLE, pf->flags);
		pf->vfs.num_supported = min_t(int, func_caps->num_allocd_vfs,
					      ICE_MAX_SRIOV_VFS);
	}
	clear_bit(ICE_FLAG_RSS_ENA, pf->flags);
	if (func_caps->common_cap.rss_table_size)
		set_bit(ICE_FLAG_RSS_ENA, pf->flags);

	clear_bit(ICE_FLAG_FD_ENA, pf->flags);
	if (func_caps->fd_fltr_guar > 0 || func_caps->fd_fltr_best_effort > 0) {
		u16 unused;

		/* ctrl_vsi_idx will be set to a valid value when flow director
		 * is setup by ice_init_fdir
		 */
		pf->ctrl_vsi_idx = ICE_NO_VSI;
		set_bit(ICE_FLAG_FD_ENA, pf->flags);
		/* force guaranteed filter pool for PF */
		ice_alloc_fd_guar_item(&pf->hw, &unused,
				       func_caps->fd_fltr_guar);
		/* force shared filter pool for PF */
		ice_alloc_fd_shrd_item(&pf->hw, &unused,
				       func_caps->fd_fltr_best_effort);
	}

	clear_bit(ICE_FLAG_PTP_SUPPORTED, pf->flags);
	if (func_caps->common_cap.ieee_1588 &&
	    !(pf->hw.mac_type == ICE_MAC_E830))
		set_bit(ICE_FLAG_PTP_SUPPORTED, pf->flags);

	pf->max_pf_txqs = func_caps->common_cap.num_txq;
	pf->max_pf_rxqs = func_caps->common_cap.num_rxq;
}

/**
 * ice_init_pf - Initialize general software structures (struct ice_pf)
 * @pf: board private structure to initialize
 */
static int ice_init_pf(struct ice_pf *pf)
{
	ice_set_pf_caps(pf);

	mutex_init(&pf->sw_mutex);
	mutex_init(&pf->tc_mutex);
	mutex_init(&pf->adev_mutex);
	mutex_init(&pf->lag_mutex);

	INIT_HLIST_HEAD(&pf->aq_wait_list);
	spin_lock_init(&pf->aq_wait_lock);
	init_waitqueue_head(&pf->aq_wait_queue);

	init_waitqueue_head(&pf->reset_wait_queue);

	/* setup service timer and periodic service task */
	timer_setup(&pf->serv_tmr, ice_service_timer, 0);
	pf->serv_tmr_period = HZ;
	INIT_WORK(&pf->serv_task, ice_service_task);
	clear_bit(ICE_SERVICE_SCHED, pf->state);

	mutex_init(&pf->avail_q_mutex);
	pf->avail_txqs = bitmap_zalloc(pf->max_pf_txqs, GFP_KERNEL);
	if (!pf->avail_txqs)
		return -ENOMEM;

	pf->avail_rxqs = bitmap_zalloc(pf->max_pf_rxqs, GFP_KERNEL);
	if (!pf->avail_rxqs) {
		bitmap_free(pf->avail_txqs);
		pf->avail_txqs = NULL;
		return -ENOMEM;
	}

	mutex_init(&pf->vfs.table_lock);
	hash_init(pf->vfs.table);
	ice_mbx_init_snapshot(&pf->hw);

	return 0;
}

/**
 * ice_is_wol_supported - check if WoL is supported
 * @hw: pointer to hardware info
 *
 * Check if WoL is supported based on the HW configuration.
 * Returns true if NVM supports and enables WoL for this port, false otherwise
 */
bool ice_is_wol_supported(struct ice_hw *hw)
{
	u16 wol_ctrl;

	/* A bit set to 1 in the NVM Software Reserved Word 2 (WoL control
	 * word) indicates WoL is not supported on the corresponding PF ID.
	 */
	if (ice_read_sr_word(hw, ICE_SR_NVM_WOL_CFG, &wol_ctrl))
		return false;

	return !(BIT(hw->port_info->lport) & wol_ctrl);
}

/**
 * ice_vsi_recfg_qs - Change the number of queues on a VSI
 * @vsi: VSI being changed
 * @new_rx: new number of Rx queues
 * @new_tx: new number of Tx queues
 * @locked: is adev device_lock held
 *
 * Only change the number of queues if new_tx, or new_rx is non-0.
 *
 * Returns 0 on success.
 */
int ice_vsi_recfg_qs(struct ice_vsi *vsi, int new_rx, int new_tx, bool locked)
{
	struct ice_pf *pf = vsi->back;
	int i, err = 0, timeout = 50;

	if (!new_rx && !new_tx)
		return -EINVAL;

	while (test_and_set_bit(ICE_CFG_BUSY, pf->state)) {
		timeout--;
		if (!timeout)
			return -EBUSY;
		usleep_range(1000, 2000);
	}

	if (new_tx)
		vsi->req_txq = (u16)new_tx;
	if (new_rx)
		vsi->req_rxq = (u16)new_rx;

	/* set for the next time the netdev is started */
	if (!netif_running(vsi->netdev)) {
		err = ice_vsi_rebuild(vsi, ICE_VSI_FLAG_NO_INIT);
		if (err)
			goto rebuild_err;
		dev_dbg(ice_pf_to_dev(pf), "Link is down, queue count change happens when link is brought up\n");
		goto done;
	}

	ice_vsi_close(vsi);
	err = ice_vsi_rebuild(vsi, ICE_VSI_FLAG_NO_INIT);
	if (err)
		goto rebuild_err;

	ice_for_each_traffic_class(i) {
		if (vsi->tc_cfg.ena_tc & BIT(i))
			netdev_set_tc_queue(vsi->netdev,
					    vsi->tc_cfg.tc_info[i].netdev_tc,
					    vsi->tc_cfg.tc_info[i].qcount_tx,
					    vsi->tc_cfg.tc_info[i].qoffset);
	}
	ice_pf_dcb_recfg(pf, locked);
	ice_vsi_open(vsi);
	goto done;

rebuild_err:
	dev_err(ice_pf_to_dev(pf), "Error during VSI rebuild: %d. Unload and reload the driver.\n",
		err);
done:
	clear_bit(ICE_CFG_BUSY, pf->state);
	return err;
}

/**
 * ice_set_safe_mode_vlan_cfg - configure PF VSI to allow all VLANs in safe mode
 * @pf: PF to configure
 *
 * No VLAN offloads/filtering are advertised in safe mode so make sure the PF
 * VSI can still Tx/Rx VLAN tagged packets.
 */
static void ice_set_safe_mode_vlan_cfg(struct ice_pf *pf)
{
	struct ice_vsi *vsi = ice_get_main_vsi(pf);
	struct ice_vsi_ctx *ctxt;
	struct ice_hw *hw;
	int status;

	if (!vsi)
		return;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return;

	hw = &pf->hw;
	ctxt->info = vsi->info;

	ctxt->info.valid_sections =
		cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID |
			    ICE_AQ_VSI_PROP_SECURITY_VALID |
			    ICE_AQ_VSI_PROP_SW_VALID);

	/* disable VLAN anti-spoof */
	ctxt->info.sec_flags &= ~(ICE_AQ_VSI_SEC_TX_VLAN_PRUNE_ENA <<
				  ICE_AQ_VSI_SEC_TX_PRUNE_ENA_S);

	/* disable VLAN pruning and keep all other settings */
	ctxt->info.sw_flags2 &= ~ICE_AQ_VSI_SW_FLAG_RX_VLAN_PRUNE_ENA;

	/* allow all VLANs on Tx and don't strip on Rx */
	ctxt->info.inner_vlan_flags = ICE_AQ_VSI_INNER_VLAN_TX_MODE_ALL |
		ICE_AQ_VSI_INNER_VLAN_EMODE_NOTHING;

	status = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (status) {
		dev_err(ice_pf_to_dev(vsi->back), "Failed to update VSI for safe mode VLANs, err %d aq_err %s\n",
			status, ice_aq_str(hw->adminq.sq_last_status));
	} else {
		vsi->info.sec_flags = ctxt->info.sec_flags;
		vsi->info.sw_flags2 = ctxt->info.sw_flags2;
		vsi->info.inner_vlan_flags = ctxt->info.inner_vlan_flags;
	}

	kfree(ctxt);
}

/**
 * ice_log_pkg_init - log result of DDP package load
 * @hw: pointer to hardware info
 * @state: state of package load
 */
static void ice_log_pkg_init(struct ice_hw *hw, enum ice_ddp_state state)
{
	struct ice_pf *pf = hw->back;
	struct device *dev;

	dev = ice_pf_to_dev(pf);

	switch (state) {
	case ICE_DDP_PKG_SUCCESS:
		dev_info(dev, "The DDP package was successfully loaded: %s version %d.%d.%d.%d\n",
			 hw->active_pkg_name,
			 hw->active_pkg_ver.major,
			 hw->active_pkg_ver.minor,
			 hw->active_pkg_ver.update,
			 hw->active_pkg_ver.draft);
		break;
	case ICE_DDP_PKG_SAME_VERSION_ALREADY_LOADED:
		dev_info(dev, "DDP package already present on device: %s version %d.%d.%d.%d\n",
			 hw->active_pkg_name,
			 hw->active_pkg_ver.major,
			 hw->active_pkg_ver.minor,
			 hw->active_pkg_ver.update,
			 hw->active_pkg_ver.draft);
		break;
	case ICE_DDP_PKG_ALREADY_LOADED_NOT_SUPPORTED:
		dev_err(dev, "The device has a DDP package that is not supported by the driver.  The device has package '%s' version %d.%d.x.x.  The driver requires version %d.%d.x.x.  Entering Safe Mode.\n",
			hw->active_pkg_name,
			hw->active_pkg_ver.major,
			hw->active_pkg_ver.minor,
			ICE_PKG_SUPP_VER_MAJ, ICE_PKG_SUPP_VER_MNR);
		break;
	case ICE_DDP_PKG_COMPATIBLE_ALREADY_LOADED:
		dev_info(dev, "The driver could not load the DDP package file because a compatible DDP package is already present on the device.  The device has package '%s' version %d.%d.%d.%d.  The package file found by the driver: '%s' version %d.%d.%d.%d.\n",
			 hw->active_pkg_name,
			 hw->active_pkg_ver.major,
			 hw->active_pkg_ver.minor,
			 hw->active_pkg_ver.update,
			 hw->active_pkg_ver.draft,
			 hw->pkg_name,
			 hw->pkg_ver.major,
			 hw->pkg_ver.minor,
			 hw->pkg_ver.update,
			 hw->pkg_ver.draft);
		break;
	case ICE_DDP_PKG_FW_MISMATCH:
		dev_err(dev, "The firmware loaded on the device is not compatible with the DDP package.  Please update the device's NVM.  Entering safe mode.\n");
		break;
	case ICE_DDP_PKG_INVALID_FILE:
		dev_err(dev, "The DDP package file is invalid. Entering Safe Mode.\n");
		break;
	case ICE_DDP_PKG_FILE_VERSION_TOO_HIGH:
		dev_err(dev, "The DDP package file version is higher than the driver supports.  Please use an updated driver.  Entering Safe Mode.\n");
		break;
	case ICE_DDP_PKG_FILE_VERSION_TOO_LOW:
		dev_err(dev, "The DDP package file version is lower than the driver supports.  The driver requires version %d.%d.x.x.  Please use an updated DDP Package file.  Entering Safe Mode.\n",
			ICE_PKG_SUPP_VER_MAJ, ICE_PKG_SUPP_VER_MNR);
		break;
	case ICE_DDP_PKG_FILE_SIGNATURE_INVALID:
		dev_err(dev, "The DDP package could not be loaded because its signature is not valid.  Please use a valid DDP Package.  Entering Safe Mode.\n");
		break;
	case ICE_DDP_PKG_FILE_REVISION_TOO_LOW:
		dev_err(dev, "The DDP Package could not be loaded because its security revision is too low.  Please use an updated DDP Package.  Entering Safe Mode.\n");
		break;
	case ICE_DDP_PKG_LOAD_ERROR:
		dev_err(dev, "An error occurred on the device while loading the DDP package.  The device will be reset.\n");
		/* poll for reset to complete */
		if (ice_check_reset(hw))
			dev_err(dev, "Error resetting device. Please reload the driver\n");
		break;
	case ICE_DDP_PKG_ERR:
	default:
		dev_err(dev, "An unknown error occurred when loading the DDP package.  Entering Safe Mode.\n");
		break;
	}
}

/**
 * ice_load_pkg - load/reload the DDP Package file
 * @firmware: firmware structure when firmware requested or NULL for reload
 * @pf: pointer to the PF instance
 *
 * Called on probe and post CORER/GLOBR rebuild to load DDP Package and
 * initialize HW tables.
 */
static void
ice_load_pkg(const struct firmware *firmware, struct ice_pf *pf)
{
	enum ice_ddp_state state = ICE_DDP_PKG_ERR;
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;

	/* Load DDP Package */
	if (firmware && !hw->pkg_copy) {
		state = ice_copy_and_init_pkg(hw, firmware->data,
					      firmware->size);
		ice_log_pkg_init(hw, state);
	} else if (!firmware && hw->pkg_copy) {
		/* Reload package during rebuild after CORER/GLOBR reset */
		state = ice_init_pkg(hw, hw->pkg_copy, hw->pkg_size);
		ice_log_pkg_init(hw, state);
	} else {
		dev_err(dev, "The DDP package file failed to load. Entering Safe Mode.\n");
	}

	if (!ice_is_init_pkg_successful(state)) {
		/* Safe Mode */
		clear_bit(ICE_FLAG_ADV_FEATURES, pf->flags);
		return;
	}

	/* Successful download package is the precondition for advanced
	 * features, hence setting the ICE_FLAG_ADV_FEATURES flag
	 */
	set_bit(ICE_FLAG_ADV_FEATURES, pf->flags);
}

/**
 * ice_verify_cacheline_size - verify driver's assumption of 64 Byte cache lines
 * @pf: pointer to the PF structure
 *
 * There is no error returned here because the driver should be able to handle
 * 128 Byte cache lines, so we only print a warning in case issues are seen,
 * specifically with Tx.
 */
static void ice_verify_cacheline_size(struct ice_pf *pf)
{
	if (rd32(&pf->hw, GLPCI_CNF2) & GLPCI_CNF2_CACHELINE_SIZE_M)
		dev_warn(ice_pf_to_dev(pf), "%d Byte cache line assumption is invalid, driver may have Tx timeouts!\n",
			 ICE_CACHE_LINE_BYTES);
}

/**
 * ice_send_version - update firmware with driver version
 * @pf: PF struct
 *
 * Returns 0 on success, else error code
 */
static int ice_send_version(struct ice_pf *pf)
{
	struct ice_driver_ver dv;

	dv.major_ver = 0xff;
	dv.minor_ver = 0xff;
	dv.build_ver = 0xff;
	dv.subbuild_ver = 0;
	strscpy((char *)dv.driver_string, UTS_RELEASE,
		sizeof(dv.driver_string));
	return ice_aq_send_driver_ver(&pf->hw, &dv, NULL);
}

/**
 * ice_init_fdir - Initialize flow director VSI and configuration
 * @pf: pointer to the PF instance
 *
 * returns 0 on success, negative on error
 */
static int ice_init_fdir(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_vsi *ctrl_vsi;
	int err;

	/* Side Band Flow Director needs to have a control VSI.
	 * Allocate it and store it in the PF.
	 */
	ctrl_vsi = ice_ctrl_vsi_setup(pf, pf->hw.port_info);
	if (!ctrl_vsi) {
		dev_dbg(dev, "could not create control VSI\n");
		return -ENOMEM;
	}

	err = ice_vsi_open_ctrl(ctrl_vsi);
	if (err) {
		dev_dbg(dev, "could not open control VSI\n");
		goto err_vsi_open;
	}

	mutex_init(&pf->hw.fdir_fltr_lock);

	err = ice_fdir_create_dflt_rules(pf);
	if (err)
		goto err_fdir_rule;

	return 0;

err_fdir_rule:
	ice_fdir_release_flows(&pf->hw);
	ice_vsi_close(ctrl_vsi);
err_vsi_open:
	ice_vsi_release(ctrl_vsi);
	if (pf->ctrl_vsi_idx != ICE_NO_VSI) {
		pf->vsi[pf->ctrl_vsi_idx] = NULL;
		pf->ctrl_vsi_idx = ICE_NO_VSI;
	}
	return err;
}

static void ice_deinit_fdir(struct ice_pf *pf)
{
	struct ice_vsi *vsi = ice_get_ctrl_vsi(pf);

	if (!vsi)
		return;

	ice_vsi_manage_fdir(vsi, false);
	ice_vsi_release(vsi);
	if (pf->ctrl_vsi_idx != ICE_NO_VSI) {
		pf->vsi[pf->ctrl_vsi_idx] = NULL;
		pf->ctrl_vsi_idx = ICE_NO_VSI;
	}

	mutex_destroy(&(&pf->hw)->fdir_fltr_lock);
}

/**
 * ice_get_opt_fw_name - return optional firmware file name or NULL
 * @pf: pointer to the PF instance
 */
static char *ice_get_opt_fw_name(struct ice_pf *pf)
{
	/* Optional firmware name same as default with additional dash
	 * followed by a EUI-64 identifier (PCIe Device Serial Number)
	 */
	struct pci_dev *pdev = pf->pdev;
	char *opt_fw_filename;
	u64 dsn;

	/* Determine the name of the optional file using the DSN (two
	 * dwords following the start of the DSN Capability).
	 */
	dsn = pci_get_dsn(pdev);
	if (!dsn)
		return NULL;

	opt_fw_filename = kzalloc(NAME_MAX, GFP_KERNEL);
	if (!opt_fw_filename)
		return NULL;

	snprintf(opt_fw_filename, NAME_MAX, "%sice-%016llx.pkg",
		 ICE_DDP_PKG_PATH, dsn);

	return opt_fw_filename;
}

/**
 * ice_request_fw - Device initialization routine
 * @pf: pointer to the PF instance
 * @firmware: double pointer to firmware struct
 *
 * Return: zero when successful, negative values otherwise.
 */
static int ice_request_fw(struct ice_pf *pf, const struct firmware **firmware)
{
	char *opt_fw_filename = ice_get_opt_fw_name(pf);
	struct device *dev = ice_pf_to_dev(pf);
	int err = 0;

	/* optional device-specific DDP (if present) overrides the default DDP
	 * package file. kernel logs a debug message if the file doesn't exist,
	 * and warning messages for other errors.
	 */
	if (opt_fw_filename) {
		err = firmware_request_nowarn(firmware, opt_fw_filename, dev);
		kfree(opt_fw_filename);
		if (!err)
			return err;
	}
	err = request_firmware(firmware, ICE_DDP_PKG_FILE, dev);
	if (err)
		dev_err(dev, "The DDP package file was not found or could not be read. Entering Safe Mode\n");

	return err;
}

/**
 * ice_init_tx_topology - performs Tx topology initialization
 * @hw: pointer to the hardware structure
 * @firmware: pointer to firmware structure
 *
 * Return: zero when init was successful, negative values otherwise.
 */
static int
ice_init_tx_topology(struct ice_hw *hw, const struct firmware *firmware)
{
	u8 num_tx_sched_layers = hw->num_tx_sched_layers;
	struct ice_pf *pf = hw->back;
	struct device *dev;
	u8 *buf_copy;
	int err;

	dev = ice_pf_to_dev(pf);
	/* ice_cfg_tx_topo buf argument is not a constant,
	 * so we have to make a copy
	 */
	buf_copy = kmemdup(firmware->data, firmware->size, GFP_KERNEL);

	err = ice_cfg_tx_topo(hw, buf_copy, firmware->size);
	if (!err) {
		if (hw->num_tx_sched_layers > num_tx_sched_layers)
			dev_info(dev, "Tx scheduling layers switching feature disabled\n");
		else
			dev_info(dev, "Tx scheduling layers switching feature enabled\n");
		/* if there was a change in topology ice_cfg_tx_topo triggered
		 * a CORER and we need to re-init hw
		 */
		ice_deinit_hw(hw);
		err = ice_init_hw(hw);

		return err;
	} else if (err == -EIO) {
		dev_info(dev, "DDP package does not support Tx scheduling layers switching feature - please update to the latest DDP package and try again\n");
	}

	return 0;
}

/**
 * ice_init_ddp_config - DDP related configuration
 * @hw: pointer to the hardware structure
 * @pf: pointer to pf structure
 *
 * This function loads DDP file from the disk, then initializes Tx
 * topology. At the end DDP package is loaded on the card.
 *
 * Return: zero when init was successful, negative values otherwise.
 */
static int ice_init_ddp_config(struct ice_hw *hw, struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	const struct firmware *firmware = NULL;
	int err;

	err = ice_request_fw(pf, &firmware);
	if (err) {
		dev_err(dev, "Fail during requesting FW: %d\n", err);
		return err;
	}

	err = ice_init_tx_topology(hw, firmware);
	if (err) {
		dev_err(dev, "Fail during initialization of Tx topology: %d\n",
			err);
		release_firmware(firmware);
		return err;
	}

	/* Download firmware to device */
	ice_load_pkg(firmware, pf);
	release_firmware(firmware);

	return 0;
}

/**
 * ice_print_wake_reason - show the wake up cause in the log
 * @pf: pointer to the PF struct
 */
static void ice_print_wake_reason(struct ice_pf *pf)
{
	u32 wus = pf->wakeup_reason;
	const char *wake_str;

	/* if no wake event, nothing to print */
	if (!wus)
		return;

	if (wus & PFPM_WUS_LNKC_M)
		wake_str = "Link\n";
	else if (wus & PFPM_WUS_MAG_M)
		wake_str = "Magic Packet\n";
	else if (wus & PFPM_WUS_MNG_M)
		wake_str = "Management\n";
	else if (wus & PFPM_WUS_FW_RST_WK_M)
		wake_str = "Firmware Reset\n";
	else
		wake_str = "Unknown\n";

	dev_info(ice_pf_to_dev(pf), "Wake reason: %s", wake_str);
}

/**
 * ice_pf_fwlog_update_module - update 1 module
 * @pf: pointer to the PF struct
 * @log_level: log_level to use for the @module
 * @module: module to update
 */
void ice_pf_fwlog_update_module(struct ice_pf *pf, int log_level, int module)
{
	struct ice_hw *hw = &pf->hw;

	hw->fwlog_cfg.module_entries[module].log_level = log_level;
}

/**
 * ice_register_netdev - register netdev
 * @vsi: pointer to the VSI struct
 */
static int ice_register_netdev(struct ice_vsi *vsi)
{
	int err;

	if (!vsi || !vsi->netdev)
		return -EIO;

	err = register_netdev(vsi->netdev);
	if (err)
		return err;

	set_bit(ICE_VSI_NETDEV_REGISTERED, vsi->state);
	netif_carrier_off(vsi->netdev);
	netif_tx_stop_all_queues(vsi->netdev);

	return 0;
}

static void ice_unregister_netdev(struct ice_vsi *vsi)
{
	if (!vsi || !vsi->netdev)
		return;

	unregister_netdev(vsi->netdev);
	clear_bit(ICE_VSI_NETDEV_REGISTERED, vsi->state);
}

/**
 * ice_cfg_netdev - Allocate, configure and register a netdev
 * @vsi: the VSI associated with the new netdev
 *
 * Returns 0 on success, negative value on failure
 */
static int ice_cfg_netdev(struct ice_vsi *vsi)
{
	struct ice_netdev_priv *np;
	struct net_device *netdev;
	u8 mac_addr[ETH_ALEN];

	netdev = alloc_etherdev_mqs(sizeof(*np), vsi->alloc_txq,
				    vsi->alloc_rxq);
	if (!netdev)
		return -ENOMEM;

	set_bit(ICE_VSI_NETDEV_ALLOCD, vsi->state);
	vsi->netdev = netdev;
	np = netdev_priv(netdev);
	np->vsi = vsi;

	ice_set_netdev_features(netdev);
	ice_set_ops(vsi);

	if (vsi->type == ICE_VSI_PF) {
		SET_NETDEV_DEV(netdev, ice_pf_to_dev(vsi->back));
		ether_addr_copy(mac_addr, vsi->port_info->mac.perm_addr);
		eth_hw_addr_set(netdev, mac_addr);
	}

	netdev->priv_flags |= IFF_UNICAST_FLT;

	/* Setup netdev TC information */
	ice_vsi_cfg_netdev_tc(vsi, vsi->tc_cfg.ena_tc);

	netdev->max_mtu = ICE_MAX_MTU;

	return 0;
}

static void ice_decfg_netdev(struct ice_vsi *vsi)
{
	clear_bit(ICE_VSI_NETDEV_ALLOCD, vsi->state);
	free_netdev(vsi->netdev);
	vsi->netdev = NULL;
}

/**
 * ice_wait_for_fw - wait for full FW readiness
 * @hw: pointer to the hardware structure
 * @timeout: milliseconds that can elapse before timing out
 */
static int ice_wait_for_fw(struct ice_hw *hw, u32 timeout)
{
	int fw_loading;
	u32 elapsed = 0;

	while (elapsed <= timeout) {
		fw_loading = rd32(hw, GL_MNG_FWSM) & GL_MNG_FWSM_FW_LOADING_M;

		/* firmware was not yet loaded, we have to wait more */
		if (fw_loading) {
			elapsed += 100;
			msleep(100);
			continue;
		}
		return 0;
	}

	return -ETIMEDOUT;
}

int ice_init_dev(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	int err;

	err = ice_init_hw(hw);
	if (err) {
		dev_err(dev, "ice_init_hw failed: %d\n", err);
		return err;
	}

	/* Some cards require longer initialization times
	 * due to necessity of loading FW from an external source.
	 * This can take even half a minute.
	 */
	if (ice_is_pf_c827(hw)) {
		err = ice_wait_for_fw(hw, 30000);
		if (err) {
			dev_err(dev, "ice_wait_for_fw timed out");
			return err;
		}
	}

	ice_init_feature_support(pf);

	err = ice_init_ddp_config(hw, pf);
	if (err)
		return err;

	/* if ice_init_ddp_config fails, ICE_FLAG_ADV_FEATURES bit won't be
	 * set in pf->state, which will cause ice_is_safe_mode to return
	 * true
	 */
	if (ice_is_safe_mode(pf)) {
		/* we already got function/device capabilities but these don't
		 * reflect what the driver needs to do in safe mode. Instead of
		 * adding conditional logic everywhere to ignore these
		 * device/function capabilities, override them.
		 */
		ice_set_safe_mode_caps(hw);
	}

	err = ice_init_pf(pf);
	if (err) {
		dev_err(dev, "ice_init_pf failed: %d\n", err);
		goto err_init_pf;
	}

	pf->hw.udp_tunnel_nic.set_port = ice_udp_tunnel_set_port;
	pf->hw.udp_tunnel_nic.unset_port = ice_udp_tunnel_unset_port;
	pf->hw.udp_tunnel_nic.flags = UDP_TUNNEL_NIC_INFO_MAY_SLEEP;
	pf->hw.udp_tunnel_nic.shared = &pf->hw.udp_tunnel_shared;
	if (pf->hw.tnl.valid_count[TNL_VXLAN]) {
		pf->hw.udp_tunnel_nic.tables[0].n_entries =
			pf->hw.tnl.valid_count[TNL_VXLAN];
		pf->hw.udp_tunnel_nic.tables[0].tunnel_types =
			UDP_TUNNEL_TYPE_VXLAN;
	}
	if (pf->hw.tnl.valid_count[TNL_GENEVE]) {
		pf->hw.udp_tunnel_nic.tables[1].n_entries =
			pf->hw.tnl.valid_count[TNL_GENEVE];
		pf->hw.udp_tunnel_nic.tables[1].tunnel_types =
			UDP_TUNNEL_TYPE_GENEVE;
	}

	err = ice_init_interrupt_scheme(pf);
	if (err) {
		dev_err(dev, "ice_init_interrupt_scheme failed: %d\n", err);
		err = -EIO;
		goto err_init_interrupt_scheme;
	}

	/* In case of MSIX we are going to setup the misc vector right here
	 * to handle admin queue events etc. In case of legacy and MSI
	 * the misc functionality and queue processing is combined in
	 * the same vector and that gets setup at open.
	 */
	err = ice_req_irq_msix_misc(pf);
	if (err) {
		dev_err(dev, "setup of misc vector failed: %d\n", err);
		goto err_req_irq_msix_misc;
	}

	return 0;

err_req_irq_msix_misc:
	ice_clear_interrupt_scheme(pf);
err_init_interrupt_scheme:
	ice_deinit_pf(pf);
err_init_pf:
	ice_deinit_hw(hw);
	return err;
}

void ice_deinit_dev(struct ice_pf *pf)
{
	ice_free_irq_msix_misc(pf);
	ice_deinit_pf(pf);
	ice_deinit_hw(&pf->hw);

	/* Service task is already stopped, so call reset directly. */
	ice_reset(&pf->hw, ICE_RESET_PFR);
	pci_wait_for_pending_transaction(pf->pdev);
	ice_clear_interrupt_scheme(pf);
}

static void ice_init_features(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);

	if (ice_is_safe_mode(pf))
		return;

	/* initialize DDP driven features */
	if (test_bit(ICE_FLAG_PTP_SUPPORTED, pf->flags))
		ice_ptp_init(pf);

	if (ice_is_feature_supported(pf, ICE_F_GNSS))
		ice_gnss_init(pf);

	if (ice_is_feature_supported(pf, ICE_F_CGU) ||
	    ice_is_feature_supported(pf, ICE_F_PHY_RCLK))
		ice_dpll_init(pf);

	/* Note: Flow director init failure is non-fatal to load */
	if (ice_init_fdir(pf))
		dev_err(dev, "could not initialize flow director\n");

	/* Note: DCB init failure is non-fatal to load */
	if (ice_init_pf_dcb(pf, false)) {
		clear_bit(ICE_FLAG_DCB_CAPABLE, pf->flags);
		clear_bit(ICE_FLAG_DCB_ENA, pf->flags);
	} else {
		ice_cfg_lldp_mib_change(&pf->hw, true);
	}

	if (ice_init_lag(pf))
		dev_warn(dev, "Failed to init link aggregation support\n");

	ice_hwmon_init(pf);
}

static void ice_deinit_features(struct ice_pf *pf)
{
	if (ice_is_safe_mode(pf))
		return;

	ice_deinit_lag(pf);
	if (test_bit(ICE_FLAG_DCB_CAPABLE, pf->flags))
		ice_cfg_lldp_mib_change(&pf->hw, false);
	ice_deinit_fdir(pf);
	if (ice_is_feature_supported(pf, ICE_F_GNSS))
		ice_gnss_exit(pf);
	if (test_bit(ICE_FLAG_PTP_SUPPORTED, pf->flags))
		ice_ptp_release(pf);
	if (test_bit(ICE_FLAG_DPLL, pf->flags))
		ice_dpll_deinit(pf);
	if (pf->eswitch_mode == DEVLINK_ESWITCH_MODE_SWITCHDEV)
		xa_destroy(&pf->eswitch.reprs);
}

static void ice_init_wakeup(struct ice_pf *pf)
{
	/* Save wakeup reason register for later use */
	pf->wakeup_reason = rd32(&pf->hw, PFPM_WUS);

	/* check for a power management event */
	ice_print_wake_reason(pf);

	/* clear wake status, all bits */
	wr32(&pf->hw, PFPM_WUS, U32_MAX);

	/* Disable WoL at init, wait for user to enable */
	device_set_wakeup_enable(ice_pf_to_dev(pf), false);
}

static int ice_init_link(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	int err;

	err = ice_init_link_events(pf->hw.port_info);
	if (err) {
		dev_err(dev, "ice_init_link_events failed: %d\n", err);
		return err;
	}

	/* not a fatal error if this fails */
	err = ice_init_nvm_phy_type(pf->hw.port_info);
	if (err)
		dev_err(dev, "ice_init_nvm_phy_type failed: %d\n", err);

	/* not a fatal error if this fails */
	err = ice_update_link_info(pf->hw.port_info);
	if (err)
		dev_err(dev, "ice_update_link_info failed: %d\n", err);

	ice_init_link_dflt_override(pf->hw.port_info);

	ice_check_link_cfg_err(pf,
			       pf->hw.port_info->phy.link_info.link_cfg_err);

	/* if media available, initialize PHY settings */
	if (pf->hw.port_info->phy.link_info.link_info &
	    ICE_AQ_MEDIA_AVAILABLE) {
		/* not a fatal error if this fails */
		err = ice_init_phy_user_cfg(pf->hw.port_info);
		if (err)
			dev_err(dev, "ice_init_phy_user_cfg failed: %d\n", err);

		if (!test_bit(ICE_FLAG_LINK_DOWN_ON_CLOSE_ENA, pf->flags)) {
			struct ice_vsi *vsi = ice_get_main_vsi(pf);

			if (vsi)
				ice_configure_phy(vsi);
		}
	} else {
		set_bit(ICE_FLAG_NO_MEDIA, pf->flags);
	}

	return err;
}

static int ice_init_pf_sw(struct ice_pf *pf)
{
	bool dvm = ice_is_dvm_ena(&pf->hw);
	struct ice_vsi *vsi;
	int err;

	/* create switch struct for the switch element created by FW on boot */
	pf->first_sw = kzalloc(sizeof(*pf->first_sw), GFP_KERNEL);
	if (!pf->first_sw)
		return -ENOMEM;

	if (pf->hw.evb_veb)
		pf->first_sw->bridge_mode = BRIDGE_MODE_VEB;
	else
		pf->first_sw->bridge_mode = BRIDGE_MODE_VEPA;

	pf->first_sw->pf = pf;

	/* record the sw_id available for later use */
	pf->first_sw->sw_id = pf->hw.port_info->sw_id;

	err = ice_aq_set_port_params(pf->hw.port_info, dvm, NULL);
	if (err)
		goto err_aq_set_port_params;

	vsi = ice_pf_vsi_setup(pf, pf->hw.port_info);
	if (!vsi) {
		err = -ENOMEM;
		goto err_pf_vsi_setup;
	}

	return 0;

err_pf_vsi_setup:
err_aq_set_port_params:
	kfree(pf->first_sw);
	return err;
}

static void ice_deinit_pf_sw(struct ice_pf *pf)
{
	struct ice_vsi *vsi = ice_get_main_vsi(pf);

	if (!vsi)
		return;

	ice_vsi_release(vsi);
	kfree(pf->first_sw);
}

static int ice_alloc_vsis(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);

	pf->num_alloc_vsi = pf->hw.func_caps.guar_num_vsi;
	if (!pf->num_alloc_vsi)
		return -EIO;

	if (pf->num_alloc_vsi > UDP_TUNNEL_NIC_MAX_SHARING_DEVICES) {
		dev_warn(dev,
			 "limiting the VSI count due to UDP tunnel limitation %d > %d\n",
			 pf->num_alloc_vsi, UDP_TUNNEL_NIC_MAX_SHARING_DEVICES);
		pf->num_alloc_vsi = UDP_TUNNEL_NIC_MAX_SHARING_DEVICES;
	}

	pf->vsi = devm_kcalloc(dev, pf->num_alloc_vsi, sizeof(*pf->vsi),
			       GFP_KERNEL);
	if (!pf->vsi)
		return -ENOMEM;

	pf->vsi_stats = devm_kcalloc(dev, pf->num_alloc_vsi,
				     sizeof(*pf->vsi_stats), GFP_KERNEL);
	if (!pf->vsi_stats) {
		devm_kfree(dev, pf->vsi);
		return -ENOMEM;
	}

	return 0;
}

static void ice_dealloc_vsis(struct ice_pf *pf)
{
	devm_kfree(ice_pf_to_dev(pf), pf->vsi_stats);
	pf->vsi_stats = NULL;

	pf->num_alloc_vsi = 0;
	devm_kfree(ice_pf_to_dev(pf), pf->vsi);
	pf->vsi = NULL;
}

static int ice_init_devlink(struct ice_pf *pf)
{
	int err;

	err = ice_devlink_register_params(pf);
	if (err)
		return err;

	ice_devlink_init_regions(pf);
	ice_devlink_register(pf);

	return 0;
}

static void ice_deinit_devlink(struct ice_pf *pf)
{
	ice_devlink_unregister(pf);
	ice_devlink_destroy_regions(pf);
	ice_devlink_unregister_params(pf);
}

static int ice_init(struct ice_pf *pf)
{
	int err;

	err = ice_init_dev(pf);
	if (err)
		return err;

	err = ice_alloc_vsis(pf);
	if (err)
		goto err_alloc_vsis;

	err = ice_init_pf_sw(pf);
	if (err)
		goto err_init_pf_sw;

	ice_init_wakeup(pf);

	err = ice_init_link(pf);
	if (err)
		goto err_init_link;

	err = ice_send_version(pf);
	if (err)
		goto err_init_link;

	ice_verify_cacheline_size(pf);

	if (ice_is_safe_mode(pf))
		ice_set_safe_mode_vlan_cfg(pf);
	else
		/* print PCI link speed and width */
		pcie_print_link_status(pf->pdev);

	/* ready to go, so clear down state bit */
	clear_bit(ICE_DOWN, pf->state);
	clear_bit(ICE_SERVICE_DIS, pf->state);

	/* since everything is good, start the service timer */
	mod_timer(&pf->serv_tmr, round_jiffies(jiffies + pf->serv_tmr_period));

	return 0;

err_init_link:
	ice_deinit_pf_sw(pf);
err_init_pf_sw:
	ice_dealloc_vsis(pf);
err_alloc_vsis:
	ice_deinit_dev(pf);
	return err;
}

static void ice_deinit(struct ice_pf *pf)
{
	set_bit(ICE_SERVICE_DIS, pf->state);
	set_bit(ICE_DOWN, pf->state);

	ice_deinit_pf_sw(pf);
	ice_dealloc_vsis(pf);
	ice_deinit_dev(pf);
}

/**
 * ice_load - load pf by init hw and starting VSI
 * @pf: pointer to the pf instance
 *
 * This function has to be called under devl_lock.
 */
int ice_load(struct ice_pf *pf)
{
	struct ice_vsi *vsi;
	int err;

	devl_assert_locked(priv_to_devlink(pf));

	vsi = ice_get_main_vsi(pf);

	/* init channel list */
	INIT_LIST_HEAD(&vsi->ch_list);

	err = ice_cfg_netdev(vsi);
	if (err)
		return err;

	/* Setup DCB netlink interface */
	ice_dcbnl_setup(vsi);

	err = ice_init_mac_fltr(pf);
	if (err)
		goto err_init_mac_fltr;

	err = ice_devlink_create_pf_port(pf);
	if (err)
		goto err_devlink_create_pf_port;

	SET_NETDEV_DEVLINK_PORT(vsi->netdev, &pf->devlink_port);

	err = ice_register_netdev(vsi);
	if (err)
		goto err_register_netdev;

	err = ice_tc_indir_block_register(vsi);
	if (err)
		goto err_tc_indir_block_register;

	ice_napi_add(vsi);

	err = ice_init_rdma(pf);
	if (err)
		goto err_init_rdma;

	ice_init_features(pf);
	ice_service_task_restart(pf);

	clear_bit(ICE_DOWN, pf->state);

	return 0;

err_init_rdma:
	ice_tc_indir_block_unregister(vsi);
err_tc_indir_block_register:
	ice_unregister_netdev(vsi);
err_register_netdev:
	ice_devlink_destroy_pf_port(pf);
err_devlink_create_pf_port:
err_init_mac_fltr:
	ice_decfg_netdev(vsi);
	return err;
}

/**
 * ice_unload - unload pf by stopping VSI and deinit hw
 * @pf: pointer to the pf instance
 *
 * This function has to be called under devl_lock.
 */
void ice_unload(struct ice_pf *pf)
{
	struct ice_vsi *vsi = ice_get_main_vsi(pf);

	devl_assert_locked(priv_to_devlink(pf));

	ice_deinit_features(pf);
	ice_deinit_rdma(pf);
	ice_tc_indir_block_unregister(vsi);
	ice_unregister_netdev(vsi);
	ice_devlink_destroy_pf_port(pf);
	ice_decfg_netdev(vsi);
}

/**
 * ice_probe - Device initialization routine
 * @pdev: PCI device information struct
 * @ent: entry in ice_pci_tbl
 *
 * Returns 0 on success, negative on failure
 */
static int
ice_probe(struct pci_dev *pdev, const struct pci_device_id __always_unused *ent)
{
	struct device *dev = &pdev->dev;
	struct ice_adapter *adapter;
	struct ice_pf *pf;
	struct ice_hw *hw;
	int err;

	if (pdev->is_virtfn) {
		dev_err(dev, "can't probe a virtual function\n");
		return -EINVAL;
	}

	/* when under a kdump kernel initiate a reset before enabling the
	 * device in order to clear out any pending DMA transactions. These
	 * transactions can cause some systems to machine check when doing
	 * the pcim_enable_device() below.
	 */
	if (is_kdump_kernel()) {
		pci_save_state(pdev);
		pci_clear_master(pdev);
		err = pcie_flr(pdev);
		if (err)
			return err;
		pci_restore_state(pdev);
	}

	/* this driver uses devres, see
	 * Documentation/driver-api/driver-model/devres.rst
	 */
	err = pcim_enable_device(pdev);
	if (err)
		return err;

	err = pcim_iomap_regions(pdev, BIT(ICE_BAR0), dev_driver_string(dev));
	if (err) {
		dev_err(dev, "BAR0 I/O map error %d\n", err);
		return err;
	}

	pf = ice_allocate_pf(dev);
	if (!pf)
		return -ENOMEM;

	/* initialize Auxiliary index to invalid value */
	pf->aux_idx = -1;

	/* set up for high or low DMA */
	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(dev, "DMA configuration failed: 0x%x\n", err);
		return err;
	}

	pci_set_master(pdev);

	adapter = ice_adapter_get(pdev);
	if (IS_ERR(adapter))
		return PTR_ERR(adapter);

	pf->pdev = pdev;
	pf->adapter = adapter;
	pci_set_drvdata(pdev, pf);
	set_bit(ICE_DOWN, pf->state);
	/* Disable service task until DOWN bit is cleared */
	set_bit(ICE_SERVICE_DIS, pf->state);

	hw = &pf->hw;
	hw->hw_addr = pcim_iomap_table(pdev)[ICE_BAR0];
	pci_save_state(pdev);

	hw->back = pf;
	hw->port_info = NULL;
	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	pci_read_config_byte(pdev, PCI_REVISION_ID, &hw->revision_id);
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_device_id = pdev->subsystem_device;
	hw->bus.device = PCI_SLOT(pdev->devfn);
	hw->bus.func = PCI_FUNC(pdev->devfn);
	ice_set_ctrlq_len(hw);

	pf->msg_enable = netif_msg_init(debug, ICE_DFLT_NETIF_M);

#ifndef CONFIG_DYNAMIC_DEBUG
	if (debug < -1)
		hw->debug_mask = debug;
#endif

	err = ice_init(pf);
	if (err)
		goto err_init;

	devl_lock(priv_to_devlink(pf));
	err = ice_load(pf);
	if (err)
		goto err_load;

	err = ice_init_devlink(pf);
	if (err)
		goto err_init_devlink;
	devl_unlock(priv_to_devlink(pf));

	return 0;

err_init_devlink:
	ice_unload(pf);
err_load:
	devl_unlock(priv_to_devlink(pf));
	ice_deinit(pf);
err_init:
	ice_adapter_put(pdev);
	pci_disable_device(pdev);
	return err;
}

/**
 * ice_set_wake - enable or disable Wake on LAN
 * @pf: pointer to the PF struct
 *
 * Simple helper for WoL control
 */
static void ice_set_wake(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;
	bool wol = pf->wol_ena;

	/* clear wake state, otherwise new wake events won't fire */
	wr32(hw, PFPM_WUS, U32_MAX);

	/* enable / disable APM wake up, no RMW needed */
	wr32(hw, PFPM_APM, wol ? PFPM_APM_APME_M : 0);

	/* set magic packet filter enabled */
	wr32(hw, PFPM_WUFC, wol ? PFPM_WUFC_MAG_M : 0);
}

/**
 * ice_setup_mc_magic_wake - setup device to wake on multicast magic packet
 * @pf: pointer to the PF struct
 *
 * Issue firmware command to enable multicast magic wake, making
 * sure that any locally administered address (LAA) is used for
 * wake, and that PF reset doesn't undo the LAA.
 */
static void ice_setup_mc_magic_wake(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	u8 mac_addr[ETH_ALEN];
	struct ice_vsi *vsi;
	int status;
	u8 flags;

	if (!pf->wol_ena)
		return;

	vsi = ice_get_main_vsi(pf);
	if (!vsi)
		return;

	/* Get current MAC address in case it's an LAA */
	if (vsi->netdev)
		ether_addr_copy(mac_addr, vsi->netdev->dev_addr);
	else
		ether_addr_copy(mac_addr, vsi->port_info->mac.perm_addr);

	flags = ICE_AQC_MAN_MAC_WR_MC_MAG_EN |
		ICE_AQC_MAN_MAC_UPDATE_LAA_WOL |
		ICE_AQC_MAN_MAC_WR_WOL_LAA_PFR_KEEP;

	status = ice_aq_manage_mac_write(hw, mac_addr, flags, NULL);
	if (status)
		dev_err(dev, "Failed to enable Multicast Magic Packet wake, err %d aq_err %s\n",
			status, ice_aq_str(hw->adminq.sq_last_status));
}

/**
 * ice_remove - Device removal routine
 * @pdev: PCI device information struct
 */
static void ice_remove(struct pci_dev *pdev)
{
	struct ice_pf *pf = pci_get_drvdata(pdev);
	int i;

	for (i = 0; i < ICE_MAX_RESET_WAIT; i++) {
		if (!ice_is_reset_in_progress(pf->state))
			break;
		msleep(100);
	}

	if (test_bit(ICE_FLAG_SRIOV_ENA, pf->flags)) {
		set_bit(ICE_VF_RESETS_DISABLED, pf->state);
		ice_free_vfs(pf);
	}

	ice_hwmon_exit(pf);

	ice_service_task_stop(pf);
	ice_aq_cancel_waiting_tasks(pf);
	set_bit(ICE_DOWN, pf->state);

	if (!ice_is_safe_mode(pf))
		ice_remove_arfs(pf);

	devl_lock(priv_to_devlink(pf));
	ice_deinit_devlink(pf);

	ice_unload(pf);
	devl_unlock(priv_to_devlink(pf));

	ice_deinit(pf);
	ice_vsi_release_all(pf);

	ice_setup_mc_magic_wake(pf);
	ice_set_wake(pf);

	ice_adapter_put(pdev);
	pci_disable_device(pdev);
}

/**
 * ice_shutdown - PCI callback for shutting down device
 * @pdev: PCI device information struct
 */
static void ice_shutdown(struct pci_dev *pdev)
{
	struct ice_pf *pf = pci_get_drvdata(pdev);

	ice_remove(pdev);

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, pf->wol_ena);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

/**
 * ice_prepare_for_shutdown - prep for PCI shutdown
 * @pf: board private structure
 *
 * Inform or close all dependent features in prep for PCI device shutdown
 */
static void ice_prepare_for_shutdown(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;
	u32 v;

	/* Notify VFs of impending reset */
	if (ice_check_sq_alive(hw, &hw->mailboxq))
		ice_vc_notify_reset(pf);

	dev_dbg(ice_pf_to_dev(pf), "Tearing down internal switch for shutdown\n");

	/* disable the VSIs and their queues that are not already DOWN */
	ice_pf_dis_all_vsi(pf, false);

	ice_for_each_vsi(pf, v)
		if (pf->vsi[v])
			pf->vsi[v]->vsi_num = 0;

	ice_shutdown_all_ctrlq(hw, true);
}

/**
 * ice_reinit_interrupt_scheme - Reinitialize interrupt scheme
 * @pf: board private structure to reinitialize
 *
 * This routine reinitialize interrupt scheme that was cleared during
 * power management suspend callback.
 *
 * This should be called during resume routine to re-allocate the q_vectors
 * and reacquire interrupts.
 */
static int ice_reinit_interrupt_scheme(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	int ret, v;

	/* Since we clear MSIX flag during suspend, we need to
	 * set it back during resume...
	 */

	ret = ice_init_interrupt_scheme(pf);
	if (ret) {
		dev_err(dev, "Failed to re-initialize interrupt %d\n", ret);
		return ret;
	}

	/* Remap vectors and rings, after successful re-init interrupts */
	ice_for_each_vsi(pf, v) {
		if (!pf->vsi[v])
			continue;

		ret = ice_vsi_alloc_q_vectors(pf->vsi[v]);
		if (ret)
			goto err_reinit;
		ice_vsi_map_rings_to_vectors(pf->vsi[v]);
		ice_vsi_set_napi_queues(pf->vsi[v]);
	}

	ret = ice_req_irq_msix_misc(pf);
	if (ret) {
		dev_err(dev, "Setting up misc vector failed after device suspend %d\n",
			ret);
		goto err_reinit;
	}

	return 0;

err_reinit:
	while (v--)
		if (pf->vsi[v])
			ice_vsi_free_q_vectors(pf->vsi[v]);

	return ret;
}

/**
 * ice_suspend
 * @dev: generic device information structure
 *
 * Power Management callback to quiesce the device and prepare
 * for D3 transition.
 */
static int ice_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct ice_pf *pf;
	int disabled, v;

	pf = pci_get_drvdata(pdev);

	if (!ice_pf_state_is_nominal(pf)) {
		dev_err(dev, "Device is not ready, no need to suspend it\n");
		return -EBUSY;
	}

	/* Stop watchdog tasks until resume completion.
	 * Even though it is most likely that the service task is
	 * disabled if the device is suspended or down, the service task's
	 * state is controlled by a different state bit, and we should
	 * store and honor whatever state that bit is in at this point.
	 */
	disabled = ice_service_task_stop(pf);

	ice_deinit_rdma(pf);

	/* Already suspended?, then there is nothing to do */
	if (test_and_set_bit(ICE_SUSPENDED, pf->state)) {
		if (!disabled)
			ice_service_task_restart(pf);
		return 0;
	}

	if (test_bit(ICE_DOWN, pf->state) ||
	    ice_is_reset_in_progress(pf->state)) {
		dev_err(dev, "can't suspend device in reset or already down\n");
		if (!disabled)
			ice_service_task_restart(pf);
		return 0;
	}

	ice_setup_mc_magic_wake(pf);

	ice_prepare_for_shutdown(pf);

	ice_set_wake(pf);

	/* Free vectors, clear the interrupt scheme and release IRQs
	 * for proper hibernation, especially with large number of CPUs.
	 * Otherwise hibernation might fail when mapping all the vectors back
	 * to CPU0.
	 */
	ice_free_irq_msix_misc(pf);
	ice_for_each_vsi(pf, v) {
		if (!pf->vsi[v])
			continue;
		ice_vsi_free_q_vectors(pf->vsi[v]);
	}
	ice_clear_interrupt_scheme(pf);

	pci_save_state(pdev);
	pci_wake_from_d3(pdev, pf->wol_ena);
	pci_set_power_state(pdev, PCI_D3hot);
	return 0;
}

/**
 * ice_resume - PM callback for waking up from D3
 * @dev: generic device information structure
 */
static int ice_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	enum ice_reset_req reset_type;
	struct ice_pf *pf;
	struct ice_hw *hw;
	int ret;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_save_state(pdev);

	if (!pci_device_is_present(pdev))
		return -ENODEV;

	ret = pci_enable_device_mem(pdev);
	if (ret) {
		dev_err(dev, "Cannot enable device after suspend\n");
		return ret;
	}

	pf = pci_get_drvdata(pdev);
	hw = &pf->hw;

	pf->wakeup_reason = rd32(hw, PFPM_WUS);
	ice_print_wake_reason(pf);

	/* We cleared the interrupt scheme when we suspended, so we need to
	 * restore it now to resume device functionality.
	 */
	ret = ice_reinit_interrupt_scheme(pf);
	if (ret)
		dev_err(dev, "Cannot restore interrupt scheme: %d\n", ret);

	ret = ice_init_rdma(pf);
	if (ret)
		dev_err(dev, "Reinitialize RDMA during resume failed: %d\n",
			ret);

	clear_bit(ICE_DOWN, pf->state);
	/* Now perform PF reset and rebuild */
	reset_type = ICE_RESET_PFR;
	/* re-enable service task for reset, but allow reset to schedule it */
	clear_bit(ICE_SERVICE_DIS, pf->state);

	if (ice_schedule_reset(pf, reset_type))
		dev_err(dev, "Reset during resume failed.\n");

	clear_bit(ICE_SUSPENDED, pf->state);
	ice_service_task_restart(pf);

	/* Restart the service task */
	mod_timer(&pf->serv_tmr, round_jiffies(jiffies + pf->serv_tmr_period));

	return 0;
}

/**
 * ice_pci_err_detected - warning that PCI error has been detected
 * @pdev: PCI device information struct
 * @err: the type of PCI error
 *
 * Called to warn that something happened on the PCI bus and the error handling
 * is in progress.  Allows the driver to gracefully prepare/handle PCI errors.
 */
static pci_ers_result_t
ice_pci_err_detected(struct pci_dev *pdev, pci_channel_state_t err)
{
	struct ice_pf *pf = pci_get_drvdata(pdev);

	if (!pf) {
		dev_err(&pdev->dev, "%s: unrecoverable device error %d\n",
			__func__, err);
		return PCI_ERS_RESULT_DISCONNECT;
	}

	if (!test_bit(ICE_SUSPENDED, pf->state)) {
		ice_service_task_stop(pf);

		if (!test_bit(ICE_PREPARED_FOR_RESET, pf->state)) {
			set_bit(ICE_PFR_REQ, pf->state);
			ice_prepare_for_reset(pf, ICE_RESET_PFR);
		}
	}

	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * ice_pci_err_slot_reset - a PCI slot reset has just happened
 * @pdev: PCI device information struct
 *
 * Called to determine if the driver can recover from the PCI slot reset by
 * using a register read to determine if the device is recoverable.
 */
static pci_ers_result_t ice_pci_err_slot_reset(struct pci_dev *pdev)
{
	struct ice_pf *pf = pci_get_drvdata(pdev);
	pci_ers_result_t result;
	int err;
	u32 reg;

	err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(&pdev->dev, "Cannot re-enable PCI device after reset, error %d\n",
			err);
		result = PCI_ERS_RESULT_DISCONNECT;
	} else {
		pci_set_master(pdev);
		pci_restore_state(pdev);
		pci_save_state(pdev);
		pci_wake_from_d3(pdev, false);

		/* Check for life */
		reg = rd32(&pf->hw, GLGEN_RTRIG);
		if (!reg)
			result = PCI_ERS_RESULT_RECOVERED;
		else
			result = PCI_ERS_RESULT_DISCONNECT;
	}

	return result;
}

/**
 * ice_pci_err_resume - restart operations after PCI error recovery
 * @pdev: PCI device information struct
 *
 * Called to allow the driver to bring things back up after PCI error and/or
 * reset recovery have finished
 */
static void ice_pci_err_resume(struct pci_dev *pdev)
{
	struct ice_pf *pf = pci_get_drvdata(pdev);

	if (!pf) {
		dev_err(&pdev->dev, "%s failed, device is unrecoverable\n",
			__func__);
		return;
	}

	if (test_bit(ICE_SUSPENDED, pf->state)) {
		dev_dbg(&pdev->dev, "%s failed to resume normal operations!\n",
			__func__);
		return;
	}

	ice_restore_all_vfs_msi_state(pf);

	ice_do_reset(pf, ICE_RESET_PFR);
	ice_service_task_restart(pf);
	mod_timer(&pf->serv_tmr, round_jiffies(jiffies + pf->serv_tmr_period));
}

/**
 * ice_pci_err_reset_prepare - prepare device driver for PCI reset
 * @pdev: PCI device information struct
 */
static void ice_pci_err_reset_prepare(struct pci_dev *pdev)
{
	struct ice_pf *pf = pci_get_drvdata(pdev);

	if (!test_bit(ICE_SUSPENDED, pf->state)) {
		ice_service_task_stop(pf);

		if (!test_bit(ICE_PREPARED_FOR_RESET, pf->state)) {
			set_bit(ICE_PFR_REQ, pf->state);
			ice_prepare_for_reset(pf, ICE_RESET_PFR);
		}
	}
}

/**
 * ice_pci_err_reset_done - PCI reset done, device driver reset can begin
 * @pdev: PCI device information struct
 */
static void ice_pci_err_reset_done(struct pci_dev *pdev)
{
	ice_pci_err_resume(pdev);
}

/* ice_pci_tbl - PCI Device ID Table
 *
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id ice_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E810C_BACKPLANE) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E810C_QSFP) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E810C_SFP) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E810_XXV_BACKPLANE) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E810_XXV_QSFP) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E810_XXV_SFP) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E823C_BACKPLANE) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E823C_QSFP) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E823C_SFP) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E823C_10G_BASE_T) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E823C_SGMII) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E822C_BACKPLANE) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E822C_QSFP) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E822C_SFP) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E822C_10G_BASE_T) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E822C_SGMII) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E822L_BACKPLANE) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E822L_SFP) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E822L_10G_BASE_T) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E822L_SGMII) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E823L_BACKPLANE) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E823L_SFP) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E823L_10G_BASE_T) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E823L_1GBE) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E823L_QSFP) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E822_SI_DFLT) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E825C_BACKPLANE), },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E825C_QSFP), },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E825C_SFP), },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E825C_SGMII), },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E830CC_BACKPLANE) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E830CC_QSFP56) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E830CC_SFP) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E830CC_SFP_DD) },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E830C_BACKPLANE), },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E830_XXV_BACKPLANE), },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E830C_QSFP), },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E830_XXV_QSFP), },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E830C_SFP), },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E830_XXV_SFP), },
	/* required last entry */
	{}
};
MODULE_DEVICE_TABLE(pci, ice_pci_tbl);

static DEFINE_SIMPLE_DEV_PM_OPS(ice_pm_ops, ice_suspend, ice_resume);

static const struct pci_error_handlers ice_pci_err_handler = {
	.error_detected = ice_pci_err_detected,
	.slot_reset = ice_pci_err_slot_reset,
	.reset_prepare = ice_pci_err_reset_prepare,
	.reset_done = ice_pci_err_reset_done,
	.resume = ice_pci_err_resume
};

static struct pci_driver ice_driver = {
	.name = KBUILD_MODNAME,
	.id_table = ice_pci_tbl,
	.probe = ice_probe,
	.remove = ice_remove,
	.driver.pm = pm_sleep_ptr(&ice_pm_ops),
	.shutdown = ice_shutdown,
	.sriov_configure = ice_sriov_configure,
	.sriov_get_vf_total_msix = ice_sriov_get_vf_total_msix,
	.sriov_set_msix_vec_count = ice_sriov_set_msix_vec_count,
	.err_handler = &ice_pci_err_handler
};

/**
 * ice_module_init - Driver registration routine
 *
 * ice_module_init is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 */
static int __init ice_module_init(void)
{
	int status = -ENOMEM;

	pr_info("%s\n", ice_driver_string);
	pr_info("%s\n", ice_copyright);

	ice_adv_lnk_speed_maps_init();

	ice_wq = alloc_workqueue("%s", 0, 0, KBUILD_MODNAME);
	if (!ice_wq) {
		pr_err("Failed to create workqueue\n");
		return status;
	}

	ice_lag_wq = alloc_ordered_workqueue("ice_lag_wq", 0);
	if (!ice_lag_wq) {
		pr_err("Failed to create LAG workqueue\n");
		goto err_dest_wq;
	}

	ice_debugfs_init();

	status = pci_register_driver(&ice_driver);
	if (status) {
		pr_err("failed to register PCI driver, err %d\n", status);
		goto err_dest_lag_wq;
	}

	return 0;

err_dest_lag_wq:
	destroy_workqueue(ice_lag_wq);
	ice_debugfs_exit();
err_dest_wq:
	destroy_workqueue(ice_wq);
	return status;
}
module_init(ice_module_init);

/**
 * ice_module_exit - Driver exit cleanup routine
 *
 * ice_module_exit is called just before the driver is removed
 * from memory.
 */
static void __exit ice_module_exit(void)
{
	pci_unregister_driver(&ice_driver);
	ice_debugfs_exit();
	destroy_workqueue(ice_wq);
	destroy_workqueue(ice_lag_wq);
	pr_info("module unloaded\n");
}
module_exit(ice_module_exit);

/**
 * ice_set_mac_address - NDO callback to set MAC address
 * @netdev: network interface device structure
 * @pi: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 */
static int ice_set_mac_address(struct net_device *netdev, void *pi)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	struct sockaddr *addr = pi;
	u8 old_mac[ETH_ALEN];
	u8 flags = 0;
	u8 *mac;
	int err;

	mac = (u8 *)addr->sa_data;

	if (!is_valid_ether_addr(mac))
		return -EADDRNOTAVAIL;

	if (test_bit(ICE_DOWN, pf->state) ||
	    ice_is_reset_in_progress(pf->state)) {
		netdev_err(netdev, "can't set mac %pM. device not ready\n",
			   mac);
		return -EBUSY;
	}

	if (ice_chnl_dmac_fltr_cnt(pf)) {
		netdev_err(netdev, "can't set mac %pM. Device has tc-flower filters, delete all of them and try again\n",
			   mac);
		return -EAGAIN;
	}

	netif_addr_lock_bh(netdev);
	ether_addr_copy(old_mac, netdev->dev_addr);
	/* change the netdev's MAC address */
	eth_hw_addr_set(netdev, mac);
	netif_addr_unlock_bh(netdev);

	/* Clean up old MAC filter. Not an error if old filter doesn't exist */
	err = ice_fltr_remove_mac(vsi, old_mac, ICE_FWD_TO_VSI);
	if (err && err != -ENOENT) {
		err = -EADDRNOTAVAIL;
		goto err_update_filters;
	}

	/* Add filter for new MAC. If filter exists, return success */
	err = ice_fltr_add_mac(vsi, mac, ICE_FWD_TO_VSI);
	if (err == -EEXIST) {
		/* Although this MAC filter is already present in hardware it's
		 * possible in some cases (e.g. bonding) that dev_addr was
		 * modified outside of the driver and needs to be restored back
		 * to this value.
		 */
		netdev_dbg(netdev, "filter for MAC %pM already exists\n", mac);

		return 0;
	} else if (err) {
		/* error if the new filter addition failed */
		err = -EADDRNOTAVAIL;
	}

err_update_filters:
	if (err) {
		netdev_err(netdev, "can't set MAC %pM. filter update failed\n",
			   mac);
		netif_addr_lock_bh(netdev);
		eth_hw_addr_set(netdev, old_mac);
		netif_addr_unlock_bh(netdev);
		return err;
	}

	netdev_dbg(vsi->netdev, "updated MAC address to %pM\n",
		   netdev->dev_addr);

	/* write new MAC address to the firmware */
	flags = ICE_AQC_MAN_MAC_UPDATE_LAA_WOL;
	err = ice_aq_manage_mac_write(hw, mac, flags, NULL);
	if (err) {
		netdev_err(netdev, "can't set MAC %pM. write to firmware failed error %d\n",
			   mac, err);
	}
	return 0;
}

/**
 * ice_set_rx_mode - NDO callback to set the netdev filters
 * @netdev: network interface device structure
 */
static void ice_set_rx_mode(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;

	if (!vsi || ice_is_switchdev_running(vsi->back))
		return;

	/* Set the flags to synchronize filters
	 * ndo_set_rx_mode may be triggered even without a change in netdev
	 * flags
	 */
	set_bit(ICE_VSI_UMAC_FLTR_CHANGED, vsi->state);
	set_bit(ICE_VSI_MMAC_FLTR_CHANGED, vsi->state);
	set_bit(ICE_FLAG_FLTR_SYNC, vsi->back->flags);

	/* schedule our worker thread which will take care of
	 * applying the new filter changes
	 */
	ice_service_task_schedule(vsi->back);
}

/**
 * ice_set_tx_maxrate - NDO callback to set the maximum per-queue bitrate
 * @netdev: network interface device structure
 * @queue_index: Queue ID
 * @maxrate: maximum bandwidth in Mbps
 */
static int
ice_set_tx_maxrate(struct net_device *netdev, int queue_index, u32 maxrate)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	u16 q_handle;
	int status;
	u8 tc;

	/* Validate maxrate requested is within permitted range */
	if (maxrate && (maxrate > (ICE_SCHED_MAX_BW / 1000))) {
		netdev_err(netdev, "Invalid max rate %d specified for the queue %d\n",
			   maxrate, queue_index);
		return -EINVAL;
	}

	q_handle = vsi->tx_rings[queue_index]->q_handle;
	tc = ice_dcb_get_tc(vsi, queue_index);

	vsi = ice_locate_vsi_using_queue(vsi, queue_index);
	if (!vsi) {
		netdev_err(netdev, "Invalid VSI for given queue %d\n",
			   queue_index);
		return -EINVAL;
	}

	/* Set BW back to default, when user set maxrate to 0 */
	if (!maxrate)
		status = ice_cfg_q_bw_dflt_lmt(vsi->port_info, vsi->idx, tc,
					       q_handle, ICE_MAX_BW);
	else
		status = ice_cfg_q_bw_lmt(vsi->port_info, vsi->idx, tc,
					  q_handle, ICE_MAX_BW, maxrate * 1000);
	if (status)
		netdev_err(netdev, "Unable to set Tx max rate, error %d\n",
			   status);

	return status;
}

/**
 * ice_fdb_add - add an entry to the hardware database
 * @ndm: the input from the stack
 * @tb: pointer to array of nladdr (unused)
 * @dev: the net device pointer
 * @addr: the MAC address entry being added
 * @vid: VLAN ID
 * @flags: instructions from stack about fdb operation
 * @extack: netlink extended ack
 */
static int
ice_fdb_add(struct ndmsg *ndm, struct nlattr __always_unused *tb[],
	    struct net_device *dev, const unsigned char *addr, u16 vid,
	    u16 flags, struct netlink_ext_ack __always_unused *extack)
{
	int err;

	if (vid) {
		netdev_err(dev, "VLANs aren't supported yet for dev_uc|mc_add()\n");
		return -EINVAL;
	}
	if (ndm->ndm_state && !(ndm->ndm_state & NUD_PERMANENT)) {
		netdev_err(dev, "FDB only supports static addresses\n");
		return -EINVAL;
	}

	if (is_unicast_ether_addr(addr) || is_link_local_ether_addr(addr))
		err = dev_uc_add_excl(dev, addr);
	else if (is_multicast_ether_addr(addr))
		err = dev_mc_add_excl(dev, addr);
	else
		err = -EINVAL;

	/* Only return duplicate errors if NLM_F_EXCL is set */
	if (err == -EEXIST && !(flags & NLM_F_EXCL))
		err = 0;

	return err;
}

/**
 * ice_fdb_del - delete an entry from the hardware database
 * @ndm: the input from the stack
 * @tb: pointer to array of nladdr (unused)
 * @dev: the net device pointer
 * @addr: the MAC address entry being added
 * @vid: VLAN ID
 * @extack: netlink extended ack
 */
static int
ice_fdb_del(struct ndmsg *ndm, __always_unused struct nlattr *tb[],
	    struct net_device *dev, const unsigned char *addr,
	    __always_unused u16 vid, struct netlink_ext_ack *extack)
{
	int err;

	if (ndm->ndm_state & NUD_PERMANENT) {
		netdev_err(dev, "FDB only supports static addresses\n");
		return -EINVAL;
	}

	if (is_unicast_ether_addr(addr))
		err = dev_uc_del(dev, addr);
	else if (is_multicast_ether_addr(addr))
		err = dev_mc_del(dev, addr);
	else
		err = -EINVAL;

	return err;
}

#define NETIF_VLAN_OFFLOAD_FEATURES	(NETIF_F_HW_VLAN_CTAG_RX | \
					 NETIF_F_HW_VLAN_CTAG_TX | \
					 NETIF_F_HW_VLAN_STAG_RX | \
					 NETIF_F_HW_VLAN_STAG_TX)

#define NETIF_VLAN_STRIPPING_FEATURES	(NETIF_F_HW_VLAN_CTAG_RX | \
					 NETIF_F_HW_VLAN_STAG_RX)

#define NETIF_VLAN_FILTERING_FEATURES	(NETIF_F_HW_VLAN_CTAG_FILTER | \
					 NETIF_F_HW_VLAN_STAG_FILTER)

/**
 * ice_fix_features - fix the netdev features flags based on device limitations
 * @netdev: ptr to the netdev that flags are being fixed on
 * @features: features that need to be checked and possibly fixed
 *
 * Make sure any fixups are made to features in this callback. This enables the
 * driver to not have to check unsupported configurations throughout the driver
 * because that's the responsiblity of this callback.
 *
 * Single VLAN Mode (SVM) Supported Features:
 *	NETIF_F_HW_VLAN_CTAG_FILTER
 *	NETIF_F_HW_VLAN_CTAG_RX
 *	NETIF_F_HW_VLAN_CTAG_TX
 *
 * Double VLAN Mode (DVM) Supported Features:
 *	NETIF_F_HW_VLAN_CTAG_FILTER
 *	NETIF_F_HW_VLAN_CTAG_RX
 *	NETIF_F_HW_VLAN_CTAG_TX
 *
 *	NETIF_F_HW_VLAN_STAG_FILTER
 *	NETIF_HW_VLAN_STAG_RX
 *	NETIF_HW_VLAN_STAG_TX
 *
 * Features that need fixing:
 *	Cannot simultaneously enable CTAG and STAG stripping and/or insertion.
 *	These are mutually exlusive as the VSI context cannot support multiple
 *	VLAN ethertypes simultaneously for stripping and/or insertion. If this
 *	is not done, then default to clearing the requested STAG offload
 *	settings.
 *
 *	All supported filtering has to be enabled or disabled together. For
 *	example, in DVM, CTAG and STAG filtering have to be enabled and disabled
 *	together. If this is not done, then default to VLAN filtering disabled.
 *	These are mutually exclusive as there is currently no way to
 *	enable/disable VLAN filtering based on VLAN ethertype when using VLAN
 *	prune rules.
 */
static netdev_features_t
ice_fix_features(struct net_device *netdev, netdev_features_t features)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	netdev_features_t req_vlan_fltr, cur_vlan_fltr;
	bool cur_ctag, cur_stag, req_ctag, req_stag;

	cur_vlan_fltr = netdev->features & NETIF_VLAN_FILTERING_FEATURES;
	cur_ctag = cur_vlan_fltr & NETIF_F_HW_VLAN_CTAG_FILTER;
	cur_stag = cur_vlan_fltr & NETIF_F_HW_VLAN_STAG_FILTER;

	req_vlan_fltr = features & NETIF_VLAN_FILTERING_FEATURES;
	req_ctag = req_vlan_fltr & NETIF_F_HW_VLAN_CTAG_FILTER;
	req_stag = req_vlan_fltr & NETIF_F_HW_VLAN_STAG_FILTER;

	if (req_vlan_fltr != cur_vlan_fltr) {
		if (ice_is_dvm_ena(&np->vsi->back->hw)) {
			if (req_ctag && req_stag) {
				features |= NETIF_VLAN_FILTERING_FEATURES;
			} else if (!req_ctag && !req_stag) {
				features &= ~NETIF_VLAN_FILTERING_FEATURES;
			} else if ((!cur_ctag && req_ctag && !cur_stag) ||
				   (!cur_stag && req_stag && !cur_ctag)) {
				features |= NETIF_VLAN_FILTERING_FEATURES;
				netdev_warn(netdev,  "802.1Q and 802.1ad VLAN filtering must be either both on or both off. VLAN filtering has been enabled for both types.\n");
			} else if ((cur_ctag && !req_ctag && cur_stag) ||
				   (cur_stag && !req_stag && cur_ctag)) {
				features &= ~NETIF_VLAN_FILTERING_FEATURES;
				netdev_warn(netdev,  "802.1Q and 802.1ad VLAN filtering must be either both on or both off. VLAN filtering has been disabled for both types.\n");
			}
		} else {
			if (req_vlan_fltr & NETIF_F_HW_VLAN_STAG_FILTER)
				netdev_warn(netdev, "cannot support requested 802.1ad filtering setting in SVM mode\n");

			if (req_vlan_fltr & NETIF_F_HW_VLAN_CTAG_FILTER)
				features |= NETIF_F_HW_VLAN_CTAG_FILTER;
		}
	}

	if ((features & (NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HW_VLAN_CTAG_TX)) &&
	    (features & (NETIF_F_HW_VLAN_STAG_RX | NETIF_F_HW_VLAN_STAG_TX))) {
		netdev_warn(netdev, "cannot support CTAG and STAG VLAN stripping and/or insertion simultaneously since CTAG and STAG offloads are mutually exclusive, clearing STAG offload settings\n");
		features &= ~(NETIF_F_HW_VLAN_STAG_RX |
			      NETIF_F_HW_VLAN_STAG_TX);
	}

	if (!(netdev->features & NETIF_F_RXFCS) &&
	    (features & NETIF_F_RXFCS) &&
	    (features & NETIF_VLAN_STRIPPING_FEATURES) &&
	    !ice_vsi_has_non_zero_vlans(np->vsi)) {
		netdev_warn(netdev, "Disabling VLAN stripping as FCS/CRC stripping is also disabled and there is no VLAN configured\n");
		features &= ~NETIF_VLAN_STRIPPING_FEATURES;
	}

	return features;
}

/**
 * ice_set_rx_rings_vlan_proto - update rings with new stripped VLAN proto
 * @vsi: PF's VSI
 * @vlan_ethertype: VLAN ethertype (802.1Q or 802.1ad) in network byte order
 *
 * Store current stripped VLAN proto in ring packet context,
 * so it can be accessed more efficiently by packet processing code.
 */
static void
ice_set_rx_rings_vlan_proto(struct ice_vsi *vsi, __be16 vlan_ethertype)
{
	u16 i;

	ice_for_each_alloc_rxq(vsi, i)
		vsi->rx_rings[i]->pkt_ctx.vlan_proto = vlan_ethertype;
}

/**
 * ice_set_vlan_offload_features - set VLAN offload features for the PF VSI
 * @vsi: PF's VSI
 * @features: features used to determine VLAN offload settings
 *
 * First, determine the vlan_ethertype based on the VLAN offload bits in
 * features. Then determine if stripping and insertion should be enabled or
 * disabled. Finally enable or disable VLAN stripping and insertion.
 */
static int
ice_set_vlan_offload_features(struct ice_vsi *vsi, netdev_features_t features)
{
	bool enable_stripping = true, enable_insertion = true;
	struct ice_vsi_vlan_ops *vlan_ops;
	int strip_err = 0, insert_err = 0;
	u16 vlan_ethertype = 0;

	vlan_ops = ice_get_compat_vsi_vlan_ops(vsi);

	if (features & (NETIF_F_HW_VLAN_STAG_RX | NETIF_F_HW_VLAN_STAG_TX))
		vlan_ethertype = ETH_P_8021AD;
	else if (features & (NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HW_VLAN_CTAG_TX))
		vlan_ethertype = ETH_P_8021Q;

	if (!(features & (NETIF_F_HW_VLAN_STAG_RX | NETIF_F_HW_VLAN_CTAG_RX)))
		enable_stripping = false;
	if (!(features & (NETIF_F_HW_VLAN_STAG_TX | NETIF_F_HW_VLAN_CTAG_TX)))
		enable_insertion = false;

	if (enable_stripping)
		strip_err = vlan_ops->ena_stripping(vsi, vlan_ethertype);
	else
		strip_err = vlan_ops->dis_stripping(vsi);

	if (enable_insertion)
		insert_err = vlan_ops->ena_insertion(vsi, vlan_ethertype);
	else
		insert_err = vlan_ops->dis_insertion(vsi);

	if (strip_err || insert_err)
		return -EIO;

	ice_set_rx_rings_vlan_proto(vsi, enable_stripping ?
				    htons(vlan_ethertype) : 0);

	return 0;
}

/**
 * ice_set_vlan_filtering_features - set VLAN filtering features for the PF VSI
 * @vsi: PF's VSI
 * @features: features used to determine VLAN filtering settings
 *
 * Enable or disable Rx VLAN filtering based on the VLAN filtering bits in the
 * features.
 */
static int
ice_set_vlan_filtering_features(struct ice_vsi *vsi, netdev_features_t features)
{
	struct ice_vsi_vlan_ops *vlan_ops = ice_get_compat_vsi_vlan_ops(vsi);
	int err = 0;

	/* support Single VLAN Mode (SVM) and Double VLAN Mode (DVM) by checking
	 * if either bit is set
	 */
	if (features &
	    (NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_VLAN_STAG_FILTER))
		err = vlan_ops->ena_rx_filtering(vsi);
	else
		err = vlan_ops->dis_rx_filtering(vsi);

	return err;
}

/**
 * ice_set_vlan_features - set VLAN settings based on suggested feature set
 * @netdev: ptr to the netdev being adjusted
 * @features: the feature set that the stack is suggesting
 *
 * Only update VLAN settings if the requested_vlan_features are different than
 * the current_vlan_features.
 */
static int
ice_set_vlan_features(struct net_device *netdev, netdev_features_t features)
{
	netdev_features_t current_vlan_features, requested_vlan_features;
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	int err;

	current_vlan_features = netdev->features & NETIF_VLAN_OFFLOAD_FEATURES;
	requested_vlan_features = features & NETIF_VLAN_OFFLOAD_FEATURES;
	if (current_vlan_features ^ requested_vlan_features) {
		if ((features & NETIF_F_RXFCS) &&
		    (features & NETIF_VLAN_STRIPPING_FEATURES)) {
			dev_err(ice_pf_to_dev(vsi->back),
				"To enable VLAN stripping, you must first enable FCS/CRC stripping\n");
			return -EIO;
		}

		err = ice_set_vlan_offload_features(vsi, features);
		if (err)
			return err;
	}

	current_vlan_features = netdev->features &
		NETIF_VLAN_FILTERING_FEATURES;
	requested_vlan_features = features & NETIF_VLAN_FILTERING_FEATURES;
	if (current_vlan_features ^ requested_vlan_features) {
		err = ice_set_vlan_filtering_features(vsi, features);
		if (err)
			return err;
	}

	return 0;
}

/**
 * ice_set_loopback - turn on/off loopback mode on underlying PF
 * @vsi: ptr to VSI
 * @ena: flag to indicate the on/off setting
 */
static int ice_set_loopback(struct ice_vsi *vsi, bool ena)
{
	bool if_running = netif_running(vsi->netdev);
	int ret;

	if (if_running && !test_and_set_bit(ICE_VSI_DOWN, vsi->state)) {
		ret = ice_down(vsi);
		if (ret) {
			netdev_err(vsi->netdev, "Preparing device to toggle loopback failed\n");
			return ret;
		}
	}
	ret = ice_aq_set_mac_loopback(&vsi->back->hw, ena, NULL);
	if (ret)
		netdev_err(vsi->netdev, "Failed to toggle loopback state\n");
	if (if_running)
		ret = ice_up(vsi);

	return ret;
}

/**
 * ice_set_features - set the netdev feature flags
 * @netdev: ptr to the netdev being adjusted
 * @features: the feature set that the stack is suggesting
 */
static int
ice_set_features(struct net_device *netdev, netdev_features_t features)
{
	netdev_features_t changed = netdev->features ^ features;
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	int ret = 0;

	/* Don't set any netdev advanced features with device in Safe Mode */
	if (ice_is_safe_mode(pf)) {
		dev_err(ice_pf_to_dev(pf),
			"Device is in Safe Mode - not enabling advanced netdev features\n");
		return ret;
	}

	/* Do not change setting during reset */
	if (ice_is_reset_in_progress(pf->state)) {
		dev_err(ice_pf_to_dev(pf),
			"Device is resetting, changing advanced netdev features temporarily unavailable.\n");
		return -EBUSY;
	}

	/* Multiple features can be changed in one call so keep features in
	 * separate if/else statements to guarantee each feature is checked
	 */
	if (changed & NETIF_F_RXHASH)
		ice_vsi_manage_rss_lut(vsi, !!(features & NETIF_F_RXHASH));

	ret = ice_set_vlan_features(netdev, features);
	if (ret)
		return ret;

	/* Turn on receive of FCS aka CRC, and after setting this
	 * flag the packet data will have the 4 byte CRC appended
	 */
	if (changed & NETIF_F_RXFCS) {
		if ((features & NETIF_F_RXFCS) &&
		    (features & NETIF_VLAN_STRIPPING_FEATURES)) {
			dev_err(ice_pf_to_dev(vsi->back),
				"To disable FCS/CRC stripping, you must first disable VLAN stripping\n");
			return -EIO;
		}

		ice_vsi_cfg_crc_strip(vsi, !!(features & NETIF_F_RXFCS));
		ret = ice_down_up(vsi);
		if (ret)
			return ret;
	}

	if (changed & NETIF_F_NTUPLE) {
		bool ena = !!(features & NETIF_F_NTUPLE);

		ice_vsi_manage_fdir(vsi, ena);
		ena ? ice_init_arfs(vsi) : ice_clear_arfs(vsi);
	}

	/* don't turn off hw_tc_offload when ADQ is already enabled */
	if (!(features & NETIF_F_HW_TC) && ice_is_adq_active(pf)) {
		dev_err(ice_pf_to_dev(pf), "ADQ is active, can't turn hw_tc_offload off\n");
		return -EACCES;
	}

	if (changed & NETIF_F_HW_TC) {
		bool ena = !!(features & NETIF_F_HW_TC);

		ena ? set_bit(ICE_FLAG_CLS_FLOWER, pf->flags) :
		      clear_bit(ICE_FLAG_CLS_FLOWER, pf->flags);
	}

	if (changed & NETIF_F_LOOPBACK)
		ret = ice_set_loopback(vsi, !!(features & NETIF_F_LOOPBACK));

	return ret;
}

/**
 * ice_vsi_vlan_setup - Setup VLAN offload properties on a PF VSI
 * @vsi: VSI to setup VLAN properties for
 */
static int ice_vsi_vlan_setup(struct ice_vsi *vsi)
{
	int err;

	err = ice_set_vlan_offload_features(vsi, vsi->netdev->features);
	if (err)
		return err;

	err = ice_set_vlan_filtering_features(vsi, vsi->netdev->features);
	if (err)
		return err;

	return ice_vsi_add_vlan_zero(vsi);
}

/**
 * ice_vsi_cfg_lan - Setup the VSI lan related config
 * @vsi: the VSI being configured
 *
 * Return 0 on success and negative value on error
 */
int ice_vsi_cfg_lan(struct ice_vsi *vsi)
{
	int err;

	if (vsi->netdev && vsi->type == ICE_VSI_PF) {
		ice_set_rx_mode(vsi->netdev);

		err = ice_vsi_vlan_setup(vsi);
		if (err)
			return err;
	}
	ice_vsi_cfg_dcb_rings(vsi);

	err = ice_vsi_cfg_lan_txqs(vsi);
	if (!err && ice_is_xdp_ena_vsi(vsi))
		err = ice_vsi_cfg_xdp_txqs(vsi);
	if (!err)
		err = ice_vsi_cfg_rxqs(vsi);

	return err;
}

/* THEORY OF MODERATION:
 * The ice driver hardware works differently than the hardware that DIMLIB was
 * originally made for. ice hardware doesn't have packet count limits that
 * can trigger an interrupt, but it *does* have interrupt rate limit support,
 * which is hard-coded to a limit of 250,000 ints/second.
 * If not using dynamic moderation, the INTRL value can be modified
 * by ethtool rx-usecs-high.
 */
struct ice_dim {
	/* the throttle rate for interrupts, basically worst case delay before
	 * an initial interrupt fires, value is stored in microseconds.
	 */
	u16 itr;
};

/* Make a different profile for Rx that doesn't allow quite so aggressive
 * moderation at the high end (it maxes out at 126us or about 8k interrupts a
 * second.
 */
static const struct ice_dim rx_profile[] = {
	{2},    /* 500,000 ints/s, capped at 250K by INTRL */
	{8},    /* 125,000 ints/s */
	{16},   /*  62,500 ints/s */
	{62},   /*  16,129 ints/s */
	{126}   /*   7,936 ints/s */
};

/* The transmit profile, which has the same sorts of values
 * as the previous struct
 */
static const struct ice_dim tx_profile[] = {
	{2},    /* 500,000 ints/s, capped at 250K by INTRL */
	{8},    /* 125,000 ints/s */
	{40},   /*  16,125 ints/s */
	{128},  /*   7,812 ints/s */
	{256}   /*   3,906 ints/s */
};

static void ice_tx_dim_work(struct work_struct *work)
{
	struct ice_ring_container *rc;
	struct dim *dim;
	u16 itr;

	dim = container_of(work, struct dim, work);
	rc = dim->priv;

	WARN_ON(dim->profile_ix >= ARRAY_SIZE(tx_profile));

	/* look up the values in our local table */
	itr = tx_profile[dim->profile_ix].itr;

	ice_trace(tx_dim_work, container_of(rc, struct ice_q_vector, tx), dim);
	ice_write_itr(rc, itr);

	dim->state = DIM_START_MEASURE;
}

static void ice_rx_dim_work(struct work_struct *work)
{
	struct ice_ring_container *rc;
	struct dim *dim;
	u16 itr;

	dim = container_of(work, struct dim, work);
	rc = dim->priv;

	WARN_ON(dim->profile_ix >= ARRAY_SIZE(rx_profile));

	/* look up the values in our local table */
	itr = rx_profile[dim->profile_ix].itr;

	ice_trace(rx_dim_work, container_of(rc, struct ice_q_vector, rx), dim);
	ice_write_itr(rc, itr);

	dim->state = DIM_START_MEASURE;
}

#define ICE_DIM_DEFAULT_PROFILE_IX 1

/**
 * ice_init_moderation - set up interrupt moderation
 * @q_vector: the vector containing rings to be configured
 *
 * Set up interrupt moderation registers, with the intent to do the right thing
 * when called from reset or from probe, and whether or not dynamic moderation
 * is enabled or not. Take special care to write all the registers in both
 * dynamic moderation mode or not in order to make sure hardware is in a known
 * state.
 */
static void ice_init_moderation(struct ice_q_vector *q_vector)
{
	struct ice_ring_container *rc;
	bool tx_dynamic, rx_dynamic;

	rc = &q_vector->tx;
	INIT_WORK(&rc->dim.work, ice_tx_dim_work);
	rc->dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_EQE;
	rc->dim.profile_ix = ICE_DIM_DEFAULT_PROFILE_IX;
	rc->dim.priv = rc;
	tx_dynamic = ITR_IS_DYNAMIC(rc);

	/* set the initial TX ITR to match the above */
	ice_write_itr(rc, tx_dynamic ?
		      tx_profile[rc->dim.profile_ix].itr : rc->itr_setting);

	rc = &q_vector->rx;
	INIT_WORK(&rc->dim.work, ice_rx_dim_work);
	rc->dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_EQE;
	rc->dim.profile_ix = ICE_DIM_DEFAULT_PROFILE_IX;
	rc->dim.priv = rc;
	rx_dynamic = ITR_IS_DYNAMIC(rc);

	/* set the initial RX ITR to match the above */
	ice_write_itr(rc, rx_dynamic ? rx_profile[rc->dim.profile_ix].itr :
				       rc->itr_setting);

	ice_set_q_vector_intrl(q_vector);
}

/**
 * ice_napi_enable_all - Enable NAPI for all q_vectors in the VSI
 * @vsi: the VSI being configured
 */
static void ice_napi_enable_all(struct ice_vsi *vsi)
{
	int q_idx;

	if (!vsi->netdev)
		return;

	ice_for_each_q_vector(vsi, q_idx) {
		struct ice_q_vector *q_vector = vsi->q_vectors[q_idx];

		ice_init_moderation(q_vector);

		if (q_vector->rx.rx_ring || q_vector->tx.tx_ring)
			napi_enable(&q_vector->napi);
	}
}

/**
 * ice_up_complete - Finish the last steps of bringing up a connection
 * @vsi: The VSI being configured
 *
 * Return 0 on success and negative value on error
 */
static int ice_up_complete(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	int err;

	ice_vsi_cfg_msix(vsi);

	/* Enable only Rx rings, Tx rings were enabled by the FW when the
	 * Tx queue group list was configured and the context bits were
	 * programmed using ice_vsi_cfg_txqs
	 */
	err = ice_vsi_start_all_rx_rings(vsi);
	if (err)
		return err;

	clear_bit(ICE_VSI_DOWN, vsi->state);
	ice_napi_enable_all(vsi);
	ice_vsi_ena_irq(vsi);

	if (vsi->port_info &&
	    (vsi->port_info->phy.link_info.link_info & ICE_AQ_LINK_UP) &&
	    vsi->netdev && vsi->type == ICE_VSI_PF) {
		ice_print_link_msg(vsi, true);
		netif_tx_start_all_queues(vsi->netdev);
		netif_carrier_on(vsi->netdev);
		ice_ptp_link_change(pf, pf->hw.pf_id, true);
	}

	/* Perform an initial read of the statistics registers now to
	 * set the baseline so counters are ready when interface is up
	 */
	ice_update_eth_stats(vsi);

	if (vsi->type == ICE_VSI_PF)
		ice_service_task_schedule(pf);

	return 0;
}

/**
 * ice_up - Bring the connection back up after being down
 * @vsi: VSI being configured
 */
int ice_up(struct ice_vsi *vsi)
{
	int err;

	err = ice_vsi_cfg_lan(vsi);
	if (!err)
		err = ice_up_complete(vsi);

	return err;
}

/**
 * ice_fetch_u64_stats_per_ring - get packets and bytes stats per ring
 * @syncp: pointer to u64_stats_sync
 * @stats: stats that pkts and bytes count will be taken from
 * @pkts: packets stats counter
 * @bytes: bytes stats counter
 *
 * This function fetches stats from the ring considering the atomic operations
 * that needs to be performed to read u64 values in 32 bit machine.
 */
void
ice_fetch_u64_stats_per_ring(struct u64_stats_sync *syncp,
			     struct ice_q_stats stats, u64 *pkts, u64 *bytes)
{
	unsigned int start;

	do {
		start = u64_stats_fetch_begin(syncp);
		*pkts = stats.pkts;
		*bytes = stats.bytes;
	} while (u64_stats_fetch_retry(syncp, start));
}

/**
 * ice_update_vsi_tx_ring_stats - Update VSI Tx ring stats counters
 * @vsi: the VSI to be updated
 * @vsi_stats: the stats struct to be updated
 * @rings: rings to work on
 * @count: number of rings
 */
static void
ice_update_vsi_tx_ring_stats(struct ice_vsi *vsi,
			     struct rtnl_link_stats64 *vsi_stats,
			     struct ice_tx_ring **rings, u16 count)
{
	u16 i;

	for (i = 0; i < count; i++) {
		struct ice_tx_ring *ring;
		u64 pkts = 0, bytes = 0;

		ring = READ_ONCE(rings[i]);
		if (!ring || !ring->ring_stats)
			continue;
		ice_fetch_u64_stats_per_ring(&ring->ring_stats->syncp,
					     ring->ring_stats->stats, &pkts,
					     &bytes);
		vsi_stats->tx_packets += pkts;
		vsi_stats->tx_bytes += bytes;
		vsi->tx_restart += ring->ring_stats->tx_stats.restart_q;
		vsi->tx_busy += ring->ring_stats->tx_stats.tx_busy;
		vsi->tx_linearize += ring->ring_stats->tx_stats.tx_linearize;
	}
}

/**
 * ice_update_vsi_ring_stats - Update VSI stats counters
 * @vsi: the VSI to be updated
 */
static void ice_update_vsi_ring_stats(struct ice_vsi *vsi)
{
	struct rtnl_link_stats64 *net_stats, *stats_prev;
	struct rtnl_link_stats64 *vsi_stats;
	struct ice_pf *pf = vsi->back;
	u64 pkts, bytes;
	int i;

	vsi_stats = kzalloc(sizeof(*vsi_stats), GFP_ATOMIC);
	if (!vsi_stats)
		return;

	/* reset non-netdev (extended) stats */
	vsi->tx_restart = 0;
	vsi->tx_busy = 0;
	vsi->tx_linearize = 0;
	vsi->rx_buf_failed = 0;
	vsi->rx_page_failed = 0;

	rcu_read_lock();

	/* update Tx rings counters */
	ice_update_vsi_tx_ring_stats(vsi, vsi_stats, vsi->tx_rings,
				     vsi->num_txq);

	/* update Rx rings counters */
	ice_for_each_rxq(vsi, i) {
		struct ice_rx_ring *ring = READ_ONCE(vsi->rx_rings[i]);
		struct ice_ring_stats *ring_stats;

		ring_stats = ring->ring_stats;
		ice_fetch_u64_stats_per_ring(&ring_stats->syncp,
					     ring_stats->stats, &pkts,
					     &bytes);
		vsi_stats->rx_packets += pkts;
		vsi_stats->rx_bytes += bytes;
		vsi->rx_buf_failed += ring_stats->rx_stats.alloc_buf_failed;
		vsi->rx_page_failed += ring_stats->rx_stats.alloc_page_failed;
	}

	/* update XDP Tx rings counters */
	if (ice_is_xdp_ena_vsi(vsi))
		ice_update_vsi_tx_ring_stats(vsi, vsi_stats, vsi->xdp_rings,
					     vsi->num_xdp_txq);

	rcu_read_unlock();

	net_stats = &vsi->net_stats;
	stats_prev = &vsi->net_stats_prev;

	/* Update netdev counters, but keep in mind that values could start at
	 * random value after PF reset. And as we increase the reported stat by
	 * diff of Prev-Cur, we need to be sure that Prev is valid. If it's not,
	 * let's skip this round.
	 */
	if (likely(pf->stat_prev_loaded)) {
		net_stats->tx_packets += vsi_stats->tx_packets - stats_prev->tx_packets;
		net_stats->tx_bytes += vsi_stats->tx_bytes - stats_prev->tx_bytes;
		net_stats->rx_packets += vsi_stats->rx_packets - stats_prev->rx_packets;
		net_stats->rx_bytes += vsi_stats->rx_bytes - stats_prev->rx_bytes;
	}

	stats_prev->tx_packets = vsi_stats->tx_packets;
	stats_prev->tx_bytes = vsi_stats->tx_bytes;
	stats_prev->rx_packets = vsi_stats->rx_packets;
	stats_prev->rx_bytes = vsi_stats->rx_bytes;

	kfree(vsi_stats);
}

/**
 * ice_update_vsi_stats - Update VSI stats counters
 * @vsi: the VSI to be updated
 */
void ice_update_vsi_stats(struct ice_vsi *vsi)
{
	struct rtnl_link_stats64 *cur_ns = &vsi->net_stats;
	struct ice_eth_stats *cur_es = &vsi->eth_stats;
	struct ice_pf *pf = vsi->back;

	if (test_bit(ICE_VSI_DOWN, vsi->state) ||
	    test_bit(ICE_CFG_BUSY, pf->state))
		return;

	/* get stats as recorded by Tx/Rx rings */
	ice_update_vsi_ring_stats(vsi);

	/* get VSI stats as recorded by the hardware */
	ice_update_eth_stats(vsi);

	cur_ns->tx_errors = cur_es->tx_errors;
	cur_ns->rx_dropped = cur_es->rx_discards;
	cur_ns->tx_dropped = cur_es->tx_discards;
	cur_ns->multicast = cur_es->rx_multicast;

	/* update some more netdev stats if this is main VSI */
	if (vsi->type == ICE_VSI_PF) {
		cur_ns->rx_crc_errors = pf->stats.crc_errors;
		cur_ns->rx_errors = pf->stats.crc_errors +
				    pf->stats.illegal_bytes +
				    pf->stats.rx_undersize +
				    pf->hw_csum_rx_error +
				    pf->stats.rx_jabber +
				    pf->stats.rx_fragments +
				    pf->stats.rx_oversize;
		/* record drops from the port level */
		cur_ns->rx_missed_errors = pf->stats.eth.rx_discards;
	}
}

/**
 * ice_update_pf_stats - Update PF port stats counters
 * @pf: PF whose stats needs to be updated
 */
void ice_update_pf_stats(struct ice_pf *pf)
{
	struct ice_hw_port_stats *prev_ps, *cur_ps;
	struct ice_hw *hw = &pf->hw;
	u16 fd_ctr_base;
	u8 port;

	port = hw->port_info->lport;
	prev_ps = &pf->stats_prev;
	cur_ps = &pf->stats;

	if (ice_is_reset_in_progress(pf->state))
		pf->stat_prev_loaded = false;

	ice_stat_update40(hw, GLPRT_GORCL(port), pf->stat_prev_loaded,
			  &prev_ps->eth.rx_bytes,
			  &cur_ps->eth.rx_bytes);

	ice_stat_update40(hw, GLPRT_UPRCL(port), pf->stat_prev_loaded,
			  &prev_ps->eth.rx_unicast,
			  &cur_ps->eth.rx_unicast);

	ice_stat_update40(hw, GLPRT_MPRCL(port), pf->stat_prev_loaded,
			  &prev_ps->eth.rx_multicast,
			  &cur_ps->eth.rx_multicast);

	ice_stat_update40(hw, GLPRT_BPRCL(port), pf->stat_prev_loaded,
			  &prev_ps->eth.rx_broadcast,
			  &cur_ps->eth.rx_broadcast);

	ice_stat_update32(hw, PRTRPB_RDPC, pf->stat_prev_loaded,
			  &prev_ps->eth.rx_discards,
			  &cur_ps->eth.rx_discards);

	ice_stat_update40(hw, GLPRT_GOTCL(port), pf->stat_prev_loaded,
			  &prev_ps->eth.tx_bytes,
			  &cur_ps->eth.tx_bytes);

	ice_stat_update40(hw, GLPRT_UPTCL(port), pf->stat_prev_loaded,
			  &prev_ps->eth.tx_unicast,
			  &cur_ps->eth.tx_unicast);

	ice_stat_update40(hw, GLPRT_MPTCL(port), pf->stat_prev_loaded,
			  &prev_ps->eth.tx_multicast,
			  &cur_ps->eth.tx_multicast);

	ice_stat_update40(hw, GLPRT_BPTCL(port), pf->stat_prev_loaded,
			  &prev_ps->eth.tx_broadcast,
			  &cur_ps->eth.tx_broadcast);

	ice_stat_update32(hw, GLPRT_TDOLD(port), pf->stat_prev_loaded,
			  &prev_ps->tx_dropped_link_down,
			  &cur_ps->tx_dropped_link_down);

	ice_stat_update40(hw, GLPRT_PRC64L(port), pf->stat_prev_loaded,
			  &prev_ps->rx_size_64, &cur_ps->rx_size_64);

	ice_stat_update40(hw, GLPRT_PRC127L(port), pf->stat_prev_loaded,
			  &prev_ps->rx_size_127, &cur_ps->rx_size_127);

	ice_stat_update40(hw, GLPRT_PRC255L(port), pf->stat_prev_loaded,
			  &prev_ps->rx_size_255, &cur_ps->rx_size_255);

	ice_stat_update40(hw, GLPRT_PRC511L(port), pf->stat_prev_loaded,
			  &prev_ps->rx_size_511, &cur_ps->rx_size_511);

	ice_stat_update40(hw, GLPRT_PRC1023L(port), pf->stat_prev_loaded,
			  &prev_ps->rx_size_1023, &cur_ps->rx_size_1023);

	ice_stat_update40(hw, GLPRT_PRC1522L(port), pf->stat_prev_loaded,
			  &prev_ps->rx_size_1522, &cur_ps->rx_size_1522);

	ice_stat_update40(hw, GLPRT_PRC9522L(port), pf->stat_prev_loaded,
			  &prev_ps->rx_size_big, &cur_ps->rx_size_big);

	ice_stat_update40(hw, GLPRT_PTC64L(port), pf->stat_prev_loaded,
			  &prev_ps->tx_size_64, &cur_ps->tx_size_64);

	ice_stat_update40(hw, GLPRT_PTC127L(port), pf->stat_prev_loaded,
			  &prev_ps->tx_size_127, &cur_ps->tx_size_127);

	ice_stat_update40(hw, GLPRT_PTC255L(port), pf->stat_prev_loaded,
			  &prev_ps->tx_size_255, &cur_ps->tx_size_255);

	ice_stat_update40(hw, GLPRT_PTC511L(port), pf->stat_prev_loaded,
			  &prev_ps->tx_size_511, &cur_ps->tx_size_511);

	ice_stat_update40(hw, GLPRT_PTC1023L(port), pf->stat_prev_loaded,
			  &prev_ps->tx_size_1023, &cur_ps->tx_size_1023);

	ice_stat_update40(hw, GLPRT_PTC1522L(port), pf->stat_prev_loaded,
			  &prev_ps->tx_size_1522, &cur_ps->tx_size_1522);

	ice_stat_update40(hw, GLPRT_PTC9522L(port), pf->stat_prev_loaded,
			  &prev_ps->tx_size_big, &cur_ps->tx_size_big);

	fd_ctr_base = hw->fd_ctr_base;

	ice_stat_update40(hw,
			  GLSTAT_FD_CNT0L(ICE_FD_SB_STAT_IDX(fd_ctr_base)),
			  pf->stat_prev_loaded, &prev_ps->fd_sb_match,
			  &cur_ps->fd_sb_match);
	ice_stat_update32(hw, GLPRT_LXONRXC(port), pf->stat_prev_loaded,
			  &prev_ps->link_xon_rx, &cur_ps->link_xon_rx);

	ice_stat_update32(hw, GLPRT_LXOFFRXC(port), pf->stat_prev_loaded,
			  &prev_ps->link_xoff_rx, &cur_ps->link_xoff_rx);

	ice_stat_update32(hw, GLPRT_LXONTXC(port), pf->stat_prev_loaded,
			  &prev_ps->link_xon_tx, &cur_ps->link_xon_tx);

	ice_stat_update32(hw, GLPRT_LXOFFTXC(port), pf->stat_prev_loaded,
			  &prev_ps->link_xoff_tx, &cur_ps->link_xoff_tx);

	ice_update_dcb_stats(pf);

	ice_stat_update32(hw, GLPRT_CRCERRS(port), pf->stat_prev_loaded,
			  &prev_ps->crc_errors, &cur_ps->crc_errors);

	ice_stat_update32(hw, GLPRT_ILLERRC(port), pf->stat_prev_loaded,
			  &prev_ps->illegal_bytes, &cur_ps->illegal_bytes);

	ice_stat_update32(hw, GLPRT_MLFC(port), pf->stat_prev_loaded,
			  &prev_ps->mac_local_faults,
			  &cur_ps->mac_local_faults);

	ice_stat_update32(hw, GLPRT_MRFC(port), pf->stat_prev_loaded,
			  &prev_ps->mac_remote_faults,
			  &cur_ps->mac_remote_faults);

	ice_stat_update32(hw, GLPRT_RUC(port), pf->stat_prev_loaded,
			  &prev_ps->rx_undersize, &cur_ps->rx_undersize);

	ice_stat_update32(hw, GLPRT_RFC(port), pf->stat_prev_loaded,
			  &prev_ps->rx_fragments, &cur_ps->rx_fragments);

	ice_stat_update32(hw, GLPRT_ROC(port), pf->stat_prev_loaded,
			  &prev_ps->rx_oversize, &cur_ps->rx_oversize);

	ice_stat_update32(hw, GLPRT_RJC(port), pf->stat_prev_loaded,
			  &prev_ps->rx_jabber, &cur_ps->rx_jabber);

	cur_ps->fd_sb_status = test_bit(ICE_FLAG_FD_ENA, pf->flags) ? 1 : 0;

	pf->stat_prev_loaded = true;
}

/**
 * ice_get_stats64 - get statistics for network device structure
 * @netdev: network interface device structure
 * @stats: main device statistics structure
 */
static
void ice_get_stats64(struct net_device *netdev, struct rtnl_link_stats64 *stats)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct rtnl_link_stats64 *vsi_stats;
	struct ice_vsi *vsi = np->vsi;

	vsi_stats = &vsi->net_stats;

	if (!vsi->num_txq || !vsi->num_rxq)
		return;

	/* netdev packet/byte stats come from ring counter. These are obtained
	 * by summing up ring counters (done by ice_update_vsi_ring_stats).
	 * But, only call the update routine and read the registers if VSI is
	 * not down.
	 */
	if (!test_bit(ICE_VSI_DOWN, vsi->state))
		ice_update_vsi_ring_stats(vsi);
	stats->tx_packets = vsi_stats->tx_packets;
	stats->tx_bytes = vsi_stats->tx_bytes;
	stats->rx_packets = vsi_stats->rx_packets;
	stats->rx_bytes = vsi_stats->rx_bytes;

	/* The rest of the stats can be read from the hardware but instead we
	 * just return values that the watchdog task has already obtained from
	 * the hardware.
	 */
	stats->multicast = vsi_stats->multicast;
	stats->tx_errors = vsi_stats->tx_errors;
	stats->tx_dropped = vsi_stats->tx_dropped;
	stats->rx_errors = vsi_stats->rx_errors;
	stats->rx_dropped = vsi_stats->rx_dropped;
	stats->rx_crc_errors = vsi_stats->rx_crc_errors;
	stats->rx_length_errors = vsi_stats->rx_length_errors;
}

/**
 * ice_napi_disable_all - Disable NAPI for all q_vectors in the VSI
 * @vsi: VSI having NAPI disabled
 */
static void ice_napi_disable_all(struct ice_vsi *vsi)
{
	int q_idx;

	if (!vsi->netdev)
		return;

	ice_for_each_q_vector(vsi, q_idx) {
		struct ice_q_vector *q_vector = vsi->q_vectors[q_idx];

		if (q_vector->rx.rx_ring || q_vector->tx.tx_ring)
			napi_disable(&q_vector->napi);

		cancel_work_sync(&q_vector->tx.dim.work);
		cancel_work_sync(&q_vector->rx.dim.work);
	}
}

/**
 * ice_vsi_dis_irq - Mask off queue interrupt generation on the VSI
 * @vsi: the VSI being un-configured
 */
static void ice_vsi_dis_irq(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	u32 val;
	int i;

	/* disable interrupt causation from each Rx queue; Tx queues are
	 * handled in ice_vsi_stop_tx_ring()
	 */
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
		synchronize_irq(vsi->q_vectors[i]->irq.virq);
}

/**
 * ice_down - Shutdown the connection
 * @vsi: The VSI being stopped
 *
 * Caller of this function is expected to set the vsi->state ICE_DOWN bit
 */
int ice_down(struct ice_vsi *vsi)
{
	int i, tx_err, rx_err, vlan_err = 0;

	WARN_ON(!test_bit(ICE_VSI_DOWN, vsi->state));

	if (vsi->netdev) {
		vlan_err = ice_vsi_del_vlan_zero(vsi);
		ice_ptp_link_change(vsi->back, vsi->back->hw.pf_id, false);
		netif_carrier_off(vsi->netdev);
		netif_tx_disable(vsi->netdev);
	}

	ice_vsi_dis_irq(vsi);

	tx_err = ice_vsi_stop_lan_tx_rings(vsi, ICE_NO_RESET, 0);
	if (tx_err)
		netdev_err(vsi->netdev, "Failed stop Tx rings, VSI %d error %d\n",
			   vsi->vsi_num, tx_err);
	if (!tx_err && ice_is_xdp_ena_vsi(vsi)) {
		tx_err = ice_vsi_stop_xdp_tx_rings(vsi);
		if (tx_err)
			netdev_err(vsi->netdev, "Failed stop XDP rings, VSI %d error %d\n",
				   vsi->vsi_num, tx_err);
	}

	rx_err = ice_vsi_stop_all_rx_rings(vsi);
	if (rx_err)
		netdev_err(vsi->netdev, "Failed stop Rx rings, VSI %d error %d\n",
			   vsi->vsi_num, rx_err);

	ice_napi_disable_all(vsi);

	ice_for_each_txq(vsi, i)
		ice_clean_tx_ring(vsi->tx_rings[i]);

	if (ice_is_xdp_ena_vsi(vsi))
		ice_for_each_xdp_txq(vsi, i)
			ice_clean_tx_ring(vsi->xdp_rings[i]);

	ice_for_each_rxq(vsi, i)
		ice_clean_rx_ring(vsi->rx_rings[i]);

	if (tx_err || rx_err || vlan_err) {
		netdev_err(vsi->netdev, "Failed to close VSI 0x%04X on switch 0x%04X\n",
			   vsi->vsi_num, vsi->vsw->sw_id);
		return -EIO;
	}

	return 0;
}

/**
 * ice_down_up - shutdown the VSI connection and bring it up
 * @vsi: the VSI to be reconnected
 */
int ice_down_up(struct ice_vsi *vsi)
{
	int ret;

	/* if DOWN already set, nothing to do */
	if (test_and_set_bit(ICE_VSI_DOWN, vsi->state))
		return 0;

	ret = ice_down(vsi);
	if (ret)
		return ret;

	ret = ice_up(vsi);
	if (ret) {
		netdev_err(vsi->netdev, "reallocating resources failed during netdev features change, may need to reload driver\n");
		return ret;
	}

	return 0;
}

/**
 * ice_vsi_setup_tx_rings - Allocate VSI Tx queue resources
 * @vsi: VSI having resources allocated
 *
 * Return 0 on success, negative on failure
 */
int ice_vsi_setup_tx_rings(struct ice_vsi *vsi)
{
	int i, err = 0;

	if (!vsi->num_txq) {
		dev_err(ice_pf_to_dev(vsi->back), "VSI %d has 0 Tx queues\n",
			vsi->vsi_num);
		return -EINVAL;
	}

	ice_for_each_txq(vsi, i) {
		struct ice_tx_ring *ring = vsi->tx_rings[i];

		if (!ring)
			return -EINVAL;

		if (vsi->netdev)
			ring->netdev = vsi->netdev;
		err = ice_setup_tx_ring(ring);
		if (err)
			break;
	}

	return err;
}

/**
 * ice_vsi_setup_rx_rings - Allocate VSI Rx queue resources
 * @vsi: VSI having resources allocated
 *
 * Return 0 on success, negative on failure
 */
int ice_vsi_setup_rx_rings(struct ice_vsi *vsi)
{
	int i, err = 0;

	if (!vsi->num_rxq) {
		dev_err(ice_pf_to_dev(vsi->back), "VSI %d has 0 Rx queues\n",
			vsi->vsi_num);
		return -EINVAL;
	}

	ice_for_each_rxq(vsi, i) {
		struct ice_rx_ring *ring = vsi->rx_rings[i];

		if (!ring)
			return -EINVAL;

		if (vsi->netdev)
			ring->netdev = vsi->netdev;
		err = ice_setup_rx_ring(ring);
		if (err)
			break;
	}

	return err;
}

/**
 * ice_vsi_open_ctrl - open control VSI for use
 * @vsi: the VSI to open
 *
 * Initialization of the Control VSI
 *
 * Returns 0 on success, negative value on error
 */
int ice_vsi_open_ctrl(struct ice_vsi *vsi)
{
	char int_name[ICE_INT_NAME_STR_LEN];
	struct ice_pf *pf = vsi->back;
	struct device *dev;
	int err;

	dev = ice_pf_to_dev(pf);
	/* allocate descriptors */
	err = ice_vsi_setup_tx_rings(vsi);
	if (err)
		goto err_setup_tx;

	err = ice_vsi_setup_rx_rings(vsi);
	if (err)
		goto err_setup_rx;

	err = ice_vsi_cfg_lan(vsi);
	if (err)
		goto err_setup_rx;

	snprintf(int_name, sizeof(int_name) - 1, "%s-%s:ctrl",
		 dev_driver_string(dev), dev_name(dev));
	err = ice_vsi_req_irq_msix(vsi, int_name);
	if (err)
		goto err_setup_rx;

	ice_vsi_cfg_msix(vsi);

	err = ice_vsi_start_all_rx_rings(vsi);
	if (err)
		goto err_up_complete;

	clear_bit(ICE_VSI_DOWN, vsi->state);
	ice_vsi_ena_irq(vsi);

	return 0;

err_up_complete:
	ice_down(vsi);
err_setup_rx:
	ice_vsi_free_rx_rings(vsi);
err_setup_tx:
	ice_vsi_free_tx_rings(vsi);

	return err;
}

/**
 * ice_vsi_open - Called when a network interface is made active
 * @vsi: the VSI to open
 *
 * Initialization of the VSI
 *
 * Returns 0 on success, negative value on error
 */
int ice_vsi_open(struct ice_vsi *vsi)
{
	char int_name[ICE_INT_NAME_STR_LEN];
	struct ice_pf *pf = vsi->back;
	int err;

	/* allocate descriptors */
	err = ice_vsi_setup_tx_rings(vsi);
	if (err)
		goto err_setup_tx;

	err = ice_vsi_setup_rx_rings(vsi);
	if (err)
		goto err_setup_rx;

	err = ice_vsi_cfg_lan(vsi);
	if (err)
		goto err_setup_rx;

	snprintf(int_name, sizeof(int_name) - 1, "%s-%s",
		 dev_driver_string(ice_pf_to_dev(pf)), vsi->netdev->name);
	err = ice_vsi_req_irq_msix(vsi, int_name);
	if (err)
		goto err_setup_rx;

	ice_vsi_cfg_netdev_tc(vsi, vsi->tc_cfg.ena_tc);

	if (vsi->type == ICE_VSI_PF) {
		/* Notify the stack of the actual queue counts. */
		err = netif_set_real_num_tx_queues(vsi->netdev, vsi->num_txq);
		if (err)
			goto err_set_qs;

		err = netif_set_real_num_rx_queues(vsi->netdev, vsi->num_rxq);
		if (err)
			goto err_set_qs;
	}

	err = ice_up_complete(vsi);
	if (err)
		goto err_up_complete;

	return 0;

err_up_complete:
	ice_down(vsi);
err_set_qs:
	ice_vsi_free_irq(vsi);
err_setup_rx:
	ice_vsi_free_rx_rings(vsi);
err_setup_tx:
	ice_vsi_free_tx_rings(vsi);

	return err;
}

/**
 * ice_vsi_release_all - Delete all VSIs
 * @pf: PF from which all VSIs are being removed
 */
static void ice_vsi_release_all(struct ice_pf *pf)
{
	int err, i;

	if (!pf->vsi)
		return;

	ice_for_each_vsi(pf, i) {
		if (!pf->vsi[i])
			continue;

		if (pf->vsi[i]->type == ICE_VSI_CHNL)
			continue;

		err = ice_vsi_release(pf->vsi[i]);
		if (err)
			dev_dbg(ice_pf_to_dev(pf), "Failed to release pf->vsi[%d], err %d, vsi_num = %d\n",
				i, err, pf->vsi[i]->vsi_num);
	}
}

/**
 * ice_vsi_rebuild_by_type - Rebuild VSI of a given type
 * @pf: pointer to the PF instance
 * @type: VSI type to rebuild
 *
 * Iterates through the pf->vsi array and rebuilds VSIs of the requested type
 */
static int ice_vsi_rebuild_by_type(struct ice_pf *pf, enum ice_vsi_type type)
{
	struct device *dev = ice_pf_to_dev(pf);
	int i, err;

	ice_for_each_vsi(pf, i) {
		struct ice_vsi *vsi = pf->vsi[i];

		if (!vsi || vsi->type != type)
			continue;

		/* rebuild the VSI */
		err = ice_vsi_rebuild(vsi, ICE_VSI_FLAG_INIT);
		if (err) {
			dev_err(dev, "rebuild VSI failed, err %d, VSI index %d, type %s\n",
				err, vsi->idx, ice_vsi_type_str(type));
			return err;
		}

		/* replay filters for the VSI */
		err = ice_replay_vsi(&pf->hw, vsi->idx);
		if (err) {
			dev_err(dev, "replay VSI failed, error %d, VSI index %d, type %s\n",
				err, vsi->idx, ice_vsi_type_str(type));
			return err;
		}

		/* Re-map HW VSI number, using VSI handle that has been
		 * previously validated in ice_replay_vsi() call above
		 */
		vsi->vsi_num = ice_get_hw_vsi_num(&pf->hw, vsi->idx);

		/* enable the VSI */
		err = ice_ena_vsi(vsi, false);
		if (err) {
			dev_err(dev, "enable VSI failed, err %d, VSI index %d, type %s\n",
				err, vsi->idx, ice_vsi_type_str(type));
			return err;
		}

		dev_info(dev, "VSI rebuilt. VSI index %d, type %s\n", vsi->idx,
			 ice_vsi_type_str(type));
	}

	return 0;
}

/**
 * ice_update_pf_netdev_link - Update PF netdev link status
 * @pf: pointer to the PF instance
 */
static void ice_update_pf_netdev_link(struct ice_pf *pf)
{
	bool link_up;
	int i;

	ice_for_each_vsi(pf, i) {
		struct ice_vsi *vsi = pf->vsi[i];

		if (!vsi || vsi->type != ICE_VSI_PF)
			return;

		ice_get_link_status(pf->vsi[i]->port_info, &link_up);
		if (link_up) {
			netif_carrier_on(pf->vsi[i]->netdev);
			netif_tx_wake_all_queues(pf->vsi[i]->netdev);
		} else {
			netif_carrier_off(pf->vsi[i]->netdev);
			netif_tx_stop_all_queues(pf->vsi[i]->netdev);
		}
	}
}

/**
 * ice_rebuild - rebuild after reset
 * @pf: PF to rebuild
 * @reset_type: type of reset
 *
 * Do not rebuild VF VSI in this flow because that is already handled via
 * ice_reset_all_vfs(). This is because requirements for resetting a VF after a
 * PFR/CORER/GLOBER/etc. are different than the normal flow. Also, we don't want
 * to reset/rebuild all the VF VSI twice.
 */
static void ice_rebuild(struct ice_pf *pf, enum ice_reset_req reset_type)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	bool dvm;
	int err;

	if (test_bit(ICE_DOWN, pf->state))
		goto clear_recovery;

	dev_dbg(dev, "rebuilding PF after reset_type=%d\n", reset_type);

#define ICE_EMP_RESET_SLEEP_MS 5000
	if (reset_type == ICE_RESET_EMPR) {
		/* If an EMP reset has occurred, any previously pending flash
		 * update will have completed. We no longer know whether or
		 * not the NVM update EMP reset is restricted.
		 */
		pf->fw_emp_reset_disabled = false;

		msleep(ICE_EMP_RESET_SLEEP_MS);
	}

	err = ice_init_all_ctrlq(hw);
	if (err) {
		dev_err(dev, "control queues init failed %d\n", err);
		goto err_init_ctrlq;
	}

	/* if DDP was previously loaded successfully */
	if (!ice_is_safe_mode(pf)) {
		/* reload the SW DB of filter tables */
		if (reset_type == ICE_RESET_PFR)
			ice_fill_blk_tbls(hw);
		else
			/* Reload DDP Package after CORER/GLOBR reset */
			ice_load_pkg(NULL, pf);
	}

	err = ice_clear_pf_cfg(hw);
	if (err) {
		dev_err(dev, "clear PF configuration failed %d\n", err);
		goto err_init_ctrlq;
	}

	ice_clear_pxe_mode(hw);

	err = ice_init_nvm(hw);
	if (err) {
		dev_err(dev, "ice_init_nvm failed %d\n", err);
		goto err_init_ctrlq;
	}

	err = ice_get_caps(hw);
	if (err) {
		dev_err(dev, "ice_get_caps failed %d\n", err);
		goto err_init_ctrlq;
	}

	err = ice_aq_set_mac_cfg(hw, ICE_AQ_SET_MAC_FRAME_SIZE_MAX, NULL);
	if (err) {
		dev_err(dev, "set_mac_cfg failed %d\n", err);
		goto err_init_ctrlq;
	}

	dvm = ice_is_dvm_ena(hw);

	err = ice_aq_set_port_params(pf->hw.port_info, dvm, NULL);
	if (err)
		goto err_init_ctrlq;

	err = ice_sched_init_port(hw->port_info);
	if (err)
		goto err_sched_init_port;

	/* start misc vector */
	err = ice_req_irq_msix_misc(pf);
	if (err) {
		dev_err(dev, "misc vector setup failed: %d\n", err);
		goto err_sched_init_port;
	}

	if (test_bit(ICE_FLAG_FD_ENA, pf->flags)) {
		wr32(hw, PFQF_FD_ENA, PFQF_FD_ENA_FD_ENA_M);
		if (!rd32(hw, PFQF_FD_SIZE)) {
			u16 unused, guar, b_effort;

			guar = hw->func_caps.fd_fltr_guar;
			b_effort = hw->func_caps.fd_fltr_best_effort;

			/* force guaranteed filter pool for PF */
			ice_alloc_fd_guar_item(hw, &unused, guar);
			/* force shared filter pool for PF */
			ice_alloc_fd_shrd_item(hw, &unused, b_effort);
		}
	}

	if (test_bit(ICE_FLAG_DCB_ENA, pf->flags))
		ice_dcb_rebuild(pf);

	/* If the PF previously had enabled PTP, PTP init needs to happen before
	 * the VSI rebuild. If not, this causes the PTP link status events to
	 * fail.
	 */
	if (test_bit(ICE_FLAG_PTP_SUPPORTED, pf->flags))
		ice_ptp_rebuild(pf, reset_type);

	if (ice_is_feature_supported(pf, ICE_F_GNSS))
		ice_gnss_init(pf);

	/* rebuild PF VSI */
	err = ice_vsi_rebuild_by_type(pf, ICE_VSI_PF);
	if (err) {
		dev_err(dev, "PF VSI rebuild failed: %d\n", err);
		goto err_vsi_rebuild;
	}

	if (reset_type == ICE_RESET_PFR) {
		err = ice_rebuild_channels(pf);
		if (err) {
			dev_err(dev, "failed to rebuild and replay ADQ VSIs, err %d\n",
				err);
			goto err_vsi_rebuild;
		}
	}

	/* If Flow Director is active */
	if (test_bit(ICE_FLAG_FD_ENA, pf->flags)) {
		err = ice_vsi_rebuild_by_type(pf, ICE_VSI_CTRL);
		if (err) {
			dev_err(dev, "control VSI rebuild failed: %d\n", err);
			goto err_vsi_rebuild;
		}

		/* replay HW Flow Director recipes */
		if (hw->fdir_prof)
			ice_fdir_replay_flows(hw);

		/* replay Flow Director filters */
		ice_fdir_replay_fltrs(pf);

		ice_rebuild_arfs(pf);
	}

	ice_update_pf_netdev_link(pf);

	/* tell the firmware we are up */
	err = ice_send_version(pf);
	if (err) {
		dev_err(dev, "Rebuild failed due to error sending driver version: %d\n",
			err);
		goto err_vsi_rebuild;
	}

	ice_replay_post(hw);

	/* if we get here, reset flow is successful */
	clear_bit(ICE_RESET_FAILED, pf->state);

	ice_plug_aux_dev(pf);
	if (ice_is_feature_supported(pf, ICE_F_SRIOV_LAG))
		ice_lag_rebuild(pf);

	/* Restore timestamp mode settings after VSI rebuild */
	ice_ptp_restore_timestamp_mode(pf);
	return;

err_vsi_rebuild:
err_sched_init_port:
	ice_sched_cleanup_all(hw);
err_init_ctrlq:
	ice_shutdown_all_ctrlq(hw, false);
	set_bit(ICE_RESET_FAILED, pf->state);
clear_recovery:
	/* set this bit in PF state to control service task scheduling */
	set_bit(ICE_NEEDS_RESTART, pf->state);
	dev_err(dev, "Rebuild failed, unload and reload driver\n");
}

/**
 * ice_change_mtu - NDO callback to change the MTU
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 */
static int ice_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	struct bpf_prog *prog;
	u8 count = 0;
	int err = 0;

	if (new_mtu == (int)netdev->mtu) {
		netdev_warn(netdev, "MTU is already %u\n", netdev->mtu);
		return 0;
	}

	prog = vsi->xdp_prog;
	if (prog && !prog->aux->xdp_has_frags) {
		int frame_size = ice_max_xdp_frame_size(vsi);

		if (new_mtu + ICE_ETH_PKT_HDR_PAD > frame_size) {
			netdev_err(netdev, "max MTU for XDP usage is %d\n",
				   frame_size - ICE_ETH_PKT_HDR_PAD);
			return -EINVAL;
		}
	} else if (test_bit(ICE_FLAG_LEGACY_RX, pf->flags)) {
		if (new_mtu + ICE_ETH_PKT_HDR_PAD > ICE_MAX_FRAME_LEGACY_RX) {
			netdev_err(netdev, "Too big MTU for legacy-rx; Max is %d\n",
				   ICE_MAX_FRAME_LEGACY_RX - ICE_ETH_PKT_HDR_PAD);
			return -EINVAL;
		}
	}

	/* if a reset is in progress, wait for some time for it to complete */
	do {
		if (ice_is_reset_in_progress(pf->state)) {
			count++;
			usleep_range(1000, 2000);
		} else {
			break;
		}

	} while (count < 100);

	if (count == 100) {
		netdev_err(netdev, "can't change MTU. Device is busy\n");
		return -EBUSY;
	}

	WRITE_ONCE(netdev->mtu, (unsigned int)new_mtu);
	err = ice_down_up(vsi);
	if (err)
		return err;

	netdev_dbg(netdev, "changed MTU to %d\n", new_mtu);
	set_bit(ICE_FLAG_MTU_CHANGED, pf->flags);

	return err;
}

/**
 * ice_eth_ioctl - Access the hwtstamp interface
 * @netdev: network interface device structure
 * @ifr: interface request data
 * @cmd: ioctl command
 */
static int ice_eth_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_pf *pf = np->vsi->back;

	switch (cmd) {
	case SIOCGHWTSTAMP:
		return ice_ptp_get_ts_config(pf, ifr);
	case SIOCSHWTSTAMP:
		return ice_ptp_set_ts_config(pf, ifr);
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * ice_aq_str - convert AQ err code to a string
 * @aq_err: the AQ error code to convert
 */
const char *ice_aq_str(enum ice_aq_err aq_err)
{
	switch (aq_err) {
	case ICE_AQ_RC_OK:
		return "OK";
	case ICE_AQ_RC_EPERM:
		return "ICE_AQ_RC_EPERM";
	case ICE_AQ_RC_ENOENT:
		return "ICE_AQ_RC_ENOENT";
	case ICE_AQ_RC_ENOMEM:
		return "ICE_AQ_RC_ENOMEM";
	case ICE_AQ_RC_EBUSY:
		return "ICE_AQ_RC_EBUSY";
	case ICE_AQ_RC_EEXIST:
		return "ICE_AQ_RC_EEXIST";
	case ICE_AQ_RC_EINVAL:
		return "ICE_AQ_RC_EINVAL";
	case ICE_AQ_RC_ENOSPC:
		return "ICE_AQ_RC_ENOSPC";
	case ICE_AQ_RC_ENOSYS:
		return "ICE_AQ_RC_ENOSYS";
	case ICE_AQ_RC_EMODE:
		return "ICE_AQ_RC_EMODE";
	case ICE_AQ_RC_ENOSEC:
		return "ICE_AQ_RC_ENOSEC";
	case ICE_AQ_RC_EBADSIG:
		return "ICE_AQ_RC_EBADSIG";
	case ICE_AQ_RC_ESVN:
		return "ICE_AQ_RC_ESVN";
	case ICE_AQ_RC_EBADMAN:
		return "ICE_AQ_RC_EBADMAN";
	case ICE_AQ_RC_EBADBUF:
		return "ICE_AQ_RC_EBADBUF";
	}

	return "ICE_AQ_RC_UNKNOWN";
}

/**
 * ice_set_rss_lut - Set RSS LUT
 * @vsi: Pointer to VSI structure
 * @lut: Lookup table
 * @lut_size: Lookup table size
 *
 * Returns 0 on success, negative on failure
 */
int ice_set_rss_lut(struct ice_vsi *vsi, u8 *lut, u16 lut_size)
{
	struct ice_aq_get_set_rss_lut_params params = {};
	struct ice_hw *hw = &vsi->back->hw;
	int status;

	if (!lut)
		return -EINVAL;

	params.vsi_handle = vsi->idx;
	params.lut_size = lut_size;
	params.lut_type = vsi->rss_lut_type;
	params.lut = lut;

	status = ice_aq_set_rss_lut(hw, &params);
	if (status)
		dev_err(ice_pf_to_dev(vsi->back), "Cannot set RSS lut, err %d aq_err %s\n",
			status, ice_aq_str(hw->adminq.sq_last_status));

	return status;
}

/**
 * ice_set_rss_key - Set RSS key
 * @vsi: Pointer to the VSI structure
 * @seed: RSS hash seed
 *
 * Returns 0 on success, negative on failure
 */
int ice_set_rss_key(struct ice_vsi *vsi, u8 *seed)
{
	struct ice_hw *hw = &vsi->back->hw;
	int status;

	if (!seed)
		return -EINVAL;

	status = ice_aq_set_rss_key(hw, vsi->idx, (struct ice_aqc_get_set_rss_keys *)seed);
	if (status)
		dev_err(ice_pf_to_dev(vsi->back), "Cannot set RSS key, err %d aq_err %s\n",
			status, ice_aq_str(hw->adminq.sq_last_status));

	return status;
}

/**
 * ice_get_rss_lut - Get RSS LUT
 * @vsi: Pointer to VSI structure
 * @lut: Buffer to store the lookup table entries
 * @lut_size: Size of buffer to store the lookup table entries
 *
 * Returns 0 on success, negative on failure
 */
int ice_get_rss_lut(struct ice_vsi *vsi, u8 *lut, u16 lut_size)
{
	struct ice_aq_get_set_rss_lut_params params = {};
	struct ice_hw *hw = &vsi->back->hw;
	int status;

	if (!lut)
		return -EINVAL;

	params.vsi_handle = vsi->idx;
	params.lut_size = lut_size;
	params.lut_type = vsi->rss_lut_type;
	params.lut = lut;

	status = ice_aq_get_rss_lut(hw, &params);
	if (status)
		dev_err(ice_pf_to_dev(vsi->back), "Cannot get RSS lut, err %d aq_err %s\n",
			status, ice_aq_str(hw->adminq.sq_last_status));

	return status;
}

/**
 * ice_get_rss_key - Get RSS key
 * @vsi: Pointer to VSI structure
 * @seed: Buffer to store the key in
 *
 * Returns 0 on success, negative on failure
 */
int ice_get_rss_key(struct ice_vsi *vsi, u8 *seed)
{
	struct ice_hw *hw = &vsi->back->hw;
	int status;

	if (!seed)
		return -EINVAL;

	status = ice_aq_get_rss_key(hw, vsi->idx, (struct ice_aqc_get_set_rss_keys *)seed);
	if (status)
		dev_err(ice_pf_to_dev(vsi->back), "Cannot get RSS key, err %d aq_err %s\n",
			status, ice_aq_str(hw->adminq.sq_last_status));

	return status;
}

/**
 * ice_set_rss_hfunc - Set RSS HASH function
 * @vsi: Pointer to VSI structure
 * @hfunc: hash function (ICE_AQ_VSI_Q_OPT_RSS_*)
 *
 * Returns 0 on success, negative on failure
 */
int ice_set_rss_hfunc(struct ice_vsi *vsi, u8 hfunc)
{
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx *ctx;
	bool symm;
	int err;

	if (hfunc == vsi->rss_hfunc)
		return 0;

	if (hfunc != ICE_AQ_VSI_Q_OPT_RSS_HASH_TPLZ &&
	    hfunc != ICE_AQ_VSI_Q_OPT_RSS_HASH_SYM_TPLZ)
		return -EOPNOTSUPP;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_Q_OPT_VALID);
	ctx->info.q_opt_rss = vsi->info.q_opt_rss;
	ctx->info.q_opt_rss &= ~ICE_AQ_VSI_Q_OPT_RSS_HASH_M;
	ctx->info.q_opt_rss |=
		FIELD_PREP(ICE_AQ_VSI_Q_OPT_RSS_HASH_M, hfunc);
	ctx->info.q_opt_tc = vsi->info.q_opt_tc;
	ctx->info.q_opt_flags = vsi->info.q_opt_rss;

	err = ice_update_vsi(hw, vsi->idx, ctx, NULL);
	if (err) {
		dev_err(ice_pf_to_dev(vsi->back), "Failed to configure RSS hash for VSI %d, error %d\n",
			vsi->vsi_num, err);
	} else {
		vsi->info.q_opt_rss = ctx->info.q_opt_rss;
		vsi->rss_hfunc = hfunc;
		netdev_info(vsi->netdev, "Hash function set to: %sToeplitz\n",
			    hfunc == ICE_AQ_VSI_Q_OPT_RSS_HASH_SYM_TPLZ ?
			    "Symmetric " : "");
	}
	kfree(ctx);
	if (err)
		return err;

	/* Fix the symmetry setting for all existing RSS configurations */
	symm = !!(hfunc == ICE_AQ_VSI_Q_OPT_RSS_HASH_SYM_TPLZ);
	return ice_set_rss_cfg_symm(hw, vsi, symm);
}

/**
 * ice_bridge_getlink - Get the hardware bridge mode
 * @skb: skb buff
 * @pid: process ID
 * @seq: RTNL message seq
 * @dev: the netdev being configured
 * @filter_mask: filter mask passed in
 * @nlflags: netlink flags passed in
 *
 * Return the bridge mode (VEB/VEPA)
 */
static int
ice_bridge_getlink(struct sk_buff *skb, u32 pid, u32 seq,
		   struct net_device *dev, u32 filter_mask, int nlflags)
{
	struct ice_netdev_priv *np = netdev_priv(dev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	u16 bmode;

	bmode = pf->first_sw->bridge_mode;

	return ndo_dflt_bridge_getlink(skb, pid, seq, dev, bmode, 0, 0, nlflags,
				       filter_mask, NULL);
}

/**
 * ice_vsi_update_bridge_mode - Update VSI for switching bridge mode (VEB/VEPA)
 * @vsi: Pointer to VSI structure
 * @bmode: Hardware bridge mode (VEB/VEPA)
 *
 * Returns 0 on success, negative on failure
 */
static int ice_vsi_update_bridge_mode(struct ice_vsi *vsi, u16 bmode)
{
	struct ice_aqc_vsi_props *vsi_props;
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx *ctxt;
	int ret;

	vsi_props = &vsi->info;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	ctxt->info = vsi->info;

	if (bmode == BRIDGE_MODE_VEB)
		/* change from VEPA to VEB mode */
		ctxt->info.sw_flags |= ICE_AQ_VSI_SW_FLAG_ALLOW_LB;
	else
		/* change from VEB to VEPA mode */
		ctxt->info.sw_flags &= ~ICE_AQ_VSI_SW_FLAG_ALLOW_LB;
	ctxt->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_SW_VALID);

	ret = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (ret) {
		dev_err(ice_pf_to_dev(vsi->back), "update VSI for bridge mode failed, bmode = %d err %d aq_err %s\n",
			bmode, ret, ice_aq_str(hw->adminq.sq_last_status));
		goto out;
	}
	/* Update sw flags for book keeping */
	vsi_props->sw_flags = ctxt->info.sw_flags;

out:
	kfree(ctxt);
	return ret;
}

/**
 * ice_bridge_setlink - Set the hardware bridge mode
 * @dev: the netdev being configured
 * @nlh: RTNL message
 * @flags: bridge setlink flags
 * @extack: netlink extended ack
 *
 * Sets the bridge mode (VEB/VEPA) of the switch to which the netdev (VSI) is
 * hooked up to. Iterates through the PF VSI list and sets the loopback mode (if
 * not already set for all VSIs connected to this switch. And also update the
 * unicast switch filter rules for the corresponding switch of the netdev.
 */
static int
ice_bridge_setlink(struct net_device *dev, struct nlmsghdr *nlh,
		   u16 __always_unused flags,
		   struct netlink_ext_ack __always_unused *extack)
{
	struct ice_netdev_priv *np = netdev_priv(dev);
	struct ice_pf *pf = np->vsi->back;
	struct nlattr *attr, *br_spec;
	struct ice_hw *hw = &pf->hw;
	struct ice_sw *pf_sw;
	int rem, v, err = 0;

	pf_sw = pf->first_sw;
	/* find the attribute in the netlink message */
	br_spec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg), IFLA_AF_SPEC);
	if (!br_spec)
		return -EINVAL;

	nla_for_each_nested_type(attr, IFLA_BRIDGE_MODE, br_spec, rem) {
		__u16 mode = nla_get_u16(attr);

		if (mode != BRIDGE_MODE_VEPA && mode != BRIDGE_MODE_VEB)
			return -EINVAL;
		/* Continue  if bridge mode is not being flipped */
		if (mode == pf_sw->bridge_mode)
			continue;
		/* Iterates through the PF VSI list and update the loopback
		 * mode of the VSI
		 */
		ice_for_each_vsi(pf, v) {
			if (!pf->vsi[v])
				continue;
			err = ice_vsi_update_bridge_mode(pf->vsi[v], mode);
			if (err)
				return err;
		}

		hw->evb_veb = (mode == BRIDGE_MODE_VEB);
		/* Update the unicast switch filter rules for the corresponding
		 * switch of the netdev
		 */
		err = ice_update_sw_rule_bridge_mode(hw);
		if (err) {
			netdev_err(dev, "switch rule update failed, mode = %d err %d aq_err %s\n",
				   mode, err,
				   ice_aq_str(hw->adminq.sq_last_status));
			/* revert hw->evb_veb */
			hw->evb_veb = (pf_sw->bridge_mode == BRIDGE_MODE_VEB);
			return err;
		}

		pf_sw->bridge_mode = mode;
	}

	return 0;
}

/**
 * ice_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 * @txqueue: Tx queue
 */
static void ice_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_tx_ring *tx_ring = NULL;
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	u32 i;

	pf->tx_timeout_count++;

	/* Check if PFC is enabled for the TC to which the queue belongs
	 * to. If yes then Tx timeout is not caused by a hung queue, no
	 * need to reset and rebuild
	 */
	if (ice_is_pfc_causing_hung_q(pf, txqueue)) {
		dev_info(ice_pf_to_dev(pf), "Fake Tx hang detected on queue %u, timeout caused by PFC storm\n",
			 txqueue);
		return;
	}

	/* now that we have an index, find the tx_ring struct */
	ice_for_each_txq(vsi, i)
		if (vsi->tx_rings[i] && vsi->tx_rings[i]->desc)
			if (txqueue == vsi->tx_rings[i]->q_index) {
				tx_ring = vsi->tx_rings[i];
				break;
			}

	/* Reset recovery level if enough time has elapsed after last timeout.
	 * Also ensure no new reset action happens before next timeout period.
	 */
	if (time_after(jiffies, (pf->tx_timeout_last_recovery + HZ * 20)))
		pf->tx_timeout_recovery_level = 1;
	else if (time_before(jiffies, (pf->tx_timeout_last_recovery +
				       netdev->watchdog_timeo)))
		return;

	if (tx_ring) {
		struct ice_hw *hw = &pf->hw;
		u32 head, val = 0;

		head = FIELD_GET(QTX_COMM_HEAD_HEAD_M,
				 rd32(hw, QTX_COMM_HEAD(vsi->txq_map[txqueue])));
		/* Read interrupt register */
		val = rd32(hw, GLINT_DYN_CTL(tx_ring->q_vector->reg_idx));

		netdev_info(netdev, "tx_timeout: VSI_num: %d, Q %u, NTC: 0x%x, HW_HEAD: 0x%x, NTU: 0x%x, INT: 0x%x\n",
			    vsi->vsi_num, txqueue, tx_ring->next_to_clean,
			    head, tx_ring->next_to_use, val);
	}

	pf->tx_timeout_last_recovery = jiffies;
	netdev_info(netdev, "tx_timeout recovery level %d, txqueue %u\n",
		    pf->tx_timeout_recovery_level, txqueue);

	switch (pf->tx_timeout_recovery_level) {
	case 1:
		set_bit(ICE_PFR_REQ, pf->state);
		break;
	case 2:
		set_bit(ICE_CORER_REQ, pf->state);
		break;
	case 3:
		set_bit(ICE_GLOBR_REQ, pf->state);
		break;
	default:
		netdev_err(netdev, "tx_timeout recovery unsuccessful, device is in unrecoverable state.\n");
		set_bit(ICE_DOWN, pf->state);
		set_bit(ICE_VSI_NEEDS_RESTART, vsi->state);
		set_bit(ICE_SERVICE_DIS, pf->state);
		break;
	}

	ice_service_task_schedule(pf);
	pf->tx_timeout_recovery_level++;
}

/**
 * ice_setup_tc_cls_flower - flower classifier offloads
 * @np: net device to configure
 * @filter_dev: device on which filter is added
 * @cls_flower: offload data
 */
static int
ice_setup_tc_cls_flower(struct ice_netdev_priv *np,
			struct net_device *filter_dev,
			struct flow_cls_offload *cls_flower)
{
	struct ice_vsi *vsi = np->vsi;

	if (cls_flower->common.chain_index)
		return -EOPNOTSUPP;

	switch (cls_flower->command) {
	case FLOW_CLS_REPLACE:
		return ice_add_cls_flower(filter_dev, vsi, cls_flower);
	case FLOW_CLS_DESTROY:
		return ice_del_cls_flower(vsi, cls_flower);
	default:
		return -EINVAL;
	}
}

/**
 * ice_setup_tc_block_cb - callback handler registered for TC block
 * @type: TC SETUP type
 * @type_data: TC flower offload data that contains user input
 * @cb_priv: netdev private data
 */
static int
ice_setup_tc_block_cb(enum tc_setup_type type, void *type_data, void *cb_priv)
{
	struct ice_netdev_priv *np = cb_priv;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return ice_setup_tc_cls_flower(np, np->vsi->netdev,
					       type_data);
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * ice_validate_mqprio_qopt - Validate TCF input parameters
 * @vsi: Pointer to VSI
 * @mqprio_qopt: input parameters for mqprio queue configuration
 *
 * This function validates MQPRIO params, such as qcount (power of 2 wherever
 * needed), and make sure user doesn't specify qcount and BW rate limit
 * for TCs, which are more than "num_tc"
 */
static int
ice_validate_mqprio_qopt(struct ice_vsi *vsi,
			 struct tc_mqprio_qopt_offload *mqprio_qopt)
{
	int non_power_of_2_qcount = 0;
	struct ice_pf *pf = vsi->back;
	int max_rss_q_cnt = 0;
	u64 sum_min_rate = 0;
	struct device *dev;
	int i, speed;
	u8 num_tc;

	if (vsi->type != ICE_VSI_PF)
		return -EINVAL;

	if (mqprio_qopt->qopt.offset[0] != 0 ||
	    mqprio_qopt->qopt.num_tc < 1 ||
	    mqprio_qopt->qopt.num_tc > ICE_CHNL_MAX_TC)
		return -EINVAL;

	dev = ice_pf_to_dev(pf);
	vsi->ch_rss_size = 0;
	num_tc = mqprio_qopt->qopt.num_tc;
	speed = ice_get_link_speed_kbps(vsi);

	for (i = 0; num_tc; i++) {
		int qcount = mqprio_qopt->qopt.count[i];
		u64 max_rate, min_rate, rem;

		if (!qcount)
			return -EINVAL;

		if (is_power_of_2(qcount)) {
			if (non_power_of_2_qcount &&
			    qcount > non_power_of_2_qcount) {
				dev_err(dev, "qcount[%d] cannot be greater than non power of 2 qcount[%d]\n",
					qcount, non_power_of_2_qcount);
				return -EINVAL;
			}
			if (qcount > max_rss_q_cnt)
				max_rss_q_cnt = qcount;
		} else {
			if (non_power_of_2_qcount &&
			    qcount != non_power_of_2_qcount) {
				dev_err(dev, "Only one non power of 2 qcount allowed[%d,%d]\n",
					qcount, non_power_of_2_qcount);
				return -EINVAL;
			}
			if (qcount < max_rss_q_cnt) {
				dev_err(dev, "non power of 2 qcount[%d] cannot be less than other qcount[%d]\n",
					qcount, max_rss_q_cnt);
				return -EINVAL;
			}
			max_rss_q_cnt = qcount;
			non_power_of_2_qcount = qcount;
		}

		/* TC command takes input in K/N/Gbps or K/M/Gbit etc but
		 * converts the bandwidth rate limit into Bytes/s when
		 * passing it down to the driver. So convert input bandwidth
		 * from Bytes/s to Kbps
		 */
		max_rate = mqprio_qopt->max_rate[i];
		max_rate = div_u64(max_rate, ICE_BW_KBPS_DIVISOR);

		/* min_rate is minimum guaranteed rate and it can't be zero */
		min_rate = mqprio_qopt->min_rate[i];
		min_rate = div_u64(min_rate, ICE_BW_KBPS_DIVISOR);
		sum_min_rate += min_rate;

		if (min_rate && min_rate < ICE_MIN_BW_LIMIT) {
			dev_err(dev, "TC%d: min_rate(%llu Kbps) < %u Kbps\n", i,
				min_rate, ICE_MIN_BW_LIMIT);
			return -EINVAL;
		}

		if (max_rate && max_rate > speed) {
			dev_err(dev, "TC%d: max_rate(%llu Kbps) > link speed of %u Kbps\n",
				i, max_rate, speed);
			return -EINVAL;
		}

		iter_div_u64_rem(min_rate, ICE_MIN_BW_LIMIT, &rem);
		if (rem) {
			dev_err(dev, "TC%d: Min Rate not multiple of %u Kbps",
				i, ICE_MIN_BW_LIMIT);
			return -EINVAL;
		}

		iter_div_u64_rem(max_rate, ICE_MIN_BW_LIMIT, &rem);
		if (rem) {
			dev_err(dev, "TC%d: Max Rate not multiple of %u Kbps",
				i, ICE_MIN_BW_LIMIT);
			return -EINVAL;
		}

		/* min_rate can't be more than max_rate, except when max_rate
		 * is zero (implies max_rate sought is max line rate). In such
		 * a case min_rate can be more than max.
		 */
		if (max_rate && min_rate > max_rate) {
			dev_err(dev, "min_rate %llu Kbps can't be more than max_rate %llu Kbps\n",
				min_rate, max_rate);
			return -EINVAL;
		}

		if (i >= mqprio_qopt->qopt.num_tc - 1)
			break;
		if (mqprio_qopt->qopt.offset[i + 1] !=
		    (mqprio_qopt->qopt.offset[i] + qcount))
			return -EINVAL;
	}
	if (vsi->num_rxq <
	    (mqprio_qopt->qopt.offset[i] + mqprio_qopt->qopt.count[i]))
		return -EINVAL;
	if (vsi->num_txq <
	    (mqprio_qopt->qopt.offset[i] + mqprio_qopt->qopt.count[i]))
		return -EINVAL;

	if (sum_min_rate && sum_min_rate > (u64)speed) {
		dev_err(dev, "Invalid min Tx rate(%llu) Kbps > speed (%u) Kbps specified\n",
			sum_min_rate, speed);
		return -EINVAL;
	}

	/* make sure vsi->ch_rss_size is set correctly based on TC's qcount */
	vsi->ch_rss_size = max_rss_q_cnt;

	return 0;
}

/**
 * ice_add_vsi_to_fdir - add a VSI to the flow director group for PF
 * @pf: ptr to PF device
 * @vsi: ptr to VSI
 */
static int ice_add_vsi_to_fdir(struct ice_pf *pf, struct ice_vsi *vsi)
{
	struct device *dev = ice_pf_to_dev(pf);
	bool added = false;
	struct ice_hw *hw;
	int flow;

	if (!(vsi->num_gfltr || vsi->num_bfltr))
		return -EINVAL;

	hw = &pf->hw;
	for (flow = 0; flow < ICE_FLTR_PTYPE_MAX; flow++) {
		struct ice_fd_hw_prof *prof;
		int tun, status;
		u64 entry_h;

		if (!(hw->fdir_prof && hw->fdir_prof[flow] &&
		      hw->fdir_prof[flow]->cnt))
			continue;

		for (tun = 0; tun < ICE_FD_HW_SEG_MAX; tun++) {
			enum ice_flow_priority prio;

			/* add this VSI to FDir profile for this flow */
			prio = ICE_FLOW_PRIO_NORMAL;
			prof = hw->fdir_prof[flow];
			status = ice_flow_add_entry(hw, ICE_BLK_FD,
						    prof->prof_id[tun],
						    prof->vsi_h[0], vsi->idx,
						    prio, prof->fdir_seg[tun],
						    &entry_h);
			if (status) {
				dev_err(dev, "channel VSI idx %d, not able to add to group %d\n",
					vsi->idx, flow);
				continue;
			}

			prof->entry_h[prof->cnt][tun] = entry_h;
		}

		/* store VSI for filter replay and delete */
		prof->vsi_h[prof->cnt] = vsi->idx;
		prof->cnt++;

		added = true;
		dev_dbg(dev, "VSI idx %d added to fdir group %d\n", vsi->idx,
			flow);
	}

	if (!added)
		dev_dbg(dev, "VSI idx %d not added to fdir groups\n", vsi->idx);

	return 0;
}

/**
 * ice_add_channel - add a channel by adding VSI
 * @pf: ptr to PF device
 * @sw_id: underlying HW switching element ID
 * @ch: ptr to channel structure
 *
 * Add a channel (VSI) using add_vsi and queue_map
 */
static int ice_add_channel(struct ice_pf *pf, u16 sw_id, struct ice_channel *ch)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_vsi *vsi;

	if (ch->type != ICE_VSI_CHNL) {
		dev_err(dev, "add new VSI failed, ch->type %d\n", ch->type);
		return -EINVAL;
	}

	vsi = ice_chnl_vsi_setup(pf, pf->hw.port_info, ch);
	if (!vsi || vsi->type != ICE_VSI_CHNL) {
		dev_err(dev, "create chnl VSI failure\n");
		return -EINVAL;
	}

	ice_add_vsi_to_fdir(pf, vsi);

	ch->sw_id = sw_id;
	ch->vsi_num = vsi->vsi_num;
	ch->info.mapping_flags = vsi->info.mapping_flags;
	ch->ch_vsi = vsi;
	/* set the back pointer of channel for newly created VSI */
	vsi->ch = ch;

	memcpy(&ch->info.q_mapping, &vsi->info.q_mapping,
	       sizeof(vsi->info.q_mapping));
	memcpy(&ch->info.tc_mapping, vsi->info.tc_mapping,
	       sizeof(vsi->info.tc_mapping));

	return 0;
}

/**
 * ice_chnl_cfg_res
 * @vsi: the VSI being setup
 * @ch: ptr to channel structure
 *
 * Configure channel specific resources such as rings, vector.
 */
static void ice_chnl_cfg_res(struct ice_vsi *vsi, struct ice_channel *ch)
{
	int i;

	for (i = 0; i < ch->num_txq; i++) {
		struct ice_q_vector *tx_q_vector, *rx_q_vector;
		struct ice_ring_container *rc;
		struct ice_tx_ring *tx_ring;
		struct ice_rx_ring *rx_ring;

		tx_ring = vsi->tx_rings[ch->base_q + i];
		rx_ring = vsi->rx_rings[ch->base_q + i];
		if (!tx_ring || !rx_ring)
			continue;

		/* setup ring being channel enabled */
		tx_ring->ch = ch;
		rx_ring->ch = ch;

		/* following code block sets up vector specific attributes */
		tx_q_vector = tx_ring->q_vector;
		rx_q_vector = rx_ring->q_vector;
		if (!tx_q_vector && !rx_q_vector)
			continue;

		if (tx_q_vector) {
			tx_q_vector->ch = ch;
			/* setup Tx and Rx ITR setting if DIM is off */
			rc = &tx_q_vector->tx;
			if (!ITR_IS_DYNAMIC(rc))
				ice_write_itr(rc, rc->itr_setting);
		}
		if (rx_q_vector) {
			rx_q_vector->ch = ch;
			/* setup Tx and Rx ITR setting if DIM is off */
			rc = &rx_q_vector->rx;
			if (!ITR_IS_DYNAMIC(rc))
				ice_write_itr(rc, rc->itr_setting);
		}
	}

	/* it is safe to assume that, if channel has non-zero num_t[r]xq, then
	 * GLINT_ITR register would have written to perform in-context
	 * update, hence perform flush
	 */
	if (ch->num_txq || ch->num_rxq)
		ice_flush(&vsi->back->hw);
}

/**
 * ice_cfg_chnl_all_res - configure channel resources
 * @vsi: pte to main_vsi
 * @ch: ptr to channel structure
 *
 * This function configures channel specific resources such as flow-director
 * counter index, and other resources such as queues, vectors, ITR settings
 */
static void
ice_cfg_chnl_all_res(struct ice_vsi *vsi, struct ice_channel *ch)
{
	/* configure channel (aka ADQ) resources such as queues, vectors,
	 * ITR settings for channel specific vectors and anything else
	 */
	ice_chnl_cfg_res(vsi, ch);
}

/**
 * ice_setup_hw_channel - setup new channel
 * @pf: ptr to PF device
 * @vsi: the VSI being setup
 * @ch: ptr to channel structure
 * @sw_id: underlying HW switching element ID
 * @type: type of channel to be created (VMDq2/VF)
 *
 * Setup new channel (VSI) based on specified type (VMDq2/VF)
 * and configures Tx rings accordingly
 */
static int
ice_setup_hw_channel(struct ice_pf *pf, struct ice_vsi *vsi,
		     struct ice_channel *ch, u16 sw_id, u8 type)
{
	struct device *dev = ice_pf_to_dev(pf);
	int ret;

	ch->base_q = vsi->next_base_q;
	ch->type = type;

	ret = ice_add_channel(pf, sw_id, ch);
	if (ret) {
		dev_err(dev, "failed to add_channel using sw_id %u\n", sw_id);
		return ret;
	}

	/* configure/setup ADQ specific resources */
	ice_cfg_chnl_all_res(vsi, ch);

	/* make sure to update the next_base_q so that subsequent channel's
	 * (aka ADQ) VSI queue map is correct
	 */
	vsi->next_base_q = vsi->next_base_q + ch->num_rxq;
	dev_dbg(dev, "added channel: vsi_num %u, num_rxq %u\n", ch->vsi_num,
		ch->num_rxq);

	return 0;
}

/**
 * ice_setup_channel - setup new channel using uplink element
 * @pf: ptr to PF device
 * @vsi: the VSI being setup
 * @ch: ptr to channel structure
 *
 * Setup new channel (VSI) based on specified type (VMDq2/VF)
 * and uplink switching element
 */
static bool
ice_setup_channel(struct ice_pf *pf, struct ice_vsi *vsi,
		  struct ice_channel *ch)
{
	struct device *dev = ice_pf_to_dev(pf);
	u16 sw_id;
	int ret;

	if (vsi->type != ICE_VSI_PF) {
		dev_err(dev, "unsupported parent VSI type(%d)\n", vsi->type);
		return false;
	}

	sw_id = pf->first_sw->sw_id;

	/* create channel (VSI) */
	ret = ice_setup_hw_channel(pf, vsi, ch, sw_id, ICE_VSI_CHNL);
	if (ret) {
		dev_err(dev, "failed to setup hw_channel\n");
		return false;
	}
	dev_dbg(dev, "successfully created channel()\n");

	return ch->ch_vsi ? true : false;
}

/**
 * ice_set_bw_limit - setup BW limit for Tx traffic based on max_tx_rate
 * @vsi: VSI to be configured
 * @max_tx_rate: max Tx rate in Kbps to be configured as maximum BW limit
 * @min_tx_rate: min Tx rate in Kbps to be configured as minimum BW limit
 */
static int
ice_set_bw_limit(struct ice_vsi *vsi, u64 max_tx_rate, u64 min_tx_rate)
{
	int err;

	err = ice_set_min_bw_limit(vsi, min_tx_rate);
	if (err)
		return err;

	return ice_set_max_bw_limit(vsi, max_tx_rate);
}

/**
 * ice_create_q_channel - function to create channel
 * @vsi: VSI to be configured
 * @ch: ptr to channel (it contains channel specific params)
 *
 * This function creates channel (VSI) using num_queues specified by user,
 * reconfigs RSS if needed.
 */
static int ice_create_q_channel(struct ice_vsi *vsi, struct ice_channel *ch)
{
	struct ice_pf *pf = vsi->back;
	struct device *dev;

	if (!ch)
		return -EINVAL;

	dev = ice_pf_to_dev(pf);
	if (!ch->num_txq || !ch->num_rxq) {
		dev_err(dev, "Invalid num_queues requested: %d\n", ch->num_rxq);
		return -EINVAL;
	}

	if (!vsi->cnt_q_avail || vsi->cnt_q_avail < ch->num_txq) {
		dev_err(dev, "cnt_q_avail (%u) less than num_queues %d\n",
			vsi->cnt_q_avail, ch->num_txq);
		return -EINVAL;
	}

	if (!ice_setup_channel(pf, vsi, ch)) {
		dev_info(dev, "Failed to setup channel\n");
		return -EINVAL;
	}
	/* configure BW rate limit */
	if (ch->ch_vsi && (ch->max_tx_rate || ch->min_tx_rate)) {
		int ret;

		ret = ice_set_bw_limit(ch->ch_vsi, ch->max_tx_rate,
				       ch->min_tx_rate);
		if (ret)
			dev_err(dev, "failed to set Tx rate of %llu Kbps for VSI(%u)\n",
				ch->max_tx_rate, ch->ch_vsi->vsi_num);
		else
			dev_dbg(dev, "set Tx rate of %llu Kbps for VSI(%u)\n",
				ch->max_tx_rate, ch->ch_vsi->vsi_num);
	}

	vsi->cnt_q_avail -= ch->num_txq;

	return 0;
}

/**
 * ice_rem_all_chnl_fltrs - removes all channel filters
 * @pf: ptr to PF, TC-flower based filter are tracked at PF level
 *
 * Remove all advanced switch filters only if they are channel specific
 * tc-flower based filter
 */
static void ice_rem_all_chnl_fltrs(struct ice_pf *pf)
{
	struct ice_tc_flower_fltr *fltr;
	struct hlist_node *node;

	/* to remove all channel filters, iterate an ordered list of filters */
	hlist_for_each_entry_safe(fltr, node,
				  &pf->tc_flower_fltr_list,
				  tc_flower_node) {
		struct ice_rule_query_data rule;
		int status;

		/* for now process only channel specific filters */
		if (!ice_is_chnl_fltr(fltr))
			continue;

		rule.rid = fltr->rid;
		rule.rule_id = fltr->rule_id;
		rule.vsi_handle = fltr->dest_vsi_handle;
		status = ice_rem_adv_rule_by_id(&pf->hw, &rule);
		if (status) {
			if (status == -ENOENT)
				dev_dbg(ice_pf_to_dev(pf), "TC flower filter (rule_id %u) does not exist\n",
					rule.rule_id);
			else
				dev_err(ice_pf_to_dev(pf), "failed to delete TC flower filter, status %d\n",
					status);
		} else if (fltr->dest_vsi) {
			/* update advanced switch filter count */
			if (fltr->dest_vsi->type == ICE_VSI_CHNL) {
				u32 flags = fltr->flags;

				fltr->dest_vsi->num_chnl_fltr--;
				if (flags & (ICE_TC_FLWR_FIELD_DST_MAC |
					     ICE_TC_FLWR_FIELD_ENC_DST_MAC))
					pf->num_dmac_chnl_fltrs--;
			}
		}

		hlist_del(&fltr->tc_flower_node);
		kfree(fltr);
	}
}

/**
 * ice_remove_q_channels - Remove queue channels for the TCs
 * @vsi: VSI to be configured
 * @rem_fltr: delete advanced switch filter or not
 *
 * Remove queue channels for the TCs
 */
static void ice_remove_q_channels(struct ice_vsi *vsi, bool rem_fltr)
{
	struct ice_channel *ch, *ch_tmp;
	struct ice_pf *pf = vsi->back;
	int i;

	/* remove all tc-flower based filter if they are channel filters only */
	if (rem_fltr)
		ice_rem_all_chnl_fltrs(pf);

	/* remove ntuple filters since queue configuration is being changed */
	if  (vsi->netdev->features & NETIF_F_NTUPLE) {
		struct ice_hw *hw = &pf->hw;

		mutex_lock(&hw->fdir_fltr_lock);
		ice_fdir_del_all_fltrs(vsi);
		mutex_unlock(&hw->fdir_fltr_lock);
	}

	/* perform cleanup for channels if they exist */
	list_for_each_entry_safe(ch, ch_tmp, &vsi->ch_list, list) {
		struct ice_vsi *ch_vsi;

		list_del(&ch->list);
		ch_vsi = ch->ch_vsi;
		if (!ch_vsi) {
			kfree(ch);
			continue;
		}

		/* Reset queue contexts */
		for (i = 0; i < ch->num_rxq; i++) {
			struct ice_tx_ring *tx_ring;
			struct ice_rx_ring *rx_ring;

			tx_ring = vsi->tx_rings[ch->base_q + i];
			rx_ring = vsi->rx_rings[ch->base_q + i];
			if (tx_ring) {
				tx_ring->ch = NULL;
				if (tx_ring->q_vector)
					tx_ring->q_vector->ch = NULL;
			}
			if (rx_ring) {
				rx_ring->ch = NULL;
				if (rx_ring->q_vector)
					rx_ring->q_vector->ch = NULL;
			}
		}

		/* Release FD resources for the channel VSI */
		ice_fdir_rem_adq_chnl(&pf->hw, ch->ch_vsi->idx);

		/* clear the VSI from scheduler tree */
		ice_rm_vsi_lan_cfg(ch->ch_vsi->port_info, ch->ch_vsi->idx);

		/* Delete VSI from FW, PF and HW VSI arrays */
		ice_vsi_delete(ch->ch_vsi);

		/* free the channel */
		kfree(ch);
	}

	/* clear the channel VSI map which is stored in main VSI */
	ice_for_each_chnl_tc(i)
		vsi->tc_map_vsi[i] = NULL;

	/* reset main VSI's all TC information */
	vsi->all_enatc = 0;
	vsi->all_numtc = 0;
}

/**
 * ice_rebuild_channels - rebuild channel
 * @pf: ptr to PF
 *
 * Recreate channel VSIs and replay filters
 */
static int ice_rebuild_channels(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_vsi *main_vsi;
	bool rem_adv_fltr = true;
	struct ice_channel *ch;
	struct ice_vsi *vsi;
	int tc_idx = 1;
	int i, err;

	main_vsi = ice_get_main_vsi(pf);
	if (!main_vsi)
		return 0;

	if (!test_bit(ICE_FLAG_TC_MQPRIO, pf->flags) ||
	    main_vsi->old_numtc == 1)
		return 0; /* nothing to be done */

	/* reconfigure main VSI based on old value of TC and cached values
	 * for MQPRIO opts
	 */
	err = ice_vsi_cfg_tc(main_vsi, main_vsi->old_ena_tc);
	if (err) {
		dev_err(dev, "failed configuring TC(ena_tc:0x%02x) for HW VSI=%u\n",
			main_vsi->old_ena_tc, main_vsi->vsi_num);
		return err;
	}

	/* rebuild ADQ VSIs */
	ice_for_each_vsi(pf, i) {
		enum ice_vsi_type type;

		vsi = pf->vsi[i];
		if (!vsi || vsi->type != ICE_VSI_CHNL)
			continue;

		type = vsi->type;

		/* rebuild ADQ VSI */
		err = ice_vsi_rebuild(vsi, ICE_VSI_FLAG_INIT);
		if (err) {
			dev_err(dev, "VSI (type:%s) at index %d rebuild failed, err %d\n",
				ice_vsi_type_str(type), vsi->idx, err);
			goto cleanup;
		}

		/* Re-map HW VSI number, using VSI handle that has been
		 * previously validated in ice_replay_vsi() call above
		 */
		vsi->vsi_num = ice_get_hw_vsi_num(&pf->hw, vsi->idx);

		/* replay filters for the VSI */
		err = ice_replay_vsi(&pf->hw, vsi->idx);
		if (err) {
			dev_err(dev, "VSI (type:%s) replay failed, err %d, VSI index %d\n",
				ice_vsi_type_str(type), err, vsi->idx);
			rem_adv_fltr = false;
			goto cleanup;
		}
		dev_info(dev, "VSI (type:%s) at index %d rebuilt successfully\n",
			 ice_vsi_type_str(type), vsi->idx);

		/* store ADQ VSI at correct TC index in main VSI's
		 * map of TC to VSI
		 */
		main_vsi->tc_map_vsi[tc_idx++] = vsi;
	}

	/* ADQ VSI(s) has been rebuilt successfully, so setup
	 * channel for main VSI's Tx and Rx rings
	 */
	list_for_each_entry(ch, &main_vsi->ch_list, list) {
		struct ice_vsi *ch_vsi;

		ch_vsi = ch->ch_vsi;
		if (!ch_vsi)
			continue;

		/* reconfig channel resources */
		ice_cfg_chnl_all_res(main_vsi, ch);

		/* replay BW rate limit if it is non-zero */
		if (!ch->max_tx_rate && !ch->min_tx_rate)
			continue;

		err = ice_set_bw_limit(ch_vsi, ch->max_tx_rate,
				       ch->min_tx_rate);
		if (err)
			dev_err(dev, "failed (err:%d) to rebuild BW rate limit, max_tx_rate: %llu Kbps, min_tx_rate: %llu Kbps for VSI(%u)\n",
				err, ch->max_tx_rate, ch->min_tx_rate,
				ch_vsi->vsi_num);
		else
			dev_dbg(dev, "successfully rebuild BW rate limit, max_tx_rate: %llu Kbps, min_tx_rate: %llu Kbps for VSI(%u)\n",
				ch->max_tx_rate, ch->min_tx_rate,
				ch_vsi->vsi_num);
	}

	/* reconfig RSS for main VSI */
	if (main_vsi->ch_rss_size)
		ice_vsi_cfg_rss_lut_key(main_vsi);

	return 0;

cleanup:
	ice_remove_q_channels(main_vsi, rem_adv_fltr);
	return err;
}

/**
 * ice_create_q_channels - Add queue channel for the given TCs
 * @vsi: VSI to be configured
 *
 * Configures queue channel mapping to the given TCs
 */
static int ice_create_q_channels(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct ice_channel *ch;
	int ret = 0, i;

	ice_for_each_chnl_tc(i) {
		if (!(vsi->all_enatc & BIT(i)))
			continue;

		ch = kzalloc(sizeof(*ch), GFP_KERNEL);
		if (!ch) {
			ret = -ENOMEM;
			goto err_free;
		}
		INIT_LIST_HEAD(&ch->list);
		ch->num_rxq = vsi->mqprio_qopt.qopt.count[i];
		ch->num_txq = vsi->mqprio_qopt.qopt.count[i];
		ch->base_q = vsi->mqprio_qopt.qopt.offset[i];
		ch->max_tx_rate = vsi->mqprio_qopt.max_rate[i];
		ch->min_tx_rate = vsi->mqprio_qopt.min_rate[i];

		/* convert to Kbits/s */
		if (ch->max_tx_rate)
			ch->max_tx_rate = div_u64(ch->max_tx_rate,
						  ICE_BW_KBPS_DIVISOR);
		if (ch->min_tx_rate)
			ch->min_tx_rate = div_u64(ch->min_tx_rate,
						  ICE_BW_KBPS_DIVISOR);

		ret = ice_create_q_channel(vsi, ch);
		if (ret) {
			dev_err(ice_pf_to_dev(pf),
				"failed creating channel TC:%d\n", i);
			kfree(ch);
			goto err_free;
		}
		list_add_tail(&ch->list, &vsi->ch_list);
		vsi->tc_map_vsi[i] = ch->ch_vsi;
		dev_dbg(ice_pf_to_dev(pf),
			"successfully created channel: VSI %pK\n", ch->ch_vsi);
	}
	return 0;

err_free:
	ice_remove_q_channels(vsi, false);

	return ret;
}

/**
 * ice_setup_tc_mqprio_qdisc - configure multiple traffic classes
 * @netdev: net device to configure
 * @type_data: TC offload data
 */
static int ice_setup_tc_mqprio_qdisc(struct net_device *netdev, void *type_data)
{
	struct tc_mqprio_qopt_offload *mqprio_qopt = type_data;
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	u16 mode, ena_tc_qdisc = 0;
	int cur_txq, cur_rxq;
	u8 hw = 0, num_tcf;
	struct device *dev;
	int ret, i;

	dev = ice_pf_to_dev(pf);
	num_tcf = mqprio_qopt->qopt.num_tc;
	hw = mqprio_qopt->qopt.hw;
	mode = mqprio_qopt->mode;
	if (!hw) {
		clear_bit(ICE_FLAG_TC_MQPRIO, pf->flags);
		vsi->ch_rss_size = 0;
		memcpy(&vsi->mqprio_qopt, mqprio_qopt, sizeof(*mqprio_qopt));
		goto config_tcf;
	}

	/* Generate queue region map for number of TCF requested */
	for (i = 0; i < num_tcf; i++)
		ena_tc_qdisc |= BIT(i);

	switch (mode) {
	case TC_MQPRIO_MODE_CHANNEL:

		if (pf->hw.port_info->is_custom_tx_enabled) {
			dev_err(dev, "Custom Tx scheduler feature enabled, can't configure ADQ\n");
			return -EBUSY;
		}
		ice_tear_down_devlink_rate_tree(pf);

		ret = ice_validate_mqprio_qopt(vsi, mqprio_qopt);
		if (ret) {
			netdev_err(netdev, "failed to validate_mqprio_qopt(), ret %d\n",
				   ret);
			return ret;
		}
		memcpy(&vsi->mqprio_qopt, mqprio_qopt, sizeof(*mqprio_qopt));
		set_bit(ICE_FLAG_TC_MQPRIO, pf->flags);
		/* don't assume state of hw_tc_offload during driver load
		 * and set the flag for TC flower filter if hw_tc_offload
		 * already ON
		 */
		if (vsi->netdev->features & NETIF_F_HW_TC)
			set_bit(ICE_FLAG_CLS_FLOWER, pf->flags);
		break;
	default:
		return -EINVAL;
	}

config_tcf:

	/* Requesting same TCF configuration as already enabled */
	if (ena_tc_qdisc == vsi->tc_cfg.ena_tc &&
	    mode != TC_MQPRIO_MODE_CHANNEL)
		return 0;

	/* Pause VSI queues */
	ice_dis_vsi(vsi, true);

	if (!hw && !test_bit(ICE_FLAG_TC_MQPRIO, pf->flags))
		ice_remove_q_channels(vsi, true);

	if (!hw && !test_bit(ICE_FLAG_TC_MQPRIO, pf->flags)) {
		vsi->req_txq = min_t(int, ice_get_avail_txq_count(pf),
				     num_online_cpus());
		vsi->req_rxq = min_t(int, ice_get_avail_rxq_count(pf),
				     num_online_cpus());
	} else {
		/* logic to rebuild VSI, same like ethtool -L */
		u16 offset = 0, qcount_tx = 0, qcount_rx = 0;

		for (i = 0; i < num_tcf; i++) {
			if (!(ena_tc_qdisc & BIT(i)))
				continue;

			offset = vsi->mqprio_qopt.qopt.offset[i];
			qcount_rx = vsi->mqprio_qopt.qopt.count[i];
			qcount_tx = vsi->mqprio_qopt.qopt.count[i];
		}
		vsi->req_txq = offset + qcount_tx;
		vsi->req_rxq = offset + qcount_rx;

		/* store away original rss_size info, so that it gets reused
		 * form ice_vsi_rebuild during tc-qdisc delete stage - to
		 * determine, what should be the rss_sizefor main VSI
		 */
		vsi->orig_rss_size = vsi->rss_size;
	}

	/* save current values of Tx and Rx queues before calling VSI rebuild
	 * for fallback option
	 */
	cur_txq = vsi->num_txq;
	cur_rxq = vsi->num_rxq;

	/* proceed with rebuild main VSI using correct number of queues */
	ret = ice_vsi_rebuild(vsi, ICE_VSI_FLAG_NO_INIT);
	if (ret) {
		/* fallback to current number of queues */
		dev_info(dev, "Rebuild failed with new queues, try with current number of queues\n");
		vsi->req_txq = cur_txq;
		vsi->req_rxq = cur_rxq;
		clear_bit(ICE_RESET_FAILED, pf->state);
		if (ice_vsi_rebuild(vsi, ICE_VSI_FLAG_NO_INIT)) {
			dev_err(dev, "Rebuild of main VSI failed again\n");
			return ret;
		}
	}

	vsi->all_numtc = num_tcf;
	vsi->all_enatc = ena_tc_qdisc;
	ret = ice_vsi_cfg_tc(vsi, ena_tc_qdisc);
	if (ret) {
		netdev_err(netdev, "failed configuring TC for VSI id=%d\n",
			   vsi->vsi_num);
		goto exit;
	}

	if (test_bit(ICE_FLAG_TC_MQPRIO, pf->flags)) {
		u64 max_tx_rate = vsi->mqprio_qopt.max_rate[0];
		u64 min_tx_rate = vsi->mqprio_qopt.min_rate[0];

		/* set TC0 rate limit if specified */
		if (max_tx_rate || min_tx_rate) {
			/* convert to Kbits/s */
			if (max_tx_rate)
				max_tx_rate = div_u64(max_tx_rate, ICE_BW_KBPS_DIVISOR);
			if (min_tx_rate)
				min_tx_rate = div_u64(min_tx_rate, ICE_BW_KBPS_DIVISOR);

			ret = ice_set_bw_limit(vsi, max_tx_rate, min_tx_rate);
			if (!ret) {
				dev_dbg(dev, "set Tx rate max %llu min %llu for VSI(%u)\n",
					max_tx_rate, min_tx_rate, vsi->vsi_num);
			} else {
				dev_err(dev, "failed to set Tx rate max %llu min %llu for VSI(%u)\n",
					max_tx_rate, min_tx_rate, vsi->vsi_num);
				goto exit;
			}
		}
		ret = ice_create_q_channels(vsi);
		if (ret) {
			netdev_err(netdev, "failed configuring queue channels\n");
			goto exit;
		} else {
			netdev_dbg(netdev, "successfully configured channels\n");
		}
	}

	if (vsi->ch_rss_size)
		ice_vsi_cfg_rss_lut_key(vsi);

exit:
	/* if error, reset the all_numtc and all_enatc */
	if (ret) {
		vsi->all_numtc = 0;
		vsi->all_enatc = 0;
	}
	/* resume VSI */
	ice_ena_vsi(vsi, true);

	return ret;
}

static LIST_HEAD(ice_block_cb_list);

static int
ice_setup_tc(struct net_device *netdev, enum tc_setup_type type,
	     void *type_data)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_pf *pf = np->vsi->back;
	bool locked = false;
	int err;

	switch (type) {
	case TC_SETUP_BLOCK:
		return flow_block_cb_setup_simple(type_data,
						  &ice_block_cb_list,
						  ice_setup_tc_block_cb,
						  np, np, true);
	case TC_SETUP_QDISC_MQPRIO:
		if (ice_is_eswitch_mode_switchdev(pf)) {
			netdev_err(netdev, "TC MQPRIO offload not supported, switchdev is enabled\n");
			return -EOPNOTSUPP;
		}

		if (pf->adev) {
			mutex_lock(&pf->adev_mutex);
			device_lock(&pf->adev->dev);
			locked = true;
			if (pf->adev->dev.driver) {
				netdev_err(netdev, "Cannot change qdisc when RDMA is active\n");
				err = -EBUSY;
				goto adev_unlock;
			}
		}

		/* setup traffic classifier for receive side */
		mutex_lock(&pf->tc_mutex);
		err = ice_setup_tc_mqprio_qdisc(netdev, type_data);
		mutex_unlock(&pf->tc_mutex);

adev_unlock:
		if (locked) {
			device_unlock(&pf->adev->dev);
			mutex_unlock(&pf->adev_mutex);
		}
		return err;
	default:
		return -EOPNOTSUPP;
	}
	return -EOPNOTSUPP;
}

static struct ice_indr_block_priv *
ice_indr_block_priv_lookup(struct ice_netdev_priv *np,
			   struct net_device *netdev)
{
	struct ice_indr_block_priv *cb_priv;

	list_for_each_entry(cb_priv, &np->tc_indr_block_priv_list, list) {
		if (!cb_priv->netdev)
			return NULL;
		if (cb_priv->netdev == netdev)
			return cb_priv;
	}
	return NULL;
}

static int
ice_indr_setup_block_cb(enum tc_setup_type type, void *type_data,
			void *indr_priv)
{
	struct ice_indr_block_priv *priv = indr_priv;
	struct ice_netdev_priv *np = priv->np;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return ice_setup_tc_cls_flower(np, priv->netdev,
					       (struct flow_cls_offload *)
					       type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static int
ice_indr_setup_tc_block(struct net_device *netdev, struct Qdisc *sch,
			struct ice_netdev_priv *np,
			struct flow_block_offload *f, void *data,
			void (*cleanup)(struct flow_block_cb *block_cb))
{
	struct ice_indr_block_priv *indr_priv;
	struct flow_block_cb *block_cb;

	if (!ice_is_tunnel_supported(netdev) &&
	    !(is_vlan_dev(netdev) &&
	      vlan_dev_real_dev(netdev) == np->vsi->netdev))
		return -EOPNOTSUPP;

	if (f->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		indr_priv = ice_indr_block_priv_lookup(np, netdev);
		if (indr_priv)
			return -EEXIST;

		indr_priv = kzalloc(sizeof(*indr_priv), GFP_KERNEL);
		if (!indr_priv)
			return -ENOMEM;

		indr_priv->netdev = netdev;
		indr_priv->np = np;
		list_add(&indr_priv->list, &np->tc_indr_block_priv_list);

		block_cb =
			flow_indr_block_cb_alloc(ice_indr_setup_block_cb,
						 indr_priv, indr_priv,
						 ice_rep_indr_tc_block_unbind,
						 f, netdev, sch, data, np,
						 cleanup);

		if (IS_ERR(block_cb)) {
			list_del(&indr_priv->list);
			kfree(indr_priv);
			return PTR_ERR(block_cb);
		}
		flow_block_cb_add(block_cb, f);
		list_add_tail(&block_cb->driver_list, &ice_block_cb_list);
		break;
	case FLOW_BLOCK_UNBIND:
		indr_priv = ice_indr_block_priv_lookup(np, netdev);
		if (!indr_priv)
			return -ENOENT;

		block_cb = flow_block_cb_lookup(f->block,
						ice_indr_setup_block_cb,
						indr_priv);
		if (!block_cb)
			return -ENOENT;

		flow_indr_block_cb_remove(block_cb, f);

		list_del(&block_cb->driver_list);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int
ice_indr_setup_tc_cb(struct net_device *netdev, struct Qdisc *sch,
		     void *cb_priv, enum tc_setup_type type, void *type_data,
		     void *data,
		     void (*cleanup)(struct flow_block_cb *block_cb))
{
	switch (type) {
	case TC_SETUP_BLOCK:
		return ice_indr_setup_tc_block(netdev, sch, cb_priv, type_data,
					       data, cleanup);

	default:
		return -EOPNOTSUPP;
	}
}

/**
 * ice_open - Called when a network interface becomes active
 * @netdev: network interface device structure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP). At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the netdev watchdog is enabled,
 * and the stack is notified that the interface is ready.
 *
 * Returns 0 on success, negative value on failure
 */
int ice_open(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_pf *pf = np->vsi->back;

	if (ice_is_reset_in_progress(pf->state)) {
		netdev_err(netdev, "can't open net device while reset is in progress");
		return -EBUSY;
	}

	return ice_open_internal(netdev);
}

/**
 * ice_open_internal - Called when a network interface becomes active
 * @netdev: network interface device structure
 *
 * Internal ice_open implementation. Should not be used directly except for ice_open and reset
 * handling routine
 *
 * Returns 0 on success, negative value on failure
 */
int ice_open_internal(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	struct ice_port_info *pi;
	int err;

	if (test_bit(ICE_NEEDS_RESTART, pf->state)) {
		netdev_err(netdev, "driver needs to be unloaded and reloaded\n");
		return -EIO;
	}

	netif_carrier_off(netdev);

	pi = vsi->port_info;
	err = ice_update_link_info(pi);
	if (err) {
		netdev_err(netdev, "Failed to get link info, error %d\n", err);
		return err;
	}

	ice_check_link_cfg_err(pf, pi->phy.link_info.link_cfg_err);

	/* Set PHY if there is media, otherwise, turn off PHY */
	if (pi->phy.link_info.link_info & ICE_AQ_MEDIA_AVAILABLE) {
		clear_bit(ICE_FLAG_NO_MEDIA, pf->flags);
		if (!test_bit(ICE_PHY_INIT_COMPLETE, pf->state)) {
			err = ice_init_phy_user_cfg(pi);
			if (err) {
				netdev_err(netdev, "Failed to initialize PHY settings, error %d\n",
					   err);
				return err;
			}
		}

		err = ice_configure_phy(vsi);
		if (err) {
			netdev_err(netdev, "Failed to set physical link up, error %d\n",
				   err);
			return err;
		}
	} else {
		set_bit(ICE_FLAG_NO_MEDIA, pf->flags);
		ice_set_link(vsi, false);
	}

	err = ice_vsi_open(vsi);
	if (err)
		netdev_err(netdev, "Failed to open VSI 0x%04X on switch 0x%04X\n",
			   vsi->vsi_num, vsi->vsw->sw_id);

	/* Update existing tunnels information */
	udp_tunnel_get_rx_info(netdev);

	return err;
}

/**
 * ice_stop - Disables a network interface
 * @netdev: network interface device structure
 *
 * The stop entry point is called when an interface is de-activated by the OS,
 * and the netdevice enters the DOWN state. The hardware is still under the
 * driver's control, but the netdev interface is disabled.
 *
 * Returns success only - not allowed to fail
 */
int ice_stop(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;

	if (ice_is_reset_in_progress(pf->state)) {
		netdev_err(netdev, "can't stop net device while reset is in progress");
		return -EBUSY;
	}

	if (test_bit(ICE_FLAG_LINK_DOWN_ON_CLOSE_ENA, vsi->back->flags)) {
		int link_err = ice_force_phys_link_state(vsi, false);

		if (link_err) {
			if (link_err == -ENOMEDIUM)
				netdev_info(vsi->netdev, "Skipping link reconfig - no media attached, VSI %d\n",
					    vsi->vsi_num);
			else
				netdev_err(vsi->netdev, "Failed to set physical link down, VSI %d error %d\n",
					   vsi->vsi_num, link_err);

			ice_vsi_close(vsi);
			return -EIO;
		}
	}

	ice_vsi_close(vsi);

	return 0;
}

/**
 * ice_features_check - Validate encapsulated packet conforms to limits
 * @skb: skb buffer
 * @netdev: This port's netdev
 * @features: Offload features that the stack believes apply
 */
static netdev_features_t
ice_features_check(struct sk_buff *skb,
		   struct net_device __always_unused *netdev,
		   netdev_features_t features)
{
	bool gso = skb_is_gso(skb);
	size_t len;

	/* No point in doing any of this if neither checksum nor GSO are
	 * being requested for this frame. We can rule out both by just
	 * checking for CHECKSUM_PARTIAL
	 */
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return features;

	/* We cannot support GSO if the MSS is going to be less than
	 * 64 bytes. If it is then we need to drop support for GSO.
	 */
	if (gso && (skb_shinfo(skb)->gso_size < ICE_TXD_CTX_MIN_MSS))
		features &= ~NETIF_F_GSO_MASK;

	len = skb_network_offset(skb);
	if (len > ICE_TXD_MACLEN_MAX || len & 0x1)
		goto out_rm_features;

	len = skb_network_header_len(skb);
	if (len > ICE_TXD_IPLEN_MAX || len & 0x1)
		goto out_rm_features;

	if (skb->encapsulation) {
		/* this must work for VXLAN frames AND IPIP/SIT frames, and in
		 * the case of IPIP frames, the transport header pointer is
		 * after the inner header! So check to make sure that this
		 * is a GRE or UDP_TUNNEL frame before doing that math.
		 */
		if (gso && (skb_shinfo(skb)->gso_type &
			    (SKB_GSO_GRE | SKB_GSO_UDP_TUNNEL))) {
			len = skb_inner_network_header(skb) -
			      skb_transport_header(skb);
			if (len > ICE_TXD_L4LEN_MAX || len & 0x1)
				goto out_rm_features;
		}

		len = skb_inner_network_header_len(skb);
		if (len > ICE_TXD_IPLEN_MAX || len & 0x1)
			goto out_rm_features;
	}

	return features;
out_rm_features:
	return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
}

static const struct net_device_ops ice_netdev_safe_mode_ops = {
	.ndo_open = ice_open,
	.ndo_stop = ice_stop,
	.ndo_start_xmit = ice_start_xmit,
	.ndo_set_mac_address = ice_set_mac_address,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_change_mtu = ice_change_mtu,
	.ndo_get_stats64 = ice_get_stats64,
	.ndo_tx_timeout = ice_tx_timeout,
	.ndo_bpf = ice_xdp_safe_mode,
};

static const struct net_device_ops ice_netdev_ops = {
	.ndo_open = ice_open,
	.ndo_stop = ice_stop,
	.ndo_start_xmit = ice_start_xmit,
	.ndo_select_queue = ice_select_queue,
	.ndo_features_check = ice_features_check,
	.ndo_fix_features = ice_fix_features,
	.ndo_set_rx_mode = ice_set_rx_mode,
	.ndo_set_mac_address = ice_set_mac_address,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_change_mtu = ice_change_mtu,
	.ndo_get_stats64 = ice_get_stats64,
	.ndo_set_tx_maxrate = ice_set_tx_maxrate,
	.ndo_eth_ioctl = ice_eth_ioctl,
	.ndo_set_vf_spoofchk = ice_set_vf_spoofchk,
	.ndo_set_vf_mac = ice_set_vf_mac,
	.ndo_get_vf_config = ice_get_vf_cfg,
	.ndo_set_vf_trust = ice_set_vf_trust,
	.ndo_set_vf_vlan = ice_set_vf_port_vlan,
	.ndo_set_vf_link_state = ice_set_vf_link_state,
	.ndo_get_vf_stats = ice_get_vf_stats,
	.ndo_set_vf_rate = ice_set_vf_bw,
	.ndo_vlan_rx_add_vid = ice_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = ice_vlan_rx_kill_vid,
	.ndo_setup_tc = ice_setup_tc,
	.ndo_set_features = ice_set_features,
	.ndo_bridge_getlink = ice_bridge_getlink,
	.ndo_bridge_setlink = ice_bridge_setlink,
	.ndo_fdb_add = ice_fdb_add,
	.ndo_fdb_del = ice_fdb_del,
#ifdef CONFIG_RFS_ACCEL
	.ndo_rx_flow_steer = ice_rx_flow_steer,
#endif
	.ndo_tx_timeout = ice_tx_timeout,
	.ndo_bpf = ice_xdp,
	.ndo_xdp_xmit = ice_xdp_xmit,
	.ndo_xsk_wakeup = ice_xsk_wakeup,
};
