// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

/* Intel(R) Ethernet Connection E800 Series Linux Driver */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "ice.h"
#include "ice_lib.h"

#define DRV_VERSION	"0.7.2-k"
#define DRV_SUMMARY	"Intel(R) Ethernet Connection E800 Series Linux Driver"
const char ice_drv_ver[] = DRV_VERSION;
static const char ice_driver_string[] = DRV_SUMMARY;
static const char ice_copyright[] = "Copyright (c) 2018, Intel Corporation.";

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION(DRV_SUMMARY);
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);

static int debug = -1;
module_param(debug, int, 0644);
#ifndef CONFIG_DYNAMIC_DEBUG
MODULE_PARM_DESC(debug, "netif level (0=none,...,16=all), hw debug_mask (0x8XXXXXXX)");
#else
MODULE_PARM_DESC(debug, "netif level (0=none,...,16=all)");
#endif /* !CONFIG_DYNAMIC_DEBUG */

static struct workqueue_struct *ice_wq;
static const struct net_device_ops ice_netdev_ops;

static void ice_pf_dis_all_vsi(struct ice_pf *pf);
static void ice_rebuild(struct ice_pf *pf);

static void ice_vsi_release_all(struct ice_pf *pf);
static void ice_update_vsi_stats(struct ice_vsi *vsi);
static void ice_update_pf_stats(struct ice_pf *pf);

/**
 * ice_get_tx_pending - returns number of Tx descriptors not processed
 * @ring: the ring of descriptors
 */
static u32 ice_get_tx_pending(struct ice_ring *ring)
{
	u32 head, tail;

	head = ring->next_to_clean;
	tail = readl(ring->tail);

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
	unsigned int i;
	u32 v, v_idx;
	int packets;

	ice_for_each_vsi(pf, v)
		if (pf->vsi[v] && pf->vsi[v]->type == ICE_VSI_PF) {
			vsi = pf->vsi[v];
			break;
		}

	if (!vsi || test_bit(__ICE_DOWN, vsi->state))
		return;

	if (!(vsi->netdev && netif_carrier_ok(vsi->netdev)))
		return;

	for (i = 0; i < vsi->num_txq; i++) {
		struct ice_ring *tx_ring = vsi->tx_rings[i];

		if (tx_ring && tx_ring->desc) {
			int itr = ICE_ITR_NONE;

			/* If packet counter has not changed the queue is
			 * likely stalled, so force an interrupt for this
			 * queue.
			 *
			 * prev_pkt would be negative if there was no
			 * pending work.
			 */
			packets = tx_ring->stats.pkts & INT_MAX;
			if (tx_ring->tx_stats.prev_pkt == packets) {
				/* Trigger sw interrupt to revive the queue */
				v_idx = tx_ring->q_vector->v_idx;
				wr32(&vsi->back->hw,
				     GLINT_DYN_CTL(vsi->hw_base_vector + v_idx),
				     (itr << GLINT_DYN_CTL_ITR_INDX_S) |
				     GLINT_DYN_CTL_SWINT_TRIG_M |
				     GLINT_DYN_CTL_INTENA_MSK_M);
				continue;
			}

			/* Memory barrier between read of packet count and call
			 * to ice_get_tx_pending()
			 */
			smp_rmb();
			tx_ring->tx_stats.prev_pkt =
			    ice_get_tx_pending(tx_ring) ? packets : -1;
		}
	}
}

/**
 * ice_add_mac_to_sync_list - creates list of mac addresses to be synced
 * @netdev: the net device on which the sync is happening
 * @addr: mac address to sync
 *
 * This is a callback function which is called by the in kernel device sync
 * functions (like __dev_uc_sync, __dev_mc_sync, etc). This function only
 * populates the tmp_sync_list, which is later used by ice_add_mac to add the
 * mac filters from the hardware.
 */
static int ice_add_mac_to_sync_list(struct net_device *netdev, const u8 *addr)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;

	if (ice_add_mac_to_list(vsi, &vsi->tmp_sync_list, addr))
		return -EINVAL;

	return 0;
}

/**
 * ice_add_mac_to_unsync_list - creates list of mac addresses to be unsynced
 * @netdev: the net device on which the unsync is happening
 * @addr: mac address to unsync
 *
 * This is a callback function which is called by the in kernel device unsync
 * functions (like __dev_uc_unsync, __dev_mc_unsync, etc). This function only
 * populates the tmp_unsync_list, which is later used by ice_remove_mac to
 * delete the mac filters from the hardware.
 */
static int ice_add_mac_to_unsync_list(struct net_device *netdev, const u8 *addr)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;

	if (ice_add_mac_to_list(vsi, &vsi->tmp_unsync_list, addr))
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
	return test_bit(ICE_VSI_FLAG_UMAC_FLTR_CHANGED, vsi->flags) ||
	       test_bit(ICE_VSI_FLAG_MMAC_FLTR_CHANGED, vsi->flags) ||
	       test_bit(ICE_VSI_FLAG_VLAN_FLTR_CHANGED, vsi->flags);
}

/**
 * ice_vsi_sync_fltr - Update the VSI filter list to the HW
 * @vsi: ptr to the VSI
 *
 * Push any outstanding VSI filter changes through the AdminQ.
 */
static int ice_vsi_sync_fltr(struct ice_vsi *vsi)
{
	struct device *dev = &vsi->back->pdev->dev;
	struct net_device *netdev = vsi->netdev;
	bool promisc_forced_on = false;
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	enum ice_status status = 0;
	u32 changed_flags = 0;
	int err = 0;

	if (!vsi->netdev)
		return -EINVAL;

	while (test_and_set_bit(__ICE_CFG_BUSY, vsi->state))
		usleep_range(1000, 2000);

	changed_flags = vsi->current_netdev_flags ^ vsi->netdev->flags;
	vsi->current_netdev_flags = vsi->netdev->flags;

	INIT_LIST_HEAD(&vsi->tmp_sync_list);
	INIT_LIST_HEAD(&vsi->tmp_unsync_list);

	if (ice_vsi_fltr_changed(vsi)) {
		clear_bit(ICE_VSI_FLAG_UMAC_FLTR_CHANGED, vsi->flags);
		clear_bit(ICE_VSI_FLAG_MMAC_FLTR_CHANGED, vsi->flags);
		clear_bit(ICE_VSI_FLAG_VLAN_FLTR_CHANGED, vsi->flags);

		/* grab the netdev's addr_list_lock */
		netif_addr_lock_bh(netdev);
		__dev_uc_sync(netdev, ice_add_mac_to_sync_list,
			      ice_add_mac_to_unsync_list);
		__dev_mc_sync(netdev, ice_add_mac_to_sync_list,
			      ice_add_mac_to_unsync_list);
		/* our temp lists are populated. release lock */
		netif_addr_unlock_bh(netdev);
	}

	/* Remove mac addresses in the unsync list */
	status = ice_remove_mac(hw, &vsi->tmp_unsync_list);
	ice_free_fltr_list(dev, &vsi->tmp_unsync_list);
	if (status) {
		netdev_err(netdev, "Failed to delete MAC filters\n");
		/* if we failed because of alloc failures, just bail */
		if (status == ICE_ERR_NO_MEMORY) {
			err = -ENOMEM;
			goto out;
		}
	}

	/* Add mac addresses in the sync list */
	status = ice_add_mac(hw, &vsi->tmp_sync_list);
	ice_free_fltr_list(dev, &vsi->tmp_sync_list);
	if (status) {
		netdev_err(netdev, "Failed to add MAC filters\n");
		/* If there is no more space for new umac filters, vsi
		 * should go into promiscuous mode. There should be some
		 * space reserved for promiscuous filters.
		 */
		if (hw->adminq.sq_last_status == ICE_AQ_RC_ENOSPC &&
		    !test_and_set_bit(__ICE_FLTR_OVERFLOW_PROMISC,
				      vsi->state)) {
			promisc_forced_on = true;
			netdev_warn(netdev,
				    "Reached MAC filter limit, forcing promisc mode on VSI %d\n",
				    vsi->vsi_num);
		} else {
			err = -EIO;
			goto out;
		}
	}
	/* check for changes in promiscuous modes */
	if (changed_flags & IFF_ALLMULTI)
		netdev_warn(netdev, "Unsupported configuration\n");

	if (((changed_flags & IFF_PROMISC) || promisc_forced_on) ||
	    test_bit(ICE_VSI_FLAG_PROMISC_CHANGED, vsi->flags)) {
		clear_bit(ICE_VSI_FLAG_PROMISC_CHANGED, vsi->flags);
		if (vsi->current_netdev_flags & IFF_PROMISC) {
			/* Apply TX filter rule to get traffic from VMs */
			status = ice_cfg_dflt_vsi(hw, vsi->idx, true,
						  ICE_FLTR_TX);
			if (status) {
				netdev_err(netdev, "Error setting default VSI %i tx rule\n",
					   vsi->vsi_num);
				vsi->current_netdev_flags &= ~IFF_PROMISC;
				err = -EIO;
				goto out_promisc;
			}
			/* Apply RX filter rule to get traffic from wire */
			status = ice_cfg_dflt_vsi(hw, vsi->idx, true,
						  ICE_FLTR_RX);
			if (status) {
				netdev_err(netdev, "Error setting default VSI %i rx rule\n",
					   vsi->vsi_num);
				vsi->current_netdev_flags &= ~IFF_PROMISC;
				err = -EIO;
				goto out_promisc;
			}
		} else {
			/* Clear TX filter rule to stop traffic from VMs */
			status = ice_cfg_dflt_vsi(hw, vsi->idx, false,
						  ICE_FLTR_TX);
			if (status) {
				netdev_err(netdev, "Error clearing default VSI %i tx rule\n",
					   vsi->vsi_num);
				vsi->current_netdev_flags |= IFF_PROMISC;
				err = -EIO;
				goto out_promisc;
			}
			/* Clear RX filter to remove traffic from wire */
			status = ice_cfg_dflt_vsi(hw, vsi->idx, false,
						  ICE_FLTR_RX);
			if (status) {
				netdev_err(netdev, "Error clearing default VSI %i rx rule\n",
					   vsi->vsi_num);
				vsi->current_netdev_flags |= IFF_PROMISC;
				err = -EIO;
				goto out_promisc;
			}
		}
	}
	goto exit;

out_promisc:
	set_bit(ICE_VSI_FLAG_PROMISC_CHANGED, vsi->flags);
	goto exit;
out:
	/* if something went wrong then set the changed flag so we try again */
	set_bit(ICE_VSI_FLAG_UMAC_FLTR_CHANGED, vsi->flags);
	set_bit(ICE_VSI_FLAG_MMAC_FLTR_CHANGED, vsi->flags);
exit:
	clear_bit(__ICE_CFG_BUSY, vsi->state);
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

	for (v = 0; v < pf->num_alloc_vsi; v++)
		if (pf->vsi[v] && ice_vsi_fltr_changed(pf->vsi[v]) &&
		    ice_vsi_sync_fltr(pf->vsi[v])) {
			/* come back and try again later */
			set_bit(ICE_FLAG_FLTR_SYNC, pf->flags);
			break;
		}
}

/**
 * ice_prepare_for_reset - prep for the core to reset
 * @pf: board private structure
 *
 * Inform or close all dependent features in prep for reset.
 */
static void
ice_prepare_for_reset(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;

	/* Notify VFs of impending reset */
	if (ice_check_sq_alive(hw, &hw->mailboxq))
		ice_vc_notify_reset(pf);

	/* disable the VSIs and their queues that are not already DOWN */
	ice_pf_dis_all_vsi(pf);

	if (hw->port_info)
		ice_sched_clear_port(hw->port_info);

	ice_shutdown_all_ctrlq(hw);

	set_bit(__ICE_PREPARED_FOR_RESET, pf->state);
}

/**
 * ice_do_reset - Initiate one of many types of resets
 * @pf: board private structure
 * @reset_type: reset type requested
 * before this function was called.
 */
