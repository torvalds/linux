/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

#ifndef DC_LINK_H_
#define DC_LINK_H_

#include "dc.h"
#include "dc_types.h"
#include "grph_object_defs.h"

struct link_resource;

enum dc_link_fec_state {
	dc_link_fec_not_ready,
	dc_link_fec_ready,
	dc_link_fec_enabled
};

struct dc_link_status {
	bool link_active;
	struct dpcd_caps *dpcd_caps;
};

struct dprx_states {
	bool cable_id_written;
};

/* DP MST stream allocation (payload bandwidth number) */
struct link_mst_stream_allocation {
	/* DIG front */
	const struct stream_encoder *stream_enc;
	/* HPO DP Stream Encoder */
	const struct hpo_dp_stream_encoder *hpo_dp_stream_enc;
	/* associate DRM payload table with DC stream encoder */
	uint8_t vcp_id;
	/* number of slots required for the DP stream in transport packet */
	uint8_t slot_count;
};

/* DP MST stream allocation table */
struct link_mst_stream_allocation_table {
	/* number of DP video streams */
	int stream_count;
	/* array of stream allocations */
	struct link_mst_stream_allocation stream_allocations[MAX_CONTROLLER_NUM];
};

struct edp_trace_power_timestamps {
	uint64_t poweroff;
	uint64_t poweron;
};

struct dp_trace_lt_counts {
	unsigned int total;
	unsigned int fail;
};

struct dp_trace_lt {
	struct dp_trace_lt_counts counts;
	struct dp_trace_timestamps {
		unsigned long long start;
		unsigned long long end;
	} timestamps;
	enum link_training_result result;
	bool is_logged;
};

struct dp_trace {
	struct dp_trace_lt detect_lt_trace;
	struct dp_trace_lt commit_lt_trace;
	unsigned int link_loss_count;
	bool is_initialized;
	struct edp_trace_power_timestamps edp_trace_power_timestamps;
};

/* PSR feature flags */
struct psr_settings {
	bool psr_feature_enabled;		// PSR is supported by sink
	bool psr_allow_active;			// PSR is currently active
	enum dc_psr_version psr_version;		// Internal PSR version, determined based on DPCD
	bool psr_vtotal_control_support;	// Vtotal control is supported by sink

	/* These parameters are calculated in Driver,
	 * based on display timing and Sink capabilities.
	 * If VBLANK region is too small and Sink takes a long time
	 * to set up RFB, it may take an extra frame to enter PSR state.
	 */
	bool psr_frame_capture_indication_req;
	unsigned int psr_sdp_transmit_line_num_deadline;
	uint8_t force_ffu_mode;
	unsigned int psr_power_opt;
};

/* To split out "global" and "per-panel" config settings.
 * Add a struct dc_panel_config under dc_link
 */
struct dc_panel_config {
	// extra panel power sequence parameters
	struct pps {
		unsigned int extra_t3_ms;
		unsigned int extra_t7_ms;
		unsigned int extra_delay_backlight_off;
		unsigned int extra_post_t7_ms;
		unsigned int extra_pre_t11_ms;
		unsigned int extra_t12_ms;
		unsigned int extra_post_OUI_ms;
	} pps;
	// ABM
	struct varib {
		unsigned int varibright_feature_enable;
		unsigned int def_varibright_level;
		unsigned int abm_config_setting;
	} varib;
	// edp DSC
	struct dsc {
		bool disable_dsc_edp;
		unsigned int force_dsc_edp_policy;
	} dsc;
};
/*
 * A link contains one or more sinks and their connected status.
 * The currently active signal type (HDMI, DP-SST, DP-MST) is also reported.
 */
struct dc_link {
	struct dc_sink *remote_sinks[MAX_SINKS_PER_LINK];
	unsigned int sink_count;
	struct dc_sink *local_sink;
	unsigned int link_index;
	enum dc_connection_type type;
	enum signal_type connector_signal;
	enum dc_irq_source irq_source_hpd;
	enum dc_irq_source irq_source_hpd_rx;/* aka DP Short Pulse  */
	bool is_hpd_filter_disabled;
	bool dp_ss_off;
	bool link_state_valid;
	bool aux_access_disabled;
	bool sync_lt_in_progress;
	bool is_internal_display;

	/* TODO: Rename. Flag an endpoint as having a programmable mapping to a
	 * DIG encoder. */
	bool is_dig_mapping_flexible;
	bool hpd_status; /* HPD status of link without physical HPD pin. */
	bool is_hpd_pending; /* Indicates a new received hpd */

	bool edp_sink_present;

	struct dp_trace dp_trace;

