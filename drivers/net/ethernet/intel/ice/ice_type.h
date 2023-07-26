/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_TYPE_H_
#define _ICE_TYPE_H_

#define ICE_BYTES_PER_WORD	2
#define ICE_BYTES_PER_DWORD	4
#define ICE_CHNL_MAX_TC		16

#include "ice_hw_autogen.h"
#include "ice_devids.h"
#include "ice_osdep.h"
#include "ice_controlq.h"
#include "ice_lan_tx_rx.h"
#include "ice_flex_type.h"
#include "ice_protocol_type.h"
#include "ice_sbq_cmd.h"
#include "ice_vlan_mode.h"

static inline bool ice_is_tc_ena(unsigned long bitmap, u8 tc)
{
	return test_bit(tc, &bitmap);
}

static inline u64 round_up_64bit(u64 a, u32 b)
{
	return div64_long(((a) + (b) / 2), (b));
}

static inline u32 ice_round_to_num(u32 N, u32 R)
{
	return ((((N) % (R)) < ((R) / 2)) ? (((N) / (R)) * (R)) :
		((((N) + (R) - 1) / (R)) * (R)));
}

/* Driver always calls main vsi_handle first */
#define ICE_MAIN_VSI_HANDLE		0

/* debug masks - set these bits in hw->debug_mask to control output */
#define ICE_DBG_INIT		BIT_ULL(1)
#define ICE_DBG_FW_LOG		BIT_ULL(3)
#define ICE_DBG_LINK		BIT_ULL(4)
#define ICE_DBG_PHY		BIT_ULL(5)
#define ICE_DBG_QCTX		BIT_ULL(6)
#define ICE_DBG_NVM		BIT_ULL(7)
#define ICE_DBG_LAN		BIT_ULL(8)
#define ICE_DBG_FLOW		BIT_ULL(9)
#define ICE_DBG_SW		BIT_ULL(13)
#define ICE_DBG_SCHED		BIT_ULL(14)
#define ICE_DBG_RDMA		BIT_ULL(15)
#define ICE_DBG_PKG		BIT_ULL(16)
#define ICE_DBG_RES		BIT_ULL(17)
#define ICE_DBG_PTP		BIT_ULL(19)
#define ICE_DBG_AQ_MSG		BIT_ULL(24)
#define ICE_DBG_AQ_DESC		BIT_ULL(25)
#define ICE_DBG_AQ_DESC_BUF	BIT_ULL(26)
#define ICE_DBG_AQ_CMD		BIT_ULL(27)
#define ICE_DBG_AQ		(ICE_DBG_AQ_MSG		| \
				 ICE_DBG_AQ_DESC	| \
				 ICE_DBG_AQ_DESC_BUF	| \
				 ICE_DBG_AQ_CMD)

#define ICE_DBG_USER		BIT_ULL(31)

enum ice_aq_res_ids {
	ICE_NVM_RES_ID = 1,
	ICE_SPD_RES_ID,
	ICE_CHANGE_LOCK_RES_ID,
	ICE_GLOBAL_CFG_LOCK_RES_ID
};

/* FW update timeout definitions are in milliseconds */
#define ICE_NVM_TIMEOUT			180000
#define ICE_CHANGE_LOCK_TIMEOUT		1000
#define ICE_GLOBAL_CFG_LOCK_TIMEOUT	5000

enum ice_aq_res_access_type {
	ICE_RES_READ = 1,
	ICE_RES_WRITE
};

struct ice_driver_ver {
	u8 major_ver;
	u8 minor_ver;
	u8 build_ver;
	u8 subbuild_ver;
	u8 driver_string[32];
};

enum ice_fc_mode {
	ICE_FC_NONE = 0,
	ICE_FC_RX_PAUSE,
	ICE_FC_TX_PAUSE,
	ICE_FC_FULL,
	ICE_FC_PFC,
	ICE_FC_DFLT
};

enum ice_phy_cache_mode {
	ICE_FC_MODE = 0,
	ICE_SPEED_MODE,
	ICE_FEC_MODE
};

enum ice_fec_mode {
	ICE_FEC_NONE = 0,
	ICE_FEC_RS,
	ICE_FEC_BASER,
	ICE_FEC_AUTO
};

struct ice_phy_cache_mode_data {
	union {
		enum ice_fec_mode curr_user_fec_req;
		enum ice_fc_mode curr_user_fc_req;
		u16 curr_user_speed_req;
	} data;
};

enum ice_set_fc_aq_failures {
	ICE_SET_FC_AQ_FAIL_NONE = 0,
	ICE_SET_FC_AQ_FAIL_GET,
	ICE_SET_FC_AQ_FAIL_SET,
	ICE_SET_FC_AQ_FAIL_UPDATE
};

/* Various MAC types */
enum ice_mac_type {
	ICE_MAC_UNKNOWN = 0,
	ICE_MAC_E810,
	ICE_MAC_GENERIC,
};

/* Media Types */
enum ice_media_type {
	ICE_MEDIA_UNKNOWN = 0,
	ICE_MEDIA_FIBER,
	ICE_MEDIA_BASET,
	ICE_MEDIA_BACKPLANE,
	ICE_MEDIA_DA,
};

enum ice_vsi_type {
	ICE_VSI_PF = 0,
	ICE_VSI_VF = 1,
	ICE_VSI_CTRL = 3,	/* equates to ICE_VSI_PF with 1 queue pair */
	ICE_VSI_CHNL = 4,
	ICE_VSI_LB = 6,
	ICE_VSI_SWITCHDEV_CTRL = 7,
};

struct ice_link_status {
	/* Refer to ice_aq_phy_type for bits definition */
	u64 phy_type_low;
	u64 phy_type_high;
	u8 topo_media_conflict;
	u16 max_frame_size;
	u16 link_speed;
	u16 req_speeds;
	u8 link_cfg_err;
	u8 lse_ena;	/* Link Status Event notification */
	u8 link_info;
	u8 an_info;
	u8 ext_info;
	u8 fec_info;
	u8 pacing;
	/* Refer to #define from module_type[ICE_MODULE_TYPE_TOTAL_BYTE] of
	 * ice_aqc_get_phy_caps structure
	 */
	u8 module_type[ICE_MODULE_TYPE_TOTAL_BYTE];
};

/* Different reset sources for which a disable queue AQ call has to be made in
 * order to clean the Tx scheduler as a part of the reset
 */
enum ice_disq_rst_src {
	ICE_NO_RESET = 0,
	ICE_VM_RESET,
	ICE_VF_RESET,
};