static void ice_do_reset(struct ice_pf *pf, enum ice_reset_req reset_type)
{
	struct device *dev = &pf->pdev->dev;
	struct ice_hw *hw = &pf->hw;

	dev_dbg(dev, "reset_type 0x%x requested\n", reset_type);
	WARN_ON(in_interrupt());

	ice_prepare_for_reset(pf);

	/* trigger the reset */
	if (ice_reset(hw, reset_type)) {
		dev_err(dev, "reset %d failed\n", reset_type);
		set_bit(__ICE_RESET_FAILED, pf->state);
		clear_bit(__ICE_RESET_OICR_RECV, pf->state);
		clear_bit(__ICE_PREPARED_FOR_RESET, pf->state);
		clear_bit(__ICE_PFR_REQ, pf->state);
		clear_bit(__ICE_CORER_REQ, pf->state);
		clear_bit(__ICE_GLOBR_REQ, pf->state);
		return;
	}

	/* PFR is a bit of a special case because it doesn't result in an OICR
	 * interrupt. So for PFR, rebuild after the reset and clear the reset-
	 * associated state bits.
	 */
	if (reset_type == ICE_RESET_PFR) {
		pf->pfr_count++;
		ice_rebuild(pf);
		clear_bit(__ICE_PREPARED_FOR_RESET, pf->state);
		clear_bit(__ICE_PFR_REQ, pf->state);
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
	 * type and __ICE_RESET_OICR_RECV.  So, if the latter bit is set
	 * prepare for pending reset if not already (for PF software-initiated
	 * global resets the software should already be prepared for it as
	 * indicated by __ICE_PREPARED_FOR_RESET; for global resets initiated
	 * by firmware or software on other PFs, that bit is not set so prepare
	 * for the reset now), poll for reset done, rebuild and return.
	 */
	if (test_bit(__ICE_RESET_OICR_RECV, pf->state)) {
		clear_bit(__ICE_GLOBR_RECV, pf->state);
		clear_bit(__ICE_CORER_RECV, pf->state);
		if (!test_bit(__ICE_PREPARED_FOR_RESET, pf->state))
			ice_prepare_for_reset(pf);

		/* make sure we are ready to rebuild */
		if (ice_check_reset(&pf->hw)) {
			set_bit(__ICE_RESET_FAILED, pf->state);
		} else {
			/* done with reset. start rebuild */
			pf->hw.reset_ongoing = false;
			ice_rebuild(pf);
			/* clear bit to resume normal operations, but
			 * ICE_NEEDS_RESTART bit is set incase rebuild failed
			 */
			clear_bit(__ICE_RESET_OICR_RECV, pf->state);
			clear_bit(__ICE_PREPARED_FOR_RESET, pf->state);
			clear_bit(__ICE_PFR_REQ, pf->state);
			clear_bit(__ICE_CORER_REQ, pf->state);
			clear_bit(__ICE_GLOBR_REQ, pf->state);
		}

		return;
	}

	/* No pending resets to finish processing. Check for new resets */
	if (test_bit(__ICE_PFR_REQ, pf->state))
		reset_type = ICE_RESET_PFR;
	if (test_bit(__ICE_CORER_REQ, pf->state))
		reset_type = ICE_RESET_CORER;
	if (test_bit(__ICE_GLOBR_REQ, pf->state))
		reset_type = ICE_RESET_GLOBR;
	/* If no valid reset type requested just return */
	if (reset_type == ICE_RESET_INVAL)
		return;

	/* reset if not already down or busy */
	if (!test_bit(__ICE_DOWN, pf->state) &&
	    !test_bit(__ICE_CFG_BUSY, pf->state)) {
		ice_do_reset(pf, reset_type);
	}
}

/**
 * ice_print_link_msg - print link up or down message
 * @vsi: the VSI whose link status is being queried
 * @isup: boolean for if the link is now up or down
 */
void ice_print_link_msg(struct ice_vsi *vsi, bool isup)
{
	const char *speed;
	const char *fc;

	if (vsi->current_isup == isup)
		return;

	vsi->current_isup = isup;

	if (!isup) {
		netdev_info(vsi->netdev, "NIC Link is Down\n");
		return;
	}

	switch (vsi->port_info->phy.link_info.link_speed) {
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
		speed = "Unknown";
		break;
	}

	switch (vsi->port_info->fc.current_mode) {
	case ICE_FC_FULL:
		fc = "RX/TX";
		break;
	case ICE_FC_TX_PAUSE:
		fc = "TX";
		break;
	case ICE_FC_RX_PAUSE:
		fc = "RX";
		break;
	default:
		fc = "Unknown";
		break;
	}

	netdev_info(vsi->netdev, "NIC Link is up %sbps, Flow Control: %s\n",
		    speed, fc);
}

/**
 * ice_vsi_link_event - update the vsi's netdev
 * @vsi: the vsi on which the link event occurred
 * @link_up: whether or not the vsi needs to be set up or down
 */
static void ice_vsi_link_event(struct ice_vsi *vsi, bool link_up)
{
	if (!vsi || test_bit(__ICE_DOWN, vsi->state))
		return;

	if (vsi->type == ICE_VSI_PF) {
		if (!vsi->netdev) {
			dev_dbg(&vsi->back->pdev->dev,
				"vsi->netdev is not initialized!\n");
			return;
		}
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
 * ice_link_event - process the link event
 * @pf: pf that the link event is associated with
 * @pi: port_info for the port that the link event is associated with
 *
 * Returns -EIO if ice_get_link_status() fails
 * Returns 0 on success
 */
static int
ice_link_event(struct ice_pf *pf, struct ice_port_info *pi)
{
	u8 new_link_speed, old_link_speed;
	struct ice_phy_info *phy_info;
	bool new_link_same_as_old;
	bool new_link, old_link;
	u8 lport;
	u16 v;

	phy_info = &pi->phy;
	phy_info->link_info_old = phy_info->link_info;
	/* Force ice_get_link_status() to update link info */
	phy_info->get_link_info = true;

	old_link = (phy_info->link_info_old.link_info & ICE_AQ_LINK_UP);
	old_link_speed = phy_info->link_info_old.link_speed;

	lport = pi->lport;
	if (ice_get_link_status(pi, &new_link)) {
		dev_dbg(&pf->pdev->dev,
			"Could not get link status for port %d\n", lport);
		return -EIO;
	}

	new_link_speed = phy_info->link_info.link_speed;

	new_link_same_as_old = (new_link == old_link &&
				new_link_speed == old_link_speed);

	ice_for_each_vsi(pf, v) {
		struct ice_vsi *vsi = pf->vsi[v];

		if (!vsi || !vsi->port_info)
			continue;

		if (new_link_same_as_old &&
		    (test_bit(__ICE_DOWN, vsi->state) ||
		    new_link == netif_carrier_ok(vsi->netdev)))
			continue;

		if (vsi->port_info->lport == lport) {
			ice_print_link_msg(vsi, new_link);
			ice_vsi_link_event(vsi, new_link);
		}
	}

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
	if (test_bit(__ICE_DOWN, pf->state) ||
	    test_bit(__ICE_CFG_BUSY, pf->state))
		return;

	/* make sure we don't do these things too often */
	if (time_before(jiffies,
			pf->serv_tmr_prev + pf->serv_tmr_period))
		return;

	pf->serv_tmr_prev = jiffies;

	if (ice_link_event(pf, pf->hw.port_info))
		dev_dbg(&pf->pdev->dev, "ice_link_event failed\n");

	/* Update the stats for active netdevs so the network stack
	 * can look at updated numbers whenever it cares to
	 */
	ice_update_pf_stats(pf);
	for (i = 0; i < pf->num_alloc_vsi; i++)
		if (pf->vsi[i] && pf->vsi[i]->netdev)
			ice_update_vsi_stats(pf->vsi[i]);
}

/**
 * __ice_clean_ctrlq - helper function to clean controlq rings
 * @pf: ptr to struct ice_pf
 * @q_type: specific Control queue type
 */
static int __ice_clean_ctrlq(struct ice_pf *pf, enum ice_ctl_q q_type)
{
	struct ice_rq_event_info event;
	struct ice_hw *hw = &pf->hw;
	struct ice_ctl_q_info *cq;
	u16 pending, i = 0;
	const char *qtype;
	u32 oldval, val;

	/* Do not clean control queue if/when PF reset fails */
	if (test_bit(__ICE_RESET_FAILED, pf->state))
		return 0;

	switch (q_type) {
	case ICE_CTL_Q_ADMIN:
		cq = &hw->adminq;
		qtype = "Admin";
		break;
	case ICE_CTL_Q_MAILBOX:
		cq = &hw->mailboxq;
		qtype = "Mailbox";
		break;
	default:
		dev_warn(&pf->pdev->dev, "Unknown control queue type 0x%x\n",
			 q_type);
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
			dev_dbg(&pf->pdev->dev,
				"%s Receive Queue VF Error detected\n", qtype);
		if (val & PF_FW_ARQLEN_ARQOVFL_M) {
			dev_dbg(&pf->pdev->dev,
				"%s Receive Queue Overflow Error detected\n",
				qtype);
		}
		if (val & PF_FW_ARQLEN_ARQCRIT_M)
			dev_dbg(&pf->pdev->dev,
				"%s Receive Queue Critical Error detected\n",
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
			dev_dbg(&pf->pdev->dev,
				"%s Send Queue VF Error detected\n", qtype);
		if (val & PF_FW_ATQLEN_ATQOVFL_M) {
			dev_dbg(&pf->pdev->dev,
				"%s Send Queue Overflow Error detected\n",
				qtype);
		}
		if (val & PF_FW_ATQLEN_ATQCRIT_M)
			dev_dbg(&pf->pdev->dev,
				"%s Send Queue Critical Error detected\n",
				qtype);
		val &= ~(PF_FW_ATQLEN_ATQVFE_M | PF_FW_ATQLEN_ATQOVFL_M |
			 PF_FW_ATQLEN_ATQCRIT_M);
		if (oldval != val)
			wr32(hw, cq->sq.len, val);
	}

	event.buf_len = cq->rq_buf_size;
	event.msg_buf = devm_kzalloc(&pf->pdev->dev, event.buf_len,
				     GFP_KERNEL);
	if (!event.msg_buf)
		return 0;

	do {
		enum ice_status ret;
		u16 opcode;

		ret = ice_clean_rq_elem(hw, cq, &event, &pending);
		if (ret == ICE_ERR_AQ_NO_WORK)
			break;
		if (ret) {
			dev_err(&pf->pdev->dev,
				"%s Receive Queue event error %d\n", qtype,
				ret);
			break;
		}

		opcode = le16_to_cpu(event.desc.opcode);

		switch (opcode) {
		case ice_mbx_opc_send_msg_to_pf:
			ice_vc_process_vf_msg(pf, &event);
			break;
		case ice_aqc_opc_fw_logging:
			ice_output_fw_log(hw, &event.desc, event.msg_buf);
			break;
		default:
			dev_dbg(&pf->pdev->dev,
				"%s Receive Queue unknown event 0x%04x ignored\n",
				qtype, opcode);
			break;
		}
	} while (pending && (i++ < ICE_DFLT_IRQ_WORK));

	devm_kfree(&pf->pdev->dev, event.msg_buf);

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

	if (!test_bit(__ICE_ADMINQ_EVENT_PENDING, pf->state))
		return;

	if (__ice_clean_ctrlq(pf, ICE_CTL_Q_ADMIN))
		return;

	clear_bit(__ICE_ADMINQ_EVENT_PENDING, pf->state);

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

	if (!test_bit(__ICE_MAILBOXQ_EVENT_PENDING, pf->state))
		return;

	if (__ice_clean_ctrlq(pf, ICE_CTL_Q_MAILBOX))
		return;

	clear_bit(__ICE_MAILBOXQ_EVENT_PENDING, pf->state);

	if (ice_ctrlq_pending(hw, &hw->mailboxq))
		__ice_clean_ctrlq(pf, ICE_CTL_Q_MAILBOX);

	ice_flush(hw);
}

/**
 * ice_service_task_schedule - schedule the service task to wake up
 * @pf: board private structure
 *
 * If not already scheduled, this puts the task into the work queue.
 */
static void ice_service_task_schedule(struct ice_pf *pf)
{
	if (!test_bit(__ICE_SERVICE_DIS, pf->state) &&
	    !test_and_set_bit(__ICE_SERVICE_SCHED, pf->state) &&
	    !test_bit(__ICE_NEEDS_RESTART, pf->state))
		queue_work(ice_wq, &pf->serv_task);
}

/**
 * ice_service_task_complete - finish up the service task
 * @pf: board private structure
 */
static void ice_service_task_complete(struct ice_pf *pf)
{
	WARN_ON(!test_bit(__ICE_SERVICE_SCHED, pf->state));

	/* force memory (pf->state) to sync before next service task */
	smp_mb__before_atomic();
	clear_bit(__ICE_SERVICE_SCHED, pf->state);
}

/**
 * ice_service_task_stop - stop service task and cancel works
 * @pf: board private structure
 */
static void ice_service_task_stop(struct ice_pf *pf)
{
	set_bit(__ICE_SERVICE_DIS, pf->state);

	if (pf->serv_tmr.function)
		del_timer_sync(&pf->serv_tmr);
	if (pf->serv_task.func)
		cancel_work_sync(&pf->serv_task);

	clear_bit(__ICE_SERVICE_SCHED, pf->state);
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
 * ice_handle_mdd_event - handle malicious driver detect event
 * @pf: pointer to the PF structure
 *
 * Called from service task. OICR interrupt handler indicates MDD event
 */
static void ice_handle_mdd_event(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;
	bool mdd_detected = false;
	u32 reg;
	int i;

	if (!test_bit(__ICE_MDD_EVENT_PENDING, pf->state))
		return;

	/* find what triggered the MDD event */
	reg = rd32(hw, GL_MDET_TX_PQM);
	if (reg & GL_MDET_TX_PQM_VALID_M) {
		u8 pf_num = (reg & GL_MDET_TX_PQM_PF_NUM_M) >>
				GL_MDET_TX_PQM_PF_NUM_S;
		u16 vf_num = (reg & GL_MDET_TX_PQM_VF_NUM_M) >>
				GL_MDET_TX_PQM_VF_NUM_S;
		u8 event = (reg & GL_MDET_TX_PQM_MAL_TYPE_M) >>
				GL_MDET_TX_PQM_MAL_TYPE_S;
		u16 queue = ((reg & GL_MDET_TX_PQM_QNUM_M) >>
				GL_MDET_TX_PQM_QNUM_S);

		if (netif_msg_tx_err(pf))
			dev_info(&pf->pdev->dev, "Malicious Driver Detection event %d on TX queue %d PF# %d VF# %d\n",
				 event, queue, pf_num, vf_num);
		wr32(hw, GL_MDET_TX_PQM, 0xffffffff);
		mdd_detected = true;
	}

	reg = rd32(hw, GL_MDET_TX_TCLAN);
	if (reg & GL_MDET_TX_TCLAN_VALID_M) {
		u8 pf_num = (reg & GL_MDET_TX_TCLAN_PF_NUM_M) >>
				GL_MDET_TX_TCLAN_PF_NUM_S;
		u16 vf_num = (reg & GL_MDET_TX_TCLAN_VF_NUM_M) >>
				GL_MDET_TX_TCLAN_VF_NUM_S;
		u8 event = (reg & GL_MDET_TX_TCLAN_MAL_TYPE_M) >>
				GL_MDET_TX_TCLAN_MAL_TYPE_S;
		u16 queue = ((reg & GL_MDET_TX_TCLAN_QNUM_M) >>
				GL_MDET_TX_TCLAN_QNUM_S);

		if (netif_msg_rx_err(pf))
			dev_info(&pf->pdev->dev, "Malicious Driver Detection event %d on TX queue %d PF# %d VF# %d\n",
				 event, queue, pf_num, vf_num);
		wr32(hw, GL_MDET_TX_TCLAN, 0xffffffff);
		mdd_detected = true;
	}

	reg = rd32(hw, GL_MDET_RX);
	if (reg & GL_MDET_RX_VALID_M) {
		u8 pf_num = (reg & GL_MDET_RX_PF_NUM_M) >>
				GL_MDET_RX_PF_NUM_S;
		u16 vf_num = (reg & GL_MDET_RX_VF_NUM_M) >>
				GL_MDET_RX_VF_NUM_S;
		u8 event = (reg & GL_MDET_RX_MAL_TYPE_M) >>
				GL_MDET_RX_MAL_TYPE_S;
		u16 queue = ((reg & GL_MDET_RX_QNUM_M) >>
				GL_MDET_RX_QNUM_S);

		if (netif_msg_rx_err(pf))
			dev_info(&pf->pdev->dev, "Malicious Driver Detection event %d on RX queue %d PF# %d VF# %d\n",
				 event, queue, pf_num, vf_num);
		wr32(hw, GL_MDET_RX, 0xffffffff);
		mdd_detected = true;
	}

	if (mdd_detected) {
		bool pf_mdd_detected = false;

		reg = rd32(hw, PF_MDET_TX_PQM);
		if (reg & PF_MDET_TX_PQM_VALID_M) {
			wr32(hw, PF_MDET_TX_PQM, 0xFFFF);
			dev_info(&pf->pdev->dev, "TX driver issue detected, PF reset issued\n");
			pf_mdd_detected = true;
		}

		reg = rd32(hw, PF_MDET_TX_TCLAN);
		if (reg & PF_MDET_TX_TCLAN_VALID_M) {
			wr32(hw, PF_MDET_TX_TCLAN, 0xFFFF);
			dev_info(&pf->pdev->dev, "TX driver issue detected, PF reset issued\n");
			pf_mdd_detected = true;
		}

		reg = rd32(hw, PF_MDET_RX);
		if (reg & PF_MDET_RX_VALID_M) {
			wr32(hw, PF_MDET_RX, 0xFFFF);
			dev_info(&pf->pdev->dev, "RX driver issue detected, PF reset issued\n");
			pf_mdd_detected = true;
		}
		/* Queue belongs to the PF initiate a reset */
		if (pf_mdd_detected) {
			set_bit(__ICE_NEEDS_RESTART, pf->state);
			ice_service_task_schedule(pf);
		}
	}

	/* see if one of the VFs needs to be reset */
	for (i = 0; i < pf->num_alloc_vfs && mdd_detected; i++) {
		struct ice_vf *vf = &pf->vf[i];

		reg = rd32(hw, VP_MDET_TX_PQM(i));
		if (reg & VP_MDET_TX_PQM_VALID_M) {
			wr32(hw, VP_MDET_TX_PQM(i), 0xFFFF);
			vf->num_mdd_events++;
			dev_info(&pf->pdev->dev, "TX driver issue detected on VF %d\n",
				 i);
		}

		reg = rd32(hw, VP_MDET_TX_TCLAN(i));
		if (reg & VP_MDET_TX_TCLAN_VALID_M) {
			wr32(hw, VP_MDET_TX_TCLAN(i), 0xFFFF);
			vf->num_mdd_events++;
			dev_info(&pf->pdev->dev, "TX driver issue detected on VF %d\n",
				 i);
		}

		reg = rd32(hw, VP_MDET_TX_TDPU(i));
		if (reg & VP_MDET_TX_TDPU_VALID_M) {
			wr32(hw, VP_MDET_TX_TDPU(i), 0xFFFF);
			vf->num_mdd_events++;
			dev_info(&pf->pdev->dev, "TX driver issue detected on VF %d\n",
				 i);
		}

		reg = rd32(hw, VP_MDET_RX(i));
		if (reg & VP_MDET_RX_VALID_M) {
			wr32(hw, VP_MDET_RX(i), 0xFFFF);
			vf->num_mdd_events++;
			dev_info(&pf->pdev->dev, "RX driver issue detected on VF %d\n",
				 i);
		}

		if (vf->num_mdd_events > ICE_DFLT_NUM_MDD_EVENTS_ALLOWED) {
			dev_info(&pf->pdev->dev,
				 "Too many MDD events on VF %d, disabled\n", i);
			dev_info(&pf->pdev->dev,
				 "Use PF Control I/F to re-enable the VF\n");
			set_bit(ICE_VF_STATE_DIS, vf->vf_states);
		}
	}

	/* re-enable MDD interrupt cause */
	clear_bit(__ICE_MDD_EVENT_PENDING, pf->state);
	reg = rd32(hw, PFINT_OICR_ENA);
	reg |= PFINT_OICR_MAL_DETECT_M;
	wr32(hw, PFINT_OICR_ENA, reg);
	ice_flush(hw);
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
	    test_bit(__ICE_SUSPENDED, pf->state) ||
	    test_bit(__ICE_NEEDS_RESTART, pf->state)) {
		ice_service_task_complete(pf);
		return;
	}

	ice_check_for_hang_subtask(pf);
	ice_sync_fltr_subtask(pf);
	ice_handle_mdd_event(pf);
	ice_process_vflr_event(pf);
	ice_watchdog_subtask(pf);
	ice_clean_adminq_subtask(pf);
	ice_clean_mailboxq_subtask(pf);

	/* Clear __ICE_SERVICE_SCHED flag to allow scheduling next event */
	ice_service_task_complete(pf);

	/* If the tasks have taken longer than one service timer period
	 * or there is more work to be done, reset the service timer to
	 * schedule the service task now.
	 */
	if (time_after(jiffies, (start_time + pf->serv_tmr_period)) ||
	    test_bit(__ICE_MDD_EVENT_PENDING, pf->state) ||
	    test_bit(__ICE_VFLR_EVENT_PENDING, pf->state) ||
	    test_bit(__ICE_MAILBOXQ_EVENT_PENDING, pf->state) ||
	    test_bit(__ICE_ADMINQ_EVENT_PENDING, pf->state))
		mod_timer(&pf->serv_tmr, jiffies);
}

/**
 * ice_set_ctrlq_len - helper function to set controlq length
 * @hw: pointer to the hw instance
 */
static void ice_set_ctrlq_len(struct ice_hw *hw)
{
	hw->adminq.num_rq_entries = ICE_AQ_LEN;
	hw->adminq.num_sq_entries = ICE_AQ_LEN;
	hw->adminq.rq_buf_size = ICE_AQ_MAX_BUF_LEN;
	hw->adminq.sq_buf_size = ICE_AQ_MAX_BUF_LEN;
	hw->mailboxq.num_rq_entries = ICE_MBXQ_LEN;
	hw->mailboxq.num_sq_entries = ICE_MBXQ_LEN;
	hw->mailboxq.rq_buf_size = ICE_MBXQ_MAX_BUF_LEN;
	hw->mailboxq.sq_buf_size = ICE_MBXQ_MAX_BUF_LEN;
}

/**
 * ice_irq_affinity_notify - Callback for affinity changes
 * @notify: context as to what irq was changed
 * @mask: the new affinity mask
 *
 * This is a callback function used by the irq_set_affinity_notifier function
 * so that we may register to receive changes to the irq affinity masks.
 */
static void ice_irq_affinity_notify(struct irq_affinity_notify *notify,
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
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;

	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags)) {
		int i;

		for (i = 0; i < vsi->num_q_vectors; i++)
			ice_irq_dynamic_ena(hw, vsi, vsi->q_vectors[i]);
	}

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
	int base = vsi->sw_base_vector;
	int rx_int_idx = 0;
	int tx_int_idx = 0;
	int vector, err;
	int irq_num;

	for (vector = 0; vector < q_vectors; vector++) {
		struct ice_q_vector *q_vector = vsi->q_vectors[vector];

		irq_num = pf->msix_entries[base + vector].vector;

		if (q_vector->tx.ring && q_vector->rx.ring) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-%s-%d", basename, "TxRx", rx_int_idx++);
			tx_int_idx++;
		} else if (q_vector->rx.ring) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-%s-%d", basename, "rx", rx_int_idx++);
		} else if (q_vector->tx.ring) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-%s-%d", basename, "tx", tx_int_idx++);
		} else {
			/* skip this unused q_vector */
			continue;
		}
		err = devm_request_irq(&pf->pdev->dev,
				       pf->msix_entries[base + vector].vector,
				       vsi->irq_handler, 0, q_vector->name,
				       q_vector);
		if (err) {
			netdev_err(vsi->netdev,
				   "MSIX request_irq failed, error: %d\n", err);
			goto free_q_irqs;
		}

		/* register for affinity change notifications */
		q_vector->affinity_notify.notify = ice_irq_affinity_notify;
		q_vector->affinity_notify.release = ice_irq_affinity_release;
		irq_set_affinity_notifier(irq_num, &q_vector->affinity_notify);

		/* assign the mask for this irq */
		irq_set_affinity_hint(irq_num, &q_vector->affinity_mask);
	}

	vsi->irqs_ready = true;
	return 0;

