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
#include "dm_helpers.h"
#include "gpio_service_interface.h"
#include "include/ddc_service_types.h"
#include "include/grph_object_id.h"
#include "include/dpcd_defs.h"
#include "include/logger_interface.h"
#include "include/vector.h"
#include "core_types.h"
#include "dc_link_ddc.h"
#include "aux_engine.h"

#define AUX_POWER_UP_WA_DELAY 500
#define I2C_OVER_AUX_DEFER_WA_DELAY 70

/* CV smart dongle slave address for retrieving supported HDTV modes*/
#define CV_SMART_DONGLE_ADDRESS 0x20
/* DVI-HDMI dongle slave address for retrieving dongle signature*/
#define DVI_HDMI_DONGLE_ADDRESS 0x68
static const int8_t dvi_hdmi_dongle_signature_str[] = "6140063500G";
struct dvi_hdmi_dongle_signature_data {
	int8_t vendor[3];/* "AMD" */
	uint8_t version[2];
	uint8_t size;
	int8_t id[11];/* "6140063500G"*/
};
/* DP-HDMI dongle slave address for retrieving dongle signature*/
#define DP_HDMI_DONGLE_ADDRESS 0x40
static const uint8_t dp_hdmi_dongle_signature_str[] = "DP-HDMI ADAPTOR";
#define DP_HDMI_DONGLE_SIGNATURE_EOT 0x04

struct dp_hdmi_dongle_signature_data {
	int8_t id[15];/* "DP-HDMI ADAPTOR"*/
	uint8_t eot;/* end of transmition '\x4' */
};

/* SCDC Address defines (HDMI 2.0)*/
#define HDMI_SCDC_WRITE_UPDATE_0_ARRAY 3
#define HDMI_SCDC_ADDRESS  0x54
#define HDMI_SCDC_SINK_VERSION 0x01
#define HDMI_SCDC_SOURCE_VERSION 0x02
#define HDMI_SCDC_UPDATE_0 0x10
#define HDMI_SCDC_TMDS_CONFIG 0x20
#define HDMI_SCDC_SCRAMBLER_STATUS 0x21
#define HDMI_SCDC_CONFIG_0 0x30
#define HDMI_SCDC_STATUS_FLAGS 0x40
#define HDMI_SCDC_ERR_DETECT 0x50
#define HDMI_SCDC_TEST_CONFIG 0xC0

union hdmi_scdc_update_read_data {
	uint8_t byte[2];
	struct {
		uint8_t STATUS_UPDATE:1;
		uint8_t CED_UPDATE:1;
		uint8_t RR_TEST:1;
		uint8_t RESERVED:5;
		uint8_t RESERVED2:8;
	} fields;
};

union hdmi_scdc_status_flags_data {
	uint8_t byte[2];
	struct {
		uint8_t CLOCK_DETECTED:1;
		uint8_t CH0_LOCKED:1;
		uint8_t CH1_LOCKED:1;
		uint8_t CH2_LOCKED:1;
		uint8_t RESERVED:4;
		uint8_t RESERVED2:8;
	} fields;
};

union hdmi_scdc_ced_data {
	uint8_t byte[7];
	struct {
		uint8_t CH0_8LOW:8;
		uint8_t CH0_7HIGH:7;
		uint8_t CH0_VALID:1;
		uint8_t CH1_8LOW:8;
		uint8_t CH1_7HIGH:7;
		uint8_t CH1_VALID:1;
		uint8_t CH2_8LOW:8;
		uint8_t CH2_7HIGH:7;
		uint8_t CH2_VALID:1;
		uint8_t CHECKSUM:8;
	} fields;
};

union hdmi_scdc_test_config_Data {
	uint8_t byte;
	struct {
		uint8_t TEST_READ_REQUEST_DELAY:7;
		uint8_t TEST_READ_REQUEST: 1;
	} fields;
};

struct i2c_payloads {
	struct vector payloads;
};

struct aux_payloads {
	struct vector payloads;
};

static struct i2c_payloads *dal_ddc_i2c_payloads_create(struct dc_context *ctx, uint32_t count)
{
	struct i2c_payloads *payloads;

	payloads = kzalloc(sizeof(struct i2c_payloads), GFP_KERNEL);

	if (!payloads)
		return NULL;

	if (dal_vector_construct(
		&payloads->payloads, ctx, count, sizeof(struct i2c_payload)))
		return payloads;

