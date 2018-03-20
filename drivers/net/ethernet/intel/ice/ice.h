/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_H_
#define _ICE_H_

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/compiler.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/workqueue.h>
#include <linux/aer.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/bitmap.h>
#include <linux/if_bridge.h>
#include "ice_devids.h"
#include "ice_type.h"
#include "ice_txrx.h"
#include "ice_switch.h"
#include "ice_common.h"
#include "ice_sched.h"

#define ICE_BAR0		0
#define ICE_INT_NAME_STR_LEN	(IFNAMSIZ + 16)
#define ICE_AQ_LEN		64
#define ICE_MIN_MSIX		2
#define ICE_MAX_VSI_ALLOC	130
#define ICE_MAX_TXQS		2048
#define ICE_MAX_RXQS		2048
#define ICE_RES_VALID_BIT	0x8000
#define ICE_RES_MISC_VEC_ID	(ICE_RES_VALID_BIT - 1)

#define ICE_DFLT_NETIF_M (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

struct ice_res_tracker {
	u16 num_entries;
	u16 search_hint;
	u16 list[1];
};

struct ice_sw {
	struct ice_pf *pf;
	u16 sw_id;		/* switch ID for this switch */
	u16 bridge_mode;	/* VEB/VEPA/Port Virtualizer */
};

enum ice_state {
	__ICE_DOWN,
	__ICE_PFR_REQ,			/* set by driver and peers */
	__ICE_ADMINQ_EVENT_PENDING,
	__ICE_SERVICE_SCHED,
	__ICE_STATE_NBITS		/* must be last */
};

/* struct that defines a VSI, associated with a dev */
struct ice_vsi {
	struct net_device *netdev;
	struct ice_port_info *port_info; /* back pointer to port_info */
	u16 vsi_num;			 /* HW (absolute) index of this VSI */
} ____cacheline_internodealigned_in_smp;

enum ice_pf_flags {
	ICE_FLAG_MSIX_ENA,
	ICE_FLAG_FLTR_SYNC,
	ICE_FLAG_RSS_ENA,
	ICE_PF_FLAGS_NBITS		/* must be last */
};

struct ice_pf {
	struct pci_dev *pdev;
	struct msix_entry *msix_entries;
	struct ice_res_tracker *irq_tracker;
	struct ice_vsi **vsi;		/* VSIs created by the driver */
	struct ice_sw *first_sw;	/* first switch created by firmware */
	DECLARE_BITMAP(state, __ICE_STATE_NBITS);
	DECLARE_BITMAP(avail_txqs, ICE_MAX_TXQS);
	DECLARE_BITMAP(avail_rxqs, ICE_MAX_RXQS);
	DECLARE_BITMAP(flags, ICE_PF_FLAGS_NBITS);
	unsigned long serv_tmr_period;
	unsigned long serv_tmr_prev;
	struct timer_list serv_tmr;
	struct work_struct serv_task;
	struct mutex avail_q_mutex;	/* protects access to avail_[rx|tx]qs */
	struct mutex sw_mutex;		/* lock for protecting VSI alloc flow */
	u32 msg_enable;
	u32 oicr_idx;		/* Other interrupt cause vector index */
	u32 num_lan_msix;	/* Total MSIX vectors for base driver */
	u32 num_avail_msix;	/* remaining MSIX vectors left unclaimed */
	u16 num_lan_tx;		/* num lan tx queues setup */
	u16 num_lan_rx;		/* num lan rx queues setup */
	u16 q_left_tx;		/* remaining num tx queues left unclaimed */
	u16 q_left_rx;		/* remaining num rx queues left unclaimed */
	u16 next_vsi;		/* Next free slot in pf->vsi[] - 0-based! */
	u16 num_alloc_vsi;

	struct ice_hw hw;
	char int_name[ICE_INT_NAME_STR_LEN];
};

/**
 * ice_irq_dynamic_ena - Enable default interrupt generation settings
 * @hw: pointer to hw struct
 */
static inline void ice_irq_dynamic_ena(struct ice_hw *hw)
{
	u32 vector = ((struct ice_pf *)hw->back)->oicr_idx;
	int itr = ICE_ITR_NONE;
	u32 val;

	/* clear the PBA here, as this function is meant to clean out all
	 * previous interrupts and enable the interrupt
	 */
	val = GLINT_DYN_CTL_INTENA_M | GLINT_DYN_CTL_CLEARPBA_M |
	      (itr << GLINT_DYN_CTL_ITR_INDX_S);

	wr32(hw, GLINT_DYN_CTL(vector), val);
}
#endif /* _ICE_H_ */
