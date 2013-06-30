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

#ifndef _GMA_DISPLAY_H_
#define _GMA_DISPLAY_H_

struct gma_clock_t {
	/* given values */
	int n;
	int m1, m2;
	int p1, p2;
	/* derived values */
	int dot;
	int vco;
	int m;
	int p;
};

struct gma_range_t {
	int min, max;
};

struct gma_p2_t {
	int dot_limit;
	int p2_slow, p2_fast;
};

struct gma_limit_t {
	struct gma_range_t dot, vco, n, m, m1, m2, p, p1;
	struct gma_p2_t p2;
	bool (*find_pll)(const struct gma_limit_t *, struct drm_crtc *,
			 int target, int refclk,
			 struct gma_clock_t *best_clock);
};

struct gma_clock_funcs {
	void (*clock)(int refclk, struct gma_clock_t *clock);
	const struct gma_limit_t *(*limit)(struct drm_crtc *crtc, int refclk);
	bool (*pll_is_valid)(struct drm_crtc *crtc,
			     const struct gma_limit_t *limit,
			     struct gma_clock_t *clock);
};

/* Common pipe related functions */
extern bool gma_pipe_has_type(struct drm_crtc *crtc, int type);

/* Common clock related functions */
extern const struct gma_limit_t *gma_limit(struct drm_crtc *crtc, int refclk);
extern void gma_clock(int refclk, struct gma_clock_t *clock);
extern bool gma_pll_is_valid(struct drm_crtc *crtc,
			     const struct gma_limit_t *limit,
			     struct gma_clock_t *clock);
extern bool gma_find_best_pll(const struct gma_limit_t *limit,
			      struct drm_crtc *crtc, int target, int refclk,
			      struct gma_clock_t *best_clock);
#endif
