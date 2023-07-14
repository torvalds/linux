/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
 * This file owns the creation/destruction of link structure.
 */
#include "link_factory.h"
#include "link_detection.h"
#include "link_resource.h"
#include "link_validation.h"
#include "link_dpms.h"
#include "accessories/link_dp_cts.h"
#include "accessories/link_dp_trace.h"
#include "accessories/link_fpga.h"
#include "protocols/link_ddc.h"
#include "protocols/link_dp_capability.h"
#include "protocols/link_dp_dpia_bw.h"
#include "protocols/link_dp_dpia.h"
#include "protocols/link_dp_irq_handler.h"
#include "protocols/link_dp_phy.h"
#include "protocols/link_dp_training.h"
#include "protocols/link_edp_panel_control.h"
#include "protocols/link_hpd.h"
#include "gpio_service_interface.h"
#include "atomfirmware.h"

#define DC_LOGGER_INIT(logger)

#define LINK_INFO(...) \
	DC_LOG_HW_HOTPLUG(  \
		__VA_ARGS__)

/* link factory owns the creation/destruction of link structures. */
static void construct_link_service_factory(struct link_service *link_srv)
{

	link_srv->create_link = link_create;
	link_srv->destroy_link = link_destroy;
}

/* link_detection manages link detection states and receiver states by using
 * various link protocols. It also provides helper functions to interpret
 * certain capabilities or status based on the states it manages or retrieve
 * them directly from connected receivers.
 */
static void construct_link_service_detection(struct link_service *link_srv)
{
	link_srv->detect_link = link_detect;
	link_srv->detect_connection_type = link_detect_connection_type;
	link_srv->add_remote_sink = link_add_remote_sink;
	link_srv->remove_remote_sink = link_remove_remote_sink;
	link_srv->get_hpd_state = link_get_hpd_state;
	link_srv->get_hpd_gpio = link_get_hpd_gpio;
	link_srv->enable_hpd = link_enable_hpd;
	link_srv->disable_hpd = link_disable_hpd;
	link_srv->enable_hpd_filter = link_enable_hpd_filter;
	link_srv->reset_cur_dp_mst_topology = link_reset_cur_dp_mst_topology;
	link_srv->get_status = link_get_status;
	link_srv->is_hdcp1x_supported = link_is_hdcp14;
	link_srv->is_hdcp2x_supported = link_is_hdcp22;
	link_srv->clear_dprx_states = link_clear_dprx_states;
}

/* link resource implements accessors to link resource. */
static void construct_link_service_resource(struct link_service *link_srv)
{
	link_srv->get_cur_res_map = link_get_cur_res_map;
	link_srv->restore_res_map = link_restore_res_map;
	link_srv->get_cur_link_res = link_get_cur_link_res;
}

/* link validation owns timing validation against various link limitations. (ex.
 * link bandwidth, receiver capability or our hardware capability) It also
 * provides helper functions exposing bandwidth formulas used in validation.
 */
static void construct_link_service_validation(struct link_service *link_srv)
{
	link_srv->validate_mode_timing = link_validate_mode_timing;
	link_srv->dp_link_bandwidth_kbps = dp_link_bandwidth_kbps;
	link_srv->validate_dpia_bandwidth = link_validate_dpia_bandwidth;
}

/* link dpms owns the programming sequence of stream's dpms state associated
 * with the link and link's enable/disable sequences as result of the stream's
 * dpms state change.
 */
static void construct_link_service_dpms(struct link_service *link_srv)
{
	link_srv->set_dpms_on = link_set_dpms_on;
	link_srv->set_dpms_off = link_set_dpms_off;
	link_srv->resume = link_resume;
	link_srv->blank_all_dp_displays = link_blank_all_dp_displays;
	link_srv->blank_all_edp_displays = link_blank_all_edp_displays;
	link_srv->blank_dp_stream = link_blank_dp_stream;
	link_srv->increase_mst_payload = link_increase_mst_payload;
	link_srv->reduce_mst_payload = link_reduce_mst_payload;
	link_srv->set_dsc_on_stream = link_set_dsc_on_stream;
	link_srv->set_dsc_enable = link_set_dsc_enable;
	link_srv->update_dsc_config = link_update_dsc_config;
}

