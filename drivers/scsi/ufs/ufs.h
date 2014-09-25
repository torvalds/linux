/*
 * Universal Flash Storage Host controller driver
 *
 * This code is based on drivers/scsi/ufs/ufs.h
 * Copyright (C) 2011-2013 Samsung India Software Operations
 *
 * Authors:
 *	Santosh Yaraganavi <santosh.sy@samsung.com>
 *	Vinayak Holikatti <h.vinayak@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * See the COPYING file in the top-level directory or visit
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This program is provided "AS IS" and "WITH ALL FAULTS" and
 * without warranty of any kind. You are solely responsible for
 * determining the appropriateness of using and distributing
 * the program and assume all risks associated with your exercise
 * of rights with respect to the program, including but not limited
 * to infringement of third party rights, the risks and costs of
 * program errors, damage to or loss of data, programs or equipment,
 * and unavailability or interruption of operations. Under no
 * circumstances will the contributor of this Program be liable for
 * any damages of any kind arising from your use or distribution of
 * this program.
 */

#ifndef _UFS_H
#define _UFS_H

#include <linux/mutex.h>
#include <linux/types.h>

#define MAX_CDB_SIZE	16
#define GENERAL_UPIU_REQUEST_SIZE 32
#define QUERY_DESC_MAX_SIZE       255
#define QUERY_DESC_MIN_SIZE       2
#define QUERY_OSF_SIZE            (GENERAL_UPIU_REQUEST_SIZE - \
					(sizeof(struct utp_upiu_header)))

#define UPIU_HEADER_DWORD(byte3, byte2, byte1, byte0)\
			cpu_to_be32((byte3 << 24) | (byte2 << 16) |\
			 (byte1 << 8) | (byte0))

/*
 * UFS Protocol Information Unit related definitions
 */

/* Task management functions */
enum {
	UFS_ABORT_TASK		= 0x01,
	UFS_ABORT_TASK_SET	= 0x02,
	UFS_CLEAR_TASK_SET	= 0x04,
	UFS_LOGICAL_RESET	= 0x08,
	UFS_QUERY_TASK		= 0x80,
	UFS_QUERY_TASK_SET	= 0x81,
};

/* UTP UPIU Transaction Codes Initiator to Target */
enum {
	UPIU_TRANSACTION_NOP_OUT	= 0x00,
	UPIU_TRANSACTION_COMMAND	= 0x01,
	UPIU_TRANSACTION_DATA_OUT	= 0x02,
	UPIU_TRANSACTION_TASK_REQ	= 0x04,
	UPIU_TRANSACTION_QUERY_REQ	= 0x16,
};

/* UTP UPIU Transaction Codes Target to Initiator */
enum {
	UPIU_TRANSACTION_NOP_IN		= 0x20,
	UPIU_TRANSACTION_RESPONSE	= 0x21,
	UPIU_TRANSACTION_DATA_IN	= 0x22,
	UPIU_TRANSACTION_TASK_RSP	= 0x24,
	UPIU_TRANSACTION_READY_XFER	= 0x31,
	UPIU_TRANSACTION_QUERY_RSP	= 0x36,
	UPIU_TRANSACTION_REJECT_UPIU	= 0x3F,
};

/* UPIU Read/Write flags */
enum {
	UPIU_CMD_FLAGS_NONE	= 0x00,
	UPIU_CMD_FLAGS_WRITE	= 0x20,
	UPIU_CMD_FLAGS_READ	= 0x40,
};

/* UPIU Task Attributes */
enum {
	UPIU_TASK_ATTR_SIMPLE	= 0x00,
	UPIU_TASK_ATTR_ORDERED	= 0x01,
	UPIU_TASK_ATTR_HEADQ	= 0x02,
	UPIU_TASK_ATTR_ACA	= 0x03,
};

/* UPIU Query request function */
enum {
	UPIU_QUERY_FUNC_STANDARD_READ_REQUEST           = 0x01,
	UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST          = 0x81,
};

