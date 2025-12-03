/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef __SKL_PREFILL_H__
#define __SKL_PREFILL_H__

#include <linux/types.h>

struct intel_crtc_state;

struct skl_prefill_ctx {
	/* .16 scanlines */
	struct {
		unsigned int fixed;
		unsigned int wm0;
		unsigned int scaler_1st;
		unsigned int scaler_2nd;
		unsigned int dsc;
		unsigned int full;
	} prefill;

	/* .16 adjustment factors */
	struct {
		unsigned int cdclk;
		unsigned int scaler_1st;
		unsigned int scaler_2nd;
	} adj;
};

void skl_prefill_init_worst(struct skl_prefill_ctx *ctx,
			    const struct intel_crtc_state *crtc_state);
void skl_prefill_init(struct skl_prefill_ctx *ctx,
		      const struct intel_crtc_state *crtc_state);

bool skl_prefill_vblank_too_short(const struct skl_prefill_ctx *ctx,
				  const struct intel_crtc_state *crtc_state,
				  unsigned int latency_us);
int skl_prefill_min_guardband(const struct skl_prefill_ctx *ctx,
			      const struct intel_crtc_state *crtc_state,
			      unsigned int latency_us);
int skl_prefill_min_cdclk(const struct skl_prefill_ctx *ctx,
			  const struct intel_crtc_state *crtc_state);

#endif /* __SKL_PREFILL_H__ */
