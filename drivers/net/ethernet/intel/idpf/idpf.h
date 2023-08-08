/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023 Intel Corporation */

#ifndef _IDPF_H_
#define _IDPF_H_

/* Forward declaration */
struct idpf_adapter;
struct idpf_vport;
struct idpf_vport_max_q;

#include <net/pkt_sched.h>
#include <linux/aer.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/bitfield.h>
#include <linux/sctp.h>
#include <linux/ethtool.h>
#include <net/gro.h>
#include <linux/dim.h>

#include "virtchnl2.h"
#include "idpf_lan_txrx.h"
#include "idpf_txrx.h"
#include "idpf_controlq.h"

#define GETMAXVAL(num_bits)		GENMASK((num_bits) - 1, 0)

#define IDPF_NO_FREE_SLOT		0xffff

/* Default Mailbox settings */
#define IDPF_NUM_FILTERS_PER_MSG	20
#define IDPF_NUM_DFLT_MBX_Q		2	/* includes both TX and RX */
#define IDPF_DFLT_MBX_Q_LEN		64
#define IDPF_DFLT_MBX_ID		-1
/* maximum number of times to try before resetting mailbox */
#define IDPF_MB_MAX_ERR			20
#define IDPF_NUM_CHUNKS_PER_MSG(struct_sz, chunk_sz)	\
	((IDPF_CTLQ_MAX_BUF_LEN - (struct_sz)) / (chunk_sz))
#define IDPF_WAIT_FOR_EVENT_TIMEO_MIN	2000
#define IDPF_WAIT_FOR_EVENT_TIMEO	60000

#define IDPF_MAX_WAIT			500

/* available message levels */
#define IDPF_AVAIL_NETIF_M (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

#define IDPF_DIM_PROFILE_SLOTS  5

#define IDPF_VIRTCHNL_VERSION_MAJOR VIRTCHNL2_VERSION_MAJOR_2
#define IDPF_VIRTCHNL_VERSION_MINOR VIRTCHNL2_VERSION_MINOR_0

/**
 * struct idpf_mac_filter
 * @list: list member field
 * @macaddr: MAC address
 * @remove: filter should be removed (virtchnl)
 * @add: filter should be added (virtchnl)
 */
struct idpf_mac_filter {
	struct list_head list;
	u8 macaddr[ETH_ALEN];
	bool remove;
	bool add;
};

/**
 * enum idpf_state - State machine to handle bring up
 * @__IDPF_STARTUP: Start the state machine
 * @__IDPF_VER_CHECK: Negotiate virtchnl version
 * @__IDPF_GET_CAPS: Negotiate capabilities
 * @__IDPF_INIT_SW: Init based on given capabilities
 * @__IDPF_STATE_LAST: Must be last, used to determine size
 */
enum idpf_state {
	__IDPF_STARTUP,
	__IDPF_VER_CHECK,
	__IDPF_GET_CAPS,
	__IDPF_INIT_SW,
	__IDPF_STATE_LAST,
};

/**
 * enum idpf_flags - Hard reset causes.
 * @IDPF_HR_FUNC_RESET: Hard reset when TxRx timeout
 * @IDPF_HR_DRV_LOAD: Set on driver load for a clean HW
 * @IDPF_HR_RESET_IN_PROG: Reset in progress
 * @IDPF_REMOVE_IN_PROG: Driver remove in progress
 * @IDPF_MB_INTR_MODE: Mailbox in interrupt mode
 * @IDPF_FLAGS_NBITS: Must be last
 */
enum idpf_flags {
	IDPF_HR_FUNC_RESET,
	IDPF_HR_DRV_LOAD,
	IDPF_HR_RESET_IN_PROG,
	IDPF_REMOVE_IN_PROG,
	IDPF_MB_INTR_MODE,
	IDPF_FLAGS_NBITS,
};

/**
 * enum idpf_cap_field - Offsets into capabilities struct for specific caps
 * @IDPF_BASE_CAPS: generic base capabilities
 * @IDPF_CSUM_CAPS: checksum offload capabilities
 * @IDPF_SEG_CAPS: segmentation offload capabilities
 * @IDPF_RSS_CAPS: RSS offload capabilities
 * @IDPF_HSPLIT_CAPS: Header split capabilities
 * @IDPF_RSC_CAPS: RSC offload capabilities
 * @IDPF_OTHER_CAPS: miscellaneous offloads
 *
 * Used when checking for a specific capability flag since different capability
 * sets are not mutually exclusive numerically, the caller must specify which
 * type of capability they are checking for.
 */
enum idpf_cap_field {
	IDPF_BASE_CAPS		= -1,
	IDPF_CSUM_CAPS		= offsetof(struct virtchnl2_get_capabilities,
					   csum_caps),
	IDPF_SEG_CAPS		= offsetof(struct virtchnl2_get_capabilities,
					   seg_caps),
	IDPF_RSS_CAPS		= offsetof(struct virtchnl2_get_capabilities,
					   rss_caps),
	IDPF_HSPLIT_CAPS	= offsetof(struct virtchnl2_get_capabilities,
					   hsplit_caps),
	IDPF_RSC_CAPS		= offsetof(struct virtchnl2_get_capabilities,
					   rsc_caps),
	IDPF_OTHER_CAPS		= offsetof(struct virtchnl2_get_capabilities,
					   other_caps),
};

