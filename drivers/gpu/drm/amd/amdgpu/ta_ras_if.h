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

/* Responses have bit 31 set */
#define RSP_ID_MASK (1U << 31)
#define RSP_ID(cmdId) (((uint32_t)(cmdId)) | RSP_ID_MASK)

/* RAS related enumerations */
/**********************************************************/
enum ras_command {
	TA_RAS_COMMAND__ENABLE_FEATURES = 0,
	TA_RAS_COMMAND__DISABLE_FEATURES,
	TA_RAS_COMMAND__TRIGGER_ERROR,
};

enum ta_ras_status {
	TA_RAS_STATUS__SUCCESS				= 0x00,
	TA_RAS_STATUS__RESET_NEEDED			= 0x01,
	TA_RAS_STATUS__ERROR_INVALID_PARAMETER		= 0x02,
	TA_RAS_STATUS__ERROR_RAS_NOT_AVAILABLE		= 0x03,
	TA_RAS_STATUS__ERROR_RAS_DUPLICATE_CMD		= 0x04,
	TA_RAS_STATUS__ERROR_INJECTION_FAILED		= 0x05,
	TA_RAS_STATUS__ERROR_ASD_READ_WRITE		= 0x06,
	TA_RAS_STATUS__ERROR_TOGGLE_DF_CSTATE		= 0x07,
	TA_RAS_STATUS__ERROR_TIMEOUT			= 0x08,
	TA_RAS_STATUS__ERROR_BLOCK_DISABLED		= 0x09,
	TA_RAS_STATUS__ERROR_GENERIC			= 0x10,
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
	TA_NUM_BLOCK_MAX
};

enum ta_ras_error_type {
	TA_RAS_ERROR__NONE			= 0,
	TA_RAS_ERROR__PARITY			= 1,
	TA_RAS_ERROR__SINGLE_CORRECTABLE	= 2,
	TA_RAS_ERROR__MULTI_UNCORRECTABLE	= 4,
	TA_RAS_ERROR__POISON			= 8,
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

/* Common input structure for RAS callbacks */
/**********************************************************/
union ta_ras_cmd_input {
	struct ta_ras_enable_features_input	enable_features;
	struct ta_ras_disable_features_input	disable_features;
	struct ta_ras_trigger_error_input	trigger_error;
};

/* Shared Memory structures */
/**********************************************************/
struct ta_ras_shared_memory {
	uint32_t		cmd_id;
	uint32_t		resp_id;
	enum ta_ras_status	ras_status;
	uint32_t		reserved;
	union ta_ras_cmd_input	ras_in_message;
};

#endif // TL_RAS_IF_H_