/* link ddc implements generic display communication protocols such as i2c, aux
 * and scdc. It should not contain any specific applications of these
 * protocols such as display capability query, detection, or handshaking such as
 * link training.
 */
static void construct_link_service_ddc(struct link_service *link_srv)
{
	link_srv->create_ddc_service = link_create_ddc_service;
	link_srv->destroy_ddc_service = link_destroy_ddc_service;
	link_srv->query_ddc_data = link_query_ddc_data;
	link_srv->aux_transfer_raw = link_aux_transfer_raw;
	link_srv->aux_transfer_with_retries_no_mutex =
			link_aux_transfer_with_retries_no_mutex;
	link_srv->is_in_aux_transaction_mode = link_is_in_aux_transaction_mode;
	link_srv->get_aux_defer_delay = link_get_aux_defer_delay;
}

/* link dp capability implements dp specific link capability retrieval sequence.
 * It is responsible for retrieving, parsing, overriding, deciding capability
 * obtained from dp link. Link capability consists of encoders, DPRXs, cables,
 * retimers, usb and all other possible backend capabilities.
 */
static void construct_link_service_dp_capability(struct link_service *link_srv)
{
	link_srv->dp_is_sink_present = dp_is_sink_present;
	link_srv->dp_is_fec_supported = dp_is_fec_supported;
	link_srv->dp_is_128b_132b_signal = dp_is_128b_132b_signal;
	link_srv->dp_get_max_link_enc_cap = dp_get_max_link_enc_cap;
	link_srv->dp_get_verified_link_cap = dp_get_verified_link_cap;
	link_srv->dp_get_encoding_format = link_dp_get_encoding_format;
	link_srv->dp_should_enable_fec = dp_should_enable_fec;
	link_srv->dp_decide_link_settings = link_decide_link_settings;
	link_srv->mst_decide_link_encoding_format =
			mst_decide_link_encoding_format;
	link_srv->edp_decide_link_settings = edp_decide_link_settings;
	link_srv->bw_kbps_from_raw_frl_link_rate_data =
			link_bw_kbps_from_raw_frl_link_rate_data;
	link_srv->dp_overwrite_extended_receiver_cap =
			dp_overwrite_extended_receiver_cap;
	link_srv->dp_decide_lttpr_mode = dp_decide_lttpr_mode;
}

/* link dp phy/dpia implements basic dp phy/dpia functionality such as
 * enable/disable output and set lane/drive settings. It is responsible for
 * maintaining and update software state representing current phy/dpia status
 * such as current link settings.
 */
static void construct_link_service_dp_phy_or_dpia(struct link_service *link_srv)
{
	link_srv->dpia_handle_usb4_bandwidth_allocation_for_link =
			dpia_handle_usb4_bandwidth_allocation_for_link;
	link_srv->dpia_handle_bw_alloc_response = dpia_handle_bw_alloc_response;
	link_srv->dp_set_drive_settings = dp_set_drive_settings;
	link_srv->dpcd_write_rx_power_ctrl = dpcd_write_rx_power_ctrl;
}

/* link dp irq handler implements DP HPD short pulse handling sequence according
 * to DP specifications
 */
static void construct_link_service_dp_irq_handler(struct link_service *link_srv)
{
	link_srv->dp_parse_link_loss_status = dp_parse_link_loss_status;
	link_srv->dp_should_allow_hpd_rx_irq = dp_should_allow_hpd_rx_irq;
	link_srv->dp_handle_link_loss = dp_handle_link_loss;
	link_srv->dp_read_hpd_rx_irq_data = dp_read_hpd_rx_irq_data;
	link_srv->dp_handle_hpd_rx_irq = dp_handle_hpd_rx_irq;
}

/* link edp panel control implements retrieval and configuration of eDP panel
 * features such as PSR and ABM and it also manages specs defined eDP panel
 * power sequences.
 */
