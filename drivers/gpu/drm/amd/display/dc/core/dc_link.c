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
#include "dc.h"
#include "core_dc.h"
#include "grph_object_id.h"
#include "gpio_service_interface.h"
#include "core_status.h"
#include "dc_link_dp.h"
#include "dc_link_ddc.h"
#include "link_hwss.h"
#include "stream_encoder.h"
#include "link_encoder.h"
#include "hw_sequencer.h"
#include "resource.h"
#include "fixed31_32.h"
#include "dpcd_defs.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_enum.h"
#include "dce/dce_11_0_sh_mask.h"

#ifndef mmDMCU_STATUS__UC_IN_RESET__SHIFT
#define mmDMCU_STATUS__UC_IN_RESET__SHIFT 0x0
#endif

#ifndef mmDMCU_STATUS__UC_IN_RESET_MASK
#define mmDMCU_STATUS__UC_IN_RESET_MASK 0x00000001L
#endif

#define LINK_INFO(...) \
	dm_logger_write(dc_ctx->logger, LOG_HW_HOTPLUG, \
		__VA_ARGS__)

/*******************************************************************************
 * Private structures
 ******************************************************************************/

enum {
	LINK_RATE_REF_FREQ_IN_MHZ = 27,
	PEAK_FACTOR_X1000 = 1006
};

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static void destruct(struct core_link *link)
{
	int i;

	if (link->ddc)
		dal_ddc_service_destroy(&link->ddc);

	if(link->link_enc)
		link->link_enc->funcs->destroy(&link->link_enc);

	if (link->public.local_sink)
		dc_sink_release(link->public.local_sink);

	for (i = 0; i < link->public.sink_count; ++i)
		dc_sink_release(link->public.remote_sinks[i]);
}

static struct gpio *get_hpd_gpio(const struct core_link *link)
{
	enum bp_result bp_result;
	struct dc_bios *dcb = link->ctx->dc_bios;
	struct graphics_object_hpd_info hpd_info;
	struct gpio_pin_info pin_info;

	if (dcb->funcs->get_hpd_info(dcb, link->link_id, &hpd_info) != BP_RESULT_OK)
		return NULL;

	bp_result = dcb->funcs->get_gpio_pin_info(dcb,
		hpd_info.hpd_int_gpio_uid, &pin_info);

	if (bp_result != BP_RESULT_OK) {
		ASSERT(bp_result == BP_RESULT_NORECORD);
		return NULL;
	}

	return dal_gpio_service_create_irq(
		link->ctx->gpio_service,
		pin_info.offset,
		pin_info.mask);
}

/*
 *  Function: program_hpd_filter
 *
 *  @brief
 *     Programs HPD filter on associated HPD line
 *
 *  @param [in] delay_on_connect_in_ms: Connect filter timeout
 *  @param [in] delay_on_disconnect_in_ms: Disconnect filter timeout
 *
 *  @return
 *     true on success, false otherwise
 */
static bool program_hpd_filter(
	const struct core_link *link)
{
	bool result = false;

	struct gpio *hpd;

	int delay_on_connect_in_ms = 0;
	int delay_on_disconnect_in_ms = 0;

	/* Verify feature is supported */
	switch (link->public.connector_signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
		/* Program hpd filter */
		delay_on_connect_in_ms = 500;
		delay_on_disconnect_in_ms = 100;
		break;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		/* Program hpd filter to allow DP signal to settle */
		/* 500:	not able to detect MST <-> SST switch as HPD is low for
		 * 	only 100ms on DELL U2413
		 * 0:	some passive dongle still show aux mode instead of i2c
		 * 20-50:not enough to hide bouncing HPD with passive dongle.
		 * 	also see intermittent i2c read issues.
		 */
		delay_on_connect_in_ms = 80;
		delay_on_disconnect_in_ms = 0;
		break;
	case SIGNAL_TYPE_LVDS:
	case SIGNAL_TYPE_EDP:
	default:
		/* Don't program hpd filter */
		return false;
	}

	/* Obtain HPD handle */
	hpd = get_hpd_gpio(link);

	if (!hpd)
		return result;

	/* Setup HPD filtering */
	if (dal_gpio_open(hpd, GPIO_MODE_INTERRUPT) == GPIO_RESULT_OK) {
		struct gpio_hpd_config config;

		config.delay_on_connect = delay_on_connect_in_ms;
		config.delay_on_disconnect = delay_on_disconnect_in_ms;

		dal_irq_setup_hpd_filter(hpd, &config);

		dal_gpio_close(hpd);

		result = true;
	} else {
		ASSERT_CRITICAL(false);
	}

	/* Release HPD handle */
	dal_gpio_destroy_irq(&hpd);

	return result;
}

static bool detect_sink(struct core_link *link, enum dc_connection_type *type)
{
	uint32_t is_hpd_high = 0;
	struct gpio *hpd_pin;

	/* todo: may need to lock gpio access */
	hpd_pin = get_hpd_gpio(link);
	if (hpd_pin == NULL)
		goto hpd_gpio_failure;

	dal_gpio_open(hpd_pin, GPIO_MODE_INTERRUPT);
	dal_gpio_get_value(hpd_pin, &is_hpd_high);
	dal_gpio_close(hpd_pin);
	dal_gpio_destroy_irq(&hpd_pin);

	if (is_hpd_high) {
		*type = dc_connection_single;
		/* TODO: need to do the actual detection */
	} else {
		*type = dc_connection_none;
	}

	return true;

hpd_gpio_failure:
	return false;
}

enum ddc_transaction_type get_ddc_transaction_type(
		enum signal_type sink_signal)
{
	enum ddc_transaction_type transaction_type = DDC_TRANSACTION_TYPE_NONE;

	switch (sink_signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_LVDS:
	case SIGNAL_TYPE_RGB:
		transaction_type = DDC_TRANSACTION_TYPE_I2C;
		break;

	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_EDP:
		transaction_type = DDC_TRANSACTION_TYPE_I2C_OVER_AUX;
		break;

	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		/* MST does not use I2COverAux, but there is the
		 * SPECIAL use case for "immediate dwnstrm device
		 * access" (EPR#370830). */
		transaction_type = DDC_TRANSACTION_TYPE_I2C_OVER_AUX;
		break;

	default:
		break;
	}

	return transaction_type;
}

static enum signal_type get_basic_signal_type(
	struct graphics_object_id encoder,
	struct graphics_object_id downstream)
{
	if (downstream.type == OBJECT_TYPE_CONNECTOR) {
		switch (downstream.id) {
		case CONNECTOR_ID_SINGLE_LINK_DVII:
			switch (encoder.id) {
			case ENCODER_ID_INTERNAL_DAC1:
			case ENCODER_ID_INTERNAL_KLDSCP_DAC1:
			case ENCODER_ID_INTERNAL_DAC2:
			case ENCODER_ID_INTERNAL_KLDSCP_DAC2:
				return SIGNAL_TYPE_RGB;
			default:
				return SIGNAL_TYPE_DVI_SINGLE_LINK;
			}
		break;
		case CONNECTOR_ID_DUAL_LINK_DVII:
		{
			switch (encoder.id) {
			case ENCODER_ID_INTERNAL_DAC1:
			case ENCODER_ID_INTERNAL_KLDSCP_DAC1:
			case ENCODER_ID_INTERNAL_DAC2:
			case ENCODER_ID_INTERNAL_KLDSCP_DAC2:
				return SIGNAL_TYPE_RGB;
			default:
				return SIGNAL_TYPE_DVI_DUAL_LINK;
			}
		}
		break;
		case CONNECTOR_ID_SINGLE_LINK_DVID:
			return SIGNAL_TYPE_DVI_SINGLE_LINK;
		case CONNECTOR_ID_DUAL_LINK_DVID:
			return SIGNAL_TYPE_DVI_DUAL_LINK;
		case CONNECTOR_ID_VGA:
			return SIGNAL_TYPE_RGB;
		case CONNECTOR_ID_HDMI_TYPE_A:
			return SIGNAL_TYPE_HDMI_TYPE_A;
		case CONNECTOR_ID_LVDS:
			return SIGNAL_TYPE_LVDS;
		case CONNECTOR_ID_DISPLAY_PORT:
			return SIGNAL_TYPE_DISPLAY_PORT;
		case CONNECTOR_ID_EDP:
			return SIGNAL_TYPE_EDP;
		default:
			return SIGNAL_TYPE_NONE;
		}
	} else if (downstream.type == OBJECT_TYPE_ENCODER) {
		switch (downstream.id) {
		case ENCODER_ID_EXTERNAL_NUTMEG:
		case ENCODER_ID_EXTERNAL_TRAVIS:
			return SIGNAL_TYPE_DISPLAY_PORT;
		default:
			return SIGNAL_TYPE_NONE;
		}
	}

