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

#ifndef __DAL_TIMING_GENERATOR_TYPES_H__
#define __DAL_TIMING_GENERATOR_TYPES_H__

#include "hw_shared.h"

struct dc_bios;

/* Contains CRTC vertical/horizontal pixel counters */
struct crtc_position {
	int32_t vertical_count;
	int32_t horizontal_count;
	int32_t nominal_vcount;
};

struct dcp_gsl_params {
	int gsl_group;
	int gsl_master;
};

struct gsl_params {
	int gsl0_en;
	int gsl1_en;
	int gsl2_en;
	int gsl_master_en;
	int gsl_master_mode;
	int master_update_lock_gsl_en;
	int gsl_window_start_x;
	int gsl_window_end_x;
	int gsl_window_start_y;
	int gsl_window_end_y;
};

/* define the structure of Dynamic Refresh Mode */
struct drr_params {
	uint32_t vertical_total_min;
	uint32_t vertical_total_max;
	uint32_t vertical_total_mid;
	uint32_t vertical_total_mid_frame_num;
	bool immediate_flip;
};

#define LEFT_EYE_3D_PRIMARY_SURFACE 1
#define RIGHT_EYE_3D_PRIMARY_SURFACE 0

enum crtc_state {
	CRTC_STATE_VBLANK = 0,
	CRTC_STATE_VACTIVE
};

struct vupdate_keepout_params {
	int start_offset;
	int end_offset;
	int enable;
};

struct crtc_stereo_flags {
	uint8_t PROGRAM_STEREO         : 1;
	uint8_t PROGRAM_POLARITY       : 1;
	uint8_t RIGHT_EYE_POLARITY     : 1;
	uint8_t FRAME_PACKED           : 1;
	uint8_t DISABLE_STEREO_DP_SYNC : 1;
};

enum crc_selection {
	/* Order must match values expected by hardware */
	UNION_WINDOW_A_B = 0,
	UNION_WINDOW_A_NOT_B,
	UNION_WINDOW_NOT_A_B,
	UNION_WINDOW_NOT_A_NOT_B,
	INTERSECT_WINDOW_A_B,
	INTERSECT_WINDOW_A_NOT_B,
	INTERSECT_WINDOW_NOT_A_B,
	INTERSECT_WINDOW_NOT_A_NOT_B,
};

enum otg_out_mux_dest {
	OUT_MUX_DIO = 0,
	OUT_MUX_HPO_DP = 2,
};

enum h_timing_div_mode {
	H_TIMING_NO_DIV,
	H_TIMING_DIV_BY2,
	H_TIMING_RESERVED,
	H_TIMING_DIV_BY4,
};

enum timing_synchronization_type {
	NOT_SYNCHRONIZABLE,
	TIMING_SYNCHRONIZABLE,
	VBLANK_SYNCHRONIZABLE
};

struct crc_params {
	/* Regions used to calculate CRC*/
	uint16_t windowa_x_start;
	uint16_t windowa_x_end;
	uint16_t windowa_y_start;
	uint16_t windowa_y_end;

	uint16_t windowb_x_start;
	uint16_t windowb_x_end;
	uint16_t windowb_y_start;
	uint16_t windowb_y_end;

	enum crc_selection selection;

	uint8_t dsc_mode;
	uint8_t odm_mode;

	bool continuous_mode;
	bool enable;
};

/**
 * struct timing_generator - Entry point to Output Timing Generator feature.
 */
struct timing_generator {
	/**
	 * @funcs: Timing generator control functions
	 */
	const struct timing_generator_funcs *funcs;
	struct dc_bios *bp;
	struct dc_context *ctx;
	int inst;
};

struct dc_crtc_timing;

struct drr_params;

/**
 * struct timing_generator_funcs - Control timing generator on a given device.
 */
