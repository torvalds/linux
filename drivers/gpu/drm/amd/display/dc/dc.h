/*
 * Copyright 2012-2023 Advanced Micro Devices, Inc.
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

#ifndef DC_INTERFACE_H_
#define DC_INTERFACE_H_

#include "dc_types.h"
#include "grph_object_defs.h"
#include "logger_types.h"
#include "hdcp_msg_types.h"
#include "gpio_types.h"
#include "link_service_types.h"
#include "grph_object_ctrl_defs.h"
#include <inc/hw/opp.h>

#include "inc/hw_sequencer.h"
#include "inc/compressor.h"
#include "inc/hw/dmcu.h"
#include "dml/display_mode_lib.h"

/* forward declaration */
struct aux_payload;
struct set_config_cmd_payload;
struct dmub_notification;

#define DC_VER "3.2.239"

#define MAX_SURFACES 3
#define MAX_PLANES 6
#define MAX_STREAMS 6
#define MIN_VIEWPORT_SIZE 12
#define MAX_NUM_EDP 2

/* Display Core Interfaces */
struct dc_versions {
	const char *dc_ver;
	struct dmcu_version dmcu_version;
};

enum dp_protocol_version {
	DP_VERSION_1_4 = 0,
	DP_VERSION_2_1,
	DP_VERSION_UNKNOWN,
};

enum dc_plane_type {
	DC_PLANE_TYPE_INVALID,
	DC_PLANE_TYPE_DCE_RGB,
	DC_PLANE_TYPE_DCE_UNDERLAY,
	DC_PLANE_TYPE_DCN_UNIVERSAL,
};

// Sizes defined as multiples of 64KB
enum det_size {
	DET_SIZE_DEFAULT = 0,
	DET_SIZE_192KB = 3,
	DET_SIZE_256KB = 4,
	DET_SIZE_320KB = 5,
	DET_SIZE_384KB = 6
};


struct dc_plane_cap {
	enum dc_plane_type type;
	uint32_t per_pixel_alpha : 1;
	struct {
		uint32_t argb8888 : 1;
		uint32_t nv12 : 1;
		uint32_t fp16 : 1;
		uint32_t p010 : 1;
		uint32_t ayuv : 1;
	} pixel_format_support;
	// max upscaling factor x1000
	// upscaling factors are always >= 1
	// for example, 1080p -> 8K is 4.0, or 4000 raw value
	struct {
		uint32_t argb8888;
		uint32_t nv12;
		uint32_t fp16;
	} max_upscale_factor;
	// max downscale factor x1000
	// downscale factors are always <= 1
	// for example, 8K -> 1080p is 0.25, or 250 raw value
	struct {
		uint32_t argb8888;
		uint32_t nv12;
		uint32_t fp16;
	} max_downscale_factor;
	// minimal width/height
	uint32_t min_width;
	uint32_t min_height;
};

/**
 * DOC: color-management-caps
 *
 * **Color management caps (DPP and MPC)**
 *
 * Modules/color calculates various color operations which are translated to
 * abstracted HW. DCE 5-12 had almost no important changes, but starting with
 * DCN1, every new generation comes with fairly major differences in color
 * pipeline. Therefore, we abstract color pipe capabilities so modules/DM can
 * decide mapping to HW block based on logical capabilities.
 */

/**
 * struct rom_curve_caps - predefined transfer function caps for degamma and regamma
 * @srgb: RGB color space transfer func
 * @bt2020: BT.2020 transfer func
 * @gamma2_2: standard gamma
 * @pq: perceptual quantizer transfer function
 * @hlg: hybrid log–gamma transfer function
 */
struct rom_curve_caps {
	uint16_t srgb : 1;
	uint16_t bt2020 : 1;
	uint16_t gamma2_2 : 1;
	uint16_t pq : 1;
	uint16_t hlg : 1;
};

/**
 * struct dpp_color_caps - color pipeline capabilities for display pipe and
 * plane blocks
 *
 * @dcn_arch: all DCE generations treated the same
 * @input_lut_shared: shared with DGAM. Input LUT is different than most LUTs,
 * just plain 256-entry lookup
 * @icsc: input color space conversion
 * @dgam_ram: programmable degamma LUT
 * @post_csc: post color space conversion, before gamut remap
 * @gamma_corr: degamma correction
 * @hw_3d_lut: 3D LUT support. It implies a shaper LUT before. It may be shared
 * with MPC by setting mpc:shared_3d_lut flag
 * @ogam_ram: programmable out/blend gamma LUT
 * @ocsc: output color space conversion
 * @dgam_rom_for_yuv: pre-defined degamma LUT for YUV planes
 * @dgam_rom_caps: pre-definied curve caps for degamma 1D LUT
 * @ogam_rom_caps: pre-definied curve caps for regamma 1D LUT
 *
 * Note: hdr_mult and gamut remap (CTM) are always available in DPP (in that order)
 */
struct dpp_color_caps {
	uint16_t dcn_arch : 1;
	uint16_t input_lut_shared : 1;
	uint16_t icsc : 1;
	uint16_t dgam_ram : 1;
	uint16_t post_csc : 1;
	uint16_t gamma_corr : 1;
	uint16_t hw_3d_lut : 1;
	uint16_t ogam_ram : 1;
	uint16_t ocsc : 1;
	uint16_t dgam_rom_for_yuv : 1;
	struct rom_curve_caps dgam_rom_caps;
	struct rom_curve_caps ogam_rom_caps;
};

/**
 * struct mpc_color_caps - color pipeline capabilities for multiple pipe and
 * plane combined blocks
 *
 * @gamut_remap: color transformation matrix
 * @ogam_ram: programmable out gamma LUT
 * @ocsc: output color space conversion matrix
 * @num_3dluts: MPC 3D LUT; always assumes a preceding shaper LUT
 * @shared_3d_lut: shared 3D LUT flag. Can be either DPP or MPC, but single
 * instance
 * @ogam_rom_caps: pre-definied curve caps for regamma 1D LUT
 */
struct mpc_color_caps {
	uint16_t gamut_remap : 1;
	uint16_t ogam_ram : 1;
	uint16_t ocsc : 1;
	uint16_t num_3dluts : 3;
	uint16_t shared_3d_lut:1;
	struct rom_curve_caps ogam_rom_caps;
};

/**
 * struct dc_color_caps - color pipes capabilities for DPP and MPC hw blocks
 * @dpp: color pipes caps for DPP
 * @mpc: color pipes caps for MPC
 */
struct dc_color_caps {
	struct dpp_color_caps dpp;
	struct mpc_color_caps mpc;
};

struct dc_dmub_caps {
	bool psr;
	bool mclk_sw;
	bool subvp_psr;
	bool gecc_enable;
};

struct dc_caps {
	uint32_t max_streams;
	uint32_t max_links;
	uint32_t max_audios;
	uint32_t max_slave_planes;
	uint32_t max_slave_yuv_planes;
	uint32_t max_slave_rgb_planes;
	uint32_t max_planes;
	uint32_t max_downscale_ratio;
	uint32_t i2c_speed_in_khz;
	uint32_t i2c_speed_in_khz_hdcp;
	uint32_t dmdata_alloc_size;
	unsigned int max_cursor_size;
	unsigned int max_video_width;
	unsigned int min_horizontal_blanking_period;
	int linear_pitch_alignment;
	bool dcc_const_color;
	bool dynamic_audio;
	bool is_apu;
	bool dual_link_dvi;
	bool post_blend_color_processing;
	bool force_dp_tps4_for_cp2520;
	bool disable_dp_clk_share;
	bool psp_setup_panel_mode;
	bool extended_aux_timeout_support;
	bool dmcub_support;
	bool zstate_support;
	uint32_t num_of_internal_disp;
	enum dp_protocol_version max_dp_protocol_version;
	unsigned int mall_size_per_mem_channel;
	unsigned int mall_size_total;
	unsigned int cursor_cache_size;
	struct dc_plane_cap planes[MAX_PLANES];
	struct dc_color_caps color;
	struct dc_dmub_caps dmub_caps;
	bool dp_hpo;
	bool dp_hdmi21_pcon_support;
	bool edp_dsc_support;
	bool vbios_lttpr_aware;
	bool vbios_lttpr_enable;
	uint32_t max_otg_num;
	uint32_t max_cab_allocation_bytes;
	uint32_t cache_line_size;
	uint32_t cache_num_ways;
	uint16_t subvp_fw_processing_delay_us;
	uint8_t subvp_drr_max_vblank_margin_us;
	uint16_t subvp_prefetch_end_to_mall_start_us;
	uint8_t subvp_swath_height_margin_lines; // subvp start line must be aligned to 2 x swath height
	uint16_t subvp_pstate_allow_width_us;
	uint16_t subvp_vertical_int_margin_us;
	bool seamless_odm;
	uint32_t max_v_total;
	uint8_t subvp_drr_vblank_start_margin_us;
};

struct dc_bug_wa {
	bool no_connect_phy_config;
	bool dedcn20_305_wa;
	bool skip_clock_update;
	bool lt_early_cr_pattern;
	struct {
		uint8_t uclk : 1;
		uint8_t fclk : 1;
		uint8_t dcfclk : 1;
		uint8_t dcfclk_ds: 1;
	} clock_update_disable_mask;
};
struct dc_dcc_surface_param {
	struct dc_size surface_size;
	enum surface_pixel_format format;
	enum swizzle_mode_values swizzle_mode;
	enum dc_scan_direction scan;
};

