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

struct ddc_service {
	struct ddc *ddc_pin;
	struct ddc_flags flags;
	union ddc_wa wa;
	enum ddc_transaction_type transaction_type;
	enum display_dongle_type dongle_type;
	struct dc_context *ctx;
	struct core_link *link;

	uint32_t address;
	uint32_t edid_buf_len;
	uint8_t edid_buf[MAX_EDID_BUFFER_SIZE];
};

#endif /* DC_DDC_TYPES_H_ */