	kfree(payloads);
	return NULL;

}

static struct i2c_payload *dal_ddc_i2c_payloads_get(struct i2c_payloads *p)
{
	return (struct i2c_payload *)p->payloads.container;
}

static uint32_t dal_ddc_i2c_payloads_get_count(struct i2c_payloads *p)
{
	return p->payloads.count;
}

static void dal_ddc_i2c_payloads_destroy(struct i2c_payloads **p)
{
	if (!p || !*p)
		return;
	dal_vector_destruct(&(*p)->payloads);
	kfree(*p);
	*p = NULL;

}

static struct aux_payloads *dal_ddc_aux_payloads_create(struct dc_context *ctx, uint32_t count)
{
	struct aux_payloads *payloads;

	payloads = kzalloc(sizeof(struct aux_payloads), GFP_KERNEL);

	if (!payloads)
		return NULL;

	if (dal_vector_construct(
		&payloads->payloads, ctx, count, sizeof(struct aux_payload)))
		return payloads;

	kfree(payloads);
	return NULL;
}

static struct aux_payload *dal_ddc_aux_payloads_get(struct aux_payloads *p)
{
	return (struct aux_payload *)p->payloads.container;
}

static uint32_t  dal_ddc_aux_payloads_get_count(struct aux_payloads *p)
{
	return p->payloads.count;
}

static void dal_ddc_aux_payloads_destroy(struct aux_payloads **p)
{
	if (!p || !*p)
		return;

	dal_vector_destruct(&(*p)->payloads);
	kfree(*p);
	*p = NULL;
}

#define DDC_MIN(a, b) (((a) < (b)) ? (a) : (b))

void dal_ddc_i2c_payloads_add(
	struct i2c_payloads *payloads,
	uint32_t address,
	uint32_t len,
	uint8_t *data,
	bool write)
{
	uint32_t payload_size = EDID_SEGMENT_SIZE;
	uint32_t pos;

	for (pos = 0; pos < len; pos += payload_size) {
		struct i2c_payload payload = {
			.write = write,
			.address = address,
			.length = DDC_MIN(payload_size, len - pos),
			.data = data + pos };
		dal_vector_append(&payloads->payloads, &payload);
	}

}

void dal_ddc_aux_payloads_add(
	struct aux_payloads *payloads,
	uint32_t address,
	uint32_t len,
	uint8_t *data,
	bool write)
{
	uint32_t payload_size = DEFAULT_AUX_MAX_DATA_SIZE;
	uint32_t pos;

	for (pos = 0; pos < len; pos += payload_size) {
		struct aux_payload payload = {
			.i2c_over_aux = true,
			.write = write,
			.address = address,
			.length = DDC_MIN(payload_size, len - pos),
			.data = data + pos };
		dal_vector_append(&payloads->payloads, &payload);
	}
}

static void construct(
	struct ddc_service *ddc_service,
	struct ddc_service_init_data *init_data)
{
	enum connector_id connector_id =
		dal_graphics_object_id_get_connector_id(init_data->id);

	struct gpio_service *gpio_service = init_data->ctx->gpio_service;
	struct graphics_object_i2c_info i2c_info;
	struct gpio_ddc_hw_info hw_info;
	struct dc_bios *dcb = init_data->ctx->dc_bios;

	ddc_service->link = init_data->link;
	ddc_service->ctx = init_data->ctx;

	if (BP_RESULT_OK != dcb->funcs->get_i2c_info(dcb, init_data->id, &i2c_info)) {
		ddc_service->ddc_pin = NULL;
	} else {
		hw_info.ddc_channel = i2c_info.i2c_line;
		hw_info.hw_supported = i2c_info.i2c_hw_assist;

		ddc_service->ddc_pin = dal_gpio_create_ddc(
			gpio_service,
			i2c_info.gpio_info.clk_a_register_index,
			1 << i2c_info.gpio_info.clk_a_shift,
			&hw_info);
	}

	ddc_service->flags.EDID_QUERY_DONE_ONCE = false;
	ddc_service->flags.FORCE_READ_REPEATED_START = false;
	ddc_service->flags.EDID_STRESS_READ = false;

	ddc_service->flags.IS_INTERNAL_DISPLAY =
		connector_id == CONNECTOR_ID_EDP ||
		connector_id == CONNECTOR_ID_LVDS;

	ddc_service->wa.raw = 0;
}

