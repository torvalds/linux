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

struct ddc_service *link_create_ddc_service(
		struct ddc_service_init_data *ddc_init_data);

void link_destroy_ddc_service(struct ddc_service **ddc);

void set_ddc_transaction_type(
		struct ddc_service *ddc,
		enum ddc_transaction_type type);

uint32_t link_get_aux_defer_delay(struct ddc_service *ddc);

bool link_is_in_aux_transaction_mode(struct ddc_service *ddc);

bool try_to_configure_aux_timeout(struct ddc_service *ddc,
		uint32_t timeout);

bool link_query_ddc_data(
		struct ddc_service *ddc,
		uint32_t address,
		uint8_t *write_buf,
		uint32_t write_size,
		uint8_t *read_buf,
		uint32_t read_size);

/* Attempt to submit an aux payload, retrying on timeouts, defers, and busy
 * states as outlined in the DP spec.  Returns true if the request was
 * successful.
 *
 * NOTE: The function requires explicit mutex on DM side in order to prevent
 * potential race condition. DC components should call the dpcd read/write
 * function in dm_helpers in order to access dpcd safely
 */
bool link_aux_transfer_with_retries_no_mutex(struct ddc_service *ddc,
		struct aux_payload *payload);

void write_scdc_data(
		struct ddc_service *ddc_service,
		uint32_t pix_clk,
		bool lte_340_scramble);

void read_scdc_data(
		struct ddc_service *ddc_service);

void set_dongle_type(struct ddc_service *ddc,
		enum display_dongle_type dongle_type);

struct ddc *get_ddc_pin(struct ddc_service *ddc_service);

int link_aux_transfer_raw(struct ddc_service *ddc,
		struct aux_payload *payload,
		enum aux_return_code_type *operation_result);
#endif /* __DAL_DDC_SERVICE_H__ */

