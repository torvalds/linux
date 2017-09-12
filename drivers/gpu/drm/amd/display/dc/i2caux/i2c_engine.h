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

#ifndef __DAL_I2C_ENGINE_H__
#define __DAL_I2C_ENGINE_H__

enum i2c_channel_operation_result {
	I2C_CHANNEL_OPERATION_SUCCEEDED,
	I2C_CHANNEL_OPERATION_FAILED,
	I2C_CHANNEL_OPERATION_NOT_GRANTED,
	I2C_CHANNEL_OPERATION_IS_BUSY,
	I2C_CHANNEL_OPERATION_NO_HANDLE_PROVIDED,
	I2C_CHANNEL_OPERATION_CHANNEL_IN_USE,
	I2C_CHANNEL_OPERATION_CHANNEL_CLIENT_MAX_ALLOWED,
	I2C_CHANNEL_OPERATION_ENGINE_BUSY,
	I2C_CHANNEL_OPERATION_TIMEOUT,
	I2C_CHANNEL_OPERATION_NO_RESPONSE,
	I2C_CHANNEL_OPERATION_HW_REQUEST_I2C_BUS,
	I2C_CHANNEL_OPERATION_WRONG_PARAMETER,
	I2C_CHANNEL_OPERATION_OUT_NB_OF_RETRIES,
	I2C_CHANNEL_OPERATION_NOT_STARTED
};

struct i2c_request_transaction_data {
	enum i2caux_transaction_action action;
	enum i2c_channel_operation_result status;
	uint8_t address;
	uint32_t length;
	uint8_t *data;
};

struct i2c_reply_transaction_data {
	uint32_t length;
	uint8_t *data;
};

struct i2c_engine;

struct i2c_engine_funcs {
	void (*destroy)(
		struct i2c_engine **ptr);
	uint32_t (*get_speed)(
		const struct i2c_engine *engine);
	void (*set_speed)(
		struct i2c_engine *engine,
		uint32_t speed);
	bool (*acquire_engine)(
		struct i2c_engine *engine,
		struct ddc *ddc);
	bool (*setup_engine)(
		struct i2c_engine *engine);
	void (*submit_channel_request)(
		struct i2c_engine *engine,
		struct i2c_request_transaction_data *request);
	void (*process_channel_reply)(
		struct i2c_engine *engine,
		struct i2c_reply_transaction_data *reply);
	enum i2c_channel_operation_result (*get_channel_status)(
		struct i2c_engine *engine,
		uint8_t *returned_bytes);
};

struct i2c_engine {
	struct engine base;
	const struct i2c_engine_funcs *funcs;
	uint32_t timeout_delay;
};

bool dal_i2c_engine_construct(
	struct i2c_engine *engine,
	struct dc_context *ctx);

void dal_i2c_engine_destruct(
	struct i2c_engine *engine);

bool dal_i2c_engine_setup_i2c_engine(
	struct i2c_engine *engine);

void dal_i2c_engine_submit_channel_request(
	struct i2c_engine *engine,
	struct i2c_request_transaction_data *request);

void dal_i2c_engine_process_channel_reply(
	struct i2c_engine *engine,
	struct i2c_reply_transaction_data *reply);

bool dal_i2c_engine_acquire(
	struct engine *ptr,
	struct ddc *ddc_handle);

#endif
