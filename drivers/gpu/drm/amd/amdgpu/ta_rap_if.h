/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef _TA_RAP_IF_H
#define _TA_RAP_IF_H

/* Responses have bit 31 set */
#define RSP_ID_MASK (1U << 31)
#define RSP_ID(cmdId) (((uint32_t)(cmdId)) | RSP_ID_MASK)

enum ta_rap_status {
	TA_RAP_STATUS__SUCCESS                              = 1,
	TA_RAP_STATUS__ERROR_GENERIC_FAILURE                = 2,
	TA_RAP_STATUS__ERROR_CMD_NOT_SUPPORTED              = 3,
	TA_RAP_STATUS__ERROR_INVALID_VALIDATION_METHOD      = 4,
	TA_RAP_STATUS__ERROR_NULL_POINTER                   = 5,
	TA_RAP_STATUS__ERROR_NOT_INITIALIZED                = 6,
	TA_RAP_STATUS__ERROR_VALIDATION_FAILED              = 7,
	TA_RAP_STATUS__ERROR_ASIC_NOT_SUPPORTED             = 8,
	TA_RAP_STATUS__ERROR_OPERATION_NOT_PERMISSABLE      = 9,
	TA_RAP_STATUS__ERROR_ALREADY_INIT                   = 10,
};

enum ta_rap_cmd {
	TA_CMD_RAP__INITIALIZE              = 1,
	TA_CMD_RAP__VALIDATE_L0             = 2,
};

enum ta_rap_validation_method {
	METHOD_A           = 1,
};

struct ta_rap_cmd_input_data {
	uint8_t reserved[8];
};

struct ta_rap_cmd_output_data {
	uint32_t    last_subsection;
	uint32_t    num_total_validate;
	uint32_t    num_valid;
	uint32_t    last_validate_addr;
	uint32_t    last_validate_val;
	uint32_t    last_validate_val_exptd;
};

union ta_rap_cmd_input {
	struct ta_rap_cmd_input_data input;
};

union ta_rap_cmd_output {
	struct ta_rap_cmd_output_data output;
};

struct ta_rap_shared_memory {
	uint32_t                    cmd_id;
	uint32_t                    validation_method_id;
	uint32_t                    resp_id;
	enum ta_rap_status          rap_status;
	union ta_rap_cmd_input      rap_in_message;
	union ta_rap_cmd_output     rap_out_message;
	uint8_t                     reserved[64];
};

#endif  // #define _TA_RAP_IF_H