/* PHY info such as phy_type, etc... */
struct ice_phy_info {
	struct ice_link_status link_info;
	struct ice_link_status link_info_old;
	u64 phy_type_low;
	u64 phy_type_high;
	enum ice_media_type media_type;
	u8 get_link_info;
	/* Please refer to struct ice_aqc_get_link_status_data to get
	 * detail of enable bit in curr_user_speed_req
	 */
	u16 curr_user_speed_req;
	enum ice_fec_mode curr_user_fec_req;
	enum ice_fc_mode curr_user_fc_req;
	struct ice_aqc_set_phy_cfg_data curr_user_phy_cfg;
};

/* protocol enumeration for filters */
enum ice_fltr_ptype {
	/* NONE - used for undef/error */
	ICE_FLTR_PTYPE_NONF_NONE = 0,
	ICE_FLTR_PTYPE_NONF_IPV4_UDP,
	ICE_FLTR_PTYPE_NONF_IPV4_TCP,
	ICE_FLTR_PTYPE_NONF_IPV4_SCTP,
	ICE_FLTR_PTYPE_NONF_IPV4_OTHER,
	ICE_FLTR_PTYPE_NONF_IPV4_GTPU_IPV4_UDP,
	ICE_FLTR_PTYPE_NONF_IPV4_GTPU_IPV4_TCP,
	ICE_FLTR_PTYPE_NONF_IPV4_GTPU_IPV4_ICMP,
	ICE_FLTR_PTYPE_NONF_IPV4_GTPU_IPV4_OTHER,
	ICE_FLTR_PTYPE_NONF_IPV6_GTPU_IPV6_OTHER,
	ICE_FLTR_PTYPE_NONF_IPV4_L2TPV3,
	ICE_FLTR_PTYPE_NONF_IPV6_L2TPV3,
	ICE_FLTR_PTYPE_NONF_IPV4_ESP,
	ICE_FLTR_PTYPE_NONF_IPV6_ESP,
	ICE_FLTR_PTYPE_NONF_IPV4_AH,
	ICE_FLTR_PTYPE_NONF_IPV6_AH,
	ICE_FLTR_PTYPE_NONF_IPV4_NAT_T_ESP,
	ICE_FLTR_PTYPE_NONF_IPV6_NAT_T_ESP,
	ICE_FLTR_PTYPE_NONF_IPV4_PFCP_NODE,
	ICE_FLTR_PTYPE_NONF_IPV4_PFCP_SESSION,
	ICE_FLTR_PTYPE_NONF_IPV6_PFCP_NODE,
	ICE_FLTR_PTYPE_NONF_IPV6_PFCP_SESSION,
	ICE_FLTR_PTYPE_NON_IP_L2,
	ICE_FLTR_PTYPE_FRAG_IPV4,
	ICE_FLTR_PTYPE_NONF_IPV6_UDP,
	ICE_FLTR_PTYPE_NONF_IPV6_TCP,
	ICE_FLTR_PTYPE_NONF_IPV6_SCTP,
	ICE_FLTR_PTYPE_NONF_IPV6_OTHER,
	ICE_FLTR_PTYPE_MAX,
};

enum ice_fd_hw_seg {
	ICE_FD_HW_SEG_NON_TUN = 0,
	ICE_FD_HW_SEG_TUN,
	ICE_FD_HW_SEG_MAX,
};

/* 1 ICE_VSI_PF + 1 ICE_VSI_CTRL + ICE_CHNL_MAX_TC */
#define ICE_MAX_FDIR_VSI_PER_FILTER	(2 + ICE_CHNL_MAX_TC)

struct ice_fd_hw_prof {
	struct ice_flow_seg_info *fdir_seg[ICE_FD_HW_SEG_MAX];
	int cnt;
	u64 entry_h[ICE_MAX_FDIR_VSI_PER_FILTER][ICE_FD_HW_SEG_MAX];
	u16 vsi_h[ICE_MAX_FDIR_VSI_PER_FILTER];
};

/* Common HW capabilities for SW use */
struct ice_hw_common_caps {
	u32 valid_functions;
	/* DCB capabilities */
	u32 active_tc_bitmap;
	u32 maxtc;

	/* Tx/Rx queues */
	u16 num_rxq;		/* Number/Total Rx queues */
	u16 rxq_first_id;	/* First queue ID for Rx queues */
	u16 num_txq;		/* Number/Total Tx queues */
	u16 txq_first_id;	/* First queue ID for Tx queues */

	/* MSI-X vectors */
	u16 num_msix_vectors;
	u16 msix_vector_first_id;

	/* Max MTU for function or device */
	u16 max_mtu;

	/* Virtualization support */
	u8 sr_iov_1_1;			/* SR-IOV enabled */

	/* RSS related capabilities */
	u16 rss_table_size;		/* 512 for PFs and 64 for VFs */
	u8 rss_table_entry_width;	/* RSS Entry width in bits */

	u8 dcb;
	u8 ieee_1588;
	u8 rdma;
	u8 roce_lag;
	u8 sriov_lag;

	bool nvm_update_pending_nvm;
	bool nvm_update_pending_orom;
	bool nvm_update_pending_netlist;
#define ICE_NVM_PENDING_NVM_IMAGE		BIT(0)
#define ICE_NVM_PENDING_OROM			BIT(1)
#define ICE_NVM_PENDING_NETLIST			BIT(2)
	bool nvm_unified_update;
#define ICE_NVM_MGMT_UNIFIED_UPD_SUPPORT	BIT(3)
	/* PCIe reset avoidance */
	bool pcie_reset_avoidance;
	/* Post update reset restriction */
	bool reset_restrict_support;
};

/* IEEE 1588 TIME_SYNC specific info */
/* Function specific definitions */
#define ICE_TS_FUNC_ENA_M		BIT(0)
#define ICE_TS_SRC_TMR_OWND_M		BIT(1)
#define ICE_TS_TMR_ENA_M		BIT(2)
#define ICE_TS_TMR_IDX_OWND_S		4
#define ICE_TS_TMR_IDX_OWND_M		BIT(4)
#define ICE_TS_CLK_FREQ_S		16
#define ICE_TS_CLK_FREQ_M		ICE_M(0x7, ICE_TS_CLK_FREQ_S)
#define ICE_TS_CLK_SRC_S		20
#define ICE_TS_CLK_SRC_M		BIT(20)
#define ICE_TS_TMR_IDX_ASSOC_S		24
#define ICE_TS_TMR_IDX_ASSOC_M		BIT(24)

