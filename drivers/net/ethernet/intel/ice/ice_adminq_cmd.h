/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_ADMINQ_CMD_H_
#define _ICE_ADMINQ_CMD_H_

/* This header file defines the Admin Queue commands, error codes and
 * descriptor format. It is shared between Firmware and Software.
 */

#define ICE_MAX_VSI			768
#define ICE_AQC_TOPO_MAX_LEVEL_NUM	0x9
#define ICE_AQ_SET_MAC_FRAME_SIZE_MAX	9728

struct ice_aqc_generic {
	__le32 param0;
	__le32 param1;
	__le32 addr_high;
	__le32 addr_low;
};

/* Get version (direct 0x0001) */
struct ice_aqc_get_ver {
	__le32 rom_ver;
	__le32 fw_build;
	u8 fw_branch;
	u8 fw_major;
	u8 fw_minor;
	u8 fw_patch;
	u8 api_branch;
	u8 api_major;
	u8 api_minor;
	u8 api_patch;
};

/* Send driver version (indirect 0x0002) */
struct ice_aqc_driver_ver {
	u8 major_ver;
	u8 minor_ver;
	u8 build_ver;
	u8 subbuild_ver;
	u8 reserved[4];
	__le32 addr_high;
	__le32 addr_low;
};

/* Queue Shutdown (direct 0x0003) */
struct ice_aqc_q_shutdown {
	u8 driver_unloading;
#define ICE_AQC_DRIVER_UNLOADING	BIT(0)
	u8 reserved[15];
};

/* Request resource ownership (direct 0x0008)
 * Release resource ownership (direct 0x0009)
 */
struct ice_aqc_req_res {
	__le16 res_id;
#define ICE_AQC_RES_ID_NVM		1
#define ICE_AQC_RES_ID_SDP		2
#define ICE_AQC_RES_ID_CHNG_LOCK	3
#define ICE_AQC_RES_ID_GLBL_LOCK	4
	__le16 access_type;
#define ICE_AQC_RES_ACCESS_READ		1
#define ICE_AQC_RES_ACCESS_WRITE	2

	/* Upon successful completion, FW writes this value and driver is
	 * expected to release resource before timeout. This value is provided
	 * in milliseconds.
	 */
	__le32 timeout;
#define ICE_AQ_RES_NVM_READ_DFLT_TIMEOUT_MS	3000
#define ICE_AQ_RES_NVM_WRITE_DFLT_TIMEOUT_MS	180000
#define ICE_AQ_RES_CHNG_LOCK_DFLT_TIMEOUT_MS	1000
#define ICE_AQ_RES_GLBL_LOCK_DFLT_TIMEOUT_MS	3000
	/* For SDP: pin ID of the SDP */
	__le32 res_number;
	/* Status is only used for ICE_AQC_RES_ID_GLBL_LOCK */
	__le16 status;
#define ICE_AQ_RES_GLBL_SUCCESS		0
#define ICE_AQ_RES_GLBL_IN_PROG		1
#define ICE_AQ_RES_GLBL_DONE		2
	u8 reserved[2];
};

/* Get function capabilities (indirect 0x000A)
 * Get device capabilities (indirect 0x000B)
 */
struct ice_aqc_list_caps {
	u8 cmd_flags;
	u8 pf_index;
	u8 reserved[2];
	__le32 count;
	__le32 addr_high;
	__le32 addr_low;
};

/* Device/Function buffer entry, repeated per reported capability */
struct ice_aqc_list_caps_elem {
	__le16 cap;
#define ICE_AQC_CAPS_VALID_FUNCTIONS			0x0005
#define ICE_AQC_CAPS_SRIOV				0x0012
#define ICE_AQC_CAPS_VF					0x0013
#define ICE_AQC_CAPS_VSI				0x0017
#define ICE_AQC_CAPS_DCB				0x0018
#define ICE_AQC_CAPS_RSS				0x0040
#define ICE_AQC_CAPS_RXQS				0x0041
#define ICE_AQC_CAPS_TXQS				0x0042
#define ICE_AQC_CAPS_MSIX				0x0043
#define ICE_AQC_CAPS_FD					0x0045
#define ICE_AQC_CAPS_MAX_MTU				0x0047
#define ICE_AQC_CAPS_NVM_VER				0x0048
#define ICE_AQC_CAPS_PENDING_NVM_VER			0x0049
#define ICE_AQC_CAPS_OROM_VER				0x004A
#define ICE_AQC_CAPS_PENDING_OROM_VER			0x004B
#define ICE_AQC_CAPS_NET_VER				0x004C
#define ICE_AQC_CAPS_PENDING_NET_VER			0x004D
#define ICE_AQC_CAPS_NVM_MGMT				0x0080

	u8 major_ver;
	u8 minor_ver;
	/* Number of resources described by this capability */
	__le32 number;
	/* Only meaningful for some types of resources */
	__le32 logical_id;
	/* Only meaningful for some types of resources */
	__le32 phys_id;
	__le64 rsvd1;
	__le64 rsvd2;
};

/* Manage MAC address, read command - indirect (0x0107)
 * This struct is also used for the response
 */
struct ice_aqc_manage_mac_read {
	__le16 flags; /* Zeroed by device driver */
#define ICE_AQC_MAN_MAC_LAN_ADDR_VALID		BIT(4)
#define ICE_AQC_MAN_MAC_SAN_ADDR_VALID		BIT(5)
#define ICE_AQC_MAN_MAC_PORT_ADDR_VALID		BIT(6)
#define ICE_AQC_MAN_MAC_WOL_ADDR_VALID		BIT(7)
#define ICE_AQC_MAN_MAC_READ_S			4
#define ICE_AQC_MAN_MAC_READ_M			(0xF << ICE_AQC_MAN_MAC_READ_S)
	u8 rsvd[2];
	u8 num_addr; /* Used in response */
	u8 rsvd1[3];
	__le32 addr_high;
	__le32 addr_low;
};

/* Response buffer format for manage MAC read command */
struct ice_aqc_manage_mac_read_resp {
	u8 lport_num;
	u8 addr_type;
#define ICE_AQC_MAN_MAC_ADDR_TYPE_LAN		0
#define ICE_AQC_MAN_MAC_ADDR_TYPE_WOL		1
	u8 mac_addr[ETH_ALEN];
};

/* Manage MAC address, write command - direct (0x0108) */
struct ice_aqc_manage_mac_write {
	u8 rsvd;
	u8 flags;
#define ICE_AQC_MAN_MAC_WR_MC_MAG_EN		BIT(0)
#define ICE_AQC_MAN_MAC_WR_WOL_LAA_PFR_KEEP	BIT(1)
#define ICE_AQC_MAN_MAC_WR_S		6
#define ICE_AQC_MAN_MAC_WR_M		ICE_M(3, ICE_AQC_MAN_MAC_WR_S)
#define ICE_AQC_MAN_MAC_UPDATE_LAA	0
#define ICE_AQC_MAN_MAC_UPDATE_LAA_WOL	BIT(ICE_AQC_MAN_MAC_WR_S)
	/* byte stream in network order */
	u8 mac_addr[ETH_ALEN];
	__le32 addr_high;
	__le32 addr_low;
};

/* Clear PXE Command and response (direct 0x0110) */
struct ice_aqc_clear_pxe {
	u8 rx_cnt;
#define ICE_AQC_CLEAR_PXE_RX_CNT		0x2
	u8 reserved[15];
};

/* Get switch configuration (0x0200) */
struct ice_aqc_get_sw_cfg {
	/* Reserved for command and copy of request flags for response */
	__le16 flags;
	/* First desc in case of command and next_elem in case of response
	 * In case of response, if it is not zero, means all the configuration
	 * was not returned and new command shall be sent with this value in
	 * the 'first desc' field
	 */
	__le16 element;
	/* Reserved for command, only used for response */
	__le16 num_elems;
	__le16 rsvd;
	__le32 addr_high;
	__le32 addr_low;
};

/* Each entry in the response buffer is of the following type: */
struct ice_aqc_get_sw_cfg_resp_elem {
	/* VSI/Port Number */
	__le16 vsi_port_num;
#define ICE_AQC_GET_SW_CONF_RESP_VSI_PORT_NUM_S	0
#define ICE_AQC_GET_SW_CONF_RESP_VSI_PORT_NUM_M	\
			(0x3FF << ICE_AQC_GET_SW_CONF_RESP_VSI_PORT_NUM_S)
#define ICE_AQC_GET_SW_CONF_RESP_TYPE_S	14
#define ICE_AQC_GET_SW_CONF_RESP_TYPE_M	(0x3 << ICE_AQC_GET_SW_CONF_RESP_TYPE_S)
#define ICE_AQC_GET_SW_CONF_RESP_PHYS_PORT	0
#define ICE_AQC_GET_SW_CONF_RESP_VIRT_PORT	1
#define ICE_AQC_GET_SW_CONF_RESP_VSI		2

	/* SWID VSI/Port belongs to */
	__le16 swid;

	/* Bit 14..0 : PF/VF number VSI belongs to
	 * Bit 15 : VF indication bit
	 */
	__le16 pf_vf_num;
#define ICE_AQC_GET_SW_CONF_RESP_FUNC_NUM_S	0
#define ICE_AQC_GET_SW_CONF_RESP_FUNC_NUM_M	\
				(0x7FFF << ICE_AQC_GET_SW_CONF_RESP_FUNC_NUM_S)
#define ICE_AQC_GET_SW_CONF_RESP_IS_VF		BIT(15)
};

/* These resource type defines are used for all switch resource
 * commands where a resource type is required, such as:
 * Get Resource Allocation command (indirect 0x0204)
 * Allocate Resources command (indirect 0x0208)
 * Free Resources command (indirect 0x0209)
 * Get Allocated Resource Descriptors Command (indirect 0x020A)
 */
#define ICE_AQC_RES_TYPE_VSI_LIST_REP			0x03
#define ICE_AQC_RES_TYPE_VSI_LIST_PRUNE			0x04
#define ICE_AQC_RES_TYPE_FDIR_COUNTER_BLOCK		0x21
#define ICE_AQC_RES_TYPE_FDIR_GUARANTEED_ENTRIES	0x22
#define ICE_AQC_RES_TYPE_FDIR_SHARED_ENTRIES		0x23
#define ICE_AQC_RES_TYPE_FD_PROF_BLDR_PROFID		0x58
#define ICE_AQC_RES_TYPE_FD_PROF_BLDR_TCAM		0x59
#define ICE_AQC_RES_TYPE_HASH_PROF_BLDR_PROFID		0x60
#define ICE_AQC_RES_TYPE_HASH_PROF_BLDR_TCAM		0x61

#define ICE_AQC_RES_TYPE_FLAG_SCAN_BOTTOM		BIT(12)
#define ICE_AQC_RES_TYPE_FLAG_IGNORE_INDEX		BIT(13)

#define ICE_AQC_RES_TYPE_FLAG_DEDICATED			0x00

#define ICE_AQC_RES_TYPE_S	0
#define ICE_AQC_RES_TYPE_M	(0x07F << ICE_AQC_RES_TYPE_S)

/* Allocate Resources command (indirect 0x0208)
 * Free Resources command (indirect 0x0209)
 */
struct ice_aqc_alloc_free_res_cmd {
	__le16 num_entries; /* Number of Resource entries */
	u8 reserved[6];
	__le32 addr_high;
	__le32 addr_low;
};

/* Resource descriptor */
struct ice_aqc_res_elem {
	union {
		__le16 sw_resp;
		__le16 flu_resp;
	} e;
};

/* Buffer for Allocate/Free Resources commands */
struct ice_aqc_alloc_free_res_elem {
	__le16 res_type; /* Types defined above cmd 0x0204 */
#define ICE_AQC_RES_TYPE_SHARED_S	7
#define ICE_AQC_RES_TYPE_SHARED_M	(0x1 << ICE_AQC_RES_TYPE_SHARED_S)
#define ICE_AQC_RES_TYPE_VSI_PRUNE_LIST_S	8
#define ICE_AQC_RES_TYPE_VSI_PRUNE_LIST_M	\
				(0xF << ICE_AQC_RES_TYPE_VSI_PRUNE_LIST_S)
	__le16 num_elems;
	struct ice_aqc_res_elem elem[];
};

/* Add VSI (indirect 0x0210)
 * Update VSI (indirect 0x0211)
 * Get VSI (indirect 0x0212)
 * Free VSI (indirect 0x0213)
 */
struct ice_aqc_add_get_update_free_vsi {
	__le16 vsi_num;
#define ICE_AQ_VSI_NUM_S	0
#define ICE_AQ_VSI_NUM_M	(0x03FF << ICE_AQ_VSI_NUM_S)
#define ICE_AQ_VSI_IS_VALID	BIT(15)
	__le16 cmd_flags;
#define ICE_AQ_VSI_KEEP_ALLOC	0x1
	u8 vf_id;
	u8 reserved;
	__le16 vsi_flags;
#define ICE_AQ_VSI_TYPE_S	0
#define ICE_AQ_VSI_TYPE_M	(0x3 << ICE_AQ_VSI_TYPE_S)
#define ICE_AQ_VSI_TYPE_VF	0x0
#define ICE_AQ_VSI_TYPE_VMDQ2	0x1
#define ICE_AQ_VSI_TYPE_PF	0x2
#define ICE_AQ_VSI_TYPE_EMP_MNG	0x3
	__le32 addr_high;
	__le32 addr_low;
};

/* Response descriptor for:
 * Add VSI (indirect 0x0210)
 * Update VSI (indirect 0x0211)
 * Free VSI (indirect 0x0213)
 */
struct ice_aqc_add_update_free_vsi_resp {
	__le16 vsi_num;
	__le16 ext_status;
	__le16 vsi_used;
	__le16 vsi_free;
	__le32 addr_high;
	__le32 addr_low;
};

