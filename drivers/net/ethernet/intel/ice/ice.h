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
#include <linux/wait.h>
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
#include <linux/btf.h>
#include <linux/auxiliary_bus.h>
#include <linux/avf/virtchnl.h>
#include <linux/cpu_rmap.h>
#include <linux/dim.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_mirred.h>
#include <net/tc_act/tc_gact.h>
#include <net/ip.h>
#include <net/devlink.h>
#include <net/ipv6.h>
#include <net/xdp_sock.h>
#include <net/xdp_sock_drv.h>
#include <net/geneve.h>
#include <net/gre.h>
#include <net/udp_tunnel.h>
#include <net/vxlan.h>
#if IS_ENABLED(CONFIG_DCB)
#include <scsi/iscsi_proto.h>
#endif /* CONFIG_DCB */
#include "ice_devids.h"
#include "ice_type.h"
#include "ice_txrx.h"
#include "ice_dcb.h"
#include "ice_switch.h"
#include "ice_common.h"
#include "ice_flow.h"
#include "ice_sched.h"
#include "ice_idc_int.h"
#include "ice_virtchnl_pf.h"
#include "ice_sriov.h"
#include "ice_ptp.h"
#include "ice_fdir.h"
#include "ice_xsk.h"
#include "ice_arfs.h"
#include "ice_repr.h"
#include "ice_eswitch.h"
#include "ice_lag.h"

#define ICE_BAR0		0
#define ICE_REQ_DESC_MULTIPLE	32
#define ICE_MIN_NUM_DESC	64
#define ICE_MAX_NUM_DESC	8160
#define ICE_DFLT_MIN_RX_DESC	512
#define ICE_DFLT_NUM_TX_DESC	256
#define ICE_DFLT_NUM_RX_DESC	2048

#define ICE_DFLT_TRAFFIC_CLASS	BIT(0)
#define ICE_INT_NAME_STR_LEN	(IFNAMSIZ + 16)
#define ICE_AQ_LEN		192
#define ICE_MBXSQ_LEN		64
#define ICE_SBQ_LEN		64
#define ICE_MIN_LAN_TXRX_MSIX	1
#define ICE_MIN_LAN_OICR_MSIX	1
#define ICE_MIN_MSIX		(ICE_MIN_LAN_TXRX_MSIX + ICE_MIN_LAN_OICR_MSIX)
#define ICE_FDIR_MSIX		2
#define ICE_RDMA_NUM_AEQ_MSIX	4
#define ICE_MIN_RDMA_MSIX	2
#define ICE_ESWITCH_MSIX	1
#define ICE_NO_VSI		0xffff
#define ICE_VSI_MAP_CONTIG	0
#define ICE_VSI_MAP_SCATTER	1
#define ICE_MAX_SCATTER_TXQS	16
#define ICE_MAX_SCATTER_RXQS	16
#define ICE_Q_WAIT_RETRY_LIMIT	10
#define ICE_Q_WAIT_MAX_RETRY	(5 * ICE_Q_WAIT_RETRY_LIMIT)
#define ICE_MAX_LG_RSS_QS	256
#define ICE_RES_VALID_BIT	0x8000
#define ICE_RES_MISC_VEC_ID	(ICE_RES_VALID_BIT - 1)
#define ICE_RES_RDMA_VEC_ID	(ICE_RES_MISC_VEC_ID - 1)
/* All VF control VSIs share the same IRQ, so assign a unique ID for them */
#define ICE_RES_VF_CTRL_VEC_ID	(ICE_RES_RDMA_VEC_ID - 1)
#define ICE_INVAL_Q_INDEX	0xffff
#define ICE_INVAL_VFID		256

#define ICE_MAX_RXQS_PER_TC		256	/* Used when setting VSI context per TC Rx queues */

#define ICE_CHNL_START_TC		1

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
#define ICE_TX_FDIRDESC(R, i) (&(((struct ice_fltr_desc *)((R)->desc))[i]))

