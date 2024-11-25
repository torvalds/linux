// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML_TOP_DISPLAY_CFG_TYPES_H__
#define __DML_TOP_DISPLAY_CFG_TYPES_H__

#include "dml2_external_lib_deps.h"

#define DML2_MAX_PLANES 8
#define DML2_MAX_DCN_PIPES 8
#define DML2_MAX_MCACHES 8 // assume plane is going to be supported by a max of 8 mcaches
#define DML2_MAX_WRITEBACK 3

enum dml2_swizzle_mode {
	dml2_sw_linear, // SW_LINEAR accepts 256 byte aligned pitch and also 128 byte aligned pitch if DCC is not enabled
	dml2_sw_256b_2d,
	dml2_sw_4kb_2d,
	dml2_sw_64kb_2d,
	dml2_sw_256kb_2d,

	dml2_gfx11_sw_linear,
	dml2_gfx11_sw_64kb_d,
	dml2_gfx11_sw_64kb_d_t,
	dml2_gfx11_sw_64kb_d_x,
	dml2_gfx11_sw_64kb_r_x,
	dml2_gfx11_sw_256kb_d_x,
	dml2_gfx11_sw_256kb_r_x,

};

enum dml2_source_format_class {
	dml2_444_8 = 0,
	dml2_444_16 = 1,
	dml2_444_32 = 2,
	dml2_444_64 = 3,
	dml2_420_8 = 4,
	dml2_420_10 = 5,
	dml2_420_12 = 6,
	dml2_rgbe_alpha = 9,
	dml2_rgbe = 10,
	dml2_mono_8 = 11,
	dml2_mono_16 = 12,
	dml2_422_planar_8 = 13,
	dml2_422_planar_10 = 14,
	dml2_422_planar_12 = 15,
	dml2_422_packed_8 = 16,
	dml2_422_packed_10 = 17,
	dml2_422_packed_12 = 18
};

enum dml2_rotation_angle {
	dml2_rotation_0 = 0,
	dml2_rotation_90 = 1,
	dml2_rotation_180 = 2,
	dml2_rotation_270 = 3
};

enum dml2_output_format_class {
	dml2_444 = 0,
	dml2_s422 = 1,
	dml2_n422 = 2,
	dml2_420 = 3
};

enum dml2_output_encoder_class {
	dml2_dp = 0,
	dml2_edp = 1,
	dml2_dp2p0 = 2,
	dml2_hdmi = 3,
	dml2_hdmifrl = 4,
	dml2_none = 5
};

enum dml2_output_link_dp_rate {
	dml2_dp_rate_na = 0,
	dml2_dp_rate_hbr = 1,
	dml2_dp_rate_hbr2 = 2,
	dml2_dp_rate_hbr3 = 3,
	dml2_dp_rate_uhbr10 = 4,
	dml2_dp_rate_uhbr13p5 = 5,
	dml2_dp_rate_uhbr20 = 6
};

enum dml2_uclk_pstate_change_strategy {
	dml2_uclk_pstate_change_strategy_auto = 0,
	dml2_uclk_pstate_change_strategy_force_vactive = 1,
	dml2_uclk_pstate_change_strategy_force_vblank = 2,
	dml2_uclk_pstate_change_strategy_force_drr = 3,
	dml2_uclk_pstate_change_strategy_force_mall_svp = 4,
	dml2_uclk_pstate_change_strategy_force_mall_full_frame = 5,
};

enum dml2_svp_mode_override {
	dml2_svp_mode_override_auto = 0,
	dml2_svp_mode_override_main_pipe = 1,
	dml2_svp_mode_override_phantom_pipe = 2, //does not need to be defined explicitly, main overrides result in implicit phantom additions
	dml2_svp_mode_override_phantom_pipe_no_data_return = 3,
	dml2_svp_mode_override_imall = 4
};

enum dml2_refresh_from_mall_mode_override {
	dml2_refresh_from_mall_mode_override_auto = 0,
	dml2_refresh_from_mall_mode_override_force_disable = 1,
	dml2_refresh_from_mall_mode_override_force_enable = 2
};

