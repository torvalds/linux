/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_H_
#define _ICE_H_

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/firmware.h>
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
#include <linux/sctp.h>
#include <linux/ipv6.h>
#include <linux/pkt_sched.h>
#include <linux/if_bridge.h>
#include <linux/ctype.h>
#include <linux/bpf.h>
#include <linux/avf/virtchnl.h>
#include <net/ipv6.h>
#include <net/xdp_sock.h>
#include "ice_devids.h"
#include "ice_type.h"
#include "ice_txrx.h"
#include "ice_dcb.h"
#include "ice_switch.h"
#include "ice_common.h"
#include "ice_sched.h"
#include "ice_virtchnl_pf.h"
#include "ice_sriov.h"
#include "ice_xsk.h"

extern const char ice_drv_ver[];
#define ICE_BAR0		0
#define ICE_REQ_DESC_MULTIPLE	32
#define ICE_MIN_NUM_DESC	64
#define ICE_MAX_NUM_DESC	8160
#define ICE_DFLT_MIN_RX_DESC	512
#define ICE_DFLT_NUM_TX_DESC	256
#define ICE_DFLT_NUM_RX_DESC	2048

#define ICE_DFLT_TRAFFIC_CLASS	BIT(0)
#define ICE_INT_NAME_STR_LEN	(IFNAMSIZ + 16)
#define ICE_AQ_LEN		64
#define ICE_MBXSQ_LEN		64
#define ICE_MBXRQ_LEN		512
#define ICE_MIN_MSIX		2
#define ICE_NO_VSI		0xffff
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
#define ICE_INVAL_VFID		256

#define ICE_MAX_RESET_WAIT		20

#define ICE_VSIQF_HKEY_ARRAY_SIZE	((VSIQF_HKEY_MAX_INDEX + 1) *	4)

#define ICE_DFLT_NETIF_M (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

#define ICE_MAX_MTU	(ICE_AQ_SET_MAC_FRAME_SIZE_MAX - ICE_ETH_PKT_HDR_PAD)

#define ICE_UP_TABLE_TRANSLATE(val, i) \
		(((val) << ICE_AQ_VSI_UP_TABLE_UP##i##_S) & \
		  ICE_AQ_VSI_UP_TABLE_UP##i##_M)

#define ICE_TX_DESC(R, i) (&(((struct ice_tx_desc *)((R)->desc))[i]))
#define ICE_RX_DESC(R, i) (&(((union ice_32b_rx_flex_desc *)((R)->desc))[i]))
#define ICE_TX_CTX_DESC(R, i) (&(((struct ice_tx_ctx_desc *)((R)->desc))[i]))

/* Macro for each VSI in a PF */
#define ice_for_each_vsi(pf, i) \
	for ((i) = 0; (i) < (pf)->num_alloc_vsi; (i)++)

/* Macros for each Tx/Rx ring in a VSI */
#define ice_for_each_txq(vsi, i) \
	for ((i) = 0; (i) < (vsi)->num_txq; (i)++)

#define ice_for_each_rxq(vsi, i) \
	for ((i) = 0; (i) < (vsi)->num_rxq; (i)++)

/* Macros for each allocated Tx/Rx ring whether used or not in a VSI */
#define ice_for_each_alloc_txq(vsi, i) \
	for ((i) = 0; (i) < (vsi)->alloc_txq; (i)++)

#define ice_for_each_alloc_rxq(vsi, i) \
	for ((i) = 0; (i) < (vsi)->alloc_rxq; (i)++)

#define ice_for_each_q_vector(vsi, i) \
	for ((i) = 0; (i) < (vsi)->num_q_vectors; (i)++)

#define ICE_UCAST_PROMISC_BITS (ICE_PROMISC_UCAST_TX | ICE_PROMISC_MCAST_TX | \
				ICE_PROMISC_UCAST_RX | ICE_PROMISC_MCAST_RX)

#define ICE_UCAST_VLAN_PROMISC_BITS (ICE_PROMISC_UCAST_TX | \
				     ICE_PROMISC_MCAST_TX | \
				     ICE_PROMISC_UCAST_RX | \
				     ICE_PROMISC_MCAST_RX | \
				     ICE_PROMISC_VLAN_TX  | \
				     ICE_PROMISC_VLAN_RX)