/* Minimum BW limit is 500 Kbps for any scheduler node */
#define ICE_MIN_BW_LIMIT		500
/* User can specify BW in either Kbit/Mbit/Gbit and OS converts it in bytes.
 * use it to convert user specified BW limit into Kbps
 */
#define ICE_BW_KBPS_DIVISOR		125

/* Macro for each VSI in a PF */
#define ice_for_each_vsi(pf, i) \
	for ((i) = 0; (i) < (pf)->num_alloc_vsi; (i)++)

/* Macros for each Tx/Xdp/Rx ring in a VSI */
#define ice_for_each_txq(vsi, i) \
	for ((i) = 0; (i) < (vsi)->num_txq; (i)++)

#define ice_for_each_xdp_txq(vsi, i) \
	for ((i) = 0; (i) < (vsi)->num_xdp_txq; (i)++)

#define ice_for_each_rxq(vsi, i) \
	for ((i) = 0; (i) < (vsi)->num_rxq; (i)++)

/* Macros for each allocated Tx/Rx ring whether used or not in a VSI */
#define ice_for_each_alloc_txq(vsi, i) \
	for ((i) = 0; (i) < (vsi)->alloc_txq; (i)++)

#define ice_for_each_alloc_rxq(vsi, i) \
	for ((i) = 0; (i) < (vsi)->alloc_rxq; (i)++)

#define ice_for_each_q_vector(vsi, i) \
	for ((i) = 0; (i) < (vsi)->num_q_vectors; (i)++)

#define ice_for_each_chnl_tc(i)	\
	for ((i) = ICE_CHNL_START_TC; (i) < ICE_CHNL_MAX_TC; (i)++)

#define ICE_UCAST_PROMISC_BITS (ICE_PROMISC_UCAST_TX | ICE_PROMISC_UCAST_RX)

#define ICE_UCAST_VLAN_PROMISC_BITS (ICE_PROMISC_UCAST_TX | \
				     ICE_PROMISC_UCAST_RX | \
				     ICE_PROMISC_VLAN_TX  | \
				     ICE_PROMISC_VLAN_RX)

#define ICE_MCAST_PROMISC_BITS (ICE_PROMISC_MCAST_TX | ICE_PROMISC_MCAST_RX)

#define ICE_MCAST_VLAN_PROMISC_BITS (ICE_PROMISC_MCAST_TX | \
				     ICE_PROMISC_MCAST_RX | \
				     ICE_PROMISC_VLAN_TX  | \
				     ICE_PROMISC_VLAN_RX)

#define ice_pf_to_dev(pf) (&((pf)->pdev->dev))

enum ice_feature {
	ICE_F_DSCP,
	ICE_F_SMA_CTRL,
	ICE_F_MAX
};

DECLARE_STATIC_KEY_FALSE(ice_xdp_locking_key);

struct ice_channel {
	struct list_head list;
	u8 type;
	u16 sw_id;
	u16 base_q;
	u16 num_rxq;
	u16 num_txq;
	u16 vsi_num;
	u8 ena_tc;
	struct ice_aqc_vsi_props info;
	u64 max_tx_rate;
	u64 min_tx_rate;
	atomic_t num_sb_fltr;
	struct ice_vsi *ch_vsi;
};

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
	u16 ena_tc; /* Tx map */
	struct ice_tc_info tc_info[ICE_MAX_TRAFFIC_CLASS];
};

