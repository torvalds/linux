/*
 * Copyright 2016-2023 Advanced Micro Devices, Inc.
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
#include "dc.h"
#include "mod_freesync.h"
#include "core_types.h"

#define MOD_FREESYNC_MAX_CONCURRENT_STREAMS  32

#define MIN_REFRESH_RANGE 10
/* Refresh rate ramp at a fixed rate of 65 Hz/second */
#define STATIC_SCREEN_RAMP_DELTA_REFRESH_RATE_PER_FRAME ((1000 / 60) * 65)
/* Number of elements in the render times cache array */
#define RENDER_TIMES_MAX_COUNT 10
/* Threshold to exit/exit BTR (to avoid frequent enter-exits at the lower limit) */
#define BTR_MAX_MARGIN 2500
/* Threshold to change BTR multiplier (to avoid frequent changes) */
#define BTR_DRIFT_MARGIN 2000
/* Threshold to exit fixed refresh rate */
#define FIXED_REFRESH_EXIT_MARGIN_IN_HZ 1
/* Number of consecutive frames to check before entering/exiting fixed refresh */
#define FIXED_REFRESH_ENTER_FRAME_COUNT 5
#define FIXED_REFRESH_EXIT_FRAME_COUNT 10
/* Flip interval workaround constants */
#define VSYNCS_BETWEEN_FLIP_THRESHOLD 2
#define FREESYNC_CONSEC_FLIP_AFTER_VSYNC 5
#define FREESYNC_VSYNC_TO_FLIP_DELTA_IN_US 500

struct core_freesync {
	struct mod_freesync public;
	struct dc *dc;
};

#define MOD_FREESYNC_TO_CORE(mod_freesync)\
		container_of(mod_freesync, struct core_freesync, public)

struct mod_freesync *mod_freesync_create(struct dc *dc)
{
	struct core_freesync *core_freesync =
			kzalloc(sizeof(struct core_freesync), GFP_KERNEL);

	if (core_freesync == NULL)
		goto fail_alloc_context;

	if (dc == NULL)
		goto fail_construct;

	core_freesync->dc = dc;
	return &core_freesync->public;

fail_construct:
	kfree(core_freesync);

fail_alloc_context:
	return NULL;
}

void mod_freesync_destroy(struct mod_freesync *mod_freesync)
{
	struct core_freesync *core_freesync = NULL;

	if (mod_freesync == NULL)
		return;
	core_freesync = MOD_FREESYNC_TO_CORE(mod_freesync);
	kfree(core_freesync);
}

#if 0 /* Unused currently */
static unsigned int calc_refresh_in_uhz_from_duration(
		unsigned int duration_in_ns)
{
	unsigned int refresh_in_uhz =
			((unsigned int)(div64_u64((1000000000ULL * 1000000),
					duration_in_ns)));
	return refresh_in_uhz;
}
#endif

static unsigned int calc_duration_in_us_from_refresh_in_uhz(
		unsigned int refresh_in_uhz)
{
	unsigned int duration_in_us =
			((unsigned int)(div64_u64((1000000000ULL * 1000),
					refresh_in_uhz)));
	return duration_in_us;
}

static unsigned int calc_duration_in_us_from_v_total(
		const struct dc_stream_state *stream,
		const struct mod_vrr_params *in_vrr,
		unsigned int v_total)
{
	unsigned int duration_in_us =
			(unsigned int)(div64_u64(((unsigned long long)(v_total)
				* 10000) * stream->timing.h_total,
					stream->timing.pix_clk_100hz));

	return duration_in_us;
}

unsigned int mod_freesync_calc_v_total_from_refresh(
		const struct dc_stream_state *stream,
		unsigned int refresh_in_uhz)
{
	unsigned int v_total;
	unsigned int frame_duration_in_ns;

	frame_duration_in_ns =
			((unsigned int)(div64_u64((1000000000ULL * 1000000),
					refresh_in_uhz)));

	v_total = div64_u64(div64_u64(((unsigned long long)(
			frame_duration_in_ns) * (stream->timing.pix_clk_100hz / 10)),
			stream->timing.h_total), 1000000);

	/* v_total cannot be less than nominal */
	if (v_total < stream->timing.v_total) {
		ASSERT(v_total < stream->timing.v_total);
		v_total = stream->timing.v_total;
	}

	return v_total;
}

static unsigned int calc_v_total_from_duration(
		const struct dc_stream_state *stream,
		const struct mod_vrr_params *vrr,
		unsigned int duration_in_us)
{
	unsigned int v_total = 0;

	if (duration_in_us < vrr->min_duration_in_us)
		duration_in_us = vrr->min_duration_in_us;

	if (duration_in_us > vrr->max_duration_in_us)
		duration_in_us = vrr->max_duration_in_us;

	if (dc_is_hdmi_signal(stream->signal)) { // change for HDMI to comply with spec
		uint32_t h_total_up_scaled;

		h_total_up_scaled = stream->timing.h_total * 10000;
		v_total = div_u64((unsigned long long)duration_in_us
					* stream->timing.pix_clk_100hz + (h_total_up_scaled - 1),
					h_total_up_scaled); //ceiling for MMax and MMin for MVRR
	} else {
		v_total = div64_u64(div64_u64(((unsigned long long)(
					duration_in_us) * (stream->timing.pix_clk_100hz / 10)),
					stream->timing.h_total), 1000);
	}

	/* v_total cannot be less than nominal */
	if (v_total < stream->timing.v_total) {
		ASSERT(v_total < stream->timing.v_total);
		v_total = stream->timing.v_total;
	}

	return v_total;
}

static void update_v_total_for_static_ramp(
		struct core_freesync *core_freesync,
		const struct dc_stream_state *stream,
		struct mod_vrr_params *in_out_vrr)
{
	unsigned int v_total = 0;
	unsigned int current_duration_in_us =
			calc_duration_in_us_from_v_total(
				stream, in_out_vrr,
				in_out_vrr->adjust.v_total_max);
	unsigned int target_duration_in_us =
			calc_duration_in_us_from_refresh_in_uhz(
				in_out_vrr->fixed.target_refresh_in_uhz);
	bool ramp_direction_is_up = (current_duration_in_us >
				target_duration_in_us) ? true : false;

	/* Calculate ratio between new and current frame duration with 3 digit */
	unsigned int frame_duration_ratio = div64_u64(1000000,
		(1000 +  div64_u64(((unsigned long long)(
		STATIC_SCREEN_RAMP_DELTA_REFRESH_RATE_PER_FRAME) *
		current_duration_in_us),
		1000000)));

	/* Calculate delta between new and current frame duration in us */
	unsigned int frame_duration_delta = div64_u64(((unsigned long long)(
		current_duration_in_us) *
		(1000 - frame_duration_ratio)), 1000);

	/* Adjust frame duration delta based on ratio between current and
	 * standard frame duration (frame duration at 60 Hz refresh rate).
	 */
	unsigned int ramp_rate_interpolated = div64_u64(((unsigned long long)(
		frame_duration_delta) * current_duration_in_us), 16666);