struct ddc_service *dal_ddc_service_create(
	struct ddc_service_init_data *init_data)
{
	struct ddc_service *ddc_service;

	ddc_service = kzalloc(sizeof(struct ddc_service), GFP_KERNEL);

	if (!ddc_service)
		return NULL;

	construct(ddc_service, init_data);
	return ddc_service;
}

static void destruct(struct ddc_service *ddc)
{
	if (ddc->ddc_pin)
		dal_gpio_destroy_ddc(&ddc->ddc_pin);
}

void dal_ddc_service_destroy(struct ddc_service **ddc)
{
	if (!ddc || !*ddc) {
		BREAK_TO_DEBUGGER();
		return;
	}
	destruct(*ddc);
	kfree(*ddc);
	*ddc = NULL;
}

enum ddc_service_type dal_ddc_service_get_type(struct ddc_service *ddc)
{
	return DDC_SERVICE_TYPE_CONNECTOR;
}

void dal_ddc_service_set_transaction_type(
	struct ddc_service *ddc,
	enum ddc_transaction_type type)
{
	ddc->transaction_type = type;
}

bool dal_ddc_service_is_in_aux_transaction_mode(struct ddc_service *ddc)
{
	switch (ddc->transaction_type) {
	case DDC_TRANSACTION_TYPE_I2C_OVER_AUX:
	case DDC_TRANSACTION_TYPE_I2C_OVER_AUX_WITH_DEFER:
	case DDC_TRANSACTION_TYPE_I2C_OVER_AUX_RETRY_DEFER:
		return true;
	default:
		break;
	}
	return false;
}

void ddc_service_set_dongle_type(struct ddc_service *ddc,
		enum display_dongle_type dongle_type)
{
	ddc->dongle_type = dongle_type;
}

static uint32_t defer_delay_converter_wa(
	struct ddc_service *ddc,
	uint32_t defer_delay)
{
	struct dc_link *link = ddc->link;

	if (link->dpcd_caps.branch_dev_id == DP_BRANCH_DEVICE_ID_4 &&
		!memcmp(link->dpcd_caps.branch_dev_name,
			DP_DVI_CONVERTER_ID_4,
			sizeof(link->dpcd_caps.branch_dev_name)))
		return defer_delay > I2C_OVER_AUX_DEFER_WA_DELAY ?
			defer_delay : I2C_OVER_AUX_DEFER_WA_DELAY;

	return defer_delay;
}

#define DP_TRANSLATOR_DELAY 5

uint32_t get_defer_delay(struct ddc_service *ddc)
{
	uint32_t defer_delay = 0;

	switch (ddc->transaction_type) {
	case DDC_TRANSACTION_TYPE_I2C_OVER_AUX:
		if ((DISPLAY_DONGLE_DP_VGA_CONVERTER == ddc->dongle_type) ||
			(DISPLAY_DONGLE_DP_DVI_CONVERTER == ddc->dongle_type) ||
			(DISPLAY_DONGLE_DP_HDMI_CONVERTER ==
				ddc->dongle_type)) {

			defer_delay = DP_TRANSLATOR_DELAY;

			defer_delay =
				defer_delay_converter_wa(ddc, defer_delay);

		} else /*sink has a delay different from an Active Converter*/
			defer_delay = 0;
		break;
	case DDC_TRANSACTION_TYPE_I2C_OVER_AUX_WITH_DEFER:
		defer_delay = DP_TRANSLATOR_DELAY;
		break;
	default:
		break;
	}
	return defer_delay;
}

static bool i2c_read(
	struct ddc_service *ddc,
	uint32_t address,
	uint8_t *buffer,
	uint32_t len)
{
	uint8_t offs_data = 0;
	struct i2c_payload payloads[2] = {
		{
		.write = true,
		.address = address,
		.length = 1,
		.data = &offs_data },
		{
		.write = false,
		.address = address,
		.length = len,
		.data = buffer } };

	struct i2c_command command = {
		.payloads = payloads,
		.number_of_payloads = 2,
		.engine = DDC_I2C_COMMAND_ENGINE,
		.speed = ddc->ctx->dc->caps.i2c_speed_in_khz };

	return dm_helpers_submit_i2c(
			ddc->ctx,
			ddc->link,
			&command);
}