/* TIME_REF clock rate specification */
enum ice_time_ref_freq {
	ICE_TIME_REF_FREQ_25_000	= 0,
	ICE_TIME_REF_FREQ_122_880	= 1,
	ICE_TIME_REF_FREQ_125_000	= 2,
	ICE_TIME_REF_FREQ_153_600	= 3,
	ICE_TIME_REF_FREQ_156_250	= 4,
	ICE_TIME_REF_FREQ_245_760	= 5,

	NUM_ICE_TIME_REF_FREQ
};

/* Clock source specification */
enum ice_clk_src {
	ICE_CLK_SRC_TCX0	= 0, /* Temperature compensated oscillator  */
	ICE_CLK_SRC_TIME_REF	= 1, /* Use TIME_REF reference clock */

	NUM_ICE_CLK_SRC
};

struct ice_ts_func_info {
	/* Function specific info */
	enum ice_time_ref_freq time_ref;
	u8 clk_freq;
	u8 clk_src;
	u8 tmr_index_assoc;
	u8 ena;
	u8 tmr_index_owned;
	u8 src_tmr_owned;
	u8 tmr_ena;
};

/* Device specific definitions */
#define ICE_TS_TMR0_OWNR_M		0x7
#define ICE_TS_TMR0_OWND_M		BIT(3)
#define ICE_TS_TMR1_OWNR_S		4
#define ICE_TS_TMR1_OWNR_M		ICE_M(0x7, ICE_TS_TMR1_OWNR_S)
#define ICE_TS_TMR1_OWND_M		BIT(7)
#define ICE_TS_DEV_ENA_M		BIT(24)
#define ICE_TS_TMR0_ENA_M		BIT(25)
#define ICE_TS_TMR1_ENA_M		BIT(26)
#define ICE_TS_LL_TX_TS_READ_M		BIT(28)

struct ice_ts_dev_info {
	/* Device specific info */
	u32 ena_ports;
	u32 tmr_own_map;
	u32 tmr0_owner;
	u32 tmr1_owner;
	u8 tmr0_owned;
	u8 tmr1_owned;
	u8 ena;
	u8 tmr0_ena;
	u8 tmr1_ena;
	u8 ts_ll_read;
};

/* Function specific capabilities */
struct ice_hw_func_caps {
	struct ice_hw_common_caps common_cap;
	u32 num_allocd_vfs;		/* Number of allocated VFs */
	u32 vf_base_id;			/* Logical ID of the first VF */
	u32 guar_num_vsi;
	u32 fd_fltr_guar;		/* Number of filters guaranteed */
	u32 fd_fltr_best_effort;	/* Number of best effort filters */
	struct ice_ts_func_info ts_func_info;
};

/* Device wide capabilities */
struct ice_hw_dev_caps {
	struct ice_hw_common_caps common_cap;
	u32 num_vfs_exposed;		/* Total number of VFs exposed */
	u32 num_vsi_allocd_to_host;	/* Excluding EMP VSI */
	u32 num_flow_director_fltr;	/* Number of FD filters available */
	struct ice_ts_dev_info ts_dev_info;
	u32 num_funcs;
};

/* MAC info */
struct ice_mac_info {
	u8 lan_addr[ETH_ALEN];
	u8 perm_addr[ETH_ALEN];
};

/* Reset types used to determine which kind of reset was requested. These
 * defines match what the RESET_TYPE field of the GLGEN_RSTAT register.
 * ICE_RESET_PFR does not match any RESET_TYPE field in the GLGEN_RSTAT register
 * because its reset source is different than the other types listed.
 */
enum ice_reset_req {
	ICE_RESET_POR	= 0,
	ICE_RESET_INVAL	= 0,
	ICE_RESET_CORER	= 1,
	ICE_RESET_GLOBR	= 2,
	ICE_RESET_EMPR	= 3,
	ICE_RESET_PFR	= 4,
};

/* Bus parameters */
struct ice_bus_info {
	u16 device;
	u8 func;
};

/* Flow control (FC) parameters */
struct ice_fc_info {
	enum ice_fc_mode current_mode;	/* FC mode in effect */
	enum ice_fc_mode req_mode;	/* FC mode requested by caller */
};

/* Option ROM version information */
struct ice_orom_info {
	u8 major;			/* Major version of OROM */
	u8 patch;			/* Patch version of OROM */
	u16 build;			/* Build version of OROM */
};

/* NVM version information */
struct ice_nvm_info {
	u32 eetrack;
	u8 major;
	u8 minor;
};

/* netlist version information */
struct ice_netlist_info {
	u32 major;			/* major high/low */
	u32 minor;			/* minor high/low */
	u32 type;			/* type high/low */
	u32 rev;			/* revision high/low */
	u32 hash;			/* SHA-1 hash word */
	u16 cust_ver;			/* customer version */
};

/* Enumeration of possible flash banks for the NVM, OROM, and Netlist modules
 * of the flash image.
 */
enum ice_flash_bank {
	ICE_INVALID_FLASH_BANK,
	ICE_1ST_FLASH_BANK,
	ICE_2ND_FLASH_BANK,
};

/* Enumeration of which flash bank is desired to read from, either the active
 * bank or the inactive bank. Used to abstract 1st and 2nd bank notion from
 * code which just wants to read the active or inactive flash bank.
 */
enum ice_bank_select {
	ICE_ACTIVE_FLASH_BANK,
	ICE_INACTIVE_FLASH_BANK,
};

/* information for accessing NVM, OROM, and Netlist flash banks */
struct ice_bank_info {
	u32 nvm_ptr;				/* Pointer to 1st NVM bank */
	u32 nvm_size;				/* Size of NVM bank */
	u32 orom_ptr;				/* Pointer to 1st OROM bank */
	u32 orom_size;				/* Size of OROM bank */
	u32 netlist_ptr;			/* Pointer to 1st Netlist bank */
	u32 netlist_size;			/* Size of Netlist bank */
	enum ice_flash_bank nvm_bank;		/* Active NVM bank */
	enum ice_flash_bank orom_bank;		/* Active OROM bank */
	enum ice_flash_bank netlist_bank;	/* Active Netlist bank */
};

/* Flash Chip Information */
struct ice_flash_info {
	struct ice_orom_info orom;	/* Option ROM version info */
	struct ice_nvm_info nvm;	/* NVM version information */
	struct ice_netlist_info netlist;/* Netlist version info */
	struct ice_bank_info banks;	/* Flash Bank information */
	u16 sr_words;			/* Shadow RAM size in words */
	u32 flash_size;			/* Size of available flash in bytes */
	u8 blank_nvm_mode;		/* is NVM empty (no FW present) */
};