	/* caps is the same as reported_link_cap. link_traing use
	 * reported_link_cap. Will clean up.  TODO
	 */
	struct dc_link_settings reported_link_cap;
	struct dc_link_settings verified_link_cap;
	struct dc_link_settings cur_link_settings;
	struct dc_lane_settings cur_lane_setting[LANE_COUNT_DP_MAX];
	struct dc_link_settings preferred_link_setting;
	/* preferred_training_settings are override values that
	 * come from DM. DM is responsible for the memory
	 * management of the override pointers.
	 */
	struct dc_link_training_overrides preferred_training_settings;
	struct dp_audio_test_data audio_test_data;

	uint8_t ddc_hw_inst;

	uint8_t hpd_src;

	uint8_t link_enc_hw_inst;
	/* DIG link encoder ID. Used as index in link encoder resource pool.
	 * For links with fixed mapping to DIG, this is not changed after dc_link
	 * object creation.
	 */
	enum engine_id eng_id;

	bool test_pattern_enabled;
	union compliance_test_state compliance_test_state;

	void *priv;

	struct ddc_service *ddc;

	bool aux_mode;

	/* Private to DC core */

	const struct dc *dc;

	struct dc_context *ctx;

	struct panel_cntl *panel_cntl;
	struct link_encoder *link_enc;
	struct graphics_object_id link_id;
	/* Endpoint type distinguishes display endpoints which do not have entries
	 * in the BIOS connector table from those that do. Helps when tracking link
	 * encoder to display endpoint assignments.
	 */
	enum display_endpoint_type ep_type;
	union ddi_channel_mapping ddi_channel_mapping;
	struct connector_device_tag_info device_tag;
	struct dpcd_caps dpcd_caps;
	uint32_t dongle_max_pix_clk;
	unsigned short chip_caps;
	unsigned int dpcd_sink_count;
#if defined(CONFIG_DRM_AMD_DC_HDCP)
	struct hdcp_caps hdcp_caps;
#endif
	enum edp_revision edp_revision;
	union dpcd_sink_ext_caps dpcd_sink_ext_caps;

	struct psr_settings psr_settings;

	/* Drive settings read from integrated info table */
	struct dc_lane_settings bios_forced_drive_settings;

	/* Vendor specific LTTPR workaround variables */
	uint8_t vendor_specific_lttpr_link_rate_wa;
	bool apply_vendor_specific_lttpr_link_rate_wa;

	/* MST record stream using this link */
	struct link_flags {
		bool dp_keep_receiver_powered;
		bool dp_skip_DID2;
		bool dp_skip_reset_segment;
		bool dp_mot_reset_segment;
		/* Some USB4 docks do not handle turning off MST DSC once it has been enabled. */
		bool dpia_mst_dsc_always_on;
		/* Forced DPIA into TBT3 compatibility mode. */
		bool dpia_forced_tbt3_mode;
		bool dongle_mode_timing_override;
	} wa_flags;
	struct link_mst_stream_allocation_table mst_stream_alloc_table;

	struct dc_link_status link_status;
	struct dprx_states dprx_states;

	struct gpio *hpd_gpio;
	enum dc_link_fec_state fec_state;
	struct dc_panel_config panel_config;
	struct phy_state phy_state;
};

const struct dc_link_status *dc_link_get_status(const struct dc_link *dc_link);

/**
 * dc_get_link_at_index() - Return an enumerated dc_link.
 *
 * dc_link order is constant and determined at
 * boot time.  They cannot be created or destroyed.
 * Use dc_get_caps() to get number of links.
 */
static inline struct dc_link *dc_get_link_at_index(struct dc *dc, uint32_t link_index)
{
	return dc->links[link_index];
}

