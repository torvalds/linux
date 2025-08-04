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

#ifndef __DC_HW_SEQUENCER_PRIVATE_H__
#define __DC_HW_SEQUENCER_PRIVATE_H__

#include "dc_types.h"

enum pipe_gating_control {
	PIPE_GATING_CONTROL_DISABLE = 0,
	PIPE_GATING_CONTROL_ENABLE,
	PIPE_GATING_CONTROL_INIT
};

struct dce_hwseq_wa {
	bool blnd_crtc_trigger;
	bool DEGVIDCN10_253;
	bool false_optc_underflow;
	bool DEGVIDCN10_254;
	bool DEGVIDCN21;
	bool disallow_self_refresh_during_multi_plane_transition;
	bool dp_hpo_and_otg_sequence;
	bool wait_hubpret_read_start_during_mpo_transition;
};

struct hwseq_wa_state {
	bool DEGVIDCN10_253_applied;
	bool disallow_self_refresh_during_multi_plane_transition_applied;
	unsigned int disallow_self_refresh_during_multi_plane_transition_applied_on_frame;
	bool skip_blank_stream;
};

struct pipe_ctx;
struct dc_state;
struct dc_stream_status;
struct dc_writeback_info;
struct dchub_init_data;
struct dc_static_screen_params;
struct resource_pool;
struct resource_context;
struct stream_resource;
struct dc_phy_addr_space_config;
struct dc_virtual_addr_space_config;
struct hubp;
struct dpp;
struct dce_hwseq;
struct timing_generator;
struct tg_color;
struct output_pixel_processor;
struct mpcc_blnd_cfg;

struct hwseq_private_funcs {

