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
#include "dc_state.h"
#include "dc_plane.h"
#include "grph_object_defs.h"
#include "logger_types.h"
#include "hdcp_msg_types.h"
#include "gpio_types.h"
#include "link_service_types.h"
#include "grph_object_ctrl_defs.h"
#include <inc/hw/opp.h>

#include "hwss/hw_sequencer.h"
#include "inc/compressor.h"
#include "inc/hw/dmcu.h"
#include "dml/display_mode_lib.h"

#include "dml2_0/dml2_wrapper.h"

#include "dmub/inc/dmub_cmd.h"

#include "sspl/dc_spl_types.h"

struct abm_save_restore;

/* forward declaration */
struct aux_payload;
struct set_config_cmd_payload;
struct dmub_notification;
struct dcn_hubbub_reg_state;
struct dcn_hubp_reg_state;
struct dcn_dpp_reg_state;
struct dcn_mpc_reg_state;
struct dcn_opp_reg_state;
struct dcn_dsc_reg_state;
struct dcn_optc_reg_state;
struct dcn_dccg_reg_state;

#define DC_VER "3.2.367"

/**
 * MAX_SURFACES - representative of the upper bound of surfaces that can be piped to a single CRTC
 */
#define MAX_SURFACES 4
/**
 * MAX_PLANES - representative of the upper bound of planes that are supported by the HW
 */
#define MAX_PLANES 6
#define MAX_STREAMS 6
#define MIN_VIEWPORT_SIZE 12
#define MAX_NUM_EDP 2
#define MAX_SUPPORTED_FORMATS 7

#define MAX_HOST_ROUTERS_NUM 3
#define MAX_DPIA_PER_HOST_ROUTER 3
#define MAX_DPIA_NUM  (MAX_HOST_ROUTERS_NUM * MAX_DPIA_PER_HOST_ROUTER)

#define NUM_FAST_FLIPS_TO_STEADY_STATE 20

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

/* Below structure is to describe the HW support for mem layout, extend support
	range to match what OS could handle in the roadmap */
struct lut3d_caps {
	uint32_t dma_3d_lut : 1; /*< DMA mode support for 3D LUT */
	struct {
		uint32_t swizzle_3d_rgb : 1;
		uint32_t swizzle_3d_bgr : 1;
		uint32_t linear_1d : 1;
	} mem_layout_support;
	struct {
		uint32_t unorm_12msb : 1;
		uint32_t unorm_12lsb : 1;
		uint32_t float_fp1_5_10 : 1;
	} mem_format_support;
	struct {
		uint32_t order_rgba : 1;
		uint32_t order_bgra : 1;
	} mem_pixel_order_support;
	/*< size options are 9, 17, 33, 45, 65 */
	struct {
		uint32_t dim_9 : 1; /* 3D LUT support for 9x9x9 */
		uint32_t dim_17 : 1; /* 3D LUT support for 17x17x17 */
		uint32_t dim_33 : 1; /* 3D LUT support for 33x33x33 */
		uint32_t dim_45 : 1; /* 3D LUT support for 45x45x45 */
		uint32_t dim_65 : 1; /* 3D LUT support for 65x65x65 */
	} lut_dim_caps;
};

/**
 * struct mpc_color_caps - color pipeline capabilities for multiple pipe and
 * plane combined blocks
 *
 * @gamut_remap: color transformation matrix
 * @ogam_ram: programmable out gamma LUT
 * @ocsc: output color space conversion matrix
 * @num_3dluts: MPC 3D LUT; always assumes a preceding shaper LUT
 * @num_rmcm_3dluts: number of RMCM 3D LUTS; always assumes a preceding shaper LUT
 * @shared_3d_lut: shared 3D LUT flag. Can be either DPP or MPC, but single
 * instance
 * @ogam_rom_caps: pre-definied curve caps for regamma 1D LUT
 * @mcm_3d_lut_caps: HW support cap for MCM LUT memory
 * @rmcm_3d_lut_caps: HW support cap for RMCM LUT memory
 * @preblend: whether color manager supports preblend with MPC
 */
struct mpc_color_caps {
	uint16_t gamut_remap : 1;
	uint16_t ogam_ram : 1;
	uint16_t ocsc : 1;
	uint16_t num_3dluts : 3;
	uint16_t num_rmcm_3dluts : 3;
	uint16_t shared_3d_lut:1;
	struct rom_curve_caps ogam_rom_caps;
	struct lut3d_caps mcm_3d_lut_caps;
	struct lut3d_caps rmcm_3d_lut_caps;
	bool preblend;
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
	uint8_t fams_ver;
	bool aux_backlight_support;
};

struct dc_scl_caps {
	bool sharpener_support;
};

struct dc_check_config {
	/**
	 * max video plane width that can be safely assumed to be always
	 * supported by single DPP pipe.
	 */
	unsigned int max_optimizable_video_width;
	bool enable_legacy_fast_update;

	bool deferred_transition_state;
	unsigned int transition_countdown_to_steady_state;
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
	unsigned int max_buffered_cursor_size;
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
	bool ips_support;
	bool ips_v2_support;
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
	bool fused_io_supported;
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
	bool vtotal_limited_by_fp2;
	uint32_t max_disp_clock_khz_at_vmin;
	uint8_t subvp_drr_vblank_start_margin_us;
	bool cursor_not_scaled;
	bool dcmode_power_limits_present;
	bool sequential_ono;
	/* Conservative limit for DCC cases which require ODM4:1 to support*/
	uint32_t dcc_plane_width_limit;
	struct dc_scl_caps scl_caps;
	uint8_t num_of_host_routers;
	uint8_t num_of_dpias_per_host_router;
	/* limit of the ODM only, could be limited by other factors (like pipe count)*/
	uint8_t max_odm_combine_factor;
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
	bool skip_psr_ips_crtc_disable;
};
struct dc_dcc_surface_param {
	struct dc_size surface_size;
	enum surface_pixel_format format;
	unsigned int plane0_pitch;
	struct dc_size plane1_size;
	unsigned int plane1_pitch;
	union {
		enum swizzle_mode_values swizzle_mode;
		enum swizzle_mode_addr3_values swizzle_mode_addr3;
	};
	enum dc_scan_direction scan;
};

struct dc_dcc_setting {
	unsigned int max_compressed_blk_size;
	unsigned int max_uncompressed_blk_size;
	bool independent_64b_blks;
	//These bitfields to be used starting with DCN 3.0
	struct {
		uint32_t dcc_256_64_64 : 1;//available in ASICs before DCN 3.0 (the worst compression case)
		uint32_t dcc_128_128_uncontrained : 1;  //available in ASICs before DCN 3.0
		uint32_t dcc_256_128_128 : 1;		//available starting with DCN 3.0
		uint32_t dcc_256_256_unconstrained : 1;  //available in ASICs before DCN 3.0 (the best compression case)
		uint32_t dcc_256_256 : 1;  //available in ASICs starting with DCN 4.0x (the best compression case)
		uint32_t dcc_256_128 : 1;  //available in ASICs starting with DCN 4.0x
		uint32_t dcc_256_64 : 1;   //available in ASICs starting with DCN 4.0x (the worst compression case)
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

enum dc_lock_descriptor {
	LOCK_DESCRIPTOR_NONE = 0x0,
	LOCK_DESCRIPTOR_STREAM = 0x1,
	LOCK_DESCRIPTOR_LINK = 0x2,
	LOCK_DESCRIPTOR_GLOBAL = 0x4,
};

struct surface_update_descriptor {
	enum surface_update_type update_type;
	enum dc_lock_descriptor lock_descriptor;
};

/* Forward declaration*/
struct dc;
struct dc_plane_state;
struct dc_state;

struct dc_cap_funcs {
	bool (*get_dcc_compression_cap)(const struct dc *dc,
			const struct dc_dcc_surface_param *input,
			struct dc_surface_dcc_cap *output);
	bool (*get_subvp_en)(struct dc *dc, struct dc_state *context);
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
	bool skip_riommu_prefetch_wa;
	bool clamp_min_dcfclk;
	uint64_t vblank_alignment_dto_params;
	uint8_t  vblank_alignment_max_frame_time_diff;
	bool is_asymmetric_memory;
	bool is_single_rank_dimm;
	bool is_vmin_only_asic;
	bool use_spl;
	bool prefer_easf;
	bool use_pipe_ctx_sync_logic;
	int smart_mux_version;
	bool ignore_dpref_ss;
	bool enable_mipi_converter_optimization;
	bool use_default_clock_table;
	bool force_bios_enable_lttpr;
	uint8_t force_bios_fixed_vs;
	int sdpif_request_limit_words_per_umc;
	bool dc_mode_clk_limit_support;
	bool EnableMinDispClkODM;
	bool enable_auto_dpm_test_logs;
	unsigned int disable_ips;
	unsigned int disable_ips_rcg;
	unsigned int disable_ips_in_vpb;
	bool disable_ips_in_dpms_off;
	bool usb4_bw_alloc_support;
	bool allow_0_dtb_clk;
	bool use_assr_psp_message;
	bool support_edp0_on_dp1;
	unsigned int enable_fpo_flicker_detection;
	bool disable_hbr_audio_dp2;
	bool consolidated_dpia_dp_lt;
	bool set_pipe_unlock_order;
	bool enable_dpia_pre_training;
	bool unify_link_enc_assignment;
	bool enable_cursor_offload;
	struct spl_sharpness_range dcn_sharpness_range;
	struct spl_sharpness_range dcn_override_sharpness_range;
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
	VISUAL_CONFIRM_SMARTMUX_DGPU = 10,
	VISUAL_CONFIRM_REPLAY = 12,
	VISUAL_CONFIRM_SUBVP = 14,
	VISUAL_CONFIRM_MCLK_SWITCH = 16,
	VISUAL_CONFIRM_FAMS2 = 19,
	VISUAL_CONFIRM_HW_CURSOR = 20,
	VISUAL_CONFIRM_VABC = 21,
	VISUAL_CONFIRM_DCC = 22,
	VISUAL_CONFIRM_EXPLICIT = 0x80000000,
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

enum dc_replay_power_opts {
	replay_power_opt_invalid		= 0x0,
	replay_power_opt_smu_opt_static_screen	= 0x1,
	replay_power_opt_z10_static_screen	= 0x10,
};

enum dcc_option {
	DCC_ENABLE = 0,
	DCC_DISABLE = 1,
	DCC_HALF_REQ_DISALBE = 2,
};

enum in_game_fams_config {
	INGAME_FAMS_SINGLE_DISP_ENABLE, // enable in-game fams
	INGAME_FAMS_DISABLE, // disable in-game fams
	INGAME_FAMS_MULTI_DISP_ENABLE, //enable in-game fams for multi-display
	INGAME_FAMS_MULTI_DISP_CLAMPED_ONLY, //enable in-game fams for multi-display only for clamped RR strategies
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

/*
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
	int host_router_bw_kbps[MAX_HOST_ROUTERS_NUM];

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
	int idle_dramclk_khz;
	int idle_fclk_khz;
	int subvp_prefetch_dramclk_khz;
	int subvp_prefetch_fclk_khz;

	/* Stutter efficiency is technically not clock values
	 * but stored here so the values are part of the update_clocks call similar to num_ways
	 * Efficiencies are stored as percentage (0-100)
	 */
	struct {
		uint8_t base_efficiency; //LP1
		uint8_t low_power_efficiency; //LP2
	} stutter_efficiency;
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

union fine_grain_clock_gating_enable_options {
	struct {
		bool dccg_global_fgcg_rep : 1; /* Global fine grain clock gating of repeaters */
		bool dchub : 1;	   /* Display controller hub */
		bool dchubbub : 1;
		bool dpp : 1;	   /* Display pipes and planes */
		bool opp : 1;	   /* Output pixel processing */
		bool optc : 1;	   /* Output pipe timing combiner */
		bool dio : 1;	   /* Display output */
		bool dwb : 1;	   /* Display writeback */
		bool mmhubbub : 1; /* Multimedia hub */
		bool dmu : 1;	   /* Display core management unit */
		bool az : 1;	   /* Azalia */
		bool dchvm : 1;
		bool dsc : 1;	   /* Display stream compression */