void dal_ddc_service_i2c_query_dp_dual_mode_adaptor(
	struct ddc_service *ddc,
	struct display_sink_capability *sink_cap)
{
	uint8_t i;
	bool is_valid_hdmi_signature;
	enum display_dongle_type *dongle = &sink_cap->dongle_type;
	uint8_t type2_dongle_buf[DP_ADAPTOR_TYPE2_SIZE];
	bool is_type2_dongle = false;
	int retry_count = 2;
	struct dp_hdmi_dongle_signature_data *dongle_signature;

	/* Assume we have no valid DP passive dongle connected */
	*dongle = DISPLAY_DONGLE_NONE;
	sink_cap->max_hdmi_pixel_clock = DP_ADAPTOR_HDMI_SAFE_MAX_TMDS_CLK;

	/* Read DP-HDMI dongle I2c (no response interpreted as DP-DVI dongle)*/
	if (!i2c_read(
		ddc,
		DP_HDMI_DONGLE_ADDRESS,
		type2_dongle_buf,
		sizeof(type2_dongle_buf))) {
		/* Passive HDMI dongles can sometimes fail here without retrying*/
		while (retry_count > 0) {
			if (i2c_read(ddc,
				DP_HDMI_DONGLE_ADDRESS,
				type2_dongle_buf,
				sizeof(type2_dongle_buf)))
				break;
			retry_count--;
		}
		if (retry_count == 0) {
			*dongle = DISPLAY_DONGLE_DP_DVI_DONGLE;
			sink_cap->max_hdmi_pixel_clock = DP_ADAPTOR_DVI_MAX_TMDS_CLK;

			CONN_DATA_DETECT(ddc->link, type2_dongle_buf, sizeof(type2_dongle_buf),
					"DP-DVI passive dongle %dMhz: ",
					DP_ADAPTOR_DVI_MAX_TMDS_CLK / 1000);
			return;
		}
	}

	/* Check if Type 2 dongle.*/
	if (type2_dongle_buf[DP_ADAPTOR_TYPE2_REG_ID] == DP_ADAPTOR_TYPE2_ID)
		is_type2_dongle = true;

	dongle_signature =
		(struct dp_hdmi_dongle_signature_data *)type2_dongle_buf;

	is_valid_hdmi_signature = true;

	/* Check EOT */
	if (dongle_signature->eot != DP_HDMI_DONGLE_SIGNATURE_EOT) {
		is_valid_hdmi_signature = false;
	}

	/* Check signature */
	for (i = 0; i < sizeof(dongle_signature->id); ++i) {
		/* If its not the right signature,
		 * skip mismatch in subversion byte.*/
		if (dongle_signature->id[i] !=
			dp_hdmi_dongle_signature_str[i] && i != 3) {

			if (is_type2_dongle) {
				is_valid_hdmi_signature = false;
				break;
			}

		}
	}

	if (is_type2_dongle) {
		uint32_t max_tmds_clk =
			type2_dongle_buf[DP_ADAPTOR_TYPE2_REG_MAX_TMDS_CLK];

		max_tmds_clk = max_tmds_clk * 2 + max_tmds_clk / 2;

		if (0 == max_tmds_clk ||
				max_tmds_clk < DP_ADAPTOR_TYPE2_MIN_TMDS_CLK ||
				max_tmds_clk > DP_ADAPTOR_TYPE2_MAX_TMDS_CLK) {
			*dongle = DISPLAY_DONGLE_DP_DVI_DONGLE;

			CONN_DATA_DETECT(ddc->link, type2_dongle_buf,
					sizeof(type2_dongle_buf),
					"DP-DVI passive dongle %dMhz: ",
					DP_ADAPTOR_DVI_MAX_TMDS_CLK / 1000);
		} else {
			if (is_valid_hdmi_signature == true) {
				*dongle = DISPLAY_DONGLE_DP_HDMI_DONGLE;

				CONN_DATA_DETECT(ddc->link, type2_dongle_buf,
						sizeof(type2_dongle_buf),
						"Type 2 DP-HDMI passive dongle %dMhz: ",
						max_tmds_clk);
			} else {
				*dongle = DISPLAY_DONGLE_DP_HDMI_MISMATCHED_DONGLE;

				CONN_DATA_DETECT(ddc->link, type2_dongle_buf,
						sizeof(type2_dongle_buf),
						"Type 2 DP-HDMI passive dongle (no signature) %dMhz: ",
						max_tmds_clk);

			}

			/* Multiply by 1000 to convert to kHz. */
			sink_cap->max_hdmi_pixel_clock =
				max_tmds_clk * 1000;
		}

	} else {
		if (is_valid_hdmi_signature == true) {
			*dongle = DISPLAY_DONGLE_DP_HDMI_DONGLE;

			CONN_DATA_DETECT(ddc->link, type2_dongle_buf,
					sizeof(type2_dongle_buf),
					"Type 1 DP-HDMI passive dongle %dMhz: ",
					sink_cap->max_hdmi_pixel_clock / 1000);
		} else {
			*dongle = DISPLAY_DONGLE_DP_HDMI_MISMATCHED_DONGLE;

			CONN_DATA_DETECT(ddc->link, type2_dongle_buf,
					sizeof(type2_dongle_buf),
					"Type 1 DP-HDMI passive dongle (no signature) %dMhz: ",
					sink_cap->max_hdmi_pixel_clock / 1000);
		}
	}

