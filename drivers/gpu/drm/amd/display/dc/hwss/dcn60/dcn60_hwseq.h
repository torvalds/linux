// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DC_HWSS_DCN60_H__
#define __DC_HWSS_DCN60_H__

#include "inc/core_types.h"
#include "dc.h"
#include "dc_stream.h"
#include "hw_sequencer_private.h"
#include "dcn401/dcn401_dccg.h"

struct dc;

enum dc_status dcn60_apply_ctx_to_hw(
		struct dc *dc,
		struct dc_state *context);

enum dc_status dcn60_apply_single_controller_ctx_to_hw(
    struct pipe_ctx *pipe_ctx,
    struct dc_state *context,
    struct dc *dc);

void dcn60_init_hw(struct dc *dc);
void dcn60_set_cursor_attribute(struct pipe_ctx *pipe_ctx);
void dcn60_update_cursor_offload_pipe(struct dc *dc, const struct pipe_ctx *pipe);

void dcn60_program_perfmon(struct dc *dc, struct dc_state *context);
bool dcn60_apply_idle_power_optimizations(struct dc *dc, bool enable);

#endif /* __DC_HWSS_DCN60_H__ */
