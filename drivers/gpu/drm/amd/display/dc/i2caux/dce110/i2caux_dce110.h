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

#ifndef __DAL_I2C_AUX_DCE110_H__
#define __DAL_I2C_AUX_DCE110_H__

#include "../i2caux.h"

struct i2caux_dce110 {
	struct i2caux base;
	/* indicate the I2C HW circular buffer is in use */
	bool i2c_hw_buffer_in_use;
};

struct dce110_aux_registers;
struct dce110_i2c_hw_engine_registers;
struct dce110_i2c_hw_engine_shift;
struct dce110_i2c_hw_engine_mask;

struct i2caux *dal_i2caux_dce110_create(
	struct dc_context *ctx);

void dal_i2caux_dce110_construct(
	struct i2caux_dce110 *i2caux_dce110,
	struct dc_context *ctx,
	const struct dce110_aux_registers *aux_regs,
	const struct dce110_i2c_hw_engine_registers *i2c_hw_engine_regs,
	const struct dce110_i2c_hw_engine_shift *i2c_shift,
	const struct dce110_i2c_hw_engine_mask *i2c_mask);

#endif /* __DAL_I2C_AUX_DCE110_H__ */
