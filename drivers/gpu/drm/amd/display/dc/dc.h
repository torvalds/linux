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

#define MAX_SURFACES 3
#define MAX_STREAMS 6
#define MAX_SINKS_PER_LINK 4

/*******************************************************************************
 * Display Core Interfaces
 ******************************************************************************/

struct dc_caps {
	uint32_t max_streams;
	uint32_t max_links;
	uint32_t max_audios;
	uint32_t max_slave_planes;
	uint32_t max_downscale_ratio;
	uint32_t i2c_speed_in_khz;
};


struct dc_dcc_surface_param {
	enum surface_pixel_format format;
	struct dc_size surface_size;
	enum dc_scan_direction scan;
};

struct dc_dcc_setting {
	unsigned int max_compressed_blk_size;
	unsigned int max_uncompressed_blk_size;
	bool independent_64b_blks;
};

struct dc_surface_dcc_cap {
	bool capable;
	bool const_color_support;

	union {
		struct {
			struct dc_dcc_setting rgb;
		} grph;

		struct {
			struct dc_dcc_setting luma;
			struct dc_dcc_setting chroma;
		} video;
	};
};

/* Forward declaration*/
struct dc;
struct dc_surface;
struct validate_context;

struct dc_cap_funcs {
	int i;
};

struct dc_stream_funcs {
	bool (*adjust_vmin_vmax)(struct dc *dc,
			const struct dc_stream **stream,
			int num_streams,
			int vmin,
			int vmax);

	void (*stream_update_scaling)(const struct dc *dc,
			const struct dc_stream *dc_stream,
			const struct rect *src,
			const struct rect *dst);
	bool (*set_gamut_remap)(struct dc *dc,
			const struct dc_stream **stream, int num_streams);
	bool (*set_backlight)(struct dc *dc, unsigned int backlight_level,
		unsigned int frame_ramp, const struct dc_stream *stream);
	bool (*init_dmcu_backlight_settings)(struct dc *dc);
	bool (*set_abm_level)(struct dc *dc, unsigned int abm_level);
	bool (*set_psr_enable)(struct dc *dc, bool enable);
	bool (*setup_psr)(struct dc *dc, const struct dc_stream *stream);
};

struct link_training_settings;

struct dc_link_funcs {
	void (*set_drive_settings)(struct dc *dc,
			struct link_training_settings *lt_settings,
			const struct dc_link *link);
	void (*perform_link_training)(struct dc *dc,
			struct dc_link_settings *link_setting,
			bool skip_video_pattern);
	void (*set_preferred_link_settings)(struct dc *dc,
			struct dc_link_settings *link_setting,
			const struct dc_link *link);
	void (*enable_hpd)(const struct dc_link *link);
	void (*disable_hpd)(const struct dc_link *link);
	void (*set_test_pattern)(
			const struct dc_link *link,
			enum dp_test_pattern test_pattern,
			const struct link_training_settings *p_link_settings,
			const unsigned char *p_custom_pattern,
			unsigned int cust_pattern_size);
};

/* Structure to hold configuration flags set by dm at dc creation. */
struct dc_config {
	bool gpu_vm_support;
	bool disable_disp_pll_sharing;
};

struct dc_debug {
	bool surface_visual_confirm;
	bool max_disp_clk;
	bool surface_trace;
	bool timing_trace;
	bool validation_trace;
	bool disable_stutter;
	bool disable_dcc;
	bool disable_dfs_bypass;
	bool disable_clock_gate;
	bool disable_dmcu;
	bool disable_color_module;
};

struct dc {
	struct dc_caps caps;
	struct dc_cap_funcs cap_funcs;
	struct dc_stream_funcs stream_funcs;
	struct dc_link_funcs link_funcs;
	struct dc_config config;
	struct dc_debug debug;
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
};

struct dc *dc_create(const struct dc_init_data *init_params);

void dc_destroy(struct dc **dc);

/*******************************************************************************
 * Surface Interfaces
 ******************************************************************************/

enum {
	TRANSFER_FUNC_POINTS = 1025
};

struct dc_hdr_static_metadata {
	bool is_hdr;

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
};

struct dc_transfer_func {
	enum dc_transfer_func_type type;
	enum dc_transfer_func_predefined tf;
	struct dc_transfer_func_distributed_points tf_pts;
};

struct dc_surface {
	bool visible;
	bool flip_immediate;
	struct dc_plane_address address;

	struct scaling_taps scaling_quality;
	struct rect src_rect;
	struct rect dst_rect;
	struct rect clip_rect;

	union plane_size plane_size;
	union dc_tiling_info tiling_info;
	struct dc_plane_dcc_param dcc;
	enum dc_color_space color_space;

