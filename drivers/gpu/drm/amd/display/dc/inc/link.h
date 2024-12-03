/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#ifndef __DC_LINK_H__
#define __DC_LINK_H__

/* FILE POLICY AND INTENDED USAGE:
 *
 * This header defines link component function interfaces aka link_service.
 * link_service provides the only entry point to link functions with function
 * pointer style. This header is strictly private in dc and should never be
 * included by DM because it exposes too much dc detail including all dc
 * private types defined in core_types.h. Otherwise it will break DM - DC
 * encapsulation and turn DM into a maintenance nightmare.
 *
 * The following shows a link component relation map.
 *
 * DM to DC:
 * DM includes dc.h
 * dc_link_exports.c or other dc files implement dc.h
 *
 * DC to Link:
 * dc_link_exports.c or other dc files include link.h
 * link_factory.c implements link.h
 *
 * Link sub-component to Link sub-component:
 * link_factory.c includes --> link_xxx.h
 * link_xxx.c implements link_xxx.h

 * As you can see if you ever need to add a new dc link function and call it on
 * DM/dc side, it is very difficult because you will need layers of translation.
 * The most appropriate approach to implement new requirements on DM/dc side is
 * to extend or generalize the functionality of existing link function
 * interfaces so minimal modification is needed outside link component to
 * achieve your new requirements. This approach reduces or even eliminates the
 * effort needed outside link component to support a new link feature. This also
 * reduces code discrepancy among DMs to support the same link feature. If we
 * test full code path on one version of DM, and there is no feature specific
 * modification required on other DMs, then we can have higher confidence that
 * the feature will run on other DMs and produce the same result. The following
 * are some good examples to start with:
 *
 * - detect_link --> to add new link detection or capability retrieval routines
 *
 * - validate_mode_timing --> to add new timing validation conditions
 *
 * - set_dpms_on/set_dpms_off --> to include new link enablement sequences
 *
 * If you must add new link functions, you will need to:
 * 1. declare the function pointer here under the suitable commented category.
 * 2. Implement your function in the suitable link_xxx.c file.
 * 3. Assign the function to link_service in link_factory.c
 * 4. NEVER include link_xxx.h headers outside link component.
 * 5. NEVER include link.h on DM side.
 */
#include "core_types.h"

struct link_service *link_create_link_service(void);
void link_destroy_link_service(struct link_service **link_srv);

struct link_init_data {
	const struct dc *dc;
	struct dc_context *ctx; /* TODO: remove 'dal' when DC is complete. */
	uint32_t connector_index; /* this will be mapped to the HPD pins */
	uint32_t link_index; /* this is mapped to DAL display_index
				TODO: remove it when DC is complete. */
	bool is_dpia_link;
};

struct ddc_service_init_data {
	struct graphics_object_id id;
	struct dc_context *ctx;
	struct dc_link *link;
	bool is_dpia_link;
};

struct link_service {
	/************************** Factory ***********************************/
	struct dc_link *(*create_link)(
			const struct link_init_data *init_params);
	void (*destroy_link)(struct dc_link **link);


	/************************** Detection *********************************/
	bool (*detect_link)(struct dc_link *link, enum dc_detect_reason reason);
	bool (*detect_connection_type)(struct dc_link *link,
			enum dc_connection_type *type);
	struct dc_sink *(*add_remote_sink)(
			struct dc_link *link,
			const uint8_t *edid,
			int len,
			struct dc_sink_init_data *init_data);
	void (*remove_remote_sink)(struct dc_link *link, struct dc_sink *sink);
	bool (*get_hpd_state)(struct dc_link *link);
	struct gpio *(*get_hpd_gpio)(struct dc_bios *dcb,
			struct graphics_object_id link_id,
			struct gpio_service *gpio_service);
	void (*enable_hpd)(const struct dc_link *link);
	void (*disable_hpd)(const struct dc_link *link);
	void (*enable_hpd_filter)(struct dc_link *link, bool enable);
	bool (*reset_cur_dp_mst_topology)(struct dc_link *link);
	const struct dc_link_status *(*get_status)(const struct dc_link *link);
	bool (*is_hdcp1x_supported)(struct dc_link *link,
			enum signal_type signal);
	bool (*is_hdcp2x_supported)(struct dc_link *link,
			enum signal_type signal);
	void (*clear_dprx_states)(struct dc_link *link);


	/*************************** Resource *********************************/
	void (*get_cur_res_map)(const struct dc *dc, uint32_t *map);
	void (*restore_res_map)(const struct dc *dc, uint32_t *map);
	void (*get_cur_link_res)(const struct dc_link *link,
			struct link_resource *link_res);


	/*************************** Validation *******************************/
	enum dc_status (*validate_mode_timing)(
			const struct dc_stream_state *stream,
			struct dc_link *link,
			const struct dc_crtc_timing *timing);
	uint32_t (*dp_link_bandwidth_kbps)(
		const struct dc_link *link,
		const struct dc_link_settings *link_settings);
	bool (*validate_dpia_bandwidth)(
			const struct dc_stream_state *stream,
			const unsigned int num_streams);


