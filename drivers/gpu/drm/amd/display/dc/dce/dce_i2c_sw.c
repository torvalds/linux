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

#include "dce_i2c.h"
#include "dce_i2c_sw.h"
#include "include/gpio_service_interface.h"
#define SCL false
#define SDA true

void dce_i2c_sw_construct(
	struct dce_i2c_sw *dce_i2c_sw,
	struct dc_context *ctx)
{
	dce_i2c_sw->ctx = ctx;
}

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

static void release_engine_dce_sw(
	struct resource_pool *pool,
	struct dce_i2c_sw *dce_i2c_sw)
{
	dal_ddc_close(dce_i2c_sw->ddc);
	dce_i2c_sw->ddc = NULL;
}

static bool wait_for_scl_high_sw(
	struct dc_context *ctx,
	struct ddc *ddc,
	uint16_t clock_delay_div_4)
{
	uint32_t scl_retry = 0;
	uint32_t scl_retry_max = I2C_SW_TIMEOUT_DELAY / clock_delay_div_4;

	udelay(clock_delay_div_4);

	do {
		if (read_bit_from_ddc(ddc, SCL))
			return true;

		udelay(clock_delay_div_4);

		++scl_retry;
	} while (scl_retry <= scl_retry_max);

	return false;
}
static bool write_byte_sw(
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

		if (!wait_for_scl_high_sw(ctx, ddc_handle, clock_delay_div_4))
			return false;

		write_bit_to_ddc(ddc_handle, SCL, false);

		--shift;
	} while (shift >= 0);

	/* The display sends ACK by preventing the SDA from going high
	 * after the SCL pulse we use to send our last data bit.
	 * If the SDA goes high after that bit, it's a NACK
	 */

	udelay(clock_delay_div_4);

	write_bit_to_ddc(ddc_handle, SDA, true);

	udelay(clock_delay_div_4);

	write_bit_to_ddc(ddc_handle, SCL, true);

	if (!wait_for_scl_high_sw(ctx, ddc_handle, clock_delay_div_4))
		return false;

	/* read ACK bit */

	ack = !read_bit_from_ddc(ddc_handle, SDA);

	udelay(clock_delay_div_4 << 1);

	write_bit_to_ddc(ddc_handle, SCL, false);

	udelay(clock_delay_div_4 << 1);

	return ack;
}