struct dc_dcc_setting {
	unsigned int max_compressed_blk_size;
	unsigned int max_uncompressed_blk_size;
	bool independent_64b_blks;
	//These bitfields to be used starting with DCN
	struct {
		uint32_t dcc_256_64_64 : 1;//available in ASICs before DCN (the worst compression case)
		uint32_t dcc_128_128_uncontrained : 1;  //available in ASICs before DCN
		uint32_t dcc_256_128_128 : 1;		//available starting with DCN
		uint32_t dcc_256_256_unconstrained : 1;  //available in ASICs before DCN (the best compression case)
	} dcc_controls;
};

struct dc_surface_dcc_cap {
	union {
		struct {
			struct dc_dcc_setting rgb;
		} grph;

		struct {
			struct dc_dcc_setting luma;
			struct dc_dcc_setting chroma;
		} video;
	};

	bool capable;
	bool const_color_support;
};

struct dc_static_screen_params {
	struct {
		bool force_trigger;
		bool cursor_update;
		bool surface_update;
		bool overlay_update;
	} triggers;
	unsigned int num_frames;
};


/* Surface update type is used by dc_update_surfaces_and_stream
 * The update type is determined at the very beginning of the function based
 * on parameters passed in and decides how much programming (or updating) is
 * going to be done during the call.
 *
 * UPDATE_TYPE_FAST is used for really fast updates that do not require much
 * logical calculations or hardware register programming. This update MUST be
 * ISR safe on windows. Currently fast update will only be used to flip surface
 * address.
 *
 * UPDATE_TYPE_MED is used for slower updates which require significant hw
 * re-programming however do not affect bandwidth consumption or clock
 * requirements. At present, this is the level at which front end updates
 * that do not require us to run bw_calcs happen. These are in/out transfer func
 * updates, viewport offset changes, recout size changes and pixel depth changes.
 * This update can be done at ISR, but we want to minimize how often this happens.
 *
 * UPDATE_TYPE_FULL is slow. Really slow. This requires us to recalculate our
 * bandwidth and clocks, possibly rearrange some pipes and reprogram anything front
 * end related. Any time viewport dimensions, recout dimensions, scaling ratios or
 * gamma need to be adjusted or pipe needs to be turned on (or disconnected) we do
 * a full update. This cannot be done at ISR level and should be a rare event.
 * Unless someone is stress testing mpo enter/exit, playing with colour or adjusting
 * underscan we don't expect to see this call at all.
 */

enum surface_update_type {
	UPDATE_TYPE_FAST, /* super fast, safe to execute in isr */
	UPDATE_TYPE_MED,  /* ISR safe, most of programming needed, no bw/clk change*/
	UPDATE_TYPE_FULL, /* may need to shuffle resources */
};

/* Forward declaration*/
struct dc;
struct dc_plane_state;
struct dc_state;


struct dc_cap_funcs {
	bool (*get_dcc_compression_cap)(const struct dc *dc,
			const struct dc_dcc_surface_param *input,
			struct dc_surface_dcc_cap *output);
};

struct link_training_settings;

union allow_lttpr_non_transparent_mode {
	struct {
		bool DP1_4A : 1;
		bool DP2_0 : 1;
	} bits;
	unsigned char raw;
};

/* Structure to hold configuration flags set by dm at dc creation. */
struct dc_config {
	bool gpu_vm_support;
	bool disable_disp_pll_sharing;
	bool fbc_support;
	bool disable_fractional_pwm;
	bool allow_seamless_boot_optimization;
	bool seamless_boot_edp_requested;
	bool edp_not_connected;
	bool edp_no_power_sequencing;
	bool force_enum_edp;
	bool forced_clocks;
	union allow_lttpr_non_transparent_mode allow_lttpr_non_transparent_mode;
	bool multi_mon_pp_mclk_switch;
	bool disable_dmcu;
	bool enable_4to1MPC;
	bool enable_windowed_mpo_odm;
	bool forceHBR2CP2520; // Used for switching between test patterns TPS4 and CP2520
	uint32_t allow_edp_hotplug_detection;
	bool clamp_min_dcfclk;
	uint64_t vblank_alignment_dto_params;
	uint8_t  vblank_alignment_max_frame_time_diff;
	bool is_asymmetric_memory;
	bool is_single_rank_dimm;
	bool is_vmin_only_asic;
	bool use_pipe_ctx_sync_logic;
	bool ignore_dpref_ss;
	bool enable_mipi_converter_optimization;
	bool use_default_clock_table;
	bool force_bios_enable_lttpr;
	uint8_t force_bios_fixed_vs;
	int sdpif_request_limit_words_per_umc;
	bool use_old_fixed_vs_sequence;
	bool dc_mode_clk_limit_support;
};

enum visual_confirm {
	VISUAL_CONFIRM_DISABLE = 0,
	VISUAL_CONFIRM_SURFACE = 1,
	VISUAL_CONFIRM_HDR = 2,
	VISUAL_CONFIRM_MPCTREE = 4,
	VISUAL_CONFIRM_PSR = 5,
	VISUAL_CONFIRM_SWAPCHAIN = 6,
	VISUAL_CONFIRM_FAMS = 7,
	VISUAL_CONFIRM_SWIZZLE = 9,
	VISUAL_CONFIRM_SUBVP = 14,
	VISUAL_CONFIRM_MCLK_SWITCH = 16,
};

enum dc_psr_power_opts {
	psr_power_opt_invalid = 0x0,
	psr_power_opt_smu_opt_static_screen = 0x1,
	psr_power_opt_z10_static_screen = 0x10,
	psr_power_opt_ds_disable_allow = 0x100,
};

enum dml_hostvm_override_opts {
	DML_HOSTVM_NO_OVERRIDE = 0x0,
	DML_HOSTVM_OVERRIDE_FALSE = 0x1,
	DML_HOSTVM_OVERRIDE_TRUE = 0x2,
};

enum dcc_option {
	DCC_ENABLE = 0,
	DCC_DISABLE = 1,
	DCC_HALF_REQ_DISALBE = 2,
};

/**
 * enum pipe_split_policy - Pipe split strategy supported by DCN
 *
 * This enum is used to define the pipe split policy supported by DCN. By
 * default, DC favors MPC_SPLIT_DYNAMIC.
 */
enum pipe_split_policy {
	/**
	 * @MPC_SPLIT_DYNAMIC: DC will automatically decide how to split the
	 * pipe in order to bring the best trade-off between performance and
	 * power consumption. This is the recommended option.
	 */
	MPC_SPLIT_DYNAMIC = 0,

	/**
	 * @MPC_SPLIT_AVOID: Avoid pipe split, which means that DC will not
	 * try any sort of split optimization.
	 */
	MPC_SPLIT_AVOID = 1,

	/**
	 * @MPC_SPLIT_AVOID_MULT_DISP: With this option, DC will only try to
	 * optimize the pipe utilization when using a single display; if the
	 * user connects to a second display, DC will avoid pipe split.
	 */
	MPC_SPLIT_AVOID_MULT_DISP = 2,
};

enum wm_report_mode {
	WM_REPORT_DEFAULT = 0,
	WM_REPORT_OVERRIDE = 1,
};
enum dtm_pstate{
	dtm_level_p0 = 0,/*highest voltage*/
	dtm_level_p1,
	dtm_level_p2,
	dtm_level_p3,
	dtm_level_p4,/*when active_display_count = 0*/
};

enum dcn_pwr_state {
	DCN_PWR_STATE_UNKNOWN = -1,
	DCN_PWR_STATE_MISSION_MODE = 0,
	DCN_PWR_STATE_LOW_POWER = 3,
};

enum dcn_zstate_support_state {
	DCN_ZSTATE_SUPPORT_UNKNOWN,
	DCN_ZSTATE_SUPPORT_ALLOW,
	DCN_ZSTATE_SUPPORT_ALLOW_Z8_ONLY,
	DCN_ZSTATE_SUPPORT_ALLOW_Z8_Z10_ONLY,
	DCN_ZSTATE_SUPPORT_ALLOW_Z10_ONLY,
	DCN_ZSTATE_SUPPORT_DISALLOW,
};

/**
 * struct dc_clocks - DC pipe clocks
 *
 * For any clocks that may differ per pipe only the max is stored in this
 * structure
 */
struct dc_clocks {
	int dispclk_khz;
	int actual_dispclk_khz;
	int dppclk_khz;
	int actual_dppclk_khz;
	int disp_dpp_voltage_level_khz;
	int dcfclk_khz;
	int socclk_khz;
	int dcfclk_deep_sleep_khz;
	int fclk_khz;
	int phyclk_khz;
	int dramclk_khz;
	bool p_state_change_support;
	enum dcn_zstate_support_state zstate_support;
	bool dtbclk_en;
	int ref_dtbclk_khz;
	bool fclk_p_state_change_support;
	enum dcn_pwr_state pwr_state;
	/*
	 * Elements below are not compared for the purposes of
	 * optimization required
	 */
	bool prev_p_state_change_support;
	bool fclk_prev_p_state_change_support;
	int num_ways;

	/*
	 * @fw_based_mclk_switching
	 *
	 * DC has a mechanism that leverage the variable refresh rate to switch
	 * memory clock in cases that we have a large latency to achieve the
	 * memory clock change and a short vblank window. DC has some
	 * requirements to enable this feature, and this field describes if the
	 * system support or not such a feature.
	 */
	bool fw_based_mclk_switching;
	bool fw_based_mclk_switching_shut_down;
	int prev_num_ways;
	enum dtm_pstate dtm_level;
	int max_supported_dppclk_khz;
	int max_supported_dispclk_khz;
	int bw_dppclk_khz; /*a copy of dppclk_khz*/
	int bw_dispclk_khz;
};

