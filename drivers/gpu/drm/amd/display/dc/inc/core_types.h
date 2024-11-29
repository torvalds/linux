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
#include "mpc.h"
#include "dwb.h"
#include "mcif_wb.h"
#include "panel_cntl.h"
#include "dmub/inc/dmub_cmd.h"
#include "pg_cntl.h"
#include "spl/dc_spl.h"

#define MAX_CLOCK_SOURCES 7
#define MAX_SVP_PHANTOM_STREAMS 2
#define MAX_SVP_PHANTOM_PLANES 2

#include "grph_object_id.h"
#include "link_encoder.h"
#include "stream_encoder.h"
#include "clock_source.h"
#include "audio.h"
#include "dm_pp_smu.h"
#include "dm_cp_psp.h"
#include "link_hwss.h"

/********** DAL Core*********************/
#include "transform.h"
#include "dpp.h"

#include "dml2/dml21/inc/dml_top_dchub_registers.h"
#include "dml2/dml21/inc/dml_top_types.h"

struct resource_pool;
struct dc_state;
struct resource_context;
struct clk_bw_params;

struct resource_funcs {
	enum engine_id (*get_preferred_eng_id_dpia)(unsigned int dpia_index);
	void (*destroy)(struct resource_pool **pool);
	void (*link_init)(struct dc_link *link);
	struct panel_cntl*(*panel_cntl_create)(
		const struct panel_cntl_init_data *panel_cntl_init_data);
	struct link_encoder *(*link_enc_create)(
			struct dc_context *ctx,
			const struct encoder_init_data *init);
	/* Create a minimal link encoder object with no dc_link object
	 * associated with it. */
	struct link_encoder *(*link_enc_create_minimal)(struct dc_context *ctx, enum engine_id eng_id);

	bool (*validate_bandwidth)(
					struct dc *dc,
					struct dc_state *context,
					bool fast_validate);
	void (*calculate_wm_and_dlg)(
				struct dc *dc, struct dc_state *context,
				display_e2e_pipe_params_st *pipes,
				int pipe_cnt,
				int vlevel);
	void (*update_soc_for_wm_a)(
				struct dc *dc, struct dc_state *context);

	unsigned int (*calculate_mall_ways_from_bytes)(
				const struct dc *dc,
				unsigned int total_size_in_mall_bytes);
	void (*prepare_mcache_programming)(
					struct dc *dc,
					struct dc_state *context);
	/**
	 * @populate_dml_pipes - Populate pipe data struct
	 *
	 * Returns:
	 * Total of pipes available in the specific ASIC.
	 */
	int (*populate_dml_pipes)(
		struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		bool fast_validate);

	/*
	 * Algorithm for assigning available link encoders to links.
	 *
	 * Update link_enc_assignments table and link_enc_avail list accordingly in
	 * struct resource_context.
	 */
	void (*link_encs_assign)(
			struct dc *dc,
			struct dc_state *state,
			struct dc_stream_state *streams[],
			uint8_t stream_count);
	/*
	 * Unassign a link encoder from a stream.
	 *
	 * Update link_enc_assignments table and link_enc_avail list accordingly in
	 * struct resource_context.
	 */
	void (*link_enc_unassign)(
			struct dc_state *state,
			struct dc_stream_state *stream);

	enum dc_status (*validate_global)(
		struct dc *dc,
		struct dc_state *context);

	struct pipe_ctx *(*acquire_free_pipe_as_secondary_dpp_pipe)(
			const struct dc_state *cur_ctx,
			struct dc_state *new_ctx,
			const struct resource_pool *pool,
			const struct pipe_ctx *opp_head_pipe);

	struct pipe_ctx *(*acquire_free_pipe_as_secondary_opp_head)(
			const struct dc_state *cur_ctx,
			struct dc_state *new_ctx,
			const struct resource_pool *pool,
			const struct pipe_ctx *otg_master);

	void (*release_pipe)(struct dc_state *context,
			struct pipe_ctx *pipe,
			const struct resource_pool *pool);

	enum dc_status (*validate_plane)(
			const struct dc_plane_state *plane_state,
			struct dc_caps *caps);

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
	bool (*acquire_post_bldn_3dlut)(
			struct resource_context *res_ctx,
			const struct resource_pool *pool,
			int mpcc_id,
			struct dc_3dlut **lut,
			struct dc_transfer_func **shaper);

