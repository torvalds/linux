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
#include "core_types.h"
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
	FROM_AUX_ENGINE(container_of((ptr), struct dce_aux, base))

#define FROM_AUX_ENGINE_ENGINE(ptr) \
	container_of((ptr), struct dce_aux, base)
enum {
	AUX_INVALID_REPLY_RETRY_COUNTER = 1,
	AUX_TIMED_OUT_RETRY_COUNTER = 2,
	AUX_DEFER_RETRY_COUNTER = 6
};
static void release_engine(
	struct dce_aux *engine)
{
	struct aux_engine_dce110 *aux110 = FROM_AUX_ENGINE(engine);

	dal_ddc_close(engine->ddc);

	engine->ddc = NULL;

	REG_UPDATE(AUX_ARB_CONTROL, AUX_SW_DONE_USING_AUX_REG, 1);
}

#define SW_CAN_ACCESS_AUX 1
#define DMCU_CAN_ACCESS_AUX 2

static bool is_engine_available(
	struct dce_aux *engine)
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
	struct dce_aux *engine)
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
	struct dce_aux *engine,
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

	REG_UPDATE(AUX_INTERRUPT_CONTROL, AUX_SW_DONE_ACK, 1);

	REG_WAIT(AUX_SW_STATUS, AUX_SW_DONE, 0,
				10, aux110->timeout_period/10);

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

	REG_UPDATE(AUX_SW_CONTROL, AUX_SW_GO, 1);
}

static int read_channel_reply(struct dce_aux *engine, uint32_t size,
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
	if (reply_result != NULL)
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

static enum aux_channel_operation_result get_channel_status(
	struct dce_aux *engine,
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

enum i2caux_engine_type get_engine_type(
		const struct dce_aux *engine)
{
	return I2CAUX_ENGINE_TYPE_AUX;
}

static bool acquire(
	struct dce_aux *engine,
	struct ddc *ddc)
{

	enum gpio_result result;

	if (!is_engine_available(engine))
		return false;

	result = dal_ddc_open(ddc, GPIO_MODE_HARDWARE,
		GPIO_DDC_CONFIG_TYPE_MODE_AUX);

	if (result != GPIO_RESULT_OK)
		return false;

	if (!acquire_engine(engine)) {
		dal_ddc_close(ddc);
		return false;
	}

	engine->ddc = ddc;

	return true;
}

void dce110_engine_destroy(struct dce_aux **engine)
{

	struct aux_engine_dce110 *engine110 = FROM_AUX_ENGINE(*engine);

	kfree(engine110);
	*engine = NULL;

}
struct dce_aux *dce110_aux_engine_construct(struct aux_engine_dce110 *aux_engine110,
		struct dc_context *ctx,
		uint32_t inst,
		uint32_t timeout_period,
		const struct dce110_aux_registers *regs)
{
	aux_engine110->base.ddc = NULL;
	aux_engine110->base.ctx = ctx;
	aux_engine110->base.delay = 0;
	aux_engine110->base.max_defer_write_retry = 0;
	aux_engine110->base.inst = inst;
	aux_engine110->timeout_period = timeout_period;
	aux_engine110->regs = regs;

	return &aux_engine110->base;
}

static enum i2caux_transaction_action i2caux_action_from_payload(struct aux_payload *payload)
{
	if (payload->i2c_over_aux) {
		if (payload->write) {
			if (payload->mot)
				return I2CAUX_TRANSACTION_ACTION_I2C_WRITE_MOT;
			return I2CAUX_TRANSACTION_ACTION_I2C_WRITE;
		}
		if (payload->mot)
			return I2CAUX_TRANSACTION_ACTION_I2C_READ_MOT;
		return I2CAUX_TRANSACTION_ACTION_I2C_READ;
	}
	if (payload->write)
		return I2CAUX_TRANSACTION_ACTION_DP_WRITE;
	return I2CAUX_TRANSACTION_ACTION_DP_READ;
}

int dce_aux_transfer(struct ddc_service *ddc,
		struct aux_payload *payload)
{
	struct ddc *ddc_pin = ddc->ddc_pin;
	struct dce_aux *aux_engine;
	enum aux_channel_operation_result operation_result;
	struct aux_request_transaction_data aux_req;
	struct aux_reply_transaction_data aux_rep;
	uint8_t returned_bytes = 0;
	int res = -1;
	uint32_t status;

	memset(&aux_req, 0, sizeof(aux_req));
	memset(&aux_rep, 0, sizeof(aux_rep));

	aux_engine = ddc->ctx->dc->res_pool->engines[ddc_pin->pin_data->en];
	acquire(aux_engine, ddc_pin);

	if (payload->i2c_over_aux)
		aux_req.type = AUX_TRANSACTION_TYPE_I2C;
	else
		aux_req.type = AUX_TRANSACTION_TYPE_DP;

	aux_req.action = i2caux_action_from_payload(payload);

	aux_req.address = payload->address;
	aux_req.delay = payload->defer_delay * 10;
	aux_req.length = payload->length;
	aux_req.data = payload->data;

	submit_channel_request(aux_engine, &aux_req);
	operation_result = get_channel_status(aux_engine, &returned_bytes);

	switch (operation_result) {
	case AUX_CHANNEL_OPERATION_SUCCEEDED:
		res = read_channel_reply(aux_engine, payload->length,
							payload->data, payload->reply,
							&status);
		break;
	case AUX_CHANNEL_OPERATION_FAILED_HPD_DISCON:
		res = 0;
		break;
	case AUX_CHANNEL_OPERATION_FAILED_REASON_UNKNOWN:
	case AUX_CHANNEL_OPERATION_FAILED_INVALID_REPLY:
	case AUX_CHANNEL_OPERATION_FAILED_TIMEOUT:
		res = -1;
		break;
	}
	release_engine(aux_engine);
	return res;
}

#define AUX_RETRY_MAX 7

bool dce_aux_transfer_with_retries(struct ddc_service *ddc,
		struct aux_payload *payload)
{
	int i, ret = 0;
	uint8_t reply;
	bool payload_reply = true;

	if (!payload->reply) {
		payload_reply = false;
		payload->reply = &reply;
	}

	for (i = 0; i < AUX_RETRY_MAX; i++) {
		ret = dce_aux_transfer(ddc, payload);

		if (ret >= 0) {
			if (*payload->reply == 0) {
				if (!payload_reply)
					payload->reply = NULL;
				return true;
			}
		}

		udelay(1000);
	}
	return false;
}