	enum surface_pixel_format format;
	enum dc_rotation_angle rotation;
	bool horizontal_mirror;
	enum plane_stereo_format stereo_format;

	struct dc_hdr_static_metadata hdr_static_ctx;

	const struct dc_gamma *gamma_correction;
	const struct dc_transfer_func *in_transfer_func;
};

struct dc_plane_info {
	union plane_size plane_size;
	union dc_tiling_info tiling_info;
	struct dc_plane_dcc_param dcc;
	enum surface_pixel_format format;
	enum dc_rotation_angle rotation;
	bool horizontal_mirror;
	enum plane_stereo_format stereo_format;
	enum dc_color_space color_space; /*todo: wrong place, fits in scaling info*/
	bool visible;
};

struct dc_scaling_info {
		struct rect src_rect;
		struct rect dst_rect;
		struct rect clip_rect;
		struct scaling_taps scaling_quality;
};

struct dc_surface_update {
	const struct dc_surface *surface;

	/* isr safe update parameters.  null means no updates */
	struct dc_flip_addrs *flip_addr;
	struct dc_plane_info *plane_info;
	struct dc_scaling_info *scaling_info;
	/* following updates require alloc/sleep/spin that is not isr safe,
	 * null means no updates
	 */
	/* gamma TO BE REMOVED */
	struct dc_gamma *gamma;
	struct dc_hdr_static_metadata *hdr_static_metadata;
	struct dc_transfer_func *in_transfer_func;
	struct dc_transfer_func *out_transfer_func;


};
/*
 * This structure is filled in by dc_surface_get_status and contains
 * the last requested address and the currently active address so the called
 * can determine if there are any outstanding flips
 */
struct dc_surface_status {
	struct dc_plane_address requested_address;
	struct dc_plane_address current_address;
	bool is_flip_pending;
};

/*
 * Create a new surface with default parameters;
 */
struct dc_surface *dc_create_surface(const struct dc *dc);
const struct dc_surface_status *dc_surface_get_status(
		const struct dc_surface *dc_surface);

void dc_surface_retain(const struct dc_surface *dc_surface);
void dc_surface_release(const struct dc_surface *dc_surface);

void dc_gamma_retain(const struct dc_gamma *dc_gamma);
void dc_gamma_release(const struct dc_gamma **dc_gamma);
struct dc_gamma *dc_create_gamma(void);

void dc_transfer_func_retain(const struct dc_transfer_func *dc_tf);
void dc_transfer_func_release(const struct dc_transfer_func *dc_tf);
struct dc_transfer_func *dc_create_transfer_func(void);

/*
 * This structure holds a surface address.  There could be multiple addresses
 * in cases such as Stereo 3D, Planar YUV, etc.  Other per-flip attributes such
 * as frame durations and DCC format can also be set.
 */
struct dc_flip_addrs {
	struct dc_plane_address address;
	bool flip_immediate;
	/* TODO: DCC format info */
	/* TODO: add flip duration for FreeSync */
};

/*
 * Optimized flip address update function.
 *
 * After this call:
 *   Surface addresses and flip attributes are programmed.
 *   Surface flip occur at next configured time (h_sync or v_sync flip)
 */
void dc_flip_surface_addrs(struct dc *dc,
		const struct dc_surface *const surfaces[],
		struct dc_flip_addrs flip_addrs[],
		uint32_t count);

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

bool dc_commit_surfaces_to_stream(
		struct dc *dc,
		const struct dc_surface **dc_surfaces,
		uint8_t surface_count,
		const struct dc_stream *stream);

bool dc_pre_update_surfaces_to_stream(
		struct dc *dc,
		const struct dc_surface *const *new_surfaces,
		uint8_t new_surface_count,
		const struct dc_stream *stream);

bool dc_post_update_surfaces_to_stream(
		struct dc *dc);

void dc_update_surfaces_for_stream(struct dc *dc, struct dc_surface_update *updates,
		int surface_count, const struct dc_stream *stream);

/*******************************************************************************
 * Stream Interfaces
 ******************************************************************************/
struct dc_stream {
	const struct dc_sink *sink;
	struct dc_crtc_timing timing;

	enum dc_color_space output_color_space;

	struct rect src; /* composition area */
	struct rect dst; /* stream addressable area */

	struct audio_info audio_info;

	bool ignore_msa_timing_param;

	struct freesync_context freesync_ctx;

	const struct dc_transfer_func *out_transfer_func;
	struct colorspace_transform gamut_remap_matrix;
	struct csc_transform csc_color_matrix;

	/* TODO: dithering */
	/* TODO: custom INFO packets */
	/* TODO: ABM info (DMCU) */
	/* TODO: PSR info */
	/* TODO: CEA VIC */
};

/*
 * Log the current stream state.
 */
