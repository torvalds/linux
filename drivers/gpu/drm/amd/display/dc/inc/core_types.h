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
#include "dce_calcs.h"
#include "dcn_calcs.h"
#include "ddc_service_types.h"
#include "dc_bios_types.h"
#include "mem_input.h"
#include "hubp.h"
#if defined(CONFIG_DRM_AMD_DC_DCN)
#include "mpc.h"
#endif
#include "dwb.h"
#include "mcif_wb.h"
#include "panel_cntl.h"

#define MAX_CLOCK_SOURCES 7

void enable_surface_flip_reporting(struct dc_plane_state *plane_state,
		uint32_t controller_id);

#include "grph_object_id.h"
#include "link_encoder.h"
#include "stream_encoder.h"
#include "clock_source.h"
#include "audio.h"
#include "dm_pp_smu.h"
#ifdef CONFIG_DRM_AMD_DC_HDCP
#include "dm_cp_psp.h"
#endif

/************ link *****************/
struct link_init_data {
	const struct dc *dc;
	struct dc_context *ctx; /* TODO: remove 'dal' when DC is complete. */
	uint32_t connector_index; /* this will be mapped to the HPD pins */
	uint32_t link_index; /* this is mapped to DAL display_index
				TODO: remove it when DC is complete. */
};

struct dc_link *link_create(const struct link_init_data *init_params);
void link_destroy(struct dc_link **link);

enum dc_status dc_link_validate_mode_timing(
		const struct dc_stream_state *stream,
		struct dc_link *link,
		const struct dc_crtc_timing *timing);

void core_link_resume(struct dc_link *link);

void core_link_enable_stream(
		struct dc_state *state,
		struct pipe_ctx *pipe_ctx);

void core_link_disable_stream(struct pipe_ctx *pipe_ctx);

void core_link_set_avmute(struct pipe_ctx *pipe_ctx, bool enable);
/********** DAL Core*********************/
#include "transform.h"
#include "dpp.h"

struct resource_pool;
struct dc_state;
struct resource_context;
struct clk_bw_params;

struct resource_funcs {
	void (*destroy)(struct resource_pool **pool);
	void (*link_init)(struct dc_link *link);
	struct panel_cntl*(*panel_cntl_create)(
		const struct panel_cntl_init_data *panel_cntl_init_data);
	struct link_encoder *(*link_enc_create)(
			const struct encoder_init_data *init);
	bool (*validate_bandwidth)(
					struct dc *dc,
					struct dc_state *context,
					bool fast_validate);

	int (*populate_dml_pipes)(
		struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes);

	enum dc_status (*validate_global)(
		struct dc *dc,
		struct dc_state *context);

	struct pipe_ctx *(*acquire_idle_pipe_for_layer)(
			struct dc_state *context,
			const struct resource_pool *pool,
			struct dc_stream_state *stream);

	enum dc_status (*validate_plane)(const struct dc_plane_state *plane_state, struct dc_caps *caps);

	enum dc_status (*add_stream_to_ctx)(
			struct dc *dc,
			struct dc_state *new_ctx,
			struct dc_stream_state *dc_stream);

	enum dc_status (*remove_stream_from_ctx)(
				struct dc *dc,
				struct dc_state *new_ctx,
				struct dc_stream_state *stream);
	enum dc_status (*patch_unknown_plane_state)(
			struct dc_plane_state *plane_state);

	struct stream_encoder *(*find_first_free_match_stream_enc_for_link)(
			struct resource_context *res_ctx,
			const struct resource_pool *pool,
			struct dc_stream_state *stream);
	void (*populate_dml_writeback_from_context)(
			struct dc *dc,
			struct resource_context *res_ctx,
			display_e2e_pipe_params_st *pipes);

	void (*set_mcif_arb_params)(
			struct dc *dc,
			struct dc_state *context,
			display_e2e_pipe_params_st *pipes,
			int pipe_cnt);
	void (*update_bw_bounding_box)(
			struct dc *dc,
			struct clk_bw_params *bw_params);

};

