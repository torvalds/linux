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

#ifndef __DAL_I2C_HW_ENGINE_H__
#define __DAL_I2C_HW_ENGINE_H__

enum {
	TRANSACTION_TIMEOUT_IN_I2C_CLOCKS = 32
};

struct i2c_hw_engine;

struct i2c_hw_engine_funcs {
	uint32_t (*get_hw_buffer_available_size)(
		const struct i2c_hw_engine *engine);
	enum i2c_channel_operation_result (*wait_on_operation_result)(
		struct i2c_hw_engine *engine,
		uint32_t timeout,
		enum i2c_channel_operation_result expected_result);
	uint32_t (*get_transaction_timeout)(
		const struct i2c_hw_engine *engine,
		uint32_t length);
};

struct i2c_hw_engine {
	struct i2c_engine base;
	const struct i2c_hw_engine_funcs *funcs;

	/* Values below are in kilohertz */
	uint32_t original_speed;
	uint32_t default_speed;
};

bool dal_i2c_hw_engine_construct(
	struct i2c_hw_engine *engine,
	struct dc_context *ctx);

void dal_i2c_hw_engine_destruct(
	struct i2c_hw_engine *engine);

enum i2c_channel_operation_result dal_i2c_hw_engine_wait_on_operation_result(
	struct i2c_hw_engine *engine,
	uint32_t timeout,
	enum i2c_channel_operation_result expected_result);

bool dal_i2c_hw_engine_acquire_engine(
	struct i2c_engine *engine,
	struct ddc *ddc);

bool dal_i2c_hw_engine_submit_request(
	struct engine *ptr,
	struct i2caux_transaction_request *i2caux_request,
	bool middle_of_transaction);

enum i2caux_engine_type dal_i2c_hw_engine_get_engine_type(
	const struct engine *engine);

#endif
