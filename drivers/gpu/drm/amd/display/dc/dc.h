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

#ifndef DC_INTERFACE_H_
#define DC_INTERFACE_H_

#include "dc_types.h"
#include "grph_object_defs.h"
#include "logger_types.h"
#if defined(CONFIG_DRM_AMD_DC_HDCP)
#include "hdcp_types.h"
#endif
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

#define DC_VER "3.2.172"

#define MAX_SURFACES 3
#define MAX_PLANES 6
#define MAX_STREAMS 6
#define MAX_SINKS_PER_LINK 4
#define MIN_VIEWPORT_SIZE 12
#define MAX_NUM_EDP 2

/*******************************************************************************
 * Display Core Interfaces
 ******************************************************************************/
struct dc_versions {
	const char *dc_ver;
	struct dmcu_version dmcu_version;
};

enum dp_protocol_version {
	DP_VERSION_1_4,
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
	uint32_t blends_with_above : 1;
	uint32_t blends_with_below : 1;
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

// Color management caps (DPP and MPC)
struct rom_curve_caps {
	uint16_t srgb : 1;
	uint16_t bt2020 : 1;
	uint16_t gamma2_2 : 1;
	uint16_t pq : 1;
	uint16_t hlg : 1;
};

struct dpp_color_caps {
	uint16_t dcn_arch : 1; // all DCE generations treated the same
	// input lut is different than most LUTs, just plain 256-entry lookup
	uint16_t input_lut_shared : 1; // shared with DGAM
	uint16_t icsc : 1;
	uint16_t dgam_ram : 1;
	uint16_t post_csc : 1; // before gamut remap
	uint16_t gamma_corr : 1;

	// hdr_mult and gamut remap always available in DPP (in that order)
	// 3d lut implies shaper LUT,
	// it may be shared with MPC - check MPC:shared_3d_lut flag
	uint16_t hw_3d_lut : 1;
	uint16_t ogam_ram : 1; // blnd gam
	uint16_t ocsc : 1;
	uint16_t dgam_rom_for_yuv : 1;
	struct rom_curve_caps dgam_rom_caps;
	struct rom_curve_caps ogam_rom_caps;
};

struct mpc_color_caps {
	uint16_t gamut_remap : 1;
	uint16_t ogam_ram : 1;
	uint16_t ocsc : 1;
	uint16_t num_3dluts : 3; //3d lut always assumes a preceding shaper LUT
	uint16_t shared_3d_lut:1; //can be in either DPP or MPC, but single instance

