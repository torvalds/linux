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
#include "dce_i2c_hw.h"
#include "reg_helper.h"
#include "include/gpio_service_interface.h"

#define CTX \
	dce_i2c_hw->ctx
#define REG(reg)\
	dce_i2c_hw->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	dce_i2c_hw->shifts->field_name, dce_i2c_hw->masks->field_name

static void execute_transaction(
	struct dce_i2c_hw *dce_i2c_hw)
{
	REG_UPDATE_N(SETUP, 5,
		     FN(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_DATA_DRIVE_EN), 0,
		     FN(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_CLK_DRIVE_EN), 0,
		     FN(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_DATA_DRIVE_SEL), 0,
		     FN(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_INTRA_TRANSACTION_DELAY), 0,
		     FN(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_INTRA_BYTE_DELAY), 0);


	REG_UPDATE_5(DC_I2C_CONTROL,
		     DC_I2C_SOFT_RESET, 0,
		     DC_I2C_SW_STATUS_RESET, 0,
		     DC_I2C_SEND_RESET, 0,
		     DC_I2C_GO, 0,
		     DC_I2C_TRANSACTION_COUNT, dce_i2c_hw->transaction_count - 1);

	/* start I2C transfer */
	REG_UPDATE(DC_I2C_CONTROL, DC_I2C_GO, 1);

	/* all transactions were executed and HW buffer became empty
	 * (even though it actually happens when status becomes DONE)
	 */
	dce_i2c_hw->transaction_count = 0;
	dce_i2c_hw->buffer_used_bytes = 0;
}

static enum i2c_channel_operation_result get_channel_status(
	struct dce_i2c_hw *dce_i2c_hw,
	uint8_t *returned_bytes)
{
	uint32_t i2c_sw_status = 0;
	uint32_t value =
		REG_GET(DC_I2C_SW_STATUS, DC_I2C_SW_STATUS, &i2c_sw_status);
	if (i2c_sw_status == DC_I2C_STATUS__DC_I2C_STATUS_USED_BY_SW)
		return I2C_CHANNEL_OPERATION_ENGINE_BUSY;
	else if (value & dce_i2c_hw->masks->DC_I2C_SW_STOPPED_ON_NACK)
		return I2C_CHANNEL_OPERATION_NO_RESPONSE;
	else if (value & dce_i2c_hw->masks->DC_I2C_SW_TIMEOUT)
		return I2C_CHANNEL_OPERATION_TIMEOUT;
	else if (value & dce_i2c_hw->masks->DC_I2C_SW_ABORTED)
		return I2C_CHANNEL_OPERATION_FAILED;
	else if (value & dce_i2c_hw->masks->DC_I2C_SW_DONE)
		return I2C_CHANNEL_OPERATION_SUCCEEDED;

	/*
	 * this is the case when HW used for communication, I2C_SW_STATUS
	 * could be zero
	 */
	return I2C_CHANNEL_OPERATION_SUCCEEDED;
}

static uint32_t get_hw_buffer_available_size(
	const struct dce_i2c_hw *dce_i2c_hw)
{
	return dce_i2c_hw->buffer_size -
			dce_i2c_hw->buffer_used_bytes;
}

uint32_t get_reference_clock(
		struct dc_bios *bios)
{
	struct dc_firmware_info info = { { 0 } };

	if (bios->funcs->get_firmware_info(bios, &info) != BP_RESULT_OK)
		return 0;

	return info.pll_info.crystal_frequency;
}

static uint32_t get_speed(
	const struct dce_i2c_hw *dce_i2c_hw)
{
	uint32_t pre_scale = 0;

	REG_GET(SPEED, DC_I2C_DDC1_PRESCALE, &pre_scale);

	/* [anaumov] it seems following is unnecessary */
	/*ASSERT(value.bits.DC_I2C_DDC1_PRESCALE);*/
	return pre_scale ?
		dce_i2c_hw->reference_frequency / pre_scale :
		dce_i2c_hw->default_speed;
}