struct ice_res_tracker {
	u16 num_entries;
	u16 end;
	u16 list[];
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

enum ice_pf_state {
	ICE_TESTING,
	ICE_DOWN,
	ICE_NEEDS_RESTART,
	ICE_PREPARED_FOR_RESET,	/* set by driver when prepared */
	ICE_RESET_OICR_RECV,		/* set by driver after rcv reset OICR */
	ICE_PFR_REQ,		/* set by driver */
	ICE_CORER_REQ,		/* set by driver */
	ICE_GLOBR_REQ,		/* set by driver */
	ICE_CORER_RECV,		/* set by OICR handler */
	ICE_GLOBR_RECV,		/* set by OICR handler */
	ICE_EMPR_RECV,		/* set by OICR handler */
	ICE_SUSPENDED,		/* set on module remove path */
	ICE_RESET_FAILED,		/* set by reset/rebuild */
	/* When checking for the PF to be in a nominal operating state, the
	 * bits that are grouped at the beginning of the list need to be
	 * checked. Bits occurring before ICE_STATE_NOMINAL_CHECK_BITS will
	 * be checked. If you need to add a bit into consideration for nominal
	 * operating state, it must be added before
	 * ICE_STATE_NOMINAL_CHECK_BITS. Do not move this entry's position
	 * without appropriate consideration.
	 */
	ICE_STATE_NOMINAL_CHECK_BITS,
	ICE_ADMINQ_EVENT_PENDING,
	ICE_MAILBOXQ_EVENT_PENDING,
	ICE_SIDEBANDQ_EVENT_PENDING,
	ICE_MDD_EVENT_PENDING,
	ICE_VFLR_EVENT_PENDING,
	ICE_FLTR_OVERFLOW_PROMISC,
	ICE_VF_DIS,
	ICE_CFG_BUSY,
	ICE_SERVICE_SCHED,
	ICE_SERVICE_DIS,
	ICE_FD_FLUSH_REQ,
	ICE_OICR_INTR_DIS,		/* Global OICR interrupt disabled */
	ICE_MDD_VF_PRINT_PENDING,	/* set when MDD event handle */
	ICE_VF_RESETS_DISABLED,	/* disable resets during ice_remove */
	ICE_LINK_DEFAULT_OVERRIDE_PENDING,
	ICE_PHY_INIT_COMPLETE,
	ICE_FD_VF_FLUSH_CTX,		/* set at FD Rx IRQ or timeout */
	ICE_STATE_NBITS		/* must be last */
};

enum ice_vsi_state {
	ICE_VSI_DOWN,
	ICE_VSI_NEEDS_RESTART,
	ICE_VSI_NETDEV_ALLOCD,
	ICE_VSI_NETDEV_REGISTERED,
	ICE_VSI_UMAC_FLTR_CHANGED,
	ICE_VSI_MMAC_FLTR_CHANGED,
	ICE_VSI_VLAN_FLTR_CHANGED,
	ICE_VSI_PROMISC_CHANGED,
	ICE_VSI_STATE_NBITS		/* must be last */
};

/* struct that defines a VSI, associated with a dev */
struct ice_vsi {
	struct net_device *netdev;
	struct ice_sw *vsw;		 /* switch this VSI is on */
	struct ice_pf *back;		 /* back pointer to PF */
	struct ice_port_info *port_info; /* back pointer to port_info */
	struct ice_rx_ring **rx_rings;	 /* Rx ring array */
	struct ice_tx_ring **tx_rings;	 /* Tx ring array */
	struct ice_q_vector **q_vectors; /* q_vector array */

	irqreturn_t (*irq_handler)(int irq, void *data);

	u64 tx_linearize;
	DECLARE_BITMAP(state, ICE_VSI_STATE_NBITS);
	unsigned int current_netdev_flags;
	u32 tx_restart;
	u32 tx_busy;
	u32 rx_buf_failed;
	u32 rx_page_failed;
	u16 num_q_vectors;
	u16 base_vector;		/* IRQ base for OS reserved vectors */
	enum ice_vsi_type type;
	u16 vsi_num;			/* HW (absolute) index of this VSI */
	u16 idx;			/* software index in pf->vsi[] */

	s16 vf_id;			/* VF ID for SR-IOV VSIs */

	u16 ethtype;			/* Ethernet protocol for pause frame */
	u16 num_gfltr;
	u16 num_bfltr;

	/* RSS config */
	u16 rss_table_size;	/* HW RSS table size */
	u16 rss_size;		/* Allocated RSS queues */
	u8 *rss_hkey_user;	/* User configured hash keys */
	u8 *rss_lut_user;	/* User configured lookup table entries */
	u8 rss_lut_type;	/* used to configure Get/Set RSS LUT AQ call */

