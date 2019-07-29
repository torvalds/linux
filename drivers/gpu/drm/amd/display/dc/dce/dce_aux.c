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
#include "dce_aux.h"
#include "dce/dce_11_0_sh_mask.h"

#define CTX \
	aux110->base.ctx
#define REG(reg_name)\
	(aux110->regs->reg_name)

#define DC_LOGGER \
	engine->ctx->logger

#include "reg_helper.h"

#define FROM_AUX_ENGINE(ptr) \
	container_of((ptr), struct aux_engine_dce110, base)

#define FROM_ENGINE(ptr) \
	FROM_AUX_ENGINE(container_of((ptr), struct aux_engine, base))

#define FROM_AUX_ENGINE_ENGINE(ptr) \
	container_of((ptr), struct aux_engine, base)
enum {
	AUX_INVALID_REPLY_RETRY_COUNTER = 1,
	AUX_TIMED_OUT_RETRY_COUNTER = 2,
	AUX_DEFER_RETRY_COUNTER = 6
};
static void release_engine(
	struct aux_engine *engine)
{
	struct aux_engine_dce110 *aux110 = FROM_AUX_ENGINE(engine);

	dal_ddc_close(engine->ddc);

	engine->ddc = NULL;

	REG_UPDATE(AUX_ARB_CONTROL, AUX_SW_DONE_USING_AUX_REG, 1);
}

#define SW_CAN_ACCESS_AUX 1
#define DMCU_CAN_ACCESS_AUX 2

static bool is_engine_available(
	struct aux_engine *engine)
{
	struct aux_engine_dce110 *aux110 = FROM_AUX_ENGINE(engine);

	uint32_t value = REG_READ(AUX_ARB_CONTROL);
	uint32_t field = get_reg_field_value(
			value,
			AUX_ARB_CONTROL,
			AUX_REG_RW_CNTL_STATUS);

	return (field != DMCU_CAN_ACCESS_AUX);
}
static bool acquire_engine(
	struct aux_engine *engine)
{
	struct aux_engine_dce110 *aux110 = FROM_AUX_ENGINE(engine);

	uint32_t value = REG_READ(AUX_ARB_CONTROL);
	uint32_t field = get_reg_field_value(
			value,
			AUX_ARB_CONTROL,
			AUX_REG_RW_CNTL_STATUS);
	if (field == DMCU_CAN_ACCESS_AUX)
		return false;
	/* enable AUX before request SW to access AUX */
	value = REG_READ(AUX_CONTROL);
	field = get_reg_field_value(value,
				AUX_CONTROL,
				AUX_EN);

	if (field == 0) {
		set_reg_field_value(
				value,
				1,
				AUX_CONTROL,
				AUX_EN);

		if (REG(AUX_RESET_MASK)) {
			/*DP_AUX block as part of the enable sequence*/
			set_reg_field_value(
				value,
				1,
				AUX_CONTROL,
				AUX_RESET);
		}

		REG_WRITE(AUX_CONTROL, value);

		if (REG(AUX_RESET_MASK)) {
			/*poll HW to make sure reset it done*/

			REG_WAIT(AUX_CONTROL, AUX_RESET_DONE, 1,
					1, 11);

			set_reg_field_value(
				value,
				0,
				AUX_CONTROL,
				AUX_RESET);

			REG_WRITE(AUX_CONTROL, value);

			REG_WAIT(AUX_CONTROL, AUX_RESET_DONE, 0,
					1, 11);
		}
	} /*if (field)*/

	/* request SW to access AUX */
	REG_UPDATE(AUX_ARB_CONTROL, AUX_SW_USE_AUX_REG_REQ, 1);

	value = REG_READ(AUX_ARB_CONTROL);
	field = get_reg_field_value(
			value,
			AUX_ARB_CONTROL,
			AUX_REG_RW_CNTL_STATUS);

	return (field == SW_CAN_ACCESS_AUX);
}

#define COMPOSE_AUX_SW_DATA_16_20(command, address) \
	((command) | ((0xF0000 & (address)) >> 16))

#define COMPOSE_AUX_SW_DATA_8_15(address) \
	((0xFF00 & (address)) >> 8)

#define COMPOSE_AUX_SW_DATA_0_7(address) \
	(0xFF & (address))

static void submit_channel_request(
	struct aux_engine *engine,
	struct aux_request_transaction_data *request)
{
	struct aux_engine_dce110 *aux110 = FROM_AUX_ENGINE(engine);
	uint32_t value;
	uint32_t length;