free_q_irqs:
	while (vector) {
		vector--;
		irq_num = pf->msix_entries[base + vector].vector,
		irq_set_affinity_notifier(irq_num, NULL);
		irq_set_affinity_hint(irq_num, NULL);
		devm_free_irq(&pf->pdev->dev, irq_num, &vsi->q_vectors[vector]);
	}
	return err;
}

/**
 * ice_ena_misc_vector - enable the non-queue interrupts
 * @pf: board private structure
 */
static void ice_ena_misc_vector(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;
	u32 val;

	/* clear things first */
	wr32(hw, PFINT_OICR_ENA, 0);	/* disable all */
	rd32(hw, PFINT_OICR);		/* read to clear */

	val = (PFINT_OICR_ECC_ERR_M |
	       PFINT_OICR_MAL_DETECT_M |
	       PFINT_OICR_GRST_M |
	       PFINT_OICR_PCI_EXCEPTION_M |
	       PFINT_OICR_VFLR_M |
	       PFINT_OICR_HMC_ERR_M |
	       PFINT_OICR_PE_CRITERR_M);

	wr32(hw, PFINT_OICR_ENA, val);

	/* SW_ITR_IDX = 0, but don't change INTENA */
	wr32(hw, GLINT_DYN_CTL(pf->hw_oicr_idx),
	     GLINT_DYN_CTL_SW_ITR_INDX_M | GLINT_DYN_CTL_INTENA_MSK_M);
}

/**
 * ice_misc_intr - misc interrupt handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 */
static irqreturn_t ice_misc_intr(int __always_unused irq, void *data)
{
	struct ice_pf *pf = (struct ice_pf *)data;
	struct ice_hw *hw = &pf->hw;
	irqreturn_t ret = IRQ_NONE;
	u32 oicr, ena_mask;

	set_bit(__ICE_ADMINQ_EVENT_PENDING, pf->state);
	set_bit(__ICE_MAILBOXQ_EVENT_PENDING, pf->state);

	oicr = rd32(hw, PFINT_OICR);
	ena_mask = rd32(hw, PFINT_OICR_ENA);

	if (oicr & PFINT_OICR_MAL_DETECT_M) {
		ena_mask &= ~PFINT_OICR_MAL_DETECT_M;
		set_bit(__ICE_MDD_EVENT_PENDING, pf->state);
	}
	if (oicr & PFINT_OICR_VFLR_M) {
		ena_mask &= ~PFINT_OICR_VFLR_M;
		set_bit(__ICE_VFLR_EVENT_PENDING, pf->state);
	}

	if (oicr & PFINT_OICR_GRST_M) {
		u32 reset;

		/* we have a reset warning */
		ena_mask &= ~PFINT_OICR_GRST_M;
		reset = (rd32(hw, GLGEN_RSTAT) & GLGEN_RSTAT_RESET_TYPE_M) >>
			GLGEN_RSTAT_RESET_TYPE_S;

		if (reset == ICE_RESET_CORER)
			pf->corer_count++;
		else if (reset == ICE_RESET_GLOBR)
			pf->globr_count++;
		else if (reset == ICE_RESET_EMPR)
			pf->empr_count++;
		else
			dev_dbg(&pf->pdev->dev, "Invalid reset type %d\n",
				reset);

		/* If a reset cycle isn't already in progress, we set a bit in
		 * pf->state so that the service task can start a reset/rebuild.
		 * We also make note of which reset happened so that peer
		 * devices/drivers can be informed.
		 */
		if (!test_and_set_bit(__ICE_RESET_OICR_RECV, pf->state)) {
			if (reset == ICE_RESET_CORER)
				set_bit(__ICE_CORER_RECV, pf->state);
			else if (reset == ICE_RESET_GLOBR)
				set_bit(__ICE_GLOBR_RECV, pf->state);
			else
				set_bit(__ICE_EMPR_RECV, pf->state);

			/* There are couple of different bits at play here.
			 * hw->reset_ongoing indicates whether the hardware is
			 * in reset. This is set to true when a reset interrupt
			 * is received and set back to false after the driver
			 * has determined that the hardware is out of reset.
			 *
			 * __ICE_RESET_OICR_RECV in pf->state indicates
			 * that a post reset rebuild is required before the
			 * driver is operational again. This is set above.
			 *
			 * As this is the start of the reset/rebuild cycle, set
			 * both to indicate that.
			 */
			hw->reset_ongoing = true;
		}
	}

	if (oicr & PFINT_OICR_HMC_ERR_M) {
		ena_mask &= ~PFINT_OICR_HMC_ERR_M;
		dev_dbg(&pf->pdev->dev,
			"HMC Error interrupt - info 0x%x, data 0x%x\n",
			rd32(hw, PFHMC_ERRORINFO),
			rd32(hw, PFHMC_ERRORDATA));
	}

	/* Report and mask off any remaining unexpected interrupts */
	oicr &= ena_mask;
	if (oicr) {
		dev_dbg(&pf->pdev->dev, "unhandled interrupt oicr=0x%08x\n",
			oicr);
		/* If a critical error is pending there is no choice but to
		 * reset the device.
		 */
		if (oicr & (PFINT_OICR_PE_CRITERR_M |
			    PFINT_OICR_PCI_EXCEPTION_M |
			    PFINT_OICR_ECC_ERR_M)) {
			set_bit(__ICE_PFR_REQ, pf->state);
			ice_service_task_schedule(pf);
		}
		ena_mask &= ~oicr;
	}
	ret = IRQ_HANDLED;

	/* re-enable interrupt causes that are not handled during this pass */
	wr32(hw, PFINT_OICR_ENA, ena_mask);
	if (!test_bit(__ICE_DOWN, pf->state)) {
		ice_service_task_schedule(pf);
		ice_irq_dynamic_ena(hw, NULL, NULL);
	}

	return ret;
}

/**
 * ice_free_irq_msix_misc - Unroll misc vector setup
 * @pf: board private structure
 */
static void ice_free_irq_msix_misc(struct ice_pf *pf)
{
	/* disable OICR interrupt */
	wr32(&pf->hw, PFINT_OICR_ENA, 0);
	ice_flush(&pf->hw);

	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags) && pf->msix_entries) {
		synchronize_irq(pf->msix_entries[pf->sw_oicr_idx].vector);
		devm_free_irq(&pf->pdev->dev,
			      pf->msix_entries[pf->sw_oicr_idx].vector, pf);
	}

	pf->num_avail_sw_msix += 1;
	ice_free_res(pf->sw_irq_tracker, pf->sw_oicr_idx, ICE_RES_MISC_VEC_ID);
	pf->num_avail_hw_msix += 1;
	ice_free_res(pf->hw_irq_tracker, pf->hw_oicr_idx, ICE_RES_MISC_VEC_ID);
}

/**
 * ice_req_irq_msix_misc - Setup the misc vector to handle non queue events
 * @pf: board private structure
 *
 * This sets up the handler for MSIX 0, which is used to manage the
 * non-queue interrupts, e.g. AdminQ and errors.  This is not used
 * when in MSI or Legacy interrupt mode.
 */