	bool (*release_post_bldn_3dlut)(
			struct resource_context *res_ctx,
			const struct resource_pool *pool,
			struct dc_3dlut **lut,
			struct dc_transfer_func **shaper);

	enum dc_status (*add_dsc_to_stream_resource)(
			struct dc *dc, struct dc_state *state,
			struct dc_stream_state *stream);

	void (*add_phantom_pipes)(
            struct dc *dc,
            struct dc_state *context,
            display_e2e_pipe_params_st *pipes,
			unsigned int pipe_cnt,
            unsigned int index);

	void (*get_panel_config_defaults)(struct dc_panel_config *panel_config);
	void (*build_pipe_pix_clk_params)(struct pipe_ctx *pipe_ctx);
	/*
	 * Get indicator of power from a context that went through full validation
	 */
	int (*get_power_profile)(const struct dc_state *context);
	unsigned int (*get_det_buffer_size)(const struct dc_state *context);
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

	/* An array for accessing the link encoder objects that have been created.
	 * Index in array corresponds to engine ID - viz. 0: ENGINE_ID_DIGA
	 */
	struct link_encoder *link_encoders[MAX_DIG_LINK_ENCODERS];
	/* Number of DIG link encoder objects created - i.e. number of valid
	 * entries in link_encoders array.
	 */
	unsigned int dig_link_enc_count;
	/* Number of USB4 DPIA (DisplayPort Input Adapter) link objects created.*/
	unsigned int usb4_dpia_count;

	unsigned int hpo_dp_stream_enc_count;
	struct hpo_dp_stream_encoder *hpo_dp_stream_enc[MAX_HPO_DP2_ENCODERS];
	unsigned int hpo_dp_link_enc_count;
	struct hpo_dp_link_encoder *hpo_dp_link_enc[MAX_HPO_DP2_LINK_ENCODERS];
	struct dc_3dlut *mpc_lut[MAX_PIPES];
	struct dc_transfer_func *mpc_shaper[MAX_PIPES];

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
	struct pg_cntl *pg_cntl;
	struct irq_service *irqs;

	struct abm *abm;
	struct dmcu *dmcu;
	struct dmub_psr *psr;
	struct dmub_replay *replay;

	struct abm *multiple_abms[MAX_PIPES];

	const struct resource_funcs *funcs;
	const struct resource_caps *res_cap;

	struct ddc_service *oem_device;
};

struct dcn_fe_bandwidth {
	int dppclk_khz;

};

/* Parameters needed to call set_disp_pattern_generator */
struct test_pattern_params {
	enum controller_dp_test_pattern test_pattern;
	enum controller_dp_color_space color_space;
	enum dc_color_depth color_depth;
	int width;
	int height;
	int offset;
};

struct stream_resource {
	struct output_pixel_processor *opp;
	struct display_stream_compressor *dsc;
	struct timing_generator *tg;
	struct stream_encoder *stream_enc;
	struct hpo_dp_stream_encoder *hpo_dp_stream_enc;
	struct audio *audio;

	struct pixel_clk_params pix_clk_params;
	struct encoder_info_frame encoder_info_frame;

	struct abm *abm;
	/* There are only (num_pipes+1)/2 groups. 0 means unassigned,
	 * otherwise it's using group number 'gsl_group-1'
	 */
	uint8_t gsl_group;

	struct test_pattern_params test_pattern_params;
};

struct plane_resource {
	/* scl_data is scratch space required to program a plane */
	struct scaler_data scl_data;
	/* Below pointers to hw objects are required to enable the plane */
	/* spl_in and spl_out are the input and output structures for SPL
	 * which are required when using Scaler Programming Library
	 * these are scratch spaces needed when programming a plane
	 */
	struct spl_in spl_in;
	struct spl_out spl_out;
	/* Below pointers to hw objects are required to enable the plane */
	struct hubp *hubp;
	struct mem_input *mi;
	struct input_pixel_processor *ipp;
	struct transform *xfm;
	struct dpp *dpp;
	uint8_t mpcc_inst;

	struct dcn_fe_bandwidth bw;
};

#define LINK_RES_HPO_DP_REC_MAP__MASK 0xFFFF
#define LINK_RES_HPO_DP_REC_MAP__SHIFT 0

/* all mappable hardware resources used to enable a link */
struct link_resource {
	struct hpo_dp_link_encoder *hpo_dp_link_enc;
};

