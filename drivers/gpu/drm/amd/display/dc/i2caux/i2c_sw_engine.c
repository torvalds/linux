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

#include "i2c_sw_engine.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

#define SCL false
#define SDA true

static inline bool read_bit_from_ddc(
	struct ddc *ddc,
	bool data_nor_clock)
{
	uint32_t value = 0;

	if (data_nor_clock)
		dal_gpio_get_value(ddc->pin_data, &value);
	else
		dal_gpio_get_value(ddc->pin_clock, &value);

	return (value != 0);
}

static inline void write_bit_to_ddc(
	struct ddc *ddc,
	bool data_nor_clock,
	bool bit)
{
	uint32_t value = bit ? 1 : 0;

	if (data_nor_clock)
		dal_gpio_set_value(ddc->pin_data, value);
	else
		dal_gpio_set_value(ddc->pin_clock, value);
}

static bool wait_for_scl_high(
	struct dc_context *ctx,
	struct ddc *ddc,
	uint16_t clock_delay_div_4)
{
	uint32_t scl_retry = 0;
	uint32_t scl_retry_max = I2C_SW_TIMEOUT_DELAY / clock_delay_div_4;

	udelay(clock_delay_div_4);

	/* 3 milliseconds delay
	 * to wake up some displays from "low power" state.
	 */

	do {
		if (read_bit_from_ddc(ddc, SCL))
			return true;

		udelay(clock_delay_div_4);

		++scl_retry;
	} while (scl_retry <= scl_retry_max);

	return false;
}

static bool start_sync(
	struct dc_context *ctx,
	struct ddc *ddc_handle,
	uint16_t clock_delay_div_4)
{
	uint32_t retry = 0;

	/* The I2C communications start signal is:
	 * the SDA going low from high, while the SCL is high. */

	write_bit_to_ddc(ddc_handle, SCL, true);

	udelay(clock_delay_div_4);

	do {
		write_bit_to_ddc(ddc_handle, SDA, true);

		if (!read_bit_from_ddc(ddc_handle, SDA)) {
			++retry;
			continue;
		}

		udelay(clock_delay_div_4);

		write_bit_to_ddc(ddc_handle, SCL, true);

		if (!wait_for_scl_high(ctx, ddc_handle, clock_delay_div_4))
			break;

		write_bit_to_ddc(ddc_handle, SDA, false);

		udelay(clock_delay_div_4);

		write_bit_to_ddc(ddc_handle, SCL, false);

		udelay(clock_delay_div_4);

		return true;
	} while (retry <= I2C_SW_RETRIES);

	return false;
}

static bool stop_sync(
	struct dc_context *ctx,
	struct ddc *ddc_handle,
	uint16_t clock_delay_div_4)
{
	uint32_t retry = 0;

	/* The I2C communications stop signal is:
	 * the SDA going high from low, while the SCL is high. */

	write_bit_to_ddc(ddc_handle, SCL, false);

	udelay(clock_delay_div_4);

	write_bit_to_ddc(ddc_handle, SDA, false);

	udelay(clock_delay_div_4);

	write_bit_to_ddc(ddc_handle, SCL, true);

	if (!wait_for_scl_high(ctx, ddc_handle, clock_delay_div_4))
		return false;

	write_bit_to_ddc(ddc_handle, SDA, true);

	do {
		udelay(clock_delay_div_4);

		if (read_bit_from_ddc(ddc_handle, SDA))
			return true;

		++retry;
	} while (retry <= 2);

	return false;
}

static bool write_byte(
	struct dc_context *ctx,
	struct ddc *ddc_handle,
	uint16_t clock_delay_div_4,
	uint8_t byte)
{
	int32_t shift = 7;
	bool ack;

	/* bits are transmitted serially, starting from MSB */

	do {
		udelay(clock_delay_div_4);

		write_bit_to_ddc(ddc_handle, SDA, (byte >> shift) & 1);

		udelay(clock_delay_div_4);

		write_bit_to_ddc(ddc_handle, SCL, true);

		if (!wait_for_scl_high(ctx, ddc_handle, clock_delay_div_4))
			return false;

		write_bit_to_ddc(ddc_handle, SCL, false);

		--shift;
	} while (shift >= 0);

	/* The display sends ACK by preventing the SDA from going high
	 * after the SCL pulse we use to send our last data bit.
	 * If the SDA goes high after that bit, it's a NACK */

	udelay(clock_delay_div_4);

	write_bit_to_ddc(ddc_handle, SDA, true);

	udelay(clock_delay_div_4);

	write_bit_to_ddc(ddc_handle, SCL, true);

	if (!wait_for_scl_high(ctx, ddc_handle, clock_delay_div_4))
		return false;

	/* read ACK bit */

	ack = !read_bit_from_ddc(ddc_handle, SDA);

	udelay(clock_delay_div_4 << 1);

	write_bit_to_ddc(ddc_handle, SCL, false);

	udelay(clock_delay_div_4 << 1);

	return ack;
}

