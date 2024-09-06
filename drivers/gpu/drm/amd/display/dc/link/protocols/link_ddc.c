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

/* FILE POLICY AND INTENDED USAGE:
 *
 * This file implements generic display communication protocols such as i2c, aux
 * and scdc. The file should not contain any specific applications of these
 * protocols such as display capability query, detection, or handshaking such as
 * link training.
 */
#include "link_ddc.h"
#include "vector.h"
#include "dce/dce_aux.h"
#include "dal_asic_id.h"
#include "link_dpcd.h"
#include "dm_helpers.h"
#include "atomfirmware.h"

#define DC_LOGGER \
	ddc_service->ctx->logger
#define DC_LOGGER_INIT(logger)

static const uint8_t DP_VGA_DONGLE_BRANCH_DEV_NAME[] = "DpVga";
/* DP to Dual link DVI converter */
static const uint8_t DP_DVI_CONVERTER_ID_4[] = "m2DVIa";
static const uint8_t DP_DVI_CONVERTER_ID_5[] = "3393N2";

struct i2c_payloads {
	struct vector payloads;
};

static bool i2c_payloads_create(
		struct dc_context *ctx,
		struct i2c_payloads *payloads,
		uint32_t count)
{
	if (dal_vector_construct(
		&payloads->payloads, ctx, count, sizeof(struct i2c_payload)))
		return true;

	return false;
}

static struct i2c_payload *i2c_payloads_get(struct i2c_payloads *p)
{
	return (struct i2c_payload *)p->payloads.container;
}

static uint32_t i2c_payloads_get_count(struct i2c_payloads *p)
{
	return p->payloads.count;
}

static void i2c_payloads_destroy(struct i2c_payloads *p)
{
	if (!p)
		return;

	dal_vector_destruct(&p->payloads);
}

#define DDC_MIN(a, b) (((a) < (b)) ? (a) : (b))