	bool is_write =
		((request->type == AUX_TRANSACTION_TYPE_DP) &&
		 (request->action == I2CAUX_TRANSACTION_ACTION_DP_WRITE)) ||
		((request->type == AUX_TRANSACTION_TYPE_I2C) &&
		((request->action == I2CAUX_TRANSACTION_ACTION_I2C_WRITE) ||
		 (request->action == I2CAUX_TRANSACTION_ACTION_I2C_WRITE_MOT)));
	if (REG(AUXN_IMPCAL)) {
		/* clear_aux_error */
		REG_UPDATE_SEQ(AUXN_IMPCAL, AUXN_CALOUT_ERROR_AK,
				1,
				0);

		REG_UPDATE_SEQ(AUXP_IMPCAL, AUXP_CALOUT_ERROR_AK,
				1,
				0);

		/* force_default_calibrate */
		REG_UPDATE_1BY1_2(AUXN_IMPCAL,
				AUXN_IMPCAL_ENABLE, 1,
				AUXN_IMPCAL_OVERRIDE_ENABLE, 0);

		/* bug? why AUXN update EN and OVERRIDE_EN 1 by 1 while AUX P toggles OVERRIDE? */

		REG_UPDATE_SEQ(AUXP_IMPCAL, AUXP_IMPCAL_OVERRIDE_ENABLE,
				1,
				0);
	}
	/* set the delay and the number of bytes to write */

	/* The length include
	 * the 4 bit header and the 20 bit address
	 * (that is 3 byte).
	 * If the requested length is non zero this means
	 * an addition byte specifying the length is required.
	 */

	length = request->length ? 4 : 3;
	if (is_write)
		length += request->length;

	REG_UPDATE_2(AUX_SW_CONTROL,
			AUX_SW_START_DELAY, request->delay,
			AUX_SW_WR_BYTES, length);

	/* program action and address and payload data (if 'is_write') */
	value = REG_UPDATE_4(AUX_SW_DATA,
			AUX_SW_INDEX, 0,
			AUX_SW_DATA_RW, 0,
			AUX_SW_AUTOINCREMENT_DISABLE, 1,
			AUX_SW_DATA, COMPOSE_AUX_SW_DATA_16_20(request->action, request->address));

	value = REG_SET_2(AUX_SW_DATA, value,
			AUX_SW_AUTOINCREMENT_DISABLE, 0,
			AUX_SW_DATA, COMPOSE_AUX_SW_DATA_8_15(request->address));

	value = REG_SET(AUX_SW_DATA, value,
			AUX_SW_DATA, COMPOSE_AUX_SW_DATA_0_7(request->address));

	if (request->length) {
		value = REG_SET(AUX_SW_DATA, value,
				AUX_SW_DATA, request->length - 1);
	}

	if (is_write) {
		/* Load the HW buffer with the Data to be sent.
		 * This is relevant for write operation.
		 * For read, the data recived data will be
		 * processed in process_channel_reply().
		 */
		uint32_t i = 0;

		while (i < request->length) {
			value = REG_SET(AUX_SW_DATA, value,
					AUX_SW_DATA, request->data[i]);

			++i;
		}
	}

	REG_UPDATE(AUX_INTERRUPT_CONTROL, AUX_SW_DONE_ACK, 1);
	REG_WAIT(AUX_SW_STATUS, AUX_SW_DONE, 0,
				10, aux110->timeout_period/10);
	REG_UPDATE(AUX_SW_CONTROL, AUX_SW_GO, 1);
}

static int read_channel_reply(struct aux_engine *engine, uint32_t size,
			      uint8_t *buffer, uint8_t *reply_result,
			      uint32_t *sw_status)
{
	struct aux_engine_dce110 *aux110 = FROM_AUX_ENGINE(engine);
	uint32_t bytes_replied;
	uint32_t reply_result_32;

	*sw_status = REG_GET(AUX_SW_STATUS, AUX_SW_REPLY_BYTE_COUNT,
			     &bytes_replied);

	/* In case HPD is LOW, exit AUX transaction */
	if ((*sw_status & AUX_SW_STATUS__AUX_SW_HPD_DISCON_MASK))
		return -1;

	/* Need at least the status byte */
	if (!bytes_replied)
		return -1;

	REG_UPDATE_1BY1_3(AUX_SW_DATA,
			  AUX_SW_INDEX, 0,
			  AUX_SW_AUTOINCREMENT_DISABLE, 1,
			  AUX_SW_DATA_RW, 1);

	REG_GET(AUX_SW_DATA, AUX_SW_DATA, &reply_result_32);
	reply_result_32 = reply_result_32 >> 4;
	*reply_result = (uint8_t)reply_result_32;

	if (reply_result_32 == 0) { /* ACK */
		uint32_t i = 0;

		/* First byte was already used to get the command status */
		--bytes_replied;

		/* Do not overflow buffer */
		if (bytes_replied > size)
			return -1;

		while (i < bytes_replied) {
			uint32_t aux_sw_data_val;

			REG_GET(AUX_SW_DATA, AUX_SW_DATA, &aux_sw_data_val);
			buffer[i] = aux_sw_data_val;
			++i;
		}

		return i;
	}

	return 0;
}