/**
 * enum idpf_vport_state - Current vport state
 * @__IDPF_VPORT_DOWN: Vport is down
 * @__IDPF_VPORT_UP: Vport is up
 * @__IDPF_VPORT_STATE_LAST: Must be last, number of states
 */
enum idpf_vport_state {
	__IDPF_VPORT_DOWN,
	__IDPF_VPORT_UP,
	__IDPF_VPORT_STATE_LAST,
};

/**
 * struct idpf_netdev_priv - Struct to store vport back pointer
 * @adapter: Adapter back pointer
 * @vport: Vport back pointer
 * @vport_id: Vport identifier
 * @vport_idx: Relative vport index
 * @state: See enum idpf_vport_state
 * @netstats: Packet and byte stats
 * @stats_lock: Lock to protect stats update
 */
struct idpf_netdev_priv {
	struct idpf_adapter *adapter;
	struct idpf_vport *vport;
	u32 vport_id;
	u16 vport_idx;
	enum idpf_vport_state state;
	struct rtnl_link_stats64 netstats;
	spinlock_t stats_lock;
};

/**
 * struct idpf_reset_reg - Reset register offsets/masks
 * @rstat: Reset status register
 * @rstat_m: Reset status mask
 */
struct idpf_reset_reg {
	void __iomem *rstat;
	u32 rstat_m;
};

/**
 * struct idpf_vport_max_q - Queue limits
 * @max_rxq: Maximum number of RX queues supported
 * @max_txq: Maixmum number of TX queues supported
 * @max_bufq: In splitq, maximum number of buffer queues supported
 * @max_complq: In splitq, maximum number of completion queues supported
 */
struct idpf_vport_max_q {
	u16 max_rxq;
	u16 max_txq;
	u16 max_bufq;
	u16 max_complq;
};

/**
 * struct idpf_reg_ops - Device specific register operation function pointers
 * @ctlq_reg_init: Mailbox control queue register initialization
 * @intr_reg_init: Traffic interrupt register initialization
 * @mb_intr_reg_init: Mailbox interrupt register initialization
 * @reset_reg_init: Reset register initialization
 * @trigger_reset: Trigger a reset to occur
 */
struct idpf_reg_ops {
	void (*ctlq_reg_init)(struct idpf_ctlq_create_info *cq);
	int (*intr_reg_init)(struct idpf_vport *vport);
	void (*mb_intr_reg_init)(struct idpf_adapter *adapter);
	void (*reset_reg_init)(struct idpf_adapter *adapter);
	void (*trigger_reset)(struct idpf_adapter *adapter,
			      enum idpf_flags trig_cause);
};

/**
 * struct idpf_dev_ops - Device specific operations
 * @reg_ops: Register operations
 */
struct idpf_dev_ops {
	struct idpf_reg_ops reg_ops;
};

/* These macros allow us to generate an enum and a matching char * array of
 * stringified enums that are always in sync. Checkpatch issues a bogus warning
 * about this being a complex macro; but it's wrong, these are never used as a
 * statement and instead only used to define the enum and array.
 */
#define IDPF_FOREACH_VPORT_VC_STATE(STATE)	\
	STATE(IDPF_VC_CREATE_VPORT)		\
	STATE(IDPF_VC_CREATE_VPORT_ERR)		\
	STATE(IDPF_VC_ENA_VPORT)		\
	STATE(IDPF_VC_ENA_VPORT_ERR)		\
	STATE(IDPF_VC_DIS_VPORT)		\
	STATE(IDPF_VC_DIS_VPORT_ERR)		\
	STATE(IDPF_VC_DESTROY_VPORT)		\
	STATE(IDPF_VC_DESTROY_VPORT_ERR)	\
	STATE(IDPF_VC_CONFIG_TXQ)		\
	STATE(IDPF_VC_CONFIG_TXQ_ERR)		\
	STATE(IDPF_VC_CONFIG_RXQ)		\
	STATE(IDPF_VC_CONFIG_RXQ_ERR)		\
	STATE(IDPF_VC_ENA_QUEUES)		\
	STATE(IDPF_VC_ENA_QUEUES_ERR)		\
	STATE(IDPF_VC_DIS_QUEUES)		\
	STATE(IDPF_VC_DIS_QUEUES_ERR)		\
	STATE(IDPF_VC_MAP_IRQ)			\
	STATE(IDPF_VC_MAP_IRQ_ERR)		\
	STATE(IDPF_VC_UNMAP_IRQ)		\
	STATE(IDPF_VC_UNMAP_IRQ_ERR)		\
	STATE(IDPF_VC_ADD_QUEUES)		\
	STATE(IDPF_VC_ADD_QUEUES_ERR)		\
	STATE(IDPF_VC_DEL_QUEUES)		\
	STATE(IDPF_VC_DEL_QUEUES_ERR)		\
	STATE(IDPF_VC_ALLOC_VECTORS)		\
	STATE(IDPF_VC_ALLOC_VECTORS_ERR)	\
	STATE(IDPF_VC_DEALLOC_VECTORS)		\
	STATE(IDPF_VC_DEALLOC_VECTORS_ERR)	\
	STATE(IDPF_VC_SET_SRIOV_VFS)		\
	STATE(IDPF_VC_SET_SRIOV_VFS_ERR)	\
	STATE(IDPF_VC_GET_RSS_LUT)		\
	STATE(IDPF_VC_GET_RSS_LUT_ERR)		\
	STATE(IDPF_VC_SET_RSS_LUT)		\
	STATE(IDPF_VC_SET_RSS_LUT_ERR)		\
	STATE(IDPF_VC_GET_RSS_KEY)		\
	STATE(IDPF_VC_GET_RSS_KEY_ERR)		\
	STATE(IDPF_VC_SET_RSS_KEY)		\
	STATE(IDPF_VC_SET_RSS_KEY_ERR)		\
	STATE(IDPF_VC_GET_STATS)		\
	STATE(IDPF_VC_GET_STATS_ERR)		\
	STATE(IDPF_VC_ADD_MAC_ADDR)		\
	STATE(IDPF_VC_ADD_MAC_ADDR_ERR)		\
	STATE(IDPF_VC_DEL_MAC_ADDR)		\
	STATE(IDPF_VC_DEL_MAC_ADDR_ERR)		\
	STATE(IDPF_VC_GET_PTYPE_INFO)		\
	STATE(IDPF_VC_GET_PTYPE_INFO_ERR)	\
	STATE(IDPF_VC_LOOPBACK_STATE)		\
	STATE(IDPF_VC_LOOPBACK_STATE_ERR)	\
	STATE(IDPF_VC_NBITS)

