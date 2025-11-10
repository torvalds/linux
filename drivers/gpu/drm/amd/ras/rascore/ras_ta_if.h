/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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

#ifndef _RAS_TA_IF_H
#define _RAS_TA_IF_H
#include "ras.h"

#define RAS_TA_HOST_IF_VER	0

/* Responses have bit 31 set */
#define RSP_ID_MASK (1U << 31)
#define RSP_ID(cmdId) (((uint32_t)(cmdId)) | RSP_ID_MASK)

/* invalid node instance value */
#define RAS_TA_INV_NODE 0xffff

/* RAS related enumerations */
/**********************************************************/
enum ras_ta_cmd_id {
	RAS_TA_CMD_ID__ENABLE_FEATURES = 0,
	RAS_TA_CMD_ID__DISABLE_FEATURES,
	RAS_TA_CMD_ID__TRIGGER_ERROR,
	RAS_TA_CMD_ID__QUERY_BLOCK_INFO,
	RAS_TA_CMD_ID__QUERY_SUB_BLOCK_INFO,
	RAS_TA_CMD_ID__QUERY_ADDRESS,
	MAX_RAS_TA_CMD_ID
};

enum ras_ta_status {
	RAS_TA_STATUS__SUCCESS                          = 0x0000,
	RAS_TA_STATUS__RESET_NEEDED                     = 0xA001,
	RAS_TA_STATUS__ERROR_INVALID_PARAMETER          = 0xA002,
	RAS_TA_STATUS__ERROR_RAS_NOT_AVAILABLE          = 0xA003,
	RAS_TA_STATUS__ERROR_RAS_DUPLICATE_CMD          = 0xA004,
	RAS_TA_STATUS__ERROR_INJECTION_FAILED           = 0xA005,
	RAS_TA_STATUS__ERROR_ASD_READ_WRITE             = 0xA006,
	RAS_TA_STATUS__ERROR_TOGGLE_DF_CSTATE           = 0xA007,
	RAS_TA_STATUS__ERROR_TIMEOUT                    = 0xA008,
	RAS_TA_STATUS__ERROR_BLOCK_DISABLED             = 0XA009,
	RAS_TA_STATUS__ERROR_GENERIC                    = 0xA00A,
	RAS_TA_STATUS__ERROR_RAS_MMHUB_INIT             = 0xA00B,
	RAS_TA_STATUS__ERROR_GET_DEV_INFO               = 0xA00C,
	RAS_TA_STATUS__ERROR_UNSUPPORTED_DEV            = 0xA00D,
	RAS_TA_STATUS__ERROR_NOT_INITIALIZED            = 0xA00E,
	RAS_TA_STATUS__ERROR_TEE_INTERNAL               = 0xA00F,
	RAS_TA_STATUS__ERROR_UNSUPPORTED_FUNCTION       = 0xA010,
	RAS_TA_STATUS__ERROR_SYS_DRV_REG_ACCESS         = 0xA011,
	RAS_TA_STATUS__ERROR_RAS_READ_WRITE             = 0xA012,
	RAS_TA_STATUS__ERROR_NULL_PTR                   = 0xA013,
	RAS_TA_STATUS__ERROR_UNSUPPORTED_IP             = 0xA014,
	RAS_TA_STATUS__ERROR_PCS_STATE_QUIET            = 0xA015,
	RAS_TA_STATUS__ERROR_PCS_STATE_ERROR            = 0xA016,
	RAS_TA_STATUS__ERROR_PCS_STATE_HANG             = 0xA017,
	RAS_TA_STATUS__ERROR_PCS_STATE_UNKNOWN          = 0xA018,
	RAS_TA_STATUS__ERROR_UNSUPPORTED_ERROR_INJ      = 0xA019,
	RAS_TA_STATUS__TEE_ERROR_ACCESS_DENIED          = 0xA01A
};

enum ras_ta_block {
	RAS_TA_BLOCK__UMC = 0,
	RAS_TA_BLOCK__SDMA,
	RAS_TA_BLOCK__GFX,
	RAS_TA_BLOCK__MMHUB,
	RAS_TA_BLOCK__ATHUB,
	RAS_TA_BLOCK__PCIE_BIF,
	RAS_TA_BLOCK__HDP,
	RAS_TA_BLOCK__XGMI_WAFL,
	RAS_TA_BLOCK__DF,
	RAS_TA_BLOCK__SMN,
	RAS_TA_BLOCK__SEM,
	RAS_TA_BLOCK__MP0,
	RAS_TA_BLOCK__MP1,
	RAS_TA_BLOCK__FUSE,
	RAS_TA_BLOCK__MCA,
	RAS_TA_BLOCK__VCN,
	RAS_TA_BLOCK__JPEG,
	RAS_TA_BLOCK__IH,
	RAS_TA_BLOCK__MPIO,
	RAS_TA_BLOCK__MMSCH,
	RAS_TA_NUM_BLOCK_MAX
};

