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
#include "dm_event_log.h"

/*
 * Pre-requisites: headers required by header of this unit
 */
#include "include/i2caux_interface.h"
#include "engine.h"

/*
 * Header of this unit
 */

#include "aux_engine.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "include/link_service_types.h"

/*
 * This unit
 */

enum {
	AUX_INVALID_REPLY_RETRY_COUNTER = 1,
	AUX_TIMED_OUT_RETRY_COUNTER = 2,
	AUX_DEFER_RETRY_COUNTER = 6
};

#define FROM_ENGINE(ptr) \
	container_of((ptr), struct aux_engine, base)
#define DC_LOGGER \
	engine->base.ctx->logger

enum i2caux_engine_type dal_aux_engine_get_engine_type(
	const struct engine *engine)
{
	return I2CAUX_ENGINE_TYPE_AUX;
}

bool dal_aux_engine_acquire(
	struct engine *engine,
	struct ddc *ddc)
{
	struct aux_engine *aux_engine = FROM_ENGINE(engine);

	enum gpio_result result;
	if (aux_engine->funcs->is_engine_available) {
		/*check whether SW could use the engine*/
		if (!aux_engine->funcs->is_engine_available(aux_engine)) {
			return false;
		}
	}

	result = dal_ddc_open(ddc, GPIO_MODE_HARDWARE,
		GPIO_DDC_CONFIG_TYPE_MODE_AUX);

	if (result != GPIO_RESULT_OK)
		return false;

	if (!aux_engine->funcs->acquire_engine(aux_engine)) {
		dal_ddc_close(ddc);
		return false;
	}

	engine->ddc = ddc;

	return true;
}

struct read_command_context {
	uint8_t *buffer;
	uint32_t current_read_length;
	uint32_t offset;
	enum i2caux_transaction_status status;

	struct aux_request_transaction_data request;
	struct aux_reply_transaction_data reply;

	uint8_t returned_byte;

	uint32_t timed_out_retry_aux;
	uint32_t invalid_reply_retry_aux;
	uint32_t defer_retry_aux;
	uint32_t defer_retry_i2c;
	uint32_t invalid_reply_retry_aux_on_ack;

	bool transaction_complete;
	bool operation_succeeded;
};

static void process_read_reply(
	struct aux_engine *engine,
	struct read_command_context *ctx)
{
	engine->funcs->process_channel_reply(engine, &ctx->reply);

	switch (ctx->reply.status) {
	case AUX_TRANSACTION_REPLY_AUX_ACK:
		ctx->defer_retry_aux = 0;
		if (ctx->returned_byte > ctx->current_read_length) {
			ctx->status =
				I2CAUX_TRANSACTION_STATUS_FAILED_PROTOCOL_ERROR;
			ctx->operation_succeeded = false;
		} else if (ctx->returned_byte < ctx->current_read_length) {
			ctx->current_read_length -= ctx->returned_byte;

			ctx->offset += ctx->returned_byte;

			++ctx->invalid_reply_retry_aux_on_ack;

			if (ctx->invalid_reply_retry_aux_on_ack >
				AUX_INVALID_REPLY_RETRY_COUNTER) {
				ctx->status =
				I2CAUX_TRANSACTION_STATUS_FAILED_PROTOCOL_ERROR;
				ctx->operation_succeeded = false;
			}
		} else {
			ctx->status = I2CAUX_TRANSACTION_STATUS_SUCCEEDED;
			ctx->transaction_complete = true;
			ctx->operation_succeeded = true;
		}
	break;
	case AUX_TRANSACTION_REPLY_AUX_NACK:
		ctx->status = I2CAUX_TRANSACTION_STATUS_FAILED_NACK;
		ctx->operation_succeeded = false;
	break;
	case AUX_TRANSACTION_REPLY_AUX_DEFER:
		++ctx->defer_retry_aux;

		if (ctx->defer_retry_aux > AUX_DEFER_RETRY_COUNTER) {
			ctx->status = I2CAUX_TRANSACTION_STATUS_FAILED_TIMEOUT;
			ctx->operation_succeeded = false;
		}
	break;
	case AUX_TRANSACTION_REPLY_I2C_DEFER:
		ctx->defer_retry_aux = 0;

		++ctx->defer_retry_i2c;

		if (ctx->defer_retry_i2c > AUX_DEFER_RETRY_COUNTER) {
			ctx->status = I2CAUX_TRANSACTION_STATUS_FAILED_TIMEOUT;
			ctx->operation_succeeded = false;
		}
	break;
	case AUX_TRANSACTION_REPLY_HPD_DISCON:
		ctx->status = I2CAUX_TRANSACTION_STATUS_FAILED_HPD_DISCON;
		ctx->operation_succeeded = false;
	break;
	default:
		ctx->status = I2CAUX_TRANSACTION_STATUS_UNKNOWN;
		ctx->operation_succeeded = false;
	}
}

