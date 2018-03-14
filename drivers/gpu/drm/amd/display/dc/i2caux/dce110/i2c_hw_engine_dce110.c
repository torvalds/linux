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
#include "include/logger_interface.h"
/*
 * Pre-requisites: headers required by header of this unit
 */

#include "include/i2caux_interface.h"
#include "../engine.h"
#include "../i2c_engine.h"
#include "../i2c_hw_engine.h"
#include "../i2c_generic_hw_engine.h"
/*
 * Header of this unit
 */

#include "i2c_hw_engine_dce110.h"

/*
 * Post-requisites: headers required by this unit
 */
#include "reg_helper.h"

/*
 * This unit
 */
#define DC_LOGGER \
		hw_engine->base.base.base.ctx->logger

enum dc_i2c_status {
	DC_I2C_STATUS__DC_I2C_STATUS_IDLE,
	DC_I2C_STATUS__DC_I2C_STATUS_USED_BY_SW,
	DC_I2C_STATUS__DC_I2C_STATUS_USED_BY_HW
};

enum dc_i2c_arbitration {
	DC_I2C_ARBITRATION__DC_I2C_SW_PRIORITY_NORMAL,
	DC_I2C_ARBITRATION__DC_I2C_SW_PRIORITY_HIGH
};

enum {
	/* No timeout in HW
	 * (timeout implemented in SW by querying status) */
	I2C_SETUP_TIME_LIMIT = 255,
	I2C_HW_BUFFER_SIZE = 538
};

/*
 * @brief
 * Cast pointer to 'struct i2c_hw_engine *'
 * to pointer 'struct i2c_hw_engine_dce110 *'
 */
#define FROM_I2C_HW_ENGINE(ptr) \
	container_of((ptr), struct i2c_hw_engine_dce110, base)
/*
 * @brief
 * Cast pointer to 'struct i2c_engine *'
 * to pointer to 'struct i2c_hw_engine_dce110 *'
 */
#define FROM_I2C_ENGINE(ptr) \
	FROM_I2C_HW_ENGINE(container_of((ptr), struct i2c_hw_engine, base))

/*
 * @brief
 * Cast pointer to 'struct engine *'
 * to 'pointer to struct i2c_hw_engine_dce110 *'
 */
#define FROM_ENGINE(ptr) \
	FROM_I2C_ENGINE(container_of((ptr), struct i2c_engine, base))

#define CTX \
		hw_engine->base.base.base.ctx

#define REG(reg_name)\
	(hw_engine->regs->reg_name)

#undef FN
#define FN(reg_name, field_name) \
	hw_engine->i2c_shift->field_name, hw_engine->i2c_mask->field_name

#include "reg_helper.h"

static void disable_i2c_hw_engine(
	struct i2c_hw_engine_dce110 *hw_engine)
{
	REG_UPDATE_N(SETUP, 1, FN(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_ENABLE), 0);
}

static void release_engine(
	struct engine *engine)
{
	struct i2c_hw_engine_dce110 *hw_engine = FROM_ENGINE(engine);

	struct i2c_engine *base = NULL;
	bool safe_to_reset;

	base = &hw_engine->base.base;

	/* Restore original HW engine speed */

	base->funcs->set_speed(base, hw_engine->base.original_speed);

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
		REG_UPDATE_2(
			DC_I2C_CONTROL,
			DC_I2C_SOFT_RESET, 1,
			DC_I2C_SW_STATUS_RESET, 1);
	else
		REG_UPDATE(DC_I2C_CONTROL, DC_I2C_SW_STATUS_RESET, 1);

	/* HW I2c engine - clock gating feature */
	if (!hw_engine->engine_keep_power_up_count)
		disable_i2c_hw_engine(hw_engine);
}

static bool setup_engine(
	struct i2c_engine *i2c_engine)
{
	struct i2c_hw_engine_dce110 *hw_engine = FROM_I2C_ENGINE(i2c_engine);

	/* Program pin select */
	REG_UPDATE_6(
			DC_I2C_CONTROL,
			DC_I2C_GO, 0,
			DC_I2C_SOFT_RESET, 0,
			DC_I2C_SEND_RESET, 0,
			DC_I2C_SW_STATUS_RESET, 1,
			DC_I2C_TRANSACTION_COUNT, 0,
			DC_I2C_DDC_SELECT, hw_engine->engine_id);

	/* Program time limit */
	REG_UPDATE_N(
			SETUP, 2,
			FN(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_TIME_LIMIT), I2C_SETUP_TIME_LIMIT,
			FN(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_ENABLE), 1);