	/* Going to a higher refresh rate (lower frame duration) */
	if (ramp_direction_is_up) {
		/* Reduce frame duration */
		current_duration_in_us -= ramp_rate_interpolated;

		/* Adjust for frame duration below min */
		if (current_duration_in_us <= target_duration_in_us) {
			in_out_vrr->fixed.ramping_active = false;
			in_out_vrr->fixed.ramping_done = true;
			current_duration_in_us =
				calc_duration_in_us_from_refresh_in_uhz(
				in_out_vrr->fixed.target_refresh_in_uhz);
		}
	/* Going to a lower refresh rate (larger frame duration) */
	} else {
		/* Increase frame duration */
		current_duration_in_us += ramp_rate_interpolated;

		/* Adjust for frame duration above max */
		if (current_duration_in_us >= target_duration_in_us) {
			in_out_vrr->fixed.ramping_active = false;
			in_out_vrr->fixed.ramping_done = true;
			current_duration_in_us =
				calc_duration_in_us_from_refresh_in_uhz(
				in_out_vrr->fixed.target_refresh_in_uhz);
		}
	}

	v_total = div64_u64(div64_u64(((unsigned long long)(
			current_duration_in_us) * (stream->timing.pix_clk_100hz / 10)),
				stream->timing.h_total), 1000);

	/* v_total cannot be less than nominal */
	if (v_total < stream->timing.v_total)
		v_total = stream->timing.v_total;

	in_out_vrr->adjust.v_total_min = v_total;
	in_out_vrr->adjust.v_total_max = v_total;
}

static void apply_below_the_range(struct core_freesync *core_freesync,
		const struct dc_stream_state *stream,
		unsigned int last_render_time_in_us,
		struct mod_vrr_params *in_out_vrr)
{
	unsigned int inserted_frame_duration_in_us = 0;
	unsigned int mid_point_frames_ceil = 0;
	unsigned int mid_point_frames_floor = 0;
	unsigned int frame_time_in_us = 0;
	unsigned int delta_from_mid_point_in_us_1 = 0xFFFFFFFF;
	unsigned int delta_from_mid_point_in_us_2 = 0xFFFFFFFF;
	unsigned int frames_to_insert = 0;
	unsigned int delta_from_mid_point_delta_in_us;
	unsigned int max_render_time_in_us =
			in_out_vrr->max_duration_in_us - in_out_vrr->btr.margin_in_us;

	/* Program BTR */
	if ((last_render_time_in_us + in_out_vrr->btr.margin_in_us / 2) < max_render_time_in_us) {
		/* Exit Below the Range */
		if (in_out_vrr->btr.btr_active) {
			in_out_vrr->btr.frame_counter = 0;
			in_out_vrr->btr.btr_active = false;
		}
	} else if (last_render_time_in_us > (max_render_time_in_us + in_out_vrr->btr.margin_in_us / 2)) {
		/* Enter Below the Range */
		if (!in_out_vrr->btr.btr_active)
			in_out_vrr->btr.btr_active = true;
	}

	/* BTR set to "not active" so disengage */
	if (!in_out_vrr->btr.btr_active) {
		in_out_vrr->btr.inserted_duration_in_us = 0;
		in_out_vrr->btr.frames_to_insert = 0;
		in_out_vrr->btr.frame_counter = 0;

		/* Restore FreeSync */
		in_out_vrr->adjust.v_total_min =
			mod_freesync_calc_v_total_from_refresh(stream,
				in_out_vrr->max_refresh_in_uhz);
		in_out_vrr->adjust.v_total_max =
			mod_freesync_calc_v_total_from_refresh(stream,
				in_out_vrr->min_refresh_in_uhz);
	/* BTR set to "active" so engage */
	} else {

		/* Calculate number of midPoint frames that could fit within
		 * the render time interval - take ceil of this value
		 */
		mid_point_frames_ceil = (last_render_time_in_us +
				in_out_vrr->btr.mid_point_in_us - 1) /
					in_out_vrr->btr.mid_point_in_us;

		if (mid_point_frames_ceil > 0) {
			frame_time_in_us = last_render_time_in_us /
				mid_point_frames_ceil;
			delta_from_mid_point_in_us_1 =
				(in_out_vrr->btr.mid_point_in_us >
				frame_time_in_us) ?
				(in_out_vrr->btr.mid_point_in_us - frame_time_in_us) :
				(frame_time_in_us - in_out_vrr->btr.mid_point_in_us);
		}

		/* Calculate number of midPoint frames that could fit within
		 * the render time interval - take floor of this value
		 */
		mid_point_frames_floor = last_render_time_in_us /
				in_out_vrr->btr.mid_point_in_us;

		if (mid_point_frames_floor > 0) {

			frame_time_in_us = last_render_time_in_us /
				mid_point_frames_floor;
			delta_from_mid_point_in_us_2 =
				(in_out_vrr->btr.mid_point_in_us >
				frame_time_in_us) ?
				(in_out_vrr->btr.mid_point_in_us - frame_time_in_us) :
				(frame_time_in_us - in_out_vrr->btr.mid_point_in_us);
		}

		/* Choose number of frames to insert based on how close it
		 * can get to the mid point of the variable range.
		 *  - Delta for CEIL: delta_from_mid_point_in_us_1
		 *  - Delta for FLOOR: delta_from_mid_point_in_us_2
		 */
		if (mid_point_frames_ceil &&
		    (last_render_time_in_us / mid_point_frames_ceil) <
		    in_out_vrr->min_duration_in_us) {
			/* Check for out of range.
			 * If using CEIL produces a value that is out of range,
			 * then we are forced to use FLOOR.
			 */
			frames_to_insert = mid_point_frames_floor;
		} else if (mid_point_frames_floor < 2) {
			/* Check if FLOOR would result in non-LFC. In this case
			 * choose to use CEIL
			 */
			frames_to_insert = mid_point_frames_ceil;
		} else if (delta_from_mid_point_in_us_1 < delta_from_mid_point_in_us_2) {
			/* If choosing CEIL results in a frame duration that is
			 * closer to the mid point of the range.
			 * Choose CEIL
			 */
			frames_to_insert = mid_point_frames_ceil;
		} else {
			/* If choosing FLOOR results in a frame duration that is
			 * closer to the mid point of the range.
			 * Choose FLOOR
			 */
			frames_to_insert = mid_point_frames_floor;
		}

		/* Prefer current frame multiplier when BTR is enabled unless it drifts
		 * too far from the midpoint
		 */
		if (delta_from_mid_point_in_us_1 < delta_from_mid_point_in_us_2) {
			delta_from_mid_point_delta_in_us = delta_from_mid_point_in_us_2 -
					delta_from_mid_point_in_us_1;
		} else {
			delta_from_mid_point_delta_in_us = delta_from_mid_point_in_us_1 -
					delta_from_mid_point_in_us_2;
		}
		if (in_out_vrr->btr.frames_to_insert != 0 &&
				delta_from_mid_point_delta_in_us < BTR_DRIFT_MARGIN) {
			if (((last_render_time_in_us / in_out_vrr->btr.frames_to_insert) <
					max_render_time_in_us) &&
				((last_render_time_in_us / in_out_vrr->btr.frames_to_insert) >
					in_out_vrr->min_duration_in_us))
				frames_to_insert = in_out_vrr->btr.frames_to_insert;
		}

		/* Either we've calculated the number of frames to insert,
		 * or we need to insert min duration frames
		 */
		if (frames_to_insert &&
		    (last_render_time_in_us / frames_to_insert) <
		    in_out_vrr->min_duration_in_us){
			frames_to_insert -= (frames_to_insert > 1) ?
					1 : 0;
		}

		if (frames_to_insert > 0)
			inserted_frame_duration_in_us = last_render_time_in_us /
							frames_to_insert;

		if (inserted_frame_duration_in_us < in_out_vrr->min_duration_in_us)
			inserted_frame_duration_in_us = in_out_vrr->min_duration_in_us;

		/* Cache the calculated variables */
		in_out_vrr->btr.inserted_duration_in_us =
			inserted_frame_duration_in_us;
		in_out_vrr->btr.frames_to_insert = frames_to_insert;
		in_out_vrr->btr.frame_counter = frames_to_insert;
	}
}