	return SIGNAL_TYPE_NONE;
}

/*
 * @brief
 * Check whether there is a dongle on DP connector
 */
static bool is_dp_sink_present(struct core_link *link)
{
	enum gpio_result gpio_result;
	uint32_t clock_pin = 0;

	struct ddc *ddc;

	enum connector_id connector_id =
		dal_graphics_object_id_get_connector_id(link->link_id);

	bool present =
		((connector_id == CONNECTOR_ID_DISPLAY_PORT) ||
		(connector_id == CONNECTOR_ID_EDP));

	ddc = dal_ddc_service_get_ddc_pin(link->ddc);

	if (!ddc) {
		BREAK_TO_DEBUGGER();
		return present;
	}

	/* Open GPIO and set it to I2C mode */
	/* Note: this GpioMode_Input will be converted
	 * to GpioConfigType_I2cAuxDualMode in GPIO component,
	 * which indicates we need additional delay */

	if (GPIO_RESULT_OK != dal_ddc_open(
		ddc, GPIO_MODE_INPUT, GPIO_DDC_CONFIG_TYPE_MODE_I2C)) {
		dal_gpio_destroy_ddc(&ddc);

		return present;
	}

	/* Read GPIO: DP sink is present if both clock and data pins are zero */
	/* [anaumov] in DAL2, there was no check for GPIO failure */

	gpio_result = dal_gpio_get_value(ddc->pin_clock, &clock_pin);
	ASSERT(gpio_result == GPIO_RESULT_OK);

	present = (gpio_result == GPIO_RESULT_OK) && !clock_pin;

	dal_ddc_close(ddc);

	return present;
}

/*
 * @brief
 * Detect output sink type
 */
static enum signal_type link_detect_sink(struct core_link *link)
{
	enum signal_type result = get_basic_signal_type(
		link->link_enc->id, link->link_id);

	/* Internal digital encoder will detect only dongles
	 * that require digital signal */

	/* Detection mechanism is different
	 * for different native connectors.
	 * LVDS connector supports only LVDS signal;
	 * PCIE is a bus slot, the actual connector needs to be detected first;
	 * eDP connector supports only eDP signal;
	 * HDMI should check straps for audio */

	/* PCIE detects the actual connector on add-on board */

	if (link->link_id.id == CONNECTOR_ID_PCIE) {
		/* ZAZTODO implement PCIE add-on card detection */
	}

	switch (link->link_id.id) {
	case CONNECTOR_ID_HDMI_TYPE_A: {
		/* check audio support:
		 * if native HDMI is not supported, switch to DVI */
		struct audio_support *aud_support = &link->dc->res_pool->audio_support;

		if (!aud_support->hdmi_audio_native)
			if (link->link_id.id == CONNECTOR_ID_HDMI_TYPE_A)
				result = SIGNAL_TYPE_DVI_SINGLE_LINK;
	}
	break;
	case CONNECTOR_ID_DISPLAY_PORT: {

		/* Check whether DP signal detected: if not -
		 * we assume signal is DVI; it could be corrected
		 * to HDMI after dongle detection */
		if (!is_dp_sink_present(link))
			result = SIGNAL_TYPE_DVI_SINGLE_LINK;
	}
	break;
	default:
	break;
	}

	return result;
}

static enum signal_type decide_signal_from_strap_and_dongle_type(
		enum display_dongle_type dongle_type,
		struct audio_support *audio_support)
{
	enum signal_type signal = SIGNAL_TYPE_NONE;

	switch (dongle_type) {
	case DISPLAY_DONGLE_DP_HDMI_DONGLE:
		if (audio_support->hdmi_audio_on_dongle)
			signal =  SIGNAL_TYPE_HDMI_TYPE_A;
		else
			signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		break;
	case DISPLAY_DONGLE_DP_DVI_DONGLE:
		signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		break;
	case DISPLAY_DONGLE_DP_HDMI_MISMATCHED_DONGLE:
		if (audio_support->hdmi_audio_native)
			signal =  SIGNAL_TYPE_HDMI_TYPE_A;
		else
			signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		break;
	default:
		signal = SIGNAL_TYPE_NONE;
		break;
	}

	return signal;
}

static enum signal_type dp_passive_dongle_detection(
		struct ddc_service *ddc,
		struct display_sink_capability *sink_cap,
		struct audio_support *audio_support)
{
	dal_ddc_service_i2c_query_dp_dual_mode_adaptor(
						ddc, sink_cap);
	return decide_signal_from_strap_and_dongle_type(
			sink_cap->dongle_type,
			audio_support);
}

static void link_disconnect_sink(struct core_link *link)
{
	if (link->public.local_sink) {
		dc_sink_release(link->public.local_sink);
		link->public.local_sink = NULL;
	}

	link->dpcd_sink_count = 0;
}

static enum dc_edid_status read_edid(
	struct core_link *link,
	struct core_sink *sink)
{
	uint32_t edid_retry = 3;
	enum dc_edid_status edid_status;

	/* some dongles read edid incorrectly the first time,
	 * do check sum and retry to make sure read correct edid.
	 */
	do {
		sink->public.dc_edid.length =
				dal_ddc_service_edid_query(link->ddc);

		if (0 == sink->public.dc_edid.length)
			return EDID_NO_RESPONSE;

		dal_ddc_service_get_edid_buf(link->ddc,
				sink->public.dc_edid.raw_edid);
		edid_status = dm_helpers_parse_edid_caps(
				sink->ctx,
				&sink->public.dc_edid,
				&sink->public.edid_caps);
		--edid_retry;
		if (edid_status == EDID_BAD_CHECKSUM)
			dm_logger_write(link->ctx->logger, LOG_WARNING,
					"Bad EDID checksum, retry remain: %d\n",
					edid_retry);
	} while (edid_status == EDID_BAD_CHECKSUM && edid_retry > 0);

	return edid_status;
}