struct ice_link_default_override_tlv {
	u8 options;
#define ICE_LINK_OVERRIDE_OPT_M		0x3F
#define ICE_LINK_OVERRIDE_STRICT_MODE	BIT(0)
#define ICE_LINK_OVERRIDE_EPCT_DIS	BIT(1)
#define ICE_LINK_OVERRIDE_PORT_DIS	BIT(2)
#define ICE_LINK_OVERRIDE_EN		BIT(3)
#define ICE_LINK_OVERRIDE_AUTO_LINK_DIS	BIT(4)
#define ICE_LINK_OVERRIDE_EEE_EN	BIT(5)
	u8 phy_config;
#define ICE_LINK_OVERRIDE_PHY_CFG_S	8
#define ICE_LINK_OVERRIDE_PHY_CFG_M	(0xC3 << ICE_LINK_OVERRIDE_PHY_CFG_S)
#define ICE_LINK_OVERRIDE_PAUSE_M	0x3
#define ICE_LINK_OVERRIDE_LESM_EN	BIT(6)
#define ICE_LINK_OVERRIDE_AUTO_FEC_EN	BIT(7)
	u8 fec_options;
#define ICE_LINK_OVERRIDE_FEC_OPT_M	0xFF
	u8 rsvd1;
	u64 phy_type_low;
	u64 phy_type_high;
};

#define ICE_NVM_VER_LEN	32

/* Max number of port to queue branches w.r.t topology */
#define ICE_MAX_TRAFFIC_CLASS 8
#define ICE_TXSCHED_MAX_BRANCHES ICE_MAX_TRAFFIC_CLASS

#define ice_for_each_traffic_class(_i)	\
	for ((_i) = 0; (_i) < ICE_MAX_TRAFFIC_CLASS; (_i)++)

/* ICE_DFLT_AGG_ID means that all new VM(s)/VSI node connects
 * to driver defined policy for default aggregator
 */
#define ICE_INVAL_TEID 0xFFFFFFFF
#define ICE_DFLT_AGG_ID 0

struct ice_sched_node {
	struct ice_sched_node *parent;
	struct ice_sched_node *sibling; /* next sibling in the same layer */
	struct ice_sched_node **children;
	struct ice_aqc_txsched_elem_data info;
	char *name;
	struct devlink_rate *rate_node;
	u64 tx_max;
	u64 tx_share;
	u32 agg_id;			/* aggregator group ID */
	u32 id;
	u32 tx_priority;
	u32 tx_weight;
	u16 vsi_handle;
	u8 in_use;			/* suspended or in use */
	u8 tx_sched_layer;		/* Logical Layer (1-9) */
	u8 num_children;
	u8 tc_num;
	u8 owner;
#define ICE_SCHED_NODE_OWNER_LAN	0
#define ICE_SCHED_NODE_OWNER_RDMA	2
};

/* Access Macros for Tx Sched Elements data */
#define ICE_TXSCHED_GET_NODE_TEID(x) le32_to_cpu((x)->info.node_teid)

/* The aggregator type determines if identifier is for a VSI group,
 * aggregator group, aggregator of queues, or queue group.
 */
enum ice_agg_type {
	ICE_AGG_TYPE_UNKNOWN = 0,
	ICE_AGG_TYPE_VSI,
	ICE_AGG_TYPE_AGG, /* aggregator */
	ICE_AGG_TYPE_Q,
	ICE_AGG_TYPE_QG
};

/* Rate limit types */
enum ice_rl_type {
	ICE_UNKNOWN_BW = 0,
	ICE_MIN_BW,		/* for CIR profile */
	ICE_MAX_BW,		/* for EIR profile */
	ICE_SHARED_BW		/* for shared profile */
};

#define ICE_SCHED_MIN_BW		500		/* in Kbps */
#define ICE_SCHED_MAX_BW		100000000	/* in Kbps */
#define ICE_SCHED_DFLT_BW		0xFFFFFFFF	/* unlimited */
#define ICE_SCHED_DFLT_RL_PROF_ID	0
#define ICE_SCHED_NO_SHARED_RL_PROF_ID	0xFFFF
#define ICE_SCHED_DFLT_BW_WT		4
#define ICE_SCHED_INVAL_PROF_ID		0xFFFF
#define ICE_SCHED_DFLT_BURST_SIZE	(15 * 1024)	/* in bytes (15k) */

#define ICE_MAX_PORT_PER_PCI_DEV 8

 /* Data structure for saving BW information */
enum ice_bw_type {
	ICE_BW_TYPE_PRIO,
	ICE_BW_TYPE_CIR,
	ICE_BW_TYPE_CIR_WT,
	ICE_BW_TYPE_EIR,
	ICE_BW_TYPE_EIR_WT,
	ICE_BW_TYPE_SHARED,
	ICE_BW_TYPE_CNT		/* This must be last */
};

struct ice_bw {
	u32 bw;
	u16 bw_alloc;
};

struct ice_bw_type_info {
	DECLARE_BITMAP(bw_t_bitmap, ICE_BW_TYPE_CNT);
	u8 generic;
	struct ice_bw cir_bw;
	struct ice_bw eir_bw;
	u32 shared_bw;
};

/* VSI queue context structure for given TC */
struct ice_q_ctx {
	u16  q_handle;
	u32  q_teid;
	/* bw_t_info saves queue BW information */
	struct ice_bw_type_info bw_t_info;
};

/* VSI type list entry to locate corresponding VSI/aggregator nodes */
struct ice_sched_vsi_info {
	struct ice_sched_node *vsi_node[ICE_MAX_TRAFFIC_CLASS];
	struct ice_sched_node *ag_node[ICE_MAX_TRAFFIC_CLASS];
	struct list_head list_entry;
	u16 max_lanq[ICE_MAX_TRAFFIC_CLASS];
	u16 max_rdmaq[ICE_MAX_TRAFFIC_CLASS];
	/* bw_t_info saves VSI BW information */
	struct ice_bw_type_info bw_t_info[ICE_MAX_TRAFFIC_CLASS];
};

/* driver defines the policy */
struct ice_sched_tx_policy {
	u16 max_num_vsis;
	u8 max_num_lan_qs_per_tc[ICE_MAX_TRAFFIC_CLASS];
	u8 rdma_ena;
};

