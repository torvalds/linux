/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
#include "link_hwss_hpo_frl.h"
#include "core_types.h"
#include "virtual/virtual_link_hwss.h"

static void setup_hpo_frl_stream_attribute(struct pipe_ctx *pipe_ctx)
{
	struct hpo_frl_stream_encoder *stream_enc = pipe_ctx->stream_res.hpo_frl_stream_enc;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct pipe_ctx *odm_pipe;
	int odm_combine_num_segments = 1;

	/* get number of ODM combine input segments */
	for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
		odm_combine_num_segments++;

	stream_enc->funcs->hdmi_frl_set_stream_attribute(
			stream_enc,
			&stream->timing,
			&stream->link->frl_link_settings.borrow_params,
			odm_combine_num_segments);
}

static const struct link_hwss hpo_frl_link_hwss = {
	.setup_stream_encoder = virtual_setup_stream_encoder,
	.reset_stream_encoder = virtual_reset_stream_encoder,
	.setup_stream_attribute = setup_hpo_frl_stream_attribute,
};

bool can_use_hpo_frl_link_hwss(const struct dc_link *link,
		const struct link_resource *link_res)
{
	return link_res->hpo_frl_link_enc != NULL;
}

const struct link_hwss *get_hpo_frl_link_hwss(void)
{
	return &hpo_frl_link_hwss;
}
