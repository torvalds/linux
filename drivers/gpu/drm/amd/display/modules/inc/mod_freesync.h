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
};

struct mod_freesync *mod_freesync_create(struct dc *dc);
void mod_freesync_destroy(struct mod_freesync *mod_freesync);

bool mod_freesync_get_vmin_vmax(struct mod_freesync *mod_freesync,
		const struct dc_stream_state *stream,
		unsigned int *vmin,
		unsigned int *vmax);

bool mod_freesync_get_v_position(struct mod_freesync *mod_freesync,
		struct dc_stream_state *stream,
		unsigned int *nom_v_pos,
		unsigned int *v_pos);

void mod_freesync_get_settings(struct mod_freesync *mod_freesync,
		const struct mod_vrr_params *vrr,
		unsigned int *v_total_min, unsigned int *v_total_max,
		unsigned int *event_triggers,
		unsigned int *window_min, unsigned int *window_max,
		unsigned int *lfc_mid_point_in_us,
		unsigned int *inserted_frames,
		unsigned int *inserted_duration_in_us);

void mod_freesync_build_vrr_infopacket(struct mod_freesync *mod_freesync,
		const struct dc_stream_state *stream,
		const struct mod_vrr_params *vrr,
		enum vrr_packet_type packet_type,
		enum color_transfer_func app_tf,
		struct dc_info_packet *infopacket);

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

bool mod_freesync_is_valid_range(struct mod_freesync *mod_freesync,
		const struct dc_stream_state *stream,
		uint32_t min_refresh_cap_in_uhz,
		uint32_t max_refresh_cap_in_uhz,
		uint32_t min_refresh_request_in_uhz,
		uint32_t max_refresh_request_in_uhz);



#endif
