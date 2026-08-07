// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DC_HWSS_DCN50_H__
#define __DC_HWSS_DCN50_H__

#include "inc/core_types.h"
#include "dc.h"
#include "dc_stream.h"
#include "hw_sequencer_private.h"
#include "dcn401/dcn401_dccg.h"

struct dc;

void dcn50_init_hw(struct dc *dc);

void dcn50_update_dchubp_dpp(
	struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct dc_state *context);
void dcn50_update_dchubp_dpp_sequence(struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct dc_state *context,
	struct block_sequence_state *seq_state);
void dcn50_update_mpcc_sequence(struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct block_sequence_state *seq_state);
void dcn50_program_front_end_for_ctx(struct dc *dc, struct dc_state *context);
void dcn50_post_unlock_program_front_end(struct dc *dc, struct dc_state *context);

#endif /* __DC_HWSS_DCN50_H__ */