static void process_channel_reply(
	struct dce_i2c_hw *dce_i2c_hw,
	struct i2c_payload *reply)
{
	uint32_t length = reply->length;
	uint8_t *buffer = reply->data;

	REG_SET_3(DC_I2C_DATA, 0,
		 DC_I2C_INDEX, dce_i2c_hw->buffer_used_write,
		 DC_I2C_DATA_RW, 1,
		 DC_I2C_INDEX_WRITE, 1);

	while (length) {
		/* after reading the status,
		 * if the I2C operation executed successfully
		 * (i.e. DC_I2C_STATUS_DONE = 1) then the I2C controller
		 * should read data bytes from I2C circular data buffer
		 */

		uint32_t i2c_data;

		REG_GET(DC_I2C_DATA, DC_I2C_DATA, &i2c_data);
		*buffer++ = i2c_data;

		--length;
	}
}

static bool process_transaction(
	struct dce_i2c_hw *dce_i2c_hw,
	struct i2c_request_transaction_data *request)
{
	uint32_t length = request->length;
	uint8_t *buffer = request->data;

	bool last_transaction = false;
	uint32_t value = 0;

	last_transaction = ((dce_i2c_hw->transaction_count == 3) ||
			(request->action == DCE_I2C_TRANSACTION_ACTION_I2C_WRITE) ||
			(request->action & DCE_I2C_TRANSACTION_ACTION_I2C_READ));


	switch (dce_i2c_hw->transaction_count) {
	case 0:
		REG_UPDATE_5(DC_I2C_TRANSACTION0,
				 DC_I2C_STOP_ON_NACK0, 1,
				 DC_I2C_START0, 1,
				 DC_I2C_RW0, 0 != (request->action & DCE_I2C_TRANSACTION_ACTION_I2C_READ),
				 DC_I2C_COUNT0, length,
				 DC_I2C_STOP0, last_transaction ? 1 : 0);
		break;
	case 1:
		REG_UPDATE_5(DC_I2C_TRANSACTION1,
				 DC_I2C_STOP_ON_NACK0, 1,
				 DC_I2C_START0, 1,
				 DC_I2C_RW0, 0 != (request->action & DCE_I2C_TRANSACTION_ACTION_I2C_READ),
				 DC_I2C_COUNT0, length,
				 DC_I2C_STOP0, last_transaction ? 1 : 0);
		break;
	case 2:
		REG_UPDATE_5(DC_I2C_TRANSACTION2,
				 DC_I2C_STOP_ON_NACK0, 1,
				 DC_I2C_START0, 1,
				 DC_I2C_RW0, 0 != (request->action & DCE_I2C_TRANSACTION_ACTION_I2C_READ),
				 DC_I2C_COUNT0, length,
				 DC_I2C_STOP0, last_transaction ? 1 : 0);
		break;
	case 3:
		REG_UPDATE_5(DC_I2C_TRANSACTION3,
				 DC_I2C_STOP_ON_NACK0, 1,
				 DC_I2C_START0, 1,
				 DC_I2C_RW0, 0 != (request->action & DCE_I2C_TRANSACTION_ACTION_I2C_READ),
				 DC_I2C_COUNT0, length,
				 DC_I2C_STOP0, last_transaction ? 1 : 0);
		break;
	default:
		/* TODO Warning ? */
		break;
	}

	/* Write the I2C address and I2C data
	 * into the hardware circular buffer, one byte per entry.
	 * As an example, the 7-bit I2C slave address for CRT monitor
	 * for reading DDC/EDID information is 0b1010001.
	 * For an I2C send operation, the LSB must be programmed to 0;
	 * for I2C receive operation, the LSB must be programmed to 1.
	 */
	if (dce_i2c_hw->transaction_count == 0) {
		value = REG_SET_4(DC_I2C_DATA, 0,
				  DC_I2C_DATA_RW, false,
				  DC_I2C_DATA, request->address,
				  DC_I2C_INDEX, 0,
				  DC_I2C_INDEX_WRITE, 1);
		dce_i2c_hw->buffer_used_write = 0;
	} else
		value = REG_SET_2(DC_I2C_DATA, 0,
			  DC_I2C_DATA_RW, false,
			  DC_I2C_DATA, request->address);