static int ice_req_irq_msix_misc(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;
	int oicr_idx, err = 0;
	u8 itr_gran;
	u32 val;

	if (!pf->int_name[0])
		snprintf(pf->int_name, sizeof(pf->int_name) - 1, "%s-%s:misc",
			 dev_driver_string(&pf->pdev->dev),
			 dev_name(&pf->pdev->dev));

	/* Do not request IRQ but do enable OICR interrupt since settings are
	 * lost during reset. Note that this function is called only during
	 * rebuild path and not while reset is in progress.
	 */
	if (ice_is_reset_in_progress(pf->state))
		goto skip_req_irq;

	/* reserve one vector in sw_irq_tracker for misc interrupts */
	oicr_idx = ice_get_res(pf, pf->sw_irq_tracker, 1, ICE_RES_MISC_VEC_ID);
	if (oicr_idx < 0)
		return oicr_idx;

	pf->num_avail_sw_msix -= 1;
	pf->sw_oicr_idx = oicr_idx;

	/* reserve one vector in hw_irq_tracker for misc interrupts */
	oicr_idx = ice_get_res(pf, pf->hw_irq_tracker, 1, ICE_RES_MISC_VEC_ID);
	if (oicr_idx < 0) {
		ice_free_res(pf->sw_irq_tracker, 1, ICE_RES_MISC_VEC_ID);
		pf->num_avail_sw_msix += 1;
		return oicr_idx;
	}
	pf->num_avail_hw_msix -= 1;
	pf->hw_oicr_idx = oicr_idx;

	err = devm_request_irq(&pf->pdev->dev,
			       pf->msix_entries[pf->sw_oicr_idx].vector,
			       ice_misc_intr, 0, pf->int_name, pf);
	if (err) {
		dev_err(&pf->pdev->dev,
			"devm_request_irq for %s failed: %d\n",
			pf->int_name, err);
		ice_free_res(pf->sw_irq_tracker, 1, ICE_RES_MISC_VEC_ID);
		pf->num_avail_sw_msix += 1;
		ice_free_res(pf->hw_irq_tracker, 1, ICE_RES_MISC_VEC_ID);
		pf->num_avail_hw_msix += 1;
		return err;
	}

skip_req_irq:
	ice_ena_misc_vector(pf);

	val = ((pf->hw_oicr_idx & PFINT_OICR_CTL_MSIX_INDX_M) |
	       PFINT_OICR_CTL_CAUSE_ENA_M);
	wr32(hw, PFINT_OICR_CTL, val);

	/* This enables Admin queue Interrupt causes */
	val = ((pf->hw_oicr_idx & PFINT_FW_CTL_MSIX_INDX_M) |
	       PFINT_FW_CTL_CAUSE_ENA_M);
	wr32(hw, PFINT_FW_CTL, val);

	/* This enables Mailbox queue Interrupt causes */
	val = ((pf->hw_oicr_idx & PFINT_MBX_CTL_MSIX_INDX_M) |
	       PFINT_MBX_CTL_CAUSE_ENA_M);
	wr32(hw, PFINT_MBX_CTL, val);

	itr_gran = hw->itr_gran;

	wr32(hw, GLINT_ITR(ICE_RX_ITR, pf->hw_oicr_idx),
	     ITR_TO_REG(ICE_ITR_8K, itr_gran));

	ice_flush(hw);
	ice_irq_dynamic_ena(hw, NULL, NULL);

	return 0;
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

	for (v_idx = 0; v_idx < vsi->num_q_vectors; v_idx++)
		netif_napi_del(&vsi->q_vectors[v_idx]->napi);
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

	for (v_idx = 0; v_idx < vsi->num_q_vectors; v_idx++)
		netif_napi_add(vsi->netdev, &vsi->q_vectors[v_idx]->napi,
			       ice_napi_poll, NAPI_POLL_WEIGHT);
}

/**
 * ice_cfg_netdev - Allocate, configure and register a netdev
 * @vsi: the VSI associated with the new netdev
 *
 * Returns 0 on success, negative value on failure
 */
static int ice_cfg_netdev(struct ice_vsi *vsi)
{
	netdev_features_t csumo_features;
	netdev_features_t vlano_features;
	netdev_features_t dflt_features;
	netdev_features_t tso_features;
	struct ice_netdev_priv *np;
	struct net_device *netdev;
	u8 mac_addr[ETH_ALEN];
	int err;

	netdev = alloc_etherdev_mqs(sizeof(struct ice_netdev_priv),
				    vsi->alloc_txq, vsi->alloc_rxq);
	if (!netdev)
		return -ENOMEM;

	vsi->netdev = netdev;
	np = netdev_priv(netdev);
	np->vsi = vsi;

	dflt_features = NETIF_F_SG	|
			NETIF_F_HIGHDMA	|
			NETIF_F_RXHASH;

	csumo_features = NETIF_F_RXCSUM	  |
			 NETIF_F_IP_CSUM  |
			 NETIF_F_IPV6_CSUM;

	vlano_features = NETIF_F_HW_VLAN_CTAG_FILTER |
			 NETIF_F_HW_VLAN_CTAG_TX     |
			 NETIF_F_HW_VLAN_CTAG_RX;

	tso_features = NETIF_F_TSO;

	/* set features that user can change */
	netdev->hw_features = dflt_features | csumo_features |
			      vlano_features | tso_features;

	/* enable features */
	netdev->features |= netdev->hw_features;
	/* encap and VLAN devices inherit default, csumo and tso features */
	netdev->hw_enc_features |= dflt_features | csumo_features |
				   tso_features;
	netdev->vlan_features |= dflt_features | csumo_features |
				 tso_features;

	if (vsi->type == ICE_VSI_PF) {
		SET_NETDEV_DEV(netdev, &vsi->back->pdev->dev);
		ether_addr_copy(mac_addr, vsi->port_info->mac.perm_addr);

		ether_addr_copy(netdev->dev_addr, mac_addr);
		ether_addr_copy(netdev->perm_addr, mac_addr);
	}

	netdev->priv_flags |= IFF_UNICAST_FLT;

	/* assign netdev_ops */
	netdev->netdev_ops = &ice_netdev_ops;

	/* setup watchdog timeout value to be 5 second */
	netdev->watchdog_timeo = 5 * HZ;

	ice_set_ethtool_ops(netdev);

	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = ICE_MAX_MTU;

	err = register_netdev(vsi->netdev);
	if (err)
		return err;

	netif_carrier_off(vsi->netdev);

	/* make sure transmit queues start off as stopped */
	netif_tx_stop_all_queues(vsi->netdev);

	return 0;
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
 * Returns pointer to the successfully allocated VSI sw struct on success,
 * otherwise returns NULL on failure.
 */
static struct ice_vsi *
ice_pf_vsi_setup(struct ice_pf *pf, struct ice_port_info *pi)
{
	return ice_vsi_setup(pf, pi, ICE_VSI_PF, ICE_INVAL_VFID);
}

/**
 * ice_vlan_rx_add_vid - Add a vlan id filter to HW offload
 * @netdev: network interface to be adjusted
 * @proto: unused protocol
 * @vid: vlan id to be added
 *
 * net_device_ops implementation for adding vlan ids
 */
static int ice_vlan_rx_add_vid(struct net_device *netdev,
			       __always_unused __be16 proto, u16 vid)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;

	if (vid >= VLAN_N_VID) {
		netdev_err(netdev, "VLAN id requested %d is out of range %d\n",
			   vid, VLAN_N_VID);
		return -EINVAL;
	}

	if (vsi->info.pvid)
		return -EINVAL;

	/* Enable VLAN pruning when VLAN 0 is added */
	if (unlikely(!vid)) {
		int ret = ice_cfg_vlan_pruning(vsi, true);

		if (ret)
			return ret;
	}

	/* Add all VLAN ids including 0 to the switch filter. VLAN id 0 is
	 * needed to continue allowing all untagged packets since VLAN prune
	 * list is applied to all packets by the switch
	 */
	return ice_vsi_add_vlan(vsi, vid);
}

/**
 * ice_vlan_rx_kill_vid - Remove a vlan id filter from HW offload
 * @netdev: network interface to be adjusted
 * @proto: unused protocol
 * @vid: vlan id to be removed
 *
 * net_device_ops implementation for removing vlan ids
 */
static int ice_vlan_rx_kill_vid(struct net_device *netdev,
				__always_unused __be16 proto, u16 vid)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	int status;

	if (vsi->info.pvid)
		return -EINVAL;

	/* Make sure ice_vsi_kill_vlan is successful before updating VLAN
	 * information
	 */
	status = ice_vsi_kill_vlan(vsi, vid);
	if (status)
		return status;

	/* Disable VLAN pruning when VLAN 0 is removed */
	if (unlikely(!vid))
		status = ice_cfg_vlan_pruning(vsi, false);

	return status;
}

/**
 * ice_setup_pf_sw - Setup the HW switch on startup or after reset
 * @pf: board private structure
 *
 * Returns 0 on success, negative value on failure
 */
static int ice_setup_pf_sw(struct ice_pf *pf)
{
	LIST_HEAD(tmp_add_list);
	u8 broadcast[ETH_ALEN];
	struct ice_vsi *vsi;
	int status = 0;

	if (ice_is_reset_in_progress(pf->state))
		return -EBUSY;

	vsi = ice_pf_vsi_setup(pf, pf->hw.port_info);
	if (!vsi) {
		status = -ENOMEM;
		goto unroll_vsi_setup;
	}

	status = ice_cfg_netdev(vsi);
	if (status) {
		status = -ENODEV;
		goto unroll_vsi_setup;
	}

	/* registering the NAPI handler requires both the queues and
	 * netdev to be created, which are done in ice_pf_vsi_setup()
	 * and ice_cfg_netdev() respectively
	 */
	ice_napi_add(vsi);

	/* To add a MAC filter, first add the MAC to a list and then
	 * pass the list to ice_add_mac.
	 */

	 /* Add a unicast MAC filter so the VSI can get its packets */
	status = ice_add_mac_to_list(vsi, &tmp_add_list,
				     vsi->port_info->mac.perm_addr);
	if (status)
		goto unroll_napi_add;

	/* VSI needs to receive broadcast traffic, so add the broadcast
	 * MAC address to the list as well.
	 */
	eth_broadcast_addr(broadcast);
	status = ice_add_mac_to_list(vsi, &tmp_add_list, broadcast);
	if (status)
		goto free_mac_list;

	/* program MAC filters for entries in tmp_add_list */
	status = ice_add_mac(&pf->hw, &tmp_add_list);
	if (status) {
		dev_err(&pf->pdev->dev, "Could not add MAC filters\n");
		status = -ENOMEM;
		goto free_mac_list;
	}

	ice_free_fltr_list(&pf->pdev->dev, &tmp_add_list);
	return status;

free_mac_list:
	ice_free_fltr_list(&pf->pdev->dev, &tmp_add_list);

unroll_napi_add:
	if (vsi) {
		ice_napi_del(vsi);
		if (vsi->netdev) {
			if (vsi->netdev->reg_state == NETREG_REGISTERED)
				unregister_netdev(vsi->netdev);
			free_netdev(vsi->netdev);
			vsi->netdev = NULL;
		}
	}

unroll_vsi_setup:
	if (vsi) {
		ice_vsi_free_q_vectors(vsi);
		ice_vsi_delete(vsi);
		ice_vsi_put_qs(vsi);
		pf->q_left_tx += vsi->alloc_txq;
		pf->q_left_rx += vsi->alloc_rxq;
		ice_vsi_clear(vsi);
	}
	return status;
}

/**
 * ice_determine_q_usage - Calculate queue distribution
 * @pf: board private structure
 *
 * Return -ENOMEM if we don't get enough queues for all ports
 */
static void ice_determine_q_usage(struct ice_pf *pf)
{
	u16 q_left_tx, q_left_rx;

	q_left_tx = pf->hw.func_caps.common_cap.num_txq;
	q_left_rx = pf->hw.func_caps.common_cap.num_rxq;

	pf->num_lan_tx = min_t(int, q_left_tx, num_online_cpus());

	/* only 1 rx queue unless RSS is enabled */
	if (!test_bit(ICE_FLAG_RSS_ENA, pf->flags))
		pf->num_lan_rx = 1;
	else
		pf->num_lan_rx = min_t(int, q_left_rx, num_online_cpus());

	pf->q_left_tx = q_left_tx - pf->num_lan_tx;
	pf->q_left_rx = q_left_rx - pf->num_lan_rx;
}

/**
 * ice_deinit_pf - Unrolls initialziations done by ice_init_pf
 * @pf: board private structure to initialize
 */
static void ice_deinit_pf(struct ice_pf *pf)
{
	ice_service_task_stop(pf);
	mutex_destroy(&pf->sw_mutex);
	mutex_destroy(&pf->avail_q_mutex);
}

/**
 * ice_init_pf - Initialize general software structures (struct ice_pf)
 * @pf: board private structure to initialize
 */
static void ice_init_pf(struct ice_pf *pf)
{
	bitmap_zero(pf->flags, ICE_PF_FLAGS_NBITS);
	set_bit(ICE_FLAG_MSIX_ENA, pf->flags);
#ifdef CONFIG_PCI_IOV
	if (pf->hw.func_caps.common_cap.sr_iov_1_1) {
		struct ice_hw *hw = &pf->hw;

		set_bit(ICE_FLAG_SRIOV_CAPABLE, pf->flags);
		pf->num_vfs_supported = min_t(int, hw->func_caps.num_allocd_vfs,
					      ICE_MAX_VF_COUNT);
	}
#endif /* CONFIG_PCI_IOV */

	mutex_init(&pf->sw_mutex);
	mutex_init(&pf->avail_q_mutex);

	/* Clear avail_[t|r]x_qs bitmaps (set all to avail) */
	mutex_lock(&pf->avail_q_mutex);
	bitmap_zero(pf->avail_txqs, ICE_MAX_TXQS);
	bitmap_zero(pf->avail_rxqs, ICE_MAX_RXQS);
	mutex_unlock(&pf->avail_q_mutex);

	if (pf->hw.func_caps.common_cap.rss_table_size)
		set_bit(ICE_FLAG_RSS_ENA, pf->flags);

	/* setup service timer and periodic service task */
	timer_setup(&pf->serv_tmr, ice_service_timer, 0);
	pf->serv_tmr_period = HZ;
	INIT_WORK(&pf->serv_task, ice_service_task);
	clear_bit(__ICE_SERVICE_SCHED, pf->state);
}