	/* aRFS members only allocated for the PF VSI */
#define ICE_MAX_ARFS_LIST	1024
#define ICE_ARFS_LST_MASK	(ICE_MAX_ARFS_LIST - 1)
	struct hlist_head *arfs_fltr_list;
	struct ice_arfs_active_fltr_cntrs *arfs_fltr_cntrs;
	spinlock_t arfs_lock;	/* protects aRFS hash table and filter state */
	atomic_t *arfs_last_fltr_id;

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
	u16 qset_handle[ICE_MAX_TRAFFIC_CLASS];
	struct ice_tc_cfg tc_cfg;
	struct bpf_prog *xdp_prog;
	struct ice_tx_ring **xdp_rings;	 /* XDP ring array */
	unsigned long *af_xdp_zc_qps;	 /* tracks AF_XDP ZC enabled qps */
	u16 num_xdp_txq;		 /* Used XDP queues */
	u8 xdp_mapping_mode;		 /* ICE_MAP_MODE_[CONTIG|SCATTER] */

	struct net_device **target_netdevs;

	struct tc_mqprio_qopt_offload mqprio_qopt; /* queue parameters */

	/* Channel Specific Fields */
	struct ice_vsi *tc_map_vsi[ICE_CHNL_MAX_TC];
	u16 cnt_q_avail;
	u16 next_base_q;	/* next queue to be used for channel setup */
	struct list_head ch_list;
	u16 num_chnl_rxq;
	u16 num_chnl_txq;
	u16 ch_rss_size;
	u16 num_chnl_fltr;
	/* store away rss size info before configuring ADQ channels so that,
	 * it can be used after tc-qdisc delete, to get back RSS setting as
	 * they were before
	 */
	u16 orig_rss_size;
	/* this keeps tracks of all enabled TC with and without DCB
	 * and inclusive of ADQ, vsi->mqprio_opt keeps track of queue
	 * information
	 */
	u8 all_numtc;
	u16 all_enatc;

	/* store away TC info, to be used for rebuild logic */
	u8 old_numtc;
	u16 old_ena_tc;

	struct ice_channel *ch;

	/* setup back reference, to which aggregator node this VSI
	 * corresponds to
	 */
	struct ice_agg_node *agg_node;
} ____cacheline_internodealigned_in_smp;

/* struct that defines an interrupt vector */
struct ice_q_vector {
	struct ice_vsi *vsi;

	u16 v_idx;			/* index in the vsi->q_vector array. */
	u16 reg_idx;
	u8 num_ring_rx;			/* total number of Rx rings in vector */
	u8 num_ring_tx;			/* total number of Tx rings in vector */
	u8 wb_on_itr:1;			/* if true, WB on ITR is enabled */
	/* in usecs, need to use ice_intrl_to_usecs_reg() before writing this
	 * value to the device
	 */
	u8 intrl;

	struct napi_struct napi;

	struct ice_ring_container rx;
	struct ice_ring_container tx;

	cpumask_t affinity_mask;
	struct irq_affinity_notify affinity_notify;

	struct ice_channel *ch;

	char name[ICE_INT_NAME_STR_LEN];

