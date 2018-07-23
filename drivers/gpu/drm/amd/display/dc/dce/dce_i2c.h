/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef __DCE_I2C_H__
#define __DCE_I2C_H__

#include "inc/core_types.h"
#include "dce_i2c_hw.h"
#include "dce_i2c_sw.h"

enum dce_i2c_transaction_status {
	DCE_I2C_TRANSACTION_STATUS_UNKNOWN = (-1L),
	DCE_I2C_TRANSACTION_STATUS_SUCCEEDED,
	DCE_I2C_TRANSACTION_STATUS_FAILED_CHANNEL_BUSY,
	DCE_I2C_TRANSACTION_STATUS_FAILED_TIMEOUT,
	DCE_I2C_TRANSACTION_STATUS_FAILED_PROTOCOL_ERROR,
	DCE_I2C_TRANSACTION_STATUS_FAILED_NACK,
	DCE_I2C_TRANSACTION_STATUS_FAILED_INCOMPLETE,
	DCE_I2C_TRANSACTION_STATUS_FAILED_OPERATION,
	DCE_I2C_TRANSACTION_STATUS_FAILED_INVALID_OPERATION,
	DCE_I2C_TRANSACTION_STATUS_FAILED_BUFFER_OVERFLOW,
	DCE_I2C_TRANSACTION_STATUS_FAILED_HPD_DISCON
};

enum dce_i2c_transaction_operation {
	DCE_I2C_TRANSACTION_READ,
	DCE_I2C_TRANSACTION_WRITE
};

struct dce_i2c_transaction_payload {
	enum dce_i2c_transaction_address_space address_space;
	uint32_t address;
	uint32_t length;
	uint8_t *data;
};

struct dce_i2c_transaction_request {
	enum dce_i2c_transaction_operation operation;
	struct dce_i2c_transaction_payload payload;
	enum dce_i2c_transaction_status status;
};


bool dce_i2c_submit_command(
	struct resource_pool *pool,
	struct ddc *ddc,
	struct i2c_command *cmd);

#endif
