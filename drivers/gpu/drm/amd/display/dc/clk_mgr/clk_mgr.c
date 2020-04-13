/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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

#include <linux/slab.h>

#include "dal_asic_id.h"
#include "dc_types.h"
#include "dccg.h"
#include "clk_mgr_internal.h"

#include "dce100/dce_clk_mgr.h"
#include "dce110/dce110_clk_mgr.h"
#include "dce112/dce112_clk_mgr.h"
#include "dce120/dce120_clk_mgr.h"
#include "dcn10/rv1_clk_mgr.h"
#include "dcn10/rv2_clk_mgr.h"
#include "dcn20/dcn20_clk_mgr.h"
#include "dcn21/rn_clk_mgr.h"


int clk_mgr_helper_get_active_display_cnt(
		struct dc *dc,
		struct dc_state *context)
{
	int i, display_count;

	display_count = 0;
	for (i = 0; i < context->stream_count; i++) {
		const struct dc_stream_state *stream = context->streams[i];

		/*
		 * Only notify active stream or virtual stream.
		 * Need to notify virtual stream to work around
		 * headless case. HPD does not fire when system is in
		 * S0i2.
		 */
		if (!stream->dpms_off || stream->signal == SIGNAL_TYPE_VIRTUAL)
			display_count++;
	}

	return display_count;
}

int clk_mgr_helper_get_active_plane_cnt(
		struct dc *dc,
		struct dc_state *context)
{
	int i, total_plane_count;

	total_plane_count = 0;
	for (i = 0; i < context->stream_count; i++) {
		const struct dc_stream_status stream_status = context->stream_status[i];

		/*
		 * Sum up plane_count for all streams ( active and virtual ).
		 */
		total_plane_count += stream_status.plane_count;
	}

	return total_plane_count;
}

void clk_mgr_exit_optimized_pwr_state(const struct dc *dc, struct clk_mgr *clk_mgr)
{
	struct dc_link *edp_link = get_edp_link(dc);

	if (dc->hwss.exit_optimized_pwr_state)
		dc->hwss.exit_optimized_pwr_state(dc, dc->current_state);

	if (edp_link) {
		clk_mgr->psr_allow_active_cache = edp_link->psr_allow_active;
		dc_link_set_psr_allow_active(edp_link, false, false);
	}

}

void clk_mgr_optimize_pwr_state(const struct dc *dc, struct clk_mgr *clk_mgr)
{
	struct dc_link *edp_link = get_edp_link(dc);

	if (edp_link)
		dc_link_set_psr_allow_active(edp_link, clk_mgr->psr_allow_active_cache, false);

	if (dc->hwss.optimize_pwr_state)
		dc->hwss.optimize_pwr_state(dc, dc->current_state);

}

struct clk_mgr *dc_clk_mgr_create(struct dc_context *ctx, struct pp_smu_funcs *pp_smu, struct dccg *dccg)
{
	struct hw_asic_id asic_id = ctx->asic_id;

	struct clk_mgr_internal *clk_mgr = kzalloc(sizeof(*clk_mgr), GFP_KERNEL);

	if (clk_mgr == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	switch (asic_id.chip_family) {
	case FAMILY_CI:
	case FAMILY_KV:
		dce_clk_mgr_construct(ctx, clk_mgr);
		break;
	case FAMILY_CZ:
		dce110_clk_mgr_construct(ctx, clk_mgr);
		break;
	case FAMILY_VI:
		if (ASIC_REV_IS_TONGA_P(asic_id.hw_internal_rev) ||
				ASIC_REV_IS_FIJI_P(asic_id.hw_internal_rev)) {
			dce_clk_mgr_construct(ctx, clk_mgr);
			break;
		}
		if (ASIC_REV_IS_POLARIS10_P(asic_id.hw_internal_rev) ||
				ASIC_REV_IS_POLARIS11_M(asic_id.hw_internal_rev) ||
				ASIC_REV_IS_POLARIS12_V(asic_id.hw_internal_rev)) {
			dce112_clk_mgr_construct(ctx, clk_mgr);
			break;
		}
		if (ASIC_REV_IS_VEGAM(asic_id.hw_internal_rev)) {
			dce112_clk_mgr_construct(ctx, clk_mgr);
			break;
		}
		break;
	case FAMILY_AI:
		if (ASICREV_IS_VEGA20_P(asic_id.hw_internal_rev))
			dce121_clk_mgr_construct(ctx, clk_mgr);
		else
			dce120_clk_mgr_construct(ctx, clk_mgr);
		break;

#if defined(CONFIG_DRM_AMD_DC_DCN)
	case FAMILY_RV:
		if (ASICREV_IS_RENOIR(asic_id.hw_internal_rev)) {
			rn_clk_mgr_construct(ctx, clk_mgr, pp_smu, dccg);
			break;
		}
		if (ASICREV_IS_RAVEN2(asic_id.hw_internal_rev)) {
			rv2_clk_mgr_construct(ctx, clk_mgr, pp_smu);
			break;
		}
		if (ASICREV_IS_RAVEN(asic_id.hw_internal_rev) ||
				ASICREV_IS_PICASSO(asic_id.hw_internal_rev)) {
			rv1_clk_mgr_construct(ctx, clk_mgr, pp_smu);
			break;
		}
		break;

	case FAMILY_NV:
		dcn20_clk_mgr_construct(ctx, clk_mgr, pp_smu, dccg);
		break;
#endif	/* Family RV and NV*/

	default:
		ASSERT(0); /* Unknown Asic */
		break;
	}

	return &clk_mgr->base;
}

void dc_destroy_clk_mgr(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	kfree(clk_mgr);
}

