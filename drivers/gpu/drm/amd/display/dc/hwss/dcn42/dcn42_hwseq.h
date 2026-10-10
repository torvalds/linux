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

bool dcn42_set_mcm_luts(struct dc *dc, struct dpp *dpp, struct hubp *hubp,
	struct hubp *primary_hubp, struct mpc *mpc, int mpcc_id,
	struct dc_stream_state *stream,
	struct dc_plane_state *plane_state);

void dcn42_disable_rmcm_luts(struct dc *dc, struct rmcm *rmcm, struct hubp *hubp, int mpcc_id);
void dcn42_release_rmcm_from_mpcc(struct dc *dc, int mpcc_id, const struct rmcm *keep);
void dcn42_disable_mcm_luts(struct mpc *mpc, int mpcc_id);
void dcn42_trigger_3dlut_dma_load(struct pipe_ctx *pipe_ctx);

bool dcn42_set_rmcm_luts(struct set_input_transfer_func_params *params);

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
bool dcn42_dmub_hw_control_lock(struct dc *dc, struct dc_state *context, bool lock);
bool dcn42_is_dmub_hw_lock_supported(const struct dc *dc);
void dcn42_setup_stereo(struct pipe_ctx *pipe_ctx, struct dc *dc);
void dcn42_power_down_on_boot(struct dc *dc);
#endif
