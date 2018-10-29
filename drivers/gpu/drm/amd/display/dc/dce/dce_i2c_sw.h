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

#ifndef __DCE_I2C_SW_H__
#define __DCE_I2C_SW_H__

enum {
	DCE_I2C_DEFAULT_I2C_SW_SPEED = 50,
	I2C_SW_RETRIES = 10,
	I2C_SW_TIMEOUT_DELAY = 3000,
};

struct dce_i2c_sw {
	struct ddc *ddc;
	struct dc_context *ctx;
	uint32_t clock_delay;
	uint32_t speed;
};

void dce_i2c_sw_construct(
	struct dce_i2c_sw *dce_i2c_sw,
	struct dc_context *ctx);

bool dce_i2c_submit_command_sw(
	struct resource_pool *pool,
	struct ddc *ddc,
	struct i2c_command *cmd,
	struct dce_i2c_sw *dce_i2c_sw);

struct dce_i2c_sw *dce_i2c_acquire_i2c_sw_engine(
	struct resource_pool *pool,
	struct ddc *ddc);

#endif