static void apply_fixed_refresh(struct core_freesync *core_freesync,
		const struct dc_stream_state *stream,
		unsigned int last_render_time_in_us,
		struct mod_vrr_params *in_out_vrr)
{
	bool update = false;
	unsigned int max_render_time_in_us = in_out_vrr->max_duration_in_us;

	/* Compute the exit refresh rate and exit frame duration */
	unsigned int exit_refresh_rate_in_milli_hz = ((1000000000/max_render_time_in_us)
			+ (1000*FIXED_REFRESH_EXIT_MARGIN_IN_HZ));
	unsigned int exit_frame_duration_in_us = 1000000000/exit_refresh_rate_in_milli_hz;

	if (last_render_time_in_us < exit_frame_duration_in_us) {
		/* Exit Fixed Refresh mode */
		if (in_out_vrr->fixed.fixed_active) {
			in_out_vrr->fixed.frame_counter++;

			if (in_out_vrr->fixed.frame_counter >
					FIXED_REFRESH_EXIT_FRAME_COUNT) {
				in_out_vrr->fixed.frame_counter = 0;
				in_out_vrr->fixed.fixed_active = false;
				in_out_vrr->fixed.target_refresh_in_uhz = 0;
				update = true;
			}
		} else
			in_out_vrr->fixed.frame_counter = 0;
	} else if (last_render_time_in_us > max_render_time_in_us) {
		/* Enter Fixed Refresh mode */
		if (!in_out_vrr->fixed.fixed_active) {
			in_out_vrr->fixed.frame_counter++;

			if (in_out_vrr->fixed.frame_counter >
					FIXED_REFRESH_ENTER_FRAME_COUNT) {
				in_out_vrr->fixed.frame_counter = 0;
				in_out_vrr->fixed.fixed_active = true;
				in_out_vrr->fixed.target_refresh_in_uhz =
						in_out_vrr->max_refresh_in_uhz;
				update = true;
			}
		} else
			in_out_vrr->fixed.frame_counter = 0;
	}

	if (update) {
		if (in_out_vrr->fixed.fixed_active) {
			in_out_vrr->adjust.v_total_min =
				mod_freesync_calc_v_total_from_refresh(
				stream, in_out_vrr->max_refresh_in_uhz);
			in_out_vrr->adjust.v_total_max =
					in_out_vrr->adjust.v_total_min;
		} else {
			in_out_vrr->adjust.v_total_min =
				mod_freesync_calc_v_total_from_refresh(stream,
					in_out_vrr->max_refresh_in_uhz);
			in_out_vrr->adjust.v_total_max =
				mod_freesync_calc_v_total_from_refresh(stream,
					in_out_vrr->min_refresh_in_uhz);
		}
	}
}

static void determine_flip_interval_workaround_req(struct mod_vrr_params *in_vrr,
		unsigned int curr_time_stamp_in_us)
{
	in_vrr->flip_interval.vsync_to_flip_in_us = curr_time_stamp_in_us -
			in_vrr->flip_interval.v_update_timestamp_in_us;

	/* Determine conditions for stopping workaround */
	if (in_vrr->flip_interval.flip_interval_workaround_active &&
			in_vrr->flip_interval.vsyncs_between_flip < VSYNCS_BETWEEN_FLIP_THRESHOLD &&
			in_vrr->flip_interval.vsync_to_flip_in_us > FREESYNC_VSYNC_TO_FLIP_DELTA_IN_US) {
		in_vrr->flip_interval.flip_interval_detect_counter = 0;
		in_vrr->flip_interval.program_flip_interval_workaround = true;
		in_vrr->flip_interval.flip_interval_workaround_active = false;
	} else {
		/* Determine conditions for starting workaround */
		if (in_vrr->flip_interval.vsyncs_between_flip >= VSYNCS_BETWEEN_FLIP_THRESHOLD &&
				in_vrr->flip_interval.vsync_to_flip_in_us < FREESYNC_VSYNC_TO_FLIP_DELTA_IN_US) {
			/* Increase flip interval counter we have 2 vsyncs between flips and
			 * vsync to flip interval is less than 500us
			 */
			in_vrr->flip_interval.flip_interval_detect_counter++;
			if (in_vrr->flip_interval.flip_interval_detect_counter > FREESYNC_CONSEC_FLIP_AFTER_VSYNC) {
				/* Start workaround if we detect 5 consecutive instances of the above case */
				in_vrr->flip_interval.program_flip_interval_workaround = true;
				in_vrr->flip_interval.flip_interval_workaround_active = true;
			}
		} else {
			/* Reset the flip interval counter if we condition is no longer met */
			in_vrr->flip_interval.flip_interval_detect_counter = 0;
		}
	}

	in_vrr->flip_interval.vsyncs_between_flip = 0;
}

static bool vrr_settings_require_update(struct core_freesync *core_freesync,
		struct mod_freesync_config *in_config,
		unsigned int min_refresh_in_uhz,
		unsigned int max_refresh_in_uhz,
		struct mod_vrr_params *in_vrr)
{
	if (in_vrr->state != in_config->state) {
		return true;
	} else if (in_vrr->state == VRR_STATE_ACTIVE_FIXED &&
			in_vrr->fixed.target_refresh_in_uhz !=
					in_config->fixed_refresh_in_uhz) {
		return true;
	} else if (in_vrr->min_refresh_in_uhz != min_refresh_in_uhz) {
		return true;
	} else if (in_vrr->max_refresh_in_uhz != max_refresh_in_uhz) {
		return true;
	}

	return false;
}

bool mod_freesync_get_vmin_vmax(struct mod_freesync *mod_freesync,
		const struct dc_stream_state *stream,
		unsigned int *vmin,
		unsigned int *vmax)
{
	*vmin = stream->adjust.v_total_min;
	*vmax = stream->adjust.v_total_max;

	return true;
}