struct dc_bw_validation_profile {
	bool enable;

	unsigned long long total_ticks;
	unsigned long long voltage_level_ticks;
	unsigned long long watermark_ticks;
	unsigned long long rq_dlg_ticks;

	unsigned long long total_count;
	unsigned long long skip_fast_count;
	unsigned long long skip_pass_count;
	unsigned long long skip_fail_count;
};

#define BW_VAL_TRACE_SETUP() \
		unsigned long long end_tick = 0; \
		unsigned long long voltage_level_tick = 0; \
		unsigned long long watermark_tick = 0; \
		unsigned long long start_tick = dc->debug.bw_val_profile.enable ? \
				dm_get_timestamp(dc->ctx) : 0

#define BW_VAL_TRACE_COUNT() \
		if (dc->debug.bw_val_profile.enable) \
			dc->debug.bw_val_profile.total_count++

#define BW_VAL_TRACE_SKIP(status) \
		if (dc->debug.bw_val_profile.enable) { \
			if (!voltage_level_tick) \
				voltage_level_tick = dm_get_timestamp(dc->ctx); \
			dc->debug.bw_val_profile.skip_ ## status ## _count++; \
		}

#define BW_VAL_TRACE_END_VOLTAGE_LEVEL() \
		if (dc->debug.bw_val_profile.enable) \
			voltage_level_tick = dm_get_timestamp(dc->ctx)

#define BW_VAL_TRACE_END_WATERMARKS() \
		if (dc->debug.bw_val_profile.enable) \
			watermark_tick = dm_get_timestamp(dc->ctx)

#define BW_VAL_TRACE_FINISH() \
		if (dc->debug.bw_val_profile.enable) { \
			end_tick = dm_get_timestamp(dc->ctx); \
			dc->debug.bw_val_profile.total_ticks += end_tick - start_tick; \
			dc->debug.bw_val_profile.voltage_level_ticks += voltage_level_tick - start_tick; \
			if (watermark_tick) { \
				dc->debug.bw_val_profile.watermark_ticks += watermark_tick - voltage_level_tick; \
				dc->debug.bw_val_profile.rq_dlg_ticks += end_tick - watermark_tick; \
			} \
		}

union mem_low_power_enable_options {
	struct {
		bool vga: 1;
		bool i2c: 1;
		bool dmcu: 1;
		bool dscl: 1;
		bool cm: 1;
		bool mpc: 1;
		bool optc: 1;
		bool vpg: 1;
		bool afmt: 1;
	} bits;
	uint32_t u32All;
};

union root_clock_optimization_options {
	struct {
		bool dpp: 1;
		bool dsc: 1;
		bool hdmistream: 1;
		bool hdmichar: 1;
		bool dpstream: 1;
		bool symclk32_se: 1;
		bool symclk32_le: 1;
		bool symclk_fe: 1;
		bool physymclk: 1;
		bool dpiasymclk: 1;
		uint32_t reserved: 22;
	} bits;
	uint32_t u32All;
};

union dpia_debug_options {
	struct {
		uint32_t disable_dpia:1; /* bit 0 */
		uint32_t force_non_lttpr:1; /* bit 1 */
		uint32_t extend_aux_rd_interval:1; /* bit 2 */
		uint32_t disable_mst_dsc_work_around:1; /* bit 3 */
		uint32_t enable_force_tbt3_work_around:1; /* bit 4 */
		uint32_t reserved:27;
	} bits;
	uint32_t raw;
};

/* AUX wake work around options
 * 0: enable/disable work around
 * 1: use default timeout LINK_AUX_WAKE_TIMEOUT_MS
 * 15-2: reserved
 * 31-16: timeout in ms
 */
union aux_wake_wa_options {
	struct {
		uint32_t enable_wa : 1;
		uint32_t use_default_timeout : 1;
		uint32_t rsvd: 14;
		uint32_t timeout_ms : 16;
	} bits;
	uint32_t raw;
};

struct dc_debug_data {
	uint32_t ltFailCount;
	uint32_t i2cErrorCount;
	uint32_t auxErrorCount;
};

struct dc_phy_addr_space_config {
	struct {
		uint64_t start_addr;
		uint64_t end_addr;
		uint64_t fb_top;
		uint64_t fb_offset;
		uint64_t fb_base;
		uint64_t agp_top;
		uint64_t agp_bot;
		uint64_t agp_base;
	} system_aperture;

	struct {
		uint64_t page_table_start_addr;
		uint64_t page_table_end_addr;
		uint64_t page_table_base_addr;
		bool base_addr_is_mc_addr;
	} gart_config;

	bool valid;
	bool is_hvm_enabled;
	uint64_t page_table_default_page_addr;
};

struct dc_virtual_addr_space_config {
	uint64_t	page_table_base_addr;
	uint64_t	page_table_start_addr;
	uint64_t	page_table_end_addr;
	uint32_t	page_table_block_size_in_bytes;
	uint8_t		page_table_depth; // 1 = 1 level, 2 = 2 level, etc.  0 = invalid
};

struct dc_bounding_box_overrides {
	int sr_exit_time_ns;
	int sr_enter_plus_exit_time_ns;
	int sr_exit_z8_time_ns;
	int sr_enter_plus_exit_z8_time_ns;
	int urgent_latency_ns;
	int percent_of_ideal_drambw;
	int dram_clock_change_latency_ns;
	int dummy_clock_change_latency_ns;
	int fclk_clock_change_latency_ns;
	/* This forces a hard min on the DCFCLK we use
	 * for DML.  Unlike the debug option for forcing
	 * DCFCLK, this override affects watermark calculations
	 */
	int min_dcfclk_mhz;
};

struct dc_state;
struct resource_pool;
struct dce_hwseq;
struct link_service;

/**
 * struct dc_debug_options - DC debug struct
 *
 * This struct provides a simple mechanism for developers to change some
 * configurations, enable/disable features, and activate extra debug options.
 * This can be very handy to narrow down whether some specific feature is
 * causing an issue or not.
 */
struct dc_debug_options {
	bool native422_support;
	bool disable_dsc;
	enum visual_confirm visual_confirm;
	int visual_confirm_rect_height;

	bool sanity_checks;
	bool max_disp_clk;
	bool surface_trace;
	bool timing_trace;
	bool clock_trace;
	bool validation_trace;
	bool bandwidth_calcs_trace;
	int max_downscale_src_width;

	/* stutter efficiency related */
	bool disable_stutter;
	bool use_max_lb;
	enum dcc_option disable_dcc;

	/**
	 * @pipe_split_policy: Define which pipe split policy is used by the
	 * display core.
	 */
	enum pipe_split_policy pipe_split_policy;
	bool force_single_disp_pipe_split;
	bool voltage_align_fclk;
	bool disable_min_fclk;

	bool disable_dfs_bypass;
	bool disable_dpp_power_gate;
	bool disable_hubp_power_gate;
	bool disable_dsc_power_gate;
	int dsc_min_slice_height_override;
	int dsc_bpp_increment_div;
	bool disable_pplib_wm_range;
	enum wm_report_mode pplib_wm_report_mode;
	unsigned int min_disp_clk_khz;
	unsigned int min_dpp_clk_khz;
	unsigned int min_dram_clk_khz;
	int sr_exit_time_dpm0_ns;
	int sr_enter_plus_exit_time_dpm0_ns;
	int sr_exit_time_ns;
	int sr_enter_plus_exit_time_ns;
	int sr_exit_z8_time_ns;
	int sr_enter_plus_exit_z8_time_ns;
	int urgent_latency_ns;
	uint32_t underflow_assert_delay_us;
	int percent_of_ideal_drambw;
	int dram_clock_change_latency_ns;
	bool optimized_watermark;
	int always_scale;
	bool disable_pplib_clock_request;
	bool disable_clock_gate;
	bool disable_mem_low_power;
	bool pstate_enabled;
	bool disable_dmcu;
	bool force_abm_enable;
	bool disable_stereo_support;
	bool vsr_support;
	bool performance_trace;
	bool az_endpoint_mute_only;
	bool always_use_regamma;
	bool recovery_enabled;
	bool avoid_vbios_exec_table;
	bool scl_reset_length10;
	bool hdmi20_disable;
	bool skip_detection_link_training;
	uint32_t edid_read_retry_times;
	unsigned int force_odm_combine; //bit vector based on otg inst
	unsigned int seamless_boot_odm_combine;
	unsigned int force_odm_combine_4to1; //bit vector based on otg inst
	int minimum_z8_residency_time;
	bool disable_z9_mpc;
	unsigned int force_fclk_khz;
	bool enable_tri_buf;
	bool dmub_offload_enabled;
	bool dmcub_emulation;
	bool disable_idle_power_optimizations;
	unsigned int mall_size_override;
	unsigned int mall_additional_timer_percent;
	bool mall_error_as_fatal;
	bool dmub_command_table; /* for testing only */
	struct dc_bw_validation_profile bw_val_profile;
	bool disable_fec;
	bool disable_48mhz_pwrdwn;
	/* This forces a hard min on the DCFCLK requested to SMU/PP
	 * watermarks are not affected.
	 */
	unsigned int force_min_dcfclk_mhz;
	int dwb_fi_phase;
	bool disable_timing_sync;
	bool cm_in_bypass;
	int force_clock_mode;/*every mode change.*/