#define IDPF_GEN_ENUM(ENUM) ENUM,
#define IDPF_GEN_STRING(STRING) #STRING,

enum idpf_vport_vc_state {
	IDPF_FOREACH_VPORT_VC_STATE(IDPF_GEN_ENUM)
};

extern const char * const idpf_vport_vc_state_str[];

/**
 * enum idpf_vport_reset_cause - Vport soft reset causes
 * @IDPF_SR_Q_CHANGE: Soft reset queue change
 * @IDPF_SR_Q_DESC_CHANGE: Soft reset descriptor change
 * @IDPF_SR_MTU_CHANGE: Soft reset MTU change
 * @IDPF_SR_RSC_CHANGE: Soft reset RSC change
 */
enum idpf_vport_reset_cause {
	IDPF_SR_Q_CHANGE,
	IDPF_SR_Q_DESC_CHANGE,
	IDPF_SR_MTU_CHANGE,
	IDPF_SR_RSC_CHANGE,
};

/**
 * enum idpf_vport_flags - Vport flags
 * @IDPF_VPORT_DEL_QUEUES: To send delete queues message
 * @IDPF_VPORT_SW_MARKER: Indicate TX pipe drain software marker packets
 *			  processing is done
 * @IDPF_VPORT_FLAGS_NBITS: Must be last
 */
enum idpf_vport_flags {
	IDPF_VPORT_DEL_QUEUES,
	IDPF_VPORT_SW_MARKER,
	IDPF_VPORT_FLAGS_NBITS,
};

struct idpf_port_stats {
	struct u64_stats_sync stats_sync;
	u64_stats_t rx_hw_csum_err;
	u64_stats_t rx_hsplit;
	u64_stats_t rx_hsplit_hbo;
	u64_stats_t rx_bad_descs;
	u64_stats_t tx_linearize;
	u64_stats_t tx_busy;
	u64_stats_t tx_drops;
	u64_stats_t tx_dma_map_errs;
	struct virtchnl2_vport_stats vport_stats;
};

/**
 * struct idpf_vport - Handle for netdevices and queue resources
 * @num_txq: Number of allocated TX queues
 * @num_complq: Number of allocated completion queues
 * @txq_desc_count: TX queue descriptor count
 * @complq_desc_count: Completion queue descriptor count
 * @compln_clean_budget: Work budget for completion clean
 * @num_txq_grp: Number of TX queue groups
 * @txq_grps: Array of TX queue groups
 * @txq_model: Split queue or single queue queuing model
 * @txqs: Used only in hotpath to get to the right queue very fast
 * @crc_enable: Enable CRC insertion offload
 * @num_rxq: Number of allocated RX queues
 * @num_bufq: Number of allocated buffer queues
 * @rxq_desc_count: RX queue descriptor count. *MUST* have enough descriptors
 *		    to complete all buffer descriptors for all buffer queues in
 *		    the worst case.
 * @num_bufqs_per_qgrp: Buffer queues per RX queue in a given grouping
 * @bufq_desc_count: Buffer queue descriptor count
 * @bufq_size: Size of buffers in ring (e.g. 2K, 4K, etc)
 * @num_rxq_grp: Number of RX queues in a group
 * @rxq_grps: Total number of RX groups. Number of groups * number of RX per
 *	      group will yield total number of RX queues.
 * @rxq_model: Splitq queue or single queue queuing model
 * @rx_ptype_lkup: Lookup table for ptypes on RX
 * @adapter: back pointer to associated adapter
 * @netdev: Associated net_device. Each vport should have one and only one
 *	    associated netdev.
 * @flags: See enum idpf_vport_flags
 * @vport_type: Default SRIOV, SIOV, etc.
 * @vport_id: Device given vport identifier
 * @idx: Software index in adapter vports struct
 * @default_vport: Use this vport if one isn't specified
 * @base_rxd: True if the driver should use base descriptors instead of flex
 * @num_q_vectors: Number of IRQ vectors allocated
 * @q_vectors: Array of queue vectors
 * @q_vector_idxs: Starting index of queue vectors
 * @max_mtu: device given max possible MTU
 * @default_mac_addr: device will give a default MAC to use
 * @rx_itr_profile: RX profiles for Dynamic Interrupt Moderation
 * @tx_itr_profile: TX profiles for Dynamic Interrupt Moderation
 * @port_stats: per port csum, header split, and other offload stats
 * @link_up: True if link is up
 * @link_speed_mbps: Link speed in mbps
 * @vc_msg: Virtchnl message buffer
 * @vc_state: Virtchnl message state
 * @vchnl_wq: Wait queue for virtchnl messages
 * @sw_marker_wq: workqueue for marker packets
 * @vc_buf_lock: Lock to protect virtchnl buffer
 */