enum dml2_odm_mode {
	dml2_odm_mode_auto = 0,
	dml2_odm_mode_bypass,
	dml2_odm_mode_combine_2to1,
	dml2_odm_mode_combine_3to1,
	dml2_odm_mode_combine_4to1,
	dml2_odm_mode_split_1to2,
	dml2_odm_mode_mso_1to2,
	dml2_odm_mode_mso_1to4
};

enum dml2_scaling_transform {
	dml2_scaling_transform_explicit = 0,
	dml2_scaling_transform_fullscreen,
	dml2_scaling_transform_aspect_ratio,
	dml2_scaling_transform_centered
};

enum dml2_dsc_enable_option {
	dml2_dsc_disable = 0,
	dml2_dsc_enable = 1,
	dml2_dsc_enable_if_necessary = 2
};

enum dml2_tdlut_addressing_mode {
	dml2_tdlut_sw_linear = 0,
	dml2_tdlut_simple_linear = 1
};

enum dml2_tdlut_width_mode {
	dml2_tdlut_width_17_cube = 0,
	dml2_tdlut_width_33_cube = 1
};

enum dml2_twait_budgeting_setting {
	dml2_twait_budgeting_setting_ignore = 0,// Ignore this budget in twait

	dml2_twait_budgeting_setting_if_needed,         // Budget for it only if needed
											//(i.e. UCLK/FCLK DPM cannot be supported in active)

	dml2_twait_budgeting_setting_try,	   // Budget for it as long as there is an SoC state that
											// can support it
};

struct dml2_get_cursor_dlg_reg{
	unsigned int cursor_x_position;
	unsigned int cursor_hotspot_x;
	unsigned int cursor_primary_offset;
	unsigned int cursor_secondary_offset;
	bool cursor_stereo_en;
	bool cursor_2x_magnify;
	double hratio;
	double pixel_rate_mhz;
	double dlg_refclk_mhz;
};

/// @brief Surface Parameters
struct dml2_surface_cfg {
	enum dml2_swizzle_mode tiling;

	struct {
		unsigned long pitch;
		unsigned long width;
		unsigned long height;
	} plane0;


	struct {
		unsigned long pitch;
		unsigned long width;
		unsigned long height;
	} plane1;

	struct {
		bool enable;
		struct {
			unsigned long pitch;
		} plane0;
		struct {
			unsigned long pitch;
		} plane1;

		struct {
			double dcc_rate_plane0;
			double dcc_rate_plane1;
			double fraction_of_zero_size_request_plane0;
			double fraction_of_zero_size_request_plane1;
		} informative;
	} dcc;
};


struct dml2_composition_cfg {
	enum dml2_rotation_angle rotation_angle;
	bool mirrored;
	enum dml2_scaling_transform scaling_transform;
	bool rect_out_height_spans_vactive;

	struct {
		bool stationary;
		struct {
			unsigned long width;
			unsigned long height;
			unsigned long x_start;
			unsigned long y_start;
		} plane0;

		struct {
			unsigned long width;
			unsigned long height;
			unsigned long x_start;
			unsigned long y_start;
		} plane1;
	} viewport;

	struct {
		bool enabled;
		struct {
			double h_ratio;
			double v_ratio;
			unsigned int h_taps;
			unsigned int v_taps;
		} plane0;

		struct {
			double h_ratio;
			double v_ratio;
			unsigned int h_taps;
			unsigned int v_taps;
		} plane1;

		unsigned long rect_out_width;
	} scaler_info;
};

