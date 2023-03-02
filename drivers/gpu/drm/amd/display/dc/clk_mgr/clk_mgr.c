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
#include "link.h"

#include "dce100/dce_clk_mgr.h"
#include "dce110/dce110_clk_mgr.h"
#include "dce112/dce112_clk_mgr.h"
#include "dce120/dce120_clk_mgr.h"
#include "dce60/dce60_clk_mgr.h"
#include "dcn10/rv1_clk_mgr.h"
#include "dcn10/rv2_clk_mgr.h"
#include "dcn20/dcn20_clk_mgr.h"
#include "dcn21/rn_clk_mgr.h"
#include "dcn201/dcn201_clk_mgr.h"
#include "dcn30/dcn30_clk_mgr.h"
#include "dcn301/vg_clk_mgr.h"
#include "dcn31/dcn31_clk_mgr.h"
#include "dcn314/dcn314_clk_mgr.h"
#include "dcn315/dcn315_clk_mgr.h"
#include "dcn316/dcn316_clk_mgr.h"
#include "dcn32/dcn32_clk_mgr.h"

int clk_mgr_helper_get_active_display_cnt(
		struct dc *dc,
		struct dc_state *context)
{
	int i, display_count;

	display_count = 0;
	for (i = 0; i < context->stream_count; i++) {
		const struct dc_stream_state *stream = context->streams[i];

		/* Don't count SubVP phantom pipes as part of active
		 * display count
		 */
		if (stream->mall_stream_config.type == SUBVP_PHANTOM)
			continue;

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
	struct dc_link *edp_links[MAX_NUM_EDP];
	struct dc_link *edp_link = NULL;
	int edp_num;
	unsigned int panel_inst;

	get_edp_links(dc, edp_links, &edp_num);
	if (dc->hwss.exit_optimized_pwr_state)
		dc->hwss.exit_optimized_pwr_state(dc, dc->current_state);

	if (edp_num) {
		for (panel_inst = 0; panel_inst < edp_num; panel_inst++) {
			bool allow_active = false;

			edp_link = edp_links[panel_inst];
			if (!edp_link->psr_settings.psr_feature_enabled)
				continue;
			clk_mgr->psr_allow_active_cache = edp_link->psr_settings.psr_allow_active;
			dc_link_set_psr_allow_active(edp_link, &allow_active, false, false, NULL);
		}
	}

}

void clk_mgr_optimize_pwr_state(const struct dc *dc, struct clk_mgr *clk_mgr)
{
	struct dc_link *edp_links[MAX_NUM_EDP];
	struct dc_link *edp_link = NULL;
	int edp_num;
	unsigned int panel_inst;

	get_edp_links(dc, edp_links, &edp_num);
	if (edp_num) {
		for (panel_inst = 0; panel_inst < edp_num; panel_inst++) {
			edp_link = edp_links[panel_inst];
			if (!edp_link->psr_settings.psr_feature_enabled)
				continue;
			dc_link_set_psr_allow_active(edp_link,
					&clk_mgr->psr_allow_active_cache, false, false, NULL);
		}
	}

	if (dc->hwss.optimize_pwr_state)
		dc->hwss.optimize_pwr_state(dc, dc->current_state);

}

struct clk_mgr *dc_clk_mgr_create(struct dc_context *ctx, struct pp_smu_funcs *pp_smu, struct dccg *dccg)
{
	struct hw_asic_id asic_id = ctx->asic_id;