struct ice_aqc_vsi_props {
	__le16 valid_sections;
#define ICE_AQ_VSI_PROP_SW_VALID		BIT(0)
#define ICE_AQ_VSI_PROP_SECURITY_VALID		BIT(1)
#define ICE_AQ_VSI_PROP_VLAN_VALID		BIT(2)
#define ICE_AQ_VSI_PROP_OUTER_TAG_VALID		BIT(3)
#define ICE_AQ_VSI_PROP_INGRESS_UP_VALID	BIT(4)
#define ICE_AQ_VSI_PROP_EGRESS_UP_VALID		BIT(5)
#define ICE_AQ_VSI_PROP_RXQ_MAP_VALID		BIT(6)
#define ICE_AQ_VSI_PROP_Q_OPT_VALID		BIT(7)
#define ICE_AQ_VSI_PROP_OUTER_UP_VALID		BIT(8)
#define ICE_AQ_VSI_PROP_FLOW_DIR_VALID		BIT(11)
#define ICE_AQ_VSI_PROP_PASID_VALID		BIT(12)
	/* switch section */
	u8 sw_id;
	u8 sw_flags;
#define ICE_AQ_VSI_SW_FLAG_ALLOW_LB		BIT(5)
#define ICE_AQ_VSI_SW_FLAG_LOCAL_LB		BIT(6)
#define ICE_AQ_VSI_SW_FLAG_SRC_PRUNE		BIT(7)
	u8 sw_flags2;
#define ICE_AQ_VSI_SW_FLAG_RX_PRUNE_EN_S	0
#define ICE_AQ_VSI_SW_FLAG_RX_PRUNE_EN_M	\
				(0xF << ICE_AQ_VSI_SW_FLAG_RX_PRUNE_EN_S)
#define ICE_AQ_VSI_SW_FLAG_RX_VLAN_PRUNE_ENA	BIT(0)
#define ICE_AQ_VSI_SW_FLAG_LAN_ENA		BIT(4)
	u8 veb_stat_id;
#define ICE_AQ_VSI_SW_VEB_STAT_ID_S		0
#define ICE_AQ_VSI_SW_VEB_STAT_ID_M	(0x1F << ICE_AQ_VSI_SW_VEB_STAT_ID_S)
#define ICE_AQ_VSI_SW_VEB_STAT_ID_VALID		BIT(5)
	/* security section */
	u8 sec_flags;
#define ICE_AQ_VSI_SEC_FLAG_ALLOW_DEST_OVRD	BIT(0)
#define ICE_AQ_VSI_SEC_FLAG_ENA_MAC_ANTI_SPOOF	BIT(2)
#define ICE_AQ_VSI_SEC_TX_PRUNE_ENA_S	4
#define ICE_AQ_VSI_SEC_TX_PRUNE_ENA_M	(0xF << ICE_AQ_VSI_SEC_TX_PRUNE_ENA_S)
#define ICE_AQ_VSI_SEC_TX_VLAN_PRUNE_ENA	BIT(0)
	u8 sec_reserved;
	/* VLAN section */
	__le16 pvid; /* VLANS include priority bits */
	u8 pvlan_reserved[2];
	u8 vlan_flags;
#define ICE_AQ_VSI_VLAN_MODE_S	0
#define ICE_AQ_VSI_VLAN_MODE_M	(0x3 << ICE_AQ_VSI_VLAN_MODE_S)
#define ICE_AQ_VSI_VLAN_MODE_UNTAGGED	0x1
#define ICE_AQ_VSI_VLAN_MODE_TAGGED	0x2
#define ICE_AQ_VSI_VLAN_MODE_ALL	0x3
#define ICE_AQ_VSI_PVLAN_INSERT_PVID	BIT(2)
#define ICE_AQ_VSI_VLAN_EMOD_S		3
#define ICE_AQ_VSI_VLAN_EMOD_M		(0x3 << ICE_AQ_VSI_VLAN_EMOD_S)
#define ICE_AQ_VSI_VLAN_EMOD_STR_BOTH	(0x0 << ICE_AQ_VSI_VLAN_EMOD_S)
#define ICE_AQ_VSI_VLAN_EMOD_STR_UP	(0x1 << ICE_AQ_VSI_VLAN_EMOD_S)
#define ICE_AQ_VSI_VLAN_EMOD_STR	(0x2 << ICE_AQ_VSI_VLAN_EMOD_S)
#define ICE_AQ_VSI_VLAN_EMOD_NOTHING	(0x3 << ICE_AQ_VSI_VLAN_EMOD_S)
	u8 pvlan_reserved2[3];
	/* ingress egress up sections */
	__le32 ingress_table; /* bitmap, 3 bits per up */
#define ICE_AQ_VSI_UP_TABLE_UP0_S	0
#define ICE_AQ_VSI_UP_TABLE_UP0_M	(0x7 << ICE_AQ_VSI_UP_TABLE_UP0_S)
#define ICE_AQ_VSI_UP_TABLE_UP1_S	3
#define ICE_AQ_VSI_UP_TABLE_UP1_M	(0x7 << ICE_AQ_VSI_UP_TABLE_UP1_S)
#define ICE_AQ_VSI_UP_TABLE_UP2_S	6
#define ICE_AQ_VSI_UP_TABLE_UP2_M	(0x7 << ICE_AQ_VSI_UP_TABLE_UP2_S)
#define ICE_AQ_VSI_UP_TABLE_UP3_S	9
#define ICE_AQ_VSI_UP_TABLE_UP3_M	(0x7 << ICE_AQ_VSI_UP_TABLE_UP3_S)
#define ICE_AQ_VSI_UP_TABLE_UP4_S	12
#define ICE_AQ_VSI_UP_TABLE_UP4_M	(0x7 << ICE_AQ_VSI_UP_TABLE_UP4_S)
#define ICE_AQ_VSI_UP_TABLE_UP5_S	15
#define ICE_AQ_VSI_UP_TABLE_UP5_M	(0x7 << ICE_AQ_VSI_UP_TABLE_UP5_S)
#define ICE_AQ_VSI_UP_TABLE_UP6_S	18
#define ICE_AQ_VSI_UP_TABLE_UP6_M	(0x7 << ICE_AQ_VSI_UP_TABLE_UP6_S)
#define ICE_AQ_VSI_UP_TABLE_UP7_S	21
#define ICE_AQ_VSI_UP_TABLE_UP7_M	(0x7 << ICE_AQ_VSI_UP_TABLE_UP7_S)
	__le32 egress_table;   /* same defines as for ingress table */
	/* outer tags section */
	__le16 outer_tag;
	u8 outer_tag_flags;
#define ICE_AQ_VSI_OUTER_TAG_MODE_S	0
#define ICE_AQ_VSI_OUTER_TAG_MODE_M	(0x3 << ICE_AQ_VSI_OUTER_TAG_MODE_S)
#define ICE_AQ_VSI_OUTER_TAG_NOTHING	0x0
#define ICE_AQ_VSI_OUTER_TAG_REMOVE	0x1
#define ICE_AQ_VSI_OUTER_TAG_COPY	0x2
#define ICE_AQ_VSI_OUTER_TAG_TYPE_S	2
#define ICE_AQ_VSI_OUTER_TAG_TYPE_M	(0x3 << ICE_AQ_VSI_OUTER_TAG_TYPE_S)
#define ICE_AQ_VSI_OUTER_TAG_NONE	0x0
#define ICE_AQ_VSI_OUTER_TAG_STAG	0x1
#define ICE_AQ_VSI_OUTER_TAG_VLAN_8100	0x2
#define ICE_AQ_VSI_OUTER_TAG_VLAN_9100	0x3
#define ICE_AQ_VSI_OUTER_TAG_INSERT	BIT(4)
#define ICE_AQ_VSI_OUTER_TAG_ACCEPT_HOST BIT(6)
	u8 outer_tag_reserved;
	/* queue mapping section */
	__le16 mapping_flags;
#define ICE_AQ_VSI_Q_MAP_CONTIG	0x0
#define ICE_AQ_VSI_Q_MAP_NONCONTIG	BIT(0)
	__le16 q_mapping[16];
#define ICE_AQ_VSI_Q_S		0
#define ICE_AQ_VSI_Q_M		(0x7FF << ICE_AQ_VSI_Q_S)
	__le16 tc_mapping[8];
#define ICE_AQ_VSI_TC_Q_OFFSET_S	0
#define ICE_AQ_VSI_TC_Q_OFFSET_M	(0x7FF << ICE_AQ_VSI_TC_Q_OFFSET_S)
#define ICE_AQ_VSI_TC_Q_NUM_S		11
#define ICE_AQ_VSI_TC_Q_NUM_M		(0xF << ICE_AQ_VSI_TC_Q_NUM_S)
	/* queueing option section */
	u8 q_opt_rss;
#define ICE_AQ_VSI_Q_OPT_RSS_LUT_S	0
#define ICE_AQ_VSI_Q_OPT_RSS_LUT_M	(0x3 << ICE_AQ_VSI_Q_OPT_RSS_LUT_S)
#define ICE_AQ_VSI_Q_OPT_RSS_LUT_VSI	0x0
#define ICE_AQ_VSI_Q_OPT_RSS_LUT_PF	0x2
#define ICE_AQ_VSI_Q_OPT_RSS_LUT_GBL	0x3
#define ICE_AQ_VSI_Q_OPT_RSS_GBL_LUT_S	2
#define ICE_AQ_VSI_Q_OPT_RSS_GBL_LUT_M	(0xF << ICE_AQ_VSI_Q_OPT_RSS_GBL_LUT_S)
#define ICE_AQ_VSI_Q_OPT_RSS_HASH_S	6
#define ICE_AQ_VSI_Q_OPT_RSS_HASH_M	(0x3 << ICE_AQ_VSI_Q_OPT_RSS_HASH_S)
#define ICE_AQ_VSI_Q_OPT_RSS_TPLZ	(0x0 << ICE_AQ_VSI_Q_OPT_RSS_HASH_S)
#define ICE_AQ_VSI_Q_OPT_RSS_SYM_TPLZ	(0x1 << ICE_AQ_VSI_Q_OPT_RSS_HASH_S)
#define ICE_AQ_VSI_Q_OPT_RSS_XOR	(0x2 << ICE_AQ_VSI_Q_OPT_RSS_HASH_S)
#define ICE_AQ_VSI_Q_OPT_RSS_JHASH	(0x3 << ICE_AQ_VSI_Q_OPT_RSS_HASH_S)
	u8 q_opt_tc;
#define ICE_AQ_VSI_Q_OPT_TC_OVR_S	0
#define ICE_AQ_VSI_Q_OPT_TC_OVR_M	(0x1F << ICE_AQ_VSI_Q_OPT_TC_OVR_S)
#define ICE_AQ_VSI_Q_OPT_PROF_TC_OVR	BIT(7)
	u8 q_opt_flags;
#define ICE_AQ_VSI_Q_OPT_PE_FLTR_EN	BIT(0)
	u8 q_opt_reserved[3];
	/* outer up section */
	__le32 outer_up_table; /* same structure and defines as ingress tbl */
	/* section 10 */
	__le16 sect_10_reserved;
	/* flow director section */
	__le16 fd_options;
#define ICE_AQ_VSI_FD_ENABLE		BIT(0)
#define ICE_AQ_VSI_FD_TX_AUTO_ENABLE	BIT(1)
#define ICE_AQ_VSI_FD_PROG_ENABLE	BIT(3)
	__le16 max_fd_fltr_dedicated;
	__le16 max_fd_fltr_shared;
	__le16 fd_def_q;
#define ICE_AQ_VSI_FD_DEF_Q_S		0
#define ICE_AQ_VSI_FD_DEF_Q_M		(0x7FF << ICE_AQ_VSI_FD_DEF_Q_S)
#define ICE_AQ_VSI_FD_DEF_GRP_S	12
#define ICE_AQ_VSI_FD_DEF_GRP_M	(0x7 << ICE_AQ_VSI_FD_DEF_GRP_S)
	__le16 fd_report_opt;
#define ICE_AQ_VSI_FD_REPORT_Q_S	0
#define ICE_AQ_VSI_FD_REPORT_Q_M	(0x7FF << ICE_AQ_VSI_FD_REPORT_Q_S)
#define ICE_AQ_VSI_FD_DEF_PRIORITY_S	12
#define ICE_AQ_VSI_FD_DEF_PRIORITY_M	(0x7 << ICE_AQ_VSI_FD_DEF_PRIORITY_S)
#define ICE_AQ_VSI_FD_DEF_DROP		BIT(15)
	/* PASID section */
	__le32 pasid_id;
#define ICE_AQ_VSI_PASID_ID_S		0
#define ICE_AQ_VSI_PASID_ID_M		(0xFFFFF << ICE_AQ_VSI_PASID_ID_S)
#define ICE_AQ_VSI_PASID_ID_VALID	BIT(31)
	u8 reserved[24];
};

#define ICE_MAX_NUM_RECIPES 64

/* Add/Update/Remove/Get switch rules (indirect 0x02A0, 0x02A1, 0x02A2, 0x02A3)
 */
struct ice_aqc_sw_rules {
	/* ops: add switch rules, referring the number of rules.
	 * ops: update switch rules, referring the number of filters
	 * ops: remove switch rules, referring the entry index.
	 * ops: get switch rules, referring to the number of filters.
	 */
	__le16 num_rules_fltr_entry_index;
	u8 reserved[6];
	__le32 addr_high;
	__le32 addr_low;
};

/* Add/Update/Get/Remove lookup Rx/Tx command/response entry
 * This structures describes the lookup rules and associated actions. "index"
 * is returned as part of a response to a successful Add command, and can be
 * used to identify the rule for Update/Get/Remove commands.
 */
struct ice_sw_rule_lkup_rx_tx {
	__le16 recipe_id;
#define ICE_SW_RECIPE_LOGICAL_PORT_FWD		10
	/* Source port for LOOKUP_RX and source VSI in case of LOOKUP_TX */
	__le16 src;
	__le32 act;

	/* Bit 0:1 - Action type */
#define ICE_SINGLE_ACT_TYPE_S	0x00
#define ICE_SINGLE_ACT_TYPE_M	(0x3 << ICE_SINGLE_ACT_TYPE_S)