struct audio_support{
	bool dp_audio;
	bool hdmi_audio_on_dongle;
	bool hdmi_audio_native;
};

#define NO_UNDERLAY_PIPE -1

struct resource_pool {
	struct mem_input *mis[MAX_PIPES];
	struct hubp *hubps[MAX_PIPES];
	struct input_pixel_processor *ipps[MAX_PIPES];
	struct transform *transforms[MAX_PIPES];
	struct dpp *dpps[MAX_PIPES];
	struct output_pixel_processor *opps[MAX_PIPES];
	struct timing_generator *timing_generators[MAX_PIPES];
	struct stream_encoder *stream_enc[MAX_PIPES * 2];
	struct hubbub *hubbub;
	struct mpc *mpc;
	struct pp_smu_funcs *pp_smu;
	struct dce_aux *engines[MAX_PIPES];
	struct dce_i2c_hw *hw_i2cs[MAX_PIPES];
	struct dce_i2c_sw *sw_i2cs[MAX_PIPES];
	bool i2c_hw_buffer_in_use;

	struct dwbc *dwbc[MAX_DWB_PIPES];
	struct mcif_wb *mcif_wb[MAX_DWB_PIPES];
	struct {
		unsigned int gsl_0:1;
		unsigned int gsl_1:1;
		unsigned int gsl_2:1;
	} gsl_groups;

	struct display_stream_compressor *dscs[MAX_PIPES];

	unsigned int pipe_count;
	unsigned int underlay_pipe_index;
	unsigned int stream_enc_count;

	struct {
		unsigned int xtalin_clock_inKhz;
		unsigned int dccg_ref_clock_inKhz;
		unsigned int dchub_ref_clock_inKhz;
	} ref_clocks;
	unsigned int timing_generator_count;
	unsigned int mpcc_count;

	unsigned int writeback_pipe_count;
	/*
	 * reserved clock source for DP
	 */
	struct clock_source *dp_clock_source;

	struct clock_source *clock_sources[MAX_CLOCK_SOURCES];
	unsigned int clk_src_count;

	struct audio *audios[MAX_AUDIOS];
	unsigned int audio_count;
	struct audio_support audio_support;

	struct dccg *dccg;
	struct irq_service *irqs;

	struct abm *abm;
	struct dmcu *dmcu;
	struct dmub_psr *psr;

	const struct resource_funcs *funcs;
	const struct resource_caps *res_cap;

	struct ddc_service *oem_device;
};

struct dcn_fe_bandwidth {
	int dppclk_khz;

};

struct stream_resource {
	struct output_pixel_processor *opp;
	struct display_stream_compressor *dsc;
	struct timing_generator *tg;
	struct stream_encoder *stream_enc;
	struct audio *audio;

	struct pixel_clk_params pix_clk_params;
	struct encoder_info_frame encoder_info_frame;

	struct abm *abm;
	/* There are only (num_pipes+1)/2 groups. 0 means unassigned,
	 * otherwise it's using group number 'gsl_group-1'
	 */
	uint8_t gsl_group;
};

struct plane_resource {
	struct scaler_data scl_data;
	struct hubp *hubp;
	struct mem_input *mi;
	struct input_pixel_processor *ipp;
	struct transform *xfm;
	struct dpp *dpp;
	uint8_t mpcc_inst;

	struct dcn_fe_bandwidth bw;
};

union pipe_update_flags {
	struct {
		uint32_t enable : 1;
		uint32_t disable : 1;
		uint32_t odm : 1;
		uint32_t global_sync : 1;
		uint32_t opp_changed : 1;
		uint32_t tg_changed : 1;
		uint32_t mpcc : 1;
		uint32_t dppclk : 1;
		uint32_t hubp_interdependent : 1;
		uint32_t hubp_rq_dlg_ttu : 1;
		uint32_t gamut_remap : 1;
		uint32_t scaler : 1;
		uint32_t viewport : 1;
	} bits;
	uint32_t raw;
};