static bool read_byte_sw(
	struct dc_context *ctx,
	struct ddc *ddc_handle,
	uint16_t clock_delay_div_4,
	uint8_t *byte,
	bool more)
{
	int32_t shift = 7;

	uint8_t data = 0;

	/* The data bits are read from MSB to LSB;
	 * bit is read while SCL is high
	 */

	do {
		write_bit_to_ddc(ddc_handle, SCL, true);

		if (!wait_for_scl_high_sw(ctx, ddc_handle, clock_delay_div_4))
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
	 * SDA low means ACK, SDA high means NACK
	 */

	write_bit_to_ddc(ddc_handle, SDA, !more);

	udelay(clock_delay_div_4);

	write_bit_to_ddc(ddc_handle, SCL, true);

	if (!wait_for_scl_high_sw(ctx, ddc_handle, clock_delay_div_4))
		return false;

	write_bit_to_ddc(ddc_handle, SCL, false);

	udelay(clock_delay_div_4);

	write_bit_to_ddc(ddc_handle, SDA, true);

	udelay(clock_delay_div_4);

	return true;
}
static bool stop_sync_sw(
	struct dc_context *ctx,
	struct ddc *ddc_handle,
	uint16_t clock_delay_div_4)
{
	uint32_t retry = 0;

	/* The I2C communications stop signal is:
	 * the SDA going high from low, while the SCL is high.
	 */

	write_bit_to_ddc(ddc_handle, SCL, false);

	udelay(clock_delay_div_4);

	write_bit_to_ddc(ddc_handle, SDA, false);

	udelay(clock_delay_div_4);

	write_bit_to_ddc(ddc_handle, SCL, true);

	if (!wait_for_scl_high_sw(ctx, ddc_handle, clock_delay_div_4))
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
static bool i2c_write_sw(
	struct dc_context *ctx,
	struct ddc *ddc_handle,
	uint16_t clock_delay_div_4,
	uint8_t address,
	uint32_t length,
	const uint8_t *data)
{
	uint32_t i = 0;

	if (!write_byte_sw(ctx, ddc_handle, clock_delay_div_4, address))
		return false;

	while (i < length) {
		if (!write_byte_sw(ctx, ddc_handle, clock_delay_div_4, data[i]))
			return false;
		++i;
	}

	return true;
}

static bool i2c_read_sw(
	struct dc_context *ctx,
	struct ddc *ddc_handle,
	uint16_t clock_delay_div_4,
	uint8_t address,
	uint32_t length,
	uint8_t *data)
{
	uint32_t i = 0;

	if (!write_byte_sw(ctx, ddc_handle, clock_delay_div_4, address))
		return false;

	while (i < length) {
		if (!read_byte_sw(ctx, ddc_handle, clock_delay_div_4, data + i,
			i < length - 1))
			return false;
		++i;
	}

	return true;
}



static bool start_sync_sw(
	struct dc_context *ctx,
	struct ddc *ddc_handle,
	uint16_t clock_delay_div_4)
{
	uint32_t retry = 0;

	/* The I2C communications start signal is:
	 * the SDA going low from high, while the SCL is high.
	 */

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

		if (!wait_for_scl_high_sw(ctx, ddc_handle, clock_delay_div_4))
			break;

		write_bit_to_ddc(ddc_handle, SDA, false);

		udelay(clock_delay_div_4);

		write_bit_to_ddc(ddc_handle, SCL, false);

		udelay(clock_delay_div_4);

		return true;
	} while (retry <= I2C_SW_RETRIES);

	return false;
}

static void dce_i2c_sw_engine_set_speed(
	struct dce_i2c_sw *engine,
	uint32_t speed)
{
	ASSERT(speed);

	engine->speed = speed ? speed : DCE_I2C_DEFAULT_I2C_SW_SPEED;

	engine->clock_delay = 1000 / engine->speed;

	if (engine->clock_delay < 12)
		engine->clock_delay = 12;
}

static bool dce_i2c_sw_engine_acquire_engine(
	struct dce_i2c_sw *engine,
	struct ddc *ddc)
{
	enum gpio_result result;

	result = dal_ddc_open(ddc, GPIO_MODE_FAST_OUTPUT,
		GPIO_DDC_CONFIG_TYPE_MODE_I2C);

	if (result != GPIO_RESULT_OK)
		return false;

	engine->ddc = ddc;

	return true;
}

bool dce_i2c_engine_acquire_sw(
	struct dce_i2c_sw *dce_i2c_sw,
	struct ddc *ddc_handle)
{
	uint32_t counter = 0;
	bool result;

	do {

		result = dce_i2c_sw_engine_acquire_engine(
				dce_i2c_sw, ddc_handle);

		if (result)
			break;

		/* i2c_engine is busy by VBios, lets wait and retry */

		udelay(10);

		++counter;
	} while (counter < 2);

	return result;
}

static void dce_i2c_sw_engine_submit_channel_request(struct dce_i2c_sw *engine,
						     struct i2c_request_transaction_data *req)
{
	struct ddc *ddc = engine->ddc;
	uint16_t clock_delay_div_4 = engine->clock_delay >> 2;

	/* send sync (start / repeated start) */

	bool result = start_sync_sw(engine->ctx, ddc, clock_delay_div_4);

	/* process payload */

	if (result) {
		switch (req->action) {
		case DCE_I2C_TRANSACTION_ACTION_I2C_WRITE:
		case DCE_I2C_TRANSACTION_ACTION_I2C_WRITE_MOT:
			result = i2c_write_sw(engine->ctx, ddc, clock_delay_div_4,
				req->address, req->length, req->data);
		break;
		case DCE_I2C_TRANSACTION_ACTION_I2C_READ:
		case DCE_I2C_TRANSACTION_ACTION_I2C_READ_MOT:
			result = i2c_read_sw(engine->ctx, ddc, clock_delay_div_4,
				req->address, req->length, req->data);
		break;
		default:
			result = false;
		break;
		}
	}

	/* send stop if not 'mot' or operation failed */

	if (!result ||
		(req->action == DCE_I2C_TRANSACTION_ACTION_I2C_WRITE) ||
		(req->action == DCE_I2C_TRANSACTION_ACTION_I2C_READ))
		if (!stop_sync_sw(engine->ctx, ddc, clock_delay_div_4))
			result = false;

	req->status = result ?
		I2C_CHANNEL_OPERATION_SUCCEEDED :
		I2C_CHANNEL_OPERATION_FAILED;
}

static bool dce_i2c_sw_engine_submit_payload(struct dce_i2c_sw *engine,
					     struct i2c_payload *payload,
					     bool middle_of_transaction)
{
	struct i2c_request_transaction_data request;

	if (!payload->write)
		request.action = middle_of_transaction ?
			DCE_I2C_TRANSACTION_ACTION_I2C_READ_MOT :
			DCE_I2C_TRANSACTION_ACTION_I2C_READ;
	else
		request.action = middle_of_transaction ?
			DCE_I2C_TRANSACTION_ACTION_I2C_WRITE_MOT :
			DCE_I2C_TRANSACTION_ACTION_I2C_WRITE;

	request.address = (uint8_t) ((payload->address << 1) | (payload->write ? 0 : 1));
	request.length = payload->length;
	request.data = payload->data;

	dce_i2c_sw_engine_submit_channel_request(engine, &request);

	if ((request.status == I2C_CHANNEL_OPERATION_ENGINE_BUSY) ||
		(request.status == I2C_CHANNEL_OPERATION_FAILED))
		return false;

	return true;
}
bool dce_i2c_submit_command_sw(
	struct resource_pool *pool,
	struct ddc *ddc,
	struct i2c_command *cmd,
	struct dce_i2c_sw *dce_i2c_sw)
{
	uint8_t index_of_payload = 0;
	bool result;

	dce_i2c_sw_engine_set_speed(dce_i2c_sw, cmd->speed);

	result = true;

	while (index_of_payload < cmd->number_of_payloads) {
		bool mot = (index_of_payload != cmd->number_of_payloads - 1);

		struct i2c_payload *payload = cmd->payloads + index_of_payload;

		if (!dce_i2c_sw_engine_submit_payload(
			dce_i2c_sw, payload, mot)) {
			result = false;
			break;
		}

		++index_of_payload;
	}

	release_engine_dce_sw(pool, dce_i2c_sw);

	return result;
}