	/*************************** DPMS *************************************/
	void (*set_dpms_on)(struct dc_state *state, struct pipe_ctx *pipe_ctx);
	void (*set_dpms_off)(struct pipe_ctx *pipe_ctx);
	void (*resume)(struct dc_link *link);
	void (*blank_all_dp_displays)(struct dc *dc);
	void (*blank_all_edp_displays)(struct dc *dc);
	void (*blank_dp_stream)(struct dc_link *link, bool hw_init);
	enum dc_status (*increase_mst_payload)(
			struct pipe_ctx *pipe_ctx, uint32_t req_pbn);
	enum dc_status (*reduce_mst_payload)(
			struct pipe_ctx *pipe_ctx, uint32_t req_pbn);
	void (*set_dsc_on_stream)(struct pipe_ctx *pipe_ctx, bool enable);
	bool (*set_dsc_enable)(struct pipe_ctx *pipe_ctx, bool enable);
	bool (*update_dsc_config)(struct pipe_ctx *pipe_ctx);


	/*************************** DDC **************************************/
	struct ddc_service *(*create_ddc_service)(
			struct ddc_service_init_data *ddc_init_data);
	void (*destroy_ddc_service)(struct ddc_service **ddc);
	bool (*query_ddc_data)(
			struct ddc_service *ddc,
			uint32_t address,
			uint8_t *write_buf,
			uint32_t write_size,
			uint8_t *read_buf,
			uint32_t read_size);
	int (*aux_transfer_raw)(struct ddc_service *ddc,
			struct aux_payload *payload,
			enum aux_return_code_type *operation_result);
	bool (*configure_fixed_vs_pe_retimer)(
			struct ddc_service *ddc,
			const uint8_t *data,
			uint32_t len);
	bool (*aux_transfer_with_retries_no_mutex)(struct ddc_service *ddc,
			struct aux_payload *payload);
	bool (*is_in_aux_transaction_mode)(struct ddc_service *ddc);
	uint32_t (*get_aux_defer_delay)(struct ddc_service *ddc);


	/*************************** DP Capability ****************************/
	bool (*dp_is_sink_present)(struct dc_link *link);
	bool (*dp_is_fec_supported)(const struct dc_link *link);
	bool (*dp_is_128b_132b_signal)(struct pipe_ctx *pipe_ctx);
	bool (*dp_get_max_link_enc_cap)(const struct dc_link *link,
			struct dc_link_settings *max_link_enc_cap);
	const struct dc_link_settings *(*dp_get_verified_link_cap)(
			const struct dc_link *link);
	enum dp_link_encoding (*dp_get_encoding_format)(
			const struct dc_link_settings *link_settings);
	bool (*dp_should_enable_fec)(const struct dc_link *link);
	bool (*dp_decide_link_settings)(
		struct dc_stream_state *stream,
		struct dc_link_settings *link_setting);
	enum dp_link_encoding (*mst_decide_link_encoding_format)(
			const struct dc_link *link);
	bool (*edp_decide_link_settings)(struct dc_link *link,
			struct dc_link_settings *link_setting, uint32_t req_bw);
	uint32_t (*bw_kbps_from_raw_frl_link_rate_data)(uint8_t bw);
	bool (*dp_overwrite_extended_receiver_cap)(struct dc_link *link);
	enum lttpr_mode (*dp_decide_lttpr_mode)(struct dc_link *link,
			struct dc_link_settings *link_setting);


	/*************************** DP DPIA/PHY ******************************/
	int (*dpia_handle_usb4_bandwidth_allocation_for_link)(
			struct dc_link *link, int peak_bw);
	void (*dpia_handle_bw_alloc_response)(
			struct dc_link *link, uint8_t bw, uint8_t result);
	void (*dp_set_drive_settings)(
		struct dc_link *link,
		const struct link_resource *link_res,
		struct link_training_settings *lt_settings);
	void (*dpcd_write_rx_power_ctrl)(struct dc_link *link, bool on);


	/*************************** DP IRQ Handler ***************************/
	bool (*dp_parse_link_loss_status)(
		struct dc_link *link,
		union hpd_irq_data *hpd_irq_dpcd_data);
	bool (*dp_should_allow_hpd_rx_irq)(const struct dc_link *link);
	void (*dp_handle_link_loss)(struct dc_link *link);
	enum dc_status (*dp_read_hpd_rx_irq_data)(
		struct dc_link *link,
		union hpd_irq_data *irq_data);
	bool (*dp_handle_hpd_rx_irq)(struct dc_link *link,
			union hpd_irq_data *out_hpd_irq_dpcd_data,
			bool *out_link_loss,
			bool defer_handling, bool *has_left_work);


