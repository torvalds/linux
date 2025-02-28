/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef _DML2_WRAPPER_H_
#define _DML2_WRAPPER_H_

#include "os_types.h"

#define DML2_MAX_NUM_DPM_LVL 30

struct dml2_context;
struct display_mode_lib_st;
struct dc;
struct pipe_ctx;
struct dc_plane_state;
struct dc_sink;
struct dc_stream_state;
struct resource_context;
struct display_stream_compressor;

// Configuration of the MALL on the SoC
struct dml2_soc_mall_info {
	// Cache line size of 0 means MALL is not enabled/present
	unsigned int cache_line_size_bytes;
	unsigned int cache_num_ways;
	unsigned int max_cab_allocation_bytes;

	unsigned int mblk_width_pixels;
	unsigned int mblk_size_bytes;
	unsigned int mblk_height_4bpe_pixels;
	unsigned int mblk_height_8bpe_pixels;
};

// Output of DML2 for clock requirements
struct dml2_dcn_clocks {
	unsigned int dispclk_khz;
	unsigned int dcfclk_khz;
	unsigned int fclk_khz;
	unsigned int uclk_mts;
	unsigned int phyclk_khz;
	unsigned int socclk_khz;
	unsigned int ref_dtbclk_khz;
	bool p_state_supported;
	unsigned int cab_num_ways_required;
	unsigned int dcfclk_khz_ds;
};

struct dml2_dc_callbacks {
	struct dc *dc;
	bool (*build_scaling_params)(struct pipe_ctx *pipe_ctx);
	void (*build_test_pattern_params)(struct resource_context *res_ctx, struct pipe_ctx *otg_master);
	bool (*can_support_mclk_switch_using_fw_based_vblank_stretch)(struct dc *dc, struct dc_state *context);
	bool (*acquire_secondary_pipe_for_mpc_odm)(const struct dc *dc, struct dc_state *state, struct pipe_ctx *pri_pipe, struct pipe_ctx *sec_pipe, bool odm);
	bool (*update_pipes_for_stream_with_slice_count)(
			struct dc_state *new_ctx,
			const struct dc_state *cur_ctx,
			const struct resource_pool *pool,
			const struct dc_stream_state *stream,
			int new_slice_count);
	bool (*update_pipes_for_plane_with_slice_count)(
			struct dc_state *new_ctx,
			const struct dc_state *cur_ctx,
			const struct resource_pool *pool,
			const struct dc_plane_state *plane,
			int slice_count);
	int (*get_odm_slice_index)(const struct pipe_ctx *opp_head);
	int (*get_odm_slice_count)(const struct pipe_ctx *opp_head);
	int (*get_mpc_slice_index)(const struct pipe_ctx *dpp_pipe);
	int (*get_mpc_slice_count)(const struct pipe_ctx *dpp_pipe);
	struct pipe_ctx *(*get_opp_head)(const struct pipe_ctx *pipe_ctx);
	struct pipe_ctx *(*get_otg_master_for_stream)(
		struct resource_context *res_ctx,
		const struct dc_stream_state *stream);
	int (*get_opp_heads_for_otg_master)(const struct pipe_ctx *otg_master,
		struct resource_context *res_ctx,
		struct pipe_ctx *opp_heads[MAX_PIPES]);
	int (*get_dpp_pipes_for_plane)(const struct dc_plane_state *plane,
			struct resource_context *res_ctx,
			struct pipe_ctx *dpp_pipes[MAX_PIPES]);
	struct dc_stream_status *(*get_stream_status)(
		struct dc_state *state,
		const struct dc_stream_state *stream);
	struct dc_stream_state *(*get_stream_from_id)(const struct dc_state *state, unsigned int id);
	unsigned int (*get_max_flickerless_instant_vtotal_increase)(
			struct dc_stream_state *stream,
			bool is_gaming);
};

