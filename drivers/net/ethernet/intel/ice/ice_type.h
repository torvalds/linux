/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_TYPE_H_
#define _ICE_TYPE_H_

#define ICE_BYTES_PER_WORD	2
#define ICE_BYTES_PER_DWORD	4

#include "ice_status.h"
#include "ice_hw_autogen.h"
#include "ice_osdep.h"
#include "ice_controlq.h"
#include "ice_lan_tx_rx.h"
#include "ice_flex_type.h"
#include "ice_protocol_type.h"

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
#define ICE_DBG_PKG		BIT_ULL(16)
#define ICE_DBG_RES		BIT_ULL(17)
#define ICE_DBG_AQ_MSG		BIT_ULL(24)
#define ICE_DBG_AQ_DESC		BIT_ULL(25)
#define ICE_DBG_AQ_DESC_BUF	BIT_ULL(26)
#define ICE_DBG_AQ_CMD		BIT_ULL(27)
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
#define ICE_GLOBAL_CFG_LOCK_TIMEOUT	3000

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

enum ice_fec_mode {
	ICE_FEC_NONE = 0,
	ICE_FEC_RS,
	ICE_FEC_BASER,
	ICE_FEC_AUTO
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
	ICE_VSI_VF,
	ICE_VSI_LB = 6,
};

struct ice_link_status {
	/* Refer to ice_aq_phy_type for bits definition */
	u64 phy_type_low;
	u64 phy_type_high;
	u8 topo_media_conflict;
	u16 max_frame_size;
	u16 link_speed;
	u16 req_speeds;
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
};

/* Function specific capabilities */
struct ice_hw_func_caps {
	struct ice_hw_common_caps common_cap;
	u32 num_allocd_vfs;		/* Number of allocated VFs */
	u32 vf_base_id;			/* Logical ID of the first VF */
	u32 guar_num_vsi;
};

/* Device wide capabilities */
struct ice_hw_dev_caps {
	struct ice_hw_common_caps common_cap;
	u32 num_vfs_exposed;		/* Total number of VFs exposed */
	u32 num_vsi_allocd_to_host;	/* Excluding EMP VSI */
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

/* NVM Information */
struct ice_nvm_info {
	struct ice_orom_info orom;	/* Option ROM version info */
	u32 eetrack;			/* NVM data version */
	u16 sr_words;			/* Shadow RAM size in words */
	u32 flash_size;			/* Size of available flash in bytes */
	u8 major_ver;			/* major version of NVM package */
	u8 minor_ver;			/* minor version of dev starter */
	u8 blank_nvm_mode;        /* is NVM empty (no FW present) */
};

#define ICE_NVM_VER_LEN	32

/* Max number of port to queue branches w.r.t topology */
#define ICE_MAX_TRAFFIC_CLASS 8
#define ICE_TXSCHED_MAX_BRANCHES ICE_MAX_TRAFFIC_CLASS

#define ice_for_each_traffic_class(_i)	\
	for ((_i) = 0; (_i) < ICE_MAX_TRAFFIC_CLASS; (_i)++)

#define ICE_INVAL_TEID 0xFFFFFFFF

struct ice_sched_node {
	struct ice_sched_node *parent;
	struct ice_sched_node *sibling; /* next sibling in the same layer */
	struct ice_sched_node **children;
	struct ice_aqc_txsched_elem_data info;
	u32 agg_id;			/* aggregator group ID */
	u16 vsi_handle;
	u8 in_use;			/* suspended or in use */
	u8 tx_sched_layer;		/* Logical Layer (1-9) */
	u8 num_children;
	u8 tc_num;
	u8 owner;
#define ICE_SCHED_NODE_OWNER_LAN	0
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
#define ICE_SCHED_DFLT_BW_WT		1
#define ICE_SCHED_INVAL_PROF_ID		0xFFFF
#define ICE_SCHED_DFLT_BURST_SIZE	(15 * 1024)	/* in bytes (15k) */

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
#define ICE_DCBX_MAX_APPS	32
#define ICE_LLDPDU_SIZE		1500
#define ICE_TLV_STATUS_OPER	0x1
#define ICE_TLV_STATUS_SYNC	0x2
#define ICE_TLV_STATUS_ERR	0x4
#define ICE_APP_PROT_ID_FCOE	0x8906
#define ICE_APP_PROT_ID_ISCSI	0x0cbc
#define ICE_APP_PROT_ID_FIP	0x8914
#define ICE_APP_SEL_ETHTYPE	0x1
#define ICE_APP_SEL_TCPIP	0x2
#define ICE_CEE_APP_SEL_ETHTYPE	0x0
#define ICE_CEE_APP_SEL_TCPIP	0x1

struct ice_dcbx_cfg {
	u32 numapps;
	u32 tlv_status; /* CEE mode TLV status */
	struct ice_dcb_ets_cfg etscfg;
	struct ice_dcb_ets_cfg etsrec;
	struct ice_dcb_pfc_cfg pfc;
	struct ice_dcb_app_priority_table app[ICE_DCBX_MAX_APPS];
	u8 dcbx_mode;
#define ICE_DCBX_MODE_CEE	0x1
#define ICE_DCBX_MODE_IEEE	0x2
	u8 app_mode;
#define ICE_DCBX_APPS_NON_WILLING	0x1
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
	u16 dflt_tx_vsi_rule_id;
	u16 dflt_tx_vsi_num;
	u16 dflt_rx_vsi_rule_id;
	u16 dflt_rx_vsi_num;
	struct ice_fc_info fc;
	struct ice_mac_info mac;
	struct ice_phy_info phy;
	struct mutex sched_lock;	/* protect access to TXSched tree */
	struct ice_sched_node *
		sib_head[ICE_MAX_TRAFFIC_CLASS][ICE_AQC_TOPO_MAX_LEVEL_NUM];
	/* List contain profile ID(s) and other params per layer */
	struct list_head rl_prof_list[ICE_AQC_TOPO_MAX_LEVEL_NUM];
	struct ice_dcbx_cfg local_dcbx_cfg;	/* Oper/Local Cfg */
	/* DCBX info */
	struct ice_dcbx_cfg remote_dcbx_cfg;	/* Peer Cfg */
	struct ice_dcbx_cfg desired_dcbx_cfg;	/* CEE Desired Cfg */
	/* LLDP/DCBX Status */
	u8 dcbx_status:3;		/* see ICE_DCBX_STATUS_DIS */
	u8 is_sw_lldp:1;
	u8 is_vf:1;
};

struct ice_switch_info {
	struct list_head vsi_list_map_head;
	struct ice_sw_recipe *recp_list;
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

/* Port hardware description */
struct ice_hw {
	u8 __iomem *hw_addr;
	void *back;
	struct ice_aqc_layer_props *layer_info;
	struct ice_port_info *port_info;
	u64 debug_mask;		/* bitmap for debug mask */
	enum ice_mac_type mac_type;