struct idpf_vport {
	u16 num_txq;
	u16 num_complq;
	u32 txq_desc_count;
	u32 complq_desc_count;
	u32 compln_clean_budget;
	u16 num_txq_grp;
	struct idpf_txq_group *txq_grps;
	u32 txq_model;
	struct idpf_queue **txqs;
	bool crc_enable;

	u16 num_rxq;
	u16 num_bufq;
	u32 rxq_desc_count;
	u8 num_bufqs_per_qgrp;
	u32 bufq_desc_count[IDPF_MAX_BUFQS_PER_RXQ_GRP];
	u32 bufq_size[IDPF_MAX_BUFQS_PER_RXQ_GRP];
	u16 num_rxq_grp;
	struct idpf_rxq_group *rxq_grps;
	u32 rxq_model;
	struct idpf_rx_ptype_decoded rx_ptype_lkup[IDPF_RX_MAX_PTYPE];

	struct idpf_adapter *adapter;
	struct net_device *netdev;
	DECLARE_BITMAP(flags, IDPF_VPORT_FLAGS_NBITS);
	u16 vport_type;
	u32 vport_id;
	u16 idx;
	bool default_vport;
	bool base_rxd;

	u16 num_q_vectors;
	struct idpf_q_vector *q_vectors;
	u16 *q_vector_idxs;
	u16 max_mtu;
	u8 default_mac_addr[ETH_ALEN];
	u16 rx_itr_profile[IDPF_DIM_PROFILE_SLOTS];
	u16 tx_itr_profile[IDPF_DIM_PROFILE_SLOTS];
	struct idpf_port_stats port_stats;

	bool link_up;
	u32 link_speed_mbps;

	char vc_msg[IDPF_CTLQ_MAX_BUF_LEN];
	DECLARE_BITMAP(vc_state, IDPF_VC_NBITS);

	wait_queue_head_t vchnl_wq;
	wait_queue_head_t sw_marker_wq;
	struct mutex vc_buf_lock;
};

/**
 * enum idpf_user_flags
 * @__IDPF_PROMISC_UC: Unicast promiscuous mode
 * @__IDPF_PROMISC_MC: Multicast promiscuous mode
 * @__IDPF_USER_FLAGS_NBITS: Must be last
 */
enum idpf_user_flags {
	__IDPF_PROMISC_UC = 32,
	__IDPF_PROMISC_MC,

	__IDPF_USER_FLAGS_NBITS,
};

/**
 * struct idpf_rss_data - Associated RSS data
 * @rss_key_size: Size of RSS hash key
 * @rss_key: RSS hash key
 * @rss_lut_size: Size of RSS lookup table
 * @rss_lut: RSS lookup table
 * @cached_lut: Used to restore previously init RSS lut
 */
struct idpf_rss_data {
	u16 rss_key_size;
	u8 *rss_key;
	u16 rss_lut_size;
	u32 *rss_lut;
	u32 *cached_lut;
};

/**
 * struct idpf_vport_user_config_data - User defined configuration values for
 *					each vport.
 * @rss_data: See struct idpf_rss_data
 * @num_req_tx_qs: Number of user requested TX queues through ethtool
 * @num_req_rx_qs: Number of user requested RX queues through ethtool
 * @num_req_txq_desc: Number of user requested TX queue descriptors through
 *		      ethtool
 * @num_req_rxq_desc: Number of user requested RX queue descriptors through
 *		      ethtool
 * @user_flags: User toggled config flags
 * @mac_filter_list: List of MAC filters
 *
 * Used to restore configuration after a reset as the vport will get wiped.
 */
struct idpf_vport_user_config_data {
	struct idpf_rss_data rss_data;
	u16 num_req_tx_qs;
	u16 num_req_rx_qs;
	u32 num_req_txq_desc;
	u32 num_req_rxq_desc;
	DECLARE_BITMAP(user_flags, __IDPF_USER_FLAGS_NBITS);
	struct list_head mac_filter_list;
};

