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

#include "i2c_sw_engine_dce110.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

/*
 * @brief
 * Cast 'struct i2c_sw_engine *'
 * to 'struct i2c_sw_engine_dce110 *'
 */
#define FROM_I2C_SW_ENGINE(ptr) \
	container_of((ptr), struct i2c_sw_engine_dce110, base)
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
	struct i2c_sw_engine_dce110 *engine)
{
	dal_i2c_sw_engine_destruct(&engine->base);
}

static void destroy(
	struct i2c_engine **engine)
{
	struct i2c_sw_engine_dce110 *sw_engine = FROM_I2C_ENGINE(*engine);

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
	struct i2c_sw_engine_dce110 *engine_dce110,
	const struct i2c_sw_engine_dce110_create_arg *arg_dce110)
{
	struct i2c_sw_engine_create_arg arg_base;

	arg_base.ctx = arg_dce110->ctx;
	arg_base.default_speed = arg_dce110->default_speed;

	if (!dal_i2c_sw_engine_construct(
			&engine_dce110->base, &arg_base)) {
		ASSERT_CRITICAL(false);
		return false;
	}

	/*struct engine   struct engine_funcs*/
	engine_dce110->base.base.base.funcs = &engine_funcs;
	/*struct i2c_engine  struct i2c_engine_funcs*/
	engine_dce110->base.base.funcs = &i2c_engine_funcs;
	engine_dce110->base.default_speed = arg_dce110->default_speed;
	engine_dce110->engine_id = arg_dce110->engine_id;

	return true;
}

struct i2c_engine *dal_i2c_sw_engine_dce110_create(
	const struct i2c_sw_engine_dce110_create_arg *arg)
{
	struct i2c_sw_engine_dce110 *engine_dce110;

	if (!arg) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	engine_dce110 = dm_alloc(sizeof(struct i2c_sw_engine_dce110));

	if (!engine_dce110) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	if (construct(engine_dce110, arg))
		return &engine_dce110->base.base;

	ASSERT_CRITICAL(false);

	dm_free(engine_dce110);

	return NULL;
}