static void construct_link_service_edp_panel_control(struct link_service *link_srv)
{
	link_srv->edp_panel_backlight_power_on = edp_panel_backlight_power_on;
	link_srv->edp_get_backlight_level = edp_get_backlight_level;
	link_srv->edp_get_backlight_level_nits = edp_get_backlight_level_nits;
	link_srv->edp_set_backlight_level = edp_set_backlight_level;
	link_srv->edp_set_backlight_level_nits = edp_set_backlight_level_nits;
	link_srv->edp_get_target_backlight_pwm = edp_get_target_backlight_pwm;
	link_srv->edp_get_psr_state = edp_get_psr_state;
	link_srv->edp_set_psr_allow_active = edp_set_psr_allow_active;
	link_srv->edp_setup_psr = edp_setup_psr;
	link_srv->edp_set_sink_vtotal_in_psr_active =
			edp_set_sink_vtotal_in_psr_active;
	link_srv->edp_get_psr_residency = edp_get_psr_residency;
	link_srv->edp_wait_for_t12 = edp_wait_for_t12;
	link_srv->edp_is_ilr_optimization_required =
			edp_is_ilr_optimization_required;
	link_srv->edp_backlight_enable_aux = edp_backlight_enable_aux;
	link_srv->edp_add_delay_for_T9 = edp_add_delay_for_T9;
	link_srv->edp_receiver_ready_T9 = edp_receiver_ready_T9;
	link_srv->edp_receiver_ready_T7 = edp_receiver_ready_T7;
	link_srv->edp_power_alpm_dpcd_enable = edp_power_alpm_dpcd_enable;
}

/* link dp cts implements dp compliance test automation protocols and manual
 * testing interfaces for debugging and certification purpose.
 */
static void construct_link_service_dp_cts(struct link_service *link_srv)
{
	link_srv->dp_handle_automated_test = dp_handle_automated_test;
	link_srv->dp_set_test_pattern = dp_set_test_pattern;
	link_srv->dp_set_preferred_link_settings =
			dp_set_preferred_link_settings;
	link_srv->dp_set_preferred_training_settings =
			dp_set_preferred_training_settings;
}

/* link dp trace implements tracing interfaces for tracking major dp sequences
 * including execution status and timestamps
 */
static void construct_link_service_dp_trace(struct link_service *link_srv)
{
	link_srv->dp_trace_is_initialized = dp_trace_is_initialized;
	link_srv->dp_trace_set_is_logged_flag = dp_trace_set_is_logged_flag;
	link_srv->dp_trace_is_logged = dp_trace_is_logged;
	link_srv->dp_trace_get_lt_end_timestamp = dp_trace_get_lt_end_timestamp;
	link_srv->dp_trace_get_lt_counts = dp_trace_get_lt_counts;
	link_srv->dp_trace_get_link_loss_count = dp_trace_get_link_loss_count;
	link_srv->dp_trace_set_edp_power_timestamp =
			dp_trace_set_edp_power_timestamp;
	link_srv->dp_trace_get_edp_poweron_timestamp =
			dp_trace_get_edp_poweron_timestamp;
	link_srv->dp_trace_get_edp_poweroff_timestamp =
			dp_trace_get_edp_poweroff_timestamp;
	link_srv->dp_trace_source_sequence = dp_trace_source_sequence;
}

static void construct_link_service(struct link_service *link_srv)
{
	/* All link service functions should fall under some sub categories.
	 * If a new function doesn't perfectly fall under an existing sub
	 * category, it must be that you are either adding a whole new aspect of
	 * responsibility to link service or something doesn't belong to link
	 * service. In that case please contact the arch owner to arrange a
	 * design review meeting.
	 */
	construct_link_service_factory(link_srv);
	construct_link_service_detection(link_srv);
	construct_link_service_resource(link_srv);
	construct_link_service_validation(link_srv);
	construct_link_service_dpms(link_srv);
	construct_link_service_ddc(link_srv);
	construct_link_service_dp_capability(link_srv);
	construct_link_service_dp_phy_or_dpia(link_srv);
	construct_link_service_dp_irq_handler(link_srv);
	construct_link_service_edp_panel_control(link_srv);
	construct_link_service_dp_cts(link_srv);
	construct_link_service_dp_trace(link_srv);
}

struct link_service *link_create_link_service(void)
{
	struct link_service *link_srv = kzalloc(sizeof(*link_srv), GFP_KERNEL);

	if (link_srv == NULL)
		goto fail;

	construct_link_service(link_srv);

	return link_srv;
fail:
	return NULL;
}