	void (*disable_stream_gating)(struct dc *dc, struct pipe_ctx *pipe_ctx);
	void (*enable_stream_gating)(struct dc *dc, struct pipe_ctx *pipe_ctx);
	void (*init_pipes)(struct dc *dc, struct dc_state *context);
	void (*reset_hw_ctx_wrap)(struct dc *dc, struct dc_state *context);
	void (*plane_atomic_disconnect)(struct dc *dc,
			struct dc_state *state,
			struct pipe_ctx *pipe_ctx);
	void (*update_mpcc)(struct dc *dc, struct pipe_ctx *pipe_ctx);
	bool (*set_input_transfer_func)(struct dc *dc,
				struct pipe_ctx *pipe_ctx,
				const struct dc_plane_state *plane_state);
	bool (*set_output_transfer_func)(struct dc *dc,
				struct pipe_ctx *pipe_ctx,
				const struct dc_stream_state *stream);
	void (*power_down)(struct dc *dc);
	void (*enable_display_pipe_clock_gating)(struct dc_context *ctx,
					bool clock_gating);
	bool (*enable_display_power_gating)(struct dc *dc,
					uint8_t controller_id,
					struct dc_bios *dcb,
					enum pipe_gating_control power_gating);
	void (*blank_pixel_data)(struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			bool blank);
	enum dc_status (*enable_stream_timing)(
			struct pipe_ctx *pipe_ctx,
			struct dc_state *context,
			struct dc *dc);
	void (*edp_backlight_control)(struct dc_link *link,
			bool enable);
	void (*setup_vupdate_interrupt)(struct dc *dc,
			struct pipe_ctx *pipe_ctx);
	bool (*did_underflow_occur)(struct dc *dc, struct pipe_ctx *pipe_ctx);
	void (*init_blank)(struct dc *dc, struct timing_generator *tg);
	void (*disable_vga)(struct dce_hwseq *hws);
	void (*bios_golden_init)(struct dc *dc);
	void (*plane_atomic_power_down)(struct dc *dc,
			struct dpp *dpp,
			struct hubp *hubp);
	void (*plane_atomic_disable)(struct dc *dc, struct pipe_ctx *pipe_ctx);
	void (*enable_power_gating_plane)(struct dce_hwseq *hws,
		bool enable);
	void (*dpp_root_clock_control)(
			struct dce_hwseq *hws,
			unsigned int dpp_inst,
			bool clock_on);
	void (*dpstream_root_clock_control)(
			struct dce_hwseq *hws,
			unsigned int dpp_inst,
			bool clock_on);
	void (*physymclk_root_clock_control)(
			struct dce_hwseq *hws,
			unsigned int phy_inst,
			bool clock_on);
	void (*dpp_pg_control)(struct dce_hwseq *hws,
			unsigned int dpp_inst,
			bool power_on);
	void (*hubp_pg_control)(struct dce_hwseq *hws,
			unsigned int hubp_inst,
			bool power_on);
	void (*dsc_pg_control)(struct dce_hwseq *hws,
			unsigned int dsc_inst,
			bool power_on);
	bool (*dsc_pg_status)(struct dce_hwseq *hws,
			unsigned int dsc_inst);
	void (*update_odm)(struct dc *dc, struct dc_state *context,
			struct pipe_ctx *pipe_ctx);
	void (*program_all_writeback_pipes_in_tree)(struct dc *dc,
			const struct dc_stream_state *stream,
			struct dc_state *context);
	bool (*s0i3_golden_init_wa)(struct dc *dc);
	void (*set_hdr_multiplier)(struct pipe_ctx *pipe_ctx);
	void (*verify_allow_pstate_change_high)(struct dc *dc);
	void (*program_pipe)(struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			struct dc_state *context);
	bool (*wait_for_blank_complete)(struct output_pixel_processor *opp);
	void (*dccg_init)(struct dce_hwseq *hws);
	bool (*set_blend_lut)(struct pipe_ctx *pipe_ctx,
			const struct dc_plane_state *plane_state);
	bool (*set_shaper_3dlut)(struct pipe_ctx *pipe_ctx,
			const struct dc_plane_state *plane_state);
	bool (*set_mcm_luts)(struct pipe_ctx *pipe_ctx,
			const struct dc_plane_state *plane_state);
	void (*PLAT_58856_wa)(struct dc_state *context,
			struct pipe_ctx *pipe_ctx);
	void (*setup_hpo_hw_control)(const struct dce_hwseq *hws, bool enable);
	void (*enable_plane)(struct dc *dc, struct pipe_ctx *pipe_ctx,
			       struct dc_state *context);
	void (*program_mall_pipe_config)(struct dc *dc, struct dc_state *context);
	void (*update_force_pstate)(struct dc *dc, struct dc_state *context);
	void (*update_mall_sel)(struct dc *dc, struct dc_state *context);
	unsigned int (*calculate_dccg_k1_k2_values)(struct pipe_ctx *pipe_ctx,
			unsigned int *k1_div,
			unsigned int *k2_div);
	void (*resync_fifo_dccg_dio)(struct dce_hwseq *hws, struct dc *dc,
			struct dc_state *context,
			unsigned int current_pipe_idx);
	enum dc_status (*apply_single_controller_ctx_to_hw)(
			struct pipe_ctx *pipe_ctx,
			struct dc_state *context,
			struct dc *dc);
	bool (*is_dp_dig_pixel_rate_div_policy)(struct pipe_ctx *pipe_ctx);
	void (*reset_back_end_for_pipe)(struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			struct dc_state *context);
	void (*populate_mcm_luts)(struct dc *dc,
			struct pipe_ctx *pipe_ctx,
			struct dc_cm2_func_luts mcm_luts,
			bool lut_bank_a);
	void (*perform_3dlut_wa_unlock)(struct pipe_ctx *pipe_ctx);
	void (*wait_for_pipe_update_if_needed)(struct dc *dc, struct pipe_ctx *pipe_ctx, bool is_surface_update_only);
	void (*set_wait_for_update_needed_for_pipe)(struct dc *dc, struct pipe_ctx *pipe_ctx);
};

struct dce_hwseq {
	struct dc_context *ctx;
	const struct dce_hwseq_registers *regs;
	const struct dce_hwseq_shift *shifts;
	const struct dce_hwseq_mask *masks;
	struct dce_hwseq_wa wa;
	struct hwseq_wa_state wa_state;
	struct hwseq_private_funcs funcs;

	PHYSICAL_ADDRESS_LOC fb_base;
	PHYSICAL_ADDRESS_LOC fb_top;
	PHYSICAL_ADDRESS_LOC fb_offset;
	PHYSICAL_ADDRESS_LOC uma_top;
};

#endif /* __DC_HW_SEQUENCER_PRIVATE_H__ */