	bool disable_dram_clock_change_vactive_support;
	bool validate_dml_output;
	bool enable_dmcub_surface_flip;
	bool usbc_combo_phy_reset_wa;
	bool enable_dram_clock_change_one_display_vactive;
	/* TODO - remove once tested */
	bool legacy_dp2_lt;
	bool set_mst_en_for_sst;
	bool disable_uhbr;
	bool force_dp2_lt_fallback_method;
	bool ignore_cable_id;
	union mem_low_power_enable_options enable_mem_low_power;
	union root_clock_optimization_options root_clock_optimization;
	bool hpo_optimization;
	bool force_vblank_alignment;

	/* Enable dmub aux for legacy ddc */
	bool enable_dmub_aux_for_legacy_ddc;
	bool disable_fams;
	/* FEC/PSR1 sequence enable delay in 100us */
	uint8_t fec_enable_delay_in100us;
	bool enable_driver_sequence_debug;
	enum det_size crb_alloc_policy;
	int crb_alloc_policy_min_disp_count;
	bool disable_z10;
	bool enable_z9_disable_interface;
	bool psr_skip_crtc_disable;
	union dpia_debug_options dpia_debug;
	bool disable_fixed_vs_aux_timeout_wa;
	bool force_disable_subvp;
	bool force_subvp_mclk_switch;
	bool allow_sw_cursor_fallback;
	unsigned int force_subvp_num_ways;
	unsigned int force_mall_ss_num_ways;
	bool alloc_extra_way_for_cursor;
	uint32_t subvp_extra_lines;
	bool force_usr_allow;
	/* uses value at boot and disables switch */
	bool disable_dtb_ref_clk_switch;
	bool extended_blank_optimization;
	union aux_wake_wa_options aux_wake_wa;
	uint32_t mst_start_top_delay;
	uint8_t psr_power_use_phy_fsm;
	enum dml_hostvm_override_opts dml_hostvm_override;
	bool dml_disallow_alternate_prefetch_modes;
	bool use_legacy_soc_bb_mechanism;
	bool exit_idle_opt_for_cursor_updates;
	bool enable_single_display_2to1_odm_policy;
	bool enable_double_buffered_dsc_pg_support;
	bool enable_dp_dig_pixel_rate_div_policy;
	enum lttpr_mode lttpr_mode_override;
	unsigned int dsc_delay_factor_wa_x1000;
	unsigned int min_prefetch_in_strobe_ns;
	bool disable_unbounded_requesting;
	bool dig_fifo_off_in_blank;
	bool temp_mst_deallocation_sequence;
	bool override_dispclk_programming;
	bool disable_fpo_optimizations;
	bool support_eDP1_5;
	uint32_t fpo_vactive_margin_us;
	bool disable_fpo_vactive;
	bool disable_boot_optimizations;
	bool override_odm_optimization;
	bool minimize_dispclk_using_odm;
	bool disable_subvp_high_refresh;
	bool disable_dp_plus_plus_wa;
	uint32_t fpo_vactive_min_active_margin_us;
	uint32_t fpo_vactive_max_blank_us;
	bool enable_legacy_fast_update;
	bool disable_dc_mode_overwrite;
};

struct gpu_info_soc_bounding_box_v1_0;
struct dc {
	struct dc_debug_options debug;
	struct dc_versions versions;
	struct dc_caps caps;
	struct dc_cap_funcs cap_funcs;
	struct dc_config config;
	struct dc_bounding_box_overrides bb_overrides;
	struct dc_bug_wa work_arounds;
	struct dc_context *ctx;
	struct dc_phy_addr_space_config vm_pa_config;

	uint8_t link_count;
	struct dc_link *links[MAX_PIPES * 2];
	struct link_service *link_srv;

	struct dc_state *current_state;
	struct resource_pool *res_pool;

	struct clk_mgr *clk_mgr;

	/* Display Engine Clock levels */
	struct dm_pp_clock_levels sclk_lvls;

	/* Inputs into BW and WM calculations. */
	struct bw_calcs_dceip *bw_dceip;
	struct bw_calcs_vbios *bw_vbios;
	struct dcn_soc_bounding_box *dcn_soc;
	struct dcn_ip_params *dcn_ip;
	struct display_mode_lib dml;

	/* HW functions */
	struct hw_sequencer_funcs hwss;
	struct dce_hwseq *hwseq;

	/* Require to optimize clocks and bandwidth for added/removed planes */
	bool optimized_required;
	bool wm_optimized_required;
	bool idle_optimizations_allowed;
	bool enable_c20_dtm_b0;

	/* Require to maintain clocks and bandwidth for UEFI enabled HW */

	/* FBC compressor */
	struct compressor *fbc_compressor;

	struct dc_debug_data debug_data;
	struct dpcd_vendor_signature vendor_signature;

	const char *build_id;
	struct vm_helper *vm_helper;

	uint32_t *dcn_reg_offsets;
	uint32_t *nbio_reg_offsets;

	/* Scratch memory */
	struct {
		struct {
			/*
			 * For matching clock_limits table in driver with table
			 * from PMFW.
			 */
			struct _vcs_dpi_voltage_scaling_st clock_limits[DC__VOLTAGE_STATES];
		} update_bw_bounding_box;
	} scratch;
};

enum frame_buffer_mode {
	FRAME_BUFFER_MODE_LOCAL_ONLY = 0,
	FRAME_BUFFER_MODE_ZFB_ONLY,
	FRAME_BUFFER_MODE_MIXED_ZFB_AND_LOCAL,
} ;

struct dchub_init_data {
	int64_t zfb_phys_addr_base;
	int64_t zfb_mc_base_addr;
	uint64_t zfb_size_in_byte;
	enum frame_buffer_mode fb_mode;
	bool dchub_initialzied;
	bool dchub_info_valid;
};

struct dc_init_data {
	struct hw_asic_id asic_id;
	void *driver; /* ctx */
	struct cgs_device *cgs_device;
	struct dc_bounding_box_overrides bb_overrides;

	int num_virtual_links;
	/*
	 * If 'vbios_override' not NULL, it will be called instead
	 * of the real VBIOS. Intended use is Diagnostics on FPGA.
	 */
	struct dc_bios *vbios_override;
	enum dce_environment dce_environment;

	struct dmub_offload_funcs *dmub_if;
	struct dc_reg_helper_state *dmub_offload;

	struct dc_config flags;
	uint64_t log_mask;

	struct dpcd_vendor_signature vendor_signature;
	bool force_smu_not_present;
	/*
	 * IP offset for run time initializaion of register addresses
	 *
	 * DCN3.5+ will fail dc_create() if these fields are null for them. They are
	 * applicable starting with DCN32/321 and are not used for ASICs upstreamed
	 * before them.
	 */
	uint32_t *dcn_reg_offsets;
	uint32_t *nbio_reg_offsets;
};

struct dc_callback_init {
	struct cp_psp cp_psp;
};

struct dc *dc_create(const struct dc_init_data *init_params);
void dc_hardware_init(struct dc *dc);

int dc_get_vmid_use_vector(struct dc *dc);
void dc_setup_vm_context(struct dc *dc, struct dc_virtual_addr_space_config *va_config, int vmid);
/* Returns the number of vmids supported */
int dc_setup_system_context(struct dc *dc, struct dc_phy_addr_space_config *pa_config);
void dc_init_callbacks(struct dc *dc,
		const struct dc_callback_init *init_params);
void dc_deinit_callbacks(struct dc *dc);
void dc_destroy(struct dc **dc);

/* Surface Interfaces */

enum {
	TRANSFER_FUNC_POINTS = 1025
};

struct dc_hdr_static_metadata {
	/* display chromaticities and white point in units of 0.00001 */
	unsigned int chromaticity_green_x;
	unsigned int chromaticity_green_y;
	unsigned int chromaticity_blue_x;
	unsigned int chromaticity_blue_y;
	unsigned int chromaticity_red_x;
	unsigned int chromaticity_red_y;
	unsigned int chromaticity_white_point_x;
	unsigned int chromaticity_white_point_y;

	uint32_t min_luminance;
	uint32_t max_luminance;
	uint32_t maximum_content_light_level;
	uint32_t maximum_frame_average_light_level;
};

enum dc_transfer_func_type {
	TF_TYPE_PREDEFINED,
	TF_TYPE_DISTRIBUTED_POINTS,
	TF_TYPE_BYPASS,
	TF_TYPE_HWPWL
};

struct dc_transfer_func_distributed_points {
	struct fixed31_32 red[TRANSFER_FUNC_POINTS];
	struct fixed31_32 green[TRANSFER_FUNC_POINTS];
	struct fixed31_32 blue[TRANSFER_FUNC_POINTS];

	uint16_t end_exponent;
	uint16_t x_point_at_y1_red;
	uint16_t x_point_at_y1_green;
	uint16_t x_point_at_y1_blue;
};

enum dc_transfer_func_predefined {
	TRANSFER_FUNCTION_SRGB,
	TRANSFER_FUNCTION_BT709,
	TRANSFER_FUNCTION_PQ,
	TRANSFER_FUNCTION_LINEAR,
	TRANSFER_FUNCTION_UNITY,
	TRANSFER_FUNCTION_HLG,
	TRANSFER_FUNCTION_HLG12,
	TRANSFER_FUNCTION_GAMMA22,
	TRANSFER_FUNCTION_GAMMA24,
	TRANSFER_FUNCTION_GAMMA26
};