struct dml2_timing_cfg {
	unsigned long h_total;
	unsigned long v_total;
	unsigned long h_blank_end;
	unsigned long v_blank_end;
	unsigned long h_front_porch;
	unsigned long v_front_porch;
	unsigned long h_sync_width;
	unsigned long pixel_clock_khz;
	unsigned long h_active;
	unsigned long v_active;
	unsigned int bpc; //FIXME: review with Jun
	struct {
		enum dml2_dsc_enable_option enable;
		unsigned int dsc_compressed_bpp_x16;
		struct {
			// for dv to specify num dsc slices to use
			unsigned int num_slices;
		} overrides;
	} dsc;
	bool interlaced;
	struct {
		/* static */
		bool enabled;
		unsigned long min_refresh_uhz;
		unsigned int max_instant_vtotal_delta;
		/* dynamic */
		bool disallowed;
		bool drr_active_variable;
		bool drr_active_fixed;
	} drr_config;
	unsigned long vblank_nom;
};

struct dml2_link_output_cfg {
	enum dml2_output_format_class output_format;
	enum dml2_output_encoder_class output_encoder;
	unsigned int output_dp_lane_count;
	enum dml2_output_link_dp_rate output_dp_link_rate;
	unsigned long audio_sample_rate;
	unsigned long audio_sample_layout;
	bool output_disabled; // The stream does not go to a backend for output to a physical
						  //connector (e.g. writeback only, phantom pipe) goes to writeback
	bool validate_output; // Do not validate the link configuration for this display stream.
};

struct dml2_writeback_info {
	enum dml2_source_format_class pixel_format;
	unsigned long input_width;
	unsigned long input_height;
	unsigned long output_width;
	unsigned long output_height;
	unsigned long v_taps;
	unsigned long h_taps;
	unsigned long v_taps_chroma;
	unsigned long h_taps_chroma;
	double h_ratio;
	double v_ratio;
};

struct dml2_writeback_cfg {
	unsigned int active_writebacks_per_stream;
	struct dml2_writeback_info writeback_stream[DML2_MAX_WRITEBACK];
};

struct dml2_plane_parameters {
	unsigned int stream_index; // Identifies which plane will be composed

	enum dml2_source_format_class pixel_format;
	/*
	 * The surface and composition structures use
	 * the terms plane0 and plane1.  These planes
	 * are expected to hold the following data based
	 * on the pixel format.
	 *
	 * RGB or YUV Non-Planar Types:
	 *  dml2_444_8
	 *	dml2_444_16
	 *	dml2_444_32
	 *	dml2_444_64
	 *	dml2_rgbe
	 *
	 * plane0 = argb or rgbe
	 * plane1 = not used
	 *
	 * YUV Planar-Types:
	 *	dml2_420_8
	 *	dml2_420_10
	 *	dml2_420_12
	 *
	 * plane0 = luma
	 * plane1 = chroma
	 *
	 * RGB Planar Types:
	 *	dml2_rgbe_alpha
	 *
	 * plane0 = rgbe
	 * plane1 = alpha
	 *
	 * Mono Non-Planar Types:
	 *	dml2_mono_8
	 *	dml2_mono_16
	 *
	 * plane0 = luma
	 * plane1 = not used
	 */

	struct dml2_surface_cfg surface;
	struct dml2_composition_cfg composition;

	struct {
		bool enable;
		unsigned long lines_before_active_required;
		unsigned long transmitted_bytes;
	} dynamic_meta_data;

	struct {
		unsigned int num_cursors;
		unsigned long cursor_width;
		unsigned long cursor_bpp;
	} cursor;

	// For TDLUT, SW would assume TDLUT is setup and enable all the time and
	// budget for worst case addressing/width mode
	struct {
		bool setup_for_tdlut;
		enum dml2_tdlut_addressing_mode tdlut_addressing_mode;
		enum dml2_tdlut_width_mode tdlut_width_mode;
		bool tdlut_mpc_width_flag;
	} tdlut;

	bool immediate_flip;

	struct {
		// Logical overrides to power management policies (usually)
		enum dml2_uclk_pstate_change_strategy uclk_pstate_change_strategy;
		enum dml2_refresh_from_mall_mode_override refresh_from_mall;
		unsigned int det_size_override_kb;
		unsigned int mpcc_combine_factor;