#define ICE_MCAST_PROMISC_BITS (ICE_PROMISC_MCAST_TX | ICE_PROMISC_MCAST_RX)

#define ICE_MCAST_VLAN_PROMISC_BITS (ICE_PROMISC_MCAST_TX | \
				     ICE_PROMISC_MCAST_RX | \
				     ICE_PROMISC_VLAN_TX  | \
				     ICE_PROMISC_VLAN_RX)

#define ice_pf_to_dev(pf) (&((pf)->pdev->dev))

struct ice_txq_meta {
	u32 q_teid;	/* Tx-scheduler element identifier */
	u16 q_id;	/* Entry in VSI's txq_map bitmap */
	u16 q_handle;	/* Relative index of Tx queue within TC */
	u16 vsi_idx;	/* VSI index that Tx queue belongs to */
	u8 tc;		/* TC number that Tx queue belongs to */
};

struct ice_tc_info {
	u16 qoffset;
	u16 qcount_tx;
	u16 qcount_rx;
	u8 netdev_tc;
};

struct ice_tc_cfg {
	u8 numtc; /* Total number of enabled TCs */
	u8 ena_tc; /* Tx map */
	struct ice_tc_info tc_info[ICE_MAX_TRAFFIC_CLASS];
};

struct ice_res_tracker {
	u16 num_entries;
	u16 end;
	u16 list[1];
};

struct ice_qs_cfg {
	struct mutex *qs_mutex;  /* will be assigned to &pf->avail_q_mutex */
	unsigned long *pf_map;
	unsigned long pf_map_size;
	unsigned int q_count;
	unsigned int scatter_count;
	u16 *vsi_map;
	u16 vsi_map_offset;
	u8 mapping_mode;
};

struct ice_sw {
	struct ice_pf *pf;
	u16 sw_id;		/* switch ID for this switch */
	u16 bridge_mode;	/* VEB/VEPA/Port Virtualizer */
	struct ice_vsi *dflt_vsi;	/* default VSI for this switch */
	u8 dflt_vsi_ena:1;	/* true if above dflt_vsi is enabled */
};

enum ice_state {
	__ICE_TESTING,
	__ICE_DOWN,
	__ICE_NEEDS_RESTART,
	__ICE_PREPARED_FOR_RESET,	/* set by driver when prepared */
	__ICE_RESET_OICR_RECV,		/* set by driver after rcv reset OICR */
	__ICE_DCBNL_DEVRESET,		/* set by dcbnl devreset */
	__ICE_PFR_REQ,			/* set by driver and peers */
	__ICE_CORER_REQ,		/* set by driver and peers */
	__ICE_GLOBR_REQ,		/* set by driver and peers */
	__ICE_CORER_RECV,		/* set by OICR handler */
	__ICE_GLOBR_RECV,		/* set by OICR handler */
	__ICE_EMPR_RECV,		/* set by OICR handler */
	__ICE_SUSPENDED,		/* set on module remove path */
	__ICE_RESET_FAILED,		/* set by reset/rebuild */
	/* When checking for the PF to be in a nominal operating state, the
	 * bits that are grouped at the beginning of the list need to be
	 * checked. Bits occurring before __ICE_STATE_NOMINAL_CHECK_BITS will
	 * be checked. If you need to add a bit into consideration for nominal
	 * operating state, it must be added before
	 * __ICE_STATE_NOMINAL_CHECK_BITS. Do not move this entry's position
	 * without appropriate consideration.
	 */
	__ICE_STATE_NOMINAL_CHECK_BITS,
	__ICE_ADMINQ_EVENT_PENDING,
	__ICE_MAILBOXQ_EVENT_PENDING,
	__ICE_MDD_EVENT_PENDING,
	__ICE_VFLR_EVENT_PENDING,
	__ICE_FLTR_OVERFLOW_PROMISC,
	__ICE_VF_DIS,
	__ICE_CFG_BUSY,
	__ICE_SERVICE_SCHED,
	__ICE_SERVICE_DIS,
	__ICE_OICR_INTR_DIS,		/* Global OICR interrupt disabled */
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
	struct ice_ring **rx_rings;	 /* Rx ring array */
	struct ice_ring **tx_rings;	 /* Tx ring array */
	struct ice_q_vector **q_vectors; /* q_vector array */