bool mod_freesync_get_v_position(struct mod_freesync *mod_freesync,
		struct dc_stream_state *stream,
		unsigned int *nom_v_pos,
		unsigned int *v_pos)
{
	struct core_freesync *core_freesync = NULL;
	struct crtc_position position;

	if (mod_freesync == NULL)
		return false;

	core_freesync = MOD_FREESYNC_TO_CORE(mod_freesync);

	if (dc_stream_get_crtc_position(core_freesync->dc, &stream, 1,
					&position.vertical_count,
					&position.nominal_vcount)) {

		*nom_v_pos = position.nominal_vcount;
		*v_pos = position.vertical_count;

		return true;
	}

	return false;
}

static void build_vrr_infopacket_data_v1(const struct mod_vrr_params *vrr,
		struct dc_info_packet *infopacket,
		bool freesync_on_desktop)
{
	/* PB1 = 0x1A (24bit AMD IEEE OUI (0x00001A) - Byte 0) */
	infopacket->sb[1] = 0x1A;

	/* PB2 = 0x00 (24bit AMD IEEE OUI (0x00001A) - Byte 1) */
	infopacket->sb[2] = 0x00;

	/* PB3 = 0x00 (24bit AMD IEEE OUI (0x00001A) - Byte 2) */
	infopacket->sb[3] = 0x00;

	/* PB4 = Reserved */

	/* PB5 = Reserved */

	/* PB6 = [Bits 7:3 = Reserved] */

	/* PB6 = [Bit 0 = FreeSync Supported] */
	if (vrr->state != VRR_STATE_UNSUPPORTED)
		infopacket->sb[6] |= 0x01;

	/* PB6 = [Bit 1 = FreeSync Enabled] */
	if (vrr->state != VRR_STATE_DISABLED &&
			vrr->state != VRR_STATE_UNSUPPORTED)
		infopacket->sb[6] |= 0x02;

	if (freesync_on_desktop) {
		/* PB6 = [Bit 2 = FreeSync Active] */
		if (vrr->state != VRR_STATE_DISABLED &&
			vrr->state != VRR_STATE_UNSUPPORTED)
			infopacket->sb[6] |= 0x04;
	} else {
		if (vrr->state == VRR_STATE_ACTIVE_VARIABLE ||
			vrr->state == VRR_STATE_ACTIVE_FIXED)
			infopacket->sb[6] |= 0x04;
	}

	// For v1 & 2 infoframes program nominal if non-fs mode, otherwise full range
	/* PB7 = FreeSync Minimum refresh rate (Hz) */
	if (vrr->state == VRR_STATE_ACTIVE_VARIABLE ||
			vrr->state == VRR_STATE_ACTIVE_FIXED) {
		infopacket->sb[7] = (unsigned char)((vrr->min_refresh_in_uhz + 500000) / 1000000);
	} else {
		infopacket->sb[7] = (unsigned char)((vrr->max_refresh_in_uhz + 500000) / 1000000);
	}

	/* PB8 = FreeSync Maximum refresh rate (Hz)
	 * Note: We should never go above the field rate of the mode timing set.
	 */
	infopacket->sb[8] = (unsigned char)((vrr->max_refresh_in_uhz + 500000) / 1000000);
}

static void build_vrr_infopacket_data_v3(const struct mod_vrr_params *vrr,
		struct dc_info_packet *infopacket,
		bool freesync_on_desktop)
{
	unsigned int min_refresh;
	unsigned int max_refresh;
	unsigned int fixed_refresh;
	unsigned int min_programmed;

	/* PB1 = 0x1A (24bit AMD IEEE OUI (0x00001A) - Byte 0) */
	infopacket->sb[1] = 0x1A;

	/* PB2 = 0x00 (24bit AMD IEEE OUI (0x00001A) - Byte 1) */
	infopacket->sb[2] = 0x00;

	/* PB3 = 0x00 (24bit AMD IEEE OUI (0x00001A) - Byte 2) */
	infopacket->sb[3] = 0x00;

	/* PB4 = Reserved */

	/* PB5 = Reserved */

	/* PB6 = [Bits 7:3 = Reserved] */

	/* PB6 = [Bit 0 = FreeSync Supported] */
	if (vrr->state != VRR_STATE_UNSUPPORTED)
		infopacket->sb[6] |= 0x01;

	/* PB6 = [Bit 1 = FreeSync Enabled] */
	if (vrr->state != VRR_STATE_DISABLED &&
			vrr->state != VRR_STATE_UNSUPPORTED)
		infopacket->sb[6] |= 0x02;

	/* PB6 = [Bit 2 = FreeSync Active] */
	if (freesync_on_desktop) {
		if (vrr->state != VRR_STATE_DISABLED &&
			vrr->state != VRR_STATE_UNSUPPORTED)
			infopacket->sb[6] |= 0x04;
	} else {
		if (vrr->state == VRR_STATE_ACTIVE_VARIABLE ||
			vrr->state == VRR_STATE_ACTIVE_FIXED)
			infopacket->sb[6] |= 0x04;
	}

	min_refresh = (vrr->min_refresh_in_uhz + 500000) / 1000000;
	max_refresh = (vrr->max_refresh_in_uhz + 500000) / 1000000;
	fixed_refresh = (vrr->fixed_refresh_in_uhz + 500000) / 1000000;

	min_programmed = (vrr->state == VRR_STATE_ACTIVE_FIXED) ? fixed_refresh :
			(vrr->state == VRR_STATE_ACTIVE_VARIABLE) ? min_refresh :
			(vrr->state == VRR_STATE_INACTIVE) ? min_refresh :
			max_refresh; // Non-fs case, program nominal range

	/* PB7 = FreeSync Minimum refresh rate (Hz) */
	infopacket->sb[7] = min_programmed & 0xFF;

	/* PB8 = FreeSync Maximum refresh rate (Hz) */
	infopacket->sb[8] = max_refresh & 0xFF;

	/* PB11 : MSB FreeSync Minimum refresh rate [Hz] - bits 9:8 */
	infopacket->sb[11] = (min_programmed >> 8) & 0x03;

	/* PB12 : MSB FreeSync Maximum refresh rate [Hz] - bits 9:8 */
	infopacket->sb[12] = (max_refresh >> 8) & 0x03;

	/* PB16 : Reserved bits 7:1, FixedRate bit 0 */
	infopacket->sb[16] = (vrr->state == VRR_STATE_ACTIVE_FIXED) ? 1 : 0;
}

static void build_vrr_infopacket_fs2_data(enum color_transfer_func app_tf,
		struct dc_info_packet *infopacket)
{
	if (app_tf != TRANSFER_FUNC_UNKNOWN) {
		infopacket->valid = true;

		if (app_tf == TRANSFER_FUNC_PQ2084)
			infopacket->sb[9] |= 0x20; // PB9 = [Bit 5 = PQ EOTF Active]
		else {
			infopacket->sb[6] |= 0x08;  // PB6 = [Bit 3 = Native Color Active]
			if (app_tf == TRANSFER_FUNC_GAMMA_22)
				infopacket->sb[9] |= 0x04;  // PB9 = [Bit 2 = Gamma 2.2 EOTF Active]
		}
	}
}