	/* Bit 2 - Loop back enable
	 * Bit 3 - LAN enable
	 */
#define ICE_SINGLE_ACT_LB_ENABLE	BIT(2)
#define ICE_SINGLE_ACT_LAN_ENABLE	BIT(3)

	/* Action type = 0 - Forward to VSI or VSI list */
#define ICE_SINGLE_ACT_VSI_FORWARDING	0x0

#define ICE_SINGLE_ACT_VSI_ID_S		4
#define ICE_SINGLE_ACT_VSI_ID_M		(0x3FF << ICE_SINGLE_ACT_VSI_ID_S)
#define ICE_SINGLE_ACT_VSI_LIST_ID_S	4
#define ICE_SINGLE_ACT_VSI_LIST_ID_M	(0x3FF << ICE_SINGLE_ACT_VSI_LIST_ID_S)
	/* This bit needs to be set if action is forward to VSI list */
#define ICE_SINGLE_ACT_VSI_LIST		BIT(14)
#define ICE_SINGLE_ACT_VALID_BIT	BIT(17)
#define ICE_SINGLE_ACT_DROP		BIT(18)

	/* Action type = 1 - Forward to Queue of Queue group */
#define ICE_SINGLE_ACT_TO_Q		0x1
#define ICE_SINGLE_ACT_Q_INDEX_S	4
#define ICE_SINGLE_ACT_Q_INDEX_M	(0x7FF << ICE_SINGLE_ACT_Q_INDEX_S)
#define ICE_SINGLE_ACT_Q_REGION_S	15
#define ICE_SINGLE_ACT_Q_REGION_M	(0x7 << ICE_SINGLE_ACT_Q_REGION_S)
#define ICE_SINGLE_ACT_Q_PRIORITY	BIT(18)

	/* Action type = 2 - Prune */
#define ICE_SINGLE_ACT_PRUNE		0x2
#define ICE_SINGLE_ACT_EGRESS		BIT(15)
#define ICE_SINGLE_ACT_INGRESS		BIT(16)
#define ICE_SINGLE_ACT_PRUNET		BIT(17)
	/* Bit 18 should be set to 0 for this action */

	/* Action type = 2 - Pointer */
#define ICE_SINGLE_ACT_PTR		0x2
#define ICE_SINGLE_ACT_PTR_VAL_S	4
#define ICE_SINGLE_ACT_PTR_VAL_M	(0x1FFF << ICE_SINGLE_ACT_PTR_VAL_S)
	/* Bit 18 should be set to 1 */
#define ICE_SINGLE_ACT_PTR_BIT		BIT(18)

	/* Action type = 3 - Other actions. Last two bits
	 * are other action identifier
	 */
#define ICE_SINGLE_ACT_OTHER_ACTS		0x3
#define ICE_SINGLE_OTHER_ACT_IDENTIFIER_S	17
#define ICE_SINGLE_OTHER_ACT_IDENTIFIER_M	\
				(0x3 << ICE_SINGLE_OTHER_ACT_IDENTIFIER_S)

	/* Bit 17:18 - Defines other actions */
	/* Other action = 0 - Mirror VSI */
#define ICE_SINGLE_OTHER_ACT_MIRROR		0
#define ICE_SINGLE_ACT_MIRROR_VSI_ID_S	4
#define ICE_SINGLE_ACT_MIRROR_VSI_ID_M	\
				(0x3FF << ICE_SINGLE_ACT_MIRROR_VSI_ID_S)

	/* Other action = 3 - Set Stat count */
#define ICE_SINGLE_OTHER_ACT_STAT_COUNT		3
#define ICE_SINGLE_ACT_STAT_COUNT_INDEX_S	4
#define ICE_SINGLE_ACT_STAT_COUNT_INDEX_M	\
				(0x7F << ICE_SINGLE_ACT_STAT_COUNT_INDEX_S)

	__le16 index; /* The index of the rule in the lookup table */
	/* Length and values of the header to be matched per recipe or
	 * lookup-type
	 */
	__le16 hdr_len;
	u8 hdr[];
};

/* Add/Update/Remove large action command/response entry
 * "index" is returned as part of a response to a successful Add command, and
 * can be used to identify the action for Update/Get/Remove commands.
 */
struct ice_sw_rule_lg_act {
	__le16 index; /* Index in large action table */
	__le16 size;
	/* Max number of large actions */
#define ICE_MAX_LG_ACT	4
	/* Bit 0:1 - Action type */
#define ICE_LG_ACT_TYPE_S	0
#define ICE_LG_ACT_TYPE_M	(0x7 << ICE_LG_ACT_TYPE_S)

	/* Action type = 0 - Forward to VSI or VSI list */
#define ICE_LG_ACT_VSI_FORWARDING	0
#define ICE_LG_ACT_VSI_ID_S		3
#define ICE_LG_ACT_VSI_ID_M		(0x3FF << ICE_LG_ACT_VSI_ID_S)
#define ICE_LG_ACT_VSI_LIST_ID_S	3
#define ICE_LG_ACT_VSI_LIST_ID_M	(0x3FF << ICE_LG_ACT_VSI_LIST_ID_S)
	/* This bit needs to be set if action is forward to VSI list */
#define ICE_LG_ACT_VSI_LIST		BIT(13)

#define ICE_LG_ACT_VALID_BIT		BIT(16)

	/* Action type = 1 - Forward to Queue of Queue group */
#define ICE_LG_ACT_TO_Q			0x1
#define ICE_LG_ACT_Q_INDEX_S		3
#define ICE_LG_ACT_Q_INDEX_M		(0x7FF << ICE_LG_ACT_Q_INDEX_S)
#define ICE_LG_ACT_Q_REGION_S		14
#define ICE_LG_ACT_Q_REGION_M		(0x7 << ICE_LG_ACT_Q_REGION_S)
#define ICE_LG_ACT_Q_PRIORITY_SET	BIT(17)

	/* Action type = 2 - Prune */
#define ICE_LG_ACT_PRUNE		0x2
#define ICE_LG_ACT_EGRESS		BIT(14)
#define ICE_LG_ACT_INGRESS		BIT(15)
#define ICE_LG_ACT_PRUNET		BIT(16)

	/* Action type = 3 - Mirror VSI */
#define ICE_LG_OTHER_ACT_MIRROR		0x3
#define ICE_LG_ACT_MIRROR_VSI_ID_S	3
#define ICE_LG_ACT_MIRROR_VSI_ID_M	(0x3FF << ICE_LG_ACT_MIRROR_VSI_ID_S)

	/* Action type = 5 - Generic Value */
#define ICE_LG_ACT_GENERIC		0x5
#define ICE_LG_ACT_GENERIC_VALUE_S	3
#define ICE_LG_ACT_GENERIC_VALUE_M	(0xFFFF << ICE_LG_ACT_GENERIC_VALUE_S)
#define ICE_LG_ACT_GENERIC_OFFSET_S	19
#define ICE_LG_ACT_GENERIC_OFFSET_M	(0x7 << ICE_LG_ACT_GENERIC_OFFSET_S)
#define ICE_LG_ACT_GENERIC_PRIORITY_S	22
#define ICE_LG_ACT_GENERIC_PRIORITY_M	(0x7 << ICE_LG_ACT_GENERIC_PRIORITY_S)
#define ICE_LG_ACT_GENERIC_OFF_RX_DESC_PROF_IDX	7

	/* Action = 7 - Set Stat count */
#define ICE_LG_ACT_STAT_COUNT		0x7
#define ICE_LG_ACT_STAT_COUNT_S		3
#define ICE_LG_ACT_STAT_COUNT_M		(0x7F << ICE_LG_ACT_STAT_COUNT_S)
	__le32 act[]; /* array of size for actions */
};

/* Add/Update/Remove VSI list command/response entry
 * "index" is returned as part of a response to a successful Add command, and
 * can be used to identify the VSI list for Update/Get/Remove commands.
 */
struct ice_sw_rule_vsi_list {
	__le16 index; /* Index of VSI/Prune list */
	__le16 number_vsi;
	__le16 vsi[]; /* Array of number_vsi VSI numbers */
};

/* Query VSI list command/response entry */
struct ice_sw_rule_vsi_list_query {
	__le16 index;
	DECLARE_BITMAP(vsi_list, ICE_MAX_VSI);
} __packed;

/* Add switch rule response:
 * Content of return buffer is same as the input buffer. The status field and
 * LUT index are updated as part of the response
 */
struct ice_aqc_sw_rules_elem {
	__le16 type; /* Switch rule type, one of T_... */
#define ICE_AQC_SW_RULES_T_LKUP_RX		0x0
#define ICE_AQC_SW_RULES_T_LKUP_TX		0x1
#define ICE_AQC_SW_RULES_T_LG_ACT		0x2
#define ICE_AQC_SW_RULES_T_VSI_LIST_SET		0x3
#define ICE_AQC_SW_RULES_T_VSI_LIST_CLEAR	0x4
#define ICE_AQC_SW_RULES_T_PRUNE_LIST_SET	0x5
#define ICE_AQC_SW_RULES_T_PRUNE_LIST_CLEAR	0x6
	__le16 status;
	union {
		struct ice_sw_rule_lkup_rx_tx lkup_tx_rx;
		struct ice_sw_rule_lg_act lg_act;
		struct ice_sw_rule_vsi_list vsi_list;
		struct ice_sw_rule_vsi_list_query vsi_list_query;
	} __packed pdata;
};

/* Get Default Topology (indirect 0x0400) */
struct ice_aqc_get_topo {
	u8 port_num;
	u8 num_branches;
	__le16 reserved1;
	__le32 reserved2;
	__le32 addr_high;
	__le32 addr_low;
};

/* Update TSE (indirect 0x0403)
 * Get TSE (indirect 0x0404)
 * Add TSE (indirect 0x0401)
 * Delete TSE (indirect 0x040F)
 * Move TSE (indirect 0x0408)
 * Suspend Nodes (indirect 0x0409)
 * Resume Nodes (indirect 0x040A)
 */
struct ice_aqc_sched_elem_cmd {
	__le16 num_elem_req;	/* Used by commands */
	__le16 num_elem_resp;	/* Used by responses */
	__le32 reserved;
	__le32 addr_high;
	__le32 addr_low;
};

struct ice_aqc_elem_info_bw {
	__le16 bw_profile_idx;
	__le16 bw_alloc;
};

struct ice_aqc_txsched_elem {
	u8 elem_type; /* Special field, reserved for some aq calls */
#define ICE_AQC_ELEM_TYPE_UNDEFINED		0x0
#define ICE_AQC_ELEM_TYPE_ROOT_PORT		0x1
#define ICE_AQC_ELEM_TYPE_TC			0x2
#define ICE_AQC_ELEM_TYPE_SE_GENERIC		0x3
#define ICE_AQC_ELEM_TYPE_ENTRY_POINT		0x4
#define ICE_AQC_ELEM_TYPE_LEAF			0x5
#define ICE_AQC_ELEM_TYPE_SE_PADDED		0x6
	u8 valid_sections;
#define ICE_AQC_ELEM_VALID_GENERIC		BIT(0)
#define ICE_AQC_ELEM_VALID_CIR			BIT(1)
#define ICE_AQC_ELEM_VALID_EIR			BIT(2)
#define ICE_AQC_ELEM_VALID_SHARED		BIT(3)
	u8 generic;
#define ICE_AQC_ELEM_GENERIC_MODE_M		0x1
#define ICE_AQC_ELEM_GENERIC_PRIO_S		0x1
#define ICE_AQC_ELEM_GENERIC_PRIO_M	(0x7 << ICE_AQC_ELEM_GENERIC_PRIO_S)
#define ICE_AQC_ELEM_GENERIC_SP_S		0x4
#define ICE_AQC_ELEM_GENERIC_SP_M	(0x1 << ICE_AQC_ELEM_GENERIC_SP_S)
#define ICE_AQC_ELEM_GENERIC_ADJUST_VAL_S	0x5
#define ICE_AQC_ELEM_GENERIC_ADJUST_VAL_M	\
	(0x3 << ICE_AQC_ELEM_GENERIC_ADJUST_VAL_S)
	u8 flags; /* Special field, reserved for some aq calls */
#define ICE_AQC_ELEM_FLAG_SUSPEND_M		0x1
	struct ice_aqc_elem_info_bw cir_bw;
	struct ice_aqc_elem_info_bw eir_bw;
	__le16 srl_id;
	__le16 reserved2;
};

struct ice_aqc_txsched_elem_data {
	__le32 parent_teid;
	__le32 node_teid;
	struct ice_aqc_txsched_elem data;
};

struct ice_aqc_txsched_topo_grp_info_hdr {
	__le32 parent_teid;
	__le16 num_elems;
	__le16 reserved2;
};

struct ice_aqc_add_elem {
	struct ice_aqc_txsched_topo_grp_info_hdr hdr;
	struct ice_aqc_txsched_elem_data generic[];
};

struct ice_aqc_get_topo_elem {
	struct ice_aqc_txsched_topo_grp_info_hdr hdr;
	struct ice_aqc_txsched_elem_data
		generic[ICE_AQC_TOPO_MAX_LEVEL_NUM];
};

struct ice_aqc_delete_elem {
	struct ice_aqc_txsched_topo_grp_info_hdr hdr;
	__le32 teid[];
};

/* Query Port ETS (indirect 0x040E)
 *
 * This indirect command is used to query port TC node configuration.
 */
struct ice_aqc_query_port_ets {
	__le32 port_teid;
	__le32 reserved;
	__le32 addr_high;
	__le32 addr_low;
};

struct ice_aqc_port_ets_elem {
	u8 tc_valid_bits;
	u8 reserved[3];
	/* 3 bits for UP per TC 0-7, 4th byte reserved */
	__le32 up2tc;
	u8 tc_bw_share[8];
	__le32 port_eir_prof_id;
	__le32 port_cir_prof_id;
	/* 3 bits per Node priority to TC 0-7, 4th byte reserved */
	__le32 tc_node_prio;
#define ICE_TC_NODE_PRIO_S	0x4
	u8 reserved1[4];
	__le32 tc_node_teid[8]; /* Used for response, reserved in command */
};

