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
 * Authors: AMD
 *
 */

#ifndef _DMUB_GPINT_CMD_H_
#define _DMUB_GPINT_CMD_H_

#include "dmub_types.h"

/**
 * The register format for sending a command via the GPINT.
 */
union dmub_gpint_data_register {
	struct {
		uint32_t param : 16;
		uint32_t command_code : 12;
		uint32_t status : 4;
	} bits;
	uint32_t all;
};

/**
 * The shifts and masks below may alternatively be used to format and read
 * the command register bits.
 */

#define DMUB_GPINT_DATA_PARAM_MASK 0xFFFF
#define DMUB_GPINT_DATA_PARAM_SHIFT 0

#define DMUB_GPINT_DATA_COMMAND_CODE_MASK 0xFFF
#define DMUB_GPINT_DATA_COMMAND_CODE_SHIFT 16

#define DMUB_GPINT_DATA_STATUS_MASK 0xF
#define DMUB_GPINT_DATA_STATUS_SHIFT 28

/*
 * Command IDs should be treated as stable ABI.
 * Do not reuse or modify IDs.
 */

enum dmub_gpint_command {
	DMUB_GPINT__INVALID_COMMAND = 0,
	DMUB_GPINT__GET_FW_VERSION = 1,
	DMUB_GPINT__STOP_FW = 2,
	DMUB_GPINT__GET_PSR_STATE = 7,
};

/**
 * Command responses.
 */

#define DMUB_GPINT__STOP_FW_RESPONSE 0xDEADDEAD

#endif /* _DMUB_GPINT_CMD_H_ */
