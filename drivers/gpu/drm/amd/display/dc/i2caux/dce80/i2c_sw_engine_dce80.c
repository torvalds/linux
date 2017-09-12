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

#include "dm_services.h"

/*
 * Pre-requisites: headers required by header of this unit
 */
#include "include/i2caux_interface.h"
#include "../engine.h"
#include "../i2c_engine.h"
#include "../i2c_sw_engine.h"

/*
 * Header of this unit
 */

#include "i2c_sw_engine_dce80.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"

/*
 * This unit
 */

static const uint32_t ddc_hw_status_addr[] = {
	mmDC_I2C_DDC1_HW_STATUS,
	mmDC_I2C_DDC2_HW_STATUS,
	mmDC_I2C_DDC3_HW_STATUS,
	mmDC_I2C_DDC4_HW_STATUS,
	mmDC_I2C_DDC5_HW_STATUS,
	mmDC_I2C_DDC6_HW_STATUS,
	mmDC_I2C_DDCVGA_HW_STATUS
};

/*
 * @brief
 * Cast 'struct i2c_sw_engine *'
 * to 'struct i2c_sw_engine_dce80 *'
 */
#define FROM_I2C_SW_ENGINE(ptr) \
	container_of((ptr), struct i2c_sw_engine_dce80, base)

/*
 * @brief
 * Cast 'struct i2c_engine *'
 * to 'struct i2c_sw_engine_dce80 *'
 */
#define FROM_I2C_ENGINE(ptr) \
	FROM_I2C_SW_ENGINE(container_of((ptr), struct i2c_sw_engine, base))

/*
 * @brief
 * Cast 'struct engine *'
 * to 'struct i2c_sw_engine_dce80 *'
 */
#define FROM_ENGINE(ptr) \
	FROM_I2C_ENGINE(container_of((ptr), struct i2c_engine, base))

static void release_engine(
	struct engine *engine)
{

}

static void destruct(
	struct i2c_sw_engine_dce80 *engine)
{
	dal_i2c_sw_engine_destruct(&engine->base);
}

static void destroy(
	struct i2c_engine **engine)
{
	struct i2c_sw_engine_dce80 *sw_engine = FROM_I2C_ENGINE(*engine);

	destruct(sw_engine);

	dm_free(sw_engine);

	*engine = NULL;
}

static bool acquire_engine(
	struct i2c_engine *engine,
	struct ddc *ddc_handle)
{
	return dal_i2caux_i2c_sw_engine_acquire_engine(engine, ddc_handle);
}

static const struct i2c_engine_funcs i2c_engine_funcs = {
	.acquire_engine = acquire_engine,
	.destroy = destroy,
	.get_speed = dal_i2c_sw_engine_get_speed,
	.set_speed = dal_i2c_sw_engine_set_speed,
	.setup_engine = dal_i2c_engine_setup_i2c_engine,
	.submit_channel_request = dal_i2c_sw_engine_submit_channel_request,
	.process_channel_reply = dal_i2c_engine_process_channel_reply,
	.get_channel_status = dal_i2c_sw_engine_get_channel_status,
};

static const struct engine_funcs engine_funcs = {
	.release_engine = release_engine,
	.get_engine_type = dal_i2c_sw_engine_get_engine_type,
	.acquire = dal_i2c_engine_acquire,
	.submit_request = dal_i2c_sw_engine_submit_request,
};

static bool construct(
	struct i2c_sw_engine_dce80 *engine,
	const struct i2c_sw_engine_dce80_create_arg *arg)
{
	struct i2c_sw_engine_create_arg arg_base;

	arg_base.ctx = arg->ctx;
	arg_base.default_speed = arg->default_speed;

	if (!dal_i2c_sw_engine_construct(&engine->base, &arg_base)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	engine->base.base.base.funcs = &engine_funcs;
	engine->base.base.funcs = &i2c_engine_funcs;
	engine->base.default_speed = arg->default_speed;
	engine->engine_id = arg->engine_id;

	return true;
}

struct i2c_engine *dal_i2c_sw_engine_dce80_create(
	const struct i2c_sw_engine_dce80_create_arg *arg)
{
	struct i2c_sw_engine_dce80 *engine;

	if (!arg) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	engine = dm_alloc(sizeof(struct i2c_sw_engine_dce80));

	if (!engine) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (construct(engine, arg))
		return &engine->base.base;

	BREAK_TO_DEBUGGER();

	dm_free(engine);

	return NULL;
}