/* CEE or IEEE 802.1Qaz ETS Configuration data */
struct ice_dcb_ets_cfg {
	u8 willing;
	u8 cbs;
	u8 maxtcs;
	u8 prio_table[ICE_MAX_TRAFFIC_CLASS];
	u8 tcbwtable[ICE_MAX_TRAFFIC_CLASS];
	u8 tsatable[ICE_MAX_TRAFFIC_CLASS];
};

/* CEE or IEEE 802.1Qaz PFC Configuration data */
struct ice_dcb_pfc_cfg {
	u8 willing;
	u8 mbc;
	u8 pfccap;
	u8 pfcena;
};

/* CEE or IEEE 802.1Qaz Application Priority data */
struct ice_dcb_app_priority_table {
	u16 prot_id;
	u8 priority;
	u8 selector;
};

#define ICE_MAX_USER_PRIORITY	8
#define ICE_DCBX_MAX_APPS	64
#define ICE_DSCP_NUM_VAL	64
#define ICE_LLDPDU_SIZE		1500
#define ICE_TLV_STATUS_OPER	0x1
#define ICE_TLV_STATUS_SYNC	0x2
#define ICE_TLV_STATUS_ERR	0x4
#define ICE_APP_PROT_ID_ISCSI_860 0x035c
#define ICE_APP_SEL_ETHTYPE	0x1
#define ICE_APP_SEL_TCPIP	0x2
#define ICE_CEE_APP_SEL_ETHTYPE	0x0
#define ICE_SR_LINK_DEFAULT_OVERRIDE_PTR	0x134
#define ICE_CEE_APP_SEL_TCPIP	0x1

struct ice_dcbx_cfg {
	u32 numapps;
	u32 tlv_status; /* CEE mode TLV status */
	struct ice_dcb_ets_cfg etscfg;
	struct ice_dcb_ets_cfg etsrec;
	struct ice_dcb_pfc_cfg pfc;
#define ICE_QOS_MODE_VLAN	0x0
#define ICE_QOS_MODE_DSCP	0x1
	u8 pfc_mode;
	struct ice_dcb_app_priority_table app[ICE_DCBX_MAX_APPS];
	/* when DSCP mapping defined by user set its bit to 1 */
	DECLARE_BITMAP(dscp_mapped, ICE_DSCP_NUM_VAL);
	/* array holding DSCP -> UP/TC values for DSCP L3 QoS mode */
	u8 dscp_map[ICE_DSCP_NUM_VAL];
	u8 dcbx_mode;
#define ICE_DCBX_MODE_CEE	0x1
#define ICE_DCBX_MODE_IEEE	0x2
	u8 app_mode;
#define ICE_DCBX_APPS_NON_WILLING	0x1
};

struct ice_qos_cfg {
	struct ice_dcbx_cfg local_dcbx_cfg;	/* Oper/Local Cfg */
	struct ice_dcbx_cfg desired_dcbx_cfg;	/* CEE Desired Cfg */
	struct ice_dcbx_cfg remote_dcbx_cfg;	/* Peer Cfg */
	u8 dcbx_status : 3;			/* see ICE_DCBX_STATUS_DIS */
	u8 is_sw_lldp : 1;
};

struct ice_port_info {
	struct ice_sched_node *root;	/* Root Node per Port */
	struct ice_hw *hw;		/* back pointer to HW instance */
	u32 last_node_teid;		/* scheduler last node info */
	u16 sw_id;			/* Initial switch ID belongs to port */
	u16 pf_vf_num;
	u8 port_state;
#define ICE_SCHED_PORT_STATE_INIT	0x0
#define ICE_SCHED_PORT_STATE_READY	0x1
	u8 lport;
#define ICE_LPORT_MASK			0xff
	struct ice_fc_info fc;
	struct ice_mac_info mac;
	struct ice_phy_info phy;
	struct mutex sched_lock;	/* protect access to TXSched tree */
	struct ice_sched_node *
		sib_head[ICE_MAX_TRAFFIC_CLASS][ICE_AQC_TOPO_MAX_LEVEL_NUM];
	/* List contain profile ID(s) and other params per layer */
	struct list_head rl_prof_list[ICE_AQC_TOPO_MAX_LEVEL_NUM];
	struct ice_qos_cfg qos_cfg;
	struct xarray sched_node_ids;
	u8 is_vf:1;
	u8 is_custom_tx_enabled:1;
};

struct ice_switch_info {
	struct list_head vsi_list_map_head;
	struct ice_sw_recipe *recp_list;
	u16 prof_res_bm_init;
	u16 max_used_prof_index;

	DECLARE_BITMAP(prof_res_bm[ICE_MAX_NUM_PROFILES], ICE_MAX_FV_WORDS);
};

/* FW logging configuration */
struct ice_fw_log_evnt {
	u8 cfg : 4;	/* New event enables to configure */
	u8 cur : 4;	/* Current/active event enables */
};

struct ice_fw_log_cfg {
	u8 cq_en : 1;    /* FW logging is enabled via the control queue */
	u8 uart_en : 1;  /* FW logging is enabled via UART for all PFs */
	u8 actv_evnts;   /* Cumulation of currently enabled log events */

#define ICE_FW_LOG_EVNT_INFO	(ICE_AQC_FW_LOG_INFO_EN >> ICE_AQC_FW_LOG_EN_S)
#define ICE_FW_LOG_EVNT_INIT	(ICE_AQC_FW_LOG_INIT_EN >> ICE_AQC_FW_LOG_EN_S)
#define ICE_FW_LOG_EVNT_FLOW	(ICE_AQC_FW_LOG_FLOW_EN >> ICE_AQC_FW_LOG_EN_S)
#define ICE_FW_LOG_EVNT_ERR	(ICE_AQC_FW_LOG_ERR_EN >> ICE_AQC_FW_LOG_EN_S)
	struct ice_fw_log_evnt evnts[ICE_AQC_FW_LOG_ID_MAX];
};

/* Enum defining the different states of the mailbox snapshot in the
 * PF-VF mailbox overflow detection algorithm. The snapshot can be in
 * states:
 * 1. ICE_MAL_VF_DETECT_STATE_NEW_SNAPSHOT - generate a new static snapshot
 * within the mailbox buffer.
 * 2. ICE_MAL_VF_DETECT_STATE_TRAVERSE - iterate through the mailbox snaphot
 * 3. ICE_MAL_VF_DETECT_STATE_DETECT - track the messages sent per VF via the
 * mailbox and mark any VFs sending more messages than the threshold limit set.
 * 4. ICE_MAL_VF_DETECT_STATE_INVALID - Invalid mailbox state set to 0xFFFFFFFF.
 */