	dce_i2c_hw->buffer_used_write++;

	if (!(request->action & DCE_I2C_TRANSACTION_ACTION_I2C_READ)) {
		while (length) {
			REG_SET_2(DC_I2C_DATA, value,
				  DC_I2C_INDEX_WRITE, 0,
				  DC_I2C_DATA, *buffer++);
			dce_i2c_hw->buffer_used_write++;
			--length;
		}
	}

	++dce_i2c_hw->transaction_count;
	dce_i2c_hw->buffer_used_bytes += length + 1;

	return last_transaction;
}

static inline void reset_hw_engine(struct dce_i2c_hw *dce_i2c_hw)
{
	REG_UPDATE_2(DC_I2C_CONTROL,
		     DC_I2C_SW_STATUS_RESET, 1,
		     DC_I2C_SW_STATUS_RESET, 1);
}

static void set_speed(
	struct dce_i2c_hw *dce_i2c_hw,
	uint32_t speed)
{

	if (speed) {
		if (dce_i2c_hw->masks->DC_I2C_DDC1_START_STOP_TIMING_CNTL)
			REG_UPDATE_N(SPEED, 3,
				     FN(DC_I2C_DDC1_SPEED, DC_I2C_DDC1_PRESCALE), dce_i2c_hw->reference_frequency / speed,
				     FN(DC_I2C_DDC1_SPEED, DC_I2C_DDC1_THRESHOLD), 2,
				     FN(DC_I2C_DDC1_SPEED, DC_I2C_DDC1_START_STOP_TIMING_CNTL), speed > 50 ? 2:1);
		else
			REG_UPDATE_N(SPEED, 2,
				     FN(DC_I2C_DDC1_SPEED, DC_I2C_DDC1_PRESCALE), dce_i2c_hw->reference_frequency / speed,
				     FN(DC_I2C_DDC1_SPEED, DC_I2C_DDC1_THRESHOLD), 2);
	}
}

static bool setup_engine(
	struct dce_i2c_hw *dce_i2c_hw)
{
	uint32_t i2c_setup_limit = I2C_SETUP_TIME_LIMIT_DCE;

	if (dce_i2c_hw->setup_limit != 0)
		i2c_setup_limit = dce_i2c_hw->setup_limit;
	/* Program pin select */
	REG_UPDATE_6(DC_I2C_CONTROL,
		     DC_I2C_GO, 0,
		     DC_I2C_SOFT_RESET, 0,
		     DC_I2C_SEND_RESET, 0,
		     DC_I2C_SW_STATUS_RESET, 1,
		     DC_I2C_TRANSACTION_COUNT, 0,
		     DC_I2C_DDC_SELECT, dce_i2c_hw->engine_id);

	/* Program time limit */
	if (dce_i2c_hw->send_reset_length == 0) {
		/*pre-dcn*/
		REG_UPDATE_N(SETUP, 2,
			     FN(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_TIME_LIMIT), i2c_setup_limit,
			     FN(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_ENABLE), 1);
	}
	/* Program HW priority
	 * set to High - interrupt software I2C at any time
	 * Enable restart of SW I2C that was interrupted by HW
	 * disable queuing of software while I2C is in use by HW
	 */
	REG_UPDATE_2(DC_I2C_ARBITRATION,
		     DC_I2C_NO_QUEUED_SW_GO, 0,
		     DC_I2C_SW_PRIORITY, DC_I2C_ARBITRATION__DC_I2C_SW_PRIORITY_NORMAL);

	return true;
}

