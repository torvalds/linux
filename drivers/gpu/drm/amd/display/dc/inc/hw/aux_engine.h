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

#ifndef __DAL_AUX_ENGINE_H__
#define __DAL_AUX_ENGINE_H__

#include "dc_ddc_types.h"

enum aux_return_code_type;

enum i2caux_transaction_operation {
	I2CAUX_TRANSACTION_READ,
	I2CAUX_TRANSACTION_WRITE
};

enum i2caux_transaction_address_space {
	I2CAUX_TRANSACTION_ADDRESS_SPACE_I2C = 1,
	I2CAUX_TRANSACTION_ADDRESS_SPACE_DPCD
};

struct i2caux_transaction_payload {
	enum i2caux_transaction_address_space address_space;
	uint32_t address;
	uint32_t length;
	uint8_t *data;
};

enum i2caux_transaction_status {
	I2CAUX_TRANSACTION_STATUS_UNKNOWN = (-1L),
	I2CAUX_TRANSACTION_STATUS_SUCCEEDED,
	I2CAUX_TRANSACTION_STATUS_FAILED_CHANNEL_BUSY,
	I2CAUX_TRANSACTION_STATUS_FAILED_TIMEOUT,
	I2CAUX_TRANSACTION_STATUS_FAILED_PROTOCOL_ERROR,
	I2CAUX_TRANSACTION_STATUS_FAILED_NACK,
	I2CAUX_TRANSACTION_STATUS_FAILED_INCOMPLETE,
	I2CAUX_TRANSACTION_STATUS_FAILED_OPERATION,
	I2CAUX_TRANSACTION_STATUS_FAILED_INVALID_OPERATION,
	I2CAUX_TRANSACTION_STATUS_FAILED_BUFFER_OVERFLOW,
	I2CAUX_TRANSACTION_STATUS_FAILED_HPD_DISCON
};

struct i2caux_transaction_request {
	enum i2caux_transaction_operation operation;
	struct i2caux_transaction_payload payload;
	enum i2caux_transaction_status status;
};

enum i2caux_engine_type {
	I2CAUX_ENGINE_TYPE_UNKNOWN = (-1L),
	I2CAUX_ENGINE_TYPE_AUX,
	I2CAUX_ENGINE_TYPE_I2C_DDC_HW,
	I2CAUX_ENGINE_TYPE_I2C_GENERIC_HW,
	I2CAUX_ENGINE_TYPE_I2C_SW
};

enum i2c_default_speed {
	I2CAUX_DEFAULT_I2C_HW_SPEED = 50,
	I2CAUX_DEFAULT_I2C_SW_SPEED = 50
};

union aux_config {
	struct {
		uint32_t ALLOW_AUX_WHEN_HPD_LOW:1;
	} bits;
	uint32_t raw;
};

struct aux_engine {
	uint32_t inst;
	struct ddc *ddc;
	struct dc_context *ctx;
	const struct aux_engine_funcs *funcs;
	/* following values are expressed in milliseconds */
	uint32_t delay;
	uint32_t max_defer_write_retry;
	bool acquire_reset;
};

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


struct aux_engine_funcs {
	bool (*configure_timeout)(
		struct ddc_service *ddc,
		uint32_t timeout);
	void (*destroy)(
		struct aux_engine **ptr);
	bool (*acquire_engine)(
		struct aux_engine *engine);
	void (*configure)(
		struct aux_engine *engine,
		union aux_config cfg);
	void (*submit_channel_request)(
		struct aux_engine *engine,
		struct aux_request_transaction_data *request);
	void (*process_channel_reply)(
		struct aux_engine *engine,
		struct aux_reply_transaction_data *reply);
	int (*read_channel_reply)(
		struct aux_engine *engine,
		uint32_t size,
		uint8_t *buffer,
		uint8_t *reply_result,
		uint32_t *sw_status);
	enum aux_return_code_type (*get_channel_status)(
		struct aux_engine *engine,
		uint8_t *returned_bytes);
	bool (*is_engine_available)(struct aux_engine *engine);
	bool (*acquire)(
		struct aux_engine *engine,
		struct ddc *ddc);
	bool (*submit_request)(
		struct aux_engine *engine,
		struct i2caux_transaction_request *request,
		bool middle_of_transaction);
	void (*release_engine)(
		struct aux_engine *engine);
	void (*destroy_engine)(
		struct aux_engine **engine);
};
#endif
