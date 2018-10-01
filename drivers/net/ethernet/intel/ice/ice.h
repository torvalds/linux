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
#include <linux/skbuff.h>
#include <linux/cpumask.h>
#include <linux/rtnetlink.h>
#include <linux/if_vlan.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/workqueue.h>
#include <linux/aer.h>
#include <linux/interrupt.h>
#include <linux/ethtool.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/bitmap.h>
#include <linux/log2.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/if_bridge.h>
#include <net/ipv6.h>
#include "ice_devids.h"
#include "ice_type.h"
#include "ice_txrx.h"
#include "ice_switch.h"
#include "ice_common.h"
#include "ice_sched.h"

extern const char ice_drv_ver[];
#define ICE_BAR0		0
#define ICE_DFLT_NUM_DESC	128
#define ICE_MIN_NUM_DESC	8
#define ICE_MAX_NUM_DESC	8160
#define ICE_REQ_DESC_MULTIPLE	32
#define ICE_DFLT_TRAFFIC_CLASS	BIT(0)
#define ICE_INT_NAME_STR_LEN	(IFNAMSIZ + 16)
#define ICE_ETHTOOL_FWVER_LEN	32
#define ICE_AQ_LEN		64
#define ICE_MIN_MSIX		2
#define ICE_NO_VSI		0xffff
#define ICE_MAX_VSI_ALLOC	130
#define ICE_MAX_TXQS		2048
#define ICE_MAX_RXQS		2048
#define ICE_VSI_MAP_CONTIG	0
#define ICE_VSI_MAP_SCATTER	1
#define ICE_MAX_SCATTER_TXQS	16
#define ICE_MAX_SCATTER_RXQS	16
#define ICE_Q_WAIT_RETRY_LIMIT	10
#define ICE_Q_WAIT_MAX_RETRY	(5 * ICE_Q_WAIT_RETRY_LIMIT)
#define ICE_MAX_LG_RSS_QS	256
#define ICE_MAX_SMALL_RSS_QS	8
#define ICE_RES_VALID_BIT	0x8000
#define ICE_RES_MISC_VEC_ID	(ICE_RES_VALID_BIT - 1)
#define ICE_INVAL_Q_INDEX	0xffff

#define ICE_VSIQF_HKEY_ARRAY_SIZE	((VSIQF_HKEY_MAX_INDEX + 1) *	4)

#define ICE_DFLT_NETIF_M (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

#define ICE_MAX_MTU	(ICE_AQ_SET_MAC_FRAME_SIZE_MAX - \
			 ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN)