/* Rate limiting profile for
 * Add RL profile (indirect 0x0410)
 * Query RL profile (indirect 0x0411)
 * Remove RL profile (indirect 0x0415)
 * These indirect commands acts on single or multiple
 * RL profiles with specified data.
 */
struct ice_aqc_rl_profile {
	__le16 num_profiles;
	__le16 num_processed; /* Only for response. Reserved in Command. */
	u8 reserved[4];
	__le32 addr_high;
	__le32 addr_low;
};

struct ice_aqc_rl_profile_elem {
	u8 level;
	u8 flags;
#define ICE_AQC_RL_PROFILE_TYPE_S	0x0
#define ICE_AQC_RL_PROFILE_TYPE_M	(0x3 << ICE_AQC_RL_PROFILE_TYPE_S)
#define ICE_AQC_RL_PROFILE_TYPE_CIR	0
#define ICE_AQC_RL_PROFILE_TYPE_EIR	1
#define ICE_AQC_RL_PROFILE_TYPE_SRL	2
/* The following flag is used for Query RL Profile Data */
#define ICE_AQC_RL_PROFILE_INVAL_S	0x7
#define ICE_AQC_RL_PROFILE_INVAL_M	(0x1 << ICE_AQC_RL_PROFILE_INVAL_S)

	__le16 profile_id;
	__le16 max_burst_size;
	__le16 rl_multiply;
	__le16 wake_up_calc;
	__le16 rl_encode;
};

/* Query Scheduler Resource Allocation (indirect 0x0412)
 * This indirect command retrieves the scheduler resources allocated by
 * EMP Firmware to the given PF.
 */
struct ice_aqc_query_txsched_res {
	u8 reserved[8];
	__le32 addr_high;
	__le32 addr_low;
};

struct ice_aqc_generic_sched_props {
	__le16 phys_levels;
	__le16 logical_levels;
	u8 flattening_bitmap;
	u8 max_device_cgds;
	u8 max_pf_cgds;
	u8 rsvd0;
	__le16 rdma_qsets;
	u8 rsvd1[22];
};

struct ice_aqc_layer_props {
	u8 logical_layer;
	u8 chunk_size;
	__le16 max_device_nodes;
	__le16 max_pf_nodes;
	u8 rsvd0[4];
	__le16 max_sibl_grp_sz;
	__le16 max_cir_rl_profiles;
	__le16 max_eir_rl_profiles;
	__le16 max_srl_profiles;
	u8 rsvd1[14];
};

struct ice_aqc_query_txsched_res_resp {
	struct ice_aqc_generic_sched_props sched_props;
	struct ice_aqc_layer_props layer_props[ICE_AQC_TOPO_MAX_LEVEL_NUM];
};

/* Get PHY capabilities (indirect 0x0600) */
struct ice_aqc_get_phy_caps {
	u8 lport_num;
	u8 reserved;
	__le16 param0;
	/* 18.0 - Report qualified modules */
#define ICE_AQC_GET_PHY_RQM		BIT(0)
	/* 18.1 - 18.2 : Report mode
	 * 00b - Report NVM capabilities
	 * 01b - Report topology capabilities
	 * 10b - Report SW configured
	 */
#define ICE_AQC_REPORT_MODE_S			1
#define ICE_AQC_REPORT_MODE_M			(3 << ICE_AQC_REPORT_MODE_S)
#define ICE_AQC_REPORT_TOPO_CAP_NO_MEDIA	0
#define ICE_AQC_REPORT_TOPO_CAP_MEDIA		BIT(1)
#define ICE_AQC_REPORT_ACTIVE_CFG		BIT(2)
	__le32 reserved1;
	__le32 addr_high;
	__le32 addr_low;
};

/* This is #define of PHY type (Extended):
 * The first set of defines is for phy_type_low.
 */
#define ICE_PHY_TYPE_LOW_100BASE_TX		BIT_ULL(0)
#define ICE_PHY_TYPE_LOW_100M_SGMII		BIT_ULL(1)
#define ICE_PHY_TYPE_LOW_1000BASE_T		BIT_ULL(2)
#define ICE_PHY_TYPE_LOW_1000BASE_SX		BIT_ULL(3)
#define ICE_PHY_TYPE_LOW_1000BASE_LX		BIT_ULL(4)
#define ICE_PHY_TYPE_LOW_1000BASE_KX		BIT_ULL(5)
#define ICE_PHY_TYPE_LOW_1G_SGMII		BIT_ULL(6)
#define ICE_PHY_TYPE_LOW_2500BASE_T		BIT_ULL(7)
#define ICE_PHY_TYPE_LOW_2500BASE_X		BIT_ULL(8)
#define ICE_PHY_TYPE_LOW_2500BASE_KX		BIT_ULL(9)
#define ICE_PHY_TYPE_LOW_5GBASE_T		BIT_ULL(10)
#define ICE_PHY_TYPE_LOW_5GBASE_KR		BIT_ULL(11)
#define ICE_PHY_TYPE_LOW_10GBASE_T		BIT_ULL(12)
#define ICE_PHY_TYPE_LOW_10G_SFI_DA		BIT_ULL(13)
#define ICE_PHY_TYPE_LOW_10GBASE_SR		BIT_ULL(14)
#define ICE_PHY_TYPE_LOW_10GBASE_LR		BIT_ULL(15)
#define ICE_PHY_TYPE_LOW_10GBASE_KR_CR1		BIT_ULL(16)
#define ICE_PHY_TYPE_LOW_10G_SFI_AOC_ACC	BIT_ULL(17)
#define ICE_PHY_TYPE_LOW_10G_SFI_C2C		BIT_ULL(18)
#define ICE_PHY_TYPE_LOW_25GBASE_T		BIT_ULL(19)
#define ICE_PHY_TYPE_LOW_25GBASE_CR		BIT_ULL(20)
#define ICE_PHY_TYPE_LOW_25GBASE_CR_S		BIT_ULL(21)
#define ICE_PHY_TYPE_LOW_25GBASE_CR1		BIT_ULL(22)
#define ICE_PHY_TYPE_LOW_25GBASE_SR		BIT_ULL(23)
#define ICE_PHY_TYPE_LOW_25GBASE_LR		BIT_ULL(24)
#define ICE_PHY_TYPE_LOW_25GBASE_KR		BIT_ULL(25)
#define ICE_PHY_TYPE_LOW_25GBASE_KR_S		BIT_ULL(26)
#define ICE_PHY_TYPE_LOW_25GBASE_KR1		BIT_ULL(27)
#define ICE_PHY_TYPE_LOW_25G_AUI_AOC_ACC	BIT_ULL(28)
#define ICE_PHY_TYPE_LOW_25G_AUI_C2C		BIT_ULL(29)
#define ICE_PHY_TYPE_LOW_40GBASE_CR4		BIT_ULL(30)
#define ICE_PHY_TYPE_LOW_40GBASE_SR4		BIT_ULL(31)
#define ICE_PHY_TYPE_LOW_40GBASE_LR4		BIT_ULL(32)
#define ICE_PHY_TYPE_LOW_40GBASE_KR4		BIT_ULL(33)
#define ICE_PHY_TYPE_LOW_40G_XLAUI_AOC_ACC	BIT_ULL(34)
#define ICE_PHY_TYPE_LOW_40G_XLAUI		BIT_ULL(35)
#define ICE_PHY_TYPE_LOW_50GBASE_CR2		BIT_ULL(36)
#define ICE_PHY_TYPE_LOW_50GBASE_SR2		BIT_ULL(37)
#define ICE_PHY_TYPE_LOW_50GBASE_LR2		BIT_ULL(38)
#define ICE_PHY_TYPE_LOW_50GBASE_KR2		BIT_ULL(39)
#define ICE_PHY_TYPE_LOW_50G_LAUI2_AOC_ACC	BIT_ULL(40)
#define ICE_PHY_TYPE_LOW_50G_LAUI2		BIT_ULL(41)
#define ICE_PHY_TYPE_LOW_50G_AUI2_AOC_ACC	BIT_ULL(42)
#define ICE_PHY_TYPE_LOW_50G_AUI2		BIT_ULL(43)
#define ICE_PHY_TYPE_LOW_50GBASE_CP		BIT_ULL(44)
#define ICE_PHY_TYPE_LOW_50GBASE_SR		BIT_ULL(45)
#define ICE_PHY_TYPE_LOW_50GBASE_FR		BIT_ULL(46)
#define ICE_PHY_TYPE_LOW_50GBASE_LR		BIT_ULL(47)
#define ICE_PHY_TYPE_LOW_50GBASE_KR_PAM4	BIT_ULL(48)
#define ICE_PHY_TYPE_LOW_50G_AUI1_AOC_ACC	BIT_ULL(49)
#define ICE_PHY_TYPE_LOW_50G_AUI1		BIT_ULL(50)
#define ICE_PHY_TYPE_LOW_100GBASE_CR4		BIT_ULL(51)
#define ICE_PHY_TYPE_LOW_100GBASE_SR4		BIT_ULL(52)
#define ICE_PHY_TYPE_LOW_100GBASE_LR4		BIT_ULL(53)
#define ICE_PHY_TYPE_LOW_100GBASE_KR4		BIT_ULL(54)
#define ICE_PHY_TYPE_LOW_100G_CAUI4_AOC_ACC	BIT_ULL(55)
#define ICE_PHY_TYPE_LOW_100G_CAUI4		BIT_ULL(56)
#define ICE_PHY_TYPE_LOW_100G_AUI4_AOC_ACC	BIT_ULL(57)
#define ICE_PHY_TYPE_LOW_100G_AUI4		BIT_ULL(58)
#define ICE_PHY_TYPE_LOW_100GBASE_CR_PAM4	BIT_ULL(59)
#define ICE_PHY_TYPE_LOW_100GBASE_KR_PAM4	BIT_ULL(60)
#define ICE_PHY_TYPE_LOW_100GBASE_CP2		BIT_ULL(61)
#define ICE_PHY_TYPE_LOW_100GBASE_SR2		BIT_ULL(62)
#define ICE_PHY_TYPE_LOW_100GBASE_DR		BIT_ULL(63)
#define ICE_PHY_TYPE_LOW_MAX_INDEX		63
/* The second set of defines is for phy_type_high. */
#define ICE_PHY_TYPE_HIGH_100GBASE_KR2_PAM4	BIT_ULL(0)
#define ICE_PHY_TYPE_HIGH_100G_CAUI2_AOC_ACC	BIT_ULL(1)
#define ICE_PHY_TYPE_HIGH_100G_CAUI2		BIT_ULL(2)
#define ICE_PHY_TYPE_HIGH_100G_AUI2_AOC_ACC	BIT_ULL(3)
#define ICE_PHY_TYPE_HIGH_100G_AUI2		BIT_ULL(4)
#define ICE_PHY_TYPE_HIGH_MAX_INDEX		5

struct ice_aqc_get_phy_caps_data {
	__le64 phy_type_low; /* Use values from ICE_PHY_TYPE_LOW_* */
	__le64 phy_type_high; /* Use values from ICE_PHY_TYPE_HIGH_* */
	u8 caps;
#define ICE_AQC_PHY_EN_TX_LINK_PAUSE			BIT(0)
#define ICE_AQC_PHY_EN_RX_LINK_PAUSE			BIT(1)
#define ICE_AQC_PHY_LOW_POWER_MODE			BIT(2)
#define ICE_AQC_PHY_EN_LINK				BIT(3)
#define ICE_AQC_PHY_AN_MODE				BIT(4)
#define ICE_AQC_GET_PHY_EN_MOD_QUAL			BIT(5)
#define ICE_AQC_PHY_EN_AUTO_FEC				BIT(7)
#define ICE_AQC_PHY_CAPS_MASK				ICE_M(0xff, 0)
	u8 low_power_ctrl_an;
#define ICE_AQC_PHY_EN_D3COLD_LOW_POWER_AUTONEG		BIT(0)
#define ICE_AQC_PHY_AN_EN_CLAUSE28			BIT(1)
#define ICE_AQC_PHY_AN_EN_CLAUSE73			BIT(2)
#define ICE_AQC_PHY_AN_EN_CLAUSE37			BIT(3)
	__le16 eee_cap;
#define ICE_AQC_PHY_EEE_EN_100BASE_TX			BIT(0)
#define ICE_AQC_PHY_EEE_EN_1000BASE_T			BIT(1)
#define ICE_AQC_PHY_EEE_EN_10GBASE_T			BIT(2)
#define ICE_AQC_PHY_EEE_EN_1000BASE_KX			BIT(3)
#define ICE_AQC_PHY_EEE_EN_10GBASE_KR			BIT(4)
#define ICE_AQC_PHY_EEE_EN_25GBASE_KR			BIT(5)
#define ICE_AQC_PHY_EEE_EN_40GBASE_KR4			BIT(6)
	__le16 eeer_value;
	u8 phy_id_oui[4]; /* PHY/Module ID connected on the port */
	u8 phy_fw_ver[8];
	u8 link_fec_options;
#define ICE_AQC_PHY_FEC_10G_KR_40G_KR4_EN		BIT(0)
#define ICE_AQC_PHY_FEC_10G_KR_40G_KR4_REQ		BIT(1)
#define ICE_AQC_PHY_FEC_25G_RS_528_REQ			BIT(2)
#define ICE_AQC_PHY_FEC_25G_KR_REQ			BIT(3)
#define ICE_AQC_PHY_FEC_25G_RS_544_REQ			BIT(4)
#define ICE_AQC_PHY_FEC_25G_RS_CLAUSE91_EN		BIT(6)
#define ICE_AQC_PHY_FEC_25G_KR_CLAUSE74_EN		BIT(7)
#define ICE_AQC_PHY_FEC_MASK				ICE_M(0xdf, 0)
	u8 module_compliance_enforcement;
#define ICE_AQC_MOD_ENFORCE_STRICT_MODE			BIT(0)
	u8 extended_compliance_code;
#define ICE_MODULE_TYPE_TOTAL_BYTE			3
	u8 module_type[ICE_MODULE_TYPE_TOTAL_BYTE];
#define ICE_AQC_MOD_TYPE_BYTE0_SFP_PLUS			0xA0
#define ICE_AQC_MOD_TYPE_BYTE0_QSFP_PLUS		0x80
#define ICE_AQC_MOD_TYPE_IDENT				1
#define ICE_AQC_MOD_TYPE_BYTE1_SFP_PLUS_CU_PASSIVE	BIT(0)
#define ICE_AQC_MOD_TYPE_BYTE1_SFP_PLUS_CU_ACTIVE	BIT(1)
#define ICE_AQC_MOD_TYPE_BYTE1_10G_BASE_SR		BIT(4)
#define ICE_AQC_MOD_TYPE_BYTE1_10G_BASE_LR		BIT(5)
#define ICE_AQC_MOD_TYPE_BYTE1_10G_BASE_LRM		BIT(6)
#define ICE_AQC_MOD_TYPE_BYTE1_10G_BASE_ER		BIT(7)
#define ICE_AQC_MOD_TYPE_BYTE2_SFP_PLUS			0xA0
#define ICE_AQC_MOD_TYPE_BYTE2_QSFP_PLUS		0x86
	u8 qualified_module_count;
	u8 rsvd2[7];	/* Bytes 47:41 reserved */
#define ICE_AQC_QUAL_MOD_COUNT_MAX			16
	struct {
		u8 v_oui[3];
		u8 rsvd3;
		u8 v_part[16];
		__le32 v_rev;
		__le64 rsvd4;
	} qual_modules[ICE_AQC_QUAL_MOD_COUNT_MAX];
};