static bool is_hw_busy(struct dce_i2c_hw *dce_i2c_hw)
{
	uint32_t i2c_sw_status = 0;

	REG_GET(DC_I2C_SW_STATUS, DC_I2C_SW_STATUS, &i2c_sw_status);
	if (i2c_sw_status == DC_I2C_STATUS__DC_I2C_STATUS_IDLE)
		return false;

	reset_hw_engine(dce_i2c_hw);

	REG_GET(DC_I2C_SW_STATUS, DC_I2C_SW_STATUS, &i2c_sw_status);
	return i2c_sw_status != DC_I2C_STATUS__DC_I2C_STATUS_IDLE;
}

static void release_engine(
	struct dce_i2c_hw *dce_i2c_hw)
{
	bool safe_to_reset;

	/* Restore original HW engine speed */

	set_speed(dce_i2c_hw, dce_i2c_hw->original_speed);

	/* Release I2C */
	REG_UPDATE(DC_I2C_ARBITRATION, DC_I2C_SW_DONE_USING_I2C_REG, 1);

	/* Reset HW engine */
	{
		uint32_t i2c_sw_status = 0;

		REG_GET(DC_I2C_SW_STATUS, DC_I2C_SW_STATUS, &i2c_sw_status);
		/* if used by SW, safe to reset */
		safe_to_reset = (i2c_sw_status == 1);
	}

	if (safe_to_reset)
		REG_UPDATE_2(DC_I2C_CONTROL,
			     DC_I2C_SOFT_RESET, 1,
			     DC_I2C_SW_STATUS_RESET, 1);
	else
		REG_UPDATE(DC_I2C_CONTROL, DC_I2C_SW_STATUS_RESET, 1);
	/* HW I2c engine - clock gating feature */
	if (!dce_i2c_hw->engine_keep_power_up_count)
		REG_UPDATE_N(SETUP, 1, FN(SETUP, DC_I2C_DDC1_ENABLE), 0);

}

struct dce_i2c_hw *acquire_i2c_hw_engine(
	struct resource_pool *pool,
	struct ddc *ddc)
{
	uint32_t counter = 0;
	enum gpio_result result;
	uint32_t current_speed;
	struct dce_i2c_hw *dce_i2c_hw = NULL;

	if (!ddc)
		return NULL;

	if (ddc->hw_info.hw_supported) {
		enum gpio_ddc_line line = dal_ddc_get_line(ddc);

		if (line < pool->pipe_count)
			dce_i2c_hw = pool->hw_i2cs[line];
	}

	if (!dce_i2c_hw)
		return NULL;

	if (pool->i2c_hw_buffer_in_use)
		return NULL;

	do {
		result = dal_ddc_open(ddc, GPIO_MODE_HARDWARE,
			GPIO_DDC_CONFIG_TYPE_MODE_I2C);

		if (result == GPIO_RESULT_OK)
			break;

		/* i2c_engine is busy by VBios, lets wait and retry */

		udelay(10);

		++counter;
	} while (counter < 2);

	if (result != GPIO_RESULT_OK)
		return NULL;

	dce_i2c_hw->ddc = ddc;

	current_speed = get_speed(dce_i2c_hw);

	if (current_speed)
		dce_i2c_hw->original_speed = current_speed;

	if (!setup_engine(dce_i2c_hw)) {
		release_engine(dce_i2c_hw);
		return NULL;
	}

	pool->i2c_hw_buffer_in_use = true;
	return dce_i2c_hw;
}

enum i2c_channel_operation_result dce_i2c_hw_engine_wait_on_operation_result(
	struct dce_i2c_hw *dce_i2c_hw,
	uint32_t timeout,
	enum i2c_channel_operation_result expected_result)
{
	enum i2c_channel_operation_result result;
	uint32_t i = 0;

	if (!timeout)
		return I2C_CHANNEL_OPERATION_SUCCEEDED;

	do {

		result = get_channel_status(
				dce_i2c_hw, NULL);

		if (result != expected_result)
			break;

		udelay(1);

		++i;
	} while (i < timeout);
	return result;
}