		uint32_t reserved : 19;
	} bits;
	uint32_t u32All;
};

enum pg_hw_pipe_resources {
	PG_HUBP = 0,
	PG_DPP,
	PG_DSC,
	PG_MPCC,
	PG_OPP,
	PG_OPTC,
	PG_DPSTREAM,
	PG_HDMISTREAM,
	PG_PHYSYMCLK,
	PG_HW_PIPE_RESOURCES_NUM_ELEMENT
};

enum pg_hw_resources {
	PG_DCCG = 0,
	PG_DCIO,
	PG_DIO,
	PG_DCHUBBUB,
	PG_DCHVM,
	PG_DWB,
	PG_HPO,
	PG_DCOH,
	PG_HW_RESOURCES_NUM_ELEMENT
};

struct pg_block_update {
	bool pg_pipe_res_update[PG_HW_PIPE_RESOURCES_NUM_ELEMENT][MAX_PIPES];
	bool pg_res_update[PG_HW_RESOURCES_NUM_ELEMENT];
};

union dpia_debug_options {
	struct {
		uint32_t disable_dpia:1; /* bit 0 */
		uint32_t force_non_lttpr:1; /* bit 1 */
		uint32_t extend_aux_rd_interval:1; /* bit 2 */
		uint32_t disable_mst_dsc_work_around:1; /* bit 3 */
		uint32_t enable_force_tbt3_work_around:1; /* bit 4 */
		uint32_t disable_usb4_pm_support:1; /* bit 5 */
		uint32_t enable_usb4_bw_zero_alloc_patch:1; /* bit 6 */
		uint32_t reserved:25;
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
	struct pipe_topology_history topology_history;
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

struct dc_qos_info {
	uint32_t actual_peak_bw_in_mbps;
	uint32_t qos_bandwidth_lb_in_mbps;
	uint32_t actual_avg_bw_in_mbps;
	uint32_t calculated_avg_bw_in_mbps;
	uint32_t actual_max_latency_in_ns;
	uint32_t actual_min_latency_in_ns;
	uint32_t qos_max_latency_ub_in_ns;
	uint32_t actual_avg_latency_in_ns;
	uint32_t qos_avg_latency_ub_in_ns;
	uint32_t dcn_bandwidth_ub_in_mbps;
};

struct dc_state;
struct resource_pool;
struct dce_hwseq;
struct link_service;

/*
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
	bool clock_trace;
	bool validation_trace;
	bool bandwidth_calcs_trace;
	int max_downscale_src_width;

	/* stutter efficiency related */
	bool disable_stutter;
	bool use_max_lb;
	enum dcc_option disable_dcc;

	/*
	 * @pipe_split_policy: Define which pipe split policy is used by the
	 * display core.
	 */
	enum pipe_split_policy pipe_split_policy;
	bool force_single_disp_pipe_split;
	bool voltage_align_fclk;
	bool disable_min_fclk;

	bool hdcp_lc_force_fw_enable;
	bool hdcp_lc_enable_sw_fallback;

	bool disable_dfs_bypass;
	bool disable_dpp_power_gate;
	bool disable_hubp_power_gate;
	bool disable_dsc_power_gate;
	bool disable_optc_power_gate;
	bool disable_hpo_power_gate;
	bool disable_io_clk_power_gate;
	bool disable_mem_power_gate;
	bool disable_dio_power_gate;
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
	int minimum_z10_residency_time;
	bool disable_z9_mpc;
	unsigned int force_fclk_khz;
	bool enable_tri_buf;
	bool ips_disallow_entry;
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
	union fine_grain_clock_gating_enable_options enable_fine_grain_clock_gating;
	bool hpo_optimization;
	bool force_vblank_alignment;

	/* Enable dmub aux for legacy ddc */
	bool enable_dmub_aux_for_legacy_ddc;
	bool disable_fams;
	enum in_game_fams_config disable_fams_gaming;
	/* FEC/PSR1 sequence enable delay in 100us */
	uint8_t fec_enable_delay_in100us;
	bool enable_driver_sequence_debug;
	enum det_size crb_alloc_policy;
	int crb_alloc_policy_min_disp_count;
	bool disable_z10;
	bool enable_z9_disable_interface;
	bool psr_skip_crtc_disable;
	uint32_t ips_skip_crtc_disable_mask;
	union dpia_debug_options dpia_debug;
	bool disable_fixed_vs_aux_timeout_wa;
	uint32_t fixed_vs_aux_delay_config_wa;
	bool force_disable_subvp;
	bool force_subvp_mclk_switch;
	bool allow_sw_cursor_fallback;
	unsigned int force_subvp_num_ways;
	unsigned int force_mall_ss_num_ways;
	bool alloc_extra_way_for_cursor;
	uint32_t subvp_extra_lines;
	bool disable_force_pstate_allow_on_hw_release;
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
	bool using_dml2;
	bool enable_single_display_2to1_odm_policy;
	bool enable_double_buffered_dsc_pg_support;
	bool enable_dp_dig_pixel_rate_div_policy;
	bool using_dml21;
	enum lttpr_mode lttpr_mode_override;
	unsigned int dsc_delay_factor_wa_x1000;
	unsigned int min_prefetch_in_strobe_ns;
	bool disable_unbounded_requesting;
	bool dig_fifo_off_in_blank;
	bool override_dispclk_programming;
	bool otg_crc_db;
	bool disallow_dispclk_dppclk_ds;
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
	bool enable_hpo_pg_support;
	bool disable_dc_mode_overwrite;
	bool replay_skip_crtc_disabled;
	bool ignore_pg;/*do nothing, let pmfw control it*/
	bool psp_disabled_wa;
	unsigned int ips2_eval_delay_us;
	unsigned int ips2_entry_delay_us;
	bool optimize_ips_handshake;
	bool disable_dmub_reallow_idle;
	bool disable_timeout;
	bool disable_extblankadj;
	bool enable_idle_reg_checks;
	unsigned int static_screen_wait_frames;
	uint32_t pwm_freq;
	bool force_chroma_subsampling_1tap;
	unsigned int dcc_meta_propagation_delay_us;
	bool disable_422_left_edge_pixel;
	bool dml21_force_pstate_method;
	uint32_t dml21_force_pstate_method_values[MAX_PIPES];
	uint32_t dml21_disable_pstate_method_mask;
	union fw_assisted_mclk_switch_version fams_version;
	union dmub_fams2_global_feature_config fams2_config;
	unsigned int force_cositing;
	unsigned int disable_spl;
	unsigned int force_easf;
	unsigned int force_sharpness;
	unsigned int force_sharpness_level;
	unsigned int force_lls;
	bool notify_dpia_hr_bw;
	bool enable_ips_visual_confirm;
	unsigned int sharpen_policy;
	unsigned int scale_to_sharpness_policy;
	unsigned int enable_oled_edp_power_up_opt;
	bool enable_hblank_borrow;
	bool force_subvp_df_throttle;
	uint32_t acpi_transition_bitmasks[MAX_PIPES];
	bool enable_pg_cntl_debug_logs;
	unsigned int auxless_alpm_lfps_setup_ns;
	unsigned int auxless_alpm_lfps_period_ns;
	unsigned int auxless_alpm_lfps_silence_ns;
	unsigned int auxless_alpm_lfps_t1t2_us;
	short auxless_alpm_lfps_t1t2_offset_us;
	bool disable_stutter_for_wm_program;
	bool enable_block_sequence_programming;
	uint32_t custom_psp_footer_size;
	bool disable_deferred_minimal_transitions;
	unsigned int num_fast_flips_to_steady_state_override;
	bool enable_dmu_recovery;
	unsigned int force_vmin_threshold;
};


/* Generic structure that can be used to query properties of DC. More fields
 * can be added as required.
 */
struct dc_current_properties {
	unsigned int cursor_size_limit;
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

struct dml2_soc_bb;

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
	uint32_t *clk_reg_offsets;
	void *bb_from_dmub;
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


#define MATRIX_9C__DIM_128_ALIGNED_LEN   16 // 9+8 :  9 * 8 +  7 * 8 = 72  + 56  = 128 % 128 = 0
#define MATRIX_17C__DIM_128_ALIGNED_LEN  32 //17+15:  17 * 8 + 15 * 8 = 136 + 120 = 256 % 128 = 0
#define MATRIX_33C__DIM_128_ALIGNED_LEN  64 //17+47:  17 * 8 + 47 * 8 = 136 + 376 = 512 % 128 = 0

struct lut_rgb {
	uint16_t b;
	uint16_t g;
	uint16_t r;
	uint16_t padding;
};

//this structure maps directly to how the lut will read it from memory
struct lut_mem_mapping {
	union {
		//NATIVE MODE 1, 2
		//RGB layout          [b][g][r]      //red  is 128 byte aligned
		//BGR layout          [r][g][b]      //blue is 128 byte aligned
		struct lut_rgb rgb_17c[17][17][MATRIX_17C__DIM_128_ALIGNED_LEN];
		struct lut_rgb rgb_33c[33][33][MATRIX_33C__DIM_128_ALIGNED_LEN];

		//TRANSFORMED
		uint16_t linear_rgb[(33*33*33*4/128+1)*128];
	};
	uint16_t size;
};

struct dc_rmcm_3dlut {
	bool isInUse;
	const struct dc_stream_state *stream;
	uint8_t protection_bits;
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
		uint32_t mcm_transfer_function_enable_change:1; /* disable or enable MCM transfer func */
		uint32_t full_update:1;
		uint32_t sdr_white_level_nits:1;
	} bits;

	uint32_t raw;
};

#define DC_REMOVE_PLANE_POINTERS 1

struct dc_plane_state {
	struct dc_plane_address address;
	struct dc_plane_flip_time time;
	bool triplebuffer_flips;
	struct scaling_taps scaling_quality;
	struct rect src_rect;
	struct rect dst_rect;
	struct rect clip_rect;

	struct plane_size plane_size;
	struct dc_tiling_info tiling_info;

	struct dc_plane_dcc_param dcc;

	struct dc_gamma gamma_correction;
	struct dc_transfer_func in_transfer_func;
	struct dc_bias_and_scale bias_and_scale;
	struct dc_csc_transform input_csc_color_matrix;
	struct fixed31_32 coeff_reduction_factor;
	struct fixed31_32 hdr_mult;
	struct colorspace_transform gamut_remap_matrix;

	// TODO: No longer used, remove
	struct dc_hdr_static_metadata hdr_static_ctx;

	enum dc_color_space color_space;

	struct dc_3dlut lut3d_func;
	struct dc_transfer_func in_shaper_func;
	struct dc_transfer_func blend_tf;

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
	enum chroma_cositing cositing;
	enum dc_cm2_shaper_3dlut_setting mcm_shaper_3dlut_setting;
	bool mcm_lut1d_enable;
	struct dc_cm2_func_luts mcm_luts;
	bool lut_bank_a;
	enum mpcc_movable_cm_location mcm_location;
	struct dc_csc_transform cursor_csc_color_matrix;
	bool adaptive_sharpness_en;
	int adaptive_sharpness_policy;
	int sharpness_level;
	enum linear_light_scaling linear_light_scaling;
	unsigned int sdr_white_level_nits;
	struct spl_sharpness_range sharpness_range;
	enum sharpness_range_source sharpness_source;
};

struct dc_plane_info {
	struct plane_size plane_size;
	struct dc_tiling_info tiling_info;
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
	enum chroma_cositing cositing;
};

#include "dc_stream.h"

struct dc_scratch_space {
	/* used to temporarily backup plane states of a stream during
	 * dc update. The reason is that plane states are overwritten
	 * with surface updates in dc update. Once they are overwritten
	 * current state is no longer valid. We want to temporarily
	 * store current value in plane states so we can still recover
	 * a valid current state during dc update.
	 */
	struct dc_plane_state plane_states[MAX_SURFACES];

	struct dc_stream_state stream_state;
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
	enum dc_irq_source irq_source_read_request;/* Read Request */

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

	/* USB4 DPIA links skip verifying link cap, instead performing the fallback method
	 * for every link training. This is incompatible with DP LL compliance automation,
	 * which expects the same link settings to be used every retry on a link loss.
	 * This flag is used to skip the fallback when link loss occurs during automation.
	 */
	bool skip_fallback_on_link_loss;

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
	enum engine_id dpia_preferred_eng_id;

