/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DAL_ENGINE_H__
#define __DAL_ENGINE_H__

#include "dc_ddc_types.h"

enum i2caux_transaction_operation {
	I2CAUX_TRANSACTION_READ,
	I2CAUX_TRANSACTION_WRITE
};

enum i2caux_transaction_address_space {
	I2CAUX_TRANSACTION_ADDRESS_SPACE_I2C = 1,
	I2CAUX_TRANSACTION_ADDRESS_SPACE_DPCD
};

struct i2caux_transaction_payload {
	enum i2caux_transaction_address_space address_space;
	uint32_t address;
	uint32_t length;
	uint8_t *data;
};

enum i2caux_transaction_status {
	I2CAUX_TRANSACTION_STATUS_UNKNOWN = (-1L),
	I2CAUX_TRANSACTION_STATUS_SUCCEEDED,
	I2CAUX_TRANSACTION_STATUS_FAILED_CHANNEL_BUSY,
	I2CAUX_TRANSACTION_STATUS_FAILED_TIMEOUT,
	I2CAUX_TRANSACTION_STATUS_FAILED_PROTOCOL_ERROR,
	I2CAUX_TRANSACTION_STATUS_FAILED_NACK,
	I2CAUX_TRANSACTION_STATUS_FAILED_INCOMPLETE,
	I2CAUX_TRANSACTION_STATUS_FAILED_OPERATION,
	I2CAUX_TRANSACTION_STATUS_FAILED_INVALID_OPERATION,
	I2CAUX_TRANSACTION_STATUS_FAILED_BUFFER_OVERFLOW,
	I2CAUX_TRANSACTION_STATUS_FAILED_HPD_DISCON
};

struct i2caux_transaction_request {
	enum i2caux_transaction_operation operation;
	struct i2caux_transaction_payload payload;
	enum i2caux_transaction_status status;
};

enum i2caux_engine_type {
	I2CAUX_ENGINE_TYPE_UNKNOWN = (-1L),
	I2CAUX_ENGINE_TYPE_AUX,
	I2CAUX_ENGINE_TYPE_I2C_DDC_HW,
	I2CAUX_ENGINE_TYPE_I2C_GENERIC_HW,
	I2CAUX_ENGINE_TYPE_I2C_SW
};

enum i2c_default_speed {
	I2CAUX_DEFAULT_I2C_HW_SPEED = 50,
	I2CAUX_DEFAULT_I2C_SW_SPEED = 50
};

struct engine;

struct engine_funcs {
	enum i2caux_engine_type (*get_engine_type)(
		const struct engine *engine);
	struct aux_engine* (*acquire)(
		struct engine *engine,
		struct ddc *ddc);
	bool (*submit_request)(
		struct engine *engine,
		struct i2caux_transaction_request *request,
		bool middle_of_transaction);
	void (*release_engine)(
		struct engine *engine);
	void (*destroy_engine)(
		struct engine **engine);
};

struct engine {
	const struct engine_funcs *funcs;
	uint32_t inst;
	struct ddc *ddc;
	struct dc_context *ctx;
};

#endif