/* Set PHY capabilities (direct 0x0601)
 * NOTE: This command must be followed by setup link and restart auto-neg
 */
struct ice_aqc_set_phy_cfg {
	u8 lport_num;
	u8 reserved[7];
	__le32 addr_high;
	__le32 addr_low;
};

/* Set PHY config command data structure */
struct ice_aqc_set_phy_cfg_data {
	__le64 phy_type_low; /* Use values from ICE_PHY_TYPE_LOW_* */
	__le64 phy_type_high; /* Use values from ICE_PHY_TYPE_HIGH_* */
	u8 caps;
#define ICE_AQ_PHY_ENA_VALID_MASK	ICE_M(0xef, 0)
#define ICE_AQ_PHY_ENA_TX_PAUSE_ABILITY	BIT(0)
#define ICE_AQ_PHY_ENA_RX_PAUSE_ABILITY	BIT(1)
#define ICE_AQ_PHY_ENA_LOW_POWER	BIT(2)
#define ICE_AQ_PHY_ENA_LINK		BIT(3)
#define ICE_AQ_PHY_ENA_AUTO_LINK_UPDT	BIT(5)
#define ICE_AQ_PHY_ENA_LESM		BIT(6)
#define ICE_AQ_PHY_ENA_AUTO_FEC		BIT(7)
	u8 low_power_ctrl_an;
	__le16 eee_cap; /* Value from ice_aqc_get_phy_caps */
	__le16 eeer_value;
	u8 link_fec_opt; /* Use defines from ice_aqc_get_phy_caps */
	u8 module_compliance_enforcement;
};

/* Set MAC Config command data structure (direct 0x0603) */
struct ice_aqc_set_mac_cfg {
	__le16 max_frame_size;
	u8 params;
#define ICE_AQ_SET_MAC_PACE_S		3
#define ICE_AQ_SET_MAC_PACE_M		(0xF << ICE_AQ_SET_MAC_PACE_S)
#define ICE_AQ_SET_MAC_PACE_TYPE_M	BIT(7)
#define ICE_AQ_SET_MAC_PACE_TYPE_RATE	0
#define ICE_AQ_SET_MAC_PACE_TYPE_FIXED	ICE_AQ_SET_MAC_PACE_TYPE_M
	u8 tx_tmr_priority;
	__le16 tx_tmr_value;
	__le16 fc_refresh_threshold;
	u8 drop_opts;
#define ICE_AQ_SET_MAC_AUTO_DROP_MASK		BIT(0)
#define ICE_AQ_SET_MAC_AUTO_DROP_NONE		0
#define ICE_AQ_SET_MAC_AUTO_DROP_BLOCKING_PKTS	BIT(0)
	u8 reserved[7];
};

/* Restart AN command data structure (direct 0x0605)
 * Also used for response, with only the lport_num field present.
 */
struct ice_aqc_restart_an {
	u8 lport_num;
	u8 reserved;
	u8 cmd_flags;
#define ICE_AQC_RESTART_AN_LINK_RESTART	BIT(1)
#define ICE_AQC_RESTART_AN_LINK_ENABLE	BIT(2)
	u8 reserved2[13];
};

/* Get link status (indirect 0x0607), also used for Link Status Event */
struct ice_aqc_get_link_status {
	u8 lport_num;
	u8 reserved;
	__le16 cmd_flags;
#define ICE_AQ_LSE_M			0x3
#define ICE_AQ_LSE_NOP			0x0
#define ICE_AQ_LSE_DIS			0x2
#define ICE_AQ_LSE_ENA			0x3
	/* only response uses this flag */
#define ICE_AQ_LSE_IS_ENABLED		0x1
	__le32 reserved2;
	__le32 addr_high;
	__le32 addr_low;
};

/* Get link status response data structure, also used for Link Status Event */
struct ice_aqc_get_link_status_data {
	u8 topo_media_conflict;
#define ICE_AQ_LINK_TOPO_CONFLICT	BIT(0)
#define ICE_AQ_LINK_MEDIA_CONFLICT	BIT(1)
#define ICE_AQ_LINK_TOPO_CORRUPT	BIT(2)
#define ICE_AQ_LINK_TOPO_UNREACH_PRT	BIT(4)
#define ICE_AQ_LINK_TOPO_UNDRUTIL_PRT	BIT(5)
#define ICE_AQ_LINK_TOPO_UNDRUTIL_MEDIA	BIT(6)
#define ICE_AQ_LINK_TOPO_UNSUPP_MEDIA	BIT(7)
	u8 reserved1;
	u8 link_info;
#define ICE_AQ_LINK_UP			BIT(0)	/* Link Status */
#define ICE_AQ_LINK_FAULT		BIT(1)
#define ICE_AQ_LINK_FAULT_TX		BIT(2)
#define ICE_AQ_LINK_FAULT_RX		BIT(3)
#define ICE_AQ_LINK_FAULT_REMOTE	BIT(4)
#define ICE_AQ_LINK_UP_PORT		BIT(5)	/* External Port Link Status */
#define ICE_AQ_MEDIA_AVAILABLE		BIT(6)
#define ICE_AQ_SIGNAL_DETECT		BIT(7)
	u8 an_info;
#define ICE_AQ_AN_COMPLETED		BIT(0)
#define ICE_AQ_LP_AN_ABILITY		BIT(1)
#define ICE_AQ_PD_FAULT			BIT(2)	/* Parallel Detection Fault */
#define ICE_AQ_FEC_EN			BIT(3)
#define ICE_AQ_PHY_LOW_POWER		BIT(4)	/* Low Power State */
#define ICE_AQ_LINK_PAUSE_TX		BIT(5)
#define ICE_AQ_LINK_PAUSE_RX		BIT(6)
#define ICE_AQ_QUALIFIED_MODULE		BIT(7)
	u8 ext_info;
#define ICE_AQ_LINK_PHY_TEMP_ALARM	BIT(0)
#define ICE_AQ_LINK_EXCESSIVE_ERRORS	BIT(1)	/* Excessive Link Errors */
	/* Port Tx Suspended */
#define ICE_AQ_LINK_TX_S		2
#define ICE_AQ_LINK_TX_M		(0x03 << ICE_AQ_LINK_TX_S)
#define ICE_AQ_LINK_TX_ACTIVE		0
#define ICE_AQ_LINK_TX_DRAINED		1
#define ICE_AQ_LINK_TX_FLUSHED		3
	u8 reserved2;
	__le16 max_frame_size;
	u8 cfg;
#define ICE_AQ_LINK_25G_KR_FEC_EN	BIT(0)
#define ICE_AQ_LINK_25G_RS_528_FEC_EN	BIT(1)
#define ICE_AQ_LINK_25G_RS_544_FEC_EN	BIT(2)
#define ICE_AQ_FEC_MASK			ICE_M(0x7, 0)
	/* Pacing Config */
#define ICE_AQ_CFG_PACING_S		3
#define ICE_AQ_CFG_PACING_M		(0xF << ICE_AQ_CFG_PACING_S)
#define ICE_AQ_CFG_PACING_TYPE_M	BIT(7)
#define ICE_AQ_CFG_PACING_TYPE_AVG	0
#define ICE_AQ_CFG_PACING_TYPE_FIXED	ICE_AQ_CFG_PACING_TYPE_M
	/* External Device Power Ability */
	u8 power_desc;
#define ICE_AQ_PWR_CLASS_M		0x3
#define ICE_AQ_LINK_PWR_BASET_LOW_HIGH	0
#define ICE_AQ_LINK_PWR_BASET_HIGH	1
#define ICE_AQ_LINK_PWR_QSFP_CLASS_1	0
#define ICE_AQ_LINK_PWR_QSFP_CLASS_2	1
#define ICE_AQ_LINK_PWR_QSFP_CLASS_3	2
#define ICE_AQ_LINK_PWR_QSFP_CLASS_4	3
	__le16 link_speed;
#define ICE_AQ_LINK_SPEED_M		0x7FF
#define ICE_AQ_LINK_SPEED_10MB		BIT(0)
#define ICE_AQ_LINK_SPEED_100MB		BIT(1)
#define ICE_AQ_LINK_SPEED_1000MB	BIT(2)
#define ICE_AQ_LINK_SPEED_2500MB	BIT(3)
#define ICE_AQ_LINK_SPEED_5GB		BIT(4)
#define ICE_AQ_LINK_SPEED_10GB		BIT(5)
#define ICE_AQ_LINK_SPEED_20GB		BIT(6)
#define ICE_AQ_LINK_SPEED_25GB		BIT(7)
#define ICE_AQ_LINK_SPEED_40GB		BIT(8)
#define ICE_AQ_LINK_SPEED_50GB		BIT(9)
#define ICE_AQ_LINK_SPEED_100GB		BIT(10)
#define ICE_AQ_LINK_SPEED_UNKNOWN	BIT(15)
	__le32 reserved3; /* Aligns next field to 8-byte boundary */
	__le64 phy_type_low; /* Use values from ICE_PHY_TYPE_LOW_* */
	__le64 phy_type_high; /* Use values from ICE_PHY_TYPE_HIGH_* */
};

/* Set event mask command (direct 0x0613) */
struct ice_aqc_set_event_mask {
	u8	lport_num;
	u8	reserved[7];
	__le16	event_mask;
#define ICE_AQ_LINK_EVENT_UPDOWN		BIT(1)
#define ICE_AQ_LINK_EVENT_MEDIA_NA		BIT(2)
#define ICE_AQ_LINK_EVENT_LINK_FAULT		BIT(3)
#define ICE_AQ_LINK_EVENT_PHY_TEMP_ALARM	BIT(4)
#define ICE_AQ_LINK_EVENT_EXCESSIVE_ERRORS	BIT(5)
#define ICE_AQ_LINK_EVENT_SIGNAL_DETECT		BIT(6)
#define ICE_AQ_LINK_EVENT_AN_COMPLETED		BIT(7)
#define ICE_AQ_LINK_EVENT_MODULE_QUAL_FAIL	BIT(8)
#define ICE_AQ_LINK_EVENT_PORT_TX_SUSPENDED	BIT(9)
	u8	reserved1[6];
};

/* Set MAC Loopback command (direct 0x0620) */
struct ice_aqc_set_mac_lb {
	u8 lb_mode;
#define ICE_AQ_MAC_LB_EN		BIT(0)
#define ICE_AQ_MAC_LB_OSC_CLK		BIT(1)
	u8 reserved[15];
};

struct ice_aqc_link_topo_addr {
	u8 lport_num;
	u8 lport_num_valid;
#define ICE_AQC_LINK_TOPO_PORT_NUM_VALID	BIT(0)
	u8 node_type_ctx;
#define ICE_AQC_LINK_TOPO_NODE_TYPE_S		0
#define ICE_AQC_LINK_TOPO_NODE_TYPE_M	(0xF << ICE_AQC_LINK_TOPO_NODE_TYPE_S)
#define ICE_AQC_LINK_TOPO_NODE_TYPE_PHY		0
#define ICE_AQC_LINK_TOPO_NODE_TYPE_GPIO_CTRL	1
#define ICE_AQC_LINK_TOPO_NODE_TYPE_MUX_CTRL	2
#define ICE_AQC_LINK_TOPO_NODE_TYPE_LED_CTRL	3
#define ICE_AQC_LINK_TOPO_NODE_TYPE_LED		4
#define ICE_AQC_LINK_TOPO_NODE_TYPE_THERMAL	5
#define ICE_AQC_LINK_TOPO_NODE_TYPE_CAGE	6
#define ICE_AQC_LINK_TOPO_NODE_TYPE_MEZZ	7
#define ICE_AQC_LINK_TOPO_NODE_TYPE_ID_EEPROM	8
#define ICE_AQC_LINK_TOPO_NODE_CTX_S		4
#define ICE_AQC_LINK_TOPO_NODE_CTX_M		\
				(0xF << ICE_AQC_LINK_TOPO_NODE_CTX_S)
#define ICE_AQC_LINK_TOPO_NODE_CTX_GLOBAL	0
#define ICE_AQC_LINK_TOPO_NODE_CTX_BOARD	1
#define ICE_AQC_LINK_TOPO_NODE_CTX_PORT		2
#define ICE_AQC_LINK_TOPO_NODE_CTX_NODE		3
#define ICE_AQC_LINK_TOPO_NODE_CTX_PROVIDED	4
#define ICE_AQC_LINK_TOPO_NODE_CTX_OVERRIDE	5
	u8 index;
	__le16 handle;
#define ICE_AQC_LINK_TOPO_HANDLE_S	0
#define ICE_AQC_LINK_TOPO_HANDLE_M	(0x3FF << ICE_AQC_LINK_TOPO_HANDLE_S)
/* Used to decode the handle field */
#define ICE_AQC_LINK_TOPO_HANDLE_BRD_TYPE_M	BIT(9)
#define ICE_AQC_LINK_TOPO_HANDLE_BRD_TYPE_LOM	BIT(9)
#define ICE_AQC_LINK_TOPO_HANDLE_BRD_TYPE_MEZZ	0
#define ICE_AQC_LINK_TOPO_HANDLE_NODE_S		0
/* In case of a Mezzanine type */
#define ICE_AQC_LINK_TOPO_HANDLE_MEZZ_NODE_M	\
				(0x3F << ICE_AQC_LINK_TOPO_HANDLE_NODE_S)
#define ICE_AQC_LINK_TOPO_HANDLE_MEZZ_S	6
#define ICE_AQC_LINK_TOPO_HANDLE_MEZZ_M	(0x7 << ICE_AQC_LINK_TOPO_HANDLE_MEZZ_S)
/* In case of a LOM type */
#define ICE_AQC_LINK_TOPO_HANDLE_LOM_NODE_M	\
				(0x1FF << ICE_AQC_LINK_TOPO_HANDLE_NODE_S)
};