/* Flag idn for Query Requests*/
enum flag_idn {
	QUERY_FLAG_IDN_FDEVICEINIT      = 0x01,
	QUERY_FLAG_IDN_BKOPS_EN         = 0x04,
};

/* Attribute idn for Query requests */
enum attr_idn {
	QUERY_ATTR_IDN_BKOPS_STATUS	= 0x05,
	QUERY_ATTR_IDN_EE_CONTROL	= 0x0D,
	QUERY_ATTR_IDN_EE_STATUS	= 0x0E,
};

/* Descriptor idn for Query requests */
enum desc_idn {
	QUERY_DESC_IDN_DEVICE		= 0x0,
	QUERY_DESC_IDN_CONFIGURAION	= 0x1,
	QUERY_DESC_IDN_UNIT		= 0x2,
	QUERY_DESC_IDN_RFU_0		= 0x3,
	QUERY_DESC_IDN_INTERCONNECT	= 0x4,
	QUERY_DESC_IDN_STRING		= 0x5,
	QUERY_DESC_IDN_RFU_1		= 0x6,
	QUERY_DESC_IDN_GEOMETRY		= 0x7,
	QUERY_DESC_IDN_POWER		= 0x8,
	QUERY_DESC_IDN_RFU_2		= 0x9,
};

#define UNIT_DESC_MAX_SIZE       0x22
/* Unit descriptor parameters offsets in bytes*/
enum unit_desc_param {
	UNIT_DESC_PARAM_LEN			= 0x0,
	UNIT_DESC_PARAM_TYPE			= 0x1,
	UNIT_DESC_PARAM_UNIT_INDEX		= 0x2,
	UNIT_DESC_PARAM_LU_ENABLE		= 0x3,
	UNIT_DESC_PARAM_BOOT_LUN_ID		= 0x4,
	UNIT_DESC_PARAM_LU_WR_PROTECT		= 0x5,
	UNIT_DESC_PARAM_LU_Q_DEPTH		= 0x6,
	UNIT_DESC_PARAM_MEM_TYPE		= 0x8,
	UNIT_DESC_PARAM_DATA_RELIABILITY	= 0x9,
	UNIT_DESC_PARAM_LOGICAL_BLK_SIZE	= 0xA,
	UNIT_DESC_PARAM_LOGICAL_BLK_COUNT	= 0xB,
	UNIT_DESC_PARAM_ERASE_BLK_SIZE		= 0x13,
	UNIT_DESC_PARAM_PROVISIONING_TYPE	= 0x17,
	UNIT_DESC_PARAM_PHY_MEM_RSRC_CNT	= 0x18,
	UNIT_DESC_PARAM_CTX_CAPABILITIES	= 0x20,
	UNIT_DESC_PARAM_LARGE_UNIT_SIZE_M1	= 0x22,
};

/* Exception event mask values */
enum {
	MASK_EE_STATUS		= 0xFFFF,
	MASK_EE_URGENT_BKOPS	= (1 << 2),
};

/* Background operation status */
enum {
	BKOPS_STATUS_NO_OP               = 0x0,
	BKOPS_STATUS_NON_CRITICAL        = 0x1,
	BKOPS_STATUS_PERF_IMPACT         = 0x2,
	BKOPS_STATUS_CRITICAL            = 0x3,
};

/* UTP QUERY Transaction Specific Fields OpCode */
enum query_opcode {
	UPIU_QUERY_OPCODE_NOP		= 0x0,
	UPIU_QUERY_OPCODE_READ_DESC	= 0x1,
	UPIU_QUERY_OPCODE_WRITE_DESC	= 0x2,
	UPIU_QUERY_OPCODE_READ_ATTR	= 0x3,
	UPIU_QUERY_OPCODE_WRITE_ATTR	= 0x4,
	UPIU_QUERY_OPCODE_READ_FLAG	= 0x5,
	UPIU_QUERY_OPCODE_SET_FLAG	= 0x6,
	UPIU_QUERY_OPCODE_CLEAR_FLAG	= 0x7,
	UPIU_QUERY_OPCODE_TOGGLE_FLAG	= 0x8,
};