	bool test_pattern_enabled;
	/* Pending/Current test pattern are only used to perform and track
	 * FIXED_VS retimer test pattern/lane adjustment override state.
	 * Pending allows link HWSS to differentiate PHY vs non-PHY pattern,
	 * to perform specific lane adjust overrides before setting certain
	 * PHY test patterns. In cases when lane adjust and set test pattern
	 * calls are not performed atomically (i.e. performing link training),
	 * pending_test_pattern will be invalid or contain a non-PHY test pattern
	 * and current_test_pattern will contain required context for any future
	 * set pattern/set lane adjust to transition between override state(s).
	 * */
	enum dp_test_pattern current_test_pattern;
	enum dp_test_pattern pending_test_pattern;

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
	struct replay_settings replay_settings;

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
		bool read_dpcd204h_on_irq_hpd;
		bool force_dp_ffe_preset;
		bool skip_phy_ssc_reduction;
	} wa_flags;
	union dc_dp_ffe_preset forced_dp_ffe_preset;
	struct link_mst_stream_allocation_table mst_stream_alloc_table;

	struct dc_link_status link_status;
	struct dprx_states dprx_states;

	enum dc_link_fec_state fec_state;
	bool is_dds;
	bool is_display_mux_present;
	bool link_powered_externally;	// Used to bypass hardware sequencing delays when panel is powered down forcibly

	struct dc_panel_config panel_config;
	enum dc_panel_type panel_type;
	struct phy_state phy_state;
	uint32_t phy_transition_bitmask;
	// BW ALLOCATON USB4 ONLY
	struct dc_dpia_bw_alloc dpia_bw_alloc_config;
	bool skip_implict_edp_power_control;
	enum backlight_control_type backlight_control_type;
};

struct dc {
	struct dc_debug_options debug;
	struct dc_versions versions;
	struct dc_caps caps;
	struct dc_check_config check_config;
	struct dc_cap_funcs cap_funcs;
	struct dc_config config;
	struct dc_bounding_box_overrides bb_overrides;
	struct dc_bug_wa work_arounds;
	struct dc_context *ctx;
	struct dc_phy_addr_space_config vm_pa_config;

	uint8_t link_count;
	struct dc_link *links[MAX_LINKS];
	uint8_t lowest_dpia_link_index;
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
	bool idle_optimizations_allowed;
	bool enable_c20_dtm_b0;

	/* Require to maintain clocks and bandwidth for UEFI enabled HW */

	/* For eDP to know the switching state of SmartMux */
	bool is_switch_in_progress_orig;
	bool is_switch_in_progress_dest;

	/* FBC compressor */
	struct compressor *fbc_compressor;

	struct dc_debug_data debug_data;
	struct dpcd_vendor_signature vendor_signature;

	const char *build_id;
	struct vm_helper *vm_helper;

	uint32_t *dcn_reg_offsets;
	uint32_t *nbio_reg_offsets;
	uint32_t *clk_reg_offsets;

	/* Scratch memory */
	struct {
		struct {
			/*
			 * For matching clock_limits table in driver with table
			 * from PMFW.
			 */
			struct _vcs_dpi_voltage_scaling_st clock_limits[DC__VOLTAGE_STATES];
		} update_bw_bounding_box;
		struct dc_scratch_space current_state;
		struct dc_scratch_space new_state;
		struct dc_stream_state temp_stream; // Used so we don't need to allocate stream on the stack
		struct dc_link temp_link;
		bool pipes_to_unlock_first[MAX_PIPES]; /* Any of the pipes indicated here should be unlocked first */
	} scratch;

	struct dml2_configuration_options dml2_options;
	struct dml2_configuration_options dml2_dc_power_options;
	enum dc_acpi_cm_power_state power_state;
	struct soc_and_ip_translator *soc_and_ip_translator;
};

struct dc_scaling_info {
	struct rect src_rect;
	struct rect dst_rect;
	struct rect clip_rect;
	struct scaling_taps scaling_quality;
};

struct dc_fast_update {
	const struct dc_flip_addrs *flip_addr;
	const struct dc_gamma *gamma;
	const struct colorspace_transform *gamut_remap_matrix;
	const struct dc_csc_transform *input_csc_color_matrix;
	const struct fixed31_32 *coeff_reduction_factor;
	struct dc_transfer_func *out_transfer_func;
	struct dc_csc_transform *output_csc_transform;
	const struct dc_csc_transform *cursor_csc_color_matrix;
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
	/*
	 * Color Transformations for pre-blend MCM (Shaper, 3DLUT, 1DLUT)
	 *
	 * change cm2_params.component_settings: Full update
	 * change cm2_params.cm2_luts: Fast update
	 */
	const struct dc_cm2_parameters *cm2_params;
	const struct dc_csc_transform *cursor_csc_color_matrix;
	unsigned int sdr_white_level_nits;
	struct dc_bias_and_scale bias_and_scale;
};

struct dc_underflow_debug_data {
	struct dcn_hubbub_reg_state *hubbub_reg_state;
	struct dcn_hubp_reg_state *hubp_reg_state[MAX_PIPES];
	struct dcn_dpp_reg_state *dpp_reg_state[MAX_PIPES];
	struct dcn_mpc_reg_state *mpc_reg_state[MAX_PIPES];
	struct dcn_opp_reg_state *opp_reg_state[MAX_PIPES];
	struct dcn_dsc_reg_state *dsc_reg_state[MAX_PIPES];
	struct dcn_optc_reg_state *optc_reg_state[MAX_PIPES];
	struct dcn_dccg_reg_state *dccg_reg_state[MAX_PIPES];
};

struct power_features {
	bool ips;
	bool rcg;
	bool replay;
	bool dds;
	bool sprs;
	bool psr;
	bool fams;
	bool mpo;
	bool uclk_p_state;
};

/*
 * Create a new surface with default parameters;
 */
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

/**
 * struct dc_validation_set - Struct to store surface/stream associations for validation
 */
struct dc_validation_set {
	/**
	 * @stream: Stream state properties
	 */
	struct dc_stream_state *stream;