static void build_vrr_infopacket_header_v1(enum signal_type signal,
		struct dc_info_packet *infopacket,
		unsigned int *payload_size)
{
	if (dc_is_hdmi_signal(signal)) {

		/* HEADER */

		/* HB0  = Packet Type = 0x83 (Source Product
		 *	  Descriptor InfoFrame)
		 */
		infopacket->hb0 = DC_HDMI_INFOFRAME_TYPE_SPD;

		/* HB1  = Version = 0x01 */
		infopacket->hb1 = 0x01;

		/* HB2  = [Bits 7:5 = 0] [Bits 4:0 = Length = 0x08] */
		infopacket->hb2 = 0x08;

		*payload_size = 0x08;

	} else if (dc_is_dp_signal(signal)) {

		/* HEADER */

		/* HB0  = Secondary-data Packet ID = 0 - Only non-zero
		 *	  when used to associate audio related info packets
		 */
		infopacket->hb0 = 0x00;

		/* HB1  = Packet Type = 0x83 (Source Product
		 *	  Descriptor InfoFrame)
		 */
		infopacket->hb1 = DC_HDMI_INFOFRAME_TYPE_SPD;

		/* HB2  = [Bits 7:0 = Least significant eight bits -
		 *	  For INFOFRAME, the value must be 1Bh]
		 */
		infopacket->hb2 = 0x1B;

		/* HB3  = [Bits 7:2 = INFOFRAME SDP Version Number = 0x1]
		 *	  [Bits 1:0 = Most significant two bits = 0x00]
		 */
		infopacket->hb3 = 0x04;

		*payload_size = 0x1B;
	}
}

static void build_vrr_infopacket_header_v2(enum signal_type signal,
		struct dc_info_packet *infopacket,
		unsigned int *payload_size)
{
	if (dc_is_hdmi_signal(signal)) {

		/* HEADER */

		/* HB0  = Packet Type = 0x83 (Source Product
		 *	  Descriptor InfoFrame)
		 */
		infopacket->hb0 = DC_HDMI_INFOFRAME_TYPE_SPD;

		/* HB1  = Version = 0x02 */
		infopacket->hb1 = 0x02;

		/* HB2  = [Bits 7:5 = 0] [Bits 4:0 = Length = 0x09] */
		infopacket->hb2 = 0x09;

		*payload_size = 0x09;
	} else if (dc_is_dp_signal(signal)) {

		/* HEADER */

		/* HB0  = Secondary-data Packet ID = 0 - Only non-zero
		 *	  when used to associate audio related info packets
		 */
		infopacket->hb0 = 0x00;

		/* HB1  = Packet Type = 0x83 (Source Product
		 *	  Descriptor InfoFrame)
		 */
		infopacket->hb1 = DC_HDMI_INFOFRAME_TYPE_SPD;

		/* HB2  = [Bits 7:0 = Least significant eight bits -
		 *	  For INFOFRAME, the value must be 1Bh]
		 */
		infopacket->hb2 = 0x1B;

		/* HB3  = [Bits 7:2 = INFOFRAME SDP Version Number = 0x2]
		 *	  [Bits 1:0 = Most significant two bits = 0x00]
		 */
		infopacket->hb3 = 0x08;

		*payload_size = 0x1B;
	}
}

static void build_vrr_infopacket_header_v3(enum signal_type signal,
		struct dc_info_packet *infopacket,
		unsigned int *payload_size)
{
	unsigned char version;

	version = 3;
	if (dc_is_hdmi_signal(signal)) {

		/* HEADER */

		/* HB0  = Packet Type = 0x83 (Source Product
		 *	  Descriptor InfoFrame)
		 */
		infopacket->hb0 = DC_HDMI_INFOFRAME_TYPE_SPD;

		/* HB1  = Version = 0x03 */
		infopacket->hb1 = version;

		/* HB2  = [Bits 7:5 = 0] [Bits 4:0 = Length] */
		infopacket->hb2 = 0x10;

		*payload_size = 0x10;
	} else if (dc_is_dp_signal(signal)) {

		/* HEADER */

		/* HB0  = Secondary-data Packet ID = 0 - Only non-zero
		 *	  when used to associate audio related info packets
		 */
		infopacket->hb0 = 0x00;

		/* HB1  = Packet Type = 0x83 (Source Product
		 *	  Descriptor InfoFrame)
		 */
		infopacket->hb1 = DC_HDMI_INFOFRAME_TYPE_SPD;

		/* HB2  = [Bits 7:0 = Least significant eight bits -
		 *	  For INFOFRAME, the value must be 1Bh]
		 */
		infopacket->hb2 = 0x1B;

		/* HB3  = [Bits 7:2 = INFOFRAME SDP Version Number = 0x2]
		 *	  [Bits 1:0 = Most significant two bits = 0x00]
		 */

		infopacket->hb3 = (version & 0x3F) << 2;

		*payload_size = 0x1B;
	}
}

static void build_vrr_infopacket_checksum(unsigned int *payload_size,
		struct dc_info_packet *infopacket)
{
	/* Calculate checksum */
	unsigned int idx = 0;
	unsigned char checksum = 0;

	checksum += infopacket->hb0;
	checksum += infopacket->hb1;
	checksum += infopacket->hb2;
	checksum += infopacket->hb3;

	for (idx = 1; idx <= *payload_size; idx++)
		checksum += infopacket->sb[idx];

	/* PB0 = Checksum (one byte complement) */
	infopacket->sb[0] = (unsigned char)(0x100 - checksum);

	infopacket->valid = true;
}

static void build_vrr_infopacket_v1(enum signal_type signal,
		const struct mod_vrr_params *vrr,
		struct dc_info_packet *infopacket,
		bool freesync_on_desktop)
{
	/* SPD info packet for FreeSync */
	unsigned int payload_size = 0;

	build_vrr_infopacket_header_v1(signal, infopacket, &payload_size);
	build_vrr_infopacket_data_v1(vrr, infopacket, freesync_on_desktop);
	build_vrr_infopacket_checksum(&payload_size, infopacket);

	infopacket->valid = true;
}

static void build_vrr_infopacket_v2(enum signal_type signal,
		const struct mod_vrr_params *vrr,
		enum color_transfer_func app_tf,
		struct dc_info_packet *infopacket,
		bool freesync_on_desktop)
{
	unsigned int payload_size = 0;

	build_vrr_infopacket_header_v2(signal, infopacket, &payload_size);
	build_vrr_infopacket_data_v1(vrr, infopacket, freesync_on_desktop);

	build_vrr_infopacket_fs2_data(app_tf, infopacket);

	build_vrr_infopacket_checksum(&payload_size, infopacket);

	infopacket->valid = true;
}

