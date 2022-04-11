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

#include <linux/slab.h>

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
#include "dce/dce_aux.h"
#include "dmub/inc/dmub_cmd.h"

#define DC_LOGGER_INIT(logger)

static const uint8_t DP_VGA_DONGLE_BRANCH_DEV_NAME[] = "DpVga";
/* DP to Dual link DVI converter */
static const uint8_t DP_DVI_CONVERTER_ID_4[] = "m2DVIa";
static const uint8_t DP_DVI_CONVERTER_ID_5[] = "3393N2";

#define AUX_POWER_UP_WA_DELAY 500
#define I2C_OVER_AUX_DEFER_WA_DELAY 70
#define DPVGA_DONGLE_AUX_DEFER_WA_DELAY 40
#define I2C_OVER_AUX_DEFER_WA_DELAY_1MS 1

/* CV smart dongle slave address for retrieving supported HDTV modes*/
#define CV_SMART_DONGLE_ADDRESS 0x20
/* DVI-HDMI dongle slave address for retrieving dongle signature*/
#define DVI_HDMI_DONGLE_ADDRESS 0x68
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
		uint8_t RESERVED3:8;

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
		uint8_t RESERVED:8;
		uint8_t RESERVED2:8;
		uint8_t RESERVED3:8;
		uint8_t RESERVED4:4;
	} fields;
};

struct i2c_payloads {
	struct vector payloads;
};

struct aux_payloads {
	struct vector payloads;
};

static bool dal_ddc_i2c_payloads_create(
		struct dc_context *ctx,
		struct i2c_payloads *payloads,
		uint32_t count)
{
	if (dal_vector_construct(
		&payloads->payloads, ctx, count, sizeof(struct i2c_payload)))
		return true;

	return false;
}

static struct i2c_payload *dal_ddc_i2c_payloads_get(struct i2c_payloads *p)
{
	return (struct i2c_payload *)p->payloads.container;
}