/* Get Link Topology Handle (direct, 0x06E0) */
struct ice_aqc_get_link_topo {
	struct ice_aqc_link_topo_addr addr;
	u8 node_part_num;
	u8 rsvd[9];
};

/* Set Port Identification LED (direct, 0x06E9) */
struct ice_aqc_set_port_id_led {
	u8 lport_num;
	u8 lport_num_valid;
	u8 ident_mode;
#define ICE_AQC_PORT_IDENT_LED_BLINK	BIT(0)
#define ICE_AQC_PORT_IDENT_LED_ORIG	0
	u8 rsvd[13];
};

/* Read/Write SFF EEPROM command (indirect 0x06EE) */
struct ice_aqc_sff_eeprom {
	u8 lport_num;
	u8 lport_num_valid;
#define ICE_AQC_SFF_PORT_NUM_VALID	BIT(0)
	__le16 i2c_bus_addr;
#define ICE_AQC_SFF_I2CBUS_7BIT_M	0x7F
#define ICE_AQC_SFF_I2CBUS_10BIT_M	0x3FF
#define ICE_AQC_SFF_I2CBUS_TYPE_M	BIT(10)
#define ICE_AQC_SFF_I2CBUS_TYPE_7BIT	0
#define ICE_AQC_SFF_I2CBUS_TYPE_10BIT	ICE_AQC_SFF_I2CBUS_TYPE_M
#define ICE_AQC_SFF_SET_EEPROM_PAGE_S	11
#define ICE_AQC_SFF_SET_EEPROM_PAGE_M	(0x3 << ICE_AQC_SFF_SET_EEPROM_PAGE_S)
#define ICE_AQC_SFF_NO_PAGE_CHANGE	0
#define ICE_AQC_SFF_SET_23_ON_MISMATCH	1
#define ICE_AQC_SFF_SET_22_ON_MISMATCH	2
#define ICE_AQC_SFF_IS_WRITE		BIT(15)
	__le16 i2c_mem_addr;
	__le16 eeprom_page;
#define  ICE_AQC_SFF_EEPROM_BANK_S 0
#define  ICE_AQC_SFF_EEPROM_BANK_M (0xFF << ICE_AQC_SFF_EEPROM_BANK_S)
#define  ICE_AQC_SFF_EEPROM_PAGE_S 8
#define  ICE_AQC_SFF_EEPROM_PAGE_M (0xFF << ICE_AQC_SFF_EEPROM_PAGE_S)
	__le32 addr_high;
	__le32 addr_low;
};

/* NVM Read command (indirect 0x0701)
 * NVM Erase commands (direct 0x0702)
 * NVM Update commands (indirect 0x0703)
 */
struct ice_aqc_nvm {
#define ICE_AQC_NVM_MAX_OFFSET		0xFFFFFF
	__le16 offset_low;
	u8 offset_high;
	u8 cmd_flags;
#define ICE_AQC_NVM_LAST_CMD		BIT(0)
#define ICE_AQC_NVM_PCIR_REQ		BIT(0)	/* Used by NVM Update reply */
#define ICE_AQC_NVM_PRESERVATION_S	1
#define ICE_AQC_NVM_PRESERVATION_M	(3 << ICE_AQC_NVM_PRESERVATION_S)
#define ICE_AQC_NVM_NO_PRESERVATION	(0 << ICE_AQC_NVM_PRESERVATION_S)
#define ICE_AQC_NVM_PRESERVE_ALL	BIT(1)
#define ICE_AQC_NVM_FACTORY_DEFAULT	(2 << ICE_AQC_NVM_PRESERVATION_S)
#define ICE_AQC_NVM_PRESERVE_SELECTED	(3 << ICE_AQC_NVM_PRESERVATION_S)
#define ICE_AQC_NVM_ACTIV_SEL_NVM	BIT(3) /* Write Activate/SR Dump only */
#define ICE_AQC_NVM_ACTIV_SEL_OROM	BIT(4)
#define ICE_AQC_NVM_ACTIV_SEL_NETLIST	BIT(5)
#define ICE_AQC_NVM_SPECIAL_UPDATE	BIT(6)
#define ICE_AQC_NVM_REVERT_LAST_ACTIV	BIT(6) /* Write Activate only */
#define ICE_AQC_NVM_ACTIV_SEL_MASK	ICE_M(0x7, 3)
#define ICE_AQC_NVM_FLASH_ONLY		BIT(7)
	__le16 module_typeid;
	__le16 length;
#define ICE_AQC_NVM_ERASE_LEN	0xFFFF
	__le32 addr_high;
	__le32 addr_low;
};

#define ICE_AQC_NVM_START_POINT			0

/* NVM Checksum Command (direct, 0x0706) */
struct ice_aqc_nvm_checksum {
	u8 flags;
#define ICE_AQC_NVM_CHECKSUM_VERIFY	BIT(0)
#define ICE_AQC_NVM_CHECKSUM_RECALC	BIT(1)
	u8 rsvd;
	__le16 checksum; /* Used only by response */
#define ICE_AQC_NVM_CHECKSUM_CORRECT	0xBABA
	u8 rsvd2[12];
};

/* The result of netlist NVM read comes in a TLV format. The actual data
 * (netlist header) starts from word offset 1 (byte 2). The FW strips
 * out the type field from the TLV header so all the netlist fields
 * should adjust their offset value by 1 word (2 bytes) in order to map
 * their correct location.
 */
#define ICE_AQC_NVM_LINK_TOPO_NETLIST_MOD_ID		0x11B
#define ICE_AQC_NVM_LINK_TOPO_NETLIST_LEN_OFFSET	1
#define ICE_AQC_NVM_LINK_TOPO_NETLIST_LEN		2 /* In bytes */
#define ICE_AQC_NVM_NETLIST_NODE_COUNT_OFFSET		2
#define ICE_AQC_NVM_NETLIST_NODE_COUNT_LEN		2 /* In bytes */
#define ICE_AQC_NVM_NETLIST_NODE_COUNT_M		ICE_M(0x3FF, 0)
#define ICE_AQC_NVM_NETLIST_ID_BLK_START_OFFSET		5
#define ICE_AQC_NVM_NETLIST_ID_BLK_LEN			0x30 /* In words */

/* netlist ID block field offsets (word offsets) */
#define ICE_AQC_NVM_NETLIST_ID_BLK_MAJOR_VER_LOW	2
#define ICE_AQC_NVM_NETLIST_ID_BLK_MAJOR_VER_HIGH	3
#define ICE_AQC_NVM_NETLIST_ID_BLK_MINOR_VER_LOW	4
#define ICE_AQC_NVM_NETLIST_ID_BLK_MINOR_VER_HIGH	5
#define ICE_AQC_NVM_NETLIST_ID_BLK_TYPE_LOW		6
#define ICE_AQC_NVM_NETLIST_ID_BLK_TYPE_HIGH		7
#define ICE_AQC_NVM_NETLIST_ID_BLK_REV_LOW		8
#define ICE_AQC_NVM_NETLIST_ID_BLK_REV_HIGH		9
#define ICE_AQC_NVM_NETLIST_ID_BLK_SHA_HASH		0xA
#define ICE_AQC_NVM_NETLIST_ID_BLK_CUST_VER		0x2F

/* Used for NVM Set Package Data command - 0x070A */
struct ice_aqc_nvm_pkg_data {
	u8 reserved[3];
	u8 cmd_flags;
#define ICE_AQC_NVM_PKG_DELETE		BIT(0) /* used for command call */
#define ICE_AQC_NVM_PKG_SKIPPED		BIT(0) /* used for command response */

	u32 reserved1;
	__le32 addr_high;
	__le32 addr_low;
};

/* Used for Pass Component Table command - 0x070B */
struct ice_aqc_nvm_pass_comp_tbl {
	u8 component_response; /* Response only */
#define ICE_AQ_NVM_PASS_COMP_CAN_BE_UPDATED		0x0
#define ICE_AQ_NVM_PASS_COMP_CAN_MAY_BE_UPDATEABLE	0x1
#define ICE_AQ_NVM_PASS_COMP_CAN_NOT_BE_UPDATED		0x2
	u8 component_response_code; /* Response only */
#define ICE_AQ_NVM_PASS_COMP_CAN_BE_UPDATED_CODE	0x0
#define ICE_AQ_NVM_PASS_COMP_STAMP_IDENTICAL_CODE	0x1
#define ICE_AQ_NVM_PASS_COMP_STAMP_LOWER		0x2
#define ICE_AQ_NVM_PASS_COMP_INVALID_STAMP_CODE		0x3
#define ICE_AQ_NVM_PASS_COMP_CONFLICT_CODE		0x4
#define ICE_AQ_NVM_PASS_COMP_PRE_REQ_NOT_MET_CODE	0x5
#define ICE_AQ_NVM_PASS_COMP_NOT_SUPPORTED_CODE		0x6
#define ICE_AQ_NVM_PASS_COMP_CANNOT_DOWNGRADE_CODE	0x7
#define ICE_AQ_NVM_PASS_COMP_INCOMPLETE_IMAGE_CODE	0x8
#define ICE_AQ_NVM_PASS_COMP_VER_STR_IDENTICAL_CODE	0xA
#define ICE_AQ_NVM_PASS_COMP_VER_STR_LOWER_CODE		0xB
	u8 reserved;
	u8 transfer_flag;
#define ICE_AQ_NVM_PASS_COMP_TBL_START			0x1
#define ICE_AQ_NVM_PASS_COMP_TBL_MIDDLE			0x2
#define ICE_AQ_NVM_PASS_COMP_TBL_END			0x4
#define ICE_AQ_NVM_PASS_COMP_TBL_START_AND_END		0x5
	__le32 reserved1;
	__le32 addr_high;
	__le32 addr_low;
};

struct ice_aqc_nvm_comp_tbl {
	__le16 comp_class;
#define NVM_COMP_CLASS_ALL_FW	0x000A

	__le16 comp_id;
#define NVM_COMP_ID_OROM	0x5
#define NVM_COMP_ID_NVM		0x6
#define NVM_COMP_ID_NETLIST	0x8

	u8 comp_class_idx;
#define FWU_COMP_CLASS_IDX_NOT_USE 0x0

	__le32 comp_cmp_stamp;
	u8 cvs_type;
#define NVM_CVS_TYPE_ASCII	0x1

	u8 cvs_len;
	u8 cvs[]; /* Component Version String */
} __packed;

/*
 * Send to PF command (indirect 0x0801) ID is only used by PF
 *
 * Send to VF command (indirect 0x0802) ID is only used by PF
 *
 */
struct ice_aqc_pf_vf_msg {
	__le32 id;
	u32 reserved;
	__le32 addr_high;
	__le32 addr_low;
};

/* Get LLDP MIB (indirect 0x0A00)
 * Note: This is also used by the LLDP MIB Change Event (0x0A01)
 * as the format is the same.
 */
struct ice_aqc_lldp_get_mib {
	u8 type;
#define ICE_AQ_LLDP_MIB_TYPE_S			0
#define ICE_AQ_LLDP_MIB_TYPE_M			(0x3 << ICE_AQ_LLDP_MIB_TYPE_S)
#define ICE_AQ_LLDP_MIB_LOCAL			0
#define ICE_AQ_LLDP_MIB_REMOTE			1
#define ICE_AQ_LLDP_MIB_LOCAL_AND_REMOTE	2
#define ICE_AQ_LLDP_BRID_TYPE_S			2
#define ICE_AQ_LLDP_BRID_TYPE_M			(0x3 << ICE_AQ_LLDP_BRID_TYPE_S)
#define ICE_AQ_LLDP_BRID_TYPE_NEAREST_BRID	0
#define ICE_AQ_LLDP_BRID_TYPE_NON_TPMR		1
/* Tx pause flags in the 0xA01 event use ICE_AQ_LLDP_TX_* */
#define ICE_AQ_LLDP_TX_S			0x4
#define ICE_AQ_LLDP_TX_M			(0x03 << ICE_AQ_LLDP_TX_S)
#define ICE_AQ_LLDP_TX_ACTIVE			0
#define ICE_AQ_LLDP_TX_SUSPENDED		1
#define ICE_AQ_LLDP_TX_FLUSHED			3
/* The following bytes are reserved for the Get LLDP MIB command (0x0A00)
 * and in the LLDP MIB Change Event (0x0A01). They are valid for the
 * Get LLDP MIB (0x0A00) response only.
 */
	u8 reserved1;
	__le16 local_len;
	__le16 remote_len;
	u8 reserved2[2];
	__le32 addr_high;
	__le32 addr_low;
};

/* Configure LLDP MIB Change Event (direct 0x0A01) */
/* For MIB Change Event use ice_aqc_lldp_get_mib structure above */
struct ice_aqc_lldp_set_mib_change {
	u8 command;
#define ICE_AQ_LLDP_MIB_UPDATE_ENABLE		0x0
#define ICE_AQ_LLDP_MIB_UPDATE_DIS		0x1
	u8 reserved[15];
};