static void detect_dp(
	struct core_link *link,
	struct display_sink_capability *sink_caps,
	bool *converter_disable_audio,
	struct audio_support *audio_support,
	bool boot)
{
	sink_caps->signal = link_detect_sink(link);
	sink_caps->transaction_type =
		get_ddc_transaction_type(sink_caps->signal);

	if (sink_caps->transaction_type == DDC_TRANSACTION_TYPE_I2C_OVER_AUX) {
		sink_caps->signal = SIGNAL_TYPE_DISPLAY_PORT;
		detect_dp_sink_caps(link);

		/* DP active dongles */
		if (is_dp_active_dongle(link)) {
			link->public.type = dc_connection_active_dongle;
			if (!link->dpcd_caps.sink_count.bits.SINK_COUNT) {
				/*
				 * active dongle unplug processing for short irq
				 */
				link_disconnect_sink(link);
				return;
			}

			if (link->dpcd_caps.dongle_type !=
			DISPLAY_DONGLE_DP_HDMI_CONVERTER) {
				*converter_disable_audio = true;
			}
		}
		if (is_mst_supported(link)) {
			sink_caps->signal = SIGNAL_TYPE_DISPLAY_PORT_MST;

			/*
			 * This call will initiate MST topology discovery. Which
			 * will detect MST ports and add new DRM connector DRM
			 * framework. Then read EDID via remote i2c over aux. In
			 * the end, will notify DRM detect result and save EDID
			 * into DRM framework.
			 *
			 * .detect is called by .fill_modes.
			 * .fill_modes is called by user mode ioctl
			 * DRM_IOCTL_MODE_GETCONNECTOR.
			 *
			 * .get_modes is called by .fill_modes.
			 *
			 * call .get_modes, AMDGPU DM implementation will create
			 * new dc_sink and add to dc_link. For long HPD plug
			 * in/out, MST has its own handle.
			 *
			 * Therefore, just after dc_create, link->sink is not
			 * created for MST until user mode app calls
			 * DRM_IOCTL_MODE_GETCONNECTOR.
			 *
			 * Need check ->sink usages in case ->sink = NULL
			 * TODO: s3 resume check
			 */

			if (dm_helpers_dp_mst_start_top_mgr(
				link->ctx,
				&link->public, boot)) {
				link->public.type = dc_connection_mst_branch;
			} else {
				/* MST not supported */
				sink_caps->signal = SIGNAL_TYPE_DISPLAY_PORT;
			}
		}
	} else {
		/* DP passive dongles */
		sink_caps->signal = dp_passive_dongle_detection(link->ddc,
				sink_caps,
				audio_support);
	}
}

bool dc_link_detect(const struct dc_link *dc_link, bool boot)
{
	struct core_link *link = DC_LINK_TO_LINK(dc_link);
	struct dc_sink_init_data sink_init_data = { 0 };
	struct display_sink_capability sink_caps = { 0 };
	uint8_t i;
	bool converter_disable_audio = false;
	struct audio_support *aud_support = &link->dc->res_pool->audio_support;
	enum dc_edid_status edid_status;
	struct dc_context *dc_ctx = link->ctx;
	struct dc_sink *dc_sink;
	struct core_sink *sink = NULL;
	enum dc_connection_type new_connection_type = dc_connection_none;

	if (link->public.connector_signal == SIGNAL_TYPE_VIRTUAL)
		return false;

	if (false == detect_sink(link, &new_connection_type)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (link->public.connector_signal == SIGNAL_TYPE_EDP &&
			link->public.local_sink)
		return true;

	link_disconnect_sink(link);

	if (new_connection_type != dc_connection_none) {
		link->public.type = new_connection_type;

		/* From Disconnected-to-Connected. */
		switch (link->public.connector_signal) {
		case SIGNAL_TYPE_HDMI_TYPE_A: {
			sink_caps.transaction_type = DDC_TRANSACTION_TYPE_I2C;
			if (aud_support->hdmi_audio_native)
				sink_caps.signal = SIGNAL_TYPE_HDMI_TYPE_A;
			else
				sink_caps.signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
			break;
		}

		case SIGNAL_TYPE_DVI_SINGLE_LINK: {
			sink_caps.transaction_type = DDC_TRANSACTION_TYPE_I2C;
			sink_caps.signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
			break;
		}

		case SIGNAL_TYPE_DVI_DUAL_LINK: {
			sink_caps.transaction_type = DDC_TRANSACTION_TYPE_I2C;
			sink_caps.signal = SIGNAL_TYPE_DVI_DUAL_LINK;
			break;
		}

		case SIGNAL_TYPE_EDP: {
			detect_dp_sink_caps(link);
			sink_caps.transaction_type =
				DDC_TRANSACTION_TYPE_I2C_OVER_AUX;
			sink_caps.signal = SIGNAL_TYPE_EDP;
			break;
		}

		case SIGNAL_TYPE_DISPLAY_PORT: {
			detect_dp(
				link,
				&sink_caps,
				&converter_disable_audio,
				aud_support, boot);

			/* Active dongle downstream unplug */
			if (link->public.type == dc_connection_active_dongle
					&& link->dpcd_caps.sink_count.
					bits.SINK_COUNT == 0)
				return true;

			if (link->public.type == dc_connection_mst_branch) {
				LINK_INFO("link=%d, mst branch is now Connected\n",
					link->public.link_index);
				return false;
			}

			break;
		}

		default:
			DC_ERROR("Invalid connector type! signal:%d\n",
				link->public.connector_signal);
			return false;
		} /* switch() */

		if (link->dpcd_caps.sink_count.bits.SINK_COUNT)
			link->dpcd_sink_count = link->dpcd_caps.sink_count.
					bits.SINK_COUNT;
			else
				link->dpcd_sink_count = 1;

		dal_ddc_service_set_transaction_type(
						link->ddc,
						sink_caps.transaction_type);

		sink_init_data.link = &link->public;
		sink_init_data.sink_signal = sink_caps.signal;
		sink_init_data.dongle_max_pix_clk =
			sink_caps.max_hdmi_pixel_clock;
		sink_init_data.converter_disable_audio =
			converter_disable_audio;

		dc_sink = dc_sink_create(&sink_init_data);
		if (!dc_sink) {
			DC_ERROR("Failed to create sink!\n");
			return false;
		}

		sink = DC_SINK_TO_CORE(dc_sink);
		link->public.local_sink = &sink->public;

		edid_status = read_edid(link, sink);

		switch (edid_status) {
		case EDID_BAD_CHECKSUM:
			dm_logger_write(link->ctx->logger, LOG_ERROR,
				"EDID checksum invalid.\n");
			break;
		case EDID_NO_RESPONSE:
			dm_logger_write(link->ctx->logger, LOG_ERROR,
				"No EDID read.\n");
			return false;

		default:
			break;
		}

		/* HDMI-DVI Dongle */
		if (dc_sink->sink_signal == SIGNAL_TYPE_HDMI_TYPE_A &&
				!dc_sink->edid_caps.edid_hdmi)
			dc_sink->sink_signal = SIGNAL_TYPE_DVI_SINGLE_LINK;

		/* Connectivity log: detection */
		for (i = 0; i < sink->public.dc_edid.length / EDID_BLOCK_SIZE; i++) {
			CONN_DATA_DETECT(link,
					&sink->public.dc_edid.raw_edid[i * EDID_BLOCK_SIZE],
					EDID_BLOCK_SIZE,
					"%s: [Block %d] ", sink->public.edid_caps.display_name, i);
		}

		dm_logger_write(link->ctx->logger, LOG_DETECTION_EDID_PARSER,
			"%s: "
			"manufacturer_id = %X, "
			"product_id = %X, "
			"serial_number = %X, "
			"manufacture_week = %d, "
			"manufacture_year = %d, "
			"display_name = %s, "
			"speaker_flag = %d, "
			"audio_mode_count = %d\n",
			__func__,
			sink->public.edid_caps.manufacturer_id,
			sink->public.edid_caps.product_id,
			sink->public.edid_caps.serial_number,
			sink->public.edid_caps.manufacture_week,
			sink->public.edid_caps.manufacture_year,
			sink->public.edid_caps.display_name,
			sink->public.edid_caps.speaker_flags,
			sink->public.edid_caps.audio_mode_count);

		for (i = 0; i < sink->public.edid_caps.audio_mode_count; i++) {
			dm_logger_write(link->ctx->logger, LOG_DETECTION_EDID_PARSER,
				"%s: mode number = %d, "
				"format_code = %d, "
				"channel_count = %d, "
				"sample_rate = %d, "
				"sample_size = %d\n",
				__func__,
				i,
				sink->public.edid_caps.audio_modes[i].format_code,
				sink->public.edid_caps.audio_modes[i].channel_count,
				sink->public.edid_caps.audio_modes[i].sample_rate,
				sink->public.edid_caps.audio_modes[i].sample_size);
		}

	} else {
		/* From Connected-to-Disconnected. */
		if (link->public.type == dc_connection_mst_branch) {
			LINK_INFO("link=%d, mst branch is now Disconnected\n",
				link->public.link_index);
			dm_helpers_dp_mst_stop_top_mgr(link->ctx, &link->public);

			link->mst_stream_alloc_table.stream_count = 0;
			memset(link->mst_stream_alloc_table.stream_allocations, 0, sizeof(link->mst_stream_alloc_table.stream_allocations));
		}

		link->public.type = dc_connection_none;
		sink_caps.signal = SIGNAL_TYPE_NONE;
	}

	LINK_INFO("link=%d, dc_sink_in=%p is now %s\n",
		link->public.link_index, &sink->public,
		(sink_caps.signal == SIGNAL_TYPE_NONE ?
			"Disconnected":"Connected"));

	return true;
}

static enum hpd_source_id get_hpd_line(
		struct core_link *link)
{
	struct gpio *hpd;
	enum hpd_source_id hpd_id = HPD_SOURCEID_UNKNOWN;

	hpd = get_hpd_gpio(link);

	if (hpd) {
		switch (dal_irq_get_source(hpd)) {
		case DC_IRQ_SOURCE_HPD1:
			hpd_id = HPD_SOURCEID1;
		break;
		case DC_IRQ_SOURCE_HPD2:
			hpd_id = HPD_SOURCEID2;
		break;
		case DC_IRQ_SOURCE_HPD3:
			hpd_id = HPD_SOURCEID3;
		break;
		case DC_IRQ_SOURCE_HPD4:
			hpd_id = HPD_SOURCEID4;
		break;
		case DC_IRQ_SOURCE_HPD5:
			hpd_id = HPD_SOURCEID5;
		break;
		case DC_IRQ_SOURCE_HPD6:
			hpd_id = HPD_SOURCEID6;
		break;
		default:
			BREAK_TO_DEBUGGER();
		break;
		}

		dal_gpio_destroy_irq(&hpd);
	}

	return hpd_id;
}

static enum channel_id get_ddc_line(struct core_link *link)
{
	struct ddc *ddc;
	enum channel_id channel = CHANNEL_ID_UNKNOWN;

	ddc = dal_ddc_service_get_ddc_pin(link->ddc);

	if (ddc) {
		switch (dal_ddc_get_line(ddc)) {
		case GPIO_DDC_LINE_DDC1:
			channel = CHANNEL_ID_DDC1;
			break;
		case GPIO_DDC_LINE_DDC2:
			channel = CHANNEL_ID_DDC2;
			break;
		case GPIO_DDC_LINE_DDC3:
			channel = CHANNEL_ID_DDC3;
			break;
		case GPIO_DDC_LINE_DDC4:
			channel = CHANNEL_ID_DDC4;
			break;
		case GPIO_DDC_LINE_DDC5:
			channel = CHANNEL_ID_DDC5;
			break;
		case GPIO_DDC_LINE_DDC6:
			channel = CHANNEL_ID_DDC6;
			break;
		case GPIO_DDC_LINE_DDC_VGA:
			channel = CHANNEL_ID_DDC_VGA;
			break;
		case GPIO_DDC_LINE_I2C_PAD:
			channel = CHANNEL_ID_I2C_PAD;
			break;
		default:
			BREAK_TO_DEBUGGER();
			break;
		}
	}

	return channel;
}

static enum transmitter translate_encoder_to_transmitter(
	struct graphics_object_id encoder)
{
	switch (encoder.id) {
	case ENCODER_ID_INTERNAL_UNIPHY:
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return TRANSMITTER_UNIPHY_A;
		case ENUM_ID_2:
			return TRANSMITTER_UNIPHY_B;
		default:
			return TRANSMITTER_UNKNOWN;
		}
	break;
	case ENCODER_ID_INTERNAL_UNIPHY1:
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return TRANSMITTER_UNIPHY_C;
		case ENUM_ID_2:
			return TRANSMITTER_UNIPHY_D;
		default:
			return TRANSMITTER_UNKNOWN;
		}
	break;
	case ENCODER_ID_INTERNAL_UNIPHY2:
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return TRANSMITTER_UNIPHY_E;
		case ENUM_ID_2:
			return TRANSMITTER_UNIPHY_F;
		default:
			return TRANSMITTER_UNKNOWN;
		}
	break;
	case ENCODER_ID_INTERNAL_UNIPHY3:
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return TRANSMITTER_UNIPHY_G;
		default:
			return TRANSMITTER_UNKNOWN;
		}
	break;
	case ENCODER_ID_EXTERNAL_NUTMEG:
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return TRANSMITTER_NUTMEG_CRT;
		default:
			return TRANSMITTER_UNKNOWN;
		}
	break;
	case ENCODER_ID_EXTERNAL_TRAVIS:
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return TRANSMITTER_TRAVIS_CRT;
		case ENUM_ID_2:
			return TRANSMITTER_TRAVIS_LCD;
		default:
			return TRANSMITTER_UNKNOWN;
		}
	break;
	default:
		return TRANSMITTER_UNKNOWN;
	}
}