static void i2c_payloads_add(
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

struct ddc_service *link_create_ddc_service(
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

void link_destroy_ddc_service(struct ddc_service **ddc)
{
	if (!ddc || !*ddc) {
		BREAK_TO_DEBUGGER();
		return;
	}
	ddc_service_destruct(*ddc);
	kfree(*ddc);
	*ddc = NULL;
}

void set_ddc_transaction_type(
	struct ddc_service *ddc,
	enum ddc_transaction_type type)
{
	ddc->transaction_type = type;
}

bool link_is_in_aux_transaction_mode(struct ddc_service *ddc)
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

void set_dongle_type(struct ddc_service *ddc,
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

uint32_t link_get_aux_defer_delay(struct ddc_service *ddc)
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

static bool submit_aux_command(struct ddc_service *ddc,
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

		ret = link_aux_transfer_with_retries_no_mutex(ddc, &current_payload);

		retrieved += payload_length;
	} while (retrieved < payload->length && ret == true);

	return ret;
}

bool link_query_ddc_data(
	struct ddc_service *ddc,
	uint32_t address,
	uint8_t *write_buf,
	uint32_t write_size,
	uint8_t *read_buf,
	uint32_t read_size)
{
	bool success = true;
	uint32_t payload_size =
		link_is_in_aux_transaction_mode(ddc) ?
			DEFAULT_AUX_MAX_DATA_SIZE : EDID_SEGMENT_SIZE;

	uint32_t write_payloads =
		(write_size + payload_size - 1) / payload_size;

	uint32_t read_payloads =
		(read_size + payload_size - 1) / payload_size;

	uint32_t payloads_num = write_payloads + read_payloads;

	if (!payloads_num)
		return false;

	if (link_is_in_aux_transaction_mode(ddc)) {
		struct aux_payload payload;

		payload.i2c_over_aux = true;
		payload.address = address;
		payload.reply = NULL;
		payload.defer_delay = link_get_aux_defer_delay(ddc);
		payload.write_status_update = false;

		if (write_size != 0) {
			payload.write = true;
			/* should not set mot (middle of transaction) to 0
			 * if there are pending read payloads
			 */
			payload.mot = !(read_size == 0);
			payload.length = write_size;
			payload.data = write_buf;

			success = submit_aux_command(ddc, &payload);
		}

		if (read_size != 0 && success) {
			payload.write = false;
			/* should set mot (middle of transaction) to 0
			 * since it is the last payload to send
			 */
			payload.mot = false;
			payload.length = read_size;
			payload.data = read_buf;

			success = submit_aux_command(ddc, &payload);
		}
	} else {
		struct i2c_command command = {0};
		struct i2c_payloads payloads;

		if (!i2c_payloads_create(ddc->ctx, &payloads, payloads_num))
			return false;

		command.payloads = i2c_payloads_get(&payloads);
		command.number_of_payloads = 0;
		command.engine = DDC_I2C_COMMAND_ENGINE;
		command.speed = ddc->ctx->dc->caps.i2c_speed_in_khz;

		i2c_payloads_add(
			&payloads, address, write_size, write_buf, true);

		i2c_payloads_add(
			&payloads, address, read_size, read_buf, false);

		command.number_of_payloads =
			i2c_payloads_get_count(&payloads);

		success = dm_helpers_submit_i2c(
				ddc->ctx,
				ddc->link,
				&command);

		i2c_payloads_destroy(&payloads);
	}

	return success;
}

int link_aux_transfer_raw(struct ddc_service *ddc,
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

uint32_t link_get_fixed_vs_pe_retimer_write_address(struct dc_link *link)
{
	uint32_t vendor_lttpr_write_address = 0xF004F;
	uint8_t offset;

	switch (link->dpcd_caps.lttpr_caps.phy_repeater_cnt) {
	case 0x80: // 1 lttpr repeater
		offset =  1;
		break;
	case 0x40: // 2 lttpr repeaters
		offset = 2;
		break;
	case 0x20: // 3 lttpr repeaters
		offset = 3;
		break;
	case 0x10: // 4 lttpr repeaters
		offset = 4;
		break;
	case 0x08: // 5 lttpr repeaters
		offset = 5;
		break;
	case 0x04: // 6 lttpr repeaters
		offset = 6;
		break;
	case 0x02: // 7 lttpr repeaters
		offset = 7;
		break;
	case 0x01: // 8 lttpr repeaters
		offset = 8;
		break;
	default:
		offset = 0xFF;
	}

	if (offset != 0xFF) {
		vendor_lttpr_write_address +=
				((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (offset - 1));
	}
	return vendor_lttpr_write_address;
}

uint32_t link_get_fixed_vs_pe_retimer_read_address(struct dc_link *link)
{
	return link_get_fixed_vs_pe_retimer_write_address(link) + 4;
}

bool link_configure_fixed_vs_pe_retimer(struct ddc_service *ddc, const uint8_t *data, uint32_t length)
{
	struct aux_payload write_payload = {
		.i2c_over_aux = false,
		.write = true,
		.address = link_get_fixed_vs_pe_retimer_write_address(ddc->link),
		.length = length,
		.data = (uint8_t *) data,
		.reply = NULL,
		.mot = I2C_MOT_UNDEF,
		.write_status_update = false,
		.defer_delay = 0,
	};

	return link_aux_transfer_with_retries_no_mutex(ddc,
			&write_payload);
}

bool link_query_fixed_vs_pe_retimer(struct ddc_service *ddc, uint8_t *data, uint32_t length)
{
	struct aux_payload read_payload = {
		.i2c_over_aux = false,
		.write = false,
		.address = link_get_fixed_vs_pe_retimer_read_address(ddc->link),
		.length = length,
		.data = data,
		.reply = NULL,
		.mot = I2C_MOT_UNDEF,
		.write_status_update = false,
		.defer_delay = 0,
	};

	return link_aux_transfer_with_retries_no_mutex(ddc,
			&read_payload);
}

bool link_aux_transfer_with_retries_no_mutex(struct ddc_service *ddc,
		struct aux_payload *payload)
{
	return dce_aux_transfer_with_retries(ddc, payload);
}


bool try_to_configure_aux_timeout(struct ddc_service *ddc,
		uint32_t timeout)
{
	bool result = false;
	struct ddc *ddc_pin = ddc->ddc_pin;

	if ((ddc->link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) &&
			!ddc->link->dc->debug.disable_fixed_vs_aux_timeout_wa &&
			ddc->ctx->dce_version == DCN_VERSION_3_1) {
		/* Fixed VS workaround for AUX timeout */
		const uint32_t fixed_vs_address = 0xF004F;
		const uint8_t fixed_vs_data[4] = {0x1, 0x22, 0x63, 0xc};

		core_link_write_dpcd(ddc->link,
				fixed_vs_address,
				fixed_vs_data,
				sizeof(fixed_vs_data));

		timeout = 3072;
	}

	/* Do not try to access nonexistent DDC pin. */
	if (ddc->link->ep_type != DISPLAY_ENDPOINT_PHY)
		return true;

	if (ddc->ctx->dc->res_pool->engines[ddc_pin->pin_data->en]->funcs->configure_timeout) {
		ddc->ctx->dc->res_pool->engines[ddc_pin->pin_data->en]->funcs->configure_timeout(ddc, timeout);
		result = true;
	}

	return result;
}

struct ddc *get_ddc_pin(struct ddc_service *ddc_service)
{
	return ddc_service->ddc_pin;
}

void write_scdc_data(struct ddc_service *ddc_service,
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

	link_query_ddc_data(ddc_service, slave_address, &offset,
			sizeof(offset), &sink_version, sizeof(sink_version));
	if (sink_version == 1) {
		/*Source Version = 1*/
		write_buffer[0] = HDMI_SCDC_SOURCE_VERSION;
		write_buffer[1] = 1;
		link_query_ddc_data(ddc_service, slave_address,
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
	link_query_ddc_data(ddc_service, slave_address, write_buffer,
			sizeof(write_buffer), NULL, 0);
}

void read_scdc_data(struct ddc_service *ddc_service)
{
	uint8_t slave_address = HDMI_SCDC_ADDRESS;
	uint8_t offset = HDMI_SCDC_TMDS_CONFIG;
	uint8_t tmds_config = 0;

	if (ddc_service->link->local_sink &&
		ddc_service->link->local_sink->edid_caps.panel_patch.skip_scdc_overwrite)
		return;

	link_query_ddc_data(ddc_service, slave_address, &offset,
			sizeof(offset), &tmds_config, sizeof(tmds_config));
	if (tmds_config & 0x1) {
		union hdmi_scdc_status_flags_data status_data = {0};
		uint8_t scramble_status = 0;

		offset = HDMI_SCDC_SCRAMBLER_STATUS;
		link_query_ddc_data(ddc_service, slave_address,
				&offset, sizeof(offset), &scramble_status,
				sizeof(scramble_status));
		offset = HDMI_SCDC_STATUS_FLAGS;
		link_query_ddc_data(ddc_service, slave_address,
				&offset, sizeof(offset), &status_data.byte,
				sizeof(status_data.byte));
	}
}