		// reserved_vblank_time_ns is the minimum time to reserve in vblank for Twait
		// The actual reserved vblank time used for the corresponding stream in mode_programming would be at least as much as this per-plane override.
		long reserved_vblank_time_ns;
		unsigned int max_vactive_det_fill_delay_us; // 0 = no reserved time, +ve = explicit max delay
		unsigned int gpuvm_min_page_size_kbytes;

		enum dml2_svp_mode_override legacy_svp_config; //TODO remove in favor of svp_config

		struct {
			// HW specific overrides, there's almost no reason to mess with these
			// generally used for debugging or simulation
			bool force_one_row_for_frame;
			struct {
				bool enable;
				bool value;
			} force_pte_buffer_mode;
			double dppclk_mhz;
		} hw;
	} overrides;
};

struct dml2_stream_parameters {
	struct dml2_timing_cfg timing;
	struct dml2_link_output_cfg output;
	struct dml2_writeback_cfg writeback;

	struct {
		enum dml2_odm_mode odm_mode;
		bool disable_dynamic_odm;
		bool disable_subvp;
		int minimum_vblank_idle_requirement_us;
		bool minimize_active_latency_hiding;

		struct {
			struct {
				enum dml2_twait_budgeting_setting uclk_pstate;
				enum dml2_twait_budgeting_setting fclk_pstate;
				enum dml2_twait_budgeting_setting stutter_enter_exit;
			} twait_budgeting;
		} hw;
	} overrides;
};

struct dml2_display_cfg {
	bool gpuvm_enable;
	bool hostvm_enable;

	// Allocate DET proportionally between streams based on pixel rate
	// and then allocate proportionally between planes.
	bool minimize_det_reallocation;

	unsigned int gpuvm_max_page_table_levels;
	unsigned int hostvm_max_non_cached_page_table_levels;

	struct dml2_plane_parameters plane_descriptors[DML2_MAX_PLANES];
	struct dml2_stream_parameters stream_descriptors[DML2_MAX_PLANES];

	unsigned int num_planes;
	unsigned int num_streams;

	struct {
		struct {
			// HW specific overrides, there's almost no reason to mess with these
			// generally used for debugging or simulation
			struct {
				bool enable;
				bool value;
			} force_unbounded_requesting;

			struct {
				bool enable;
				bool value;
			} force_nom_det_size_kbytes;
			bool mode_support_check_disable;
			bool mcache_admissibility_check_disable;
			bool surface_viewport_size_check_disable;
			double dlg_ref_clk_mhz;
			double dispclk_mhz;
			double dcfclk_mhz;
			bool optimize_tdlut_scheduling; // TBD: for DV, will set this to 1, to ensure tdlut schedule is calculated based on address/width mode
		} hw;

		struct {
			bool uclk_pstate_change_disable;
			bool fclk_pstate_change_disable;
			bool g6_temp_read_pstate_disable;
			bool g7_ppt_pstate_disable;
		} power_management;

		bool enhanced_prefetch_schedule_acceleration;
		bool dcc_programming_assumes_scan_direction_unknown;
		bool synchronize_timings;
		bool synchronize_ddr_displays_for_uclk_pstate_change;
		bool max_outstanding_when_urgent_expected_disable;
		bool enable_subvp_implicit_pmo; //enables PMO to switch pipe uclk strategy to subvp, and generate phantom programming
		unsigned int best_effort_min_active_latency_hiding_us;
		bool all_streams_blanked;
	} overrides;
};

struct dml2_pipe_configuration_descriptor {
	struct {
		unsigned int viewport_x_start;
		unsigned int viewport_width;
	} plane0;

	struct {
		unsigned int viewport_x_start;
		unsigned int viewport_width;
	} plane1;

	bool plane1_enabled;
	bool imall_enabled;
};

struct dml2_plane_mcache_configuration_descriptor {
	const struct dml2_plane_parameters *plane_descriptor;
	const struct dml2_mcache_surface_allocation *mcache_allocation;

	struct dml2_pipe_configuration_descriptor pipe_configurations[DML2_MAX_DCN_PIPES];
	char num_pipes;
};

#endif