static uint32_t dal_ddc_i2c_payloads_get_count(struct i2c_payloads *p)
{
	return p->payloads.count;
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

static void ddc_service_construct(
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

	if (init_data->is_dpia_link ||
	    dcb->funcs->get_i2c_info(dcb, init_data->id, &i2c_info) != BP_RESULT_OK) {
		ddc_service->ddc_pin = NULL;
	} else {
		DC_LOGGER_INIT(ddc_service->ctx->logger);
		DC_LOG_DC("BIOS object table - i2c_line: %d", i2c_info.i2c_line);
		DC_LOG_DC("BIOS object table - i2c_engine_id: %d", i2c_info.i2c_engine_id);

		hw_info.ddc_channel = i2c_info.i2c_line;
		if (ddc_service->link != NULL)
			hw_info.hw_supported = i2c_info.i2c_hw_assist;
		else
			hw_info.hw_supported = false;

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

	ddc_service_construct(ddc_service, init_data);
	return ddc_service;
}

static void ddc_service_destruct(struct ddc_service *ddc)
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
	ddc_service_destruct(*ddc);
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

	if (link->dpcd_caps.dongle_type == DISPLAY_DONGLE_DP_VGA_CONVERTER &&
		link->dpcd_caps.branch_dev_id == DP_BRANCH_DEVICE_ID_0080E1 &&
		(link->dpcd_caps.branch_fw_revision[0] < 0x01 ||
				(link->dpcd_caps.branch_fw_revision[0] == 0x01 &&
				link->dpcd_caps.branch_fw_revision[1] < 0x40)) &&
		!memcmp(link->dpcd_caps.branch_dev_name,
		    DP_VGA_DONGLE_BRANCH_DEV_NAME,
			sizeof(link->dpcd_caps.branch_dev_name)))

		return defer_delay > DPVGA_DONGLE_AUX_DEFER_WA_DELAY ?
			defer_delay : DPVGA_DONGLE_AUX_DEFER_WA_DELAY;

	if (link->dpcd_caps.branch_dev_id == DP_BRANCH_DEVICE_ID_0080E1 &&
	    !memcmp(link->dpcd_caps.branch_dev_name,
		    DP_DVI_CONVERTER_ID_4,
		    sizeof(link->dpcd_caps.branch_dev_name)))
		return defer_delay > I2C_OVER_AUX_DEFER_WA_DELAY ?
			defer_delay : I2C_OVER_AUX_DEFER_WA_DELAY;
	if (link->dpcd_caps.branch_dev_id == DP_BRANCH_DEVICE_ID_006037 &&
	    !memcmp(link->dpcd_caps.branch_dev_name,
		    DP_DVI_CONVERTER_ID_5,
		    sizeof(link->dpcd_caps.branch_dev_name)))
		return defer_delay > I2C_OVER_AUX_DEFER_WA_DELAY_1MS ?
			I2C_OVER_AUX_DEFER_WA_DELAY_1MS : defer_delay;

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
		sink_cap->is_dongle_type_one = false;

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
		sink_cap->is_dongle_type_one = true;
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
	bool success = true;
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

	if (!payloads_num)
		return false;

	/*TODO: len of payload data for i2c and aux is uint8!!!!,
	 *  but we want to read 256 over i2c!!!!*/
	if (dal_ddc_service_is_in_aux_transaction_mode(ddc)) {
		struct aux_payload payload;

		payload.i2c_over_aux = true;
		payload.address = address;
		payload.reply = NULL;
		payload.defer_delay = get_defer_delay(ddc);
		payload.write_status_update = false;

		if (write_size != 0) {
			payload.write = true;
			/* should not set mot (middle of transaction) to 0
			 * if there are pending read payloads
			 */
			payload.mot = !(read_size == 0);
			payload.length = write_size;
			payload.data = write_buf;

			success = dal_ddc_submit_aux_command(ddc, &payload);
		}

		if (read_size != 0 && success) {
			payload.write = false;
			/* should set mot (middle of transaction) to 0
			 * since it is the last payload to send
			 */
			payload.mot = false;
			payload.length = read_size;
			payload.data = read_buf;

			success = dal_ddc_submit_aux_command(ddc, &payload);
		}
	} else {
		struct i2c_command command = {0};
		struct i2c_payloads payloads;

		if (!dal_ddc_i2c_payloads_create(ddc->ctx, &payloads, payloads_num))
			return false;

		command.payloads = dal_ddc_i2c_payloads_get(&payloads);
		command.number_of_payloads = 0;
		command.engine = DDC_I2C_COMMAND_ENGINE;
		command.speed = ddc->ctx->dc->caps.i2c_speed_in_khz;

		dal_ddc_i2c_payloads_add(
			&payloads, address, write_size, write_buf, true);

		dal_ddc_i2c_payloads_add(
			&payloads, address, read_size, read_buf, false);

		command.number_of_payloads =
			dal_ddc_i2c_payloads_get_count(&payloads);

		success = dm_helpers_submit_i2c(
				ddc->ctx,
				ddc->link,
				&command);

		dal_vector_destruct(&payloads.payloads);
	}

	return success;
}

bool dal_ddc_submit_aux_command(struct ddc_service *ddc,
		struct aux_payload *payload)
{
	uint32_t retrieved = 0;
	bool ret = false;

	if (!ddc)
		return false;

	if (!payload)
		return false;

	do {
		struct aux_payload current_payload;
		bool is_end_of_payload = (retrieved + DEFAULT_AUX_MAX_DATA_SIZE) >=
				payload->length;
		uint32_t payload_length = is_end_of_payload ?
				payload->length - retrieved : DEFAULT_AUX_MAX_DATA_SIZE;

		current_payload.address = payload->address;
		current_payload.data = &payload->data[retrieved];
		current_payload.defer_delay = payload->defer_delay;
		current_payload.i2c_over_aux = payload->i2c_over_aux;
		current_payload.length = payload_length;
		/* set mot (middle of transaction) to false if it is the last payload */
		current_payload.mot = is_end_of_payload ? payload->mot:true;
		current_payload.write_status_update = false;
		current_payload.reply = payload->reply;
		current_payload.write = payload->write;

		ret = dc_link_aux_transfer_with_retries(ddc, &current_payload);

		retrieved += payload_length;
	} while (retrieved < payload->length && ret == true);

	return ret;
}

/* dc_link_aux_transfer_raw() - Attempt to transfer
 * the given aux payload.  This function does not perform
 * retries or handle error states.  The reply is returned
 * in the payload->reply and the result through
 * *operation_result.  Returns the number of bytes transferred,
 * or -1 on a failure.
 */
int dc_link_aux_transfer_raw(struct ddc_service *ddc,
		struct aux_payload *payload,
		enum aux_return_code_type *operation_result)
{
	if (ddc->ctx->dc->debug.enable_dmub_aux_for_legacy_ddc ||
	    !ddc->ddc_pin) {
		return dce_aux_transfer_dmub_raw(ddc, payload, operation_result);
	} else {
		return dce_aux_transfer_raw(ddc, payload, operation_result);
	}
}

/* dc_link_aux_transfer_with_retries() - Attempt to submit an
 * aux payload, retrying on timeouts, defers, and busy states
 * as outlined in the DP spec.  Returns true if the request
 * was successful.
 *
 * Unless you want to implement your own retry semantics, this
 * is probably the one you want.
 */
bool dc_link_aux_transfer_with_retries(struct ddc_service *ddc,
		struct aux_payload *payload)
{
	return dce_aux_transfer_with_retries(ddc, payload);
}


bool dc_link_aux_try_to_configure_timeout(struct ddc_service *ddc,
		uint32_t timeout)
{
	bool result = false;
	struct ddc *ddc_pin = ddc->ddc_pin;

	/* Do not try to access nonexistent DDC pin. */
	if (ddc->link->ep_type != DISPLAY_ENDPOINT_PHY)
		return true;

	if (ddc->ctx->dc->res_pool->engines[ddc_pin->pin_data->en]->funcs->configure_timeout) {
		ddc->ctx->dc->res_pool->engines[ddc_pin->pin_data->en]->funcs->configure_timeout(ddc, timeout);
		result = true;
	}
	return result;
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

	if (ddc_service->link->local_sink &&
		ddc_service->link->local_sink->edid_caps.panel_patch.skip_scdc_overwrite)
		return;

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

	if (ddc_service->link->local_sink &&
		ddc_service->link->local_sink->edid_caps.panel_patch.skip_scdc_overwrite)
		return;

	dal_ddc_service_query_ddc_data(ddc_service, slave_address, &offset,
			sizeof(offset), &tmds_config, sizeof(tmds_config));
	if (tmds_config & 0x1) {
		union hdmi_scdc_status_flags_data status_data = {0};
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

