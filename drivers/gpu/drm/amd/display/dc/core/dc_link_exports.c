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
 * This file provides single entrance to link functionality declared in dc
 * public headers. The file is intended to be used as a thin translation layer
 * that directly calls link internal functions without adding new functional
 * behavior.
 *
 * When exporting a new link related dc function, add function declaration in
 * dc.h with detail interface documentation, then add function implementation
 * in this file which calls link functions.
 */
#include "link.h"
#include "dce/dce_i2c.h"
struct dc_link *dc_get_link_at_index(struct dc *dc, uint32_t link_index)
{
	if (link_index >= MAX_LINKS)
		return NULL;

	return dc->links[link_index];
}

void dc_get_edp_links(const struct dc *dc,
		struct dc_link **edp_links,
		int *edp_num)
{
	int i;

	*edp_num = 0;
	for (i = 0; i < dc->link_count; i++) {
		// report any eDP links, even unconnected DDI's
		if (!dc->links[i])
			continue;
		if (dc->links[i]->connector_signal == SIGNAL_TYPE_EDP) {
			edp_links[*edp_num] = dc->links[i];
			if (++(*edp_num) == MAX_NUM_EDP)
				return;
		}
	}
}

bool dc_get_edp_link_panel_inst(const struct dc *dc,
		const struct dc_link *link,
		unsigned int *inst_out)
{
	struct dc_link *edp_links[MAX_NUM_EDP];
	int edp_num, i;

	*inst_out = 0;
	if (link->connector_signal != SIGNAL_TYPE_EDP)
		return false;
	dc_get_edp_links(dc, edp_links, &edp_num);
	for (i = 0; i < edp_num; i++) {
		if (link == edp_links[i])
			break;
		(*inst_out)++;
	}
	return true;
}

bool dc_link_detect(struct dc_link *link, enum dc_detect_reason reason)
{
	return link->dc->link_srv->detect_link(link, reason);
}

bool dc_link_detect_connection_type(struct dc_link *link,
		enum dc_connection_type *type)
{
	return link->dc->link_srv->detect_connection_type(link, type);
}

const struct dc_link_status *dc_link_get_status(const struct dc_link *link)
{
	return link->dc->link_srv->get_status(link);
}

/* return true if the connected receiver supports the hdcp version */
bool dc_link_is_hdcp14(struct dc_link *link, enum signal_type signal)
{
	return link->dc->link_srv->is_hdcp1x_supported(link, signal);
}

bool dc_link_is_hdcp22(struct dc_link *link, enum signal_type signal)
{
	return link->dc->link_srv->is_hdcp2x_supported(link, signal);
}

void dc_link_clear_dprx_states(struct dc_link *link)
{
	link->dc->link_srv->clear_dprx_states(link);
}

bool dc_link_reset_cur_dp_mst_topology(struct dc_link *link)
{
	return link->dc->link_srv->reset_cur_dp_mst_topology(link);
}

uint32_t dc_link_bandwidth_kbps(
	const struct dc_link *link,
	const struct dc_link_settings *link_settings)
{
	return link->dc->link_srv->dp_link_bandwidth_kbps(link, link_settings);
}

void dc_get_cur_link_res_map(const struct dc *dc, uint32_t *map)
{
	dc->link_srv->get_cur_res_map(dc, map);
}

void dc_restore_link_res_map(const struct dc *dc, uint32_t *map)
{
	dc->link_srv->restore_res_map(dc, map);
}

bool dc_link_update_dsc_config(struct pipe_ctx *pipe_ctx)
{
	struct dc_link *link = pipe_ctx->stream->link;

	return link->dc->link_srv->update_dsc_config(pipe_ctx);
}

bool dc_is_oem_i2c_device_present(
	struct dc *dc,
	size_t slave_address)
{
	if (dc->res_pool->oem_device)
		return dce_i2c_oem_device_present(
			dc->res_pool,
			dc->res_pool->oem_device,
			slave_address);

	return false;
}

bool dc_submit_i2c(
		struct dc *dc,
		uint32_t link_index,
		struct i2c_command *cmd)
{