static bool construct(
	struct core_link *link,
	const struct link_init_data *init_params)
{
	uint8_t i;
	struct gpio *hpd_gpio = NULL;
	struct ddc_service_init_data ddc_service_init_data = { 0 };
	struct dc_context *dc_ctx = init_params->ctx;
	struct encoder_init_data enc_init_data = { 0 };
	struct integrated_info info = {{{ 0 }}};
	struct dc_bios *bios = init_params->dc->ctx->dc_bios;
	const struct dc_vbios_funcs *bp_funcs = bios->funcs;

	link->public.irq_source_hpd = DC_IRQ_SOURCE_INVALID;
	link->public.irq_source_hpd_rx = DC_IRQ_SOURCE_INVALID;

	link->link_status.dpcd_caps = &link->dpcd_caps;

	link->dc = init_params->dc;
	link->ctx = dc_ctx;
	link->public.link_index = init_params->link_index;

	link->link_id = bios->funcs->get_connector_id(bios, init_params->connector_index);

	if (link->link_id.type != OBJECT_TYPE_CONNECTOR) {
		dm_error("%s: Invalid Connector ObjectID from Adapter Service for connector index:%d!\n",
				__func__, init_params->connector_index);
		goto create_fail;
	}

	hpd_gpio = get_hpd_gpio(link);

	if (hpd_gpio != NULL)
		link->public.irq_source_hpd = dal_irq_get_source(hpd_gpio);

	switch (link->link_id.id) {
	case CONNECTOR_ID_HDMI_TYPE_A:
		link->public.connector_signal = SIGNAL_TYPE_HDMI_TYPE_A;

		break;
	case CONNECTOR_ID_SINGLE_LINK_DVID:
	case CONNECTOR_ID_SINGLE_LINK_DVII:
		link->public.connector_signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		break;
	case CONNECTOR_ID_DUAL_LINK_DVID:
	case CONNECTOR_ID_DUAL_LINK_DVII:
		link->public.connector_signal = SIGNAL_TYPE_DVI_DUAL_LINK;
		break;
	case CONNECTOR_ID_DISPLAY_PORT:
		link->public.connector_signal =	SIGNAL_TYPE_DISPLAY_PORT;

		if (hpd_gpio != NULL)
			link->public.irq_source_hpd_rx =
					dal_irq_get_rx_source(hpd_gpio);

		break;
	case CONNECTOR_ID_EDP:
		link->public.connector_signal = SIGNAL_TYPE_EDP;

		if (hpd_gpio != NULL) {
			link->public.irq_source_hpd = DC_IRQ_SOURCE_INVALID;
			link->public.irq_source_hpd_rx =
					dal_irq_get_rx_source(hpd_gpio);
		}
		break;
	default:
		dm_logger_write(dc_ctx->logger, LOG_WARNING,
			"Unsupported Connector type:%d!\n", link->link_id.id);
		goto create_fail;
	}

	if (hpd_gpio != NULL) {
		dal_gpio_destroy_irq(&hpd_gpio);
		hpd_gpio = NULL;
	}

	/* TODO: #DAL3 Implement id to str function.*/
	LINK_INFO("Connector[%d] description:"
			"signal %d\n",
			init_params->connector_index,
			link->public.connector_signal);

	ddc_service_init_data.ctx = link->ctx;
	ddc_service_init_data.id = link->link_id;
	ddc_service_init_data.link = link;
	link->ddc = dal_ddc_service_create(&ddc_service_init_data);

	if (NULL == link->ddc) {
		DC_ERROR("Failed to create ddc_service!\n");
		goto ddc_create_fail;
	}

	link->public.ddc_hw_inst =
		dal_ddc_get_line(
			dal_ddc_service_get_ddc_pin(link->ddc));

	enc_init_data.ctx = dc_ctx;
	bp_funcs->get_src_obj(dc_ctx->dc_bios, link->link_id, 0, &enc_init_data.encoder);
	enc_init_data.connector = link->link_id;
	enc_init_data.channel = get_ddc_line(link);
	enc_init_data.hpd_source = get_hpd_line(link);
	enc_init_data.transmitter =
			translate_encoder_to_transmitter(enc_init_data.encoder);
	link->link_enc = link->dc->res_pool->funcs->link_enc_create(
								&enc_init_data);

	if( link->link_enc == NULL) {
		DC_ERROR("Failed to create link encoder!\n");
		goto link_enc_create_fail;
	}

	link->public.link_enc_hw_inst = link->link_enc->transmitter;

	for (i = 0; i < 4; i++) {
		if (BP_RESULT_OK !=
				bp_funcs->get_device_tag(dc_ctx->dc_bios, link->link_id, i, &link->device_tag)) {
			DC_ERROR("Failed to find device tag!\n");
			goto device_tag_fail;
		}

		/* Look for device tag that matches connector signal,
		 * CRT for rgb, LCD for other supported signal tyes
		 */
		if (!bp_funcs->is_device_id_supported(dc_ctx->dc_bios, link->device_tag.dev_id))
			continue;
		if (link->device_tag.dev_id.device_type == DEVICE_TYPE_CRT
			&& link->public.connector_signal != SIGNAL_TYPE_RGB)
			continue;
		if (link->device_tag.dev_id.device_type == DEVICE_TYPE_LCD
			&& link->public.connector_signal == SIGNAL_TYPE_RGB)
			continue;
		break;
	}

	if (bios->integrated_info)
		info = *bios->integrated_info;

	/* Look for channel mapping corresponding to connector and device tag */
	for (i = 0; i < MAX_NUMBER_OF_EXT_DISPLAY_PATH; i++) {
		struct external_display_path *path =
			&info.ext_disp_conn_info.path[i];
		if (path->device_connector_id.enum_id == link->link_id.enum_id
			&& path->device_connector_id.id == link->link_id.id
			&& path->device_connector_id.type == link->link_id.type
			&& path->device_acpi_enum
					== link->device_tag.acpi_device) {
			link->ddi_channel_mapping = path->channel_mapping;
			break;
		}
	}

	/*
	 * TODO check if GPIO programmed correctly
	 *
	 * If GPIO isn't programmed correctly HPD might not rise or drain
	 * fast enough, leading to bounces.
	 */
	program_hpd_filter(link);

	return true;
device_tag_fail:
	link->link_enc->funcs->destroy(&link->link_enc);
link_enc_create_fail:
	dal_ddc_service_destroy(&link->ddc);
ddc_create_fail:
create_fail:

	if (hpd_gpio != NULL) {
		dal_gpio_destroy_irq(&hpd_gpio);
	}

	return false;
}