/**
 * ice_ena_msix_range - Request a range of MSIX vectors from the OS
 * @pf: board private structure
 *
 * compute the number of MSIX vectors required (v_budget) and request from
 * the OS. Return the number of vectors reserved or negative on failure
 */
static int ice_ena_msix_range(struct ice_pf *pf)
{
	int v_left, v_actual, v_budget = 0;
	int needed, err, i;

	v_left = pf->hw.func_caps.common_cap.num_msix_vectors;

	/* reserve one vector for miscellaneous handler */
	needed = 1;
	v_budget += needed;
	v_left -= needed;

	/* reserve vectors for LAN traffic */
	pf->num_lan_msix = min_t(int, num_online_cpus(), v_left);
	v_budget += pf->num_lan_msix;
	v_left -= pf->num_lan_msix;

	pf->msix_entries = devm_kcalloc(&pf->pdev->dev, v_budget,
					sizeof(struct msix_entry), GFP_KERNEL);

	if (!pf->msix_entries) {
		err = -ENOMEM;
		goto exit_err;
	}

	for (i = 0; i < v_budget; i++)
		pf->msix_entries[i].entry = i;

	/* actually reserve the vectors */
	v_actual = pci_enable_msix_range(pf->pdev, pf->msix_entries,
					 ICE_MIN_MSIX, v_budget);

	if (v_actual < 0) {
		dev_err(&pf->pdev->dev, "unable to reserve MSI-X vectors\n");
		err = v_actual;
		goto msix_err;
	}

	if (v_actual < v_budget) {
		dev_warn(&pf->pdev->dev,
			 "not enough vectors. requested = %d, obtained = %d\n",
			 v_budget, v_actual);
		if (v_actual >= (pf->num_lan_msix + 1)) {
			pf->num_avail_sw_msix = v_actual -
						(pf->num_lan_msix + 1);
		} else if (v_actual >= 2) {
			pf->num_lan_msix = 1;
			pf->num_avail_sw_msix = v_actual - 2;
		} else {
			pci_disable_msix(pf->pdev);
			err = -ERANGE;
			goto msix_err;
		}
	}

	return v_actual;

msix_err:
	devm_kfree(&pf->pdev->dev, pf->msix_entries);
	goto exit_err;

exit_err:
	pf->num_lan_msix = 0;
	clear_bit(ICE_FLAG_MSIX_ENA, pf->flags);
	return err;
}

/**
 * ice_dis_msix - Disable MSI-X interrupt setup in OS
 * @pf: board private structure
 */
static void ice_dis_msix(struct ice_pf *pf)
{
	pci_disable_msix(pf->pdev);
	devm_kfree(&pf->pdev->dev, pf->msix_entries);
	pf->msix_entries = NULL;
	clear_bit(ICE_FLAG_MSIX_ENA, pf->flags);
}

/**
 * ice_clear_interrupt_scheme - Undo things done by ice_init_interrupt_scheme
 * @pf: board private structure
 */
static void ice_clear_interrupt_scheme(struct ice_pf *pf)
{
	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags))
		ice_dis_msix(pf);

	if (pf->sw_irq_tracker) {
		devm_kfree(&pf->pdev->dev, pf->sw_irq_tracker);
		pf->sw_irq_tracker = NULL;
	}

	if (pf->hw_irq_tracker) {
		devm_kfree(&pf->pdev->dev, pf->hw_irq_tracker);
		pf->hw_irq_tracker = NULL;
	}
}

/**
 * ice_init_interrupt_scheme - Determine proper interrupt scheme
 * @pf: board private structure to initialize
 */
static int ice_init_interrupt_scheme(struct ice_pf *pf)
{
	int vectors = 0, hw_vectors = 0;
	ssize_t size;

	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags))
		vectors = ice_ena_msix_range(pf);
	else
		return -ENODEV;

	if (vectors < 0)
		return vectors;

	/* set up vector assignment tracking */
	size = sizeof(struct ice_res_tracker) + (sizeof(u16) * vectors);

	pf->sw_irq_tracker = devm_kzalloc(&pf->pdev->dev, size, GFP_KERNEL);
	if (!pf->sw_irq_tracker) {
		ice_dis_msix(pf);
		return -ENOMEM;
	}

	/* populate SW interrupts pool with number of OS granted IRQs. */
	pf->num_avail_sw_msix = vectors;
	pf->sw_irq_tracker->num_entries = vectors;

	/* set up HW vector assignment tracking */
	hw_vectors = pf->hw.func_caps.common_cap.num_msix_vectors;
	size = sizeof(struct ice_res_tracker) + (sizeof(u16) * hw_vectors);

	pf->hw_irq_tracker = devm_kzalloc(&pf->pdev->dev, size, GFP_KERNEL);
	if (!pf->hw_irq_tracker) {
		ice_clear_interrupt_scheme(pf);
		return -ENOMEM;
	}

	/* populate HW interrupts pool with number of HW supported irqs. */
	pf->num_avail_hw_msix = hw_vectors;
	pf->hw_irq_tracker->num_entries = hw_vectors;

	return 0;
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
		dev_warn(&pf->pdev->dev,
			 "%d Byte cache line assumption is invalid, driver may have Tx timeouts!\n",
			 ICE_CACHE_LINE_BYTES);
}

/**
 * ice_probe - Device initialization routine
 * @pdev: PCI device information struct
 * @ent: entry in ice_pci_tbl
 *
 * Returns 0 on success, negative on failure
 */
static int ice_probe(struct pci_dev *pdev,
		     const struct pci_device_id __always_unused *ent)
{
	struct ice_pf *pf;
	struct ice_hw *hw;
	int err;

	/* this driver uses devres, see Documentation/driver-model/devres.txt */
	err = pcim_enable_device(pdev);
	if (err)
		return err;

	err = pcim_iomap_regions(pdev, BIT(ICE_BAR0), pci_name(pdev));
	if (err) {
		dev_err(&pdev->dev, "BAR0 I/O map error %d\n", err);
		return err;
	}

	pf = devm_kzalloc(&pdev->dev, sizeof(*pf), GFP_KERNEL);
	if (!pf)
		return -ENOMEM;

	/* set up for high or low dma */
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err)
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&pdev->dev, "DMA configuration failed: 0x%x\n", err);
		return err;
	}

	pci_enable_pcie_error_reporting(pdev);
	pci_set_master(pdev);

	pf->pdev = pdev;
	pci_set_drvdata(pdev, pf);
	set_bit(__ICE_DOWN, pf->state);
	/* Disable service task until DOWN bit is cleared */
	set_bit(__ICE_SERVICE_DIS, pf->state);

	hw = &pf->hw;
	hw->hw_addr = pcim_iomap_table(pdev)[ICE_BAR0];
	hw->back = pf;
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

	err = ice_init_hw(hw);
	if (err) {
		dev_err(&pdev->dev, "ice_init_hw failed: %d\n", err);
		err = -EIO;
		goto err_exit_unroll;
	}

	dev_info(&pdev->dev, "firmware %d.%d.%05d api %d.%d\n",
		 hw->fw_maj_ver, hw->fw_min_ver, hw->fw_build,
		 hw->api_maj_ver, hw->api_min_ver);

	ice_init_pf(pf);

	ice_determine_q_usage(pf);

	pf->num_alloc_vsi = hw->func_caps.guar_num_vsi;
	if (!pf->num_alloc_vsi) {
		err = -EIO;
		goto err_init_pf_unroll;
	}

	pf->vsi = devm_kcalloc(&pdev->dev, pf->num_alloc_vsi,
			       sizeof(struct ice_vsi *), GFP_KERNEL);
	if (!pf->vsi) {
		err = -ENOMEM;
		goto err_init_pf_unroll;
	}

	err = ice_init_interrupt_scheme(pf);
	if (err) {
		dev_err(&pdev->dev,
			"ice_init_interrupt_scheme failed: %d\n", err);
		err = -EIO;
		goto err_init_interrupt_unroll;
	}

	/* Driver is mostly up */
	clear_bit(__ICE_DOWN, pf->state);

	/* In case of MSIX we are going to setup the misc vector right here
	 * to handle admin queue events etc. In case of legacy and MSI
	 * the misc functionality and queue processing is combined in
	 * the same vector and that gets setup at open.
	 */
	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags)) {
		err = ice_req_irq_msix_misc(pf);
		if (err) {
			dev_err(&pdev->dev,
				"setup of misc vector failed: %d\n", err);
			goto err_init_interrupt_unroll;
		}
	}

	/* create switch struct for the switch element created by FW on boot */
	pf->first_sw = devm_kzalloc(&pdev->dev, sizeof(struct ice_sw),
				    GFP_KERNEL);
	if (!pf->first_sw) {
		err = -ENOMEM;
		goto err_msix_misc_unroll;
	}

	if (hw->evb_veb)
		pf->first_sw->bridge_mode = BRIDGE_MODE_VEB;
	else
		pf->first_sw->bridge_mode = BRIDGE_MODE_VEPA;

	pf->first_sw->pf = pf;

	/* record the sw_id available for later use */
	pf->first_sw->sw_id = hw->port_info->sw_id;

	err = ice_setup_pf_sw(pf);
	if (err) {
		dev_err(&pdev->dev,
			"probe failed due to setup pf switch:%d\n", err);
		goto err_alloc_sw_unroll;
	}

	clear_bit(__ICE_SERVICE_DIS, pf->state);

	/* since everything is good, start the service timer */
	mod_timer(&pf->serv_tmr, round_jiffies(jiffies + pf->serv_tmr_period));

	ice_verify_cacheline_size(pf);

	return 0;

err_alloc_sw_unroll:
	set_bit(__ICE_SERVICE_DIS, pf->state);
	set_bit(__ICE_DOWN, pf->state);
	devm_kfree(&pf->pdev->dev, pf->first_sw);
err_msix_misc_unroll:
	ice_free_irq_msix_misc(pf);
err_init_interrupt_unroll:
	ice_clear_interrupt_scheme(pf);
	devm_kfree(&pdev->dev, pf->vsi);
err_init_pf_unroll:
	ice_deinit_pf(pf);
	ice_deinit_hw(hw);
err_exit_unroll:
	pci_disable_pcie_error_reporting(pdev);
	return err;
}

/**
 * ice_remove - Device removal routine
 * @pdev: PCI device information struct
 */
static void ice_remove(struct pci_dev *pdev)
{
	struct ice_pf *pf = pci_get_drvdata(pdev);
	int i;

	if (!pf)
		return;

	for (i = 0; i < ICE_MAX_RESET_WAIT; i++) {
		if (!ice_is_reset_in_progress(pf->state))
			break;
		msleep(100);
	}

	set_bit(__ICE_DOWN, pf->state);
	ice_service_task_stop(pf);

	if (test_bit(ICE_FLAG_SRIOV_ENA, pf->flags))
		ice_free_vfs(pf);
	ice_vsi_release_all(pf);
	ice_free_irq_msix_misc(pf);
	ice_for_each_vsi(pf, i) {
		if (!pf->vsi[i])
			continue;
		ice_vsi_free_q_vectors(pf->vsi[i]);
	}
	ice_clear_interrupt_scheme(pf);
	ice_deinit_pf(pf);
	ice_deinit_hw(&pf->hw);
	pci_disable_pcie_error_reporting(pdev);
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
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E810C_BACKPLANE), 0 },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E810C_QSFP), 0 },
	{ PCI_VDEVICE(INTEL, ICE_DEV_ID_E810C_SFP), 0 },
	/* required last entry */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ice_pci_tbl);

static struct pci_driver ice_driver = {
	.name = KBUILD_MODNAME,
	.id_table = ice_pci_tbl,
	.probe = ice_probe,
	.remove = ice_remove,
	.sriov_configure = ice_sriov_configure,
};

/**
 * ice_module_init - Driver registration routine
 *
 * ice_module_init is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 */
static int __init ice_module_init(void)
{
	int status;

	pr_info("%s - version %s\n", ice_driver_string, ice_drv_ver);
	pr_info("%s\n", ice_copyright);

	ice_wq = alloc_workqueue("%s", WQ_MEM_RECLAIM, 0, KBUILD_MODNAME);
	if (!ice_wq) {
		pr_err("Failed to create workqueue\n");
		return -ENOMEM;
	}

	status = pci_register_driver(&ice_driver);
	if (status) {
		pr_err("failed to register pci driver, err %d\n", status);
		destroy_workqueue(ice_wq);
	}

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
	destroy_workqueue(ice_wq);
	pr_info("module unloaded\n");
}
module_exit(ice_module_exit);

/**
 * ice_set_mac_address - NDO callback to set mac address
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
	enum ice_status status;
	LIST_HEAD(a_mac_list);
	LIST_HEAD(r_mac_list);
	u8 flags = 0;
	int err;
	u8 *mac;

	mac = (u8 *)addr->sa_data;

	if (!is_valid_ether_addr(mac))
		return -EADDRNOTAVAIL;

	if (ether_addr_equal(netdev->dev_addr, mac)) {
		netdev_warn(netdev, "already using mac %pM\n", mac);
		return 0;
	}

	if (test_bit(__ICE_DOWN, pf->state) ||
	    ice_is_reset_in_progress(pf->state)) {
		netdev_err(netdev, "can't set mac %pM. device not ready\n",
			   mac);
		return -EBUSY;
	}

	/* When we change the mac address we also have to change the mac address
	 * based filter rules that were created previously for the old mac
	 * address. So first, we remove the old filter rule using ice_remove_mac
	 * and then create a new filter rule using ice_add_mac. Note that for
	 * both these operations, we first need to form a "list" of mac
	 * addresses (even though in this case, we have only 1 mac address to be
	 * added/removed) and this done using ice_add_mac_to_list. Depending on
	 * the ensuing operation this "list" of mac addresses is either to be
	 * added or removed from the filter.
	 */
	err = ice_add_mac_to_list(vsi, &r_mac_list, netdev->dev_addr);
	if (err) {
		err = -EADDRNOTAVAIL;
		goto free_lists;
	}

	status = ice_remove_mac(hw, &r_mac_list);
	if (status) {
		err = -EADDRNOTAVAIL;
		goto free_lists;
	}

	err = ice_add_mac_to_list(vsi, &a_mac_list, mac);
	if (err) {
		err = -EADDRNOTAVAIL;
		goto free_lists;
	}

	status = ice_add_mac(hw, &a_mac_list);
	if (status) {
		err = -EADDRNOTAVAIL;
		goto free_lists;
	}

