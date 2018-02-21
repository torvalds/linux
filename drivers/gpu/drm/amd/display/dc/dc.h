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
#include "gpio_types.h"
#include "link_service_types.h"
#include "grph_object_ctrl_defs.h"
#include <inc/hw/opp.h>

#include "inc/hw_sequencer.h"
#include "inc/compressor.h"
#include "dml/display_mode_lib.h"

#define DC_VER "3.1.40"

#define MAX_SURFACES 3
#define MAX_STREAMS 6
#define MAX_SINKS_PER_LINK 4


/*******************************************************************************
 * Display Core Interfaces
 ******************************************************************************/
struct dmcu_version {
	unsigned int date;
	unsigned int month;
	unsigned int year;
	unsigned int interface_version;
};

struct dc_versions {
	const char *dc_ver;
	struct dmcu_version dmcu_version;
};

struct dc_caps {
	uint32_t max_streams;
	uint32_t max_links;
	uint32_t max_audios;
	uint32_t max_slave_planes;
	uint32_t max_planes;
	uint32_t max_downscale_ratio;
	uint32_t i2c_speed_in_khz;
	unsigned int max_cursor_size;
	unsigned int max_video_width;
	int linear_pitch_alignment;
	bool dcc_const_color;
	bool dynamic_audio;
	bool is_apu;
	bool dual_link_dvi;
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

struct dc_static_screen_events {
	bool force_trigger;
	bool cursor_update;
	bool surface_update;
	bool overlay_update;
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


/* Structure to hold configuration flags set by dm at dc creation. */
struct dc_config {
	bool gpu_vm_support;
	bool disable_disp_pll_sharing;
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

struct dc_clocks {
	int dispclk_khz;
	int max_supported_dppclk_khz;
	int dppclk_khz;
	int dcfclk_khz;
	int socclk_khz;
	int dcfclk_deep_sleep_khz;
	int fclk_khz;
};

struct dc_debug {
	bool surface_visual_confirm;
	bool sanity_checks;
	bool max_disp_clk;
	bool surface_trace;
	bool timing_trace;
	bool clock_trace;
	bool validation_trace;
	bool bandwidth_calcs_trace;

	/* stutter efficiency related */
	bool disable_stutter;
	bool use_max_lb;
	enum dcc_option disable_dcc;
	enum pipe_split_policy pipe_split_policy;
	bool force_single_disp_pipe_split;
	bool voltage_align_fclk;

	bool disable_dfs_bypass;
	bool disable_dpp_power_gate;
	bool disable_hubp_power_gate;
	bool disable_pplib_wm_range;
	enum wm_report_mode pplib_wm_report_mode;
	unsigned int min_disp_clk_khz;
	int sr_exit_time_dpm0_ns;
	int sr_enter_plus_exit_time_dpm0_ns;
	int sr_exit_time_ns;
	int sr_enter_plus_exit_time_ns;
	int urgent_latency_ns;
	int percent_of_ideal_drambw;
	int dram_clock_change_latency_ns;
	int always_scale;
	bool disable_pplib_clock_request;
	bool disable_clock_gate;
	bool disable_dmcu;
	bool disable_psr;
	bool force_abm_enable;
	bool disable_hbup_pg;
	bool disable_dpp_pg;
	bool disable_stereo_support;
	bool vsr_support;
	bool performance_trace;
	bool az_endpoint_mute_only;
	bool always_use_regamma;
	bool p010_mpo_support;
};
struct dc_state;
struct resource_pool;
struct dce_hwseq;
struct dc {
	struct dc_versions versions;
	struct dc_caps caps;
	struct dc_cap_funcs cap_funcs;
	struct dc_config config;
	struct dc_debug debug;

	struct dc_context *ctx;

	uint8_t link_count;
	struct dc_link *links[MAX_PIPES * 2];

	struct dc_state *current_state;
	struct resource_pool *res_pool;