struct timing_generator_funcs {
	bool (*validate_timing)(struct timing_generator *tg,
							const struct dc_crtc_timing *timing);
	void (*program_timing)(struct timing_generator *tg,
							const struct dc_crtc_timing *timing,
							int vready_offset,
							int vstartup_start,
							int vupdate_offset,
							int vupdate_width,
							const enum signal_type signal,
							bool use_vbios
	);
	void (*setup_vertical_interrupt0)(
			struct timing_generator *optc,
			uint32_t start_line,
			uint32_t end_line);
	void (*setup_vertical_interrupt1)(
			struct timing_generator *optc,
			uint32_t start_line);
	void (*setup_vertical_interrupt2)(
			struct timing_generator *optc,
			uint32_t start_line);

	bool (*enable_crtc)(struct timing_generator *tg);
	bool (*disable_crtc)(struct timing_generator *tg);
#ifdef CONFIG_DRM_AMD_DC_DCN
	void (*phantom_crtc_post_enable)(struct timing_generator *tg);
#endif
	void (*disable_phantom_crtc)(struct timing_generator *tg);
	bool (*immediate_disable_crtc)(struct timing_generator *tg);
	bool (*is_counter_moving)(struct timing_generator *tg);
	void (*get_position)(struct timing_generator *tg,
				struct crtc_position *position);

	uint32_t (*get_frame_count)(struct timing_generator *tg);
	void (*get_scanoutpos)(
		struct timing_generator *tg,
		uint32_t *v_blank_start,
		uint32_t *v_blank_end,
		uint32_t *h_position,
		uint32_t *v_position);
	bool (*get_otg_active_size)(struct timing_generator *optc,
			uint32_t *otg_active_width,
			uint32_t *otg_active_height);
	bool (*is_matching_timing)(struct timing_generator *tg,
			const struct dc_crtc_timing *otg_timing);
	void (*set_early_control)(struct timing_generator *tg,
							   uint32_t early_cntl);
	void (*wait_for_state)(struct timing_generator *tg,
							enum crtc_state state);
	void (*set_blank)(struct timing_generator *tg,
					bool enable_blanking);
	bool (*is_blanked)(struct timing_generator *tg);
	void (*set_overscan_blank_color) (struct timing_generator *tg, const struct tg_color *color);
	void (*set_blank_color)(struct timing_generator *tg, const struct tg_color *color);
	void (*set_colors)(struct timing_generator *tg,
						const struct tg_color *blank_color,
						const struct tg_color *overscan_color);

	void (*disable_vga)(struct timing_generator *tg);
	bool (*did_triggered_reset_occur)(struct timing_generator *tg);
	void (*setup_global_swap_lock)(struct timing_generator *tg,
							const struct dcp_gsl_params *gsl_params);
	void (*unlock)(struct timing_generator *tg);
	void (*lock)(struct timing_generator *tg);
	void (*lock_doublebuffer_disable)(struct timing_generator *tg);
	void (*lock_doublebuffer_enable)(struct timing_generator *tg);
	void(*triplebuffer_unlock)(struct timing_generator *tg);
	void(*triplebuffer_lock)(struct timing_generator *tg);
	void (*enable_reset_trigger)(struct timing_generator *tg,
				     int source_tg_inst);
	void (*enable_crtc_reset)(struct timing_generator *tg,
				  int source_tg_inst,
				  struct crtc_trigger_info *crtc_tp);
	void (*disable_reset_trigger)(struct timing_generator *tg);
	void (*tear_down_global_swap_lock)(struct timing_generator *tg);
	void (*enable_advanced_request)(struct timing_generator *tg,
					bool enable, const struct dc_crtc_timing *timing);
	void (*set_drr)(struct timing_generator *tg, const struct drr_params *params);
	void (*set_vtotal_min_max)(struct timing_generator *optc, int vtotal_min, int vtotal_max);
	void (*get_last_used_drr_vtotal)(struct timing_generator *optc, uint32_t *refresh_rate);
	void (*set_static_screen_control)(struct timing_generator *tg,
						uint32_t event_triggers,
						uint32_t num_frames);
	void (*set_test_pattern)(
		struct timing_generator *tg,
		enum controller_dp_test_pattern test_pattern,
		enum dc_color_depth color_depth);