	/**
	 * @plane_states: Surface state
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

enum dc_status dc_validate_with_context(struct dc *dc,
					const struct dc_validation_set set[],
					int set_count,
					struct dc_state *context,
					enum dc_validate_mode validate_mode);

bool dc_set_generic_gpio_for_stereo(bool enable,
		struct gpio_service *gpio_service);

enum dc_status dc_validate_global_state(
		struct dc *dc,
		struct dc_state *new_ctx,
		enum dc_validate_mode validate_mode);

bool dc_acquire_release_mpc_3dlut(
		struct dc *dc, bool acquire,
		struct dc_stream_state *stream,
		struct dc_3dlut **lut,
		struct dc_transfer_func **shaper);

bool dc_resource_is_dsc_encoding_supported(const struct dc *dc);
void get_audio_check(struct audio_info *aud_modes,
	struct audio_check *aud_chk);

bool fast_nonaddr_updates_exist(struct dc_fast_update *fast_update, int surface_count);
void populate_fast_updates(struct dc_fast_update *fast_update,
		struct dc_surface_update *srf_updates,
		int surface_count,
		struct dc_stream_update *stream_update);
/*
 * Set up streams and links associated to drive sinks
 * The streams parameter is an absolute set of all active streams.
 *
 * After this call:
 *   Phy, Encoder, Timing Generator are programmed and enabled.
 *   New streams are enabled with blank stream; no memory read.
 */
enum dc_status dc_commit_streams(struct dc *dc, struct dc_commit_streams_params *params);


struct dc_plane_state *dc_get_surface_for_mpcc(struct dc *dc,
		struct dc_stream_state *stream,
		int mpcc_inst);


uint32_t dc_get_opp_for_plane(struct dc *dc, struct dc_plane_state *plane);

void dc_set_disable_128b_132b_stream_overhead(bool disable);

/* The function returns minimum bandwidth required to drive a given timing
 * return - minimum required timing bandwidth in kbps.
 */
uint32_t dc_bandwidth_in_kbps_from_timing(
		const struct dc_crtc_timing *timing,
		const enum dc_link_encoding_format link_encoding);

/* Link Interfaces */
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

void dc_set_edp_power(const struct dc *dc, struct dc_link *edp_link,
				 bool powerOn);

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

struct ddc_service *
dc_get_oem_i2c_device(struct dc *dc);

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

struct dp_audio_bandwidth_params {
	const struct dc_crtc_timing *crtc_timing;
	enum dp_link_encoding link_encoding;
	uint32_t channel_count;
	uint32_t sample_rate_hz;
};

/* The function calculates the minimum size of hblank (in bytes) needed to
 * support the specified channel count and sample rate combination, given the
 * link encoding and timing to be used. This calculation is not supported
 * for 8b/10b SST.
 *
 * return - min hblank size in bytes, 0 if 8b/10b SST.
 */
uint32_t dc_link_required_hblank_size_bytes(
	const struct dc_link *link,
	struct dp_audio_bandwidth_params *audio_params);

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

/* Get the highest encoding format that the link supports; highest meaning the
 * encoding format which supports the maximum bandwidth.
 *
 * @link - a link with DP RX connection
 * return - highest encoding format link supports.
 */
enum dc_link_encoding_format dc_link_get_highest_encoding_format(const struct dc_link *link);

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
		struct set_backlight_level_params *backlight_level_params);

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

/*
 * Communicate with DMUB to allow or disallow Panel Replay on the specified link:
 *
 * @link: pointer to the dc_link struct instance
 * @enable: enable(active) or disable(inactive) replay
 * @wait: state transition need to wait the active set completed.
 * @force_static: force disable(inactive) the replay
 * @power_opts: set power optimazation parameters to DMUB.
 *
 * return: allow Replay active will return true, else will return false.
 */
bool dc_link_set_replay_allow_active(struct dc_link *dc_link, const bool *enable,
		bool wait, bool force_static, const unsigned int *power_opts);

bool dc_link_get_replay_state(const struct dc_link *dc_link, uint64_t *state);

/*
 * Enable or disable Panel Replay on the specified link:
 *
 * @link: pointer to the dc_link struct instance
 * @enable: enable or disable Panel Replay
 *
 * return: true if successful, false otherwise
 */
bool dc_link_set_pr_enable(struct dc_link *link, bool enable);

/*
 * Update Panel Replay state parameters:
 *
 * @link: pointer to the dc_link struct instance
 * @update_state_data: pointer to state update data structure
 *
 * return: true if successful, false otherwise
 */
bool dc_link_update_pr_state(struct dc_link *link,
		struct dmub_cmd_pr_update_state_data *update_state_data);

/*
 * Send general command to Panel Replay firmware:
 *
 * @link: pointer to the dc_link struct instance
 * @general_cmd_data: pointer to general command data structure
 *
 * return: true if successful, false otherwise
 */
bool dc_link_set_pr_general_cmd(struct dc_link *link,
		struct dmub_cmd_pr_general_cmd_data *general_cmd_data);

/*
 * Get Panel Replay state:
 *
 * @link: pointer to the dc_link struct instance
 * @state: pointer to store the Panel Replay state
 *
 * return: true if successful, false otherwise
 */
bool dc_link_get_pr_state(const struct dc_link *link, uint64_t *state);

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
 * Handle the USB4 BW Allocation related functionality here:
 * Plug => Try to allocate max bw from timing parameters supported by the sink
 * Unplug => de-allocate bw
 *
 * @link: pointer to the dc_link struct instance
 * @peak_bw: Peak bw used by the link/sink
 *
 */
void dc_link_dp_dpia_handle_usb4_bandwidth_allocation_for_link(
		struct dc_link *link, int peak_bw);

/*
 * Calculates the DP tunneling bandwidth required for the stream timing
 * and aggregates the stream bandwidth for the respective DP tunneling link
 *
 * return: dc_status
 */
enum dc_status dc_link_validate_dp_tunneling_bandwidth(const struct dc *dc, const struct dc_state *new_ctx);

/*
 * Get if ALPM is supported by the link
 */
void dc_link_get_alpm_support(struct dc_link *link, bool *auxless_support,
	bool *auxwake_support);

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
	// 'true' if MST topology supports DSC passthrough for sink
	// 'false' if MST topology does not support DSC passthrough
	bool is_dsc_passthrough_supported;
	struct dsc_dec_dpcd_caps dsc_dec_caps;
};

struct dc_sink_hblank_expansion_caps {
	// 'true' if these are virtual DPCD's HBlank expansion caps (immediately upstream of sink in MST topology),
	// 'false' if they are sink's HBlank expansion caps
	bool is_virtual_dpcd_hblank_expansion;
	struct hblank_expansion_dpcd_caps dpcd_caps;
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
	struct dc_sink_hblank_expansion_caps hblank_expansion_caps;

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

bool dc_is_plane_eligible_for_idle_optimizations(struct dc *dc,
		unsigned int pitch,
		unsigned int height,
		enum surface_pixel_format format,
		struct dc_cursor_attributes *cursor_attr);

#define dc_allow_idle_optimizations(dc, allow) dc_allow_idle_optimizations_internal(dc, allow, __func__)
#define dc_exit_ips_for_hw_access(dc) dc_exit_ips_for_hw_access_internal(dc, __func__)

void dc_allow_idle_optimizations_internal(struct dc *dc, bool allow, const char *caller_name);
void dc_exit_ips_for_hw_access_internal(struct dc *dc, const char *caller_name);
bool dc_dmub_is_ips_idle_state(struct dc *dc);

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

bool dc_set_replay_allow_active(struct dc *dc, bool active);

bool dc_set_ips_disable(struct dc *dc, unsigned int disable_ips);

void dc_z10_restore(const struct dc *dc);
void dc_z10_save_init(struct dc *dc);

bool dc_is_dmub_outbox_supported(struct dc *dc);
bool dc_enable_dmub_notifications(struct dc *dc);

bool dc_abm_save_restore(
		struct dc *dc,
		struct dc_stream_state *stream,
		struct abm_save_restore *pData);

void dc_enable_dmub_outbox(struct dc *dc);

bool dc_process_dmub_aux_transfer_async(struct dc *dc,
				uint32_t link_index,
				struct aux_payload *payload);

/*
 * smart power OLED Interfaces
 */
bool dc_smart_power_oled_enable(const struct dc_link *link, bool enable, uint16_t peak_nits,
	uint8_t debug_control, uint16_t fixed_CLL, uint32_t triggerline);
bool dc_smart_power_oled_get_max_cll(const struct dc_link *link, unsigned int *pCurrent_MaxCLL);

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

void dc_process_dmub_dpia_set_tps_notification(const struct dc *dc, uint32_t link_index, uint8_t tps);

void dc_process_dmub_dpia_hpd_int_enable(const struct dc *dc,
				uint32_t hpd_int_enable);

void dc_print_dmub_diagnostic_data(const struct dc *dc);

void dc_query_current_properties(struct dc *dc, struct dc_current_properties *properties);

struct dc_power_profile {
	int power_level; /* Lower is better */
};

struct dc_power_profile dc_get_power_profile_for_dc_state(const struct dc_state *context);

unsigned int dc_get_det_buffer_size_from_state(const struct dc_state *context);

bool dc_get_host_router_index(const struct dc_link *link, unsigned int *host_router_index);

void dc_log_preos_dmcub_info(const struct dc *dc);

/* DSC Interfaces */
#include "dc_dsc.h"

void dc_get_visual_confirm_for_stream(
	struct dc *dc,
	struct dc_stream_state *stream_state,
	struct tg_color *color);

/* Disable acc mode Interfaces */
void dc_disable_accelerated_mode(struct dc *dc);

bool dc_is_timing_changed(struct dc_stream_state *cur_stream,
		       struct dc_stream_state *new_stream);

bool dc_is_cursor_limit_pending(struct dc *dc);
bool dc_can_clear_cursor_limit(const struct dc *dc);

/**
 * dc_get_underflow_debug_data_for_otg() - Retrieve underflow debug data.
 *
 * @dc: Pointer to the display core context.
 * @primary_otg_inst: Instance index of the primary OTG that underflowed.
 * @out_data: Pointer to a dc_underflow_debug_data struct to be filled with debug information.
 *
 * This function collects and logs underflow-related HW states when underflow happens,
 * including OTG underflow status, current read positions, frame count, and per-HUBP debug data.
 * The results are stored in the provided out_data structure for further analysis or logging.
 */
void dc_get_underflow_debug_data_for_otg(struct dc *dc, int primary_otg_inst, struct dc_underflow_debug_data *out_data);

void dc_get_power_feature_status(struct dc *dc, int primary_otg_inst, struct power_features *out_data);

/*
 * Software state variables used to program register fields across the display pipeline
 */
struct dc_register_software_state {
	/* HUBP register programming variables for each pipe */
	struct {
		bool valid_plane_state;
		bool valid_stream;
		bool min_dc_gfx_version9;
		uint32_t vtg_sel;                        /* DCHUBP_CNTL->HUBP_VTG_SEL from pipe_ctx->stream_res.tg->inst */
		uint32_t hubp_clock_enable;              /* HUBP_CLK_CNTL->HUBP_CLOCK_ENABLE from power management */
		uint32_t surface_pixel_format;           /* DCSURF_SURFACE_CONFIG->SURFACE_PIXEL_FORMAT from plane_state->format */
		uint32_t rotation_angle;                 /* DCSURF_SURFACE_CONFIG->ROTATION_ANGLE from plane_state->rotation */
		uint32_t h_mirror_en;                    /* DCSURF_SURFACE_CONFIG->H_MIRROR_EN from plane_state->horizontal_mirror */
		uint32_t surface_dcc_en;                 /* DCSURF_SURFACE_CONTROL->PRIMARY_SURFACE_DCC_EN from dcc->enable */
		uint32_t surface_size_width;             /* HUBP_SIZE->SURFACE_SIZE_WIDTH from plane_size.surface_size.width */
		uint32_t surface_size_height;            /* HUBP_SIZE->SURFACE_SIZE_HEIGHT from plane_size.surface_size.height */
		uint32_t pri_viewport_width;             /* DCSURF_PRI_VIEWPORT_DIMENSION->PRI_VIEWPORT_WIDTH from scaler_data.viewport.width */
		uint32_t pri_viewport_height;            /* DCSURF_PRI_VIEWPORT_DIMENSION->PRI_VIEWPORT_HEIGHT from scaler_data.viewport.height */
		uint32_t pri_viewport_x_start;           /* DCSURF_PRI_VIEWPORT_START->PRI_VIEWPORT_X_START from scaler_data.viewport.x */
		uint32_t pri_viewport_y_start;           /* DCSURF_PRI_VIEWPORT_START->PRI_VIEWPORT_Y_START from scaler_data.viewport.y */
		uint32_t cursor_enable;                  /* CURSOR_CONTROL->CURSOR_ENABLE from cursor_attributes.enable */
		uint32_t cursor_width;                   /* CURSOR_SETTINGS->CURSOR_WIDTH from cursor_position.width */
		uint32_t cursor_height;                  /* CURSOR_SETTINGS->CURSOR_HEIGHT from cursor_position.height */

		/* Additional DCC configuration */
		uint32_t surface_dcc_ind_64b_blk;        /* DCSURF_SURFACE_CONTROL->PRIMARY_SURFACE_DCC_IND_64B_BLK from dcc.independent_64b_blks */
		uint32_t surface_dcc_ind_128b_blk;       /* DCSURF_SURFACE_CONTROL->PRIMARY_SURFACE_DCC_IND_128B_BLK from dcc.independent_128b_blks */

		/* Surface pitch configuration */
		uint32_t surface_pitch;                  /* DCSURF_SURFACE_PITCH->PITCH from plane_size.surface_pitch */
		uint32_t meta_pitch;                     /* DCSURF_SURFACE_PITCH->META_PITCH from dcc.meta_pitch */
		uint32_t chroma_pitch;                   /* DCSURF_SURFACE_PITCH_C->PITCH_C from plane_size.chroma_pitch */
		uint32_t meta_pitch_c;                   /* DCSURF_SURFACE_PITCH_C->META_PITCH_C from dcc.meta_pitch_c */

		/* Surface addresses */
		uint32_t primary_surface_address_low;    /* DCSURF_PRIMARY_SURFACE_ADDRESS->PRIMARY_SURFACE_ADDRESS from address.grph.addr.low_part */
		uint32_t primary_surface_address_high;   /* DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH->PRIMARY_SURFACE_ADDRESS_HIGH from address.grph.addr.high_part */
		uint32_t primary_meta_surface_address_low;  /* DCSURF_PRIMARY_META_SURFACE_ADDRESS->PRIMARY_META_SURFACE_ADDRESS from address.grph.meta_addr.low_part */
		uint32_t primary_meta_surface_address_high; /* DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH->PRIMARY_META_SURFACE_ADDRESS_HIGH from address.grph.meta_addr.high_part */

		/* TMZ configuration */
		uint32_t primary_surface_tmz;            /* DCSURF_SURFACE_CONTROL->PRIMARY_SURFACE_TMZ from address.tmz_surface */
		uint32_t primary_meta_surface_tmz;       /* DCSURF_SURFACE_CONTROL->PRIMARY_META_SURFACE_TMZ from address.tmz_surface */

		/* Tiling configuration */
		uint32_t sw_mode;                        /* DCSURF_TILING_CONFIG->SW_MODE from tiling_info.gfx9.swizzle */
		uint32_t num_pipes;                      /* DCSURF_ADDR_CONFIG->NUM_PIPES from tiling_info.gfx9.num_pipes */
		uint32_t num_banks;                      /* DCSURF_ADDR_CONFIG->NUM_BANKS from tiling_info.gfx9.num_banks */
		uint32_t pipe_interleave;                /* DCSURF_ADDR_CONFIG->PIPE_INTERLEAVE from tiling_info.gfx9.pipe_interleave */
		uint32_t num_shader_engines;             /* DCSURF_ADDR_CONFIG->NUM_SE from tiling_info.gfx9.num_shader_engines */
		uint32_t num_rb_per_se;                  /* DCSURF_ADDR_CONFIG->NUM_RB_PER_SE from tiling_info.gfx9.num_rb_per_se */
		uint32_t num_pkrs;                       /* DCSURF_ADDR_CONFIG->NUM_PKRS from tiling_info.gfx9.num_pkrs */

		/* DML Request Size Configuration - Luma */
		uint32_t rq_chunk_size;                  /* DCHUBP_REQ_SIZE_CONFIG->CHUNK_SIZE from rq_regs.rq_regs_l.chunk_size */
		uint32_t rq_min_chunk_size;              /* DCHUBP_REQ_SIZE_CONFIG->MIN_CHUNK_SIZE from rq_regs.rq_regs_l.min_chunk_size */
		uint32_t rq_meta_chunk_size;             /* DCHUBP_REQ_SIZE_CONFIG->META_CHUNK_SIZE from rq_regs.rq_regs_l.meta_chunk_size */
		uint32_t rq_min_meta_chunk_size;         /* DCHUBP_REQ_SIZE_CONFIG->MIN_META_CHUNK_SIZE from rq_regs.rq_regs_l.min_meta_chunk_size */
		uint32_t rq_dpte_group_size;             /* DCHUBP_REQ_SIZE_CONFIG->DPTE_GROUP_SIZE from rq_regs.rq_regs_l.dpte_group_size */
		uint32_t rq_mpte_group_size;             /* DCHUBP_REQ_SIZE_CONFIG->MPTE_GROUP_SIZE from rq_regs.rq_regs_l.mpte_group_size */
		uint32_t rq_swath_height_l;              /* DCHUBP_REQ_SIZE_CONFIG->SWATH_HEIGHT_L from rq_regs.rq_regs_l.swath_height */
		uint32_t rq_pte_row_height_l;            /* DCHUBP_REQ_SIZE_CONFIG->PTE_ROW_HEIGHT_L from rq_regs.rq_regs_l.pte_row_height */