struct dml2_dc_svp_callbacks {
	struct dc *dc;
	bool (*build_scaling_params)(struct pipe_ctx *pipe_ctx);
	struct dc_stream_state* (*create_phantom_stream)(const struct dc *dc,
			struct dc_state *state,
			struct dc_stream_state *main_stream);
	struct dc_plane_state* (*create_phantom_plane)(const struct dc *dc,
			struct dc_state *state,
			struct dc_plane_state *main_plane);
	enum dc_status (*add_phantom_stream)(const struct dc *dc,
			struct dc_state *state,
			struct dc_stream_state *phantom_stream,
			struct dc_stream_state *main_stream);
	bool (*add_phantom_plane)(const struct dc *dc, struct dc_stream_state *stream, struct dc_plane_state *plane_state, struct dc_state *context);
	bool (*remove_phantom_plane)(const struct dc *dc,
			struct dc_stream_state *stream,
			struct dc_plane_state *plane_state,
			struct dc_state *context);
	enum dc_status (*remove_phantom_stream)(const struct dc *dc,
			struct dc_state *state,
			struct dc_stream_state *stream);
	void (*release_phantom_plane)(const struct dc *dc,
			struct dc_state *state,
			struct dc_plane_state *plane);
	void (*release_phantom_stream)(const struct dc *dc,
			struct dc_state *state,
			struct dc_stream_state *stream);
	void (*release_dsc)(struct resource_context *res_ctx, const struct resource_pool *pool, struct display_stream_compressor **dsc);
	enum mall_stream_type (*get_pipe_subvp_type)(const struct dc_state *state, const struct pipe_ctx *pipe_ctx);
	enum mall_stream_type (*get_stream_subvp_type)(const struct dc_state *state, const struct dc_stream_state *stream);
	struct dc_stream_state *(*get_paired_subvp_stream)(const struct dc_state *state, const struct dc_stream_state *stream);
	bool (*remove_phantom_streams_and_planes)(
			const struct dc *dc,
			struct dc_state *state);
	void (*release_phantom_streams_and_planes)(
			const struct dc *dc,
			struct dc_state *state);
	unsigned int (*calculate_mall_ways_from_bytes)(
				const struct dc *dc,
				unsigned int total_size_in_mall_bytes);
};

struct dml2_clks_table_entry {
	unsigned int dcfclk_mhz;
	unsigned int fclk_mhz;
	unsigned int memclk_mhz;
	unsigned int socclk_mhz;
	unsigned int dtbclk_mhz;
	unsigned int dispclk_mhz;
	unsigned int dppclk_mhz;
	unsigned int dram_speed_mts; /*which is based on wck_ratio*/
};

struct dml2_clks_num_entries {
	unsigned int num_dcfclk_levels;
	unsigned int num_fclk_levels;
	unsigned int num_memclk_levels;
	unsigned int num_socclk_levels;
	unsigned int num_dtbclk_levels;
	unsigned int num_dispclk_levels;
	unsigned int num_dppclk_levels;
};

struct dml2_clks_limit_table {
	struct dml2_clks_table_entry clk_entries[DML2_MAX_NUM_DPM_LVL];
	struct dml2_clks_num_entries num_entries_per_clk;
	unsigned int num_states;
};

// Various overrides, per ASIC or per SKU specific, or for debugging purpose when/if available
struct dml2_soc_bbox_overrides {
	double xtalclk_mhz;
	double dchub_refclk_mhz;
	double dprefclk_mhz;
	double disp_pll_vco_speed_mhz;
	double urgent_latency_us;
	double sr_exit_latency_us;
	double sr_enter_plus_exit_latency_us;
	double sr_exit_z8_time_us;
	double sr_enter_plus_exit_z8_time_us;
	double dram_clock_change_latency_us;
	double fclk_change_latency_us;
	unsigned int dram_num_chan;
	unsigned int dram_chanel_width_bytes;
	struct dml2_clks_limit_table clks_table;
};

enum dml2_force_pstate_methods {
	dml2_force_pstate_method_auto = 0,
	dml2_force_pstate_method_vactive,
	dml2_force_pstate_method_vblank,
	dml2_force_pstate_method_drr,
	dml2_force_pstate_method_subvp,
};