	/*************************** eDP Panel Control ************************/
	void (*edp_panel_backlight_power_on)(
			struct dc_link *link, bool wait_for_hpd);
	int (*edp_get_backlight_level)(const struct dc_link *link);
	bool (*edp_get_backlight_level_nits)(struct dc_link *link,
			uint32_t *backlight_millinits_avg,
			uint32_t *backlight_millinits_peak);
	bool (*edp_set_backlight_level)(const struct dc_link *link,
			struct set_backlight_level_params *backlight_level_params);
	bool (*edp_set_backlight_level_nits)(struct dc_link *link,
			bool isHDR,
			uint32_t backlight_millinits,
			uint32_t transition_time_in_ms);
	int (*edp_get_target_backlight_pwm)(const struct dc_link *link);
	bool (*edp_get_psr_state)(
			const struct dc_link *link, enum dc_psr_state *state);
	bool (*edp_set_psr_allow_active)(
			struct dc_link *link,
			const bool *allow_active,
			bool wait,
			bool force_static,
			const unsigned int *power_opts);
	bool (*edp_setup_psr)(struct dc_link *link,
			const struct dc_stream_state *stream,
			struct psr_config *psr_config,
			struct psr_context *psr_context);
	bool (*edp_set_sink_vtotal_in_psr_active)(
			const struct dc_link *link,
			uint16_t psr_vtotal_idle,
			uint16_t psr_vtotal_su);
	void (*edp_get_psr_residency)(
			const struct dc_link *link, uint32_t *residency, enum psr_residency_mode mode);

	bool (*edp_get_replay_state)(
			const struct dc_link *link, uint64_t *state);
	bool (*edp_set_replay_allow_active)(struct dc_link *dc_link,
			const bool *enable, bool wait, bool force_static,
			const unsigned int *power_opts);
	bool (*edp_setup_replay)(struct dc_link *link,
			const struct dc_stream_state *stream);
	bool (*edp_send_replay_cmd)(struct dc_link *link,
			enum replay_FW_Message_type msg,
			union dmub_replay_cmd_set *cmd_data);
	bool (*edp_set_coasting_vtotal)(
			struct dc_link *link, uint32_t coasting_vtotal);
	bool (*edp_replay_residency)(const struct dc_link *link,
			unsigned int *residency, const bool is_start,
			const enum pr_residency_mode mode);
	bool (*edp_set_replay_power_opt_and_coasting_vtotal)(struct dc_link *link,
			const unsigned int *power_opts, uint32_t coasting_vtotal);

	bool (*edp_wait_for_t12)(struct dc_link *link);
	bool (*edp_is_ilr_optimization_required)(struct dc_link *link,
			struct dc_crtc_timing *crtc_timing);
	bool (*edp_backlight_enable_aux)(struct dc_link *link, bool enable);
	void (*edp_add_delay_for_T9)(struct dc_link *link);
	bool (*edp_receiver_ready_T9)(struct dc_link *link);
	bool (*edp_receiver_ready_T7)(struct dc_link *link);
	bool (*edp_power_alpm_dpcd_enable)(struct dc_link *link, bool enable);
	void (*edp_set_panel_power)(struct dc_link *link, bool powerOn);


	/*************************** DP CTS ************************************/
	void (*dp_handle_automated_test)(struct dc_link *link);
	bool (*dp_set_test_pattern)(
			struct dc_link *link,
			enum dp_test_pattern test_pattern,
			enum dp_test_pattern_color_space test_pattern_color_space,
			const struct link_training_settings *p_link_settings,
			const unsigned char *p_custom_pattern,
			unsigned int cust_pattern_size);
	void (*dp_set_preferred_link_settings)(struct dc *dc,
			struct dc_link_settings *link_setting,
			struct dc_link *link);
	void (*dp_set_preferred_training_settings)(struct dc *dc,
			struct dc_link_settings *link_setting,
			struct dc_link_training_overrides *lt_overrides,
			struct dc_link *link,
			bool skip_immediate_retrain);


	/*************************** DP Trace *********************************/
	bool (*dp_trace_is_initialized)(struct dc_link *link);
	void (*dp_trace_set_is_logged_flag)(struct dc_link *link,
			bool in_detection,
			bool is_logged);
	bool (*dp_trace_is_logged)(struct dc_link *link, bool in_detection);
	unsigned long long (*dp_trace_get_lt_end_timestamp)(
			struct dc_link *link, bool in_detection);
	const struct dp_trace_lt_counts *(*dp_trace_get_lt_counts)(
			struct dc_link *link, bool in_detection);
	unsigned int (*dp_trace_get_link_loss_count)(struct dc_link *link);
	void (*dp_trace_set_edp_power_timestamp)(struct dc_link *link,
			bool power_up);
	uint64_t (*dp_trace_get_edp_poweron_timestamp)(struct dc_link *link);
	uint64_t (*dp_trace_get_edp_poweroff_timestamp)(struct dc_link *link);
	void (*dp_trace_source_sequence)(
			struct dc_link *link, uint8_t dp_test_mode);
};
#endif /* __DC_LINK_HPD_H__ */
