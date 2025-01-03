/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _TA_RAS_IF_H
#define _TA_RAS_IF_H

#define RAS_TA_HOST_IF_VER	0

/* Responses have bit 31 set */
#define RSP_ID_MASK (1U << 31)
#define RSP_ID(cmdId) (((uint32_t)(cmdId)) | RSP_ID_MASK)

/* RAS related enumerations */
/**********************************************************/
enum ras_command {
	TA_RAS_COMMAND__ENABLE_FEATURES = 0,
	TA_RAS_COMMAND__DISABLE_FEATURES,
	TA_RAS_COMMAND__TRIGGER_ERROR,
	TA_RAS_COMMAND__QUERY_BLOCK_INFO,
	TA_RAS_COMMAND__QUERY_SUB_BLOCK_INFO,
	TA_RAS_COMMAND__QUERY_ADDRESS,
};

enum ta_ras_status {
	TA_RAS_STATUS__SUCCESS                          = 0x0000,
	TA_RAS_STATUS__RESET_NEEDED                     = 0xA001,
	TA_RAS_STATUS__ERROR_INVALID_PARAMETER          = 0xA002,
	TA_RAS_STATUS__ERROR_RAS_NOT_AVAILABLE          = 0xA003,
	TA_RAS_STATUS__ERROR_RAS_DUPLICATE_CMD          = 0xA004,
	TA_RAS_STATUS__ERROR_INJECTION_FAILED           = 0xA005,
	TA_RAS_STATUS__ERROR_ASD_READ_WRITE             = 0xA006,
	TA_RAS_STATUS__ERROR_TOGGLE_DF_CSTATE           = 0xA007,
	TA_RAS_STATUS__ERROR_TIMEOUT                    = 0xA008,
	TA_RAS_STATUS__ERROR_BLOCK_DISABLED             = 0XA009,
	TA_RAS_STATUS__ERROR_GENERIC                    = 0xA00A,
	TA_RAS_STATUS__ERROR_RAS_MMHUB_INIT             = 0xA00B,
	TA_RAS_STATUS__ERROR_GET_DEV_INFO               = 0xA00C,
	TA_RAS_STATUS__ERROR_UNSUPPORTED_DEV            = 0xA00D,
	TA_RAS_STATUS__ERROR_NOT_INITIALIZED            = 0xA00E,
	TA_RAS_STATUS__ERROR_TEE_INTERNAL               = 0xA00F,
	TA_RAS_STATUS__ERROR_UNSUPPORTED_FUNCTION       = 0xA010,
	TA_RAS_STATUS__ERROR_SYS_DRV_REG_ACCESS         = 0xA011,
	TA_RAS_STATUS__ERROR_RAS_READ_WRITE             = 0xA012,
	TA_RAS_STATUS__ERROR_NULL_PTR                   = 0xA013,
	TA_RAS_STATUS__ERROR_UNSUPPORTED_IP             = 0xA014,
	TA_RAS_STATUS__ERROR_PCS_STATE_QUIET            = 0xA015,
	TA_RAS_STATUS__ERROR_PCS_STATE_ERROR            = 0xA016,
	TA_RAS_STATUS__ERROR_PCS_STATE_HANG             = 0xA017,
	TA_RAS_STATUS__ERROR_PCS_STATE_UNKNOWN          = 0xA018,
	TA_RAS_STATUS__ERROR_UNSUPPORTED_ERROR_INJ      = 0xA019,
	TA_RAS_STATUS__TEE_ERROR_ACCESS_DENIED          = 0xA01A
};

enum ta_ras_block {
	TA_RAS_BLOCK__UMC = 0,
	TA_RAS_BLOCK__SDMA,
	TA_RAS_BLOCK__GFX,
	TA_RAS_BLOCK__MMHUB,
	TA_RAS_BLOCK__ATHUB,
	TA_RAS_BLOCK__PCIE_BIF,
	TA_RAS_BLOCK__HDP,
	TA_RAS_BLOCK__XGMI_WAFL,
	TA_RAS_BLOCK__DF,
	TA_RAS_BLOCK__SMN,
	TA_RAS_BLOCK__SEM,
	TA_RAS_BLOCK__MP0,
	TA_RAS_BLOCK__MP1,
	TA_RAS_BLOCK__FUSE,
	TA_RAS_BLOCK__MCA,
	TA_RAS_BLOCK__VCN,
	TA_RAS_BLOCK__JPEG,
	TA_NUM_BLOCK_MAX
};

