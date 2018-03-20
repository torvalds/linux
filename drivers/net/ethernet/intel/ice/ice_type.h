/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_TYPE_H_
#define _ICE_TYPE_H_

#include "ice_status.h"
#include "ice_hw_autogen.h"
#include "ice_osdep.h"
#include "ice_controlq.h"

/* debug masks - set these bits in hw->debug_mask to control output */
#define ICE_DBG_INIT		BIT_ULL(1)
#define ICE_DBG_NVM		BIT_ULL(7)
#define ICE_DBG_SW		BIT_ULL(13)
#define ICE_DBG_SCHED		BIT_ULL(14)
#define ICE_DBG_RES		BIT_ULL(17)
#define ICE_DBG_AQ_MSG		BIT_ULL(24)
#define ICE_DBG_AQ_CMD		BIT_ULL(27)

enum ice_aq_res_ids {
	ICE_NVM_RES_ID = 1,
	ICE_SPD_RES_ID,
	ICE_GLOBAL_CFG_LOCK_RES_ID,
	ICE_CHANGE_LOCK_RES_ID
};

enum ice_aq_res_access_type {
	ICE_RES_READ = 1,
	ICE_RES_WRITE
};

/* Various MAC types */
enum ice_mac_type {
	ICE_MAC_UNKNOWN = 0,
	ICE_MAC_GENERIC,
};

/* Common HW capabilities for SW use */
struct ice_hw_common_caps {
	/* TX/RX queues */
	u16 num_rxq;		/* Number/Total RX queues */
	u16 rxq_first_id;	/* First queue ID for RX queues */
	u16 num_txq;		/* Number/Total TX queues */
	u16 txq_first_id;	/* First queue ID for TX queues */

	/* MSI-X vectors */
	u16 num_msix_vectors;
	u16 msix_vector_first_id;

	/* Max MTU for function or device */
	u16 max_mtu;

	/* RSS related capabilities */
	u16 rss_table_size;		/* 512 for PFs and 64 for VFs */
	u8 rss_table_entry_width;	/* RSS Entry width in bits */
};

/* Function specific capabilities */
struct ice_hw_func_caps {
	struct ice_hw_common_caps common_cap;
	u32 guaranteed_num_vsi;
};

/* Device wide capabilities */
struct ice_hw_dev_caps {
	struct ice_hw_common_caps common_cap;
	u32 num_vsi_allocd_to_host;	/* Excluding EMP VSI */
};

/* Various RESET request, These are not tied with HW reset types */
enum ice_reset_req {
	ICE_RESET_PFR	= 0,
	ICE_RESET_CORER	= 1,
	ICE_RESET_GLOBR	= 2,
};

/* Bus parameters */
struct ice_bus_info {
	u16 device;
	u8 func;
};

/* NVM Information */
struct ice_nvm_info {
	u32 eetrack;              /* NVM data version */
	u32 oem_ver;              /* OEM version info */
	u16 sr_words;             /* Shadow RAM size in words */
	u16 ver;                  /* NVM package version */
	bool blank_nvm_mode;      /* is NVM empty (no FW present) */
};

/* Max number of port to queue branches w.r.t topology */
#define ICE_MAX_TRAFFIC_CLASS 8

struct ice_sched_node {
	struct ice_sched_node *parent;
	struct ice_sched_node *sibling; /* next sibling in the same layer */
	struct ice_sched_node **children;
	struct ice_aqc_txsched_elem_data info;
	u32 agg_id;			/* aggregator group id */
	u16 vsi_id;
	bool in_use;			/* suspended or in use */
	u8 tx_sched_layer;		/* Logical Layer (1-9) */
	u8 num_children;
	u8 tc_num;
	u8 owner;
#define ICE_SCHED_NODE_OWNER_LAN	0
};

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

/* vsi type list entry to locate corresponding vsi/ag nodes */
struct ice_sched_vsi_info {
	struct ice_sched_node *vsi_node[ICE_MAX_TRAFFIC_CLASS];
	struct ice_sched_node *ag_node[ICE_MAX_TRAFFIC_CLASS];
	struct list_head list_entry;
	u16 max_lanq[ICE_MAX_TRAFFIC_CLASS];
	u16 vsi_id;
};

/* driver defines the policy */
struct ice_sched_tx_policy {
	u16 max_num_vsis;
	u8 max_num_lan_qs_per_tc[ICE_MAX_TRAFFIC_CLASS];
	bool rdma_ena;
};

struct ice_port_info {
	struct ice_sched_node *root;	/* Root Node per Port */
	struct ice_hw *hw;		/* back pointer to hw instance */
	u16 sw_id;			/* Initial switch ID belongs to port */
	u16 pf_vf_num;
	u8 port_state;
#define ICE_SCHED_PORT_STATE_INIT	0x0
#define ICE_SCHED_PORT_STATE_READY	0x1
	u16 dflt_tx_vsi_num;
	u16 dflt_rx_vsi_num;
	struct mutex sched_lock;	/* protect access to TXSched tree */
	struct ice_sched_tx_policy sched_policy;
	struct list_head vsi_info_list;
	struct list_head agg_list;	/* lists all aggregator */
	u8 lport;
#define ICE_LPORT_MASK		0xff
	bool is_vf;
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

	/* TX Scheduler values */
	u16 num_tx_sched_layers;
	u16 num_tx_sched_phys_layers;
	u8 flattened_layers;
	u8 max_cgds;
	u8 sw_entry_point_layer;

	struct ice_bus_info bus;
	struct ice_nvm_info nvm;
	struct ice_hw_dev_caps dev_caps;	/* device capabilities */
	struct ice_hw_func_caps func_caps;	/* function capabilities */

	/* Control Queue info */
	struct ice_ctl_q_info adminq;

	u8 api_branch;		/* API branch version */
	u8 api_maj_ver;		/* API major version */
	u8 api_min_ver;		/* API minor version */
	u8 api_patch;		/* API patch version */
	u8 fw_branch;		/* firmware branch version */
	u8 fw_maj_ver;		/* firmware major version */
	u8 fw_min_ver;		/* firmware minor version */
	u8 fw_patch;		/* firmware patch version */
	u32 fw_build;		/* firmware build number */
};

/* Checksum and Shadow RAM pointers */
#define ICE_SR_NVM_DEV_STARTER_VER	0x18
#define ICE_SR_NVM_EETRACK_LO		0x2D
#define ICE_SR_NVM_EETRACK_HI		0x2E
#define ICE_SR_SECTOR_SIZE_IN_WORDS	0x800
#define ICE_SR_WORDS_IN_1KB		512

#endif /* _ICE_TYPE_H_ */