	struct rom_curve_caps ogam_rom_caps;
};

struct dc_color_caps {
	struct dpp_color_caps dpp;
	struct mpc_color_caps mpc;
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
	uint32_t num_of_internal_disp;
	enum dp_protocol_version max_dp_protocol_version;
	unsigned int mall_size_per_mem_channel;
	unsigned int mall_size_total;
	unsigned int cursor_cache_size;
	struct dc_plane_cap planes[MAX_PLANES];
	struct dc_color_caps color;
	bool dp_hpo;
	bool hdmi_frl_pcon_support;
	bool edp_dsc_support;
	bool vbios_lttpr_aware;
	bool vbios_lttpr_enable;
	uint32_t max_otg_num;
};

struct dc_bug_wa {
	bool no_connect_phy_config;
	bool dedcn20_305_wa;
	bool skip_clock_update;
	bool lt_early_cr_pattern;
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
#if defined(CONFIG_DRM_AMD_DC_DCN)
	//These bitfields to be used starting with DCN
	struct {
		uint32_t dcc_256_64_64 : 1;//available in ASICs before DCN (the worst compression case)
		uint32_t dcc_128_128_uncontrained : 1;  //available in ASICs before DCN
		uint32_t dcc_256_128_128 : 1;		//available starting with DCN
		uint32_t dcc_256_256_unconstrained : 1;  //available in ASICs before DCN (the best compression case)
	} dcc_controls;
#endif
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
	bool allow_edp_hotplug_detection;
#if defined(CONFIG_DRM_AMD_DC_DCN)
	bool clamp_min_dcfclk;
#endif
	uint64_t vblank_alignment_dto_params;
	uint8_t  vblank_alignment_max_frame_time_diff;
	bool is_asymmetric_memory;
	bool is_single_rank_dimm;
	bool use_pipe_ctx_sync_logic;
};

enum visual_confirm {
	VISUAL_CONFIRM_DISABLE = 0,
	VISUAL_CONFIRM_SURFACE = 1,
	VISUAL_CONFIRM_HDR = 2,
	VISUAL_CONFIRM_MPCTREE = 4,
	VISUAL_CONFIRM_PSR = 5,
	VISUAL_CONFIRM_SWIZZLE = 9,
};

enum dc_psr_power_opts {
	psr_power_opt_invalid = 0x0,
	psr_power_opt_smu_opt_static_screen = 0x1,
	psr_power_opt_z10_static_screen = 0x10,
};

enum dcc_option {
	DCC_ENABLE = 0,
	DCC_DISABLE = 1,
	DCC_HALF_REQ_DISALBE = 2,
};

enum pipe_split_policy {
	MPC_SPLIT_DYNAMIC = 0,
	MPC_SPLIT_AVOID = 1,
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

#if defined(CONFIG_DRM_AMD_DC_DCN)
enum dcn_zstate_support_state {
	DCN_ZSTATE_SUPPORT_UNKNOWN,
	DCN_ZSTATE_SUPPORT_ALLOW,
	DCN_ZSTATE_SUPPORT_ALLOW_Z10_ONLY,
	DCN_ZSTATE_SUPPORT_DISALLOW,
};
#endif
/*
 * For any clocks that may differ per pipe
 * only the max is stored in this structure
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
#if defined(CONFIG_DRM_AMD_DC_DCN)
	enum dcn_zstate_support_state zstate_support;
	bool dtbclk_en;
#endif
	enum dcn_pwr_state pwr_state;
	/*
	 * Elements below are not compared for the purposes of
	 * optimization required
	 */
	bool prev_p_state_change_support;
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
		uint32_t hpd_delay_in_ms:12; /* bits 4-15 */
		uint32_t disable_force_tbt3_work_around:1; /* bit 16 */
		uint32_t reserved:15;
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
	int urgent_latency_ns;
	int percent_of_ideal_drambw;
	int dram_clock_change_latency_ns;
	int dummy_clock_change_latency_ns;
	/* This forces a hard min on the DCFCLK we use
	 * for DML.  Unlike the debug option for forcing
	 * DCFCLK, this override affects watermark calculations
	 */
	int min_dcfclk_mhz;
};

struct dc_state;
struct resource_pool;
struct dce_hwseq;

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
	int urgent_latency_ns;
	uint32_t underflow_assert_delay_us;
	int percent_of_ideal_drambw;
	int dram_clock_change_latency_ns;
	bool optimized_watermark;
	int always_scale;
	bool disable_pplib_clock_request;
	bool disable_clock_gate;
	bool disable_mem_low_power;
#if defined(CONFIG_DRM_AMD_DC_DCN)
	bool pstate_enabled;
#endif
	bool disable_dmcu;
	bool disable_psr;
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
	bool remove_disconnect_edp;
	unsigned int force_odm_combine; //bit vector based on otg inst
#if defined(CONFIG_DRM_AMD_DC_DCN)
	unsigned int force_odm_combine_4to1; //bit vector based on otg inst
	bool disable_z9_mpc;
#endif
	unsigned int force_fclk_khz;
	bool enable_tri_buf;
	bool dmub_offload_enabled;
	bool dmcub_emulation;
#if defined(CONFIG_DRM_AMD_DC_DCN)
	bool disable_idle_power_optimizations;
	unsigned int mall_size_override;
	unsigned int mall_additional_timer_percent;
	bool mall_error_as_fatal;
#endif
	bool dmub_command_table; /* for testing only */
	struct dc_bw_validation_profile bw_val_profile;
	bool disable_fec;
	bool disable_48mhz_pwrdwn;
	/* This forces a hard min on the DCFCLK requested to SMU/PP
	 * watermarks are not affected.
	 */
	unsigned int force_min_dcfclk_mhz;
#if defined(CONFIG_DRM_AMD_DC_DCN)
	int dwb_fi_phase;
#endif
	bool disable_timing_sync;
	bool cm_in_bypass;
	int force_clock_mode;/*every mode change.*/