enum ice_mbx_snapshot_state {
	ICE_MAL_VF_DETECT_STATE_NEW_SNAPSHOT = 0,
	ICE_MAL_VF_DETECT_STATE_TRAVERSE,
	ICE_MAL_VF_DETECT_STATE_DETECT,
	ICE_MAL_VF_DETECT_STATE_INVALID = 0xFFFFFFFF,
};

/* Structure to hold information of the static snapshot and the mailbox
 * buffer data used to generate and track the snapshot.
 * 1. state: the state of the mailbox snapshot in the malicious VF
 * detection state handler ice_mbx_vf_state_handler()
 * 2. head: head of the mailbox snapshot in a circular mailbox buffer
 * 3. tail: tail of the mailbox snapshot in a circular mailbox buffer
 * 4. num_iterations: number of messages traversed in circular mailbox buffer
 * 5. num_msg_proc: number of messages processed in mailbox
 * 6. num_pending_arq: number of pending asynchronous messages
 * 7. max_num_msgs_mbx: maximum messages in mailbox for currently
 * serviced work item or interrupt.
 */
struct ice_mbx_snap_buffer_data {
	enum ice_mbx_snapshot_state state;
	u32 head;
	u32 tail;
	u32 num_iterations;
	u16 num_msg_proc;
	u16 num_pending_arq;
	u16 max_num_msgs_mbx;
};

/* Structure used to track a single VF's messages on the mailbox:
 * 1. list_entry: linked list entry node
 * 2. msg_count: the number of asynchronous messages sent by this VF
 * 3. malicious: whether this VF has been detected as malicious before
 */
struct ice_mbx_vf_info {
	struct list_head list_entry;
	u32 msg_count;
	u8 malicious : 1;
};

/* Structure to hold data relevant to the captured static snapshot
 * of the PF-VF mailbox.
 */
struct ice_mbx_snapshot {
	struct ice_mbx_snap_buffer_data mbx_buf;
	struct list_head mbx_vf;
};

/* Structure to hold data to be used for capturing or updating a
 * static snapshot.
 * 1. num_msg_proc: number of messages processed in mailbox
 * 2. num_pending_arq: number of pending asynchronous messages
 * 3. max_num_msgs_mbx: maximum messages in mailbox for currently
 * serviced work item or interrupt.
 * 4. async_watermark_val: An upper threshold set by caller to determine
 * if the pending arq count is large enough to assume that there is
 * the possibility of a mailicious VF.
 */
struct ice_mbx_data {
	u16 num_msg_proc;
	u16 num_pending_arq;
	u16 max_num_msgs_mbx;
	u16 async_watermark_val;
};

/* PHY model */
enum ice_phy_model {
	ICE_PHY_UNSUP = -1,
	ICE_PHY_E810  = 1,
	ICE_PHY_E822,
};

/* Port hardware description */
struct ice_hw {
	u8 __iomem *hw_addr;
	void *back;
	struct ice_aqc_layer_props *layer_info;
	struct ice_port_info *port_info;
	/* PSM clock frequency for calculating RL profile params */
	u32 psm_clk_freq;
	u64 debug_mask;		/* bitmap for debug mask */
	enum ice_mac_type mac_type;

	u16 fd_ctr_base;	/* FD counter base index */

	/* pci info */
	u16 device_id;
	u16 vendor_id;
	u16 subsystem_device_id;
	u16 subsystem_vendor_id;
	u8 revision_id;

	u8 pf_id;		/* device profile info */
	enum ice_phy_model phy_model;

	u16 max_burst_size;	/* driver sets this value */

	/* Tx Scheduler values */
	u8 num_tx_sched_layers;
	u8 num_tx_sched_phys_layers;
	u8 flattened_layers;
	u8 max_cgds;
	u8 sw_entry_point_layer;
	u16 max_children[ICE_AQC_TOPO_MAX_LEVEL_NUM];
	struct list_head agg_list;	/* lists all aggregator */

	struct ice_vsi_ctx *vsi_ctx[ICE_MAX_VSI];
	u8 evb_veb;		/* true for VEB, false for VEPA */
	u8 reset_ongoing;	/* true if HW is in reset, false otherwise */
	struct ice_bus_info bus;
	struct ice_flash_info flash;
	struct ice_hw_dev_caps dev_caps;	/* device capabilities */
	struct ice_hw_func_caps func_caps;	/* function capabilities */

	struct ice_switch_info *switch_info;	/* switch filter lists */

	/* Control Queue info */
	struct ice_ctl_q_info adminq;
	struct ice_ctl_q_info sbq;
	struct ice_ctl_q_info mailboxq;

	u8 api_branch;		/* API branch version */
	u8 api_maj_ver;		/* API major version */
	u8 api_min_ver;		/* API minor version */
	u8 api_patch;		/* API patch version */
	u8 fw_branch;		/* firmware branch version */
	u8 fw_maj_ver;		/* firmware major version */
	u8 fw_min_ver;		/* firmware minor version */
	u8 fw_patch;		/* firmware patch version */
	u32 fw_build;		/* firmware build number */

	struct ice_fw_log_cfg fw_log;

/* Device max aggregate bandwidths corresponding to the GL_PWR_MODE_CTL
 * register. Used for determining the ITR/INTRL granularity during
 * initialization.
 */
#define ICE_MAX_AGG_BW_200G	0x0
#define ICE_MAX_AGG_BW_100G	0X1
#define ICE_MAX_AGG_BW_50G	0x2
#define ICE_MAX_AGG_BW_25G	0x3
	/* ITR granularity for different speeds */
#define ICE_ITR_GRAN_ABOVE_25	2
#define ICE_ITR_GRAN_MAX_25	4
	/* ITR granularity in 1 us */
	u8 itr_gran;
	/* INTRL granularity for different speeds */
#define ICE_INTRL_GRAN_ABOVE_25	4
#define ICE_INTRL_GRAN_MAX_25	8
	/* INTRL granularity in 1 us */
	u8 intrl_gran;

#define ICE_PHY_PER_NAC_E822		1
#define ICE_MAX_QUAD			2
#define ICE_QUADS_PER_PHY_E822		2
#define ICE_PORTS_PER_PHY_E822		8
#define ICE_PORTS_PER_QUAD		4
#define ICE_PORTS_PER_PHY_E810		4
#define ICE_NUM_EXTERNAL_PORTS		(ICE_MAX_QUAD * ICE_PORTS_PER_QUAD)

