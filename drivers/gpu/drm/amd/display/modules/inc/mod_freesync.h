/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef MOD_FREESYNC_H_
#define MOD_FREESYNC_H_

#include "mod_shared.h"

// Access structures
struct mod_freesync {
	int dummy;
};

// TODO: References to this should be removed
struct mod_freesync_caps {
	bool supported;
	unsigned int min_refresh_in_micro_hz;
	unsigned int max_refresh_in_micro_hz;
};

enum mod_vrr_state {
	VRR_STATE_UNSUPPORTED = 0,
	VRR_STATE_DISABLED,
	VRR_STATE_INACTIVE,
	VRR_STATE_ACTIVE_VARIABLE,
	VRR_STATE_ACTIVE_FIXED
};

struct mod_freesync_config {
	enum mod_vrr_state state;
	bool vsif_supported;
	bool ramping;
	bool btr;
	unsigned int min_refresh_in_uhz;
	unsigned int max_refresh_in_uhz;
	unsigned int fixed_refresh_in_uhz;

};

struct mod_vrr_params_btr {
	bool btr_enabled;
	bool btr_active;
	uint32_t mid_point_in_us;
	uint32_t inserted_duration_in_us;
	uint32_t frames_to_insert;
	uint32_t frame_counter;
	uint32_t margin_in_us;
};

struct mod_vrr_params_fixed_refresh {
	bool fixed_active;
	bool ramping_active;
	bool ramping_done;
	uint32_t target_refresh_in_uhz;
	uint32_t frame_counter;
};

struct mod_vrr_params_flip_interval {
	bool flip_interval_workaround_active;
	bool program_flip_interval_workaround;
	bool do_flip_interval_workaround_cleanup;
	uint32_t flip_interval_detect_counter;
	uint32_t vsyncs_between_flip;
	uint32_t vsync_to_flip_in_us;
	uint32_t v_update_timestamp_in_us;
};

struct mod_vrr_params {
	bool supported;
	bool send_info_frame;
	enum mod_vrr_state state;

	uint32_t min_refresh_in_uhz;
	uint32_t max_duration_in_us;
	uint32_t max_refresh_in_uhz;
	uint32_t min_duration_in_us;
	uint32_t fixed_refresh_in_uhz;

	struct dc_crtc_timing_adjust adjust;

	struct mod_vrr_params_fixed_refresh fixed;

	struct mod_vrr_params_btr btr;

	struct mod_vrr_params_flip_interval flip_interval;
};

struct mod_freesync *mod_freesync_create(struct dc *dc);
void mod_freesync_destroy(struct mod_freesync *mod_freesync);

void mod_freesync_build_vrr_infopacket(struct mod_freesync *mod_freesync,
		const struct dc_stream_state *stream,
		const struct mod_vrr_params *vrr,
		enum vrr_packet_type packet_type,
		enum color_transfer_func app_tf,
		struct dc_info_packet *infopacket,
		bool pack_sdp_v1_3);

void mod_freesync_build_vrr_params(struct mod_freesync *mod_freesync,
		const struct dc_stream_state *stream,
		struct mod_freesync_config *in_config,
		struct mod_vrr_params *in_out_vrr);

void mod_freesync_handle_preflip(struct mod_freesync *mod_freesync,
		const struct dc_plane_state *plane,
		const struct dc_stream_state *stream,
		unsigned int curr_time_stamp_in_us,
		struct mod_vrr_params *in_out_vrr);

void mod_freesync_handle_v_update(struct mod_freesync *mod_freesync,
		const struct dc_stream_state *stream,
		struct mod_vrr_params *in_out_vrr);

unsigned long long mod_freesync_calc_nominal_field_rate(
			const struct dc_stream_state *stream);

unsigned int mod_freesync_calc_v_total_from_refresh(
		const struct dc_stream_state *stream,
		unsigned int refresh_in_uhz);

// Returns true when FreeSync is supported and enabled (even if it is inactive)
bool mod_freesync_get_freesync_enabled(struct mod_vrr_params *pVrr);

#endif