	bool disable_dram_clock_change_vactive_support;
	bool validate_dml_output;
	bool enable_dmcub_surface_flip;
	bool usbc_combo_phy_reset_wa;
	bool disable_dsc_edp;
	unsigned int  force_dsc_edp_policy;
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
	bool optimize_edp_link_rate; /* eDP ILR */
	/* FEC/PSR1 sequence enable delay in 100us */
	uint8_t fec_enable_delay_in100us;
	bool enable_driver_sequence_debug;
	enum det_size crb_alloc_policy;
	int crb_alloc_policy_min_disp_count;
#if defined(CONFIG_DRM_AMD_DC_DCN)
	bool disable_z10;
	bool enable_z9_disable_interface;
	bool enable_sw_cntl_psr;
	union dpia_debug_options dpia_debug;
#endif
	bool apply_vendor_specific_lttpr_wa;
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

	struct dc_state *current_state;
	struct resource_pool *res_pool;

	struct clk_mgr *clk_mgr;

	/* Display Engine Clock levels */
	struct dm_pp_clock_levels sclk_lvls;

	/* Inputs into BW and WM calculations. */
	struct bw_calcs_dceip *bw_dceip;
	struct bw_calcs_vbios *bw_vbios;
#ifdef CONFIG_DRM_AMD_DC_DCN
	struct dcn_soc_bounding_box *dcn_soc;
	struct dcn_ip_params *dcn_ip;
	struct display_mode_lib dml;
#endif

	/* HW functions */
	struct hw_sequencer_funcs hwss;
	struct dce_hwseq *hwseq;

	/* Require to optimize clocks and bandwidth for added/removed planes */
	bool optimized_required;
	bool wm_optimized_required;
#if defined(CONFIG_DRM_AMD_DC_DCN)
	bool idle_optimizations_allowed;
#endif
#if defined(CONFIG_DRM_AMD_DC_DCN)
	bool enable_c20_dtm_b0;
#endif

	/* Require to maintain clocks and bandwidth for UEFI enabled HW */

	/* FBC compressor */
	struct compressor *fbc_compressor;

	struct dc_debug_data debug_data;
	struct dpcd_vendor_signature vendor_signature;

	const char *build_id;
	struct vm_helper *vm_helper;
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
#if defined(CONFIG_DRM_AMD_DC_DCN)
	bool force_smu_not_present;
#endif
};

struct dc_callback_init {
#ifdef CONFIG_DRM_AMD_DC_HDCP
	struct cp_psp cp_psp;
#else
	uint8_t reserved;
#endif
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

/*******************************************************************************
 * Surface Interfaces
 ******************************************************************************/

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

#if defined(CONFIG_DRM_AMD_DC_DCN)
	struct dc_transfer_func *gamcor_tf;
#endif
	enum surface_pixel_format format;
	enum dc_rotation_angle rotation;
	enum plane_stereo_format stereo_format;

	bool is_tiling_rotated;
	bool per_pixel_alpha;
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