void link_destroy_link_service(struct link_service **link_srv)
{
	kfree(*link_srv);
	*link_srv = NULL;
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

static void link_destruct(struct dc_link *link)
{
	int i;

	if (link->hpd_gpio) {
		dal_gpio_destroy_irq(&link->hpd_gpio);
		link->hpd_gpio = NULL;
	}

	if (link->ddc)
		link_destroy_ddc_service(&link->ddc);

	if (link->panel_cntl)
		link->panel_cntl->funcs->destroy(&link->panel_cntl);

	if (link->link_enc) {
		/* Update link encoder resource tracking variables. These are used for
		 * the dynamic assignment of link encoders to streams. Virtual links
		 * are not assigned encoder resources on creation.
		 */
		if (link->link_id.id != CONNECTOR_ID_VIRTUAL) {
			link->dc->res_pool->link_encoders[link->eng_id - ENGINE_ID_DIGA] = NULL;
			link->dc->res_pool->dig_link_enc_count--;
		}
		link->link_enc->funcs->destroy(&link->link_enc);
	}

	if (link->local_sink)
		dc_sink_release(link->local_sink);

	for (i = 0; i < link->sink_count; ++i)
		dc_sink_release(link->remote_sinks[i]);
}

static enum channel_id get_ddc_line(struct dc_link *link)
{
	struct ddc *ddc;
	enum channel_id channel;

	channel = CHANNEL_ID_UNKNOWN;

	ddc = get_ddc_pin(link->ddc);

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

static bool construct_phy(struct dc_link *link,
			      const struct link_init_data *init_params)
{
	uint8_t i;
	struct ddc_service_init_data ddc_service_init_data = { 0 };
	struct dc_context *dc_ctx = init_params->ctx;
	struct encoder_init_data enc_init_data = { 0 };
	struct panel_cntl_init_data panel_cntl_init_data = { 0 };
	struct integrated_info info = { 0 };
	struct dc_bios *bios = init_params->dc->ctx->dc_bios;
	const struct dc_vbios_funcs *bp_funcs = bios->funcs;
	struct bp_disp_connector_caps_info disp_connect_caps_info = { 0 };

	DC_LOGGER_INIT(dc_ctx->logger);

	link->irq_source_hpd = DC_IRQ_SOURCE_INVALID;
	link->irq_source_hpd_rx = DC_IRQ_SOURCE_INVALID;
	link->link_status.dpcd_caps = &link->dpcd_caps;

	link->dc = init_params->dc;
	link->ctx = dc_ctx;
	link->link_index = init_params->link_index;

	memset(&link->preferred_training_settings, 0,
	       sizeof(struct dc_link_training_overrides));
	memset(&link->preferred_link_setting, 0,
	       sizeof(struct dc_link_settings));

	link->link_id =
		bios->funcs->get_connector_id(bios, init_params->connector_index);

	link->ep_type = DISPLAY_ENDPOINT_PHY;

	DC_LOG_DC("BIOS object table - link_id: %d", link->link_id.id);

	if (bios->funcs->get_disp_connector_caps_info) {
		bios->funcs->get_disp_connector_caps_info(bios, link->link_id, &disp_connect_caps_info);
		link->is_internal_display = disp_connect_caps_info.INTERNAL_DISPLAY;
		DC_LOG_DC("BIOS object table - is_internal_display: %d", link->is_internal_display);
	}

	if (link->link_id.type != OBJECT_TYPE_CONNECTOR) {
		dm_output_to_console("%s: Invalid Connector ObjectID from Adapter Service for connector index:%d! type %d expected %d\n",
				     __func__, init_params->connector_index,
				     link->link_id.type, OBJECT_TYPE_CONNECTOR);
		goto create_fail;
	}

	if (link->dc->res_pool->funcs->link_init)
		link->dc->res_pool->funcs->link_init(link);

	link->hpd_gpio = link_get_hpd_gpio(link->ctx->dc_bios, link->link_id,
				      link->ctx->gpio_service);

	if (link->hpd_gpio) {
		dal_gpio_open(link->hpd_gpio, GPIO_MODE_INTERRUPT);
		dal_gpio_unlock_pin(link->hpd_gpio);
		link->irq_source_hpd = dal_irq_get_source(link->hpd_gpio);

		DC_LOG_DC("BIOS object table - hpd_gpio id: %d", link->hpd_gpio->id);
		DC_LOG_DC("BIOS object table - hpd_gpio en: %d", link->hpd_gpio->en);
	}

	switch (link->link_id.id) {
	case CONNECTOR_ID_HDMI_TYPE_A:
		link->connector_signal = SIGNAL_TYPE_HDMI_TYPE_A;

		break;
	case CONNECTOR_ID_SINGLE_LINK_DVID:
	case CONNECTOR_ID_SINGLE_LINK_DVII:
		link->connector_signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		break;
	case CONNECTOR_ID_DUAL_LINK_DVID:
	case CONNECTOR_ID_DUAL_LINK_DVII:
		link->connector_signal = SIGNAL_TYPE_DVI_DUAL_LINK;
		break;
	case CONNECTOR_ID_DISPLAY_PORT:
	case CONNECTOR_ID_USBC:
		link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;

		if (link->hpd_gpio)
			link->irq_source_hpd_rx =
					dal_irq_get_rx_source(link->hpd_gpio);

		break;
	case CONNECTOR_ID_EDP:
		link->connector_signal = SIGNAL_TYPE_EDP;

		if (link->hpd_gpio) {
			if (!link->dc->config.allow_edp_hotplug_detection)
				link->irq_source_hpd = DC_IRQ_SOURCE_INVALID;

			switch (link->dc->config.allow_edp_hotplug_detection) {
			case HPD_EN_FOR_ALL_EDP:
				link->irq_source_hpd_rx =
						dal_irq_get_rx_source(link->hpd_gpio);
				break;
			case HPD_EN_FOR_PRIMARY_EDP_ONLY:
				if (link->link_index == 0)
					link->irq_source_hpd_rx =
						dal_irq_get_rx_source(link->hpd_gpio);
				else
					link->irq_source_hpd = DC_IRQ_SOURCE_INVALID;
				break;
			case HPD_EN_FOR_SECONDARY_EDP_ONLY:
				if (link->link_index == 1)
					link->irq_source_hpd_rx =
						dal_irq_get_rx_source(link->hpd_gpio);
				else
					link->irq_source_hpd = DC_IRQ_SOURCE_INVALID;
				break;
			default:
				link->irq_source_hpd = DC_IRQ_SOURCE_INVALID;
				break;
			}
		}

		break;
	case CONNECTOR_ID_LVDS:
		link->connector_signal = SIGNAL_TYPE_LVDS;
		break;
	default:
		DC_LOG_WARNING("Unsupported Connector type:%d!\n",
			       link->link_id.id);
		goto create_fail;
	}

	LINK_INFO("Connector[%d] description: signal: %s\n",
		  init_params->connector_index,
		  signal_type_to_string(link->connector_signal));

	ddc_service_init_data.ctx = link->ctx;
	ddc_service_init_data.id = link->link_id;
	ddc_service_init_data.link = link;
	link->ddc = link_create_ddc_service(&ddc_service_init_data);

	if (!link->ddc) {
		DC_ERROR("Failed to create ddc_service!\n");
		goto ddc_create_fail;
	}

	if (!link->ddc->ddc_pin) {
		DC_ERROR("Failed to get I2C info for connector!\n");
		goto ddc_create_fail;
	}

	link->ddc_hw_inst =
		dal_ddc_get_line(get_ddc_pin(link->ddc));


	if (link->dc->res_pool->funcs->panel_cntl_create &&
		(link->link_id.id == CONNECTOR_ID_EDP ||
			link->link_id.id == CONNECTOR_ID_LVDS)) {
		panel_cntl_init_data.ctx = dc_ctx;
		panel_cntl_init_data.inst =
			panel_cntl_init_data.ctx->dc_edp_id_count;
		link->panel_cntl =
			link->dc->res_pool->funcs->panel_cntl_create(
								&panel_cntl_init_data);
		panel_cntl_init_data.ctx->dc_edp_id_count++;

		if (link->panel_cntl == NULL) {
			DC_ERROR("Failed to create link panel_cntl!\n");
			goto panel_cntl_create_fail;
		}
	}

	enc_init_data.ctx = dc_ctx;
	bp_funcs->get_src_obj(dc_ctx->dc_bios, link->link_id, 0,
			      &enc_init_data.encoder);
	enc_init_data.connector = link->link_id;
	enc_init_data.channel = get_ddc_line(link);
	enc_init_data.hpd_source = get_hpd_line(link);

	link->hpd_src = enc_init_data.hpd_source;

	enc_init_data.transmitter =
		translate_encoder_to_transmitter(enc_init_data.encoder);
	link->link_enc =
		link->dc->res_pool->funcs->link_enc_create(dc_ctx, &enc_init_data);

	DC_LOG_DC("BIOS object table - DP_IS_USB_C: %d", link->link_enc->features.flags.bits.DP_IS_USB_C);
	DC_LOG_DC("BIOS object table - IS_DP2_CAPABLE: %d", link->link_enc->features.flags.bits.IS_DP2_CAPABLE);

	if (!link->link_enc) {
		DC_ERROR("Failed to create link encoder!\n");
		goto link_enc_create_fail;
	}

	/* Update link encoder tracking variables. These are used for the dynamic
	 * assignment of link encoders to streams.
	 */
	link->eng_id = link->link_enc->preferred_engine;
	link->dc->res_pool->link_encoders[link->eng_id - ENGINE_ID_DIGA] = link->link_enc;
	link->dc->res_pool->dig_link_enc_count++;

	link->link_enc_hw_inst = link->link_enc->transmitter;
	for (i = 0; i < 4; i++) {
		if (bp_funcs->get_device_tag(dc_ctx->dc_bios,
					     link->link_id, i,
					     &link->device_tag) != BP_RESULT_OK) {
			DC_ERROR("Failed to find device tag!\n");
			goto device_tag_fail;
		}

		/* Look for device tag that matches connector signal,
		 * CRT for rgb, LCD for other supported signal tyes
		 */
		if (!bp_funcs->is_device_id_supported(dc_ctx->dc_bios,
						      link->device_tag.dev_id))
			continue;
		if (link->device_tag.dev_id.device_type == DEVICE_TYPE_CRT &&
		    link->connector_signal != SIGNAL_TYPE_RGB)
			continue;
		if (link->device_tag.dev_id.device_type == DEVICE_TYPE_LCD &&
		    link->connector_signal == SIGNAL_TYPE_RGB)
			continue;

		DC_LOG_DC("BIOS object table - device_tag.acpi_device: %d", link->device_tag.acpi_device);
		DC_LOG_DC("BIOS object table - device_tag.dev_id.device_type: %d", link->device_tag.dev_id.device_type);
		DC_LOG_DC("BIOS object table - device_tag.dev_id.enum_id: %d", link->device_tag.dev_id.enum_id);
		break;
	}

	if (bios->integrated_info)
		info = *bios->integrated_info;

	/* Look for channel mapping corresponding to connector and device tag */
	for (i = 0; i < MAX_NUMBER_OF_EXT_DISPLAY_PATH; i++) {
		struct external_display_path *path =
			&info.ext_disp_conn_info.path[i];

		if (path->device_connector_id.enum_id == link->link_id.enum_id &&
		    path->device_connector_id.id == link->link_id.id &&
		    path->device_connector_id.type == link->link_id.type) {
			if (link->device_tag.acpi_device != 0 &&
			    path->device_acpi_enum == link->device_tag.acpi_device) {
				link->ddi_channel_mapping = path->channel_mapping;
				link->chip_caps = path->caps;
				DC_LOG_DC("BIOS object table - ddi_channel_mapping: 0x%04X", link->ddi_channel_mapping.raw);
				DC_LOG_DC("BIOS object table - chip_caps: %d", link->chip_caps);
			} else if (path->device_tag ==
				   link->device_tag.dev_id.raw_device_tag) {
				link->ddi_channel_mapping = path->channel_mapping;
				link->chip_caps = path->caps;
				DC_LOG_DC("BIOS object table - ddi_channel_mapping: 0x%04X", link->ddi_channel_mapping.raw);
				DC_LOG_DC("BIOS object table - chip_caps: %d", link->chip_caps);
			}

			if (link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) {
				link->bios_forced_drive_settings.VOLTAGE_SWING =
						(info.ext_disp_conn_info.fixdpvoltageswing & 0x3);
				link->bios_forced_drive_settings.PRE_EMPHASIS =
						((info.ext_disp_conn_info.fixdpvoltageswing >> 2) & 0x3);
			}

			break;
		}
	}

	if (bios->funcs->get_atom_dc_golden_table)
		bios->funcs->get_atom_dc_golden_table(bios);

	/*
	 * TODO check if GPIO programmed correctly
	 *
	 * If GPIO isn't programmed correctly HPD might not rise or drain
	 * fast enough, leading to bounces.
	 */
	program_hpd_filter(link);

	link->psr_settings.psr_vtotal_control_support = false;
	link->psr_settings.psr_version = DC_PSR_VERSION_UNSUPPORTED;

	DC_LOG_DC("BIOS object table - %s finished successfully.\n", __func__);
	return true;
device_tag_fail:
	link->link_enc->funcs->destroy(&link->link_enc);
link_enc_create_fail:
	if (link->panel_cntl != NULL)
		link->panel_cntl->funcs->destroy(&link->panel_cntl);
panel_cntl_create_fail:
	link_destroy_ddc_service(&link->ddc);
ddc_create_fail:
create_fail:

	if (link->hpd_gpio) {
		dal_gpio_destroy_irq(&link->hpd_gpio);
		link->hpd_gpio = NULL;
	}

	DC_LOG_DC("BIOS object table - %s failed.\n", __func__);
	return false;
}

static bool construct_dpia(struct dc_link *link,
			      const struct link_init_data *init_params)
{
	struct ddc_service_init_data ddc_service_init_data = { 0 };
	struct dc_context *dc_ctx = init_params->ctx;

	DC_LOGGER_INIT(dc_ctx->logger);

	/* Initialized irq source for hpd and hpd rx */
	link->irq_source_hpd = DC_IRQ_SOURCE_INVALID;
	link->irq_source_hpd_rx = DC_IRQ_SOURCE_INVALID;
	link->link_status.dpcd_caps = &link->dpcd_caps;

	link->dc = init_params->dc;
	link->ctx = dc_ctx;
	link->link_index = init_params->link_index;

	memset(&link->preferred_training_settings, 0,
	       sizeof(struct dc_link_training_overrides));
	memset(&link->preferred_link_setting, 0,
	       sizeof(struct dc_link_settings));

	/* Dummy Init for linkid */
	link->link_id.type = OBJECT_TYPE_CONNECTOR;
	link->link_id.id = CONNECTOR_ID_DISPLAY_PORT;
	link->link_id.enum_id = ENUM_ID_1 + init_params->connector_index;
	link->is_internal_display = false;
	link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;
	LINK_INFO("Connector[%d] description:signal %d\n",
		  init_params->connector_index,
		  link->connector_signal);

	link->ep_type = DISPLAY_ENDPOINT_USB4_DPIA;
	link->is_dig_mapping_flexible = true;

	/* TODO: Initialize link : funcs->link_init */

	ddc_service_init_data.ctx = link->ctx;
	ddc_service_init_data.id = link->link_id;
	ddc_service_init_data.link = link;
	/* Set indicator for dpia link so that ddc wont be created */
	ddc_service_init_data.is_dpia_link = true;

	link->ddc = link_create_ddc_service(&ddc_service_init_data);
	if (!link->ddc) {
		DC_ERROR("Failed to create ddc_service!\n");
		goto ddc_create_fail;
	}

	/* Set dpia port index : 0 to number of dpia ports */
	link->ddc_hw_inst = init_params->connector_index;

	/* TODO: Create link encoder */

	link->psr_settings.psr_version = DC_PSR_VERSION_UNSUPPORTED;

	/* Some docks seem to NAK I2C writes to segment pointer with mot=0. */
	link->wa_flags.dp_mot_reset_segment = true;

	return true;

ddc_create_fail:
	return false;
}

static bool link_construct(struct dc_link *link,
			      const struct link_init_data *init_params)
{
	/* Handle dpia case */
	if (init_params->is_dpia_link == true)
		return construct_dpia(link, init_params);
	else
		return construct_phy(link, init_params);
}

struct dc_link *link_create(const struct link_init_data *init_params)
{
	struct dc_link *link =
			kzalloc(sizeof(*link), GFP_KERNEL);

	if (NULL == link)
		goto alloc_fail;

	if (false == link_construct(link, init_params))
		goto construct_fail;

	return link;

construct_fail:
	kfree(link);

alloc_fail:
	return NULL;
}

void link_destroy(struct dc_link **link)
{
	link_destruct(*link);
	kfree(*link);
	*link = NULL;
}