static inline void get_edp_links(const struct dc *dc,
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

static inline bool dc_get_edp_link_panel_inst(const struct dc *dc,
		const struct dc_link *link,
		unsigned int *inst_out)
{
	struct dc_link *edp_links[MAX_NUM_EDP];
	int edp_num;

	if (link->connector_signal != SIGNAL_TYPE_EDP)
		return false;
	get_edp_links(dc, edp_links, &edp_num);
	if ((edp_num > 1) && (link->link_index > edp_links[0]->link_index))
		*inst_out = 1;
	else
		*inst_out = 0;
	return true;
}

/* Set backlight level of an embedded panel (eDP, LVDS).
 * backlight_pwm_u16_16 is unsigned 32 bit with 16 bit integer
 * and 16 bit fractional, where 1.0 is max backlight value.
 */
bool dc_link_set_backlight_level(const struct dc_link *dc_link,
		uint32_t backlight_pwm_u16_16,
		uint32_t frame_ramp);

/* Set/get nits-based backlight level of an embedded panel (eDP, LVDS). */
bool dc_link_set_backlight_level_nits(struct dc_link *link,
		bool isHDR,
		uint32_t backlight_millinits,
		uint32_t transition_time_in_ms);

bool dc_link_get_backlight_level_nits(struct dc_link *link,
		uint32_t *backlight_millinits,
		uint32_t *backlight_millinits_peak);

bool dc_link_backlight_enable_aux(struct dc_link *link, bool enable);

bool dc_link_read_default_bl_aux(struct dc_link *link, uint32_t *backlight_millinits);
bool dc_link_set_default_brightness_aux(struct dc_link *link);

int dc_link_get_backlight_level(const struct dc_link *dc_link);

int dc_link_get_target_backlight_pwm(const struct dc_link *link);

bool dc_link_set_psr_allow_active(struct dc_link *dc_link, const bool *enable,
		bool wait, bool force_static, const unsigned int *power_opts);

bool dc_link_get_psr_state(const struct dc_link *dc_link, enum dc_psr_state *state);

bool dc_link_setup_psr(struct dc_link *dc_link,
		const struct dc_stream_state *stream, struct psr_config *psr_config,
		struct psr_context *psr_context);

bool dc_power_alpm_dpcd_enable(struct dc_link *link, bool enable);

void dc_link_get_psr_residency(const struct dc_link *link, uint32_t *residency);

void dc_link_blank_all_dp_displays(struct dc *dc);
void dc_link_blank_all_edp_displays(struct dc *dc);

void dc_link_blank_dp_stream(struct dc_link *link, bool hw_init);
bool dc_link_set_sink_vtotal_in_psr_active(const struct dc_link *link,
		uint16_t psr_vtotal_idle, uint16_t psr_vtotal_su);

/* Request DC to detect if there is a Panel connected.
 * boot - If this call is during initial boot.
 * Return false for any type of detection failure or MST detection
 * true otherwise. True meaning further action is required (status update
 * and OS notification).
 */
enum dc_detect_reason {
	DETECT_REASON_BOOT,
	DETECT_REASON_RESUMEFROMS3S4,
	DETECT_REASON_HPD,
	DETECT_REASON_HPDRX,
	DETECT_REASON_FALLBACK,
	DETECT_REASON_RETRAIN,
	DETECT_REASON_TDR,
};

bool dc_link_detect(struct dc_link *dc_link, enum dc_detect_reason reason);
bool dc_link_get_hpd_state(struct dc_link *dc_link);
enum dc_status dc_link_allocate_mst_payload(struct pipe_ctx *pipe_ctx);
enum dc_status dc_link_reduce_mst_payload(struct pipe_ctx *pipe_ctx, uint32_t req_pbn);
enum dc_status dc_link_increase_mst_payload(struct pipe_ctx *pipe_ctx, uint32_t req_pbn);

/* Notify DC about DP RX Interrupt (aka Short Pulse Interrupt).
 * Return:
 * true - Downstream port status changed. DM should call DC to do the
 * detection.
 * false - no change in Downstream port status. No further action required
 * from DM. */
bool dc_link_handle_hpd_rx_irq(struct dc_link *dc_link,
		union hpd_irq_data *hpd_irq_dpcd_data, bool *out_link_loss,
		bool defer_handling, bool *has_left_work);

/*
 * On eDP links this function call will stall until T12 has elapsed.
 * If the panel is not in power off state, this function will return
 * immediately.
 */
bool dc_link_wait_for_t12(struct dc_link *link);

void dc_link_dp_handle_automated_test(struct dc_link *link);
void dc_link_dp_handle_link_loss(struct dc_link *link);
bool dc_link_dp_allow_hpd_rx_irq(const struct dc_link *link);

struct dc_sink_init_data;

struct dc_sink *dc_link_add_remote_sink(
		struct dc_link *dc_link,
		const uint8_t *edid,
		int len,
		struct dc_sink_init_data *init_data);

void dc_link_remove_remote_sink(
	struct dc_link *link,
	struct dc_sink *sink);

/* Used by diagnostics for virtual link at the moment */

void dc_link_dp_set_drive_settings(
	struct dc_link *link,
	const struct link_resource *link_res,
	struct link_training_settings *lt_settings);

bool dc_link_dp_perform_link_training_skip_aux(
	struct dc_link *link,
	const struct link_resource *link_res,
	const struct dc_link_settings *link_setting);

enum link_training_result dc_link_dp_perform_link_training(
	struct dc_link *link,
	const struct link_resource *link_res,
	const struct dc_link_settings *link_settings,
	bool skip_video_pattern);

bool dc_link_dp_sync_lt_begin(struct dc_link *link);

enum link_training_result dc_link_dp_sync_lt_attempt(
	struct dc_link *link,
	const struct link_resource *link_res,
	struct dc_link_settings *link_setting,
	struct dc_link_training_overrides *lt_settings);

bool dc_link_dp_sync_lt_end(struct dc_link *link, bool link_down);

void dc_link_dp_enable_hpd(const struct dc_link *link);

void dc_link_dp_disable_hpd(const struct dc_link *link);

bool dc_link_dp_set_test_pattern(
	struct dc_link *link,
	enum dp_test_pattern test_pattern,
	enum dp_test_pattern_color_space test_pattern_color_space,
	const struct link_training_settings *p_link_settings,
	const unsigned char *p_custom_pattern,
	unsigned int cust_pattern_size);

bool dc_link_dp_get_max_link_enc_cap(const struct dc_link *link, struct dc_link_settings *max_link_enc_cap);

void dc_link_enable_hpd_filter(struct dc_link *link, bool enable);

bool dc_link_is_dp_sink_present(struct dc_link *link);

bool dc_link_detect_sink(struct dc_link *link, enum dc_connection_type *type);
/*
 * DPCD access interfaces
 */

#ifdef CONFIG_DRM_AMD_DC_HDCP
bool dc_link_is_hdcp14(struct dc_link *link, enum signal_type signal);
bool dc_link_is_hdcp22(struct dc_link *link, enum signal_type signal);
#endif
void dc_link_set_drive_settings(struct dc *dc,
				struct link_training_settings *lt_settings,
				const struct dc_link *link);
void dc_link_set_preferred_link_settings(struct dc *dc,
					 struct dc_link_settings *link_setting,
					 struct dc_link *link);
void dc_link_set_preferred_training_settings(struct dc *dc,
					struct dc_link_settings *link_setting,
					struct dc_link_training_overrides *lt_overrides,
					struct dc_link *link,
					bool skip_immediate_retrain);
void dc_link_enable_hpd(const struct dc_link *link);
void dc_link_disable_hpd(const struct dc_link *link);
void dc_link_set_test_pattern(struct dc_link *link,
			enum dp_test_pattern test_pattern,
			enum dp_test_pattern_color_space test_pattern_color_space,
			const struct link_training_settings *p_link_settings,
			const unsigned char *p_custom_pattern,
			unsigned int cust_pattern_size);
uint32_t dc_link_bandwidth_kbps(
	const struct dc_link *link,
	const struct dc_link_settings *link_setting);

const struct dc_link_settings *dc_link_get_link_cap(
		const struct dc_link *link);

void dc_link_overwrite_extended_receiver_cap(
		struct dc_link *link);

bool dc_is_oem_i2c_device_present(
	struct dc *dc,
	size_t slave_address
);

bool dc_submit_i2c(
		struct dc *dc,
		uint32_t link_index,
		struct i2c_command *cmd);

bool dc_submit_i2c_oem(
		struct dc *dc,
		struct i2c_command *cmd);

uint32_t dc_bandwidth_in_kbps_from_timing(
	const struct dc_crtc_timing *timing);

bool dc_link_is_fec_supported(const struct dc_link *link);
bool dc_link_should_enable_fec(const struct dc_link *link);

uint32_t dc_link_bw_kbps_from_raw_frl_link_rate_data(uint8_t bw);
enum dp_link_encoding dc_link_dp_mst_decide_link_encoding_format(const struct dc_link *link);

void dc_link_get_cur_link_res(const struct dc_link *link,
		struct link_resource *link_res);
/* take a snapshot of current link resource allocation state */
void dc_get_cur_link_res_map(const struct dc *dc, uint32_t *map);
/* restore link resource allocation state from a snapshot */
void dc_restore_link_res_map(const struct dc *dc, uint32_t *map);
void dc_link_clear_dprx_states(struct dc_link *link);
struct gpio *get_hpd_gpio(struct dc_bios *dcb,
		struct graphics_object_id link_id,
		struct gpio_service *gpio_service);
void dp_trace_reset(struct dc_link *link);
bool dc_dp_trace_is_initialized(struct dc_link *link);
unsigned long long dc_dp_trace_get_lt_end_timestamp(struct dc_link *link,
		bool in_detection);
void dc_dp_trace_set_is_logged_flag(struct dc_link *link,
		bool in_detection,
		bool is_logged);
bool dc_dp_trace_is_logged(struct dc_link *link,
		bool in_detection);
struct dp_trace_lt_counts *dc_dp_trace_get_lt_counts(struct dc_link *link,
		bool in_detection);
unsigned int dc_dp_trace_get_link_loss_count(struct dc_link *link);

/* Destruct the mst topology of the link and reset the allocated payload table */
bool reset_cur_dp_mst_topology(struct dc_link *link);
#endif /* DC_LINK_H_ */
