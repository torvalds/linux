/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_ADMINQ_CMD_H_
#define _ICE_ADMINQ_CMD_H_

/* This header file defines the Admin Queue commands, error codes and
 * descriptor format.  It is shared between Firmware and Software.
 */

#define ICE_AQC_TOPO_MAX_LEVEL_NUM	0x9

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

/* Queue Shutdown (direct 0x0003) */
struct ice_aqc_q_shutdown {
#define ICE_AQC_DRIVER_UNLOADING	BIT(0)
	__le32 driver_unloading;
	u8 reserved[12];
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
	/* For SDP: pin id of the SDP */
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
#define ICE_AQC_CAPS_VSI				0x0017
#define ICE_AQC_CAPS_RSS				0x0040
#define ICE_AQC_CAPS_RXQS				0x0041
#define ICE_AQC_CAPS_TXQS				0x0042
#define ICE_AQC_CAPS_MSIX				0x0043
#define ICE_AQC_CAPS_MAX_MTU				0x0047

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
	u8 lport_num;
	u8 lport_num_valid;
#define ICE_AQC_MAN_MAC_PORT_NUM_IS_VALID	BIT(0)
	u8 num_addr; /* Used in response */
	u8 reserved[3];
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

/* The response buffer is as follows. Note that the length of the
 * elements array varies with the length of the command response.
 */
struct ice_aqc_get_sw_cfg_resp {
	struct ice_aqc_get_sw_cfg_resp_elem elements[1];
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

/* Add TSE (indirect 0x0401)
 * Delete TSE (indirect 0x040F)
 * Move TSE (indirect 0x0408)
 */
struct ice_aqc_add_move_delete_elem {
	__le16 num_grps_req;
	__le16 num_grps_updated;
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

struct ice_aqc_get_topo_elem {
	struct ice_aqc_txsched_topo_grp_info_hdr hdr;
	struct ice_aqc_txsched_elem_data
		generic[ICE_AQC_TOPO_MAX_LEVEL_NUM];
};

struct ice_aqc_delete_elem {
	struct ice_aqc_txsched_topo_grp_info_hdr hdr;
	__le32 teid[1];
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
	u8 rsvd0[2];
	__le16 max_shared_rate_lmtr;
	__le16 max_children;
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
#define ICE_AQC_REPORT_MODE_S		1
#define ICE_AQC_REPORT_MODE_M		(3 << ICE_AQC_REPORT_MODE_S)
#define ICE_AQC_REPORT_NVM_CAP		0
#define ICE_AQC_REPORT_TOPO_CAP		BIT(1)
#define ICE_AQC_REPORT_SW_CFG		BIT(2)
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
#define ICE_PHY_TYPE_LOW_MAX_INDEX		63

struct ice_aqc_get_phy_caps_data {
	__le64 phy_type_low; /* Use values from ICE_PHY_TYPE_LOW_* */
	__le64 reserved;
	u8 caps;
#define ICE_AQC_PHY_EN_TX_LINK_PAUSE			BIT(0)
#define ICE_AQC_PHY_EN_RX_LINK_PAUSE			BIT(1)
#define ICE_AQC_PHY_LOW_POWER_MODE			BIT(2)
#define ICE_AQC_PHY_EN_LINK				BIT(3)
#define ICE_AQC_PHY_AN_MODE				BIT(4)
#define ICE_AQC_GET_PHY_EN_MOD_QUAL			BIT(5)
	u8 low_power_ctrl;
#define ICE_AQC_PHY_EN_D3COLD_LOW_POWER_AUTONEG		BIT(0)
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
	u8 link_fec_options;
#define ICE_AQC_PHY_FEC_10G_KR_40G_KR4_EN		BIT(0)
#define ICE_AQC_PHY_FEC_10G_KR_40G_KR4_REQ		BIT(1)
#define ICE_AQC_PHY_FEC_25G_RS_528_REQ			BIT(2)
#define ICE_AQC_PHY_FEC_25G_KR_REQ			BIT(3)
#define ICE_AQC_PHY_FEC_25G_RS_544_REQ			BIT(4)
#define ICE_AQC_PHY_FEC_25G_RS_CLAUSE91_EN		BIT(6)
#define ICE_AQC_PHY_FEC_25G_KR_CLAUSE74_EN		BIT(7)
	u8 extended_compliance_code;
#define ICE_MODULE_TYPE_TOTAL_BYTE			3
	u8 module_type[ICE_MODULE_TYPE_TOTAL_BYTE];
#define ICE_AQC_MOD_TYPE_BYTE0_SFP_PLUS			0xA0
#define ICE_AQC_MOD_TYPE_BYTE0_QSFP_PLUS		0x80
#define ICE_AQC_MOD_TYPE_BYTE1_SFP_PLUS_CU_PASSIVE	BIT(0)
#define ICE_AQC_MOD_TYPE_BYTE1_SFP_PLUS_CU_ACTIVE	BIT(1)
#define ICE_AQC_MOD_TYPE_BYTE1_10G_BASE_SR		BIT(4)
#define ICE_AQC_MOD_TYPE_BYTE1_10G_BASE_LR		BIT(5)
#define ICE_AQC_MOD_TYPE_BYTE1_10G_BASE_LRM		BIT(6)
#define ICE_AQC_MOD_TYPE_BYTE1_10G_BASE_ER		BIT(7)
#define ICE_AQC_MOD_TYPE_BYTE2_SFP_PLUS			0xA0
#define ICE_AQC_MOD_TYPE_BYTE2_QSFP_PLUS		0x86
	u8 qualified_module_count;
#define ICE_AQC_QUAL_MOD_COUNT_MAX			16
	struct {
		u8 v_oui[3];
		u8 rsvd1;
		u8 v_part[16];
		__le32 v_rev;
		__le64 rsvd8;
	} qual_modules[ICE_AQC_QUAL_MOD_COUNT_MAX];
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
	/* Port TX Suspended */
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
#define ICE_AQ_LINK_SPEED_10MB		BIT(0)
#define ICE_AQ_LINK_SPEED_100MB		BIT(1)
#define ICE_AQ_LINK_SPEED_1000MB	BIT(2)
#define ICE_AQ_LINK_SPEED_2500MB	BIT(3)
#define ICE_AQ_LINK_SPEED_5GB		BIT(4)
#define ICE_AQ_LINK_SPEED_10GB		BIT(5)
#define ICE_AQ_LINK_SPEED_20GB		BIT(6)
#define ICE_AQ_LINK_SPEED_25GB		BIT(7)
#define ICE_AQ_LINK_SPEED_40GB		BIT(8)
#define ICE_AQ_LINK_SPEED_UNKNOWN	BIT(15)
	__le32 reserved3; /* Aligns next field to 8-byte boundary */
	__le64 phy_type_low; /* Use values from ICE_PHY_TYPE_LOW_* */
	__le64 reserved4;
};

/* NVM Read command (indirect 0x0701)
 * NVM Erase commands (direct 0x0702)
 * NVM Update commands (indirect 0x0703)
 */
struct ice_aqc_nvm {
	u8	cmd_flags;
#define ICE_AQC_NVM_LAST_CMD		BIT(0)
#define ICE_AQC_NVM_PCIR_REQ		BIT(0)	/* Used by NVM Update reply */
#define ICE_AQC_NVM_PRESERVATION_S	1
#define ICE_AQC_NVM_PRESERVATION_M	(3 << CSR_AQ_NVM_PRESERVATION_S)
#define ICE_AQC_NVM_NO_PRESERVATION	(0 << CSR_AQ_NVM_PRESERVATION_S)
#define ICE_AQC_NVM_PRESERVE_ALL	BIT(1)
#define ICE_AQC_NVM_PRESERVE_SELECTED	(3 << CSR_AQ_NVM_PRESERVATION_S)
#define ICE_AQC_NVM_FLASH_ONLY		BIT(7)
	u8	module_typeid;
	__le16	length;
#define ICE_AQC_NVM_ERASE_LEN	0xFFFF
	__le32	offset;
	__le32	addr_high;
	__le32	addr_low;
};

/**
 * struct ice_aq_desc - Admin Queue (AQ) descriptor
 * @flags: ICE_AQ_FLAG_* flags
 * @opcode: AQ command opcode
 * @datalen: length in bytes of indirect/external data buffer
 * @retval: return value from firmware
 * @cookie_h: opaque data high-half
 * @cookie_l: opaque data low-half
 * @params: command-specific parameters
 *
 * Descriptor format for commands the driver posts on the Admin Transmit Queue
 * (ATQ).  The firmware writes back onto the command descriptor and returns
 * the result of the command.  Asynchronous events that are not an immediate
 * result of the command are written to the Admin Receive Queue (ARQ) using
 * the same descriptor format.  Descriptors are in little-endian notation with
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
		struct ice_aqc_q_shutdown q_shutdown;
		struct ice_aqc_req_res res_owner;
		struct ice_aqc_manage_mac_read mac_read;
		struct ice_aqc_clear_pxe clear_pxe;
		struct ice_aqc_list_caps get_cap;
		struct ice_aqc_get_phy_caps get_phy;
		struct ice_aqc_get_sw_cfg get_sw_conf;
		struct ice_aqc_get_topo get_topo;
		struct ice_aqc_query_txsched_res query_sched_res;
		struct ice_aqc_add_move_delete_elem add_move_delete_elem;
		struct ice_aqc_nvm nvm;
		struct ice_aqc_get_link_status get_link_status;
	} params;
};

/* FW defined boundary for a large buffer, 4k >= Large buffer > 512 bytes */
#define ICE_AQ_LG_BUF	512

#define ICE_AQ_FLAG_LB_S	9
#define ICE_AQ_FLAG_RD_S	10
#define ICE_AQ_FLAG_BUF_S	12
#define ICE_AQ_FLAG_SI_S	13

#define ICE_AQ_FLAG_LB		BIT(ICE_AQ_FLAG_LB_S)  /* 0x200  */
#define ICE_AQ_FLAG_RD		BIT(ICE_AQ_FLAG_RD_S)  /* 0x400  */
#define ICE_AQ_FLAG_BUF		BIT(ICE_AQ_FLAG_BUF_S) /* 0x1000 */
#define ICE_AQ_FLAG_SI		BIT(ICE_AQ_FLAG_SI_S)  /* 0x2000 */

/* error codes */
enum ice_aq_err {
	ICE_AQ_RC_OK		= 0,  /* success */
	ICE_AQ_RC_ENOMEM	= 9,  /* Out of memory */
	ICE_AQ_RC_EBUSY		= 12, /* Device or resource busy */
	ICE_AQ_RC_EEXIST	= 13, /* object already exists */
};

/* Admin Queue command opcodes */
enum ice_adminq_opc {
	/* AQ commands */
	ice_aqc_opc_get_ver				= 0x0001,
	ice_aqc_opc_q_shutdown				= 0x0003,

	/* resource ownership */
	ice_aqc_opc_req_res				= 0x0008,
	ice_aqc_opc_release_res				= 0x0009,

	/* device/function capabilities */
	ice_aqc_opc_list_func_caps			= 0x000A,
	ice_aqc_opc_list_dev_caps			= 0x000B,

	/* manage MAC address */
	ice_aqc_opc_manage_mac_read			= 0x0107,

	/* PXE */
	ice_aqc_opc_clear_pxe_mode			= 0x0110,

	/* internal switch commands */
	ice_aqc_opc_get_sw_cfg				= 0x0200,

	ice_aqc_opc_clear_pf_cfg			= 0x02A4,

	/* transmit scheduler commands */
	ice_aqc_opc_get_dflt_topo			= 0x0400,
	ice_aqc_opc_delete_sched_elems			= 0x040F,
	ice_aqc_opc_query_sched_res			= 0x0412,

	/* PHY commands */
	ice_aqc_opc_get_phy_caps			= 0x0600,
	ice_aqc_opc_get_link_status			= 0x0607,

	/* NVM commands */
	ice_aqc_opc_nvm_read				= 0x0701,

};

#endif /* _ICE_ADMINQ_CMD_H_ */