#define ICE_UP_TABLE_TRANSLATE(val, i) \
		(((val) << ICE_AQ_VSI_UP_TABLE_UP##i##_S) & \
		  ICE_AQ_VSI_UP_TABLE_UP##i##_M)

#define ICE_TX_DESC(R, i) (&(((struct ice_tx_desc *)((R)->desc))[i]))
#define ICE_RX_DESC(R, i) (&(((union ice_32b_rx_flex_desc *)((R)->desc))[i]))
#define ICE_TX_CTX_DESC(R, i) (&(((struct ice_tx_ctx_desc *)((R)->desc))[i]))

/* Macro for each VSI in a PF */
#define ice_for_each_vsi(pf, i) \
	for ((i) = 0; (i) < (pf)->num_alloc_vsi; (i)++)

/* Macros for each tx/rx ring in a VSI */
#define ice_for_each_txq(vsi, i) \
	for ((i) = 0; (i) < (vsi)->num_txq; (i)++)

#define ice_for_each_rxq(vsi, i) \
	for ((i) = 0; (i) < (vsi)->num_rxq; (i)++)

/* Macros for each allocated tx/rx ring whether used or not in a VSI */
#define ice_for_each_alloc_txq(vsi, i) \
	for ((i) = 0; (i) < (vsi)->alloc_txq; (i)++)

#define ice_for_each_alloc_rxq(vsi, i) \
	for ((i) = 0; (i) < (vsi)->alloc_rxq; (i)++)

struct ice_tc_info {
	u16 qoffset;
	u16 qcount;
};

struct ice_tc_cfg {
	u8 numtc; /* Total number of enabled TCs */
	u8 ena_tc; /* TX map */
	struct ice_tc_info tc_info[ICE_MAX_TRAFFIC_CLASS];
};

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
	__ICE_NEEDS_RESTART,
	__ICE_RESET_RECOVERY_PENDING,	/* set by driver when reset starts */
	__ICE_PFR_REQ,			/* set by driver and peers */
	__ICE_CORER_REQ,		/* set by driver and peers */
	__ICE_GLOBR_REQ,		/* set by driver and peers */
	__ICE_CORER_RECV,		/* set by OICR handler */
	__ICE_GLOBR_RECV,		/* set by OICR handler */
	__ICE_EMPR_RECV,		/* set by OICR handler */
	__ICE_SUSPENDED,		/* set on module remove path */
	__ICE_RESET_FAILED,		/* set by reset/rebuild */
	__ICE_ADMINQ_EVENT_PENDING,
	__ICE_FLTR_OVERFLOW_PROMISC,
	__ICE_CFG_BUSY,
	__ICE_SERVICE_SCHED,
	__ICE_STATE_NBITS		/* must be last */
};

enum ice_vsi_flags {
	ICE_VSI_FLAG_UMAC_FLTR_CHANGED,
	ICE_VSI_FLAG_MMAC_FLTR_CHANGED,
	ICE_VSI_FLAG_VLAN_FLTR_CHANGED,
	ICE_VSI_FLAG_PROMISC_CHANGED,
	ICE_VSI_FLAG_NBITS		/* must be last */
};

/* struct that defines a VSI, associated with a dev */
struct ice_vsi {
	struct net_device *netdev;
	struct ice_sw *vsw;		 /* switch this VSI is on */
	struct ice_pf *back;		 /* back pointer to PF */
	struct ice_port_info *port_info; /* back pointer to port_info */
	struct ice_ring **rx_rings;	 /* rx ring array */
	struct ice_ring **tx_rings;	 /* tx ring array */
	struct ice_q_vector **q_vectors; /* q_vector array */

	irqreturn_t (*irq_handler)(int irq, void *data);

	u64 tx_linearize;
	DECLARE_BITMAP(state, __ICE_STATE_NBITS);
	DECLARE_BITMAP(flags, ICE_VSI_FLAG_NBITS);
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	unsigned int current_netdev_flags;
	u32 tx_restart;
	u32 tx_busy;
	u32 rx_buf_failed;
	u32 rx_page_failed;
	int num_q_vectors;
	int base_vector;
	enum ice_vsi_type type;
	u16 vsi_num;			 /* HW (absolute) index of this VSI */
	u16 idx;			 /* software index in pf->vsi[] */

	/* Interrupt thresholds */
	u16 work_lmt;

	/* RSS config */
	u16 rss_table_size;	/* HW RSS table size */
	u16 rss_size;		/* Allocated RSS queues */
	u8 *rss_hkey_user;	/* User configured hash keys */
	u8 *rss_lut_user;	/* User configured lookup table entries */
	u8 rss_lut_type;	/* used to configure Get/Set RSS LUT AQ call */

	u16 max_frame;
	u16 rx_buf_len;

	struct ice_aqc_vsi_props info;	 /* VSI properties */

	/* VSI stats */
	struct rtnl_link_stats64 net_stats;
	struct ice_eth_stats eth_stats;
	struct ice_eth_stats eth_stats_prev;

	struct list_head tmp_sync_list;		/* MAC filters to be synced */
	struct list_head tmp_unsync_list;	/* MAC filters to be unsynced */

	u8 irqs_ready;
	u8 current_isup;		 /* Sync 'link up' logging */
	u8 stat_offsets_loaded;

	/* queue information */
	u8 tx_mapping_mode;		 /* ICE_MAP_MODE_[CONTIG|SCATTER] */
	u8 rx_mapping_mode;		 /* ICE_MAP_MODE_[CONTIG|SCATTER] */
	u16 txq_map[ICE_MAX_TXQS];	 /* index in pf->avail_txqs */
	u16 rxq_map[ICE_MAX_RXQS];	 /* index in pf->avail_rxqs */
	u16 alloc_txq;			 /* Allocated Tx queues */
	u16 num_txq;			 /* Used Tx queues */
	u16 alloc_rxq;			 /* Allocated Rx queues */
	u16 num_rxq;			 /* Used Rx queues */
	u16 num_desc;
	struct ice_tc_cfg tc_cfg;
} ____cacheline_internodealigned_in_smp;

/* struct that defines an interrupt vector */
struct ice_q_vector {
	struct ice_vsi *vsi;
	cpumask_t affinity_mask;
	struct napi_struct napi;
	struct ice_ring_container rx;
	struct ice_ring_container tx;
	struct irq_affinity_notify affinity_notify;
	u16 v_idx;			/* index in the vsi->q_vector array. */
	u8 num_ring_tx;			/* total number of tx rings in vector */
	u8 num_ring_rx;			/* total number of rx rings in vector */
	char name[ICE_INT_NAME_STR_LEN];
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
	u32 hw_csum_rx_error;
	u32 oicr_idx;		/* Other interrupt cause vector index */
	u32 num_lan_msix;	/* Total MSIX vectors for base driver */
	u32 num_avail_msix;	/* remaining MSIX vectors left unclaimed */
	u16 num_lan_tx;		/* num lan tx queues setup */
	u16 num_lan_rx;		/* num lan rx queues setup */
	u16 q_left_tx;		/* remaining num tx queues left unclaimed */
	u16 q_left_rx;		/* remaining num rx queues left unclaimed */
	u16 next_vsi;		/* Next free slot in pf->vsi[] - 0-based! */
	u16 num_alloc_vsi;
	u16 corer_count;	/* Core reset count */
	u16 globr_count;	/* Global reset count */
	u16 empr_count;		/* EMP reset count */
	u16 pfr_count;		/* PF reset count */

	struct ice_hw_port_stats stats;
	struct ice_hw_port_stats stats_prev;
	struct ice_hw hw;
	u8 stat_prev_loaded;	/* has previous stats been loaded */
	char int_name[ICE_INT_NAME_STR_LEN];
};

struct ice_netdev_priv {
	struct ice_vsi *vsi;
};

/**
 * ice_irq_dynamic_ena - Enable default interrupt generation settings
 * @hw: pointer to hw struct
 * @vsi: pointer to vsi struct, can be NULL
 * @q_vector: pointer to q_vector, can be NULL
 */
static inline void ice_irq_dynamic_ena(struct ice_hw *hw, struct ice_vsi *vsi,
				       struct ice_q_vector *q_vector)
{
	u32 vector = (vsi && q_vector) ? vsi->base_vector + q_vector->v_idx :
					((struct ice_pf *)hw->back)->oicr_idx;
	int itr = ICE_ITR_NONE;
	u32 val;

	/* clear the PBA here, as this function is meant to clean out all
	 * previous interrupts and enable the interrupt
	 */
	val = GLINT_DYN_CTL_INTENA_M | GLINT_DYN_CTL_CLEARPBA_M |
	      (itr << GLINT_DYN_CTL_ITR_INDX_S);
	if (vsi)
		if (test_bit(__ICE_DOWN, vsi->state))
			return;
	wr32(hw, GLINT_DYN_CTL(vector), val);
}

static inline void ice_vsi_set_tc_cfg(struct ice_vsi *vsi)
{
	vsi->tc_cfg.ena_tc =  ICE_DFLT_TRAFFIC_CLASS;
	vsi->tc_cfg.numtc = 1;
}

void ice_set_ethtool_ops(struct net_device *netdev);
int ice_up(struct ice_vsi *vsi);
int ice_down(struct ice_vsi *vsi);
int ice_set_rss(struct ice_vsi *vsi, u8 *seed, u8 *lut, u16 lut_size);
int ice_get_rss(struct ice_vsi *vsi, u8 *seed, u8 *lut, u16 lut_size);
void ice_fill_rss_lut(u8 *lut, u16 rss_table_size, u16 rss_size);
void ice_print_link_msg(struct ice_vsi *vsi, bool isup);

#endif /* _ICE_H_ */