static void build_vrr_infopacket_v3(enum signal_type signal,
		const struct mod_vrr_params *vrr,
		enum color_transfer_func app_tf,
		struct dc_info_packet *infopacket,
		bool freesync_on_desktop)
{
	unsigned int payload_size = 0;

	build_vrr_infopacket_header_v3(signal, infopacket, &payload_size);
	build_vrr_infopacket_data_v3(vrr, infopacket, freesync_on_desktop);

	build_vrr_infopacket_fs2_data(app_tf, infopacket);

	build_vrr_infopacket_checksum(&payload_size, infopacket);

	infopacket->valid = true;
}

static void build_vrr_infopacket_sdp_v1_3(enum vrr_packet_type packet_type,
										struct dc_info_packet *infopacket)
{
	uint8_t idx = 0, size = 0;

	size = ((packet_type == PACKET_TYPE_FS_V1) ? 0x08 :
			(packet_type == PACKET_TYPE_FS_V3) ? 0x10 :
												0x09);

	for (idx = infopacket->hb2; idx > 1; idx--) // Data Byte Count: 0x1B
		infopacket->sb[idx] = infopacket->sb[idx-1];

	infopacket->sb[1] = size;                         // Length
	infopacket->sb[0] = (infopacket->hb3 >> 2) & 0x3F;//Version
	infopacket->hb3   = (0x13 << 2);                  // Header,SDP 1.3
	infopacket->hb2   = 0x1D;
}

void mod_freesync_build_vrr_infopacket(struct mod_freesync *mod_freesync,
		const struct dc_stream_state *stream,
		const struct mod_vrr_params *vrr,
		enum vrr_packet_type packet_type,
		enum color_transfer_func app_tf,
		struct dc_info_packet *infopacket,
		bool pack_sdp_v1_3)
{
	/* SPD info packet for FreeSync
	 * VTEM info packet for HdmiVRR
	 * Check if Freesync is supported. Return if false. If true,
	 * set the corresponding bit in the info packet
	 */
	if (!vrr->send_info_frame)
		return;

	switch (packet_type) {
	case PACKET_TYPE_FS_V3:
		build_vrr_infopacket_v3(stream->signal, vrr, app_tf, infopacket, stream->freesync_on_desktop);
		break;
	case PACKET_TYPE_FS_V2:
		build_vrr_infopacket_v2(stream->signal, vrr, app_tf, infopacket, stream->freesync_on_desktop);
		break;
	case PACKET_TYPE_VRR:
	case PACKET_TYPE_FS_V1:
	default:
		build_vrr_infopacket_v1(stream->signal, vrr, infopacket, stream->freesync_on_desktop);
	}

	if (true == pack_sdp_v1_3 &&
		true == dc_is_dp_signal(stream->signal) &&
		packet_type != PACKET_TYPE_VRR &&
		packet_type != PACKET_TYPE_VTEM)
		build_vrr_infopacket_sdp_v1_3(packet_type, infopacket);
}

void mod_freesync_build_vrr_params(struct mod_freesync *mod_freesync,
		const struct dc_stream_state *stream,
		struct mod_freesync_config *in_config,
		struct mod_vrr_params *in_out_vrr)
{
	struct core_freesync *core_freesync = NULL;
	unsigned long long nominal_field_rate_in_uhz = 0;
	unsigned long long rounded_nominal_in_uhz = 0;
	unsigned int refresh_range = 0;
	unsigned long long min_refresh_in_uhz = 0;
	unsigned long long max_refresh_in_uhz = 0;
	unsigned long long min_hardware_refresh_in_uhz = 0;

	if (mod_freesync == NULL)
		return;

	core_freesync = MOD_FREESYNC_TO_CORE(mod_freesync);

	/* Calculate nominal field rate for stream */
	nominal_field_rate_in_uhz =
			mod_freesync_calc_nominal_field_rate(stream);

	if (stream->ctx->dc->caps.max_v_total != 0 && stream->timing.h_total != 0) {
		min_hardware_refresh_in_uhz = div64_u64((stream->timing.pix_clk_100hz * 100000000ULL),
			(stream->timing.h_total * stream->ctx->dc->caps.max_v_total));
	}
	/* Limit minimum refresh rate to what can be supported by hardware */
	min_refresh_in_uhz = min_hardware_refresh_in_uhz > in_config->min_refresh_in_uhz ?
		min_hardware_refresh_in_uhz : in_config->min_refresh_in_uhz;
	max_refresh_in_uhz = in_config->max_refresh_in_uhz;

	/* Full range may be larger than current video timing, so cap at nominal */
	if (max_refresh_in_uhz > nominal_field_rate_in_uhz)
		max_refresh_in_uhz = nominal_field_rate_in_uhz;

	/* Full range may be larger than current video timing, so cap at nominal */
	if (min_refresh_in_uhz > max_refresh_in_uhz)
		min_refresh_in_uhz = max_refresh_in_uhz;

	/* If a monitor reports exactly max refresh of 2x of min, enforce it on nominal */
	rounded_nominal_in_uhz =
			div_u64(nominal_field_rate_in_uhz + 50000, 100000) * 100000;
	if (in_config->max_refresh_in_uhz == (2 * in_config->min_refresh_in_uhz) &&
		in_config->max_refresh_in_uhz == rounded_nominal_in_uhz)
		min_refresh_in_uhz = div_u64(nominal_field_rate_in_uhz, 2);

	if (!vrr_settings_require_update(core_freesync,
			in_config, (unsigned int)min_refresh_in_uhz, (unsigned int)max_refresh_in_uhz,
			in_out_vrr))
		return;

	in_out_vrr->state = in_config->state;
	in_out_vrr->send_info_frame = in_config->vsif_supported;

	if (in_config->state == VRR_STATE_UNSUPPORTED) {
		in_out_vrr->state = VRR_STATE_UNSUPPORTED;
		in_out_vrr->supported = false;
		in_out_vrr->adjust.v_total_min = stream->timing.v_total;
		in_out_vrr->adjust.v_total_max = stream->timing.v_total;

		return;

	} else {
		in_out_vrr->min_refresh_in_uhz = (unsigned int)min_refresh_in_uhz;
		in_out_vrr->max_duration_in_us =
				calc_duration_in_us_from_refresh_in_uhz(
						(unsigned int)min_refresh_in_uhz);

		in_out_vrr->max_refresh_in_uhz = (unsigned int)max_refresh_in_uhz;
		in_out_vrr->min_duration_in_us =
				calc_duration_in_us_from_refresh_in_uhz(
						(unsigned int)max_refresh_in_uhz);

		if (in_config->state == VRR_STATE_ACTIVE_FIXED)
			in_out_vrr->fixed_refresh_in_uhz = in_config->fixed_refresh_in_uhz;
		else
			in_out_vrr->fixed_refresh_in_uhz = 0;

		refresh_range = div_u64(in_out_vrr->max_refresh_in_uhz + 500000, 1000000) -
				div_u64(in_out_vrr->min_refresh_in_uhz + 500000, 1000000);

		in_out_vrr->supported = true;
	}

	in_out_vrr->fixed.ramping_active = in_config->ramping;

	in_out_vrr->btr.btr_enabled = in_config->btr;