	irqreturn_t (*irq_handler)(int irq, void *data);

	u64 tx_linearize;
	DECLARE_BITMAP(state, __ICE_STATE_NBITS);
	DECLARE_BITMAP(flags, ICE_VSI_FLAG_NBITS);
	unsigned int current_netdev_flags;
	u32 tx_restart;
	u32 tx_busy;
	u32 rx_buf_failed;
	u32 rx_page_failed;
	int num_q_vectors;
	int base_vector;		/* IRQ base for OS reserved vectors */
	enum ice_vsi_type type;
	u16 vsi_num;			/* HW (absolute) index of this VSI */
	u16 idx;			/* software index in pf->vsi[] */

	s16 vf_id;			/* VF ID for SR-IOV VSIs */

	u16 ethtype;			/* Ethernet protocol for pause frame */

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

	u8 irqs_ready:1;
	u8 current_isup:1;		 /* Sync 'link up' logging */
	u8 stat_offsets_loaded:1;
	u8 vlan_ena:1;
	u16 num_vlan;

	/* queue information */
	u8 tx_mapping_mode;		 /* ICE_MAP_MODE_[CONTIG|SCATTER] */
	u8 rx_mapping_mode;		 /* ICE_MAP_MODE_[CONTIG|SCATTER] */
	u16 *txq_map;			 /* index in pf->avail_txqs */
	u16 *rxq_map;			 /* index in pf->avail_rxqs */
	u16 alloc_txq;			 /* Allocated Tx queues */
	u16 num_txq;			 /* Used Tx queues */
	u16 alloc_rxq;			 /* Allocated Rx queues */
	u16 num_rxq;			 /* Used Rx queues */
	u16 req_txq;			 /* User requested Tx queues */
	u16 req_rxq;			 /* User requested Rx queues */
	u16 num_rx_desc;
	u16 num_tx_desc;
	struct ice_tc_cfg tc_cfg;
	struct bpf_prog *xdp_prog;
	struct ice_ring **xdp_rings;	 /* XDP ring array */
	u16 num_xdp_txq;		 /* Used XDP queues */
	u8 xdp_mapping_mode;		 /* ICE_MAP_MODE_[CONTIG|SCATTER] */
	struct xdp_umem **xsk_umems;
	u16 num_xsk_umems_used;
	u16 num_xsk_umems;
} ____cacheline_internodealigned_in_smp;

/* struct that defines an interrupt vector */
struct ice_q_vector {
	struct ice_vsi *vsi;

	u16 v_idx;			/* index in the vsi->q_vector array. */
	u16 reg_idx;
	u8 num_ring_rx;			/* total number of Rx rings in vector */
	u8 num_ring_tx;			/* total number of Tx rings in vector */
	u8 itr_countdown;		/* when 0 should adjust adaptive ITR */
	/* in usecs, need to use ice_intrl_to_usecs_reg() before writing this
	 * value to the device
	 */
	u8 intrl;

	struct napi_struct napi;

	struct ice_ring_container rx;
	struct ice_ring_container tx;

	cpumask_t affinity_mask;
	struct irq_affinity_notify affinity_notify;

	char name[ICE_INT_NAME_STR_LEN];
} ____cacheline_internodealigned_in_smp;