free_lists:
	/* free list entries */
	ice_free_fltr_list(&pf->pdev->dev, &r_mac_list);
	ice_free_fltr_list(&pf->pdev->dev, &a_mac_list);

	if (err) {
		netdev_err(netdev, "can't set mac %pM. filter update failed\n",
			   mac);
		return err;
	}

	/* change the netdev's mac address */
	memcpy(netdev->dev_addr, mac, netdev->addr_len);
	netdev_dbg(vsi->netdev, "updated mac address to %pM\n",
		   netdev->dev_addr);

	/* write new mac address to the firmware */
	flags = ICE_AQC_MAN_MAC_UPDATE_LAA_WOL;
	status = ice_aq_manage_mac_write(hw, mac, flags, NULL);
	if (status) {
		netdev_err(netdev, "can't set mac %pM. write to firmware failed.\n",
			   mac);
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

	if (!vsi)
		return;

	/* Set the flags to synchronize filters
	 * ndo_set_rx_mode may be triggered even without a change in netdev
	 * flags
	 */
	set_bit(ICE_VSI_FLAG_UMAC_FLTR_CHANGED, vsi->flags);
	set_bit(ICE_VSI_FLAG_MMAC_FLTR_CHANGED, vsi->flags);
	set_bit(ICE_FLAG_FLTR_SYNC, vsi->back->flags);

	/* schedule our worker thread which will take care of
	 * applying the new filter changes
	 */
	ice_service_task_schedule(vsi->back);
}

/**
 * ice_fdb_add - add an entry to the hardware database
 * @ndm: the input from the stack
 * @tb: pointer to array of nladdr (unused)
 * @dev: the net device pointer
 * @addr: the MAC address entry being added
 * @vid: VLAN id
 * @flags: instructions from stack about fdb operation
 */
static int ice_fdb_add(struct ndmsg *ndm, struct nlattr __always_unused *tb[],
		       struct net_device *dev, const unsigned char *addr,
		       u16 vid, u16 flags)
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
 * @vid: VLAN id
 */
static int ice_fdb_del(struct ndmsg *ndm, __always_unused struct nlattr *tb[],
		       struct net_device *dev, const unsigned char *addr,
		       __always_unused u16 vid)
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

/**
 * ice_set_features - set the netdev feature flags
 * @netdev: ptr to the netdev being adjusted
 * @features: the feature set that the stack is suggesting
 */
static int ice_set_features(struct net_device *netdev,
			    netdev_features_t features)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	int ret = 0;

	if (features & NETIF_F_RXHASH && !(netdev->features & NETIF_F_RXHASH))
		ret = ice_vsi_manage_rss_lut(vsi, true);
	else if (!(features & NETIF_F_RXHASH) &&
		 netdev->features & NETIF_F_RXHASH)
		ret = ice_vsi_manage_rss_lut(vsi, false);

	if ((features & NETIF_F_HW_VLAN_CTAG_RX) &&
	    !(netdev->features & NETIF_F_HW_VLAN_CTAG_RX))
		ret = ice_vsi_manage_vlan_stripping(vsi, true);
	else if (!(features & NETIF_F_HW_VLAN_CTAG_RX) &&
		 (netdev->features & NETIF_F_HW_VLAN_CTAG_RX))
		ret = ice_vsi_manage_vlan_stripping(vsi, false);
	else if ((features & NETIF_F_HW_VLAN_CTAG_TX) &&
		 !(netdev->features & NETIF_F_HW_VLAN_CTAG_TX))
		ret = ice_vsi_manage_vlan_insertion(vsi);
	else if (!(features & NETIF_F_HW_VLAN_CTAG_TX) &&
		 (netdev->features & NETIF_F_HW_VLAN_CTAG_TX))
		ret = ice_vsi_manage_vlan_insertion(vsi);

	return ret;
}

/**
 * ice_vsi_vlan_setup - Setup vlan offload properties on a VSI
 * @vsi: VSI to setup vlan properties for
 */
static int ice_vsi_vlan_setup(struct ice_vsi *vsi)
{
	int ret = 0;

	if (vsi->netdev->features & NETIF_F_HW_VLAN_CTAG_RX)
		ret = ice_vsi_manage_vlan_stripping(vsi, true);
	if (vsi->netdev->features & NETIF_F_HW_VLAN_CTAG_TX)
		ret = ice_vsi_manage_vlan_insertion(vsi);

	return ret;
}

/**
 * ice_vsi_cfg - Setup the VSI
 * @vsi: the VSI being configured
 *
 * Return 0 on success and negative value on error
 */
static int ice_vsi_cfg(struct ice_vsi *vsi)
{
	int err;

	if (vsi->netdev) {
		ice_set_rx_mode(vsi->netdev);

		err = ice_vsi_vlan_setup(vsi);

		if (err)
			return err;
	}
	err = ice_vsi_cfg_txqs(vsi);
	if (!err)
		err = ice_vsi_cfg_rxqs(vsi);

	return err;
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

	for (q_idx = 0; q_idx < vsi->num_q_vectors; q_idx++)
		napi_enable(&vsi->q_vectors[q_idx]->napi);
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

	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags))
		ice_vsi_cfg_msix(vsi);
	else
		return -ENOTSUPP;

	/* Enable only Rx rings, Tx rings were enabled by the FW when the
	 * Tx queue group list was configured and the context bits were
	 * programmed using ice_vsi_cfg_txqs
	 */
	err = ice_vsi_start_rx_rings(vsi);
	if (err)
		return err;

	clear_bit(__ICE_DOWN, vsi->state);
	ice_napi_enable_all(vsi);
	ice_vsi_ena_irq(vsi);

	if (vsi->port_info &&
	    (vsi->port_info->phy.link_info.link_info & ICE_AQ_LINK_UP) &&
	    vsi->netdev) {
		ice_print_link_msg(vsi, true);
		netif_tx_start_all_queues(vsi->netdev);
		netif_carrier_on(vsi->netdev);
	}

	ice_service_task_schedule(pf);

	return err;
}

/**
 * ice_up - Bring the connection back up after being down
 * @vsi: VSI being configured
 */
int ice_up(struct ice_vsi *vsi)
{
	int err;

	err = ice_vsi_cfg(vsi);
	if (!err)
		err = ice_up_complete(vsi);

	return err;
}

/**
 * ice_fetch_u64_stats_per_ring - get packets and bytes stats per ring
 * @ring: Tx or Rx ring to read stats from
 * @pkts: packets stats counter
 * @bytes: bytes stats counter
 *
 * This function fetches stats from the ring considering the atomic operations
 * that needs to be performed to read u64 values in 32 bit machine.
 */
static void ice_fetch_u64_stats_per_ring(struct ice_ring *ring, u64 *pkts,
					 u64 *bytes)
{
	unsigned int start;
	*pkts = 0;
	*bytes = 0;

	if (!ring)
		return;
	do {
		start = u64_stats_fetch_begin_irq(&ring->syncp);
		*pkts = ring->stats.pkts;
		*bytes = ring->stats.bytes;
	} while (u64_stats_fetch_retry_irq(&ring->syncp, start));
}

/**
 * ice_update_vsi_ring_stats - Update VSI stats counters
 * @vsi: the VSI to be updated
 */
static void ice_update_vsi_ring_stats(struct ice_vsi *vsi)
{
	struct rtnl_link_stats64 *vsi_stats = &vsi->net_stats;
	struct ice_ring *ring;
	u64 pkts, bytes;
	int i;

	/* reset netdev stats */
	vsi_stats->tx_packets = 0;
	vsi_stats->tx_bytes = 0;
	vsi_stats->rx_packets = 0;
	vsi_stats->rx_bytes = 0;

	/* reset non-netdev (extended) stats */
	vsi->tx_restart = 0;
	vsi->tx_busy = 0;
	vsi->tx_linearize = 0;
	vsi->rx_buf_failed = 0;
	vsi->rx_page_failed = 0;

	rcu_read_lock();

	/* update Tx rings counters */
	ice_for_each_txq(vsi, i) {
		ring = READ_ONCE(vsi->tx_rings[i]);
		ice_fetch_u64_stats_per_ring(ring, &pkts, &bytes);
		vsi_stats->tx_packets += pkts;
		vsi_stats->tx_bytes += bytes;
		vsi->tx_restart += ring->tx_stats.restart_q;
		vsi->tx_busy += ring->tx_stats.tx_busy;
		vsi->tx_linearize += ring->tx_stats.tx_linearize;
	}

	/* update Rx rings counters */
	ice_for_each_rxq(vsi, i) {
		ring = READ_ONCE(vsi->rx_rings[i]);
		ice_fetch_u64_stats_per_ring(ring, &pkts, &bytes);
		vsi_stats->rx_packets += pkts;
		vsi_stats->rx_bytes += bytes;
		vsi->rx_buf_failed += ring->rx_stats.alloc_buf_failed;
		vsi->rx_page_failed += ring->rx_stats.alloc_page_failed;
	}

	rcu_read_unlock();
}

/**
 * ice_update_vsi_stats - Update VSI stats counters
 * @vsi: the VSI to be updated
 */
static void ice_update_vsi_stats(struct ice_vsi *vsi)
{
	struct rtnl_link_stats64 *cur_ns = &vsi->net_stats;
	struct ice_eth_stats *cur_es = &vsi->eth_stats;
	struct ice_pf *pf = vsi->back;

	if (test_bit(__ICE_DOWN, vsi->state) ||
	    test_bit(__ICE_CFG_BUSY, pf->state))
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
				    pf->stats.illegal_bytes;
		cur_ns->rx_length_errors = pf->stats.rx_len_errors;
	}
}

/**
 * ice_update_pf_stats - Update PF port stats counters
 * @pf: PF whose stats needs to be updated
 */
static void ice_update_pf_stats(struct ice_pf *pf)
{
	struct ice_hw_port_stats *prev_ps, *cur_ps;
	struct ice_hw *hw = &pf->hw;
	u8 pf_id;

	prev_ps = &pf->stats_prev;
	cur_ps = &pf->stats;
	pf_id = hw->pf_id;

	ice_stat_update40(hw, GLPRT_GORCH(pf_id), GLPRT_GORCL(pf_id),
			  pf->stat_prev_loaded, &prev_ps->eth.rx_bytes,
			  &cur_ps->eth.rx_bytes);

	ice_stat_update40(hw, GLPRT_UPRCH(pf_id), GLPRT_UPRCL(pf_id),
			  pf->stat_prev_loaded, &prev_ps->eth.rx_unicast,
			  &cur_ps->eth.rx_unicast);

	ice_stat_update40(hw, GLPRT_MPRCH(pf_id), GLPRT_MPRCL(pf_id),
			  pf->stat_prev_loaded, &prev_ps->eth.rx_multicast,
			  &cur_ps->eth.rx_multicast);

	ice_stat_update40(hw, GLPRT_BPRCH(pf_id), GLPRT_BPRCL(pf_id),
			  pf->stat_prev_loaded, &prev_ps->eth.rx_broadcast,
			  &cur_ps->eth.rx_broadcast);

	ice_stat_update40(hw, GLPRT_GOTCH(pf_id), GLPRT_GOTCL(pf_id),
			  pf->stat_prev_loaded, &prev_ps->eth.tx_bytes,
			  &cur_ps->eth.tx_bytes);

	ice_stat_update40(hw, GLPRT_UPTCH(pf_id), GLPRT_UPTCL(pf_id),
			  pf->stat_prev_loaded, &prev_ps->eth.tx_unicast,
			  &cur_ps->eth.tx_unicast);

	ice_stat_update40(hw, GLPRT_MPTCH(pf_id), GLPRT_MPTCL(pf_id),
			  pf->stat_prev_loaded, &prev_ps->eth.tx_multicast,
			  &cur_ps->eth.tx_multicast);

	ice_stat_update40(hw, GLPRT_BPTCH(pf_id), GLPRT_BPTCL(pf_id),
			  pf->stat_prev_loaded, &prev_ps->eth.tx_broadcast,
			  &cur_ps->eth.tx_broadcast);

	ice_stat_update32(hw, GLPRT_TDOLD(pf_id), pf->stat_prev_loaded,
			  &prev_ps->tx_dropped_link_down,
			  &cur_ps->tx_dropped_link_down);

	ice_stat_update40(hw, GLPRT_PRC64H(pf_id), GLPRT_PRC64L(pf_id),
			  pf->stat_prev_loaded, &prev_ps->rx_size_64,
			  &cur_ps->rx_size_64);

	ice_stat_update40(hw, GLPRT_PRC127H(pf_id), GLPRT_PRC127L(pf_id),
			  pf->stat_prev_loaded, &prev_ps->rx_size_127,
			  &cur_ps->rx_size_127);

	ice_stat_update40(hw, GLPRT_PRC255H(pf_id), GLPRT_PRC255L(pf_id),
			  pf->stat_prev_loaded, &prev_ps->rx_size_255,
			  &cur_ps->rx_size_255);

	ice_stat_update40(hw, GLPRT_PRC511H(pf_id), GLPRT_PRC511L(pf_id),
			  pf->stat_prev_loaded, &prev_ps->rx_size_511,
			  &cur_ps->rx_size_511);

	ice_stat_update40(hw, GLPRT_PRC1023H(pf_id),
			  GLPRT_PRC1023L(pf_id), pf->stat_prev_loaded,
			  &prev_ps->rx_size_1023, &cur_ps->rx_size_1023);

	ice_stat_update40(hw, GLPRT_PRC1522H(pf_id),
			  GLPRT_PRC1522L(pf_id), pf->stat_prev_loaded,
			  &prev_ps->rx_size_1522, &cur_ps->rx_size_1522);

	ice_stat_update40(hw, GLPRT_PRC9522H(pf_id),
			  GLPRT_PRC9522L(pf_id), pf->stat_prev_loaded,
			  &prev_ps->rx_size_big, &cur_ps->rx_size_big);

	ice_stat_update40(hw, GLPRT_PTC64H(pf_id), GLPRT_PTC64L(pf_id),
			  pf->stat_prev_loaded, &prev_ps->tx_size_64,
			  &cur_ps->tx_size_64);

	ice_stat_update40(hw, GLPRT_PTC127H(pf_id), GLPRT_PTC127L(pf_id),
			  pf->stat_prev_loaded, &prev_ps->tx_size_127,
			  &cur_ps->tx_size_127);

	ice_stat_update40(hw, GLPRT_PTC255H(pf_id), GLPRT_PTC255L(pf_id),
			  pf->stat_prev_loaded, &prev_ps->tx_size_255,
			  &cur_ps->tx_size_255);

	ice_stat_update40(hw, GLPRT_PTC511H(pf_id), GLPRT_PTC511L(pf_id),
			  pf->stat_prev_loaded, &prev_ps->tx_size_511,
			  &cur_ps->tx_size_511);

	ice_stat_update40(hw, GLPRT_PTC1023H(pf_id),
			  GLPRT_PTC1023L(pf_id), pf->stat_prev_loaded,
			  &prev_ps->tx_size_1023, &cur_ps->tx_size_1023);

	ice_stat_update40(hw, GLPRT_PTC1522H(pf_id),
			  GLPRT_PTC1522L(pf_id), pf->stat_prev_loaded,
			  &prev_ps->tx_size_1522, &cur_ps->tx_size_1522);

	ice_stat_update40(hw, GLPRT_PTC9522H(pf_id),
			  GLPRT_PTC9522L(pf_id), pf->stat_prev_loaded,
			  &prev_ps->tx_size_big, &cur_ps->tx_size_big);

	ice_stat_update32(hw, GLPRT_LXONRXC(pf_id), pf->stat_prev_loaded,
			  &prev_ps->link_xon_rx, &cur_ps->link_xon_rx);

	ice_stat_update32(hw, GLPRT_LXOFFRXC(pf_id), pf->stat_prev_loaded,
			  &prev_ps->link_xoff_rx, &cur_ps->link_xoff_rx);

	ice_stat_update32(hw, GLPRT_LXONTXC(pf_id), pf->stat_prev_loaded,
			  &prev_ps->link_xon_tx, &cur_ps->link_xon_tx);

	ice_stat_update32(hw, GLPRT_LXOFFTXC(pf_id), pf->stat_prev_loaded,
			  &prev_ps->link_xoff_tx, &cur_ps->link_xoff_tx);

	ice_stat_update32(hw, GLPRT_CRCERRS(pf_id), pf->stat_prev_loaded,
			  &prev_ps->crc_errors, &cur_ps->crc_errors);

	ice_stat_update32(hw, GLPRT_ILLERRC(pf_id), pf->stat_prev_loaded,
			  &prev_ps->illegal_bytes, &cur_ps->illegal_bytes);

	ice_stat_update32(hw, GLPRT_MLFC(pf_id), pf->stat_prev_loaded,
			  &prev_ps->mac_local_faults,
			  &cur_ps->mac_local_faults);

	ice_stat_update32(hw, GLPRT_MRFC(pf_id), pf->stat_prev_loaded,
			  &prev_ps->mac_remote_faults,
			  &cur_ps->mac_remote_faults);

	ice_stat_update32(hw, GLPRT_RLEC(pf_id), pf->stat_prev_loaded,
			  &prev_ps->rx_len_errors, &cur_ps->rx_len_errors);

	ice_stat_update32(hw, GLPRT_RUC(pf_id), pf->stat_prev_loaded,
			  &prev_ps->rx_undersize, &cur_ps->rx_undersize);

	ice_stat_update32(hw, GLPRT_RFC(pf_id), pf->stat_prev_loaded,
			  &prev_ps->rx_fragments, &cur_ps->rx_fragments);

	ice_stat_update32(hw, GLPRT_ROC(pf_id), pf->stat_prev_loaded,
			  &prev_ps->rx_oversize, &cur_ps->rx_oversize);

	ice_stat_update32(hw, GLPRT_RJC(pf_id), pf->stat_prev_loaded,
			  &prev_ps->rx_jabber, &cur_ps->rx_jabber);

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

	if (test_bit(__ICE_DOWN, vsi->state) || !vsi->num_txq || !vsi->num_rxq)
		return;
	/* netdev packet/byte stats come from ring counter. These are obtained
	 * by summing up ring counters (done by ice_update_vsi_ring_stats).
	 */
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

	for (q_idx = 0; q_idx < vsi->num_q_vectors; q_idx++)
		napi_disable(&vsi->q_vectors[q_idx]->napi);
}

