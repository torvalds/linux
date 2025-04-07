/*
* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DC_HWSS_DCE100_H__
#define __DC_HWSS_DCE100_H__

#include "core_types.h"
#include "hw_sequencer_private.h"

struct dc;
struct dc_state;

void dce100_hw_sequencer_construct(struct dc *dc);

void dce100_prepare_bandwidth(
		struct dc *dc,
		struct dc_state *context);

void dce100_optimize_bandwidth(
		struct dc *dc,
		struct dc_state *context);

bool dce100_enable_display_power_gating(struct dc *dc, uint8_t controller_id,
					struct dc_bios *dcb,
					enum pipe_gating_control power_gating);

void dce100_reset_surface_dcc_and_tiling(struct pipe_ctx *pipe_ctx,
					struct dc_plane_state *plane_state,
					bool clear_tiling);

#endif /* __DC_HWSS_DCE100_H__ */

