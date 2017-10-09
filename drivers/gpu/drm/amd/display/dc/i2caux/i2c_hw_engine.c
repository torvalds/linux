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
#include "i2c_engine.h"

/*
 * Header of this unit
 */

#include "i2c_hw_engine.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

/*
 * @brief
 * Cast 'struct i2c_engine *'
 * to 'struct i2c_hw_engine *'
 */
#define FROM_I2C_ENGINE(ptr) \
	container_of((ptr), struct i2c_hw_engine, base)

/*
 * @brief
 * Cast 'struct engine *'
 * to 'struct i2c_hw_engine *'
 */
#define FROM_ENGINE(ptr) \
	FROM_I2C_ENGINE(container_of((ptr), struct i2c_engine, base))

enum i2caux_engine_type dal_i2c_hw_engine_get_engine_type(
	const struct engine *engine)
{
	return I2CAUX_ENGINE_TYPE_I2C_DDC_HW;
}

bool dal_i2c_hw_engine_submit_request(
	struct engine *engine,
	struct i2caux_transaction_request *i2caux_request,
	bool middle_of_transaction)
{
	struct i2c_hw_engine *hw_engine = FROM_ENGINE(engine);

	struct i2c_request_transaction_data request;

	uint32_t transaction_timeout;

	enum i2c_channel_operation_result operation_result;

	bool result = false;

	/* We need following:
	 * transaction length will not exceed
	 * the number of free bytes in HW buffer (minus one for address)*/

	if (i2caux_request->payload.length >=
		hw_engine->funcs->get_hw_buffer_available_size(hw_engine)) {
		i2caux_request->status =
			I2CAUX_TRANSACTION_STATUS_FAILED_BUFFER_OVERFLOW;
		return false;
	}

	if (i2caux_request->operation == I2CAUX_TRANSACTION_READ)
		request.action = middle_of_transaction ?
			I2CAUX_TRANSACTION_ACTION_I2C_READ_MOT :
			I2CAUX_TRANSACTION_ACTION_I2C_READ;
	else if (i2caux_request->operation == I2CAUX_TRANSACTION_WRITE)
		request.action = middle_of_transaction ?
			I2CAUX_TRANSACTION_ACTION_I2C_WRITE_MOT :
			I2CAUX_TRANSACTION_ACTION_I2C_WRITE;
	else {
		i2caux_request->status =
			I2CAUX_TRANSACTION_STATUS_FAILED_INVALID_OPERATION;
		/* [anaumov] in DAL2, there was no "return false" */
		return false;
	}

	request.address = (uint8_t)i2caux_request->payload.address;
	request.length = i2caux_request->payload.length;
	request.data = i2caux_request->payload.data;

	/* obtain timeout value before submitting request */

	transaction_timeout = hw_engine->funcs->get_transaction_timeout(
		hw_engine, i2caux_request->payload.length + 1);

	hw_engine->base.funcs->submit_channel_request(
		&hw_engine->base, &request);

	if ((request.status == I2C_CHANNEL_OPERATION_FAILED) ||
		(request.status == I2C_CHANNEL_OPERATION_ENGINE_BUSY)) {
		i2caux_request->status =
			I2CAUX_TRANSACTION_STATUS_FAILED_CHANNEL_BUSY;
		return false;
	}

	/* wait until transaction proceed */

	operation_result = hw_engine->funcs->wait_on_operation_result(
		hw_engine,
		transaction_timeout,
		I2C_CHANNEL_OPERATION_ENGINE_BUSY);

	/* update transaction status */

	switch (operation_result) {
	case I2C_CHANNEL_OPERATION_SUCCEEDED:
		i2caux_request->status =
			I2CAUX_TRANSACTION_STATUS_SUCCEEDED;
		result = true;
	break;
	case I2C_CHANNEL_OPERATION_NO_RESPONSE:
		i2caux_request->status =
			I2CAUX_TRANSACTION_STATUS_FAILED_NACK;
	break;
	case I2C_CHANNEL_OPERATION_TIMEOUT:
		i2caux_request->status =
			I2CAUX_TRANSACTION_STATUS_FAILED_TIMEOUT;
	break;
	case I2C_CHANNEL_OPERATION_FAILED:
		i2caux_request->status =
			I2CAUX_TRANSACTION_STATUS_FAILED_INCOMPLETE;
	break;
	default:
		i2caux_request->status =
			I2CAUX_TRANSACTION_STATUS_FAILED_OPERATION;
	}

	if (result && (i2caux_request->operation == I2CAUX_TRANSACTION_READ)) {
		struct i2c_reply_transaction_data reply;

		reply.data = i2caux_request->payload.data;
		reply.length = i2caux_request->payload.length;

		hw_engine->base.funcs->
			process_channel_reply(&hw_engine->base, &reply);
	}

	return result;
}

bool dal_i2c_hw_engine_acquire_engine(
	struct i2c_engine *engine,
	struct ddc *ddc)
{
	enum gpio_result result;
	uint32_t current_speed;

	result = dal_ddc_open(ddc, GPIO_MODE_HARDWARE,
		GPIO_DDC_CONFIG_TYPE_MODE_I2C);

	if (result != GPIO_RESULT_OK)
		return false;

	engine->base.ddc = ddc;

	current_speed = engine->funcs->get_speed(engine);

	if (current_speed)
		FROM_I2C_ENGINE(engine)->original_speed = current_speed;

	return true;
}
/*
 * @brief
 * Queries in a loop for current engine status
 * until retrieved status matches 'expected_result', or timeout occurs.
 * Timeout given in microseconds
 * and the status query frequency is also one per microsecond.
 */
enum i2c_channel_operation_result dal_i2c_hw_engine_wait_on_operation_result(
	struct i2c_hw_engine *engine,
	uint32_t timeout,
	enum i2c_channel_operation_result expected_result)
{
	enum i2c_channel_operation_result result;
	uint32_t i = 0;

	if (!timeout)
		return I2C_CHANNEL_OPERATION_SUCCEEDED;

	do {
		result = engine->base.funcs->get_channel_status(
			&engine->base, NULL);

		if (result != expected_result)
			break;

		udelay(1);

		++i;
	} while (i < timeout);

	return result;
}

void dal_i2c_hw_engine_construct(
	struct i2c_hw_engine *engine,
	struct dc_context *ctx)
{
	dal_i2c_engine_construct(&engine->base, ctx);
	engine->original_speed = I2CAUX_DEFAULT_I2C_HW_SPEED;
	engine->default_speed = I2CAUX_DEFAULT_I2C_HW_SPEED;
}

void dal_i2c_hw_engine_destruct(
	struct i2c_hw_engine *engine)
{
	dal_i2c_engine_destruct(&engine->base);
}