		/* DML Request Size Configuration - Chroma */
		uint32_t rq_chunk_size_c;                /* DCHUBP_REQ_SIZE_CONFIG_C->CHUNK_SIZE_C from rq_regs.rq_regs_c.chunk_size */
		uint32_t rq_min_chunk_size_c;            /* DCHUBP_REQ_SIZE_CONFIG_C->MIN_CHUNK_SIZE_C from rq_regs.rq_regs_c.min_chunk_size */
		uint32_t rq_meta_chunk_size_c;           /* DCHUBP_REQ_SIZE_CONFIG_C->META_CHUNK_SIZE_C from rq_regs.rq_regs_c.meta_chunk_size */
		uint32_t rq_min_meta_chunk_size_c;       /* DCHUBP_REQ_SIZE_CONFIG_C->MIN_META_CHUNK_SIZE_C from rq_regs.rq_regs_c.min_meta_chunk_size */
		uint32_t rq_dpte_group_size_c;           /* DCHUBP_REQ_SIZE_CONFIG_C->DPTE_GROUP_SIZE_C from rq_regs.rq_regs_c.dpte_group_size */
		uint32_t rq_mpte_group_size_c;           /* DCHUBP_REQ_SIZE_CONFIG_C->MPTE_GROUP_SIZE_C from rq_regs.rq_regs_c.mpte_group_size */
		uint32_t rq_swath_height_c;              /* DCHUBP_REQ_SIZE_CONFIG_C->SWATH_HEIGHT_C from rq_regs.rq_regs_c.swath_height */
		uint32_t rq_pte_row_height_c;            /* DCHUBP_REQ_SIZE_CONFIG_C->PTE_ROW_HEIGHT_C from rq_regs.rq_regs_c.pte_row_height */

		/* DML Expansion Modes */
		uint32_t drq_expansion_mode;             /* DCN_EXPANSION_MODE->DRQ_EXPANSION_MODE from rq_regs.drq_expansion_mode */
		uint32_t prq_expansion_mode;             /* DCN_EXPANSION_MODE->PRQ_EXPANSION_MODE from rq_regs.prq_expansion_mode */
		uint32_t mrq_expansion_mode;             /* DCN_EXPANSION_MODE->MRQ_EXPANSION_MODE from rq_regs.mrq_expansion_mode */
		uint32_t crq_expansion_mode;             /* DCN_EXPANSION_MODE->CRQ_EXPANSION_MODE from rq_regs.crq_expansion_mode */

		/* DML DLG parameters - nominal */
		uint32_t dst_y_per_vm_vblank;            /* NOM_PARAMETERS_0->DST_Y_PER_VM_VBLANK from dlg_regs.dst_y_per_vm_vblank */
		uint32_t dst_y_per_row_vblank;           /* NOM_PARAMETERS_0->DST_Y_PER_ROW_VBLANK from dlg_regs.dst_y_per_row_vblank */
		uint32_t dst_y_per_vm_flip;              /* NOM_PARAMETERS_1->DST_Y_PER_VM_FLIP from dlg_regs.dst_y_per_vm_flip */
		uint32_t dst_y_per_row_flip;             /* NOM_PARAMETERS_1->DST_Y_PER_ROW_FLIP from dlg_regs.dst_y_per_row_flip */

		/* DML prefetch settings */
		uint32_t dst_y_prefetch;                 /* PREFETCH_SETTINS->DST_Y_PREFETCH from dlg_regs.dst_y_prefetch */
		uint32_t vratio_prefetch;                /* PREFETCH_SETTINS->VRATIO_PREFETCH from dlg_regs.vratio_prefetch */
		uint32_t vratio_prefetch_c;              /* PREFETCH_SETTINS_C->VRATIO_PREFETCH_C from dlg_regs.vratio_prefetch_c */

		/* TTU parameters */
		uint32_t qos_level_low_wm;               /* TTU_CNTL1->QoSLevelLowWaterMark from ttu_regs.qos_level_low_wm */
		uint32_t qos_level_high_wm;              /* TTU_CNTL1->QoSLevelHighWaterMark from ttu_regs.qos_level_high_wm */
		uint32_t qos_level_flip;                 /* TTU_CNTL2->QoS_LEVEL_FLIP_L from ttu_regs.qos_level_flip */
		uint32_t min_ttu_vblank;                 /* DCN_GLOBAL_TTU_CNTL->MIN_TTU_VBLANK from ttu_regs.min_ttu_vblank */
	} hubp[MAX_PIPES];

	/* HUBBUB register programming variables */
	struct {
		/* Individual DET buffer control per pipe - software state that programs DET registers */
		uint32_t det0_size;                      /* DCHUBBUB_DET0_CTRL->DET0_SIZE from hubbub->funcs->program_det_size(hubbub, 0, det_buffer_size_kb) */
		uint32_t det1_size;                      /* DCHUBBUB_DET1_CTRL->DET1_SIZE from hubbub->funcs->program_det_size(hubbub, 1, det_buffer_size_kb) */
		uint32_t det2_size;                      /* DCHUBBUB_DET2_CTRL->DET2_SIZE from hubbub->funcs->program_det_size(hubbub, 2, det_buffer_size_kb) */
		uint32_t det3_size;                      /* DCHUBBUB_DET3_CTRL->DET3_SIZE from hubbub->funcs->program_det_size(hubbub, 3, det_buffer_size_kb) */

		/* Compression buffer control - software state that programs COMPBUF registers */
		uint32_t compbuf_size;                   /* DCHUBBUB_COMPBUF_CTRL->COMPBUF_SIZE from hubbub->funcs->program_compbuf_size(hubbub, compbuf_size_kb, safe_to_increase) */
		uint32_t compbuf_reserved_space_64b;     /* COMPBUF_RESERVED_SPACE->COMPBUF_RESERVED_SPACE_64B from hubbub2->pixel_chunk_size / 32 */
		uint32_t compbuf_reserved_space_zs;      /* COMPBUF_RESERVED_SPACE->COMPBUF_RESERVED_SPACE_ZS from hubbub2->pixel_chunk_size / 128 */
	} hubbub;

	/* DPP register programming variables for each pipe (simplified for available fields) */
	struct {
		uint32_t dpp_clock_enable;               /* DPP_CONTROL->DPP_CLOCK_ENABLE from dppclk_enable */

		/* Recout (Rectangle of Interest) configuration */
		uint32_t recout_start_x;                 /* RECOUT_START->RECOUT_START_X from pipe_ctx->plane_res.scl_data.recout.x */
		uint32_t recout_start_y;                 /* RECOUT_START->RECOUT_START_Y from pipe_ctx->plane_res.scl_data.recout.y */
		uint32_t recout_width;                   /* RECOUT_SIZE->RECOUT_WIDTH from pipe_ctx->plane_res.scl_data.recout.width */
		uint32_t recout_height;                  /* RECOUT_SIZE->RECOUT_HEIGHT from pipe_ctx->plane_res.scl_data.recout.height */

		/* MPC (Multiple Pipe/Plane Combiner) size configuration */
		uint32_t mpc_width;                      /* MPC_SIZE->MPC_WIDTH from pipe_ctx->plane_res.scl_data.h_active */
		uint32_t mpc_height;                     /* MPC_SIZE->MPC_HEIGHT from pipe_ctx->plane_res.scl_data.v_active */

		/* DSCL mode configuration */
		uint32_t dscl_mode;                      /* SCL_MODE->DSCL_MODE from pipe_ctx->plane_res.scl_data.dscl_prog_data.dscl_mode */

		/* Scaler ratios (simplified to integer parts) */
		uint32_t horz_ratio_int;                 /* SCL_HORZ_FILTER_SCALE_RATIO->SCL_H_SCALE_RATIO integer part from ratios.horz */
		uint32_t vert_ratio_int;                 /* SCL_VERT_FILTER_SCALE_RATIO->SCL_V_SCALE_RATIO integer part from ratios.vert */

		/* Basic scaler taps */
		uint32_t h_taps;                         /* SCL_TAP_CONTROL->SCL_H_NUM_TAPS from taps.h_taps */
		uint32_t v_taps;                         /* SCL_TAP_CONTROL->SCL_V_NUM_TAPS from taps.v_taps */
	} dpp[MAX_PIPES];

	/* DCCG register programming variables */
	struct {
		/* Core Display Clock Control */
		uint32_t dispclk_khz;                    /* DENTIST_DISPCLK_CNTL->DENTIST_DISPCLK_WDIVIDER from clk_mgr.dispclk_khz */
		uint32_t dc_mem_global_pwr_req_dis;      /* DC_MEM_GLOBAL_PWR_REQ_CNTL->DC_MEM_GLOBAL_PWR_REQ_DIS from memory power management settings */

		/* DPP Clock Control - 4 fields per pipe */
		uint32_t dppclk_khz[MAX_PIPES];          /* DPPCLK_CTRL->DPPCLK_R_GATE_DISABLE from dpp_clocks[pipe] */
		uint32_t dppclk_enable[MAX_PIPES];       /* DPPCLK_CTRL->DPPCLK0_EN,DPPCLK1_EN,DPPCLK2_EN,DPPCLK3_EN from dccg31_update_dpp_dto() */
		uint32_t dppclk_dto_enable[MAX_PIPES];   /* DPPCLK_DTO_CTRL->DPPCLK_DTO_ENABLE from dccg->dpp_clock_gated[dpp_inst] state */
		uint32_t dppclk_dto_phase[MAX_PIPES];    /* DPPCLK0_DTO_PARAM->DPPCLK0_DTO_PHASE from phase calculation req_dppclk/ref_dppclk */
		uint32_t dppclk_dto_modulo[MAX_PIPES];   /* DPPCLK0_DTO_PARAM->DPPCLK0_DTO_MODULO from modulo = 0xff */

		/* DSC Clock Control - 4 fields per DSC resource */
		uint32_t dscclk_khz[MAX_PIPES]; /* DSCCLK_DTO_CTRL->DSCCLK_DTO_ENABLE from dsc_clocks */
		uint32_t dscclk_dto_enable[MAX_PIPES]; /* DSCCLK_DTO_CTRL->DSCCLK0_DTO_ENABLE,DSCCLK1_DTO_ENABLE,DSCCLK2_DTO_ENABLE,DSCCLK3_DTO_ENABLE */
		uint32_t dscclk_dto_phase[MAX_PIPES];  /* DSCCLK0_DTO_PARAM->DSCCLK0_DTO_PHASE from dccg31_enable_dscclk() */
		uint32_t dscclk_dto_modulo[MAX_PIPES]; /* DSCCLK0_DTO_PARAM->DSCCLK0_DTO_MODULO from dccg31_enable_dscclk() */

		/* Pixel Clock Control - per pipe */
		uint32_t pixclk_khz[MAX_PIPES];          /* PIXCLK_RESYNC_CNTL->PIXCLK_RESYNC_ENABLE from stream.timing.pix_clk_100hz */
		uint32_t otg_pixel_rate_div[MAX_PIPES];  /* OTG_PIXEL_RATE_DIV->OTG_PIXEL_RATE_DIV from OTG pixel rate divider control */
		uint32_t dtbclk_dto_enable[MAX_PIPES];   /* OTG0_PIXEL_RATE_CNTL->DTBCLK_DTO_ENABLE from dccg31_set_dtbclk_dto() */
		uint32_t pipe_dto_src_sel[MAX_PIPES];    /* OTG0_PIXEL_RATE_CNTL->PIPE_DTO_SRC_SEL from dccg31_set_dtbclk_dto() source selection */
		uint32_t dtbclk_dto_div[MAX_PIPES];      /* OTG0_PIXEL_RATE_CNTL->DTBCLK_DTO_DIV from dtbdto_div calculation */
		uint32_t otg_add_pixel[MAX_PIPES];       /* OTG0_PIXEL_RATE_CNTL->OTG_ADD_PIXEL from dccg31_otg_add_pixel() */
		uint32_t otg_drop_pixel[MAX_PIPES];      /* OTG0_PIXEL_RATE_CNTL->OTG_DROP_PIXEL from dccg31_otg_drop_pixel() */

		/* DTBCLK DTO Control - 4 DTOs */
		uint32_t dtbclk_dto_modulo[4];           /* DTBCLK_DTO0_MODULO->DTBCLK_DTO0_MODULO from dccg31_set_dtbclk_dto() modulo calculation */
		uint32_t dtbclk_dto_phase[4];            /* DTBCLK_DTO0_PHASE->DTBCLK_DTO0_PHASE from phase calculation pixclk_khz/ref_dtbclk_khz */
		uint32_t dtbclk_dto_dbuf_en;             /* DTBCLK_DTO_DBUF_EN->DTBCLK DTO data buffer enable */

