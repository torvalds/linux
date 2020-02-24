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

enum dc_link_fec_state {
	dc_link_fec_not_ready,
	dc_link_fec_ready,
	dc_link_fec_enabled
};
struct dc_link_status {
	bool link_active;
	struct dpcd_caps *dpcd_caps;
};

/* DP MST stream allocation (payload bandwidth number) */
struct link_mst_stream_allocation {
	/* DIG front */
	const struct stream_encoder *stream_enc;
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

struct time_stamp {
	uint64_t edp_poweroff;
	uint64_t edp_poweron;
};

struct link_trace {
	struct time_stamp time_stamp;
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
	bool is_lttpr_mode_transparent;

	/* caps is the same as reported_link_cap. link_traing use
	 * reported_link_cap. Will clean up.  TODO
	 */
	struct dc_link_settings reported_link_cap;
	struct dc_link_settings verified_link_cap;
	struct dc_link_settings cur_link_settings;
	struct dc_lane_settings cur_lane_setting;
	struct dc_link_settings preferred_link_setting;
	struct dc_link_training_overrides preferred_training_settings;
	struct dp_audio_test_data audio_test_data;

	uint8_t ddc_hw_inst;

	uint8_t hpd_src;

	uint8_t link_enc_hw_inst;

	bool test_pattern_enabled;
	union compliance_test_state compliance_test_state;

	void *priv;

	struct ddc_service *ddc;

	bool aux_mode;

	/* Private to DC core */

	const struct dc *dc;

	struct dc_context *ctx;

	struct link_encoder *link_enc;
	struct graphics_object_id link_id;
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
	bool psr_feature_enabled;
	bool psr_allow_active;
	union dpcd_sink_ext_caps dpcd_sink_ext_caps;

	/* MST record stream using this link */
	struct link_flags {
		bool dp_keep_receiver_powered;
		bool dp_skip_DID2;
		bool dp_skip_reset_segment;
	} wa_flags;
	struct link_mst_stream_allocation_table mst_stream_alloc_table;

	struct dc_link_status link_status;

	struct link_trace link_trace;
	struct gpio *hpd_gpio;
	enum dc_link_fec_state fec_state;
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

static inline struct dc_link *get_edp_link(const struct dc *dc)
{
	int i;

	// report any eDP links, even unconnected DDI's
	for (i = 0; i < dc->link_count; i++) {
		if (dc->links[i]->connector_signal == SIGNAL_TYPE_EDP)
			return dc->links[i];
	}
	return NULL;
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

bool dc_link_set_abm_disable(const struct dc_link *dc_link);

bool dc_link_set_psr_allow_active(struct dc_link *dc_link, bool enable, bool wait);

bool dc_link_get_psr_state(const struct dc_link *dc_link, uint32_t *psr_state);

bool dc_link_setup_psr(struct dc_link *dc_link,
		const struct dc_stream_state *stream, struct psr_config *psr_config,
		struct psr_context *psr_context);

/* Request DC to detect if there is a Panel connected.
 * boot - If this call is during initial boot.
 * Return false for any type of detection failure or MST detection
 * true otherwise. True meaning further action is required (status update
 * and OS notification).
 */
enum dc_detect_reason {
	DETECT_REASON_BOOT,
	DETECT_REASON_HPD,
	DETECT_REASON_HPDRX,
};

bool dc_link_detect(struct dc_link *dc_link, enum dc_detect_reason reason);
bool dc_link_get_hpd_state(struct dc_link *dc_link);
enum dc_status dc_link_allocate_mst_payload(struct pipe_ctx *pipe_ctx);
enum dc_status dc_link_reallocate_mst_payload(struct dc_link *link);

/* Notify DC about DP RX Interrupt (aka Short Pulse Interrupt).
 * Return:
 * true - Downstream port status changed. DM should call DC to do the
 * detection.
 * false - no change in Downstream port status. No further action required
 * from DM. */
bool dc_link_handle_hpd_rx_irq(struct dc_link *dc_link,
		union hpd_irq_data *hpd_irq_dpcd_data, bool *out_link_loss);

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
	struct link_training_settings *lt_settings);

bool dc_link_dp_perform_link_training_skip_aux(
	struct dc_link *link,
	const struct dc_link_settings *link_setting);

enum link_training_result dc_link_dp_perform_link_training(
	struct dc_link *link,
	const struct dc_link_settings *link_setting,
	bool skip_video_pattern);

bool dc_link_dp_sync_lt_begin(struct dc_link *link);

enum link_training_result dc_link_dp_sync_lt_attempt(
	struct dc_link *link,
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

void dc_link_enable_hpd_filter(struct dc_link *link, bool enable);

bool dc_link_is_dp_sink_present(struct dc_link *link);

bool dc_link_detect_sink(struct dc_link *link, enum dc_connection_type *type);
/*
 * DPCD access interfaces
 */

#ifdef CONFIG_DRM_AMD_DC_HDCP
bool dc_link_is_hdcp14(struct dc_link *link);
bool dc_link_is_hdcp22(struct dc_link *link);
#endif
void dc_link_set_drive_settings(struct dc *dc,
				struct link_training_settings *lt_settings,
				const struct dc_link *link);
void dc_link_perform_link_training(struct dc *dc,
				   struct dc_link_settings *link_setting,
				   bool skip_video_pattern);
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

#endif /* DC_LINK_H_ */