/* Stop LLDP (direct 0x0A05) */
struct ice_aqc_lldp_stop {
	u8 command;
#define ICE_AQ_LLDP_AGENT_STATE_MASK	BIT(0)
#define ICE_AQ_LLDP_AGENT_STOP		0x0
#define ICE_AQ_LLDP_AGENT_SHUTDOWN	ICE_AQ_LLDP_AGENT_STATE_MASK
#define ICE_AQ_LLDP_AGENT_PERSIST_DIS	BIT(1)
	u8 reserved[15];
};

/* Start LLDP (direct 0x0A06) */
struct ice_aqc_lldp_start {
	u8 command;
#define ICE_AQ_LLDP_AGENT_START		BIT(0)
#define ICE_AQ_LLDP_AGENT_PERSIST_ENA	BIT(1)
	u8 reserved[15];
};

/* Get CEE DCBX Oper Config (0x0A07)
 * The command uses the generic descriptor struct and
 * returns the struct below as an indirect response.
 */
struct ice_aqc_get_cee_dcb_cfg_resp {
	u8 oper_num_tc;
	u8 oper_prio_tc[4];
	u8 oper_tc_bw[8];
	u8 oper_pfc_en;
	__le16 oper_app_prio;
#define ICE_AQC_CEE_APP_FCOE_S		0
#define ICE_AQC_CEE_APP_FCOE_M		(0x7 << ICE_AQC_CEE_APP_FCOE_S)
#define ICE_AQC_CEE_APP_ISCSI_S		3
#define ICE_AQC_CEE_APP_ISCSI_M		(0x7 << ICE_AQC_CEE_APP_ISCSI_S)
#define ICE_AQC_CEE_APP_FIP_S		8
#define ICE_AQC_CEE_APP_FIP_M		(0x7 << ICE_AQC_CEE_APP_FIP_S)
	__le32 tlv_status;
#define ICE_AQC_CEE_PG_STATUS_S		0
#define ICE_AQC_CEE_PG_STATUS_M		(0x7 << ICE_AQC_CEE_PG_STATUS_S)
#define ICE_AQC_CEE_PFC_STATUS_S	3
#define ICE_AQC_CEE_PFC_STATUS_M	(0x7 << ICE_AQC_CEE_PFC_STATUS_S)
#define ICE_AQC_CEE_FCOE_STATUS_S	8
#define ICE_AQC_CEE_FCOE_STATUS_M	(0x7 << ICE_AQC_CEE_FCOE_STATUS_S)
#define ICE_AQC_CEE_ISCSI_STATUS_S	11
#define ICE_AQC_CEE_ISCSI_STATUS_M	(0x7 << ICE_AQC_CEE_ISCSI_STATUS_S)
#define ICE_AQC_CEE_FIP_STATUS_S	16
#define ICE_AQC_CEE_FIP_STATUS_M	(0x7 << ICE_AQC_CEE_FIP_STATUS_S)
	u8 reserved[12];
};

/* Set Local LLDP MIB (indirect 0x0A08)
 * Used to replace the local MIB of a given LLDP agent. e.g. DCBX
 */
struct ice_aqc_lldp_set_local_mib {
	u8 type;
#define SET_LOCAL_MIB_TYPE_DCBX_M		BIT(0)
#define SET_LOCAL_MIB_TYPE_LOCAL_MIB		0
#define SET_LOCAL_MIB_TYPE_CEE_M		BIT(1)
#define SET_LOCAL_MIB_TYPE_CEE_WILLING		0
#define SET_LOCAL_MIB_TYPE_CEE_NON_WILLING	SET_LOCAL_MIB_TYPE_CEE_M
	u8 reserved0;
	__le16 length;
	u8 reserved1[4];
	__le32 addr_high;
	__le32 addr_low;
};

/* Stop/Start LLDP Agent (direct 0x0A09)
 * Used for stopping/starting specific LLDP agent. e.g. DCBX.
 * The same structure is used for the response, with the command field
 * being used as the status field.
 */
struct ice_aqc_lldp_stop_start_specific_agent {
	u8 command;
#define ICE_AQC_START_STOP_AGENT_M		BIT(0)
#define ICE_AQC_START_STOP_AGENT_STOP_DCBX	0
#define ICE_AQC_START_STOP_AGENT_START_DCBX	ICE_AQC_START_STOP_AGENT_M
	u8 reserved[15];
};

/* Get/Set RSS key (indirect 0x0B04/0x0B02) */
struct ice_aqc_get_set_rss_key {
#define ICE_AQC_GSET_RSS_KEY_VSI_VALID	BIT(15)
#define ICE_AQC_GSET_RSS_KEY_VSI_ID_S	0
#define ICE_AQC_GSET_RSS_KEY_VSI_ID_M	(0x3FF << ICE_AQC_GSET_RSS_KEY_VSI_ID_S)
	__le16 vsi_id;
	u8 reserved[6];
	__le32 addr_high;
	__le32 addr_low;
};

#define ICE_AQC_GET_SET_RSS_KEY_DATA_RSS_KEY_SIZE	0x28
#define ICE_AQC_GET_SET_RSS_KEY_DATA_HASH_KEY_SIZE	0xC
#define ICE_GET_SET_RSS_KEY_EXTEND_KEY_SIZE \
				(ICE_AQC_GET_SET_RSS_KEY_DATA_RSS_KEY_SIZE + \
				 ICE_AQC_GET_SET_RSS_KEY_DATA_HASH_KEY_SIZE)

struct ice_aqc_get_set_rss_keys {
	u8 standard_rss_key[ICE_AQC_GET_SET_RSS_KEY_DATA_RSS_KEY_SIZE];
	u8 extended_hash_key[ICE_AQC_GET_SET_RSS_KEY_DATA_HASH_KEY_SIZE];
};

/* Get/Set RSS LUT (indirect 0x0B05/0x0B03) */
struct ice_aqc_get_set_rss_lut {
#define ICE_AQC_GSET_RSS_LUT_VSI_VALID	BIT(15)
#define ICE_AQC_GSET_RSS_LUT_VSI_ID_S	0
#define ICE_AQC_GSET_RSS_LUT_VSI_ID_M	(0x3FF << ICE_AQC_GSET_RSS_LUT_VSI_ID_S)
	__le16 vsi_id;
#define ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_S	0
#define ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_M	\
				(0x3 << ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_S)

#define ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_VSI	 0
#define ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_PF	 1
#define ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_GLOBAL	 2

#define ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_S	 2
#define ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_M	 \
				(0x3 << ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_S)

#define ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_128	 128
#define ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_128_FLAG 0
#define ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_512	 512
#define ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_512_FLAG 1
#define ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_2K	 2048
#define ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_2K_FLAG	 2

#define ICE_AQC_GSET_RSS_LUT_GLOBAL_IDX_S	 4
#define ICE_AQC_GSET_RSS_LUT_GLOBAL_IDX_M	 \
				(0xF << ICE_AQC_GSET_RSS_LUT_GLOBAL_IDX_S)

	__le16 flags;
	__le32 reserved;
	__le32 addr_high;
	__le32 addr_low;
};

/* Add Tx LAN Queues (indirect 0x0C30) */
struct ice_aqc_add_txqs {
	u8 num_qgrps;
	u8 reserved[3];
	__le32 reserved1;
	__le32 addr_high;
	__le32 addr_low;
};

/* This is the descriptor of each queue entry for the Add Tx LAN Queues
 * command (0x0C30). Only used within struct ice_aqc_add_tx_qgrp.
 */
struct ice_aqc_add_txqs_perq {
	__le16 txq_id;
	u8 rsvd[2];
	__le32 q_teid;
	u8 txq_ctx[22];
	u8 rsvd2[2];
	struct ice_aqc_txsched_elem info;
};

/* The format of the command buffer for Add Tx LAN Queues (0x0C30)
 * is an array of the following structs. Please note that the length of
 * each struct ice_aqc_add_tx_qgrp is variable due
 * to the variable number of queues in each group!
 */
struct ice_aqc_add_tx_qgrp {
	__le32 parent_teid;
	u8 num_txqs;
	u8 rsvd[3];
	struct ice_aqc_add_txqs_perq txqs[];
};

/* Disable Tx LAN Queues (indirect 0x0C31) */
struct ice_aqc_dis_txqs {
	u8 cmd_type;
#define ICE_AQC_Q_DIS_CMD_S		0
#define ICE_AQC_Q_DIS_CMD_M		(0x3 << ICE_AQC_Q_DIS_CMD_S)
#define ICE_AQC_Q_DIS_CMD_NO_FUNC_RESET	(0 << ICE_AQC_Q_DIS_CMD_S)
#define ICE_AQC_Q_DIS_CMD_VM_RESET	BIT(ICE_AQC_Q_DIS_CMD_S)
#define ICE_AQC_Q_DIS_CMD_VF_RESET	(2 << ICE_AQC_Q_DIS_CMD_S)
#define ICE_AQC_Q_DIS_CMD_PF_RESET	(3 << ICE_AQC_Q_DIS_CMD_S)
#define ICE_AQC_Q_DIS_CMD_SUBSEQ_CALL	BIT(2)
#define ICE_AQC_Q_DIS_CMD_FLUSH_PIPE	BIT(3)
	u8 num_entries;
	__le16 vmvf_and_timeout;
#define ICE_AQC_Q_DIS_VMVF_NUM_S	0
#define ICE_AQC_Q_DIS_VMVF_NUM_M	(0x3FF << ICE_AQC_Q_DIS_VMVF_NUM_S)
#define ICE_AQC_Q_DIS_TIMEOUT_S		10
#define ICE_AQC_Q_DIS_TIMEOUT_M		(0x3F << ICE_AQC_Q_DIS_TIMEOUT_S)
	__le32 blocked_cgds;
	__le32 addr_high;
	__le32 addr_low;
};

/* The buffer for Disable Tx LAN Queues (indirect 0x0C31)
 * contains the following structures, arrayed one after the
 * other.
 * Note: Since the q_id is 16 bits wide, if the
 * number of queues is even, then 2 bytes of alignment MUST be
 * added before the start of the next group, to allow correct
 * alignment of the parent_teid field.
 */
struct ice_aqc_dis_txq_item {
	__le32 parent_teid;
	u8 num_qs;
	u8 rsvd;
	/* The length of the q_id array varies according to num_qs */
#define ICE_AQC_Q_DIS_BUF_ELEM_TYPE_S		15
#define ICE_AQC_Q_DIS_BUF_ELEM_TYPE_LAN_Q	\
			(0 << ICE_AQC_Q_DIS_BUF_ELEM_TYPE_S)
#define ICE_AQC_Q_DIS_BUF_ELEM_TYPE_RDMA_QSET	\
			(1 << ICE_AQC_Q_DIS_BUF_ELEM_TYPE_S)
	__le16 q_id[];
} __packed;

/* Configure Firmware Logging Command (indirect 0xFF09)
 * Logging Information Read Response (indirect 0xFF10)
 * Note: The 0xFF10 command has no input parameters.
 */
struct ice_aqc_fw_logging {
	u8 log_ctrl;
#define ICE_AQC_FW_LOG_AQ_EN		BIT(0)
#define ICE_AQC_FW_LOG_UART_EN		BIT(1)
	u8 rsvd0;
	u8 log_ctrl_valid; /* Not used by 0xFF10 Response */
#define ICE_AQC_FW_LOG_AQ_VALID		BIT(0)
#define ICE_AQC_FW_LOG_UART_VALID	BIT(1)
	u8 rsvd1[5];
	__le32 addr_high;
	__le32 addr_low;
};

enum ice_aqc_fw_logging_mod {
	ICE_AQC_FW_LOG_ID_GENERAL = 0,
	ICE_AQC_FW_LOG_ID_CTRL,
	ICE_AQC_FW_LOG_ID_LINK,
	ICE_AQC_FW_LOG_ID_LINK_TOPO,
	ICE_AQC_FW_LOG_ID_DNL,
	ICE_AQC_FW_LOG_ID_I2C,
	ICE_AQC_FW_LOG_ID_SDP,
	ICE_AQC_FW_LOG_ID_MDIO,
	ICE_AQC_FW_LOG_ID_ADMINQ,
	ICE_AQC_FW_LOG_ID_HDMA,
	ICE_AQC_FW_LOG_ID_LLDP,
	ICE_AQC_FW_LOG_ID_DCBX,
	ICE_AQC_FW_LOG_ID_DCB,
	ICE_AQC_FW_LOG_ID_NETPROXY,
	ICE_AQC_FW_LOG_ID_NVM,
	ICE_AQC_FW_LOG_ID_AUTH,
	ICE_AQC_FW_LOG_ID_VPD,
	ICE_AQC_FW_LOG_ID_IOSF,
	ICE_AQC_FW_LOG_ID_PARSER,
	ICE_AQC_FW_LOG_ID_SW,
	ICE_AQC_FW_LOG_ID_SCHEDULER,
	ICE_AQC_FW_LOG_ID_TXQ,
	ICE_AQC_FW_LOG_ID_RSVD,
	ICE_AQC_FW_LOG_ID_POST,
	ICE_AQC_FW_LOG_ID_WATCHDOG,
	ICE_AQC_FW_LOG_ID_TASK_DISPATCH,
	ICE_AQC_FW_LOG_ID_MNG,
	ICE_AQC_FW_LOG_ID_MAX,
};

/* Defines for both above FW logging command/response buffers */
#define ICE_AQC_FW_LOG_ID_S		0
#define ICE_AQC_FW_LOG_ID_M		(0xFFF << ICE_AQC_FW_LOG_ID_S)

#define ICE_AQC_FW_LOG_CONF_SUCCESS	0	/* Used by response */
#define ICE_AQC_FW_LOG_CONF_BAD_INDX	BIT(12)	/* Used by response */