		/* DP Stream Clock Control - 4 pipes */
		uint32_t dpstreamclk_enable[MAX_PIPES];          /* DPSTREAMCLK_CNTL->DPSTREAMCLK_PIPE0_EN,DPSTREAMCLK_PIPE1_EN,DPSTREAMCLK_PIPE2_EN,DPSTREAMCLK_PIPE3_EN */
		uint32_t dp_dto_modulo[4];               /* DP_DTO0_MODULO->DP_DTO0_MODULO from DP stream DTO programming */
		uint32_t dp_dto_phase[4];                /* DP_DTO0_PHASE->DP_DTO0_PHASE from DP stream DTO programming */
		uint32_t dp_dto_dbuf_en;                 /* DP_DTO_DBUF_EN->DP DTO data buffer enable */

		/* PHY Symbol Clock Control - 5 PHYs (A,B,C,D,E) */
		uint32_t phy_symclk_force_en[5];         /* PHYASYMCLK_CLOCK_CNTL->PHYASYMCLK_FORCE_EN from dccg31_set_physymclk() force_enable */
		uint32_t phy_symclk_force_src_sel[5];    /* PHYASYMCLK_CLOCK_CNTL->PHYASYMCLK_FORCE_SRC_SEL from dccg31_set_physymclk() clk_src */
		uint32_t phy_symclk_gate_disable[5];     /* DCCG_GATE_DISABLE_CNTL2->PHYASYMCLK_GATE_DISABLE from debug.root_clock_optimization.bits.physymclk */

		/* SYMCLK32 SE Control - 4 instances */
		uint32_t symclk32_se_src_sel[4];         /* SYMCLK32_SE_CNTL->SYMCLK32_SE0_SRC_SEL from dccg31_enable_symclk32_se() with get_phy_mux_symclk() mapping */
		uint32_t symclk32_se_enable[4];          /* SYMCLK32_SE_CNTL->SYMCLK32_SE0_EN from dccg31_enable_symclk32_se() enable */
		uint32_t symclk32_se_gate_disable[4];    /* DCCG_GATE_DISABLE_CNTL3->SYMCLK32_SE0_GATE_DISABLE from debug.root_clock_optimization.bits.symclk32_se */

		/* SYMCLK32 LE Control - 2 instances */
		uint32_t symclk32_le_src_sel[2];         /* SYMCLK32_LE_CNTL->SYMCLK32_LE0_SRC_SEL from dccg31_enable_symclk32_le() phyd32clk source */
		uint32_t symclk32_le_enable[2];          /* SYMCLK32_LE_CNTL->SYMCLK32_LE0_EN from dccg31_enable_symclk32_le() enable */
		uint32_t symclk32_le_gate_disable[2];    /* DCCG_GATE_DISABLE_CNTL3->SYMCLK32_LE0_GATE_DISABLE from debug.root_clock_optimization.bits.symclk32_le */

		/* DPIA Clock Control */
		uint32_t dpiaclk_540m_dto_modulo;        /* DPIACLK_540M_DTO_MODULO->DPIA 540MHz DTO modulo */
		uint32_t dpiaclk_540m_dto_phase;         /* DPIACLK_540M_DTO_PHASE->DPIA 540MHz DTO phase */
		uint32_t dpiaclk_810m_dto_modulo;        /* DPIACLK_810M_DTO_MODULO->DPIA 810MHz DTO modulo */
		uint32_t dpiaclk_810m_dto_phase;         /* DPIACLK_810M_DTO_PHASE->DPIA 810MHz DTO phase */
		uint32_t dpiaclk_dto_cntl;               /* DPIACLK_DTO_CNTL->DPIA clock DTO control */
		uint32_t dpiasymclk_cntl;                /* DPIASYMCLK_CNTL->DPIA symbol clock control */

		/* Clock Gating Control */
		uint32_t dccg_gate_disable_cntl;         /* DCCG_GATE_DISABLE_CNTL->Clock gate disable control from dccg31_init() */
		uint32_t dpstreamclk_gate_disable;       /* DCCG_GATE_DISABLE_CNTL3->DPSTREAMCLK_GATE_DISABLE from debug.root_clock_optimization.bits.dpstream */
		uint32_t dpstreamclk_root_gate_disable;  /* DCCG_GATE_DISABLE_CNTL3->DPSTREAMCLK_ROOT_GATE_DISABLE from debug.root_clock_optimization.bits.dpstream */

		/* VSync Control */
		uint32_t vsync_cnt_ctrl;                 /* DCCG_VSYNC_CNT_CTRL->VSync counter control */
		uint32_t vsync_cnt_int_ctrl;             /* DCCG_VSYNC_CNT_INT_CTRL->VSync counter interrupt control */
		uint32_t vsync_otg_latch_value[6];       /* DCCG_VSYNC_OTG0_LATCH_VALUE->OTG0 VSync latch value (for OTG0-5) */

		/* Time Base Control */
		uint32_t microsecond_time_base_div;      /* MICROSECOND_TIME_BASE_DIV->Microsecond time base divider */
		uint32_t millisecond_time_base_div;      /* MILLISECOND_TIME_BASE_DIV->Millisecond time base divider */
	} dccg;

	/* DSC essential configuration for underflow analysis */
	struct {
		/* DSC active state - critical for bandwidth analysis */
		uint32_t dsc_clock_enable;               /* DSC enabled - affects bandwidth requirements */

		/* DSC configuration affecting bandwidth and timing */
		uint32_t dsc_num_slices_h;              /* Horizontal slice count - affects throughput */
		uint32_t dsc_num_slices_v;              /* Vertical slice count - affects throughput */
		uint32_t dsc_bits_per_pixel;            /* Compression ratio - affects bandwidth */

		/* OPP integration - affects pipeline flow */
		uint32_t dscrm_dsc_forward_enable;      /* DSC forwarding to OPP enabled */
		uint32_t dscrm_dsc_opp_pipe_source;    /* Which OPP receives DSC output */
	} dsc[MAX_PIPES];

	/* MPC register programming variables */
	struct {
		/* MPCC blending tree and mode control */
		uint32_t mpcc_mode[MAX_PIPES];           /* MPCC_CONTROL->MPCC_MODE from blend_cfg.blend_mode */
		uint32_t mpcc_alpha_blend_mode[MAX_PIPES]; /* MPCC_CONTROL->MPCC_ALPHA_BLND_MODE from blend_cfg.alpha_mode */
		uint32_t mpcc_alpha_multiplied_mode[MAX_PIPES]; /* MPCC_CONTROL->MPCC_ALPHA_MULTIPLIED_MODE from blend_cfg.pre_multiplied_alpha */
		uint32_t mpcc_blnd_active_overlap_only[MAX_PIPES]; /* MPCC_CONTROL->MPCC_BLND_ACTIVE_OVERLAP_ONLY from blend_cfg.overlap_only */
		uint32_t mpcc_global_alpha[MAX_PIPES];   /* MPCC_CONTROL->MPCC_GLOBAL_ALPHA from blend_cfg.global_alpha */
		uint32_t mpcc_global_gain[MAX_PIPES];    /* MPCC_CONTROL->MPCC_GLOBAL_GAIN from blend_cfg.global_gain */
		uint32_t mpcc_bg_bpc[MAX_PIPES];         /* MPCC_CONTROL->MPCC_BG_BPC from background color depth */
		uint32_t mpcc_bot_gain_mode[MAX_PIPES];  /* MPCC_CONTROL->MPCC_BOT_GAIN_MODE from bottom layer gain control */

		/* MPCC blending tree connections */
		uint32_t mpcc_bot_sel[MAX_PIPES];        /* MPCC_BOT_SEL->MPCC_BOT_SEL from mpcc_state->bot_sel */
		uint32_t mpcc_top_sel[MAX_PIPES];        /* MPCC_TOP_SEL->MPCC_TOP_SEL from mpcc_state->dpp_id */

		/* MPCC output gamma control */
		uint32_t mpcc_ogam_mode[MAX_PIPES];      /* MPCC_OGAM_CONTROL->MPCC_OGAM_MODE from output gamma mode */
		uint32_t mpcc_ogam_select[MAX_PIPES];    /* MPCC_OGAM_CONTROL->MPCC_OGAM_SELECT from gamma LUT bank selection */
		uint32_t mpcc_ogam_pwl_disable[MAX_PIPES]; /* MPCC_OGAM_CONTROL->MPCC_OGAM_PWL_DISABLE from PWL control */

		/* MPCC pipe assignment and status */
		uint32_t mpcc_opp_id[MAX_PIPES];         /* MPCC_OPP_ID->MPCC_OPP_ID from mpcc_state->opp_id */
		uint32_t mpcc_idle[MAX_PIPES];           /* MPCC_STATUS->MPCC_IDLE from mpcc idle status */
		uint32_t mpcc_busy[MAX_PIPES];           /* MPCC_STATUS->MPCC_BUSY from mpcc busy status */

		/* MPC output processing */
		uint32_t mpc_out_csc_mode;               /* MPC_OUT_CSC_COEF->MPC_OUT_CSC_MODE from output_csc */
		uint32_t mpc_out_gamma_mode;             /* MPC_OUT_GAMMA_LUT->MPC_OUT_GAMMA_MODE from output_gamma */
	} mpc;

	/* OPP register programming variables for each pipe */
	struct {
		/* Display Pattern Generator (DPG) Control - 19 fields from DPG_CONTROL register */
		uint32_t dpg_enable;                     /* DPG_CONTROL->DPG_EN from test_pattern parameter (enable/disable) */

		/* Format Control (FMT) - 18 fields from FMT_CONTROL register */
		uint32_t fmt_pixel_encoding;             /* FMT_CONTROL->FMT_PIXEL_ENCODING from clamping->pixel_encoding */
		uint32_t fmt_subsampling_mode;           /* FMT_CONTROL->FMT_SUBSAMPLING_MODE from force_chroma_subsampling_1tap */
		uint32_t fmt_cbcr_bit_reduction_bypass;  /* FMT_CONTROL->FMT_CBCR_BIT_REDUCTION_BYPASS from pixel_encoding bypass control */
		uint32_t fmt_stereosync_override;        /* FMT_CONTROL->FMT_STEREOSYNC_OVERRIDE from stereo timing override */
		uint32_t fmt_spatial_dither_frame_counter_max; /* FMT_CONTROL->FMT_SPATIAL_DITHER_FRAME_COUNTER_MAX from fmt_bit_depth->flags */
		uint32_t fmt_spatial_dither_frame_counter_bit_swap; /* FMT_CONTROL->FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP from dither control */
		uint32_t fmt_truncate_enable;            /* FMT_CONTROL->FMT_TRUNCATE_EN from fmt_bit_depth->flags.TRUNCATE_ENABLED */
		uint32_t fmt_truncate_depth;             /* FMT_CONTROL->FMT_TRUNCATE_DEPTH from fmt_bit_depth->flags.TRUNCATE_DEPTH */
		uint32_t fmt_truncate_mode;              /* FMT_CONTROL->FMT_TRUNCATE_MODE from fmt_bit_depth->flags.TRUNCATE_MODE */
		uint32_t fmt_spatial_dither_enable;      /* FMT_CONTROL->FMT_SPATIAL_DITHER_EN from fmt_bit_depth->flags.SPATIAL_DITHER_ENABLED */
		uint32_t fmt_spatial_dither_mode;        /* FMT_CONTROL->FMT_SPATIAL_DITHER_MODE from fmt_bit_depth->flags.SPATIAL_DITHER_MODE */
		uint32_t fmt_spatial_dither_depth;       /* FMT_CONTROL->FMT_SPATIAL_DITHER_DEPTH from fmt_bit_depth->flags.SPATIAL_DITHER_DEPTH */
		uint32_t fmt_temporal_dither_enable;     /* FMT_CONTROL->FMT_TEMPORAL_DITHER_EN from fmt_bit_depth->flags.TEMPORAL_DITHER_ENABLED */
		uint32_t fmt_clamp_data_enable;          /* FMT_CONTROL->FMT_CLAMP_DATA_EN from clamping->clamping_range enable */
		uint32_t fmt_clamp_color_format;         /* FMT_CONTROL->FMT_CLAMP_COLOR_FORMAT from clamping->color_format */
		uint32_t fmt_dynamic_exp_enable;         /* FMT_CONTROL->FMT_DYNAMIC_EXP_EN from color_sp/color_dpth/signal */
		uint32_t fmt_dynamic_exp_mode;           /* FMT_CONTROL->FMT_DYNAMIC_EXP_MODE from color space mode mapping */
		uint32_t fmt_bit_depth_control;          /* Legacy field - kept for compatibility */