static void submit_channel_request_hw(
	struct dce_i2c_hw *dce_i2c_hw,
	struct i2c_request_transaction_data *request)
{
	request->status = I2C_CHANNEL_OPERATION_SUCCEEDED;

	if (!process_transaction(dce_i2c_hw, request))
		return;

	if (is_hw_busy(dce_i2c_hw)) {
		request->status = I2C_CHANNEL_OPERATION_ENGINE_BUSY;
		return;
	}

	execute_transaction(dce_i2c_hw);


}

static uint32_t get_transaction_timeout_hw(
	const struct dce_i2c_hw *dce_i2c_hw,
	uint32_t length)
{

	uint32_t speed = get_speed(dce_i2c_hw);



	uint32_t period_timeout;
	uint32_t num_of_clock_stretches;

	if (!speed)
		return 0;

	period_timeout = (1000 * TRANSACTION_TIMEOUT_IN_I2C_CLOCKS) / speed;

	num_of_clock_stretches = 1 + (length << 3) + 1;
	num_of_clock_stretches +=
		(dce_i2c_hw->buffer_used_bytes << 3) +
		(dce_i2c_hw->transaction_count << 1);

	return period_timeout * num_of_clock_stretches;
}

bool dce_i2c_hw_engine_submit_payload(
	struct dce_i2c_hw *dce_i2c_hw,
	struct i2c_payload *payload,
	bool middle_of_transaction)
{

	struct i2c_request_transaction_data request;

	uint32_t transaction_timeout;

	enum i2c_channel_operation_result operation_result;

	bool result = false;

	/* We need following:
	 * transaction length will not exceed
	 * the number of free bytes in HW buffer (minus one for address)
	 */

	if (payload->length >=
			get_hw_buffer_available_size(dce_i2c_hw)) {
		return false;
	}

	if (!payload->write)
		request.action = middle_of_transaction ?
			DCE_I2C_TRANSACTION_ACTION_I2C_READ_MOT :
			DCE_I2C_TRANSACTION_ACTION_I2C_READ;
	else
		request.action = middle_of_transaction ?
			DCE_I2C_TRANSACTION_ACTION_I2C_WRITE_MOT :
			DCE_I2C_TRANSACTION_ACTION_I2C_WRITE;


	request.address = (uint8_t) ((payload->address << 1) | !payload->write);
	request.length = payload->length;
	request.data = payload->data;

	/* obtain timeout value before submitting request */

	transaction_timeout = get_transaction_timeout_hw(
		dce_i2c_hw, payload->length + 1);

	submit_channel_request_hw(
		dce_i2c_hw, &request);

	if ((request.status == I2C_CHANNEL_OPERATION_FAILED) ||
		(request.status == I2C_CHANNEL_OPERATION_ENGINE_BUSY))
		return false;

	/* wait until transaction proceed */

	operation_result = dce_i2c_hw_engine_wait_on_operation_result(
		dce_i2c_hw,
		transaction_timeout,
		I2C_CHANNEL_OPERATION_ENGINE_BUSY);

	/* update transaction status */

	if (operation_result == I2C_CHANNEL_OPERATION_SUCCEEDED)
		result = true;

	if (result && (!payload->write))
		process_channel_reply(dce_i2c_hw, payload);

	return result;
}

bool dce_i2c_submit_command_hw(
	struct resource_pool *pool,
	struct ddc *ddc,
	struct i2c_command *cmd,
	struct dce_i2c_hw *dce_i2c_hw)
{
	uint8_t index_of_payload = 0;
	bool result;

	set_speed(dce_i2c_hw, cmd->speed);

	result = true;

	while (index_of_payload < cmd->number_of_payloads) {
		bool mot = (index_of_payload != cmd->number_of_payloads - 1);

		struct i2c_payload *payload = cmd->payloads + index_of_payload;

		if (!dce_i2c_hw_engine_submit_payload(
				dce_i2c_hw, payload, mot)) {
			result = false;
			break;
		}



		++index_of_payload;
	}

