/*
 * Copyright © 2006-2011 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 *	Patrik Jakobsson <patrik.r.jakobsson@gmail.com>
 */

#include <drm/drmP.h>
#include "gma_display.h"
#include "psb_intel_drv.h"
#include "psb_intel_reg.h"
#include "psb_drv.h"

/**
 * Returns whether any output on the specified pipe is of the specified type
 */
bool gma_pipe_has_type(struct drm_crtc *crtc, int type)
{
	struct drm_device *dev = crtc->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *l_entry;

	list_for_each_entry(l_entry, &mode_config->connector_list, head) {
		if (l_entry->encoder && l_entry->encoder->crtc == crtc) {
			struct psb_intel_encoder *psb_intel_encoder =
					psb_intel_attached_encoder(l_entry);
			if (psb_intel_encoder->type == type)
				return true;
		}
	}

	return false;
}

#define GMA_PLL_INVALID(s) { /* DRM_ERROR(s); */ return false; }

bool gma_pll_is_valid(struct drm_crtc *crtc,
		      const struct gma_limit_t *limit,
		      struct gma_clock_t *clock)
{
	if (clock->p1 < limit->p1.min || limit->p1.max < clock->p1)
		GMA_PLL_INVALID("p1 out of range");
	if (clock->p < limit->p.min || limit->p.max < clock->p)
		GMA_PLL_INVALID("p out of range");
	if (clock->m2 < limit->m2.min || limit->m2.max < clock->m2)
		GMA_PLL_INVALID("m2 out of range");
	if (clock->m1 < limit->m1.min || limit->m1.max < clock->m1)
		GMA_PLL_INVALID("m1 out of range");
	/* On CDV m1 is always 0 */
	if (clock->m1 <= clock->m2 && clock->m1 != 0)
		GMA_PLL_INVALID("m1 <= m2 && m1 != 0");
	if (clock->m < limit->m.min || limit->m.max < clock->m)
		GMA_PLL_INVALID("m out of range");
	if (clock->n < limit->n.min || limit->n.max < clock->n)
		GMA_PLL_INVALID("n out of range");
	if (clock->vco < limit->vco.min || limit->vco.max < clock->vco)
		GMA_PLL_INVALID("vco out of range");
	/* XXX: We may need to be checking "Dot clock"
	 * depending on the multiplier, connector, etc.,
	 * rather than just a single range.
	 */
	if (clock->dot < limit->dot.min || limit->dot.max < clock->dot)
		GMA_PLL_INVALID("dot out of range");

	return true;
}

bool gma_find_best_pll(const struct gma_limit_t *limit,
		       struct drm_crtc *crtc, int target, int refclk,
		       struct gma_clock_t *best_clock)
{
	struct drm_device *dev = crtc->dev;
	const struct gma_clock_funcs *clock_funcs =
					to_psb_intel_crtc(crtc)->clock_funcs;
	struct gma_clock_t clock;
	int err = target;

	if (gma_pipe_has_type(crtc, INTEL_OUTPUT_LVDS) &&
	    (REG_READ(LVDS) & LVDS_PORT_EN) != 0) {
		/*
		 * For LVDS, if the panel is on, just rely on its current
		 * settings for dual-channel.  We haven't figured out how to
		 * reliably set up different single/dual channel state, if we
		 * even can.
		 */
		if ((REG_READ(LVDS) & LVDS_CLKB_POWER_MASK) ==
		    LVDS_CLKB_POWER_UP)
			clock.p2 = limit->p2.p2_fast;
		else
			clock.p2 = limit->p2.p2_slow;
	} else {
		if (target < limit->p2.dot_limit)
			clock.p2 = limit->p2.p2_slow;
		else
			clock.p2 = limit->p2.p2_fast;
	}

	memset(best_clock, 0, sizeof(*best_clock));

	/* m1 is always 0 on CDV so the outmost loop will run just once */
	for (clock.m1 = limit->m1.min; clock.m1 <= limit->m1.max; clock.m1++) {
		for (clock.m2 = limit->m2.min;
		     (clock.m2 < clock.m1 || clock.m1 == 0) &&
		      clock.m2 <= limit->m2.max; clock.m2++) {
			for (clock.n = limit->n.min;
			     clock.n <= limit->n.max; clock.n++) {
				for (clock.p1 = limit->p1.min;
				     clock.p1 <= limit->p1.max;
				     clock.p1++) {
					int this_err;

					clock_funcs->clock(refclk, &clock);

					if (!clock_funcs->pll_is_valid(crtc,
								limit, &clock))
						continue;

					this_err = abs(clock.dot - target);
					if (this_err < err) {
						*best_clock = clock;
						err = this_err;
					}
				}
			}
		}
	}

	return err != target;
}
