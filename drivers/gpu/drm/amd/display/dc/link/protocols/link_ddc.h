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

#ifndef __DAL_DDC_SERVICE_H__
#define __DAL_DDC_SERVICE_H__

#include "link.h"

#define AUX_POWER_UP_WA_DELAY 500
#define I2C_OVER_AUX_DEFER_WA_DELAY 70
#define DPVGA_DONGLE_AUX_DEFER_WA_DELAY 40
#define I2C_OVER_AUX_DEFER_WA_DELAY_1MS 1
#define LINK_AUX_DEFAULT_LTTPR_TIMEOUT_PERIOD 3200 /*us*/
#define LINK_AUX_DEFAULT_TIMEOUT_PERIOD 552 /*us*/

#define EDID_SEGMENT_SIZE 256

void set_ddc_transaction_type(
		struct ddc_service *ddc,
		enum ddc_transaction_type type);

bool try_to_configure_aux_timeout(struct ddc_service *ddc,
		uint32_t timeout);

void write_scdc_data(
		struct ddc_service *ddc_service,
		uint32_t pix_clk,
		bool lte_340_scramble);

void read_scdc_data(
		struct ddc_service *ddc_service);

void set_dongle_type(struct ddc_service *ddc,
		enum display_dongle_type dongle_type);

struct ddc *get_ddc_pin(struct ddc_service *ddc_service);

#endif /* __DAL_DDC_SERVICE_H__ */