/**
 * ice_down - Shutdown the connection
 * @vsi: The VSI being stopped
 */
int ice_down(struct ice_vsi *vsi)
{
	int i, tx_err, rx_err;

	/* Caller of this function is expected to set the
	 * vsi->state __ICE_DOWN bit
	 */
	if (vsi->netdev) {
		netif_carrier_off(vsi->netdev);
		netif_tx_disable(vsi->netdev);
	}

	ice_vsi_dis_irq(vsi);
	tx_err = ice_vsi_stop_tx_rings(vsi, ICE_NO_RESET, 0);
	if (tx_err)
		netdev_err(vsi->netdev,
			   "Failed stop Tx rings, VSI %d error %d\n",
			   vsi->vsi_num, tx_err);

	rx_err = ice_vsi_stop_rx_rings(vsi);
	if (rx_err)
		netdev_err(vsi->netdev,
			   "Failed stop Rx rings, VSI %d error %d\n",
			   vsi->vsi_num, rx_err);

	ice_napi_disable_all(vsi);

	ice_for_each_txq(vsi, i)
		ice_clean_tx_ring(vsi->tx_rings[i]);

	ice_for_each_rxq(vsi, i)
		ice_clean_rx_ring(vsi->rx_rings[i]);

	if (tx_err || rx_err) {
		netdev_err(vsi->netdev,
			   "Failed to close VSI 0x%04X on switch 0x%04X\n",
			   vsi->vsi_num, vsi->vsw->sw_id);
		return -EIO;
	}

	return 0;
}

/**
 * ice_vsi_setup_tx_rings - Allocate VSI Tx queue resources
 * @vsi: VSI having resources allocated
 *
 * Return 0 on success, negative on failure
 */
static int ice_vsi_setup_tx_rings(struct ice_vsi *vsi)
{
	int i, err = 0;

	if (!vsi->num_txq) {
		dev_err(&vsi->back->pdev->dev, "VSI %d has 0 Tx queues\n",
			vsi->vsi_num);
		return -EINVAL;
	}

	ice_for_each_txq(vsi, i) {
		vsi->tx_rings[i]->netdev = vsi->netdev;
		err = ice_setup_tx_ring(vsi->tx_rings[i]);
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
static int ice_vsi_setup_rx_rings(struct ice_vsi *vsi)
{
	int i, err = 0;

	if (!vsi->num_rxq) {
		dev_err(&vsi->back->pdev->dev, "VSI %d has 0 Rx queues\n",
			vsi->vsi_num);
		return -EINVAL;
	}

	ice_for_each_rxq(vsi, i) {
		vsi->rx_rings[i]->netdev = vsi->netdev;
		err = ice_setup_rx_ring(vsi->rx_rings[i]);
		if (err)
			break;
	}

	return err;
}

/**
 * ice_vsi_req_irq - Request IRQ from the OS
 * @vsi: The VSI IRQ is being requested for
 * @basename: name for the vector
 *
 * Return 0 on success and a negative value on error
 */
static int ice_vsi_req_irq(struct ice_vsi *vsi, char *basename)
{
	struct ice_pf *pf = vsi->back;
	int err = -EINVAL;

	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags))
		err = ice_vsi_req_irq_msix(vsi, basename);

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
static int ice_vsi_open(struct ice_vsi *vsi)
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

	err = ice_vsi_cfg(vsi);
	if (err)
		goto err_setup_rx;

	snprintf(int_name, sizeof(int_name) - 1, "%s-%s",
		 dev_driver_string(&pf->pdev->dev), vsi->netdev->name);
	err = ice_vsi_req_irq(vsi, int_name);
	if (err)
		goto err_setup_rx;

	/* Notify the stack of the actual queue counts. */
	err = netif_set_real_num_tx_queues(vsi->netdev, vsi->num_txq);
	if (err)
		goto err_set_qs;

	err = netif_set_real_num_rx_queues(vsi->netdev, vsi->num_rxq);
	if (err)
		goto err_set_qs;

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

	for (i = 0; i < pf->num_alloc_vsi; i++) {
		if (!pf->vsi[i])
			continue;

		err = ice_vsi_release(pf->vsi[i]);
		if (err)
			dev_dbg(&pf->pdev->dev,
				"Failed to release pf->vsi[%d], err %d, vsi_num = %d\n",
				i, err, pf->vsi[i]->vsi_num);
	}
}

/**
 * ice_dis_vsi - pause a VSI
 * @vsi: the VSI being paused
 * @locked: is the rtnl_lock already held
 */
static void ice_dis_vsi(struct ice_vsi *vsi, bool locked)
{
	if (test_bit(__ICE_DOWN, vsi->state))
		return;

	set_bit(__ICE_NEEDS_RESTART, vsi->state);

	if (vsi->type == ICE_VSI_PF && vsi->netdev) {
		if (netif_running(vsi->netdev)) {
			if (!locked) {
				rtnl_lock();
				vsi->netdev->netdev_ops->ndo_stop(vsi->netdev);
				rtnl_unlock();
			} else {
				vsi->netdev->netdev_ops->ndo_stop(vsi->netdev);
			}
		} else {
			ice_vsi_close(vsi);
		}
	}
}

/**
 * ice_ena_vsi - resume a VSI
 * @vsi: the VSI being resume
 */
static int ice_ena_vsi(struct ice_vsi *vsi)
{
	int err = 0;

	if (test_and_clear_bit(__ICE_NEEDS_RESTART, vsi->state) &&
	    vsi->netdev) {
		if (netif_running(vsi->netdev)) {
			rtnl_lock();
			err = vsi->netdev->netdev_ops->ndo_open(vsi->netdev);
			rtnl_unlock();
		} else {
			err = ice_vsi_open(vsi);
		}
	}

	return err;
}

/**
 * ice_pf_dis_all_vsi - Pause all VSIs on a PF
 * @pf: the PF
 */
static void ice_pf_dis_all_vsi(struct ice_pf *pf)
{
	int v;

	ice_for_each_vsi(pf, v)
		if (pf->vsi[v])
			ice_dis_vsi(pf->vsi[v], false);
}

/**
 * ice_pf_ena_all_vsi - Resume all VSIs on a PF
 * @pf: the PF
 */
static int ice_pf_ena_all_vsi(struct ice_pf *pf)
{
	int v;

	ice_for_each_vsi(pf, v)
		if (pf->vsi[v])
			if (ice_ena_vsi(pf->vsi[v]))
				return -EIO;

	return 0;
}

/**
 * ice_vsi_rebuild_all - rebuild all VSIs in pf
 * @pf: the PF
 */
static int ice_vsi_rebuild_all(struct ice_pf *pf)
{
	int i;

	/* loop through pf->vsi array and reinit the VSI if found */
	for (i = 0; i < pf->num_alloc_vsi; i++) {
		int err;

		if (!pf->vsi[i])
			continue;

		/* VF VSI rebuild isn't supported yet */
		if (pf->vsi[i]->type == ICE_VSI_VF)
			continue;

		err = ice_vsi_rebuild(pf->vsi[i]);
		if (err) {
			dev_err(&pf->pdev->dev,
				"VSI at index %d rebuild failed\n",
				pf->vsi[i]->idx);
			return err;
		}

		dev_info(&pf->pdev->dev,
			 "VSI at index %d rebuilt. vsi_num = 0x%x\n",
			 pf->vsi[i]->idx, pf->vsi[i]->vsi_num);
	}

	return 0;
}

/**
 * ice_vsi_replay_all - replay all VSIs configuration in the PF
 * @pf: the PF
 */
static int ice_vsi_replay_all(struct ice_pf *pf)
{
	struct ice_hw *hw = &pf->hw;
	enum ice_status ret;
	int i;

	/* loop through pf->vsi array and replay the VSI if found */
	for (i = 0; i < pf->num_alloc_vsi; i++) {
		if (!pf->vsi[i])
			continue;

		ret = ice_replay_vsi(hw, pf->vsi[i]->idx);
		if (ret) {
			dev_err(&pf->pdev->dev,
				"VSI at index %d replay failed %d\n",
				pf->vsi[i]->idx, ret);
			return -EIO;
		}

		/* Re-map HW VSI number, using VSI handle that has been
		 * previously validated in ice_replay_vsi() call above
		 */
		pf->vsi[i]->vsi_num = ice_get_hw_vsi_num(hw, pf->vsi[i]->idx);

		dev_info(&pf->pdev->dev,
			 "VSI at index %d filter replayed successfully - vsi_num %i\n",
			 pf->vsi[i]->idx, pf->vsi[i]->vsi_num);
	}

	/* Clean up replay filter after successful re-configuration */
	ice_replay_post(hw);
	return 0;
}

/**
 * ice_rebuild - rebuild after reset
 * @pf: pf to rebuild
 */
static void ice_rebuild(struct ice_pf *pf)
{
	struct device *dev = &pf->pdev->dev;
	struct ice_hw *hw = &pf->hw;
	enum ice_status ret;
	int err, i;

	if (test_bit(__ICE_DOWN, pf->state))
		goto clear_recovery;

	dev_dbg(dev, "rebuilding pf\n");

	ret = ice_init_all_ctrlq(hw);
	if (ret) {
		dev_err(dev, "control queues init failed %d\n", ret);
		goto err_init_ctrlq;
	}

	ret = ice_clear_pf_cfg(hw);
	if (ret) {
		dev_err(dev, "clear PF configuration failed %d\n", ret);
		goto err_init_ctrlq;
	}

	ice_clear_pxe_mode(hw);

	ret = ice_get_caps(hw);
	if (ret) {
		dev_err(dev, "ice_get_caps failed %d\n", ret);
		goto err_init_ctrlq;
	}

	err = ice_sched_init_port(hw->port_info);
	if (err)
		goto err_sched_init_port;

	/* reset search_hint of irq_trackers to 0 since interrupts are
	 * reclaimed and could be allocated from beginning during VSI rebuild
	 */
	pf->sw_irq_tracker->search_hint = 0;
	pf->hw_irq_tracker->search_hint = 0;

	err = ice_vsi_rebuild_all(pf);
	if (err) {
		dev_err(dev, "ice_vsi_rebuild_all failed\n");
		goto err_vsi_rebuild;
	}

	err = ice_update_link_info(hw->port_info);
	if (err)
		dev_err(&pf->pdev->dev, "Get link status error %d\n", err);

	/* Replay all VSIs Configuration, including filters after reset */
	if (ice_vsi_replay_all(pf)) {
		dev_err(&pf->pdev->dev,
			"error replaying VSI configurations with switch filter rules\n");
		goto err_vsi_rebuild;
	}

	/* start misc vector */
	if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags)) {
		err = ice_req_irq_msix_misc(pf);
		if (err) {
			dev_err(dev, "misc vector setup failed: %d\n", err);
			goto err_vsi_rebuild;
		}
	}

	/* restart the VSIs that were rebuilt and running before the reset */
	err = ice_pf_ena_all_vsi(pf);
	if (err) {
		dev_err(&pf->pdev->dev, "error enabling VSIs\n");
		/* no need to disable VSIs in tear down path in ice_rebuild()
		 * since its already taken care in ice_vsi_open()
		 */
		goto err_vsi_rebuild;
	}

	ice_reset_all_vfs(pf, true);

	for (i = 0; i < pf->num_alloc_vsi; i++) {
		bool link_up;

		if (!pf->vsi[i] || pf->vsi[i]->type != ICE_VSI_PF)
			continue;
		ice_get_link_status(pf->vsi[i]->port_info, &link_up);
		if (link_up) {
			netif_carrier_on(pf->vsi[i]->netdev);
			netif_tx_wake_all_queues(pf->vsi[i]->netdev);
		} else {
			netif_carrier_off(pf->vsi[i]->netdev);
			netif_tx_stop_all_queues(pf->vsi[i]->netdev);
		}
	}

	/* if we get here, reset flow is successful */
	clear_bit(__ICE_RESET_FAILED, pf->state);
	return;