	return;
}

enum {
	DP_SINK_CAP_SIZE =
		DP_EDP_CONFIGURATION_CAP - DP_DPCD_REV + 1
};

bool dal_ddc_service_query_ddc_data(
	struct ddc_service *ddc,
	uint32_t address,
	uint8_t *write_buf,
	uint32_t write_size,
	uint8_t *read_buf,
	uint32_t read_size)
{
	bool ret;
	uint32_t payload_size =
		dal_ddc_service_is_in_aux_transaction_mode(ddc) ?
			DEFAULT_AUX_MAX_DATA_SIZE : EDID_SEGMENT_SIZE;

	uint32_t write_payloads =
		(write_size + payload_size - 1) / payload_size;

	uint32_t read_payloads =
		(read_size + payload_size - 1) / payload_size;

	uint32_t payloads_num = write_payloads + read_payloads;

	if (write_size > EDID_SEGMENT_SIZE || read_size > EDID_SEGMENT_SIZE)
		return false;

	/*TODO: len of payload data for i2c and aux is uint8!!!!,
	 *  but we want to read 256 over i2c!!!!*/
	if (dal_ddc_service_is_in_aux_transaction_mode(ddc)) {

		struct aux_payloads *payloads =
			dal_ddc_aux_payloads_create(ddc->ctx, payloads_num);

		struct aux_command command = {
			.payloads = dal_ddc_aux_payloads_get(payloads),
			.number_of_payloads = 0,
			.defer_delay = get_defer_delay(ddc),
			.max_defer_write_retry = 0 };

		dal_ddc_aux_payloads_add(
			payloads, address, write_size, write_buf, true);

		dal_ddc_aux_payloads_add(
			payloads, address, read_size, read_buf, false);

		command.number_of_payloads =
			dal_ddc_aux_payloads_get_count(payloads);

		ret = dal_i2caux_submit_aux_command(
				ddc->ctx->i2caux,
				ddc->ddc_pin,
				&command);

		dal_ddc_aux_payloads_destroy(&payloads);

	} else {
		struct i2c_payloads *payloads =
			dal_ddc_i2c_payloads_create(ddc->ctx, payloads_num);

		struct i2c_command command = {
			.payloads = dal_ddc_i2c_payloads_get(payloads),
			.number_of_payloads = 0,
			.engine = DDC_I2C_COMMAND_ENGINE,
			.speed = ddc->ctx->dc->caps.i2c_speed_in_khz };

		dal_ddc_i2c_payloads_add(
			payloads, address, write_size, write_buf, true);

		dal_ddc_i2c_payloads_add(
			payloads, address, read_size, read_buf, false);

		command.number_of_payloads =
			dal_ddc_i2c_payloads_get_count(payloads);

		ret = dm_helpers_submit_i2c(
				ddc->ctx,
				ddc->link,
				&command);

		dal_ddc_i2c_payloads_destroy(&payloads);
	}

	return ret;
}

int dc_link_aux_transfer(struct ddc_service *ddc,
			     unsigned int address,
			     uint8_t *reply,
			     void *buffer,
			     unsigned int size,
			     enum aux_transaction_type type,
			     enum i2caux_transaction_action action)
{
	struct ddc *ddc_pin = ddc->ddc_pin;
	struct aux_engine *aux_engine;
	enum aux_channel_operation_result operation_result;
	struct aux_request_transaction_data aux_req;
	struct aux_reply_transaction_data aux_rep;
	uint8_t returned_bytes = 0;
	int res = -1;
	uint32_t status;