static void process_read_request(
	struct aux_engine *engine,
	struct read_command_context *ctx)
{
	enum aux_channel_operation_result operation_result;

	engine->funcs->submit_channel_request(engine, &ctx->request);

	operation_result = engine->funcs->get_channel_status(
		engine, &ctx->returned_byte);

	switch (operation_result) {
	case AUX_CHANNEL_OPERATION_SUCCEEDED:
		if (ctx->returned_byte > ctx->current_read_length) {
			ctx->status =
				I2CAUX_TRANSACTION_STATUS_FAILED_PROTOCOL_ERROR;
			ctx->operation_succeeded = false;
		} else {
			ctx->timed_out_retry_aux = 0;
			ctx->invalid_reply_retry_aux = 0;

			ctx->reply.length = ctx->returned_byte;
			ctx->reply.data = ctx->buffer;

			process_read_reply(engine, ctx);
		}
	break;
	case AUX_CHANNEL_OPERATION_FAILED_INVALID_REPLY:
		++ctx->invalid_reply_retry_aux;

		if (ctx->invalid_reply_retry_aux >
			AUX_INVALID_REPLY_RETRY_COUNTER) {
			ctx->status =
				I2CAUX_TRANSACTION_STATUS_FAILED_PROTOCOL_ERROR;
			ctx->operation_succeeded = false;
		} else
			udelay(400);
	break;
	case AUX_CHANNEL_OPERATION_FAILED_TIMEOUT:
		++ctx->timed_out_retry_aux;

		if (ctx->timed_out_retry_aux > AUX_TIMED_OUT_RETRY_COUNTER) {
			ctx->status = I2CAUX_TRANSACTION_STATUS_FAILED_TIMEOUT;
			ctx->operation_succeeded = false;
		} else {
			/* DP 1.2a, table 2-58:
			 * "S3: AUX Request CMD PENDING:
			 * retry 3 times, with 400usec wait on each"
			 * The HW timeout is set to 550usec,
			 * so we should not wait here */
		}
	break;
	case AUX_CHANNEL_OPERATION_FAILED_HPD_DISCON:
		ctx->status = I2CAUX_TRANSACTION_STATUS_FAILED_HPD_DISCON;
		ctx->operation_succeeded = false;
	break;
	default:
		ctx->status = I2CAUX_TRANSACTION_STATUS_UNKNOWN;
		ctx->operation_succeeded = false;
	}
}

