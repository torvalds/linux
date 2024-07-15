/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2024 Advanced Micro Devices, Inc.
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

#include "core_types.h"
#include "resource.h"
#include "dcn351_hwseq.h"
#include "dcn35/dcn35_hwseq.h"

#define DC_LOGGER_INIT(logger) \
	struct dal_logger *dc_logger = logger

#define DC_LOGGER \
	dc_logger

void dcn351_calc_blocks_to_gate(struct dc *dc, struct dc_state *context,
	struct pg_block_update *update_state)
{
	int i, j;

	dcn35_calc_blocks_to_gate(dc, context, update_state);

	for (i = dc->res_pool->pipe_count - 1; i >= 0; i--) {
		if (!update_state->pg_pipe_res_update[PG_HUBP][i] &&
			!update_state->pg_pipe_res_update[PG_DPP][i]) {
			for (j = i - 1; j >= 0; j--) {
				update_state->pg_pipe_res_update[PG_HUBP][j] = false;
				update_state->pg_pipe_res_update[PG_DPP][j] = false;
			}

			break;
		}
	}
}

void dcn351_calc_blocks_to_ungate(struct dc *dc, struct dc_state *context,
	struct pg_block_update *update_state)
{
	int i, j;

	dcn35_calc_blocks_to_ungate(dc, context, update_state);

	for (i = dc->res_pool->pipe_count - 1; i >= 0; i--) {
		if (update_state->pg_pipe_res_update[PG_HUBP][i] &&
			update_state->pg_pipe_res_update[PG_DPP][i]) {
			for (j = i - 1; j >= 0; j--) {
				update_state->pg_pipe_res_update[PG_HUBP][j] = true;
				update_state->pg_pipe_res_update[PG_DPP][j] = true;
			}

			break;
		}
	}
}

/**
 * dcn351_hw_block_power_down() - power down sequence
 *
 * The following sequence describes the ON-OFF (ONO) for power down:
 *
 *	ONO Region 11, DCPG 19: dsc3
 *	ONO Region 10, DCPG 3: dchubp3, dpp3
 *	ONO Region 9, DCPG 18: dsc2
 *	ONO Region 8, DCPG 2: dchubp2, dpp2
 *	ONO Region 7, DCPG 17: dsc1
 *	ONO Region 6, DCPG 1: dchubp1, dpp1
 *	ONO Region 5, DCPG 16: dsc0
 *	ONO Region 4, DCPG 0: dchubp0, dpp0
 *	ONO Region 3, DCPG 25: hpo - SKIPPED. Should be kept on
 *	ONO Region 2, DCPG 24: mpc opp optc dwb
 *	ONO Region 1, DCPG 23: dchubbub dchvm dchubbubmem - SKIPPED. PMFW will pwr dwn at IPS2 entry
 *	ONO Region 0, DCPG 22: dccg dio dcio - SKIPPED. will be pwr dwn after lono timer is armed
 *
 * @dc: Current DC state
 * @update_state: update PG sequence states for HW block
 */
void dcn351_hw_block_power_down(struct dc *dc,
	struct pg_block_update *update_state)
{
	int i = 0;
	struct pg_cntl *pg_cntl = dc->res_pool->pg_cntl;

	if (!pg_cntl || dc->debug.ignore_pg)
		return;

	for (i = dc->res_pool->pipe_count - 1; i >= 0; i--) {
		if (update_state->pg_pipe_res_update[PG_DSC][i]) {
			if (pg_cntl->funcs->dsc_pg_control)
				pg_cntl->funcs->dsc_pg_control(pg_cntl, i, false);
		}

		if (update_state->pg_pipe_res_update[PG_HUBP][i] &&
			update_state->pg_pipe_res_update[PG_DPP][i]) {
			if (pg_cntl->funcs->hubp_dpp_pg_control)
				pg_cntl->funcs->hubp_dpp_pg_control(pg_cntl, i, false);
		}
	}

	// domain25 currently always on.

	/* this will need all the clients to unregister optc interrupts, let dmubfw handle this */
	if (pg_cntl->funcs->plane_otg_pg_control)
		pg_cntl->funcs->plane_otg_pg_control(pg_cntl, false);

	// domain23 currently always on.
	// domain22 currently always on.
}

/**
 * dcn351_hw_block_power_up() - power up sequence
 *
 * The following sequence describes the ON-OFF (ONO) for power up:
 *
 *	ONO Region 0, DCPG 22: dccg dio dcio - SKIPPED
 *	ONO Region 1, DCPG 23: dchubbub dchvm dchubbubmem - SKIPPED. PMFW will power up at IPS2 exit
 *	ONO Region 2, DCPG 24: mpc opp optc dwb
 *	ONO Region 3, DCPG 25: hpo - SKIPPED
 *	ONO Region 4, DCPG 0: dchubp0, dpp0
 *	ONO Region 5, DCPG 16: dsc0
 *	ONO Region 6, DCPG 1: dchubp1, dpp1
 *	ONO Region 7, DCPG 17: dsc1
 *	ONO Region 8, DCPG 2: dchubp2, dpp2
 *	ONO Region 9, DCPG 18: dsc2
 *	ONO Region 10, DCPG 3: dchubp3, dpp3
 *	ONO Region 11, DCPG 19: dsc3
 *
 * @dc: Current DC state
 * @update_state: update PG sequence states for HW block
 */
void dcn351_hw_block_power_up(struct dc *dc,
	struct pg_block_update *update_state)
{
	int i = 0;
	struct pg_cntl *pg_cntl = dc->res_pool->pg_cntl;

	if (!pg_cntl || dc->debug.ignore_pg)
		return;

	// domain22 currently always on.
	// domain23 currently always on.

	/* this will need all the clients to unregister optc interrupts, let dmubfw handle this */
	if (pg_cntl->funcs->plane_otg_pg_control)
		pg_cntl->funcs->plane_otg_pg_control(pg_cntl, true);

	// domain25 currently always on.

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (update_state->pg_pipe_res_update[PG_HUBP][i] &&
			update_state->pg_pipe_res_update[PG_DPP][i]) {
			if (pg_cntl->funcs->hubp_dpp_pg_control)
				pg_cntl->funcs->hubp_dpp_pg_control(pg_cntl, i, true);
		}

		if (update_state->pg_pipe_res_update[PG_DSC][i]) {
			if (pg_cntl->funcs->dsc_pg_control)
				pg_cntl->funcs->dsc_pg_control(pg_cntl, i, true);
		}
	}
}