static bool read_byte(
	struct dc_context *ctx,
	struct ddc *ddc_handle,
	uint16_t clock_delay_div_4,
	uint8_t *byte,
	bool more)
{
	int32_t shift = 7;

	uint8_t data = 0;

	/* The data bits are read from MSB to LSB;
	 * bit is read while SCL is high */

	do {
		write_bit_to_ddc(ddc_handle, SCL, true);

		if (!wait_for_scl_high(ctx, ddc_handle, clock_delay_div_4))
			return false;

		if (read_bit_from_ddc(ddc_handle, SDA))
			data |= (1 << shift);

		write_bit_to_ddc(ddc_handle, SCL, false);

		udelay(clock_delay_div_4 << 1);

		--shift;
	} while (shift >= 0);

	/* read only whole byte */

	*byte = data;

	udelay(clock_delay_div_4);

	/* send the acknowledge bit:
	 * SDA low means ACK, SDA high means NACK */

	write_bit_to_ddc(ddc_handle, SDA, !more);

	udelay(clock_delay_div_4);

	write_bit_to_ddc(ddc_handle, SCL, true);

	if (!wait_for_scl_high(ctx, ddc_handle, clock_delay_div_4))
		return false;

	write_bit_to_ddc(ddc_handle, SCL, false);

	udelay(clock_delay_div_4);

	write_bit_to_ddc(ddc_handle, SDA, true);

	udelay(clock_delay_div_4);

	return true;
}

static bool i2c_write(
	struct dc_context *ctx,
	struct ddc *ddc_handle,
	uint16_t clock_delay_div_4,
	uint8_t address,
	uint32_t length,
	const uint8_t *data)
{
	uint32_t i = 0;

	if (!write_byte(ctx, ddc_handle, clock_delay_div_4, address))
		return false;

	while (i < length) {
		if (!write_byte(ctx, ddc_handle, clock_delay_div_4, data[i]))
			return false;
		++i;
	}

	return true;
}

static bool i2c_read(
	struct dc_context *ctx,
	struct ddc *ddc_handle,
	uint16_t clock_delay_div_4,
	uint8_t address,
	uint32_t length,
	uint8_t *data)
{
	uint32_t i = 0;

	if (!write_byte(ctx, ddc_handle, clock_delay_div_4, address))
		return false;

	while (i < length) {
		if (!read_byte(ctx, ddc_handle, clock_delay_div_4, data + i,
			i < length - 1))
			return false;
		++i;
	}

	return true;
}

/*
 * @brief
 * Cast 'struct i2c_engine *'
 * to 'struct i2c_sw_engine *'
 */
#define FROM_I2C_ENGINE(ptr) \
	container_of((ptr), struct i2c_sw_engine, base)

/*
 * @brief
 * Cast 'struct engine *'
 * to 'struct i2c_sw_engine *'
 */
#define FROM_ENGINE(ptr) \
	FROM_I2C_ENGINE(container_of((ptr), struct i2c_engine, base))

enum i2caux_engine_type dal_i2c_sw_engine_get_engine_type(
	const struct engine *engine)
{
	return I2CAUX_ENGINE_TYPE_I2C_SW;
}

bool dal_i2c_sw_engine_submit_request(
	struct engine *engine,
	struct i2caux_transaction_request *i2caux_request,
	bool middle_of_transaction)
{
	struct i2c_sw_engine *sw_engine = FROM_ENGINE(engine);

	struct i2c_engine *base = &sw_engine->base;

	struct i2c_request_transaction_data request;
	bool operation_succeeded = false;

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
		/* in DAL2, there was no "return false" */
		return false;
	}

	request.address = (uint8_t)i2caux_request->payload.address;
	request.length = i2caux_request->payload.length;
	request.data = i2caux_request->payload.data;

	base->funcs->submit_channel_request(base, &request);

	if ((request.status == I2C_CHANNEL_OPERATION_ENGINE_BUSY) ||
		(request.status == I2C_CHANNEL_OPERATION_FAILED))
		i2caux_request->status =
			I2CAUX_TRANSACTION_STATUS_FAILED_CHANNEL_BUSY;
	else {
		enum i2c_channel_operation_result operation_result;

		do {
			operation_result =
				base->funcs->get_channel_status(base, NULL);

			switch (operation_result) {
			case I2C_CHANNEL_OPERATION_SUCCEEDED:
				i2caux_request->status =
					I2CAUX_TRANSACTION_STATUS_SUCCEEDED;
				operation_succeeded = true;
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
			break;
			}
		} while (operation_result == I2C_CHANNEL_OPERATION_ENGINE_BUSY);
	}

	return operation_succeeded;
}

uint32_t dal_i2c_sw_engine_get_speed(
	const struct i2c_engine *engine)
{
	return FROM_I2C_ENGINE(engine)->speed;
}

