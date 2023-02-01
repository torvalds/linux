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
#ifndef DC_DDC_TYPES_H_
#define DC_DDC_TYPES_H_

enum aux_transaction_type {
	AUX_TRANSACTION_TYPE_DP,
	AUX_TRANSACTION_TYPE_I2C
};


enum i2caux_transaction_action {
	I2CAUX_TRANSACTION_ACTION_I2C_WRITE = 0x00,
	I2CAUX_TRANSACTION_ACTION_I2C_READ = 0x10,
	I2CAUX_TRANSACTION_ACTION_I2C_STATUS_REQUEST = 0x20,

	I2CAUX_TRANSACTION_ACTION_I2C_WRITE_MOT = 0x40,
	I2CAUX_TRANSACTION_ACTION_I2C_READ_MOT = 0x50,
	I2CAUX_TRANSACTION_ACTION_I2C_STATUS_REQUEST_MOT = 0x60,

	I2CAUX_TRANSACTION_ACTION_DP_WRITE = 0x80,
	I2CAUX_TRANSACTION_ACTION_DP_READ = 0x90
};

struct aux_request_transaction_data {
	enum aux_transaction_type type;
	enum i2caux_transaction_action action;
	/* 20-bit AUX channel transaction address */
	uint32_t address;
	/* delay, in 100-microsecond units */
	uint8_t delay;
	uint32_t length;
	uint8_t *data;
};

enum aux_transaction_reply {
	AUX_TRANSACTION_REPLY_AUX_ACK = 0x00,
	AUX_TRANSACTION_REPLY_AUX_NACK = 0x01,
	AUX_TRANSACTION_REPLY_AUX_DEFER = 0x02,
	AUX_TRANSACTION_REPLY_I2C_OVER_AUX_NACK = 0x04,
	AUX_TRANSACTION_REPLY_I2C_OVER_AUX_DEFER = 0x08,

	AUX_TRANSACTION_REPLY_I2C_ACK = 0x00,
	AUX_TRANSACTION_REPLY_I2C_NACK = 0x10,
	AUX_TRANSACTION_REPLY_I2C_DEFER = 0x20,

	AUX_TRANSACTION_REPLY_HPD_DISCON = 0x40,

	AUX_TRANSACTION_REPLY_INVALID = 0xFF
};

struct aux_reply_transaction_data {
	enum aux_transaction_reply status;
	uint32_t length;
	uint8_t *data;
};

struct aux_payload {
	/* set following flag to read/write I2C data,
	 * reset it to read/write DPCD data */
	bool i2c_over_aux;
	/* set following flag to write data,
	 * reset it to read data */
	bool write;
	bool mot;
	bool write_status_update;

	uint32_t address;
	uint32_t length;
	uint8_t *data;
	/*
	 * used to return the reply type of the transaction
	 * ignored if NULL
	 */
	uint8_t *reply;
	/* expressed in milliseconds
	 * zero means "use default value"
	 */
	uint32_t defer_delay;

};
#define DEFAULT_AUX_MAX_DATA_SIZE 16

struct i2c_payload {
	bool write;
	uint8_t address;
	uint32_t length;
	uint8_t *data;
};

enum i2c_command_engine {
	I2C_COMMAND_ENGINE_DEFAULT,
	I2C_COMMAND_ENGINE_SW,
	I2C_COMMAND_ENGINE_HW
};

#define DDC_I2C_COMMAND_ENGINE I2C_COMMAND_ENGINE_SW

struct i2c_command {
	struct i2c_payload *payloads;
	uint8_t number_of_payloads;

	enum i2c_command_engine engine;

	/* expressed in KHz
	 * zero means "use default value" */
	uint32_t speed;
};

struct gpio_ddc_hw_info {
	bool hw_supported;
	uint32_t ddc_channel;
};

struct ddc {
	struct gpio *pin_data;
	struct gpio *pin_clock;
	struct gpio_ddc_hw_info hw_info;
	struct dc_context *ctx;
};

union ddc_wa {
	struct {
		uint32_t DP_SKIP_POWER_OFF:1;
		uint32_t DP_AUX_POWER_UP_WA_DELAY:1;
	} bits;
	uint32_t raw;
};

struct ddc_flags {
	uint8_t EDID_QUERY_DONE_ONCE:1;
	uint8_t IS_INTERNAL_DISPLAY:1;
	uint8_t FORCE_READ_REPEATED_START:1;
	uint8_t EDID_STRESS_READ:1;

};

enum ddc_transaction_type {
	DDC_TRANSACTION_TYPE_NONE = 0,
	DDC_TRANSACTION_TYPE_I2C,
	DDC_TRANSACTION_TYPE_I2C_OVER_AUX,
	DDC_TRANSACTION_TYPE_I2C_OVER_AUX_WITH_DEFER,
	DDC_TRANSACTION_TYPE_I2C_OVER_AUX_RETRY_DEFER
};

enum display_dongle_type {
	DISPLAY_DONGLE_NONE = 0,
	/* Active converter types*/
	DISPLAY_DONGLE_DP_VGA_CONVERTER,
	DISPLAY_DONGLE_DP_DVI_CONVERTER,
	DISPLAY_DONGLE_DP_HDMI_CONVERTER,
	/* DP-HDMI/DVI passive dongles (Type 1 and Type 2)*/
	DISPLAY_DONGLE_DP_DVI_DONGLE,
	DISPLAY_DONGLE_DP_HDMI_DONGLE,
	/* Other types of dongle*/
	DISPLAY_DONGLE_DP_HDMI_MISMATCHED_DONGLE,
};

#define DC_MAX_EDID_BUFFER_SIZE 2048
#define DC_EDID_BLOCK_SIZE 128

struct ddc_service {
	struct ddc *ddc_pin;
	struct ddc_flags flags;
	union ddc_wa wa;
	enum ddc_transaction_type transaction_type;
	enum display_dongle_type dongle_type;
	struct dc_context *ctx;
	struct dc_link *link;

	uint32_t address;
	uint32_t edid_buf_len;
	uint8_t edid_buf[DC_MAX_EDID_BUFFER_SIZE];
};

#endif /* DC_DDC_TYPES_H_ */
