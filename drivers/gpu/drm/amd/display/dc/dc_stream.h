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

#ifndef DC_STREAM_H_
#define DC_STREAM_H_

#include "dc_types.h"
#include "grph_object_defs.h"

/*******************************************************************************
 * Stream Interfaces
 ******************************************************************************/

struct dc_stream_status {
	int primary_otg_inst;
	int stream_enc_inst;
	int plane_count;
	struct dc_plane_state *plane_states[MAX_SURFACE_NUM];

	/*
	 * link this stream passes through
	 */
	struct dc_link *link;
};

// TODO: References to this needs to be removed..
struct freesync_context {
	bool dummy;
};

struct dc_stream_state {
	struct dc_sink *sink;
	struct dc_crtc_timing timing;
	struct dc_crtc_timing_adjust adjust;
	struct dc_info_packet vrr_infopacket;
	struct dc_info_packet vsc_infopacket;

	struct rect src; /* composition area */
	struct rect dst; /* stream addressable area */

	// TODO: References to this needs to be removed..
	struct freesync_context freesync_ctx;

	struct audio_info audio_info;

	struct dc_info_packet hdr_static_metadata;
	PHYSICAL_ADDRESS_LOC dmdata_address;
	bool   use_dynamic_meta;

	struct dc_transfer_func *out_transfer_func;
	struct colorspace_transform gamut_remap_matrix;
	struct dc_csc_transform csc_color_matrix;

	enum dc_color_space output_color_space;
	enum dc_dither_option dither_option;

	enum view_3d_format view_format;

	bool ignore_msa_timing_param;

	unsigned long long periodic_fn_vsync_delta;

	/* TODO: custom INFO packets */
	/* TODO: ABM info (DMCU) */
	/* PSR info */
	unsigned char psr_version;
	/* TODO: CEA VIC */

	/* DMCU info */
	unsigned int abm_level;
	unsigned int bl_pwm_level;

	/* from core_stream struct */
	struct dc_context *ctx;

	/* used by DCP and FMT */
	struct bit_depth_reduction_params bit_depth_params;
	struct clamping_and_pixel_encoding_params clamping;

	int phy_pix_clk;
	enum signal_type signal;
	bool dpms_off;
	bool apply_edp_fast_boot_optimization;

	struct dc_stream_status status;

	struct dc_cursor_attributes cursor_attributes;
	struct dc_cursor_position cursor_position;
	uint32_t sdr_white_level; // for boosting (SDR) cursor in HDR mode

	/* from stream struct */
	struct kref refcount;

	struct crtc_trigger_info triggered_crtc_reset;

	/* Computed state bits */
	bool mode_changed : 1;

};

struct dc_stream_update {
	struct rect src;
	struct rect dst;
	struct dc_transfer_func *out_transfer_func;
	struct dc_info_packet *hdr_static_metadata;
	unsigned int *abm_level;

	unsigned long long *periodic_fn_vsync_delta;
	struct dc_crtc_timing_adjust *adjust;
	struct dc_info_packet *vrr_infopacket;
	struct dc_info_packet *vsc_infopacket;

	bool *dpms_off;

	struct colorspace_transform *gamut_remap;
	enum dc_color_space *output_color_space;

	struct dc_csc_transform *output_csc_transform;

};

bool dc_is_stream_unchanged(
	struct dc_stream_state *old_stream, struct dc_stream_state *stream);
bool dc_is_stream_scaling_unchanged(
	struct dc_stream_state *old_stream, struct dc_stream_state *stream);

/*
 * Set up surface attributes and associate to a stream
 * The surfaces parameter is an absolute set of all surface active for the stream.
 * If no surfaces are provided, the stream will be blanked; no memory read.
 * Any flip related attribute changes must be done through this interface.
 *
 * After this call:
 *   Surfaces attributes are programmed and configured to be composed into stream.
 *   This does not trigger a flip.  No surface address is programmed.
 */

void dc_commit_updates_for_stream(struct dc *dc,
		struct dc_surface_update *srf_updates,
		int surface_count,
		struct dc_stream_state *stream,
		struct dc_stream_update *stream_update,
		struct dc_plane_state **plane_states,
		struct dc_state *state);
/*
 * Log the current stream state.
 */
void dc_stream_log(const struct dc *dc, const struct dc_stream_state *stream);

uint8_t dc_get_current_stream_count(struct dc *dc);
struct dc_stream_state *dc_get_stream_at_index(struct dc *dc, uint8_t i);