/**
 * enum idpf_vport_config_flags - Vport config flags
 * @IDPF_VPORT_REG_NETDEV: Register netdev
 * @IDPF_VPORT_UP_REQUESTED: Set if interface up is requested on core reset
 * @IDPF_VPORT_ADD_MAC_REQ: Asynchronous add ether address in flight
 * @IDPF_VPORT_DEL_MAC_REQ: Asynchronous delete ether address in flight
 * @IDPF_VPORT_CONFIG_FLAGS_NBITS: Must be last
 */
enum idpf_vport_config_flags {
	IDPF_VPORT_REG_NETDEV,
	IDPF_VPORT_UP_REQUESTED,
	IDPF_VPORT_ADD_MAC_REQ,
	IDPF_VPORT_DEL_MAC_REQ,
	IDPF_VPORT_CONFIG_FLAGS_NBITS,
};

/**
 * struct idpf_avail_queue_info
 * @avail_rxq: Available RX queues
 * @avail_txq: Available TX queues
 * @avail_bufq: Available buffer queues
 * @avail_complq: Available completion queues
 *
 * Maintain total queues available after allocating max queues to each vport.
 */
struct idpf_avail_queue_info {
	u16 avail_rxq;
	u16 avail_txq;
	u16 avail_bufq;
	u16 avail_complq;
};

/**
 * struct idpf_vector_info - Utility structure to pass function arguments as a
 *			     structure
 * @num_req_vecs: Vectors required based on the number of queues updated by the
 *		  user via ethtool
 * @num_curr_vecs: Current number of vectors, must be >= @num_req_vecs
 * @index: Relative starting index for vectors
 * @default_vport: Vectors are for default vport
 */
struct idpf_vector_info {
	u16 num_req_vecs;
	u16 num_curr_vecs;
	u16 index;
	bool default_vport;
};

/**
 * struct idpf_vector_lifo - Stack to maintain vector indexes used for vector
 *			     distribution algorithm
 * @top: Points to stack top i.e. next available vector index
 * @base: Always points to start of the free pool
 * @size: Total size of the vector stack
 * @vec_idx: Array to store all the vector indexes
 *
 * Vector stack maintains all the relative vector indexes at the *adapter*
 * level. This stack is divided into 2 parts, first one is called as 'default
 * pool' and other one is called 'free pool'.  Vector distribution algorithm
 * gives priority to default vports in a way that at least IDPF_MIN_Q_VEC
 * vectors are allocated per default vport and the relative vector indexes for
 * those are maintained in default pool. Free pool contains all the unallocated
 * vector indexes which can be allocated on-demand basis. Mailbox vector index
 * is maintained in the default pool of the stack.
 */
struct idpf_vector_lifo {
	u16 top;
	u16 base;
	u16 size;
	u16 *vec_idx;
};

/**
 * struct idpf_vport_config - Vport configuration data
 * @user_config: see struct idpf_vport_user_config_data
 * @max_q: Maximum possible queues
 * @req_qs_chunks: Queue chunk data for requested queues
 * @mac_filter_list_lock: Lock to protect mac filters
 * @flags: See enum idpf_vport_config_flags
 */
struct idpf_vport_config {
	struct idpf_vport_user_config_data user_config;
	struct idpf_vport_max_q max_q;
	void *req_qs_chunks;
	spinlock_t mac_filter_list_lock;
	DECLARE_BITMAP(flags, IDPF_VPORT_CONFIG_FLAGS_NBITS);
};

/**
 * struct idpf_adapter - Device data struct generated on probe
 * @pdev: PCI device struct given on probe
 * @virt_ver_maj: Virtchnl version major
 * @virt_ver_min: Virtchnl version minor
 * @msg_enable: Debug message level enabled
 * @mb_wait_count: Number of times mailbox was attempted initialization
 * @state: Init state machine
 * @flags: See enum idpf_flags
 * @reset_reg: See struct idpf_reset_reg
 * @hw: Device access data
 * @num_req_msix: Requested number of MSIX vectors
 * @num_avail_msix: Available number of MSIX vectors
 * @num_msix_entries: Number of entries in MSIX table
 * @msix_entries: MSIX table
 * @req_vec_chunks: Requested vector chunk data
 * @mb_vector: Mailbox vector data
 * @vector_stack: Stack to store the msix vector indexes
 * @irq_mb_handler: Handler for hard interrupt for mailbox
 * @tx_timeout_count: Number of TX timeouts that have occurred
 * @avail_queues: Device given queue limits
 * @vports: Array to store vports created by the driver
 * @netdevs: Associated Vport netdevs
 * @vport_params_reqd: Vport params requested
 * @vport_params_recvd: Vport params received
 * @vport_ids: Array of device given vport identifiers
 * @vport_config: Vport config parameters
 * @max_vports: Maximum vports that can be allocated
 * @num_alloc_vports: Current number of vports allocated
 * @next_vport: Next free slot in pf->vport[] - 0-based!
 * @init_task: Initialization task
 * @init_wq: Workqueue for initialization task
 * @serv_task: Periodically recurring maintenance task
 * @serv_wq: Workqueue for service task
 * @mbx_task: Task to handle mailbox interrupts
 * @mbx_wq: Workqueue for mailbox responses
 * @vc_event_task: Task to handle out of band virtchnl event notifications
 * @vc_event_wq: Workqueue for virtchnl events
 * @stats_task: Periodic statistics retrieval task
 * @stats_wq: Workqueue for statistics task
 * @caps: Negotiated capabilities with device
 * @vchnl_wq: Wait queue for virtchnl messages
 * @vc_state: Virtchnl message state
 * @vc_msg: Virtchnl message buffer
 * @dev_ops: See idpf_dev_ops
 * @num_vfs: Number of allocated VFs through sysfs. PF does not directly talk
 *	     to VFs but is used to initialize them
 * @crc_enable: Enable CRC insertion offload
 * @req_tx_splitq: TX split or single queue model to request
 * @req_rx_splitq: RX split or single queue model to request
 * @vport_ctrl_lock: Lock to protect the vport control flow
 * @vector_lock: Lock to protect vector distribution
 * @queue_lock: Lock to protect queue distribution
 * @vc_buf_lock: Lock to protect virtchnl buffer
 */