/*******************************************************************************
 * Public functions
 ******************************************************************************/
struct core_link *link_create(const struct link_init_data *init_params)
{
	struct core_link *link =
			dm_alloc(sizeof(*link));

	if (NULL == link)
		goto alloc_fail;

	if (false == construct(link, init_params))
		goto construct_fail;

	return link;

construct_fail:
	dm_free(link);

alloc_fail:
	return NULL;
}

void link_destroy(struct core_link **link)
{
	destruct(*link);
	dm_free(*link);
	*link = NULL;
}

static void dpcd_configure_panel_mode(
	struct core_link *link,
	enum dp_panel_mode panel_mode)
{
	union dpcd_edp_config edp_config_set;
	bool panel_mode_edp = false;

	memset(&edp_config_set, '\0', sizeof(union dpcd_edp_config));

	if (DP_PANEL_MODE_DEFAULT != panel_mode) {

		switch (panel_mode) {
		case DP_PANEL_MODE_EDP:
		case DP_PANEL_MODE_SPECIAL:
			panel_mode_edp = true;
			break;

		default:
			break;
		}

		/*set edp panel mode in receiver*/
		core_link_read_dpcd(
			link,
			DP_EDP_CONFIGURATION_SET,
			&edp_config_set.raw,
			sizeof(edp_config_set.raw));

		if (edp_config_set.bits.PANEL_MODE_EDP
			!= panel_mode_edp) {
			enum ddc_result result = DDC_RESULT_UNKNOWN;

			edp_config_set.bits.PANEL_MODE_EDP =
			panel_mode_edp;
			result = core_link_write_dpcd(
				link,
				DP_EDP_CONFIGURATION_SET,
				&edp_config_set.raw,
				sizeof(edp_config_set.raw));

			ASSERT(result == DDC_RESULT_SUCESSFULL);
		}
	}
	dm_logger_write(link->ctx->logger, LOG_DETECTION_DP_CAPS,
			"Link: %d eDP panel mode supported: %d "
			"eDP panel mode enabled: %d \n",
			link->public.link_index,
			link->dpcd_caps.panel_mode_edp,
			panel_mode_edp);
}

static void enable_stream_features(struct pipe_ctx *pipe_ctx)
{
	struct core_stream *stream = pipe_ctx->stream;
	struct core_link *link = stream->sink->link;
	union down_spread_ctrl downspread;

	core_link_read_dpcd(link, DP_DOWNSPREAD_CTRL,
			&downspread.raw, sizeof(downspread));

	downspread.bits.IGNORE_MSA_TIMING_PARAM =
			(stream->public.ignore_msa_timing_param) ? 1 : 0;

	core_link_write_dpcd(link, DP_DOWNSPREAD_CTRL,
			&downspread.raw, sizeof(downspread));
}

static enum dc_status enable_link_dp(struct pipe_ctx *pipe_ctx)
{
	struct core_stream *stream = pipe_ctx->stream;
	enum dc_status status;
	bool skip_video_pattern;
	struct core_link *link = stream->sink->link;
	struct dc_link_settings link_settings = {0};
	enum dp_panel_mode panel_mode;
	enum dc_link_rate max_link_rate = LINK_RATE_HIGH2;

	/* get link settings for video mode timing */
	decide_link_settings(stream, &link_settings);

	/* raise clock state for HBR3 if required. Confirmed with HW DCE/DPCS
	 * logic for HBR3 still needs Nominal (0.8V) on VDDC rail
	 */
	if (link->link_enc->features.flags.bits.IS_HBR3_CAPABLE)
		max_link_rate = LINK_RATE_HIGH3;

