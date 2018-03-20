/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_ADMINQ_CMD_H_
#define _ICE_ADMINQ_CMD_H_

/* This header file defines the Admin Queue commands, error codes and
 * descriptor format.  It is shared between Firmware and Software.
 */

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

/* Clear PXE Command and response (direct 0x0110) */
struct ice_aqc_clear_pxe {
	u8 rx_cnt;
#define ICE_AQC_CLEAR_PXE_RX_CNT		0x2
	u8 reserved[15];
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
		struct ice_aqc_nvm nvm;
	} params;
};

/* FW defined boundary for a large buffer, 4k >= Large buffer > 512 bytes */
#define ICE_AQ_LG_BUF	512

#define ICE_AQ_FLAG_LB_S	9
#define ICE_AQ_FLAG_BUF_S	12
#define ICE_AQ_FLAG_SI_S	13

#define ICE_AQ_FLAG_LB		BIT(ICE_AQ_FLAG_LB_S)  /* 0x200  */
#define ICE_AQ_FLAG_BUF		BIT(ICE_AQ_FLAG_BUF_S) /* 0x1000 */
#define ICE_AQ_FLAG_SI		BIT(ICE_AQ_FLAG_SI_S)  /* 0x2000 */

/* error codes */
enum ice_aq_err {
	ICE_AQ_RC_OK		= 0,  /* success */
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

	/* PXE */
	ice_aqc_opc_clear_pxe_mode			= 0x0110,

	ice_aqc_opc_clear_pf_cfg			= 0x02A4,

	/* NVM commands */
	ice_aqc_opc_nvm_read				= 0x0701,

};

#endif /* _ICE_ADMINQ_CMD_H_ */
