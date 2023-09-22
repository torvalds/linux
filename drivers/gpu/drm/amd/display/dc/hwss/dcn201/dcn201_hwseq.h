/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#ifndef __DC_HWSS_DCN201_H__
#define __DC_HWSS_DCN201_H__

#include "hw_sequencer_private.h"

void dcn201_set_dmdata_attributes(struct pipe_ctx *pipe_ctx);
void dcn201_init_hw(struct dc *dc);
void dcn201_unblank_stream(struct pipe_ctx *pipe_ctx,
		struct dc_link_settings *link_settings);
void dcn201_update_plane_addr(const struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn201_plane_atomic_disconnect(struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn201_update_mpcc(struct dc *dc, struct pipe_ctx *pipe_ctx);
void dcn201_set_cursor_attribute(struct pipe_ctx *pipe_ctx);
void dcn201_pipe_control_lock(
	struct dc *dc,
	struct pipe_ctx *pipe,
	bool lock);
void dcn201_init_blank(
		struct dc *dc,
		struct timing_generator *tg);
#endif /* __DC_HWSS_DCN201_H__ */