enum ice_pf_flags {
	ICE_FLAG_FLTR_SYNC,
	ICE_FLAG_RSS_ENA,
	ICE_FLAG_SRIOV_ENA,
	ICE_FLAG_SRIOV_CAPABLE,
	ICE_FLAG_DCB_CAPABLE,
	ICE_FLAG_DCB_ENA,
	ICE_FLAG_ADV_FEATURES,
	ICE_FLAG_LINK_DOWN_ON_CLOSE_ENA,
	ICE_FLAG_NO_MEDIA,
	ICE_FLAG_FW_LLDP_AGENT,
	ICE_FLAG_ETHTOOL_CTXT,		/* set when ethtool holds RTNL lock */
	ICE_FLAG_LEGACY_RX,
	ICE_PF_FLAGS_NBITS		/* must be last */
};

struct ice_pf {
	struct pci_dev *pdev;

	/* OS reserved IRQ details */
	struct msix_entry *msix_entries;
	struct ice_res_tracker *irq_tracker;
	/* First MSIX vector used by SR-IOV VFs. Calculated by subtracting the
	 * number of MSIX vectors needed for all SR-IOV VFs from the number of
	 * MSIX vectors allowed on this PF.
	 */
	u16 sriov_base_vector;

	struct ice_vsi **vsi;		/* VSIs created by the driver */
	struct ice_sw *first_sw;	/* first switch created by firmware */
	/* Virtchnl/SR-IOV config info */
	struct ice_vf *vf;
	int num_alloc_vfs;		/* actual number of VFs allocated */
	u16 num_vfs_supported;		/* num VFs supported for this PF */
	u16 num_vf_qps;			/* num queue pairs per VF */
	u16 num_vf_msix;		/* num vectors per VF */
	DECLARE_BITMAP(state, __ICE_STATE_NBITS);
	DECLARE_BITMAP(flags, ICE_PF_FLAGS_NBITS);
	unsigned long *avail_txqs;	/* bitmap to track PF Tx queue usage */
	unsigned long *avail_rxqs;	/* bitmap to track PF Rx queue usage */
	unsigned long serv_tmr_period;
	unsigned long serv_tmr_prev;
	struct timer_list serv_tmr;
	struct work_struct serv_task;
	struct mutex avail_q_mutex;	/* protects access to avail_[rx|tx]qs */
	struct mutex sw_mutex;		/* lock for protecting VSI alloc flow */
	struct mutex tc_mutex;		/* lock to protect TC changes */
	u32 msg_enable;
	u32 hw_csum_rx_error;
	u32 oicr_idx;		/* Other interrupt cause MSIX vector index */
	u32 num_avail_sw_msix;	/* remaining MSIX SW vectors left unclaimed */
	u16 max_pf_txqs;	/* Total Tx queues PF wide */
	u16 max_pf_rxqs;	/* Total Rx queues PF wide */
	u32 num_lan_msix;	/* Total MSIX vectors for base driver */
	u16 num_lan_tx;		/* num LAN Tx queues setup */
	u16 num_lan_rx;		/* num LAN Rx queues setup */
	u16 next_vsi;		/* Next free slot in pf->vsi[] - 0-based! */
	u16 num_alloc_vsi;
	u16 corer_count;	/* Core reset count */
	u16 globr_count;	/* Global reset count */
	u16 empr_count;		/* EMP reset count */
	u16 pfr_count;		/* PF reset count */

	struct ice_hw_port_stats stats;
	struct ice_hw_port_stats stats_prev;
	struct ice_hw hw;
	u8 stat_prev_loaded:1; /* has previous stats been loaded */
#ifdef CONFIG_DCB
	u16 dcbx_cap;
#endif /* CONFIG_DCB */
	u32 tx_timeout_count;
	unsigned long tx_timeout_last_recovery;
	u32 tx_timeout_recovery_level;
	char int_name[ICE_INT_NAME_STR_LEN];
	u32 sw_int_count;
};