void dal_i2c_sw_engine_set_speed(
	struct i2c_engine *engine,
	uint32_t speed)
{
	struct i2c_sw_engine *sw_engine = FROM_I2C_ENGINE(engine);

	ASSERT(speed);

	sw_engine->speed = speed ? speed : I2CAUX_DEFAULT_I2C_SW_SPEED;

	sw_engine->clock_delay = 1000 / sw_engine->speed;

	if (sw_engine->clock_delay < 12)
		sw_engine->clock_delay = 12;
}

bool dal_i2caux_i2c_sw_engine_acquire_engine(
	struct i2c_engine *engine,
	struct ddc *ddc)
{
	enum gpio_result result;

	result = dal_ddc_open(ddc, GPIO_MODE_FAST_OUTPUT,
		GPIO_DDC_CONFIG_TYPE_MODE_I2C);

	if (result != GPIO_RESULT_OK)
		return false;

	engine->base.ddc = ddc;

	return true;
}

void dal_i2c_sw_engine_submit_channel_request(
	struct i2c_engine *engine,
	struct i2c_request_transaction_data *req)
{
	struct i2c_sw_engine *sw_engine = FROM_I2C_ENGINE(engine);

	struct ddc *ddc = engine->base.ddc;
	uint16_t clock_delay_div_4 = sw_engine->clock_delay >> 2;

	/* send sync (start / repeated start) */

	bool result = start_sync(engine->base.ctx, ddc, clock_delay_div_4);

	/* process payload */

	if (result) {
		switch (req->action) {
		case I2CAUX_TRANSACTION_ACTION_I2C_WRITE:
		case I2CAUX_TRANSACTION_ACTION_I2C_WRITE_MOT:
			result = i2c_write(engine->base.ctx, ddc, clock_delay_div_4,
				req->address, req->length, req->data);
		break;
		case I2CAUX_TRANSACTION_ACTION_I2C_READ:
		case I2CAUX_TRANSACTION_ACTION_I2C_READ_MOT:
			result = i2c_read(engine->base.ctx, ddc, clock_delay_div_4,
				req->address, req->length, req->data);
		break;
		default:
			result = false;
		break;
		}
	}

	/* send stop if not 'mot' or operation failed */

	if (!result ||
		(req->action == I2CAUX_TRANSACTION_ACTION_I2C_WRITE) ||
		(req->action == I2CAUX_TRANSACTION_ACTION_I2C_READ))
		if (!stop_sync(engine->base.ctx, ddc, clock_delay_div_4))
			result = false;

	req->status = result ?
		I2C_CHANNEL_OPERATION_SUCCEEDED :
		I2C_CHANNEL_OPERATION_FAILED;
}

enum i2c_channel_operation_result dal_i2c_sw_engine_get_channel_status(
	struct i2c_engine *engine,
	uint8_t *returned_bytes)
{
	/* No arbitration with VBIOS is performed since DCE 6.0 */
	return I2C_CHANNEL_OPERATION_SUCCEEDED;
}

void dal_i2c_sw_engine_destruct(
	struct i2c_sw_engine *engine)
{
	dal_i2c_engine_destruct(&engine->base);
}

static void destroy(
	struct i2c_engine **ptr)
{
	dal_i2c_sw_engine_destruct(FROM_I2C_ENGINE(*ptr));

	dm_free(*ptr);
	*ptr = NULL;
}

static const struct i2c_engine_funcs i2c_engine_funcs = {
	.acquire_engine = dal_i2caux_i2c_sw_engine_acquire_engine,
	.destroy = destroy,
	.get_speed = dal_i2c_sw_engine_get_speed,
	.set_speed = dal_i2c_sw_engine_set_speed,
	.setup_engine = dal_i2c_engine_setup_i2c_engine,
	.submit_channel_request = dal_i2c_sw_engine_submit_channel_request,
	.process_channel_reply = dal_i2c_engine_process_channel_reply,
	.get_channel_status = dal_i2c_sw_engine_get_channel_status,
};

static void release_engine(
	struct engine *engine)
{

}

static const struct engine_funcs engine_funcs = {
	.release_engine = release_engine,
	.get_engine_type = dal_i2c_sw_engine_get_engine_type,
	.acquire = dal_i2c_engine_acquire,
	.submit_request = dal_i2c_sw_engine_submit_request,
};

bool dal_i2c_sw_engine_construct(
	struct i2c_sw_engine *engine,
	const struct i2c_sw_engine_create_arg *arg)
{
	if (!dal_i2c_engine_construct(&engine->base, arg->ctx))
		return false;

	dal_i2c_sw_engine_set_speed(&engine->base, arg->default_speed);
	engine->base.funcs = &i2c_engine_funcs;
	engine->base.base.funcs = &engine_funcs;
	return true;
}

struct i2c_engine *dal_i2c_sw_engine_create(
	const struct i2c_sw_engine_create_arg *arg)
{
	struct i2c_sw_engine *engine;

	if (!arg) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	engine = dm_alloc(sizeof(struct i2c_sw_engine));

	if (!engine) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (dal_i2c_sw_engine_construct(engine, arg))
		return &engine->base;

	BREAK_TO_DEBUGGER();

	dm_free(engine);

	return NULL;
}