void dc_stream_log(
	const struct dc_stream *stream,
	struct dal_logger *dc_logger,
	enum dc_log_type log_type);

uint8_t dc_get_current_stream_count(const struct dc *dc);
struct dc_stream *dc_get_stream_at_index(const struct dc *dc, uint8_t i);

/*
 * Return the current frame counter.
 */
uint32_t dc_stream_get_vblank_counter(const struct dc_stream *stream);

/* TODO: Return parsed values rather than direct register read
 * This has a dependency on the caller (amdgpu_get_crtc_scanoutpos)
 * being refactored properly to be dce-specific
 */
uint32_t dc_stream_get_scanoutpos(
		const struct dc_stream *stream, uint32_t *vbl, uint32_t *position);

/*
 * Structure to store surface/stream associations for validation
 */
struct dc_validation_set {
	const struct dc_stream *stream;
	const struct dc_surface *surfaces[MAX_SURFACES];
	uint8_t surface_count;
};

/*
 * This function takes a set of resources and checks that they are cofunctional.
 *
 * After this call:
 *   No hardware is programmed for call.  Only validation is done.
 */
bool dc_validate_resources(
		const struct dc *dc,
		const struct dc_validation_set set[],
		uint8_t set_count);

/*
 * This function takes a stream and checks if it is guaranteed to be supported.
 * Guaranteed means that MAX_COFUNC similar streams are supported.
 *
 * After this call:
 *   No hardware is programmed for call.  Only validation is done.
 */

bool dc_validate_guaranteed(
		const struct dc *dc,
		const struct dc_stream *stream);

/*
 * Set up streams and links associated to drive sinks
 * The streams parameter is an absolute set of all active streams.
 *
 * After this call:
 *   Phy, Encoder, Timing Generator are programmed and enabled.
 *   New streams are enabled with blank stream; no memory read.
 */
bool dc_commit_streams(
		struct dc *dc,
		const struct dc_stream *streams[],
		uint8_t stream_count);

/**
 * Create a new default stream for the requested sink
 */
struct dc_stream *dc_create_stream_for_sink(const struct dc_sink *dc_sink);

void dc_stream_retain(const struct dc_stream *dc_stream);
void dc_stream_release(const struct dc_stream *dc_stream);

struct dc_stream_status {
	int primary_otg_inst;
	int surface_count;
	const struct dc_surface *surfaces[MAX_SURFACE_NUM];

	/*
	 * link this stream passes through
	 */
	const struct dc_link *link;
};

const struct dc_stream_status *dc_stream_get_status(
	const struct dc_stream *dc_stream);

/*******************************************************************************
 * Link Interfaces
 ******************************************************************************/

/*
 * A link contains one or more sinks and their connected status.
 * The currently active signal type (HDMI, DP-SST, DP-MST) is also reported.
 */
struct dc_link {
	const struct dc_sink *remote_sinks[MAX_SINKS_PER_LINK];
	unsigned int sink_count;
	const struct dc_sink *local_sink;
	unsigned int link_index;
	enum dc_connection_type type;
	enum signal_type connector_signal;
	enum dc_irq_source irq_source_hpd;
	enum dc_irq_source irq_source_hpd_rx;/* aka DP Short Pulse  */
	/* caps is the same as reported_link_cap. link_traing use
	 * reported_link_cap. Will clean up.  TODO
	 */
	struct dc_link_settings reported_link_cap;
	struct dc_link_settings verified_link_cap;
	struct dc_link_settings max_link_setting;
	struct dc_link_settings cur_link_settings;
	struct dc_lane_settings cur_lane_setting;

	uint8_t ddc_hw_inst;
	uint8_t link_enc_hw_inst;

	struct psr_caps psr_caps;
	bool test_pattern_enabled;
	union compliance_test_state compliance_test_state;
};

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
	bool is_dp_hdmi_s3d_converter;

	bool allow_invalid_MSA_timing_param;
	bool panel_mode_edp;
	uint32_t sink_dev_id;
	uint32_t branch_dev_id;
	int8_t branch_dev_name[6];
	int8_t branch_hw_revision;
};

struct dc_link_status {
	struct dpcd_caps *dpcd_caps;
};

const struct dc_link_status *dc_link_get_status(const struct dc_link *dc_link);

/*
 * Return an enumerated dc_link.  dc_link order is constant and determined at
 * boot time.  They cannot be created or destroyed.
 * Use dc_get_caps() to get number of links.
 */
const struct dc_link *dc_get_link_at_index(const struct dc *dc, uint32_t link_index);

/* Return id of physical connector represented by a dc_link at link_index.*/
const struct graphics_object_id dc_get_link_id_at_index(
		struct dc *dc, uint32_t link_index);

