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

#ifndef __DAL_I2C_SW_ENGINE_H__
#define __DAL_I2C_SW_ENGINE_H__

enum {
	I2C_SW_RETRIES = 10,
	I2C_SW_SCL_READ_RETRIES = 128,
	/* following value is in microseconds */
	I2C_SW_TIMEOUT_DELAY = 3000
};

struct i2c_sw_engine;

struct i2c_sw_engine {
	struct i2c_engine base;
	uint32_t clock_delay;
	/* Values below are in KHz */
	uint32_t speed;
	uint32_t default_speed;
};

struct i2c_sw_engine_create_arg {
	uint32_t default_speed;
	struct dc_context *ctx;
};

void dal_i2c_sw_engine_construct(
	struct i2c_sw_engine *engine,
	const struct i2c_sw_engine_create_arg *arg);

bool dal_i2caux_i2c_sw_engine_acquire_engine(
	struct i2c_engine *engine,
	struct ddc *ddc_handle);

void dal_i2c_sw_engine_destruct(
	struct i2c_sw_engine *engine);

struct i2c_engine *dal_i2c_sw_engine_create(
	const struct i2c_sw_engine_create_arg *arg);
enum i2caux_engine_type dal_i2c_sw_engine_get_engine_type(
	const struct engine *engine);
bool dal_i2c_sw_engine_submit_request(
	struct engine *ptr,
	struct i2caux_transaction_request *i2caux_request,
	bool middle_of_transaction);
uint32_t dal_i2c_sw_engine_get_speed(
	const struct i2c_engine *engine);
void dal_i2c_sw_engine_set_speed(
	struct i2c_engine *ptr,
	uint32_t speed);
void dal_i2c_sw_engine_submit_channel_request(
	struct i2c_engine *ptr,
	struct i2c_request_transaction_data *req);
enum i2c_channel_operation_result dal_i2c_sw_engine_get_channel_status(
	struct i2c_engine *engine,
	uint8_t *returned_bytes);
#endif