	struct dc_link *link = dc->links[link_index];
	struct ddc_service *ddc = link->ddc;

	return dce_i2c_submit_command(
		dc->res_pool,
		ddc->ddc_pin,
		cmd);
}

bool dc_submit_i2c_oem(
		struct dc *dc,
		struct i2c_command *cmd)
{
	struct ddc_service *ddc = dc->res_pool->oem_device;

	if (ddc)
		return dce_i2c_submit_command(
			dc->res_pool,
			ddc->ddc_pin,
			cmd);

	return false;
}

void dc_link_dp_handle_automated_test(struct dc_link *link)
{
	link->dc->link_srv->dp_handle_automated_test(link);
}

bool dc_link_dp_set_test_pattern(
	struct dc_link *link,
	enum dp_test_pattern test_pattern,
	enum dp_test_pattern_color_space test_pattern_color_space,
	const struct link_training_settings *p_link_settings,
	const unsigned char *p_custom_pattern,
	unsigned int cust_pattern_size)
{
	return link->dc->link_srv->dp_set_test_pattern(link, test_pattern,
			test_pattern_color_space, p_link_settings,
			p_custom_pattern, cust_pattern_size);
}

void dc_link_set_drive_settings(struct dc *dc,
				struct link_training_settings *lt_settings,
				struct dc_link *link)
{
	struct link_resource link_res;

	dc->link_srv->get_cur_link_res(link, &link_res);
	dc->link_srv->dp_set_drive_settings(link, &link_res, lt_settings);
}

void dc_link_set_preferred_link_settings(struct dc *dc,
					 struct dc_link_settings *link_setting,
					 struct dc_link *link)
{
	dc->link_srv->dp_set_preferred_link_settings(dc, link_setting, link);
}

void dc_link_set_preferred_training_settings(struct dc *dc,
		struct dc_link_settings *link_setting,
		struct dc_link_training_overrides *lt_overrides,
		struct dc_link *link,
		bool skip_immediate_retrain)
{
	dc->link_srv->dp_set_preferred_training_settings(dc, link_setting,
			lt_overrides, link, skip_immediate_retrain);
}

bool dc_dp_trace_is_initialized(struct dc_link *link)
{
	return link->dc->link_srv->dp_trace_is_initialized(link);
}

void dc_dp_trace_set_is_logged_flag(struct dc_link *link,
		bool in_detection,
		bool is_logged)
{
	link->dc->link_srv->dp_trace_set_is_logged_flag(link, in_detection, is_logged);
}

bool dc_dp_trace_is_logged(struct dc_link *link, bool in_detection)
{
	return link->dc->link_srv->dp_trace_is_logged(link, in_detection);
}

unsigned long long dc_dp_trace_get_lt_end_timestamp(struct dc_link *link,
		bool in_detection)
{
	return link->dc->link_srv->dp_trace_get_lt_end_timestamp(link, in_detection);
}

const struct dp_trace_lt_counts *dc_dp_trace_get_lt_counts(struct dc_link *link,
		bool in_detection)
{
	return link->dc->link_srv->dp_trace_get_lt_counts(link, in_detection);
}

unsigned int dc_dp_trace_get_link_loss_count(struct dc_link *link)
{
	return link->dc->link_srv->dp_trace_get_link_loss_count(link);
}

struct dc_sink *dc_link_add_remote_sink(
		struct dc_link *link,
		const uint8_t *edid,
		int len,
		struct dc_sink_init_data *init_data)
{
	return link->dc->link_srv->add_remote_sink(link, edid, len, init_data);
}

void dc_link_remove_remote_sink(struct dc_link *link, struct dc_sink *sink)
{
	link->dc->link_srv->remove_remote_sink(link, sink);
}

int dc_link_aux_transfer_raw(struct ddc_service *ddc,
		struct aux_payload *payload,
		enum aux_return_code_type *operation_result)
{
	const struct dc *dc = ddc->link->dc;

	return dc->link_srv->aux_transfer_raw(
			ddc, payload, operation_result);
}