	/* Display Engine Clock levels */
	struct dm_pp_clock_levels sclk_lvls;

	/* Inputs into BW and WM calculations. */
	struct bw_calcs_dceip *bw_dceip;
	struct bw_calcs_vbios *bw_vbios;
#ifdef CONFIG_DRM_AMD_DC_DCN1_0
	struct dcn_soc_bounding_box *dcn_soc;
	struct dcn_ip_params *dcn_ip;
	struct display_mode_lib dml;
#endif

	/* HW functions */
	struct hw_sequencer_funcs hwss;
	struct dce_hwseq *hwseq;

	/* temp store of dm_pp_display_configuration
	 * to compare to see if display config changed
	 */
	struct dm_pp_display_configuration prev_display_config;

	bool optimized_required;

	bool apply_edp_fast_boot_optimization;

	/* FBC compressor */
#if defined(CONFIG_DRM_AMD_DC_FBC)
	struct compressor *fbc_compressor;
#endif
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

	int num_virtual_links;
	/*
	 * If 'vbios_override' not NULL, it will be called instead
	 * of the real VBIOS. Intended use is Diagnostics on FPGA.
	 */
	struct dc_bios *vbios_override;
	enum dce_environment dce_environment;

	struct dc_config flags;
	uint32_t log_mask;
};

struct dc *dc_create(const struct dc_init_data *init_params);

void dc_destroy(struct dc **dc);

/*******************************************************************************
 * Surface Interfaces
 ******************************************************************************/

enum {
	TRANSFER_FUNC_POINTS = 1025
};

// Moved here from color module for linux
enum color_transfer_func {
	transfer_func_unknown,
	transfer_func_srgb,
	transfer_func_bt709,
	transfer_func_pq2084,
	transfer_func_pq2084_interim,
	transfer_func_linear_0_1,
	transfer_func_linear_0_125,
	transfer_func_dolbyvision,
	transfer_func_gamma_22,
	transfer_func_gamma_26
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

	bool hdr_supported;
	bool is_hdr;
};

enum dc_transfer_func_type {
	TF_TYPE_PREDEFINED,
	TF_TYPE_DISTRIBUTED_POINTS,
	TF_TYPE_BYPASS,
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
};

struct dc_transfer_func {
	struct kref refcount;
	struct dc_transfer_func_distributed_points tf_pts;
	enum dc_transfer_func_type type;
	enum dc_transfer_func_predefined tf;
	/* FP16 1.0 reference level in nits, default is 80 nits, only for PQ*/
	uint32_t sdr_ref_white_level;
	struct dc_context *ctx;
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
		/* Medium updates */
		uint32_t dcc_change:1;
		uint32_t color_space_change:1;
		uint32_t input_tf_change:1;
		uint32_t horizontal_mirror_change:1;
		uint32_t per_pixel_alpha_change:1;
		uint32_t rotation_change:1;
		uint32_t swizzle_change:1;
		uint32_t scaling_change:1;
		uint32_t position_change:1;
		uint32_t in_transfer_func_change:1;
		uint32_t input_csc_change:1;
		uint32_t output_tf_change:1;
		uint32_t pixel_format_change:1;

		/* Full updates */
		uint32_t new_plane:1;
		uint32_t bpp_change:1;
		uint32_t gamma_change:1;
		uint32_t bandwidth_change:1;
		uint32_t clock_change:1;
		uint32_t stereo_format_change:1;
		uint32_t full_update:1;
	} bits;

	uint32_t raw;
};

struct dc_plane_state {
	struct dc_plane_address address;
	struct dc_plane_flip_time time;
	struct scaling_taps scaling_quality;
	struct rect src_rect;
	struct rect dst_rect;
	struct rect clip_rect;

	union plane_size plane_size;
	union dc_tiling_info tiling_info;

	struct dc_plane_dcc_param dcc;