	/* private to dc_surface.c */
	enum dc_irq_source irq_source;
	struct kref refcount;
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
/*
 * This structure holds a surface address.  There could be multiple addresses
 * in cases such as Stereo 3D, Planar YUV, etc.  Other per-flip attributes such
 * as frame durations and DCC format can also be set.
 */
struct dc_flip_addrs {
	struct dc_plane_address address;
	unsigned int flip_timestamp_in_us;
	bool flip_immediate;
	/* TODO: add flip duration for FreeSync */
	bool triplebuffer_flips;
};

void dc_post_update_surfaces_to_stream(
		struct dc *dc);

#include "dc_stream.h"

/*
 * Structure to store surface/stream associations for validation
 */
struct dc_validation_set {
	struct dc_stream_state *stream;
	struct dc_plane_state *plane_states[MAX_SURFACES];
	uint8_t plane_count;
};

bool dc_validate_boot_timing(const struct dc *dc,
				const struct dc_sink *sink,
				struct dc_crtc_timing *crtc_timing);

enum dc_status dc_validate_plane(struct dc *dc, const struct dc_plane_state *plane_state);

void get_clock_requirements_for_state(struct dc_state *state, struct AsicStateEx *info);

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

#if defined(CONFIG_DRM_AMD_DC_DCN)
bool dc_acquire_release_mpc_3dlut(
		struct dc *dc, bool acquire,
		struct dc_stream_state *stream,
		struct dc_3dlut **lut,
		struct dc_transfer_func **shaper);
#endif

void dc_resource_state_copy_construct(
		const struct dc_state *src_ctx,
		struct dc_state *dst_ctx);

void dc_resource_state_copy_construct_current(
		const struct dc *dc,
		struct dc_state *dst_ctx);

void dc_resource_state_destruct(struct dc_state *context);

bool dc_resource_is_dsc_encoding_supported(const struct dc *dc);

/*
 * TODO update to make it about validation sets
 * Set up streams and links associated to drive sinks
 * The streams parameter is an absolute set of all active streams.
 *
 * After this call:
 *   Phy, Encoder, Timing Generator are programmed and enabled.
 *   New streams are enabled with blank stream; no memory read.
 */
bool dc_commit_state(struct dc *dc, struct dc_state *context);

struct dc_state *dc_create_state(struct dc *dc);
struct dc_state *dc_copy_state(struct dc_state *src_ctx);
void dc_retain_state(struct dc_state *context);
void dc_release_state(struct dc_state *context);

/*******************************************************************************
 * Link Interfaces
 ******************************************************************************/

struct dpcd_caps {
	union dpcd_rev dpcd_rev;
	union max_lane_count max_ln_count;
	union max_down_spread max_down_spread;
	union dprx_feature dprx_feature;

	/* valid only for eDP v1.4 or higher*/
	uint8_t edp_supported_link_rates_count;
	enum dc_link_rate edp_supported_link_rates[8];

	/* dongle type (DP converter, CV smart dongle) */
	enum display_dongle_type dongle_type;
	/* branch device or sink device */
	bool is_branch_dev;
	/* Dongle's downstream count. */
	union sink_count sink_count;
	bool is_mst_capable;
	/* If dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER,
	indicates 'Frame Sequential-to-lllFrame Pack' conversion capability.*/
	struct dc_dongle_caps dongle_caps;

	uint32_t sink_dev_id;
	int8_t sink_dev_id_str[6];
	int8_t sink_hw_revision;
	int8_t sink_fw_revision[2];

	uint32_t branch_dev_id;
	int8_t branch_dev_name[6];
	int8_t branch_hw_revision;
	int8_t branch_fw_revision[2];

	bool allow_invalid_MSA_timing_param;
	bool panel_mode_edp;
	bool dpcd_display_control_capable;
	bool ext_receiver_cap_field_present;
	bool dynamic_backlight_capable_edp;
	union dpcd_fec_capability fec_cap;
	struct dpcd_dsc_capabilities dsc_caps;
	struct dc_lttpr_caps lttpr_caps;
	struct psr_caps psr_caps;
	struct dpcd_usb4_dp_tunneling_info usb4_dp_tun_info;