static bool read_command(
	struct aux_engine *engine,
	struct i2caux_transaction_request *request,
	bool middle_of_transaction)
{
	struct read_command_context ctx;

	ctx.buffer = request->payload.data;
	ctx.current_read_length = request->payload.length;
	ctx.offset = 0;
	ctx.timed_out_retry_aux = 0;
	ctx.invalid_reply_retry_aux = 0;
	ctx.defer_retry_aux = 0;
	ctx.defer_retry_i2c = 0;
	ctx.invalid_reply_retry_aux_on_ack = 0;
	ctx.transaction_complete = false;
	ctx.operation_succeeded = true;

	if (request->payload.address_space ==
		I2CAUX_TRANSACTION_ADDRESS_SPACE_DPCD) {
		ctx.request.type = AUX_TRANSACTION_TYPE_DP;
		ctx.request.action = I2CAUX_TRANSACTION_ACTION_DP_READ;
		ctx.request.address = request->payload.address;
	} else if (request->payload.address_space ==
		I2CAUX_TRANSACTION_ADDRESS_SPACE_I2C) {
		ctx.request.type = AUX_TRANSACTION_TYPE_I2C;
		ctx.request.action = middle_of_transaction ?
			I2CAUX_TRANSACTION_ACTION_I2C_READ_MOT :
			I2CAUX_TRANSACTION_ACTION_I2C_READ;
		ctx.request.address = request->payload.address >> 1;
	} else {
		/* in DAL2, there was no return in such case */
		BREAK_TO_DEBUGGER();
		return false;
	}

	ctx.request.delay = 0;

	do {
		memset(ctx.buffer + ctx.offset, 0, ctx.current_read_length);

		ctx.request.data = ctx.buffer + ctx.offset;
		ctx.request.length = ctx.current_read_length;

		process_read_request(engine, &ctx);

		request->status = ctx.status;

		if (ctx.operation_succeeded && !ctx.transaction_complete)
			if (ctx.request.type == AUX_TRANSACTION_TYPE_I2C)
				msleep(engine->delay);
	} while (ctx.operation_succeeded && !ctx.transaction_complete);

	if (request->payload.address_space ==
		I2CAUX_TRANSACTION_ADDRESS_SPACE_DPCD) {
		DC_LOG_I2C_AUX("READ: addr:0x%x  value:0x%x Result:%d",
				request->payload.address,
				request->payload.data[0],
				ctx.operation_succeeded);
	}

	return ctx.operation_succeeded;
}

struct write_command_context {
	bool mot;

	uint8_t *buffer;
	uint32_t current_write_length;
	enum i2caux_transaction_status status;

	struct aux_request_transaction_data request;
	struct aux_reply_transaction_data reply;

	uint8_t returned_byte;

	uint32_t timed_out_retry_aux;
	uint32_t invalid_reply_retry_aux;
	uint32_t defer_retry_aux;
	uint32_t defer_retry_i2c;
	uint32_t max_defer_retry;
	uint32_t ack_m_retry;

	uint8_t reply_data[DEFAULT_AUX_MAX_DATA_SIZE];

	bool transaction_complete;
	bool operation_succeeded;
};

static void process_write_reply(
	struct aux_engine *engine,
	struct write_command_context *ctx)
{
	engine->funcs->process_channel_reply(engine, &ctx->reply);

	switch (ctx->reply.status) {
	case AUX_TRANSACTION_REPLY_AUX_ACK:
		ctx->operation_succeeded = true;

		if (ctx->returned_byte) {
			ctx->request.action = ctx->mot ?
			I2CAUX_TRANSACTION_ACTION_I2C_STATUS_REQUEST_MOT :
			I2CAUX_TRANSACTION_ACTION_I2C_STATUS_REQUEST;

			ctx->current_write_length = 0;

			++ctx->ack_m_retry;

			if (ctx->ack_m_retry > AUX_DEFER_RETRY_COUNTER) {
				ctx->status =
				I2CAUX_TRANSACTION_STATUS_FAILED_TIMEOUT;
				ctx->operation_succeeded = false;
			} else
				udelay(300);
		} else {
			ctx->status = I2CAUX_TRANSACTION_STATUS_SUCCEEDED;
			ctx->defer_retry_aux = 0;
			ctx->ack_m_retry = 0;
			ctx->transaction_complete = true;
		}
	break;
	case AUX_TRANSACTION_REPLY_AUX_NACK:
		ctx->status = I2CAUX_TRANSACTION_STATUS_FAILED_NACK;
		ctx->operation_succeeded = false;
	break;
	case AUX_TRANSACTION_REPLY_AUX_DEFER:
		++ctx->defer_retry_aux;

		if (ctx->defer_retry_aux > ctx->max_defer_retry) {
			ctx->status = I2CAUX_TRANSACTION_STATUS_FAILED_TIMEOUT;
			ctx->operation_succeeded = false;
		}
	break;
	case AUX_TRANSACTION_REPLY_I2C_DEFER:
		ctx->defer_retry_aux = 0;
		ctx->current_write_length = 0;

		ctx->request.action = ctx->mot ?
			I2CAUX_TRANSACTION_ACTION_I2C_STATUS_REQUEST_MOT :
			I2CAUX_TRANSACTION_ACTION_I2C_STATUS_REQUEST;

		++ctx->defer_retry_i2c;

		if (ctx->defer_retry_i2c > ctx->max_defer_retry) {
			ctx->status = I2CAUX_TRANSACTION_STATUS_FAILED_TIMEOUT;
			ctx->operation_succeeded = false;
		}
	break;
	case AUX_TRANSACTION_REPLY_HPD_DISCON:
		ctx->status = I2CAUX_TRANSACTION_STATUS_FAILED_HPD_DISCON;
		ctx->operation_succeeded = false;
	break;
	default:
		ctx->status = I2CAUX_TRANSACTION_STATUS_UNKNOWN;
		ctx->operation_succeeded = false;
	}
}