	/* pci info */
	u16 device_id;
	u16 vendor_id;
	u16 subsystem_device_id;
	u16 subsystem_vendor_id;
	u8 revision_id;

	u8 pf_id;		/* device profile info */

	u16 max_burst_size;	/* driver sets this value */

	/* Tx Scheduler values */
	u16 num_tx_sched_layers;
	u16 num_tx_sched_phys_layers;
	u8 flattened_layers;
	u8 max_cgds;
	u8 sw_entry_point_layer;
	u16 max_children[ICE_AQC_TOPO_MAX_LEVEL_NUM];
	struct list_head agg_list;	/* lists all aggregator */

	struct ice_vsi_ctx *vsi_ctx[ICE_MAX_VSI];
	u8 evb_veb;		/* true for VEB, false for VEPA */
	u8 reset_ongoing;	/* true if HW is in reset, false otherwise */
	struct ice_bus_info bus;
	struct ice_nvm_info nvm;
	struct ice_hw_dev_caps dev_caps;	/* device capabilities */
	struct ice_hw_func_caps func_caps;	/* function capabilities */

	struct ice_switch_info *switch_info;	/* switch filter lists */

	/* Control Queue info */
	struct ice_ctl_q_info adminq;
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

	u8 ucast_shared;	/* true if VSIs can share unicast addr */

	/* Active package version (currently active) */
	struct ice_pkg_ver active_pkg_ver;
	u8 active_pkg_name[ICE_PKG_NAME_SIZE];
	u8 active_pkg_in_nvm;

	enum ice_aq_err pkg_dwnld_status;

	/* Driver's package ver - (from the Metadata seg) */
	struct ice_pkg_ver pkg_ver;
	u8 pkg_name[ICE_PKG_NAME_SIZE];

	/* Driver's Ice package version (from the Ice seg) */
	struct ice_pkg_ver ice_pkg_ver;
	u8 ice_pkg_name[ICE_PKG_NAME_SIZE];

	/* Pointer to the ice segment */
	struct ice_seg *seg;

	/* Pointer to allocated copy of pkg memory */
	u8 *pkg_copy;
	u32 pkg_size;

	/* HW block tables */
	struct ice_blk_info blk[ICE_BLK_COUNT];
	struct mutex fl_profs_locks[ICE_BLK_COUNT];	/* lock fltr profiles */
	struct list_head fl_profs[ICE_BLK_COUNT];
	struct mutex rss_locks;	/* protect RSS configuration */
	struct list_head rss_list_head;
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
};

/* Checksum and Shadow RAM pointers */
#define ICE_SR_BOOT_CFG_PTR		0x132
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
#define ICE_SR_SECTOR_SIZE_IN_WORDS	0x800
#define ICE_SR_WORDS_IN_1KB		512

/* Hash redirection LUT for VSI - maximum array size */
#define ICE_VSIQF_HLUT_ARRAY_SIZE	((VSIQF_HLUT_MAX_INDEX + 1) * 4)

#endif /* _ICE_TYPE_H_ */