struct dc_transfer_func {
	struct kref refcount;
	enum dc_transfer_func_type type;
	enum dc_transfer_func_predefined tf;
	/* FP16 1.0 reference level in nits, default is 80 nits, only for PQ*/
	uint32_t sdr_ref_white_level;
	union {
		struct pwl_params pwl;
		struct dc_transfer_func_distributed_points tf_pts;
	};
};


union dc_3dlut_state {
	struct {
		uint32_t initialized:1;		/*if 3dlut is went through color module for initialization */
		uint32_t rmu_idx_valid:1;	/*if mux settings are valid*/
		uint32_t rmu_mux_num:3;		/*index of mux to use*/
		uint32_t mpc_rmu0_mux:4;	/*select mpcc on mux, one of the following : mpcc0, mpcc1, mpcc2, mpcc3*/
		uint32_t mpc_rmu1_mux:4;
		uint32_t mpc_rmu2_mux:4;
		uint32_t reserved:15;
	} bits;
	uint32_t raw;
};


struct dc_3dlut {
	struct kref refcount;
	struct tetrahedral_params lut_3d;
	struct fixed31_32 hdr_multiplier;
	union dc_3dlut_state state;
};
/*
 * This structure is filled in by dc_surface_get_status and contains
 * the last requested address and the currently active address so the called
 * can determine if there are any outstanding flips
 */
struct dc_plane_status {
	struct dc_plane_address requested_address;
	struct dc_plane_address current_address;
	bool is_flip_pending;
	bool is_right_eye;
};

union surface_update_flags {

	struct {
		uint32_t addr_update:1;
		/* Medium updates */
		uint32_t dcc_change:1;
		uint32_t color_space_change:1;
		uint32_t horizontal_mirror_change:1;
		uint32_t per_pixel_alpha_change:1;
		uint32_t global_alpha_change:1;
		uint32_t hdr_mult:1;
		uint32_t rotation_change:1;
		uint32_t swizzle_change:1;
		uint32_t scaling_change:1;
		uint32_t position_change:1;
		uint32_t in_transfer_func_change:1;
		uint32_t input_csc_change:1;
		uint32_t coeff_reduction_change:1;
		uint32_t output_tf_change:1;
		uint32_t pixel_format_change:1;
		uint32_t plane_size_change:1;
		uint32_t gamut_remap_change:1;

		/* Full updates */
		uint32_t new_plane:1;
		uint32_t bpp_change:1;
		uint32_t gamma_change:1;
		uint32_t bandwidth_change:1;
		uint32_t clock_change:1;
		uint32_t stereo_format_change:1;
		uint32_t lut_3d:1;
		uint32_t tmz_changed:1;
		uint32_t full_update:1;
	} bits;

	uint32_t raw;
};

struct dc_plane_state {
	struct dc_plane_address address;
	struct dc_plane_flip_time time;
	bool triplebuffer_flips;
	struct scaling_taps scaling_quality;
	struct rect src_rect;
	struct rect dst_rect;
	struct rect clip_rect;

	struct plane_size plane_size;
	union dc_tiling_info tiling_info;

	struct dc_plane_dcc_param dcc;

	struct dc_gamma *gamma_correction;
	struct dc_transfer_func *in_transfer_func;
	struct dc_bias_and_scale *bias_and_scale;
	struct dc_csc_transform input_csc_color_matrix;
	struct fixed31_32 coeff_reduction_factor;
	struct fixed31_32 hdr_mult;
	struct colorspace_transform gamut_remap_matrix;

	// TODO: No longer used, remove
	struct dc_hdr_static_metadata hdr_static_ctx;

	enum dc_color_space color_space;

	struct dc_3dlut *lut3d_func;
	struct dc_transfer_func *in_shaper_func;
	struct dc_transfer_func *blend_tf;

	struct dc_transfer_func *gamcor_tf;
	enum surface_pixel_format format;
	enum dc_rotation_angle rotation;
	enum plane_stereo_format stereo_format;

	bool is_tiling_rotated;
	bool per_pixel_alpha;
	bool pre_multiplied_alpha;
	bool global_alpha;
	int  global_alpha_value;
	bool visible;
	bool flip_immediate;
	bool horizontal_mirror;
	int layer_index;

	union surface_update_flags update_flags;
	bool flip_int_enabled;
	bool skip_manual_trigger;

	/* private to DC core */
	struct dc_plane_status status;
	struct dc_context *ctx;

	/* HACK: Workaround for forcing full reprogramming under some conditions */
	bool force_full_update;

	bool is_phantom; // TODO: Change mall_stream_config into mall_plane_config instead

	/* private to dc_surface.c */
	enum dc_irq_source irq_source;
	struct kref refcount;
	struct tg_color visual_confirm_color;

	bool is_statically_allocated;
};

struct dc_plane_info {
	struct plane_size plane_size;
	union dc_tiling_info tiling_info;
	struct dc_plane_dcc_param dcc;
	enum surface_pixel_format format;
	enum dc_rotation_angle rotation;
	enum plane_stereo_format stereo_format;
	enum dc_color_space color_space;
	bool horizontal_mirror;
	bool visible;
	bool per_pixel_alpha;
	bool pre_multiplied_alpha;
	bool global_alpha;
	int  global_alpha_value;
	bool input_csc_enabled;
	int layer_index;
};

struct dc_scaling_info {
	struct rect src_rect;
	struct rect dst_rect;
	struct rect clip_rect;
	struct scaling_taps scaling_quality;
};

struct dc_surface_update {
	struct dc_plane_state *surface;

	/* isr safe update parameters.  null means no updates */
	const struct dc_flip_addrs *flip_addr;
	const struct dc_plane_info *plane_info;
	const struct dc_scaling_info *scaling_info;
	struct fixed31_32 hdr_mult;
	/* following updates require alloc/sleep/spin that is not isr safe,
	 * null means no updates
	 */
	const struct dc_gamma *gamma;
	const struct dc_transfer_func *in_transfer_func;

	const struct dc_csc_transform *input_csc_color_matrix;
	const struct fixed31_32 *coeff_reduction_factor;
	const struct dc_transfer_func *func_shaper;
	const struct dc_3dlut *lut3d_func;
	const struct dc_transfer_func *blend_tf;
	const struct colorspace_transform *gamut_remap_matrix;
};

/*
 * Create a new surface with default parameters;
 */
struct dc_plane_state *dc_create_plane_state(struct dc *dc);
const struct dc_plane_status *dc_plane_get_status(
		const struct dc_plane_state *plane_state);

void dc_plane_state_retain(struct dc_plane_state *plane_state);
void dc_plane_state_release(struct dc_plane_state *plane_state);

void dc_gamma_retain(struct dc_gamma *dc_gamma);
void dc_gamma_release(struct dc_gamma **dc_gamma);
struct dc_gamma *dc_create_gamma(void);

void dc_transfer_func_retain(struct dc_transfer_func *dc_tf);
void dc_transfer_func_release(struct dc_transfer_func *dc_tf);
struct dc_transfer_func *dc_create_transfer_func(void);

struct dc_3dlut *dc_create_3dlut_func(void);
void dc_3dlut_func_release(struct dc_3dlut *lut);
void dc_3dlut_func_retain(struct dc_3dlut *lut);

void dc_post_update_surfaces_to_stream(
		struct dc *dc);

#include "dc_stream.h"

/**
 * struct dc_validation_set - Struct to store surface/stream associations for validation
 */
struct dc_validation_set {
	/**
	 * @stream: Stream state properties
	 */
	struct dc_stream_state *stream;

	/**
	 * @plane_state: Surface state
	 */
	struct dc_plane_state *plane_states[MAX_SURFACES];

	/**
	 * @plane_count: Total of active planes
	 */
	uint8_t plane_count;
};

bool dc_validate_boot_timing(const struct dc *dc,
				const struct dc_sink *sink,
				struct dc_crtc_timing *crtc_timing);

enum dc_status dc_validate_plane(struct dc *dc, const struct dc_plane_state *plane_state);

void get_clock_requirements_for_state(struct dc_state *state, struct AsicStateEx *info);

enum dc_status dc_validate_with_context(struct dc *dc,
					const struct dc_validation_set set[],
					int set_count,
					struct dc_state *context,
					bool fast_validate);

bool dc_set_generic_gpio_for_stereo(bool enable,
		struct gpio_service *gpio_service);

/*
 * fast_validate: we return after determining if we can support the new state,
 * but before we populate the programming info
 */
enum dc_status dc_validate_global_state(
		struct dc *dc,
		struct dc_state *new_ctx,
		bool fast_validate);


void dc_resource_state_construct(
		const struct dc *dc,
		struct dc_state *dst_ctx);

bool dc_acquire_release_mpc_3dlut(
		struct dc *dc, bool acquire,
		struct dc_stream_state *stream,
		struct dc_3dlut **lut,
		struct dc_transfer_func **shaper);

void dc_resource_state_copy_construct(
		const struct dc_state *src_ctx,
		struct dc_state *dst_ctx);

void dc_resource_state_copy_construct_current(
		const struct dc *dc,
		struct dc_state *dst_ctx);

void dc_resource_state_destruct(struct dc_state *context);

bool dc_resource_is_dsc_encoding_supported(const struct dc *dc);

enum dc_status dc_commit_streams(struct dc *dc,
				 struct dc_stream_state *streams[],
				 uint8_t stream_count);

struct dc_state *dc_create_state(struct dc *dc);
struct dc_state *dc_copy_state(struct dc_state *src_ctx);
void dc_retain_state(struct dc_state *context);
void dc_release_state(struct dc_state *context);