static void process_write_request(
	struct aux_engine *engine,
	struct write_command_context *ctx)
{
	enum aux_channel_operation_result operation_result;

	engine->funcs->submit_channel_request(engine, &ctx->request);

	operation_result = engine->funcs->get_channel_status(
		engine, &ctx->returned_byte);

	switch (operation_result) {
	case AUX_CHANNEL_OPERATION_SUCCEEDED:
		ctx->timed_out_retry_aux = 0;
		ctx->invalid_reply_retry_aux = 0;

		ctx->reply.length = ctx->returned_byte;
		ctx->reply.data = ctx->reply_data;

		process_write_reply(engine, ctx);
	break;
	case AUX_CHANNEL_OPERATION_FAILED_INVALID_REPLY:
		++ctx->invalid_reply_retry_aux;

		if (ctx->invalid_reply_retry_aux >
			AUX_INVALID_REPLY_RETRY_COUNTER) {
			ctx->status =
				I2CAUX_TRANSACTION_STATUS_FAILED_PROTOCOL_ERROR;
			ctx->operation_succeeded = false;
		} else
			udelay(400);
	break;
	case AUX_CHANNEL_OPERATION_FAILED_TIMEOUT:
		++ctx->timed_out_retry_aux;

		if (ctx->timed_out_retry_aux > AUX_TIMED_OUT_RETRY_COUNTER) {
			ctx->status = I2CAUX_TRANSACTION_STATUS_FAILED_TIMEOUT;
			ctx->operation_succeeded = false;
		} else {
			/* DP 1.2a, table 2-58:
			 * "S3: AUX Request CMD PENDING:
			 * retry 3 times, with 400usec wait on each"
			 * The HW timeout is set to 550usec,
			 * so we should not wait here */
		}
	break;
	case AUX_CHANNEL_OPERATION_FAILED_HPD_DISCON:
		ctx->status = I2CAUX_TRANSACTION_STATUS_FAILED_HPD_DISCON;
		ctx->operation_succeeded = false;
	break;
	default:
		ctx->status = I2CAUX_TRANSACTION_STATUS_UNKNOWN;
		ctx->operation_succeeded = false;
	}
}