enum ras_ta_mca_block {
	RAS_TA_MCA_BLOCK__MP0   = 0,
	RAS_TA_MCA_BLOCK__MP1   = 1,
	RAS_TA_MCA_BLOCK__MPIO  = 2,
	RAS_TA_MCA_BLOCK__IOHC  = 3,
	RAS_TA_MCA_NUM_BLOCK_MAX
};

enum ras_ta_error_type {
	RAS_TA_ERROR__NONE			= 0,
	RAS_TA_ERROR__PARITY			= 1,
	RAS_TA_ERROR__SINGLE_CORRECTABLE	= 2,
	RAS_TA_ERROR__MULTI_UNCORRECTABLE	= 4,
	RAS_TA_ERROR__POISON			= 8,
};

enum ras_ta_address_type {
	RAS_TA_MCA_TO_PA,
	RAS_TA_PA_TO_MCA,
};

enum ras_ta_nps_mode {
	RAS_TA_UNKNOWN_MODE = 0,
	RAS_TA_NPS1_MODE = 1,
	RAS_TA_NPS2_MODE = 2,
	RAS_TA_NPS4_MODE = 4,
	RAS_TA_NPS8_MODE = 8,
};

/* Input/output structures for RAS commands */
/**********************************************************/

struct ras_ta_enable_features_input {
	enum ras_ta_block	block_id;
	enum ras_ta_error_type	error_type;
};

struct ras_ta_disable_features_input {
	enum ras_ta_block	block_id;
	enum ras_ta_error_type	error_type;
};

struct ras_ta_trigger_error_input {
	/* ras-block. i.e. umc, gfx */
	enum ras_ta_block block_id;

	/* type of error. i.e. single_correctable */
	enum ras_ta_error_type inject_error_type;

	/* mem block. i.e. hbm, sram etc. */
	uint32_t sub_block_index;

	/* explicit address of error */
	uint64_t address;

	/* method if error injection. i.e persistent, coherent etc. */
	uint64_t value;
};

struct ras_ta_init_flags {
	uint8_t poison_mode_en;
	uint8_t dgpu_mode;
	uint16_t xcc_mask;
	uint8_t channel_dis_num;
	uint8_t nps_mode;
	uint32_t active_umc_mask;
};

struct ras_ta_mca_addr {
	uint64_t err_addr;
	uint32_t ch_inst;
	uint32_t umc_inst;
	uint32_t node_inst;
	uint32_t socket_id;
};

struct ras_ta_phy_addr {
	uint64_t pa;
	uint32_t bank;
	uint32_t channel_idx;
};

struct ras_ta_query_address_input {
	enum ras_ta_address_type addr_type;
	struct ras_ta_mca_addr ma;
	struct ras_ta_phy_addr pa;
};

struct ras_ta_output_flags {
	uint8_t ras_init_success_flag;
	uint8_t err_inject_switch_disable_flag;
	uint8_t reg_access_failure_flag;
};

struct ras_ta_query_address_output {
	/* don't use the flags here */
	struct ras_ta_output_flags flags;
	struct ras_ta_mca_addr ma;
	struct ras_ta_phy_addr pa;
};

/* Common input structure for RAS callbacks */
/**********************************************************/
union ras_ta_cmd_input {
	struct ras_ta_init_flags		init_flags;
	struct ras_ta_enable_features_input	enable_features;
	struct ras_ta_disable_features_input	disable_features;
	struct ras_ta_trigger_error_input	trigger_error;
	struct ras_ta_query_address_input	address;
	uint32_t reserve_pad[256];
};

union ras_ta_cmd_output {
	struct ras_ta_output_flags flags;
	struct ras_ta_query_address_output address;
	uint32_t reserve_pad[256];
};

struct ras_ta_cmd {
	uint32_t  cmd_id;
	uint32_t  resp_id;
	uint32_t  ras_status;
	uint32_t  if_version;
	union ras_ta_cmd_input  ras_in_message;
	union ras_ta_cmd_output ras_out_message;
};

#endif