/* Query response result code */
enum {
	QUERY_RESULT_SUCCESS                    = 0x00,
	QUERY_RESULT_NOT_READABLE               = 0xF6,
	QUERY_RESULT_NOT_WRITEABLE              = 0xF7,
	QUERY_RESULT_ALREADY_WRITTEN            = 0xF8,
	QUERY_RESULT_INVALID_LENGTH             = 0xF9,
	QUERY_RESULT_INVALID_VALUE              = 0xFA,
	QUERY_RESULT_INVALID_SELECTOR           = 0xFB,
	QUERY_RESULT_INVALID_INDEX              = 0xFC,
	QUERY_RESULT_INVALID_IDN                = 0xFD,
	QUERY_RESULT_INVALID_OPCODE             = 0xFE,
	QUERY_RESULT_GENERAL_FAILURE            = 0xFF,
};

/* UTP Transfer Request Command Type (CT) */
enum {
	UPIU_COMMAND_SET_TYPE_SCSI	= 0x0,
	UPIU_COMMAND_SET_TYPE_UFS	= 0x1,
	UPIU_COMMAND_SET_TYPE_QUERY	= 0x2,
};

/* UTP Transfer Request Command Offset */
#define UPIU_COMMAND_TYPE_OFFSET	28

/* Offset of the response code in the UPIU header */
#define UPIU_RSP_CODE_OFFSET		8

enum {
	MASK_SCSI_STATUS		= 0xFF,
	MASK_TASK_RESPONSE              = 0xFF00,
	MASK_RSP_UPIU_RESULT            = 0xFFFF,
	MASK_QUERY_DATA_SEG_LEN         = 0xFFFF,
	MASK_RSP_UPIU_DATA_SEG_LEN	= 0xFFFF,
	MASK_RSP_EXCEPTION_EVENT        = 0x10000,
};

/* Task management service response */
enum {
	UPIU_TASK_MANAGEMENT_FUNC_COMPL		= 0x00,
	UPIU_TASK_MANAGEMENT_FUNC_NOT_SUPPORTED = 0x04,
	UPIU_TASK_MANAGEMENT_FUNC_SUCCEEDED	= 0x08,
	UPIU_TASK_MANAGEMENT_FUNC_FAILED	= 0x05,
	UPIU_INCORRECT_LOGICAL_UNIT_NO		= 0x09,
};
/**
 * struct utp_upiu_header - UPIU header structure
 * @dword_0: UPIU header DW-0
 * @dword_1: UPIU header DW-1
 * @dword_2: UPIU header DW-2
 */
struct utp_upiu_header {
	__be32 dword_0;
	__be32 dword_1;
	__be32 dword_2;
};

/**
 * struct utp_upiu_cmd - Command UPIU structure
 * @data_transfer_len: Data Transfer Length DW-3
 * @cdb: Command Descriptor Block CDB DW-4 to DW-7
 */
struct utp_upiu_cmd {
	__be32 exp_data_transfer_len;
	u8 cdb[MAX_CDB_SIZE];
};

/**
 * struct utp_upiu_query - upiu request buffer structure for
 * query request.
 * @opcode: command to perform B-0
 * @idn: a value that indicates the particular type of data B-1
 * @index: Index to further identify data B-2
 * @selector: Index to further identify data B-3
 * @reserved_osf: spec reserved field B-4,5
 * @length: number of descriptor bytes to read/write B-6,7
 * @value: Attribute value to be written DW-5
 * @reserved: spec reserved DW-6,7
 */
struct utp_upiu_query {
	u8 opcode;
	u8 idn;
	u8 index;
	u8 selector;
	__be16 reserved_osf;
	__be16 length;
	__be32 value;
	__be32 reserved[2];
};

