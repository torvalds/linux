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

#ifndef __DAL_I2C_AUX_H__
#define __DAL_I2C_AUX_H__

uint32_t dal_i2caux_get_reference_clock(
	struct dc_bios *bios);

struct i2caux;

struct engine;

struct i2caux_funcs {
	void (*destroy)(struct i2caux **ptr);
	struct i2c_engine * (*acquire_i2c_sw_engine)(
		struct i2caux *i2caux,
		struct ddc *ddc);
	struct i2c_engine * (*acquire_i2c_hw_engine)(
		struct i2caux *i2caux,
		struct ddc *ddc);
	struct aux_engine * (*acquire_aux_engine)(
		struct i2caux *i2caux,
		struct ddc *ddc);
	void (*release_engine)(
		struct i2caux *i2caux,
		struct engine *engine);
};

struct i2c_engine;
struct aux_engine;

struct i2caux {
	struct dc_context *ctx;
	const struct i2caux_funcs *funcs;
	/* On ASIC we have certain amount of lines with HW DDC engine
	 * (4, 6, or maybe more in the future).
	 * For every such line, we create separate HW DDC engine
	 * (since we have these engines in HW) and separate SW DDC engine
	 * (to allow concurrent use of few lines).
	 * In similar way we have AUX engines. */

	/* I2C SW engines, per DDC line.
	 * Only lines with HW DDC support will be initialized */
	struct i2c_engine *i2c_sw_engines[GPIO_DDC_LINE_COUNT];

	/* I2C HW engines, per DDC line.
	 * Only lines with HW DDC support will be initialized */
	struct i2c_engine *i2c_hw_engines[GPIO_DDC_LINE_COUNT];

	/* AUX engines, per DDC line.
	 * Only lines with HW AUX support will be initialized */
	struct aux_engine *aux_engines[GPIO_DDC_LINE_COUNT];

	/* For all other lines, we can use
	 * single instance of generic I2C HW engine
	 * (since in HW, there is single instance of it)
	 * or single instance of generic I2C SW engine.
	 * AUX is not supported for other lines. */

	/* General-purpose I2C SW engine.
	 * Can be assigned dynamically to any line per transaction */
	struct i2c_engine *i2c_generic_sw_engine;

	/* General-purpose I2C generic HW engine.
	 * Can be assigned dynamically to almost any line per transaction */
	struct i2c_engine *i2c_generic_hw_engine;

	/* [anaumov] in DAL2, there is a Mutex */

	uint32_t aux_timeout_period;

	/* expressed in KHz */
	uint32_t default_i2c_sw_speed;
	uint32_t default_i2c_hw_speed;
};

void dal_i2caux_construct(
	struct i2caux *i2caux,
	struct dc_context *ctx);

void dal_i2caux_release_engine(
	struct i2caux *i2caux,
	struct engine *engine);

void dal_i2caux_destruct(
	struct i2caux *i2caux);

void dal_i2caux_destroy(
	struct i2caux **ptr);

struct i2c_engine *dal_i2caux_acquire_i2c_sw_engine(
	struct i2caux *i2caux,
	struct ddc *ddc);

struct aux_engine *dal_i2caux_acquire_aux_engine(
	struct i2caux *i2caux,
	struct ddc *ddc);

#endif