/* Set backlight level of an embedded panel (eDP, LVDS). */
bool dc_link_set_backlight_level(const struct dc_link *dc_link, uint32_t level,
		uint32_t frame_ramp, const struct dc_stream *stream);

bool dc_link_init_dmcu_backlight_settings(const struct dc_link *dc_link);

bool dc_link_set_abm_level(const struct dc_link *dc_link, uint32_t level);

bool dc_link_set_psr_enable(const struct dc_link *dc_link, bool enable);

bool dc_link_setup_psr(const struct dc_link *dc_link,
		const struct dc_stream *stream);

/* Request DC to detect if there is a Panel connected.
 * boot - If this call is during initial boot.
 * Return false for any type of detection failure or MST detection
 * true otherwise. True meaning further action is required (status update
 * and OS notification).
 */
bool dc_link_detect(const struct dc_link *dc_link, bool boot);

/* Notify DC about DP RX Interrupt (aka Short Pulse Interrupt).
 * Return:
 * true - Downstream port status changed. DM should call DC to do the
 * detection.
 * false - no change in Downstream port status. No further action required
 * from DM. */
bool dc_link_handle_hpd_rx_irq(const struct dc_link *dc_link);

struct dc_sink_init_data;

struct dc_sink *dc_link_add_remote_sink(
		const struct dc_link *dc_link,
		const uint8_t *edid,
		int len,
		struct dc_sink_init_data *init_data);

void dc_link_remove_remote_sink(
	const struct dc_link *link,
	const struct dc_sink *sink);

/* Used by diagnostics for virtual link at the moment */
void dc_link_set_sink(const struct dc_link *link, struct dc_sink *sink);

void dc_link_dp_set_drive_settings(
	struct dc_link *link,
	struct link_training_settings *lt_settings);

bool dc_link_dp_perform_link_training(
	struct dc_link *link,
	const struct dc_link_settings *link_setting,
	bool skip_video_pattern);

void dc_link_dp_enable_hpd(const struct dc_link *link);

void dc_link_dp_disable_hpd(const struct dc_link *link);

bool dc_link_dp_set_test_pattern(
	const struct dc_link *link,
	enum dp_test_pattern test_pattern,
	const struct link_training_settings *p_link_settings,
	const unsigned char *p_custom_pattern,
	unsigned int cust_pattern_size);

/*******************************************************************************
 * Sink Interfaces - A sink corresponds to a display output device
 ******************************************************************************/

/*
 * The sink structure contains EDID and other display device properties
 */
struct dc_sink {
	enum signal_type sink_signal;
	struct dc_edid dc_edid; /* raw edid */
	struct dc_edid_caps edid_caps; /* parse display caps */
};

void dc_sink_retain(const struct dc_sink *sink);
void dc_sink_release(const struct dc_sink *sink);

const struct audio **dc_get_audios(struct dc *dc);

struct dc_sink_init_data {
	enum signal_type sink_signal;
	const struct dc_link *link;
	uint32_t dongle_max_pix_clk;
	bool converter_disable_audio;
};

struct dc_sink *dc_sink_create(const struct dc_sink_init_data *init_params);

/*******************************************************************************
 * Cursor interfaces - To manages the cursor within a stream
 ******************************************************************************/
/* TODO: Deprecated once we switch to dc_set_cursor_position */
bool dc_stream_set_cursor_attributes(
	const struct dc_stream *stream,
	const struct dc_cursor_attributes *attributes);

bool dc_stream_set_cursor_position(
	const struct dc_stream *stream,
	const struct dc_cursor_position *position);

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
void dc_interrupt_set(const struct dc *dc, enum dc_irq_source src, bool enable);
void dc_interrupt_ack(struct dc *dc, enum dc_irq_source src);
enum dc_irq_source dc_get_hpd_irq_source_at_index(
		struct dc *dc, uint32_t link_index);

/*******************************************************************************
 * Power Interfaces
 ******************************************************************************/

void dc_set_power_state(
		struct dc *dc,
		enum dc_acpi_cm_power_state power_state,
		enum dc_video_power_state video_power_state);
void dc_resume(const struct dc *dc);

/*******************************************************************************
 * DDC Interfaces
 ******************************************************************************/

const struct ddc_service *dc_get_ddc_at_index(
		struct dc *dc, uint32_t link_index);

/*
 * DPCD access interfaces
 */

bool dc_read_dpcd(
		struct dc *dc,
		uint32_t link_index,
		uint32_t address,
		uint8_t *data,
		uint32_t size);

bool dc_write_dpcd(
		struct dc *dc,
		uint32_t link_index,
		uint32_t address,
		const uint8_t *data,
	uint32_t size);

bool dc_submit_i2c(
		struct dc *dc,
		uint32_t link_index,
		struct i2c_command *cmd);

#endif /* DC_INTERFACE_H_ */