struct idpf_adapter {
	struct pci_dev *pdev;
	u32 virt_ver_maj;
	u32 virt_ver_min;

	u32 msg_enable;
	u32 mb_wait_count;
	enum idpf_state state;
	DECLARE_BITMAP(flags, IDPF_FLAGS_NBITS);
	struct idpf_reset_reg reset_reg;
	struct idpf_hw hw;
	u16 num_req_msix;
	u16 num_avail_msix;
	u16 num_msix_entries;
	struct msix_entry *msix_entries;
	struct virtchnl2_alloc_vectors *req_vec_chunks;
	struct idpf_q_vector mb_vector;
	struct idpf_vector_lifo vector_stack;
	irqreturn_t (*irq_mb_handler)(int irq, void *data);

	u32 tx_timeout_count;
	struct idpf_avail_queue_info avail_queues;
	struct idpf_vport **vports;
	struct net_device **netdevs;
	struct virtchnl2_create_vport **vport_params_reqd;
	struct virtchnl2_create_vport **vport_params_recvd;
	u32 *vport_ids;

	struct idpf_vport_config **vport_config;
	u16 max_vports;
	u16 num_alloc_vports;
	u16 next_vport;

	struct delayed_work init_task;
	struct workqueue_struct *init_wq;
	struct delayed_work serv_task;
	struct workqueue_struct *serv_wq;
	struct delayed_work mbx_task;
	struct workqueue_struct *mbx_wq;
	struct delayed_work vc_event_task;
	struct workqueue_struct *vc_event_wq;
	struct delayed_work stats_task;
	struct workqueue_struct *stats_wq;
	struct virtchnl2_get_capabilities caps;

	wait_queue_head_t vchnl_wq;
	DECLARE_BITMAP(vc_state, IDPF_VC_NBITS);
	char vc_msg[IDPF_CTLQ_MAX_BUF_LEN];
	struct idpf_dev_ops dev_ops;
	int num_vfs;
	bool crc_enable;
	bool req_tx_splitq;
	bool req_rx_splitq;

	struct mutex vport_ctrl_lock;
	struct mutex vector_lock;
	struct mutex queue_lock;
	struct mutex vc_buf_lock;
};

/**
 * idpf_is_queue_model_split - check if queue model is split
 * @q_model: queue model single or split
 *
 * Returns true if queue model is split else false
 */
static inline int idpf_is_queue_model_split(u16 q_model)
{
	return q_model == VIRTCHNL2_QUEUE_MODEL_SPLIT;
}

#define idpf_is_cap_ena(adapter, field, flag) \
	idpf_is_capability_ena(adapter, false, field, flag)
#define idpf_is_cap_ena_all(adapter, field, flag) \
	idpf_is_capability_ena(adapter, true, field, flag)

bool idpf_is_capability_ena(struct idpf_adapter *adapter, bool all,
			    enum idpf_cap_field field, u64 flag);

#define IDPF_CAP_RSS (\
	VIRTCHNL2_CAP_RSS_IPV4_TCP	|\
	VIRTCHNL2_CAP_RSS_IPV4_TCP	|\
	VIRTCHNL2_CAP_RSS_IPV4_UDP	|\
	VIRTCHNL2_CAP_RSS_IPV4_SCTP	|\
	VIRTCHNL2_CAP_RSS_IPV4_OTHER	|\
	VIRTCHNL2_CAP_RSS_IPV6_TCP	|\
	VIRTCHNL2_CAP_RSS_IPV6_TCP	|\
	VIRTCHNL2_CAP_RSS_IPV6_UDP	|\
	VIRTCHNL2_CAP_RSS_IPV6_SCTP	|\
	VIRTCHNL2_CAP_RSS_IPV6_OTHER)

#define IDPF_CAP_RSC (\
	VIRTCHNL2_CAP_RSC_IPV4_TCP	|\
	VIRTCHNL2_CAP_RSC_IPV6_TCP)

#define IDPF_CAP_HSPLIT	(\
	VIRTCHNL2_CAP_RX_HSPLIT_AT_L4V4	|\
	VIRTCHNL2_CAP_RX_HSPLIT_AT_L4V6)

#define IDPF_CAP_RX_CSUM_L4V4 (\
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_TCP	|\
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_UDP)

#define IDPF_CAP_RX_CSUM_L4V6 (\
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_TCP	|\
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_UDP)

