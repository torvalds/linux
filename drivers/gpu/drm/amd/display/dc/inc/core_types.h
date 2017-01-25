/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef _CORE_TYPES_H_
#define _CORE_TYPES_H_

#include "dc.h"
#include "bandwidth_calcs.h"
#include "ddc_service_types.h"
#include "dc_bios_types.h"

struct core_stream;

#define MAX_PIPES 6
#define MAX_CLOCK_SOURCES 7


/********* core_surface **********/
#define DC_SURFACE_TO_CORE(dc_surface) \
	container_of(dc_surface, struct core_surface, public)

#define DC_GAMMA_TO_CORE(dc_gamma) \
	container_of(dc_gamma, struct core_gamma, public)

#define DC_TRANSFER_FUNC_TO_CORE(dc_transfer_func) \
	container_of(dc_transfer_func, struct core_transfer_func, public)

struct core_surface {
	struct dc_surface public;
	struct dc_surface_status status;
	struct dc_context *ctx;
};

struct core_gamma {
	struct dc_gamma public;
	struct dc_context *ctx;
};

struct core_transfer_func {
	struct dc_transfer_func public;
	struct dc_context *ctx;
};

void enable_surface_flip_reporting(struct dc_surface *dc_surface,
		uint32_t controller_id);

/********* core_stream ************/
#include "grph_object_id.h"
#include "link_encoder.h"
#include "stream_encoder.h"
#include "clock_source.h"
#include "audio.h"
#include "hw_sequencer_types.h"
#include "opp.h"

#define DC_STREAM_TO_CORE(dc_stream) container_of( \
	dc_stream, struct core_stream, public)

struct core_stream {
	struct dc_stream public;

	/* field internal to DC */
	struct dc_context *ctx;
	const struct core_sink *sink;

	/* used by DCP and FMT */
	struct bit_depth_reduction_params bit_depth_params;
	struct clamping_and_pixel_encoding_params clamping;

	int phy_pix_clk;
	enum signal_type signal;

	struct dc_stream_status status;
};

/************ core_sink *****************/

#define DC_SINK_TO_CORE(dc_sink) \
	container_of(dc_sink, struct core_sink, public)

struct core_sink {
	/** The public, read-only (for DM) area of sink. **/
	struct dc_sink public;
	/** End-of-public area. **/

	/** The 'protected' area - read/write access, for use only inside DC **/
	/* not used for now */
	struct core_link *link;
	struct dc_context *ctx;
	uint32_t dongle_max_pix_clk;
	bool converter_disable_audio;
};

/************ link *****************/
#define DC_LINK_TO_CORE(dc_link) container_of(dc_link, struct core_link, public)

struct link_init_data {
	const struct core_dc *dc;
	struct dc_context *ctx; /* TODO: remove 'dal' when DC is complete. */
	uint32_t connector_index; /* this will be mapped to the HPD pins */
	uint32_t link_index; /* this is mapped to DAL display_index
				TODO: remove it when DC is complete. */
};

/* DP MST stream allocation (payload bandwidth number) */
struct link_mst_stream_allocation {
	/* DIG front */
	const struct stream_encoder *stream_enc;
	/* associate DRM payload table with DC stream encoder */
	uint8_t vcp_id;
	/* number of slots required for the DP stream in transport packet */
	uint8_t slot_count;
};

/* DP MST stream allocation table */
struct link_mst_stream_allocation_table {
	/* number of DP video streams */
	int stream_count;
	/* array of stream allocations */
	struct link_mst_stream_allocation
	stream_allocations[MAX_CONTROLLER_NUM];
};

struct core_link {
	struct dc_link public;
	const struct core_dc *dc;

	struct dc_context *ctx; /* TODO: AUTO remove 'dal' when DC is complete*/

	struct link_encoder *link_enc;
	struct ddc_service *ddc;
	struct graphics_object_id link_id;
	union ddi_channel_mapping ddi_channel_mapping;
	struct connector_device_tag_info device_tag;
	struct dpcd_caps dpcd_caps;
	unsigned int dpcd_sink_count;

	enum edp_revision edp_revision;

	/* MST record stream using this link */
	struct link_flags {
		bool dp_keep_receiver_powered;
	} wa_flags;
	struct link_mst_stream_allocation_table mst_stream_alloc_table;

	struct dc_link_status link_status;
};

#define DC_LINK_TO_LINK(dc_link) container_of(dc_link, struct core_link, public)

