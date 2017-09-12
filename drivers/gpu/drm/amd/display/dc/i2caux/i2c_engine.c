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
#include "engine.h"

/*
 * Header of this unit
 */

#include "i2c_engine.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

#define FROM_ENGINE(ptr) \
	container_of((ptr), struct i2c_engine, base)

bool dal_i2c_engine_acquire(
	struct engine *engine,
	struct ddc *ddc_handle)
{
	struct i2c_engine *i2c_engine = FROM_ENGINE(engine);

	uint32_t counter = 0;
	bool result;

	do {
		result = i2c_engine->funcs->acquire_engine(
			i2c_engine, ddc_handle);

		if (result)
			break;

		/* i2c_engine is busy by VBios, lets wait and retry */

		udelay(10);

		++counter;
	} while (counter < 2);

	if (result) {
		if (!i2c_engine->funcs->setup_engine(i2c_engine)) {
			engine->funcs->release_engine(engine);
			result = false;
		}
	}

	return result;
}

bool dal_i2c_engine_setup_i2c_engine(
	struct i2c_engine *engine)
{
	/* Derivative classes do not have to override this */

	return true;
}

void dal_i2c_engine_submit_channel_request(
	struct i2c_engine *engine,
	struct i2c_request_transaction_data *request)
{

}

void dal_i2c_engine_process_channel_reply(
	struct i2c_engine *engine,
	struct i2c_reply_transaction_data *reply)
{

}

bool dal_i2c_engine_construct(
	struct i2c_engine *engine,
	struct dc_context *ctx)
{
	if (!dal_i2caux_construct_engine(&engine->base, ctx))
		return false;

	engine->timeout_delay = 0;
	return true;
}

void dal_i2c_engine_destruct(
	struct i2c_engine *engine)
{
	dal_i2caux_destruct_engine(&engine->base);
}
