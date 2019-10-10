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

#include "include/ddc_service_types.h"
#include "include/i2caux_interface.h"

#define EDID_SEGMENT_SIZE 256

/* Address range from 0x00 to 0x1F.*/
#define DP_ADAPTOR_TYPE2_SIZE 0x20
#define DP_ADAPTOR_TYPE2_REG_ID 0x10
#define DP_ADAPTOR_TYPE2_REG_MAX_TMDS_CLK 0x1D
/* Identifies adaptor as Dual-mode adaptor */
#define DP_ADAPTOR_TYPE2_ID 0xA0
/* MHz*/
#define DP_ADAPTOR_TYPE2_MAX_TMDS_CLK 600
/* MHz*/
#define DP_ADAPTOR_TYPE2_MIN_TMDS_CLK 25
/* kHZ*/
#define DP_ADAPTOR_DVI_MAX_TMDS_CLK 165000
/* kHZ*/
#define DP_ADAPTOR_HDMI_SAFE_MAX_TMDS_CLK 165000

#define DDC_I2C_COMMAND_ENGINE I2C_COMMAND_ENGINE_SW

struct ddc_service;
struct graphics_object_id;
enum ddc_result;
struct av_sync_data;
struct dp_receiver_id_info;

struct i2c_payloads;
struct aux_payloads;

void dal_ddc_i2c_payloads_add(
		struct i2c_payloads *payloads,
		uint32_t address,
		uint32_t len,
		uint8_t *data,
		bool write);

struct ddc_service_init_data {
	struct graphics_object_id id;
	struct dc_context *ctx;
	struct dc_link *link;
};

struct ddc_service *dal_ddc_service_create(
		struct ddc_service_init_data *ddc_init_data);

void dal_ddc_service_destroy(struct ddc_service **ddc);

enum ddc_service_type dal_ddc_service_get_type(struct ddc_service *ddc);

void dal_ddc_service_set_transaction_type(
		struct ddc_service *ddc,
		enum ddc_transaction_type type);

bool dal_ddc_service_is_in_aux_transaction_mode(struct ddc_service *ddc);

void dal_ddc_service_i2c_query_dp_dual_mode_adaptor(
		struct ddc_service *ddc,
		struct display_sink_capability *sink_cap);

bool dal_ddc_service_query_ddc_data(
		struct ddc_service *ddc,
		uint32_t address,
		uint8_t *write_buf,
		uint32_t write_size,
		uint8_t *read_buf,
		uint32_t read_size);

int dc_link_aux_transfer_raw(struct ddc_service *ddc,
		struct aux_payload *payload,
		enum aux_channel_operation_result *operation_result);

bool dc_link_aux_transfer_with_retries(struct ddc_service *ddc,
		struct aux_payload *payload);

void dal_ddc_service_write_scdc_data(
		struct ddc_service *ddc_service,
		uint32_t pix_clk,
		bool lte_340_scramble);

void dal_ddc_service_read_scdc_data(
		struct ddc_service *ddc_service);

void ddc_service_set_dongle_type(struct ddc_service *ddc,
		enum display_dongle_type dongle_type);

void dal_ddc_service_set_ddc_pin(
		struct ddc_service *ddc_service,
		struct ddc *ddc);

struct ddc *dal_ddc_service_get_ddc_pin(struct ddc_service *ddc_service);

uint32_t get_defer_delay(struct ddc_service *ddc);

#endif /* __DAL_DDC_SERVICE_H__ */

