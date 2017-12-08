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

#ifndef __DAL_I2C_HW_ENGINE_DCE80_H__
#define __DAL_I2C_HW_ENGINE_DCE80_H__

struct i2c_hw_engine_dce80 {
	struct i2c_hw_engine base;
	struct {
		uint32_t DC_I2C_DDCX_SETUP;
		uint32_t DC_I2C_DDCX_SPEED;
	} addr;
	uint32_t engine_id;
	/* expressed in kilohertz */
	uint32_t reference_frequency;
	/* number of bytes currently used in HW buffer */
	uint32_t buffer_used_bytes;
	/* number of pending transactions (before GO) */
	uint32_t transaction_count;
	uint32_t engine_keep_power_up_count;
};

struct i2c_hw_engine_dce80_create_arg {
	uint32_t engine_id;
	uint32_t reference_frequency;
	uint32_t default_speed;
	struct dc_context *ctx;
};

struct i2c_engine *dal_i2c_hw_engine_dce80_create(
	const struct i2c_hw_engine_dce80_create_arg *arg);
#endif