struct dml2_configuration_options {
	int dcn_pipe_count;
	bool use_native_pstate_optimization;
	bool enable_windowed_mpo_odm;
	bool use_native_soc_bb_construction;
	bool skip_hw_state_mapping;
	bool optimize_odm_4to1;
	bool minimize_dispclk_using_odm;
	bool override_det_buffer_size_kbytes;
	struct dml2_dc_callbacks callbacks;
	struct {
		bool force_disable_subvp;
		bool force_enable_subvp;
		unsigned int subvp_fw_processing_delay_us;
		unsigned int subvp_pstate_allow_width_us;
		unsigned int subvp_prefetch_end_to_mall_start_us;
		unsigned int subvp_swath_height_margin_lines;
		struct dml2_dc_svp_callbacks callbacks;
	} svp_pstate;
	struct dml2_soc_mall_info mall_cfg;
	struct dml2_soc_bbox_overrides bbox_overrides;
	unsigned int max_segments_per_hubp;
	unsigned int det_segment_size;
	/* Only for debugging purposes when initializing SOCBB params via tool for DML21. */
	struct socbb_ip_params_external *external_socbb_ip_params;
	struct {
		bool force_pstate_method_enable;
		enum dml2_force_pstate_methods force_pstate_method_values[MAX_PIPES];
	} pmo;
	bool map_dc_pipes_with_callbacks;

	bool use_clock_dc_limits;
	bool gpuvm_enable;
	bool force_tdlut_enable;
	struct dml2_soc_bb *bb_from_dmub;
};

/*
 * dml2_create - Creates dml2_context.
 * @in_dc: dc.
 * @config: dml2 configuration options.
 * @dml2: Created dml2 context.
 *
 * Create and destroy of DML2 is done as part of dc_state creation
 * and dc_state_free. DML2 IP, SOC and STATES are initialized at
 * creation time.
 *
 * Return: True if dml2 is successfully created, false otherwise.
 */
bool dml2_create(const struct dc *in_dc,
				 const struct dml2_configuration_options *config,
				 struct dml2_context **dml2);

void dml2_destroy(struct dml2_context *dml2);
void dml2_copy(struct dml2_context *dst_dml2,
	struct dml2_context *src_dml2);
bool dml2_create_copy(struct dml2_context **dst_dml2,
	struct dml2_context *src_dml2);
void dml2_reinit(const struct dc *in_dc,
				 const struct dml2_configuration_options *config,
				 struct dml2_context **dml2);

/*
 * dml2_validate - Determines if a display configuration is supported or not.
 * @in_dc: dc.
 * @context: dc_state to be validated.
 * @fast_validate: Fast validate will not populate context.res_ctx.
 *
 * DML1.0 compatible interface for validation.
 *
 * Based on fast_validate option internally would call:
 *
 * -dml2_validate_and_build_resource - for non fast_validate option
 * Calculates if dc_state can be supported on the SOC, and attempts to
 * optimize the power management feature supports versus minimum clocks.
 * If supported, also builds out_new_hw_state to represent the hw programming
 * for the new dc state.
 *
 * -dml2_validate_only - for fast_validate option
 * Calculates if dc_state can be supported on the SOC (i.e. at maximum
 * clocks) with all mandatory power features enabled.

 * Context: Two threads may not invoke this function concurrently unless they reference
 *          separate dc_states for validation.
 * Return: True if mode is supported, false otherwise.
 */
bool dml2_validate(const struct dc *in_dc,
				   struct dc_state *context,
				   struct dml2_context *dml2,
				   bool fast_validate);

/*
 * dml2_extract_dram_and_fclk_change_support - Extracts the FCLK and UCLK change support info.
 * @dml2: input dml2 context pointer.
 * @fclk_change_support: output pointer holding the fclk change support info (vactive, vblank, unsupported).
 * @dram_clk_change_support: output pointer holding the uclk change support info (vactive, vblank, unsupported).
 */
void dml2_extract_dram_and_fclk_change_support(struct dml2_context *dml2,
	unsigned int *fclk_change_support, unsigned int *dram_clk_change_support);
void dml2_prepare_mcache_programming(struct dc *in_dc, struct dc_state *context, struct dml2_context *dml2);
#endif //_DML2_WRAPPER_H_