struct dc_plane_state *dc_get_surface_for_mpcc(struct dc *dc,
		struct dc_stream_state *stream,
		int mpcc_inst);


uint32_t dc_get_opp_for_plane(struct dc *dc, struct dc_plane_state *plane);

/* The function returns minimum bandwidth required to drive a given timing
 * return - minimum required timing bandwidth in kbps.
 */
uint32_t dc_bandwidth_in_kbps_from_timing(const struct dc_crtc_timing *timing);

/* Link Interfaces */
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

	/**
	 * @link_state_valid:
	 *
	 * If there is no link and local sink, this variable should be set to
	 * false. Otherwise, it should be set to true; usually, the function
	 * core_link_enable_stream sets this field to true.
	 */
	bool link_state_valid;
	bool aux_access_disabled;
	bool sync_lt_in_progress;
	bool skip_stream_reenable;
	bool is_internal_display;
	/** @todo Rename. Flag an endpoint as having a programmable mapping to a DIG encoder. */
	bool is_dig_mapping_flexible;
	bool hpd_status; /* HPD status of link without physical HPD pin. */
	bool is_hpd_pending; /* Indicates a new received hpd */
	bool is_automated; /* Indicates automated testing */

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

	enum dp_panel_mode panel_mode;
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
	struct hdcp_caps hdcp_caps;
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
		bool dp_skip_fs_144hz;
		bool dp_mot_reset_segment;
		/* Some USB4 docks do not handle turning off MST DSC once it has been enabled. */
		bool dpia_mst_dsc_always_on;
		/* Forced DPIA into TBT3 compatibility mode. */
		bool dpia_forced_tbt3_mode;
		bool dongle_mode_timing_override;
		bool blank_stream_on_ocs_change;
	} wa_flags;
	struct link_mst_stream_allocation_table mst_stream_alloc_table;

	struct dc_link_status link_status;
	struct dprx_states dprx_states;

	struct gpio *hpd_gpio;
	enum dc_link_fec_state fec_state;
	bool link_powered_externally;	// Used to bypass hardware sequencing delays when panel is powered down forcibly

	struct dc_panel_config panel_config;
	struct phy_state phy_state;
	// BW ALLOCATON USB4 ONLY
	struct dc_dpia_bw_alloc dpia_bw_alloc_config;
};

/* Return an enumerated dc_link.
 * dc_link order is constant and determined at
 * boot time.  They cannot be created or destroyed.
 * Use dc_get_caps() to get number of links.
 */
struct dc_link *dc_get_link_at_index(struct dc *dc, uint32_t link_index);

/* Return instance id of the edp link. Inst 0 is primary edp link. */
bool dc_get_edp_link_panel_inst(const struct dc *dc,
		const struct dc_link *link,
		unsigned int *inst_out);

/* Return an array of link pointers to edp links. */
void dc_get_edp_links(const struct dc *dc,
		struct dc_link **edp_links,
		int *edp_num);

/* The function initiates detection handshake over the given link. It first
 * determines if there are display connections over the link. If so it initiates
 * detection protocols supported by the connected receiver device. The function
 * contains protocol specific handshake sequences which are sometimes mandatory
 * to establish a proper connection between TX and RX. So it is always
 * recommended to call this function as the first link operation upon HPD event
 * or power up event. Upon completion, the function will update link structure
 * in place based on latest RX capabilities. The function may also cause dpms
 * to be reset to off for all currently enabled streams to the link. It is DM's
 * responsibility to serialize detection and DPMS updates.
 *
 * @reason - Indicate which event triggers this detection. dc may customize
 * detection flow depending on the triggering events.
 * return false - if detection is not fully completed. This could happen when
 * there is an unrecoverable error during detection or detection is partially
 * completed (detection has been delegated to dm mst manager ie.
 * link->connection_type == dc_connection_mst_branch when returning false).
 * return true - detection is completed, link has been fully updated with latest
 * detection result.
 */
bool dc_link_detect(struct dc_link *link, enum dc_detect_reason reason);

struct dc_sink_init_data;

/* When link connection type is dc_connection_mst_branch, remote sink can be
 * added to the link. The interface creates a remote sink and associates it with
 * current link. The sink will be retained by link until remove remote sink is
 * called.
 *
 * @dc_link - link the remote sink will be added to.
 * @edid - byte array of EDID raw data.
 * @len - size of the edid in byte
 * @init_data -
 */
struct dc_sink *dc_link_add_remote_sink(
		struct dc_link *dc_link,
		const uint8_t *edid,
		int len,
		struct dc_sink_init_data *init_data);

/* Remove remote sink from a link with dc_connection_mst_branch connection type.
 * @link - link the sink should be removed from
 * @sink - sink to be removed.
 */
void dc_link_remove_remote_sink(
	struct dc_link *link,
	struct dc_sink *sink);

/* Enable HPD interrupt handler for a given link */
void dc_link_enable_hpd(const struct dc_link *link);

/* Disable HPD interrupt handler for a given link */
void dc_link_disable_hpd(const struct dc_link *link);

/* determine if there is a sink connected to the link
 *
 * @type - dc_connection_single if connected, dc_connection_none otherwise.
 * return - false if an unexpected error occurs, true otherwise.
 *
 * NOTE: This function doesn't detect downstream sink connections i.e
 * dc_connection_mst_branch, dc_connection_sst_branch. In this case, it will
 * return dc_connection_single if the branch device is connected despite of
 * downstream sink's connection status.
 */
bool dc_link_detect_connection_type(struct dc_link *link,
		enum dc_connection_type *type);

/* query current hpd pin value
 * return - true HPD is asserted (HPD high), false otherwise (HPD low)
 *
 */
bool dc_link_get_hpd_state(struct dc_link *link);

/* Getter for cached link status from given link */
const struct dc_link_status *dc_link_get_status(const struct dc_link *link);

/* enable/disable hardware HPD filter.
 *
 * @link - The link the HPD pin is associated with.
 * @enable = true - enable hardware HPD filter. HPD event will only queued to irq
 * handler once after no HPD change has been detected within dc default HPD
 * filtering interval since last HPD event. i.e if display keeps toggling hpd
 * pulses within default HPD interval, no HPD event will be received until HPD
 * toggles have stopped. Then HPD event will be queued to irq handler once after
 * dc default HPD filtering interval since last HPD event.
 *
 * @enable = false - disable hardware HPD filter. HPD event will be queued
 * immediately to irq handler after no HPD change has been detected within
 * IRQ_HPD (aka HPD short pulse) interval (i.e 2ms).
 */
void dc_link_enable_hpd_filter(struct dc_link *link, bool enable);

/* submit i2c read/write payloads through ddc channel
 * @link_index - index to a link with ddc in i2c mode
 * @cmd - i2c command structure
 * return - true if success, false otherwise.
 */
bool dc_submit_i2c(
		struct dc *dc,
		uint32_t link_index,
		struct i2c_command *cmd);

/* submit i2c read/write payloads through oem channel
 * @link_index - index to a link with ddc in i2c mode
 * @cmd - i2c command structure
 * return - true if success, false otherwise.
 */
bool dc_submit_i2c_oem(
		struct dc *dc,
		struct i2c_command *cmd);

enum aux_return_code_type;
/* Attempt to transfer the given aux payload. This function does not perform
 * retries or handle error states. The reply is returned in the payload->reply
 * and the result through operation_result. Returns the number of bytes
 * transferred,or -1 on a failure.
 */
int dc_link_aux_transfer_raw(struct ddc_service *ddc,
		struct aux_payload *payload,
		enum aux_return_code_type *operation_result);

bool dc_is_oem_i2c_device_present(
	struct dc *dc,
	size_t slave_address
);

/* return true if the connected receiver supports the hdcp version */
bool dc_link_is_hdcp14(struct dc_link *link, enum signal_type signal);
bool dc_link_is_hdcp22(struct dc_link *link, enum signal_type signal);

/* Notify DC about DP RX Interrupt (aka DP IRQ_HPD).
 *
 * TODO - When defer_handling is true the function will have a different purpose.
 * It no longer does complete hpd rx irq handling. We should create a separate
 * interface specifically for this case.
 *
 * Return:
 * true - Downstream port status changed. DM should call DC to do the
 * detection.
 * false - no change in Downstream port status. No further action required
 * from DM.
 */
bool dc_link_handle_hpd_rx_irq(struct dc_link *dc_link,
		union hpd_irq_data *hpd_irq_dpcd_data, bool *out_link_loss,
		bool defer_handling, bool *has_left_work);
/* handle DP specs define test automation sequence*/
void dc_link_dp_handle_automated_test(struct dc_link *link);

/* handle DP Link loss sequence and try to recover RX link loss with best
 * effort
 */
void dc_link_dp_handle_link_loss(struct dc_link *link);

/* Determine if hpd rx irq should be handled or ignored
 * return true - hpd rx irq should be handled.
 * return false - it is safe to ignore hpd rx irq event
 */
bool dc_link_dp_allow_hpd_rx_irq(const struct dc_link *link);

/* Determine if link loss is indicated with a given hpd_irq_dpcd_data.
 * @link - link the hpd irq data associated with
 * @hpd_irq_dpcd_data - input hpd irq data
 * return - true if hpd irq data indicates a link lost
 */
bool dc_link_check_link_loss_status(struct dc_link *link,
		union hpd_irq_data *hpd_irq_dpcd_data);