	if (link_settings.link_rate == max_link_rate) {
		if (pipe_ctx->dis_clk->funcs->set_min_clocks_state) {
			if (pipe_ctx->dis_clk->cur_min_clks_state < DM_PP_CLOCKS_STATE_NOMINAL)
				pipe_ctx->dis_clk->funcs->set_min_clocks_state(
					pipe_ctx->dis_clk, DM_PP_CLOCKS_STATE_NOMINAL);
		} else {
		}
	}

	dp_enable_link_phy(
		link,
		pipe_ctx->stream->signal,
		pipe_ctx->clock_source->id,
		&link_settings);

	panel_mode = dp_get_panel_mode(link);
	dpcd_configure_panel_mode(link, panel_mode);

	skip_video_pattern = true;

	if (link_settings.link_rate == LINK_RATE_LOW)
			skip_video_pattern = false;

	if (perform_link_training_with_retries(
			link,
			&link_settings,
			skip_video_pattern,
			LINK_TRAINING_ATTEMPTS)) {
		link->public.cur_link_settings = link_settings;
		status = DC_OK;
	}
	else
		status = DC_ERROR_UNEXPECTED;

	enable_stream_features(pipe_ctx);

	return status;
}

static enum dc_status enable_link_dp_mst(struct pipe_ctx *pipe_ctx)
{
	struct core_link *link = pipe_ctx->stream->sink->link;

	/* sink signal type after MST branch is MST. Multiple MST sinks
	 * share one link. Link DP PHY is enable or training only once.
	 */
	if (link->public.cur_link_settings.lane_count != LANE_COUNT_UNKNOWN)
		return DC_OK;

	return enable_link_dp(pipe_ctx);
}

static void enable_link_hdmi(struct pipe_ctx *pipe_ctx)
{
	struct core_stream *stream = pipe_ctx->stream;
	struct core_link *link = stream->sink->link;

	if (dc_is_hdmi_signal(pipe_ctx->stream->signal))
		dal_ddc_service_write_scdc_data(
			stream->sink->link->ddc,
			stream->phy_pix_clk,
			stream->public.timing.flags.LTE_340MCSC_SCRAMBLE);

	memset(&stream->sink->link->public.cur_link_settings, 0,
			sizeof(struct dc_link_settings));

	link->link_enc->funcs->enable_tmds_output(
			link->link_enc,
			pipe_ctx->clock_source->id,
			stream->public.timing.display_color_depth,
			pipe_ctx->stream->signal == SIGNAL_TYPE_HDMI_TYPE_A,
			pipe_ctx->stream->signal == SIGNAL_TYPE_DVI_DUAL_LINK,
			stream->phy_pix_clk);

	if (pipe_ctx->stream->signal == SIGNAL_TYPE_HDMI_TYPE_A)
		dal_ddc_service_read_scdc_data(link->ddc);
}

/****************************enable_link***********************************/
static enum dc_status enable_link(struct pipe_ctx *pipe_ctx)
{
	enum dc_status status = DC_ERROR_UNEXPECTED;
	switch (pipe_ctx->stream->signal) {
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_EDP:
		status = enable_link_dp(pipe_ctx);
		break;
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		status = enable_link_dp_mst(pipe_ctx);
		msleep(200);
		break;
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
		enable_link_hdmi(pipe_ctx);
		status = DC_OK;
		break;
	case SIGNAL_TYPE_VIRTUAL:
		status = DC_OK;
		break;
	default:
		break;
	}

	if (pipe_ctx->audio && status == DC_OK) {
		/* notify audio driver for audio modes of monitor */
		pipe_ctx->audio->funcs->az_enable(pipe_ctx->audio);

		/* un-mute audio */
		/* TODO: audio should be per stream rather than per link */
		pipe_ctx->stream_enc->funcs->audio_mute_control(
			pipe_ctx->stream_enc, false);
	}

	return status;
}

static void disable_link(struct core_link *link, enum signal_type signal)
{
	/*
	 * TODO: implement call for dp_set_hw_test_pattern
	 * it is needed for compliance testing
	 */

	/* here we need to specify that encoder output settings
	 * need to be calculated as for the set mode,
	 * it will lead to querying dynamic link capabilities
	 * which should be done before enable output */

	if (dc_is_dp_signal(signal)) {
		/* SST DP, eDP */
		if (dc_is_dp_sst_signal(signal))
			dp_disable_link_phy(link, signal);
		else
			dp_disable_link_phy_mst(link, signal);
	} else
		link->link_enc->funcs->disable_output(link->link_enc, signal);
}

enum dc_status dc_link_validate_mode_timing(
		const struct core_stream *stream,
		struct core_link *link,
		const struct dc_crtc_timing *timing)
{
	uint32_t max_pix_clk = stream->sink->dongle_max_pix_clk;

	/* A hack to avoid failing any modes for EDID override feature on
	 * topology change such as lower quality cable for DP or different dongle
	 */
	if (link->public.remote_sinks[0])
		return DC_OK;

	if (0 != max_pix_clk && timing->pix_clk_khz > max_pix_clk)
		return DC_EXCEED_DONGLE_MAX_CLK;

	switch (stream->signal) {
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_DISPLAY_PORT:
		if (!dp_validate_mode_timing(
				link,
				timing))
			return DC_NO_DP_LINK_BANDWIDTH;
		break;

	default:
		break;
	}

	return DC_OK;
}


bool dc_link_set_backlight_level(const struct dc_link *dc_link, uint32_t level,
		uint32_t frame_ramp, const struct dc_stream *stream)
{
	struct core_link *link = DC_LINK_TO_CORE(dc_link);
	struct dc_context *ctx = link->ctx;
	struct core_dc *core_dc = DC_TO_CORE(ctx->dc);
	struct core_stream *core_stream = DC_STREAM_TO_CORE(stream);
	unsigned int controller_id = 0;
	int i;
	uint32_t dmcu_status;

	dm_logger_write(ctx->logger, LOG_BACKLIGHT,
			"New Backlight level: %d (0x%X)\n", level, level);

	dmcu_status = dm_read_reg(ctx, mmDMCU_STATUS);

	/* If DMCU is in reset state, DMCU is uninitialized */
	if (get_reg_field_value(dmcu_status, mmDMCU_STATUS, UC_IN_RESET)) {
		link->link_enc->funcs->set_lcd_backlight_level(link->link_enc,
						level);
	} else {
		for (i = 0; i < MAX_PIPES; i++) {
			if (core_dc->current_context->res_ctx.pipe_ctx[i].stream
					== core_stream)
				/* dmcu -1 for all controller id values,
				 * therefore +1 here
				 */
				controller_id = core_dc->current_context->res_ctx.
						pipe_ctx[i].tg->inst + 1;
		}

		link->link_enc->funcs->set_dmcu_backlight_level
				(link->link_enc, level,
				frame_ramp, controller_id);
	}
	return true;
}


bool dc_link_init_dmcu_backlight_settings(const struct dc_link *dc_link)
{
	struct core_link *link = DC_LINK_TO_CORE(dc_link);

	if (link->link_enc->funcs->init_dmcu_backlight_settings != NULL)
		link->link_enc->funcs->
			init_dmcu_backlight_settings(link->link_enc);

	return true;
}

bool dc_link_set_abm_level(const struct dc_link *dc_link, uint32_t level)
{
	struct core_link *link = DC_LINK_TO_CORE(dc_link);
	struct dc_context *ctx = link->ctx;

	dm_logger_write(ctx->logger, LOG_BACKLIGHT,
			"New abm level: %d (0x%X)\n", level, level);

	link->link_enc->funcs->set_dmcu_abm_level(link->link_enc, level);
	return true;
}

bool dc_link_set_psr_enable(const struct dc_link *dc_link, bool enable)
{
	struct core_link *link = DC_LINK_TO_CORE(dc_link);

	if (dc_link != NULL && dc_link->psr_caps.psr_version > 0)
		link->link_enc->funcs->set_dmcu_psr_enable(link->link_enc,
								enable);
	return true;
}