	/* Active package version (currently active) */
	struct ice_pkg_ver active_pkg_ver;
	u32 active_track_id;
	u8 active_pkg_name[ICE_PKG_NAME_SIZE];
	u8 active_pkg_in_nvm;

	/* Driver's package ver - (from the Ice Metadata section) */
	struct ice_pkg_ver pkg_ver;
	u8 pkg_name[ICE_PKG_NAME_SIZE];

	/* Driver's Ice segment format version and ID (from the Ice seg) */
	struct ice_pkg_ver ice_seg_fmt_ver;
	u8 ice_seg_id[ICE_SEG_ID_SIZE];

	/* Pointer to the ice segment */
	struct ice_seg *seg;

	/* Pointer to allocated copy of pkg memory */
	u8 *pkg_copy;
	u32 pkg_size;

	/* tunneling info */
	struct mutex tnl_lock;
	struct ice_tunnel_table tnl;

	struct udp_tunnel_nic_shared udp_tunnel_shared;
	struct udp_tunnel_nic_info udp_tunnel_nic;

	/* dvm boost update information */
	struct ice_dvm_table dvm_upd;

	/* HW block tables */
	struct ice_blk_info blk[ICE_BLK_COUNT];
	struct mutex fl_profs_locks[ICE_BLK_COUNT];	/* lock fltr profiles */
	struct list_head fl_profs[ICE_BLK_COUNT];

	/* Flow Director filter info */
	int fdir_active_fltr;

	struct mutex fdir_fltr_lock;	/* protect Flow Director */
	struct list_head fdir_list_head;

	/* Book-keeping of side-band filter count per flow-type.
	 * This is used to detect and handle input set changes for
	 * respective flow-type.
	 */
	u16 fdir_fltr_cnt[ICE_FLTR_PTYPE_MAX];

	struct ice_fd_hw_prof **fdir_prof;
	DECLARE_BITMAP(fdir_perfect_fltr, ICE_FLTR_PTYPE_MAX);
	struct mutex rss_locks;	/* protect RSS configuration */
	struct list_head rss_list_head;
	struct ice_mbx_snapshot mbx_snapshot;
	DECLARE_BITMAP(hw_ptype, ICE_FLOW_PTYPE_MAX);
	u8 dvm_ena;
	u16 io_expander_handle;
	u8 cgu_part_number;
};

/* Statistics collected by each port, VSI, VEB, and S-channel */
struct ice_eth_stats {
	u64 rx_bytes;			/* gorc */
	u64 rx_unicast;			/* uprc */
	u64 rx_multicast;		/* mprc */
	u64 rx_broadcast;		/* bprc */
	u64 rx_discards;		/* rdpc */
	u64 rx_unknown_protocol;	/* rupp */
	u64 tx_bytes;			/* gotc */
	u64 tx_unicast;			/* uptc */
	u64 tx_multicast;		/* mptc */
	u64 tx_broadcast;		/* bptc */
	u64 tx_discards;		/* tdpc */
	u64 tx_errors;			/* tepc */
};

#define ICE_MAX_UP	8

/* Statistics collected by the MAC */
struct ice_hw_port_stats {
	/* eth stats collected by the port */
	struct ice_eth_stats eth;
	/* additional port specific stats */
	u64 tx_dropped_link_down;	/* tdold */
	u64 crc_errors;			/* crcerrs */
	u64 illegal_bytes;		/* illerrc */
	u64 error_bytes;		/* errbc */
	u64 mac_local_faults;		/* mlfc */
	u64 mac_remote_faults;		/* mrfc */
	u64 rx_len_errors;		/* rlec */
	u64 link_xon_rx;		/* lxonrxc */
	u64 link_xoff_rx;		/* lxoffrxc */
	u64 link_xon_tx;		/* lxontxc */
	u64 link_xoff_tx;		/* lxofftxc */
	u64 priority_xon_rx[8];		/* pxonrxc[8] */
	u64 priority_xoff_rx[8];	/* pxoffrxc[8] */
	u64 priority_xon_tx[8];		/* pxontxc[8] */
	u64 priority_xoff_tx[8];	/* pxofftxc[8] */
	u64 priority_xon_2_xoff[8];	/* pxon2offc[8] */
	u64 rx_size_64;			/* prc64 */
	u64 rx_size_127;		/* prc127 */
	u64 rx_size_255;		/* prc255 */
	u64 rx_size_511;		/* prc511 */
	u64 rx_size_1023;		/* prc1023 */
	u64 rx_size_1522;		/* prc1522 */
	u64 rx_size_big;		/* prc9522 */
	u64 rx_undersize;		/* ruc */
	u64 rx_fragments;		/* rfc */
	u64 rx_oversize;		/* roc */
	u64 rx_jabber;			/* rjc */
	u64 tx_size_64;			/* ptc64 */
	u64 tx_size_127;		/* ptc127 */
	u64 tx_size_255;		/* ptc255 */
	u64 tx_size_511;		/* ptc511 */
	u64 tx_size_1023;		/* ptc1023 */
	u64 tx_size_1522;		/* ptc1522 */
	u64 tx_size_big;		/* ptc9522 */
	/* flow director stats */
	u32 fd_sb_status;
	u64 fd_sb_match;
};

enum ice_sw_fwd_act_type {
	ICE_FWD_TO_VSI = 0,
	ICE_FWD_TO_VSI_LIST, /* Do not use this when adding filter */
	ICE_FWD_TO_Q,
	ICE_FWD_TO_QGRP,
	ICE_DROP_PACKET,
	ICE_NOP,
	ICE_INVAL_ACT
};

struct ice_aq_get_set_rss_lut_params {
	u8 *lut;		/* input RSS LUT for set and output RSS LUT for get */
	enum ice_lut_size lut_size; /* size of the LUT buffer */
	enum ice_lut_type lut_type; /* type of the LUT (i.e. VSI, PF, Global) */
	u16 vsi_handle;		/* software VSI handle */
	u8 global_lut_id;	/* only valid when lut_type is global */
};