	u16 total_events;	/* net_dim(): number of interrupts processed */
} ____cacheline_internodealigned_in_smp;

enum ice_pf_flags {
	ICE_FLAG_FLTR_SYNC,
	ICE_FLAG_RDMA_ENA,
	ICE_FLAG_RSS_ENA,
	ICE_FLAG_SRIOV_ENA,
	ICE_FLAG_SRIOV_CAPABLE,
	ICE_FLAG_DCB_CAPABLE,
	ICE_FLAG_DCB_ENA,
	ICE_FLAG_FD_ENA,
	ICE_FLAG_PTP_SUPPORTED,		/* PTP is supported by NVM */
	ICE_FLAG_PTP,			/* PTP is enabled by software */
	ICE_FLAG_AUX_ENA,
	ICE_FLAG_ADV_FEATURES,
	ICE_FLAG_TC_MQPRIO,		/* support for Multi queue TC */
	ICE_FLAG_CLS_FLOWER,
	ICE_FLAG_LINK_DOWN_ON_CLOSE_ENA,
	ICE_FLAG_TOTAL_PORT_SHUTDOWN_ENA,
	ICE_FLAG_NO_MEDIA,
	ICE_FLAG_FW_LLDP_AGENT,
	ICE_FLAG_MOD_POWER_UNSUPPORTED,
	ICE_FLAG_PHY_FW_LOAD_FAILED,
	ICE_FLAG_ETHTOOL_CTXT,		/* set when ethtool holds RTNL lock */
	ICE_FLAG_LEGACY_RX,
	ICE_FLAG_VF_TRUE_PROMISC_ENA,
	ICE_FLAG_MDD_AUTO_RESET_VF,
	ICE_FLAG_LINK_LENIENT_MODE_ENA,
	ICE_FLAG_PLUG_AUX_DEV,
	ICE_FLAG_MTU_CHANGED,
	ICE_PF_FLAGS_NBITS		/* must be last */
};

struct ice_switchdev_info {
	struct ice_vsi *control_vsi;
	struct ice_vsi *uplink_vsi;
	bool is_running;
};

struct ice_agg_node {
	u32 agg_id;
#define ICE_MAX_VSIS_IN_AGG_NODE	64
	u32 num_vsis;
	u8 valid;
};

struct ice_pf {
	struct pci_dev *pdev;

	struct devlink_region *nvm_region;
	struct devlink_region *sram_region;
	struct devlink_region *devcaps_region;

	/* devlink port data */
	struct devlink_port devlink_port;

	/* OS reserved IRQ details */
	struct msix_entry *msix_entries;
	struct ice_res_tracker *irq_tracker;
	/* First MSIX vector used by SR-IOV VFs. Calculated by subtracting the
	 * number of MSIX vectors needed for all SR-IOV VFs from the number of
	 * MSIX vectors allowed on this PF.
	 */
	u16 sriov_base_vector;

	u16 ctrl_vsi_idx;		/* control VSI index in pf->vsi array */

	struct ice_vsi **vsi;		/* VSIs created by the driver */
	struct ice_sw *first_sw;	/* first switch created by firmware */
	u16 eswitch_mode;		/* current mode of eswitch */
	/* Virtchnl/SR-IOV config info */
	struct ice_vf *vf;
	u16 num_alloc_vfs;		/* actual number of VFs allocated */
	u16 num_vfs_supported;		/* num VFs supported for this PF */
	u16 num_qps_per_vf;
	u16 num_msix_per_vf;
	/* used to ratelimit the MDD event logging */
	unsigned long last_printed_mdd_jiffies;
	DECLARE_BITMAP(malvfs, ICE_MAX_VF_COUNT);
	DECLARE_BITMAP(features, ICE_F_MAX);
	DECLARE_BITMAP(state, ICE_STATE_NBITS);
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
	struct ice_ptp ptp;
	u16 num_rdma_msix;		/* Total MSIX vectors for RDMA driver */
	u16 rdma_base_vector;

	/* spinlock to protect the AdminQ wait list */
	spinlock_t aq_wait_lock;
	struct hlist_head aq_wait_list;
	wait_queue_head_t aq_wait_queue;
	bool fw_emp_reset_disabled;

	wait_queue_head_t reset_wait_queue;

	u32 hw_csum_rx_error;
	u16 oicr_idx;		/* Other interrupt cause MSIX vector index */
	u16 num_avail_sw_msix;	/* remaining MSIX SW vectors left unclaimed */
	u16 max_pf_txqs;	/* Total Tx queues PF wide */
	u16 max_pf_rxqs;	/* Total Rx queues PF wide */
	u16 num_lan_msix;	/* Total MSIX vectors for base driver */
	u16 num_lan_tx;		/* num LAN Tx queues setup */
	u16 num_lan_rx;		/* num LAN Rx queues setup */
	u16 next_vsi;		/* Next free slot in pf->vsi[] - 0-based! */
	u16 num_alloc_vsi;
	u16 corer_count;	/* Core reset count */
	u16 globr_count;	/* Global reset count */
	u16 empr_count;		/* EMP reset count */
	u16 pfr_count;		/* PF reset count */