		/* OPP Pipe Control - 1 field from OPP_PIPE_CONTROL register */
		uint32_t opp_pipe_clock_enable;          /* OPP_PIPE_CONTROL->OPP_PIPE_CLOCK_EN from enable parameter (bool) */

		/* OPP CRC Control - 3 fields from OPP_PIPE_CRC_CONTROL register */
		uint32_t opp_crc_enable;                 /* OPP_PIPE_CRC_CONTROL->CRC_EN from CRC enable control */
		uint32_t opp_crc_select_source;          /* OPP_PIPE_CRC_CONTROL->CRC_SELECT_SOURCE from CRC source selection */
		uint32_t opp_crc_stereo_cont;            /* OPP_PIPE_CRC_CONTROL->CRC_STEREO_CONT from stereo continuous CRC */

		/* Output Buffer (OPPBUF) Control - 6 fields from OPPBUF_CONTROL register */
		uint32_t oppbuf_active_width;            /* OPPBUF_CONTROL->OPPBUF_ACTIVE_WIDTH from oppbuf_params->active_width */
		uint32_t oppbuf_pixel_repetition;        /* OPPBUF_CONTROL->OPPBUF_PIXEL_REPETITION from oppbuf_params->pixel_repetition */
		uint32_t oppbuf_display_segmentation;    /* OPPBUF_CONTROL->OPPBUF_DISPLAY_SEGMENTATION from oppbuf_params->mso_segmentation */
		uint32_t oppbuf_overlap_pixel_num;       /* OPPBUF_CONTROL->OPPBUF_OVERLAP_PIXEL_NUM from oppbuf_params->mso_overlap_pixel_num */
		uint32_t oppbuf_3d_vact_space1_size;     /* OPPBUF_CONTROL->OPPBUF_3D_VACT_SPACE1_SIZE from 3D timing space1_size */
		uint32_t oppbuf_3d_vact_space2_size;     /* OPPBUF_CONTROL->OPPBUF_3D_VACT_SPACE2_SIZE from 3D timing space2_size */

		/* DSC Forward Config - 3 fields from DSCRM_DSC_FORWARD_CONFIG register */
		uint32_t dscrm_dsc_forward_enable;       /* DSCRM_DSC_FORWARD_CONFIG->DSCRM_DSC_FORWARD_EN from DSC forward enable control */
		uint32_t dscrm_dsc_opp_pipe_source;      /* DSCRM_DSC_FORWARD_CONFIG->DSCRM_DSC_OPP_PIPE_SOURCE from opp_pipe parameter */
		uint32_t dscrm_dsc_forward_enable_status; /* DSCRM_DSC_FORWARD_CONFIG->DSCRM_DSC_FORWARD_EN_STATUS from DSC forward status (read-only) */
	} opp[MAX_PIPES];

	/* OPTC register programming variables for each pipe */
	struct {
		uint32_t otg_master_inst;

		/* OTG_CONTROL register - 5 fields for OTG control */
		uint32_t otg_master_enable;              /* OTG_CONTROL->OTG_MASTER_EN from timing enable/disable control */
		uint32_t otg_disable_point_cntl;         /* OTG_CONTROL->OTG_DISABLE_POINT_CNTL from disable timing control */
		uint32_t otg_start_point_cntl;           /* OTG_CONTROL->OTG_START_POINT_CNTL from start timing control */
		uint32_t otg_field_number_cntl;          /* OTG_CONTROL->OTG_FIELD_NUMBER_CNTL from interlace field control */
		uint32_t otg_out_mux;                    /* OTG_CONTROL->OTG_OUT_MUX from output mux selection */

		/* OTG Horizontal Timing - 7 fields */
		uint32_t otg_h_total;                    /* OTG_H_TOTAL->OTG_H_TOTAL from dc_crtc_timing->h_total */
		uint32_t otg_h_blank_start;              /* OTG_H_BLANK_START_END->OTG_H_BLANK_START from dc_crtc_timing->h_front_porch */
		uint32_t otg_h_blank_end;                /* OTG_H_BLANK_START_END->OTG_H_BLANK_END from dc_crtc_timing->h_addressable_video_pixel_width */
		uint32_t otg_h_sync_start;               /* OTG_H_SYNC_A->OTG_H_SYNC_A_START from dc_crtc_timing->h_sync_width */
		uint32_t otg_h_sync_end;                 /* OTG_H_SYNC_A->OTG_H_SYNC_A_END from calculated sync end position */
		uint32_t otg_h_sync_polarity;            /* OTG_H_SYNC_A_CNTL->OTG_H_SYNC_A_POL from dc_crtc_timing->flags.HSYNC_POSITIVE_POLARITY */
		uint32_t otg_h_timing_div_mode;          /* OTG_H_TIMING_CNTL->OTG_H_TIMING_DIV_MODE from horizontal timing division mode */

		/* OTG Vertical Timing - 7 fields */
		uint32_t otg_v_total;                    /* OTG_V_TOTAL->OTG_V_TOTAL from dc_crtc_timing->v_total */
		uint32_t otg_v_blank_start;              /* OTG_V_BLANK_START_END->OTG_V_BLANK_START from dc_crtc_timing->v_front_porch */
		uint32_t otg_v_blank_end;                /* OTG_V_BLANK_START_END->OTG_V_BLANK_END from dc_crtc_timing->v_addressable_video_line_width */
		uint32_t otg_v_sync_start;               /* OTG_V_SYNC_A->OTG_V_SYNC_A_START from dc_crtc_timing->v_sync_width */
		uint32_t otg_v_sync_end;                 /* OTG_V_SYNC_A->OTG_V_SYNC_A_END from calculated sync end position */
		uint32_t otg_v_sync_polarity;            /* OTG_V_SYNC_A_CNTL->OTG_V_SYNC_A_POL from dc_crtc_timing->flags.VSYNC_POSITIVE_POLARITY */
		uint32_t otg_v_sync_mode;                /* OTG_V_SYNC_A_CNTL->OTG_V_SYNC_MODE from sync mode selection */

		/* OTG DRR (Dynamic Refresh Rate) Control - 8 fields */
		uint32_t otg_v_total_max;                /* OTG_V_TOTAL_MAX->OTG_V_TOTAL_MAX from drr_params->vertical_total_max */
		uint32_t otg_v_total_min;                /* OTG_V_TOTAL_MIN->OTG_V_TOTAL_MIN from drr_params->vertical_total_min */
		uint32_t otg_v_total_mid;                /* OTG_V_TOTAL_MID->OTG_V_TOTAL_MID from drr_params->vertical_total_mid */
		uint32_t otg_v_total_max_sel;            /* OTG_V_TOTAL_CONTROL->OTG_V_TOTAL_MAX_SEL from DRR max selection enable */
		uint32_t otg_v_total_min_sel;            /* OTG_V_TOTAL_CONTROL->OTG_V_TOTAL_MIN_SEL from DRR min selection enable */
		uint32_t otg_vtotal_mid_replacing_max_en; /* OTG_V_TOTAL_CONTROL->OTG_VTOTAL_MID_REPLACING_MAX_EN from DRR mid-frame enable */
		uint32_t otg_vtotal_mid_frame_num;       /* OTG_V_TOTAL_CONTROL->OTG_VTOTAL_MID_FRAME_NUM from drr_params->vertical_total_mid_frame_num */
		uint32_t otg_set_v_total_min_mask;       /* OTG_V_TOTAL_CONTROL->OTG_SET_V_TOTAL_MIN_MASK from DRR trigger mask */
		uint32_t otg_force_lock_on_event;        /* OTG_V_TOTAL_CONTROL->OTG_FORCE_LOCK_ON_EVENT from DRR force lock control */

		/* OPTC Data Source and ODM - 6 fields */
		uint32_t optc_seg0_src_sel;              /* OPTC_DATA_SOURCE_SELECT->OPTC_SEG0_SRC_SEL from opp_id[0] ODM segment 0 source */
		uint32_t optc_seg1_src_sel;              /* OPTC_DATA_SOURCE_SELECT->OPTC_SEG1_SRC_SEL from opp_id[1] ODM segment 1 source */
		uint32_t optc_seg2_src_sel;              /* OPTC_DATA_SOURCE_SELECT->OPTC_SEG2_SRC_SEL from opp_id[2] ODM segment 2 source */
		uint32_t optc_seg3_src_sel;              /* OPTC_DATA_SOURCE_SELECT->OPTC_SEG3_SRC_SEL from opp_id[3] ODM segment 3 source */
		uint32_t optc_num_of_input_segment;      /* OPTC_DATA_SOURCE_SELECT->OPTC_NUM_OF_INPUT_SEGMENT from opp_cnt-1 number of input segments */
		uint32_t optc_mem_sel;                   /* OPTC_MEMORY_CONFIG->OPTC_MEM_SEL from memory_mask ODM memory selection */

		/* OPTC Data Format and DSC - 4 fields */
		uint32_t optc_data_format;               /* OPTC_DATA_FORMAT_CONTROL->OPTC_DATA_FORMAT from data format selection */
		uint32_t optc_dsc_mode;                  /* OPTC_DATA_FORMAT_CONTROL->OPTC_DSC_MODE from dsc_mode parameter */
		uint32_t optc_dsc_bytes_per_pixel;       /* OPTC_BYTES_PER_PIXEL->OPTC_DSC_BYTES_PER_PIXEL from dsc_bytes_per_pixel parameter */
		uint32_t optc_segment_width;             /* OPTC_WIDTH_CONTROL->OPTC_SEGMENT_WIDTH from segment_width parameter */
		uint32_t optc_dsc_slice_width;           /* OPTC_WIDTH_CONTROL->OPTC_DSC_SLICE_WIDTH from dsc_slice_width parameter */

		/* OPTC Clock and Underflow Control - 4 fields */
		uint32_t optc_input_pix_clk_en;          /* OPTC_INPUT_CLOCK_CONTROL->OPTC_INPUT_PIX_CLK_EN from pixel clock enable */
		uint32_t optc_underflow_occurred_status; /* OPTC_INPUT_GLOBAL_CONTROL->OPTC_UNDERFLOW_OCCURRED_STATUS from underflow status (read-only) */
		uint32_t optc_underflow_clear;           /* OPTC_INPUT_GLOBAL_CONTROL->OPTC_UNDERFLOW_CLEAR from underflow clear control */
		uint32_t otg_clock_enable;               /* OTG_CLOCK_CONTROL->OTG_CLOCK_EN from OTG clock enable */
		uint32_t otg_clock_gate_dis;             /* OTG_CLOCK_CONTROL->OTG_CLOCK_GATE_DIS from clock gate disable */

		/* OTG Stereo and 3D Control - 6 fields */
		uint32_t otg_stereo_enable;              /* OTG_STEREO_CONTROL->OTG_STEREO_EN from stereo enable control */
		uint32_t otg_stereo_sync_output_line_num; /* OTG_STEREO_CONTROL->OTG_STEREO_SYNC_OUTPUT_LINE_NUM from timing->stereo_3d_format line num */
		uint32_t otg_stereo_sync_output_polarity; /* OTG_STEREO_CONTROL->OTG_STEREO_SYNC_OUTPUT_POLARITY from stereo polarity control */
		uint32_t otg_3d_structure_en;            /* OTG_3D_STRUCTURE_CONTROL->OTG_3D_STRUCTURE_EN from 3D structure enable */
		uint32_t otg_3d_structure_v_update_mode; /* OTG_3D_STRUCTURE_CONTROL->OTG_3D_STRUCTURE_V_UPDATE_MODE from 3D vertical update mode */
		uint32_t otg_3d_structure_stereo_sel_ovr; /* OTG_3D_STRUCTURE_CONTROL->OTG_3D_STRUCTURE_STEREO_SEL_OVR from 3D stereo selection override */
		uint32_t otg_interlace_enable;           /* OTG_INTERLACE_CONTROL->OTG_INTERLACE_ENABLE from dc_crtc_timing->flags.INTERLACE */

		/* OTG GSL (Global Sync Lock) Control - 5 fields */
		uint32_t otg_gsl0_en;                    /* OTG_GSL_CONTROL->OTG_GSL0_EN from GSL group 0 enable */
		uint32_t otg_gsl1_en;                    /* OTG_GSL_CONTROL->OTG_GSL1_EN from GSL group 1 enable */
		uint32_t otg_gsl2_en;                    /* OTG_GSL_CONTROL->OTG_GSL2_EN from GSL group 2 enable */
		uint32_t otg_gsl_master_en;              /* OTG_GSL_CONTROL->OTG_GSL_MASTER_EN from GSL master enable */
		uint32_t otg_gsl_master_mode;            /* OTG_GSL_CONTROL->OTG_GSL_MASTER_MODE from gsl_params->gsl_master mode */