/* Checksum and Shadow RAM pointers */
#define ICE_SR_NVM_CTRL_WORD		0x00
#define ICE_SR_BOOT_CFG_PTR		0x132
#define ICE_SR_NVM_WOL_CFG		0x19
#define ICE_NVM_OROM_VER_OFF		0x02
#define ICE_SR_PBA_BLOCK_PTR		0x16
#define ICE_SR_NVM_DEV_STARTER_VER	0x18
#define ICE_SR_NVM_EETRACK_LO		0x2D
#define ICE_SR_NVM_EETRACK_HI		0x2E
#define ICE_NVM_VER_LO_SHIFT		0
#define ICE_NVM_VER_LO_MASK		(0xff << ICE_NVM_VER_LO_SHIFT)
#define ICE_NVM_VER_HI_SHIFT		12
#define ICE_NVM_VER_HI_MASK		(0xf << ICE_NVM_VER_HI_SHIFT)
#define ICE_OROM_VER_PATCH_SHIFT	0
#define ICE_OROM_VER_PATCH_MASK		(0xff << ICE_OROM_VER_PATCH_SHIFT)
#define ICE_OROM_VER_BUILD_SHIFT	8
#define ICE_OROM_VER_BUILD_MASK		(0xffff << ICE_OROM_VER_BUILD_SHIFT)
#define ICE_OROM_VER_SHIFT		24
#define ICE_OROM_VER_MASK		(0xff << ICE_OROM_VER_SHIFT)
#define ICE_SR_PFA_PTR			0x40
#define ICE_SR_1ST_NVM_BANK_PTR		0x42
#define ICE_SR_NVM_BANK_SIZE		0x43
#define ICE_SR_1ST_OROM_BANK_PTR	0x44
#define ICE_SR_OROM_BANK_SIZE		0x45
#define ICE_SR_NETLIST_BANK_PTR		0x46
#define ICE_SR_NETLIST_BANK_SIZE	0x47
#define ICE_SR_SECTOR_SIZE_IN_WORDS	0x800

/* CSS Header words */
#define ICE_NVM_CSS_SREV_L			0x14
#define ICE_NVM_CSS_SREV_H			0x15

/* Length of CSS header section in words */
#define ICE_CSS_HEADER_LENGTH			330

/* Offset of Shadow RAM copy in the NVM bank area. */
#define ICE_NVM_SR_COPY_WORD_OFFSET		roundup(ICE_CSS_HEADER_LENGTH, 32)

/* Size in bytes of Option ROM trailer */
#define ICE_NVM_OROM_TRAILER_LENGTH		(2 * ICE_CSS_HEADER_LENGTH)

/* The Link Topology Netlist section is stored as a series of words. It is
 * stored in the NVM as a TLV, with the first two words containing the type
 * and length.
 */
#define ICE_NETLIST_LINK_TOPO_MOD_ID		0x011B
#define ICE_NETLIST_TYPE_OFFSET			0x0000
#define ICE_NETLIST_LEN_OFFSET			0x0001

/* The Link Topology section follows the TLV header. When reading the netlist
 * using ice_read_netlist_module, we need to account for the 2-word TLV
 * header.
 */
#define ICE_NETLIST_LINK_TOPO_OFFSET(n)		((n) + 2)

#define ICE_LINK_TOPO_MODULE_LEN		ICE_NETLIST_LINK_TOPO_OFFSET(0x0000)
#define ICE_LINK_TOPO_NODE_COUNT		ICE_NETLIST_LINK_TOPO_OFFSET(0x0001)

#define ICE_LINK_TOPO_NODE_COUNT_M		ICE_M(0x3FF, 0)

/* The Netlist ID Block is located after all of the Link Topology nodes. */
#define ICE_NETLIST_ID_BLK_SIZE			0x30
#define ICE_NETLIST_ID_BLK_OFFSET(n)		ICE_NETLIST_LINK_TOPO_OFFSET(0x0004 + 2 * (n))

/* netlist ID block field offsets (word offsets) */
#define ICE_NETLIST_ID_BLK_MAJOR_VER_LOW	0x02
#define ICE_NETLIST_ID_BLK_MAJOR_VER_HIGH	0x03
#define ICE_NETLIST_ID_BLK_MINOR_VER_LOW	0x04
#define ICE_NETLIST_ID_BLK_MINOR_VER_HIGH	0x05
#define ICE_NETLIST_ID_BLK_TYPE_LOW		0x06
#define ICE_NETLIST_ID_BLK_TYPE_HIGH		0x07
#define ICE_NETLIST_ID_BLK_REV_LOW		0x08
#define ICE_NETLIST_ID_BLK_REV_HIGH		0x09
#define ICE_NETLIST_ID_BLK_SHA_HASH_WORD(n)	(0x0A + (n))
#define ICE_NETLIST_ID_BLK_CUST_VER		0x2F

/* Auxiliary field, mask, and shift definition for Shadow RAM and NVM Flash */
#define ICE_SR_CTRL_WORD_1_S		0x06
#define ICE_SR_CTRL_WORD_1_M		(0x03 << ICE_SR_CTRL_WORD_1_S)
#define ICE_SR_CTRL_WORD_VALID		0x1
#define ICE_SR_CTRL_WORD_OROM_BANK	BIT(3)
#define ICE_SR_CTRL_WORD_NETLIST_BANK	BIT(4)
#define ICE_SR_CTRL_WORD_NVM_BANK	BIT(5)

#define ICE_SR_NVM_PTR_4KB_UNITS	BIT(15)

/* Link override related */
#define ICE_SR_PFA_LINK_OVERRIDE_WORDS		10
#define ICE_SR_PFA_LINK_OVERRIDE_PHY_WORDS	4
#define ICE_SR_PFA_LINK_OVERRIDE_OFFSET		2
#define ICE_SR_PFA_LINK_OVERRIDE_FEC_OFFSET	1
#define ICE_SR_PFA_LINK_OVERRIDE_PHY_OFFSET	2
#define ICE_FW_API_LINK_OVERRIDE_MAJ		1
#define ICE_FW_API_LINK_OVERRIDE_MIN		5
#define ICE_FW_API_LINK_OVERRIDE_PATCH		2

#define ICE_SR_WORDS_IN_1KB		512

/* AQ API version for LLDP_FILTER_CONTROL */
#define ICE_FW_API_LLDP_FLTR_MAJ	1
#define ICE_FW_API_LLDP_FLTR_MIN	7
#define ICE_FW_API_LLDP_FLTR_PATCH	1

/* AQ API version for report default configuration */
#define ICE_FW_API_REPORT_DFLT_CFG_MAJ		1
#define ICE_FW_API_REPORT_DFLT_CFG_MIN		7
#define ICE_FW_API_REPORT_DFLT_CFG_PATCH	3

#endif /* _ICE_TYPE_H_ */