	u8 wol_ena : 1;		/* software state of WoL */
	u32 wakeup_reason;	/* last wakeup reason */
	struct ice_hw_port_stats stats;
	struct ice_hw_port_stats stats_prev;
	struct ice_hw hw;
	u8 stat_prev_loaded:1; /* has previous stats been loaded */
	u8 rdma_mode;
	u16 dcbx_cap;
	u32 tx_timeout_count;
	unsigned long tx_timeout_last_recovery;
	u32 tx_timeout_recovery_level;
	char int_name[ICE_INT_NAME_STR_LEN];
	struct auxiliary_device *adev;
	int aux_idx;
	u32 sw_int_count;
	/* count of tc_flower filters specific to channel (aka where filter
	 * action is "hw_tc <tc_num>")
	 */
	u16 num_dmac_chnl_fltrs;
	struct hlist_head tc_flower_fltr_list;

	__le64 nvm_phy_type_lo; /* NVM PHY type low */
	__le64 nvm_phy_type_hi; /* NVM PHY type high */
	struct ice_link_default_override_tlv link_dflt_override;
	struct ice_lag *lag; /* Link Aggregation information */

	struct ice_switchdev_info switchdev;

#define ICE_INVALID_AGG_NODE_ID		0
#define ICE_PF_AGG_NODE_ID_START	1
#define ICE_MAX_PF_AGG_NODES		32
	struct ice_agg_node pf_agg_node[ICE_MAX_PF_AGG_NODES];
#define ICE_VF_AGG_NODE_ID_START	65
#define ICE_MAX_VF_AGG_NODES		32
	struct ice_agg_node vf_agg_node[ICE_MAX_VF_AGG_NODES];
};

struct ice_netdev_priv {
	struct ice_vsi *vsi;
	struct ice_repr *repr;
	/* indirect block callbacks on registered higher level devices
	 * (e.g. tunnel devices)
	 *
	 * tc_indr_block_cb_priv_list is used to look up indirect callback
	 * private data
	 */
	struct list_head tc_indr_block_priv_list;
};

/**
 * ice_vector_ch_enabled
 * @qv: pointer to q_vector, can be NULL
 *
 * This function returns true if vector is channel enabled otherwise false
 */
static inline bool ice_vector_ch_enabled(struct ice_q_vector *qv)
{
	return !!qv->ch; /* Enable it to run with TC */
}

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
		if (test_bit(ICE_VSI_DOWN, vsi->state))
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

static inline void ice_set_ring_xdp(struct ice_tx_ring *ring)
{
	ring->flags |= ICE_TX_FLAGS_RING_XDP;
}

/**
 * ice_xsk_pool - get XSK buffer pool bound to a ring
 * @ring: Rx ring to use
 *
 * Returns a pointer to xdp_umem structure if there is a buffer pool present,
 * NULL otherwise.
 */
static inline struct xsk_buff_pool *ice_xsk_pool(struct ice_rx_ring *ring)
{
	struct ice_vsi *vsi = ring->vsi;
	u16 qid = ring->q_index;

	if (!ice_is_xdp_ena_vsi(vsi) || !test_bit(qid, vsi->af_xdp_zc_qps))
		return NULL;

	return xsk_get_pool_from_qid(vsi->netdev, qid);
}

/**
 * ice_tx_xsk_pool - get XSK buffer pool bound to a ring
 * @ring: Tx ring to use
 *
 * Returns a pointer to xdp_umem structure if there is a buffer pool present,
 * NULL otherwise. Tx equivalent of ice_xsk_pool.
 */
static inline struct xsk_buff_pool *ice_tx_xsk_pool(struct ice_tx_ring *ring)
{
	struct ice_vsi *vsi = ring->vsi;
	u16 qid;

	qid = ring->q_index - vsi->num_xdp_txq;

	if (!ice_is_xdp_ena_vsi(vsi) || !test_bit(qid, vsi->af_xdp_zc_qps))
		return NULL;

	return xsk_get_pool_from_qid(vsi->netdev, qid);
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

/**
 * ice_get_netdev_priv_vsi - return VSI associated with netdev priv.
 * @np: private netdev structure
 */
static inline struct ice_vsi *ice_get_netdev_priv_vsi(struct ice_netdev_priv *np)
{
	/* In case of port representor return source port VSI. */
	if (np->repr)
		return np->repr->src_vsi;
	else
		return np->vsi;
}

/**
 * ice_get_ctrl_vsi - Get the control VSI
 * @pf: PF instance
 */
static inline struct ice_vsi *ice_get_ctrl_vsi(struct ice_pf *pf)
{
	/* if pf->ctrl_vsi_idx is ICE_NO_VSI, control VSI was not set up */
	if (!pf->vsi || pf->ctrl_vsi_idx == ICE_NO_VSI)
		return NULL;

	return pf->vsi[pf->ctrl_vsi_idx];
}

/**
 * ice_is_switchdev_running - check if switchdev is configured
 * @pf: pointer to PF structure
 *
 * Returns true if eswitch mode is set to DEVLINK_ESWITCH_MODE_SWITCHDEV
 * and switchdev is configured, false otherwise.
 */
static inline bool ice_is_switchdev_running(struct ice_pf *pf)
{
	return pf->switchdev.is_running;
}

/**
 * ice_set_sriov_cap - enable SRIOV in PF flags
 * @pf: PF struct
 */
static inline void ice_set_sriov_cap(struct ice_pf *pf)
{
	if (pf->hw.func_caps.common_cap.sr_iov_1_1)
		set_bit(ICE_FLAG_SRIOV_CAPABLE, pf->flags);
}

/**
 * ice_clear_sriov_cap - disable SRIOV in PF flags
 * @pf: PF struct
 */
static inline void ice_clear_sriov_cap(struct ice_pf *pf)
{
	clear_bit(ICE_FLAG_SRIOV_CAPABLE, pf->flags);
}

#define ICE_FD_STAT_CTR_BLOCK_COUNT	256
#define ICE_FD_STAT_PF_IDX(base_idx) \
			((base_idx) * ICE_FD_STAT_CTR_BLOCK_COUNT)
#define ICE_FD_SB_STAT_IDX(base_idx) ICE_FD_STAT_PF_IDX(base_idx)
#define ICE_FD_STAT_CH			1
#define ICE_FD_CH_STAT_IDX(base_idx) \
			(ICE_FD_STAT_PF_IDX(base_idx) + ICE_FD_STAT_CH)

/**
 * ice_is_adq_active - any active ADQs
 * @pf: pointer to PF
 *
 * This function returns true if there are any ADQs configured (which is
 * determined by looking at VSI type (which should be VSI_PF), numtc, and
 * TC_MQPRIO flag) otherwise return false
 */
static inline bool ice_is_adq_active(struct ice_pf *pf)
{
	struct ice_vsi *vsi;

	vsi = ice_get_main_vsi(pf);
	if (!vsi)
		return false;

	/* is ADQ configured */
	if (vsi->tc_cfg.numtc > ICE_CHNL_START_TC &&
	    test_bit(ICE_FLAG_TC_MQPRIO, pf->flags))
		return true;

	return false;
}

bool netif_is_ice(struct net_device *dev);
int ice_vsi_setup_tx_rings(struct ice_vsi *vsi);
int ice_vsi_setup_rx_rings(struct ice_vsi *vsi);
int ice_vsi_open_ctrl(struct ice_vsi *vsi);
int ice_vsi_open(struct ice_vsi *vsi);
void ice_set_ethtool_ops(struct net_device *netdev);
void ice_set_ethtool_repr_ops(struct net_device *netdev);
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
int ice_vsi_determine_xdp_res(struct ice_vsi *vsi);
int ice_prepare_xdp_rings(struct ice_vsi *vsi, struct bpf_prog *prog);
int ice_destroy_xdp_rings(struct ice_vsi *vsi);
int
ice_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **frames,
	     u32 flags);
int ice_set_rss_lut(struct ice_vsi *vsi, u8 *lut, u16 lut_size);
int ice_get_rss_lut(struct ice_vsi *vsi, u8 *lut, u16 lut_size);
int ice_set_rss_key(struct ice_vsi *vsi, u8 *seed);
int ice_get_rss_key(struct ice_vsi *vsi, u8 *seed);
void ice_fill_rss_lut(u8 *lut, u16 rss_table_size, u16 rss_size);
int ice_schedule_reset(struct ice_pf *pf, enum ice_reset_req reset);
void ice_print_link_msg(struct ice_vsi *vsi, bool isup);
int ice_plug_aux_dev(struct ice_pf *pf);
void ice_unplug_aux_dev(struct ice_pf *pf);
int ice_init_rdma(struct ice_pf *pf);
const char *ice_aq_str(enum ice_aq_err aq_err);
bool ice_is_wol_supported(struct ice_hw *hw);
void ice_fdir_del_all_fltrs(struct ice_vsi *vsi);
int
ice_fdir_write_fltr(struct ice_pf *pf, struct ice_fdir_fltr *input, bool add,
		    bool is_tun);
void ice_vsi_manage_fdir(struct ice_vsi *vsi, bool ena);
int ice_add_fdir_ethtool(struct ice_vsi *vsi, struct ethtool_rxnfc *cmd);
int ice_del_fdir_ethtool(struct ice_vsi *vsi, struct ethtool_rxnfc *cmd);
int ice_get_ethtool_fdir_entry(struct ice_hw *hw, struct ethtool_rxnfc *cmd);
int
ice_get_fdir_fltr_ids(struct ice_hw *hw, struct ethtool_rxnfc *cmd,
		      u32 *rule_locs);
void ice_fdir_rem_adq_chnl(struct ice_hw *hw, u16 vsi_idx);
void ice_fdir_release_flows(struct ice_hw *hw);
void ice_fdir_replay_flows(struct ice_hw *hw);
void ice_fdir_replay_fltrs(struct ice_pf *pf);
int ice_fdir_create_dflt_rules(struct ice_pf *pf);
int ice_aq_wait_for_event(struct ice_pf *pf, u16 opcode, unsigned long timeout,
			  struct ice_rq_event_info *event);
int ice_open(struct net_device *netdev);
int ice_open_internal(struct net_device *netdev);
int ice_stop(struct net_device *netdev);
void ice_service_task_schedule(struct ice_pf *pf);

/**
 * ice_set_rdma_cap - enable RDMA support
 * @pf: PF struct
 */
static inline void ice_set_rdma_cap(struct ice_pf *pf)
{
	if (pf->hw.func_caps.common_cap.rdma && pf->num_rdma_msix) {
		set_bit(ICE_FLAG_RDMA_ENA, pf->flags);
		set_bit(ICE_FLAG_AUX_ENA, pf->flags);
		set_bit(ICE_FLAG_PLUG_AUX_DEV, pf->flags);
	}
}

/**
 * ice_clear_rdma_cap - disable RDMA support
 * @pf: PF struct
 */
static inline void ice_clear_rdma_cap(struct ice_pf *pf)
{
	/* We can directly unplug aux device here only if the flag bit
	 * ICE_FLAG_PLUG_AUX_DEV is not set because ice_unplug_aux_dev()
	 * could race with ice_plug_aux_dev() called from
	 * ice_service_task(). In this case we only clear that bit now and
	 * aux device will be unplugged later once ice_plug_aux_device()
	 * called from ice_service_task() finishes (see ice_service_task()).
	 */
	if (!test_and_clear_bit(ICE_FLAG_PLUG_AUX_DEV, pf->flags))
		ice_unplug_aux_dev(pf);

	clear_bit(ICE_FLAG_RDMA_ENA, pf->flags);
	clear_bit(ICE_FLAG_AUX_ENA, pf->flags);
}
#endif /* _ICE_H_ */