struct ice_netdev_priv {
	struct ice_vsi *vsi;
};

/**
 * ice_irq_dynamic_ena - Enable default interrupt generation settings
 * @hw: pointer to HW struct
 * @vsi: pointer to VSI struct, can be NULL
 * @q_vector: pointer to q_vector, can be NULL
 */
static inline void
ice_irq_dynamic_ena(struct ice_hw *hw, struct ice_vsi *vsi,
		    struct ice_q_vector *q_vector)
{
	u32 vector = (vsi && q_vector) ? q_vector->reg_idx :
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

/**
 * ice_netdev_to_pf - Retrieve the PF struct associated with a netdev
 * @netdev: pointer to the netdev struct
 */
static inline struct ice_pf *ice_netdev_to_pf(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);

	return np->vsi->back;
}

static inline bool ice_is_xdp_ena_vsi(struct ice_vsi *vsi)
{
	return !!vsi->xdp_prog;
}

static inline void ice_set_ring_xdp(struct ice_ring *ring)
{
	ring->flags |= ICE_TX_FLAGS_RING_XDP;
}

/**
 * ice_xsk_umem - get XDP UMEM bound to a ring
 * @ring - ring to use
 *
 * Returns a pointer to xdp_umem structure if there is an UMEM present,
 * NULL otherwise.
 */
static inline struct xdp_umem *ice_xsk_umem(struct ice_ring *ring)
{
	struct xdp_umem **umems = ring->vsi->xsk_umems;
	int qid = ring->q_index;

	if (ice_ring_is_xdp(ring))
		qid -= ring->vsi->num_xdp_txq;

	if (!umems || !umems[qid] || !ice_is_xdp_ena_vsi(ring->vsi))
		return NULL;

	return umems[qid];
}

/**
 * ice_get_main_vsi - Get the PF VSI
 * @pf: PF instance
 *
 * returns pf->vsi[0], which by definition is the PF VSI
 */
static inline struct ice_vsi *ice_get_main_vsi(struct ice_pf *pf)
{
	if (pf->vsi)
		return pf->vsi[0];

	return NULL;
}

int ice_vsi_setup_tx_rings(struct ice_vsi *vsi);
int ice_vsi_setup_rx_rings(struct ice_vsi *vsi);
void ice_set_ethtool_ops(struct net_device *netdev);
void ice_set_ethtool_safe_mode_ops(struct net_device *netdev);
u16 ice_get_avail_txq_count(struct ice_pf *pf);
u16 ice_get_avail_rxq_count(struct ice_pf *pf);
int ice_vsi_recfg_qs(struct ice_vsi *vsi, int new_rx, int new_tx);
void ice_update_vsi_stats(struct ice_vsi *vsi);
void ice_update_pf_stats(struct ice_pf *pf);
int ice_up(struct ice_vsi *vsi);
int ice_down(struct ice_vsi *vsi);
int ice_vsi_cfg(struct ice_vsi *vsi);
struct ice_vsi *ice_lb_vsi_setup(struct ice_pf *pf, struct ice_port_info *pi);
int ice_prepare_xdp_rings(struct ice_vsi *vsi, struct bpf_prog *prog);
int ice_destroy_xdp_rings(struct ice_vsi *vsi);
int
ice_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **frames,
	     u32 flags);
int ice_set_rss(struct ice_vsi *vsi, u8 *seed, u8 *lut, u16 lut_size);
int ice_get_rss(struct ice_vsi *vsi, u8 *seed, u8 *lut, u16 lut_size);
void ice_fill_rss_lut(u8 *lut, u16 rss_table_size, u16 rss_size);
int ice_schedule_reset(struct ice_pf *pf, enum ice_reset_req reset);
void ice_print_link_msg(struct ice_vsi *vsi, bool isup);
int ice_open(struct net_device *netdev);
int ice_stop(struct net_device *netdev);

#endif /* _ICE_H_ */