	/* Program HW priority
	 * set to High - interrupt software I2C at any time
	 * Enable restart of SW I2C that was interrupted by HW
	 * disable queuing of software while I2C is in use by HW */
	REG_UPDATE_2(
			DC_I2C_ARBITRATION,
			DC_I2C_NO_QUEUED_SW_GO, 0,
			DC_I2C_SW_PRIORITY, DC_I2C_ARBITRATION__DC_I2C_SW_PRIORITY_NORMAL);

	return true;
}

static uint32_t get_speed(
	const struct i2c_engine *i2c_engine)
{
	const struct i2c_hw_engine_dce110 *hw_engine = FROM_I2C_ENGINE(i2c_engine);
	uint32_t pre_scale = 0;

	REG_GET(SPEED, DC_I2C_DDC1_PRESCALE, &pre_scale);

	/* [anaumov] it seems following is unnecessary */
	/*ASSERT(value.bits.DC_I2C_DDC1_PRESCALE);*/
	return pre_scale ?
		hw_engine->reference_frequency / pre_scale :
		hw_engine->base.default_speed;
}

static void set_speed(
	struct i2c_engine *i2c_engine,
	uint32_t speed)
{
	struct i2c_hw_engine_dce110 *hw_engine = FROM_I2C_ENGINE(i2c_engine);

	if (speed) {
		if (hw_engine->i2c_mask->DC_I2C_DDC1_START_STOP_TIMING_CNTL)
			REG_UPDATE_N(
				SPEED, 3,
				FN(DC_I2C_DDC1_SPEED, DC_I2C_DDC1_PRESCALE), hw_engine->reference_frequency / speed,
				FN(DC_I2C_DDC1_SPEED, DC_I2C_DDC1_THRESHOLD), 2,
				FN(DC_I2C_DDC1_SPEED, DC_I2C_DDC1_START_STOP_TIMING_CNTL), speed > 50 ? 2:1);
		else
			REG_UPDATE_N(
				SPEED, 2,
				FN(DC_I2C_DDC1_SPEED, DC_I2C_DDC1_PRESCALE), hw_engine->reference_frequency / speed,
				FN(DC_I2C_DDC1_SPEED, DC_I2C_DDC1_THRESHOLD), 2);
	}
}

static inline void reset_hw_engine(struct engine *engine)
{
	struct i2c_hw_engine_dce110 *hw_engine = FROM_ENGINE(engine);

	REG_UPDATE_2(
			DC_I2C_CONTROL,
			DC_I2C_SW_STATUS_RESET, 1,
			DC_I2C_SW_STATUS_RESET, 1);
}

static bool is_hw_busy(struct engine *engine)
{
	struct i2c_hw_engine_dce110 *hw_engine = FROM_ENGINE(engine);
	uint32_t i2c_sw_status = 0;

	REG_GET(DC_I2C_SW_STATUS, DC_I2C_SW_STATUS, &i2c_sw_status);
	if (i2c_sw_status == DC_I2C_STATUS__DC_I2C_STATUS_IDLE)
		return false;

	reset_hw_engine(engine);

	REG_GET(DC_I2C_SW_STATUS, DC_I2C_SW_STATUS, &i2c_sw_status);
	return i2c_sw_status != DC_I2C_STATUS__DC_I2C_STATUS_IDLE;
}


#define STOP_TRANS_PREDICAT \
		((hw_engine->transaction_count == 3) ||	\
				(request->action == I2CAUX_TRANSACTION_ACTION_I2C_WRITE) ||	\
				(request->action & I2CAUX_TRANSACTION_ACTION_I2C_READ))

#define SET_I2C_TRANSACTION(id)	\
		do {	\
			REG_UPDATE_N(DC_I2C_TRANSACTION##id, 5,	\
				FN(DC_I2C_TRANSACTION0, DC_I2C_STOP_ON_NACK0), 1,	\
				FN(DC_I2C_TRANSACTION0, DC_I2C_START0), 1,	\
				FN(DC_I2C_TRANSACTION0, DC_I2C_STOP0), STOP_TRANS_PREDICAT ? 1:0,	\
				FN(DC_I2C_TRANSACTION0, DC_I2C_RW0), (0 != (request->action & I2CAUX_TRANSACTION_ACTION_I2C_READ)),	\
				FN(DC_I2C_TRANSACTION0, DC_I2C_COUNT0), length);	\
				if (STOP_TRANS_PREDICAT)	\
					last_transaction = true;	\
		} while (false)


static bool process_transaction(
	struct i2c_hw_engine_dce110 *hw_engine,
	struct i2c_request_transaction_data *request)
{
	uint32_t length = request->length;
	uint8_t *buffer = request->data;
	uint32_t value = 0;

	bool last_transaction = false;

	struct dc_context *ctx = NULL;

	ctx = hw_engine->base.base.base.ctx;



