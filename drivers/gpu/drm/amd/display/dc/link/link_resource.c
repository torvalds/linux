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
/* FILE POLICY AND INTENDED USAGE:
 * This file implements accessors to link resource.
 */

#include "link_resource.h"
#include "protocols/link_dp_capability.h"

void link_get_cur_link_res(const struct dc_link *link,
		struct link_resource *link_res)
{
	int i;
	struct pipe_ctx *pipe = NULL;

	memset(link_res, 0, sizeof(*link_res));

	for (i = 0; i < MAX_PIPES; i++) {
		pipe = &link->dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe->stream && pipe->stream->link && pipe->top_pipe == NULL) {
			if (pipe->stream->link == link) {
				*link_res = pipe->link_res;
				break;
			}
		}
	}

}

void link_get_cur_res_map(const struct dc *dc, uint32_t *map)
{
	struct dc_link *link;
	uint32_t i;
	uint32_t hpo_dp_recycle_map = 0;

	*map = 0;

	if (dc->caps.dp_hpo) {
		for (i = 0; i < dc->caps.max_links; i++) {
			link = dc->links[i];
			if (link->link_status.link_active &&
					link_dp_get_encoding_format(&link->reported_link_cap) == DP_128b_132b_ENCODING &&
					link_dp_get_encoding_format(&link->cur_link_settings) != DP_128b_132b_ENCODING)
				/* hpo dp link encoder is considered as recycled, when RX reports 128b/132b encoding capability
				 * but current link doesn't use it.
				 */
				hpo_dp_recycle_map |= (1 << i);
		}
		*map |= (hpo_dp_recycle_map << LINK_RES_HPO_DP_REC_MAP__SHIFT);
	}
}

void link_restore_res_map(const struct dc *dc, uint32_t *map)
{
	struct dc_link *link;
	uint32_t i;
	unsigned int available_hpo_dp_count;
	uint32_t hpo_dp_recycle_map = (*map & LINK_RES_HPO_DP_REC_MAP__MASK)
			>> LINK_RES_HPO_DP_REC_MAP__SHIFT;

	if (dc->caps.dp_hpo) {
		available_hpo_dp_count = dc->res_pool->hpo_dp_link_enc_count;
		/* remove excess 128b/132b encoding support for not recycled links */
		for (i = 0; i < dc->caps.max_links; i++) {
			if ((hpo_dp_recycle_map & (1 << i)) == 0) {
				link = dc->links[i];
				if (link->type != dc_connection_none &&
						link_dp_get_encoding_format(&link->verified_link_cap) == DP_128b_132b_ENCODING) {
					if (available_hpo_dp_count > 0)
						available_hpo_dp_count--;
					else
						/* remove 128b/132b encoding capability by limiting verified link rate to HBR3 */
						link->verified_link_cap.link_rate = LINK_RATE_HIGH3;
				}
			}
		}
		/* remove excess 128b/132b encoding support for recycled links */
		for (i = 0; i < dc->caps.max_links; i++) {
			if ((hpo_dp_recycle_map & (1 << i)) != 0) {
				link = dc->links[i];
				if (link->type != dc_connection_none &&
						link_dp_get_encoding_format(&link->verified_link_cap) == DP_128b_132b_ENCODING) {
					if (available_hpo_dp_count > 0)
						available_hpo_dp_count--;
					else
						/* remove 128b/132b encoding capability by limiting verified link rate to HBR3 */
						link->verified_link_cap.link_rate = LINK_RATE_HIGH3;
				}
			}
		}
	}
}