		/* OTG DRR Advanced Control - 4 fields */
		uint32_t otg_v_total_last_used_by_drr;   /* OTG_DRR_CONTROL->OTG_V_TOTAL_LAST_USED_BY_DRR from last used DRR V_TOTAL (read-only) */
		uint32_t otg_drr_trigger_window_start_x; /* OTG_DRR_TRIGGER_WINDOW->OTG_DRR_TRIGGER_WINDOW_START_X from window_start parameter */
		uint32_t otg_drr_trigger_window_end_x;   /* OTG_DRR_TRIGGER_WINDOW->OTG_DRR_TRIGGER_WINDOW_END_X from window_end parameter */
		uint32_t otg_drr_v_total_change_limit;   /* OTG_DRR_V_TOTAL_CHANGE->OTG_DRR_V_TOTAL_CHANGE_LIMIT from limit parameter */

		/* OTG DSC Position Control - 2 fields */
		uint32_t otg_dsc_start_position_x;       /* OTG_DSC_START_POSITION->OTG_DSC_START_POSITION_X from DSC start X position */
		uint32_t otg_dsc_start_position_line_num; /* OTG_DSC_START_POSITION->OTG_DSC_START_POSITION_LINE_NUM from DSC start line number */

		/* OTG Double Buffer Control - 2 fields */
		uint32_t otg_drr_timing_dbuf_update_mode; /* OTG_DOUBLE_BUFFER_CONTROL->OTG_DRR_TIMING_DBUF_UPDATE_MODE from DRR double buffer mode */
		uint32_t otg_blank_data_double_buffer_en; /* OTG_DOUBLE_BUFFER_CONTROL->OTG_BLANK_DATA_DOUBLE_BUFFER_EN from blank data double buffer enable */

		/* OTG Vertical Interrupts - 6 fields */
		uint32_t otg_vertical_interrupt0_int_enable; /* OTG_VERTICAL_INTERRUPT0_CONTROL->OTG_VERTICAL_INTERRUPT0_INT_ENABLE from interrupt 0 enable */
		uint32_t otg_vertical_interrupt0_line_start; /* OTG_VERTICAL_INTERRUPT0_POSITION->OTG_VERTICAL_INTERRUPT0_LINE_START from start_line parameter */
		uint32_t otg_vertical_interrupt1_int_enable; /* OTG_VERTICAL_INTERRUPT1_CONTROL->OTG_VERTICAL_INTERRUPT1_INT_ENABLE from interrupt 1 enable */
		uint32_t otg_vertical_interrupt1_line_start; /* OTG_VERTICAL_INTERRUPT1_POSITION->OTG_VERTICAL_INTERRUPT1_LINE_START from start_line parameter */
		uint32_t otg_vertical_interrupt2_int_enable; /* OTG_VERTICAL_INTERRUPT2_CONTROL->OTG_VERTICAL_INTERRUPT2_INT_ENABLE from interrupt 2 enable */
		uint32_t otg_vertical_interrupt2_line_start; /* OTG_VERTICAL_INTERRUPT2_POSITION->OTG_VERTICAL_INTERRUPT2_LINE_START from start_line parameter */

		/* OTG Global Sync Parameters - 6 fields */
		uint32_t otg_vready_offset;              /* OTG_VREADY_PARAM->OTG_VREADY_OFFSET from vready_offset parameter */
		uint32_t otg_vstartup_start;             /* OTG_VSTARTUP_PARAM->OTG_VSTARTUP_START from vstartup_start parameter */
		uint32_t otg_vupdate_offset;             /* OTG_VUPDATE_PARAM->OTG_VUPDATE_OFFSET from vupdate_offset parameter */
		uint32_t otg_vupdate_width;              /* OTG_VUPDATE_PARAM->OTG_VUPDATE_WIDTH from vupdate_width parameter */
		uint32_t master_update_lock_vupdate_keepout_start_offset; /* OTG_VUPDATE_KEEPOUT->MASTER_UPDATE_LOCK_VUPDATE_KEEPOUT_START_OFFSET from pstate_keepout start */
		uint32_t master_update_lock_vupdate_keepout_end_offset;   /* OTG_VUPDATE_KEEPOUT->MASTER_UPDATE_LOCK_VUPDATE_KEEPOUT_END_OFFSET from pstate_keepout end */

		/* OTG Manual Trigger Control - 11 fields */
		uint32_t otg_triga_source_select;        /* OTG_TRIGA_CNTL->OTG_TRIGA_SOURCE_SELECT from trigger A source selection */
		uint32_t otg_triga_source_pipe_select;   /* OTG_TRIGA_CNTL->OTG_TRIGA_SOURCE_PIPE_SELECT from trigger A pipe selection */
		uint32_t otg_triga_rising_edge_detect_cntl; /* OTG_TRIGA_CNTL->OTG_TRIGA_RISING_EDGE_DETECT_CNTL from trigger A rising edge detect */
		uint32_t otg_triga_falling_edge_detect_cntl; /* OTG_TRIGA_CNTL->OTG_TRIGA_FALLING_EDGE_DETECT_CNTL from trigger A falling edge detect */
		uint32_t otg_triga_polarity_select;      /* OTG_TRIGA_CNTL->OTG_TRIGA_POLARITY_SELECT from trigger A polarity selection */
		uint32_t otg_triga_frequency_select;     /* OTG_TRIGA_CNTL->OTG_TRIGA_FREQUENCY_SELECT from trigger A frequency selection */
		uint32_t otg_triga_delay;                /* OTG_TRIGA_CNTL->OTG_TRIGA_DELAY from trigger A delay */
		uint32_t otg_triga_clear;                /* OTG_TRIGA_CNTL->OTG_TRIGA_CLEAR from trigger A clear */
		uint32_t otg_triga_manual_trig;          /* OTG_TRIGA_MANUAL_TRIG->OTG_TRIGA_MANUAL_TRIG from manual trigger A */
		uint32_t otg_trigb_source_select;        /* OTG_TRIGB_CNTL->OTG_TRIGB_SOURCE_SELECT from trigger B source selection */
		uint32_t otg_trigb_polarity_select;      /* OTG_TRIGB_CNTL->OTG_TRIGB_POLARITY_SELECT from trigger B polarity selection */
		uint32_t otg_trigb_manual_trig;          /* OTG_TRIGB_MANUAL_TRIG->OTG_TRIGB_MANUAL_TRIG from manual trigger B */

		/* OTG Static Screen and Update Control - 6 fields */
		uint32_t otg_static_screen_event_mask;   /* OTG_STATIC_SCREEN_CONTROL->OTG_STATIC_SCREEN_EVENT_MASK from event_triggers parameter */
		uint32_t otg_static_screen_frame_count;  /* OTG_STATIC_SCREEN_CONTROL->OTG_STATIC_SCREEN_FRAME_COUNT from num_frames parameter */
		uint32_t master_update_lock;             /* OTG_MASTER_UPDATE_LOCK->MASTER_UPDATE_LOCK from update lock control */
		uint32_t master_update_mode;             /* OTG_MASTER_UPDATE_MODE->MASTER_UPDATE_MODE from update mode selection */
		uint32_t otg_force_count_now_mode;       /* OTG_FORCE_COUNT_NOW_CNTL->OTG_FORCE_COUNT_NOW_MODE from force count mode */
		uint32_t otg_force_count_now_clear;      /* OTG_FORCE_COUNT_NOW_CNTL->OTG_FORCE_COUNT_NOW_CLEAR from force count clear */

		/* VTG Control - 3 fields */
		uint32_t vtg0_enable;                    /* CONTROL->VTG0_ENABLE from VTG enable control */
		uint32_t vtg0_fp2;                       /* CONTROL->VTG0_FP2 from VTG front porch 2 */
		uint32_t vtg0_vcount_init;               /* CONTROL->VTG0_VCOUNT_INIT from VTG vertical count init */

		/* OTG Status (Read-Only) - 12 fields */
		uint32_t otg_v_blank;                    /* OTG_STATUS->OTG_V_BLANK from vertical blank status (read-only) */
		uint32_t otg_v_active_disp;              /* OTG_STATUS->OTG_V_ACTIVE_DISP from vertical active display (read-only) */
		uint32_t otg_frame_count;                /* OTG_STATUS_FRAME_COUNT->OTG_FRAME_COUNT from frame count (read-only) */
		uint32_t otg_horz_count;                 /* OTG_STATUS_POSITION->OTG_HORZ_COUNT from horizontal position (read-only) */
		uint32_t otg_vert_count;                 /* OTG_STATUS_POSITION->OTG_VERT_COUNT from vertical position (read-only) */
		uint32_t otg_horz_count_hv;              /* OTG_STATUS_HV_COUNT->OTG_HORZ_COUNT from horizontal count (read-only) */
		uint32_t otg_vert_count_nom;             /* OTG_STATUS_HV_COUNT->OTG_VERT_COUNT_NOM from vertical count nominal (read-only) */
		uint32_t otg_flip_pending;               /* OTG_PIPE_UPDATE_STATUS->OTG_FLIP_PENDING from flip pending status (read-only) */
		uint32_t otg_dc_reg_update_pending;      /* OTG_PIPE_UPDATE_STATUS->OTG_DC_REG_UPDATE_PENDING from DC register update pending (read-only) */
		uint32_t otg_cursor_update_pending;      /* OTG_PIPE_UPDATE_STATUS->OTG_CURSOR_UPDATE_PENDING from cursor update pending (read-only) */
		uint32_t otg_vupdate_keepout_status;     /* OTG_PIPE_UPDATE_STATUS->OTG_VUPDATE_KEEPOUT_STATUS from VUPDATE keepout status (read-only) */
	} optc[MAX_PIPES];

	/* Metadata */
	uint32_t active_pipe_count;
	uint32_t active_stream_count;
	bool state_valid;
};

/**
 * dc_capture_register_software_state() - Capture software state for register programming
 * @dc: DC context containing current display configuration
 * @state: Pointer to dc_register_software_state structure to populate
 *
 * Extracts all software state variables that are used to program hardware register
 * fields across the display driver pipeline. This provides a complete snapshot
 * of the software configuration that drives hardware register programming.
 *
 * The function traverses the DC context and extracts values from:
 * - Stream configurations (timing, format, DSC settings)
 * - Plane states (surface format, rotation, scaling, cursor)
 * - Pipe contexts (resource allocation, blending, viewport)
 * - Clock manager (display clocks, DPP clocks, pixel clocks)
 * - Resource context (DET buffer allocation, ODM configuration)
 *
 * This is essential for underflow debugging as it captures the exact software
 * state that determines how registers are programmed, allowing analysis of
 * whether underflow is caused by incorrect register programming or timing issues.
 *
 * Return: true if state was successfully captured, false on error
 */
bool dc_capture_register_software_state(struct dc *dc, struct dc_register_software_state *state);

/**
 * dc_get_qos_info() - Retrieve Quality of Service (QoS) information from display core
 * @dc: DC context containing current display configuration
 * @info: Pointer to dc_qos_info structure to populate with QoS metrics
 *
 * This function retrieves QoS metrics from the display core that can be used by
 * benchmark tools to analyze display system performance. The function may take
 * several milliseconds to execute due to hardware measurement requirements.
 *
 * QoS information includes:
 * - Bandwidth bounds (lower limits in Mbps)
 * - Latency bounds (upper limits in nanoseconds)
 * - Hardware-measured bandwidth metrics (peak/average in Mbps)
 * - Hardware-measured latency metrics (maximum/average in nanoseconds)
 *
 * The function will populate the provided dc_qos_info structure with current
 * QoS measurements. If hardware measurement functions are not available for
 * the current DCN version, the function returns false with zero'd info structure.
 *
 * Return: true if QoS information was successfully retrieved, false if measurement
 *         functions are unavailable or hardware measurements cannot be performed
 */
bool dc_get_qos_info(struct dc *dc, struct dc_qos_info *info);

#endif /* DC_INTERFACE_H_ */