uint32_t dc_link_bw_kbps_from_raw_frl_link_rate_data(const struct dc *dc, uint8_t bw)
{
	return dc->link_srv->bw_kbps_from_raw_frl_link_rate_data(bw);
}

bool dc_link_decide_edp_link_settings(struct dc_link *link,
		struct dc_link_settings *link_setting, uint32_t req_bw)
{
	return link->dc->link_srv->edp_decide_link_settings(link, link_setting, req_bw);
}


bool dc_link_dp_get_max_link_enc_cap(const struct dc_link *link,
		struct dc_link_settings *max_link_enc_cap)
{
	return link->dc->link_srv->dp_get_max_link_enc_cap(link, max_link_enc_cap);
}

enum dp_link_encoding dc_link_dp_mst_decide_link_encoding_format(
		const struct dc_link *link)
{
	return link->dc->link_srv->mst_decide_link_encoding_format(link);
}

const struct dc_link_settings *dc_link_get_link_cap(const struct dc_link *link)
{
	return link->dc->link_srv->dp_get_verified_link_cap(link);
}

enum dc_link_encoding_format dc_link_get_highest_encoding_format(const struct dc_link *link)
{
	if (dc_is_dp_signal(link->connector_signal)) {
		if (link->dpcd_caps.dongle_type >= DISPLAY_DONGLE_DP_DVI_DONGLE &&
				link->dpcd_caps.dongle_type <= DISPLAY_DONGLE_DP_HDMI_MISMATCHED_DONGLE)
			return DC_LINK_ENCODING_HDMI_TMDS;
		else if (link->dc->link_srv->dp_get_encoding_format(&link->verified_link_cap) ==
				DP_8b_10b_ENCODING)
			return DC_LINK_ENCODING_DP_8b_10b;
		else if (link->dc->link_srv->dp_get_encoding_format(&link->verified_link_cap) ==
				DP_128b_132b_ENCODING)
			return DC_LINK_ENCODING_DP_128b_132b;
	} else if (dc_is_hdmi_signal(link->connector_signal)) {
	}

	return DC_LINK_ENCODING_UNSPECIFIED;
}

bool dc_link_is_dp_sink_present(struct dc_link *link)
{
	return link->dc->link_srv->dp_is_sink_present(link);
}

bool dc_link_is_fec_supported(const struct dc_link *link)
{
	return link->dc->link_srv->dp_is_fec_supported(link);
}

void dc_link_overwrite_extended_receiver_cap(
		struct dc_link *link)
{
	link->dc->link_srv->dp_overwrite_extended_receiver_cap(link);
}

bool dc_link_should_enable_fec(const struct dc_link *link)
{
	return link->dc->link_srv->dp_should_enable_fec(link);
}

int dc_link_dp_dpia_handle_usb4_bandwidth_allocation_for_link(
		struct dc_link *link, int peak_bw)
{
	return link->dc->link_srv->dpia_handle_usb4_bandwidth_allocation_for_link(link, peak_bw);
}

void dc_link_handle_usb4_bw_alloc_response(struct dc_link *link, uint8_t bw, uint8_t result)
{
	link->dc->link_srv->dpia_handle_bw_alloc_response(link, bw, result);
}

bool dc_link_check_link_loss_status(
	struct dc_link *link,
	union hpd_irq_data *hpd_irq_dpcd_data)
{
	return link->dc->link_srv->dp_parse_link_loss_status(link, hpd_irq_dpcd_data);
}

bool dc_link_dp_allow_hpd_rx_irq(const struct dc_link *link)
{
	return link->dc->link_srv->dp_should_allow_hpd_rx_irq(link);
}

void dc_link_dp_handle_link_loss(struct dc_link *link)
{
	link->dc->link_srv->dp_handle_link_loss(link);
}

enum dc_status dc_link_dp_read_hpd_rx_irq_data(
	struct dc_link *link,
	union hpd_irq_data *irq_data)
{
	return link->dc->link_srv->dp_read_hpd_rx_irq_data(link, irq_data);
}