	if (in_out_vrr->max_refresh_in_uhz < (2 * in_out_vrr->min_refresh_in_uhz))
		in_out_vrr->btr.btr_enabled = false;
	else {
		in_out_vrr->btr.margin_in_us = in_out_vrr->max_duration_in_us -
				2 * in_out_vrr->min_duration_in_us;
		if (in_out_vrr->btr.margin_in_us > BTR_MAX_MARGIN)
			in_out_vrr->btr.margin_in_us = BTR_MAX_MARGIN;
	}

	in_out_vrr->btr.btr_active = false;
	in_out_vrr->btr.inserted_duration_in_us = 0;
	in_out_vrr->btr.frames_to_insert = 0;
	in_out_vrr->btr.frame_counter = 0;
	in_out_vrr->fixed.fixed_active = false;
	in_out_vrr->fixed.target_refresh_in_uhz = 0;

	in_out_vrr->btr.mid_point_in_us =
				(in_out_vrr->min_duration_in_us +
				 in_out_vrr->max_duration_in_us) / 2;

	if (in_out_vrr->state == VRR_STATE_UNSUPPORTED) {
		in_out_vrr->adjust.v_total_min = stream->timing.v_total;
		in_out_vrr->adjust.v_total_max = stream->timing.v_total;
	} else if (in_out_vrr->state == VRR_STATE_DISABLED) {
		in_out_vrr->adjust.v_total_min = stream->timing.v_total;
		in_out_vrr->adjust.v_total_max = stream->timing.v_total;
	} else if (in_out_vrr->state == VRR_STATE_INACTIVE) {
		in_out_vrr->adjust.v_total_min = stream->timing.v_total;
		in_out_vrr->adjust.v_total_max = stream->timing.v_total;
	} else if (in_out_vrr->state == VRR_STATE_ACTIVE_VARIABLE &&
			refresh_range >= MIN_REFRESH_RANGE) {

		in_out_vrr->adjust.v_total_min =
			mod_freesync_calc_v_total_from_refresh(stream,
				in_out_vrr->max_refresh_in_uhz);
		in_out_vrr->adjust.v_total_max =
			mod_freesync_calc_v_total_from_refresh(stream,
				in_out_vrr->min_refresh_in_uhz);
	} else if (in_out_vrr->state == VRR_STATE_ACTIVE_FIXED) {
		in_out_vrr->fixed.target_refresh_in_uhz =
				in_out_vrr->fixed_refresh_in_uhz;
		if (in_out_vrr->fixed.ramping_active &&
				in_out_vrr->fixed.fixed_active) {
			/* Do not update vtotals if ramping is already active
			 * in order to continue ramp from current refresh.
			 */
			in_out_vrr->fixed.fixed_active = true;
		} else {
			in_out_vrr->fixed.fixed_active = true;
			in_out_vrr->adjust.v_total_min =
				mod_freesync_calc_v_total_from_refresh(stream,
					in_out_vrr->fixed.target_refresh_in_uhz);
			in_out_vrr->adjust.v_total_max =
				in_out_vrr->adjust.v_total_min;
		}
	} else {
		in_out_vrr->state = VRR_STATE_INACTIVE;
		in_out_vrr->adjust.v_total_min = stream->timing.v_total;
		in_out_vrr->adjust.v_total_max = stream->timing.v_total;
	}

	in_out_vrr->adjust.allow_otg_v_count_halt = (in_config->state == VRR_STATE_ACTIVE_FIXED) ? true : false;
}

void mod_freesync_handle_preflip(struct mod_freesync *mod_freesync,
		const struct dc_plane_state *plane,
		const struct dc_stream_state *stream,
		unsigned int curr_time_stamp_in_us,
		struct mod_vrr_params *in_out_vrr)
{
	struct core_freesync *core_freesync = NULL;
	unsigned int last_render_time_in_us = 0;

	if (mod_freesync == NULL)
		return;

	core_freesync = MOD_FREESYNC_TO_CORE(mod_freesync);

	if (in_out_vrr->supported &&
			in_out_vrr->state == VRR_STATE_ACTIVE_VARIABLE) {

		last_render_time_in_us = curr_time_stamp_in_us -
				plane->time.prev_update_time_in_us;

		if (in_out_vrr->btr.btr_enabled) {
			apply_below_the_range(core_freesync,
					stream,
					last_render_time_in_us,
					in_out_vrr);
		} else {
			apply_fixed_refresh(core_freesync,
				stream,
				last_render_time_in_us,
				in_out_vrr);
		}

		determine_flip_interval_workaround_req(in_out_vrr,
				curr_time_stamp_in_us);

	}
}

void mod_freesync_handle_v_update(struct mod_freesync *mod_freesync,
		const struct dc_stream_state *stream,
		struct mod_vrr_params *in_out_vrr)
{
	struct core_freesync *core_freesync = NULL;
	unsigned int cur_timestamp_in_us;
	unsigned long long cur_tick;

	if ((mod_freesync == NULL) || (stream == NULL) || (in_out_vrr == NULL))
		return;

	core_freesync = MOD_FREESYNC_TO_CORE(mod_freesync);

	if (in_out_vrr->supported == false)
		return;

	cur_tick = dm_get_timestamp(core_freesync->dc->ctx);
	cur_timestamp_in_us = (unsigned int)
			div_u64(dm_get_elapse_time_in_ns(core_freesync->dc->ctx, cur_tick, 0), 1000);

	in_out_vrr->flip_interval.vsyncs_between_flip++;
	in_out_vrr->flip_interval.v_update_timestamp_in_us = cur_timestamp_in_us;

	if (in_out_vrr->state == VRR_STATE_ACTIVE_VARIABLE &&
			(in_out_vrr->flip_interval.flip_interval_workaround_active ||
			(!in_out_vrr->flip_interval.flip_interval_workaround_active &&
			in_out_vrr->flip_interval.program_flip_interval_workaround))) {
		// set freesync vmin vmax to nominal for workaround
		in_out_vrr->adjust.v_total_min =
			mod_freesync_calc_v_total_from_refresh(
			stream, in_out_vrr->max_refresh_in_uhz);
		in_out_vrr->adjust.v_total_max =
				in_out_vrr->adjust.v_total_min;
		in_out_vrr->flip_interval.program_flip_interval_workaround = false;
		in_out_vrr->flip_interval.do_flip_interval_workaround_cleanup = true;
		return;
	}

	if (in_out_vrr->state != VRR_STATE_ACTIVE_VARIABLE &&
			in_out_vrr->flip_interval.do_flip_interval_workaround_cleanup) {
		in_out_vrr->flip_interval.do_flip_interval_workaround_cleanup = false;
		in_out_vrr->flip_interval.flip_interval_detect_counter = 0;
		in_out_vrr->flip_interval.vsyncs_between_flip = 0;
		in_out_vrr->flip_interval.vsync_to_flip_in_us = 0;
	}

	/* Below the Range Logic */