/* Read hpd rx irq data from a given link
 * @link - link where the hpd irq data should be read from
 * @irq_data - output hpd irq data
 * return - DC_OK if hpd irq data is read successfully, otherwise hpd irq data
 * read has failed.
 */
enum dc_status dc_link_dp_read_hpd_rx_irq_data(
	struct dc_link *link,
	union hpd_irq_data *irq_data);

/* The function clears recorded DP RX states in the link. DM should call this
 * function when it is resuming from S3 power state to previously connected links.
 *
 * TODO - in the future we should consider to expand link resume interface to
 * support clearing previous rx states. So we don't have to rely on dm to call
 * this interface explicitly.
 */
void dc_link_clear_dprx_states(struct dc_link *link);

/* Destruct the mst topology of the link and reset the allocated payload table
 *
 * NOTE: this should only be called if DM chooses not to call dc_link_detect but
 * still wants to reset MST topology on an unplug event */
bool dc_link_reset_cur_dp_mst_topology(struct dc_link *link);

/* The function calculates effective DP link bandwidth when a given link is
 * using the given link settings.
 *
 * return - total effective link bandwidth in kbps.
 */
uint32_t dc_link_bandwidth_kbps(
	const struct dc_link *link,
	const struct dc_link_settings *link_setting);

/* The function takes a snapshot of current link resource allocation state
 * @dc: pointer to dc of the dm calling this
 * @map: a dc link resource snapshot defined internally to dc.
 *
 * DM needs to capture a snapshot of current link resource allocation mapping
 * and store it in its persistent storage.
 *
 * Some of the link resource is using first come first serve policy.
 * The allocation mapping depends on original hotplug order. This information
 * is lost after driver is loaded next time. The snapshot is used in order to
 * restore link resource to its previous state so user will get consistent
 * link capability allocation across reboot.
 *
 */
void dc_get_cur_link_res_map(const struct dc *dc, uint32_t *map);

/* This function restores link resource allocation state from a snapshot
 * @dc: pointer to dc of the dm calling this
 * @map: a dc link resource snapshot defined internally to dc.
 *
 * DM needs to call this function after initial link detection on boot and
 * before first commit streams to restore link resource allocation state
 * from previous boot session.
 *
 * Some of the link resource is using first come first serve policy.
 * The allocation mapping depends on original hotplug order. This information
 * is lost after driver is loaded next time. The snapshot is used in order to
 * restore link resource to its previous state so user will get consistent
 * link capability allocation across reboot.
 *
 */
void dc_restore_link_res_map(const struct dc *dc, uint32_t *map);

/* TODO: this is not meant to be exposed to DM. Should switch to stream update
 * interface i.e stream_update->dsc_config
 */
bool dc_link_update_dsc_config(struct pipe_ctx *pipe_ctx);

/* translate a raw link rate data to bandwidth in kbps */
uint32_t dc_link_bw_kbps_from_raw_frl_link_rate_data(const struct dc *dc, uint8_t bw);

/* determine the optimal bandwidth given link and required bw.
 * @link - current detected link
 * @req_bw - requested bandwidth in kbps
 * @link_settings - returned most optimal link settings that can fit the
 * requested bandwidth
 * return - false if link can't support requested bandwidth, true if link
 * settings is found.
 */
bool dc_link_decide_edp_link_settings(struct dc_link *link,
		struct dc_link_settings *link_settings,
		uint32_t req_bw);

/* return the max dp link settings can be driven by the link without considering
 * connected RX device and its capability
 */
bool dc_link_dp_get_max_link_enc_cap(const struct dc_link *link,
		struct dc_link_settings *max_link_enc_cap);

/* determine when the link is driving MST mode, what DP link channel coding
 * format will be used. The decision will remain unchanged until next HPD event.
 *
 * @link -  a link with DP RX connection
 * return - if stream is committed to this link with MST signal type, type of
 * channel coding format dc will choose.
 */
enum dp_link_encoding dc_link_dp_mst_decide_link_encoding_format(
		const struct dc_link *link);

/* get max dp link settings the link can enable with all things considered. (i.e
 * TX/RX/Cable capabilities and dp override policies.
 *
 * @link - a link with DP RX connection
 * return - max dp link settings the link can enable.
 *
 */
const struct dc_link_settings *dc_link_get_link_cap(const struct dc_link *link);

/* Check if a RX (ex. DP sink, MST hub, passive or active dongle) is connected
 * to a link with dp connector signal type.
 * @link - a link with dp connector signal type
 * return - true if connected, false otherwise
 */
bool dc_link_is_dp_sink_present(struct dc_link *link);

/* Force DP lane settings update to main-link video signal and notify the change
 * to DP RX via DPCD. This is a debug interface used for video signal integrity
 * tuning purpose. The interface assumes link has already been enabled with DP
 * signal.
 *
 * @lt_settings - a container structure with desired hw_lane_settings
 */
void dc_link_set_drive_settings(struct dc *dc,
				struct link_training_settings *lt_settings,
				struct dc_link *link);

/* Enable a test pattern in Link or PHY layer in an active link for compliance
 * test or debugging purpose. The test pattern will remain until next un-plug.
 *
 * @link - active link with DP signal output enabled.
 * @test_pattern - desired test pattern to output.
 * NOTE: set to DP_TEST_PATTERN_VIDEO_MODE to disable previous test pattern.
 * @test_pattern_color_space - for video test pattern choose a desired color
 * space.
 * @p_link_settings - For PHY pattern choose a desired link settings
 * @p_custom_pattern - some test pattern will require a custom input to
 * customize some pattern details. Otherwise keep it to NULL.
 * @cust_pattern_size - size of the custom pattern input.
 *
 */
bool dc_link_dp_set_test_pattern(
	struct dc_link *link,
	enum dp_test_pattern test_pattern,
	enum dp_test_pattern_color_space test_pattern_color_space,
	const struct link_training_settings *p_link_settings,
	const unsigned char *p_custom_pattern,
	unsigned int cust_pattern_size);

/* Force DP link settings to always use a specific value until reboot to a
 * specific link. If link has already been enabled, the interface will also
 * switch to desired link settings immediately. This is a debug interface to
 * generic dp issue trouble shooting.
 */
void dc_link_set_preferred_link_settings(struct dc *dc,
		struct dc_link_settings *link_setting,
		struct dc_link *link);

/* Force DP link to customize a specific link training behavior by overriding to
 * standard DP specs defined protocol. This is a debug interface to trouble shoot
 * display specific link training issues or apply some display specific
 * workaround in link training.
 *
 * @link_settings - if not NULL, force preferred link settings to the link.
 * @lt_override - a set of override pointers. If any pointer is none NULL, dc
 * will apply this particular override in future link training. If NULL is
 * passed in, dc resets previous overrides.
 * NOTE: DM must keep the memory from override pointers until DM resets preferred
 * training settings.
 */
void dc_link_set_preferred_training_settings(struct dc *dc,
		struct dc_link_settings *link_setting,
		struct dc_link_training_overrides *lt_overrides,
		struct dc_link *link,
		bool skip_immediate_retrain);

/* return - true if FEC is supported with connected DP RX, false otherwise */
bool dc_link_is_fec_supported(const struct dc_link *link);

/* query FEC enablement policy to determine if FEC will be enabled by dc during
 * link enablement.
 * return - true if FEC should be enabled, false otherwise.
 */
bool dc_link_should_enable_fec(const struct dc_link *link);

/* determine lttpr mode the current link should be enabled with a specific link
 * settings.
 */
enum lttpr_mode dc_link_decide_lttpr_mode(struct dc_link *link,
		struct dc_link_settings *link_setting);

/* Force DP RX to update its power state.
 * NOTE: this interface doesn't update dp main-link. Calling this function will
 * cause DP TX main-link and DP RX power states out of sync. DM has to restore
 * RX power state back upon finish DM specific execution requiring DP RX in a
 * specific power state.
 * @on - true to set DP RX in D0 power state, false to set DP RX in D3 power
 * state.
 */
void dc_link_dp_receiver_power_ctrl(struct dc_link *link, bool on);

/* Force link to read base dp receiver caps from dpcd 000h - 00Fh and overwrite
 * current value read from extended receiver cap from 02200h - 0220Fh.
 * Some DP RX has problems of providing accurate DP receiver caps from extended
 * field, this interface is a workaround to revert link back to use base caps.
 */
void dc_link_overwrite_extended_receiver_cap(
		struct dc_link *link);

void dc_link_edp_panel_backlight_power_on(struct dc_link *link,
		bool wait_for_hpd);

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

int dc_link_get_backlight_level(const struct dc_link *dc_link);

int dc_link_get_target_backlight_pwm(const struct dc_link *link);

bool dc_link_set_psr_allow_active(struct dc_link *dc_link, const bool *enable,
		bool wait, bool force_static, const unsigned int *power_opts);

bool dc_link_get_psr_state(const struct dc_link *dc_link, enum dc_psr_state *state);

bool dc_link_setup_psr(struct dc_link *dc_link,
		const struct dc_stream_state *stream, struct psr_config *psr_config,
		struct psr_context *psr_context);

/* On eDP links this function call will stall until T12 has elapsed.
 * If the panel is not in power off state, this function will return
 * immediately.
 */
bool dc_link_wait_for_t12(struct dc_link *link);

/* Determine if dp trace has been initialized to reflect upto date result *
 * return - true if trace is initialized and has valid data. False dp trace
 * doesn't have valid result.
 */
bool dc_dp_trace_is_initialized(struct dc_link *link);

/* Query a dp trace flag to indicate if the current dp trace data has been
 * logged before
 */
