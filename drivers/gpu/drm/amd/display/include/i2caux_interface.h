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

#ifndef __DAL_I2CAUX_INTERFACE_H__
#define __DAL_I2CAUX_INTERFACE_H__

#include "dc_types.h"
#include "gpio_service_interface.h"


#define DEFAULT_AUX_MAX_DATA_SIZE 16
#define AUX_MAX_DEFER_WRITE_RETRY 20

struct aux_payload {
	/* set following flag to read/write I2C data,
	 * reset it to read/write DPCD data */
	bool i2c_over_aux;
	/* set following flag to write data,
	 * reset it to read data */
	bool write;
	bool mot;
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

struct aux_command {
	struct aux_payload *payloads;
	uint8_t number_of_payloads;

	/* expressed in milliseconds
	 * zero means "use default value" */
	uint32_t defer_delay;

	/* zero means "use default value" */
	uint32_t max_defer_write_retry;

	enum i2c_mot_mode mot;
};

union aux_config {
	struct {
		uint32_t ALLOW_AUX_WHEN_HPD_LOW:1;
	} bits;
	uint32_t raw;
};

#endif