/*
 * Return the current frame counter.
 */
uint32_t dc_stream_get_vblank_counter(const struct dc_stream_state *stream);

/* TODO: Return parsed values rather than direct register read
 * This has a dependency on the caller (amdgpu_display_get_crtc_scanoutpos)
 * being refactored properly to be dce-specific
 */
bool dc_stream_get_scanoutpos(const struct dc_stream_state *stream,
				  uint32_t *v_blank_start,
				  uint32_t *v_blank_end,
				  uint32_t *h_position,
				  uint32_t *v_position);

enum dc_status dc_add_stream_to_ctx(
			struct dc *dc,
		struct dc_state *new_ctx,
		struct dc_stream_state *stream);

enum dc_status dc_remove_stream_from_ctx(
		struct dc *dc,
			struct dc_state *new_ctx,
			struct dc_stream_state *stream);


bool dc_add_plane_to_context(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_plane_state *plane_state,
		struct dc_state *context);

bool dc_remove_plane_from_context(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_plane_state *plane_state,
		struct dc_state *context);

bool dc_rem_all_planes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_state *context);

bool dc_add_all_planes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *stream,
		struct dc_plane_state * const *plane_states,
		int plane_count,
		struct dc_state *context);

enum dc_status dc_validate_stream(struct dc *dc, struct dc_stream_state *stream);

/*
 * Set up streams and links associated to drive sinks
 * The streams parameter is an absolute set of all active streams.
 *
 * After this call:
 *   Phy, Encoder, Timing Generator are programmed and enabled.
 *   New streams are enabled with blank stream; no memory read.
 */
/*
 * Enable stereo when commit_streams is not required,
 * for example, frame alternate.
 */
bool dc_enable_stereo(
	struct dc *dc,
	struct dc_state *context,
	struct dc_stream_state *streams[],
	uint8_t stream_count);


enum surface_update_type dc_check_update_surfaces_for_stream(
		struct dc *dc,
		struct dc_surface_update *updates,
		int surface_count,
		struct dc_stream_update *stream_update,
		const struct dc_stream_status *stream_status);

/**
 * Create a new default stream for the requested sink
 */
struct dc_stream_state *dc_create_stream_for_sink(struct dc_sink *dc_sink);

void update_stream_signal(struct dc_stream_state *stream);

void dc_stream_retain(struct dc_stream_state *dc_stream);
void dc_stream_release(struct dc_stream_state *dc_stream);

struct dc_stream_status *dc_stream_get_status(
	struct dc_stream_state *dc_stream);

/*******************************************************************************
 * Cursor interfaces - To manages the cursor within a stream
 ******************************************************************************/
/* TODO: Deprecated once we switch to dc_set_cursor_position */
bool dc_stream_set_cursor_attributes(
	struct dc_stream_state *stream,
	const struct dc_cursor_attributes *attributes);

bool dc_stream_set_cursor_position(
	struct dc_stream_state *stream,
	const struct dc_cursor_position *position);


bool dc_stream_adjust_vmin_vmax(struct dc *dc,
				struct dc_stream_state *stream,
				struct dc_crtc_timing_adjust *adjust);

bool dc_stream_get_crtc_position(struct dc *dc,
				 struct dc_stream_state **stream,
				 int num_streams,
				 unsigned int *v_pos,
				 unsigned int *nom_v_pos);

bool dc_stream_configure_crc(struct dc *dc,
			     struct dc_stream_state *stream,
			     bool enable,
			     bool continuous);

bool dc_stream_get_crc(struct dc *dc,
		       struct dc_stream_state *stream,
		       uint32_t *r_cr,
		       uint32_t *g_y,
		       uint32_t *b_cb);

void dc_stream_set_static_screen_events(struct dc *dc,
					struct dc_stream_state **stream,
					int num_streams,
					const struct dc_static_screen_events *events);

void dc_stream_set_dither_option(struct dc_stream_state *stream,
				 enum dc_dither_option option);

bool dc_stream_set_gamut_remap(struct dc *dc,
			       const struct dc_stream_state *stream);

bool dc_stream_program_csc_matrix(struct dc *dc,
				  struct dc_stream_state *stream);

bool dc_stream_get_crtc_position(struct dc *dc,
				 struct dc_stream_state **stream,
				 int num_streams,
				 unsigned int *v_pos,
				 unsigned int *nom_v_pos);

#endif /* DC_STREAM_H_ */