bool dc_dp_trace_is_logged(struct dc_link *link,
		bool in_detection);

/* Set dp trace flag to indicate whether DM has already logged the current dp
 * trace data. DM can set is_logged to true upon logging and check
 * dc_dp_trace_is_logged before logging to avoid logging the same result twice.
 */
void dc_dp_trace_set_is_logged_flag(struct dc_link *link,
		bool in_detection,
		bool is_logged);

/* Obtain driver time stamp for last dp link training end. The time stamp is
 * formatted based on dm_get_timestamp DM function.
 * @in_detection - true to get link training end time stamp of last link
 * training in detection sequence. false to get link training end time stamp
 * of last link training in commit (dpms) sequence
 */
unsigned long long dc_dp_trace_get_lt_end_timestamp(struct dc_link *link,
		bool in_detection);

/* Get how many link training attempts dc has done with latest sequence.
 * @in_detection - true to get link training count of last link
 * training in detection sequence. false to get link training count of last link
 * training in commit (dpms) sequence
 */
const struct dp_trace_lt_counts *dc_dp_trace_get_lt_counts(struct dc_link *link,
		bool in_detection);

/* Get how many link loss has happened since last link training attempts */
unsigned int dc_dp_trace_get_link_loss_count(struct dc_link *link);

/*
 *  USB4 DPIA BW ALLOCATION PUBLIC FUNCTIONS
 */
/*
 * Send a request from DP-Tx requesting to allocate BW remotely after
 * allocating it locally. This will get processed by CM and a CB function
 * will be called.
 *
 * @link: pointer to the dc_link struct instance
 * @req_bw: The requested bw in Kbyte to allocated
 *
 * return: none
 */
void dc_link_set_usb4_req_bw_req(struct dc_link *link, int req_bw);

/*
 * Handle function for when the status of the Request above is complete.
 * We will find out the result of allocating on CM and update structs.
 *
 * @link: pointer to the dc_link struct instance
 * @bw: Allocated or Estimated BW depending on the result
 * @result: Response type
 *
 * return: none
 */
void dc_link_handle_usb4_bw_alloc_response(struct dc_link *link,
		uint8_t bw, uint8_t result);

/*
 * Handle the USB4 BW Allocation related functionality here:
 * Plug => Try to allocate max bw from timing parameters supported by the sink
 * Unplug => de-allocate bw
 *
 * @link: pointer to the dc_link struct instance
 * @peak_bw: Peak bw used by the link/sink
 *
 * return: allocated bw else return 0
 */
int dc_link_dp_dpia_handle_usb4_bandwidth_allocation_for_link(
		struct dc_link *link, int peak_bw);

/*
 * Validate the BW of all the valid DPIA links to make sure it doesn't exceed
 * available BW for each host router
 *
 * @dc: pointer to dc struct
 * @stream: pointer to all possible streams
 * @num_streams: number of valid DPIA streams
 *
 * return: TRUE if bw used by DPIAs doesn't exceed available BW else return FALSE
 */
bool dc_link_validate(struct dc *dc, const struct dc_stream_state *streams,
		const unsigned int count);

/* Sink Interfaces - A sink corresponds to a display output device */

struct dc_container_id {
	// 128bit GUID in binary form
	unsigned char  guid[16];
	// 8 byte port ID -> ELD.PortID
	unsigned int   portId[2];
	// 128bit GUID in binary formufacturer name -> ELD.ManufacturerName
	unsigned short manufacturerName;
	// 2 byte product code -> ELD.ProductCode
	unsigned short productCode;
};


struct dc_sink_dsc_caps {
	// 'true' if these are virtual DPCD's DSC caps (immediately upstream of sink in MST topology),
	// 'false' if they are sink's DSC caps
	bool is_virtual_dpcd_dsc;
#if defined(CONFIG_DRM_AMD_DC_FP)
	// 'true' if MST topology supports DSC passthrough for sink
	// 'false' if MST topology does not support DSC passthrough
	bool is_dsc_passthrough_supported;
#endif
	struct dsc_dec_dpcd_caps dsc_dec_caps;
};

struct dc_sink_fec_caps {
	bool is_rx_fec_supported;
	bool is_topology_fec_supported;
};

struct scdc_caps {
	union hdmi_scdc_manufacturer_OUI_data manufacturer_OUI;
	union hdmi_scdc_device_id_data device_id;
};

/*
 * The sink structure contains EDID and other display device properties
 */
struct dc_sink {
	enum signal_type sink_signal;
	struct dc_edid dc_edid; /* raw edid */
	struct dc_edid_caps edid_caps; /* parse display caps */
	struct dc_container_id *dc_container_id;
	uint32_t dongle_max_pix_clk;
	void *priv;
	struct stereo_3d_features features_3d[TIMING_3D_FORMAT_MAX];
	bool converter_disable_audio;

	struct scdc_caps scdc_caps;
	struct dc_sink_dsc_caps dsc_caps;
	struct dc_sink_fec_caps fec_caps;

	bool is_vsc_sdp_colorimetry_supported;

	/* private to DC core */
	struct dc_link *link;
	struct dc_context *ctx;

	uint32_t sink_id;

	/* private to dc_sink.c */
	// refcount must be the last member in dc_sink, since we want the
	// sink structure to be logically cloneable up to (but not including)
	// refcount
	struct kref refcount;
};

void dc_sink_retain(struct dc_sink *sink);
void dc_sink_release(struct dc_sink *sink);

struct dc_sink_init_data {
	enum signal_type sink_signal;
	struct dc_link *link;
	uint32_t dongle_max_pix_clk;
	bool converter_disable_audio;
};

struct dc_sink *dc_sink_create(const struct dc_sink_init_data *init_params);

/* Newer interfaces  */
struct dc_cursor {
	struct dc_plane_address address;
	struct dc_cursor_attributes attributes;
};


/* Interrupt interfaces */
enum dc_irq_source dc_interrupt_to_irq_source(
		struct dc *dc,
		uint32_t src_id,
		uint32_t ext_id);
bool dc_interrupt_set(struct dc *dc, enum dc_irq_source src, bool enable);
void dc_interrupt_ack(struct dc *dc, enum dc_irq_source src);
enum dc_irq_source dc_get_hpd_irq_source_at_index(
		struct dc *dc, uint32_t link_index);

void dc_notify_vsync_int_state(struct dc *dc, struct dc_stream_state *stream, bool enable);

/* Power Interfaces */

void dc_set_power_state(
		struct dc *dc,
		enum dc_acpi_cm_power_state power_state);
void dc_resume(struct dc *dc);

void dc_power_down_on_boot(struct dc *dc);

/*
 * HDCP Interfaces
 */
enum hdcp_message_status dc_process_hdcp_msg(
		enum signal_type signal,
		struct dc_link *link,
		struct hdcp_protection_message *message_info);
bool dc_is_dmcu_initialized(struct dc *dc);

enum dc_status dc_set_clock(struct dc *dc, enum dc_clock_type clock_type, uint32_t clk_khz, uint32_t stepping);
void dc_get_clock(struct dc *dc, enum dc_clock_type clock_type, struct dc_clock_config *clock_cfg);

bool dc_is_plane_eligible_for_idle_optimizations(struct dc *dc, struct dc_plane_state *plane,
				struct dc_cursor_attributes *cursor_attr);

void dc_allow_idle_optimizations(struct dc *dc, bool allow);

/* set min and max memory clock to lowest and highest DPM level, respectively */
void dc_unlock_memory_clock_frequency(struct dc *dc);

/* set min memory clock to the min required for current mode, max to maxDPM */
void dc_lock_memory_clock_frequency(struct dc *dc);

/* set soft max for memclk, to be used for AC/DC switching clock limitations */
void dc_enable_dcmode_clk_limit(struct dc *dc, bool enable);

/* cleanup on driver unload */
void dc_hardware_release(struct dc *dc);

/* disables fw based mclk switch */
void dc_mclk_switch_using_fw_based_vblank_stretch_shut_down(struct dc *dc);

bool dc_set_psr_allow_active(struct dc *dc, bool enable);
void dc_z10_restore(const struct dc *dc);
void dc_z10_save_init(struct dc *dc);

bool dc_is_dmub_outbox_supported(struct dc *dc);
bool dc_enable_dmub_notifications(struct dc *dc);

void dc_enable_dmub_outbox(struct dc *dc);

bool dc_process_dmub_aux_transfer_async(struct dc *dc,
				uint32_t link_index,
				struct aux_payload *payload);

/* Get dc link index from dpia port index */
uint8_t get_link_index_from_dpia_port_index(const struct dc *dc,
				uint8_t dpia_port_index);

bool dc_process_dmub_set_config_async(struct dc *dc,
				uint32_t link_index,
				struct set_config_cmd_payload *payload,
				struct dmub_notification *notify);

enum dc_status dc_process_dmub_set_mst_slots(const struct dc *dc,
				uint32_t link_index,
				uint8_t mst_alloc_slots,
				uint8_t *mst_slots_in_use);

void dc_process_dmub_dpia_hpd_int_enable(const struct dc *dc,
				uint32_t hpd_int_enable);

void dc_print_dmub_diagnostic_data(const struct dc *dc);

/* DSC Interfaces */
#include "dc_dsc.h"

/* Disable acc mode Interfaces */
void dc_disable_accelerated_mode(struct dc *dc);

bool dc_is_timing_changed(struct dc_stream_state *cur_stream,
		       struct dc_stream_state *new_stream);

#endif /* DC_INTERFACE_H_ */
