/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "msm_drv.h"
#include "mdp4_kms.h"

#define FMT(name, a, r, g, b, e0, e1, e2, e3, alpha, tight, c, cnt) { \
		.base = { .pixel_format = DRM_FORMAT_ ## name }, \
		.bpc_a = BPC ## a ## A,                          \
		.bpc_r = BPC ## r,                               \
		.bpc_g = BPC ## g,                               \
		.bpc_b = BPC ## b,                               \
		.unpack = { e0, e1, e2, e3 },                    \
		.alpha_enable = alpha,                           \
		.unpack_tight = tight,                           \
		.cpp = c,                                        \
		.unpack_count = cnt,                             \
	}

#define BPC0A 0

static const struct mdp4_format formats[] = {
	/*  name      a  r  g  b   e0 e1 e2 e3  alpha   tight  cpp cnt */
	FMT(ARGB8888, 8, 8, 8, 8,  1, 0, 2, 3,  true,   true,  4,  4),
	FMT(XRGB8888, 8, 8, 8, 8,  1, 0, 2, 3,  false,  true,  4,  4),
	FMT(RGB888,   0, 8, 8, 8,  1, 0, 2, 0,  false,  true,  3,  3),
	FMT(BGR888,   0, 8, 8, 8,  2, 0, 1, 0,  false,  true,  3,  3),
	FMT(RGB565,   0, 5, 6, 5,  1, 0, 2, 0,  false,  true,  2,  3),
	FMT(BGR565,   0, 5, 6, 5,  2, 0, 1, 0,  false,  true,  2,  3),
};

uint32_t mdp4_get_formats(enum mdp4_pipe pipe_id, uint32_t *pixel_formats,
		uint32_t max_formats)
{
	uint32_t i;
	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		const struct mdp4_format *f = &formats[i];

		if (i == max_formats)
			break;

		pixel_formats[i] = f->base.pixel_format;
	}

	return i;
}

const struct msm_format *mdp4_get_format(struct msm_kms *kms, uint32_t format)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		const struct mdp4_format *f = &formats[i];
		if (f->base.pixel_format == format)
			return &f->base;
	}
	return NULL;
}