bool dc_link_handle_hpd_rx_irq(struct dc_link *link,
		union hpd_irq_data *out_hpd_irq_dpcd_data, bool *out_link_loss,
		bool defer_handling, bool *has_left_work)
{
	return link->dc->link_srv->dp_handle_hpd_rx_irq(link, out_hpd_irq_dpcd_data,
			out_link_loss, defer_handling, has_left_work);
}

void dc_link_dp_receiver_power_ctrl(struct dc_link *link, bool on)
{
	link->dc->link_srv->dpcd_write_rx_power_ctrl(link, on);
}

enum lttpr_mode dc_link_decide_lttpr_mode(struct dc_link *link,
		struct dc_link_settings *link_setting)
{
	return link->dc->link_srv->dp_decide_lttpr_mode(link, link_setting);
}

void dc_link_edp_panel_backlight_power_on(struct dc_link *link, bool wait_for_hpd)
{
	link->dc->link_srv->edp_panel_backlight_power_on(link, wait_for_hpd);
}

int dc_link_get_backlight_level(const struct dc_link *link)
{
	return link->dc->link_srv->edp_get_backlight_level(link);
}

bool dc_link_get_backlight_level_nits(struct dc_link *link,
		uint32_t *backlight_millinits_avg,
		uint32_t *backlight_millinits_peak)
{
	return link->dc->link_srv->edp_get_backlight_level_nits(link,
			backlight_millinits_avg,
			backlight_millinits_peak);
}

bool dc_link_set_backlight_level(const struct dc_link *link,
		uint32_t backlight_pwm_u16_16,
		uint32_t frame_ramp)
{
	return link->dc->link_srv->edp_set_backlight_level(link,
			backlight_pwm_u16_16, frame_ramp);
}

bool dc_link_set_backlight_level_nits(struct dc_link *link,
		bool isHDR,
		uint32_t backlight_millinits,
		uint32_t transition_time_in_ms)
{
	return link->dc->link_srv->edp_set_backlight_level_nits(link, isHDR,
			backlight_millinits, transition_time_in_ms);
}

int dc_link_get_target_backlight_pwm(const struct dc_link *link)
{
	return link->dc->link_srv->edp_get_target_backlight_pwm(link);
}

bool dc_link_get_psr_state(const struct dc_link *link, enum dc_psr_state *state)
{
	return link->dc->link_srv->edp_get_psr_state(link, state);
}

bool dc_link_set_psr_allow_active(struct dc_link *link, const bool *allow_active,
		bool wait, bool force_static, const unsigned int *power_opts)
{
	return link->dc->link_srv->edp_set_psr_allow_active(link, allow_active, wait,
			force_static, power_opts);
}

bool dc_link_setup_psr(struct dc_link *link,
		const struct dc_stream_state *stream, struct psr_config *psr_config,
		struct psr_context *psr_context)
{
	return link->dc->link_srv->edp_setup_psr(link, stream, psr_config, psr_context);
}

bool dc_link_set_replay_allow_active(struct dc_link *link, const bool *allow_active,
		bool wait, bool force_static, const unsigned int *power_opts)
{
	return link->dc->link_srv->edp_set_replay_allow_active(link, allow_active, wait,
			force_static, power_opts);
}

bool dc_link_get_replay_state(const struct dc_link *link, uint64_t *state)
{
	return link->dc->link_srv->edp_get_replay_state(link, state);
}

bool dc_link_wait_for_t12(struct dc_link *link)
{
	return link->dc->link_srv->edp_wait_for_t12(link);
}

bool dc_link_get_hpd_state(struct dc_link *link)
{
	return link->dc->link_srv->get_hpd_state(link);
}

void dc_link_enable_hpd(const struct dc_link *link)
{
	link->dc->link_srv->enable_hpd(link);
}

void dc_link_disable_hpd(const struct dc_link *link)
{
	link->dc->link_srv->disable_hpd(link);
}

void dc_link_enable_hpd_filter(struct dc_link *link, bool enable)
{
	link->dc->link_srv->enable_hpd_filter(link, enable);
}

bool dc_link_dp_dpia_validate(struct dc *dc, const struct dc_stream_state *streams, const unsigned int count)
{
	return dc->link_srv->validate_dpia_bandwidth(streams, count);
}