	switch (hw_engine->transaction_count) {
	case 0:
		SET_I2C_TRANSACTION(0);
		break;
	case 1:
		SET_I2C_TRANSACTION(1);
		break;
	case 2:
		SET_I2C_TRANSACTION(2);
		break;
	case 3:
		SET_I2C_TRANSACTION(3);
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
	 * for I2C receive operation, the LSB must be programmed to 1. */
	if (hw_engine->transaction_count == 0) {
		value = REG_SET_4(DC_I2C_DATA, 0,
				  DC_I2C_DATA_RW, false,
				  DC_I2C_DATA, request->address,
				  DC_I2C_INDEX, 0,
				  DC_I2C_INDEX_WRITE, 1);
		hw_engine->buffer_used_write = 0;
	} else
		value = REG_SET_2(DC_I2C_DATA, 0,
				  DC_I2C_DATA_RW, false,
				  DC_I2C_DATA, request->address);

	hw_engine->buffer_used_write++;

	if (!(request->action & I2CAUX_TRANSACTION_ACTION_I2C_READ)) {
		while (length) {
			REG_SET_2(DC_I2C_DATA, value,
					DC_I2C_INDEX_WRITE, 0,
					DC_I2C_DATA, *buffer++);
			hw_engine->buffer_used_write++;
			--length;
		}
	}

	++hw_engine->transaction_count;
	hw_engine->buffer_used_bytes += length + 1;

	return last_transaction;
}

static void execute_transaction(
	struct i2c_hw_engine_dce110 *hw_engine)
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
		DC_I2C_TRANSACTION_COUNT, hw_engine->transaction_count - 1);

	/* start I2C transfer */
	REG_UPDATE(DC_I2C_CONTROL, DC_I2C_GO, 1);

	/* all transactions were executed and HW buffer became empty
	 * (even though it actually happens when status becomes DONE) */
	hw_engine->transaction_count = 0;
	hw_engine->buffer_used_bytes = 0;
}

static void submit_channel_request(
	struct i2c_engine *engine,
	struct i2c_request_transaction_data *request)
{
	request->status = I2C_CHANNEL_OPERATION_SUCCEEDED;

	if (!process_transaction(FROM_I2C_ENGINE(engine), request))
		return;

	if (is_hw_busy(&engine->base)) {
		request->status = I2C_CHANNEL_OPERATION_ENGINE_BUSY;
		return;
	}

	execute_transaction(FROM_I2C_ENGINE(engine));
}

static void process_channel_reply(
	struct i2c_engine *engine,
	struct i2c_reply_transaction_data *reply)
{
	uint32_t length = reply->length;
	uint8_t *buffer = reply->data;

	struct i2c_hw_engine_dce110 *hw_engine =
		FROM_I2C_ENGINE(engine);


	REG_SET_3(DC_I2C_DATA, 0,
			DC_I2C_INDEX, hw_engine->buffer_used_write,
			DC_I2C_DATA_RW, 1,
			DC_I2C_INDEX_WRITE, 1);

	while (length) {
		/* after reading the status,
		 * if the I2C operation executed successfully
		 * (i.e. DC_I2C_STATUS_DONE = 1) then the I2C controller
		 * should read data bytes from I2C circular data buffer */

		uint32_t i2c_data;

		REG_GET(DC_I2C_DATA, DC_I2C_DATA, &i2c_data);
		*buffer++ = i2c_data;

		--length;
	}
}

static enum i2c_channel_operation_result get_channel_status(
	struct i2c_engine *i2c_engine,
	uint8_t *returned_bytes)
{
	uint32_t i2c_sw_status = 0;
	struct i2c_hw_engine_dce110 *hw_engine = FROM_I2C_ENGINE(i2c_engine);
	uint32_t value =
			REG_GET(DC_I2C_SW_STATUS, DC_I2C_SW_STATUS, &i2c_sw_status);

	if (i2c_sw_status == DC_I2C_STATUS__DC_I2C_STATUS_USED_BY_SW)
		return I2C_CHANNEL_OPERATION_ENGINE_BUSY;
	else if (value & hw_engine->i2c_mask->DC_I2C_SW_STOPPED_ON_NACK)
		return I2C_CHANNEL_OPERATION_NO_RESPONSE;
	else if (value & hw_engine->i2c_mask->DC_I2C_SW_TIMEOUT)
		return I2C_CHANNEL_OPERATION_TIMEOUT;
	else if (value & hw_engine->i2c_mask->DC_I2C_SW_ABORTED)
		return I2C_CHANNEL_OPERATION_FAILED;
	else if (value & hw_engine->i2c_mask->DC_I2C_SW_DONE)
		return I2C_CHANNEL_OPERATION_SUCCEEDED;

	/*
	 * this is the case when HW used for communication, I2C_SW_STATUS
	 * could be zero
	 */
	return I2C_CHANNEL_OPERATION_SUCCEEDED;
}

