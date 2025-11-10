// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/debugfs.h>

#include <drm/drm_print.h>

#include "intel_cdclk.h"
#include "intel_display_core.h"
#include "intel_display_types.h"
#include "intel_vblank.h"
#include "intel_vdsc.h"
#include "skl_prefill.h"
#include "skl_scaler.h"
#include "skl_watermark.h"

static unsigned int prefill_usecs_to_lines(const struct intel_crtc_state *crtc_state,
					   unsigned int usecs)
{
	const struct drm_display_mode *pipe_mode = &crtc_state->hw.pipe_mode;

	return DIV_ROUND_UP_ULL(mul_u32_u32(pipe_mode->crtc_clock, usecs << 16),
				pipe_mode->crtc_htotal * 1000);
}

static void prefill_init(struct skl_prefill_ctx *ctx,
			 const struct intel_crtc_state *crtc_state)
{
	memset(ctx, 0, sizeof(*ctx));

	ctx->prefill.fixed = crtc_state->framestart_delay << 16;

	/* 20 usec for translation walks/etc. */
	ctx->prefill.fixed += prefill_usecs_to_lines(crtc_state, 20);

	ctx->prefill.dsc = intel_vdsc_prefill_lines(crtc_state);
}

static void prefill_init_nocdclk_worst(struct skl_prefill_ctx *ctx,
				       const struct intel_crtc_state *crtc_state)
{
	prefill_init(ctx, crtc_state);

	ctx->prefill.wm0 = skl_wm0_prefill_lines_worst(crtc_state);
	ctx->prefill.scaler_1st = skl_scaler_1st_prefill_lines_worst(crtc_state);
	ctx->prefill.scaler_2nd = skl_scaler_2nd_prefill_lines_worst(crtc_state);

	ctx->adj.scaler_1st = skl_scaler_1st_prefill_adjustment_worst(crtc_state);
	ctx->adj.scaler_2nd = skl_scaler_2nd_prefill_adjustment_worst(crtc_state);
}

static void prefill_init_nocdclk(struct skl_prefill_ctx *ctx,
				 const struct intel_crtc_state *crtc_state)
{
	prefill_init(ctx, crtc_state);

	ctx->prefill.wm0 = skl_wm0_prefill_lines(crtc_state);
	ctx->prefill.scaler_1st = skl_scaler_1st_prefill_lines(crtc_state);
	ctx->prefill.scaler_2nd = skl_scaler_2nd_prefill_lines(crtc_state);

	ctx->adj.scaler_1st = skl_scaler_1st_prefill_adjustment(crtc_state);
	ctx->adj.scaler_2nd = skl_scaler_2nd_prefill_adjustment(crtc_state);
}

static unsigned int prefill_adjust(unsigned int value, unsigned int factor)
{
	return DIV_ROUND_UP_ULL(mul_u32_u32(value, factor), 0x10000);
}

static unsigned int prefill_lines_nocdclk(const struct skl_prefill_ctx *ctx)
{
	unsigned int prefill = 0;

	prefill += ctx->prefill.dsc;
	prefill = prefill_adjust(prefill, ctx->adj.scaler_2nd);

	prefill += ctx->prefill.scaler_2nd;
	prefill = prefill_adjust(prefill, ctx->adj.scaler_1st);

	prefill += ctx->prefill.scaler_1st;
	prefill += ctx->prefill.wm0;

	return prefill;
}

static unsigned int prefill_lines_cdclk(const struct skl_prefill_ctx *ctx)
{
	return prefill_adjust(prefill_lines_nocdclk(ctx), ctx->adj.cdclk);
}

static unsigned int prefill_lines_full(const struct skl_prefill_ctx *ctx)
{
	return ctx->prefill.fixed + prefill_lines_cdclk(ctx);
}

void skl_prefill_init_worst(struct skl_prefill_ctx *ctx,
			    const struct intel_crtc_state *crtc_state)
{
	prefill_init_nocdclk_worst(ctx, crtc_state);

	ctx->adj.cdclk = intel_cdclk_prefill_adjustment_worst(crtc_state);

	ctx->prefill.full = prefill_lines_full(ctx);
}

void skl_prefill_init(struct skl_prefill_ctx *ctx,
		      const struct intel_crtc_state *crtc_state)
{
	prefill_init_nocdclk(ctx, crtc_state);

	ctx->adj.cdclk = intel_cdclk_prefill_adjustment(crtc_state);

	ctx->prefill.full = prefill_lines_full(ctx);
}

static unsigned int prefill_lines_with_latency(const struct skl_prefill_ctx *ctx,
					       const struct intel_crtc_state *crtc_state,
					       unsigned int latency_us)
{
	return ctx->prefill.full + prefill_usecs_to_lines(crtc_state, latency_us);
}

int skl_prefill_min_guardband(const struct skl_prefill_ctx *ctx,
			      const struct intel_crtc_state *crtc_state,
			      unsigned int latency_us)
{
	unsigned int prefill = prefill_lines_with_latency(ctx, crtc_state, latency_us);

	return DIV_ROUND_UP(prefill, 0x10000);
}

static unsigned int prefill_guardband(const struct intel_crtc_state *crtc_state)
{
	return intel_crtc_vblank_length(crtc_state) << 16;
}

bool skl_prefill_vblank_too_short(const struct skl_prefill_ctx *ctx,
				  const struct intel_crtc_state *crtc_state,
				  unsigned int latency_us)
{
	unsigned int guardband = prefill_guardband(crtc_state);
	unsigned int prefill = prefill_lines_with_latency(ctx, crtc_state, latency_us);

	return guardband < prefill;
}

int skl_prefill_min_cdclk(const struct skl_prefill_ctx *ctx,
			  const struct intel_crtc_state *crtc_state)
{
	unsigned int prefill_unadjusted = prefill_lines_nocdclk(ctx);
	unsigned int prefill_available = prefill_guardband(crtc_state) - ctx->prefill.fixed;

	return intel_cdclk_min_cdclk_for_prefill(crtc_state, prefill_unadjusted,
						 prefill_available);
}