/**
 * struct utp_upiu_req - general upiu request structure
 * @header:UPIU header structure DW-0 to DW-2
 * @sc: fields structure for scsi command DW-3 to DW-7
 * @qr: fields structure for query request DW-3 to DW-7
 */
struct utp_upiu_req {
	struct utp_upiu_header header;
	union {
		struct utp_upiu_cmd sc;
		struct utp_upiu_query qr;
	};
};

/**
 * struct utp_cmd_rsp - Response UPIU structure
 * @residual_transfer_count: Residual transfer count DW-3
 * @reserved: Reserved double words DW-4 to DW-7
 * @sense_data_len: Sense data length DW-8 U16
 * @sense_data: Sense data field DW-8 to DW-12
 */
struct utp_cmd_rsp {
	__be32 residual_transfer_count;
	__be32 reserved[4];
	__be16 sense_data_len;
	u8 sense_data[18];
};

/**
 * struct utp_upiu_rsp - general upiu response structure
 * @header: UPIU header structure DW-0 to DW-2
 * @sr: fields structure for scsi command DW-3 to DW-12
 * @qr: fields structure for query request DW-3 to DW-7
 */
struct utp_upiu_rsp {
	struct utp_upiu_header header;
	union {
		struct utp_cmd_rsp sr;
		struct utp_upiu_query qr;
	};
};

/**
 * struct utp_upiu_task_req - Task request UPIU structure
 * @header - UPIU header structure DW0 to DW-2
 * @input_param1: Input parameter 1 DW-3
 * @input_param2: Input parameter 2 DW-4
 * @input_param3: Input parameter 3 DW-5
 * @reserved: Reserved double words DW-6 to DW-7
 */
struct utp_upiu_task_req {
	struct utp_upiu_header header;
	__be32 input_param1;
	__be32 input_param2;
	__be32 input_param3;
	__be32 reserved[2];
};

/**
 * struct utp_upiu_task_rsp - Task Management Response UPIU structure
 * @header: UPIU header structure DW0-DW-2
 * @output_param1: Ouput parameter 1 DW3
 * @output_param2: Output parameter 2 DW4
 * @reserved: Reserved double words DW-5 to DW-7
 */
struct utp_upiu_task_rsp {
	struct utp_upiu_header header;
	__be32 output_param1;
	__be32 output_param2;
	__be32 reserved[3];
};

/**
 * struct ufs_query_req - parameters for building a query request
 * @query_func: UPIU header query function
 * @upiu_req: the query request data
 */
struct ufs_query_req {
	u8 query_func;
	struct utp_upiu_query upiu_req;
};

/**
 * struct ufs_query_resp - UPIU QUERY
 * @response: device response code
 * @upiu_res: query response data
 */
struct ufs_query_res {
	u8 response;
	struct utp_upiu_query upiu_res;
};

#define UFS_VREG_VCC_MIN_UV	   2700000 /* uV */
#define UFS_VREG_VCC_MAX_UV	   3600000 /* uV */
#define UFS_VREG_VCC_1P8_MIN_UV    1700000 /* uV */
#define UFS_VREG_VCC_1P8_MAX_UV    1950000 /* uV */
#define UFS_VREG_VCCQ_MIN_UV	   1100000 /* uV */
#define UFS_VREG_VCCQ_MAX_UV	   1300000 /* uV */
#define UFS_VREG_VCCQ2_MIN_UV	   1650000 /* uV */
#define UFS_VREG_VCCQ2_MAX_UV	   1950000 /* uV */

struct ufs_vreg {
	struct regulator *reg;
	const char *name;
	bool enabled;
	int min_uV;
	int max_uV;
	int min_uA;
	int max_uA;
};

struct ufs_vreg_info {
	struct ufs_vreg *vcc;
	struct ufs_vreg *vccq;
	struct ufs_vreg *vccq2;
	struct ufs_vreg *vdd_hba;
};

#endif /* End of Header */