	/* Only execute if in fullscreen mode */
	if (in_out_vrr->state == VRR_STATE_ACTIVE_VARIABLE &&
					in_out_vrr->btr.btr_active) {
		/* TODO: pass in flag for Pre-DCE12 ASIC
		 * in order for frame variable duration to take affect,
		 * it needs to be done one VSYNC early, which is at
		 * frameCounter == 1.
		 * For DCE12 and newer updates to V_TOTAL_MIN/MAX
		 * will take affect on current frame
		 */
		if (in_out_vrr->btr.frames_to_insert ==
				in_out_vrr->btr.frame_counter) {
			in_out_vrr->adjust.v_total_min =
				calc_v_total_from_duration(stream,
				in_out_vrr,
				in_out_vrr->btr.inserted_duration_in_us);
			in_out_vrr->adjust.v_total_max =
				in_out_vrr->adjust.v_total_min;
		}

		if (in_out_vrr->btr.frame_counter > 0)
			in_out_vrr->btr.frame_counter--;

		/* Restore FreeSync */
		if (in_out_vrr->btr.frame_counter == 0) {
			in_out_vrr->adjust.v_total_min =
				mod_freesync_calc_v_total_from_refresh(stream,
				in_out_vrr->max_refresh_in_uhz);
			in_out_vrr->adjust.v_total_max =
				mod_freesync_calc_v_total_from_refresh(stream,
				in_out_vrr->min_refresh_in_uhz);
		}
	}

	/* If in fullscreen freesync mode or in video, do not program
	 * static screen ramp values
	 */
	if (in_out_vrr->state == VRR_STATE_ACTIVE_VARIABLE)
		in_out_vrr->fixed.ramping_active = false;

	/* Gradual Static Screen Ramping Logic
	 * Execute if ramp is active and user enabled freesync static screen
	 */
	if (in_out_vrr->state == VRR_STATE_ACTIVE_FIXED &&
				in_out_vrr->fixed.ramping_active) {
		update_v_total_for_static_ramp(
				core_freesync, stream, in_out_vrr);
	}
}

void mod_freesync_get_settings(struct mod_freesync *mod_freesync,
		const struct mod_vrr_params *vrr,
		unsigned int *v_total_min, unsigned int *v_total_max,
		unsigned int *event_triggers,
		unsigned int *window_min, unsigned int *window_max,
		unsigned int *lfc_mid_point_in_us,
		unsigned int *inserted_frames,
		unsigned int *inserted_duration_in_us)
{
	if (mod_freesync == NULL)
		return;

	if (vrr->supported) {
		*v_total_min = vrr->adjust.v_total_min;
		*v_total_max = vrr->adjust.v_total_max;
		*event_triggers = 0;
		*lfc_mid_point_in_us = vrr->btr.mid_point_in_us;
		*inserted_frames = vrr->btr.frames_to_insert;
		*inserted_duration_in_us = vrr->btr.inserted_duration_in_us;
	}
}

unsigned long long mod_freesync_calc_nominal_field_rate(
			const struct dc_stream_state *stream)
{
	unsigned long long nominal_field_rate_in_uhz = 0;
	unsigned int total = stream->timing.h_total * stream->timing.v_total;

	/* Calculate nominal field rate for stream, rounded up to nearest integer */
	nominal_field_rate_in_uhz = stream->timing.pix_clk_100hz;
	nominal_field_rate_in_uhz *= 100000000ULL;

	nominal_field_rate_in_uhz =	div_u64(nominal_field_rate_in_uhz, total);

	return nominal_field_rate_in_uhz;
}

unsigned long long mod_freesync_calc_field_rate_from_timing(
		unsigned int vtotal, unsigned int htotal, unsigned int pix_clk)
{
	unsigned long long field_rate_in_uhz = 0;
	unsigned int total = htotal * vtotal;

	/* Calculate nominal field rate for stream, rounded up to nearest integer */
	field_rate_in_uhz = pix_clk;
	field_rate_in_uhz *= 1000000ULL;

	field_rate_in_uhz =	div_u64(field_rate_in_uhz, total);

	return field_rate_in_uhz;
}

bool mod_freesync_get_freesync_enabled(struct mod_vrr_params *pVrr)
{
	return (pVrr->state != VRR_STATE_UNSUPPORTED) && (pVrr->state != VRR_STATE_DISABLED);
}

bool mod_freesync_is_valid_range(uint32_t min_refresh_cap_in_uhz,
		uint32_t max_refresh_cap_in_uhz,
		uint32_t nominal_field_rate_in_uhz)
{

	/* Typically nominal refresh calculated can have some fractional part.
	 * Allow for some rounding error of actual video timing by taking floor
	 * of caps and request. Round the nominal refresh rate.
	 *
	 * Dividing will convert everything to units in Hz although input
	 * variable name is in uHz!
	 *
	 * Also note, this takes care of rounding error on the nominal refresh
	 * so by rounding error we only expect it to be off by a small amount,
	 * such as < 0.1 Hz. i.e. 143.9xxx or 144.1xxx.
	 *
	 * Example 1. Caps    Min = 40 Hz, Max = 144 Hz
	 *            Request Min = 40 Hz, Max = 144 Hz
	 *                    Nominal = 143.5x Hz rounded to 144 Hz
	 *            This function should allow this as valid request
	 *
	 * Example 2. Caps    Min = 40 Hz, Max = 144 Hz
	 *            Request Min = 40 Hz, Max = 144 Hz
	 *                    Nominal = 144.4x Hz rounded to 144 Hz
	 *            This function should allow this as valid request
	 *
	 * Example 3. Caps    Min = 40 Hz, Max = 144 Hz
	 *            Request Min = 40 Hz, Max = 144 Hz
	 *                    Nominal = 120.xx Hz rounded to 120 Hz
	 *            This function should return NOT valid since the requested
	 *            max is greater than current timing's nominal
	 *
	 * Example 4. Caps    Min = 40 Hz, Max = 120 Hz
	 *            Request Min = 40 Hz, Max = 120 Hz
	 *                    Nominal = 144.xx Hz rounded to 144 Hz
	 *            This function should return NOT valid since the nominal
	 *            is greater than the capability's max refresh
	 */
	nominal_field_rate_in_uhz =
			div_u64(nominal_field_rate_in_uhz + 500000, 1000000);
	min_refresh_cap_in_uhz /= 1000000;
	max_refresh_cap_in_uhz /= 1000000;

	/* Check nominal is within range */
	if (nominal_field_rate_in_uhz > max_refresh_cap_in_uhz ||
		nominal_field_rate_in_uhz < min_refresh_cap_in_uhz)
		return false;

	/* If nominal is less than max, limit the max allowed refresh rate */
	if (nominal_field_rate_in_uhz < max_refresh_cap_in_uhz)
		max_refresh_cap_in_uhz = nominal_field_rate_in_uhz;

	/* Check min is within range */
	if (min_refresh_cap_in_uhz > max_refresh_cap_in_uhz)
		return false;

	/* For variable range, check for at least 10 Hz range */
	if (nominal_field_rate_in_uhz - min_refresh_cap_in_uhz < 10)
		return false;

	return true;
}
