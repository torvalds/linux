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

#ifndef __DC_LINK_DPMS_H__
#define __DC_LINK_DPMS_H__

#include "link_service.h"
void link_set_dpms_on(
		struct dc_state *state,
		struct pipe_ctx *pipe_ctx);
void link_set_dpms_off(struct pipe_ctx *pipe_ctx);
void link_resume(struct dc_link *link);
void link_blank_all_dp_displays(struct dc *dc);
void link_blank_all_edp_displays(struct dc *dc);
void link_blank_dp_stream(struct dc_link *link, bool hw_init);
void link_set_all_streams_dpms_off_for_link(struct dc_link *link);
void link_get_master_pipes_with_dpms_on(const struct dc_link *link,
		struct dc_state *state,
		uint8_t *count,
		struct pipe_ctx *pipes[MAX_PIPES]);
enum dc_status link_increase_mst_payload(struct pipe_ctx *pipe_ctx, uint32_t req_pbn);
enum dc_status link_reduce_mst_payload(struct pipe_ctx *pipe_ctx, uint32_t req_pbn);
bool link_set_dsc_pps_packet(struct pipe_ctx *pipe_ctx,
		bool enable, bool immediate_update);
struct fixed31_32 link_calculate_sst_avg_time_slots_per_mtp(
		const struct dc_stream_state *stream,
		const struct dc_link *link);
void link_set_dsc_on_stream(struct pipe_ctx *pipe_ctx, bool enable);
bool link_set_dsc_enable(struct pipe_ctx *pipe_ctx, bool enable);
bool link_update_dsc_config(struct pipe_ctx *pipe_ctx);
#endif /* __DC_LINK_DPMS_H__ */
