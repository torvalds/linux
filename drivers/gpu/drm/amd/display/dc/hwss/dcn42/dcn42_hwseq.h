/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Advanced Micro Devices, Inc. */

#ifndef __DC_HWSS_DCN42_H__
#define __DC_HWSS_DCN42_H__

#include "dc.h"
#include "hw_sequencer_private.h"

void dcn42_init_hw(struct dc *dc);
void dcn42_update_mpcc(struct dc *dc, struct pipe_ctx *pipe_ctx);

void dcn42_program_cm_hist(
	struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	const struct dc_plane_state *plane_state);

bool dcn42_set_mcm_luts(struct pipe_ctx *pipe_ctx,
	const struct dc_plane_state *plane_state);

void dcn42_populate_mcm_luts(struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_cm2_func_luts mcm_luts,
		bool lut_bank_a);

bool dcn42_program_rmcm_luts(
	struct hubp *hubp,
	struct pipe_ctx *pipe_ctx,
	enum dc_cm2_transfer_func_source lut3d_src,
	struct dc_cm2_func_luts *mcm_luts,
	struct mpc *mpc,
	bool lut_bank_a,
	int mpcc_id);
void dcn42_hardware_release(struct dc *dc);

void dcn42_prepare_bandwidth(
	struct dc *dc,
	struct dc_state *context);
void dcn42_optimize_bandwidth(struct dc *dc, struct dc_state *context);
void dcn42_calc_blocks_to_gate(struct dc *dc, struct dc_state *context,
		struct pg_block_update *update_state);
void dcn42_calc_blocks_to_ungate(struct dc *dc, struct dc_state *context,
		struct pg_block_update *update_state);
void dcn42_hw_block_power_down(struct dc *dc,
	struct pg_block_update *update_state);
void dcn42_hw_block_power_up(struct dc *dc,
		struct pg_block_update *update_state);
void dcn42_root_clock_control(struct dc *dc,
		struct pg_block_update *update_state, bool power_on);
void dcn42_dmub_hw_control_lock(struct dc *dc, struct dc_state *context, bool lock);
void dcn42_dmub_hw_control_lock_fast(union block_sequence_params *params);
void dcn42_setup_stereo(struct pipe_ctx *pipe_ctx, struct dc *dc);
#endif