	struct dc_gamma *gamma_correction;
	struct dc_transfer_func *in_transfer_func;
	struct dc_bias_and_scale *bias_and_scale;
	struct csc_transform input_csc_color_matrix;
	struct fixed31_32 coeff_reduction_factor;
	uint32_t sdr_white_level;

	// TODO: No longer used, remove
	struct dc_hdr_static_metadata hdr_static_ctx;

	enum dc_color_space color_space;
	enum color_transfer_func input_tf;

	enum surface_pixel_format format;
	enum dc_rotation_angle rotation;
	enum plane_stereo_format stereo_format;

	bool is_tiling_rotated;
	bool per_pixel_alpha;
	bool visible;
	bool flip_immediate;
	bool horizontal_mirror;

	union surface_update_flags update_flags;
	/* private to DC core */
	struct dc_plane_status status;
	struct dc_context *ctx;

	/* private to dc_surface.c */
	enum dc_irq_source irq_source;
	struct kref refcount;
};

struct dc_plane_info {
	union plane_size plane_size;
	union dc_tiling_info tiling_info;
	struct dc_plane_dcc_param dcc;
	enum surface_pixel_format format;
	enum dc_rotation_angle rotation;
	enum plane_stereo_format stereo_format;
	enum dc_color_space color_space;
	enum color_transfer_func input_tf;
	unsigned int sdr_white_level;
	bool horizontal_mirror;
	bool visible;
	bool per_pixel_alpha;
	bool input_csc_enabled;
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
	struct dc_flip_addrs *flip_addr;
	struct dc_plane_info *plane_info;
	struct dc_scaling_info *scaling_info;

	/* following updates require alloc/sleep/spin that is not isr safe,
	 * null means no updates
	 */
	struct dc_gamma *gamma;
	enum color_transfer_func color_input_tf;
	struct dc_transfer_func *in_transfer_func;

	struct csc_transform *input_csc_color_matrix;
	struct fixed31_32 *coeff_reduction_factor;
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
};

bool dc_post_update_surfaces_to_stream(
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

enum dc_status dc_validate_plane(struct dc *dc, const struct dc_plane_state *plane_state);

enum dc_status dc_validate_global_state(
		struct dc *dc,
		struct dc_state *new_ctx);


void dc_resource_state_construct(
		const struct dc *dc,
		struct dc_state *dst_ctx);

void dc_resource_state_copy_construct(
		const struct dc_state *src_ctx,
		struct dc_state *dst_ctx);

void dc_resource_state_copy_construct_current(
		const struct dc *dc,
		struct dc_state *dst_ctx);

void dc_resource_state_destruct(struct dc_state *context);

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


struct dc_state *dc_create_state(void);
void dc_retain_state(struct dc_state *context);
void dc_release_state(struct dc_state *context);

/*******************************************************************************
 * Link Interfaces
 ******************************************************************************/

struct dpcd_caps {
	union dpcd_rev dpcd_rev;
	union max_lane_count max_ln_count;
	union max_down_spread max_down_spread;

	/* dongle type (DP converter, CV smart dongle) */
	enum display_dongle_type dongle_type;
	/* Dongle's downstream count. */
	union sink_count sink_count;
	/* If dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER,
	indicates 'Frame Sequential-to-lllFrame Pack' conversion capability.*/
	struct dc_dongle_caps dongle_caps;

	uint32_t sink_dev_id;
	uint32_t branch_dev_id;
	int8_t branch_dev_name[6];
	int8_t branch_hw_revision;

	bool allow_invalid_MSA_timing_param;
	bool panel_mode_edp;
	bool dpcd_display_control_capable;
};

#include "dc_link.h"

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

	/* private to DC core */
	struct dc_link *link;
	struct dc_context *ctx;

	/* private to dc_sink.c */
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

/*******************************************************************************
 * Power Interfaces
 ******************************************************************************/

void dc_set_power_state(
		struct dc *dc,
		enum dc_acpi_cm_power_state power_state);
void dc_resume(struct dc *dc);

#endif /* DC_INTERFACE_H_ */