static void process_channel_reply(
	struct aux_engine *engine,
	struct aux_reply_transaction_data *reply)
{
	int bytes_replied;
	uint8_t reply_result;
	uint32_t sw_status;

	bytes_replied = read_channel_reply(engine, reply->length, reply->data,
					   &reply_result, &sw_status);

	/* in case HPD is LOW, exit AUX transaction */
	if ((sw_status & AUX_SW_STATUS__AUX_SW_HPD_DISCON_MASK)) {
		reply->status = AUX_TRANSACTION_REPLY_HPD_DISCON;
		return;
	}

	if (bytes_replied < 0) {
		/* Need to handle an error case...
		 * Hopefully, upper layer function won't call this function if
		 * the number of bytes in the reply was 0, because there was
		 * surely an error that was asserted that should have been
		 * handled for hot plug case, this could happens
		 */
		if (!(sw_status & AUX_SW_STATUS__AUX_SW_HPD_DISCON_MASK)) {
			reply->status = AUX_TRANSACTION_REPLY_INVALID;
			ASSERT_CRITICAL(false);
			return;
		}
	} else {

		switch (reply_result) {
		case 0: /* ACK */
			reply->status = AUX_TRANSACTION_REPLY_AUX_ACK;
		break;
		case 1: /* NACK */
			reply->status = AUX_TRANSACTION_REPLY_AUX_NACK;
		break;
		case 2: /* DEFER */
			reply->status = AUX_TRANSACTION_REPLY_AUX_DEFER;
		break;
		case 4: /* AUX ACK / I2C NACK */
			reply->status = AUX_TRANSACTION_REPLY_I2C_NACK;
		break;
		case 8: /* AUX ACK / I2C DEFER */
			reply->status = AUX_TRANSACTION_REPLY_I2C_DEFER;
		break;
		default:
			reply->status = AUX_TRANSACTION_REPLY_INVALID;
		}
	}
}

