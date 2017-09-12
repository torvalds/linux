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
#include "i2c_hw_engine.h"

/*
 * Header of this unit
 */

#include "i2c_generic_hw_engine.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

/*
 * @brief
 * Cast 'struct i2c_hw_engine *'
 * to 'struct i2c_generic_hw_engine *'
 */
#define FROM_I2C_HW_ENGINE(ptr) \
	container_of((ptr), struct i2c_generic_hw_engine, base)

/*
 * @brief
 * Cast 'struct i2c_engine *'
 * to 'struct i2c_generic_hw_engine *'
 */
#define FROM_I2C_ENGINE(ptr) \
	FROM_I2C_HW_ENGINE(container_of((ptr), struct i2c_hw_engine, base))

/*
 * @brief
 * Cast 'struct engine *'
 * to 'struct i2c_generic_hw_engine *'
 */
#define FROM_ENGINE(ptr) \
	FROM_I2C_ENGINE(container_of((ptr), struct i2c_engine, base))

enum i2caux_engine_type dal_i2c_generic_hw_engine_get_engine_type(
	const struct engine *engine)
{
	return I2CAUX_ENGINE_TYPE_I2C_GENERIC_HW;
}

/*
 * @brief
 * Single transaction handling.
 * Since transaction may be bigger than HW buffer size,
 * it divides transaction to sub-transactions
 * and uses batch transaction feature of the engine.
 */
bool dal_i2c_generic_hw_engine_submit_request(
	struct engine *engine,
	struct i2caux_transaction_request *i2caux_request,
	bool middle_of_transaction)
{
	struct i2c_generic_hw_engine *hw_engine = FROM_ENGINE(engine);

	struct i2c_hw_engine *base = &hw_engine->base;

	uint32_t max_payload_size =
		base->funcs->get_hw_buffer_available_size(base);

	bool initial_stop_bit = !middle_of_transaction;

	struct i2c_generic_transaction_attributes attributes;

	enum i2c_channel_operation_result operation_result =
		I2C_CHANNEL_OPERATION_FAILED;

	bool result = false;

	/* setup transaction initial properties */

	uint8_t address = i2caux_request->payload.address;
	uint8_t *current_payload = i2caux_request->payload.data;
	uint32_t remaining_payload_size = i2caux_request->payload.length;

	bool first_iteration = true;

	if (i2caux_request->operation == I2CAUX_TRANSACTION_READ)
		attributes.action = I2CAUX_TRANSACTION_ACTION_I2C_READ;
	else if (i2caux_request->operation == I2CAUX_TRANSACTION_WRITE)
		attributes.action = I2CAUX_TRANSACTION_ACTION_I2C_WRITE;
	else {
		i2caux_request->status =
			I2CAUX_TRANSACTION_STATUS_FAILED_INVALID_OPERATION;
		return false;
	}

	/* Do batch transaction.
	 * Divide read/write data into payloads which fit HW buffer size.
	 * 1. Single transaction:
	 *    start_bit = 1, stop_bit depends on session state, ack_on_read = 0;
	 * 2. Start of batch transaction:
	 *    start_bit = 1, stop_bit = 0, ack_on_read = 1;
	 * 3. Middle of batch transaction:
	 *    start_bit = 0, stop_bit = 0, ack_on_read = 1;
	 * 4. End of batch transaction:
	 *    start_bit = 0, stop_bit depends on session state, ack_on_read = 0.
	 * Session stop bit is set if 'middle_of_transaction' = 0. */

	while (remaining_payload_size) {
		uint32_t current_transaction_size;
		uint32_t current_payload_size;

		bool last_iteration;
		bool stop_bit;

		/* Calculate current transaction size and payload size.
		 * Transaction size = total number of bytes in transaction,
		 * including slave's address;
		 * Payload size = number of data bytes in transaction. */

		if (first_iteration) {
			/* In the first sub-transaction we send slave's address
			 * thus we need to reserve one byte for it */
			current_transaction_size =
			(remaining_payload_size > max_payload_size - 1) ?
				max_payload_size :
				remaining_payload_size + 1;

			current_payload_size = current_transaction_size - 1;
		} else {
			/* Second and further sub-transactions will have
			 * entire buffer reserved for data */
			current_transaction_size =
				(remaining_payload_size > max_payload_size) ?
				max_payload_size :
				remaining_payload_size;

			current_payload_size = current_transaction_size;
		}

		last_iteration =
			(remaining_payload_size == current_payload_size);

		stop_bit = last_iteration ? initial_stop_bit : false;

		/* write slave device address */

		if (first_iteration)
			hw_engine->funcs->write_address(hw_engine, address);

		/* write current portion of data, if requested */

		if (i2caux_request->operation == I2CAUX_TRANSACTION_WRITE)
			hw_engine->funcs->write_data(
				hw_engine,
				current_payload,
				current_payload_size);

		/* execute transaction */

		attributes.start_bit = first_iteration;
		attributes.stop_bit = stop_bit;
		attributes.last_read = last_iteration;
		attributes.transaction_size = current_transaction_size;

		hw_engine->funcs->execute_transaction(hw_engine, &attributes);

		/* wait until transaction is processed; if it fails - quit */

		operation_result = base->funcs->wait_on_operation_result(
			base,
			base->funcs->get_transaction_timeout(
				base, current_transaction_size),
			I2C_CHANNEL_OPERATION_ENGINE_BUSY);

		if (operation_result != I2C_CHANNEL_OPERATION_SUCCEEDED)
			break;

		/* read current portion of data, if requested */

		/* the read offset should be 1 for first sub-transaction,
		 * and 0 for any next one */

		if (i2caux_request->operation == I2CAUX_TRANSACTION_READ)
			hw_engine->funcs->read_data(hw_engine, current_payload,
				current_payload_size, first_iteration ? 1 : 0);

		/* update loop variables */

		first_iteration = false;
		current_payload += current_payload_size;
		remaining_payload_size -= current_payload_size;
	}

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

	return result;
}

/*
 * @brief
 * Returns number of microseconds to wait until timeout to be considered
 */
uint32_t dal_i2c_generic_hw_engine_get_transaction_timeout(
	const struct i2c_hw_engine *engine,
	uint32_t length)
{
	const struct i2c_engine *base = &engine->base;

	uint32_t speed = base->funcs->get_speed(base);

	if (!speed)
		return 0;

	/* total timeout = period_timeout * (start + data bits count + stop) */

	return ((1000 * TRANSACTION_TIMEOUT_IN_I2C_CLOCKS) / speed) *
		(1 + (length << 3) + 1);
}

bool dal_i2c_generic_hw_engine_construct(
	struct i2c_generic_hw_engine *engine,
	struct dc_context *ctx)
{
	if (!dal_i2c_hw_engine_construct(&engine->base, ctx))
		return false;
	return true;
}

void dal_i2c_generic_hw_engine_destruct(
	struct i2c_generic_hw_engine *engine)
{
	dal_i2c_hw_engine_destruct(&engine->base);
}