struct pipe_ctx {
	struct dc_plane_state *plane_state;
	struct dc_stream_state *stream;

	struct plane_resource plane_res;
	struct stream_resource stream_res;

	struct clock_source *clock_source;

	struct pll_settings pll_settings;

	uint8_t pipe_idx;

	struct pipe_ctx *top_pipe;
	struct pipe_ctx *bottom_pipe;
	struct pipe_ctx *next_odm_pipe;
	struct pipe_ctx *prev_odm_pipe;

#ifdef CONFIG_DRM_AMD_DC_DCN
	struct _vcs_dpi_display_dlg_regs_st dlg_regs;
	struct _vcs_dpi_display_ttu_regs_st ttu_regs;
	struct _vcs_dpi_display_rq_regs_st rq_regs;
	struct _vcs_dpi_display_pipe_dest_params_st pipe_dlg_param;
#endif
	union pipe_update_flags update_flags;
	struct dwbc *dwbc;
	struct mcif_wb *mcif_wb;
};

struct resource_context {
	struct pipe_ctx pipe_ctx[MAX_PIPES];
	bool is_stream_enc_acquired[MAX_PIPES * 2];
	bool is_audio_acquired[MAX_PIPES];
	uint8_t clock_source_ref_count[MAX_CLOCK_SOURCES];
	uint8_t dp_clock_source_ref_count;
	bool is_dsc_acquired[MAX_PIPES];
};

struct dce_bw_output {
	bool cpuc_state_change_enable;
	bool cpup_state_change_enable;
	bool stutter_mode_enable;
	bool nbp_state_change_enable;
	bool all_displays_in_sync;
	struct dce_watermarks urgent_wm_ns[MAX_PIPES];
	struct dce_watermarks stutter_exit_wm_ns[MAX_PIPES];
	struct dce_watermarks stutter_entry_wm_ns[MAX_PIPES];
	struct dce_watermarks nbp_state_change_wm_ns[MAX_PIPES];
	int sclk_khz;
	int sclk_deep_sleep_khz;
	int yclk_khz;
	int dispclk_khz;
	int blackout_recovery_time_us;
};

struct dcn_bw_writeback {
	struct mcif_arb_params mcif_wb_arb[MAX_DWB_PIPES];
};

struct dcn_bw_output {
	struct dc_clocks clk;
	struct dcn_watermark_set watermarks;
	struct dcn_bw_writeback bw_writeback;
};

union bw_output {
	struct dcn_bw_output dcn;
	struct dce_bw_output dce;
};

struct bw_context {
	union bw_output bw;
	struct display_mode_lib dml;
};
/**
 * struct dc_state - The full description of a state requested by a user
 *
 * @streams: Stream properties
 * @stream_status: The planes on a given stream
 * @res_ctx: Persistent state of resources
 * @bw_ctx: The output from bandwidth and watermark calculations and the DML
 * @pp_display_cfg: PowerPlay clocks and settings
 * @dcn_bw_vars: non-stack memory to support bandwidth calculations
 *
 */
struct dc_state {
	struct dc_stream_state *streams[MAX_PIPES];
	struct dc_stream_status stream_status[MAX_PIPES];
	uint8_t stream_count;

	struct resource_context res_ctx;

	struct bw_context bw_ctx;

	/* Note: these are big structures, do *not* put on stack! */
	struct dm_pp_display_configuration pp_display_cfg;
#ifdef CONFIG_DRM_AMD_DC_DCN
	struct dcn_bw_internal_vars dcn_bw_vars;
#endif

	struct clk_mgr *clk_mgr;

	struct kref refcount;
};

#endif /* _CORE_TYPES_H_ */