	switch (asic_id.chip_family) {
#if defined(CONFIG_DRM_AMD_DC_SI)
	case FAMILY_SI: {
		struct clk_mgr_internal *clk_mgr = kzalloc(sizeof(*clk_mgr), GFP_KERNEL);

		if (clk_mgr == NULL) {
			BREAK_TO_DEBUGGER();
			return NULL;
		}
		dce60_clk_mgr_construct(ctx, clk_mgr);
		dce_clk_mgr_construct(ctx, clk_mgr);
		return &clk_mgr->base;
	}
#endif
	case FAMILY_CI:
	case FAMILY_KV: {
		struct clk_mgr_internal *clk_mgr = kzalloc(sizeof(*clk_mgr), GFP_KERNEL);

		if (clk_mgr == NULL) {
			BREAK_TO_DEBUGGER();
			return NULL;
		}
		dce_clk_mgr_construct(ctx, clk_mgr);
		return &clk_mgr->base;
	}
	case FAMILY_CZ: {
		struct clk_mgr_internal *clk_mgr = kzalloc(sizeof(*clk_mgr), GFP_KERNEL);

		if (clk_mgr == NULL) {
			BREAK_TO_DEBUGGER();
			return NULL;
		}
		dce110_clk_mgr_construct(ctx, clk_mgr);
		return &clk_mgr->base;
	}
	case FAMILY_VI: {
		struct clk_mgr_internal *clk_mgr = kzalloc(sizeof(*clk_mgr), GFP_KERNEL);

		if (clk_mgr == NULL) {
			BREAK_TO_DEBUGGER();
			return NULL;
		}
		if (ASIC_REV_IS_TONGA_P(asic_id.hw_internal_rev) ||
				ASIC_REV_IS_FIJI_P(asic_id.hw_internal_rev)) {
			dce_clk_mgr_construct(ctx, clk_mgr);
			return &clk_mgr->base;
		}
		if (ASIC_REV_IS_POLARIS10_P(asic_id.hw_internal_rev) ||
				ASIC_REV_IS_POLARIS11_M(asic_id.hw_internal_rev) ||
				ASIC_REV_IS_POLARIS12_V(asic_id.hw_internal_rev)) {
			dce112_clk_mgr_construct(ctx, clk_mgr);
			return &clk_mgr->base;
		}
		if (ASIC_REV_IS_VEGAM(asic_id.hw_internal_rev)) {
			dce112_clk_mgr_construct(ctx, clk_mgr);
			return &clk_mgr->base;
		}
		return &clk_mgr->base;
	}
	case FAMILY_AI: {
		struct clk_mgr_internal *clk_mgr = kzalloc(sizeof(*clk_mgr), GFP_KERNEL);

		if (clk_mgr == NULL) {
			BREAK_TO_DEBUGGER();
			return NULL;
		}
		if (ASICREV_IS_VEGA20_P(asic_id.hw_internal_rev))
			dce121_clk_mgr_construct(ctx, clk_mgr);
		else
			dce120_clk_mgr_construct(ctx, clk_mgr);
		return &clk_mgr->base;
	}
#if defined(CONFIG_DRM_AMD_DC_DCN)
	case FAMILY_RV: {
		struct clk_mgr_internal *clk_mgr = kzalloc(sizeof(*clk_mgr), GFP_KERNEL);

		if (clk_mgr == NULL) {
			BREAK_TO_DEBUGGER();
			return NULL;
		}

		if (ASICREV_IS_RENOIR(asic_id.hw_internal_rev)) {
			rn_clk_mgr_construct(ctx, clk_mgr, pp_smu, dccg);
			return &clk_mgr->base;
		}

		if (ASICREV_IS_GREEN_SARDINE(asic_id.hw_internal_rev)) {
			rn_clk_mgr_construct(ctx, clk_mgr, pp_smu, dccg);
			return &clk_mgr->base;
		}
		if (ASICREV_IS_RAVEN2(asic_id.hw_internal_rev)) {
			rv2_clk_mgr_construct(ctx, clk_mgr, pp_smu);
			return &clk_mgr->base;
		}
		if (ASICREV_IS_RAVEN(asic_id.hw_internal_rev) ||
				ASICREV_IS_PICASSO(asic_id.hw_internal_rev)) {
			rv1_clk_mgr_construct(ctx, clk_mgr, pp_smu);
			return &clk_mgr->base;
		}
		return &clk_mgr->base;
	}
	case FAMILY_NV: {
		struct clk_mgr_internal *clk_mgr = kzalloc(sizeof(*clk_mgr), GFP_KERNEL);

		if (clk_mgr == NULL) {
			BREAK_TO_DEBUGGER();
			return NULL;
		}
		if (ASICREV_IS_SIENNA_CICHLID_P(asic_id.hw_internal_rev)) {
			dcn3_clk_mgr_construct(ctx, clk_mgr, pp_smu, dccg);
			return &clk_mgr->base;
		}
		if (ASICREV_IS_DIMGREY_CAVEFISH_P(asic_id.hw_internal_rev)) {
			dcn3_clk_mgr_construct(ctx, clk_mgr, pp_smu, dccg);
			return &clk_mgr->base;
		}
		if (ASICREV_IS_BEIGE_GOBY_P(asic_id.hw_internal_rev)) {
			dcn3_clk_mgr_construct(ctx, clk_mgr, pp_smu, dccg);
			return &clk_mgr->base;
		}
		if (asic_id.chip_id == DEVICE_ID_NV_13FE) {
			dcn201_clk_mgr_construct(ctx, clk_mgr, pp_smu, dccg);
			return &clk_mgr->base;
		}
		dcn20_clk_mgr_construct(ctx, clk_mgr, pp_smu, dccg);
		return &clk_mgr->base;
	}
	case FAMILY_VGH:
		if (ASICREV_IS_VANGOGH(asic_id.hw_internal_rev)) {
			struct clk_mgr_vgh *clk_mgr = kzalloc(sizeof(*clk_mgr), GFP_KERNEL);

			if (clk_mgr == NULL) {
				BREAK_TO_DEBUGGER();
				return NULL;
			}
			vg_clk_mgr_construct(ctx, clk_mgr, pp_smu, dccg);
			return &clk_mgr->base.base;
		}
		break;

	case FAMILY_YELLOW_CARP: {
		struct clk_mgr_dcn31 *clk_mgr = kzalloc(sizeof(*clk_mgr), GFP_KERNEL);

		if (clk_mgr == NULL) {
			BREAK_TO_DEBUGGER();
			return NULL;
		}

		dcn31_clk_mgr_construct(ctx, clk_mgr, pp_smu, dccg);
		return &clk_mgr->base.base;
	}
		break;
	case AMDGPU_FAMILY_GC_10_3_6: {
		struct clk_mgr_dcn315 *clk_mgr = kzalloc(sizeof(*clk_mgr), GFP_KERNEL);

		if (clk_mgr == NULL) {
			BREAK_TO_DEBUGGER();
			return NULL;
		}

		dcn315_clk_mgr_construct(ctx, clk_mgr, pp_smu, dccg);
		return &clk_mgr->base.base;
	}
		break;
	case AMDGPU_FAMILY_GC_10_3_7: {
		struct clk_mgr_dcn316 *clk_mgr = kzalloc(sizeof(*clk_mgr), GFP_KERNEL);

		if (clk_mgr == NULL) {
			BREAK_TO_DEBUGGER();
			return NULL;
		}

		dcn316_clk_mgr_construct(ctx, clk_mgr, pp_smu, dccg);
		return &clk_mgr->base.base;
	}
		break;
	case AMDGPU_FAMILY_GC_11_0_0: {
	    struct clk_mgr_internal *clk_mgr = kzalloc(sizeof(*clk_mgr), GFP_KERNEL);

	    if (clk_mgr == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	    }

	    dcn32_clk_mgr_construct(ctx, clk_mgr, pp_smu, dccg);
	    return &clk_mgr->base;
	    break;
	}

	case AMDGPU_FAMILY_GC_11_0_1: {
		struct clk_mgr_dcn314 *clk_mgr = kzalloc(sizeof(*clk_mgr), GFP_KERNEL);

		if (clk_mgr == NULL) {
			BREAK_TO_DEBUGGER();
			return NULL;
		}

		dcn314_clk_mgr_construct(ctx, clk_mgr, pp_smu, dccg);
		return &clk_mgr->base.base;
	}
	break;

#endif
	default:
		ASSERT(0); /* Unknown Asic */
		break;
	}

