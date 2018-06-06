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
#include "../aux_engine.h"

/*
 * Header of this unit
 */

#include "aux_engine_dce110.h"

/*
 * Post-requisites: headers required by this unit
 */
#include "dce/dce_11_0_sh_mask.h"

#define CTX \
	aux110->base.base.ctx
#define REG(reg_name)\
	(aux110->regs->reg_name)
#include "reg_helper.h"

/*
 * This unit
 */

/*
 * @brief
 * Cast 'struct aux_engine *'
 * to 'struct aux_engine_dce110 *'
 */
#define FROM_AUX_ENGINE(ptr) \
	container_of((ptr), struct aux_engine_dce110, base)

/*
 * @brief
 * Cast 'struct engine *'
 * to 'struct aux_engine_dce110 *'
 */
#define FROM_ENGINE(ptr) \
	FROM_AUX_ENGINE(container_of((ptr), struct aux_engine, base))

static void release_engine(
	struct engine *engine)
{
	struct aux_engine_dce110 *aux110 = FROM_ENGINE(engine);

	REG_UPDATE(AUX_ARB_CONTROL, AUX_SW_DONE_USING_AUX_REG, 1);
}

static void destruct(
	struct aux_engine_dce110 *engine);

static void destroy(
	struct aux_engine **aux_engine)
{
	struct aux_engine_dce110 *engine = FROM_AUX_ENGINE(*aux_engine);

	destruct(engine);

	kfree(engine);

	*aux_engine = NULL;
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

	/* set the delay and the number of bytes to write */

	/* The length include
	 * the 4 bit header and the 20 bit address
	 * (that is 3 byte).
	 * If the requested length is non zero this means
	 * an addition byte specifying the length is required. */

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
		 * processed in process_channel_reply(). */
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

static void process_channel_reply(
	struct aux_engine *engine,
	struct aux_reply_transaction_data *reply)
{
	struct aux_engine_dce110 *aux110 = FROM_AUX_ENGINE(engine);

	/* Need to do a read to get the number of bytes to process
	 * Alternatively, this information can be passed -
	 * but that causes coupling which isn't good either. */

	uint32_t bytes_replied;
	uint32_t value;

	value = REG_GET(AUX_SW_STATUS,
			AUX_SW_REPLY_BYTE_COUNT, &bytes_replied);

	if (bytes_replied) {
		uint32_t reply_result;

		REG_UPDATE_1BY1_3(AUX_SW_DATA,
				AUX_SW_INDEX, 0,
				AUX_SW_AUTOINCREMENT_DISABLE, 1,
				AUX_SW_DATA_RW, 1);

		REG_GET(AUX_SW_DATA,
				AUX_SW_DATA, &reply_result);

		reply_result = reply_result >> 4;

		switch (reply_result) {
		case 0: /* ACK */ {
			uint32_t i = 0;

			/* first byte was already used
			 * to get the command status */
			--bytes_replied;

			while (i < bytes_replied) {
				uint32_t aux_sw_data_val;

				REG_GET(AUX_SW_DATA,
						AUX_SW_DATA, &aux_sw_data_val);

				reply->data[i] = aux_sw_data_val;
				++i;
			}

			reply->status = AUX_TRANSACTION_REPLY_AUX_ACK;
		}
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
	} else {
		/* Need to handle an error case...
		 * hopefully, upper layer function won't call this function
		 * if the number of bytes in the reply was 0
		 * because there was surely an error that was asserted
		 * that should have been handled
		 * for hot plug case, this could happens*/
		if (!(value & AUX_SW_STATUS__AUX_SW_HPD_DISCON_MASK))
			ASSERT_CRITICAL(false);
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

	/* Note that the following bits are set in 'status.bits'
	 * during CTS 4.2.1.2 (FW 3.3.1):
	 * AUX_SW_RX_MIN_COUNT_VIOL, AUX_SW_RX_INVALID_STOP,
	 * AUX_SW_RX_RECV_NO_DET, AUX_SW_RX_RECV_INVALID_H.
	 *
	 * AUX_SW_RX_MIN_COUNT_VIOL is an internal,
	 * HW debugging bit and should be ignored. */
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
		/*time_elapsed >= aux_engine->timeout_period */
		if (!(value & AUX_SW_STATUS__AUX_SW_HPD_DISCON_MASK))
			ASSERT_CRITICAL(false);

		return AUX_CHANNEL_OPERATION_FAILED_TIMEOUT;
	}
}

static const struct aux_engine_funcs aux_engine_funcs = {
	.destroy = destroy,
	.acquire_engine = acquire_engine,
	.submit_channel_request = submit_channel_request,
	.process_channel_reply = process_channel_reply,
	.get_channel_status = get_channel_status,
	.is_engine_available = is_engine_available,
};

static const struct engine_funcs engine_funcs = {
	.release_engine = release_engine,
	.submit_request = dal_aux_engine_submit_request,
	.get_engine_type = dal_aux_engine_get_engine_type,
	.acquire = dal_aux_engine_acquire,
};

static void construct(
	struct aux_engine_dce110 *engine,
	const struct aux_engine_dce110_init_data *aux_init_data)
{
	dal_aux_engine_construct(&engine->base, aux_init_data->ctx);
	engine->base.base.funcs = &engine_funcs;
	engine->base.funcs = &aux_engine_funcs;

	engine->timeout_period = aux_init_data->timeout_period;
	engine->regs = aux_init_data->regs;
}

static void destruct(
	struct aux_engine_dce110 *engine)
{
	dal_aux_engine_destruct(&engine->base);
}

struct aux_engine *dal_aux_engine_dce110_create(
	const struct aux_engine_dce110_init_data *aux_init_data)
{
	struct aux_engine_dce110 *engine;

	if (!aux_init_data) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	engine = kzalloc(sizeof(*engine), GFP_KERNEL);

	if (!engine) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	construct(engine, aux_init_data);
	return &engine->base;
}