	memset(&aux_req, 0, sizeof(aux_req));
	memset(&aux_rep, 0, sizeof(aux_rep));

	aux_engine = ddc->ctx->dc->res_pool->engines[ddc_pin->pin_data->en];
	aux_engine->funcs->acquire(aux_engine, ddc_pin);

	aux_req.type = type;
	aux_req.action = action;

	aux_req.address = address;
	aux_req.delay = 0;
	aux_req.length = size;
	aux_req.data = buffer;

	aux_engine->funcs->submit_channel_request(aux_engine, &aux_req);
	operation_result = aux_engine->funcs->get_channel_status(aux_engine, &returned_bytes);

	switch (operation_result) {
	case AUX_CHANNEL_OPERATION_SUCCEEDED:
		res = returned_bytes;

		if (res <= size && res >= 0)
			res = aux_engine->funcs->read_channel_reply(aux_engine, size,
								buffer, reply,
								&status);

		break;
	case AUX_CHANNEL_OPERATION_FAILED_HPD_DISCON:
		res = 0;
		break;
	case AUX_CHANNEL_OPERATION_FAILED_REASON_UNKNOWN:
	case AUX_CHANNEL_OPERATION_FAILED_INVALID_REPLY:
	case AUX_CHANNEL_OPERATION_FAILED_TIMEOUT:
		res = -1;
		break;
	}
	aux_engine->funcs->release_engine(aux_engine);
	return res;
}

/*test only function*/
void dal_ddc_service_set_ddc_pin(
	struct ddc_service *ddc_service,
	struct ddc *ddc)
{
	ddc_service->ddc_pin = ddc;
}

struct ddc *dal_ddc_service_get_ddc_pin(struct ddc_service *ddc_service)
{
	return ddc_service->ddc_pin;
}

void dal_ddc_service_write_scdc_data(struct ddc_service *ddc_service,
		uint32_t pix_clk,
		bool lte_340_scramble)
{
	bool over_340_mhz = pix_clk > 340000 ? 1 : 0;
	uint8_t slave_address = HDMI_SCDC_ADDRESS;
	uint8_t offset = HDMI_SCDC_SINK_VERSION;
	uint8_t sink_version = 0;
	uint8_t write_buffer[2] = {0};
	/*Lower than 340 Scramble bit from SCDC caps*/

	dal_ddc_service_query_ddc_data(ddc_service, slave_address, &offset,
			sizeof(offset), &sink_version, sizeof(sink_version));
	if (sink_version == 1) {
		/*Source Version = 1*/
		write_buffer[0] = HDMI_SCDC_SOURCE_VERSION;
		write_buffer[1] = 1;
		dal_ddc_service_query_ddc_data(ddc_service, slave_address,
				write_buffer, sizeof(write_buffer), NULL, 0);
		/*Read Request from SCDC caps*/
	}
	write_buffer[0] = HDMI_SCDC_TMDS_CONFIG;

	if (over_340_mhz) {
		write_buffer[1] = 3;
	} else if (lte_340_scramble) {
		write_buffer[1] = 1;
	} else {
		write_buffer[1] = 0;
	}
	dal_ddc_service_query_ddc_data(ddc_service, slave_address, write_buffer,
			sizeof(write_buffer), NULL, 0);
}

void dal_ddc_service_read_scdc_data(struct ddc_service *ddc_service)
{
	uint8_t slave_address = HDMI_SCDC_ADDRESS;
	uint8_t offset = HDMI_SCDC_TMDS_CONFIG;
	uint8_t tmds_config = 0;

	dal_ddc_service_query_ddc_data(ddc_service, slave_address, &offset,
			sizeof(offset), &tmds_config, sizeof(tmds_config));
	if (tmds_config & 0x1) {
		union hdmi_scdc_status_flags_data status_data = { {0} };
		uint8_t scramble_status = 0;

		offset = HDMI_SCDC_SCRAMBLER_STATUS;
		dal_ddc_service_query_ddc_data(ddc_service, slave_address,
				&offset, sizeof(offset), &scramble_status,
				sizeof(scramble_status));
		offset = HDMI_SCDC_STATUS_FLAGS;
		dal_ddc_service_query_ddc_data(ddc_service, slave_address,
				&offset, sizeof(offset), status_data.byte,
				sizeof(status_data.byte));
	}
}