static uint32_t get_hw_buffer_available_size(
	const struct i2c_hw_engine *engine)
{
	return I2C_HW_BUFFER_SIZE -
		FROM_I2C_HW_ENGINE(engine)->buffer_used_bytes;
}

static uint32_t get_transaction_timeout(
	const struct i2c_hw_engine *engine,
	uint32_t length)
{
	uint32_t speed = engine->base.funcs->get_speed(&engine->base);

	uint32_t period_timeout;
	uint32_t num_of_clock_stretches;

	if (!speed)
		return 0;

	period_timeout = (1000 * TRANSACTION_TIMEOUT_IN_I2C_CLOCKS) / speed;

	num_of_clock_stretches = 1 + (length << 3) + 1;
	num_of_clock_stretches +=
		(FROM_I2C_HW_ENGINE(engine)->buffer_used_bytes << 3) +
		(FROM_I2C_HW_ENGINE(engine)->transaction_count << 1);

	return period_timeout * num_of_clock_stretches;
}

static void destroy(
	struct i2c_engine **i2c_engine)
{
	struct i2c_hw_engine_dce110 *engine_dce110 =
			FROM_I2C_ENGINE(*i2c_engine);

	dal_i2c_hw_engine_destruct(&engine_dce110->base);

	kfree(engine_dce110);

	*i2c_engine = NULL;
}

static const struct i2c_engine_funcs i2c_engine_funcs = {
	.destroy = destroy,
	.get_speed = get_speed,
	.set_speed = set_speed,
	.setup_engine = setup_engine,
	.submit_channel_request = submit_channel_request,
	.process_channel_reply = process_channel_reply,
	.get_channel_status = get_channel_status,
	.acquire_engine = dal_i2c_hw_engine_acquire_engine,
};

static const struct engine_funcs engine_funcs = {
	.release_engine = release_engine,
	.get_engine_type = dal_i2c_hw_engine_get_engine_type,
	.acquire = dal_i2c_engine_acquire,
	.submit_request = dal_i2c_hw_engine_submit_request,
};

static const struct i2c_hw_engine_funcs i2c_hw_engine_funcs = {
	.get_hw_buffer_available_size = get_hw_buffer_available_size,
	.get_transaction_timeout = get_transaction_timeout,
	.wait_on_operation_result = dal_i2c_hw_engine_wait_on_operation_result,
};

static void construct(
	struct i2c_hw_engine_dce110 *hw_engine,
	const struct i2c_hw_engine_dce110_create_arg *arg)
{
	uint32_t xtal_ref_div = 0;

	dal_i2c_hw_engine_construct(&hw_engine->base, arg->ctx);

	hw_engine->base.base.base.funcs = &engine_funcs;
	hw_engine->base.base.funcs = &i2c_engine_funcs;
	hw_engine->base.funcs = &i2c_hw_engine_funcs;
	hw_engine->base.default_speed = arg->default_speed;

	hw_engine->regs = arg->regs;
	hw_engine->i2c_shift = arg->i2c_shift;
	hw_engine->i2c_mask = arg->i2c_mask;

	hw_engine->engine_id = arg->engine_id;

	hw_engine->buffer_used_bytes = 0;
	hw_engine->transaction_count = 0;
	hw_engine->engine_keep_power_up_count = 1;


	REG_GET(MICROSECOND_TIME_BASE_DIV, XTAL_REF_DIV, &xtal_ref_div);

	if (xtal_ref_div == 0) {
		DC_LOG_WARNING("Invalid base timer divider [%s]\n",
				__func__);
		xtal_ref_div = 2;
	}

	/*Calculating Reference Clock by divding original frequency by
	 * XTAL_REF_DIV.
	 * At upper level, uint32_t reference_frequency =
	 *  dal_i2caux_get_reference_clock(as) >> 1
	 *  which already divided by 2. So we need x2 to get original
	 *  reference clock from ppll_info
	 */
	hw_engine->reference_frequency =
		(arg->reference_frequency * 2) / xtal_ref_div;
}

struct i2c_engine *dal_i2c_hw_engine_dce110_create(
	const struct i2c_hw_engine_dce110_create_arg *arg)
{
	struct i2c_hw_engine_dce110 *engine_dce10;

	if (!arg) {
		ASSERT_CRITICAL(false);
		return NULL;
	}
	if (!arg->reference_frequency) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	engine_dce10 = kzalloc(sizeof(struct i2c_hw_engine_dce110),
			       GFP_KERNEL);

	if (!engine_dce10) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	construct(engine_dce10, arg);
	return &engine_dce10->base.base;
}