bool dc_link_setup_psr(const struct dc_link *dc_link,
		const struct dc_stream *stream)
{

	struct core_link *link = DC_LINK_TO_CORE(dc_link);
	struct dc_context *ctx = link->ctx;
	struct core_dc *core_dc = DC_TO_CORE(ctx->dc);
	struct core_stream *core_stream = DC_STREAM_TO_CORE(stream);
	struct psr_dmcu_context psr_context = {0};
	int i;

	psr_context.controllerId = CONTROLLER_ID_UNDEFINED;


	if (dc_link != NULL && dc_link->psr_caps.psr_version > 0) {
		/* updateSinkPsrDpcdConfig*/
		union dpcd_psr_configuration psr_configuration;

		memset(&psr_configuration, 0, sizeof(psr_configuration));

		psr_configuration.bits.ENABLE                    = 1;
		psr_configuration.bits.CRC_VERIFICATION          = 1;
		psr_configuration.bits.FRAME_CAPTURE_INDICATION  =
			dc_link->psr_caps.psr_frame_capture_indication_req;

		/* Check for PSR v2*/
		if (dc_link->psr_caps.psr_version == 0x2) {
			/* For PSR v2 selective update.
			 * Indicates whether sink should start capturing
			 * immediately following active scan line,
			 * or starting with the 2nd active scan line.
			 */
			psr_configuration.bits.LINE_CAPTURE_INDICATION = 0;
			/*For PSR v2, determines whether Sink should generate
			 * IRQ_HPD when CRC mismatch is detected.
			 */
			psr_configuration.bits.IRQ_HPD_WITH_CRC_ERROR    = 1;
		}
		dal_ddc_service_write_dpcd_data(
					link->ddc,
					368,
					&psr_configuration.raw,
					sizeof(psr_configuration.raw));

		psr_context.channel = link->ddc->ddc_pin->hw_info.ddc_channel;
		if (psr_context.channel == 0)
			psr_context.channel = 1;
		psr_context.transmitterId = link->link_enc->transmitter;
		psr_context.engineId = link->link_enc->preferred_engine;

		for (i = 0; i < MAX_PIPES; i++) {
			if (core_dc->current_context->res_ctx.pipe_ctx[i].stream
					== core_stream) {
				/* dmcu -1 for all controller id values,
				 * therefore +1 here
				 */
				psr_context.controllerId =
					core_dc->current_context->res_ctx.
					pipe_ctx[i].tg->inst + 1;
				break;
			}
		}

		/* Hardcoded for now.  Can be Pcie or Uniphy (or Unknown)*/
		psr_context.phyType = PHY_TYPE_UNIPHY;
		/*PhyId is associated with the transmitter id*/
		psr_context.smuPhyId = link->link_enc->transmitter;

		psr_context.crtcTimingVerticalTotal = stream->timing.v_total;
		psr_context.vsyncRateHz = div64_u64(div64_u64((stream->
						timing.pix_clk_khz * 1000),
						stream->timing.v_total),
						stream->timing.h_total);

		psr_context.psrSupportedDisplayConfig =
			(dc_link->psr_caps.psr_version > 0) ? true : false;
		psr_context.psrExitLinkTrainingRequired =
			dc_link->psr_caps.psr_exit_link_training_required;
		psr_context.sdpTransmitLineNumDeadline =
			dc_link->psr_caps.psr_sdp_transmit_line_num_deadline;
		psr_context.psrFrameCaptureIndicationReq =
			dc_link->psr_caps.psr_frame_capture_indication_req;

		psr_context.skipPsrWaitForPllLock = 0; /* only = 1 in KV */

		psr_context.numberOfControllers =
				link->dc->res_pool->res_cap->num_timing_generator;

		psr_context.rfb_update_auto_en = true;

		/* 2 frames before enter PSR. */
		psr_context.timehyst_frames = 2;
		/* half a frame
		 * (units in 100 lines, i.e. a value of 1 represents 100 lines)
		 */
		psr_context.hyst_lines = stream->timing.v_total / 2 / 100;
		psr_context.aux_repeats = 10;

		psr_context.psr_level.u32all = 0;

		/* SMU will perform additional powerdown sequence.
		 * For unsupported ASICs, set psr_level flag to skip PSR
		 *  static screen notification to SMU.
		 *  (Always set for DAL2, did not check ASIC)
		 */
		psr_context.psr_level.bits.SKIP_SMU_NOTIFICATION = 1;

		/* Controls additional delay after remote frame capture before
		 * continuing power down, default = 0
		 */
		psr_context.frame_delay = 0;

		link->link_enc->funcs->setup_dmcu_psr
			(link->link_enc, &psr_context);
		return true;
	} else
		return false;

}

const struct dc_link_status *dc_link_get_status(const struct dc_link *dc_link)
{
	struct core_link *link = DC_LINK_TO_CORE(dc_link);

	return &link->link_status;
}

void core_link_resume(struct core_link *link)
{
	if (link->public.connector_signal != SIGNAL_TYPE_VIRTUAL)
		program_hpd_filter(link);
}

static struct fixed31_32 get_pbn_per_slot(struct core_stream *stream)
{
	struct dc_link_settings *link_settings =
			&stream->sink->link->public.cur_link_settings;
	uint32_t link_rate_in_mbps =
			link_settings->link_rate * LINK_RATE_REF_FREQ_IN_MHZ;
	struct fixed31_32 mbps = dal_fixed31_32_from_int(
			link_rate_in_mbps * link_settings->lane_count);

	return dal_fixed31_32_div_int(mbps, 54);
}

static int get_color_depth(enum dc_color_depth color_depth)
{
	switch (color_depth) {
	case COLOR_DEPTH_666: return 6;
	case COLOR_DEPTH_888: return 8;
	case COLOR_DEPTH_101010: return 10;
	case COLOR_DEPTH_121212: return 12;
	case COLOR_DEPTH_141414: return 14;
	case COLOR_DEPTH_161616: return 16;
	default: return 0;
	}
}

static struct fixed31_32 get_pbn_from_timing(struct pipe_ctx *pipe_ctx)
{
	uint32_t bpc;
	uint64_t kbps;
	struct fixed31_32 peak_kbps;
	uint32_t numerator;
	uint32_t denominator;

	bpc = get_color_depth(pipe_ctx->pix_clk_params.color_depth);
	kbps = pipe_ctx->pix_clk_params.requested_pix_clk * bpc * 3;

	/*
	 * margin 5300ppm + 300ppm ~ 0.6% as per spec, factor is 1.006
	 * The unit of 54/64Mbytes/sec is an arbitrary unit chosen based on
	 * common multiplier to render an integer PBN for all link rate/lane
	 * counts combinations
	 * calculate
	 * peak_kbps *= (1006/1000)
	 * peak_kbps *= (64/54)
	 * peak_kbps *= 8    convert to bytes
	 */

	numerator = 64 * PEAK_FACTOR_X1000;
	denominator = 54 * 8 * 1000 * 1000;
	kbps *= numerator;
	peak_kbps = dal_fixed31_32_from_fraction(kbps, denominator);

	return peak_kbps;
}