#define IDPF_CAP_RX_CSUM (\
	VIRTCHNL2_CAP_RX_CSUM_L3_IPV4		|\
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_TCP	|\
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_UDP	|\
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_TCP	|\
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_UDP)

#define IDPF_CAP_SCTP_CSUM (\
	VIRTCHNL2_CAP_TX_CSUM_L4_IPV4_SCTP	|\
	VIRTCHNL2_CAP_TX_CSUM_L4_IPV6_SCTP	|\
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_SCTP	|\
	VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_SCTP)

#define IDPF_CAP_TUNNEL_TX_CSUM (\
	VIRTCHNL2_CAP_TX_CSUM_L3_SINGLE_TUNNEL	|\
	VIRTCHNL2_CAP_TX_CSUM_L4_SINGLE_TUNNEL)

/**
 * idpf_get_reserved_vecs - Get reserved vectors
 * @adapter: private data struct
 */
static inline u16 idpf_get_reserved_vecs(struct idpf_adapter *adapter)
{
	return le16_to_cpu(adapter->caps.num_allocated_vectors);
}

/**
 * idpf_get_default_vports - Get default number of vports
 * @adapter: private data struct
 */
static inline u16 idpf_get_default_vports(struct idpf_adapter *adapter)
{
	return le16_to_cpu(adapter->caps.default_num_vports);
}

/**
 * idpf_get_max_vports - Get max number of vports
 * @adapter: private data struct
 */
static inline u16 idpf_get_max_vports(struct idpf_adapter *adapter)
{
	return le16_to_cpu(adapter->caps.max_vports);
}

/**
 * idpf_get_max_tx_bufs - Get max scatter-gather buffers supported by the device
 * @adapter: private data struct
 */
static inline unsigned int idpf_get_max_tx_bufs(struct idpf_adapter *adapter)
{
	return adapter->caps.max_sg_bufs_per_tx_pkt;
}

/**
 * idpf_get_min_tx_pkt_len - Get min packet length supported by the device
 * @adapter: private data struct
 */
static inline u8 idpf_get_min_tx_pkt_len(struct idpf_adapter *adapter)
{
	u8 pkt_len = adapter->caps.min_sso_packet_len;

	return pkt_len ? pkt_len : IDPF_TX_MIN_PKT_LEN;
}

/**
 * idpf_get_reg_addr - Get BAR0 register address
 * @adapter: private data struct
 * @reg_offset: register offset value
 *
 * Based on the register offset, return the actual BAR0 register address
 */
static inline void __iomem *idpf_get_reg_addr(struct idpf_adapter *adapter,
					      resource_size_t reg_offset)
{
	return (void __iomem *)(adapter->hw.hw_addr + reg_offset);
}

/**
 * idpf_is_reset_detected - check if we were reset at some point
 * @adapter: driver specific private structure
 *
 * Returns true if we are either in reset currently or were previously reset.
 */
static inline bool idpf_is_reset_detected(struct idpf_adapter *adapter)
{
	if (!adapter->hw.arq)
		return true;

	return !(readl(idpf_get_reg_addr(adapter, adapter->hw.arq->reg.len)) &
		 adapter->hw.arq->reg.len_mask);
}

/**
 * idpf_is_reset_in_prog - check if reset is in progress
 * @adapter: driver specific private structure
 *
 * Returns true if hard reset is in progress, false otherwise
 */
static inline bool idpf_is_reset_in_prog(struct idpf_adapter *adapter)
{
	return (test_bit(IDPF_HR_RESET_IN_PROG, adapter->flags) ||
		test_bit(IDPF_HR_FUNC_RESET, adapter->flags) ||
		test_bit(IDPF_HR_DRV_LOAD, adapter->flags));
}

/**
 * idpf_netdev_to_vport - get a vport handle from a netdev
 * @netdev: network interface device structure
 */
static inline struct idpf_vport *idpf_netdev_to_vport(struct net_device *netdev)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);

	return np->vport;
}

/**
 * idpf_netdev_to_adapter - Get adapter handle from a netdev
 * @netdev: Network interface device structure
 */
static inline struct idpf_adapter *idpf_netdev_to_adapter(struct net_device *netdev)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);

	return np->adapter;
}

/**
 * idpf_is_feature_ena - Determine if a particular feature is enabled
 * @vport: Vport to check
 * @feature: Netdev flag to check
 *
 * Returns true or false if a particular feature is enabled.
 */
static inline bool idpf_is_feature_ena(const struct idpf_vport *vport,
				       netdev_features_t feature)
{
	return vport->netdev->features & feature;
}

/**
 * idpf_get_max_tx_hdr_size -- get the size of tx header
 * @adapter: Driver specific private structure
 */
static inline u16 idpf_get_max_tx_hdr_size(struct idpf_adapter *adapter)
{
	return le16_to_cpu(adapter->caps.max_tx_hdr_size);
}

/**
 * idpf_vport_ctrl_lock - Acquire the vport control lock
 * @netdev: Network interface device structure
 *
 * This lock should be used by non-datapath code to protect against vport
 * destruction.
 */
static inline void idpf_vport_ctrl_lock(struct net_device *netdev)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);

	mutex_lock(&np->adapter->vport_ctrl_lock);
}