	bool (*arm_vert_intr)(struct timing_generator *tg, uint8_t width);

	void (*program_global_sync)(struct timing_generator *tg,
			int vready_offset,
			int vstartup_start,
			int vupdate_offset,
			int vupdate_width);
	void (*enable_optc_clock)(struct timing_generator *tg, bool enable);
	void (*program_stereo)(struct timing_generator *tg,
		const struct dc_crtc_timing *timing, struct crtc_stereo_flags *flags);
	bool (*is_stereo_left_eye)(struct timing_generator *tg);

	void (*set_blank_data_double_buffer)(struct timing_generator *tg, bool enable);

	void (*tg_init)(struct timing_generator *tg);
	bool (*is_tg_enabled)(struct timing_generator *tg);
	bool (*is_optc_underflow_occurred)(struct timing_generator *tg);
	void (*clear_optc_underflow)(struct timing_generator *tg);

	void (*set_dwb_source)(struct timing_generator *optc,
		uint32_t dwb_pipe_inst);

	void (*get_optc_source)(struct timing_generator *optc,
			uint32_t *num_of_input_segments,
			uint32_t *seg0_src_sel,
			uint32_t *seg1_src_sel);

	/**
	 * Configure CRCs for the given timing generator. Return false if TG is
	 * not on.
	 */
	bool (*configure_crc)(struct timing_generator *tg,
			       const struct crc_params *params);

	/**
	 * @get_crc: Get CRCs for the given timing generator. Return false if
	 * CRCs are not enabled (via configure_crc).
	 */
	bool (*get_crc)(struct timing_generator *tg,
			uint32_t *r_cr, uint32_t *g_y, uint32_t *b_cb);

	void (*program_manual_trigger)(struct timing_generator *optc);
	void (*setup_manual_trigger)(struct timing_generator *optc);
	bool (*get_hw_timing)(struct timing_generator *optc,
			struct dc_crtc_timing *hw_crtc_timing);

	void (*set_vtg_params)(struct timing_generator *optc,
			const struct dc_crtc_timing *dc_crtc_timing, bool program_fp2);

	void (*set_dsc_config)(struct timing_generator *optc,
			       enum optc_dsc_mode dsc_mode,
			       uint32_t dsc_bytes_per_pixel,
			       uint32_t dsc_slice_width);
	void (*get_dsc_status)(struct timing_generator *optc,
					uint32_t *dsc_mode);
	void (*set_odm_bypass)(struct timing_generator *optc, const struct dc_crtc_timing *dc_crtc_timing);

	/**
	 * @set_odm_combine: Set up the ODM block to read from the correct
	 * OPP(s) and turn on/off ODM memory.
	 */
	void (*set_odm_combine)(struct timing_generator *optc, int *opp_id, int opp_cnt,
			struct dc_crtc_timing *timing);
	void (*set_h_timing_div_manual_mode)(struct timing_generator *optc, bool manual_mode);
	void (*set_gsl)(struct timing_generator *optc, const struct gsl_params *params);
	void (*set_gsl_source_select)(struct timing_generator *optc,
			int group_idx,
			uint32_t gsl_ready_signal);
	void (*set_out_mux)(struct timing_generator *tg, enum otg_out_mux_dest dest);
	void (*set_drr_trigger_window)(struct timing_generator *optc,
			uint32_t window_start, uint32_t window_end);
	void (*set_vtotal_change_limit)(struct timing_generator *optc,
			uint32_t limit);
	void (*align_vblanks)(struct timing_generator *master_optc,
			struct timing_generator *slave_optc,
			uint32_t master_pixel_clock_100Hz,
			uint32_t slave_pixel_clock_100Hz,
			uint8_t master_clock_divider,
			uint8_t slave_clock_divider);
	bool (*validate_vmin_vmax)(struct timing_generator *optc,
			int vmin, int vmax);
	bool (*validate_vtotal_change_limit)(struct timing_generator *optc,
			uint32_t vtotal_change_limit);

	void (*init_odm)(struct timing_generator *tg);
};

#endif