static void update_mst_stream_alloc_table(
	struct core_link *link,
	struct stream_encoder *stream_enc,
	const struct dp_mst_stream_allocation_table *proposed_table)
{
	struct link_mst_stream_allocation work_table[MAX_CONTROLLER_NUM] = {
			{ 0 } };
	struct link_mst_stream_allocation *dc_alloc;

	int i;
	int j;

	/* if DRM proposed_table has more than one new payload */
	ASSERT(proposed_table->stream_count -
			link->mst_stream_alloc_table.stream_count < 2);

	/* copy proposed_table to core_link, add stream encoder */
	for (i = 0; i < proposed_table->stream_count; i++) {

		for (j = 0; j < link->mst_stream_alloc_table.stream_count; j++) {
			dc_alloc =
			&link->mst_stream_alloc_table.stream_allocations[j];

			if (dc_alloc->vcp_id ==
				proposed_table->stream_allocations[i].vcp_id) {

				work_table[i] = *dc_alloc;
				break; /* exit j loop */
			}
		}

		/* new vcp_id */
		if (j == link->mst_stream_alloc_table.stream_count) {
			work_table[i].vcp_id =
				proposed_table->stream_allocations[i].vcp_id;
			work_table[i].slot_count =
				proposed_table->stream_allocations[i].slot_count;
			work_table[i].stream_enc = stream_enc;
		}
	}

	/* update link->mst_stream_alloc_table with work_table */
	link->mst_stream_alloc_table.stream_count =
			proposed_table->stream_count;
	for (i = 0; i < MAX_CONTROLLER_NUM; i++)
		link->mst_stream_alloc_table.stream_allocations[i] =
				work_table[i];
}

/* convert link_mst_stream_alloc_table to dm dp_mst_stream_alloc_table
 * because stream_encoder is not exposed to dm
 */
static enum dc_status allocate_mst_payload(struct pipe_ctx *pipe_ctx)
{
	struct core_stream *stream = pipe_ctx->stream;
	struct core_link *link = stream->sink->link;
	struct link_encoder *link_encoder = link->link_enc;
	struct stream_encoder *stream_encoder = pipe_ctx->stream_enc;
	struct dp_mst_stream_allocation_table proposed_table = {0};
	struct fixed31_32 avg_time_slots_per_mtp;
	struct fixed31_32 pbn;
	struct fixed31_32 pbn_per_slot;
	uint8_t i;

	/* enable_link_dp_mst already check link->enabled_stream_count
	 * and stream is in link->stream[]. This is called during set mode,
	 * stream_enc is available.
	 */

	/* get calculate VC payload for stream: stream_alloc */
	if (dm_helpers_dp_mst_write_payload_allocation_table(
		stream->ctx,
		&stream->public,
		&proposed_table,
		true)) {
		update_mst_stream_alloc_table(
					link, pipe_ctx->stream_enc, &proposed_table);
	}
	else
		dm_logger_write(link->ctx->logger, LOG_WARNING,
				"Failed to update"
				"MST allocation table for"
				"pipe idx:%d\n",
				pipe_ctx->pipe_idx);

	dm_logger_write(link->ctx->logger, LOG_MST,
			"%s  "
			"stream_count: %d: \n ",
			__func__,
			link->mst_stream_alloc_table.stream_count);

	for (i = 0; i < MAX_CONTROLLER_NUM; i++) {
		dm_logger_write(link->ctx->logger, LOG_MST,
		"stream_enc[%d]: 0x%x      "
		"stream[%d].vcp_id: %d      "
		"stream[%d].slot_count: %d\n",
		i,
		link->mst_stream_alloc_table.stream_allocations[i].stream_enc,
		i,
		link->mst_stream_alloc_table.stream_allocations[i].vcp_id,
		i,
		link->mst_stream_alloc_table.stream_allocations[i].slot_count);
	}

	ASSERT(proposed_table.stream_count > 0);

	/* program DP source TX for payload */
	link_encoder->funcs->update_mst_stream_allocation_table(
		link_encoder,
		&link->mst_stream_alloc_table);

	/* send down message */
	dm_helpers_dp_mst_poll_for_allocation_change_trigger(
			stream->ctx,
			&stream->public);

	dm_helpers_dp_mst_send_payload_allocation(
			stream->ctx,
			&stream->public,
			true);

	/* slot X.Y for only current stream */
	pbn_per_slot = get_pbn_per_slot(stream);
	pbn = get_pbn_from_timing(pipe_ctx);
	avg_time_slots_per_mtp = dal_fixed31_32_div(pbn, pbn_per_slot);

	stream_encoder->funcs->set_mst_bandwidth(
		stream_encoder,
		avg_time_slots_per_mtp);

	return DC_OK;

}

static enum dc_status deallocate_mst_payload(struct pipe_ctx *pipe_ctx)
{
	struct core_stream *stream = pipe_ctx->stream;
	struct core_link *link = stream->sink->link;
	struct link_encoder *link_encoder = link->link_enc;
	struct stream_encoder *stream_encoder = pipe_ctx->stream_enc;
	struct dp_mst_stream_allocation_table proposed_table = {0};
	struct fixed31_32 avg_time_slots_per_mtp = dal_fixed31_32_from_int(0);
	uint8_t i;
	bool mst_mode = (link->public.type == dc_connection_mst_branch);

	/* deallocate_mst_payload is called before disable link. When mode or
	 * disable/enable monitor, new stream is created which is not in link
	 * stream[] yet. For this, payload is not allocated yet, so de-alloc
	 * should not done. For new mode set, map_resources will get engine
	 * for new stream, so stream_enc->id should be validated until here.
	 */

	/* slot X.Y */
	stream_encoder->funcs->set_mst_bandwidth(
		stream_encoder,
		avg_time_slots_per_mtp);

	/* TODO: which component is responsible for remove payload table? */
	if (mst_mode) {
		if (dm_helpers_dp_mst_write_payload_allocation_table(
				stream->ctx,
				&stream->public,
				&proposed_table,
				false)) {

			update_mst_stream_alloc_table(
				link, pipe_ctx->stream_enc, &proposed_table);
		}
		else {
				dm_logger_write(link->ctx->logger, LOG_WARNING,
						"Failed to update"
						"MST allocation table for"
						"pipe idx:%d\n",
						pipe_ctx->pipe_idx);
		}
	}

	dm_logger_write(link->ctx->logger, LOG_MST,
			"%s"
			"stream_count: %d: ",
			__func__,
			link->mst_stream_alloc_table.stream_count);

	for (i = 0; i < MAX_CONTROLLER_NUM; i++) {
		dm_logger_write(link->ctx->logger, LOG_MST,
		"stream_enc[%d]: 0x%x      "
		"stream[%d].vcp_id: %d      "
		"stream[%d].slot_count: %d\n",
		i,
		link->mst_stream_alloc_table.stream_allocations[i].stream_enc,
		i,
		link->mst_stream_alloc_table.stream_allocations[i].vcp_id,
		i,
		link->mst_stream_alloc_table.stream_allocations[i].slot_count);
	}

	link_encoder->funcs->update_mst_stream_allocation_table(
		link_encoder,
		&link->mst_stream_alloc_table);

	if (mst_mode) {
		dm_helpers_dp_mst_poll_for_allocation_change_trigger(
			stream->ctx,
			&stream->public);

		dm_helpers_dp_mst_send_payload_allocation(
			stream->ctx,
			&stream->public,
			false);
	}

	return DC_OK;
}

void core_link_enable_stream(struct pipe_ctx *pipe_ctx)
{
	struct core_dc *core_dc = DC_TO_CORE(pipe_ctx->stream->ctx->dc);

	if (DC_OK != enable_link(pipe_ctx)) {
			BREAK_TO_DEBUGGER();
			return;
	}

	core_dc->hwss.enable_stream(pipe_ctx);

	if (pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST)
		allocate_mst_payload(pipe_ctx);
}

void core_link_disable_stream(struct pipe_ctx *pipe_ctx)
{
	struct core_dc *core_dc = DC_TO_CORE(pipe_ctx->stream->ctx->dc);

	if (pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST)
		deallocate_mst_payload(pipe_ctx);

	core_dc->hwss.disable_stream(pipe_ctx);

	disable_link(pipe_ctx->stream->sink->link, pipe_ctx->stream->signal);
}