/**
 * idpf_vport_ctrl_unlock - Release the vport control lock
 * @netdev: Network interface device structure
 */
static inline void idpf_vport_ctrl_unlock(struct net_device *netdev)
{
	struct idpf_netdev_priv *np = netdev_priv(netdev);

	mutex_unlock(&np->adapter->vport_ctrl_lock);
}

void idpf_statistics_task(struct work_struct *work);
void idpf_init_task(struct work_struct *work);
void idpf_service_task(struct work_struct *work);
void idpf_mbx_task(struct work_struct *work);
void idpf_vc_event_task(struct work_struct *work);
void idpf_dev_ops_init(struct idpf_adapter *adapter);
void idpf_vf_dev_ops_init(struct idpf_adapter *adapter);
int idpf_vport_adjust_qs(struct idpf_vport *vport);
int idpf_init_dflt_mbx(struct idpf_adapter *adapter);
void idpf_deinit_dflt_mbx(struct idpf_adapter *adapter);
int idpf_vc_core_init(struct idpf_adapter *adapter);
void idpf_vc_core_deinit(struct idpf_adapter *adapter);
int idpf_intr_req(struct idpf_adapter *adapter);
void idpf_intr_rel(struct idpf_adapter *adapter);
int idpf_get_reg_intr_vecs(struct idpf_vport *vport,
			   struct idpf_vec_regs *reg_vals);
u16 idpf_get_max_tx_hdr_size(struct idpf_adapter *adapter);
int idpf_send_delete_queues_msg(struct idpf_vport *vport);
int idpf_send_add_queues_msg(const struct idpf_vport *vport, u16 num_tx_q,
			     u16 num_complq, u16 num_rx_q, u16 num_rx_bufq);
int idpf_initiate_soft_reset(struct idpf_vport *vport,
			     enum idpf_vport_reset_cause reset_cause);
int idpf_send_enable_vport_msg(struct idpf_vport *vport);
int idpf_send_disable_vport_msg(struct idpf_vport *vport);
int idpf_send_destroy_vport_msg(struct idpf_vport *vport);
int idpf_send_get_rx_ptype_msg(struct idpf_vport *vport);
int idpf_send_ena_dis_loopback_msg(struct idpf_vport *vport);
int idpf_send_get_set_rss_key_msg(struct idpf_vport *vport, bool get);
int idpf_send_get_set_rss_lut_msg(struct idpf_vport *vport, bool get);
int idpf_send_dealloc_vectors_msg(struct idpf_adapter *adapter);
int idpf_send_alloc_vectors_msg(struct idpf_adapter *adapter, u16 num_vectors);
void idpf_deinit_task(struct idpf_adapter *adapter);
int idpf_req_rel_vector_indexes(struct idpf_adapter *adapter,
				u16 *q_vector_idxs,
				struct idpf_vector_info *vec_info);
int idpf_vport_alloc_vec_indexes(struct idpf_vport *vport);
int idpf_send_get_stats_msg(struct idpf_vport *vport);
int idpf_get_vec_ids(struct idpf_adapter *adapter,
		     u16 *vecids, int num_vecids,
		     struct virtchnl2_vector_chunks *chunks);
int idpf_recv_mb_msg(struct idpf_adapter *adapter, u32 op,
		     void *msg, int msg_size);
int idpf_send_mb_msg(struct idpf_adapter *adapter, u32 op,
		     u16 msg_size, u8 *msg);
void idpf_set_ethtool_ops(struct net_device *netdev);
int idpf_vport_alloc_max_qs(struct idpf_adapter *adapter,
			    struct idpf_vport_max_q *max_q);
void idpf_vport_dealloc_max_qs(struct idpf_adapter *adapter,
			       struct idpf_vport_max_q *max_q);
int idpf_add_del_mac_filters(struct idpf_vport *vport,
			     struct idpf_netdev_priv *np,
			     bool add, bool async);
int idpf_set_promiscuous(struct idpf_adapter *adapter,
			 struct idpf_vport_user_config_data *config_data,
			 u32 vport_id);
int idpf_send_disable_queues_msg(struct idpf_vport *vport);
void idpf_vport_init(struct idpf_vport *vport, struct idpf_vport_max_q *max_q);
u32 idpf_get_vport_id(struct idpf_vport *vport);
int idpf_vport_queue_ids_init(struct idpf_vport *vport);
int idpf_queue_reg_init(struct idpf_vport *vport);
int idpf_send_config_queues_msg(struct idpf_vport *vport);
int idpf_send_enable_queues_msg(struct idpf_vport *vport);
int idpf_send_create_vport_msg(struct idpf_adapter *adapter,
			       struct idpf_vport_max_q *max_q);
int idpf_check_supported_desc_ids(struct idpf_vport *vport);
void idpf_vport_intr_write_itr(struct idpf_q_vector *q_vector,
			       u16 itr, bool tx);
int idpf_send_map_unmap_queue_vector_msg(struct idpf_vport *vport, bool map);
int idpf_send_set_sriov_vfs_msg(struct idpf_adapter *adapter, u16 num_vfs);
int idpf_sriov_configure(struct pci_dev *pdev, int num_vfs);

#endif /* !_IDPF_H_ */