err_vsi_rebuild:
	ice_vsi_release_all(pf);
err_sched_init_port:
	ice_sched_cleanup_all(hw);
err_init_ctrlq:
	ice_shutdown_all_ctrlq(hw);
	set_bit(__ICE_RESET_FAILED, pf->state);
clear_recovery:
	/* set this bit in PF state to control service task scheduling */
	set_bit(__ICE_NEEDS_RESTART, pf->state);
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
	u8 count = 0;

	if (new_mtu == netdev->mtu) {
		netdev_warn(netdev, "mtu is already %u\n", netdev->mtu);
		return 0;
	}

	if (new_mtu < netdev->min_mtu) {
		netdev_err(netdev, "new mtu invalid. min_mtu is %d\n",
			   netdev->min_mtu);
		return -EINVAL;
	} else if (new_mtu > netdev->max_mtu) {
		netdev_err(netdev, "new mtu invalid. max_mtu is %d\n",
			   netdev->min_mtu);
		return -EINVAL;
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
		netdev_err(netdev, "can't change mtu. Device is busy\n");
		return -EBUSY;
	}

	netdev->mtu = new_mtu;

	/* if VSI is up, bring it down and then back up */
	if (!test_and_set_bit(__ICE_DOWN, vsi->state)) {
		int err;

		err = ice_down(vsi);
		if (err) {
			netdev_err(netdev, "change mtu if_up err %d\n", err);
			return err;
		}

		err = ice_up(vsi);
		if (err) {
			netdev_err(netdev, "change mtu if_up err %d\n", err);
			return err;
		}
	}

	netdev_dbg(netdev, "changed mtu to %d\n", new_mtu);
	return 0;
}

/**
 * ice_set_rss - Set RSS keys and lut
 * @vsi: Pointer to VSI structure
 * @seed: RSS hash seed
 * @lut: Lookup table
 * @lut_size: Lookup table size
 *
 * Returns 0 on success, negative on failure
 */
int ice_set_rss(struct ice_vsi *vsi, u8 *seed, u8 *lut, u16 lut_size)
{
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;

	if (seed) {
		struct ice_aqc_get_set_rss_keys *buf =
				  (struct ice_aqc_get_set_rss_keys *)seed;

		status = ice_aq_set_rss_key(hw, vsi->idx, buf);

		if (status) {
			dev_err(&pf->pdev->dev,
				"Cannot set RSS key, err %d aq_err %d\n",
				status, hw->adminq.rq_last_status);
			return -EIO;
		}
	}

	if (lut) {
		status = ice_aq_set_rss_lut(hw, vsi->idx, vsi->rss_lut_type,
					    lut, lut_size);
		if (status) {
			dev_err(&pf->pdev->dev,
				"Cannot set RSS lut, err %d aq_err %d\n",
				status, hw->adminq.rq_last_status);
			return -EIO;
		}
	}

	return 0;
}

/**
 * ice_get_rss - Get RSS keys and lut
 * @vsi: Pointer to VSI structure
 * @seed: Buffer to store the keys
 * @lut: Buffer to store the lookup table entries
 * @lut_size: Size of buffer to store the lookup table entries
 *
 * Returns 0 on success, negative on failure
 */
int ice_get_rss(struct ice_vsi *vsi, u8 *seed, u8 *lut, u16 lut_size)
{
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;

	if (seed) {
		struct ice_aqc_get_set_rss_keys *buf =
				  (struct ice_aqc_get_set_rss_keys *)seed;

		status = ice_aq_get_rss_key(hw, vsi->idx, buf);
		if (status) {
			dev_err(&pf->pdev->dev,
				"Cannot get RSS key, err %d aq_err %d\n",
				status, hw->adminq.rq_last_status);
			return -EIO;
		}
	}

	if (lut) {
		status = ice_aq_get_rss_lut(hw, vsi->idx, vsi->rss_lut_type,
					    lut, lut_size);
		if (status) {
			dev_err(&pf->pdev->dev,
				"Cannot get RSS lut, err %d aq_err %d\n",
				status, hw->adminq.rq_last_status);
			return -EIO;
		}
	}

	return 0;
}

/**
 * ice_bridge_getlink - Get the hardware bridge mode
 * @skb: skb buff
 * @pid: process id
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
	struct device *dev = &vsi->back->pdev->dev;
	struct ice_aqc_vsi_props *vsi_props;
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx ctxt = { 0 };
	enum ice_status status;

	vsi_props = &vsi->info;
	ctxt.info = vsi->info;

	if (bmode == BRIDGE_MODE_VEB)
		/* change from VEPA to VEB mode */
		ctxt.info.sw_flags |= ICE_AQ_VSI_SW_FLAG_ALLOW_LB;
	else
		/* change from VEB to VEPA mode */
		ctxt.info.sw_flags &= ~ICE_AQ_VSI_SW_FLAG_ALLOW_LB;
	ctxt.info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_SW_VALID);

	status = ice_update_vsi(hw, vsi->idx, &ctxt, NULL);
	if (status) {
		dev_err(dev, "update VSI for bridge mode failed, bmode = %d err %d aq_err %d\n",
			bmode, status, hw->adminq.sq_last_status);
		return -EIO;
	}
	/* Update sw flags for book keeping */
	vsi_props->sw_flags = ctxt.info.sw_flags;

	return 0;
}

/**
 * ice_bridge_setlink - Set the hardware bridge mode
 * @dev: the netdev being configured
 * @nlh: RTNL message
 * @flags: bridge setlink flags
 *
 * Sets the bridge mode (VEB/VEPA) of the switch to which the netdev (VSI) is
 * hooked up to. Iterates through the PF VSI list and sets the loopback mode (if
 * not already set for all VSIs connected to this switch. And also update the
 * unicast switch filter rules for the corresponding switch of the netdev.
 */
static int
ice_bridge_setlink(struct net_device *dev, struct nlmsghdr *nlh,
		   u16 __always_unused flags)
{
	struct ice_netdev_priv *np = netdev_priv(dev);
	struct ice_pf *pf = np->vsi->back;
	struct nlattr *attr, *br_spec;
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	struct ice_sw *pf_sw;
	int rem, v, err = 0;

	pf_sw = pf->first_sw;
	/* find the attribute in the netlink message */
	br_spec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg), IFLA_AF_SPEC);

	nla_for_each_nested(attr, br_spec, rem) {
		__u16 mode;

		if (nla_type(attr) != IFLA_BRIDGE_MODE)
			continue;
		mode = nla_get_u16(attr);
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
		status = ice_update_sw_rule_bridge_mode(hw);
		if (status) {
			netdev_err(dev, "update SW_RULE for bridge mode failed,  = %d err %d aq_err %d\n",
				   mode, status, hw->adminq.sq_last_status);
			/* revert hw->evb_veb */
			hw->evb_veb = (pf_sw->bridge_mode == BRIDGE_MODE_VEB);
			return -EIO;
		}

		pf_sw->bridge_mode = mode;
	}

	return 0;
}

/**
 * ice_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 */
static void ice_tx_timeout(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_ring *tx_ring = NULL;
	struct ice_vsi *vsi = np->vsi;
	struct ice_pf *pf = vsi->back;
	int hung_queue = -1;
	u32 i;

	pf->tx_timeout_count++;

	/* find the stopped queue the same way the stack does */
	for (i = 0; i < netdev->num_tx_queues; i++) {
		struct netdev_queue *q;
		unsigned long trans_start;

		q = netdev_get_tx_queue(netdev, i);
		trans_start = q->trans_start;
		if (netif_xmit_stopped(q) &&
		    time_after(jiffies,
			       (trans_start + netdev->watchdog_timeo))) {
			hung_queue = i;
			break;
		}
	}

	if (i == netdev->num_tx_queues) {
		netdev_info(netdev, "tx_timeout: no netdev hung queue found\n");
	} else {
		/* now that we have an index, find the tx_ring struct */
		for (i = 0; i < vsi->num_txq; i++) {
			if (vsi->tx_rings[i] && vsi->tx_rings[i]->desc) {
				if (hung_queue ==
				    vsi->tx_rings[i]->q_index) {
					tx_ring = vsi->tx_rings[i];
					break;
				}
			}
		}
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

		head = (rd32(hw, QTX_COMM_HEAD(vsi->txq_map[hung_queue])) &
			QTX_COMM_HEAD_HEAD_M) >> QTX_COMM_HEAD_HEAD_S;
		/* Read interrupt register */
		if (test_bit(ICE_FLAG_MSIX_ENA, pf->flags))
			val = rd32(hw,
				   GLINT_DYN_CTL(tx_ring->q_vector->v_idx +
					tx_ring->vsi->hw_base_vector));

		netdev_info(netdev, "tx_timeout: VSI_num: %d, Q %d, NTC: 0x%x, HW_HEAD: 0x%x, NTU: 0x%x, INT: 0x%x\n",
			    vsi->vsi_num, hung_queue, tx_ring->next_to_clean,
			    head, tx_ring->next_to_use, val);
	}

	pf->tx_timeout_last_recovery = jiffies;
	netdev_info(netdev, "tx_timeout recovery level %d, hung_queue %d\n",
		    pf->tx_timeout_recovery_level, hung_queue);

	switch (pf->tx_timeout_recovery_level) {
	case 1:
		set_bit(__ICE_PFR_REQ, pf->state);
		break;
	case 2:
		set_bit(__ICE_CORER_REQ, pf->state);
		break;
	case 3:
		set_bit(__ICE_GLOBR_REQ, pf->state);
		break;
	default:
		netdev_err(netdev, "tx_timeout recovery unsuccessful, device is in unrecoverable state.\n");
		set_bit(__ICE_DOWN, pf->state);
		set_bit(__ICE_NEEDS_RESTART, vsi->state);
		set_bit(__ICE_SERVICE_DIS, pf->state);
		break;
	}

	ice_service_task_schedule(pf);
	pf->tx_timeout_recovery_level++;
}

/**
 * ice_open - Called when a network interface becomes active
 * @netdev: network interface device structure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the netdev watchdog is enabled,
 * and the stack is notified that the interface is ready.
 *
 * Returns 0 on success, negative value on failure
 */
static int ice_open(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	int err;

	if (test_bit(__ICE_NEEDS_RESTART, vsi->back->state)) {
		netdev_err(netdev, "driver needs to be unloaded and reloaded\n");
		return -EIO;
	}

	netif_carrier_off(netdev);

	err = ice_vsi_open(vsi);

	if (err)
		netdev_err(netdev, "Failed to open VSI 0x%04X on switch 0x%04X\n",
			   vsi->vsi_num, vsi->vsw->sw_id);
	return err;
}

/**
 * ice_stop - Disables a network interface
 * @netdev: network interface device structure
 *
 * The stop entry point is called when an interface is de-activated by the OS,
 * and the netdevice enters the DOWN state.  The hardware is still under the
 * driver's control, but the netdev interface is disabled.
 *
 * Returns success only - not allowed to fail
 */
static int ice_stop(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;

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
	size_t len;

	/* No point in doing any of this if neither checksum nor GSO are
	 * being requested for this frame.  We can rule out both by just
	 * checking for CHECKSUM_PARTIAL
	 */
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return features;

	/* We cannot support GSO if the MSS is going to be less than
	 * 64 bytes.  If it is then we need to drop support for GSO.
	 */
	if (skb_is_gso(skb) && (skb_shinfo(skb)->gso_size < 64))
		features &= ~NETIF_F_GSO_MASK;

	len = skb_network_header(skb) - skb->data;
	if (len & ~(ICE_TXD_MACLEN_MAX))
		goto out_rm_features;

	len = skb_transport_header(skb) - skb_network_header(skb);
	if (len & ~(ICE_TXD_IPLEN_MAX))
		goto out_rm_features;

	if (skb->encapsulation) {
		len = skb_inner_network_header(skb) - skb_transport_header(skb);
		if (len & ~(ICE_TXD_L4LEN_MAX))
			goto out_rm_features;

		len = skb_inner_transport_header(skb) -
		      skb_inner_network_header(skb);
		if (len & ~(ICE_TXD_IPLEN_MAX))
			goto out_rm_features;
	}

	return features;
out_rm_features:
	return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
}

static const struct net_device_ops ice_netdev_ops = {
	.ndo_open = ice_open,
	.ndo_stop = ice_stop,
	.ndo_start_xmit = ice_start_xmit,
	.ndo_features_check = ice_features_check,
	.ndo_set_rx_mode = ice_set_rx_mode,
	.ndo_set_mac_address = ice_set_mac_address,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_change_mtu = ice_change_mtu,
	.ndo_get_stats64 = ice_get_stats64,
	.ndo_set_vf_spoofchk = ice_set_vf_spoofchk,
	.ndo_set_vf_mac = ice_set_vf_mac,
	.ndo_get_vf_config = ice_get_vf_cfg,
	.ndo_set_vf_trust = ice_set_vf_trust,
	.ndo_set_vf_vlan = ice_set_vf_port_vlan,
	.ndo_set_vf_link_state = ice_set_vf_link_state,
	.ndo_vlan_rx_add_vid = ice_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = ice_vlan_rx_kill_vid,
	.ndo_set_features = ice_set_features,
	.ndo_bridge_getlink = ice_bridge_getlink,
	.ndo_bridge_setlink = ice_bridge_setlink,
	.ndo_fdb_add = ice_fdb_add,
	.ndo_fdb_del = ice_fdb_del,
	.ndo_tx_timeout = ice_tx_timeout,
};