#define ICE_AQC_FW_LOG_EN_S		12
#define ICE_AQC_FW_LOG_EN_M		(0xF << ICE_AQC_FW_LOG_EN_S)
#define ICE_AQC_FW_LOG_INFO_EN		BIT(12)	/* Used by command */
#define ICE_AQC_FW_LOG_INIT_EN		BIT(13)	/* Used by command */
#define ICE_AQC_FW_LOG_FLOW_EN		BIT(14)	/* Used by command */
#define ICE_AQC_FW_LOG_ERR_EN		BIT(15)	/* Used by command */

/* Get/Clear FW Log (indirect 0xFF11) */
struct ice_aqc_get_clear_fw_log {
	u8 flags;
#define ICE_AQC_FW_LOG_CLEAR		BIT(0)
#define ICE_AQC_FW_LOG_MORE_DATA_AVAIL	BIT(1)
	u8 rsvd1[7];
	__le32 addr_high;
	__le32 addr_low;
};

/* Download Package (indirect 0x0C40) */
/* Also used for Update Package (indirect 0x0C42) */
struct ice_aqc_download_pkg {
	u8 flags;
#define ICE_AQC_DOWNLOAD_PKG_LAST_BUF	0x01
	u8 reserved[3];
	__le32 reserved1;
	__le32 addr_high;
	__le32 addr_low;
};

struct ice_aqc_download_pkg_resp {
	__le32 error_offset;
	__le32 error_info;
	__le32 addr_high;
	__le32 addr_low;
};

/* Get Package Info List (indirect 0x0C43) */
struct ice_aqc_get_pkg_info_list {
	__le32 reserved1;
	__le32 reserved2;
	__le32 addr_high;
	__le32 addr_low;
};

/* Version format for packages */
struct ice_pkg_ver {
	u8 major;
	u8 minor;
	u8 update;
	u8 draft;
};

#define ICE_PKG_NAME_SIZE	32
#define ICE_SEG_NAME_SIZE	28

struct ice_aqc_get_pkg_info {
	struct ice_pkg_ver ver;
	char name[ICE_SEG_NAME_SIZE];
	__le32 track_id;
	u8 is_in_nvm;
	u8 is_active;
	u8 is_active_at_boot;
	u8 is_modified;
};

/* Get Package Info List response buffer format (0x0C43) */
struct ice_aqc_get_pkg_info_resp {
	__le32 count;
	struct ice_aqc_get_pkg_info pkg_info[];
};

/* Lan Queue Overflow Event (direct, 0x1001) */
struct ice_aqc_event_lan_overflow {
	__le32 prtdcb_ruptq;
	__le32 qtx_ctl;
	u8 reserved[8];
};

/**
 * struct ice_aq_desc - Admin Queue (AQ) descriptor
 * @flags: ICE_AQ_FLAG_* flags
 * @opcode: AQ command opcode
 * @datalen: length in bytes of indirect/external data buffer
 * @retval: return value from firmware
 * @cookie_high: opaque data high-half
 * @cookie_low: opaque data low-half
 * @params: command-specific parameters
 *
 * Descriptor format for commands the driver posts on the Admin Transmit Queue
 * (ATQ). The firmware writes back onto the command descriptor and returns
 * the result of the command. Asynchronous events that are not an immediate
 * result of the command are written to the Admin Receive Queue (ARQ) using
 * the same descriptor format. Descriptors are in little-endian notation with
 * 32-bit words.
 */
struct ice_aq_desc {
	__le16 flags;
	__le16 opcode;
	__le16 datalen;
	__le16 retval;
	__le32 cookie_high;
	__le32 cookie_low;
	union {
		u8 raw[16];
		struct ice_aqc_generic generic;
		struct ice_aqc_get_ver get_ver;
		struct ice_aqc_driver_ver driver_ver;
		struct ice_aqc_q_shutdown q_shutdown;
		struct ice_aqc_req_res res_owner;
		struct ice_aqc_manage_mac_read mac_read;
		struct ice_aqc_manage_mac_write mac_write;
		struct ice_aqc_clear_pxe clear_pxe;
		struct ice_aqc_list_caps get_cap;
		struct ice_aqc_get_phy_caps get_phy;
		struct ice_aqc_set_phy_cfg set_phy;
		struct ice_aqc_restart_an restart_an;
		struct ice_aqc_sff_eeprom read_write_sff_param;
		struct ice_aqc_set_port_id_led set_port_id_led;
		struct ice_aqc_get_sw_cfg get_sw_conf;
		struct ice_aqc_sw_rules sw_rules;
		struct ice_aqc_get_topo get_topo;
		struct ice_aqc_sched_elem_cmd sched_elem_cmd;
		struct ice_aqc_query_txsched_res query_sched_res;
		struct ice_aqc_query_port_ets port_ets;
		struct ice_aqc_rl_profile rl_profile;
		struct ice_aqc_nvm nvm;
		struct ice_aqc_nvm_checksum nvm_checksum;
		struct ice_aqc_nvm_pkg_data pkg_data;
		struct ice_aqc_nvm_pass_comp_tbl pass_comp_tbl;
		struct ice_aqc_pf_vf_msg virt;
		struct ice_aqc_lldp_get_mib lldp_get_mib;
		struct ice_aqc_lldp_set_mib_change lldp_set_event;
		struct ice_aqc_lldp_stop lldp_stop;
		struct ice_aqc_lldp_start lldp_start;
		struct ice_aqc_lldp_set_local_mib lldp_set_mib;
		struct ice_aqc_lldp_stop_start_specific_agent lldp_agent_ctrl;
		struct ice_aqc_get_set_rss_lut get_set_rss_lut;
		struct ice_aqc_get_set_rss_key get_set_rss_key;
		struct ice_aqc_add_txqs add_txqs;
		struct ice_aqc_dis_txqs dis_txqs;
		struct ice_aqc_add_get_update_free_vsi vsi_cmd;
		struct ice_aqc_add_update_free_vsi_resp add_update_free_vsi_res;
		struct ice_aqc_fw_logging fw_logging;
		struct ice_aqc_get_clear_fw_log get_clear_fw_log;
		struct ice_aqc_download_pkg download_pkg;
		struct ice_aqc_set_mac_lb set_mac_lb;
		struct ice_aqc_alloc_free_res_cmd sw_res_ctrl;
		struct ice_aqc_set_mac_cfg set_mac_cfg;
		struct ice_aqc_set_event_mask set_event_mask;
		struct ice_aqc_get_link_status get_link_status;
		struct ice_aqc_event_lan_overflow lan_overflow;
		struct ice_aqc_get_link_topo get_link_topo;
	} params;
};

/* FW defined boundary for a large buffer, 4k >= Large buffer > 512 bytes */
#define ICE_AQ_LG_BUF	512

#define ICE_AQ_FLAG_ERR_S	2
#define ICE_AQ_FLAG_LB_S	9
#define ICE_AQ_FLAG_RD_S	10
#define ICE_AQ_FLAG_BUF_S	12
#define ICE_AQ_FLAG_SI_S	13

#define ICE_AQ_FLAG_ERR		BIT(ICE_AQ_FLAG_ERR_S) /* 0x4    */
#define ICE_AQ_FLAG_LB		BIT(ICE_AQ_FLAG_LB_S)  /* 0x200  */
#define ICE_AQ_FLAG_RD		BIT(ICE_AQ_FLAG_RD_S)  /* 0x400  */
#define ICE_AQ_FLAG_BUF		BIT(ICE_AQ_FLAG_BUF_S) /* 0x1000 */
#define ICE_AQ_FLAG_SI		BIT(ICE_AQ_FLAG_SI_S)  /* 0x2000 */

/* error codes */
enum ice_aq_err {
	ICE_AQ_RC_OK		= 0,  /* Success */
	ICE_AQ_RC_EPERM		= 1,  /* Operation not permitted */
	ICE_AQ_RC_ENOENT	= 2,  /* No such element */
	ICE_AQ_RC_ENOMEM	= 9,  /* Out of memory */
	ICE_AQ_RC_EBUSY		= 12, /* Device or resource busy */
	ICE_AQ_RC_EEXIST	= 13, /* Object already exists */
	ICE_AQ_RC_EINVAL	= 14, /* Invalid argument */
	ICE_AQ_RC_ENOSPC	= 16, /* No space left or allocation failure */
	ICE_AQ_RC_ENOSYS	= 17, /* Function not implemented */
	ICE_AQ_RC_EMODE		= 21, /* Op not allowed in current dev mode */
	ICE_AQ_RC_ENOSEC	= 24, /* Missing security manifest */
	ICE_AQ_RC_EBADSIG	= 25, /* Bad RSA signature */
	ICE_AQ_RC_ESVN		= 26, /* SVN number prohibits this package */
	ICE_AQ_RC_EBADMAN	= 27, /* Manifest hash mismatch */
	ICE_AQ_RC_EBADBUF	= 28, /* Buffer hash mismatches manifest */
};

/* Admin Queue command opcodes */
enum ice_adminq_opc {
	/* AQ commands */
	ice_aqc_opc_get_ver				= 0x0001,
	ice_aqc_opc_driver_ver				= 0x0002,
	ice_aqc_opc_q_shutdown				= 0x0003,

	/* resource ownership */
	ice_aqc_opc_req_res				= 0x0008,
	ice_aqc_opc_release_res				= 0x0009,

	/* device/function capabilities */
	ice_aqc_opc_list_func_caps			= 0x000A,
	ice_aqc_opc_list_dev_caps			= 0x000B,

	/* manage MAC address */
	ice_aqc_opc_manage_mac_read			= 0x0107,
	ice_aqc_opc_manage_mac_write			= 0x0108,

	/* PXE */
	ice_aqc_opc_clear_pxe_mode			= 0x0110,

	/* internal switch commands */
	ice_aqc_opc_get_sw_cfg				= 0x0200,

	/* Alloc/Free/Get Resources */
	ice_aqc_opc_alloc_res				= 0x0208,
	ice_aqc_opc_free_res				= 0x0209,

	/* VSI commands */
	ice_aqc_opc_add_vsi				= 0x0210,
	ice_aqc_opc_update_vsi				= 0x0211,
	ice_aqc_opc_free_vsi				= 0x0213,

	/* switch rules population commands */
	ice_aqc_opc_add_sw_rules			= 0x02A0,
	ice_aqc_opc_update_sw_rules			= 0x02A1,
	ice_aqc_opc_remove_sw_rules			= 0x02A2,

	ice_aqc_opc_clear_pf_cfg			= 0x02A4,

	/* transmit scheduler commands */
	ice_aqc_opc_get_dflt_topo			= 0x0400,
	ice_aqc_opc_add_sched_elems			= 0x0401,
	ice_aqc_opc_cfg_sched_elems			= 0x0403,
	ice_aqc_opc_get_sched_elems			= 0x0404,
	ice_aqc_opc_suspend_sched_elems			= 0x0409,
	ice_aqc_opc_resume_sched_elems			= 0x040A,
	ice_aqc_opc_query_port_ets			= 0x040E,
	ice_aqc_opc_delete_sched_elems			= 0x040F,
	ice_aqc_opc_add_rl_profiles			= 0x0410,
	ice_aqc_opc_query_sched_res			= 0x0412,
	ice_aqc_opc_remove_rl_profiles			= 0x0415,

	/* PHY commands */
	ice_aqc_opc_get_phy_caps			= 0x0600,
	ice_aqc_opc_set_phy_cfg				= 0x0601,
	ice_aqc_opc_set_mac_cfg				= 0x0603,
	ice_aqc_opc_restart_an				= 0x0605,
	ice_aqc_opc_get_link_status			= 0x0607,
	ice_aqc_opc_set_event_mask			= 0x0613,
	ice_aqc_opc_set_mac_lb				= 0x0620,
	ice_aqc_opc_get_link_topo			= 0x06E0,
	ice_aqc_opc_set_port_id_led			= 0x06E9,
	ice_aqc_opc_sff_eeprom				= 0x06EE,

	/* NVM commands */
	ice_aqc_opc_nvm_read				= 0x0701,
	ice_aqc_opc_nvm_erase				= 0x0702,
	ice_aqc_opc_nvm_write				= 0x0703,
	ice_aqc_opc_nvm_checksum			= 0x0706,
	ice_aqc_opc_nvm_write_activate			= 0x0707,
	ice_aqc_opc_nvm_update_empr			= 0x0709,
	ice_aqc_opc_nvm_pkg_data			= 0x070A,
	ice_aqc_opc_nvm_pass_component_tbl		= 0x070B,

	/* PF/VF mailbox commands */
	ice_mbx_opc_send_msg_to_pf			= 0x0801,
	ice_mbx_opc_send_msg_to_vf			= 0x0802,
	/* LLDP commands */
	ice_aqc_opc_lldp_get_mib			= 0x0A00,
	ice_aqc_opc_lldp_set_mib_change			= 0x0A01,
	ice_aqc_opc_lldp_stop				= 0x0A05,
	ice_aqc_opc_lldp_start				= 0x0A06,
	ice_aqc_opc_get_cee_dcb_cfg			= 0x0A07,
	ice_aqc_opc_lldp_set_local_mib			= 0x0A08,
	ice_aqc_opc_lldp_stop_start_specific_agent	= 0x0A09,

	/* RSS commands */
	ice_aqc_opc_set_rss_key				= 0x0B02,
	ice_aqc_opc_set_rss_lut				= 0x0B03,
	ice_aqc_opc_get_rss_key				= 0x0B04,
	ice_aqc_opc_get_rss_lut				= 0x0B05,

	/* Tx queue handling commands/events */
	ice_aqc_opc_add_txqs				= 0x0C30,
	ice_aqc_opc_dis_txqs				= 0x0C31,

	/* package commands */
	ice_aqc_opc_download_pkg			= 0x0C40,
	ice_aqc_opc_update_pkg				= 0x0C42,
	ice_aqc_opc_get_pkg_info_list			= 0x0C43,

	/* Standalone Commands/Events */
	ice_aqc_opc_event_lan_overflow			= 0x1001,

	/* debug commands */
	ice_aqc_opc_fw_logging				= 0xFF09,
	ice_aqc_opc_fw_logging_info			= 0xFF10,
};

#endif /* _ICE_ADMINQ_CMD_H_ */