	union dp_128b_132b_supported_link_rates dp_128b_132b_supported_link_rates;
	union dp_main_line_channel_coding_cap channel_coding_cap;
	union dp_sink_video_fallback_formats fallback_formats;
	union dp_fec_capability1 fec_cap1;
	union dp_cable_attributes cable_attributes;
};

union dpcd_sink_ext_caps {
	struct {
		/* 0 - Sink supports backlight adjust via PWM during SDR/HDR mode
		 * 1 - Sink supports backlight adjust via AUX during SDR/HDR mode.
		 */
		uint8_t sdr_aux_backlight_control : 1;
		uint8_t hdr_aux_backlight_control : 1;
		uint8_t reserved_1 : 2;
		uint8_t oled : 1;
		uint8_t reserved : 3;
	} bits;
	uint8_t raw;
};

#if defined(CONFIG_DRM_AMD_DC_HDCP)
union hdcp_rx_caps {
	struct {
		uint8_t version;
		uint8_t reserved;
		struct {
			uint8_t repeater	: 1;
			uint8_t hdcp_capable	: 1;
			uint8_t reserved	: 6;
		} byte0;
	} fields;
	uint8_t raw[3];
};

union hdcp_bcaps {
	struct {
		uint8_t HDCP_CAPABLE:1;
		uint8_t REPEATER:1;
		uint8_t RESERVED:6;
	} bits;
	uint8_t raw;
};

struct hdcp_caps {
	union hdcp_rx_caps rx_caps;
	union hdcp_bcaps bcaps;
};
#endif

#include "dc_link.h"

#if defined(CONFIG_DRM_AMD_DC_DCN)
uint32_t dc_get_opp_for_plane(struct dc *dc, struct dc_plane_state *plane);

#endif
/*******************************************************************************
 * Sink Interfaces - A sink corresponds to a display output device
 ******************************************************************************/

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
#if defined(CONFIG_DRM_AMD_DC_DCN)
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


/*******************************************************************************
 * Interrupt interfaces
 ******************************************************************************/
enum dc_irq_source dc_interrupt_to_irq_source(
		struct dc *dc,
		uint32_t src_id,
		uint32_t ext_id);
bool dc_interrupt_set(struct dc *dc, enum dc_irq_source src, bool enable);
void dc_interrupt_ack(struct dc *dc, enum dc_irq_source src);
enum dc_irq_source dc_get_hpd_irq_source_at_index(
		struct dc *dc, uint32_t link_index);

void dc_notify_vsync_int_state(struct dc *dc, struct dc_stream_state *stream, bool enable);

/*******************************************************************************
 * Power Interfaces
 ******************************************************************************/

void dc_set_power_state(
		struct dc *dc,
		enum dc_acpi_cm_power_state power_state);
void dc_resume(struct dc *dc);

void dc_power_down_on_boot(struct dc *dc);

#if defined(CONFIG_DRM_AMD_DC_HDCP)
/*
 * HDCP Interfaces
 */
enum hdcp_message_status dc_process_hdcp_msg(
		enum signal_type signal,
		struct dc_link *link,
		struct hdcp_protection_message *message_info);
#endif
bool dc_is_dmcu_initialized(struct dc *dc);

enum dc_status dc_set_clock(struct dc *dc, enum dc_clock_type clock_type, uint32_t clk_khz, uint32_t stepping);
void dc_get_clock(struct dc *dc, enum dc_clock_type clock_type, struct dc_clock_config *clock_cfg);
#if defined(CONFIG_DRM_AMD_DC_DCN)

bool dc_is_plane_eligible_for_idle_optimizations(struct dc *dc, struct dc_plane_state *plane,
				struct dc_cursor_attributes *cursor_attr);

void dc_allow_idle_optimizations(struct dc *dc, bool allow);

/*
 * blank all streams, and set min and max memory clock to
 * lowest and highest DPM level, respectively
 */
void dc_unlock_memory_clock_frequency(struct dc *dc);

/*
 * set min memory clock to the min required for current mode,
 * max to maxDPM, and unblank streams
 */
void dc_lock_memory_clock_frequency(struct dc *dc);

/* set soft max for memclk, to be used for AC/DC switching clock limitations */
void dc_enable_dcmode_clk_limit(struct dc *dc, bool enable);

/* cleanup on driver unload */
void dc_hardware_release(struct dc *dc);

#endif

bool dc_set_psr_allow_active(struct dc *dc, bool enable);
#if defined(CONFIG_DRM_AMD_DC_DCN)
void dc_z10_restore(const struct dc *dc);
void dc_z10_save_init(struct dc *dc);
#endif

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

/*******************************************************************************
 * DSC Interfaces
 ******************************************************************************/
#include "dc_dsc.h"

/*******************************************************************************
 * Disable acc mode Interfaces
 ******************************************************************************/
void dc_disable_accelerated_mode(struct dc *dc);

#endif /* DC_INTERFACE_H_ */