struct link_config {
	struct dc_link_settings dp_link_settings;
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
		uint32_t plane_changed : 1;
		uint32_t det_size : 1;
		uint32_t unbounded_req : 1;
		uint32_t test_pattern_changed : 1;
	} bits;
	uint32_t raw;
};

struct pixel_rate_divider {
	uint32_t div_factor1;
	uint32_t div_factor2;
};

enum p_state_switch_method {
	P_STATE_UNKNOWN						= 0,
	P_STATE_V_BLANK						= 1,
	P_STATE_FPO,
	P_STATE_V_ACTIVE,
	P_STATE_SUB_VP,
	P_STATE_DRR_SUB_VP,
	P_STATE_V_BLANK_SUB_VP
};

struct pipe_ctx {
	struct dc_plane_state *plane_state;
	struct dc_stream_state *stream;

	struct plane_resource plane_res;

	/**
	 * @stream_res: Reference to DCN resource components such OPP and DSC.
	 */
	struct stream_resource stream_res;
	struct link_resource link_res;

	struct clock_source *clock_source;

	struct pll_settings pll_settings;

	/**
	 * @link_config:
	 *
	 * link config records software decision for what link config should be
	 * enabled given current link capability and stream during hw resource
	 * mapping. This is to decouple the dependency on link capability during
	 * dc commit or update.
	 */
	struct link_config link_config;

	uint8_t pipe_idx;
	uint8_t pipe_idx_syncd;

	struct pipe_ctx *top_pipe;
	struct pipe_ctx *bottom_pipe;
	struct pipe_ctx *next_odm_pipe;
	struct pipe_ctx *prev_odm_pipe;

	struct _vcs_dpi_display_dlg_regs_st dlg_regs;
	struct _vcs_dpi_display_ttu_regs_st ttu_regs;
	struct _vcs_dpi_display_rq_regs_st rq_regs;
	struct _vcs_dpi_display_pipe_dest_params_st pipe_dlg_param;
	struct _vcs_dpi_display_rq_params_st dml_rq_param;
	struct _vcs_dpi_display_dlg_sys_params_st dml_dlg_sys_param;
	struct _vcs_dpi_display_e2e_pipe_params_st dml_input;
	int det_buffer_size_kb;
	bool unbounded_req;
	unsigned int surface_size_in_mall_bytes;
	struct dml2_dchub_per_pipe_register_set hubp_regs;
	struct dml2_hubp_pipe_mcache_regs mcache_regs;

	struct dwbc *dwbc;
	struct mcif_wb *mcif_wb;
	union pipe_update_flags update_flags;
	enum p_state_switch_method p_state_type;
	struct tg_color visual_confirm_color;
	bool has_vactive_margin;
	/* subvp_index: only valid if the pipe is a SUBVP_MAIN*/
	uint8_t subvp_index;
	struct pixel_rate_divider pixel_rate_divider;
	/* pixels borrowed from hblank to hactive */
	uint8_t hblank_borrow;
};

/* Data used for dynamic link encoder assignment.
 * Tracks current and future assignments; available link encoders;
 * and mode of operation (whether to use current or future assignments).
 */
struct link_enc_cfg_context {
	enum link_enc_cfg_mode mode;
	struct link_enc_assignment link_enc_assignments[MAX_PIPES];
	enum engine_id link_enc_avail[MAX_DIG_LINK_ENCODERS];
	struct link_enc_assignment transient_assignments[MAX_PIPES];
};

struct resource_context {
	struct pipe_ctx pipe_ctx[MAX_PIPES];
	bool is_stream_enc_acquired[MAX_PIPES * 2];
	bool is_audio_acquired[MAX_PIPES];
	uint8_t clock_source_ref_count[MAX_CLOCK_SOURCES];
	uint8_t dp_clock_source_ref_count;
	bool is_dsc_acquired[MAX_PIPES];
	struct link_enc_cfg_context link_enc_cfg_ctx;
	bool is_hpo_dp_stream_enc_acquired[MAX_HPO_DP2_ENCODERS];
	unsigned int hpo_dp_link_enc_to_link_idx[MAX_HPO_DP2_LINK_ENCODERS];
	int hpo_dp_link_enc_ref_cnts[MAX_HPO_DP2_LINK_ENCODERS];
	bool is_mpc_3dlut_acquired[MAX_PIPES];
	/* solely used for build scalar data in dml2 */
	struct pipe_ctx temp_pipe;
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
	union dcn_watermark_set watermarks;
	struct dcn_bw_writeback bw_writeback;
	int compbuf_size_kb;
	unsigned int mall_ss_size_bytes;
	unsigned int mall_ss_psr_active_size_bytes;
	unsigned int mall_subvp_size_bytes;
	unsigned int legacy_svp_drr_stream_index;
	bool legacy_svp_drr_stream_index_valid;
	struct dml2_mcache_surface_allocation mcache_allocations[DML2_MAX_PLANES];
	struct dmub_cmd_fams2_global_config fams2_global_config;
	struct dmub_fams2_stream_static_state fams2_stream_params[DML2_MAX_PLANES];
	/*struct dmub_fams2_stream_static_state_v1 fams2_stream_params[DML2_MAX_PLANES];*/ // TODO: Update to this once DML is updated
	struct dml2_display_arb_regs arb_regs;
};

