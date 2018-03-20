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
		struct ice_aqc_clear_pxe clear_pxe;
		struct ice_aqc_list_caps get_cap;
		struct ice_aqc_get_sw_cfg get_sw_conf;
		struct ice_aqc_query_txsched_res query_sched_res;
		struct ice_aqc_add_move_delete_elem add_move_delete_elem;
		struct ice_aqc_nvm nvm;
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

	/* PXE */
	ice_aqc_opc_clear_pxe_mode			= 0x0110,

	/* internal switch commands */
	ice_aqc_opc_get_sw_cfg				= 0x0200,

	ice_aqc_opc_clear_pf_cfg			= 0x02A4,

	/* transmit scheduler commands */
	ice_aqc_opc_delete_sched_elems			= 0x040F,
	ice_aqc_opc_query_sched_res			= 0x0412,

	/* NVM commands */
	ice_aqc_opc_nvm_read				= 0x0701,

};

#endif /* _ICE_ADMINQ_CMD_H_ */