enum ta_ras_mca_block {
	TA_RAS_MCA_BLOCK__MP0   = 0,
	TA_RAS_MCA_BLOCK__MP1   = 1,
	TA_RAS_MCA_BLOCK__MPIO  = 2,
	TA_RAS_MCA_BLOCK__IOHC  = 3,
	TA_MCA_NUM_BLOCK_MAX
};

enum ta_ras_error_type {
	TA_RAS_ERROR__NONE			= 0,
	TA_RAS_ERROR__PARITY			= 1,
	TA_RAS_ERROR__SINGLE_CORRECTABLE	= 2,
	TA_RAS_ERROR__MULTI_UNCORRECTABLE	= 4,
	TA_RAS_ERROR__POISON			= 8,
};

enum ta_ras_address_type {
	TA_RAS_MCA_TO_PA,
	TA_RAS_PA_TO_MCA,
};

enum ta_ras_nps_mode {
	TA_RAS_UNKNOWN_MODE = 0,
	TA_RAS_NPS1_MODE = 1,
	TA_RAS_NPS2_MODE = 2,
	TA_RAS_NPS4_MODE = 4,
	TA_RAS_NPS8_MODE = 8,
};

/* Input/output structures for RAS commands */
/**********************************************************/

struct ta_ras_enable_features_input {
	enum ta_ras_block	block_id;
	enum ta_ras_error_type	error_type;
};

struct ta_ras_disable_features_input {
	enum ta_ras_block	block_id;
	enum ta_ras_error_type	error_type;
};

struct ta_ras_trigger_error_input {
	enum ta_ras_block	block_id;		// ras-block. i.e. umc, gfx
	enum ta_ras_error_type	inject_error_type;	// type of error. i.e. single_correctable
	uint32_t		sub_block_index;	// mem block. i.e. hbm, sram etc.
	uint64_t		address;		// explicit address of error
	uint64_t		value;			// method if error injection. i.e persistent, coherent etc.
};

struct ta_ras_init_flags {
	uint8_t poison_mode_en;
	uint8_t dgpu_mode;
	uint16_t xcc_mask;
	uint8_t channel_dis_num;
	uint8_t nps_mode;
};

struct ta_ras_mca_addr {
	uint64_t err_addr;
	uint32_t ch_inst;
	uint32_t umc_inst;
	uint32_t node_inst;
	uint32_t socket_id;
};

struct ta_ras_phy_addr {
	uint64_t pa;
	uint32_t bank;
	uint32_t channel_idx;
};

struct ta_ras_query_address_input {
	enum ta_ras_address_type addr_type;
	struct ta_ras_mca_addr ma;
	struct ta_ras_phy_addr pa;
};

struct ta_ras_output_flags {
	uint8_t ras_init_success_flag;
	uint8_t err_inject_switch_disable_flag;
	uint8_t reg_access_failure_flag;
};

struct ta_ras_query_address_output {
	/* don't use the flags here */
	struct ta_ras_output_flags flags;
	struct ta_ras_mca_addr ma;
	struct ta_ras_phy_addr pa;
};

/* Common input structure for RAS callbacks */
/**********************************************************/
union ta_ras_cmd_input {
	struct ta_ras_init_flags		init_flags;
	struct ta_ras_enable_features_input	enable_features;
	struct ta_ras_disable_features_input	disable_features;
	struct ta_ras_trigger_error_input	trigger_error;
	struct ta_ras_query_address_input	address;

	uint32_t reserve_pad[256];
};

union ta_ras_cmd_output {
	struct ta_ras_output_flags flags;
	struct ta_ras_query_address_output address;

	uint32_t reserve_pad[256];
};

/* Shared Memory structures */
/**********************************************************/
struct ta_ras_shared_memory {
	uint32_t		    cmd_id;
	uint32_t		    resp_id;
	uint32_t	    	    ras_status;
	uint32_t		    if_version;
	union ta_ras_cmd_input	    ras_in_message;
	union ta_ras_cmd_output     ras_out_message;
};

#endif // TL_RAS_IF_H_