union bw_output {
	struct dcn_bw_output dcn;
	struct dce_bw_output dce;
};

struct bw_context {
	union bw_output bw;
	struct display_mode_lib dml;
	struct dml2_context *dml2;
	struct dml2_context *dml2_dc_power_source;
};

struct dc_dmub_cmd {
	union dmub_rb_cmd dmub_cmd;
	enum dm_dmub_wait_type wait_type;
};

/**
 * struct dc_state - The full description of a state requested by users
 */
struct dc_state {
	/**
	 * @streams: Stream state properties
	 */
	struct dc_stream_state *streams[MAX_PIPES];

	/**
	 * @stream_status: Planes status on a given stream
	 */
	struct dc_stream_status stream_status[MAX_PIPES];
	/**
	 * @phantom_streams: Stream state properties for phantoms
	 */
	struct dc_stream_state *phantom_streams[MAX_PHANTOM_PIPES];
	/**
	 * @phantom_planes: Planes state properties for phantoms
	 */
	struct dc_plane_state *phantom_planes[MAX_PHANTOM_PIPES];

	/**
	 * @stream_count: Total of streams in use
	 */
	uint8_t stream_count;
	uint8_t stream_mask;

	/**
	 * @stream_count: Total phantom streams in use
	 */
	uint8_t phantom_stream_count;
	/**
	 * @stream_count: Total phantom planes in use
	 */
	uint8_t phantom_plane_count;
	/**
	 * @res_ctx: Persistent state of resources
	 */
	struct resource_context res_ctx;

	/**
	 * @pp_display_cfg: PowerPlay clocks and settings
	 * Note: this is a big struct, do *not* put on stack!
	 */
	struct dm_pp_display_configuration pp_display_cfg;

	/**
	 * @dcn_bw_vars: non-stack memory to support bandwidth calculations
	 * Note: this is a big struct, do *not* put on stack!
	 */
	struct dcn_bw_internal_vars dcn_bw_vars;

	struct clk_mgr *clk_mgr;

	/**
	 * @bw_ctx: The output from bandwidth and watermark calculations and the DML
	 *
	 * Each context must have its own instance of VBA, and in order to
	 * initialize and obtain IP and SOC, the base DML instance from DC is
	 * initially copied into every context.
	 */
	struct bw_context bw_ctx;

	struct block_sequence block_sequence[50];
	unsigned int block_sequence_steps;
	struct dc_dmub_cmd dc_dmub_cmd[10];
	unsigned int dmub_cmd_count;

	/**
	 * @refcount: refcount reference
	 *
	 * Notice that dc_state is used around the code to capture the current
	 * context, so we need to pass it everywhere. That's why we want to use
	 * kref in this struct.
	 */
	struct kref refcount;

	struct {
		unsigned int stutter_period_us;
	} perf_params;

	enum dc_power_source_type power_source;
};

struct replay_context {
	/* ddc line */
	enum channel_id aux_inst;
	/* Transmitter id */
	enum transmitter digbe_inst;
	/* Engine Id is used for Dig Be source select */
	enum engine_id digfe_inst;
	/* Controller Id used for Dig Fe source select */
	enum controller_id controllerId;
	unsigned int line_time_in_ns;
};

enum dc_replay_enable {
	DC_REPLAY_DISABLE			= 0,
	DC_REPLAY_ENABLE			= 1,
};

struct dc_bounding_box_max_clk {
	int max_dcfclk_mhz;
	int max_dispclk_mhz;
	int max_dppclk_mhz;
	int max_phyclk_mhz;
};

#endif /* _CORE_TYPES_H_ */