	pool->i2c_hw_buffer_in_use = false;

	release_engine(dce_i2c_hw);
	dal_ddc_close(dce_i2c_hw->ddc);

	dce_i2c_hw->ddc = NULL;

	return result;
}

void dce_i2c_hw_construct(
	struct dce_i2c_hw *dce_i2c_hw,
	struct dc_context *ctx,
	uint32_t engine_id,
	const struct dce_i2c_registers *regs,
	const struct dce_i2c_shift *shifts,
	const struct dce_i2c_mask *masks)
{
	dce_i2c_hw->ctx = ctx;
	dce_i2c_hw->engine_id = engine_id;
	dce_i2c_hw->reference_frequency = get_reference_clock(ctx->dc_bios) >> 1;
	dce_i2c_hw->regs = regs;
	dce_i2c_hw->shifts = shifts;
	dce_i2c_hw->masks = masks;
	dce_i2c_hw->buffer_used_bytes = 0;
	dce_i2c_hw->transaction_count = 0;
	dce_i2c_hw->engine_keep_power_up_count = 1;
	dce_i2c_hw->original_speed = DEFAULT_I2C_HW_SPEED;
	dce_i2c_hw->default_speed = DEFAULT_I2C_HW_SPEED;
	dce_i2c_hw->send_reset_length = 0;
	dce_i2c_hw->setup_limit = I2C_SETUP_TIME_LIMIT_DCE;
	dce_i2c_hw->buffer_size = I2C_HW_BUFFER_SIZE_DCE;
}

void dce100_i2c_hw_construct(
	struct dce_i2c_hw *dce_i2c_hw,
	struct dc_context *ctx,
	uint32_t engine_id,
	const struct dce_i2c_registers *regs,
	const struct dce_i2c_shift *shifts,
	const struct dce_i2c_mask *masks)
{

	uint32_t xtal_ref_div = 0;

	dce_i2c_hw_construct(dce_i2c_hw,
			ctx,
			engine_id,
			regs,
			shifts,
			masks);
	dce_i2c_hw->buffer_size = I2C_HW_BUFFER_SIZE_DCE100;

	REG_GET(MICROSECOND_TIME_BASE_DIV, XTAL_REF_DIV, &xtal_ref_div);

	if (xtal_ref_div == 0)
		xtal_ref_div = 2;

	/*Calculating Reference Clock by divding original frequency by
	 * XTAL_REF_DIV.
	 * At upper level, uint32_t reference_frequency =
	 *  dal_dce_i2c_get_reference_clock(as) >> 1
	 *  which already divided by 2. So we need x2 to get original
	 *  reference clock from ppll_info
	 */
	dce_i2c_hw->reference_frequency =
		(dce_i2c_hw->reference_frequency * 2) / xtal_ref_div;
}

void dce112_i2c_hw_construct(
	struct dce_i2c_hw *dce_i2c_hw,
	struct dc_context *ctx,
	uint32_t engine_id,
	const struct dce_i2c_registers *regs,
	const struct dce_i2c_shift *shifts,
	const struct dce_i2c_mask *masks)
{
	dce100_i2c_hw_construct(dce_i2c_hw,
			ctx,
			engine_id,
			regs,
			shifts,
			masks);
	dce_i2c_hw->default_speed = DEFAULT_I2C_HW_SPEED_100KHZ;
}

void dcn1_i2c_hw_construct(
	struct dce_i2c_hw *dce_i2c_hw,
	struct dc_context *ctx,
	uint32_t engine_id,
	const struct dce_i2c_registers *regs,
	const struct dce_i2c_shift *shifts,
	const struct dce_i2c_mask *masks)
{
	dce112_i2c_hw_construct(dce_i2c_hw,
			ctx,
			engine_id,
			regs,
			shifts,
			masks);
	dce_i2c_hw->setup_limit = I2C_SETUP_TIME_LIMIT_DCN;
}