static bool write_command(
	struct aux_engine *engine,
	struct i2caux_transaction_request *request,
	bool middle_of_transaction)
{
	struct write_command_context ctx;

	ctx.mot = middle_of_transaction;
	ctx.buffer = request->payload.data;
	ctx.current_write_length = request->payload.length;
	ctx.timed_out_retry_aux = 0;
	ctx.invalid_reply_retry_aux = 0;
	ctx.defer_retry_aux = 0;
	ctx.defer_retry_i2c = 0;
	ctx.ack_m_retry = 0;
	ctx.transaction_complete = false;
	ctx.operation_succeeded = true;

	if (request->payload.address_space ==
		I2CAUX_TRANSACTION_ADDRESS_SPACE_DPCD) {
		ctx.request.type = AUX_TRANSACTION_TYPE_DP;
		ctx.request.action = I2CAUX_TRANSACTION_ACTION_DP_WRITE;
		ctx.request.address = request->payload.address;
	} else if (request->payload.address_space ==
		I2CAUX_TRANSACTION_ADDRESS_SPACE_I2C) {
		ctx.request.type = AUX_TRANSACTION_TYPE_I2C;
		ctx.request.action = middle_of_transaction ?
			I2CAUX_TRANSACTION_ACTION_I2C_WRITE_MOT :
			I2CAUX_TRANSACTION_ACTION_I2C_WRITE;
		ctx.request.address = request->payload.address >> 1;
	} else {
		/* in DAL2, there was no return in such case */
		BREAK_TO_DEBUGGER();
		return false;
	}

	ctx.request.delay = 0;

	ctx.max_defer_retry =
		(engine->max_defer_write_retry > AUX_DEFER_RETRY_COUNTER) ?
			engine->max_defer_write_retry : AUX_DEFER_RETRY_COUNTER;

	do {
		ctx.request.data = ctx.buffer;
		ctx.request.length = ctx.current_write_length;

		process_write_request(engine, &ctx);

		request->status = ctx.status;

		if (ctx.operation_succeeded && !ctx.transaction_complete)
			if (ctx.request.type == AUX_TRANSACTION_TYPE_I2C)
				msleep(engine->delay);
	} while (ctx.operation_succeeded && !ctx.transaction_complete);

	if (request->payload.address_space ==
		I2CAUX_TRANSACTION_ADDRESS_SPACE_DPCD) {
		DC_LOG_I2C_AUX("WRITE: addr:0x%x  value:0x%x Result:%d",
				request->payload.address,
				request->payload.data[0],
				ctx.operation_succeeded);
	}

	return ctx.operation_succeeded;
}

static bool end_of_transaction_command(
	struct aux_engine *engine,
	struct i2caux_transaction_request *request)
{
	struct i2caux_transaction_request dummy_request;
	uint8_t dummy_data;

	/* [tcheng] We only need to send the stop (read with MOT = 0)
	 * for I2C-over-Aux, not native AUX */

	if (request->payload.address_space !=
		I2CAUX_TRANSACTION_ADDRESS_SPACE_I2C)
		return false;

	dummy_request.operation = request->operation;
	dummy_request.payload.address_space = request->payload.address_space;
	dummy_request.payload.address = request->payload.address;

	/*
	 * Add a dummy byte due to some receiver quirk
	 * where one byte is sent along with MOT = 0.
	 * Ideally this should be 0.
	 */

	dummy_request.payload.length = 0;
	dummy_request.payload.data = &dummy_data;

	if (request->operation == I2CAUX_TRANSACTION_READ)
		return read_command(engine, &dummy_request, false);
	else
		return write_command(engine, &dummy_request, false);

	/* according Syed, it does not need now DoDummyMOT */
}

bool dal_aux_engine_submit_request(
	struct engine *engine,
	struct i2caux_transaction_request *request,
	bool middle_of_transaction)
{
	struct aux_engine *aux_engine = FROM_ENGINE(engine);

	bool result;
	bool mot_used = true;

	switch (request->operation) {
	case I2CAUX_TRANSACTION_READ:
		result = read_command(aux_engine, request, mot_used);
	break;
	case I2CAUX_TRANSACTION_WRITE:
		result = write_command(aux_engine, request, mot_used);
	break;
	default:
		result = false;
	}

	/* [tcheng]
	 * need to send stop for the last transaction to free up the AUX
	 * if the above command fails, this would be the last transaction */

	if (!middle_of_transaction || !result)
		end_of_transaction_command(aux_engine, request);

	/* mask AUX interrupt */

	return result;
}

void dal_aux_engine_construct(
	struct aux_engine *engine,
	struct dc_context *ctx)
{
	dal_i2caux_construct_engine(&engine->base, ctx);
	engine->delay = 0;
	engine->max_defer_write_retry = 0;
}

void dal_aux_engine_destruct(
	struct aux_engine *engine)
{
	dal_i2caux_destruct_engine(&engine->base);
}