static enum aux_channel_operation_result get_channel_status(
	struct aux_engine *engine,
	uint8_t *returned_bytes)
{
	struct aux_engine_dce110 *aux110 = FROM_AUX_ENGINE(engine);

	uint32_t value;

	if (returned_bytes == NULL) {
		/*caller pass NULL pointer*/
		ASSERT_CRITICAL(false);
		return AUX_CHANNEL_OPERATION_FAILED_REASON_UNKNOWN;
	}
	*returned_bytes = 0;

	/* poll to make sure that SW_DONE is asserted */
	value = REG_WAIT(AUX_SW_STATUS, AUX_SW_DONE, 1,
				10, aux110->timeout_period/10);

	/* in case HPD is LOW, exit AUX transaction */
	if ((value & AUX_SW_STATUS__AUX_SW_HPD_DISCON_MASK))
		return AUX_CHANNEL_OPERATION_FAILED_HPD_DISCON;

	/* Note that the following bits are set in 'status.bits'
	 * during CTS 4.2.1.2 (FW 3.3.1):
	 * AUX_SW_RX_MIN_COUNT_VIOL, AUX_SW_RX_INVALID_STOP,
	 * AUX_SW_RX_RECV_NO_DET, AUX_SW_RX_RECV_INVALID_H.
	 *
	 * AUX_SW_RX_MIN_COUNT_VIOL is an internal,
	 * HW debugging bit and should be ignored.
	 */
	if (value & AUX_SW_STATUS__AUX_SW_DONE_MASK) {
		if ((value & AUX_SW_STATUS__AUX_SW_RX_TIMEOUT_STATE_MASK) ||
			(value & AUX_SW_STATUS__AUX_SW_RX_TIMEOUT_MASK))
			return AUX_CHANNEL_OPERATION_FAILED_TIMEOUT;

		else if ((value & AUX_SW_STATUS__AUX_SW_RX_INVALID_STOP_MASK) ||
			(value & AUX_SW_STATUS__AUX_SW_RX_RECV_NO_DET_MASK) ||
			(value &
				AUX_SW_STATUS__AUX_SW_RX_RECV_INVALID_H_MASK) ||
			(value & AUX_SW_STATUS__AUX_SW_RX_RECV_INVALID_L_MASK))
			return AUX_CHANNEL_OPERATION_FAILED_INVALID_REPLY;

		*returned_bytes = get_reg_field_value(value,
				AUX_SW_STATUS,
				AUX_SW_REPLY_BYTE_COUNT);

		if (*returned_bytes == 0)
			return
			AUX_CHANNEL_OPERATION_FAILED_INVALID_REPLY;
		else {
			*returned_bytes -= 1;
			return AUX_CHANNEL_OPERATION_SUCCEEDED;
		}
	} else {
		/*time_elapsed >= aux_engine->timeout_period
		 *  AUX_SW_STATUS__AUX_SW_HPD_DISCON = at this point
		 */
		ASSERT_CRITICAL(false);
		return AUX_CHANNEL_OPERATION_FAILED_TIMEOUT;
	}
}
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
			 * so we should not wait here
			 */
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
			 * so we should not wait here
			 */
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
	 * for I2C-over-Aux, not native AUX
	 */

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
static bool submit_request(
	struct aux_engine *engine,
	struct i2caux_transaction_request *request,
	bool middle_of_transaction)
{

	bool result;
	bool mot_used = true;

	switch (request->operation) {
	case I2CAUX_TRANSACTION_READ:
		result = read_command(engine, request, mot_used);
	break;
	case I2CAUX_TRANSACTION_WRITE:
		result = write_command(engine, request, mot_used);
	break;
	default:
		result = false;
	}

	/* [tcheng]
	 * need to send stop for the last transaction to free up the AUX
	 * if the above command fails, this would be the last transaction
	 */

	if (!middle_of_transaction || !result)
		end_of_transaction_command(engine, request);

	/* mask AUX interrupt */

	return result;
}
enum i2caux_engine_type get_engine_type(
		const struct aux_engine *engine)
{
	return I2CAUX_ENGINE_TYPE_AUX;
}

static bool acquire(
	struct aux_engine *engine,
	struct ddc *ddc)
{

	enum gpio_result result;

	if (engine->funcs->is_engine_available) {
		/*check whether SW could use the engine*/
		if (!engine->funcs->is_engine_available(engine))
			return false;
	}

	result = dal_ddc_open(ddc, GPIO_MODE_HARDWARE,
		GPIO_DDC_CONFIG_TYPE_MODE_AUX);

	if (result != GPIO_RESULT_OK)
		return false;

	if (!engine->funcs->acquire_engine(engine)) {
		dal_ddc_close(ddc);
		return false;
	}

	engine->ddc = ddc;

	return true;
}

static const struct aux_engine_funcs aux_engine_funcs = {
	.acquire_engine = acquire_engine,
	.submit_channel_request = submit_channel_request,
	.process_channel_reply = process_channel_reply,
	.read_channel_reply = read_channel_reply,
	.get_channel_status = get_channel_status,
	.is_engine_available = is_engine_available,
	.release_engine = release_engine,
	.destroy_engine = dce110_engine_destroy,
	.submit_request = submit_request,
	.get_engine_type = get_engine_type,
	.acquire = acquire,
};

void dce110_engine_destroy(struct aux_engine **engine)
{

	struct aux_engine_dce110 *engine110 = FROM_AUX_ENGINE(*engine);

	kfree(engine110);
	*engine = NULL;

}
struct aux_engine *dce110_aux_engine_construct(struct aux_engine_dce110 *aux_engine110,
		struct dc_context *ctx,
		uint32_t inst,
		uint32_t timeout_period,
		const struct dce110_aux_registers *regs)
{
	aux_engine110->base.ddc = NULL;
	aux_engine110->base.ctx = ctx;
	aux_engine110->base.delay = 0;
	aux_engine110->base.max_defer_write_retry = 0;
	aux_engine110->base.funcs = &aux_engine_funcs;
	aux_engine110->base.inst = inst;
	aux_engine110->timeout_period = timeout_period;
	aux_engine110->regs = regs;

	return &aux_engine110->base;
}