	return NULL;
}

void dc_destroy_clk_mgr(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

#ifdef CONFIG_DRM_AMD_DC_DCN
	switch (clk_mgr_base->ctx->asic_id.chip_family) {
	case FAMILY_NV:
		if (ASICREV_IS_SIENNA_CICHLID_P(clk_mgr_base->ctx->asic_id.hw_internal_rev)) {
			dcn3_clk_mgr_destroy(clk_mgr);
		} else if (ASICREV_IS_DIMGREY_CAVEFISH_P(clk_mgr_base->ctx->asic_id.hw_internal_rev)) {
			dcn3_clk_mgr_destroy(clk_mgr);
		}
		if (ASICREV_IS_BEIGE_GOBY_P(clk_mgr_base->ctx->asic_id.hw_internal_rev)) {
			dcn3_clk_mgr_destroy(clk_mgr);
		}
		break;

	case FAMILY_VGH:
		if (ASICREV_IS_VANGOGH(clk_mgr_base->ctx->asic_id.hw_internal_rev))
			vg_clk_mgr_destroy(clk_mgr);
		break;

	case FAMILY_YELLOW_CARP:
		dcn31_clk_mgr_destroy(clk_mgr);
		break;

	case AMDGPU_FAMILY_GC_10_3_6:
		dcn315_clk_mgr_destroy(clk_mgr);
		break;

	case AMDGPU_FAMILY_GC_10_3_7:
		dcn316_clk_mgr_destroy(clk_mgr);
		break;

	case AMDGPU_FAMILY_GC_11_0_0:
		dcn32_clk_mgr_destroy(clk_mgr);
		break;

	case AMDGPU_FAMILY_GC_11_0_1:
		dcn314_clk_mgr_destroy(clk_mgr);
		break;

	default:
		break;
	}
#endif

	kfree(clk_mgr);
}