struct core_link *link_create(const struct link_init_data *init_params);
void link_destroy(struct core_link **link);

enum dc_status dc_link_validate_mode_timing(
		const struct core_stream *stream,
		struct core_link *link,
		const struct dc_crtc_timing *timing);

void core_link_resume(struct core_link *link);

void core_link_enable_stream(struct pipe_ctx *pipe_ctx);

void core_link_disable_stream(struct pipe_ctx *pipe_ctx);

/********** DAL Core*********************/
#include "display_clock.h"
#include "transform.h"

struct resource_pool;
struct validate_context;
struct resource_context;

struct resource_funcs {
	void (*destroy)(struct resource_pool **pool);
	struct link_encoder *(*link_enc_create)(
			const struct encoder_init_data *init);
	enum dc_status (*validate_with_context)(
					const struct core_dc *dc,
					const struct dc_validation_set set[],
					int set_count,
					struct validate_context *context);

	enum dc_status (*validate_guaranteed)(
					const struct core_dc *dc,
					const struct dc_stream *stream,
					struct validate_context *context);

	enum dc_status (*validate_bandwidth)(
					const struct core_dc *dc,
					struct validate_context *context);

	struct validate_context *(*apply_clk_constraints)(
					const struct core_dc *dc,
					struct validate_context *context);

	struct pipe_ctx *(*acquire_idle_pipe_for_layer)(
			struct resource_context *res_ctx,
			struct core_stream *stream);

	void (*build_bit_depth_reduction_params)(
			const struct core_stream *stream,
			struct bit_depth_reduction_params *fmt_bit_depth);
};

struct audio_support{
	bool dp_audio;
	bool hdmi_audio_on_dongle;
	bool hdmi_audio_native;
};

#define NO_UNDERLAY_PIPE -1

struct resource_pool {
	struct mem_input *mis[MAX_PIPES];
	struct input_pixel_processor *ipps[MAX_PIPES];
	struct transform *transforms[MAX_PIPES];
	struct output_pixel_processor *opps[MAX_PIPES];
	struct timing_generator *timing_generators[MAX_PIPES];
	struct stream_encoder *stream_enc[MAX_PIPES * 2];

	unsigned int pipe_count;
	unsigned int underlay_pipe_index;
	unsigned int stream_enc_count;

	/*
	 * reserved clock source for DP
	 */
	struct clock_source *dp_clock_source;

	struct clock_source *clock_sources[MAX_CLOCK_SOURCES];
	unsigned int clk_src_count;

	struct audio *audios[MAX_PIPES];
	unsigned int audio_count;
	struct audio_support audio_support;

	struct display_clock *display_clock;
	struct irq_service *irqs;

	struct abm *abm;
	struct dmcu *dmcu;

	const struct resource_funcs *funcs;
	const struct resource_caps *res_cap;
};

struct pipe_ctx {
	struct core_surface *surface;
	struct core_stream *stream;

	struct mem_input *mi;
	struct input_pixel_processor *ipp;
	struct transform *xfm;
	struct output_pixel_processor *opp;
	struct timing_generator *tg;

	struct scaler_data scl_data;

	struct stream_encoder *stream_enc;
	struct display_clock *dis_clk;
	struct clock_source *clock_source;

	struct audio *audio;

	struct pixel_clk_params pix_clk_params;
	struct pll_settings pll_settings;

	/*fmt*/
	struct encoder_info_frame encoder_info_frame;

	uint8_t pipe_idx;

	struct pipe_ctx *top_pipe;
	struct pipe_ctx *bottom_pipe;
};

struct resource_context {
	const struct resource_pool *pool;
	struct pipe_ctx pipe_ctx[MAX_PIPES];
	bool is_stream_enc_acquired[MAX_PIPES * 2];
	bool is_audio_acquired[MAX_PIPES];
	uint8_t clock_source_ref_count[MAX_CLOCK_SOURCES];
	uint8_t dp_clock_source_ref_count;
 };

struct validate_context {
	struct core_stream *streams[MAX_PIPES];
	struct dc_stream_status stream_status[MAX_PIPES];
	uint8_t stream_count;

	struct resource_context res_ctx;

	/* The output from BW and WM calculations. */
	struct bw_calcs_output bw_results;
	/* Note: these are big structures, do *not* put on stack! */
	struct dm_pp_display_configuration pp_display_cfg;
	int dispclk_khz;
};

#endif /* _CORE_TYPES_H_ */
